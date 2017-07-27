/*****************************************************************************

Copyright (c) 2017, Oracle and/or its affiliates. All Rights Reserved.

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

/** @file dict/dict0dd.cc
Data dictionary interface */

#include <current_thd.h>
#include <sql_thd_internal_api.h>

#include "dict0dd.h"
#include "dict0dict.h"
#include "dict0crea.h"
#include "dict0priv.h"
#include <dd/properties.h>
#include "dict0mem.h"
#include "dict0stats.h"
#include "rem0rec.h"
#include "data0type.h"
#include "mach0data.h"
#include "dict0dict.h"
#include "fts0priv.h"
#include "ut0crc32.h"
#include "srv0start.h"
#include "sql_table.h"
#include "sql_base.h"
#include "ha_innodb.h"
#include "ha_innopart.h"
#include "ha_prototypes.h"
#include "derror.h"
#include "fts0plugin.h"
#include "btr0sea.h"
#include "query_options.h"
#include <bitset>

/** Check if the InnoDB index is consistent with dd::Index
@param[in]	index		InnoDB index
@param[in]	dd_index	dd::Index or dd::Partition_index
@return	true	if match
@retval	false	if not match */
template<typename Index>
static
bool
dd_index_match(
	const dict_index_t*	index,
	const Index*		dd_index)
{
	bool	match = true;

	/* Don't check the name for primary index, since internal index
	name could be variant */
	if (my_strcasecmp(system_charset_info, index->name(),
			  dd_index->name().c_str()) != 0
	    && strcmp(dd_index->name().c_str(), "PRIMARY") != 0) {
		ib::warn() << "Index name in InnoDB is "
			<< index->name() << " while index name in global DD is "
			<< dd_index->name();
		match = false;
	}

	const dd::Properties&	p = dd_index->se_private_data();
	uint64	id;
	uint32	root;
	uint64	trx_id;
	ut_ad(p.exists(dd_index_key_strings[DD_INDEX_ID]));
	p.get_uint64(dd_index_key_strings[DD_INDEX_ID], &id);
	if (id != index->id) {
		ib::warn() << "Index id in InnoDB is "
			<< index->id << " while index id in"
			<< " global DD is " << id;
		match = false;
	}

	ut_ad(p.exists(dd_index_key_strings[DD_INDEX_ROOT]));
	p.get_uint32(dd_index_key_strings[DD_INDEX_ROOT], &root);
	if (root != index->page) {
		ib::warn() << "Index root in InnoDB is "
			<< index->page << " while index root in"
			<< " global DD is " << root;
		match = false;
	}

	ut_ad(p.exists(dd_index_key_strings[DD_INDEX_TRX_ID]));
	p.get_uint64(dd_index_key_strings[DD_INDEX_TRX_ID], &trx_id);
	/* For DD tables, the trx_id=0 is got from get_se_private_id().
	TODO: index->trx_id is not expected to be 0 once Bug#25730513 is fixed*/
	if (trx_id != 0 && index->trx_id != 0 && trx_id != index->trx_id) {
		ib::warn() << "Index transaction id in InnoDB is "
			<< index->trx_id << " while index transaction"
			<< " id in global DD is " << trx_id;
		match = false;
	}

	return(match);
}

/** Check if the InnoDB table is consistent with dd::Table
@param[in]	table			InnoDB table
@param[in]	dd_table		dd::Table or dd::Partition
@return	true	if match
@retval	false	if not match */
template<typename Table>
bool
dd_table_match(
	const dict_table_t*	table,
	const Table*		dd_table)
{
	/* Temporary table has no metadata written */
	if (dd_table == nullptr || table->is_temporary()) {
		return(true);
	}

	bool	match = true;

	if (dd_table->se_private_id() != table->id) {
		ib::warn() << "Table id in InnoDB is "
			<< table->id << " while the id in global DD is "
			<< dd_table->se_private_id();
		match = false;
	}

	/* If tablespace is discarded, no need to check indexes */
	if (dict_table_is_discarded(table)) {
		return(match);
	}

	for (const auto dd_index : dd_table->indexes()) {

		if (dd_table->tablespace_id() == dict_sys_t::dd_sys_space_id
		    && dd_index->tablespace_id() != dd_table->tablespace_id()) {
			ib::warn() << "Tablespace id in table is "
				<< dd_table->tablespace_id()
				<< ", while tablespace id in index "
				<< dd_index->name() << " is "
				<< dd_index->tablespace_id();
		}

		const dict_index_t*	index = dd_find_index(table, dd_index);
		ut_ad(index != nullptr);

		if (!dd_index_match(index, dd_index)) {
			match = false;
		}
	}

	/* Tablespace and options can be checked here too */
	return(match);
}

template bool dd_table_match<dd::Table>(const dict_table_t*, const dd::Table*);
template bool dd_table_match<dd::Partition>(const dict_table_t*, const dd::Partition*);

/** Release a metadata lock.
@param[in,out]	thd	current thread
@param[in,out]	mdl	metadata lock */
void
dd_mdl_release(
	THD*		thd,
	MDL_ticket**	mdl)
{
	if (*mdl == nullptr) {
		return;
	}

	dd::release_mdl(thd, *mdl);
	*mdl = nullptr;
}

/** Instantiate an InnoDB in-memory table metadata (dict_table_t)
based on a Global DD object.
@param[in,out]	client		data dictionary client
@param[in]	dd_table	Global DD table object
@param[in]	dd_part		Global DD partition or subpartition, or NULL
@param[in]	tbl_name	table name, or NULL if not known
@param[out]	table		InnoDB table (NULL if not found or loadable)
@param[in]	thd		Thread THD
@return	error code
@retval	0	on success */
int
dd_table_open_on_dd_obj(
	dd::cache::Dictionary_client*	client,
	const dd::Table&		dd_table,
	const dd::Partition*		dd_part,
	const char*			tbl_name,
	dict_table_t*&			table,
	THD*				thd)
{
#ifdef UNIV_DEBUG
	ut_ad(dd_table.is_persistent());

	if (dd_part != nullptr) {
		ut_ad(&dd_part->table() == &dd_table);
		ut_ad(dd_table.se_private_id() == dd::INVALID_OBJECT_ID);
		ut_ad(dd_table_is_partitioned(dd_table));

		ut_ad(dd_part->parent_partition_id() == dd::INVALID_OBJECT_ID ||
                      dd_part->parent() != nullptr);

		ut_ad(((dd_part->table().subpartition_type()
		       != dd::Table::ST_NONE)
			  == (dd_part->parent() != nullptr)));
	}

	/* If this is a internal temporary table, it's impossible
	to verify the MDL against the table name, because both the
	database name and table name may be invalid for MDL */
	if (tbl_name && !row_is_mysql_tmp_table_name(tbl_name)) {
		char	db_buf[MAX_DATABASE_NAME_LEN];
		char	tbl_buf[MAX_TABLE_NAME_LEN];

		dd_parse_tbl_name(tbl_name, db_buf, tbl_buf, nullptr);
		if (dd_part == nullptr) {
			ut_ad(innobase_strcasecmp(dd_table.name().c_str(),
						  tbl_buf) == 0);
		} else {
			ut_ad(innobase_strcasecmp(dd_table.name().c_str(),							  tbl_buf) == 0);
		}
	}
#endif /* UNIV_DEBUG */

	int			error		= 0;
	const table_id_t	table_id	= dd_part == nullptr
		? dd_table.se_private_id()
		: dd_part->se_private_id();
	const ulint		fold		= ut_fold_ull(table_id);

	ut_ad(table_id != dd::INVALID_OBJECT_ID);

	mutex_enter(&dict_sys->mutex);

	HASH_SEARCH(id_hash, dict_sys->table_id_hash, fold,
		    dict_table_t*, table, ut_ad(table->cached),
		    table->id == table_id);

	if (table != nullptr) {
		table->acquire();
	}

	mutex_exit(&dict_sys->mutex);

	if (table != nullptr) {
		return(0);
	}

	TABLE_SHARE		ts;
	dd::Schema*		schema;
	const char*		table_cache_key;
	size_t			table_cache_key_len;

	if (tbl_name != nullptr) {
		schema = nullptr;
		table_cache_key = tbl_name;
		table_cache_key_len = dict_get_db_name_len(tbl_name);
	} else {
		error = client->acquire_uncached<dd::Schema>(
			dd_table.schema_id(), &schema);

		if (error != 0) {
			return(error);
		}

		table_cache_key = schema->name().c_str();
		table_cache_key_len = schema->name().size();
	}

	init_tmp_table_share(thd,
			     &ts, table_cache_key, table_cache_key_len,
			     dd_table.name().c_str(), ""/* file name */,
			     nullptr);

	error = open_table_def_suppress_invalid_meta_data(thd, &ts, dd_table);

	if (error == 0) {
		TABLE	td;

		error = open_table_from_share(thd, &ts,
					      dd_table.name().c_str(),
					      0, OPEN_FRM_FILE_ONLY, 0,
					      &td, false, &dd_table);
		if (error == 0) {
			char		tmp_name[MAX_FULL_NAME_LEN];
			const char*	tab_namep;

			if (tbl_name) {
				tab_namep = tbl_name;
			} else {
				snprintf(tmp_name, sizeof tmp_name,
					 "%s/%s", schema->name().c_str(),
					 dd_table.name().c_str());
				tab_namep = tmp_name;
			}

			if (dd_part == nullptr) {
				table = dd_open_table(
					client, &td, tab_namep,
					&dd_table, thd);
			} else {
				table = dd_open_table(
					client, &td, tab_namep,
					dd_part, thd);
			}

			closefrm(&td, false);
		}
	}

	free_table_share(&ts);

	return(error);
}

/** Load an InnoDB table definition by InnoDB table ID.
@param[in,out]	thd		current thread
@param[in,out]	mdl		metadata lock;
nullptr if we are resurrecting table IX locks in recovery
@param[in]	table_id	InnoDB table or partition ID
@return	InnoDB table
@retval	nullptr	if the table is not found, or there was an error */
static
dict_table_t*
dd_table_open_on_id_low(
	THD*			thd,
	MDL_ticket**		mdl,
	table_id_t		table_id)
{
	char		part_name[FN_REFLEN];
	const char*	name_to_open = nullptr;

	ut_ad(thd == nullptr || thd == current_thd);
#ifdef UNIV_DEBUG
	btrsea_sync_check	check(false);
	ut_ad(!sync_check_iterate(check));
#endif
	ut_ad(!srv_is_being_shutdown);

	if (thd == nullptr) {
		ut_ad(mdl == nullptr);
		thd = current_thd;
	}

	const dd::Table*				dd_table;
	const dd::Partition*				dd_part	= nullptr;
	dd::cache::Dictionary_client*			dc
		= dd::get_dd_client(thd);
	dd::cache::Dictionary_client::Auto_releaser	releaser(dc);

	for (;;) {
		dd::String_type	schema;
		dd::String_type	tablename;
		if (dc->get_table_name_by_se_private_id(handler_name,
							table_id,
							&schema, &tablename)) {
			return(nullptr);
		}

		const bool	not_table = schema.empty();

		if (not_table) {
			if (dc->get_table_name_by_partition_se_private_id(
				    handler_name, table_id,
				    &schema, &tablename)
			    || schema.empty()) {
				return(nullptr);
			}
		}

		/* Now we have tablename, and MDL locked it if necessary. */
		if (mdl != nullptr) {
			if (*mdl == nullptr && dd_mdl_acquire(
				    thd, mdl, schema.c_str(),
				    tablename.c_str())) {
				return(nullptr);
			}

			ut_ad(*mdl != nullptr);
		}

		if (dc->acquire(schema, tablename, &dd_table)
		    || dd_table == nullptr) {
			if (mdl != nullptr) {
				dd_mdl_release(thd, mdl);
			}
			return(nullptr);
		}

		const bool	is_part = dd_table_is_partitioned(*dd_table);

		/* Verify facts between dd_table and facts we know
		1) Partiton table or not
		2) Table ID matches or not
		3) Table in InnoDB */
		bool		same_name = not_table == is_part
			&& (not_table || dd_table->se_private_id() == table_id)
			&& dd_table->engine() == handler_name;

		/* Do more verification for partition table */
		if (same_name && is_part) {
			auto end = dd_table->leaf_partitions().end();
			auto i = std::search_n(
				dd_table->leaf_partitions().begin(), end, 1,
				table_id,
				[](const dd::Partition* p, table_id_t id)
				{
					return(p->se_private_id() == id);
				});

			if (i == end) {
				same_name = false;
			} else {
				size_t	name_len;

				dd_part = *i;
				ut_ad(dd_part_is_stored(dd_part));
				/* For partition, we need to compose the
				name. */
				snprintf(part_name, sizeof part_name,
					 "%s/%s", schema.c_str(),
					 tablename.c_str());
					name_len = strlen(part_name);
				Ha_innopart_share::create_partition_postfix(
					part_name + name_len,
					FN_REFLEN - name_len, dd_part);
				name_to_open = part_name;
			}
		}

		/* facts do not match, retry */
		if (!same_name) {
			if (mdl != nullptr) {
				dd_mdl_release(thd, mdl);
			}
			continue;
		}

		ut_ad(same_name);
		break;
	}

	ut_ad(dd_part != nullptr
	      || dd_table->se_private_id() == table_id);
	ut_ad(dd_part == nullptr || dd_table == &dd_part->table());
	ut_ad(dd_part == nullptr || dd_part->se_private_id() == table_id);

	dict_table_t*	ib_table = nullptr;

	dd_table_open_on_dd_obj(
		dc, *dd_table, dd_part, name_to_open,
		ib_table, thd);

	if (mdl && ib_table == nullptr) {
		dd_mdl_release(thd, mdl);
	}

	return(ib_table);
}

/** Check if access to a table should be refused.
@param[in,out]	table	InnoDB table or partition
@return	error code
@retval	0	on success */
static MY_ATTRIBUTE((warn_unused_result))
int
dd_check_corrupted(dict_table_t*& table)
{

	if (table->is_corrupted()) {
		if (dict_table_is_sdi(table->id)
		    || dict_table_is_system(table->id)) {
			my_error(ER_TABLE_CORRUPT, MYF(0),
				 "", table->name.m_name);
		} else {
			char	db_buf[MAX_DATABASE_NAME_LEN];
			char	tbl_buf[MAX_TABLE_NAME_LEN];

			dd_parse_tbl_name(
				table->name.m_name, db_buf, tbl_buf, nullptr);
			my_error(ER_TABLE_CORRUPT, MYF(0),
				 db_buf, tbl_buf);
		}
		table = nullptr;
		return(HA_ERR_TABLE_CORRUPT);
	}

	dict_index_t* index = table->first_index();
	if (!dict_table_is_sdi(table->id)
	    && fil_space_get(index->space) == nullptr) {
		my_error(ER_TABLESPACE_MISSING, MYF(0), table->name.m_name);
		table = nullptr;
		return(HA_ERR_TABLESPACE_MISSING);
	}

	/* Ignore missing tablespaces for secondary indexes. */
	while ((index = index->next())) {
		if (!index->is_corrupted()
		    && fil_space_get(index->space) == nullptr) {
			dict_set_corrupted(index);
		}
	}

	return(0);
}

/** Open a persistent InnoDB table based on InnoDB table id, and
hold Shared MDL lock on it.
@param[in]	table_id		table identifier
@param[in,out]	thd			current MySQL connection (for mdl)
@param[in,out]	mdl			metadata lock (*mdl set if table_id was found);
@param[in]	dict_locked		dict_sys mutex is held
@param[in]	check_corruption	check if the table is corrupted or not.
mdl=NULL if we are resurrecting table IX locks in recovery
@return table
@retval NULL if the table does not exist or cannot be opened */
dict_table_t*
dd_table_open_on_id(
	table_id_t	table_id,
	THD*		thd,
	MDL_ticket**	mdl,
	bool		dict_locked,
	bool		check_corruption)
{
	dict_table_t*   ib_table;
	const ulint     fold = ut_fold_ull(table_id);
	char		db_buf[MAX_DATABASE_NAME_LEN];
	char		tbl_buf[MAX_TABLE_NAME_LEN];
	char		full_name[MAX_FULL_NAME_LEN];
	ib_uint64_t	autoinc = 0;

	if (!dict_locked) {
		mutex_enter(&dict_sys->mutex);
	}

	HASH_SEARCH(id_hash, dict_sys->table_id_hash, fold,
		    dict_table_t*, ib_table, ut_ad(ib_table->cached),
		    ib_table->id == table_id);

reopen:
	if (ib_table == nullptr) {
		if (dict_table_is_sdi(table_id)) {
			/* The table is SDI table */
			space_id_t	space_id = dict_sdi_get_space_id(
				table_id);

			/* Create in-memory table oject for SDI table */
			dict_index_t*   sdi_index = dict_sdi_create_idx_in_mem(
				space_id, false, 0, false);

			if (sdi_index == nullptr) {
				if (!dict_locked) {
					mutex_exit(&dict_sys->mutex);
				}
				return(nullptr);
			}

			ib_table = sdi_index->table;

			ut_ad(ib_table != nullptr);
			ib_table->acquire();
			mutex_exit(&dict_sys->mutex);
		} else {
			mutex_exit(&dict_sys->mutex);

			ib_table = dd_table_open_on_id_low(
				thd, mdl, table_id);

			if (autoinc != 0 && ib_table != nullptr) {
				dict_table_autoinc_lock(ib_table);
				dict_table_autoinc_update_if_greater(
					ib_table, autoinc);
				dict_table_autoinc_unlock(ib_table);
			}
		}
	} else if (mdl == nullptr || ib_table->is_temporary()
		   || dict_table_is_sdi(ib_table->id)) {
		if (dd_check_corrupted(ib_table)) {
			ut_ad(ib_table == nullptr);
		} else {
			ib_table->acquire();
		}
		mutex_exit(&dict_sys->mutex);
	} else {
		for (;;) {
			bool ret = dd_parse_tbl_name(
				ib_table->name.m_name, db_buf, tbl_buf, nullptr);
			memset(full_name, 0, MAX_FULL_NAME_LEN);
			strcpy(full_name, ib_table->name.m_name);

			mutex_exit(&dict_sys->mutex);

			if (ret == false) {
				if (dict_locked) {
					mutex_enter(&dict_sys->mutex);
				}
				return(nullptr);
			}

			ut_ad(!ib_table->is_temporary());

			if (dd_mdl_acquire(thd, mdl, db_buf, tbl_buf)) {
				if (dict_locked) {
					mutex_enter(&dict_sys->mutex);
				}
				return(nullptr);
			}

			/* Re-lookup the table after acquiring MDL. */
			mutex_enter(&dict_sys->mutex);

			HASH_SEARCH(
				id_hash, dict_sys->table_id_hash, fold,
				dict_table_t*, ib_table,
				ut_ad(ib_table->cached),
				ib_table->id == table_id);

			if (ib_table != nullptr) {
				ulint	namelen = strlen(ib_table->name.m_name);

				/* The table could have been renamed. After
				we release dict mutex before the old table
				name is MDL locked. So we need to go back
				to  MDL lock the new name. */
				if (namelen != strlen(full_name)
				    || memcmp(ib_table->name.m_name,
					      full_name, namelen)) {
					dd_mdl_release(thd, mdl);
					continue;
				} else if (check_corruption
					   && dd_check_corrupted(ib_table)) {
					ut_ad(ib_table == nullptr);
				} else if (ib_table->discard_after_ddl) {
					btr_drop_ahi_for_table(ib_table);
					dict_table_autoinc_lock(ib_table);
					autoinc = dict_table_autoinc_read(
						ib_table);
					dict_table_autoinc_unlock(ib_table);
					dict_table_remove_from_cache(ib_table);
					ib_table = nullptr;
					dd_mdl_release(thd, mdl);
					goto reopen;
				} else {
					ib_table->acquire();
				}
			}

			mutex_exit(&dict_sys->mutex);
			break;
		}

		ut_ad(*mdl != nullptr);

		/* Now the table can't be found, release MDL,
		let dd_table_open_on_id_low() do the lock, as table
		name could be changed */
		if (ib_table == nullptr) {
			dd_mdl_release(thd, mdl);
			ib_table = dd_table_open_on_id_low(
				thd, mdl, table_id);

			if (ib_table == nullptr && *mdl != nullptr) {
				dd_mdl_release(thd, mdl);
			}
		}
	}

	if (dict_locked) {
		mutex_enter(&dict_sys->mutex);
	}
	return(ib_table);
}

