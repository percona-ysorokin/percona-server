/*
 * Copyright (c) 2016, 2017, Oracle and/or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is also distributed with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms,
 * as designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have included with MySQL.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
 */

#ifndef XPL_LISTENER_UNIX_SOCKET_H_
#define XPL_LISTENER_UNIX_SOCKET_H_

#include "my_inttypes.h"
#include "plugin/x/ngs/include/ngs/interface/listener_interface.h"
#include "plugin/x/ngs/include/ngs/interface/socket_events_interface.h"
#include "plugin/x/ngs/include/ngs_common/operations_factory_interface.h"
#include "plugin/x/ngs/include/ngs_common/socket_interface.h"


namespace xpl {

class Listener_unix_socket: public ngs::Listener_interface {
public:
  typedef ngs::Socket_interface::Shared_ptr Socket_ptr;

  Listener_unix_socket(ngs::Operations_factory_interface::Shared_ptr operations_factory,
                       const std::string &unix_socket_path,
                       ngs::Socket_events_interface &event,
                       const uint32 backlog);
  ~Listener_unix_socket();

  bool is_handled_by_socket_event();

  Sync_variable_state &get_state();
  std::string get_name_and_configuration() const;
  std::string get_last_error();
  std::vector<std::string> get_configuration_variables() const;

  bool setup_listener(On_connection on_connection);
  void close_listener();
  void loop();

private:
  ngs::Operations_factory_interface::Shared_ptr m_operations_factory;
  const std::string m_unix_socket_path;
  const uint32 m_backlog;

  std::string m_last_error;
  Sync_variable_state m_state;

  Socket_ptr m_unix_socket;
  ::ngs::Socket_events_interface &m_event;
};

} // namespace xpl

#endif // XPL_LISTENER_UNIX_SOCKET_H_
