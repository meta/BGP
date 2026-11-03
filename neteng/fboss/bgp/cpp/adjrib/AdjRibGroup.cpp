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

#include <folly/Poly.h>
#include <folly/Portability.h>
#include <folly/ScopeGuard.h>
#include <folly/Singleton.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Collect.h>
#include <folly/coro/CurrentExecutor.h>
#include <folly/coro/Sleep.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/EventBase.h>
#include <folly/logging/xlog.h>

#include "common/network/AddressUtil.h"
#include "neteng/fboss/bgp/cpp/BgpProfiler.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibCommon.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibGroup.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibGroupSerializer.h"
#include "neteng/fboss/bgp/cpp/adjrib/LegacyV4NlriEncoding.h"
#include "neteng/fboss/bgp/cpp/adjrib/WellKnownCommunityFilter.h"
#include "neteng/fboss/bgp/cpp/changeTracker/ConsumerBitmap.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/Utils.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyManager.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

namespace facebook::bgp {

/*
 * Global cache for policy result strings (shared with AdjRib)
 * This is needed because AdjRibEntry::setPostPolicy() references it
 */
extern PostPolicyResultCacheT postPolicyResultCache_;

AdjRibOutGroup::~AdjRibOutGroup() {
  XLOGF(INFO, "Destroyed update group: {}", groupDescriptor_);
}

/*
 * @brief Cooperatively drain asyncScope_ before group destruction.
 *
 * Cleanup sequence:
 * 1. Request cancellation of all pending coroutines
 * 2. Wait for all coroutines to complete (joinAsync)
 *
 * - asyncScope_ holds coroutines for deferredPushToPeer operations
 * - Without cleanup, coroutines may still be running when group is destroyed
 * - This causes use-after-free when coroutines access group members
 *
 * Must be co_awaited before the group is destroyed. Called by
 * UpdateGroupManager::maybeDestroyUpdateGroups().
 */
folly::coro::Task<void> AdjRibOutGroup::drainAsyncScope() {
  XLOGF(
      DBG1,
      "Group {} drainAsyncScope: canceling pending async operations",
      groupDescriptor_);

  asyncScope_.requestCancellation();
  co_await asyncScope_.joinAsync();

  XLOGF(
      DBG1,
      "Group {} drainAsyncScope: async cleanup complete",
      groupDescriptor_);
}

void AdjRibOutGroup::deleteFromPathTree(
    AdjRibPathTree& pathTree,
    const folly::CIDRNetwork& prefix,
    const AdjRibOutOwnerKey& ownerKey,
    uint32_t pathId) noexcept {
  deleteFromPathTree(
      pathTree,
      getRadixNodeItrFromPathTree(pathTree, prefix),
      ownerKey,
      pathId);
}

void AdjRibOutGroup::deleteFromPathTree(
    AdjRibPathTree& pathTree,
    AdjRibPathTree::Iterator&& itr,
    const AdjRibOutOwnerKey& ownerKey,
    uint32_t pathId) noexcept {
  if (itr.atEnd()) {
    /*
     * Prefix does not exist, nothing to delete
     */
    return;
  }

  auto ownerItr = itr.value().find(ownerKey);
  if (ownerItr == itr.value().end()) {
    /*
     * owner for prefix does not exist, nothing to delete
     */
    return;
  }

  auto pathItr = ownerItr->second.find(pathId);
  if (pathItr == ownerItr->second.end()) {
    /*
     * path entry does not exist, nothing to delete
     */
    return;
  }

  ownerItr->second.erase(pathId);
  if (ownerItr->second.size() == 0) {
    /*
     * No paths for this owner in a given prefix, delete owner
     */
    itr.value().erase(ownerItr);
  }

  if (itr.value().size() == 0) {
    /*
     * No owner for this prefix, delete prefix
     */
    pathTree.erase(itr);
  }
}

void AdjRibOutGroup::deleteFromLiteTree(
    AdjRibLiteTree& liteTree,
    const folly::CIDRNetwork& prefix,
    const AdjRibOutOwnerKey& ownerKey) noexcept {
  deleteFromLiteTree(
      liteTree, getRadixNodeItrFromLiteTree(liteTree, prefix), ownerKey);
}

void AdjRibOutGroup::deleteFromLiteTree(
    AdjRibLiteTree& liteTree,
    AdjRibLiteTree::Iterator&& itr,
    const AdjRibOutOwnerKey& ownerKey) noexcept {
  if (itr.atEnd()) {
    /*
     * Prefix does not exist, nothing to delete
     */
    return;
  }

  auto ownerItr = itr.value().find(ownerKey);
  if (ownerItr == itr.value().end()) {
    /*
     * owner for prefix does not exist, nothing to delete
     */
    return;
  }

  itr.value().erase(ownerItr);
  if (itr.value().size() == 0) {
    /*
     * No owner for this prefix, delete prefix
     */
    liteTree.erase(itr);
  }
}

AdjRibEntry* FOLLY_NONNULL AdjRibOutGroup::addToPathTree(
    AdjRibPathTree& pathTree,
    const folly::CIDRNetwork& prefix,
    const AdjRibOutOwnerKey& ownerKey,
    uint32_t pathId) noexcept {
  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>> paths;

  auto radixNodeItr = getRadixNodeItrFromPathTree(pathTree, prefix);
  if (radixNodeItr.atEnd()) {
    folly::F14ValueMap<
        AdjRibOutOwnerKey,
        folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>>,
        AdjRibOutOwnerKeyHash>
        owners;

    owners[ownerKey] = std::move(paths);
    radixNodeItr =
        pathTree.insert(prefix.first, prefix.second, std::move(owners)).first;
  } else {
    if (radixNodeItr.value().find(ownerKey) == radixNodeItr.value().end()) {
      radixNodeItr.value()[ownerKey] = std::move(paths);
    }
  }

  auto ownerItr = radixNodeItr.value().find(ownerKey);
  ownerItr->second[pathId] = std::make_unique<AdjRibEntry>(pathId);
  return ownerItr->second.at(pathId).get();
}

AdjRibEntry* FOLLY_NONNULL AdjRibOutGroup::addToLiteTree(
    AdjRibLiteTree& liteTree,
    const folly::CIDRNetwork& prefix,
    const AdjRibOutOwnerKey& ownerKey,
    uint32_t pathId) noexcept {
  auto radixNodeItr = getRadixNodeItrFromLiteTree(liteTree, prefix);
  if (radixNodeItr.atEnd()) {
    folly::F14ValueMap<
        AdjRibOutOwnerKey,
        std::unique_ptr<AdjRibEntry>,
        AdjRibOutOwnerKeyHash>
        owners;
    radixNodeItr =
        liteTree.insert(prefix.first, prefix.second, std::move(owners)).first;
  }

  radixNodeItr.value()[ownerKey] = std::make_unique<AdjRibEntry>(pathId);
  return radixNodeItr.value().at(ownerKey).get();
}

AdjRibEntry* FOLLY_NULLABLE AdjRibOutGroup::getFromPathTree(
    AdjRibPathTree& pathTree,
    const folly::CIDRNetwork& prefix,
    const AdjRibOutOwnerKey& ownerKey,
    uint32_t pathId) noexcept {
  auto radixNodeItr = getRadixNodeItrFromPathTree(pathTree, prefix);
  return getAdjRibEntryFromPathNodeItr(radixNodeItr, ownerKey, pathId);
}

AdjRibEntry* FOLLY_NULLABLE AdjRibOutGroup::getAdjRibEntryFromPathNodeItr(
    const AdjRibPathTree::Iterator& itr,
    const AdjRibOutOwnerKey& ownerKey,
    uint32_t pathId) noexcept {
  if (itr.atEnd()) {
    /*
     * Prefix does not exist
     */
    return nullptr;
  }

  auto ownerItr = itr.value().find(ownerKey);
  if (ownerItr == itr.value().end()) {
    /*
     * Owner entry for that specific prefix does not exist
     */
    return nullptr;
  }

  auto pathItr = ownerItr->second.find(pathId);
  if (pathItr == ownerItr->second.end()) {
    /*
     * Path entry specific to prefix/owner does not exist
     */
    return nullptr;
  }

  return pathItr->second.get();
}

AdjRibEntry* FOLLY_NULLABLE AdjRibOutGroup::getFromLiteTree(
    AdjRibLiteTree& liteTree,
    const folly::CIDRNetwork& prefix,
    const AdjRibOutOwnerKey& ownerKey) noexcept {
  auto radixNodeItr = getRadixNodeItrFromLiteTree(liteTree, prefix);
  return getAdjRibEntryFromLiteNodeItr(radixNodeItr, ownerKey);
}

AdjRibEntry* FOLLY_NULLABLE AdjRibOutGroup::getAdjRibEntryFromLiteNodeItr(
    const AdjRibLiteTree::Iterator& itr,
    const AdjRibOutOwnerKey& ownerKey) noexcept {
  if (itr.atEnd()) {
    /*
     * Prefix does not exist
     */
    return nullptr;
  }

  auto ownerItr = itr.value().find(ownerKey);
  if (ownerItr == itr.value().end()) {
    /*
     * Owner entry for that specific prefix does not exist
     */
    return nullptr;
  }

  return ownerItr->second.get();
}

AdjRibOutGroup::AdjRibPathTree::Iterator
AdjRibOutGroup::getRadixNodeItrFromPathTree(
    AdjRibPathTree& pathTree,
    const folly::CIDRNetwork& prefix) noexcept {
  return pathTree.exactMatch(prefix.first, prefix.second);
}

AdjRibOutGroup::AdjRibLiteTree::Iterator
AdjRibOutGroup::getRadixNodeItrFromLiteTree(
    AdjRibLiteTree& liteTree,
    const folly::CIDRNetwork& prefix) noexcept {
  return liteTree.exactMatch(prefix.first, prefix.second);
}

AdjRibEntry* AdjRibOutGroup::addRibEntry(
    const folly::CIDRNetwork& prefix,
    const AdjRibOutOwnerKey& ownerKey,
    uint32_t pathId) noexcept {
  if (groupKey_.sendAddPath) {
    return addToPathTree(PathTree_, prefix, ownerKey, pathId);
  }
  return addToLiteTree(LiteTree_, prefix, ownerKey, pathId);
}

std::pair<AdjRibEntry * FOLLY_NULLABLE, bool>
AdjRibOutGroup::getRibPathEntrySharedOrPeer(
    const folly::CIDRNetwork& prefix,
    const AdjRibOutOwnerKey& peerOwnerKey,
    uint32_t pathId) noexcept {
  auto itr = getRadixNodeItrFromPathTree(PathTree_, prefix);
  auto* entry = getAdjRibEntryFromPathNodeItr(itr, peerOwnerKey, pathId);
  if (entry) {
    return {entry, true /* isPerPeerEntry */};
  }
  return {
      getAdjRibEntryFromPathNodeItr(itr, getGroupOwnerKey(), pathId),
      false /* isPerPeerEntry */};
}

std::pair<AdjRibEntry * FOLLY_NULLABLE, bool>
AdjRibOutGroup::getRibLiteEntrySharedOrPeer(
    const folly::CIDRNetwork& prefix,
    const AdjRibOutOwnerKey& peerOwnerKey) noexcept {
  auto itr = getRadixNodeItrFromLiteTree(LiteTree_, prefix);
  auto* entry = getAdjRibEntryFromLiteNodeItr(itr, peerOwnerKey);
  if (entry) {
    return {entry, true /* isPerPeerEntry */};
  }
  return {
      getAdjRibEntryFromLiteNodeItr(itr, getGroupOwnerKey()),
      false /* isPerPeerEntry */};
}

std::pair<AdjRibEntry * FOLLY_NULLABLE, bool>
AdjRibOutGroup::getRibEntrySharedOrPeer(
    const folly::CIDRNetwork& prefix,
    const AdjRibOutOwnerKey& peerOwnerKey,
    uint32_t pathId,
    uint64_t detachedRibVersion) noexcept {
  auto [entry, isPerPeerEntry] = groupKey_.sendAddPath
      ? getRibPathEntrySharedOrPeer(prefix, peerOwnerKey, pathId)
      : getRibLiteEntrySharedOrPeer(prefix, peerOwnerKey);
  if (isPerPeerEntry) {
    return {entry, true /* isPerPeerEntry */};
  }
  if (entry &&
      !isEntryShared(
          /*peerHasOwnEntry=*/false,
          detachedRibVersion,
          entry->getRibVersion())) {
    return {nullptr, false /* isPerPeerEntry */};
  }
  return {entry, false /* isPerPeerEntry */};
}

AdjRibEntry* FOLLY_NULLABLE AdjRibOutGroup::getWritableRibEntryForPeer(
    const folly::CIDRNetwork& prefix,
    const AdjRibOutOwnerKey& peerOwnerKey,
    uint32_t pathId,
    uint64_t detachedRibVersion) noexcept {
  /* addPath */
  if (groupKey_.sendAddPath) {
    auto radixNodeItr = getRadixNodeItrFromPathTree(PathTree_, prefix);
    auto* entry =
        getAdjRibEntryFromPathNodeItr(radixNodeItr, peerOwnerKey, pathId);
    if (entry) {
      return entry;
    }
    entry =
        getAdjRibEntryFromPathNodeItr(radixNodeItr, getGroupOwnerKey(), pathId);
    if (!entry ||
        !isEntryShared(
            /*peerHasOwnEntry=*/false,
            detachedRibVersion,
            entry->getRibVersion())) {
      return nullptr;
    }
    return copyEntryForOwner(radixNodeItr.value(), peerOwnerKey, entry);
  }

  /* non addPath */
  auto radixNodeItr = getRadixNodeItrFromLiteTree(LiteTree_, prefix);
  auto* entry = getAdjRibEntryFromLiteNodeItr(radixNodeItr, peerOwnerKey);
  if (entry) {
    return entry;
  }
  entry = getAdjRibEntryFromLiteNodeItr(radixNodeItr, getGroupOwnerKey());
  if (!entry ||
      !isEntryShared(
          /*peerHasOwnEntry=*/false,
          detachedRibVersion,
          entry->getRibVersion())) {
    return nullptr;
  }
  return copyEntryForOwner(radixNodeItr.value(), peerOwnerKey, entry);
}

uint32_t AdjRibOutGroup::getPeerEntriesCountFromPathTree(
    AdjRibPathTree& pathTree,
    const AdjRibOutOwnerKey& ownerKey) noexcept {
  uint32_t size = 0;

  for (auto itr = pathTree.begin(); itr != pathTree.end(); itr++) {
    if (itr->value().find(ownerKey) != itr->value().end()) {
      size += itr->value().at(ownerKey).size();
    }
  }

  return size;
}

uint32_t AdjRibOutGroup::getPeerEntriesCountFromLiteTree(
    AdjRibLiteTree& liteTree,
    const AdjRibOutOwnerKey& ownerKey) noexcept {
  uint32_t size = 0;

  for (auto itr = liteTree.begin(); itr != liteTree.end(); itr++) {
    if (itr->value().find(ownerKey) != itr->value().end()) {
      size++;
    }
  }

  return size;
}

/*
 * @brief  Activate change list consumer for this update group
 *         Registers group with change tracker and starts polled consumption
 *         Same pattern as AdjRib::activateChangeListConsumer
 *
 * @param  none
 *
 * @return void
 */
void AdjRibOutGroup::registerGroupConsumer() noexcept {
  if (changeListConsumeTimer_) {
    XLOGF(
        ERR,
        "update group {} has already been registered to changeListTracker",
        groupDescriptor_);
    return;
  }

  /*
   * Create the group's change list consumer on first registration (after the
   * initial RIB dump). The tracker and bitmaps must already be wired via
   * setChangeListTracker().
   */
  if (!changeListConsumer_) {
    if (!changeListTracker_ || !addPathConsumerBitmap_ ||
        !nonAddPathConsumerBitmap_) {
      XLOGF(
          ERR,
          "Cannot create change list consumer for group {}: tracker/bitmaps not set",
          groupDescriptor_);
      return;
    }
    changeListConsumer_ = std::make_shared<AdjRibOutGroupConsumer>(
        changeListTracker_,
        shared_from_this(),
        fmt::format("UpdateGroup-{}", groupName_),
        evb_,
        *addPathConsumerBitmap_,
        *nonAddPathConsumerBitmap_);
  }

  XLOGF(
      DBG1, "Register update group {} to changeListTracker", groupDescriptor_);

  // Register with tracker in polled mode
  changeListConsumer_->registerWithTracker();
  changeListConsumer_->setPolledMode();
  changeListConsumer_->setBitmap();

  createChangeListConsumeTimer();
}

void AdjRibOutGroup::tryIterateChangesToEnd() noexcept {
  changeListConsumer_->iterateChangesToEnd();
  /*
   * iterateChangesToEnd() runs to the end of the changelist. It is important
   * for us to set the correct rib version at the end of this call, which is
   * actually the maxRibVersion_ perceived by PeerManager's shadow RIB.
   *
   * This is because:
   *   1. Not all changes from RIB are published to the changelist
   *   2. Not all changes published to the changelist are for this consumer.
   *
   * If we do not have the opportunity to run processChangeItem on
   * an actual item, then we can only update to the RIB version of
   * a change item seen by this consumer. This means we do not
   * reflect that consuming to the end of the changelist will
   * update the maxRibVersion_ to the latest snapshot of the RIB, which
   * is undesired.
   *
   * Reaching the tail is guaranteed rather than checked: no boundary marker is
   * passed, and AdjRibOutGroupConsumer::processChangeItem() always returns
   * ProcessResult::CONTINUE, never YIELD -- the only other way the walk can
   * stop short. Nothing tracks the version per item, so introducing a YIELD on
   * this path would silently stall the group's version at its pre-drain value.
   */
  setLastSeenRibVersion(getShadowRibMaxVersion());
}

void AdjRibOutGroup::createChangeListConsumeTimer() noexcept {
  changeListConsumeTimer_ = folly::AsyncTimeout::make(evb_, [this]() noexcept {
    /*
     * The callback runs the change list consumption inline in the timer
     * callback. TODO(T274212842): consider running change list consumption as a
     * cancellable coroutine on packing timer expiry instead.
     *
     * Guard against use-after-reset: the MRAI timer is scheduled, but before it
     * fires other EventBase callbacks may run — including session termination
     * paths that call deactivateChangeListConsumer() and nullify
     * changeListConsumer_. Under scale with mass peer flaps, this window is
     * wide enough to hit consistently.
     */
    if (!changeListConsumer_) {
      XLOGF(
          WARN,
          "Group {}: CL consume timer fired but "
          "changeListConsumer_ is null, skipping",
          groupDescriptor_);
      return;
    }
    // Use iterator-based interface for consuming change items
    auto previousRibVersion = lastSeenRibVersion_;
    {
      ScopedProfile profile("AdjRibOutGroup::consumeChangeList");
      tryIterateChangesToEnd();
    }
    if (changeListConsumer_->isStale(kConsumerStalenessThreshold) &&
        !changeListConsumer_->isStalenessLogged()) {
      XLOGF(
          WARN,
          "Group {}: change list consumer stale for {}ms, marker has not advanced",
          groupDescriptor_,
          changeListConsumer_->stalenessDuration().count());
      changeListConsumer_->markStalenessLogged();
    }
    XLOGF_IF(
        DBG1,
        previousRibVersion != lastSeenRibVersion_,
        "Group {}: Updating cached RIB version from {} to {}, state: {} with packing list to send: {}",
        groupDescriptor_,
        previousRibVersion,
        lastSeenRibVersion_,
        state_,
        !attrToPrefixMap_.empty());

    /* A sharing DSP needs the group consumer to keep advancing to the end. */
    if (hasEntrySharingPeers()) {
      scheduleChangeListConsumeTimer();
    }
    /*
     * Trigger message building if route updates or EoRs are pending. The group
     * state does not gate this; packingInProgress_ does, so an in-flight
     * buildAndSendGroupBgpMessages is neither duplicated nor reported as
     * drained.
     */
    if (!packingInProgress_) {
      if (hasPendingMessages()) {
        state_ = UpdateGroupState::WAITING;
        asyncScope_.add(
            folly::coro::co_withExecutor(
                &evb_, buildAndSendGroupBgpMessages()));
      } else {
        tryRejoinDetachedPeersOnAllChangesProcessed();
      }
    }
  });
}

bool AdjRibOutGroup::hasPendingMessages() const noexcept {
  return !attrToPrefixMap_.empty() || egressEoRsPending();
}

void AdjRibOutGroup::scheduleChangeListConsumeTimer() noexcept {
  if (!changeListConsumeTimer_) {
    XLOGF(
        ERR,
        "Group {}: cannot schedule consume timer — timer not created",
        groupDescriptor_);
    return;
  }
  if (changeListConsumeTimer_->isScheduled()) {
    return;
  }
  changeListConsumeTimer_->scheduleTimeout(
      std::chrono::milliseconds(mraiInterval_));
}

void AdjRibOutGroup::cancelChangeListConsumeTimer() noexcept {
  if (changeListConsumeTimer_) {
    changeListConsumeTimer_->cancelTimeout();
  }
}

/*
 * @brief  Deactivate change list consumer for this update group
 *         Unregisters group from change tracker and cancels timer
 *         Same pattern as AdjRib::deactivateChangeListConsumer
 *
 * @param  none
 *
 * @return void
 */
void AdjRibOutGroup::deactivateChangeListConsumer() noexcept {
  // cleanup any pending attrToPrefixMap_
  clearPackingList();

  resetChangeListConsumeTimer();

  /*
   * Always deregister the consumer, even if timer wasn't active.
   * This ensures bitPosition_ is set to -1 so the consumer destructor
   * won't try to access the (possibly destroyed) tracker.
   */
  if (changeListConsumer_) {
    changeListConsumer_->terminate();
    changeListConsumer_->resetBitmap();
    changeListConsumer_->deregisterFromTracker();
    XLOGF(
        DBG1, "Deregistered group {} from changeListTracker", groupDescriptor_);
  }
}

/*
 * @brief  Schedule initial dump to start asynchronously
 *         Transitions state from UNINITIALIZED to building packing list
 *
 * @param  none
 *
 * @return void
 */
void AdjRibOutGroup::scheduleInitialDump() noexcept {
  if (state_ != UpdateGroupState::UNINITIALIZED) {
    XLOGF(
        WARN,
        "Group {} not in UNINITIALIZED state, skipping initial dump",
        groupDescriptor_);
    return;
  }

  if (isRibDumpScheduled()) {
    XLOGF(
        DBG1,
        "Group {} initial dump already scheduled, coalescing duplicate request",
        groupDescriptor_);
    return;
  }

  if (!enableUpdateGroup_) {
    return;
  }

  XLOGF(INFO, "Scheduling initial RIB dump for group {}", groupDescriptor_);
  asyncScope_.add(
      co_withExecutor(&evb_, buildAndScheduleSendInitialDumpFromShadowRib()),
      getCancellationTokenForNewGroupDump());
}

/*
 * Shared core: walk ShadowRib and process each entry through
 * processRibAnnouncedEntryForGroup() into the group packing list.
 * Used by both initial dump and policy re-evaluation.
 */
void AdjRibOutGroup::walkAndProcessShadowRib(bool sendWithEoR) {
  size_t processedEntries = 0;

  /*
   * Track whether any entry was skipped because it is already pending on this
   * group's changeList consumer. If so, the group will still receive those
   * entries (and advance its version) via the changeList, so we must NOT
   * advance its lastSeenRibVersion off this dump.
   */
  bool skippedChangeListEntry = false;

  /*
   * Walk through all shadow RIB entries
   * NOTE: this ensures maximum packing without chunk limit.
   */
  for (const auto& [prefix, srEntryPtr] : shadowRibEntries_) {
    // TODO: add cancellable token to make sure this iteration is interruptible
    if (!srEntryPtr) {
      continue;
    }

    /*
     * If this shadow rib entry is already on a changeList for this group's
     * consumer, skip it from the dump. Mirrors
     * PeerManagerBase::processRibDumpReq at the peer level.
     */
    if (changeListTracker_ && changeListConsumer_ &&
        changeListTracker_->isConsumerSetOnTrackableObject(
            srEntryPtr.get(), changeListConsumer_)) {
      skippedChangeListEntry = true;
      continue;
    }

    const auto& srEntry = srEntryPtr->get();

    if (!groupKey_.sendAddPath) {
      // Send out bestpath only (no add-path)
      const auto bestpath = srEntry.bestpath;
      if (bestpath) {
        if (isShadowRibRouteInWithdraw(bestpath->flags)) {
          continue;
        }
        RibOutAnnouncementEntry entry(
            prefix,
            kDefaultPathID,
            bestpath->peer,
            bestpath->attrs,
            srEntry.switchId,
            srEntry.multiPathSize,
            srEntry.aggregateReceivedUcmpWeight,
            srEntry.aggregateLocalUcmpWeight,
            srEntry.ribPolicyUcmpWeight,
            srEntry.newlyInstalledInLocalRib,
            srEntry.installTimeStamp,
            srEntry.ribVersion,
            bestpath->isPartialDrain);
        processRibAnnouncedEntryForGroup(entry);
        ++processedEntries;
      }
    } else {
      // Send out all multipaths with add-path enabled
      for (const auto& [_, multipath] : srEntry.multipaths) {
        if (multipath) {
          if (isShadowRibRouteInWithdraw(multipath->flags)) {
            continue;
          }
          RibOutAnnouncementEntry entry(
              prefix,
              multipath->pathIdToSend,
              multipath->peer,
              multipath->attrs,
              srEntry.switchId,
              srEntry.multiPathSize,
              srEntry.aggregateReceivedUcmpWeight,
              srEntry.aggregateLocalUcmpWeight,
              srEntry.ribPolicyUcmpWeight,
              srEntry.newlyInstalledInLocalRib,
              srEntry.installTimeStamp,
              srEntry.ribVersion,
              multipath->isPartialDrain);
          processRibAnnouncedEntryForGroup(entry);
          ++processedEntries;
        }
      }
    }
  }

  /*
   * Mark every currently in-sync peer as owing the negotiated AFIs, mirroring
   * AdjRib::handleRibAnnouncedEntries. Per-peer EGRESS_EOR_PENDING flags are
   * the single source of truth from this point on.
   */
  if (sendWithEoR) {
    setEgressEorsPendingSyncPeers();
  }

  XLOGF(
      INFO,
      "Group {} walkAndProcessShadowRib completed with {} entries "
      "(sendAddPath={}, sendWithEoR={})",
      groupDescriptor_,
      processedEntries,
      groupKey_.sendAddPath,
      sendWithEoR);

  /*
   * If the dump covered every entry (nothing was skipped because it was already
   * pending on this group's changeList consumer), the group has now seen the
   * whole table, so set it to the PeerManager's max seen RIB version -- correct
   * even when the walk was empty (all routes withdrawn and their entries
   * erased). All members of an update group share this version since they
   * receive updates as a unit. If any entry was skipped, the group will receive
   * those entries and advance its version through the changeList instead, so
   * leave lastSeenRibVersion untouched here.
   */
  if (!skippedChangeListEntry) {
    XLOGF(
        DBG1,
        "Group {}: Updating cached RIB version from {} to {} after rib walk",
        groupDescriptor_,
        lastSeenRibVersion_,
        maxRibVersion_);
    setLastSeenRibVersion(maxRibVersion_);
  }
}

/*
 * Build initial RIB dump from shadow RIB.
 * Walks shadow RIB, builds RibOutAnnouncement, processes it.
 * Transitions from UNINITIALIZED to WAITING state.
 */
void AdjRibOutGroup::processRibDumpForGroup(bool sendWithEoR) {
  XLOGF(
      INFO,
      "Group {} starting initial dump from shadow RIB (sendWithEoR={})",
      groupDescriptor_,
      sendWithEoR);

  /* walkAndProcessShadowRib advances lastSeenRibVersion_ after the full walk.
   */
  walkAndProcessShadowRib(sendWithEoR);

  /*
   * Publish whether the walk produced work for a builder, using the same test
   * as the egress policy re-evaluation walk. A dump over an empty shadow RIB
   * that leaves no routes packed and no EoRs owed has nothing to send, so it
   * lands in IDLE rather than advertising itself as WAITING. hasPendingMessages
   * counts owed EoRs, so a group whose RIB is empty but whose peers still owe
   * EoRs stays WAITING and the send below delivers them.
   */
  state_ =
      hasPendingMessages() ? UpdateGroupState::WAITING : UpdateGroupState::IDLE;

  // Schedule the single async build+send for the initial dump.
  asyncScope_.add(
      folly::coro::co_withExecutor(&evb_, buildAndSendGroupBgpMessages()));

  initialDumpCompletionTimeMs_ =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
}

void AdjRibOutGroup::transitionInitPeersToJoinedRunning() noexcept {
  /*
   * Transition INIT peers to JOINED_RUNNING
   * These peers were waiting for initial dump and are now active
   * Note: sync bitmap bit already set when they registered
   */
  for (const auto& [bitPos, adjRib] : bitToAdjRibs_) {
    if (!adjRib) {
      continue;
    }
    adjRib->resetInInitialAnnouncement();
    if (adjRib->getPeerState() == PeerUpdateState::INIT) {
      XLOGF(
          DBG1,
          "Group {}: Peer {} at bit {} State Transition: {} -> {}",
          groupDescriptor_,
          adjRib->getPeerName(),
          bitPos,
          adjRib->getPeerState(),
          PeerUpdateState::JOINED_RUNNING);
      adjRib->setPeerState(PeerUpdateState::JOINED_RUNNING);
      adjRib->setLastSeenRibVersion(lastSeenRibVersion_);
    }
  }
}

/*
 * state_ is expected to be UNINITIALIZED whenever this task starts.
 * Cancellation handles a queued task whose group is emptied, erased, or
 * superseded before it runs.
 */
folly::coro::Task<void>
AdjRibOutGroup::buildAndScheduleSendInitialDumpFromShadowRib() {
  auto cancelToken = co_await folly::coro::co_current_cancellation_token;

  /*
   * A policy reconciliation can empty and erase this group, and the group can
   * be torn down outright, while this task is still queued. Either cancels it
   * before it ever runs. Whoever cancelled us owns the cancellation source
   * from that point on, so return without running a stale walk and without
   * touching the source.
   */
  if (cancelToken.isCancellationRequested()) {
    co_return;
  }

  if (FOLLY_UNLIKELY(testOnlyDeferInitialDump)) {
    /*
     * The dump is deferred, not completed. Retire this task's source and queue
     * a fresh dump so the group keeps reporting one scheduled until the defer
     * clears. Reset before rescheduling so scheduleInitialDump() does not
     * coalesce against our own source, and keep this above the guard below so
     * the reset does not discard the new one.
     */
    resetRibDumpCancellationSource();
    scheduleInitialDump();
    co_return;
  }

  /*
   * An armed source means "a dump task owns this group", so retire it on every
   * uncancelled exit: normal completion or the early return below. Those exits
   * leave state_ at UNINITIALIZED, so the state guard in scheduleInitialDump()
   * does not back the latch up there -- a source left armed would coalesce
   * every later request and strand the group with no dump. Re-read the token
   * here rather than trusting the check above: cancellation can land while the
   * walk runs, and the canceller has already retired or replaced the source.
   */
  auto clearLatchGuard = folly::makeGuard([this, &cancelToken]() noexcept {
    if (!cancelToken.isCancellationRequested()) {
      resetRibDumpCancellationSource();
    }
  });

  /* Covers a task that was already runnable when the group became empty, so
   * nothing cancelled it: the cancellation lands when the emptied group is
   * erased, which can trail the emptying by an event-loop turn.
   */
  if (getMemberCount() == 0) {
    co_return;
  }

  processRibDumpForGroup();

  transitionInitPeersToJoinedRunning();

  registerGroupConsumer();
  /* Keep advancing while a peer still shares the group's entries. */
  if (hasEntrySharingPeers()) {
    scheduleChangeListConsumeTimer();
  }
}

/*
 * Re-evaluate all ShadowRib entries with the current egress policy.
 * Called when the group's egress policy content changes.
 *
 * Runs for an UNINITIALIZED group too, so a group rekeyed before its initial
 * dump gets the new policy applied to the whole shadow RIB in this event-loop
 * turn instead of waiting on a deferred dump.
 *
 * Reuses walkAndProcessShadowRib() for the walk. During processing,
 * processRibAnnouncedEntryForGroup() does deep compare with existing
 * group AdjRibOut entries and only updates the packing list when the
 * policy result actually changed. Lazy clone (Phase 4) preserves
 * detached peers' old-policy state before group entries are mutated.
 */
void AdjRibOutGroup::reEvaluateSyncPeersEgressPolicy() {
  /*
   * An UNINITIALIZED group has never dumped, so its peers still owe their
   * egress EoRs and this walk is what books them. Past that point the EoRs are
   * already owed or already sent, and re-arming them here would emit a
   * duplicate.
   */
  const bool wasUninitialized = state_ == UpdateGroupState::UNINITIALIZED;

  /*
   * This walk supersedes any dump still queued for the group: it applies the
   * new policy to the whole shadow RIB inline, in this turn. A deferred walk
   * landing afterwards would redo it, and for an UNINITIALIZED group would
   * book the EoRs a second time.
   */
  if (isRibDumpScheduled()) {
    XLOGF(
        INFO,
        "Group {}: cancelling scheduled rib dump to run egress policy "
        "re-evaluation inline",
        groupDescriptor_);
    cancelRibDump();
  }

  XLOGF(
      INFO,
      "Group {}: Starting group RIB walk for egress policy re-evaluation "
      "(state={}, sendWithEoR={})",
      groupDescriptor_,
      state_,
      wasUninitialized);

  auto previousLastSeenRibVersion = lastSeenRibVersion_;
  walkAndProcessShadowRib(/*sendWithEoR=*/wasUninitialized);

  /*
   * This walk stands in for the never-dumped group's initial dump, so it owes
   * that dump's peer bookkeeping: peers parked in INIT are what the dump would
   * have moved to JOINED_RUNNING.
   */
  if (wasUninitialized) {
    transitionInitPeersToJoinedRunning();
  }

  /*
   * Consume the change list to the tail after the full walk so the group's
   * RIB-OUT reflects the latest shadow RIB and the consumer marker is advanced
   * to the end. A detached peer bounded by the group marker can then catch up
   * to the tail too.
   *
   * These change-list entries are not routes that arrived during the walk --
   * the walk is uninterrupted, so nothing is added mid-walk. They are entries
   * that were already pending on this group's consumer before the
   * re-evaluation. The walk skips those already-pending entries -- and declines
   * to advance the version when it does -- so draining them here both applies
   * their latest state and advances the consumer marker (and
   * lastSeenRibVersion) to the tail.
   *
   * A group re-evaluated before its initial dump has no consumer to drain --
   * registration normally happens at the end of the dump. Register it here
   * instead, so the walk this turn is followed by the group tracking changes
   * from that point on rather than only once a dump eventually runs.
   */
  if (changeListConsumer_) {
    tryIterateChangesToEnd();
  } else {
    registerGroupConsumer();
  }

  /*
   * Publish whether the walk produced work for a builder. This also moves a
   * group that was never dumped out of UNINITIALIZED without making an
   * unchanged walk look pending. An in-flight buildAndSendGroupBgpMessages is
   * unaffected -- it does not read the state, and its cleanup guard publishes
   * for itself when it returns.
   */
  state_ =
      hasPendingMessages() ? UpdateGroupState::WAITING : UpdateGroupState::IDLE;

  /* At this point, the group RIB-OUT is completely up to date with the new
   * policy. */
  XLOGF(
      INFO,
      "Group {}: Finished group egress policy re-evaluation for {} in-sync peers, (lastSeenRibVersion {} -> {})",
      groupDescriptor_,
      numInSyncPeers_,
      previousLastSeenRibVersion,
      lastSeenRibVersion_);
}

/*
 * @brief  Process ShadowRibEntry change from change tracker
 *         Converts shadow RIB entries to announcements at group level
 *
 * Similar to AdjRib::processShadowRibEntryChange but operates at group level.
 * This is called by AdjRibOutGroupConsumer::processChangeItem() when consuming
 * changes from the tracker.
 *
 * @param  srEntry - shadow Rib entry consumed from the changeList
 *
 * @return void
 */
void AdjRibOutGroup::processShadowRibEntryChange(
    ShadowRibEntry& srEntry) noexcept {
  /*
   * Convert shadow RIB entry to announcement entries and feed each directly
   * into the packing list via processRibAnnouncedEntryForGroup(). Unlike the
   * initial dump / re-eval walk, an incremental change never owes an EoR, so
   * there is no RibOutAnnouncement to accumulate -- the send for this batch is
   * scheduled once by the changeListConsumeTimer_ callback after all consumed
   * items are drained.
   */
  if (groupKey_.sendAddPath) {
    // Send out all multipaths with add-path enabled
    for (const auto& [_, multipath] : srEntry.multipaths) {
      if (!multipath) {
        continue;
      }
      if (isShadowRibRouteInUpdate(multipath->flags)) {
        RibOutAnnouncementEntry entry(
            srEntry.prefix,
            multipath->pathIdToSend,
            multipath->peer,
            multipath->attrs,
            srEntry.switchId,
            srEntry.multiPathSize,
            srEntry.aggregateReceivedUcmpWeight,
            srEntry.aggregateLocalUcmpWeight,
            srEntry.ribPolicyUcmpWeight);
        entry.newlyInstalledInLocalRib = srEntry.newlyInstalledInLocalRib;
        entry.installTimeStamp = srEntry.installTimeStamp;
        entry.ribVersion = srEntry.ribVersion;
        entry.isPartialDrain = multipath->isPartialDrain;
        processRibAnnouncedEntryForGroup(entry);
      } else if (isShadowRibRouteInWithdraw(multipath->flags)) {
        /*
         * Withdrawal announcements from RIB don't have attrs, however
         * when we are processing withdrawal here, it is a priori
         * shadow RIB entry that was existing with attrs in place (the
         * entry yet has not been reset for multipath) and hence it
         * should have attrs available to get the nexthop value
         *
         * Thus, we would never expect nullptr to attrs here. The check
         * here is simply a safety check
         */
        if (multipath->attrs) {
          // Process withdrawal for this path
          processGroupRibWithdraw(srEntry.prefix, multipath->pathIdToSend);
        } else {
          XLOGF(
              ERR,
              "Unexpected null attr for multipath when processing "
              "withdrawal for shadowrib entry. prefix {}, group {}",
              folly::IPAddress::networkToString(srEntry.prefix),
              groupDescriptor_);
        }
      }
    }
  } else {
    // Send out bestpath only (no add-path)
    if (!srEntry.bestpath) {
      return;
    }
    if (isShadowRibRouteInUpdate(srEntry.bestpath->flags)) {
      RibOutAnnouncementEntry entry(
          srEntry.prefix,
          kDefaultPathID,
          srEntry.bestpath->peer,
          srEntry.bestpath->attrs,
          srEntry.switchId,
          srEntry.multiPathSize,
          srEntry.aggregateReceivedUcmpWeight,
          srEntry.aggregateLocalUcmpWeight,
          srEntry.ribPolicyUcmpWeight);
      entry.newlyInstalledInLocalRib = srEntry.newlyInstalledInLocalRib;
      entry.installTimeStamp = srEntry.installTimeStamp;
      entry.ribVersion = srEntry.ribVersion;
      entry.isPartialDrain = srEntry.bestpath->isPartialDrain;
      processRibAnnouncedEntryForGroup(entry);
    } else if (isShadowRibRouteInWithdraw(srEntry.bestpath->flags)) {
      /*
       * Withdrawal announcements from RIB don't have attrs, however
       * when we are processing withdrawal here, it is a priori
       * shadow RIB entry that was existing with attrs in place (the
       * entry yet has not been reset for bestpath) and hence it
       * should have attrs available to get the nexthop value
       *
       * Thus, we would never expect nullptr to attrs here. The check
       * here is simply a safety check
       */
      if (srEntry.bestpath->attrs) {
        // Process withdrawal for bestpath
        processGroupRibWithdraw(srEntry.prefix, kDefaultPathID);
      } else {
        XLOGF(
            ERR,
            "Unexpected null attr for bestpath when processing "
            "withdrawal for shadowrib entry. prefix {}, group {}",
            folly::IPAddress::networkToString(srEntry.prefix),
            groupDescriptor_);
      }
    }
  }
}

/*
 * @brief  Process withdrawal for the group
 *         Similar to AdjRib::processRibWithdraw but at group level
 *
 * Key differences from peer-level:
 * - No deferred updates (out-delay) - not implemented yet for groups (future
 * diff)
 * - No AFI negotiation check - groups inherit AFI from peer membership
 * - No per-prefix stats tracking like AdjRib - groups optimize for memory
 * - Inline withdrawal logic instead of separate tryInsertWithdrawal() method
 * - No RibOutWithdrawalEntry struct - simpler direct call
 *
 * @param  prefix - Prefix to withdraw
 * @param  pathId - Path ID to withdraw
 *
 * @return void
 */
void AdjRibOutGroup::processGroupRibWithdraw(
    const folly::CIDRNetwork& prefix,
    uint32_t pathId) noexcept {
  if (!AdjRibCommonUtils::isAfiNegotiated(
          prefix, groupKey_.afiIpv4Negotiated, groupKey_.afiIpv6Negotiated)) {
    XLOGF(
        DBG4,
        "Ignore RibWithdrawal of prefix {}: AFI is not supported for group {}.",
        folly::IPAddress::networkToString(prefix),
        groupDescriptor_);
    return;
  }

  XLOGF(
      DBG5,
      "Group {} processing withdrawal of {}",
      groupDescriptor_,
      folly::IPAddress::networkToString(prefix));

  /*
   * TODO: Out-delay support will be added in future diff
   * When implemented: check and remove from deferredUpdates_ here
   */

  // Get group RIB-OUT entry via radix node iterator
  auto groupOwnerKey = getGroupOwnerKey();
  AdjRibEntry* adjRibEntry = nullptr;

  AdjRibPathTree::Iterator radixPathNodeItr;
  AdjRibLiteTree::Iterator radixLiteNodeItr;

  if (groupKey_.sendAddPath) {
    radixPathNodeItr = getRadixNodeItrFromPathTree(PathTree_, prefix);
    adjRibEntry =
        getAdjRibEntryFromPathNodeItr(radixPathNodeItr, groupOwnerKey, pathId);
  } else {
    radixLiteNodeItr = getRadixNodeItrFromLiteTree(LiteTree_, prefix);
    adjRibEntry =
        getAdjRibEntryFromLiteNodeItr(radixLiteNodeItr, groupOwnerKey);
  }

  if (!adjRibEntry || !adjRibEntry->getPreOut()) {
    XLOGF(
        DBG3,
        "Received withdraw of prefix {} without announcement for group {}",
        folly::IPAddress::networkToString(prefix),
        groupDescriptor_);
    return;
  }

  // Lazy clone BEFORE removal
  if (!detachedPeers_.empty()) {
    if (groupKey_.sendAddPath) {
      lazyClonePathForDetachedPeers(
          prefix, pathId, radixPathNodeItr, adjRibEntry);
    } else {
      lazyCloneLiteForDetachedPeers(prefix, radixLiteNodeItr, adjRibEntry);
    }
  }

  // Clear preOut and postOutPolicy (same as AdjRib)
  adjRibEntry->setPreOut(nullptr);
  adjRibEntry->setPostOutPolicy({});
  stats_.decrementPreOutPrefixCount(prefix.first.isV4());

  /*
   * Try to insert withdrawal into packing list
   * Only withdraw if we previously announced (has postAttr)
   */
  auto oldPostAttr = adjRibEntry->getPostAttr();
  if (oldPostAttr) {
    XLOGF_IF(
        DBG1,
        stats_.getPostOutPrefixCount() == 0,
        "Invalid sent prefix count of {} for group {}",
        folly::IPAddress::networkToString(prefix),
        groupDescriptor_);
    stats_.decrementPostOutPrefixCount(
        prefix.first.isV4(), getNumInSyncPeers());

    adjRibEntry->setPostAttr(nullptr);
    auto prefixPathId = std::make_pair(prefix, pathId);

    // Add to packing list with nullptr key (marks as withdrawal)
    tryUpdateAttrToPrefixMapForGroup(prefixPathId, oldPostAttr, nullptr);

    XLOGF(
        DBG3,
        "Withdrawing {} from group {}",
        folly::IPAddress::networkToString(prefix),
        groupDescriptor_);
  } else {
    XLOGF(
        DBG3,
        "Ignoring withdraw of prefix {} for unannounced route for group {}",
        folly::IPAddress::networkToString(prefix),
        groupDescriptor_);
  }

  // Delete entry if no longer needed (neither preOut nor postAttr)
  if (!adjRibEntry->getPreOut() && !adjRibEntry->getPostAttr()) {
    if (groupKey_.sendAddPath) {
      deleteFromPathTree(
          PathTree_, std::move(radixPathNodeItr), groupOwnerKey, pathId);
    } else {
      deleteFromLiteTree(LiteTree_, std::move(radixLiteNodeItr), groupOwnerKey);
    }
  }
}

/*
 * @brief Mark every currently in-sync peer as owing each negotiated AFI's EoR.
 *
 * Called at the instant EoR becomes owed (walkAndProcessShadowRib), right
 * after the RIB walk, so the per-peer EGRESS_EOR_PENDING flags are the single
 * source of truth from this point on.
 * markEgressEoRSent clears a peer's flag the moment its EoR push resolves, so a
 * peer that detaches at any later point (route-distribution drain or EoR
 * distribution) already carries exactly the AFIs it still owes: no duplicate
 * (an already-committed AFI is never set again) and no miss (an uncommitted AFI
 * stays set). Peers that join after this point are intentionally not marked --
 * they run their own init dump rather than inheriting this group's EoR.
 *
 * @return void
 */
void AdjRibOutGroup::setEgressEorsPendingSyncPeers() noexcept {
  uint64_t markedPeers = 0;
  for (const auto& [bitPos, adjRib] : bitToAdjRibs_) {
    if (!BitmapUtils::isBitSet(adjRibSyncBitmap_, bitPos)) {
      continue;
    }
    if (!adjRib) {
      XLOGF(
          WARN,
          "Group {} has null AdjRib at bit {} while marking EoR pending",
          groupDescriptor_,
          bitPos);
      continue;
    }
    adjRib->setEgressEoRsPending(
        groupKey_.afiIpv4Negotiated, groupKey_.afiIpv6Negotiated);
    if (adjRib->egressEoRsPending()) {
      syncPeersWithPendingEoRs_.insert(adjRib);
    }
    ++markedPeers;
  }
  XLOGF(
      INFO,
      "Group {} marked {} in-sync peers with pending EoR",
      groupDescriptor_,
      markedPeers);
}

/*
 * @brief  Group-level canAnnounce check
 *         Determines if route can be announced based on group's session type
 *
 * Key differences from peer-level canAnnounce():
 * - Skips same-peer check (split-horizon done per-peer during distribution)
 * - Uses groupKey_.sessionType and groupKey_.isRrClient
 * - Checks IBGP/EBGP rules that apply uniformly to all peers in group
 *
 * @param  update - The announcement entry to check
 * @return bool - true if route can be announced by this group
 */
bool AdjRibOutGroup::canAnnounceForGroup(
    const RibOutAnnouncementEntry& update) noexcept {
  /*
   * Skip same-peer check - that's done per-peer during distribution
   * because each peer has different peerAddr
   */

  /*
   * RFC 1997 well-known community egress filter. Run BEFORE the IBGP
   * early-return below because NO_ADVERTISE MUST suppress on ANY session
   * type, including IBGP. NO_EXPORT / NO_EXPORT_SUBCONFED do not affect
   * IBGP per the filter's own session-type checks. Gated by gflag so
   * rollout is bisectable; when the flag is off behavior is unchanged.
   *
   * Shares the wrapper with the per-peer path (AdjRib::canAnnounceEntry)
   * so the decision + logging + counter side-effects stay in lock-step
   * regardless of whether update-grouping is enabled.
   */
  if (FLAGS_enable_well_known_community_filter &&
      applyWellKnownCommunityFilter(
          update.attrs,
          groupKey_.sessionType,
          groupDescriptor_,
          update.prefix)) {
    return false;
  }

  if (groupKey_.sessionType == BgpSessionType::IBGP) {
    if (groupKey_.isRrClient) {
      // RR clients can announce all routes
      XLOGF(
          DBG4,
          "Group {} permits announcement of {}. Reason: Group is RR client",
          groupDescriptor_,
          folly::IPAddress::networkToString(update.prefix));
      return true;
    }

    /*
     * Non-RR IBGP peers: only announce eBGP routes, local routes, or RRC routes
     * Local routes will have addr.isZero()
     */
    if (update.peer.sessionType != BgpSessionType::IBGP ||
        update.peer.addr.isZero() || update.peer.isRrClient) {
      XLOGF(
          DBG4,
          "Group {} permits announcement of {}. "
          "Reason: EBGP learnt route | Local Route | RRC Learnt Route",
          groupDescriptor_,
          folly::IPAddress::networkToString(update.prefix));
      return true;
    }

    // IBGP-to-IBGP not allowed for non-RR peers
    XLOGF(
        DBG4,
        "Group {} blocks announcement of {}. Reason: Learnt from same AS",
        groupDescriptor_,
        folly::IPAddress::networkToString(update.prefix));
    return false;
  }

  // eBGP peers can announce all routes
  return true;
}

/*
 * @brief  Process a single RibOutAnnouncementEntry for the group
 *         Similar to AdjRib::processRibAnnouncedEntry but at group level
 *
 * Key differences from peer-level:
 * - Runs egress policy once for the group (all peers share same policy)
 * - Skips canAnnounce() checks (all peers in group have identical config)
 * - Stores in group-level RIB-OUT tree using getGroupOwnerKey()
 * - Skips per-peer LBW config (no per-peer config at group level)
 * - Skips nexthop validation (nexthop set per-peer at send time)
 * - No stats tracking (groups don't track stats like peers)
 *
 * @param  entry - The announcement entry to process
 *
 * @return void
 */
void AdjRibOutGroup::processRibAnnouncedEntryForGroup(
    const RibOutAnnouncementEntry& entry) noexcept {
  XLOGF(
      DBG5,
      "Group {} processing entry for {}",
      groupDescriptor_,
      folly::IPAddress::networkToString(entry.prefix));

  // ignore unsupported afi
  if (!AdjRibCommonUtils::isAfiNegotiated(
          entry.prefix,
          groupKey_.afiIpv4Negotiated,
          groupKey_.afiIpv6Negotiated)) {
    XLOGF(
        DBG4,
        "Ignore RibAnnouncement of prefix {}: AFI is not supported for group {}.",
        folly::IPAddress::networkToString(entry.prefix),
        groupDescriptor_);
    return;
  }

  // sanity check to determine if we should announce to the members in the group
  if (!canAnnounceForGroup(entry)) {
    XLOGF(
        DBG3,
        "Implicit withdrawing prefix {} from group {}.",
        folly::IPAddress::networkToString(entry.prefix),
        groupDescriptor_);

    processGroupRibWithdraw(entry.prefix, entry.pathIdToSend);
    return;
  }

  const std::string updatePeerIdStr =
      nettools::bgplib::BgpPeerId(entry.peer.addr, entry.peer.routerId).str();

  // Get or create adjRibEntry in group RIB-OUT tree
  auto adjRibEntry = tryInsertRibOutEntry(
      entry.prefix, entry.attrs->getNexthop(), entry.pathIdToSend);
  CHECK(adjRibEntry);

  // Store preOut attributes
  adjRibEntry->setPreOut(entry.attrs);
  adjRibEntry->setRibVersion(entry.ribVersion);

  // Clone attributes for policy evaluation
  auto prePolicyAttrs = entry.attrs->clone();
  if (entry.isPartialDrain) {
    applyPartialDrainCommunities(prePolicyAttrs);
  }
  if (groupKey_.advertiseLinkBandwidth.has_value() &&
      peeringParams_.has_value()) {
    updateAdvertiseLbwExtCommunityCommon(
        *peeringParams_, entry, prePolicyAttrs);
  }

  /*
   * Get post policy attributes
   * Policy cache lookup happens inside getPostOutPolicyAttributesAndInfo
   */
  const auto& [policyCachedAttrs, postPolicyInfo] =
      getPostOutPolicyAttributesAndInfo(
          entry, adjRibEntry, prePolicyAttrs, updatePeerIdStr);

  /* Preserve previous value of the isNexthopSetByPolicy flag for comparison. */
  const bool wasNexthopSetByPolicy = adjRibEntry->isNexthopSetByPolicy();

  /* Store flag on AdjRibEntry for CLI display (AdjRibShow). */
  adjRibEntry->setNexthopSetByPolicy(postPolicyInfo.isNexthopSetByPolicy);

  if (!policyCachedAttrs) {
    // Policy blocked this prefix
    tryInsertWithdrawal(
        entry.prefix,
        adjRibEntry,
        fmt::format(
            "Group {} withdrawing {} learned from [{}]. "
            "Reason: Blocked prefix by policy. (Previously announced to group)",
            groupDescriptor_,
            folly::IPAddress::networkToString(entry.prefix),
            updatePeerIdStr),
        fmt::format(
            "Group {} ignoring Rib announcement {} learned from [{}]. "
            "Reason: Blocked prefix by policy. (Not previously announced to group)",
            groupDescriptor_,
            folly::IPAddress::networkToString(entry.prefix),
            updatePeerIdStr));
    return;
  }

  /*
   * NOTE: Skip nexthop validation at group level
   * Nexthop is set per-peer at send time, not at group level
   */

  /*
   * Prefix is permitted from egress policy.
   * Attributes need to be updated before sending to packing list.
   *
   * NOTE: directly modifying prePolicyAttrs will make cache miss since it is
   * used as the policy cache key. Clone it for modification.
   */
  auto postOutAttrsNew = prePolicyAttrs->clone();
  updateAttributesOutWithoutNexthop(
      entry, policyCachedAttrs, postOutAttrsNew, postPolicyInfo);

  /* Attributes will not change any more. Publish. */
  postOutAttrsNew->publish();

  /*
   * Check if we announced the prefix before
   * post out is set if the prefix was previously announced
   */
  if (adjRibEntry->getPostAttr()) {
    /*
     * Announce the prefix to group again only if postOut has changed.
     * We are doing deep compare to avoid notifying group in cases where
     * due to policy changes of attributes, we end up with same contents
     * but different BgpPath shared_ptr.
     *
     * isNexthopSetByPolicy is part of the comparison because it is a packing
     * list key (BgpPathWithAfi) and it decides per-peer nexthop-self at send
     * time, so a toggle changes the wire output even when the attributes are
     * byte-identical. This mirrors AdjRib::processRibAnnouncedEntry.
     */
    if (*adjRibEntry->getPostAttr() == *postOutAttrsNew &&
        wasNexthopSetByPolicy == adjRibEntry->isNexthopSetByPolicy()) {
      XLOGF(
          DBG3,
          "Group {} skipping unchanged announcement {} from [{}]. "
          "Reason: Previously announced to group",
          groupDescriptor_,
          folly::IPAddress::networkToString(entry.prefix),
          updatePeerIdStr);
      return;
    }
  } else {
    // First-time announcement — increment group prefix count
    stats_.incrementPostOutPrefixCount(
        entry.prefix.first.isV4(), getNumInSyncPeers());
  }

  XLOGF(
      DBG3,
      "Group {} announcing {} from [{}].",
      groupDescriptor_,
      folly::IPAddress::networkToString(entry.prefix),
      updatePeerIdStr);

  auto prefixPathId = std::make_pair(entry.prefix, adjRibEntry->getPathId());

  auto oldPostAttr = adjRibEntry->getPostAttr();

  /*
   * Set the post out
   * Majority of the cases we update attributes with nexthop/AS-Path changes
   * or policy changes, but in few cases where there is no change between
   * preOut and postOut, this deep compare will save memory.
   */
  if (*postOutAttrsNew == *adjRibEntry->getPreOut()) {
    auto preOutPostAttr = adjRibEntry->getPreOut();
    adjRibEntry->setPostAttr(preOutPostAttr);
  } else {
    adjRibEntry->setPostAttr(postOutAttrsNew);
  }
  tryUpdateAttrToPrefixMapForGroup(
      prefixPathId,
      oldPostAttr,
      adjRibEntry->getPostAttr(),
      postPolicyInfo.isNexthopSetByPolicy);
}

AdjRibOutGroupConsumer::~AdjRibOutGroupConsumer() {
  adjRibOutGroup_.reset();
}

/*
 * @brief  Try to insert or get RIB-OUT entry for the group
 *         Similar to AdjRib::tryInsertRibOutEntry but operates at group level
 *
 * Key differences from peer-level:
 * - Uses group owner key instead of peer owner key
 * - Update-group path always uses pathIdToSend (never pathIdGenerator_)
 * - Conditionally uses PathTree or LiteTree based on groupKey_.sendAddPath
 *
 * @param  prefix - The prefix to insert/get
 * @param  nexthop - Nexthop (unused at group level, kept for signature compat)
 * @param  pathIdToSend - Path ID from RIB
 *
 * @return AdjRibEntry* - Pointer to entry (never null)
 */
AdjRibEntry* FOLLY_NULLABLE AdjRibOutGroup::tryInsertRibOutEntry(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& /* nexthop */,
    const uint32_t pathIdToSend) noexcept {
  // Update-group path always uses pathIdToSend (never pathIdGenerator_)
  auto pathId = pathIdToSend;

  auto groupOwnerKey = getGroupOwnerKey();
  AdjRibEntry* adjRibEntry = nullptr;

  // Use PathTree or LiteTree based on UpdateGroupKey.sendAddPath setting
  if (groupKey_.sendAddPath) {
    auto radixPathNodeItr = getRadixNodeItrFromPathTree(PathTree_, prefix);
    adjRibEntry =
        getAdjRibEntryFromPathNodeItr(radixPathNodeItr, groupOwnerKey, pathId);
    if (!adjRibEntry) {
      XLOGF(
          DBG4,
          "Learning new prefix from Rib for group {}: {}",
          groupDescriptor_,
          folly::IPAddress::networkToString(prefix));
      stats_.incrementPreOutPrefixCount(prefix.first.isV4());
      return addToPathTree(PathTree_, prefix, groupOwnerKey, pathId);
    }
    // Lazy clone before the caller mutates this existing entry
    if (!detachedPeers_.empty()) {
      lazyClonePathForDetachedPeers(
          prefix, pathId, radixPathNodeItr, adjRibEntry);
    }
  } else {
    auto radixLiteNodeItr = getRadixNodeItrFromLiteTree(LiteTree_, prefix);
    adjRibEntry =
        getAdjRibEntryFromLiteNodeItr(radixLiteNodeItr, groupOwnerKey);
    if (!adjRibEntry) {
      XLOGF(
          DBG4,
          "Learning new prefix from Rib for group {}: {}",
          groupDescriptor_,
          folly::IPAddress::networkToString(prefix));
      stats_.incrementPreOutPrefixCount(prefix.first.isV4());
      return addToLiteTree(LiteTree_, prefix, groupOwnerKey, pathId);
    }
    // Lazy clone before the caller mutates this existing entry
    if (!detachedPeers_.empty()) {
      lazyCloneLiteForDetachedPeers(prefix, radixLiteNodeItr, adjRibEntry);
    }
  }

  XLOGF(
      DBG4,
      "Updating preOut attributes of prefix {} for group {}",
      folly::IPAddress::networkToString(prefix),
      groupDescriptor_);

  return adjRibEntry;
}

/*
 * @brief  Try to insert withdrawal for a prefix in the group's packing list
 *         Similar to AdjRib::tryInsertWithdrawal but operates at group level
 *
 * Key differences from peer-level:
 * - Decrements the group's postOutPrefixCount by getNumInSyncPeers() -- the
 *   prefix was advertised to every in-sync peer -- not by 1
 * - Uses tryUpdateAttrToPrefixMapForGroup instead of tryUpdateAttrToPrefixMap
 * - Logs with group name instead of peer name
 *
 * @param  prefix - The prefix to withdraw
 * @param  adjRibEntry - The AdjRibEntry for this prefix
 * @param  insertedMsg - Message to log if withdrawal is inserted
 * @param  notInsertedMsg - Message to log if withdrawal is not inserted
 *
 * @return void
 */
void AdjRibOutGroup::tryInsertWithdrawal(
    const folly::CIDRNetwork& prefix,
    AdjRibEntry* adjRibEntry,
    const std::string& insertedMsg,
    const std::string& notInsertedMsg) noexcept {
  auto oldPostAttr = adjRibEntry->getPostAttr();
  if (oldPostAttr) {
    /*
     * Withdraw the prefix only if we previously announced it to this group
     * This can happen if attributes change for a prefix and policy based on
     * new attributes blocks the prefix. Withdraw it from group.
     */
    XLOGF_IF(
        DBG1,
        stats_.getPostOutPrefixCount() == 0,
        "Invalid sent prefix count of {} for group {}",
        folly::IPAddress::networkToString(prefix),
        groupDescriptor_);
    stats_.decrementPostOutPrefixCount(
        prefix.first.isV4(), getNumInSyncPeers());

    adjRibEntry->setPostAttr(nullptr);

    auto prefixPathId = std::make_pair(prefix, adjRibEntry->getPathId());

    /*
     * Update attrToPrefixMap to move prefixPathId from the previous postAttrs
     * to the new postAttr (nullptr).
     */
    tryUpdateAttrToPrefixMapForGroup(
        prefixPathId, oldPostAttr, adjRibEntry->getPostAttr());

    XLOG(DBG3, insertedMsg);
  } else {
    // we didn't inform group before, so no need of informing to group now.
    XLOG(DBG3, notInsertedMsg);
  }
}

/*
 * @brief  Get post-policy attributes, policy term name, and policy info
 *         Similar to AdjRib::getPostPolicyAttributesPolicyTermAndInfo
 *
 * Key differences from peer-level:
 * - Uses group's policyManager_ and policyCache_
 * - Logs with group name instead of peer name
 * - Otherwise identical logic
 *
 * TODO: This function is exactly the same as in AdjRib.cpp. Consider
 * consolidating and moving to AdjRibCommon.
 *
 * @param  policyName - Name of the egress policy to evaluate
 * @param  prefix - The prefix being evaluated
 * @param  prePolicyAttrs - Attributes before policy evaluation
 * @param  policyActionData - Action data for policy (LBW, etc.)
 *
 * @return tuple of (postPolicyAttrs, policyTermName, PostPolicyInfo)
 */
std::tuple<std::shared_ptr<const BgpPath>, std::string, PostPolicyInfo>
AdjRibOutGroup::getPostPolicyAttributesPolicyTermAndInfo(
    const std::string& policyName,
    const folly::CIDRNetwork& prefix,
    const std::shared_ptr<const BgpPath>& prePolicyAttrs,
    const std::shared_ptr<BgpPolicyActionData>& policyActionData,
    bool isPartialDrain) noexcept {
  std::optional<AdjRibPolicyCache::PolicyCacheValue> cacheResult{std::nullopt};
  std::shared_ptr<const BgpPath> postPolicyAttrs{nullptr};
  std::string policyTermName;
  PostPolicyInfo postPolicyInfo;

  if (policyCache_) {
    cacheResult = policyCache_->lookupPolicyCache(
        policyName,
        policyManager_->getPolicyAttributesMask(policyName),
        prefix,
        prePolicyAttrs,
        policyActionData,
        isPartialDrain);
  }

  if (cacheResult == std::nullopt) {
    // Policy cache miss - evaluate policy
    XLOGF(
        DBG5,
        "Policy Cache Miss for {} {} for group {}",
        folly::IPAddress::networkToString(prefix),
        policyName,
        groupDescriptor_);

    // Clone attributes for policy evaluation
    auto prePolicyAttrsClone = prePolicyAttrs->clone();

    // Apply policy
    auto policyOut = policyManager_->applyPolicy(
        policyName,
        PolicyInMessage({prefix}, prePolicyAttrsClone, policyActionData));

    const auto attrsAndPolicy =
        getPostPolicyOutAttrsAndPolicyFromMessage(prefix, policyOut);

    postPolicyAttrs = attrsAndPolicy->attrs;
    policyTermName = attrsAndPolicy->policyName;
    if (policyActionData) {
      postPolicyInfo.isMedSetByPolicy = policyActionData->isMedSetByPolicy;
      postPolicyInfo.isNexthopSetByPolicy =
          policyActionData->isNexthopSetByPolicy;
    }

    if (policyCache_) {
      policyCache_->addToPolicyCache(
          policyName,
          policyManager_->getPolicyAttributesMask(policyName),
          prefix,
          prePolicyAttrs,
          policyActionData,
          attrsAndPolicy,
          isPartialDrain);
    }

  } else {
    // Policy cache hit
    XLOGF(
        DBG5,
        "Policy Cache Hit for {} {} for group {}",
        folly::IPAddress::networkToString(prefix),
        policyName,
        groupDescriptor_);

    if (cacheResult->attrsAndPolicy) {
      postPolicyAttrs = cacheResult->attrsAndPolicy->attrs;
      policyTermName = cacheResult->attrsAndPolicy->policyName;
    }
    postPolicyInfo.isMedSetByPolicy = cacheResult->isMedSetByPolicy;
    postPolicyInfo.isNexthopSetByPolicy = cacheResult->isNexthopSetByPolicy;
  }

  return {
      std::move(postPolicyAttrs),
      std::move(policyTermName),
      std::move(postPolicyInfo)};
}

/*
 * @brief  Get post-policy attributes and metadata for the group
 *         Similar to AdjRib::getPostOutPolicyAttributesAndInfo
 *
 * Key differences from peer-level:
 * - Uses group's policyManager, policyCache, and groupKey_.egressPolicyName
 * - Logs with group name instead of peer name
 * - No CRF (egress route filter) check at group level for now
 * - Otherwise identical logic
 *
 * @param  update - The RibOutAnnouncementEntry being processed
 * @param  adjRibEntry - The AdjRibEntry for this prefix
 * @param  prePolicyAttrs - Attributes before policy evaluation
 * @param  updatePeerIdStr - Source peer ID string (for logging)
 *
 * @return pair of (postPolicyAttrs, PostPolicyInfo)
 */
std::pair<const std::shared_ptr<const BgpPath>, const PostPolicyInfo>
AdjRibOutGroup::getPostOutPolicyAttributesAndInfo(
    const RibOutAnnouncementEntry& update,
    AdjRibEntry* adjRibEntry,
    const std::shared_ptr<const BgpPath>& prePolicyAttrs,
    const std::string& /* updatePeerIdStr */) noexcept {
  std::shared_ptr<const BgpPath> policyResultAttrs{nullptr};
  PostPolicyInfo postPolicyResultInfo;

  // adjRibEntry must not be nullptr
  CHECK(adjRibEntry != nullptr);

  if (egressPolicyConfigured()) {
    /*
     * Snapshot the policy action data from the announcement, exactly as
     * AdjRib::getPostOutPolicyAttributesAndInfo does for a detached peer.
     * A default-constructed BgpPolicyActionData carries no LBW inputs, so any
     * policy term reading them (LBW / UCMP actions) evaluated differently for
     * the group than it did for a detached member.
     *
     * peeringParams_ is the first registered member's; the only field this
     * reads that is not part of UpdateGroupKey is localAs (tracked separately
     * as audit finding M3).
     */
    auto policyActionData = peeringParams_
        ? createPolicyActionDataCommon(
              *peeringParams_,
              update.attrs,
              update.switchId,
              update.multiPathSize,
              update.aggregateReceivedUcmpWeight,
              update.aggregateLocalUcmpWeight,
              update.ribPolicyUcmpWeight)
        : std::make_shared<BgpPolicyActionData>();

    const auto& [attrs, postTermName, postPolicyInfo] =
        getPostPolicyAttributesPolicyTermAndInfo(
            *groupKey_.egressPolicyName,
            update.prefix,
            prePolicyAttrs,
            policyActionData,
            update.isPartialDrain);

    policyResultAttrs = attrs;
    postPolicyResultInfo = postPolicyInfo;
    adjRibEntry->setPostOutPolicy(postTermName);
  } else {
    policyResultAttrs = prePolicyAttrs;
  }

  /*
   * TODO: Add egress route filter (CRF) check for group level if needed
   * For now, skip CRF check at group level
   */

  return {policyResultAttrs, postPolicyResultInfo};
}

/*
 * @brief  Update all attributes except nexthop before putting in packing list
 *         Uses the common implementation shared with per-peer AdjRib
 *
 * Key differences from peer-level:
 * - Checks that peeringParams_ is cached before using it
 * - Otherwise delegates to common implementation
 *
 * @param  update - The RibOutAnnouncementEntry being processed
 * @param  policyResultAttrs - Attributes after policy evaluation
 * @param  attrsToUpdate - Attributes to modify (must not be published)
 * @param  postPolicyInfo - Metadata from policy evaluation
 *
 * @return void
 */
void AdjRibOutGroup::updateAttributesOutWithoutNexthop(
    const RibOutAnnouncementEntry& update,
    const std::shared_ptr<const BgpPath>& policyResultAttrs,
    std::shared_ptr<BgpPath> attrsToUpdate,
    const PostPolicyInfo& postPolicyInfo) noexcept {
  if (!peeringParams_) {
    XLOGF(WARN, "No peering params cached for group {}", groupDescriptor_);
    return;
  }

  PeerConfig config{
      *peeringParams_,
      groupKey_.sessionType,
      groupKey_.egressPolicyName,
      policyManager_.get()};
  updateAttributesOutWithoutNexthopCommon(
      config, update, policyResultAttrs, attrsToUpdate, postPolicyInfo);
}

/*
 * @brief  Pack prefixes into BgpUpdate2 thrift collection.
 *         Wrapper for packPrefixesCommon that uses group-specific
 * parameters.
 *
 * @param  prefixPathIds - Set of (prefix, pathId) pairs (modified in place)
 * @param  bgpUpdatePrefixes - Container to pack prefixes into
 * @param  sendAddPath - Whether to include path IDs in packed prefixes
 * @return Number of prefixes packed
 */
uint32_t AdjRibOutGroup::packGroupPrefixes(
    PrefixSet& prefixPathIds,
    std::vector<nettools::bgplib::RiggedIPPrefix>& bgpUpdatePrefixes,
    bool sendAddPath) noexcept {
  return packPrefixesCommon(
      prefixPathIds, bgpUpdatePrefixes, sendAddPath, groupDescriptor_);
}

/*
 * @brief  Build a single BGP UPDATE message from attrToPrefixMap entry
 *         Similar to AdjRib::buildUpdateWithSizeEstimation.
 *
 * Handles BOTH announcements and withdrawals in one method.
 * Drains the entire pfxSet.
 *
 * Key differences from peer-level:
 * - Uses pre-calculated nexthop from processRibAnnouncedEntryForGroup
 * - No stats tracking (groups don't have stats_ member)
 * - Uses groupKey_.sendAddPath instead of member variable
 *
 * @param  attrsWithAfi - Pair of (attributes, AFI) from map key
 * @param  prefixPathIds - Set of prefixes to pack (cleared in place after
 * draining)
 * @return BgpUpdate2 message, or nullptr if nothing to pack
 */
std::shared_ptr<nettools::bgplib::BgpUpdate2> AdjRibOutGroup::buildGroupUpdate(
    const BgpPathWithAfi& attrsWithAfi,
    PrefixSet& prefixPathIds) noexcept {
  if (prefixPathIds.empty()) {
    return nullptr;
  }

  auto& postOutAttrs = attrsWithAfi.attrs;
  auto afi = attrsWithAfi.afi;
  std::shared_ptr<nettools::bgplib::BgpUpdate2> update;

  if (postOutAttrs) {
    /* Case 1: Announcement */
    update = postOutAttrs->getBgpUpdate2();
    /*
     * The group path passes the raw path nexthop (not a per-peer transformed
     * one): a group shares one PDU whose per-peer nexthop-self / policy nexthop
     * is applied at serialization, so no per-peer transform exists at build
     * time. The encoding decision depends only on the nexthop family, which is
     * v4 for a v4 route not using RFC 5549 (already excluded via
     * extNhEncodingCapable).
     */
    const auto& newNexthop = postOutAttrs->getNexthop();
    switch (v4EncodingDecision(
        groupKey_,
        afi == nettools::bgplib::BgpUpdateAfi::AFI_IPv4,
        newNexthop,
        groupDescriptor_)) {
      case V4EncodingDecision::ClassicNlri:
        /*
         * Legacy RFC 4271: classic v4 NLRI + NEXT_HOP (attr 3). Set only the
         * binary v4Nexthop() (what the serializer emits and rewrites per-peer);
         * attrs()->nexthop() is intentionally left unset to stay symmetric with
         * the MP branch below, which likewise relies on the binary nexthop.
         * Unlike the single-peer path (AdjRib::buildUpdateWithSizeEstimation),
         * which also sets the string attrs()->nexthop(), the group path omits
         * it: group PDUs are serialized straight from the binary nexthop
         * (per-peer rewritten), so the string form is never read.
         */
        update->v4Nexthop() = network::toBinaryAddress(newNexthop);
        packGroupPrefixes(
            prefixPathIds, *update->v4Announced2(), groupKey_.sendAddPath);
        break;
      case V4EncodingDecision::Drop:
        /*
         * Capability-less peer with a v6 nexthop: drop the route (already
         * logged) instead of building an UPDATE the peer would reject. Clear
         * prefixPathIds so buildAndSendGroupBgpMessages erases the map entry
         * instead of re-picking it forever (it only advances once pfxSet
         * drains).
         */
        prefixPathIds.clear();
        return nullptr;
      case V4EncodingDecision::MpReach:
        update->mpAnnounced()->afi() = afi;
        update->mpAnnounced()->safi() =
            nettools::bgplib::BgpUpdateSafi::SAFI_UNICAST;
        update->mpAnnounced()->nexthop() = network::toBinaryAddress(newNexthop);
        packGroupPrefixes(
            prefixPathIds,
            *update->mpAnnounced()->prefixes(),
            groupKey_.sendAddPath);
        break;
    }

  } else {
    /* Case 2: Withdrawal */
    update = std::make_shared<nettools::bgplib::BgpUpdate2>();
    if (afi == nettools::bgplib::BgpUpdateAfi::AFI_IPv4) {
      packGroupPrefixes(
          prefixPathIds, *update->v4Withdrawn2(), groupKey_.sendAddPath);
    } else if (afi == nettools::bgplib::BgpUpdateAfi::AFI_IPv6) {
      packGroupPrefixes(
          prefixPathIds,
          *update->mpWithdrawn()->prefixes(),
          groupKey_.sendAddPath);
      update->mpWithdrawn()->afi() = nettools::bgplib::BgpUpdateAfi::AFI_IPv6;
      update->mpWithdrawn()->safi() =
          nettools::bgplib::BgpUpdateSafi::SAFI_UNICAST;
    }
  }

  return update;
}

/*
 * @brief  Build group BGP messages from packing list
 *         Similar to AdjRib::sendBgpUpdates but for update groups
 *
 * Orchestrates the complete message building and distribution workflow:
 * 1. Iterates through attrToPrefixMap_ entries
 * 2. Builds one BGP UPDATE at a time.
 * 3. Distributes message to all in-sync peers
 * 4. Removes processed prefixes from packing list
 * 5. Repeats until packing list is empty
 *
 * Unlike the peer-level path, each message is built once and distributed to
 * every eligible in-sync peer.
 *
 * @return void
 */
folly::coro::Task<void>
AdjRibOutGroup::buildAndSendGroupBgpMessages() noexcept {
  ScopedProfile profile("AdjRibOutGroup::buildAndSendGroupBgpMessages");
  /*
   * Guard against re-entrant calls to maintain BGP UPDATE ordering
   * If already building/sending, skip this invocation to avoid:
   * 1. Message ordering violations (UPDATE #2 sent before UPDATE #1 completes)
   * 2. Concurrent modification of attrToPrefixMap_ during iteration
   * 3. Multiple coroutines waiting on same blocked peers
   */
  if (packingInProgress_) {
    XLOGF(
        DBG3,
        "Group {} packing already in progress, skipping duplicate invocation",
        groupDescriptor_);
    co_return;
  }

  packingInProgress_ = true;

  /* Check for asyncScope cancellation at start */
  co_await folly::coro::co_safe_point;

  /*
   * Cancel changeListConsumeTimer_ to prevent concurrent modifications
   * of attrToPrefixMap_ while we're iterating it.
   * Timer will be rescheduled when we exit.
   */
  cancelChangeListConsumeTimer();

  /*
   * RAII guard to ensure cleanup even if exception occurs:
   * 1. Clear packingInProgress_ flag
   * 2. Reschedule changeListConsumeTimer_
   * 3. Transition group to IDLE if packing list is empty, WAITING if not.
   * 4. Check and accept all ready to join peers.
   */
  auto guard = folly::makeGuard([this]() {
    packingInProgress_ = false;

    /*
     * A non-empty packing list means work was appended while this
     * builder was suspended -- an in-place policy re-evaluation during the
     * EoR wait, for instance. This is because policy re-evaluation must
     * happen inline and cannot check for the packingInProgress_ flag. That
     * work is still pending, so the group stays WAITING and the consume timer
     * picks it up.
     */
    if (attrToPrefixMap_.empty()) {
      tryRejoinDetachedPeersOnAllChangesProcessed();
    } else {
      state_ = UpdateGroupState::WAITING;
    }

    /* Keep advancing while a SYNC peer or a sharing DSP needs the group. */
    if (hasEntrySharingPeers()) {
      scheduleChangeListConsumeTimer();
    }
  });

  if (checkForBlockedPeers_) {
    checkForBlockedPeers_ = false;
    if (hasBlockedPeers()) {
      /*
       * Rare edge case: a split (splitToNewGroup) can hand this group in-sync
       * peers that were still BLOCKED, with a deferred push in flight from
       * their previous group, tracked by their carried-over bit in
       * adjRibBlockedBitmap_. Only then does splitToNewGroup set
       * checkForBlockedPeers_, so the common path (no blocked peers ever moved
       * in) skips the hasBlockedPeers() scan entirely. That earlier push has
       * not yet reached the peer's queue, and the packing list cloned into this
       * group holds only the items that follow it. Draining now could enqueue a
       * later item ahead of that in-flight one and reorder BGP UPDATEs, so wait
       * for the carried-over pushes to drain first, then clear the flag.
       */
      XLOGF(
          DBG2,
          "Group {}: waiting for carried-over pending pushes to drain before "
          "packing (preserves UPDATE ordering after a split)",
          groupDescriptor_);
      co_await waitForAllPendingPushes();
    }
  }

  if (attrToPrefixMap_.empty() && syncPeersWithPendingEoRs_.empty()) {
    co_return;
  }

  XLOGF(
      DBG1,
      "Group {} building BGP messages from packing list ({} attr entries)",
      groupDescriptor_,
      attrToPrefixMap_.size());

  uint32_t msgCount = 0;
  uint32_t withdrawPrefixCnt = 0;
  uint32_t announcePrefixCnt = 0;

  // Iterate attrToPrefixMap and build/distribute one message at a time.
  while (!attrToPrefixMap_.empty()) {
    /* Check for cancellation at start of each iteration */
    co_await folly::coro::co_safe_point;

    /*
     * If we were backpressured and resume here, it is currently possible for
     * the map to be cleared if the peer session flaps.
     * Here we need to check one more time to make sure that the map is not
     * empty before taking the begin iterator.
     */
    if (attrToPrefixMap_.empty()) {
      break;
    }

    auto it = attrToPrefixMap_.begin();
    auto& [attrWithAfi, pfxSet] = *it;
    const auto attr = attrWithAfi.attrs;
    const auto afi = attrWithAfi.afi;
    const auto isNhSetByPolicy = attrWithAfi.isNexthopSetByPolicy;

    // Build one message (announcement or withdrawal) with size limit
    auto update = buildGroupUpdate(attrWithAfi, pfxSet);

    /*
     * Remove entry from map if all prefixes processed.
     * This step is done above the co_await in case the iterator becomes
     * invalidated after the coroutine yields.
     */
    if (pfxSet.empty()) {
      attrToPrefixMap_.erase(it);
    }

    if (update) {
      msgCount++;
      /*
       * Building the shared PDU is the group counter commit point. Publish it
       * before distribution can suspend so counter clear and cancellation
       * cannot separate the PDU from its accounting.
       */
      stats_.incrementSentUpdateMsgs(1);
      // Update counters
      if (attr) {
        // Announcement
        /*
         * buildGroupUpdate populates exactly one of mpAnnounced / v4Announced2
         * per UPDATE, so summing their sizes counts each prefix once. Assert
         * the mutual exclusion so a future mixed-encoding path cannot silently
         * double-count.
         */
        DCHECK(
            update->mpAnnounced()->prefixes()->empty() ||
            update->v4Announced2()->empty());
        announcePrefixCnt += update->mpAnnounced()->prefixes()->size() +
            update->v4Announced2()->size();

        /* Group-level count of announcement UPDATE PDUs generated once for this
         * group, per AFI. Per-peer total UPDATE counts are recorded when each
         * queue push resolves. */
        if (afi == nettools::bgplib::BgpUpdateAfi::AFI_IPv4) {
          stats_.incrementSentAnnouncementsIpv4();
          /* Legacy classic-NLRI v4 announcements land in v4Announced2. */
          if (!update->v4Announced2()->empty()) {
            stats_.incrementSentLegacyV4Announcements();
          }
        } else if (afi == nettools::bgplib::BgpUpdateAfi::AFI_IPv6) {
          stats_.incrementSentAnnouncementsIpv6();
        }
      } else {
        // Withdrawal
        if (afi == nettools::bgplib::BgpUpdateAfi::AFI_IPv4) {
          withdrawPrefixCnt += update->v4Withdrawn2()->size();
        } else if (afi == nettools::bgplib::BgpUpdateAfi::AFI_IPv6) {
          withdrawPrefixCnt += update->mpWithdrawn()->prefixes()->size();
        }

        /* Group-level count of withdrawal UPDATE PDUs generated for this group
         * (single counter, not per-AFI -- mirrors the per-peer AdjRibOut site).
         */
        stats_.incrementSentWithdrawals();
      }

      /*
       * Distribute to all in-sync peers
       * This suspends if any peer queue is full (backpressure)
       */
      co_await distributeMessageToInSyncPeers(
          update, attr, afi, isNhSetByPolicy);
    }
  }

  /* Distribute pending EoRs after the packing list drains. EoR is a distinct
   * BGP PDU from UPDATE, so keep its count separate from msgCount (which counts
   * only UPDATE announcements/withdrawals). */
  uint32_t eorCount = 0;
  if (!syncPeersWithPendingEoRs_.empty()) {
    eorCount = co_await distributePendingEoRs();
  }

  XLOGF(
      INFO,
      "Group {} built and distributed {} BGP messages ({} withdrawals, {} announcements, {} EoRs)",
      groupDescriptor_,
      msgCount + eorCount,
      withdrawPrefixCnt,
      announcePrefixCnt,
      eorCount);

  // packingInProgress_ flag will be cleared automatically by RAII guard
}

/*
 * @brief Distribute per-AFI EoR markers to eligible in-sync peers.
 *
 * Queues each eligible in-sync peer's EoR PDU through tryPushToPeer;
 * each EoR push carries an onResolved continuation (markEgressEoRSent) that
 * handles the per-peer EoR-sent bookkeeping: it clears the peer's
 * EGRESS_EOR_PENDING_<afi> flag when the PDU lands, and once no AFI flags
 * remain (!egressEoRsPending()) fires onEgressEoRSent on the LAST EoR to land.
 *
 * In-sync peers are marked as owing EoR when the EoR becomes owed
 * (walkAndProcessShadowRib), not here, and markEgressEoRSent clears each
 * peer's flag as its push resolves.
 *
 * Returns the number of EoR PDUs accepted for delivery (one per AFI
 * distributed).
 */
folly::coro::Task<uint32_t> AdjRibOutGroup::distributePendingEoRs() noexcept {
  uint32_t eorMsgCount = 0;
  /*
   * In-sync peers were already marked as owing EoR when the EoR became owed
   * (walkAndProcessShadowRib), and markEgressEoRSent clears each peer's flag
   * as its push resolves, so no marking is needed here -- the per-peer
   * EGRESS_EOR_PENDING flags are the single source of truth.
   */
  XLOGF(INFO, "Group {} processing pending EoRs", groupDescriptor_);

  auto distributeEoRs = [&](nettools::bgplib::BgpUpdateAfi afi) {
    auto eorMessage = buildEndOfRib(afi);
    bool distributed = false;
    const std::vector<std::shared_ptr<AdjRib>> pendingPeers(
        syncPeersWithPendingEoRs_.begin(), syncPeersWithPendingEoRs_.end());
    for (const auto& adjRib : pendingPeers) {
      const auto bitPos = adjRib->getGroupBitPosition();
      const bool eorPending = afi == nettools::bgplib::BgpUpdateAfi::AFI_IPv4
          ? adjRib->egressEoRPendingV4()
          : adjRib->egressEoRPendingV6();
      if (!eorPending) {
        continue;
      }
      /* On land, finalize this peer's EoR for this AFI. */
      const auto result =
          tryPushToPeer(eorMessage, adjRib, bitPos, [adjRib, afi]() noexcept {
            adjRib->markEgressEoRSent(afi);
            if (!adjRib->egressEoRsPending()) {
              if (auto group = adjRib->getUpdateGroup()) {
                group->syncPeersWithPendingEoRs_.erase(adjRib);
              }
            }
          });
      distributed |= result != PushResult::PUSH_FAILED;
    }
    return distributed;
  };

  /*
   * Acceptance is the group EoR commit point. Publish each AFI before its
   * pending pushes can suspend so a counter clear cannot be crossed.
   */
  if (distributeEoRs(nettools::bgplib::BgpUpdateAfi::AFI_IPv4)) {
    eorMsgCount++;
    stats_.incrementSentEndOfRibMsgs(1);
  }
  co_await waitForAllPendingPushes();
  if (distributeEoRs(nettools::bgplib::BgpUpdateAfi::AFI_IPv6)) {
    eorMsgCount++;
    stats_.incrementSentEndOfRibMsgs(1);
  }
  co_await waitForAllPendingPushes();

  XLOGF(INFO, "Group {} completed sending pending EoRs", groupDescriptor_);

  co_return eorMsgCount;
}

void AdjRibOutGroup::setSyncBit(
    uint64_t bit,
    const std::shared_ptr<AdjRib>& adjRib) noexcept {
  if (!BitmapUtils::isBitSet(adjRibSyncBitmap_, bit)) {
    ++numInSyncPeers_;
  }
  BitmapUtils::setBit(adjRibSyncBitmap_, bit);

  if (adjRib->egressEoRsPending()) {
    syncPeersWithPendingEoRs_.insert(adjRib);
  }
}

void AdjRibOutGroup::clearSyncBit(
    uint64_t bit,
    const std::shared_ptr<AdjRib>& adjRib) noexcept {
  syncPeersWithPendingEoRs_.erase(adjRib);

  if (BitmapUtils::isBitSet(adjRibSyncBitmap_, bit)) {
    --numInSyncPeers_;
  }
  BitmapUtils::clearBit(adjRibSyncBitmap_, bit);
}

/*
 * @brief Distribute BGP UPDATE message to all in-sync peers
 * @param  message - BGP UPDATE message to distribute
 * @param  postOutAttrs - Post-policy BGP path attributes for nexthop
 * computation
 * @param  afi - Address Family Identifier (IPv4 or IPv6)
 * @return void
 */
folly::coro::Task<void> AdjRibOutGroup::distributeMessageToInSyncPeers(
    const std::shared_ptr<nettools::bgplib::BgpUpdate2>& message,
    const std::shared_ptr<const BgpPath>& postOutAttrs,
    nettools::bgplib::BgpUpdateAfi afi,
    bool isNexthopSetByPolicy) noexcept {
  if (!message) {
    XLOGF(
        WARN, "Group {} received null message to distribute", groupDescriptor_);
    co_return;
  }

  uint32_t pushOkCount = 0;
  uint32_t pushPendingCount = 0;
  uint32_t pushDroppedCount = 0;

  // Serialize once at group level if enabled
  nettools::bgplib::UpdateDescriptor groupDescriptor;
  if (updateGroupConfig_.enableSerializeGroupPdu) {
    bool as4byte = groupKey_.as4ByteCapable;
    bool extNhEncoding = groupKey_.extNhEncodingCapable;

    groupDescriptor = AdjRibGroupSerializer::serializeUpdateAndCreateDescriptor(
        *message, as4byte, extNhEncoding);

    if (!groupDescriptor.serializedGroupPDU) {
      XLOGF(
          ERR,
          "Group {} failed to serialize BGP UPDATE, skipping distribution",
          groupDescriptor_);
      co_return;
    }

    XLOGF(
        DBG4,
        "Group {} using zero-copy distribution with UpdateDescriptor",
        groupDescriptor_);
  }

  // Iterate through all in-sync peers
  for (const auto& [bitPos, adjRib] : bitToAdjRibs_) {
    if (!BitmapUtils::isBitSet(adjRibSyncBitmap_, bitPos)) {
      continue;
    }

    if (!adjRib) {
      XLOGF(
          WARN,
          "Group {} has null AdjRib at bit position {}",
          groupDescriptor_,
          bitPos);
      continue;
    }

    // Determine what to push to this peer
    nettools::bgplib::FiberBgpPeer::InputMessageT peerMessage;

    if (updateGroupConfig_.enableSerializeGroupPdu) {
      // Zero-copy path: use serialized UpdateDescriptor
      if (postOutAttrs) {
        /* Announcement: compute per-peer nexthop */
        auto peerNexthop = adjRib->getNewNexthopFromAttributesOut(
            afi == nettools::bgplib::BgpUpdateAfi::AFI_IPv4 /* isV4 */,
            postOutAttrs,
            isNexthopSetByPolicy);

        nettools::bgplib::UpdateDescriptor peerDescriptor = groupDescriptor;
        /* Set the appropriate nexthop based on address family */
        if (peerNexthop.isV4()) {
          peerDescriptor.v4Nexthop = peerNexthop;
        } else {
          peerDescriptor.v6Nexthop = peerNexthop;
        }
        peerMessage = peerDescriptor;
      } else {
        /* Withdrawal: nexthop irrelevant, use group descriptor as-is */
        peerMessage = groupDescriptor;
      }
    } else {
      /* Non-zero-copy path: clone BgpUpdate2 and set per-peer nexthop */
      if (postOutAttrs) {
        /* Announcement: clone message and set per-peer nexthop */
        auto clonedMessage =
            std::make_shared<nettools::bgplib::BgpUpdate2>(*message);

        /* Compute per-peer nexthop */
        auto peerNexthop = adjRib->getNewNexthopFromAttributesOut(
            afi == nettools::bgplib::BgpUpdateAfi::AFI_IPv4 /* isV4 */,
            postOutAttrs,
            isNexthopSetByPolicy);

        /* Set per-peer nexthop in the cloned message */
        clonedMessage->mpAnnounced()->nexthop() =
            network::toBinaryAddress(peerNexthop);
        clonedMessage->attrs()->nexthop() = peerNexthop.str();

        peerMessage = clonedMessage;
      } else {
        /* Withdrawal: nexthop irrelevant, use message as-is */
        peerMessage = message;
      }
    }

    /* deferredPushToPeer owns a shared_ptr to the peer until this callback
     * runs, so the hot distribution path can avoid another shared_ptr copy. */
    auto* peer = adjRib.get();
    auto result = tryPushToPeer(
        peerMessage,
        adjRib,
        bitPos,
        [peer, afi, isAnnouncement = postOutAttrs != nullptr]() noexcept {
          peer->incrementSentUpdateMsgs();
          if (!isAnnouncement) {
            peer->incrementSentWithdrawals();
          } else if (afi == nettools::bgplib::BgpUpdateAfi::AFI_IPv4) {
            peer->incrementSentAnnouncementsIpv4();
          } else if (afi == nettools::bgplib::BgpUpdateAfi::AFI_IPv6) {
            peer->incrementSentAnnouncementsIpv6();
          }
        });

    if (result == PushResult::PUSH_OK) {
      pushOkCount++;
    } else if (result == PushResult::PUSH_PENDING) {
      pushPendingCount++;
    } else {
      /* PUSH_FAILED: scheduling failed, blocked state already cleared.
       * Message is dropped — peer session is likely going down. */
      pushDroppedCount++;
      XLOGF(
          WARN,
          "Group {}: Dropped message for peer {} at bit {} — "
          "deferred push scheduling failed",
          groupDescriptor_,
          adjRib->getPeerName(),
          bitPos);
    }
  }

  XLOGF(
      DBG3,
      "Group {} distributed message ({} immediate, {} pending, {} dropped)",
      groupDescriptor_,
      pushOkCount,
      pushPendingCount,
      pushDroppedCount);

  // Wait for all pending pushes to complete
  if (pushPendingCount > 0) {
    co_await waitForAllPendingPushes();

    XLOGF(DBG3, "Group {} all pending pushes completed", groupDescriptor_);
  }
}

/*
 * @brief  Try to push message to a peer's bounded queue
 *
 * Checks if peer's queue is blocked. If not, pushes immediately.
 * If blocked, schedules a deferred push coroutine and returns PUSH_PENDING.
 *
 * onResolved (optional) runs once when the message resolves into the peer's
 * queue (inline or deferred); not run on PUSH_FAILED. Route distribution uses
 * it for per-peer UPDATE accounting; the EoR phase uses it to finalize the
 * peer's EoR.
 *
 * @param  message - Message to push (InputMessageT variant)
 * @param  adjRib - Peer's AdjRib
 * @param  bitPos - Peer's bit position in bitmap
 * @param  onResolved - Optional continuation; run only when the push resolves
 * @return PUSH_OK if pushed, PUSH_PENDING if deferred, PUSH_FAILED if
 *         scheduling failed
 */
AdjRibOutGroup::PushResult AdjRibOutGroup::tryPushToPeer(
    const nettools::bgplib::FiberBgpPeer::InputMessageT& message,
    const std::shared_ptr<AdjRib>& adjRib,
    uint64_t bitPos,
    folly::Function<void() noexcept> onResolved) noexcept {
  auto boundedQueue = adjRib->getAdjRibOutQueue();
  if (!boundedQueue) {
    XLOGF(
        WARN,
        "Group {} peer {} at bit {} has null bounded queue",
        groupDescriptor_,
        adjRib->getPeerName(),
        bitPos);
    /* No queue; return push failed. */
    return PushResult::PUSH_FAILED;
  }

  /* Check if queue is blocked */
  if (boundedQueue->isBlocked()) {
    /*
     * Queue full - mark peer blocked (sets bitmap, checks slow peer criteria)
     */
    markPeerBlocked(adjRib);

    XLOGF(
        DBG3,
        "Group {} peer {} at bit {} blocked, scheduling deferred push",
        groupDescriptor_,
        adjRib->getPeerName(),
        bitPos);

    // Schedule deferred push on the peer's asyncScope
    if (!adjRib->scheduleDeferredPushToPeer(message, std::move(onResolved))) {
      /* Scheduling failed (asyncScope null or cancelled). Clear the
       * blocked state we just set so waitForAllPushesToComplete doesn't
       * hang on a bit that will never be cleared. */
      XLOGF(
          WARN,
          "Group {}: Failed to schedule deferred push for peer {} at bit {}, "
          "clearing blocked state",
          groupDescriptor_,
          adjRib->getPeerName(),
          bitPos);
      markPeerUnblocked(adjRib);
      return PushResult::PUSH_FAILED;
    }

    return PushResult::PUSH_PENDING;
  } else {
    // Queue has space - push immediately, then run the continuation
    if (!boundedQueue->push(message)) {
      XLOGF(
          WARN,
          "Group {} failed to push message to peer {} at bit {}",
          groupDescriptor_,
          adjRib->getPeerName(),
          bitPos);
      return PushResult::PUSH_FAILED;
    }
    if (onResolved) {
      onResolved();
    }

    XLOGF(
        DBG5,
        "Group {} pushed message to peer {} at bit {} immediately",
        groupDescriptor_,
        adjRib->getPeerName(),
        bitPos);

    return PushResult::PUSH_OK;
  }
}

/*
 * @brief  Wait for all pending pushes to complete (all bits clear)
 *
 * Suspends the coroutine and periodically checks if all blocked bits are
 * cleared. Each bit is cleared by its corresponding deferredPushToPeer()
 * coroutine when that peer's push completes.
 *
 * Key improvement: This suspends the coroutine (allowing other groups and
 * coroutines to run) instead of blocking the entire EventBase thread.
 *
 * BUSY-WAIT JUSTIFICATION:
 * This function uses co_reschedule_on_current_executor without explicit sleep,
 * which may appear to be a busy-wait loop. However, this is acceptable because:
 * 1. Single-threaded EventBase: All coroutines run on the same event loop
 * 2. Cooperative multitasking: When we yield, the EventBase schedules other
 *    ready coroutines (including deferredPushToPeer) to run
 * 3. Progress guarantee: deferredPushToPeer coroutines will make progress
 *    (waiting on queue space, pushing, clearing bits) before we're rescheduled
 * 4. Not spinning: Between yields, other work runs on the EventBase. We don't
 *    consume CPU while suspended - the EventBase scheduler handles resume
 * 5. No sleep needed: Adding sleep would only add latency. The bits will be
 *    cleared as soon as deferredPushToPeer coroutines complete their pushes
 *
 * This pattern is equivalent to: while(!condition) { co_await other_work; }
 * where other_work is implicitly "run other ready coroutines on this executor".
 *
 * @return void
 */
folly::coro::Task<void> AdjRibOutGroup::waitForAllPendingPushes() noexcept {
  /* Check for asyncScope cancellation at start */
  co_await folly::coro::co_safe_point;

  /* Wait until all blocked bits are cleared by yielding to other coroutines.
   * Each yield allows deferredPushToPeer coroutines to run and clear bits.
   */
  while (!BitmapUtils::isAllBitsCleared(adjRibBlockedBitmap_)) {
    /* Check for cancellation at start of each iteration */
    co_await folly::coro::co_safe_point;

    /* Yield to allow deferred push coroutines to run and clear bits.
     * This is NOT a busy-wait - see function comment for justification.
     */
    co_await folly::coro::co_reschedule_on_current_executor;
  }

  XLOGF(
      DBG4,
      "Group {} all pending pushes completed, all bits clear",
      groupDescriptor_);
}

/*
 * Register a peer with this update group
 * Assigns bit position and updates tracking structures
 * Handles different states based on whether group is initialized
 */
void AdjRibOutGroup::registerPeer(const std::shared_ptr<AdjRib>& adjRib) {
  if (!adjRib) {
    XLOGF(
        WARN,
        "Group {} received null adjRib in registerPeer",
        groupDescriptor_);
    return;
  }

  /*
   * Assign bit position using ConsumerBitManager
   * This properly handles bit reuse when peers leave and rejoin
   */
  uint64_t bit = bitManager_.getConsumerBit();

  // Store the mapping
  bitToAdjRibs_[bit] = adjRib;

  // Cache bit position in AdjRib for efficient lookups
  adjRib->setGroupBitPosition(bit);

  // Cache peering params from first peer
  if (!peeringParams_.has_value()) {
    peeringParams_ = adjRib->getPeeringParams();
    XLOGF(
        DBG2,
        "Group {}: Cached peering params from first peer {} at bit {}",
        groupDescriptor_,
        adjRib->getPeerName(),
        bit);
  }

  // Set bit in established bitmap
  BitmapUtils::setBit(adjRibEstablishedBitmap_, bit);

  // Determine peer state based on group state
  if (state_ == UpdateGroupState::UNINITIALIZED) {
    /*
     * Group not yet initialized - peer joins and waits for initial dump
     * Mark as in-sync immediately so peer receives dump when it happens
     */
    XLOGF(
        DBG1,
        "Group {}: Registered peer {} at bit {} with State Transition: {} -> {}",
        groupDescriptor_,
        adjRib->getPeerName(),
        bit,
        adjRib->getPeerState(),
        PeerUpdateState::INIT);
    adjRib->setPeerState(PeerUpdateState::INIT);

    /*
     * Reset stale per-peer CL consumer set during AdjRib construction.
     * It was never registered with the ChangeTracker and would block
     * registerDetachedConsumer() on first detach.
     */
    adjRib->resetChangeListConsumer();

    /* Set in-sync bitmap bit - peer is ready to receive group dump */
    setSyncBit(bit, adjRib);
  } else {
    /*
     * Group already initialized and running
     * New peer must catch up independently before joining
     */
    detachedPeers_.insert(adjRib);

    XLOGF(
        DBG1,
        "Group {}: Registered peer {} at bit {} with State Transition: {} -> {}",
        groupDescriptor_,
        adjRib->getPeerName(),
        bit,
        adjRib->getPeerState(),
        PeerUpdateState::DETACHED_INIT_DUMP);
    adjRib->setPeerState(PeerUpdateState::DETACHED_INIT_DUMP);
    adjRib->setAdjRibFlag(AdjRib::DETACHED_ON_REGISTRATION);

    /*
     * Reset the stale per-peer CL consumer set during AdjRib construction,
     * mirroring the INIT branch above. It was never
     * registered with the ChangeTracker, so leaving it in place makes
     * registerDetachedConsumer() short-circuit on its `if
     * (changeListConsumer_)` guard when
     * PeerManagerBase::processRibDumpReqWithCancellationCoro registers this
     * detached peer. That skips creation of
     * changeListConsumeTimer_, so detached CL consumption would never start for
     * a peer entering a running group.
     */
    adjRib->resetChangeListConsumer();

    /*
     * Schedule individual initial dump for this peer
     * This will be done by PeerManagerBase after registerPeer returns
     */
  }
}

/*
 * Unregister a peer from this update group
 * Cleans up tracking structures and bitmaps
 */
void AdjRibOutGroup::unregisterPeer(
    const std::shared_ptr<AdjRib>& adjRib) noexcept {
  if (!adjRib) {
    XLOGF(
        WARN,
        "Group {} received null adjRib in unregisterPeer",
        groupDescriptor_);
    return;
  }

  /*
   * Cancel any pending deferred push coroutines for this peer
   * Note: asyncScope_ manages all group coroutines, cannot selectively cancel
   * per-peer. Clearing adjRibBlockedBitmap_ bit is sufficient - deferred push
   * coroutines hold shared_ptr<AdjRib> and will handle cleanup gracefully.
   * When coroutine wakes up, it will find bit cleared and return safely.
   */

  /* Get bit position from adjRib. */
  uint64_t bit = adjRib->getGroupBitPosition();

  if (adjRib->isDetachedPeer()) {
    XLOGF(
        INFO,
        "Group {}: Cleaning up detached peer {} at bit {}",
        groupDescriptor_,
        adjRib->getPeerName(),
        bit);
    /*
     * cleanUpPeerRibOut removes the detached peer's own advertisements
     * from both its per-container counts and the global totalSentPrefixCount as
     * it walks the peer's entries -- it is the single accounting point for a
     * departing detached peer (the AdjRib::sessionTerminated teardown loop
     * would otherwise do the per-prefix decrement, but is a no-op once these
     * entries are erased). It must run before deactivateDetachedModeProcessing,
     * which zeroes the detachedRibVersion the shared-entry check relies on.
     */
    cleanUpPeerRibOut(adjRib);
    adjRib->deactivateDetachedModeProcessing();
  } else if (isPeerInSync(bit)) {
    /*
     * An in-sync peer shares the group's RIB-OUT, so the prefixes it advertised
     * equal the group's postOutPrefixCount. Remove that single peer's share
     * from the global total. The group's own local counts are left untouched
     * because it keeps advertising the same prefixes to its remaining in-sync
     * peers.
     *
     * Gate on isPeerInSync: only in-sync peers are counted in the global, so a
     * peer torn down before reaching in-sync must not subtract its group share.
     *
     * Gate on enableUpdateGroup_: when update groups are disabled, the
     * AdjRib::sessionTerminated teardown loop decrements the global per-prefix
     * (itself guarded by !enableUpdateGroup_), so subtracting the group share
     * here too would double-decrement.
     */
    if (enableUpdateGroup_) {
      stats_.subtractFromTotalSentPrefixCount(stats_.getPostOutPrefixCount());
    }
  }

  XLOGF(
      DBG1,
      "Group {}: Peer {} at bit {} State Transition: {} -> {}",
      groupDescriptor_,
      adjRib->getPeerName(),
      bit,
      adjRib->getPeerState(),
      PeerUpdateState::DOWN);
  adjRib->setPeerState(PeerUpdateState::DOWN);

  adjRib->resetPeerBlockInfo();
  removePeer(adjRib);

  /*
   * If this removal left the group with members but no in-sync peers, recover:
   * clear the packing list and try to promote a detached peer. A group with a
   * sharing DSP keeps consuming changes until both consumers reach the end;
   * otherwise its consumer remains inactive until a DEP-A finishes draining.
   *
   * TODO: optimize -- this runs synchronously per removal. Skip it when nothing
   * is promotable (no DETACHED_READY_TO_JOIN peers).
   */
  recoverIfNoSyncPeers();

  XLOGF(
      INFO,
      "Group {}: Successfully unregistered peer {} from bit {}",
      groupDescriptor_,
      adjRib->getPeerName(),
      bit);

  /*
   * Drop the peer's reference to this group. removePeer already severed
   * group -> AdjRib; without this the AdjRib -> group edge survives, and since
   * a session going down without the peer being deleted (hold timer,
   * NOTIFICATION, TCP reset, graceful restart) keeps the AdjRib in
   * PeerManagerBase::adjRibs_, a departed last member would pin the whole group
   * until it re-establishes. The caller holds its own shared_ptr across
   * maybeDestroyUpdateGroups, so this does not destroy the group here.
   *
   * The AdjRib keeps serving CLI/thrift reads while it is down, so every
   * egress accessor that reaches into the group tolerates a null group and
   * reports an empty RIB-OUT -- which is what a peer with no update group has.
   */
  adjRib->setUpdateGroup(nullptr);

  /*
   * Note: We don't call maybeDestroyUpdateGroups() here because
   * the group might still have other members.
   */
}

/*
 * Erase a detached peer's diverged (lazily-cloned) per-peer RIB-OUT entries
 * from the group's trees.
 * Every entry the peer advertised has its prefix counts decremented
 * (preOutPrefixCount, plus postOutPrefixCount when the entry was advertised):
 * the peer's counts were seeded from the group at detach
 * (copyEgressPrefixCountsFrom) and it advertised both its diverged and its
 * shared entries, so all of them must be settled here. Erasing the per-peer
 * entries also makes AdjRib::sessionTerminated's teardown decrement a no-op, so
 * this is the single place those counts are settled. For diverged entries (peer
 * owner key): erased from the tree. For shared entries (group owner key,
 * ribVersion <= detachedRibVersion): left in place — they remain owned by the
 * group, still serving its other members.
 *
 * With add-path the two are not mutually exclusive: divergence is per (prefix,
 * pathId), so one prefix can hold both peer-owned paths and group paths the
 * peer still shares, and both have to be settled. The Lite tree has no such
 * mix — one entry per owner per prefix, and the peer's own supersedes the
 * group's.
 *
 * Must be called after capturing detachedRibVersion but before removePeer()
 * clears the peer's bit position.
 */
void AdjRibOutGroup::cleanUpPeerRibOut(
    const std::shared_ptr<AdjRib>& adjRib) noexcept {
  auto peerOwnerKey = adjRib->getPeerOwnerKey();
  auto groupOwnerKey = getGroupOwnerKey();
  auto detachedRibVersion = adjRib->getDetachedRibVersion();
  uint64_t bit = adjRib->getGroupBitPosition();
  size_t deletedCount = 0;
  size_t sharedCount = 0;

  if (adjRib->sendAddPath()) {
    /* Clean up PathTree entries with peer owner key */
    std::vector<AdjRibPathTree::Iterator> emptyPathNodes;
    for (auto itr = PathTree_.begin(); itr != PathTree_.end(); ++itr) {
      auto& ownerMap = itr->value();
      auto peerIt = ownerMap.find(peerOwnerKey);
      auto groupIt = ownerMap.find(groupOwnerKey);
      const bool peerOwnsEntry = peerIt != ownerMap.end();
      if (!peerOwnsEntry && groupIt == ownerMap.end()) {
        continue;
      }
      bool isV4 = itr.ipAddress().isV4();

      /*
       * Divergence is per (prefix, pathId), so one node can hold both
       * peer-owned paths and group paths the peer still shares. Settle both --
       * mirroring resolvePathEntriesForPeer, the canonical answer to "what did
       * this peer advertise at this prefix" -- otherwise a mixed node leaks the
       * shared paths' counts. The shared loop runs before the peer entries are
       * erased because it needs the peer's pathIds to tell which group paths
       * the peer diverged from.
       */
      if (groupIt != ownerMap.end()) {
        for (const auto& [pathId, entry] : groupIt->second) {
          const bool peerHasOwnEntry = peerOwnsEntry &&
              peerIt->second.find(pathId) != peerIt->second.end();
          if (isEntryShared(
                  peerHasOwnEntry,
                  detachedRibVersion,
                  entry->getRibVersion())) {
            /*
             * The peer advertised this shared (group-owned) entry too -- its
             * counts were seeded from the group at detach
             * (copyEgressPrefixCountsFrom) -- so decrement them even though
             * the entry stays owned by the group.
             */
            if (entry->getPostAttr()) {
              adjRib->decrementPostOutPrefixCount(isV4);
            }
            adjRib->decrementPreOutPrefixCount(isV4);
            sharedCount++;
          }
        }
      }

      if (peerOwnsEntry) {
        /*
         * Decrement the peer's own prefix counts for each per-peer entry we
         * erase. AdjRib::sessionTerminated's teardown loop would otherwise do
         * this, but it becomes a no-op once these per-peer entries are gone.
         */
        for (const auto& [_, entry] : peerIt->second) {
          if (entry->getPostAttr()) {
            adjRib->decrementPostOutPrefixCount(isV4);
          }
          adjRib->decrementPreOutPrefixCount(isV4);
        }
        ownerMap.erase(peerIt);
        deletedCount++;
        if (ownerMap.empty()) {
          emptyPathNodes.push_back(itr);
        }
      }
    }
    for (auto& itr : emptyPathNodes) {
      PathTree_.erase(itr);
    }

    XLOGF(
        INFO,
        "Group {}: Deleted {} PathTree entries, left {} shared with group for detached peer {} at bit {}",
        groupDescriptor_,
        deletedCount,
        sharedCount,
        adjRib->getPeerName(),
        bit);
  } else {
    /* Clean up LiteTree entries with peer owner key */
    std::vector<AdjRibLiteTree::Iterator> emptyLiteNodes;
    for (auto itr = LiteTree_.begin(); itr != LiteTree_.end(); ++itr) {
      auto& ownerMap = itr->value();
      auto peerIt = ownerMap.find(peerOwnerKey);
      if (peerIt != ownerMap.end()) {
        /* Decrement the peer's own prefix counts for the per-peer entry we
         * erase here (see the PathTree branch above). */
        bool isV4 = itr.ipAddress().isV4();
        if (peerIt->second->getPostAttr()) {
          adjRib->decrementPostOutPrefixCount(isV4);
        }
        adjRib->decrementPreOutPrefixCount(isV4);
        ownerMap.erase(peerIt);
        deletedCount++;
        if (ownerMap.empty()) {
          emptyLiteNodes.push_back(itr);
        }
      } else {
        auto groupIt = ownerMap.find(groupOwnerKey);
        if (groupIt != ownerMap.end() &&
            isEntryShared(
                /*peerHasOwnEntry=*/false,
                detachedRibVersion,
                groupIt->second->getRibVersion())) {
          /* Shared (group-owned) entry the peer advertised too; decrement its
           * counts (see the PathTree branch above). */
          bool isV4 = itr.ipAddress().isV4();
          if (groupIt->second->getPostAttr()) {
            adjRib->decrementPostOutPrefixCount(isV4);
          }
          adjRib->decrementPreOutPrefixCount(isV4);
          sharedCount++;
        }
      }
    }
    for (auto& itr : emptyLiteNodes) {
      LiteTree_.erase(itr);
    }

    XLOGF(
        INFO,
        "Group {}: Deleted {} LiteTree entries, left {} shared with group for detached peer {} at bit {}",
        groupDescriptor_,
        deletedCount,
        sharedCount,
        adjRib->getPeerName(),
        bit);
  }
}

void AdjRibOutGroup::incrementPeersDetachedAfterJoin() noexcept {
  ++numPeersDetachedAfterJoin_;
}

void AdjRibOutGroup::decrementPeersDetachedAfterJoin() noexcept {
  if (numPeersDetachedAfterJoin_ == 0) {
    /*
     * Underflow: a peer is leaving the detached set but the counter is already
     * zero. This indicates an increment/decrement accounting bug.
     */
    XLOGF_EVERY_MS(
        ERR,
        200000,
        "Group {}: numPeersDetachedAfterJoin_ underflow on decrement",
        groupDescriptor_);
    return;
  }
  --numPeersDetachedAfterJoin_;
}

void AdjRibOutGroup::removePeer(
    const std::shared_ptr<AdjRib>& adjRib) noexcept {
  uint64_t bit = adjRib->getGroupBitPosition();

  adjRib->resetSlowPeerDurationTimer();

  BitmapUtils::clearBit(adjRibEstablishedBitmap_, bit);
  clearSyncBit(bit, adjRib);
  BitmapUtils::clearBit(adjRibBlockedBitmap_, bit);

  /*
   * A detached-after-join member (detachedRibVersion > 0) is leaving this
   * group, so drop it from numPeersDetachedAfterJoin_ -- otherwise the stale
   * count keeps the group consumer running for a peer that is gone. This is a
   * no-op for peers that already deactivated detached mode processing (e.g.
   * unregisterPeer), which cleared the version.
   */
  if (adjRib->getDetachedRibVersion() > 0) {
    decrementPeersDetachedAfterJoin();
  }
  detachedPeers_.erase(adjRib);

  bitToAdjRibs_.erase(bit);
  bitManager_.freeConsumerBit(bit);
  adjRib->clearGroupBitPosition();
}

void AdjRibOutGroup::movePeerMaterializedRibOutPathEntries(
    const std::vector<std::shared_ptr<AdjRib>>& peersToMove,
    const std::shared_ptr<AdjRibOutGroup>& newGroup) noexcept {
  auto groupOwnerKey = getGroupOwnerKey();
  size_t movedCount = 0;
  size_t copiedCount = 0;
  std::vector<AdjRibPathTree::Iterator> emptyNodes;

  for (auto itr = PathTree_.begin(); itr != PathTree_.end(); ++itr) {
    auto& ownerMap = itr->value();
    auto prefix = folly::CIDRNetwork{itr.ipAddress(), itr.masklen()};
    PathOwnerMap* newOwnerMapForPrefix = nullptr;
    auto copyToNewGroup = [&](const AdjRibOutOwnerKey& ownerKey,
                              const AdjRibEntry* entry) {
      if (!newOwnerMapForPrefix) {
        newOwnerMapForPrefix = &newGroup->getOrCreatePathOwnerMap(prefix);
      }
      newGroup->copyEntryForOwner(*newOwnerMapForPrefix, ownerKey, entry);
    };

    for (const auto& adjRib : peersToMove) {
      auto peerOwnerKey = adjRib->getPeerOwnerKey();
      auto detachedRibVersion = adjRib->getDetachedRibVersion();

      // Move the peer's own entries to the new group.
      auto peerEntryItr = ownerMap.find(peerOwnerKey);
      bool peerOwnsEntry = peerEntryItr != ownerMap.end();
      if (peerOwnsEntry) {
        for (auto& [_, entry] : peerEntryItr->second) {
          copyToNewGroup(peerOwnerKey, entry.get());
          movedCount++;
        }
      }

      /*
       * Copy the shared group entries the peer was using before detachment,
       * skipping any pathId the peer already owns. Divergence is per (prefix,
       * pathId), so a path the peer diverged on is not shared no matter what
       * the group entry's version says, and copyEntryForOwner overwrites --
       * without this skip the group's attributes would silently replace the
       * peer's own entry that was just moved. Mirrors shouldClonePathForPeer,
       * which tests peer ownership before the version gate, and the LiteTree
       * counterpart, which skips the shared copy once it has moved a peer
       * entry.
       */
      auto groupItr = ownerMap.find(groupOwnerKey);
      if (groupItr != ownerMap.end()) {
        for (auto& [pathId, entry] : groupItr->second) {
          const bool peerHasOwnEntry = peerOwnsEntry &&
              peerEntryItr->second.find(pathId) != peerEntryItr->second.end();
          if (isEntryShared(
                  peerHasOwnEntry,
                  detachedRibVersion,
                  entry->getRibVersion())) {
            copyToNewGroup(peerOwnerKey, entry.get());
            copiedCount++;
          }
        }
      }

      /*
       * Erase the peer's entries last: the shared-entry loop above needs them
       * to tell which pathIds the peer already owns.
       */
      if (peerEntryItr != ownerMap.end()) {
        ownerMap.erase(peerEntryItr);
      }
    }

    // Clean up empty ownerMaps to not leave any empty radix node
    if (ownerMap.empty()) {
      emptyNodes.push_back(itr);
    }
  }

  for (auto& itr : emptyNodes) {
    PathTree_.erase(itr);
  }

  XLOGF(
      INFO,
      "Group {}: Moved {} per-peer PathTree entries, copied {} shared entries "
      "for {} peer(s) to group {}",
      groupDescriptor_,
      movedCount,
      copiedCount,
      peersToMove.size(),
      newGroup->getAdjRibGroupName());
}

void AdjRibOutGroup::movePeerMaterializedRibOutLiteEntries(
    const std::vector<std::shared_ptr<AdjRib>>& peersToMove,
    const std::shared_ptr<AdjRibOutGroup>& newGroup) noexcept {
  auto groupOwnerKey = getGroupOwnerKey();
  size_t movedCount = 0;
  size_t copiedCount = 0;
  std::vector<AdjRibLiteTree::Iterator> emptyNodes;

  for (auto itr = LiteTree_.begin(); itr != LiteTree_.end(); ++itr) {
    auto& ownerMap = itr->value();
    auto prefix = folly::CIDRNetwork{itr.ipAddress(), itr.masklen()};
    LiteOwnerMap* newOwnerMapForPrefix = nullptr;
    auto copyToNewGroup = [&](const AdjRibOutOwnerKey& ownerKey,
                              const AdjRibEntry* entry) {
      if (!newOwnerMapForPrefix) {
        newOwnerMapForPrefix = &newGroup->getOrCreateLiteOwnerMap(prefix);
      }
      newGroup->copyEntryForOwner(*newOwnerMapForPrefix, ownerKey, entry);
    };

    for (const auto& adjRib : peersToMove) {
      auto peerOwnerKey = adjRib->getPeerOwnerKey();
      auto detachedRibVersion = adjRib->getDetachedRibVersion();

      // Move the peer's own entry to the new group, then delete from old.
      auto entryItr = ownerMap.find(peerOwnerKey);
      if (entryItr != ownerMap.end()) {
        copyToNewGroup(peerOwnerKey, entryItr->second.get());
        ownerMap.erase(entryItr);
        movedCount++;
        continue;
      }

      // Copy shared group entries the peer was using before detachment.
      auto groupItr = ownerMap.find(groupOwnerKey);
      if (groupItr != ownerMap.end()) {
        auto groupEntry = groupItr->second.get();
        if (isEntryShared(
                /*peerHasOwnEntry=*/false,
                detachedRibVersion,
                groupEntry->getRibVersion())) {
          copyToNewGroup(peerOwnerKey, groupEntry);
          copiedCount++;
        }
      }
    }

    // Clean up empty ownerMaps to not leave any empty radix node
    if (ownerMap.empty()) {
      emptyNodes.push_back(itr);
    }
  }

  for (auto& itr : emptyNodes) {
    LiteTree_.erase(itr);
  }

  XLOGF(
      INFO,
      "Group {}: Moved {} per-peer LiteTree entries, copied {} shared entries "
      "for {} peer(s) to group {}",
      groupDescriptor_,
      movedCount,
      copiedCount,
      peersToMove.size(),
      newGroup->getAdjRibGroupName());
}

void AdjRibOutGroup::movePeers(
    const std::vector<std::shared_ptr<AdjRib>>& peersToMove,
    const std::shared_ptr<AdjRibOutGroup>& newGroup,
    const std::function<void(const std::shared_ptr<AdjRib>&)>&
        onPeerMoved) noexcept {
  XLOGF(
      INFO,
      "Group {}: Moving {} peer(s) to group {}",
      groupDescriptor_,
      peersToMove.size(),
      newGroup->getAdjRibGroupName());

  /*
   * Move the materialized RIB-OUT entries for all peers in a single pass over
   * the group's tree. The peers' entries (and the shared group entries they
   * were using) are copied to the new group under each peer's own owner key.
   */
  if (groupKey_.sendAddPath) {
    movePeerMaterializedRibOutPathEntries(peersToMove, newGroup);
  } else {
    movePeerMaterializedRibOutLiteEntries(peersToMove, newGroup);
  }

  /*
   * Register each moved peer into the new group as a detached peer. Its RIB-OUT
   * entries were already copied above, so it needs no initial dump: it drains
   * its packing list and rejoins via the detached (DEP-A) recovery path. We do
   * not special-case an empty target group -- a sole moved peer becomes SYNC
   * through promoteDetachedPeerToSync once it is ready.
   */
  for (const auto& adjRib : peersToMove) {
    XLOGF(
        INFO,
        "Group {}: Removing peer {} due to group move",
        groupDescriptor_,
        adjRib->getPeerName());

    removePeer(adjRib);
    adjRib->setUpdateGroup(newGroup);

    uint64_t bit = newGroup->bitManager_.getConsumerBit();
    newGroup->bitToAdjRibs_[bit] = adjRib;
    adjRib->setGroupBitPosition(bit);
    if (!newGroup->peeringParams_.has_value()) {
      newGroup->peeringParams_ = adjRib->getPeeringParams();
    }
    BitmapUtils::setBit(newGroup->adjRibEstablishedBitmap_, bit);

    /* Clear state that pertained to the old group. */
    adjRib->setDetachedRibVersion(0);
    adjRib->clearAdjRibFlag(AdjRib::RIB_OUT_DISCREPANCY);
    adjRib->clearAdjRibFlag(AdjRib::IS_DETACHED_FAST_PEER);

    /*
     * Join as a detached peer (a blocked peer stays blocked); it catches up
     * independently and rejoins the new group.
     */
    auto peerState = adjRib->getPeerState();
    auto targetState = (peerState == PeerUpdateState::DETACHED_BLOCKED ||
                        peerState == PeerUpdateState::JOINED_BLOCKED)
        ? PeerUpdateState::DETACHED_BLOCKED
        : PeerUpdateState::DETACHED_INIT_DUMP;
    adjRib->setPeerState(targetState);
    adjRib->setAdjRibFlag(AdjRib::DETACHED_ON_REGISTRATION);
    adjRib->setDetachReason(DetachReason::Policy);

    newGroup->detachedPeers_.insert(adjRib);

    if (onPeerMoved) {
      onPeerMoved(adjRib);
    }
  }
}

void AdjRibOutGroup::movePeersSharedRibOutPathEntries(
    const std::vector<std::shared_ptr<AdjRib>>& peersToMove,
    const std::shared_ptr<AdjRibOutGroup>& newGroup) noexcept {
  const auto groupOwnerKey = getGroupOwnerKey();
  const auto newGroupOwnerKey = newGroup->getGroupOwnerKey();
  size_t copiedCount = 0;
  size_t movedCount = 0;
  std::vector<AdjRibPathTree::Iterator> emptyNodes;

  for (auto itr = PathTree_.begin(); itr != PathTree_.end(); ++itr) {
    auto& ownerMap = itr->value();
    auto prefix = folly::CIDRNetwork{itr.ipAddress(), itr.masklen()};
    PathOwnerMap* newOwnerMapForPrefix = nullptr;
    auto copyToNewGroup = [&](const AdjRibOutOwnerKey& ownerKey,
                              const AdjRibEntry* entry) {
      if (!newOwnerMapForPrefix) {
        newOwnerMapForPrefix = &newGroup->getOrCreatePathOwnerMap(prefix);
      }
      newGroup->copyEntryForOwner(*newOwnerMapForPrefix, ownerKey, entry);
    };

    // Copy this group's group-owned entries to the new group (kept here).
    auto groupItr = ownerMap.find(groupOwnerKey);
    if (groupItr != ownerMap.end()) {
      for (auto& [_, entry] : groupItr->second) {
        copyToNewGroup(newGroupOwnerKey, entry.get());
        copiedCount++;
      }
    }

    // Move the per-peer entries of the given peers (erased from this group).
    for (const auto& peer : peersToMove) {
      auto peerOwnerKey = peer->getPeerOwnerKey();
      auto peerItr = ownerMap.find(peerOwnerKey);
      if (peerItr != ownerMap.end()) {
        for (auto& [_, entry] : peerItr->second) {
          copyToNewGroup(peerOwnerKey, entry.get());
          movedCount++;
        }
        ownerMap.erase(peerItr);
      }
    }

    // Clean up empty ownerMaps to not leave any empty radix node
    if (ownerMap.empty()) {
      emptyNodes.push_back(itr);
    }
  }
  for (auto& itr : emptyNodes) {
    PathTree_.erase(itr);
  }

  XLOGF(
      INFO,
      "Group {}: Copied {} group-owned PathTree entries, moved {} per-peer "
      "entries for {} peer(s) to group {}",
      groupDescriptor_,
      copiedCount,
      movedCount,
      peersToMove.size(),
      newGroup->getAdjRibGroupName());
}

void AdjRibOutGroup::movePeersSharedRibOutLiteEntries(
    const std::vector<std::shared_ptr<AdjRib>>& peersToMove,
    const std::shared_ptr<AdjRibOutGroup>& newGroup) noexcept {
  const auto groupOwnerKey = getGroupOwnerKey();
  const auto newGroupOwnerKey = newGroup->getGroupOwnerKey();
  size_t copiedCount = 0;
  size_t movedCount = 0;
  std::vector<AdjRibLiteTree::Iterator> emptyNodes;

  for (auto itr = LiteTree_.begin(); itr != LiteTree_.end(); ++itr) {
    auto& ownerMap = itr->value();
    auto prefix = folly::CIDRNetwork{itr.ipAddress(), itr.masklen()};
    LiteOwnerMap* newOwnerMapForPrefix = nullptr;
    auto copyToNewGroup = [&](const AdjRibOutOwnerKey& ownerKey,
                              const AdjRibEntry* entry) {
      if (!newOwnerMapForPrefix) {
        newOwnerMapForPrefix = &newGroup->getOrCreateLiteOwnerMap(prefix);
      }
      newGroup->copyEntryForOwner(*newOwnerMapForPrefix, ownerKey, entry);
    };

    // Copy this group's group-owned entry to the new group (kept here).
    auto groupItr = ownerMap.find(groupOwnerKey);
    if (groupItr != ownerMap.end()) {
      copyToNewGroup(newGroupOwnerKey, groupItr->second.get());
      copiedCount++;
    }

    // Move the per-peer entries of the given peers (erased from this group).
    for (const auto& peer : peersToMove) {
      auto peerOwnerKey = peer->getPeerOwnerKey();
      auto peerItr = ownerMap.find(peerOwnerKey);
      if (peerItr != ownerMap.end()) {
        copyToNewGroup(peerOwnerKey, peerItr->second.get());
        ownerMap.erase(peerItr);
        movedCount++;
      }
    }

    // Clean up empty ownerMaps to not leave any empty radix node
    if (ownerMap.empty()) {
      emptyNodes.push_back(itr);
    }
  }
  for (auto& itr : emptyNodes) {
    LiteTree_.erase(itr);
  }

  XLOGF(
      INFO,
      "Group {}: Copied {} group-owned LiteTree entries, moved {} per-peer "
      "entries for {} peer(s) to group {}",
      groupDescriptor_,
      copiedCount,
      movedCount,
      peersToMove.size(),
      newGroup->getAdjRibGroupName());
}

void AdjRibOutGroup::copyGroupFieldsToNewGroup(
    const std::shared_ptr<AdjRibOutGroup>& newGroup) noexcept {
  newGroup->setChangeListTracker(
      changeListTracker_, *addPathConsumerBitmap_, *nonAddPathConsumerBitmap_);
  /* The scheduling latch is object-local: a task queued on this source group
   * cannot service newGroup. */
  newGroup->setState(state_);
  newGroup->setLastSeenRibVersion(lastSeenRibVersion_);
  newGroup->peeringParams_ = peeringParams_;
  newGroup->mraiInterval_ = mraiInterval_;
  newGroup->updateGroupConfig_ = updateGroupConfig_;
  newGroup->initialDumpCompletionTimeMs_ = initialDumpCompletionTimeMs_;
  /*
   * Deep-copy the group-level packing list so newGroup resumes packing from the
   * same CL position this group was at. Copy-assigning the F14 maps clones the
   * entries by value, matching clonePackingListForPeer.
   */
  newGroup->attrToPrefixMap_ = attrToPrefixMap_;
  /*
   * The split copies this group's RIB-OUT entries into newGroup (see
   * movePeersSharedRibOut*), so newGroup advertises the same prefix set at
   * split time. Seed its egress prefix counts to match, or newGroup's
   * postOutPrefixCount would stay 0 while it advertises real prefixes: a later
   * withdrawal would underflow it, and an in-sync peer going down would
   * subtract 0 from the global totalSentPrefixCount (leaking that peer's
   * share). The global total itself is left untouched -- the split advertises
   * nothing new, it just re-homes existing advertisements under newGroup.
   */
  newGroup->stats_.copyEgressPrefixCountsFrom(stats_);
}

void AdjRibOutGroup::splitToNewGroup(
    const std::shared_ptr<AdjRibOutGroup>& newGroup,
    const std::vector<std::shared_ptr<AdjRib>>& peersToMove) noexcept {
  XLOGF(
      INFO,
      "Group {}: Splitting {} peer(s) into new group {}",
      groupDescriptor_,
      peersToMove.size(),
      newGroup->getAdjRibGroupName());

  /*
   * 1. Copy this group's operational state and flags so newGroup is a faithful
   * copy. Identity (groupId/name/groupKey) is set by the caller at construction
   * and is intentionally not copied -- newGroup keeps its own key (which may
   * differ from this group's, e.g. a per-peer-override split).
   */
  copyGroupFieldsToNewGroup(newGroup);

  /*
   * 2. Create newGroup's change list consumer and join it at this group's CL
   * marker so newGroup resumes consumption from the exact same position.
   */
  newGroup->registerGroupConsumer();
  if (changeListTracker_ && changeListConsumer_ &&
      newGroup->getChangeListConsumer()) {
    changeListTracker_->joinConsumer(
        changeListConsumer_, newGroup->getChangeListConsumer());
  } else {
    XLOGF(
        WARN,
        "Group {}: no CL consumer to join newGroup {} at; newGroup starts at "
        "end of change list",
        groupDescriptor_,
        newGroup->getAdjRibGroupName());
  }

  /*
   * 3. Copy this group's group-owned RIB-OUT entries to newGroup (kept here
   * too) and move the moved peers' per-peer entries to newGroup.
   */
  if (groupKey_.sendAddPath) {
    movePeersSharedRibOutPathEntries(peersToMove, newGroup);
  } else {
    movePeersSharedRibOutLiteEntries(peersToMove, newGroup);
  }

  /*
   * 4. Re-home each peer. removePeer() tears down its membership on this group
   * (bitmaps, detachedPeers_, bit position) without touching its detached CL
   * consumer or detached RIB version, so its live status can be mirrored into
   * newGroup at a freshly allocated bit (bit positions are not preserved across
   * the split). Status is snapshotted from the old bit before removePeer()
   * clears it.
   */
  for (const auto& peer : peersToMove) {
    uint64_t oldBit = peer->getGroupBitPosition();
    bool wasInSync = isPeerInSync(oldBit);
    bool wasBlocked = BitmapUtils::isBitSet(adjRibBlockedBitmap_, oldBit);
    uint64_t detachedRibVersion = peer->getDetachedRibVersion();

    /*
     * removePeer does not run handleNoSyncPeers; we call it once at the end so
     * the recovery only promotes a remaining peer after every peer in
     * peersToMove has been removed (running it mid-loop could promote a peer
     * that is still pending its own move).
     */
    removePeer(peer);

    /* Faithful register into newGroup. */
    uint64_t newBit = newGroup->bitManager_.getConsumerBit();
    newGroup->bitToAdjRibs_[newBit] = peer;
    peer->setGroupBitPosition(newBit);
    peer->setUpdateGroup(newGroup);
    BitmapUtils::setBit(newGroup->adjRibEstablishedBitmap_, newBit);

    if (wasInSync) {
      newGroup->setSyncBit(newBit, peer);
      /*
       * Only in-sync peers carry the blocked bit (detachPeer clears it when a
       * peer leaves the sync set), so a JOINED_BLOCKED peer -- in-sync AND
       * blocked -- must have its blocked bit restored here.
       */
      if (wasBlocked) {
        BitmapUtils::setBit(newGroup->adjRibBlockedBitmap_, newBit);
        /*
         * A carried-over blocked peer has an in-flight push from its previous
         * group; flag newGroup so buildAndSendGroupBgpMessages drains it before
         * packing to preserve BGP UPDATE ordering.
         */
        newGroup->checkForBlockedPeers_ = true;
      }
    } else {
      newGroup->detachedPeers_.insert(peer);
      if (detachedRibVersion > 0) {
        newGroup->incrementPeersDetachedAfterJoin();
      }
    }

    XLOGF(
        INFO,
        "Group {}: Moved peer {} (bit {} -> {}) to new group {} as {}",
        groupDescriptor_,
        peer->getPeerName(),
        oldBit,
        newBit,
        newGroup->getAdjRibGroupName(),
        peer->getPeerState());
  }
}

/*
 * Mark a peer as blocked due to TCP backpressure.
 * Sets bitmap bit, checks frequency threshold if enabled, schedules duration
 * timer.
 */
void AdjRibOutGroup::markPeerBlocked(
    const std::shared_ptr<AdjRib>& adjRib) noexcept {
  uint64_t bitPos = adjRib->getGroupBitPosition();

  BitmapUtils::setBit(adjRibBlockedBitmap_, bitPos);

  // Transition peer state: JOINED_RUNNING -> JOINED_BLOCKED
  XLOGF(
      DBG1,
      "Group {}: Peer {} at bit {} State Transition: {} -> {}",
      groupDescriptor_,
      adjRib->getPeerName(),
      bitPos,
      adjRib->getPeerState(),
      PeerUpdateState::JOINED_BLOCKED);
  adjRib->setPeerState(PeerUpdateState::JOINED_BLOCKED);

  if (updateGroupConfig_.enableSlowPeerBlockCountDetection) {
    // Frequency-based detection: fixed window with counter reset
    auto now = std::chrono::steady_clock::now();
    auto& info = adjRib->getPeerBlockInfo();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - info.windowStart);
    if (elapsed >= updateGroupConfig_.slowPeerBlockCountWindow) {
      // Window expired, reset counter
      info.windowStart = now;
      info.blockCount = 1;
    } else {
      info.blockCount++;
    }

    if (info.blockCount >= updateGroupConfig_.slowPeerBlockCountThreshold) {
      XLOGF(
          INFO,
          "Group {}: Peer {} at bit {} exceeded block frequency threshold "
          "({} blocks in window)",
          groupDescriptor_,
          adjRib->getPeerName(),
          bitPos,
          info.blockCount);
      detachSlowPeer(adjRib);
      return;
    }
  }