/** Set the discard flag for a non-partitioned dd table.
@param[in,out]	thd		current thread
@param[in]	table		InnoDB table
@param[in,out]	table_def	MySQL dd::Table to update
@param[in]	discard		discard flag
@return	true	if success
@retval false if fail. */
bool
dd_table_discard_tablespace(
	THD*			thd,
	const dict_table_t*	table,
	dd::Table*		table_def,
	bool			discard)
{
	bool			ret = false;

	DBUG_ENTER("dd_table_set_discard_flag");

	ut_ad(thd == current_thd);
#ifdef UNIV_DEBUG
	btrsea_sync_check       check(false);
	ut_ad(!sync_check_iterate(check));
#endif
	ut_ad(!srv_is_being_shutdown);

	if (table_def->se_private_id() != dd::INVALID_OBJECT_ID) {
		ut_ad(table_def->table().leaf_partitions()->empty());

		/* For discarding, we need to set new private
		id to dd_table */
		if (discard) {
			/* Set the new private id to dd_table object. */
			table_def->set_se_private_id(table->id);
		} else {
			ut_ad(table_def->se_private_id() == table->id);
		}

		/* Set index root page. */
		const dict_index_t* index = table->first_index();
		for (auto dd_index : *table_def->indexes()) {
			ut_ad(index != nullptr);

			dd::Properties& p = dd_index->se_private_data();
			p.set_uint32(dd_index_key_strings[DD_INDEX_ROOT],
				index->page);
			index = index->next();
		}

		/* Set discard flag. */
		dd::Properties& p = table_def->se_private_data();
		p.set_bool(dd_table_key_strings[DD_TABLE_DISCARD], discard);

		/* Get Tablespace object */
		dd::Tablespace*		dd_space = nullptr;
		dd::cache::Dictionary_client*	client = dd::get_dd_client(thd);
		dd::cache::Dictionary_client::Auto_releaser	releaser(client);

		dd::Object_id   dd_space_id =
			(*table_def->indexes()->begin())->tablespace_id();

		char    name[FN_REFLEN];
		snprintf(name, sizeof name, "%s.%u",
			 dict_sys_t::file_per_table_name, table->space);

		if (dd::acquire_exclusive_tablespace_mdl(thd, name, false)) {
			ut_a(false);
		}

		if (client->acquire_for_modification(dd_space_id, &dd_space)) {
			ut_a(false);
		}

		ut_a(dd_space != NULL);

		dd_tablespace_set_discard(dd_space, discard);

		if (client->update(dd_space)) {
			ut_ad(0);
		}
		ret = true;
	} else {
		ret = false;
	}

	DBUG_RETURN(ret);
}

/** Open an internal handle to a persistent InnoDB table by name.
@param[in,out]	thd		current thread
@param[out]	mdl		metadata lock
@param[in]	name		InnoDB table name
@param[in]	dict_locked	has dict_sys mutex locked
@param[in]	ignore_err	whether to ignore err
@return handle to non-partitioned table
@retval NULL if the table does not exist */
dict_table_t*
dd_table_open_on_name(
	THD*			thd,
	MDL_ticket**		mdl,
	const char*		name,
	bool			dict_locked,
	ulint			ignore_err)
{
	DBUG_ENTER("dd_table_open_on_name");

#ifdef UNIV_DEBUG
	btrsea_sync_check       check(false);
	ut_ad(!sync_check_iterate(check));
#endif
	ut_ad(!srv_is_being_shutdown);

	char		db_buf[MAX_DATABASE_NAME_LEN];
	char		tbl_buf[MAX_TABLE_NAME_LEN];
	bool		skip_mdl = !(thd && mdl);
	dict_table_t*	table = nullptr;

	/* Get pointer to a table object in InnoDB dictionary cache.
	For intrinsic table, get it from session private data */
	if (thd) {
		table = thd_to_innodb_session(
			thd)->lookup_table_handler(name);
	}

	if (table != nullptr) {
		table->acquire();
		DBUG_RETURN(table);
	}

	if (!dd_parse_tbl_name(name, db_buf, tbl_buf, nullptr)) {
		DBUG_RETURN(nullptr);
	}

	if (!skip_mdl && dd_mdl_acquire(thd, mdl, db_buf, tbl_buf)) {
		DBUG_RETURN(nullptr);
	}

	if (!dict_locked) {
		mutex_enter(&dict_sys->mutex);
	}

	table = dict_table_check_if_in_cache_low(name);

	if (table != nullptr) {
		table->acquire();
		if (!dict_locked) {
			mutex_exit(&dict_sys->mutex);
		}
		DBUG_RETURN(table);
	}

	mutex_exit(&dict_sys->mutex);

	const dd::Table*		dd_table = nullptr;
	dd::cache::Dictionary_client*	client = dd::get_dd_client(thd);
	dd::cache::Dictionary_client::Auto_releaser	releaser(client);

	if (client->acquire(db_buf, tbl_buf, &dd_table)
	    || dd_table == nullptr
	    || dd_table->engine() != innobase_hton_name) {
		/* The checking for engine should be only useful(valid)
		for getting table statistics for IS. Two relevant API
		functions are:
		1. innobase_get_table_statistics
		2. innobase_get_index_column_cardinality */
		table = nullptr;
	} else {
		if (dd_table->se_private_id() == dd::INVALID_OBJECT_ID) {
			/* This must be a partitioned table. */
			ut_ad(!dd_table->leaf_partitions().empty());
			table = nullptr;
		} else {
			ut_ad(dd_table->leaf_partitions().empty());
			dd_table_open_on_dd_obj(
				client, *dd_table, nullptr, name,
				table, thd);
		}
	}

	if (table && table->is_corrupted()
	    && !(ignore_err & DICT_ERR_IGNORE_CORRUPT)) {
		mutex_enter(&dict_sys->mutex);
		table->release();
		dict_table_remove_from_cache(table);
		table = nullptr;
		mutex_exit(&dict_sys->mutex);
	}

	if (table == nullptr && mdl) {
		dd_mdl_release(thd, mdl);
		*mdl = nullptr;
	}

	if (dict_locked) {
		mutex_enter(&dict_sys->mutex);
	}

	DBUG_RETURN(table);
}

/** Close an internal InnoDB table handle.
@param[in,out]	table		InnoDB table handle
@param[in,out]	thd		current MySQL connection (for mdl)
@param[in,out]	mdl		metadata lock (will be set NULL)
@param[in]	dict_locked	whether we hold dict_sys mutex */
void
dd_table_close(
	dict_table_t*	table,
	THD*		thd,
	MDL_ticket**	mdl,
	bool		dict_locked)
{
	dict_table_close(table, dict_locked, false);

	if (mdl != nullptr && *mdl != nullptr) {
		ut_ad(!table->is_temporary());
		dd_mdl_release(thd, mdl);
	}
}

/** Update filename of dd::Tablespace
@param[in]	dd_space_id	dd tablespace id
@param[in]	new_path	new data file path
@retval true if fail. */
bool
dd_tablespace_update_filename(
	dd::Object_id		dd_space_id,
	const char*		new_path)
{
	dd::Tablespace*		dd_space = nullptr;
	dd::Tablespace*		new_space = nullptr;
	bool			ret = false;
	THD*			thd = current_thd;

	DBUG_ENTER("dd_tablespace_update_for_rename");
#ifdef UNIV_DEBUG
	btrsea_sync_check       check(false);
	ut_ad(!sync_check_iterate(check));
#endif
	ut_ad(!srv_is_being_shutdown);
	ut_ad(new_path != nullptr);

	dd::cache::Dictionary_client*	client = dd::get_dd_client(thd);
	dd::cache::Dictionary_client::Auto_releaser	releaser(client);

	/* Get the dd tablespace */

	if (client->acquire_uncached_uncommitted<dd::Tablespace>(
			dd_space_id, &dd_space)) {
		ut_ad(false);
		DBUG_RETURN(true);
	}

	ut_a(dd_space != nullptr);
	/* Acquire mdl share lock */
	if (dd::acquire_exclusive_tablespace_mdl(
		    thd, dd_space->name().c_str(), false)) {
		ut_ad(false);
		DBUG_RETURN(true);
	}

	/* Acquire the new dd tablespace for modification */
	if (client->acquire_for_modification<dd::Tablespace>(
			dd_space_id, &new_space)) {
		ut_ad(false);
		DBUG_RETURN(true);
	}

	ut_ad(new_space->files().size() == 1);
	dd::Tablespace_file*	dd_file = const_cast<
		dd::Tablespace_file*>(*(new_space->files().begin()));
	dd_file->set_filename(new_path);
	bool fail = client->update(new_space);

	if (fail) {
		ut_ad(false);
		ret = true;
	}

	DBUG_RETURN(ret);
}

/** Validate the table format options.
@param[in]	m_thd		THD instance
@param[in]	m_form		MySQL table definition
@param[in]	zip_allowed	whether ROW_FORMAT=COMPRESSED is OK
@param[in]	strict		whether innodb_strict_mode=ON
@param[out]	is_redundant	whether ROW_FORMAT=REDUNDANT
@param[out]	blob_prefix	whether ROW_FORMAT=DYNAMIC
				or ROW_FORMAT=COMPRESSED
@param[out]	zip_ssize	log2(compressed page size),
				or 0 if not ROW_FORMAT=COMPRESSED
@param[out]	is_implicit	if tablespace is implicit
@retval true if invalid (my_error will have been called)
@retval false if valid */
static
bool
format_validate(
	THD*			m_thd,
	const TABLE*		m_form,
	bool			zip_allowed,
	bool			strict,
	bool*			is_redundant,
	bool*			blob_prefix,
	ulint*			zip_ssize,
	bool			is_implicit)
{
	bool	is_temporary = false;
	ut_ad(m_thd != nullptr);
	ut_ad(!zip_allowed || srv_page_size <= UNIV_ZIP_SIZE_MAX);

	/* 1+log2(compressed_page_size), or 0 if not compressed */
	*zip_ssize			= 0;
	const ulint	zip_ssize_max	= std::min(
		(ulint)UNIV_PAGE_SSIZE_MAX, (ulint)PAGE_ZIP_SSIZE_MAX);
	const char*	zip_refused	= zip_allowed
		? nullptr
		: srv_page_size <= UNIV_ZIP_SIZE_MAX
		? "innodb_file_per_table=OFF"
		: "innodb_page_size>16k";
	bool		invalid		= false;

	if (auto key_block_size = m_form->s->key_block_size) {
		unsigned	valid_zssize = 0;
		char		kbs[MY_INT32_NUM_DECIMAL_DIGITS
				    + sizeof "KEY_BLOCK_SIZE=" + 1];
		snprintf(kbs, sizeof kbs, "KEY_BLOCK_SIZE=%u",
			 key_block_size);
		for (unsigned kbsize = 1, zssize = 1;
		     zssize <= zip_ssize_max;
		     zssize++, kbsize <<= 1) {
			if (kbsize == key_block_size) {
				valid_zssize = zssize;
				break;
			}
		}

		if (valid_zssize == 0) {
			if (strict) {
				my_error(ER_WRONG_VALUE, MYF(0),
					 "KEY_BLOCK_SIZE",
					 kbs + sizeof "KEY_BLOCK_SIZE");
				invalid = true;
			} else {
				push_warning_printf(
					m_thd, Sql_condition::SL_WARNING,
					ER_WRONG_VALUE,
					ER_DEFAULT(ER_WRONG_VALUE),
					"KEY_BLOCK_SIZE",
					kbs + sizeof "KEY_BLOCK_SIZE");
			}
		} else if (!zip_allowed) {
			int		error = is_temporary
				? ER_UNSUPPORT_COMPRESSED_TEMPORARY_TABLE
				: ER_ILLEGAL_HA_CREATE_OPTION;

			if (strict) {
				my_error(error, MYF(0), innobase_hton_name,
					 kbs, zip_refused);
				invalid = true;
			} else {
				push_warning_printf(
					m_thd, Sql_condition::SL_WARNING,
					error,
					ER_DEFAULT(error),
					innobase_hton_name,
					kbs, zip_refused);
			}
		} else if (m_form->s->row_type == ROW_TYPE_DEFAULT
			   || m_form->s->row_type == ROW_TYPE_COMPRESSED) {
			ut_ad(m_form->s->real_row_type == ROW_TYPE_COMPRESSED);
			*zip_ssize = valid_zssize;
		} else {
			int	error = is_temporary
				? ER_UNSUPPORT_COMPRESSED_TEMPORARY_TABLE
				: ER_ILLEGAL_HA_CREATE_OPTION;
			const char* conflict = get_row_format_name(
				m_form->s->row_type);

			if (strict) {
				my_error(error, MYF(0),innobase_hton_name,
					 kbs, conflict);
				invalid = true;
			} else {
				push_warning_printf(
					m_thd, Sql_condition::SL_WARNING,
					error,
					ER_DEFAULT(error),
					innobase_hton_name, kbs, conflict);
			}
		}
	} else if (m_form->s->row_type != ROW_TYPE_COMPRESSED
		   || !is_temporary) {
		/* not ROW_FORMAT=COMPRESSED (nor KEY_BLOCK_SIZE),
		or not TEMPORARY TABLE */
	} else if (strict) {
		my_error(ER_UNSUPPORT_COMPRESSED_TEMPORARY_TABLE, MYF(0));
		invalid = true;
	} else {
		push_warning(m_thd, Sql_condition::SL_WARNING,
			     ER_UNSUPPORT_COMPRESSED_TEMPORARY_TABLE,
			     ER_THD(m_thd,
				    ER_UNSUPPORT_COMPRESSED_TEMPORARY_TABLE));
	}

	/* Check for a valid InnoDB ROW_FORMAT specifier and
	other incompatibilities. */
	rec_format_t	innodb_row_format = REC_FORMAT_DYNAMIC;

	switch (m_form->s->row_type) {
	case ROW_TYPE_DYNAMIC:
		ut_ad(*zip_ssize == 0);
		ut_ad(m_form->s->real_row_type == ROW_TYPE_DYNAMIC);
		break;
	case ROW_TYPE_COMPACT:
		ut_ad(*zip_ssize == 0);
		ut_ad(m_form->s->real_row_type == ROW_TYPE_COMPACT);
		innodb_row_format = REC_FORMAT_COMPACT;
		break;
	case ROW_TYPE_REDUNDANT:
		ut_ad(*zip_ssize == 0);
		ut_ad(m_form->s->real_row_type == ROW_TYPE_REDUNDANT);
		innodb_row_format = REC_FORMAT_REDUNDANT;
		break;
	case ROW_TYPE_FIXED:
	case ROW_TYPE_PAGED:
	case ROW_TYPE_NOT_USED:
		{
			const char* name = get_row_format_name(
				m_form->s->row_type);
			if (strict) {
				my_error(ER_ILLEGAL_HA_CREATE_OPTION, MYF(0),
					 innobase_hton_name, name);
				invalid = true;
			} else {
				push_warning_printf(
					m_thd, Sql_condition::SL_WARNING,
					ER_ILLEGAL_HA_CREATE_OPTION,
					ER_DEFAULT(ER_ILLEGAL_HA_CREATE_OPTION),
					innobase_hton_name, name);
			}
		}
		/* fall through */
	case ROW_TYPE_DEFAULT:
		switch (m_form->s->real_row_type) {
		case ROW_TYPE_FIXED:
		case ROW_TYPE_PAGED:
		case ROW_TYPE_NOT_USED:
		case ROW_TYPE_DEFAULT:
			/* get_real_row_type() should not return these */
			ut_ad(0);
			/* fall through */
		case ROW_TYPE_DYNAMIC:
			ut_ad(*zip_ssize == 0);
			break;
		case ROW_TYPE_COMPACT:
			ut_ad(*zip_ssize == 0);
			innodb_row_format = REC_FORMAT_COMPACT;
			break;
		case ROW_TYPE_REDUNDANT:
			ut_ad(*zip_ssize == 0);
			innodb_row_format = REC_FORMAT_REDUNDANT;
			break;
		case ROW_TYPE_COMPRESSED:
			innodb_row_format = REC_FORMAT_COMPRESSED;
			break;
		}

		if (*zip_ssize == 0) {
			/* No valid KEY_BLOCK_SIZE was specified,
			so do not imply ROW_FORMAT=COMPRESSED. */
			if (innodb_row_format == REC_FORMAT_COMPRESSED) {
				innodb_row_format = REC_FORMAT_DYNAMIC;
			}
			break;
		}
		/* fall through */
	case ROW_TYPE_COMPRESSED:
		if (is_temporary) {
			if (strict) {
				invalid = true;
			}
			/* ER_UNSUPPORT_COMPRESSED_TEMPORARY_TABLE
			was already reported. */
			ut_ad(m_form->s->real_row_type == ROW_TYPE_DYNAMIC);
			break;
		} else if (zip_allowed) {
			/* ROW_FORMAT=COMPRESSED without KEY_BLOCK_SIZE
			implies half the maximum compressed page size. */
			if (*zip_ssize == 0) {
				*zip_ssize = zip_ssize_max - 1;
			}
			ut_ad(m_form->s->real_row_type == ROW_TYPE_COMPRESSED);
			innodb_row_format = REC_FORMAT_COMPRESSED;
			break;
		}

		if (strict) {
			my_error(ER_ILLEGAL_HA_CREATE_OPTION, MYF(0),
				 innobase_hton_name,
				 "ROW_FORMAT=COMPRESSED", zip_refused);
			invalid = true;
		}
	}

	if (const char* algorithm = m_form->s->compress.length > 0
	    ? m_form->s->compress.str : nullptr) {
		Compression	compression;
		dberr_t		err = Compression::check(algorithm,
							 &compression);

		if (err == DB_UNSUPPORTED) {
			my_error(ER_WRONG_VALUE, MYF(0),
				 "COMPRESSION", algorithm);
			invalid = true;
		} else if (compression.m_type != Compression::NONE) {
			if (*zip_ssize != 0) {
				if (strict) {
					my_error(ER_ILLEGAL_HA_CREATE_OPTION,
						 MYF(0),
						 innobase_hton_name,
						 "COMPRESSION",
						 m_form->s->key_block_size
						 ? "KEY_BLOCK_SIZE"
						 : "ROW_FORMAT=COMPRESSED");
					invalid = true;
				}
			}

			if (is_temporary) {
				my_error(ER_ILLEGAL_HA_CREATE_OPTION, MYF(0),
					 innobase_hton_name,
					 "COMPRESSION", "TEMPORARY");
				invalid = true;
			} else if (!is_implicit) {
				my_error(ER_ILLEGAL_HA_CREATE_OPTION, MYF(0),
					 innobase_hton_name,
					 "COMPRESSION", "TABLESPACE");
				invalid = true;
			}
		}
	}

	/* Check if there are any FTS indexes defined on this table. */
	for (uint i = 0; i < m_form->s->keys; i++) {
		const KEY*	key = &m_form->key_info[i];

		if ((key->flags & HA_FULLTEXT) && is_temporary) {
			/* We don't support FTS indexes in temporary
			tables. */
			my_error(ER_INNODB_NO_FT_TEMP_TABLE, MYF(0));
			return(true);
		}
	}

	ut_ad((*zip_ssize == 0)
	      == (innodb_row_format != REC_FORMAT_COMPRESSED));

	*is_redundant = false;
	*blob_prefix = false;

	switch (innodb_row_format) {
	case REC_FORMAT_REDUNDANT:
		*is_redundant = true;
		*blob_prefix = true;
		break;
	case REC_FORMAT_COMPACT:
		*blob_prefix = true;
		break;
	case REC_FORMAT_COMPRESSED:
		ut_ad(!is_temporary);
		break;
	case REC_FORMAT_DYNAMIC:
		break;
	}

	return(invalid);
}

/** Set the AUTO_INCREMENT attribute.
@param[in,out]	se_private_data		dd::Table::se_private_data
@param[in]	autoinc			the auto-increment value */
void
dd_set_autoinc(dd::Properties& se_private_data, uint64 autoinc)
{
	/* The value of "autoinc" here is the AUTO_INCREMENT attribute
	specified at table creation. AUTO_INCREMENT=0 will silently
	be treated as AUTO_INCREMENT=1. Likewise, if no AUTO_INCREMENT
	attribute was specified, the value would be 0. */

	if (autoinc > 0) {
		/* InnoDB persists the "previous" AUTO_INCREMENT value. */
		autoinc--;
	}

	uint64	version = 0;

	if (se_private_data.exists(dd_table_key_strings[DD_TABLE_AUTOINC])) {
		/* Increment the dynamic metadata version, so that
		any previously buffered persistent dynamic metadata
		will be ignored after this transaction commits. */

		if (!se_private_data.get_uint64(
			    dd_table_key_strings[DD_TABLE_VERSION],
			    &version)) {
			version++;
		} else {
			/* incomplete se_private_data */
			ut_ad(false);
		}
	}

	se_private_data.set_uint64(dd_table_key_strings[DD_TABLE_VERSION],
				   version);
	se_private_data.set_uint64(dd_table_key_strings[DD_TABLE_AUTOINC],
				   autoinc);
}

