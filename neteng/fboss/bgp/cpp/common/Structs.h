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

#include <folly/IPAddress.h>

#include "neteng/fboss/bgp/cpp/common/Types.h"
#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"

namespace facebook::bgp {

/*
 * used for communication between AdjRib and Rib
 * NOTE: Any change in TinyPeerInfo might have impact on TTinyPeerInfo
 * in openr/prefix-manager/facebook/if/Bgp.thrift
 */
struct TinyPeerInfo {
  const folly::IPAddress addr;
  const uint32_t asn{0};
  const uint32_t routerId{0};
  const BgpSessionType sessionType;
  const bool isRrClient{false};
  const bool isRedistributePeer{false};
  const std::optional<float> ucmpWeight;
  const std::string description;

  TinyPeerInfo(
      const folly::IPAddress& addr,
      uint32_t asn,
      uint32_t routerId,
      const BgpSessionType& sessionType,
      bool isRrClient,
      bool isRedistributePeer = false,
      std::optional<float> ucmpWeight = std::nullopt,
      const std::string& description = "")
      : addr(addr),
        asn(asn),
        routerId(routerId),
        sessionType(sessionType),
        isRrClient(isRrClient),
        isRedistributePeer(isRedistributePeer),
        ucmpWeight(ucmpWeight),
        description(description) {}

  inline bool operator==(const TinyPeerInfo& other) const {
    return (addr == other.addr) && (asn == other.asn) &&
        (routerId == other.routerId) && (sessionType == other.sessionType) &&
        (isRrClient == other.isRrClient) &&
        (isRedistributePeer == other.isRedistributePeer) &&
        (ucmpWeight == other.ucmpWeight) && (description == other.description);
  }
};

struct NeighborEventMsg {
  // Routing table of the interface on which the neighbor was learned.
  const VrfId vrfId;
  const folly::IPAddress nbrAddr;
  const bool isUp;

  NeighborEventMsg(VrfId vrfId, const folly::IPAddress& nbrAddr, bool isUp)
      : vrfId(vrfId), nbrAddr(nbrAddr), isUp(isUp) {}
};

struct NeighborReachabilityMsg {};

// used for communication between NeighborWatcher and PeerManagerBase
using NeighborWatcherMessage =
    std::variant<NeighborEventMsg, NeighborReachabilityMsg>;

// Map of Nexthop address to weight
using WeightedNexthopMap = folly::F14NodeMap<folly::IPAddress, uint32_t>;

/*
 * Map of Nexthop address to topology information map
 * Example: {1.2.3.4/32: {"rack_id": 1, "plane_id: 2", "spine_capacity": 36,
 * "remote_pod_capacity": 36}}
 * The second level key is the Thrift field names of the encoding scheme
 */
using NexthopTopoInfoMap = std::
    unordered_map<folly::IPAddress, std::unordered_map<std::string, int64_t>>;

/*
 * Store information of all operations that can cause RIB bestpath calculation
 * and FIB programming to be paused or resumed.
 */
enum RibPauseResumeCause {
  SAFE_MODE = 0,
  ROUTE_CHURN = 2,
  ROUTE_FILTER_POLICY_UPDATE = 3,
  ROUTING_POLICY_UPDATE = 4,
  BACKPRESSURE = 5,
};

} // namespace facebook::bgp
