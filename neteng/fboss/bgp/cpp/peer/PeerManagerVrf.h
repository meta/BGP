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
#include <string>

#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/coro/AsyncScope.h>
#include <folly/coro/Task.h>

#include "neteng/fboss/bgp/cpp/adjrib/ShadowRibTypes.h"
#include "neteng/fboss/bgp/cpp/changeTracker/ChangeTracker.h"
#include "neteng/fboss/bgp/cpp/changeTracker/ConsumerBitmap.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/common/Types.h"
#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"

namespace facebook::bgp {

class AdjRib;
class PeerManagerBase;
struct PeeringParams;

/*
 * Mutable PeerManager state for one routing table. Runs on the owner's
 * EventBase.
 */
class PeerManagerVrf final {
 public:
  using PendingRouteRefreshAfis =
      folly::F14FastSet<nettools::bgplib::BgpUpdateAfi>;

  PeerManagerVrf(
      VrfId vrfId,
      PeerManagerBase& owner,
      folly::coro::CancellableAsyncScope& asyncScope);

  PeerManagerVrf(const PeerManagerVrf&) = delete;
  PeerManagerVrf& operator=(const PeerManagerVrf&) = delete;
  PeerManagerVrf(PeerManagerVrf&&) = delete;
  PeerManagerVrf& operator=(PeerManagerVrf&&) = delete;

  ~PeerManagerVrf() = default;

  VrfId vrfId() const {
    return vrfId_;
  }

  // ============================ SHADOW RIB STATE =============================

  // Current version of this table's shadow RIB. See @maxRibVersion_.
  uint64_t getMaxRibVersion() const noexcept {
    return maxRibVersion_;
  }

  std::shared_ptr<ChangeTracker<ShadowRibEntry>>&
  getChangeListTracker() noexcept {
    return changeListTracker_;
  }

  ShadowRibEntriesMap& getShadowRibEntries() noexcept {
    return shadowRibEntries_;
  }

  // Live view of @shadowRibEntries_ and @maxRibVersion_ for update groups.
  ShadowRibView getShadowRibView() const noexcept {
    return ShadowRibView{shadowRibEntries_, maxRibVersion_};
  }

  ConsumerBitmap& getAddPathConsumerBitmap() noexcept {
    return addPathConsumerBitmap_;
  }

  ConsumerBitmap& getNonAddPathConsumerBitmap() noexcept {
    return nonAddPathConsumerBitmap_;
  }

  /*
   * Get the appropriate consumer bitmap for a change.
   * - Bestpath changes use nonAddPathConsumerBitmap_
   * - Multipath changes use addPathConsumerBitmap_
   * ChangeTracker handles ORing if item is already on changelist
   */
  const ConsumerBitmap& getConsumerBitmapForChange(
      bool isBestpathChange) const noexcept;

  /*
   * @brief: create/update shadow rib entries to local shadowRib collection
   *
   * @param: RibOutAnnouncement from ribOutQ to create/update local collection
   */
  void handleShadowRibEntryAnnouncement(RibOutAnnouncement& announcement);

  /*
   * @brief: delete shadow rib entries to local shadowRib collection
   *
   * @param: RibOutWithdrawal from ribOutQ to remove local collection
   */
  void handleShadowRibEntryWithdrawal(RibOutWithdrawal& withdrawal);

  /*
   * @brief: util function to update the common attribute of shadow rib entries
   *
   * @param: srEntry - this is the ShadowRibEntry to be updated
   * @param: entry - this is RibOut entry used as the SoT
   *
   * The version stamped on @srEntry is @maxRibVersion_, which the entry's
   * chunk has already advanced. @entry.ribVersion is not used: the shadow RIB
   * versions its own contents.
   */
  void updateShadowRibEntryUtil(
      ShadowRibEntry& srEntry,
      const RibOutAnnouncementEntry& entry) const;

  /*
   * Walks this table's ShadowRIB and sends adjRib every entry it has not
   * already been sent through the change list, then activates its change list
   * consumer when update groups do not.
   */
  void processRibDumpReq(
      const std::shared_ptr<AdjRib>& adjRib,
      const RibDumpReq& ribDumpReq,
      bool sendWithEoR);

  /*
   * Whether a rib dump is pending for this peer in either form: buffered in
   * pendingRibDumpReqs_ (waiting for the drain) or scheduled / in flight on
   * asyncScope_ (its cancellation source is armed). Pure predicate.
   */
  bool isRibDumpScheduledForAdjRib(const std::shared_ptr<AdjRib>& adjRib) const;

  /*
   * Cancel a peer's pending rib dump regardless of state: drop a buffered
   * request from pendingRibDumpReqs_ and cancel an armed/in-flight one via the
   * AdjRib's cancellation source. Counterpart to scheduleRibDumpForAdjRib.
   */
  void cancelRibDumpForAdjRib(const std::shared_ptr<AdjRib>& adjRib);
  void cancelRibDumpForAdjRib(
      const std::shared_ptr<AdjRib>& adjRib,
      bool discardRouteRefreshRequests);

