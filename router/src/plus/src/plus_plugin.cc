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

#include "plus_plugin.h"

#include <array>
#include <cinttypes>
#include <cstdint>
#include <string>
#include <string_view>

#include "plus_plugin_config.h"

#include "mysql/harness/loader.h"
#include "mysql/harness/logging/logging.h"
#include "mysql/harness/plugin.h"

#include "mysqlrouter/plus_component.h"
#include "mysqlrouter/plus_export.h"

IMPORT_LOG_FUNCTIONS()

static constexpr char kSectionNameRaw[]{"plus"};
static constexpr std::string_view kSectionName{kSectionNameRaw};

static void init(mysql_harness::PluginFuncEnv *env) {
  const mysql_harness::AppInfo *info = get_app_info(env);

  if (info == nullptr || nullptr == info->config) {
    return;
  }

  // assume there is only one section for us
  try {
    log_info("reading configuration section");

    bool section_found{false};

    std::uint16_t port{0};

    for (const mysql_harness::ConfigSection *section :
         info->config->sections()) {
      if (section->name != kSectionName) {
        continue;
      }

      if (section_found) {
        set_error(env, mysql_harness::kConfigInvalidArgument,
                  "[%s] found another config-section '%s', only one allowed",
                  kSectionNameRaw, section->key.c_str());
        return;
      }

      if (!section->key.empty()) {
        set_error(env, mysql_harness::kConfigInvalidArgument,
                  "[%s] section does not expect a key, found '%s'",
                  kSectionNameRaw, section->key.c_str());
        return;
      }

      PlusPluginConfig config{section};
      port = config.get_port();

      section_found = true;
    }

    log_info("extracted port %" PRIu16 " from the configuration", port);

    const auto init_res = PlusComponent::get_instance().init(port);
    if (!init_res) {
      const auto ec = init_res.error();

      if (ec == make_error_code(PlusComponentErrc::port_not_available)) {
        set_error(env, mysql_harness::kConfigInvalidArgument,
                  "port %" PRIu16 " is not available", port);
      } else if (ec == make_error_condition(
                           std::errc::resource_unavailable_try_again)) {
        // libc++ returns system:35
        // libstdc++ returns generic:35
        // aka, make_error_condition() needs to be used.
        set_error(env, mysql_harness::kConfigInvalidArgument,
                  "failed to spawn a thread");
      } else {
        set_error(env, mysql_harness::kConfigInvalidArgument, "%s",
                  ec.message().c_str());
      }
    }
  } catch (const std::invalid_argument &exc) {
    set_error(env, mysql_harness::kConfigInvalidArgument, "%s", exc.what());
  } catch (const std::exception &exc) {
    set_error(env, mysql_harness::kRuntimeError, "%s", exc.what());
  } catch (...) {
    set_error(env, mysql_harness::kUndefinedError, "Unexpected exception");
  }
}

static void start(mysql_harness::PluginFuncEnv * /* env */) {
  PlusComponent::get_instance().run();
}

static void stop(mysql_harness::PluginFuncEnv * /* env */) {
  PlusComponent::get_instance().stop();
}
static void deinit(mysql_harness::PluginFuncEnv * /* env */) {
  PlusComponent::get_instance().reset();
}

static std::array<const char *, 1> required = {{"logger"}};

extern "C" {
mysql_harness::Plugin PLUS_EXPORT harness_plugin_plus = {
    mysql_harness::PLUGIN_ABI_VERSION,       // abi-version
    mysql_harness::ARCHITECTURE_DESCRIPTOR,  // arch-descriptor
    "PLUS",
    VERSION_NUMBER(0, 0, 1),
    // requires
    required.size(),
    required.data(),
    // conflicts
    0,
    nullptr,
    init,    // init
    deinit,  // deinit
    start,   // start
    stop,    // stop
    false,   // signals ready
    supported_plus_plugin_options.size(),
    supported_plus_plugin_options.data(),
};
}
