/*****************************************************************************

Copyright (c) 1994, 2013, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2012, Facebook Inc.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA

*****************************************************************************/

/********************************************************************//**
@file page/page0cur.cc
The page cursor

Created 10/4/1994 Heikki Tuuri
*************************************************************************/

#include "page0cur.h"
#ifdef UNIV_NONINL
#include "page0cur.ic"
#endif

#include "page0zip.h"
#include "btr0btr.h"
#include "btr0sea.h"
#include "mtr0log.h"
#include "lock0lock.h"
#include "log0recv.h"
#include "ut0ut.h"
#ifndef UNIV_HOTBACKUP
#include "rem0cmp.h"

#ifdef PAGE_CUR_ADAPT
# ifdef UNIV_SEARCH_PERF_STAT
static ulint	page_cur_short_succ	= 0;
# endif /* UNIV_SEARCH_PERF_STAT */

/*******************************************************************//**
This is a linear congruential generator PRNG. Returns a pseudo random
number between 0 and 2^64-1 inclusive. The formula and the constants
being used are:
X[n+1] = (a * X[n] + c) mod m
where:
X[0] = ut_time_us(NULL)
a = 1103515245 (3^5 * 5 * 7 * 129749)
c = 12345 (3 * 5 * 823)
m = 18446744073709551616 (2^64)

@return	number between 0 and 2^64-1 */
static
ib_uint64_t
page_cur_lcg_prng(void)
/*===================*/
{
#define LCG_a	1103515245
#define LCG_c	12345
	static ib_uint64_t	lcg_current = 0;
	static ibool		initialized = FALSE;

	if (!initialized) {
		lcg_current = (ib_uint64_t) ut_time_us(NULL);
		initialized = TRUE;
	}

	/* no need to "% 2^64" explicitly because lcg_current is
	64 bit and this will be done anyway */
	lcg_current = LCG_a * lcg_current + LCG_c;

	return(lcg_current);
}

/****************************************************************//**
Tries a search shortcut based on the last insert.
@return	TRUE on success */
UNIV_INLINE
ibool
page_cur_try_search_shortcut(
/*=========================*/
	const buf_block_t*	block,	/*!< in: index page */
	const dict_index_t*	index,	/*!< in: record descriptor */
	const dtuple_t*		tuple,	/*!< in: data tuple */
	ulint*			iup_matched_fields,
					/*!< in/out: already matched
					fields in upper limit record */
	ulint*			iup_matched_bytes,
					/*!< in/out: already matched
					bytes in a field not yet
					completely matched */
	ulint*			ilow_matched_fields,
					/*!< in/out: already matched
					fields in lower limit record */
	ulint*			ilow_matched_bytes,
					/*!< in/out: already matched
					bytes in a field not yet
					completely matched */
	page_cur_t*		cursor) /*!< out: page cursor */
{
	const rec_t*	rec;
	const rec_t*	next_rec;
	ulint		low_match;
	ulint		low_bytes;
	ulint		up_match;
	ulint		up_bytes;
	ibool		success		= FALSE;
	const page_t*	page		= buf_block_get_frame(block);
	mem_heap_t*	heap		= NULL;
	ulint		offsets_[REC_OFFS_NORMAL_SIZE];
	ulint*		offsets		= offsets_;
	rec_offs_init(offsets_);

	ut_ad(dtuple_check_typed(tuple));

	rec = page_header_get_ptr(page, PAGE_LAST_INSERT);
	offsets = rec_get_offsets(rec, index, offsets,
				  dtuple_get_n_fields(tuple), &heap);

	ut_ad(rec);
	ut_ad(page_rec_is_user_rec(rec));

	ut_pair_min(&low_match, &low_bytes,
		    *ilow_matched_fields, *ilow_matched_bytes,
		    *iup_matched_fields, *iup_matched_bytes);

	up_match = low_match;
	up_bytes = low_bytes;

	if (page_cmp_dtuple_rec_with_match(tuple, rec, offsets,
					   &low_match, &low_bytes) < 0) {
		goto exit_func;
	}

	next_rec = page_rec_get_next_const(rec);
	offsets = rec_get_offsets(next_rec, index, offsets,
				  dtuple_get_n_fields(tuple), &heap);

	if (page_cmp_dtuple_rec_with_match(tuple, next_rec, offsets,
					   &up_match, &up_bytes) >= 0) {
		goto exit_func;
	}

	page_cur_position(rec, block, cursor);

	if (!page_rec_is_supremum(next_rec)) {

		*iup_matched_fields = up_match;
		*iup_matched_bytes = up_bytes;
	}

	*ilow_matched_fields = low_match;
	*ilow_matched_bytes = low_bytes;

#ifdef UNIV_SEARCH_PERF_STAT
	page_cur_short_succ++;
#endif
	success = TRUE;
exit_func:
	if (UNIV_LIKELY_NULL(heap)) {
		mem_heap_free(heap);
	}
	return(success);
}

#endif

/****************************************************************//**
Searches the right position for a page cursor. */
UNIV_INTERN
void
page_cur_search_with_match(
/*=======================*/
	const buf_block_t*	block,	/*!< in: buffer block */
	const dict_index_t*	index,	/*!< in: record descriptor */
	const dtuple_t*		tuple,	/*!< in: data tuple */
	ulint			mode,	/*!< in: PAGE_CUR_L,
					PAGE_CUR_LE, PAGE_CUR_G, or
					PAGE_CUR_GE */
	ulint*			iup_matched_fields,
					/*!< in/out: already matched
					fields in upper limit record */
	ulint*			iup_matched_bytes,
					/*!< in/out: already matched
					bytes in a field not yet
					completely matched */
	ulint*			ilow_matched_fields,
					/*!< in/out: already matched
					fields in lower limit record */
	ulint*			ilow_matched_bytes,
					/*!< in/out: already matched
					bytes in a field not yet
					completely matched */
	page_cur_t*		cursor)	/*!< out: page cursor */
{
	ulint		up;
	ulint		low;
	ulint		mid;
	const page_t*	page;
	const page_dir_slot_t* slot;
	const rec_t*	up_rec;
	const rec_t*	low_rec;
	const rec_t*	mid_rec;
	ulint		up_matched_fields;
	ulint		up_matched_bytes;
	ulint		low_matched_fields;
	ulint		low_matched_bytes;
	ulint		cur_matched_fields;
	ulint		cur_matched_bytes;
	int		cmp;
	mem_heap_t*	heap		= NULL;
	ulint		offsets_[REC_OFFS_NORMAL_SIZE];
	ulint*		offsets		= offsets_;
	rec_offs_init(offsets_);

	ut_ad(block && tuple && iup_matched_fields && iup_matched_bytes
	      && ilow_matched_fields && ilow_matched_bytes && cursor);
	ut_ad(dtuple_validate(tuple));
#ifdef UNIV_DEBUG
	switch (mode) {
	case PAGE_CUR_L:
	case PAGE_CUR_LE:
	case PAGE_CUR_G:
	case PAGE_CUR_GE:
		goto mode_ok;
	}
	ut_ad(0);
mode_ok:
#endif /* UNIV_DEBUG */
	page = buf_block_get_frame(block);
	page_zip_validate_if_zip(buf_block_get_page_zip(block), page, index);

	page_check_dir(page);

#ifdef PAGE_CUR_ADAPT
	if (page_is_leaf(page)
	    && (mode == PAGE_CUR_LE)
	    && (page_header_get_field(page, PAGE_N_DIRECTION) > 3)
	    && (page_header_get_ptr(page, PAGE_LAST_INSERT))
	    && (page_header_get_field(page, PAGE_DIRECTION) == PAGE_RIGHT)) {

		if (page_cur_try_search_shortcut(
			    block, index, tuple,
			    iup_matched_fields, iup_matched_bytes,
			    ilow_matched_fields, ilow_matched_bytes,
			    cursor)) {
			return;
		}
	}
#endif

	/* If mode PAGE_CUR_G is specified, we are trying to position the
	cursor to answer a query of the form "tuple < X", where tuple is
	the input parameter, and X denotes an arbitrary physical record on
	the page. We want to position the cursor on the first X which
	satisfies the condition. */

	up_matched_fields  = *iup_matched_fields;
	up_matched_bytes   = *iup_matched_bytes;
	low_matched_fields = *ilow_matched_fields;
	low_matched_bytes  = *ilow_matched_bytes;

	/* Perform binary search. First the search is done through the page
	directory, after that as a linear search in the list of records
	owned by the upper limit directory slot. */

	low = 0;
	up = page_dir_get_n_slots(page) - 1;

	/* Perform binary search until the lower and upper limit directory
	slots come to the distance 1 of each other */

	while (up - low > 1) {
		mid = (low + up) / 2;
		slot = page_dir_get_nth_slot(page, mid);
		mid_rec = page_dir_slot_get_rec(slot);

		ut_pair_min(&cur_matched_fields, &cur_matched_bytes,
			    low_matched_fields, low_matched_bytes,
			    up_matched_fields, up_matched_bytes);

		offsets = rec_get_offsets(mid_rec, index, offsets,
					  dtuple_get_n_fields_cmp(tuple),
					  &heap);

		cmp = cmp_dtuple_rec_with_match(tuple, mid_rec, offsets,
						&cur_matched_fields,
						&cur_matched_bytes);
		if (UNIV_LIKELY(cmp > 0)) {
low_slot_match:
			low = mid;
			low_matched_fields = cur_matched_fields;
			low_matched_bytes = cur_matched_bytes;

		} else if (UNIV_EXPECT(cmp, -1)) {
up_slot_match:
			up = mid;
			up_matched_fields = cur_matched_fields;
			up_matched_bytes = cur_matched_bytes;

		} else if (mode == PAGE_CUR_G || mode == PAGE_CUR_LE) {

			goto low_slot_match;
		} else {

			goto up_slot_match;
		}
	}

	slot = page_dir_get_nth_slot(page, low);
	low_rec = page_dir_slot_get_rec(slot);
	slot = page_dir_get_nth_slot(page, up);
	up_rec = page_dir_slot_get_rec(slot);

	/* Perform linear search until the upper and lower records come to
	distance 1 of each other. */

	while (page_rec_get_next_const(low_rec) != up_rec) {

		mid_rec = page_rec_get_next_const(low_rec);

		ut_pair_min(&cur_matched_fields, &cur_matched_bytes,
			    low_matched_fields, low_matched_bytes,
			    up_matched_fields, up_matched_bytes);

		offsets = rec_get_offsets(mid_rec, index, offsets,
					  dtuple_get_n_fields_cmp(tuple),
					  &heap);

		cmp = cmp_dtuple_rec_with_match(tuple, mid_rec, offsets,
						&cur_matched_fields,
						&cur_matched_bytes);
		if (UNIV_LIKELY(cmp > 0)) {
low_rec_match:
			low_rec = mid_rec;
			low_matched_fields = cur_matched_fields;
			low_matched_bytes = cur_matched_bytes;

		} else if (UNIV_EXPECT(cmp, -1)) {
up_rec_match:
			up_rec = mid_rec;
			up_matched_fields = cur_matched_fields;
			up_matched_bytes = cur_matched_bytes;
		} else if (mode == PAGE_CUR_G || mode == PAGE_CUR_LE) {

			goto low_rec_match;
		} else {

			goto up_rec_match;
		}
	}

	if (mode <= PAGE_CUR_GE) {
		page_cur_position(up_rec, block, cursor);
	} else {
		page_cur_position(low_rec, block, cursor);
	}

	*iup_matched_fields  = up_matched_fields;
	*iup_matched_bytes   = up_matched_bytes;
	*ilow_matched_fields = low_matched_fields;
	*ilow_matched_bytes  = low_matched_bytes;
	if (UNIV_LIKELY_NULL(heap)) {
		mem_heap_free(heap);
	}
}

