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

#define PeerManager_TEST_FRIENDS                                   \
  friend class UpdateGroupDynamicPolicyReEvaluationTest;           \
  FRIEND_TEST(                                                     \
      UpdateGroupDynamicPolicyReEvaluationTest,                    \
      GetPolicyReEvalPendingGroups_EmptyWhenNoPendingPeers);       \
  FRIEND_TEST(                                                     \
      UpdateGroupDynamicPolicyReEvaluationTest,                    \
      GetPolicyReEvalPendingGroups_CollectsPendingPeerGroups);     \
  FRIEND_TEST(                                                     \
      UpdateGroupDynamicPolicyReEvaluationTest,                    \
      GetPolicyReEvalPendingGroups_DeduplicatesSharedGroup);       \
  FRIEND_TEST(                                                     \
      UpdateGroupDynamicPolicyReEvaluationTest,                    \
      GetPolicyReEvalPendingGroups_SkipsInitialAnnouncementPeers); \
  FRIEND_TEST(                                                     \
      UpdateGroupDynamicPolicyReEvaluationTest,                    \
      ProcessGroupEgressPolicyReEvaluation_HandlesAllPeerStates);  \
  FRIEND_TEST(                                                     \
      UpdateGroupDynamicPolicyReEvaluationTest,                    \
      ProcessGroupEgressPolicyReEvaluation_UpdatesGroupKey);       \
  FRIEND_TEST(                                                     \
      UpdateGroupDynamicPolicyReEvaluationTest,                    \
      ProcessRibDumpReq_DetachedPeerAheadOfGroupAfterRibWalk);     \
  FRIEND_TEST(                                                     \
      UpdateGroupDynamicPolicyReEvaluationTest,                    \
      ReEvalAdvancesToMaxRibVersionWhenChangeListTailIsNotForGroup);

#define AdjRib_TEST_FRIENDS                                                \
  friend class UpdateGroupDynamicPolicyReEvaluationTest;                   \
  friend class SinglePeerPolicyReEvaluation;                               \
  FRIEND_TEST(                                                             \
      UpdateGroupDynamicPolicyReEvaluationTest,                            \
      ProcessRibDumpReq_DetachedPeerAheadOfGroupAfterRibWalk);             \
  FRIEND_TEST(                                                             \
      SinglePeerPolicyReEvaluation,                                        \
      DeferredPushDoesNotCorruptBitmapAfterMovePeer);                      \
  FRIEND_TEST(SinglePeerPolicyReEvaluation, MovePeerPathTreeMixedEntries); \
  FRIEND_TEST(                                                             \
      SinglePeerPolicyReEvaluation,                                        \
      MovePeerPathTreeMixedEntriesToNonEmptyGroup);                        \
  FRIEND_TEST(SinglePeerPolicyReEvaluation, MovePeerLiteTreeMixedEntries); \
  FRIEND_TEST(                                                             \
      SinglePeerPolicyReEvaluation,                                        \
      MovePeerLiteTreeMixedEntriesToNonEmptyGroup);

#define AdjRibOutGroup_TEST_FRIENDS                                        \
  friend class SinglePeerPolicyReEvaluation;                               \
  FRIEND_TEST(SinglePeerPolicyReEvaluation, MovePeerPathTreeMixedEntries); \
  FRIEND_TEST(                                                             \
      SinglePeerPolicyReEvaluation,                                        \
      MovePeerPathTreeMixedEntriesToNonEmptyGroup);                        \
  FRIEND_TEST(SinglePeerPolicyReEvaluation, MovePeerLiteTreeMixedEntries); \
  FRIEND_TEST(                                                             \
      SinglePeerPolicyReEvaluation,                                        \
      MovePeerLiteTreeMixedEntriesToNonEmptyGroup);

#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/changeTracker/ChangeTracker.h"
#include "neteng/fboss/bgp/cpp/changeTracker/TrackableObject.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/peer/SessionManager.h"
#include "neteng/fboss/bgp/cpp/tests/PeerManagerTestUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"

namespace facebook::bgp {

class UpdateGroupDynamicPolicyReEvaluationTest : public PeerManagerTestFixture {
 protected:
  struct TestContext {
    std::shared_ptr<PeerManagerBase> peerMgr;
    std::shared_ptr<SessionManager> sessionMgr;
    std::shared_ptr<AdjRib> adjRib1;
    std::shared_ptr<AdjRib> adjRib2;
    std::shared_ptr<AdjRib> adjRib3;
    std::thread peerMgrThread;
    std::thread sessionMgrThread;
  };

  std::shared_ptr<AdjRib> setupAdjRib(
      folly::EventBase& evb,
      std::shared_ptr<ChangeTracker<ShadowRibEntry>> /* changeListTracker */,
      const nettools::bgplib::BgpPeerId& peerId,
      const AsNum& remoteAs,
      std::shared_ptr<folly::coro::Baton>& sessionTerminateBaton,
      std::shared_ptr<const Config> config,
      const std::string& description,
      const std::string& peerGroupName) {
    auto adjRibOutGroup =
        std::make_shared<AdjRibOutGroup>(evb, "PeerManagerTest");
    auto adjRib = std::make_shared<AdjRib>(
        peerId,
        PeeringParams(
            fboss::kDefaultVrf,
            peerId.peerAddr,
            std::nullopt,
            kAsn1,
            kAsn1,
            remoteAs,
            kLocalAddr1.asV4(),
            kLocalAddr1.asV4(),
            std::chrono::seconds(kDefaultHoldTime),
            std::chrono::seconds(kGrRestartTime),
            nettools::bgplib::constants::kBgpPort,
            folly::AsyncSocket::anyAddress(),
            TBgpSessionConnectMode::PASSIVE_ACTIVE,
            kV4Nexthop1.asV4(),
            kV6Nexthop1.asV6(),
            RrClientConfigured(false),
            NextHopSelfConfigured{false},
            AfiIpv4Configured{true},
            AfiIpv6Configured{true},
            ConfedPeerConfigured{false},
            RemovePrivateAsConfigured{false},
            std::nullopt,
            std::nullopt,
            AdvertiseLinkBandwidth::DISABLE,
            ReceiveLinkBandwidth::ACCEPT,
            std::nullopt,
            ValidateRemoteAs{true},
            std::nullopt,
            std::nullopt,
            false,
            EnableStatefulHa{false},
            std::nullopt,
            V4OverV6Nexthop{false}),
        evb,
        ribInQ_,
        observerQ_,
        sessionTerminateBaton,
        nullptr,
        isSafeModeOn_,
        std::nullopt,
        std::nullopt,
        adjRibOutGroup,
        std::nullopt,
        std::make_shared<ConfigManager>(config));
    adjRib->peeringParams_.description = description;
    adjRib->peeringParams_.peerGroupName = peerGroupName;
    return adjRib;
  }

  TestContext setUp(bool enableUpdateGroup = true) {
    auto config = getConfig(
        true, // includeStaticPeer
        true, // includeDynamicShivPeer
        false, // includeDynamicMonitorPeer
        false, // includeDynamicVipInjectorPeer
        false, // enableStatefulHa
        true, // enableVipServer
        kDefaultEorTimeS, // eorTimeS
        false, // enableSubscriberLimit
        false, // enableSwitchLimit
        false, // applyGoldenPrefixPolicy
        {}, // bgpFeatures
        false, // enableDynamicPolicyEvaluation
        enableUpdateGroup);

    auto configManager = std::make_shared<ConfigManager>(config);
    auto peerMgr = std::make_shared<PeerManagerBase>(
        configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

    auto globalConfig = config->getBgpGlobalConfig();
    auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
    peerMgr->setSessionManager(sessionMgr);

    auto& evb = peerMgr->getEventBase();

    auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
    if (!vrf) {
      XLOGF(ERR, "Non-existent VRF {}", fboss::kDefaultVrf);
      return {};
    }
    auto adjRib1 = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId1,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config,
        "adjRib1",
        "PEERGROUP_RSW_FSW_V4");
    auto group1 = std::make_shared<AdjRibOutGroup>(evb, "Group1");
    adjRib1->adjRibOutGroup_ = group1;
    adjRib1->resetInInitialAnnouncement();
    group1->registerPeer(adjRib1);

    auto adjRib2 = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId2,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config,
        "adjRib2",
        "PEERGROUP_FSW_RSW_V4");
    auto group2 = std::make_shared<AdjRibOutGroup>(evb, "Group2");
    adjRib2->adjRibOutGroup_ = group2;
    adjRib2->resetInInitialAnnouncement();
    group2->registerPeer(adjRib2);

    auto adjRib3 = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId3,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config,
        "adjRib3",
        "PEERGROUP_RSW_FSW_V4");
    auto group3 = std::make_shared<AdjRibOutGroup>(evb, "Group3");
    adjRib3->adjRibOutGroup_ = group3;
    adjRib3->resetInInitialAnnouncement();
    group3->registerPeer(adjRib3);

    peerMgr->adjRibs_[kPeerId1] = adjRib1;
    peerMgr->adjRibs_[kPeerId2] = adjRib2;
    peerMgr->adjRibs_[kPeerId3] = adjRib3;

    auto peerMgrThread = peerMgr->runInThread();
    auto sessionMgrThread = sessionMgr->runInThread();

    return {
        peerMgr,
        sessionMgr,
        adjRib1,
        adjRib2,
        adjRib3,
        std::move(peerMgrThread),
        std::move(sessionMgrThread)};
  }

  void tearDown(TestContext& ctx) {
    ctx.peerMgr->stop();
    ctx.sessionMgr->stop();
    ctx.peerMgrThread.join();
    ctx.sessionMgrThread.join();
  }
};

TEST_F(
    UpdateGroupDynamicPolicyReEvaluationTest,
    GetPolicyReEvalPendingGroups_EmptyWhenNoPendingPeers) {
  auto ctx = setUp(true /* enableUpdateGroup */);
  auto& evb = ctx.peerMgr->getEventBase();

  evb.runInEventBaseThreadAndWait([&]() {
    auto groups = ctx.peerMgr->getPolicyReEvalPendingGroups();
    EXPECT_EQ(groups.size(), 0);
  });

  tearDown(ctx);
}

TEST_F(
    UpdateGroupDynamicPolicyReEvaluationTest,
    GetPolicyReEvalPendingGroups_CollectsPendingPeerGroups) {
  auto ctx = setUp(true /* enableUpdateGroup */);
  auto& evb = ctx.peerMgr->getEventBase();

  evb.runInEventBaseThreadAndWait([&]() {
    ctx.adjRib1->setPendingEgressPolicyUpdate(true);
    ctx.adjRib2->setPendingEgressPolicyUpdate(true);
    auto groups = ctx.peerMgr->getPolicyReEvalPendingGroups();
    EXPECT_EQ(groups.size(), 2);
  });

  tearDown(ctx);
}

