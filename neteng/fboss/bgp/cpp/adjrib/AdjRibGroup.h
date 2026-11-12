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

#include <fmt/format.h>
#include <folly/CancellationToken.h>
#include <folly/Function.h>
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/coro/AsyncScope.h>
#include <folly/logging/xlog.h>
#include <chrono>
#include <deque>
#include <memory>
#include <optional>

#include <folly/coro/Task.h>
#include <folly/io/IOBuf.h>

#include "fboss/lib/RadixTree.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibCommon.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibEntry.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibPolicyCache.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibStats.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibStructs.h"
#include "neteng/fboss/bgp/cpp/adjrib/ShadowRibTypes.h"
#include "neteng/fboss/bgp/cpp/changeTracker/ChangeTracker.h"
#include "neteng/fboss/bgp/cpp/changeTracker/Consumer.h"
#include "neteng/fboss/bgp/cpp/changeTracker/ConsumerBitManager.h"
#include "neteng/fboss/bgp/cpp/changeTracker/ConsumerBitmap.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/config/ConfigStructs.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyStructs.h"

// UpdateDescriptor definition available from BgpStructs.h
#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"
// FiberBgpPeer::InputMessageT type definition
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberBgpPeer.h"

namespace facebook::bgp {

class AdjRib;
class AdjRibOutConsumer;
class AdjRibOutGroupConsumer;
class PolicyManager;
struct AdjRibEntry;
struct PostPolicyInfo;

/**
 * Adjacency RIB Grouping has benefits for enabling memory and CPU improvements
 * However, implementation differences for the use of Group may differ for
 * received routes and to be advertised routes, and hence keep class definition
 * separate for In and Out adjacency groups
 *
 * Based on group selection criteria, each adjRib points a specific adjRibGroup
 * Thus, every AdjRibGroup instance is a collection of a set of adjRibs that
 * share a same result from group selection criteria
 *
 * adjRibGroup instances are created separate for received routes and to be
 * advertised routes
 *
 */
class AdjRibOutGroup : public std::enable_shared_from_this<AdjRibOutGroup> {
 public:
  explicit AdjRibOutGroup(
      folly::EventBase& evb,
      const std::string& groupName,
      uint64_t groupId = 0,
      bool enableUpdateGroup = false,
      const UpdateGroupKey& groupKey = UpdateGroupKey{},
      const ShadowRibView& shadowRib = ShadowRibView::empty(),
      std::shared_ptr<PolicyManager> policyManager = nullptr,
      const UpdateGroupConfig& updateGroupConfig = {})
      : evb_(evb),
        groupName_(groupName),
        groupId_(groupId),
        enableUpdateGroup_(enableUpdateGroup),
        groupKey_(groupKey),
        groupDescriptor_(buildGroupDescriptor(groupId, groupKey)),
        shadowRibEntries_(shadowRib.entries),
        maxRibVersion_(shadowRib.maxRibVersion),
        policyManager_(std::move(policyManager)),
        policyCache_(AdjRibPolicyCache::get()),
        updateGroupConfig_(updateGroupConfig),
        stats_(fmt::format("group.{}", groupName)) {}

  virtual ~AdjRibOutGroup();

  /*
   * @brief: cooperatively drain all pending coroutines on asyncScope_.
   * Must be co_awaited before the group is destroyed to avoid
   * blocking the EventBase thread in the destructor.
   * @return: Task<void> to be co_awaited by the caller.
   */
  folly::coro::Task<void> drainAsyncScope();

  AdjRibOutGroup(const AdjRibOutGroup&) = delete;
  AdjRibOutGroup& operator=(const AdjRibOutGroup&) = delete;
  AdjRibOutGroup(AdjRibOutGroup&&) = delete;
  AdjRibOutGroup& operator=(AdjRibOutGroup&&) = delete;

  // Reset the group's cumulative egress (sent) message counts.
  void clearEgressMessageCounts() {
    stats_.clearEgressMessageCounts();
  }

  const AdjRibStats& getStats() const {
    return stats_;
  }

  /**
   * @brief  Get group name of this AdjRibOutGroup
   */
  inline const std::string& getAdjRibGroupName() const {
    return groupName_;
  }

  /**
   * @brief  Get friendly group descriptor for logging:
   *         "groupId(policyName/afiLabel)"
   */
  inline const std::string& getGroupDescriptor() const {
    return groupDescriptor_;
  }

  /**
   * @brief  Get unique group ID
   */
  inline uint64_t getGroupId() const {
    return groupId_;
  }

  /**
   * @brief  Create an AdjRibOutOwnerKey for this group
   * This key can be used to store group-level entries in the RIB-OUT tree.
   */
  inline AdjRibOutOwnerKey getGroupOwnerKey() const {
    return AdjRibOutOwnerKey::forGroup(groupId_);
  }

  /*
   * Owner map types: map of owner key to AdjRibEntry (or path map of entries).
   * Owner key identifies whether an entry belongs to a peer or group.
   */
  using PathOwnerMap = folly::F14ValueMap<
      AdjRibOutOwnerKey,
      folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>>,
      AdjRibOutOwnerKeyHash>;

  using LiteOwnerMap = folly::F14ValueMap<
      AdjRibOutOwnerKey,
      std::unique_ptr<AdjRibEntry>,
      AdjRibOutOwnerKeyHash>;

  /**
   * The outer F14ValueMap definition here is map of path to <owner,
   * AdjRibEntry> The inner F14ValueMap definition here is map of owner to
   * AdjRibEntry Owner key explicitly identifies whether an entry belongs to a
   * peer or group In a long run, owner to AdjRibEntry mapping can go away, when
   * each owner can be made work with just one AdjRibEntry
   */
  using AdjRibPathTree =
      facebook::network::RadixTree<folly::IPAddress, PathOwnerMap>;

  /**
   * The F14ValueMap definition here is map of owner to AdjRibEntry
   * Owner key explicitly identifies whether an entry belongs to a peer or group
   * In a long run, owner to AdjRibEntry mapping can go away, when each owner
   * can be made work with just one AdjRibEntry
   */
  using AdjRibLiteTree =
      facebook::network::RadixTree<folly::IPAddress, LiteOwnerMap>;

  AdjRibPathTree PathTree_;
  AdjRibLiteTree LiteTree_;

  void deleteFromPathTree(
      AdjRibPathTree& pathTree,
      const folly::CIDRNetwork& prefix,
      const AdjRibOutOwnerKey& ownerKey,
      uint32_t pathId) noexcept;

  void deleteFromPathTree(
      AdjRibPathTree& pathTree,
      AdjRibPathTree::Iterator&& itr,
      const AdjRibOutOwnerKey& ownerKey,
      uint32_t pathId) noexcept;

  void deleteFromLiteTree(
      AdjRibLiteTree& liteTree,
      const folly::CIDRNetwork& prefix,
      const AdjRibOutOwnerKey& ownerKey) noexcept;

  void deleteFromLiteTree(
      AdjRibLiteTree& liteTree,
      AdjRibLiteTree::Iterator&& itr,
      const AdjRibOutOwnerKey& ownerKey) noexcept;

  AdjRibEntry* FOLLY_NONNULL addToPathTree(
      AdjRibPathTree& pathTree,
      const folly::CIDRNetwork& prefix,
      const AdjRibOutOwnerKey& ownerKey,
      uint32_t pathId) noexcept;

  AdjRibEntry* FOLLY_NONNULL addToLiteTree(
      AdjRibLiteTree& liteTree,
      const folly::CIDRNetwork& prefix,
      const AdjRibOutOwnerKey& ownerKey,
      uint32_t pathId) noexcept;

  AdjRibEntry* FOLLY_NULLABLE getFromPathTree(
      AdjRibPathTree& pathTree,
      const folly::CIDRNetwork& prefix,
      const AdjRibOutOwnerKey& ownerKey,
      uint32_t pathId) noexcept;

  /**
   * Add a peer entry into the group's RIB-OUT tree under the given owner key.
   */
  AdjRibEntry* FOLLY_NONNULL addRibEntry(
      const folly::CIDRNetwork& prefix,
      const AdjRibOutOwnerKey& ownerKey,
      uint32_t pathId) noexcept;

  /**
   * Whether a peer still shares a group-owned entry.
   *
   * Sharing requires both halves, and the caller must supply the first:
   *
   * - peerHasOwnEntry: the peer has its own entry at this (prefix, pathId). A
   *   diverged peer advertises from that entry, so the group's is not shared
   *   no matter what the versions say. Divergence is per (prefix, pathId), so
   *   one radix node can hold both peer-owned paths and still-shared group
   *   paths. Pass what you know: look the peer entry up if you have not
   *   already, and pass false when you have established its absence.
   *
   * - the version gate: when a peer detaches at rib version N, group entries
   *   at or below N existed while the peer was still attached, so the peer saw
   *   them. Entries above N were created or re-announced after the detach and
   *   were never advertised to the peer.
   */
  static bool isEntryShared(
      bool peerHasOwnEntry,
      uint64_t peerDetachedRibVersion,
      uint64_t groupEntryRibVersion) noexcept {
    return !peerHasOwnEntry && peerDetachedRibVersion >= groupEntryRibVersion;
  }

