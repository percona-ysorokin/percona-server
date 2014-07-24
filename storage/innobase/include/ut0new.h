/*****************************************************************************

Copyright (c) 2014, 2014, Oracle and/or its affiliates. All Rights Reserved.

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

/**************************************************//**
@file ut/ut0new.h
Instrumented memory allocator.

Created May 26, 2014 Vasil Dimov
*******************************************************/

/** Dynamic memory allocation within InnoDB guidelines.
All dynamic (heap) memory allocations (malloc(3), strdup(3), etc, "new",
various std:: containers that allocate memory internally), that are done
within InnoDB are instrumented. This means that InnoDB uses a custom set
of functions for allocating memory, rather than calling e.g. "new" directly.

Here follows a cheat sheet on what InnoDB functions to use whenever a
standard one would have been used.

Creating new objects with "new":
--------------------------------
Standard:
  new expression
  or
  new(std::nothrow) expression
InnoDB, default instrumentation:
  UT_NEW_NOKEY(expression)
InnoDB, custom instrumentation, preferred:
  UT_NEW(expression, key)

Destroying objects, created with "new":
---------------------------------------
Standard:
  delete ptr
InnoDB:
  UT_DELETE(ptr)

Creating new arrays with "new[]":
---------------------------------
Standard:
  new type[num]
  or
  new(std::nothrow) type[num]
InnoDB, default instrumentation:
  UT_NEW_ARRAY_NOKEY(type, num)
InnoDB, custom instrumentation, preferred:
  UT_NEW_ARRAY(type, num, key)

Destroying arrays, created with "new[]":
----------------------------------------
Standard:
  delete[] ptr
InnoDB:
  UT_DELETE_ARRAY(ptr)

Declaring a type with a std:: container, e.g. std::vector:
----------------------------------------------------------
Standard:
  std::vector<t>
InnoDB:
  std::vector<t, ut_allocator<t> >

Declaring objects of some std:: type:
-------------------------------------
Standard:
  std::vector<t> v
InnoDB, default instrumentation:
  std::vector<t, ut_allocator<t> > v
InnoDB, custom instrumentation, preferred:
  std::vector<t, ut_allocator<t> > v(ut_allocator<t>(key))

Raw block allocation (as usual in C++, consider whether using "new" would
not be more appropriate):
-------------------------------------------------------------------------
Standard:
  malloc(num)
InnoDB, default instrumentation:
  ut_malloc_nokey(num)
InnoDB, custom instrumentation, preferred:
  ut_malloc(num, key)

Raw block resize:
-----------------
Standard:
  realloc(ptr, new_size)
InnoDB:
  ut_realloc(ptr, new_size)

Raw block deallocation:
-----------------------
Standard:
  free(ptr)
InnoDB:
  ut_free(ptr)

Note: the expression passed to UT_NEW() or UT_NEW_NOKEY() must always end
with (), thus:
Standard:
  new int
InnoDB:
  UT_NEW_NOKEY(int())
*/

#ifndef ut0new_h
#define ut0new_h

#include <limits> /* std::numeric_limits */
#include <map> /* std::map */

#include <stddef.h>
#include <stdlib.h> /* malloc() */
#include <string.h> /* strlen(), strrchr(), strncmp() */

#include "my_global.h" /* needed for headers from mysql/psi/ */
#include "mysql/psi/mysql_memory.h" /* PSI_MEMORY_CALL() */
#include "mysql/psi/psi_memory.h" /* PSI_memory_key, PSI_memory_info */

#include "univ.i"

#include "os0proc.h" /* os_mem_alloc_large() */
#include "os0thread.h" /* os_thread_sleep() */
#include "ut0mem.h" /* OUT_OF_MEMORY_MSG */
#include "ut0ut.h" /* ut_strcmp_functor */