TEST_F(
    UpdateGroupDynamicPolicyReEvaluationTest,
    GetPolicyReEvalPendingGroups_DeduplicatesSharedGroup) {
  auto ctx = setUp(true /* enableUpdateGroup */);
  auto& evb = ctx.peerMgr->getEventBase();

  // Move adjRib2 into adjRib1's group
  auto group2 = ctx.adjRib2->getUpdateGroup();
  group2->unregisterPeer(ctx.adjRib2);
  auto group1 = ctx.adjRib1->getUpdateGroup();
  group1->registerPeer(ctx.adjRib2);
  ctx.adjRib2->setUpdateGroup(group1);

  evb.runInEventBaseThreadAndWait([&]() {
    ctx.adjRib1->setPendingEgressPolicyUpdate(true);
    ctx.adjRib2->setPendingEgressPolicyUpdate(true);
    auto groups = ctx.peerMgr->getPolicyReEvalPendingGroups();
    EXPECT_EQ(groups.size(), 1);
    EXPECT_TRUE(groups.count(group1));
  });

  tearDown(ctx);
}

TEST_F(
    UpdateGroupDynamicPolicyReEvaluationTest,
    GetPolicyReEvalPendingGroups_SkipsInitialAnnouncementPeers) {
  auto ctx = setUp(true /* enableUpdateGroup */);
  auto& evb = ctx.peerMgr->getEventBase();

  ctx.adjRib1->setInInitialAnnouncement();

  evb.runInEventBaseThreadAndWait([&]() {
    ctx.adjRib1->setPendingEgressPolicyUpdate(
        true); // no-op: inInitialAnnouncement
    ctx.adjRib2->setPendingEgressPolicyUpdate(true);
    EXPECT_FALSE(ctx.adjRib1->isEgressPolicyUpdateRequired());
    auto groups = ctx.peerMgr->getPolicyReEvalPendingGroups();
    EXPECT_EQ(groups.size(), 1);
    EXPECT_TRUE(groups.count(ctx.adjRib2->getUpdateGroup()));
  });

  tearDown(ctx);
}

/**
 * processGroupEgressPolicyReEvaluation handles three peer states:
 *   IN_SYNC: clears pendingEgressPolicyUpdate immediately
 *   DETACHED (no rib dump scheduled): re-walks the ShadowRib via
 *     processRibDumpReq, which clears pending after
 *   DETACHED + rib dump scheduled: cancels the scheduled dump, re-walks the
 *     ShadowRib inline via processRibDumpReq, which clears pending after
 */
TEST_F(
    UpdateGroupDynamicPolicyReEvaluationTest,
    ProcessGroupEgressPolicyReEvaluation_HandlesAllPeerStates) {
  auto ctx = setUp(true /* enableUpdateGroup */);
  auto& evb = ctx.peerMgr->getEventBase();

  // Register all three adjRibs in a single manager-tracked group
  std::shared_ptr<AdjRibOutGroup> group;
  UpdateGroupKey originalKey;
  evb.runInEventBaseThreadAndWait([&]() {
    originalKey = ctx.adjRib1->buildAndSetUpdateGroupKey();
    group = ctx.peerMgr->updateGroupManager_->findOrCreateGroup(originalKey);

    auto oldGroup1 = ctx.adjRib1->getUpdateGroup();
    oldGroup1->unregisterPeer(ctx.adjRib1);
    group->registerPeer(ctx.adjRib1);
    ctx.adjRib1->setUpdateGroup(group);
    ctx.adjRib1->resetInInitialAnnouncement();

    auto oldGroup2 = ctx.adjRib2->getUpdateGroup();
    oldGroup2->unregisterPeer(ctx.adjRib2);
    group->registerPeer(ctx.adjRib2);
    ctx.adjRib2->setUpdateGroup(group);
    ctx.adjRib2->resetInInitialAnnouncement();

    auto oldGroup3 = ctx.adjRib3->getUpdateGroup();
    oldGroup3->unregisterPeer(ctx.adjRib3);
    group->registerPeer(ctx.adjRib3);
    ctx.adjRib3->setUpdateGroup(group);
    ctx.adjRib3->resetInInitialAnnouncement();
  });

  /*
   * adjRib1: IN_SYNC (default)
   * adjRib2: DETACHED_RUNNING without a rib dump scheduled
   * adjRib3: DETACHED_INIT_DUMP with a rib dump scheduled
   */
  ctx.adjRib2->setPeerState(PeerUpdateState::DETACHED_RUNNING);
  ctx.adjRib3->setPeerState(PeerUpdateState::DETACHED_INIT_DUMP);
  /*
   * adjRib3 has a rib dump in flight: hold it via testOnlyDeferInitDump (the
   * scheduled dump self-reschedules instead of completing, keeping its
   * cancellation source in place) and schedule it so isRibDumpScheduled() stays
   * true when re-eval runs.
   */
  ctx.adjRib3->testOnlyDeferInitDump = true;
  evb.runInEventBaseThreadAndWait([&]() {
    ctx.peerMgr->getDefaultVrf().scheduleRibDumpForAdjRib(ctx.adjRib3);
  });

  // Change egress policy on all peers so re-evaluation sees the new policy
  evb.runInEventBaseThreadAndWait([&]() {
    folly::F14FastMap<bgp_policy::DIRECTION, std::optional<std::string>>
        newPolicy;
    newPolicy[bgp_policy::DIRECTION::OUT] = "new_group_policy";
    ctx.adjRib1->updateIngressEgressPolicyNames(newPolicy);
    ctx.adjRib2->updateIngressEgressPolicyNames(newPolicy);
    ctx.adjRib3->updateIngressEgressPolicyNames(newPolicy);

    ctx.adjRib1->setPendingEgressPolicyUpdate(true);
    ctx.adjRib2->setPendingEgressPolicyUpdate(true);
    ctx.adjRib3->setPendingEgressPolicyUpdate(true);
  });

  evb.runInEventBaseThreadAndWait(
      [&]() { ctx.peerMgr->processGroupEgressPolicyReEvaluation(group); });

  WITH_RETRIES({
    evb.runInEventBaseThreadAndWait([&]() {
      // adjRib1 (IN_SYNC): flag cleared immediately
      EXPECT_EVENTUALLY_FALSE(ctx.adjRib1->isEgressPolicyUpdateRequired());

      // adjRib2 (DETACHED_RUNNING): pending flag cleared after async rib dump
      EXPECT_EVENTUALLY_FALSE(ctx.adjRib2->isEgressPolicyUpdateRequired());

      /*
       * adjRib3 (DETACHED_INIT_DUMP + rib dump scheduled): scheduled dump
       * cancelled and re-walked inline, pending flag cleared
       */
      EXPECT_EVENTUALLY_FALSE(ctx.adjRib3->isEgressPolicyUpdateRequired());
    });
  });

  tearDown(ctx);
}

/**
 * Get a peer into DETACHED_READY_TO_JOIN via the DFP path, then verify
 * that processRibDumpReq reschedules the changeListConsumeTimer_ via
 * activateChangeListConsumer -> scheduleSendBgpUpdates -> sendBgpUpdates
 * -> reschedulePackingTimers.
 */
TEST_F(
    UpdateGroupDynamicPolicyReEvaluationTest,
    ProcessRibDumpReq_DetachedPeerAheadOfGroupAfterRibWalk) {
  auto ctx = setUp(true /* enableUpdateGroup */);
  auto& evb = ctx.peerMgr->getEventBase();

  /*
   * Equip adjRib1 with queues, backpressure, and AFI negotiation so the
   * full send pipeline (processRibMessage -> sendBgpUpdates) works.
   */
  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib = ctx.adjRib1;
    adjRib->adjRibOutQueue_ =
        std::make_shared<AdjRib::AdjRibOutQueueT>(10, 8, 2);
    adjRib->pathIdGenerator_ = std::make_unique<PathIdGenerator>(false);
    adjRib->isAfiIpv4Negotiated_ = true;
  });

  auto* vrf = ctx.peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  /*
   * Phase 1: Populate the ShadowRib with 5 prefixes via
   * handleShadowRibEntryAnnouncement so processRibDumpReq has entries to walk.
   */
  evb.runInEventBaseThreadAndWait([&]() {
    auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
    attrs->publish();
    /*
     * Use kPeerAddr2 as originator — canAnnounce() rejects routes from the
     * same peer (kPeerAddr1) that the AdjRib belongs to.
     */
    TinyPeerInfo peer(
        kPeerAddr2, kAsn1, kPeerRouterId2, BgpSessionType::EBGP, false);

    RibOutAnnouncement announcement;
    announcement.initialDump = true;
    for (int i = 1; i <= 5; ++i) {
      announcement.entries.emplace_back(
          folly::CIDRNetwork{folly::IPAddress(fmt::format("10.0.{}.0", i)), 24},
          kDefaultPathID,
          peer,
          attrs);
      announcement.entries.back().ribVersion = i;
    }
    vrf->handleShadowRibEntryAnnouncement(announcement);
  });

  /*
   * Phase 2: Register the group consumer on adjRib1's group.
   * Use PeerManagerBase's bitmaps so publishChange stamps consumer bits
   * correctly.
   */
  auto& addPathBitmap = vrf->getAddPathConsumerBitmap();
  auto& nonAddPathBitmap = vrf->getNonAddPathConsumerBitmap();
  std::shared_ptr<AdjRibOutGroupConsumer> groupConsumer;
  evb.runInEventBaseThreadAndWait([&]() {
    auto group = ctx.adjRib1->getUpdateGroup();
    group->setChangeListTracker(
        vrf->getChangeListTracker(), addPathBitmap, nonAddPathBitmap);
    group->registerGroupConsumer();
    groupConsumer = group->getChangeListConsumer();
  });

  /*
   * Phase 3: Publish one more prefix to ShadowRib. This item lands on
   * the changelist after the group consumer is registered, so the
   * per-peer consumer will have unconsumed items (isReady() == false).
   */
  evb.runInEventBaseThreadAndWait([&]() {
    auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
    attrs->publish();
    TinyPeerInfo peer(
        kPeerAddr2, kAsn1, kPeerRouterId2, BgpSessionType::EBGP, false);

    RibOutAnnouncement announcement;
    for (int i = 0; i < 1; ++i) {
      announcement.entries.emplace_back(
          folly::CIDRNetwork{folly::IPAddress("10.1.0.0"), 24},
          kDefaultPathID,
          peer,
          attrs);
    }
    vrf->handleShadowRibEntryAnnouncement(announcement);
  });

  /*
   * Phase 4: Get the peer into DETACHED_READY_TO_JOIN via DFP path.
   * Follows the ActivateDFPTransitionsViaIsDFPPath pattern.
   */
  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib = ctx.adjRib1;
    adjRib->setPeerState(PeerUpdateState::DETACHED_RUNNING);

    /*
     * Clear the dummy consumer set by PeerManagerTestFixture::setupAdjRib
     * so registerDetachedConsumer can create and register a proper one.
     */
    adjRib->changeListConsumer_.reset();

    /*
     * Register detached consumer at the group consumer's position.
     * joinConsumer ensures the per-peer consumer inherits the group's
     * position, so entries published before the group consumer (Phase 1)
     * will NOT have the per-peer bit set.
     */
    adjRib->registerDetachedConsumerAtGroupPosition(
        vrf->getChangeListTracker(),
        groupConsumer,
        addPathBitmap,
        nonAddPathBitmap);
    EXPECT_NE(adjRib->changeListConsumeTimer_, nullptr);

    /*
     * Set up DFP conditions:
     *   1. Peer PL empty (default)
     *   2. Matching lastSeenRibVersion between group and peer (both 0)
     *   3. Group PL non-empty
     */
    auto group = adjRib->getUpdateGroup();
    auto attrs = std::make_shared<BgpPath>(BgpPathFields());
    attrs->setLocalPref(100);
    std::shared_ptr<const BgpPath> constAttrs = attrs;
    group->tryUpdateAttrToPrefixMapForGroup(
        std::make_pair(
            folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24},
            kPlaceholderPathID),
        nullptr,
        constAttrs);

    EXPECT_TRUE(adjRib->isDFP());

    // Activate detached mode — schedules sendBgpUpdates on the evb
    adjRib->activateDetachedModeProcessing();
  });

  /*
   * Phase 5: Wait for sendBgpUpdates to run. isDFP() triggers transition to
   * DETACHED_READY_TO_JOIN with cancelled packing timers.
   *
   * activateDetachedModeProcessing only enqueues sendBgpUpdates on the async
   * scope, and that coroutine suspends (co_safe_point, waitForQueueSpace,
   * sendPendingEoRs) before reaching transitionPeerUpdateState at the end. A
   * single evb turn is therefore not enough to observe the transition -- retry
   * until it lands.
   */
  WITH_RETRIES({
    evb.runInEventBaseThreadAndWait([&]() {
      auto& adjRib = ctx.adjRib1;
      EXPECT_EVENTUALLY_EQ(
          adjRib->getPeerState(), PeerUpdateState::DETACHED_READY_TO_JOIN);
      EXPECT_EVENTUALLY_TRUE(
          adjRib->isAdjRibFlagSet(AdjRib::IS_DETACHED_FAST_PEER));
      EXPECT_EVENTUALLY_FALSE(adjRib->changeListConsumeTimer_->isScheduled());
    });
  });

  /*
   * Phase 6: Call processRibDumpReq — walks the ShadowRib, calls
   * processRibMessage for each entry, then activateChangeListConsumer
   * triggers scheduleSendBgpUpdates -> sendBgpUpdates ->
   * reschedulePackingTimers.
   */
  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib = ctx.adjRib1;
    folly::coro::blockingWait(ctx.peerMgr->processRibDumpReqCoro(
        RibDumpReq(adjRib->getRemotePeerId(), false /* sendAddPath */)));

    /*
     * Verify 5 entries from the RibDump were received into the PL.
     * The 6th prefix (10.1.0.0/24) is on the changelist for this consumer
     * and is skipped by processRibDumpReq.
     */
    size_t totalPrefixes = 0;
    for (const auto& [key, prefixes] : adjRib->attrToPrefixMap_) {
      totalPrefixes += prefixes.size();
    }
    EXPECT_EQ(totalPrefixes, 5);
  });

  /*
   * Phase 7: EventBase processes sendBgpUpdates which drains the PL.
   * Peer's lastSeenRibVersion (from RIB dump) > group's (0), so the
   * ahead-of-group branch fires and cancels packing timers.
   */
  evb.runInEventBaseThreadAndWait([&]() {
    EXPECT_FALSE(ctx.adjRib1->changeListConsumeTimer_->isScheduled());
  });

  // Clean up before teardown
  evb.runInEventBaseThreadAndWait([&]() {
    ctx.adjRib1->deactivateDetachedModeProcessing();
    groupConsumer->resetBitmap();
    groupConsumer->terminate();
    groupConsumer->deregisterFromTracker();
    ctx.adjRib1->getUpdateGroup()->resetChangeListConsumer();
    /*
     * Clear shadowRibEntries_ while changeListTracker_ is still alive,
     * so ChangeItems properly unlink from the change list.
     */
    vrf->getShadowRibEntries().clear();
  });
  // Drain any async work triggered by cleanup
  evb.runInEventBaseThreadAndWait([]() {});

  tearDown(ctx);
}

