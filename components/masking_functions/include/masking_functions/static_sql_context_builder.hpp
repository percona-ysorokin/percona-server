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

#ifndef MASKING_FUNCTIONS_STATIC_SQL_CONTEXT_BUILDER_HPP
#define MASKING_FUNCTIONS_STATIC_SQL_CONTEXT_BUILDER_HPP

#include "masking_functions/basic_sql_context_builder.hpp"

#include "masking_functions/command_service_tuple_fwd.hpp"
#include "masking_functions/sql_context_fwd.hpp"

namespace masking_functions {

class static_sql_context_builder : public basic_sql_context_builder {
 public:
  static_sql_context_builder(const command_service_tuple &services,
                             sql_context_registry_access registry_locking_mode)
      : basic_sql_context_builder{services},
        registry_locking_mode_{registry_locking_mode},
        static_instance_{} {}

 private:
  sql_context_registry_access registry_locking_mode_;
  sql_context_ptr static_instance_;

  void do_prepare() override;
  sql_context_ptr do_build() const override;
  void do_cleanup() override;
};

}  // namespace masking_functions

#endif  // MASKING_FUNCTIONS_STATIC_SQL_CONTEXT_BUILDER_HPP
