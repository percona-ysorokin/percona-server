#ifndef SQL_EXECUTOR_INCLUDED
#define SQL_EXECUTOR_INCLUDED

/* Copyright (c) 2000, 2020, Oracle and/or its affiliates.

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
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/**
  @file sql/sql_executor.h
  Classes for query execution.
*/

#include <sys/types.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "my_alloc.h"
#include "my_compiler.h"
#include "my_inttypes.h"
#include "my_table_map.h"
#include "sql/row_iterator.h"
#include "sql/sql_lex.h"
#include "sql/sql_opt_exec_shared.h"  // QEP_shared_owner
#include "sql/table.h"
#include "sql/temp_table_param.h"  // Temp_table_param

class CacheInvalidatorIterator;
class Cached_item;
class Field;
class Field_longlong;
class Filesort;
class FollowTailIterator;
class Item;
class Item_sum;
class JOIN;
class JOIN_TAB;
class KEY;
class MultiRangeRowIterator;
class Opt_trace_object;
class QEP_TAB;
class QUICK_SELECT_I;
class THD;
class Window;
enum class Window_retrieve_cached_row_reason;
struct AccessPath;
struct CACHE_FIELD;
struct POSITION;
template <class T>
class List;
template <typename Element_type>
class Mem_root_array;

/*
  Array of pointers to tables whose rowids compose the temporary table
  record.
*/
struct SJ_TMP_TABLE_TAB {
  QEP_TAB *qep_tab;
  uint rowid_offset;
  ushort null_byte;
  uchar null_bit;
};

/*
  Temporary table used by semi-join DuplicateElimination strategy

  This consists of the temptable itself and data needed to put records
  into it. The table's DDL is as follows:

    CREATE TABLE tmptable (col VARCHAR(n) BINARY, PRIMARY KEY(col));

  where the primary key can be replaced with unique constraint if n exceeds
  the limit (as it is always done for query execution-time temptables).

  The record value is a concatenation of rowids of tables from the join we're
  executing. If a join table is on the inner side of the outer join, we
  assume that its rowid can be NULL and provide means to store this rowid in
  the tuple.
*/

class SJ_TMP_TABLE {
 public:
  SJ_TMP_TABLE() : hash_field(nullptr) {}
  SJ_TMP_TABLE_TAB *tabs;
  SJ_TMP_TABLE_TAB *tabs_end;

  /*
    is_confluent==true means this is a special case where the temptable record
    has zero length (and presence of a unique key means that the temptable can
    have either 0 or 1 records).
    In this case we don't create the physical temptable but instead record
    its state in SJ_TMP_TABLE::have_confluent_record.
  */
  bool is_confluent;

  /*
    When is_confluent==true: the contents of the table (whether it has the
    record or not).
  */
  bool have_confluent_row;

  /* table record parameters */
  uint null_bits;
  uint null_bytes;
  uint rowid_len;

  /* The temporary table itself (NULL means not created yet) */
  TABLE *tmp_table;

  /* Pointer to next table (next->start_idx > this->end_idx) */
  SJ_TMP_TABLE *next;
  /* Calc hash instead of too long key */
  Field_longlong *hash_field;
};

/**
 Executor structure for the materialized semi-join info, which contains
  - Description of expressions selected from subquery
  - The sj-materialization temporary table
*/
class Semijoin_mat_exec {
 public:
  Semijoin_mat_exec(TABLE_LIST *sj_nest, bool is_scan, uint table_count,
                    uint mat_table_index, uint inner_table_index)
      : sj_nest(sj_nest),
        is_scan(is_scan),
        table_count(table_count),
        mat_table_index(mat_table_index),
        inner_table_index(inner_table_index),
        table_param(),
        table(nullptr) {}
  ~Semijoin_mat_exec() {}
  TABLE_LIST *const sj_nest;     ///< Semi-join nest for this materialization
  const bool is_scan;            ///< true if executing a scan, false if lookup
  const uint table_count;        ///< Number of tables in the sj-nest
  const uint mat_table_index;    ///< Index in join_tab for materialized table
  const uint inner_table_index;  ///< Index in join_tab for first inner table
  Temp_table_param table_param;  ///< The temptable and its related info
  TABLE *table;                  ///< Reference to temporary table
};

void setup_tmptable_write_func(QEP_TAB *tab, Opt_trace_object *trace);

MY_ATTRIBUTE((warn_unused_result))
bool copy_fields(Temp_table_param *param, const THD *thd,
                 bool reverse_copy = false);

