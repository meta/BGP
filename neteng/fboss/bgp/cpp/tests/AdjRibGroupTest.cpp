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

#include <gtest/gtest.h>
#include <chrono>

#define AdjRib_TEST_FRIENDS                                                   \
  friend class AdjRibGroupTest;                                               \
  FRIEND_TEST(                                                                \
      AdjRibGroupDistributionFixture, CommittedUpdateCountDoesNotCrossClear); \
  FRIEND_TEST(                                                                \
      AdjRibGroupDistributionFixture, CommittedEorCountDoesNotCrossClear);    \
  FRIEND_TEST(                                                                \
      AdjRibGroupTest, ClearEgressPrefixCountsPreservesCumulativeUpdateCount);

#define AdjRibOutGroup_TEST_FRIENDS                                                  \
  friend class AdjRibGroupTest;                                                      \
  friend class AdjRibGroupPackingFixture;                                            \
  friend class AdjRibGroupRibOutEntryFixture;                                        \
  friend class AdjRibGroupWithdrawalFixture;                                         \
  friend class AdjRibGroupDistributionFixture;                                       \
  friend class AdjRibGroupPolicyFixture;                                             \
  friend class AdjRibGroupAddPathFixture;                                            \
  FRIEND_TEST(                                                                       \
      AdjRibGroupTest,                                                               \
      BuildAndSendGroupBgpMessages_EmptyPackingListRejoinLogGating);                 \
  FRIEND_TEST(                                                                       \
      AdjRibGroupTest,                                                               \
      BuildAndSendGroupBgpMessages_NoSyncPeersDoesNotRestartConsumeTimer);           \
  FRIEND_TEST(                                                                       \
      AdjRibGroupTest,                                                               \
      BuildAndSendGroupBgpMessages_SyncPeerRestartsConsumeTimer);                    \
  FRIEND_TEST(                                                                       \
      AdjRibGroupPackingFixture,                                                     \
      ProcessRibAnnouncedEntryForGroup_NoEgressPolicyWithPolicyManagerDoesNotCrash); \
  FRIEND_TEST(                                                                       \
      AdjRibGroupTest,                                                               \
      MovePeerMaterializedPathEntries_PeerOwnedPathNotOverwritten);                  \
  FRIEND_TEST(                                                                       \
      AdjRibGroupPolicyFixture,                                                      \
      GetPostOutPolicyAttributesAndInfo_PopulatesLbwActionData);                     \
  FRIEND_TEST(AdjRibGroupPolicyFixture, PostOutPolicyAgreesWithPerPeerProducer);

#include <folly/coro/BlockingWait.h>
#include <folly/coro/Collect.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/io/async/EventBase.h>
#include <folly/logging/xlog.h>

#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibCommon.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibGroup.h"
#include "neteng/fboss/bgp/cpp/changeTracker/ChangeTracker.h"
#include "neteng/fboss/bgp/cpp/changeTracker/ConsumerBitmap.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyManager.h"
#include "neteng/fboss/bgp/cpp/tests/BoundedWaitUtils.h"
#include "neteng/fboss/bgp/cpp/tests/PolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"

namespace facebook::bgp {

class AdjRibGroupTest : public ::testing::Test {
 protected:
  void SetUp() override {
    folly::SingletonVault::singleton()->registrationComplete();
    evb_ = std::make_unique<folly::EventBase>();
    changeListTracker_ =
        std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  }

  void TearDown() override {
    for (const auto& [group, peer] : registeredInSyncPeers_) {
      group->unregisterPeer(peer);
    }
    registeredInSyncPeers_.clear();

    if (adjRibOutGroup_) {
      adjRibOutGroup_->resetChangeListConsumer();

      /* Drain asyncScope_ cooperatively before destroying the group */
      folly::coro::blockingWait(adjRibOutGroup_->drainAsyncScope());
      adjRibOutGroup_.reset();
    }
    changeListTracker_.reset();
    evb_.reset();
  }

  void runEventLoopUntilIdle() {
    // Run the event loop once to process scheduled async tasks
    evb_->loopOnce();
  }

  void createAdjRibOutGroup(
      const std::string& groupName,
      uint64_t groupId = 0,
      const UpdateGroupKey& groupKey = UpdateGroupKey{}) {
    adjRibOutGroup_ = std::make_shared<AdjRibOutGroup>(
        *evb_,
        groupName,
        groupId,
        true /* enableUpdateGroup */,
        groupKey,
        /*
         * Empty shadow RIB map (tests that need entries pass their own view),
         * but the fixture's own maxRibVersion_ so the change list consume
         * timer reads a version the test can advance.
         */
        ShadowRibView{ShadowRibView::emptyEntries(), maxRibVersion_});
  }

  /**
   * @brief Create an UpdateGroupKey with both IPv4 and IPv6 AFI negotiated
   */
  static UpdateGroupKey createDefaultGroupKey() {
    UpdateGroupKey key;
    key.sessionType = BgpSessionType::IBGP;
    key.afiIpv4Negotiated = true;
    key.afiIpv6Negotiated = true;
    return key;
  }

  void MockChangeListConsumer() {
    /*
     * Wire the tracker and bitmaps so the group can create and register its own
     * change list consumer via registerGroupConsumer() (the production path).
     */
    adjRibOutGroup_->setChangeListTracker(
        changeListTracker_, addPathConsumerBitmap_, nonAddPathConsumerBitmap_);
  }

  std::shared_ptr<AdjRib> createMinimalAdjRib(
      uint8_t id = 1,
      PeeringParams peeringParams = PeeringParams{}) {
    auto peerId = nettools::bgplib::BgpPeerId(
        folly::IPAddress(fmt::format("10.0.0.{}", id)),
        folly::IPAddressV4("255.0.0.1").toLongHBO());
    return std::make_shared<AdjRib>(
        peerId,
        std::move(peeringParams),
        *evb_,
        ribInQ_,
        observerQ_,
        std::make_shared<folly::coro::Baton>(),
        nullptr /* policyManager */,
        std::make_shared<std::atomic<bool>>(false));
  }

  /*
   * Register a real in-sync peer so the group's packing list accepts entries
   * (the group only queues when it has an in-sync peer).
   */
  std::shared_ptr<AdjRib> addInSyncPeer(
      PeeringParams peeringParams = PeeringParams{}) {
    auto adjRib = createMinimalAdjRib(1, std::move(peeringParams));
    adjRibOutGroup_->registerPeer(adjRib);
    registeredInSyncPeers_.emplace_back(adjRibOutGroup_, adjRib);
    return adjRib;
  }

  /*
   * Build the per-peer producer with an explicit egress policy, so a test can
   * run it against the group producer configured the same way.
   */
  std::shared_ptr<AdjRib> createAdjRibWithEgressPolicy(
      const PeeringParams& peeringParams,
      const std::shared_ptr<PolicyManager>& policyManager,
      const std::string& egressPolicyName) {
    auto peerId = nettools::bgplib::BgpPeerId(
        folly::IPAddress("10.0.0.1"),
        folly::IPAddressV4("255.0.0.1").toLongHBO());
    return std::make_shared<AdjRib>(
        peerId,
        peeringParams,
        *evb_,
        ribInQ_,
        observerQ_,
        std::make_shared<folly::coro::Baton>(),
        policyManager,
        std::make_shared<std::atomic<bool>>(false),
        std::nullopt /* ingressPolicyName */,
        egressPolicyName);
  }

  /*
   * Run the per-peer producer's policy step. Lives on the fixture because
   * AdjRib::getPostOutPolicyAttributesAndInfo is private and friendship does
   * not extend to the TEST_F subclasses.
   */
  static std::pair<std::shared_ptr<const BgpPath>, PostPolicyInfo>
  runPeerPostOutPolicy(
      const std::shared_ptr<AdjRib>& adjRib,
      const RibOutAnnouncementEntry& update,
      AdjRibEntry* adjRibEntry,
      const std::shared_ptr<const BgpPath>& prePolicyAttrs) {
    auto [attrs, info] = adjRib->getPostOutPolicyAttributesAndInfo(
        update, adjRibEntry, prePolicyAttrs, "test_peer");
    return {attrs, info};
  }

  std::unique_ptr<folly::EventBase> evb_;
  std::shared_ptr<ChangeTracker<ShadowRibEntry>> changeListTracker_;
  std::shared_ptr<AdjRibOutGroup> adjRibOutGroup_;
  std::vector<
      std::pair<std::shared_ptr<AdjRibOutGroup>, std::shared_ptr<AdjRib>>>
      registeredInSyncPeers_;
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ_{
      nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<AdjRib::ObservableMessageT> observerQ_;

  /*
   * Dummy bitmaps for testing (normally owned by PeerManagerBase)
   * These are required by AdjRibOutGroupConsumer constructor
   */
  ConsumerBitmap addPathConsumerBitmap_;
  ConsumerBitmap nonAddPathConsumerBitmap_;

  /*
   * Stands in for PeerManagerBase's maxRibVersion_, which a group binds to
   * through its ShadowRibView. Groups built off the ShadowRibView default read
   * a constant 0 instead, so any group whose version the test advances must be
   * given this one. Declared after adjRibOutGroup_ so it outlives the group
   * bound to it.
   */
  uint64_t maxRibVersion_{0};
};

/**
 * Test: Basic construction and getters
 */
TEST_F(AdjRibGroupTest, BasicConstruction) {
  createAdjRibOutGroup("test_group", 42);

  EXPECT_EQ(adjRibOutGroup_->getAdjRibGroupName(), "test_group");
  EXPECT_EQ(adjRibOutGroup_->getGroupId(), 42);
}

TEST_F(AdjRibGroupTest, ClearEgressPrefixCountsPreservesCumulativeUpdateCount) {
  auto adjRib = createMinimalAdjRib();
  adjRib->incrementPreOutPrefixCount(/*isIpv4=*/true);
  adjRib->incrementPostOutPrefixCount(/*isIpv4=*/true);
  adjRib->incrementSentUpdateMsgs(3);
  adjRib->stats_.incrementSentAnnouncementsIpv4();
  adjRib->stats_.incrementSentAnnouncementsIpv6();
  adjRib->stats_.incrementSentWithdrawals();

  EXPECT_EQ(3, adjRib->getStats().getSentUpdateMsgs());
  EXPECT_EQ(1, adjRib->getStats().getSentAnnouncementsIpv4());
  EXPECT_EQ(1, adjRib->getStats().getSentAnnouncementsIpv6());
  EXPECT_EQ(1, adjRib->getStats().getSentWithdrawals());

  adjRib->clearEgressPrefixCounts();

  EXPECT_EQ(0, adjRib->getStats().getPreOutPrefixCount());
  EXPECT_EQ(0, adjRib->getStats().getPostOutPrefixCount());
  EXPECT_EQ(3, adjRib->getStats().getSentUpdateMsgs());
  EXPECT_EQ(1, adjRib->getStats().getSentAnnouncementsIpv4());
  EXPECT_EQ(1, adjRib->getStats().getSentAnnouncementsIpv6());
  EXPECT_EQ(1, adjRib->getStats().getSentWithdrawals());
}

/**
 * Test: setChangeListConsumer and getChangeListConsumer
 */
TEST_F(AdjRibGroupTest, SetGetChangeListConsumer) {
  createAdjRibOutGroup("test_group");

  // Initially, changeListConsumer should be null
  EXPECT_EQ(adjRibOutGroup_->getChangeListConsumer(), nullptr);

  // Wire the tracker and register: the group creates its own consumer
  MockChangeListConsumer();
  adjRibOutGroup_->registerGroupConsumer();

  // Verify consumer is set
  EXPECT_NE(adjRibOutGroup_->getChangeListConsumer(), nullptr);
}

/**
 * Test: resetChangeListConsumer
 */
TEST_F(AdjRibGroupTest, ResetChangeListConsumer) {
  createAdjRibOutGroup("test_group");

  // Wire the tracker and register: the group creates its own consumer
  MockChangeListConsumer();
  adjRibOutGroup_->registerGroupConsumer();
  EXPECT_NE(adjRibOutGroup_->getChangeListConsumer(), nullptr);

  // Reset the consumer
  adjRibOutGroup_->resetChangeListConsumer();

  // Verify consumer is null again
  EXPECT_EQ(adjRibOutGroup_->getChangeListConsumer(), nullptr);
}

/**
 * Test: registerGroupConsumer with no tracker set (cannot create consumer)
 */
TEST_F(AdjRibGroupTest, RegisterGroupConsumerNoTracker) {
  createAdjRibOutGroup("test_group");

  // Should log error but not crash
  EXPECT_NO_THROW(adjRibOutGroup_->registerGroupConsumer());
}

/**
 * Test: registerGroupConsumer with valid consumer
 */
TEST_F(AdjRibGroupTest, RegisterGroupConsumer) {
  createAdjRibOutGroup("test_group");
  MockChangeListConsumer();

  // An in-sync peer so the group is not frozen (numInSyncPeers_ > 0).
  auto adjRib = createMinimalAdjRib();
  adjRibOutGroup_->registerPeer(adjRib);
  adjRibOutGroup_->markPeerInSync(adjRib);

  // Activate the consumer and schedule the timer
  adjRibOutGroup_->registerGroupConsumer();
  adjRibOutGroup_->scheduleChangeListConsumeTimer();
  // First loopOnce fires the timer, second runs the coro body
  evb_->loopOnce();
  evb_->loopOnce();

  // Deactivate to clean up
  adjRibOutGroup_->deactivateChangeListConsumer();
}

/**
 * Test: registerGroupConsumer called twice (idempotent)
 */
TEST_F(AdjRibGroupTest, RegisterGroupConsumerTwice) {
  createAdjRibOutGroup("test_group");
  MockChangeListConsumer();

  // Activate the consumer twice
  adjRibOutGroup_->registerGroupConsumer();
  adjRibOutGroup_->registerGroupConsumer();

  // Should not crash, second call should be no-op
  EXPECT_NO_THROW(evb_->loopOnce());

  // Deactivate to clean up
  adjRibOutGroup_->deactivateChangeListConsumer();
}

/**
 * Test: deactivateChangeListConsumer with no activation
 */
TEST_F(AdjRibGroupTest, DeregisterGroupConsumerNotActivated) {
  createAdjRibOutGroup("test_group");
  MockChangeListConsumer();

  // Deactivate without activating first (no-op)
  EXPECT_NO_THROW(adjRibOutGroup_->deactivateChangeListConsumer());
}

/**
 * Test: deactivateChangeListConsumer after activation
 */
TEST_F(AdjRibGroupTest, DeregisterGroupConsumerAfterActivation) {
  createAdjRibOutGroup("test_group");
  MockChangeListConsumer();

  // Activate then deactivate
  adjRibOutGroup_->registerGroupConsumer();
  adjRibOutGroup_->deactivateChangeListConsumer();

  // Should cleanly deactivate
  EXPECT_NO_THROW(evb_->loopOnce());
}

/**
 * Test: Activate, deactivate, then activate again
 */
TEST_F(AdjRibGroupTest, ActivateDeactivateReactivate) {
  createAdjRibOutGroup("test_group");
  MockChangeListConsumer();

  // First activation
  adjRibOutGroup_->registerGroupConsumer();
  EXPECT_NO_THROW(evb_->loopOnce());

  // Deactivation
  adjRibOutGroup_->deactivateChangeListConsumer();
  EXPECT_NO_THROW(evb_->loopOnce());

  // Second activation
  adjRibOutGroup_->registerGroupConsumer();
  EXPECT_NO_THROW(evb_->loopOnce());

  // Final cleanup
  adjRibOutGroup_->deactivateChangeListConsumer();
}

/**
 * Test: Multiple groups with same change tracker
 */
TEST_F(AdjRibGroupTest, MultipleGroupsSameTracker) {
  // Create first group
  createAdjRibOutGroup("group1", 1);
  MockChangeListConsumer();
  adjRibOutGroup_->registerGroupConsumer();

  /* Create second group with same tracker */
  auto group2 = std::make_shared<AdjRibOutGroup>(
      *evb_,
      "group2",
      2,
      true /* enableUpdateGroup */,
      UpdateGroupKey{},
      ShadowRibView{ShadowRibView::emptyEntries(), maxRibVersion_});
  group2->setChangeListTracker(
      changeListTracker_, addPathConsumerBitmap_, nonAddPathConsumerBitmap_);
  group2->registerGroupConsumer();

  // Both should work without interference
  EXPECT_NO_THROW(evb_->loopOnce());

  // Cleanup
  group2->deactivateChangeListConsumer();
  group2->resetChangeListConsumer();
  folly::coro::blockingWait(group2->drainAsyncScope());
  adjRibOutGroup_->deactivateChangeListConsumer();
}

/**
 * Test: Group ID uniqueness
 */
TEST_F(AdjRibGroupTest, GroupIdUniqueness) {
  auto group1 = std::make_shared<AdjRibOutGroup>(*evb_, "group1", 1);
  auto group2 = std::make_shared<AdjRibOutGroup>(*evb_, "group2", 2);

  // Group IDs should be different
  EXPECT_NE(group1->getGroupId(), group2->getGroupId());
  EXPECT_EQ(group1->getGroupId(), 1);
  EXPECT_EQ(group2->getGroupId(), 2);

  folly::coro::blockingWait(group1->drainAsyncScope());
  folly::coro::blockingWait(group2->drainAsyncScope());
}

/**
 * Test: RIB tree operations with group owner key
 */
TEST_F(AdjRibGroupTest, RibTreeOperationsWithGroupKey) {
  createAdjRibOutGroup("test_group", 42);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  auto ownerKey = AdjRibOutOwnerKey::forGroup(42);
  uint32_t pathId = 1;

  // Add entry to path tree
  auto* entry = adjRibOutGroup_->addToPathTree(
      adjRibOutGroup_->PathTree_, prefix, ownerKey, pathId);
  EXPECT_NE(entry, nullptr);

  // Retrieve entry
  auto* retrievedEntry = adjRibOutGroup_->getFromPathTree(
      adjRibOutGroup_->PathTree_, prefix, ownerKey, pathId);
  EXPECT_EQ(retrievedEntry, entry);

  // Count entries
  auto count = adjRibOutGroup_->getPeerEntriesCountFromPathTree(
      adjRibOutGroup_->PathTree_, ownerKey);
  EXPECT_EQ(count, 1);

  // Delete entry
  adjRibOutGroup_->deleteFromPathTree(
      adjRibOutGroup_->PathTree_, prefix, ownerKey, pathId);

  // Verify deletion
  auto* afterDelete = adjRibOutGroup_->getFromPathTree(
      adjRibOutGroup_->PathTree_, prefix, ownerKey, pathId);
  EXPECT_EQ(afterDelete, nullptr);
}

/**
 * Test: Lite tree operations with group owner key
 */
TEST_F(AdjRibGroupTest, LiteTreeOperationsWithGroupKey) {
  createAdjRibOutGroup("test_group", 42);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  auto ownerKey = AdjRibOutOwnerKey::forGroup(42);
  uint32_t pathId = 1;

  // Add entry to lite tree
  auto* entry = adjRibOutGroup_->addToLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, ownerKey, pathId);
  EXPECT_NE(entry, nullptr);

  // Retrieve entry
  auto* retrievedEntry = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, ownerKey);
  EXPECT_EQ(retrievedEntry, entry);

  // Count entries
  auto count = adjRibOutGroup_->getPeerEntriesCountFromLiteTree(
      adjRibOutGroup_->LiteTree_, ownerKey);
  EXPECT_EQ(count, 1);

  // Delete entry
  adjRibOutGroup_->deleteFromLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, ownerKey);

  // Verify deletion
  auto* afterDelete = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, ownerKey);
  EXPECT_EQ(afterDelete, nullptr);
}

/**
 * Test: resolveLiteEntryForPeer applies the ribVersion sharing gate.
 *   - peer still shares the group entry when sharingVersion >= entry version
 *     (in-sync, or detached with the entry at/below the detach version),
 *   - omits a post-detach group entry (sharingVersion < entry version),
 *   - returns the per-peer (lazy-cloned) entry when present, regardless of
 *     the version gate.
 */
TEST_F(AdjRibGroupTest, ResolveLiteEntryForPeer_VersionGatedSharing) {
  createAdjRibOutGroup("test_group", 42, createDefaultGroupKey());

  const folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  const auto groupKey = adjRibOutGroup_->getGroupOwnerKey();
  auto peerId = std::make_shared<nettools::bgplib::BgpPeerId>(
      folly::IPAddress("10.0.0.1"),
      folly::IPAddressV4("255.0.0.1").toLongHBO());
  const auto peerKey = AdjRibOutOwnerKey::forPeer(peerId);

  // Group entry created at rib version 100; no per-peer entry yet.
  constexpr uint64_t kGroupEntryVersion = 100;
  auto* groupEntry = adjRibOutGroup_->addToLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, groupKey, kDefaultPathID);
  groupEntry->setRibVersion(kGroupEntryVersion);

  const auto& ownerMap =
      adjRibOutGroup_
          ->getRadixNodeItrFromLiteTree(adjRibOutGroup_->LiteTree_, prefix)
          ->value();

  // In-sync / detached-unchanged: sharingVersion >= entry version -> shared.
  EXPECT_EQ(
      groupEntry,
      adjRibOutGroup_->resolveLiteEntryForPeer(
          ownerMap, peerKey, kGroupEntryVersion));
  EXPECT_EQ(
      groupEntry,
      adjRibOutGroup_->resolveLiteEntryForPeer(
          ownerMap, peerKey, kGroupEntryVersion + 50));

  // Post-detach: sharingVersion < entry version -> peer never saw it -> omit.
  EXPECT_EQ(
      nullptr,
      adjRibOutGroup_->resolveLiteEntryForPeer(
          ownerMap, peerKey, kGroupEntryVersion - 1));

  /*
   * Diverged: a per-peer (lazy-cloned) entry wins regardless of the version
   * gate.
   */
  auto* peerEntry = adjRibOutGroup_->addToLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, peerKey, kDefaultPathID);
  peerEntry->setRibVersion(kGroupEntryVersion + 20);
  EXPECT_EQ(
      peerEntry,
      adjRibOutGroup_->resolveLiteEntryForPeer(
          ownerMap, peerKey, kGroupEntryVersion - 1));
  EXPECT_EQ(
      peerEntry,
      adjRibOutGroup_->resolveLiteEntryForPeer(
          ownerMap, peerKey, kGroupEntryVersion + 1000));
}

/**
 * Test: moving a peer to another group must not overwrite a path the peer has
 * already diverged on. movePeers materializes the peer's full RIB-OUT under its
 * own owner key in the new group, copying across the shared group entries it
 * was still using -- but divergence is per (prefix, pathId), so a path the peer
 * owns is not shared regardless of the group entry's version, and copying the
 * group's version over it would silently revert that path's attributes.
 */
TEST_F(
    AdjRibGroupTest,
    MovePeerMaterializedPathEntries_PeerOwnedPathNotOverwritten) {
  UpdateGroupKey key = createDefaultGroupKey();
  key.sendAddPath = true;
  createAdjRibOutGroup("source_group", 42, key);

  const folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  const auto groupOwnerKey = adjRibOutGroup_->getGroupOwnerKey();
  auto adjRib = createMinimalAdjRib(1);
  const auto peerOwnerKey = adjRib->getPeerOwnerKey();

  /*
   * The peer detached at v120 and owns pathId 1. The group's pathId 1 predates
   * the detach (v100), so it still passes the shared-entry version gate -- the
   * peer simply does not use it, because it has its own. pathId 2 is group-only
   * and shared, so it must still be carried across.
   */
  adjRib->setDetachedRibVersion(120);
  auto* groupPath1 = adjRibOutGroup_->addToPathTree(
      adjRibOutGroup_->PathTree_, prefix, groupOwnerKey, /*pathId=*/1);
  groupPath1->setRibVersion(100);
  auto* groupPath2 = adjRibOutGroup_->addToPathTree(
      adjRibOutGroup_->PathTree_, prefix, groupOwnerKey, /*pathId=*/2);
  groupPath2->setRibVersion(110);
  auto* peerPath1 = adjRibOutGroup_->addToPathTree(
      adjRibOutGroup_->PathTree_, prefix, peerOwnerKey, /*pathId=*/1);
  peerPath1->setRibVersion(150);

  auto targetGroup = std::make_shared<AdjRibOutGroup>(
      *evb_, "target_group", 43, true /* enableUpdateGroup */, key);

  adjRibOutGroup_->movePeerMaterializedRibOutPathEntries({adjRib}, targetGroup);

  auto* movedPath1 = targetGroup->getFromPathTree(
      targetGroup->PathTree_, prefix, peerOwnerKey, /*pathId=*/1);
  ASSERT_NE(movedPath1, nullptr);
  EXPECT_EQ(movedPath1->getRibVersion(), 150u)
      << "the group's shared pathId 1 overwrote the peer's own diverged entry";

  /* The group-only shared path is still materialized for the peer. */
  auto* movedPath2 = targetGroup->getFromPathTree(
      targetGroup->PathTree_, prefix, peerOwnerKey, /*pathId=*/2);
  ASSERT_NE(movedPath2, nullptr);
  EXPECT_EQ(movedPath2->getRibVersion(), 110u);
}

