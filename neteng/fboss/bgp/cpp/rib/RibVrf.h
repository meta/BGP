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

#include <memory>
#include <optional>

#include <folly/IPAddress.h>
#include <folly/IntrusiveList.h>
#include <folly/container/F14Map.h>
#include <folly/coro/Promise.h>
#include <folly/coro/Task.h>
#include <folly/io/async/AsyncTimeout.h>

#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/common/Types.h"
#include "neteng/fboss/bgp/cpp/rib/FibOut.h"
#include "neteng/fboss/bgp/cpp/rib/LocalRoute.h"
#include "neteng/fboss/bgp/cpp/rib/RibCounters.h"
#include "neteng/fboss/bgp/cpp/rib/RibEntry.h"

namespace facebook::bgp {

class Fib;
class RibBase;

class RibVrf final {
 public:
  // FIB programming requests from each VRF; defined after RibVrf.
  struct VrfProgrammingRequests;

  // Aggregate (locally originated) route updates: prefix and attrs, or null.
  using AggregateRoutes = std::vector<
      std::pair<folly::CIDRNetwork, std::shared_ptr<const BgpPath>>>;

  RibVrf(VrfId vrfId, RibBase& owner);
  RibVrf(const RibVrf&) = delete;
  RibVrf& operator=(const RibVrf&) = delete;
  RibVrf(RibVrf&&) = delete;
  RibVrf& operator=(RibVrf&&) = delete;

  ~RibVrf() = default;

  VrfId vrfId() const {
    return vrfId_;
  }

  folly::F14NodeMap<folly::CIDRNetwork, RibEntry>& ribEntries() noexcept {
    return ribEntries_;
  }
  const folly::F14NodeMap<folly::CIDRNetwork, RibEntry>& ribEntries()
      const noexcept {
    return ribEntries_;
  }

  folly::IntrusiveList<RibEntry, &RibEntry::fibBatchListHook_>&
  fibBatchList() noexcept {
    return fibBatchList_;
  }
  const folly::IntrusiveList<RibEntry, &RibEntry::fibBatchListHook_>&
  fibBatchList() const noexcept {
    return fibBatchList_;
  }

  // Adds the entry to fibBatchList_ unless it is already on it.
  void addToFibBatch(RibEntry& entry) noexcept;

  /*
   * Adds a FIB update for every entry on fibBatchList_ to the FIB batch,
   * records each entry's FIB-out state, then clears fibBatchList_. Returns the
   * number of entries added.
   */
  size_t populateFibBatch(Fib* FOLLY_NONNULL fib);

  FibNexthopSets& fibNexthopSets() noexcept {
    return fibNexthopSets_;
  }
  const FibNexthopSets& fibNexthopSets() const noexcept {
    return fibNexthopSets_;
  }

  /*
   * Clears ribEntries_ and fibNexthopSets_, resets ribCounters_, then clears
   * localRoutes_ and conditionalLocalRoutes_.
   */
  void clear();

  /*
   * Applies one RIB-in announcement (attrs set) or withdrawal (attrs null) to
   * the prefix's RibEntry and returns the aggregate route updates it causes.
   * nexthopInfo is the tracked NexthopInfo of attrs, or null when the nexthop
   * is not tracked; unlinkRouteInfoFn unlinks a removed path from its nexthop.
   */
  AggregateRoutes processSingleRibInUpdate(
      const TinyPeerInfo& peer,
      std::shared_ptr<const BgpPath> attrs,
      const PrefixPathId& pfxPid,
      NexthopInfo* FOLLY_NULLABLE nexthopInfo,
      RibEntry::UnlinkRouteInfoFn unlinkRouteInfoFn) noexcept;

  /*
   * Removes the entry: erases its FIB-out state, then erases it from
   * ribEntries_ and, if it was there, releases its counts from ribCounters_.
   */
  void remove(RibEntry& entry);

  folly::F14NodeMap<folly::CIDRNetwork, LocalRoute>& localRoutes() noexcept {
    return localRoutes_;
  }
  const folly::F14NodeMap<folly::CIDRNetwork, LocalRoute>& localRoutes()
      const noexcept {
    return localRoutes_;
  }

  folly::F14NodeMap<folly::IPAddress, std::vector<folly::CIDRNetwork>>&
  conditionalLocalRoutes() noexcept {
    return conditionalLocalRoutes_;
  }
  const folly::F14NodeMap<folly::IPAddress, std::vector<folly::CIDRNetwork>>&
  conditionalLocalRoutes() const noexcept {
    return conditionalLocalRoutes_;
  }

  RibCounters& ribCounters() noexcept {
    return ribCounters_;
  }
  const RibCounters& ribCounters() const noexcept {
    return ribCounters_;
  }

