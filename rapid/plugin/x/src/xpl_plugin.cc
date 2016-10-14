/*
   Copyright (c) 2015, 2016 Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

#if !defined(MYSQL_DYNAMIC_PLUGIN) && defined(WIN32) && !defined(XPLUGIN_UNIT_TESTS)
#define MYSQL_DYNAMIC_PLUGIN 1
#endif
#include <my_config.h>

#include <mysql/plugin.h>
#include <mysql_version.h>

#include "xpl_session.h"
#include "xpl_system_variables.h"
#include "xpl_server.h"
#include "xpl_performance_schema.h"
#include "mysqlx_version.h"
#include "xpl_replication_observer.h"
#include "xpl_log.h"

#include <stdio.h>                            // Solaris header file bug.
#include <limits>

#define BYTE(X)  (X)
#define KBYTE(X) ((X) * 1024)
#define MBYTE(X) ((X) * 1024 * 1024)
#define GBYTE(X) ((X) * 1024 * 1024 * 1024)

namespace
{

typedef void (*Xpl_status_variable_get)(THD *, st_mysql_show_var *, char *);

char *xpl_func_ptr(Xpl_status_variable_get callback)
{
  union
  {
    char *ptr;
    Xpl_status_variable_get callback;
  } ptr_cast;

  ptr_cast.callback = callback;

  return ptr_cast.ptr;
}

void exit_hook()
{
  google::protobuf::ShutdownProtobufLibrary();
}

} // namespace


/*
  Start the plugin: start webservers

  SYNOPSIS
    xpl_plugin_init()
    p plugin handle

  RETURN
     0  success
     1  error
 */
int xpl_plugin_init(MYSQL_PLUGIN p)
{
  static bool atexit_installed = false;
  if (!atexit_installed)
  {
    atexit_installed = true;
    atexit(exit_hook);
  }

  xpl::Plugin_system_variables::clean_callbacks();

  xpl_init_performance_schema();

  ngs::Connection_vio::init();

  if (xpl::xpl_register_server_observers(p) != 0)
  {
    xpl::plugin_log_message(&p, MY_WARNING_LEVEL, "Error registering server observers");

    return 1;
  }

  return xpl::Server::main(p);
}

/*
  Shutdown the plugin: stop webservers

  SYNOPSIS
    xpl_plugin_deinit()
    p plugin handle

  RETURN
     0  success
     1  error
 */
int xpl_plugin_deinit(MYSQL_PLUGIN p)
{
  if (xpl::xpl_unregister_server_observers(p) != 0)
    xpl::plugin_log_message(&p, MY_WARNING_LEVEL, "Error unregistering server observers");

  return xpl::Server::exit(p);
}


static struct st_mysql_daemon xpl_plugin_info ={
  MYSQL_DAEMON_INTERFACE_VERSION
};


static MYSQL_SYSVAR_UINT(port, xpl::Plugin_system_variables::port,
    PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_READONLY,
    "Port on which xplugin is going to accept incoming connections.",
    NULL, NULL, MYSQLX_TCP_PORT, 1, std::numeric_limits<unsigned short>::max(), 0);

static MYSQL_SYSVAR_INT(max_connections, xpl::Plugin_system_variables::max_connections,
    PLUGIN_VAR_OPCMDARG,
    "Maximum number of concurrent X protocol connections. Actual number of connections is also affected by the general max_connections.",
    NULL, NULL, 100, 1, std::numeric_limits<unsigned short>::max(), 0);

static MYSQL_SYSVAR_UINT(min_worker_threads, xpl::Plugin_system_variables::min_worker_threads,
    PLUGIN_VAR_OPCMDARG,
    "Minimal number of worker threads.",
    NULL, &xpl::Plugin_system_variables::update_func<unsigned int>, 2U, 1, 100, 0);

static MYSQL_SYSVAR_UINT(idle_worker_thread_timeout, xpl::Plugin_system_variables::idle_worker_thread_timeout,
    PLUGIN_VAR_OPCMDARG,
    "Time after which an idle worker thread is terminated (in seconds).",
    NULL, &xpl::Plugin_system_variables::update_func<unsigned int>, 60, 0, 60 * 60, 0);

static MYSQL_SYSVAR_UINT(max_allowed_packet, xpl::Plugin_system_variables::max_allowed_packet,
    PLUGIN_VAR_OPCMDARG,
    "Size of largest message that client is going to handle.",
    NULL, &xpl::Plugin_system_variables::update_func<unsigned int>, MBYTE(1), BYTE(512), GBYTE(1), 0);