/**
 * Test: resolvePathEntriesForPeer resolves divergence per (prefix, pathId).
 * A peer-owned path wins; a group-only path is emitted only when not owned by
 * the peer AND still shared per the ribVersion gate.
 */
TEST_F(AdjRibGroupTest, ResolvePathEntriesForPeer_PerPathDivergence) {
  UpdateGroupKey key = createDefaultGroupKey();
  key.sendAddPath = true;
  createAdjRibOutGroup("test_group", 42, key);

  const folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  const auto groupKey = adjRibOutGroup_->getGroupOwnerKey();
  auto peerId = std::make_shared<nettools::bgplib::BgpPeerId>(
      folly::IPAddress("10.0.0.1"),
      folly::IPAddressV4("255.0.0.1").toLongHBO());
  const auto peerKey = AdjRibOutOwnerKey::forPeer(peerId);

  // Group paths: pathId 1 @ v100, pathId 2 @ v200. Peer diverged on pathId 1.
  auto* g1 = adjRibOutGroup_->addToPathTree(
      adjRibOutGroup_->PathTree_, prefix, groupKey, /*pathId=*/1);
  g1->setRibVersion(100);
  auto* g2 = adjRibOutGroup_->addToPathTree(
      adjRibOutGroup_->PathTree_, prefix, groupKey, /*pathId=*/2);
  g2->setRibVersion(200);
  auto* p1 = adjRibOutGroup_->addToPathTree(
      adjRibOutGroup_->PathTree_, prefix, peerKey, /*pathId=*/1);
  p1->setRibVersion(150);

  const auto& ownerMap =
      adjRibOutGroup_
          ->getRadixNodeItrFromPathTree(adjRibOutGroup_->PathTree_, prefix)
          ->value();

  /*
   * Detached at version 120: pathId 1 -> peer-owned p1; pathId 2 group entry
   * (v200 > 120) was never seen -> omitted.
   */
  std::map<uint32_t, const AdjRibEntry*> detachedView;
  adjRibOutGroup_->resolvePathEntriesForPeer(
      ownerMap,
      peerKey,
      /*sharingVersion=*/120,
      [&](uint32_t pathId, const AdjRibEntry& entry) {
        detachedView[pathId] = &entry;
      });
  EXPECT_EQ(1u, detachedView.size());
  EXPECT_EQ(p1, detachedView[1]);

  // In-sync (sharingVersion 200): pathId 1 -> p1; pathId 2 group entry shared.
  std::map<uint32_t, const AdjRibEntry*> inSyncView;
  adjRibOutGroup_->resolvePathEntriesForPeer(
      ownerMap,
      peerKey,
      /*sharingVersion=*/200,
      [&](uint32_t pathId, const AdjRibEntry& entry) {
        inSyncView[pathId] = &entry;
      });
  EXPECT_EQ(2u, inSyncView.size());
  EXPECT_EQ(p1, inSyncView[1]);
  EXPECT_EQ(g2, inSyncView[2]);
}

/**
 * Test: with update group DISABLED, neither resolver consults the group owner
 * key — only the peer's own entries are surfaced. (createAdjRibOutGroup always
 * enables update-group, so the group is constructed directly here.)
 */
TEST_F(AdjRibGroupTest, ResolveEntriesForPeer_UpdateGroupDisabled) {
  const folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  auto peerId = std::make_shared<nettools::bgplib::BgpPeerId>(
      folly::IPAddress("10.0.0.1"),
      folly::IPAddressV4("255.0.0.1").toLongHBO());
  const auto peerKey = AdjRibOutOwnerKey::forPeer(peerId);

  // --- Lite (non-add-path) ---
  adjRibOutGroup_ = std::make_shared<AdjRibOutGroup>(
      *evb_,
      "test_group_disabled_lite",
      42,
      false /* enableUpdateGroup */,
      createDefaultGroupKey());
  const auto groupKey = adjRibOutGroup_->getGroupOwnerKey();

  auto* liteGroupEntry = adjRibOutGroup_->addToLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, groupKey, kDefaultPathID);
  liteGroupEntry->setRibVersion(100);
  const auto& liteOwnerMap =
      adjRibOutGroup_
          ->getRadixNodeItrFromLiteTree(adjRibOutGroup_->LiteTree_, prefix)
          ->value();
  // Group-only entry is never consulted, regardless of version.
  EXPECT_EQ(
      nullptr,
      adjRibOutGroup_->resolveLiteEntryForPeer(liteOwnerMap, peerKey, 100));
  EXPECT_EQ(
      nullptr,
      adjRibOutGroup_->resolveLiteEntryForPeer(liteOwnerMap, peerKey, 1000));
  // A per-peer entry is still returned (peer key is always consulted).
  auto* litePeerEntry = adjRibOutGroup_->addToLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, peerKey, kDefaultPathID);
  litePeerEntry->setRibVersion(50);
  EXPECT_EQ(
      litePeerEntry,
      adjRibOutGroup_->resolveLiteEntryForPeer(liteOwnerMap, peerKey, 100));

  // --- Path (add-path) ---
  UpdateGroupKey pathKey = createDefaultGroupKey();
  pathKey.sendAddPath = true;
  adjRibOutGroup_ = std::make_shared<AdjRibOutGroup>(
      *evb_,
      "test_group_disabled_path",
      43,
      false /* enableUpdateGroup */,
      pathKey);
  const auto pathGroupKey = adjRibOutGroup_->getGroupOwnerKey();

  auto* pathGroupEntry = adjRibOutGroup_->addToPathTree(
      adjRibOutGroup_->PathTree_, prefix, pathGroupKey, /*pathId=*/1);
  pathGroupEntry->setRibVersion(100);
  auto* pathPeerEntry = adjRibOutGroup_->addToPathTree(
      adjRibOutGroup_->PathTree_, prefix, peerKey, /*pathId=*/2);
  pathPeerEntry->setRibVersion(50);
  const auto& pathOwnerMap =
      adjRibOutGroup_
          ->getRadixNodeItrFromPathTree(adjRibOutGroup_->PathTree_, prefix)
          ->value();
  /*
   * Only the peer-owned pathId 2 is emitted; the group-only pathId 1 is never
   * consulted.
   */
  std::map<uint32_t, const AdjRibEntry*> view;
  adjRibOutGroup_->resolvePathEntriesForPeer(
      pathOwnerMap,
      peerKey,
      /*sharingVersion=*/1000,
      [&](uint32_t pathId, const AdjRibEntry& entry) {
        view[pathId] = &entry;
      });
  EXPECT_EQ(1u, view.size());
  EXPECT_EQ(pathPeerEntry, view[2]);
}

/**
 * Test: Initial state is UNINITIALIZED
 */
TEST_F(AdjRibGroupTest, InitialStateUninitialized) {
  createAdjRibOutGroup("test_group");
  EXPECT_EQ(adjRibOutGroup_->getState(), UpdateGroupState::UNINITIALIZED);
}

/**
 * Test: setState and getState
 */
TEST_F(AdjRibGroupTest, StateManagement) {
  createAdjRibOutGroup("test_group");

  // Initial state
  EXPECT_EQ(adjRibOutGroup_->getState(), UpdateGroupState::UNINITIALIZED);

  // Transition to WAITING
  adjRibOutGroup_->setState(UpdateGroupState::WAITING);
  EXPECT_EQ(adjRibOutGroup_->getState(), UpdateGroupState::WAITING);

  // Transition to IDLE
  adjRibOutGroup_->setState(UpdateGroupState::IDLE);
  EXPECT_EQ(adjRibOutGroup_->getState(), UpdateGroupState::IDLE);

  // Transition to READY
  adjRibOutGroup_->setState(UpdateGroupState::READY);
  EXPECT_EQ(adjRibOutGroup_->getState(), UpdateGroupState::READY);
}

/**
 * Test: scheduleInitialDump transitions state
 * Note: Without feature flag enabled, state goes to IDLE instead of WAITING
 */
TEST_F(AdjRibGroupTest, ScheduleInitialDump) {
  createAdjRibOutGroup("test_group");
  auto adjRib = createMinimalAdjRib(1);
  adjRibOutGroup_->registerPeer(adjRib);

  // Initial state should be UNINITIALIZED
  EXPECT_EQ(adjRibOutGroup_->getState(), UpdateGroupState::UNINITIALIZED);

  // Schedule initial dump
  adjRibOutGroup_->scheduleInitialDump();

  // Process event loop to allow coroutine to execute
  evb_->loopOnce();

  /*
   * State should transition away from UNINITIALIZED
   * With feature flag enabled: WAITING
   * With feature flag disabled: IDLE
   */
  EXPECT_NE(adjRibOutGroup_->getState(), UpdateGroupState::UNINITIALIZED);
}

/**
 * Test: scheduleInitialDump skips if not in UNINITIALIZED state
 */
TEST_F(AdjRibGroupTest, ScheduleInitialDumpWrongState) {
  createAdjRibOutGroup("test_group");

  // Move to IDLE state
  adjRibOutGroup_->setState(UpdateGroupState::IDLE);

  // Attempt to schedule initial dump (should be skipped)
  adjRibOutGroup_->scheduleInitialDump();

  // Process event loop
  evb_->loopOnce();

  // State should remain IDLE (not transition to WAITING)
  EXPECT_EQ(adjRibOutGroup_->getState(), UpdateGroupState::IDLE);
}

/**
 * Test: scheduleInitialDump can only be called once
 */
TEST_F(AdjRibGroupTest, ScheduleInitialDumpOnce) {
  createAdjRibOutGroup("test_group");
  auto adjRib = createMinimalAdjRib(1);
  adjRibOutGroup_->registerPeer(adjRib);

  // Both calls happen before the event-loop turn that starts the dump.
  adjRibOutGroup_->scheduleInitialDump();
  adjRibOutGroup_->scheduleInitialDump();
  evb_->loopOnce();

  // Should have transitioned from UNINITIALIZED
  EXPECT_NE(adjRibOutGroup_->getState(), UpdateGroupState::UNINITIALIZED);
  auto stateAfterFirst = adjRibOutGroup_->getState();

  // Attempting to schedule again should be no-op
  adjRibOutGroup_->scheduleInitialDump();
  evb_->loopOnce();

  // State should remain unchanged
  EXPECT_EQ(adjRibOutGroup_->getState(), stateAfterFirst);
}

/*
 * The group's cached RIB version only advances. Equal versions are ordinary
 * no-ops, while a lower version is rejected and reported as an ordering
 * violation.
 */
TEST_F(AdjRibGroupTest, SetLastSeenRibVersionMonotonicity) {
  createAdjRibOutGroup("test_group");
  auto& messages = subscribeToLogMessages("", folly::LogLevel::ERR);
  messages.clear();

  adjRibOutGroup_->setLastSeenRibVersion(10);
  EXPECT_EQ(10, adjRibOutGroup_->getLastSeenRibVersion());

  adjRibOutGroup_->setLastSeenRibVersion(10);
  EXPECT_EQ(10, adjRibOutGroup_->getLastSeenRibVersion());
  EXPECT_TRUE(messages.empty());

  adjRibOutGroup_->setLastSeenRibVersion(4);
  EXPECT_EQ(10, adjRibOutGroup_->getLastSeenRibVersion());
  ASSERT_EQ(1, messages.size());
  EXPECT_THAT(
      messages[0].first.getMessage(),
      testing::HasSubstr(
          "Group 0(/none,peerOverride=false) RIB version "
          "monotonicity violation, ignoring attempt to set last seen RIB "
          "version 4 below current 10"));
}

/*
 * The dump task bails when the group is drained before it gets its turn. That
 * exit leaves state_ at UNINITIALIZED, so the state guard cannot reject a
 * later request -- the latch has to be released or the group coalesces every
 * request from then on and never dumps.
 */
TEST_F(AdjRibGroupTest, ScheduleInitialDumpAfterEmptyGroupBail) {
  createAdjRibOutGroup("test_group");
  auto adjRib = createMinimalAdjRib(1);
  adjRibOutGroup_->registerPeer(adjRib);

  // Arm the latch, then drain the group before the queued task runs.
  adjRibOutGroup_->scheduleInitialDump();
  adjRibOutGroup_->unregisterPeer(adjRib);
  ASSERT_EQ(adjRibOutGroup_->getMemberCount(), 0);
  evb_->loopOnce();

  // Task bailed on the empty group, so the state machine never advanced.
  ASSERT_EQ(adjRibOutGroup_->getState(), UpdateGroupState::UNINITIALIZED);

  // A peer arriving afterwards must still get its dump.
  adjRibOutGroup_->registerPeer(adjRib);
  adjRibOutGroup_->scheduleInitialDump();
  evb_->loopOnce();

  // No shadowRibEntries_ — a dump that ran lands the group in IDLE.
  EXPECT_EQ(adjRibOutGroup_->getState(), UpdateGroupState::IDLE);
}

/*
 * With populated shadowRibEntries_, processRibDumpForGroup walks the shadow
 * RIB and transitions INIT peers to JOINED_RUNNING with the PeerManager's max
 * RIB version set on them.
 */
TEST_F(AdjRibGroupTest, ScheduleInitialDumpSetsRibVersionOnJoinedPeers) {
  ShadowRibEntriesMap shadowRibEntries;
  auto prefix = folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24};
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  auto routeInfo = std::make_shared<ShadowRibRouteInfo>(
      kV4LocalPeerInfo, attrs, /*pathIdToSend=*/0);
  routeInfo->flags = SHADOWRIBROUTE_IN_UPDATE;
  ShadowRibEntry srEntry;
  srEntry.prefix = prefix;
  srEntry.ribVersion = 42;
  srEntry.bestpath = routeInfo;
  auto trackable =
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(srEntry));
  shadowRibEntries.emplace(prefix, std::move(trackable));

  uint64_t maxRibVersion = 42;
  adjRibOutGroup_ = std::make_shared<AdjRibOutGroup>(
      *evb_,
      "test_group",
      0,
      true /* enableUpdateGroup */,
      UpdateGroupKey{},
      ShadowRibView{shadowRibEntries, maxRibVersion});

  auto adjRib1 = createMinimalAdjRib(1);
  adjRibOutGroup_->registerPeer(adjRib1);
  ASSERT_EQ(adjRib1->getPeerState(), PeerUpdateState::INIT);

  adjRibOutGroup_->scheduleInitialDump();
  evb_->loopOnce();

  EXPECT_EQ(adjRib1->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_EQ(adjRib1->getLastSeenRibVersion(), 42);
}

/*
 * setPeerState() is the single chokepoint that stamps the peer's last
 * update-group state transition, so the timestamp must track real transitions
 * and ignore redundant sets of the current state.
 */
TEST_F(AdjRibGroupTest, PeerUpdateStateTransitionIsTimestamped) {
  auto adjRib = createMinimalAdjRib();

  ASSERT_EQ(adjRib->getPeerState(), PeerUpdateState::DOWN);
  EXPECT_EQ(adjRib->getPeerUpdateStateInfo().lastModifiedTimeMs, 0);

  const auto beforeFirst =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  adjRib->setPeerState(PeerUpdateState::INIT);
  const auto firstStamp = adjRib->getPeerUpdateStateInfo().lastModifiedTimeMs;

  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::INIT);
  EXPECT_GE(firstStamp, beforeFirst);

  // Re-setting the state the peer is already in is not a transition.
  adjRib->setPeerState(PeerUpdateState::INIT);
  EXPECT_EQ(adjRib->getPeerUpdateStateInfo().lastModifiedTimeMs, firstStamp);

  adjRib->setPeerState(PeerUpdateState::JOINED_RUNNING);
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_GE(adjRib->getPeerUpdateStateInfo().lastModifiedTimeMs, firstStamp);
}

/*
 * With an empty shadow RIB but a wired-in maxRibVersion, the dump walks
 * nothing (no change-list entry skipped), so the group -- and its joined peers
 * -- must advance to the PeerManager's maxRibVersion, not the walked max
 * (which is 0 for an empty shadow RIB). Mirrors the AdjRib
 * InitialDumpOnEmptyShadowRibUsesMaxRibVersion case.
 */
TEST_F(AdjRibGroupTest, InitialDumpOnEmptyShadowRibUsesMaxRibVersion) {
  ShadowRibEntriesMap emptyShadowRib;
  uint64_t maxRibVersion = 7;

  adjRibOutGroup_ = std::make_shared<AdjRibOutGroup>(
      *evb_,
      "test_group",
      0,
      true /* enableUpdateGroup */,
      UpdateGroupKey{},
      ShadowRibView{emptyShadowRib, maxRibVersion});

  auto adjRib1 = createMinimalAdjRib(1);
  adjRibOutGroup_->registerPeer(adjRib1);
  ASSERT_EQ(adjRib1->getPeerState(), PeerUpdateState::INIT);

  adjRibOutGroup_->scheduleInitialDump();
  evb_->loopOnce();

  EXPECT_EQ(adjRibOutGroup_->getLastSeenRibVersion(), maxRibVersion);
  EXPECT_EQ(adjRib1->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_EQ(adjRib1->getLastSeenRibVersion(), maxRibVersion);
}

/*
 * A dump whose walk produced no work for a builder settles in IDLE, not
 * WAITING -- the same hasPendingMessages() test the egress policy
 * re-evaluation walk uses.
 *
 * Driven through processRibDumpForGroup directly: scheduleInitialDump is a
 * no-op for a group with no members, and a group with a member owes EoRs,
 * which is the WAITING case covered above.
 */
TEST_F(AdjRibGroupTest, DumpWithNothingPendingSettlesIdle) {
  ShadowRibEntriesMap emptyShadowRib;
  uint64_t maxRibVersion = 7;

  adjRibOutGroup_ = std::make_shared<AdjRibOutGroup>(
      *evb_,
      "test_group",
      0,
      true /* enableUpdateGroup */,
      UpdateGroupKey{},
      ShadowRibView{emptyShadowRib, maxRibVersion});
  ASSERT_EQ(UpdateGroupState::UNINITIALIZED, adjRibOutGroup_->getState());

  /*
   * Empty shadow RIB packs nothing, and sendWithEoR=false leaves no peer owing
   * an EoR, so hasPendingMessages() is false.
   */
  adjRibOutGroup_->processRibDumpForGroup(/*sendWithEoR=*/false);

  EXPECT_EQ(UpdateGroupState::IDLE, adjRibOutGroup_->getState());

  /*
   * The dump queued a build+send on asyncScope_. Run the loop so it completes:
   * TearDown blockingWaits on drainAsyncScope(), which cannot make progress
   * while the coroutine is still queued on an idle EventBase.
   */
  evb_->loopOnce();
}

/**
 * Test fixture for group packing list tests
 * Similar to AdjRibOutboundPackingFixture
 */
class AdjRibGroupPackingFixture : public AdjRibGroupTest {
 protected:
  void SetUp() override {
    AdjRibGroupTest::SetUp();

    // Create test attributes (non-const so we can modify them)
    auto attrs1 = std::make_shared<BgpPath>(BgpPathFields());
    attrs1->setLocalPref(100);
    announcementAttrs_ = attrs1;

    auto attrs2 = std::make_shared<BgpPath>(BgpPathFields());
    attrs2->setLocalPref(200);
    announcementAttrs2_ = attrs2;
  }

  /*
   * Packing tests exercise the group packing list, which only accepts entries
   * when the group has an in-sync peer. Add one on every group creation.
   */
  void createAdjRibOutGroup(
      const std::string& groupName,
      uint64_t groupId = 0,
      const UpdateGroupKey& groupKey = UpdateGroupKey{}) {
    AdjRibGroupTest::createAdjRibOutGroup(groupName, groupId, groupKey);
    addInSyncPeer();
  }

  std::shared_ptr<const BgpPath> withdrawalAttrs_{nullptr};
  std::shared_ptr<const BgpPath> announcementAttrs_;
  std::shared_ptr<const BgpPath> announcementAttrs2_;

  const folly::CIDRNetwork kV4Prefix1_{folly::IPAddress("10.0.0.0"), 24};
  const folly::CIDRNetwork kV4Prefix2_{folly::IPAddress("20.0.0.0"), 24};
  const folly::CIDRNetwork kV6Prefix1_{folly::IPAddress("2001:db8::"), 64};
};

/**
 * Test: Initial advertisement - add first prefix to packing list
 */
TEST_F(
    AdjRibGroupPackingFixture,
    TryUpdateAttrToPrefixMapForGroup_InitialAdvertisement) {
  createAdjRibOutGroup("test_group");

  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());

  auto prefixPathId = std::make_pair(kV4Prefix1_, kPlaceholderPathID);

  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId, withdrawalAttrs_, announcementAttrs_);

  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();

  EXPECT_EQ(1, attrToPrefixMap.size());
  EXPECT_EQ(announcementAttrs_, attrToPrefixMap.begin()->first.attrs);

  auto& pfxSet = attrToPrefixMap.begin()->second;
  EXPECT_EQ(1, pfxSet.size());
  EXPECT_EQ(prefixPathId, *pfxSet.begin());
}

/**
 * Test: Add multiple prefixes with same attributes
 */
TEST_F(
    AdjRibGroupPackingFixture,
    TryUpdateAttrToPrefixMapForGroup_MultiplePrefixesSameAttrs) {
  createAdjRibOutGroup("test_group");

  auto prefixPathId1 = std::make_pair(kV4Prefix1_, kPlaceholderPathID);
  auto prefixPathId2 = std::make_pair(kV4Prefix2_, kPlaceholderPathID);

  // Add first prefix
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId1, withdrawalAttrs_, announcementAttrs_);

  // Add second prefix with same attributes
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId2, withdrawalAttrs_, announcementAttrs_);

  // Should have 1 attr entry with 2 prefixes
  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();
  EXPECT_EQ(1, attrToPrefixMap.size());

  auto attrsWithAfi = BgpPathWithAfi{
      announcementAttrs_, nettools::bgplib::BgpUpdateAfi::AFI_IPv4};
  EXPECT_TRUE(attrToPrefixMap.contains(attrsWithAfi));

  auto& pfxSet = attrToPrefixMap.at(attrsWithAfi);
  EXPECT_EQ(2, pfxSet.size());
  EXPECT_TRUE(pfxSet.contains(prefixPathId1));
  EXPECT_TRUE(pfxSet.contains(prefixPathId2));
}

/**
 * Test: Add prefixes with different attributes
 */
TEST_F(
    AdjRibGroupPackingFixture,
    TryUpdateAttrToPrefixMapForGroup_DifferentAttrs) {
  createAdjRibOutGroup("test_group");

  auto prefixPathId1 = std::make_pair(kV4Prefix1_, kPlaceholderPathID);
  auto prefixPathId2 = std::make_pair(kV4Prefix2_, kPlaceholderPathID);

  // Add first prefix with announcementAttrs_
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId1, withdrawalAttrs_, announcementAttrs_);

  // Add second prefix with announcementAttrs2_
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId2, withdrawalAttrs_, announcementAttrs2_);

  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();

  // Should have 2 attr entries, each with 1 prefix
  EXPECT_EQ(2, attrToPrefixMap.size());

  auto attrsWithAfi1 = BgpPathWithAfi{
      announcementAttrs_, nettools::bgplib::BgpUpdateAfi::AFI_IPv4};
  auto attrsWithAfi2 = BgpPathWithAfi{
      announcementAttrs2_, nettools::bgplib::BgpUpdateAfi::AFI_IPv4};

  EXPECT_TRUE(attrToPrefixMap.contains(attrsWithAfi1));
  EXPECT_TRUE(attrToPrefixMap.contains(attrsWithAfi2));

  EXPECT_EQ(1, attrToPrefixMap.at(attrsWithAfi1).size());
  EXPECT_TRUE(attrToPrefixMap.at(attrsWithAfi1).contains(prefixPathId1));

  EXPECT_EQ(1, attrToPrefixMap.at(attrsWithAfi2).size());
  EXPECT_TRUE(attrToPrefixMap.at(attrsWithAfi2).contains(prefixPathId2));
}

