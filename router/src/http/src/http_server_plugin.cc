/*
  Copyright (c) 2018, Oracle and/or its affiliates. All rights reserved.

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

/**
 */

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <sys/types.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/bufferevent_ssl.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/listener.h>
#include <event2/util.h>

// Harness interface include files
#include "common.h"  // rename_thread()
#include "mysql/harness/config_parser.h"
#include "mysql/harness/loader.h"
#include "mysql/harness/logging/logging.h"
#include "mysql/harness/plugin.h"

#include "http_server_plugin.h"
#include "mysqlrouter/http_server_component.h"
#include "mysqlrouter/plugin_config.h"
#include "posix_re.h"
#include "tls_server_context.h"

IMPORT_LOG_FUNCTIONS()

static constexpr const char kSectionName[]{"http_server"};

using mysql_harness::ARCHITECTURE_DESCRIPTOR;
using mysql_harness::PLUGIN_ABI_VERSION;
using mysql_harness::Plugin;
using mysql_harness::PluginFuncEnv;

std::promise<void> stopper;
std::future<void> stopped = stopper.get_future();

/**
 * request router
 *
 * send requests for a path of the URI to a handler callback
 *
 * if no handler is found, reply with 404 not found
 */
void HttpRequestRouter::append(const std::string &url_regex_str,
                               std::unique_ptr<BaseRequestHandler> cb) {
  std::lock_guard<std::mutex> lock(route_mtx_);
  request_handlers_.emplace_back(
      RouterData{url_regex_str, PosixRE{url_regex_str}, std::move(cb)});
}

void HttpRequestRouter::remove(const std::string &url_regex_str) {
  std::lock_guard<std::mutex> lock(route_mtx_);
  for (auto it = request_handlers_.begin(); it != request_handlers_.end();) {
    if (it->url_regex_str == url_regex_str) {
      it = request_handlers_.erase(it);
    } else {
      it++;
    }
  }
}

// if no routes are specified, return 404
void HttpRequestRouter::route_default(HttpRequest &req) {
  if (default_route_) {
    default_route_->handle_request(req);
  } else {
    req.send_error(HttpStatusCode::NotFound, "Not Found");
  }
}

void HttpRequestRouter::set_default_route(
    std::unique_ptr<BaseRequestHandler> cb) {
  std::lock_guard<std::mutex> lock(route_mtx_);
  default_route_ = std::move(cb);
}

void HttpRequestRouter::clear_default_route() {
  std::lock_guard<std::mutex> lock(route_mtx_);
  default_route_ = nullptr;
}

void HttpRequestRouter::route(HttpRequest req) {
  std::lock_guard<std::mutex> lock(route_mtx_);

  auto uri = req.get_uri();

  for (auto &request_handler : request_handlers_) {
    if (request_handler.url_regex.search(uri)) {
      request_handler.handler->handle_request(req);
      return;
    }
  }

  route_default(req);
}

void stop_eventloop(evutil_socket_t, short, void *cb_arg) {
  auto *ev_base = static_cast<event_base *>(cb_arg);

  if (stopped.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    event_base_loopexit(ev_base, nullptr);
  }
}

void HttpRequestThread::accept_socket() {
  // we could replace the callback after accept here, but sadly
  // we don't have access to it easily
  evhttp_accept_socket_with_handle(ev_http.get(), accept_fd_);
}

void HttpRequestThread::set_request_router(HttpRequestRouter &router) {
  evhttp_set_gencb(ev_http.get(),
                   [](evhttp_request *req, void *user_data) {
                     auto *rtr = static_cast<HttpRequestRouter *>(user_data);
                     rtr->route(HttpRequest{
                         std::unique_ptr<evhttp_request,
                                         std::function<void(evhttp_request *)>>(
                             req, [](evhttp_request *) {})});
                   },
                   &router);
}

void HttpRequestThread::wait_and_dispatch() {
  struct timeval tv {
    0, 10 * 1000
  };
  event_add(ev_shutdown_timer.get(), &tv);
  event_base_dispatch(ev_base.get());
}

class HttpRequestMainThread : public HttpRequestThread {
 public:
  void bind(const std::string &address, uint16_t port) {
    auto *handle =
        evhttp_bind_socket_with_handle(ev_http.get(), address.c_str(), port);
    if (nullptr == handle) {
      throw std::runtime_error("binding socket failed ...");
    }
    accept_fd_ = evhttp_bound_socket_get_fd(handle);
  }
};

