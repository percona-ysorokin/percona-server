/*
   Copyright (c) 2013, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include "socket_connection.h"

#include "my_net.h"                     // addrinfo
#include "violite.h"                    // Vio
#include "channel_info.h"               // Channel_info
#include "connection_handler_manager.h" // Connection_handler_manager
#include "mysqld.h"                     // key_socket_tcpip
#include "log.h"                        // sql_print_error
#include "sql_class.h"                  // THD

#include <algorithm>
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif

using std::max;

ulong Mysqld_socket_listener::connection_errors_select= 0;
ulong Mysqld_socket_listener::connection_errors_accept= 0;
ulong Mysqld_socket_listener::connection_errors_tcpwrap= 0;

#ifdef HAVE_PSI_STATEMENT_INTERFACE
PSI_statement_info stmt_info_new_packet;
#endif

void net_before_header_psi(struct st_net *net, void *user_data, size_t /* unused: count */)
{
  THD *thd;
  thd= static_cast<THD*> (user_data);
  DBUG_ASSERT(thd != NULL);

  if (thd->m_server_idle)
  {
    /*
      The server is IDLE, waiting for the next command.
      Technically, it is a wait on a socket, which may take a long time,
      because the call is blocking.
      Disable the socket instrumentation, to avoid recording a SOCKET event.
      Instead, start explicitly an IDLE event.
    */
    MYSQL_SOCKET_SET_STATE(net->vio->mysql_socket, PSI_SOCKET_STATE_IDLE);
    MYSQL_START_IDLE_WAIT(thd->m_idle_psi, &thd->m_idle_state);
  }
}

void net_after_header_psi(struct st_net *net, void *user_data, size_t /* unused: count */, my_bool rc)
{
  THD *thd;
  thd= static_cast<THD*> (user_data);
  DBUG_ASSERT(thd != NULL);

  if (thd->m_server_idle)
  {
    /*
      The server just got data for a network packet header,
      from the network layer.
      The IDLE event is now complete, since we now have a message to process.
      We need to:
      - start a new STATEMENT event
      - start a new STAGE event, within this statement,
      - start recording SOCKET WAITS events, within this stage.
      The proper order is critical to get events numbered correctly,
      and nested in the proper parent.
    */
    MYSQL_END_IDLE_WAIT(thd->m_idle_psi);

    if (! rc)
    {
      thd->m_statement_psi= MYSQL_START_STATEMENT(&thd->m_statement_state,
                                                  stmt_info_new_packet.m_key,
                                                  thd->db, thd->db_length,
                                                  thd->charset(), NULL);

      THD_STAGE_INFO(thd, stage_init);
    }

    /*
      TODO: consider recording a SOCKET event for the bytes just read,
      by also passing count here.
    */
    MYSQL_SOCKET_SET_STATE(net->vio->mysql_socket, PSI_SOCKET_STATE_ACTIVE);
  }
}


static void init_net_server_extension(THD *thd)
{
#ifdef HAVE_PSI_INTERFACE
  /* Start with a clean state for connection events. */
  thd->m_idle_psi= NULL;
  thd->m_statement_psi= NULL;
  thd->m_server_idle= false;
  /* Hook up the NET_SERVER callback in the net layer. */
  thd->m_net_server_extension.m_user_data= thd;
  thd->m_net_server_extension.m_before_header= net_before_header_psi;
  thd->m_net_server_extension.m_after_header= net_after_header_psi;
  /* Activate this private extension for the mysqld server. */
  thd->net.extension= & thd->m_net_server_extension;
#else
  thd->net.extension= NULL;
#endif
}


///////////////////////////////////////////////////////////////////////////
// Channel_info_local_socket implementation
///////////////////////////////////////////////////////////////////////////