static MYSQL_SYSVAR_UINT(connect_timeout, xpl::Plugin_system_variables::connect_timeout,
    PLUGIN_VAR_OPCMDARG,
    "Maximum allowed waiting time for connection to setup a session (in seconds).",
    NULL, &xpl::Plugin_system_variables::update_func<unsigned int>, 30, 1, 1000000000, 0);

static MYSQL_SYSVAR_STR(ssl_key, xpl::Plugin_system_variables::ssl_config.ssl_key,
      PLUGIN_VAR_READONLY | PLUGIN_VAR_MEMALLOC,
      "X509 key in PEM format.", NULL, NULL, NULL);

static MYSQL_SYSVAR_STR(ssl_ca, xpl::Plugin_system_variables::ssl_config.ssl_ca,
      PLUGIN_VAR_READONLY | PLUGIN_VAR_MEMALLOC,
      "CA file in PEM format.", NULL, NULL, NULL);

static MYSQL_SYSVAR_STR(ssl_capath, xpl::Plugin_system_variables::ssl_config.ssl_capath,
      PLUGIN_VAR_READONLY | PLUGIN_VAR_MEMALLOC,
      "CA directory.", NULL, NULL, NULL);

static MYSQL_SYSVAR_STR(ssl_cert, xpl::Plugin_system_variables::ssl_config.ssl_cert,
      PLUGIN_VAR_READONLY | PLUGIN_VAR_MEMALLOC,
      "X509 cert in PEM format.", NULL, NULL, NULL);

static MYSQL_SYSVAR_STR(ssl_cipher, xpl::Plugin_system_variables::ssl_config.ssl_cipher,
      PLUGIN_VAR_READONLY | PLUGIN_VAR_MEMALLOC,
      "SSL cipher to use.", NULL, NULL, NULL);

static MYSQL_SYSVAR_STR(ssl_crl, xpl::Plugin_system_variables::ssl_config.ssl_crl,
      PLUGIN_VAR_READONLY | PLUGIN_VAR_MEMALLOC,
      "Certificate revocation list.", NULL, NULL, NULL);

static MYSQL_SYSVAR_STR(ssl_crlpath, xpl::Plugin_system_variables::ssl_config.ssl_crlpath,
      PLUGIN_VAR_READONLY,
      "Certificate revocation list path.", NULL, NULL, NULL);

static MYSQL_SYSVAR_STR(socket, xpl::Plugin_system_variables::socket,
      PLUGIN_VAR_READONLY | PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_MEMALLOC,
      "X Plugins unix socket for local connection.", NULL, NULL, NULL);

#ifdef _WIN32
static MYSQL_SYSVAR_BOOL(named_pipe, xpl::Plugin_system_variables::named_pipe,
      PLUGIN_VAR_READONLY | PLUGIN_VAR_OPCMDARG,
      "X Plugins named pipes for local connection.", NULL, NULL, FALSE);
#endif // _WIN32

static struct st_mysql_sys_var* xpl_plugin_system_variables[]= {
  MYSQL_SYSVAR(port),
  MYSQL_SYSVAR(max_connections),
  MYSQL_SYSVAR(min_worker_threads),
  MYSQL_SYSVAR(idle_worker_thread_timeout),
  MYSQL_SYSVAR(max_allowed_packet),
  MYSQL_SYSVAR(connect_timeout),
  MYSQL_SYSVAR(ssl_key),
  MYSQL_SYSVAR(ssl_ca),
  MYSQL_SYSVAR(ssl_capath),
  MYSQL_SYSVAR(ssl_cert),
  MYSQL_SYSVAR(ssl_cipher),
  MYSQL_SYSVAR(ssl_crl),
  MYSQL_SYSVAR(ssl_crlpath),
  MYSQL_SYSVAR(socket),
#ifdef _WIN32
  MYSQL_SYSVAR(named_pipe),
#endif // _WIN32
  NULL
};