  // Duration-based detection: schedule timer on AdjRib
  adjRib->scheduleSlowPeerDurationTimer(
      evb_, updateGroupConfig_.slowPeerTimeThreshold, adjRib);
}

/*
 * Mark a peer as unblocked (queue space available).
 * Clears bitmap bit, cancels duration timer.
 */
void AdjRibOutGroup::markPeerUnblocked(
    const std::shared_ptr<AdjRib>& adjRib) noexcept {
  uint64_t bitPos = adjRib->getGroupBitPosition();

  BitmapUtils::clearBit(adjRibBlockedBitmap_, bitPos);

  // Cancel duration timer if running
  adjRib->cancelSlowPeerDurationTimer();

  auto peerState = adjRib->getPeerState();
  if (peerState == PeerUpdateState::DETACHED_BLOCKED) {
    XLOGF(
        DBG1,
        "Group {}: Peer {} at bit {} State Transition: {} -> {}",
        groupDescriptor_,
        adjRib->getPeerName(),
        bitPos,
        adjRib->getPeerState(),
        PeerUpdateState::DETACHED_RUNNING);
    adjRib->setPeerState(PeerUpdateState::DETACHED_RUNNING);
    adjRib->activateDetachedModeProcessing();
  } else if (peerState == PeerUpdateState::JOINED_BLOCKED) {
    XLOGF(
        DBG1,
        "Group {}: Peer {} at bit {} State Transition: {} -> {}",
        groupDescriptor_,
        adjRib->getPeerName(),
        bitPos,
        adjRib->getPeerState(),
        PeerUpdateState::JOINED_RUNNING);
    adjRib->setPeerState(PeerUpdateState::JOINED_RUNNING);
  }
}

