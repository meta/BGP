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

#
# Find dependencies
#

set(REQ_BOOST_COMPONENTS
  ${REQ_BOOST_COMPONENTS}
  system thread context filesystem program_options regex
)

find_package(Boost REQUIRED COMPONENTS ${REQ_BOOST_COMPONENTS})
find_package(folly CONFIG REQUIRED)
find_package(fb303 CONFIG REQUIRED)
find_package(fizz REQUIRED)
find_package(fmt REQUIRED)
find_package(Gflags REQUIRED)
find_package(Glog REQUIRED)
# Not find_package(thrift): fb303's package config links FBThrift::thrift-cpp
# without finding the FBThrift package itself.
find_package(FBThrift CONFIG REQUIRED)
find_package(wangle REQUIRED)
find_package(Threads REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(GTest REQUIRED)
find_library(DOUBLE-CONVERSION double-conversion)
find_library(RE2 re2)
find_library(ZSTD zstd)

find_path(RE2_INCLUDE_DIR re2/re2.h)

# FBOSS does not install a package configuration. NodeBase.h is part of both
# supported FBOSS profiles and provides a stable sentinel for their include root.
find_path(
  FBOSS_INCLUDE_DIR
  fboss/agent/state/NodeBase.h
  REQUIRED
)
include_directories(SYSTEM ${FBOSS_INCLUDE_DIR})

find_library(FBOSS_LOG_THRIFT_CALL log_thrift_call REQUIRED)
find_library(FBOSS_ALERT_LOGGER alert_logger REQUIRED)

# FBOSS headers include <gtest/gtest_prod.h> for FRIEND_TEST macros;
# make the GTest include path available globally. GTestConfig.cmake
# provides imported targets but may not set GTEST_INCLUDE_DIRS;
# extract the include directory from the imported target.
get_target_property(GTEST_INCLUDE_DIR
  GTest::gtest INTERFACE_INCLUDE_DIRECTORIES)
include_directories(SYSTEM ${GTEST_INCLUDE_DIR})

# Open/R installs the generated Thrift headers and component libraries consumed
# by BGP++. Derive its package root from a unique header.
find_path(OPENR_INCLUDE_DIR openr/nl/NetlinkProtocolSocket.h REQUIRED)
include_directories(SYSTEM ${OPENR_INCLUDE_DIR})

get_filename_component(
  OPENR_INSTALL_PREFIX "${OPENR_INCLUDE_DIR}" DIRECTORY
)
set(OPENR_LIBRARY_DIR "${OPENR_INSTALL_PREFIX}/lib")

bgp_find_openr_library(OPENR_CONSTANTS_LIBRARY openr_constants)
bgp_find_openr_library(OPENR_FBNL_LIBRARY openr_fbnl)
bgp_find_openr_library(OPENR_NETWORK_UTIL_LIBRARY openr_network_util)
bgp_find_openr_library(OPENR_SYSTEM_METRICS_LIBRARY openr_system_metrics)
bgp_find_openr_library(
  OPENR_ROUTING_POLICY_THRIFT_LIBRARY routing_policy_cpp2
)
bgp_find_openr_library(OPENR_CONFIG_THRIFT_LIBRARY openr_config_cpp2)
bgp_find_openr_library(OPENR_NETWORK_THRIFT_LIBRARY network_cpp2)
bgp_find_openr_library(OPENR_PLATFORM_THRIFT_LIBRARY platform_cpp2)
bgp_find_openr_library(OPENR_KV_STORE_THRIFT_LIBRARY kv_store_cpp2)
bgp_find_openr_library(OPENR_TYPES_THRIFT_LIBRARY types_cpp2)
bgp_find_openr_library(OPENR_CTRL_THRIFT_LIBRARY openr_ctrl_cpp2)
bgp_find_openr_library(OPENR_MESSAGING_STATS_LIBRARY openr_messaging_stats)

set(OPENR_LIBS
  ${OPENR_FBNL_LIBRARY}
  ${OPENR_NETWORK_UTIL_LIBRARY}
  ${OPENR_SYSTEM_METRICS_LIBRARY}
  ${OPENR_MESSAGING_STATS_LIBRARY}
  ${OPENR_CONSTANTS_LIBRARY}
  ${OPENR_PLATFORM_THRIFT_LIBRARY}
  ${OPENR_CTRL_THRIFT_LIBRARY}
  ${OPENR_KV_STORE_THRIFT_LIBRARY}
  ${OPENR_TYPES_THRIFT_LIBRARY}
  ${OPENR_CONFIG_THRIFT_LIBRARY}
  ${OPENR_NETWORK_THRIFT_LIBRARY}
  ${OPENR_ROUTING_POLICY_THRIFT_LIBRARY}
)

set(FOLLY_EXCEPTION_TRACER)
if (TARGET Folly::folly_exception_tracer)
  set(FOLLY_EXCEPTION_TRACER Folly::folly_exception_tracer)
endif()

add_compile_definitions(NO_FOLLY_EXCEPTION_TRACER)
add_compile_definitions(IS_OSS)

#
# Build thrift libs
#

set(BGP_THRIFT_LIBS)

thrift_add_library(
  fb303_cpp2
  common/fb303/if/fb303.thrift
  LANGUAGE cpp2
  SERVICES
    FacebookService
  OPTIONS
    json
)
set(BGP_THRIFT_LIBS ${BGP_THRIFT_LIBS} fb303_cpp2)

# Layer 0 — no inter-project dependencies

thrift_add_library(
  network_address_cpp2
  common/network/if/Address.thrift
  LANGUAGE cpp2
  OPTIONS
    json
)
set(BGP_THRIFT_LIBS ${BGP_THRIFT_LIBS} network_address_cpp2)

thrift_add_library(
  cfgr_fboss_common_cpp2
  configerator/structs/neteng/fboss/thrift/common.thrift
  LANGUAGE cpp2
  OPTIONS
    json
)
set(BGP_THRIFT_LIBS ${BGP_THRIFT_LIBS} cfgr_fboss_common_cpp2)

thrift_add_library(
  routing_policy_cpp2
  configerator/structs/neteng/bgp_policy/thrift/routing_policy.thrift
  LANGUAGE cpp2
  OPTIONS
    json
)
set(BGP_THRIFT_LIBS ${BGP_THRIFT_LIBS} routing_policy_cpp2)

thrift_add_library(
  nsf_policy_cpp2
  configerator/structs/neteng/bgp_policy/thrift/nsf_policy.thrift
  LANGUAGE cpp2
  OPTIONS
    json
)
set(BGP_THRIFT_LIBS ${BGP_THRIFT_LIBS} nsf_policy_cpp2)

thrift_add_library(
  bgp_attr_cpp2
  configerator/structs/neteng/fboss/bgp/if/bgp_attr.thrift
  LANGUAGE cpp2
  OPTIONS
    json
)
set(BGP_THRIFT_LIBS ${BGP_THRIFT_LIBS} bgp_attr_cpp2)

thrift_add_library(
  netwhoami_cpp2
  configerator/structs/neteng/netwhoami/netwhoami.thrift
  LANGUAGE cpp2
  OPTIONS
    json
)
set(BGP_THRIFT_LIBS ${BGP_THRIFT_LIBS} netwhoami_cpp2)

thrift_add_library(
  bgp_cpp2
  neteng/fboss/bgp/if/bgp.thrift
  LANGUAGE cpp2
  OPTIONS
    json
)
set(BGP_THRIFT_LIBS ${BGP_THRIFT_LIBS} bgp_cpp2)

# bgp_thrift.thrift includes neteng/fboss/bgp/if/policy_thrift.thrift, so the
# OSS build needs a thrift target for policy_thrift to generate
# policy_thrift_types.h that bgp_thrift_types.h #includes.
thrift_add_library(
  bgp_policy_thrift_cpp2
  neteng/fboss/bgp/if/policy_thrift.thrift
  LANGUAGE cpp2
  OPTIONS
    json
)
set(BGP_THRIFT_LIBS ${BGP_THRIFT_LIBS} bgp_policy_thrift_cpp2)

# Layer 1 — depends on Layer 0

thrift_add_library(
  bgp_policy_cpp2
  configerator/structs/neteng/bgp_policy/thrift/bgp_policy.thrift
  LANGUAGE cpp2
  OPTIONS
    json
  DEPENDS
    nsf_policy_cpp2
    routing_policy_cpp2
)
set(BGP_THRIFT_LIBS ${BGP_THRIFT_LIBS} bgp_policy_cpp2)

thrift_add_library(
  bgp_structs_cpp2
  neteng/fboss/bgp/if/BgpStructs.thrift
  LANGUAGE cpp2
  OPTIONS
    json
  DEPENDS
    network_address_cpp2
)
set(BGP_THRIFT_LIBS ${BGP_THRIFT_LIBS} bgp_structs_cpp2)

# Layer 2 — depends on Layer 0+1

thrift_add_library(
  rib_policy_cpp2
  configerator/structs/neteng/bgp_policy/thrift/rib_policy.thrift
  LANGUAGE cpp2
  OPTIONS
    json
  DEPENDS
    bgp_policy_cpp2
    routing_policy_cpp2
    bgp_attr_cpp2
    cfgr_fboss_common_cpp2
)
set(BGP_THRIFT_LIBS ${BGP_THRIFT_LIBS} rib_policy_cpp2)

thrift_add_library(
  bgp_route_types_cpp2
  neteng/fboss/bgp/if/bgp_route_types.thrift
  LANGUAGE cpp2
  OPTIONS
    json
  DEPENDS
    bgp_attr_cpp2
    rib_policy_cpp2
    cfgr_fboss_common_cpp2
)
set(BGP_THRIFT_LIBS ${BGP_THRIFT_LIBS} bgp_route_types_cpp2)

thrift_add_library(
  bgp_config_cpp2
  configerator/structs/neteng/fboss/bgp/bgp_config.thrift
  LANGUAGE cpp2
  OPTIONS
    json
  DEPENDS
    bgp_policy_cpp2
    bgp_attr_cpp2
)
set(BGP_THRIFT_LIBS ${BGP_THRIFT_LIBS} bgp_config_cpp2)

# Layer 3 — service thrift files

thrift_add_library(
  bgp_thrift_cpp2
  neteng/fboss/bgp/if/bgp_thrift.thrift
  LANGUAGE cpp2
  SERVICES
    TBgpService
  OPTIONS
    json
  DEPENDS
    rib_policy_cpp2
    bgp_policy_cpp2
    bgp_policy_thrift_cpp2
    bgp_config_cpp2
    bgp_attr_cpp2
    bgp_route_types_cpp2
    fb303_cpp2
)
set(BGP_THRIFT_LIBS ${BGP_THRIFT_LIBS} bgp_thrift_cpp2)

thrift_add_library(
  bgp_stream_cpp2
  neteng/fboss/bgp/if/bgp_stream.thrift
  LANGUAGE cpp2
  SERVICES
    TBgpServiceStream
  OPTIONS
    json
  DEPENDS
    bgp_structs_cpp2
    network_address_cpp2
    fb303_cpp2
)
set(BGP_THRIFT_LIBS ${BGP_THRIFT_LIBS} bgp_stream_cpp2)

add_build_info(build_info)

install(TARGETS
  ${BGP_THRIFT_LIBS}
  DESTINATION lib
)

#
# Common external dependencies
#
# Not ${Boost_LIBRARIES}: folly's package config runs find_package(Boost) again
# with its own components, which resets it.
set(BGP_BOOST_LIBS)
foreach(component IN LISTS REQ_BOOST_COMPONENTS)
  list(APPEND BGP_BOOST_LIBS Boost::${component})
endforeach()

set(BGP_BASE_LIBS
  Folly::folly
  glog::glog
  gflags
  Threads::Threads
  ${BGP_BOOST_LIBS}
  magic_enum::magic_enum
)
# Variant-specific archive closures are intentionally excluded. They may
# cross-reference the BGP Thrift libraries, so each variant groups them at its
# final executable link.

#
# Tier 0: Leaf libraries (no bgp/cpp cross-module deps)
#

add_library(bgp_routelib
  neteng/fboss/bgp/cpp/routelib/RouteFilter.cpp
  neteng/fboss/bgp/cpp/routelib/RouteSelector.cpp
)

target_link_libraries(bgp_routelib
  ${BGP_BASE_LIBS}
)

add_library(bgp_lib_core
  neteng/fboss/bgp/cpp/lib/BgpAttributesSmartSet.cpp
  neteng/fboss/bgp/cpp/lib/BgpAttributesWrapper.cpp
  neteng/fboss/bgp/cpp/lib/BgpMessageSerializer.cpp
  neteng/fboss/bgp/cpp/lib/BgpStructs.cpp
  neteng/fboss/bgp/cpp/lib/BgpUtil.cpp
)

target_link_libraries(bgp_lib_core
  ${BGP_THRIFT_LIBS}
  ${BGP_BASE_LIBS}
)

add_library(bgp_ip_prefix_utils
  neteng/fboss/bgp/cpp/common/IpPrefixUtils.cpp
)

target_link_libraries(bgp_ip_prefix_utils
  bgp_attr_cpp2
  bgp_structs_cpp2
  network_address_cpp2
  ${BGP_BASE_LIBS}
)

add_library(bgp_utils
  neteng/fboss/bgp/cpp/common/Utils.cpp
)

target_link_libraries(bgp_utils
  ${BGP_THRIFT_LIBS}
  ${BGP_BASE_LIBS}
)

#
# Tier 1: Foundation
#

add_library(bgp_stats_base
  neteng/fboss/bgp/cpp/stats/StatsBase.cpp
)

target_link_libraries(bgp_stats_base
  bgp_utils
  fb303::fb303
  ${BGP_THRIFT_LIBS}
  ${BGP_BASE_LIBS}
)

add_library(bgp_common
  neteng/fboss/bgp/cpp/common/BuildInfo.cpp
  neteng/fboss/bgp/cpp/common/BgpModuleBase.cpp
  neteng/fboss/bgp/cpp/common/BgpPath.cpp
  neteng/fboss/bgp/cpp/common/FeatureFlags.cpp
  neteng/fboss/bgp/cpp/common/RouteInfo.cpp
  neteng/fboss/bgp/cpp/lib/BgpMessageParser.cpp
  neteng/fboss/bgp/cpp/lib/detail/BgpMessageParserUtils.cpp
)

target_link_libraries(bgp_common
  bgp_lib_core
  bgp_routelib
  bgp_stats_base
  bgp_utils
  build_info
  fb303::fb303
  ${BGP_THRIFT_LIBS}
  ${BGP_BASE_LIBS}
  ${OPENR_LIBS}
  ${RE2}
)

add_library(bgp_changetracker
  neteng/fboss/bgp/cpp/changeTracker/ChangeTrackerDebug.cpp
  neteng/fboss/bgp/cpp/changeTracker/ConsumerBitManager.cpp
)

target_link_libraries(bgp_changetracker
  bgp_common
)

#
# Tier 2: Infrastructure
#

add_library(bgp_config_policy
  neteng/fboss/bgp/cpp/BgpServiceUtil.cpp
  neteng/fboss/bgp/cpp/config/Config.cpp
  neteng/fboss/bgp/cpp/config/ConfigManager.cpp
  neteng/fboss/bgp/cpp/config/ConfigStructs.cpp
  neteng/fboss/bgp/cpp/config/ConfigUtils.cpp
  neteng/fboss/bgp/cpp/policy/Policy.cpp
  neteng/fboss/bgp/cpp/policy/PolicyAction.cpp
  neteng/fboss/bgp/cpp/policy/PolicyManager.cpp
  neteng/fboss/bgp/cpp/policy/PolicyMatch.cpp
  neteng/fboss/bgp/cpp/policy/PolicyTerm.cpp
  neteng/fboss/bgp/cpp/policy/PolicyUtils.cpp
  neteng/fboss/bgp/cpp/policy/base/PolicyMatchBase.cpp
  neteng/fboss/bgp/cpp/policy/base/PolicyUtils.cpp
)

target_link_libraries(bgp_config_policy
  bgp_lib_core
  bgp_common
  bgp_ip_prefix_utils
  ${BGP_THRIFT_LIBS}
  ${BGP_BASE_LIBS}
)

add_library(bgp_watchdog
  neteng/fboss/bgp/cpp/watchdog/MemProfiler.cpp
  neteng/fboss/bgp/cpp/watchdog/MonitoredModule.cpp
  neteng/fboss/bgp/cpp/watchdog/QueryTree.cpp
  neteng/fboss/bgp/cpp/watchdog/Watchdog.cpp
)

target_link_libraries(bgp_watchdog
  bgp_lib_core
  bgp_common
  bgp_config_policy
  ${BGP_THRIFT_LIBS}
  ${BGP_BASE_LIBS}
)

#
# Tier 3: Transport
#

add_library(bgp_lib_transport
  neteng/fboss/bgp/cpp/lib/fibers/BgpPeerDisplayInfo.cpp
  neteng/fboss/bgp/cpp/lib/fibers/BgpSerializer.cpp
  neteng/fboss/bgp/cpp/lib/fibers/FiberBgpParser.cpp
  neteng/fboss/bgp/cpp/lib/fibers/FiberBgpPeer.cpp
  neteng/fboss/bgp/cpp/lib/fibers/FiberBgpPeerManager.cpp
  neteng/fboss/bgp/cpp/lib/fibers/FiberServerSocket.cpp
  neteng/fboss/bgp/cpp/lib/fibers/FiberSocket.cpp
  neteng/fboss/bgp/cpp/lib/fibers/Utils.cpp
)

target_link_libraries(bgp_lib_transport
  bgp_lib_core
  bgp_common
  bgp_watchdog
  bgp_config_policy
)

add_library(bgp_nexthop
  neteng/fboss/bgp/cpp/nexthopTracker/InterfaceEntry.cpp
  neteng/fboss/bgp/cpp/nexthopTracker/NexthopCache.cpp
  neteng/fboss/bgp/cpp/nexthopTracker/NexthopHandler.cpp
)
# NetlinkWrapper.cpp is BB-only and is added by BgpBB.cmake so the common
# nexthop library does not acquire BB-specific dependencies.

target_link_libraries(bgp_nexthop
  bgp_common
  bgp_lib_core
  bgp_lib_transport
  bgp_watchdog
  thrift::rpc
  ${OPENR_LIBS}
)

#
# Tier 4: Core routing
#

# FIB implementations — separated per code boundary rules (DC / EBB / Dev)

add_library(bgp_fib_holddown
  neteng/fboss/bgp/cpp/rib/FibProgrammingHolddown.cpp
)

target_link_libraries(bgp_fib_holddown
  ${BGP_BASE_LIBS}
)

add_library(bgp_fib_out
  neteng/fboss/bgp/cpp/rib/FibOut.cpp
)

target_link_libraries(bgp_fib_out
  bgp_structs_cpp2
  ${BGP_BASE_LIBS}
)

add_library(bgp_fib_out_thrift
  neteng/fboss/bgp/cpp/rib/FibOutThrift.cpp
)

target_link_libraries(bgp_fib_out_thrift
  bgp_fib_out
  bgp_ip_prefix_utils
  bgp_route_types_cpp2
  thrift::rpc
  ${BGP_BASE_LIBS}
)

add_library(bgp_fib_dev
  neteng/fboss/bgp/cpp/rib/FibDev.cpp
)

target_link_libraries(bgp_fib_dev
  bgp_common
  bgp_fib_out
  bgp_lib_core
  ${BGP_BASE_LIBS}
)

add_library(bgp_rib_base
  neteng/fboss/bgp/cpp/rib/canonical/CanonicalConvert.cpp
  neteng/fboss/bgp/cpp/rib/canonical/CanonicalRibBuilder.cpp
  neteng/fboss/bgp/cpp/rib/RibBase.cpp
  neteng/fboss/bgp/cpp/rib/RibEntry.cpp
  neteng/fboss/bgp/cpp/rib/RibPolicy.cpp
  neteng/fboss/bgp/cpp/rib/RibPolicyLogger.cpp
  neteng/fboss/bgp/cpp/rib/RouteInfoFilter.cpp
  neteng/fboss/bgp/cpp/rib/RouteInfoSelector.cpp
  neteng/fboss/bgp/cpp/rib/Utils.cpp
)

target_link_libraries(bgp_rib_base
  bgp_common
  bgp_config_policy
  bgp_fib_dev
  bgp_fib_out
  bgp_fib_out_thrift
  bgp_lib_core
  bgp_nexthop
  bgp_routelib
  bgp_stats_base
  bgp_watchdog
  thrift::rpc
  ${OPENR_LIBS}
)

add_library(bgp_adjrib
  neteng/fboss/bgp/cpp/adjrib/AdjRib.cpp
  neteng/fboss/bgp/cpp/adjrib/AdjRibCommon.cpp
  neteng/fboss/bgp/cpp/adjrib/AdjRibGroup.cpp
  neteng/fboss/bgp/cpp/adjrib/AdjRibGroupSerializer.cpp
  neteng/fboss/bgp/cpp/adjrib/AdjRibIn.cpp
  neteng/fboss/bgp/cpp/adjrib/AdjRibOut.cpp
  neteng/fboss/bgp/cpp/adjrib/AdjRibPolicyCache.cpp
  neteng/fboss/bgp/cpp/adjrib/AdjRibShow.cpp
  neteng/fboss/bgp/cpp/adjrib/AdjRibStats.cpp
  neteng/fboss/bgp/cpp/adjrib/AdjRibStructs.cpp
  neteng/fboss/bgp/cpp/adjrib/AdjRibUtil.cpp
  neteng/fboss/bgp/cpp/adjrib/LegacyV4NlriEncoding.cpp
  neteng/fboss/bgp/cpp/adjrib/PathIdGenerator.cpp
  neteng/fboss/bgp/cpp/adjrib/RouteFilterLogger.cpp
  neteng/fboss/bgp/cpp/adjrib/UpdateGroupManager.cpp
  neteng/fboss/bgp/cpp/adjrib/WellKnownCommunityFilter.cpp
)

target_link_libraries(bgp_adjrib
  bgp_changetracker
  bgp_common
  bgp_config_policy
  bgp_lib_core
  bgp_lib_transport
  bgp_rib_base
  bgp_watchdog
  ${FBOSS_ALERT_LOGGER}
  ${OPENR_LIBS}
)

add_library(bgp_peer_base
  neteng/fboss/bgp/cpp/peer/PeerManagerBase.cpp
  neteng/fboss/bgp/cpp/peer/PeerManagerUtils.cpp
  neteng/fboss/bgp/cpp/peer/SessionManager.cpp
)

target_link_libraries(bgp_peer_base
  bgp_adjrib
  bgp_changetracker
  bgp_common
  bgp_config_policy
  bgp_lib_core
  bgp_lib_transport
  bgp_nexthop
  bgp_rib_base
  bgp_stats_base
  bgp_watchdog
  thrift::rpc
  ${OPENR_LIBS}
)

#
# Tier 5: Application
#

add_library(bgp_service_base
  neteng/fboss/bgp/cpp/BgpServiceBase.cpp
  neteng/fboss/bgp/cpp/BgpServiceStream.cpp
  neteng/fboss/bgp/cpp/BgpServiceEventHandler.cpp
  neteng/fboss/bgp/cpp/BgpConfigValidator.cpp
  neteng/fboss/bgp/cpp/BgpProfiler.cpp
  neteng/fboss/bgp/cpp/health/HealthValidator.cpp
)

target_link_libraries(bgp_service_base
  ${FBOSS_ALERT_LOGGER}
  ${FBOSS_LOG_THRIFT_CALL}
  bgp_common
  bgp_config_policy
  bgp_lib_core
  bgp_peer_base
  bgp_rib_base
  bgp_stats_base
  bgp_watchdog
  ${RE2}
  thrift::rpc
  fb303::fb303
  ${OPENR_LIBS}
)

set(BGP_INSTALL_LIBRARIES
  bgp_ip_prefix_utils
  bgp_utils
  bgp_stats_base
  bgp_routelib
  bgp_lib_core
  bgp_common
  bgp_changetracker
  bgp_config_policy
  bgp_watchdog
  bgp_lib_transport
  bgp_nexthop
  bgp_fib_holddown
  bgp_fib_out
  bgp_fib_out_thrift
  bgp_fib_dev
  bgp_rib_base
  bgp_adjrib
  bgp_peer_base
  bgp_service_base
)

install(TARGETS
  ${BGP_INSTALL_LIBRARIES}
  DESTINATION lib
)

include(CTest)
if (BUILD_TESTING)
  add_test(
    NAME bgp_oss_build_boundary_test
    COMMAND ${CMAKE_COMMAND}
      -DBGP_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
      -P ${CMAKE_SOURCE_DIR}/neteng/fboss/bgp/cpp/tests/VerifyOssBuildBoundary.cmake
  )
endif()

#
# Install files
#

install(
  DIRECTORY ${CMAKE_SOURCE_DIR}/neteng
  DESTINATION include
  FILES_MATCHING
    PATTERN "*.h"
    # facebook/ subdirs hold Meta-internal-only code and must not ship in OSS.
    PATTERN "facebook" EXCLUDE
)

install(
  DIRECTORY ${CMAKE_SOURCE_DIR}/neteng
  DESTINATION include
  FILES_MATCHING PATTERN "*.thrift"
)

install(
  DIRECTORY ${CMAKE_SOURCE_DIR}/configerator
  DESTINATION include
  FILES_MATCHING PATTERN "*.thrift"
)

install(
  DIRECTORY ${CMAKE_SOURCE_DIR}/common
  DESTINATION include
  FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*.thrift"
)