  /*
   * Look up an entry for a peer, trying the peer's owner key first, then
   * falling back to the group owner key. The returned entry may be a
   * lazy-cloned per-peer entry (owned by the peer) or a shared group entry.
   * The boolean indicates whether the entry is a per-peer entry (true) or
   * a group entry shared by the peer (false).
   */
  std::pair<AdjRibEntry * FOLLY_NULLABLE, bool> getRibEntrySharedOrPeer(
      const folly::CIDRNetwork& prefix,
      const AdjRibOutOwnerKey& peerOwnerKey,
      uint32_t pathId,
      uint64_t detachedRibVersion = 0) noexcept;

  /*
   * Return the peer-owned entry when present. If the peer is still sharing the
   * group entry, clone it into the already-located radix node and return the
   * clone so the caller can mutate it.
   */
  AdjRibEntry* FOLLY_NULLABLE getWritableRibEntryForPeer(
      const folly::CIDRNetwork& prefix,
      const AdjRibOutOwnerKey& peerOwnerKey,
      uint32_t pathId,
      uint64_t detachedRibVersion) noexcept;

  std::pair<AdjRibEntry * FOLLY_NULLABLE, bool> getRibPathEntrySharedOrPeer(
      const folly::CIDRNetwork& prefix,
      const AdjRibOutOwnerKey& peerOwnerKey,
      uint32_t pathId) noexcept;

  std::pair<AdjRibEntry * FOLLY_NULLABLE, bool> getRibLiteEntrySharedOrPeer(
      const folly::CIDRNetwork& prefix,
      const AdjRibOutOwnerKey& peerOwnerKey) noexcept;

  /*
   * Resolve the lite RIB-OUT entry visible to a peer at a single radix node,
   * applying the same version gate as getRibEntrySharedOrPeer but on an
   * already-iterated owner map (no per-prefix re-lookup). Returns:
   *   - the peer's own entry when present (diverged peer, or standalone /
   *     update-group disabled),
   *   - else, only when this group has update-group enabled, the shared group
   *     entry when the peer still shares it (in-sync, or detached with the
   *     group entry at/below its detach version),
   *   - else nullptr (update group disabled, or a post-detach group entry the
   *     peer never saw -> the "missing" per-peer entry is expected, not a bug).
   *
   * When enableUpdateGroup_ is false the group owner key is never consulted —
   * group-key entries only exist while update groups are enabled (writes go
   * under the peer key otherwise), so this keeps that intent explicit rather
   * than relying on the empty group key.
   *
   * sharingVersion: for a detached peer pass getDetachedRibVersion(); for a
   * non-detached peer pass the group's getLastSeenRibVersion() (shares all).
   */
  const AdjRibEntry* FOLLY_NULLABLE resolveLiteEntryForPeer(
      const LiteOwnerMap& ownerMap,
      const AdjRibOutOwnerKey& peerOwnerKey,
      uint64_t sharingVersion) const noexcept {
    auto peerItr = ownerMap.find(peerOwnerKey);
    if (peerItr != ownerMap.end()) {
      return peerItr->second.get();
    }
    if (!enableUpdateGroup_) {
      return nullptr;
    }
    auto groupItr = ownerMap.find(getGroupOwnerKey());
    if (groupItr != ownerMap.end() &&
        isEntryShared(
            /*peerHasOwnEntry=*/false,
            sharingVersion,
            groupItr->second->getRibVersion())) {
      return groupItr->second.get();
    }
    return nullptr;
  }

  /*
   * Path-tree counterpart of resolveLiteEntryForPeer: invoke
   * cb(uint32_t pathId, const AdjRibEntry& entry) for each path entry visible
   * to the peer at a single radix node. Peer-owned paths are emitted as-is;
   * only when this group has update-group enabled, a group-owned path is also
   * emitted when the peer does not already own that pathId and still shares the
   * group entry (version gate). Divergence is per (prefix, pathId), matching
   * getRibEntrySharedOrPeer.
   */
  template <typename Callback>
  void resolvePathEntriesForPeer(
      const PathOwnerMap& ownerMap,
      const AdjRibOutOwnerKey& peerOwnerKey,
      uint64_t sharingVersion,
      Callback&& cb) const {
    auto peerItr = ownerMap.find(peerOwnerKey);
    if (peerItr != ownerMap.end()) {
      for (const auto& [pathId, entry] : peerItr->second) {
        cb(pathId, *entry);
      }
    }
    if (!enableUpdateGroup_) {
      return;
    }
    auto groupItr = ownerMap.find(getGroupOwnerKey());
    if (groupItr == ownerMap.end()) {
      return;
    }
    for (const auto& [pathId, entry] : groupItr->second) {
      const bool peerHasOwnEntry = peerItr != ownerMap.end() &&
          peerItr->second.find(pathId) != peerItr->second.end();
      if (isEntryShared(
              peerHasOwnEntry, sharingVersion, entry->getRibVersion())) {
        cb(pathId, *entry);
      }
    }
  }

  AdjRibEntry* FOLLY_NULLABLE getAdjRibEntryFromPathNodeItr(
      const AdjRibPathTree::Iterator& itr,
      const AdjRibOutOwnerKey& ownerKey,
      uint32_t pathId) noexcept;

  AdjRibEntry* FOLLY_NULLABLE getFromLiteTree(
      AdjRibLiteTree& liteTree,
      const folly::CIDRNetwork& prefix,
      const AdjRibOutOwnerKey& ownerKey) noexcept;

  AdjRibEntry* FOLLY_NULLABLE getAdjRibEntryFromLiteNodeItr(
      const AdjRibLiteTree::Iterator& itr,
      const AdjRibOutOwnerKey& ownerKey) noexcept;

  AdjRibPathTree::Iterator getRadixNodeItrFromPathTree(
      AdjRibPathTree& pathTree,
      const folly::CIDRNetwork& prefix) noexcept;

  AdjRibLiteTree::Iterator getRadixNodeItrFromLiteTree(
      AdjRibLiteTree& liteTree,
      const folly::CIDRNetwork& prefix) noexcept;

  uint32_t getPeerEntriesCountFromPathTree(
      AdjRibPathTree& pathTree,
      const AdjRibOutOwnerKey& ownerKey) noexcept;

  uint32_t getPeerEntriesCountFromLiteTree(
      AdjRibLiteTree& liteTree,
      const AdjRibOutOwnerKey& ownerKey) noexcept;

  /*
   * Change list consumer activation/deactivation
   * Same pattern as AdjRib for consistency
   */
  void scheduleChangeListConsumeTimer() noexcept;

  /*
   * Cancel (without destroying) the group's change list consume timer. Change
   * list consumption remains inactive until
   * scheduleChangeListConsumeTimer() is called.
   */
  void cancelChangeListConsumeTimer() noexcept;

  /*
   * Create (if not already set) the group's change list consumer, register it
   * with the tracker, and create the consume timer. The consumer is created
   * lazily here — after the initial RIB dump — rather than at peer
   * establishment. Requires setChangeListTracker() to have wired the tracker
   * and bitmaps first.
   */
  void registerGroupConsumer() noexcept;
  void deactivateChangeListConsumer() noexcept;

  /*
   * Consume all available group change-list items. Reaching the tail means the
   * group has seen every item offered to it, so advance its cached version to
   * the Shadow RIB's current maximum. The last applicable change item's
   * version can be lower because not every RIB change is published or applies
   * to this consumer. If processFullMultipaths is set, every multipath of each
   * item is processed instead of only the delta.
   */
  void tryIterateChangesToEnd(bool processFullMultipaths = false) noexcept;

  /*
   * Create the group's change list consume (MRAI) timer for the currently set
   * changeListConsumer_. Shared by registerGroupConsumer() and the consumer
   * swap in promoteDetachedPeerToSync().
   */
  void createChangeListConsumeTimer() noexcept;

  void clearPackingList() {
    attrToPrefixMap_.clear();
  }

  /**
   * @brief Update packing list with attribute-to-prefix mapping for this group
   *
   * @details Wrapper around tryUpdateAttrToPrefixMapImpl() that automatically
   * provides the group's context (e.g. attrToPrefixMap_, stats_).
   *
   * @param prefixPathId - Prefix and path ID pair
   * @param oldPath - Previous attributes (nullptr if new prefix)
   * @param newPath - New attributes (nullptr for withdrawal)
   */
  inline void tryUpdateAttrToPrefixMapForGroup(
      const std::pair<folly::CIDRNetwork, uint32_t>& prefixPathId,
      const std::shared_ptr<const BgpPath>& oldPath,
      const std::shared_ptr<const BgpPath>& newPath,
      bool isNexthopSetByPolicy = false) {
    /*
     * The packing list is the group's send queue for its in-sync peers.
     *
     * This is still reached with zero in-sync peers: a group can have members
     * but nobody in sync (all detached or blocked) and yet still process
     * shadow-RIB entries -- e.g. an egress policy re-evaluation walk, or
     * consuming its change list -- to keep the group's RIB-OUT and consumer
     * marker current so detached peers can catch up against it. With no in-sync
     * peer to send to, skip updating the packing list.
     */
    if (numInSyncPeers_ == 0) {
      return;
    }
    tryUpdateAttrToPrefixMapImpl(
        prefixPathId,
        oldPath,
        newPath,
        attrToPrefixMap_,
        fmt::format("Group {}", groupDescriptor_),
        stats_,
        isNexthopSetByPolicy);
  }