/*
 * Check if any peer in the bitmap is currently blocked.
 */
bool AdjRibOutGroup::hasBlockedPeers() const noexcept {
  return !BitmapUtils::isAllBitsCleared(adjRibBlockedBitmap_);
}

/*
 * Core peer detachment logic shared by detachSlowPeer and policy re-evaluation.
 * detachPeer transitions the peer to its detached state: DETACHED_BLOCKED if it
 * was blocked (JOINED_BLOCKED), otherwise DETACHED_RUNNING.
 */
void AdjRibOutGroup::detachPeer(
    const std::shared_ptr<AdjRib>& adjRib,
    DetachReason reason) noexcept {
  uint64_t bit = adjRib->getGroupBitPosition();

  XLOGF(
      INFO,
      "Group {}: Detaching peer {} at bit {}",
      groupDescriptor_,
      adjRib->getPeerName(),
      bit);

  // 1. Copy egress prefix counts while peer is still IN_SYNC
  adjRib->copyEgressPrefixCountsFrom(stats_);

  // 2. Clone packing list for the peer
  clonePackingListForPeer(adjRib);

  // 3. Mark peer as detached (adds to detachedPeers_, clears sync bit)
  markPeerDetached(adjRib);

  // 4. Peer inherits version fields
  adjRib->setLastSeenRibVersion(lastSeenRibVersion_);
  adjRib->setDetachedRibVersion(lastSeenRibVersion_);

  /*
   * Count this peer toward numPeersDetachedAfterJoin_ when it detaches at a
   * non-zero RIB version (it was in sync at a real version). Callers guard with
   * !isDetachedPeer(), so detachPeer runs once per detach and counts exactly
   * once; version > 0 thus means "counted". The count is dropped again only by
   * deactivateDetachedModeProcessing (rejoin / peer-down) or removePeer (move /
   * unregister), both guarded on the same non-zero version.
   */
  if (adjRib->getDetachedRibVersion() != 0) {
    incrementPeersDetachedAfterJoin();
  }

  // 5. Clear blocked bitmap (peer is leaving the group's sync set)
  BitmapUtils::clearBit(adjRibBlockedBitmap_, bit);

  // 6. Cancel slow peer duration timer (may be pending if peer was blocked)
  adjRib->cancelSlowPeerDurationTimer();

  // 7. Register detached consumer at group's CL position
  if (changeListTracker_ && changeListConsumer_ && addPathConsumerBitmap_ &&
      nonAddPathConsumerBitmap_) {
    adjRib->registerDetachedConsumerAtGroupPosition(
        changeListTracker_,
        changeListConsumer_,
        *addPathConsumerBitmap_,
        *nonAddPathConsumerBitmap_);
  } else {
    XLOGF(
        WARN,
        "Group {}: Cannot register detached consumer for peer {} at bit {} - "
        "missing changeListTracker or consumer bitmaps",
        groupDescriptor_,
        adjRib->getPeerName(),
        bit);
  }

  /*
   * 8. EoR pending state needs no action here. In-sync peers are marked as
   * owing EoR when the EoR becomes owed (walkAndProcessShadowRib), and
   * markEgressEoRSent clears each AFI the instant that peer's push resolves, so
   * the peer already carries exactly the AFIs it still owes. Copying the
   * group's coarser "owed to all in-sync peers" flags here would set an AFI
   * this peer was already committed back to pending and make its detached
   * sendPendingEoRs emit a duplicate EoR.
   */

  // 9. Record this detachment in the peer's cumulative per-reason counters
  switch (reason) {
    case DetachReason::Blocking:
      adjRib->incrementTimesDetachedByBlocking();
      break;
    case DetachReason::Policy:
      adjRib->incrementTimesDetachedByPolicy();
      break;
    case DetachReason::RouteRefresh:
      adjRib->incrementTimesDetachedByRouteRefresh();
      break;
  }

  // Persist the reason and time so they can be reported after the peer rejoins
  adjRib->setDetachReason(reason);

  /*
   * 10. Transition the peer to a detached state. A blocked peer
   * (JOINED_BLOCKED) stays blocked (DETACHED_BLOCKED); any other in-sync peer
   * runs in detached mode (DETACHED_RUNNING). The peer state is still JOINED_*
   * here (nothing above mutates it), so it is read as the "from" state before
   * the transition.
   */
  const auto fromState = adjRib->getPeerState();
  const auto targetState = fromState == PeerUpdateState::JOINED_BLOCKED
      ? PeerUpdateState::DETACHED_BLOCKED
      : PeerUpdateState::DETACHED_RUNNING;
  XLOGF(
      DBG1,
      "Group {}: Peer {} at bit {} State Transition: {} -> {}",
      groupDescriptor_,
      adjRib->getPeerName(),
      bit,
      fromState,
      targetState);
  adjRib->setPeerState(targetState);

  XLOGF(
      INFO,
      "Group {}: Peer {} at bit {} detached successfully "
      "(lastSeenRibVersion={}, detachedRibVersion={})",
      groupDescriptor_,
      adjRib->getPeerName(),
      bit,
      lastSeenRibVersion_,
      lastSeenRibVersion_);
}

