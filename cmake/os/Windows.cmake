# Copyright (c) 2010, 2019, Oracle and/or its affiliates. All rights reserved.
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# This program is also distributed with certain software (including
# but not limited to OpenSSL) that is licensed under separate terms,
# as designated in a particular file or component or in included license
# documentation.  The authors of MySQL hereby grant you an additional
# permission to link the program and your derivative works with the
# separately licensed software that they have included with MySQL.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA 

# This file includes Windows specific hacks, mostly around compiler flags

INCLUDE (CheckCSourceCompiles)
INCLUDE (CheckCXXSourceCompiles)
INCLUDE (CheckStructHasMember)
INCLUDE (CheckLibraryExists)
INCLUDE (CheckFunctionExists)
INCLUDE (CheckCCompilerFlag)
INCLUDE (CheckCSourceRuns)
INCLUDE (CheckSymbolExists)
INCLUDE (CheckTypeSize)

IF(MY_COMPILER_IS_CLANG)
  SET(WIN32_CLANG 1)
  SET(CMAKE_INCLUDE_SYSTEM_FLAG_C "/imsvc ")
  SET(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "/imsvc ")
ENDIF()

# Optionally read user configuration, generated by configure.js.
# This is left for backward compatibility reasons only.
INCLUDE(${CMAKE_BINARY_DIR}/win/configure.data OPTIONAL)

# avoid running system checks by using pre-cached check results
# system checks are expensive on VS since every tiny program is to be compiled
# in a VC solution.
GET_FILENAME_COMPONENT(_SCRIPT_DIR ${CMAKE_CURRENT_LIST_FILE} PATH)
INCLUDE(${_SCRIPT_DIR}/WindowsCache.cmake)

# We require at least Visual Studio 2019 (aka 16) which has version nr 1920.
IF(NOT FORCE_UNSUPPORTED_COMPILER AND MSVC_VERSION LESS 1920)
  MESSAGE(FATAL_ERROR
    "Visual Studio 2019 or newer is required!")
ENDIF()

# OS display name (version_compile_os etc).
# Used by the test suite to ignore bugs on some platforms, 
IF(CMAKE_SIZEOF_VOID_P MATCHES 8)
  SET(SYSTEM_TYPE "Win64")
  SET(MYSQL_MACHINE_TYPE "x86_64")
ELSE()
  IF(WITHOUT_SERVER)
    MESSAGE(WARNING "32bit is experimental!!")
  ELSE()
    MESSAGE(FATAL_ERROR "32 bit Windows builds are not supported. "
      "Clean the build dir and rebuild using -G \"${CMAKE_GENERATOR} Win64\"")
  ENDIF()
ENDIF()

