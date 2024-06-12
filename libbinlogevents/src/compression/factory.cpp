/* Copyright (c) 2019, 2024, Oracle and/or its affiliates.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is designed to work with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have either included with
   the program or referenced in the documentation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include <compression/factory.h>
#include <compression/none_comp.h>
#include <compression/none_dec.h>
#include <compression/zstd_comp.h>
#include <compression/zstd_dec.h>
#include <my_byteorder.h>
#include <algorithm>

namespace binary_log::transaction::compression {

std::unique_ptr<Compressor> Factory::build_compressor(type t) {
  switch (t) {
    case ALGORITHM_ZSTD:
      return std::make_unique<Zstd_comp>();
    case NONE:
      return std::make_unique<None_comp>();
    default:
      break;
  }
  return std::unique_ptr<Compressor>();
}

std::unique_ptr<Decompressor> Factory::build_decompressor(type t) {
  switch (t) {
    case ALGORITHM_ZSTD:
      return std::make_unique<Zstd_dec>();
    case NONE:
      return std::make_unique<None_dec>();
    default:
      break;
  }
  return std::unique_ptr<Decompressor>();
}

}  // namespace binary_log::transaction::compression
