/*
   Copyright (C) 2009 Sun Microsystems Inc
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
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef NdbQueryOperationImpl_H
#define NdbQueryOperationImpl_H

#include "NdbQueryOperation.hpp"
#include "NdbQueryBuilderImpl.hpp"
#include "NdbImpl.hpp"
#include "NdbError.hpp"
#include "NdbTransaction.hpp"
#include <ObjectMap.hpp>
#include <Vector.hpp>
#include <NdbOut.hpp>

class NdbQueryImpl {
private:
  // Only constructable from factory ::buildQuery();
  explicit NdbQueryImpl(
             NdbTransaction& trans,
             const NdbQueryDefImpl& queryDef,
             NdbQueryImpl* next);

  ~NdbQueryImpl();
public:
  STATIC_CONST (MAGIC = 0xdeadface);

  // Factory method which instantiate a query from its definition
  static NdbQueryImpl* buildQuery(NdbTransaction& trans, 
                                  const NdbQueryDefImpl& queryDef, 
                                  NdbQueryImpl* next);

  Uint32 getNoOfOperations() const;

  // Get a specific NdbQueryOperation by ident specified
  // when the NdbQueryOperationDef was created.
  NdbQueryOperationImpl& getQueryOperation(Uint32 ident) const;
  NdbQueryOperationImpl* getQueryOperation(const char* ident) const;

  Uint32 getNoOfParameters() const;
  const NdbParamOperand* getParameter(const char* name) const;
  const NdbParamOperand* getParameter(Uint32 num) const;

  int nextResult(bool fetchAllowed, bool forceSend);

  void close(bool forceSend, bool release);

  NdbTransaction* getNdbTransaction() const;

  const NdbError& getNdbError() const;

  void setErrorCode(int aErrorCode)
  { if (!m_error.code)
      m_error.code = aErrorCode;
  }
  void setErrorCodeAbort(int aErrorCode);

 /** Process TCKEYCONF message. Return true if query is complete.*/
  bool execTCKEYCONF();

  /** Count number of completed operations.
   * @param increment Change in count of  completed operations.
   * @return True if query is complete.*/
  bool incPendingOperations(int increment);

  /** Prepare for execution. 
   *  @return possible error code.*/
  int prepareSend();

  /** Release all NdbReceiver instances.*/
  void release();

  bool checkMagicNumber() const
  { return m_magic == MAGIC; }

  Uint32 ptr2int() const
  { return m_id; }
  
  const NdbQuery& getInterface() const
  { return m_interface; }

  NdbQuery& getInterface()
  { return m_interface; }
  
  /** Get next query in same transaction.*/
  NdbQueryImpl* getNext() const
  { return m_next; }

  /** TODO: Remove. Temporary hack for prototype.*/
  NdbOperation* getNdbOperation() const
  { return m_ndbOperation; }

private:
  NdbQuery m_interface;

  /** For verifying pointers to this class.*/
  const Uint32 m_magic;
  /** I-value for object maps.*/
  const Uint32 m_id;
  /** Possible error status of this query.*/
  NdbError m_error;
  /** Transaction in which this query instance executes.*/
  NdbTransaction& m_transaction;
  /** The operations constituting this query.*/
  Vector<NdbQueryOperationImpl*> m_operations;
  /** True if a TCKEYCONF message has been received for this query.*/
  bool m_tcKeyConfReceived;
  /** Number of operations not yet completed.*/
  int m_pendingOperations;
  /** Serialized representation of parameters. To be sent in TCKEYREQ*/
  Uint32Buffer m_serializedParams;
  /** Next query in same transaction.*/
  NdbQueryImpl* const m_next;
  /** TODO: Remove this.*/
  NdbOperation* m_ndbOperation;
  /** Definition of this query.*/
  const NdbQueryDefImpl& m_queryDef;
}; // class NdbQueryImpl


/** This class contains data members for NdbQueryOperation, such that these
 * do not need to exposed in NdbQueryOperation.hpp. This class may be 
 * changed without forcing the customer to recompile his application.*/
class NdbQueryOperationImpl {
  // friend class NdbQueryImpl;
  /** For debugging.*/
  friend NdbOut& operator<<(NdbOut& out, const NdbQueryOperationImpl&);
public:
  STATIC_CONST (MAGIC = 0xfade1234);

  explicit NdbQueryOperationImpl(NdbQueryImpl& queryImpl, 
                                 const NdbQueryOperationDefImpl& def);
  ~NdbQueryOperationImpl(){
    if (m_id != NdbObjectIdMap::InvalidId) {
      m_queryImpl.getNdbTransaction()->getNdb()->theImpl
       ->theNdbObjectIdMap.unmap(m_id, this);
    }
  }

  Uint32 getNoOfParentOperations() const;
  NdbQueryOperationImpl& getParentOperation(Uint32 i) const;

  Uint32 getNoOfChildOperations() const;
  NdbQueryOperationImpl& getChildOperation(Uint32 i) const;

