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

# BgpCommon.cmake generates cfgr_fboss_common_cpp2 from the canonical schema,
# so the FBOSS-installed copy is intentionally omitted from this list.
set(BGP_DC_FBOSS_LIBRARY_NAMES
  address_utils
  nodebase
  radix_tree
  exponential_back_off
  # ctrl_cpp2 and its transitive thrift dependencies. FBOSS ships no
  # fbossConfig.cmake, so the consumer must list every transitive static
  # library explicitly. Order matters for one-pass linkers: dependents come
  # before their dependencies, with the deepest leaf libraries last.
  ctrl_cpp2
  agent_stats_cpp2
  hardware_stats_cpp2
  asic_temp_cpp2
  mka_structs_cpp2
  transceiver_cpp2
  product_info_cpp2
  prbs_cpp2
  link_cpp2
  phy_cpp2
  platform_config_cpp2
  bcm_config_cpp2
  asic_config_cpp2
  asic_config_v2_cpp2
  switch_config_cpp2
  fboss_cpp2
  optic_cpp2
  mpls_cpp2
  io_stats_cpp2
  common_cpp2
  fboss_common_cpp2
  fboss_error
  fboss_types
  # FSDB client libraries for FsdbSyncer. Same pattern as ctrl_cpp2 above:
  # dependents before dependencies for one-pass linkers.
  fsdb_syncer
  fsdb_pub_sub
  fsdb_sub_mgr
  fsdb_stream_client
  fsdb_flags
  fsdb_utils
  fsdb_model_cpp2
  thriftpath_lib
  fsdb_common_cpp2
  fsdb_oper_cpp2
  fsdb_cpp2
  cow_storage_mgr
  oper_path_helpers
  common_thrift_utils
  thrift_service_client
  # COW subscription instantiations for NeighborWatcher.
  fsdb_cow_state_sub_mgr
  fsdb_cow_storage
  fsdb_cow_root
  # FSDB model transitive thrift dependencies.
  switch_state_cpp2
  agent_config_cpp2
  agent_info_cpp2
  switch_reachability_cpp2
  qsfp_state_cpp2
  qsfp_stats_cpp2
  sensor_service_stats_cpp2
  te_srv6_agent_stats_cpp2
  # Leaf thrift dependencies pulled in by qsfp_state and sensor_service.
  qsfp_config_cpp2
  port_state_cpp2
  pim_state_cpp2
  sensor_service_cpp2
  # thrift_cow C++ libraries backing the FsdbOperStateRoot COW storage.
  # Ordering between these and the BGP++/FSDB thrift libraries is handled by
  # the RESCAN link group in the bgp_bin rule below.
  patch_cpp2
  cow_visitor_results_cpp2
  storage
  cow_storage
  thrift_cow_serializer
  thrift_cow_nodes
  thrift_cow_visitors_common
  thrift_cow_visitors
  # FsdbOperStateRoot extern-template instantiation libraries.
  fsdb_cow_root_path_visitor
  fsdb_path_visitor_oper_state_instantiations
  fsdb_patch_applier_oper_state_instantiations
  fsdb_thrift_struct_oper_instantiations
  # Leaf thrift libraries reached through the QSFP, sensor, and service
  # client portions of the FsdbOperStateRoot model.
  transceiver_validation_cpp2
  sensor_config_cpp2
  reboot_cause_config_cpp2
  qsfp_cpp2
)

set(BGP_DC_FBOSS_LIBS)
foreach(fboss_library IN LISTS BGP_DC_FBOSS_LIBRARY_NAMES)
  find_library(
    BGP_DC_FBOSS_${fboss_library}_LIBRARY
    ${fboss_library}
    REQUIRED
  )
  list(APPEND
    BGP_DC_FBOSS_LIBS
    ${BGP_DC_FBOSS_${fboss_library}_LIBRARY}
  )
endforeach()

# FBOSS's OSS fsdb_model.thrift includes bgp_config.thrift via the
# public_tld-prefixed path (neteng/fboss/bgp/public_tld/configerator/...).
# BGP++ builds bgp_config from the canonical configerator/ path because
# public_tld is the repository root. Create a symlink so FBOSS-installed
# headers resolve against the locally generated bgp_config headers.
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/neteng/fboss/bgp/public_tld)
file(CREATE_LINK
  ${CMAKE_BINARY_DIR}/configerator
  ${CMAKE_BINARY_DIR}/neteng/fboss/bgp/public_tld/configerator
  SYMBOLIC
)

