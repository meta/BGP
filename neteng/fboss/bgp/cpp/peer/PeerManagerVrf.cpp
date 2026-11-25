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

#include "neteng/fboss/bgp/cpp/peer/PeerManagerVrf.h"

#include <algorithm>

#include <folly/ScopeGuard.h>
#include <folly/String.h>
#include <folly/coro/CurrentExecutor.h>
#include <folly/coro/Sleep.h>

#include "fboss/lib/AlertLogger.h"
#include "neteng/fboss/bgp/cpp/BgpProfiler.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibStats.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/config/ConfigStructs.h"
#include "neteng/fboss/bgp/cpp/peer/PeerManagerBase.h"
#include "neteng/fboss/bgp/cpp/rib/Utils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

namespace facebook::bgp {

PeerManagerVrf::PeerManagerVrf(
    VrfId vrfId,
    PeerManagerBase& owner,
    folly::coro::CancellableAsyncScope& asyncScope,
    bool publishStats)
    : vrfId_(vrfId),
      owner_(owner),
      asyncScope_(asyncScope),
      publishStats_(publishStats) {
  /*
   * Tracks changes from the RIB for this table. Every change to the table's
   * ShadowRib must be published to this change list tracker.
   */
  changeListTracker_ = std::make_shared<ChangeTracker<ShadowRibEntry>>(
      "A tracker for tracking ShadowRibEntries_ updates");
  changeListTracker_->setGlobalOnChangeProcessedCallback(
      [this](TrackableObject<ShadowRibEntry>* trackedObject) {
        processChangeItemCompleteCallback(trackedObject);
      });
}

// ============================= SHADOW RIB STATE ==============================

const ConsumerBitmap& PeerManagerVrf::getConsumerBitmapForChange(
    bool isBestpathChange) const noexcept {
  /*
   * Simply return the appropriate bitmap based on change type
   * ChangeTracker will handle ORing if item is already on changelist
   */
  return isBestpathChange ? nonAddPathConsumerBitmap_ : addPathConsumerBitmap_;
}

void PeerManagerVrf::handleShadowRibEntryAnnouncement(
    RibOutAnnouncement& announcement) {
  /*
   * The chunk bump happens before any of the per-entry skip paths below, so a
   * skipped or erased entry still advances maxRibVersion_.
   */
  const folly::CIDRNetwork* chunkPrefix = nullptr;
  for (auto& entry : announcement.entries) {
    if (chunkPrefix == nullptr || *chunkPrefix != entry.prefix) {
      chunkPrefix = &entry.prefix;
      incrementMaxRibVersion();
    }
    entry.ribVersion = getMaxRibVersion();
    // bestpath only advertisement, hence the pathId will always be 0
    auto srRouteInfo = std::make_shared<ShadowRibRouteInfo>(
        entry.peer, entry.attrs, kDefaultPathID, entry.isPartialDrain);
    setShadowRibRouteState(srRouteInfo, SHADOWRIBROUTE_IN_UPDATE);
    resetShadowRibRouteState(srRouteInfo, SHADOWRIBROUTE_IN_WITHDRAW);

    // process bestpath announcement if any
    auto srEntryIter = shadowRibEntries_.find(entry.prefix);
    if (srEntryIter == shadowRibEntries_.end()) {
      auto trackableShadowRibEntry =
          std::make_unique<TrackableObject<ShadowRibEntry>>(ShadowRibEntry(
              entry.prefix,
              std::move(srRouteInfo), /* bestpath */
              {} /* multipath */,
              entry.switchId,
              entry.multiPathSize,
              entry.aggregateReceivedUcmpWeight,
              entry.aggregateLocalUcmpWeight,
              entry.ribPolicyUcmpWeight,
              entry.newlyInstalledInLocalRib,
              entry.installTimeStamp,
              getMaxRibVersion()));
      auto trackedObject = trackableShadowRibEntry.get();
      shadowRibEntries_.emplace(
          entry.prefix, std::move(trackableShadowRibEntry));
      changeListTracker_->publishChange(
          trackedObject,
          getConsumerBitmapForChange(true /* isBestpathChange */));
      trackedObject->get().publishCount++;
    } else {
      auto trackedObject = srEntryIter->second.get();
      auto& srEntry = trackedObject->get();
      /*
       * update common entry attribute from RibOutAnnouncement
       * TODO: optimize RibOutAnnouncement to send only one time of common
       * update
       */
      updateShadowRibEntryUtil(srEntry, entry);

      // update "bestpath" attribute
      srEntry.bestpath = std::move(srRouteInfo);
      changeListTracker_->publishChange(
          trackedObject,
          getConsumerBitmapForChange(true /* isBestpathChange */));
      srEntry.publishCount++;
    }
  }

  chunkPrefix = nullptr;
  for (auto& entry : announcement.addPathEntries) {
    if (chunkPrefix == nullptr || *chunkPrefix != entry.prefix) {
      chunkPrefix = &entry.prefix;
      incrementMaxRibVersion();
    }
    entry.ribVersion = getMaxRibVersion();
    // check before access
    if (!entry.attrs) {
      continue;
    }

    // process multi-path entries if it exists
    auto pathId = getPathId(entry);
    auto srEntryIter = shadowRibEntries_.find(entry.prefix);
    if (srEntryIter == shadowRibEntries_.end()) {
      // create the first multi-paths of this prefix with ShadowRibRouteInfo
      ShadowRibRouteInfos multipaths = {
          {pathId,
           std::make_shared<ShadowRibRouteInfo>(
               entry.peer,
               entry.attrs,
               entry.pathIdToSend,
               entry.isPartialDrain)}};
      setShadowRibRouteState(
          multipaths.begin()->second, SHADOWRIBROUTE_IN_UPDATE);
      auto trackableShadowRibEntry =
          std::make_unique<TrackableObject<ShadowRibEntry>>(ShadowRibEntry(
              entry.prefix,
              nullptr,
              std::move(multipaths),
              entry.switchId,
              entry.multiPathSize,
              entry.aggregateReceivedUcmpWeight,
              entry.aggregateLocalUcmpWeight,
              entry.ribPolicyUcmpWeight,
              entry.newlyInstalledInLocalRib,
              entry.installTimeStamp,
              getMaxRibVersion()));
      auto trackedObject = trackableShadowRibEntry.get();
      shadowRibEntries_.emplace(
          entry.prefix, std::move(trackableShadowRibEntry));
      changeListTracker_->publishChange(
          trackedObject,
          getConsumerBitmapForChange(false /* isBestpathChange */));
      trackedObject->get().publishCount++;
    } else {
      // find the existing shadow ribEntry
      auto trackedObject = srEntryIter->second.get();
      auto& srEntry = trackedObject->get();
      auto srRouteInfo = std::make_shared<ShadowRibRouteInfo>(
          entry.peer, entry.attrs, entry.pathIdToSend, entry.isPartialDrain);
      setShadowRibRouteState(srRouteInfo, SHADOWRIBROUTE_IN_UPDATE);
      resetShadowRibRouteState(srRouteInfo, SHADOWRIBROUTE_IN_WITHDRAW);

      auto multipathIter = srEntry.multipaths.find(pathId);
      if (multipathIter == srEntry.multipaths.end()) {
        // create path entry
        srEntry.multipaths.emplace(pathId, std::move(srRouteInfo));
      } else {
        // update existing path entry
        multipathIter->second = std::move(srRouteInfo);
      }

      // update common entry attribute from RibOutAnnouncement
      updateShadowRibEntryUtil(srEntry, entry);
      changeListTracker_->publishChange(
          trackedObject,
          getConsumerBitmapForChange(false /* isBestpathChange */));
      srEntry.publishCount++;
    }
  }

  /*
   * publish shadow rib collection size
   * TODO: export a per-VRF shadow RIB size to ODS instead of VRF 0 only.
   */
  if (publishStats_) {
    RibStats::publishShadowRibSize(shadowRibEntries_.size());
  }
}

void PeerManagerVrf::handleShadowRibEntryWithdrawal(
    RibOutWithdrawal& withdrawal) {
  /*
   * The chunk bump happens before the lookup below, so a withdrawal for an
   * already-removed prefix still advances maxRibVersion_.
   */
  const folly::CIDRNetwork* chunkPrefix = nullptr;
  // process bestpath withdrawal
  for (auto& entry : withdrawal.entries) {
    if (chunkPrefix == nullptr || *chunkPrefix != entry.prefix) {
      chunkPrefix = &entry.prefix;
      incrementMaxRibVersion();
    }
    entry.ribVersion = getMaxRibVersion();
    auto srEntryIter = shadowRibEntries_.find(entry.prefix);
    if (srEntryIter == shadowRibEntries_.end()) {
      XLOGF(
          DBG4,
          "Skip processing withdrawal for non-existent prefix in shadowRibEntries: {}",
          folly::IPAddress::networkToString(entry.prefix));
      continue;
    }
    auto trackedObject = srEntryIter->second.get();
    auto& srEntry = trackedObject->get();
    if (srEntry.bestpath) {
      setShadowRibRouteState(srEntry.bestpath, SHADOWRIBROUTE_IN_WITHDRAW);
      resetShadowRibRouteState(srEntry.bestpath, SHADOWRIBROUTE_IN_UPDATE);
    }
    srEntry.ribVersion = getMaxRibVersion();
    changeListTracker_->publishChange(
        trackedObject, getConsumerBitmapForChange(true /* isBestpathChange */));
  }

  // process multipath withdrawal
  chunkPrefix = nullptr;
  for (auto& entry : withdrawal.addPathEntries) {
    if (chunkPrefix == nullptr || *chunkPrefix != entry.prefix) {
      chunkPrefix = &entry.prefix;
      incrementMaxRibVersion();
    }
    entry.ribVersion = getMaxRibVersion();
    auto srEntryIter = shadowRibEntries_.find(entry.prefix);
    if (srEntryIter == shadowRibEntries_.end()) {
      // skip processing since this prefix does not exist
      continue;
    }

    auto trackedObject = srEntryIter->second.get();
    auto& srEntry = trackedObject->get();
    /*
     * TODO: once rib-allocated path ID is rolled out, there's not a case where
     * a RibOutWithdrawal entry has no valid pathID value, so at that point we
     * could remove this block
     */
    auto maybePathId = getPathId(entry);
    if (!maybePathId.has_value()) {
      XLOGF(
          ERR,
          "Skip erasing path from ShadowRib with unknown path ID for prefix: {}",
          folly::IPAddress::networkToString(entry.prefix));
      continue;
    }

    // skip processing since this path ID does not exist
    auto pathId = maybePathId.value();
    if (!srEntry.multipaths.contains(pathId)) {
      auto isUint = std::holds_alternative<uint32_t>(pathId);
      XLOGF(
          ERR,
          "Skip erasing path from ShadowRib as multipaths of entry for pfx {} does not contain path ID {}",
          folly::IPAddress::networkToString(entry.prefix),
          isUint ? std::to_string(std::get<uint32_t>(pathId))
                 : std::get<folly::IPAddress>(pathId).toJson());
      continue;
    }

    setShadowRibRouteState(
        srEntry.multipaths.at(pathId), SHADOWRIBROUTE_IN_WITHDRAW);
    resetShadowRibRouteState(
        srEntry.multipaths.at(pathId), SHADOWRIBROUTE_IN_UPDATE);
    srEntry.ribVersion = getMaxRibVersion();
    const auto& bitmap =
        getConsumerBitmapForChange(false /* isBestpathChange */);
    changeListTracker_->publishChange(trackedObject, bitmap);
  }

  /*
   * publish shadow rib collection size
   * TODO: export a per-VRF shadow RIB size to ODS instead of VRF 0 only.
   */
  if (publishStats_) {
    RibStats::publishShadowRibSize(shadowRibEntries_.size());
  }
}

void PeerManagerVrf::updateShadowRibEntryUtil(
    ShadowRibEntry& srEntry,
    const RibOutAnnouncementEntry& entry) const {
  srEntry.switchId = entry.switchId;
  srEntry.multiPathSize = entry.multiPathSize;
  srEntry.aggregateReceivedUcmpWeight = entry.aggregateReceivedUcmpWeight;
  srEntry.aggregateLocalUcmpWeight = entry.aggregateLocalUcmpWeight;
  srEntry.ribPolicyUcmpWeight = entry.ribPolicyUcmpWeight;
  srEntry.newlyInstalledInLocalRib = entry.newlyInstalledInLocalRib;
  srEntry.installTimeStamp = entry.installTimeStamp;
  srEntry.ribVersion = getMaxRibVersion();
}

/*
 * @brief  This is the callback from changeList tracker indicating
 *         that a specific shadowRibEntry that was earlier published
 *         to the tracker has been consumed by all the registered
 *         consumers. And this callback is a trigger to commit all
 *         pending work. For example, if a path or an entry is meant
 *         to be withdrawn/removed, withdraw/remove that specific
 *         path or an entry
 *
 * @param  trackedObject  the object that is being tracked by tracker
 *                        (which is a wrapper for a specific shadow
 *                        rib entry)
 *
 * @return void
 */
void PeerManagerVrf::processChangeItemCompleteCallback(
    TrackableObject<ShadowRibEntry>* trackedObject) {
  ShadowRibEntry& srEntry = trackedObject->get();
  if (srEntry.bestpath) {
    resetShadowRibRouteState(srEntry.bestpath, SHADOWRIBROUTE_IN_UPDATE);
    /*
     * If it is withdraw, just set bestpath to nullptr for now
     * (to not remove the whole shadow rib entry yet) because
     * other multipath entries still may be in process of being
     * consumed
     */
    if (isShadowRibRouteInWithdraw(srEntry.bestpath->flags)) {
      resetShadowRibRouteState(srEntry.bestpath, SHADOWRIBROUTE_IN_WITHDRAW);
      srEntry.bestpath = nullptr;
    }
  }

  for (auto it = srEntry.multipaths.begin(); it != srEntry.multipaths.end();) {
    resetShadowRibRouteState(it->second, SHADOWRIBROUTE_IN_UPDATE);
    /*
     * If it is withdraw, just erase this specific multipath
     */
    if (isShadowRibRouteInWithdraw(it->second->flags)) {
      it = srEntry.multipaths.erase(it);
    } else {
      ++it;
    }
  }

  /*
   * If bestpath and all multipaths have been removed then remove
   * the shadow rib entry
   */
  if (!srEntry.multipaths.size() && !srEntry.bestpath) {
    shadowRibEntries_.erase(srEntry.prefix);
  }
}

PathId PeerManagerVrf::getPathId(const RibOutAnnouncementEntry& entry) const {
  if (owner_.isRibAllocatedPathIdEnabled()) {
    return entry.pathIdToSend;
  }
  return folly::IPAddress(entry.attrs->getNexthop());
}

std::optional<PathId> PeerManagerVrf::getPathId(
    const RibOutWithdrawalEntry& entry) const {
  if (owner_.isRibAllocatedPathIdEnabled()) {
    return entry.pathIdToSend;
  }
  if (entry.nh.has_value()) {
    return folly::IPAddress(*entry.nh);
  } else {
    return std::nullopt;
  }
}

void PeerManagerVrf::incrementMaxRibVersion() noexcept {
  // TODO: export a per-VRF table version to ODS instead of VRF 0 only.
  if (publishStats_) {
    RibStats::incrementRibTableVersion();
  }
  ++maxRibVersion_;
}

void PeerManagerVrf::processRibDumpReq(
    const std::shared_ptr<AdjRib>& adjRib,
    const RibDumpReq& ribDumpReq,
    bool sendWithEoR) {
  [[maybe_unused]] ScopedProfile profile("PeerManagerBase::processRibDumpReq");
  auto ribVersionBeforeWalk = adjRib->getLastSeenRibVersion();
  XLOGF(
      DBG1,
      "Handling RibDumpReq inside ShadowRib from peer {}",
      adjRib->getRemotePeerId().str());

  if (!adjRib->getAdjRibOutQueue()) {
    XLOGF(
        ERR,
        "Skip RibDumpReq since session queues not initialized for {}.",
        adjRib->getRemotePeerId().str());
    return;
  }

  RibOutAnnouncement announcement;
  announcement.initialDump = true;
  announcement.routeRefreshAfi = ribDumpReq.filterAfi;

  /*
   * Anything skipped below means the peer was not shown the whole table, so
   * lastSeenRibVersion must not advance; the changeList carries those entries,
   * and the version, instead.
   */
  bool skippedEntry = false;
  for (const auto& [prefix, trackedShadowRibEntry] : shadowRibEntries_) {
    /*
     * RFC 2918 §3: re-advertise only the requested family. nullopt means all
     * AFIs (initial dump, GR re-init, policy re-evaluation).
     */
    if (ribDumpReq.filterAfi.has_value()) {
      /*
       * processPeerRouteRefresh rejects any AFI but v4/v6, so collapsing to
       * v4-vs-not-v4 is safe. A caller bypassing that gate would silently be
       * treated as v6 -- guard at the producer, not here.
       */
      const bool prefixIsV4 = prefix.first.isV4();
      const bool wantV4 =
          (*ribDumpReq.filterAfi == nettools::bgplib::BgpUpdateAfi::AFI_IPv4);
      if (prefixIsV4 != wantV4) {
        skippedEntry = true;
        continue;
      }
    }

    /*
     * Skip entries already pending on this consumer's changeList -- the peer
     * receives them from there and advances its version. Route Refresh
     * (RFC 2918) bypasses the skip: the peer explicitly asked for every route.
     */
    if (!ribDumpReq.filterAfi &&
        changeListTracker_->isConsumerSetOnTrackableObject(
            trackedShadowRibEntry.get(), adjRib->getChangeListConsumer())) {
      skippedEntry = true;
      continue;
    }

    /*
     * NOTE: we want to pack all paths from one prefix inside the same
     * announcement even if can over the packing limit with add-path enabled.
     */
    auto& shadowRibEntry = trackedShadowRibEntry->get();

    if (!ribDumpReq.sendAddPath) {
      // send out bestpath only.
      const auto bestpath = shadowRibEntry.bestpath;
      if (bestpath) {
        if (isShadowRibRouteInWithdraw(bestpath->flags)) {
          continue;
        }
        announcement.entries.emplace_back(
            prefix,
            kDefaultPathID,
            bestpath->peer,
            bestpath->attrs,
            shadowRibEntry.switchId,
            shadowRibEntry.multiPathSize,
            shadowRibEntry.aggregateReceivedUcmpWeight,
            shadowRibEntry.aggregateLocalUcmpWeight,
            shadowRibEntry.ribPolicyUcmpWeight,
            /*newlyInstalledInLocalRib=*/false,
            std::chrono::system_clock::now(),
            shadowRibEntry.ribVersion,
            bestpath->isPartialDrain);
      }
    } else {
      // send out all multipaths with add-path capable peer
      for (const auto& [_, multipath] : shadowRibEntry.multipaths) {
        if (multipath) {
          if (isShadowRibRouteInWithdraw(multipath->flags)) {
            continue;
          }
          announcement.addPathEntries.emplace_back(
              prefix,
              multipath->pathIdToSend,
              multipath->peer,
              multipath->attrs,
              shadowRibEntry.switchId,
              shadowRibEntry.multiPathSize,
              shadowRibEntry.aggregateReceivedUcmpWeight,
              shadowRibEntry.aggregateLocalUcmpWeight,
              shadowRibEntry.ribPolicyUcmpWeight,
              /*newlyInstalledInLocalRib=*/false,
              std::chrono::system_clock::now(),
              shadowRibEntry.ribVersion,
              multipath->isPartialDrain);
        }
      }
    }
  }

  // Last RibOutAnnouncement with `sendWithEoR` flag to indicate End-of-Rib.
  announcement.sendWithEoR = sendWithEoR;

  XLOGF(DBG1, "{}", formatRibOutAnnouncementLog(announcement));

  /**
   * TODO: Once we change the stateful GR behavior to send all initial
   * full dump to adjRib and let adjRib to decide when to send to socket
   * we can then call processRibMessage for each prefix as we walk
   * through shadowRib table
   */
  adjRib->processRibMessage(announcement);
  /*
   * A dump that covered every entry means the peer has seen the whole table;
   * advance it to the PeerManager's max version. If anything was skipped, leave
   * the version to the changeList. setLastSeenRibVersion only ever advances.
   */
  if (!skippedEntry) {
    adjRib->setLastSeenRibVersion(getMaxRibVersion());
  }
  /*
   * changeList consumers are to be registered to the tracker
   * library only after initial dump with EoR flag is sent to
   * adjRibOut, right away as soon EoR flag is sent so as to not
   * miss any further incremental changes to routes.
   *
   * When update group is disabled we activate the changelist
   * consumer here via activateChangeListConsumer (which carries
   * out-delay logic).
   *
   * The per-AdjRib test is necessary for a bounded stream subscriber. Such a
   * subscriber is never registered with UpdateGroupManager. If the global
   * knob is on, no other code activates its consumer. The bounded path sends
   * through sendBgpUpdates(), and the only entry to sendBgpUpdates() is the
   * change-list consume timer that activateChangeListConsumer() creates.
   * Without this test the subscriber would get an open stream, no route, and
   * no EoR.
   */
  if (!owner_.isUpdateGroupEnabled() || !adjRib->isUpdateGroupEnabled()) {
    adjRib->activateChangeListConsumer();
  }

  XLOGF(
      INFO,
      "Peer {}: processRibDumpReq complete "
      "(lastSeenRibVersion: {} -> {})",
      adjRib->getPeerName(),
      ribVersionBeforeWalk,
      adjRib->getLastSeenRibVersion());
}

/*
 * Cancellable variant of processRibDumpReqCoro used by scheduleRibDumpForAdjRib
 * for update-group peers. The rib dump is tracked by the AdjRib's cancellation
 * source so it can be superseded by a newer dump or cancelled on session
 * teardown.
 *
 * This coroutine is ONLY executed when update group is enabled.
 */
folly::coro::Task<void> PeerManagerVrf::processRibDumpReqWithCancellationCoro(
    std::shared_ptr<AdjRib> adjRib,
    RibDumpReq ribDumpReq) {
  auto cancelToken = co_await folly::coro::co_current_cancellation_token;

  /*
   * If this dump was cancelled -- superseded by a newer dump for the same peer,
   * or cancelled on session teardown -- skip it entirely. Whoever cancelled us
   * now owns the cancellation source, so we must neither deliver a stale dump
   * nor touch the source.
   */
  if (cancelToken.isCancellationRequested()) {
    co_return;
  }

  if (adjRib->testOnlyDeferInitDump) [[unlikely]] {
    /*
     * The dump is deferred, not completed. Retire this coro's cancellation
     * source and schedule a fresh dump so the peer stays "scheduled" (a dump is
     * always in flight) until the defer condition clears. We must reset before
     * rescheduling so scheduleRibDumpForAdjRib does not coalesce against our
     * own existing source, and we keep this above the SCOPE_EXIT below so the
     * reset does not discard the new source.
     */
    XLOGF(
        INFO,
        "testOnlyDeferInitDump: re-scheduling RibDumpReq for peer {}",
        adjRib->getRemotePeerId().str());
    adjRib->resetRibDumpCancellationSource();
    scheduleRibDumpForAdjRib(adjRib, std::move(ribDumpReq));
    co_return;
  }

  /*
   * On NORMAL completion, clear the AdjRib's rib-dump tracking so a subsequent
   * dump can be scheduled. Guard on our own token: if cancellation was
   * requested (superseded by a newer dump or session teardown), the source has
   * already been retired/replaced, so we must not overwrite it.
   *
   * The dump re-applies the AdjRib's current egress policy to the full RIB, so
   * also clear any pending egress policy update here (no-op when none is
   * pending). If we were cancelled, a superseding dump will clear it instead.
   */
  SCOPE_EXIT {
    if (!cancelToken.isCancellationRequested()) {
      adjRib->resetRibDumpCancellationSource();
      adjRib->clearPendingEgressPolicyUpdate();
      onRibDumpCompleted(adjRib, ribDumpReq);
    }
  };

  processRibDumpReq(adjRib, ribDumpReq, /*sendWithEoR=*/true);

  /*
   * With update group enabled, register and activate the detached peer's
   * changelist consumer after its dump completes. registerDetachedConsumer uses
   * bounded consumption up until the group's position (unlike
   * activateChangeListConsumer, which consumes to the end of the changelist and
   * carries out-delay logic that does not apply under update groups).
   */
  if (adjRib->isDetachedPeer()) {
    if (!adjRib->getChangeListConsumer()) {
      adjRib->registerDetachedConsumer(
          changeListTracker_,
          addPathConsumerBitmap_,
          nonAddPathConsumerBitmap_);
    }
    /*
     * A peer blocked on a group push must wait for markPeerUnblocked(). A peer
     * transiently blocked in its private sender already has a sender that will
     * resume when its queue drains.
     */
    if (adjRib->getPeerState() != PeerUpdateState::DETACHED_BLOCKED) {
      adjRib->activateDetachedModeProcessing();
    }
  }
  co_return;
}

bool PeerManagerVrf::isRibDumpScheduledForAdjRib(
    const std::shared_ptr<AdjRib>& adjRib) const {
  /*
   * A dump is pending for this detached peer if it is either buffered (in
   * pendingRibDumpAdjRibs_, waiting for the drain) or already scheduled / in
   * flight on asyncScope_ (its cancellation source is armed). Pure predicate,
   * no side effects. Update-group only.
   */
  return adjRib->isRibDumpScheduled() ||
      pendingRibDumpAdjRibs_.contains(adjRib);
}

void PeerManagerVrf::cancelRibDumpForAdjRib(
    const std::shared_ptr<AdjRib>& adjRib) {
  cancelRibDumpForAdjRib(adjRib, false /* discardRouteRefreshRequests */);
}

void PeerManagerVrf::cancelRibDumpForAdjRib(
    const std::shared_ptr<AdjRib>& adjRib,
    bool discardRouteRefreshRequests) {
  /*
   * Cancel a detached peer's pending rib dump. Invokes the cancellation from
   * AdjRib, but should also unconditionally remove a buffered request so that
   * the AdjRib isn't serviced in handleBufferedRibDumpsForDetachedPeers.
   * Update-group only.
   */
  if (pendingRibDumpAdjRibs_.erase(adjRib) > 0) {
    BgpStats::decrPendingRibDumpReqsCount(1);
  }
  if (auto pendingIt = pendingRouteRefreshPeers_.find(adjRib);
      pendingIt != pendingRouteRefreshPeers_.end()) {
    if (discardRouteRefreshRequests) {
      XLOGF(
          DBG1,
          "Peer {}: discarding update-group Route Refresh work on teardown "
          "(pending={})",
          adjRib->getPeerName(),
          pendingIt->second.size());
      pendingRouteRefreshPeers_.erase(pendingIt);
    } else if (pendingIt->second.empty()) {
      pendingRouteRefreshPeers_.erase(pendingIt);
    }
  }
  adjRib->cancelRibDump();
}

void PeerManagerVrf::scheduleRibDumpForAdjRib(
    const std::shared_ptr<AdjRib>& adjRib) {
  scheduleRibDumpForAdjRib(
      adjRib, RibDumpReq(adjRib->getRemotePeerId(), adjRib->sendAddPath()));
}

void PeerManagerVrf::scheduleRibDumpForAdjRib(
    const std::shared_ptr<AdjRib>& adjRib,
    RibDumpReq ribDumpReq) {
  /*
   * Coalesce: if a rib dump is already pending for this detached peer --
   * buffered in pendingRibDumpAdjRibs_ (the drain will serve it) or scheduled /
   * in flight on asyncScope_ -- do not schedule another. The in-flight/buffered
   * walk serves the latest ShadowRib state. Update-group only.
   */
  if (isRibDumpScheduledForAdjRib(adjRib)) {
    return;
  }
  asyncScope_.add(
      co_withExecutor(
          &owner_.getEventBase(),
          processRibDumpReqWithCancellationCoro(adjRib, std::move(ribDumpReq))),
      adjRib->getCancellationTokenForNewRibDump());
}

void PeerManagerVrf::scheduleRouteRefreshForUpdateGroupPeer(
    const std::shared_ptr<AdjRib>& adjRib,
    nettools::bgplib::BgpUpdateAfi requestedAfi) {
  auto& pendingAfis = pendingRouteRefreshPeers_[adjRib];
  if (!pendingAfis.insert(requestedAfi).second) {
    XLOGF(
        DBG1,
        "Peer {}: coalescing update-group Route Refresh AFI={}",
        adjRib->getPeerName(),
        static_cast<int>(requestedAfi));
    return;
  }
  maybeSchedulePendingRouteRefresh(adjRib);
}

void PeerManagerVrf::maybeSchedulePendingRouteRefresh(
    const std::shared_ptr<AdjRib>& adjRib) {
  auto pendingIt = pendingRouteRefreshPeers_.find(adjRib);
  if (pendingIt == pendingRouteRefreshPeers_.end() ||
      pendingIt->second.empty()) {
    return;
  }

  const auto peerState = adjRib->getPeerState();
  // Drop a Route Refresh event queued before teardown but delivered afterward.
  if (peerState == PeerUpdateState::DOWN) {
    pendingRouteRefreshPeers_.erase(pendingIt);
    return;
  }
  if (isRibDumpScheduledForAdjRib(adjRib)) {
    XLOGF(
        DBG1,
        "Peer {}: retaining {} update-group Route Refresh AFI(s) behind "
        "active dump",
        adjRib->getPeerName(),
        pendingIt->second.size());
    return;
  }
  if (peerState == PeerUpdateState::INIT ||
      peerState == PeerUpdateState::DETACHED_INIT_DUMP) {
    XLOGF(
        DBG1,
        "Peer {}: retaining {} update-group Route Refresh AFI(s) in state {}",
        adjRib->getPeerName(),
        pendingIt->second.size(),
        peerState);
    return;
  }

  auto updateGroup = adjRib->getUpdateGroup();
  if (!updateGroup) {
    XLOGF(
        WARN,
        "Peer {}: retaining pending update-group Route Refresh work until "
        "the peer has a group",
        adjRib->getPeerName());
    return;
  }

  /* Keep exhaustive so new peer states require explicit handling. */
  switch (peerState) {
    case PeerUpdateState::JOINED_RUNNING:
    case PeerUpdateState::JOINED_BLOCKED:
      updateGroup->detachPeer(
          adjRib, AdjRibOutGroup::DetachReason::RouteRefresh);
      updateGroup->recoverIfNoSyncPeers();
      break;
    case PeerUpdateState::DETACHED_RUNNING:
    case PeerUpdateState::DETACHED_BLOCKED:
      break;
    case PeerUpdateState::DETACHED_READY_TO_JOIN:
      adjRib->clearAdjRibFlag(AdjRib::IS_DETACHED_FAST_PEER);
      adjRib->setPeerState(PeerUpdateState::DETACHED_RUNNING);
      break;
    case PeerUpdateState::INIT:
    case PeerUpdateState::DETACHED_INIT_DUMP:
    case PeerUpdateState::DOWN:
      return;
  }

  /* AFI replay order is intentionally unspecified. */
  const auto requestedAfi = *pendingIt->second.begin();
  XLOGF(
      DBG1,
      "Peer {}: scheduling private update-group Route Refresh AFI={} from "
      "state {}",
      adjRib->getPeerName(),
      static_cast<int>(requestedAfi),
      peerState);
  scheduleRibDumpForAdjRib(
      adjRib,
      RibDumpReq(
          adjRib->getRemotePeerId(), adjRib->sendAddPath(), requestedAfi));
}

void PeerManagerVrf::processDetachedPeerEgressPolicyReEvaluation(
    const std::shared_ptr<AdjRib>& adjRib) {
  /*
   * Cancel any rib dump already scheduled for the peer and run the
   * re-evaluation dump inline so it completes within this event-loop turn -- a
   * deferred walk leaves a window where the peer could rejoin with stale
   * (old-policy) entries and surface discrepancies during the collapse.
   */
  if (isRibDumpScheduledForAdjRib(adjRib)) {
    XLOGF(
        INFO,
        "Peer {}: cancelling scheduled rib dump to run egress policy "
        "re-evaluation inline",
        adjRib->getPeerName());
    cancelRibDumpForAdjRib(adjRib);
  }
  /*
   * Gate on egressEoRsSent() rather than egressEoRsPending(): the latter only
   * reports a debt the dump path already booked, and the dump cancelled above
   * is what would have booked it, so absorbing the initial dump of a peer that
   * has never dumped would silently drop its EoR. A peer that has not been
   * sent an EoR for this session still owes one; passing sendWithEoR marks the
   * pending flags in turn, via processRibOutAnnouncement.
   */
  processRibDumpReq(
      adjRib,
      RibDumpReq(adjRib->getRemotePeerId(), adjRib->sendAddPath()),
      !adjRib->egressEoRsSent() /* sendWithEoR */);
  maybeSchedulePendingRouteRefresh(adjRib);

  if (!adjRib->getChangeListConsumer()) {
    adjRib->registerDetachedConsumer(
        changeListTracker_, addPathConsumerBitmap_, nonAddPathConsumerBitmap_);
  }

  /*
   * Consume the peer's change list to the tail so its RIB-OUT reflects the full
   * shadow RIB plus every pending change, completing the re-evaluation in this
   * turn rather than leaving items for the async consume timer.
   */
  if (adjRib->getChangeListConsumer()) {
    adjRib->tryIterateChangesToEnd(
        adjRib->sendAddPath() /* processFullMultipaths */);
  }
}

void PeerManagerVrf::onRibDumpCompleted(
    const std::shared_ptr<AdjRib>& adjRib,
    const RibDumpReq& ribDumpReq) {
  auto pendingIt = pendingRouteRefreshPeers_.find(adjRib);
  if (pendingIt == pendingRouteRefreshPeers_.end()) {
    return;
  }
  if (ribDumpReq.filterAfi &&
      pendingIt->second.erase(*ribDumpReq.filterAfi) > 0) {
    XLOGF(
        DBG1,
        "Peer {}: completed private update-group Route Refresh AFI={} "
        "(pending={})",
        adjRib->getPeerName(),
        static_cast<int>(*ribDumpReq.filterAfi),
        pendingIt->second.size());
  }
  if (pendingIt->second.empty()) {
    pendingRouteRefreshPeers_.erase(pendingIt);
    return;
  }
  maybeSchedulePendingRouteRefresh(adjRib);
}

void PeerManagerVrf::maybeBufferRibDumpReq(
    const std::shared_ptr<AdjRib>& adjRib) {
  /**
   *    Start     Rib is announcing...    Done
   *      | [A1, A2,    ....,          AN] |
   * -----|--------------------------------|--------------> PeerMgr timeline
   *      |                                |
   */
  const auto& peerId = adjRib->getRemotePeerId();
  if (!ribInitialAnnouncementStarted_) {
    /*
     * For peers that come up before Start
     * (i.e. ribInitialAnnouncementStart_ = false),
     * they can rely on PeerMgr to send [A1, ..., AN] regularly through
     * processRibOutMsgLoop. No need to entertain RibDumpReq
     */
    XLOGF(
        DBG1,
        "Not sending RibDumpReq for {}; Rib has not started initial announcement.",
        peerId.str());
    return;
  }

  /*
   * For peers that come up after ribInitialAnnouncementStart_, buffer
   * them in the pending list to be served
   */
  XLOGF(INFO, "Insert RibDumpReq in pending list for {}", peerId.str());

  auto [it, inserted] =
      pendingRibDumpReqs_.insert_or_assign(peerId, adjRib->sendAddPath());
  if (inserted) {
    BgpStats::incrPendingRibDumpReqsCount();
  }
  if (ribInitialAnnouncementDone_) {
    scheduleHandleBufferedRibDumpReqs();
  }
}

void PeerManagerVrf::scheduleHandleBufferedRibDumpReqs() {
  if (handleRibDumpsScheduled_) {
    return;
  }
  asyncScope_.add(
      co_withExecutor(&owner_.getEventBase(), handleBufferedRibDumpReqs()));
  handleRibDumpsScheduled_ = true;
}

folly::coro::Task<void> PeerManagerVrf::handleBufferedRibDumpReqs() {
  /*
   * This coroutine is only scheduled when ribInitialAnnouncementDone_ is true
   * (callers check before scheduling). ribInitialAnnouncementStarted_ is
   * always set before ribInitialAnnouncementDone_, so
   * isRibInitialAnnouncementStarted() is guaranteed true here.
   */
  XLOG(INFO, "Handling all buffered RibDumpReqs");

  /*
   * Clear the scheduled flag however this coroutine leaves, unless it hands
   * off to a follow-up drain below. processRibDumpReq is not noexcept and the
   * sleep throws on cancellation; a flag left set stops
   * scheduleHandleBufferedRibDumpReqs from ever scheduling another drain.
   */
  auto clearScheduledGuard =
      folly::makeGuard([this]() noexcept { handleRibDumpsScheduled_ = false; });

  folly::F14NodeMap<nettools::bgplib::BgpPeerId, bool /* sendAddPath */>
      curPendingRibDumpReqs_ = folly::copy(pendingRibDumpReqs_);
  BgpStats::decrPendingRibDumpReqsCount(pendingRibDumpReqs_.size());
  pendingRibDumpReqs_.clear();

  /**
   * Iterate over the buffered RibDumpReqs and schedule processRibDumpReq.
   * Here we do not need synchronization or lock because only one fiber
   * or coro is running at once on PeerManagerBase event base. This
   * can change in the future if we make PeerManagerBase multi-threaded.
   */
  for (auto& req : curPendingRibDumpReqs_) {
    auto& peerId = req.first;
    auto& sendAddPath = req.second;

    XLOGF(INFO, "Processing pending RibDumpReq for peer {}", peerId.str());

    const auto& adjRibs = owner_.getAdjRibs();
    if (auto adjRibIt = adjRibs.find(peerId); adjRibIt != adjRibs.cend()) {
      processRibDumpReq(
          adjRibIt->second,
          RibDumpReq(peerId, sendAddPath),
          true /* sendWithEoR */);
    } else {
      XLOGF(
          ERR,
          "Skip sending announcement since adjRib {} does not exist.",
          peerId.str());
    }
    co_await folly::coro::sleep(std::chrono::milliseconds(1));
  }

  if (!pendingRibDumpReqs_.empty()) {
    XLOG(INFO, "pending RibDumps non-empty, schedule another task");
    asyncScope_.add(
        co_withExecutor(&owner_.getEventBase(), handleBufferedRibDumpReqs()));
    clearScheduledGuard.dismiss();
  }

  co_return;
}

void PeerManagerVrf::maybeBufferRibDumpForDetachedPeer(
    const std::shared_ptr<AdjRib>& adjRib) {
  /**
   *    Start  Shadow Rib is populating  Done
   *      | [A1, A2,    ....,          AN] |
   * -----|--------------------------------|--------------> PeerMgr timeline
   *      |                                |
   */
  const auto& peerId = adjRib->getRemotePeerId();
  if (!ribInitialAnnouncementDone_) {
    /*
     * For peers that come up before DONE
     * (i.e. ribInitialAnnouncementDone_ = false),
     * they should wait with the update group that they have joined;
     * on markRibInitialAnnouncementDone_ = true, all the groups will
     * begin to walk the shadow RIB. No need to entertain RibDumpReq
     * for independent peer.
     *
     * We must also not buffer one here: a pre-DONE entry would linger in
     * pendingRibDumpAdjRibs_ and later get drained by
     * handleBufferedRibDumpsForDetachedPeers() when the next peer comes up
     * after DONE (the drain serves the whole set), giving this peer a spurious
     * independent RIB dump on top of the group's initial shadow-RIB walk.
     */
    XLOGF(
        DBG1,
        "Waiting for group RIB walk for {}; Rib has not finished initial announcement.",
        peerId.str());
    return;
  }

  /*
   * For peers that come up after ribInitialAnnouncementDone_, buffer
   * them in the pending list to be served
   */
  XLOGF(INFO, "Insert RibDumpReq in pending list for {}", peerId.str());

  auto [it, inserted] = pendingRibDumpAdjRibs_.insert(adjRib);
  if (inserted) {
    BgpStats::incrPendingRibDumpReqsCount();
  }

  if (!handleRibDumpsScheduled_) {
    asyncScope_.add(co_withExecutor(
        &owner_.getEventBase(), handleBufferedRibDumpsForDetachedPeers()));
    handleRibDumpsScheduled_ = true;
  }
}

folly::coro::Task<void>
PeerManagerVrf::handleBufferedRibDumpsForDetachedPeers() {
  /*
   * This coroutine is only scheduled when ribInitialAnnouncementDone_ is true
   * (callers check before scheduling). ribInitialAnnouncementStarted_ is
   * always set before ribInitialAnnouncementDone_, so
   * isRibInitialAnnouncementStarted() is guaranteed true here.
   */
  XLOG(INFO, "Handling all buffered RibDumpReqs for detached peers");

  /*
   * Clear the scheduled flag however this coroutine leaves, not just on the
   * normal path. maybeBufferRibDumpReq only schedules a drain when the flag is
   * false, so if the loop below unwinds --
   * processRibDumpReqWithCancellationCoro is not noexcept and runs a full
   * ShadowRib walk with policy evaluation -- a flag left set means no drain is
   * ever scheduled again and every peer that comes up afterwards is buffered
   * and never served. Peers still buffered when that happens are picked up by
   * the next maybeBufferRibDumpReq.
   *
   */
  auto clearScheduledGuard =
      folly::makeGuard([this]() noexcept { handleRibDumpsScheduled_ = false; });

  /**
   * Drain the buffered RibDumpReqs one at a time, co_awaiting each per-peer
   * dump inline so they run sequentially. The sleep between dumps paces the
   * ShadowRib walks so a large batch does not monopolize the event base. Always
   * take begin(): requests buffered during a sleep are picked up by a later
   * iteration, so no copy or self-reschedule is needed. No lock is needed
   * because only one fiber or coro runs at once on the PeerManagerBase event
   * base.
   */
  while (!pendingRibDumpAdjRibs_.empty()) {
    auto adjRib = *pendingRibDumpAdjRibs_.begin();
    pendingRibDumpAdjRibs_.erase(pendingRibDumpAdjRibs_.begin());
    BgpStats::decrPendingRibDumpReqsCount(1);

    co_await processRibDumpReqWithCancellationCoro(
        adjRib, RibDumpReq(adjRib->getRemotePeerId(), adjRib->sendAddPath()));
    co_await folly::coro::sleepReturnEarlyOnCancel(
        std::chrono::milliseconds(1));
  }

  co_return;
}

// ======================= EOR AND INITIALIZATION STATE =======================

void PeerManagerVrf::processPeerEoR(
    const nettools::bgplib::BgpPeerId& peerId) noexcept {
  XLOGF(INFO, "Process EoR from {}", peerId.str());

  if (staticPeerEoRReceived_.size() != 0 ||
      dynamicPeerEoRReceived_.size() != 0) {
    /*
     * in case there are any static OR dynamic peers try to match
     * peerId. In case there is no match, do nothing.
     */
    auto staticIt = staticPeerEoRReceived_.find(peerId.peerAddr);
    if (staticIt != staticPeerEoRReceived_.end()) {
      staticIt->second.first = true;
    } else {
      auto dynamicIt = dynamicPeerEoRReceived_.find(peerId);
      if (dynamicIt != dynamicPeerEoRReceived_.end()) {
        dynamicIt->second.first = true;
      } else {
        /**
         * It is possible that static or dynamic peer may have been removed
         * from staticPeerEoRReceived_() and/or dynamicPeerEoRReceived_()
         *
         * Fallthrough to check if we can declare all EoR received for
         * expected static and dynamic peers
         */
      }
    }
  } else {
    /*
     * if we have no static and dynamic peers and GR state was not loaded do
     * nothing.
     */
    if (!owner_.isGrStateLoaded()) {
      return;
    }

    /*
     * it is possible that GR state file exists but is completely empty (no
     * peers) this can happen if two peers that support GR start restart
     * procedure at the same time. This happens quite often in emulation. In
     * this case rib will be notified as soon as this node receives EoR from
     * any peer.
     */
  }

  /*
   * Check if we received EoR from all expected peers
   * and notify RIB if not already notified
   */
  checkAndNotifyAllEoRReceived();
}

void PeerManagerVrf::checkAndNotifyAllEoRReceived() noexcept {
  if (allPeerEorsReceived_) {
    // Already observed once; skip O(N) recomputation.
    return;
  }

  auto received = [](const auto& kv) { return kv.second.first; };
  if (!std::all_of(
          staticPeerEoRReceived_.begin(),
          staticPeerEoRReceived_.end(),
          received) ||
      !std::all_of(
          dynamicPeerEoRReceived_.begin(),
          dynamicPeerEoRReceived_.end(),
          received)) {
    return;
  }

  allPeerEorsReceived_ = true;

  /*
   * Log BGP++ initialization event. Only the VRF that publishes stats logs it.
   * TODO_CTR_VRF: Log the initialization event of every VRF.
   */
  if (publishStats_) {
    BgpStats::logInitializationEvent(
        "PeerManager",
        neteng::fboss::bgp::thrift::BgpInitializationEvent::ALL_EOR_RECEIVED);
  }

  maybeNotifyRibInitialPathComputation();
}

void PeerManagerVrf::notifyRibInitialPathComputation(bool timerFired) noexcept {
  if (ribInitPathComputationNotified_) {
    XLOG(INFO, "Rib already notified about best-path start");
    return;
  }

  ribInitPathComputationNotified_ = true;

  /*
   * Budget RIB may spend waiting for all registered nexthops to resolve before
   * its initial full-sync, so the sync does not wipe FIB routes whose nexthops
   * FSDB has not resolved yet (e.g. a coordinated FSDB+bgpd restart). It is the
   * REMAINING time in the single eor_time_s budget when all peer EoRs were
   * received (eor_time_s minus the time already elapsed since the EoR timer was
   * armed).
   *
   * Why bound it by the remaining budget rather than start a fresh timer in
   * RIB: eor_time_s is one overall startup-convergence deadline, and the
   * timers that protect us are sized against that single budget -- most
   * importantly the peers' and the warm-booted agent's graceful-restart hold
   * timers (bgpd must program FIB and send EoR before GR hold expires, else the
   * GR-retained routes are purged). The PeerManager EoR wait and RIB's
   * nexthop-resolution wait therefore have to fit inside eor_time_s *together*.
   * If RIB instead waited an extra, independent eor_time_s on top of the EoR
   * wait, total convergence could roughly double and overrun those GR holds:
   * peers/agent would purge the GR-retained routes and FIB programming would
   * slip past the deadline -- the exact route loss this gate exists to prevent.
   * Capping at the leftover keeps (EoR wait + nexthop-resolution wait) <=
   * eor_time_s.
   *
   * Zero means RIB computes immediately: nexthop tracking is not in effect, or
   * the EoR timer already expired (timerFired) and is forcing computation, so
   * there is no budget left to wait on.
   */
  const auto eorWaitDuration = owner_.getEorWaitDuration();
  auto nexthopResolutionTimeout = std::chrono::milliseconds(0);
  if (owner_.isNexthopResolutionRequired() && !timerFired) {
    if (eorTimerArmedAt_ == std::chrono::steady_clock::time_point{}) {
      /*
       * The EoR timer was never armed (createAndScheduleTimers() not called).
       * Expected only for direct unit-test calls; in production the timer is
       * always armed before EoR processing, so warn loudly to surface a real
       * "timer never armed" regression rather than silently masking it. Fall
       * back to the full budget instead of letting a bogus "elapsed since
       * epoch" clamp the timeout to 0 and defeat the nexthop-resolution gate.
       */
      XLOG(WARNING)
          << "notifyRibInitialPathComputation: eorTimerArmedAt_ unset; "
             "falling back to full EoR budget for nexthop-resolution timeout";
      nexthopResolutionTimeout =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              eorWaitDuration);
    } else {
      const auto elapsed = std::chrono::steady_clock::now() - eorTimerArmedAt_;
      nexthopResolutionTimeout = std::max(
          std::chrono::milliseconds(0),
          std::chrono::duration_cast<std::chrono::milliseconds>(
              eorWaitDuration - elapsed));
    }
  }

