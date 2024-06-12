/*
 * Copyright (c) 2019, 2024, Oracle and/or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is designed to work with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms,
 * as designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have either included with
 * the program or referenced in the documentation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
 */

#ifndef PLUGIN_X_PROTOCOL_ENCODERS_ENCODING_PROTOBUF_H_
#define PLUGIN_X_PROTOCOL_ENCODERS_ENCODING_PROTOBUF_H_

#include <assert.h>
#include <google/protobuf/wire_format_lite.h>
#include <cassert>
#include <cstdint>
#include <string>

#include "plugin/x/protocol/encoders/encoding_primitives.h"

namespace protocol {

class Delayed_fixed_varuint32 {
 public:
  Delayed_fixed_varuint32() : m_out(nullptr) {}
  explicit Delayed_fixed_varuint32(uint8_t *&out) : m_out(out) { out += 5; }

  void encode(const uint32_t value) const {
    assert(m_out);
    uint8_t *out = m_out;
    primitives::base::Varint_length<5>::encode(out, value);
  }

 private:
  uint8_t *m_out;
};

/**
  Class responsible for protobuf message serialization

  The class is compatible with serializes supplied in libprotobuf and its
  kept simple to be possible to run ubench on it.
  The goal is to serialize the message payload into `Encoding_buffer class
  with the constrain that "encode_*" method doesn't check if buffer sufficient
  space inside it. Only following functions do that:

  * encode_field_delimited_raw
  * encode_field_string

  the reason is that, those function can serialize large amount of data, and
  other at most 20 bytes.

  The user of this class should group together encode calls and check the space
  before, for example:

  ```
     ensure_buffer_size(100);

     encode_field_bool<10>(true);
     encode_field_bool<11>(true);
     encode_field_var_sint32<12>(2238);
     ...
  ```

  User code should develop a method to check if the required size is sufficient
  to serialize subsequent encode calls.
*/
class Protobuf_encoder : public Primitives_encoder {
 private:
  using Helper = primitives::base::Helper;

 public:
  /**
      Check if output buffer has at last `size` bytes available.

      If the current buffer page has less data the needed,
      then next page should be acquired.

      @tparam size   required number of bytes
    */
  template <uint32_t size>
  void ensure_buffer_size() {
    // Static methods that check available size always must succeed
    m_buffer->ensure_buffer_size<size>();
    m_page = m_buffer->m_current;
  }

 public:
  explicit Protobuf_encoder(Encoding_buffer *buffer)
      : Primitives_encoder(buffer) {}

  /**
    Function serializes a bool field.

    User need to guarantee by calling `ensure_buffer_size`
    that the buffer has at least 11 bytes (when its not checking
    how long field entry is going to be generated by `field_id`).
  */
  template <uint32_t field_id>
  void encode_field_bool(const bool value) {
    encode_const_var_uint<Helper::encode_field_tag(
        field_id, Helper::WireType::WIRETYPE_VARINT)>();
    encode_fixedvar8_uint8(value ? 1 : 0);
  }

  /**
    Function serializes a varint32 field.

    User need to guarantee by calling `ensure_buffer_size`
    that the buffer has at least 15 bytes (when its not checking
    how long field entry is going to be generated by `field_id`).
  */
  template <uint32_t field_id>
  void encode_field_var_uint32(const uint32_t value) {
    encode_const_var_uint<Helper::encode_field_tag(
        field_id, Helper::WireType::WIRETYPE_VARINT)>();
    encode_var_uint32(value);
  }

  /**
    Function serializes a varint32 field in case when `value` is not null.

    User need to guarantee by calling `ensure_buffer_size`
    that the buffer has at least 15 bytes (when its not checking
    how long field entry is going to be generated by `field_id`).
  */
  template <uint32_t field_id>
  void encode_optional_field_var_uint32(const uint32_t *value) {
    if (value) encode_field_var_uint32<field_id>(*value);
  }

