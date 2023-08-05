/* Copyright (c) 2018, Oracle and/or its affiliates. All rights reserved.
   Copyright (c) 2018, 2019 Francisco Miguel Biete Banon. All rights reserved.
   Copyright (c) 2023 Percona LLC and/or its affiliates. All rights reserved.

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

#include "masking_functions/registration_routines.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cctype>

#include <mysql/components/component_implementation.h>

#include <mysql/components/services/dynamic_privilege.h>
#include <mysql/components/services/mysql_current_thread_reader.h>
#include <mysql/components/services/security_context.h>

#include <mysqlpp/udf_context.hpp>
#include <mysqlpp/udf_context_charset_extension.hpp>
#include <mysqlpp/udf_registration.hpp>
#include <mysqlpp/udf_wrappers.hpp>

#include "masking_functions/charset_string.hpp"
#include "masking_functions/charset_string_operations.hpp"
#include "masking_functions/command_service_tuple.hpp"
#include "masking_functions/primitive_singleton.hpp"
#include "masking_functions/query_builder.hpp"
#include "masking_functions/random_string_generators.hpp"
#include "masking_functions/sql_context.hpp"
#include "masking_functions/string_service_tuple.hpp"

extern REQUIRES_SERVICE_PLACEHOLDER(udf_registration);
extern REQUIRES_SERVICE_PLACEHOLDER(dynamic_privilege_register);

extern REQUIRES_SERVICE_PLACEHOLDER(mysql_udf_metadata);

extern REQUIRES_SERVICE_PLACEHOLDER(mysql_current_thread_reader);
extern REQUIRES_SERVICE_PLACEHOLDER(mysql_thd_security_context);
extern REQUIRES_SERVICE_PLACEHOLDER(global_grants_check);

namespace {

using global_string_services = masking_functions::primitive_singleton<
    masking_functions::string_service_tuple>;
using global_command_services = masking_functions::primitive_singleton<
    masking_functions::command_service_tuple>;
using global_query_builder =
    masking_functions::primitive_singleton<masking_functions::query_builder>;

constexpr std::string_view masking_dictionaries_privilege_name =
    "MASKING_DICTIONARIES_ADMIN";

bool have_masking_admin_privilege() {
  THD *thd;
  if (mysql_service_mysql_current_thread_reader->get(&thd)) {
    throw std::runtime_error{"Couldn't query current thd"};
  }

  Security_context_handle sctx;
  if (mysql_service_mysql_thd_security_context->get(thd, &sctx)) {
    throw std::runtime_error{"Couldn't query security context"};
  }

  if (mysql_service_global_grants_check->has_global_grant(
          sctx, masking_dictionaries_privilege_name.data(),
          masking_dictionaries_privilege_name.size()))
    return true;

  return false;
}

masking_functions::charset_string extract_charset_string_from_arg(
    mysqlpp::udf_context const &ctx, std::size_t argno) {
  assert(argno < ctx.get_number_of_args());
  assert(ctx.get_arg_type(argno) == STRING_RESULT);
  const auto arg = ctx.get_arg<STRING_RESULT>(argno);
  if (arg.data() == nullptr)
    throw std::invalid_argument{"cannot create charset_string from NULL"};

  mysqlpp::udf_context_charset_extension charset_ext{
      mysql_service_mysql_udf_metadata};
  return {global_string_services::instance(), arg,
          charset_ext.get_arg_collation(ctx, argno)};
}

constexpr std::string_view x_ascii_masking_char = "X";
constexpr std::string_view star_ascii_masking_char = "*";

masking_functions::charset_string determine_masking_char(
    mysqlpp::udf_context const &ctx, std::size_t argno,
    std::string_view default_ascii_masking_char) {
  masking_functions::charset_string masking_char;

  if (argno >= ctx.get_number_of_args() || ctx.is_arg_null(argno)) {
    masking_char = masking_functions::charset_string(
        global_string_services::instance(), default_ascii_masking_char,
        masking_functions::charset_string::ascii_collation_name);
  } else {
    masking_char = extract_charset_string_from_arg(ctx, argno);
  }
  if (masking_char.get_size_in_characters() != 1)
    throw std::invalid_argument{"masking character must be of length 1"};

  return masking_char;
}

void set_return_value_collation_from_arg(
    mysqlpp::udf_context_charset_extension &charset_ext,
    mysqlpp::udf_context &ctx, std::size_t argno) {
  charset_ext.set_return_value_collation(
      ctx, charset_ext.get_arg_collation(ctx, argno));
}

//
// gen_range(int, int)
//
class gen_range_impl {
 public:
  gen_range_impl(mysqlpp::udf_context &ctx) {
    if (ctx.get_number_of_args() != 2)
      throw std::invalid_argument{"Wrong argument list: should be (int, int)"};

    ctx.mark_result_nullable(true);
    ctx.mark_result_const(false);

    ctx.mark_arg_nullable(0, false);
    ctx.set_arg_type(0, INT_RESULT);

    ctx.mark_arg_nullable(1, false);
    ctx.set_arg_type(1, INT_RESULT);
  }

  mysqlpp::udf_result_t<INT_RESULT> calculate(const mysqlpp::udf_context &ctx) {
    const auto lower = *ctx.get_arg<INT_RESULT>(0);
    const auto upper = *ctx.get_arg<INT_RESULT>(1);

    if (upper < lower) {
      return std::nullopt;
    } else {
      return masking_functions::random_number(lower, upper);
    }
  }
};

//
// gen_rnd_email([int], [int], [string])
//
class gen_rnd_email_impl {
 private:
  static constexpr std::string_view default_ascii_email_domain = "example.com";
  static constexpr std::size_t default_name_length = 5;
  static constexpr std::size_t default_surname_length = 7;
  static constexpr std::size_t max_name_length = 1024;
  static constexpr std::size_t max_surname_length = 1024;

 public:
  gen_rnd_email_impl(mysqlpp::udf_context &ctx) {
    if (ctx.get_number_of_args() > 3)
      throw std::invalid_argument{
          "Wrong argument list: should be ([int], [int], [string])"};

    ctx.mark_result_nullable(true);
    ctx.mark_result_const(false);

    if (ctx.get_number_of_args() >= 1) {
      ctx.mark_arg_nullable(0, false);
      ctx.set_arg_type(0, INT_RESULT);
    }

    if (ctx.get_number_of_args() >= 2) {
      ctx.mark_arg_nullable(1, false);
      ctx.set_arg_type(1, INT_RESULT);
    }

    if (ctx.get_number_of_args() >= 3) {
      ctx.mark_arg_nullable(2, false);
      ctx.set_arg_type(2, STRING_RESULT);
    }

    mysqlpp::udf_context_charset_extension charset_ext{
        mysql_service_mysql_udf_metadata};
    if (ctx.get_number_of_args() >= 3) {
      set_return_value_collation_from_arg(charset_ext, ctx, 2);
    } else {
      charset_ext.set_return_value_collation(
          ctx, masking_functions::charset_string::default_collation_name);
    }
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate(
      const mysqlpp::udf_context &ctx) {
    masking_functions::charset_string cs_email_domain;
    if (ctx.get_number_of_args() >= 3) {
      cs_email_domain = extract_charset_string_from_arg(ctx, 2);
    } else {
      cs_email_domain = masking_functions::charset_string{
          global_string_services::instance(), default_ascii_email_domain,
          masking_functions::charset_string::default_collation_name};
    }

    const long long name_length = ctx.get_number_of_args() >= 1
                                      ? *ctx.get_arg<INT_RESULT>(0)
                                      : default_name_length;
    if (name_length <= 0) {
      throw std::invalid_argument{"Name length must be a positive number"};
    }
    const auto casted_name_length = static_cast<std::size_t>(name_length);
    if (casted_name_length > max_name_length) {
      throw std::invalid_argument{"Name length must not exceed " +
                                  std::to_string(max_name_length)};
    }

    const long long surname_length = ctx.get_number_of_args() >= 2
                                         ? *ctx.get_arg<INT_RESULT>(1)
                                         : default_surname_length;
    if (surname_length <= 0) {
      throw std::invalid_argument{"Surname length must be a positive number"};
    }
    const auto casted_surname_length = static_cast<std::size_t>(surname_length);
    if (casted_surname_length > max_surname_length) {
      throw std::invalid_argument{"Surname length must not exceed " +
                                  std::to_string(max_surname_length)};
    }

    std::string email;
    email.reserve(casted_name_length + 1 + casted_surname_length + 1);
    email += masking_functions::random_lower_alpha_string(casted_name_length);
    email += '.';
    email +=
        masking_functions::random_lower_alpha_string(casted_surname_length);
    email += '@';

    masking_functions::charset_string default_cs_email{
        global_string_services::instance(), email,
        masking_functions::charset_string::default_collation_name};
    auto cs_email = default_cs_email.convert_to_collation_copy(
        cs_email_domain.get_collation());

    cs_email += cs_email_domain;

    return {std::string{cs_email.get_buffer()}};
  }
};

//
// gen_rnd_iban([string], [int])
//
class gen_rnd_iban_impl {
 private:
  static constexpr std::string_view default_ascii_country_code{"ZZ"};
  static constexpr std::size_t country_code_length{2U};
  static constexpr std::size_t min_number_of_characters{15U};
  static constexpr std::size_t max_number_of_characters{34U};
  static constexpr std::size_t default_number_of_characters{16U};

  static void validata_ansi_country_code(
      const masking_functions::charset_string &ascii_country_code) {
    if (ascii_country_code.get_size_in_characters() != country_code_length ||
        ascii_country_code.get_size_in_bytes() != country_code_length) {
      throw std::invalid_argument{"IBAN country code must be exactly " +
                                  std::to_string(country_code_length) +
                                  " ASCII characters"};
    }
    const auto buffer = ascii_country_code.get_buffer();
    if (std::find_if_not(std::begin(buffer), std::end(buffer),
                         static_cast<int (*)(int)>(&std::isupper)) !=
        std::end(buffer))
      throw std::invalid_argument{
          "IBAN country code must include only only upper-case characters"};
  }

 public:
  gen_rnd_iban_impl(mysqlpp::udf_context &ctx) {
    if (ctx.get_number_of_args() > 2)
      throw std::invalid_argument{
          "Wrong argument list: should be ([string], [int])"};

    ctx.mark_result_nullable(true);
    ctx.mark_result_const(false);

    if (ctx.get_number_of_args() >= 1) {
      ctx.mark_arg_nullable(0, false);
      ctx.set_arg_type(0, STRING_RESULT);
    }

    if (ctx.get_number_of_args() >= 2) {
      ctx.mark_arg_nullable(1, false);
      ctx.set_arg_type(1, INT_RESULT);
    }

    mysqlpp::udf_context_charset_extension charset_ext{
        mysql_service_mysql_udf_metadata};
    if (ctx.get_number_of_args() >= 1) {
      set_return_value_collation_from_arg(charset_ext, ctx, 0);
    } else {
      charset_ext.set_return_value_collation(
          ctx, masking_functions::charset_string::default_collation_name);
    }
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate(
      const mysqlpp::udf_context &ctx) {
    masking_functions::charset_string cs_country_code;
    if (ctx.get_number_of_args() >= 1) {
      cs_country_code = extract_charset_string_from_arg(ctx, 0);
    } else {
      cs_country_code = masking_functions::charset_string{
          global_string_services::instance(), default_ascii_country_code,
          masking_functions::charset_string::default_collation_name};
    }

    if (cs_country_code.get_size_in_characters() != country_code_length) {
      throw std::invalid_argument{"IBAN country code must be exactly " +
                                  std::to_string(country_code_length) +
                                  " characters"};
    }
    masking_functions::charset_string conversion_buffer;
    const auto &ascii_country_code =
        masking_functions::smart_convert_to_collation(
            cs_country_code,
            masking_functions::charset_string::ascii_collation_name,
            conversion_buffer);
    validata_ansi_country_code(ascii_country_code);

    const long long iban_length = ctx.get_number_of_args() >= 2
                                      ? *ctx.get_arg<INT_RESULT>(1)
                                      : default_number_of_characters;

    if (iban_length < 0) {
      throw std::invalid_argument{"IBAN length must not be a negative number"};
    }

    const auto casted_iban_length = static_cast<std::size_t>(iban_length);

    if (casted_iban_length < min_number_of_characters ||
        casted_iban_length > max_number_of_characters) {
      throw std::invalid_argument{"IBAN length must be between " +
                                  std::to_string(min_number_of_characters) +
                                  " and " +
                                  std::to_string(max_number_of_characters)};
    }

    auto generated_iban = masking_functions::random_iban(
        ascii_country_code.get_buffer(),
        casted_iban_length - country_code_length);
    auto ascii_iban = masking_functions::charset_string(
        global_string_services::instance(), generated_iban,
        masking_functions::charset_string::ascii_collation_name);

    const auto &cs_iban = masking_functions::smart_convert_to_collation(
        ascii_iban, cs_country_code.get_collation(), conversion_buffer);
    return {std::string{cs_iban.get_buffer()}};
  }
};

class rnd_impl_base {
 public:
  rnd_impl_base(mysqlpp::udf_context &ctx) {
    if (ctx.get_number_of_args() != 0) {
      throw std::invalid_argument{"Wrong argument list: should be empty"};
    }
    mysqlpp::udf_context_charset_extension charset_ext{
        mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_collation(
        ctx, masking_functions::charset_string::default_collation_name);
  }

 protected:
  ~rnd_impl_base() = default;
};

//
// gen_rnd_canada_sin()
//
class gen_rnd_canada_sin_impl final : private rnd_impl_base {
 public:
  using rnd_impl_base::rnd_impl_base;
  mysqlpp::udf_result_t<STRING_RESULT> calculate(const mysqlpp::udf_context &) {
    return masking_functions::random_canada_sin();
  }
};

//
// gen_rnd_pan()
//
class gen_rnd_pan_impl final : private rnd_impl_base {
 public:
  using rnd_impl_base::rnd_impl_base;
  mysqlpp::udf_result_t<STRING_RESULT> calculate(const mysqlpp::udf_context &) {
    return masking_functions::random_credit_card();
  }
};

//
// gen_rnd_ssn()
//
class gen_rnd_ssn_impl final : private rnd_impl_base {
 public:
  using rnd_impl_base::rnd_impl_base;
  mysqlpp::udf_result_t<STRING_RESULT> calculate(const mysqlpp::udf_context &) {
    return masking_functions::random_ssn();
  }
};

//
// gen_rnd_uk_nin()
//
class gen_rnd_uk_nin_impl final : private rnd_impl_base {
 public:
  using rnd_impl_base::rnd_impl_base;
  mysqlpp::udf_result_t<STRING_RESULT> calculate(const mysqlpp::udf_context &) {
    return masking_functions::random_uk_nin();
  }
};

//
// gen_rnd_us_phone()
//
class gen_rnd_us_phone_impl final : private rnd_impl_base {
 public:
  using rnd_impl_base::rnd_impl_base;
  mysqlpp::udf_result_t<STRING_RESULT> calculate(const mysqlpp::udf_context &) {
    return masking_functions::random_us_phone();
  }
};

//
// gen_rnd_uuid()
//
class gen_rnd_uuid_impl final : private rnd_impl_base {
 public:
  using rnd_impl_base::rnd_impl_base;
  mysqlpp::udf_result_t<STRING_RESULT> calculate(const mysqlpp::udf_context &) {
    return masking_functions::random_uuid();
  }
};

//
// mask_inner(string, int, int, [char])
//
class mask_inner_impl {
 public:
  mask_inner_impl(mysqlpp::udf_context &ctx) {
    if (ctx.get_number_of_args() < 3 || ctx.get_number_of_args() > 4)
      throw std::invalid_argument{
          "Wrong argument list: should be (string, int, int, [char])"};

    ctx.mark_result_nullable(true);
    ctx.mark_result_const(false);

    ctx.mark_arg_nullable(0, false);
    ctx.set_arg_type(0, STRING_RESULT);

    ctx.mark_arg_nullable(1, false);
    ctx.set_arg_type(1, INT_RESULT);

    ctx.mark_arg_nullable(2, false);
    ctx.set_arg_type(2, INT_RESULT);

    if (ctx.get_number_of_args() >= 4) {
      ctx.mark_arg_nullable(3, false);
      ctx.set_arg_type(3, STRING_RESULT);
    }

    mysqlpp::udf_context_charset_extension charset_ext{
        mysql_service_mysql_udf_metadata};
    set_return_value_collation_from_arg(charset_ext, ctx, 0);
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate(
      const mysqlpp::udf_context &ctx) {
    const auto cs_str = extract_charset_string_from_arg(ctx, 0);

    const auto masking_char =
        determine_masking_char(ctx, 3, x_ascii_masking_char);

    const auto left_margin = *ctx.get_arg<INT_RESULT>(1);
    const auto right_margin = *ctx.get_arg<INT_RESULT>(2);

    if (left_margin < 0 || right_margin < 0) {
      throw std::invalid_argument{"Margins can't be negative!"};
    }

    const auto casted_left_margin = static_cast<std::size_t>(left_margin);
    const auto casted_right_margin = static_cast<std::size_t>(right_margin);

    const auto result = masking_functions::mask_inner(
        cs_str, casted_left_margin, casted_right_margin, masking_char);

    return {std::string{result.get_buffer()}};
  }
};

//
// mask_outer(string, int, int, [char])
//
class mask_outer_impl {
 public:
  mask_outer_impl(mysqlpp::udf_context &ctx) {
    if (ctx.get_number_of_args() < 3 || ctx.get_number_of_args() > 4)
      throw std::invalid_argument{
          "Wrong argument list: should be (string, int, int [char])"};

    ctx.mark_result_nullable(true);
    ctx.mark_result_const(false);

    ctx.mark_arg_nullable(0, false);
    ctx.set_arg_type(0, STRING_RESULT);

    ctx.mark_arg_nullable(1, false);
    ctx.set_arg_type(1, INT_RESULT);

    ctx.mark_arg_nullable(2, false);
    ctx.set_arg_type(2, INT_RESULT);

    if (ctx.get_number_of_args() >= 4) {
      ctx.mark_arg_nullable(3, false);
      ctx.set_arg_type(3, STRING_RESULT);
    }

    mysqlpp::udf_context_charset_extension charset_ext{
        mysql_service_mysql_udf_metadata};
    set_return_value_collation_from_arg(charset_ext, ctx, 0);
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate(
      const mysqlpp::udf_context &ctx) {
    const auto cs_str = extract_charset_string_from_arg(ctx, 0);

    const auto masking_char =
        determine_masking_char(ctx, 3, x_ascii_masking_char);

    const auto left_margin = *ctx.get_arg<INT_RESULT>(1);
    const auto right_margin = *ctx.get_arg<INT_RESULT>(2);

    if (left_margin < 0 || right_margin < 0) {
      throw std::invalid_argument{"Margins can't be negative!"};
    }

    const auto casted_left_margin = static_cast<std::size_t>(left_margin);
    const auto casted_right_margin = static_cast<std::size_t>(right_margin);

    const auto result = masking_functions::mask_outer(
        cs_str, casted_left_margin, casted_right_margin, masking_char);

    return {std::string{result.get_buffer()}};
  }
};

class mask_impl_base {
 private:
  virtual std::size_t min_length() const = 0;
  virtual std::size_t max_length() const = 0;
  virtual std::string_view default_ascii_masking_char() const = 0;
  virtual masking_functions::charset_string process(
      const masking_functions::charset_string &cs_str,
      const masking_functions::charset_string &masking_char) const = 0;

 public:
  mask_impl_base(mysqlpp::udf_context &ctx) {
    if (ctx.get_number_of_args() < 1 || ctx.get_number_of_args() > 2)
      throw std::invalid_argument{
          "Wrong argument list: should be (string, [char])"};

    ctx.mark_result_nullable(true);
    ctx.mark_result_const(false);

    ctx.mark_arg_nullable(0, false);
    ctx.set_arg_type(0, STRING_RESULT);

    if (ctx.get_number_of_args() >= 2) {
      ctx.mark_arg_nullable(1, false);
      ctx.set_arg_type(1, STRING_RESULT);
    }

    mysqlpp::udf_context_charset_extension charset_ext{
        mysql_service_mysql_udf_metadata};
    set_return_value_collation_from_arg(charset_ext, ctx, 0);
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate(
      const mysqlpp::udf_context &ctx) {
    const auto cs_str = extract_charset_string_from_arg(ctx, 0);
    const auto cs_str_length = cs_str.get_size_in_characters();

    if (cs_str_length < min_length() || cs_str_length > max_length()) {
      if (min_length() == max_length()) {
        throw std::invalid_argument{"Argument must be exactly " +
                                    std::to_string(min_length()) +
                                    " characters"};
      } else {
        throw std::invalid_argument{
            "Argument must be between " + std::to_string(min_length()) +
            " and " + std::to_string(max_length()) + " characters"};
      }
    }

    const auto masking_char =
        determine_masking_char(ctx, 1, default_ascii_masking_char());

    const auto result = process(cs_str, masking_char);

    return {std::string{result.get_buffer()}};
  }

 protected:
  ~mask_impl_base() = default;
};

//
// mask_canada_sin(string, [char])
//
class mask_canada_sin_impl final : private mask_impl_base {
 public:
  using mask_impl_base::calculate;
  using mask_impl_base::mask_impl_base;

 private:
  virtual std::size_t min_length() const override { return 9; }
  virtual std::size_t max_length() const override { return 11; }
  virtual std::string_view default_ascii_masking_char() const override {
    return x_ascii_masking_char;
  }

  virtual masking_functions::charset_string process(
      const masking_functions::charset_string &cs_str,
      const masking_functions::charset_string &masking_char) const override {
    if (cs_str.get_size_in_characters() == max_length()) {
      auto sresult = masking_functions::mask_inner(cs_str, 4, 4, masking_char);
      sresult = masking_functions::mask_inner(sresult, 0, 8, masking_char);
      return masking_functions::mask_inner(sresult, 8, 0, masking_char);
    } else {
      return masking_functions::mask_inner_alphanum(cs_str, 0, 0, masking_char);
    }
  }
};

//
// mask_iban(string, [char])
//
class mask_iban_impl final : private mask_impl_base {
 public:
  using mask_impl_base::calculate;
  using mask_impl_base::mask_impl_base;

 private:
  virtual std::size_t min_length() const override { return 15; }
  virtual std::size_t max_length() const override { return 34 + 8; }
  virtual std::string_view default_ascii_masking_char() const override {
    return star_ascii_masking_char;
  }
  virtual masking_functions::charset_string process(
      const masking_functions::charset_string &cs_str,
      const masking_functions::charset_string &masking_char) const override {
    return masking_functions::mask_inner_alphanum(cs_str, 2, 0, masking_char);
  }
};

//
// mask_pan(string, [char])
//
class mask_pan_impl final : private mask_impl_base {
 public:
  using mask_impl_base::calculate;
  using mask_impl_base::mask_impl_base;

 private:
  virtual std::size_t min_length() const override { return 14; }
  virtual std::size_t max_length() const override { return 19; }
  virtual std::string_view default_ascii_masking_char() const override {
    return x_ascii_masking_char;
  }

  virtual masking_functions::charset_string process(
      const masking_functions::charset_string &cs_str,
      const masking_functions::charset_string &masking_char) const override {
    return masking_functions::mask_inner_alphanum(cs_str, 0, 4, masking_char);
  }
};

//
// mask_pan_relaxed(string, [char])
//
class mask_pan_relaxed_impl final : private mask_impl_base {
 public:
  using mask_impl_base::calculate;
  using mask_impl_base::mask_impl_base;

 private:
  virtual std::size_t min_length() const override { return 14; }
  virtual std::size_t max_length() const override { return 19; }
  virtual std::string_view default_ascii_masking_char() const override {
    return x_ascii_masking_char;
  }
  virtual masking_functions::charset_string process(
      const masking_functions::charset_string &cs_str,
      const masking_functions::charset_string &masking_char) const override {
    return masking_functions::mask_inner_alphanum(cs_str, 6, 4, masking_char);
  }
};

//
// mask_ssn(string, [char])
//
class mask_ssn_impl final : private mask_impl_base {
 public:
  using mask_impl_base::calculate;
  using mask_impl_base::mask_impl_base;

 private:
  virtual std::size_t min_length() const override { return 9; }
  virtual std::size_t max_length() const override { return 11; }
  virtual std::string_view default_ascii_masking_char() const override {
    return star_ascii_masking_char;
  }
  virtual masking_functions::charset_string process(
      const masking_functions::charset_string &cs_str,
      const masking_functions::charset_string &masking_char) const override {
    if (cs_str.get_size_in_characters() == max_length()) {
      auto sresult = masking_functions::mask_inner(cs_str, 4, 5, masking_char);
      return masking_functions::mask_inner(sresult, 0, 8, masking_char);
    } else {
      return masking_functions::mask_inner_alphanum(cs_str, 0, 4, masking_char);
    }
  }
};

//
// mask_uk_nin(string, [char])
//
class mask_uk_nin_impl final : private mask_impl_base {
 public:
  using mask_impl_base::calculate;
  using mask_impl_base::mask_impl_base;

 private:
  virtual std::size_t min_length() const override { return 9; }
  virtual std::size_t max_length() const override { return 11; }
  virtual std::string_view default_ascii_masking_char() const override {
    return star_ascii_masking_char;
  }
  virtual masking_functions::charset_string process(
      const masking_functions::charset_string &cs_str,
      const masking_functions::charset_string &masking_char) const override {
    return masking_functions::mask_inner_alphanum(cs_str, 2, 0, masking_char);
  }
};

//
// mask_uuid(string, [char])
//
class mask_uuid_impl final : private mask_impl_base {
 public:
  using mask_impl_base::calculate;
  using mask_impl_base::mask_impl_base;

 private:
  virtual std::size_t min_length() const override { return 36; }
  virtual std::size_t max_length() const override { return 36; }
  virtual std::string_view default_ascii_masking_char() const override {
    return star_ascii_masking_char;
  }
  virtual masking_functions::charset_string process(
      const masking_functions::charset_string &cs_str,
      const masking_functions::charset_string &masking_char) const override {
    auto sresult =
        masking_functions::mask_inner(cs_str, 0, 36 - 8, masking_char);
    sresult =
        masking_functions::mask_inner(sresult, 9, 36 - 9 - 4, masking_char);
    sresult = masking_functions::mask_inner(sresult, 9 + 5, 36 - 9 - 5 - 4,
                                            masking_char);
    sresult = masking_functions::mask_inner(sresult, 9 + 5 + 5,
                                            36 - 9 - 5 - 5 - 4, masking_char);
    sresult =
        masking_functions::mask_inner(sresult, 9 + 5 + 5 + 5, 0, masking_char);

    return sresult;
  }
};

//
// gen_blocklist(string, string, string)
//
class gen_blocklist_impl {
 public:
  gen_blocklist_impl(mysqlpp::udf_context &ctx) {
    if (ctx.get_number_of_args() != 3)
      throw std::invalid_argument{
          "Wrong argument list: gen_blocklist(string, string, string)"};

    ctx.mark_result_nullable(true);
    ctx.mark_result_const(false);

    // arg1 - dictionary
    ctx.mark_arg_nullable(0, false);
    ctx.set_arg_type(0, STRING_RESULT);

    ctx.mark_arg_nullable(1, false);
    ctx.set_arg_type(1, STRING_RESULT);

    ctx.mark_arg_nullable(2, false);
    ctx.set_arg_type(2, STRING_RESULT);

    mysqlpp::udf_context_charset_extension charset_ext{
        mysql_service_mysql_udf_metadata};
    set_return_value_collation_from_arg(charset_ext, ctx, 0);
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate(
      const mysqlpp::udf_context &ctx) {
    const auto cs_term = extract_charset_string_from_arg(ctx, 0);
    const auto cs_dict_a = extract_charset_string_from_arg(ctx, 1);
    const auto cs_dict_b = extract_charset_string_from_arg(ctx, 2);

    {
      masking_functions::sql_context sql_ctx{
          global_command_services::instance()};

      auto query =
          global_query_builder::instance()
              .select_random_term_for_dictionary_and_term(cs_dict_a, cs_term);
      auto sresult = sql_ctx.query_single_value(query);

      if (!sresult) {
        return {std::string{cs_term.get_buffer()}};
      }
    }

    masking_functions::sql_context sql_ctx{global_command_services::instance()};

    auto query =
        global_query_builder::instance().select_random_term_for_dictionary(
            cs_dict_b);
    auto sresult = sql_ctx.query_single_value(query);

    if (sresult && sresult->size() > 0) {
      masking_functions::charset_string utf8_result{
          global_string_services::instance(), *sresult,
          masking_functions::charset_string::utf8mb4_collation_name};
      masking_functions::charset_string conversion_buffer;
      const auto &cs_result = masking_functions::smart_convert_to_collation(
          utf8_result, cs_term.get_collation(), conversion_buffer);
      return {std::string{cs_result.get_buffer()}};
    } else {
      return std::nullopt;
    }
  }
};

//
// gen_dictionary(string)
//
class gen_dictionary_impl {
 public:
  gen_dictionary_impl(mysqlpp::udf_context &ctx) {
    if (ctx.get_number_of_args() != 1)
      throw std::invalid_argument{
          "Wrong argument list: gen_dictionary(string)"};

    ctx.mark_result_nullable(true);
    ctx.mark_result_const(false);

    // arg1 - dictionary
    ctx.mark_arg_nullable(0, false);
    ctx.set_arg_type(0, STRING_RESULT);

    mysqlpp::udf_context_charset_extension charset_ext{
        mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_collation(
        ctx, masking_functions::charset_string::default_collation_name);
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate(
      const mysqlpp::udf_context &ctx) {
    const auto cs_dictionary = extract_charset_string_from_arg(ctx, 0);

    masking_functions::sql_context sql_ctx{global_command_services::instance()};

    auto query =
        global_query_builder::instance().select_random_term_for_dictionary(
            cs_dictionary);
    auto sresult = sql_ctx.query_single_value(query);

    if (sresult && sresult->size() > 0) {
      return *sresult;
    } else {
      return std::nullopt;
    }
  }
};

//
// masking_dictionary_remove(string)
//
class masking_dictionary_remove_impl {
 public:
  masking_dictionary_remove_impl(mysqlpp::udf_context &ctx) {
    if (!have_masking_admin_privilege()) {
      throw std::invalid_argument{
          "Function requires " +
          std::string(masking_dictionaries_privilege_name) + " privilege"};
    }

    if (ctx.get_number_of_args() != 1)
      throw std::invalid_argument{
          "Wrong argument list: masking_dictionary_remove(string)"};

    ctx.mark_result_nullable(true);
    ctx.mark_result_const(false);

    // arg1 - dictionary
    ctx.mark_arg_nullable(0, false);
    ctx.set_arg_type(0, STRING_RESULT);

    mysqlpp::udf_context_charset_extension charset_ext{
        mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_collation(
        ctx, masking_functions::charset_string::default_collation_name);
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate(
      const mysqlpp::udf_context &ctx) {
    const auto cs_dictionary = extract_charset_string_from_arg(ctx, 0);

    masking_functions::sql_context sql_ctx{global_command_services::instance()};

    auto query =
        global_query_builder::instance().delete_for_dictionary(cs_dictionary);
    if (sql_ctx.execute(query) == 0) {
      return std::nullopt;
    } else {
      return "1";
    }
  }
};

//
// masking_dictionary_term_add(string)
//
class masking_dictionary_term_add_impl {
 public:
  masking_dictionary_term_add_impl(mysqlpp::udf_context &ctx) {
    if (!have_masking_admin_privilege()) {
      throw std::invalid_argument{
          "Function requires " +
          std::string(masking_dictionaries_privilege_name) + " privilege"};
    }

    if (ctx.get_number_of_args() != 2)
      throw std::invalid_argument{
          "Wrong argument list: masking_dictionary_term_add(string, "
          "string)"};

    ctx.mark_result_nullable(true);
    ctx.mark_result_const(false);

    // arg1 - dictionary
    ctx.mark_arg_nullable(0, false);
    ctx.set_arg_type(0, STRING_RESULT);

    // arg2 - term
    ctx.mark_arg_nullable(1, false);
    ctx.set_arg_type(1, STRING_RESULT);

    mysqlpp::udf_context_charset_extension charset_ext{
        mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_collation(
        ctx, masking_functions::charset_string::default_collation_name);
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate(
      const mysqlpp::udf_context &ctx) {
    const auto cs_dictionary = extract_charset_string_from_arg(ctx, 0);
    const auto cs_term = extract_charset_string_from_arg(ctx, 1);

    masking_functions::sql_context sql_ctx{global_command_services::instance()};

    auto query = global_query_builder::instance().insert_ignore_record(
        cs_dictionary, cs_term);

    if (sql_ctx.execute(query) == 0) {
      return std::nullopt;
    } else {
      return "1";
    }
  }
};

//
// masking_dictionary_term_remove(string)
//
class masking_dictionary_term_remove_impl {
 public:
  masking_dictionary_term_remove_impl(mysqlpp::udf_context &ctx) {
    if (!have_masking_admin_privilege()) {
      throw std::invalid_argument{
          "Function requires " +
          std::string(masking_dictionaries_privilege_name) + " privilege"};
    }

    if (ctx.get_number_of_args() != 2)
      throw std::invalid_argument{
          "Wrong argument list: masking_dictionary_term_remove(string, "
          "string)"};

    ctx.mark_result_nullable(true);
    ctx.mark_result_const(false);

    // arg1 - dictionary
    ctx.mark_arg_nullable(0, false);
    ctx.set_arg_type(0, STRING_RESULT);

    // arg2 - term
    ctx.mark_arg_nullable(1, false);
    ctx.set_arg_type(1, STRING_RESULT);

    mysqlpp::udf_context_charset_extension charset_ext{
        mysql_service_mysql_udf_metadata};
    charset_ext.set_return_value_collation(
        ctx, masking_functions::charset_string::default_collation_name);
  }

  mysqlpp::udf_result_t<STRING_RESULT> calculate(
      const mysqlpp::udf_context &ctx) {
    const auto cs_dictionary = extract_charset_string_from_arg(ctx, 0);
    const auto cs_term = extract_charset_string_from_arg(ctx, 1);

    masking_functions::sql_context sql_ctx{global_command_services::instance()};

    auto query =
        global_query_builder::instance().delete_for_dictionary_and_term(
            cs_dictionary, cs_term);
    if (sql_ctx.execute(query) == 0) {
      return std::nullopt;
    } else {
      return "1";
    }
  }
};

}  // anonymous namespace

DECLARE_INT_UDF(gen_range_impl, gen_range)
DECLARE_STRING_UDF(gen_rnd_email_impl, gen_rnd_email)
DECLARE_STRING_UDF(gen_rnd_iban_impl, gen_rnd_iban)
DECLARE_STRING_UDF(gen_rnd_canada_sin_impl, gen_rnd_canada_sin)
DECLARE_STRING_UDF(gen_rnd_pan_impl, gen_rnd_pan)
DECLARE_STRING_UDF(gen_rnd_ssn_impl, gen_rnd_ssn)
DECLARE_STRING_UDF(gen_rnd_uk_nin_impl, gen_rnd_uk_nin)
DECLARE_STRING_UDF(gen_rnd_us_phone_impl, gen_rnd_us_phone)
DECLARE_STRING_UDF(gen_rnd_uuid_impl, gen_rnd_uuid)

DECLARE_STRING_UDF(mask_inner_impl, mask_inner)
DECLARE_STRING_UDF(mask_outer_impl, mask_outer)
DECLARE_STRING_UDF(mask_canada_sin_impl, mask_canada_sin)
DECLARE_STRING_UDF(mask_iban_impl, mask_iban)
DECLARE_STRING_UDF(mask_pan_impl, mask_pan)
DECLARE_STRING_UDF(mask_pan_relaxed_impl, mask_pan_relaxed)
DECLARE_STRING_UDF(mask_ssn_impl, mask_ssn)
DECLARE_STRING_UDF(mask_uk_nin_impl, mask_uk_nin)
DECLARE_STRING_UDF(mask_uuid_impl, mask_uuid)
DECLARE_STRING_UDF(gen_blocklist_impl, gen_blocklist)
DECLARE_STRING_UDF(gen_dictionary_impl, gen_dictionary)
DECLARE_STRING_UDF(masking_dictionary_remove_impl, masking_dictionary_remove)
DECLARE_STRING_UDF(masking_dictionary_term_add_impl,
                   masking_dictionary_term_add)
DECLARE_STRING_UDF(masking_dictionary_term_remove_impl,
                   masking_dictionary_term_remove)

/* The UDFs we will register. */
std::array known_udfs{
    DECLARE_UDF_INFO(gen_range, INT_RESULT),
    DECLARE_UDF_INFO(gen_rnd_email, STRING_RESULT),
    DECLARE_UDF_INFO(gen_rnd_iban, STRING_RESULT),
    DECLARE_UDF_INFO(gen_rnd_canada_sin, STRING_RESULT),
    DECLARE_UDF_INFO(gen_rnd_pan, STRING_RESULT),
    DECLARE_UDF_INFO(gen_rnd_ssn, STRING_RESULT),
    DECLARE_UDF_INFO(gen_rnd_uk_nin, STRING_RESULT),
    DECLARE_UDF_INFO(gen_rnd_us_phone, STRING_RESULT),
    DECLARE_UDF_INFO(gen_rnd_uuid, STRING_RESULT),

    DECLARE_UDF_INFO(mask_inner, STRING_RESULT),
    DECLARE_UDF_INFO(mask_outer, STRING_RESULT),
    DECLARE_UDF_INFO(mask_canada_sin, STRING_RESULT),
    DECLARE_UDF_INFO(mask_iban, STRING_RESULT),
    DECLARE_UDF_INFO(mask_pan, STRING_RESULT),
    DECLARE_UDF_INFO(mask_pan_relaxed, STRING_RESULT),
    DECLARE_UDF_INFO(mask_ssn, STRING_RESULT),
    DECLARE_UDF_INFO(mask_uk_nin, STRING_RESULT),
    DECLARE_UDF_INFO(mask_uuid, STRING_RESULT),
    DECLARE_UDF_INFO(gen_blocklist, STRING_RESULT),
    DECLARE_UDF_INFO(gen_dictionary, STRING_RESULT),
    DECLARE_UDF_INFO(masking_dictionary_remove, STRING_RESULT),
    DECLARE_UDF_INFO(masking_dictionary_term_add, STRING_RESULT),
    DECLARE_UDF_INFO(masking_dictionary_term_remove, STRING_RESULT)};

