/*
  Copyright (c) 2022, 2024, Oracle and/or its affiliates.

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
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _CORE_FINDER_H_
#define _CORE_FINDER_H_

#include <string>

#include "mysql/harness/filesystem.h"
#include "mysql/harness/stdx/expected.h"
#include "process_launcher.h"

class CoreFinder {
 public:
  using pid_type = mysql_harness::ProcessLauncher::process_id_type;

  CoreFinder(std::string executable, pid_type pid)
      : executable_{std::move(executable)}, pid_{pid} {}

  std::string core_name() const;

 private:
  std::string executable_;
  pid_type pid_;
};

#endif