  /*
   * Schedule a rib dump for a peer on asyncScope_, but only if one is not
   * already scheduled or in flight (checked via isRibDumpScheduledForAdjRib).
   * The AdjRib owns the cancellation source so teardown can cancel the dump;
   * its token is passed into asyncScope_.add().
   */
  void scheduleRibDumpForAdjRib(const std::shared_ptr<AdjRib>& adjRib);
  void scheduleRibDumpForAdjRib(
      const std::shared_ptr<AdjRib>& adjRib,
      RibDumpReq ribDumpReq);

  void scheduleRouteRefreshForUpdateGroupPeer(
      const std::shared_ptr<AdjRib>& adjRib,
      nettools::bgplib::BgpUpdateAfi requestedAfi);
  void maybeSchedulePendingRouteRefresh(const std::shared_ptr<AdjRib>& adjRib);

  /*
   * Re-evaluate a single peer's egress policy inline: cancel any scheduled rib
   * dump, walk the shadow RIB, register a detached change-list consumer when
   * the cancelled dump was its only owner, then consume to the end so its
   * RIB-OUT is fully rebuilt within this event-loop turn. Does NOT clear the
   * peer's pending-egress-policy flag -- callers do that.
   */
  void processDetachedPeerEgressPolicyReEvaluation(
      const std::shared_ptr<AdjRib>& adjRib);

  /**
   * When sessionEstablished is called, the peer should eventually
   * receive the routes from this BGP speaker's RIB.
   * There are two vehicles to get the Rib dump.
   *
   * A) Peer calls for a RibDumpReq through PeerManagerBase who serves
   * the @shadowRibEntries_ that were received from RIB while handling
   * RibOutAnnouncements processed from ribOutQ_ in processRibOutMsgLoop.
   *
   * B) In the same method processRibOutMsgLoop, we are also sending
   * announcements to peers as we receive them.
   *
   * Case 1: The peer comes up before Rib has started initial announcement.
   * Then the peer should depend on vehicle B to get the RIB dump.
   *
   * Case 2: The peer comes up after Rib has finished initial announcement.
   * Then the peer should depend on vehicle A to get the RIB dump.
   *
   * Case 3: The peer comes up while Rib is in initial announcement.
   * Suppose Rib has announced the first K of N announcements.
   *
   * With vehicle A, peer will only get 1 to K and then EOR because shadow
   * RIB only has 1 to K.
   * With vehicle B, peer will only get K + 1 to N and then EOR because
   * 1 to K were already processed and dequeued.
   * We cannot only use one, but if we try to use both (i.e., let peer request
   * RibDumpReq while Rib is also announcing) we will send two EOR.
   *
   * Solution for case 3 is to submit a RibDumpReq after Rib has finished
   * initial announcement.
   *
   * This method handles all three cases above.
   */
  void maybeBufferRibDumpReq(const std::shared_ptr<AdjRib>& adjRib);

  /**
   * All RibDumpReqs that were buffered during initial announcement
   * will be scheduled as coro tasks per peer in this method.
   */
  folly::coro::Task<void> handleBufferedRibDumpReqs();

  // Schedules handleBufferedRibDumpReqs unless it is already scheduled.
  void scheduleHandleBufferedRibDumpReqs();

  folly::F14NodeMap<nettools::bgplib::BgpPeerId, bool /* sendAddPath */>&
  getPendingRibDumpReqs() noexcept {
    return pendingRibDumpReqs_;
  }

  folly::F14FastMap<std::shared_ptr<AdjRib>, PendingRouteRefreshAfis>&
  getPendingRouteRefreshPeers() noexcept {
    return pendingRouteRefreshPeers_;
  }

  folly::F14FastSet<std::shared_ptr<AdjRib>>&
  getPendingRibDumpAdjRibs() noexcept {
    return pendingRibDumpAdjRibs_;
  }

  /**
   * Update-group equivalent of maybeBufferRibDumpReq for detached peers that
   * must catch up independently before joining their group. Buffers the AdjRib
   * in pendingRibDumpAdjRibs_ to be served once the initial announcement is
   * done.
   */
  void maybeBufferRibDumpForDetachedPeer(const std::shared_ptr<AdjRib>& adjRib);

  /**
   * Update-group equivalent of handleBufferedRibDumpReqs. Drains the buffered
   * detached-peer RibDumpReqs in pendingRibDumpAdjRibs_ one at a time,
   * co_awaiting each per-peer dump inline with a small sleep between dumps to
   * pace the ShadowRib walks.
   */
  folly::coro::Task<void> handleBufferedRibDumpsForDetachedPeers();

  // ============================== ADJ-RIB STATE ==============================