/** Keys for registering allocations with performance schema.
Pointers to these variables are supplied to PFS code via the pfs_info[]
array and the PFS code initializes them via PSI_MEMORY_CALL(register_memory)().
mem_key_other and mem_key_std are special in the following way (see also
ut_allocator::get_mem_key()):
* If the caller has not provided a key and the file name of the caller is
  unknown, then mem_key_std will be used. This happens only when called from
  within std::* containers.
* If the caller has not provided a key and the file name of the caller is
  known, but is not amongst the listed names in auto_names[] (see ut_new_boot())
  then mem_key_other will be used. Generally this should not happen and if it
  happens then that means that the list in auto_names[] needs to be extended.
Keep this list alphabetically sorted. */
extern PSI_memory_key	mem_key_buf_buf_pool;
extern PSI_memory_key	mem_key_dict_stats_index_map_t;
extern PSI_memory_key	mem_key_dict_stats_n_diff_on_level;
extern PSI_memory_key	mem_key_other;
extern PSI_memory_key	mem_key_row_log_buf;
extern PSI_memory_key	mem_key_row_merge_sort;
extern PSI_memory_key	mem_key_std;
extern PSI_memory_key	mem_key_sync_debug_latches;
extern PSI_memory_key	mem_key_trx_sys_t_rw_trx_ids;

/** Setup the internal objects needed for UT_NEW() to operate.
This must be called before the first call to UT_NEW(). */
void
ut_new_boot();

/** Retrieve a memory key (registered with PFS), given a portion of the file
name of the caller.
@param[in]	file	portion of the filename - basename without an extension
@return registered memory key or PSI_NOT_INSTRUMENTED if not found */
PSI_memory_key
ut_new_get_key_by_file(
	const char*	file);

#ifdef ut0new_cc

/* Below are the declarations of mem_key_* and pfs_info[]
which only need to go in ut0new.cc, but we have put them here to
make editing easier - avoid having to edit them in both
ut0new.h and ut0new.cc */

/** Keys for registering allocations with performance schema.
Keep this list alphabetically sorted. */
PSI_memory_key	mem_key_buf_buf_pool;
PSI_memory_key	mem_key_dict_stats_index_map_t;
PSI_memory_key	mem_key_dict_stats_n_diff_on_level;
PSI_memory_key	mem_key_other;
PSI_memory_key	mem_key_row_log_buf;
PSI_memory_key	mem_key_row_merge_sort;
PSI_memory_key	mem_key_std;
PSI_memory_key	mem_key_sync_debug_latches;
PSI_memory_key	mem_key_trx_sys_t_rw_trx_ids;

/** Auxiliary array of performance schema 'PSI_memory_info'.
Each allocation appears in
performance_schema.memory_summary_global_by_event_name (and alike) in the form
of e.g. 'memory/innodb/NAME' where the last component NAME is picked from
the list below:
1. If key is specified, then the respective name is used
2. Without a specified key, allocations from inside std::* containers use
   mem_key_std
3. Without a specified key, allocations from outside std::* pick up the key
   based on the file name, and if file name is not found in auto_names[] (in
   ut_new_boot()) then mem_key_other is used.
NOTE: keep this list alphabetically sorted. */
PSI_memory_info	pfs_info[] = {
	{&mem_key_buf_buf_pool, "buf_buf_pool", 0},
	{&mem_key_dict_stats_index_map_t, "dict_stats_index_map_t", 0},
	{&mem_key_dict_stats_n_diff_on_level, "dict_stats_n_diff_on_level", 0},
	{&mem_key_other, "other", 0},
	{&mem_key_row_log_buf, "row_log_buf", 0},
	{&mem_key_row_merge_sort, "row_merge_sort", 0},
	{&mem_key_std, "std", 0},
	{&mem_key_sync_debug_latches, "sync_debug_latches", 0},
	{&mem_key_trx_sys_t_rw_trx_ids, "trx_sys_t::rw_trx_ids", 0},
};

#ifdef UNIV_PFS_MEMORY

/** Map used for default performance schema keys, based on file name of the
caller. The key is the file name of the caller and the value is a pointer
to a PSI_memory_key variable to be passed to performance schema methods.
We use ut_strcmp_functor because by default std::map will compare the pointers
themselves (cont char*) and not do strcmp(). */
typedef std::map<const char*, PSI_memory_key*, ut_strcmp_functor>
	mem_keys_auto_t;

/** Map of filename/pfskey, used for tracing allocations that have not
provided a manually created pfs key. This map is only ever modified (bulk
insert) at startup in a single-threaded environment by ut_new_boot().
Later it is only read (only std::map::find() is called) from multithreaded
environment, thus it is not protected by any latch. */
static mem_keys_auto_t	mem_keys_auto;

