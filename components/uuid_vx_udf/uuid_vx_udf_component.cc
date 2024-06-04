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

// defined as a macro because needed both raw and stringized
#define CURRENT_COMPONENT_NAME uuidx_udf
#define CURRENT_COMPONENT_NAME_STR BOOST_PP_STRINGIZE(CURRENT_COMPONENT_NAME)

REQUIRES_SERVICE_PLACEHOLDER(udf_registration);
REQUIRES_SERVICE_PLACEHOLDER(mysql_udf_metadata);
REQUIRES_SERVICE_PLACEHOLDER(mysql_runtime_error);
namespace {
  constexpr static const char *string_charset = "utf8mb3";  
  constexpr static const char *uuid_charset = "ascii";
  constexpr static const char *nil_uuid = "00000000-0000-0000-0000-000000000000";
  constexpr static const char *max_uuid = "FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF";
  constexpr static const char *err_msg_one_argument = "Function requires exactly one argument.";
  constexpr static const char *err_msg_valid_uuid = "Invalid argument. Should be valid UUID string.";
  constexpr static const char *err_msg_valid_v167_uuid = "Invalid argument. Should be valid UUID versions 1,6,7 in a string representation.";
  constexpr static const char *err_msg_no_arguments = "Function requires no arguments.";
  constexpr static const char *err_msg_one_or_two_arguments = "Function requires one or two arguments.";
  constexpr static const char *err_msg_zero_or_one_argument = "Function requires zero or one arguments.";
  constexpr static const char *err_msg_uuid_namespace_idx = "UUID namespace index must be in range 0-3.";
}

#define UUID_SIZE 16
class uuid_vx_version_impl {
  public:

    uuid_vx_version_impl(mysqlpp::udf_context &ctx) {
      ctx.mark_result_const(false);
      ctx.mark_result_nullable(true);
      if(ctx.get_number_of_args()!=1){
        throw mysqlpp::udf_exception{err_msg_one_argument, ER_EXCESS_ARGUMENTS};
      }       
      // arg0 - @uuid_string   
      ctx.mark_arg_nullable(0, true);
      ctx.set_arg_type(0, STRING_RESULT);
      mysqlpp::udf_context_charset_extension charset_ext{mysql_service_mysql_udf_metadata};
      charset_ext.set_arg_value_charset(ctx, 0, uuid_charset);             
    }

    mysqlpp::udf_result_t<INT_RESULT> calculate( const mysqlpp::udf_context  &ctx ){

      int version = 0;
      boost::uuids::string_generator gen;
      auto uxs = ctx.get_arg<STRING_RESULT>(0);
       
      if (ctx.is_arg_null(0)){ 
         return {};         
      }

      try {
        boost::uuids::uuid u = gen(uxs.data());
        version = u.version();
      } catch (const std::exception &ex){
        throw std::invalid_argument{err_msg_valid_uuid};
      }
      return version;
    }
};


class uuid_vx_variant_impl {
  public:

    uuid_vx_variant_impl(mysqlpp::udf_context &ctx) {
      ctx.mark_result_const(false);
      ctx.mark_result_nullable(true);
      if(ctx.get_number_of_args()!=1){
        throw mysqlpp::udf_exception{err_msg_one_argument, ER_EXCESS_ARGUMENTS};
      }       
      // arg0 - @uuid_string
      ctx.mark_arg_nullable(0, true);
      ctx.set_arg_type(0, STRING_RESULT);
      mysqlpp::udf_context_charset_extension charset_ext{mysql_service_mysql_udf_metadata};
      charset_ext.set_arg_value_charset(ctx, 0, uuid_charset);              
    }

    mysqlpp::udf_result_t<INT_RESULT> calculate( const mysqlpp::udf_context  &ctx ){

      int variant = -1;
      boost::uuids::string_generator gen;
      if (ctx.is_arg_null(0)){ 
         return {};
      }
      auto uxs = ctx.get_arg<STRING_RESULT>(0);
      try {
        boost::uuids::uuid u = gen(uxs.data());
        variant = u.variant();
      } catch (const std::exception &ex){
        throw std::invalid_argument{err_msg_valid_uuid};
      }
      return variant;
    }
};

class is_uuid_vx_impl {
  public:

