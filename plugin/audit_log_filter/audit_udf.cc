/* Copyright (c) 2022 Percona LLC and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */

#include "plugin/audit_log_filter/audit_udf.h"
#include "plugin/audit_log_filter/audit_error_log.h"
#include "plugin/audit_log_filter/audit_log_filter.h"
#include "plugin/audit_log_filter/audit_log_reader.h"
#include "plugin/audit_log_filter/audit_psi_info.h"
#include "plugin/audit_log_filter/audit_table/audit_log_filter.h"
#include "plugin/audit_log_filter/audit_table/audit_log_user.h"
#include "plugin/audit_log_filter/log_record_formatter/base.h"
#include "plugin/audit_log_filter/sys_vars.h"

#include "mysql/plugin.h"

#include "rapidjson/document.h"

#include <mysql/components/services/mysql_current_thread_reader.h>
#include <mysql/components/services/udf_metadata.h>
#include <mysql/components/services/udf_registration.h>

#include <cstring>
#include <regex>
#include <unordered_set>

namespace audit_log_filter {
namespace {

const std::unordered_set<std::string> log_read_udf_allowed_args{
    "start", "timestamp", "id", "max_array_length"};

struct UserNameInfo {
  char username[audit_table::kAuditFieldLengthUsername + 1];
  char userhost[audit_table::kAuditFieldLengthUserhost + 1];
};

std::unique_ptr<UserNameInfo> check_parse_user_name_host(
    const std::string &user_name_host, char *message) {
  // user_name format is "user_name@host_name" or "%"
  auto max_username_len = audit_table::kAuditFieldLengthUsername +
                          audit_table::kAuditFieldLengthUserhost + 1;

  if (user_name_host.length() > max_username_len) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument: user_name is too long, max length is %ld",
                  max_username_len);
    return nullptr;
  }

  const std::regex user_name_all_regex("^%$");
  const std::regex user_name_regex("(.*)@(.*)");

  auto user_info_data = std::make_unique<UserNameInfo>();

  if (std::regex_match(user_name_host, user_name_all_regex)) {
    strncpy(user_info_data->username, user_name_host.c_str(),
            user_name_host.length() + 1);
    strncpy(user_info_data->userhost, user_name_host.c_str(),
            user_name_host.length() + 1);
  } else {
    std::smatch pieces_match;

    if (std::regex_match(user_name_host, pieces_match, user_name_regex)) {
      std::ssub_match user_name_match = pieces_match[1];
      std::ssub_match user_host_match = pieces_match[2];

      if (user_name_match.str().length() >
          audit_table::kAuditFieldLengthUsername) {
        std::snprintf(message, MYSQL_ERRMSG_SIZE,
                      "Wrong argument: user name part of user_name "
                      "is too long, max length is %ld",
                      audit_table::kAuditFieldLengthUsername);
        return nullptr;
      }

      if (user_host_match.str().length() >
          audit_table::kAuditFieldLengthUserhost) {
        std::snprintf(message, MYSQL_ERRMSG_SIZE,
                      "Wrong argument: user host part of user_name "
                      "is too long, max length is %ld",
                      audit_table::kAuditFieldLengthUserhost);
        return nullptr;
      }

      strncpy(user_info_data->username, user_name_match.str().c_str(),
              user_name_match.str().length() + 1);
      strncpy(user_info_data->userhost, user_host_match.str().c_str(),
              user_host_match.str().length() + 1);
    } else {
      std::snprintf(message, MYSQL_ERRMSG_SIZE,
                    "Wrong argument: wrong user_name format, it should be in "
                    "user_name@host_name format, or '%%' to represent the "
                    "default account");
      return nullptr;
    }
  }

  return user_info_data;
}

}  // namespace

AuditUdf::AuditUdf(comp_registry_srv_t *comp_registry_srv)
    : m_comp_registry_srv{comp_registry_srv} {}

AuditUdf::~AuditUdf() {
  int was_present = 0;
  my_service<SERVICE_TYPE(udf_registration)> udf_registration_srv(
      "udf_registration", m_comp_registry_srv);

  for (const auto &name : m_active_udf_names) {
    udf_registration_srv->udf_unregister(name.c_str(), &was_present);
  }

  m_active_udf_names.clear();
}

