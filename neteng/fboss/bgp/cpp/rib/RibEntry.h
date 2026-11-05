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

#include <cstdint>
#include <memory>

#include <folly/Function.h>
#include <folly/IPAddress.h>
#include <folly/IntrusiveList.h>
#include <folly/logging/xlog.h>

#include "neteng/fboss/bgp/cpp/common/BgpPath.h"
#include "neteng/fboss/bgp/cpp/common/RouteInfo.h"
#include "neteng/fboss/bgp/cpp/common/Structs.h"
#include "neteng/fboss/bgp/cpp/common/Utils.h"
#include "neteng/fboss/bgp/cpp/config/ConfigStructs.h"
#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopCache.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopInfo.h"
#include "neteng/fboss/bgp/cpp/rib/FibOut.h"
#include "neteng/fboss/bgp/cpp/rib/RouteInfoSelector.h"

namespace facebook::bgp {

class PathSelectionPolicy;

class RibEntry {
 public:
  using UnlinkRouteInfoFn = folly::FunctionRef<bool(RouteInfo&)>;

  explicit RibEntry(const folly::CIDRNetwork& prefix) : prefix_(prefix) {}

  /*
   * [Accessor Methods]
   */
  const folly::CIDRNetwork& getPrefix() const {
    return prefix_;
  }

  // best-path calculated V.S. advertised(may lag behind)
  std::shared_ptr<RouteInfo> getBestPath() const {
    return bestpath_;
  }
  /*
   * Raw best path for read-only use without copying the owning shared_ptr
   * (avoids refcount traffic on the per-RibEntry best-path hot path).
   */
  const RouteInfo* getBestPathRaw() const {
    return bestpath_.get();
  }
  std::shared_ptr<RouteInfo> getAdvertisedBestPath() const {
    return advertisedBestpath_;
  }

  // multi-path calculated V.S. advertised(may lag behind)
  const folly::F14NodeMap<uint32_t, std::shared_ptr<RouteInfo>>& getMultipaths()
      const {
    return multipaths_;
  }
  const folly::F14NodeMap<uint32_t, std::shared_ptr<RouteInfo>>&
  getAdvertisedMultipaths() const {
    return advertisedMultipaths_;
  }

  // nexthops with weight calculated V.S. advertised(may lag behind)
  std::shared_ptr<const WeightedNexthopMap> getMultipathWeightedNexthops()
      const {
    return weightedNexthops_;
  }
  std::shared_ptr<const WeightedNexthopMap>
  getAdvertisedMultipathWeightedNexthops() const {
    return advertisedWeightedNexthops_;
  }

  // routeInfo(1-1 mapping to BgpPath) accessor methods
  std::vector<std::shared_ptr<RouteInfo>> getAllPaths() const {
    std::vector<std::shared_ptr<RouteInfo>> vec;
    for (const auto& itr : routeInfos_) {
      for (const auto& routeIter : itr.second) {
        vec.emplace_back(routeIter.second);
      }
    }
    return vec;
  }

  // retrieve size of all paths received from all peers
  // O(1): maintained incrementally by updatePath(), the sole mutator of
  // routeInfos_
  uint64_t getAllPathsCnt() const {
    return allPathsCount_;
  }

  /*
   * Paths of this prefix that the last path-selection pass excluded from
   * best-path selection because their nexthops were unresolved. Zero until
   * selection has run for this entry.
   */
  uint32_t getInactivePathCnt() const {
    return inactivePathCount_;
  }

  // retrieve paths received from one dedicated peer
  folly::F14NodeMap<uint32_t, std::shared_ptr<RouteInfo>> getRouteInfos(
      const nettools::bgplib::BgpPeerId& peer) {
    auto kv = routeInfos_.find(peer);
    if (kv != routeInfos_.end()) {
      return kv->second;
    }
    return {};
  }