void AdjRibOutGroup::detachPeers(
    const std::vector<std::shared_ptr<AdjRib>>& peers,
    DetachReason reason) noexcept {
  for (const auto& adjRib : peers) {
    // detachPeer is not idempotent; only in-sync members need detaching.
    if (!adjRib->isDetachedPeer()) {
      detachPeer(adjRib, reason);
    }
  }
}

/*
 * Detach a slow peer from the group.
 */
void AdjRibOutGroup::detachSlowPeer(
    const std::shared_ptr<AdjRib>& adjRib) noexcept {
  uint64_t bit = adjRib->getGroupBitPosition();
  BgpStats::incrSlowPeerDetectionCount();

  // If slow peer detachment is disabled by config, skip detachment.
  if (!updateGroupConfig_.allowSlowPeerDetach) {
    XLOGF(
        INFO,
        "Group {}: Slow peer detach disabled by config, "
        "skipping detachment for peer {} at bit {}",
        groupDescriptor_,
        adjRib->getPeerName(),
        bit);
    return;
  }

  /*
   * If peer is the last synced peer in the group, skip detachment.
   * Detaching the last synced peer would leave no peers receiving group
   * updates.
   */
  if (numInSyncPeers_ <= 1) {
    XLOGF(
        INFO,
        "Group {}: Peer {} at bit {} is the only synced member, skipping detachment",
        groupDescriptor_,
        adjRib->getPeerName(),
        bit);
    return;
  }

  XLOGF(
      INFO,
      "Group {}: Detaching slow peer {} at bit {}",
      groupDescriptor_,
      adjRib->getPeerName(),
      bit);

  detachPeer(adjRib, DetachReason::Blocking);

  /*
   * EoR pending state needs no slow-peer-specific handling: the peer was marked
   * as owing EoR when the EoR became owed and markEgressEoRSent clears each AFI
   * as it commits, so the peer already carries exactly the AFIs it still owes
   * (see detachPeer step 8).
   */
}

