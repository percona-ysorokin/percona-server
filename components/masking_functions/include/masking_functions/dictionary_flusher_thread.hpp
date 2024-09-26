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

#ifndef MASKING_FUNCTIONS_DICTIONARY_FLUSHER_THREAD_HPP
#define MASKING_FUNCTIONS_DICTIONARY_FLUSHER_THREAD_HPP

#include "masking_functions/dictionary_flusher_thread_fwd.hpp"

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

#include "masking_functions/query_cache_fwd.hpp"

namespace masking_functions {

class dictionary_flusher_thread {
 public:
  dictionary_flusher_thread(const query_cache_ptr &cache,
                            std::uint64_t flush_interval_seconds);
  dictionary_flusher_thread(const dictionary_flusher_thread &other) = delete;
  dictionary_flusher_thread(dictionary_flusher_thread &&other) = delete;
  dictionary_flusher_thread &operator=(const dictionary_flusher_thread &other) =
      delete;
  dictionary_flusher_thread &operator=(dictionary_flusher_thread &&other) =
      delete;
  ~dictionary_flusher_thread();

 private:
  query_cache_ptr cache_;
  std::uint64_t flush_interval_seconds_;

  bool stopped_;
  std::mutex flusher_mutex_;
  std::condition_variable flusher_condition_var_;

  struct jthread_deleter {
    void operator()(void *ptr) const noexcept;
  };
  using jthread_ptr = std::unique_ptr<void, jthread_deleter>;
  jthread_ptr thread_impl_;

  void do_periodic_reload();
};

}  // namespace masking_functions

#endif  // MASKING_FUNCTIONS_DICTIONARY_FLUSHER_THREAD_HPP