  /** Replace one entry's FIB-out state with a canonical nexthop set. */
  void replaceFibOut(RibEntry& entry, FibOutState state);

  /** Remove one entry's FIB-out state on the RIB event-base thread. */
  void eraseFibOut(RibEntry& entry);

  /** Return an entry's submitted FIB-out state, or null if none exists. */
  const FibOutState* FOLLY_NULLABLE findFibOut(const RibEntry& entry) const;

  std::unique_ptr<folly::AsyncTimeout>& fibBatchTimer() noexcept {
    return fibBatchTimer_;
  }

  std::unique_ptr<folly::AsyncTimeout>& nexthopResolutionTimer() noexcept {
    return nexthopResolutionTimer_;
  }

  /*
   * Schedule the prepareFibProgramming timer.
   *
   * This function is used to schedule the prepareFibProgramming function to be
   * called after a certain amount of time. The time is usually 200ms by
   * default. If the fib churn is high, a longer timeout of
   * kFibBatchTimeoutChurn is used instead. Skipped while the owner has paused
   * best path selection and FIB programming.
   */
  void schedulePrepareFibProgrammingTimer() noexcept;

  bool fullSync() const noexcept {
    return fullSync_;
  }
  void setFullSync(bool fullSync) noexcept {
    fullSync_ = fullSync;
  }

  bool pendingInitialPathComputation() const noexcept {
    return pendingInitialPathComputation_;
  }
  void setPendingInitialPathComputation(bool pending) noexcept {
    pendingInitialPathComputation_ = pending;
  }

  uint64_t ribVersion() const noexcept {
    return ribVersion_;
  }

  /**
   * Bump the RIB version counter and return the new value. Called at
   * queue-emission time -- once per emitted prefix, and again at each
   * kRibChunkSize chunk boundary. Also advances the published RIB table-version
   * stat.
   *
   * ribVersion is stamped on every RibOut{Announcement,Withdrawal} entry and
   * consumed by AdjRib / AdjRibOutGroup (notably AdjRibGroup::isEntryShared) to
   * decide how caught-up each peer/group is. It is stamped at emission time
   * (here, and in handleFibProgrammedMessage / prepareFibProgramming), NOT at
   * path selection, so a prefix's version reflects the order its entries are
   * pushed onto ribOutQ_. The emit sites in RibBase.cpp reference three rules
   * by name; they are defined here:
   *
   *   Rule 1: a prefix's paths emitted within one chunk share one version.
   *   Rule 2: a prefix whose paths span more than one chunk strictly increases
   *           across those chunks.
   *   Rule 3: different prefixes within a message strictly increase.
   *
   * Net: non-decreasing within a chunk, strictly increasing across
   * chunks/messages. This is what isEntryShared relies on so a detached peer is
   * never wrongly treated as having already seen a post-detach entry.
   */
  uint64_t incrementRibVersion() noexcept {
    /*
     * Temporarily commented out until we fix the RIB versioning in RIB. The
     * bgpcpp.rib.tableVersion counter is advanced by
     * PeerManagerBase::incrementMaxRibVersion() in the meantime, so it reports
     * the sequence peers' cached versions are stamped from.
     *
     * RibStats::incrementRibTableVersion();
     */
    // Plain increment: ribVersion_ is mutated only on the RIB event base.
    return ++ribVersion_;
  }

 private:
  /*
   * Removes the entry when a withdrawal leaves it with no paths and no FIB-out
   * state, i.e. it was never programmed.
   */
  void checkWithdrawalBeforeRouteProgrammed(
      folly::CIDRNetwork& prefix,
      RibEntry& entry) noexcept;

  /*
   * @brief: helper function called in processSingleRibInUpdate
   * @return:
   *  1. local route announcement if >= minimum_supporting_routes number of
   *     subnet are processed.
   *  2. local routes withdrawal if < minimum_supporting_routes number of
   *     subnet are processed.
   */
  AggregateRoutes aggregateRoute(
      const folly::CIDRNetwork& prefix,
      bool supportingRouteAnnouncement) noexcept;

  // ======================== Identity ========================

  // Identifies the routing table owned by this context.
  const VrfId vrfId_;

  // RIB that owns this VRF.
  RibBase& owner_;

  // ======================== RIB state =======================

  // prefix -> RibEntry map
  folly::F14NodeMap<folly::CIDRNetwork, RibEntry> ribEntries_;

