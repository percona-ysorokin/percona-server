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

#ifndef MASKING_FUNCTIONS_RANDOM_STRING_GENERATORS_HPP
#define MASKING_FUNCTIONS_RANDOM_STRING_GENERATORS_HPP

#include <cstddef>
#include <string>
#include <string_view>

namespace masking_functions {

enum class character_class {
  lower_alpha,
  upper_alpha,
  numeric,
  alpha,
  lower_alpha_numeric,
  upper_alpha_numeric,
  alpha_numeric
};

std::string random_character_class_string(character_class char_class,
                                          std::size_t length);

inline std::string random_lower_alpha_string(std::size_t length) {
  return random_character_class_string(character_class::lower_alpha, length);
}

inline std::string random_upper_alpha_string(std::size_t length) {
  return random_character_class_string(character_class::upper_alpha, length);
}

inline std::string random_numeric_string(std::size_t length) {
  return random_character_class_string(character_class::numeric, length);
}

std::size_t random_number(std::size_t min, std::size_t max);

std::string random_credit_card();

std::string random_canada_sin();

std::string random_iban(std::string_view country, std::size_t length);

std::string random_ssn();

std::string random_uuid();

std::string random_uk_nin();

std::string random_us_phone();

}  // namespace masking_functions

#endif  // MASKING_FUNCTIONS_RANDOM_STRING_GENERATORS_HPP
