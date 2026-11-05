# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.9)
project(bgp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_WITH_LTO "BUILD_WITH_LTO" OFF)
if (BUILD_WITH_LTO)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

set(BGP_BUILD_VARIANT "dc" CACHE STRING "BGP daemon variant: dc | bb")
set_property(CACHE BGP_BUILD_VARIANT PROPERTY STRINGS dc bb)

if (NOT BGP_BUILD_VARIANT MATCHES "^(dc|bb)$")
  message(FATAL_ERROR "Unknown BGP_BUILD_VARIANT '${BGP_BUILD_VARIANT}'")
endif()

message(STATUS "BGP build variant: ${BGP_BUILD_VARIANT}")

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_BINARY_DIR})

set(
  CMAKE_MODULE_PATH
  "${CMAKE_SOURCE_DIR}/build/fbcode_builder/CMake"
  "${CMAKE_SOURCE_DIR}"
  ${CMAKE_MODULE_PATH}
)
include(FBGenCMakeBuildInfo)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/BgpLibrary.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/BgpCommon.cmake)

if (BGP_BUILD_VARIANT STREQUAL "dc")
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/BgpDC.cmake)
else()
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/BgpBB.cmake)
endif()