  /*
   * Use forcePush instead of fiberPush to avoid blocking the EventBase
   * thread. fiberPush uses folly::fibers::Semaphore::wait() which requires
   * a FiberManager to yield cooperatively. notifyRibInitialPathComputation
   * is called from both timer callbacks and plain coroutines (no
   * FiberManager), so fiberPush would block the entire OS thread if ribInQ_
   * is full.
   *
   * forcePush is safe here because this is a one-shot control signal
   * (guarded by ribInitPathComputationNotified_), not a data-path operation
   * at scale.
   */
  owner_.getRibInQ().forcePush(
      RibInInitialPathComputation(vrfId_, nexthopResolutionTimeout));

  eorTimerExpired_ = timerFired;
  if (publishStats_) {
    BgpStats::setEorTimerExpired(timerFired);
  }

  // Reset EoR timer since either we 1) received all EoRs, or 2) timed out
  eorTimer_.reset();

  /*
   * Start the max-cap countdown for INITIALIZED signal publication (the last
   * line of defense in case bad peers never send egressEoR). scheduleTimer()
   * is a no-op if the timer is null (notify reached without run(), e.g. direct
   * unit-test calls) where there is no event loop to fire it anyway.
   */
  owner_.scheduleTimer(
      initializedMaxWaitTimer_,
      "initialized max-wait timer",
      kInitializedMaxWaitMultiplier * eorWaitDuration);