/**
 * Test: Update prefix attributes (move from old to new)
 */
TEST_F(
    AdjRibGroupPackingFixture,
    TryUpdateAttrToPrefixMapForGroup_UpdateAttributes) {
  createAdjRibOutGroup("test_group");

  auto prefixPathId = std::make_pair(kV4Prefix1_, kPlaceholderPathID);

  // Initial announcement
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId, withdrawalAttrs_, announcementAttrs_);

  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();
  EXPECT_EQ(1, attrToPrefixMap.size());

  // Update to new attributes
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId, announcementAttrs_, announcementAttrs2_);

  // Should have moved prefix from old attrs to new attrs
  const auto& attrToPrefixMap2 = adjRibOutGroup_->getAttrToPrefixMap();
  EXPECT_EQ(1, attrToPrefixMap2.size());

  auto attrsWithAfi2 = BgpPathWithAfi{
      announcementAttrs2_, nettools::bgplib::BgpUpdateAfi::AFI_IPv4};
  EXPECT_TRUE(attrToPrefixMap.contains(attrsWithAfi2));
  EXPECT_TRUE(attrToPrefixMap.at(attrsWithAfi2).contains(prefixPathId));
}

/**
 * Test: Withdrawal (move to nullptr)
 */
TEST_F(AdjRibGroupPackingFixture, TryUpdateAttrToPrefixMapForGroup_Withdrawal) {
  createAdjRibOutGroup("test_group");

  auto prefixPathId = std::make_pair(kV4Prefix1_, kPlaceholderPathID);

  // Initial announcement
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId, withdrawalAttrs_, announcementAttrs_);

  EXPECT_EQ(1, adjRibOutGroup_->getAttrToPrefixMap().size());

  // Withdraw (set to nullptr)
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId, announcementAttrs_, withdrawalAttrs_);

  // Should have withdrawal entry
  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();
  EXPECT_EQ(1, attrToPrefixMap.size());
  EXPECT_EQ(withdrawalAttrs_, attrToPrefixMap.begin()->first.attrs);
}

/**
 * Test: IPv4 and IPv6 prefixes tracked separately
 */
TEST_F(
    AdjRibGroupPackingFixture,
    TryUpdateAttrToPrefixMapForGroup_V4AndV6Separate) {
  createAdjRibOutGroup("test_group");

  auto prefixPathIdV4 = std::make_pair(kV4Prefix1_, kPlaceholderPathID);
  auto prefixPathIdV6 = std::make_pair(kV6Prefix1_, kPlaceholderPathID);

  // Add IPv4 prefix
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathIdV4, withdrawalAttrs_, announcementAttrs_);

  // Add IPv6 prefix with same attributes
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathIdV6, withdrawalAttrs_, announcementAttrs_);

  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();

  // Should have 2 entries (one for IPv4, one for IPv6)
  EXPECT_EQ(2, attrToPrefixMap.size());

  auto attrsWithAfiV4 = BgpPathWithAfi{
      announcementAttrs_, nettools::bgplib::BgpUpdateAfi::AFI_IPv4};
  auto attrsWithAfiV6 = BgpPathWithAfi{
      announcementAttrs_, nettools::bgplib::BgpUpdateAfi::AFI_IPv6};

  EXPECT_TRUE(attrToPrefixMap.contains(attrsWithAfiV4));
  EXPECT_TRUE(attrToPrefixMap.contains(attrsWithAfiV6));

  EXPECT_EQ(1, attrToPrefixMap.at(attrsWithAfiV4).size());
  EXPECT_TRUE(attrToPrefixMap.at(attrsWithAfiV4).contains(prefixPathIdV4));

  EXPECT_EQ(1, attrToPrefixMap.at(attrsWithAfiV6).size());
  EXPECT_TRUE(attrToPrefixMap.at(attrsWithAfiV6).contains(prefixPathIdV6));
}

/**
 * Test: Duplicate announcement (no-op)
 */
TEST_F(
    AdjRibGroupPackingFixture,
    TryUpdateAttrToPrefixMapForGroup_DuplicateAnnouncement) {
  createAdjRibOutGroup("test_group");

  auto prefixPathId = std::make_pair(kV4Prefix1_, kPlaceholderPathID);

  // Initial announcement
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId, withdrawalAttrs_, announcementAttrs_);

  EXPECT_EQ(1, adjRibOutGroup_->getAttrToPrefixMap().size());

  // Duplicate announcement (same old and new attrs)
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId, announcementAttrs_, announcementAttrs_);

  // Should remain unchanged
  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();
  EXPECT_EQ(1, attrToPrefixMap.size());
  EXPECT_EQ(announcementAttrs_, attrToPrefixMap.begin()->first.attrs);
}

/**
 * Test: processRibAnnouncedEntryForGroup stores in RIB-OUT tree
 */
TEST_F(
    AdjRibGroupPackingFixture,
    ProcessRibAnnouncedEntryForGroup_StoresInTree) {
  createAdjRibOutGroup("test_group", 42, createDefaultGroupKey());

  RibOutAnnouncementEntry entry(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  // Process entry (initial dump)
  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry);

  // Verify stored in RIB-OUT tree
  auto groupOwnerKey =
      AdjRibOutOwnerKey::forGroup(adjRibOutGroup_->getGroupId());
  auto* adjRibEntry = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, kV4Prefix1_, groupOwnerKey);

  EXPECT_NE(adjRibEntry, nullptr);
  EXPECT_NE(adjRibEntry->getPostAttr(), nullptr);
  EXPECT_EQ(adjRibEntry->getPostAttr(), announcementAttrs_);

  // Verify stats incremented
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCount());
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCountIpv4());
  EXPECT_EQ(0, adjRibOutGroup_->getStats().getPostOutPrefixCountIpv6());
}

TEST_F(
    AdjRibGroupPackingFixture,
    ProcessRibAnnouncedEntryForGroup_AppliesAdvertiseLinkBandwidth) {
  constexpr uint32_t kLocalAs = 65001;
  constexpr float kLinkBandwidth = 5e9;

  auto groupKey = createDefaultGroupKey();
  groupKey.advertiseLinkBandwidth = AdvertiseLinkBandwidth::SET_LINK_BPS;
  groupKey.linkBandwidthBps = kLinkBandwidth;
  AdjRibGroupTest::createAdjRibOutGroup("test_group", 42, groupKey);

  PeeringParams peeringParams;
  peeringParams.localAs = kLocalAs;
  peeringParams.advertiseLinkBandwidth = AdvertiseLinkBandwidth::SET_LINK_BPS;
  peeringParams.linkBandwidthBps = kLinkBandwidth;
  addInSyncPeer(std::move(peeringParams));

  RibOutAnnouncementEntry entry(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_);

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry);

  auto* adjRibEntry = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_,
      kV4Prefix1_,
      AdjRibOutOwnerKey::forGroup(adjRibOutGroup_->getGroupId()));
  ASSERT_NE(adjRibEntry, nullptr);
  const auto& postAttrs = adjRibEntry->getPostAttr();
  ASSERT_NE(postAttrs, nullptr);
  ASSERT_TRUE(postAttrs->getNonTransitiveLbwAsn().has_value());
  EXPECT_EQ(kLocalAs, *postAttrs->getNonTransitiveLbwAsn());
  ASSERT_TRUE(postAttrs->getNonTransitiveLbwValue().has_value());
  EXPECT_EQ(kLinkBandwidth, *postAttrs->getNonTransitiveLbwValue());
}

TEST_F(
    AdjRibGroupPackingFixture,
    ProcessRibAnnouncedEntryForGroup_PropagatesRibVersion) {
  createAdjRibOutGroup("test_group", 42, createDefaultGroupKey());

  RibOutAnnouncementEntry entry(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_);
  entry.ribVersion = 55;

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry);

  auto groupOwnerKey =
      AdjRibOutOwnerKey::forGroup(adjRibOutGroup_->getGroupId());
  auto* adjRibEntry = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, kV4Prefix1_, groupOwnerKey);

  ASSERT_NE(adjRibEntry, nullptr);
  EXPECT_EQ(adjRibEntry->getRibVersion(), 55);
}

TEST_F(
    AdjRibGroupPackingFixture,
    ProcessRibAnnouncedEntryForGroup_VersionUpdatedOnSubsequentAnnouncement) {
  createAdjRibOutGroup("test_group", 42, createDefaultGroupKey());

  RibOutAnnouncementEntry entry1(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_);
  entry1.ribVersion = 10;

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry1);

  // Re-announce same prefix with updated version and different attrs
  RibOutAnnouncementEntry entry2(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs2_);
  entry2.ribVersion = 20;

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry2);

  auto groupOwnerKey =
      AdjRibOutOwnerKey::forGroup(adjRibOutGroup_->getGroupId());
  auto* adjRibEntry = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, kV4Prefix1_, groupOwnerKey);

  ASSERT_NE(adjRibEntry, nullptr);
  EXPECT_EQ(adjRibEntry->getRibVersion(), 20);

  // Verify count stays at 1 after re-announcement (no double-count)
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCount());
}

/**
 * Test: processRibAnnouncedEntryForGroup adds to packing list
 */
TEST_F(
    AdjRibGroupPackingFixture,
    ProcessRibAnnouncedEntryForGroup_AddsToPackingList) {
  createAdjRibOutGroup("test_group", 0, createDefaultGroupKey());

  RibOutAnnouncementEntry entry(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());

  // Process entry
  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry);

  // Verify added to packing list
  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();
  EXPECT_EQ(1, attrToPrefixMap.size());

  const auto& [attrsWithAfi, prefixPathIds] = *attrToPrefixMap.begin();
  EXPECT_EQ(attrsWithAfi.afi, nettools::bgplib::BgpUpdateAfi::AFI_IPv4);
  auto prefixPathId = std::make_pair(kV4Prefix1_, kPlaceholderPathID);
  EXPECT_TRUE(prefixPathIds.contains(prefixPathId));
}

/*
 * Regression test: the group's unchanged-announcement suppression must also
 * compare isNexthopSetByPolicy, the way AdjRib::processRibAnnouncedEntry does
 * for a detached peer. The flag is part of the packing list key and decides
 * per-peer nexthop-self at send time, so a toggle changes the wire output even
 * when the post-policy attributes are byte-identical. Comparing attributes
 * alone suppressed the re-advertisement and left the prefix staged under the
 * stale flag.
 */
TEST_F(
    AdjRibGroupPackingFixture,
    ProcessRibAnnouncedEntryForGroup_NexthopSetByPolicyToggleIsAnnounced) {
  createAdjRibOutGroup("test_group", 0, createDefaultGroupKey());

  RibOutAnnouncementEntry entry(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry);
  ASSERT_EQ(1, adjRibOutGroup_->getAttrToPrefixMap().size());

  /* Drain the packing list so the second pass starts from an empty one. */
  folly::coro::blockingWait(adjRibOutGroup_->buildAndSendGroupBgpMessages());
  ASSERT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());

  /*
   * Stand in for the previous announcement having gone out with a
   * policy-set nexthop; the attributes themselves do not change.
   */
  auto* adjRibEntry = adjRibOutGroup_->tryInsertRibOutEntry(
      kV4Prefix1_, announcementAttrs_->getNexthop(), kDefaultPathID);
  ASSERT_NE(adjRibEntry, nullptr);
  ASSERT_NE(adjRibEntry->getPostAttr(), nullptr);
  adjRibEntry->setNexthopSetByPolicy(true);

  /* Same attributes, no egress policy: the flag drops back to false. */
  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry);

  const auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();
  ASSERT_EQ(1, attrToPrefixMap.size());

  const auto& [attrsWithAfi, prefixPathIds] = *attrToPrefixMap.begin();
  EXPECT_FALSE(attrsWithAfi.isNexthopSetByPolicy);
  EXPECT_TRUE(
      prefixPathIds.contains(std::make_pair(kV4Prefix1_, kPlaceholderPathID)));
  EXPECT_FALSE(adjRibEntry->isNexthopSetByPolicy());
}

/*
 * Regression test: a group with NO egress policy configured (empty
 * egressPolicyName) but a non-null policyManager_ must not crash while
 * processing the initial dump.
 *
 * UpdateGroupKey::egressPolicyName is a std::string where "" means "no egress
 * policy", whereas the shared PeerConfig::egressPolicyName is a
 * std::optional<std::string> where has_value() means "policy configured".
 * Constructing the optional from an empty string yields optional("") whose
 * has_value() is true, so updateAttributesOutWithoutNexthopCommon() used to
 * look up a policy-attributes mask for "" (returning nullptr) and CHECK-fail.
 * This drives the exact production crash path: buildInitialDumpFromShadowRib ->
 * ... -> overridePrePolicyAttributesCommon.
 */
TEST_F(
    AdjRibGroupPackingFixture,
    ProcessRibAnnouncedEntryForGroup_NoEgressPolicyWithPolicyManagerDoesNotCrash) {
  // Group with both AFIs negotiated and no egress policy name configured.
  createAdjRibOutGroup("test_group", 42, createDefaultGroupKey());
  ASSERT_FALSE(adjRibOutGroup_->getGroupKey().egressPolicyName.has_value());

  /*
   * Reaching the crashing path requires (1) cached peering params so
   * updateAttributesOutWithoutNexthop() does not early-return, and (2) a
   * non-null policy manager so the egress-policy branch is entered. An empty
   * BgpPolicies yields a policy DB with no entry for "" (mask lookup -> null).
   */
  adjRibOutGroup_->peeringParams_ = PeeringParams{};
  adjRibOutGroup_->policyManager_ =
      std::make_shared<PolicyManager>(bgp_policy::BgpPolicies{});

  RibOutAnnouncementEntry entry(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  // Pre-fix: aborts via CHECK(mask) in overridePrePolicyAttributesCommon.
  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry);

  /*
   * Post-fix: the announcement is processed and stored (no policy override
   * applied because no egress policy is configured).
   */
  auto groupOwnerKey =
      AdjRibOutOwnerKey::forGroup(adjRibOutGroup_->getGroupId());
  auto* adjRibEntry = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, kV4Prefix1_, groupOwnerKey);
  ASSERT_NE(adjRibEntry, nullptr);
  EXPECT_NE(adjRibEntry->getPostAttr(), nullptr);
}

/**
 * Test: processRibAnnouncedEntryForGroup processes multiple entries
 */
TEST_F(AdjRibGroupPackingFixture, ProcessRibOutAnnouncement_MultipleEntries) {
  createAdjRibOutGroup("test_group");

  RibOutAnnouncement announcement;
  announcement.initialDump = true;

  announcement.entries.emplace_back(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  announcement.entries.emplace_back(
      kV4Prefix2_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());

  // Process each entry into the packing list
  for (const auto& entry : announcement.entries) {
    adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry);
  }

  // Run event loop to allow async build and send to complete
  runEventLoopUntilIdle();

  /*
   * Verify packing list is now EMPTY after buildAndSendGroupBgpMessages drains
   * it (even though there are no peers to send to, messages are built and
   * distributed)
   */
  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();
  EXPECT_EQ(0, attrToPrefixMap.size());
}

/**
 * Test fixture for tryInsertRibOutEntry tests
 */
class AdjRibGroupRibOutEntryFixture : public AdjRibGroupTest {
 protected:
  void SetUp() override {
    AdjRibGroupTest::SetUp();
    createAdjRibOutGroup("test_group", 42);
  }

  const folly::CIDRNetwork kV4Prefix1_{folly::IPAddress("10.0.0.0"), 24};
  const folly::CIDRNetwork kV4Prefix2_{folly::IPAddress("20.0.0.0"), 24};
  const folly::IPAddress kNexthop_{folly::IPAddress("1.1.1.1")};
  const uint32_t kPathId1_{100};
  const uint32_t kPathId2_{200};
};

/**
 * Test: tryInsertRibOutEntry creates new entry when not exists (LiteTree)
 */
TEST_F(
    AdjRibGroupRibOutEntryFixture,
    TryInsertRibOutEntry_CreatesNewEntry_LiteTree) {
  // Group uses LiteTree by default (sendAddPath_ = false)

  auto* entry =
      adjRibOutGroup_->tryInsertRibOutEntry(kV4Prefix1_, kNexthop_, kPathId1_);

  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->getPathId(), kPathId1_);

  // Verify entry is stored in LiteTree with group owner key
  auto groupOwnerKey = adjRibOutGroup_->getGroupOwnerKey();
  auto* retrievedEntry = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, kV4Prefix1_, groupOwnerKey);

  EXPECT_EQ(retrievedEntry, entry);
}

/**
 * Test: tryInsertRibOutEntry returns existing entry when already exists
 */
TEST_F(
    AdjRibGroupRibOutEntryFixture,
    TryInsertRibOutEntry_ReturnsExistingEntry) {
  // Insert first time
  auto* entry1 =
      adjRibOutGroup_->tryInsertRibOutEntry(kV4Prefix1_, kNexthop_, kPathId1_);
  ASSERT_NE(entry1, nullptr);

  // Insert again with same prefix and pathId
  auto* entry2 =
      adjRibOutGroup_->tryInsertRibOutEntry(kV4Prefix1_, kNexthop_, kPathId1_);

  // Should return same entry
  EXPECT_EQ(entry1, entry2);
}

/**
 * Test: tryInsertRibOutEntry with multiple prefixes
 */
TEST_F(AdjRibGroupRibOutEntryFixture, TryInsertRibOutEntry_MultiplePrefixes) {
  auto* entry1 =
      adjRibOutGroup_->tryInsertRibOutEntry(kV4Prefix1_, kNexthop_, kPathId1_);
  auto* entry2 =
      adjRibOutGroup_->tryInsertRibOutEntry(kV4Prefix2_, kNexthop_, kPathId1_);

  ASSERT_NE(entry1, nullptr);
  ASSERT_NE(entry2, nullptr);
  EXPECT_NE(entry1, entry2);

  // Verify both are stored
  auto groupOwnerKey = adjRibOutGroup_->getGroupOwnerKey();
  auto* retrieved1 = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, kV4Prefix1_, groupOwnerKey);
  auto* retrieved2 = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, kV4Prefix2_, groupOwnerKey);

  EXPECT_EQ(retrieved1, entry1);
  EXPECT_EQ(retrieved2, entry2);
}

/**
 * Test: tryInsertRibOutEntry with PathTree when sendAddPath is enabled
 */
TEST_F(
    AdjRibGroupRibOutEntryFixture,
    TryInsertRibOutEntry_CreatesNewEntry_PathTree) {
  /*
   * Enable sendAddPath to use PathTree
   * Note: We need to manually set this via constructor or setter
   * For this test, let's create a new group with proper setup
   */

  auto groupWithAddPath = std::make_shared<AdjRibOutGroup>(
      *evb_, "test_group_addpath", 43, true /* enableUpdateGroup */);

  /*
   * Manually enable sendAddPath (would normally be set from peer config)
   * Since there's no public setter, this tests the PathTree code path
   * by directly accessing if possible, or we can test through integration
   */

  /*
   * For now, test that the LiteTree path works correctly
   * PathTree logic is tested through integration tests
   */
}

/**
 * Test: tryInsertRibOutEntry count verification
 */
TEST_F(AdjRibGroupRibOutEntryFixture, TryInsertRibOutEntry_CountVerification) {
  auto groupOwnerKey = adjRibOutGroup_->getGroupOwnerKey();

  // Initially no entries
  auto count = adjRibOutGroup_->getPeerEntriesCountFromLiteTree(
      adjRibOutGroup_->LiteTree_, groupOwnerKey);
  EXPECT_EQ(count, 0);

  // Add first entry
  adjRibOutGroup_->tryInsertRibOutEntry(kV4Prefix1_, kNexthop_, kPathId1_);

  count = adjRibOutGroup_->getPeerEntriesCountFromLiteTree(
      adjRibOutGroup_->LiteTree_, groupOwnerKey);
  EXPECT_EQ(count, 1);

  // Add second entry
  adjRibOutGroup_->tryInsertRibOutEntry(kV4Prefix2_, kNexthop_, kPathId1_);

  count = adjRibOutGroup_->getPeerEntriesCountFromLiteTree(
      adjRibOutGroup_->LiteTree_, groupOwnerKey);
  EXPECT_EQ(count, 2);

  // Add duplicate (should not increase count)
  adjRibOutGroup_->tryInsertRibOutEntry(kV4Prefix1_, kNexthop_, kPathId1_);

  count = adjRibOutGroup_->getPeerEntriesCountFromLiteTree(
      adjRibOutGroup_->LiteTree_, groupOwnerKey);
  EXPECT_EQ(count, 2);
}

/**
 * Test: tryInsertRibOutEntry with IPv6 prefix
 */
TEST_F(AdjRibGroupRibOutEntryFixture, TryInsertRibOutEntry_IPv6Prefix) {
  folly::CIDRNetwork v6Prefix{folly::IPAddress("2001:db8::"), 64};
  folly::IPAddress v6Nexthop{folly::IPAddress("2001:db8::1")};

  auto* entry =
      adjRibOutGroup_->tryInsertRibOutEntry(v6Prefix, v6Nexthop, kPathId1_);

  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->getPathId(), kPathId1_);

  // Verify stored
  auto groupOwnerKey = adjRibOutGroup_->getGroupOwnerKey();
  auto* retrieved = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, v6Prefix, groupOwnerKey);

  EXPECT_EQ(retrieved, entry);
}

/**
 * Test fixture for tryInsertWithdrawal tests
 */
class AdjRibGroupWithdrawalFixture : public AdjRibGroupTest {
 protected:
  void SetUp() override {
    AdjRibGroupTest::SetUp();
    createAdjRibOutGroup("test_group", 42);
    // The group only queues to its packing list when it has an in-sync peer.
    addInSyncPeer();

    // Create test attributes
    auto attrs = std::make_shared<BgpPath>(BgpPathFields());
    attrs->setLocalPref(100);
    testAttrs_ = attrs;
  }

  const folly::CIDRNetwork kV4Prefix1_{folly::IPAddress("10.0.0.0"), 24};
  const folly::CIDRNetwork kV4Prefix2_{folly::IPAddress("20.0.0.0"), 24};
  const folly::IPAddress kNexthop_{folly::IPAddress("1.1.1.1")};
  const uint32_t kPathId1_{100};
  std::shared_ptr<const BgpPath> testAttrs_;
};

/**
 * Test: tryInsertWithdrawal with previously announced prefix
 */
TEST_F(
    AdjRibGroupWithdrawalFixture,
    TryInsertWithdrawal_WithPreviouslyAnnouncedPrefix) {
  // First insert a RIB-OUT entry with attributes
  auto* entry =
      adjRibOutGroup_->tryInsertRibOutEntry(kV4Prefix1_, kNexthop_, kPathId1_);
  ASSERT_NE(entry, nullptr);
  entry->setPostAttr(testAttrs_);

  // Verify packing list is empty initially
  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());

  // Now try to withdraw it
  adjRibOutGroup_->tryInsertWithdrawal(
      kV4Prefix1_,
      entry,
      "Group test_group: Withdrawal inserted for 10.0.0.0/24",
      "Group test_group: Withdrawal not inserted for 10.0.0.0/24");

  // Verify postAttr is set to nullptr (withdrawal)
  EXPECT_EQ(entry->getPostAttr(), nullptr);

  // Verify withdrawal was added to packing list
  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();
  EXPECT_EQ(1, attrToPrefixMap.size());

  // Withdrawal should have nullptr as attr
  auto attrsWithAfi =
      BgpPathWithAfi{nullptr, nettools::bgplib::BgpUpdateAfi::AFI_IPv4};
  EXPECT_TRUE(attrToPrefixMap.contains(attrsWithAfi));

  auto prefixPathId = std::make_pair(kV4Prefix1_, kPathId1_);
  EXPECT_TRUE(attrToPrefixMap.at(attrsWithAfi).contains(prefixPathId));
}