  /*
   * Cancel MRAI timer if running
   * Used during group cleanup
   */
  void resetChangeListConsumeTimer() noexcept {
    cancelChangeListConsumeTimer();
    changeListConsumeTimer_.reset();
  }

  /*
   * State management
   */
  UpdateGroupState getState() const noexcept {
    return state_;
  }

  void setState(UpdateGroupState newState) noexcept {
    state_ = newState;
  }

  /*
   * Process changes to shadow RIB entries consumed from change tracker.
   * Called by AdjRibOutGroupConsumer when consuming change items.
   */
  void processShadowRibEntryChange(ShadowRibEntry& srEntry) noexcept;

  /*
   * Announce every multipath of the entry, not only the ones in update, and
   * withdraw the ones in withdraw.
   */
  void processShadowRibEntryFullMultipaths(ShadowRibEntry& srEntry) noexcept;

  /*
   * Process withdrawal for the group
   * Similar to AdjRib::processRibWithdraw but at group level
   */
  void processGroupRibWithdraw(
      const folly::CIDRNetwork& prefix,
      uint32_t pathId) noexcept;

  /*
   * Shared core: walk ShadowRib and process each entry through
   * processRibAnnouncedEntryForGroup() into the group packing list. Used by
   * both initial dump and policy re-evaluation.
   * This is not interruptible -- runs synchronously in a single
   * event loop turn with no co_await inside the loop.
   * @param sendWithEoR - whether to mark each in-sync peer's pending EoRs
   */
  void walkAndProcessShadowRib(bool sendWithEoR);

  /*
   * Build initial RIB dump from shadow RIB. Advances lastSeenRibVersion_.
   * @param sendWithEoR - true if the peer still has pending EoRs to send,
   *        false if the peer has already sent EoRs (e.g., group move).
   */
  void processRibDumpForGroup(bool sendWithEoR = true);

  /*
   * Move peers parked in INIT to JOINED_RUNNING at the group's current RIB
   * version. Runs after whichever walk stands in for the group's initial dump
   * -- the dump itself, or an inline policy re-evaluation of a group that
   * never dumped.
   */
  void transitionInitPeersToJoinedRunning() noexcept;

  /*
   * Build initial dump, transition INIT peers to JOINED_RUNNING,
   * activate the change list consumer, and schedule the consume timer.
   * state_ is expected to be UNINITIALIZED whenever this task starts;
   * cancellation handles a queued task whose group is superseded first.
   */
  folly::coro::Task<void> buildAndScheduleSendInitialDumpFromShadowRib();

  /*
   * Re-evaluate all ShadowRib entries with the current egress policy.
   * Called when the group's egress policy content changes, and for a group
   * that has never dumped, where this walk stands in for the initial dump.
   * Reuses walkAndProcessShadowRib(). Lazy clone (Phase 4) preserves detached
   * peers' old-policy state before group entries are mutated.
   *
   * Leaves the group WAITING. For the never-dumped case it stands in for the
   * initial dump and so does that dump's work as well: processes the RIB dump
   * by walking the whole shadow RIB into the packing list, marks the egress
   * EoR as owed by every in-sync peer, moves peers parked in INIT to
   * JOINED_RUNNING, and registers the change list consumer. See the
   * definition for why.
   */
  void reEvaluateSyncPeersEgressPolicy();

  /*
   * Whether a rib dump is currently scheduled or in flight for this group: a
   * cancellation source exists and has not been cancelled.
   */
  bool isRibDumpScheduled() const noexcept {
    return ribDumpCancellationSource_.has_value() &&
        !ribDumpCancellationSource_->isCancellationRequested();
  }

  /*
   * Cancel an in-flight rib dump -- the group was emptied or erased out from
   * under the walk, or the walk is being superseded -- and clear the source so
   * a subsequent dump can be scheduled. The in-flight task's token still
   * observes the cancellation (the underlying state outlives the source via
   * the token).
   */
  void cancelRibDump() noexcept {
    if (ribDumpCancellationSource_) {
      ribDumpCancellationSource_->requestCancellation();
      ribDumpCancellationSource_.reset();
    }
  }

  /*
   * Schedule initial dump to start asynchronously
   */
  void scheduleInitialDump() noexcept;

  /*
   * Test-only: hold the group's queued initial dump. While set, the dump task
   * re-queues itself instead of walking, so the group stays UNINITIALIZED with
   * a dump outstanding -- the window a policy split has to land in for the new
   * group to inherit UNINITIALIZED.
   */
  bool testOnlyDeferInitialDump{false};

  /*
   * Process individual announced entry for the group. Invoked per-entry by
   * the initial dump / re-eval walk (walkAndProcessShadowRib) and by
   * processShadowRibEntryChange for incremental change items.
   */
  void processRibAnnouncedEntryForGroup(
      const RibOutAnnouncementEntry& entry) noexcept;

  /*
   * Group-level canAnnounce check
   * Determines if a route can be announced based on group's session type
   * and RR client settings. Unlike peer-level canAnnounce, this skips
   * same-peer filtering (handled per-peer during distribution).
   */
  bool canAnnounceForGroup(const RibOutAnnouncementEntry& update) noexcept;

  /*
   * Get the attribute to prefix mapping (for testing)
   */
  const AttrToPrefixMap& getAttrToPrefixMap() const noexcept {
    return attrToPrefixMap_;
  }

  /*
   * Set the change list tracker and consumer bitmaps for detached peer
   * support. Called by PeerManagerBase when setting up the group's change list
   * consumer.
   */
  void setChangeListTracker(
      std::shared_ptr<ChangeTracker<ShadowRibEntry>>& tracker,
      ConsumerBitmap& addPathBitmap,
      ConsumerBitmap& nonAddPathBitmap) noexcept {
    changeListTracker_ = tracker;
    addPathConsumerBitmap_ = &addPathBitmap;
    nonAddPathConsumerBitmap_ = &nonAddPathBitmap;
  }

  const std::shared_ptr<ChangeTracker<ShadowRibEntry>>& getChangeListTracker()
      const noexcept {
    return changeListTracker_;
  }

  /*
   * Reset the changeListConsumer for this group
   */
  void resetChangeListConsumer() noexcept {
    if (changeListConsumer_) {
      deactivateChangeListConsumer();
      changeListConsumer_.reset();
    }
  }

  /*
   * Get saved changeListConsumer for this group
   */
  const std::shared_ptr<AdjRibOutGroupConsumer>& getChangeListConsumer()
      const noexcept {
    return changeListConsumer_;
  }

  /*
   * Try to insert or get RIB-OUT entry for the group
   * Similar to AdjRib::tryInsertRibOutEntry but uses group owner key
   * Made public for testing
   */
  AdjRibEntry* FOLLY_NULLABLE tryInsertRibOutEntry(
      const folly::CIDRNetwork& prefix,
      const folly::IPAddress& nexthop,
      const uint32_t pathIdToSend) noexcept;

  /*
   * Build and send BGP UPDATE messages from group packing list
   */
  folly::coro::Task<void> buildAndSendGroupBgpMessages() noexcept;

  /*
   * Return whether any in-sync peer still owes one of the group's egress
   * EoRs. The per-peer EGRESS_EOR_PENDING flags remain the source of truth;
   * this reports the group's incrementally maintained view of them.
   */
  bool egressEoRsPending() const noexcept {
    return !syncPeersWithPendingEoRs_.empty();
  }

  /*
   * Return whether a buildAndSendGroupBgpMessages invocation currently owns
   * the group. This, not the group state, is what keeps a second one out.
   */
  bool isPackingInProgress() const noexcept {
    return packingInProgress_;
  }

  /*
   * Return whether the group has route updates or per-peer EoRs to send.
   */
  bool hasPendingMessages() const noexcept;

  /*
   * Mark every currently in-sync peer as owing each AFI negotiated by the
   * group. The per-peer EGRESS_EOR_PENDING flags are the single source of
   * truth for what each peer still owes.
   */
  void setEgressEorsPendingSyncPeers() noexcept;

  /*
   * Distribute per-AFI EoR markers to in-sync peers that still owe them.
   * Drains one AFI at a time (waitForAllPendingPushes between AFIs). Returns
   * the number of EoR PDUs accepted for delivery (one per AFI distributed) so
   * the caller can fold it into its BGP message count.
   */
  folly::coro::Task<uint32_t> distributePendingEoRs() noexcept;