  const NdbQueryOperationDefImpl& getQueryOperationDef() const;

  // Get the entire query object which this operation is part of
  NdbQueryImpl& getQuery() const;

  NdbRecAttr* getValue(const char* anAttrName, char* resultBuffer);
  NdbRecAttr* getValue(Uint32 anAttrId, char* resultBuffer);
  NdbRecAttr* getValue(const NdbDictionary::Column*, char* resultBuffer);

  int setResultRowBuf (const NdbRecord *rec,
                       char* resBuffer,
                       const unsigned char* result_mask);

  int setResultRowRef (const NdbRecord* rec,
                       char* & bufRef,
                       const unsigned char* result_mask);

  bool isRowNULL() const;    // Row associated with Operation is NULL value?

  bool isRowChanged() const; // Prev ::nextResult() on NdbQuery retrived a new
                             // value for this NdbQueryOperation

  /** Returns an I-value for the NdbReceiver object that shall receive results
   * for this operation. 
   * @return The I-value.
   */
  /*Uint32 getResultPtr() const {
    return m_receiver.getId();
    };*/


  /** Process result data for this operation. Return true if query complete.*/
  bool execTRANSID_AI(const Uint32* ptr, Uint32 len);
  
  /** Process absence of result data for this operation. 
   * Return true if query complete.*/
  bool execTCKEYREF(NdbApiSignal* aSignal);

  /** Serialize parameter values.
   *  @return possible error code.*/
  int serializeParams(const constVoidPtr paramValues[]);

  /** Prepare for execution. 
   *  @return possible error code.*/
  int prepareSend(Uint32Buffer& serializedParams);

  /** Release NdbReceiver objects.*/
  void release();

  /* TODO: Remove this method. Only needed in spj_test.cpp.*/
  /** Return I-value for putting in object map.*/
  Uint32 ptr2int() const {
    return m_id;
  }
  
  /** Verify magic number.*/
  bool checkMagicNumber() const { 
    return m_magic == MAGIC;
  }

  /** Check if operation is complete. 
   * For doing sanity check on internal state.*/
  bool isComplete() const { 
    return m_pendingResults==0;
  }

  const NdbQueryOperation& getInterface() const
  { return m_interface; }
  NdbQueryOperation& getInterface()
  { return m_interface; }

private:
  /** This class represents a projection that shall be sent to the 
   * application.*/
  class UserProjection{
  public:
    explicit UserProjection(const NdbDictionary::Table& tab);

    /** Add a column to the projection.
     * @return Possible error code.*/ 
    int addColumn(const NdbDictionary::Column& col);
    
    /** Make a serialize representation of this object, to be sent to the 
     * SPJ block.
     * @param dst Buffer for storing serialized projection.
     * @return Possible error code.*/
    int serialize(Uint32Slice dst) const;
    
  private:
    /** The columns that consitutes the projection.*/
    const NdbDictionary::Column* m_columns[MAX_ATTRIBUTES_IN_TABLE];
    /** The number of columns in the projection.*/
    int m_columnCount;
    /** The number of columns in the table that the operation refers to.*/
    const int m_noOfColsInTable;
    /** User Projection, represented as a bitmap (indexed with column numbers).*/
    Bitmask<MAXNROFATTRIBUTESINWORDS> m_mask;
    /** True if columns were added in ascending order (ordered according to 
     * column number).*/
    bool m_isOrdered;
    /** Highest column number seen so far.*/
    int m_maxColNo;
  }; // class UserProjection

  /** Interface for the application developer.*/
  NdbQueryOperation m_interface;
  /** For verifying pointers to this class.*/
  const Uint32 m_magic;
  /** I-value for object maps.*/
  const Uint32 m_id;
  /** NdbQuery to which this operation belongs. */
  NdbQueryImpl& m_queryImpl;
  /** The (transaction independent ) definition from which this instance
   * was created.*/
  const NdbQueryOperationDefImpl& m_operationDef;
  /* TODO: replace m_children and m_parents with navigation via 
   * m_operationDef.getParentOperation() etc.*/
  /** Parents of this operation.*/
  Vector<NdbQueryOperationImpl*> m_parents;
  /** Children of this operation.*/
  Vector<NdbQueryOperationImpl*> m_children;

  /** For processing results from this operation.*/
  NdbReceiver m_receiver;
  /** Number of pending TCKEYREF or TRANSID_AI messages for this operation.*/
  int m_pendingResults;
  /** Buffer for parameters in serialized format */
  Uint32Buffer m_params;
  /** Projection to be sent to the application.*/
  UserProjection m_userProjection;
  /** NdbRecord and old style result retrieval may not be combined.*/
  enum {
    Style_None,       // Not set yet.
    Style_NdbRecord,  // Use old style result retrieval.
    Style_NdbRecAttr, // Use NdbRecord.
  } m_resultStyle;
}; // class NdbQueryOperationImpl



#endif
