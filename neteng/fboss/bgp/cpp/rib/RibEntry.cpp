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

#include <fmt/format.h>
#include <folly/logging/xlog.h>
#include <numeric>

#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopInfo.h"
#include "neteng/fboss/bgp/cpp/rib/RibEntry.h"
#include "neteng/fboss/bgp/cpp/rib/RibPolicy.h"
#include "neteng/fboss/bgp/cpp/rib/Utils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

using std::make_pair;
using std::make_shared;
using std::shared_ptr;
using std::vector;

namespace facebook::bgp {
bool RibEntry::updatePath(
    const TinyPeerInfo& peer,
    shared_ptr<const BgpPath> attrs,
    const bool installToFib,
    const uint32_t receivedPathId,
    std::optional<facebook::bgp::NexthopInfo*> nexthopInfo,
    UnlinkRouteInfoFn unlinkRouteInfoFn) noexcept {
  nettools::bgplib::BgpPeerId peerId{peer.addr, peer.routerId};
  // withdrawn path
  if (!attrs) {
    auto peerIt = routeInfos_.find(peerId);
    if (peerIt == routeInfos_.end()) {
      return false;
    }
    auto routeInfoIt = peerIt->second.find(receivedPathId);
    if (routeInfoIt == peerIt->second.end()) {
      return false;
    }
    unlinkRouteInfoFromNexthop(*routeInfoIt->second, unlinkRouteInfoFn);
    peerIt->second.erase(routeInfoIt);
    --allPathsCount_;
    needPathSelection_ = true;
    if (peerIt->second.empty()) {
      RibStats::decrRibPaths();
      routeInfos_.erase(peerIt);
    }
    return true;
  }

  // announced path
  auto iter = routeInfos_.find(peerId);
  std::optional<uint32_t> pathIdToSend = std::nullopt;
  std::shared_ptr<RouteInfo>* routeInfoToReplace{nullptr};

  // attributes did not change, no need for update
  if (iter != routeInfos_.end()) {
    auto pathIt = iter->second.find(receivedPathId);
    if (pathIt != iter->second.end()) {
      if (pathIt->second->attrs == attrs && pathIt->second->peer == peer) {
        return false;
      }
      // otherwise, proceed with update. Re-use allocated pathID
      pathIdToSend = pathIt->second->pathIdToSend;
      routeInfoToReplace = &pathIt->second;
    }
  }

  // Create the RouteInfo with ribEntry reference
  auto routeInfo = std::make_shared<RouteInfo>(
      prefix_, peer, attrs, receivedPathId, *this, pathIdToSend, installToFib);

  // Link the RouteInfo to the NexthopInfo if provided
  linkRouteInfoToNexthop(*routeInfo, nexthopInfo);

  if (routeInfoToReplace != nullptr) {
    auto replacedRouteInfo = std::move(*routeInfoToReplace);
    *routeInfoToReplace = std::move(routeInfo);
    unlinkRouteInfoFromNexthop(*replacedRouteInfo, unlinkRouteInfoFn);
  } else if (iter == routeInfos_.end()) {
    folly::F14NodeMap<uint32_t, std::shared_ptr<RouteInfo>> info{
        {receivedPathId, std::move(routeInfo)}};
    // first time receive a route from peer for this prefix
    RibStats::incrRibPaths();
    routeInfos_.emplace(peerId, std::move(info));
    ++allPathsCount_;
  } else {
    if (iter->second.insert_or_assign(receivedPathId, std::move(routeInfo))
            .second) {
      ++allPathsCount_;
    }
  }
  needPathSelection_ = true;
  return true;
}

/*
 * Path selection (selectBestPath + 7 phase helpers + PathSelectionInput /
 * MultiPathSelectionResult structs) lives on RibBase as static methods
 * taking RibEntry& by reference. See RibBase::selectBestPath in RibBase.cpp.
 */

std::string RibEntry::getMultipathNexthopsStr() const {
  std::stringstream sstream;
  sstream << "[";
  if (weightedNexthops_) {
    for (const auto& nhwtPair : *weightedNexthops_) {
      if (&nhwtPair != &(*weightedNexthops_->cbegin())) {
        sstream << ", ";
      }
      sstream << "(" << nhwtPair.first.str() << ":" << nhwtPair.second << ")";
    }
  }
  sstream << "]";
  return sstream.str();
}

/**
 * @brief Override route's weighted next-hops with the supplied value.
 *
 * @param weightedNexthops Pre-computed collection of next-hop addresses and
 * corresponding weights.
 */
void RibEntry::overrideWeightedNexthops(WeightedNexthopMap& weigtedNexthops) {
  weightedNexthops_ =
      make_shared<WeightedNexthopMap>(std::move(weigtedNexthops));
}

void RibEntry::unlinkRouteInfoFromNexthop(
    RouteInfo& routeInfo,
    UnlinkRouteInfoFn unlinkRouteInfoFn) {
  const bool unlinked = unlinkRouteInfoFn ? unlinkRouteInfoFn(routeInfo)
                                          : routeInfo.unlinkFromNexthopInfo();
  if (!unlinked) {
    return;
  }
  const auto nexthop = routeInfo.attrs->getNexthop();
  XLOGF(
      DBG2,
      "Unlinked RouteInfo for prefix {} from NexthopInfo for {}",
      folly::IPAddress::networkToString(prefix_),
      nexthop.str());
}

void RibEntry::linkRouteInfoToNexthop(
    RouteInfo& routeInfo,
    std::optional<facebook::bgp::NexthopInfo*> nexthopInfo) {
  if (nexthopInfo.has_value() && *nexthopInfo != nullptr) {
    (*nexthopInfo)->linkRouteInfo(routeInfo);
    XLOGF(
        DBG2,
        "Linked RouteInfo for prefix {} to NexthopInfo for {}",
        folly::IPAddress::networkToString(prefix_),
        (*nexthopInfo)->getNextHop().str());
  }
}

/*
 * when a path ID is requested, return the left element of the Rib Entry's free
 * interval and then increment it. The interval is exhausted when the left
 * element exceeds the right one, in which case a new free interval would need
 * to be determined
 */
uint32_t RibEntry::getPathIdToSend() {
  /*
   * if the interval has no remaining free path IDs, we need to find the largest
   * free interval among assigned path IDs
   */
  if (freePathIdInterval_.first > freePathIdInterval_.second) {
    freePathIdInterval_ = findLargestFreePathIdInterval(
        routeInfos_, kMinPathIDToSend, kMaxPathIDToSend);
  }

  // take the ID out of the interval and return it
  return freePathIdInterval_.first++;
}

uint32_t RibEntry::getOrAssignSendPathId(
    const std::shared_ptr<RouteInfo>& path) {
  if (!path->pathIdToSend.has_value()) {
    path->pathIdToSend = getPathIdToSend();
  }
  return path->pathIdToSend.value();
}

} // namespace facebook::bgp