add_library(bgp_stats_dc
  neteng/fboss/bgp/cpp/stats/StatsDC.cpp
)

target_link_libraries(bgp_stats_dc
  bgp_stats_base
  fb303::fb303
  ${BGP_BASE_LIBS}
)

add_library(bgp_fsdb_fib_watcher
  neteng/fboss/bgp/cpp/nexthopTracker/FsdbFibWatcher.cpp
)

target_link_libraries(bgp_fsdb_fib_watcher
  bgp_common
  bgp_nexthop
  bgp_stats_dc
  ${BGP_THRIFT_LIBS}
  ${BGP_BASE_LIBS}
  thrift::rpc
)

add_library(bgp_fib_fboss
  neteng/fboss/bgp/cpp/rib/FibFboss.cpp
)

target_link_libraries(bgp_fib_fboss
  bgp_common
  bgp_fib_holddown
  bgp_fib_out
  bgp_lib_core
  cfgr_fboss_common_cpp2
  thrift::rpc
  ${BGP_BASE_LIBS}
)

add_library(bgp_canonical_rib
  neteng/fboss/bgp/cpp/fsdb/CanonicalRibExporter.cpp
  neteng/fboss/bgp/cpp/rib/canonical/CanonicalRibEncoder.cpp
)

target_link_libraries(bgp_canonical_rib
  bgp_common
  bgp_config_policy
  bgp_lib_core
  bgp_rib_base
  ${BGP_THRIFT_LIBS}
  ${BGP_BASE_LIBS}
  thrift::rpc
)

add_library(bgp_fsdb_syncer
  neteng/fboss/bgp/cpp/fsdb/FsdbSyncer.cpp
)

target_link_libraries(bgp_fsdb_syncer
  bgp_canonical_rib
  bgp_stats_dc
  fb303::fb303
  ${BGP_THRIFT_LIBS}
  ${BGP_BASE_LIBS}
  thrift::rpc
)

add_library(bgp_rib_dc
  neteng/fboss/bgp/cpp/rib/RibDC.cpp
)

target_link_libraries(bgp_rib_dc
  bgp_canonical_rib
  bgp_fib_fboss
  bgp_fib_out_thrift
  bgp_fsdb_syncer
  bgp_rib_base
  bgp_stats_dc
)

add_library(bgp_neighbor_watcher
  neteng/fboss/bgp/cpp/peer/NeighborWatcher.cpp
)

target_link_libraries(bgp_neighbor_watcher
  bgp_fsdb_fib_watcher
  bgp_peer_base
  bgp_stats_dc
)

add_library(bgp_service_dc
  neteng/fboss/bgp/cpp/BgpServiceDC.cpp
)

target_link_libraries(bgp_service_dc
  bgp_neighbor_watcher
  bgp_rib_dc
  bgp_service_base
  bgp_stats_dc
)

install(TARGETS
  bgp_stats_dc
  bgp_fsdb_fib_watcher
  bgp_fib_fboss
  bgp_canonical_rib
  bgp_fsdb_syncer
  bgp_rib_dc
  bgp_neighbor_watcher
  bgp_service_dc
  DESTINATION lib
)

add_library(bgp_main_util_dc
  neteng/fboss/bgp/cpp/MainUtilDC.cpp
)

target_link_libraries(bgp_main_util_dc
  thrift::rpc
  bgp_config_cpp2
  bgp_stats_base
  ${BGP_BASE_LIBS}
)

add_executable(bgp_bin
  neteng/fboss/bgp/cpp/MainOSS.cpp
)

# The BGP++/FSDB thrift libraries and the FBOSS static archives cross-reference
# each other. Wrap both sets in a RESCAN link group so the linker iterates until
# all references are resolved.
set(BGP_DC_LINK_GROUP_LIBS ${BGP_THRIFT_LIBS} ${BGP_DC_FBOSS_LIBS})
list(JOIN BGP_DC_LINK_GROUP_LIBS "," BGP_DC_LINK_GROUP_LIBS_CSV)

target_link_libraries(bgp_bin
  bgp_main_util_dc
  bgp_service_dc
  bgp_stats_base
  bgp_stats_dc
  build_info
  "$<LINK_GROUP:RESCAN,${BGP_DC_LINK_GROUP_LIBS_CSV}>"
  fb303::fb303
  ${BGP_BASE_LIBS}
  thrift::rpc
  ${FOLLY_EXCEPTION_TRACER}
)

set_target_properties(bgp_bin PROPERTIES OUTPUT_NAME bgp)
install(TARGETS bgp_bin DESTINATION sbin)
