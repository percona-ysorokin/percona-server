/******************************************************
SQL parser symbol table

(c) 1997 Innobase Oy

Created 12/15/1997 Heikki Tuuri
*******************************************************/

#include "pars0sym.h"

#ifdef UNIV_NONINL
#include "pars0sym.ic"
#endif

#include "mem0mem.h"
#include "data0type.h"
#include "data0data.h"
#include "pars0grm.h"
#include "pars0pars.h"
#include "que0que.h"
#include "eval0eval.h"
#include "row0sel.h"

/**********************************************************************
Creates a symbol table for a single stored procedure or query. */

sym_tab_t*
sym_tab_create(
/*===========*/
				/* out, own: symbol table */
	mem_heap_t*	heap)	/* in: memory heap where to create */
{
	sym_tab_t*	sym_tab;

	sym_tab = mem_heap_alloc(heap, sizeof(sym_tab_t));

	UT_LIST_INIT(sym_tab->sym_list);
	UT_LIST_INIT(sym_tab->func_node_list);

	sym_tab->heap = heap;

	return(sym_tab);
}

/**********************************************************************
Frees the memory allocated dynamically AFTER parsing phase for variables
etc. in the symbol table. Does not free the mem heap where the table was
originally created. Frees also SQL explicit cursor definitions. */

void
sym_tab_free_private(
/*=================*/
	sym_tab_t*	sym_tab)	/* in, own: symbol table */
{
	sym_node_t*	sym;
	func_node_t*	func;

	sym = UT_LIST_GET_FIRST(sym_tab->sym_list);

	while (sym) {
		eval_node_free_val_buf(sym);

		if (sym->prefetch_buf) {
			sel_col_prefetch_buf_free(sym->prefetch_buf);
		}

		if (sym->cursor_def) {
			que_graph_free_recursive(sym->cursor_def);
		}

		sym = UT_LIST_GET_NEXT(sym_list, sym);
	}

	func = UT_LIST_GET_FIRST(sym_tab->func_node_list);

	while (func) {
		eval_node_free_val_buf(func);

		func = UT_LIST_GET_NEXT(func_node_list, func);
	}
}

/**********************************************************************
Adds an integer literal to a symbol table. */

sym_node_t*
sym_tab_add_int_lit(
/*================*/
					/* out: symbol table node */
	sym_tab_t*	sym_tab,	/* in: symbol table */
	ulint		val)		/* in: integer value */
{
	sym_node_t*	node;
	byte*		data;

	node = mem_heap_alloc(sym_tab->heap, sizeof(sym_node_t));

	node->common.type = QUE_NODE_SYMBOL;

	node->resolved = TRUE;
	node->token_type = SYM_LIT;

	node->indirection = NULL;

	dtype_set(&(node->common.val.type), DATA_INT, 0, 4, 0);

	data = mem_heap_alloc(sym_tab->heap, 4);
	mach_write_to_4(data, val);

	dfield_set_data(&(node->common.val), data, 4);

	node->common.val_buf_size = 0;
	node->prefetch_buf = NULL;
	node->cursor_def = NULL;

	UT_LIST_ADD_LAST(sym_list, sym_tab->sym_list, node);

	node->sym_table = sym_tab;

	return(node);
}

/**********************************************************************
Adds a string literal to a symbol table. */

sym_node_t*
sym_tab_add_str_lit(
/*================*/
					/* out: symbol table node */
	sym_tab_t*	sym_tab,	/* in: symbol table */
	byte*		str,		/* in: string with no quotes around
					it */
	ulint		len)		/* in: string length */
{
	sym_node_t*	node;
	byte*		data;

	node = mem_heap_alloc(sym_tab->heap, sizeof(sym_node_t));

	node->common.type = QUE_NODE_SYMBOL;

	node->resolved = TRUE;
	node->token_type = SYM_LIT;

	node->indirection = NULL;

	dtype_set(&(node->common.val.type), DATA_VARCHAR, DATA_ENGLISH, 0, 0);

	if (len) {
		data = mem_heap_alloc(sym_tab->heap, len);
		ut_memcpy(data, str, len);
	} else {
		data = NULL;
	}

	dfield_set_data(&(node->common.val), data, len);

	node->common.val_buf_size = 0;
	node->prefetch_buf = NULL;
	node->cursor_def = NULL;

	UT_LIST_ADD_LAST(sym_list, sym_tab->sym_list, node);

	node->sym_table = sym_tab;

	return(node);
}

