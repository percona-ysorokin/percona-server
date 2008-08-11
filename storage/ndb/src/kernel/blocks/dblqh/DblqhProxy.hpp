/* Copyright (C) 2003 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef NDB_DBLQH_PROXY_HPP
#define NDB_DBLQH_PROXY_HPP

#include <LocalProxy.hpp>

class DblqhProxy : public LocalProxy {
public:
  DblqhProxy(Block_context& ctx);
  virtual ~DblqhProxy();
  BLOCK_DEFINES(DblqhProxy);

protected:
  virtual SimulatedBlock* newWorker(Uint32 instanceNo);

  // GSN_SEND_PACKED
  void execSEND_PACKED(Signal*);

  // GSN_NDB_STTOR
  virtual void callNDB_STTOR(Signal*);
};

#endif
