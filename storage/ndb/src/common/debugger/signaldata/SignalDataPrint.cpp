/*
   Copyright (c) 2003, 2021, Oracle and/or its affiliates.

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
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/



#include <GlobalSignalNumbers.h>
#include <signaldata/SignalData.hpp>
#include <signaldata/SignalDataPrint.hpp>

/** 
 * This is the register
 */

const NameFunctionPair
SignalDataPrintFunctions[] = {
  { GSN_TCKEYREQ, printTCKEYREQ },
  { GSN_TCINDXREQ, printTCKEYREQ },
  { GSN_TCKEYCONF, printTCKEYCONF },
  { GSN_TCKEYREF, printTCKEYREF },
  { GSN_LQHKEYREQ, printLQHKEYREQ },
  { GSN_LQHKEYCONF, printLQHKEYCONF },
  { GSN_LQHKEYREF, printLQHKEYREF },
  { GSN_TUPKEYREQ, printTUPKEYREQ },
  { GSN_TUPKEYCONF, printTUPKEYCONF },
  { GSN_TUPKEYREF, printTUPKEYREF },
  { GSN_TUP_COMMITREQ, printTUPCOMMITREQ },
  { GSN_CONTINUEB, printCONTINUEB },
  { GSN_FSOPENREQ, printFSOPENREQ },
  { GSN_FSCLOSEREQ, printFSCLOSEREQ },
  { GSN_FSREADREQ, printFSREADWRITEREQ },
  { GSN_FSWRITEREQ, printFSREADWRITEREQ },
  { GSN_FSCLOSEREF, printFSREF },
  { GSN_FSOPENREF, printFSREF },
  { GSN_FSWRITEREF, printFSREF },
  { GSN_FSREADREF, printFSREF },
  { GSN_FSSYNCREF, printFSREF },
  { GSN_FSCLOSECONF, printFSCONF },
  { GSN_FSOPENCONF, printFSCONF },
  { GSN_FSWRITECONF, printFSCONF },
  { GSN_FSREADCONF, printFSCONF },
  { GSN_FSSYNCCONF, printFSCONF },
  { GSN_CLOSE_COMREQ, printCLOSECOMREQCONF },
  { GSN_CLOSE_COMCONF, printCLOSECOMREQCONF },
  { GSN_PACKED_SIGNAL, printPACKED_SIGNAL },
  { GSN_PREP_FAILREQ, printPREPFAILREQREF },
  { GSN_PREP_FAILREF, printPREPFAILREQREF },
  { GSN_ALTER_TABLE_REQ, printALTER_TABLE_REQ },
  { GSN_ALTER_TABLE_CONF, printALTER_TABLE_CONF },
  { GSN_ALTER_TABLE_REF, printALTER_TABLE_REF },
  { GSN_ALTER_TAB_REQ, printALTER_TAB_REQ },
  { GSN_ALTER_TAB_CONF, printALTER_TAB_CONF },
  { GSN_ALTER_TAB_REF, printALTER_TAB_REF },
  { GSN_CREATE_TRIG_REQ, printCREATE_TRIG_REQ },
  { GSN_CREATE_TRIG_CONF, printCREATE_TRIG_CONF },
  { GSN_CREATE_TRIG_REF, printCREATE_TRIG_REF },
  { GSN_DROP_TRIG_REQ, printDROP_TRIG_REQ },
  { GSN_DROP_TRIG_CONF, printDROP_TRIG_CONF },
  { GSN_DROP_TRIG_REF, printDROP_TRIG_REF },
  { GSN_FIRE_TRIG_ORD, printFIRE_TRIG_ORD },
  { GSN_TRIG_ATTRINFO, printTRIG_ATTRINFO },
  { GSN_CREATE_INDX_REQ, printCREATE_INDX_REQ },
  { GSN_CREATE_INDX_CONF, printCREATE_INDX_CONF },
  { GSN_CREATE_INDX_REF, printCREATE_INDX_REF },
  { GSN_DROP_INDX_REQ, printDROP_INDX_REQ },
  { GSN_DROP_INDX_CONF, printDROP_INDX_CONF },
  { GSN_DROP_INDX_REF, printDROP_INDX_REF },
  { GSN_ALTER_INDX_REQ, printALTER_INDX_REQ },
  { GSN_ALTER_INDX_CONF, printALTER_INDX_CONF },
  { GSN_ALTER_INDX_REF, printALTER_INDX_REF },
  { GSN_TCINDXCONF, printTCKEYCONF },
  { GSN_TCINDXREF, printTCINDXREF },
  { GSN_INDXKEYINFO, printINDXKEYINFO },
  { GSN_INDXATTRINFO, printINDXATTRINFO },
  { GSN_FSAPPENDREQ, printFSAPPENDREQ },
  { GSN_BACKUP_REQ,             printBACKUP_REQ },
  { GSN_BACKUP_DATA,            printBACKUP_DATA },
  { GSN_BACKUP_REF,             printBACKUP_REF },
  { GSN_BACKUP_CONF,            printBACKUP_CONF },
  { GSN_ABORT_BACKUP_ORD,       printABORT_BACKUP_ORD },
  { GSN_BACKUP_ABORT_REP,       printBACKUP_ABORT_REP },
  { GSN_BACKUP_COMPLETE_REP,    printBACKUP_COMPLETE_REP },
  { GSN_BACKUP_NF_COMPLETE_REP, printBACKUP_NF_COMPLETE_REP },
  { GSN_DEFINE_BACKUP_REQ,      printDEFINE_BACKUP_REQ },
  { GSN_DEFINE_BACKUP_REF,      printDEFINE_BACKUP_REF },
  { GSN_DEFINE_BACKUP_CONF,     printDEFINE_BACKUP_CONF },
  { GSN_START_BACKUP_REQ,       printSTART_BACKUP_REQ },
  { GSN_START_BACKUP_REF,       printSTART_BACKUP_REF },
  { GSN_START_BACKUP_CONF,      printSTART_BACKUP_CONF },
  { GSN_BACKUP_FRAGMENT_REQ,    printBACKUP_FRAGMENT_REQ },
  { GSN_BACKUP_FRAGMENT_REF,    printBACKUP_FRAGMENT_REF },
  { GSN_BACKUP_FRAGMENT_CONF,   printBACKUP_FRAGMENT_CONF },
  { GSN_STOP_BACKUP_REQ,        printSTOP_BACKUP_REQ },
  { GSN_STOP_BACKUP_REF,        printSTOP_BACKUP_REF },
  { GSN_STOP_BACKUP_CONF,       printSTOP_BACKUP_CONF },
  { GSN_BACKUP_STATUS_REQ,      printBACKUP_STATUS_REQ },
  //{ GSN_BACKUP_STATUS_REF,      printBACKUP_STATUS_REF },
  { GSN_BACKUP_STATUS_CONF,     printBACKUP_STATUS_CONF },
  { GSN_UTIL_SEQUENCE_REQ,      printUTIL_SEQUENCE_REQ },
  { GSN_UTIL_SEQUENCE_REF,      printUTIL_SEQUENCE_REF },
  { GSN_UTIL_SEQUENCE_CONF,     printUTIL_SEQUENCE_CONF },
  { GSN_UTIL_PREPARE_REQ,      printUTIL_PREPARE_REQ },
  { GSN_UTIL_PREPARE_REF,      printUTIL_PREPARE_REF },
  { GSN_UTIL_PREPARE_CONF,     printUTIL_PREPARE_CONF },
  { GSN_UTIL_EXECUTE_REQ,      printUTIL_EXECUTE_REQ },
  { GSN_UTIL_EXECUTE_REF,      printUTIL_EXECUTE_REF },
  { GSN_UTIL_EXECUTE_CONF,     printUTIL_EXECUTE_CONF },
  { GSN_SCAN_TABREQ,            printSCANTABREQ },
  { GSN_SCAN_TABCONF,           printSCANTABCONF },
  { GSN_SCAN_TABREF,            printSCANTABREF },
  { GSN_SCAN_NEXTREQ,           printSCANNEXTREQ }, 
  { GSN_LQHFRAGREQ,             printLQH_FRAG_REQ },
  { GSN_LQHFRAGREF,             printLQH_FRAG_REF },
  { GSN_LQHFRAGCONF,            printLQH_FRAG_CONF },
  { GSN_PREP_DROP_TAB_REQ,      printPREP_DROP_TAB_REQ },
  { GSN_PREP_DROP_TAB_REF,      printPREP_DROP_TAB_REF },
  { GSN_PREP_DROP_TAB_CONF,     printPREP_DROP_TAB_CONF },
  { GSN_DROP_TAB_REQ,           printDROP_TAB_REQ },
  { GSN_DROP_TAB_REF,           printDROP_TAB_REF },
  { GSN_DROP_TAB_CONF,          printDROP_TAB_CONF },
  { GSN_LCP_FRAG_ORD,           printLCP_FRAG_ORD },
  { GSN_LCP_FRAG_REP,           printLCP_FRAG_REP },
  { GSN_LCP_COMPLETE_REP,       printLCP_COMPLETE_REP },
  { GSN_START_LCP_REQ,          printSTART_LCP_REQ },
  { GSN_START_LCP_CONF,         printSTART_LCP_CONF },
  { GSN_MASTER_LCPREQ,          printMASTER_LCP_REQ },
  { GSN_MASTER_LCPREF,          printMASTER_LCP_REF },
  { GSN_MASTER_LCPCONF,         printMASTER_LCP_CONF },
  { GSN_COPY_GCIREQ,            printCOPY_GCI_REQ },
  { GSN_SYSTEM_ERROR,           printSYSTEM_ERROR },
  { GSN_START_RECREQ,           printSTART_REC_REQ },
  { GSN_START_RECCONF,          printSTART_REC_CONF },
  { GSN_START_FRAGREQ,          printSTART_FRAG_REQ },
  { GSN_NF_COMPLETEREP,         printNF_COMPLETE_REP },
  { GSN_SIGNAL_DROPPED_REP,     printSIGNAL_DROPPED_REP },
  { GSN_FAIL_REP,               printFAIL_REP },
  { GSN_DISCONNECT_REP,         printDISCONNECT_REP },
  
  { GSN_SUB_CREATE_REQ,         printSUB_CREATE_REQ },
  { GSN_SUB_CREATE_REF,         printSUB_CREATE_REF },
  { GSN_SUB_CREATE_CONF,        printSUB_CREATE_CONF },
  { GSN_SUB_REMOVE_REQ,         printSUB_REMOVE_REQ },
  { GSN_SUB_REMOVE_REF,         printSUB_REMOVE_REF },
  { GSN_SUB_REMOVE_CONF,        printSUB_REMOVE_CONF },
  { GSN_SUB_START_REQ,          printSUB_START_REQ },
  { GSN_SUB_START_REF,          printSUB_START_REF },
  { GSN_SUB_START_CONF,         printSUB_START_CONF },
  { GSN_SUB_STOP_REQ,           printSUB_STOP_REQ },
  { GSN_SUB_STOP_REF,           printSUB_STOP_REF },
  { GSN_SUB_STOP_CONF,          printSUB_STOP_CONF },
  { GSN_SUB_SYNC_REQ,           printSUB_SYNC_REQ },
  { GSN_SUB_SYNC_REF,           printSUB_SYNC_REF },
  { GSN_SUB_SYNC_CONF,          printSUB_SYNC_CONF },
  { GSN_SUB_TABLE_DATA,         printSUB_TABLE_DATA },
  { GSN_SUB_SYNC_CONTINUE_REQ,  printSUB_SYNC_CONTINUE_REQ },
  { GSN_SUB_SYNC_CONTINUE_REF,  printSUB_SYNC_CONTINUE_REF },
  { GSN_SUB_SYNC_CONTINUE_CONF, printSUB_SYNC_CONTINUE_CONF },
  { GSN_SUB_GCP_COMPLETE_REP,   printSUB_GCP_COMPLETE_REP }
  
  ,{ GSN_CREATE_FRAGMENTATION_REQ, printCREATE_FRAGMENTATION_REQ }
  ,{ GSN_CREATE_FRAGMENTATION_REF, printCREATE_FRAGMENTATION_REF }
  ,{ GSN_CREATE_FRAGMENTATION_CONF, printCREATE_FRAGMENTATION_CONF }

  ,{ GSN_UTIL_CREATE_LOCK_REQ,   printUTIL_CREATE_LOCK_REQ }
  ,{ GSN_UTIL_CREATE_LOCK_REF,   printUTIL_CREATE_LOCK_REF }
  ,{ GSN_UTIL_CREATE_LOCK_CONF,  printUTIL_CREATE_LOCK_CONF }
  ,{ GSN_UTIL_DESTROY_LOCK_REQ,  printUTIL_DESTROY_LOCK_REQ }
  ,{ GSN_UTIL_DESTROY_LOCK_REF,  printUTIL_DESTROY_LOCK_REF }
  ,{ GSN_UTIL_DESTROY_LOCK_CONF, printUTIL_DESTROY_LOCK_CONF }
  ,{ GSN_UTIL_LOCK_REQ,          printUTIL_LOCK_REQ }
  ,{ GSN_UTIL_LOCK_REF,          printUTIL_LOCK_REF }
  ,{ GSN_UTIL_LOCK_CONF,         printUTIL_LOCK_CONF }
  ,{ GSN_UTIL_UNLOCK_REQ,        printUTIL_UNLOCK_REQ }
  ,{ GSN_UTIL_UNLOCK_REF,        printUTIL_UNLOCK_REF }
  ,{ GSN_UTIL_UNLOCK_CONF,       printUTIL_UNLOCK_CONF }
  ,{ GSN_CNTR_START_REQ,         printCNTR_START_REQ }
  ,{ GSN_CNTR_START_REF,         printCNTR_START_REF }
  ,{ GSN_CNTR_START_CONF,        printCNTR_START_CONF }

  ,{ GSN_READ_NODESCONF,         printREAD_NODES_CONF }

  ,{ GSN_TUX_MAINT_REQ, printTUX_MAINT_REQ }
  ,{ GSN_ACC_LOCKREQ, printACC_LOCKREQ }
  ,{ GSN_LQH_TRANSCONF, printLQH_TRANSCONF }
  ,{ GSN_SCAN_FRAGREQ, printSCAN_FRAGREQ }
  ,{ GSN_SCAN_FRAGCONF, printSCAN_FRAGCONF }
  ,{ GSN_START_FRAGREQ, printSTART_FRAG_REQ }

  ,{ GSN_SCHEMA_TRANS_BEGIN_REQ, printSCHEMA_TRANS_BEGIN_REQ }
  ,{ GSN_SCHEMA_TRANS_BEGIN_CONF, printSCHEMA_TRANS_BEGIN_CONF }
  ,{ GSN_SCHEMA_TRANS_BEGIN_REF, printSCHEMA_TRANS_BEGIN_REF }
  ,{ GSN_SCHEMA_TRANS_END_REQ, printSCHEMA_TRANS_END_REQ }
  ,{ GSN_SCHEMA_TRANS_END_CONF, printSCHEMA_TRANS_END_CONF }
  ,{ GSN_SCHEMA_TRANS_END_REF, printSCHEMA_TRANS_END_REF }
  ,{ GSN_SCHEMA_TRANS_END_REP, printSCHEMA_TRANS_END_REP }
  ,{ GSN_SCHEMA_TRANS_IMPL_REQ, printSCHEMA_TRANS_IMPL_REQ }
  ,{ GSN_SCHEMA_TRANS_IMPL_CONF, printSCHEMA_TRANS_IMPL_CONF }
  ,{ GSN_SCHEMA_TRANS_IMPL_REF, printSCHEMA_TRANS_IMPL_REF }

  ,{ GSN_GET_TABINFOREQ, printGET_TABINFO_REQ }
  ,{ GSN_GET_TABINFOREF, printGET_TABINFO_REF }
  ,{ GSN_GET_TABINFO_CONF, printGET_TABINFO_CONF }

  ,{ GSN_CREATE_TABLE_REQ, printCREATE_TABLE_REQ }
  ,{ GSN_CREATE_TABLE_CONF, printCREATE_TABLE_CONF }
  ,{ GSN_CREATE_TABLE_REF, printCREATE_TABLE_REF }
  ,{ GSN_CREATE_TAB_REQ, printCREATE_TAB_REQ }
  ,{ GSN_CREATE_TAB_REF, printCREATE_TAB_REF }
  ,{ GSN_CREATE_TAB_CONF, printCREATE_TAB_CONF }
  ,{ GSN_DROP_TABLE_REQ,           printDROP_TABLE_REQ }
  ,{ GSN_DROP_TABLE_REF,           printDROP_TABLE_REF }
  ,{ GSN_DROP_TABLE_CONF,          printDROP_TABLE_CONF }

  ,{ GSN_CREATE_TRIG_IMPL_REQ, printCREATE_TRIG_IMPL_REQ }
  ,{ GSN_CREATE_TRIG_IMPL_CONF, printCREATE_TRIG_IMPL_CONF }
  ,{ GSN_CREATE_TRIG_IMPL_REF, printCREATE_TRIG_IMPL_REF }
  ,{ GSN_DROP_TRIG_IMPL_REQ, printDROP_TRIG_IMPL_REQ }
  ,{ GSN_DROP_TRIG_IMPL_CONF, printDROP_TRIG_IMPL_CONF }
  ,{ GSN_DROP_TRIG_IMPL_REF, printDROP_TRIG_IMPL_REF }

  ,{ GSN_CREATE_INDX_IMPL_REQ, printCREATE_INDX_IMPL_REQ }
  ,{ GSN_CREATE_INDX_IMPL_CONF, printCREATE_INDX_IMPL_CONF }
  ,{ GSN_CREATE_INDX_IMPL_REF, printCREATE_INDX_IMPL_REF }
  ,{ GSN_DROP_INDX_IMPL_REQ, printDROP_INDX_IMPL_REQ }
  ,{ GSN_DROP_INDX_IMPL_CONF, printDROP_INDX_IMPL_CONF }
  ,{ GSN_DROP_INDX_IMPL_REF, printDROP_INDX_IMPL_REF }
  ,{ GSN_ALTER_INDX_IMPL_REQ, printALTER_INDX_IMPL_REQ }
  ,{ GSN_ALTER_INDX_IMPL_CONF, printALTER_INDX_IMPL_CONF }
  ,{ GSN_ALTER_INDX_IMPL_REF, printALTER_INDX_IMPL_REF }

  ,{ GSN_BUILDINDXREQ, printBUILD_INDX_REQ }
  ,{ GSN_BUILDINDXCONF, printBUILD_INDX_CONF }
  ,{ GSN_BUILDINDXREF, printBUILD_INDX_REF }
  ,{ GSN_BUILD_INDX_IMPL_REQ, printBUILD_INDX_IMPL_REQ }
  ,{ GSN_BUILD_INDX_IMPL_CONF, printBUILD_INDX_IMPL_CONF }
  ,{ GSN_BUILD_INDX_IMPL_REF, printBUILD_INDX_IMPL_REF }

  ,{ GSN_API_VERSION_REQ, printAPI_VERSION_REQ }
  ,{ GSN_API_VERSION_CONF, printAPI_VERSION_CONF }

  ,{ GSN_LOCAL_ROUTE_ORD, printLOCAL_ROUTE_ORD }

  ,{ GSN_DBINFO_SCANREQ, printDBINFO_SCAN }
  ,{ GSN_DBINFO_SCANCONF, printDBINFO_SCAN }
  ,{ GSN_DBINFO_SCANREF, printDBINFO_SCAN_REF }

  ,{ GSN_NODE_PING_REQ, printNODE_PING_REQ }
  ,{ GSN_NODE_PING_CONF, printNODE_PING_CONF }

  ,{ GSN_INDEX_STAT_REQ, printINDEX_STAT_REQ }
  ,{ GSN_INDEX_STAT_CONF, printINDEX_STAT_CONF }
  ,{ GSN_INDEX_STAT_REF, printINDEX_STAT_REF }
  ,{ GSN_INDEX_STAT_IMPL_REQ, printINDEX_STAT_IMPL_REQ }
  ,{ GSN_INDEX_STAT_IMPL_CONF, printINDEX_STAT_IMPL_CONF }
  ,{ GSN_INDEX_STAT_IMPL_REF, printINDEX_STAT_IMPL_REF }
  ,{ GSN_INDEX_STAT_REP, printINDEX_STAT_REP }

  ,{ GSN_GET_CONFIG_REQ, printGET_CONFIG_REQ }
  ,{ GSN_GET_CONFIG_REF, printGET_CONFIG_REF }
  ,{ GSN_GET_CONFIG_CONF, printGET_CONFIG_CONF }

  ,{ GSN_ALLOC_NODEID_REQ, printALLOC_NODEID_REQ }
  ,{ GSN_ALLOC_NODEID_CONF, printALLOC_NODEID_CONF }
  ,{ GSN_ALLOC_NODEID_REF, printALLOC_NODEID_REF }

  ,{ GSN_LCP_STATUS_REQ, printLCP_STATUS_REQ }
  ,{ GSN_LCP_STATUS_CONF, printLCP_STATUS_CONF }
  ,{ GSN_LCP_STATUS_REF, printLCP_STATUS_REF }

  ,{ GSN_ISOLATE_ORD, printISOLATE_ORD }

  ,{ GSN_CREATE_FK_REQ, printCREATE_FK_REQ }
  ,{ GSN_CREATE_FK_REF, printCREATE_FK_REF }
  ,{ GSN_CREATE_FK_CONF, printCREATE_FK_CONF }
  ,{ GSN_DROP_FK_REQ, printDROP_FK_REQ }
  ,{ GSN_DROP_FK_REF, printDROP_FK_REF }
  ,{ GSN_DROP_FK_CONF, printDROP_FK_CONF }

  ,{ 0, 0 }
};

