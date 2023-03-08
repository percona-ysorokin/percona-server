/*****************************************************************************

Copyright (c) 2007, 2023, Oracle and/or its affiliates.

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

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA

*****************************************************************************/

/**************************************************//**
@file handler/i_s.h
InnoDB INFORMATION SCHEMA tables interface to MySQL.

Created July 18, 2007 Vasil Dimov
*******************************************************/

#ifndef i_s_h
#define i_s_h

const char plugin_author[] = "Oracle Corporation";

extern struct st_mysql_plugin	i_s_innodb_trx;
extern struct st_mysql_plugin	i_s_innodb_locks;
extern struct st_mysql_plugin	i_s_innodb_lock_waits;
extern struct st_mysql_plugin	i_s_innodb_cmp;
extern struct st_mysql_plugin	i_s_innodb_cmp_reset;
extern struct st_mysql_plugin	i_s_innodb_cmp_per_index;
extern struct st_mysql_plugin	i_s_innodb_cmp_per_index_reset;
extern struct st_mysql_plugin	i_s_innodb_cmpmem;
extern struct st_mysql_plugin	i_s_innodb_cmpmem_reset;
extern struct st_mysql_plugin   i_s_innodb_metrics;
extern struct st_mysql_plugin	i_s_innodb_ft_default_stopword;
extern struct st_mysql_plugin	i_s_innodb_ft_deleted;
extern struct st_mysql_plugin	i_s_innodb_ft_being_deleted;
extern struct st_mysql_plugin	i_s_innodb_ft_index_cache;
extern struct st_mysql_plugin	i_s_innodb_ft_index_table;
extern struct st_mysql_plugin	i_s_innodb_ft_config;
extern struct st_mysql_plugin	i_s_innodb_buffer_page;
extern struct st_mysql_plugin	i_s_innodb_buffer_page_lru;
extern struct st_mysql_plugin	i_s_innodb_buffer_stats;
extern struct st_mysql_plugin	i_s_innodb_temp_table_info;
extern struct st_mysql_plugin	i_s_innodb_sys_tables;
extern struct st_mysql_plugin	i_s_innodb_sys_tablestats;
extern struct st_mysql_plugin	i_s_innodb_sys_indexes;
extern struct st_mysql_plugin	i_s_innodb_sys_columns;
extern struct st_mysql_plugin	i_s_innodb_sys_fields;
extern struct st_mysql_plugin	i_s_innodb_sys_foreign;
extern struct st_mysql_plugin	i_s_innodb_sys_foreign_cols;
extern struct st_mysql_plugin	i_s_innodb_sys_tablespaces;
extern struct st_mysql_plugin	i_s_innodb_sys_datafiles;
extern struct st_mysql_plugin	i_s_innodb_sys_virtual;

/** Fill handlerton based INFORMATION_SCHEMA.FILES table.
@param[in,out]	thd	thread/connection descriptor
@param[in,out]	tables	information schema tables to fill
@retval 0 for success
@retval HA_ERR_OUT_OF_MEM when running out of memory
@return nonzero for failure */
int
i_s_files_table_fill(
	THD		*thd,
	TABLE_LIST	*tables);

#endif /* i_s_h */
