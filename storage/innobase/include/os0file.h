/***********************************************************************

Copyright (c) 1995, 2020, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2009, 2017, Percona Inc.

Portions of this file contain modifications contributed and copyrighted
by Percona Inc.. Those modifications are
gratefully acknowledged and are described briefly in the InnoDB
documentation. The contributions by Percona Inc. are incorporated with
their permission, and subject to the conditions contained in the file
COPYING.Percona.

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

***********************************************************************/

/** @file include/os0file.h
 The interface to the operating system file io

 Created 10/21/1995 Heikki Tuuri
 *******************************************************/

#ifndef os0file_h
#define os0file_h

#include "my_dbug.h"
#include "my_io.h"

#include "os/file.h"
#include "os0atomic.h"
#include "os0enc.h"

#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#else
#include <Strsafe.h>
#include <locale>
#include <string>
#endif /* !_WIN32 */
#include "page0types.h"

#include <functional>
#include <stack>

/** Prefix all files and directory created under data directory with special
string so that it never conflicts with MySQL schema directory. */
#define OS_FILE_PREFIX "#"

/** File node of a tablespace or the log data space */
struct fil_node_t;

struct trx_t;

extern bool os_has_said_disk_full;

/** Number of pending read operations */
extern ulint os_n_pending_reads;
/** Number of pending write operations */
extern ulint os_n_pending_writes;

/* Flush after each os_fsync_threshold bytes */
extern unsigned long long os_fsync_threshold;

/** Set to true when default master key is used. This variable
main purpose is to avoid extra Encryption::get_master_key() when there
are no encrypted tablespaces */
extern bool default_master_key_used;

/** File offset in bytes */
typedef ib_uint64_t os_offset_t;

/** Free a page after sync IO
@param[in,out]	block		The block to free/release */
void os_free_block(file::Block *block) noexcept;

namespace file {
/** Blocks for doing IO, used in the transparent compression
and encryption code. */
struct Block {
  /** Default constructor */
  Block() : m_ptr(), m_in_use() {}

  byte *m_ptr;

  byte pad[ut::INNODB_CACHE_LINE_SIZE - sizeof(ulint)];
  lock_word_t m_in_use;
};

struct Block_deleter {
  void operator()(Block *ptr) const noexcept { os_free_block(ptr); }
};

}  // namespace file

#ifdef _WIN32

typedef HANDLE os_file_dir_t; /*!< directory stream */

/** We define always WIN_ASYNC_IO, and check at run-time whether
the OS actually supports it: Win 95 does not, NT does. */
#define WIN_ASYNC_IO

/** Use unbuffered I/O */
#define UNIV_NON_BUFFERED_IO

static const constexpr auto SRV_PATH_SEPARATOR = '\\';

/** File handle */
#define os_file_t HANDLE

/** Convert a C file descriptor to a native file handle
@param fd file descriptor
@return native file handle */
#define OS_FILE_FROM_FD(fd) (HANDLE) _get_osfhandle(fd)

/** Associates a C file descriptor with an existing native file handle
@param[in]	file	native file handle
@return C file descriptor */
#define OS_FD_FROM_FILE(file) _open_osfhandle((intptr_t)file, _O_RDONLY)

/** Closes the file associated with C file descriptor fd
@param[in]	fd	C file descriptor
@return 0 if success */
#define OS_FILE_CLOSE_FD(fd) _close(fd)

#else /* _WIN32 */

#define SRV_PATH_SEPARATOR '/'

/** File handle */
typedef int os_file_t;

/** Convert a C file descriptor to a native file handle
@param fd file descriptor
@return native file handle */
#define OS_FILE_FROM_FD(fd) fd

/** C file descriptor from an existing native file handle
@param[in]	file	native file handle
@return C file descriptor */
#define OS_FD_FROM_FILE(file) file

/** Closes the file associated with C file descriptor fd
@param[in]	fd	C file descriptor
@return 0 if success */
#define OS_FILE_CLOSE_FD(fd) (os_file_close(fd) ? 0 : -1)

#endif /* _WIN32 */

static const constexpr os_file_t OS_FILE_CLOSED = os_file_t(~0);

/** Common file descriptor for file IO instrumentation with PFS
on windows and other platforms */
struct pfs_os_file_t {
#ifdef UNIV_PFS_IO
  struct PSI_file *m_psi;
#else  /* UNIV_PFS_IO */
  pfs_os_file_t &operator=(os_file_t file) {
    m_file = file;
    return (*this);
  }
#endif /* UNIV_PFS_IO */

  os_file_t m_file;

  bool is_closed() const noexcept { return m_file == OS_FILE_CLOSED; }

  void set_closed() noexcept { m_file = OS_FILE_CLOSED; }
};

/** The next value should be smaller or equal to the smallest sector size used
on any disk. A log block is required to be a portion of disk which is written
so that if the start and the end of a block get written to disk, then the
whole block gets written. This should be true even in most cases of a crash:
if this fails for a log block, then it is equivalent to a media failure in the
log. */

#define OS_FILE_LOG_BLOCK_SIZE 512

/** Options for os_file_create_func @{ */
enum os_file_create_t {
  OS_FILE_OPEN = 51,   /*!< to open an existing file (if
                       doesn't exist, error) */
  OS_FILE_CREATE,      /*!< to create new file (if
                       exists, error) */
  OS_FILE_OPEN_RAW,    /*!< to open a raw device or disk
                       partition */
  OS_FILE_CREATE_PATH, /*!< to create the directories */
  OS_FILE_OPEN_RETRY,  /*!< open with retry */

  /** Flags that can be combined with the above values. Please ensure
  that the above values stay below 128. */

  OS_FILE_ON_ERROR_NO_EXIT = 128, /*!< do not exit on unknown errors */
  OS_FILE_ON_ERROR_SILENT = 256   /*!< don't print diagnostic messages to
                            the log unless it is a fatal error,
                            this flag is only used if
                            ON_ERROR_NO_EXIT is set */
};

/** Options for os_file_advise_func @{ */
enum os_file_advise_t {
  OS_FILE_ADVISE_NORMAL = 1,     /*!< no advice on access pattern
                                 (default) */
  OS_FILE_ADVISE_RANDOM = 2,     /*!< access in random order */
  OS_FILE_ADVISE_SEQUENTIAL = 4, /*!< access the specified data
                                 sequentially (with lower offsets read
                                 before higher ones) */
  OS_FILE_ADVISE_WILLNEED = 8,   /*!< specified data will be accessed
                                 in the near future */
  OS_FILE_ADVISE_DONTNEED = 16,  /*!< specified data will not be
                                 accessed in the near future */
  OS_FILE_ADVISE_NOREUSE = 32    /*!< access only once */
};

static const ulint OS_FILE_READ_ONLY = 333;
static const ulint OS_FILE_READ_WRITE = 444;

/** Used by MySQLBackup */
static const ulint OS_FILE_READ_ALLOW_DELETE = 555;

/* Options for file_create */
static const ulint OS_FILE_AIO = 61;
static const ulint OS_FILE_NORMAL = 62;
/** @} */

/** Types for file create @{ */
static const ulint OS_DATA_FILE = 100;
static const ulint OS_LOG_FILE = 101;
/* Don't use this for Data files, Log files. Use it for smaller files
or if number of bytes to write are not multiple of sector size.
With this flag, writes to file will be always buffered and ignores the value
of innodb_flush_method. */
static const ulint OS_BUFFERED_FILE = 102;

static const ulint OS_CLONE_DATA_FILE = 103;
static const ulint OS_CLONE_LOG_FILE = 104;

/** Doublewrite files. */
static const ulint OS_DBLWR_FILE = 105;

/** Redo log archive file. */
static const ulint OS_REDO_LOG_ARCHIVE_FILE = 105;
/** @} */

/** Error codes from os_file_get_last_error @{ */
static const ulint OS_FILE_NOT_FOUND = 71;
static const ulint OS_FILE_DISK_FULL = 72;
static const ulint OS_FILE_ALREADY_EXISTS = 73;
static const ulint OS_FILE_PATH_ERROR = 74;

/** wait for OS aio resources to become available again */
static const ulint OS_FILE_AIO_RESOURCES_RESERVED = 75;

static const ulint OS_FILE_SHARING_VIOLATION = 76;
static const ulint OS_FILE_ERROR_NOT_SPECIFIED = 77;
static const ulint OS_FILE_INSUFFICIENT_RESOURCE = 78;
static const ulint OS_FILE_AIO_INTERRUPTED = 79;
static const ulint OS_FILE_OPERATION_ABORTED = 80;
static const ulint OS_FILE_ACCESS_VIOLATION = 81;
static const ulint OS_FILE_NAME_TOO_LONG = 82;
static const ulint OS_FILE_ERROR_MAX = 100;
/** @} */

/** Types for AIO operations @{ */

/** No transformations during read/write, write as is. */
#define IORequestRead IORequest(IORequest::READ)
#define IORequestWrite IORequest(IORequest::WRITE)
#define IORequestLogRead IORequest(IORequest::LOG | IORequest::READ)
#define IORequestLogWrite IORequest(IORequest::LOG | IORequest::WRITE)

/**
The IO Context that is passed down to the low level IO code */
class IORequest {
 public:
  /** Flags passed in the request, they can be ORred together. */
  enum {
    UNSET = 0,
    READ = 1,
    WRITE = 2,

    /** Request for a doublewrite page IO */
    DBLWR = 4,

    /** Enumerations below can be ORed to READ/WRITE above*/

    /** Data file */
    DATA_FILE = 8,

    /** Log file request*/
    LOG = 16,

    /** Disable partial read warnings */
    DISABLE_PARTIAL_IO_WARNINGS = 32,

    /** Do not to wake i/o-handler threads, but the caller will do
    the waking explicitly later, in this way the caller can post
    several requests in a batch; NOTE that the batch must not be
    so big that it exhausts the slots in AIO arrays! NOTE that
    a simulated batch may introduce hidden chances of deadlocks,
    because I/Os are not actually handled until all
    have been posted: use with great caution! */
    DO_NOT_WAKE = 64,

