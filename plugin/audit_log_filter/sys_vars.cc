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

#include "plugin/audit_log_filter/sys_vars.h"
#include "plugin/audit_log_filter/audit_error_log.h"
#include "plugin/audit_log_filter/audit_log_filter.h"

#include "mysql/plugin.h"
#include "sql/sql_const.h"
#include "sql/sql_error.h"
#include "sql/sql_plugin_var.h"

#include <syslog.h>
#include <atomic>

namespace audit_log_filter {
namespace {

/*
 * Status variables
 */
std::atomic<uint64_t> events_total{0};
std::atomic<uint64_t> events_lost{0};
std::atomic<uint64_t> events_filtered{0};
std::atomic<uint64_t> events_written{0};
std::atomic<uint64_t> write_waits{0};
std::atomic<uint64_t> event_max_drop_size{0};
std::atomic<uint64_t> current_log_size{0};
std::atomic<uint64_t> total_log_size{0};

int show_events_total(THD *, SHOW_VAR *var, char *buff) {
  var->type = SHOW_LONG;
  var->value = buff;
  auto *value = reinterpret_cast<uint64_t *>(buff);
  *value = events_total.load(std::memory_order_relaxed);
  return 0;
}

int show_events_lost(THD *, SHOW_VAR *var, char *buff) {
  var->type = SHOW_LONG;
  var->value = buff;
  auto *value = reinterpret_cast<uint64_t *>(buff);
  *value = events_lost.load(std::memory_order_relaxed);
  return 0;
}

int show_events_filtered(THD *, SHOW_VAR *var, char *buff) {
  var->type = SHOW_LONG;
  var->value = buff;
  auto *value = reinterpret_cast<uint64_t *>(buff);
  *value = events_filtered.load(std::memory_order_relaxed);
  return 0;
}

int show_events_written(THD *, SHOW_VAR *var, char *buff) {
  var->type = SHOW_LONG;
  var->value = buff;
  auto *value = reinterpret_cast<uint64_t *>(buff);
  *value = events_written.load(std::memory_order_relaxed);
  return 0;
}

int show_write_waits(THD *, SHOW_VAR *var, char *buff) {
  var->type = SHOW_LONG;
  var->value = buff;
  auto *value = reinterpret_cast<uint64_t *>(buff);
  *value = write_waits.load(std::memory_order_relaxed);
  return 0;
}

int show_event_max_drop_size(THD *, SHOW_VAR *var, char *buff) {
  var->type = SHOW_LONG;
  var->value = buff;
  auto *value = reinterpret_cast<uint64_t *>(buff);
  *value = event_max_drop_size.load(std::memory_order_relaxed);
  return 0;
}

int show_current_log_size(THD *, SHOW_VAR *var, char *buff) {
  var->type = SHOW_LONG;
  var->value = buff;
  auto *value = reinterpret_cast<uint64_t *>(buff);
  *value = current_log_size.load(std::memory_order_relaxed);
  return 0;
}

int show_total_log_size(THD *, SHOW_VAR *var, char *buff) {
  var->type = SHOW_LONG;
  var->value = buff;
  auto *value = reinterpret_cast<uint64_t *>(buff);
  *value = total_log_size.load(std::memory_order_relaxed);
  return 0;
}

SHOW_VAR status_vars[] = {
    {"Audit_log_filter_events", reinterpret_cast<char *>(&show_events_total),
     SHOW_FUNC, SHOW_SCOPE_GLOBAL},
    {"Audit_log_filter_events_lost",
     reinterpret_cast<char *>(&show_events_lost), SHOW_FUNC, SHOW_SCOPE_GLOBAL},
    {"Audit_log_filter_events_filtered",
     reinterpret_cast<char *>(&show_events_filtered), SHOW_FUNC,
     SHOW_SCOPE_GLOBAL},
    {"Audit_log_filter_events_written",
     reinterpret_cast<char *>(&show_events_written), SHOW_FUNC,
     SHOW_SCOPE_GLOBAL},
    {"Audit_log_filter_write_waits",
     reinterpret_cast<char *>(&show_write_waits), SHOW_FUNC, SHOW_SCOPE_GLOBAL},
    {"Audit_log_filter_event_max_drop_size",
     reinterpret_cast<char *>(&show_event_max_drop_size), SHOW_FUNC,
     SHOW_SCOPE_GLOBAL},
    {"Audit_log_filter_current_size",
     reinterpret_cast<char *>(&show_current_log_size), SHOW_FUNC,
     SHOW_SCOPE_GLOBAL},
    {"Audit_log_filter_total_size",
     reinterpret_cast<char *>(&show_total_log_size), SHOW_FUNC,
     SHOW_SCOPE_GLOBAL},
    {nullptr, nullptr, SHOW_UNDEF, SHOW_SCOPE_UNDEF}};

/*
 * System variables
 */
char *log_file_name;
const char default_log_file_name[] = "audit_filter.log";
ulong log_handler_type = static_cast<ulong>(AuditLogHandlerType::File);
ulong log_format_type = static_cast<ulong>(AuditLogFormatType::New);
ulong log_strategy_type =
    static_cast<ulong>(AuditLogStrategyType::Asynchronous);
ulonglong log_write_buffer_size = 1048576UL;
ulonglong log_rotate_on_size = 0;
ulonglong log_max_size = 0;
ulonglong log_prune_seconds = 0;
bool log_flush_requested = false;
char *log_syslog_ident = nullptr;
const char default_log_syslog_ident[] = "percona-audit-event-filter";
ulong log_syslog_facility = 0;
ulong log_syslog_priority = 0;

/*
 * The audit_log_filter.file variable is used to specify the filename that’s
 * going to store the audit log. It can contain the path relative to the
 * datadir or absolute path.
 */
MYSQL_SYSVAR_STR(file, log_file_name,
                 PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY |
                     PLUGIN_VAR_MEMALLOC,
                 "The name of the log file.", nullptr, nullptr,
                 default_log_file_name);

/*
 * The audit_log_filter.handler variable is used to configure where the
 * audit log will be written. If it is set to FILE, the log will be written
 * into a file specified by audit_log_filter.file variable. If it is set to
 * SYSLOG, the audit log will be written to syslog.
 */
const char *audit_log_filter_handler_names[] = {"FILE", "SYSLOG", nullptr};
TYPELIB audit_log_filter_handler_typelib = {
    array_elements(audit_log_filter_handler_names) - 1,
    "audit_log_filter_handler_typelib", audit_log_filter_handler_names,
    nullptr};

MYSQL_SYSVAR_ENUM(handler, log_handler_type,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "The audit log handler.", nullptr, nullptr,
                  static_cast<ulong>(AuditLogHandlerType::File),
                  &audit_log_filter_handler_typelib);

/*
 * The audit_log_filter.format variable is used to specify the audit filter
 * log format. The audit log filter plugin supports four log formats:
 * OLD, NEW, JSON, and CSV. OLD and NEW formats are based on XML, where
 * the former outputs log record properties as XML attributes and the latter
 * as XML tags.
 */
const char *audit_log_filter_format_names[] = {"NEW", "OLD", "JSON", "CSV",
                                               nullptr};
TYPELIB audit_log_filter_format_typelib = {
    array_elements(audit_log_filter_format_names) - 1,
    "audit_log_filter_format_typelib", audit_log_filter_format_names, nullptr};

static MYSQL_SYSVAR_ENUM(format, log_format_type,
                         PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                         "The audit log file format.", nullptr, nullptr,
                         static_cast<ulong>(AuditLogFormatType::New),
                         &audit_log_filter_format_typelib);

/*
 * The audit_log_filter.strategy variable is used to specify the audit log
 * filter strategy, possible values are:
 * ASYNCHRONOUS - (default) log using memory buffer, do not drop messages
 *                if buffer is full
 * PERFORMANCE - log using memory buffer, drop messages if buffer is full
 * SEMISYNCHRONOUS - log directly to file, do not flush and sync every event
 * SYNCHRONOUS - log directly to file, flush and sync every event.
 *
 * This variable has effect only when audit_log_handler is set to FILE.
 */
const char *audit_log_filter_strategy_names[] = {
    "ASYNCHRONOUS", "PERFORMANCE", "SEMISYNCHRONOUS", "SYNCHRONOUS", nullptr};
TYPELIB audit_log_filter_strategy_typelib = {
    array_elements(audit_log_filter_strategy_names) - 1,
    "audit_log_filter_strategy_typelib", audit_log_filter_strategy_names,
    nullptr};

MYSQL_SYSVAR_ENUM(
    strategy, log_strategy_type, PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "The logging method used by the audit log plugin, if FILE handler is used.",
    nullptr, nullptr, static_cast<ulong>(AuditLogStrategyType::Asynchronous),
    &audit_log_filter_strategy_typelib);

/*
 * The audit_log_filter.buffer_size variable can be used to specify the size
 * of memory buffer used for logging, used when audit_log_filter.strategy
 * variable is set to ASYNCHRONOUS or PERFORMANCE values. This variable has
 * effect only when audit_log_filter.handler is set to FILE.
 */
MYSQL_SYSVAR_ULONGLONG(buffer_size, log_write_buffer_size,
                       PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                       "The size of the buffer for asynchronous logging, "
                       "if FILE handler is used.",
                       nullptr, nullptr, 1048576UL, 4096UL, ULLONG_MAX, 4096UL);

/*
 * The audit_log_filter.rotate_on_size variable specifies the maximum size
 * of the audit log file. Upon reaching this size, the audit log will be
 * rotated. For this variable to take effect, set the audit_log_filter.handler
 * variable to FILE and the audit_log_filter.rotations variable to a value
 * greater than zero.
 */
MYSQL_SYSVAR_ULONGLONG(
    rotate_on_size, log_rotate_on_size, PLUGIN_VAR_RQCMDARG,
    "Maximum size of the log to start the rotation, if FILE handler is used.",
    nullptr, nullptr, 0UL, 0UL, ULLONG_MAX, 4096UL);

/*
 * A value greater than 0 enables size-based pruning. The value is the
 * combined size above which audit log files become subject to pruning.
 */
void max_size_update_func(MYSQL_THD thd, SYS_VAR *, void *val_ptr,
                          const void *save) {
  const auto *val = static_cast<const ulonglong *>(save);
  *static_cast<ulonglong *>(val_ptr) = *val;

  if (*val > 0) {
    if (SysVars::get_log_prune_seconds() > 0) {
      push_warning(thd, Sql_condition::SL_WARNING,
                   ER_WARN_ADUIT_FILTER_MAX_SIZE_AND_PRUNE_SECONDS, nullptr);
    }

    get_audit_log_filter_instance()->on_audit_log_prune_requested();
  }
}

MYSQL_SYSVAR_ULONGLONG(
    max_size, log_max_size, PLUGIN_VAR_OPCMDARG,
    "The maximum combined size of log files in bytes after which log "
    "files become subject to pruning.",
    nullptr, max_size_update_func, 0UL, 0UL, ULLONG_MAX, 4096UL);

/*
 * A value greater than 0 enables age-based pruning. The value is the number
 * of seconds after which log files become subject to pruning.
 */
void prune_seconds_update_func(MYSQL_THD thd, SYS_VAR *, void *val_ptr,
                               const void *save) {
  const auto *val = static_cast<const ulonglong *>(save);
  *static_cast<ulonglong *>(val_ptr) = *val;

  if (*val > 0) {
    if (SysVars::get_log_max_size() > 0) {
      push_warning(thd, Sql_condition::SL_WARNING,
                   ER_WARN_ADUIT_FILTER_MAX_SIZE_AND_PRUNE_SECONDS, nullptr);
    }

    get_audit_log_filter_instance()->on_audit_log_prune_requested();
  }
}

MYSQL_SYSVAR_ULONGLONG(
    prune_seconds, log_prune_seconds, PLUGIN_VAR_OPCMDARG,
    "The maximum log file age in seconds after which log file "
    "become subject to pruning.",
    nullptr, prune_seconds_update_func, 0UL, 0UL, ULLONG_MAX, 0UL);

/*
 * When this variable is set to ON log file will be closed and reopened.
 * This can be used for manual log rotation.
 */
void flush_update_func(MYSQL_THD, SYS_VAR *, void *, const void *save) {
  const auto *val = static_cast<const bool *>(save);

  if (*val && SysVars::get_rotate_on_size() == 0) {
    get_audit_log_filter_instance()->on_audit_log_flush_requested();
  }
}

MYSQL_SYSVAR_BOOL(flush, log_flush_requested, PLUGIN_VAR_NOCMDARG,
                  "Close and reopen log file when set to ON.", nullptr,
                  flush_update_func, false);

/*
 * The audit_log_filter.syslog_ident variable is used to specify the ident
 * value for syslog.
 */
MYSQL_SYSVAR_STR(syslog_ident, log_syslog_ident,
                 PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY |
                     PLUGIN_VAR_MEMALLOC,
                 "The string that will be prepended to each log message, "
                 "if SYSLOG handler is used.",
                 nullptr, nullptr, default_log_syslog_ident);

/*
 * The audit_log_filter.syslog_facility variable is used to specify the
 * facility value for syslog.
 */
const int audit_log_filter_syslog_facility_codes[] = {
    LOG_USER,     LOG_AUTHPRIV, LOG_CRON,   LOG_DAEMON, LOG_FTP,    LOG_KERN,
    LOG_LPR,      LOG_MAIL,     LOG_NEWS,
#if (defined LOG_SECURITY)
    LOG_SECURITY,
#endif
    LOG_SYSLOG,   LOG_AUTH,     LOG_UUCP,   LOG_LOCAL0, LOG_LOCAL1, LOG_LOCAL2,
    LOG_LOCAL3,   LOG_LOCAL4,   LOG_LOCAL5, LOG_LOCAL6, LOG_LOCAL7, 0};

const char *audit_log_filter_syslog_facility_names[] = {
    "LOG_USER",     "LOG_AUTHPRIV", "LOG_CRON",   "LOG_DAEMON", "LOG_FTP",
    "LOG_KERN",     "LOG_LPR",      "LOG_MAIL",   "LOG_NEWS",
#if (defined LOG_SECURITY)
    "LOG_SECURITY",
#endif
    "LOG_SYSLOG",   "LOG_AUTH",     "LOG_UUCP",   "LOG_LOCAL0", "LOG_LOCAL1",
    "LOG_LOCAL2",   "LOG_LOCAL3",   "LOG_LOCAL4", "LOG_LOCAL5", "LOG_LOCAL6",
    "LOG_LOCAL7",   nullptr};

TYPELIB audit_log_filter_syslog_facility_typelib = {
    array_elements(audit_log_filter_syslog_facility_names) - 1,
    "audit_log_filter_syslog_facility_typelib",
    audit_log_filter_syslog_facility_names, nullptr};

MYSQL_SYSVAR_ENUM(
    syslog_facility, log_syslog_facility, PLUGIN_VAR_RQCMDARG,
    "The syslog facility to assign to messages, if SYSLOG handler is used.",
    nullptr, nullptr, 0, &audit_log_filter_syslog_facility_typelib);

/*
 * The audit_log_filter.syslog_priority variable is used to specify the
 * priority value for syslog. This variable has the same meaning as the
 * appropriate parameter described in the syslog(3) manual.
 */
const int audit_log_filter_syslog_priority_codes[] = {
    LOG_INFO,   LOG_ALERT, LOG_CRIT,  LOG_ERR, LOG_WARNING,
    LOG_NOTICE, LOG_EMERG, LOG_DEBUG, 0};

const char *audit_log_filter_syslog_priority_names[] = {
    "LOG_INFO",   "LOG_ALERT", "LOG_CRIT",  "LOG_ERR", "LOG_WARNING",
    "LOG_NOTICE", "LOG_EMERG", "LOG_DEBUG", 0};

TYPELIB audit_log_filter_syslog_priority_typelib = {
    array_elements(audit_log_filter_syslog_priority_names) - 1,
    "audit_log_filter_syslog_priority_typelib",
    audit_log_filter_syslog_priority_names, nullptr};

MYSQL_SYSVAR_ENUM(syslog_priority, log_syslog_priority, PLUGIN_VAR_RQCMDARG,
                  "Priority to be assigned to all messages written to syslog.",
                  nullptr, nullptr, 0,
                  &audit_log_filter_syslog_priority_typelib);

SYS_VAR *sys_vars[] = {MYSQL_SYSVAR(file),
                       MYSQL_SYSVAR(handler),
                       MYSQL_SYSVAR(format),
                       MYSQL_SYSVAR(strategy),
                       MYSQL_SYSVAR(buffer_size),
                       MYSQL_SYSVAR(rotate_on_size),
                       MYSQL_SYSVAR(max_size),
                       MYSQL_SYSVAR(prune_seconds),
                       MYSQL_SYSVAR(flush),
                       MYSQL_SYSVAR(syslog_ident),
                       MYSQL_SYSVAR(syslog_facility),
                       MYSQL_SYSVAR(syslog_priority),
                       nullptr};

}  // namespace

SHOW_VAR *SysVars::get_status_var_defs() noexcept { return status_vars; }

SYS_VAR **SysVars::get_sys_var_defs() noexcept { return sys_vars; }

void SysVars::validate() noexcept {
  if (SysVars::get_log_max_size() > 0 && SysVars::get_log_prune_seconds() > 0) {
    LogPluginErr(WARNING_LEVEL,
                 ER_WARN_ADUIT_FILTER_MAX_SIZE_AND_PRUNE_SECONDS_LOG);
  }
}

// TODO: support for
// sys vars
//  MYSQL_SYSVAR(record_buffer),
//  MYSQL_SYSVAR(query_stack),
//  audit_log_current_session
//  audit_log_disable
//  audit_log_filter_id
//  audit_log_password_history_keep_days
//  audit_log_read_buffer_size

const char *SysVars::get_file_name() noexcept { return log_file_name; }

AuditLogHandlerType SysVars::get_handler_type() noexcept {
  return static_cast<AuditLogHandlerType>(log_handler_type);
}

AuditLogFormatType SysVars::get_format_type() noexcept {
  return static_cast<AuditLogFormatType>(log_format_type);
}

AuditLogStrategyType SysVars::get_file_strategy_type() noexcept {
  return static_cast<AuditLogStrategyType>(log_strategy_type);
}

ulonglong SysVars::get_buffer_size() noexcept { return log_write_buffer_size; }

ulonglong SysVars::get_rotate_on_size() noexcept { return log_rotate_on_size; }

ulonglong SysVars::get_log_max_size() noexcept { return log_max_size; }

ulonglong SysVars::get_log_prune_seconds() noexcept {
  return log_prune_seconds;
}

const char *SysVars::get_syslog_ident() noexcept { return log_syslog_ident; }

int SysVars::get_syslog_facility() noexcept {
  return audit_log_filter_syslog_facility_codes[log_syslog_facility];
}

int SysVars::get_syslog_priority() noexcept {
  return audit_log_filter_syslog_priority_codes[log_syslog_priority];
}

uint64_t SysVars::get_events_lost() noexcept {
  return events_lost.load(std::memory_order_relaxed);
}

void SysVars::inc_events_total() noexcept {
  events_total.fetch_add(1, std::memory_order_relaxed);
}

void SysVars::inc_events_lost() noexcept {
  events_lost.fetch_add(1, std::memory_order_relaxed);
}

void SysVars::inc_events_filtered() noexcept {
  events_filtered.fetch_add(1, std::memory_order_relaxed);
}

void SysVars::inc_events_written() noexcept {
  events_written.fetch_add(1, std::memory_order_relaxed);
}

void SysVars::inc_write_waits() noexcept {
  write_waits.fetch_add(1, std::memory_order_relaxed);
}

void SysVars::update_event_max_drop_size(uint64_t size) noexcept {
  uint64_t prev_max_size = event_max_drop_size.load();
  while (prev_max_size < size &&
         !event_max_drop_size.compare_exchange_weak(prev_max_size, size)) {
  }
}

void SysVars::set_current_log_size(uint64_t size) noexcept {
  uint64_t current_size = current_log_size.load();
  while (!current_log_size.compare_exchange_weak(current_size, size)) {
  }
}

void SysVars::update_current_log_size(uint64_t size) noexcept {
  current_log_size.fetch_add(size, std::memory_order_relaxed);
}

void SysVars::set_total_log_size(uint64_t size) noexcept {
  uint64_t current_size = total_log_size.load();
  while (!total_log_size.compare_exchange_weak(current_size, size)) {
  }
}

void SysVars::update_total_log_size(uint64_t size) noexcept {
  total_log_size.fetch_add(size, std::memory_order_relaxed);
}

}  // namespace audit_log_filter
