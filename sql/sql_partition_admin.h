/* Copyright (c) 2010, 2023, Oracle and/or its affiliates.

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

#ifndef SQL_PARTITION_ADMIN_H
#define SQL_PARTITION_ADMIN_H

#include "my_global.h"
#include "sql_cmd.h"                   // Sql_cmd
#include "sql_alter.h"                 // Sql_cmd_alter_table
#include "sql_admin.h"                 // Sql_cmd_analyze_table
#include "sql_truncate.h"              // Sql_cmd_truncate_table

/**
  Class that represents the ALTER TABLE t1 EXCHANGE PARTITION p
                            WITH TABLE t2 statement.
*/
class Sql_cmd_alter_table_exchange_partition : public Sql_cmd_common_alter_table
{
public:
  /**
    Constructor, used to represent a ALTER TABLE EXCHANGE PARTITION statement.
  */
  Sql_cmd_alter_table_exchange_partition()
    : Sql_cmd_common_alter_table()
  {}

  ~Sql_cmd_alter_table_exchange_partition()
  {}

  bool execute(THD *thd);

private:
  bool exchange_partition(THD *thd, TABLE_LIST *, Alter_info *);
};


/**
  Class that represents the ALTER TABLE t1 ANALYZE PARTITION p statement.
*/
class Sql_cmd_alter_table_analyze_partition : public Sql_cmd_analyze_table
{
public:
  /**
    Constructor, used to represent a ALTER TABLE ANALYZE PARTITION statement.
  */
  Sql_cmd_alter_table_analyze_partition()
    : Sql_cmd_analyze_table()
  {}

  ~Sql_cmd_alter_table_analyze_partition()
  {}

  bool execute(THD *thd);

  /* Override SQLCOM_ANALYZE, since it is an ALTER command */
  virtual enum_sql_command sql_command_code() const
  {
    return SQLCOM_ALTER_TABLE;
  }
};


/**
  Class that represents the ALTER TABLE t1 CHECK PARTITION p statement.
*/
class Sql_cmd_alter_table_check_partition : public Sql_cmd_check_table
{
public:
  /**
    Constructor, used to represent a ALTER TABLE CHECK PARTITION statement.
  */
  Sql_cmd_alter_table_check_partition()
    : Sql_cmd_check_table()
  {}

  ~Sql_cmd_alter_table_check_partition()
  {}

  bool execute(THD *thd);

  /* Override SQLCOM_CHECK, since it is an ALTER command */
  virtual enum_sql_command sql_command_code() const
  {
    return SQLCOM_ALTER_TABLE;
  }
};


/**
  Class that represents the ALTER TABLE t1 OPTIMIZE PARTITION p statement.
*/
class Sql_cmd_alter_table_optimize_partition : public Sql_cmd_optimize_table
{
public:
  /**
    Constructor, used to represent a ALTER TABLE OPTIMIZE PARTITION statement.
  */
  Sql_cmd_alter_table_optimize_partition()
    : Sql_cmd_optimize_table()
  {}

  ~Sql_cmd_alter_table_optimize_partition()
  {}

  bool execute(THD *thd);

  /* Override SQLCOM_OPTIMIZE, since it is an ALTER command */
  virtual enum_sql_command sql_command_code() const
  {
    return SQLCOM_ALTER_TABLE;
  }
};


/**
  Class that represents the ALTER TABLE t1 REPAIR PARTITION p statement.
*/
class Sql_cmd_alter_table_repair_partition : public Sql_cmd_repair_table
{
public:
  /**
    Constructor, used to represent a ALTER TABLE REPAIR PARTITION statement.
  */
  Sql_cmd_alter_table_repair_partition()
    : Sql_cmd_repair_table()
  {}

  ~Sql_cmd_alter_table_repair_partition()
  {}

  bool execute(THD *thd);

  /* Override SQLCOM_REPAIR, since it is an ALTER command */
  virtual enum_sql_command sql_command_code() const
  {
    return SQLCOM_ALTER_TABLE;
  }
};


/**
  Class that represents the ALTER TABLE t1 TRUNCATE PARTITION p statement.
*/
class Sql_cmd_alter_table_truncate_partition : public Sql_cmd_truncate_table
{
public:
  /**
    Constructor, used to represent a ALTER TABLE TRUNCATE PARTITION statement.
  */
  Sql_cmd_alter_table_truncate_partition()
  {}

  virtual ~Sql_cmd_alter_table_truncate_partition()
  {}

  bool execute(THD *thd);

  /* Override SQLCOM_TRUNCATE, since it is an ALTER command */
  virtual enum_sql_command sql_command_code() const
  {
    return SQLCOM_ALTER_TABLE;
  }
};

#endif /* SQL_PARTITION_ADMIN_H */
