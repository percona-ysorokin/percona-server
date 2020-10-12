/*
  Copyright (c) 2018, 2020, Oracle and/or its affiliates. All rights reserved.

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
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "mysql/harness/tls_context.h"

#include <array>
#include <string>
#include <vector>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include "mysql/harness/stdx/expected.h"
#include "mysql/harness/tls_error.h"
#include "openssl_version.h"

TlsLibraryContext::TlsLibraryContext() {
#if OPENSSL_VERSION_NUMBER >= ROUTER_OPENSSL_VERSION(1, 1, 0)
  OPENSSL_init_ssl(0, nullptr);
#else
  SSL_library_init();
#endif
  SSL_load_error_strings();
  ERR_load_crypto_strings();
}

TlsContext::TlsContext(const SSL_METHOD *method)
    : ssl_ctx_{SSL_CTX_new(const_cast<SSL_METHOD *>(method)), &SSL_CTX_free} {
  // SSL_CTX_new may fail if ciphers aren't loaded.
}

stdx::expected<void, std::error_code> TlsContext::ssl_ca(
    const std::string &ca_file, const std::string &ca_path) {
  if (!ssl_ctx_) {
    return stdx::make_unexpected(make_error_code(std::errc::invalid_argument));
  }

  if (1 != SSL_CTX_load_verify_locations(
               ssl_ctx_.get(), ca_file.empty() ? nullptr : ca_file.c_str(),
               ca_path.empty() ? nullptr : ca_path.c_str())) {
    return stdx::make_unexpected(make_tls_error());
  }
  return {};
}

stdx::expected<void, std::error_code> TlsContext::crl(
    const std::string &crl_file, const std::string &crl_path) {
  if (!ssl_ctx_) {
    return stdx::make_unexpected(make_error_code(std::errc::invalid_argument));
  }

  auto *store = SSL_CTX_get_cert_store(ssl_ctx_.get());

  if (1 != X509_STORE_load_locations(
               store, crl_file.empty() ? nullptr : crl_file.c_str(),
               crl_path.empty() ? nullptr : crl_path.c_str())) {
    return stdx::make_unexpected(make_tls_error());
  }

  if (1 != X509_STORE_set_flags(
               store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL)) {
    return stdx::make_unexpected(make_tls_error());
  }

  return {};
}

stdx::expected<void, std::error_code> TlsContext::curves_list(
    const std::string &curves) {
  if (curves.empty()) return {};

#if OPENSSL_VERSION_NUMBER >= ROUTER_OPENSSL_VERSION(1, 0, 2)
  if (1 != SSL_CTX_set1_curves_list(ssl_ctx_.get(),
                                    const_cast<char *>(curves.c_str()))) {
    return stdx::make_unexpected(make_tls_error());
  }
  return {};
#else
  return stdx::make_unexpected(
      make_error_code(std::errc::function_not_supported));
#endif
}

#if OPENSSL_VERSION_NUMBER >= ROUTER_OPENSSL_VERSION(1, 1, 0)
static int o11x_version(TlsVersion version) {
  switch (version) {
    case TlsVersion::AUTO:
      return 0;
    case TlsVersion::SSL_3:
      return SSL3_VERSION;
    case TlsVersion::TLS_1_0:
      return TLS1_VERSION;
    case TlsVersion::TLS_1_1:
      return TLS1_1_VERSION;
    case TlsVersion::TLS_1_2:
      return TLS1_2_VERSION;
#if OPENSSL_VERSION_NUMBER >= ROUTER_OPENSSL_VERSION(1, 1, 1)
    case TlsVersion::TLS_1_3:
      return TLS1_3_VERSION;
#endif
    default:
      throw std::invalid_argument("version out of range");
  }
}
#endif

stdx::expected<void, std::error_code> TlsContext::version_range(
    TlsVersion min_version, TlsVersion max_version) {
// set min TLS version
#if OPENSSL_VERSION_NUMBER >= ROUTER_OPENSSL_VERSION(1, 1, 0)
  if (1 != SSL_CTX_set_min_proto_version(ssl_ctx_.get(),
                                         o11x_version(min_version))) {
    return stdx::make_unexpected(make_tls_error());
  }
  if (1 != SSL_CTX_set_max_proto_version(ssl_ctx_.get(),
                                         o11x_version(max_version))) {
    return stdx::make_unexpected(make_tls_error());
  }
#else
  // disable all by default
  long opts = SSL_CTX_clear_options(
      ssl_ctx_.get(), SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 |
                          SSL_OP_NO_TLSv1_1 | SSL_OP_NO_TLSv1_2);
  switch (min_version) {
    default:
      // unknown, leave all disabled
      // fallthrough
    case TlsVersion::TLS_1_3:
      opts |= SSL_OP_NO_TLSv1_2;
      // fallthrough
    case TlsVersion::TLS_1_2:
      opts |= SSL_OP_NO_TLSv1_1;
      // fallthrough
    case TlsVersion::TLS_1_1:
      opts |= SSL_OP_NO_TLSv1;
      // fallthrough
    case TlsVersion::TLS_1_0:
      opts |= SSL_OP_NO_SSLv3;
      // fallthrough
    case TlsVersion::AUTO:
    case TlsVersion::SSL_3:
      opts |= SSL_OP_NO_SSLv2;
      break;
  }

  switch (max_version) {
      // fallthrough
    case TlsVersion::SSL_3:
      opts |= SSL_OP_NO_TLSv1;
      // fallthrough
    case TlsVersion::TLS_1_0:
      opts |= SSL_OP_NO_TLSv1_1;
      // fallthrough
    case TlsVersion::TLS_1_1:
      opts |= SSL_OP_NO_TLSv1_2;
      // fallthrough
    default:
      break;
  }

  // returns the updated options
  SSL_CTX_set_options(ssl_ctx_.get(), opts);

#endif
  return {};
}

TlsVersion TlsContext::min_version() const {
#if OPENSSL_VERSION_NUMBER >= ROUTER_OPENSSL_VERSION(1, 1, 1)
  switch (auto v = SSL_CTX_get_min_proto_version(ssl_ctx_.get())) {
    case SSL3_VERSION:
      return TlsVersion::SSL_3;
    case TLS1_VERSION:
      return TlsVersion::TLS_1_0;
    case TLS1_1_VERSION:
      return TlsVersion::TLS_1_1;
    case TLS1_2_VERSION:
      return TlsVersion::TLS_1_2;
    case TLS1_3_VERSION:
      return TlsVersion::TLS_1_3;
    case 0:
      return TlsVersion::AUTO;
    default:
      throw std::invalid_argument("unknown min-proto-version: " +
                                  std::to_string(v));
  }
#else
  auto opts = SSL_CTX_get_options(ssl_ctx_.get());

  if (opts & SSL_OP_NO_SSLv3) {
    if (opts & SSL_OP_NO_TLSv1) {
      if (opts & SSL_OP_NO_TLSv1_1) {
        if (opts & SSL_OP_NO_TLSv1_2) {
          return TlsVersion::TLS_1_3;
        } else {
          return TlsVersion::TLS_1_2;
        }
      } else {
        return TlsVersion::TLS_1_1;
      }
    } else {
      return TlsVersion::TLS_1_0;
    }
  } else {
    return TlsVersion::SSL_3;
  }
#endif
}

std::vector<std::string> TlsContext::cipher_list() const {
#if OPENSSL_VERSION_NUMBER >= ROUTER_OPENSSL_VERSION(1, 1, 0)
  // dump the cipher-list we actually have
  STACK_OF(SSL_CIPHER) *st = SSL_CTX_get_ciphers(ssl_ctx_.get());
  size_t num_ciphers = sk_SSL_CIPHER_num(st);

  std::vector<std::string> out(num_ciphers);
  for (size_t ndx = 0; ndx < num_ciphers; ++ndx) {
    auto *cipher = sk_SSL_CIPHER_value(st, ndx);
    out.emplace_back(SSL_CIPHER_get_name(cipher));
  }

  return out;
#else
  throw std::invalid_argument(
      "::cipher_list() isn't implemented. Use .has_get_cipher_list() "
      "to check before calling");
#endif
}

void TlsContext::info_callback(TlsContext::InfoCallback cb) {
  SSL_CTX_set_info_callback(ssl_ctx_.get(), cb);
}

TlsContext::InfoCallback TlsContext::info_callback() const {
  return SSL_CTX_get_info_callback(ssl_ctx_.get());
}

int TlsContext::security_level() const {
#if OPENSSL_VERSION_NUMBER >= ROUTER_OPENSSL_VERSION(1, 1, 0)
  return SSL_CTX_get_security_level(ssl_ctx_.get());
#else
  return 0;
#endif
}