using udf_bitset_type =
    mysqlpp::udf_bitset<std::tuple_size_v<decltype(known_udfs)>>;
static udf_bitset_type registered_udfs;

static bool privileges_registered = false;

namespace masking_functions {

bool register_dynamic_privileges() {
  if (privileges_registered) {
    return true;
  }
  if (mysql_service_dynamic_privilege_register->register_privilege(
          masking_dictionaries_privilege_name.data(),
          masking_dictionaries_privilege_name.size()) == 0) {
    privileges_registered = true;
  }
  return privileges_registered;
}

bool unregister_dynamic_privileges() {
  if (!privileges_registered) {
    return true;
  }
  if (mysql_service_dynamic_privilege_register->unregister_privilege(
          masking_dictionaries_privilege_name.data(),
          masking_dictionaries_privilege_name.size()) == 0) {
    privileges_registered = false;
  }
  return !privileges_registered;
}

bool register_udfs() {
  mysqlpp::register_udfs(mysql_service_udf_registration, known_udfs,
                         registered_udfs);

  return registered_udfs.all();
}

bool unregister_udfs() {
  mysqlpp::unregister_udfs(mysql_service_udf_registration, known_udfs,
                           registered_udfs);

  return registered_udfs.none();
}

}  // namespace masking_functions
