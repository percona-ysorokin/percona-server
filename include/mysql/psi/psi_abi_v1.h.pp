#include "mysql/psi/psi.h"
C_MODE_START
struct TABLE_SHARE;
struct PSI_mutex;
struct PSI_rwlock;
struct PSI_cond;
struct PSI_table_share;
struct PSI_table;
struct PSI_thread;
struct PSI_file;
struct PSI_socket;
struct PSI_table_locker;
struct PSI_bootstrap
{
  void* (*get_interface)(int version);
};
struct PSI_idle_locker;
struct PSI_mutex_locker;
struct PSI_rwlock_locker;
struct PSI_cond_locker;
struct PSI_file_locker;
enum PSI_mutex_operation
{
  PSI_MUTEX_LOCK= 0,
  PSI_MUTEX_TRYLOCK= 1
};
enum PSI_rwlock_operation
{
  PSI_RWLOCK_READLOCK= 0,
  PSI_RWLOCK_WRITELOCK= 1,
  PSI_RWLOCK_TRYREADLOCK= 2,
  PSI_RWLOCK_TRYWRITELOCK= 3
};
enum PSI_cond_operation
{
  PSI_COND_WAIT= 0,
  PSI_COND_TIMEDWAIT= 1
};
enum PSI_file_operation
{
  PSI_FILE_CREATE= 0,
  PSI_FILE_CREATE_TMP= 1,
  PSI_FILE_OPEN= 2,
  PSI_FILE_STREAM_OPEN= 3,
  PSI_FILE_CLOSE= 4,
  PSI_FILE_STREAM_CLOSE= 5,
  PSI_FILE_READ= 6,
  PSI_FILE_WRITE= 7,
  PSI_FILE_SEEK= 8,
  PSI_FILE_TELL= 9,
  PSI_FILE_FLUSH= 10,
  PSI_FILE_STAT= 11,
  PSI_FILE_FSTAT= 12,
  PSI_FILE_CHSIZE= 13,
  PSI_FILE_DELETE= 14,
  PSI_FILE_RENAME= 15,
  PSI_FILE_SYNC= 16
};
enum PSI_table_io_operation
{
  PSI_TABLE_FETCH_ROW= 0,
  PSI_TABLE_WRITE_ROW= 1,
  PSI_TABLE_UPDATE_ROW= 2,
  PSI_TABLE_DELETE_ROW= 3
};
enum PSI_table_lock_operation
{
  PSI_TABLE_LOCK= 0,
  PSI_TABLE_EXTERNAL_LOCK= 1
};
enum PSI_socket_state
{
  PSI_SOCKET_STATE_IDLE= 0,
  PSI_SOCKET_STATE_ACTIVE= 1
};
enum PSI_socket_operation
{
  PSI_SOCKET_CREATE= 0,
  PSI_SOCKET_CONNECT= 1,
  PSI_SOCKET_BIND= 2,
  PSI_SOCKET_CLOSE= 3,
  PSI_SOCKET_SEND= 4,
  PSI_SOCKET_RECV= 5,
  PSI_SOCKET_SENDTO= 6,
  PSI_SOCKET_RECVFROM= 7,
  PSI_SOCKET_SENDMSG= 8,
  PSI_SOCKET_RECVMSG= 9,
  PSI_SOCKET_SEEK= 10,
  PSI_SOCKET_OPT= 11,
  PSI_SOCKET_STAT= 12,
  PSI_SOCKET_SHUTDOWN= 13
};
typedef unsigned int PSI_mutex_key;
typedef unsigned int PSI_rwlock_key;
typedef unsigned int PSI_cond_key;
typedef unsigned int PSI_thread_key;
typedef unsigned int PSI_file_key;
typedef unsigned int PSI_socket_key;
struct PSI_mutex_info_v1
{
  PSI_mutex_key *m_key;
  const char *m_name;
  int m_flags;
};
struct PSI_rwlock_info_v1
{
  PSI_rwlock_key *m_key;
  const char *m_name;
  int m_flags;
};
struct PSI_cond_info_v1
{
  PSI_cond_key *m_key;
  const char *m_name;
  int m_flags;
};
struct PSI_thread_info_v1
{
  PSI_thread_key *m_key;
  const char *m_name;
  int m_flags;
};
struct PSI_file_info_v1
{
  PSI_file_key *m_key;
  const char *m_name;
  int m_flags;
};
struct PSI_socket_info_v1
{
  PSI_socket_key *m_key;
  const char *m_name;
  int m_flags;
};
struct PSI_idle_locker_state_v1
{
  uint m_flags;
  struct PSI_thread *m_thread;
  ulonglong m_timer_start;
  ulonglong (*m_timer)(void);
  void *m_wait;
};
struct PSI_mutex_locker_state_v1
{
  uint m_flags;
  struct PSI_mutex *m_mutex;
  struct PSI_thread *m_thread;
  ulonglong m_timer_start;
  ulonglong (*m_timer)(void);
  enum PSI_mutex_operation m_operation;
  const char* m_src_file;
  int m_src_line;
  void *m_wait;
};
struct PSI_rwlock_locker_state_v1
{
  uint m_flags;
  struct PSI_rwlock *m_rwlock;
  struct PSI_thread *m_thread;
  ulonglong m_timer_start;
  ulonglong (*m_timer)(void);
  enum PSI_rwlock_operation m_operation;
  const char* m_src_file;
  int m_src_line;
  void *m_wait;
};
struct PSI_cond_locker_state_v1
{
  uint m_flags;
  struct PSI_cond *m_cond;
  struct PSI_mutex *m_mutex;
  struct PSI_thread *m_thread;
  ulonglong m_timer_start;
  ulonglong (*m_timer)(void);
  enum PSI_cond_operation m_operation;
  const char* m_src_file;
  int m_src_line;
  void *m_wait;
};
struct PSI_file_locker_state_v1
{
  uint m_flags;
  struct PSI_file *m_file;
  struct PSI_thread *m_thread;
  size_t m_number_of_bytes;
  ulonglong m_timer_start;
  ulonglong (*m_timer)(void);
  enum PSI_file_operation m_operation;
  const char* m_src_file;
  int m_src_line;
  void *m_wait;
};
struct PSI_table_locker_state_v1
{
  uint m_flags;
  struct PSI_table *m_table;
  struct PSI_table_share *m_table_share;
  struct PSI_thread *m_thread;
  ulonglong m_timer_start;
  ulonglong (*m_timer)(void);
  enum PSI_table_io_operation m_io_operation;
  uint m_index;
  const char* m_src_file;
  int m_src_line;
  void *m_wait;
};
struct PSI_socket_locker_state_v1
{
  uint m_flags;
  struct PSI_socket *m_socket;
  struct PSI_thread *m_thread;
  size_t m_number_of_bytes;
  ulonglong m_timer_start;
  ulonglong (*m_timer)(void);
  enum PSI_socket_operation m_operation;
  const char* m_src_file;
  int m_src_line;
  void *m_wait;
};
typedef void (*register_mutex_v1_t)
  (const char *category, struct PSI_mutex_info_v1 *info, int count);