  /*
   * Try to insert withdrawal for a prefix in the group's packing list
   * Similar to AdjRib::tryInsertWithdrawal but operates at group level
   * Made public for testing
   */
  void tryInsertWithdrawal(
      const folly::CIDRNetwork& prefix,
      AdjRibEntry* adjRibEntry,
      const std::string& insertedMsg,
      const std::string& notInsertedMsg) noexcept;

  /*
   * Get post-policy attributes and metadata for the group
   * Similar to AdjRib::getPostOutPolicyAttributesAndInfo but operates at
   * group level Made public for testing
   */
  std::pair<const std::shared_ptr<const BgpPath>, const PostPolicyInfo>
  getPostOutPolicyAttributesAndInfo(
      const RibOutAnnouncementEntry& update,
      AdjRibEntry* adjRibEntry,
      const std::shared_ptr<const BgpPath>& prePolicyAttrs,
      const std::string& updatePeerIdStr) noexcept;

  /*
   * Update all attributes except nexthop before putting in packing list
   * Similar to AdjRib::updateAttributesOutWithoutNexthop but uses group's
   * peeringParams Made public for testing
   */
  void updateAttributesOutWithoutNexthop(
      const RibOutAnnouncementEntry& update,
      const std::shared_ptr<const BgpPath>& policyResultAttrs,
      std::shared_ptr<BgpPath> attrsToUpdate,
      const PostPolicyInfo& postPolicyInfo) noexcept;

  /*
   * Result of trying to push message to a peer's queue
   */
  enum class PushResult {
    PUSH_OK, // Message pushed successfully
    PUSH_PENDING, // Queue blocked, deferred coro scheduled
    PUSH_FAILED // Scheduling failed (asyncScope null or cancelled)
  };

  /*
   * Distribute a single message to all in-sync peers with backpressure
   * Sets bits for pending pushes, schedules deferred coros, waits for
   * completion
   */
  folly::coro::Task<void> distributeMessageToInSyncPeers(
      const std::shared_ptr<nettools::bgplib::BgpUpdate2>& message,
      const std::shared_ptr<const BgpPath>& postOutAttrs,
      nettools::bgplib::BgpUpdateAfi afi,
      bool isNexthopSetByPolicy = false) noexcept;

  /*
   * Try to push message to a peer's bounded queue
   * Returns PUSH_OK if immediate push succeeded, PUSH_PENDING if deferred,
   * PUSH_FAILED if scheduling failed.
   * onResolved (optional) runs once when the push resolves; not on
   * PUSH_FAILED.
   */
  PushResult tryPushToPeer(
      const nettools::bgplib::FiberBgpPeer::InputMessageT& message,
      const std::shared_ptr<AdjRib>& adjRib,
      uint64_t bitPos,
      folly::Function<void() noexcept> onResolved = {}) noexcept;

  /*
   * Wait for all pending pushes to complete (all bits clear)
   */
  folly::coro::Task<void> waitForAllPendingPushes() noexcept;

  /*
   * Pack prefixes into BgpUpdate2 thrift collection.
   * Wrapper for packPrefixesCommon that uses group-specific parameters
   * Made public for testing
   */
  uint32_t packGroupPrefixes(
      PrefixSet& prefixPathIds,
      std::vector<nettools::bgplib::RiggedIPPrefix>& bgpUpdatePrefixes,
      bool sendAddPath) noexcept;

  /*
   * Build single BGP UPDATE message with size limit
   * Similar to AdjRib::buildUpdateWithSizeEstimation but for group-level
   * Made public for testing
   */
  std::shared_ptr<nettools::bgplib::BgpUpdate2> buildGroupUpdate(
      const BgpPathWithAfi& attrsWithAfi,
      PrefixSet& prefixPathIds) noexcept;

  /*
   * Override update group config for testing.
   */
  void setUpdateGroupConfigForTesting(
      const UpdateGroupConfig& config) noexcept {
    updateGroupConfig_ = config;
  }

  const ConsumerBitmap& getBlockedBitmap() const noexcept {
    return adjRibBlockedBitmap_;
  }

  /*
   * Register a peer with this update group
   * Assigns bit position and updates tracking structures
   * Handles different states based on whether group is initialized
   * @param adjRib - The peer to register with the group
   */
  void registerPeer(const std::shared_ptr<AdjRib>& adjRib);

  /*
   * Unregister a peer from this update group
   * Cleans up tracking structures and bitmaps
   * @param adjRib - The peer to unregister from the group
   */
  void unregisterPeer(const std::shared_ptr<AdjRib>& adjRib) noexcept;

  /*
   * Erase a detached peer's diverged per-peer RIB-OUT entries from the
   * group's trees; group-owned entries the peer still shares are left in
   * place, still serving the group's other members. Either way the peer's
   * egress prefix counts are settled for every entry it advertised. Must be
   * called before removePeer() clears the bit.
   */
  void cleanUpPeerRibOut(const std::shared_ptr<AdjRib>& adjRib) noexcept;

  /*
   * High-level method that transfers per-peer RIB-OUT entries for a set of
   * peers to the new group, cleans up this group's bitmaps and tracking state,
   * and registers each peer with the new group as a detached peer.
   *
   * movePeers itself schedules no coroutines -- it is a pure state/RIB-OUT
   * move, so synchronous callers (unit tests) can pass no callback and move
   * peers without spawning work. Reactivating a moved peer (so it drains and
   * rejoins the new group) is the caller's responsibility, injected via the
   * optional onPeerMoved hook: it runs once per moved peer after the whole
   * batch is re-homed. The egress re-eval sweep passes a hook that re-evaluates
   * (policy changed) or reactivates (policy unchanged) each moved peer.
   */
  void movePeers(
      const std::vector<std::shared_ptr<AdjRib>>& peersToMove,
      const std::shared_ptr<AdjRibOutGroup>& newGroup,
      const std::function<void(const std::shared_ptr<AdjRib>&)>& onPeerMoved =
          {}) noexcept;

  /*
   * COPY this group's group-owned entries to newGroup's group owner key (they
   * remain in this group, still serving its other members) and MOVE the
   * per-peer entries of peersToMove to newGroup under each peer's own owner key
   * (erased from this group). Group-owned entries stay group-owned in the new
   * group; only the listed peers' per-peer entries are transferred. Path/Lite
   * variants for the add-path and non-add-path trees.
   *
   * peersToMove is never empty: the only reason to call this API is to move
   * peers, so callers always pass at least one peer.
   */
  void movePeersSharedRibOutPathEntries(
      const std::vector<std::shared_ptr<AdjRib>>& peersToMove,
      const std::shared_ptr<AdjRibOutGroup>& newGroup) noexcept;

  void movePeersSharedRibOutLiteEntries(
      const std::vector<std::shared_ptr<AdjRib>>& peersToMove,
      const std::shared_ptr<AdjRibOutGroup>& newGroup) noexcept;

  /*
   * Split a subset of this group's members into newGroup, which becomes a
   * faithful copy of this group at this group's current change-list position.
   * Copies this group's operational state and flags, wires newGroup's change
   * list consumer joined at this group's CL marker, copies this group's
   * group-owned RIB-OUT entries and moves the peers' per-peer entries via
   * movePeersSharedRibOut, then re-homes each peer: its
   * in-sync/detached/blocked status (and detached RIB version) is mirrored into
   * newGroup at a freshly allocated bit (bit positions are NOT preserved). On
   * this (old) group only the peer's bitmap bits and detachedPeers_ membership
   * are cleared -- the peer's detached CL consumer and detached RIB version are
   * left untouched. Caller constructs newGroup as a proper update group
   * (groupId/groupKey/ sendAddPath) before calling.
   */
  void splitToNewGroup(
      const std::shared_ptr<AdjRibOutGroup>& newGroup,
      const std::vector<std::shared_ptr<AdjRib>>& peersToMove) noexcept;

  /*
   * Mark a peer as blocked due to TCP backpressure.
   * Sets bitmap bit, checks frequency threshold if enabled, schedules duration
   * timer.
   * @param adjRib - The peer that just became blocked
   */
  void markPeerBlocked(const std::shared_ptr<AdjRib>& adjRib) noexcept;

  /*
   * Mark a peer as unblocked (queue space available).
   * Clears bitmap bit, cancels duration timer.
   * Called from makeGuard in deferredPushToPeer to guarantee cleanup.
   * @param adjRib - The peer that just unblocked
   */
  void markPeerUnblocked(const std::shared_ptr<AdjRib>& adjRib) noexcept;

  /*
   * Check if any peer in the sync bitmap is currently blocked.
   */
  bool hasBlockedPeers() const noexcept;