  owner_.maybeFinishRibInitialPathComputation(vrfId_);
}

void PeerManagerVrf::maybeNotifyRibInitialPathComputation() noexcept {
  if (ribInitPathComputationNotified_) {
    return;
  }
  if (!allPeerEorsReceived_) {
    XLOGF(
        DBG1,
        "Deferring notifyRibInitialPathComputation: allPeerEorsReceived={}",
        allPeerEorsReceived_);
    return;
  }
  notifyRibInitialPathComputation(/*timerFired=*/false);
}

void PeerManagerVrf::createEorTimer() noexcept {
  // Made on the evb thread; its callback runs there too.
  eorTimer_ = folly::AsyncTimeout::make(
      owner_.getEventBase(), [this]() noexcept { onEorConvergenceTimeout(); });
}

void PeerManagerVrf::onEorConvergenceTimeout() noexcept {
  if (ribInitPathComputationNotified_) {
    return;
  }

  /*
   * Log BGP++ initialization event. Only the VRF that publishes stats logs it.
   * TODO_CTR_VRF: Log the initialization event of every VRF.
   */
  if (publishStats_) {
    BgpStats::logInitializationEvent(
        "PeerManager",
        neteng::fboss::bgp::thrift::BgpInitializationEvent::EOR_TIMER_EXPIRED);
  }

  // Notify RIB since we hit max-cap duration
  notifyRibInitialPathComputation(/*timerFired=*/true);

  // Log pending peers from initialization sequence
  std::vector<std::string> pendingStaticPeers{};
  for (const auto& [peerAddr, eorRcvd] : staticPeerEoRReceived_) {
    if (!eorRcvd.first) {
      pendingStaticPeers.emplace_back(peerAddr.str());
    }
  }

  XLOGF(
      ERR,
      "[Initialization] VRF {} still awaits the EoR from static peers: {}",
      vrfId_,
      folly::join(", ", pendingStaticPeers));

  std::vector<std::string> pendingDynamicPeers{};
  for (const auto& [peerId, eorRcvd] : dynamicPeerEoRReceived_) {
    if (!eorRcvd.first) {
      pendingDynamicPeers.emplace_back(peerId.str());
    }
  }

  XLOGF(
      ERR,
      "[Initialization] VRF {} still awaits the EoR from dynamic peers: {}",
      vrfId_,
      folly::join(", ", pendingDynamicPeers));
}

