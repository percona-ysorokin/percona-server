/* Copyright (c) 2017, 2023, Oracle and/or its affiliates.

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

#include "sql-common/json_diff.h"

#include <sys/types.h>
#include <cassert>
#include <utility>

#include "my_alloc.h"
#include "my_byteorder.h"
#include "my_dbug.h"  // assert
#include "my_inttypes.h"
#include "my_sys.h"
#include "mysql/components/services/bits/psi_bits.h"
#include "mysql_com.h"
#include "mysqld_error.h"
#include "sql-common/json_binary.h"
#include "sql-common/json_dom.h"  // Json_dom, Json_wrapper
#include "sql-common/json_error_handler.h"
#include "sql-common/json_path.h"  // Json_path
#include "sql/current_thd.h"       // current_thd
#include "sql/log_event.h"         // net_field_length_checked
#include "sql/psi_memory_key.h"
#include "sql/sql_class.h"
#include "sql/sql_const.h"
#include "sql/system_variables.h"
#include "sql/table.h"
#include "sql_string.h"  // StringBuffer
#include "template_utils.h"

// Define constructor and destructor here instead of in the header file, to
// avoid dependencies on psi_memory_key.h and json_dom.h from the header file.

Json_diff::Json_diff(const Json_seekable_path &path,
                     enum_json_diff_operation operation,
                     std::unique_ptr<Json_dom> value)
    : m_path(key_memory_JSON),
      m_operation(operation),
      m_value(std::move(value)) {
  for (const Json_path_leg *leg : path) m_path.append(*leg);
}

Json_diff::~Json_diff() = default;

Json_wrapper Json_diff::value() const {
  Json_wrapper result(m_value.get());
  result.set_alias();
  return result;
}

/**
  Return the total size of a data field, plus the size of the
  preceding integer that describes the length, when the integer is
  stored in net_field_length() format

  @param length The length of the data
  @return The length of the data plus the length of the length field.
*/
static size_t length_of_length_and_string(size_t length) {
  return length + net_length_size(length);
}

/**
  Encode a String as (length, data) pair, with length being stored in
  net_field_length() format.

  @param to Buffer where length and data will be stored.
  @param from Source string containing the data.
  @return true on out of memory, false on success.
*/
static bool write_length_and_string(String *to, const String &from) {
  // Serialize length.
  size_t length = from.length();
  DBUG_EXECUTE_IF("binlog_corrupt_write_length_and_string_bad_length", {
    DBUG_SET("-d,binlog_corrupt_write_length_and_string_bad_length");
    length = 1 << 30;
  });
  char length_buf[9];
  const size_t length_length =
      net_store_length((uchar *)length_buf, length) - (uchar *)length_buf;
  DBUG_PRINT("info", ("write_length_and_string: length=%lu length_length=%lu",
                      (unsigned long)length, (unsigned long)length_length));
  DBUG_EXECUTE_IF(
      "binlog_corrupt_write_length_and_string_truncate_before_string", {
        DBUG_SET(
            "-d,binlog_corrupt_write_length_and_string_truncate_before_string");
        return false;
      });
  DBUG_EXECUTE_IF("binlog_corrupt_write_length_and_string_bad_char", {
    DBUG_SET("-d,binlog_corrupt_write_length_and_string_bad_char");
    // Instead of "some text", write "\xffsome tex"
    // This is sure to corrupt both JSON paths and
    // binary JSON.
    return to->append(length_buf, length_length) ||
           to->append(static_cast<char>(0xff)) ||
           to->append(from.ptr(), from.length() - 1);
  });
  // Allocate memory and append
  return to->append(length_buf, length_length) || to->append(from);
}

size_t Json_diff::binary_length() const {
  DBUG_TRACE;

  // operation
  size_t ret = ENCODED_OPERATION_BYTES;

  /*
    It would be better to compute length without serializing the path
    and json.  And given that we serialize the path and json, it would
    be better if we dealt with out-of-memory errors in a better way.

    In the future, we should remove the need to pre-compute the size.
    Currently this is only needed by the binlog writer.  And it would
    be better to rewrite the binlog writer so that it streams rows
    directly to the thread caches instead of storing them in memory.

    And currently, this function is used from
    Row_data_memory::max_row_length, and the return value of that
    function is used in Row_data_memory::allocate_memory, which
    doesn't check out-of-memory conditions at all and might just
    dereference nullptr in case of out of memory.  So these asserts do
    not make the situation worse.
  */
  StringBuffer<STRING_BUFFER_USUAL_SIZE> buf;

  // path
  if (m_path.to_string(&buf)) assert(0); /* purecov: deadcode */
  ret += length_of_length_and_string(buf.length());

  if (operation() != enum_json_diff_operation::REMOVE) {
    // value
    buf.length(0);
    const THD *const thd = current_thd;
    if (value().to_binary(JsonSerializationDefaultErrorHandler(thd), &buf))
      assert(0); /* purecov: deadcode */
    if (buf.length() > thd->variables.max_allowed_packet) {
      my_error(ER_WARN_ALLOWED_PACKET_OVERFLOWED, MYF(0),
               "json_binary::serialize", thd->variables.max_allowed_packet);
      assert(0);
    }
    ret += length_of_length_and_string(buf.length());
  }

  return ret;
}