/***********************************************************//**
Positions a page cursor on a randomly chosen user record on a page. If there
are no user records, sets the cursor on the infimum record. */
UNIV_INTERN
void
page_cur_open_on_rnd_user_rec(
/*==========================*/
	buf_block_t*	block,	/*!< in: page */
	page_cur_t*	cursor)	/*!< out: page cursor */
{
	const page_t*	page = buf_block_get_frame(block);

	if (page_is_empty(page)) {
		page_cur_set_before_first(block, cursor);
		return;
	}

	ulint 	rnd = (ulint) (page_cur_lcg_prng() % page_get_n_recs(page));
	page_cur_position(page_rec_get_nth_const(page, 1 + rnd),
			  block, cursor);
}

/***********************************************************//**
Writes the log record of a record insert on a page. */
static
void
page_cur_insert_rec_write_log(
/*==========================*/
	const rec_t*		insert_rec,	/*!< in: inserted record */
	ulint			rec_size,	/*!< in: insert_rec size */
	const rec_t*		cursor_rec,	/*!< in: record the
						cursor is pointing to */
	const dict_index_t*	index,		/*!< in: record descriptor */
	mtr_t*			mtr)		/*!< in/out: mini-transaction */
{
	ulint	cur_rec_size;
	ulint	extra_size;
	ulint	cur_extra_size;
	const byte* ins_ptr;
	byte*	log_ptr;
	const byte* log_end;
	ulint	i;

	/* Avoid REDO logging to save on costly IO because
	temporary tables are not recovered during crash recovery. */
	if (dict_table_is_temporary(index->table)) {
		log_ptr = mlog_open(mtr, 0);
		if (log_ptr == NULL) {
			return;
		}
		mlog_close(mtr, log_ptr);
		log_ptr = NULL;
	}

	ut_a(rec_size < UNIV_PAGE_SIZE);
	ut_ad(page_align(insert_rec) == page_align(cursor_rec));
	ut_ad(!page_rec_is_comp(insert_rec)
	      == !dict_table_is_comp(index->table));

	{
		mem_heap_t*	heap		= NULL;
		ulint		cur_offs_[REC_OFFS_NORMAL_SIZE];
		ulint		ins_offs_[REC_OFFS_NORMAL_SIZE];

		ulint*		cur_offs;
		ulint*		ins_offs;

		rec_offs_init(cur_offs_);
		rec_offs_init(ins_offs_);

		cur_offs = rec_get_offsets(cursor_rec, index, cur_offs_,
					   ULINT_UNDEFINED, &heap);
		ins_offs = rec_get_offsets(insert_rec, index, ins_offs_,
					   ULINT_UNDEFINED, &heap);

		extra_size = rec_offs_extra_size(ins_offs);
		cur_extra_size = rec_offs_extra_size(cur_offs);
		ut_ad(rec_size == rec_offs_size(ins_offs));
		cur_rec_size = rec_offs_size(cur_offs);

		if (UNIV_LIKELY_NULL(heap)) {
			mem_heap_free(heap);
		}
	}

	ins_ptr = insert_rec - extra_size;

	i = 0;

	if (cur_extra_size == extra_size) {
		ulint		min_rec_size = ut_min(cur_rec_size, rec_size);

		const byte*	cur_ptr = cursor_rec - cur_extra_size;

		/* Find out the first byte in insert_rec which differs from
		cursor_rec; skip the bytes in the record info */

		do {
			if (*ins_ptr == *cur_ptr) {
				i++;
				ins_ptr++;
				cur_ptr++;
			} else if ((i < extra_size)
				   && (i >= extra_size
				       - page_rec_get_base_extra_size
				       (insert_rec))) {
				i = extra_size;
				ins_ptr = insert_rec;
				cur_ptr = cursor_rec;
			} else {
				break;
			}
		} while (i < min_rec_size);
	}

	if (mtr_get_log_mode(mtr) != MTR_LOG_SHORT_INSERTS) {

		if (page_rec_is_comp(insert_rec)) {
			log_ptr = mlog_open_and_write_index(
				mtr, insert_rec, index, MLOG_COMP_REC_INSERT,
				2 + 5 + 1 + 5 + 5 + MLOG_BUF_MARGIN);
			if (UNIV_UNLIKELY(!log_ptr)) {
				/* Logging in mtr is switched off
				during crash recovery: in that case
				mlog_open returns NULL */
				return;
			}
		} else {
			log_ptr = mlog_open(mtr, 11
					    + 2 + 5 + 1 + 5 + 5
					    + MLOG_BUF_MARGIN);
			if (UNIV_UNLIKELY(!log_ptr)) {
				/* Logging in mtr is switched off
				during crash recovery: in that case
				mlog_open returns NULL */
				return;
			}

			log_ptr = mlog_write_initial_log_record_fast(
				insert_rec, MLOG_REC_INSERT, log_ptr, mtr);
		}

		log_end = &log_ptr[2 + 5 + 1 + 5 + 5 + MLOG_BUF_MARGIN];
		/* Write the cursor rec offset as a 2-byte ulint */
		mach_write_to_2(log_ptr, page_offset(cursor_rec));
		log_ptr += 2;
	} else {
		log_ptr = mlog_open(mtr, 5 + 1 + 5 + 5 + MLOG_BUF_MARGIN);
		if (!log_ptr) {
			/* Logging in mtr is switched off during crash
			recovery: in that case mlog_open returns NULL */
			return;
		}
		log_end = &log_ptr[5 + 1 + 5 + 5 + MLOG_BUF_MARGIN];
	}

	if (page_rec_is_comp(insert_rec)) {
		if (UNIV_UNLIKELY
		    (rec_get_info_and_status_bits(insert_rec, TRUE)
		     != rec_get_info_and_status_bits(cursor_rec, TRUE))) {

			goto need_extra_info;
		}
	} else {
		if (UNIV_UNLIKELY
		    (rec_get_info_and_status_bits(insert_rec, FALSE)
		     != rec_get_info_and_status_bits(cursor_rec, FALSE))) {

			goto need_extra_info;
		}
	}

	if (extra_size != cur_extra_size || rec_size != cur_rec_size) {
need_extra_info:
		/* Write the record end segment length
		and the extra info storage flag */
		log_ptr += mach_write_compressed(log_ptr,
						 2 * (rec_size - i) + 1);

		/* Write the info bits */
		mach_write_to_1(log_ptr,
				rec_get_info_and_status_bits(
					insert_rec,
					page_rec_is_comp(insert_rec)));
		log_ptr++;

		/* Write the record origin offset */
		log_ptr += mach_write_compressed(log_ptr, extra_size);

		/* Write the mismatch index */
		log_ptr += mach_write_compressed(log_ptr, i);

		ut_a(i < UNIV_PAGE_SIZE);
		ut_a(extra_size < UNIV_PAGE_SIZE);
	} else {
		/* Write the record end segment length
		and the extra info storage flag */
		log_ptr += mach_write_compressed(log_ptr, 2 * (rec_size - i));
	}

	/* Write to the log the inserted index record end segment which
	differs from the cursor record */

	rec_size -= i;

	if (log_ptr + rec_size <= log_end) {
		memcpy(log_ptr, ins_ptr, rec_size);
		mlog_close(mtr, log_ptr + rec_size);
	} else {
		mlog_close(mtr, log_ptr);
		ut_a(rec_size < UNIV_PAGE_SIZE);
		mlog_catenate_string(mtr, ins_ptr, rec_size);
	}
}
#else /* !UNIV_HOTBACKUP */
# define page_cur_insert_rec_write_log(ins_rec,size,cur,index,mtr) ((void) 0)
#endif /* !UNIV_HOTBACKUP */

/***********************************************************//**
Parses a log record of a record insert on a page.
@return	end of log record or NULL */
UNIV_INTERN
byte*
page_cur_parse_insert_rec(
/*======================*/
	ibool		is_short,/*!< in: TRUE if short inserts */
	byte*		ptr,	/*!< in: buffer */
	byte*		end_ptr,/*!< in: buffer end */
	buf_block_t*	block,	/*!< in: page or NULL */
	dict_index_t*	index,	/*!< in: record descriptor */
	mtr_t*		mtr)	/*!< in: mtr or NULL */
{
	ulint	origin_offset;
	ulint	end_seg_len;
	ulint	mismatch_index;
	page_t*	page;
	rec_t*	cursor_rec;
	byte	buf1[1024];
	byte*	buf;
	byte*	ptr2			= ptr;
	ulint	info_and_status_bits = 0; /* remove warning */
	page_cur_t	cursor;
	mem_heap_t*	heap		= NULL;
	ulint		offsets_[REC_OFFS_NORMAL_SIZE];
	ulint*		offsets		= offsets_;
	rec_offs_init(offsets_);

	page = block ? buf_block_get_frame(block) : NULL;

	if (is_short) {
		cursor_rec = page_rec_get_prev(page_get_supremum_rec(page));
	} else {
		ulint	offset;

		/* Read the cursor rec offset as a 2-byte ulint */

		if (UNIV_UNLIKELY(end_ptr < ptr + 2)) {

			return(NULL);
		}

		offset = mach_read_from_2(ptr);
		ptr += 2;

		cursor_rec = page + offset;

		if (UNIV_UNLIKELY(offset >= UNIV_PAGE_SIZE)) {

			recv_sys->found_corrupt_log = TRUE;

			return(NULL);
		}
	}

	ptr = mach_parse_compressed(ptr, end_ptr, &end_seg_len);

	if (ptr == NULL) {

		return(NULL);
	}

	if (UNIV_UNLIKELY(end_seg_len >= UNIV_PAGE_SIZE << 1)) {
		recv_sys->found_corrupt_log = TRUE;

		return(NULL);
	}

	if (end_seg_len & 0x1UL) {
		/* Read the info bits */

		if (end_ptr < ptr + 1) {

			return(NULL);
		}

		info_and_status_bits = mach_read_from_1(ptr);
		ptr++;

		ptr = mach_parse_compressed(ptr, end_ptr, &origin_offset);

		if (ptr == NULL) {

			return(NULL);
		}

		ut_a(origin_offset < UNIV_PAGE_SIZE);

		ptr = mach_parse_compressed(ptr, end_ptr, &mismatch_index);

		if (ptr == NULL) {

			return(NULL);
		}

		ut_a(mismatch_index < UNIV_PAGE_SIZE);
	}

	if (UNIV_UNLIKELY(end_ptr < ptr + (end_seg_len >> 1))) {

		return(NULL);
	}

	if (!block) {

		return(ptr + (end_seg_len >> 1));
	}

	ut_ad(!!page_is_comp(page) == dict_table_is_comp(index->table));
	ut_ad(!buf_block_get_page_zip(block) || page_is_comp(page));

	/* Read from the log the inserted index record end segment which
	differs from the cursor record */

	offsets = rec_get_offsets(cursor_rec, index, offsets,
				  ULINT_UNDEFINED, &heap);

	if (!(end_seg_len & 0x1UL)) {
		info_and_status_bits = rec_get_info_and_status_bits(
			cursor_rec, page_is_comp(page));
		origin_offset = rec_offs_extra_size(offsets);
		mismatch_index = rec_offs_size(offsets) - (end_seg_len >> 1);
	}

	end_seg_len >>= 1;

	if (mismatch_index + end_seg_len < sizeof buf1) {
		buf = buf1;
	} else {
		buf = static_cast<byte*>(
			mem_alloc(mismatch_index + end_seg_len));
	}

	/* Build the inserted record to buf */

        if (UNIV_UNLIKELY(mismatch_index >= UNIV_PAGE_SIZE)) {
		ib_logf(IB_LOG_LEVEL_ERROR,
			"Is short %lu, info_and_status_bits %lu, offset %lu, "
			"o_offset %lu, mismatch index %lu, end_seg_len %lu"
			"parsed len %lu",
			(ulong) is_short, (ulong) info_and_status_bits,
			(ulong) page_offset(cursor_rec),
			(ulong) origin_offset,
			(ulong) mismatch_index, (ulong) end_seg_len,
			(ulong) (ptr - ptr2));

		fputs("Dump of 300 bytes of log:\n", stderr);
		ut_print_buf(stderr, ptr2, 300);
		putc('\n', stderr);

		buf_page_print(page, 0, 0);

		ut_error;
	}

	ut_memcpy(buf, rec_get_start(cursor_rec, offsets), mismatch_index);
	ut_memcpy(buf + mismatch_index, ptr, end_seg_len);

	if (page_is_comp(page)) {
		rec_set_info_and_status_bits(buf + origin_offset,
				     info_and_status_bits);
	} else {
		rec_set_info_bits_old(buf + origin_offset,
							info_and_status_bits);
	}

	page_cur_position(cursor_rec, block, &cursor);

	offsets = rec_get_offsets(buf + origin_offset, index, offsets,
				  ULINT_UNDEFINED, &heap);
	if (UNIV_UNLIKELY(!page_cur_rec_insert(&cursor,
					       buf + origin_offset,
					       index, offsets, mtr))) {
		/* The redo log record should only have been written
		after the write was successful. */
		ut_error;
	}

	if (buf != buf1) {

		mem_free(buf);
	}

	if (UNIV_LIKELY_NULL(heap)) {
		mem_heap_free(heap);
	}

	return(ptr + end_seg_len);
}

