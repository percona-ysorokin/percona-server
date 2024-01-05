/* Copyright (c) 2017, 2024, Oracle and/or its affiliates.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License, version 2.0,
as published by the Free Software Foundation.

This program is also distributed with certain software (including
but not limited to OpenSSL) that is licensed under separate terms,
as designated in a particular file or component or in included license
documentation.  The authors of MySQL hereby grant you an additional
permission to link the program and your derivative works with the
separately licensed software that they have included with MySQL.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License, version 2.0, for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef COMPONENT_STATUS_VAR_SERVICE_H
#define COMPONENT_STATUS_VAR_SERVICE_H

#include <mysql/components/service.h>
#include <mysql/status_var.h>

/**
  Component status variables as a service to mysql_server component
*/

struct SHOW_VAR;

/**
  Service to register variable and unregister variable.
*/
BEGIN_SERVICE_DEFINITION(status_variable_registration)

/**
  Register status variable.

  @param  status_var fully constructed status variable object.
  @return Status of performed operation
  @retval false success
  @retval true failure

  Note: Please see the components/test/test_status_var_service.cc file,
  to know how to construct status variables for different variable types.
*/
DECLARE_BOOL_METHOD(register_variable, (SHOW_VAR * status_var));

/**
  Unregister's status variable.
  @param  status_var SHOW_VAR object with only the name of the variable,
                     which has to be removed from the global list.
  @return Status of performed operation
  @retval false success
  @retval true failure
*/
DECLARE_BOOL_METHOD(unregister_variable, (SHOW_VAR * status_var));

END_SERVICE_DEFINITION(status_variable_registration)

#endif /* COMPONENT_STATUS_VAR_SERVICE_H */
