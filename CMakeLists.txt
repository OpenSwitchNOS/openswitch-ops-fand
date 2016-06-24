# (c) Copyright 2015 Hewlett Packard Enterprise Development LP
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.

cmake_minimum_required (VERSION 2.8)

set (FAND ops-fand)
project (${FAND})
set (SRC_DIR src)
set (INCL_DIR include)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -Wall -Werror")

# Temporary hardcodes to compile without changes in ops-build
set (LIBDIR usr/lib)
set (FAND_PLUGIN_NAME yaml-i2c)

# Rules to locate needed libraries
include(FindPkgConfig)
pkg_check_modules(OVSCOMMON REQUIRED libovscommon)
pkg_check_modules(OVSDB REQUIRED libovsdb)

include_directories (${PROJECT_BINARY_DIR} ${PROJECT_SOURCE_DIR}/${INCL_DIR}
                     ${OVSCOMMON_INCLUDE_DIRS}
)

configure_file(${CMAKE_SOURCE_DIR}/${INCL_DIR}/fand_plugins.h.in
               ${CMAKE_SOURCE_DIR}/${INCL_DIR}/fand_plugins.h)

# Source files to build ops-fand
set (SOURCES ${SRC_DIR}/fand.c ${SRC_DIR}/fand_plugins.c ${SRC_DIR}/fanspeed.c
             ${SRC_DIR}/fanstatus.c ${SRC_DIR}/fandirection.c)

# Rules to build ops-fand
add_executable (${FAND} ${SOURCES})

target_link_libraries (${FAND}
                       ${OVSCOMMON_LIBRARIES} ${OVSDB_LIBRARIES}
                       -lpthread -lrt -lsupportability)

# Build ops-ledd cli shared libraries.
add_subdirectory(src/cli)
add_subdirectory(plugins/${FAND_PLUGIN_NAME})

# Rules to install ops-fand binary in rootfs
install(TARGETS ${FAND}
        RUNTIME DESTINATION bin)