/***********************************************************//**
Inserts a record next to page cursor on an uncompressed page.
Returns pointer to inserted record if succeed, i.e., enough
space available, NULL otherwise. The cursor stays at the same position.
@return	pointer to record if succeed, NULL otherwise */
UNIV_INTERN
rec_t*
page_cur_insert_rec_low(
/*====================*/
	rec_t*		current_rec,/*!< in: pointer to current record after
				which the new record is inserted */
	dict_index_t*	index,	/*!< in: record descriptor */
	const rec_t*	rec,	/*!< in: pointer to a physical record */
	ulint*		offsets,/*!< in/out: rec_get_offsets(rec, index) */
	mtr_t*		mtr)	/*!< in: mini-transaction handle, or NULL */
{
	byte*		insert_buf;
	ulint		rec_size;
	page_t*		page;		/*!< the relevant page */
	rec_t*		last_insert;	/*!< cursor position at previous
					insert */
	rec_t*		free_rec;	/*!< a free record that was reused,
					or NULL */
	rec_t*		insert_rec;	/*!< inserted record */
	ulint		heap_no;	/*!< heap number of the inserted
					record */

	ut_ad(rec_offs_validate(rec, index, offsets));

	page = page_align(current_rec);
	ut_ad(dict_table_is_comp(index->table)
	      == (ibool) !!page_is_comp(page));
	ut_ad(fil_page_get_type(page) == FIL_PAGE_INDEX);
	ut_ad(mach_read_from_8(page + PAGE_HEADER + PAGE_INDEX_ID)
	      == index->id || recv_recovery_is_on() || mtr->inside_ibuf);

	ut_ad(!page_rec_is_supremum(current_rec));

	/* 1. Get the size of the physical record in the page */
	rec_size = rec_offs_size(offsets);

#ifdef UNIV_DEBUG_VALGRIND
	{
		const void*	rec_start
			= rec - rec_offs_extra_size(offsets);
		ulint		extra_size
			= rec_offs_extra_size(offsets)
			- (rec_offs_comp(offsets)
			   ? REC_N_NEW_EXTRA_BYTES
			   : REC_N_OLD_EXTRA_BYTES);

		/* All data bytes of the record must be valid. */
		UNIV_MEM_ASSERT_RW(rec, rec_offs_data_size(offsets));
		/* The variable-length header must be valid. */
		UNIV_MEM_ASSERT_RW(rec_start, extra_size);
	}
#endif /* UNIV_DEBUG_VALGRIND */

	/* 2. Try to find suitable space from page memory management */

	free_rec = page_header_get_ptr(page, PAGE_FREE);
	if (UNIV_LIKELY_NULL(free_rec)) {
		/* Try to allocate from the head of the free list. */
		ulint		foffsets_[REC_OFFS_NORMAL_SIZE];
		ulint*		foffsets	= foffsets_;
		mem_heap_t*	heap		= NULL;

		rec_offs_init(foffsets_);

		foffsets = rec_get_offsets(
			free_rec, index, foffsets, ULINT_UNDEFINED, &heap);
		if (rec_offs_size(foffsets) < rec_size) {
			if (UNIV_LIKELY_NULL(heap)) {
				mem_heap_free(heap);
			}

			goto use_heap;
		}

		insert_buf = free_rec - rec_offs_extra_size(foffsets);

		if (page_is_comp(page)) {
			heap_no = rec_get_heap_no_new(free_rec);
			page_mem_alloc_free(page, NULL,
					rec_get_next_ptr(free_rec, TRUE),
					rec_size);
		} else {
			heap_no = rec_get_heap_no_old(free_rec);
			page_mem_alloc_free(page, NULL,
					rec_get_next_ptr(free_rec, FALSE),
					rec_size);
		}

		if (UNIV_LIKELY_NULL(heap)) {
			mem_heap_free(heap);
		}
	} else {
use_heap:
		free_rec = NULL;
		insert_buf = page_mem_alloc_heap(page, NULL,
						 rec_size, &heap_no);

		if (UNIV_UNLIKELY(insert_buf == NULL)) {
			return(NULL);
		}
	}

	/* 3. Create the record */
	insert_rec = rec_copy(insert_buf, rec, offsets);
	rec_offs_make_valid(insert_rec, index, offsets);

	/* 4. Insert the record in the linked list of records */
	ut_ad(current_rec != insert_rec);

	{
		/* next record after current before the insertion */
		rec_t*	next_rec = page_rec_get_next(current_rec);
#ifdef UNIV_DEBUG
		if (page_is_comp(page)) {
			ut_ad(rec_get_status(current_rec)
				<= REC_STATUS_INFIMUM);
			ut_ad(rec_get_status(insert_rec) < REC_STATUS_INFIMUM);
			ut_ad(rec_get_status(next_rec) != REC_STATUS_INFIMUM);
		}
#endif
		page_rec_set_next(insert_rec, next_rec);
		page_rec_set_next(current_rec, insert_rec);
	}

	page_header_set_field(page, NULL, PAGE_N_RECS,
			      1 + page_get_n_recs(page));

	/* 5. Set the n_owned field in the inserted record to zero,
	and set the heap_no field */
	if (page_is_comp(page)) {
		rec_set_n_owned_new(insert_rec, NULL, 0);
		rec_set_heap_no_new(insert_rec, heap_no);
	} else {
		rec_set_n_owned_old(insert_rec, 0);
		rec_set_heap_no_old(insert_rec, heap_no);
	}

	UNIV_MEM_ASSERT_RW(rec_get_start(insert_rec, offsets),
			   rec_offs_size(offsets));
	/* 6. Update the last insertion info in page header */

	last_insert = page_header_get_ptr(page, PAGE_LAST_INSERT);
	ut_ad(!last_insert || !page_is_comp(page)
	      || rec_get_node_ptr_flag(last_insert)
	      == rec_get_node_ptr_flag(insert_rec));

	if (UNIV_UNLIKELY(last_insert == NULL)) {
		page_header_set_field(page, NULL, PAGE_DIRECTION,
				      PAGE_NO_DIRECTION);
		page_header_set_field(page, NULL, PAGE_N_DIRECTION, 0);

	} else if ((last_insert == current_rec)
		   && (page_header_get_field(page, PAGE_DIRECTION)
		       != PAGE_LEFT)) {

		page_header_set_field(page, NULL, PAGE_DIRECTION,
							PAGE_RIGHT);
		page_header_set_field(page, NULL, PAGE_N_DIRECTION,
				      page_header_get_field(
					      page, PAGE_N_DIRECTION) + 1);

	} else if ((page_rec_get_next(insert_rec) == last_insert)
		   && (page_header_get_field(page, PAGE_DIRECTION)
		       != PAGE_RIGHT)) {

		page_header_set_field(page, NULL, PAGE_DIRECTION,
							PAGE_LEFT);
		page_header_set_field(page, NULL, PAGE_N_DIRECTION,
				      page_header_get_field(
					      page, PAGE_N_DIRECTION) + 1);
	} else {
		page_header_set_field(page, NULL, PAGE_DIRECTION,
							PAGE_NO_DIRECTION);
		page_header_set_field(page, NULL, PAGE_N_DIRECTION, 0);
	}

	page_header_set_ptr(page, NULL, PAGE_LAST_INSERT, insert_rec);

	/* 7. It remains to update the owner record. */
	{
		rec_t*	owner_rec	= page_rec_find_owner_rec(insert_rec);
		ulint	n_owned;
		if (page_is_comp(page)) {
			n_owned = rec_get_n_owned_new(owner_rec);
			rec_set_n_owned_new(owner_rec, NULL, n_owned + 1);
		} else {
			n_owned = rec_get_n_owned_old(owner_rec);
			rec_set_n_owned_old(owner_rec, n_owned + 1);
		}

		/* 8. Now we have incremented the n_owned field of the owner
		record. If the number exceeds PAGE_DIR_SLOT_MAX_N_OWNED,
		we have to split the corresponding directory slot in two. */

		if (UNIV_UNLIKELY(n_owned == PAGE_DIR_SLOT_MAX_N_OWNED)) {
			page_dir_split_slot(
				page, NULL,
				page_dir_find_owner_slot(owner_rec));
		}
	}

	/* 9. Write log record of the insert */
	if (UNIV_LIKELY(mtr != NULL)) {
		page_cur_insert_rec_write_log(insert_rec, rec_size,
					      current_rec, index, mtr);
	}

	btr_blob_dbg_add_rec(insert_rec, index, offsets, "insert");

	return(insert_rec);
}