bool AuditUdf::init(UdfFuncInfo *begin, UdfFuncInfo *end) {
  my_service<SERVICE_TYPE(udf_registration)> udf_registration_srv(
      "udf_registration", m_comp_registry_srv);

  for (UdfFuncInfo *it = begin; it != end; ++it) {
    if (udf_registration_srv->udf_register(it->udf_name, STRING_RESULT,
                                           (Udf_func_any)it->udf_func,
                                           it->init_func, it->deinit_func)) {
      LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                      "Failed to register %s UDF", it->udf_name);
      return false;
    }

    m_active_udf_names.emplace_back(it->udf_name);
  }

  return true;
}

bool AuditUdf::audit_log_filter_set_filter_udf_init(AuditUdf *udf,
                                                    UDF_INIT *initid,
                                                    UDF_ARGS *udf_args,
                                                    char *message) noexcept {
  if (udf_args->arg_count != 2) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument list: "
                  "audit_log_filter_set_filter(filter_name, definition)");
    return true;
  }

  if (udf_args->arg_type[0] != STRING_RESULT ||
      udf_args->arg_type[1] != STRING_RESULT) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument type: "
                  "audit_log_filter_set_filter(string, string)");
    return true;
  }

  if (udf_args->lengths[0] == 0) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument: empty filter name");
    return true;
  }

  if (udf_args->lengths[1] == 0) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument: empty filter definition");
    return true;
  }

  if (udf_args->lengths[0] > audit_table::kAuditFieldLengthFiltername) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument: filter_name is too long, max length is %ld",
                  audit_table::kAuditFieldLengthFiltername);
    return true;
  }

  if (udf_args->lengths[1] > audit_table::kAuditFieldLengthFilter) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument: definition is too long, max length is %ld",
                  audit_table::kAuditFieldLengthFilter);
    return true;
  }

  if (!udf->set_return_value_charset(initid) ||
      !udf->set_args_charset(udf_args)) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Unable to set character set service for "
                  "audit_log_filter_set_filter UDF");
    return true;
  }

  initid->maybe_null = false;
  initid->const_item = false;
  return false;
}

char *AuditUdf::audit_log_filter_set_filter_udf(
    AuditUdf *udf, UDF_INIT *initid [[maybe_unused]], UDF_ARGS *udf_args,
    char *result, unsigned long *length, unsigned char *is_null,
    unsigned char *error) noexcept {
  *is_null = 0;
  *error = 0;
  AuditRule rule{udf_args->args[0], udf_args->args[1]};

  if (!rule.check_valid() || !rule.check_parse_state()) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "Wrong argument: incorrect rule definition '%s'",
                    udf_args->args[1]);
    std::snprintf(result, MYSQL_ERRMSG_SIZE,
                  "ERROR: Incorrect rule definition");
    *length = std::strlen(result);
    return result;
  }

  audit_table::AuditLogFilter audit_log_filter{udf->get_comp_registry_srv()};

  auto check_result = audit_log_filter.check_name_exists(udf_args->args[0]);

  if (check_result == audit_table::TableResult::Fail) {
    LogPluginErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                 "Failed to check filtering rule name existence");
    std::snprintf(result, MYSQL_ERRMSG_SIZE,
                  "ERROR: Failed to check filtering rule name existence");
    *length = std::strlen(result);
    return result;
  }

  if (check_result == audit_table::TableResult::Found) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "Filtering rule with the name '%s' already exists",
                    udf_args->args[0]);
    std::snprintf(result, MYSQL_ERRMSG_SIZE,
                  "ERROR: Rule with this name already exists");
    *length = std::strlen(result);
    return result;
  }

  auto insert_result =
      audit_log_filter.insert_filter(udf_args->args[0], udf_args->args[1]);

  if (insert_result != audit_table::TableResult::Ok) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "Failed to insert filtering rule '%s', '%s'",
                    udf_args->args[0], udf_args->args[1]);
    std::snprintf(result, MYSQL_ERRMSG_SIZE, "ERROR: Failed to insert rule");
    *length = std::strlen(result);
    return result;
  }

  std::snprintf(result, MYSQL_ERRMSG_SIZE, "OK");
  *length = std::strlen(result);

  return result;
}

void AuditUdf::audit_log_filter_set_filter_udf_deinit(UDF_INIT *) {}

