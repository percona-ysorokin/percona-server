/* Copyright (c) 2017 Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

/*
  This file contains code that updates data dictionary tables
  with the metadata of INFORMATION_SCHEMA tables.

  There are two types of I_S tables.
  - I_S tables exposed by the server.
  - I_S tables exposed by the plugin.

*/

#include "dd/info_schema/metadata.h"

#include "dd/cache/dictionary_client.h"     // dd::cache::Dictionary_client
#include "dd/dd.h"                          // dd::get_dictionary()
#include "dd/dd_schema.h"                   // dd::Schema_MDL_locker
#include "dd/dd_table.h"                    // dd::get_sql_type_by_field_info
#include "dd/impl/dictionary_impl.h"        // dd::Dictionary_impl
#include "dd/impl/bootstrapper.h"           // dd::Column
#include "dd/impl/system_registry.h"        // dd::System_views
#include "dd/properties.h"                  // dd::Properties
#include "dd/types/column.h"                // dd::Column
#include "dd/types/system_view_definition.h"// dd::System_view_definition
#include "log.h"                            // sql_print_warning()
#include "mysql/plugin.h"
#include "mysqld.h"                         // opt_readonly
#include "sql_plugin.h"                     // plugin_foreach
#include "sql_class.h"                      // THD
#include "transaction.h"                    // trans_rollback

namespace {

unsigned int UNKNOWN_PLUGIN_VERSION= -1;

const dd::String_type PLUGIN_VERSION_STRING("plugin_version");
const dd::String_type SERVER_I_S_TABLE_STRING("server_i_s_table");

}

/**
  Check if DDSE (Data Dictionary Storage Engine) is in
  readonly mode.

  @param thd                 Thread
  @param schema_name_abbrev  Abbreviation of schema (I_S or P_S) for use
                             in warning message output

  @returns false on success, otherwise true.
*/
bool check_if_server_ddse_readonly(THD *thd, const char *schema_name_abbrev)
{
  /*
    If we are in read-only mode, we skip updating I_S/P_S metadata. Here,
    'opt_readonly' is the value of the '--read-only' option.
  */
  if (opt_readonly)
  {
    sql_print_warning("Skip updating %s metadata in read-only mode.",
                      schema_name_abbrev);
    return true;
  }

  /*
    We must also check if the DDSE is started in a way that makes the DD
    read only. For now, we only support InnoDB as SE for the DD. The call
    to retrieve the handlerton for the DDSE should be replaced by a more
    generic mechanism.
  */
  handlerton *ddse= ha_resolve_by_legacy_type(thd, DB_TYPE_INNODB);
  if (ddse->is_dict_readonly && ddse->is_dict_readonly())
  {
    sql_print_warning("Skip updating %s metadata in InnoDB read-only mode.",
                      schema_name_abbrev);
    return true;
  }

  return false;
}

namespace {

// Hold context during IS metadata update to DD.
class Update_context
{
public:
  Update_context(THD *thd, bool commit_gaurd) :
    m_thd(thd),
    m_saved_var_tx_read_only(thd->variables.transaction_read_only),
    m_saved_tx_read_only(thd->tx_read_only),
    m_autocommit_guard(commit_gaurd ? thd : nullptr),
    m_mdl_handler(thd),
    m_auto_releaser(thd->dd_client()),
    m_schema_obj(nullptr),
    m_plugin_names(nullptr)
  {
    /*
      Set tx_read_only to false to allow installing DD tables even
      if the server is started with --transaction-read-only=true.
    */
    m_thd->variables.transaction_read_only= false;
    m_thd->tx_read_only= false;

    if (m_mdl_handler.ensure_locked(INFORMATION_SCHEMA_NAME.str) ||
        m_thd->dd_client()->acquire(INFORMATION_SCHEMA_NAME.str, &m_schema_obj))
      m_schema_obj= nullptr;

    DBUG_ASSERT(m_schema_obj);
  }

  ~Update_context()
  {
    // Restore thd state.
    m_thd->variables.transaction_read_only= m_saved_var_tx_read_only;
    m_thd->tx_read_only= m_saved_tx_read_only;
  }