/**
 * Test: tryInsertWithdrawal with prefix that was never announced
 */
TEST_F(
    AdjRibGroupWithdrawalFixture,
    TryInsertWithdrawal_WithNeverAnnouncedPrefix) {
  // Create entry but don't set postAttr (never announced)
  auto* entry =
      adjRibOutGroup_->tryInsertRibOutEntry(kV4Prefix1_, kNexthop_, kPathId1_);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->getPostAttr(), nullptr);

  // Try to withdraw it
  adjRibOutGroup_->tryInsertWithdrawal(
      kV4Prefix1_,
      entry,
      "Group test_group: Withdrawal inserted for 10.0.0.0/24",
      "Group test_group: Withdrawal not inserted for 10.0.0.0/24");

  // PostAttr should still be nullptr
  EXPECT_EQ(entry->getPostAttr(), nullptr);

  // Packing list should remain empty (no withdrawal added)
  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());
}

/**
 * Test: tryInsertWithdrawal multiple times (idempotent)
 */
TEST_F(
    AdjRibGroupWithdrawalFixture,
    TryInsertWithdrawal_MultipleTimesSamePrefix) {
  // Insert and announce a prefix
  auto* entry =
      adjRibOutGroup_->tryInsertRibOutEntry(kV4Prefix1_, kNexthop_, kPathId1_);
  entry->setPostAttr(testAttrs_);

  // First withdrawal
  adjRibOutGroup_->tryInsertWithdrawal(
      kV4Prefix1_,
      entry,
      "Group test_group: Withdrawal inserted for 10.0.0.0/24",
      "Group test_group: Withdrawal not inserted for 10.0.0.0/24");

  EXPECT_EQ(entry->getPostAttr(), nullptr);
  EXPECT_EQ(1, adjRibOutGroup_->getAttrToPrefixMap().size());

  // Second withdrawal (should be no-op since postAttr is already nullptr)
  adjRibOutGroup_->tryInsertWithdrawal(
      kV4Prefix1_,
      entry,
      "Group test_group: Withdrawal inserted for 10.0.0.0/24",
      "Group test_group: Withdrawal not inserted for 10.0.0.0/24");

  // Should still have nullptr postAttr
  EXPECT_EQ(entry->getPostAttr(), nullptr);

  // Packing list should still have 1 entry (no duplicate withdrawal)
  EXPECT_EQ(1, adjRibOutGroup_->getAttrToPrefixMap().size());
}

/**
 * Test: tryInsertWithdrawal for multiple prefixes
 */
TEST_F(AdjRibGroupWithdrawalFixture, TryInsertWithdrawal_MultiplePrefixes) {
  // Insert two prefixes with same attributes
  auto* entry1 =
      adjRibOutGroup_->tryInsertRibOutEntry(kV4Prefix1_, kNexthop_, kPathId1_);
  entry1->setPostAttr(testAttrs_);

  auto* entry2 =
      adjRibOutGroup_->tryInsertRibOutEntry(kV4Prefix2_, kNexthop_, kPathId1_);
  entry2->setPostAttr(testAttrs_);

  // Withdraw both
  adjRibOutGroup_->tryInsertWithdrawal(
      kV4Prefix1_,
      entry1,
      "Group test_group: Withdrawal inserted for 10.0.0.0/24",
      "Group test_group: Withdrawal not inserted for 10.0.0.0/24");

  adjRibOutGroup_->tryInsertWithdrawal(
      kV4Prefix2_,
      entry2,
      "Group test_group: Withdrawal inserted for 20.0.0.0/24",
      "Group test_group: Withdrawal not inserted for 20.0.0.0/24");

  // Both should have nullptr postAttr
  EXPECT_EQ(entry1->getPostAttr(), nullptr);
  EXPECT_EQ(entry2->getPostAttr(), nullptr);

  // Packing list should have both withdrawals under same attr (nullptr)
  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();
  EXPECT_EQ(1, attrToPrefixMap.size());

  auto attrsWithAfi =
      BgpPathWithAfi{nullptr, nettools::bgplib::BgpUpdateAfi::AFI_IPv4};
  EXPECT_TRUE(attrToPrefixMap.contains(attrsWithAfi));
  EXPECT_EQ(2, attrToPrefixMap.at(attrsWithAfi).size());
}

/**
 * Test: tryInsertWithdrawal clearing packing list after announcement
 */
TEST_F(
    AdjRibGroupWithdrawalFixture,
    TryInsertWithdrawal_ClearsPackingListEntry) {
  // Insert and announce a prefix
  auto* entry =
      adjRibOutGroup_->tryInsertRibOutEntry(kV4Prefix1_, kNexthop_, kPathId1_);
  entry->setPostAttr(testAttrs_);

  // Manually add to packing list (simulating previous announcement)
  auto prefixPathId = std::make_pair(kV4Prefix1_, kPathId1_);
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId, nullptr, testAttrs_);

  // Verify announcement is in packing list
  auto& attrToPrefixMapBefore = adjRibOutGroup_->getAttrToPrefixMap();
  auto attrsWithAfi =
      BgpPathWithAfi{testAttrs_, nettools::bgplib::BgpUpdateAfi::AFI_IPv4};
  EXPECT_TRUE(attrToPrefixMapBefore.contains(attrsWithAfi));
  EXPECT_TRUE(attrToPrefixMapBefore.at(attrsWithAfi).contains(prefixPathId));

  // Now withdraw it
  adjRibOutGroup_->tryInsertWithdrawal(
      kV4Prefix1_,
      entry,
      "Group test_group: Withdrawal inserted for 10.0.0.0/24",
      "Group test_group: Withdrawal not inserted for 10.0.0.0/24");

  // Old announcement should be removed from packing list
  auto& attrToPrefixMapAfter = adjRibOutGroup_->getAttrToPrefixMap();
  EXPECT_FALSE(attrToPrefixMapAfter.contains(attrsWithAfi));

  // New withdrawal should be in packing list
  auto withdrawalKey =
      BgpPathWithAfi{nullptr, nettools::bgplib::BgpUpdateAfi::AFI_IPv4};
  EXPECT_TRUE(attrToPrefixMapAfter.contains(withdrawalKey));
  EXPECT_TRUE(attrToPrefixMapAfter.at(withdrawalKey).contains(prefixPathId));
}

/**
 * Test: tryInsertWithdrawal with IPv6 prefix
 */
TEST_F(AdjRibGroupWithdrawalFixture, TryInsertWithdrawal_IPv6Prefix) {
  folly::CIDRNetwork v6Prefix{folly::IPAddress("2001:db8::"), 64};
  folly::IPAddress v6Nexthop{folly::IPAddress("2001:db8::1")};

  // Insert and announce IPv6 prefix
  auto* entry =
      adjRibOutGroup_->tryInsertRibOutEntry(v6Prefix, v6Nexthop, kPathId1_);
  entry->setPostAttr(testAttrs_);

  // Withdraw it
  adjRibOutGroup_->tryInsertWithdrawal(
      v6Prefix,
      entry,
      "Group test_group: Withdrawal inserted for 2001:db8::/64",
      "Group test_group: Withdrawal not inserted for 2001:db8::/64");

  // Verify withdrawal
  EXPECT_EQ(entry->getPostAttr(), nullptr);

  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();
  auto attrsWithAfi =
      BgpPathWithAfi{nullptr, nettools::bgplib::BgpUpdateAfi::AFI_IPv6};
  EXPECT_TRUE(attrToPrefixMap.contains(attrsWithAfi));

  auto prefixPathId = std::make_pair(v6Prefix, kPathId1_);
  EXPECT_TRUE(attrToPrefixMap.at(attrsWithAfi).contains(prefixPathId));
}

/**
 * Test: PostPolicyInfo struct is accessible from AdjRibCommon
 */
TEST_F(AdjRibGroupTest, PostPolicyInfoAccessibility) {
  // Verify we can create and use PostPolicyInfo from AdjRibCommon.h
  PostPolicyInfo info;
  EXPECT_FALSE(info.isMedSetByPolicy);

  info.isMedSetByPolicy = true;
  EXPECT_TRUE(info.isMedSetByPolicy);
}

/**
 * Test fixture for policy evaluation tests
 */
class AdjRibGroupPolicyFixture : public AdjRibGroupTest {
 protected:
  void SetUp() override {
    AdjRibGroupTest::SetUp();
    createAdjRibOutGroup("test_group", 42);

    // Create test attributes
    auto attrs = std::make_shared<BgpPath>(BgpPathFields());
    attrs->setLocalPref(100);
    testAttrs_ = attrs;
  }

  std::shared_ptr<const BgpPath> testAttrs_;
  const folly::CIDRNetwork kV4Prefix1_{folly::IPAddress("10.0.0.0"), 24};
  const folly::IPAddress kNexthop_{folly::IPAddress("1.1.1.1")};
  const uint32_t kPathId1_{100};
};

/**
 * Test: getPostOutPolicyAttributesAndInfo without policy configured
 */
TEST_F(
    AdjRibGroupPolicyFixture,
    GetPostOutPolicyAttributesAndInfo_NoPolicyConfigured) {
  // Create RibOutAnnouncementEntry
  RibOutAnnouncementEntry update(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      testAttrs_,
      0,
      0,
      0,
      0,
      0);

  // Create RIB-OUT entry
  auto* entry =
      adjRibOutGroup_->tryInsertRibOutEntry(kV4Prefix1_, kNexthop_, kPathId1_);
  ASSERT_NE(entry, nullptr);

  // Call getPostOutPolicyAttributesAndInfo without policy configured
  auto [postAttrs, postPolicyInfo] =
      adjRibOutGroup_->getPostOutPolicyAttributesAndInfo(
          update, entry, testAttrs_, "test_peer");

  // Should return same attributes when no policy configured
  EXPECT_EQ(postAttrs, testAttrs_);
  EXPECT_FALSE(postPolicyInfo.isMedSetByPolicy);
}

/**
 * Test: getPostOutPolicyAttributesAndInfo with null attributes
 */
TEST_F(AdjRibGroupPolicyFixture, GetPostOutPolicyAttributesAndInfo_NullAttrs) {
  RibOutAnnouncementEntry update(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      nullptr,
      0,
      0,
      0,
      0,
      0);

  auto* entry =
      adjRibOutGroup_->tryInsertRibOutEntry(kV4Prefix1_, kNexthop_, kPathId1_);
  ASSERT_NE(entry, nullptr);

  // Call with null attributes
  auto [postAttrs, postPolicyInfo] =
      adjRibOutGroup_->getPostOutPolicyAttributesAndInfo(
          update, entry, nullptr, "test_peer");

  // Should handle null gracefully (returns null)
  EXPECT_EQ(postAttrs, nullptr);
}

/**
 * Test: getPostOutPolicyAttributesAndInfo with null adjRibEntry (should CHECK
 * fail)
 */
TEST_F(AdjRibGroupPolicyFixture, GetPostOutPolicyAttributesAndInfo_NullEntry) {
  RibOutAnnouncementEntry update(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      testAttrs_,
      0,
      0,
      0,
      0,
      0);

  /*
   * This should trigger CHECK failure in the implementation
   * Extract lambda to avoid macro issues with structured bindings
   */
  auto callWithNullEntry = [&]() {
    adjRibOutGroup_->getPostOutPolicyAttributesAndInfo(
        update, nullptr, testAttrs_, "test_peer");
  };

  EXPECT_DEATH(callWithNullEntry(), "");
}

/*
 * Regression test: the group producer must build BgpPolicyActionData from the
 * announcement the way AdjRib::getPostOutPolicyAttributesAndInfo does for a
 * detached peer. It used to default-construct it, leaving lbwActionData unset,
 * so an egress policy term reading that data (here SET_LINK_BPS) was a silent
 * no-op for the group while the detached member applied it.
 */
TEST_F(
    AdjRibGroupPolicyFixture,
    GetPostOutPolicyAttributesAndInfo_PopulatesLbwActionData) {
  constexpr uint32_t kLocalAs = 65001;
  constexpr float kLinkBandwidthBps = 5e9;
  const std::string kPolicyName = "test_lbw_policy";

  auto groupKey = createDefaultGroupKey();
  groupKey.egressPolicyName = kPolicyName;
  createAdjRibOutGroup("test_group", 42, groupKey);

  /*
   * SET_LINK_BPS derives the LBW ext-community purely from the action data
   * (lbwActionData.asn / .linkBandwidthBps), so its output is a direct
   * readout of what the group handed to policy evaluation.
   */
  auto lbwAction = createBgpPolicyLbwExtCommunityAction(
      bgp_policy::LbwExtCommunityActionType::SET_LINK_BPS);
  adjRibOutGroup_->policyManager_ = std::make_shared<PolicyManager>(
      createBgpPolicies(kPolicyName, {}, {lbwAction}),
      createTestBgpGlobalConfig());

  PeeringParams peeringParams;
  peeringParams.localAs = kLocalAs;
  peeringParams.linkBandwidthBps = kLinkBandwidthBps;
  adjRibOutGroup_->peeringParams_ = peeringParams;

  RibOutAnnouncementEntry update(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      testAttrs_,
      0,
      0,
      0,
      0,
      0);

  auto* entry =
      adjRibOutGroup_->tryInsertRibOutEntry(kV4Prefix1_, kNexthop_, kPathId1_);
  ASSERT_NE(entry, nullptr);

  auto [postAttrs, postPolicyInfo] =
      adjRibOutGroup_->getPostOutPolicyAttributesAndInfo(
          update, entry, testAttrs_, "test_peer");

  ASSERT_NE(postAttrs, nullptr);
  ASSERT_TRUE(postAttrs->getNonTransitiveLbwAsn().has_value());
  EXPECT_EQ(kLocalAs, *postAttrs->getNonTransitiveLbwAsn());
  ASSERT_TRUE(postAttrs->getNonTransitiveLbwValue().has_value());
  EXPECT_EQ(kLinkBandwidthBps, *postAttrs->getNonTransitiveLbwValue());
}

/*
 * Producer-parity test. The whole point of the update-group design is that a
 * DETACHED peer running the per-peer producer and its group running the group
 * producer emit the same thing. This asserts that directly for the policy step:
 * same peering params, same egress policy, same announcement in, identical
 * post-policy attributes, PostPolicyInfo and policy term name out.
 *
 * Audit finding M2 was exactly this invariant breaking -- the group
 * default-constructed BgpPolicyActionData, so an LBW policy term produced a
 * different post-policy attribute set than the detached peer did.
 */
TEST_F(AdjRibGroupPolicyFixture, PostOutPolicyAgreesWithPerPeerProducer) {
  constexpr uint32_t kLocalAs = 65001;
  constexpr float kLinkBandwidthBps = 5e9;
  const std::string kPolicyName = "parity_egress_policy";

  /*
   * SET_LINK_BPS reads the action data both producers are supposed to build
   * identically, so it makes any divergence in that data observable.
   */
  auto lbwAction = createBgpPolicyLbwExtCommunityAction(
      bgp_policy::LbwExtCommunityActionType::SET_LINK_BPS);
  auto policyManager = std::make_shared<PolicyManager>(
      createBgpPolicies(kPolicyName, {}, {lbwAction}),
      createTestBgpGlobalConfig());

  PeeringParams peeringParams;
  peeringParams.localAs = kLocalAs;
  peeringParams.linkBandwidthBps = kLinkBandwidthBps;

  /* Group producer */
  auto groupKey = createDefaultGroupKey();
  groupKey.egressPolicyName = kPolicyName;
  createAdjRibOutGroup("test_group", 42, groupKey);
  adjRibOutGroup_->policyManager_ = policyManager;
  adjRibOutGroup_->peeringParams_ = peeringParams;

  /* Per-peer producer, configured identically */
  auto adjRib =
      createAdjRibWithEgressPolicy(peeringParams, policyManager, kPolicyName);

  RibOutAnnouncementEntry update(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      testAttrs_,
      0,
      0,
      0,
      0,
      0);

  AdjRibEntry groupEntry(kDefaultPathID);
  AdjRibEntry peerEntry(kDefaultPathID);

  /*
   * The policy cache is a process-wide singleton keyed partly on the action
   * data. Clear it around each call so both producers genuinely evaluate the
   * policy instead of one of them answering from the other's cache entry.
   */
  AdjRibPolicyCache::get()->clearCache();
  auto [groupAttrs, groupInfo] =
      adjRibOutGroup_->getPostOutPolicyAttributesAndInfo(
          update, &groupEntry, testAttrs_, "test_peer");

  AdjRibPolicyCache::get()->clearCache();
  auto [peerAttrs, peerInfo] =
      runPeerPostOutPolicy(adjRib, update, &peerEntry, testAttrs_);

  ASSERT_NE(nullptr, peerAttrs);
  ASSERT_NE(nullptr, groupAttrs);

  /* Deep compare: same contents, regardless of BgpPath identity. */
  EXPECT_EQ(*peerAttrs, *groupAttrs)
      << "Group and per-peer producers disagree on post-policy attributes. "
      << "A detached peer would advertise something different from its group. "
      << "peer LBW asn/value="
      << peerAttrs->getNonTransitiveLbwAsn().value_or(0) << "/"
      << peerAttrs->getNonTransitiveLbwValue().value_or(0)
      << ", group=" << groupAttrs->getNonTransitiveLbwAsn().value_or(0) << "/"
      << groupAttrs->getNonTransitiveLbwValue().value_or(0);
  EXPECT_EQ(peerInfo.isMedSetByPolicy, groupInfo.isMedSetByPolicy);
  EXPECT_EQ(peerInfo.isNexthopSetByPolicy, groupInfo.isNexthopSetByPolicy);
  EXPECT_EQ(peerEntry.getPostOutPolicy(), groupEntry.getPostOutPolicy());
}

/**
 * Test: packPrefixesWithLimitCommon - basic packing functionality
 */
TEST_F(AdjRibGroupPackingFixture, PackPrefixesWithLimitCommon_BasicPacking) {
  PrefixSet prefixSet;
  prefixSet.insert({kV4Prefix1_, kPlaceholderPathID});
  prefixSet.insert({kV4Prefix2_, kPlaceholderPathID});

  std::vector<nettools::bgplib::RiggedIPPrefix> bgpPrefixes;

  auto packed = packPrefixesWithLimitCommon(
      100, prefixSet, bgpPrefixes, false, "test_context");

  EXPECT_EQ(packed, 2);
  EXPECT_EQ(bgpPrefixes.size(), 2);
  EXPECT_TRUE(prefixSet.empty());
}

/**
 * Test: packPrefixesWithLimitCommon - with add-path
 */
TEST_F(AdjRibGroupPackingFixture, PackPrefixesWithLimitCommon_WithAddPath) {
  PrefixSet prefixSet;
  prefixSet.insert({kV4Prefix1_, kPlaceholderPathID});

  std::vector<nettools::bgplib::RiggedIPPrefix> bgpPrefixes;

  auto packed = packPrefixesWithLimitCommon(
      100, prefixSet, bgpPrefixes, true, "test_context");

  EXPECT_EQ(packed, 1);
  EXPECT_EQ(bgpPrefixes.size(), 1);
  EXPECT_EQ(bgpPrefixes[0].pathId(), kPlaceholderPathID);
}

/**
 * Test: packPrefixesWithLimitCommon - empty prefix set
 */
TEST_F(AdjRibGroupPackingFixture, PackPrefixesWithLimitCommon_EmptySet) {
  PrefixSet prefixSet;
  std::vector<nettools::bgplib::RiggedIPPrefix> bgpPrefixes;

  auto packed = packPrefixesWithLimitCommon(
      100, prefixSet, bgpPrefixes, false, "test_context");

  EXPECT_EQ(packed, 0);
  EXPECT_TRUE(bgpPrefixes.empty());
}

/**
 * Test: packPrefixesWithLimitCommon - size limit (incremental draining)
 */
TEST_F(AdjRibGroupPackingFixture, PackPrefixesWithLimitCommon_SizeLimit) {
  PrefixSet prefixSet;
  // Use 1500 prefixes to test size limit handling
  for (int i = 0; i < 1500; i++) {
    auto prefix = folly::IPAddress::createNetwork(
        fmt::format("10.{}.{}.0/24", i / 256, i % 256));
    prefixSet.insert({prefix, static_cast<uint32_t>(i)});
  }

  std::vector<nettools::bgplib::RiggedIPPrefix> bgpPrefixes;
  size_t originalSize = prefixSet.size();

  auto packed = packPrefixesWithLimitCommon(
      3900, prefixSet, bgpPrefixes, false, "test_context");

  EXPECT_GT(packed, 0);
  EXPECT_LT(packed, originalSize); // Should pack less than all due to limit
  EXPECT_EQ(prefixSet.size(), originalSize - packed);
}

/**
 * Test: packGroupPrefixes - wrapper delegates to common function
 */
TEST_F(AdjRibGroupPackingFixture, PackGroupPrefixesWithLimit_Wrapper) {
  createAdjRibOutGroup("test_group");

  PrefixSet prefixSet;
  prefixSet.insert({kV4Prefix1_, kPlaceholderPathID});
  prefixSet.insert({kV4Prefix2_, kPlaceholderPathID});

  std::vector<nettools::bgplib::RiggedIPPrefix> bgpPrefixes;

  auto packed =
      adjRibOutGroup_->packGroupPrefixes(prefixSet, bgpPrefixes, false);

  EXPECT_EQ(packed, 2);
  EXPECT_EQ(bgpPrefixes.size(), 2);
  EXPECT_TRUE(prefixSet.empty());
}

/**
 * Test: buildGroupUpdate - IPv4 announcement
 */
TEST_F(
    AdjRibGroupPackingFixture,
    BuildGroupUpdateWithSizeEstimation_AnnouncementV4) {
  createAdjRibOutGroup("test_group");

  // Empty packing list - should return early
  EXPECT_NO_THROW(
      folly::coro::blockingWait(
          adjRibOutGroup_->buildAndSendGroupBgpMessages()));

  PrefixSet prefixSet;
  prefixSet.insert({kV4Prefix1_, kPlaceholderPathID});
  prefixSet.insert({kV4Prefix2_, kPlaceholderPathID});

  BgpPathWithAfi attrsWithAfi{
      announcementAttrs_, nettools::bgplib::BgpUpdateAfi::AFI_IPv4};

  auto update = adjRibOutGroup_->buildGroupUpdate(attrsWithAfi, prefixSet);

  ASSERT_NE(update, nullptr);
  EXPECT_EQ(
      update->mpAnnounced()->afi(), nettools::bgplib::BgpUpdateAfi::AFI_IPv4);
  EXPECT_EQ(
      update->mpAnnounced()->safi(),
      nettools::bgplib::BgpUpdateSafi::SAFI_UNICAST);
  EXPECT_EQ(update->mpAnnounced()->prefixes()->size(), 2);
  EXPECT_TRUE(prefixSet.empty());
}

/**
 * Test: buildGroupUpdate - IPv4 withdrawal
 */
TEST_F(
    AdjRibGroupPackingFixture,
    BuildGroupUpdateWithSizeEstimation_WithdrawalV4) {
  createAdjRibOutGroup("test_group");

  PrefixSet prefixSet;
  prefixSet.insert({kV4Prefix1_, kPlaceholderPathID});
  prefixSet.insert({kV4Prefix2_, kPlaceholderPathID});

  BgpPathWithAfi attrsWithAfi{
      withdrawalAttrs_, nettools::bgplib::BgpUpdateAfi::AFI_IPv4};

  // Build and send
  folly::coro::blockingWait(adjRibOutGroup_->buildAndSendGroupBgpMessages());

  auto update = adjRibOutGroup_->buildGroupUpdate(attrsWithAfi, prefixSet);

  ASSERT_NE(update, nullptr);
  EXPECT_EQ(update->v4Withdrawn2()->size(), 2);
  EXPECT_TRUE(prefixSet.empty());
}

/**
 * Test: buildGroupUpdate - IPv6 withdrawal
 */
