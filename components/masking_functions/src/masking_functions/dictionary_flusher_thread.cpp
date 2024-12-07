/* Copyright (c) 2024 Percona LLC and/or its affiliates. All rights reserved.

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

#include "masking_functions/dictionary_flusher_thread.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <my_dbug.h>
#include <my_sys.h>
#include <mysqld_error.h>

#include <mysql/service_srv_session.h>

#include <mysql/components/services/log_builtins.h>
#include <mysql/components/services/mysql_current_thread_reader.h>

#include <mysql/psi/mysql_thread.h>

#include "masking_functions/basic_sql_context_builder.hpp"
#include "masking_functions/command_service_tuple.hpp"
#include "masking_functions/primitive_singleton.hpp"
#include "masking_functions/query_cache.hpp"
#ifndef NDEBUG
#include "masking_functions/sql_context.hpp"
#endif
#include "masking_functions/static_sql_context_builder.hpp"

#include "sql/debug_sync.h"

extern SERVICE_TYPE(log_builtins) * log_bi;
extern SERVICE_TYPE(log_builtins_string) * log_bs;

extern REQUIRES_SERVICE_PLACEHOLDER(mysql_current_thread_reader);

namespace {

using global_command_services = masking_functions::primitive_singleton<
    masking_functions::command_service_tuple>;

// an auxilary RAII class that is intended to be used inside 'jthread' handler
// function: it calls 'init()' method from the 'mysql_command_thread' service
// in constructor and 'end()' method from the 'mysql_command_thread' service
// in destructor
class thread_handler_context {
 public:
  thread_handler_context() {
    if ((*global_command_services::instance().thread->init)() != 0) {
      throw std::runtime_error{"Cannot initialize thread handler context"};
    }
  }

  thread_handler_context(const thread_handler_context &) = delete;
  thread_handler_context &operator=(const thread_handler_context &) = delete;
  thread_handler_context(thread_handler_context &&) = delete;
  thread_handler_context &operator=(thread_handler_context &&) = delete;

  ~thread_handler_context() {
    (*global_command_services::instance().thread->end)();
  }
};

// an auxiliary RAII class that wraps an instance of 'my_thread_attr_t': it
// calls 'my_thread_attr_init()' in constructor and 'my_thread_attr_destroy()'
// in destructor
class thread_attributes {
 public:
  thread_attributes() : attributes_{} {
    if (my_thread_attr_init(&attributes_) != 0) {
      throw std::runtime_error{"Cannot initialize thread attributes"};
    }
  }

  thread_attributes(const thread_attributes &) = delete;
  thread_attributes &operator=(const thread_attributes &) = delete;
  thread_attributes(thread_attributes &&) = delete;
  thread_attributes &operator=(thread_attributes &&) = delete;

  ~thread_attributes() { my_thread_attr_destroy(&attributes_); }

  void make_joinable() {
    if (my_thread_attr_setdetachstate(&attributes_,
                                      MY_THREAD_CREATE_JOINABLE) != 0) {
      throw std::runtime_error{
          "Cannot set joinable state for thread attributes"};
    }
  }
  const my_thread_attr_t &get_underlying() const noexcept {
    return attributes_;
  }

 private:
  my_thread_attr_t attributes_;
};

// an auxilary RAII class that spawns a joinable thread in the constructor and
// joins it in destructor
class jthread {
 public:
  using handler_function_type = std::function<void()>;

  // passing 'handler_function' deliberately by value to move from
  jthread(handler_function_type handler_function, const char *category_name,
          const char *name, const char *os_name)
      : handler_function_{std::move(handler_function)},
        psi_thread_key_{PSI_NOT_INSTRUMENTED},
        psi_thread_info_{&psi_thread_key_,   name, os_name,
                         PSI_FLAG_SINGLETON, 0,    PSI_DOCUMENT_ME},
        handle_{} {
    thread_attributes attributes{};
    attributes.make_joinable();

    mysql_thread_register(category_name, &psi_thread_info_, 1U);

    if (mysql_thread_create(psi_thread_key_, &handle_,
                            &attributes.get_underlying(), raw_handler,
                            this) != 0) {
      throw std::runtime_error{"Cannot create jthread"};
    }
  }

  jthread(const jthread &) = delete;
  jthread &operator=(const jthread &) = delete;
  jthread(jthread &&) = delete;
  jthread &operator=(jthread &&) = delete;
  ~jthread() { my_thread_join(&handle_, nullptr); }

 private:
  handler_function_type handler_function_;

  PSI_thread_key psi_thread_key_;
  PSI_thread_info psi_thread_info_;

  my_thread_handle handle_;

  static void *raw_handler(void *arg) {
    try {
      thread_handler_context handler_ctx{};
      const auto *self = static_cast<jthread *>(arg);
      (self->handler_function_)();
    } catch (const std::exception &e) {
      std::string message{"Exception caught in jthread handler - "};
      message += e.what();
      LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, message.c_str());
    } catch (...) {
      LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                      "Unexpected exception caught in jthread handler");
    }
    return nullptr;
  }
};

constexpr char flusher_psi_category_name[]{"masking_functions"};
constexpr char flusher_psi_thread_info_name[]{"masking_functions_dict_flusher"};
constexpr char flusher_psi_thread_info_os_name[]{"mf_flusher"};

}  // anonymous namespace

namespace masking_functions {

enum class dictionary_flusher_thread::state_type {
  initial,
  initialization_failure,
  operational,
  stopped
};

void dictionary_flusher_thread::jthread_deleter::operator()(
    void *ptr) const noexcept {
  std::default_delete<jthread>{}(static_cast<jthread *>(ptr));
}

dictionary_flusher_thread::dictionary_flusher_thread(
    const query_cache_core_ptr &cache_core,
    const query_builder_ptr &sql_query_builder,
    std::uint64_t flush_interval_seconds)
    : cache_core_{cache_core},
      sql_query_builder_{sql_query_builder},
      flush_interval_seconds_{flush_interval_seconds},
      state_{state_type::initial},
      thread_impl_{new jthread{
          [this]() { do_periodic_reload(); }, flusher_psi_category_name,
          flusher_psi_thread_info_name, flusher_psi_thread_info_os_name}} {
  // we do not try to populate dict_cache here immediately as this constructor
  // is called from the component initialization method and any call to
  // mysql_command_query service may mess up with current THD

  // the cache will be loaded during the first call to one of the dictionary
  // functions or by the flusher thread
}

dictionary_flusher_thread::~dictionary_flusher_thread() {
  request_termination();
  // thread_impl_'s destructor will join the thread
}

bool dictionary_flusher_thread::request_termination() {
  return state_.exchange(state_type::stopped) != state_type::initial;
}

void dictionary_flusher_thread::do_periodic_reload() {
  // waiting for Session Server availability
  static constexpr auto sleep_interval{std::chrono::seconds(1)};
  static constexpr std::size_t max_number_of_attempts{30};
  std::size_t number_of_attempts{0U};

  while (number_of_attempts < max_number_of_attempts &&
         state_ != state_type::stopped && !srv_session_server_is_available()) {
    std::this_thread::sleep_for(sleep_interval);
    ++number_of_attempts;
  }

  if (state_ == state_type::stopped) {
    LogComponentErr(
        WARNING_LEVEL, ER_LOG_PRINTF_MSG,
        "Flusher thread terminated while waiting for session server");
    return;
  }
  if (number_of_attempts >= max_number_of_attempts) {
    LogComponentErr(
        ERROR_LEVEL, ER_LOG_PRINTF_MSG,
        "Session server is unavailable during flusher thread initialization");
    state_ = state_type::initialization_failure;
    return;
  }

  // initializing internal connection (along with THD)
  query_cache_ptr cache;

  using optional_string_type = std::optional<std::string>;
  optional_string_type failure_message;
  try {
    // it is important that 'static_sql_context_builder' is created here
    // (inside the background thread) because the "static" instance of the
    // 'sql_context', which will be created inside 'static_sql_context_builder'
    // constructor, will set the 'MYSQL_COMMAND_LOCAL_THD_HANDLE' option to
    // 'nullptr' meaning that internal THD object must be created and associated
    // with the current thread
    basic_sql_context_builder_ptr sql_ctx_builder{
        std::make_shared<static_sql_context_builder>(
            global_command_services::instance(),
            sql_context_registry_access::non_locking)};
    cache = std::make_shared<query_cache>(cache_core_, sql_ctx_builder,
                                          sql_query_builder_);

    DBUG_EXECUTE_IF("enable_masking_functions_flush_thread_sync", {
      const auto sql_ctx{sql_ctx_builder->build()};
      std::string wait_action{
          "SET debug_sync = 'masking_functions_before_cache_reload WAIT_FOR "
          "masking_functions_before_cache_reload_signal"};
      std::string signal_action{
          "SET debug_sync = 'masking_functions_after_cache_reload SIGNAL "
          "masking_functions_after_cache_reload_signal"};
      DBUG_EXECUTE_IF("enable_masking_functions_flush_thread_double_pass", {
        wait_action += " EXECUTE 2";
        signal_action += " EXECUTE 2";
      });
      wait_action += '\'';
      signal_action += '\'';

      sql_ctx->execute_dml(wait_action);
      sql_ctx->execute_dml(signal_action);
    });
  } catch (const std::exception &e) {
    failure_message.emplace(
        "Exception during flusher thread initialization - ");
    failure_message->append(e.what());
  } catch (...) {
    failure_message.emplace(
        "Unknown exception during flusher thread initialization");
  }

  MYSQL_THD extracted_thd{nullptr};
  mysql_service_mysql_current_thread_reader->get(&extracted_thd);
  assert(extracted_thd != nullptr);

  const auto is_terminated_lambda{[thd = extracted_thd, this]() {
    if ((*is_killed_hook)(thd) != 0) {
      state_ = state_type::stopped;
    }
    return state_ == state_type::stopped;
  }};

  if (is_terminated_lambda()) {
    LogComponentErr(
        WARNING_LEVEL, ER_LOG_PRINTF_MSG,
        "Flusher thread terminated after creating internal connection");
    return;
  }
  if (failure_message.has_value()) {
    LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, failure_message->c_str());
    state_ = state_type::initialization_failure;
    return;
  }
  state_ = state_type::operational;

  const auto flush_interval_duration{
      std::chrono::seconds{flush_interval_seconds_}};

  auto expires_at{std::chrono::steady_clock::now()};
  while (!is_terminated_lambda()) {
    if (std::chrono::steady_clock::now() >= expires_at) {
      DBUG_EXECUTE_IF("enable_masking_functions_flush_thread_sync", {
        DEBUG_SYNC(extracted_thd, "masking_functions_before_cache_reload");
      });

      failure_message.reset();
      try {
        cache->reload_cache();
      } catch (const std::exception &e) {
        failure_message.emplace(
            "Exception during reloading dictionary cache - ");
        failure_message->append(e.what());
      } catch (...) {
        failure_message.emplace(
            "Unknown exception during reloading dictionary cache");
      }
      if (failure_message.has_value()) {
        LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                        failure_message->c_str());
      }

      DBUG_EXECUTE_IF("enable_masking_functions_flush_thread_sync", {
        DEBUG_SYNC(extracted_thd, "masking_functions_after_cache_reload");
      });

      expires_at = std::chrono::steady_clock::now() + flush_interval_duration;
    } else {
      std::this_thread::sleep_for(sleep_interval);
    }
  }
}

}  // namespace masking_functions
