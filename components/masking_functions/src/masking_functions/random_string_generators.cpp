/* Copyright (c) 2018, 2019 Francisco Miguel Biete Banon. All rights reserved.
   Copyright (c) 2023 Percona LLC and/or its affiliates. All rights reserved.

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

#include "masking_functions/random_string_generators.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <random>
#include <string>
#include <string_view>

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace {

std::default_random_engine &get_thread_local_prng() {
  static thread_local std::default_random_engine instance{
      std::random_device{}()};
  return instance;
}
}  // anonymous namespace

namespace masking_functions {

std::string random_character_class_string(character_class char_class,
                                          std::size_t length) {
  if (length == 0U) return {};

  const std::string_view charset_full{
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz"
      "0123456789"};

  std::string_view selected_charset;

  switch (char_class) {
    case character_class::lower_alpha:
      selected_charset = charset_full.substr(10U + 26U, 26U);
      break;
    case character_class::upper_alpha:
      selected_charset = charset_full.substr(10U, 26U);
      break;
    case character_class::numeric:
      selected_charset = charset_full.substr(0U, 10U);
      break;
    case character_class::alpha:
      selected_charset = charset_full.substr(10U, 26U + 26U);
      break;
    case character_class::lower_alpha_numeric:
      selected_charset = charset_full.substr(10U + 26U, 26U + 10U);
      break;
    case character_class::upper_alpha_numeric:
      selected_charset = charset_full.substr(0U, 10U + 26U);
      break;
    case character_class::alpha_numeric:
      selected_charset = charset_full.substr(0U, 10U + 26U + 26U);
      break;
    default:
      assert(false);
  }

  auto &prng = get_thread_local_prng();

  std::uniform_int_distribution<std::size_t> dist(
      0U, selected_charset.length() - 1U);

  auto random_char = [&]() noexcept { return selected_charset[dist(prng)]; };

  std::string str(length, '-');
  std::generate_n(str.data(), length, random_char);

  return str;
}

std::size_t random_number(std::size_t min, std::size_t max) {
  auto &prng = get_thread_local_prng();
  std::uniform_int_distribution<std::size_t> dist(min, max);

  return dist(prng);
}

std::string random_canada_sin() {
  std::string str;
  str.append(random_numeric_string(3));
  str.append(random_numeric_string(3));
  str.append(random_numeric_string(2));

  std::size_t check_sum = 0, n;
  std::size_t check_offset = (str.size() + 1) % 2;
  for (std::size_t i = 0; i < str.size(); i++) {
    n = str[i] - '0';  // We can convert to int substracting the ASCII for 0
    if ((i + check_offset) % 2 == 0) {
      n *= 2;
      check_sum += n > 9 ? (n - 9) : n;
    } else {
      check_sum += n;
    }
  }

  if (check_sum % 10 == 0) {
    str.append(std::to_string(0));
  } else {
    str.append(std::to_string(10 - (check_sum % 10)));
  }

  str.insert(6, "-");
  str.insert(3, "-");
  return str;
}

// Validate: https://stevemorse.org/ssn/cc.html
std::string random_credit_card() {
  std::string str;
  switch (random_number(3, 6)) {
    case 3:
      // American Express: 1st N 3, 2nd N [4,7], len 15
      str.assign("3")
          .append(random_number(0, 1) == 0 ? "4" : "7")
          .append(random_numeric_string(12));
      break;
    case 4:
      // Visa: 1st N 4, len 16
      str.assign("4").append(random_numeric_string(14));
      break;
    case 5:
      // Master Card: 1st N 5, 2nd N [1,5], len 16
      str.assign("5")
          .append(std::to_string(random_number(1, 5)))
          .append(random_numeric_string(13));
      break;
    case 6:
      // Discover Card: 1st N 6, 2nd N 0, 3rd N 1, 4th N 1, len 16
      str.assign("6011").append(random_numeric_string(11));
      break;
  }

  std::size_t check_sum = 0, n;
  std::size_t check_offset = (str.size() + 1) % 2;
  for (std::size_t i = 0; i < str.size(); i++) {
    n = str[i] - '0';  // We can convert to int substracting the ASCII for 0
    if ((i + check_offset) % 2 == 0) {
      n *= 2;
      check_sum += (n > 9 ? (n - 9) : n);
    } else {
      check_sum += n;
    }
  }

  if (check_sum % 10 == 0) {
    str.append(std::to_string(0));
  } else {
    str.append(std::to_string(10 - (check_sum % 10)));
  }

  assert(str.size() == 16 || str.size() == 15);
  return str;
}

std::string random_uuid() {
  static thread_local boost::uuids::random_generator gen;
  auto generated = gen();
  return boost::uuids::to_string(generated);
}

std::string random_ssn() {
  // AAA-GG-SSSS
  // Area, Group number, Serial number
  // Not valid number: Area: 000, 666, 900-999
  return std::to_string(random_number(900, 999))
      .append("-")
      .append(random_numeric_string(2))
      .append("-")
      .append(random_numeric_string(4));
}

std::string random_iban(std::string_view country, std::size_t length) {
  // TODO: consider adding IBAN checksum
  return std::string(country).append(random_numeric_string(length));
}

std::string random_uk_nin() {
  return std::string("AA").append(random_numeric_string(6)).append("C");
}

std::string random_us_phone() {
  // 1-555-AAA-BBBB

  return std::string("1")
      .append("-")
      .append("555")
      .append("-")
      .append(random_numeric_string(3))
      .append("-")
      .append(random_numeric_string(4));
}

}  // namespace masking_functions
