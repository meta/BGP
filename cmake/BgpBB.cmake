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

find_library(BGP_BB_FBOSS_NODEBASE_LIBRARY nodebase REQUIRED)
find_library(BGP_BB_FBOSS_RADIX_TREE_LIBRARY radix_tree REQUIRED)
find_library(
  BGP_BB_FBOSS_EXPONENTIAL_BACK_OFF_LIBRARY
  exponential_back_off
  REQUIRED
)

set(BGP_BB_FBOSS_LIBS
  ${BGP_BB_FBOSS_NODEBASE_LIBRARY}
  ${BGP_BB_FBOSS_RADIX_TREE_LIBRARY}
  ${BGP_BB_FBOSS_EXPONENTIAL_BACK_OFF_LIBRARY}
  ${FBOSS_LOG_THRIFT_CALL}
  ${FBOSS_ALERT_LOGGER}
)

add_library(bgp_config_bb
  neteng/fboss/bgp/cpp/config/ConfigBB.cpp
)

target_link_libraries(bgp_config_bb
  bgp_config_policy
)

add_library(bgp_stats_bb
  neteng/fboss/bgp/cpp/stats/StatsBB.cpp
)

target_link_libraries(bgp_stats_bb
  bgp_stats_base
  fb303::fb303
  ${BGP_BASE_LIBS}
)

add_library(bgp_netlink_wrapper
  neteng/fboss/bgp/cpp/nexthopTracker/NetlinkWrapper.cpp
)

target_link_libraries(bgp_netlink_wrapper
  bgp_common
  bgp_lib_core
  bgp_lib_transport
  bgp_nexthop
  bgp_stats_base
  bgp_watchdog
  thrift::rpc
  ${BGP_BASE_LIBS}
  ${OPENR_LIBS}
  ${RE2}
)

add_library(bgp_fib_ebb
  neteng/fboss/bgp/cpp/rib/FibEbb.cpp
)

target_link_libraries(bgp_fib_ebb
  bgp_common
  bgp_fib_holddown
  bgp_fib_out
  bgp_lib_core
  bgp_stats_base
  thrift::rpc
  ${BGP_BASE_LIBS}
  ${OPENR_LIBS}
)

add_library(bgp_rib_bb
  neteng/fboss/bgp/cpp/rib/RibBB.cpp
)

target_link_libraries(bgp_rib_bb
  bgp_fib_ebb
  bgp_rib_base
  bgp_stats_bb
)

add_library(bgp_peer_bb
  neteng/fboss/bgp/cpp/peer/PeerManagerBB.cpp
)

target_link_libraries(bgp_peer_bb
  bgp_peer_base
)

add_library(bgp_health_bb
  neteng/fboss/bgp/cpp/health/HealthValidatorBB.cpp
)

target_link_libraries(bgp_health_bb
  bgp_netlink_wrapper
  bgp_service_base
  ${BGP_BASE_LIBS}
)

add_library(bgp_service_bb
  neteng/fboss/bgp/cpp/BgpServiceBB.cpp
)

target_link_libraries(bgp_service_bb
  bgp_health_bb
  bgp_netlink_wrapper
  bgp_peer_bb
  bgp_rib_bb
  bgp_service_base
  bgp_stats_bb
)

install(TARGETS
  bgp_config_bb
  bgp_stats_bb
  bgp_netlink_wrapper
  bgp_fib_ebb
  bgp_rib_bb
  bgp_peer_bb
  bgp_health_bb
  bgp_service_bb
  DESTINATION lib
)

add_library(bgp_main_util_bb
  neteng/fboss/bgp/cpp/MainUtilBB.cpp
)

target_link_libraries(bgp_main_util_bb
  thrift::rpc
  bgp_config_cpp2
  bgp_stats_base
  ${BGP_BASE_LIBS}
  ${OPENR_LIBS}
)

add_executable(bgp_bb_bin
  neteng/fboss/bgp/cpp/MainBBOSS.cpp
)

set(BGP_BB_LINK_GROUP_LIBS
  ${BGP_THRIFT_LIBS}
  ${OPENR_LIBS}
  ${BGP_BB_FBOSS_LIBS}
)
list(JOIN BGP_BB_LINK_GROUP_LIBS "," BGP_BB_LINK_GROUP_LIBS_CSV)

target_link_libraries(bgp_bb_bin
  bgp_config_bb
  bgp_main_util_bb
  bgp_netlink_wrapper
  bgp_peer_bb
  bgp_rib_bb
  bgp_service_bb
  bgp_stats_base
  bgp_stats_bb
  build_info
  "$<LINK_GROUP:RESCAN,${BGP_BB_LINK_GROUP_LIBS_CSV}>"
  ${BGP_BASE_LIBS}
  thrift::rpc
  ${FOLLY_EXCEPTION_TRACER}
)

set_target_properties(bgp_bb_bin PROPERTIES OUTPUT_NAME bgp_bb)
install(TARGETS bgp_bb_bin DESTINATION sbin)

if (BUILD_TESTING)
  configure_file(
    cmake/OssTestMain.cpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/OssTestMain.cpp
    COPYONLY
  )

  add_executable(bgp_platform_constant_bb_test
    neteng/fboss/bgp/cpp/tests/PlatformConstantBbTest.cpp
  )
  add_dependencies(bgp_platform_constant_bb_test bgp_structs_cpp2)
  target_link_libraries(bgp_platform_constant_bb_test
    thrift::rpc
    fmt::fmt
    Folly::folly
    GTest::gtest_main
    ${RE2}
  )
  add_test(
    NAME bgp_platform_constant_bb_test
    COMMAND bgp_platform_constant_bb_test
  )

  add_executable(bgp_stats_bb_test
    ${CMAKE_CURRENT_BINARY_DIR}/OssTestMain.cpp
    neteng/fboss/bgp/cpp/tests/StatsBBTest.cpp
  )
  target_link_libraries(bgp_stats_bb_test
    bgp_stats_bb
    GTest::gtest
    "$<LINK_GROUP:RESCAN,${BGP_BB_LINK_GROUP_LIBS_CSV}>"
    ${RE2}
  )
  add_test(
    NAME bgp_stats_bb_test
    COMMAND bgp_stats_bb_test
  )

  add_test(
    NAME bgp_bb_version_test
    COMMAND bgp_bb_bin --version
  )
endif()