enum Copy_func_type {
  /**
    In non-windowing step, copies functions
  */
  CFT_ALL,
  /**
    In windowing step, copies framing window function, including
    all grouping aggregates, e.g. SUM, AVG and FIRST_VALUE, LAST_VALUE.
  */
  CFT_WF_FRAMING,
  /**
    In windowing step, copies non framing window function, e.g.
    ROW_NUMBER, RANK, DENSE_RANK, except those that are two_pass cf.
    copy_two_pass_window_functions which are treated separately.
   */
  CFT_WF_NON_FRAMING,
  /**
    In windowing step, copies window functions that need frame cardinality,
    that is we need to read all rows of a partition before we can compute the
    wf's value for the the first row in the partition.
  */
  CFT_WF_NEEDS_CARD,
  /**
    In windowing step, copies framing window functions that read only one row
    per frame.
  */
  CFT_WF_USES_ONLY_ONE_ROW,
  /**
    In first windowing step, copies non-window functions which do not rely on
    window functions, i.e. those that have Item::has_wf() == false.
  */
  CFT_HAS_NO_WF,
  /**
    In final windowing step, copies all non-wf functions. Must be called after
    all wfs have been evaluated, as non-wf functions may reference wf,
    e.g. 1+RANK.
  */
  CFT_HAS_WF,
  /**
    Copies all window functions.
  */
  CFT_WF,
};

bool copy_funcs(Temp_table_param *, const THD *thd,
                Copy_func_type type = CFT_ALL);

// Combines copy_fields() and copy_funcs().
bool copy_fields_and_funcs(Temp_table_param *param, const THD *thd,
                           Copy_func_type type = CFT_ALL);

/**
  Copy the lookup key into the table ref's key buffer.

  @param thd   pointer to the THD object
  @param table the table to read
  @param ref   information about the index lookup key

  @retval false ref key copied successfully
  @retval true  error dectected during copying of key
*/
bool construct_lookup_ref(THD *thd, TABLE *table, TABLE_REF *ref);

/** Help function when we get some an error from the table handler. */
int report_handler_error(TABLE *table, int error);

int safe_index_read(QEP_TAB *tab);

int join_read_const_table(JOIN_TAB *tab, POSITION *pos);

int do_sj_dups_weedout(THD *thd, SJ_TMP_TABLE *sjtbl);
int update_item_cache_if_changed(List<Cached_item> &list);

// Create list for using with tempory table
bool change_to_use_tmp_fields(mem_root_deque<Item *> *fields, THD *thd,
                              Ref_item_array ref_item_array,
                              mem_root_deque<Item *> *res_fields);
// Create list for using with tempory table
bool change_to_use_tmp_fields_except_sums(mem_root_deque<Item *> *fields,
                                          THD *thd, SELECT_LEX *select,
                                          Ref_item_array ref_item_array,
                                          mem_root_deque<Item *> *res_fields);
bool prepare_sum_aggregators(Item_sum **func_ptr, bool need_distinct);
bool setup_sum_funcs(THD *thd, Item_sum **func_ptr);
bool make_group_fields(JOIN *main_join, JOIN *curr_join);
bool check_unique_constraint(TABLE *table);
ulonglong unique_hash(const Field *field, ulonglong *hash);

class QEP_TAB : public QEP_shared_owner {
 public:
  QEP_TAB()
      : QEP_shared_owner(),
        table_ref(nullptr),
        flush_weedout_table(nullptr),
        check_weed_out_table(nullptr),
        firstmatch_return(NO_PLAN_IDX),
        loosescan_key_len(0),
        match_tab(NO_PLAN_IDX),
        rematerialize(false),
        not_used_in_distinct(false),
        cache_idx_cond(nullptr),
        having(nullptr),
        tmp_table_param(nullptr),
        filesort(nullptr),
        ref_item_slice(REF_SLICE_SAVED_BASE),
        m_condition_optim(nullptr),
        m_quick_optim(nullptr),
        m_keyread_optim(false),
        m_reversed_access(false),
        lateral_derived_tables_depend_on_me(0) {}

  /// Initializes the object from a JOIN_TAB
  void init(JOIN_TAB *jt);
  // Cleans up.
  void cleanup();

  // Getters and setters

  Item *condition_optim() const { return m_condition_optim; }
  QUICK_SELECT_I *quick_optim() const { return m_quick_optim; }
  void set_quick_optim() { m_quick_optim = quick(); }
  void set_condition_optim() { m_condition_optim = condition(); }
  bool keyread_optim() const { return m_keyread_optim; }
  void set_keyread_optim() {
    if (table()) m_keyread_optim = table()->key_read;
  }
  bool reversed_access() const { return m_reversed_access; }
  void set_reversed_access(bool arg) { m_reversed_access = arg; }

  void set_table(TABLE *t) {
    m_qs->set_table(t);
    if (t) t->reginfo.qep_tab = this;
  }

  /// @returns semijoin strategy for this table.
  uint get_sj_strategy() const;