void PeerManagerVrf::maybeMarkInitialized() noexcept {
  if (allEgressEoRSent_) {
    return;
  }

  /*
   * If state machine has not reached to ribInitialAnnouncementStarted_
   * then not ready to move to initialization state yet
   * FIB_SYNCED indirectly represented by ribInitialAnnouncementStarted_
   * must happen first
   */
  if (!ribInitialAnnouncementStarted_) {
    return;
  }

  if (!checkAllEoRSent()) {
    // yet confirm the last egressEoR sent. Just return.
    return;
  }

  allEgressEoRSent_ = true;

  markInitialized();
}

void PeerManagerVrf::markInitialized() noexcept {
  if (initialized_) {
    return;
  }

  // Reset the initializedSignalTimer for max-cap purpose
  initializedMaxWaitTimer_.reset();

  if (!publishStats_) {
    initialized_ = true;
    XLOGF(INFO, "VRF {}: Successfully sent EoR to all peers", vrfId_);
    return;
  }

  // Log BGP++ initializationn event
  BgpStats::logInitializationEvent(
      "PeerManager",
      neteng::fboss::bgp::thrift::BgpInitializationEvent::INITIALIZED);

  // Set convergence time
  int64_t duration = BgpStats::getInitializationDurationMs();
  if (!eorTimerExpired_) {
    BgpStats::setConvergenceTime(duration);
  }

  /*
   * Check totalSendPrefixCount
   * ATTN:
   *  - `totalSentPrefixCount` is updated inside AdjRib;
   *  - This is an alerting sign indicating no prefixes is sent after
   * INITIALIZED;
   */
  if (totalSentPrefixCount == 0) {
    BgpStats::setNoPrefixSent();
  }

  // Mark initialized_ variable for internal access
  initialized_ = true;
  BgpStats::setPeerManagerReachesInitializedTimeout(false);

  XLOGF(
      INFO,
      "{}Successfully sent EoR to all peers. Convergence time {}ms{}",
      facebook::fboss::BGPAlert().str(),
      duration,
      eorTimerExpired_ ? " (EoR Timer Expired)" : "");
}

