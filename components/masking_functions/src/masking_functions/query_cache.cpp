/* Copyright (c) 2024 Percona LLC and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

#include "masking_functions/query_cache.hpp"

#include <string_view>

#include "masking_functions/bookshelf.hpp"
#include "masking_functions/command_service_tuple.hpp"
#include "masking_functions/primitive_singleton.hpp"
#include "masking_functions/query_builder.hpp"
#include "masking_functions/sql_context.hpp"

namespace masking_functions {

using global_command_services = masking_functions::primitive_singleton<
    masking_functions::command_service_tuple>;

query_cache::query_cache(query_builder_ptr query_builder)
    : dict_query_builder_{std::move(query_builder)},
      dict_cache_{},
      dict_cache_mutex_{} {}

query_cache::~query_cache() = default;

bool query_cache::contains(const std::string &dictionary_name,
                           const std::string &term) const {
  shared_lock_type read_lock{};
  unique_lock_type write_lock{};
  const auto &acquired_dict_cache{
      acquire_dict_cache_shared(read_lock, write_lock)};
  return acquired_dict_cache.contains(dictionary_name, term);
}

std::string query_cache::get_random(const std::string &dictionary_name) const {
  shared_lock_type read_lock{};
  unique_lock_type write_lock{};
  const auto &acquired_dict_cache{
      acquire_dict_cache_shared(read_lock, write_lock)};
  return std::string{acquired_dict_cache.get_random(dictionary_name)};
}

bool query_cache::remove(const std::string &dictionary_name) {
  masking_functions::sql_context sql_ctx{global_command_services::instance()};
  auto query{dict_query_builder_->delete_for_dictionary(dictionary_name)};

  unique_lock_type write_lock{};
  auto &acquired_dict_cache{acquire_dict_cache_unique(write_lock)};

  // there is a chance that a user can delete the dictionary from the
  // dictionary table directly (not via UDF function) and execute_dml()
  // will return false here, whereas cache operation will return true -
  // this is why we rely only on the result of the cache operation
  sql_ctx.execute_dml(query);
  return acquired_dict_cache.remove(dictionary_name);
}

bool query_cache::remove(const std::string &dictionary_name,
                         const std::string &term) {
  masking_functions::sql_context sql_ctx{global_command_services::instance()};
  auto query{dict_query_builder_->delete_for_dictionary_and_term(
      dictionary_name, term)};

  unique_lock_type write_lock{};
  auto &acquired_dict_cache{acquire_dict_cache_unique(write_lock)};

  // similarly to another remove() method, we ignore the result of the
  // sql operation and rely only on the result of the cache modification
  sql_ctx.execute_dml(query);
  return acquired_dict_cache.remove(dictionary_name, term);
}

bool query_cache::insert(const std::string &dictionary_name,
                         const std::string &term) {
  masking_functions::sql_context sql_ctx{global_command_services::instance()};
  auto query{dict_query_builder_->insert_ignore_record(dictionary_name, term)};

  unique_lock_type write_lock{};
  auto &acquired_dict_cache{acquire_dict_cache_unique(write_lock)};

  // here, as cache insert may throw, we start the 2-phase operation
  // with this cache insert because it can be easily reversed without throwing
  const auto result{acquired_dict_cache.insert(dictionary_name, term)};
  try {
    sql_ctx.execute_dml(query);
  } catch (...) {
    dict_cache_->remove(dictionary_name, term);
    throw;
  }

  return result;
}

void query_cache::reload_cache() {
  unique_lock_type dict_cache_write_lock{dict_cache_mutex_};

  auto local_dict_cache{create_dict_cache_internal()};
  if (!local_dict_cache) {
    throw std::runtime_error{"Cannot load dictionary cache"};
  }

  dict_cache_ = std::move(local_dict_cache);
}

bookshelf_ptr query_cache::create_dict_cache_internal() const {
  bookshelf_ptr result;
  try {
    masking_functions::sql_context sql_ctx{global_command_services::instance()};
    auto query{dict_query_builder_->select_all_from_dictionary()};
    auto local_dict_cache{std::make_unique<bookshelf>()};
    sql_context::row_callback<2> result_inserter{[&terms = *local_dict_cache](
                                                     const auto &field_values) {
      terms.insert(std::string{field_values[0]}, std::string{field_values[1]});
    }};
    sql_ctx.execute_select(query, result_inserter);
    result = std::move(local_dict_cache);
  } catch (...) {
  }

  return result;
}

const bookshelf &query_cache::acquire_dict_cache_shared(
    shared_lock_type &read_lock, unique_lock_type &write_lock) const {
  read_lock = shared_lock_type{dict_cache_mutex_};
  if (!dict_cache_) {
    // upgrading to a unique_lock
    read_lock.unlock();
    acquire_dict_cache_unique(write_lock);
  }
  return *dict_cache_;
}

bookshelf &query_cache::acquire_dict_cache_unique(
    unique_lock_type &write_lock) const {
  write_lock = unique_lock_type{dict_cache_mutex_};
  if (!dict_cache_) {
    auto local_dict_cache{create_dict_cache_internal()};
    if (!local_dict_cache) {
      throw std::runtime_error{"Cannot load dictionary cache"};
    }
    dict_cache_ = std::move(local_dict_cache);
  }
  return *dict_cache_;
}

}  // namespace masking_functions
