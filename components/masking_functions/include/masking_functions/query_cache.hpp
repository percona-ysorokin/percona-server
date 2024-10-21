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

#ifndef MASKING_FUNCTIONS_QUERY_CACHE_HPP
#define MASKING_FUNCTIONS_QUERY_CACHE_HPP

#include "masking_functions/query_cache_fwd.hpp"

#include <string>

#include "masking_functions/basic_sql_context_builder_fwd.hpp"
#include "masking_functions/query_builder_fwd.hpp"
#include "masking_functions/query_cache_core_fwd.hpp"

namespace masking_functions {

class query_cache {
 public:
  query_cache(const query_cache_core_ptr &core,
              const basic_sql_context_builder_ptr &sql_ctx_builder,
              const query_builder_ptr &sql_query_builder);

  query_cache(const query_cache &other) = delete;
  query_cache(query_cache &&other) = delete;
  query_cache &operator=(const query_cache &other) = delete;
  query_cache &operator=(query_cache &&other) = delete;
  ~query_cache();

  bool contains(const std::string &dictionary_name,
                const std::string &term) const;
  // returns a copy of the string to avoid race conditions
  // an empty string is returned if the dictionary does not exist
  std::string get_random(const std::string &dictionary_name) const;
  bool remove(const std::string &dictionary_name);
  bool remove(const std::string &dictionary_name, const std::string &term);
  bool insert(const std::string &dictionary_name, const std::string &term);

  void reload_cache();

  basic_sql_context_builder_ptr get_sql_context_builder() const {
    return sql_ctx_builder_;
  }

 private:
  query_cache_core_ptr core_;
  basic_sql_context_builder_ptr sql_ctx_builder_;
  query_builder_ptr sql_query_builder_;
};

}  // namespace masking_functions

#endif  // MASKING_FUNCTIONS_QUERY_CACHE_HPP