  /*
   * The list to store all RibEntry that have been updated and needs to
   * be programmed in Fib and/or send to peers.
   *
   * Using a non-intrusive list requires storing either a key or a shared
   * pointer of the entry. It could contribute to some performance overload.
   * Using folly::IntrusiveList could help in this case. Measurement shows
   * IntrusiveList has about 4x performance gain on constructing the list
   * and 7x gain on looping, comparing with std::forward_list.
   *
   * Declared after ribEntries_ so it is destroyed first.
   */
  folly::IntrusiveList<RibEntry, &RibEntry::fibBatchListHook_> fibBatchList_;

  // ======================== FIB batch =======================

  // fib update with batch processing
  std::unique_ptr<folly::AsyncTimeout> fibBatchTimer_;

  // ===================== FIB programming ====================

  /*
   * Set when prepareFibProgramming is asked for a full sync. Cleared only when
   * the FIB loop triggers that full sync.
   */
  bool fullSync_{false};

  /*
   * Links this VRF into VrfProgrammingRequests::requests while it is pending.
   * The hook's destructor unlinks it automatically, so no special lifecycle
   * management is needed.
   */
  folly::IntrusiveListHook programmingRequestHook_;

  // ========================= FIB-out ========================

  // Canonical immutable nexthop sets referenced by FIB-out route state.
  FibNexthopSets fibNexthopSets_;

  // ====================== Local routes ======================

  // Locally originated routes of this VRF, keyed by prefix.
  folly::F14NodeMap<folly::CIDRNetwork, LocalRoute> localRoutes_;

  /*
   * There could be more than one route pointing to the same nexthop ip,
   * hence we store a map of nexthop ip to a list of routes in localRoutes_.
   * Routes in each list conditionally originate based on resolution of the
   * nexthop ip
   */
  folly::F14NodeMap<
      folly::IPAddress /* nexthop ip */,
      std::vector<folly::CIDRNetwork> /* route */>
      conditionalLocalRoutes_;

  // ======================== Counters ========================

  /*
   * Single authoritative aggregate of this VRF's counts. Only the default VRF's
   * counts are mirrored to fb303 ODS counters. Mutated only on the RIB
   * EventBase alongside ribEntries_.
   */
  RibCounters ribCounters_;

  // ===================== Initialization =====================

  /*
   * Set when a RibInInitialPathComputation with a non-zero
   * nexthopResolutionTimeout arrives but not all registered nexthops are
   * resolved yet. The initial full-sync is held off and run later by
   * maybeRunPendingInitialPathComputation() once nexthop resolution completes,
   * or by onNexthopResolutionTimeout() if nexthopResolutionTimer_ fires first.
   * RIB-thread only.
   */
  bool pendingInitialPathComputation_{false};

  /*
   * Bounds the deferred initial full-sync's wait for nexthop resolution. Armed
   * with the remaining EoR budget carried in RibInInitialPathComputation.
   */
  std::unique_ptr<folly::AsyncTimeout> nexthopResolutionTimer_;

  // ======================== RIB version =====================

  /*
   * RIB version counter - monotonically increasing value bumped once per prefix
   * at queue-emission time (see incrementRibVersion). Used for tracking how
   * caught up each peer is with RIB state. Written only on the RIB event base
   * (incrementRibVersion); the sole cross-thread reader (the thrift
   * getRibVersion handler) hops onto the RIB evb, so a plain uint64_t is
   * race-free -- no atomic needed, which keeps the per-prefix bumps a plain
   * increment on the hot path.
   */
  uint64_t ribVersion_{0};
};

/*
 * VRFs with FIB programming pending, read by RibBase's FIB loop. A VRF is
 * linked at most once. Used only on the RIB event base, so it takes no lock.
 */
struct RibVrf::VrfProgrammingRequests {
  VrfProgrammingRequests() = default;
  VrfProgrammingRequests(const VrfProgrammingRequests&) = delete;
  VrfProgrammingRequests& operator=(const VrfProgrammingRequests&) = delete;
  VrfProgrammingRequests(VrfProgrammingRequests&&) = delete;
  VrfProgrammingRequests& operator=(VrfProgrammingRequests&&) = delete;

  ~VrfProgrammingRequests() = default;

  // Links the VRF unless it is already linked, and wakes a waiting pop().
  void push(RibVrf& vrf) noexcept;

  /*
   * Waits until a VRF is linked, then unlinks it. Fails with OperationCancelled
   * when the awaiting coroutine is cancelled.
   */
  folly::coro::Task<RibVrf&> pop();

  folly::IntrusiveList<RibVrf, &RibVrf::programmingRequestHook_> requests;

 private:
  /*
   * Fulfilled by push() to wake pop(). Set only while pop() waits on an empty
   * list.
   */
  std::optional<folly::coro::Promise<void>> waiter_;
};

} // namespace facebook::bgp
