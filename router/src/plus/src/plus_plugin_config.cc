#include "plus_plugin_config.h"

#include <limits>

static constexpr char kPortOptionName[]{"port"};
static constexpr char kPortOptionDefaultValue[]{"4242"};

std::array<const char *, 1> supported_plus_plugin_options{kPortOptionName};

PlusPluginConfig::PlusPluginConfig(const mysql_harness::ConfigSection *section)
    : mysql_harness::BasePluginConfig(section),
      port_(get_option(section, kPortOptionName,
                       mysql_harness::IntOption<std::uint16_t>{
                           0, std::numeric_limits<std::uint16_t>::max()})) {}

std::string PlusPluginConfig::get_default(const std::string &option) const {
  const std::map<std::string_view, std::string_view> defaults{
      {kPortOptionName, kPortOptionDefaultValue}};

  auto it = defaults.find(option);
  if (it == defaults.end()) {
    return std::string{};
  }
  return std::string{it->second};
}

bool PlusPluginConfig::is_required(const std::string & /* option */) const {
  return false;
}
