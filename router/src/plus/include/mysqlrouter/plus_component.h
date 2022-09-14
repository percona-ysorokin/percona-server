/*
  Copyright (c) 2020, 2021, Oracle and/or its affiliates.

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
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef ROUTER_PLUS_COMPONENT_INCLUDED
#define ROUTER_PLUS_COMPONENT_INCLUDED

#include <atomic>
#include <cstdint>
#include <system_error>
#include <type_traits>

#include "mysql/harness/stdx/expected.h"

#include "mysqlrouter/plus_component_export.h"

enum class PlusComponentErrc {
  already_initialized = 1,
  port_not_available = 2,
};

namespace std {
template <>
struct is_error_code_enum<PlusComponentErrc> : true_type {};
}  // namespace std

PLUS_COMPONENT_EXPORT std::error_code make_error_code(PlusComponentErrc ec);

class PLUS_COMPONENT_EXPORT PlusComponent {
 public:
  static PlusComponent &get_instance();

  stdx::expected<void, std::error_code> init(std::uint16_t port);

  void run();

  void stop();

  void reset();

 private:
  PlusComponent() = default;

  std::atomic_bool running_{false};
};

#endif