    /** Ignore failed reads of non-existent pages */
    IGNORE_MISSING = 128,

    /** Use punch hole if available, only makes sense if
    compression algorithm != NONE. Ignored if not set */
    PUNCH_HOLE = 256,

    /** Force raw read, do not try to compress/decompress.
    This can be used to force a read and write without any
    compression e.g., for redo log, merge sort temporary files
    and the truncate redo log. */
    NO_COMPRESSION = 512,

    /** We optimise cases where punch hole is not done if the compressed length
    of the page is the same as the original size of the page. Ignore such
    optimisations if this flag is set. */
    DISABLE_PUNCH_HOLE_OPTIMISATION = 1024,

    /** Force write of decrypted pages in encrypted tablespace. */
    NO_ENCRYPTION = 2048
  };

  /** Default constructor */
  IORequest()
      : m_block_size(UNIV_SECTOR_SIZE),
        m_type(READ),
        m_compression(),
        m_encryption(),
        m_is_page_zip_compressed(false),
        m_zip_page_physical_size(0) {
    /* No op */
  }

  /**
  @param[in]	type		Request type, can be a value that is
                                  ORed from the above enum */
  explicit IORequest(ulint type)
      : m_block_size(UNIV_SECTOR_SIZE),
        m_type(static_cast<uint16_t>(type)),
        m_compression(),
        m_encryption(),
        m_is_page_zip_compressed(false),
        m_zip_page_physical_size(0) {
    if (is_log()) {
      disable_compression();
    }

    if (!is_punch_hole_supported()) {
      clear_punch_hole();
    }
  }

  /** @return true if ignore missing flag is set */
  static bool ignore_missing(ulint type) MY_ATTRIBUTE((warn_unused_result)) {
    return ((type & IGNORE_MISSING) == IGNORE_MISSING);
  }

  /** @return true if it is a read request */
  bool is_read() const MY_ATTRIBUTE((warn_unused_result)) {
    return ((m_type & READ) == READ);
  }

  /** @return true if it is a write request */
  bool is_write() const MY_ATTRIBUTE((warn_unused_result)) {
    return ((m_type & WRITE) == WRITE);
  }

  /** @return true if it is a redo log write */
  bool is_log() const MY_ATTRIBUTE((warn_unused_result)) {
    return ((m_type & LOG) == LOG);
  }

  /** @return true if the simulated AIO thread should be woken up */
  bool is_wake() const MY_ATTRIBUTE((warn_unused_result)) {
    return ((m_type & DO_NOT_WAKE) == 0);
  }

  /** @return true if partial read warning disabled */
  bool is_partial_io_warning_disabled() const
      MY_ATTRIBUTE((warn_unused_result)) {
    return ((m_type & DISABLE_PARTIAL_IO_WARNINGS) ==
            DISABLE_PARTIAL_IO_WARNINGS);
  }

  /** Disable partial read warnings */
  void disable_partial_io_warnings() { m_type |= DISABLE_PARTIAL_IO_WARNINGS; }

  /** @return true if missing files should be ignored */
  bool ignore_missing() const MY_ATTRIBUTE((warn_unused_result)) {
    return (ignore_missing(m_type));
  }

  /** @return true if punch hole should be used */
  bool punch_hole() const MY_ATTRIBUTE((warn_unused_result)) {
    return ((m_type & PUNCH_HOLE) == PUNCH_HOLE);
  }

  /** @return true if punch hole needs to be done always if it's supported and
  if the page is to be compressed. */
  bool is_punch_hole_optimisation_disabled() const
      MY_ATTRIBUTE((warn_unused_result)) {
    ut_ad(is_compressed() && punch_hole());

    return (m_type & DISABLE_PUNCH_HOLE_OPTIMISATION) ==
           DISABLE_PUNCH_HOLE_OPTIMISATION;
  }

  /** @return true if the read should be validated */
  bool validate() const MY_ATTRIBUTE((warn_unused_result)) {
    ut_ad(is_read() ^ is_write());

    return (!is_read() || !punch_hole());
  }

  /** Set the punch hole flag */
  void set_punch_hole() {
    if (is_punch_hole_supported()) {
      m_type |= PUNCH_HOLE;
    }
  }

  /** Set the force punch hole flag */
  void disable_punch_hole_optimisation() {
    if (is_punch_hole_supported()) {
      m_type |= DISABLE_PUNCH_HOLE_OPTIMISATION;
    }
  }

  /** Clear the do not wake flag */
  void clear_do_not_wake() { m_type &= ~DO_NOT_WAKE; }

  /** Clear the punch hole flag */
  void clear_punch_hole() { m_type &= ~PUNCH_HOLE; }

  /** @return the block size to use for IO */
  ulint block_size() const MY_ATTRIBUTE((warn_unused_result)) {
    return (m_block_size);
  }

  /** Set the block size for IO
  @param[in] block_size		Block size to set */
  void block_size(ulint block_size) {
    m_block_size = static_cast<uint32_t>(block_size);
  }

  /** Clear all compression related flags */
  void clear_compressed() {
    clear_punch_hole();

    m_compression.m_type = Compression::NONE;
  }

  /** Compare two requests
  @return true if the are equal */
  bool operator==(const IORequest &rhs) const { return (m_type == rhs.m_type); }

  /** Set compression algorithm
  @param[in]	type	The compression algorithm to use */
  void compression_algorithm(Compression::Type type) {
    if (type == Compression::NONE) {
      return;
    }

    set_punch_hole();

    m_compression.m_type = type;
  }

  /** Get the compression algorithm.
  @return the compression algorithm */
  Compression compression_algorithm() const MY_ATTRIBUTE((warn_unused_result)) {
    return (m_compression);
  }

  /** @return true if the page should be compressed */
  bool is_compressed() const MY_ATTRIBUTE((warn_unused_result)) {
    return (compression_algorithm().m_type != Compression::NONE);
  }

  /** @return true if the page read should not be transformed. */
  bool is_compression_enabled() const MY_ATTRIBUTE((warn_unused_result)) {
    return ((m_type & NO_COMPRESSION) == 0);
  }

  /** @return true if the page write should not be encrypted */
  MY_NODISCARD bool is_encryption_disabled() const noexcept {
    return ((m_type & NO_ENCRYPTION) != 0);
  }

  /** Disable transformations. */
  void disable_compression() { m_type |= NO_COMPRESSION; }

  /** Disable encryption of a page in encrypted tablespace */
  void disable_encryption() noexcept { m_type |= NO_ENCRYPTION; }

  /** Set encryption algorithm
  @param[in] type		The encryption algorithm to use */
  void encryption_algorithm(Encryption::Type type) {
    if (type == Encryption::NONE) {
      return;
    }

    m_encryption.set_type(type);
  }

  /** Set encryption key and iv
  @param[in] key		The encryption key to use
  @param[in] key_len	length of the encryption key
  @param[in] iv		The encryption iv to use */
  void encryption_key(byte *key, ulint key_len, byte *iv, uint key_version,
                      uint key_id, byte *tablespace_key, const char *uuid,
                      std::map<uint, byte *> *key_versions_cache) {
    m_encryption.set_key(key, key_len);
    m_encryption.set_key_versions_cache(key_versions_cache);
    m_encryption.set_initial_vector(iv);
    m_encryption.set_key_version(key_version);
    m_encryption.set_key_id(key_id);
    m_encryption.set_tablespace_key(tablespace_key);
    m_encryption.set_key_id_uuid(uuid);
  }

  void encryption_rotation(Encryption_rotation encryption_rotation) {
    m_encryption.set_encryption_rotation(encryption_rotation);
  }

  /** Set encryption key and iv
  @param[in] key		The encryption key to use
  @param[in] key_len	length of the encryption key
  @param[in] iv		The encryption iv to use */
  void encryption_key(byte *key, ulint key_len, byte *iv) {
    m_encryption.set_key(key);
    m_encryption.set_key_length(key_len);
    m_encryption.set_initial_vector(iv);
  }

  /** Get the encryption algorithm.
  @return the encryption algorithm */
  Encryption encryption_algorithm() const MY_ATTRIBUTE((warn_unused_result)) {
    return (m_encryption);
  }

  /** @return true if the page should be encrypted. */
  bool is_encrypted() const MY_ATTRIBUTE((warn_unused_result)) {
    return (m_encryption.get_type() != Encryption::NONE);
  }

  /** Clear all encryption related flags */
  void clear_encrypted() {
    m_encryption.set_key(nullptr, 0);
    m_encryption.set_initial_vector(nullptr);
    m_encryption.set_key_versions_cache(nullptr);
    m_encryption.set_type(Encryption::NONE);
    m_encryption.set_encryption_rotation(Encryption_rotation::NO_ROTATION);
    m_encryption.set_key_id(0);
    m_encryption.set_tablespace_key(nullptr);
  }

  void mark_page_zip_compressed() { m_is_page_zip_compressed = true; }

  bool is_page_zip_compressed() const MY_ATTRIBUTE((warn_unused_result)) {
    return m_is_page_zip_compressed;
  }

  ulint get_zip_page_physical_size() const { return m_zip_page_physical_size; }

  void set_zip_page_physical_size(ulint zip_page_physical_size) {
    m_zip_page_physical_size = zip_page_physical_size;
  }

  /** Note that the IO is for double write buffer page write. */
  void dblwr() { m_type |= DBLWR; }

  /** @return true if the request is for a dblwr page. */
  bool is_dblwr() const MY_ATTRIBUTE((warn_unused_result)) {
    return ((m_type & DBLWR) == DBLWR);
  }