typedef void (*register_rwlock_v1_t)
  (const char *category, struct PSI_rwlock_info_v1 *info, int count);
typedef void (*register_cond_v1_t)
  (const char *category, struct PSI_cond_info_v1 *info, int count);
typedef void (*register_thread_v1_t)
  (const char *category, struct PSI_thread_info_v1 *info, int count);
typedef void (*register_file_v1_t)
  (const char *category, struct PSI_file_info_v1 *info, int count);
typedef void (*register_socket_v1_t)
  (const char *category, struct PSI_socket_info_v1 *info, int count);
typedef struct PSI_mutex* (*init_mutex_v1_t)
  (PSI_mutex_key key, const void *identity);
typedef void (*destroy_mutex_v1_t)(struct PSI_mutex *mutex);
typedef struct PSI_rwlock* (*init_rwlock_v1_t)
  (PSI_rwlock_key key, const void *identity);
typedef void (*destroy_rwlock_v1_t)(struct PSI_rwlock *rwlock);
typedef struct PSI_cond* (*init_cond_v1_t)
  (PSI_cond_key key, const void *identity);
typedef void (*destroy_cond_v1_t)(struct PSI_cond *cond);
typedef struct PSI_socket* (*init_socket_v1_t)
  (PSI_socket_key key, const my_socket *fd);
