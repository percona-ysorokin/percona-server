/* Copyright (c) 2023 Percona LLC and/or its affiliates. All rights reserved.

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

#ifndef MASKING_FUNCTIONS_BASIC_SQL_CONTEXT_BUILDER_HPP
#define MASKING_FUNCTIONS_BASIC_SQL_CONTEXT_BUILDER_HPP

#include "masking_functions/basic_sql_context_builder_fwd.hpp"

#include "masking_functions/command_service_tuple_fwd.hpp"
#include "masking_functions/sql_context_fwd.hpp"

namespace masking_functions {

class basic_sql_context_builder {
 public:
  basic_sql_context_builder(const basic_sql_context_builder &) = delete;
  basic_sql_context_builder &operator=(const basic_sql_context_builder &) =
      delete;
  basic_sql_context_builder(basic_sql_context_builder &&) = delete;
  basic_sql_context_builder &operator=(basic_sql_context_builder &&) = delete;

  virtual ~basic_sql_context_builder() = default;

  void prepare() { do_prepare(); }
  sql_context_ptr build() const { return do_build(); }
  void cleanup() { do_cleanup(); }

 protected:
  basic_sql_context_builder(const command_service_tuple &services)
      : services_{&services} {}

  const command_service_tuple &get_services() const noexcept {
    return *services_;
  }

 private:
  const command_service_tuple *services_;

  virtual void do_prepare() = 0;
  virtual sql_context_ptr do_build() const = 0;
  virtual void do_cleanup() = 0;
};

}  // namespace masking_functions

#endif  // MASKING_FUNCTIONS_BASIC_SQL_CONTEXT_BUILDER_HPP
