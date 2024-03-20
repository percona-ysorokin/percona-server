#ifndef PERCONA_TELEMETRY_COMPONENT_H
#define PERCONA_TELEMETRY_COMPONENT_H

#include <memory>

#include <mysql/components/component_implementation.h>
#include <mysql/components/services/component_sys_var_service.h>
#include <mysql/components/services/log_builtins.h>
#include <mysql/components/services/mysql_command_services.h>
#include <mysql/components/services/security_context.h>

#include "config.h"
#include "data_provider.h"
#include "logger.h"
#include "storage.h"
#include "worker.h"

class PerconaTelemetryComponent {
 public:
  struct Services {
    SERVICE_TYPE(mysql_thd_security_context) * thread_security_context_service;
    SERVICE_TYPE(mysql_command_thread) * command_thread_service;
    SERVICE_TYPE(mysql_command_factory) * command_factory_service;
    SERVICE_TYPE(mysql_command_options) * command_options_service;
    SERVICE_TYPE(mysql_command_query) * command_query_service;
    SERVICE_TYPE(mysql_command_query_result) * command_query_result_service;
    SERVICE_TYPE(mysql_command_field_info) * command_field_info_service;
    SERVICE_TYPE(mysql_command_error_info) * command_error_info_service;
    SERVICE_TYPE(log_builtins) * log_builtins_service;
    SERVICE_TYPE(log_builtins_string) * log_builtins_string;
    SERVICE_TYPE(component_sys_variable_register) * var_register_service;
    SERVICE_TYPE(component_sys_variable_unregister) * var_unregister_service;
  };

  PerconaTelemetryComponent(std::unique_ptr<Services> services);
  ~PerconaTelemetryComponent() = default;

  PerconaTelemetryComponent(const PerconaTelemetryComponent &rhs) = delete;
  PerconaTelemetryComponent(PerconaTelemetryComponent &&rhs) = delete;
  PerconaTelemetryComponent &operator=(const PerconaTelemetryComponent &rhs) =
      delete;
  PerconaTelemetryComponent &operator=(PerconaTelemetryComponent &&rhs) =
      delete;

  bool start();
  bool stop();

 private:
  std::unique_ptr<Services> services_;

  std::unique_ptr<Logger> logger_;
  std::unique_ptr<Config> config_;
  std::unique_ptr<Storage> storage_;
  std::unique_ptr<DataProvider> data_provider_;
  std::unique_ptr<Worker> worker_;
};

#endif /* PERCONA_TELEMETRY_COMPONENT_H */