/** Copy the engine-private parts of a table definition
when the change does not affect InnoDB. Keep the already set
AUTOINC counter related information if exist
@tparam		Table		dd::Table or dd::Partition
@param[in,out]	new_table	Copy of old table or partition definition
@param[in]	old_table	Old table or partition definition */
template<typename Table>
void
dd_copy_private(
	Table&		new_table,
	const Table&	old_table)
{
	uint64		autoinc = 0;
	uint64		version = 0;
	bool		reset = false;
	dd::Properties&	se_private_data = new_table.se_private_data();

	/* AUTOINC metadata could be set at the beginning for
	non-partitioned tables. So already set metadata should be kept */
	if (se_private_data.exists(dd_table_key_strings[DD_TABLE_AUTOINC])) {
		se_private_data.get_uint64(
			dd_table_key_strings[DD_TABLE_AUTOINC], &autoinc);
		se_private_data.get_uint64(
			dd_table_key_strings[DD_TABLE_VERSION], &version);
		reset = true;
		new_table.se_private_data().clear();
	}

	ut_ad(new_table.se_private_data().empty());

	new_table.set_se_private_id(old_table.se_private_id());
	new_table.set_se_private_data(old_table.se_private_data());

	if (reset) {
		se_private_data.set_uint64(
			dd_table_key_strings[DD_TABLE_VERSION], version);
		se_private_data.set_uint64(
			dd_table_key_strings[DD_TABLE_AUTOINC], autoinc);
	}

	ut_ad(new_table.indexes()->size() == old_table.indexes().size());

	/* Note that server could provide old and new dd::Table with
	different index order in this case, so always do a double loop */
	for (const auto old_index : old_table.indexes()) {
		auto idx = new_table.indexes()->begin();
		for (; (*idx)->name() != old_index->name(); ++idx)
			;
		ut_ad(idx != new_table.indexes()->end());

		auto new_index = *idx;
		ut_ad(!old_index->se_private_data().empty());
		ut_ad(new_index != nullptr);
		ut_ad(new_index->se_private_data().empty());
		ut_ad(new_index->name() == old_index->name());

		new_index->set_se_private_data(old_index->se_private_data());
		new_index->set_tablespace_id(old_index->tablespace_id());

		dd::Properties&	new_options = new_index->options();
		new_options.clear();
		new_options.assign(old_index->options());
	}

	new_table.table().set_row_format(old_table.table().row_format());
	dd::Properties&	new_options = new_table.options();
	new_options.clear();
	new_options.assign(old_table.options());
}

template void dd_copy_private<dd::Table>(dd::Table&, const dd::Table&);
template void dd_copy_private<dd::Partition>(
	dd::Partition&, const dd::Partition&);

/** Write metadata of a index to dd::Index
@param[in]	dd_space_id	Tablespace id, which server allocates
@param[in,out]	dd_index	dd::Index
@param[in]	index		InnoDB index object */
template<typename Index>
static
void
dd_write_index(
	dd::Object_id		dd_space_id,
	Index*			dd_index,
	const dict_index_t*	index)
{
	ut_ad(index->id != 0);
	ut_ad(index->page >= FSP_FIRST_INODE_PAGE_NO);

	dd_index->set_tablespace_id(dd_space_id);

	dd::Properties& p = dd_index->se_private_data();
	p.set_uint64(dd_index_key_strings[DD_INDEX_ID], index->id);
	p.set_uint64(dd_index_key_strings[DD_INDEX_SPACE_ID], index->space);
	p.set_uint64(dd_index_key_strings[DD_TABLE_ID], index->table->id);
	p.set_uint32(dd_index_key_strings[DD_INDEX_ROOT], index->page);
	p.set_uint64(dd_index_key_strings[DD_INDEX_TRX_ID], index->trx_id);
}

template void dd_write_index<dd::Index>(
	dd::Object_id, dd::Index*, const dict_index_t*);
template void dd_write_index<dd::Partition_index>(
	dd::Object_id, dd::Partition_index*, const dict_index_t*);

/** Write metadata of a table to dd::Table
@param[in]	dd_space_id	Tablespace id, which server allocates
@param[in,out]	dd_table	dd::Table
@param[in]	table		InnoDB table object */
template<typename Table>
void
dd_write_table(
	dd::Object_id		dd_space_id,
	Table*			dd_table,
	const dict_table_t*	table)
{
	/* Only set the tablespace id for tables in innodb_system tablespace */
	if (dd_space_id == dict_sys_t::dd_sys_space_id) {
		dd_table->set_tablespace_id(dd_space_id);
	}

	dd_table->set_se_private_id(table->id);

	if (DICT_TF_HAS_DATA_DIR(table->flags)) {
		ut_ad(dict_table_is_file_per_table(table));
		dd_table->se_private_data().set_bool(
			dd_table_key_strings[DD_TABLE_DATA_DIRECTORY], true);
	}

	for (auto dd_index : *dd_table->indexes()) {
		/* Don't assume the index orders are the same, even on
		CREATE TABLE. This could be called from TRUNCATE path,
		which would do some adjustment on FULLTEXT index, thus
		the out-of-sync order */
		const dict_index_t*	index = dd_find_index(table, dd_index);
		ut_ad(index != nullptr);
		dd_write_index(dd_space_id, dd_index, index);
	}

	for (auto dd_column : *dd_table->table().columns()) {
		dd_column->se_private_data().set_uint64(
			dd_index_key_strings[DD_TABLE_ID], table->id);
	}
}

template void dd_write_table<dd::Table>(
	dd::Object_id, dd::Table*, const dict_table_t*);
template void dd_write_table<dd::Partition>(
	dd::Object_id, dd::Partition*, const dict_table_t*);

/** Set options of dd::Table according to InnoDB table object
@param[in,out]	dd_table	dd::Table
@param[in]	table		InnoDB table object */
void
dd_set_table_options(
	dd::Table*		dd_table,
	const dict_table_t*	table)
{
	enum row_type	type;
	dd::Table::enum_row_format	format;
	dd::Properties& options = dd_table->options();

	if (auto zip_ssize = DICT_TF_GET_ZIP_SSIZE(table->flags)) {
		uint32	old_size;
		if (!options.get_uint32("key_block_size", &old_size)
		    && old_size != 0) {
			options.set_uint32("key_block_size",
					   1 << (zip_ssize - 1));
		}
	} else {
		options.set_uint32("key_block_size", 0);
		/* It's possible that InnoDB ignores the specified
		key_block_size, so check the block_size for every index.
		Server assumes if block_size = 0, there should be no
		option found, so remove it when found */
		for (auto dd_index : *dd_table->indexes()) {
			if (dd_index->options().exists("block_size")) {
				dd_index->options().remove("block_size");
			}
		}
	}

	switch (dict_tf_get_rec_format(table->flags)) {
	case REC_FORMAT_REDUNDANT:
		format = dd::Table::RF_REDUNDANT;
		type = ROW_TYPE_REDUNDANT;
		break;
	case REC_FORMAT_COMPACT:
		format = dd::Table::RF_COMPACT;
		type = ROW_TYPE_COMPACT;
		break;
	case REC_FORMAT_COMPRESSED:
		format = dd::Table::RF_COMPRESSED;
		type = ROW_TYPE_COMPRESSED;
		break;
	case REC_FORMAT_DYNAMIC:
		format = dd::Table::RF_DYNAMIC;
		type = ROW_TYPE_DYNAMIC;
		break;
	default:
		ut_ad(0);
	}

	dd_table->set_row_format(format);
	if (options.exists("row_type")) {
		options.set_uint32("row_type", type);
	}
}

/** Write metadata of a tablespace to dd::Tablespace
@param[in,out]	dd_space	dd::Tablespace
@param[in]	tablespace	InnoDB tablespace object */
void
dd_write_tablespace(
	dd::Tablespace*		dd_space,
	const Tablespace&	tablespace)
{
	dd::Properties& p = dd_space->se_private_data();
	p.set_uint32(dd_space_key_strings[DD_SPACE_ID], tablespace.space_id());
	p.set_uint32(dd_space_key_strings[DD_SPACE_FLAGS],
		     static_cast<uint32>(tablespace.flags()));
}

/** Add fts doc id column and index to new table
when old table has hidden fts doc id without fulltext index
@param[in,out]	new_table	New dd table
@param[in]	old_table	Old dd table */
void
dd_add_fts_doc_id_index(
	dd::Table&		new_table,
	const dd::Table&	old_table)
{
	if (new_table.columns()->size() == old_table.columns().size()) {
		ut_ad(new_table.indexes()->size() == old_table.indexes().size());
		return;
	}

	ut_ad(new_table.columns()->size() + 1 == old_table.columns().size());
	ut_ad(new_table.indexes()->size() + 1 == old_table.indexes().size());

	/* Add hidden FTS_DOC_ID column */
	dd::Column* col = new_table.add_column();
	col->set_hidden(true);
	col->set_name(FTS_DOC_ID_COL_NAME);
	col->set_type(dd::enum_column_types::LONGLONG);
	col->set_nullable(false);
	col->set_unsigned(true);
	col->set_collation_id(1);

	/* Add hidden FTS_DOC_ID index */
	dd_set_hidden_unique_index(new_table.add_index(),
				   FTS_DOC_ID_INDEX_NAME, col);

	return;
}

/** Find the specified dd::Index or dd::Partition_index in an InnoDB table
@tparam		Index			dd::Index or dd::Partition_index
@param[in]	table			InnoDB table object
@param[in]	dd_index		Index to search
@return	the dict_index_t object related to the index */
template<typename Index>
const dict_index_t*
dd_find_index(
	const dict_table_t*	table,
	Index*			dd_index)
{
	/* If the name is PRIMARY, return the first index directly,
	because the internal index name could be 'GEN_CLUST_INDEX'.
	It could be possible that the primary key name is not PRIMARY,
	because it's an implicitly upgraded unique index. We have to
	search all the indexes */
	if (dd_index->name() == "PRIMARY") {
		return(table->first_index());
	}

	/* The order could be different because all unique dd::Index(es)
	would be in front of other indexes. */
	const dict_index_t*   index;
	for (index = table->first_index();
	     (index != nullptr
	      && (dd_index->name() != index->name()
		  || !index->is_committed()));
	     index = index->next()) {}

	ut_ad(index != nullptr);

#ifdef UNIV_DEBUG
	/* Never find another index with the same name */
	const dict_index_t*	next_index = index->next();
	for (;
	     (next_index != nullptr
	      && (dd_index->name() != next_index->name()
		  || !next_index->is_committed()));
	     next_index = next_index->next()) {}
	ut_ad(next_index == nullptr);
#endif /* UNIV_DEBUG */

	return(index);
}

template const dict_index_t* dd_find_index<dd::Index>(
	const dict_table_t*, dd::Index*);
template const dict_index_t* dd_find_index<dd::Partition_index>(
	const dict_table_t*, dd::Partition_index*);

/** Create an index.
@param[in,out]	table		InnoDB table
@param[in]	strict		whether to be strict about the max record size
@param[in]	form		MySQL table structure
@param[in]	key_num		key_info[] offset
@return		error code
@retval		0 on success
@retval		HA_ERR_INDEX_COL_TOO_LONG if a column is too long
@retval		HA_ERR_TOO_BIG_ROW if the record is too long */
static MY_ATTRIBUTE((warn_unused_result))
int
dd_fill_one_dict_index(
	dict_table_t*		table,
	bool			strict,
	const TABLE_SHARE*	form,
	uint			key_num)
{
	const KEY&		key		= form->key_info[key_num];
	ulint			type = 0;
	unsigned		n_fields	= key.user_defined_key_parts;
	unsigned		n_uniq		= n_fields;
	std::bitset<REC_MAX_N_FIELDS>	indexed;

	/* This name cannot be used for a non-primary index */
	ut_ad(key_num == form->primary_key
	      || my_strcasecmp(system_charset_info,
			       key.name, primary_key_name) != 0);
	/* PARSER is only valid for FULLTEXT INDEX */
	ut_ad((key.flags & (HA_FULLTEXT | HA_USES_PARSER)) != HA_USES_PARSER);
	ut_ad(form->fields > 0);
	ut_ad(n_fields > 0);

	if (key.flags & HA_SPATIAL) {
		ut_ad(!table->is_intrinsic());
		type = DICT_SPATIAL;
		ut_ad(n_fields == 1);
	} else if (key.flags & HA_FULLTEXT) {
		ut_ad(!table->is_intrinsic());
		type = DICT_FTS;
		n_uniq = 0;
	} else if (key_num == form->primary_key) {
		ut_ad(key.flags & HA_NOSAME);
		ut_ad(n_uniq > 0);
		type = DICT_CLUSTERED | DICT_UNIQUE;
	} else {
		type = (key.flags & HA_NOSAME)
			? DICT_UNIQUE
			: 0;
	}

	ut_ad(!!(type & DICT_FTS) == (n_uniq == 0));

	dict_index_t*	index = dict_mem_index_create(
		table->name.m_name, key.name, 0, type, n_fields);

	index->n_uniq = n_uniq;

	const ulint	max_len	= DICT_MAX_FIELD_LEN_BY_FORMAT(table);
	DBUG_EXECUTE_IF("ib_create_table_fail_at_create_index",
			dict_mem_index_free(index);
			my_error(ER_INDEX_COLUMN_TOO_LONG, MYF(0), max_len);
			return(HA_ERR_TOO_BIG_ROW););

	for (unsigned i = 0; i < key.user_defined_key_parts; i++) {
		const KEY_PART_INFO*	key_part	= &key.key_part[i];
		unsigned		prefix_len	= 0;
		const Field*		field		= key_part->field;
		ut_ad(field == form->field[key_part->fieldnr - 1]);
		ut_ad(field == form->field[field->field_index]);

		if (field->is_virtual_gcol()) {
			index->type |= DICT_VIRTUAL;
		}

		bool	is_asc = true;

		if (key_part->key_part_flag & HA_REVERSE_SORT) {
			is_asc = false;
		}

		if (key.flags & HA_SPATIAL) {
			prefix_len = 0;
		} else if (key.flags & HA_FULLTEXT) {
			prefix_len = 0;
		} else if (key_part->key_part_flag & HA_PART_KEY_SEG) {
			/* SPATIAL and FULLTEXT index always are on
			full columns. */
			ut_ad(!(key.flags & (HA_SPATIAL | HA_FULLTEXT)));
			prefix_len = key_part->length;
			ut_ad(prefix_len > 0);
		} else {
			ut_ad(key.flags & (HA_SPATIAL | HA_FULLTEXT)
			      || (!is_blob(field->real_type())
				  && field->real_type()
				  != MYSQL_TYPE_GEOMETRY)
			      || key_part->length
			      >= (field->type() == MYSQL_TYPE_VARCHAR
				  ? field->key_length()
				  : field->pack_length()));
			prefix_len = 0;
		}

		if ((key_part->length > max_len || prefix_len > max_len)
		    && !(key.flags & (HA_FULLTEXT))) {

			dict_mem_index_free(index);
			my_error(ER_INDEX_COLUMN_TOO_LONG, MYF(0), max_len);
			return(HA_ERR_INDEX_COL_TOO_LONG);
		}

		dict_col_t*	col = nullptr;

		if (innobase_is_v_fld(field)) {
			dict_v_col_t*	v_col = dict_table_get_nth_v_col_mysql(
					table, field->field_index);
			col = reinterpret_cast<dict_col_t*>(v_col);
		} else {
			ulint	t_num_v = 0;
			for (ulint z = 0; z < field->field_index; z++) {
				if (innobase_is_v_fld(form->field[z])) {
					t_num_v++;
				}
			}

			col = &table->cols[field->field_index - t_num_v];
		}

		dict_index_add_col(index, table, col, prefix_len, is_asc);
	}

	ut_ad(((key.flags & HA_FULLTEXT) == HA_FULLTEXT)
	      == !!(index->type & DICT_FTS));

	index->n_user_defined_cols = key.user_defined_key_parts;

	if (dict_index_add_to_cache(table, index, 0, FALSE) != DB_SUCCESS) {
		ut_ad(0);
		return(HA_ERR_GENERIC);
	}

	index = UT_LIST_GET_LAST(table->indexes);

	if (index->type & DICT_FTS) {
		ut_ad((key.flags & HA_FULLTEXT) == HA_FULLTEXT);
		ut_ad(index->n_uniq == 0);
		ut_ad(n_uniq == 0);

		if (table->fts->cache == nullptr) {
			DICT_TF2_FLAG_SET(table, DICT_TF2_FTS);
			table->fts->cache = fts_cache_create(table);

			rw_lock_x_lock(&table->fts->cache->init_lock);
			/* Notify the FTS cache about this index. */
			fts_cache_index_cache_create(table, index);
			rw_lock_x_unlock(&table->fts->cache->init_lock);
		}
	}

	if (strcmp(index->name, FTS_DOC_ID_INDEX_NAME) == 0) {
		ut_ad(table->fts_doc_id_index == nullptr);
		table->fts_doc_id_index = index;
	}

	return(0);
}

/** Parse MERGE_THRESHOLD value from a comment string.
@param[in]      thd     connection
@param[in]      str     string which might include 'MERGE_THRESHOLD='
@return value parsed
@retval dict_index_t::MERGE_THRESHOLD_DEFAULT for missing or invalid value. */
static
ulint
dd_parse_merge_threshold(THD* thd, const char* str)
{
	static constexpr char   label[] = "MERGE_THRESHOLD=";
	const char* pos = strstr(str, label);

	if (pos != nullptr) {
		pos += (sizeof label) - 1;

		int ret = atoi(pos);

		if (ret > 0
		    && unsigned(ret) <= DICT_INDEX_MERGE_THRESHOLD_DEFAULT) {
			return(static_cast<ulint>(ret));
		}

		push_warning_printf(
			thd, Sql_condition::SL_WARNING,
			WARN_OPTION_IGNORED,
			ER_DEFAULT(WARN_OPTION_IGNORED),
			"MERGE_THRESHOLD");
	}

	return(DICT_INDEX_MERGE_THRESHOLD_DEFAULT);
}

/** Copy attributes from MySQL TABLE_SHARE into an InnoDB table object.
@param[in,out]	thd		thread context
@param[in,out]	table		InnoDB table
@param[in]	table_share	TABLE_SHARE */
inline
void
dd_copy_from_table_share(
	THD*			thd,
	dict_table_t*		table,
	const TABLE_SHARE*	table_share)
{
	if (table->is_temporary()) {
		dict_stats_set_persistent(table, false, true);
	} else {
		switch (table_share->db_create_options
			& (HA_OPTION_STATS_PERSISTENT
			   | HA_OPTION_NO_STATS_PERSISTENT)) {
		default:
			/* If a CREATE or ALTER statement contains
			STATS_PERSISTENT=0 STATS_PERSISTENT=1,
			it will be interpreted as STATS_PERSISTENT=1. */
		case HA_OPTION_STATS_PERSISTENT:
			dict_stats_set_persistent(table, true, false);
			break;
		case HA_OPTION_NO_STATS_PERSISTENT:
			dict_stats_set_persistent(table, false, true);
			break;
		case 0:
			break;
		}
	}

	dict_stats_auto_recalc_set(
		table,
		table_share->stats_auto_recalc == HA_STATS_AUTO_RECALC_ON,
		table_share->stats_auto_recalc == HA_STATS_AUTO_RECALC_OFF);


	table->stats_sample_pages = table_share->stats_sample_pages;

	const ulint	merge_threshold_table = table_share->comment.str
		? dd_parse_merge_threshold(thd, table_share->comment.str)
		: DICT_INDEX_MERGE_THRESHOLD_DEFAULT;
	dict_index_t*	index	= table->first_index();

	index->merge_threshold = merge_threshold_table;

	if (dict_index_is_auto_gen_clust(index)) {
		index = index->next();
	}

	for (uint i = 0; i < table_share->keys; i++) {
		const KEY*	key_info = &table_share->key_info[i];

		ut_ad(index != nullptr);

		if (key_info->flags & HA_USES_COMMENT
		    && key_info->comment.str != nullptr) {
			index->merge_threshold = dd_parse_merge_threshold(
				thd, key_info->comment.str);
		} else {
			index->merge_threshold = merge_threshold_table;
		}

		index = index->next();

		/* Skip hidden FTS_DOC_ID index */
		if (index != nullptr && index->hidden) {
			ut_ad(strcmp(index->name, FTS_DOC_ID_INDEX_NAME) == 0);
			index = index->next();
		}
	}

#ifdef UNIV_DEBUG
	if (index != nullptr) {
		ut_ad(table_share->keys == 0);
		ut_ad(index->hidden);
		ut_ad(strcmp(index->name, FTS_DOC_ID_INDEX_NAME) == 0);
	}
#endif
}