  /** @return true if punch hole is supported */
  static bool is_punch_hole_supported() {
    /* In this debugging mode, we act as if punch hole is supported,
    and then skip any calls to actually punch a hole here.
    In this way, Transparent Page Compression is still being tested. */
    DBUG_EXECUTE_IF("ignore_punch_hole", return (true););

#if defined(HAVE_FALLOC_PUNCH_HOLE_AND_KEEP_SIZE) || defined(_WIN32)
    return (true);
#else
    return (false);
#endif /* HAVE_FALLOC_PUNCH_HOLE_AND_KEEP_SIZE || _WIN32 */
  }

  /** @return string representation. */
  std::string to_string() const {
    std::ostringstream os;

    os << "bs: " << m_block_size << " flags:";

    if (m_type & READ) {
      os << " READ";
    } else if (m_type & WRITE) {
      os << " WRITE";
    } else if (m_type & DBLWR) {
      os << " DBLWR";
    }

    /** Enumerations below can be ORed to READ/WRITE above*/

    /** Data file */
    if (m_type & DATA_FILE) {
      os << " | DATA_FILE";
    }

    if (m_type & LOG) {
      os << " | LOG";
    }

    if (m_type & DISABLE_PARTIAL_IO_WARNINGS) {
      os << " | DISABLE_PARTIAL_IO_WARNINGS";
    }

    if (m_type & DO_NOT_WAKE) {
      os << " | IGNORE_MISSING";
    }

    if (m_type & PUNCH_HOLE) {
      os << " | PUNCH_HOLE";
    }

    if (m_type & NO_COMPRESSION) {
      os << " | NO_COMPRESSION";
    }

    os << ", comp: " << m_compression.to_string();
    os << ", enc: " << m_encryption.to_string(m_encryption.get_type());

    return (os.str());
  }

 private:
  /* File system best block size */
  uint32_t m_block_size;

  /** Request type bit flags */
  uint16_t m_type;

  /** Compression algorithm */
  Compression m_compression;

  /** Encryption algorithm */
  Encryption m_encryption;

  bool m_is_page_zip_compressed;

  ulint m_zip_page_physical_size;
};

/** @} */

/** Sparse file size information. */
struct os_file_size_t {
  /** Total size of file in bytes */
  os_offset_t m_total_size;

  /** If it is a sparse file then this is the number of bytes
  actually allocated for the file. */
  os_offset_t m_alloc_size;
};

/** Win NT does not allow more than 64 */
static const ulint OS_AIO_N_PENDING_IOS_PER_THREAD = 32;

/** Modes for aio operations @{ */
enum class AIO_mode : size_t {
  /** Normal asynchronous i/o not for ibuf pages or ibuf bitmap pages */
  NORMAL = 21,

  /**  Asynchronous i/o for ibuf pages or ibuf bitmap pages */
  IBUF = 22,

  /** Asynchronous i/o for the log */
  LOG = 23,

  /** Asynchronous i/o where the calling thread will itself wait for
  the i/o to complete, doing also the job of the i/o-handler thread;
  can be used for any pages, ibuf or non-ibuf.  This is used to save
  CPU time, as we can do with fewer thread switches. Plain synchronous
  I/O is not as good, because it must serialize the file seek and read
  or write, causing a bottleneck for parallelism. */
  SYNC = 24
};
/** @} */

extern ulint os_n_file_reads;
extern ulint os_n_file_writes;
extern ulint os_n_fsyncs;

/* File types for directory entry data type */

enum os_file_type_t {
  /** Get status failed. */
  OS_FILE_TYPE_FAILED,

  /** stat() failed, with ENAMETOOLONG */
  OS_FILE_TYPE_NAME_TOO_LONG,

  /** stat() failed with EACCESS */
  OS_FILE_PERMISSION_ERROR,

  /** File doesn't exist. */
  OS_FILE_TYPE_MISSING,

  /** File exists but type is unknown. */
  OS_FILE_TYPE_UNKNOWN,

  /** Ordinary file. */
  OS_FILE_TYPE_FILE,

  /** Directory. */
  OS_FILE_TYPE_DIR,

  /** Symbolic link. */
  OS_FILE_TYPE_LINK,

  /** Block device. */
  OS_FILE_TYPE_BLOCK
};

/* Maximum path string length in bytes when referring to tables with in the
'./databasename/tablename.ibd' path format; we can allocate at least 2 buffers
of this size from the thread stack; that is why this should not be made much
bigger than 4000 bytes.  The maximum path length used by any storage engine
in the server must be at least this big. */
#define OS_FILE_MAX_PATH 4000
#if (FN_REFLEN_SE < OS_FILE_MAX_PATH)
#error "(FN_REFLEN_SE < OS_FILE_MAX_PATH)"
#endif

/** Struct used in fetching information of a file in a directory */
struct os_file_stat_t {
  char name[OS_FILE_MAX_PATH]; /*!< path to a file */
  os_file_type_t type;         /*!< file type */
  os_offset_t size;            /*!< file size in bytes */
  os_offset_t alloc_size;      /*!< Allocated size for
                               sparse files in bytes */
  uint32_t block_size;         /*!< Block size to use for IO
                               in bytes*/
  time_t ctime;                /*!< creation time */
  time_t mtime;                /*!< modification time */
  time_t atime;                /*!< access time */
  bool rw_perm;                /*!< true if can be opened
                               in read-write mode. Only valid
                               if type == OS_FILE_TYPE_FILE */
};

#ifndef UNIV_HOTBACKUP
/** Create a temporary file. This function is like tmpfile(3), but
the temporary file is created in the given parameter path. If the path
is NULL then it will create the file in the MySQL server configuration
parameter (--tmpdir).
@param[in]	path	location for creating temporary file
@return temporary file handle, or NULL on error */
FILE *os_file_create_tmpfile(const char *path);
#endif /* !UNIV_HOTBACKUP */

/** This function attempts to create a directory named pathname. The new
directory gets default permissions. On Unix the permissions are
(0770 & ~umask). If the directory exists already, nothing is done and
the call succeeds, unless the fail_if_exists arguments is true.
If another error occurs, such as a permission error, this does not crash,
but reports the error and returns false.
@param[in]	pathname	directory name as null-terminated string
@param[in]	fail_if_exists	if true, pre-existing directory is treated as
                                an error.
@return true if call succeeds, false on error */
bool os_file_create_directory(const char *pathname, bool fail_if_exists);

/** Callback function type to be implemented by caller. It is called for each
entry in directory.
@param[in]	path	path to the file
@param[in]	name	name of the file */
typedef void (*os_dir_cbk_t)(const char *path, const char *name);

/** This function scans the contents of a directory and invokes the callback
for each entry.
@param[in]	path		directory name as null-terminated string
@param[in]	scan_cbk	use callback to be called for each entry
@param[in]	is_drop		attempt to drop the directory after scan
@return true if call succeeds, false on error */
bool os_file_scan_directory(const char *path, os_dir_cbk_t scan_cbk,
                            bool is_drop);

/** NOTE! Use the corresponding macro os_file_create_simple(), not directly
this function!
A simple function to open or create a file.
@param[in]	name		name of the file or path as a null-terminated
                                string
@param[in]	create_mode	create mode
@param[in]	access_type	OS_FILE_READ_ONLY or OS_FILE_READ_WRITE
@param[in]	read_only	if true, read only checks are enforced
@param[out]	success		true if succeed, false if error
@return handle to the file, not defined if error, error number
        can be retrieved with os_file_get_last_error */
os_file_t os_file_create_simple_func(const char *name, ulint create_mode,
                                     ulint access_type, bool read_only,
                                     bool *success);

/** NOTE! Use the corresponding macro
os_file_create_simple_no_error_handling(), not directly this function!
A simple function to open or create a file.
@param[in]	name		name of the file or path as a
null-terminated string
@param[in]	create_mode	create mode
@param[in]	access_type	OS_FILE_READ_ONLY, OS_FILE_READ_WRITE, or
                                OS_FILE_READ_ALLOW_DELETE; the last option
                                is used by a backup program reading the file
@param[in]	read_only	if true read only mode checks are enforced
@param[out]	success		true if succeeded
@return own: handle to the file, not defined if error, error number
        can be retrieved with os_file_get_last_error */
pfs_os_file_t os_file_create_simple_no_error_handling_func(
    const char *name, ulint create_mode, ulint access_type, bool read_only,
    bool *success) MY_ATTRIBUTE((warn_unused_result));

/** Tries to disable OS caching on an opened file descriptor.
@param[in]	fd		file descriptor to alter
@param[in]	file_name	file name, used in the diagnostic message
@param[in]	operation_name	"open" or "create"; used in the diagnostic
                                message
@return true if operation is success and false */
bool os_file_set_nocache(int fd, const char *file_name,
                         const char *operation_name);

/** Tries to disable OS caching on an opened file file.
@param[in]	file		file to alter
@param[in]	file_name	file name, used in the diagnostic message
@param[in]	name		"open" or "create"; used in the diagnostic
message
@return true if operation is success and false */
UNIV_INLINE
bool os_file_set_nocache(pfs_os_file_t file, const char *file_name,
                         const char *operation_name);

/** NOTE! Use the corresponding macro os_file_create(), not directly
this function!
Opens an existing file or creates a new.
@param[in]	name		name of the file or path as a null-terminated
                                string
@param[in]	create_mode	create mode
@param[in]	purpose		OS_FILE_AIO, if asynchronous, non-buffered I/O
                                is desired, OS_FILE_NORMAL, if any normal file;
                                NOTE that it also depends on type, os_aio_..
                                and srv_.. variables whether we really use
                                async I/O or unbuffered I/O: look in the
                                function source code for the exact rules
@param[in]	type		OS_DATA_FILE, OS_LOG_FILE etc.
@param[in]	read_only	if true read only mode checks are enforced
@param[in]	success		true if succeeded
@return own: handle to the file, not defined if error, error number
        can be retrieved with os_file_get_last_error */
pfs_os_file_t os_file_create_func(const char *name, ulint create_mode,
                                  ulint purpose, ulint type, bool read_only,
                                  bool *success)
    MY_ATTRIBUTE((warn_unused_result));

