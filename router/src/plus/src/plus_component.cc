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

#include "mysqlrouter/plus_component.h"

#include <chrono>
#include <cinttypes>
#include <stdexcept>
#include <string>
#include <thread>

#include "mysql/harness/logging/logging.h"

#include "mysql/harness/stdx/expected.h"

#include "plus_plugin_config.h"

IMPORT_LOG_FUNCTIONS()

const std::error_category &plus_component_category() noexcept {
  class category_impl : public std::error_category {
   public:
    const char *name() const noexcept override { return "plus_component"; }
    std::string message(int ev) const override {
      switch (static_cast<PlusComponentErrc>(ev)) {
        case PlusComponentErrc::already_initialized:
          return "already initialized";
        case PlusComponentErrc::port_not_available:
          return "port is not available";
      }
      return "unknown error";
    }
  };

  static category_impl instance;
  return instance;
}

std::error_code make_error_code(PlusComponentErrc e) {
  return {static_cast<int>(e), plus_component_category()};
}

stdx::expected<void, std::error_code> PlusComponent::init(
    std::uint16_t /*port*/) {
  if (running_)
    return stdx::make_unexpected(
        std::error_code(PlusComponentErrc::already_initialized));

  try {
    log_info("initialized");

    running_ = true;
    // if (<something_bad>) {
    //   throw std::system_error(open_res.error());
    // }
  } catch (const std::system_error &e) {
    // reset the component
    reset();
    return stdx::make_unexpected(e.code());
  }

  return {};
}

void PlusComponent::run() {
  std::uint32_t counter{0};
  while (running_) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    log_info("running iteration %" PRIu32, counter);
    ++counter;
  }
  log_info("stoped");
}

void PlusComponent::stop() {
  log_info("stop requested");
  running_ = false;
}

void PlusComponent::reset() {
  log_info("reset");
  running_ = false;
}

PlusComponent &PlusComponent::get_instance() {
  static PlusComponent instance;

  return instance;
}
