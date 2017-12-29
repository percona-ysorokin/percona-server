/* Copyright (c) 2016, 2017, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include "sql/persisted_variable.h"

#include "my_config.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <algorithm>
#include <memory>
#include <new>
#include <utility>

#include "lex_string.h"
#include "m_ctype.h"
#include "m_string.h"
#include "my_compiler.h"
#include "my_dbug.h"
#include "my_default.h"                 // check_file_permissions
#include "my_getopt.h"
#include "my_io.h"
#include "my_loglevel.h"
#include "my_macros.h"
#include "my_sys.h"
#include "my_thread.h"
#include "mysql/components/services/log_builtins.h"
#include "mysql/components/services/log_shared.h"
#include "mysql/components/services/psi_file_bits.h"
#include "mysql/components/services/psi_memory_bits.h"
#include "mysql/components/services/psi_mutex_bits.h"
#include "mysql/components/services/system_variable_source_type.h"
#include "mysql/psi/mysql_file.h"
#include "mysql/psi/mysql_memory.h"
#include "mysql/psi/mysql_mutex.h"
#include "mysql/psi/psi_base.h"
#include "mysql/status_var.h"
#include "mysql_version.h"
#include "mysqld_error.h"
#include "prealloced_array.h"
#include "sql/auth/sql_security_ctx.h"
#include "sql/current_thd.h"
#include "sql/derror.h"       // ER_THD
#include "sql/item.h"
#include "sql/log.h"
#include "sql/mysqld.h"
#include "sql/set_var.h"
#include "sql/sql_class.h"
#include "sql/sql_error.h"
#include "sql/sql_lex.h"
#include "sql/sql_list.h"
#include "sql/sql_show.h"
#include "sql/sys_vars_shared.h"
#include "sql/thr_malloc.h"
#include "sql_string.h"
#include "template_utils.h"
#include "thr_mutex.h"
#include "typelib.h"

using std::string;
using std::map;
using std::vector;

const string version("\"Version\"");
const string name("\"Name\"");
const string value("\"Value\"");
const string metadata("\"Metadata\"");
const string timestamp("\"Timestamp\"");
const string user("\"User\"");
const string host("\"Host\"");
const string mysqld_section("\"mysql_server\"");
const string static_section("\"mysql_server_static_options\"");
const string colon(" : ");
const string comma(" , ");
const string open_brace("{ ");
const string close_brace(" }");

const int file_version= 1;

PSI_file_key key_persist_file_cnf;

#ifdef HAVE_PSI_FILE_INTERFACE
static PSI_file_info all_persist_files[]=
{
  { &key_persist_file_cnf, "cnf", 0, 0, PSI_DOCUMENT_ME}
};
#endif /* HAVE_PSI_FILE_INTERFACE */

PSI_mutex_key key_persist_file, key_persist_variables;

#ifdef HAVE_PSI_MUTEX_INTERFACE
static PSI_mutex_info all_persist_mutexes[]=
{
  { &key_persist_file, "m_LOCK_persist_file", 0, 0, PSI_DOCUMENT_ME},
  { &key_persist_variables, "m_LOCK_persist_variables", 0, 0, PSI_DOCUMENT_ME}
};
#endif /* HAVE_PSI_MUTEX_INTERFACE */

PSI_memory_key key_memory_persisted_variables;

#ifdef HAVE_PSI_MEMORY_INTERFACE
static PSI_memory_info all_options[]=
{
  {&key_memory_persisted_variables, "persisted_options_root", 0, PSI_FLAG_ONLY_GLOBAL_STAT, PSI_DOCUMENT_ME}
};
#endif /* HAVE_PSI_MEMORY_INTERFACE */

#ifdef HAVE_PSI_INTERFACE
void my_init_persist_psi_keys(void)
{
  const char* category MY_ATTRIBUTE((unused))= "persist";
  int count MY_ATTRIBUTE((unused));

#ifdef HAVE_PSI_FILE_INTERFACE
  count= sizeof(all_persist_files)/sizeof(all_persist_files[0]);
  mysql_file_register(category, all_persist_files, count);
#endif

#ifdef HAVE_PSI_MUTEX_INTERFACE
  count= static_cast<int>(array_elements(all_persist_mutexes));
  mysql_mutex_register(category, all_persist_mutexes, count);
#endif

#ifdef HAVE_PSI_MEMORY_INTERFACE
  count= static_cast<int>(array_elements(all_options));
  mysql_memory_register(category, all_options, count);
#endif
}
#endif

Persisted_variables_cache* Persisted_variables_cache::m_instance= NULL;

/* Standard Constructors for st_persist_var */