/** Instantiate index related metadata
@param[in,out]	dd_table	Global DD table metadata
@param[in]	m_form		MySQL table definition
@param[in,out]	m_table		InnoDB table definition
@param[in]	create_info	create table information
@param[in]	zip_allowed	if compression is allowed
@param[in]	strict		if report error in strict mode
@param[in]	m_thd		THD instance
@return 0 if successful, otherwise error number */
inline
int
dd_fill_dict_index(
	const dd::Table&	dd_table,
	const TABLE*		m_form,
	dict_table_t*		m_table,
	HA_CREATE_INFO*		create_info,
	bool			zip_allowed,
	bool			strict,
	THD*			m_thd)
{
	int		error = 0;

	/* Create the keys */
	if (m_form->s->keys == 0 || m_form->s->primary_key == MAX_KEY) {
		/* Create an index which is used as the clustered index;
		order the rows by the hidden InnoDB column DB_ROW_ID. */
		dict_index_t*	index = dict_mem_index_create(
			m_table->name.m_name, "GEN_CLUST_INDEX",
			0, DICT_CLUSTERED, 0);
		index->n_uniq = 0;

		dberr_t	new_err = dict_index_add_to_cache(
			m_table, index, index->page, FALSE);
		if (new_err != DB_SUCCESS) {
			error = HA_ERR_GENERIC;
			goto dd_error;
		}
	} else {
		/* In InnoDB, the clustered index must always be
		created first. */
		error = dd_fill_one_dict_index(
			m_table, strict, m_form->s, m_form->s->primary_key);
		if (error != 0) {
			goto dd_error;
		}
	}

	for (uint i = !m_form->s->primary_key; i < m_form->s->keys; i++) {
		error = dd_fill_one_dict_index(
			m_table, strict, m_form->s, i);
		if (error != 0) {
			goto dd_error;
		}
	}

	if (dict_table_has_fts_index(m_table)) {
		ut_ad(DICT_TF2_FLAG_IS_SET(m_table, DICT_TF2_FTS));
	}

	/* Create the ancillary tables that are common to all FTS indexes on
	this table. */
	if (DICT_TF2_FLAG_IS_SET(m_table, DICT_TF2_FTS_HAS_DOC_ID)
	    || DICT_TF2_FLAG_IS_SET(m_table, DICT_TF2_FTS)) {
		fts_doc_id_index_enum	ret;

		ut_ad(!m_table->is_intrinsic());
		/* Check whether there already exists FTS_DOC_ID_INDEX */
		ret = innobase_fts_check_doc_id_index_in_def(
			m_form->s->keys, m_form->key_info);

		switch (ret) {
		case FTS_INCORRECT_DOC_ID_INDEX:
			push_warning_printf(m_thd,
					    Sql_condition::SL_WARNING,
					    ER_WRONG_NAME_FOR_INDEX,
					    " InnoDB: Index name %s is reserved"
					    " for the unique index on"
					    " FTS_DOC_ID column for FTS"
					    " Document ID indexing"
					    " on table %s. Please check"
					    " the index definition to"
					    " make sure it is of correct"
					    " type\n",
					    FTS_DOC_ID_INDEX_NAME,
					    m_table->name.m_name);

			if (m_table->fts) {
				fts_free(m_table);
			}

			my_error(ER_WRONG_NAME_FOR_INDEX, MYF(0),
				 FTS_DOC_ID_INDEX_NAME);
			return(HA_ERR_GENERIC);
		case FTS_EXIST_DOC_ID_INDEX:
			break;
		case FTS_NOT_EXIST_DOC_ID_INDEX:
			dict_index_t*	doc_id_index;
			doc_id_index = dict_mem_index_create(
				m_table->name.m_name,
				FTS_DOC_ID_INDEX_NAME,
				0, DICT_UNIQUE, 1);
			doc_id_index->add_field(FTS_DOC_ID_COL_NAME, 0, true);

			dberr_t	new_err = dict_index_add_to_cache(
				m_table, doc_id_index,
				doc_id_index->page, FALSE);
			if (new_err != DB_SUCCESS) {
				error = HA_ERR_GENERIC;
				goto dd_error;
			}

			doc_id_index = UT_LIST_GET_LAST(m_table->indexes);
			doc_id_index->hidden = true;
		}

		/* Cache all the FTS indexes on this table in the FTS
		specific structure. They are used for FTS indexed
		column update handling. */
		if (dict_table_has_fts_index(m_table)) {

			fts_t*	fts = m_table->fts;
			ut_a(fts != nullptr);

			dict_table_get_all_fts_indexes(
				m_table, m_table->fts->indexes);
		}

		ulint	fts_doc_id_col = ULINT_UNDEFINED;

		ret = innobase_fts_check_doc_id_index(
			m_table, nullptr, &fts_doc_id_col);

		if (ret != FTS_INCORRECT_DOC_ID_INDEX) {
			ut_ad(m_table->fts->doc_col == ULINT_UNDEFINED);
			m_table->fts->doc_col = fts_doc_id_col;
			ut_ad(m_table->fts->doc_col != ULINT_UNDEFINED);

			m_table->fts_doc_id_index =
				dict_table_get_index_on_name(
					m_table, FTS_DOC_ID_INDEX_NAME);
		}
	}

	if (Field** autoinc_col = m_form->s->found_next_number_field) {
		const dd::Properties& p = dd_table.se_private_data();
		dict_table_autoinc_set_col_pos(
			m_table, (*autoinc_col)->field_index);
		uint64	version, autoinc = 0;
		if (p.get_uint64(dd_table_key_strings[DD_TABLE_VERSION],
				 &version)
		    || p.get_uint64(dd_table_key_strings[DD_TABLE_AUTOINC],
				    &autoinc)) {
			ut_ad(!"problem setting AUTO_INCREMENT");
			error = HA_ERR_CRASHED;
			goto dd_error;
		}

		dict_table_autoinc_lock(m_table);
		dict_table_autoinc_initialize(m_table, autoinc + 1);
		dict_table_autoinc_unlock(m_table);
		m_table->autoinc_persisted = autoinc;
	}

	if (error == 0) {
		dd_copy_from_table_share(m_thd, m_table, m_form->s);
		ut_ad(!m_table->is_temporary()
		      || !dict_table_page_size(m_table).is_compressed());
		if (!m_table->is_temporary()) {
			dict_table_stats_latch_create(m_table, true);
		}
	} else {
dd_error:
		for (dict_index_t* f_index = UT_LIST_GET_LAST(m_table->indexes);
		     f_index != nullptr;
		     f_index = UT_LIST_GET_LAST(m_table->indexes)) {

			dict_index_remove_from_cache(m_table, f_index);
		}

		dict_mem_table_free(m_table);
	}

	return(error);
}

/** Determine if a table contains a fulltext index.
@param[in]      table		dd::Table
@return whether the table contains any fulltext index */
inline
bool
dd_table_contains_fulltext(const dd::Table& table)
{
	for (const dd::Index* index : table.indexes()) {
		if (index->type() == dd::Index::IT_FULLTEXT) {
			return(true);
		}
	}
	return(false);
}

/** Instantiate in-memory InnoDB table metadata (dict_table_t),
without any indexes.
@tparam		Table		dd::Table or dd::Partition
@param[in]	dd_tab		Global Data Dictionary metadata,
				or NULL for internal temporary table
@param[in]	m_form		MySQL TABLE for current table
@param[in]	norm_name	normalized table name
@param[in]	create_info	create info
@param[in]	zip_allowed	whether ROW_FORMAT=COMPRESSED is OK
@param[in]	strict		whether to use innodb_strict_mode=ON
@param[in]	m_thd		thread THD
@param[in]	is_implicit	if it is an implicit tablespace
@return created dict_table_t on success or nullptr */
template<typename Table>
inline
dict_table_t*
dd_fill_dict_table(
	const Table*		dd_tab,
	const TABLE*		m_form,
	const char*		norm_name,
	HA_CREATE_INFO*		create_info,
	bool			zip_allowed,
	bool			strict,
	THD*			m_thd,
	bool			is_implicit)
{
	mem_heap_t*	heap;
	bool		is_encrypted = false;
	bool		is_discard = false;

	ut_ad(dd_tab != nullptr);
	ut_ad(m_thd != nullptr);
	ut_ad(norm_name != nullptr);
	ut_ad(create_info == nullptr
	      || m_form->s->row_type == create_info->row_type);
	ut_ad(create_info == nullptr
	      || m_form->s->key_block_size == create_info->key_block_size);
	ut_ad(dd_tab != nullptr);

	if (m_form->s->fields > REC_MAX_N_USER_FIELDS) {
		my_error(ER_TOO_MANY_FIELDS, MYF(0));
		return(nullptr);
	}

	/* Set encryption option. */
	dd::String_type	encrypt;
	if (dd_tab->table().options().exists("encrypt_type")) {
		dd_tab->table().options().get("encrypt_type", encrypt);
		if (!Encryption::is_none(encrypt.c_str())) {
			ut_ad(innobase_strcasecmp(encrypt.c_str(), "y") == 0);
			is_encrypted = true;
		}

	}

	/* Check discard flag. */
	const dd::Properties& p = dd_tab->table().se_private_data();
	if (p.exists(dd_table_key_strings[DD_TABLE_DISCARD])) {
		p.get_bool(dd_table_key_strings[DD_TABLE_DISCARD], &is_discard);
	}

	const unsigned	n_mysql_cols = m_form->s->fields;

	bool	has_doc_id = false;

	/* First check if dd::Table contains the right hidden column
	as FTS_DOC_ID */
	const dd::Column*	doc_col;
	doc_col = dd_find_column(&dd_tab->table(), FTS_DOC_ID_COL_NAME);

	/* Check weather this is a proper typed FTS_DOC_ID */
	if (doc_col && doc_col->type() == dd::enum_column_types::LONGLONG
	    && !doc_col->is_nullable()) {
		has_doc_id = true;
	}

	const bool	fulltext = dd_tab != nullptr
		&& dd_table_contains_fulltext(dd_tab->table());

	/* If there is a fulltext index, then it must have a FTS_DOC_ID */
	if (fulltext) {
		ut_ad(has_doc_id);
	}

	bool	add_doc_id = false;

	/* Need to add FTS_DOC_ID column if it is not defined by user,
	since TABLE_SHARE::fields does not contain it if it is a hidden col */
	if (has_doc_id && doc_col->is_hidden()) {
#ifdef UNIV_DEBUG
		ulint	doc_id_col;
		ut_ad(!create_table_check_doc_id_col(
			m_thd, m_form, &doc_id_col));
#endif
		add_doc_id = true;
	}

	const unsigned	n_cols = n_mysql_cols + add_doc_id;

	bool	is_redundant;
	bool	blob_prefix;
	ulint	zip_ssize;

	/* Validate the table format options */
	if (format_validate(m_thd, m_form, zip_allowed, strict,
			    &is_redundant, &blob_prefix, &zip_ssize,
			    is_implicit)) {
		return(nullptr);
	}

	ulint	n_v_cols = 0;

	/* Find out the number of virtual columns */
	for (ulint i = 0; i < m_form->s->fields; i++) {
		Field*  field = m_form->field[i];

		if (innobase_is_v_fld(field)) {
			n_v_cols++;
		}
	}

	ut_ad(n_v_cols <= n_cols);

	/* Create the dict_table_t */
	dict_table_t*	m_table = dict_mem_table_create(
		norm_name, 0, n_cols, n_v_cols, 0, 0);

	/* Set up the field in the newly allocated dict_table_t */
	m_table->id = dd_tab->se_private_id();

	if (dd_tab->se_private_data().exists(
		dd_table_key_strings[DD_TABLE_DATA_DIRECTORY])) {
		m_table->flags |= DICT_TF_MASK_DATA_DIR;
	}

	/* Check if this table is FTS AUX table, if so, set DICT_TF2_AUX flag */
	fts_aux_table_t aux_table;
	if (fts_is_aux_table_name(&aux_table, norm_name, strlen(norm_name))) {
		DICT_TF2_FLAG_SET(m_table, DICT_TF2_AUX);
		m_table->parent_id = aux_table.parent_id;
	}

	if (is_discard) {
		m_table->ibd_file_missing = true;
		m_table->flags2 |= DICT_TF2_DISCARDED;
	}

	if (!is_redundant) {
		m_table->flags |= DICT_TF_COMPACT;
	}

	if (is_implicit) {
		m_table->flags2 |= DICT_TF2_USE_FILE_PER_TABLE;
	} else {
		m_table->flags |= (1 << DICT_TF_POS_SHARED_SPACE);
	}

	if (!blob_prefix) {
		m_table->flags |= (1 << DICT_TF_POS_ATOMIC_BLOBS);
	}

	if (zip_ssize != 0) {
		m_table->flags |= (zip_ssize << DICT_TF_POS_ZIP_SSIZE);
	}

	m_table->fts = nullptr;
	if (has_doc_id) {
		if (fulltext) {
			DICT_TF2_FLAG_SET(m_table, DICT_TF2_FTS);
		}

		if (add_doc_id) {
			DICT_TF2_FLAG_SET(m_table, DICT_TF2_FTS_HAS_DOC_ID);
		}

		if (fulltext || add_doc_id) {
			m_table->fts = fts_create(m_table);
			m_table->fts->cache = fts_cache_create(m_table);
		}
	}

	bool	is_temp = !dd_tab->is_persistent()
		&& (dd_tab->se_private_id()
		    >= dict_sys_t::NUM_HARD_CODED_TABLES);
	if (is_temp) {
		m_table->flags2 |= DICT_TF2_TEMPORARY;
	}

	if (is_encrypted) {
		/* We don't support encrypt intrinsic and temporary table.  */
		ut_ad(!m_table->is_intrinsic() && !m_table->is_temporary());
		DICT_TF2_FLAG_SET(m_table, DICT_TF2_ENCRYPTION);
	}

	/* Since 8.0 (including after the upgrade from 5.7), all
	FTS table name would be in HEX format */
	m_table->flags2 |= DICT_TF2_FTS_AUX_HEX_NAME;

	heap = mem_heap_create(1000);

	/* Fill out each column info */
	for (unsigned i = 0; i < n_mysql_cols; i++) {
		const Field*	field = m_form->field[i];
		ulint		prtype = 0;
		unsigned	col_len = field->pack_length();

		/* The MySQL type code has to fit in 8 bits
		in the metadata stored in the InnoDB change buffer. */
		ut_ad(field->charset() == nullptr
		      || field->charset()->number <= MAX_CHAR_COLL_NUM);
		ut_ad(field->charset() == nullptr
		      || field->charset()->number > 0);

		ulint	nulls_allowed;
		ulint	unsigned_type;
		ulint	binary_type;
		ulint	long_true_varchar;
		ulint	charset_no;
		ulint	mtype = get_innobase_type_from_mysql_type(
			&unsigned_type, field);

		nulls_allowed = field->real_maybe_null() ? 0 : DATA_NOT_NULL;

		/* Convert non nullable fields in FTS AUX tables as nullable.
		This is because in 5.7, we created FTS AUX tables clustered
		index with nullable field, although NULLS are not inserted.
		When fields are nullable, the record layout is dependent on
		that. When registering FTS AUX Tables with new DD, we cannot
		register nullable fields as part of Primary Key. Hence we register
		them as non-nullabe in DD but treat as nullable in InnoDB.
		This way the compatibility with 5.7 FTS AUX tables is also
		maintained. */
		if (m_table->is_fts_aux()) {
			  if ((strcmp(field->field_name, "doc_id") == 0)
			      || (strcmp(field->field_name, "key") == 0)) {
				nulls_allowed = 0;
			}
		}

		binary_type = field->binary() ? DATA_BINARY_TYPE : 0;

		charset_no = 0;
		if (dtype_is_string_type(mtype)) {
			charset_no = static_cast<ulint>(
				field->charset()->number);
		}

		long_true_varchar = 0;
		if (field->type() == MYSQL_TYPE_VARCHAR) {
			col_len -= ((Field_varstring*) field)->length_bytes;

			if (((Field_varstring*) field)->length_bytes == 2) {
				long_true_varchar = DATA_LONG_TRUE_VARCHAR;
			}
		}

		ulint	is_virtual = (innobase_is_v_fld(field))
					? DATA_VIRTUAL : 0;

		bool    is_stored = innobase_is_s_fld(field);

		if (!is_virtual) {
			prtype = dtype_form_prtype(
				(ulint) field->type() | nulls_allowed
				| unsigned_type | binary_type
				| long_true_varchar, charset_no);
			dict_mem_table_add_col(m_table, heap, field->field_name,
					       mtype, prtype, col_len);
		} else {
			prtype = dtype_form_prtype(
				(ulint) field->type() | nulls_allowed
				| unsigned_type | binary_type
				| long_true_varchar | is_virtual, charset_no);
			dict_mem_table_add_v_col(
				m_table, heap, field->field_name, mtype,
				prtype, col_len, i,
				field->gcol_info->non_virtual_base_columns());
		}

		if (is_stored) {
			ut_ad(!is_virtual);
			/* Added stored column in m_s_cols list. */
			dict_mem_table_add_s_col(
				m_table,
				field->gcol_info->non_virtual_base_columns());
		}
	}

	ulint	j = 0;

	/* For each virtual column, we will need to set up its base column
	info */
	if (m_table->n_v_cols > 0) {
		for (unsigned i = 0; i < n_mysql_cols; i++) {
			dict_v_col_t*	v_col;

			Field*  field = m_form->field[i];

			if (!innobase_is_v_fld(field)) {
				continue;
			}

			v_col = dict_table_get_nth_v_col(m_table, j);

			j++;

			innodb_base_col_setup(m_table, field, v_col);
		}
	}

	if (add_doc_id) {
		/* Add the hidden FTS_DOC_ID column. */
		fts_add_doc_id_column(m_table, heap);
	}

	/* Add system columns to make adding index work */
	dict_table_add_system_columns(m_table, heap);

	mem_heap_free(heap);

	return(m_table);
}

/* Create metadata for specified tablespace, acquiring exlcusive MDL first
@param[in,out]	dd_client	data dictionary client
@param[in,out]	thd		THD
@param[in,out]	dd_space_name	dd tablespace name
@param[in]	space		InnoDB tablespace ID
@param[in]	flags		InnoDB tablespace flags
@param[in]	filename	filename of this tablespace
@param[in,out]	dd_space_id	dd_space_id
@retval false on success
@retval true on failure */
bool
create_dd_tablespace(
	dd::cache::Dictionary_client*	dd_client,
	THD*				thd,
	const char*			dd_space_name,
	space_id_t			space_id,
	ulint				flags,
	const char*			filename,
	dd::Object_id&			dd_space_id)
{
	std::unique_ptr<dd::Tablespace> dd_space(
		dd::create_object<dd::Tablespace>());

	if (dd_space_name != nullptr) {
		dd_space->set_name(dd_space_name);
	}

	if (dd::acquire_exclusive_tablespace_mdl(
		thd, dd_space->name().c_str(), true)) {
		return(true);
	}

	dd_space->set_engine(innobase_hton_name);
	dd::Properties& p       = dd_space->se_private_data();
	p.set_uint32(dd_space_key_strings[DD_SPACE_ID],
		     static_cast<uint32>(space_id));
	p.set_uint32(dd_space_key_strings[DD_SPACE_FLAGS],
		     static_cast<uint32>(flags));
	dd::Tablespace_file*    dd_file = dd_space->add_file();
	dd_file->set_filename(filename);
	dd_file->se_private_data().set_uint32(
		dd_space_key_strings[DD_SPACE_ID],
		static_cast<uint32>(space_id));

	if (dd_client->store(dd_space.get())) {
		return(true);
	}

	dd_space_id = dd_space.get()->id();

	return(false);
}

/** Create metadata for implicit tablespace
@param[in,out]	dd_client	data dictionary client
@param[in,out]	thd		THD
@param[in]	space		InnoDB tablespace ID
@param[in]	filename	tablespace filename
@param[in,out]	dd_space_id	dd tablespace id
@retval false	on success
@retval true	on failure */
bool
dd_create_implicit_tablespace(
	dd::cache::Dictionary_client*	dd_client,
	THD*				thd,
	space_id_t			space,
	const char*			filename,
	dd::Object_id&			dd_space_id)
{
	char	space_name[11 + sizeof reserved_implicit_name];

	snprintf(space_name, sizeof space_name, "%s.%u",
		 dict_sys_t::file_per_table_name, space);

	ulint flags = fil_space_get_flags(space);

	bool fail = create_dd_tablespace(
		dd_client, thd, space_name, space,
		flags, filename, dd_space_id);

	return(fail);
}

/** Check if a tablespace is implicit.
@param[in]	dd_space	tablespace metadata
@param[in]	space_id	InnoDB tablespace ID
@retval true	if the tablespace is implicit (file per table or partition)
@retval false	if the tablespace is shared (predefined or user-created) */
bool
dd_tablespace_is_implicit(const dd::Tablespace* dd_space, space_id_t space_id)
{
	const char*	name = dd_space->name().c_str();
	const char*	suffix = &name[sizeof reserved_implicit_name];
	char*		end;

	ut_d(uint32 id);
	ut_ad(!dd_space->se_private_data().get_uint32(
		      dd_space_key_strings[DD_SPACE_ID], &id));
	ut_ad(id == space_id);

	/* TODO: NewDD: WL#10436  NewDD: Implicit tablespace name
	should be same as table name.
	Once the tablespace name is same with the table name,
	this becomes invalid */
	if (strncmp(name, dict_sys_t::file_per_table_name, suffix - name - 1)) {
		/* Not starting with innodb_file_per_table. */
		return(false);
	}

	if (suffix[-1] != '.' || suffix[0] == '\0'
	    || strtoul(suffix, &end, 10) != space_id
	    || *end != '\0') {
		ut_ad(!"invalid implicit tablespace name");
		return(false);
	}

	return(true);
}

