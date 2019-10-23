/* Copyright (c) 2018 Percona LLC and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; version 2 of
   the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */

#ifndef MYSQL_VAULT_PARSER_H
#define MYSQL_VAULT_PARSER_H

#include "my_global.h"
#include "i_vault_parser.h"
#include "logger.h"
#include "secure_string.h"
//#include "vault_credentials.h"

namespace keyring
{

class Vault_parser : public IVault_parser
{
public:
  Vault_parser(ILogger *logger)
    : logger(logger)
  {}

  virtual bool parse_keys(const Secure_string &payload, Vault_keys_list *keys);
  virtual bool parse_key_data(const Secure_string &payload, IKey *key);
  virtual bool parse_key_signature(const Secure_string &base64_key_signature, KeyParameters *key_parameters);
  virtual bool parse_errors(const Secure_string &payload, Secure_string *errors);
  virtual bool get_vault_version(const Vault_credentials &vault_credentials, const Secure_string &mount_points_payload,
                                 int &vault_version);

private:
  typedef std::vector<Secure_string> Tokens;

  bool retrieve_tag_value(const Secure_string &payload, const Secure_string &tag, const char opening_bracket,
                          const char closing_bracket, Secure_string *value);
  bool retrieve_list(const Secure_string &payload, const Secure_string &list_name, Secure_string *list);
  bool retrieve_map(const Secure_string &payload, const Secure_string &map_name, Secure_string *map);
  bool retrieve_tokens_from_list(const Secure_string &list, Tokens *tokens);
  bool retrieve_value_from_map(const Secure_string &map, const Secure_string &key,
                               Secure_string *value);

  bool is_null_tag(Secure_string tag);
  bool is_empty_map(Secure_string map);

  ILogger *logger;
  const static size_t start_tag_length;
};

} // namespace keyring

#endif // MYSQL_VAULT_PARSER_H