void PeerManagerVrf::createInitializedMaxWaitTimer() noexcept {
  /*
   * Created here; armed later in notifyRibInitialPathComputation(). Its
   * callback runs on the evb thread.
   */
  initializedMaxWaitTimer_ = folly::AsyncTimeout::make(
      owner_.getEventBase(),
      [this]() noexcept { onInitializedMaxWaitTimeout(); });
}

void PeerManagerVrf::onInitializedMaxWaitTimeout() noexcept {
  if (initialized_) {
    return;
  }

  if (publishStats_) {
    BgpStats::setPeerManagerReachesInitializedTimeout(true);

    // Check totalSendPrefixCount
    if (totalSentPrefixCount == 0) {
      BgpStats::setNoPrefixSent();
    }

    // Log BGP++ initialization event
    BgpStats::logInitializationEvent(
        "PeerManager",
        neteng::fboss::bgp::thrift::BgpInitializationEvent::INITIALIZED);
  }

  // Mark initialized_ variable for internal access
  initialized_ = true;

  /*
   * Log peers still pending egress EoR. The ingress EoR should have been
   * caught logged when eorTimer_ expired
   */
  logEoRPeers(false /* check egressEoR */);
}

void PeerManagerVrf::processEgressEoR(
    const nettools::bgplib::BgpPeerId& peerId) noexcept {
  XLOGF(INFO, "Process EgressEoR towards {}", peerId.str());

  if (allEgressEoRSent_) {
    XLOGF(
        DBG2,
        "[Initialization] EgressEoR already received from {}. Skip.",
        peerId.str());

    return;
  }

  if (staticPeerEoRReceived_.size() != 0 ||
      dynamicPeerEoRReceived_.size() != 0) {
    /*
     * Match static peer's peerAddr or dynamic peer's peerId.
     * In case there is no match, do nothing.
     */
    auto staticIt = staticPeerEoRReceived_.find(peerId.peerAddr);
    if (staticIt != staticPeerEoRReceived_.end()) {
      // Mark egressEoR(NOT ingressEoR) being sent out
      staticIt->second.second = true;
    } else {
      auto dynamicIt = dynamicPeerEoRReceived_.find(peerId);
      if (dynamicIt != dynamicPeerEoRReceived_.end()) {
        dynamicIt->second.second = true;
      } else {
        /**
         * It is possible that static or dynamic peer may have been removed
         * from staticPeerEoRReceived_() and/or dynamicPeerEoRReceived_()
         *
         * Fallthrough to check if EoR for expected static and dynamic peers
         * have been sent and if so move to initialized_
         */
      }
    }
  }

  maybeMarkInitialized();
}

