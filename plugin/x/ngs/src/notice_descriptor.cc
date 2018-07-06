/*
 * Copyright (c) 2018, Oracle and/or its affiliates. All rights reserved.
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

#include "plugin/x/ngs/include/ngs/notice_descriptor.h"

#include <array>

namespace ngs {

const std::array<Notice_type, 4> Notice_descriptor::dispatchables{
    {Notice_type::k_group_replication_quorum_loss,
     Notice_type::k_group_replication_view_changed,
     Notice_type::k_group_replication_member_role_changed,
     Notice_type::k_group_replication_member_state_changed}};

Notice_descriptor::Notice_descriptor(const Notice_type notice_type,
                                     const std::string &payload)
    : m_notice_type(notice_type), m_payload(payload) {}

}  // namespace ngs