  /**
   * Helper method to get a specific RouteInfo by peer and path ID.
   *
   * @param peer The peer ID
   * @param receivedPathId The path ID
   * @return A pointer to the RouteInfo if found, nullptr otherwise
   */
  std::shared_ptr<RouteInfo> getRouteInfo(
      const nettools::bgplib::BgpPeerId& peer,
      uint32_t receivedPathId) const {
    auto kv = routeInfos_.find(peer);
    if (kv != routeInfos_.end()) {
      auto routeInfoIter = kv->second.find(receivedPathId);
      if (routeInfoIter != kv->second.end()) {
        return routeInfoIter->second;
      }
    }
    return nullptr;
  }

  std::string getMultipathNexthopsStr() const;

  // GAR(Global Adaptive Routing) functionality with topo information
  std::shared_ptr<const NexthopTopoInfoMap> getNexthopTopoInfoMap() const {
    return nexthopsAndTopoInfo_;
  }

  // TODO: reconcile this with per-path installToFib flag
  inline bool getInstallToFib() const {
    return installToFib_;
  }

  bool getIsPartialDrain() const {
    return isPartialDrain_;
  }

  /*
   * The configured BGP native min-nexthop threshold that triggered the
   * current partial-drain state, or 0 when isPartialDrain_ is false or
   * the trigger was aggregate LBW (use getAggLbwBpsThreshold() in that
   * case). Captured during selectBestPath() from PathSelectionPolicyResult
   * so the Thrift accessor can surface it without re-walking the policy.
   */
  int32_t getMnhThreshold() const {
    return mnhThreshold_;
  }

  /*
   * The configured aggregate-LBW threshold (bps) that triggered the
   * current partial-drain state, or 0 when isPartialDrain_ is false or
   * the trigger was MNH (use getMnhThreshold() in that case). Captured
   * during selectBestPath() from PathSelectionPolicyResult.
   */
  int64_t getAggLbwBpsThreshold() const {
    return aggLbwBpsThreshold_;
  }

  /**
   * Return aggregate UCMP weight of all ECMP paths. Will be none if any of the
   * ECMP path is missing the UCMP weight community.
   */
  inline std::optional<float> getAggregateReceivedUcmpWeight() const {
    if (aggregateReceivedUcmpWeight_) {
      return aggregateReceivedUcmpWeight_;
    }
    return std::nullopt;
  }

  /**
   * Return aggregate UCMP weight of all ECMP path-peers. Will be none if any
   * of the ECMP path-peer is missing link-bandwidth-bps in configuration.
   */
  inline std::optional<float> getAggregateLocalUcmpWeight() const {
    if (aggregateLocalUcmpWeight_) {
      return aggregateLocalUcmpWeight_;
    }
    return std::nullopt;
  }

  /**
   * Return rib policy set UCMP weight. Will be none if the weight is default
   * zero
   */
  inline std::optional<float> getRibPolicyUcmpWeight() const {
    if (ribPolicyUcmpWeight_ != 0) {
      return ribPolicyUcmpWeight_;
    }
    return std::nullopt;
  }

  /*
   * [Mutator Methods]
   */

  /*
   * commit best path selection when it's safe to advertise path back
   * return true if advertisedBestpath_ has changed or aggregated LBW changed
   * which indicates rib needs to advertise new path to peers
   */
  bool commitBestpath() {
    bool ret = (advertisedBestpath_ != bestpath_);
    ret |=
        (advertisedAggregateReceivedUcmpWeight_ !=
         aggregateReceivedUcmpWeight_);
    ret |= (advertisedAggregateLocalUcmpWeight_ != aggregateLocalUcmpWeight_);
    ret |= (advertisedRibPolicyUcmpWeight_ != ribPolicyUcmpWeight_);
    ret |= (advertisedIsPartialDrain_ != isPartialDrain_);

    advertisedAggregateReceivedUcmpWeight_ = aggregateReceivedUcmpWeight_;
    advertisedAggregateLocalUcmpWeight_ = aggregateLocalUcmpWeight_;
    advertisedRibPolicyUcmpWeight_ = ribPolicyUcmpWeight_;
    advertisedIsPartialDrain_ = isPartialDrain_;

    if (!advertisedBestpath_ && !bestpath_) {
      // if old and new bestpaths are nullptr don't bother advertise anything
      return false;
    }
    advertisedBestpath_ = bestpath_;
    return ret;
  }