/***********************************************************//**
Inserts a record next to page cursor on a compressed and uncompressed
page. Returns pointer to inserted record if succeed, i.e.,
enough space available, NULL otherwise.
The cursor stays at the same position.

IMPORTANT: The caller will have to update IBUF_BITMAP_FREE
if this is a compressed leaf page in a secondary index.
This has to be done either within the same mini-transaction,
or by invoking ibuf_reset_free_bits() before mtr_commit().

@return	pointer to record if succeed, NULL otherwise */
UNIV_INTERN
rec_t*
page_cur_insert_rec_zip(
/*====================*/
	page_cur_t*	cursor,	/*!< in/out: page cursor */
	dict_index_t*	index,	/*!< in: record descriptor */
	const rec_t*	rec,	/*!< in: pointer to a physical record */
	ulint*		offsets,/*!< in/out: rec_get_offsets(rec, index) */
	mtr_t*		mtr)	/*!< in: mini-transaction handle, or NULL */
{
	byte*		insert_buf;
	ulint		rec_size;
	page_t*		page;		/*!< the relevant page */
	rec_t*		last_insert;	/*!< cursor position at previous
					insert */
	rec_t*		free_rec;	/*!< a free record that was reused,
					or NULL */
	rec_t*		insert_rec;	/*!< inserted record */
	ulint		heap_no;	/*!< heap number of the inserted
					record */
	page_zip_des_t*	page_zip;

	page_zip = page_cur_get_page_zip(cursor);
	ut_ad(page_zip);

	ut_ad(rec_offs_validate(rec, index, offsets));

	page = page_cur_get_page(cursor);
	ut_ad(dict_table_is_comp(index->table));
	ut_ad(page_is_comp(page));
	ut_ad(fil_page_get_type(page) == FIL_PAGE_INDEX);
	ut_ad(mach_read_from_8(page + PAGE_HEADER + PAGE_INDEX_ID)
	      == index->id || mtr->inside_ibuf || recv_recovery_is_on());

	ut_ad(!page_cur_is_after_last(cursor));
	page_zip_validate_if_zip(page_zip, page, index);

	/* 1. Get the size of the physical record in the page */
	rec_size = rec_offs_size(offsets);

#ifdef UNIV_DEBUG_VALGRIND
	{
		const void*	rec_start
			= rec - rec_offs_extra_size(offsets);
		ulint		extra_size
			= rec_offs_extra_size(offsets)
			- (rec_offs_comp(offsets)
			   ? REC_N_NEW_EXTRA_BYTES
			   : REC_N_OLD_EXTRA_BYTES);

		/* All data bytes of the record must be valid. */
		UNIV_MEM_ASSERT_RW(rec, rec_offs_data_size(offsets));
		/* The variable-length header must be valid. */
		UNIV_MEM_ASSERT_RW(rec_start, extra_size);
	}
#endif /* UNIV_DEBUG_VALGRIND */

	const bool reorg_before_insert = page_has_garbage(page)
		&& rec_size > page_get_max_insert_size(page, 1)
		&& rec_size <= page_get_max_insert_size_after_reorganize(
			page, 1);

	/* 2. Try to find suitable space from page memory management */
	if (!page_zip_available(page_zip, dict_index_is_clust(index),
				rec_size, 1)
	    || reorg_before_insert) {
		/* The values can change dynamically. */
		bool	log_compressed	= page_zip_log_pages;
		ulint	level		= page_zip_level;
#ifdef UNIV_DEBUG
		rec_t*	cursor_rec	= page_cur_get_rec(cursor);
#endif /* UNIV_DEBUG */

		/* If we are not writing compressed page images, we
		must reorganize the page before attempting the
		insert. */
		if (recv_recovery_is_on()) {
			/* Insert into the uncompressed page only.
			The page reorganization or creation that we
			would attempt outside crash recovery would
			have been covered by a previous redo log record. */
		} else if (page_is_empty(page)) {
			ut_ad(page_cur_is_before_first(cursor));

			/* This is an empty page. Recreate it to
			get rid of the modification log. */
			page_create_zip(page_cur_get_block(cursor), index,
					page_header_get_field(page, PAGE_LEVEL),
					level, 0, mtr);
			ut_ad(!page_header_get_ptr(page, PAGE_FREE));

			if (page_zip_available(
				    page_zip, dict_index_is_clust(index),
				    rec_size, 1)) {
				goto use_heap;
			}

			/* The cursor should remain on the page infimum. */
			return(NULL);
		} else if (!page_zip->m_nonempty && !page_has_garbage(page)) {
			/* The page has been freshly compressed, so
			reorganizing it will not help. */
		} else if (log_compressed && !reorg_before_insert) {
			/* Insert into uncompressed page only, and
			try page_zip_reorganize() afterwards. */
		} else if (btr_page_reorganize_low(
				   recv_recovery_is_on(), level,
				   cursor, index, mtr)) {
			ut_ad(!page_header_get_ptr(page, PAGE_FREE));

			if (page_zip_available(
				    page_zip, dict_index_is_clust(index),
				    rec_size, 1)) {
				/* After reorganizing, there is space
				available. */
				goto use_heap;
			}
		} else {
			ut_ad(cursor->rec == cursor_rec);
			return(NULL);
		}

		/* Try compressing the whole page afterwards. */
		insert_rec = page_cur_insert_rec_low(
			cursor->rec, index, rec, offsets, NULL);

		/* If recovery is on, this implies that the compression
		of the page was successful during runtime. Had that not
		been the case or had the redo logging of compressed
		pages been enabled during runtime then we'd have seen
		a MLOG_ZIP_PAGE_COMPRESS redo record. Therefore, we
		know that we don't need to reorganize the page. We,
		however, do need to recompress the page. That will
		happen when the next redo record is read which must
		be of type MLOG_ZIP_PAGE_COMPRESS_NO_DATA and it must
		contain a valid compression level value.
		This implies that during recovery from this point till
		the next redo is applied the uncompressed and
		compressed versions are not identical and
		page_zip_validate will fail but that is OK because
		we call page_zip_validate only after processing
		all changes to a page under a single mtr during
		recovery. */
		if (insert_rec == NULL) {
			/* Out of space.
			This should never occur during crash recovery,
			because the MLOG_COMP_REC_INSERT should only
			be logged after a successful operation. */
			ut_ad(!recv_recovery_is_on());
		} else if (recv_recovery_is_on()) {
			/* This should be followed by
			MLOG_ZIP_PAGE_COMPRESS_NO_DATA,
			which should succeed. */
			rec_offs_make_valid(insert_rec, index, offsets);
		} else {
			ulint	pos = page_rec_get_n_recs_before(insert_rec);
			ut_ad(pos > 0);

			if (!log_compressed) {
				if (page_zip_compress(
					    page_zip, page, index,
					    level, NULL)) {
					page_cur_insert_rec_write_log(
						insert_rec, rec_size,
						cursor->rec, index, mtr);
					page_zip_compress_write_log_no_data(
						level, page, index, mtr);

					rec_offs_make_valid(
						insert_rec, index, offsets);
					return(insert_rec);
				}

				ut_ad(cursor->rec
				      == (pos > 1
					  ? page_rec_get_nth(
						  page, pos - 1)
					  : page + PAGE_NEW_INFIMUM));
			} else {
				/* We are writing entire page images
				to the log. Reduce the redo log volume
				by reorganizing the page at the same time. */
				if (page_zip_reorganize(
					    cursor->block, index, mtr)) {
					/* The page was reorganized:
					Seek to pos. */
					if (pos > 1) {
						cursor->rec = page_rec_get_nth(
							page, pos - 1);
					} else {
						cursor->rec = page
							+ PAGE_NEW_INFIMUM;
					}

					insert_rec = page + rec_get_next_offs(
						cursor->rec, TRUE);
					rec_offs_make_valid(
						insert_rec, index, offsets);
					return(insert_rec);
				}

				/* Theoretically, we could try one
				last resort of btr_page_reorganize_low()
				followed by page_zip_available(), but
				that would be very unlikely to
				succeed. (If the full reorganized page
				failed to compress, why would it
				succeed to compress the page, plus log
				the insert of this record? */
			}

			/* Out of space: restore the page */
			btr_blob_dbg_remove(page, index, "insert_zip_fail");
			if (!page_zip_decompress(page_zip, page, FALSE)) {
				ut_error; /* Memory corrupted? */
			}
			ut_ad(page_validate(page, index));
			btr_blob_dbg_add(page, index, "insert_zip_fail");
			insert_rec = NULL;
		}

		return(insert_rec);
	}

	free_rec = page_header_get_ptr(page, PAGE_FREE);
	if (UNIV_LIKELY_NULL(free_rec)) {
		/* Try to allocate from the head of the free list. */
		lint	extra_size_diff;
		ulint		foffsets_[REC_OFFS_NORMAL_SIZE];
		ulint*		foffsets	= foffsets_;
		mem_heap_t*	heap		= NULL;

		rec_offs_init(foffsets_);

		foffsets = rec_get_offsets(free_rec, index, foffsets,
					   ULINT_UNDEFINED, &heap);
		if (rec_offs_size(foffsets) < rec_size) {
too_small:
			if (UNIV_LIKELY_NULL(heap)) {
				mem_heap_free(heap);
			}

			goto use_heap;
		}

		insert_buf = free_rec - rec_offs_extra_size(foffsets);

		/* On compressed pages, do not relocate records from
		the free list.  If extra_size would grow, use the heap. */
		extra_size_diff
			= rec_offs_extra_size(offsets)
			- rec_offs_extra_size(foffsets);

		if (UNIV_UNLIKELY(extra_size_diff < 0)) {
			/* Add an offset to the extra_size. */
			if (rec_offs_size(foffsets)
			    < rec_size - extra_size_diff) {

				goto too_small;
			}

			insert_buf -= extra_size_diff;
		} else if (UNIV_UNLIKELY(extra_size_diff)) {
			/* Do not allow extra_size to grow */

			goto too_small;
		}

		heap_no = rec_get_heap_no_new(free_rec);
		page_mem_alloc_free(page, page_zip,
				    rec_get_next_ptr(free_rec, TRUE),
				    rec_size);

		if (!page_is_leaf(page)) {
			/* Zero out the node pointer of free_rec,
			in case it will not be overwritten by
			insert_rec. */

			ut_ad(rec_size > REC_NODE_PTR_SIZE);

			if (rec_offs_extra_size(foffsets)
			    + rec_offs_data_size(foffsets) > rec_size) {

				memset(rec_get_end(free_rec, foffsets)
				       - REC_NODE_PTR_SIZE, 0,
				       REC_NODE_PTR_SIZE);
			}
		} else if (dict_index_is_clust(index)) {
			/* Zero out the DB_TRX_ID and DB_ROLL_PTR
			columns of free_rec, in case it will not be
			overwritten by insert_rec. */

			ulint	trx_id_col;
			ulint	trx_id_offs;
			ulint	len;

			trx_id_col = dict_index_get_sys_col_pos(index,
								DATA_TRX_ID);
			ut_ad(trx_id_col > 0);
			ut_ad(trx_id_col != ULINT_UNDEFINED);

			trx_id_offs = rec_get_nth_field_offs(foffsets,
							     trx_id_col, &len);
			ut_ad(len == DATA_TRX_ID_LEN);

			if (DATA_TRX_ID_LEN + DATA_ROLL_PTR_LEN + trx_id_offs
			    + rec_offs_extra_size(foffsets) > rec_size) {
				/* We will have to zero out the
				DB_TRX_ID and DB_ROLL_PTR, because
				they will not be fully overwritten by
				insert_rec. */

				memset(free_rec + trx_id_offs, 0,
				       DATA_TRX_ID_LEN + DATA_ROLL_PTR_LEN);
			}

			ut_ad(free_rec + trx_id_offs + DATA_TRX_ID_LEN
			      == rec_get_nth_field(free_rec, foffsets,
						   trx_id_col + 1, &len));
			ut_ad(len == DATA_ROLL_PTR_LEN);
		}

		if (UNIV_LIKELY_NULL(heap)) {
			mem_heap_free(heap);
		}
	} else {
use_heap:
		free_rec = NULL;
		insert_buf = page_mem_alloc_heap(page, page_zip,
						 rec_size, &heap_no);

		if (UNIV_UNLIKELY(insert_buf == NULL)) {
			return(NULL);
		}

		page_zip_dir_add_slot(page_zip, dict_index_is_clust(index));
	}

	/* 3. Create the record */
	insert_rec = rec_copy(insert_buf, rec, offsets);
	rec_offs_make_valid(insert_rec, index, offsets);

	/* 4. Insert the record in the linked list of records */
	ut_ad(cursor->rec != insert_rec);

	{
		/* next record after current before the insertion */
		const rec_t*	next_rec = page_rec_get_next_low(
			cursor->rec, TRUE);
		ut_ad(rec_get_status(cursor->rec)
		      <= REC_STATUS_INFIMUM);
		ut_ad(rec_get_status(insert_rec) < REC_STATUS_INFIMUM);
		ut_ad(rec_get_status(next_rec) != REC_STATUS_INFIMUM);

		page_rec_set_next(insert_rec, next_rec);
		page_rec_set_next(cursor->rec, insert_rec);
	}

	page_header_set_field(page, page_zip, PAGE_N_RECS,
			      1 + page_get_n_recs(page));

	/* 5. Set the n_owned field in the inserted record to zero,
	and set the heap_no field */
	rec_set_n_owned_new(insert_rec, NULL, 0);
	rec_set_heap_no_new(insert_rec, heap_no);

	UNIV_MEM_ASSERT_RW(rec_get_start(insert_rec, offsets),
			   rec_offs_size(offsets));

	page_zip_dir_insert(page_zip, cursor->rec, free_rec, insert_rec);

	/* 6. Update the last insertion info in page header */

	last_insert = page_header_get_ptr(page, PAGE_LAST_INSERT);
	ut_ad(!last_insert
	      || rec_get_node_ptr_flag(last_insert)
	      == rec_get_node_ptr_flag(insert_rec));

	if (UNIV_UNLIKELY(last_insert == NULL)) {
		page_header_set_field(page, page_zip, PAGE_DIRECTION,
							PAGE_NO_DIRECTION);
		page_header_set_field(page, page_zip, PAGE_N_DIRECTION, 0);

	} else if ((last_insert == cursor->rec)
		   && (page_header_get_field(page, PAGE_DIRECTION)
		       != PAGE_LEFT)) {

		page_header_set_field(page, page_zip, PAGE_DIRECTION,
							PAGE_RIGHT);
		page_header_set_field(page, page_zip, PAGE_N_DIRECTION,
				      page_header_get_field(
					      page, PAGE_N_DIRECTION) + 1);

	} else if ((page_rec_get_next(insert_rec) == last_insert)
		   && (page_header_get_field(page, PAGE_DIRECTION)
		       != PAGE_RIGHT)) {

		page_header_set_field(page, page_zip, PAGE_DIRECTION,
							PAGE_LEFT);
		page_header_set_field(page, page_zip, PAGE_N_DIRECTION,
				      page_header_get_field(
					      page, PAGE_N_DIRECTION) + 1);
	} else {
		page_header_set_field(page, page_zip, PAGE_DIRECTION,
							PAGE_NO_DIRECTION);
		page_header_set_field(page, page_zip, PAGE_N_DIRECTION, 0);
	}

	page_header_set_ptr(page, page_zip, PAGE_LAST_INSERT, insert_rec);

	/* 7. It remains to update the owner record. */
	{
		rec_t*	owner_rec	= page_rec_find_owner_rec(insert_rec);
		ulint	n_owned;

		n_owned = rec_get_n_owned_new(owner_rec);
		rec_set_n_owned_new(owner_rec, page_zip, n_owned + 1);

		/* 8. Now we have incremented the n_owned field of the owner
		record. If the number exceeds PAGE_DIR_SLOT_MAX_N_OWNED,
		we have to split the corresponding directory slot in two. */

		if (UNIV_UNLIKELY(n_owned == PAGE_DIR_SLOT_MAX_N_OWNED)) {
			page_dir_split_slot(
				page, page_zip,
				page_dir_find_owner_slot(owner_rec));
		}
	}

	page_zip_write_rec(page_zip, insert_rec, index, offsets, 1);

	btr_blob_dbg_add_rec(insert_rec, index, offsets, "insert_zip_ok");

	/* 9. Write log record of the insert */
	if (UNIV_LIKELY(mtr != NULL)) {
		page_cur_insert_rec_write_log(insert_rec, rec_size,
					      cursor->rec, index, mtr);
	}

	return(insert_rec);
}

