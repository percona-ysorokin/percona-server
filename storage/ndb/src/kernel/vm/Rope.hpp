/*
   Copyright (C) 2005, 2006 MySQL AB, 2009 Sun Microsystems, Inc.
    All rights reserved. Use is subject to license terms.

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

#ifndef NDB_ROPE_HPP
#define NDB_ROPE_HPP

#include "DataBuffer.hpp"

typedef DataBuffer<7> RopeBase;
typedef DataBuffer<7>::DataBufferPool RopePool;

struct RopeHandle {
  RopeHandle() { m_hash = 0; }

  Uint32 m_hash;
  RopeBase::Head m_head; 

  Uint32 hashValue() const { return m_hash; }
};

class ConstRope : private RopeBase {
public:
  ConstRope(RopePool& thePool, const RopeHandle& handle)  
    : RopeBase(thePool), src(handle)
  {
    this->head = src.m_head;
  }
  
  ~ConstRope(){
  }

  size_t size() const;
  bool empty() const;

  void copy(char* buf) const;
  
  int compare(const char * s) const { return compare(s, strlen(s) + 1); }
  int compare(const char *, size_t len) const; 
  
private:
  const RopeHandle & src;
};

class Rope : private RopeBase {
public:
  Rope(RopePool& thePool, RopeHandle& handle)  
    : RopeBase(thePool), src(handle)
  {
    this->head = src.m_head;
    m_hash = src.m_hash;
  }
  
  ~Rope(){
    src.m_head = this->head;
    src.m_hash = m_hash;
  }

  size_t size() const;
  bool empty() const;

  void copy(char* buf) const;
  
  int compare(const char * s) const { return compare(s, strlen(s) + 1); }
  int compare(const char *, size_t len) const; 
  
  bool assign(const char * s) { return assign(s, strlen(s) + 1);}
  bool assign(const char * s, size_t l) { return assign(s, l, hash(s, l));}
  bool assign(const char *, size_t len, Uint32 hash);

  void erase();
  
  static Uint32 hash(const char * str, Uint32 len);

  static Uint32 getSegmentSize() { return RopeBase::getSegmentSize();}
private:
  Uint32 m_hash;
  RopeHandle & src;
};

inline
size_t
Rope::size() const {
  return head.used;
}

inline
bool
Rope::empty() const {
  return head.used == 0;
}

inline
size_t
ConstRope::size() const {
  return head.used;
}

inline
bool
ConstRope::empty() const {
  return head.used == 0;
}

#endif