/**
  This class abstracts the info. about local socket mode of communication with
  the server.
*/
class Channel_info_local_socket : public Channel_info
{
  // listen socket object
  MYSQL_SOCKET m_listen_sock;
  // connect socket object
  MYSQL_SOCKET m_connect_sock;

protected:
  virtual Vio* create_and_init_vio() const
  {
    return mysql_socket_vio_new(m_connect_sock, VIO_TYPE_SOCKET, VIO_LOCALHOST);
  }

public:
  /**
    Constructor that sets listen and connect socket.

    @param listen_socket  set listen socket descriptor.
    @param connect_socket set connect socket descriptor.
  */
  Channel_info_local_socket(MYSQL_SOCKET listen_socket,
                            MYSQL_SOCKET connect_socket)
  : m_listen_sock(listen_socket),
    m_connect_sock(connect_socket)
  { }

  virtual THD* create_thd()
  {
    THD* thd= Channel_info::create_thd();

    if (thd != NULL)
    {
      init_net_server_extension(thd);
      thd->security_ctx->set_host(my_localhost);
    }
    return thd;
  }

  virtual void send_error_and_close_channel(uint errorcode,
                                            int error,
                                            bool senderror)
  {
    Channel_info::send_error_and_close_channel(errorcode, error, senderror);

    mysql_socket_shutdown(m_connect_sock, SHUT_RDWR);
    mysql_socket_close(m_connect_sock);
  }
};


///////////////////////////////////////////////////////////////////////////
// Channel_info_tcpip_socket implementation
///////////////////////////////////////////////////////////////////////////

/**
  This class abstracts the info. about TCP/IP socket mode of communication with
  the server.
*/
class Channel_info_tcpip_socket : public Channel_info
{
  // listen socket object
  MYSQL_SOCKET m_listen_sock;
  // connect socket object
  MYSQL_SOCKET m_connect_sock;

protected:
  virtual Vio* create_and_init_vio() const
  {
    return mysql_socket_vio_new(m_connect_sock, VIO_TYPE_TCPIP, 0);
  }

public:
  /**
    Constructor that sets listen and connect socket.

    @param listen_socket  set listen socket descriptor.
    @param connect_socket set connect socket descriptor.
  */
  Channel_info_tcpip_socket(MYSQL_SOCKET listen_socket,
                            MYSQL_SOCKET connect_socket)
  : m_listen_sock(listen_socket),
    m_connect_sock(connect_socket)
  { }

  virtual THD* create_thd()
  {
    THD* thd= Channel_info::create_thd();

    if (thd != NULL)
      init_net_server_extension(thd);
    return thd;
  }

  virtual void send_error_and_close_channel(uint errorcode,
                                            int error,
                                            bool senderror)
  {
    Channel_info::send_error_and_close_channel(errorcode, error, senderror);

    mysql_socket_shutdown(m_connect_sock, SHUT_RDWR);
    mysql_socket_close(m_connect_sock);
  }
};


///////////////////////////////////////////////////////////////////////////
// TCP_socket implementation
///////////////////////////////////////////////////////////////////////////

/**
  MY_BIND_ALL_ADDRESSES defines a special value for the bind-address option,
  which means that the server should listen to all available network addresses,
  both IPv6 (if available) and IPv4.

  Basically, this value instructs the server to make an attempt to bind the
  server socket to '::' address, and rollback to '0.0.0.0' if the attempt fails.
*/
const char *MY_BIND_ALL_ADDRESSES= "*";


/**
  TCP_socket class represents the TCP sockets abstraction. It provides
  the get_listener_socket that setup a TCP listener socket to listen.
*/
class TCP_socket
{
  std::string m_bind_addr_str;  // IP address as string.
  uint m_tcp_port; // TCP port to bind to
  uint m_backlog;  // Backlog length for queue of pending connections.
  uint m_port_timeout; // Port timeout

