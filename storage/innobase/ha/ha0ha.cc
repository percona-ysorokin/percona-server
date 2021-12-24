/*****************************************************************************

Copyright (c) 1994, 2021, Oracle and/or its affiliates.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License, version 2.0, as published by the
Free Software Foundation.

This program is also distributed with certain software (including but not
limited to OpenSSL) that is licensed under separate terms, as designated in a
particular file or component or in included license documentation. The authors
of MySQL hereby grant you an additional permission to link the program and
your derivative works with the separately licensed software that they have
included with MySQL.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License, version 2.0,
for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA

*****************************************************************************/

/** @file ha/ha0ha.cc
 The hash table with external chains

 Created 8/22/1994 Heikki Tuuri
 *************************************************************************/

#include "ha0ha.h"

#include <sys/types.h>

#ifdef UNIV_DEBUG
#include "buf0buf.h"
#endif /* UNIV_DEBUG */
#include "btr0sea.h"
#include "page0page.h"

#if defined UNIV_AHI_DEBUG || defined UNIV_DEBUG
/** Maximum number of records in a page */
static const ulint MAX_N_POINTERS = UNIV_PAGE_SIZE_MAX / REC_N_NEW_EXTRA_BYTES;
#endif /* UNIV_AHI_DEBUG || UNIV_DEBUG */

hash_table_t *ib_create(ulint n, latch_id_t id, ulint n_sync_obj, ulint type) {
  hash_table_t *table;

  ut_a(type == MEM_HEAP_FOR_BTR_SEARCH || type == MEM_HEAP_FOR_PAGE_HASH);

  ut_ad(ut_is_2pow(n_sync_obj));
  table = hash_create(n);
  ut_ad(table->heap == nullptr);

  /* Creating MEM_HEAP_BTR_SEARCH type heaps can potentially fail,
  but in practise it never should in this case, hence the asserts. */

  if (n_sync_obj == 0) {
    table->heap = mem_heap_create_typed(
        std::min(uint64_t{4096}, MEM_MAX_ALLOC_IN_BUF / 2 -
                                     MEM_BLOCK_HEADER_SIZE -
                                     MEM_SPACE_NEEDED(0)),
        UT_LOCATION_HERE, type);
    ut_a(table->heap);

    return table;
  }
  ut_ad(type == MEM_HEAP_FOR_PAGE_HASH);
  /* We create a hash table protected by rw_locks for buf_pool->page_hash. */
  hash_create_sync_obj(table, id, n_sync_obj);
  return table;
}

/** Empties a hash table and frees the memory heaps. */
void ha_clear(hash_table_t *table) /*!< in, own: hash table */
{
  ut_ad(table->magic_n == HASH_TABLE_MAGIC_N);
  ut_ad(!table->adaptive || btr_search_own_all(RW_LOCK_X));
  ut_ad(table->type == HASH_TABLE_SYNC_RW_LOCK);
  ut_ad(table->heap == nullptr);

  for (ulint i = 0; i < table->n_sync_obj; ++i) {
    rw_lock_free(&table->rw_locks[i]);
  }

  ut::free(table->rw_locks);
  table->rw_locks = nullptr;

  table->n_sync_obj = 0;
  table->type = HASH_TABLE_SYNC_NONE;

  /* Clear the hash table. */
  ulint n = hash_get_n_cells(table);

  for (ulint i = 0; i < n; i++) {
    hash_get_nth_cell(table, i)->node = nullptr;
  }
}