#ifndef UNIV_HOTBACKUP
/**********************************************************//**
Writes a log record of copying a record list end to a new created page.
@return 4-byte field where to write the log data length, or NULL if
logging is disabled */
UNIV_INLINE
byte*
page_copy_rec_list_to_created_page_write_log(
/*=========================================*/
	page_t*		page,	/*!< in: index page */
	dict_index_t*	index,	/*!< in: record descriptor */
	mtr_t*		mtr)	/*!< in: mtr */
{
	byte*	log_ptr;

	ut_ad(!!page_is_comp(page) == dict_table_is_comp(index->table));

	log_ptr = mlog_open_and_write_index(mtr, page, index,
					    page_is_comp(page)
					    ? MLOG_COMP_LIST_END_COPY_CREATED
					    : MLOG_LIST_END_COPY_CREATED, 4);
	if (UNIV_LIKELY(log_ptr != NULL)) {
		mlog_close(mtr, log_ptr + 4);
	}

	return(log_ptr);
}
#endif /* !UNIV_HOTBACKUP */

/**********************************************************//**
Parses a log record of copying a record list end to a new created page.
@return	end of log record or NULL */
UNIV_INTERN
byte*
page_parse_copy_rec_list_to_created_page(
/*=====================================*/
	byte*		ptr,	/*!< in: buffer */
	byte*		end_ptr,/*!< in: buffer end */
	buf_block_t*	block,	/*!< in: page or NULL */
	dict_index_t*	index,	/*!< in: record descriptor */
	mtr_t*		mtr)	/*!< in: mtr or NULL */
{
	byte*		rec_end;
	ulint		log_data_len;
	page_t*		page;
	page_zip_des_t*	page_zip;

	if (ptr + 4 > end_ptr) {

		return(NULL);
	}

	log_data_len = mach_read_from_4(ptr);
	ptr += 4;

	rec_end = ptr + log_data_len;

	if (rec_end > end_ptr) {

		return(NULL);
	}

	if (!block) {

		return(rec_end);
	}

	while (ptr < rec_end) {
		ptr = page_cur_parse_insert_rec(TRUE, ptr, end_ptr,
						block, index, mtr);
	}

	ut_a(ptr == rec_end);

	page = buf_block_get_frame(block);
	page_zip = buf_block_get_page_zip(block);

	page_header_set_ptr(page, page_zip, PAGE_LAST_INSERT, NULL);
	page_header_set_field(page, page_zip, PAGE_DIRECTION,
							PAGE_NO_DIRECTION);
	page_header_set_field(page, page_zip, PAGE_N_DIRECTION, 0);

	return(rec_end);
}

#ifndef UNIV_HOTBACKUP
/*************************************************************//**
Copies records from page to a newly created page, from a given record onward,
including that record. Infimum and supremum records are not copied.

IMPORTANT: The caller will have to update IBUF_BITMAP_FREE
if this is a compressed leaf page in a secondary index.
This has to be done either within the same mini-transaction,
or by invoking ibuf_reset_free_bits() before mtr_commit(). */
UNIV_INTERN
void
page_copy_rec_list_end_to_created_page(
/*===================================*/
	page_t*		new_page,	/*!< in/out: index page to copy to */
	rec_t*		rec,		/*!< in: first record to copy */
	dict_index_t*	index,		/*!< in: record descriptor */
	mtr_t*		mtr)		/*!< in: mtr */
{
	page_dir_slot_t* slot = 0; /* remove warning */
	byte*	heap_top;
	rec_t*	insert_rec = 0; /* remove warning */
	rec_t*	prev_rec;
	ulint	count;
	ulint	n_recs;
	ulint	slot_index;
	ulint	rec_size;
	ulint	log_mode;
	byte*	log_ptr;
	ulint	log_data_len;
	mem_heap_t*	heap		= NULL;
	ulint		offsets_[REC_OFFS_NORMAL_SIZE];
	ulint*		offsets		= offsets_;
	rec_offs_init(offsets_);

	ut_ad(page_dir_get_n_heap(new_page) == PAGE_HEAP_NO_USER_LOW);
	ut_ad(page_align(rec) != new_page);
	ut_ad(page_rec_is_comp(rec) == page_is_comp(new_page));

	if (page_rec_is_infimum(rec)) {

		rec = page_rec_get_next(rec);
	}

	if (page_rec_is_supremum(rec)) {

		return;
	}

#ifdef UNIV_DEBUG
	/* To pass the debug tests we have to set these dummy values
	in the debug version */
	page_dir_set_n_slots(new_page, NULL, UNIV_PAGE_SIZE / 2);
	page_header_set_ptr(new_page, NULL, PAGE_HEAP_TOP,
			    new_page + UNIV_PAGE_SIZE - 1);
#endif

	log_ptr = page_copy_rec_list_to_created_page_write_log(new_page,
							       index, mtr);

	log_data_len = dyn_array_get_data_size(&(mtr->log));

	/* Individual inserts are logged in a shorter form */
	if (!dict_table_is_temporary(index->table)) {
		log_mode = mtr_set_log_mode(mtr, MTR_LOG_SHORT_INSERTS);
	} else {
		log_mode = mtr_get_log_mode(mtr);
	}

	prev_rec = page_get_infimum_rec(new_page);
	if (page_is_comp(new_page)) {
		heap_top = new_page + PAGE_NEW_SUPREMUM_END;
	} else {
		heap_top = new_page + PAGE_OLD_SUPREMUM_END;
	}
	count = 0;
	slot_index = 0;
	n_recs = 0;

	do {
		offsets = rec_get_offsets(rec, index, offsets,
					  ULINT_UNDEFINED, &heap);
		insert_rec = rec_copy(heap_top, rec, offsets);

		if (page_is_comp(new_page)) {
			rec_set_next_offs_new(prev_rec,
					      page_offset(insert_rec));

			rec_set_n_owned_new(insert_rec, NULL, 0);
			rec_set_heap_no_new(insert_rec,
					    PAGE_HEAP_NO_USER_LOW + n_recs);
		} else {
			rec_set_next_offs_old(prev_rec,
					      page_offset(insert_rec));

			rec_set_n_owned_old(insert_rec, 0);
			rec_set_heap_no_old(insert_rec,
					    PAGE_HEAP_NO_USER_LOW + n_recs);
		}

		count++;
		n_recs++;

		if (UNIV_UNLIKELY
		    (count == (PAGE_DIR_SLOT_MAX_N_OWNED + 1) / 2)) {

			slot_index++;

			slot = page_dir_get_nth_slot(new_page, slot_index);

			page_dir_slot_set_rec(slot, insert_rec);
			page_dir_slot_set_n_owned(slot, NULL, count);

			count = 0;
		}

		rec_size = rec_offs_size(offsets);

		ut_ad(heap_top < new_page + UNIV_PAGE_SIZE);

		heap_top += rec_size;

		rec_offs_make_valid(insert_rec, index, offsets);
		btr_blob_dbg_add_rec(insert_rec, index, offsets, "copy_end");

		page_cur_insert_rec_write_log(insert_rec, rec_size, prev_rec,
					      index, mtr);
		prev_rec = insert_rec;
		rec = page_rec_get_next(rec);
	} while (!page_rec_is_supremum(rec));

	if ((slot_index > 0) && (count + 1
				 + (PAGE_DIR_SLOT_MAX_N_OWNED + 1) / 2
				 <= PAGE_DIR_SLOT_MAX_N_OWNED)) {
		/* We can merge the two last dir slots. This operation is
		here to make this function imitate exactly the equivalent
		task made using page_cur_insert_rec, which we use in database
		recovery to reproduce the task performed by this function.
		To be able to check the correctness of recovery, it is good
		that it imitates exactly. */

		count += (PAGE_DIR_SLOT_MAX_N_OWNED + 1) / 2;

		page_dir_slot_set_n_owned(slot, NULL, 0);

		slot_index--;
	}

	if (UNIV_LIKELY_NULL(heap)) {
		mem_heap_free(heap);
	}

	log_data_len = dyn_array_get_data_size(&(mtr->log)) - log_data_len;

	ut_a(log_data_len < 100 * UNIV_PAGE_SIZE);

	if (UNIV_LIKELY(log_ptr != NULL)) {
		mach_write_to_4(log_ptr, log_data_len);
	}

	if (page_is_comp(new_page)) {
		rec_set_next_offs_new(insert_rec, PAGE_NEW_SUPREMUM);
	} else {
		rec_set_next_offs_old(insert_rec, PAGE_OLD_SUPREMUM);
	}

	slot = page_dir_get_nth_slot(new_page, 1 + slot_index);

	page_dir_slot_set_rec(slot, page_get_supremum_rec(new_page));
	page_dir_slot_set_n_owned(slot, NULL, count + 1);

	page_dir_set_n_slots(new_page, NULL, 2 + slot_index);
	page_header_set_ptr(new_page, NULL, PAGE_HEAP_TOP, heap_top);
	page_dir_set_n_heap(new_page, NULL, PAGE_HEAP_NO_USER_LOW + n_recs);
	page_header_set_field(new_page, NULL, PAGE_N_RECS, n_recs);

	page_header_set_ptr(new_page, NULL, PAGE_LAST_INSERT, NULL);
	page_header_set_field(new_page, NULL, PAGE_DIRECTION,
							PAGE_NO_DIRECTION);
	page_header_set_field(new_page, NULL, PAGE_N_DIRECTION, 0);

	/* Restore the log mode */

	mtr_set_log_mode(mtr, log_mode);
}

