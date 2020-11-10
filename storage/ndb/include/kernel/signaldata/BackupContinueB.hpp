/*
   Copyright (c) 2003, 2020, Oracle and/or its affiliates.

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

#ifndef BACKUP_CONTINUEB_H
#define BACKUP_CONTINUEB_H

#include "SignalData.hpp"

#define JAM_FILE_ID 47


class BackupContinueB {
  /**
   * Sender(s)/Reciver(s)
   */
  friend class Backup;
  friend bool printCONTINUEB_BACKUP(FILE * output, const Uint32 * theData, Uint32 len);
private:
  enum {
    START_FILE_THREAD = 0,
    BUFFER_UNDERFLOW  = 1,
    BUFFER_FULL_SCAN  = 2,
    BUFFER_FULL_FRAG_COMPLETE = 3,
    BUFFER_FULL_META  = 4,
    BACKUP_FRAGMENT_INFO = 5,
    RESET_DISK_SPEED_COUNTER = 6,
    ZGET_NEXT_FRAGMENT = 7,
    ZDELETE_LCP_FILE = 8,
    ZCHECK_PGMAN_PREP_LCP = 9
  };
};


#undef JAM_FILE_ID

#endif