st_persist_var::st_persist_var()
{
  if (current_thd)
  {
    timeval tv= current_thd->query_start_timeval_trunc(0);
    timestamp= tv.tv_sec * 1000000ULL;
  }
  else
    timestamp= my_micro_time();
}

st_persist_var::st_persist_var(THD *thd)
{
  timeval tv= thd->query_start_timeval_trunc(0);
  timestamp= tv.tv_sec * 1000000ULL;
  user= thd->security_context()->user().str;
  host= thd->security_context()->host().str;
}

st_persist_var::st_persist_var(const st_persist_var& var)
{
  this->key= var.key;
  this->value= var.value;
  this->timestamp= var.timestamp;
  this->user= var.user;
  this->host= var.host;
}

st_persist_var::st_persist_var(const string key, const string value,
                               const ulonglong timestamp, const string user,
                               const string host)
{
  this->key= key;
  this->value= value;
  this->timestamp= timestamp;
  this->user= user;
  this->host= host;
}

/**
  Initialize class members. This function reads datadir if present in
  config file or set at command line, in order to know from where to
  load this config file. If datadir is not set then read from MYSQL_DATADIR.

   @param [in] argc                      Pointer to argc of original program
   @param [in] argv                      Pointer to argv of original program

   @return 0 Success
   @return 1 Failure

*/
int Persisted_variables_cache::init(int *argc, char ***argv)
{
#ifdef HAVE_PSI_INTERFACE
   my_init_persist_psi_keys();
#endif

  int temp_argc= *argc;
  MEM_ROOT alloc{PSI_NOT_INSTRUMENTED, 512};
  char *ptr, **res, *datadir= NULL;
  char dir[FN_REFLEN]= { 0 };
  const char *dirs= NULL;
  bool persist_load= true;

  my_option persist_options[]= {
    {"persisted_globals_load", 0, "", &persist_load, &persist_load,
      0, GET_BOOL, OPT_ARG, 1, 0, 0, 0, 0, 0},
    {"datadir", 0, "", &datadir, 0, 0, GET_STR, OPT_ARG,
      0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0}
  };

  /* create temporary args list and pass it to handle_options */
  init_alloc_root(key_memory_persisted_variables, &alloc, 512, 0);
  if (!(ptr= (char *) alloc_root(&alloc, sizeof(alloc) +
    (*argc + 1) * sizeof(char *))))
    return 1;
  memset(ptr, 0, (sizeof(char *) * (*argc + 1)));
  res= (char **) (ptr);
  memcpy((uchar *) res, (char *) (*argv), (*argc) * sizeof(char *));

  my_getopt_skip_unknown= true;
  if (my_handle_options(&temp_argc, &res, persist_options,
                        NULL, NULL, true))
  {
    free_root(&alloc, MYF(0));
    return 1;
  }
  my_getopt_skip_unknown= 0;
  free_root(&alloc, MYF(0));

  persisted_globals_load= persist_load;
  /*
    if datadir is set then search in this data dir else search in
    MYSQL_DATADIR
  */
  dirs= ((datadir) ? datadir : MYSQL_DATADIR);
  /* expand path if it is a relative path */
  if (dirs[0] == FN_CURLIB && my_getwd(dir, sizeof(dir), MYF(0)))
    return 1;
  if (fn_format(datadir_buffer, dirs, dir, "",
      MY_UNPACK_FILENAME | MY_SAFE_PATH | MY_RELATIVE_PATH) == NULL)
    return 1;
  unpack_dirname(datadir_buffer, datadir_buffer);
  m_persist_filename= string(datadir_buffer) + MYSQL_PERSIST_CONFIG_NAME + ".cnf";

  mysql_mutex_init(key_persist_variables,
    &m_LOCK_persist_variables, MY_MUTEX_INIT_FAST);

  mysql_mutex_init(key_persist_file,
    &m_LOCK_persist_file, MY_MUTEX_INIT_FAST);

  m_instance= this;
  return 0;
}

/**
  Return a singleton object
*/
Persisted_variables_cache* Persisted_variables_cache::get_instance()
{
  DBUG_ASSERT(m_instance != NULL);
  return m_instance;
}