  const dd::Schema *info_schema()
  { return m_schema_obj; }

  void set_plugin_names(std::vector<dd::String_type> *names)
  { m_plugin_names= names; }

  std::vector<dd::String_type> *plugin_names()
  { return m_plugin_names; }

private:
  THD *m_thd;
  bool m_saved_var_tx_read_only;
  bool m_saved_tx_read_only;
  Disable_autocommit_guard m_autocommit_guard;
  dd::Schema_MDL_locker m_mdl_handler;
  dd::cache::Dictionary_client::Auto_releaser m_auto_releaser;

  // DD object for INFORMATION_SCHEMA.
  const dd::Schema *m_schema_obj;

  // List of plugins whose I_S tables are already present in DD.
  std::vector<dd::String_type> *m_plugin_names;
};


/**
  Store metadata from ST_SCHEMA_TABLE into DD tables.
  Store option plugin_version=version, if it is not UNKNOWN_PLUGIN_VERSION,
  otherwise store server_i_s_table=true.

  @param thd           Thread
  @param ctx           Update context.
  @param schema_table  Pointer to ST_SCHEMA_TABLE contain
                       metadata of IS table.
  @param version       Plugin version.

  @returns false on success, else true.
*/
bool store_in_dd(THD *thd, Update_context *ctx,
                 ST_SCHEMA_TABLE *schema_table, unsigned int version)
{
  // Skip I_S tables that are hidden from users.
  if (schema_table->hidden)
    return false;

  std::unique_ptr<dd::View> view_obj(
                              ctx->info_schema()->create_system_view(thd));

  // Set view properties
  view_obj->set_client_collation_id(
              ctx->info_schema()->default_collation_id());

  view_obj->set_connection_collation_id(
              ctx->info_schema()->default_collation_id());

  view_obj->set_name(schema_table->table_name);

  dd::Properties *view_options= &view_obj->options();
  if (version != UNKNOWN_PLUGIN_VERSION)
    view_options->set_uint32(PLUGIN_VERSION_STRING, version);
  else
    view_options->set_bool(SERVER_I_S_TABLE_STRING, true);

  /*
    Fill columns details
  */
  ST_FIELD_INFO *fields_info= schema_table->fields_info;
  const CHARSET_INFO* cs= get_charset(system_charset_info->number, MYF(0));
  for (; fields_info->field_name; fields_info++)
  {
    dd::Column *col_obj= view_obj->add_column();

    col_obj->set_name(fields_info->field_name);

    /*
      The 5.7 create_schema_table() creates Item_empty_string() item for
      MYSQL_TYPE_STRING. Item_empty_string->field_type() maps to
      MYSQL_TYPE_VARCHAR. So, we map MYSQL_TYPE_STRING to
      MYSQL_TYPE_VARCHAR when storing metadata into DD.
    */
    enum_field_types ft= fields_info->field_type;
    uint32 fl= fields_info->field_length;
    if (fields_info->field_type == MYSQL_TYPE_STRING)
    {
      ft= MYSQL_TYPE_VARCHAR;
      fl= fields_info->field_length * cs->mbmaxlen;
    }

    col_obj->set_type(dd::get_new_field_type(ft));

    col_obj->set_char_length(fields_info->field_length);

    col_obj->set_nullable(fields_info->field_flags & MY_I_S_MAYBE_NULL);

    col_obj->set_unsigned(fields_info->field_flags & MY_I_S_UNSIGNED);

    col_obj->set_zerofill(false);

    // Collation ID
    col_obj->set_collation_id(system_charset_info->number);

    col_obj->set_column_type_utf8(
               dd::get_sql_type_by_field_info(thd, ft, fl, 0,
                 col_obj->is_nullable(), col_obj->is_unsigned(), cs));

    col_obj->set_default_value_utf8(dd::String_type(STRING_WITH_LEN("")));
  }

  /*
    Acquire MDL on the view name, if not yet acquired.

    In case of INSTALL PLUGIN, the lock would be already taken. We acquire
    lock here during server restart and bootstrap.
  */
  if (thd->mdl_context.owns_equal_or_stronger_lock(MDL_key::TABLE,
                                                   ctx->info_schema()->name().c_str(),
                                                   view_obj->name().c_str(),
                                                   MDL_EXCLUSIVE) == false)
  {
    MDL_request mdl_request;
    MDL_REQUEST_INIT(&mdl_request, MDL_key::TABLE,
                     ctx->info_schema()->name().c_str(), view_obj->name().c_str(),
                     MDL_EXCLUSIVE,
                     MDL_TRANSACTION);
    if (thd->mdl_context.acquire_lock(&mdl_request,
                                      thd->variables.lock_wait_timeout))
      return true;
  }

  // Store the metadata into DD
  if (thd->dd_client()->store(view_obj.get()))
  {
    DBUG_ASSERT(thd->is_system_thread() || thd->killed || thd->is_error());
    return(true);
  }

  return false;
}


/**
  Store plugin IS table metadata into DD.
  Skip storing, if the I_S name is already present in
  Update_context plugin names.

  @param THD    Thread ID
  @param plugin Reference to a plugin.
  @param arg    Pointer to Context for I_S update.

  @return
    false on success
    true when fails to store the metadata.
*/
bool store_plugin_metadata(THD *thd,
                           plugin_ref plugin,
                           void *arg)
{
  DBUG_ASSERT(plugin && arg);

  st_plugin_int *pi= plugin_ref_to_int(plugin);
  Update_context *ctx= static_cast<Update_context*>(arg);

  // Stop if we already have IS metadata in DD.
  if (ctx->plugin_names())
  {
    dd::String_type name(pi->name.str, pi->name.length);
    if (std::find(ctx->plugin_names()->begin(),
                  ctx->plugin_names()->end(),
                  name) != ctx->plugin_names()->end() )
      return false;
  }

  // Store in DD tables.
  ST_SCHEMA_TABLE *schema_table= plugin_data<ST_SCHEMA_TABLE*>(plugin);
  return store_in_dd(thd, ctx, schema_table, pi->plugin->version);
}


/**
  Iterate through dynamic I_S plugins, and store I_S table
  metadata into dictionary, during MySQL server startup. These
  are plugins that are loaded using server command line options.

  Does following,
  - Remove I_S table metadata for plugins that are not loaded.
  - Store I_S table metadata of plugins that are newly loaded.

  @param thd  Thread

  @returns  false on success, otherwise true.
*/
bool update_plugins_I_S_metadata(THD *thd)
{
  // Warn if we have read-only mode enabled and continue.
  if (check_if_server_ddse_readonly(thd, INFORMATION_SCHEMA_NAME.str))
    return false;

  /*
    Stage 1:
    Remove I_S table metadata for plugins that are not loaded.
  */

  /*
    Create std::vector to hold 'plugin_name' of I_S tables that are
    persistent in DD and if the plugin is loaded.
  */
  Update_context ctx(thd, true);
  std::vector<dd::String_type> plugin_names;
  ctx.set_plugin_names(&plugin_names);
  std::vector<const dd::View*> sch_views;
  if (thd->dd_client()->fetch_schema_components(ctx.info_schema(), &sch_views))
    return true;

  bool error= false;
  for (const dd::View *view : sch_views)
  {
    // Skip if this is not a plugin I_S metadata.
    const dd::Properties *view_options= &view->options();
    if (!view_options->exists(PLUGIN_VERSION_STRING))
      continue;

    // Lookup if plugin is loaded during this server startup.
    LEX_CSTRING plugin_name= {view->name().c_str(),
                              static_cast<unsigned int>(view->name().length())};
    plugin_ref plugin= my_plugin_lock_by_name(thd, plugin_name,
                                              MYSQL_INFORMATION_SCHEMA_PLUGIN);
    if (plugin != nullptr)
    {
      unsigned int plugin_version;
      st_plugin_int *plugin_int= plugin_ref_to_int(plugin);
      view_options->get_uint32(PLUGIN_VERSION_STRING, &plugin_version);

      // Testing to make sure we update plugins when version changes.
      DBUG_EXECUTE_IF("test_i_s_metadata_version",
                      { plugin_version = UNKNOWN_PLUGIN_VERSION; });

      if (plugin_int->plugin->version == plugin_version &&
          plugin_int->state == PLUGIN_IS_READY)
      {
        plugin_names.push_back(view->name());
        continue;
      }
    }

    // Remove metadata from DD if version mismatch.
    if (dd::info_schema::remove_I_S_view_metadata(thd, view->name()))
      break;
  }


  /*
    Stage 2:
    Store I_S table metadata of plugins that are newly loaded.
  */
  error= error || plugin_foreach_with_mask(thd,
                                  store_plugin_metadata,
                                  MYSQL_INFORMATION_SCHEMA_PLUGIN,
                                  PLUGIN_IS_READY, &ctx);

  return dd::end_transaction(thd, error);
}


/**
  Does following during server restart.

  - If I_S version is not changed, do nothing.
  - Else, remove all I_S metadata that belongs to server.
  - Store new server I_S metadata.
  - Recreate I_S system views, which will update old I_S metadata
    because we execute CREATE OR REPLACE VIEW.

  @param thd   Thread

  @returns false on success, otherwise true.
*/
bool update_server_I_S_metadata(THD *thd)
{
  bool error= false;
  dd::Dictionary_impl *d= dd::Dictionary_impl::instance();

  // Stop if I_S version is same.
  uint actual_version= d->get_actual_I_S_version(thd);

  // Testing to make sure we update plugins when version changes.
  DBUG_EXECUTE_IF("test_i_s_metadata_version",
                  { actual_version = UNKNOWN_PLUGIN_VERSION; });

  if (d->get_target_I_S_version() == actual_version)
    return false;

  /*
    Stop server restart if I_S version is changed and the server is
    started in read-only mode.
  */
  if (check_if_server_ddse_readonly(thd, INFORMATION_SCHEMA_NAME.str))
    return true;

  Update_context ctx(thd, true);
  const dd::Schema *sch_obj= ctx.info_schema();
  if (sch_obj == nullptr)
    return true;

  /*
    Stage 1:
    Remove all server I_S metadata from DD tables.
  */

  std::vector<const dd::View*> sch_views;
  if (thd->dd_client()->fetch_schema_components(sch_obj, &sch_views))
    return true;

  for (const dd::View *view : sch_views)
  {
    // Skip if this is not a server I_S table.
    const dd::Properties *view_options= &view->options();

    if (!view_options->exists(PLUGIN_VERSION_STRING))
    {
      // Remove metadata from DD as I_S version is changed.
      error= dd::info_schema::remove_I_S_view_metadata(thd, view->name());

      if (error)
        break;
    }
  }

  /*
    Stage 2:
    1) Store server I_S tables
    2) Recreate system view.
    3) Update the target IS version in DD.
  */
  error= error ||
         dd::info_schema::store_server_I_S_metadata(thd) ||
         dd::info_schema::create_system_views(thd) ||
         d->set_I_S_version(thd, d->get_target_I_S_version());

  return dd::end_transaction(thd, error);
}

}