/** Deletes a file. The file has to be closed before calling this.
@param[in]	name		file path as a null-terminated string
@return true if success */
bool os_file_delete_func(const char *name);

/** Deletes a file if it exists. The file has to be closed before calling this.
@param[in]	name		file path as a null-terminated string
@param[out]	exist		indicate if file pre-exist
@return true if success */
bool os_file_delete_if_exists_func(const char *name, bool *exist);

/** NOTE! Use the corresponding macro os_file_rename(), not directly
this function!
Renames a file (can also move it to another directory). It is safest that the
file is closed before calling this function.
@param[in]	oldpath		old file path as a null-terminated string
@param[in]	newpath		new file path
@return true if success */
bool os_file_rename_func(const char *oldpath, const char *newpath);

/** NOTE! Use the corresponding macro os_file_close(), not directly
this function!
Closes a file handle. In case of error, error number can be retrieved with
os_file_get_last_error.
@param[in]	file		Handle to a file
@return true if success */
bool os_file_close_func(os_file_t file);

/** NOTE! Use the corresponding macro os_file_close_no_error_handling(), not
directly this function!
Closes a file handle.
@param[in] file		handle to a file
@return true if success */
bool os_file_close_no_error_handling_func(os_file_t file);

#ifdef UNIV_PFS_IO

/* Keys to register InnoDB I/O with performance schema */
extern mysql_pfs_key_t innodb_log_file_key;
extern mysql_pfs_key_t innodb_temp_file_key;
extern mysql_pfs_key_t innodb_dblwr_file_key;
extern mysql_pfs_key_t innodb_arch_file_key;
extern mysql_pfs_key_t innodb_clone_file_key;
extern mysql_pfs_key_t innodb_data_file_key;
extern mysql_pfs_key_t innodb_tablespace_open_file_key;
extern mysql_pfs_key_t innodb_bmp_file_key;

/* Following four macros are instumentations to register
various file I/O operations with performance schema.
1) register_pfs_file_open_begin() and register_pfs_file_open_end() are
used to register file creation, opening and closing.
2) register_pfs_file_rename_begin() and  register_pfs_file_rename_end()
are used to register file renaming.
3) register_pfs_file_io_begin() and register_pfs_file_io_end() are
used to register actual file read, write and flush
4) register_pfs_file_close_begin() and register_pfs_file_close_end()
are used to register file deletion operations*/
#define register_pfs_file_open_begin(state, locker, key, op, name, src_file, \
                                     src_line)                               \
  do {                                                                       \
    locker = PSI_FILE_CALL(get_thread_file_name_locker)(state, key.m_value,  \
                                                        op, name, &locker);  \
    if (locker != nullptr) {                                                 \
      PSI_FILE_CALL(start_file_open_wait)                                    \
      (locker, src_file, static_cast<uint>(src_line));                       \
    }                                                                        \
  } while (0)

#define register_pfs_file_open_end(locker, file, result)              \
  do {                                                                \
    if (locker != nullptr) {                                          \
      file.m_psi = PSI_FILE_CALL(end_file_open_wait)(locker, result); \
    }                                                                 \
  } while (0)

#define register_pfs_file_rename_begin(state, locker, key, op, from, to,    \
                                       src_file, src_line)                  \
  do {                                                                      \
    locker = PSI_FILE_CALL(get_thread_file_name_locker)(state, key.m_value, \
                                                        op, from, &locker); \
    if (locker != nullptr) {                                                \
      PSI_FILE_CALL(start_file_rename_wait)                                 \
      (locker, (size_t)0, from, to, src_file, static_cast<uint>(src_line)); \
    }                                                                       \
  } while (0)

#define register_pfs_file_rename_end(locker, from, to, result)       \
  do {                                                               \
    if (locker != nullptr) {                                         \
      PSI_FILE_CALL(end_file_rename_wait)(locker, from, to, result); \
    }                                                                \
  } while (0)

#define register_pfs_file_close_begin(state, locker, key, op, name, src_file, \
                                      src_line)                               \
  do {                                                                        \
    locker = PSI_FILE_CALL(get_thread_file_name_locker)(state, key.m_value,   \
                                                        op, name, &locker);   \
    if (locker != nullptr) {                                                  \
      PSI_FILE_CALL(start_file_close_wait)                                    \
      (locker, src_file, static_cast<uint>(src_line));                        \
    }                                                                         \
  } while (0)

#define register_pfs_file_close_end(locker, result)       \
  do {                                                    \
    if (locker != nullptr) {                              \
      PSI_FILE_CALL(end_file_close_wait)(locker, result); \
    }                                                     \
  } while (0)

#define register_pfs_file_io_begin(state, locker, file, count, op, src_file, \
                                   src_line)                                 \
  do {                                                                       \
    locker =                                                                 \
        PSI_FILE_CALL(get_thread_file_stream_locker)(state, file.m_psi, op); \
    if (locker != nullptr) {                                                 \
      PSI_FILE_CALL(start_file_wait)                                         \
      (locker, count, src_file, static_cast<uint>(src_line));                \
    }                                                                        \
  } while (0)

#define register_pfs_file_io_end(locker, count)    \
  do {                                             \
    if (locker != nullptr) {                       \
      PSI_FILE_CALL(end_file_wait)(locker, count); \
    }                                              \
  } while (0)

/* Following macros/functions are file I/O APIs that would be performance
schema instrumented if "UNIV_PFS_IO" is defined. They would point to
wrapper functions with performance schema instrumentation in such case.

os_file_create
os_file_create_simple
os_file_create_simple_no_error_handling
os_file_close
os_file_close_no_error_handling
os_file_rename
os_aio
os_file_read
os_file_read_no_error_handling
os_file_read_no_error_handling_int_fd
os_file_write
os_file_set_eof_at

The wrapper functions have the prefix of "innodb_". */

#define os_file_create(key, name, create, purpose, type, read_only, success) \
  pfs_os_file_create_func(key, name, create, purpose, type, read_only,       \
                          success, __FILE__, __LINE__)

#define os_file_create_simple(key, name, create, access, read_only, success) \
  pfs_os_file_create_simple_func(key, name, create, access, read_only,       \
                                 success, __FILE__, __LINE__)

#define os_file_create_simple_no_error_handling(key, name, create_mode,     \
                                                access, read_only, success) \
  pfs_os_file_create_simple_no_error_handling_func(                         \
      key, name, create_mode, access, read_only, success, __FILE__, __LINE__)

#define os_file_close_pfs(file) pfs_os_file_close_func(file, __FILE__, __LINE__)

#define os_file_close_no_error_handling_pfs(file) \
  pfs_os_file_close_no_error_handling_func(file, __FILE__, __LINE__)

#define os_aio(type, mode, name, file, buf, offset, n, read_only, message1,    \
               message2, space_id, trx, should_buffer)                         \
  pfs_os_aio_func(type, mode, name, file, buf, offset, n, read_only, message1, \
                  message2, space_id, trx, should_buffer, __FILE__, __LINE__)

#define os_file_read_pfs(type, file_name, file, buf, offset, n)         \
  pfs_os_file_read_func(type, file_name, file, buf, offset, n, nullptr, \
                        __FILE__, __LINE__)

#define os_file_read_trx_pfs(file, buf, offset, n, trx) \
  pfs_os_file_read_func(file, buf, offset, n, trx, __FILE__, __LINE__)

#define os_file_read_first_page_pfs(type, file_name, file, buf, n, exit)    \
  pfs_os_file_read_first_page_func(type, file_name, file, buf, n, __FILE__, \
                                   __LINE__, exit)

#define os_file_copy_pfs(src, src_offset, dest, dest_offset, size)          \
  pfs_os_file_copy_func(src, src_offset, dest, dest_offset, size, __FILE__, \
                        __LINE__)

#define os_file_read_no_error_handling_pfs(type, file_name, file, buf, offset, \
                                           n, o)                               \
  pfs_os_file_read_no_error_handling_func(type, file_name, file, buf, offset,  \
                                          n, o, __FILE__, __LINE__)

#define os_file_read_no_error_handling_int_fd(type, file_name, file, buf, \
                                              offset, n, o)               \
  pfs_os_file_read_no_error_handling_int_fd_func(                         \
      type, file_name, file, buf, offset, n, o, __FILE__, __LINE__)

#define os_file_write_pfs(type, name, file, buf, offset, n) \
  pfs_os_file_write_func(type, name, file, buf, offset, n, __FILE__, __LINE__)

#define os_file_write_int_fd(type, name, file, buf, offset, n)              \
  pfs_os_file_write_int_fd_func(type, name, file, buf, offset, n, __FILE__, \
                                __LINE__)

#define os_file_flush_pfs(file) pfs_os_file_flush_func(file, __FILE__, __LINE__)

#define os_file_rename(key, oldpath, newpath) \
  pfs_os_file_rename_func(key, oldpath, newpath, __FILE__, __LINE__)

#define os_file_delete(key, name) \
  pfs_os_file_delete_func(key, name, __FILE__, __LINE__)

#define os_file_delete_if_exists(key, name, exist) \
  pfs_os_file_delete_if_exists_func(key, name, exist, __FILE__, __LINE__)

#define os_file_set_eof_at_pfs(file, new_len) \
  pfs_os_file_set_eof_at_func(file, new_len, __FILE__, __LINE__)

/** NOTE! Please use the corresponding macro os_file_create_simple(),
not directly this function!
A performance schema instrumented wrapper function for
os_file_create_simple() which opens or creates a file.
@param[in]	key		Performance Schema Key
@param[in]	name		name of the file or path as a null-terminated
                                string
@param[in]	create_mode	create mode
@param[in]	access_type	OS_FILE_READ_ONLY or OS_FILE_READ_WRITE
@param[in]	read_only	if true read only mode checks are enforced
@param[out]	success		true if succeeded
@param[in]	src_file	file name where func invoked
@param[in]	src_line	line where the func invoked
@return own: handle to the file, not defined if error, error number
        can be retrieved with os_file_get_last_error */