bool Json_diff::write_binary(String *to) const {
  DBUG_TRACE;

  // Serialize operation
  char operation = (char)m_operation;
  DBUG_EXECUTE_IF("binlog_corrupt_json_diff_bad_op", {
    DBUG_SET("-d,binlog_corrupt_json_diff_bad_op");
    operation = 127;
  });
  if (to->append(&operation, ENCODED_OPERATION_BYTES))
    return true; /* purecov: inspected */  // OOM, error is reported
  DBUG_PRINT("info", ("wrote JSON operation=%d", (int)operation));

  /**
    @todo This first serializes in one buffer and then copies to
    another buffer.  It would be better if we could write directly to
    the output and save a round of memory allocation + copy. /Sven
  */

  // Serialize JSON path
  StringBuffer<STRING_BUFFER_USUAL_SIZE> buf;
#ifndef NDEBUG
  bool return_early = false;
  DBUG_EXECUTE_IF("binlog_corrupt_json_diff_truncate_before_path_length", {
    DBUG_SET("-d,binlog_corrupt_json_diff_truncate_before_path_length");
    return false;
  });
  DBUG_EXECUTE_IF("binlog_corrupt_json_diff_bad_path_length", {
    DBUG_SET("-d,binlog_corrupt_json_diff_bad_path_length");
    DBUG_SET("+d,binlog_corrupt_write_length_and_string_bad_length");
  });
  DBUG_EXECUTE_IF("binlog_corrupt_json_diff_truncate_before_path", {
    DBUG_SET("-d,binlog_corrupt_json_diff_truncate_before_path");
    DBUG_SET(
        "+d,binlog_corrupt_write_length_and_string_truncate_before_string");
    return_early = true;
  });
  DBUG_EXECUTE_IF("binlog_corrupt_json_diff_bad_path_char", {
    DBUG_SET("-d,binlog_corrupt_json_diff_bad_path_char");
    DBUG_SET("+d,binlog_corrupt_write_length_and_string_bad_char");
  });
#endif  // ifndef NDEBUG
  if (m_path.to_string(&buf) || write_length_and_string(to, buf))
    return true; /* purecov: inspected */  // OOM, error is reported
#ifndef NDEBUG
  if (return_early) return false;
#endif
  DBUG_PRINT("info", ("wrote JSON path '%s' of length %lu", buf.ptr(),
                      (unsigned long)buf.length()));

  if (m_operation != enum_json_diff_operation::REMOVE) {
    // Serialize JSON value
    buf.length(0);
#ifndef NDEBUG
    DBUG_EXECUTE_IF("binlog_corrupt_json_diff_truncate_before_doc_length", {
      DBUG_SET("-d,binlog_corrupt_json_diff_truncate_before_doc_length");
      return false;
    });
    DBUG_EXECUTE_IF("binlog_corrupt_json_diff_bad_doc_length", {
      DBUG_SET("-d,binlog_corrupt_json_diff_bad_doc_length");
      DBUG_SET("+d,binlog_corrupt_write_length_and_string_bad_length");
    });
    DBUG_EXECUTE_IF("binlog_corrupt_json_diff_truncate_before_doc", {
      DBUG_SET("-d,binlog_corrupt_json_diff_truncate_before_doc");
      DBUG_SET(
          "+d,binlog_corrupt_write_length_and_string_truncate_before_string");
    });
    DBUG_EXECUTE_IF("binlog_corrupt_json_diff_bad_doc_char", {
      DBUG_SET("-d,binlog_corrupt_json_diff_bad_doc_char");
      DBUG_SET("+d,binlog_corrupt_write_length_and_string_bad_char");
    });
#endif  // ifndef NDEBUG
    const THD *const thd = current_thd;
    if (value().to_binary(JsonSerializationDefaultErrorHandler(thd), &buf) ||
        write_length_and_string(to, buf)) {
      return true; /* purecov: inspected */  // OOM, error is reported
    }
    if (buf.length() > thd->variables.max_allowed_packet) {
      my_error(ER_WARN_ALLOWED_PACKET_OVERFLOWED, MYF(0),
               "json_binary::serialize", thd->variables.max_allowed_packet);
      return true;
    }
    DBUG_PRINT("info",
               ("wrote JSON value of length %lu", (unsigned long)buf.length()));
  }

  return false;
}

