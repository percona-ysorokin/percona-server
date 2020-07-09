/*
   Copyright (c) 2020, Oracle and/or its affiliates. All rights reserved.

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
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include <stdio.h>
#include <string.h>

#include "kernel/signaldata/FsOpenReq.hpp"
#include "portlib/ndb_file.h"
#include "util/ndb_opts.h"
#include "util/ndbxfrm_buffer.h"
#include "util/ndbxfrm_iterator.h"
#include "util/ndbxfrm_readfile.h"
#include "util/ndbxfrm_writefile.h"
#include "util/ndb_openssl_evp.h"

//#define DUMMY_PASSWORD

using byte = unsigned char;

static int g_compress = 0;
static char* g_decrypt_password = nullptr;
static size_t g_decrypt_password_length = 0;
static char* g_encrypt_password = nullptr;
static size_t g_encrypt_password_length = 0;
static int g_info = 0;
static int g_encrypt_kdf_iter_count = 100000;
#if defined(TODO_READ_REVERSE)
static int g_read_reverse = 0;
#endif
#if defined(DUMMY_PASSWORD)
static char g_dummy_password[] = "DUMMY";
#endif

static struct my_option my_long_options[] =
{
  // Generic options from NDB_STD_OPTS_COMMON
  { "usage", '?', "Display this help and exit.", \
    0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0 }, \
  { "help", '?', "Display this help and exit.", \
    0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0 }, \
  { "version", 'V', "Output version information and exit.", 0, 0, 0, \
    GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0 }, \

  // Specific options
  { "compress", 'c', "Compress file",
    (uchar**) &g_compress, (uchar**) &g_compress, 0,
    GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0 },
  { "decrypt-password", NDB_OPT_NOSHORT, "Decryption password",
    (uchar**) &g_decrypt_password, (uchar**) &g_decrypt_password, 0,
    GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0 },
  { "encrypt-kdf-iter-count", 'k', "Iteration count to used in key definition",
    (uchar**) &g_encrypt_kdf_iter_count, (uchar**) &g_encrypt_kdf_iter_count, 0,
    GET_INT, REQUIRED_ARG, 100000, 0, INT_MAX, 0, 0, 0 },
  { "encrypt-password", NDB_OPT_NOSHORT, "Encryption password",
    (uchar**) &g_encrypt_password, (uchar**) &g_encrypt_password, 0,
    GET_STR, REQUIRED_ARG, 0, 0, 0, 0, 0, 0 },
  { "info", 'i', "Print info about file",
    (uchar**) &g_info, (uchar**) &g_info, 0,
    GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0 },
#if defined(TODO_READ_REVERSE)
  { "read-reverse", 'R', "Read file in reverse",
    (uchar**) &g_read_reverse, (uchar**) &g_read_reverse, 0,
    GET_BOOL, NO_ARG, 0, 0, 0, 0, 0, 0 },
#endif
  { 0, 0, 0, 0, 0, 0, GET_NO_ARG, NO_ARG, 0, 0, 0, 0, 0, 0}
};

static const char* load_defaults_groups[] = { "ndbxfrm", nullptr };

static int dump_info(const char name[]);
static int copy_file(const char src[], const char dst[]);

int main(int argc, char* argv[])
{
  NDB_INIT(argv[0]);
  Ndb_opts opts(argc, argv, my_long_options, load_defaults_groups);
  if (opts.handle_options())
    return 2;

#if defined(TODO_READ_REVERSE)
  bool do_write = (g_compress || (g_encrypt_password != nullptr));

  if (g_read_reverse && do_write)
  {
    fprintf(stderr,
            "Error: Writing with encryption (--encrypt-password) or compression "
            "(--compress) not allowed when reading in reverse "
            "(--read-reverse).\n");
    return 1;
  }

  if (g_info && (do_write || g_read_reverse))
  {
    fprintf(stderr,
            "Error: Writing (--encrypt-password, --compress) or reading "
            "(--read-reverse) file not allowed in connection with --info.\n");
    return 1;
  }
#endif

#if defined(DUMMY_PASSWORD)
  /*
   * Replace given password to hard coded version to match what data nodes
   * and ndb_restore use.
   */
  if (g_decrypt_password)
  {
    g_decrypt_password = g_dummy_password;
  }
  if (g_encrypt_password)
  {
    g_encrypt_password = g_dummy_password;
  } 