#endif /* UNIV_PFS_MEMORY */

/** Setup the internal objects needed for UT_NEW() to operate.
This must be called before the first call to UT_NEW(). */
void
ut_new_boot()
{
#ifdef UNIV_PFS_MEMORY
	static const char*	auto_names[] = {
		/* Keep this list alphabetically sorted. */
		"api0api",
		"btr0btr",
		"btr0bulk",
		"btr0cur",
		"btr0pcur",
		"btr0sea",
		"buf0buf",
		"buf0dblwr",
		"buf0dump",
		"buf0flu",
		"buf0lru",
		"data0data",
		"dict0dict",
		"dict0mem",
		"dict0stats",
		"eval0eval",
		"fil0fil",
		"fsp0file",
		"fts0ast",
		"fts0config",
		"fts0fts",
		"fts0opt",
		"fts0pars",
		"fts0que",
		"fts0sql",
		"fts0types",
		"gis0sea",
		"ha0ha",
		"ha_innodb",
		"handler0alter",
		"hash0hash",
		"i_s",
		"ibuf0ibuf",
		"lexyy",
		"lock0lock",
		"log0log",
		"log0recv",
		"mem0mem",
		"os0file",
		"page0cur",
		"page0zip",
		"pars0lex",
		"read0read",
		"rem0rec",
		"row0ftsort",
		"row0import",
		"row0log",
		"row0merge",
		"row0mysql",
		"row0sel",
		"row0trunc",
		"srv0conc",
		"srv0srv",
		"srv0start",
		"sync0arr",
		"sync0debug",
		"sync0rw",
		"trx0i_s",
		"trx0purge",
		"trx0roll",
		"trx0rseg",
		"trx0sys",
		"trx0trx",
		"trx0undo",
		"usr0sess",
		"ut0bh",
		"ut0list",
		"ut0mem",
		"ut0pool",
		"ut0rbt",
		"ut0vec",
		"ut0wqueue",
	};
	static const size_t	n_auto_names = UT_ARR_SIZE(auto_names);
	static PSI_memory_key	auto_keys[n_auto_names];
	static PSI_memory_info	pfs_info_auto[n_auto_names];

	for (size_t i = 0; i < n_auto_names; i++) {
		const std::pair<mem_keys_auto_t::iterator, bool>	ret
			= mem_keys_auto.insert(
			mem_keys_auto_t::value_type(auto_names[i],
						    &auto_keys[i]));

		/* ret.second is true if new element has been inserted */
		ut_ad(ret.second);

		/* e.g. "btr0btr" */
		pfs_info_auto[i].m_name = auto_names[i];

		/* a pointer to the pfs key */
		pfs_info_auto[i].m_key = &auto_keys[i];

		pfs_info_auto[i].m_flags = 0;
	}

	PSI_MEMORY_CALL(register_memory)("innodb",
					 pfs_info,
					 UT_ARR_SIZE(pfs_info));
	PSI_MEMORY_CALL(register_memory)("innodb",
					 pfs_info_auto,
					 n_auto_names);
#endif /* UNIV_PFS_MEMORY */
}

/** Retrieve a memory key (registered with PFS), given a portion of the file
name of the caller.
@param[in]	file	portion of the filename - basename without an extension
@return registered memory key or PSI_NOT_INSTRUMENTED if not found */
PSI_memory_key
ut_new_get_key_by_file(
	const char*	file)
{
	mem_keys_auto_t::const_iterator	el = mem_keys_auto.find(file);

	if (el != mem_keys_auto.end()) {
		return(*(el->second));
	}

	return(PSI_NOT_INSTRUMENTED);
}

#endif /* ut0new_cc */

/** A structure that holds the necessary data for performance schema
accounting. An object of this type is put in front of each allocated block
of memory. This is because the data is needed even when freeing the memory. */
struct ut_new_pfx_t {

#ifdef UNIV_PFS_MEMORY

	/** Performance schema key. Assigned to a name at startup via
	PSI_MEMORY_CALL(register_memory)() and later used for accounting
	allocations and deallocations with
	PSI_MEMORY_CALL(memory_alloc)(key, size) and
	PSI_MEMORY_CALL(memory_free)(key, size). */
	PSI_memory_key	m_key;

#endif /* UNIV_PFS_MEMORY */