UNIV_INLINE
pfs_os_file_t pfs_os_file_create_simple_func(
    mysql_pfs_key_t key, const char *name, ulint create_mode, ulint access_type,
    bool read_only, bool *success, const char *src_file, uint src_line)
    MY_ATTRIBUTE((warn_unused_result));

/** NOTE! Please use the corresponding macro
os_file_create_simple_no_error_handling(), not directly this function!
A performance schema instrumented wrapper function for
os_file_create_simple_no_error_handling(). Add instrumentation to
monitor file creation/open.
@param[in]	key		Performance Schema Key
@param[in]	name		name of the file or path as a null-terminated
                                string
@param[in]	create_mode	create mode
@param[in]	access_type	OS_FILE_READ_ONLY, OS_FILE_READ_WRITE, or
                                OS_FILE_READ_ALLOW_DELETE; the last option is
                                used by a backup program reading the file
@param[in]	read_only	if true read only mode checks are enforced
@param[out]	success		true if succeeded
@param[in]	src_file	file name where func invoked
@param[in]	src_line	line where the func invoked
@return own: handle to the file, not defined if error, error number
        can be retrieved with os_file_get_last_error */
UNIV_INLINE
pfs_os_file_t pfs_os_file_create_simple_no_error_handling_func(
    mysql_pfs_key_t key, const char *name, ulint create_mode, ulint access_type,
    bool read_only, bool *success, const char *src_file, uint src_line)
    MY_ATTRIBUTE((warn_unused_result));

/** NOTE! Please use the corresponding macro os_file_create(), not directly
this function!
A performance schema wrapper function for os_file_create().
Add instrumentation to monitor file creation/open.
@param[in]	key		Performance Schema Key
@param[in]	name		name of the file or path as a null-terminated
                                string
@param[in]	create_mode	create mode
@param[in]	purpose		OS_FILE_AIO, if asynchronous, non-buffered I/O
                                is desired, OS_FILE_NORMAL, if any normal file;
                                NOTE that it also depends on type, os_aio_..
                                and srv_.. variables whether we really use
                                async I/O or unbuffered I/O: look in the
                                function source code for the exact rules
@param[in]	type		OS_DATA_FILE or OS_LOG_FILE
@param[in]	read_only	if true read only mode checks are enforced
@param[out]	success		true if succeeded
@param[in]	src_file	file name where func invoked
@param[in]	src_line	line where the func invoked
@return own: handle to the file, not defined if error, error number
        can be retrieved with os_file_get_last_error */
UNIV_INLINE
pfs_os_file_t pfs_os_file_create_func(mysql_pfs_key_t key, const char *name,
                                      ulint create_mode, ulint purpose,
                                      ulint type, bool read_only, bool *success,
                                      const char *src_file, uint src_line)
    MY_ATTRIBUTE((warn_unused_result));

/** NOTE! Please use the corresponding macro os_file_close(), not directly
this function!
A performance schema instrumented wrapper function for os_file_close().
@param[in]	file		handle to a file
@param[in]	src_file	file name where func invoked
@param[in]	src_line	line where the func invoked
@return true if success */
UNIV_INLINE
bool pfs_os_file_close_func(pfs_os_file_t file, const char *src_file,
                            uint src_line);

/** NOTE! Use the corresponding macro os_file_close_no_error_handling(), not
directly this function!
Closes a file handle.
@param[in] file		handle to a file
@param[in]	src_file	file name where func invoked
@param[in]	src_line	line where the func invoked
@return true if success */
UNIV_INLINE
bool pfs_os_file_close_no_error_handling_func(pfs_os_file_t file,
                                              const char *src_file,
                                              ulint src_line);

/** NOTE! Please use the corresponding macro os_file_read(), not directly
this function!
This is the performance schema instrumented wrapper function for
os_file_read() which requests a synchronous read operation.
@param[in, out]	type		IO request context
@param[in]  file_name file name
@param[in]	file		Open file handle
@param[out]	buf		buffer where to read
@param[in]	offset		file offset where to read
@param[in]	n		number of bytes to read
@param[in]	src_file	file name where func invoked
@param[in]	src_line	line where the func invoked
@return DB_SUCCESS if request was successful */
UNIV_INLINE
dberr_t pfs_os_file_read_func(IORequest &type, const char *file_name,
                              pfs_os_file_t file, void *buf, os_offset_t offset,
                              ulint n, trx_t *trx, const char *src_file,
                              uint src_line);

/** NOTE! Please use the corresponding macro os_file_read_first_page(),
not directly this function!
This is the performance schema instrumented wrapper function for
os_file_read_first_page() which requests a synchronous read operation
of page 0 of IBD file
@param[in, out]	type		IO request context
@param[in]  file_name file name
@param[in]	file		Open file handle
@param[out]	buf		buffer where to read
@param[in]	n		number of bytes to read
@param[in]	src_file	file name where func invoked
@param[in]	src_line	line where the func invoked
@param[in]	exit_on_err	if true then exit on error
@return DB_SUCCESS if request was successful */
UNIV_INLINE
dberr_t pfs_os_file_read_first_page_func(IORequest &type, const char *file_name,
                                         pfs_os_file_t file, void *buf, ulint n,
                                         const char *src_file, uint src_line,
                                         bool exit_on_err);

/** copy data from one file to another file. Data is read/written
at current file offset.
@param[in]	src		file handle to copy from
@param[in]	src_offset	offset to copy from
@param[in]	dest		file handle to copy to
@param[in]	dest_offset	offset to copy to
@param[in]	size		number of bytes to copy
@param[in]	src_file	file name where func invoked
@param[in]	src_line	line where the func invoked
@return DB_SUCCESS if successful */
UNIV_INLINE
dberr_t pfs_os_file_copy_func(pfs_os_file_t src, os_offset_t src_offset,
                              pfs_os_file_t dest, os_offset_t dest_offset,
                              uint size, const char *src_file, uint src_line);

/** NOTE! Please use the corresponding macro os_file_read_no_error_handling(),
not directly this function!
This is the performance schema instrumented wrapper function for
os_file_read_no_error_handling_func() which requests a synchronous
read operation.
@param[in, out]	type		IO request context
@param[in]  file_name file name
@param[in]	file		Open file handle
@param[out]	buf		buffer where to read
@param[in]	offset		file offset where to read
@param[in]	n		number of bytes to read
@param[out]	o		number of bytes actually read
@param[in]	src_file	file name where func invoked
@param[in]	src_line	line where the func invoked
@return DB_SUCCESS if request was successful */
UNIV_INLINE
dberr_t pfs_os_file_read_no_error_handling_func(
    IORequest &type, const char *file_name, pfs_os_file_t file, void *buf,
    os_offset_t offset, ulint n, ulint *o, const char *src_file, uint src_line);

/** NOTE! Please use the corresponding macro
os_file_read_no_error_handling_int_fd(), not directly this function!
This is the performance schema instrumented wrapper function for
os_file_read_no_error_handling_int_fd_func() which requests a
synchronous read operation on files with int type descriptors.
@param[in, out] type            IO request context
@param[in]      file_name       file name
@param[in]      file            Open file handle
@param[out]     buf             buffer where to read
@param[in]      offset          file offset where to read
@param[in]      n               number of bytes to read
@param[out]     o               number of bytes actually read
@param[in]      src_file        file name where func invoked
@param[in]      src_line        line where the func invoked
@return DB_SUCCESS if request was successful */

UNIV_INLINE
dberr_t pfs_os_file_read_no_error_handling_int_fd_func(
    IORequest &type, const char *file_name, int file, void *buf,
    os_offset_t offset, ulint n, ulint *o, const char *src_file,
    ulint src_line);

/** NOTE! Please use the corresponding macro os_aio(), not directly this
function!
Performance schema wrapper function of os_aio() which requests
an asynchronous I/O operation.
@param[in]	type		IO request context
@param[in]	mode		IO mode
@param[in]	name		Name of the file or path as NUL terminated
                                string
@param[in]	file		Open file handle
@param[out]	buf		buffer where to read
@param[in]	offset		file offset where to read
@param[in]	n		number of bytes to read
@param[in]	read_only	if true read only mode checks are enforced
@param[in,out]	m1		Message for the AIO handler, (can be used to
                                identify a completed AIO operation); ignored
                                if mode is OS_AIO_SYNC
@param[in,out]	m2		message for the AIO handler (can be used to
                                identify a completed AIO operation); ignored
                                if mode is OS_AIO_SYNC
@param[in]	should_buffer	Whether to buffer an aio request.
                                AIO read ahead uses this. If you plan to
                                use this parameter, make sure you remember to
                                call os_aio_dispatch_read_array_submit()
                                when you're ready to commit all your
                                requests.
@param[in]	src_file	file name where func invoked
@param[in]	src_line	line where the func invoked
@return DB_SUCCESS if request was queued successfully, false if fail */
UNIV_INLINE
dberr_t pfs_os_aio_func(IORequest &type, AIO_mode mode, const char *name,
                        pfs_os_file_t file, void *buf, os_offset_t offset,
                        ulint n, bool read_only, fil_node_t *m1, void *m2,
                        space_id_t space_id, trx_t *trx, bool should_buffer,
                        const char *src_file, uint src_line);

/** NOTE! Please use the corresponding macro os_file_write(), not directly
this function!
This is the performance schema instrumented wrapper function for
os_file_write() which requests a synchronous write operation.
@param[in, out]	type		IO request context
@param[in]	name		Name of the file or path as NUL terminated
                                string
@param[in]	file		Open file handle
@param[out]	buf		buffer where to read
@param[in]	offset		file offset where to read
@param[in]	n		number of bytes to read
@param[in]	src_file	file name where func invoked
@param[in]	src_line	line where the func invoked
@return DB_SUCCESS if request was successful */
UNIV_INLINE
dberr_t pfs_os_file_write_func(IORequest &type, const char *name,
                               pfs_os_file_t file, const void *buf,
                               os_offset_t offset, ulint n,
                               const char *src_file, uint src_line);

