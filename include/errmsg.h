#ifndef ERRMSG_INCLUDED
#define ERRMSG_INCLUDED

/* Copyright (c) 2000, 2015, Oracle and/or its affiliates. All rights reserved.

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

/**
  @file include/errmsg.h
  Error messages for MySQL clients.
*/

#ifdef	__cplusplus
extern "C" {
#endif
void	init_client_errs(void);
void	finish_client_errs(void);
extern const char *client_errors[];	/* Error messages */
#ifdef	__cplusplus
}
#endif

#define CR_MIN_ERROR		2000	/* For easier client code */
#define CR_MAX_ERROR		2999
#define CLIENT_ERRMAP		2	/* Errormap used by my_error() */

/* Do not add error numbers before CR_ERROR_FIRST. */
/* If necessary to add lower numbers, change CR_ERROR_FIRST accordingly. */
#define CR_ERROR_FIRST  	2000 /*Copy first error nr.*/
#define CR_UNKNOWN_ERROR	2000
#define CR_SOCKET_CREATE_ERROR	2001
#define CR_CONNECTION_ERROR	2002
#define CR_CONN_HOST_ERROR	2003
#define CR_IPSOCK_ERROR		2004
#define CR_UNKNOWN_HOST		2005
#define CR_SERVER_GONE_ERROR	2006
#define CR_VERSION_ERROR	2007
#define CR_OUT_OF_MEMORY	2008
#define CR_WRONG_HOST_INFO	2009
#define CR_LOCALHOST_CONNECTION 2010
#define CR_TCP_CONNECTION	2011
#define CR_SERVER_HANDSHAKE_ERR 2012
#define CR_SERVER_LOST		2013
#define CR_COMMANDS_OUT_OF_SYNC 2014
#define CR_NAMEDPIPE_CONNECTION 2015
#define CR_NAMEDPIPEWAIT_ERROR  2016
#define CR_NAMEDPIPEOPEN_ERROR  2017
#define CR_NAMEDPIPESETSTATE_ERROR 2018
#define CR_CANT_READ_CHARSET	2019
#define CR_NET_PACKET_TOO_LARGE 2020
#define CR_EMBEDDED_CONNECTION	2021
#define CR_PROBE_SLAVE_STATUS   2022
#define CR_PROBE_SLAVE_HOSTS    2023
#define CR_PROBE_SLAVE_CONNECT  2024
#define CR_PROBE_MASTER_CONNECT 2025
#define CR_SSL_CONNECTION_ERROR 2026
#define CR_MALFORMED_PACKET     2027
#define CR_WRONG_LICENSE	2028

/* new 4.1 error codes */
#define CR_NULL_POINTER		2029
#define CR_NO_PREPARE_STMT	2030
#define CR_PARAMS_NOT_BOUND	2031
#define CR_DATA_TRUNCATED	2032
#define CR_NO_PARAMETERS_EXISTS 2033
#define CR_INVALID_PARAMETER_NO 2034
#define CR_INVALID_BUFFER_USE	2035
#define CR_UNSUPPORTED_PARAM_TYPE 2036

#define CR_SHARED_MEMORY_CONNECTION             2037
#define CR_SHARED_MEMORY_CONNECT_REQUEST_ERROR  2038
#define CR_SHARED_MEMORY_CONNECT_ANSWER_ERROR   2039
#define CR_SHARED_MEMORY_CONNECT_FILE_MAP_ERROR 2040
#define CR_SHARED_MEMORY_CONNECT_MAP_ERROR      2041
#define CR_SHARED_MEMORY_FILE_MAP_ERROR         2042
#define CR_SHARED_MEMORY_MAP_ERROR              2043
#define CR_SHARED_MEMORY_EVENT_ERROR     	2044
#define CR_SHARED_MEMORY_CONNECT_ABANDONED_ERROR 2045
#define CR_SHARED_MEMORY_CONNECT_SET_ERROR      2046
#define CR_CONN_UNKNOW_PROTOCOL 		2047
#define CR_INVALID_CONN_HANDLE			2048
#define CR_UNUSED_1                             2049
#define CR_FETCH_CANCELED                       2050
#define CR_NO_DATA                              2051
#define CR_NO_STMT_METADATA                     2052
#define CR_NO_RESULT_SET                        2053
#define CR_NOT_IMPLEMENTED                      2054
#define CR_SERVER_LOST_EXTENDED			2055
#define CR_STMT_CLOSED				2056
#define CR_NEW_STMT_METADATA                    2057
#define CR_ALREADY_CONNECTED                    2058
#define CR_AUTH_PLUGIN_CANNOT_LOAD              2059
#define CR_DUPLICATE_CONNECTION_ATTR            2060
#define CR_AUTH_PLUGIN_ERR                      2061
#define CR_INSECURE_API_ERR                     2062
#define CR_ERROR_LAST  /*Copy last error nr:*/  2062
/* Add error numbers before CR_ERROR_LAST and change it accordingly. */

static inline const char* ER_CLIENT(int client_errno)
{
  if (client_errno >= CR_ERROR_FIRST && client_errno <= CR_ERROR_LAST)
    return client_errors[client_errno - CR_ERROR_FIRST];
  return client_errors[CR_UNKNOWN_ERROR];
}

#endif /* ERRMSG_INCLUDED */
