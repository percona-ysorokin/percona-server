#ifndef FAKE_COSTMODEL_CC_INCLUDED
#define FAKE_COSTMODEL_CC_INCLUDED

/*
   Copyright (c) 2014, 2022, Oracle and/or its affiliates.

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
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

#include "fake_costmodel.h"

Cost_model_server::~Cost_model_server()
{
}

const double Server_cost_constants::KEY_COMPARE_COST= 0.1;
const double Server_cost_constants::MEMORY_TEMPTABLE_CREATE_COST= 2.0;
const double Server_cost_constants::MEMORY_TEMPTABLE_ROW_COST= 0.2;
const double Server_cost_constants::DISK_TEMPTABLE_CREATE_COST= 40.0;
const double Server_cost_constants::DISK_TEMPTABLE_ROW_COST= 1.0;
const double Server_cost_constants::ROW_EVALUATE_COST= 0.2;
const double SE_cost_constants::MEMORY_BLOCK_READ_COST= 1.0;
const double SE_cost_constants::IO_BLOCK_READ_COST= 1.0;

/* purecov: begin inspected */
const SE_cost_constants
*Cost_model_constants::get_se_cost_constants(const TABLE *table) const
{
  // This is only implemented in order to link the unit tests
  assert(false);
  return NULL;
}
/* purecov: end */

/* purecov: begin inspected */
cost_constant_error SE_cost_constants::set(const LEX_CSTRING &name,
                                           const double value,
                                           bool default_value)
{
  // This is only implemented in order to link the unit tests
  assert(false);
  return COST_CONSTANT_OK;
}
/* purecov: end */

/* purecov: begin inspected */
Cost_model_se_info::~Cost_model_se_info()
{
  // This is only implemented in order to link the unit tests
  assert(false);
}
/* purecov: end */

/* purecov: begin inspected */
Cost_model_constants::~Cost_model_constants()
{
  // This is only implemented in order to link the unit tests
  assert(false);
}
/* purecov: end */

/* purecov: begin inspected */
uint Cost_model_constants::find_handler_slot_from_name(THD *thd,
                                           const LEX_CSTRING &name) const
{
  // This is only implemented in order to link the unit tests
  assert(false);
  return 0;
}
/* purecov: end */

#endif /* FAKE_COSTMODEL_CC_INCLUDED */
