/*
 * Copyright (c) 2015, 2017, Oracle and/or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */

#ifndef _NGS_PROTOCOL_CONFIG_H_
#define _NGS_PROTOCOL_CONFIG_H_

#include <stdint.h>
#include <list>

#include "plugin/x/ngs/include/ngs_common/chrono.h"

#include "plugin/x/src/global_timeouts.h"
#include "plugin/x/src/xpl_system_variables.h"

namespace ngs
{

class Protocol_config
{
public:
  uint32_t default_max_frame_size;
  uint32_t max_message_size;

  chrono::seconds connect_timeout;
  chrono::milliseconds connect_timeout_hysteresis;
  uint32_t m_wait_timeout = Global_timeouts::Default::k_wait_timeout;
  uint32_t m_interactive_timeout =
      Global_timeouts::Default::k_interactive_timeout;
  uint32_t m_read_timeout = Global_timeouts::Default::k_read_timeout;
  uint32_t m_write_timeout = Global_timeouts::Default::k_write_timeout;

  void set_global_timeouts(const Global_timeouts &timeouts) {
    m_interactive_timeout = timeouts.interactive_timeout;
    m_wait_timeout = timeouts.wait_timeout;
    m_read_timeout = timeouts.read_timeout;
    m_write_timeout = timeouts.write_timeout;
  }

  Global_timeouts get_global_timeouts() const {
    return {m_interactive_timeout, m_wait_timeout, m_read_timeout,
        m_write_timeout};
  }

  Protocol_config()
  : default_max_frame_size(16*1024*1024),
    max_message_size(16*1024*1024),
    connect_timeout(0),
    connect_timeout_hysteresis(100)
  {
  }
};

} // namespace ngs

#endif // _NGS_PROTOCOL_CONFIG_H_
