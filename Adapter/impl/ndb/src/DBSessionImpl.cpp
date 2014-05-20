/*
 Copyright (c) 2014, Oracle and/or its affiliates. All rights
 reserved.
 
 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; version 2 of
 the License.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 02110-1301  USA
*/

#include <NdbApi.hpp>

#include "adapter_global.h"
#include "unified_debug.h"
#include "AsyncNdbContext.h"
#include "KeyOperation.h"
#include "DBSessionImpl.h"
#include "DBTransactionContext.h"


//////////
/////////////////
///////////////////////// CachedTransactionsAccountant
/////////////////
//////////

CachedTransactionsAccountant::CachedTransactionsAccountant(Ndb_cluster_connection *conn,
                                                           int maxTransactions):
  tc_bitmap(0),
  nDataNodes(conn->no_db_nodes()),
  concurrency(0),
  cacheConcurrency(0),
  maxConcurrency(maxTransactions)
{
  assert(nDataNodes > 0);
}

inline CachedTransactionsAccountant::~CachedTransactionsAccountant() {
}

inline void CachedTransactionsAccountant::tallySetNodeId(int nodeId) { 
  tc_bitmap ^= (1 << nodeId);
}

inline void CachedTransactionsAccountant::tallySetMaskedNodeIds(int64_t mask) {
  tc_bitmap ^= mask;
}

inline void CachedTransactionsAccountant::tallyClear() {
  tc_bitmap = 0;
}

int CachedTransactionsAccountant::tallyCountSetNodeIds() {
  // "Brian Kernighan's algorithm"; iterates once for each set bit
  uint64_t v = tc_bitmap;
  int c;
  for(c = 0 ; v ; c++) {
    v &= v-1;
  }
  return c;
}


/*  Returns a token that the user will supply to registerTxOpen().
    If token is -1, user is allowed to call immediate startTransaction() 
    knowing it will not block (because the needed transaction record is 
    already cached).  Otherwise transaction should be started in an async
    worker thread.
*/
int64_t CachedTransactionsAccountant::registerIntentToOpen() {
  concurrency++;
  assert(concurrency <= maxConcurrency);

  // Is it already established that we can handle this many transactions?
  if(concurrency < cacheConcurrency) {
    return -1;  
  }

  // Do we have enough cached transactions to establish that fact now?
  if(tallyCountSetNodeIds() == nDataNodes) {
    cacheConcurrency++;
    DEBUG_PRINT("Concurrency now: %d", cacheConcurrency);
    tallyClear();
    return -1;    
  }

  // Clear all tallies; return a token indicating which ones were cleared
  int64_t token = static_cast<int64_t>(tc_bitmap);
  tallyClear();
  return token;
}

void CachedTransactionsAccountant::registerTxClosed(int64_t token, int nodeId) {
  concurrency--;
  if(token >= 0) {
    tallySetMaskedNodeIds(token);
    tallySetNodeId(nodeId); 
  }
}




//////////
/////////////////
///////////////////////// DBSessionImpl
/////////////////
//////////

DBSessionImpl::DBSessionImpl(Ndb_cluster_connection *conn, 
                             AsyncNdbContext * asyncNdbContext,
                             const char *defaultDatabase,
                             int maxTransactions) :
  CachedTransactionsAccountant(conn, maxTransactions),
  maxNdbTransactions(maxTransactions),
  nContexts(0),
  asyncContext(asyncNdbContext),
  freeList(0)
{
  ndb = new Ndb(conn, defaultDatabase);
  ndb->init(maxTransactions * 2);
}


DBSessionImpl::~DBSessionImpl() {
  DEBUG_MARKER(UDEB_DEBUG);
  delete ndb;
}


DBTransactionContext * DBSessionImpl::seizeTransaction() {
  DBTransactionContext * ctx;
  DEBUG_PRINT("FreeList: %p, nContexts: %d, maxNdbTransactions: %d",
              freeList, nContexts, maxNdbTransactions);
  
  /* Is there a context on the freelist? */
  if(freeList) {
    ctx = freeList;
    freeList = ctx->next;
    return ctx;
  }

  /* Can we produce a new context? */
  if(nContexts < maxNdbTransactions) {
    ctx = new DBTransactionContext(this);
    nContexts++;
    return ctx;  
  }

  return 0;
}


bool DBSessionImpl::releaseTransaction(DBTransactionContext *ctx) {
  assert(ctx->parent == this);
  bool status = ctx->isClosed();
  DEBUG_PRINT("releaseTransaction status: %s", status ? "closed" : "open");
  if(status) {
    ctx->next = freeList;
    freeList = ctx;
  }
  return status;
}


void DBSessionImpl::freeTransactions() {
  while(freeList) {
    DBTransactionContext * ctx = freeList;
    freeList = ctx->next;
    delete ctx;
  }
}

const NdbError & DBSessionImpl::getNdbError() const {
  return ndb->getNdbError();
}