// audit_log_filter_remove_filter(filter_name)
bool AuditUdf::audit_log_filter_remove_filter_udf_init(AuditUdf *udf,
                                                       UDF_INIT *initid,
                                                       UDF_ARGS *udf_args,
                                                       char *message) noexcept {
  if (udf_args->arg_count != 1) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument list: "
                  "audit_log_filter_remove_filter(filter_name)");
    return true;
  }

  if (udf_args->arg_type[0] != STRING_RESULT) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument type: "
                  "audit_log_filter_remove_filter(string)");
    return true;
  }

  if (udf_args->lengths[0] == 0) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument: empty filter name");
    return true;
  }

  if (udf_args->lengths[0] > audit_table::kAuditFieldLengthFiltername) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument: filter_name is too long, max length is %ld",
                  audit_table::kAuditFieldLengthFiltername);
    return true;
  }

  if (!udf->set_return_value_charset(initid) ||
      !udf->set_args_charset(udf_args)) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Unable to set character set service for "
                  "audit_log_filter_remove_filter UDF");
    return true;
  }

  initid->maybe_null = false;
  initid->const_item = false;
  return false;
}

char *AuditUdf::audit_log_filter_remove_filter_udf(
    AuditUdf *udf, UDF_INIT *initid [[maybe_unused]], UDF_ARGS *udf_args,
    char *result, unsigned long *length, unsigned char *is_null,
    unsigned char *error) noexcept {
  *is_null = 0;
  *error = 0;

  audit_table::AuditLogFilter audit_log_filter{udf->get_comp_registry_srv()};
  audit_table::AuditLogUser audit_log_user{udf->get_comp_registry_srv()};

  auto check_result = audit_log_filter.check_name_exists(udf_args->args[0]);

  if (check_result == audit_table::TableResult::Fail) {
    LogPluginErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                 "Failed to check filtering rule name existence");
    std::snprintf(result, MYSQL_ERRMSG_SIZE,
                  "ERROR: Failed to check filtering rule name existence");
    *length = std::strlen(result);
    return result;
  }

  if (check_result == audit_table::TableResult::NotFound) {
    std::snprintf(result, MYSQL_ERRMSG_SIZE, "OK");
    *length = std::strlen(result);
    return result;
  }

  if (audit_log_user.delete_user_by_filter(udf_args->args[0]) ==
      audit_table::TableResult::Fail) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "Failed to remove filter '%s' from users table",
                    udf_args->args[0]);
    std::snprintf(result, MYSQL_ERRMSG_SIZE,
                  "ERROR: Failed to remove filter from users table");
    *length = std::strlen(result);
    return result;
  }

  if (audit_log_filter.delete_filter(udf_args->args[0]) ==
      audit_table::TableResult::Fail) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "Failed to remove filter '%s'", udf_args->args[0]);
    std::snprintf(result, MYSQL_ERRMSG_SIZE, "ERROR: Failed to remove filter");
    *length = std::strlen(result);
    return result;
  }

  std::snprintf(result, MYSQL_ERRMSG_SIZE, "OK");
  *length = std::strlen(result);

  return result;
}

void AuditUdf::audit_log_filter_remove_filter_udf_deinit(UDF_INIT *) {}

// audit_log_filter_set_user(user_name, filter_name)
// user_name -> "user_name@host_name" or "%"
bool AuditUdf::audit_log_filter_set_user_udf_init(AuditUdf *udf,
                                                  UDF_INIT *initid,
                                                  UDF_ARGS *udf_args,
                                                  char *message) noexcept {
  if (udf_args->arg_count != 2) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument list: "
                  "audit_log_filter_set_user(user_name, filter_name)");
    return true;
  }

  if (udf_args->arg_type[0] != STRING_RESULT ||
      udf_args->arg_type[1] != STRING_RESULT) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument type: "
                  "audit_log_filter_set_user(string, string)");
    return true;
  }

  if (udf_args->lengths[0] == 0) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument: empty user name");
    return true;
  }

  if (udf_args->lengths[1] == 0) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument: empty filter name");
    return true;
  }

  auto user_info_data = check_parse_user_name_host(udf_args->args[0], message);

  if (user_info_data == nullptr) {
    return true;
  }

  if (udf_args->lengths[1] > audit_table::kAuditFieldLengthFiltername) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument: filter_name is too long, max length is %ld",
                  audit_table::kAuditFieldLengthFiltername);
    return true;
  }

  if (!udf->set_return_value_charset(initid) ||
      !udf->set_args_charset(udf_args)) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Unable to set character set service for "
                  "audit_log_filter_set_user UDF");
    return true;
  }

  initid->ptr = reinterpret_cast<char *>(user_info_data.release());
  initid->maybe_null = false;
  initid->const_item = false;

  return false;
}