	/** Size of the allocated block in bytes, including this prepended
	aux structure. For example if InnoDB code requests to allocate
	100 bytes, and sizeof(ut_new_pfx_t) is 16, then 116 bytes are
	allocated in total and m_size will be 116. */
	size_t		m_size;
};

/** Allocator class for allocating memory from inside std::* containers. */
template <class T>
class ut_allocator {
public:
	typedef T*		pointer;
	typedef const T*	const_pointer;
	typedef T&		reference;
	typedef const T&	const_reference;
	typedef T		value_type;
	typedef size_t		size_type;
	typedef ptrdiff_t	difference_type;

	/** Default constructor. */
	explicit
	ut_allocator(
		PSI_memory_key	key = PSI_NOT_INSTRUMENTED)
		:
#ifdef UNIV_PFS_MEMORY
		m_key(key),
#endif /* UNIV_PFS_MEMORY */
		m_max_retries(60)
	{
	}

	/** Constructor from allocator of another type. */
	template <class U>
	ut_allocator(
		const ut_allocator<U>&	other)
		:
#ifdef UNIV_PFS_MEMORY
		m_key(other.get_mem_key(NULL)),
#endif /* UNIV_PFS_MEMORY */
		m_max_retries(60)
	{
	}

	/** Return the maximum number of objects that can be allocated by
	this allocator. */
	size_type
	max_size() const
	{
		const size_type	s_max = std::numeric_limits<size_type>::max();

#ifdef UNIV_PFS_MEMORY
		return((s_max - sizeof(ut_new_pfx_t)) / sizeof(T));
#else
		return(s_max / sizeof(T));
#endif /* UNIV_PFS_MEMORY */
	}

	/** Allocate a chunk of memory that can hold 'n_elements' objects of
	type 'T' and trace the allocation.
	If the allocation fails this method may throw an exception. This
	is mandated by the standard and if it returns NULL instead, then
	STL containers that use it (e.g. std::vector) may get confused.
	After successfull allocation the returned pointer must be passed
	to ut_allocator::deallocate() when no longer needed.
	@param[in]	n_elements	number of elements
	@param[in]	hint		pointer to a nearby memory location,
	unused by this implementation
	@param[in]	file		file name of the caller
	@param[in]	set_to_zero	if true, then the returned memory is
	initialized with 0x0 bytes.
	@return pointer to the allocated memory */
	pointer
	allocate(
		size_type	n_elements,
		const_pointer	hint = NULL,
		const char*	file = NULL,
		bool		set_to_zero = false,
		bool		throw_on_error = true)
	{
		if (n_elements == 0) {
			return(NULL);
		}

		if (n_elements > max_size()) {
			if (throw_on_error) {
				throw(std::bad_alloc());
			} else {
				return(NULL);
			}
		}

		void*			ptr;
		size_t			retries = 0;
		size_t			total_bytes = n_elements * sizeof(T);

#ifdef UNIV_PFS_MEMORY
		total_bytes += sizeof(ut_new_pfx_t);
#endif /* UNIV_PFS_MEMORY */

		do {
			if (set_to_zero) {
				ptr = calloc(1, total_bytes);
			} else {
				ptr = malloc(total_bytes);
			}

			if (ptr == NULL) {
				os_thread_sleep(1000000 /* 1 second */);
				retries++;
			}
		} while (ptr == NULL && retries < m_max_retries);

		if (ptr == NULL) {
			ib::fatal()
				<< "Cannot allocate " << total_bytes
				<< " bytes of memory after " << m_max_retries
				<< " retries over " << m_max_retries
				<< " seconds. OS error: " << strerror(errno)
				<< " (" << errno << "). " << OUT_OF_MEMORY_MSG;
			/* not reached */
			if (throw_on_error) {
				throw(std::bad_alloc());
			} else {
				return(NULL);
			}
		}

#ifdef UNIV_PFS_MEMORY
		ut_new_pfx_t*	pfx = static_cast<ut_new_pfx_t*>(ptr);

		allocate_trace(total_bytes, file, pfx);

		return(reinterpret_cast<pointer>(pfx + 1));
#else
		return(reinterpret_cast<pointer>(ptr));
#endif /* UNIV_PFS_MEMORY */
	}