/**
  Retrieve variables name/value and update the in-memory copy with
  this new values. If value is default then remove this entry from
  in-memory copy, else update existing key with new value

   @param [in] thd           Pointer to connection handler
   @param [in] setvar        Pointer to set_var which is being SET

   @return void
*/
void Persisted_variables_cache::set_variable(THD *thd, set_var *setvar)
{
  char val_buf[1024]= { 0 };
  String str(val_buf, sizeof(val_buf), system_charset_info), *res;
  String utf8_str;

  struct st_persist_var tmp_var(thd);
  sys_var *system_var= setvar->var;

  const char* var_name=
    Persisted_variables_cache::get_variable_name(system_var);
  const char* var_value= val_buf;
  if (setvar->type == OPT_PERSIST_ONLY && setvar->value)
  {
    res= setvar->value->val_str(&str);
    if (res && res->length())
    {
      /*
        value held by Item class can be of different charset,
        so convert to utf8mb4
      */
      const CHARSET_INFO *tocs= &my_charset_utf8mb4_bin;
      uint dummy_err;
      utf8_str.copy(res->ptr(), res->length(), res->charset(), tocs,
        &dummy_err);
      var_value= utf8_str.c_ptr_quick();
    }
  }
  else
  {
    Persisted_variables_cache::get_variable_value(thd, system_var, &utf8_str);
    var_value= utf8_str.c_ptr_quick();
  }

  /* structured variables may have basename if specified */
  tmp_var.key= (setvar->base.str ?
                string(setvar->base.str).append(".").append(var_name) :
                string(var_name));
  tmp_var.value= var_value;

  /* modification to in-memory must be thread safe */
  mysql_mutex_lock(&m_LOCK_persist_variables);
  /* if present update variable with new value else insert into hash */
  if ((setvar->type == OPT_PERSIST_ONLY && setvar->var->is_readonly()) ||
    setvar->var->is_plugin_var_read_only())
    m_persist_ro_variables[tmp_var.key]= tmp_var;
  else
  {
    /*
     if element is present remove from current position and insert
     at end of vector to restore insertion order.
    */
    string str= tmp_var.key;
    auto it= std::find_if(m_persist_variables.begin(), m_persist_variables.end(),
      [str](st_persist_var const& s) { return s.key == str; });
    if (it != m_persist_variables.end())
      m_persist_variables.erase(it);
    m_persist_variables.push_back(tmp_var);
  }
  mysql_mutex_unlock(&m_LOCK_persist_variables);
}

/**
  Retrieve variables value from sys_var

   @param [in] thd           Pointer to connection handler
   @param [in] system_var    Pointer to sys_var which is being SET
   @param [in] str           Pointer to String instance into which value
                             is copied

   @return
     Pointer to String instance holding the value
*/
String* Persisted_variables_cache::get_variable_value(THD *thd,
  sys_var *system_var, String *str)
{
  const char* value;
  char val_buf[1024];
  size_t val_length;
  char show_var_buffer[sizeof(SHOW_VAR)];
  SHOW_VAR *show= (SHOW_VAR *)show_var_buffer;
  const CHARSET_INFO *fromcs;
  const CHARSET_INFO *tocs= &my_charset_utf8mb4_bin;
  uint dummy_err;

  show->type= SHOW_SYS;
  show->name= system_var->name.str;
  show->value= (char *) system_var;

  mysql_mutex_lock(&LOCK_global_system_variables);
  value= get_one_variable(thd, show, OPT_GLOBAL, show->type, NULL,
                          &fromcs, val_buf, &val_length);
  mysql_mutex_unlock(&LOCK_global_system_variables);

  /* convert the retrieved value to utf8mb4 */
  str->copy(value, val_length, fromcs, tocs, &dummy_err);
  return str;
}

/**
  Retrieve variables name from sys_var

   @param [in] system_var    Pointer to sys_var which is being SET
   @return
     Pointer to buffer holding the name
*/
const char* Persisted_variables_cache::get_variable_name(sys_var *system_var)
{
  return system_var->name.str;
}

/**
  Given information of variable which needs to be persisted, this function
  will construct a json foematted string out of it.

  Format will be as below for variable named "X":
  "X" : {
    "Value" : "value",
    "Metadata" : {
      "Timestamp" : timestamp_value,
      "User" : "user_name",
      "Host" : "host_name"
      }
    }

   @param [in]  name               Variable name
   @param [in]  value              Variable value
   @param [in]  timestamp          Timestamp value when this variable was set
   @param [in]  user               User who set this variable
   @param [in]  host               Host on which this variable was set
   @param [out] dest               String object where json formatted string
                                   is stored

   @return
     Pointer to String instance holding the json formatted string

*/
String* Persisted_variables_cache::construct_json_string(std::string name,
                                                         std::string value,
                                                         ulonglong timestamp,
                                                         std::string user,
                                                         std::string host,
                                                         String *dest)
{
  String str;
  std::unique_ptr<Json_string> var_name(new (std::nothrow)
    Json_string(name));
  Json_wrapper vn(var_name.release());
  vn.to_string(&str, true, String().ptr());
  dest->append(str);
  dest->append(string(colon + open_brace + ::value + colon).c_str());

  /* reset str */
  str= String();
  std::unique_ptr<Json_string> var_val(new (std::nothrow)
    Json_string(value));
  Json_wrapper vv(var_val.release());
  vv.to_string(&str, true, String().ptr());
  dest->append(str);
  dest->append(comma.c_str());

  /* reset str */
  str= String();
  dest->append(string(metadata + colon + open_brace + ::timestamp +
    colon).c_str());
  std::unique_ptr<Json_uint> var_ts(new (std::nothrow)
    Json_uint(timestamp));
  Json_wrapper vt(var_ts.release());
  vt.to_string(&str, true, String().ptr());
  dest->append(str);
  dest->append(comma.c_str());

  /* reset str */
  str= String();
  dest->append(string(::user + colon).c_str());
  std::unique_ptr<Json_string> var_user(new (std::nothrow)
    Json_string(user));
  Json_wrapper vu(var_user.release());
  vu.to_string(&str, true, String().ptr());
  dest->append(str);
  dest->append(comma.c_str());

  /* reset str */
  str= String();
  dest->append(string(::host + colon).c_str());
  std::unique_ptr<Json_string> var_host(new (std::nothrow)
    Json_string(host));
  Json_wrapper vh(var_host.release());
  vh.to_string(&str, true, String().ptr());
  dest->append(str);
  dest->append(string(close_brace + close_brace + comma).c_str());

  return dest;
}