  /* Why a peer was detached from the group. Reasons with an associated
   * AdjRibStats counter select the counter bumped by detachPeer. */
  enum class DetachReason {
    Blocking, // Slow/blocked peer (backpressure)
    Policy, // Egress policy re-evaluation / group move
    RouteRefresh, // Requester-specific RFC 2918 replay
  };

  /*
   * Detach a peer from the group (core detachment logic).
   * Copies egress prefix counts, clones packing list, marks detached,
   * sets version fields, clears blocked bitmap, cancels slow peer timer,
   * registers detached CL consumer, propagates EoR state, bumps the
   * cumulative AdjRibStats detachment counter for `reason` when one exists, and
   * transitions the peer to its detached state (DETACHED_BLOCKED if blocked,
   * else DETACHED_RUNNING).
   * Does NOT handle slow-peer-specific logic (stats, last-synced guard).
   */
  void detachPeer(
      const std::shared_ptr<AdjRib>& adjRib,
      DetachReason reason) noexcept;

  /*
   * Detach a set of peers in the group. Detaching gives each in-sync member
   * sets the detachedRibVersion and creates the peer's own change-list
   * consumer so that it can run independent processing. The detachedRibVersion
   * being explicitly set allows us to know which entries the peer is
   * sharing with the group.
   *
   * The usage of this API is currently for detachment before
   * moving peers to another group.
   *
   * Members already in a DETACHED_* state are skipped, since detachPeer is not
   * idempotent (it would re-clone the packing list and re-register
   * the consumer).
   */
  void detachPeers(
      const std::vector<std::shared_ptr<AdjRib>>& peers,
      DetachReason reason) noexcept;

  /*
   * Detach a slow peer from the group.
   * Calls detachPeer for core logic (including blocked bitmap clear),
   * then handles slow-peer-specific cleanup: stats, last-synced guard,
   * EoR, state transition JOINED_BLOCKED -> DETACHED_BLOCKED.
   * Skips detachment if the peer is the last synced member.
   * EoR pending state is handled inside detachPeer (copies the group's
   * per-AFI egress EoR pending flags onto the peer).
   * @param adjRib - The peer to detach
   */
  void detachSlowPeer(const std::shared_ptr<AdjRib>& adjRib) noexcept;

  /*
   * Mark a peer as detached from the group (with possibly different
   * RIB-OUT state). Enables lazy cloning in Phase 4.
   */
  void markPeerDetached(const std::shared_ptr<AdjRib>& adjRib) noexcept;

  /*
   * Clear diverged state for a peer (on rejoin).
   */
  void markPeerInSync(const std::shared_ptr<AdjRib>& adjRib) noexcept;

  /*
   * Recover the group after its last SYNC peer leaves, leaving only detached
   * peers. Clears the packing list and tries to restore a SYNC peer. A sharing
   * DSP keeps the group consumer active until both consumers reach the end;
   * with only DEP-As, the consumer remains inactive until one finishes
   * draining.
   */
  void handleNoSyncPeers() noexcept;

  /*
   * Recover this group after a membership change (peer move/removal) if it was
   * left with members but no in-sync peers. This is the caller-side recovery
   * step: movePeers()/removePeer() stay pure so a bulk re-home never promotes a
   * peer still pending its own move; the orchestrator (e.g. PeerManagerBase
   * egress-policy re-evaluation) runs this once per affected group after all
   * moves complete. No-op if the group still has a SYNC peer or no members.
   */
  void recoverIfNoSyncPeers() noexcept;

  /*
   * Promote a detached peer at or ahead of the group's change-list position to
   * SYNC: move its RIB-OUT entries to the group owner key, re-create the
   * group's change list consumer joined at the peer's CL position, adopt the
   * peer's RIB version, tear down the peer's detached-mode processing, and mark
   * it in sync.
   */
  void promoteDetachedPeerToSync(std::shared_ptr<AdjRib> adjRib) noexcept;

  /*
   * Get the set of detached peers.
   */
  const folly::F14NodeSet<std::shared_ptr<AdjRib>>& getDetachedPeers()
      const noexcept {
    return detachedPeers_;
  }

  /*
   * Deep-copy the group's current packing list to the peer on detachment.
   * The peer drains this independently.
   * @param adjRib - The peer to clone the packing list for
   */
  void clonePackingListForPeer(const std::shared_ptr<AdjRib>& adjRib) noexcept;

  /*
   * Check for detached peers ready to rejoin and accept them back into
   * the group. DFP peers (IS_DETACHED_FAST_PEER flag set) are accepted
   * directly — no collapse needed. DSP peers are accepted only after both
   * change-list consumers reach the end, both packing lists drain, and their
   * materialized RIB versions match. DSP peers then go through
   * tryAcceptPeersToGroup for collapse verification. Called ONLY after PL
   * drain completes (WAITING -> IDLE transition).
   */
  void checkAndAcceptReadyToJoinPeers() noexcept;

  /*
   * Invoked once the group has processed all pending changes (packing list
   * empty): transition the group to IDLE and try to rejoin detached peers via
   * checkAndAcceptReadyToJoinPeers. No-op if the packing list still has
   * entries. Called from both the change list consume timer callback and the
   * buildAndSendGroupBgpMessages cleanup guard.
   */
  void tryRejoinDetachedPeersOnAllChangesProcessed() noexcept;

  /*
   * Called by a DSP peer that has transitioned to DETACHED_READY_TO_JOIN
   * to proactively trigger its own rejoin without waiting for the next group
   * PL drain. Revalidates the complete DSP readiness invariant immediately
   * before collapse.
   * @param adjRib - The peer attempting to rejoin
   */
  void maybeAcceptDSPPeer(const std::shared_ptr<AdjRib>& adjRib) noexcept;

  /*
   * Test-only: defer DRJ acceptance for a specific peer.
   * When deferred, checkAndAcceptReadyToJoinPeers and maybeAcceptDSPPeer
   * skip the peer, keeping it in DETACHED_READY_TO_JOIN.
   * On release (defer=false), triggers acceptance for the peer.
   */
  void testOnlySetDeferDrjAcceptance(
      const folly::IPAddress& peerAddr,
      bool defer) noexcept;

  /*
   * Try to accept a single DSP candidate peer back into the group.
   * @param candidatePeer - Peer eligible to rejoin
   * @return true if the peer was successfully accepted
   */
  bool tryAcceptPeerToGroup(
      const std::shared_ptr<AdjRib>& candidatePeer) noexcept;

  /*
   * Try to accept DSP candidate peers back into the group in batch.
   * Splits candidates into addPath and non-addPath peers, then
   * collapses their RIB-OUT entries in one tree walk per type.
   * Peers with matching entries are deactivated from detached mode
   * and transitioned to JOINED_RUNNING. Peers with discrepancies
   * are set back to DETACHED_RUNNING with packing timers rescheduled.
   * @param candidatePeers - Peers eligible to rejoin
   * @return peers that were successfully resynced
   */
  std::vector<std::shared_ptr<AdjRib>> tryAcceptPeersToGroup(
      const std::vector<std::shared_ptr<AdjRib>>& candidatePeers) noexcept;

  /*
   * Get UpdateGroupKey for this group
   */
  const UpdateGroupKey& getGroupKey() const noexcept {
    return groupKey_;
  }

  /*
   * groupDescriptor_ is derived from groupKey_, so it must be rebuilt here.
   * Otherwise every log line emitted after a rekey still names the previous
   * egress policy, AFI set and peerOverride.
   */
  void setGroupKey(const UpdateGroupKey& key) {
    groupKey_ = key;
    groupDescriptor_ = buildGroupDescriptor(groupId_, groupKey_);
  }

  /*
   * Get count of members in the group
   */
  size_t getMemberCount() const noexcept {
    return bitToAdjRibs_.size();
  }

  /*
   * Get the cached RIB version for this group.
   * All members of an update group share the same version since they
   * consume updates as a unit. A value of 0 indicates the group is new
   * or hasn't completed initial dump (displayed as "N/A" in CLI).
   */
  uint64_t getLastSeenRibVersion() const noexcept {
    return lastSeenRibVersion_;
  }

  /*
   * PeerManagerBase's max seen RIB version, read through the group's
   * ShadowRibView. Reads 0 for a group built without a shadow RIB -- callers
   * feed this to a setLastSeenRibVersion() that only advances, so 0 is a
   * no-op.
   */
  uint64_t getShadowRibMaxVersion() const noexcept {
    return maxRibVersion_;
  }

  /*
   * Get read-only access to the peer-to-AdjRib map for CLI/thrift reporting.
   */
  const std::unordered_map<uint64_t, std::shared_ptr<AdjRib>>& getBitToAdjRibs()
      const noexcept {
    return bitToAdjRibs_;
  }

  std::optional<int64_t> getInitialDumpCompletionTimeMs() const noexcept {
    return initialDumpCompletionTimeMs_;
  }

  int64_t getTotalDiscrepancies() const noexcept {
    return totalDiscrepancies_;
  }

  void incrTotalDiscrepancies() noexcept {
    ++totalDiscrepancies_;
  }