# Target Windows 7 / Windows Server 2008 R2 or later, i.e _WIN32_WINNT_WIN7
ADD_DEFINITIONS(-D_WIN32_WINNT=0x0601)
SET(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -D_WIN32_WINNT=0x0601")

# Speed up build process excluding unused header files
ADD_DEFINITIONS(-DWIN32_LEAN_AND_MEAN -DNOGDI)

# We want to use std::min/std::max, not the windows.h macros
ADD_DEFINITIONS(-DNOMINMAX)

IF(WITH_MSCRT_DEBUG)
  ADD_DEFINITIONS(-DMY_MSCRT_DEBUG)
  ADD_DEFINITIONS(-D_CRTDBG_MAP_ALLOC)
ENDIF()

IF(WIN32_CLANG)
  # RapidJSON doesn't understand the Win32/Clang combination.
  ADD_DEFINITIONS(-DRAPIDJSON_HAS_CXX11_RVALUE_REFS=1)
  ADD_DEFINITIONS(-DRAPIDJSON_HAS_CXX11_NOEXCEPT=1)
  ADD_DEFINITIONS(-DRAPIDJSON_HAS_CXX11_RANGE_FOR=1)
ENDIF()
  
OPTION(WIN_DEBUG_NO_INLINE "Disable inlining for debug builds on Windows" OFF)

IF(MSVC)
  OPTION(LINK_STATIC_RUNTIME_LIBRARIES "Link with /MT" OFF)
  IF(WITH_ASAN AND WIN32_CLANG)
    SET(LINK_STATIC_RUNTIME_LIBRARIES ON)
  ENDIF()

  # Enable debug info also in Release build,
  # and create PDB to be able to analyze crashes.
  FOREACH(type EXE SHARED MODULE)
   SET(CMAKE_{type}_LINKER_FLAGS_RELEASE
     "${CMAKE_${type}_LINKER_FLAGS_RELEASE} /debug")
  ENDFOREACH()
  
  # For release types Debug Release RelWithDebInfo (but not MinSizeRel):
  # - Choose C++ exception handling:
  #     If /EH is not specified, the compiler will catch structured and
  #     C++ exceptions, but will not destroy C++ objects that will go out of
  #     scope as a result of the exception.
  #     /EHsc catches C++ exceptions only and tells the compiler to assume that
  #     extern C functions never throw a C++ exception.
  # - Choose debugging information:
  #     /Z7
  #     Produces an .obj file containing full symbolic debugging
  #     information for use with the debugger. The symbolic debugging
  #     information includes the names and types of variables, as well as
  #     functions and line numbers. No .pdb file is produced by the compiler.
  #     We can't use /ZI too since it's causing __LINE__ macros to be non-
  #     constant on visual studio and hence XCom stops building correctly.
  # - Enable explicit inline:
  #     /Ob1
  #     Expands explicitly inlined functions. By default /Ob0 is used,
  #     meaning no inlining. But this impacts test execution time.
  #     Allowing inline reduces test time using the debug server by
  #     30% or so. If you do want to keep inlining off, set the
  #     cmake flag WIN_DEBUG_NO_INLINE.
  FOREACH(lang C CXX)
    SET(CMAKE_${lang}_FLAGS_RELEASE "${CMAKE_${lang}_FLAGS_RELEASE} /Z7")
  ENDFOREACH()

  FOREACH(flag
      CMAKE_C_FLAGS_MINSIZEREL
      CMAKE_C_FLAGS_RELEASE    CMAKE_C_FLAGS_RELWITHDEBINFO
      CMAKE_C_FLAGS_DEBUG      CMAKE_C_FLAGS_DEBUG_INIT
      CMAKE_CXX_FLAGS_MINSIZEREL
      CMAKE_CXX_FLAGS_RELEASE  CMAKE_CXX_FLAGS_RELWITHDEBINFO
      CMAKE_CXX_FLAGS_DEBUG    CMAKE_CXX_FLAGS_DEBUG_INIT)
    IF(LINK_STATIC_RUNTIME_LIBRARIES)
      STRING(REPLACE "/MD"  "/MT" "${flag}" "${${flag}}")
    ENDIF()
    STRING(REPLACE "/Zi"  "/Z7" "${flag}" "${${flag}}")
    STRING(REPLACE "/ZI"  "/Z7" "${flag}" "${${flag}}")
    IF (NOT WIN_DEBUG_NO_INLINE)
      STRING(REPLACE "/Ob0"  "/Ob1" "${flag}" "${${flag}}")
    ENDIF()
    SET("${flag}" "${${flag}} /EHsc")
    # Due to a bug in VS2019 we need the full paths of files in error messages
    # See bug #30255096 for details
    SET("${flag}" "${${flag}} /FC")
  ENDFOREACH()

  # Turn on c++14 mode explicitly so that using c++17 features is disabled.
  FOREACH(flag
          CMAKE_CXX_FLAGS_MINSIZEREL
          CMAKE_CXX_FLAGS_RELEASE  CMAKE_CXX_FLAGS_RELWITHDEBINFO
          CMAKE_CXX_FLAGS_DEBUG    CMAKE_CXX_FLAGS_DEBUG_INIT)
    SET("${flag}" "${${flag}} /std:c++14")
  ENDFOREACH()

  FOREACH(type EXE SHARED MODULE)
    FOREACH(config DEBUG RELWITHDEBINFO RELEASE MINSIZEREL)
      SET(flag "CMAKE_${type}_LINKER_FLAGS_${config}")
      SET("${flag}" "${${flag}} /INCREMENTAL:NO")
    ENDFOREACH()
  ENDFOREACH()

  IF(NOT WIN32_CLANG)
    # Speed up multiprocessor build (not supported by the Clang driver)
    STRING_APPEND(CMAKE_C_FLAGS " /MP")
    STRING_APPEND(CMAKE_CXX_FLAGS " /MP")
  ENDIF()

  #TODO: update the code and remove the disabled warnings

  # The compiler encountered a deprecated declaration.
  STRING_APPEND(CMAKE_C_FLAGS " /wd4996")
  STRING_APPEND(CMAKE_CXX_FLAGS " /wd4996")

  # 'var' : conversion from 'size_t' to 'type', possible loss of data
  STRING_APPEND(CMAKE_C_FLAGS " /wd4267")
  STRING_APPEND(CMAKE_CXX_FLAGS " /wd4267")

  # 'conversion' conversion from 'type1' to 'type2', possible loss of data
  STRING_APPEND(CMAKE_C_FLAGS " /wd4244")
  STRING_APPEND(CMAKE_CXX_FLAGS " /wd4244")

  # Enable stricter standards conformance when using Visual Studio
  IF(NOT WIN32_CLANG)
    STRING_APPEND(CMAKE_CXX_FLAGS " /permissive-")
  ENDIF()
ENDIF()

# Always link with socket library
LINK_LIBRARIES(ws2_32)
# ..also for tests
LIST(APPEND CMAKE_REQUIRED_LIBRARIES ws2_32)

SET(FN_NO_CASE_SENSE 1)