static struct st_mysql_show_var xpl_plugin_status[]=
{
  { MYSQLX_STATUS_VARIABLE_PREFIX("stmt_execute_sql"),               xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_stmt_execute_sql>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("stmt_execute_xplugin"),           xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_stmt_execute_xplugin>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("stmt_execute_mysqlx"),            xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_stmt_execute_mysqlx>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("crud_update"),                    xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_crud_update>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("crud_delete"),                    xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_crud_delete>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("crud_find"),                      xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_crud_find>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("crud_insert"),                    xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_crud_insert>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("expect_open"),                    xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_expect_open>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("expect_close"),                   xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_expect_close>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("stmt_create_collection"),         xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_stmt_create_collection>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("stmt_create_collection_index"),   xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_stmt_create_collection_index>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("stmt_drop_collection"),           xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_stmt_drop_collection>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("stmt_ensure_collection"),         xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_stmt_ensure_collection>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("stmt_drop_collection_index"),     xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_stmt_drop_collection_index>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("stmt_list_objects"),              xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_stmt_list_objects>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("stmt_enable_notices"),            xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_stmt_enable_notices>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("stmt_disable_notices"),           xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_stmt_disable_notices>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("stmt_list_notices"),              xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_stmt_list_notices>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("stmt_list_clients"),              xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_stmt_list_clients>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("stmt_kill_client"),               xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_stmt_kill_client>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("stmt_ping"),                      xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_stmt_ping>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("bytes_sent"),                     xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_bytes_sent>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("bytes_received"),                 xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_bytes_received>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("errors_sent"),                    xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_errors_sent>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("rows_sent"),                      xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_rows_sent>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("notice_warning_sent"),            xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_notice_warning_sent>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("notice_other_sent"),              xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_notice_other_sent>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("unknown_message_type"),           xpl_func_ptr(xpl::Server::common_status_variable<long long, &xpl::Common_status_variables::m_unknown_message_type>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("sessions"),                       xpl_func_ptr(xpl::Server::global_status_variable_server<long long, &xpl::Global_status_variables::m_sessions_count>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("sessions_closed"),                xpl_func_ptr(xpl::Server::global_status_variable_server<long long, &xpl::Global_status_variables::m_closed_sessions_count>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("sessions_fatal_error"),           xpl_func_ptr(xpl::Server::global_status_variable_server<long long, &xpl::Global_status_variables::m_sessions_fatal_errors_count>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("init_error"),                     xpl_func_ptr(xpl::Server::global_status_variable_server<long long, &xpl::Global_status_variables::m_init_errors_count>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("sessions_accepted"),              xpl_func_ptr(xpl::Server::global_status_variable_server<long long, &xpl::Global_status_variables::m_accepted_sessions_count>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("sessions_rejected"),              xpl_func_ptr(xpl::Server::global_status_variable_server<long long, &xpl::Global_status_variables::m_rejected_sessions_count>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("sessions_killed"),                xpl_func_ptr(xpl::Server::global_status_variable_server<long long, &xpl::Global_status_variables::m_killed_sessions_count>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("connections_closed"),             xpl_func_ptr(xpl::Server::global_status_variable_server<long long, &xpl::Global_status_variables::m_closed_connections_count>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("connections_accepted"),           xpl_func_ptr(xpl::Server::global_status_variable_server<long long, &xpl::Global_status_variables::m_accepted_connections_count>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("connections_rejected"),           xpl_func_ptr(xpl::Server::global_status_variable_server<long long, &xpl::Global_status_variables::m_rejected_connections_count>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("connection_accept_errors"),       xpl_func_ptr(xpl::Server::global_status_variable_server<long long, &xpl::Global_status_variables::m_connection_accept_errors_count>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("connection_errors"),              xpl_func_ptr(xpl::Server::global_status_variable_server<long long, &xpl::Global_status_variables::m_connection_errors_count>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("worker_threads"),                 xpl_func_ptr(xpl::Server::global_status_variable_server<long long, &xpl::Global_status_variables::m_worker_thread_count>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("worker_threads_active"),          xpl_func_ptr(xpl::Server::global_status_variable_server<long long, &xpl::Global_status_variables::m_active_worker_thread_count>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_active"),                     xpl_func_ptr(xpl::Server::session_status_variable<bool, &ngs::IOptions_session::active_tls>),              SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_cipher_list"),                xpl_func_ptr(xpl::Server::session_status_variable<&xpl::Client::get_status_ssl_cipher_list>),                    SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_cipher"),                     xpl_func_ptr(xpl::Server::session_status_variable<std::string, &ngs::IOptions_session::ssl_cipher>),              SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_version"),                    xpl_func_ptr(xpl::Server::session_status_variable<std::string, &ngs::IOptions_session::ssl_version>),             SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_verify_depth"),               xpl_func_ptr(xpl::Server::session_status_variable<long, &ngs::IOptions_session::ssl_verify_depth>),               SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_verify_mode"),                xpl_func_ptr(xpl::Server::session_status_variable<long, &ngs::IOptions_session::ssl_verify_mode>),                SHOW_FUNC, SHOW_SCOPE_GLOBAL },
//  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_sessions_reused"),            xpl_func_ptr(xpl::Server::session_status_variable<long, &ngs::IOptions_session::ssl_sessions_reused>),            SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_ctx_verify_depth"),           xpl_func_ptr(xpl::Server::global_status_variable<long, &ngs::IOptions_context::ssl_ctx_verify_depth>),            SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_ctx_verify_mode"),            xpl_func_ptr(xpl::Server::global_status_variable<long, &ngs::IOptions_context::ssl_ctx_verify_mode>),             SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_finished_accepts"),           xpl_func_ptr(xpl::Server::global_status_variable<long, &ngs::IOptions_context::ssl_sess_accept_good>),            SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_accepts"),                    xpl_func_ptr(xpl::Server::global_status_variable<long, &ngs::IOptions_context::ssl_sess_accept>),                 SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_server_not_after"),           xpl_func_ptr(xpl::Server::global_status_variable<std::string, &ngs::IOptions_context::ssl_server_not_after>),     SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_server_not_before"),          xpl_func_ptr(xpl::Server::global_status_variable<std::string, &ngs::IOptions_context::ssl_server_not_before>),    SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { MYSQLX_STATUS_VARIABLE_PREFIX("socket"),                         xpl_func_ptr(xpl::Server::global_status_variable_server_with_return<std::string, &xpl::Server::get_socket_file>), SHOW_FUNC, SHOW_SCOPE_GLOBAL },
//  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_accept_renegotiates"),        xpl_func_ptr(xpl::Server::global_status_variable<long, &ngs::IOptions_context::ssl_accept_renegotiates>),         SHOW_FUNC, SHOW_SCOPE_GLOBAL },
//  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_session_cache_hits"),         xpl_func_ptr(xpl::Server::global_status_variable<long, &ngs::IOptions_context::ssl_session_cache_hits>),          SHOW_FUNC, SHOW_SCOPE_GLOBAL },
//  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_session_cache_misses"),       xpl_func_ptr(xpl::Server::global_status_variable<long, &ngs::IOptions_context::ssl_session_cache_misses>),        SHOW_FUNC, SHOW_SCOPE_GLOBAL },
//  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_session_cache_mode"),         xpl_func_ptr(xpl::Server::global_status_variable<std::string, &ngs::IOptions_context::ssl_session_cache_mode>),   SHOW_FUNC, SHOW_SCOPE_GLOBAL },
//  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_session_cache_size"),         xpl_func_ptr(xpl::Server::global_status_variable<long, &ngs::IOptions_context::ssl_session_cache_size>),          SHOW_FUNC, SHOW_SCOPE_GLOBAL },
//  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_session_cache_timeouts"),     xpl_func_ptr(xpl::Server::global_status_variable<long, &ngs::IOptions_context::ssl_session_cache_timeouts>),      SHOW_FUNC, SHOW_SCOPE_GLOBAL },
//  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_session_cache_overflows"),    xpl_func_ptr(xpl::Server::global_status_variable<long, &ngs::IOptions_context::ssl_session_cache_overflows>),     SHOW_FUNC, SHOW_SCOPE_GLOBAL },
//  { MYSQLX_STATUS_VARIABLE_PREFIX("ssl_used_session_cache_entries"), xpl_func_ptr(xpl::Server::global_status_variable<long, &ngs::IOptions_context::ssl_used_session_cache_entries>),  SHOW_FUNC, SHOW_SCOPE_GLOBAL },
  { NULL, NULL, SHOW_BOOL, SHOW_SCOPE_GLOBAL}
};


mysql_declare_plugin(xpl)
{
  MYSQL_DAEMON_PLUGIN,
  &xpl_plugin_info,
  MYSQLX_PLUGIN_NAME,
  "Oracle Corp",
  "X Plugin for MySQL",
  PLUGIN_LICENSE_GPL,
  xpl_plugin_init,              /* init       */
  xpl_plugin_deinit,            /* deinit     */
  MYSQLX_PLUGIN_VERSION,           /* version    */
  xpl_plugin_status,            /* status var */
  xpl_plugin_system_variables,  /* system var */
  NULL,                        /* options    */
  0                            /* flags      */
}
mysql_declare_plugin_end;
