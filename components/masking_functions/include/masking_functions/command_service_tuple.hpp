/* Copyright (c) 2018, 2019 Francisco Miguel Biete Banon. All rights reserved.
   Copyright (c) 2023 Percona LLC and/or its affiliates. All rights reserved.

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

#ifndef MASKING_FUNCTIONS_COMMAND_SERVICE_TUPLE_HPP
#define MASKING_FUNCTIONS_COMMAND_SERVICE_TUPLE_HPP

#include <mysql/components/service.h>

#include <mysql/components/services/mysql_command_services.h>

#include "masking_functions/command_service_tuple_fwd.hpp"

namespace masking_functions {

struct command_service_tuple {
  SERVICE_TYPE(mysql_command_query) * query;
  SERVICE_TYPE(mysql_command_query_result) * query_result;
  SERVICE_TYPE(mysql_command_options) * options;
  SERVICE_TYPE(mysql_command_factory) * factory;
};

}  // namespace masking_functions

#endif  // MASKING_FUNCTIONS_COMMAND_SERVICE_TUPLE_HPP