/**
  Convert in-memory copy into a stream of characters and write this
  stream to persisted config file

  @return Error state
    @retval true An error occurred
    @retval false Success
*/
bool Persisted_variables_cache::flush_to_file()
{
  mysql_mutex_lock(&m_LOCK_persist_variables);
  mysql_mutex_lock(&m_LOCK_persist_file);

  string tmp_str(open_brace + version + colon + std::to_string(file_version) +
                 comma + mysqld_section + colon + open_brace);
  String dest(tmp_str.c_str(), &my_charset_utf8mb4_bin);

  for (auto iter= m_persist_variables.begin();
       iter != m_persist_variables.end(); iter++)
  {
    String json_formatted_string;
    Persisted_variables_cache::construct_json_string(iter->key, iter->value,
      iter->timestamp, iter->user, iter->host, &json_formatted_string);
    dest.append(json_formatted_string.c_ptr_quick());
  }

  if (m_persist_ro_variables.size())
  {
    dest.append(string(static_section + colon + open_brace).c_str());
  }

  for (auto iter = m_persist_ro_variables.begin();
       iter != m_persist_ro_variables.end(); iter++)
  {
    String json_formatted_string;
    Persisted_variables_cache::construct_json_string(iter->second.key,
      iter->second.value, iter->second.timestamp, iter->second.user,
      iter->second.host, &json_formatted_string);
    dest.append(json_formatted_string.c_ptr_quick());
  }

  if (m_persist_ro_variables.size())
  {
    /* remove last " , " characters */
    dest.chop(); dest.chop(); dest.chop();
    dest.append(close_brace.c_str());
  }
  if (m_persist_variables.size() && !m_persist_ro_variables.size())
  {
    dest.chop(); dest.chop(); dest.chop();
  }
  dest.append(string(close_brace + close_brace).c_str());
  /*
    If file does not exists create one. When persisted_globals_load is 0
    we dont read contents of mysqld-auto.cnf file, thus append any new
    variables which are persisted to this file.
  */
  bool ret= false;

  if(open_persist_file(O_CREAT | O_WRONLY))
  {
    ret= true;
  }
  else
  {
    /* write to file */
    if (mysql_file_fputs(dest.c_ptr(), m_fd) < 0)
    {
      ret= true;
    }
  }

  close_persist_file();
  mysql_mutex_unlock(&m_LOCK_persist_file);
  mysql_mutex_unlock(&m_LOCK_persist_variables);
  return ret;
}

/**
  Open persisted config file

  @param [in] flag    File open mode
  @return Error state
    @retval true An error occurred
    @retval false Success
*/
bool Persisted_variables_cache::open_persist_file(int flag)
{
  m_fd= mysql_file_fopen(key_persist_file_cnf,
                       m_persist_filename.c_str(), flag, MYF(0));
  return (m_fd ? 0 : 1);
}

/**
  Close persisted config file
   @return void
*/
void Persisted_variables_cache::close_persist_file()
{
  mysql_file_fclose(m_fd, MYF(0));
  m_fd= NULL;
}

/**
  load_persist_file() read persisted config file

  @return Error state
    @retval true An error occurred
    @retval false Success
*/
bool Persisted_variables_cache::load_persist_file()
{
  if (read_persist_file() > 0)
    return 1;
  return 0;
}