Json_diff_vector::Json_diff_vector(allocator_type arg)
    : m_vector(std::vector<Json_diff, allocator_type>(arg)),
      m_binary_length(0) {}

static MEM_ROOT empty_json_diff_vector_mem_root(PSI_NOT_INSTRUMENTED, 256);
const Json_diff_vector Json_diff_vector::EMPTY_JSON_DIFF_VECTOR{
    Json_diff_vector::allocator_type{&empty_json_diff_vector_mem_root}};

void Json_diff_vector::add_diff(const Json_seekable_path &path,
                                enum_json_diff_operation operation,
                                std::unique_ptr<Json_dom> dom) {
  m_vector.emplace_back(path, operation, std::move(dom));
  m_binary_length += at(size() - 1).binary_length();
}

void Json_diff_vector::add_diff(const Json_seekable_path &path,
                                enum_json_diff_operation operation) {
  m_vector.emplace_back(path, operation, nullptr);
  m_binary_length += at(size() - 1).binary_length();
}

void Json_diff_vector::clear() {
  m_vector.clear();
  m_binary_length = 0;
}

size_t Json_diff_vector::binary_length(bool include_metadata) const {
  return m_binary_length + (include_metadata ? ENCODED_LENGTH_BYTES : 0);
}

bool Json_diff_vector::write_binary(String *to) const {
  // Insert placeholder where we will store the length, once that is known.
  char length_buf[ENCODED_LENGTH_BYTES] = {0, 0, 0, 0};
  if (to->append(length_buf, ENCODED_LENGTH_BYTES))
    return true; /* purecov: inspected */  // OOM, error is reported

  // Store all the diffs.
  for (const Json_diff &diff : *this)
    if (diff.write_binary(to))
      return true; /* purecov: inspected */  // OOM, error is reported

  // Store the length.
  const size_t length = to->length() - ENCODED_LENGTH_BYTES;
  int4store(to->ptr(), (uint32)length);

  DBUG_PRINT("info", ("Wrote JSON diff vector length %lu=%02x %02x %02x %02x",
                      (unsigned long)length, length_buf[0], length_buf[1],
                      length_buf[2], length_buf[3]));

  return false;
}

bool Json_diff_vector::read_binary(const char **from, const TABLE *table,
                                   const char *field_name) {
  DBUG_TRACE;
  const uchar *p = pointer_cast<const uchar *>(*from);

  // Caller should have validated that the buffer is least 4 + length
  // bytes long.
  size_t length = uint4korr(p);
  p += 4;

  DBUG_PRINT("info", ("length=%d p=%p", (int)length, p));

  while (length) {
    DBUG_PRINT("info",
               ("length=%u bytes remaining to decode into Json_diff_vector",
                (uint)length));
    // Read operation
    if (length < 1) goto corrupted;
    const int operation_number = *p;
    DBUG_PRINT("info", ("operation_number=%d", operation_number));
    if (operation_number >= JSON_DIFF_OPERATION_COUNT) goto corrupted;
    const enum_json_diff_operation operation =
        static_cast<enum_json_diff_operation>(operation_number);
    length--;
    p++;

    // Read path length
    size_t path_length;
    if (net_field_length_checked<size_t>(&p, &length, &path_length))
      goto corrupted;
    if (length < path_length) goto corrupted;

    // Read path
    Json_path path(key_memory_JSON);
    size_t bad_index;
    DBUG_PRINT("info", ("path='%.*s'", (int)path_length, p));
    if (parse_path(path_length, pointer_cast<const char *>(p), &path,
                   &bad_index))
      goto corrupted;
    p += path_length;
    length -= path_length;

    if (operation != enum_json_diff_operation::REMOVE) {
      // Read value length
      size_t value_length;
      if (net_field_length_checked<size_t>(&p, &length, &value_length))
        goto corrupted;

      if (length < value_length) goto corrupted;

      // Read value
      json_binary::Value value = json_binary::parse_binary(
          pointer_cast<const char *>(p), value_length);
      if (value.type() == json_binary::Value::ERROR) goto corrupted;
      const Json_wrapper wrapper(value);
      std::unique_ptr<Json_dom> dom = wrapper.clone_dom();
      if (dom == nullptr)
        return true; /* purecov: inspected */  // OOM, error is reported
      wrapper.dbug_print("", JsonDepthErrorHandler);

      // Store diff
      add_diff(path, operation, std::move(dom));

      p += value_length;
      length -= value_length;
    } else {
      // Store diff
      add_diff(path, operation);
    }
  }

  *from = pointer_cast<const char *>(p);
  return false;

corrupted:
  my_error(ER_CORRUPTED_JSON_DIFF, MYF(0), (int)table->s->table_name.length,
           table->s->table_name.str, field_name);
  return true;
}