  MYSQL_SOCKET create_socket(const struct addrinfo *addrinfo_list,
                             int addr_family,
                             struct addrinfo **use_addrinfo)
  {
    for (const struct addrinfo *cur_ai= addrinfo_list; cur_ai != NULL;
         cur_ai= cur_ai->ai_next)
    {
      if (cur_ai->ai_family != addr_family)
        continue;

      MYSQL_SOCKET sock= mysql_socket_socket(key_socket_tcpip,
                                             cur_ai->ai_family,
                                             cur_ai->ai_socktype,
                                             cur_ai->ai_protocol);

      char ip_addr[INET6_ADDRSTRLEN];

      if (vio_getnameinfo(cur_ai->ai_addr, ip_addr, sizeof (ip_addr),
                          NULL, 0, NI_NUMERICHOST))
      {
        ip_addr[0]= 0;
      }

      if (mysql_socket_getfd(sock) == INVALID_SOCKET)
      {
        sql_print_error("Failed to create a socket for %s '%s': errno: %d.",
                        (addr_family == AF_INET) ? "IPv4" : "IPv6",
                        (const char *) ip_addr,
                        (int) socket_errno);
      }
      else
      {
        sql_print_information("Server socket created on IP: '%s'.",
                              (const char *) ip_addr);

        *use_addrinfo= (struct addrinfo *)cur_ai;
        return sock;
      }
    }

    return MYSQL_INVALID_SOCKET;
  }

public:
  /**
    Constructor that takes tcp port and ip address string and other
    related parameters to set up listener tcp to listen for connection
    events.

    @param  tcp_port  tcp port number.
    @param  bind_addr_str  ip address as string value.
    @param  back_log backlog specifying length of pending connection queue.
    @param  m_port_timeout port timeout value
  */
  TCP_socket(std::string bind_addr_str,
             uint tcp_port,
             uint backlog,
             uint port_timeout)
  : m_bind_addr_str(bind_addr_str),
    m_tcp_port(tcp_port),
    m_backlog(backlog),
    m_port_timeout(port_timeout)
  { }