  /**
    Function serializes a varsint32 field.

    User need to guarantee by calling `ensure_buffer_size`
    that the buffer has at least 15 bytes (when its not checking
    how long field entry is going to be generated by `field_id`).
  */
  template <uint32_t field_id>
  void encode_field_var_sint32(const int32_t value) {
    encode_const_var_uint<Helper::encode_field_tag(
        field_id, Helper::WireType::WIRETYPE_VARINT)>();
    encode_var_sint32(value);
  }

  /**
    Function serializes a varsint32 field in case when `value` is not null.

    User need to guarantee by calling `ensure_buffer_size`
    that the buffer has at least 15 bytes (when its not checking
    how long field entry is going to be generated by `field_id`).
  */
  template <uint32_t field_id>
  void encode_optiona_field_var_sint32(const int32_t *value) {
    if (value) encode_field_var_sint32<field_id>(*value);
  }

  /**
    Function serializes a varuint64 field.

    User need to guarantee by calling `ensure_buffer_size`
    that the buffer has at least 20 bytes (when its not checking
    how long field entry is going to be generated by `field_id`).
  */
  template <uint32_t field_id>
  void encode_field_var_uint64(const uint64_t value) {
    encode_const_var_uint<Helper::encode_field_tag(
        field_id, Helper::WireType::WIRETYPE_VARINT)>();
    encode_var_uint64(value);
  }

  /**
    Function serializes a varuint64 field in case when `value` is not null.

    User need to guarantee by calling `ensure_buffer_size`
    that the buffer has at least 20 bytes (when its not checking
    how long field entry is going to be generated by `field_id`).
  */
  template <uint32_t field_id>
  void encode_optional_field_var_uint64(const uint64_t *value) {
    if (value) encode_field_var_uint64<field_id>(*value);
  }

  /**
    Function serializes a varsint64 field.

    User need to guarantee by calling `ensure_buffer_size`
    that the buffer has at least 20 bytes (when its not checking
    how long field entry is going to be generated by `field_id`).
  */
  template <uint32_t field_id>
  void encode_field_var_sint64(const int64_t value) {
    encode_const_var_uint<Helper::encode_field_tag(
        field_id, Helper::WireType::WIRETYPE_VARINT)>();
    encode_var_sint64(value);
  }

  /**
    Function serializes a varsint64 field in case when `value` is not null.

    User need to guarantee by calling `ensure_buffer_size`
    that the buffer has at least 20 bytes (when its not checking
    how long field entry is going to be generated by `field_id`).
  */
  template <uint32_t field_id>
  void encode_optional_field_var_sint64(const int64_t *value) {
    if (value) encode_field_var_sint64<field_id>(*value);
  }

  /**
    Function serializes a varuint64 field using compile time informations.

    User need to guarantee by calling `ensure_buffer_size`
    that the buffer has at least 20 bytes (when its not checking
    how long field entry is going to be generated by `field_id`).
  */
  template <uint32_t field_id, uint64_t value>
  void encode_field_const_var_uint() {
    encode_const_var_uint<Helper::encode_field_tag(
        field_id, Helper::WireType::WIRETYPE_VARINT)>();
    encode_const_var_uint<value>();
  }

  /**
    Function serializes a varsint64 field using compile time informations.

    User need to guarantee by calling `ensure_buffer_size`
    that the buffer has at least 20 bytes (when its not checking
    how long field entry is going to be generated by `field_id`).
  */
  template <uint32_t field_id, int64_t value>
  void encode_field_const_var_int() {
    encode_const_var_uint<Helper::encode_field_tag(
        field_id, Helper::WireType::WIRETYPE_VARINT)>();
    encode_const_var_sint<value>();
  }