/** Determine if a tablespace is implicit.
@param[in,out]	client		data dictionary client
@param[in]	dd_space_id	dd tablespace id
@param[out]	implicit	whether the tablespace is implicit tablespace
@retval false	on success
@retval true	on failure */
bool
dd_tablespace_is_implicit(
	dd::cache::Dictionary_client*	client,
	dd::Object_id			dd_space_id,
	bool*				implicit)
{
	dd::Tablespace*		dd_space = nullptr;
	uint32			id = 0;

	const bool	fail
		= client->acquire_uncached_uncommitted<dd::Tablespace>(
			dd_space_id, &dd_space)
		|| dd_space == nullptr
		|| dd_space->se_private_data().get_uint32(
			dd_space_key_strings[DD_SPACE_ID], &id);

	if (!fail) {
		*implicit = dd_tablespace_is_implicit(dd_space, id);
	}

	return(fail);
}

/** Load foreign key constraint info for the dd::Table object.
@param[out]	m_table		InnoDB table handle
@param[in]	dd_table	Global DD table
@param[in]	col_names	column names, or NULL
@param[in]	ignore_err	DICT_ERR_IGNORE_FK_NOKEY or DICT_ERR_IGNORE_NONE
@param[in]	dict_locked	True if dict_sys->mutex is already held,
				otherwise false
@return DB_SUCCESS	if successfully load FK constraint */
dberr_t
dd_table_load_fk_from_dd(
	dict_table_t*			m_table,
	const dd::Table*		dd_table,
	const char**			col_names,
	dict_err_ignore_t		ignore_err,
	bool				dict_locked)
{
	dberr_t	err = DB_SUCCESS;

	/* Now fill in the foreign key info */
	for (const dd::Foreign_key* key : dd_table->foreign_keys()) {
		char	buf[MAX_FULL_NAME_LEN];

		if (*(key->name().c_str()) == '#' 
		    && *(key->name().c_str() + 1) == 'f') {
			continue;
		}

		dd::String_type	db_name = key->referenced_table_schema_name();
		dd::String_type	tb_name = key->referenced_table_name();

		bool	truncated;
		build_table_filename(buf, sizeof(buf),
				     db_name.c_str(), tb_name.c_str(),
				     NULL, 0, &truncated);
		ut_ad(!truncated);
		char	norm_name[FN_REFLEN];
		normalize_table_name(norm_name, buf);

		dict_foreign_t* foreign = dict_mem_foreign_create();
		foreign->foreign_table_name = mem_heap_strdup(
			foreign->heap, m_table->name.m_name);

		dict_mem_foreign_table_name_lookup_set(foreign, TRUE);

		if (innobase_get_lower_case_table_names() == 2) {
			innobase_casedn_str(norm_name);
		} else {
#ifndef _WIN32
			if (innobase_get_lower_case_table_names() == 1) {
				innobase_casedn_str(norm_name);
			}
#endif /* !_WIN32 */
		}

		foreign->referenced_table_name = mem_heap_strdup(
			foreign->heap, norm_name);
		dict_mem_referenced_table_name_lookup_set(foreign, TRUE);
		ulint	db_len = dict_get_db_name_len(m_table->name.m_name);

		ut_ad(db_len > 0);

		memcpy(buf, m_table->name.m_name, db_len);

		buf[db_len] = '\0';

		snprintf(norm_name, sizeof norm_name, "%s/%s",
			 buf, key->name().c_str());

		foreign->id = mem_heap_strdup(
			foreign->heap, norm_name);

		switch (key->update_rule()) {
		case dd::Foreign_key::RULE_NO_ACTION:
			foreign->type = DICT_FOREIGN_ON_UPDATE_NO_ACTION;
			break;
		case dd::Foreign_key::RULE_RESTRICT:
		case dd::Foreign_key::RULE_SET_DEFAULT:
			foreign->type = 0;
			break;
		case dd::Foreign_key::RULE_CASCADE:
			foreign->type = DICT_FOREIGN_ON_UPDATE_CASCADE;
			break;
		case dd::Foreign_key::RULE_SET_NULL:
			foreign->type = DICT_FOREIGN_ON_UPDATE_SET_NULL;
			break;
		default:
			ut_ad(0);
		}

		switch (key->delete_rule()) {
		case dd::Foreign_key::RULE_NO_ACTION:
			foreign->type |= DICT_FOREIGN_ON_DELETE_NO_ACTION;
		case dd::Foreign_key::RULE_RESTRICT:
		case dd::Foreign_key::RULE_SET_DEFAULT:
			break;
		case dd::Foreign_key::RULE_CASCADE:
			foreign->type |= DICT_FOREIGN_ON_DELETE_CASCADE;
			break;
		case dd::Foreign_key::RULE_SET_NULL:
			foreign->type |= DICT_FOREIGN_ON_DELETE_SET_NULL;
			break;
		default:
			ut_ad(0);
		}

		foreign->n_fields = key->elements().size();

		foreign->foreign_col_names = static_cast<const char**>(
			mem_heap_alloc(foreign->heap,
				       foreign->n_fields * sizeof(void*)));

		foreign->referenced_col_names = static_cast<const char**>(
			mem_heap_alloc(foreign->heap,
				       foreign->n_fields * sizeof(void*)));

		ulint	num_ref = 0;

		for (const dd::Foreign_key_element* key_e : key->elements()) {
			dd::String_type	ref_col_name
				 = key_e->referenced_column_name();

			foreign->referenced_col_names[num_ref]
				= mem_heap_strdup(foreign->heap,
						  ref_col_name.c_str());
			ut_ad(ref_col_name.c_str());

			const dd::Column*	f_col =  &key_e->column();
			foreign->foreign_col_names[num_ref]
				= mem_heap_strdup(
					foreign->heap, f_col->name().c_str());
			num_ref++;
		}

		if (!dict_locked) {
			mutex_enter(&dict_sys->mutex);
		}
#ifdef UNIV_DEBUG
		dict_table_t*   for_table;

		for_table = dict_table_check_if_in_cache_low(
			foreign->foreign_table_name_lookup);

		ut_ad(for_table);
#endif
		/* Fill in foreign->foreign_table and index, then add to
		dict_table_t */
		err = dict_foreign_add_to_cache(
			foreign, col_names, FALSE, true, ignore_err);
		if (!dict_locked) {
			mutex_exit(&dict_sys->mutex);
		}

		if (err != DB_SUCCESS) {
			break;
		}

		/* Set up the FK virtual column info */
		dict_mem_table_free_foreign_vcol_set(m_table);
		dict_mem_table_fill_foreign_vcol_set(m_table);
	}
	return(err);
}

/** Load foreign key constraint for the table. Note, it could also open
the foreign table, if this table is referenced by the foreign table
@param[in,out]	client		data dictionary client
@param[in]	tbl_name	Table Name
@param[in]	col_names	column names, or NULL
@param[out]	m_table		InnoDB table handle
@param[in]	dd_table	Global DD table
@param[in]	thd		thread THD
@param[in]	dict_locked	True if dict_sys->mutex is already held,
				otherwise false
@param[in]	check_charsets	whether to check charset compatibility
@param[in,out]	fk_tables	name list for tables that refer to this table
@return DB_SUCCESS	if successfully load FK constraint */
dberr_t
dd_table_load_fk(
	dd::cache::Dictionary_client*	client,
	const char*			tbl_name,
	const char**			col_names,
	dict_table_t*			m_table,
	const dd::Table*		dd_table,
	THD*				thd,
	bool				dict_locked,
	bool				check_charsets,
	dict_names_t*			fk_tables)
{
	dberr_t			err = DB_SUCCESS;
	dict_err_ignore_t	ignore_err = DICT_ERR_IGNORE_NONE;

	/* Check whether FOREIGN_KEY_CHECKS is set to 0. If so, the table
	can be opened even if some FK indexes are missing. If not, the table
	can't be opened in the same situation */
	if (thd_test_options(thd, OPTION_NO_FOREIGN_KEY_CHECKS)) {
		ignore_err = DICT_ERR_IGNORE_FK_NOKEY;
	}

	err = dd_table_load_fk_from_dd(m_table, dd_table, col_names,
				       ignore_err, dict_locked);

	if (err != DB_SUCCESS) {
		return(err);
	}

	if (dict_locked) {
		mutex_exit(&dict_sys->mutex);
	}

	err = dd_table_check_for_child(client, tbl_name, col_names, m_table,
				       dd_table, thd, check_charsets,
				       ignore_err, fk_tables);

	if (dict_locked) {
		mutex_enter(&dict_sys->mutex);
	}

	return(err);
}

/** Load foreign key constraint for the table. Note, it could also open
the foreign table, if this table is referenced by the foreign table
@param[in,out]	client		data dictionary client
@param[in]	tbl_name	Table Name
@param[in]	col_names	column names, or NULL
@param[out]	m_table		InnoDB table handle
@param[in]	dd_table	Global DD table
@param[in]	thd		thread THD
@param[in]	check_charsets	whether to check charset compatibility
@param[in]	ignore_err	DICT_ERR_IGNORE_FK_NOKEY or DICT_ERR_IGNORE_NONE
@param[in,out]	fk_tables	name list for tables that refer to this table
@return DB_SUCCESS	if successfully load FK constraint */
dberr_t
dd_table_check_for_child(
	dd::cache::Dictionary_client*	client,
	const char*			tbl_name,
	const char**			col_names,
	dict_table_t*			m_table,
	const dd::Table*		dd_table,
	THD*				thd,
	bool				check_charsets,
	dict_err_ignore_t		ignore_err,
	dict_names_t*			fk_tables)
{
	dberr_t	err = DB_SUCCESS;

	/* TODO: NewDD: Temporary ignore DD system table until
	WL#6049 inplace */
	if (!dict_sys_t::is_hardcoded(m_table->id) && fk_tables != nullptr) {
		std::vector<dd::String_type>	child_schema;
		std::vector<dd::String_type>	child_name;

		char    name_buf1[MAX_DATABASE_NAME_LEN];
		char    name_buf2[MAX_TABLE_NAME_LEN];

		dd_parse_tbl_name(m_table->name.m_name,
					name_buf1, name_buf2, nullptr);

		if (client->fetch_fk_children_uncached(
			name_buf1, name_buf2, &child_schema, &child_name)) {
			return(DB_ERROR);
		}

		std::vector<dd::String_type>::iterator it = child_name.begin();
		for (auto& db_name : child_schema) {
			dd::String_type	tb_name = *it;
			char	buf[2 * NAME_CHAR_LEN * 5 + 2 + 1];
			bool	truncated;
			build_table_filename(
				buf, sizeof(buf), db_name.c_str(),
				tb_name.c_str(), NULL, 0, &truncated);
			ut_ad(!truncated);
			char	full_name[FN_REFLEN];
			normalize_table_name(full_name, buf);

			if (innobase_get_lower_case_table_names() == 2) {
				innobase_casedn_str(full_name);
			} else {
#ifndef _WIN32
				if (innobase_get_lower_case_table_names() == 1) {
				       innobase_casedn_str(full_name);
				}
#endif /* !_WIN32 */
			}

			mutex_enter(&dict_sys->mutex);

			/* Load the foreign table first */
			dict_table_t*	foreign_table =
				dd_table_open_on_name_in_mem(
					full_name, true);

			if (foreign_table) {
				for (auto &fk : foreign_table->foreign_set) {
					if (strcmp(fk->referenced_table_name,
						   tbl_name) != 0) {
						continue;
					}

					if (fk->referenced_table) {
						ut_ad(fk->referenced_table == m_table);
					} else {
						err = dict_foreign_add_to_cache(
							fk, col_names,
							check_charsets,
							false,
							ignore_err);
						if (err != DB_SUCCESS) {
							foreign_table->release();
							mutex_exit(
							&dict_sys->mutex);
							return(err);
						}
					}
				}
				foreign_table->release();
			} else {
				/* To avoid recursively loading the tables
				related through the foreign key constraints,
				the child table name is saved here. The child
				table will be loaded later, along with its
				foreign key constraint. */
				lint old_size = mem_heap_get_size(
					m_table->heap);

				fk_tables->push_back(
					mem_heap_strdupl(m_table->heap,
						full_name,
						strlen(full_name)));

				lint new_size = mem_heap_get_size(
					m_table->heap);
				dict_sys->size += new_size - old_size;
			}

			mutex_exit(&dict_sys->mutex);

			ut_ad(it != child_name.end());
			++it;
		}
	}

	return(err);
}

/** Get tablespace name of dd::Table
@param[in]	dd_table	dd table object
@return the tablespace name. */
template<typename Table>
const char*
dd_table_get_space_name(
	const Table*		dd_table)
{
	dd::Tablespace*		dd_space = nullptr;
	THD*			thd = current_thd;
	const char*		space_name;

	DBUG_ENTER("dd_table_get_space_name");
	ut_ad(!srv_is_being_shutdown);

	dd::cache::Dictionary_client*	client = dd::get_dd_client(thd);
	dd::cache::Dictionary_client::Auto_releaser	releaser(client);

	dd::Object_id   dd_space_id = (*dd_table->indexes().begin())
		->tablespace_id();

	if (client->acquire_uncached_uncommitted<dd::Tablespace>(
		dd_space_id, &dd_space)) {
		ut_ad(false);
		DBUG_RETURN(nullptr);
	}

	ut_a(dd_space != nullptr);
	space_name = dd_space->name().c_str();

	DBUG_RETURN(space_name);
}

/********************************************************************//**
Using the table->heap, copy the null-terminated filepath into
table->data_dir_path and replace the 'databasename/tablename.ibd'
portion with 'tablename'.
This allows SHOW CREATE TABLE to return the correct DATA DIRECTORY path.
Make this data directory path only if it has not yet been saved. */
static
void
dd_save_data_dir_path(
/*====================*/
	dict_table_t*	table,		/*!< in/out: table */
	char*		filepath)	/*!< in: filepath of tablespace */
{
	ut_ad(mutex_own(&dict_sys->mutex));
	ut_a(DICT_TF_HAS_DATA_DIR(table->flags));

	ut_a(!table->data_dir_path);
	ut_a(filepath);

	/* Be sure this filepath is not the default filepath. */
	char*	default_filepath = fil_make_filepath(
			NULL, table->name.m_name, IBD, false);
	if (default_filepath) {
		if (0 != strcmp(filepath, default_filepath)) {
			ulint pathlen = strlen(filepath);
			ut_a(pathlen < OS_FILE_MAX_PATH);
			ut_a(0 == strcmp(filepath + pathlen - 4, DOT_IBD));

			table->data_dir_path = mem_heap_strdup(
				table->heap, filepath);
			os_file_make_data_dir_path(table->data_dir_path);
		}

		ut_free(default_filepath);
	}
}

/** Get the first filepath from mysql.tablespace_datafiles for a given space_id.
@param[in]	heap		heap for store file name.
@param[in]	table		dict table
@param[in]	dd_table	dd table obj
@return First filepath (caller must invoke ut_free() on it)
@retval NULL if no mysql.tablespace_datafilesentry was found. */
template<typename Table>
char*
dd_get_first_path(
	mem_heap_t*	heap,
	dict_table_t*	table,
	const Table*	dd_table)
{
	char*		filepath = NULL;
	dd::Tablespace*	dd_space = nullptr;
	THD*		thd = current_thd;
	MDL_ticket*     mdl = nullptr;
	dd::Object_id   dd_space_id;

	ut_ad(!srv_is_being_shutdown);

	dd::cache::Dictionary_client*	client = dd::get_dd_client(thd);
	dd::cache::Dictionary_client::Auto_releaser	releaser(client);

	if (dd_table == NULL) {
		char		db_buf[MAX_DATABASE_NAME_LEN];
		char		tbl_buf[MAX_TABLE_NAME_LEN];
		const dd::Table*	table_def = nullptr;

		if (!dd_parse_tbl_name(
				table->name.m_name, db_buf, tbl_buf, NULL)
		    || dd_mdl_acquire(thd, &mdl, db_buf, tbl_buf)) {
			return(filepath);
		}

		if (client->acquire(db_buf, tbl_buf, &table_def)
			|| table_def == nullptr) {
			dd_mdl_release(thd, &mdl);
			return(filepath);
		}

		dd_space_id = (*table_def->indexes().begin())->tablespace_id();

		dd_mdl_release(thd, &mdl);
	} else {
		dd_space_id = (*dd_table->indexes().begin())->tablespace_id();
	}

	if (client->acquire_uncached_uncommitted<dd::Tablespace>(
		dd_space_id, &dd_space)) {
		ut_a(false);
	}

	ut_a(dd_space != NULL);
	dd::Tablespace_file*	dd_file = const_cast<
		dd::Tablespace_file*>(*(dd_space->files().begin()));

	filepath = mem_heap_strdup(heap, dd_file->filename().c_str());

	return(filepath);
}

/** Make sure the data_dir_path is saved in dict_table_t if DATA DIRECTORY
was used. Try to read it from the fil_system first, then from NEW DD.
@param[in]	table		Table object
@param[in]	dd_table	DD table object
@param[in]	dict_mutex_own	true if dict_sys->mutex is owned already */
template<typename Table>
void
dd_get_and_save_data_dir_path(
	dict_table_t*	table,
	const Table*	dd_table,
	bool		dict_mutex_own)
{
	mem_heap_t*		heap = NULL;

	if (DICT_TF_HAS_DATA_DIR(table->flags)
	    && (!table->data_dir_path)) {
		char*	path = fil_space_get_first_path(table->space);

		if (!dict_mutex_own) {
			dict_mutex_enter_for_mysql();
		}

		if (path == NULL) {
			heap = mem_heap_create(1000);
			path = dd_get_first_path(heap, table, dd_table);
		}

		if (path != NULL) {
			dd_save_data_dir_path(table, path);
		}

		if (table->data_dir_path == NULL) {
			/* Since we did not set the table data_dir_path,
			unset the flag. */
			table->flags &= ~DICT_TF_MASK_DATA_DIR;
		}

		if (!dict_mutex_own) {
			dict_mutex_exit_for_mysql();
		}

		if (heap) {
			mem_heap_free(heap);
		} else {
			ut_free(path);
		}
	}
}

template void dd_get_and_save_data_dir_path<dd::Table>(
	dict_table_t*, const dd::Table*, bool);

template void dd_get_and_save_data_dir_path<dd::Partition>(
	dict_table_t*, const dd::Partition*, bool);

/** Get the meta-data filename from the table name for a
single-table tablespace.
@param[in]	table		table object
@param[in]	dd_table	DD table object
@param[out]	filename	filename
@param[in]	max_len		filename max length */
void
dd_get_meta_data_filename(
	dict_table_t*	table,
	dd::Table*	dd_table,
	char*		filename,
	ulint		max_len)
{
	ulint		len;
	char*		path;

	/* Make sure the data_dir_path is set. */
	dd_get_and_save_data_dir_path(table, dd_table, false);

	if (DICT_TF_HAS_DATA_DIR(table->flags)) {
		ut_a(table->data_dir_path);

		path = fil_make_filepath(
			table->data_dir_path, table->name.m_name, CFG, true);
	} else {
		path = fil_make_filepath(NULL, table->name.m_name, CFG, false);
	}

	ut_a(path);
	len = ut_strlen(path);
	ut_a(max_len >= len);

	strcpy(filename, path);

	ut_free(path);
}

