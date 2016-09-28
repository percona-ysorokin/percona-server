/* Copyright (c) 2008, 2016, Oracle and/or its affiliates. All rights reserved.

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

#ifndef TABLE_FILE_SUMMARY_BY_INSTANCE_H
#define TABLE_FILE_SUMMARY_BY_INSTANCE_H

/**
  @file storage/perfschema/table_file_summary_by_instance.h
  Table FILE_SUMMARY_BY_INSTANCE (declarations).
*/

#include "pfs_column_types.h"
#include "pfs_engine_table.h"
#include "pfs_instr_class.h"
#include "pfs_instr.h"
#include "table_helper.h"

/**
  @addtogroup performance_schema_tables
  @{
*/

/** A row of PERFORMANCE_SCHEMA.FILE_SUMMARY_BY_INSTANCE. */
struct row_file_summary_by_instance
{
  /** Column FILE_NAME. */
  const char *m_filename;
  /** Length in bytes of @c m_filename. */
  uint m_filename_length;

  /** Column EVENT_NAME. */
  PFS_event_name_row m_event_name;

  /** Column OBJECT_INSTANCE_BEGIN */
  const void *m_identity;
  /**
    Columns COUNT_STAR, SUM/MIN/AVG/MAX TIMER and NUMBER_OF_BYTES for READ,
    WRITE and MISC operation types.
  */
  PFS_file_io_stat_row m_io_stat;
};

class PFS_index_file_summary_by_instance : public PFS_engine_index
{
public:
  PFS_index_file_summary_by_instance(PFS_engine_key *key_1)
    : PFS_engine_index(key_1)
  {}

  ~PFS_index_file_summary_by_instance()
  {}

  virtual bool match(const PFS_file *pfs) = 0;
};

class PFS_index_file_summary_by_instance_by_instance
  : public PFS_index_file_summary_by_instance
{
public:
  PFS_index_file_summary_by_instance_by_instance()
    : PFS_index_file_summary_by_instance(&m_key),
      m_key("OBJECT_INSTANCE_BEGIN")
  {}

  ~PFS_index_file_summary_by_instance_by_instance()
  {}

  bool match(const PFS_file *pfs);

private:
  PFS_key_object_instance m_key;
};

class PFS_index_file_summary_by_instance_by_file_name
  : public PFS_index_file_summary_by_instance
{
public:
  PFS_index_file_summary_by_instance_by_file_name()
    : PFS_index_file_summary_by_instance(&m_key),
      m_key("FILE_NAME")
  {}

  ~PFS_index_file_summary_by_instance_by_file_name()
  {}

  bool match(const PFS_file *pfs);

private:
  PFS_key_file_name m_key;
};

class PFS_index_file_summary_by_instance_by_event_name
  : public PFS_index_file_summary_by_instance
{
public:
  PFS_index_file_summary_by_instance_by_event_name()
    : PFS_index_file_summary_by_instance(&m_key),
      m_key("EVENT_NAME")
  {}

  ~PFS_index_file_summary_by_instance_by_event_name()
  {}

  bool match(const PFS_file *pfs);

private:
  PFS_key_event_name m_key;
};

/** Table PERFORMANCE_SCHEMA.FILE_SUMMARY_BY_INSTANCE. */
class table_file_summary_by_instance : public PFS_engine_table
{
public:
  /** Table share */
  static PFS_engine_table_share m_share;
  static PFS_engine_table* create();
  static int delete_all_rows();
  static ha_rows get_row_count();

  virtual void reset_position(void);

  virtual int rnd_next();
  virtual int rnd_pos(const void *pos);

  virtual int index_init(uint idx, bool sorted);
  virtual int index_next();

private:
  virtual int read_row_values(TABLE *table,
                              unsigned char *buf,
                              Field **fields,
                              bool read_all);

  table_file_summary_by_instance();

public:
  ~table_file_summary_by_instance()
  {}

private:
  void make_row(PFS_file *pfs);

  /** Table share lock. */
  static THR_LOCK m_table_lock;
  /** Fields definition. */
  static TABLE_FIELD_DEF m_field_def;

  /** Current row. */
  row_file_summary_by_instance m_row;
  /** True if the current row exists. */
  bool m_row_exists;
  /** Current position. */
  PFS_simple_index m_pos;
  /** Next position. */
  PFS_simple_index m_next_pos;

protected:
  PFS_index_file_summary_by_instance *m_opened_index;
};

/** @} */
#endif
