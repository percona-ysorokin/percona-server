/* Copyright (c) 2021, 2024, Oracle and/or its affiliates.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   Without limiting anything contained in the foregoing, this file,
   which is part of C Driver for MySQL (Connector/C), is also subject to the
   Universal FOSS Exception, version 1.0, a copy of which can be found at
   http://oss.oracle.com/licenses/universal-foss-exception.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include <common.h>
#include <my_dbug.h>
#include <mysql.h>
#include <mysql/client_plugin.h>

#include "fido_assertion.h"
#include "fido_registration.h"

#ifndef NDEBUG
static bool is_fido_testing = false;
#endif
static unsigned char registration_challenge[128] = {0};
static unsigned char *registration_challenge_response = nullptr;

static bool do_registration();

/*
  Handler to callback function which will pass informative messages generated by
  this plugin caller. This callback function is registered via
  mysql_plugin_option("fido_messages_callback")

  If callback is not registered, all messaged are redirected to stderr/stdout.
*/
plugin_messages_callback mc = nullptr;
plugin_messages_callback_get_uint mc_get_uint = nullptr;
plugin_messages_callback_get_password mc_get_password = nullptr;

/**
  authentication_fido_client plugin API to initialize
*/
static int fido_auth_client_plugin_init(char *, size_t, int, va_list) {
  fido_init(0);
  return 0;
}

/**
  Deinitialize authentication_fido_client plugin
*/
static int fido_auth_client_plugin_deinit() { return 0; }

/**
  authentication_fido_client plugin API to allow client to pass optional data
  for plugin to process
*/
static int fido_auth_client_plugin_option(const char *option, const void *val) {
#ifndef NDEBUG
  if (strcmp(option, "is_fido_testing") == 0) {
    is_fido_testing = *static_cast<const bool *>(val);
    return false;
  }
#endif
  if (strcmp(option, "fido_messages_callback") == 0) {
    mc = (plugin_messages_callback)(const_cast<void *>(val));
    return false;
  }
  if (strcmp(option, "registration_challenge") == 0) {
    unsigned char *p =
        reinterpret_cast<unsigned char *>(const_cast<void *>(val));
    memcpy(registration_challenge, p, strlen(reinterpret_cast<char *>(p)));
    /* finish registration */
    if (do_registration()) return true;
    return false;
  }
  return true;
}

/**
  authentication_fido_client plugin API to allow client to get optional data
  from plugin
*/
static int fido_auth_client_get_plugin_option(const char *option, void *val) {
  if (strcmp(option, "registration_response") == 0) {
    *(static_cast<unsigned char **>(val)) = registration_challenge_response;
  }
  return 0;
}

/**
   FIDO client side authentication method. This method does following:

   1. Receive challenge from server side FIDO plugin. This challenge
      comprises of salt, relying party name.
   2. Send this challenge to FIDO device and get the signed challenge.
      Signed challenge includes signature and authenticator data, which
      is to be verified by server side plugin with public key.

  @param [in] vio  Virtual I/O interface

  @return authentication status
  @retval CR_OK    Successful authentication
  @retval true     Authentication failure
*/
static int fido_auth_client(MYSQL_PLUGIN_VIO *vio, MYSQL *) {
  unsigned char *server_challenge = nullptr;
  int server_challenge_len = 0;

  /** Get the challenge from the MySQL server. */
  server_challenge_len = vio->read_packet(vio, &server_challenge);
  if (server_challenge_len == 0) {
    /*
      an empty packet means registration step is pending, thus for now allow
      connection with limited operations for user so that user can perform
      registration step.
    */
    return CR_OK_AUTH_IN_SANDBOX_MODE;
  }
  unsigned char *buff = nullptr;
  size_t length = 0;
#ifndef NDEBUG
  if (is_fido_testing) {
    length = 33;
    buff = new (std::nothrow) unsigned char[length];
    memcpy(buff, "\nsakila    \nsakila    \nsakila    ", length);
    vio->write_packet(vio, buff, length);
    delete[] buff;
    return CR_OK;
  } else
#endif
  {
    fido_assertion *fa = new fido_assertion();
    if (fa->parse_challenge(server_challenge) || fa->sign_challenge()) {
      delete fa;
      return true;
    }
    /* copy signed challenge into buff */
    fa->get_signed_challenge(&buff, length);
    /* send signed challenge to fido server plugin */
    vio->write_packet(vio, buff, length);
    delete fa;
    delete[] buff;
  }
  return CR_OK;
}

/**
   FIDO client side registration method. This method does following:

   1. Receive challenge from server side FIDO plugin. This challenge
      comprises of username, salt and relying party name.
   2. Send this challenge to FIDO device and get the signature, authenticator
      data and x509 certificate generated by device. This is sent to server
      as challenge response.

  @return registration status
  @retval false    Successful registration
  @retval true     Registration failure
*/
static bool do_registration() {
#ifndef NDEBUG
  if (is_fido_testing) {
    const char *dummy = "\nSIGNATURE \nAUTHDATA \nCERT      ";
    const size_t sz = strlen(dummy);
    memcpy(registration_challenge, dummy, sz);
    /* dummy challenge response for testing */
    registration_challenge_response = new unsigned char[sz + 1];
    memcpy(registration_challenge_response, dummy, sz);
    registration_challenge_response[sz] = 0;
    return false;
  } else
#endif
  {
    fido_registration *fr = new fido_registration();
    if (fr->make_credentials(const_cast<const char *>(
            reinterpret_cast<char *>(registration_challenge)))) {
      delete fr;
      return true;
    }
    if (fr->make_challenge_response(registration_challenge_response)) {
      delete fr;
      return true;
    }
    delete fr;
  }
  return false;
}

mysql_declare_client_plugin(AUTHENTICATION) "authentication_fido_client",
    MYSQL_CLIENT_PLUGIN_AUTHOR_ORACLE, "Fido Client Authentication Plugin",
    {0, 1, 0}, "GPL", nullptr, fido_auth_client_plugin_init,
    fido_auth_client_plugin_deinit, fido_auth_client_plugin_option,
    fido_auth_client_get_plugin_option, fido_auth_client,
    nullptr, mysql_end_client_plugin;