  static std::string buildAdjRibOutGroupName(
      const nettools::bgplib::BgpPeerId& peerId,
      const PeeringParams& peeringParams) noexcept;

 private:
  // ============================ SHADOW RIB STATE =============================

  /*
   * Called once every change list consumer has processed trackedObject.
   * Clears the in-flight route flags and erases the entry from
   * shadowRibEntries_ once no path is left.
   */
  void processChangeItemCompleteCallback(
      TrackableObject<ShadowRibEntry>* trackedObject);

  PathId getPathId(const RibOutAnnouncementEntry& entry) const;

  std::optional<PathId> getPathId(const RibOutWithdrawalEntry& entry) const;

  /*
   * Sole mutator of @maxRibVersion_. The counter only ever steps forward, one
   * step per contiguous prefix chunk, so every write goes through here rather
   * than touching the member directly. Reads go through getMaxRibVersion().
   * Also advances the bgpcpp.rib.tableVersion ODS counter, which reports this
   * sequence rather than the loc-RIB's.
   */
  void incrementMaxRibVersion() noexcept;

  /*
   * Cancellable variant used by scheduleRibDumpForAdjRib for update-group
   * peers: the dump is tracked by the AdjRib's cancellation source so it can be
   * superseded or cancelled on teardown.
   */
  folly::coro::Task<void> processRibDumpReqWithCancellationCoro(
      std::shared_ptr<AdjRib> adjRib,
      RibDumpReq ribDumpReq);

  void onRibDumpCompleted(
      const std::shared_ptr<AdjRib>& adjRib,
      const RibDumpReq& ribDumpReq);

  // Identifies the routing table owned by this context.
  const VrfId vrfId_;
  PeerManagerBase& owner_;

  // The owner's scope, for coroutines scheduled on behalf of this table.
  folly::coro::CancellableAsyncScope& asyncScope_;

  // ============================ SHADOW RIB STATE =============================

  /*
   * [Shadow Rib]
   *
   * Shadow rib collection is the data structure which stores the:
   *
   * prefix -> ShadowRibEntry
   *
   * This collection will be used to handle RibDumpReq independently without
   * adding extra burdens to ribOut message queue.
   */
  ShadowRibEntriesMap shadowRibEntries_;

  /*
   * The maxRibVersion_ represents a numbering space which increments
   * on every prefix route change seen from RIB. The shadowRibEntry associated
   * to that prefix will receive its ribVersion value from the
   * maxRibVersion_.
   *
   * Though RIB also maintains its own rib versioning,
   * we currently are not able to guarantee that the vector of RIB entries
   * processed by PeerManager is ordered increasing by RIB version.
   * So, temporarily, we will let the shadowRibEntries_ be enumerated
   * by maxRibVersion_; and subsequently the changelist will be using
   * the number space associated to according to PeerManager::maxRibVersion_.
   */
  uint64_t maxRibVersion_{0};

  // A tracker for tracking ShadowRibEntries_ updates
  std::shared_ptr<ChangeTracker<ShadowRibEntry>> changeListTracker_{nullptr};

  // Bitmap of add-path capable consumers for selective notification
  ConsumerBitmap addPathConsumerBitmap_;

  // Bitmap of non-add-path consumers for bestpath changes
  ConsumerBitmap nonAddPathConsumerBitmap_;

  /*
   * One-time flag to prevent scheduling multiple handleBufferedRibDumpReqs
   * tasks
   */
  bool handleRibDumpsScheduled_{false};

  /**
   * One-time collection of pending RibDumpReqs from peers who come up
   * and call sessionEstablished during initial announcement.
   * Note that we only maintain the latest RibDumpReq from a peer;
   * this means there is only one RibDumpReq per peer.
   *
   * Because RibDumpReqs are read from @shadowRibEntries_, we can
   * only serve RibDumpReqs once we are certain that
   * Shadow RIB and RIB are in sync. This map stores the RibDumpReqs
   * that are requested during the initial announcement (i.e.
   * while initialAnnouncementDone_ = false); these will be processed
   * once initialAnnouncementDone_ = true.
   */
  folly::F14NodeMap<nettools::bgplib::BgpPeerId, bool /* sendAddPath */>
      pendingRibDumpReqs_;

  /**
   * Update-group equivalent of pendingRibDumpReqs_. One-time collection of
   * AdjRibs with a pending RibDumpReq from detached peers that come up and must
   * catch up independently before joining their group. Held as a set of AdjRib
   * shared_ptrs; since there is one AdjRib per peer, this keeps at most one
   * pending RibDumpReq per peer.
   */
  folly::F14FastSet<std::shared_ptr<AdjRib>> pendingRibDumpAdjRibs_;

  folly::F14FastMap<std::shared_ptr<AdjRib>, PendingRouteRefreshAfis>
      pendingRouteRefreshPeers_;
};

} // namespace facebook::bgp