/**
  set_persist_options() will set the options read from persisted config file

  This function does nothing when --no-defaults is set or if
  persisted_globals_load is set to false

   @param [in] plugin_options      Flag which tells what options are being set.
                                   If set to false non plugin variables are set
                                   else plugin variables are set

  @return Error state
    @retval true An error occurred
    @retval false Success
*/
bool Persisted_variables_cache::set_persist_options(bool plugin_options)
{
  THD *thd;
  LEX lex_tmp, *sav_lex= NULL;
  List<set_var_base> tmp_var_list;
  vector<st_persist_var> *persist_variables= NULL;
  ulong access= 0;
  bool result= 0, new_thd= 0;

  /*
    if persisted_globals_load is set to false or --no-defaults is set
    then do not set persistent options
  */
  if (no_defaults || !persisted_globals_load)
    return 0;

  /*
    This function is called in only 2 places
      1. During server startup.
      2. During install plugin after server has started.
    During server startup before server components are initialized
    current_thd is NULL thus instantiate new temporary THD.
    After server has started we have current_thd so make use of current_thd.
  */
  if (current_thd)
  {
    thd= current_thd;
    sav_lex= thd->lex;
    thd->lex= &lex_tmp;
    lex_start(thd);
  }
  else
  {
    if (!(thd= new THD))
    {
      LogErr(ERROR_LEVEL, ER_FAILED_TO_SET_PERSISTED_OPTIONS);
      return 1;
    }
    thd->thread_stack= (char*) &thd;
    thd->set_new_thread_id();
    thd->store_globals();
    lex_start(thd);
    /* save access privileges */
    access= thd->security_context()->master_access();
    thd->security_context()->set_master_access(~(ulong)0);
    thd->real_id= my_thread_self();
    new_thd= 1;
  }

  /*
    Based on plugin_options, we decide on what options to be set. If
    plugin_options is false we set all non plugin variables and then
    keep all plugin variables in a map. When the plugin is installed
    plugin variables are read from the map and set.
  */
  persist_variables= (plugin_options ? &m_persist_plugin_variables:
                                       &m_persist_variables);

  for (auto iter= persist_variables->begin();
       iter != persist_variables->end(); iter++)
  {
    Item *res= NULL;
    set_var *var= NULL;
    sys_var *sysvar= NULL;
    string var_name= iter->key;

    LEX_STRING base_name= { const_cast<char*>(var_name.c_str()),
                            var_name.length() };

    sysvar= intern_find_sys_var(var_name.c_str(), var_name.length());
    if (sysvar == NULL)
    {
      /*
        for plugin variables we report a warning in error log,
        keep track of this variable so that it is set when plugin
        is loaded and continue with remaining persisted variables
      */
      m_persist_plugin_variables.push_back(*iter);
      my_message_local(WARNING_LEVEL, "Currently unknown variable '%s' "
                       "was read from the persisted config file",
                       var_name.c_str());
      continue;
    }
    switch (sysvar->show_type())
    {
    case SHOW_INT:
    case SHOW_LONG:
    case SHOW_SIGNED_LONG:
    case SHOW_LONGLONG:
    case SHOW_HA_ROWS:
      res= new (thd->mem_root) Item_uint(iter->value.c_str(),
                                         (uint)iter->value.length());
    break;
    case SHOW_CHAR:
    case SHOW_CHAR_PTR:
    case SHOW_LEX_STRING:
    case SHOW_BOOL:
    case SHOW_MY_BOOL:
      res= new (thd->mem_root) Item_string(iter->value.c_str(),
                                            iter->value.length(),
                                            &my_charset_utf8mb4_bin);
    break;
    case SHOW_DOUBLE:
      res= new (thd->mem_root) Item_float(iter->value.c_str(),
                                          (uint)iter->value.length());
    break;
    default:
      my_error(ER_UNKNOWN_SYSTEM_VARIABLE, MYF(0), sysvar->name.str);
      result= 1;
      goto err;
    }

    var= new (thd->mem_root) set_var(OPT_GLOBAL, sysvar,
                                      &base_name, res);
    tmp_var_list.push_back(var);

    if (sql_set_variables(thd, &tmp_var_list, false))
    {
      /*
       If there is a connection and an error occurred during install plugin
       then report error at sql layer, else log the error in server log.
      */
      if (current_thd && plugin_options)
      {
        if (thd->is_error())
          LogErr(ERROR_LEVEL, ER_PERSIST_OPTION_STATUS,
            thd->get_stmt_da()->message_text());
        else
          my_error(ER_CANT_SET_PERSISTED, MYF(0));
      }
      else
      {
        if (thd->is_error())
          LogErr(ERROR_LEVEL, ER_PERSIST_OPTION_STATUS,
            thd->get_stmt_da()->message_text());
        else
          LogErr(ERROR_LEVEL, ER_FAILED_TO_SET_PERSISTED_OPTIONS);
      }
    result= 1;
    goto err;
    }
    tmp_var_list.empty();
    /*
      Once persisted variables are SET in the server,
      update variables source/user/timestamp/host from m_persist_variables.
    */
    auto it= std::find_if(m_persist_variables.begin(), m_persist_variables.end(),
      [var_name](st_persist_var const& s) { return s.key == var_name; });
    if (it != m_persist_variables.end())
    {
      /* persisted variable is found */
      sysvar->set_source(enum_variable_source::PERSISTED);
      sysvar->set_source_name(m_persist_filename.c_str());
      sysvar->set_timestamp(it->timestamp);
      sysvar->set_user(it->user.c_str());
      sysvar->set_host(it->host.c_str());
    }
  }

err:
  if (new_thd)
  {
    /* restore access privileges */
    thd->security_context()->set_master_access(access);
    lex_end(thd->lex);
    thd->release_resources();
    delete thd;
  }
  else
  {
    thd->lex= sav_lex;
  }
  return result;
}