class HttpsRequestMainThread : public HttpRequestMainThread {
 public:
  HttpsRequestMainThread(SSL_CTX *ssl_ctx) {
    evhttp_set_bevcb(ev_http.get(),
                     [](struct event_base *base, void *arg) {
                       return bufferevent_openssl_socket_new(
                           base, -1, SSL_new(static_cast<SSL_CTX *>(arg)),
                           BUFFEREVENT_SSL_ACCEPTING, BEV_OPT_CLOSE_ON_FREE);
                     },
                     ssl_ctx);
  }
};

class HttpRequestWorkerThread : public HttpRequestThread {
 public:
  explicit HttpRequestWorkerThread(harness_socket_t accept_fd) {
    accept_fd_ = accept_fd;
  }
};

class HttpsRequestWorkerThread : public HttpRequestWorkerThread {
 public:
  explicit HttpsRequestWorkerThread(harness_socket_t accept_fd,
                                    SSL_CTX *ssl_ctx)
      : HttpRequestWorkerThread(accept_fd) {
    evhttp_set_bevcb(ev_http.get(),
                     [](struct event_base *base, void *arg) {
                       return bufferevent_openssl_socket_new(
                           base, -1, SSL_new(static_cast<SSL_CTX *>(arg)),
                           BUFFEREVENT_SSL_ACCEPTING, BEV_OPT_CLOSE_ON_FREE);
                     },
                     ssl_ctx);
  }
};

void HttpServer::join_all() {
  while (!sys_threads_.empty()) {
    auto &thr = sys_threads_.back();
    thr.join();
    sys_threads_.pop_back();
  }
}

void HttpServer::start(size_t max_threads) {
  {
    auto main_thread = HttpRequestMainThread();
    main_thread.bind(address_, port_);
    thread_contexts_.emplace_back(std::move(main_thread));
  }

  harness_socket_t accept_fd = thread_contexts_[0].get_socket_fd();
  for (size_t ndx = 1; ndx < max_threads; ndx++) {
    thread_contexts_.emplace_back(HttpRequestWorkerThread(accept_fd));
  }

  for (size_t ndx = 0; ndx < max_threads; ndx++) {
    auto &thr = thread_contexts_[ndx];

    sys_threads_.emplace_back([&]() {
      mysql_harness::rename_thread("HttpSrv Worker");

      thr.set_request_router(request_router_);
      thr.accept_socket();
      thr.wait_and_dispatch();
    });
  }
}

class HttpsServer : public HttpServer {
 public:
  HttpsServer(const std::string &address, uint16_t port,
              const std::string &cert_file, const std::string &key_file,
              const std::string &ciphers, const std::string &dh_params)
      : HttpServer(address.c_str(), port),
        ssl_ctx_{cert_file, key_file, ciphers, dh_params} {}
  void start(size_t max_threads) override;

 private:
  TlsServerContext ssl_ctx_;
};

void HttpsServer::start(size_t max_threads) {
  {
    auto main_thread = HttpsRequestMainThread(ssl_ctx_.get());
    main_thread.bind(address_, port_);
    thread_contexts_.emplace_back(std::move(main_thread));
  }

  harness_socket_t accept_fd = thread_contexts_[0].get_socket_fd();
  for (size_t ndx = 1; ndx < max_threads; ndx++) {
    thread_contexts_.emplace_back(
        HttpsRequestWorkerThread(accept_fd, ssl_ctx_.get()));
  }

  for (size_t ndx = 0; ndx < max_threads; ndx++) {
    auto &thr = thread_contexts_[ndx];

    sys_threads_.emplace_back([&]() {
      mysql_harness::rename_thread("HttpSrv Worker");

      thr.set_request_router(request_router_);
      thr.accept_socket();
      thr.wait_and_dispatch();
    });
  }
}

void HttpServer::add_route(const std::string &url_regex,
                           std::unique_ptr<BaseRequestHandler> cb) {
  log_debug("adding route for regex: %s", url_regex.c_str());
  if (url_regex.empty()) {
    request_router_.set_default_route(std::move(cb));
  } else {
    request_router_.append(url_regex, std::move(cb));
  }
}

void HttpServer::remove_route(const std::string &url_regex) {
  log_debug("removing route for regex: %s", url_regex.c_str());
  if (url_regex.empty()) {
    request_router_.clear_default_route();
  } else {
    request_router_.remove(url_regex);
  }
}

class PluginConfig : public mysqlrouter::BasePluginConfig {
 public:
  std::string static_basedir;
  std::string srv_address;
  std::string ssl_cert;
  std::string ssl_key;
  std::string ssl_cipher;
  std::string ssl_dh_params;
  bool with_ssl;
  uint16_t srv_port;