/*
 * A policy re-evaluation must leave the group at the RIB's max version even
 * when the tail of the change list is not for this group's consumer.
 *
 * The walk skips entries already pending on the group's consumer and therefore
 * declines to advance the version off the walk, delegating that to the drain
 * that follows. But the drain only ever sees items this consumer subscribes
 * to, so a trailing add-path-only change -- published with the add-path
 * bitmap, which a non-add-path group is not in -- carries maxRibVersion past
 * anything the group can observe item by item.
 */
TEST_F(
    UpdateGroupDynamicPolicyReEvaluationTest,
    ReEvalAdvancesToMaxRibVersionWhenChangeListTailIsNotForGroup) {
  /*
   * maxRibVersion_ counts the contiguous prefix chunks PeerManager applies, so
   * each of the two single-prefix announcements below moves it by one: the
   * bestpath change lands on 1 and the add-path-only change on 2. The RIB's own
   * RibVersion on the entries is overwritten during the walk, so seeding it
   * here would prove nothing.
   */
  constexpr uint64_t kVersionAfterAddPathOnly = 2;
  const auto kGroupPrefix =
      folly::CIDRNetwork{folly::IPAddress("10.0.1.0"), 24};
  const auto kAddPathOnlyPrefix =
      folly::CIDRNetwork{folly::IPAddress("10.0.2.0"), 24};

  auto ctx = setUp(true /* enableUpdateGroup */);
  auto& evb = ctx.peerMgr->getEventBase();

  auto* vrf = ctx.peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  auto announce = [&](bool addPathOnly, const folly::CIDRNetwork& prefix) {
    auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
    attrs->publish();
    TinyPeerInfo peer(
        kPeerAddr2, kAsn1, kPeerRouterId2, BgpSessionType::EBGP, false);

    RibOutAnnouncement announcement;
    auto& entries =
        addPathOnly ? announcement.addPathEntries : announcement.entries;
    entries.emplace_back(prefix, addPathOnly ? 1 : kDefaultPathID, peer, attrs);
    vrf->handleShadowRibEntryAnnouncement(announcement);
  };

  /*
   * The group is bound to PeerManager's shadow RIB so getShadowRibMaxVersion()
   * tracks it, and is non-add-path so its consumer bit lands only in
   * nonAddPathConsumerBitmap_. No peers are registered: this covers the
   * group's own version bookkeeping, which does not depend on membership.
   */
  UpdateGroupKey groupKey;
  groupKey.sendAddPath = false;
  std::shared_ptr<AdjRibOutGroup> group;
  std::shared_ptr<AdjRibOutGroupConsumer> groupConsumer;
  evb.runInEventBaseThreadAndWait([&]() {
    group = std::make_shared<AdjRibOutGroup>(
        evb,
        "ReEvalMaxRibVersion",
        1 /* groupId */,
        true /* enableUpdateGroup */,
        groupKey,
        vrf->getShadowRibView());
    group->setChangeListTracker(
        vrf->getChangeListTracker(),
        vrf->getAddPathConsumerBitmap(),
        vrf->getNonAddPathConsumerBitmap());
    group->registerGroupConsumer();
    groupConsumer = group->getChangeListConsumer();
  });

  /*
   * A bestpath change carries the non-add-path bitmap, so it is pending on the
   * group's consumer and the re-eval walk will skip it.
   */
  evb.runInEventBaseThreadAndWait([&]() { announce(false, kGroupPrefix); });

  /*
   * An add-path-only change carries the add-path bitmap only: it advances
   * maxRibVersion but never reaches this group's consumer.
   */
  evb.runInEventBaseThreadAndWait(
      [&]() { announce(true, kAddPathOnlyPrefix); });

  evb.runInEventBaseThreadAndWait([&]() {
    ASSERT_EQ(kVersionAfterAddPathOnly, vrf->getMaxRibVersion());
    ASSERT_EQ(0, group->getLastSeenRibVersion());

    group->reEvaluateSyncPeersEgressPolicy();

    /*
     * Not the bestpath chunk's version (1): the drain reached the tail of the
     * change list, so the group owns the RIB's max version rather than the
     * version of the last item it happened to be offered.
     */
    EXPECT_EQ(kVersionAfterAddPathOnly, group->getLastSeenRibVersion());
  });

  evb.runInEventBaseThreadAndWait([&]() {
    groupConsumer->resetBitmap();
    groupConsumer->terminate();
    groupConsumer->deregisterFromTracker();
    group->resetChangeListConsumeTimer();
    group->resetChangeListConsumer();
    groupConsumer.reset();
    group.reset();
    /*
     * Clear shadowRibEntries_ while changeListTracker_ is still alive, so
     * ChangeItems properly unlink from the change list.
     */
    vrf->getShadowRibEntries().clear();
  });
  evb.runInEventBaseThreadAndWait([]() {});

  tearDown(ctx);
}

class SinglePeerPolicyReEvaluation : public ::testing::Test {
 protected:
  void SetUp() override {
    folly::SingletonVault::singleton()->registrationComplete();
    evb_ = std::make_unique<folly::EventBase>();
    sourceGroup_ = std::make_shared<AdjRibOutGroup>(*evb_, "source_group");
    targetGroup_ = std::make_shared<AdjRibOutGroup>(*evb_, "target_group");

    auto peerId = nettools::bgplib::BgpPeerId(
        folly::IPAddress("10.0.0.1"),
        folly::IPAddressV4("255.0.0.1").toLongHBO());
    adjRib_ = std::make_shared<AdjRib>(
        peerId,
        PeeringParams(fboss::kDefaultVrf),
        *evb_,
        ribInQ_,
        observerQ_,
        std::make_shared<folly::coro::Baton>(),
        nullptr,
        std::make_shared<std::atomic<bool>>(false));
    adjRib_->adjRibOutGroup_ = sourceGroup_;
    sourceGroup_->registerPeer(adjRib_);
  }

  void TearDown() override {
    if (adjRib_->getGroupBitPosition() != static_cast<uint64_t>(-1)) {
      adjRib_->getUpdateGroup()->unregisterPeer(adjRib_);
    }
    adjRib_.reset();
    sourceGroup_.reset();
    targetGroup_.reset();
    evb_.reset();
  }

  std::shared_ptr<AdjRibOutConsumer> setupChangeListConsumer(
      const std::shared_ptr<AdjRib>& adjRib) {
    static ConsumerBitmap dummyAddPathBitmap;
    static ConsumerBitmap dummyNonAddPathBitmap;
    auto consumer = std::make_shared<AdjRibOutConsumer>(
        changeListTracker_,
        nullptr /* adjRib */,
        "Test ChangeList Consumer",
        *evb_,
        dummyAddPathBitmap,
        dummyNonAddPathBitmap);
    adjRib->setChangeListConsumer(consumer);
    return consumer;
  }

