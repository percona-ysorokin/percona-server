/*
   Copyright (c) 2003, 2024, Oracle and/or its affiliates.

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
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

#ifndef SIGNAL_DATA_H
#define SIGNAL_DATA_H

#include <kernel/GlobalSignalNumbers.h>
#include <kernel/kernel_types.h>
#include <kernel/ndb_limits.h>
#include <ndb_global.h>

#define JAM_FILE_ID 61

#define ASSERT_BOOL(flag, message) assert(flag <= 1)
#define ASSERT_RANGE(value, min, max, message) \
  assert((value) >= (min) && (value) <= (max))
#define ASSERT_MAX(value, max, message) assert((value) <= (max))

#define SECTION(x) static constexpr Uint32 x

template <typename T>
inline T *cast_ptr(Uint32 *ptr) {
  NDB_ASSERT_POD(T);
  return new (ptr) T;
}

template <typename T>
inline const T *cast_constptr(const Uint32 *ptr) {
  NDB_ASSERT_POD(T);
  return const_cast<const T *>(new (const_cast<Uint32 *>(ptr)) T);
}

#define CAST_PTR(Y, X) cast_ptr<Y>(X)
#define CAST_CONSTPTR(Y, X) cast_constptr<Y>(X)

// defines for setter and getters on commonly used member data in signals

#define GET_SET_SENDERDATA                      \
  Uint32 getSenderData() { return senderData; } \
  void setSenderData(Uint32 _s) { senderData = _s; }

#define GET_SET_SENDERREF                     \
  Uint32 getSenderRef() { return senderRef; } \
  void setSenderRef(Uint32 _s) { senderRef = _s; }

#define GET_SET_PREPAREID                     \
  Uint32 getPrepareId() { return prepareId; } \
  void setPrepareId(Uint32 _s) { prepareId = _s; }

#define GET_SET_ERRORCODE                     \
  Uint32 getErrorCode() { return errorCode; } \
  void setErrorCode(Uint32 _s) { errorCode = _s; }

#define GET_SET_TCERRORCODE                       \
  Uint32 getTCErrorCode() { return TCErrorCode; } \
  void setTCErrorCode(Uint32 _s) { TCErrorCode = _s; }

#define GSN_PRINT_SIGNATURE(f) bool f(FILE *, const Uint32 *, Uint32, Uint16)

GSN_PRINT_SIGNATURE(printTCKEYREQ);
GSN_PRINT_SIGNATURE(printTCKEYCONF);
GSN_PRINT_SIGNATURE(printTCKEYREF);
GSN_PRINT_SIGNATURE(printLQHKEYREQ);
GSN_PRINT_SIGNATURE(printLQHKEYCONF);
GSN_PRINT_SIGNATURE(printLQHKEYREF);
GSN_PRINT_SIGNATURE(printTUPKEYREQ);
GSN_PRINT_SIGNATURE(printTUPKEYCONF);
GSN_PRINT_SIGNATURE(printTUPKEYREF);
GSN_PRINT_SIGNATURE(printTUPCOMMITREQ);
GSN_PRINT_SIGNATURE(printCONTINUEB);
GSN_PRINT_SIGNATURE(printFSOPENREQ);
GSN_PRINT_SIGNATURE(printFSCLOSEREQ);
GSN_PRINT_SIGNATURE(printFSREADWRITEREQ);
GSN_PRINT_SIGNATURE(printFSREADWRITEREQ);
GSN_PRINT_SIGNATURE(printFSREF);
GSN_PRINT_SIGNATURE(printFSREF);
GSN_PRINT_SIGNATURE(printFSREF);
GSN_PRINT_SIGNATURE(printFSREF);
GSN_PRINT_SIGNATURE(printFSREF);
GSN_PRINT_SIGNATURE(printFSCONF);
GSN_PRINT_SIGNATURE(printFSCONF);
GSN_PRINT_SIGNATURE(printFSCONF);
GSN_PRINT_SIGNATURE(printFSCONF);
GSN_PRINT_SIGNATURE(printFSCONF);
GSN_PRINT_SIGNATURE(printCLOSECOMREQCONF);
GSN_PRINT_SIGNATURE(printCLOSECOMREQCONF);
GSN_PRINT_SIGNATURE(printPACKED_SIGNAL);
GSN_PRINT_SIGNATURE(printPREPFAILREQREF);
GSN_PRINT_SIGNATURE(printPREPFAILREQREF);
GSN_PRINT_SIGNATURE(printALTER_TABLE_REQ);
GSN_PRINT_SIGNATURE(printALTER_TABLE_CONF);
GSN_PRINT_SIGNATURE(printALTER_TABLE_REF);
GSN_PRINT_SIGNATURE(printALTER_TAB_REQ);
GSN_PRINT_SIGNATURE(printALTER_TAB_CONF);
GSN_PRINT_SIGNATURE(printALTER_TAB_REF);
GSN_PRINT_SIGNATURE(printCREATE_TRIG_REQ);
GSN_PRINT_SIGNATURE(printCREATE_TRIG_CONF);
GSN_PRINT_SIGNATURE(printCREATE_TRIG_REF);
GSN_PRINT_SIGNATURE(printALTER_TRIG_REQ);
GSN_PRINT_SIGNATURE(printALTER_TRIG_CONF);
GSN_PRINT_SIGNATURE(printALTER_TRIG_REF);
GSN_PRINT_SIGNATURE(printDROP_TRIG_REQ);
GSN_PRINT_SIGNATURE(printDROP_TRIG_CONF);
GSN_PRINT_SIGNATURE(printDROP_TRIG_REF);
GSN_PRINT_SIGNATURE(printFIRE_TRIG_ORD);
GSN_PRINT_SIGNATURE(printTRIG_ATTRINFO);
GSN_PRINT_SIGNATURE(printCREATE_INDX_REQ);
GSN_PRINT_SIGNATURE(printCREATE_INDX_CONF);
GSN_PRINT_SIGNATURE(printCREATE_INDX_REF);
GSN_PRINT_SIGNATURE(printDROP_INDX_REQ);
GSN_PRINT_SIGNATURE(printDROP_INDX_CONF);
GSN_PRINT_SIGNATURE(printDROP_INDX_REF);
GSN_PRINT_SIGNATURE(printALTER_INDX_REQ);
GSN_PRINT_SIGNATURE(printALTER_INDX_CONF);
GSN_PRINT_SIGNATURE(printALTER_INDX_REF);
GSN_PRINT_SIGNATURE(printTCINDXREQ);
GSN_PRINT_SIGNATURE(printTCINDXCONF);
GSN_PRINT_SIGNATURE(printTCINDXREF);
GSN_PRINT_SIGNATURE(printINDXKEYINFO);
GSN_PRINT_SIGNATURE(printINDXATTRINFO);
GSN_PRINT_SIGNATURE(printFSAPPENDREQ);
GSN_PRINT_SIGNATURE(printBACKUP_REQ);
GSN_PRINT_SIGNATURE(printBACKUP_DATA);
GSN_PRINT_SIGNATURE(printBACKUP_REF);
GSN_PRINT_SIGNATURE(printBACKUP_CONF);
GSN_PRINT_SIGNATURE(printABORT_BACKUP_ORD);
GSN_PRINT_SIGNATURE(printBACKUP_ABORT_REP);
GSN_PRINT_SIGNATURE(printBACKUP_COMPLETE_REP);
GSN_PRINT_SIGNATURE(printBACKUP_NF_COMPLETE_REP);
GSN_PRINT_SIGNATURE(printDEFINE_BACKUP_REQ);
GSN_PRINT_SIGNATURE(printDEFINE_BACKUP_REF);
GSN_PRINT_SIGNATURE(printDEFINE_BACKUP_CONF);
GSN_PRINT_SIGNATURE(printSTART_BACKUP_REQ);
GSN_PRINT_SIGNATURE(printSTART_BACKUP_REF);
GSN_PRINT_SIGNATURE(printSTART_BACKUP_CONF);
GSN_PRINT_SIGNATURE(printBACKUP_FRAGMENT_REQ);
GSN_PRINT_SIGNATURE(printBACKUP_FRAGMENT_REF);
GSN_PRINT_SIGNATURE(printBACKUP_FRAGMENT_CONF);
GSN_PRINT_SIGNATURE(printSTOP_BACKUP_REQ);
GSN_PRINT_SIGNATURE(printSTOP_BACKUP_REF);
GSN_PRINT_SIGNATURE(printSTOP_BACKUP_CONF);
GSN_PRINT_SIGNATURE(printBACKUP_STATUS_REQ);
GSN_PRINT_SIGNATURE(printBACKUP_STATUS_CONF);
GSN_PRINT_SIGNATURE(printUTIL_SEQUENCE_REQ);
GSN_PRINT_SIGNATURE(printUTIL_SEQUENCE_REF);
GSN_PRINT_SIGNATURE(printUTIL_SEQUENCE_CONF);
GSN_PRINT_SIGNATURE(printUTIL_PREPARE_REQ);
GSN_PRINT_SIGNATURE(printUTIL_PREPARE_REF);
GSN_PRINT_SIGNATURE(printUTIL_PREPARE_CONF);
GSN_PRINT_SIGNATURE(printUTIL_EXECUTE_REQ);
GSN_PRINT_SIGNATURE(printUTIL_EXECUTE_REF);
GSN_PRINT_SIGNATURE(printUTIL_EXECUTE_CONF);
GSN_PRINT_SIGNATURE(printSCANTABREQ);
GSN_PRINT_SIGNATURE(printSCANTABCONF);
GSN_PRINT_SIGNATURE(printSCANTABREF);
GSN_PRINT_SIGNATURE(printSCANNEXTREQ);
GSN_PRINT_SIGNATURE(printSCANFRAGNEXTREQ);
GSN_PRINT_SIGNATURE(printLQH_FRAG_REQ);
GSN_PRINT_SIGNATURE(printLQH_FRAG_REF);
GSN_PRINT_SIGNATURE(printLQH_FRAG_CONF);
GSN_PRINT_SIGNATURE(printPREP_DROP_TAB_REQ);
GSN_PRINT_SIGNATURE(printPREP_DROP_TAB_REF);
GSN_PRINT_SIGNATURE(printPREP_DROP_TAB_CONF);
GSN_PRINT_SIGNATURE(printDROP_TAB_REQ);
GSN_PRINT_SIGNATURE(printDROP_TAB_REF);
GSN_PRINT_SIGNATURE(printDROP_TAB_CONF);
GSN_PRINT_SIGNATURE(printLCP_FRAG_ORD);
GSN_PRINT_SIGNATURE(printLCP_FRAG_REP);
GSN_PRINT_SIGNATURE(printLCP_COMPLETE_REP);
GSN_PRINT_SIGNATURE(printSTART_LCP_REQ);
GSN_PRINT_SIGNATURE(printSTART_LCP_CONF);
GSN_PRINT_SIGNATURE(printMASTER_LCP_REQ);
GSN_PRINT_SIGNATURE(printMASTER_LCP_REF);
GSN_PRINT_SIGNATURE(printMASTER_LCP_CONF);
GSN_PRINT_SIGNATURE(printCOPY_GCI_REQ);
GSN_PRINT_SIGNATURE(printSYSTEM_ERROR);
GSN_PRINT_SIGNATURE(printSTART_REC_REQ);
GSN_PRINT_SIGNATURE(printSTART_REC_CONF);
GSN_PRINT_SIGNATURE(printNF_COMPLETE_REP);
GSN_PRINT_SIGNATURE(printSIGNAL_DROPPED_REP);
GSN_PRINT_SIGNATURE(printFAIL_REP);
GSN_PRINT_SIGNATURE(printDISCONNECT_REP);
GSN_PRINT_SIGNATURE(printSUB_CREATE_REQ);
GSN_PRINT_SIGNATURE(printSUB_CREATE_CONF);
GSN_PRINT_SIGNATURE(printSUB_CREATE_REF);
GSN_PRINT_SIGNATURE(printSUB_REMOVE_REQ);
GSN_PRINT_SIGNATURE(printSUB_REMOVE_CONF);
GSN_PRINT_SIGNATURE(printSUB_REMOVE_REF);
GSN_PRINT_SIGNATURE(printSUB_START_REQ);
GSN_PRINT_SIGNATURE(printSUB_START_REF);
GSN_PRINT_SIGNATURE(printSUB_START_CONF);
GSN_PRINT_SIGNATURE(printSUB_STOP_REQ);
GSN_PRINT_SIGNATURE(printSUB_STOP_REF);
GSN_PRINT_SIGNATURE(printSUB_STOP_CONF);
GSN_PRINT_SIGNATURE(printSUB_SYNC_REQ);
GSN_PRINT_SIGNATURE(printSUB_SYNC_REF);
GSN_PRINT_SIGNATURE(printSUB_SYNC_CONF);
GSN_PRINT_SIGNATURE(printSUB_META_DATA);
GSN_PRINT_SIGNATURE(printSUB_TABLE_DATA);
GSN_PRINT_SIGNATURE(printSUB_SYNC_CONTINUE_REQ);
GSN_PRINT_SIGNATURE(printSUB_SYNC_CONTINUE_REF);
GSN_PRINT_SIGNATURE(printSUB_SYNC_CONTINUE_CONF);
GSN_PRINT_SIGNATURE(printSUB_GCP_COMPLETE_REP);
GSN_PRINT_SIGNATURE(printCREATE_FRAGMENTATION_REQ);
GSN_PRINT_SIGNATURE(printCREATE_FRAGMENTATION_REF);
GSN_PRINT_SIGNATURE(printCREATE_FRAGMENTATION_CONF);
GSN_PRINT_SIGNATURE(printUTIL_CREATE_LOCK_REQ);
GSN_PRINT_SIGNATURE(printUTIL_CREATE_LOCK_REF);
GSN_PRINT_SIGNATURE(printUTIL_CREATE_LOCK_CONF);
GSN_PRINT_SIGNATURE(printUTIL_DESTROY_LOCK_REQ);
GSN_PRINT_SIGNATURE(printUTIL_DESTROY_LOCK_REF);
GSN_PRINT_SIGNATURE(printUTIL_DESTROY_LOCK_CONF);
GSN_PRINT_SIGNATURE(printUTIL_LOCK_REQ);
GSN_PRINT_SIGNATURE(printUTIL_LOCK_REF);
GSN_PRINT_SIGNATURE(printUTIL_LOCK_CONF);
GSN_PRINT_SIGNATURE(printUTIL_UNLOCK_REQ);
GSN_PRINT_SIGNATURE(printUTIL_UNLOCK_REF);
GSN_PRINT_SIGNATURE(printUTIL_UNLOCK_CONF);
GSN_PRINT_SIGNATURE(printCNTR_START_REQ);
GSN_PRINT_SIGNATURE(printCNTR_START_REF);
GSN_PRINT_SIGNATURE(printCNTR_START_CONF);
GSN_PRINT_SIGNATURE(printREAD_NODES_CONF);
GSN_PRINT_SIGNATURE(printTUX_MAINT_REQ);
GSN_PRINT_SIGNATURE(printACC_LOCKREQ);
GSN_PRINT_SIGNATURE(printLQH_TRANSCONF);
GSN_PRINT_SIGNATURE(printSCAN_FRAGREQ);
GSN_PRINT_SIGNATURE(printSCAN_FRAGCONF);

GSN_PRINT_SIGNATURE(printCONTINUEB_NDBFS);
GSN_PRINT_SIGNATURE(printCONTINUEB_DBDIH);
GSN_PRINT_SIGNATURE(printSTART_FRAG_REQ);

GSN_PRINT_SIGNATURE(printSCHEMA_TRANS_BEGIN_REQ);
GSN_PRINT_SIGNATURE(printSCHEMA_TRANS_BEGIN_CONF);
GSN_PRINT_SIGNATURE(printSCHEMA_TRANS_BEGIN_REF);
GSN_PRINT_SIGNATURE(printSCHEMA_TRANS_END_REQ);
GSN_PRINT_SIGNATURE(printSCHEMA_TRANS_END_CONF);
GSN_PRINT_SIGNATURE(printSCHEMA_TRANS_END_REF);
GSN_PRINT_SIGNATURE(printSCHEMA_TRANS_END_REP);
GSN_PRINT_SIGNATURE(printSCHEMA_TRANS_IMPL_REQ);
GSN_PRINT_SIGNATURE(printSCHEMA_TRANS_IMPL_CONF);
GSN_PRINT_SIGNATURE(printSCHEMA_TRANS_IMPL_REF);
GSN_PRINT_SIGNATURE(printCREATE_TAB_REQ);
GSN_PRINT_SIGNATURE(printCREATE_TAB_CONF);
GSN_PRINT_SIGNATURE(printCREATE_TAB_REF);
GSN_PRINT_SIGNATURE(printCREATE_TABLE_REQ);
GSN_PRINT_SIGNATURE(printCREATE_TABLE_CONF);
GSN_PRINT_SIGNATURE(printCREATE_TABLE_REF);
GSN_PRINT_SIGNATURE(printDROP_TABLE_REQ);
GSN_PRINT_SIGNATURE(printDROP_TABLE_REF);
GSN_PRINT_SIGNATURE(printDROP_TABLE_CONF);

GSN_PRINT_SIGNATURE(printGET_TABINFO_REQ);
GSN_PRINT_SIGNATURE(printGET_TABINFO_REF);
GSN_PRINT_SIGNATURE(printGET_TABINFO_CONF);

GSN_PRINT_SIGNATURE(printCREATE_TRIG_IMPL_REQ);
GSN_PRINT_SIGNATURE(printCREATE_TRIG_IMPL_CONF);
GSN_PRINT_SIGNATURE(printCREATE_TRIG_IMPL_REF);
GSN_PRINT_SIGNATURE(printDROP_TRIG_IMPL_REQ);
GSN_PRINT_SIGNATURE(printDROP_TRIG_IMPL_CONF);
GSN_PRINT_SIGNATURE(printDROP_TRIG_IMPL_REF);
GSN_PRINT_SIGNATURE(printALTER_TRIG_IMPL_REQ);
GSN_PRINT_SIGNATURE(printALTER_TRIG_IMPL_CONF);
GSN_PRINT_SIGNATURE(printALTER_TRIG_IMPL_REF);

GSN_PRINT_SIGNATURE(printCREATE_INDX_IMPL_REQ);
GSN_PRINT_SIGNATURE(printCREATE_INDX_IMPL_CONF);
GSN_PRINT_SIGNATURE(printCREATE_INDX_IMPL_REF);
GSN_PRINT_SIGNATURE(printDROP_INDX_IMPL_REQ);
GSN_PRINT_SIGNATURE(printDROP_INDX_IMPL_CONF);
GSN_PRINT_SIGNATURE(printDROP_INDX_IMPL_REF);
GSN_PRINT_SIGNATURE(printALTER_INDX_IMPL_REQ);
GSN_PRINT_SIGNATURE(printALTER_INDX_IMPL_CONF);
GSN_PRINT_SIGNATURE(printALTER_INDX_IMPL_REF);

GSN_PRINT_SIGNATURE(printBUILD_INDX_REQ);
GSN_PRINT_SIGNATURE(printBUILD_INDX_CONF);
GSN_PRINT_SIGNATURE(printBUILD_INDX_REF);
GSN_PRINT_SIGNATURE(printBUILD_INDX_IMPL_REQ);
GSN_PRINT_SIGNATURE(printBUILD_INDX_IMPL_CONF);
GSN_PRINT_SIGNATURE(printBUILD_INDX_IMPL_REF);

GSN_PRINT_SIGNATURE(printAPI_VERSION_REQ);
GSN_PRINT_SIGNATURE(printAPI_VERSION_CONF);

GSN_PRINT_SIGNATURE(printLOCAL_ROUTE_ORD);

GSN_PRINT_SIGNATURE(printDBINFO_SCAN);
GSN_PRINT_SIGNATURE(printDBINFO_SCAN_REF);

GSN_PRINT_SIGNATURE(printNODE_PING_REQ);
GSN_PRINT_SIGNATURE(printNODE_PING_CONF);

GSN_PRINT_SIGNATURE(printINDEX_STAT_REQ);
GSN_PRINT_SIGNATURE(printINDEX_STAT_CONF);
GSN_PRINT_SIGNATURE(printINDEX_STAT_REF);
GSN_PRINT_SIGNATURE(printINDEX_STAT_IMPL_REQ);
GSN_PRINT_SIGNATURE(printINDEX_STAT_IMPL_CONF);
GSN_PRINT_SIGNATURE(printINDEX_STAT_IMPL_REF);
GSN_PRINT_SIGNATURE(printINDEX_STAT_REP);

GSN_PRINT_SIGNATURE(printGET_CONFIG_REQ);
GSN_PRINT_SIGNATURE(printGET_CONFIG_REF);
GSN_PRINT_SIGNATURE(printGET_CONFIG_CONF);

GSN_PRINT_SIGNATURE(printALLOC_NODEID_REQ);
GSN_PRINT_SIGNATURE(printALLOC_NODEID_CONF);
GSN_PRINT_SIGNATURE(printALLOC_NODEID_REF);

GSN_PRINT_SIGNATURE(printLCP_STATUS_REQ);
GSN_PRINT_SIGNATURE(printLCP_STATUS_CONF);
GSN_PRINT_SIGNATURE(printLCP_STATUS_REF);

GSN_PRINT_SIGNATURE(printLCP_PREPARE_REQ);
GSN_PRINT_SIGNATURE(printLCP_PREPARE_CONF);
GSN_PRINT_SIGNATURE(printLCP_PREPARE_REF);

GSN_PRINT_SIGNATURE(printSYNC_PAGE_CACHE_REQ);
GSN_PRINT_SIGNATURE(printSYNC_PAGE_CACHE_CONF);

GSN_PRINT_SIGNATURE(printEND_LCPREQ);
GSN_PRINT_SIGNATURE(printEND_LCPCONF);

GSN_PRINT_SIGNATURE(printRESTORE_LCP_REQ);
GSN_PRINT_SIGNATURE(printRESTORE_LCP_CONF);
GSN_PRINT_SIGNATURE(printRESTORE_LCP_REF);

GSN_PRINT_SIGNATURE(printCREATE_FK_REQ);
GSN_PRINT_SIGNATURE(printCREATE_FK_REF);
GSN_PRINT_SIGNATURE(printCREATE_FK_CONF);
GSN_PRINT_SIGNATURE(printDROP_FK_REQ);
GSN_PRINT_SIGNATURE(printDROP_FK_REF);
GSN_PRINT_SIGNATURE(printDROP_FK_CONF);

GSN_PRINT_SIGNATURE(printISOLATE_ORD);

GSN_PRINT_SIGNATURE(printPROCESSINFO_REP);
GSN_PRINT_SIGNATURE(printTRP_KEEP_ALIVE);
GSN_PRINT_SIGNATURE(printCREATE_EVNT_CONF);
GSN_PRINT_SIGNATURE(printCREATE_EVNT_REQ);
GSN_PRINT_SIGNATURE(printCREATE_EVNT_REF);

/**
   Signal scope monitoring

   Any signal can be received via any connected transporter.
   Signals are sent between all node types (API, MGMD, Data nodes).
   By adding checks to the data nodes about where signals were received from,
   we can improve the robustness and security of the system.
   The main goal is to ensure that only allowed cluster nodes can send certain
   signals. To achieve this we distinguish between remote and local signals and
   add checks when particular signals are received.

   The signals can be defined with the following signal sending scopes:

   Local:
   This signal should only be received from blocks on the same data node, this
   can be effectively checked. Any such signal received from another node will
   cause an error (normally controlled restart of the receiving node).

   Remote:
   This specifies a signal can be received from any data node. Any such signal
   received from an API/MGM node will cause an error (normally controlled
   restart of the receiving node).

   Management:
   This specifies a signal can only be received from an MGM node or a data node,
   but not an API node. Any such signal sent from an API node will cause an
   error (normally controlled restart of the receiving node).

   External:
   This specifies the signal can be received from any node. This has the same
   semantics as if the signal has no scope defined. It is primarily for
   documenting the signal.

   The signal scope is defined in conjunction with setting up signal handler
   functions for a block during node startup. This is done by the addRecSignal
   calls.

   The signal scope for individual signals are defined together with the signal
   classes. Signals without specific classes have their signal scope defined
   below.

   The format is as follows:

   DECLARE_SIGNAL_SCOPE(GlobalSignalNumber, SignalScope)

   For example, after definition of class FailRep

   DECLARE_SIGNAL_SCOPE(GSN_FAIL_REP, Remote);
*/
enum SignalScope { Local, Remote, Management, External };

template <GlobalSignalNumber GSN>
struct signal_property {
  static constexpr SignalScope scope =
      External;  // Default value if there is no GSN-specific specialisation is
                 // External
};

// Macro to define a template specialisation for a specific GSN
#define DECLARE_SIGNAL_SCOPE(gsn, theScope)        \
  template <>                                      \
  struct signal_property<gsn> {                    \
    static constexpr SignalScope scope = theScope; \
  }

/*
 Define all generic signal scopes for signals with no
 unique signal classes below. All other signal scopes are defined
 with the respective signal classes.
*/

DECLARE_SIGNAL_SCOPE(GSN_CONTINUEB, Local);
DECLARE_SIGNAL_SCOPE(GSN_FSSUSPENDORD, Local);

#undef JAM_FILE_ID

#endif
