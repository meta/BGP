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

#include "neteng/fboss/bgp/cpp/common/Types.h"
#include "neteng/fboss/bgp/cpp/rib/FibOut.h"
#include "neteng/fboss/bgp/cpp/rib/RibCounters.h"
#include "neteng/fboss/bgp/cpp/rib/RibEntry.h"

namespace facebook::bgp {

class RibBase;

class RibVrf final {
 public:
  // FIB programming requests from each VRF; defined after RibVrf.
  struct VrfProgrammingRequests;

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

  FibNexthopSets& fibNexthopSets() noexcept {
    return fibNexthopSets_;
  }
  const FibNexthopSets& fibNexthopSets() const noexcept {
    return fibNexthopSets_;
  }

  // Clears ribEntries_ and fibNexthopSets_, then resets ribCounters_.
  void clear();

  /*
   * Removes the entry: erases its FIB-out state, then erases it from
   * ribEntries_ and, if it was there, releases its counts from ribCounters_.
   */
  void remove(RibEntry& entry);

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

  // ======================== Counters ========================

  /*
   * Single authoritative aggregate of this VRF's counts. Only the default VRF's
   * counts are mirrored to fb303 ODS counters. Mutated only on the RIB
   * EventBase alongside ribEntries_.
   */
  RibCounters ribCounters_;

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