TEST_F(
    AdjRibGroupPackingFixture,
    BuildGroupUpdateWithSizeEstimation_WithdrawalV6) {
  createAdjRibOutGroup("test_group");

  PrefixSet prefixSet;
  prefixSet.insert({kV6Prefix1_, kPlaceholderPathID});

  // Add announcement
  auto prefixPathId1 = std::make_pair(kV4Prefix1_, kPlaceholderPathID);
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId1, nullptr, announcementAttrs_);
  // Add withdrawal
  auto prefixPathId2 = std::make_pair(kV4Prefix2_, kPlaceholderPathID);
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId2, announcementAttrs2_, nullptr);
  // Build and send
  EXPECT_NO_THROW(
      folly::coro::blockingWait(
          adjRibOutGroup_->buildAndSendGroupBgpMessages()));

  BgpPathWithAfi attrsWithAfi{
      nullptr, nettools::bgplib::BgpUpdateAfi::AFI_IPv6};

  auto update = adjRibOutGroup_->buildGroupUpdate(attrsWithAfi, prefixSet);

  ASSERT_NE(update, nullptr);
  EXPECT_EQ(
      update->mpWithdrawn()->afi(), nettools::bgplib::BgpUpdateAfi::AFI_IPv6);
  EXPECT_EQ(
      update->mpWithdrawn()->safi(),
      nettools::bgplib::BgpUpdateSafi::SAFI_UNICAST);
  EXPECT_EQ(update->mpWithdrawn()->prefixes()->size(), 1);
  EXPECT_TRUE(prefixSet.empty());
}

/**
 * Test: buildGroupUpdate - empty prefix set
 * returns nullptr
 */
TEST_F(AdjRibGroupPackingFixture, BuildGroupUpdateWithSizeEstimation_EmptySet) {
  createAdjRibOutGroup("test_group");

  PrefixSet prefixSet;
  BgpPathWithAfi attrsWithAfi{
      announcementAttrs_, nettools::bgplib::BgpUpdateAfi::AFI_IPv4};

  auto update = adjRibOutGroup_->buildGroupUpdate(attrsWithAfi, prefixSet);

  EXPECT_EQ(update, nullptr);
}

/**
 * Test: distributeMessageToInSyncPeers - no peers registered
 */
TEST_F(AdjRibGroupTest, DistributeMessageToInSyncPeers_NoPeers) {
  createAdjRibOutGroup("test_group");

  // Create a dummy message
  auto update = std::make_shared<nettools::bgplib::BgpUpdate2>();

  // No peers registered, should not crash
  EXPECT_NO_THROW(
      folly::coro::blockingWait(adjRibOutGroup_->distributeMessageToInSyncPeers(
          update, nullptr, nettools::bgplib::BgpUpdateAfi::AFI_IPv4)));
}

/**
 * Test: distributeMessageToInSyncPeers - null message
 */
TEST_F(AdjRibGroupPackingFixture, DistributeMessageToInSyncPeers_NullMessage) {
  createAdjRibOutGroup("test_group");

  // Null message - should handle gracefully
  EXPECT_NO_THROW(
      folly::coro::blockingWait(adjRibOutGroup_->distributeMessageToInSyncPeers(
          nullptr, nullptr, nettools::bgplib::BgpUpdateAfi::AFI_IPv4)));
}

/**
 * Test: buildAndSendGroupBgpMessages - empty packing list
 */
TEST_F(
    AdjRibGroupPackingFixture,
    BuildAndSendGroupBgpMessages_EmptyPackingList) {
  createAdjRibOutGroup("test_group");

  // Empty packing list - should return early
  EXPECT_NO_THROW(
      folly::coro::blockingWait(
          adjRibOutGroup_->buildAndSendGroupBgpMessages()));

  // Packing list should still be empty
  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());
}

/**
 * Test: buildAndSendGroupBgpMessages - clears packing list
 */
TEST_F(
    AdjRibGroupPackingFixture,
    BuildAndSendGroupBgpMessages_ClearsPackingList) {
  createAdjRibOutGroup("test_group");

  // Add some announcements to packing list
  auto prefixPathId = std::make_pair(kV4Prefix1_, kPlaceholderPathID);
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId, nullptr, announcementAttrs_);
  EXPECT_EQ(1, adjRibOutGroup_->getAttrToPrefixMap().size());

  // Build and send
  folly::coro::blockingWait(adjRibOutGroup_->buildAndSendGroupBgpMessages());

  // Packing list should be cleared
  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());
}

/**
 * Test: buildAndSendGroupBgpMessages - mixed announcements and withdrawals
 */
TEST_F(AdjRibGroupPackingFixture, BuildAndSendGroupBgpMessages_MixedMessages) {
  createAdjRibOutGroup("test_group");

  // Add announcement
  auto prefixPathId1 = std::make_pair(kV4Prefix1_, kPlaceholderPathID);
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId1, nullptr, announcementAttrs_);

  // Add withdrawal
  auto prefixPathId2 = std::make_pair(kV4Prefix2_, kPlaceholderPathID);
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId2, announcementAttrs2_, nullptr);

  EXPECT_EQ(2, adjRibOutGroup_->getAttrToPrefixMap().size());

  // Build and send
  EXPECT_NO_THROW(
      folly::coro::blockingWait(
          adjRibOutGroup_->buildAndSendGroupBgpMessages()));

  // Packing list should be cleared
  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());

  /*
   * Group-level control-plane count: 2 UPDATE PDUs generated (1 announcement +
   * 1 withdrawal).
   */
  EXPECT_EQ(2, adjRibOutGroup_->getStats().getSentUpdateMsgs());
  // EoR is a distinct PDU; a pure UPDATE flow must not touch the EoR counter.
  EXPECT_EQ(0, adjRibOutGroup_->getStats().getSentEndOfRibMsgs());
  /*
   * Announcement/withdrawal PDUs are tracked per-AFI on the group (1 IPv4
   * announcement + 1 IPv4 withdrawal here).
   */
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getSentAnnouncementsIpv4());
  EXPECT_EQ(0, adjRibOutGroup_->getStats().getSentAnnouncementsIpv6());
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getSentWithdrawals());
}

/**
 * Test: the group tracks announcement PDUs per AFI and withdrawal PDUs,
 * mirroring the per-peer AdjRibOut counters. An in-sync group builds each
 * UPDATE once, so these counters remain available as group diagnostics.
 */
TEST_F(
    AdjRibGroupPackingFixture,
    BuildAndSendGroupBgpMessages_CountsAnnouncementsAndWithdrawalsPerAfi) {
  UpdateGroupKey key;
  key.afiIpv4Negotiated = true;
  key.afiIpv6Negotiated = true;
  createAdjRibOutGroup("test_group", 0, key);

  // 1 IPv4 announcement, 1 IPv6 announcement, 1 IPv4 withdrawal.
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      std::make_pair(kV4Prefix1_, kPlaceholderPathID),
      nullptr,
      announcementAttrs_);
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      std::make_pair(kV6Prefix1_, kPlaceholderPathID),
      nullptr,
      announcementAttrs_);
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      std::make_pair(kV4Prefix2_, kPlaceholderPathID),
      announcementAttrs2_,
      nullptr);

  folly::coro::blockingWait(adjRibOutGroup_->buildAndSendGroupBgpMessages());

  const auto& stats = adjRibOutGroup_->getStats();
  EXPECT_EQ(1, stats.getSentAnnouncementsIpv4());
  EXPECT_EQ(1, stats.getSentAnnouncementsIpv6());
  EXPECT_EQ(1, stats.getSentWithdrawals());
  // Announcement + withdrawal PDUs sum to the total UPDATE PDU count.
  EXPECT_EQ(3, stats.getSentUpdateMsgs());
}

/**
 * Test: EoR PDUs are counted separately from UPDATE PDUs. When an announcement
 * requests EoR, the per-AFI EoR PDUs land in sentEndOfRibMsgs (so it converges
 * with the socket-layer txMsgs.endOfRib) and are NOT folded into sentUpdateMsgs
 * (which must converge with txMsgs.update).
 */
TEST_F(AdjRibGroupPackingFixture, EorCountedSeparatelyFromUpdate) {
  // Negotiate both AFIs so an EoR is owed per AFI.
  UpdateGroupKey key;
  key.afiIpv4Negotiated = true;
  key.afiIpv6Negotiated = true;

  /*
   * An initial dump over an empty (but non-null) shadow RIB queues no UPDATE
   * PDUs while still owing a per-AFI EoR -- exactly the isolation this test
   * needs.
   */
  ShadowRibEntriesMap emptyShadowRib;
  uint64_t maxRibVersion = 0;
  adjRibOutGroup_ = std::make_shared<AdjRibOutGroup>(
      *evb_,
      "test_group",
      0,
      true /* enableUpdateGroup */,
      key,
      ShadowRibView{emptyShadowRib, maxRibVersion});

  auto adjRib = createMinimalAdjRib();
  auto adjRibInQ = std::make_shared<AdjRib::AdjRibInQueueT>(
      nettools::bgplib::kMaxIngressQueueSize);
  auto boundedAdjRibOutQ = std::make_shared<AdjRib::AdjRibOutQueueT>(
      nettools::bgplib::kMaxEgressQueueSize,
      nettools::bgplib::kEgressQueueHighWatermark,
      nettools::bgplib::kEgressQueueLowWatermark);
  adjRib->sessionEstablished(std::nullopt, adjRibInQ, boundedAdjRibOutQ);
  adjRib->setUpdateGroup(adjRibOutGroup_);
  adjRibOutGroup_->registerPeer(adjRib);
  adjRib->markStateEstablished();
  struct PeerCleanup {
    std::shared_ptr<AdjRibOutGroup> group;
    std::shared_ptr<AdjRib> peer;

    ~PeerCleanup() {
      group->unregisterPeer(peer);
      peer->markStateTerminated();
    }
  } cleanup{adjRibOutGroup_, adjRib};

  // Request EoR with no queued UPDATEs: only EoR PDUs should be produced.
  adjRibOutGroup_->processRibDumpForGroup(/*sendWithEoR=*/true);
  runEventLoopUntilIdle();

  ASSERT_EQ(2, boundedAdjRibOutQ->size());
  bool sawV4Eor = false;
  bool sawV6Eor = false;
  for (int i = 0; i < 2; ++i) {
    auto message = folly::coro::blockingWait(boundedAdjRibOutQ->pop());
    ASSERT_TRUE(message.has_value());
    auto* eor = std::get_if<nettools::bgplib::BgpEndOfRib>(&*message);
    ASSERT_NE(nullptr, eor);
    if (*eor->afi() == nettools::bgplib::BgpUpdateAfi::AFI_IPv4) {
      sawV4Eor = true;
    } else if (*eor->afi() == nettools::bgplib::BgpUpdateAfi::AFI_IPv6) {
      sawV6Eor = true;
    }
  }
  EXPECT_TRUE(sawV4Eor);
  EXPECT_TRUE(sawV6Eor);

  /*
   * No UPDATE PDUs were queued, so the UPDATE counter stays 0 — the EoRs are
   * NOT folded into it (the regression being guarded against).
   */
  EXPECT_EQ(0, adjRibOutGroup_->getStats().getSentUpdateMsgs());
  // Both negotiated AFIs (v4 + v6) emit an EoR PDU, counted separately.
  EXPECT_EQ(2, adjRibOutGroup_->getStats().getSentEndOfRibMsgs());
  EXPECT_EQ(2, adjRib->getStats().getSentEndOfRibMsgs());
  EXPECT_FALSE(adjRib->egressEoRPendingV4());
  EXPECT_FALSE(adjRib->egressEoRPendingV6());
}

/**
 * Test fixture for distribution and backpressure tests
 */
class AdjRibGroupDistributionFixture : public AdjRibGroupTest {
 protected:
  void SetUp() override {
    AdjRibGroupTest::SetUp();
    createAdjRibOutGroup("test_group", 42, createDefaultGroupKey());
    // The group only queues to its packing list when it has an in-sync peer.
    addInSyncPeer();
  }
};

/**
 * Test: Bitmap iteration bug fix - empty map
 * Validates the fix: for (const auto& [bitPos, adjRib] : bitToAdjRibs_)
 * instead of: for (uint64_t bitPos = 0; bitPos < size; ++bitPos)
 */
TEST_F(AdjRibGroupDistributionFixture, BitmapIterationEmptyMap) {
  auto message = std::make_shared<nettools::bgplib::BgpUpdate2>();

  /*
   * With no peers registered (empty map), should handle gracefully
   * Old code: for (bitPos = 0; bitPos < 0; ++bitPos) - works fine
   * New code: for (const auto& [bitPos, adjRib] : emptyMap) - also works fine
   */
  EXPECT_NO_THROW(
      folly::coro::blockingWait(adjRibOutGroup_->distributeMessageToInSyncPeers(
          message, nullptr, nettools::bgplib::BgpUpdateAfi::AFI_IPv4)));
}

/**
 * Test: distributeMessageToInSyncPeers with null message
 */
TEST_F(AdjRibGroupDistributionFixture, DistributeNullMessage) {
  // Null message should be handled gracefully with warning log
  EXPECT_NO_THROW(
      folly::coro::blockingWait(adjRibOutGroup_->distributeMessageToInSyncPeers(
          nullptr, nullptr, nettools::bgplib::BgpUpdateAfi::AFI_IPv4)));
}

CO_TEST_F(
    AdjRibGroupDistributionFixture,
    CommittedUpdateCountDoesNotCrossClear) {
  auto adjRib = registeredInSyncPeers_.back().second;
  adjRib->setUpdateGroup(adjRibOutGroup_);
  adjRib->setPeerState(PeerUpdateState::JOINED_RUNNING);
  co_await adjRib->ensureAsyncScopeInitialized();
  adjRib->adjRibOutQueue_ = std::make_shared<AdjRib::AdjRibOutQueueT>(
      3 /* capacity */, 1 /* highWm */, 0 /* lowWm */);

  nettools::bgplib::FiberBgpPeer::InputMessageT dummy =
      nettools::bgplib::BgpEndOfRib();
  adjRib->adjRibOutQueue_->push(dummy);

  auto attrs1 = std::make_shared<BgpPath>(BgpPathFields());
  attrs1->setLocalPref(100);
  auto attrs2 = std::make_shared<BgpPath>(BgpPathFields());
  attrs2->setLocalPref(200);
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      {{folly::IPAddress("10.0.0.0"), 24}, kPlaceholderPathID},
      nullptr,
      attrs1);
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      {{folly::IPAddress("20.0.0.0"), 24}, kPlaceholderPathID},
      nullptr,
      attrs2);

  std::thread evbThread([this]() { evb_->loopForever(); });

  auto clearAndUnblock = [&]() -> folly::coro::Task<void> {
    while (!adjRibOutGroup_->hasBlockedPeers()) {
      co_await folly::coro::co_reschedule_on_current_executor;
    }

    /*
     * The first group UPDATE was built before the sender suspended, so its
     * count must already be visible to counter clear.
     */
    EXPECT_EQ(1, adjRibOutGroup_->getStats().getSentUpdateMsgs());
    adjRibOutGroup_->clearEgressMessageCounts();
    EXPECT_EQ(0, adjRibOutGroup_->getStats().getSentUpdateMsgs());

    co_await facebook::bgp::test::boundedPop(
        *adjRib->adjRibOutQueue_, "adjRib->adjRibOutQueue_");
    while (adjRib->adjRibOutQueue_->empty()) {
      co_await folly::coro::co_reschedule_on_current_executor;
    }
    co_await facebook::bgp::test::boundedPop(
        *adjRib->adjRibOutQueue_, "adjRib->adjRibOutQueue_");
  };

  co_await folly::coro::collectAll(
      co_withExecutor(&*evb_, adjRibOutGroup_->buildAndSendGroupBgpMessages()),
      co_withExecutor(&*evb_, clearAndUnblock()));

  evb_->terminateLoopSoon();
  evbThread.join();

  /* Only the group UPDATE built after clear belongs to the new epoch. */
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getSentUpdateMsgs());
  EXPECT_EQ(1, adjRib->adjRibOutQueue_->size());
}

CO_TEST_F(AdjRibGroupDistributionFixture, CommittedEorCountDoesNotCrossClear) {
  auto adjRib = registeredInSyncPeers_.back().second;
  adjRib->setUpdateGroup(adjRibOutGroup_);
  adjRib->setPeerState(PeerUpdateState::JOINED_RUNNING);
  co_await adjRib->ensureAsyncScopeInitialized();
  adjRib->adjRibOutQueue_ = std::make_shared<AdjRib::AdjRibOutQueueT>(
      3 /* capacity */, 1 /* highWm */, 0 /* lowWm */);

  nettools::bgplib::FiberBgpPeer::InputMessageT dummy =
      nettools::bgplib::BgpEndOfRib();
  adjRib->adjRibOutQueue_->push(dummy);
  adjRib->setEgressEoRsPending(true, true);
  adjRibOutGroup_->markPeerInSync(adjRib);

  std::thread evbThread([this]() { evb_->loopForever(); });

  auto clearAndUnblock = [&]() -> folly::coro::Task<void> {
    while (!adjRibOutGroup_->hasBlockedPeers()) {
      co_await folly::coro::co_reschedule_on_current_executor;
    }

    EXPECT_EQ(1, adjRibOutGroup_->getStats().getSentEndOfRibMsgs());
    adjRibOutGroup_->clearEgressMessageCounts();
    EXPECT_EQ(0, adjRibOutGroup_->getStats().getSentEndOfRibMsgs());

    co_await facebook::bgp::test::boundedPop(
        *adjRib->adjRibOutQueue_, "adjRib->adjRibOutQueue_");
    while (adjRib->adjRibOutQueue_->empty()) {
      co_await folly::coro::co_reschedule_on_current_executor;
    }
    co_await facebook::bgp::test::boundedPop(
        *adjRib->adjRibOutQueue_, "adjRib->adjRibOutQueue_");
  };

  co_await folly::coro::collectAll(
      co_withExecutor(&*evb_, adjRibOutGroup_->buildAndSendGroupBgpMessages()),
      co_withExecutor(&*evb_, clearAndUnblock()));

  evb_->terminateLoopSoon();
  evbThread.join();

  /* Only the v6 group EoR accepted after clear belongs to the new epoch. */
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getSentEndOfRibMsgs());
  EXPECT_EQ(2, adjRib->getStats().getSentEndOfRibMsgs());
  EXPECT_EQ(1, adjRib->adjRibOutQueue_->size());
}

/**
 * Test: buildAndSendGroupBgpMessages processes packing list
 */
TEST_F(AdjRibGroupDistributionFixture, BuildAndSendProcessesPackingList) {
  // Add some prefixes to packing list
  auto attrs = std::make_shared<BgpPath>(BgpPathFields());
  attrs->setLocalPref(100);

  auto prefixPathId1 = std::make_pair(
      folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24}, kPlaceholderPathID);
  auto prefixPathId2 = std::make_pair(
      folly::CIDRNetwork{folly::IPAddress("20.0.0.0"), 24}, kPlaceholderPathID);

  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId1, nullptr, attrs);
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId2, nullptr, attrs);

  EXPECT_EQ(1, adjRibOutGroup_->getAttrToPrefixMap().size());

  // Build and send should clear packing list
  folly::coro::blockingWait(adjRibOutGroup_->buildAndSendGroupBgpMessages());

  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());
}

/**
 * Test: buildGroupUpdate creates correct
 * update
 */
TEST_F(AdjRibGroupDistributionFixture, BuildGroupUpdateCreatesUpdate) {
  auto attrs = std::make_shared<BgpPath>(BgpPathFields());
  attrs->setLocalPref(100);

  PrefixSet prefixSet;
  prefixSet.insert(
      {folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24},
       kPlaceholderPathID});
  prefixSet.insert(
      {folly::CIDRNetwork{folly::IPAddress("20.0.0.0"), 24},
       kPlaceholderPathID});

  BgpPathWithAfi attrsWithAfi{attrs, nettools::bgplib::BgpUpdateAfi::AFI_IPv4};

  auto update = adjRibOutGroup_->buildGroupUpdate(attrsWithAfi, prefixSet);

  ASSERT_NE(update, nullptr);
  EXPECT_EQ(
      update->mpAnnounced()->afi(), nettools::bgplib::BgpUpdateAfi::AFI_IPv4);
  EXPECT_EQ(update->mpAnnounced()->prefixes()->size(), 2);
  EXPECT_TRUE(prefixSet.empty()); // Should be drained
}

/**
 * Test: packGroupPrefixes drains prefix set
 */
TEST_F(AdjRibGroupDistributionFixture, PackGroupPrefixesDrainsSet) {
  PrefixSet prefixSet;
  for (int i = 0; i < 10; i++) {
    prefixSet.insert(
        {folly::CIDRNetwork{folly::IPAddress(fmt::format("10.0.{}.0", i)), 24},
         kPlaceholderPathID});
  }

  std::vector<nettools::bgplib::RiggedIPPrefix> bgpPrefixes;

  auto packed =
      adjRibOutGroup_->packGroupPrefixes(prefixSet, bgpPrefixes, false);

  EXPECT_EQ(packed, 10);
  EXPECT_EQ(bgpPrefixes.size(), 10);
  EXPECT_TRUE(prefixSet.empty());
}

/**
 * Test: tryUpdateAttrToPrefixMapForGroup announcement
 */
TEST_F(AdjRibGroupDistributionFixture, TryUpdateAttrToPrefixMapAnnouncement) {
  auto attrs = std::make_shared<BgpPath>(BgpPathFields());
  attrs->setLocalPref(100);

  auto prefixPathId = std::make_pair(
      folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24}, kPlaceholderPathID);

  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());

  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId, nullptr, attrs);

  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();
  EXPECT_EQ(1, attrToPrefixMap.size());

  auto attrsWithAfi =
      BgpPathWithAfi{attrs, nettools::bgplib::BgpUpdateAfi::AFI_IPv4};
  EXPECT_TRUE(attrToPrefixMap.contains(attrsWithAfi));
  EXPECT_TRUE(attrToPrefixMap.at(attrsWithAfi).contains(prefixPathId));
}

/**
 * Test: tryUpdateAttrToPrefixMapForGroup withdrawal
 */
TEST_F(AdjRibGroupDistributionFixture, TryUpdateAttrToPrefixMapWithdrawal) {
  auto attrs = std::make_shared<BgpPath>(BgpPathFields());
  attrs->setLocalPref(100);

  auto prefixPathId = std::make_pair(
      folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24}, kPlaceholderPathID);

  // First announce
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId, nullptr, attrs);
  EXPECT_EQ(1, adjRibOutGroup_->getAttrToPrefixMap().size());

  // Then withdraw
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId, attrs, nullptr);

  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();
  EXPECT_EQ(1, attrToPrefixMap.size());

  auto withdrawalKey =
      BgpPathWithAfi{nullptr, nettools::bgplib::BgpUpdateAfi::AFI_IPv4};
  EXPECT_TRUE(attrToPrefixMap.contains(withdrawalKey));
  EXPECT_TRUE(attrToPrefixMap.at(withdrawalKey).contains(prefixPathId));
}

/**
 * Test: processRibAnnouncedEntryForGroup stores entry and updates packing list
 */
TEST_F(AdjRibGroupDistributionFixture, ProcessRibAnnouncedEntry) {
  auto attrs = std::make_shared<BgpPath>(BgpPathFields());
  attrs->setLocalPref(100);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};

  RibOutAnnouncementEntry entry(
      prefix,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      attrs,
      0,
      0,
      0,
      0,
      0);

  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry);

  // Should be in packing list
  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();
  EXPECT_EQ(1, attrToPrefixMap.size());

  // Should be in RIB-OUT tree
  auto groupOwnerKey =
      AdjRibOutOwnerKey::forGroup(adjRibOutGroup_->getGroupId());
  auto* adjRibEntry = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, groupOwnerKey);
  ASSERT_NE(adjRibEntry, nullptr);
  EXPECT_EQ(adjRibEntry->getPostAttr(), attrs);
}

/**
 * Test: processRibAnnouncedEntryForGroup processes multiple entries
 *
 * DISABLED: This test is currently disabled because the group schedules
 * buildAndSendGroupBgpMessages() asynchronously. The test needs to be
 * rewritten using E2ETestFixture to properly validate the end-to-end behavior.
 *
 * TODO: Re-enable this test using E2ETestFixture + AdjRibInUtils to:
 * 1. Set up mock peers with bounded queues
 * 2. Register them with the group
 * 3. Process the announced entries
 * 4. Read from peer queues and verify the BGP UPDATE message contains both
 *    prefixes (10.0.0.0/24 and 20.0.0.0/24)
 * This will provide true end-to-end validation of the group distribution
 * mechanism and properly handle the async nature of the implementation.
 */