    is_uuid_vx_impl(mysqlpp::udf_context &ctx) {
      ctx.mark_result_const(false);
      ctx.mark_result_nullable(true);
      if(ctx.get_number_of_args()!=1){
        throw mysqlpp::udf_exception{err_msg_one_argument, ER_EXCESS_ARGUMENTS};
      }       
      // arg0 - @uuid_string
      ctx.mark_arg_nullable(0, true);
      ctx.set_arg_type(0, STRING_RESULT);
      mysqlpp::udf_context_charset_extension charset_ext{mysql_service_mysql_udf_metadata};
      charset_ext.set_arg_value_charset(ctx, 0, uuid_charset);              
    }

    mysqlpp::udf_result_t<INT_RESULT> calculate( const mysqlpp::udf_context  &ctx ){

      bool verification_result = false;
      boost::uuids::string_generator gen;

      if (ctx.is_arg_null(0)){ 
         return {};
      }
      auto uxs = ctx.get_arg<STRING_RESULT>(0);
      try {
        gen(uxs.data());
        verification_result = true;
      } catch (std::exception &ex){}

      return {verification_result ? 1LL : 0LL};
    }
};

class is_nil_uuid_vx_impl {
  public:

    is_nil_uuid_vx_impl(mysqlpp::udf_context &ctx) {
      ctx.mark_result_const(false);
      ctx.mark_result_nullable(true);
      if(ctx.get_number_of_args()!=1){
        throw mysqlpp::udf_exception{err_msg_one_argument, ER_EXCESS_ARGUMENTS};
      }       
      // arg0 - @uuid_string
      ctx.mark_arg_nullable(0, true);
      ctx.set_arg_type(0, STRING_RESULT);
      mysqlpp::udf_context_charset_extension charset_ext{mysql_service_mysql_udf_metadata};
      charset_ext.set_arg_value_charset(ctx, 0, uuid_charset);              
    }

    mysqlpp::udf_result_t<INT_RESULT> calculate( const mysqlpp::udf_context  &ctx ){

      bool verification_result = false;
      boost::uuids::string_generator gen;
      if (ctx.is_arg_null(0)){ 
         return {};
      }
      auto uxs = ctx.get_arg<STRING_RESULT>(0);
      try {
        boost::uuids::uuid u = gen(uxs.data());
        verification_result = u.is_nil();
      } catch (std::exception &ex){
        throw std::invalid_argument{err_msg_valid_uuid};
      }

      return {verification_result ? 1LL : 0LL};
    }
};


class is_max_uuid_vx_impl {
  public:

    is_max_uuid_vx_impl(mysqlpp::udf_context &ctx) {
      ctx.mark_result_const(false);
      ctx.mark_result_nullable(true);
      if(ctx.get_number_of_args()!=1){
        throw mysqlpp::udf_exception{err_msg_one_argument, ER_EXCESS_ARGUMENTS};
      }       
      // arg0 - @uuid_string
      ctx.mark_arg_nullable(0, true);
      ctx.set_arg_type(0, STRING_RESULT);
      mysqlpp::udf_context_charset_extension charset_ext{mysql_service_mysql_udf_metadata};
      charset_ext.set_arg_value_charset(ctx, 0, uuid_charset);       
    }

    mysqlpp::udf_result_t<INT_RESULT> calculate( const mysqlpp::udf_context  &ctx ){

      bool verification_result = true;
      boost::uuids::string_generator gen;

      if (ctx.is_arg_null(0)){
         return {};
      }
      auto uxs = ctx.get_arg<STRING_RESULT>(0);      
      try {
        boost::uuids::uuid u = gen(uxs.data());
        verification_result = ! std::all_of(u.begin(), u.end(), [](uint8_t d){ return d == 0xFF; } );
      } catch (std::exception &ex){
        throw std::invalid_argument{err_msg_valid_uuid};
      }

      return {verification_result ? 1LL : 0LL};
    }
};
class uuid_v1_impl {
  public:

  uuid_v1_impl(mysqlpp::udf_context &ctx) {

    ctx.mark_result_const(false);
    ctx.mark_result_nullable(true);
    if(ctx.get_number_of_args()!=0){
       throw mysqlpp::udf_exception{err_msg_no_arguments, ER_EXCESS_ARGUMENTS};
    }
    mysqlpp::udf_context_charset_extension charset_ext{mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_charset(ctx, uuid_charset);   
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate([[maybe_unused]] const mysqlpp::udf_context  &ctx){
    static thread_local boost::uuids::time_generator_v1 gen_v1;
    return boost::uuids::to_string(gen_v1());
  }
};

class string_based_uuid {
  public:
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
      break; // we have 4 ns in the standard by now. In any other case we just use url()
      default: ns = boost::uuids::ns::url();
    }    
    return ns;
  }
};

