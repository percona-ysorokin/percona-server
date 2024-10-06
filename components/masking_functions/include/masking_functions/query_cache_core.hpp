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

#ifndef MASKING_FUNCTIONS_QUERY_CACHE_CORE_HPP
#define MASKING_FUNCTIONS_QUERY_CACHE_CORE_HPP

#include "masking_functions/query_cache_core_fwd.hpp"

#include <mutex>
#include <shared_mutex>
#include <string>

#include "masking_functions/basic_sql_context_builder_fwd.hpp"
#include "masking_functions/bookshelf_fwd.hpp"
#include "masking_functions/query_builder_fwd.hpp"
#include "masking_functions/sql_context_fwd.hpp"

namespace masking_functions {

class query_cache_core {
 public:
  query_cache_core();
  query_cache_core(const query_cache_core &other) = delete;
  query_cache_core(query_cache_core &&other) = delete;
  query_cache_core &operator=(const query_cache_core &other) = delete;
  query_cache_core &operator=(query_cache_core &&other) = delete;
  ~query_cache_core();

  bool contains(const basic_sql_context_builder &sql_ctx_builder,
                const query_builder &sql_query_builder,
                const std::string &dictionary_name,
                const std::string &term) const;
  // returns a copy of the string to avoid race conditions
  // an empty string is returned if the dictionary does not exist
  std::string get_random(const basic_sql_context_builder &sql_ctx_builder,
                         const query_builder &sql_query_builder,
                         const std::string &dictionary_name) const;
  bool remove(const basic_sql_context_builder &sql_ctx_builder,
              const query_builder &sql_query_builder,
              const std::string &dictionary_name);
  bool remove(const basic_sql_context_builder &sql_ctx_builder,
              const query_builder &sql_query_builder,
              const std::string &dictionary_name, const std::string &term);
  bool insert(const basic_sql_context_builder &sql_ctx_builder,
              const query_builder &sql_query_builder,
              const std::string &dictionary_name, const std::string &term);

  void reload_cache(const basic_sql_context_builder &sql_ctx_builder,
                    const query_builder &sql_query_builder);

 private:
  mutable bookshelf_ptr dict_cache_;
  mutable std::shared_mutex dict_cache_mutex_;

  bookshelf_ptr create_dict_cache_internal(
      sql_context &sql_ctx, const query_builder &sql_query_builder,
      std::string &error_message) const;

  using shared_lock_type = std::shared_lock<std::shared_mutex>;
  using unique_lock_type = std::unique_lock<std::shared_mutex>;
  const bookshelf &acquire_dict_cache_shared(
      const basic_sql_context_builder &sql_ctx_builder,
      const query_builder &sql_query_builder, sql_context_ptr &sql_ctx,
      shared_lock_type &read_lock, unique_lock_type &write_lock) const;
  bookshelf &acquire_dict_cache_unique(
      const basic_sql_context_builder &sql_ctx_builder,
      const query_builder &sql_query_builder, sql_context_ptr &sql_ctx,
      unique_lock_type &write_lock) const;
};

}  // namespace masking_functions

#endif  // MASKING_FUNCTIONS_QUERY_CACHE_CORE_HPP