  /*
   * Set the last seen RIB version for this group. Called after initial dump
   * completes and when consuming change list updates.
   *
   * Only ever advances. A lower value violates the group's version-ordering
   * invariant and is logged and ignored. Use clearLastSeenRibVersion() to
   * reset.
   */
  void setLastSeenRibVersion(uint64_t version) noexcept {
    if (version < lastSeenRibVersion_) [[unlikely]] {
      XLOGF_EVERY_MS(
          ERR,
          100000,
          "Group {} RIB version monotonicity violation, ignoring attempt to "
          "set last seen RIB version {} below current {}",
          groupDescriptor_,
          version,
          lastSeenRibVersion_);
      return;
    }
    if (version > lastSeenRibVersion_) {
      lastSeenRibVersion_ = version;
    }
  }

  /*
   * Clear the last seen RIB version for this group.
   * Called when the group is destroyed.
   */
  void clearLastSeenRibVersion() noexcept {
    lastSeenRibVersion_ = 0;
  }

  /*
   * Check if a peer (identified by bit position) is in sync with this group.
   * A peer is in-sync if its bit is set in adjRibSyncBitmap_.
   * Used to determine whether to return group's or peer's cached RIB version.
   */
  bool isPeerInSync(uint64_t bitPos) const noexcept {
    return BitmapUtils::isBitSet(adjRibSyncBitmap_, bitPos);
  }

  size_t getNumInSyncPeers() const noexcept {
    return numInSyncPeers_;
  }

  /*
   * Number of detached peers that diverged after running with the group
   * (transitioned JOINED -> BLOCKED -> detached). Equivalent to the number of
   * detached peers whose detachedRibVersion is non-zero; DETACHED_INIT_DUMP
   * peers (detachedRibVersion == 0) are excluded.
   */
  size_t getNumPeersDetachedAfterJoin() const noexcept {
    return numPeersDetachedAfterJoin_;
  }

  /*
   * Some peer holds entries in common with the group: a SYNC peer, or a
   * detached peer that diverged after running with it. Such a peer needs the
   * group consumer to keep advancing to the end of the change list. Negated,
   * it is the condition for promoting a detached peer, since
   * promoteDetachedPeerToSync deletes group-only entries that a sharing peer
   * would still need.
   */
  bool hasEntrySharingPeers() const noexcept {
    return numInSyncPeers_ > 0 || numPeersDetachedAfterJoin_ > 0;
  }

  /*
   * Increment numPeersDetachedAfterJoin_ when a peer joins the detached set
   * (detaches at a non-zero detachedRibVersion). Called from detachPeer.
   */
  void incrementPeersDetachedAfterJoin() noexcept;

  /*
   * Decrement numPeersDetachedAfterJoin_ when a peer leaves the
   * detached set (goes down or rejoins). Called from
   * AdjRib::deactivateDetachedModeProcessing() — the single point where a
   * detached peer's processing is torn down. Logs an error on underflow.
   */
  void decrementPeersDetachedAfterJoin() noexcept;

 private:
  PathOwnerMap& getOrCreatePathOwnerMap(
      const folly::CIDRNetwork& prefix) noexcept;
  LiteOwnerMap& getOrCreateLiteOwnerMap(
      const folly::CIDRNetwork& prefix) noexcept;

  /*
   * Clone a RIB-OUT entry for a specific peer.
   * Creates an entry keyed by effectiveOwnerKey, shallow copying all
   * relevant fields from the source entry.
   */
  AdjRibEntry* copyEntryForOwner(
      PathOwnerMap& ownerMap,
      const AdjRibOutOwnerKey& effectiveOwnerKey,
      const AdjRibEntry* entryToCopy) noexcept;
  AdjRibEntry* copyEntryForOwner(
      LiteOwnerMap& ownerMap,
      const AdjRibOutOwnerKey& effectiveOwnerKey,
      const AdjRibEntry* entryToCopy) noexcept;

  /*
   * Create a fresh cancellation source for a newly scheduled group dump and
   * return its token to merge into asyncScope_ when adding the walk.
   */
  folly::CancellationToken getCancellationTokenForNewGroupDump() noexcept {
    ribDumpCancellationSource_.emplace();
    return ribDumpCancellationSource_->getToken();
  }

  /*
   * Clear rib-dump tracking on normal completion so a subsequent dump can be
   * scheduled. The caller MUST only invoke this when its own task was not
   * cancelled (a superseded/stopped task must not overwrite a newer one).
   */
  void resetRibDumpCancellationSource() noexcept {
    ribDumpCancellationSource_.reset();
  }

  /*
   * Promote a peer's RIB-OUT entries to group entries. Walks the
   * RIB-OUT tree: entries with the peer's owner key are moved to the
   * group owner key; entries with only the group owner key are deleted.
   */
  void promoteDetachedPeerPathEntries(
      const std::shared_ptr<AdjRib>& adjRib) noexcept;
  void promoteDetachedPeerLiteEntries(
      const std::shared_ptr<AdjRib>& adjRib) noexcept;

  void setSyncBit(uint64_t bit, const std::shared_ptr<AdjRib>& adjRib) noexcept;

  void clearSyncBit(
      uint64_t bit,
      const std::shared_ptr<AdjRib>& adjRib) noexcept;

  /*
   * Common cleanup when removing a peer from this group.
   * Resets timers, clears bitmaps, frees bit position, and removes
   * from tracking maps. Does NOT handle per-peer RIB-OUT entries or
   * peer state transitions — callers handle those separately. Does NOT run
   * handleNoSyncPeers(): callers invoke it (guarded on members-but-no-sync)
   * after removal, so a bulk caller can remove all peers first and recover
   * once rather than per-removal.
   */
  void removePeer(const std::shared_ptr<AdjRib>& adjRib) noexcept;

  /*
   * Move each peer's RIB-OUT entries to the new group. These methods do NOT
   * copy this group's group-owned entries across as group-owned entries.
   * Instead, for each shared group entry a peer was still using, the peer gets
   * its own copy under its peer owner key (alongside its already-diverged
   * per-peer entries). The result is that each moved peer has its full RIB-OUT
   * materialized under its own peer owner key in the new group.
   */
  void movePeerMaterializedRibOutPathEntries(
      const std::vector<std::shared_ptr<AdjRib>>& peersToMove,
      const std::shared_ptr<AdjRibOutGroup>& newGroup) noexcept;

  void movePeerMaterializedRibOutLiteEntries(
      const std::vector<std::shared_ptr<AdjRib>>& peersToMove,
      const std::shared_ptr<AdjRibOutGroup>& newGroup) noexcept;

  /*
   * Copy this group's operational state and flags onto newGroup so it is a
   * faithful copy. Identity (groupId/name/groupKey) is intentionally NOT copied
   * -- newGroup keeps its own. This is the single place that mirrors group
   * state across a split, so any new operational field added to AdjRibOutGroup
   * should be copied here too.
   */
  void copyGroupFieldsToNewGroup(
      const std::shared_ptr<AdjRibOutGroup>& newGroup) noexcept;

  /*
   * Clone decision algorithm: determines if a group entry must be cloned
   * to a detached peer before the group mutates it.
   * Takes a tree iterator (radixNode) to avoid redundant exactMatch
   * lookups when the caller already has the iterator.
   * @param radixNode - Iterator to the prefix node in the tree
   * @param pathId - The path ID (for add-path)
   * @param peer - The detached peer to check
   * @param groupEntry - The existing group entry about to be mutated
   * @return true if the entry should be cloned to this peer
   */
  bool shouldClonePathForPeer(
      const AdjRibPathTree::Iterator& radixNodeItr,
      const uint32_t pathId,
      const std::shared_ptr<AdjRib>& peer,
      const uint64_t groupEntryRibVersion) noexcept;

  bool shouldCloneLiteForPeer(
      const AdjRibLiteTree::Iterator& radixNodeItr,
      const std::shared_ptr<AdjRib>& peer,
      const uint64_t groupEntryRibVersion) noexcept;

  /*
   * Iterate all detached peers and clone the group entry to any peer
   * that was sharing it (per shouldClone{Path,Lite}ForPeer). Called BEFORE
   * the group mutates or removes an entry. Takes the already-looked-up
   * iterator to avoid redundant tree lookups.
   */
  void lazyClonePathForDetachedPeers(
      const folly::CIDRNetwork& prefix,
      uint32_t pathId,
      AdjRibPathTree::Iterator& radixNodeItr,
      const AdjRibEntry* groupEntry) noexcept;

  void lazyCloneLiteForDetachedPeers(
      const folly::CIDRNetwork& prefix,
      AdjRibLiteTree::Iterator& radixNodeItr,
      const AdjRibEntry* groupEntry) noexcept;

