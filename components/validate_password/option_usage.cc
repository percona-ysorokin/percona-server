/*
  Copyright (c) 2024, Oracle and/or its affiliates.
*/

#include "option_usage.h"
#include <cassert>
#include <memory>
#include "mysql/components/component_implementation.h"
#include "mysql/components/library_mysys/option_usage_data.h"
#include "mysql/components/service.h"
#include "mysql/components/services/mysql_option_tracker.h"
#include "mysql/components/util/weak_service_reference.h"
#include "validate_password_imp.h"

const std::string c_name("component_validate_password"),
    opt_name("mysql_option_tracker_option"),
    c_option_name("Password validation component");

typedef weak_service_reference<SERVICE_TYPE(mysql_option_tracker_option),
                               c_name, opt_name>
    weak_option;
static Option_usage_data *option_usage{nullptr};

bool validate_password_component_option_usage_init() {
  assert(option_usage == nullptr);
  std::unique_ptr<Option_usage_data> ptr(new Option_usage_data(
      c_option_name.c_str(), SERVICE_PLACEHOLDER(registry)));

  bool ret = weak_option::init(
      SERVICE_PLACEHOLDER(registry), SERVICE_PLACEHOLDER(registry_registration),
      [&](SERVICE_TYPE(mysql_option_tracker_option) * opt) {
        return 0 != opt->define(c_option_name.c_str(), c_name.c_str(), 1);
      });
  if (!ret) option_usage = ptr.release();
  return ret;
}

bool validate_password_component_option_usage_deinit() {
  if (option_usage) {
    delete option_usage;
    option_usage = nullptr;
  }
  return weak_option::deinit(
      mysql_service_registry_no_lock, mysql_service_registration_no_lock,
      [&](SERVICE_TYPE(mysql_option_tracker_option) * opt) {
        return 0 != opt->undefine(c_option_name.c_str());
      });
}

bool validate_password_component_option_usage_set(unsigned long every_nth) {
  return option_usage->set_sampled(true, every_nth);
}