/*
 * Mark a peer as detached from the group.
 */
void AdjRibOutGroup::markPeerDetached(
    const std::shared_ptr<AdjRib>& adjRib) noexcept {
  auto bit = adjRib->getGroupBitPosition();
  detachedPeers_.insert(adjRib);
  clearSyncBit(bit, adjRib);
  XLOGF(
      DBG2,
      "Group {}: Peer {} at bit {} marked as detached",
      groupDescriptor_,
      adjRib->getPeerName(),
      bit);
}

/*
 * Clear detached state for a peer (on rejoin).
 */
void AdjRibOutGroup::markPeerInSync(
    const std::shared_ptr<AdjRib>& adjRib) noexcept {
  auto bit = adjRib->getGroupBitPosition();
  setSyncBit(bit, adjRib);
  detachedPeers_.erase(adjRib);
  /*
   * The peer is folding back into the group's shared accounting, so it no
   * longer counts advertisements independently. Clear its snapshot egress
   * counts. The global totalSentPrefixCount is left untouched: the peer's
   * already-sent prefixes stay counted and are now tracked via the group.
   * (On promotion, promoteDetachedPeerToSync first copies these counts into the
   * group's stats_ so the group adopts the peer's view before this clear.)
   *
   * By this point the peer's snapshot egress counts should already equal the
   * group's -- the rejoin collapse / promotion reconciled the RIB-OUTs. A
   * mismatch indicates an upstream increment/decrement accounting bug. We do
   * NOT reconcile here: the group's stats_ is authoritative and we still clear
   * so the peer folds in cleanly. The mismatch is surfaced only via the ERR
   * log below (grep/Scuba) -- there is intentionally no ODS counter for it, so
   * a resulting global divergence is observable in logs but not in ODS charts.
   */
  const auto& peerStats = adjRib->getStats();
  if (peerStats.getPreOutPrefixCount() != stats_.getPreOutPrefixCount() ||
      peerStats.getPostOutPrefixCount() != stats_.getPostOutPrefixCount()) {
    XLOGF(
        ERR,
        "Group {}: Peer {} egress prefix counts differ from the group before "
        "clearing on markPeerInSync (peer preOut {} / postOut {}, group "
        "preOut {} / postOut {})",
        groupDescriptor_,
        adjRib->getPeerName(),
        peerStats.getPreOutPrefixCount(),
        peerStats.getPostOutPrefixCount(),
        stats_.getPreOutPrefixCount(),
        stats_.getPostOutPrefixCount());
  }
  adjRib->clearEgressPrefixCounts();
  XLOGF(
      DBG2,
      "Group {}: Peer {} at bit {} marked in sync",
      groupDescriptor_,
      adjRib->getPeerName(),
      bit);
}

