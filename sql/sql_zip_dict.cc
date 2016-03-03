/* Copyright (c) 2000, 2010, Oracle and/or its affiliates. All rights reserved.

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

/* create and drop zip_dicts */

#include "sql_zip_dict.h"
#include "sql_table.h"                          // write_bin_log
#include "sql_class.h"                          // THD

int mysql_create_zip_dict(THD* thd, const char* name, const char* data)
{
  int error= HA_ADMIN_NOT_IMPLEMENTED;

  DBUG_ENTER("mysql_create_zip_dict");
  handlerton *hton= ha_default_handlerton(thd);

  if(!hton->create_zip_dict)
  {
    my_error(ER_ILLEGAL_HA_CREATE_OPTION, MYF(0),
             ha_resolve_storage_engine_name(hton),
             "ZIP_DICT");
    DBUG_RETURN(error);
  }

  
  if(!hton->create_zip_dict(hton, thd, name, data))
  {
    error = HA_ADMIN_FAILED;
    my_error(error, MYF(0));
    DBUG_RETURN(error);
  }

  error = write_bin_log(thd, FALSE, thd->query(), thd->query_length());
  DBUG_RETURN(error);
}