#endif

  if (g_decrypt_password)
  {
    g_decrypt_password_length = strlen(g_decrypt_password);
  }

  if (g_encrypt_password)
  {
    g_encrypt_password_length = strlen(g_encrypt_password);
  }

  if (g_info)
  {
    for (int argi = 0; argi < argc; argi++)
    {
      dump_info(argv[argi]);
    }
    return 0;
  }

  if (argc != 2)
  {
    fprintf(stderr, "Error: Need one source file and one destination file.");
    return 1;
  }

  ndb_openssl_evp::library_init();

  int rc = copy_file(argv[0], argv[1]);

  ndb_openssl_evp::library_end();

  return rc;
}

int dump_info(const char name[])
{
  ndb_file file;
  ndbxfrm_readfile xfrm;
  int r;

  r = file.open(name, FsOpenReq::OM_READONLY);
  if (r == -1)
  {
    fprintf(stderr,
            "Error: Could not open file '%s' for read.\n",
            name);
    return 1;
  }

  r = xfrm.open(file,
                reinterpret_cast<byte*>(g_decrypt_password),
                g_decrypt_password_length);
  require(r == 0);

  printf("File=%s, compression=%s, encryption=%s\n",
         name,
         xfrm.is_compressed() ? "yes" : "no",
         xfrm.is_encrypted() ? "yes" : "no");

  xfrm.close();
  file.close();

  return 0;
}

int copy_file(const char src[], const char dst[])
{
  ndb_file src_file;
  ndb_file dst_file;

  int r;

  if (dst_file.create(dst) != 0)
  {
    fprintf(stderr,
            "Error: Could not create file '%s'.\n",
            dst);
    perror(dst);
    return 1; // File exists?
  }

  r = src_file.open(src, FsOpenReq::OM_READONLY);
  if (r == -1)
  {
    fprintf(stderr,
            "Error: Could not open file '%s' for read.\n",
            src);
    perror(src);
    return 1;
  }

  r = dst_file.open(dst, FsOpenReq::OM_WRITEONLY);
  if (r == -1)
  {
    fprintf(stderr,
            "Error: Could not open file '%s' for write.\n",
            dst);
    perror(dst);
    src_file.close();
    return 1;
  }

  ndbxfrm_readfile src_xfrm;
  ndbxfrm_writefile dst_xfrm;

  r = src_xfrm.open(src_file,
                    reinterpret_cast<byte*>(g_decrypt_password),
                    g_decrypt_password_length);
  require(r == 0);

  r = dst_xfrm.open(dst_file,
                    g_compress,
                    reinterpret_cast<byte*>(g_encrypt_password),
                    g_encrypt_password_length,
                    g_encrypt_kdf_iter_count);
  require(r == 0);

  // Copy data

  ndbxfrm_buffer buffer;
  buffer.init();
  for (;;)
  {
    ndbxfrm_input_iterator wr_it = buffer.get_input_iterator();
    if (dst_xfrm.write_forward(&wr_it) == -1)
    {
      fprintf(stderr, "Error: Can not write file %s.\n", src);
      r = 2; // write failure
      break;
    }
    buffer.update_read(wr_it);
    buffer.rebase(0);

    if (buffer.last() && buffer.read_size() == 0)
    {
      break; // All read and written
    }

    ndbxfrm_output_iterator rd_it = buffer.get_output_iterator();
    if (src_xfrm.read_forward(&rd_it) == -1)
    {
      if (src_xfrm.is_encrypted())
      {
         fprintf(stderr, "Error: Can not read file %s, bad password?\n", src);
      }
      else
      {
         fprintf(stderr, "Error: Can not read file %s.\n", src);
      }
      r = 2; // read failure
      break;
    }
    buffer.update_write(rd_it);
  }
 
  src_xfrm.close();
  dst_xfrm.close(false);

  src_file.close();
  dst_file.sync();
  dst_file.close();

  return r;
}