/***********************************************************//**
Writes log record of a record delete on a page. */
UNIV_INLINE
void
page_cur_delete_rec_write_log(
/*==========================*/
	const rec_t*		rec,	/*!< in: record to be deleted */
	const dict_index_t*	index,	/*!< in: record descriptor */
	mtr_t*			mtr)	/*!< in: mini-transaction handle */
{
	byte*	log_ptr;

	ut_ad(!!page_rec_is_comp(rec) == dict_table_is_comp(index->table));

	log_ptr = mlog_open_and_write_index(mtr, rec, index,
					    page_rec_is_comp(rec)
					    ? MLOG_COMP_REC_DELETE
					    : MLOG_REC_DELETE, 2);

	if (!log_ptr) {
		/* Logging in mtr is switched off during crash recovery:
		in that case mlog_open returns NULL */
		return;
	}

	/* Write the cursor rec offset as a 2-byte ulint */
	mach_write_to_2(log_ptr, page_offset(rec));

	mlog_close(mtr, log_ptr + 2);
}
#else /* !UNIV_HOTBACKUP */
# define page_cur_delete_rec_write_log(rec,index,mtr) ((void) 0)
#endif /* !UNIV_HOTBACKUP */

/***********************************************************//**
Parses log record of a record delete on a page.
@return	pointer to record end or NULL */
UNIV_INTERN
byte*
page_cur_parse_delete_rec(
/*======================*/
	byte*		ptr,	/*!< in: buffer */
	byte*		end_ptr,/*!< in: buffer end */
	buf_block_t*	block,	/*!< in: page or NULL */
	dict_index_t*	index,	/*!< in: record descriptor */
	mtr_t*		mtr)	/*!< in: mtr or NULL */
{
	ulint		offset;
	page_cur_t	cursor;

	if (end_ptr < ptr + 2) {

		return(NULL);
	}

	/* Read the cursor rec offset as a 2-byte ulint */
	offset = mach_read_from_2(ptr);
	ptr += 2;

	ut_a(offset <= UNIV_PAGE_SIZE);

	if (block) {
		page_t*		page		= buf_block_get_frame(block);
		mem_heap_t*	heap		= NULL;
		ulint		offsets_[REC_OFFS_NORMAL_SIZE];
		rec_t*		rec		= page + offset;
		rec_offs_init(offsets_);

		page_cur_position(rec, block, &cursor);
		ut_ad(!buf_block_get_page_zip(block) || page_is_comp(page));

		page_cur_delete_rec(&cursor, index,
				    rec_get_offsets(rec, index, offsets_,
						    ULINT_UNDEFINED, &heap),
				    mtr);
		if (UNIV_LIKELY_NULL(heap)) {
			mem_heap_free(heap);
		}
	}

	return(ptr);
}

/***********************************************************//**
Deletes a record at the page cursor. The cursor is moved to the next
record after the deleted one. */
UNIV_INTERN
void
page_cur_delete_rec(
/*================*/
	page_cur_t*		cursor,	/*!< in/out: a page cursor */
	const dict_index_t*	index,	/*!< in: record descriptor */
	const ulint*		offsets,/*!< in: rec_get_offsets(
					cursor->rec, index) */
	mtr_t*			mtr)	/*!< in: mini-transaction handle */
{
	page_dir_slot_t* cur_dir_slot;
	page_dir_slot_t* prev_slot;
	page_t*		page;
	page_zip_des_t*	page_zip;
	rec_t*		current_rec;
	rec_t*		prev_rec	= NULL;
	rec_t*		next_rec;
	ulint		cur_slot_no;
	ulint		cur_n_owned;
	rec_t*		rec;

	page = page_cur_get_page(cursor);
	page_zip = page_cur_get_page_zip(cursor);

	/* page_zip_validate() will fail here when
	btr_cur_pessimistic_delete() invokes btr_set_min_rec_mark().
	Then, both "page_zip" and "page" would have the min-rec-mark
	set on the smallest user record, but "page" would additionally
	have it set on the smallest-but-one record.  Because sloppy
	page_zip_validate_low() only ignores min-rec-flag differences
	in the smallest user record, it cannot be used here either. */

	current_rec = cursor->rec;
	ut_ad(rec_offs_validate(current_rec, index, offsets));
	ut_ad(!!page_is_comp(page) == dict_table_is_comp(index->table));
	ut_ad(fil_page_get_type(page) == FIL_PAGE_INDEX);
	ut_ad(mach_read_from_8(page + PAGE_HEADER + PAGE_INDEX_ID)
	      == index->id || mtr->inside_ibuf || recv_recovery_is_on());

	/* The record must not be the supremum or infimum record. */
	ut_ad(page_rec_is_user_rec(current_rec));

	if (page_get_n_recs(page) == 1) {
		/* Empty the page. */
		ut_ad(page_is_leaf(page));
		/* Usually, this should be the root page,
		and the whole index tree should become empty.
		However, this could also be a call in
		btr_cur_pessimistic_update() to delete the only
		record in the page and to insert another one. */
		page_cur_move_to_next(cursor);
		ut_ad(page_cur_is_after_last(cursor));
		page_create_empty(page_cur_get_block(cursor),
				  const_cast<dict_index_t*>(index), mtr);
		return;
	}

	/* Save to local variables some data associated with current_rec */
	cur_slot_no = page_dir_find_owner_slot(current_rec);
	ut_ad(cur_slot_no > 0);
	cur_dir_slot = page_dir_get_nth_slot(page, cur_slot_no);
	cur_n_owned = page_dir_slot_get_n_owned(cur_dir_slot);

	/* 0. Write the log record */
	if (mtr != 0) {
		page_cur_delete_rec_write_log(current_rec, index, mtr);
	}

	/* 1. Reset the last insert info in the page header and increment
	the modify clock for the frame */

	page_header_set_ptr(page, page_zip, PAGE_LAST_INSERT, NULL);

	/* The page gets invalid for optimistic searches: increment the
	frame modify clock only if there is an mini-transaction covering
	the change. During IMPORT we allocate local blocks that are not
	part of the buffer pool. */

	if (mtr != 0) {
		buf_block_modify_clock_inc(page_cur_get_block(cursor));
	}

	/* 2. Find the next and the previous record. Note that the cursor is
	left at the next record. */

	ut_ad(cur_slot_no > 0);
	prev_slot = page_dir_get_nth_slot(page, cur_slot_no - 1);

	rec = (rec_t*) page_dir_slot_get_rec(prev_slot);

	/* rec now points to the record of the previous directory slot. Look
	for the immediate predecessor of current_rec in a loop. */

	while(current_rec != rec) {
		prev_rec = rec;
		rec = page_rec_get_next(rec);
	}

	page_cur_move_to_next(cursor);
	next_rec = cursor->rec;

	/* 3. Remove the record from the linked list of records */

	page_rec_set_next(prev_rec, next_rec);

	/* 4. If the deleted record is pointed to by a dir slot, update the
	record pointer in slot. In the following if-clause we assume that
	prev_rec is owned by the same slot, i.e., PAGE_DIR_SLOT_MIN_N_OWNED
	>= 2. */

#if PAGE_DIR_SLOT_MIN_N_OWNED < 2
# error "PAGE_DIR_SLOT_MIN_N_OWNED < 2"
#endif
	ut_ad(cur_n_owned > 1);

	if (current_rec == page_dir_slot_get_rec(cur_dir_slot)) {
		page_dir_slot_set_rec(cur_dir_slot, prev_rec);
	}

	/* 5. Update the number of owned records of the slot */

	page_dir_slot_set_n_owned(cur_dir_slot, page_zip, cur_n_owned - 1);

	/* 6. Free the memory occupied by the record */
	btr_blob_dbg_remove_rec(current_rec, const_cast<dict_index_t*>(index),
				offsets, "delete");
	page_mem_free(page, page_zip, current_rec, index,
		      rec_offs_data_size(offsets),
		      rec_offs_extra_size(offsets),
		      rec_offs_n_extern(offsets));

	/* 7. Now we have decremented the number of owned records of the slot.
	If the number drops below PAGE_DIR_SLOT_MIN_N_OWNED, we balance the
	slots. */

	if (cur_n_owned <= PAGE_DIR_SLOT_MIN_N_OWNED) {
		page_dir_balance_slot(page, page_zip, cur_slot_no);
	}

	page_zip_validate_if_zip(page_zip, page, index);
}

/** Initialize a page cursor, either to rec or the page infimum.
Allocates and initializes offsets[]. */
UNIV_INTERN
void
PageCur::init(void)
{
	ut_ad(dict_table_is_comp(m_index->table));

	const ulint	n	= getNumFields();
	const ulint	size	= n + (1 + REC_OFFS_HEADER_SIZE);
	const page_t*	page	= buf_block_get_frame(m_block);

	m_offsets = new ulint[size];
	rec_offs_set_n_alloc(m_offsets, size);
	rec_offs_set_n_fields(m_offsets, n);

	if (!m_rec) {
		/* Initialize to the page infimum. */
		m_rec = page + page_get_infimum_offset(page);
	} else {
		ut_ad(page_align(m_rec) == page);
		if (isUser()) {
			rec_init_offsets(m_rec, m_index, m_offsets);
			return;
		}
	}

	adjustSentinelOffsets();
}