char *AuditUdf::audit_log_filter_set_user_udf(AuditUdf *udf, UDF_INIT *initid,
                                              UDF_ARGS *udf_args, char *result,
                                              unsigned long *length,
                                              unsigned char *is_null,
                                              unsigned char *error) noexcept {
  *is_null = 0;
  *error = 0;

  audit_table::AuditLogFilter audit_log_filter{udf->get_comp_registry_srv()};
  audit_table::AuditLogUser audit_log_user{udf->get_comp_registry_srv()};

  std::string filter_name{udf_args->args[1]};

  auto filter_check_result = audit_log_filter.check_name_exists(filter_name);

  if (filter_check_result == audit_table::TableResult::Fail) {
    LogPluginErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                 "Failed to check filtering rule name existence");
    std::snprintf(result, MYSQL_ERRMSG_SIZE,
                  "ERROR: Failed to check filtering rule name existence");
    *length = std::strlen(result);
    return result;
  }

  if (filter_check_result == audit_table::TableResult::NotFound) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "Unknown filtering rule name '%s'", filter_name.c_str());
    std::snprintf(result, MYSQL_ERRMSG_SIZE,
                  "ERROR: Unknown filtering rule name '%s'",
                  filter_name.c_str());
    *length = std::strlen(result);
    return result;
  }

  auto *user_info_data = reinterpret_cast<UserNameInfo *>(initid->ptr);

  auto filter_set_result = audit_log_user.set_update_filter(
      user_info_data->username, user_info_data->userhost, filter_name);

  if (filter_set_result == audit_table::TableResult::Fail) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "Failed to set '%s' filtering rule for user '%s@%s'",
                    filter_name.c_str(), user_info_data->username,
                    user_info_data->userhost);
    std::snprintf(result, MYSQL_ERRMSG_SIZE,
                  "ERROR: Failed to set '%s' filtering rule for user '%s@%s'",
                  filter_name.c_str(), user_info_data->username,
                  user_info_data->userhost);
    *length = std::strlen(result);
    return result;
  }

  udf->get_mediator()->on_audit_rule_flush_requested();

  std::snprintf(result, MYSQL_ERRMSG_SIZE, "OK");
  *length = std::strlen(result);

  return result;
}

void AuditUdf::audit_log_filter_set_user_udf_deinit(UDF_INIT *initid) {
  if (initid != nullptr && initid->ptr != nullptr) {
    delete initid->ptr;
  }
}

// audit_log_filter_remove_user(user_name)
// user_name -> "user_name@host_name" or "%"
bool AuditUdf::audit_log_filter_remove_user_udf_init(AuditUdf *udf,
                                                     UDF_INIT *initid,
                                                     UDF_ARGS *udf_args,
                                                     char *message) noexcept {
  if (udf_args->arg_count != 1) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument list: "
                  "audit_log_filter_remove_user(user_name)");
    return true;
  }

  if (udf_args->arg_type[0] != STRING_RESULT) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument type: audit_log_filter_remove_user(string)");
    return true;
  }

  if (udf_args->lengths[0] == 0) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument: empty user name");
    return true;
  }

  auto user_info_data = check_parse_user_name_host(udf_args->args[0], message);

  if (user_info_data == nullptr) {
    return true;
  }

  if (!udf->set_return_value_charset(initid) ||
      !udf->set_args_charset(udf_args)) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Unable to set character set service for "
                  "audit_log_filter_remove_user UDF");
    return true;
  }

  initid->ptr = reinterpret_cast<char *>(user_info_data.release());
  initid->maybe_null = false;
  initid->const_item = false;

  return false;
}

