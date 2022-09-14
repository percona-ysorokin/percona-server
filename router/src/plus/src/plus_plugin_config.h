#ifndef PLUS_PLUGIN_CONFIG_INCLUDED
#define PLUS_PLUGIN_CONFIG_INCLUDED

#include "mysqlrouter/plus_export.h"

#include <array>
#include <cstdint>
#include <string>

#include "mysql/harness/plugin_config.h"

extern std::array<const char *, 1> supported_plus_plugin_options;

class PLUS_EXPORT PlusPluginConfig : public mysql_harness::BasePluginConfig {
 public:
  PlusPluginConfig(const mysql_harness::ConfigSection *section);

  std::string get_default(const std::string &option) const override;

  bool is_required(const std::string &option) const override;

  std::uint16_t get_port() const noexcept { return port_; }

 private:
  std::uint16_t port_;
};

#endif