	/** Free a memory allocated by allocate() and trace the deallocation.
	@param[in,out]	ptr		pointer to memory to free
	@param[in]	n_elements	number of elements allocated (unused) */
	void
	deallocate(
		void*		ptr,
		size_type	n_elements = 0)
	{
		if (ptr == NULL) {
			return;
		}

#ifdef UNIV_PFS_MEMORY
		ut_new_pfx_t*	pfx = static_cast<ut_new_pfx_t*>(ptr) - 1;

		deallocate_trace(pfx);

		free(pfx);
#else
		free(ptr);
#endif /* UNIV_PFS_MEMORY */
	}

	/** Create an object of type 'T' using the value 'val' over the
	memory pointed by 'p'. */
	void
	construct(
		pointer		p,
		const T&	val)
	{
		new(p) T(val);
	}

	/** Destroy an object pointed by 'p'. */
	void
	destroy(
		pointer	p)
	{
		p->~T();
	}

	/** Return the address of an object. */
	pointer
	address(
		reference	x) const
	{
		return(&x);
	}

	/** Return the address of a const object. */
	const_pointer
	address(
		const_reference	x) const
	{
		return(&x);
	}

	template <class U>
	struct rebind {
		typedef ut_allocator<U>	other;
	};

	/* The following are custom methods, not required by the standard. */

#ifdef UNIV_PFS_MEMORY

	/** realloc(3)-like method.
	The passed in ptr must have been returned by allocate() and the
	pointer returned by this method must be passed to deallocate() when
	no longer needed.
	@param[in,out]	ptr		old pointer to reallocate
	@param[in]	n_elements	new number of elements to allocate
	@param[in]	file		file name of the caller
	@return newly allocated memory */
	pointer
	reallocate(
		void*		ptr,
		size_type	n_elements,
		const char*	file)
	{
		if (n_elements == 0) {
			deallocate(ptr);
			return(NULL);
		}

		if (ptr == NULL) {
			return(allocate(n_elements, NULL, file, false, false));
		}

		if (n_elements > max_size()) {
			return(NULL);
		}

		ut_new_pfx_t*	pfx_old;
		ut_new_pfx_t*	pfx_new;
		size_t		retries = 0;
		size_t		total_bytes;

		pfx_old = reinterpret_cast<ut_new_pfx_t*>(ptr) - 1;

		total_bytes = n_elements * sizeof(T) + sizeof(ut_new_pfx_t);

		do {
			pfx_new = static_cast<ut_new_pfx_t*>(
				realloc(pfx_old, total_bytes));

			if (pfx_new == NULL) {
				os_thread_sleep(1000000 /* 1 second */);
				retries++;
			}
		} while (pfx_new == NULL && retries < m_max_retries);

		if (pfx_new == NULL) {
			ib::fatal()
				<< "Cannot reallocate " << total_bytes
				<< " bytes of memory after " << m_max_retries
				<< " retries over " << m_max_retries
				<< " seconds. OS error: " << strerror(errno)
				<< " (" << errno << "). " << OUT_OF_MEMORY_MSG;
			/* not reached */
			return(NULL);
		}

		deallocate_trace(pfx_new);

		allocate_trace(total_bytes, file, pfx_new);

		return(reinterpret_cast<pointer>(pfx_new + 1));
	}

	/** Allocate, trace the allocation and construct 'n_elements' objects
	of type 'T'. If the allocation fails or if some of the constructors
	throws an exception, then this method will return NULL. It does not
	throw exceptions. After successfull completion the returned pointer
	must be passed to delete_array() when no longer needed.
	@param[in]	n_elements	number of elements to allocate
	@param[in]	file		file name of the caller
	@return pointer to the first allocated object or NULL */
	pointer
	new_array(
		size_type	n_elements,
		const char*	file)
	{
		T*	p = allocate(n_elements, NULL, file, false, false);

		if (p == NULL) {
			return(NULL);
		}

		T*	first = p;

		try {
			for (size_type i = 0; i < n_elements; i++) {
				new(p) T;
				++p;
			}
		} catch (...) {
			for (--p; p != first; --p) {
				p->~T();
			}
			first->~T();

			deallocate(first);

			return(NULL);
		}

		return(first);
	}

