/* Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef DD_INFO_SCHEMA_TABLESPACE_STATS_INCLUDED
#define DD_INFO_SCHEMA_TABLESPACE_STATS_INCLUDED

#include <sys/types.h>
#include <string>

#include "my_inttypes.h"
#include "sql_string.h"                     // String
#include "sql/handler.h"                    // ha_tablespace_statistics

class THD;
struct TABLE_LIST;

namespace dd {
namespace info_schema {

// Tablespace statistics that are cached.
enum class enum_tablespace_stats_type
{
  TS_ID,
  TS_TYPE,
  TS_LOGFILE_GROUP_NAME,
  TS_LOGFILE_GROUP_NUMBER,
  TS_FREE_EXTENTS,
  TS_TOTAL_EXTENTS,
  TS_EXTENT_SIZE,
  TS_INITIAL_SIZE,
  TS_MAXIMUM_SIZE,
  TS_AUTOEXTEND_SIZE,
  TS_VERSION,
  TS_ROW_FORMAT,
  TS_DATA_FREE,
  TS_STATUS
};


/**
  The class hold dynamic table statistics for a table.
  This cache is used by internal UDF's defined for the purpose
  of INFORMATION_SCHEMA queries which retrieve dynamic table
  statistics. The class caches statistics for just one table.

  Overall aim of introducing this cache is to avoid making
  multiple calls to same SE API to retrieve the statistics.
*/

class Tablespace_statistics
{
public:
  Tablespace_statistics()
  {}

  /**
    Check if the stats are cached for given tablespace_name.

    @param tablespace_name       - Tablespace name.

    @return true if stats are cached, else false.
  */
  bool is_stat_cached(const String &tablespace_name)
  {
    return (m_key == form_key(tablespace_name));
  }


  /**
    Store the statistics form the given handler

    @param tablespace_name  - Tablespace name.
    @param stats            - ha_tablespace_statistics.

    @return void
  */
  void cache_stats(const String &tablespace_name,
                   ha_tablespace_statistics &stats)
  {
    m_stats= stats;
    m_error.clear();
    set_stat_cached(tablespace_name);
  }


  /**
    Read dynamic tablespace statistics from SE API OR by reading cached
    statistics from SELECT_LEX.

    @param thd                     - Current thread.
    @param tablespace_name_ptr     - Tablespace name of which we need stats.
    @param ts_se_private_data      - Tablespace se private data.

    @return true if statistics were not fetched from SE, otherwise false.
  */
  bool read_stat(THD *thd,
                 const String &tablespace_name_ptr,
                 const char* ts_se_private_data);

  bool read_stat_from_SE(THD *thd,
                         const String &tablespace_name_ptr,
                         const char* ts_se_private_data);

  // Invalidate the cache.
  void invalidate_cache(void)
  {
    m_key.clear();
    m_error.clear();
  }


  // Get error string. Its empty if a error is not reported.
  inline String_type error()
  { return m_error; }


  /**
    Return statistics of the a given type.

    @param      stype  Type of statistics requested.
    @param[out] result Value for stype.

    @returns void
  */
  void get_stat(enum_tablespace_stats_type stype,
                ulonglong *result);

  void get_stat(enum_tablespace_stats_type stype,
                dd::String_type *result);

private:

  /**
    Mark the cache as valid for a given table. This creates a key for the
    cache element. We store just a single table statistics in this cache.

    @param tablespace_name          - Tablespace name.

    @returns void.
  */
  void set_stat_cached(const String &tablespace_name)
  { m_key= form_key(tablespace_name); }


  /**
    Build a key representating the table for which stats are cached.

    @param tablespace_name          - Tablespace name.

    @returns String_type representing the key.
  */
  String_type form_key(const String &tablespace_name)
  {
    return String_type(tablespace_name.ptr());
  }

  /**
    Check if we have seen a error.

    @param tablespace_name  Tablespace name.

    @returns true if there is error reported.
             false if not.
  */
  inline bool check_error_for_key(const String &tablespace_name)
  {
    if (is_stat_cached(tablespace_name) && !m_error.empty())
      return true;

    return false;
  }

private:

  // The cache key
  String_type m_key; // Format '<tablespace_name>'

  // Error found when reading statistics.
  String_type m_error;

public:

  // Cached statistics.
  ha_tablespace_statistics m_stats;
};


} // namespace info_schema
} // namespace dd

#endif // DD_INFO_SCHEMA_TABLESPACE_STATS_INCLUDED