char *AuditUdf::audit_log_filter_remove_user_udf(
    AuditUdf *udf, UDF_INIT *initid, UDF_ARGS *udf_args [[maybe_unused]],
    char *result, unsigned long *length, unsigned char *is_null,
    unsigned char *error) noexcept {
  *is_null = 0;
  *error = 0;

  audit_table::AuditLogUser audit_log_user{udf->get_comp_registry_srv()};

  auto *user_info_data = reinterpret_cast<UserNameInfo *>(initid->ptr);

  if (audit_log_user.delete_user_by_name_host(user_info_data->username,
                                              user_info_data->userhost) ==
      audit_table::TableResult::Fail) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "Failed to remove filter for user '%s@%s' from users table",
                    user_info_data->username, user_info_data->userhost);
    std::snprintf(result, MYSQL_ERRMSG_SIZE,
                  "ERROR: Failed to remove filter for user from users table");
    *length = std::strlen(result);
    return result;
  }

  udf->get_mediator()->on_audit_rule_flush_requested();

  std::snprintf(result, MYSQL_ERRMSG_SIZE, "OK");
  *length = std::strlen(result);

  return result;
}

void AuditUdf::audit_log_filter_remove_user_udf_deinit(UDF_INIT *initid) {
  if (initid != nullptr && initid->ptr != nullptr) {
    delete initid->ptr;
  }
}

// audit_log_filter_flush()
bool AuditUdf::audit_log_filter_flush_udf_init(AuditUdf *udf [[maybe_unused]],
                                               UDF_INIT *initid,
                                               UDF_ARGS *udf_args,
                                               char *message) noexcept {
  if (udf_args->arg_count != 0) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument list: audit_log_filter_flush()");
    return true;
  }

  initid->maybe_null = false;
  initid->const_item = false;

  return false;
}

char *AuditUdf::audit_log_filter_flush_udf(AuditUdf *udf [[maybe_unused]],
                                           UDF_INIT *initid [[maybe_unused]],
                                           UDF_ARGS *udf_args [[maybe_unused]],
                                           char *result, unsigned long *length,
                                           unsigned char *is_null,
                                           unsigned char *error) noexcept {
  if (udf->get_mediator()->on_audit_rule_flush_requested()) {
    std::snprintf(result, MYSQL_ERRMSG_SIZE, "OK");
  } else {
    std::snprintf(result, MYSQL_ERRMSG_SIZE,
                  "ERROR: Could not reinitialize audit log filters");
  }

  *length = std::strlen(result);
  *is_null = 0;
  *error = 0;

  return result;
}

void AuditUdf::audit_log_filter_flush_udf_deinit(UDF_INIT *) {}

