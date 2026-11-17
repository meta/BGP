/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <fmt/format.h>

#include "fboss/agent/Constants.h"

namespace facebook::bgp {

using GracefulRestartFlag = bool;
using AsNum = uint32_t;
using RrClientConfigured = bool;
using NextHopSelfConfigured = bool;
using ConfedPeerConfigured = bool;
using RemovePrivateAsConfigured = bool;
using AfiLsConfigured = bool;
using AfiIpv4Configured = bool;
using AfiIpv6Configured = bool;
using AfiLsNegotiated = bool;
using AfiIpv4Negotiated = bool;
using AfiIpv6Negotiated = bool;
using EnableStatefulHa = bool;
using ComputeUcmpFromLbwComm = bool;
using ValidateRemoteAs = bool;
using SupportStatefulGr = bool;
using EnableServerSocket = bool;
using AllowLoopbackReflection = bool;
using V4OverV6Nexthop = bool;
using IsRedistributePeer = bool;
using AfiIpv4LUConfigured = bool;
using AfiIpv6LUConfigured = bool;
using CountConfedsInAsPathLen = bool;
using EnhancedRouteRefreshConfigured = bool;
using EnhancedRouteRefreshNegotiated = bool;
using RouteRefreshConfigured = bool;
using RouteRefreshNegotiated = bool;
using EnableNexthopTracking = bool;
using EnableNetlinkDampening = bool;
using EnableDynamicPolicyEvaluation = bool;

using fboss::VrfId;

enum class BgpSessionType { EBGP, IBGP, ConfedEBGP };

inline const char* toString(BgpSessionType type) {
  switch (type) {
    case BgpSessionType::EBGP:
      return "EBGP";
    case BgpSessionType::IBGP:
      return "IBGP";
    case BgpSessionType::ConfedEBGP:
      return "ConfedEBGP";
  }
  return "UNKNOWN";
}

enum class BgpRouteType { EBGP, IBGP, ConfedEBGP, LOCAL, UNKNOWN };

// Used for CLI to display various types of routes (pre/post in/out)
enum class RouteFilterType {
  PRE_FILTER_RECEIVED,
  POST_FILTER_RECEIVED,
  PRE_FILTER_ADVERTISED,
  POST_FILTER_ADVERTISED,
};

/**
 * Update Group State Machine State
 */
enum class UpdateGroupState {
  UNINITIALIZED = 0, // Created but not started initial dump
  IDLE = 1, // No pending packing list, no pending changes
  READY = 2, // No pending packing list, changes waiting (MRAI timer)
  WAITING = 3, // Pending packing list to drain
};

/**
 * Peer Update State Machine state within Update Group
 */
enum class PeerUpdateState {
  DOWN = 0, // Default state when peer is created
  INIT = 1, // Peer has initial rib dump scheduled(not joined group yet)
  JOINED_RUNNING = 2, // Peer joined the group and running without backpressure
  JOINED_BLOCKED = 3, // Peer joined the group and hitting backpressure
  DETACHED_INIT_DUMP = 4, // Peer waiting for initial dump to complete
  DETACHED_READY_TO_JOIN = 5, // Peer fulfilled conditions to rejoin the group
  DETACHED_BLOCKED = 6, // Peer is backpressured and run in detached mode
  DETACHED_RUNNING = 7, // Peer is running independently in detached mode
};

} // namespace facebook::bgp

template <>
struct fmt::formatter<facebook::bgp::UpdateGroupState>
    : fmt::formatter<std::string_view> {
  auto format(facebook::bgp::UpdateGroupState state, fmt::format_context& ctx)
      const {
    using facebook::bgp::UpdateGroupState;
    std::string_view name;
    switch (state) {
      case UpdateGroupState::UNINITIALIZED:
        name = "UNINITIALIZED";
        break;
      case UpdateGroupState::IDLE:
        name = "IDLE";
        break;
      case UpdateGroupState::READY:
        name = "READY";
        break;
      case UpdateGroupState::WAITING:
        name = "WAITING";
        break;
    }
    return fmt::formatter<std::string_view>::format(name, ctx);
  }
};

template <>
struct fmt::formatter<facebook::bgp::PeerUpdateState>
    : fmt::formatter<std::string_view> {
  auto format(facebook::bgp::PeerUpdateState state, fmt::format_context& ctx)
      const {
    using facebook::bgp::PeerUpdateState;
    std::string_view name;
    switch (state) {
      case PeerUpdateState::DOWN:
        name = "DOWN";
        break;
      case PeerUpdateState::INIT:
        name = "INIT";
        break;
      case PeerUpdateState::JOINED_RUNNING:
        name = "JOINED_RUNNING";
        break;
      case PeerUpdateState::JOINED_BLOCKED:
        name = "JOINED_BLOCKED";
        break;
      case PeerUpdateState::DETACHED_INIT_DUMP:
        name = "DETACHED_INIT_DUMP";
        break;
      case PeerUpdateState::DETACHED_READY_TO_JOIN:
        name = "DETACHED_READY_TO_JOIN";
        break;
      case PeerUpdateState::DETACHED_BLOCKED:
        name = "DETACHED_BLOCKED";
        break;
      case PeerUpdateState::DETACHED_RUNNING:
        name = "DETACHED_RUNNING";
        break;
    }
    return fmt::formatter<std::string_view>::format(name, ctx);
  }
};