/** NOTE! Please use the corresponding macro os_file_write(), not
directly this function!
This is the performance schema instrumented wrapper function for
os_file_write() which requests a synchronous write operation
on files with int type descriptors.
@param[in, out] type            IO request context
@param[in]      name            Name of the file or path as NUL terminated
                                string
@param[in]      file            Open file handle
@param[out]     buf             buffer where to read
@param[in]      offset          file offset where to read
@param[in]      n		number of bytes to read
@param[in]      src_file        file name where func invoked
@param[in]      src_line        line where the func invoked
@return DB_SUCCESS if request was successful */
UNIV_INLINE
dberr_t pfs_os_file_write_int_fd_func(IORequest &type, const char *name,
                                      int file, const void *buf,
                                      os_offset_t offset, ulint n,
                                      const char *src_file, ulint src_line);

/** NOTE! Please use the corresponding macro os_file_flush(), not directly
this function!
This is the performance schema instrumented wrapper function for
os_file_flush() which flushes the write buffers of a given file to the disk.
Flushes the write buffers of a given file to the disk.
@param[in]	file		Open file handle
@param[in]	src_file	file name where func invoked
@param[in]	src_line	line where the func invoked
@return true if success */
UNIV_INLINE
bool pfs_os_file_flush_func(pfs_os_file_t file, const char *src_file,
                            uint src_line);

/** NOTE! Please use the corresponding macro os_file_rename(), not directly
this function!
This is the performance schema instrumented wrapper function for
os_file_rename()
@param[in]	key		Performance Schema Key
@param[in]	oldpath		old file path as a null-terminated string
@param[in]	newpath		new file path
@param[in]	src_file	file name where func invoked
@param[in]	src_line	line where the func invoked
@return true if success */
UNIV_INLINE
bool pfs_os_file_rename_func(mysql_pfs_key_t key, const char *oldpath,
                             const char *newpath, const char *src_file,
                             uint src_line);

/**
NOTE! Please use the corresponding macro os_file_delete(), not directly
this function!
This is the performance schema instrumented wrapper function for
os_file_delete()
@param[in]	key		Performance Schema Key
@param[in]	name		old file path as a null-terminated string
@param[in]	src_file	file name where func invoked
@param[in]	src_line	line where the func invoked
@return true if success */
UNIV_INLINE
bool pfs_os_file_delete_func(mysql_pfs_key_t key, const char *name,
                             const char *src_file, uint src_line);

/**
NOTE! Please use the corresponding macro os_file_delete_if_exists(), not
directly this function!
This is the performance schema instrumented wrapper function for
os_file_delete_if_exists()
@param[in]	key		Performance Schema Key
@param[in]	name		old file path as a null-terminated string
@param[in]	exist		indicate if file pre-exist
@param[in]	src_file	file name where func invoked
@param[in]	src_line	line where the func invoked
@return true if success */
UNIV_INLINE
bool pfs_os_file_delete_if_exists_func(mysql_pfs_key_t key, const char *name,
                                       bool *exist, const char *src_file,
                                       uint src_line);

/** NOTE! Use the corresponding macro os_file_flush(), not directly this
function!
Truncates a file at the specified position.
@param[in]	file	file to truncate
@param[in]	new_len	new file length
@param[in]	src_file	file name where func invoked
@param[in]	src_line	line where the func invoked
@return true if success */
UNIV_INLINE
bool pfs_os_file_set_eof_at_func(pfs_os_file_t file, ib_uint64_t new_len,
                                 const char *src_file, ulint src_line);

#else /* UNIV_PFS_IO */

/* If UNIV_PFS_IO is not defined, these I/O APIs point
to original un-instrumented file I/O APIs */
#define os_file_create(key, name, create, purpose, type, read_only, success) \
  os_file_create_func(name, create, purpose, type, read_only, success)

#define os_file_create_simple(key, name, create_mode, access, read_only, \
                              success)                                   \
  os_file_create_simple_func(name, create_mode, access, read_only, success)

#define os_file_create_simple_no_error_handling(key, name, create_mode,     \
                                                access, read_only, success) \
  os_file_create_simple_no_error_handling_func(name, create_mode, access,   \
                                               read_only, success)

#define os_file_close_pfs(file) os_file_close_func(file)

#define os_file_close_no_error_handling_pfs(file) \
  os_file_close_no_error_handling_func(file)

#define os_aio(type, mode, name, file, buf, offset, n, read_only, message1, \
               message2, space_id, trx, should_buffer)                      \
  os_aio_func(type, mode, name, file, buf, offset, n, read_only, message1,  \
              message2, space_id, trx, should_buffer)

#define os_file_read_pfs(type, file_name, file, buf, offset, n) \
  os_file_read_func(type, file_name, file, buf, offset, n)

#define os_file_read_first_page_pfs(type, file_name, file, buf, n, exit) \
  os_file_read_first_page_func(type, file_name, file, buf, n, exit)

#define os_file_copy_pfs(src, src_offset, dest, dest_offset, size) \
  os_file_copy_func(src, src_offset, dest, dest_offset, size)

#define os_file_read_no_error_handling_pfs(type, file_name, file, buf, offset, \
                                           n, o)                               \
  os_file_read_no_error_handling_func(type, file_name, file, buf, offset, n, o)

#define os_file_read_no_error_handling_int_fd(type, file_name, file, buf,     \
                                              offset, n, o)                   \
  os_file_read_no_error_handling_func(type, file_name, OS_FILE_FROM_FD(file), \
                                      buf, offset, n, o)

#define os_file_read_trx_pfs(file, buf, offset, n, trx) \
  os_file_read_func(file, buf, offset, n, trx)

#define os_file_write_pfs(type, name, file, buf, offset, n) \
  os_file_write_func(type, name, file, buf, offset, n)

#define os_file_write_int_fd(type, name, file, buf, offset, n) \
  os_file_write_func(type, name, OS_FILE_FROM_FD(file), buf, offset, n)

#define os_file_flush_pfs(file) os_file_flush_func(file)

#define os_file_rename(key, oldpath, newpath) \
  os_file_rename_func(oldpath, newpath)

#define os_file_delete(key, name) os_file_delete_func(name)

#define os_file_delete_if_exists(key, name, exist) \
  os_file_delete_if_exists_func(name, exist)

#define os_file_set_eof_at_pfs(file, new_len) \
  os_file_set_eof_at_func(file, new_len)

#endif /* UNIV_PFS_IO */

#ifdef UNIV_PFS_IO
#define os_file_close(file) os_file_close_pfs(file)
#else
#define os_file_close(file) os_file_close_pfs((file).m_file)
#endif

#ifdef UNIV_PFS_IO
#define os_file_close_no_error_handling(file) \
  os_file_close_no_error_handling_pfs(file)
#else
#define os_file_close_no_error_handling(file) \
  os_file_close_no_error_handling_pfs((file).m_file)
#endif

#ifdef UNIV_PFS_IO
#define os_file_read(type, file_name, file, buf, offset, n) \
  os_file_read_pfs(type, file_name, file, buf, offset, n)
#else
#define os_file_read(type, file_name, file, buf, offset, n) \
  os_file_read_pfs(type, file_name, file.m_file, buf, offset, n)
#endif

#ifdef UNIV_PFS_IO
#define os_file_read_first_page(type, file_name, file, buf, n) \
  os_file_read_first_page_pfs(type, file_name, file, buf, n, true)
#else
#define os_file_read_first_page(type, file_name, file, buf, n) \
  os_file_read_first_page_pfs(type, file_name, file.m_file, buf, n, true)
#endif

#ifdef UNIV_PFS_IO
#define os_file_read_first_page_noexit(type, file_name, file, buf, n) \
  os_file_read_first_page_pfs(type, file_name, file, buf, n, false)
#else
#define os_file_read_first_page_noexit(type, file_name, file, buf, n) \
  os_file_read_first_page_pfs(type, file_name, file.m_file, buf, n, false)
#endif

#ifdef UNIV_PFS_IO
#define os_file_flush(file) os_file_flush_pfs(file)
#else
#define os_file_flush(file) os_file_flush_pfs(file.m_file)
#endif

#ifdef UNIV_PFS_IO
#define os_file_write(type, name, file, buf, offset, n) \
  os_file_write_pfs(type, name, file, buf, offset, n)
#else
#define os_file_write(type, name, file, buf, offset, n) \
  os_file_write_pfs(type, name, file.m_file, buf, offset, n)
#endif

#ifdef UNIV_PFS_IO
#define os_file_copy(src, src_offset, dest, dest_offset, size) \
  os_file_copy_pfs(src, src_offset, dest, dest_offset, size)
#else
#define os_file_copy(src, src_offset, dest, dest_offset, size) \
  os_file_copy_pfs(src.m_file, src_offset, dest.m_file, dest_offset, size)
#endif

#ifdef UNIV_PFS_IO
#define os_file_read_no_error_handling(type, file_name, file, buf, offset, n, \
                                       o)                                     \
  os_file_read_no_error_handling_pfs(type, file_name, file, buf, offset, n, o)
#else
#define os_file_read_no_error_handling(type, file_name, file, buf, offset, n, \
                                       o)                                     \
  os_file_read_no_error_handling_pfs(type, file_name, file.m_file, buf,       \
                                     offset, n, o)
#endif

#ifdef UNIV_PFS_IO
#define os_file_set_eof_at(file, new_len) os_file_set_eof_at_pfs(file, new_len)
#else
#define os_file_set_eof_at(file, new_len) \
  os_file_set_eof_at_pfs(file.m_file, new_len)
#endif