  /*
   * Collapse a single peer's entry at one radix tree node back into the
   * group. The peer entry is always erased (collapsed). If there is a
   * discrepancy between peer and group state, a correction is inserted
   * into the peer's packing list (attrToPrefixMap) so the peer converges
   * to group state:
   *   - Match: peer entry erased, no packing list update needed
   *   - Mismatch: peer entry erased, re-advertisement queued
   *   - Peer only: peer entry erased, withdrawal queued
   *   - Group only (post-detach): announcement queued
   *   - Shared pre-detachment (group only, old ribVersion): no action
   * @param ownerMap - The owner map at the current radix tree node
   * @param groupOwnerKey - The group's owner key
   * @param prefix - The prefix at this node
   * @param peer - The detached peer to collapse
   * @return true if a discrepancy was found and a correction was queued
   *         in the peer's packing list, false if collapse succeeded with
   *         no discrepancy (no packing list correction needed).
   */
  bool collapseLiteEntry(
      LiteOwnerMap& ownerMap,
      const AdjRibOutOwnerKey& groupOwnerKey,
      const folly::CIDRNetwork& prefix,
      const std::shared_ptr<AdjRib>& peer);
  bool collapsePathEntry(
      PathOwnerMap& ownerMap,
      const AdjRibOutOwnerKey& groupOwnerKey,
      const folly::CIDRNetwork& prefix,
      const std::shared_ptr<AdjRib>& peer);

  /*
   * Collapse all per-peer RIB-OUT entries back into the group in a
   * single tree walk. Every peer entry is erased; discrepancies are
   * corrected via the peer's packing list (attrToPrefixMap). Peers
   * that required any packing list correction have their
   * RIB_OUT_DISCREPANCY flag set. Empty radix tree nodes are
   * cleaned up after the walk.
   * @param groupOwnerKey - The group's owner key
   * @param peers - The detached peers to collapse
   */
  void collapsePathEntries(
      const AdjRibOutOwnerKey& groupOwnerKey,
      const std::vector<std::shared_ptr<AdjRib>>& peers);
  void collapseLiteEntries(
      const AdjRibOutOwnerKey& groupOwnerKey,
      const std::vector<std::shared_ptr<AdjRib>>& peers);

  /*
   * Helper: check if egress policy is configured for this group
   */
  inline bool egressPolicyConfigured() const {
    return groupKey_.egressPolicyName.has_value();
  }

  /*
   * Helper: extract post-policy attrs from policy output message
   * Same as AdjRib::getPostPolicyOutAttrsAndPolicyFromMessage
   */
  inline const std::shared_ptr<routing::AttributesAndPolicy<BgpPath>>
  getPostPolicyOutAttrsAndPolicyFromMessage(
      const folly::CIDRNetwork& prefix,
      const PolicyOutMessage& policyOut) const noexcept {
    auto search = policyOut.result.find(prefix);
    // prefix MUST be in policyOut
    CHECK(search != policyOut.result.end());
    return search->second;
  }

  /*
   * Helper: get post-policy attributes, policy term name, and policy info
   * Similar to AdjRib::getPostPolicyAttributesPolicyTermAndInfo
   * Uses group's policy and policyCache
   */
  std::tuple<std::shared_ptr<const BgpPath>, std::string, PostPolicyInfo>
  getPostPolicyAttributesPolicyTermAndInfo(
      const std::string& policyName,
      const folly::CIDRNetwork& prefix,
      const std::shared_ptr<const BgpPath>& prePolicyAttrs,
      const std::shared_ptr<BgpPolicyActionData>& policyActionData,
      bool isPartialDrain = false) noexcept;

  /*
   * EventBase reference for scheduling async operations.
   * Same pattern as AdjRib - deliberately use reference to share the SAME
   * folly::coro primitive across multiple update groups.
   */
  folly::EventBase& evb_;

  /*
   * Unique string to represent the name of the group.
   *  - If enableUpdateGroup_ = true, group name is set as the string version
   * of UpdateGroupKey;
   *  - If enableUpdateGroup_ = false, group name is set from the input
   * parameter, which can be either retrieved from PeeringParams or PeerAddr
   * string.
   */
  std::string groupName_;

  /*
   * Unique integer value to identify the update-group. This id will be
   * assigned to only one group.
   */
  uint64_t groupId_;

  /*
   * Boolean flag to indicate if update-group feature is enabled or NOT
   */
  bool enableUpdateGroup_{false};

  /*
   * UpdateGroupKey that defines grouping criteria.
   * Peers with identical UpdateGroupKey belong to the same update group.
   * Used for group-level canAnnounce() and add-path decisions.
   */
  UpdateGroupKey groupKey_;

  /*
   * Friendly descriptor for logging: "groupId(egressPolicyName/afiLabel)"
   * More readable than the full groupName_ (which encodes all 17 key fields).
   * Derived from groupKey_; kept in sync by setGroupKey().
   */
  std::string groupDescriptor_;

  /*
   * Build a compact AFI label from the UpdateGroupKey.
   * Examples: "v4", "v6", "v4v6", "v4ov6", "v4v6+v4ov6"
   */
  static std::string buildAfiLabel(const UpdateGroupKey& key) {
    std::string label;
    if (key.afiIpv4Negotiated) {
      label += "v4";
    }
    if (key.afiIpv6Negotiated) {
      label += "v6";
    }
    if (key.extNhEncodingCapable) {
      if (!label.empty()) {
        label += "+";
      }
      label += "v4ov6";
    }
    return label.empty() ? "none" : label;
  }

  /*
   * Build the logging descriptor from the group's identity and key.
   * Example: "2(EB-FA-OUT/v4,peerOverride=false)"
   */
  static std::string buildGroupDescriptor(
      uint64_t groupId,
      const UpdateGroupKey& key) {
    return fmt::format(
        "{}({}/{},peerOverride={})",
        groupId,
        key.egressPolicyName.value_or(""),
        buildAfiLabel(key),
        key.peerOverride);
  }

  /*
   * Bitmap of adjRibs that are currently established (bit=1 means session
   * ESTABLISHED). This is used to distinguish between peers that went down
   * (bit=0) vs never came up(no bit set).
   */
  ConsumerBitmap adjRibEstablishedBitmap_;

  /*
   * Bitmap of adjRibs that are currently in-sync with group (bit=1 means
   * in-sync). Only in-sync peers receive group-generated updates. Detached
   * peers have bit=0. This is the key bitmap that determines who participates
   * in group update generation.
   */
  ConsumerBitmap adjRibSyncBitmap_;

  /*
   * In-sync peers that still owe at least one per-AFI EoR. This is maintained
   * with adjRibSyncBitmap_: clearing a peer's sync bit removes it, while
   * setting the bit transfers any pending per-peer obligation into this group.
   */
  folly::F14NodeSet<std::shared_ptr<AdjRib>> syncPeersWithPendingEoRs_;

  /*
   * Cached count of set bits in adjRibSyncBitmap_, maintained incrementally
   * when bits are set/cleared to avoid O(bitmap-size) popcount on every
   * query.
   */
  size_t numInSyncPeers_{0};

  /*
   * Count of detached peers that diverged after running with the group
   * (transitioned JOINED -> BLOCKED -> detached). Maintained incrementally:
   * incremented in detachPeer() when a peer's detachedRibVersion goes from 0
   * to non-zero, decremented when a counted peer goes down or rejoins.
   * Equivalent to the number of detached peers whose detachedRibVersion is
   * non-zero (excludes DETACHED_INIT_DUMP peers).
   */
  size_t numPeersDetachedAfterJoin_{0};

  /*
   * Current state of the update group (UNINITIALIZED, IDLE, READY, WAITING).
   * State machine drives update group behavior - when to build packing list,
   * when to send updates, when to consume change tracker.
   */
  UpdateGroupState state_{UpdateGroupState::UNINITIALIZED};

  /* scheduleInitialDump() leaves state_ UNINITIALIZED until its queued task
   * runs, so an armed source doubles as the latch that coalesces duplicate
   * requests while that task is in flight, and as the handle that cancels the
   * walk when the group is emptied or the dump is superseded before it runs.
   * buildAndScheduleSendInitialDumpFromShadowRib() clears it on every
   * uncancelled exit; on a cancelled exit the canceller owns it instead. */
  std::optional<folly::CancellationSource> ribDumpCancellationSource_;

  /*
   * Mapping from bit position (from bitmap) to actual AdjRib object pointer.
   * Bitmaps are efficient but we need to get the actual peer object from bit
   * position to perform operations like sending updates, checking config,
   * etc.
   */
  std::unordered_map<uint64_t, std::shared_ptr<AdjRib>> bitToAdjRibs_;

  /*
   * Bit position manager for allocating and freeing peer bit positions.
   * Ensures proper bit reuse when peers leave and rejoin the group.
   */
  ConsumerBitManager bitManager_;

  /*
   * Group-level packing list: maps BGP path attributes to list of prefixes.
   * This is the key optimization - one packing list serves N peers instead of
   * N lists, reducing memory by N and CPU by N since we build it once per
   * group.
   */
  AttrToPrefixMap attrToPrefixMap_;