/** Opens a tablespace for dd_load_table_one()
@param[in,out]	dd_table	dd table
@param[in,out]	table		A table that refers to the tablespace to open
@param[in,out]	heap		A memory heap
@param[in]	ignore_err	Whether to ignore an error. */
template<typename Table>
void
dd_load_tablespace(
	const Table*			dd_table,
	dict_table_t*			table,
	mem_heap_t*			heap,
	dict_err_ignore_t		ignore_err)
{
	bool	alloc_from_heap = false;

	ut_ad(!table->is_temporary());
	ut_ad(mutex_own(&dict_sys->mutex));

	/* The system and temporary tablespaces are preloaded and always available. */
	if (fsp_is_system_or_temp_tablespace(table->space)) {
		return;
	}

	if (table->flags2 & DICT_TF2_DISCARDED) {
		ib::warn() << "Tablespace for table " << table->name
			<< " is set as discarded.";
		table->ibd_file_missing = TRUE;
		return;
	}

	/* A file-per-table table name is also the tablespace name.
	A general tablespace name is not the same as the table name.
	Use the general tablespace name if it can be read from the
	dictionary, if not use 'innodb_general_##. */
	char*	shared_space_name = nullptr;
	char*	space_name;
	if (DICT_TF_HAS_SHARED_SPACE(table->flags)) {
		if (table->space == dict_sys_t::space_id) {
			shared_space_name = mem_strdup(
				dict_sys_t::dd_space_name);
		}
		else if (srv_sys_tablespaces_open) {
			/* For avoiding deadlock, we need to exit
			dict_sys->mutex. */
			mutex_exit(&dict_sys->mutex);
			shared_space_name = mem_strdup(
				dd_table_get_space_name(dd_table));
			mutex_enter(&dict_sys->mutex);
		}
		else {
			/* Make the temporary tablespace name. */
			shared_space_name = static_cast<char*>(
				ut_malloc_nokey(
					strlen(general_space_name) + 20));

			sprintf(shared_space_name, "%s_" ULINTPF,
				general_space_name,
				static_cast<ulint>(table->space));
		}
		space_name = shared_space_name;
	}
	else {
		space_name = table->name.m_name;
	}

	/* The tablespace may already be open. */
	if (fil_space_for_table_exists_in_mem(
		table->space, space_name, false,
		true, heap, table->id)) {
		ut_free(shared_space_name);
		return;
	}

	if (!(ignore_err & DICT_ERR_IGNORE_RECOVER_LOCK)) {
		ib::error() << "Failed to find tablespace for table "
			<< table->name << " in the cache. Attempting"
			" to load the tablespace with space id "
			<< table->space;
	}

	/* Use the remote filepath if needed. This parameter is optional
	in the call to fil_ibd_open(). If not supplied, it will be built
	from the space_name. */
	char* filepath = nullptr;
	if (DICT_TF_HAS_DATA_DIR(table->flags)) {
		/* This will set table->data_dir_path from either
		fil_system */
		dd_get_and_save_data_dir_path(table, dd_table, true);

		if (table->data_dir_path) {
			filepath = fil_make_filepath(
				table->data_dir_path,
				table->name.m_name, IBD, true);
		}

	}
	else if (DICT_TF_HAS_SHARED_SPACE(table->flags)) {

		filepath = dd_get_first_path(heap, table, dd_table);
		if (filepath == nullptr) {
			ib::warn() << "Could not find the filepath"
				" for table " << table->name <<
				", space ID " << table->space;
		} else {
			alloc_from_heap = true;
		}
	}

	/* Try to open the tablespace.  We set the 2nd param (fix_dict) to
	false because we do not have an x-lock on dict_operation_lock */
	bool is_encrypted = dict_table_is_encrypted(table);
	ulint fsp_flags = dict_tf_to_fsp_flags(table->flags,
		is_encrypted);

	dberr_t err = fil_ibd_open(
		true, FIL_TYPE_TABLESPACE, table->space,
		fsp_flags, space_name, filepath);

	if (err != DB_SUCCESS) {
		/* We failed to find a sensible tablespace file */
		table->ibd_file_missing = TRUE;
	}

	ut_free(shared_space_name);
	if (!alloc_from_heap && filepath) {
		ut_free(filepath);
	}
}

/** Open or load a table definition based on a Global DD object.
@tparam		Table		dd::Table or dd::Partition
@param[in,out]	client		data dictionary client
@param[in]	table		MySQL table definition
@param[in]	norm_name	Table Name
@param[in]	dd_table	Global DD table or partition object
@param[in]	thd		thread THD
@param[in,out]	fk_list		stack of table names which neet to load
@return ptr to dict_table_t filled, otherwise, nullptr */
template<typename Table>
dict_table_t*
dd_open_table_one(
	dd::cache::Dictionary_client*	client,
	const TABLE*			table,
	const char*			norm_name,
	const Table*			dd_table,
	THD*				thd,
	dict_names_t&			fk_list)
{
	ut_ad(dd_table != nullptr);

	bool	implicit;

	if (dd_table->tablespace_id() == dict_sys_t::dd_space_id) {
		/* DD tables are in shared DD tablespace */
		implicit = false;
	} else if (dd_tablespace_is_implicit(
		client, dd_first_index(dd_table)->tablespace_id(),
		&implicit)) {
		/* Tablespace no longer exist, it could be already dropped */
		return(nullptr);
	}

	const bool      zip_allowed = srv_page_size <= UNIV_ZIP_SIZE_MAX;
	const bool	strict = false;
	bool		first_index = true;

	/* Create dict_table_t for the table */
	dict_table_t* m_table = dd_fill_dict_table(
		dd_table, table, norm_name,
		NULL, zip_allowed, strict, thd, implicit);

	if (m_table == nullptr) {
		return(nullptr);
	}

	/* Create dict_index_t for the table */
	mutex_enter(&dict_sys->mutex);
	int	ret;
	ret = dd_fill_dict_index(
		dd_table->table(), table, m_table, NULL, zip_allowed,
		strict, thd);

	mutex_exit(&dict_sys->mutex);

	if (ret != 0) {
		return(nullptr);
	}

	mem_heap_t*	heap = mem_heap_create(1000);
	bool		fail = false;

	/* Now fill the space ID and Root page number for each index */
	dict_index_t*	index = m_table->first_index();
	for (const auto dd_index : dd_table->indexes()) {
		ut_ad(index != nullptr);

		const dd::Properties&	se_private_data
			= dd_index->se_private_data();
		uint64			id = 0;
		uint32			root = 0;
		uint32			sid = 0;
		uint64			trx_id = 0;
		dd::Object_id		index_space_id =
			dd_index->tablespace_id();
		dd::Tablespace*	index_space = nullptr;

		if (dd_table->tablespace_id() == dict_sys_t::dd_space_id) {
			sid = dict_sys_t::space_id;
		} else if (dd_table->tablespace_id()
			   == dict_sys_t::dd_temp_space_id) {
			sid = dict_sys_t::temp_space_id;
		} else {
			if (client->acquire_uncached_uncommitted<
			    dd::Tablespace>(index_space_id, &index_space)) {
				my_error(ER_TABLESPACE_MISSING, MYF(0),
					 m_table->name.m_name);
				fail = true;
				break;
			}

			if (index_space->se_private_data().get_uint32(
				dd_space_key_strings[DD_SPACE_ID],
				&sid)) {
				fail = true;
				break;
			}
		}

		if (first_index) {
			ut_ad(m_table->space == 0);
			m_table->space = sid;
			m_table->dd_space_id = index_space_id;

			mutex_enter(&dict_sys->mutex);
			dd_load_tablespace(dd_table, m_table, heap,
				DICT_ERR_IGNORE_RECOVER_LOCK);
			mutex_exit(&dict_sys->mutex);
			first_index = false;
		}

		if (se_private_data.get_uint64(
			    dd_index_key_strings[DD_INDEX_ID], &id)
		    || se_private_data.get_uint32(
			    dd_index_key_strings[DD_INDEX_ROOT], &root)
		    || se_private_data.get_uint64(
			    dd_index_key_strings[DD_INDEX_TRX_ID], &trx_id)) {
			fail = true;
			break;
		}

		ut_ad(root > 1);
		ut_ad(index->type & DICT_FTS || root != FIL_NULL
			|| dict_table_is_discarded(m_table));
		ut_ad(id != 0);
		index->page = root;
		index->space = sid;
		index->id = id;
		index->trx_id = trx_id;
		index = index->next();
	}

	mutex_enter(&dict_sys->mutex);

	if (fail) {
		for (dict_index_t* index = UT_LIST_GET_LAST(m_table->indexes);
		     index != nullptr;
		     index = UT_LIST_GET_LAST(m_table->indexes)) {
			dict_index_remove_from_cache(m_table, index);
		}
		dict_mem_table_free(m_table);
		mutex_exit(&dict_sys->mutex);
		mem_heap_free(heap);

		return(nullptr);
	}

	/* Re-check if the table has been opened/added by a concurrent
	thread */
	dict_table_t*	exist = dict_table_check_if_in_cache_low(norm_name);
	if (exist != nullptr) {
		for (dict_index_t* index = UT_LIST_GET_LAST(m_table->indexes);
		     index != nullptr;
		     index = UT_LIST_GET_LAST(m_table->indexes)) {
			dict_index_remove_from_cache(m_table, index);
		}
		dict_mem_table_free(m_table);

		m_table = exist;
	} else {
		dict_table_add_to_cache(m_table, TRUE, heap);

		if (dict_sys->dynamic_metadata != nullptr) {
			dict_table_load_dynamic_metadata(m_table);
		}
	}

	m_table->acquire();

	mutex_exit(&dict_sys->mutex);

	/* Check if this is a DD system table */
	if (m_table != nullptr) {
		char db_buf[MAX_DATABASE_NAME_LEN];
		char tbl_buf[MAX_TABLE_NAME_LEN];
		dd_parse_tbl_name(m_table->name.m_name, db_buf, tbl_buf, NULL);
		m_table->is_dd_table = dd::get_dictionary()->is_dd_table_name(
			db_buf, tbl_buf);
	}

	/* Load foreign key info. It could also register child table(s) that
	refers to current table */
	if (exist == nullptr) {
		dd_table_load_fk(client, norm_name, nullptr,
				 m_table, &dd_table->table(), thd, false,
				 true, &fk_list);
	}
	mem_heap_free(heap);

	return(m_table);
}

/** Open single table with name
@param[in,out]	client		data dictionary client
@param[in]	name		table name
@param[in]	dict_locked	dict_sys mutex is held or not
@param[in,out]	fk_list		foreign key name list
@param[in]	thd		thread THD */
static
void
dd_open_table_one_on_name(
	dd::cache::Dictionary_client*	client,
	const char*			name,
	bool				dict_locked,
	dict_names_t&			fk_list,
	THD*				thd)
{
	dict_table_t*		table = nullptr;
	const dd::Table*	dd_table = nullptr;

	if (!dict_locked) {
		mutex_enter(&dict_sys->mutex);
	}

	table = dict_table_check_if_in_cache_low(name);

	/* Exit sys mutex to access server info */
	mutex_exit(&dict_sys->mutex);

	if (!table) {
		MDL_ticket*     mdl = nullptr;
		char		db_buf[MAX_DATABASE_NAME_LEN];
		char		tbl_buf[MAX_TABLE_NAME_LEN];

		if (!dd_parse_tbl_name(
			name, db_buf, tbl_buf, nullptr)) {
			goto func_exit;
		}

		if (dd_mdl_acquire(thd, &mdl, db_buf, tbl_buf)) {
			goto func_exit;
		}

		if (client->acquire(db_buf, tbl_buf, &dd_table)
		    || dd_table == nullptr) {
			dd_mdl_release(thd, &mdl);
			goto func_exit;
		}

		ut_ad(dd_table->se_private_id()
		      != dd::INVALID_OBJECT_ID);

		TABLE_SHARE	ts;

		init_tmp_table_share(thd,
			&ts, db_buf, strlen(db_buf),
			dd_table->name().c_str(),
			""/* file name */, nullptr);

		ulint error = open_table_def_suppress_invalid_meta_data(
				thd, &ts, *dd_table);

		if (error != 0) {
			dd_mdl_release(thd, &mdl);
			goto func_exit;
		}

		TABLE	td;

		error = open_table_from_share(thd, &ts,
			dd_table->name().c_str(),
			0, OPEN_FRM_FILE_ONLY, 0,
			&td, false, dd_table);

		if (error != 0) {
			free_table_share(&ts);
			dd_mdl_release(thd, &mdl);
			goto func_exit;
		}

		table = dd_open_table_one(
			client, &td, name, dd_table, thd, fk_list);

		closefrm(&td, false);
		free_table_share(&ts);
		dd_table_close(table, thd, &mdl, false);
	}

func_exit:

	if (dict_locked) {
		mutex_enter(&dict_sys->mutex);
	}
}

/** Open foreign tables reference a table.
@param[in,out]	client		data dictionary client
@param[in]	fk_list		foreign key name list
@param[in]	dict_locked	dict_sys mutex is locked or not
@param[in]	thd		thread THD */
void
dd_open_fk_tables(
	dd::cache::Dictionary_client*	client,
	dict_names_t&			fk_list,
	bool				dict_locked,
	THD*				thd)
{
	while (!fk_list.empty()) {
		char*	name  = const_cast<char*>(fk_list.front());

		if (innobase_get_lower_case_table_names() == 2) {
			innobase_casedn_str(name);
		} else {
#ifndef _WIN32
			if (innobase_get_lower_case_table_names() == 1) {
			       innobase_casedn_str(name);
			}
#endif /* !_WIN32 */
		}

		dd_open_table_one_on_name(client, name, dict_locked,
					  fk_list, thd);

		fk_list.pop_front();
	}
}

extern const char* fts_common_tables[];

/** Open FTS AUX tables
@param[in,out]	client		data dictionary client
@param[in]	table		fts table
@param[in]	dict_locked	dict_sys mutex id held or not
@param[in]	thd		thread THD */
static
void
dd_open_fts_aux_tables(
	dd::cache::Dictionary_client*	client,
	const dict_table_t*		table,
	bool				dict_locked,
	THD*				thd)
{
	ulint		i;
	fts_table_t	fts_table;
	dict_names_t	fk_list;

	FTS_INIT_FTS_TABLE(&fts_table, NULL, FTS_COMMON_TABLE, table);

	/* Rename common auxiliary tables */
	for (i = 0; fts_common_tables[i] != nullptr; ++i) {
		char    table_name[MAX_FULL_NAME_LEN];

		fts_table.suffix = fts_common_tables[i];

		fts_get_table_name(&fts_table, table_name);
		dd_open_table_one_on_name(client, table_name,
					  dict_locked, fk_list, thd);
	}

	fts_t* fts = table->fts;

	/* Rename index specific auxiliary tables */
	for (i = 0; fts->indexes != 0 && i < ib_vector_size(fts->indexes);
	     ++i) {
		dict_index_t* index;

		index = static_cast<dict_index_t*>(
			ib_vector_getp(fts->indexes, i));

		FTS_INIT_INDEX_TABLE(&fts_table, NULL, FTS_INDEX_TABLE, index);

		for (ulint j = 0; j < FTS_NUM_AUX_INDEX; ++j) {
			char table_name[MAX_FULL_NAME_LEN];

			fts_table.suffix = fts_get_suffix(j);

			fts_get_table_name(&fts_table, table_name);
			dd_open_table_one_on_name(client, table_name,
						  dict_locked, fk_list, thd);
		}
	}
}

/** Open or load a table definition based on a Global DD object.
@param[in,out]	client		data dictionary client
@param[in]	table		MySQL table definition
@param[in]	norm_name	Table Name
@param[in]	dd_table	Global DD table or partition object
@param[in]	thd		thread THD
@return ptr to dict_table_t filled, otherwise, nullptr */
template<typename Table>
dict_table_t*
dd_open_table(
	dd::cache::Dictionary_client*	client,
	const TABLE*			table,
	const char*			norm_name,
	const Table*			dd_table,
	THD*				thd)
{
	dict_table_t*			m_table = nullptr;
	dict_names_t			fk_list;

	m_table = dd_open_table_one(client, table, norm_name,
				    dd_table, thd, fk_list);

	/* If there is foreign table references to this table, we will
	try to open them */
	if (m_table != nullptr && !fk_list.empty()) {
		dd::cache::Dictionary_client*	client
			= dd::get_dd_client(thd);
		dd::cache::Dictionary_client::Auto_releaser
			releaser(client);

		dd_open_fk_tables(client, fk_list, false, thd);
	}

	if (m_table && m_table->fts) {
		dd_open_fts_aux_tables(client, m_table, false, thd);
	}

	return(m_table);
}

template dict_table_t* dd_open_table<dd::Table>(
	dd::cache::Dictionary_client*, const TABLE*, const char*,
	const dd::Table*, THD*);

template dict_table_t* dd_open_table<dd::Partition>(
	dd::cache::Dictionary_client*, const TABLE*, const char*,
	const dd::Partition*, THD*);

/** Get next record from a new dd system table, like mysql.tables...
@param[in,out]	pcur		persistent cursor
@param[in]	mtr		the mini-transaction
@retval the next rec of the dd system table */
static
const rec_t*
dd_getnext_system_low(
	btr_pcur_t*	pcur,		/*!< in/out: persistent cursor to the
					record*/
	mtr_t*		mtr)		/*!< in: the mini-transaction */
{
	rec_t*	rec = NULL;
	bool	is_comp = dict_table_is_comp(pcur->index()->table);

	while (!rec || rec_get_deleted_flag(rec, is_comp)) {
		btr_pcur_move_to_next_user_rec(pcur, mtr);

		rec = btr_pcur_get_rec(pcur);

		if (!btr_pcur_is_on_user_rec(pcur)) {
			/* end of index */
			btr_pcur_close(pcur);

			return(NULL);
		}
	}

	/* Get a record, let's save the position */
	btr_pcur_store_position(pcur, mtr);

	return(rec);
}

/** Scan a new dd system table, like mysql.tables...
@param[in]	thd		thd
@param[in,out]	mdl		mdl lock
@param[in,out]	pcur		persistent cursor
@param[in]	mtr		the mini-transaction
@param[in]	system_id	which dd system table to open
@param[in,out]	table		dict_table_t obj of dd system table
@retval the first rec of the dd system table */
const rec_t*
dd_startscan_system(
	THD*			thd,
	MDL_ticket**		mdl,
	btr_pcur_t*		pcur,
	mtr_t*			mtr,
	dd_system_id_t		system_id,
	dict_table_t**		table)
{
	dict_table_t*	system_table;
	dict_index_t*	clust_index;
	const rec_t*	rec;

	ut_a(system_id < DD_LAST_ID);

	system_table = dd_table_open_on_id(system_id, thd, mdl, true, true);
	mtr_commit(mtr);

	clust_index = UT_LIST_GET_FIRST(system_table->indexes);

	mtr_start(mtr);
	btr_pcur_open_at_index_side(true, clust_index, BTR_SEARCH_LEAF, pcur,
				    true, 0, mtr);

	rec = dd_getnext_system_low(pcur, mtr);

	*table = system_table;

	return(rec);
}

/** Process one mysql.tables record and get the dict_table_t
@param[in]	heap		temp memory heap
@param[in,out]	rec		mysql.tables record
@param[in,out]	table		dict_table_t to fill
@param[in]	dd_tables	dict_table_t obj of dd system table
@param[in]	mdl		mdl on the table
@param[in]	mtr		the mini-transaction
@retval error message, or NULL on success */
const char*
dd_process_dd_tables_rec_and_mtr_commit(
	mem_heap_t*	heap,
	const rec_t*	rec,
	dict_table_t**	table,
	dict_table_t*	dd_tables,
	MDL_ticket**	mdl,
	mtr_t*		mtr)
{
	ulint		len;
	const byte*	field;
	const char*	err_msg = NULL;
	ulint		table_id;

	ut_ad(!rec_get_deleted_flag(rec, dict_table_is_comp(dd_tables)));
	ut_ad(mtr_memo_contains_page(mtr, rec, MTR_MEMO_PAGE_S_FIX));

	ulint*	offsets = rec_get_offsets(rec, dd_tables->first_index(), NULL,
					  ULINT_UNDEFINED, &heap);

	field = rec_get_nth_field(rec, offsets, 6, &len);

	/* If "engine" field is not "innodb", return. */
	if (strncmp((const char*)field, "InnoDB", 6) != 0) {
		*table = NULL;
		mtr_commit(mtr);
		return(err_msg);
	}

	/* Get the se_private_id field. */
	field = (const byte*)rec_get_nth_field(rec, offsets, 14, &len);

	/* When table is partitioned table, the se_private_id is null. */
	if (len != 8) {
		*table = NULL;
		mtr_commit(mtr);
		return(err_msg);
	}

	/* Get the table id */
	table_id = mach_read_from_8(field);

	/* Skip mysql.* tables. */
	if (table_id <= INNODB_DD_TABLE_ID_MAX) {
		*table = NULL;
		mtr_commit(mtr);
		return(err_msg);
	}

	/* Commit before load the table again */
	mtr_commit(mtr);
	THD*		thd = current_thd;

	*table = dd_table_open_on_id(table_id, thd, mdl, true, false);

	if (!(*table)) {
		err_msg = "Table not found";
	}

	return(err_msg);
}