/** Announces an intention to access file data in a specific pattern in the
future.
@param[in, own]	file	handle to a file
@param[in]	offset	file region offset
@param[in]	len	file region length
@param[in]	advice	advice for access pattern
@return true if success */
bool os_file_advise(pfs_os_file_t file, os_offset_t offset, os_offset_t len,
                    ulint advice);

/** Gets a file size.
@param[in]	filename	Full path to the filename to check
@return file size if OK, else set m_total_size to ~0 and m_alloc_size to
        errno */
os_file_size_t os_file_get_size(const char *filename)
    MY_ATTRIBUTE((warn_unused_result));

/** Gets a file size.
@param[in]	file		Handle to a file
@return file size, or (os_offset_t) -1 on failure */
os_offset_t os_file_get_size(pfs_os_file_t file)
    MY_ATTRIBUTE((warn_unused_result));

/** Allocate a block to file using fallocate from the given offset if
fallocate is supported. Falls back to the old slower method of writing
zeros otherwise.
@param[in]	name		name of the file
@param[in]	file		handle to the file
@param[in]	offset		file offset
@param[in]	size		file size
@param[in]	read_only	enable read-only checks if true
@param[in]	flush		flush file content to disk
@return true if success */
bool os_file_set_size_fast(const char *name, pfs_os_file_t file,
                           os_offset_t offset, os_offset_t size, bool read_only,
                           bool flush) MY_ATTRIBUTE((warn_unused_result));

/** Write the specified number of zeros to a file from specific offset.
@param[in]	name		name of the file or path as a null-terminated
                                string
@param[in]	file		handle to a file
@param[in]	offset		file offset
@param[in]	size		file size
@param[in]	read_only	enable read-only checks if true
@param[in]	flush		flush file content to disk
@return true if success */
bool os_file_set_size(const char *name, pfs_os_file_t file, os_offset_t offset,
                      os_offset_t size, bool read_only, bool flush)
    MY_ATTRIBUTE((warn_unused_result));

/** Truncates a file at its current position.
@param[in,out]	file	file to be truncated
@return true if success */
bool os_file_set_eof(FILE *file); /*!< in: file to be truncated */

/** Truncates a file to a specified size in bytes.
Do nothing if the size to preserve is greater or equal to the current
size of the file.
@param[in]	pathname	file path
@param[in]	file		file to be truncated
@param[in]	size		size to preserve in bytes
@return true if success */
bool os_file_truncate(const char *pathname, pfs_os_file_t file,
                      os_offset_t size);

/** Set read/write position of a file handle to specific offset.
@param[in]	pathname	file path
@param[in]	file		file handle
@param[in]	offset		read/write offset
@return true if success */
bool os_file_seek(const char *pathname, os_file_t file, os_offset_t offset);

/** NOTE! Use the corresponding macro os_file_flush(), not directly this
function!
Truncates a file at the specified position.
@param[in]	file	file to truncate
@param[in]	new_len	new file length
@return true if success */
bool os_file_set_eof_at_func(os_file_t file, ib_uint64_t new_len);

/** NOTE! Use the corresponding macro os_file_flush(), not directly this
function!
Flushes the write buffers of a given file to the disk.
@param[in]	file		handle to a file
@return true if success */
bool os_file_flush_func(os_file_t file);

/** Retrieves the last error number if an error occurs in a file io function.
The number should be retrieved before any other OS calls (because they may
overwrite the error number). If the number is not known to this program,
the OS error number + 100 is returned.
@param[in]	report_all_errors	true if we want an error message printed
                                        for all errors
@return error number, or OS error number + 100 */
ulint os_file_get_last_error(bool report_all_errors);

/** NOTE! Use the corresponding macro os_file_read_first_page(), not directly
this function!
Requests a synchronous read operation of page 0 of IBD file.
@param[in]	type		IO request context
@param[in]  file_name file name
@param[in]	file		Open file handle
@param[out]	buf		buffer where to read
@param[in]	offset		file offset where to read
@param[in]	n		number of bytes to read
@return DB_SUCCESS if request was successful, DB_IO_ERROR on failure */
dberr_t os_file_read_func(IORequest &type, const char *file_name,
                          os_file_t file, void *buf, os_offset_t offset,
                          ulint n, trx_t *trx)
    MY_ATTRIBUTE((warn_unused_result));

/** NOTE! Use the corresponding macro os_file_read_first_page(),
not directly this function!
Requests a synchronous read operation of page 0 of IBD file
@param[in]	type		IO request context
@param[in]  file_name file name
@param[in]	file		Open file handle
@param[out]	buf		buffer where to read
@param[in]	n		number of bytes to read
@param[in]	exit_on_err	if true then exit on error
@return DB_SUCCESS if request was successful, DB_IO_ERROR on failure */
dberr_t os_file_read_first_page_func(IORequest &type, const char *file_name,
                                     os_file_t file, void *buf, ulint n,
                                     bool exit_on_err)
    MY_ATTRIBUTE((warn_unused_result));

/** Copy data from one file to another file. Data is read/written
at current file offset.
@param[in]	src_file	file handle to copy from
@param[in]	src_offset	offset to copy from
@param[in]	dest_file	file handle to copy to
@param[in]	dest_offset	offset to copy to
@param[in]	size		number of bytes to copy
@return DB_SUCCESS if successful */
dberr_t os_file_copy_func(os_file_t src_file, os_offset_t src_offset,
                          os_file_t dest_file, os_offset_t dest_offset,
                          uint size) MY_ATTRIBUTE((warn_unused_result));

/** Rewind file to its start, read at most size - 1 bytes from it to str, and
NUL-terminate str. All errors are silently ignored. This function is
mostly meant to be used with temporary files.
@param[in,out]	file		File to read from
@param[in,out]	str		Buffer where to read
@param[in]	size		Size of buffer */
void os_file_read_string(FILE *file, char *str, ulint size);

/** NOTE! Use the corresponding macro os_file_read_no_error_handling(),
not directly this function!
Requests a synchronous positioned read operation. This function does not do
any error handling. In case of error it returns FALSE.
@param[in]	type		IO request context
@param[in]  file_name file name
@param[in]	file		Open file handle
@param[out]	buf		buffer where to read
@param[in]	offset		file offset where to read
@param[in]	n		number of bytes to read
@param[out]	o		number of bytes actually read
@return DB_SUCCESS or error code */
dberr_t os_file_read_no_error_handling_func(
    IORequest &type, const char *file_name, os_file_t file, void *buf,
    os_offset_t offset, ulint n, ulint *o) MY_ATTRIBUTE((warn_unused_result));

/** NOTE! Use the corresponding macro os_file_write(), not directly this
function!
Requests a synchronous write operation.
@param[in,out]	type		IO request context
@param[in]	name		name of the file or path as a null-terminated
                                string
@param[in]	file		Open file handle
@param[out]	buf		buffer where to read
@param[in]	offset		file offset where to read
@param[in]	n		number of bytes to read
@return DB_SUCCESS if request was successful */
dberr_t os_file_write_func(IORequest &type, const char *name, os_file_t file,
                           const void *buf, os_offset_t offset, ulint n)
    MY_ATTRIBUTE((warn_unused_result));

/** Check the existence and type of a given path.
@param[in]   path    pathname of the file
@param[out]  exists  true if file exists
@param[out]  type    type of the file (if it exists)
@return true if call succeeded */
bool os_file_status(const char *path, bool *exists, os_file_type_t *type);

/** Check the existence and usefulness of a given path.
@param[in]  path  path name
@retval true if the path exists and can be used
@retval false if the path does not exist or if the path is
unuseable to get to a possibly existing file or directory. */
bool os_file_exists(const char *path);

/** Create all missing subdirectories along the given path.
@return DB_SUCCESS if OK, otherwise error code. */
dberr_t os_file_create_subdirs_if_needed(const char *path);

#ifdef UNIV_ENABLE_UNIT_TEST_GET_PARENT_DIR
/* Test the function os_file_get_parent_dir. */
void unit_test_os_file_get_parent_dir();
#endif /* UNIV_ENABLE_UNIT_TEST_GET_PARENT_DIR */

#ifdef UNIV_HOTBACKUP
/** Deallocates the "Blocks" in block_cache */
void meb_free_block_cache();
#endif /* UNIV_HOTBACKUP */

/** Creates and initializes block_cache. Creates array of MAX_BLOCKS
and allocates the memory in each block to hold BUFFER_BLOCK_SIZE
of data.

This function is called by InnoDB during srv_start().
It is also called by MEB while applying the redo logs on TDE tablespaces,
the "Blocks" allocated in this block_cache are used to hold the decrypted
page data. */
void os_create_block_cache();

/** Initializes the asynchronous io system. Creates one array each for ibuf
and log i/o. Also creates one array each for read and write where each
array is divided logically into n_readers and n_writers
respectively. The caller must create an i/o handler thread for each
segment in these arrays. This function also creates the sync array.
No i/o handler thread needs to be created for that
@param[in]	n_readers	number of reader threads
@param[in]	n_writers	number of writer threads
@param[in]	n_slots_sync	number of dblwr slots in the sync aio array */
bool os_aio_init(ulint n_readers, ulint n_writers, ulint n_slots_sync);

/**
Frees the asynchronous io system. */
void os_aio_free();

/**
NOTE! Use the corresponding macro os_aio(), not directly this function!
Requests an asynchronous i/o operation.
@param[in]	type		IO request context
@param[in]	aio_mode	IO mode
@param[in]	name		Name of the file or path as NUL terminated
                                string
@param[in]	file		Open file handle
@param[out]	buf		buffer where to read
@param[in]	offset		file offset where to read
@param[in]	n		number of bytes to read
@param[in]	read_only	if true read only mode checks are enforced
@param[in,out]	m1		Message for the AIO handler, (can be used to
                                identify a completed AIO operation); ignored
                                if mode is OS_AIO_SYNC
@param[in,out]	m2		message for the AIO handler (can be used to
                                identify a completed AIO operation); ignored
                                if mode is OS_AIO_SYNC
@param[in]	should_buffer	Whether to buffer an aio request.
                                AIO read ahead uses this. If you plan to
                                use this parameter, make sure you remember to
                                call os_aio_dispatch_read_array_submit()
                                when you're ready to commit all your
                                requests.
@return DB_SUCCESS or error code */
dberr_t os_aio_func(IORequest &type, AIO_mode aio_mode, const char *name,
                    pfs_os_file_t file, void *buf, os_offset_t offset, ulint n,
                    bool read_only, fil_node_t *m1, void *m2,
                    space_id_t space_id, trx_t *trx, bool should_buffer);