bool ha_insert_for_fold_func(hash_table_t *table, ulint fold,
                             IF_AHI_DEBUG(buf_block_t *block, )
                                 const rec_t *data) {
  hash_cell_t *cell;
  ha_node_t *node;
  ha_node_t *prev_node;
  ulint hash;

  ut_ad(data);
  ut_ad(table);
  ut_ad(table->magic_n == HASH_TABLE_MAGIC_N);
#if defined UNIV_AHI_DEBUG || defined UNIV_DEBUG
  ut_a(block->frame == page_align(data));
#endif /* UNIV_AHI_DEBUG || UNIV_DEBUG */
  hash_assert_can_modify(table, fold);
  ut_ad(btr_search_enabled);

  hash = hash_calc_hash(fold, table);

  cell = hash_get_nth_cell(table, hash);

  prev_node = static_cast<ha_node_t *>(cell->node);

  while (prev_node != nullptr) {
    if (prev_node->fold == fold) {
#if defined UNIV_AHI_DEBUG || defined UNIV_DEBUG
      if (table->adaptive) {
        buf_block_t *prev_block = prev_node->block;
        ut_a(prev_block->frame == page_align(prev_node->data));
        ut_a(prev_block->n_pointers.fetch_sub(1) - 1 < MAX_N_POINTERS);
        ut_a(block->n_pointers.fetch_add(1) + 1 < MAX_N_POINTERS);
      }

      prev_node->block = block;
#endif /* UNIV_AHI_DEBUG || UNIV_DEBUG */
      prev_node->data = data;

      return true;
    }

    prev_node = prev_node->next;
  }

  /* We have to allocate a new chain node */

  node = static_cast<ha_node_t *>(
      mem_heap_alloc(hash_get_heap(table), sizeof(ha_node_t)));

  if (node == nullptr) {
    /* It was a btr search type memory heap and at the moment
    no more memory could be allocated: return */

    ut_ad(hash_get_heap(table)->type & MEM_HEAP_BTR_SEARCH);

    return false;
  }

  ha_node_set_data(node, IF_AHI_DEBUG(block, ) data);

#if defined UNIV_AHI_DEBUG || defined UNIV_DEBUG
  if (table->adaptive) {
    ut_a(block->n_pointers.fetch_add(1) + 1 < MAX_N_POINTERS);
  }
#endif /* UNIV_AHI_DEBUG || UNIV_DEBUG */

  node->fold = fold;

  node->next = nullptr;

  prev_node = static_cast<ha_node_t *>(cell->node);

  if (prev_node == nullptr) {
    cell->node = node;

    return true;
  }

  while (prev_node->next != nullptr) {
    prev_node = prev_node->next;
  }

  prev_node->next = node;

  return true;
}

#ifdef UNIV_DEBUG
/** Verify if latch corresponding to the hash table is x-latched
@param[in]	table		hash table */
static void ha_btr_search_latch_x_locked(const hash_table_t *table) {
  ulint i;
  for (i = 0; i < btr_ahi_parts; ++i) {
    if (btr_search_sys->hash_tables[i] == table) {
      break;
    }
  }

  ut_ad(i < btr_ahi_parts);
  ut_ad(rw_lock_own(btr_search_latches[i], RW_LOCK_X));
}
#endif /* UNIV_DEBUG */

/** Deletes a hash node. */
void ha_delete_hash_node(hash_table_t *table, /*!< in: hash table */
                         ha_node_t *del_node) /*!< in: node to be deleted */
{
  ut_ad(table);
  ut_ad(table->magic_n == HASH_TABLE_MAGIC_N);
  ut_d(ha_btr_search_latch_x_locked(table));
  ut_ad(btr_search_enabled);
#if defined UNIV_AHI_DEBUG || defined UNIV_DEBUG
  if (table->adaptive) {
    ut_a(del_node->block->frame = page_align(del_node->data));
    ut_a(del_node->block->n_pointers.fetch_sub(1) - 1 < MAX_N_POINTERS);
  }
#endif /* UNIV_AHI_DEBUG || UNIV_DEBUG */

  HASH_DELETE_AND_COMPACT(ha_node_t, next, table, del_node);
}

bool ha_search_and_update_if_found_func(hash_table_t *table, ulint fold,
                                        const rec_t *data,
                                        IF_AHI_DEBUG(buf_block_t *new_block, )
                                            const rec_t *new_data) {
  ha_node_t *node;

  ut_ad(table);
  ut_ad(table->magic_n == HASH_TABLE_MAGIC_N);
  hash_assert_can_modify(table, fold);
#if defined UNIV_AHI_DEBUG || defined UNIV_DEBUG
  ut_a(new_block->frame == page_align(new_data));
#endif /* UNIV_AHI_DEBUG || UNIV_DEBUG */

  ut_d(ha_btr_search_latch_x_locked(table));

  if (!btr_search_enabled) {
    return false;
  }

  node = ha_search_with_data(table, fold, data);

  if (node) {
#if defined UNIV_AHI_DEBUG || defined UNIV_DEBUG
    if (table->adaptive) {
      ut_a(node->block->n_pointers.fetch_sub(1) - 1 < MAX_N_POINTERS);
      ut_a(new_block->n_pointers.fetch_add(1) + 1 < MAX_N_POINTERS);
    }

    node->block = new_block;
#endif /* UNIV_AHI_DEBUG || UNIV_DEBUG */
    node->data = new_data;

    return true;
  }

  return false;
}