void PeerManagerVrf::processNeighborRouteChangeDuringInitialization(
    const nettools::bgplib::BgpPeerId& peerId) noexcept {
  /*
   * Remove neighbor from static/dynamic EOR waiting list if present
   * It is a no-op if the key is not present in the unordered maps.
   */
  bool removedPeer = (staticPeerEoRReceived_.erase(peerId.peerAddr) > 0);

  removedPeer |= (dynamicPeerEoRReceived_.erase(peerId) > 0);

  /*
   * If peer was not removed, do nothing
   * Peer could have not been removed under the following cases
   * 1. Case 1: Peer was already removed from the static/dynamic EOR waiting
   *    list
   * 2. Case 2: Static/Dynamic EOR waiting list was empty as GR state was not
   *    loaded
   * 3. Case 3: Static/Dynamic EOR waiting list was empty, GR state was loaded
   *    but completely empty.
   *            This can happen if two peers that support GR start restart
   *            procedure at the same time. This happens quite often in
   *            emulation and here the RIB is notified as soon as EoR is
   *            received from any peer.
   */
  if (!removedPeer) {
    return;
  }

  // Peer was removed, exclude peer from initialization
  XLOGF(
      INFO,
      "[Initialization] Session down. Exclude peer: {} from initialization",
      peerId.str());

  /*
   * Check if all EORs have been received and notify RIB if not already
   * notified.
   */
  checkAndNotifyAllEoRReceived();

  // Check if we can now move to Initialized state
  maybeMarkInitialized();
}