// audit_log_read([arg])
bool AuditUdf::audit_log_read_udf_init(AuditUdf *udf [[maybe_unused]],
                                       UDF_INIT *initid, UDF_ARGS *udf_args,
                                       char *message) noexcept {
  if (SysVars::get_format_type() != AuditLogFormatType::Json) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Not supported for log formats other than JSON");
    return true;
  }

  if (udf_args->arg_count > 1) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument list: audit_log_read([arg])");
    return true;
  }

  my_service<SERVICE_TYPE(mysql_current_thread_reader)> thd_reader_srv(
      "mysql_current_thread_reader", udf->get_comp_registry_srv());

  MYSQL_THD thd;

  if (thd_reader_srv->get(&thd)) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE, "Internal error");
    return true;
  }

  const auto *reader_context = SysVars::get_log_reader_context(thd);

  if (udf_args->arg_count == 1) {
    if (udf_args->arg_type[0] != STRING_RESULT) {
      std::snprintf(message, MYSQL_ERRMSG_SIZE,
                    "Wrong argument type: audit_log_read(string)");
      return true;
    }

    // argument ether JSON null or JSON hash
    rapidjson::Document json_doc;
    json_doc.Parse(udf_args->args[0]);

    if (json_doc.HasParseError()) {
      std::snprintf(message, MYSQL_ERRMSG_SIZE, "Bad JSON format");
      return true;
    }

    if (!json_doc.IsNull()) {
      for (const auto &member : json_doc.GetObject()) {
        const auto *member_name = member.name.GetString();
        if (log_read_udf_allowed_args.count(member_name) == 0) {
          std::snprintf(message, MYSQL_ERRMSG_SIZE, "Wrong JSON argument: %s",
                        member_name);
          return true;
        }
      }
    }

    auto reader_args = std::make_unique<AuditLogReaderArgs>();

    if (reader_args == nullptr) {
      return true;
    }

    if (json_doc.IsObject()) {
      bool has_start_tag = json_doc.HasMember("start") &&
                           json_doc["start"].IsObject() &&
                           json_doc["start"].HasMember("timestamp");
      bool has_timestamp_tag = json_doc.HasMember("timestamp");
      bool has_id_tag = json_doc.HasMember("id");

      /*
       * Starting position for reads defined ether by "start" tag or by a
       * bookmark consisting of a combination of "timestamp" and "id".
       * Only one of this two ways may be used at the same time.
       */
      if ((has_timestamp_tag != has_id_tag) ||
          (has_start_tag && has_timestamp_tag) ||
          (reader_context != nullptr && (has_start_tag || has_timestamp_tag))) {
        std::snprintf(message, MYSQL_ERRMSG_SIZE, "Wrong argument format");
        return true;
      }

      if (has_start_tag) {
        if (!json_doc["start"]["timestamp"].IsString()) {
          std::snprintf(message, MYSQL_ERRMSG_SIZE,
                        "Wrong JSON argument: start timestamp is not a string");
          return true;
        }

        reader_args->timestamp = json_doc["start"]["timestamp"].GetString();
        reader_args->id = 0;
      } else if (has_timestamp_tag) {
        if (!json_doc["timestamp"].IsString() || !json_doc["id"].IsUint64()) {
          std::snprintf(message, MYSQL_ERRMSG_SIZE,
                        "Wrong JSON argument: bad bookmark format");
          return true;
        }

        reader_args->timestamp = json_doc["timestamp"].GetString();
        reader_args->id = json_doc["id"].GetUint();
      }

      if ((has_start_tag || has_timestamp_tag) &&
          reader_args->timestamp.empty()) {
        std::snprintf(message, MYSQL_ERRMSG_SIZE,
                      "Wrong JSON argument: bad timestamp format");
        return true;
      }

      if (json_doc.HasMember("max_array_length")) {
        if (json_doc["max_array_length"].IsUint()) {
          reader_args->max_array_length =
              json_doc["max_array_length"].GetUint();
        } else {
          std::snprintf(message, MYSQL_ERRMSG_SIZE,
                        "Wrong JSON argument: bad max_array_length format");
          return true;
        }
      }
    } else if (json_doc.IsNull()) {
      reader_args->close_read_sequence = true;
    } else {
      std::snprintf(message, MYSQL_ERRMSG_SIZE, "Wrong argument format");
      return true;
    }

    initid->ptr = reinterpret_cast<char *>(reader_args.release());
  } else if (reader_context == nullptr) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE, "Wrong argument format");
    return true;
  }

  initid->maybe_null = false;
  initid->const_item = false;

  return false;
}

char *AuditUdf::audit_log_read_udf(AuditUdf *udf [[maybe_unused]],
                                   UDF_INIT *initid [[maybe_unused]],
                                   UDF_ARGS *udf_args [[maybe_unused]],
                                   char *result, unsigned long *length,
                                   unsigned char *is_null,
                                   unsigned char *error) noexcept {
  /*
   * SELECT audit_log_read(audit_log_read_bookmark());
   * SELECT audit_log_read();
   * SELECT audit_log_read('null');
   *
   * audit_log_read() leads to error if:
   * - read sequence has not yet been initialized
   * - no more events left to be read, there was a null in prev result
   * - most recent read sequence has been closed
   */

  *is_null = 0;
  *error = 0;

  my_service<SERVICE_TYPE(mysql_current_thread_reader)> thd_reader_srv(
      "mysql_current_thread_reader", udf->get_comp_registry_srv());

  MYSQL_THD thd;

  if (thd_reader_srv->get(&thd)) {
    std::snprintf(result, MYSQL_ERRMSG_SIZE, "ERROR: Internal error");
    *length = std::strlen(result);
    return result;
  }

  auto *log_reader = get_audit_log_filter_instance()->get_log_reader();
  auto reader_args = std::unique_ptr<AuditLogReaderArgs>(
      reinterpret_cast<AuditLogReaderArgs *>(initid->ptr));
  initid->ptr = nullptr;
  auto *reader_context = SysVars::get_log_reader_context(thd);

  if (reader_args != nullptr) {
    if (reader_args->close_read_sequence) {
      if (reader_context != nullptr) {
        log_reader->close_reader_session(reader_context);
        SysVars::set_log_reader_context(thd, nullptr);
      }

      std::snprintf(result, MYSQL_ERRMSG_SIZE, "OK");
      *length = std::strlen(result);
      return result;
    }

    bool is_new_session_request = !reader_args->timestamp.empty();

    if ((reader_context == nullptr && !is_new_session_request) ||
        (reader_context != nullptr && is_new_session_request)) {
      std::snprintf(result, MYSQL_ERRMSG_SIZE, "ERROR: Wrong arguments list");
      *length = std::strlen(result);
      return result;
    }

    if (is_new_session_request) {
      reader_context = AuditLogReader::init_reader_session(reader_args.get());

      if (reader_context == nullptr) {
        std::snprintf(result, MYSQL_ERRMSG_SIZE,
                      "ERROR: Could not initialize reader session");
        *length = std::strlen(result);
        return result;
      }

      SysVars::set_log_reader_context(thd, reader_context);
    }
  }

  auto read_buf_size = SysVars::get_read_buffer_size(thd);
  initid->ptr = static_cast<char *>(my_malloc(
      key_memory_audit_log_filter_read_buffer, read_buf_size, MY_ZEROFILL));

  if (!log_reader->read(reader_args.get(), reader_context, initid->ptr,
                        read_buf_size)) {
    std::snprintf(result, MYSQL_ERRMSG_SIZE, "ERROR: Could not read log");
    *length = std::strlen(result);
    return result;
  }

  *length = std::strlen(initid->ptr);
  return initid->ptr;
}

