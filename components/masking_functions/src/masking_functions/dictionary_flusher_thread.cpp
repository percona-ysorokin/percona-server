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

#include <mysql/components/services/log_builtins.h>
#include <mysql/psi/mysql_thread.h>
#include <mysqld_error.h>
#include <sql/debug_sync.h>
#include <sql/sql_class.h>

#include "masking_functions/query_cache.hpp"

extern REQUIRES_SERVICE_PLACEHOLDER(log_builtins);

namespace {

constexpr const char psi_category_name[]{"masking_functions"};
constexpr const char flusher_thd_psi_name[]{"masking_functions_dict_flusher"};
constexpr const char flusher_thd_psi_os_name[]{"mf_flusher"};

}  // anonymous namespace

namespace masking_functions {

dictionary_flusher_thread::dictionary_flusher_thread(
    const query_cache_ptr &cache, std::uint64_t flusher_interval_seconds)
    : cache_(cache),
      flusher_interval_seconds_{flusher_interval_seconds},
      is_flusher_stopped_{true} {
  // we do not try to populate dict_cache here immediately as this constructor
  // is called from the component initialization method and any call to
  // mysql_command_query service may mess up with current THD

  // the cache will be loaded during the first call to one of the dictionary
  // functions or by the flusher thread
  if (flusher_interval_seconds_ > 0) {
    PSI_thread_info thread_info{&psi_flusher_thread_key_,
                                flusher_thd_psi_name,
                                flusher_thd_psi_os_name,
                                PSI_FLAG_SINGLETON,
                                0,
                                PSI_DOCUMENT_ME};
    mysql_thread_register(psi_category_name, &thread_info, 1);
    my_thread_attr_init(&flusher_thread_attr_);
    my_thread_attr_setdetachstate(&flusher_thread_attr_,
                                  MY_THREAD_CREATE_JOINABLE);

    const auto res =
        mysql_thread_create(psi_flusher_thread_key_, &flusher_thread_,
                            &flusher_thread_attr_, run_dict_flusher, this);

    if (res != 0) {
      LogComponentErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                      "Cannot initialize dictionary flusher");
    } else {
      is_flusher_stopped_ = false;
    }
    my_thread_attr_destroy(&flusher_thread_attr_);
  }
}

dictionary_flusher_thread::~dictionary_flusher_thread() {
  if (!is_flusher_stopped_) {
    is_flusher_stopped_ = true;
    flusher_condition_var_.notify_one();
    my_thread_join(&flusher_thread_, nullptr);
  }
}

void dictionary_flusher_thread::init_thd() noexcept {
  auto *thd = new THD;
  my_thread_init();
  thd->set_new_thread_id();
  thd->thread_stack = reinterpret_cast<char *>(&thd);
  thd->store_globals();
  flusher_thd_.reset(thd);
}

void dictionary_flusher_thread::release_thd() noexcept { my_thread_end(); }

void dictionary_flusher_thread::dict_flusher() noexcept {
#ifdef HAVE_PSI_THREAD_INTERFACE
  {
    struct PSI_thread *psi = flusher_thd_->get_psi();
    PSI_THREAD_CALL(set_thread_id)(psi, flusher_thd_->thread_id());
    PSI_THREAD_CALL(set_thread_THD)(psi, flusher_thd_.get());
    PSI_THREAD_CALL(set_thread_command)(flusher_thd_->get_command());
    PSI_THREAD_CALL(set_thread_info)
    (STRING_WITH_LEN("Masking functions component cache flusher"));
  }
#endif

  while (!is_flusher_stopped_) {
    std::unique_lock lock{flusher_mutex_};
    const auto wait_started_at = std::chrono::system_clock::now();
    flusher_condition_var_.wait_for(
        lock, std::chrono::seconds{flusher_interval_seconds_},
        [this, wait_started_at] {
          return std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now() - wait_started_at) >=
                     std::chrono::seconds{flusher_interval_seconds_} ||
                 is_flusher_stopped_.load();
        });

    if (!is_flusher_stopped_) {
      try {
        cache_->reload_cache();

      } catch (const std::exception &) {
      }

      DBUG_EXECUTE_IF("masking_functions_signal_on_cache_reload", {
        const char act[] = "now SIGNAL masking_functions_cache_reload_done";
        assert(!debug_sync_set_action(current_thd, STRING_WITH_LEN(act)));
      };);
    }
  }
}

void *dictionary_flusher_thread::run_dict_flusher(void *arg) {
  auto *self =
      reinterpret_cast<masking_functions::dictionary_flusher_thread *>(arg);
  self->init_thd();
  self->dict_flusher();
  self->release_thd();
  return nullptr;
}

}  // namespace masking_functions