  explicit PluginConfig(const mysql_harness::ConfigSection *section)
      : mysqlrouter::BasePluginConfig(section),
        static_basedir(get_option_string(section, "static_folder")),
        srv_address(get_option_string(section, "bind_address")),
        ssl_cert(get_option_string(section, "ssl_cert")),
        ssl_key(get_option_string(section, "ssl_key")),
        ssl_cipher(get_option_string(section, "ssl_cipher")),
        ssl_dh_params(get_option_string(section, "ssl_dh_param")),
        with_ssl(get_uint_option<bool>(section, "ssl")),
        srv_port(get_uint_option<uint16_t>(section, "port")) {}

  std::string get_default_ciphers() const {
    std::string out;
    for (const auto &s : Tls::get_default_ciphers()) {
      if (!out.empty()) out += ":";

      out += s;
    }

    return out;
  }

  std::string get_default(const std::string &option) const override {
    const std::map<std::string, std::string> defaults{
        {"bind_address", "0.0.0.0"},
        {"port", "5555"},
        {"ssl", "0"},
        {"ssl_cipher", get_default_ciphers()},
    };

    auto it = defaults.find(option);
    if (it == defaults.end()) {
      return std::string();
    }
    return it->second;
  }

  bool is_required(const std::string & /* option */) const override {
    return false;
  }
};
static std::map<std::string, std::shared_ptr<HttpServer>> http_servers;

class HttpServerFactory {
 public:
  static std::shared_ptr<HttpServer> create(const PluginConfig &config) {
    if (config.with_ssl) {
      return std::make_shared<HttpsServer>(
          config.srv_address, config.srv_port, config.ssl_cert, config.ssl_key,
          config.ssl_cipher, config.ssl_dh_params);
    } else {
      return std::make_shared<HttpServer>(config.srv_address.c_str(),
                                          config.srv_port);
    }
  }
};

static void init(PluginFuncEnv *env) {
  const mysql_harness::AppInfo *info = get_app_info(env);
  bool has_started = false;

  if (nullptr == info->config) {
    return;
  }

  // assume there is only one section for us
  //
  try {
    for (const mysql_harness::ConfigSection *section :
         info->config->sections()) {
      if (section->name != kSectionName) {
        continue;
      }

      if (has_started) {
        // ignore all the other sections for now
        continue;
      }

      has_started = true;

      PluginConfig config{section};

      log_info("listening on %s:%u", config.srv_address.c_str(),
               config.srv_port);

      http_servers.emplace(
          std::make_pair(section->name, HttpServerFactory::create(config)));

      auto srv = http_servers.at(section->name);
      HttpServerComponent::get_instance().init(srv);

      if (!config.static_basedir.empty()) {
        srv->add_route("",
                       std::unique_ptr<HttpStaticFolderHandler>(
                           new HttpStaticFolderHandler(config.static_basedir)));
      }
    }
  } catch (const std::invalid_argument &exc) {
    set_error(env, mysql_harness::kConfigInvalidArgument, "%s", exc.what());
  } catch (const std::exception &exc) {
    set_error(env, mysql_harness::kRuntimeError, "%s", exc.what());
  } catch (...) {
    set_error(env, mysql_harness::kUndefinedError, "Unexpected exception");
  }
}

static void start(PluginFuncEnv *env) {
  // - version string
  // - hostname
  // - active-connections vs. max-connections
  // - listing port statistics
  //   - ip addresses
  // - backend status
  //   - throughput
  //   - response latency (min time first packet)
  //   - replication lag
  // - backend health
  // - configuration
  //   - ports
  //   - log-level
  //   - metadata server ip
  // - cluster health
  //   - time since last check
  //   - log group member changes
  // - important log messages
  // - mismatch between group-membership and metadata

  mysql_harness::rename_thread("HttpSrv Main");

  try {
    auto srv = http_servers.at(get_config_section(env)->name);

    // add routes

    srv->start(8);

    // wait until we got asked to shutdown.
    //
    // 0 == wait-forever
    wait_for_stop(env, 0);
    stopper.set_value();

    srv->join_all();
  } catch (const std::invalid_argument &exc) {
    set_error(env, mysql_harness::kConfigInvalidArgument, "%s", exc.what());
  } catch (const std::exception &exc) {
    set_error(env, mysql_harness::kRuntimeError, "%s", exc.what());
  } catch (...) {
    set_error(env, mysql_harness::kUndefinedError, "Unexpected exception");
  }
}

extern "C" {
Plugin HTTP_SERVER_EXPORT harness_plugin_http_server = {
    PLUGIN_ABI_VERSION,
    ARCHITECTURE_DESCRIPTOR,
    "HTTP_SERVER",
    VERSION_NUMBER(0, 0, 1),
    0,
    nullptr,  // requires
    0,
    nullptr,  // conflicts
    init,     // init
    nullptr,  // deinit
    start,    // start
    nullptr,  // stop
};
}