  /**
    Set up a listener to listen for connection events.

    @retval   valid socket if successful else MYSQL_INVALID_SOCKET on failure.
  */
  MYSQL_SOCKET get_listener_socket()
  {
    struct addrinfo *ai;
    const char *bind_address_str= NULL;

    sql_print_information("Server hostname (bind-address): '%s'; port: %d",
                          m_bind_addr_str.c_str(), m_tcp_port);

    // Get list of IP-addresses associated with the bind-address.

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags= AI_PASSIVE;
    hints.ai_socktype= SOCK_STREAM;
    hints.ai_family= AF_UNSPEC;

    char port_buf[NI_MAXSERV];
    my_snprintf(port_buf, NI_MAXSERV, "%d", m_tcp_port);

    if (strcasecmp(my_bind_addr_str, MY_BIND_ALL_ADDRESSES) == 0)
    {
      /*
        That's the case when bind-address is set to a special value ('*'),
        meaning "bind to all available IP addresses". If the box supports
        the IPv6 stack, that means binding to '::'. If only IPv4 is available,
        bind to '0.0.0.0'.
      */

      bool ipv6_available= false;
      const char *ipv6_all_addresses= "::";
      if (!getaddrinfo(ipv6_all_addresses, port_buf, &hints, &ai))
      {
        /*
          IPv6 might be available (the system might be able to resolve an IPv6
          address, but not be able to create an IPv6-socket). Try to create a
          dummy IPv6-socket. Do not instrument that socket by P_S.
        */

        MYSQL_SOCKET s= mysql_socket_socket(0, AF_INET6, SOCK_STREAM, 0);
        ipv6_available= mysql_socket_getfd(s) != INVALID_SOCKET;
        mysql_socket_close(s);
      }
      if (ipv6_available)
      {
        sql_print_information("IPv6 is available.");

        // Address info (ai) for IPv6 address is already set.

        bind_address_str= ipv6_all_addresses;
      }
      else
      {
        sql_print_information("IPv6 is not available.");

        // Retrieve address info (ai) for IPv4 address.

        const char *ipv4_all_addresses= "0.0.0.0";
        if (getaddrinfo(ipv4_all_addresses, port_buf, &hints, &ai))
        {
          sql_print_error("%s: %s", ER_DEFAULT(ER_IPSOCK_ERROR), strerror(errno));

          sql_print_error("Can't start server: cannot resolve hostname!");
          return MYSQL_INVALID_SOCKET;
        }

        bind_address_str= ipv4_all_addresses;
      }
    }
    else
    {
      if (getaddrinfo(m_bind_addr_str.c_str(), port_buf, &hints, &ai))
      {
        sql_print_error("%s: %s", ER_DEFAULT(ER_IPSOCK_ERROR), strerror(errno));
        sql_print_error("Can't start server: cannot resolve hostname!");
        return MYSQL_INVALID_SOCKET;
      }

      bind_address_str= m_bind_addr_str.c_str();
    }

    // Log all the IP-addresses
    for (struct addrinfo *cur_ai= ai; cur_ai != NULL; cur_ai= cur_ai->ai_next)
    {
      char ip_addr[INET6_ADDRSTRLEN];

      if (vio_getnameinfo(cur_ai->ai_addr, ip_addr, sizeof (ip_addr),
                          NULL, 0, NI_NUMERICHOST))
      {
        sql_print_error("Fails to print out IP-address.");
        continue;
      }

      sql_print_information("  - '%s' resolves to '%s';",
                            bind_address_str, ip_addr);
    }

    /*
      If the 'bind-address' option specifies the hostname, which resolves to
      multiple IP-address, use the following rule:
      - if there are IPv4-addresses, use the first IPv4-address
      returned by getaddrinfo();
      - if there are IPv6-addresses, use the first IPv6-address
      returned by getaddrinfo();
    */

    struct addrinfo *a;
    MYSQL_SOCKET listener_socket= create_socket(ai, AF_INET, &a);

    if (mysql_socket_getfd(listener_socket) == INVALID_SOCKET)
      listener_socket= create_socket(ai, AF_INET6, &a);

    // Report user-error if we failed to create a socket.
    if (mysql_socket_getfd(listener_socket) == INVALID_SOCKET)
    {
      sql_print_error("%s: %s", ER_DEFAULT(ER_IPSOCK_ERROR), strerror(errno));
      return MYSQL_INVALID_SOCKET;
    }

    mysql_socket_set_thread_owner(listener_socket);

#ifndef _WIN32
    /*
      We should not use SO_REUSEADDR on windows as this would enable a
      user to open two mysqld servers with the same TCP/IP port.
    */
    {
      int option_flag= 1;
      (void) mysql_socket_setsockopt(listener_socket, SOL_SOCKET, SO_REUSEADDR,
                                     (char*)&option_flag,sizeof(option_flag));
    }
#endif
#ifdef IPV6_V6ONLY
     /*
       For interoperability with older clients, IPv6 socket should
       listen on both IPv6 and IPv4 wildcard addresses.
       Turn off IPV6_V6ONLY option.

       NOTE: this will work starting from Windows Vista only.
       On Windows XP dual stack is not available, so it will not
       listen on the corresponding IPv4-address.
     */
    if (a->ai_family == AF_INET6)
    {
      int option_flag= 0;

      if (mysql_socket_setsockopt(listener_socket, IPPROTO_IPV6, IPV6_V6ONLY,
                                  (char *) &option_flag, sizeof (option_flag)))
      {
        sql_print_warning("Failed to reset IPV6_V6ONLY flag (error: %d). "
                          "The server will listen to IPv6 addresses only.",
                          (int) socket_errno);
      }
    }
#endif
    /*
      Sometimes the port is not released fast enough when stopping and
      restarting the server. This happens quite often with the test suite
      on busy Linux systems. Retry to bind the address at these intervals:
      Sleep intervals: 1, 2, 4,  6,  9, 13, 17, 22, ...
      Retry at second: 1, 3, 7, 13, 22, 35, 52, 74, ...
      Limit the sequence by m_port_timeout (set --port-open-timeout=#).
    */
    uint this_wait= 0;
    int ret= 0;
    for (uint waited= 0, retry= 1; ; retry++, waited+= this_wait)
    {
      if (((ret= mysql_socket_bind(listener_socket, a->ai_addr, a->ai_addrlen)) >= 0 ) ||
          (socket_errno != SOCKET_EADDRINUSE) ||
          (waited >= m_port_timeout))
        break;
      sql_print_information("Retrying bind on TCP/IP port %u", mysqld_port);
      this_wait= retry * retry / 3 + 1;
      sleep(this_wait);
    }
    freeaddrinfo(ai);
    if (ret < 0)
    {
      DBUG_PRINT("error",("Got error: %d from bind",socket_errno));
      sql_print_error("Can't start server: Bind on TCP/IP port: %s",
                      strerror(errno));
      sql_print_error("Do you already have another mysqld server running on port: %d ?",m_tcp_port);
      mysql_socket_close(listener_socket);
      return MYSQL_INVALID_SOCKET;
    }

    if (mysql_socket_listen(listener_socket, static_cast<int>(m_backlog)) < 0)
    {
      sql_print_error("Can't start server: listen() on TCP/IP port: %s",
                      strerror(errno));
      sql_print_error("listen() on TCP/IP failed with error %d",
          socket_errno);
      mysql_socket_close(listener_socket);
      return MYSQL_INVALID_SOCKET;
    }

#if !defined(NO_FCNTL_NONBLOCK)
    (void) mysql_sock_set_nonblocking(listener_socket);
#endif

    return listener_socket;
  }
};