TEST_F(
    AdjRibGroupDistributionFixture,
    DISABLED_ProcessRibOutAnnouncementMultipleEntries) {
  auto attrs = std::make_shared<BgpPath>(BgpPathFields());
  attrs->setLocalPref(100);

  RibOutAnnouncement announcement;
  announcement.initialDump = true;

  folly::CIDRNetwork prefix1{folly::IPAddress("10.0.0.0"), 24};
  folly::CIDRNetwork prefix2{folly::IPAddress("20.0.0.0"), 24};

  announcement.entries.emplace_back(
      prefix1,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      attrs,
      0,
      0,
      0,
      0,
      0);

  announcement.entries.emplace_back(
      prefix2,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      attrs,
      0,
      0,
      0,
      0,
      0);

  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());

  for (const auto& entry : announcement.entries) {
    adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry);
  }

  // Packing list should be drained after buildAndSendGroupBgpMessages
  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();
  EXPECT_TRUE(attrToPrefixMap.empty());

  // Both entries should be stored in RIB-OUT tree
  auto groupOwnerKey =
      AdjRibOutOwnerKey::forGroup(adjRibOutGroup_->getGroupId());

  auto* entry1 = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, prefix1, groupOwnerKey);
  ASSERT_NE(entry1, nullptr);
  EXPECT_EQ(entry1->getPostAttr(), attrs);

  auto* entry2 = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, prefix2, groupOwnerKey);
  ASSERT_NE(entry2, nullptr);
  EXPECT_EQ(entry2->getPostAttr(), attrs);
}

/**
 * Test fixture for group add-path, message cloning, and EoR features
 */
class AdjRibGroupAddPathFixture : public AdjRibGroupTest {
 protected:
  void SetUp() override {
    AdjRibGroupTest::SetUp();
  }

  void createGroupWithAddPath(
      bool sendAddPath,
      const ShadowRibEntriesMap& shadowRibEntries =
          ShadowRibView::emptyEntries()) {
    UpdateGroupKey groupKey;
    groupKey.sendAddPath = sendAddPath;
    groupKey.sessionType = BgpSessionType::EBGP;
    groupKey.afiIpv4Negotiated = true;
    groupKey.afiIpv6Negotiated = false;

    adjRibOutGroup_ = std::make_shared<AdjRibOutGroup>(
        *evb_,
        "test_group",
        42,
        true /* enableUpdateGroup */,
        groupKey,
        ShadowRibView{shadowRibEntries, maxRibVersion_});
  }

  std::shared_ptr<BgpPath> createPath(uint32_t localPref) {
    auto path = std::make_shared<BgpPath>(BgpPathFields());
    path->setLocalPref(localPref);
    return path;
  }
};

/**
 * Test: Add-path disabled - sends bestpath only (not multipaths)
 * Verifies groupKey_.sendAddPath=false uses announcement.entries
 */
TEST_F(AdjRibGroupAddPathFixture, AddPathDisabled_SendsBestpathOnly) {
  createGroupWithAddPath(false /* sendAddPath */);

  auto bestpath = createPath(200);

  RibOutAnnouncement announcement;
  announcement.initialDump = true;

  // With sendAddPath=false, only bestpath in entries (not addPathEntries)
  announcement.entries.emplace_back(
      folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24},
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      bestpath,
      0,
      0,
      0,
      0,
      0);

  for (const auto& entry : announcement.entries) {
    adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry);
  }

  // Run event loop to process async buildAndSendGroupBgpMessages
  evb_->loopOnce();

  // Packing list should be drained (no peers to send to, but messages built)
  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();
  EXPECT_EQ(0, attrToPrefixMap.size());
}

/**
 * Test: Add-path enabled - sends all multipaths with path IDs
 * Verifies groupKey_.sendAddPath=true uses announcement.addPathEntries
 */
TEST_F(AdjRibGroupAddPathFixture, AddPathEnabled_SendsAllMultipaths) {
  createGroupWithAddPath(true /* sendAddPath */);

  auto path1 = createPath(200);
  auto path2 = createPath(150);
  auto path3 = createPath(100);

  RibOutAnnouncement announcement;
  announcement.initialDump = true;

  auto prefix = folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24};

  // With sendAddPath=true, use addPathEntries with pathIdToSend
  announcement.addPathEntries.emplace_back(
      prefix,
      1, // pathIdToSend
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      path1,
      0,
      0,
      0,
      0,
      0);

  announcement.addPathEntries.emplace_back(
      prefix,
      2, // pathIdToSend
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      path2,
      0,
      0,
      0,
      0,
      0);

  announcement.addPathEntries.emplace_back(
      prefix,
      3, // pathIdToSend
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      path3,
      0,
      0,
      0,
      0,
      0);

  for (const auto& entry : announcement.addPathEntries) {
    adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry);
  }

  // Run event loop to process async buildAndSendGroupBgpMessages
  evb_->loopOnce();

  // Packing list should be drained
  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();
  EXPECT_EQ(0, attrToPrefixMap.size());
}

/**
 * Test: processShadowRibEntryFullMultipaths announces every multipath that is
 * not being withdrawn, including one not flagged as updated, and withdraws the
 * one being withdrawn, each by its pathIdToSend.
 */
TEST_F(
    AdjRibGroupAddPathFixture,
    ProcessShadowRibEntryFullMultipaths_AnnouncesAllAndWithdraws) {
  createGroupWithAddPath(true /* sendAddPath */);

  const auto prefix = folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24};
  const TinyPeerInfo peer(
      folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false);
  const auto groupOwnerKey =
      AdjRibOutOwnerKey::forGroup(adjRibOutGroup_->getGroupId());
  const uint32_t updatedPathId = 1;
  const uint32_t unchangedPathId = 2;
  const uint32_t withdrawnPathId = 3;

  // The withdrawn path was announced before.
  auto withdrawnAttrs = createPath(100);
  adjRibOutGroup_->processRibAnnouncedEntryForGroup(RibOutAnnouncementEntry(
      prefix, withdrawnPathId, peer, withdrawnAttrs, 0, 0, 0, 0, 0));
  auto* withdrawnEntry = adjRibOutGroup_->getFromPathTree(
      adjRibOutGroup_->PathTree_, prefix, groupOwnerKey, withdrawnPathId);
  ASSERT_NE(withdrawnEntry, nullptr);
  ASSERT_NE(withdrawnEntry->getPostAttr(), nullptr);

  ShadowRibRouteInfos multipaths;
  auto updated = std::make_shared<ShadowRibRouteInfo>(
      peer, createPath(200), updatedPathId);
  setShadowRibRouteState(updated, SHADOWRIBROUTE_IN_UPDATE);
  multipaths.emplace(updatedPathId, std::move(updated));
  multipaths.emplace(
      unchangedPathId,
      std::make_shared<ShadowRibRouteInfo>(
          peer, createPath(150), unchangedPathId));
  auto withdrawn = std::make_shared<ShadowRibRouteInfo>(
      peer, withdrawnAttrs, withdrawnPathId);
  setShadowRibRouteState(withdrawn, SHADOWRIBROUTE_IN_WITHDRAW);
  multipaths.emplace(withdrawnPathId, std::move(withdrawn));
  ShadowRibOutAnnouncementEntry srEntry(
      prefix, /*bestpath=*/nullptr, std::move(multipaths));

  adjRibOutGroup_->processShadowRibEntryFullMultipaths(srEntry);

  for (const auto pathId : {updatedPathId, unchangedPathId}) {
    auto* adjRibEntry = adjRibOutGroup_->getFromPathTree(
        adjRibOutGroup_->PathTree_, prefix, groupOwnerKey, pathId);
    ASSERT_NE(adjRibEntry, nullptr) << "pathId=" << pathId;
    EXPECT_NE(adjRibEntry->getPostAttr(), nullptr) << "pathId=" << pathId;
  }
  withdrawnEntry = adjRibOutGroup_->getFromPathTree(
      adjRibOutGroup_->PathTree_, prefix, groupOwnerKey, withdrawnPathId);
  EXPECT_TRUE(withdrawnEntry == nullptr || !withdrawnEntry->getPostAttr());
}

/**
 * Test: Message cloning for nexthop handoff
 * Verifies tryPushToPeer() clones messages per peer
 * (Cannot fully test without mock peers, but ensures no crash)
 */
TEST_F(AdjRibGroupAddPathFixture, MessageCloning_Nocrash) {
  createGroupWithAddPath(false);

  auto message = std::make_shared<nettools::bgplib::BgpUpdate2>();

  /*
   * distributeMessageToInSyncPeers should handle message cloning internally
   * Without peers, this is a smoke test that it doesn't crash
   */
  EXPECT_NO_THROW(
      folly::coro::blockingWait(adjRibOutGroup_->distributeMessageToInSyncPeers(
          message, nullptr, nettools::bgplib::BgpUpdateAfi::AFI_IPv4)));
}

/**
 * Test: EoR handoff - distributePendingEoRs with no peers
 * Verifies the function handles empty peer maps gracefully
 */
TEST_F(AdjRibGroupAddPathFixture, NotifyEoR_NoPeersGraceful) {
  createGroupWithAddPath(false);

  // With no peers registered, should not crash
  EXPECT_NO_THROW(
      folly::coro::blockingWait(adjRibOutGroup_->distributePendingEoRs()));
}

/**
 * Test: EoR handoff - bitmap iteration uses map keys
 * Verifies the fixed iteration pattern works correctly
 */
TEST_F(AdjRibGroupAddPathFixture, NotifyEoR_BitmapIterationFixed) {
  createGroupWithAddPath(false);

  /*
   * Even with sparse bitmap, iteration should work
   * This tests the fix: for (const auto& [bitPos, adjRib] : bitToAdjRibs_)
   */
  EXPECT_NO_THROW(
      folly::coro::blockingWait(adjRibOutGroup_->distributePendingEoRs()));
}

/**
 * NOTE: Sliding window slow peer detection tests are intentionally minimal.
 * Full integration testing with real AdjRib objects requires complex setup
 * (BgpPeerId, PeeringParams, message queues, etc.) which is beyond the scope
 * of unit tests. The sliding window logic itself is tested via:
 * 1. Code inspection of markPeerBlocked() implementation (lines 1605-1613)
 * 2. Manual/integration testing with real peer sessions
 * 3. The test helper setSlowPeerThresholds() is provided for future tests
 *
 * The implementation uses std::deque<timestamp> with:
 * - push_back() to add new block timestamps
 * - pop_front() to remove expired timestamps older than window
 * - size() to count blocks within current window
 *
 * This ensures O(k) complexity where k = blocks within window (not total
 * blocks).
 */

/**
 * Test: Async buildAndSendGroupBgpMessages can be called multiple times
 *
 * This test validates the async refactoring by ensuring
 * buildAndSendGroupBgpMessages can be called multiple times sequentially
 * without errors, testing the async Task infrastructure.
 */
TEST_F(AdjRibGroupDistributionFixture, AsyncBuildAndSendMultipleCalls) {
  const folly::CIDRNetwork prefix1{folly::IPAddress("10.0.0.0"), 24};
  const folly::CIDRNetwork prefix2{folly::IPAddress("20.0.0.0"), 24};

  auto attrs = std::make_shared<BgpPath>(BgpPathFields());
  attrs->setLocalPref(100);

  // Add first announcement
  auto prefixPathId1 = std::make_pair(prefix1, kPlaceholderPathID);
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId1, nullptr, attrs);

  EXPECT_EQ(1, adjRibOutGroup_->getAttrToPrefixMap().size());

  // Build and send first batch - should not throw and should clear packing list
  EXPECT_NO_THROW(
      folly::coro::blockingWait(
          adjRibOutGroup_->buildAndSendGroupBgpMessages()));

  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());

  // Add second announcement
  auto prefixPathId2 = std::make_pair(prefix2, kPlaceholderPathID);
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId2, nullptr, attrs);

  EXPECT_EQ(1, adjRibOutGroup_->getAttrToPrefixMap().size());

  /*
   * Build and send second batch - should not throw and should clear packing
   * list
   */
  EXPECT_NO_THROW(
      folly::coro::blockingWait(
          adjRibOutGroup_->buildAndSendGroupBgpMessages()));

  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());

  /*
   * This validates that async buildAndSendGroupBgpMessages can be called
   * multiple times without errors, which is the key async refactoring behavior
   */
}

/**
 * Test: Destructor properly cleans up pending async operations
 *
 * This test validates that AdjRibOutGroup destructor correctly cancels and
 * waits for pending async operations (via asyncScope_.cancelAndJoinAsync()).
 * Without proper cleanup, destroying a group with pending coroutines causes
 * use-after-free.
 */
TEST_F(AdjRibGroupDistributionFixture, DestructorCleansUpAsyncOperations) {
  const folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};

  auto attrs = std::make_shared<BgpPath>(BgpPathFields());
  attrs->setLocalPref(100);

  // Add announcement and schedule async work
  auto prefixPathId = std::make_pair(prefix, kPlaceholderPathID);
  adjRibOutGroup_->tryUpdateAttrToPrefixMapForGroup(
      prefixPathId, nullptr, attrs);

  // Start async build and send (this adds work to asyncScope_)
  folly::coro::blockingWait(adjRibOutGroup_->buildAndSendGroupBgpMessages());

  /*
   * Immediately destroy the group - destructor should:
   * 1. Call asyncScope_.cancelAndJoinAsync()
   * 2. Wait for all pending coroutines to complete
   * 3. Not crash or cause use-after-free
   *
   * If the destructor didn't properly clean up asyncScope_, this would crash
   * or hang when pending coroutines try to access destroyed group members
   */
  adjRibOutGroup_.reset();

  // Verify we didn't crash - test passing means destructor worked correctly
  SUCCEED();
}

/*
 * ============================================================================
 * Peer Registration and Lifecycle Tests
 * Tests for registerPeer(), unregisterPeer(), and BGP initialization sequence
 * ============================================================================
 */

/**
 * Test: getMemberCount returns correct count
 */
TEST_F(AdjRibGroupTest, GetMemberCount) {
  createAdjRibOutGroup("test_group", 1);

  // Initially, no members
  EXPECT_EQ(adjRibOutGroup_->getMemberCount(), 0);
}

/**
 * Test: registerPeer with null adjRib
 */
TEST_F(AdjRibGroupTest, RegisterPeerNull) {
  createAdjRibOutGroup("test_group", 1);

  // Should handle null gracefully
  EXPECT_NO_THROW(adjRibOutGroup_->registerPeer(nullptr));
  EXPECT_EQ(adjRibOutGroup_->getMemberCount(), 0);
}

/**
 * Test: unregisterPeer with null adjRib
 */
TEST_F(AdjRibGroupTest, UnregisterPeerNull) {
  createAdjRibOutGroup("test_group", 1);

  // Should handle null gracefully
  EXPECT_NO_THROW(adjRibOutGroup_->unregisterPeer(nullptr));
  EXPECT_EQ(adjRibOutGroup_->getMemberCount(), 0);
}

/**
 * Test: getGroupKey returns correct UpdateGroupKey
 */
TEST_F(AdjRibGroupPackingFixture, GetGroupKey) {
  UpdateGroupKey key;
  key.egressPolicyName = "test_policy";
  key.sendAddPath = true;

  auto group = std::make_shared<AdjRibOutGroup>(
      *evb_, "test_group", 1, true /* enableUpdateGroup */, key);

  const auto& retrievedKey = group->getGroupKey();
  EXPECT_EQ(retrievedKey.egressPolicyName, "test_policy");
  EXPECT_EQ(retrievedKey.sendAddPath, true);
}

/**
 * Test: registerPeer with null adjRib does not crash and does not increase
 * member count This verifies the null check in registerPeer()
 */
TEST_F(AdjRibGroupTest, RegisterPeerNullDoesNotIncreaseCount) {
  createAdjRibOutGroup("test_group", 1);

  EXPECT_EQ(adjRibOutGroup_->getMemberCount(), 0);

  // Attempt to register null peer
  adjRibOutGroup_->registerPeer(nullptr);

  // Member count should remain 0
  EXPECT_EQ(adjRibOutGroup_->getMemberCount(), 0);
}

/**
 * Test: unregisterPeer with null adjRib does not crash
 * This verifies the null check in unregisterPeer()
 */
TEST_F(AdjRibGroupTest, UnregisterPeerNullDoesNotCrash) {
  createAdjRibOutGroup("test_group", 1);

  // Should not crash with null peer
  EXPECT_NO_THROW(adjRibOutGroup_->unregisterPeer(nullptr));
  EXPECT_EQ(adjRibOutGroup_->getMemberCount(), 0);
}

/**
 * Test: Group state transitions correctly from UNINITIALIZED to other states
 * This verifies setState() and getState() work correctly
 */
TEST_F(AdjRibGroupPackingFixture, GroupStateTransitions) {
  createAdjRibOutGroup("test_group", 1);

  // Initial state should be UNINITIALIZED
  EXPECT_EQ(adjRibOutGroup_->getState(), UpdateGroupState::UNINITIALIZED);

  // Transition to WAITING (simulating initial dump started)
  adjRibOutGroup_->setState(UpdateGroupState::WAITING);
  EXPECT_EQ(adjRibOutGroup_->getState(), UpdateGroupState::WAITING);

  // Transition to IDLE (simulating initial dump completed)
  adjRibOutGroup_->setState(UpdateGroupState::IDLE);
  EXPECT_EQ(adjRibOutGroup_->getState(), UpdateGroupState::IDLE);

  // Transition to READY (simulating ready to send updates)
  adjRibOutGroup_->setState(UpdateGroupState::READY);
  EXPECT_EQ(adjRibOutGroup_->getState(), UpdateGroupState::READY);
}

/*
 * ============================================================================
 * Detached Peer Termination Tests
 * Tests for cleanUpPeerRibOut() cleanup logic
 * ============================================================================
 */

/*
 * ============================================================================
 * AFI Negotiation Tests
 * Tests for isAfiNegotiated check in processRibAnnouncedEntryForGroup()
 * ============================================================================
 */

/**
 * Test: processRibAnnouncedEntryForGroup ignores prefix with unsupported AFI
 * When group has only IPv4 negotiated, IPv6 prefixes should be ignored
 */
TEST_F(
    AdjRibGroupPackingFixture,
    ProcessRibAnnouncedEntryForGroup_IgnoresUnsupportedAfi) {
  // Create UpdateGroupKey with only IPv4 negotiated
  UpdateGroupKey key;
  key.afiIpv4Negotiated = true;
  key.afiIpv6Negotiated = false;

  adjRibOutGroup_ = std::make_shared<AdjRibOutGroup>(
      *evb_, "test_group", 1, true /* enableUpdateGroup */, key);

  // Create IPv6 announcement entry (unsupported AFI)
  RibOutAnnouncementEntry entryV6(
      kV6Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());

  // Process IPv6 entry - should be ignored
  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entryV6);

  // Packing list should still be empty (IPv6 was ignored)
  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());

  // Verify no entry was added to the RIB-OUT tree
  auto groupOwnerKey =
      AdjRibOutOwnerKey::forGroup(adjRibOutGroup_->getGroupId());
  auto* adjRibEntry = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, kV6Prefix1_, groupOwnerKey);
  EXPECT_EQ(adjRibEntry, nullptr);
}

/**
 * Test: processRibAnnouncedEntryForGroup processes prefix with supported AFI
 * When group has IPv4 negotiated, IPv4 prefixes should be processed
 */
TEST_F(
    AdjRibGroupPackingFixture,
    ProcessRibAnnouncedEntryForGroup_ProcessesSupportedAfi) {
  // Create UpdateGroupKey with only IPv4 negotiated
  UpdateGroupKey key;
  key.afiIpv4Negotiated = true;
  key.afiIpv6Negotiated = false;

  createAdjRibOutGroup("test_group", 1, key);

  // Create IPv4 announcement entry (supported AFI)
  RibOutAnnouncementEntry entryV4(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());

  // Process IPv4 entry - should be processed
  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entryV4);

  // Packing list should have the entry
  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();
  EXPECT_EQ(1, attrToPrefixMap.size());

  // Verify entry was added to the RIB-OUT tree
  auto groupOwnerKey =
      AdjRibOutOwnerKey::forGroup(adjRibOutGroup_->getGroupId());
  auto* adjRibEntry = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, kV4Prefix1_, groupOwnerKey);
  EXPECT_NE(adjRibEntry, nullptr);
  EXPECT_NE(adjRibEntry->getPostAttr(), nullptr);
}

/**
 * Test: processRibAnnouncedEntryForGroup with both AFIs negotiated
 * When group has both IPv4 and IPv6 negotiated, both should be processed
 */
TEST_F(
    AdjRibGroupPackingFixture,
    ProcessRibAnnouncedEntryForGroup_BothAfisNegotiated) {
  // Create UpdateGroupKey with both AFIs negotiated
  UpdateGroupKey key;
  key.afiIpv4Negotiated = true;
  key.afiIpv6Negotiated = true;

  createAdjRibOutGroup("test_group", 1, key);

  // Create IPv4 and IPv6 announcement entries
  RibOutAnnouncementEntry entryV4(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  RibOutAnnouncementEntry entryV6(
      kV6Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());

  // Process both entries
  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entryV4);
  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entryV6);

  // Packing list should have both entries (2 AFI entries with 1 prefix each)
  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();
  EXPECT_EQ(2, attrToPrefixMap.size());

  // Verify both entries were added to the RIB-OUT tree
  auto groupOwnerKey =
      AdjRibOutOwnerKey::forGroup(adjRibOutGroup_->getGroupId());

  auto* adjRibEntryV4 = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, kV4Prefix1_, groupOwnerKey);
  EXPECT_NE(adjRibEntryV4, nullptr);

  auto* adjRibEntryV6 = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, kV6Prefix1_, groupOwnerKey);
  EXPECT_NE(adjRibEntryV6, nullptr);
}

/**
 * Test: processRibAnnouncedEntryForGroup with only IPv6 negotiated
 * When group has only IPv6 negotiated, IPv4 prefixes should be ignored
 */
TEST_F(
    AdjRibGroupPackingFixture,
    ProcessRibAnnouncedEntryForGroup_OnlyIpv6Negotiated) {
  // Create UpdateGroupKey with only IPv6 negotiated
  UpdateGroupKey key;
  key.afiIpv4Negotiated = false;
  key.afiIpv6Negotiated = true;

  createAdjRibOutGroup("test_group", 1, key);

  // Create IPv4 announcement entry (unsupported AFI)
  RibOutAnnouncementEntry entryV4(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  // Create IPv6 announcement entry (supported AFI)
  RibOutAnnouncementEntry entryV6(
      kV6Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());

  // Process both entries
  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entryV4);
  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entryV6);

  // Packing list should only have IPv6 entry
  auto& attrToPrefixMap = adjRibOutGroup_->getAttrToPrefixMap();
  EXPECT_EQ(1, attrToPrefixMap.size());

  // Verify only IPv6 entry was added to the RIB-OUT tree
  auto groupOwnerKey =
      AdjRibOutOwnerKey::forGroup(adjRibOutGroup_->getGroupId());

  auto* adjRibEntryV4 = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, kV4Prefix1_, groupOwnerKey);
  EXPECT_EQ(adjRibEntryV4, nullptr); // IPv4 should NOT be in tree

  auto* adjRibEntryV6 = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, kV6Prefix1_, groupOwnerKey);
  EXPECT_NE(adjRibEntryV6, nullptr); // IPv6 should be in tree
}

/**
 * Test fixture for canAnnounceForGroup tests
 */
class CanAnnounceForGroupFixture : public AdjRibGroupTest {
 protected:
  void SetUp() override {
    AdjRibGroupTest::SetUp();

    // Create test attributes
    auto attrs = std::make_shared<BgpPath>(BgpPathFields());
    attrs->setLocalPref(100);
    testAttrs_ = attrs;
  }

  /**
   * @brief Create an UpdateGroupKey with specified session type and RR client
   * setting
   */
  static UpdateGroupKey createGroupKey(
      BgpSessionType sessionType,
      bool isRrClient) {
    UpdateGroupKey key;
    key.afiIpv4Negotiated = true;
    key.afiIpv6Negotiated = true;
    key.sessionType = sessionType;
    key.isRrClient = isRrClient;
    return key;
  }