  /// Return true if join_tab should perform a FirstMatch action
  bool do_firstmatch() const { return firstmatch_return != NO_PLAN_IDX; }

  /// Return true if join_tab should perform a LooseScan action
  bool do_loosescan() const { return loosescan_key_len; }

  /// Return true if join_tab starts a Duplicate Weedout action
  bool starts_weedout() const { return flush_weedout_table; }

  /// Return true if join_tab finishes a Duplicate Weedout action
  bool finishes_weedout() const { return check_weed_out_table; }

  /**
    A helper function that allocates appropriate join cache object and
    sets next_select function of previous tab.
  */
  void init_join_cache(JOIN_TAB *join_tab);

  /**
     @returns query block id for an inner table of materialized semi-join, and
              0 for all other tables.
     @note implementation is not efficient (loops over all tables) - use this
     function only in EXPLAIN.
  */
  uint sjm_query_block_id() const;

  /// @returns whether this is doing QS_DYNAMIC_RANGE
  bool dynamic_range() const {
    if (!position()) return false;  // tmp table
    return using_dynamic_range;
  }

  bool use_order() const;  ///< Use ordering provided by chosen index?

  /**
     Used to begin a new execution of a subquery. Necessary if this subquery
     has done a filesort which which has cleared condition/quick.
  */
  void restore_quick_optim_and_condition() {
    if (m_condition_optim) set_condition(m_condition_optim);
    if (m_quick_optim) set_quick(m_quick_optim);
  }

  /**
    Construct an access path for reading from this table in the query,
    using the access method that has been determined previously
    (e.g., table scan, ref access, optional sort afterwards, etc.).
   */
  AccessPath *access_path();
  void push_index_cond(const JOIN_TAB *join_tab, uint keyno,
                       Opt_trace_object *trace_obj);

  /// @return the index used for a table in a QEP
  uint effective_index() const;

  bool pfs_batch_update(const JOIN *join) const;

 public:
  /// Pointer to table reference
  TABLE_LIST *table_ref;

  /* Variables for semi-join duplicate elimination */
  SJ_TMP_TABLE *flush_weedout_table;
  SJ_TMP_TABLE *check_weed_out_table;

  /*
    If set, means we should stop join enumeration after we've got the first
    match and return to the specified join tab. May be PRE_FIRST_PLAN_IDX
    which means stopping join execution after the first match.
  */
  plan_idx firstmatch_return;

  /*
    Length of key tuple (depends on #keyparts used) to use for loose scan.
    If zero, means that loosescan is not used.
  */
  uint loosescan_key_len;

  /*
    If doing a LooseScan, this QEP is the first (i.e.  "driving")
    QEP_TAB, and match_tab points to the last QEP_TAB handled by the strategy.
    match_tab->found_match should be checked to see if the current value group
    had a match.
  */
  plan_idx match_tab;

  /// Dependent table functions have to be materialized on each new scan
  bool rematerialize;

  enum Setup_func {
    NO_SETUP,
    MATERIALIZE_TABLE_FUNCTION,
    MATERIALIZE_DERIVED,
    MATERIALIZE_SEMIJOIN
  };
  Setup_func materialize_table = NO_SETUP;
  bool using_dynamic_range = false;

  /** true <=> remove duplicates on this table. */
  bool needs_duplicate_removal = false;

  // If we have a query of the type SELECT DISTINCT t1.* FROM t1 JOIN t2
  // ON ..., (ie., we join in one or more tables that we don't actually
  // read any columns from), we can stop scanning t2 as soon as we see the
  // first row. This pattern seems to be a workaround for lack of semijoins
  // in older versions of MySQL.
  bool not_used_in_distinct;

  /// Index condition for BKA access join
  Item *cache_idx_cond;

  /** HAVING condition for checking prior saving a record into tmp table*/
  Item *having;

  // Operation between the previous QEP_TAB and this one.
  enum enum_op_type {
    // Regular nested loop.
    OT_NONE,

    // Aggregate (GROUP BY).
    OT_AGGREGATE,

    // Various temporary table operations, used at the end of the join.
    OT_MATERIALIZE,
    OT_AGGREGATE_THEN_MATERIALIZE,
    OT_AGGREGATE_INTO_TMP_TABLE,
    OT_WINDOWING_FUNCTION,

    // Block-nested loop (rewritten to hash join).
    OT_BNL,

    // Batch key access.
    OT_BKA
  } op_type = OT_NONE;

  /* Tmp table info */
  Temp_table_param *tmp_table_param;

  /* Sorting related info */
  Filesort *filesort;

  /**
    If we pushed a global ORDER BY down onto this first table, that ORDER BY
    list will be preseved here.
   */
  ORDER *filesort_pushed_order = nullptr;

  /**
    Slice number of the ref items array to switch to before reading rows from
    this table.
  */
  uint ref_item_slice;

