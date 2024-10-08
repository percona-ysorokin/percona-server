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

#include <string>

#include "masking_functions/basic_sql_context_builder.hpp"
#include "masking_functions/query_cache_core.hpp"

namespace masking_functions {

query_cache::query_cache(const query_cache_core_ptr &core,
                         const basic_sql_context_builder_ptr &sql_ctx_builder,
                         const query_builder_ptr &sql_query_builder)
    : core_{core},
      sql_ctx_builder_{sql_ctx_builder},
      sql_query_builder_{sql_query_builder} {}

query_cache::~query_cache() = default;

bool query_cache::contains(const std::string &dictionary_name,
                           const std::string &term) const {
  return core_->contains(*sql_ctx_builder_, *sql_query_builder_,
                         dictionary_name, term);
}

std::string query_cache::get_random(const std::string &dictionary_name) const {
  return core_->get_random(*sql_ctx_builder_, *sql_query_builder_,
                           dictionary_name);
}

bool query_cache::remove(const std::string &dictionary_name) {
  return core_->remove(*sql_ctx_builder_, *sql_query_builder_, dictionary_name);
}

bool query_cache::remove(const std::string &dictionary_name,
                         const std::string &term) {
  return core_->remove(*sql_ctx_builder_, *sql_query_builder_, dictionary_name,
                       term);
}

bool query_cache::insert(const std::string &dictionary_name,
                         const std::string &term) {
  return core_->insert(*sql_ctx_builder_, *sql_query_builder_, dictionary_name,
                       term);
}

void query_cache::reload_cache() {
  return core_->reload_cache(*sql_ctx_builder_, *sql_query_builder_);
}

void query_cache::prepare_sql_context_builder() { sql_ctx_builder_->prepare(); }

void query_cache::cleanup_sql_context_builder() { sql_ctx_builder_->cleanup(); }

}  // namespace masking_functions
