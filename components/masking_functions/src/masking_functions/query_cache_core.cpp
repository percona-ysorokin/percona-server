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

#include "masking_functions/query_cache_core.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "masking_functions/basic_sql_context_builder.hpp"
#include "masking_functions/bookshelf.hpp"
#include "masking_functions/command_service_tuple.hpp"
#include "masking_functions/query_builder.hpp"
#include "masking_functions/sql_context.hpp"

namespace masking_functions {

query_cache_core::query_cache_core() : dict_cache_{}, dict_cache_mutex_{} {}

query_cache_core::~query_cache_core() = default;

bool query_cache_core::contains(
    const basic_sql_context_builder &sql_ctx_builder,
    const query_builder &sql_query_builder, const std::string &dictionary_name,
    const std::string &term) const {
  sql_context_ptr sql_ctx;
  shared_lock_type read_lock{};
  unique_lock_type write_lock{};
  const auto &acquired_dict_cache{acquire_dict_cache_shared(
      sql_ctx_builder, sql_query_builder, sql_ctx, read_lock, write_lock)};
  return acquired_dict_cache.contains(dictionary_name, term);
}

std::string query_cache_core::get_random(
    const basic_sql_context_builder &sql_ctx_builder,
    const query_builder &sql_query_builder,
    const std::string &dictionary_name) const {
  sql_context_ptr sql_ctx;
  shared_lock_type read_lock{};
  unique_lock_type write_lock{};
  const auto &acquired_dict_cache{acquire_dict_cache_shared(
      sql_ctx_builder, sql_query_builder, sql_ctx, read_lock, write_lock)};
  return std::string{acquired_dict_cache.get_random(dictionary_name)};
}

bool query_cache_core::remove(const basic_sql_context_builder &sql_ctx_builder,
                              const query_builder &sql_query_builder,
                              const std::string &dictionary_name) {
  sql_context_ptr sql_ctx;
  auto query{sql_query_builder.delete_for_dictionary(dictionary_name)};

  unique_lock_type write_lock{};
  auto &acquired_dict_cache{acquire_dict_cache_unique(
      sql_ctx_builder, sql_query_builder, sql_ctx, write_lock)};

  if (!sql_ctx) {
    sql_ctx = sql_ctx_builder.build();
  }

  // there is a chance that a user can delete the dictionary from the
  // dictionary table directly (not via UDF function) and execute_dml()
  // will return false here, whereas cache operation will return true -
  // this is why we rely only on the result of the cache operation
  sql_ctx->execute_dml(query);
  return acquired_dict_cache.remove(dictionary_name);
}

bool query_cache_core::remove(const basic_sql_context_builder &sql_ctx_builder,
                              const query_builder &sql_query_builder,
                              const std::string &dictionary_name,
                              const std::string &term) {
  sql_context_ptr sql_ctx;
  auto query{
      sql_query_builder.delete_for_dictionary_and_term(dictionary_name, term)};

  unique_lock_type write_lock{};
  auto &acquired_dict_cache{acquire_dict_cache_unique(
      sql_ctx_builder, sql_query_builder, sql_ctx, write_lock)};

  if (!sql_ctx) {
    sql_ctx = sql_ctx_builder.build();
  }

  // similarly to another remove() method, we ignore the result of the
  // sql operation and rely only on the result of the cache modification
  sql_ctx->execute_dml(query);
  return acquired_dict_cache.remove(dictionary_name, term);
}

bool query_cache_core::insert(const basic_sql_context_builder &sql_ctx_builder,
                              const query_builder &sql_query_builder,
                              const std::string &dictionary_name,
                              const std::string &term) {
  sql_context_ptr sql_ctx;
  auto query{sql_query_builder.insert_ignore_record(dictionary_name, term)};

  unique_lock_type write_lock{};
  auto &acquired_dict_cache{acquire_dict_cache_unique(
      sql_ctx_builder, sql_query_builder, sql_ctx, write_lock)};

  if (!sql_ctx) {
    sql_ctx = sql_ctx_builder.build();
  }

  // here, as cache insert may throw, we start the 2-phase operation
  // with this cache insert because it can be easily reversed without throwing
  const auto result{acquired_dict_cache.insert(dictionary_name, term)};
  try {
    sql_ctx->execute_dml(query);
  } catch (...) {
    dict_cache_->remove(dictionary_name, term);
    throw;
  }

  return result;
}

void query_cache_core::reload_cache(
    const basic_sql_context_builder &sql_ctx_builder,
    const query_builder &sql_query_builder) {
  unique_lock_type dict_cache_write_lock{dict_cache_mutex_};

  std::string error_message;
  auto sql_ctx{sql_ctx_builder.build()};
  auto local_dict_cache{
      create_dict_cache_internal(*sql_ctx, sql_query_builder, error_message)};
  if (!local_dict_cache) {
    throw std::runtime_error{error_message};
  }

  dict_cache_ = std::move(local_dict_cache);
}

bookshelf_ptr query_cache_core::create_dict_cache_internal(
    sql_context &sql_ctx, const query_builder &sql_query_builder,
    std::string &error_message) const {
  bookshelf_ptr result;
  error_message.clear();
  try {
    auto query{sql_query_builder.select_all_from_dictionary()};
    auto local_dict_cache{std::make_unique<bookshelf>()};
    sql_context::row_callback<2> result_inserter{[&terms = *local_dict_cache](
                                                     const auto &field_values) {
      terms.insert(std::string{field_values[0]}, std::string{field_values[1]});
    }};
    sql_ctx.execute_select(query, result_inserter);
    result = std::move(local_dict_cache);
  } catch (const std::exception &e) {
    error_message = e.what();
  } catch (...) {
    error_message =
        "unexpected exception caught while loading dictionary cache";
  }

  return result;
}

const bookshelf &query_cache_core::acquire_dict_cache_shared(
    const basic_sql_context_builder &sql_ctx_builder,
    const query_builder &sql_query_builder, sql_context_ptr &sql_ctx,
    shared_lock_type &read_lock, unique_lock_type &write_lock) const {
  read_lock = shared_lock_type{dict_cache_mutex_};
  if (!dict_cache_) {
    // upgrading to a unique_lock
    read_lock.unlock();
    acquire_dict_cache_unique(sql_ctx_builder, sql_query_builder, sql_ctx,
                              write_lock);
  }
  return *dict_cache_;
}

bookshelf &query_cache_core::acquire_dict_cache_unique(
    const basic_sql_context_builder &sql_ctx_builder,
    const query_builder &sql_query_builder, sql_context_ptr &sql_ctx,
    unique_lock_type &write_lock) const {
  write_lock = unique_lock_type{dict_cache_mutex_};
  if (!dict_cache_) {
    std::string error_message;
    sql_ctx = sql_ctx_builder.build();
    auto local_dict_cache{
        create_dict_cache_internal(*sql_ctx, sql_query_builder, error_message)};
    if (!local_dict_cache) {
      throw std::runtime_error{error_message};
    }
    dict_cache_ = std::move(local_dict_cache);
  }
  return *dict_cache_;
}

}  // namespace masking_functions