bool PeerManagerVrf::checkAllEoRSent() {
  const auto& adjRibs = owner_.getAdjRibs();
  const auto& peerAddrToIds = owner_.getPeerAddrToIds();
  bool isAllEoRSent = true;
  for (const auto& [peerAddr, eoR] : staticPeerEoRReceived_) {
    if (!eoR.first) {
      // Case 1: skip waiting peer if no ingress EoR received
      continue;
    }
    if (!peerAddrToIds.contains(peerAddr)) {
      // Case 2: skip waiting peer where no session established at all
      continue;
    }
    for (const auto& peerId : peerAddrToIds.at(peerAddr)) {
      // Case 3: skip waiting peer is session flapped, aka, adjRib down
      if (adjRibs.contains(peerId) &&
          adjRibs.at(peerId)->isStateEstablished()) {
        // Only wait for session EoR if adjRib has session established.
        isAllEoRSent &= eoR.second;
      }
    }
  }
  for (const auto& [peerId, eoR] : dynamicPeerEoRReceived_) {
    if (!eoR.first) {
      // Case 1: skip waiting peer if no ingress EoR received
      continue;
    }
    /*
     * Case 2: skip waiting peer where no session established at all
     * Case 3: skip waiting peer is session flapped, aka, adjRib down
     */
    if (adjRibs.contains(peerId) && adjRibs.at(peerId)->isStateEstablished()) {
      // Only wait for session EoR if adjRib has session established.
      isAllEoRSent &= eoR.second;
    }
  }
  return isAllEoRSent;
}

