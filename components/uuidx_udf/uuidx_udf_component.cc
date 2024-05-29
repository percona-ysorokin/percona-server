/* Copyright (c) 2024, Percona LLC and/or its affiliates. All rights reserved.

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

#include <boost/preprocessor/stringize.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <array>
#include <string>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <algorithm>

#include <mysql/components/services/mysql_runtime_error.h>
#include <mysql/components/component_implementation.h>

#include <mysql/components/services/component_sys_var_service.h>
#include <mysql/components/services/mysql_current_thread_reader.h>
#include <mysql/components/services/mysql_runtime_error.h>
#include <mysql/components/services/udf_registration.h>
#include <mysql/components/services/udf_metadata.h>
#include <mysqlpp/udf_context_charset_extension.hpp>

#include <mysqlpp/udf_registration.hpp>
#include <mysqlpp/udf_wrappers.hpp>

#include "ts_helpers.h"

// defined as a macro because needed both raw and stringized
#define CURRENT_COMPONENT_NAME uuidx_udf
#define CURRENT_COMPONENT_NAME_STR BOOST_PP_STRINGIZE(CURRENT_COMPONENT_NAME)

REQUIRES_SERVICE_PLACEHOLDER(udf_registration);
REQUIRES_SERVICE_PLACEHOLDER(mysql_udf_metadata);
REQUIRES_SERVICE_PLACEHOLDER(mysql_runtime_error);

constexpr static const char *return_charset = "ascii";
constexpr static const char *nil_uuid = "00000000-0000-0000-0000-000000000000";
constexpr static const char *max_uuid = "FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF";

class uuidx_version_impl {
  public:

    uuidx_version_impl(mysqlpp::udf_context &ctx) {
      ctx.mark_result_const(false);
      ctx.mark_result_nullable(true);
      if(ctx.get_number_of_args()!=1){
        throw mysqlpp::udf_exception{"Function requires exactly one argument.", ER_EXCESS_ARGUMENTS};
      }       
      // arg0 - @uuid_string   
      ctx.mark_arg_nullable(0, true);
      ctx.set_arg_type(0, STRING_RESULT);      
    }

    mysqlpp::udf_result_t<INT_RESULT> calculate( const mysqlpp::udf_context  &ctx ){

      int version = 0;
      boost::uuids::string_generator gen;
      auto uxs = ctx.get_arg<STRING_RESULT>(0);
      if (uxs.empty()){ // NULL or empty string goes here
         return NULL;
      }
      try {
        boost::uuids::uuid u = gen(uxs.data());
        version = u.version();
      } catch (std::exception &ex){
        throw mysqlpp::udf_exception{"Invalid argument. Should be valid UUID string.", ER_WRONG_ARGUMENTS};
      }
      return version;
    }
};


class uuidx_variant_impl {
  public:

    uuidx_variant_impl(mysqlpp::udf_context &ctx) {
      ctx.mark_result_const(false);
      ctx.mark_result_nullable(true);
      if(ctx.get_number_of_args()!=1){
        throw mysqlpp::udf_exception{"Function requires exactly one argument.", ER_EXCESS_ARGUMENTS};
      }       
      // arg0 - @uuid_string
      ctx.mark_arg_nullable(0, true);
      ctx.set_arg_type(0, STRING_RESULT);      
    }

    mysqlpp::udf_result_t<INT_RESULT> calculate( const mysqlpp::udf_context  &ctx ){

      int variant = -1;
      boost::uuids::string_generator gen;
      auto uxs = ctx.get_arg<STRING_RESULT>(0);
      if (uxs.empty()){ // NULL or empty string goes here
         return NULL;
      }
      try {
        boost::uuids::uuid u = gen(uxs.data());
        variant = u.variant();
      } catch (std::exception &ex){
        throw mysqlpp::udf_exception{"Invalid argument. Should be valid UUID string.", ER_WRONG_ARGUMENTS};
      }

      return variant;
    }
};

class is_uuidx_impl {
  public:

    is_uuidx_impl(mysqlpp::udf_context &ctx) {
      ctx.mark_result_const(false);
      ctx.mark_result_nullable(true);
      if(ctx.get_number_of_args()!=1){
        throw mysqlpp::udf_exception{"Function requires exactly one argument.", ER_EXCESS_ARGUMENTS};
      }       
      // arg0 - @uuid_string
      ctx.mark_arg_nullable(0, true);
      ctx.set_arg_type(0, STRING_RESULT);      
    }

    mysqlpp::udf_result_t<INT_RESULT> calculate( const mysqlpp::udf_context  &ctx ){

      bool verification_result = false;
      boost::uuids::string_generator gen;
      auto uxs = ctx.get_arg<STRING_RESULT>(0);
      if (uxs.empty()){ // NULL or empty string goes here
         return NULL;
      }
      try {
        gen(uxs.data());
        verification_result = true;
      } catch (std::exception &ex){}

      return {verification_result ? 1LL : 0LL};
    }
};

class is_nil_uuid_impl {
  public:

    is_nil_uuid_impl(mysqlpp::udf_context &ctx) {
      ctx.mark_result_const(false);
      ctx.mark_result_nullable(true);
      if(ctx.get_number_of_args()!=1){
        throw mysqlpp::udf_exception{"Function requires exactly one argument.", ER_EXCESS_ARGUMENTS};
      }       
      // arg0 - @uuid_string
      ctx.mark_arg_nullable(0, true);
      ctx.set_arg_type(0, STRING_RESULT);      
    }

    mysqlpp::udf_result_t<INT_RESULT> calculate( const mysqlpp::udf_context  &ctx ){

      bool verification_result = false;
      boost::uuids::string_generator gen;
      auto uxs = ctx.get_arg<STRING_RESULT>(0);
      if (uxs.empty()){ // NULL or empty string goes here
         return NULL;
      }
      try {
        boost::uuids::uuid u = gen(uxs.data());
        verification_result = u.is_nil();
      } catch (std::exception &ex){
        throw mysqlpp::udf_exception{"Invalid argument. Should be valid UUID string.", ER_WRONG_ARGUMENTS};
      }

      return {verification_result ? 1LL : 0LL};
    }
};


class is_max_uuid_impl {
  public:

    is_max_uuid_impl(mysqlpp::udf_context &ctx) {
      ctx.mark_result_const(false);
      ctx.mark_result_nullable(true);
      if(ctx.get_number_of_args()!=1){
        throw mysqlpp::udf_exception{"Function requires exactly one argument.", ER_EXCESS_ARGUMENTS};
      }       
      // arg0 - @uuid_string
      ctx.mark_arg_nullable(0, true);
      ctx.set_arg_type(0, STRING_RESULT);     
    }

    mysqlpp::udf_result_t<INT_RESULT> calculate( const mysqlpp::udf_context  &ctx ){

      bool verification_result = true;
      boost::uuids::string_generator gen;
      auto uxs = ctx.get_arg<STRING_RESULT>(0);
      if (uxs.empty()){ // NULL or empty string goes here
         return NULL;
      }
      try {
        boost::uuids::uuid u = gen(uxs.data());
        for(size_t i = 0; i < sizeof(u.data); i++){
          if (u.data[i] != 0xFF){
             verification_result = false;
             break;  
          }  
        }
      } catch (std::exception &ex){
        throw mysqlpp::udf_exception{"Invalid argument. Should be valid UUID string.", ER_WRONG_ARGUMENTS};
      }

      return {verification_result ? 1LL : 0LL};
    }
};
class uuid1_impl {
  public:

  uuid1_impl(mysqlpp::udf_context &ctx) {

    ctx.mark_result_const(false);
    ctx.mark_result_nullable(true);

    mysqlpp::udf_context_charset_extension charset_ext{
    mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_charset(ctx, return_charset);    
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate([[maybe_unused]] const mysqlpp::udf_context  &ctx){
    static thread_local boost::uuids::time_generator_v1 gen_v1;
    return boost::uuids::to_string(gen_v1());
  }
};


boost::uuids::uuid get_uuid_namespace(int name_index){
    boost::uuids::uuid ns;
    switch(name_index){
      case 0: ns = boost::uuids::ns::dns();
      break;
      case 1: ns = boost::uuids::ns::url();
      break;
      case 2: ns = boost::uuids::ns::oid();
      break;
      case 3: ns = boost::uuids::ns::x500dn();
      break;
      default: ns = boost::uuids::ns::url();
    }    
    return ns;
}

class uuid3_impl {
  public:

  uuid3_impl(mysqlpp::udf_context &ctx) {

    ctx.mark_result_const(false);
    ctx.mark_result_nullable(true);
    size_t narg = ctx.get_number_of_args();
    if(narg > 0){
      // arg0 - @uuid string
      ctx.mark_arg_nullable(0, true);
      ctx.set_arg_type(0, STRING_RESULT);
    }else{
      throw mysqlpp::udf_exception{"Function requires exactly one or two arguments.", ER_EXCESS_ARGUMENTS};
    }
    
    if(narg > 1){
      // arg1 - @uuid namespace: DNS: 0, URL: 1, OID: 2, X.500: 3, default is 0, or DNS
      ctx.mark_arg_nullable(1, true);
      ctx.set_arg_type(0, INT_RESULT);
    }

    mysqlpp::udf_context_charset_extension charset_ext{
    mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_charset(ctx, return_charset);    
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate([[maybe_unused]] const mysqlpp::udf_context  &ctx){
    int ns_index = 0;
    auto the_string = ctx.get_arg<STRING_RESULT>(0);
    // static thread_local
    if(ctx.get_number_of_args() > 1){
      auto ns = ctx.get_arg<INT_RESULT>(1);
      ns_index = ns.value_or(1);
    }
    boost::uuids::name_generator_md5 gen_v3(get_uuid_namespace(ns_index));
    std::string s(the_string);
    return boost::uuids::to_string(gen_v3(s));
  }
};

class uuid4_impl {
  public:

  uuid4_impl(mysqlpp::udf_context &ctx) {

    ctx.mark_result_const(false);
    ctx.mark_result_nullable(true);

    mysqlpp::udf_context_charset_extension charset_ext{
    mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_charset(ctx, return_charset);    
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate([[maybe_unused]] const mysqlpp::udf_context  &ctx){
    // static thread_local
    boost::uuids::random_generator gen_v4;
    return boost::uuids::to_string(gen_v4());
  }
};

class uuid5_impl {
  public:

  uuid5_impl(mysqlpp::udf_context &ctx) {

    ctx.mark_result_const(false);
    ctx.mark_result_nullable(true);
    size_t narg = ctx.get_number_of_args();
    if(narg > 0){
      // arg0 - @uuid string
      ctx.mark_arg_nullable(0, true);
      ctx.set_arg_type(0, STRING_RESULT);
    }else{
      throw mysqlpp::udf_exception{"Function requires exactly one or two arguments.", ER_EXCESS_ARGUMENTS};
    }
    
    if(narg > 1){
      // arg1 - @uuid namespace: DNS: 0, URL: 1, OID: 2, X.500: 3, default is 0, or DNS
      ctx.mark_arg_nullable(1, true);
      ctx.set_arg_type(0, INT_RESULT);
    }

    mysqlpp::udf_context_charset_extension charset_ext{
    mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_charset(ctx, return_charset);    
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate([[maybe_unused]] const mysqlpp::udf_context  &ctx){
    int ns_index = 0;
    auto the_string = ctx.get_arg<STRING_RESULT>(0);
    // static thread_local
    if(ctx.get_number_of_args() > 1){
      auto ns = ctx.get_arg<INT_RESULT>(1);
      ns_index = ns.value_or(1);
    }
    boost::uuids::name_generator_sha1 gen_v5(get_uuid_namespace(ns_index));
    std::string s(the_string);
    return boost::uuids::to_string(gen_v5(s));
  }
};

class uuid6_impl {
  public:

  uuid6_impl(mysqlpp::udf_context &ctx) {

    ctx.mark_result_const(false);
    ctx.mark_result_nullable(true);

    mysqlpp::udf_context_charset_extension charset_ext{
    mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_charset(ctx, return_charset);    
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate([[maybe_unused]] const mysqlpp::udf_context  &ctx){
    static thread_local boost::uuids::time_generator_v6 gen_v6;
    return boost::uuids::to_string(gen_v6());
  }
};

class uuid7_impl {
  public:

  uuid7_impl(mysqlpp::udf_context &ctx) {

    ctx.mark_result_const(false);
    ctx.mark_result_nullable(true);
    size_t narg = ctx.get_number_of_args();
    // arg0 - @time_offset_ms, optional
    if(narg>1){
        throw mysqlpp::udf_exception{"Function requires zero or one arguments.", ER_EXCESS_ARGUMENTS};
    } 
    if (narg == 1) {
      ctx.mark_arg_nullable(0, false);
      ctx.set_arg_type(0, INT_RESULT);
    }

    mysqlpp::udf_context_charset_extension charset_ext{
    mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_charset(ctx, return_charset);    
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate([[maybe_unused]] const mysqlpp::udf_context  &ctx){
    static thread_local boost::uuids::time_generator_v7 gen_v7;
    auto generated = gen_v7();
    long offset = 0;
    if (ctx.get_number_of_args() == 1) {
      auto offs = ctx.get_arg<INT_RESULT>(0);
      offset = offs.value_or(0);
    }
    if(offset != 0){
      generated = add_ts_offset(generated, offset);
    }
    return boost::uuids::to_string(generated);
  }
  private:
/**
 * This function just shifts timestamp (ms part only) for uuid version 7.
 * ofs_ms argumant is of type "long" so it will not cause integer overflow.
*/
  boost::uuids::uuid add_ts_offset(boost::uuids::uuid u, long ofs_ms){
    std::uint64_t time_ms = u.time_point_v7().time_since_epoch().count();
    time_ms += ofs_ms;
    uint16_t d6tmp = u.data[6]; // ver and rand part
    uint8_t d7tmp = u.data[7]; // rand part
    std::uint64_t timestamp_and_others = ( time_ms << 16 ) | (d6tmp  << 8 )| d7tmp;
    boost::uuids::detail::store_big_u64( u.data + 0, timestamp_and_others );
    return u;
  }
};

