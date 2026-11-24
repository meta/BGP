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

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/BgpProfiler.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"
#include "neteng/fboss/bgp/cpp/config/ConfigStructs.h"
#include "neteng/fboss/bgp/cpp/peer/PeerManagerBase.h"
#include "neteng/fboss/bgp/cpp/rib/Utils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

namespace facebook::bgp {

PeerManagerVrf::PeerManagerVrf(
    VrfId vrfId,
    PeerManagerBase& owner,
    folly::coro::CancellableAsyncScope& asyncScope)
    : vrfId_(vrfId), owner_(owner), asyncScope_(asyncScope) {
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
  if (vrfId_ == fboss::kDefaultVrf) {
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
  if (vrfId_ == fboss::kDefaultVrf) {
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
  if (vrfId_ == fboss::kDefaultVrf) {
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
