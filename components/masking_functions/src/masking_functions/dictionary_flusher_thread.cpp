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
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <my_dbug.h>

#include <mysql/components/services/log_builtins.h>

#include <sql/debug_sync.h>
#include <sql/sql_class.h>

#include "masking_functions/query_cache.hpp"

extern SERVICE_TYPE(log_builtins) * log_bi;
extern SERVICE_TYPE(log_builtins_string) * log_bs;

namespace {

// an auxilary RAII class that is intended to be used inside 'jthread' handler
// function: it calls 'my_thread_init()', sets up 'THD' object / 'PSI' info in
// constructor and calls 'my_thread_end()' in destructor
//
// the instance of this class must be declared as the very first element of
// the jthread's handler function so that 'thread_stack' member of the 'THD'
// object would be initialized properly
class thread_handler_context {
 public:
  thread_handler_context() {
    if (my_thread_init()) {
      throw std::runtime_error{"Cannot initialize my_thread"};
    }
    thd_.set_new_thread_id();
    thd_.thread_stack = reinterpret_cast<char *>(this);
    thd_.store_globals();
  }

  thread_handler_context(const thread_handler_context &) = delete;
  thread_handler_context &operator=(const thread_handler_context &) = delete;
  thread_handler_context(thread_handler_context &&) = delete;
  thread_handler_context &operator=(thread_handler_context &&) = delete;

  ~thread_handler_context() { my_thread_end(); }

  void actualize_psi(std::string_view description) noexcept {
    struct PSI_thread *psi = thd_.get_psi();
    PSI_THREAD_CALL(set_thread_id)(psi, thd_.thread_id());
    PSI_THREAD_CALL(set_thread_THD)(psi, &thd_);
    PSI_THREAD_CALL(set_thread_command)(thd_.get_command());
    PSI_THREAD_CALL(set_thread_info)(description.data(), description.size());
  }

 private:
  THD thd_;
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
          const char *name, const char *os_name, const char *description)
      : handler_function_{std::move(handler_function)},
        psi_thread_key_{PSI_NOT_INSTRUMENTED},
        psi_thread_info_{&psi_thread_key_,   name, os_name,
                         PSI_FLAG_SINGLETON, 0,    PSI_DOCUMENT_ME},
        description_{description},
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
  std::string_view description_;

  my_thread_handle handle_;

  static void *raw_handler(void *arg) {
    try {
      // the instance of this class needs to be declared as the very first
      // object within the thread handler function, so that its address ("this"
      // inside class's constructor) would represent the beginning of the thread
      // stack
      thread_handler_context handler_ctx{};
      const auto *self = static_cast<jthread *>(arg);
      handler_ctx.actualize_psi(self->description_);
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
constexpr char flusher_psi_description[]{
    "Masking functions component cache flusher"};

}  // anonymous namespace

namespace masking_functions {

void dictionary_flusher_thread::jthread_deleter::operator()(
    void *ptr) const noexcept {
  std::default_delete<jthread>{}(static_cast<jthread *>(ptr));
}

dictionary_flusher_thread::dictionary_flusher_thread(
    const query_cache_ptr &cache, std::uint64_t flush_interval_seconds)
    : cache_{cache},
      flush_interval_seconds_{flush_interval_seconds},
      stopped_{false},
      flusher_mutex_{},
      flusher_condition_var_{},
      thread_impl_{new jthread(
          [this]() { do_periodic_reload(); }, flusher_psi_category_name,
          flusher_psi_thread_info_name, flusher_psi_thread_info_os_name,
          flusher_psi_description)} {
  // we do not try to populate dict_cache here immediately as this constructor
  // is called from the component initialization method and any call to
  // mysql_command_query service may mess up with current THD

  // the cache will be loaded during the first call to one of the dictionary
  // functions or by the flusher thread
}

dictionary_flusher_thread::~dictionary_flusher_thread() {
  {
    std::unique_lock flusher_mutex_lock{flusher_mutex_};
    stopped_ = true;
  }
  flusher_condition_var_.notify_one();
  // thread_impl_'s destructor will join the thread
}

void dictionary_flusher_thread::do_periodic_reload() {
  const auto flush_interval_duration{
      std::chrono::seconds{flush_interval_seconds_}};
  std::unique_lock lock{flusher_mutex_};
  while (!stopped_) {
    flusher_condition_var_.wait_for(
        lock, flush_interval_duration,
        [&stopped = stopped_, expires_at = std::chrono::steady_clock::now() +
                                           flush_interval_duration]() {
          return stopped || std::chrono::steady_clock::now() > expires_at;
        });

    if (!stopped_) {
      // 'clear_error()' and 'reset_condition_info()' are needed here because
      // cache reload may generate an sql error (for instance, when dictionary
      // table simply does not exist) and 'debug_sync_set_action()' expects
      // THD to be in clear state
      DBUG_EXECUTE_IF("masking_functions_cache_reload", {
        const char act[] =
            "now WAIT_FOR masking_functions_cache_reload_before_signal";
        assert(!debug_sync_set_action(current_thd, STRING_WITH_LEN(act)));
      });

      try {
        cache_->reload_cache();
      } catch (const std::exception &e) {
        std::string message{"Exception during reloading dictionary cache - "};
        message += e.what();
        LogComponentErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, message.c_str());
      }
      // DEBUG_SYNC logic expects THD object to be in clear (error-wise) state -
      // that is wy we need to clear all potential errors generated by
      // 'reload_cache()'
      current_thd->clear_error();
      current_thd->get_stmt_da()->reset_condition_info(current_thd);

      DBUG_EXECUTE_IF("masking_functions_cache_reload", {
        const char act[] =
            "now SIGNAL masking_functions_cache_reload_after_signal";
        assert(!debug_sync_set_action(current_thd, STRING_WITH_LEN(act)));

        static std::size_t static_pass_counter{
            DBUG_EVALUATE_IF("masking_functions_flusher_double_pass", 2U, 1U)};
        --static_pass_counter;
        if (static_pass_counter == 0) {
          DBUG_SET("-d,masking_functions_cache_reload");
        }
      });
    }
  }
}

}  // namespace masking_functions
