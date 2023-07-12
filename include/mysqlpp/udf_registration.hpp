/* Copyright (c) 2023 Percona LLC and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; version 2 of
   the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */

#ifndef MYSQLPP_UDF_REGISTRATION_HPP
#define MYSQLPP_UDF_REGISTRATION_HPP

#include <array>
#include <bitset>
#include <chrono>
#include <cstddef>
#include <thread>

#include <mysql/components/service.h>
#include <mysql/components/services/udf_registration.h>

namespace mysqlpp {

struct udf_info {
  const char *name;
  Item_result return_type;
  Udf_func_any func;
  Udf_func_init init_func;
  Udf_func_deinit deinit_func;
};

template <std::size_t N>
using udf_info_container = std::array<udf_info, N>;

template <std::size_t N>
using udf_bitset = std::bitset<N>;

template <std::size_t N>
void register_udfs(SERVICE_TYPE(udf_registration) * service,
                   udf_info_container<N> const &known_udfs,
                   udf_bitset<N> &registered_udfs) {
  std::size_t index = 0U;
  for (const auto &element : known_udfs) {
    if (!registered_udfs.test(index)) {
      if (service->udf_register(element.name, element.return_type, element.func,
                                element.init_func, element.deinit_func) == 0)
        registered_udfs.set(index);
    }
    ++index;
  }
}

static constexpr std::size_t max_unregister_attempts = 10;
static constexpr auto unregister_sleep_interval = std::chrono::seconds(1);

template <std::size_t N>
void unregister_udfs(SERVICE_TYPE(udf_registration) * service,
                     udf_info_container<N> const &known_udfs,
                     udf_bitset<N> &registered_udfs) {
  int was_present = 0;

  std::size_t index = 0U;

  for (const auto &element : known_udfs) {
    if (registered_udfs.test(index)) {
      std::size_t attempt = 0;
      mysql_service_status_t status = 0;
      while (attempt < max_unregister_attempts &&
             (status = service->udf_unregister(element.name, &was_present)) !=
                 0 &&
             was_present != 0) {
        std::this_thread::sleep_for(unregister_sleep_interval);
        ++attempt;
      }
      if (status == 0) registered_udfs.reset(index);
    }
    ++index;
  }
}

}  // namespace mysqlpp

#define DECLARE_UDF_INFO(NAME, TYPE)                               \
  mysqlpp::udf_info {                                              \
    #NAME, TYPE, (Udf_func_any)&NAME, &NAME##_init, &NAME##_deinit \
  }

#endif