  /**
    Function serializes a enum as varint32 field.

    User need to guarantee by calling `ensure_buffer_size`
    that the buffer has at least 15 bytes (when its not checking
    how long field entry is going to be generated by `field_id`).
  */
  template <uint32_t field_id>
  void encode_field_enum(const int32_t value) {
    assert(value >= 0);

    encode_const_var_uint<Helper::encode_field_tag(
        field_id, Helper::WireType::WIRETYPE_VARINT)>();
    encode_var_uint32(static_cast<uint32_t>(value));
  }

  /**
    Function serializes a enum as varint32 field in case when `value` is not
    null.

    User need to guarantee by calling `ensure_buffer_size`
    that the buffer has at least 15 bytes (when its not checking
    how long field entry is going to be generated by `field_id`).
  */
  template <uint32_t field_id>
  void encode_optional_field_enum(const int32_t *value) {
    if (value) encode_field_enum<field_id>(*value);
  }

  /**
    Function serializes a enum as varsint64 field using compile time
    informations.

    User need to guarantee by calling `ensure_buffer_size`
    that the buffer has at least 20 bytes (when its not checking
    how long field entry is going to be generated by `field_id`).
  */
  template <uint32_t field_id, int64_t value>
  void encode_field_const_enum() {
    static_assert(value >= 0,
                  "This encoder doesn't support enum with negative values.");
    encode_field_const_var_uint<field_id, value>();
  }

  /**
    Function serializes a field header using compile time informations.

    User need to guarantee by calling `ensure_buffer_size`
    that the buffer has at least 10 bytes (when its not checking
    how long field entry is going to be generated by `field_id`).

    This function requires that user will serialize manually the payload
    of the field in subsequent call (with additional buffer size check).
  */
  template <uint32_t field_id>
  void encode_field_delimited_header() {
    encode_const_var_uint<Helper::encode_field_tag(
        field_id, Helper::WireType::WIRETYPE_LENGTH_DELIMITED)>();
  }

  /**
    Function serializes a raw data field.

    Thus function is going to validated the buffer size on its own,
    user doesn't need to call `ensure_buffer_size`.
  */
  template <uint32_t field_id>
  void encode_field_delimited_raw(const uint8_t *source, uint32_t source_size) {
    ensure_buffer_size<10 + 5>();
    encode_field_delimited_header<field_id>();
    encode_var_uint32(static_cast<uint32_t>(source_size));

    encode_raw(source, source_size);
  }

  /**
    Function serializes a raw data field.

    Thus function is going to validated the buffer size on its own,
    user doesn't need to call `ensure_buffer_size`.
  */
  template <uint32_t field_id>
  void encode_field_string(const std::string &value) {
    encode_field_delimited_raw<field_id>(
        reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
  }

  /**
    Function serializes a raw data field.

    Thus function is going to validated the buffer size on its own,
    user doesn't need to call `ensure_buffer_size`.
  */
  template <uint32_t field_id>
  void encode_field_string(const char *value) {
    encode_field_delimited_raw<field_id>(
        reinterpret_cast<const uint8_t *>(value), strlen(value));
  }

  /**
    Function serializes a raw data field.

    Thus function is going to validated the buffer size on its own,
    user doesn't need to call `ensure_buffer_size`.
  */
  template <uint32_t field_id>
  void encode_field_string(const char *value, const uint32_t length) {
    encode_field_delimited_raw<field_id>(
        reinterpret_cast<const uint8_t *>(value), length);
  }

  /**
    Function serializes that reserves space for integer varint with fixed size

    Thus function is going to validated the buffer size on its own,
    user doesn't need to call `ensure_buffer_size`.
  */
  template <uint32_t field_id>
  Delayed_fixed_varuint32 encode_field_fixed_uint32() {
    encode_const_var_uint<Helper::encode_field_tag(
        field_id, Helper::WireType::WIRETYPE_VARINT)>();

    return Delayed_fixed_varuint32(m_page->m_current_data);
  }
};

}  // namespace protocol

#endif  // PLUGIN_X_PROTOCOL_ENCODERS_ENCODING_PROTOBUF_H_