/** Insert the record that the page cursor is pointing to, to another page.
@param[in/out] rec	record after which to insert
@return	pointer to record if enough space available, NULL otherwise */
UNIV_INTERN
rec_t*
PageCur::insert(rec_t* current) const
{
	byte*		insert_buf;
	ulint		extra_size;
	ulint		data_size;
	page_t*		page		= page_align(current);
	rec_t*		last_insert;	/*!< cursor position at previous
					insert */
	ulint		heap_no;	/*!< heap number of the inserted
					record */

	if (const ulint* offsets = getOffsets()) {
		extra_size = rec_offs_extra_size(offsets);
		data_size = rec_offs_data_size(offsets);
	} else {
		ut_ad(!dict_table_is_comp(m_index->table));
		ut_ad(!page_is_comp(page));
		data_size = rec_get_size_old(m_rec, extra_size);
	}

	ut_ad(page != page_align(m_rec));
	ut_ad(!m_mtr
	      || mtr_memo_contains_page(m_mtr, page, MTR_MEMO_PAGE_X_FIX));
	ut_ad(fil_page_get_type(page) == FIL_PAGE_INDEX);
	ut_ad(mach_read_from_8(page + PAGE_HEADER + PAGE_INDEX_ID)
	      == m_index->id || recv_recovery_is_on()
	      || (m_mtr && m_mtr->inside_ibuf));
	ut_ad(!page_rec_is_supremum(current));

	const ulint rec_size = data_size + extra_size;

	UNIV_MEM_ASSERT_RW(m_rec - extra_size, rec_size);

	if (rec_t* free_rec = page_header_get_ptr(page, PAGE_FREE)) {
		/* Try to allocate from the head of the free list. */
		ulint	free_extra_size;
		ulint	free_data_size = getOffsets()
			? rec_get_size_comp(free_rec, m_index, free_extra_size)
			: rec_get_size_old(free_rec, free_extra_size);

		if (free_data_size + free_extra_size < rec_size) {
			goto use_heap;
		}

		insert_buf = free_rec - free_extra_size;
		heap_no = page_rec_get_heap_no(free_rec);
		page_mem_alloc_free(page, NULL, page_rec_get_next(free_rec),
				    rec_size);
	} else {
use_heap:
		insert_buf = page_mem_alloc_heap(
			page, NULL, rec_size, &heap_no);

		if (UNIV_UNLIKELY(insert_buf == NULL)) {
			return(NULL);
		}
	}

	memcpy(insert_buf, m_rec - extra_size, rec_size);

	rec_t*	insert_rec = insert_buf + extra_size;
	/* next record after current before the insertion */
	rec_t*	next_rec = page_rec_get_next(current);

	ut_ad(current != insert_rec);

#ifdef UNIV_DEBUG
	if (page_is_comp(page)) {
		ut_ad(rec_get_status(current) <= REC_STATUS_INFIMUM);
		ut_ad(rec_get_status(insert_rec) < REC_STATUS_INFIMUM);
		ut_ad(rec_get_status(next_rec) != REC_STATUS_INFIMUM);
	}
#endif
	page_rec_set_next(insert_rec, next_rec);
	page_rec_set_next(current, insert_rec);

	page_header_set_field(page, NULL, PAGE_N_RECS,
			      1 + page_get_n_recs(page));

	if (page_is_comp(page)) {
		rec_set_n_owned_new(insert_rec, NULL, 0);
		rec_set_heap_no_new(insert_rec, heap_no);
	} else {
		rec_set_n_owned_old(insert_rec, 0);
		rec_set_heap_no_old(insert_rec, heap_no);
	}

	/* Update the last insertion info in page header */

	last_insert = page_header_get_ptr(page, PAGE_LAST_INSERT);
	ut_ad(!last_insert || !page_is_comp(page)
	      || rec_get_node_ptr_flag(last_insert)
	      == rec_get_node_ptr_flag(insert_rec));

	if (UNIV_UNLIKELY(last_insert == NULL)) {
		page_header_set_field(page, NULL, PAGE_DIRECTION,
				      PAGE_NO_DIRECTION);
		page_header_set_field(page, NULL, PAGE_N_DIRECTION, 0);

	} else if (last_insert == current
		   && page_header_get_field(page, PAGE_DIRECTION)
		   != PAGE_LEFT) {

		page_header_set_field(page, NULL, PAGE_DIRECTION,
							PAGE_RIGHT);
		page_header_set_field(page, NULL, PAGE_N_DIRECTION,
				      page_header_get_field(
					      page, PAGE_N_DIRECTION) + 1);

	} else if ((page_rec_get_next(insert_rec) == last_insert)
		   && (page_header_get_field(page, PAGE_DIRECTION)
		       != PAGE_RIGHT)) {

		page_header_set_field(page, NULL, PAGE_DIRECTION,
							PAGE_LEFT);
		page_header_set_field(page, NULL, PAGE_N_DIRECTION,
				      page_header_get_field(
					      page, PAGE_N_DIRECTION) + 1);
	} else {
		page_header_set_field(page, NULL, PAGE_DIRECTION,
							PAGE_NO_DIRECTION);
		page_header_set_field(page, NULL, PAGE_N_DIRECTION, 0);
	}

	page_header_set_ptr(page, NULL, PAGE_LAST_INSERT, insert_rec);

	/* Update the owner record. */
	rec_t*	owner_rec	= page_rec_find_owner_rec(insert_rec);
	ulint	n_owned;
	if (page_is_comp(page)) {
		n_owned = rec_get_n_owned_new(owner_rec);
		rec_set_n_owned_new(owner_rec, NULL, n_owned + 1);
	} else {
		n_owned = rec_get_n_owned_old(owner_rec);
		rec_set_n_owned_old(owner_rec, n_owned + 1);
	}

	if (UNIV_UNLIKELY(n_owned == PAGE_DIR_SLOT_MAX_N_OWNED)) {
		page_dir_split_slot(
			page, NULL, page_dir_find_owner_slot(owner_rec));
	}

	if (m_mtr) {
		page_cur_insert_rec_write_log(insert_rec, rec_size,
					      current, m_index, m_mtr);
	}

	if (m_offsets) {
		/* TODO: do not modify const m_offsets */
		rec_offs_make_valid(insert_rec, m_index, m_offsets);
		btr_blob_dbg_add_rec(insert_rec, m_index, m_offsets,
				     "PageCur::insert");
	}

	return(insert_rec);
}

#ifdef PAGE_CUR_ADAPT
/** Try a search shortcut.
@param[in]	tuple		entry to search for
@param[in/out]	up_fields	matched fields in upper limit record
@param[in/out]	up_bytes	matched bytes in upper limit record
@param[in/out]	low_fields	matched fields in lower limit record
@param[in/out]	low_bytes	matched bytes in lower limit record
@return	whether tuple matches the current record */
inline
bool
PageCur::searchShortcut(
	const dtuple_t*	tuple,
	ulint&		up_fields,
	ulint&		up_bytes,
	ulint&		low_fields,
	ulint&		low_bytes)
{
	ulint		low_match_f;
	ulint		low_match_b;
	ulint		up_match_f;
	ulint		up_match_b;

	ut_ad(dtuple_check_typed(tuple));
	ut_ad(dtuple_get_n_fields(tuple) <= getNumFields());
	ut_ad(isUser());

	ut_pair_min(&low_match_f, &low_match_b,
		    low_fields, low_bytes, up_fields, up_bytes);

	up_match_f = low_match_f;
	up_match_b = low_match_b;

	if (page_cmp_dtuple_rec_with_match(tuple, getRec(), getOffsets(),
					   &low_match_f, &low_match_b) < 0) {
		return(false);
	}

	const rec_t*	rec = getRec();
	if (next()) {
		bool match = page_cmp_dtuple_rec_with_match(
			tuple, getRec(), getOffsets(),
			&up_match_f, &up_match_b) >= 0;
		setRec(rec);
		if (match) {
			return(false);
		} else {
			up_fields = up_match_f;
			up_bytes = up_match_b;
		}
	} else {
		setRec(rec);
		low_fields = low_match_f;
		low_bytes = low_match_b;
	}

# ifdef UNIV_SEARCH_PERF_STAT
	page_cur_short_succ++;
# endif
	return(true);
}
#endif /* PAGE_CUR_ADAPT */

/** Search for an entry in the index page.
@param entry data tuple to search for
@return true if a full match was found */
UNIV_INTERN
bool
PageCur::search(const dtuple_t* tuple)
{
	const ulint	mode	= PAGE_CUR_LE;//TODO: take as parameter
	const page_t*	page	= getPage();
	/* TODO: if mode PAGE_CUR_G is specified, we are trying to position the
	cursor to answer a query of the form "tuple < X", where tuple is
	the input parameter, and X denotes an arbitrary physical record on
	the page. We want to position the cursor on the first X which
	satisfies the condition. */
	ulint		up_match_f	= 0;
	ulint		up_match_b	= 0;
	ulint		low_match_f	= 0;
	ulint		low_match_b	= 0;

	ut_ad(dtuple_validate(tuple));
	ut_ad(dtuple_check_typed(tuple));
	ut_ad(dtuple_get_n_fields(tuple) <= getNumFields());
	ut_ad(!m_mtr
	      || mtr_memo_contains(m_mtr, m_block, MTR_MEMO_PAGE_X_FIX)
	      || mtr_memo_contains(m_mtr, m_block, MTR_MEMO_PAGE_S_FIX));

	page_zip_validate_if_zip(getPageZip(), page, m_index);

	page_check_dir(page);

#ifdef PAGE_CUR_ADAPT
	if (page_is_leaf(page)
	    && mode == PAGE_CUR_LE
	    && page_header_get_field(page, PAGE_N_DIRECTION) > 3
	    && page_header_get_field(page, PAGE_DIRECTION) == PAGE_RIGHT) {
		if (const rec_t* rec
		    = page_header_get_ptr(page, PAGE_LAST_INSERT)) {
			setRec(rec);

			if (searchShortcut(tuple,
					   up_match_f, up_match_b,
					   low_match_f, low_match_b)) {
				return(low_match_f
				       == dtuple_get_n_fields(tuple));
			}
		}
	}
#endif

	/* Perform binary search until the lower and upper limit directory
	slots come to the distance 1 of each other */
	ulint	low	= 0;
	ulint	up	= page_dir_get_n_slots(page) - 1;

	while (up - low > 1) {
		ulint			mid	= (low + up) / 2;
		const page_dir_slot_t*	slot	= page_dir_get_nth_slot(
			page, mid);
		ulint			cur_match_f;
		ulint			cur_match_b;

		ut_pair_min(&cur_match_f, &cur_match_b,
			    low_match_f, low_match_b,
			    up_match_f, up_match_b);

		setRec(page_dir_slot_get_rec(slot));
		int cmp = cmp_dtuple_rec_with_match(
			tuple, getRec(), getOffsets(),
			&cur_match_f, &cur_match_b);
		if (cmp > 0) {
low_slot_match:
			low = mid;
			low_match_f = cur_match_f;
			low_match_b = cur_match_b;
		} else if (cmp) {
up_slot_match:
			up = mid;
			up_match_f = cur_match_f;
			up_match_b = cur_match_b;
		} else if (mode == PAGE_CUR_G || mode == PAGE_CUR_LE) {
			goto low_slot_match;
		} else {
			goto up_slot_match;
		}
	}

	/* Perform linear search until the upper and lower records come to
	distance 1 of each other. */

	const rec_t*	low_rec	= page_dir_slot_get_rec(
		page_dir_get_nth_slot(page, low));
	const rec_t*	up_rec	= page_dir_slot_get_rec(
		page_dir_get_nth_slot(page, up));

	setRec(low_rec);

	for (;;) {
		ulint	cur_match_f;
		ulint	cur_match_b;

		if (!next() || getRec() == up_rec) {
			ut_ad(getRec() == up_rec);
			setRec(low_rec);
			break;
		}

		ut_pair_min(&cur_match_f, &cur_match_b,
			    low_match_f, low_match_b,
			    up_match_f, up_match_b);

		int cmp = cmp_dtuple_rec_with_match(
			tuple, getRec(), getOffsets(),
			&cur_match_f, &cur_match_b);
		if (cmp > 0) {
low_rec_match:
			low_rec = getRec();
			low_match_f = cur_match_f;
			low_match_b = cur_match_b;
		} else if (cmp) {
up_rec_match:
			up_match_f = cur_match_f;
			up_match_b = cur_match_b;
			if (mode > PAGE_CUR_GE) {
				setRec(low_rec);
			}
			break;
		} else if (mode == PAGE_CUR_G || mode == PAGE_CUR_LE) {
			goto low_rec_match;
		} else {
			goto up_rec_match;
		}
	}

	return(low_match_f == dtuple_get_n_fields(tuple));
}