#if defined(HAVE_SYS_UN_H)
///////////////////////////////////////////////////////////////////////////
// Unix_socket implementation
///////////////////////////////////////////////////////////////////////////

/**
  The Unix_socket class represents an abstraction for creating a unix
  socket ready to listen for new connections from clients.
*/
class Unix_socket
{
  std::string m_unix_sockname; // pathname for socket to bind to.
  uint m_backlog; // backlog specifying lenght of pending queue connection.

public:
  /**
    Constructor that takes pathname for unix socket to bind to
    and backlog specifying the length of pending connection queue.

    @param  unix_sockname pointer to pathname for the created unix socket
            to bind.
    @param  backlog   specifying the length of pending connection queue.
  */
  Unix_socket(const std::string *unix_sockname, uint backlog)
  : m_unix_sockname(*unix_sockname),
    m_backlog(backlog)
  { }

  /**
    Set up a listener socket which is ready to listen for connection from
    clients.

    @retval   valid socket if successful else MYSQL_INVALID_SOCKET on failure.
  */
  MYSQL_SOCKET get_listener_socket()
  {
    struct sockaddr_un UNIXaddr;
    DBUG_PRINT("general",("UNIX Socket is %s", m_unix_sockname.c_str()));

    // Check path length, probably move to set unix port?
    if (m_unix_sockname.length() > (sizeof(UNIXaddr.sun_path) - 1))
    {
      sql_print_error("The socket file path is too long (> %u): %s",
                      (uint) sizeof(UNIXaddr.sun_path) - 1,
                      m_unix_sockname.c_str());
      return MYSQL_INVALID_SOCKET;
    }

    // where to bring ps variables, socket creation
    MYSQL_SOCKET listener_socket= mysql_socket_socket(key_socket_unix, AF_UNIX,
                                                      SOCK_STREAM, 0);

    if (mysql_socket_getfd(listener_socket) < 0)
    {
      sql_print_error("Can't start server: UNIX Socket : %s", strerror(errno));
      return MYSQL_INVALID_SOCKET;
    }

    mysql_socket_set_thread_owner(listener_socket);

    memset(&UNIXaddr, 0, sizeof(UNIXaddr));
    UNIXaddr.sun_family= AF_UNIX;
    my_stpcpy(UNIXaddr.sun_path, m_unix_sockname.c_str());
    (void) unlink(m_unix_sockname.c_str());

    // Set socket option SO_REUSEADDR
    int option_enable= 1;
    (void) mysql_socket_setsockopt(listener_socket, SOL_SOCKET, SO_REUSEADDR,
                                   (char*)&option_enable, sizeof(option_enable));
    // bind
    umask(0);
    if (mysql_socket_bind(listener_socket,
                          reinterpret_cast<struct sockaddr *> (&UNIXaddr),
                          sizeof(UNIXaddr)) < 0)
    {
      sql_print_error("Can't start server : Bind on unix socket: %s",
                      strerror(errno));
      sql_print_error("Do you already have another mysqld server running on socket: %s ?",
                      m_unix_sockname.c_str());
      mysql_socket_close(listener_socket);
      return MYSQL_INVALID_SOCKET;
    }
    umask(((~my_umask) & 0666));

    // listen
    if (mysql_socket_listen(listener_socket, (int)m_backlog) < 0)
      sql_print_warning("listen() on Unix socket failed with error %d", socket_errno);

    // set sock fd non blocking? to decide accept etc.
#if !defined(NO_FCNTL_NONBLOCK)
    (void) mysql_sock_set_nonblocking(listener_socket);
#endif

    return listener_socket;
  }
};
#endif // HAVE_SYS_UN_H