void AuditUdf::audit_log_read_udf_deinit(UDF_INIT *initid) {
  if (initid != nullptr && initid->ptr != nullptr) {
    my_free(initid->ptr);
  }
}

// audit_log_read_bookmark()
bool AuditUdf::audit_log_read_bookmark_udf_init(AuditUdf *udf [[maybe_unused]],
                                                UDF_INIT *initid,
                                                UDF_ARGS *udf_args,
                                                char *message) noexcept {
  if (SysVars::get_format_type() != AuditLogFormatType::Json) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Not supported for log formats other than JSON");
    return true;
  }

  if (udf_args->arg_count != 0) {
    std::snprintf(message, MYSQL_ERRMSG_SIZE,
                  "Wrong argument list: audit_log_read_bookmark()");
    return true;
  }

  initid->maybe_null = false;
  initid->const_item = false;

  return false;
}

char *AuditUdf::audit_log_read_bookmark_udf(AuditUdf *udf [[maybe_unused]],
                                            UDF_INIT *initid [[maybe_unused]],
                                            UDF_ARGS *udf_args [[maybe_unused]],
                                            char *result, unsigned long *length,
                                            unsigned char *is_null,
                                            unsigned char *error) noexcept {
  auto bookmark = SysVars::get_log_bookmark();

  std::snprintf(result, MYSQL_ERRMSG_SIZE, R"({"timestamp": "%s", "id": %lu})",
                bookmark.timestamp.c_str(), bookmark.id);

  *length = std::strlen(result);
  *is_null = 0;
  *error = 0;

  return result;
}

void AuditUdf::audit_log_read_bookmark_udf_deinit(UDF_INIT *) {}

bool AuditUdf::set_return_value_charset(
    UDF_INIT *initid, const std::string &charset_name) noexcept {
  my_service<SERVICE_TYPE(mysql_udf_metadata)> udf_metadata_srv(
      "mysql_udf_metadata", m_comp_registry_srv);
  char *charset = const_cast<char *>(charset_name.c_str());
  return !udf_metadata_srv->result_set(initid, "charset",
                                       static_cast<void *>(charset));
}

bool AuditUdf::set_args_charset(UDF_ARGS *udf_args,
                                const std::string &charset_name) noexcept {
  my_service<SERVICE_TYPE(mysql_udf_metadata)> udf_metadata_srv(
      "mysql_udf_metadata", m_comp_registry_srv);
  char *charset = const_cast<char *>(charset_name.c_str());
  for (uint index = 0; index < udf_args->arg_count; ++index) {
    if (udf_args->arg_type[index] == STRING_RESULT &&
        udf_metadata_srv->argument_set(udf_args, "charset", index,
                                       static_cast<void *>(charset))) {
      return false;
    }
  }

  return true;
}

}  // namespace audit_log_filter
