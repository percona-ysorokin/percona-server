/* Copyright (c) 2011, 2017, Oracle and/or its affiliates. All rights reserved.

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

/**
  @file storage/perfschema/table_hosts.cc
  TABLE HOSTS.
*/

#include "storage/perfschema/table_hosts.h"

#include <stddef.h>

#include "field.h"
#include "my_dbug.h"
#include "my_thread.h"
#include "pfs_account.h"
#include "pfs_host.h"
#include "pfs_instr.h"
#include "pfs_instr_class.h"
#include "pfs_memory.h"
#include "pfs_status.h"
#include "pfs_visitor.h"

THR_LOCK table_hosts::m_table_lock;

Plugin_table table_hosts::m_table_def(
  /* Schema name */
  "performance_schema",
  /* Name */
  "hosts",
  /* Definition */
  "  HOST CHAR(60) collate utf8_bin default null,\n"
  "  CURRENT_CONNECTIONS bigint not null,\n"
  "  TOTAL_CONNECTIONS bigint not null,\n"
  "  UNIQUE KEY (HOST) USING HASH\n",
  /* Options */
  " ENGINE=PERFORMANCE_SCHEMA",
  /* Tablespace */
  nullptr);

PFS_engine_table_share table_hosts::m_share = {
  &pfs_truncatable_acl,
  table_hosts::create,
  NULL, /* write_row */
  table_hosts::delete_all_rows,
  cursor_by_host::get_row_count,
  sizeof(PFS_simple_index), /* ref length */
  &m_table_lock,
  &m_table_def,
  false /* perpetual */
};

bool
PFS_index_hosts_by_host::match(PFS_host *pfs)
{
  if (m_fields >= 1)
  {
    if (!m_key.match(pfs))
    {
      return false;
    }
  }

  return true;
}

PFS_engine_table *
table_hosts::create(PFS_engine_table_share *)
{
  return new table_hosts();
}

int
table_hosts::delete_all_rows(void)
{
  reset_events_waits_by_thread();
  reset_events_waits_by_account();
  reset_events_waits_by_host();
  reset_events_stages_by_thread();
  reset_events_stages_by_account();
  reset_events_stages_by_host();
  reset_events_statements_by_thread();
  reset_events_statements_by_account();
  reset_events_statements_by_host();
  reset_events_transactions_by_thread();
  reset_events_transactions_by_account();
  reset_events_transactions_by_host();
  reset_memory_by_thread();
  reset_memory_by_account();
  reset_memory_by_host();
  reset_status_by_thread();
  reset_status_by_account();
  reset_status_by_host();
  purge_all_account();
  purge_all_host();
  return 0;
}

table_hosts::table_hosts() : cursor_by_host(&m_share)
{
}

int
table_hosts::index_init(uint, bool)
{
  PFS_index_hosts *result = NULL;
  result = PFS_NEW(PFS_index_hosts_by_host);
  m_opened_index = result;
  m_index = result;
  return 0;
}

int
table_hosts::make_row(PFS_host *pfs)
{
  pfs_optimistic_state lock;

  pfs->m_lock.begin_optimistic_lock(&lock);

  if (m_row.m_host.make_row(pfs))
  {
    return HA_ERR_RECORD_DELETED;
  }

  PFS_connection_stat_visitor visitor;
  PFS_connection_iterator::visit_host(pfs,
                                      true,  /* accounts */
                                      true,  /* threads */
                                      false, /* THDs */
                                      &visitor);

  if (!pfs->m_lock.end_optimistic_lock(&lock))
  {
    return HA_ERR_RECORD_DELETED;
  }

  m_row.m_connection_stat.set(&visitor.m_stat);
  return 0;
}

int
table_hosts::read_row_values(TABLE *table,
                             unsigned char *buf,
                             Field **fields,
                             bool read_all)
{
  Field *f;

  /* Set the null bits */
  DBUG_ASSERT(table->s->null_bytes == 1);
  buf[0] = 0;

  for (; (f = *fields); fields++)
  {
    if (read_all || bitmap_is_set(table->read_set, f->field_index))
    {
      switch (f->field_index)
      {
      case 0: /* HOST */
        m_row.m_host.set_field(f);
        break;
      case 1: /* CURRENT_CONNECTIONS */
      case 2: /* TOTAL_CONNECTIONS */
        m_row.m_connection_stat.set_field(f->field_index - 1, f);
        break;
      default:
        DBUG_ASSERT(false);
      }
    }
  }
  return 0;
}