/** Process one mysql.table_partitions record and get the dict_table_t
@param[in]	heap		temp memory heap
@param[in,out]	rec		mysql.table_partitions record
@param[in,out]	table		dict_table_t to fill
@param[in]	dd_tables	dict_table_t obj of dd partition table
@param[in]	mdl		mdl on the table
@param[in]	mtr		the mini-transaction
@retval error message, or NULL on success */
const char*
dd_process_dd_partitions_rec_and_mtr_commit(
	mem_heap_t*	heap,
	const rec_t*	rec,
	dict_table_t**	table,
	dict_table_t*	dd_tables,
	MDL_ticket**	mdl,
	mtr_t*		mtr)
{
	ulint		len;
	const byte*	field;
	const char*	err_msg = NULL;
	ulint		table_id;

	ut_ad(mtr_memo_contains_page(mtr, rec, MTR_MEMO_PAGE_S_FIX));

	ut_ad(!rec_get_deleted_flag(rec, dict_table_is_comp(dd_tables)));

	ulint*	offsets = rec_get_offsets(rec, dd_tables->first_index(), NULL,
					  ULINT_UNDEFINED, &heap);

	field = rec_get_nth_field(rec, offsets, 7, &len);

	/* If "engine" field is not "innodb", return. */
	if (strncmp((const char*)field, "InnoDB", 6) != 0) {
		*table = NULL;
		mtr_commit(mtr);
		return(err_msg);
	}

	/* Get the se_private_id field. */
	field = (const byte*)rec_get_nth_field(rec, offsets, 11, &len);
	/* When table is partitioned table, the se_private_id is null. */
	if (len != 8) {
		*table = NULL;
		mtr_commit(mtr);
		return(err_msg);
	}

	/* Get the table id */
	table_id = mach_read_from_8(field);

	/* Skip mysql.* tables. */
	if (table_id <= INNODB_DD_TABLE_ID_MAX) {
		*table = NULL;
		mtr_commit(mtr);
		return(err_msg);
	}

	/* Commit before load the table again */
	mtr_commit(mtr);
	THD*		thd = current_thd;

	*table = dd_table_open_on_id(table_id, thd, mdl, true, false);

	if (!(*table)) {
		err_msg = "Table not found";
	}

	return(err_msg);
}

/** Get next record of new DD system tables
@param[in,out]	pcur		persistent cursor
@param[in]	mtr		the mini-transaction
@retval next record */
const rec_t*
dd_getnext_system_rec(
	btr_pcur_t*	pcur,
	mtr_t*		mtr)
{
	const rec_t*	rec = NULL;

	/* Restore the position */
	btr_pcur_restore_position(BTR_SEARCH_LEAF, pcur, mtr);

	/* Get the next record */
	while (!rec || rec_get_deleted_flag(
			rec, dict_table_is_comp(pcur->index()->table))) {
		btr_pcur_move_to_next_user_rec(pcur, mtr);

		rec = btr_pcur_get_rec(pcur);

		if (!btr_pcur_is_on_user_rec(pcur)) {
			/* end of index */
			btr_pcur_close(pcur);

			return(NULL);
		}
	}

	/* Get a record, let's save the position */
	btr_pcur_store_position(pcur, mtr);

	return(rec);
}

/** Process one mysql.columns record and get info to dict_col_t
@param[in]	heap		temp memory heap
@param[in,out]	rec		mysql.columns record
@param[in,out]	col		dict_col_t to fill
@param[in,out]	table_id	table id
@param[in,out]	col_name	column name
@param[in,out]	nth_v_col	nth v column
@param[in]	dd_columns	dict_table_t obj of mysql.columns
@param[in]	mtr		the mini-transaction
@retval true if column is filled */
bool
dd_process_dd_columns_rec(
	mem_heap_t*		heap,
	const rec_t*		rec,
	dict_col_t*		col,
	table_id_t*		table_id,
	char**			col_name,
	ulint*			nth_v_col,
	dict_table_t*		dd_columns,
	mtr_t*			mtr)
{
	ulint		len;
	const byte*	field;
	dict_col_t*	t_col;
	ulint		pos;
	bool		is_hidden;

	ut_ad(!rec_get_deleted_flag(rec, dict_table_is_comp(dd_columns)));

	ulint*	offsets = rec_get_offsets(rec, dd_columns->first_index(), NULL,
					  ULINT_UNDEFINED, &heap);

	/* Get the hidden attibute, and skip if it's a hidden column. */
	field = (const byte*)rec_get_nth_field(rec, offsets, 25, &len);
	is_hidden = mach_read_from_1(field) & 0x01;
	if (is_hidden) {
		mtr_commit(mtr);
		return(false);
	}

	/* Get the column name. */
	field = (const byte*)rec_get_nth_field(rec, offsets, 4, &len);
	*col_name = mem_heap_strdupl(heap, (const char*) field, len);

	/* Get the position. */
	field = (const byte*)rec_get_nth_field(rec, offsets, 5, &len);
	pos = mach_read_from_4(field) - 1;

	/* Get the se_private_data field. */
	field = (const byte*)rec_get_nth_field(rec, offsets, 27, &len);

	if (len == 0 || len == UNIV_SQL_NULL) {
		mtr_commit(mtr);
		return(false);
	}

	char* p_ptr = (char*)mem_heap_strdupl(heap, (const char*) field, len);
	dd::String_type prop((char*)p_ptr);
	dd::Properties* p = dd::Properties::parse_properties(prop);

	/* Load the table and get the col. */
	if (!p || !p->exists(dd_index_key_strings[DD_TABLE_ID])) {
		if (p) {
			delete p;
		}
		mtr_commit(mtr);
		return(false);
	}

	if (!p->get_uint64(dd_index_key_strings[DD_TABLE_ID], (uint64*)table_id)) {
		THD*		thd = current_thd;
		dict_table_t*	table;
		MDL_ticket*	mdl = NULL;

		/* Commit before we try to load the table. */
		mtr_commit(mtr);
		table = dd_table_open_on_id(*table_id, thd, &mdl, true, true);

		if (!table) {
			return(false);
		}

		t_col = table->get_col(pos);

		/* Copy info. */
		col->ind = t_col->ind;
		col->mtype = t_col->mtype;
		col->prtype = t_col->prtype;
		col->len = t_col->len;

		dd_table_close(table, thd, &mdl, true);
		delete p;
	} else {
		delete p;
		mtr_commit(mtr);
		return(false);
	}

	/* Report the virtual column number */
	if (col->prtype & DATA_VIRTUAL) {
		*nth_v_col = dict_get_v_col_pos(col->ind);
	}

	return(true);
}

/** Process one mysql.columns record for virtual columns
@param[in]	heap		temp memory heap
@param[in,out]	rec		mysql.columns record
@param[in,out]	table_id	table id
@param[in,out]	pos		position
@param[in,out]	base_pos	base column position
@param[in,out]	n_row		number of rows
@param[in]	dd_columns	dict_table_t obj of mysql.columns
@param[in]	mtr		the mini-transaction
@retval true if virtual info is filled */
bool
dd_process_dd_virtual_columns_rec(
	mem_heap_t*		heap,
	const rec_t*		rec,
	table_id_t*		table_id,
	ulint**			pos,
	ulint**			base_pos,
	ulint*			n_row,
	dict_table_t*		dd_columns,
	mtr_t*			mtr)
{
	ulint		len;
	const byte*	field;
	ulint		origin_pos;
	bool		is_hidden;
	bool		is_virtual;

	ut_ad(!rec_get_deleted_flag(rec, dict_table_is_comp(dd_columns)));

	ulint*	offsets = rec_get_offsets(rec, dd_columns->first_index(), NULL,
					  ULINT_UNDEFINED, &heap);

	/* Get the is_virtual attibute, and skip if it's not a virtual column. */
	field = (const byte*)rec_get_nth_field(rec, offsets, 21, &len);
	is_virtual = mach_read_from_1(field) & 0x01;
	if (!is_virtual) {
		mtr_commit(mtr);
		return(false);
	}

	/* Get the hidden attibute, and skip if it's a hidden column. */
	field = (const byte*)rec_get_nth_field(rec, offsets, 25, &len);
	is_hidden = mach_read_from_1(field) & 0x01;
	if (is_hidden) {
		mtr_commit(mtr);
		return(false);
	}

	/* Get the position. */
	field = (const byte*)rec_get_nth_field(rec, offsets, 5, &len);
	origin_pos = mach_read_from_4(field) - 1;

	/* Get the se_private_data field. */
	field = (const byte*)rec_get_nth_field(rec, offsets, 27, &len);

	if (len == 0 || len == UNIV_SQL_NULL) {
		mtr_commit(mtr);
		return(false);
	}

	char* p_ptr = (char*)mem_heap_strdupl(heap, (const char*) field, len);
	dd::String_type prop((char*)p_ptr);
	dd::Properties* p = dd::Properties::parse_properties(prop);

	/* Load the table and get the col. */
	if (!p || !p->exists(dd_index_key_strings[DD_TABLE_ID])) {
		if (p) {
			delete p;
		}
		mtr_commit(mtr);
		return(false);
	}

	if (!p->get_uint64(dd_index_key_strings[DD_TABLE_ID], (uint64*)table_id)) {
		THD*		thd = current_thd;
		dict_table_t*	table;
		MDL_ticket*	mdl = NULL;
		dict_v_col_t*	vcol = NULL;

		/* Commit before we try to load the table. */
		mtr_commit(mtr);
		table = dd_table_open_on_id(*table_id, thd, &mdl, true, true);

		if (!table) {
			delete p;
			return(false);
		}

		vcol = dict_table_get_nth_v_col_mysql(table, origin_pos);

		/* Copy info. */
		if (vcol == NULL || vcol->num_base == 0) {
			dd_table_close(table, thd, &mdl, true);
			delete p;
			return(false);
		}

		*pos = static_cast<ulint*>(mem_heap_alloc(heap, vcol->num_base * sizeof(ulint)));
		*base_pos = static_cast<ulint*>(
			mem_heap_alloc(heap, vcol->num_base * sizeof(ulint)));
		*n_row = vcol->num_base;
		for (ulint i = 0; i < *n_row; i++) {
			(*pos)[i] = dict_create_v_col_pos(vcol->v_pos, vcol->m_col.ind);
			(*base_pos)[i] = vcol->base_col[i]->ind;
		}

		dd_table_close(table, thd, &mdl, true);
		delete p;
	} else {
		delete p;
		mtr_commit(mtr);
		return(false);
	}

	return(true);
}
/** Process one mysql.indexes record and get dict_index_t
@param[in]	heap		temp memory heap
@param[in,out]	rec		mysql.indexes record
@param[in,out]	index		dict_index_t to fill
@param[in]	dd_indexes	dict_table_t obj of mysql.indexes
@param[in]	mdl		mdl on index->table
@param[in]	mtr		the mini-transaction
@retval true if index is filled */
bool
dd_process_dd_indexes_rec(
	mem_heap_t*		heap,
	const rec_t*		rec,
	const dict_index_t**	index,
	dict_table_t*		dd_indexes,
	MDL_ticket**		mdl,
	mtr_t*			mtr)
{
	ulint		len;
	const byte*	field;
	uint32		index_id;
	uint32		space_id;
	uint32		table_id;

	*index = nullptr;

	ut_ad(!rec_get_deleted_flag(rec, dict_table_is_comp(dd_indexes)));

	ulint*	offsets = rec_get_offsets(rec, dd_indexes->first_index(), NULL,
					  ULINT_UNDEFINED, &heap);

	field = rec_get_nth_field(rec, offsets, 16, &len);

	/* If "engine" field is not "innodb", return. */
	if (strncmp((const char*)field, "InnoDB", 6) != 0) {
		mtr_commit(mtr);
		return(false);
	}

	/* Get the se_private_data field. */
	field = (const byte*)rec_get_nth_field(rec, offsets, 14, &len);

	if (len == 0 || len == UNIV_SQL_NULL) {
		mtr_commit(mtr);
		return(false);
	}

	/* Get index id. */
	dd::String_type prop((char*)field);
	dd::Properties* p = dd::Properties::parse_properties(prop);

	if (!p || !p->exists(dd_index_key_strings[DD_INDEX_ID])
	    || !p->exists(dd_index_key_strings[DD_INDEX_SPACE_ID])) {
		if (p) {
			delete p;
		}
		mtr_commit(mtr);
		return(false);
	}

	if (p->get_uint32(dd_index_key_strings[DD_INDEX_ID], &index_id)) {
		delete p;
		mtr_commit(mtr);
		return(false);
	}

	/* Get the tablespace id. */
	if (p->get_uint32(dd_index_key_strings[DD_INDEX_SPACE_ID],
			  &space_id)) {
		delete p;
		mtr_commit(mtr);
		return(false);
	}

	/* Skip mysql.* indexes. */
	if (space_id == dict_sys->space_id) {
		delete p;
		mtr_commit(mtr);
		return(false);
	}

	/* Load the table and get the index. */
	if (!p->exists(dd_index_key_strings[DD_TABLE_ID])) {
		delete p;
		mtr_commit(mtr);
		return(false);
	}

	if (!p->get_uint32(dd_index_key_strings[DD_TABLE_ID], &table_id)) {
		THD*		thd = current_thd;
		dict_table_t*	table;

		/* Commit before load the table */
		mtr_commit(mtr);
		table = dd_table_open_on_id(table_id, thd, mdl, true, true);

		if (!table) {
			delete p;
			return(false);
		}

		for (const dict_index_t* t_index = table->first_index();
		     t_index != NULL;
		     t_index = t_index->next()) {
			if (t_index->space == space_id
			    && t_index->id == index_id) {
				*index = t_index;
			}
		}

		delete p;
	} else {
		delete p;
		mtr_commit(mtr);
		return(false);
	}

	return(true);
}

/* Get space_id for a general tablespace of a given path info.
@param[in,out]	path		datafile path
@param[in,out]	space_id	space id of the tablespace to be filled
@retval true if space_id is filled. */
bool
get_id_by_name(
	char*		path,
	uint32*		space_id)
{
	char*		temp_buf = nullptr;
	FilSpace	space;

	/* Remove the path info and get file name. */
	temp_buf = strrchr(path, OS_PATH_SEPARATOR);

	if (temp_buf == nullptr) {
		temp_buf = path;
	} else {
		++temp_buf;
	}

	/* Gather space_id for the matching file in fil_system. */
	for (const fil_node_t* node = fil_node_next(nullptr);
	     node != nullptr;
	     node = fil_node_next(node)) {
		char*	path_name;

		space = node->space;

		/* Remove the path info and get file name. */
		path_name = strrchr(node->name, OS_PATH_SEPARATOR);

		if (path_name!=nullptr) {
			++path_name;
		} else {
			path_name = node->name;
		}

		/* Fetch space_id if name matches. */
		if (strcmp(temp_buf, path_name) == 0) {

			/* Skip data dictionary tablespace and temp tablespace. */
			if (space()->id == 0xFFFFFFFE
			    || space()->id == dict_sys->temp_space_id) {
				return(false);
			}

			*space_id = space()->id;

			return(true);
		}

		space = nullptr;
	}

	return(false);
}

/** Process one mysql.tablespace_files record and get information from it
@param[in]	heap		temp memory heap
@param[in,out]	rec		mysql.indexes record
@param[in,out]	space_id	space id
@param[in,out]	path		datafile path
@param[in]	dd_files	dict_table_t obj of mysql.tablespace_files
@retval true if index is filled */
bool
dd_process_dd_datafiles_rec(
	mem_heap_t*		heap,
	const rec_t*		rec,
	uint32*			space_id,
	char**			path,
	dict_table_t*		dd_files)
{
	ulint		len;
	const byte*	field;

	ut_ad(!rec_get_deleted_flag(rec, dict_table_is_comp(dd_files)));

	ulint*	offsets = rec_get_offsets(rec, dd_files->first_index(), NULL,
					  ULINT_UNDEFINED, &heap);

	/* Get the data file path. */
	field = rec_get_nth_field(rec, offsets, 4, &len);
	*path = reinterpret_cast<char*>(mem_heap_alloc(heap, len + 1));
	memset(*path, 0, len + 1);
	memcpy(*path, field, len);

	/* Get the se_private_data field. */
	field = (const byte*)rec_get_nth_field(rec, offsets, 5, &len);

	if (len == 0 || len == UNIV_SQL_NULL) {
		/* For general tablespaces we don't store se_private_data field
		in dd::Tablespace_files. So we retrieve the space_id for the
		tablspace by querying the fil system. */
		return(get_id_by_name(*path, space_id));
	}

	/* Get space id. */
	char* se_str = static_cast<char*>(mem_heap_alloc(heap, len + 1));
	memset(se_str, 0, len + 1);
	memcpy(se_str, field, len);
	dd::String_type prop(se_str);
	dd::Properties* p = dd::Properties::parse_properties(prop);

	if (!p || !p->exists(dd_space_key_strings[DD_SPACE_ID])) {
		if (p) {
			delete p;
		}
		return(false);
	}

	if (p->get_uint32(dd_space_key_strings[DD_SPACE_ID], space_id)) {
		delete p;
		return(false);
	}

	/* Skip temp tablespace. */
	if (*space_id == dict_sys->temp_space_id) {
		delete p;
		return(false);
	}

	delete p;
	return(true);
}

/** Process one mysql.indexes record and get breif info to dict_index_t
@param[in]	heap		temp memory heap
@param[in,out]	rec		mysql.indexes record
@param[in,out]	index_id	index id
@param[in,out]	space_id	space id
@param[in]	dd_indexes	dict_table_t obj of mysql.indexes
@retval true if index is filled */
bool
dd_process_dd_indexes_rec_simple(
	mem_heap_t*	heap,
	const rec_t*	rec,
	uint*		index_id,
	uint*		space_id,
	dict_table_t*	dd_indexes)
{
	ulint		len;
	const byte*	field;

	ut_ad(!rec_get_deleted_flag(rec, dict_table_is_comp(dd_indexes)));

	ulint*	offsets = rec_get_offsets(rec, dd_indexes->first_index(), NULL,
					  ULINT_UNDEFINED, &heap);

	field = rec_get_nth_field(rec, offsets, 16, &len);

	/* If "engine" field is not "innodb", return. */
	if (strncmp((const char*)field, "InnoDB", 6) != 0) {
		return(false);
	}

	/* Get the se_private_data field. */
	field = (const byte*)rec_get_nth_field(rec, offsets, 14, &len);

	if (len == 0 || len == UNIV_SQL_NULL) {
		return(false);
	}

	/* Get index id. */
	dd::String_type prop((char*)field);
	dd::Properties* p = dd::Properties::parse_properties(prop);

	if (!p || !p->exists(dd_index_key_strings[DD_INDEX_ID])
	    || !p->exists(dd_index_key_strings[DD_INDEX_SPACE_ID])) {
		if (p) {
			delete p;
		}
		return(false);
	}

	if (p->get_uint32(dd_index_key_strings[DD_INDEX_ID], index_id)) {
		delete p;
		return(false);
	}

	/* Get the tablespace_id. */
	if (p->get_uint32(dd_index_key_strings[DD_INDEX_SPACE_ID], space_id)) {
		delete p;
		return(false);
	}

	delete p;

	return(true);
}

/** Process one mysql.tablespaces record and get info
@param[in]	heap		temp memory heap
@param[in,out]	rec		mysql.tablespaces record
@param[in,out]	space_id	space id
@param[in,out]	name		space name
@param[in,out]	flags		space flags
@param[in]	dd_spaces	dict_table_t obj of mysql.tablespaces
@retval true if index is filled */
bool
dd_process_dd_tablespaces_rec(
	mem_heap_t*	heap,
	const rec_t*	rec,
	space_id_t*	space_id,
	char**		name,
	uint*		flags,
	dict_table_t*	dd_spaces)
{
	ulint		len;
	const byte*	field;
	char*		prop_str;

	ut_ad(!rec_get_deleted_flag(rec, dict_table_is_comp(dd_spaces)));

	ulint*	offsets = rec_get_offsets(rec, dd_spaces->first_index(), NULL,
		ULINT_UNDEFINED, &heap);

	field = rec_get_nth_field(rec, offsets, 7, &len);

	/* If "engine" field is not "innodb", return. */
	if (strncmp((const char*)field, "InnoDB", 6) != 0) {
		return(false);
	}

	/* Get name field. */
	field = rec_get_nth_field(rec, offsets, 3, &len);
	*name = reinterpret_cast<char*>(mem_heap_alloc(heap, len + 1));
	memset(*name, 0, len + 1);
	memcpy(*name, field, len);

	/* Get the se_private_data field. */
	field = (const byte*)rec_get_nth_field(rec, offsets, 5, &len);

	if (len == 0 || len == UNIV_SQL_NULL) {
		return(false);
	}

	prop_str = static_cast<char*>(mem_heap_alloc(heap, len + 1));
	memset(prop_str, 0, len + 1);
	memcpy(prop_str, field, len);
	dd::String_type prop(prop_str);
	dd::Properties* p = dd::Properties::parse_properties(prop);

	if (!p || !p->exists(dd_space_key_strings[DD_SPACE_ID])
	    || !p->exists(dd_index_key_strings[DD_SPACE_FLAGS])) {
		if (p) {
			delete p;
		}
		return(false);
	}

	/* Get space id. */
	if (p->get_uint32(dd_space_key_strings[DD_SPACE_ID], space_id)) {
		delete p;
		return(false);
	}

	/* Get space flag. */
	if (p->get_uint32(dd_space_key_strings[DD_SPACE_FLAGS], flags)) {
		delete p;
		return(false);
	}

	delete p;

	return(true);
}