/**********************************************************************
Add a bound literal to a symbol table. */

sym_node_t*
sym_tab_add_bound_lit(
/*==================*/
					/* out: symbol table node */
	sym_tab_t*	sym_tab,	/* in: symbol table */
	const char*	name,		/* in: name of bound literal */
	ulint*		lit_type)	/* out: type of literal (PARS_*_LIT) */
{
	sym_node_t*		node;
	pars_bound_lit_t*	blit;
	ulint			len	= 0;

	blit = pars_info_get_bound_lit(sym_tab->info, name);
	ut_a(blit);

	ut_a(blit->length > 0);

	node = mem_heap_alloc(sym_tab->heap, sizeof(sym_node_t));

	node->common.type = QUE_NODE_SYMBOL;

	node->resolved = TRUE;
	node->token_type = SYM_LIT;

	node->indirection = NULL;

	switch (blit->type) {
	case DATA_FIXBINARY:
		len = blit->length;
		*lit_type = PARS_FIXBINARY_LIT;
		break;

	case DATA_BLOB:
		*lit_type = PARS_BLOB_LIT;
		break;

	case DATA_VARCHAR:
		*lit_type = PARS_STR_LIT;
		break;

	case DATA_INT:
		ut_a(blit->length <= 8);
		len = blit->length;
		*lit_type = PARS_INT_LIT;
		break;

	default:
		ut_error;
	}

	dtype_set(&(node->common.val.type), blit->type, blit->prtype, len, 0);

	dfield_set_data(&(node->common.val), blit->address, blit->length);

	node->common.val_buf_size = 0;
	node->prefetch_buf = NULL;
	node->cursor_def = NULL;

	UT_LIST_ADD_LAST(sym_list, sym_tab->sym_list, node);

	node->sym_table = sym_tab;

	return(node);
}

/**********************************************************************
Adds an SQL null literal to a symbol table. */

sym_node_t*
sym_tab_add_null_lit(
/*=================*/
					/* out: symbol table node */
	sym_tab_t*	sym_tab)	/* in: symbol table */
{
	sym_node_t*	node;

	node = mem_heap_alloc(sym_tab->heap, sizeof(sym_node_t));

	node->common.type = QUE_NODE_SYMBOL;

	node->resolved = TRUE;
	node->token_type = SYM_LIT;

	node->indirection = NULL;

	node->common.val.type.mtype = DATA_ERROR;

	dfield_set_data(&(node->common.val), NULL, UNIV_SQL_NULL);

	node->common.val_buf_size = 0;
	node->prefetch_buf = NULL;
	node->cursor_def = NULL;

	UT_LIST_ADD_LAST(sym_list, sym_tab->sym_list, node);

	node->sym_table = sym_tab;

	return(node);
}

/**********************************************************************
Adds an identifier to a symbol table. */

sym_node_t*
sym_tab_add_id(
/*===========*/
					/* out: symbol table node */
	sym_tab_t*	sym_tab,	/* in: symbol table */
	byte*		name,		/* in: identifier name */
	ulint		len)		/* in: identifier length */
{
	sym_node_t*	node;

	node = mem_heap_alloc(sym_tab->heap, sizeof(sym_node_t));

	node->common.type = QUE_NODE_SYMBOL;

	node->resolved = FALSE;
	node->indirection = NULL;

	node->name = mem_heap_strdupl(sym_tab->heap, (char*) name, len);
	node->name_len = len;

	UT_LIST_ADD_LAST(sym_list, sym_tab->sym_list, node);

	dfield_set_data(&(node->common.val), NULL, UNIV_SQL_NULL);

	node->common.val_buf_size = 0;
	node->prefetch_buf = NULL;
	node->cursor_def = NULL;

	node->sym_table = sym_tab;

	return(node);
}