/** Delete the current record.
The cursor is moved to the next record after the deleted one.
@return true if the next record is a user record */
UNIV_INTERN
bool
PageCur::purge()
{
	buf_block_t*	block	= const_cast<buf_block_t*>(m_block);
	page_t*		page	= buf_block_get_frame(block);
	page_zip_des_t*	page_zip= buf_block_get_page_zip(block);

	ut_ad(isUser());
	ut_ad(rec_offs_validate(m_rec, m_index, m_offsets));
	ut_ad(!!page_is_comp(page) == dict_table_is_comp(m_index->table));
	ut_ad(fil_page_get_type(page) == FIL_PAGE_INDEX);
	ut_ad(mach_read_from_8(page + PAGE_HEADER + PAGE_INDEX_ID)
	      == m_index->id || recv_recovery_is_on()
	      || (m_mtr && m_mtr->inside_ibuf));

	/* page_zip_validate() will fail here when
	btr_cur_pessimistic_delete() invokes btr_set_min_rec_mark().
	Then, both "page_zip" and "page" would have the min-rec-mark
	set on the smallest user record, but "page" would additionally
	have it set on the smallest-but-one record.  Because sloppy
	page_zip_validate_low() only ignores min-rec-flag differences
	in the smallest user record, it cannot be used here either. */

	if (page_get_n_recs(page) == 1) {
		/* Empty the page. */
		ut_ad(page_is_leaf(page));
		ut_ad(m_mtr);
		/* Usually, this should be the root page,
		and the whole index tree should become empty.
		However, this could also be a call in
		btr_cur_pessimistic_update() to delete the only
		record in the page and to insert another one. */
		ut_ad(!next());
		setRec(page_get_supremum_rec(page));
		page_create_empty(block,
				  const_cast<dict_index_t*>(m_index), m_mtr);
		return(false);
	}

	ulint		slot_no	= page_dir_find_owner_slot(m_rec);
	page_dir_slot_t*slot	= page_dir_get_nth_slot(page, slot_no);
	page_dir_slot_t*prev_sl	= page_dir_get_nth_slot(page, slot_no - 1);
	ulint		n_owned	= page_dir_slot_get_n_owned(slot);

	ut_ad(n_owned > 1);
	ut_ad(slot_no > 0);

	/* 0. Write the log record */
	if (m_mtr) {
		page_cur_delete_rec_write_log(m_rec, m_index, m_mtr);
	}

	/* 1. Reset the last insert info in the page header and increment
	the modify clock for the frame */

	page_header_set_ptr(page, page_zip, PAGE_LAST_INSERT, NULL);

	/* The page gets invalid for optimistic searches: increment the
	frame modify clock only if there is an mini-transaction covering
	the change. During IMPORT we allocate local blocks that are not
	part of the buffer pool. */

	if (m_mtr) {
		buf_block_modify_clock_inc(const_cast<buf_block_t*>(m_block));
	}

	/* 2. Find the next and the previous record. Note that the cursor is
	left at the next record. */

	const rec_t*	prev_rec	= NULL;
	const rec_t*	rec;

	/* Find the immediate predecessor of m_rec. */
	for (rec = page_dir_slot_get_rec(prev_sl); rec != m_rec;
	     rec = page_rec_get_next_const(rec)) {
		prev_rec = rec;
	}

	rec_t*	del_rec	= const_cast<rec_t*>(m_rec);
	ulint	data_size;
	ulint	extra_size;
	ulint	n_ext;

	if (isComp()) {
		data_size = rec_offs_data_size(m_offsets);
		extra_size = rec_offs_extra_size(m_offsets);
		n_ext = rec_offs_n_extern(m_offsets);
	} else {
		data_size = rec_get_size_old(m_rec, extra_size);
		/* This is only needed for page_zip. */
		n_ext = 0;
		ut_ad(!page_zip);
	}

	btr_blob_dbg_remove_rec(del_rec, const_cast<dict_index_t*>(m_index),
				m_offsets, "PageCur::purge");

	next();

	/* 3. Remove the record from the linked list of records */

	page_rec_set_next(const_cast<rec_t*>(prev_rec), m_rec);

	/* 4. If the deleted record is pointed to by a dir slot, update the
	record pointer in slot. In the following if-clause we assume that
	prev_rec is owned by the same slot, i.e., PAGE_DIR_SLOT_MIN_N_OWNED
	>= 2. */

#if PAGE_DIR_SLOT_MIN_N_OWNED < 2
# error "PAGE_DIR_SLOT_MIN_N_OWNED < 2"
#endif
	if (del_rec == page_dir_slot_get_rec(slot)) {
		page_dir_slot_set_rec(slot, prev_rec);
	}

	/* 5. Update the number of owned records of the slot */

	page_dir_slot_set_n_owned(slot, page_zip, n_owned - 1);

	/* 6. Free the memory occupied by the record */
	page_mem_free(page, page_zip, del_rec, m_index,
		      data_size, extra_size, n_ext);

	/* 7. Now we have decremented the number of owned records of the slot.
	If the number drops below PAGE_DIR_SLOT_MIN_N_OWNED, we balance the
	slots. */

	if (n_owned <= PAGE_DIR_SLOT_MIN_N_OWNED) {
		page_dir_balance_slot(page, page_zip, slot_no);
	}

	page_zip_validate_if_zip(page_zip, page, m_index);

	return(!isAfterLast());
}

/** Reorganize the page. The cursor position will be adjusted.

NOTE: m_mtr must not be NULL.

IMPORTANT: On success, the caller will have to update
IBUF_BITMAP_FREE if this is a compressed leaf page in a
secondary index. This has to be done either within m_mtr, or
by invoking ibuf_reset_free_bits() before
mtr_commit(m_mtr). On uncompressed pages, IBUF_BITMAP_FREE is
unaffected by reorganization.

@param[in] recovery true if called in recovery:
locks and adaptive hash index will not be updated on recovery
@param[in] z_level	compression level, for compressed pages
@retval true if the operation was successful
@retval false if it is a compressed page, and recompression failed */
UNIV_INTERN
bool
PageCur::reorganize(bool recovery, ulint z_level)
{
	buf_block_t*	block		= const_cast<buf_block_t*>(m_block);
#ifndef UNIV_HOTBACKUP
	buf_pool_t*	buf_pool	= buf_pool_from_bpage(&block->page);
#endif /* !UNIV_HOTBACKUP */
	page_t*		page		= buf_block_get_frame(block);
	page_zip_des_t*	page_zip	= buf_block_get_page_zip(block);
	buf_block_t*	temp_block;
	page_t*		temp_page;
	ulint		log_mode;
	ulint		data_size1;
	ulint		data_size2;
	ulint		max_ins_size1;
	ulint		max_ins_size2;
	bool		success		= false;
	ulint		pos;
	bool		log_compressed;

	ut_ad(m_mtr);
	ut_ad(mtr_memo_contains(m_mtr, m_block, MTR_MEMO_PAGE_X_FIX));
	page_zip_validate_if_zip(page_zip, page, m_index);
	data_size1 = page_get_data_size(page);
	max_ins_size1 = page_get_max_insert_size_after_reorganize(page, 1);

	/* Turn logging off */
	log_mode = mtr_set_log_mode(m_mtr, MTR_LOG_NONE);

#ifndef UNIV_HOTBACKUP
	temp_block = buf_block_alloc(buf_pool);
#else /* !UNIV_HOTBACKUP */
	ut_ad(block == back_block1);
	temp_block = back_block2;
#endif /* !UNIV_HOTBACKUP */
	temp_page = temp_block->frame;

	/* Copy the old page to temporary space */
	buf_frame_copy(temp_page, page);

#ifndef UNIV_HOTBACKUP
	if (!recovery) {
		btr_search_drop_page_hash_index(block);
	}

	block->check_index_page_at_flush = TRUE;
#endif /* !UNIV_HOTBACKUP */
	btr_blob_dbg_remove(page, const_cast<dict_index_t*>(m_index),
			    "PageCur::reorganize");

	/* Save the cursor position. */
	pos = page_rec_get_n_recs_before(m_rec);

	/* Recreate the page: note that global data on page (possible
	segment headers, next page-field, etc.) is preserved intact */

	page_create(block, m_mtr, isComp());

	/* Copy the records from the temporary space to the recreated page;
	do not copy the lock bits yet */

	page_copy_rec_list_end_no_locks(block, temp_block,
					page_get_infimum_rec(temp_page),
					m_index, m_mtr);

	/* Multiple transactions cannot simultaneously operate on the
	same temp-table in parallel.
	max_trx_id is ignored for temp tables because it not required
	for MVCC. */
	if (dict_index_is_sec_or_ibuf(m_index)
	    && page_is_leaf(page)
	    && !dict_table_is_temporary(m_index->table)) {
		/* Copy max trx id to recreated page */
		trx_id_t	max_trx_id = page_get_max_trx_id(temp_page);
		page_set_max_trx_id(block, NULL, max_trx_id, m_mtr);
		/* In crash recovery, dict_index_is_sec_or_ibuf() always
		holds, even for clustered indexes.  max_trx_id is
		unused in clustered index pages. */
		ut_ad(max_trx_id != 0 || recovery);
	}

	/* If innodb_log_compressed_pages is ON, page reorganize should log the
	compressed page image.*/
	log_compressed = page_zip && page_zip_log_pages;

	if (log_compressed) {
		mtr_set_log_mode(m_mtr, log_mode);
	}

	if (page_zip
	    && !page_zip_compress(page_zip, page, m_index, z_level, m_mtr)) {

		/* Restore the old page and exit. */
		btr_blob_dbg_restore(page, temp_page,
				     const_cast<dict_index_t*>(m_index),
				     "PageCur::reorganize fail");

#if defined UNIV_DEBUG || defined UNIV_ZIP_DEBUG
		/* Check that the bytes that we skip are identical. */
		ut_a(!memcmp(page, temp_page, PAGE_HEADER));
		ut_a(!memcmp(PAGE_HEADER + PAGE_N_RECS + page,
			     PAGE_HEADER + PAGE_N_RECS + temp_page,
			     PAGE_DATA - (PAGE_HEADER + PAGE_N_RECS)));
		ut_a(!memcmp(UNIV_PAGE_SIZE - FIL_PAGE_DATA_END + page,
			     UNIV_PAGE_SIZE - FIL_PAGE_DATA_END + temp_page,
			     FIL_PAGE_DATA_END));
#endif /* UNIV_DEBUG || UNIV_ZIP_DEBUG */

		memcpy(PAGE_HEADER + page, PAGE_HEADER + temp_page,
		       PAGE_N_RECS - PAGE_N_DIR_SLOTS);
		memcpy(PAGE_DATA + page, PAGE_DATA + temp_page,
		       UNIV_PAGE_SIZE - PAGE_DATA - FIL_PAGE_DATA_END);

#if defined UNIV_DEBUG || defined UNIV_ZIP_DEBUG
		ut_a(!memcmp(page, temp_page, UNIV_PAGE_SIZE));
#endif /* UNIV_DEBUG || UNIV_ZIP_DEBUG */

		goto func_exit;
	}

#ifndef UNIV_HOTBACKUP
	if (!recovery) {
		/* Update the record lock bitmaps */
		lock_move_reorganize_page(block, temp_block);
	}
#endif /* !UNIV_HOTBACKUP */

	data_size2 = page_get_data_size(page);
	max_ins_size2 = page_get_max_insert_size_after_reorganize(page, 1);

	if (data_size1 != data_size2 || max_ins_size1 != max_ins_size2) {
		buf_page_print(page, 0, BUF_PAGE_PRINT_NO_CRASH);
		buf_page_print(temp_page, 0, BUF_PAGE_PRINT_NO_CRASH);

		fprintf(stderr,
			"InnoDB: Error: page old data size %lu"
			" new data size %lu\n"
			"InnoDB: Error: page old max ins size %lu"
			" new max ins size %lu\n"
			"InnoDB: Submit a detailed bug report"
			" to http://bugs.mysql.com\n",
			(unsigned long) data_size1, (unsigned long) data_size2,
			(unsigned long) max_ins_size1,
			(unsigned long) max_ins_size2);
		ut_ad(0);
	} else {
		success = true;
	}

	/* Restore the cursor position. */
	if (pos > 0) {
		setRec(page_rec_get_nth(page, pos));
	} else {
		ut_ad(isBeforeFirst());
	}

func_exit:
	page_zip_validate_if_zip(getPageZip(), getPage(), m_index);
	mtr_set_log_mode(m_mtr, log_mode);

#ifndef UNIV_HOTBACKUP
	buf_block_free(temp_block);

	if (success) {
		byte	type;
		byte*	log_ptr;

		/* Write the log record */
		if (page_zip) {
			ut_ad(page_is_comp(page));
			type = MLOG_ZIP_PAGE_REORGANIZE;
		} else if (page_is_comp(page)) {
			type = MLOG_COMP_PAGE_REORGANIZE;
		} else {
			type = MLOG_PAGE_REORGANIZE;
		}

		log_ptr = log_compressed
			? NULL
			: mlog_open_and_write_index(
				m_mtr, page, m_index, type,
				page_zip ? 1 : 0);

		/* For compressed pages write the compression level. */
		if (log_ptr && page_zip) {
			mach_write_to_1(log_ptr, z_level);
			mlog_close(m_mtr, log_ptr + 1);
		}
	}
#endif /* !UNIV_HOTBACKUP */

	return(success);
}

#ifdef UNIV_COMPILE_TEST_FUNCS

/*******************************************************************//**
Print the first n numbers, generated by page_cur_lcg_prng() to make sure
(visually) that it works properly. */
void
test_page_cur_lcg_prng(
/*===================*/
	int	n)	/*!< in: print first n numbers */
{
	int			i;
	unsigned long long	rnd;

	for (i = 0; i < n; i++) {
		rnd = page_cur_lcg_prng();
		printf("%llu\t%%2=%llu %%3=%llu %%5=%llu %%7=%llu %%11=%llu\n",
		       rnd,
		       rnd % 2,
		       rnd % 3,
		       rnd % 5,
		       rnd % 7,
		       rnd % 11);
	}
}

#endif /* UNIV_COMPILE_TEST_FUNCS */