class nil_uuidx_impl {
  public:

  nil_uuidx_impl(mysqlpp::udf_context &ctx) {

    ctx.mark_result_const(true);
    ctx.mark_result_nullable(false);

    mysqlpp::udf_context_charset_extension charset_ext{
    mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_charset(ctx, return_charset);    
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate([[maybe_unused]] const mysqlpp::udf_context  &ctx){
    return "00000000-0000-0000-0000-000000000000";
  }
};
class max_uuidx_impl {
  public:

  max_uuidx_impl(mysqlpp::udf_context &ctx) {

    ctx.mark_result_const(true);
    ctx.mark_result_nullable(false);

    mysqlpp::udf_context_charset_extension charset_ext{
    mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_charset(ctx, return_charset);    
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate([[maybe_unused]] const mysqlpp::udf_context  &ctx){
    return "FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF";
  }
};

class uuidx_to_bin_impl {
  public:

    uuidx_to_bin_impl(mysqlpp::udf_context &ctx) {
      ctx.mark_result_const(false);
      ctx.mark_result_nullable(false);
      if(ctx.get_number_of_args()!=1){
        throw mysqlpp::udf_exception{"Function requires exactly one argument.", ER_EXCESS_ARGUMENTS};
      } 
      // arg0 - @uuid_string
      ctx.mark_arg_nullable(0, false);
      ctx.set_arg_type(0, STRING_RESULT);
    }

    mysqlpp::udf_result_t<STRING_RESULT> calculate( const mysqlpp::udf_context  &ctx ){
      boost::uuids::string_generator gen;
      auto uxs = ctx.get_arg<STRING_RESULT>(0);
      if (uxs.empty()){ // NULL or empty string goes here
         return "";
      }      
      char result[17];
      boost::uuids::uuid u;
      try {
        u = gen(uxs.data());
        for(size_t i =0; i<u.size(); i++){
          result[i] = (char)u.data[i];
        }
        result[16]=0;
      } catch (std::exception &ex){
        result[0] = '\0';
      }

      return result;
    }
};

class bin_to_uuidx_impl {
  public:

  bin_to_uuidx_impl(mysqlpp::udf_context &ctx) {

    ctx.mark_result_const(false);
    ctx.mark_result_nullable(true);
    if(ctx.get_number_of_args()!=1){
      throw mysqlpp::udf_exception{"Function requires exactly one argument.", ER_EXCESS_ARGUMENTS};
    }     
      // arg0 - @uuid_version
    ctx.mark_arg_nullable(0, false);
    ctx.set_arg_type(0, STRING_RESULT);

    mysqlpp::udf_context_charset_extension charset_ext{
    mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_charset(ctx, return_charset);    
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate([[maybe_unused]] const mysqlpp::udf_context  &ctx){
    auto ubs = ctx.get_arg<STRING_RESULT>(0);
    if (ubs.empty()){ // NULL or empty string goes here
         return "";
    }
    size_t i = 0;
    boost::uuids::uuid u;
    for(auto dp = ubs.begin(); dp!=ubs.end(); ++dp ){
       u.data[i] = *dp;
       i++;
       if(i>16) break; //ignore more data
    }
    if(i!=15) { // not enpough data
       throw mysqlpp::udf_exception{"Not enough data for UUID, must be 16 bytes exactly", ER_WRONG_ARGUMENTS};
    }
    return boost::uuids::to_string(u);
  }
};

class uuid7_to_timestamp_impl {
  public:

    uuid7_to_timestamp_impl(mysqlpp::udf_context &ctx) {
      ctx.mark_result_const(false);
      ctx.mark_result_nullable(false);
      if(ctx.get_number_of_args()!=1){
        throw mysqlpp::udf_exception{"Function requires exactly one argument.", ER_EXCESS_ARGUMENTS};
      }       
      // arg0 - @uuid_string
      ctx.mark_arg_nullable(0, false);
      ctx.set_arg_type(0, STRING_RESULT);

      mysqlpp::udf_context_charset_extension charset_ext{
      mysql_service_mysql_udf_metadata};
      charset_ext.set_return_value_charset(ctx, return_charset);       
    }

    mysqlpp::udf_result_t<STRING_RESULT> calculate( const mysqlpp::udf_context  &ctx ){
      boost::uuids::string_generator gen;
      auto uxs = ctx.get_arg<STRING_RESULT>(0);
      boost::uuids::uuid u;
      long ms = 0;
      try {
        u = gen(uxs.data());
        if(u.version() !=7 ){
          throw mysqlpp::udf_exception{"Invalid argument. Should be valid UUID 7 string.", ER_WRONG_ARGUMENTS};
        }
        ms = u.time_point_v7().time_since_epoch().count();
      } catch (std::exception &ex){
        throw mysqlpp::udf_exception{"Invalid argument. Should be valid UUID string.", ER_WRONG_ARGUMENTS};
      }
      return  get_timestamp(ms);
    }    
};

class uuid7_to_timestamp_tz_impl {
  public:

    uuid7_to_timestamp_tz_impl(mysqlpp::udf_context &ctx) {
      ctx.mark_result_const(false);
      ctx.mark_result_nullable(false);
      if(ctx.get_number_of_args()!=1){
        throw mysqlpp::udf_exception{"Function requires exactly one argument.", ER_EXCESS_ARGUMENTS};
      }       
      // arg0 - @uuid_string
      ctx.mark_arg_nullable(0, false);
      ctx.set_arg_type(0, STRING_RESULT);

      mysqlpp::udf_context_charset_extension charset_ext{
      mysql_service_mysql_udf_metadata};
      charset_ext.set_return_value_charset(ctx, return_charset);        
    }

    mysqlpp::udf_result_t<STRING_RESULT> calculate( const mysqlpp::udf_context  &ctx ){
      boost::uuids::string_generator gen;
      auto uxs = ctx.get_arg<STRING_RESULT>(0);
      boost::uuids::uuid u;
      long ms = 0;
      try {
        u = gen(uxs.data());
        ms = u.time_point_v7().time_since_epoch().count();
        if(u.version() !=7 ){
          throw mysqlpp::udf_exception{"Invalid argument. Should be valid UUID 7 string.", ER_WRONG_ARGUMENTS};
        }        
      } catch (std::exception &ex){                
          throw mysqlpp::udf_exception{"Invalid argument. Should be valid UUID 7 string.", ER_WRONG_ARGUMENTS};
      }
      return  get_timestamp_long(ms);
    }    
};

class uuid7_to_unixtime_impl {
  public:

    uuid7_to_unixtime_impl(mysqlpp::udf_context &ctx) {
      ctx.mark_result_const(false);
      ctx.mark_result_nullable(false);
      if(ctx.get_number_of_args()!=1){
        throw mysqlpp::udf_exception{"Function requires exactly one argument.", ER_EXCESS_ARGUMENTS};
      }       
      // arg0 - @uuid_string
      ctx.mark_arg_nullable(0, false);
      ctx.set_arg_type(0, STRING_RESULT);
    }

    mysqlpp::udf_result_t<INT_RESULT> calculate( const mysqlpp::udf_context  &ctx ){
      boost::uuids::string_generator gen;
      auto uxs = ctx.get_arg<STRING_RESULT>(0);
      boost::uuids::uuid u;
      long ms = 0;
      try {
        u = gen(uxs.data());
        ms = u.time_point_v7().time_since_epoch().count();
        if(u.version() !=7 ){
          throw mysqlpp::udf_exception{"Invalid argument. Should be valid UUID 7 string.", ER_WRONG_ARGUMENTS};
        }         
      } catch (std::exception &ex){
        throw mysqlpp::udf_exception{"Invalid argument. Should be valid UUID 7 string.", ER_WRONG_ARGUMENTS};
      }
      return ms;
    }    
};

DECLARE_INT_UDF_AUTO(uuidx_version);
DECLARE_INT_UDF_AUTO(uuidx_variant);
DECLARE_INT_UDF_AUTO(is_uuidx);
DECLARE_INT_UDF_AUTO(is_nil_uuid);
DECLARE_INT_UDF_AUTO(is_max_uuid);
// Invoke g++ -latomic
DECLARE_STRING_UDF_AUTO(uuid1);
DECLARE_STRING_UDF_AUTO(uuid3);
DECLARE_STRING_UDF_AUTO(uuid4);
DECLARE_STRING_UDF_AUTO(uuid5);
DECLARE_STRING_UDF_AUTO(uuid6);
DECLARE_STRING_UDF_AUTO(uuid7);
DECLARE_STRING_UDF_AUTO(nil_uuidx);
DECLARE_STRING_UDF_AUTO(max_uuidx);
DECLARE_STRING_UDF_AUTO(uuidx_to_bin);
DECLARE_STRING_UDF_AUTO(bin_to_uuidx);
DECLARE_STRING_UDF_AUTO(uuid7_to_timestamp);
DECLARE_STRING_UDF_AUTO(uuid7_to_timestamp_tz);
DECLARE_INT_UDF_AUTO(uuid7_to_unixtime);

// array of defined UFDs
static const std::array known_udfs{
  DECLARE_UDF_INFO_AUTO(uuidx_version),
  DECLARE_UDF_INFO_AUTO(uuidx_variant),
  DECLARE_UDF_INFO_AUTO(is_uuidx),
  DECLARE_UDF_INFO_AUTO(is_nil_uuid),
  DECLARE_UDF_INFO_AUTO(is_max_uuid),
  DECLARE_UDF_INFO_AUTO(uuid1),
  DECLARE_UDF_INFO_AUTO(uuid3),
  DECLARE_UDF_INFO_AUTO(uuid4),
  DECLARE_UDF_INFO_AUTO(uuid5),
  DECLARE_UDF_INFO_AUTO(uuid6),  
  DECLARE_UDF_INFO_AUTO(uuid7),  
  DECLARE_UDF_INFO_AUTO(nil_uuidx),
  DECLARE_UDF_INFO_AUTO(max_uuidx),        
  DECLARE_UDF_INFO_AUTO(uuidx_to_bin),
  DECLARE_UDF_INFO_AUTO(bin_to_uuidx),
  DECLARE_UDF_INFO_AUTO(uuid7_to_timestamp),
  DECLARE_UDF_INFO_AUTO(uuid7_to_timestamp_tz),
  DECLARE_UDF_INFO_AUTO(uuid7_to_unixtime)    
};

static void uuidx_udf_my_error(int error_id, myf flags, ...) {
  va_list args;
  va_start(args, flags);
  mysql_service_mysql_runtime_error->emit(error_id, flags, args);
  va_end(args);
}

using udf_bitset_type =
    mysqlpp::udf_bitset<std::tuple_size_v<decltype(known_udfs)>>;
static udf_bitset_type registered_udfs;

/**
  Initialization entry method for Component used when loading the Component.

  @return Status of performed operation
  @retval 0 success
  @retval non-zero failure
*/
static mysql_service_status_t component_uuidx_udf_init() {

  mysqlpp::udf_error_reporter::instance() = &uuidx_udf_my_error;

  mysqlpp::register_udfs(mysql_service_udf_registration, known_udfs,
                         registered_udfs);

  return registered_udfs.all() ? 0 : 1;
}

/**
  De-initialization method for Component
  @return Status of performed operation
  @retval 0 success
  @retval non-zero failure
*/

static mysql_service_status_t component_uuidx_udf_deinit() {

  mysqlpp::unregister_udfs(mysql_service_udf_registration, known_udfs,
                           registered_udfs);
  return registered_udfs.none() ? 0 : 1;
}


// clang-format off
BEGIN_COMPONENT_PROVIDES(CURRENT_COMPONENT_NAME)
END_COMPONENT_PROVIDES();

BEGIN_COMPONENT_REQUIRES(CURRENT_COMPONENT_NAME)
  REQUIRES_SERVICE(mysql_runtime_error),
  REQUIRES_SERVICE(udf_registration),
  REQUIRES_SERVICE(mysql_udf_metadata),
END_COMPONENT_REQUIRES();

BEGIN_COMPONENT_METADATA(CURRENT_COMPONENT_NAME)
  METADATA("mysql.author", "Percona Corporation"),
  METADATA("mysql.license", "GPL"),
END_COMPONENT_METADATA();

DECLARE_COMPONENT(CURRENT_COMPONENT_NAME, CURRENT_COMPONENT_NAME_STR)
  component_uuidx_udf_init,
  component_uuidx_udf_deinit,
END_DECLARE_COMPONENT();
// clang-format on

DECLARE_LIBRARY_COMPONENTS &COMPONENT_REF(CURRENT_COMPONENT_NAME)
    END_DECLARE_LIBRARY_COMPONENTS


