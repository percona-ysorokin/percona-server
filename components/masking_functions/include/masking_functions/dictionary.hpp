/* Copyright (c) 2024 Percona LLC and/or its affiliates. All rights reserved.

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

#ifndef MASKING_FUNCTIONS_DICTIONARY_HPP
#define MASKING_FUNCTIONS_DICTIONARY_HPP

#include "masking_functions/dictionary_fwd.hpp"  // IWYU pragma: export

#include <string>
#include <string_view>
#include <unordered_set>

namespace masking_functions {

// 'dictionary' is a collection of terms used as a basic in-memory data
// structure for caching dictionary terms within a single dictionary.
class dictionary {
 public:
  bool is_empty() const noexcept { return terms_.empty(); }

  bool contains(const std::string &term) const noexcept;
  // returns empty std::string_view if the dictionary is empty
  std::string_view get_random() const noexcept;
  // returns true if the term has been successfully inserted
  // returns false if the term is alreaddy in the dictionary
  bool insert(const std::string &term);
  // returns true if the term has been successfully removed
  // returns false if the term was not present in the dictionary
  bool remove(const std::string &term) noexcept;

 private:
  // TODO: in c++20 change to method signatures to accept std::string_view
  //       and container to std::unordered_set<std::string,
  //       transparent_string_like_hash, std::equal_to<>>.
  using term_container = std::unordered_set<std::string>;
  term_container terms_;
};

}  // namespace masking_functions

#endif  // MASKING_FUNCTIONS_DICTIONARY_HPP