	/** Destroy, deallocate and trace the deallocation of an array created
	by new_array().
	@param[in,out]	ptr	pointer to the first object in the array */
	void
	delete_array(
		T*	ptr)
	{
		if (ptr == NULL) {
			return;
		}

		const size_type	n_elements = n_elements_allocated(ptr);

		T*		p = ptr + n_elements - 1;

		for (size_type i = 0; i < n_elements; i++) {
			p->~T();
			--p;
		}

		deallocate(ptr);
	}

#endif /* UNIV_PFS_MEMORY */

	/** Allocate a large chunk of memory that can hold 'n_elements'
	objects of type 'T' and trace the allocation.
	@param[in]	n_elements	number of elements
	@param[out]	pfx		storage for the description of the
	allocated memory. The caller must provide space for this one and keep
	it until the memory is no longer needed and then pass it to
	deallocate_large().
	@return pointer to the allocated memory */
	pointer
	allocate_large(
		size_type	n_elements,
		ut_new_pfx_t*	pfx)
	{
		if (n_elements == 0) {
			return(NULL);
		}

		if (n_elements > max_size()) {
			return(NULL);
		}

		ulint	n_bytes = n_elements * sizeof(T);

		pointer	ptr = reinterpret_cast<pointer>(
			os_mem_alloc_large(&n_bytes));

#ifdef UNIV_PFS_MEMORY
		if (ptr != NULL) {
			allocate_trace(n_bytes, NULL, pfx);
		}
#else
		pfx->m_size = n_bytes;
#endif /* UNIV_PFS_MEMORY */

		return(ptr);
	}

	/** Free a memory allocated by allocate_large() and trace the
	deallocation.
	@param[in,out]	ptr	pointer to memory to free
	@param[in]	pfx	descriptor of the memory, as returned by
	allocate_large(). */
	void
	deallocate_large(
		void*			ptr,
		const ut_new_pfx_t*	pfx)
	{
#ifdef UNIV_PFS_MEMORY
		deallocate_trace(pfx);
#endif /* UNIV_PFS_MEMORY */

		os_mem_free_large(ptr, pfx->m_size);
	}

#ifdef UNIV_PFS_MEMORY

	/** Get the performance schema key to use for tracing allocations.
	@param[in]	file	file name of the caller or NULL if unknown
	@return performance schema key */
	PSI_memory_key
	get_mem_key(
		const char*	file) const
	{
		if (m_key != PSI_NOT_INSTRUMENTED) {
			return(m_key);
		}

		if (file == NULL) {
			return(mem_key_std);
		}

		/* Assuming 'file' contains something like the following,
		extract the file name without the extenstion out of it by
		setting 'beg' and 'len'.
		...mysql-trunk/storage/innobase/dict/dict0dict.cc:302
	                                             ^-- beg, len=9
		*/

		const char*	beg = strrchr(file, OS_PATH_SEPARATOR);

		if (beg == NULL) {
			beg = file;
		} else {
			beg++;
		}

		size_t		len = strlen(beg);

		const char*	end = strrchr(beg, '.');

		if (end != NULL) {
			len = end - beg;
		}

		/* e.g. "btr0btr", derived from ".../btr0btr.cc" */
		char		keyname[FILENAME_MAX];

		ut_ad(len < sizeof(keyname));

		memcpy(keyname, beg, len);
		keyname[len] = '\0';

		const PSI_memory_key	key = ut_new_get_key_by_file(keyname);

		if (key != PSI_NOT_INSTRUMENTED) {
			return(key);
		}

		return(mem_key_other);
	}

private:

	/** Retrieve the size of a memory block allocated by new_array().
	@param[in]	ptr	pointer returned by new_array().
	@return size of memory block */
	size_type
	n_elements_allocated(
		const_pointer	ptr)
	{
		const ut_new_pfx_t*	pfx;

		pfx = reinterpret_cast<const ut_new_pfx_t*>(ptr) - 1;

		const size_type	user_bytes = pfx->m_size - sizeof(ut_new_pfx_t);

		ut_ad(user_bytes % sizeof(T) == 0);

		return(user_bytes / sizeof(T));
	}

