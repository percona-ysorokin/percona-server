/* Copyright (c) 2018, 2024, Oracle and/or its affiliates.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License, version 2.0,
as published by the Free Software Foundation.

This program is designed to work with certain software (including
but not limited to OpenSSL) that is licensed under separate terms,
as designated in a particular file or component or in included license
documentation.  The authors of MySQL hereby grant you an additional
permission to link the program and your derivative works with the
separately licensed software that they have either included with
the program or referenced in the documentation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License, version 2.0, for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef AUDIT_API_MESSAGE_H
#define AUDIT_API_MESSAGE_H

#include <mysql/components/service.h>
#include <mysql/components/services/dynamic_privilege.h>
#include <mysql/plugin_audit_message_types.h>

BEGIN_SERVICE_DEFINITION(mysql_audit_api_message)

/**
  Method that emits Audit API message event.

  @param type                 Message event type.
  @param component            Component name.
  @param component_length     Component name length.
  @param producer             Producer name.
  @param producer_length      Producer name length.
  @param message              Message.
  @param message_length       Message length.
  @param key_value_map        Key-value map pointer. The function does not
                              perform key values uniqueness.
  @param key_value_map_length Key-value map length;

  @return This method always generates false no matter, whether the message
          event is consumed by the audit plugin or not.
*/
DECLARE_BOOL_METHOD(emit, (mysql_event_message_subclass_t type,
                           const char *component, size_t component_length,
                           const char *producer, size_t producer_length,
                           const char *message, size_t message_length,
                           mysql_event_message_key_value_t *key_value_map,
                           size_t key_value_map_length));

END_SERVICE_DEFINITION(mysql_audit_api_message)

#endif /* AUDIT_API_MESSAGE_H */