/**
  extract_variables_from_json() is used to extract all the variable information
  which is in the form of Json_object.

  New format for mysqld-auto.cnf is as below:
  { "Version" : 1,
    "mysql_server" :
    { "variable_name" : {
      "Value" : "variable_value",
      "Metadata" : {
        "Timestamp" : timestamp_value,
        "User" : "user_name",
        "Host" : "host_name"
        }
      }
    }
    { "variable_name" : {
      ...

    { "mysql_server_static_options" :
      { "variable_name" : {
        "Value" : "variable_value",
        ...
      }
      ...
  }

  @param [in] dom             Pointer to the Json_dom object which is an internal
                              representation of parsed json string
  @param [in] is_read_only    Bool value when set to TRUE extracts read only
                              variables and dynamic variables when set to FALSE.

  @return 0 Success
  @return 1 Failure
*/
bool Persisted_variables_cache::extract_variables_from_json(Json_dom *dom,
                                                            bool is_read_only)
{
  Json_wrapper_object_iterator var_iter(down_cast<Json_object *>(dom));
  while (!var_iter.empty())
  {
    string var_name, var_value, var_user, var_host;
    ulonglong timestamp= 0;
    Json_dom *dom_obj;

    var_name= var_iter.elt().first;

    /**
      Static variables by themselves is represented as a json object with key
      "mysql_server_static_options" as parent element.
    */
    if (var_name == "mysql_server_static_options")
    {
      if (extract_variables_from_json(var_iter.elt().second.to_dom(NULL),
        true))
        return 1;
      var_iter.next();
      continue;
    }
    dom_obj= var_iter.elt().second.to_dom(NULL);
    /**
      Every Json object which represents Variable information must have only
      2 elements which is
      {
      "Value" : "variable_value",   -- 1st element
      "Metadata" : {                -- 2nd element
        "Timestamp" : timestamp_value,
        "User" : "user_name",
        "Host" : "host_name"
        }
      }
    */
    if (dom_obj->depth() !=  3 &&
      ((Json_object*)dom_obj)->cardinality() != 2)
      goto err;

    Json_wrapper_object_iterator
      var_properties_iter(down_cast<Json_object *>(dom_obj));
    /* extract variable value */
    if (var_properties_iter.elt().second.is_dom())
    {
      if (var_properties_iter.elt().first != "Value")
        goto err;

      dom_obj= var_properties_iter.elt().second.to_dom(NULL);
      if (dom_obj->depth() != 1 &&
        ((Json_object*)dom_obj)->cardinality() != 1)
        goto err;

      /* if value is not in string form throw error. */
      if (dom_obj->json_type() != enum_json_type::J_STRING)
        goto err;
      Json_string *value= down_cast<Json_string *>(dom_obj);
      var_value= value->value();
    }
    var_properties_iter.next();
    /* extract metadata */
    if (var_properties_iter.elt().second.is_dom())
    {
      if (var_properties_iter.elt().first != "Metadata")
        goto err;

      dom_obj= var_properties_iter.elt().second.to_dom(NULL);
      if (dom_obj->depth() != 1 &&
        ((Json_object*)dom_obj)->cardinality() != 3)
        goto err;

      Json_wrapper_object_iterator
        metadata_iter(down_cast<Json_object *>(dom_obj));
      while(!metadata_iter.empty())
      {
        string metadata_type= metadata_iter.elt().first;
        if (metadata_iter.elt().second.is_dom())
        {
          dom_obj= metadata_iter.elt().second.to_dom(NULL);
          if (metadata_type == "Timestamp")
          {
            if (dom_obj->json_type() != enum_json_type::J_UINT)
              goto err;

            Json_uint *i= down_cast<Json_uint *>(dom_obj);
            timestamp= i->value();
          }
          else if (metadata_type == "User" || metadata_type == "Host")
          {
            if (dom_obj->json_type() != enum_json_type::J_STRING)
              goto err;

            Json_string *i= down_cast<Json_string *>(dom_obj);
            if (metadata_type == "User")
              var_user= i->value();
            else
              var_host= i->value();
          }
          else
            goto err;
        }
        metadata_iter.next();
      }
      st_persist_var persist_var(var_name, var_value, timestamp, var_user,
        var_host);
      if (is_read_only)
        m_persist_ro_variables[var_name]= persist_var;
      else
        m_persist_variables.push_back(persist_var);
    }
    var_iter.next();
  }
  return 0;

  err:
    LogErr(ERROR_LEVEL, ER_JSON_PARSE_ERROR);
    return 1;
}