///////////////////////////////////////////////////////////////////////////
// Mysqld_socket_listener implementation
///////////////////////////////////////////////////////////////////////////

bool Mysqld_socket_listener::setup_listener()
{
  // Setup tcp socket listener
  if (m_tcp_port)
  {
    TCP_socket tcp_socket(m_bind_addr_str, m_tcp_port,
                          m_backlog, m_port_timeout);

    MYSQL_SOCKET mysql_socket= tcp_socket.get_listener_socket();
    if (mysql_socket.fd == INVALID_SOCKET)
      return true;

    m_socket_map.insert(std::pair<MYSQL_SOCKET,bool>(mysql_socket, false));
  }
#if defined(HAVE_SYS_UN_H)
  // Setup unix socket listener
  if (m_unix_sockname != "")
  {
    Unix_socket unix_socket(&m_unix_sockname, m_backlog);

    MYSQL_SOCKET mysql_socket= unix_socket.get_listener_socket();
    if (mysql_socket.fd == INVALID_SOCKET)
      return true;

    m_socket_map.insert(std::pair<MYSQL_SOCKET,bool>(mysql_socket, true));
  }
#endif /* HAVE_SYS_UN_H */

  // Setup for connection events for poll or select
#ifdef HAVE_POLL
  int count= 0;
#endif
  for (socket_map_iterator_t sock_map_iter=  m_socket_map.begin();
       sock_map_iter != m_socket_map.end(); ++sock_map_iter)
  {
    MYSQL_SOCKET listen_socket= sock_map_iter->first;
    mysql_socket_set_thread_owner(listen_socket);
#ifdef HAVE_POLL
    m_poll_info.m_fds[count].fd= mysql_socket_getfd(listen_socket);
    m_poll_info.m_fds[count].events= POLLIN;
    m_poll_info.m_pfs_fds[count]= listen_socket;
    count++;
#else  // HAVE_POLL
    FD_SET(mysql_socket_getfd(listen_socket), &m_select_info.m_client_fds);
    if ((uint) mysql_socket_getfd(listen_socket) > m_select_info.m_max_used_connection)
      m_select_info.m_max_used_connection= mysql_socket_getfd(listen_socket);
#endif // HAVE_POLL
  }
  return false;
}