  /// @see m_quick_optim
  Item *m_condition_optim;

  /**
     m_quick is the quick "to be used at this stage of execution".
     It can happen that filesort uses the quick (produced by the optimizer) to
     produce a sorted result, then the read of this result has to be done
     without "quick", so we must reset m_quick to NULL, but we want to delay
     freeing of m_quick or it would close the filesort's result and the table
     prematurely.
     In that case, we move m_quick to m_quick_optim (=> delay deletion), reset
     m_quick to NULL (read of filesort's result will be without quick); if
     this is a subquery which is later executed a second time,
     QEP_TAB::reset() will restore the quick from m_quick_optim into m_quick.
     quick_optim stands for "the quick decided by the optimizer".
     EXPLAIN reads this member and m_condition_optim; so if you change them
     after exposing the plan (setting plan_state), do it with the
     LOCK_query_plan mutex.
  */
  QUICK_SELECT_I *m_quick_optim;

  /**
     True if only index is going to be read for this table. This is the
     optimizer's decision.
  */
  bool m_keyread_optim;

  /**
    True if reversed scan is used. This is the optimizer's decision.
  */
  bool m_reversed_access;

  /**
     Maps of all lateral derived tables which should be refreshed when
     execution reads a new row from this table.
     @note that if a LDT depends on t1 and t2, and t2 is after t1 in the plan,
     then only t2::lateral_derived_tables_depend_on_me gets the map of the
     LDT, for efficiency (less useless calls to QEP_TAB::refresh_lateral())
     and clarity in EXPLAIN.
  */
  table_map lateral_derived_tables_depend_on_me;

  Mem_root_array<const AccessPath *> *invalidators = nullptr;

  QEP_TAB(const QEP_TAB &);             // not defined
  QEP_TAB &operator=(const QEP_TAB &);  // not defined
};

/**
   @returns a pointer to the QEP_TAB whose index is qtab->member. For
   example, QEP_AT(x,first_inner) is the first_inner table of x.
*/
#define QEP_AT(qtab, member) (qtab->join()->qep_tab[qtab->member])

/**
   Use this class when you need a QEP_TAB not connected to any JOIN_TAB.
*/
class QEP_TAB_standalone {
 public:
  QEP_TAB_standalone() { m_qt.set_qs(&m_qs); }
  ~QEP_TAB_standalone() { m_qt.cleanup(); }
  /// @returns access to the QEP_TAB
  QEP_TAB &as_QEP_TAB() { return m_qt; }

 private:
  QEP_shared m_qs;
  QEP_TAB m_qt;
};

bool set_record_buffer(TABLE *table, double expected_rows_to_fetch);
void init_tmptable_sum_functions(Item_sum **func_ptr);
void update_tmptable_sum_func(Item_sum **func_ptr, TABLE *tmp_table);
bool has_rollup_result(Item *item);
bool is_rollup_group_wrapper(Item *item);
bool is_rollup_sum_wrapper(Item *item);
Item *unwrap_rollup_group(Item *item);

/*
  If a condition cannot be applied right away, for instance because it is a
  WHERE condition and we're on the right side of an outer join, we have to
  return it up so that it can be applied on a higher recursion level.
  This structure represents such a condition.
 */
struct PendingCondition {
  Item *cond;
  int table_index_to_attach_to;  // -1 means “on the last possible outer join”.
};

unique_ptr_destroy_only<RowIterator> PossiblyAttachFilterIterator(
    unique_ptr_destroy_only<RowIterator> iterator,
    const std::vector<Item *> &conditions, THD *thd);

void SplitConditions(Item *condition, QEP_TAB *current_table,
                     std::vector<Item *> *predicates_below_join,
                     std::vector<PendingCondition> *predicates_above_join,
                     std::vector<PendingCondition> *join_conditions);

bool process_buffered_windowing_record(THD *thd, Temp_table_param *param,
                                       const bool new_partition_or_eof,
                                       bool *output_row_ready);
bool buffer_windowing_record(THD *thd, Temp_table_param *param,
                             bool *new_partition);
bool bring_back_frame_row(THD *thd, Window *w, Temp_table_param *out_param,
                          int64 rowno, Window_retrieve_cached_row_reason reason,
                          int fno = 0);

AccessPath *GetAccessPathForDerivedTable(THD *thd, QEP_TAB *qep_tab,
                                         AccessPath *table_path);
void ConvertItemsToCopy(const mem_root_deque<Item *> &items, Field **fields,
                        Temp_table_param *param);
std::string RefToString(const TABLE_REF &ref, const KEY *key,
                        bool include_nulls);

bool MaterializeIsDoingDeduplication(TABLE *table);

#endif /* SQL_EXECUTOR_INCLUDED */