void AdjRibOutGroup::tryRejoinDetachedPeersOnAllChangesProcessed() noexcept {
  if (!attrToPrefixMap_.empty()) {
    return;
  }

  XLOGF_IF(
      DBG2,
      state_ != UpdateGroupState::IDLE,
      "Group {}: State Transition: {} -> IDLE",
      groupDescriptor_,
      state_);
  state_ = UpdateGroupState::IDLE;

  // Check for peers ready to rejoin after PL drain or PL empty.
  checkAndAcceptReadyToJoinPeers();
}

/*
 * Check for detached peers ready to rejoin and accept them back into the group.
 * Called ONLY after PL drain completes (WAITING -> IDLE transition).
 *
 * DFP peers (DETACHED_READY_TO_JOIN + IS_DETACHED_FAST_PEER) can rejoin
 * anytime — their RIB-OUT is guaranteed identical to the group's, so no
 * collapse is needed.
 *
 * DSP peers (DETACHED_READY_TO_JOIN without IS_DETACHED_FAST_PEER) can only
 * rejoin when the group consumer is at the end of the CL, since both must be
 * at the same position. DSP peers go through tryAcceptPeersToGroup for
 * collapse verification.
 */
void AdjRibOutGroup::checkAndAcceptReadyToJoinPeers() noexcept {
  /*
   * This runs on every change-list consume tick (~mraiInterval_) for a group
   * with SYNC peers or a sharing DSP, so log only when there is actually
   * something to check. Otherwise this DBG1 line spams once per group per tick
   * with "Skipping 0 detached peers to try rejoin". Mirrors the gated follow-up
   * logs below (the function is a no-op when detachedPeers_ is empty).
   */
  XLOGF_IF(
      DBG1,
      !detachedPeers_.empty(),
      "Group {}: Checking {} detached peers to try rejoin",
      groupDescriptor_,
      detachedPeers_.size());

  std::vector<std::shared_ptr<AdjRib>> dfpPeers;
  std::vector<std::shared_ptr<AdjRib>> dspCandidates;

  /*
   * Counts of DETACHED_READY_TO_JOIN peers that could not rejoin this pass:
   * (1) kept in place with packing timers left canceled (ahead of group on CL),
   * (2) moved to DETACHED_RUNNING with packing timers rescheduled.
   */
  size_t peersKeptWithTimersCanceled = 0;
  size_t peersWithTimersRescheduled = 0;

  for (const auto& adjRib : detachedPeers_) {
    if (adjRib->getPeerState() != PeerUpdateState::DETACHED_READY_TO_JOIN) {
      continue;
    }

    if (FOLLY_UNLIKELY(adjRib->testOnlyDeferDrjAcceptance)) {
      continue;
    }

    if (adjRib->isAdjRibFlagSet(AdjRib::IS_DETACHED_FAST_PEER)) {
      dfpPeers.push_back(adjRib);
    } else if (adjRib->isReadyToRejoinGroup()) {
      /*
       * Both consumers are at the end, both packing lists are empty, and
       * versions match.
       */
      dspCandidates.push_back(adjRib);
    } else if (adjRib->getLastSeenRibVersion() > lastSeenRibVersion_) {
      /*
       * Peer is ahead of the group on the CL.
       * Do not reschedule packing timers — the peer must wait for the
       * group to catch up before it can start consuming.
       */
      ++peersKeptWithTimersCanceled;
    } else {
      /*
       * Peers that were in DETACHED_READY_TO_JOIN state must resume consuming
       * changelist since they cannot be rejoined currently, due to two possible
       * reasons:
       *   (1) The group is no longer changelist consumer READY
       *   (2) The candidate DSP peer is no longer changelist consumer READY
       * The peer must try to rejoin again after consuming more changes.
       */
      ++peersWithTimersRescheduled;
      adjRib->setPeerState(PeerUpdateState::DETACHED_RUNNING);
      adjRib->reschedulePackingTimers();
    }
  }

  XLOGF_IF(
      DBG1,
      peersKeptWithTimersCanceled > 0,
      "Group {}: {} DETACHED_READY_TO_JOIN peers kept with packing timers "
      "canceled (ahead of group on CL)",
      groupDescriptor_,
      peersKeptWithTimersCanceled);
  XLOGF_IF(
      DBG1,
      peersWithTimersRescheduled > 0,
      "Group {}: {} DETACHED_READY_TO_JOIN peers moved to DETACHED_RUNNING "
      "with packing timers rescheduled",
      groupDescriptor_,
      peersWithTimersRescheduled);

  // Accept DFP peers directly (no collapse needed, RIB-OUT identical to group)
  for (const auto& peer : dfpPeers) {
    auto bit = peer->getGroupBitPosition();
    peer->clearAdjRibFlag(AdjRib::IS_DETACHED_FAST_PEER);
    peer->deactivateDetachedModeProcessing();
    /*
     * Reset block info on rejoin rather than on detach activation, because
     * block info is used for frequency-based slow peer detection and should
     * only be reset when the peer rejoins the group with a fresh start.
     */
    peer->resetPeerBlockInfo();
    XLOGF(
        DBG1,
        "Group {}: Peer {} at bit {} State Transition: {} (DFP) -> {}",
        groupDescriptor_,
        peer->getPeerName(),
        bit,
        peer->getPeerState(),
        PeerUpdateState::JOINED_RUNNING);
    peer->setPeerState(PeerUpdateState::JOINED_RUNNING);
    peer->incrementTimesRejoined();
    XLOGF(
        INFO,
        "Group {}: Peer {} at bit {} successfully rejoined group",
        groupDescriptor_,
        peer->getPeerName(),
        bit);
    markPeerInSync(peer);
  }

  // Accept DSP peers via collapse verification
  if (!dspCandidates.empty()) {
    tryAcceptPeersToGroup(dspCandidates);
  }
}