Channel_info* Mysqld_socket_listener::listen_for_connection_event()
{
#ifdef HAVE_POLL
  int retval= poll(&m_poll_info.m_fds[0], m_socket_map.size(), -1);
#else
  m_select_info.m_read_fds= m_select_info.m_client_fds;
  int retval= select((int) m_select_info.m_max_used_connection,
                     &m_select_info.m_read_fds, 0, 0, 0);
#endif

  if (retval < 0 && socket_errno != SOCKET_EINTR)
  {
    /*
      select(2)/poll(2) failed on the listening port.
      There is not much details to report about the client,
      increment the server global status variable.
    */
    connection_errors_select++;
    if (!select_errors++ && !abort_loop)
      sql_print_error("mysqld: Got error %d from select",socket_errno);
  }

  if (retval < 0 || abort_loop)
    return NULL;


  /* Is this a new connection request ? */
  MYSQL_SOCKET listen_sock= MYSQL_INVALID_SOCKET;
#ifdef HAVE_POLL
  for (uint i= 0; i < m_socket_map.size(); ++i)
  {
    if (m_poll_info.m_fds[i].revents & POLLIN)
    {
      listen_sock= m_poll_info.m_pfs_fds[i];
      break;
    }
  }
#else // HAVE_POLL
  for (socket_map_iterator_t sock_map_iter=  m_socket_map.begin();
       sock_map_iter != m_socket_map.end(); ++sock_map_iter)
  {
    if (FD_ISSET(mysql_socket_getfd(sock_map_iter->first),
                 &m_select_info.m_read_fds))
    {
      listen_sock= sock_map_iter->first;
      break;
    }
  }
#endif // HAVE_POLL

  MYSQL_SOCKET connect_sock;
  struct sockaddr_storage cAddr;
  for (uint retry= 0; retry < MAX_ACCEPT_RETRY; retry++)
  {
    size_socket length= sizeof(struct sockaddr_storage);
    connect_sock= mysql_socket_accept(key_socket_client_connection, listen_sock,
                                      (struct sockaddr *)(&cAddr), &length);
    if (mysql_socket_getfd(connect_sock) != INVALID_SOCKET ||
        (socket_errno != SOCKET_EINTR && socket_errno != SOCKET_EAGAIN))
      break;
  }
  if (mysql_socket_getfd(connect_sock) == INVALID_SOCKET)
  {
    /*
      accept(2) failed on the listening port, after many retries.
      There is not much details to report about the client,
      increment the server global status variable.
    */
    connection_errors_accept++;
    if ((m_error_count++ & 255) == 0) // This can happen often
      sql_print_error("Error in accept: %s", strerror(errno));
    if (socket_errno == SOCKET_ENFILE || socket_errno == SOCKET_EMFILE)
      sleep(1);             // Give other threads some time
    return NULL;
  }

#ifdef HAVE_LIBWRAP
  if (mysql_socket_getfd(listen_sock) == mysql_socket_getfd(m_tcp_socket))
  {
    struct request_info req;
    signal(SIGCHLD, SIG_DFL);
    request_init(&req, RQ_DAEMON, libwrapName, RQ_FILE,
                 mysql_socket_getfd(connect_sock), NULL);
    my_fromhost(&req);

    if (!my_hosts_access(&req))
    {
      /*
        This may be stupid but refuse() includes an exit(0)
        which we surely don't want...
        clean_exit() - same stupid thing ...
      */
      syslog(deny_severity, "refused connect from %s", my_eval_client(&req));

      /*
        C++ sucks (the gibberish in front just translates the supplied
        sink function pointer in the req structure from a void (*sink)();
        to a void(*sink)(int) if you omit the cast, the C++ compiler
        will cry...
      */
      if (req.sink)
        ((void (*)(int))req.sink)(req.fd);

      mysql_socket_shutdown(listen_sock, SHUT_RDWR);
      mysql_socket_close(listen_sock);
      /*
        The connection was refused by TCP wrappers.
        There are no details (by client IP) available to update the host_cache.
      */
      connection_errors_tcpwrap++;
      return NULL;
    }
  }
#endif // HAVE_LIBWRAP

  Channel_info* channel_info= NULL;
  if (m_socket_map[listen_sock])
    channel_info= new (std::nothrow) Channel_info_local_socket(listen_sock,
                                                               connect_sock);
  else
    channel_info= new (std::nothrow) Channel_info_tcpip_socket(listen_sock,
                                                               connect_sock);
  if (channel_info == NULL)
  {
    (void) mysql_socket_shutdown(connect_sock, SHUT_RDWR);
    (void) mysql_socket_close(connect_sock);
    connection_errors_internal++;
    return NULL;
  }

  return channel_info;
}


void Mysqld_socket_listener::close_listener()
{
  for (socket_map_iterator_t sock_map_iter=  m_socket_map.begin();
       sock_map_iter != m_socket_map.end(); ++sock_map_iter)
  {
    (void) mysql_socket_shutdown(sock_map_iter->first,SHUT_RDWR);
    (void) mysql_socket_close(sock_map_iter->first);
  }

#if defined(HAVE_SYS_UN_H)
  if (m_unix_sockname != "")
    (void) unlink(m_unix_sockname.c_str());
#endif

  if (!m_socket_map.empty())
    m_socket_map.clear();
}