/**
  read_persist_file() reads the persisted config file

  This function does following:
    1. Read the persisted config file into a string buffer
    2. This string buffer is parsed with JSON parser to check
       if the format is correct or not.
    3. Check for correct group name.
    4. Extract key/value pair and populate in m_persist_variables,
       m_persist_ro_variables.
  mysqld-auto.cnf file will have variable properties like when a
  variable is set, by wholm and on what host this variable was set.

  @return Error state
    @retval -1 or 1 Failure
    @retval 0 Success
*/
int Persisted_variables_cache::read_persist_file()
{
  char buff[4096]= { 0 };
  string parsed_value;
  const char *error= NULL;
  size_t offset= 0;

  if ((check_file_permissions(m_persist_filename.c_str(), 0)) < 2)
    return -1;

  if (open_persist_file(O_RDONLY))
    return -1;
  do
  {
    /* Read the persisted config file into a string buffer */
    parsed_value.append(buff);
    buff[0]='\0';
  } while (mysql_file_fgets(buff, sizeof(buff) - 1, m_fd));
  close_persist_file();

  /* parse the file contents to check if it is in json format or not */
  std::unique_ptr<Json_dom> json(Json_dom::parse(parsed_value.c_str(),
                                 parsed_value.length(), &error, &offset));
  if (!json.get())
  {
    LogErr(ERROR_LEVEL, ER_JSON_PARSE_ERROR);
    return 1;
  }
  Json_object *json_obj= down_cast<Json_object *>(json.get());
  Json_wrapper_object_iterator iter(down_cast<Json_object *>(json_obj));
  if (iter.elt().first != "Version")
  {
    LogErr(ERROR_LEVEL, ER_PERSIST_OPTION_STATUS,
      "Persisted config file corrupted.");
    return 1;
  }
  /* Check file version */
  Json_dom *dom_obj= iter.elt().second.to_dom(NULL);
  if (dom_obj->json_type() != enum_json_type::J_INT)
  {
    LogErr(ERROR_LEVEL, ER_PERSIST_OPTION_STATUS,
      "Persisted config file version invalid.");
    return 1;
  }
  Json_int *i= down_cast<Json_int *>(dom_obj);
  if (file_version != i->value())
  {
    LogErr(ERROR_LEVEL, ER_PERSIST_OPTION_STATUS,
      "Persisted config file version invalid.");
    return 1;
  }
  iter.next();
  if (iter.elt().first != "mysql_server")
  {
    LogErr(ERROR_LEVEL, ER_CONFIG_OPTION_WITHOUT_GROUP);
    return 1;
  }
  /* Extract key/value pair and populate in a global hash map */
  if (extract_variables_from_json(iter.elt().second.to_dom(NULL)))
    return 1;
  return 0;
}

