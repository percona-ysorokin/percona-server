/*
   Portions Copyright (c) 2015-Present, Facebook, Inc.
   Portions Copyright (c) 2012, Monty Program Ab

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */
#pragma once

/* C++ standard header files */
#include <atomic>
#include <deque>
#include <map>
#include <string>
#include <unordered_set>
#include <utility>

/* MySQL includes */
#include "include/mysql/psi/mysql_thread.h"
#include "mysql/psi/mysql_table.h"
#include "mysql/thread_pool_priv.h"
/* MySQL has an enum symbol ZSTD but rocksdb has a macro defined ZSTD, we need
   to tip-toe around this */
#if defined (ZSTD)
#define MYROCKS_TEMP_ZSTD ZSTD
#undef ZSTD
#endif  // defined(ZSTD)
#include "sql/sql_class.h"
#if defined(MYROCKS_TEMP_ZSTD)
#define ZSTD MYROCKS_TEMP_ZSTD
#undef MYROCKS_TEMP_ZSTD
#endif  // defined(MYROCKS_TEMP_ZSTD)

/* MyRocks header files */
#include "./rdb_utils.h"
#include "rocksdb/db.h"

namespace myrocks {

class Rdb_thread {
 private:
  // Disable Copying
  Rdb_thread(const Rdb_thread &);
  Rdb_thread &operator=(const Rdb_thread &);

  // Make sure we run only once
  std::atomic_bool m_run_once;

  my_thread_handle m_handle;

 protected:
  mysql_mutex_t m_signal_mutex;
  mysql_cond_t m_signal_cond;

  // TODO: When porting to 8.0 we should move to std::atomic
  // instead of volatile
  std::atomic<THD::killed_state> m_killed;

 public:
  Rdb_thread() : m_run_once(false), m_killed(THD::NOT_KILLED) {}

#ifdef HAVE_PSI_INTERFACE
  void init(my_core::PSI_mutex_key stop_bg_psi_mutex_key,
            my_core::PSI_cond_key stop_bg_psi_cond_key);
  int create_thread(const std::string &thread_name,
                    my_core::PSI_thread_key background_psi_thread_key);
#else
  void init();
  int create_thread(const std::string &thread_name);
#endif

  virtual void run(void) = 0;

  void signal(const bool stop_thread = false);

  int join() { return my_thread_join(&m_handle, nullptr); }

  void uninit();

  virtual ~Rdb_thread() {}

 private:
  static void *thread_func(void *const thread_ptr);
};

/**
  MyRocks background thread control
  N.B. This is on top of RocksDB's own background threads
       (@see rocksdb::CancelAllBackgroundWork())
*/

class Rdb_background_thread : public Rdb_thread {
 private:
  bool m_save_stats = false;

  void reset() {
    mysql_mutex_assert_owner(&m_signal_mutex);
    m_killed = THD::NOT_KILLED;
    m_save_stats = false;
  }

 public:
  virtual void run() override;

  void request_save_stats() {
    RDB_MUTEX_LOCK_CHECK(m_signal_mutex);

    m_save_stats = true;

    RDB_MUTEX_UNLOCK_CHECK(m_signal_mutex);
  }
};

class Rdb_index_stats_thread : public Rdb_thread {
 private:
  mysql_mutex_t m_is_mutex;
  std::deque<std::reference_wrapper<const std::string>> m_requests;
  std::unordered_set<std::string> m_tbl_names;

  bool m_tid_set;
  pid_t m_tid;

 public:
  Rdb_index_stats_thread() : m_tid_set(false), m_tid(0) {
    mysql_mutex_init(0, &m_is_mutex, MY_MUTEX_INIT_FAST);
  }

  virtual ~Rdb_index_stats_thread() override {
    mysql_mutex_destroy(&m_is_mutex);
  }

  virtual void run() override;
  bool get_index_stats_request(std::string *tbl_name);
  void add_index_stats_request(const std::string &tbl_name);
  void clear_all_index_stats_requests();
  size_t get_request_queue_size();
  int renice(int nice_val);
};

class Rdb_manual_compaction_thread : public Rdb_thread {
 private:
  struct Manual_compaction_request {
    int mc_id;
    enum mc_state { INITED = 0, RUNNING } state;
    std::shared_ptr<rocksdb::ColumnFamilyHandle> cf;
    rocksdb::Slice *start;
    rocksdb::Slice *limit;
    int concurrency = 0;
  };

  int m_latest_mc_id;
  mysql_mutex_t m_mc_mutex;
  std::map<int, Manual_compaction_request> m_requests;

 public:
  Rdb_manual_compaction_thread() {
    mysql_mutex_init(0, &m_mc_mutex, MY_MUTEX_INIT_FAST);
  }

  virtual ~Rdb_manual_compaction_thread() override {
    mysql_mutex_destroy(&m_mc_mutex);
  }

  virtual void run() override;
  int request_manual_compaction(std::shared_ptr<rocksdb::ColumnFamilyHandle> cf,
                                rocksdb::Slice *start, rocksdb::Slice *limit,
                                int concurrency = 0);
  bool is_manual_compaction_finished(int mc_id);
  void clear_manual_compaction_request(int mc_id, bool init_only = false);
  void clear_all_manual_compaction_requests();
};

/*
  Drop index thread control
*/

struct Rdb_drop_index_thread : public Rdb_thread {
  virtual void run() override;
};

}  // namespace myrocks
