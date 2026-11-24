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

#include <folly/coro/AsyncScope.h>

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

  // Identifies the routing table owned by this context.
  const VrfId vrfId_;
  PeerManagerBase& owner_;

  // The owner's scope, for coroutines scheduled on behalf of this table.
  [[maybe_unused]] folly::coro::CancellableAsyncScope& asyncScope_;

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
};

} // namespace facebook::bgp