	/** Trace a memory allocation.
	After the accounting, the data needed for tracing the deallocation
	later is written into 'pfx'.
	The PFS event name is picked on the following criteria:
	1. If key (!= PSI_NOT_INSTRUMENTED) has been specified when constructing
	   this ut_allocator object, then the name associated with that key will
	   be used (this is the recommended approach for new code)
	2. Otherwise, if "file" is NULL, then the name associated with
	   mem_key_std will be used
	3. Otherwise, if an entry is found by ut_new_get_key_by_file(), that
	   corresponds to "file", that will be used (see ut_new_boot(),
	   auto_names[] and mem_keys_auto)
	4. Otherwise, the name associated with mem_key_other will be used.
	@param[in]	size	number of bytes that were allocated
	@param[in]	file	file name of the caller or NULL if unknown
	@param[out]	pfx	placeholder to store the info which will be
	needed when freeing the memory */
	void
	allocate_trace(
		size_t		size,
		const char*	file,
		ut_new_pfx_t*	pfx)
	{
		const PSI_memory_key	key = get_mem_key(file);

		pfx->m_key = PSI_MEMORY_CALL(memory_alloc)(key, size);
		pfx->m_size = size;
	}

	/** Trace a memory deallocation.
	@param[in]	pfx	info for the deallocation */
	void
	deallocate_trace(
		const ut_new_pfx_t*	pfx)
	{
		PSI_MEMORY_CALL(memory_free)(pfx->m_key, pfx->m_size);
	}

	/** Performance schema key. */
	const PSI_memory_key	m_key;

#endif /* UNIV_PFS_MEMORY */

private:

	/** Assignment operator, not used, thus disabled (private). */
	template <class U>
	void
	operator=(
		const ut_allocator<U>&);

	/** Maximum number of retries to allocate memory. */
	const size_t		m_max_retries;
};

/** Compare two allocators of the same type.
As long as the type of A1 and A2 is the same, a memory allocated by A1
could be freed by A2 even if the pfs mem key is different. */
template <typename T>
inline
bool
operator==(
	const ut_allocator<T>&	lhs,
	const ut_allocator<T>&	rhs)
{
	return(true);
}

/** Compare two allocators of the same type. */
template <typename T>
inline
bool
operator!=(
	const ut_allocator<T>&	lhs,
	const ut_allocator<T>&	rhs)
{
	return(!(lhs == rhs));
}

#ifdef UNIV_PFS_MEMORY

/** Allocate, trace the allocation and construct an object.
Use this macro instead of 'new' within InnoDB.
For example: instead of
	Foo*	f = new Foo(args);
use:
	Foo*	f = UT_NEW(Foo(args), mem_key_some);
Upon failure to allocate the memory, this macro may return NULL. It
will not throw exceptions. After successfull allocation the returned
pointer must be passed to UT_DELETE() when no longer needed.
@param[in]	expr	any expression that could follow "new"
@param[in]	key	performance schema memory tracing key
@return pointer to the created object or NULL */
#define UT_NEW(expr, key) \
	/* Placement new will return NULL and not attempt to construct an
	object if the passed in pointer is NULL, e.g. if allocate() has
	failed to allocate memory and has returned NULL. */ \
	::new(ut_allocator<byte>(key).allocate( \
		sizeof expr, NULL, __FILE__, false, false)) expr

/** Allocate, trace the allocation and construct an object.
Use this macro instead of 'new' within InnoDB and instead of UT_NEW()
when creating a dedicated memory key is not feasible.
For example: instead of
	Foo*	f = new Foo(args);
use:
	Foo*	f = UT_NEW_NOKEY(Foo(args));
Upon failure to allocate the memory, this macro may return NULL. It
will not throw exceptions. After successfull allocation the returned
pointer must be passed to UT_DELETE() when no longer needed.
@param[in]	expr	any expression that could follow "new"
@return pointer to the created object or NULL */
#define UT_NEW_NOKEY(expr)	UT_NEW(expr, PSI_NOT_INSTRUMENTED)

/** Destroy, deallocate and trace the deallocation of an object created by
UT_NEW() or UT_NEW_NOKEY().
We can't instantiate ut_allocator without having the type of the object, thus
we redirect this to a templated function. */
#define UT_DELETE(ptr)		ut_delete(ptr)