/**
  append_read_only_variables() does a lookup into persist_variables for read only
  variables and place them after the command line options with a separator
  "----persist-args-separator----"

  This function does nothing when --no-defaults is set or if
  persisted_globals_load is disabled.

  @param [in] argc                      Pointer to argc of original program
  @param [in] argv                      Pointer to argv of original program
  @param [in] plugin_options            This flag tells wether options are handled
                                        during plugin install. If set to true
                                        options are handled as part of install
                                        plugin.

  @return 0 Success
  @return 1 Failure
*/
bool Persisted_variables_cache::append_read_only_variables(int *argc,
  char ***argv, bool plugin_options)
{
  Prealloced_array<char *, 100> my_args(key_memory_persisted_variables);
  TYPELIB group;
  MEM_ROOT alloc;
  const char *type_name= "mysqld";

  if (*argc < 2 || no_defaults || !persisted_globals_load)
    return 0;

  init_alloc_root(key_memory_persisted_variables, &alloc, 512, 0);
  group.count= 1;
  group.name= "defaults";
  group.type_names= &type_name;

  for (auto iter= m_persist_ro_variables.begin();
       iter != m_persist_ro_variables.end(); iter++)
  {
    string persist_option= "--loose_" + iter->first + "=" + iter->second.value;
    if (find_type((char *) type_name, &group, FIND_TYPE_NO_PREFIX))
    {
      char *tmp;
      if ((!(tmp= (char *)
          alloc_root(&alloc, strlen(persist_option.c_str()) + 1))) ||
          my_args.push_back(tmp))
        return 1;
      my_stpcpy(tmp, (const char *) persist_option.c_str());
    }
  }
  /*
   Update existing command line options if there are any persisted
   reasd only options to be appendded
  */
  if (my_args.size())
  {
    char **res= new (&alloc) char*[my_args.size() + *argc + 2];
    if (res == nullptr)
      goto err;
    memset(res, 0, (sizeof(char *) * (my_args.size() + *argc + 2)));
    /* copy all arguments to new array */
    memcpy((uchar *) (res), (char *) (*argv), (*argc) * sizeof(char *));

    if (!my_args.empty())
    {
      /*
       Set args separator to know options set as part of command line and
       options set from persisted config file
      */
      set_persist_args_separator(&res[*argc]);
      /* copy arguments from persistent config file */
      memcpy((res + *argc + 1), &my_args[0], my_args.size() * sizeof(char *));
    }
    res[my_args.size() + *argc + 1] = 0;  /* last null */
    (*argc)+= (int)my_args.size() + 1;
    *argv= res;
    if (plugin_options)
      ro_persisted_plugin_argv_alloc= std::move(alloc);  // Possibly overwrite previous.
    else
      ro_persisted_argv_alloc= std::move(alloc);
    return 0;
  }
  return 0;

err:
  my_message_local(ERROR_LEVEL,
                   "Fatal error in defaults handling. Program aborted!");
  exit(1);
}

/**
  reset_persisted_variables() does a lookup into persist_variables and remove the
  variable from the hash if present and flush the hash to file.

  @param [in] thd                     Pointer to connection handle.
  @param [in] name                    Name of variable to remove, if NULL all
                                      variables are removed from config file.
  @param [in] if_exists               Bool value when set to true reports
                                      warning else error if variable is not
                                      present in the config file.

  @return 0 Success
  @return 1 Failure
*/
bool Persisted_variables_cache::reset_persisted_variables(THD *thd,
  const char* name, bool if_exists)
{
  bool result= 0, flush= 0, not_present= 1;
  string var_name;
  bool reset_all= (name ? 0 : 1);
  var_name= (name ? name : string());
  auto it_ro= m_persist_ro_variables.find(var_name);

  if (reset_all)
  {
    if (!m_persist_variables.empty())
    {
      m_persist_variables.clear();
      flush= 1;
    }
    if (!m_persist_ro_variables.empty())
    {
      m_persist_ro_variables.clear();
      flush= 1;
    }
  }
  else
  {
    if (m_persist_variables.size())
    {
      auto it= std::find_if(m_persist_variables.begin(), m_persist_variables.end(),
        [var_name](st_persist_var const & s) { return s.key == var_name; });
      if (it != m_persist_variables.end())
      {
        /* if variable is present in config file remove it */
        m_persist_variables.erase(it);
        flush= 1;
        not_present= 0;
      }
    }
    if (it_ro != m_persist_ro_variables.end())
    {
      /* if static variable is present in config file remove it */
      m_persist_ro_variables.erase(it_ro);
      flush= 1;
      not_present = 0;
    }
    if (not_present)
    {
      /* if not present and if exists is specified, report warning */
      if (if_exists)
      {
        push_warning_printf(thd, Sql_condition::SL_WARNING,
                            ER_VAR_DOES_NOT_EXIST,
                            ER_THD(thd, ER_VAR_DOES_NOT_EXIST),
                            var_name.c_str());
      }
      else /* report error */
      {
        my_error(ER_VAR_DOES_NOT_EXIST, MYF(0), var_name.c_str());
        result= 1;
      }
    }
  }
  if (flush)
    flush_to_file();

  return result;
}

/**
  Return in-memory copy persist_variables_
*/
vector<st_persist_var>*
Persisted_variables_cache::get_persisted_variables()
{
  return &m_persist_variables;
}

/**
  Return in-memory copy for static persisted variables
*/
map<string, st_persist_var>*
Persisted_variables_cache::get_persist_ro_variables()
{
  return &m_persist_ro_variables;
}

void Persisted_variables_cache::cleanup()
{
  mysql_mutex_destroy(&m_LOCK_persist_variables);
  mysql_mutex_destroy(&m_LOCK_persist_file);
  free_root(&ro_persisted_argv_alloc, MYF(0));
  free_root(&ro_persisted_plugin_argv_alloc, MYF(0));
}