typedef void (*destroy_socket_v1_t)(struct PSI_socket *socket);
typedef struct PSI_table_share* (*get_table_share_v1_t)
  (my_bool temporary, struct TABLE_SHARE *share);
typedef void (*release_table_share_v1_t)(struct PSI_table_share *share);
typedef void (*drop_table_share_v1_t)
  (const char *schema_name, int schema_name_length,
   const char *table_name, int table_name_length);
typedef struct PSI_table* (*open_table_v1_t)
  (struct PSI_table_share *share, const void *identity);
typedef void (*close_table_v1_t)(struct PSI_table *table);
typedef void (*create_file_v1_t)(PSI_file_key key, const char *name,
                                 File file);
typedef int (*spawn_thread_v1_t)(PSI_thread_key key,
                                 pthread_t *thread,
                                 const pthread_attr_t *attr,
                                 void *(*start_routine)(void*), void *arg);
typedef struct PSI_thread* (*new_thread_v1_t)
  (PSI_thread_key key, const void *identity, ulong thread_id);
typedef void (*set_thread_id_v1_t)(struct PSI_thread *thread,
                                   unsigned long id);
typedef struct PSI_thread* (*get_thread_v1_t)(void);
typedef void (*set_thread_user_v1_t)(const char *user, int user_len);
typedef void (*set_thread_user_host_v1_t)(const char *user, int user_len,
                                          const char *host, int host_len);
typedef void (*set_thread_db_v1_t)(const char* db, int db_len);
typedef void (*set_thread_command_v1_t)(int command);
typedef void (*set_thread_start_time_v1_t)(time_t start_time);
typedef void (*set_thread_state_v1_t)(const char* state);
typedef void (*set_thread_info_v1_t)(const char* info, int info_len);
typedef void (*set_thread_v1_t)(struct PSI_thread *thread);
typedef void (*delete_current_thread_v1_t)(void);
typedef void (*delete_thread_v1_t)(struct PSI_thread *thread);
typedef struct PSI_mutex_locker* (*get_thread_mutex_locker_v1_t)
  (struct PSI_mutex_locker_state_v1 *state,
   struct PSI_mutex *mutex,
   enum PSI_mutex_operation op);
typedef struct PSI_rwlock_locker* (*get_thread_rwlock_locker_v1_t)
  (struct PSI_rwlock_locker_state_v1 *state,
   struct PSI_rwlock *rwlock,
   enum PSI_rwlock_operation op);
typedef struct PSI_cond_locker* (*get_thread_cond_locker_v1_t)
  (struct PSI_cond_locker_state_v1 *state,
   struct PSI_cond *cond, struct PSI_mutex *mutex,
   enum PSI_cond_operation op);
typedef struct PSI_table_locker* (*get_thread_table_io_locker_v1_t)
  (struct PSI_table_locker_state_v1 *state,
   struct PSI_table *table, enum PSI_table_io_operation op, uint index);
typedef struct PSI_table_locker* (*get_thread_table_lock_locker_v1_t)
  (struct PSI_table_locker_state_v1 *state,
   struct PSI_table *table, enum PSI_table_lock_operation op, ulong flags);
typedef struct PSI_file_locker* (*get_thread_file_name_locker_v1_t)
  (struct PSI_file_locker_state_v1 *state,
   PSI_file_key key, enum PSI_file_operation op, const char *name,
   const void *identity);
typedef struct PSI_file_locker* (*get_thread_file_stream_locker_v1_t)
  (struct PSI_file_locker_state_v1 *state,
   struct PSI_file *file, enum PSI_file_operation op);
typedef struct PSI_file_locker* (*get_thread_file_descriptor_locker_v1_t)
  (struct PSI_file_locker_state_v1 *state,
   File file, enum PSI_file_operation op);
typedef struct PSI_socket_locker* (*get_thread_socket_locker_v1_t)
  (struct PSI_socket_locker_state_v1 *state,
   struct PSI_socket *socket, enum PSI_socket_operation op);
typedef void (*unlock_mutex_v1_t)
  (struct PSI_mutex *mutex);
typedef void (*unlock_rwlock_v1_t)
  (struct PSI_rwlock *rwlock);
typedef void (*signal_cond_v1_t)
  (struct PSI_cond *cond);
typedef void (*broadcast_cond_v1_t)
  (struct PSI_cond *cond);
