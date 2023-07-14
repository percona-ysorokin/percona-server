/*
  Copyright (c) 2018, 2023, Oracle and/or its affiliates.

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

#include "router_component_metadata.h"

std::string RouterComponentMetadataTest::get_metadata_cache_section(
    ClusterType cluster_type, const std::string &ttl,
    const std::string &cluster_name, const std::string &ssl_mode) {
  const std::string cluster_type_str =
      (cluster_type == ClusterType::RS_V2) ? "rs" : "gr";

  std::map<std::string, std::string> options{
      {"cluster_type", cluster_type_str},
      {"router_id", "1"},
      {"user", router_metadata_username},
      {"connect_timeout", "1"},
      {"metadata_cluster", cluster_name}};

  if (!ttl.empty()) {
    options["ttl"] = ttl;
  }

  if (!ssl_mode.empty()) {
    options["ssl_mode"] = ssl_mode;
  }

  return mysql_harness::ConfigBuilder::build_section("metadata_cache:bootstrap",
                                                     options);
}

std::string RouterComponentMetadataTest::get_metadata_cache_routing_section(
    uint16_t router_port, const std::string &role, const std::string &strategy,
    const std::string &mode, const std::string &section_name,
    const std::string &protocol) {
  std::map<std::string, std::string> options{
      {"bind_port", std::to_string(router_port)},
      {"destinations", "metadata-cache://test/default?role=" + role},
      {"protocol", protocol}};

  if (!strategy.empty()) {
    options["routing_strategy"] = strategy;
  }

  if (!mode.empty()) {
    options["mode"] = mode;
  }

  return mysql_harness::ConfigBuilder::build_section("routing:" + section_name,
                                                     options);
}

std::vector<std::string> RouterComponentMetadataTest::get_array_field_value(
    const std::string &json_string, const std::string &field_name) {
  std::vector<std::string> result;

  rapidjson::Document json_doc;
  json_doc.Parse(json_string.c_str());
  EXPECT_TRUE(json_doc.HasMember(field_name.c_str())) << "json:" << json_string;
  EXPECT_TRUE(json_doc[field_name.c_str()].IsArray()) << json_string;

  auto arr = json_doc[field_name.c_str()].GetArray();
  for (size_t i = 0; i < arr.Size(); ++i) {
    result.push_back(arr[i].GetString());
  }

  return result;
}

bool RouterComponentMetadataTest::wait_metadata_read(
    const ProcessWrapper &router, const std::chrono::milliseconds timeout) {
  const std::string needle = "Potential changes detected in cluster";

  return wait_log_contains(router, needle, timeout);
}

ProcessWrapper &RouterComponentMetadataTest::launch_router(
    const std::string &metadata_cache_section,
    const std::string &routing_section,
    std::vector<uint16_t> metadata_server_ports, const int expected_exitcode,
    std::chrono::milliseconds wait_for_notify_ready) {
  auto default_section = get_DEFAULT_defaults();
  state_file_ = create_state_file(
      get_test_temp_dir_name(),
      create_state_file_content("uuid", "", metadata_server_ports, 0));
  init_keyring(default_section, get_test_temp_dir_name());
  default_section["dynamic_state"] = state_file_;

  // launch the router
  const std::string conf_file = create_config_file(
      get_test_temp_dir_name(), metadata_cache_section + routing_section,
      &default_section);
  auto &router = ProcessManager::launch_router(
      {"-c", conf_file}, expected_exitcode, true, false, wait_for_notify_ready);

  return router;
}

void RouterComponentMetadataTest::check_log_contains(
    ProcessWrapper &router, const std::string &expected_string,
    size_t expected_occurences) {
  const std::string log_content = router.get_logfile_content();
  EXPECT_EQ(expected_occurences,
            count_str_occurences(log_content, expected_string))
      << log_content;
}