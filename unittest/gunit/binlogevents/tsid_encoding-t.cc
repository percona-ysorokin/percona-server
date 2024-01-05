/* Copyright (c) 2021, 2024, Oracle and/or its affiliates.

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
#include <gtest/gtest.h>
#include <sstream>
#include <string>

#include "mysql/gtid/gtid.h"
#include "mysql/gtid/gtidset.h"
#include "mysql/gtid/tag.h"

namespace mysql::gtid::unittests {

const std::string uuid_1_str = "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa";

mysql::gtid::Uuid uuids[3];

void test_tsid_functions(const Tsid &arg1, const Tsid &arg2) {
  ASSERT_TRUE(arg1.get_uuid() == arg2.get_uuid());
  ASSERT_TRUE(arg1.get_tag() == arg2.get_tag());
  ASSERT_TRUE(arg1 == arg2);
  ASSERT_FALSE(arg1 != arg2);
  ASSERT_TRUE(arg1.to_string() == arg2.to_string());
}

TEST(TsidEncoding, Basic) {
  mysql::gtid::Uuid uuid_1;
  uuid_1.parse(uuid_1_str.c_str(), uuid_1_str.size());

  std::size_t buf_size = 64;
  std::vector<unsigned char> buf;
  buf.resize(buf_size);

  Tsid tsid_untagged(uuid_1, Tag());
  Tsid tsid_tagged(uuid_1, Tag("aloha"));

  Tsid tsid_untagged_read;
  Tsid tsid_tagged_read;

  tsid_untagged.encode_tsid(buf.data(), Gtid_format::untagged);
  auto tsid_bytes = tsid_untagged_read.decode_tsid(buf.data(), buf_size,
                                                   Gtid_format::untagged);
  ASSERT_GT(tsid_bytes, 0);

  test_tsid_functions(tsid_untagged, tsid_untagged_read);

  tsid_untagged.encode_tsid(buf.data(), Gtid_format::tagged);
  tsid_bytes =
      tsid_untagged_read.decode_tsid(buf.data(), buf_size, Gtid_format::tagged);
  ASSERT_GT(tsid_bytes, 0);

  test_tsid_functions(tsid_untagged, tsid_untagged_read);

  tsid_tagged.encode_tsid(buf.data(), Gtid_format::tagged);

  tsid_bytes =
      tsid_tagged_read.decode_tsid(buf.data(), 20, Gtid_format::tagged);
  ASSERT_EQ(tsid_bytes, 0);

  tsid_bytes =
      tsid_tagged_read.decode_tsid(buf.data(), buf_size, Gtid_format::tagged);
  ASSERT_GT(tsid_bytes, 0);

  test_tsid_functions(tsid_tagged, tsid_tagged_read);
}

}  // namespace mysql::gtid::unittests