typedef struct PSI_idle_locker* (*start_idle_wait_v1_t)
  (struct PSI_idle_locker_state_v1 *state, const char *src_file, uint src_line);
typedef void (*end_idle_wait_v1_t)
  (struct PSI_idle_locker *locker);
typedef void (*start_mutex_wait_v1_t)
  (struct PSI_mutex_locker *locker, const char *src_file, uint src_line);
typedef void (*end_mutex_wait_v1_t)
  (struct PSI_mutex_locker *locker, int rc);
typedef void (*start_rwlock_rdwait_v1_t)
  (struct PSI_rwlock_locker *locker, const char *src_file, uint src_line);
typedef void (*end_rwlock_rdwait_v1_t)
  (struct PSI_rwlock_locker *locker, int rc);
typedef void (*start_rwlock_wrwait_v1_t)
  (struct PSI_rwlock_locker *locker, const char *src_file, uint src_line);
typedef void (*end_rwlock_wrwait_v1_t)
  (struct PSI_rwlock_locker *locker, int rc);
typedef void (*start_cond_wait_v1_t)
  (struct PSI_cond_locker *locker, const char *src_file, uint src_line);
typedef void (*end_cond_wait_v1_t)
  (struct PSI_cond_locker *locker, int rc);
typedef void (*start_table_io_wait_v1_t)
  (struct PSI_table_locker *locker, const char *src_file, uint src_line);
typedef void (*end_table_io_wait_v1_t)(struct PSI_table_locker *locker);
typedef void (*start_table_lock_wait_v1_t)
  (struct PSI_table_locker *locker, const char *src_file, uint src_line);
typedef void (*end_table_lock_wait_v1_t)(struct PSI_table_locker *locker);
typedef struct PSI_file* (*start_file_open_wait_v1_t)
  (struct PSI_file_locker *locker, const char *src_file, uint src_line);
typedef void (*end_file_open_wait_v1_t)(struct PSI_file_locker *locker);
typedef void (*end_file_open_wait_and_bind_to_descriptor_v1_t)
  (struct PSI_file_locker *locker, File file);
typedef void (*start_file_wait_v1_t)
  (struct PSI_file_locker *locker, size_t count,
   const char *src_file, uint src_line);
typedef void (*end_file_wait_v1_t)
  (struct PSI_file_locker *locker, size_t count);
typedef void (*start_socket_wait_v1_t)
  (struct PSI_socket_locker *locker, size_t count,
   const char *src_file, uint src_line);
typedef void (*end_socket_wait_v1_t)
  (struct PSI_socket_locker *locker, size_t count);
typedef void (*set_socket_state_v1_t)(struct PSI_socket *socket,
                                      enum PSI_socket_state state);
typedef void (*set_socket_descriptor_v1_t)(struct PSI_socket *socket,
                                             uint fd);
typedef void (*set_socket_address_v1_t)(struct PSI_socket *socket,
                                        const struct sockaddr * addr,
                                        socklen_t addr_len);
typedef void (*set_socket_info_v1_t)(struct PSI_socket *socket,
                                     my_socket *fd,
                                     const struct sockaddr *addr,
                                     socklen_t *addr_len);
typedef void (*set_socket_thread_owner_v1_t)(struct PSI_socket *socket,
                                             struct PSI_thread *thread);
