# - Find mysqlclient
# Find the native MySQL includes and library
#
#  MYSQL_INCLUDE_DIR - where to find mysql.h, etc.
#  MYSQL_LIBRARIES   - List of libraries when using MySQL.
#  MYSQL_FOUND       - True if MySQL found.

IF (MYSQL_INCLUDE_DIR)
  # Already in cache, be silent
  SET(MYSQL_FIND_QUIETLY TRUE)
ENDIF (MYSQL_INCLUDE_DIR)

FIND_PATH(MYSQL_INCLUDE_DIR mysql.h
          $ENV{MYSQL_DIR}/include
          /usr/include/mysql
          NO_DEFAULT_PATH
)

SET(MYSQL_NAMES mysqlclient mysqlclient_r)
FIND_LIBRARY(MYSQL_LIBRARY
  NAMES ${MYSQL_NAMES}
  PATHS 
  $ENV{MYSQL_DIR}/libmysql
  $ENV{MYSQL_DIR}/lib
  /usr/lib/i386-linux-gnu
)
IF(MYSQL_LIBRARY)
  GET_FILENAME_COMPONENT(MYSQL_LIB_DIR ${MYSQL_LIBRARY} PATH)
ENDIF(MYSQL_LIBRARY)

IF (MYSQL_INCLUDE_DIR AND MYSQL_LIB_DIR)
  SET(MYSQL_FOUND TRUE)
  INCLUDE_DIRECTORIES(${MYSQL_INCLUDE_DIR})
  LINK_DIRECTORIES(${MYSQL_LIB_DIR})
ENDIF(MYSQL_INCLUDE_DIR AND MYSQL_LIB_DIR)

IF (MYSQL_FOUND)
  IF (NOT MYSQL_FIND_QUIETLY)
    MESSAGE(STATUS "Found MySQL: ${MYSQL_LIBRARY}")
  ENDIF (NOT MYSQL_FIND_QUIETLY)
ELSE (NOT MYSQL_FOUND)
  IF (MYSQL_FIND_REQUIRED)
    MESSAGE(STATUS "Looked for MySQL libraries named ${MYSQL_NAMES}.")
    MESSAGE(FATAL_ERROR "Could NOT find MySQL library")
  ENDIF (MYSQL_FIND_REQUIRED)
ENDIF (MYSQL_FOUND)

MARK_AS_ADVANCED(
  MYSQL_LIBRARY
  MYSQL_INCLUDE_DIR
  )