  std::unique_ptr<folly::EventBase> evb_;
  std::shared_ptr<AdjRibOutGroup> sourceGroup_;
  std::shared_ptr<AdjRibOutGroup> targetGroup_;
  std::shared_ptr<AdjRib> adjRib_;
  std::shared_ptr<ChangeTracker<ShadowRibEntry>> changeListTracker_ =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("Test ChangeTracker");
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ_{
      nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<AdjRib::ObservableMessageT> observerQ_;
};

/*
 * movePeerPathTreeEntries into an empty target group: the peer's per-peer
 * entries are moved (and erased from the source); shared group entries the peer
 * still sees (ribVersion <= detachedRibVersion) are copied (and kept in the
 * source); group entries the peer never saw (ribVersion > detachedRibVersion)
 * are not copied. An empty target stores them under the group owner key.
 */
TEST_F(SinglePeerPolicyReEvaluation, MovePeerPathTreeMixedEntries) {
  /*
   * movePeerPathTreeEntries is used for add-path peers, so the source/target
   * groups must be add-path for copyEntryForOwner to write to the PathTree.
   */
  UpdateGroupKey addPathKey;
  addPathKey.sendAddPath = true;
  auto sourceGroup = std::make_shared<AdjRibOutGroup>(
      *evb_,
      "source_addpath",
      /*groupId=*/0,
      /*enableUpdateGroup=*/false,
      addPathKey);
  auto targetGroup = std::make_shared<AdjRibOutGroup>(
      *evb_,
      "target_addpath",
      /*groupId=*/1,
      /*enableUpdateGroup=*/false,
      addPathKey);

  adjRib_->setPeerState(PeerUpdateState::DETACHED_RUNNING);
  adjRib_->setDetachedRibVersion(10);

  folly::CIDRNetwork prefixPeer{folly::IPAddress("10.0.0.0"), 24};
  folly::CIDRNetwork prefixShared{folly::IPAddress("10.0.1.0"), 24};
  folly::CIDRNetwork prefixUnshared{folly::IPAddress("10.0.2.0"), 24};
  auto peerOwnerKey = adjRib_->getPeerOwnerKey();
  auto sourceGroupOwnerKey = sourceGroup->getGroupOwnerKey();

  // Diverged per-peer entry -> moved to target, erased from source.
  auto peerEntry = sourceGroup->addToPathTree(
      sourceGroup->PathTree_, prefixPeer, peerOwnerKey, /*pathId=*/0);
  ASSERT_NE(peerEntry, nullptr);

  // Shared group entry the peer still sees -> copied, kept in source.
  auto sharedEntry = sourceGroup->addToPathTree(
      sourceGroup->PathTree_, prefixShared, sourceGroupOwnerKey, /*pathId=*/0);
  sharedEntry->setRibVersion(5);

  // Group entry the peer never saw -> not copied.
  auto unsharedEntry = sourceGroup->addToPathTree(
      sourceGroup->PathTree_,
      prefixUnshared,
      sourceGroupOwnerKey,
      /*pathId=*/0);
  unsharedEntry->setRibVersion(20);

  ASSERT_EQ(targetGroup->PathTree_.begin(), targetGroup->PathTree_.end());

  // Moved entries land under the peer's own owner key.
  sourceGroup->movePeerMaterializedRibOutPathEntries({adjRib_}, targetGroup);

  // Source: per-peer entry gone, group entries untouched.
  EXPECT_EQ(
      sourceGroup->getFromPathTree(
          sourceGroup->PathTree_, prefixPeer, peerOwnerKey, /*pathId=*/0),
      nullptr);
  EXPECT_NE(
      sourceGroup->getFromPathTree(
          sourceGroup->PathTree_,
          prefixShared,
          sourceGroupOwnerKey,
          /*pathId=*/0),
      nullptr);
  EXPECT_NE(
      sourceGroup->getFromPathTree(
          sourceGroup->PathTree_,
          prefixUnshared,
          sourceGroupOwnerKey,
          /*pathId=*/0),
      nullptr);

  // Target: per-peer + shared copied, unshared not.
  EXPECT_NE(
      targetGroup->getFromPathTree(
          targetGroup->PathTree_, prefixPeer, peerOwnerKey, /*pathId=*/0),
      nullptr);
  EXPECT_NE(
      targetGroup->getFromPathTree(
          targetGroup->PathTree_,
          prefixShared,
          peerOwnerKey,
          /*pathId=*/0),
      nullptr);
  EXPECT_EQ(
      targetGroup->getFromPathTree(
          targetGroup->PathTree_,
          prefixUnshared,
          peerOwnerKey,
          /*pathId=*/0),
      nullptr);
}

/*
 * movePeerPathTreeEntries into a non-empty target group: the moved entries land
 * under the peer owner key (per-peer member) without disturbing the target's
 * existing group entries.
 */
TEST_F(
    SinglePeerPolicyReEvaluation,
    MovePeerPathTreeMixedEntriesToNonEmptyGroup) {
  UpdateGroupKey addPathKey;
  addPathKey.sendAddPath = true;
  auto sourceGroup = std::make_shared<AdjRibOutGroup>(
      *evb_,
      "source_addpath",
      /*groupId=*/0,
      /*enableUpdateGroup=*/false,
      addPathKey);
  auto targetGroup = std::make_shared<AdjRibOutGroup>(
      *evb_,
      "target_addpath",
      /*groupId=*/1,
      /*enableUpdateGroup=*/false,
      addPathKey);

  adjRib_->setPeerState(PeerUpdateState::DETACHED_RUNNING);
  adjRib_->setDetachedRibVersion(10);

  folly::CIDRNetwork prefixPeer{folly::IPAddress("10.0.0.0"), 24};
  folly::CIDRNetwork prefixShared{folly::IPAddress("10.0.1.0"), 24};
  folly::CIDRNetwork prefixExisting{folly::IPAddress("10.0.9.0"), 24};
  auto peerOwnerKey = adjRib_->getPeerOwnerKey();
  auto sourceGroupOwnerKey = sourceGroup->getGroupOwnerKey();
  auto targetGroupOwnerKey = targetGroup->getGroupOwnerKey();

  auto peerEntry = sourceGroup->addToPathTree(
      sourceGroup->PathTree_, prefixPeer, peerOwnerKey, /*pathId=*/0);
  ASSERT_NE(peerEntry, nullptr);
  auto sharedEntry = sourceGroup->addToPathTree(
      sourceGroup->PathTree_, prefixShared, sourceGroupOwnerKey, /*pathId=*/0);
  sharedEntry->setRibVersion(5);

  // Pre-existing entry in the target group keeps it non-empty.
  auto existingEntry = targetGroup->addToPathTree(
      targetGroup->PathTree_,
      prefixExisting,
      targetGroupOwnerKey,
      /*pathId=*/0);
  ASSERT_NE(existingEntry, nullptr);

  // Non-empty target -> peer joins as a per-peer member.
  sourceGroup->movePeerMaterializedRibOutPathEntries({adjRib_}, targetGroup);

  // Target's existing entry is untouched.
  EXPECT_EQ(
      targetGroup->getFromPathTree(
          targetGroup->PathTree_,
          prefixExisting,
          targetGroupOwnerKey,
          /*pathId=*/0),
      existingEntry);
  // Peer's entries added under the peer owner key.
  EXPECT_NE(
      targetGroup->getFromPathTree(
          targetGroup->PathTree_, prefixPeer, peerOwnerKey, /*pathId=*/0),
      nullptr);
  EXPECT_NE(
      targetGroup->getFromPathTree(
          targetGroup->PathTree_, prefixShared, peerOwnerKey, /*pathId=*/0),
      nullptr);
  // Source per-peer entry erased.
  EXPECT_EQ(
      sourceGroup->getFromPathTree(
          sourceGroup->PathTree_, prefixPeer, peerOwnerKey, /*pathId=*/0),
      nullptr);
}

/*
 * movePeerLiteTreeEntries into an empty target group: same semantics as the
 * PathTree case — per-peer entry moved (erased from source), shared group entry
 * the peer still sees copied (kept in source), and an entry the peer never saw
 * not copied. An empty target stores them under the group owner key.
 */
TEST_F(SinglePeerPolicyReEvaluation, MovePeerLiteTreeMixedEntries) {
  adjRib_->setPeerState(PeerUpdateState::DETACHED_RUNNING);
  adjRib_->setDetachedRibVersion(10);

  folly::CIDRNetwork prefixPeer{folly::IPAddress("10.0.0.0"), 24};
  folly::CIDRNetwork prefixShared{folly::IPAddress("10.0.1.0"), 24};
  folly::CIDRNetwork prefixUnshared{folly::IPAddress("10.0.2.0"), 24};
  auto peerOwnerKey = adjRib_->getPeerOwnerKey();
  auto sourceGroupOwnerKey = sourceGroup_->getGroupOwnerKey();

  // Diverged per-peer entry -> moved to target, erased from source.
  auto peerEntry = sourceGroup_->addToLiteTree(
      sourceGroup_->LiteTree_, prefixPeer, peerOwnerKey, /*pathId=*/0);
  ASSERT_NE(peerEntry, nullptr);

  // Shared group entry the peer still sees -> copied, kept in source.
  auto sharedEntry = sourceGroup_->addToLiteTree(
      sourceGroup_->LiteTree_, prefixShared, sourceGroupOwnerKey, /*pathId=*/0);
  sharedEntry->setRibVersion(5);

  // Group entry the peer never saw -> not copied.
  auto unsharedEntry = sourceGroup_->addToLiteTree(
      sourceGroup_->LiteTree_,
      prefixUnshared,
      sourceGroupOwnerKey,
      /*pathId=*/0);
  unsharedEntry->setRibVersion(20);

  ASSERT_EQ(targetGroup_->LiteTree_.begin(), targetGroup_->LiteTree_.end());

  // Moved entries land under the peer's own owner key.
  sourceGroup_->movePeerMaterializedRibOutLiteEntries({adjRib_}, targetGroup_);

  // Source: per-peer entry gone, group entries untouched.
  EXPECT_EQ(
      sourceGroup_->getFromLiteTree(
          sourceGroup_->LiteTree_, prefixPeer, peerOwnerKey),
      nullptr);
  EXPECT_NE(
      sourceGroup_->getFromLiteTree(
          sourceGroup_->LiteTree_, prefixShared, sourceGroupOwnerKey),
      nullptr);
  EXPECT_NE(
      sourceGroup_->getFromLiteTree(
          sourceGroup_->LiteTree_, prefixUnshared, sourceGroupOwnerKey),
      nullptr);

  // Target: per-peer + shared copied, unshared not.
  EXPECT_NE(
      targetGroup_->getFromLiteTree(
          targetGroup_->LiteTree_, prefixPeer, peerOwnerKey),
      nullptr);
  EXPECT_NE(
      targetGroup_->getFromLiteTree(
          targetGroup_->LiteTree_, prefixShared, peerOwnerKey),
      nullptr);
  EXPECT_EQ(
      targetGroup_->getFromLiteTree(
          targetGroup_->LiteTree_, prefixUnshared, peerOwnerKey),
      nullptr);
}

/*
 * movePeerLiteTreeEntries into a non-empty target group: the moved entries land
 * under the peer owner key without disturbing the target's existing group
 * entries.
 */
TEST_F(
    SinglePeerPolicyReEvaluation,
    MovePeerLiteTreeMixedEntriesToNonEmptyGroup) {
  adjRib_->setPeerState(PeerUpdateState::DETACHED_RUNNING);
  adjRib_->setDetachedRibVersion(10);

  folly::CIDRNetwork prefixPeer{folly::IPAddress("10.0.0.0"), 24};
  folly::CIDRNetwork prefixShared{folly::IPAddress("10.0.1.0"), 24};
  folly::CIDRNetwork prefixExisting{folly::IPAddress("10.0.9.0"), 24};
  auto peerOwnerKey = adjRib_->getPeerOwnerKey();
  auto sourceGroupOwnerKey = sourceGroup_->getGroupOwnerKey();
  auto targetGroupOwnerKey = targetGroup_->getGroupOwnerKey();

  auto peerEntry = sourceGroup_->addToLiteTree(
      sourceGroup_->LiteTree_, prefixPeer, peerOwnerKey, /*pathId=*/0);
  ASSERT_NE(peerEntry, nullptr);
  auto sharedEntry = sourceGroup_->addToLiteTree(
      sourceGroup_->LiteTree_, prefixShared, sourceGroupOwnerKey, /*pathId=*/0);
  sharedEntry->setRibVersion(5);

  // Pre-existing entry in the target group keeps it non-empty.
  auto existingEntry = targetGroup_->addToLiteTree(
      targetGroup_->LiteTree_,
      prefixExisting,
      targetGroupOwnerKey,
      /*pathId=*/0);
  ASSERT_NE(existingEntry, nullptr);

  // Non-empty target -> peer joins as a per-peer member.
  sourceGroup_->movePeerMaterializedRibOutLiteEntries({adjRib_}, targetGroup_);

  // Target's existing entry is untouched.
  EXPECT_EQ(
      targetGroup_->getFromLiteTree(
          targetGroup_->LiteTree_, prefixExisting, targetGroupOwnerKey),
      existingEntry);
  // Peer's entries added under the peer owner key.
  EXPECT_NE(
      targetGroup_->getFromLiteTree(
          targetGroup_->LiteTree_, prefixPeer, peerOwnerKey),
      nullptr);
  EXPECT_NE(
      targetGroup_->getFromLiteTree(
          targetGroup_->LiteTree_, prefixShared, peerOwnerKey),
      nullptr);
  // Source per-peer entry erased.
  EXPECT_EQ(
      sourceGroup_->getFromLiteTree(
          sourceGroup_->LiteTree_, prefixPeer, peerOwnerKey),
      nullptr);
}

TEST_F(SinglePeerPolicyReEvaluation, MovePeerTransfersPerPeerEntries) {
  adjRib_->setPeerState(PeerUpdateState::DETACHED_RUNNING);
  adjRib_->setDetachedRibVersion(10);

  /*
   * The moved peer keeps its change list consumer: it runs in detached mode
   * in the new group and still needs the consumer to drain the change list.
   */
  auto clConsumer = setupChangeListConsumer(adjRib_);

  folly::CIDRNetwork prefix1{folly::IPAddress("10.0.0.0"), 24};
  folly::CIDRNetwork prefix2{folly::IPAddress("10.0.1.0"), 24};
  auto peerOwnerKey = adjRib_->getPeerOwnerKey();

  auto entry1 = sourceGroup_->addToLiteTree(
      sourceGroup_->LiteTree_, prefix1, peerOwnerKey, 0);
  auto entry2 = sourceGroup_->addToLiteTree(
      sourceGroup_->LiteTree_, prefix2, peerOwnerKey, 0);
  ASSERT_NE(entry1, nullptr);
  ASSERT_NE(entry2, nullptr);

  // Target should start empty
  EXPECT_EQ(targetGroup_->LiteTree_.begin(), targetGroup_->LiteTree_.end());

  sourceGroup_->movePeers({adjRib_}, targetGroup_);

  // Per-peer entries should be deleted from source
  EXPECT_EQ(
      sourceGroup_->getFromLiteTree(
          sourceGroup_->LiteTree_, prefix1, peerOwnerKey),
      nullptr);
  EXPECT_EQ(
      sourceGroup_->getFromLiteTree(
          sourceGroup_->LiteTree_, prefix2, peerOwnerKey),
      nullptr);

  // Entries land in the target under the peer's own owner key.
  EXPECT_NE(
      targetGroup_->getFromLiteTree(
          targetGroup_->LiteTree_, prefix1, peerOwnerKey),
      nullptr);
  EXPECT_NE(
      targetGroup_->getFromLiteTree(
          targetGroup_->LiteTree_, prefix2, peerOwnerKey),
      nullptr);

  // detachedRibVersion should be reset after movePeer
  EXPECT_EQ(adjRib_->getDetachedRibVersion(), 0);

  // movePeer must not reset the change list consumer.
  EXPECT_EQ(adjRib_->getChangeListConsumer(), clConsumer);
}

TEST_F(SinglePeerPolicyReEvaluation, MovePeerCopiesSharedGroupEntries) {
  adjRib_->setPeerState(PeerUpdateState::DETACHED_RUNNING);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  auto groupOwnerKey = sourceGroup_->getGroupOwnerKey();

  // Add a shared group entry with ribVersion the peer has seen
  adjRib_->setDetachedRibVersion(10);
  auto groupEntry = sourceGroup_->addToLiteTree(
      sourceGroup_->LiteTree_, prefix, groupOwnerKey, 0);
  ASSERT_NE(groupEntry, nullptr);
  groupEntry->setRibVersion(5); // < detachedRibVersion, so peer shares it

  // Target should start empty
  EXPECT_EQ(targetGroup_->LiteTree_.begin(), targetGroup_->LiteTree_.end());

  sourceGroup_->movePeers({adjRib_}, targetGroup_);

  // Shared entry should remain in source (not moved, only copied)
  EXPECT_NE(
      sourceGroup_->getFromLiteTree(
          sourceGroup_->LiteTree_, prefix, groupOwnerKey),
      nullptr);

  // A copy lands in the target under the peer's own owner key.
  auto peerOwnerKey = adjRib_->getPeerOwnerKey();
  EXPECT_NE(
      targetGroup_->getFromLiteTree(
          targetGroup_->LiteTree_, prefix, peerOwnerKey),
      nullptr);

  EXPECT_EQ(adjRib_->getDetachedRibVersion(), 0);
}

TEST_F(SinglePeerPolicyReEvaluation, MovePeerSkipsUnseenGroupEntries) {
  adjRib_->setPeerState(PeerUpdateState::DETACHED_RUNNING);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  auto groupOwnerKey = sourceGroup_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib_->getPeerOwnerKey();

  // Add a group entry with ribVersion the peer has NOT seen
  adjRib_->setDetachedRibVersion(5);
  auto groupEntry = sourceGroup_->addToLiteTree(
      sourceGroup_->LiteTree_, prefix, groupOwnerKey, 0);
  ASSERT_NE(groupEntry, nullptr);
  groupEntry->setRibVersion(10); // > detachedRibVersion, peer never saw it

  // Target should start empty
  EXPECT_EQ(targetGroup_->LiteTree_.begin(), targetGroup_->LiteTree_.end());

  sourceGroup_->movePeers({adjRib_}, targetGroup_);

  // Unseen entry should remain in source
  EXPECT_NE(
      sourceGroup_->getFromLiteTree(
          sourceGroup_->LiteTree_, prefix, groupOwnerKey),
      nullptr);

  // Nothing should be in target for this peer
  EXPECT_EQ(
      targetGroup_->getFromLiteTree(
          targetGroup_->LiteTree_, prefix, peerOwnerKey),
      nullptr);

  EXPECT_EQ(adjRib_->getDetachedRibVersion(), 0);
}

TEST_F(SinglePeerPolicyReEvaluation, MovePeerPrefersPerPeerOverShared) {
  adjRib_->setPeerState(PeerUpdateState::DETACHED_RUNNING);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  auto groupOwnerKey = sourceGroup_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib_->getPeerOwnerKey();

  adjRib_->setDetachedRibVersion(10);

  // Add both a shared group entry and a per-peer entry for the same prefix
  auto groupEntry = sourceGroup_->addToLiteTree(
      sourceGroup_->LiteTree_, prefix, groupOwnerKey, 0);
  groupEntry->setRibVersion(5);

  auto peerEntry = sourceGroup_->addToLiteTree(
      sourceGroup_->LiteTree_, prefix, peerOwnerKey, 0);
  ASSERT_NE(peerEntry, nullptr);

  // Target should start empty
  EXPECT_EQ(targetGroup_->LiteTree_.begin(), targetGroup_->LiteTree_.end());

  sourceGroup_->movePeers({adjRib_}, targetGroup_);

  // Per-peer entry should be deleted from source
  EXPECT_EQ(
      sourceGroup_->getFromLiteTree(
          sourceGroup_->LiteTree_, prefix, peerOwnerKey),
      nullptr);

  // Shared entry should remain in source
  EXPECT_NE(
      sourceGroup_->getFromLiteTree(
          sourceGroup_->LiteTree_, prefix, groupOwnerKey),
      nullptr);

  /*
   * Target should have one entry (from per-peer, not from shared), under the
   * peer's own owner key.
   */
  EXPECT_NE(
      targetGroup_->getFromLiteTree(
          targetGroup_->LiteTree_, prefix, peerOwnerKey),
      nullptr);

  EXPECT_EQ(adjRib_->getDetachedRibVersion(), 0);
}

TEST_F(SinglePeerPolicyReEvaluation, MovePeerCleansUpGroupState) {
  adjRib_->setPeerState(PeerUpdateState::DETACHED_RUNNING);
  adjRib_->setDetachedRibVersion(10);

  sourceGroup_->movePeers({adjRib_}, targetGroup_);

  // Peer should be fully removed from source group
  EXPECT_EQ(sourceGroup_->getMemberCount(), 0);

  // Peer should be registered in target group with a valid bit position
  EXPECT_EQ(targetGroup_->getMemberCount(), 1);
  EXPECT_NE(adjRib_->getGroupBitPosition(), static_cast<uint64_t>(-1));

  // detachedRibVersion should be reset after movePeer
  EXPECT_EQ(adjRib_->getDetachedRibVersion(), 0);
}

TEST_F(
    SinglePeerPolicyReEvaluation,
    MovePeerTransfersPerPeerEntriesToNonEmptyGroup) {
  /*
   * Register an existing peer in the target so bitToAdjRibs_ is non-empty
   * and copyEntryForPeer stores entries as peer-owned.
   */
  auto existingPeerId = nettools::bgplib::BgpPeerId(
      folly::IPAddress("10.0.0.2"),
      folly::IPAddressV4("255.0.0.2").toLongHBO());
  auto existingAdjRib = std::make_shared<AdjRib>(
      existingPeerId,
      PeeringParams(fboss::kDefaultVrf),
      *evb_,
      ribInQ_,
      observerQ_,
      std::make_shared<folly::coro::Baton>(),
      nullptr,
      std::make_shared<std::atomic<bool>>(false));
  targetGroup_->registerPeer(existingAdjRib);

  adjRib_->setPeerState(PeerUpdateState::DETACHED_RUNNING);
  adjRib_->setDetachedRibVersion(10);

  folly::CIDRNetwork prefix1{folly::IPAddress("10.0.0.0"), 24};
  folly::CIDRNetwork prefix2{folly::IPAddress("10.0.1.0"), 24};
  auto peerOwnerKey = adjRib_->getPeerOwnerKey();

  auto entry1 = sourceGroup_->addToLiteTree(
      sourceGroup_->LiteTree_, prefix1, peerOwnerKey, 0);
  auto entry2 = sourceGroup_->addToLiteTree(
      sourceGroup_->LiteTree_, prefix2, peerOwnerKey, 0);
  ASSERT_NE(entry1, nullptr);
  ASSERT_NE(entry2, nullptr);

  EXPECT_EQ(targetGroup_->LiteTree_.begin(), targetGroup_->LiteTree_.end());

  sourceGroup_->movePeers({adjRib_}, targetGroup_);

  // Per-peer entries should be deleted from source
  EXPECT_EQ(
      sourceGroup_->getFromLiteTree(
          sourceGroup_->LiteTree_, prefix1, peerOwnerKey),
      nullptr);
  EXPECT_EQ(
      sourceGroup_->getFromLiteTree(
          sourceGroup_->LiteTree_, prefix2, peerOwnerKey),
      nullptr);

  // Entries should exist in target under the peer's owner key
  EXPECT_NE(
      targetGroup_->getFromLiteTree(
          targetGroup_->LiteTree_, prefix1, peerOwnerKey),
      nullptr);
  EXPECT_NE(
      targetGroup_->getFromLiteTree(
          targetGroup_->LiteTree_, prefix2, peerOwnerKey),
      nullptr);

  EXPECT_EQ(adjRib_->getDetachedRibVersion(), 0);

  // Clean up the extra peer we registered in targetGroup_
  targetGroup_->unregisterPeer(existingAdjRib);
}

TEST_F(
    SinglePeerPolicyReEvaluation,
    MovePeerCopiesSharedGroupEntriesToNonEmptyGroup) {
  auto existingPeerId = nettools::bgplib::BgpPeerId(
      folly::IPAddress("10.0.0.2"),
      folly::IPAddressV4("255.0.0.2").toLongHBO());
  auto existingAdjRib = std::make_shared<AdjRib>(
      existingPeerId,
      PeeringParams(fboss::kDefaultVrf),
      *evb_,
      ribInQ_,
      observerQ_,
      std::make_shared<folly::coro::Baton>(),
      nullptr,
      std::make_shared<std::atomic<bool>>(false));
  targetGroup_->registerPeer(existingAdjRib);

  adjRib_->setPeerState(PeerUpdateState::DETACHED_RUNNING);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  auto groupOwnerKey = sourceGroup_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib_->getPeerOwnerKey();

  adjRib_->setDetachedRibVersion(10);
  auto groupEntry = sourceGroup_->addToLiteTree(
      sourceGroup_->LiteTree_, prefix, groupOwnerKey, 0);
  ASSERT_NE(groupEntry, nullptr);
  groupEntry->setRibVersion(5);

  EXPECT_EQ(targetGroup_->LiteTree_.begin(), targetGroup_->LiteTree_.end());

  sourceGroup_->movePeers({adjRib_}, targetGroup_);

  // Shared entry should remain in source
  EXPECT_NE(
      sourceGroup_->getFromLiteTree(
          sourceGroup_->LiteTree_, prefix, groupOwnerKey),
      nullptr);

  // Copy should exist in target under the peer's owner key
  EXPECT_NE(
      targetGroup_->getFromLiteTree(
          targetGroup_->LiteTree_, prefix, peerOwnerKey),
      nullptr);

  EXPECT_EQ(adjRib_->getDetachedRibVersion(), 0);

  // Clean up the extra peer we registered in targetGroup_
  targetGroup_->unregisterPeer(existingAdjRib);
}

TEST_F(
    SinglePeerPolicyReEvaluation,
    MovePeerSkipsUnseenGroupEntriesToNonEmptyGroup) {
  auto existingPeerId = nettools::bgplib::BgpPeerId(
      folly::IPAddress("10.0.0.2"),
      folly::IPAddressV4("255.0.0.2").toLongHBO());
  auto existingAdjRib = std::make_shared<AdjRib>(
      existingPeerId,
      PeeringParams(fboss::kDefaultVrf),
      *evb_,
      ribInQ_,
      observerQ_,
      std::make_shared<folly::coro::Baton>(),
      nullptr,
      std::make_shared<std::atomic<bool>>(false));
  targetGroup_->registerPeer(existingAdjRib);

  adjRib_->setPeerState(PeerUpdateState::DETACHED_RUNNING);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  auto groupOwnerKey = sourceGroup_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib_->getPeerOwnerKey();

  adjRib_->setDetachedRibVersion(5);
  auto groupEntry = sourceGroup_->addToLiteTree(
      sourceGroup_->LiteTree_, prefix, groupOwnerKey, 0);
  ASSERT_NE(groupEntry, nullptr);
  groupEntry->setRibVersion(10);

  EXPECT_EQ(targetGroup_->LiteTree_.begin(), targetGroup_->LiteTree_.end());

  sourceGroup_->movePeers({adjRib_}, targetGroup_);

  // Unseen entry should remain in source
  EXPECT_NE(
      sourceGroup_->getFromLiteTree(
          sourceGroup_->LiteTree_, prefix, groupOwnerKey),
      nullptr);

  // Nothing should be in target for this peer
  EXPECT_EQ(
      targetGroup_->getFromLiteTree(
          targetGroup_->LiteTree_, prefix, peerOwnerKey),
      nullptr);

  EXPECT_EQ(adjRib_->getDetachedRibVersion(), 0);

  // Clean up the extra peer we registered in targetGroup_
  targetGroup_->unregisterPeer(existingAdjRib);
}

TEST_F(
    SinglePeerPolicyReEvaluation,
    MovePeerPrefersPerPeerOverSharedToNonEmptyGroup) {
  auto existingPeerId = nettools::bgplib::BgpPeerId(
      folly::IPAddress("10.0.0.2"),
      folly::IPAddressV4("255.0.0.2").toLongHBO());
  auto existingAdjRib = std::make_shared<AdjRib>(
      existingPeerId,
      PeeringParams(fboss::kDefaultVrf),
      *evb_,
      ribInQ_,
      observerQ_,
      std::make_shared<folly::coro::Baton>(),
      nullptr,
      std::make_shared<std::atomic<bool>>(false));
  targetGroup_->registerPeer(existingAdjRib);

  adjRib_->setPeerState(PeerUpdateState::DETACHED_RUNNING);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  auto groupOwnerKey = sourceGroup_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib_->getPeerOwnerKey();

  adjRib_->setDetachedRibVersion(10);

  auto groupEntry = sourceGroup_->addToLiteTree(
      sourceGroup_->LiteTree_, prefix, groupOwnerKey, 0);
  groupEntry->setRibVersion(5);

  auto peerEntry = sourceGroup_->addToLiteTree(
      sourceGroup_->LiteTree_, prefix, peerOwnerKey, 0);
  ASSERT_NE(peerEntry, nullptr);

  EXPECT_EQ(targetGroup_->LiteTree_.begin(), targetGroup_->LiteTree_.end());

  sourceGroup_->movePeers({adjRib_}, targetGroup_);

  // Per-peer entry should be deleted from source
  EXPECT_EQ(
      sourceGroup_->getFromLiteTree(
          sourceGroup_->LiteTree_, prefix, peerOwnerKey),
      nullptr);

  // Shared entry should remain in source
  EXPECT_NE(
      sourceGroup_->getFromLiteTree(
          sourceGroup_->LiteTree_, prefix, groupOwnerKey),
      nullptr);

  // Target should have entry under the peer's owner key
  EXPECT_NE(
      targetGroup_->getFromLiteTree(
          targetGroup_->LiteTree_, prefix, peerOwnerKey),
      nullptr);

  EXPECT_EQ(adjRib_->getDetachedRibVersion(), 0);

  // Clean up the extra peer we registered in targetGroup_
  targetGroup_->unregisterPeer(existingAdjRib);
}

/*
 * Verify that deferredPushToPeer's RAII guard (markPeerUnblocked) on the old
 * group does not corrupt the blocked bitmap after the peer has moved to a new
 * group and a different peer has taken the same bit position.
 *
 * Flow:
 *   1. Register peer with a small bounded queue, fill it to trigger blocking
 *   2. tryPushToPeer launches deferredPushToPeer coroutine, peer becomes
 *      JOINED_BLOCKED
 *   3. While coroutine is suspended: detach peer, move to targetGroup,
 *      register a new peer at the same bit, block the new peer
 *   4. Drain the queue so the coroutine can complete
 *   5. RAII guard calls markPeerUnblocked on sourceGroup — isPeerInGroup
 *      prevents it from clearing the new peer's blocked bit
 */
CO_TEST_F(
    SinglePeerPolicyReEvaluation,
    DeferredPushDoesNotCorruptBitmapAfterMovePeer) {
  // deferredPushToPeer runs on evb_ via asyncScope_, so we need it looping.
  std::thread evbThread([this]() { evb_->loopForever(); });

  auto boundedQueue = std::make_shared<AdjRib::AdjRibOutQueueT>(3, 2, 1);
  uint64_t originalBit = 0;
  std::shared_ptr<AdjRib> adjRib2;

  // Phase 1: Set up bounded queue, fill it, trigger blocking via tryPushToPeer
  evb_->runInEventBaseThreadAndWait([&]() {
    adjRib_->adjRibOutQueue_ = boundedQueue;
    adjRib_->setPeerState(PeerUpdateState::JOINED_RUNNING);

    // Fill queue to high watermark to trigger blocking
    nettools::bgplib::FiberBgpPeer::InputMessageT dummyMsg =
        nettools::bgplib::BgpEndOfRib();
    for (int i = 0; i < 2; ++i) {
      boundedQueue->push(
          std::optional<nettools::bgplib::FiberBgpPeer::InputMessageT>(
              dummyMsg));
    }
    EXPECT_TRUE(boundedQueue->isBlocked());

    /*
     * tryPushToPeer sees blocked queue, marks peer blocked, launches
     * deferredPushToPeer coroutine on asyncScope_
     */
    originalBit = adjRib_->getGroupBitPosition();
    auto result = sourceGroup_->tryPushToPeer(dummyMsg, adjRib_, originalBit);
    EXPECT_EQ(result, AdjRibOutGroup::PushResult::PUSH_PENDING);
    EXPECT_EQ(adjRib_->getPeerState(), PeerUpdateState::JOINED_BLOCKED);
    EXPECT_TRUE(
        BitmapUtils::isBitSet(sourceGroup_->getBlockedBitmap(), originalBit));
  });

  // Phase 2: Detach, move peer, register new peer at same bit, block new peer
  evb_->runInEventBaseThreadAndWait([&]() {
    sourceGroup_->detachPeer(adjRib_, AdjRibOutGroup::DetachReason::Blocking);

    sourceGroup_->movePeers({adjRib_}, targetGroup_);

    // Register a new peer in sourceGroup — it should get the same bit
    auto peerId2 = nettools::bgplib::BgpPeerId(
        folly::IPAddress("10.0.0.2"),
        folly::IPAddressV4("255.0.0.2").toLongHBO());
    adjRib2 = std::make_shared<AdjRib>(
        peerId2,
        PeeringParams(fboss::kDefaultVrf),
        *evb_,
        ribInQ_,
        observerQ_,
        std::make_shared<folly::coro::Baton>(),
        nullptr,
        std::make_shared<std::atomic<bool>>(false));
    adjRib2->setUpdateGroup(sourceGroup_);
    sourceGroup_->registerPeer(adjRib2);
    EXPECT_EQ(adjRib2->getGroupBitPosition(), originalBit);

    // Block the new peer so its bit is set
    adjRib2->setPeerState(PeerUpdateState::JOINED_RUNNING);
    sourceGroup_->markPeerBlocked(adjRib2);
    EXPECT_TRUE(
        BitmapUtils::isBitSet(sourceGroup_->getBlockedBitmap(), originalBit));
    EXPECT_EQ(adjRib2->getPeerState(), PeerUpdateState::JOINED_BLOCKED);
  });

  /*
   * Phase 3: Pop all messages including the deferred push, then verify state.
   * Queue has 2 initial messages + 1 from deferredPushToPeer.
   * Popping unblocks waitToPush(), which lets deferredPushToPeer push
   * its message and run its RAII guard (markPeerUnblocked).
   */
  co_await co_withExecutor(evb_.get(), [&]() -> folly::coro::Task<void> {
    co_await boundedQueue->pop(); // initial msg 1
    co_await boundedQueue->pop(); // initial msg 2
    // Verify deferredPushToPeer pushed its update
    auto deferredMsg = co_await boundedQueue->pop();
    EXPECT_TRUE(deferredMsg.has_value());
    EXPECT_TRUE(
        std::holds_alternative<nettools::bgplib::BgpEndOfRib>(*deferredMsg));
    // Yield to let deferredPushToPeer's RAII guard (markPeerUnblocked) run
    co_await folly::coro::co_reschedule_on_current_executor;
    /*
     * The moved peer was registered as JOINED_BLOCKED but the deferred push
     * coroutine's RAII guard called markPeerUnblocked, transitioning to
     * JOINED_RUNNING.
     */
    EXPECT_EQ(adjRib_->getPeerState(), PeerUpdateState::DETACHED_RUNNING);
    /*
     * The new peer's blocked bit must still be set — isPeerInGroup prevented
     * the moved peer's markPeerUnblocked from clearing it.
     */
    EXPECT_TRUE(
        BitmapUtils::isBitSet(sourceGroup_->getBlockedBitmap(), originalBit));
    // The new peer should still be JOINED_BLOCKED
    EXPECT_EQ(adjRib2->getPeerState(), PeerUpdateState::JOINED_BLOCKED);
  }());

  // Clean up
  evb_->runInEventBaseThreadAndWait(
      [&]() { sourceGroup_->unregisterPeer(adjRib2); });

  evb_->terminateLoopSoon();
  evbThread.join();
}

/*
 * movePeers with three peers (LiteTree), each with a different relationship to
 * the group's shared RIB-OUT:
 *   peer1 shares nothing (its own per-peer entry for every prefix),
 *   peer2 shares some (per-peer for the diverged half, shares the stable half),
 *   peer3 shares everything (no per-peer entries).
 * Verifies the group's own RIB-OUT is left untouched and each peer lands in the
 * target under its own owner key.
 */
TEST_F(SinglePeerPolicyReEvaluation, MovePeersMultiPeerLiteTree) {
  auto peer1 = adjRib_; // registered in sourceGroup_ by SetUp
  auto makePeer = [&](const std::string& ip) {
    auto pid = nettools::bgplib::BgpPeerId(
        folly::IPAddress(ip), folly::IPAddressV4("255.0.0.1").toLongHBO());
    auto p = std::make_shared<AdjRib>(
        pid,
        PeeringParams(fboss::kDefaultVrf),
        *evb_,
        ribInQ_,
        observerQ_,
        std::make_shared<folly::coro::Baton>(),
        nullptr,
        std::make_shared<std::atomic<bool>>(false));
    p->setUpdateGroup(sourceGroup_);
    sourceGroup_->registerPeer(p);
    return p;
  };
  auto peer2 = makePeer("10.0.0.2");
  auto peer3 = makePeer("10.0.0.3");

  for (const auto& p : {peer1, peer2, peer3}) {
    p->setPeerState(PeerUpdateState::DETACHED_RUNNING);
  }
  // peer1 shares nothing, peer2 shares the stable half, peer3 shares all.
  peer1->setDetachedRibVersion(4);
  peer2->setDetachedRibVersion(10);
  peer3->setDetachedRibVersion(25);

  auto groupOwnerKey = sourceGroup_->getGroupOwnerKey();
  auto key1 = peer1->getPeerOwnerKey();
  auto key2 = peer2->getPeerOwnerKey();
  auto key3 = peer3->getPeerOwnerKey();

  constexpr int kNumPrefixes = 10;
  std::vector<folly::CIDRNetwork> prefixes;
  for (int i = 0; i < kNumPrefixes; i++) {
    folly::CIDRNetwork prefix{
        folly::IPAddress("10.0." + std::to_string(i) + ".0"), 24};
    prefixes.push_back(prefix);
    // First half "diverged" (high version), second half "stable" (low version).
    sourceGroup_
        ->addToLiteTree(sourceGroup_->LiteTree_, prefix, groupOwnerKey, 0)
        ->setRibVersion(i < kNumPrefixes / 2 ? 20 : 5);
  }

  // peer1: per-peer entry for every prefix (fully diverged).
  for (const auto& prefix : prefixes) {
    sourceGroup_->addToLiteTree(sourceGroup_->LiteTree_, prefix, key1, 0);
  }
  // peer2: per-peer entry for the diverged half only; shares the stable half.
  for (int i = 0; i < kNumPrefixes / 2; i++) {
    sourceGroup_->addToLiteTree(sourceGroup_->LiteTree_, prefixes[i], key2, 0);
  }
  // peer3: no per-peer entries; shares everything.

  sourceGroup_->movePeers({peer1, peer2, peer3}, targetGroup_);

  for (const auto& prefix : prefixes) {
    // The group's own RIB-OUT is untouched.
    EXPECT_NE(
        sourceGroup_->getFromLiteTree(
            sourceGroup_->LiteTree_, prefix, groupOwnerKey),
        nullptr);
    // Per-peer entries are erased from the source.
    EXPECT_EQ(
        sourceGroup_->getFromLiteTree(sourceGroup_->LiteTree_, prefix, key1),
        nullptr);
    EXPECT_EQ(
        sourceGroup_->getFromLiteTree(sourceGroup_->LiteTree_, prefix, key2),
        nullptr);
    // Every peer lands in the target under its own owner key.
    EXPECT_NE(
        targetGroup_->getFromLiteTree(targetGroup_->LiteTree_, prefix, key1),
        nullptr);
    EXPECT_NE(
        targetGroup_->getFromLiteTree(targetGroup_->LiteTree_, prefix, key2),
        nullptr);
    EXPECT_NE(
        targetGroup_->getFromLiteTree(targetGroup_->LiteTree_, prefix, key3),
        nullptr);
  }

  // Break peer<->target-group cycles for the locally created peers.
  peer2->setUpdateGroup(nullptr);
  peer3->setUpdateGroup(nullptr);
}

/*
 * movePeers with three peers (PathTree / add-path): same sharing scenarios as
 * the LiteTree case. Uses a local add-path source/target group so entries live
 * in the PathTree.
 */
TEST_F(SinglePeerPolicyReEvaluation, MovePeersMultiPeerPathTree) {
  UpdateGroupKey addPathKey;
  addPathKey.sendAddPath = true;
  auto sourceGroup = std::make_shared<AdjRibOutGroup>(
      *evb_,
      "source_addpath",
      /*groupId=*/0,
      /*enableUpdateGroup=*/false,
      addPathKey);
  auto targetGroup = std::make_shared<AdjRibOutGroup>(
      *evb_,
      "target_addpath",
      /*groupId=*/1,
      /*enableUpdateGroup=*/false,
      addPathKey);

  auto makePeer = [&](const std::string& ip) {
    auto pid = nettools::bgplib::BgpPeerId(
        folly::IPAddress(ip), folly::IPAddressV4("255.0.0.1").toLongHBO());
    auto p = std::make_shared<AdjRib>(
        pid,
        PeeringParams(fboss::kDefaultVrf),
        *evb_,
        ribInQ_,
        observerQ_,
        std::make_shared<folly::coro::Baton>(),
        nullptr,
        std::make_shared<std::atomic<bool>>(false));
    p->setUpdateGroup(sourceGroup);
    sourceGroup->registerPeer(p);
    p->setPeerState(PeerUpdateState::DETACHED_RUNNING);
    return p;
  };
  auto peer1 = makePeer("10.0.0.1");
  auto peer2 = makePeer("10.0.0.2");
  auto peer3 = makePeer("10.0.0.3");
  peer1->setDetachedRibVersion(4);
  peer2->setDetachedRibVersion(10);
  peer3->setDetachedRibVersion(25);

  auto groupOwnerKey = sourceGroup->getGroupOwnerKey();
  auto key1 = peer1->getPeerOwnerKey();
  auto key2 = peer2->getPeerOwnerKey();
  auto key3 = peer3->getPeerOwnerKey();

  constexpr int kNumPrefixes = 10;
  std::vector<folly::CIDRNetwork> prefixes;
  for (int i = 0; i < kNumPrefixes; i++) {
    folly::CIDRNetwork prefix{
        folly::IPAddress("10.0." + std::to_string(i) + ".0"), 24};
    prefixes.push_back(prefix);
    sourceGroup->addToPathTree(sourceGroup->PathTree_, prefix, groupOwnerKey, 0)
        ->setRibVersion(i < kNumPrefixes / 2 ? 20 : 5);
  }

  for (const auto& prefix : prefixes) {
    sourceGroup->addToPathTree(sourceGroup->PathTree_, prefix, key1, 0);
  }
  for (int i = 0; i < kNumPrefixes / 2; i++) {
    sourceGroup->addToPathTree(sourceGroup->PathTree_, prefixes[i], key2, 0);
  }

  sourceGroup->movePeers({peer1, peer2, peer3}, targetGroup);

  for (const auto& prefix : prefixes) {
    // The group's own RIB-OUT is untouched.
    EXPECT_NE(
        sourceGroup->getFromPathTree(
            sourceGroup->PathTree_, prefix, groupOwnerKey, 0),
        nullptr);
    EXPECT_EQ(
        sourceGroup->getFromPathTree(sourceGroup->PathTree_, prefix, key1, 0),
        nullptr);
    EXPECT_EQ(
        sourceGroup->getFromPathTree(sourceGroup->PathTree_, prefix, key2, 0),
        nullptr);
    EXPECT_NE(
        targetGroup->getFromPathTree(targetGroup->PathTree_, prefix, key1, 0),
        nullptr);
    EXPECT_NE(
        targetGroup->getFromPathTree(targetGroup->PathTree_, prefix, key2, 0),
        nullptr);
    EXPECT_NE(
        targetGroup->getFromPathTree(targetGroup->PathTree_, prefix, key3, 0),
        nullptr);
  }

  // Break peer<->group cycles so the locally created groups/peers don't leak.
  peer1->setUpdateGroup(nullptr);
  peer2->setUpdateGroup(nullptr);
  peer3->setUpdateGroup(nullptr);
}

/*
 * movePeersSharedRibOutLiteEntries: split a subset of peers out of a group into
 * a new (copy) group. The group's shared RIB-OUT is COPIED to the new group
 * under the NEW group's group owner key (and kept in the old group), while each
 * moved peer's per-peer entries are MOVED (erased from the old group, added to
 * the new group under the peer's own owner key). Unlike the materialized
 * variant, group entries are copied unconditionally (no rib-version gating).
 *
 * Three moved peers cover the sharing spectrum:
 *   peer1 "all shared"  - no per-peer entries (relies entirely on group
 * entries), peer2 "some shared" - per-peer entries for the first half of
 * prefixes, peer3 "non shared"  - per-peer entries for every prefix. peer4
 * stays in the old group; its per-peer entries must be left untouched.
 */
TEST_F(SinglePeerPolicyReEvaluation, MovePeersSharedRibOutLiteTree) {
  auto sourceGroup =
      std::make_shared<AdjRibOutGroup>(*evb_, "source_group", /*groupId=*/0);
  auto targetGroup =
      std::make_shared<AdjRibOutGroup>(*evb_, "target_group", /*groupId=*/1);

  auto makePeer = [&](const std::string& ip) {
    auto pid = nettools::bgplib::BgpPeerId(
        folly::IPAddress(ip), folly::IPAddressV4("255.0.0.1").toLongHBO());
    auto p = std::make_shared<AdjRib>(
        pid,
        PeeringParams(fboss::kDefaultVrf),
        *evb_,
        ribInQ_,
        observerQ_,
        std::make_shared<folly::coro::Baton>(),
        nullptr,
        std::make_shared<std::atomic<bool>>(false));
    p->setUpdateGroup(sourceGroup);
    sourceGroup->registerPeer(p);
    return p;
  };
  auto peer1 = makePeer("10.0.0.1"); // all shared
  auto peer2 = makePeer("10.0.0.2"); // some shared
  auto peer3 = makePeer("10.0.0.3"); // non shared
  auto peer4 = makePeer("10.0.0.4"); // stays in the old group

  auto sourceGroupOwnerKey = sourceGroup->getGroupOwnerKey();
  auto targetGroupOwnerKey = targetGroup->getGroupOwnerKey();
  auto key1 = peer1->getPeerOwnerKey();
  auto key2 = peer2->getPeerOwnerKey();
  auto key3 = peer3->getPeerOwnerKey();
  auto key4 = peer4->getPeerOwnerKey();

  constexpr int kNumPrefixes = 6;
  std::vector<folly::CIDRNetwork> prefixes;
  for (int i = 0; i < kNumPrefixes; i++) {
    folly::CIDRNetwork prefix{
        folly::IPAddress("10.1." + std::to_string(i) + ".0"), 24};
    prefixes.push_back(prefix);
    // Group-owned (shared) entry for every prefix.
    sourceGroup->addToLiteTree(
        sourceGroup->LiteTree_, prefix, sourceGroupOwnerKey, 0);
  }
  // peer3 (non shared): per-peer entry for every prefix.
  for (const auto& prefix : prefixes) {
    sourceGroup->addToLiteTree(sourceGroup->LiteTree_, prefix, key3, 0);
  }
  // peer2 (some shared): per-peer entry for the first half only.
  for (int i = 0; i < kNumPrefixes / 2; i++) {
    sourceGroup->addToLiteTree(sourceGroup->LiteTree_, prefixes[i], key2, 0);
  }
  // peer4 (staying): per-peer entry for every prefix; must be left untouched.
  for (const auto& prefix : prefixes) {
    sourceGroup->addToLiteTree(sourceGroup->LiteTree_, prefix, key4, 0);
  }
  // peer1 (all shared): no per-peer entries.

  sourceGroup->movePeersSharedRibOutLiteEntries(
      {peer1, peer2, peer3}, targetGroup);

  for (int i = 0; i < kNumPrefixes; i++) {
    const auto& prefix = prefixes[i];

    /*
     * Old group: shared entry copied (kept), staying peer4 untouched, moved
     * peer3's per-peer entry erased.
     */
    EXPECT_NE(
        sourceGroup->getFromLiteTree(
            sourceGroup->LiteTree_, prefix, sourceGroupOwnerKey),
        nullptr);
    EXPECT_NE(
        sourceGroup->getFromLiteTree(sourceGroup->LiteTree_, prefix, key4),
        nullptr);
    EXPECT_EQ(
        sourceGroup->getFromLiteTree(sourceGroup->LiteTree_, prefix, key3),
        nullptr);

    /*
     * New group: shared entry copied under the NEW group's owner key (not the
     * old group's key, not a peer key). peer1 (all shared) has nothing of its
     * own and relies on the copied group entry.
     */
    EXPECT_NE(
        targetGroup->getFromLiteTree(
            targetGroup->LiteTree_, prefix, targetGroupOwnerKey),
        nullptr);
    EXPECT_EQ(
        targetGroup->getFromLiteTree(
            targetGroup->LiteTree_, prefix, sourceGroupOwnerKey),
        nullptr);
    EXPECT_EQ(
        targetGroup->getFromLiteTree(targetGroup->LiteTree_, prefix, key1),
        nullptr);
    // Moved peer3 (non shared) lands under its own owner key.
    EXPECT_NE(
        targetGroup->getFromLiteTree(targetGroup->LiteTree_, prefix, key3),
        nullptr);
    // Staying peer4's entries are not moved into the new group.
    EXPECT_EQ(
        targetGroup->getFromLiteTree(targetGroup->LiteTree_, prefix, key4),
        nullptr);

    /*
     * peer2 (some shared) only diverged on the first half: moved there, absent
     * (and shared via the group) on the second half.
     */
    if (i < kNumPrefixes / 2) {
      EXPECT_EQ(
          sourceGroup->getFromLiteTree(sourceGroup->LiteTree_, prefix, key2),
          nullptr);
      EXPECT_NE(
          targetGroup->getFromLiteTree(targetGroup->LiteTree_, prefix, key2),
          nullptr);
    }
  }

  // Break peer<->group cycles for the locally created peers.
  for (const auto& p : {peer1, peer2, peer3, peer4}) {
    p->setUpdateGroup(nullptr);
  }
}

/*
 * movePeersSharedRibOutPathEntries: PathTree / add-path analogue of
 * MovePeersSharedRibOutLiteTree. Same sharing scenarios and a fourth staying
 * peer; uses a local add-path source/target group so entries live in the
 * PathTree.
 */
TEST_F(SinglePeerPolicyReEvaluation, MovePeersSharedRibOutPathTree) {
  UpdateGroupKey addPathKey;
  addPathKey.sendAddPath = true;
  auto sourceGroup = std::make_shared<AdjRibOutGroup>(
      *evb_,
      "source_addpath",
      /*groupId=*/0,
      /*enableUpdateGroup=*/false,
      addPathKey);
  auto targetGroup = std::make_shared<AdjRibOutGroup>(
      *evb_,
      "target_addpath",
      /*groupId=*/1,
      /*enableUpdateGroup=*/false,
      addPathKey);

  auto makePeer = [&](const std::string& ip) {
    auto pid = nettools::bgplib::BgpPeerId(
        folly::IPAddress(ip), folly::IPAddressV4("255.0.0.1").toLongHBO());
    auto p = std::make_shared<AdjRib>(
        pid,
        PeeringParams(fboss::kDefaultVrf),
        *evb_,
        ribInQ_,
        observerQ_,
        std::make_shared<folly::coro::Baton>(),
        nullptr,
        std::make_shared<std::atomic<bool>>(false));
    p->setUpdateGroup(sourceGroup);
    sourceGroup->registerPeer(p);
    return p;
  };
  auto peer1 = makePeer("10.0.0.1"); // all shared
  auto peer2 = makePeer("10.0.0.2"); // some shared
  auto peer3 = makePeer("10.0.0.3"); // non shared
  auto peer4 = makePeer("10.0.0.4"); // stays in the old group

  auto sourceGroupOwnerKey = sourceGroup->getGroupOwnerKey();
  auto targetGroupOwnerKey = targetGroup->getGroupOwnerKey();
  auto key1 = peer1->getPeerOwnerKey();
  auto key2 = peer2->getPeerOwnerKey();
  auto key3 = peer3->getPeerOwnerKey();
  auto key4 = peer4->getPeerOwnerKey();

  constexpr int kNumPrefixes = 6;
  std::vector<folly::CIDRNetwork> prefixes;
  for (int i = 0; i < kNumPrefixes; i++) {
    folly::CIDRNetwork prefix{
        folly::IPAddress("10.1." + std::to_string(i) + ".0"), 24};
    prefixes.push_back(prefix);
    sourceGroup->addToPathTree(
        sourceGroup->PathTree_, prefix, sourceGroupOwnerKey, 0);
  }
  for (const auto& prefix : prefixes) {
    sourceGroup->addToPathTree(sourceGroup->PathTree_, prefix, key3, 0);
  }
  for (int i = 0; i < kNumPrefixes / 2; i++) {
    sourceGroup->addToPathTree(sourceGroup->PathTree_, prefixes[i], key2, 0);
  }
  for (const auto& prefix : prefixes) {
    sourceGroup->addToPathTree(sourceGroup->PathTree_, prefix, key4, 0);
  }

  sourceGroup->movePeersSharedRibOutPathEntries(
      {peer1, peer2, peer3}, targetGroup);

  for (int i = 0; i < kNumPrefixes; i++) {
    const auto& prefix = prefixes[i];

    /*
     * Old group: shared entry copied (kept), staying peer4 untouched, moved
     * peer3's per-peer entry erased.
     */
    EXPECT_NE(
        sourceGroup->getFromPathTree(
            sourceGroup->PathTree_, prefix, sourceGroupOwnerKey, 0),
        nullptr);
    EXPECT_NE(
        sourceGroup->getFromPathTree(sourceGroup->PathTree_, prefix, key4, 0),
        nullptr);
    EXPECT_EQ(
        sourceGroup->getFromPathTree(sourceGroup->PathTree_, prefix, key3, 0),
        nullptr);

    /*
     * New group: shared entry copied under the NEW group's owner key (not the
     * old group's key, not a peer key). peer1 (all shared) has nothing of its
     * own and relies on the copied group entry.
     */
    EXPECT_NE(
        targetGroup->getFromPathTree(
            targetGroup->PathTree_, prefix, targetGroupOwnerKey, 0),
        nullptr);
    EXPECT_EQ(
        targetGroup->getFromPathTree(
            targetGroup->PathTree_, prefix, sourceGroupOwnerKey, 0),
        nullptr);
    EXPECT_EQ(
        targetGroup->getFromPathTree(targetGroup->PathTree_, prefix, key1, 0),
        nullptr);
    // Moved peer3 (non shared) lands under its own owner key.
    EXPECT_NE(
        targetGroup->getFromPathTree(targetGroup->PathTree_, prefix, key3, 0),
        nullptr);
    // Staying peer4's entries are not moved into the new group.
    EXPECT_EQ(
        targetGroup->getFromPathTree(targetGroup->PathTree_, prefix, key4, 0),
        nullptr);

    /*
     * peer2 (some shared) only diverged on the first half: moved there, absent
     * (and shared via the group) on the second half.
     */
    if (i < kNumPrefixes / 2) {
      EXPECT_EQ(
          sourceGroup->getFromPathTree(sourceGroup->PathTree_, prefix, key2, 0),
          nullptr);
      EXPECT_NE(
          targetGroup->getFromPathTree(targetGroup->PathTree_, prefix, key2, 0),
          nullptr);
    }
  }

  // Break peer<->group cycles for the locally created peers.
  for (const auto& p : {peer1, peer2, peer3, peer4}) {
    p->setUpdateGroup(nullptr);
  }
}

} // namespace facebook::bgp