namespace dd {
namespace info_schema {

/*
  Create INFORMATION_SCHEMA system views.
*/
bool create_system_views(THD *thd)
{
  // Force use of utf8 charset.
  const CHARSET_INFO *client_cs= thd->variables.character_set_client;
  const CHARSET_INFO *cs= thd->variables.collation_connection;
  const CHARSET_INFO *m_client_cs, *m_connection_cl;

  resolve_charset("utf8", system_charset_info, &m_client_cs);
  resolve_collation("utf8_general_ci", system_charset_info, &m_connection_cl);

  thd->variables.character_set_client= m_client_cs;
  thd->variables.collation_connection= m_connection_cl;
  thd->update_charset();

  // Iterate over system view definitions.
  dd::System_views::Const_iterator it= dd::System_views::instance()->begin();

  bool error= false;
  for (; it != dd::System_views::instance()->end(); ++it)
  {
    const dd::system_views::System_view_definition *view_def=
            (*it)->entity()->view_definition();

    // Build the CREATE VIEW DDL statement and execute it.
    if (view_def == nullptr ||
        execute_query(thd, view_def->build_ddl_create_view()))
    {
      error= true;
      break;
    }
  }

  // Restore the original character set.
  thd->variables.character_set_client= client_cs;
  thd->variables.collation_connection= cs;
  thd->update_charset();

  return error;
}


/*
  Store the server I_S table metadata into dictionary, once during MySQL
  server bootstrap.
*/
bool store_server_I_S_metadata(THD *thd)
{
  bool error= false;
  Update_context ctx(thd, false);
  ST_SCHEMA_TABLE *schema_tables= get_schema_table(SCH_FIRST);
  for (; schema_tables->table_name && !error; schema_tables++)
  {
    error= store_in_dd(thd, &ctx, schema_tables, UNKNOWN_PLUGIN_VERSION);
  }

  return dd::end_transaction(thd, error);
}


// Update server and plugin I_S table metadata on server restart.
bool update_I_S_metadata(THD *thd)
{
  return update_server_I_S_metadata(thd) ||
         update_plugins_I_S_metadata(thd);
}


/*
  Store dynamic I_S plugin table metadata into dictionary, during INSTALL
  command execution.
*/
bool store_dynamic_plugin_I_S_metadata(THD *thd, st_plugin_int *plugin_int)
{
  plugin_ref plugin= plugin_int_to_ref(plugin_int);
  Update_context ctx(thd, false);
  DBUG_ASSERT(plugin_int->plugin->type == MYSQL_INFORMATION_SCHEMA_PLUGIN);

  return store_plugin_metadata(thd, plugin, (void*) &ctx);
}


/*
  Remove the given plugin I_S table metadata from dictionary table.
*/
bool remove_I_S_view_metadata(THD *thd, const dd::String_type &view_name)
{
  // Make sure you have lock on I_S schema.
  Schema_MDL_locker mdl_locker(thd);
  if (mdl_locker.ensure_locked(INFORMATION_SCHEMA_NAME.str))
      return true;

  // Acquire exclusive lock on it before dropping.
  MDL_request mdl_request;
  MDL_REQUEST_INIT(&mdl_request, MDL_key::TABLE,
                   INFORMATION_SCHEMA_NAME.str,
                   view_name.c_str(),
                   MDL_EXCLUSIVE,
                   MDL_TRANSACTION);
  if (thd->mdl_context.acquire_lock(&mdl_request,
                                    thd->variables.lock_wait_timeout))
    return(true);

  // Acquire the object.
  const dd::Abstract_table *at= nullptr;
  if (thd->dd_client()->acquire(INFORMATION_SCHEMA_NAME.str,
                                view_name.c_str(), &at))
    return(true);

  DBUG_ASSERT(at->type() == dd::enum_table_type::SYSTEM_VIEW);

  // Remove view from DD tables.
  Disable_gtid_state_update_guard disabler(thd);
  if (thd->dd_client()->drop(at))
  {
    DBUG_ASSERT(thd->is_system_thread() || thd->killed || thd->is_error());
    return(true);
  }

  return(false);
}



bool initialize(THD *thd)
{
  /*
    Set tx_read_only to false to allow installing system views even
    if the server is started with --transaction-read-only=true.
  */
  thd->variables.transaction_read_only= false;
  thd->tx_read_only= false;

  Disable_autocommit_guard autocommit_guard(thd);

  Dictionary_impl *d= dd::Dictionary_impl::instance();
  DBUG_ASSERT(d);

  if (create_system_views(thd) || store_server_I_S_metadata(thd))
    return true;

  sql_print_information("Created system views with I_S version %d",
                        (int) d->get_target_dd_version());
  return false;
}

} // namespace info_schema
} // namespace dd
