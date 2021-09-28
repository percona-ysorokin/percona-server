/*
   Copyright (c) 2014, 2021, Oracle and/or its affiliates.

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
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

#ifndef I_OPTION_CHANGED_LISTENER_INCLUDED
#define I_OPTION_CHANGED_LISTENER_INCLUDED

#include "i_option.h"
#include <string>

namespace Mysql{
namespace Tools{
namespace Base{
namespace Options{

/**
  Interface for listeners on some of option changes.
 */
class I_option_changed_listener
{
public:
  /**
    Called after specified option has name changed.
    It is also called when new option is added, old_name is empty string in
    that case.
   */
  virtual void notify_option_name_changed(
    I_option* source, std::string old_name)= 0;
  /**
    Called after specified option has option ID changed.
    It is also called when new option is added, old_optid is 0 in that case.
   */
  virtual void notify_option_optid_changed(
    I_option* source, uint32 old_optid)= 0;
};

}
}
}
}

#endif
