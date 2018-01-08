/* Copyright (c) 2013, 2017, Oracle and/or its affiliates. All rights reserved.

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

#ifndef BOOTSTRAP_H
#define BOOTSTRAP_H

#include "mysql/thread_type.h"              // enum_thread_type

struct MYSQL_FILE;

class THD;

namespace bootstrap {

/* Bootstrap handler functor */
typedef bool (*bootstrap_functor)(THD *thd);

/**
  Create a thread to execute all commands from the submitted file.
  By providing an explicit bootstrap handler functor, the default
  behavior of reading and executing SQL commands from the submitted
  file may be customized.

  @param file         File providing SQL statements, if non-NULL
  @param boot_handler Optional functor for customized handling
  @param thread_type  Bootstrap thread type.

  @return             Operation outcome, 0 if no errors
*/
bool run_bootstrap_thread(MYSQL_FILE *file, bootstrap_functor boot_handler,
                          enum_thread_type thread_type);
}

#endif // BOOTSTRAP_H