  /*
   * A consumer initialized to the global change tracker.
   * This consumer is explicitly initialized after AdjRibOutGroup has been
   * created (not initialized as part of constructor).
   * Uses composition pattern same as AdjRib.
   */
  std::shared_ptr<AdjRibOutGroupConsumer> changeListConsumer_{nullptr};

  /*
   * Shared pointer to the global change list tracker.
   * Cached here so detached peers can register their own consumer.
   * Set by PeerManagerBase when configuring the group's change list consumer.
   */
  std::shared_ptr<ChangeTracker<ShadowRibEntry>> changeListTracker_{nullptr};

  /*
   * References to the global consumer bitmaps managed by PeerManagerBase.
   * Cached here so detached peers can register their own consumer with
   * the correct bitmaps.
   */
  ConsumerBitmap* addPathConsumerBitmap_{nullptr};
  ConsumerBitmap* nonAddPathConsumerBitmap_{nullptr};

  /*
   * Timer for polled consumption of change tracker (respects MRAI).
   * Same pattern as AdjRib::changeListConsumeTimer_.
   */
  std::unique_ptr<folly::AsyncTimeout> changeListConsumeTimer_{nullptr};

  /*
   * MRAI (Minimum Route Advertisement Interval) for the group.
   */
  uint32_t mraiInterval_{kDefaultMraiInterval};

  /*
   * Shadow RIB entries walked by the initial dump and by egress policy
   * re-evaluation. Non-owning; owned by PeerManagerBase, or by
   * ShadowRibView::emptyEntries() for a group built without a shadow RIB.
   */
  const ShadowRibEntriesMap& shadowRibEntries_;

  /*
   * Live reference to PeerManagerBase's max seen RIB version
   * (maxRibVersion_). Non-owning. Read during a full RIB dump to advance the
   * group to the current table version even when the shadow RIB has been
   * emptied.
   */
  const uint64_t& maxRibVersion_;
  /*
   * Peering parameters cached from the first peer that formed this group
   * Used for AS-PATH manipulation, local pref, MED, etc.
   * All peers in group share identical parameters per update group criteria
   */
  std::optional<PeeringParams> peeringParams_{std::nullopt};

  /*
   * BGP policy manager for egress policy evaluation
   * Shared across all peers in the group
   */
  std::shared_ptr<PolicyManager> policyManager_{nullptr};

  /*
   * Policy cache for egress policy
   * Caches policy evaluation results to avoid re-running policy for same
   * attrs
   */
  std::shared_ptr<AdjRibPolicyCache> policyCache_{nullptr};

  /*
   * Bitmap tracking which peers are currently blocked due to TCP
   * backpressure. bit=1 means peer is blocked, bit=0 means peer is not
   * blocked. Managed by markPeerBlocked()/markPeerUnblocked().
   */
  ConsumerBitmap adjRibBlockedBitmap_;

  /*
   * Set of detached peers that may have different RIB-OUT state.
   * Used to enable lazy cloning when group updates/withdraws entries.
   */
  folly::F14NodeSet<std::shared_ptr<AdjRib>> detachedPeers_;

  /*
   * Update group configuration: serialization mode, slow peer detection
   * thresholds, and detachment behavior.
   */
  UpdateGroupConfig updateGroupConfig_;

  /*
   * AsyncScope for managing deferred push coroutines
   * Used to schedule coroutines that wait for blocked peer queues to unblock
   */
  folly::coro::CancellableAsyncScope asyncScope_;

  /**
   * Guard flag to prevent concurrent execution of
   * buildAndSendGroupBgpMessages. Only one instance should run at a time to
   * maintain BGP UPDATE ordering. Single-threaded on evb_, so no atomic
   * needed.
   */
  bool packingInProgress_{false};

  /**
   * Guards a rare edge case: splitToNewGroup re-homed one or more in-sync peers
   * into this group that were still BLOCKED (an in-flight push carried over)
   * from their previous group. Only in that case does this group need to drain
   * those carried-over pushes before packing, so this flag gates the
   * hasBlockedPeers() drain in buildAndSendGroupBgpMessages -- avoiding that
   * check on the common path where no blocked peers were ever moved in -- and
   * is cleared once the carried-over pushes have drained.
   */
  bool checkForBlockedPeers_{false};

  /**
   * Stats associated to this AdjRibOutGroup
   * Tracks sent update messages, announcements, and withdrawals for the group
   * Uses groupName as the identifier for ODS
   */
  AdjRibStats stats_;

  /*
   * Cached RIB version for the group. All members of an update group share
   * this version since they consume updates as a unit. This is the version
   * displayed in CLI/thrift for peers that belong to a group.
   * A value of 0 indicates the group hasn't completed initial dump yet.
   */
  uint64_t lastSeenRibVersion_{0};

  /**
   * Epoch time (ms) when initial RIB dump completed for this group.
   * Set once in buildInitialDumpFromShadowRib() when transitioning to
   * WAITING.
   */
  std::optional<int64_t> initialDumpCompletionTimeMs_{std::nullopt};

  /**
   * Total number of entry discrepancies detected during detached peer
   * rejoin collapse (collapsePathEntries / collapseLiteEntries).
   */
  int64_t totalDiscrepancies_{0};

#ifdef AdjRibOutGroup_TEST_FRIENDS
  AdjRibOutGroup_TEST_FRIENDS
#endif
};

/*
 * Class for update group specific changeListTracker consumer
 * Similar to AdjRibOutConsumer but operates at group level
 */
class AdjRibOutGroupConsumer : public Consumer<ShadowRibEntry> {
 public:
  AdjRibOutGroupConsumer(
      std::shared_ptr<ChangeTracker<ShadowRibEntry>>& changeListTracker,
      std::shared_ptr<AdjRibOutGroup> adjRibOutGroup,
      std::string name,
      folly::EventBase& evb,
      ConsumerBitmap& addPathBitmap,
      ConsumerBitmap& nonAddPathBitmap)
      : Consumer<ShadowRibEntry>(*changeListTracker, static_cast<size_t>(-1)),
        adjRibOutGroup_(std::move(adjRibOutGroup)),
        name_(std::move(name)),
        evb_(evb),
        addPathConsumerBitmap_(addPathBitmap),
        nonAddPathConsumerBitmap_(nonAddPathBitmap) {
    setProcessChangeItemCallback([this](ChangeItem<ShadowRibEntry>* item) {
      return this->processShadowRibEntryDelta(item);
    });
  }

  virtual ~AdjRibOutGroupConsumer() override;

  /**
   * Copy constructor (deleted).
   */
  AdjRibOutGroupConsumer(const AdjRibOutGroupConsumer&) = delete;

  /**
   * Copy assignment operator (deleted).
   */
  AdjRibOutGroupConsumer& operator=(const AdjRibOutGroupConsumer&) = delete;

  /**
   * Move constructor (deleted).
   */
  AdjRibOutGroupConsumer(AdjRibOutGroupConsumer&&) = delete;

  /**
   * Move assignment operator (deleted).
   */
  AdjRibOutGroupConsumer& operator=(AdjRibOutGroupConsumer&&) = delete;

  ProcessResult processShadowRibEntryDelta(ChangeItem<ShadowRibEntry>* item) {
    ShadowRibEntry& srEntry = item->getTypedObject();

    /* Transition to READY when receiving a new change tracker item. */
    adjRibOutGroup_->setState(UpdateGroupState::READY);

    adjRibOutGroup_->processShadowRibEntryChange(srEntry);

    return ProcessResult::CONTINUE;
  }

  ProcessResult processShadowRibEntryFullMultipaths(
      ChangeItem<ShadowRibEntry>* item) {
    ShadowRibEntry& srEntry = item->getTypedObject();

    /* Transition to READY when receiving a new change tracker item. */
    adjRibOutGroup_->setState(UpdateGroupState::READY);

    adjRibOutGroup_->processShadowRibEntryFullMultipaths(srEntry);

    return ProcessResult::CONTINUE;
  }

  void resetBitmap() noexcept {
    if (!adjRibOutGroup_) {
      return;
    }
    size_t bitPosition = getBitPosition();
    BitmapUtils::clearBit(addPathConsumerBitmap_, bitPosition);
    BitmapUtils::clearBit(nonAddPathConsumerBitmap_, bitPosition);
  }

  void setBitmap() noexcept {
    if (!adjRibOutGroup_) {
      return;
    }
    size_t bitPosition = getBitPosition();
    if (adjRibOutGroup_->getGroupKey().sendAddPath) {
      BitmapUtils::setBit(addPathConsumerBitmap_, bitPosition);
    } else {
      BitmapUtils::setBit(nonAddPathConsumerBitmap_, bitPosition);
    }
  }

 private:
  std::shared_ptr<AdjRibOutGroup> adjRibOutGroup_;
  std::string name_;
  [[maybe_unused]] folly::EventBase& evb_;
  ConsumerBitmap& addPathConsumerBitmap_;
  ConsumerBitmap& nonAddPathConsumerBitmap_;
};

} // namespace facebook::bgp