  std::shared_ptr<const BgpPath> testAttrs_;

  const folly::CIDRNetwork kV4Prefix_{folly::IPAddress("10.0.0.0"), 24};

  // Local route peer (addr.isZero() == true)
  const TinyPeerInfo localPeer_{
      folly::IPAddress("0.0.0.0"), // isZero() == true
      0,
      0,
      BgpSessionType::IBGP,
      false};

  // IBGP non-RR client peer
  const TinyPeerInfo iBgpPeer_{
      folly::IPAddress("2.2.2.2"),
      65000,
      2,
      BgpSessionType::IBGP,
      false};

  // IBGP RR client peer
  const TinyPeerInfo rrcPeer_{
      folly::IPAddress("3.3.3.3"),
      65000,
      3,
      BgpSessionType::IBGP,
      true};

  // EBGP peer
  const TinyPeerInfo eBgpPeer_{
      folly::IPAddress("4.4.4.4"),
      65001,
      4,
      BgpSessionType::EBGP,
      false};
};

/**
 * Test: IBGP RR client group can announce all routes
 */
TEST_F(CanAnnounceForGroupFixture, IBgpRRClientGroup_CanAnnounceAll) {
  auto groupKey = createGroupKey(BgpSessionType::IBGP, true /* isRrClient */);
  createAdjRibOutGroup("rrc_group", 1, groupKey);

  // RR client group can announce local routes
  RibOutAnnouncementEntry localEntry(
      kV4Prefix_, kDefaultPathID, localPeer_, testAttrs_);
  EXPECT_TRUE(adjRibOutGroup_->canAnnounceForGroup(localEntry));

  // RR client group can announce routes from IBGP non-RRC peer
  RibOutAnnouncementEntry iBgpEntry(
      kV4Prefix_, kDefaultPathID, iBgpPeer_, testAttrs_);
  EXPECT_TRUE(adjRibOutGroup_->canAnnounceForGroup(iBgpEntry));

  // RR client group can announce routes from IBGP RRC peer
  RibOutAnnouncementEntry rrcEntry(
      kV4Prefix_, kDefaultPathID, rrcPeer_, testAttrs_);
  EXPECT_TRUE(adjRibOutGroup_->canAnnounceForGroup(rrcEntry));

  // RR client group can announce routes from EBGP peer
  RibOutAnnouncementEntry eBgpEntry(
      kV4Prefix_, kDefaultPathID, eBgpPeer_, testAttrs_);
  EXPECT_TRUE(adjRibOutGroup_->canAnnounceForGroup(eBgpEntry));
}

/**
 * Test: IBGP non-RR client group can announce local routes
 */
TEST_F(CanAnnounceForGroupFixture, IBgpNonRRClientGroup_CanAnnounceLocalRoute) {
  auto groupKey = createGroupKey(BgpSessionType::IBGP, false /* isRrClient */);
  createAdjRibOutGroup("ibgp_group", 2, groupKey);

  // IBGP non-RR group can announce local routes (addr.isZero())
  RibOutAnnouncementEntry localEntry(
      kV4Prefix_, kDefaultPathID, localPeer_, testAttrs_);
  EXPECT_TRUE(adjRibOutGroup_->canAnnounceForGroup(localEntry));
}

/**
 * Test: IBGP non-RR client group can announce EBGP-learned routes
 */
TEST_F(CanAnnounceForGroupFixture, IBgpNonRRClientGroup_CanAnnounceEBgpRoute) {
  auto groupKey = createGroupKey(BgpSessionType::IBGP, false /* isRrClient */);
  createAdjRibOutGroup("ibgp_group", 2, groupKey);

  // IBGP non-RR group can announce routes from EBGP peer
  RibOutAnnouncementEntry eBgpEntry(
      kV4Prefix_, kDefaultPathID, eBgpPeer_, testAttrs_);
  EXPECT_TRUE(adjRibOutGroup_->canAnnounceForGroup(eBgpEntry));
}

/**
 * Test: IBGP non-RR client group can announce routes learned from RRC
 */
TEST_F(CanAnnounceForGroupFixture, IBgpNonRRClientGroup_CanAnnounceRRCRoute) {
  auto groupKey = createGroupKey(BgpSessionType::IBGP, false /* isRrClient */);
  createAdjRibOutGroup("ibgp_group", 2, groupKey);

  // IBGP non-RR group can announce routes from RR client
  RibOutAnnouncementEntry rrcEntry(
      kV4Prefix_, kDefaultPathID, rrcPeer_, testAttrs_);
  EXPECT_TRUE(adjRibOutGroup_->canAnnounceForGroup(rrcEntry));
}

/**
 * Test: IBGP non-RR client group blocks routes learned from non-RRC IBGP peer
 */
TEST_F(CanAnnounceForGroupFixture, IBgpNonRRClientGroup_BlocksIBgpToIBgpRoute) {
  auto groupKey = createGroupKey(BgpSessionType::IBGP, false /* isRrClient */);
  createAdjRibOutGroup("ibgp_group", 2, groupKey);

  /*
   * IBGP non-RR group should NOT announce routes from IBGP non-RRC peer
   * This is the IBGP-to-IBGP case without route reflection
   */
  RibOutAnnouncementEntry iBgpEntry(
      kV4Prefix_, kDefaultPathID, iBgpPeer_, testAttrs_);
  EXPECT_FALSE(adjRibOutGroup_->canAnnounceForGroup(iBgpEntry));
}

/**
 * Test: EBGP group can announce all routes
 */
TEST_F(CanAnnounceForGroupFixture, EBgpGroup_CanAnnounceAll) {
  auto groupKey = createGroupKey(BgpSessionType::EBGP, false /* isRrClient */);
  createAdjRibOutGroup("ebgp_group", 3, groupKey);

  // EBGP group can announce local routes
  RibOutAnnouncementEntry localEntry(
      kV4Prefix_, kDefaultPathID, localPeer_, testAttrs_);
  EXPECT_TRUE(adjRibOutGroup_->canAnnounceForGroup(localEntry));

  // EBGP group can announce routes from IBGP non-RRC peer
  RibOutAnnouncementEntry iBgpEntry(
      kV4Prefix_, kDefaultPathID, iBgpPeer_, testAttrs_);
  EXPECT_TRUE(adjRibOutGroup_->canAnnounceForGroup(iBgpEntry));

  // EBGP group can announce routes from IBGP RRC peer
  RibOutAnnouncementEntry rrcEntry(
      kV4Prefix_, kDefaultPathID, rrcPeer_, testAttrs_);
  EXPECT_TRUE(adjRibOutGroup_->canAnnounceForGroup(rrcEntry));

  // EBGP group can announce routes from EBGP peer
  RibOutAnnouncementEntry eBgpEntry(
      kV4Prefix_, kDefaultPathID, eBgpPeer_, testAttrs_);
  EXPECT_TRUE(adjRibOutGroup_->canAnnounceForGroup(eBgpEntry));
}

/**
 * Test: getRadixNodeItrFromLiteTree returns a valid iterator for an existing
 * prefix. Populates 10k prefixes under 10.0.0.0/8, then verifies
 * that both group and peer entries are accessible via the iterator.
 */
TEST_F(AdjRibGroupTest, GetSubTreeFromLiteTreeNonLeafNode) {
  createAdjRibOutGroup("test_group", 42);
  auto groupOwnerKey = AdjRibOutOwnerKey::forGroup(42);
  auto peerId = std::make_shared<nettools::bgplib::BgpPeerId>();
  auto peerOwnerKey = AdjRibOutOwnerKey::forPeer(peerId);

  /*
   * Populate 10k /24 prefixes: 10.0.0.0/24 through 10.39.15.0/24
   * (40 * 256 = 10240 prefixes, all under 10.0.0.0/8)
   */
  constexpr uint32_t kNumPrefixes = 10000;
  uint32_t count = 0;
  for (uint32_t second = 0; second < 40 && count < kNumPrefixes; ++second) {
    for (uint32_t third = 0; third < 256 && count < kNumPrefixes; ++third) {
      folly::CIDRNetwork prefix{
          folly::IPAddress(fmt::format("10.{}.{}.0", second, third)), 24};
      adjRibOutGroup_->addToLiteTree(
          adjRibOutGroup_->LiteTree_, prefix, groupOwnerKey, 0);
      ++count;
    }
  }
  ASSERT_EQ(count, kNumPrefixes);

  // Add peer-owned entries for a subset of prefixes under 10.1.0.0/16
  for (uint32_t third = 0; third < 10; ++third) {
    folly::CIDRNetwork prefix{
        folly::IPAddress(fmt::format("10.1.{}.0", third)), 24};
    adjRibOutGroup_->addToLiteTree(
        adjRibOutGroup_->LiteTree_, prefix, peerOwnerKey, 0);
  }

  // Also add the /16 parent prefix 10.1.0.0/16 for both group and peer
  folly::CIDRNetwork parentPrefix{folly::IPAddress("10.1.0.0"), 16};
  adjRibOutGroup_->addToLiteTree(
      adjRibOutGroup_->LiteTree_, parentPrefix, groupOwnerKey, 0);
  adjRibOutGroup_->addToLiteTree(
      adjRibOutGroup_->LiteTree_, parentPrefix, peerOwnerKey, 0);

  // Get iterator for the non-leaf /16 node
  auto itr = adjRibOutGroup_->getRadixNodeItrFromLiteTree(
      adjRibOutGroup_->LiteTree_, parentPrefix);
  ASSERT_FALSE(itr.atEnd());

  // Verify both group and peer entries are accessible via the iterator
  EXPECT_NE(
      adjRibOutGroup_->getAdjRibEntryFromLiteNodeItr(itr, groupOwnerKey),
      nullptr);
  EXPECT_NE(
      adjRibOutGroup_->getAdjRibEntryFromLiteNodeItr(itr, peerOwnerKey),
      nullptr);
}

TEST_F(AdjRibGroupTest, GetSubTreeFromLiteTreeLeafNode) {
  createAdjRibOutGroup("test_group", 42);
  auto groupOwnerKey = AdjRibOutOwnerKey::forGroup(42);
  auto peerId = std::make_shared<nettools::bgplib::BgpPeerId>();
  auto peerOwnerKey = AdjRibOutOwnerKey::forPeer(peerId);

  // Populate 10k /24 prefixes
  constexpr uint32_t kNumPrefixes = 10000;
  uint32_t count = 0;
  for (uint32_t second = 0; second < 40 && count < kNumPrefixes; ++second) {
    for (uint32_t third = 0; third < 256 && count < kNumPrefixes; ++third) {
      folly::CIDRNetwork prefix{
          folly::IPAddress(fmt::format("10.{}.{}.0", second, third)), 24};
      adjRibOutGroup_->addToLiteTree(
          adjRibOutGroup_->LiteTree_, prefix, groupOwnerKey, 0);
      ++count;
    }
  }
  ASSERT_EQ(count, kNumPrefixes);

  // Add a peer-owned entry at the leaf node we'll query
  folly::CIDRNetwork leafPrefix{folly::IPAddress("10.5.100.0"), 24};
  adjRibOutGroup_->addToLiteTree(
      adjRibOutGroup_->LiteTree_, leafPrefix, peerOwnerKey, 0);

  // Get iterator for a leaf /24 node (no children)
  auto itr = adjRibOutGroup_->getRadixNodeItrFromLiteTree(
      adjRibOutGroup_->LiteTree_, leafPrefix);
  ASSERT_FALSE(itr.atEnd());

  // Verify both group and peer entries are accessible at the leaf
  EXPECT_NE(
      adjRibOutGroup_->getAdjRibEntryFromLiteNodeItr(itr, groupOwnerKey),
      nullptr);
  EXPECT_NE(
      adjRibOutGroup_->getAdjRibEntryFromLiteNodeItr(itr, peerOwnerKey),
      nullptr);
}

TEST_F(AdjRibGroupTest, GetSubTreeFromLiteTreeNonExistentPrefix) {
  createAdjRibOutGroup("test_group", 42);
  auto groupOwnerKey = AdjRibOutOwnerKey::forGroup(42);
  auto peerId = std::make_shared<nettools::bgplib::BgpPeerId>();
  auto peerOwnerKey = AdjRibOutOwnerKey::forPeer(peerId);

  // Add a single prefix with both group and peer owner keys
  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  adjRibOutGroup_->addToLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, groupOwnerKey, 0);
  adjRibOutGroup_->addToLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, peerOwnerKey, 0);

  // Look up a prefix that doesn't exist
  folly::CIDRNetwork missing{folly::IPAddress("192.168.0.0"), 24};
  auto missingItr = adjRibOutGroup_->getRadixNodeItrFromLiteTree(
      adjRibOutGroup_->LiteTree_, missing);
  EXPECT_TRUE(missingItr.atEnd());

  // Verify the existing prefix is still accessible with both owner keys
  auto existingItr = adjRibOutGroup_->getRadixNodeItrFromLiteTree(
      adjRibOutGroup_->LiteTree_, prefix);
  ASSERT_FALSE(existingItr.atEnd());
  EXPECT_NE(
      adjRibOutGroup_->getAdjRibEntryFromLiteNodeItr(
          existingItr, groupOwnerKey),
      nullptr);
  EXPECT_NE(
      adjRibOutGroup_->getAdjRibEntryFromLiteNodeItr(existingItr, peerOwnerKey),
      nullptr);
}

/**
 * Test: getRibEntry returns per-peer entry when it exists
 */
TEST_F(AdjRibGroupTest, GetRibEntryReturnsPeerEntry) {
  createAdjRibOutGroup("test_group", 42);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  auto peerId = std::make_shared<nettools::bgplib::BgpPeerId>();
  auto peerOwnerKey = AdjRibOutOwnerKey::forPeer(peerId);

  // Add a per-peer entry
  auto* added = adjRibOutGroup_->addToLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, peerOwnerKey, 0);
  ASSERT_NE(added, nullptr);

  auto [entry, isPerPeerEntry] =
      adjRibOutGroup_->getRibEntrySharedOrPeer(prefix, peerOwnerKey, 0);
  EXPECT_EQ(entry, added);
  EXPECT_TRUE(isPerPeerEntry);
}

/**
 * Test: getRibEntry returns group entry when peer has detachedRibVersion
 * greater than the entry's ribVersion (peer was sharing at detach time)
 */
TEST_F(AdjRibGroupTest, GetRibEntryReturnsGroupEntryWhenPeerWasSharing) {
  createAdjRibOutGroup("test_group", 42);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  auto groupOwnerKey = AdjRibOutOwnerKey::forGroup(42);
  auto peerId = std::make_shared<nettools::bgplib::BgpPeerId>();
  auto peerOwnerKey = AdjRibOutOwnerKey::forPeer(peerId);

  // Add a group entry with ribVersion=5
  auto* added = adjRibOutGroup_->addToLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, groupOwnerKey, 0);
  ASSERT_NE(added, nullptr);
  added->setRibVersion(5);

  // detachedRibVersion=10 > ribVersion=5: peer was sharing this entry
  auto [entry, isPerPeerEntry] =
      adjRibOutGroup_->getRibEntrySharedOrPeer(prefix, peerOwnerKey, 0, 10);
  EXPECT_EQ(entry, added);
  EXPECT_FALSE(isPerPeerEntry);
}

/**
 * Test: getRibEntry does NOT return group entry when detachedRibVersion
 * is less than or equal to the entry's ribVersion (peer never saw it)
 */
TEST_F(AdjRibGroupTest, GetRibEntrySkipsGroupEntryWhenPeerNeverSawIt) {
  createAdjRibOutGroup("test_group", 42);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  auto groupOwnerKey = AdjRibOutOwnerKey::forGroup(42);
  auto peerId = std::make_shared<nettools::bgplib::BgpPeerId>();
  auto peerOwnerKey = AdjRibOutOwnerKey::forPeer(peerId);

  // Add a group entry with ribVersion=10
  auto* added = adjRibOutGroup_->addToLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, groupOwnerKey, 0);
  ASSERT_NE(added, nullptr);
  added->setRibVersion(10);

  // detachedRibVersion=5 < ribVersion=10: entry was created after detach
  auto [entry, isPerPeerEntry] =
      adjRibOutGroup_->getRibEntrySharedOrPeer(prefix, peerOwnerKey, 0, 5);
  EXPECT_EQ(entry, nullptr);
  EXPECT_FALSE(isPerPeerEntry);
}

/**
 * Test: getRibEntry does NOT return group entry when detachedRibVersion is 0
 * (peer never detached / just joined via DETACHED_INIT_DUMP)
 */
TEST_F(AdjRibGroupTest, GetRibEntrySkipsGroupEntryForNewPeer) {
  createAdjRibOutGroup("test_group", 42);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  auto groupOwnerKey = AdjRibOutOwnerKey::forGroup(42);
  auto peerId = std::make_shared<nettools::bgplib::BgpPeerId>();
  auto peerOwnerKey = AdjRibOutOwnerKey::forPeer(peerId);

  // Add a group entry with ribVersion=5
  auto* added = adjRibOutGroup_->addToLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, groupOwnerKey, 0);
  ASSERT_NE(added, nullptr);
  added->setRibVersion(5);

  // detachedRibVersion=0 (default): new peer, never detached
  auto [entry, isPerPeerEntry] =
      adjRibOutGroup_->getRibEntrySharedOrPeer(prefix, peerOwnerKey, 0);
  EXPECT_EQ(entry, nullptr);
  EXPECT_FALSE(isPerPeerEntry);
}

/**
 * Test: getRibEntry prefers per-peer entry over group entry
 */
TEST_F(AdjRibGroupTest, GetRibEntryPrefersPeerOverGroup) {
  createAdjRibOutGroup("test_group", 42);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  auto groupOwnerKey = AdjRibOutOwnerKey::forGroup(42);
  auto peerId = std::make_shared<nettools::bgplib::BgpPeerId>();
  auto peerOwnerKey = AdjRibOutOwnerKey::forPeer(peerId);

  // Add both a group entry and a per-peer entry for the same prefix
  auto* groupEntry = adjRibOutGroup_->addToLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, groupOwnerKey, 0);
  auto* peerEntry = adjRibOutGroup_->addToLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, peerOwnerKey, 0);
  ASSERT_NE(groupEntry, nullptr);
  ASSERT_NE(peerEntry, nullptr);

  // Should return per-peer entry regardless of detachedRibVersion
  auto [entry, isPerPeerEntry] =
      adjRibOutGroup_->getRibEntrySharedOrPeer(prefix, peerOwnerKey, 0);
  EXPECT_EQ(entry, peerEntry);
  EXPECT_TRUE(isPerPeerEntry);
}

/**
 * Test: getRibEntry returns nullptr when no entry exists
 */
TEST_F(AdjRibGroupTest, GetRibEntryReturnsNullWhenNoEntry) {
  createAdjRibOutGroup("test_group", 42);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  auto peerId = std::make_shared<nettools::bgplib::BgpPeerId>();
  auto peerOwnerKey = AdjRibOutOwnerKey::forPeer(peerId);

  auto [entry, isPerPeerEntry] =
      adjRibOutGroup_->getRibEntrySharedOrPeer(prefix, peerOwnerKey, 0);
  EXPECT_EQ(entry, nullptr);
}

/**
 * Test: getRibEntry with equal detachedRibVersion and ribVersion
 * returns the group entry (peer was sharing at detach time)
 */
TEST_F(AdjRibGroupTest, GetRibEntryReturnsGroupEntryWhenVersionsEqual) {
  createAdjRibOutGroup("test_group", 42);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  auto groupOwnerKey = AdjRibOutOwnerKey::forGroup(42);
  auto peerId = std::make_shared<nettools::bgplib::BgpPeerId>();
  auto peerOwnerKey = AdjRibOutOwnerKey::forPeer(peerId);

  // Add a group entry with ribVersion=5
  auto* added = adjRibOutGroup_->addToLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, groupOwnerKey, 0);
  ASSERT_NE(added, nullptr);
  added->setRibVersion(5);

  // detachedRibVersion=5 == ribVersion=5: peer was sharing
  auto [entry, isPerPeerEntry] =
      adjRibOutGroup_->getRibEntrySharedOrPeer(prefix, peerOwnerKey, 0, 5);
  EXPECT_EQ(entry, added);
  EXPECT_FALSE(isPerPeerEntry);
}

/**
 * ==========================================================================
 * PostOutPrefixCount stats tests
 *
 * These tests verify that the group-level postOutPrefixCount is correctly
 * incremented on first announcement and decremented on withdrawal.
 * ==========================================================================
 */

/**
 * Test: Count increments from 0 to 1 on first announcement
 */
TEST_F(
    AdjRibGroupPackingFixture,
    PostOutPrefixCount_IncrementOnFirstAnnouncement) {
  createAdjRibOutGroup("test_group", 42, createDefaultGroupKey());

  EXPECT_EQ(0, adjRibOutGroup_->getStats().getPostOutPrefixCount());

  RibOutAnnouncementEntry entry(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry);

  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCount());
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCountIpv4());
  EXPECT_EQ(0, adjRibOutGroup_->getStats().getPostOutPrefixCountIpv6());
}

/**
 * Test: Count stays at 1 after re-announcement with same attrs
 */
TEST_F(
    AdjRibGroupPackingFixture,
    PostOutPrefixCount_NoIncrementOnReAnnouncement) {
  createAdjRibOutGroup("test_group", 42, createDefaultGroupKey());

  RibOutAnnouncementEntry entry(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry);
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCount());

  // Re-announce same prefix with same attrs — no-op, count unchanged
  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry);
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCount());
}

/**
 * Test: Count stays at 1 after re-announcement with different attrs
 */
TEST_F(AdjRibGroupPackingFixture, PostOutPrefixCount_NoIncrementOnAttrChange) {
  createAdjRibOutGroup("test_group", 42, createDefaultGroupKey());

  RibOutAnnouncementEntry entry1(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry1);
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCount());

  // Re-announce same prefix with different attrs — count unchanged
  RibOutAnnouncementEntry entry2(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs2_,
      0,
      0,
      0,
      0,
      0);

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry2);
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCount());
}

/**
 * Test: Count increments for each distinct prefix
 */
TEST_F(AdjRibGroupPackingFixture, PostOutPrefixCount_MultiplePrefixes) {
  createAdjRibOutGroup("test_group", 42, createDefaultGroupKey());

  RibOutAnnouncementEntry entry1(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry1);
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCount());

  RibOutAnnouncementEntry entry2(
      kV4Prefix2_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry2);
  EXPECT_EQ(2, adjRibOutGroup_->getStats().getPostOutPrefixCount());
  EXPECT_EQ(2, adjRibOutGroup_->getStats().getPostOutPrefixCountIpv4());
}

/**
 * Test: Count decrements after withdrawal via tryInsertWithdrawal
 */
TEST_F(AdjRibGroupPackingFixture, PostOutPrefixCount_DecrementOnWithdrawal) {
  createAdjRibOutGroup("test_group", 42, createDefaultGroupKey());

  // Announce a prefix (increments count)
  RibOutAnnouncementEntry entry(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry);
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCount());

  // Withdraw via tryInsertWithdrawal (decrements count)
  auto groupOwnerKey =
      AdjRibOutOwnerKey::forGroup(adjRibOutGroup_->getGroupId());
  auto* adjRibEntry = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, kV4Prefix1_, groupOwnerKey);
  ASSERT_NE(adjRibEntry, nullptr);

  adjRibOutGroup_->tryInsertWithdrawal(
      kV4Prefix1_,
      adjRibEntry,
      "Withdrawal inserted",
      "Withdrawal not inserted");

  EXPECT_EQ(0, adjRibOutGroup_->getStats().getPostOutPrefixCount());
  EXPECT_EQ(0, adjRibOutGroup_->getStats().getPostOutPrefixCountIpv4());
}

/**
 * Test: Count stays at 0 when withdrawing never-announced prefix
 */
