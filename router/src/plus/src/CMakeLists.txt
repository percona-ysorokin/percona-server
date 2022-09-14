# Copyright (c) 2020, 2021, Oracle and/or its affiliates.
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
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

INCLUDE(GenerateExportHeader)

ADD_LIBRARY(plus_component
  SHARED
  plus_component.cc
  plus_plugin_config.cc
)
TARGET_INCLUDE_DIRECTORIES(plus_component
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../include/
  ${CMAKE_CURRENT_BINARY_DIR}/../include/
)
TARGET_COMPILE_DEFINITIONS(plus_component PRIVATE
  MYSQL_ROUTER_LOG_DOMAIN="plus")

TARGET_LINK_LIBRARIES(plus_component PUBLIC harness-library)


SET_TARGET_PROPERTIES(plus_component PROPERTIES
  OUTPUT_NAME "mysqlrouter_plus_component"
  SOVERSION 1
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/runtime_output_directory
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/library_output_directory
  )

INSTALL(TARGETS plus_component
  RUNTIME DESTINATION ${ROUTER_INSTALL_BINDIR} COMPONENT Router
  ARCHIVE DESTINATION ${ROUTER_INSTALL_LIBDIR} COMPONENT Router
  LIBRARY DESTINATION ${ROUTER_INSTALL_LIBDIR} COMPONENT Router
  NAMELINK_SKIP
  )

GENERATE_EXPORT_HEADER(plus_component
  EXPORT_FILE_NAME
  ${CMAKE_CURRENT_BINARY_DIR}/../include/mysqlrouter/plus_component_export.h
  )

ADD_HARNESS_PLUGIN(plus
  SOURCES
  plus_plugin.cc
  REQUIRES plus_component
  DESTINATION "${ROUTER_INSTALL_PLUGINDIR}"
  )
TARGET_INCLUDE_DIRECTORIES(plus PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../include/
  ${CMAKE_CURRENT_BINARY_DIR}/../include/
)
GENERATE_EXPORT_HEADER(plus
  EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/../include/mysqlrouter/plus_export.h)



ADD_INSTALL_RPATH_FOR_OPENSSL(plus_component)
SET_PATH_TO_CUSTOM_SSL_FOR_APPLE(plus_component)