  bool commitMultipathNexthops() {
    bool ret = (advertisedWeightedNexthops_ != weightedNexthops_);
    advertisedWeightedNexthops_ = weightedNexthops_;
    return ret;
  }

  bool multipathChanged() const {
    return advertisedMultipaths_ != multipaths_;
  }

  /*
   * only used in add path feature to help determine if multipaths has
   * been changed.
   */
  bool commitMultipaths() {
    bool ret = (advertisedMultipaths_ != multipaths_);
    advertisedMultipaths_ = multipaths_;
    return ret;
  }

  /**
   * Add, replace, or withdraw one path for a peer and received path ID.
   *
   * Announcement (attrs is not null):
   * - Add a new RouteInfo, or replace the existing one if its attributes
   *   changed.
   * - Link the new RouteInfo to nexthopInfo when provided.
   * - On replacement, unlink the old RouteInfo from its NexthopInfo.
   *
   * Withdrawal (attrs is null):
   * - Find the stored RouteInfo, unlink it from its NexthopInfo, and erase it.
   *
   * If a replacement or withdrawal removes a RouteInfo, unlinkRouteInfoFn
   * unlinks it. RibBase passes a callback that also deletes the NexthopInfo
   * when no route uses it. When no callback is provided, updatePath() only
   * unlinks the RouteInfo from its NexthopInfo.
   *
   * @return true if a path was added, replaced, or withdrawn; false if no path
   * changed.
   */
  bool updatePath(
      const TinyPeerInfo& peer,
      std::shared_ptr<const BgpPath> attrs,
      const bool installToFib = true,
      const uint32_t receivedPathId = kDefaultPathID,
      std::optional<facebook::bgp::NexthopInfo*> nexthopInfo = std::nullopt,
      UnlinkRouteInfoFn unlinkRouteInfoFn = {}) noexcept;

  /*
   * Path selection (selectBestPath + 7 phase helpers + PathSelectionInput /
   * MultiPathSelectionResult structs) lives on RibBase as static methods
   * taking RibEntry& by reference, gated by the RibBase friend declaration
   * below.
   */

  /*
   * Check if the entry is already on Fib batch processing list by checking
   * if the list hook is linked or not.
   */
  bool isOnFibBatchList() const {
    return fibBatchListHook_.is_linked();
  }

  /**
   * Set rib policy ucmp
   */
  inline void setRibPolicyUcmpWeight(const float ribPolicyUcmpWeight) {
    ribPolicyUcmpWeight_ = ribPolicyUcmpWeight;
  }

  inline bool needPathSelection() const {
    return needPathSelection_;
  }

  /** Returns whether this entry currently has FibOut state. */
  inline bool hasFibOutState() const {
    return hasFibOutState_;
  }

  /** Marks FibOut state as present for this entry. */
  inline void markFibOutStatePresent() {
    hasFibOutState_ = true;
  }

  /** Marks FibOut state as absent for this entry. */
  inline void markFibOutStateAbsent() {
    hasFibOutState_ = false;
  }

  /**
   * Get the RIB version stamped on this entry at its last queue emission.
   */
  inline uint64_t getRibVersion() const {
    return ribVersion_;
  }

  /**
   * Set the RIB version for this entry. Called by Rib at queue-emission time
   * (once per prefix), not at path selection.
   */
  inline void setRibVersion(uint64_t version) {
    ribVersion_ = version;
  }

  /*
   * Set the needPathSelection_ = true so that the entry will be picked up for
   * path selection when preparing the Fib programming
   */
  inline void requirePathSelection() {
    needPathSelection_ = true;
  }

