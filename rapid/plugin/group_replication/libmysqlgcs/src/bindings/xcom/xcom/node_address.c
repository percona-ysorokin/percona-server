/* Copyright (c) 2015, 2017, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include "plugin/group_replication/libmysqlgcs/src/bindings/xcom/xcom/node_address.h"

#include <rpc/rpc.h>

#include "plugin/group_replication/libmysqlgcs/src/bindings/xcom/xcom/task_debug.h"
#include "plugin/group_replication/libmysqlgcs/src/bindings/xcom/xcom/xcom_common.h"
#include "plugin/group_replication/libmysqlgcs/src/bindings/xcom/xcom/xcom_profile.h"

/**
   Debug a node address.
 */
/* purecov: begin deadcode */
char *dbg_node_address(node_address n) {
  GET_NEW_GOUT;
  STRLIT("node_address ");
  PTREXP(n.address);
  STRLIT(n.address);
  STRLIT(" ");
  RET_GOUT;
}
/* purecov: end */
