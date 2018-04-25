# Copyright (c) 2017, 2018, Percona and/or its affiliates. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

# cmake include to wrap if compiler supports std cxx11 and alters compiler flags
# On return, HAVE_STDCXX11 will be set

# CMAKE_CXX_STANDARD was introduced in CMake 3.1, it will be ignored by older CMake
IF ("${CMAKE_VERSION}" VERSION_GREATER "3.1" OR "${CMAKE_VERSION}" VERSION_EQUAL "3.1")
  SET(CMAKE_CXX_STANDARD 11)
  SET(CMAKE_CXX_EXTENSIONS OFF)
  SET(CMAKE_CXX_STANDARD_REQUIRED ON)

  SET (HAVE_STDCXX11 1 CACHE INTERNAL "C++11 mode")
ELSE ()
  INCLUDE(CheckCXXCompilerFlag)
  CHECK_CXX_COMPILER_FLAG(-std=c++11 CXX11_OPTION_SUPPORTED)
  IF (CXX11_OPTION_SUPPORTED)
    SET (CMAKE_CXX_FLAGS "--std=c++11 ${CMAKE_CXX_FLAGS}")
    SET (HAVE_STDCXX11 1 CACHE INTERNAL "C++11 mode")
  ELSE ()
    CHECK_CXX_COMPILER_FLAG(-std=c++0x CXX0X_OPTION_SUPPORTED)
    IF (CXX0X_OPTION_SUPPORTED)
      SET (CMAKE_CXX_FLAGS "--std=c++0x ${CMAKE_CXX_FLAGS}")
      SET (HAVE_STDCXX11 1 CACHE INTERNAL "C++11 mode")
    ENDIF ()
  ENDIF ()
ENDIF ()

IF (HAVE_STDCXX11)
  SET (CMAKE_CXX_FLAGS "-Wno-deprecated-declarations ${CMAKE_CXX_FLAGS}")
ENDIF ()