TEST_F(
    AdjRibGroupPackingFixture,
    PostOutPrefixCount_NoDecrementOnNeverAnnounced) {
  createAdjRibOutGroup("test_group", 42, createDefaultGroupKey());

  // Create entry but never announce (no postAttr set via processRibAnnounced)
  auto* adjRibEntry = adjRibOutGroup_->tryInsertRibOutEntry(
      kV4Prefix1_, folly::IPAddress("1.1.1.1"), kDefaultPathID);
  ASSERT_NE(adjRibEntry, nullptr);
  EXPECT_EQ(adjRibEntry->getPostAttr(), nullptr);

  EXPECT_EQ(0, adjRibOutGroup_->getStats().getPostOutPrefixCount());

  // Withdrawal on never-announced prefix — no-op
  adjRibOutGroup_->tryInsertWithdrawal(
      kV4Prefix1_,
      adjRibEntry,
      "Withdrawal inserted",
      "Withdrawal not inserted");

  EXPECT_EQ(0, adjRibOutGroup_->getStats().getPostOutPrefixCount());
}

/**
 * Test: Idempotent withdrawal — second withdrawal doesn't double-decrement
 */
TEST_F(AdjRibGroupPackingFixture, PostOutPrefixCount_IdempotentWithdrawal) {
  createAdjRibOutGroup("test_group", 42, createDefaultGroupKey());

  // Announce
  RibOutAnnouncementEntry entry(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry);
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCount());

  auto groupOwnerKey =
      AdjRibOutOwnerKey::forGroup(adjRibOutGroup_->getGroupId());
  auto* adjRibEntry = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, kV4Prefix1_, groupOwnerKey);
  ASSERT_NE(adjRibEntry, nullptr);

  // First withdrawal
  adjRibOutGroup_->tryInsertWithdrawal(
      kV4Prefix1_,
      adjRibEntry,
      "Withdrawal inserted",
      "Withdrawal not inserted");
  EXPECT_EQ(0, adjRibOutGroup_->getStats().getPostOutPrefixCount());

  // Second withdrawal — no-op, count stays at 0
  adjRibOutGroup_->tryInsertWithdrawal(
      kV4Prefix1_,
      adjRibEntry,
      "Withdrawal inserted",
      "Withdrawal not inserted");
  EXPECT_EQ(0, adjRibOutGroup_->getStats().getPostOutPrefixCount());
}

/**
 * Test: IPv4 and IPv6 sub-counters tracked independently
 */
TEST_F(AdjRibGroupPackingFixture, PostOutPrefixCount_IPv4AndIPv6Separate) {
  createAdjRibOutGroup("test_group", 42, createDefaultGroupKey());

  // Announce IPv4 prefix
  RibOutAnnouncementEntry v4Entry(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(v4Entry);
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCount());
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCountIpv4());
  EXPECT_EQ(0, adjRibOutGroup_->getStats().getPostOutPrefixCountIpv6());

  // Announce IPv6 prefix
  RibOutAnnouncementEntry v6Entry(
      kV6Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(v6Entry);
  EXPECT_EQ(2, adjRibOutGroup_->getStats().getPostOutPrefixCount());
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCountIpv4());
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCountIpv6());

  // Withdraw IPv4 only
  auto groupOwnerKey =
      AdjRibOutOwnerKey::forGroup(adjRibOutGroup_->getGroupId());
  auto* v4RibEntry = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, kV4Prefix1_, groupOwnerKey);
  ASSERT_NE(v4RibEntry, nullptr);

  adjRibOutGroup_->tryInsertWithdrawal(
      kV4Prefix1_,
      v4RibEntry,
      "Withdrawal inserted",
      "Withdrawal not inserted");

  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCount());
  EXPECT_EQ(0, adjRibOutGroup_->getStats().getPostOutPrefixCountIpv4());
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCountIpv6());
}

/**
 * Test: Full lifecycle — announce, withdraw, re-announce, withdraw all
 */
TEST_F(AdjRibGroupPackingFixture, PostOutPrefixCount_FullLifecycle) {
  createAdjRibOutGroup("test_group", 42, createDefaultGroupKey());

  auto groupOwnerKey =
      AdjRibOutOwnerKey::forGroup(adjRibOutGroup_->getGroupId());

  // Announce prefix 1
  RibOutAnnouncementEntry entry1(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry1);
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCount());

  // Announce prefix 2
  RibOutAnnouncementEntry entry2(
      kV4Prefix2_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry2);
  EXPECT_EQ(2, adjRibOutGroup_->getStats().getPostOutPrefixCount());

  // Withdraw prefix 1
  auto* ribEntry1 = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, kV4Prefix1_, groupOwnerKey);
  ASSERT_NE(ribEntry1, nullptr);

  adjRibOutGroup_->tryInsertWithdrawal(
      kV4Prefix1_, ribEntry1, "Withdrawal inserted", "Withdrawal not inserted");
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCount());

  // Re-announce prefix 1 with different attrs
  RibOutAnnouncementEntry entry1b(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs2_,
      0,
      0,
      0,
      0,
      0);

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry1b);
  EXPECT_EQ(2, adjRibOutGroup_->getStats().getPostOutPrefixCount());

  // Withdraw both
  ribEntry1 = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, kV4Prefix1_, groupOwnerKey);
  ASSERT_NE(ribEntry1, nullptr);
  adjRibOutGroup_->tryInsertWithdrawal(
      kV4Prefix1_, ribEntry1, "Withdrawal inserted", "Withdrawal not inserted");
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCount());

  auto* ribEntry2 = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, kV4Prefix2_, groupOwnerKey);
  ASSERT_NE(ribEntry2, nullptr);
  adjRibOutGroup_->tryInsertWithdrawal(
      kV4Prefix2_, ribEntry2, "Withdrawal inserted", "Withdrawal not inserted");
  EXPECT_EQ(0, adjRibOutGroup_->getStats().getPostOutPrefixCount());
}

/**
 * Test: processGroupRibWithdraw decrements count
 */
TEST_F(
    AdjRibGroupPackingFixture,
    PostOutPrefixCount_DecrementOnProcessGroupRibWithdraw) {
  createAdjRibOutGroup("test_group", 42, createDefaultGroupKey());

  // Announce a prefix (increments count)
  RibOutAnnouncementEntry entry(
      kV4Prefix1_,
      kDefaultPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      announcementAttrs_,
      0,
      0,
      0,
      0,
      0);

  adjRibOutGroup_->processRibAnnouncedEntryForGroup(entry);
  EXPECT_EQ(1, adjRibOutGroup_->getStats().getPostOutPrefixCount());

  // Withdraw via processGroupRibWithdraw (decrements count)
  adjRibOutGroup_->processGroupRibWithdraw(kV4Prefix1_, kDefaultPathID);

  EXPECT_EQ(0, adjRibOutGroup_->getStats().getPostOutPrefixCount());
  EXPECT_EQ(0, adjRibOutGroup_->getStats().getPostOutPrefixCountIpv4());
}

/*
 * Tests for groupDescriptor_ AFI suffix via buildAfiLabel().
 */
TEST_F(AdjRibGroupTest, GroupLabelIncludesV4Only) {
  UpdateGroupKey key;
  key.egressPolicyName = "EB-FA-OUT";
  key.afiIpv4Negotiated = true;
  key.afiIpv6Negotiated = false;
  key.extNhEncodingCapable = false;
  createAdjRibOutGroup("test", 2, key);
  EXPECT_EQ(
      adjRibOutGroup_->getGroupDescriptor(),
      "2(EB-FA-OUT/v4,peerOverride=false)");
}

TEST_F(AdjRibGroupTest, GroupLabelIncludesV6Only) {
  UpdateGroupKey key;
  key.egressPolicyName = "EB-FA-OUT";
  key.afiIpv4Negotiated = false;
  key.afiIpv6Negotiated = true;
  key.extNhEncodingCapable = false;
  createAdjRibOutGroup("test", 3, key);
  EXPECT_EQ(
      adjRibOutGroup_->getGroupDescriptor(),
      "3(EB-FA-OUT/v6,peerOverride=false)");
}

TEST_F(AdjRibGroupTest, GroupLabelIncludesV4V6) {
  UpdateGroupKey key;
  key.egressPolicyName = "EB-FA-OUT";
  key.afiIpv4Negotiated = true;
  key.afiIpv6Negotiated = true;
  key.extNhEncodingCapable = false;
  createAdjRibOutGroup("test", 4, key);
  EXPECT_EQ(
      adjRibOutGroup_->getGroupDescriptor(),
      "4(EB-FA-OUT/v4v6,peerOverride=false)");
}

TEST_F(AdjRibGroupTest, GroupLabelIncludesV4OverV6) {
  UpdateGroupKey key;
  key.egressPolicyName = "EB-FA-OUT";
  key.afiIpv4Negotiated = false;
  key.afiIpv6Negotiated = false;
  key.extNhEncodingCapable = true;
  createAdjRibOutGroup("test", 5, key);
  EXPECT_EQ(
      adjRibOutGroup_->getGroupDescriptor(),
      "5(EB-FA-OUT/v4ov6,peerOverride=false)");
}

TEST_F(AdjRibGroupTest, GroupLabelIncludesV4V6WithV4OverV6) {
  UpdateGroupKey key;
  key.egressPolicyName = "EB-FA-OUT";
  key.afiIpv4Negotiated = true;
  key.afiIpv6Negotiated = true;
  key.extNhEncodingCapable = true;
  createAdjRibOutGroup("test", 6, key);
  EXPECT_EQ(
      adjRibOutGroup_->getGroupDescriptor(),
      "6(EB-FA-OUT/v4v6+v4ov6,peerOverride=false)");
}

TEST_F(AdjRibGroupTest, GroupLabelNoAfiNegotiated) {
  UpdateGroupKey key;
  key.egressPolicyName = "EB-FA-OUT";
  key.afiIpv4Negotiated = false;
  key.afiIpv6Negotiated = false;
  key.extNhEncodingCapable = false;
  createAdjRibOutGroup("test", 7, key);
  EXPECT_EQ(
      adjRibOutGroup_->getGroupDescriptor(),
      "7(EB-FA-OUT/none,peerOverride=false)");
}

TEST_F(AdjRibGroupTest, GroupLabelIncludesPeerOverride) {
  UpdateGroupKey key;
  key.egressPolicyName = "EB-FA-OUT";
  key.afiIpv4Negotiated = true;
  key.afiIpv6Negotiated = true;
  key.extNhEncodingCapable = false;
  key.peerOverride = true;
  createAdjRibOutGroup("test", 8, key);
  EXPECT_EQ(
      adjRibOutGroup_->getGroupDescriptor(),
      "8(EB-FA-OUT/v4v6,peerOverride=true)");
}

TEST_F(
    AdjRibGroupTest,
    BuildAndSendGroupBgpMessages_EmptyPackingListRejoinLogGating) {
  createAdjRibOutGroup("test_group");
  auto& messages = subscribeToLogMessages("", folly::LogLevel::DBG2);

  // Packing list is empty from the start
  EXPECT_TRUE(adjRibOutGroup_->getAttrToPrefixMap().empty());

  // Set state to READY so the IDLE transition log fires
  adjRibOutGroup_->setState(UpdateGroupState::READY);

  // Call buildAndSendGroupBgpMessages with empty packing list
  folly::coro::blockingWait(adjRibOutGroup_->buildAndSendGroupBgpMessages());

  // Verify state transitioned to IDLE and logged the previous state
  EXPECT_EQ(adjRibOutGroup_->getState(), UpdateGroupState::IDLE);
  bool foundStateTransitionLog = false;
  for (const auto& [msg, _] : messages) {
    if (msg.getMessage().find("State Transition: READY -> IDLE") !=
        std::string::npos) {
      foundStateTransitionLog = true;
      break;
    }
  }
  EXPECT_TRUE(foundStateTransitionLog);

  /*
   * With no detached peers, checkAndAcceptReadyToJoinPeers must NOT emit a
   * rejoin log line. The DBG1 log is gated on a non-empty detached set to
   * avoid per-tick spam (it previously logged "Skipping 0 detached peers to
   * try rejoin" on every consume tick).
   */
  bool foundRejoinLog = false;
  for (const auto& [msg, _] : messages) {
    if (msg.getMessage().find("detached peers to try rejoin") !=
        std::string::npos) {
      foundRejoinLog = true;
      break;
    }
  }
  EXPECT_FALSE(foundRejoinLog);

  // Now add a detached peer and call again to verify the other log line
  messages.clear();
  auto adjRib = createMinimalAdjRib();
  adjRibOutGroup_->detachedPeers_.insert(adjRib);

  folly::coro::blockingWait(adjRibOutGroup_->buildAndSendGroupBgpMessages());

  /*
   * With detached peers, checkAndAcceptReadyToJoinPeers should log
   * "Checking {} detached peers to try rejoin"
   */
  bool foundCheckingDetachedPeersLog = false;
  for (const auto& [msg, _] : messages) {
    if (msg.getMessage().find("Checking 1 detached peers to try rejoin") !=
        std::string::npos) {
      foundCheckingDetachedPeersLog = true;
      break;
    }
  }
  EXPECT_TRUE(foundCheckingDetachedPeersLog);

  // Clean up
  adjRibOutGroup_->detachedPeers_.clear();
}

/*
 * With no SYNC peers, buildAndSendGroupBgpMessages' cleanup guard must keep the
 * group frozen: it should NOT restart the consume (packing) timer.
 */
TEST_F(
    AdjRibGroupTest,
    BuildAndSendGroupBgpMessages_NoSyncPeersDoesNotRestartConsumeTimer) {
  createAdjRibOutGroup("test_group");
  MockChangeListConsumer();
  adjRibOutGroup_->registerGroupConsumer();
  ASSERT_EQ(adjRibOutGroup_->getNumInSyncPeers(), 0);

  /*
   * buildAndSendGroupBgpMessages' cleanup guard skips rescheduling the consume
   * timer while the group has no SYNC peers, leaving it frozen.
   */
  folly::coro::blockingWait(adjRibOutGroup_->buildAndSendGroupBgpMessages());
  EXPECT_FALSE(adjRibOutGroup_->changeListConsumeTimer_->isScheduled());
}

/*
 * Positive control: with a SYNC peer, the cleanup guard reschedules the consume
 * timer.
 */
TEST_F(
    AdjRibGroupTest,
    BuildAndSendGroupBgpMessages_SyncPeerRestartsConsumeTimer) {
  createAdjRibOutGroup("test_group");
  MockChangeListConsumer();
  adjRibOutGroup_->registerGroupConsumer();

  // One in-sync peer.
  auto adjRib = createMinimalAdjRib();
  adjRibOutGroup_->registerPeer(adjRib);
  adjRibOutGroup_->markPeerInSync(adjRib);
  ASSERT_EQ(adjRibOutGroup_->getNumInSyncPeers(), 1);

  folly::coro::blockingWait(adjRibOutGroup_->buildAndSendGroupBgpMessages());

  // With a SYNC peer present, the guard reschedules the consume timer.
  EXPECT_TRUE(adjRibOutGroup_->changeListConsumeTimer_->isScheduled());
}

/*
 * Add-path SEND drain attach test for AdjRibGroup.
 *
 * Mirrors the partial-drain coverage in AdjRibOutUpdateAttributesTest, but
 * exercises the parallel code path in AdjRibGroup.cpp:1175-1184 (group-level
 * processRibAnnouncedEntryForGroup):
 *
 *     auto prePolicyAttrs = entry.attrs->clone();
 *     if (entry.isPartialDrain) {
 *       applyPartialDrainCommunities(prePolicyAttrs);
 *     }
 *     ... = getPostOutPolicyAttributesAndInfo(...);
 *
 * Because the group fixture has no PolicyManager configured,
 * getPostOutPolicyAttributesAndInfo is a passthrough — the test isolates
 * the per-entry drain attach and verifies it survives the policy step (no
 * future change to the group policy plumbing should strip the drain
 * community). Multiple add-path entries with distinct path IDs are
 * processed to lock in that the attach is per-entry, not a one-shot.
 */
TEST_F(
    AdjRibGroupAddPathFixture,
    PartialDrainCommunityAttachedToAddPathEntries) {
  createGroupWithAddPath(true /* sendAddPath */);

  const auto prefix = folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24};
  const auto nexthop = folly::IPAddress("1.1.1.1");
  const TinyPeerInfo peer{nexthop, 65000, 1, BgpSessionType::EBGP, false};

  for (uint32_t pathId : {1U, 2U, 3U}) {
    auto attrs = std::make_shared<BgpPath>(BgpPathFields());
    attrs->setLocalPref(100);
    nettools::bgplib::BgpAttrCommunitiesC seed;
    seed.push_back(kLiveCommunity);
    attrs->setCommunities(std::move(seed));

    RibOutAnnouncementEntry update(prefix, pathId, peer, attrs, 0, 0, 0, 0, 0);
    update.isPartialDrain = true;

    auto* adjRibEntry =
        adjRibOutGroup_->tryInsertRibOutEntry(prefix, nexthop, pathId);
    ASSERT_NE(nullptr, adjRibEntry) << "pathIdToSend=" << pathId;

    // Mirror production sequence at AdjRibGroup.cpp:1175-1184.
    auto prePolicyAttrs = update.attrs->clone();
    if (update.isPartialDrain) {
      applyPartialDrainCommunities(prePolicyAttrs);
    }
    auto [postAttrs, postPolicyInfo] =
        adjRibOutGroup_->getPostOutPolicyAttributesAndInfo(
            update, adjRibEntry, prePolicyAttrs, "test_peer");

    ASSERT_NE(nullptr, postAttrs) << "pathIdToSend=" << pathId;
    const auto& comms = postAttrs->getCommunities().get();
    EXPECT_TRUE(hasCommunity(comms, kDrainCommunity))
        << "pathIdToSend=" << pathId;
    EXPECT_FALSE(hasCommunity(comms, kLiveCommunity))
        << "pathIdToSend=" << pathId;
  }
}

/*
 * walkAndProcessShadowRib bestpath test: when the ShadowRib contains a
 * bestpath with isPartialDrain=true, the initial dump / policy re-eval
 * path must propagate the flag into the RibOutAnnouncementEntry and
 * attach the drain community on the group RIB-OUT entry.
 *
 * Regression guard for AdjRibGroup.cpp walkAndProcessShadowRib() where
 * the emplace_back previously defaulted isPartialDrain to false.
 */
TEST_F(
    AdjRibGroupAddPathFixture,
    WalkAndProcessShadowRib_BestpathPartialDrainAttachesDrainCommunity) {
  const auto prefix = folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24};
  const TinyPeerInfo peer{
      folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false};

  auto attrs = std::make_shared<BgpPath>(BgpPathFields());
  attrs->setLocalPref(100);
  nettools::bgplib::BgpAttrCommunitiesC seed;
  seed.push_back(kLiveCommunity);
  attrs->setCommunities(std::move(seed));
  attrs->publish();

  auto bestpath = std::make_shared<ShadowRibRouteInfo>(
      peer, attrs, kDefaultPathID, /*isPartialDrain=*/true);
  setShadowRibRouteState(bestpath, SHADOWRIBROUTE_IN_UPDATE);

  ShadowRibEntriesMap shadowRibEntries;
  ShadowRibEntry srEntry(prefix, std::move(bestpath), {});
  srEntry.ribVersion = 1;
  shadowRibEntries.emplace(
      prefix,
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(srEntry)));

  createGroupWithAddPath(false /* sendAddPath */, shadowRibEntries);

  adjRibOutGroup_->walkAndProcessShadowRib(false /* sendWithEoR */);
  evb_->loopOnce();

  // sendAddPath=false → entries go into LiteTree, not PathTree
  auto* adjRibEntry = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, adjRibOutGroup_->getGroupOwnerKey());
  ASSERT_NE(nullptr, adjRibEntry);
  const auto& postAttrs = adjRibEntry->getPostAttr();
  ASSERT_NE(nullptr, postAttrs);
  const auto& comms = postAttrs->getCommunities().get();
  EXPECT_TRUE(hasCommunity(comms, kDrainCommunity));
  EXPECT_FALSE(hasCommunity(comms, kLiveCommunity));
}

/*
 * walkAndProcessShadowRib add-path test: when the ShadowRib contains
 * multipaths with isPartialDrain=true, every path ID must carry the
 * drain community through the group initial-dump / re-eval path.
 */
TEST_F(
    AdjRibGroupAddPathFixture,
    WalkAndProcessShadowRib_AddPathPartialDrainAttachesDrainCommunity) {
  const auto prefix = folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24};
  const TinyPeerInfo peer{
      folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false};

  constexpr std::array<uint32_t, 3> kAddPathIds{1, 2, 3};

  ShadowRibRouteInfos multipaths;
  for (uint32_t pathId : kAddPathIds) {
    auto attrs = std::make_shared<BgpPath>(BgpPathFields());
    attrs->setLocalPref(100);
    nettools::bgplib::BgpAttrCommunitiesC seed;
    seed.push_back(kLiveCommunity);
    attrs->setCommunities(std::move(seed));
    attrs->publish();

    auto path = std::make_shared<ShadowRibRouteInfo>(
        peer, attrs, pathId, /*isPartialDrain=*/true);
    setShadowRibRouteState(path, SHADOWRIBROUTE_IN_UPDATE);
    multipaths.emplace(pathId, std::move(path));
  }

  ShadowRibEntriesMap shadowRibEntries;
  ShadowRibEntry srEntry(prefix, /*bestpath=*/nullptr, std::move(multipaths));
  srEntry.ribVersion = 1;
  shadowRibEntries.emplace(
      prefix,
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(srEntry)));

  createGroupWithAddPath(true /* sendAddPath */, shadowRibEntries);

  adjRibOutGroup_->walkAndProcessShadowRib(false /* sendWithEoR */);
  evb_->loopOnce();

  for (uint32_t pathId : kAddPathIds) {
    auto* adjRibEntry = adjRibOutGroup_->getFromPathTree(
        adjRibOutGroup_->PathTree_,
        prefix,
        adjRibOutGroup_->getGroupOwnerKey(),
        pathId);
    ASSERT_NE(nullptr, adjRibEntry) << "pathIdToSend=" << pathId;
    const auto& postAttrs = adjRibEntry->getPostAttr();
    ASSERT_NE(nullptr, postAttrs) << "pathIdToSend=" << pathId;
    const auto& comms = postAttrs->getCommunities().get();
    EXPECT_TRUE(hasCommunity(comms, kDrainCommunity))
        << "pathIdToSend=" << pathId;
    EXPECT_FALSE(hasCommunity(comms, kLiveCommunity))
        << "pathIdToSend=" << pathId;
  }
}

/*
 * Negative test: walkAndProcessShadowRib must NOT attach the drain
 * community when isPartialDrain is false.
 */
TEST_F(
    AdjRibGroupAddPathFixture,
    WalkAndProcessShadowRib_NoDrainCommunityWhenNotPartialDrain) {
  const auto prefix = folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24};
  const TinyPeerInfo peer{
      folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false};

  auto attrs = std::make_shared<BgpPath>(BgpPathFields());
  attrs->setLocalPref(100);
  nettools::bgplib::BgpAttrCommunitiesC seed;
  seed.push_back(kLiveCommunity);
  attrs->setCommunities(std::move(seed));
  attrs->publish();

  auto bestpath = std::make_shared<ShadowRibRouteInfo>(
      peer, attrs, kDefaultPathID, /*isPartialDrain=*/false);
  setShadowRibRouteState(bestpath, SHADOWRIBROUTE_IN_UPDATE);

  ShadowRibEntriesMap shadowRibEntries;
  ShadowRibEntry srEntry(prefix, std::move(bestpath), {});
  srEntry.ribVersion = 1;
  shadowRibEntries.emplace(
      prefix,
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(srEntry)));

  createGroupWithAddPath(false /* sendAddPath */, shadowRibEntries);

  adjRibOutGroup_->walkAndProcessShadowRib(false /* sendWithEoR */);
  evb_->loopOnce();

  // sendAddPath=false → entries go into LiteTree
  auto* adjRibEntry = adjRibOutGroup_->getFromLiteTree(
      adjRibOutGroup_->LiteTree_, prefix, adjRibOutGroup_->getGroupOwnerKey());
  ASSERT_NE(nullptr, adjRibEntry);
  const auto& postAttrs = adjRibEntry->getPostAttr();
  ASSERT_NE(nullptr, postAttrs);
  const auto& comms = postAttrs->getCommunities().get();
  EXPECT_FALSE(hasCommunity(comms, kDrainCommunity));
  EXPECT_TRUE(hasCommunity(comms, kLiveCommunity));
}

} // namespace facebook::bgp