void PeerManagerVrf::logEoRPeers(const bool isIngressEoR) noexcept {
  if (isIngressEoR) { // Case 1
    // Log static peer addresses of the pending ingress EoR
    std::vector<std::string> staticEorNotRcvdFrom;
    for (const auto& [peerId, eoRRcvd] : staticPeerEoRReceived_) {
      if (!eoRRcvd.first) {
        staticEorNotRcvdFrom.emplace_back(peerId.str());
      }
    }
    XLOGF_IF(
        INFO,
        staticEorNotRcvdFrom.size(),
        "Still awaits receiving the EoR markers from {} static peers: [{}]",
        staticEorNotRcvdFrom.size(),
        folly::join(", ", staticEorNotRcvdFrom));

    // Log dynamic peer addresses of the pending ingress EoR
    std::vector<std::string> dynamicEorNotRcvdFrom;
    for (const auto& [peerId, eoRRcvd] : dynamicPeerEoRReceived_) {
      if (!eoRRcvd.first) {
        dynamicEorNotRcvdFrom.emplace_back(peerId.str());
      }
    }
    XLOGF_IF(
        INFO,
        dynamicEorNotRcvdFrom.size(),
        "Still awaits receiving the EoR marker from {} dynamic peers: [{}]",
        dynamicEorNotRcvdFrom.size(),
        folly::join(", ", dynamicEorNotRcvdFrom));

  } else { // Case 2
    // Log the static peer addresses of the pending egress EoR.
    std::vector<std::string> staticEorNotSentBackTo;
    for (const auto& [peerId, eoRRcvd] : staticPeerEoRReceived_) {
      if (!eoRRcvd.second) {
        staticEorNotSentBackTo.emplace_back(peerId.str());
      }
    }
    XLOGF_IF(
        INFO,
        staticEorNotSentBackTo.size(),
        "Still awaits sending EoR marker back to {} static peers: [{}]",
        staticEorNotSentBackTo.size(),
        folly::join(", ", staticEorNotSentBackTo));
    // Log the dynamic peer addresses of the pending egress EoR.
    std::vector<std::string> dynamicEorNotSentBackTo;
    for (const auto& [peerId, eoRRcvd] : dynamicPeerEoRReceived_) {
      if (!eoRRcvd.second) {
        dynamicEorNotSentBackTo.emplace_back(peerId.str());
      }
    }
    XLOGF_IF(
        INFO,
        dynamicEorNotSentBackTo.size(),
        "Still awaits sending EoR marker back to {} dynamic peers: [{}]",
        dynamicEorNotSentBackTo.size(),
        folly::join(", ", dynamicEorNotSentBackTo));
  }
}

/**
 * @brief  Build a Group Name as a key for a specific peer
 *
 * @params peerId peer for which to build groupName key
 * @params peeringParams peering parameters for passed in peer
 *
 * @return std::string
 */
std::string PeerManagerVrf::buildAdjRibOutGroupName(
    const nettools::bgplib::BgpPeerId& peerId,
    const PeeringParams& peeringParams) noexcept {
  if (peeringParams.peerGroupName.has_value()) {
    return *peeringParams.peerGroupName;
  }

  return peerId.peerAddr.str();
}

} // namespace facebook::bgp