  /**
   * @brief Override route's weighted next-hops with the supplied value.
   *
   * @param weightedNexthops  Pre-computed collection of next-hop addresses and
   * corresponding weights.
   */
  void overrideWeightedNexthops(WeightedNexthopMap& weigtedNexthops);

  /*
   * gets the first ID from this entry's free interval, to be assigned to
   * selected paths
   */
  uint32_t getPathIdToSend();

  /*
   * Returns path's pathIdToSend, first assigning one from getPathIdToSend() if
   * the path does not have one yet.
   */
  uint32_t getOrAssignSendPathId(const std::shared_ptr<RouteInfo>& path);

 private:
  void requirePathSelectionForNexthopResolvabilityChange() {
    needPathSelection_ = true;
    nexthopResolvabilityChangePending_ = true;
  }

  bool consumeNexthopResolvabilityChangePending() {
    const bool pending = nexthopResolvabilityChangePending_;
    nexthopResolvabilityChangePending_ = false;
    return pending;
  }

  // v4 or v6 prefix for this RibEntry
  const folly::CIDRNetwork prefix_;

  /*
   * core data structure containing BgpPath/BgpPeer/etc.
   * {peerId: {receivedPathID: routeInfo}}
   */
  folly::F14NodeMap<
      nettools::bgplib::BgpPeerId,
      folly::F14NodeMap<uint32_t, std::shared_ptr<RouteInfo>>>
      routeInfos_;

  /*
   * Cached total path count across all peers (sum of inner-map sizes).
   * Maintained by updatePath(); keeps getAllPathsCnt() O(1). uint32_t is
   * ample: per-prefix paths are bounded by peer count x path IDs, and a
   * count anywhere near this width would indicate far bigger problems.
   */
  uint32_t allPathsCount_{0};

  /*
   * Aggregated value of received UCMP weight of all ECMP paths. Will be 0 if
   * any ECMP path is missing the UCMP weight.
   */
  float aggregateReceivedUcmpWeight_{0};
  float advertisedAggregateReceivedUcmpWeight_{0};

  /*
   * Aggregated value of peer link bandwidth bps of all ECMP path-peers. Will
   * be 0 if any ECMP path-peer doesn't have link-bps specified
   */
  float aggregateLocalUcmpWeight_{0};
  float advertisedAggregateLocalUcmpWeight_{0};

  /*
   * link bandwidth bps set by rib policy. Will be 0 if no rib policy specified.
   */
  float ribPolicyUcmpWeight_{0};
  float advertisedRibPolicyUcmpWeight_{0};

  /*
   * This is set to true when:
   *  1. updatePath is called and any path is changed;
   *  2. requirePathSelection is called;
   *
   * NOTE: flag is set to false after selectBestPath is called.
   */
  bool needPathSelection_{true};
  bool nexthopResolvabilityChangePending_{false};

  /*
   * True while this live entry has FibOut state, including NONE. Kept
   * independently of fibOutState_ until FIB-out tracking is fully enabled.
   */
  bool hasFibOutState_{false};

  /*
   * Paths excluded from best-path selection by prePathSelectionFiltering,
   * rewritten on every pass. Cached per prefix so the RIB-wide aggregate in
   * RibCounters can be maintained by delta rather than by walking the RIB.
   *
   * Memory: occupies the alignment hole between hasFibOutState_ and the
   * shared_ptr below, so sizeof(RibEntry) is unchanged (0 bytes per entry vs.
   * 8 if placed elsewhere) -- same rationale as the partial-drain fields below.
   */
  uint32_t inactivePathCount_{0};

  // Multipath nexthop and associated weights
  std::shared_ptr<const WeightedNexthopMap> weightedNexthops_;
  std::shared_ptr<const WeightedNexthopMap> advertisedWeightedNexthops_;

  // Multipath nexthop and associated topology information
  std::shared_ptr<const NexthopTopoInfoMap> nexthopsAndTopoInfo_;

  // Last platform-normalized FIB request submitted for this live RIB entry.
  std::shared_ptr<const FibOutState> fibOutState_;

