/* Copyright (c) 2020, 2024, Oracle and/or its affiliates.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is designed to work with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have either included with
   the program or referenced in the documentation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef SSL_ACCEPTOR_CONTEXT_OPERATOR
#define SSL_ACCEPTOR_CONTEXT_OPERATOR

#include <atomic>
#include <memory>
#include <string>

#include "sql/ssl_acceptor_context_data.h" /** Ssl_acceptor_context_data */

/* Types of supported contexts */
enum class Ssl_acceptor_context_type {
  context_server_main = 0,
  context_server_admin,
  context_last
};

using Ssl_acceptor_context_data_ptr =
    std::shared_ptr<const Ssl_acceptor_context_data>;

extern Ssl_acceptor_context_data_ptr mysql_main;
extern Ssl_acceptor_context_data_ptr mysql_admin;

/** TLS context manager */
class TLS_channel {
 public:
  /**
  Initialize the single instance of the acceptor

  @param [out] out          Object initialized by the function
  @param [in]  channel      Name of the channel
  @param [in]  use_ssl_arg  Pass false if you don't want the actual
                            SSL context created
                            (as in when SSL is initially disabled)
  @param [in]  callbacks    Handle to the initialization callback object
  @param [in]  db_init      Whether database is being initialized or not

  @returns Initialization status
    @retval true failure to init
    @retval false initialized ok
*/
  static bool singleton_init(Ssl_acceptor_context_data_ptr &out,
                             const std::string &channel, bool use_ssl_arg,
                             Ssl_init_callback *callbacks, bool db_init);

  /**
    De-initialize the single instance of the acceptor

    @param [in] data TLS acceptor context object
  */
  static void singleton_deinit(Ssl_acceptor_context_data_ptr &data);
  /**
    Re-initialize the single instance of the acceptor

    @param [in,out] data      TLS acceptor context object
    @param [in]     channel   Name of the channel
    @param [in]     callbacks Handle to the initialization callback object
    @param [out]    error     SSL Error information
    @param [in]     force     Activate the SSL settings even if this will lead
                              to disabling SSL
  */
  static void singleton_flush(Ssl_acceptor_context_data_ptr &data,
                              const std::string &channel,
                              Ssl_init_callback *callbacks,
                              enum enum_ssl_init_error *error, bool force);
};

/** TLS context access wrapper for ease of use */
class Lock_and_access_ssl_acceptor_context_data {
 public:
  Lock_and_access_ssl_acceptor_context_data(
      const Ssl_acceptor_context_data_ptr &data)
      : data_(std::atomic_load(&data)) {}
  Lock_and_access_ssl_acceptor_context_data(
      const Lock_and_access_ssl_acceptor_context_data &) = delete;
  Lock_and_access_ssl_acceptor_context_data &operator=(
      const Lock_and_access_ssl_acceptor_context_data &) = delete;
  Lock_and_access_ssl_acceptor_context_data(
      Lock_and_access_ssl_acceptor_context_data &&) = delete;
  Lock_and_access_ssl_acceptor_context_data &operator=(
      Lock_and_access_ssl_acceptor_context_data &&) = delete;
  ~Lock_and_access_ssl_acceptor_context_data() = default;

  /** Access protected @ref Ssl_acceptor_context_data */
  const Ssl_acceptor_context_data *get() { return data_.get(); }

  /**
    Access to st_VioSSLFd from the protected @ref Ssl_acceptor_context_data
  */
  st_VioSSLFd *get_vio_ssl_fd() { return data_->ssl_acceptor_fd_; }

  /**
    Fetch given property from underlying TLS context

    @param [in] property_type Property to be fetched

   @returns Value of property for given context. Empty in case of failure.
  */
  std::string show_property(
      Ssl_acceptor_context_property_type property_type) const;

  /**
    Fetch channel name

    @returns Name of underlying channel
  */
  std::string channel_name() const;

  /**
    TLS context validity

    @returns Validity of TLS context
      @retval true  Valid
      @retval false Invalid
  */
  bool have_ssl() const;

 private:
  /** Shallow copy of the TLS context */
  Ssl_acceptor_context_data_ptr data_;
};

bool have_ssl();

#endif  // SSL_ACCEPTOR_CONTEXT_OPERATOR