/*
 * Called by a DSP peer to proactively trigger its own rejoin.
 */
void AdjRibOutGroup::maybeAcceptDSPPeer(
    const std::shared_ptr<AdjRib>& adjRib) noexcept {
  if (FOLLY_UNLIKELY(adjRib->testOnlyDeferDrjAcceptance)) {
    return;
  }
  /* Revalidate at the final production acceptance boundary. */
  if (!adjRib->isReadyToRejoinGroup()) {
    XLOGF(
        DBG2,
        "Group {}: deferring DSP rejoin of peer {} - readiness conditions not met",
        groupDescriptor_,
        adjRib->getPeerName());
    return;
  }
  auto bit = adjRib->getGroupBitPosition();
  XLOGF(
      INFO,
      "Group {}: Attempting to rejoin group from peer {} at bit {}",
      groupDescriptor_,
      adjRib->getPeerName(),
      bit);
  if (tryAcceptPeerToGroup(adjRib)) {
    // Ensure the group keeps consuming after the peer restores SYNC state.
    scheduleChangeListConsumeTimer();
  }
}

bool AdjRibOutGroup::tryAcceptPeerToGroup(
    const std::shared_ptr<AdjRib>& candidatePeer) noexcept {
  auto accepted = tryAcceptPeersToGroup({candidatePeer});
  return !accepted.empty();
}

void AdjRibOutGroup::testOnlySetDeferDrjAcceptance(
    const folly::IPAddress& peerAddr,
    bool defer) noexcept {
  for (const auto& [_, adjRib] : bitToAdjRibs_) {
    if (adjRib->getPeerAddress() == peerAddr) {
      adjRib->testOnlyDeferDrjAcceptance = defer;
      break;
    }
  }
  if (!defer) {
    checkAndAcceptReadyToJoinPeers();
  }
}

/*
 * Try to accept DSP candidate peers back into the group in batch.
 * Splits candidates into addPath and non-addPath, collapses RIB-OUT
 * entries in one tree walk per type. Matching peers are accepted
 * (JOINED_RUNNING); mismatched peers are set back to DETACHED_RUNNING.
 */
std::vector<std::shared_ptr<AdjRib>> AdjRibOutGroup::tryAcceptPeersToGroup(
    const std::vector<std::shared_ptr<AdjRib>>& candidatePeers) noexcept {
  /*
   * Split candidates into:
   *   pathPeers: sendAddPath peers whose entries live in AdjRibPathTree
   *   litePeers: non-addPath peers whose entries live in AdjRibLiteTree
   */
  std::vector<std::shared_ptr<AdjRib>> pathPeers;
  std::vector<std::shared_ptr<AdjRib>> litePeers;
  for (const auto& peer : candidatePeers) {
    XLOGF(
        INFO,
        "Group {}: Trying peer {} rejoin at bit {}",
        groupDescriptor_,
        peer->getPeerName(),
        peer->getGroupBitPosition());
    if (peer->sendAddPath()) {
      pathPeers.push_back(peer);
    } else {
      litePeers.push_back(peer);
    }
  }

  /*
   * Collapse per-peer RIB-OUT entries back into the group.
   * DSP entries are compared and corrections queued in the packing list.
   * Peers with discrepancies have RIB_OUT_DISCREPANCY flag set.
   */
  auto groupOwnerKey = getGroupOwnerKey();
  if (!pathPeers.empty()) {
    collapsePathEntries(groupOwnerKey, pathPeers);
  }
  if (!litePeers.empty()) {
    collapseLiteEntries(groupOwnerKey, litePeers);
  }

  // Finalize each peer
  std::vector<std::shared_ptr<AdjRib>> acceptedPeers;
  for (const auto& peer : candidatePeers) {
    auto bit = peer->getGroupBitPosition();

    if (peer->isAdjRibFlagSet(AdjRib::RIB_OUT_DISCREPANCY)) {
      XLOGF(
          ERR,
          "Group {}: Peer {} at bit {} has entry discrepancies, "
          "not rejoining group",
          groupDescriptor_,
          peer->getPeerName(),
          bit);
      // TODO: bump stats/counters for rejoin discrepancy
      XLOGF(
          DBG1,
          "Group {}: Peer {} at bit {} State Transition: {} (DSP) -> {}",
          groupDescriptor_,
          peer->getPeerName(),
          bit,
          peer->getPeerState(),
          PeerUpdateState::DETACHED_RUNNING);
      peer->setPeerState(PeerUpdateState::DETACHED_RUNNING);
      /*
       * Update detachedRibVersion to the group's current version so that
       * the next collapse attempt only flags entries newer than this point.
       * Without this, the same group-only entries keep being detected as
       * discrepancies on every rejoin attempt (infinite loop).
       *
       * If the peer had no non-zero version yet (a never-in-sync peer that was
       * not counted in numPeersDetachedAfterJoin_), giving it one makes it a
       * counted detached-after-join peer, so increment now to stay balanced
       * against the decrement its eventual rejoin/teardown will perform (both
       * are guarded on detachedRibVersion > 0). Skipped when it already had a
       * version -- it was already counted.
       */
      if (peer->getDetachedRibVersion() == 0) {
        incrementPeersDetachedAfterJoin();
      }
      peer->setDetachedRibVersion(lastSeenRibVersion_);
      peer->reschedulePackingTimers();
      peer->clearAdjRibFlag(AdjRib::RIB_OUT_DISCREPANCY);
      /*
       * After updating the detachedRibVersion and collapsing the peer entries,
       * the peer is sharing entries with the group, and therefore cannot
       * be represented by the DETACHED_ON_REGISTRATION flag.
       */
      peer->clearAdjRibFlag(AdjRib::DETACHED_ON_REGISTRATION);
      continue;
    }

    peer->deactivateDetachedModeProcessing();
    /*
     * Reset block info on rejoin rather than on detach activation, because
     * block info is used for frequency-based slow peer detection and should
     * only be reset when the peer rejoins the group with a fresh start.
     */
    peer->resetPeerBlockInfo();
    XLOGF(
        DBG1,
        "Group {}: Peer {} at bit {} State Transition: {} (DSP) -> {}",
        groupDescriptor_,
        peer->getPeerName(),
        bit,
        peer->getPeerState(),
        PeerUpdateState::JOINED_RUNNING);
    peer->setPeerState(PeerUpdateState::JOINED_RUNNING);
    peer->incrementTimesRejoined();
    XLOGF(
        INFO,
        "Group {}: Peer {} at bit {} successfully rejoined group",
        groupDescriptor_,
        peer->getPeerName(),
        bit);
    acceptedPeers.push_back(peer);
  }

  /*
   * Mark accepted peers in sync after finalization (markPeerInSync erases from
   * detachedPeers_, so do it outside any detachedPeers_ iteration).
   */
  for (const auto& peer : acceptedPeers) {
    markPeerInSync(peer);
  }
  return acceptedPeers;
}

/*
 * Deep-copy the group's current packing list to the peer on detachment.
 */
void AdjRibOutGroup::clonePackingListForPeer(
    const std::shared_ptr<AdjRib>& adjRib) noexcept {
  // Copy the group's packing list to the peer
  adjRib->setDetachedPackingList(attrToPrefixMap_);

  XLOGF(
      DBG2,
      "Group {}: Cloned packing list ({} attr entries) for peer {} at bit {}",
      groupDescriptor_,
      attrToPrefixMap_.size(),
      adjRib->getPeerName(),
      adjRib->getGroupBitPosition());
}

bool AdjRibOutGroup::shouldClonePathForPeer(
    const AdjRibPathTree::Iterator& radixNodeItr,
    const uint32_t pathId,
    const std::shared_ptr<AdjRib>& peer,
    const uint64_t groupEntryRibVersion) noexcept {
  auto peerOwnerKey = peer->getPeerOwnerKey();

  /*
   * Clone only when the peer has no entry of its own here (Case 1) and the
   * group entry predates its detach (Case 2); otherwise the peer either
   * already advertises its own or never saw this one.
   */
  const bool peerHasOwnEntry =
      getAdjRibEntryFromPathNodeItr(radixNodeItr, peerOwnerKey, pathId) !=
      nullptr;
  return isEntryShared(
      peerHasOwnEntry, peer->getDetachedRibVersion(), groupEntryRibVersion);
}

bool AdjRibOutGroup::shouldCloneLiteForPeer(
    const AdjRibLiteTree::Iterator& radixNodeItr,
    const std::shared_ptr<AdjRib>& peer,
    const uint64_t groupEntryRibVersion) noexcept {
  auto peerOwnerKey = peer->getPeerOwnerKey();

  /*
   * Clone only when the peer has no entry of its own here (Case 1) and the
   * group entry predates its detach (Case 2); otherwise the peer either
   * already advertises its own or never saw this one.
   */
  const bool peerHasOwnEntry =
      getAdjRibEntryFromLiteNodeItr(radixNodeItr, peerOwnerKey) != nullptr;
  return isEntryShared(
      peerHasOwnEntry, peer->getDetachedRibVersion(), groupEntryRibVersion);
}

AdjRibOutGroup::PathOwnerMap& AdjRibOutGroup::getOrCreatePathOwnerMap(
    const folly::CIDRNetwork& prefix) noexcept {
  auto radixNodeItr = getRadixNodeItrFromPathTree(PathTree_, prefix);
  if (radixNodeItr.atEnd()) {
    radixNodeItr =
        PathTree_.insert(prefix.first, prefix.second, PathOwnerMap{}).first;
  }
  return radixNodeItr.value();
}

AdjRibOutGroup::LiteOwnerMap& AdjRibOutGroup::getOrCreateLiteOwnerMap(
    const folly::CIDRNetwork& prefix) noexcept {
  auto radixNodeItr = getRadixNodeItrFromLiteTree(LiteTree_, prefix);
  if (radixNodeItr.atEnd()) {
    radixNodeItr =
        LiteTree_.insert(prefix.first, prefix.second, LiteOwnerMap{}).first;
  }
  return radixNodeItr.value();
}

AdjRibEntry* AdjRibOutGroup::copyEntryForOwner(
    PathOwnerMap& ownerMap,
    const AdjRibOutOwnerKey& effectiveOwnerKey,
    const AdjRibEntry* entryToCopy) noexcept {
  const auto pathId = entryToCopy->getPathId();
  auto newEntry = std::make_unique<AdjRibEntry>(pathId);
  auto* result = newEntry.get();

  /*
   * Strip RIB-IN-only add-path GR marker bits: a RIB-OUT clone must never
   * inherit old-path-id ownership or a pending op from the source entry.
   */
  newEntry->flags_ = entryToCopy->flags_ & ~AdjRibEntry::kRibInOnlyFlagsMask;
  newEntry->setPreOut(entryToCopy->getPreOut());
  newEntry->setPostAttr(entryToCopy->getPostAttr());
  if (entryToCopy->getPostOutPolicy()) {
    newEntry->setPostOutPolicy(*entryToCopy->getPostOutPolicy());
  }
  newEntry->setRibVersion(entryToCopy->getRibVersion());

  ownerMap[effectiveOwnerKey][pathId] = std::move(newEntry);
  return result;
}

AdjRibEntry* AdjRibOutGroup::copyEntryForOwner(
    LiteOwnerMap& ownerMap,
    const AdjRibOutOwnerKey& effectiveOwnerKey,
    const AdjRibEntry* entryToCopy) noexcept {
  const auto pathId = entryToCopy->getPathId();
  auto newEntry = std::make_unique<AdjRibEntry>(pathId);
  auto* result = newEntry.get();

  // Strip RIB-IN-only add-path GR marker bits: a RIB-OUT clone must never
  // inherit old-path-id ownership or a pending op from the source entry.
  newEntry->flags_ = entryToCopy->flags_ & ~AdjRibEntry::kRibInOnlyFlagsMask;
  newEntry->setPreOut(entryToCopy->getPreOut());
  newEntry->setPostAttr(entryToCopy->getPostAttr());
  if (entryToCopy->getPostOutPolicy()) {
    newEntry->setPostOutPolicy(*entryToCopy->getPostOutPolicy());
  }
  newEntry->setRibVersion(entryToCopy->getRibVersion());

  ownerMap[effectiveOwnerKey] = std::move(newEntry);
  return result;
}

void AdjRibOutGroup::handleNoSyncPeers() noexcept {
  XLOGF(
      INFO,
      "Group {}: No SYNC peers remaining, {} detached peers in group",
      groupDescriptor_,
      detachedPeers_.size());

  /* Without any SYNC peers, stop distribution and discard the pending PL. */
  clearPackingList();

  cancelChangeListConsumeTimer();

  /*
   * Promote any immediately-viable peers which have drained their packing list
   * and are at the same marker as the group.
   */
  checkAndAcceptReadyToJoinPeers();
  if (numInSyncPeers_ > 0) {
    scheduleChangeListConsumeTimer();
    return;
  }

  /*
   * No peer was immediately promotable. Now we may have peers behind
   * and ahead of the group marker on the changelist.
   *
   * We prefer to let peers behind the group marker to be promoted
   * because they are likelier to share the same view of the RIB-OUT
   * as the group.
   *
   * A sharing DSP cannot be accepted at an intermediate marker. Keep the group
   * consumer running so both consumers can reach the end before rejoining.
   */
  if (numPeersDetachedAfterJoin_ > 0) {
    XLOGF(
        INFO,
        "Group {}: Continuing group consume timer for detached peers sharing group entries",
        groupDescriptor_);
    scheduleChangeListConsumeTimer();
  } else {
    /*
     * We only have peers ahead of the group on the changelist (DEP-A)
     * which are guaranteed to have their own per peer entries. They may
     * have a different RIB-OUT from the group.
     *
     * Promote one DEP-A that has finished draining (DETACHED_READY_TO_JOIN);
     * if none are ready, no group consumer is scheduled. The first DEP-A to
     * finish draining promotes itself and restarts group consumption.
     */
    for (const auto& peer : detachedPeers_) {
      if (peer->getPeerState() == PeerUpdateState::DETACHED_READY_TO_JOIN) {
        promoteDetachedPeerToSync(peer);
        scheduleChangeListConsumeTimer();
        return;
      }
    }
    XLOGF(
        INFO,
        "Group {}: Paused group consume timer, waiting for a detached peer ahead of the group to finish draining and promote itself",
        groupDescriptor_);
  }
}

void AdjRibOutGroup::recoverIfNoSyncPeers() noexcept {
  if (getMemberCount() > 0 && numInSyncPeers_ == 0) {
    handleNoSyncPeers();
  }
}