  // map of add path IDs to routeInfos
  folly::F14NodeMap<uint32_t, std::shared_ptr<RouteInfo>> multipaths_{};
  folly::F14NodeMap<uint32_t, std::shared_ptr<RouteInfo>>
      advertisedMultipaths_{};

  // bestpath routeInfo
  std::shared_ptr<RouteInfo> bestpath_;
  std::shared_ptr<RouteInfo> advertisedBestpath_;

  // Time stmap when the entry was first installed in local-RIB
  std::chrono::time_point<std::chrono::system_clock> installTimeStamp_;

  /*
   * false if bestpath is local route and install_to_fib is configured false;
   * true, otherwise.
   */
  bool installToFib_{true};

  // ucmp active indicator
  bool isUcmpActive_{false};

  /*
   * Partial drain state — set when drain_on_min_nexthop_violation is active
   * and a min capacity threshold (MNH or aggregate LBW) is violated. Placed
   * adjacent to installToFib_ / isUcmpActive_ so all four bools share a
   * single 8-byte word; this avoids introducing new padding (cost ≈ 0 bytes
   * per RibEntry vs. 8 bytes if placed elsewhere).
   */
  bool isPartialDrain_{false};
  bool advertisedIsPartialDrain_{false};

  /*
   * Capacity-threshold values captured at the moment isPartialDrain_ flips
   * true. Exactly one is non-zero per drain (the active criterion on the
   * matched policy statement); the other stays 0. Both reset to 0 in
   * selectBestPath() prior to recompute. Surfaced via
   * TPartiallyDrainedPrefix.min_capacity (TCapacity union) without
   * per-prefix policy lookup at RPC time.
   *
   * Memory: mnhThreshold_ fills the 4-byte alignment hole between the
   * partial-drain bool word and uint64_t ribVersion_ below (0 net cost vs.
   * pre-MNH state). aggLbwBpsThreshold_ adds 8 bytes per RibEntry
   * (+240MB at 30M routes) — justified by clean LBW threshold surfacing
   * for FSDB/NSDB consumers downstream.
   */
  int32_t mnhThreshold_{0};
  int64_t aggLbwBpsThreshold_{0};

  /*
   * RIB version when this entry was last updated (best path or multipath
   * change). Used for tracking routing table version per prefix.
   */
  uint64_t ribVersion_{0};

  folly::IntrusiveListHook fibBatchListHook_;

  /**
   * @brief Helper method to unlink a RouteInfo from a NexthopInfo
   *
   * @param routeInfo The RouteInfo to unlink
   * @param unlinkRouteInfoFn Optional callback that unlinks the RouteInfo
   * instead. RibBase passes a callback that also deletes the NexthopInfo when
   * no route uses it.
   */
  void unlinkRouteInfoFromNexthop(
      RouteInfo& routeInfo,
      UnlinkRouteInfoFn unlinkRouteInfoFn);

  /**
   * @brief Helper method to link a RouteInfo to a NexthopInfo
   *
   * @param routeInfo The RouteInfo to link
   * @param nexthopInfo Optional pointer to the NexthopInfo
   */
  void linkRouteInfoToNexthop(
      RouteInfo& routeInfo,
      std::optional<facebook::bgp::NexthopInfo*> nexthopInfo);

  /*
   * Rib needs to access this list hook for its operation; path-selection
   * helpers on the rib classes are static methods taking RibEntry& that
   * read/write RibEntry private state through these friendships.
   */
  friend class RibBase;
  friend class RibDC;

  /*
   * store a free ID interval for this prefix, starting as [minPathId,
   * maxPathId] (inclusive)
   */
  std::pair<uint32_t, uint32_t> freePathIdInterval_{
      kMinPathIDToSend,
      kMaxPathIDToSend};

#ifdef RibEntry_TEST_FRIENDS
  RibEntry_TEST_FRIENDS
#endif
};
} // namespace facebook::bgp