/** Removes from the chain determined by fold all nodes whose data pointer
 points to the page given.
@param[in] table Hash table
@param[in] fold Fold value
@param[in] page Buffer page */
void ha_remove_all_nodes_to_page(hash_table_t *table, ulint fold,
                                 const page_t *page) {
  ha_node_t *node;

  ut_ad(table);
  ut_ad(table->magic_n == HASH_TABLE_MAGIC_N);
  hash_assert_can_modify(table, fold);
  ut_ad(btr_search_enabled);

  node = ha_chain_get_first(table, fold);

  while (node) {
    if (page_align(ha_node_get_data(node)) == page) {
      /* Remove the hash node */

      ha_delete_hash_node(table, node);

      /* Start again from the first node in the chain
      because the deletion may compact the heap of
      nodes and move other nodes! */

      node = ha_chain_get_first(table, fold);
    } else {
      node = ha_chain_get_next(node);
    }
  }
#ifdef UNIV_DEBUG
  /* Check that all nodes really got deleted */

  node = ha_chain_get_first(table, fold);

  while (node) {
    ut_a(page_align(ha_node_get_data(node)) != page);

    node = ha_chain_get_next(node);
  }
#endif /* UNIV_DEBUG */
}

#if defined UNIV_AHI_DEBUG || defined UNIV_DEBUG
/** Validates a given range of the cells in hash table.
 @return true if ok */
bool ha_validate(hash_table_t *table, /*!< in: hash table */
                 ulint start_index,   /*!< in: start index */
                 ulint end_index)     /*!< in: end index */
{
  bool ok = true;
  ulint i;

  ut_ad(table);
  ut_ad(table->magic_n == HASH_TABLE_MAGIC_N);
  ut_a(start_index <= end_index);
  ut_a(start_index < hash_get_n_cells(table));
  ut_a(end_index < hash_get_n_cells(table));

  for (i = start_index; i <= end_index; i++) {
    ha_node_t *node;
    hash_cell_t *cell;

    cell = hash_get_nth_cell(table, i);

    for (node = static_cast<ha_node_t *>(cell->node); node != nullptr;
         node = node->next) {
      if (hash_calc_hash(node->fold, table) != i) {
        ib::error(ER_IB_MSG_522) << "Hash table node fold value " << node->fold
                                 << " does not match the"
                                    " cell number "
                                 << i << ".";

        ok = false;
      }
    }
  }

  return (ok);
}
#endif /* defined UNIV_AHI_DEBUG || defined UNIV_DEBUG */

/** Prints info of a hash table.
@param[in] file File where to print
@param[in] table Hash table */
void ha_print_info(FILE *file, hash_table_t *table) {
#ifdef UNIV_DEBUG
/* Some of the code here is disabled for performance reasons in production
builds, see http://bugs.mysql.com/36941 */
#define PRINT_USED_CELLS
#endif /* UNIV_DEBUG */

#ifdef PRINT_USED_CELLS
  hash_cell_t *cell;
  ulint cells = 0;
  ulint i;
#endif /* PRINT_USED_CELLS */
  ulint n_bufs;

  ut_ad(table);
  ut_ad(table->magic_n == HASH_TABLE_MAGIC_N);
#ifdef PRINT_USED_CELLS
  for (i = 0; i < hash_get_n_cells(table); i++) {
    cell = hash_get_nth_cell(table, i);

    if (cell->node) {
      cells++;
    }
  }
#endif /* PRINT_USED_CELLS */

  fprintf(file, "Hash table size %lu", (ulong)hash_get_n_cells(table));

#ifdef PRINT_USED_CELLS
  fprintf(file, ", used cells %lu", (ulong)cells);
#endif /* PRINT_USED_CELLS */

  if (table->heap != nullptr) {
    /* This calculation is intended for the adaptive hash
    index: how many buffer frames we have reserved? */

    n_bufs = UT_LIST_GET_LEN(table->heap->base) - 1;

    if (table->heap->free_block) {
      n_bufs++;
    }

    fprintf(file, ", node heap has %lu buffer(s)\n", (ulong)n_bufs);
  }
}