void AdjRibOutGroup::promoteDetachedPeerToSync(
    std::shared_ptr<AdjRib> adjRib) noexcept {
  XLOGF(
      INFO,
      "Group {}: Promoting detached peer {} at bit {} to SYNC",
      groupDescriptor_,
      adjRib->getPeerName(),
      adjRib->getGroupBitPosition());

  /*
   * 1. Capture the peer's change list consumer before mutating any state. The
   * group must rejoin the changelist at the peer's CL position; without it we
   * cannot adopt that position, so abort before moving any RIB-OUT entries
   * rather than leaving the group diverged. Step 5
   * (deactivateDetachedModeProcessing) later resets it.
   */
  auto peerConsumer = adjRib->getChangeListConsumer();
  if (!peerConsumer) {
    XLOGF(
        ERR,
        "Group {}: Cannot promote peer {} to SYNC: peer has no CL consumer; aborting before moving RIB-OUT entries",
        groupDescriptor_,
        adjRib->getPeerName());
    // TODO: bump a failed-promotion error stat (number of failed promotions).
    return;
  }

  if (!changeListTracker_ || !addPathConsumerBitmap_ ||
      !nonAddPathConsumerBitmap_) {
    XLOGF(
        ERR,
        "Group {}: Cannot promote peer {} to SYNC: tracker/bitmaps not set; aborting before moving RIB-OUT entries",
        groupDescriptor_,
        adjRib->getPeerName());
    // TODO: bump a failed-promotion error stat (number of failed promotions).
    return;
  }

  /*
   * 2. Reset the group's change list consumer and re-create it joined at the
   * promoted peer's CL position so the group adopts the peer's advanced
   * position.
   */
  resetChangeListConsumer();
  registerGroupConsumer();
  changeListTracker_->joinConsumer(peerConsumer, changeListConsumer_);

  /*
   * 3. Promote the peer's RIB-OUT entries to the group owner key. The peer's
   * diverged view becomes the new group truth.
   */
  if (adjRib->sendAddPath()) {
    promoteDetachedPeerPathEntries(adjRib);
  } else {
    promoteDetachedPeerLiteEntries(adjRib);
  }

  /*
   * 4. Adopt the promoted peer's RIB version as the group's cached version
   * (read before markPeerInSync, after which the peer reads the group's).
   */
  setLastSeenRibVersion(adjRib->getLastSeenRibVersion());

  /* 5. Tear down the peer's detached-mode processing (resets its CL consumer).
   */
  adjRib->deactivateDetachedModeProcessing();

  /*
   * 6. The promoted peer's RIB-OUT is now the group's source of truth, so adopt
   * its egress prefix counts into the group's stats_ (the group's prior counts
   * are stale -- it had no in-sync peers). The global totalSentPrefixCount is
   * left unchanged: promotion advertises/withdraws nothing and the peer's
   * contribution is already counted. markPeerInSync (below) then clears the
   * peer's snapshot counts.
   */
  stats_.copyEgressPrefixCountsFrom(adjRib->getStats());

  /* 7. Mark the peer in sync (sets sync bit, removes it from detachedPeers_).
   */
  adjRib->setPeerState(PeerUpdateState::JOINED_RUNNING);
  adjRib->incrementTimesRejoined();
  markPeerInSync(adjRib);
}

/*
 * Promote a detached peer's RIB-OUT entries to group entries in the PathTree.
 * For each prefix node: the existing group entry (if any) is deleted, then the
 * peer's entry, if present, is re-keyed under the group owner key — the
 * detached peer's diverged view becomes the new group truth. Empty nodes are
 * cleaned up after the walk. Used for add-path peers (groupKey_.sendAddPath).
 */
void AdjRibOutGroup::promoteDetachedPeerPathEntries(
    const std::shared_ptr<AdjRib>& adjRib) noexcept {
  auto peerOwnerKey = adjRib->getPeerOwnerKey();
  auto groupOwnerKey = getGroupOwnerKey();
  std::vector<AdjRibPathTree::Iterator> emptyNodes;
  size_t promotedCount = 0;

  for (auto itr = PathTree_.begin(); itr != PathTree_.end(); ++itr) {
    auto& ownerMap = itr->value();
    auto peerIt = ownerMap.find(peerOwnerKey);
    if (peerIt != ownerMap.end()) {
      /*
       * Take ownership of the peer's path map before any erase: F14 may
       * relocate other elements on erase/insert, so we must not hold a
       * reference into the map across those operations.
       */
      auto promoted = std::move(peerIt->second);
      promotedCount += promoted.size();
      ownerMap.erase(peerOwnerKey);
      ownerMap.erase(groupOwnerKey);
      ownerMap[groupOwnerKey] = std::move(promoted);
    } else {
      ownerMap.erase(groupOwnerKey);
    }
    if (ownerMap.empty()) {
      emptyNodes.push_back(itr);
    }
  }

  for (auto& itr : emptyNodes) {
    PathTree_.erase(itr);
  }

  XLOGF(
      INFO,
      "Group {}: Promoted {} PathTree entries from detached peer {} at bit {}",
      groupDescriptor_,
      promotedCount,
      adjRib->getPeerName(),
      adjRib->getGroupBitPosition());
}

/*
 * Promote a detached peer's RIB-OUT entries to group entries in the LiteTree.
 * For each prefix node: the existing group entry (if any) is deleted, then the
 * peer's entry, if present, is re-keyed under the group owner key — the
 * detached peer's diverged view becomes the new group truth. Empty nodes are
 * cleaned up after the walk. Used for non-addPath peers
 * (!groupKey_.sendAddPath).
 */
void AdjRibOutGroup::promoteDetachedPeerLiteEntries(
    const std::shared_ptr<AdjRib>& adjRib) noexcept {
  auto peerOwnerKey = adjRib->getPeerOwnerKey();
  auto groupOwnerKey = getGroupOwnerKey();
  std::vector<AdjRibLiteTree::Iterator> emptyNodes;
  size_t promotedCount = 0;

  for (auto itr = LiteTree_.begin(); itr != LiteTree_.end(); ++itr) {
    auto& ownerMap = itr->value();
    auto peerIt = ownerMap.find(peerOwnerKey);
    if (peerIt != ownerMap.end()) {
      /*
       * Take ownership of the peer's entry before any erase: F14 may
       * relocate other elements on erase/insert, so we must not hold a
       * reference into the map across those operations.
       */
      auto promoted = std::move(peerIt->second);
      ownerMap.erase(peerOwnerKey);
      ownerMap.erase(groupOwnerKey);
      ownerMap[groupOwnerKey] = std::move(promoted);
      ++promotedCount;
    } else {
      ownerMap.erase(groupOwnerKey);
    }
    if (ownerMap.empty()) {
      emptyNodes.push_back(itr);
    }
  }

  for (auto& itr : emptyNodes) {
    LiteTree_.erase(itr);
  }

  XLOGF(
      INFO,
      "Group {}: Promoted {} LiteTree entries from detached peer {} at bit {}",
      groupDescriptor_,
      promotedCount,
      adjRib->getPeerName(),
      adjRib->getGroupBitPosition());
}

void AdjRibOutGroup::lazyClonePathForDetachedPeers(
    const folly::CIDRNetwork& prefix,
    uint32_t pathId,
    AdjRibPathTree::Iterator& radixNodeItr,
    const AdjRibEntry* groupEntry) noexcept {
  for (const auto& adjRib : detachedPeers_) {
    if (shouldClonePathForPeer(
            radixNodeItr, pathId, adjRib, groupEntry->getRibVersion())) {
      copyEntryForOwner(
          radixNodeItr.value(), adjRib->getPeerOwnerKey(), groupEntry);
      XLOGF(
          DBG3,
          "Group {}: Cloned path entry for {} to peer {} at bit {}",
          groupDescriptor_,
          folly::IPAddress::networkToString(prefix),
          adjRib->getPeerName(),
          adjRib->getGroupBitPosition());
    }
  }
}

void AdjRibOutGroup::lazyCloneLiteForDetachedPeers(
    const folly::CIDRNetwork& prefix,
    AdjRibLiteTree::Iterator& radixNodeItr,
    const AdjRibEntry* groupEntry) noexcept {
  for (const auto& adjRib : detachedPeers_) {
    if (shouldCloneLiteForPeer(
            radixNodeItr, adjRib, groupEntry->getRibVersion())) {
      copyEntryForOwner(
          radixNodeItr.value(), adjRib->getPeerOwnerKey(), groupEntry);
      XLOGF(
          DBG3,
          "Group {}: Cloned lite entry for {} to peer {} at bit {}",
          groupDescriptor_,
          folly::IPAddress::networkToString(prefix),
          adjRib->getPeerName(),
          adjRib->getGroupBitPosition());
    }
  }
}

bool AdjRibOutGroup::collapsePathEntry(
    PathOwnerMap& ownerMap,
    const AdjRibOutOwnerKey& groupOwnerKey,
    const folly::CIDRNetwork& prefix,
    const std::shared_ptr<AdjRib>& peer) {
  auto peerOwnerKey = peer->getPeerOwnerKey();
  auto groupIt = ownerMap.find(groupOwnerKey);
  auto peerIt = ownerMap.find(peerOwnerKey);

  if (peerIt != ownerMap.end() && groupIt == ownerMap.end()) {
    // Peer-only entries: collapse and queue withdrawal for each pathId
    for (const auto& [pathId, peerEntry] : peerIt->second) {
      auto prefixPathId = std::make_pair(prefix, pathId);
      XLOGF_EVERY_MS(
          ERR,
          10000,
          "Group {}: collapse path withdrawal for peer {} prefix {} pathId {}",
          groupDescriptor_,
          peer->getPeerName(),
          folly::IPAddress::networkToString(prefix),
          pathId);
      peer->tryUpdateAttrToPrefixMap(
          prefixPathId, peerEntry->getPostAttr(), nullptr);
      if (peerEntry->getPostAttr()) {
        peer->decrementPostOutPrefixCount(prefix.first.isV4());
      }
      peer->decrementPreOutPrefixCount(prefix.first.isV4());
    }
    ownerMap.erase(peerOwnerKey);
    return true;
  } else if (peerIt == ownerMap.end() && groupIt != ownerMap.end()) {
    /*
     * Group-only entries: queue announcements for post-detach pathIds.
     * For init dump peers (DETACHED_ON_REGISTRATION), skip the rib version
     * check since they have no meaningful detachedRibVersion — they need all
     * entries.
     */
    bool isDetachedOnRegistration =
        peer->isAdjRibFlagSet(AdjRib::DETACHED_ON_REGISTRATION);
    bool hasDiscrepancy = isDetachedOnRegistration;
    for (const auto& [pathId, groupEntry] : groupIt->second) {
      if (isDetachedOnRegistration ||
          groupEntry->getRibVersion() > peer->getDetachedRibVersion()) {
        /* Peer never saw this entry, queue announcement (discrepancy found). */
        auto prefixPathId = std::make_pair(prefix, pathId);
        if (isDetachedOnRegistration) {
          XLOGF_EVERY_MS(
              ERR,
              10000,
              "Group {}: collapse path announcement for peer {} prefix {} "
              "pathId {} (init dump peer, no meaningful detachedRibVersion)",
              groupDescriptor_,
              peer->getPeerName(),
              folly::IPAddress::networkToString(prefix),
              pathId);
        } else {
          XLOGF_EVERY_MS(
              ERR,
              10000,
              "Group {}: collapse path announcement for peer {} prefix {} "
              "pathId {} (ribVersion {} > detachedVersion {})",
              groupDescriptor_,
              peer->getPeerName(),
              folly::IPAddress::networkToString(prefix),
              pathId,
              groupEntry->getRibVersion(),
              peer->getDetachedRibVersion());
        }
        peer->tryUpdateAttrToPrefixMap(
            prefixPathId, nullptr, groupEntry->getPostAttr());
        if (groupEntry->getPostAttr()) {
          peer->incrementPostOutPrefixCount(prefix.first.isV4());
        }
        peer->incrementPreOutPrefixCount(prefix.first.isV4());
        hasDiscrepancy = true;
      }
    }
    /*
     * Returns true if there is a discrepancy, indicating the RIB-OUT between
     * group and peer does NOT match for this case, where peer entry is missing
     * and group entry exists.
     *
     * Some special notes:
     * For DETACHED_INIT_DUMP peers whose RIB dump entries are being collapsed,
     * these peers do not have 'implicit' entries because they are rejoining the
     * group from a full RIB walk and MUST have their own entries. Hence, in
     * this block of if-else where peer entry is missing,
     * hasDiscrepancy is ALWAYS true (discrepancy found).
     *
     * For non DETACHED_INIT_DUMP peers that naturally detached after running
     * with the group, they can have implicit entries (i.e. sharing an entry
     * with the group) if the group entry ribVersion <= peer detachedRibVersion.
     */
    return hasDiscrepancy;
  } else if (peerIt != ownerMap.end() && groupIt != ownerMap.end()) {
    // Both have entries: collapse peer, queue corrections for mismatches
    bool hasDiscrepancy = false;
    for (const auto& [pathId, peerEntry] : peerIt->second) {
      auto groupPidIt = groupIt->second.find(pathId);
      if (groupPidIt == groupIt->second.end()) {
        // Peer pathId not in group: queue withdrawal
        auto prefixPathId = std::make_pair(prefix, pathId);
        XLOGF_EVERY_MS(
            ERR,
            10000,
            "Group {}: collapse path withdrawal for peer {} prefix {} pathId {}",
            groupDescriptor_,
            peer->getPeerName(),
            folly::IPAddress::networkToString(prefix),
            pathId);
        peer->tryUpdateAttrToPrefixMap(
            prefixPathId, peerEntry->getPostAttr(), nullptr);
        if (peerEntry->getPostAttr()) {
          peer->decrementPostOutPrefixCount(prefix.first.isV4());
        }
        peer->decrementPreOutPrefixCount(prefix.first.isV4());
        hasDiscrepancy = true;
      } else if (!peerEntry->hasMatchingPostPolicyAttrs(
                     groupPidIt->second.get())) {
        // Attrs mismatch: queue re-advertisement with group's attrs
        auto prefixPathId = std::make_pair(prefix, pathId);
        XLOGF_EVERY_MS(
            ERR,
            10000,
            "Group {}: collapse path re-advertise for peer {} prefix {} pathId {}",
            groupDescriptor_,
            peer->getPeerName(),
            folly::IPAddress::networkToString(prefix),
            pathId);
        peer->tryUpdateAttrToPrefixMap(
            prefixPathId, nullptr, groupPidIt->second->getPostAttr());
        hasDiscrepancy = true;
      }
    }
    // Group pathIds that peer doesn't have: queue announcements
    for (const auto& [pathId, groupEntry] : groupIt->second) {
      if (peerIt->second.find(pathId) == peerIt->second.end()) {
        auto prefixPathId = std::make_pair(prefix, pathId);
        XLOGF_EVERY_MS(
            ERR,
            10000,
            "Group {}: collapse path announcement for peer {} prefix {} pathId {}",
            groupDescriptor_,
            peer->getPeerName(),
            folly::IPAddress::networkToString(prefix),
            pathId);
        peer->tryUpdateAttrToPrefixMap(
            prefixPathId, nullptr, groupEntry->getPostAttr());
        if (groupEntry->getPostAttr()) {
          peer->incrementPostOutPrefixCount(prefix.first.isV4());
        }
        peer->incrementPreOutPrefixCount(prefix.first.isV4());
        hasDiscrepancy = true;
      }
    }
    ownerMap.erase(peerOwnerKey);
    return hasDiscrepancy;
  }
  /*
   * Neither peer nor group has entry at this node, which is equivalent
   * to having the same RIB-OUT — no discrepancy. This is reachable
   * because we iterate the entire radix tree for a batch of peers.
   * For example, collapsePathEntries({peer1, peer2}) walks every prefix
   * and calls collapsePathEntry for each peer. If peer1 has an entry
   * but peer2 and the group don't, peer1 gets RIB_OUT_DISCREPANCY
   * while peer2 falls through here (matching the group's absent state).
   */
  return false;
}

void AdjRibOutGroup::collapsePathEntries(
    const AdjRibOutOwnerKey& groupOwnerKey,
    const std::vector<std::shared_ptr<AdjRib>>& peers) {
  std::vector<AdjRibPathTree::Iterator> emptyNodes;
  folly::F14FastMap<uint64_t, size_t> failedPrefixCount;

  for (auto itr = PathTree_.begin(); itr != PathTree_.end(); ++itr) {
    auto prefix = folly::CIDRNetwork{itr.ipAddress(), itr.masklen()};
    for (const auto& peer : peers) {
      if (collapsePathEntry(itr->value(), groupOwnerKey, prefix, peer)) {
        peer->setAdjRibFlag(AdjRib::RIB_OUT_DISCREPANCY);
        incrTotalDiscrepancies();
        ++failedPrefixCount[peer->getGroupBitPosition()];
      }
    }
    if (itr->value().empty()) {
      emptyNodes.push_back(itr);
    }
  }

  for (const auto& [bit, count] : failedPrefixCount) {
    auto it = bitToAdjRibs_.find(bit);
    if (it == bitToAdjRibs_.end()) {
      continue;
    }
    XLOGF(
        ERR,
        "Group {}: collapse completed for peer {} (bit {}) "
        "with {}/{} prefixes in discrepancy",
        groupDescriptor_,
        it->second->getPeerName(),
        bit,
        count,
        PathTree_.size());
  }

  for (auto& itr : emptyNodes) {
    PathTree_.erase(itr);
  }
}

bool AdjRibOutGroup::collapseLiteEntry(
    LiteOwnerMap& ownerMap,
    const AdjRibOutOwnerKey& groupOwnerKey,
    const folly::CIDRNetwork& prefix,
    const std::shared_ptr<AdjRib>& peer) {
  auto peerOwnerKey = peer->getPeerOwnerKey();
  auto groupIt = ownerMap.find(groupOwnerKey);
  auto peerIt = ownerMap.find(peerOwnerKey);

  if (peerIt != ownerMap.end() && groupIt == ownerMap.end()) {
    // Peer-only entry: collapse and queue withdrawal
    XLOGF_EVERY_MS(
        ERR,
        10000,
        "Group {}: collapse discrepancy for peer {} prefix {}: "
        "peer-only entry (no group entry), queuing withdrawal",
        groupDescriptor_,
        peer->getPeerName(),
        folly::IPAddress::networkToString(prefix));
    auto prefixPathId = std::make_pair(prefix, peerIt->second->getPathId());
    peer->tryUpdateAttrToPrefixMap(
        prefixPathId, peerIt->second->getPostAttr(), nullptr);
    if (peerIt->second->getPostAttr()) {
      peer->decrementPostOutPrefixCount(prefix.first.isV4());
    }
    peer->decrementPreOutPrefixCount(prefix.first.isV4());
    ownerMap.erase(peerOwnerKey);
    return true;
  } else if (peerIt == ownerMap.end() && groupIt != ownerMap.end()) {
    /*
     * Group-only entry: for init dump peers (DETACHED_ON_REGISTRATION),
     * skip the rib version check since they have no meaningful
     * detachedRibVersion — they need all entries. For DSP peers, only
     * post-detach entries (ribVersion > detachedRibVersion) are discrepancies.
     */
    bool isDetachedOnRegistration =
        peer->isAdjRibFlagSet(AdjRib::DETACHED_ON_REGISTRATION);
    if (isDetachedOnRegistration ||
        groupIt->second->getRibVersion() > peer->getDetachedRibVersion()) {
      /* Peer never saw this entry, queue announcement (discrepancy found). */
      auto prefixPathId = std::make_pair(prefix, kPlaceholderPathID);
      if (isDetachedOnRegistration) {
        XLOGF_EVERY_MS(
            ERR,
            10000,
            "Group {}: collapse discrepancy for peer {} prefix {}: "
            "group-only entry (init dump peer, no meaningful detachedRibVersion), "
            "queuing announcement",
            groupDescriptor_,
            peer->getPeerName(),
            folly::IPAddress::networkToString(prefix));
      } else {
        XLOGF_EVERY_MS(
            ERR,
            10000,
            "Group {}: collapse discrepancy for peer {} prefix {}: "
            "group-only entry (ribVersion {} > detachedVersion {}), "
            "queuing announcement",
            groupDescriptor_,
            peer->getPeerName(),
            folly::IPAddress::networkToString(prefix),
            groupIt->second->getRibVersion(),
            peer->getDetachedRibVersion());
      }
      peer->tryUpdateAttrToPrefixMap(
          prefixPathId, nullptr, groupIt->second->getPostAttr());
      if (groupIt->second->getPostAttr()) {
        peer->incrementPostOutPrefixCount(prefix.first.isV4());
      }
      peer->incrementPreOutPrefixCount(prefix.first.isV4());
      return true; // discrepancy: correction queued
    }
    /*
     * Shared pre-detach entry: peer already has group's view, no correction
     * needed. Init dump peers will not reach this line — they always take
     * the branch above.
     */
    return false; // no discrepancy
  } else if (peerIt != ownerMap.end() && groupIt != ownerMap.end()) {
    if (peerIt->second->hasMatchingPostPolicyAttrs(groupIt->second.get())) {
      // Match: collapse peer entry, no packing list correction needed
      ownerMap.erase(peerOwnerKey);
      return false;
    }
    // Mismatch: collapse peer entry, queue re-advertisement with group's attrs
    auto prefixPathId = std::make_pair(prefix, kPlaceholderPathID);
    XLOGF_EVERY_MS(
        ERR,
        10000,
        "Group {}: collapse discrepancy for peer {} prefix {}: "
        "peer/group attrs mismatch, queuing re-advertisement",
        groupDescriptor_,
        peer->getPeerName(),
        folly::IPAddress::networkToString(prefix));
    peer->tryUpdateAttrToPrefixMap(
        prefixPathId, nullptr, groupIt->second->getPostAttr());
    ownerMap.erase(peerOwnerKey);
    return true;
  }
  /*
   * Neither peer nor group has entry at this node, which is equivalent
   * to having the same RIB-OUT — no discrepancy. This is reachable
   * because we iterate the entire radix tree for a batch of peers.
   * For example, collapseLiteEntries({peer1, peer2}) walks every prefix
   * and calls collapseLiteEntry for each peer. If peer1 has an entry
   * but peer2 and the group don't, peer1 gets RIB_OUT_DISCREPANCY
   * while peer2 falls through here (matching the group's absent state).
   */
  return false;
}

void AdjRibOutGroup::collapseLiteEntries(
    const AdjRibOutOwnerKey& groupOwnerKey,
    const std::vector<std::shared_ptr<AdjRib>>& peers) {
  std::vector<AdjRibLiteTree::Iterator> emptyNodes;
  folly::F14FastMap<uint64_t, size_t> failedPrefixCount;

  for (auto itr = LiteTree_.begin(); itr != LiteTree_.end(); ++itr) {
    auto prefix = folly::CIDRNetwork{itr.ipAddress(), itr.masklen()};
    for (const auto& peer : peers) {
      if (collapseLiteEntry(itr->value(), groupOwnerKey, prefix, peer)) {
        peer->setAdjRibFlag(AdjRib::RIB_OUT_DISCREPANCY);
        incrTotalDiscrepancies();
        ++failedPrefixCount[peer->getGroupBitPosition()];
      }
    }
    if (itr->value().empty()) {
      emptyNodes.push_back(itr);
    }
  }

  for (const auto& [bit, count] : failedPrefixCount) {
    auto it = bitToAdjRibs_.find(bit);
    if (it == bitToAdjRibs_.end()) {
      continue;
    }
    XLOGF(
        ERR,
        "Group {}: collapse completed for peer {} (bit {}) "
        "with {}/{} prefixes in discrepancy",
        groupDescriptor_,
        it->second->getPeerName(),
        bit,
        count,
        LiteTree_.size());
  }

  for (auto& itr : emptyNodes) {
    LiteTree_.erase(itr);
  }
}

} // namespace facebook::bgp
