/*
  Copyright (c) 2023, Oracle and/or its affiliates.

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

#include "classic_set_option_sender.h"

#include "classic_connection_base.h"
#include "classic_frame.h"
#include "mysql/harness/stdx/expected.h"
#include "mysql/harness/tls_error.h"
#include "mysql_com.h"  // MYSQL_OPTION_*
#include "mysqlrouter/classic_protocol_constants.h"
#include "tracer.h"

// send COM_SET_OPTION from router to server.

stdx::expected<Processor::Result, std::error_code> SetOptionSender::process() {
  switch (stage()) {
    case Stage::Command:
      return command();
    case Stage::Response:
      return response();
    case Stage::Eof:
      return eof();
    case Stage::Error:
      return error();
    case Stage::Done:
      return Result::Done;
  }

  harness_assert_this_should_not_execute();
}

stdx::expected<Processor::Result, std::error_code> SetOptionSender::command() {
  auto &dst_conn = connection()->server_conn();
  auto &dst_protocol = dst_conn.protocol();

  if (auto &tr = tracer()) {
    tr.trace(Tracer::Event().stage("set_option::command: " +
                                   std::to_string(option_)));
  }

  dst_protocol.seq_id(0xff);

  auto send_res = ClassicFrame::send_msg(
      dst_conn,
      classic_protocol::borrowed::message::client::SetOption{option_});
  if (!send_res) return send_server_failed(send_res.error());

  stage(Stage::Response);
  return Result::SendToServer;
}

stdx::expected<Processor::Result, std::error_code> SetOptionSender::response() {
  auto &src_conn = connection()->server_conn();
  auto &src_protocol = src_conn.protocol();

  auto read_res = ClassicFrame::ensure_has_msg_prefix(src_conn);
  if (!read_res) return recv_server_failed(read_res.error());

  const uint8_t msg_type = src_protocol.current_msg_type().value();

  enum class Msg {
    Error = ClassicFrame::cmd_byte<classic_protocol::message::server::Error>(),
    Eof = ClassicFrame::cmd_byte<classic_protocol::message::server::Eof>(),
  };

  switch (Msg{msg_type}) {
    case Msg::Eof:
      stage(Stage::Eof);
      return Result::Again;
    case Msg::Error:
      stage(Stage::Error);
      return Result::Again;
  }

  if (auto &tr = tracer()) {
    tr.trace(Tracer::Event().stage("set_option::response"));
  }

  return stdx::unexpected(make_error_code(std::errc::bad_message));
}

stdx::expected<Processor::Result, std::error_code> SetOptionSender::eof() {
  auto &src_conn = connection()->server_conn();
  auto &src_protocol = src_conn.protocol();

  auto msg_res =
      ClassicFrame::recv_msg<classic_protocol::borrowed::message::server::Eof>(
          src_conn);
  if (!msg_res) return recv_server_failed(msg_res.error());

  if (auto &tr = tracer()) {
    tr.trace(Tracer::Event().stage("set_option::ok"));
  }

  auto msg = *msg_res;

  if (!msg.session_changes().empty()) {
    auto track_res = connection()->track_session_changes(
        net::buffer(msg.session_changes()), src_protocol.shared_capabilities(),
        true /* ignore some-stage-changed. */
    );
    if (!track_res) {
      // ignore
    }
  }

  auto cap = classic_protocol::capabilities::pos::multi_statements;

  switch (option_) {
    case MYSQL_OPTION_MULTI_STATEMENTS_OFF:
      src_protocol.client_capabilities(
          src_protocol.client_capabilities().reset(cap));
      break;
    case MYSQL_OPTION_MULTI_STATEMENTS_ON:
      src_protocol.client_capabilities(
          src_protocol.client_capabilities().set(cap));
      break;
  }

  discard_current_msg(src_conn);

  stage(Stage::Done);
  return Result::Again;
}

stdx::expected<Processor::Result, std::error_code> SetOptionSender::error() {
  auto &src_conn = connection()->server_conn();

  auto msg_res = ClassicFrame::recv_msg<
      classic_protocol::borrowed::message::server::Error>(src_conn);
  if (!msg_res) return recv_server_failed(msg_res.error());

  if (auto &tr = tracer()) {
    tr.trace(Tracer::Event().stage("set_option::error"));
  }

  discard_current_msg(src_conn);

  stage(Stage::Done);
  return Result::Again;
}