/** Get dd tablespace id for fts table
@param[in]	parent_table	parent table of fts table
@param[in]	table		fts table
@param[in,out]	dd_space_id	dd table space id
@return true on success, false on failure. */
bool
dd_get_fts_tablespace_id(
	const dict_table_t*	parent_table,
	const dict_table_t*	table,
	dd::Object_id&		dd_space_id)
{
	char	db_name[MAX_DATABASE_NAME_LEN];
	char	table_name[MAX_TABLE_NAME_LEN];

	dd_parse_tbl_name(parent_table->name.m_name, db_name,
				table_name, NULL);

	THD*	thd = current_thd;
	dd::cache::Dictionary_client*	client = dd::get_dd_client(thd);
	dd::cache::Dictionary_client::Auto_releaser	releaser(client);

	dd::Object_id	space_id = parent_table->dd_space_id;

	dd_space_id = dd::INVALID_OBJECT_ID;

	if (dict_table_is_file_per_table(table)) {
		/* This means user table and file_per_table */
		bool	ret;
		char*	filename =
			fil_space_get_first_path(table->space);

		ret = dd_create_implicit_tablespace(
			client, thd, table->space, filename, dd_space_id);

		ut_free(filename);
		if (ret) {
			return(false);
		}

	} else if (table->space != TRX_SYS_SPACE
		   && table->space != srv_tmp_space.space_id()) {
		/* This is a user table that resides in shared tablespace */
		ut_ad(!dict_table_is_file_per_table(table));
		ut_ad(DICT_TF_HAS_SHARED_SPACE(table->flags));

		/* Currently the tablespace id is hard coded as 0 */
		dd_space_id = space_id;

		const dd::Tablespace*	index_space = NULL;
		if (client->acquire<dd::Tablespace>(space_id, &index_space)) {
			return(false);
		}

		uint32	id;
		if (index_space == NULL) {
			return(false);
		} else if (index_space->se_private_data().get_uint32(
				    dd_space_key_strings[DD_SPACE_ID], &id)
			   || id != table->space) {
			ut_ad(!"missing or incorrect tablespace id");
			return(false);
		}
	} else if (table->space == TRX_SYS_SPACE) {
		/* This is a user table that resides in innodb_system
		tablespace */
		ut_ad(!dict_table_is_file_per_table(table));
		dd_space_id = dict_sys_t::dd_sys_space_id;
	}

	return(true);
}

/** Set table options for fts dd tables according to dict table
@param[in,out]	dd_table	dd table instance
@param[in]	table		dict table instance */
void
dd_set_fts_table_options(
	dd::Table*		dd_table,
	const	dict_table_t*	table)
{
	dd_table->set_engine(innobase_hton_name);
	dd_table->set_hidden(dd::Abstract_table::HT_HIDDEN_SE);
	dd_table->set_collation_id(my_charset_bin.number);

	dd::Table::enum_row_format row_format;
	switch (dict_tf_get_rec_format(table->flags)) {
	case REC_FORMAT_REDUNDANT:
		row_format = dd::Table::RF_REDUNDANT;
		break;
	case REC_FORMAT_COMPACT:
		row_format = dd::Table::RF_COMPACT;
		break;
	case REC_FORMAT_COMPRESSED:
		row_format = dd::Table::RF_COMPRESSED;
		break;
	case REC_FORMAT_DYNAMIC:
		row_format = dd::Table::RF_DYNAMIC;
		break;
	default:
		ut_ad(0);
	}

	dd_table->set_row_format(row_format);

	/* FTS AUX tables are always not encrypted/compressed
	as it is designed now. So both "compress" and "encrypt_type"
	option are not set */

	dd::Properties *table_options= &dd_table->options();
	table_options->set_bool("pack_record", true);
	table_options->set_bool("checksum", false);
	table_options->set_bool("delay_key_write", false);
	table_options->set_uint32("avg_row_length", 0);
	table_options->set_uint32("stats_sample_pages", 0);
	table_options->set_uint32("stats_auto_recalc",
				  HA_STATS_AUTO_RECALC_DEFAULT);

	if (auto zip_ssize = DICT_TF_GET_ZIP_SSIZE(table->flags)) {
		table_options->set_uint32(
			"key_block_size", 1 << (zip_ssize - 1));
	} else {
		table_options->set_uint32("key_block_size", 0);
	}
}

/** Create dd table for fts aux index table
@param[in]	parent_table	parent table of fts table
@param[in,out]	table		fts table
@param[in]	charset		fts index charset
@return true on success, false on failure */
bool
dd_create_fts_index_table(
	const dict_table_t*	parent_table,
	dict_table_t*		table,
	const CHARSET_INFO*	charset)
{
	ut_ad(charset != nullptr);

	char	db_name[MAX_DATABASE_NAME_LEN];
	char	table_name[MAX_TABLE_NAME_LEN];

	dd_parse_tbl_name(table->name.m_name, db_name, table_name, NULL);

	/* Create dd::Table object */
	THD*	thd = current_thd;
	dd::Schema_MDL_locker	mdl_locker(thd);
	dd::cache::Dictionary_client*	client = dd::get_dd_client(thd);
	dd::cache::Dictionary_client::Auto_releaser	releaser(client);

	const dd::Schema*	schema = nullptr;
	if (mdl_locker.ensure_locked(db_name)
	    || client->acquire<dd::Schema>(db_name, &schema)) {
		return(false);
	}

	/* Check if schema is nullptr? */
	if (schema == nullptr) {
		my_error(ER_BAD_DB_ERROR, MYF(0), db_name);
		return(false);
	}

	std::unique_ptr<dd::Table>	dd_table_obj(dd::create_object<dd::Table>());
	dd::Table*			dd_table = dd_table_obj.get();

	dd_table->set_name(table_name);
	dd_table->set_schema_id(schema->id());

	dd_set_fts_table_options(dd_table, table);

	/* FTS AUX tables are always not encrypted/compressed
	as it is designed now. So both "compress" and "encrypt_type"
	option are not set */

	/* Fill columns */
	/* 1st column: word */
	dd::Column*	col = dd_table->add_column();
	col->set_name("word");
	col->set_type(dd::enum_column_types::VARCHAR);
	col->set_char_length(FTS_INDEX_WORD_LEN);
	col->set_nullable(false);
	col->set_collation_id(charset->number);

	dd::Column*	key_col1 = col;

	/* 2nd column: first_doc_id */
	col = dd_table->add_column();
	col->set_name("first_doc_id");
	col->set_type(dd::enum_column_types::LONGLONG);
	col->set_char_length(20);
	col->set_numeric_scale(0);
	col->set_nullable(false);
	col->set_unsigned(true);
	col->set_collation_id(charset->number);

	dd::Column*	key_col2 = col;

	/* 3rd column: last_doc_id */
	col = dd_table->add_column();
	col->set_name("last_doc_id");
	col->set_type(dd::enum_column_types::LONGLONG);
	col->set_char_length(20);
	col->set_numeric_scale(0);
	col->set_nullable(false);
	col->set_unsigned(true);
	col->set_collation_id(charset->number);

	/* 4th column: doc_count */
	col = dd_table->add_column();
	col->set_name("doc_count");
	col->set_type(dd::enum_column_types::LONG);
	col->set_char_length(4);
	col->set_numeric_scale(0);
	col->set_nullable(false);
	col->set_unsigned(true);
	col->set_collation_id(charset->number);

	/* 5th column: ilist */
	col = dd_table->add_column();
	col->set_name("ilist");
	col->set_type(dd::enum_column_types::BLOB);
	col->set_char_length(8);
	col->set_nullable(false);
	col->set_collation_id(my_charset_bin.number);

	/* Fill index */
	dd::Index*	index = dd_table->add_index();
	index->set_name("FTS_INDEX_TABLE_IND");
	index->set_algorithm(dd::Index::IA_BTREE);
	index->set_algorithm_explicit(false);
	index->set_visible(true);
	index->set_type(dd::Index::IT_PRIMARY);
	index->set_ordinal_position(1);
	index->set_generated(false);
	index->set_engine(dd_table->engine());

	index->options().set_uint32("flags", 32);

	dd::Index_element*	index_elem;
	index_elem = index->add_element(key_col1);
	index_elem->set_length(FTS_INDEX_WORD_LEN);

	index_elem = index->add_element(key_col2);
	index_elem->set_length(FTS_INDEX_FIRST_DOC_ID_LEN);

	/* Fill table space info, etc */
	dd::Object_id	dd_space_id;
	if (!dd_get_fts_tablespace_id(
			parent_table, table, dd_space_id)) {
		return(false);
	}

	table->dd_space_id = dd_space_id;

	dd_write_table(dd_space_id, dd_table, table);

	MDL_ticket *mdl_ticket= NULL;
	if (dd::acquire_exclusive_table_mdl(
		thd, db_name, table_name, false, &mdl_ticket)) {
		ut_ad(0);
		return(false);
	}

	/* Store table to dd */
	bool	fail = client->store(dd_table);
	if (fail) {
		ut_ad(0);
		return(false);
	}

	return(true);
}

/** Create dd table for fts aux common table
@param[in]	parent_table	parent table of fts table
@param[in,out]	table		fts table
@param[in]	is_config	flag whether it's fts aux configure table
@return true on success, false on failure */
bool
dd_create_fts_common_table(
	const dict_table_t*	parent_table,
	dict_table_t*		table,
	bool			is_config)
{
	char	db_name[MAX_DATABASE_NAME_LEN];
	char	table_name[MAX_TABLE_NAME_LEN];

	dd_parse_tbl_name(table->name.m_name, db_name, table_name, NULL);

	/* Create dd::Table object */
	THD*	thd = current_thd;
	dd::Schema_MDL_locker	mdl_locker(thd);
	dd::cache::Dictionary_client*	client = dd::get_dd_client(thd);
	dd::cache::Dictionary_client::Auto_releaser releaser(client);

	const dd::Schema*	schema = nullptr;
	if (mdl_locker.ensure_locked(db_name)
	    || client->acquire<dd::Schema>(db_name, &schema)) {
		return(false);
	}

	/* Check if schema is nullptr */
	if (schema == nullptr) {
		my_error(ER_BAD_DB_ERROR, MYF(0), db_name);
		return(false);
	}

	std::unique_ptr<dd::Table>	dd_table_obj(dd::create_object<dd::Table>());
	dd::Table*			dd_table = dd_table_obj.get();

	dd_table->set_name(table_name);
	dd_table->set_schema_id(schema->id());

	dd_set_fts_table_options(dd_table, table);

	/* Fill columns */
	if (!is_config) {
		/* 1st column: doc_id */
		dd::Column*	col = dd_table->add_column();
		col->set_name("doc_id");
		col->set_type(dd::enum_column_types::LONGLONG);
		col->set_char_length(20);
		col->set_numeric_scale(0);
		col->set_nullable(false);
		col->set_unsigned(true);
		col->set_collation_id(my_charset_bin.number);

		dd::Column*	key_col1 = col;

		/* Fill index */
		dd::Index*	index = dd_table->add_index();
		index->set_name("FTS_COMMON_TABLE_IND");
		index->set_algorithm(dd::Index::IA_BTREE);
		index->set_algorithm_explicit(false);
		index->set_visible(true);
		index->set_type(dd::Index::IT_PRIMARY);
		index->set_ordinal_position(1);
		index->set_generated(false);
		index->set_engine(dd_table->engine());

		index->options().set_uint32("flags", 32);

		dd::Index_element*	index_elem;
		index_elem = index->add_element(key_col1);
		index_elem->set_length(FTS_INDEX_FIRST_DOC_ID_LEN);
	} else {
		/* Fill columns */
		/* 1st column: key */
		dd::Column*	col = dd_table->add_column();
		col->set_name("key");
		col->set_type(dd::enum_column_types::VARCHAR);
		col->set_char_length(FTS_CONFIG_TABLE_KEY_COL_LEN);
		col->set_nullable(false);
		col->set_collation_id(my_charset_latin1.number);

		dd::Column*	key_col1 = col;

		/* 2nd column: value */
		col = dd_table->add_column();
		col->set_name("value");
		col->set_type(dd::enum_column_types::VARCHAR);
		col->set_char_length(FTS_CONFIG_TABLE_VALUE_COL_LEN);
		col->set_nullable(false);
		col->set_collation_id(my_charset_latin1.number);

		/* Fill index */
		dd::Index*	index = dd_table->add_index();
		index->set_name("FTS_COMMON_TABLE_IND");
		index->set_algorithm(dd::Index::IA_BTREE);
		index->set_algorithm_explicit(false);
		index->set_visible(true);
		index->set_type(dd::Index::IT_PRIMARY);
		index->set_ordinal_position(1);
		index->set_generated(false);
		index->set_engine(dd_table->engine());

		index->options().set_uint32("flags", 32);

		dd::Index_element*	index_elem;
		index_elem = index->add_element(key_col1);
		index_elem->set_length(FTS_CONFIG_TABLE_KEY_COL_LEN);
	}

	/* Fill table space info, etc */
	dd::Object_id	dd_space_id;
	if (!dd_get_fts_tablespace_id(
			parent_table, table, dd_space_id)) {
		ut_ad(0);
		return(false);
	}

	table->dd_space_id = dd_space_id;

	dd_write_table(dd_space_id, dd_table, table);

	MDL_ticket*	mdl_ticket= NULL;
	if (dd::acquire_exclusive_table_mdl(
		thd, db_name, table_name, false, &mdl_ticket)) {
		return(false);
	}

	/* Store table to dd */
	bool	fail = client->store(dd_table);
	if (fail) {
		ut_ad(0);
		return(false);
	}

	return(true);
}

/** Drop dd table & tablespace for fts aux table
@param[in]	name		table name
@param[in]	file_per_table	flag whether use file per table
@return true on success, false on failure. */
bool
dd_drop_fts_table(
	const char*	name,
	bool		file_per_table)
{
	char	db_name[MAX_DATABASE_NAME_LEN];
	char	table_name[MAX_TABLE_NAME_LEN];

	dd_parse_tbl_name(name, db_name, table_name, NULL);

	/* Create dd::Table object */
	THD*	thd = current_thd;
	dd::Schema_MDL_locker	mdl_locker(thd);
	dd::cache::Dictionary_client*	client = dd::get_dd_client(thd);
	dd::cache::Dictionary_client::Auto_releaser releaser(client);

	MDL_ticket*	mdl_ticket= NULL;
	if (dd::acquire_exclusive_table_mdl(
		thd, db_name, table_name, false, &mdl_ticket)) {
		return(false);
	}

	const dd::Table*	dd_table = nullptr;
	if (client->acquire<dd::Table>(
			db_name, table_name, &dd_table)) {
		return(false);
	}

	if (dd_table == nullptr) {
		return(false);
	}

	/* Drop dd table space */
	if (file_per_table) {
		dd::Object_id   dd_space_id = (*dd_table->indexes().begin())
			->tablespace_id();

		dd::Tablespace*	dd_space;
		if (client->acquire_uncached_uncommitted<dd::Tablespace>(
				dd_space_id, &dd_space)) {
			ut_a(false);
		}

		ut_a(dd_space != NULL);

		if (dd::acquire_exclusive_tablespace_mdl(
			    thd, dd_space->name().c_str(), false)) {
			ut_a(false);
		}

		bool fail = client->drop(dd_space);
		ut_a(!fail);
	}

	if (client->drop(dd_table)) {
		ut_ad(0);
		return(false);
	}

	return(true);
}

/** Rename dd table & tablespace files for fts aux table
@param[in]	table		dict table
@param[in]	old_name	old innodb table name
@return true on success, false on failure. */
bool
dd_rename_fts_table(
	const dict_table_t*	table,
	const char*		old_name)
{
	char	new_db_name[MAX_DATABASE_NAME_LEN];
	char	new_table_name[MAX_TABLE_NAME_LEN];
	char	old_db_name[MAX_DATABASE_NAME_LEN];
	char	old_table_name[MAX_TABLE_NAME_LEN];
	char*	new_name = table->name.m_name;

	dd_parse_tbl_name(new_name, new_db_name, new_table_name, nullptr);
	dd_parse_tbl_name(old_name, old_db_name, old_table_name, nullptr);

	ut_ad(strcmp(new_db_name, old_db_name) != 0);
	ut_ad(strcmp(new_table_name, old_table_name) == 0);

	/* Create dd::Table object */
	THD*	thd = current_thd;
	dd::Schema_MDL_locker	mdl_locker(thd);
	dd::cache::Dictionary_client*	client = dd::get_dd_client(thd);
	dd::cache::Dictionary_client::Auto_releaser releaser(client);

	const dd::Schema*	to_sch = nullptr;
	if (client->acquire<dd::Schema>(new_db_name, &to_sch)) {
		return(false);
	}

	MDL_ticket*	mdl_ticket = nullptr;
	if (dd::acquire_exclusive_table_mdl(
		thd, old_db_name, old_table_name, false, &mdl_ticket)) {
		return(false);
	}

	MDL_ticket*	mdl_ticket2 = nullptr;
	if (dd::acquire_exclusive_table_mdl(
		thd, new_db_name, new_table_name, false, &mdl_ticket2)) {
		return(false);
	}

	dd::Table*	dd_table = nullptr;
	if (client->acquire_for_modification<dd::Table>(
		old_db_name, old_table_name, &dd_table)) {
		return(false);
	}

	// Set schema id
	dd_table->set_schema_id(to_sch->id());

	/* Rename dd tablespace file */
	if (dict_table_is_file_per_table(table)) {
		char* new_path = fil_space_get_first_path(table->space);

		if (dd_tablespace_update_filename(
			    table->dd_space_id, new_path)) {
			ut_a(false);
		}

		ut_free(new_path);

	}

	if (client->update(dd_table)) {
		ut_ad(0);
		return(false);
	}

	return(true);
}

/** Set Discard attribute in se_private_data of tablespace
@param[in,out]	dd_space	dd::Tablespace object
@param[in]	discard		true if discarded, else false */
void
dd_tablespace_set_discard(
	dd::Tablespace*		dd_space,
	bool			discard)
{
	dd::Properties& p = dd_space->se_private_data();
	p.set_bool(dd_space_key_strings[DD_SPACE_DISCARD], discard);
}

/** Get discard attribute value stored in se_private_dat of tablespace
@param[in]	dd_space	dd::Tablespace object
@retval		true		if Tablespace is discarded
@retval		false		if attribute doesn't exist or if the
				tablespace is not discarded */
bool
dd_tablespace_get_discard(
	const dd::Tablespace*	dd_space)
{
	const dd::Properties& p = dd_space->se_private_data();
	if (p.exists(dd_space_key_strings[DD_SPACE_DISCARD])) {
		bool	is_discarded;
		p.get_bool(dd_space_key_strings[DD_SPACE_DISCARD],
			   &is_discarded);
		return(is_discarded);
	}
	return(false);
}

#ifdef UNIV_DEBUG
/** @return total number of indexes of all DD tables */
uint32_t dd_get_total_indexes_num()
{
	uint32_t	indexes_count = 0;
	for (uint32_t idx = 0; idx < innodb_dd_table_size; idx++) {
		indexes_count += innodb_dd_table[idx].n_indexes;
	}
	return(indexes_count);
}
#endif /* UNIV_DEBUG */

/** Open a table from its database and table name, this is currently used by
foreign constraint parser to get the referenced table.
@param[in]	name			foreign key table name
@param[in]	database_name		table db name
@param[in]	database_name_len	db name length
@param[in]	table_name		table db name
@param[in]	table_name_len		table name length
@param[in,out]	table			table object or NULL
@param[in,out]	mdl			mdl on table
@param[in,out]	heap			heap memory
@return complete table name with database and table name, allocated from
heap memory passed in */
char*
dd_get_referenced_table(
	const char*	name,
	const char*	database_name,
	ulint		database_name_len,
	const char*	table_name,
	ulint		table_name_len,
	dict_table_t**	table,
	MDL_ticket**	mdl,
	mem_heap_t*	heap)
{
	char*		ref;
	const char*	db_name;

	if (!database_name) {
		/* Use the database name of the foreign key table */

		db_name = name;
		database_name_len = dict_get_db_name_len(name);
	} else {
		db_name = database_name;
	}

	/* Copy database_name, '/', table_name, '\0' */
	ref = static_cast<char*>(
		mem_heap_alloc(heap, database_name_len + table_name_len + 2));

	memcpy(ref, db_name, database_name_len);
	ref[database_name_len] = '/';
	memcpy(ref + database_name_len + 1, table_name, table_name_len + 1);

	/* Values;  0 = Store and compare as given; case sensitive
		    1 = Store and compare in lower; case insensitive
		    2 = Store as given, compare in lower; case semi-sensitive */
	if (innobase_get_lower_case_table_names() == 2) {
		innobase_casedn_str(ref);
		*table = dd_table_open_on_name(current_thd, mdl, ref,
					       true, DICT_ERR_IGNORE_NONE);
		memcpy(ref, db_name, database_name_len);
		ref[database_name_len] = '/';
		memcpy(ref + database_name_len + 1, table_name, table_name_len + 1);

	} else {
#ifndef _WIN32
		if (innobase_get_lower_case_table_names() == 1) {
			innobase_casedn_str(ref);
		}
#else
		innobase_casedn_str(ref);
#endif /* !_WIN32 */
		*table = dd_table_open_on_name(current_thd, mdl, ref,
					       true, DICT_ERR_IGNORE_NONE);
	}

	return(ref);
}