/** Wakes up all async i/o threads so that they know to exit themselves in
shutdown. */
void os_aio_wake_all_threads_at_shutdown();

/** Waits until there are no pending writes in os_aio_write_array. There can
be other, synchronous, pending writes. */
void os_aio_wait_until_no_pending_writes();

/** Wakes up simulated aio i/o-handler threads if they have something to do. */
void os_aio_simulated_wake_handler_threads();

/** This function can be called if one wants to post a batch of reads and
prefers an i/o-handler thread to handle them all at once later. You must
call os_aio_simulated_wake_handler_threads later to ensure the threads
are not left sleeping! */
void os_aio_simulated_put_read_threads_to_sleep();

/** Waits for an AIO operation to complete. This function is used to wait the
for completed requests. The AIO array of pending requests is divided
into segments. The thread specifies which segment or slot it wants to wait
for. NOTE: this function will also take care of freeing the AIO slot,
therefore no other thread is allowed to do the freeing!
@param[in]	segment		The number of the segment in the AIO arrays to
                                wait for; segment 0 is the ibuf I/O thread,
                                segment 1 the log I/O thread, then follow the
                                non-ibuf read threads, and as the last are the
                                non-ibuf write threads; if this is
                                ULINT_UNDEFINED, then it means that sync AIO
                                is used, and this parameter is ignored
@param[out]	m1		the messages passed with the AIO request; note
                                that also in the case where the AIO operation
                                failed, these output parameters are valid and
                                can be used to restart the operation,
                                for example
@param[out]	m2		callback message
@param[out]	request		OS_FILE_WRITE or ..._READ
@return DB_SUCCESS or error code */
dberr_t os_aio_handler(ulint segment, fil_node_t **m1, void **m2,
                       IORequest *request);

/** Prints info of the aio arrays.
@param[in,out]	file		file where to print */
void os_aio_print(FILE *file);

/** Refreshes the statistics used to print per-second averages. */
void os_aio_refresh_stats();

/** Checks that all slots in the system have been freed, that is, there are
no pending io operations. */
bool os_aio_all_slots_free();

#ifdef UNIV_DEBUG

/** Prints all pending IO
@param[in]	file		File where to print */
void os_aio_print_pending_io(FILE *file);

#endif /* UNIV_DEBUG */

/** Get available free space on disk
@param[in]	path		pathname of a directory or file in disk
@param[out]	free_space	free space available in bytes
@return DB_SUCCESS if all OK */
dberr_t os_get_free_space(const char *path, uint64_t &free_space);

/** This function returns information about the specified file
@param[in]	path		pathname of the file
@param[out]	stat_info	information of a file in a directory
@param[in]	check_rw_perm	for testing whether the file can be opened
                                in RW mode
@param[in]	read_only	true if file is opened in read-only mode
@return DB_SUCCESS if all OK */
dberr_t os_file_get_status(const char *path, os_file_stat_t *stat_info,
                           bool check_rw_perm, bool read_only);

#ifndef UNIV_HOTBACKUP

/** return any of the tmpdir path */
char *innobase_mysql_tmpdir();

/** Creates a temporary file in the location specified by the parameter
path. If the path is NULL, then it will be created in --tmpdir.
@param[in]	path	location for creating temporary file
@return temporary file descriptor, or < 0 on error */
int innobase_mysql_tmpfile(const char *path);

#endif /* !UNIV_HOTBACKUP */

/** If it is a compressed page return the compressed page data + footer size
@param[in]	buf		Buffer to check, must include header + 10 bytes
@return ULINT_UNDEFINED if the page is not a compressed page or length
        of the compressed data (including footer) if it is a compressed page */
ulint os_file_compressed_page_size(const byte *buf);

/** If it is a compressed page return the original page data + footer size
@param[in]	buf		Buffer to check, must include header + 10 bytes
@return ULINT_UNDEFINED if the page is not a compressed page or length
        of the original data + footer if it is a compressed page */
ulint os_file_original_page_size(const byte *buf);

/** Set the file create umask
@param[in]	umask		The umask to use for file creation. */
void os_file_set_umask(ulint umask);

/** Get the file create umask
@return the umask to use for file creation. */
ulint os_file_get_umask();

/** Free storage space associated with a section of the file.
@param[in]	fh		Open file handle
@param[in]	off		Starting offset (SEEK_SET)
@param[in]	len		Size of the hole
@return DB_SUCCESS or error code */
dberr_t os_file_punch_hole(os_file_t fh, os_offset_t off, os_offset_t len)
    MY_ATTRIBUTE((warn_unused_result));

/** Check if the file system supports sparse files.

Warning: On POSIX systems we try and punch a hole from offset 0 to
the system configured page size. This should only be called on an empty
file.

Note: On Windows we use the name and on Unices we use the file handle.

@param[in]	path	File name
@param[in]	fh	File handle for the file - if opened
@return true if the file system supports sparse files */
bool os_is_sparse_file_supported(const char *path, pfs_os_file_t fh)
    MY_ATTRIBUTE((warn_unused_result));

/** Decompress the page data contents. Page type must be FIL_PAGE_COMPRESSED, if
not then the source contents are left unchanged and DB_SUCCESS is returned.
@param[in]	dblwr_read	true of double write recovery in progress
@param[in,out]	src		Data read from disk, decompressed data will be
                                copied to this page
@param[in,out]	dst		Scratch area to use for decompression or
                                nullptr.
@param[in]	dst_len		If dst is valid, then size of the scratch area
                                in bytes
@return DB_SUCCESS or error code */
dberr_t os_file_decompress_page(bool dblwr_read, byte *src, byte *dst,
                                ulint dst_len)
    MY_ATTRIBUTE((warn_unused_result));

/** Compress a data page
@param[in]	compression	Compression algorithm
@param[in]	block_size	File system block size
@param[in]	src		Source contents to compress
@param[in]	src_len		Length in bytes of the source
@param[out]	dst		Compressed page contents
@param[out]	dst_len		Length in bytes of dst contents
@return buffer data, dst_len will have the length of the data */
byte *os_file_compress_page(Compression compression, ulint block_size,
                            byte *src, ulint src_len, byte *dst, ulint *dst_len,
                            bool will_be_encrypted_with_keyring);

/** Determine if O_DIRECT is supported.
@retval	true	if O_DIRECT is supported.
@retval	false	if O_DIRECT is not supported. */
bool os_is_o_direct_supported() MY_ATTRIBUTE((warn_unused_result));

/** Fill the pages with NULs
@param[in] file		File handle
@param[in] name		File name
@param[in] page_size	physical page size
@param[in] start	Offset from the start of the file in bytes
@param[in] len		Length in bytes
@param[in] read_only_mode
                        if true, then read only mode checks are enforced.
@return DB_SUCCESS or error code */
dberr_t os_file_write_zeros(pfs_os_file_t file, const char *name,
                            ulint page_size, os_offset_t start, ulint len,
                            bool read_only_mode)
    MY_ATTRIBUTE((warn_unused_result));

#ifndef UNIV_NONINL
/** Class to scan the directory heirarchy using a depth first scan. */
class Dir_Walker {
 public:
  using Path = std::string;

  /** Check if the path is a directory. The file/directory must exist.
  @param[in]	path		The path to check
  @return true if it is a directory */
  static bool is_directory(const Path &path);

  /** Depth first traversal of the directory starting from basedir
  @param[in]  basedir    Start scanning from this directory
  @param[in]  recursive  `true` if scan should be recursive
  @param[in]  f          Function to call for each entry */
  template <typename F>
  static void walk(const Path &basedir, bool recursive, F &&f) {
#ifdef _WIN32
    walk_win32(basedir, recursive,
               [&](const Path &path, size_t depth) { f(path); });
#else
    walk_posix(basedir, recursive,
               [&](const Path &path, size_t depth) { f(path); });
#endif /* _WIN32 */
  }

 private:
  /** Directory names for the depth first directory scan. */
  struct Entry {
    /** Constructor
    @param[in]	path		Directory to traverse
    @param[in]	depth		Relative depth to the base
                                    directory in walk() */
    Entry(const Path &path, size_t depth) : m_path(path), m_depth(depth) {}

    /** Path to the directory */
    Path m_path;

    /** Relative depth of m_path */
    size_t m_depth;
  };

  using Function = std::function<void(const Path &, size_t)>;

  /** Depth first traversal of the directory starting from basedir
  @param[in]  basedir     Start scanning from this directory
  @param[in]  recursive  `true` if scan should be recursive
  @param[in]  f           Function to call for each entry */
#ifdef _WIN32
  static void walk_win32(const Path &basedir, bool recursive, Function &&f);
#else
  static void walk_posix(const Path &basedir, bool recursive, Function &&f);
#endif /* _WIN32 */
};

/** Allocate a page for sync IO
@return pointer to page */
file::Block *os_alloc_block() noexcept;

/** Submit buffered AIO requests on the given segment to the kernel. */
void os_aio_dispatch_read_array_submit();

/** Encrypt a page content when write it to disk.
@param[in]      type    IO flags
@param[out]     buf     buffer to read or write
@param[in,out]  n       number of bytes to read/write, starting from
                        offset
@return pointer to the encrypted page */
file::Block *os_file_encrypt_page(const IORequest &type, void *&buf, ulint *n);

#include "os0file.ic"
#endif /* UNIV_NONINL */

#endif /* os0file_h */