class uuid_v3_impl : string_based_uuid {
  public:

  uuid_v3_impl(mysqlpp::udf_context &ctx) {

    ctx.mark_result_const(false);
    ctx.mark_result_nullable(true);

    mysqlpp::udf_context_charset_extension charset_ext{mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_charset(ctx, uuid_charset);     
    
    size_t narg = ctx.get_number_of_args();
    if(narg > 0 && narg < 3 ){
      // arg0 - @uuid string
      ctx.mark_arg_nullable(0, true);
      ctx.set_arg_type(0, STRING_RESULT);
      charset_ext.set_arg_value_charset(ctx, 0, string_charset); 
      if(narg == 2){
        // arg1 - @uuid namespace: DNS: 0, URL: 1, OID: 2, X.500: 3, default is 0, or DNS
        ctx.mark_arg_nullable(1, false);
        ctx.set_arg_type(1, INT_RESULT);
      }
    } else {
      throw mysqlpp::udf_exception{err_msg_one_or_two_arguments, ER_EXCESS_ARGUMENTS};
    }  

  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate( const mysqlpp::udf_context  &ctx){
    int ns_index = 0;
    if(ctx.is_arg_null(0)){
      return {};
    }
    auto the_string = ctx.get_arg<STRING_RESULT>(0);
    if(ctx.get_number_of_args() > 1){
      auto ns = ctx.get_arg<INT_RESULT>(1);
      ns_index = ns.value_or(1);
      if(ns_index < 0 || ns_index > 3){
         throw std::invalid_argument(err_msg_uuid_namespace_idx);
      }
    }
    boost::uuids::name_generator_md5 gen_v3(get_uuid_namespace(ns_index));
    std::string s(the_string);
    return boost::uuids::to_string(gen_v3(s));
  }
};

class uuid_v4_impl {
  public:

  uuid_v4_impl(mysqlpp::udf_context &ctx) {

    ctx.mark_result_const(false);
    ctx.mark_result_nullable(false);
    if(ctx.get_number_of_args() >0) {
      throw mysqlpp::udf_exception{err_msg_no_arguments, ER_EXCESS_ARGUMENTS};
    }

    mysqlpp::udf_context_charset_extension charset_ext{
    mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_charset(ctx, uuid_charset);    
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate([[maybe_unused]] const mysqlpp::udf_context  &ctx){
    boost::uuids::random_generator gen_v4;
    return boost::uuids::to_string(gen_v4());
  }
};

class uuid_v5_impl : string_based_uuid {
  public:

  uuid_v5_impl(mysqlpp::udf_context &ctx) {

    ctx.mark_result_const(false);
    ctx.mark_result_nullable(true);

    mysqlpp::udf_context_charset_extension charset_ext{mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_charset(ctx, uuid_charset);     
    
    size_t narg = ctx.get_number_of_args();
    if(narg > 0 && narg < 3 ){
      // arg0 - @uuid string
      ctx.mark_arg_nullable(0, true);
      ctx.set_arg_type(0, STRING_RESULT);
      charset_ext.set_arg_value_charset(ctx, 0, string_charset); 
      if(narg == 2){
        // arg1 - @uuid namespace: DNS: 0, URL: 1, OID: 2, X.500: 3, default is 0, or DNS
        ctx.mark_arg_nullable(1, false);
        ctx.set_arg_type(1, INT_RESULT);
      }
    } else {
      throw mysqlpp::udf_exception{err_msg_one_or_two_arguments, ER_EXCESS_ARGUMENTS};
    }     
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate(const mysqlpp::udf_context  &ctx){
    int ns_index = 0;
    if(ctx.is_arg_null(0)){
      return {};
    }
    auto the_string = ctx.get_arg<STRING_RESULT>(0);
    // static thread_local
    if(ctx.get_number_of_args() == 2){
      auto ns = ctx.get_arg<INT_RESULT>(1);
      ns_index = ns.value_or(1);
      if(ns_index < 0 || ns_index > 3){
         throw std::invalid_argument(err_msg_uuid_namespace_idx);
      }      
    }
    boost::uuids::name_generator_sha1 gen_v5(get_uuid_namespace(ns_index));
    std::string s(the_string);
    return boost::uuids::to_string(gen_v5(s));
  }
};

class uuid_v6_impl {
  public:

  uuid_v6_impl(mysqlpp::udf_context &ctx) {

    ctx.mark_result_const(false);
    ctx.mark_result_nullable(false);
    if(ctx.get_number_of_args()!=0){
      throw mysqlpp::udf_exception{err_msg_no_arguments, ER_EXCESS_ARGUMENTS};
    }
    mysqlpp::udf_context_charset_extension charset_ext{
    mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_charset(ctx, uuid_charset);    
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate([[maybe_unused]] const mysqlpp::udf_context  &ctx){
    static thread_local boost::uuids::time_generator_v6 gen_v6;
    return boost::uuids::to_string(gen_v6());
  }
};

class uuid_v7_impl {
  public:

  uuid_v7_impl(mysqlpp::udf_context &ctx) {

    ctx.mark_result_const(false);
    ctx.mark_result_nullable(false);
    size_t narg = ctx.get_number_of_args();
    // arg0 - @time_offset_ms, optional
    if(narg>1){
        throw mysqlpp::udf_exception{err_msg_zero_or_one_argument, ER_EXCESS_ARGUMENTS};
    } 
    if (narg == 1) {
      ctx.mark_arg_nullable(0, false);
      ctx.set_arg_type(0, INT_RESULT);
    }

    mysqlpp::udf_context_charset_extension charset_ext{
    mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_charset(ctx, uuid_charset);    
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate( const mysqlpp::udf_context  &ctx ){
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

class nil_uuid_vx_impl {
  public:

  nil_uuid_vx_impl(mysqlpp::udf_context &ctx) {

    ctx.mark_result_const(true);
    ctx.mark_result_nullable(false);
    if(ctx.get_number_of_args()!=0){
      throw mysqlpp::udf_exception{err_msg_no_arguments, ER_EXCESS_ARGUMENTS};
    }
    mysqlpp::udf_context_charset_extension charset_ext{
    mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_charset(ctx, uuid_charset);    
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate([[maybe_unused]] const mysqlpp::udf_context  &ctx){
    return nil_uuid;
  }
};
class max_uuid_vx_impl {
  public:

  max_uuid_vx_impl(mysqlpp::udf_context &ctx) {

    ctx.mark_result_const(true);
    ctx.mark_result_nullable(false);
    if(ctx.get_number_of_args()!=0){
      throw mysqlpp::udf_exception{err_msg_no_arguments, ER_EXCESS_ARGUMENTS};
    }
    mysqlpp::udf_context_charset_extension charset_ext{
    mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_charset(ctx, uuid_charset);    
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate([[maybe_unused]] const mysqlpp::udf_context  &ctx){
    return max_uuid;
  }
};

class uuid_vx_to_bin_impl {
  public:

    uuid_vx_to_bin_impl(mysqlpp::udf_context &ctx) {
      ctx.mark_result_const(false);
      ctx.mark_result_nullable(true);
      if(ctx.get_number_of_args()!=1){
        throw mysqlpp::udf_exception{err_msg_one_argument, ER_EXCESS_ARGUMENTS};
      } 
      // arg0 - @uuid_string
      ctx.mark_arg_nullable(0, true);
      ctx.set_arg_type(0, STRING_RESULT);
    }

    mysqlpp::udf_result_t<STRING_RESULT> calculate( const mysqlpp::udf_context  &ctx ){
      boost::uuids::string_generator gen;
      auto uxs = ctx.get_arg<STRING_RESULT>(0);
      if (ctx.is_arg_null(0)){ 
         return {};
      }      
      char result[UUID_SIZE+1];
      boost::uuids::uuid u;
      try {
        u = gen(uxs.data()); 
        for(size_t i = 0; i < UUID_SIZE; i++){
          result[i] = (uint8_t)u.data[i];
        }
        result[UUID_SIZE]=0;
      } catch (std::exception &ex){
         throw std::invalid_argument(err_msg_valid_uuid);
      }

      return result;
    }
};

class bin_to_uuid_vx_impl {
  public:

  bin_to_uuid_vx_impl(mysqlpp::udf_context &ctx) {

    ctx.mark_result_const(false);
    ctx.mark_result_nullable(true);
    if(ctx.get_number_of_args()!=1){
      throw mysqlpp::udf_exception{err_msg_one_argument, ER_EXCESS_ARGUMENTS};
    }         
      // arg0 - @uuid_version
    ctx.mark_arg_nullable(0, true);
    ctx.set_arg_type(0, STRING_RESULT); 

    mysqlpp::udf_context_charset_extension charset_ext{mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_charset(ctx, uuid_charset);       
    charset_ext.set_arg_value_charset(ctx, 0, uuid_charset);     
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate( const mysqlpp::udf_context  &ctx){
    if (ctx.is_arg_null(0)){ 
         return {};
    }
    auto ubs = ctx.get_arg<STRING_RESULT>(0);
    if(ubs.size() != UUID_SIZE) {
        throw std::invalid_argument("The UUID must be 16 bytes exactly.");
    }    
   
    boost::uuids::uuid u;
    for(size_t i = 0; i < UUID_SIZE; i++){
      u.data[i] = ubs.at(i);
    }

    return boost::uuids::to_string(u);
  }
};

class timestamp_based_uuid {
  public:

  /**
   * Returns time in ms since epoch
  */
  inline u_int64_t get_ts(std::string_view uxs) {
      boost::uuids::string_generator gen;   
      boost::uuids::uuid u;
      long ms = 0;
      try {
        u = gen(uxs.data());
      } catch (std::exception &ex){                
          throw std::invalid_argument{err_msg_valid_v167_uuid};
      }        
      switch(u.version()){
        case 1: ms = u.time_point_v1().time_since_epoch().count(); //it is bug inside of boost, gives wrong value
        break;
        case 6: ms = u.time_point_v6().time_since_epoch().count(); //it is bug inside of boost, gives wrong value
        break;
        case 7: ms = u.time_point_v7().time_since_epoch().count(); //ok
        break;
        default: {
          throw std::invalid_argument{err_msg_valid_v167_uuid};
        }
      } 
      return ms;   
  }

/** 
 * Returns timestamp in the format like:
 * 2024-05-29 18:04:14.201
 * TZ is always GMT  
*/
  inline std::string get_timestamp(uint64_t milliseconds) {
    
    std::chrono::system_clock::time_point tm{std::chrono::milliseconds{milliseconds}};
    auto in_time_t = std::chrono::system_clock::to_time_t(tm);

    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&in_time_t), "%Y-%m-%d %H:%M:%S")
         << '.' << std::setfill('0') << std::setw(3) << milliseconds % 1000;
    return oss.str();    
  }

/** Returns timestamp in the format like:
 *  Wed May 29 18:05:07 2024 GMT
 *  TZ is always GMT  
*/

  inline std::string get_timestamp_with_tz(uint64_t milliseconds) {

    std::chrono::system_clock::time_point tm{std::chrono::milliseconds{milliseconds}};
    auto in_time_t = std::chrono::system_clock::to_time_t(tm);

    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&in_time_t), "%c %Z");

    return oss.str();
  }
};

class uuid_vx_to_timestamp_impl : timestamp_based_uuid {
  public:

    uuid_vx_to_timestamp_impl(mysqlpp::udf_context &ctx) {
      ctx.mark_result_const(false);
      ctx.mark_result_nullable(true);
      if(ctx.get_number_of_args()!=1){
        throw mysqlpp::udf_exception{err_msg_one_argument, ER_EXCESS_ARGUMENTS};
      }       
      // arg0 - @uuid_string
      ctx.mark_arg_nullable(0, true);
      ctx.set_arg_type(0, STRING_RESULT);

      mysqlpp::udf_context_charset_extension charset_ext{mysql_service_mysql_udf_metadata};
      charset_ext.set_return_value_charset(ctx, string_charset);       
      charset_ext.set_arg_value_charset(ctx, 0, uuid_charset);             
    }

    mysqlpp::udf_result_t<STRING_RESULT> calculate( const mysqlpp::udf_context  &ctx ){
      if (ctx.is_arg_null(0)) {
        return {};
      }
      auto uxs = ctx.get_arg<STRING_RESULT>(0);     
      return  get_timestamp(get_ts(uxs));
    }    
};

class uuid_vx_to_timestamp_tz_impl : timestamp_based_uuid{
  public:

    uuid_vx_to_timestamp_tz_impl(mysqlpp::udf_context &ctx) {
      ctx.mark_result_const(false);
      ctx.mark_result_nullable(true);
      if(ctx.get_number_of_args()!=1){
        throw mysqlpp::udf_exception{err_msg_no_arguments, ER_EXCESS_ARGUMENTS};
      }       
      // arg0 - @uuid_string
      ctx.mark_arg_nullable(0, true);
      ctx.set_arg_type(0, STRING_RESULT);

      mysqlpp::udf_context_charset_extension charset_ext{
      mysql_service_mysql_udf_metadata};
      charset_ext.set_return_value_charset(ctx, string_charset);        
    }

    mysqlpp::udf_result_t<STRING_RESULT> calculate( const mysqlpp::udf_context  &ctx ){
      if (ctx.is_arg_null(0)) {
        return {};
      }      

      auto uxs = ctx.get_arg<STRING_RESULT>(0);

      return  get_timestamp_with_tz(get_ts(uxs));
    }    
};

class uuid_vx_to_unixtime_impl : timestamp_based_uuid {
  public:

    uuid_vx_to_unixtime_impl(mysqlpp::udf_context &ctx) {
      ctx.mark_result_const(false);
      ctx.mark_result_nullable(true);
      if( ctx.get_number_of_args() !=1 ){
        throw mysqlpp::udf_exception{err_msg_one_argument, ER_EXCESS_ARGUMENTS};
      }       
      // arg0 - @uuid_string
      ctx.mark_arg_nullable(0, true);
      ctx.set_arg_type(0, STRING_RESULT);
    }

    mysqlpp::udf_result_t<INT_RESULT> calculate( const mysqlpp::udf_context  &ctx ){
      if (ctx.is_arg_null(0)) {
        return {};
      } 
      auto uxs = ctx.get_arg<STRING_RESULT>(0);
      return get_ts(uxs);
    }    
};

DECLARE_INT_UDF_AUTO(uuid_vx_version);
DECLARE_INT_UDF_AUTO(uuid_vx_variant);
DECLARE_INT_UDF_AUTO(is_uuid_vx);
DECLARE_INT_UDF_AUTO(is_nil_uuid_vx);
DECLARE_INT_UDF_AUTO(is_max_uuid_vx);
// requires invoke of g++ with -latomic
DECLARE_STRING_UDF_AUTO(uuid_v1);
DECLARE_STRING_UDF_AUTO(uuid_v3);
DECLARE_STRING_UDF_AUTO(uuid_v4);
DECLARE_STRING_UDF_AUTO(uuid_v5);
// requires invoke of g++ with -latomic
DECLARE_STRING_UDF_AUTO(uuid_v6);
DECLARE_STRING_UDF_AUTO(uuid_v7);
DECLARE_STRING_UDF_AUTO(nil_uuid_vx);
DECLARE_STRING_UDF_AUTO(max_uuid_vx);
DECLARE_STRING_UDF_AUTO(uuid_vx_to_bin);
DECLARE_STRING_UDF_AUTO(bin_to_uuid_vx);
DECLARE_STRING_UDF_AUTO(uuid_vx_to_timestamp);
DECLARE_STRING_UDF_AUTO(uuid_vx_to_timestamp_tz);
DECLARE_INT_UDF_AUTO(uuid_vx_to_unixtime);

// array of defined UFDs
static const std::array known_udfs{
  DECLARE_UDF_INFO_AUTO(uuid_vx_version),
  DECLARE_UDF_INFO_AUTO(uuid_vx_variant),
  DECLARE_UDF_INFO_AUTO(is_uuid_vx),
  DECLARE_UDF_INFO_AUTO(is_nil_uuid_vx),
  DECLARE_UDF_INFO_AUTO(is_max_uuid_vx),
  DECLARE_UDF_INFO_AUTO(uuid_v1),
  DECLARE_UDF_INFO_AUTO(uuid_v3),
  DECLARE_UDF_INFO_AUTO(uuid_v4),
  DECLARE_UDF_INFO_AUTO(uuid_v5),
  DECLARE_UDF_INFO_AUTO(uuid_v6),  
  DECLARE_UDF_INFO_AUTO(uuid_v7),  
  DECLARE_UDF_INFO_AUTO(nil_uuid_vx),
  DECLARE_UDF_INFO_AUTO(max_uuid_vx),        
  DECLARE_UDF_INFO_AUTO(uuid_vx_to_bin),
  DECLARE_UDF_INFO_AUTO(bin_to_uuid_vx),
  DECLARE_UDF_INFO_AUTO(uuid_vx_to_timestamp),
  DECLARE_UDF_INFO_AUTO(uuid_vx_to_timestamp_tz),
  DECLARE_UDF_INFO_AUTO(uuid_vx_to_unixtime)    
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