/** Destroy and account object created by UT_NEW() or UT_NEW_NOKEY().
@param[in,out]	ptr	pointer to the object */
template <typename T>
inline
void
ut_delete(
	T*	ptr)
{
	if (ptr == NULL) {
		return;
	}

	ut_allocator<T>	allocator;

	allocator.destroy(ptr);
	allocator.deallocate(ptr);
}

/** Allocate and account 'n_elements' objects of type 'type'.
Use this macro to allocate memory within InnoDB instead of 'new[]'.
The returned pointer must be passed to UT_DELETE_ARRAY().
@param[in]	type		type of objects being created
@param[in]	n_elements	number of objects to create
@param[in]	key		performance schema memory tracing key
@return pointer to the first allocated object or NULL */
#define UT_NEW_ARRAY(type, n_elements, key) \
	ut_allocator<type>(key).new_array(n_elements, __FILE__)

/** Allocate and account 'n_elements' objects of type 'type'.
Use this macro to allocate memory within InnoDB instead of 'new[]' and
instead of UT_NEW_ARRAY() when it is not feasible to create a dedicated key.
@param[in]	type		type of objects being created
@param[in]	n_elements	number of objects to create
@return pointer to the first allocated object or NULL */
#define UT_NEW_ARRAY_NOKEY(type, n_elements) \
	UT_NEW_ARRAY(type, n_elements, PSI_NOT_INSTRUMENTED)

/** Destroy, deallocate and trace the deallocation of an array created by
UT_NEW_ARRAY() or UT_NEW_ARRAY_NOKEY().
We can't instantiate ut_allocator without having the type of the object, thus
we redirect this to a templated function. */
#define UT_DELETE_ARRAY(ptr)	ut_delete_array(ptr)

/** Destroy and account objects created by UT_NEW_ARRAY() or
UT_NEW_ARRAY_NOKEY().
@param[in,out]	ptr	pointer to the first object in the array */
template <typename T>
inline
void
ut_delete_array(
	T*	ptr)
{
	ut_allocator<T>().delete_array(ptr);
}

#define ut_malloc(n_bytes, key)		static_cast<void*>( \
	ut_allocator<byte>(key).allocate( \
		n_bytes, NULL, __FILE__, false, false))

#define ut_zalloc(n_bytes, key)		static_cast<void*>( \
	ut_allocator<byte>(key).allocate( \
		n_bytes, NULL, __FILE__, true, false))

#define ut_malloc_nokey(n_bytes)	static_cast<void*>( \
	ut_allocator<byte>(PSI_NOT_INSTRUMENTED).allocate( \
		n_bytes, NULL, __FILE__, false, false))

#define ut_zalloc_nokey(n_bytes)	static_cast<void*>( \
	ut_allocator<byte>(PSI_NOT_INSTRUMENTED).allocate( \
		n_bytes, NULL, __FILE__, true, false))

#define ut_realloc(ptr, n_bytes)	static_cast<void*>( \
	ut_allocator<byte>(PSI_NOT_INSTRUMENTED).reallocate( \
		ptr, n_bytes, __FILE__))

#define ut_free(ptr)	ut_allocator<byte>(PSI_NOT_INSTRUMENTED).deallocate(ptr)

#else /* UNIV_PFS_MEMORY */

/* Fallbacks when memory tracing is disabled at compile time. */

#define UT_NEW(expr, key)		::new(std::nothrow) expr
#define UT_NEW_NOKEY(expr)		::new(std::nothrow) expr
#define UT_DELETE(ptr)			::delete ptr

#define UT_NEW_ARRAY(type, n_elements, key) \
	::new(std::nothrow) type[n_elements]

#define UT_NEW_ARRAY_NOKEY(type, n_elements) \
	::new(std::nothrow) type[n_elements]

#define UT_DELETE_ARRAY(ptr)		::delete[] ptr

#define ut_malloc(n_bytes, key)		::malloc(n_bytes)

#define ut_zalloc(n_bytes, key)		::calloc(1, n_bytes)

#define ut_malloc_nokey(n_bytes)	::malloc(n_bytes)

#define ut_zalloc_nokey(n_bytes)	::calloc(1, n_bytes)

#define ut_realloc(ptr, n_bytes)	::realloc(ptr, n_bytes)

#define ut_free(ptr)			::free(ptr)

#endif /* UNIV_PFS_MEMORY */

#endif /* ut0new_h */