struct PSI_v1
{
  register_mutex_v1_t register_mutex;
  register_rwlock_v1_t register_rwlock;
  register_cond_v1_t register_cond;
  register_thread_v1_t register_thread;
  register_file_v1_t register_file;
  register_socket_v1_t register_socket;
  init_mutex_v1_t init_mutex;
  destroy_mutex_v1_t destroy_mutex;
  init_rwlock_v1_t init_rwlock;
  destroy_rwlock_v1_t destroy_rwlock;
  init_cond_v1_t init_cond;
  destroy_cond_v1_t destroy_cond;
  init_socket_v1_t init_socket;
  destroy_socket_v1_t destroy_socket;
  get_table_share_v1_t get_table_share;
  release_table_share_v1_t release_table_share;
  drop_table_share_v1_t drop_table_share;
  open_table_v1_t open_table;
  close_table_v1_t close_table;
  create_file_v1_t create_file;
  spawn_thread_v1_t spawn_thread;
  new_thread_v1_t new_thread;
  set_thread_id_v1_t set_thread_id;
  get_thread_v1_t get_thread;
  set_thread_user_v1_t set_thread_user;
  set_thread_user_host_v1_t set_thread_user_host;
  set_thread_db_v1_t set_thread_db;
  set_thread_command_v1_t set_thread_command;
  set_thread_start_time_v1_t set_thread_start_time;
  set_thread_state_v1_t set_thread_state;
  set_thread_info_v1_t set_thread_info;
  set_thread_v1_t set_thread;
  delete_current_thread_v1_t delete_current_thread;
  delete_thread_v1_t delete_thread;
  get_thread_mutex_locker_v1_t get_thread_mutex_locker;
  get_thread_rwlock_locker_v1_t get_thread_rwlock_locker;
  get_thread_cond_locker_v1_t get_thread_cond_locker;
  get_thread_table_io_locker_v1_t get_thread_table_io_locker;
  get_thread_table_lock_locker_v1_t get_thread_table_lock_locker;
  get_thread_file_name_locker_v1_t get_thread_file_name_locker;
  get_thread_file_stream_locker_v1_t get_thread_file_stream_locker;
  get_thread_file_descriptor_locker_v1_t get_thread_file_descriptor_locker;
  get_thread_socket_locker_v1_t get_thread_socket_locker;
  unlock_mutex_v1_t unlock_mutex;
  unlock_rwlock_v1_t unlock_rwlock;
  signal_cond_v1_t signal_cond;
  broadcast_cond_v1_t broadcast_cond;
  start_idle_wait_v1_t start_idle_wait;
  end_idle_wait_v1_t end_idle_wait;
  start_mutex_wait_v1_t start_mutex_wait;
  end_mutex_wait_v1_t end_mutex_wait;
  start_rwlock_rdwait_v1_t start_rwlock_rdwait;
  end_rwlock_rdwait_v1_t end_rwlock_rdwait;
  start_rwlock_wrwait_v1_t start_rwlock_wrwait;
  end_rwlock_wrwait_v1_t end_rwlock_wrwait;
  start_cond_wait_v1_t start_cond_wait;
  end_cond_wait_v1_t end_cond_wait;
  start_table_io_wait_v1_t start_table_io_wait;
  end_table_io_wait_v1_t end_table_io_wait;
  start_table_lock_wait_v1_t start_table_lock_wait;
  end_table_lock_wait_v1_t end_table_lock_wait;
  start_file_open_wait_v1_t start_file_open_wait;
  end_file_open_wait_v1_t end_file_open_wait;
  end_file_open_wait_and_bind_to_descriptor_v1_t
    end_file_open_wait_and_bind_to_descriptor;
  start_file_wait_v1_t start_file_wait;
  end_file_wait_v1_t end_file_wait;
  start_socket_wait_v1_t start_socket_wait;
  end_socket_wait_v1_t end_socket_wait;
  set_socket_state_v1_t set_socket_state;
  set_socket_descriptor_v1_t set_socket_descriptor;
  set_socket_address_v1_t set_socket_address;
  set_socket_info_v1_t set_socket_info;
  set_socket_thread_owner_v1_t set_socket_thread_owner;
};
typedef struct PSI_v1 PSI;
typedef struct PSI_mutex_info_v1 PSI_mutex_info;
typedef struct PSI_rwlock_info_v1 PSI_rwlock_info;
typedef struct PSI_cond_info_v1 PSI_cond_info;
typedef struct PSI_thread_info_v1 PSI_thread_info;
typedef struct PSI_file_info_v1 PSI_file_info;
typedef struct PSI_socket_info_v1 PSI_socket_info;
typedef struct PSI_idle_locker_state_v1 PSI_idle_locker_state;
typedef struct PSI_mutex_locker_state_v1 PSI_mutex_locker_state;
typedef struct PSI_rwlock_locker_state_v1 PSI_rwlock_locker_state;
typedef struct PSI_cond_locker_state_v1 PSI_cond_locker_state;
typedef struct PSI_file_locker_state_v1 PSI_file_locker_state;
typedef struct PSI_table_locker_state_v1 PSI_table_locker_state;
typedef struct PSI_socket_locker_state_v1 PSI_socket_locker_state;
extern MYSQL_PLUGIN_IMPORT PSI *PSI_server;
C_MODE_END
