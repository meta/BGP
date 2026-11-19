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

/*
 * Unit tests for update group peer, group, and policy re-evaluation.
 *
 * Test matrix:
 * https://docs.google.com/document/d/1XLc3-u0Wx7jTivHVz0tJd-PJ4NKtlPixyitfhD069Iw
 */

#define PeerManager_TEST_FRIENDS                                             \
  friend class UpdateGroupPolicyReEvalUTBase;                                \
  FRIEND_TEST(SplitToGroup, SplitBeforeInitialDumpKeepsTargetUninitialized); \
  FRIEND_TEST(                                                               \
      UpdateGroupsEgressReEvalTest,                                          \
      PolicyUpdateDuringGroupDestroyIsNotDropped);                           \
  FRIEND_TEST(                                                               \
      UpdateGroupsEgressReEvalTest,                                          \
      BufferedRibDumpRemovedBeforeSessionTerminateDrain);

#define AdjRib_TEST_FRIENDS friend class UpdateGroupPolicyReEvalUTBase;

#define AdjRibOutGroup_TEST_FRIENDS                                           \
  FRIEND_TEST(SplitToGroup, SplitBeforeInitialDumpKeepsTargetUninitialized);  \
  FRIEND_TEST(SplitToGroup, CopiesGroupFields);                               \
  FRIEND_TEST(SplitToGroup, ClonesPackingList);                               \
  FRIEND_TEST(SplitToGroup, MovesJoinedRunningPeer);                          \
  FRIEND_TEST(UpdateGroupsEgressReEvalTest, SchedulesConsumeTimerAfterSplit); \
  FRIEND_TEST(SplitToGroup, CopiesGroupFields);                               \
  FRIEND_TEST(SplitToGroup, ClonesPackingList);                               \
  FRIEND_TEST(SplitToGroup, MovesJoinedRunningPeer);                          \
  FRIEND_TEST(                                                                \
      SplitToGroup, SplitBlockedPeerDrainWaitsInBuildAndSendGroupMessages);   \
  FRIEND_TEST(                                                                \
      SplitToGroup, SplitBlockedPeerDrainNoInSyncPeerKeepsConsumingForDSP);   \
  FRIEND_TEST(                                                                \
      UpdateGroupsEgressReEvalTest,                                           \
      PolicyUpdateDuringGroupDestroyIsNotDropped);                            \
  FRIEND_TEST(                                                                \
      UpdateGroupsEgressReEvalTest,                                           \
      BufferedRibDumpRemovedBeforeSessionTerminateDrain);

#include <array>
#include <tuple>

#include <folly/CancellationToken.h>
#include <folly/coro/Baton.h>
#include <folly/coro/Invoke.h>

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/tests/UpdateGroupPolicyReEvalUTCommon.h"

namespace facebook::bgp {

/*
 * Event-base turns to pump when asserting that a coroutine stays parked. Larger
 * than any bounded coroutine's suspension chain, so the parked-state assertions
 * hold only because the coroutine is genuinely blocked, not merely un-run yet.
 */
constexpr int kEvbPumpTurns = 50;

class UpdateGroupPeerGroupAndPolicyReEvalTest
    : public UpdateGroupPolicyReEvalUTBase {};

TEST_F(
    UpdateGroupPeerGroupAndPolicyReEvalTest,
    TriggerDetachedBlocked_FrequencyDetach) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  auto peerId0 = makePeerId(0);
  auto peerId1 = makePeerId(1);

  triggerDetachedBlockedFromJoinedOnEvb(ctx, peerId0, true);

  auto& evb = ctx.peerMgr->getEventBase();
  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib0 = ctx.adjRibs.at(peerId0);
    auto& adjRib1 = ctx.adjRibs.at(peerId1);
    auto group = adjRib1->getUpdateGroup();

    EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::DETACHED_BLOCKED);
    EXPECT_EQ(adjRib1->getPeerState(), PeerUpdateState::JOINED_RUNNING);
    EXPECT_EQ(group->getNumInSyncPeers(), 1);
  });

  tearDown(ctx);
}

TEST_F(UpdateGroupPeerGroupAndPolicyReEvalTest, PeerDownPeerUp) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  auto peerId0 = makePeerId(0);
  auto& evb = ctx.peerMgr->getEventBase();

  auto originalGroup = ctx.adjRibs.at(peerId0)->getUpdateGroup();

  evb.runInEventBaseThreadAndWait([&]() {
    EXPECT_EQ(
        ctx.adjRibs.at(peerId0)->getPeerState(),
        PeerUpdateState::JOINED_RUNNING);
  });

  triggerPeerDownOnEvb(ctx, peerId0);

  expectEventualStateOnEvb(ctx, peerId0, PeerUpdateState::DOWN);

  triggerPeerUpOnEvb(ctx, peerId0);

  WITH_RETRIES({
    auto state = folly::via(&evb, [&]() {
                   return ctx.adjRibs.at(peerId0)->getPeerState();
                 }).get();
    EXPECT_EVENTUALLY_NE(state, PeerUpdateState::DOWN);
  });

  // Verify peer rejoined the same update group
  evb.runInEventBaseThreadAndWait([&]() {
    auto newGroup = ctx.adjRibs.at(peerId0)->getUpdateGroup();
    EXPECT_EQ(
        UpdateGroupKey::toString(newGroup->getGroupKey()),
        UpdateGroupKey::toString(originalGroup->getGroupKey()));
  });

  tearDown(ctx);
}

TEST_F(UpdateGroupPeerGroupAndPolicyReEvalTest, DetachedReadyToJoin_DFP) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  auto peerId0 = makePeerId(0);
  auto peerId1 = makePeerId(1);

  triggerPeerDetachedReadyToJoin(ctx, peerId1, /*isDFP=*/true);

  auto& evb = ctx.peerMgr->getEventBase();
  evb.runInEventBaseThreadAndWait([&]() {
    EXPECT_EQ(
        ctx.adjRibs.at(peerId1)->getPeerState(),
        PeerUpdateState::DETACHED_READY_TO_JOIN);
    EXPECT_EQ(
        ctx.adjRibs.at(peerId0)->getPeerState(),
        PeerUpdateState::JOINED_BLOCKED);
  });

  tearDown(ctx);
}

TEST_F(UpdateGroupPeerGroupAndPolicyReEvalTest, DetachedReadyToJoin_DSP) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  auto peerId0 = makePeerId(0);
  auto peerId1 = makePeerId(1);

  triggerPeerDetachedReadyToJoin(ctx, peerId1, /*isDFP=*/false);

  auto& evb = ctx.peerMgr->getEventBase();
  evb.runInEventBaseThreadAndWait([&]() {
    EXPECT_EQ(
        ctx.adjRibs.at(peerId1)->getPeerState(),
        PeerUpdateState::DETACHED_READY_TO_JOIN);
    EXPECT_EQ(
        ctx.adjRibs.at(peerId0)->getPeerState(),
        PeerUpdateState::JOINED_BLOCKED);
  });

  tearDown(ctx);
}

TEST_F(UpdateGroupPeerGroupAndPolicyReEvalTest, DetachedBlocked_FromDetached) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  auto peerId0 = makePeerId(0);

  triggerDetachedBlockedFromDetachedOnEvb(ctx, peerId0);

  auto& evb = ctx.peerMgr->getEventBase();
  evb.runInEventBaseThreadAndWait([&]() {
    EXPECT_EQ(
        ctx.adjRibs.at(peerId0)->getPeerState(),
        PeerUpdateState::DETACHED_BLOCKED);
  });

  tearDown(ctx);
}

TEST_F(UpdateGroupPeerGroupAndPolicyReEvalTest, DetachedInitDump) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);

  auto peerId0 = makePeerId(0);
  expectEventualStateOnEvb(ctx, peerId0, PeerUpdateState::JOINED_RUNNING);

  auto didPeerId = triggerDetachedInitDump(ctx, 0);

  auto& evb = ctx.peerMgr->getEventBase();
  evb.runInEventBaseThreadAndWait([&]() {
    EXPECT_EQ(
        ctx.adjRibs.at(didPeerId)->getPeerState(),
        PeerUpdateState::DETACHED_INIT_DUMP);
  });

  tearDown(ctx);
}

TEST_F(
    UpdateGroupPeerGroupAndPolicyReEvalTest,
    GroupWaitingBlockedByJoinedPeer) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  auto peerId0 = makePeerId(0);
  auto peerId1 = makePeerId(1);

  blockGroupViaPeerOnEvb(ctx, peerId1);
  publishRouteUpdates(ctx, /*isInitialDump=*/false);

  auto& evb = ctx.peerMgr->getEventBase();

  WITH_RETRIES({
    auto state = folly::via(&evb, [&]() {
                   return ctx.adjRibs.at(peerId0)->getUpdateGroup()->getState();
                 }).get();
    EXPECT_EVENTUALLY_EQ(state, UpdateGroupState::WAITING);
  });

  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib0 = ctx.adjRibs.at(peerId0);
    auto& adjRib1 = ctx.adjRibs.at(peerId1);

    EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_RUNNING);
    EXPECT_EQ(adjRib1->getPeerState(), PeerUpdateState::JOINED_BLOCKED);

    auto group = adjRib0->getUpdateGroup();
    EXPECT_EQ(group->getState(), UpdateGroupState::WAITING);
    EXPECT_EQ(group->getMemberCount(), 2);
  });

  tearDown(ctx);
}

/*
 * splitToNewGroup re-homes a subset of peers from one update group into a new
 * group, cloning the group's operational state onto it. There is no production
 * caller yet (the per-peer split path is a TODO(6.6) stub), so each test stands
 * up a real group and peers via the fixture, drives a peer into a genuine
 * state, then calls splitToNewGroup directly and verifies the re-home.
 */
class SplitToGroup : public UpdateGroupPolicyReEvalUTBase {};

/*
 * Smoke test that just prints serializeSharedRibOutTreesGroupKeyNormalized's
 * output so the format can be eyeballed. The realistic fixture is non-add-path,
 * so PathTree is empty and the entries land in LiteTree.
 */
TEST_F(SplitToGroup, SerializeRibOutTreesForOwner_PrintsSnapshot) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  auto peerId1 = makePeerId(1);
  expectEventualStateOnEvb(ctx, peerId1, PeerUpdateState::JOINED_RUNNING);

  auto& evb = ctx.peerMgr->getEventBase();
  std::string snapshot;
  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib = ctx.adjRibs.at(peerId1);
    snapshot = serializeSharedRibOutTreesGroupKeyNormalized(
        adjRib->getUpdateGroup(), adjRib->getPeerOwnerKey());
  });

  XLOGF(
      INFO,
      "serializeSharedRibOutTreesGroupKeyNormalized(peer1):\n{}",
      snapshot);
  EXPECT_FALSE(snapshot.empty());

  tearDown(ctx);
}

/*
 * splitToNewGroup copies the old group's operational state, flags, group-owned
 * RIB-OUT, change-list tracker, and consumer position onto the new group, and
 * leaves the old group's copies unmodified.
 */
TEST_F(SplitToGroup, CopiesGroupFields) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  auto peerId0 = makePeerId(0);
  auto& evb = ctx.peerMgr->getEventBase();

  /*
   * Route 0 (100.0.0.0/24) is advertised and, with both peers in sync, owned by
   * the group, so it exercises the group-owned RIB-OUT copy.
   */
  const folly::CIDRNetwork sharedPrefix{folly::IPAddress("100.0.0.0"), 24};

  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib0 = ctx.adjRibs.at(peerId0);
    auto sourceGroup = adjRib0->getUpdateGroup();
    ASSERT_NE(sourceGroup, nullptr);
    ASSERT_NE(sourceGroup->getChangeListConsumer(), nullptr);
    ASSERT_NE(
        sourceGroup->getFromLiteTree(
            sourceGroup->LiteTree_,
            sharedPrefix,
            sourceGroup->getGroupOwnerKey()),
        nullptr);

    /*
     * Distinctive (test-chosen) values on the source group so the post-split
     * target-vs-source comparison is meaningful.
     */
    sourceGroup->setState(UpdateGroupState::READY);
    sourceGroup->setLastSeenRibVersion(12345);
    sourceGroup->mraiInterval_ = 30000;
    sourceGroup->initialDumpCompletionTimeMs_ = int64_t{777};
    if (!sourceGroup->peeringParams_.has_value()) {
      sourceGroup->peeringParams_ = PeeringParams(fboss::kDefaultVrf);
    }
    sourceGroup->peeringParams_->globalAs = 64512;
    UpdateGroupConfig cfg;
    cfg.enableSerializeGroupPdu = true;
    cfg.slowPeerBlockCountThreshold = 42;
    sourceGroup->setUpdateGroupConfigForTesting(cfg);

    auto* sourceMarker = sourceGroup->getChangeListConsumer()->getMarker();

    /*
     * splitToNewGroup requires the caller to construct the target as a proper
     * update group (enableUpdateGroup + the source's key);
     * copyGroupFieldsToNew- Group deliberately does not copy identity, so a
     * faithful clone must match what UpdateGroupManager::findOrCreateGroup
     * builds.
     */
    auto targetGroup = makeSplitTargetGroup(ctx, evb, sourceGroup);
    sourceGroup->splitToNewGroup(targetGroup, {adjRib0});

    /*
     * Every operational field on the new group matches the old group's: the
     * distinctive values set above prove they were copied, and the old group
     * still holding them proves the split left it unmodified.
     */
    EXPECT_EQ(targetGroup->getState(), sourceGroup->getState());
    EXPECT_EQ(
        targetGroup->getLastSeenRibVersion(),
        sourceGroup->getLastSeenRibVersion());
    EXPECT_EQ(targetGroup->mraiInterval_, sourceGroup->mraiInterval_);
    EXPECT_EQ(
        targetGroup->getInitialDumpCompletionTimeMs(),
        sourceGroup->getInitialDumpCompletionTimeMs());
    ASSERT_TRUE(targetGroup->peeringParams_.has_value());
    EXPECT_EQ(
        targetGroup->peeringParams_->globalAs,
        sourceGroup->peeringParams_->globalAs);
    EXPECT_EQ(
        targetGroup->updateGroupConfig_.enableSerializeGroupPdu,
        sourceGroup->updateGroupConfig_.enableSerializeGroupPdu);
    EXPECT_EQ(
        targetGroup->updateGroupConfig_.slowPeerBlockCountThreshold,
        sourceGroup->updateGroupConfig_.slowPeerBlockCountThreshold);
    // The change list tracker is shared, not duplicated.
    EXPECT_EQ(
        targetGroup->getChangeListTracker(),
        sourceGroup->getChangeListTracker());
    // The new group's consumer joins at the old group's (unchanged) marker.
    ASSERT_NE(targetGroup->getChangeListConsumer(), nullptr);
    EXPECT_EQ(targetGroup->getChangeListConsumer()->getMarker(), sourceMarker);
    EXPECT_EQ(sourceGroup->getChangeListConsumer()->getMarker(), sourceMarker);
    // The group-owned RIB-OUT is copied to the new group (kept in the old).
    EXPECT_NE(
        targetGroup->getFromLiteTree(
            targetGroup->LiteTree_,
            sharedPrefix,
            targetGroup->getGroupOwnerKey()),
        nullptr);
    EXPECT_NE(
        sourceGroup->getFromLiteTree(
            sourceGroup->LiteTree_,
            sharedPrefix,
            sourceGroup->getGroupOwnerKey()),
        nullptr);

    /*
     * Break the peer<->group and group<->consumer cycles splitToNewGroup
     * created on the unmanaged target group so it (and its copied entries,
     * which reference the AdjRibPolicyCache singleton) is destroyed at end of
     * test instead of leaking past folly Singleton teardown.
     */
    targetGroup->unregisterPeer(adjRib0);
    adjRib0->setUpdateGroup(nullptr);
    if (auto consumer = targetGroup->getChangeListConsumer()) {
      consumer->resetBitmap();
      consumer->terminate();
      consumer->deregisterFromTracker();
    }
    targetGroup->resetChangeListConsumer();
  });

  tearDown(ctx);
}

/*
 * A split that happens before the source group has run its initial dump must
 * leave the new group UNINITIALIZED. scheduleInitialDump() only runs for an
 * UNINITIALIZED group, and the dump it starts is the only code that moves INIT
 * peers to JOINED_RUNNING and arms the egress EoR flags -- so a new group
 * forced to READY never dumps, and the peers it took never receive the RIB or
 * an egress EoR.
 */
TEST_F(SplitToGroup, SplitBeforeInitialDumpKeepsTargetUninitialized) {
  /*
   * No sendInitialRibDump() here: the fixture publishes routes into the shadow
   * RIB but leaves every group UNINITIALIZED, which is exactly the boot window
   * this test needs.
   */
  auto ctx = setUp(2);
  auto peerId0 = makePeerId(0);
  auto& evb = ctx.peerMgr->getEventBase();

  std::shared_ptr<AdjRibOutGroup> targetGroup;
  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib0 = ctx.adjRibs.at(peerId0);
    auto sourceGroup = adjRib0->getUpdateGroup();
    ASSERT_NE(sourceGroup, nullptr);
    ASSERT_EQ(sourceGroup->getState(), UpdateGroupState::UNINITIALIZED);
    ASSERT_EQ(adjRib0->getPeerState(), PeerUpdateState::INIT);

    /*
     * Build the target the way the production split does (the peer manager's
     * egress re-evaluation), so it gets the shadow RIB view its initial dump
     * walks.
     */
    auto newKey = sourceGroup->getGroupKey();
    newKey.egressPolicyName = kPNamePermitAll;
    targetGroup = ctx.peerMgr->updateGroupManager_->findOrCreateGroup(newKey);
    ASSERT_NE(targetGroup, sourceGroup);

    sourceGroup->splitToNewGroup(targetGroup, {adjRib0});

    EXPECT_EQ(targetGroup->getState(), UpdateGroupState::UNINITIALIZED)
        << "target was forced out of UNINITIALIZED, so scheduleInitialDump() "
           "is a no-op for it and the peers it took never get the RIB";
  });

  /*
   * Drive the dump through the state-gated entry point production uses, not by
   * calling the builder directly. The dump coroutine has no suspension points,
   * so one round trip through the event base runs it to completion -- this is
   * ordering, not timing.
   */
  evb.runInEventBaseThreadAndWait(
      [&]() { targetGroup->scheduleInitialDump(); });
  flushEventBase(ctx);

  evb.runInEventBaseThreadAndWait([&]() {
    EXPECT_EQ(
        ctx.adjRibs.at(peerId0)->getPeerState(),
        PeerUpdateState::JOINED_RUNNING)
        << "peer stayed in INIT: the target group never ran its initial dump, "
           "so it is advertised nothing and is owed an EoR that never comes";
  });

  tearDown(ctx);
}

/*
 * splitToNewGroup deep-copies the group-level packing list onto the new group
 * (copyGroupFieldsToNewGroup), so the new group resumes packing from the same
 * state, and the copy is independent of the old group's.
 */
TEST_F(SplitToGroup, ClonesPackingList) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  auto peerId0 = makePeerId(0);
  auto& evb = ctx.peerMgr->getEventBase();

  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib0 = ctx.adjRibs.at(peerId0);
    auto sourceGroup = adjRib0->getUpdateGroup();
    ASSERT_NE(sourceGroup, nullptr);

    /*
     * Seed a distinctive packing list entry on the source group. The key uses
     * null attrs -- supported by BgpPathHashWithNull/BgpPathCompareWithNull --
     * since this test exercises only the map copy, not advertisement.
     */
    const folly::CIDRNetwork packedPrefix{folly::IPAddress("200.0.0.0"), 24};
    BgpPathWithAfi key{};
    PrefixSet prefixes;
    prefixes.insert({packedPrefix, 0});
    sourceGroup->attrToPrefixMap_.clear();
    sourceGroup->attrToPrefixMap_[key] = prefixes;

    auto targetGroup = makeSplitTargetGroup(ctx, evb, sourceGroup);
    sourceGroup->splitToNewGroup(targetGroup, {adjRib0});

    /*
     * The new group holds an equal copy of the packing list; the old group
     * still holds its own.
     */
    ASSERT_EQ(targetGroup->attrToPrefixMap_.size(), 1);
    auto targetItr = targetGroup->attrToPrefixMap_.find(key);
    ASSERT_NE(targetItr, targetGroup->attrToPrefixMap_.end());
    EXPECT_EQ(targetItr->second.count({packedPrefix, 0u}), 1);
    EXPECT_EQ(sourceGroup->attrToPrefixMap_.size(), 1);

    /*
     * The copy is independent: clearing the source's packing list leaves the
     * new group's intact.
     */
    sourceGroup->attrToPrefixMap_.clear();
    EXPECT_EQ(targetGroup->attrToPrefixMap_.size(), 1);

    /*
     * Drop the new group's synthetic entry before teardown so nothing attempts
     * to advertise the null-attrs path.
     */
    targetGroup->attrToPrefixMap_.clear();

    /*
     * Break the peer<->group and group<->consumer cycles splitToNewGroup
     * created on the unmanaged target group so it (and its copied entries,
     * which reference the AdjRibPolicyCache singleton) is destroyed at end of
     * test instead of leaking past folly Singleton teardown.
     */
    targetGroup->unregisterPeer(adjRib0);
    adjRib0->setUpdateGroup(nullptr);
    if (auto consumer = targetGroup->getChangeListConsumer()) {
      consumer->resetBitmap();
      consumer->terminate();
      consumer->deregisterFromTracker();
    }
    targetGroup->resetChangeListConsumer();
  });

  tearDown(ctx);
}

/*
 * A JOINED_RUNNING peer is in sync: the split keeps it in sync in the new group
 * with its RIB-OUT preserved entry-for-entry.
 */
TEST_F(SplitToGroup, MovesJoinedRunningPeer) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  auto peerId1 = makePeerId(1);
  expectEventualStateOnEvb(ctx, peerId1, PeerUpdateState::JOINED_RUNNING);

  auto& evb = ctx.peerMgr->getEventBase();
  std::string ribOutBefore, ribOutAfter;
  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib = ctx.adjRibs.at(peerId1);
    auto sourceGroup = adjRib->getUpdateGroup();
    auto ownerKey = adjRib->getPeerOwnerKey();
    ribOutBefore =
        serializeSharedRibOutTreesGroupKeyNormalized(sourceGroup, ownerKey);

    auto targetGroup = makeSplitTargetGroup(ctx, evb, sourceGroup);
    sourceGroup->splitToNewGroup(targetGroup, {adjRib});

    // Re-homed into the new group; peer 0 stays behind.
    EXPECT_EQ(adjRib->getUpdateGroup(), targetGroup);
    EXPECT_EQ(targetGroup->getMemberCount(), 1);
    EXPECT_EQ(sourceGroup->getMemberCount(), 1);
    // State preserved; still in sync, not detached.
    EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::JOINED_RUNNING);
    EXPECT_FALSE(adjRib->isDetachedPeer());
    EXPECT_TRUE(targetGroup->isPeerInSync(adjRib->getGroupBitPosition()));

    ribOutAfter =
        serializeSharedRibOutTreesGroupKeyNormalized(targetGroup, ownerKey);
    /*
     * Break the peer<->group and group<->consumer cycles splitToNewGroup
     * created on the unmanaged target group so it (and its copied entries,
     * which reference the AdjRibPolicyCache singleton) is destroyed at end of
     * test instead of leaking past folly Singleton teardown.
     */
    targetGroup->unregisterPeer(adjRib);
    adjRib->setUpdateGroup(nullptr);
    if (auto consumer = targetGroup->getChangeListConsumer()) {
      consumer->resetBitmap();
      consumer->terminate();
      consumer->deregisterFromTracker();
    }
    targetGroup->resetChangeListConsumer();
  });

  EXPECT_NE(ribOutBefore, "Path{}, Lite{}");
  EXPECT_EQ(ribOutAfter, ribOutBefore);

  tearDown(ctx);
}

/*
 * Regression: splitToNewGroup seeds the new group's egress prefix counts from
 * the source group (copyGroupFieldsToNewGroup -> copyEgressPrefixCountsFrom),
 * so the new group's postOutPrefixCount matches the prefix set it inherits at
 * split time instead of starting at 0. Left at 0, a later withdrawal would
 * underflow it and an in-sync peer going down would subtract 0 from the global
 * totalSentPrefixCount (leaking that peer's share). The global total itself is
 * unchanged by the split -- it only re-homes existing advertisements.
 */
TEST_F(SplitToGroup, CopiesEgressPrefixCountsToNewGroup) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  auto peerId1 = makePeerId(1);
  expectEventualStateOnEvb(ctx, peerId1, PeerUpdateState::JOINED_RUNNING);

  auto& evb = ctx.peerMgr->getEventBase();
  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib = ctx.adjRibs.at(peerId1);
    auto sourceGroup = adjRib->getUpdateGroup();

    /* Source already advertises the setup routes -> nonzero postOut. */
    const auto srcPost = sourceGroup->getStats().getPostOutPrefixCount();
    const auto srcPostV4 = sourceGroup->getStats().getPostOutPrefixCountIpv4();
    const auto srcPostV6 = sourceGroup->getStats().getPostOutPrefixCountIpv6();
    const auto srcPre = sourceGroup->getStats().getPreOutPrefixCount();
    ASSERT_GT(srcPost, 0u);

    const auto globalBefore = totalSentPrefixCount;

    auto targetGroup = makeSplitTargetGroup(ctx, evb, sourceGroup);
    sourceGroup->splitToNewGroup(targetGroup, {adjRib});

    /* New group inherits the counts; source group's counts are unchanged. */
    EXPECT_EQ(targetGroup->getStats().getPostOutPrefixCount(), srcPost);
    EXPECT_EQ(targetGroup->getStats().getPostOutPrefixCountIpv4(), srcPostV4);
    EXPECT_EQ(targetGroup->getStats().getPostOutPrefixCountIpv6(), srcPostV6);
    EXPECT_EQ(targetGroup->getStats().getPreOutPrefixCount(), srcPre);
    EXPECT_EQ(sourceGroup->getStats().getPostOutPrefixCount(), srcPost);

    /* The split re-homes existing advertisements: global total is untouched. */
    EXPECT_EQ(totalSentPrefixCount, globalBefore);

    /* Mandatory cleanup of the unmanaged target group (see other split tests).
     */
    targetGroup->unregisterPeer(adjRib);
    adjRib->setUpdateGroup(nullptr);
    if (auto consumer = targetGroup->getChangeListConsumer()) {
      consumer->resetBitmap();
      consumer->terminate();
      consumer->deregisterFromTracker();
    }
    targetGroup->resetChangeListConsumer();
  });

  tearDown(ctx);
}

/*
 * A JOINED_BLOCKED peer is still an in-sync state: the split preserves the
 * blocked state, keeps it in sync, and preserves its RIB-OUT entry-for-entry.
 */
TEST_F(SplitToGroup, MovesJoinedBlockedPeer) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  auto peerId1 = makePeerId(1);

  blockGroupViaPeerOnEvb(ctx, peerId1);
  publishRouteUpdates(ctx, /*isInitialDump=*/false);
  expectEventualStateOnEvb(ctx, peerId1, PeerUpdateState::JOINED_BLOCKED);

  auto& evb = ctx.peerMgr->getEventBase();
  std::string ribOutBefore, ribOutAfter;
  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib = ctx.adjRibs.at(peerId1);
    auto sourceGroup = adjRib->getUpdateGroup();
    auto ownerKey = adjRib->getPeerOwnerKey();
    ribOutBefore =
        serializeSharedRibOutTreesGroupKeyNormalized(sourceGroup, ownerKey);

    auto targetGroup = makeSplitTargetGroup(ctx, evb, sourceGroup);
    sourceGroup->splitToNewGroup(targetGroup, {adjRib});

    EXPECT_EQ(adjRib->getUpdateGroup(), targetGroup);
    EXPECT_EQ(targetGroup->getMemberCount(), 1);
    EXPECT_EQ(sourceGroup->getMemberCount(), 1);
    EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::JOINED_BLOCKED);
    EXPECT_FALSE(adjRib->isDetachedPeer());
    EXPECT_TRUE(targetGroup->isPeerInSync(adjRib->getGroupBitPosition()));
    /*
     * The peer is JOINED_BLOCKED: in-sync AND blocked. The blocked bit must be
     * restored on the new group and cleared on the old (it has only the moved
     * peer, so hasBlockedPeers reflects exactly this peer's bit).
     */
    EXPECT_TRUE(targetGroup->hasBlockedPeers());
    EXPECT_FALSE(sourceGroup->hasBlockedPeers());

    ribOutAfter =
        serializeSharedRibOutTreesGroupKeyNormalized(targetGroup, ownerKey);
    /*
     * Break the peer<->group and group<->consumer cycles splitToNewGroup
     * created on the unmanaged target group so it (and its copied entries,
     * which reference the AdjRibPolicyCache singleton) is destroyed at end of
     * test instead of leaking past folly Singleton teardown.
     */
    targetGroup->unregisterPeer(adjRib);
    adjRib->setUpdateGroup(nullptr);
    if (auto consumer = targetGroup->getChangeListConsumer()) {
      consumer->resetBitmap();
      consumer->terminate();
      consumer->deregisterFromTracker();
    }
    targetGroup->resetChangeListConsumer();
  });

  EXPECT_NE(ribOutBefore, "Path{}, Lite{}");
  EXPECT_EQ(ribOutAfter, ribOutBefore);

  tearDown(ctx);
}

/*
 * Regression: splitToNewGroup can hand a group an in-sync peer that still has a
 * deferred push in flight from its previous group (its carried-over bit in
 * adjRibBlockedBitmap_). That push has not yet reached the peer's queue and the
 * packing list cloned into the new group holds only the items that follow it,
 * so buildAndSendGroupBgpMessages must wait for the carried-over push to drain
 * before packing -- otherwise it could enqueue a later UPDATE ahead of the
 * in-flight one. Once the push completes the drain proceeds and the consume
 * timer is rescheduled for the still-in-sync group.
 */
TEST_F(SplitToGroup, SplitBlockedPeerDrainWaitsInBuildAndSendGroupMessages) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  auto peerId1 = makePeerId(1);
  expectEventualStateOnEvb(ctx, peerId1, PeerUpdateState::JOINED_RUNNING);

  /* Block peer1 so a real deferred push is left in flight (blocked bit set). */
  blockGroupViaPeerOnEvb(ctx, peerId1);
  publishRouteUpdates(ctx, /*isInitialDump=*/false);
  expectEventualStateOnEvb(ctx, peerId1, PeerUpdateState::JOINED_BLOCKED);

  auto& evb = ctx.peerMgr->getEventBase();
  std::shared_ptr<AdjRibOutGroup> targetGroup;

  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib = ctx.adjRibs.at(peerId1);
    auto sourceGroup = adjRib->getUpdateGroup();

    targetGroup = makeSplitTargetGroup(ctx, evb, sourceGroup);
    sourceGroup->splitToNewGroup(targetGroup, {adjRib});

    /*
     * The moved peer is in-sync AND blocked: its carried-over push is tracked
     * on the new group, so the drain must wait for it before packing.
     */
    ASSERT_TRUE(targetGroup->hasBlockedPeers());
    ASSERT_EQ(targetGroup->getNumInSyncPeers(), 1);

    /*
     * Replace the cloned packing list with a single announcement so the drain
     * distributes exactly one message that fits peer1's queue once it drains,
     * letting buildAndSendGroupBgpMessages complete and reach its guard.
     */
    targetGroup->attrToPrefixMap_.clear();
    auto fields = buildBgpPathFields(1, 0, 0, 0);
    auto attrs = std::make_shared<BgpPath>(*fields);
    attrs->publish();
    std::shared_ptr<const BgpPath> constAttrs = attrs;
    const folly::CIDRNetwork packedPrefix{folly::IPAddress("200.0.0.0"), 24};
    targetGroup->tryUpdateAttrToPrefixMapForGroup(
        std::make_pair(packedPrefix, kDefaultPathID), nullptr, constAttrs);
    ASSERT_FALSE(targetGroup->getAttrToPrefixMap().empty());

    /* Start the new group's drain; it must park on waitForAllPendingPushes. */
    targetGroup->asyncScope_.add(
        folly::coro::co_withExecutor(
            &evb, targetGroup->buildAndSendGroupBgpMessages()));
  });

  /*
   * While the carried-over push is still in flight the drain stays parked
   * before its guard, so the group has not resumed: the consume timer is not
   * scheduled (buildAndSendGroupBgpMessages cancels it on entry and only
   * reschedules from the guard after the drain completes).
   */
  for (int i = 0; i < kEvbPumpTurns; ++i) {
    evb.runInEventBaseThreadAndWait([]() {});
  }
  evb.runInEventBaseThreadAndWait([&]() {
    EXPECT_TRUE(targetGroup->hasBlockedPeers());
    EXPECT_FALSE(
        targetGroup->changeListConsumeTimer_ &&
        targetGroup->changeListConsumeTimer_->isScheduled());
  });

  /*
   * Complete the carried-over push: draining peer1's queue frees space so the
   * in-flight deferredPushToPeer resolves and clears the blocked bit.
   */
  drainQueue(ctx, peerId1);

  /*
   * The wait completes, the drain finishes, and the group resumes: the guard
   * reschedules the consume timer for the still-in-sync group.
   */
  WITH_RETRIES({
    auto resumed = folly::via(&evb, [&]() {
                     return targetGroup->changeListConsumeTimer_ &&
                         targetGroup->changeListConsumeTimer_->isScheduled();
                   }).get();
    EXPECT_EVENTUALLY_TRUE(resumed);
  });

  /*
   * Tear down the unmanaged target group. Null the consume timer first so a
   * completing drain can't reschedule it, then cancel + join the drain
   * coroutine on the group's async scope before breaking the remaining cycles.
   */
  evb.runInEventBaseThreadAndWait(
      [&]() { targetGroup->resetChangeListConsumeTimer(); });
  folly::coro::blockingWait(targetGroup->drainAsyncScope());
  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib = ctx.adjRibs.at(peerId1);
    targetGroup->unregisterPeer(adjRib);
    adjRib->setUpdateGroup(nullptr);
    if (auto consumer = targetGroup->getChangeListConsumer()) {
      consumer->resetBitmap();
      consumer->terminate();
      consumer->deregisterFromTracker();
    }
    targetGroup->resetChangeListConsumer();
  });

  tearDown(ctx);
}

/*
 * Companion to the reschedule case: if the group is left without an in-sync
 * peer while the carried-over push wait is in flight (the blocked peer
 * detaches), the wait still completes -- detachPeer clears the blocked bit, so
 * the drain never deadlocks. detachPeer also stamps a detachedRibVersion and
 * counts the peer in numPeersDetachedAfterJoin_, making it a sharing DSP, so
 * the drain reschedules the consume timer despite there being no in-sync peer:
 * that DSP needs both consumers to reach the end of the change list before it
 * can rejoin.
 */
TEST_F(SplitToGroup, SplitBlockedPeerDrainNoInSyncPeerKeepsConsumingForDSP) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  auto peerId1 = makePeerId(1);
  expectEventualStateOnEvb(ctx, peerId1, PeerUpdateState::JOINED_RUNNING);

  blockGroupViaPeerOnEvb(ctx, peerId1);
  publishRouteUpdates(ctx, /*isInitialDump=*/false);
  expectEventualStateOnEvb(ctx, peerId1, PeerUpdateState::JOINED_BLOCKED);

  auto& evb = ctx.peerMgr->getEventBase();
  std::shared_ptr<AdjRibOutGroup> targetGroup;

  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib = ctx.adjRibs.at(peerId1);
    auto sourceGroup = adjRib->getUpdateGroup();

    targetGroup = makeSplitTargetGroup(ctx, evb, sourceGroup);
    sourceGroup->splitToNewGroup(targetGroup, {adjRib});

    ASSERT_TRUE(targetGroup->hasBlockedPeers());
    ASSERT_EQ(targetGroup->getNumInSyncPeers(), 1);

    targetGroup->attrToPrefixMap_.clear();
    auto fields = buildBgpPathFields(1, 0, 0, 0);
    auto attrs = std::make_shared<BgpPath>(*fields);
    attrs->publish();
    std::shared_ptr<const BgpPath> constAttrs = attrs;
    const folly::CIDRNetwork packedPrefix{folly::IPAddress("200.0.0.0"), 24};
    targetGroup->tryUpdateAttrToPrefixMapForGroup(
        std::make_pair(packedPrefix, kDefaultPathID), nullptr, constAttrs);
    ASSERT_FALSE(targetGroup->getAttrToPrefixMap().empty());

    targetGroup->asyncScope_.add(
        folly::coro::co_withExecutor(
            &evb, targetGroup->buildAndSendGroupBgpMessages()));
  });

  /* Let the drain reach its parked state on the carried-over push. */
  for (int i = 0; i < kEvbPumpTurns; ++i) {
    evb.runInEventBaseThreadAndWait([]() {});
  }
  evb.runInEventBaseThreadAndWait(
      [&]() { EXPECT_TRUE(targetGroup->hasBlockedPeers()); });

  /*
   * Detach the only in-sync peer. detachPeer clears the blocked bit (releasing
   * the wait) and the sync bit, leaving the group with no in-sync peer.
   */
  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib = ctx.adjRibs.at(peerId1);
    targetGroup->detachPeer(adjRib, AdjRibOutGroup::DetachReason::Policy);
    EXPECT_EQ(targetGroup->getNumInSyncPeers(), 0);
    EXPECT_FALSE(targetGroup->hasBlockedPeers());
  });

  /*
   * The wait completes and the drain finishes -- the seeded packing-list entry
   * is consumed (distributed to zero in-sync peers), proving no deadlock -- and
   * the guard reschedules the consume timer off the sharing DSP the detach
   * created, even though no in-sync peer remains.
   */
  WITH_RETRIES({
    auto drainedAndStillConsuming =
        folly::via(&evb, [&]() {
          return targetGroup->getAttrToPrefixMap().empty() &&
              targetGroup->getNumInSyncPeers() == 0 &&
              targetGroup->getNumPeersDetachedAfterJoin() > 0 &&
              targetGroup->changeListConsumeTimer_ &&
              targetGroup->changeListConsumeTimer_->isScheduled();
        }).get();
    EXPECT_EVENTUALLY_TRUE(drainedAndStillConsuming);
  });

  evb.runInEventBaseThreadAndWait(
      [&]() { targetGroup->resetChangeListConsumeTimer(); });
  folly::coro::blockingWait(targetGroup->drainAsyncScope());
  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib = ctx.adjRibs.at(peerId1);
    targetGroup->unregisterPeer(adjRib);
    adjRib->setUpdateGroup(nullptr);
    if (auto consumer = targetGroup->getChangeListConsumer()) {
      consumer->resetBitmap();
      consumer->terminate();
      consumer->deregisterFromTracker();
    }
    targetGroup->resetChangeListConsumer();
  });

  tearDown(ctx);
}

/*
 * A DETACHED_BLOCKED peer is out of sync: the split keeps it detached and out
 * of sync in the new group, and its RIB-OUT (per-peer + shared) is preserved
 * entry-for-entry -- a detached peer keeps its detached RIB version across the
 * split, so even its per-peer entries carry over unchanged.
 */
TEST_F(SplitToGroup, MovesDetachedBlockedPeer) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  auto peerId1 = makePeerId(1);

  triggerDetachedBlockedFromJoinedOnEvb(
      ctx, peerId1, /*useBlockFrequency=*/true);
  expectEventualStateOnEvb(ctx, peerId1, PeerUpdateState::DETACHED_BLOCKED);

  auto& evb = ctx.peerMgr->getEventBase();
  std::string ribOutBefore, ribOutAfter;
  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib = ctx.adjRibs.at(peerId1);
    auto sourceGroup = adjRib->getUpdateGroup();
    auto ownerKey = adjRib->getPeerOwnerKey();
    ribOutBefore =
        serializeSharedRibOutTreesGroupKeyNormalized(sourceGroup, ownerKey);

    auto targetGroup = makeSplitTargetGroup(ctx, evb, sourceGroup);
    sourceGroup->splitToNewGroup(targetGroup, {adjRib});

    EXPECT_EQ(adjRib->getUpdateGroup(), targetGroup);
    EXPECT_EQ(targetGroup->getMemberCount(), 1);
    EXPECT_EQ(sourceGroup->getMemberCount(), 1);
    EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::DETACHED_BLOCKED);
    EXPECT_TRUE(adjRib->isDetachedPeer());
    EXPECT_FALSE(targetGroup->isPeerInSync(adjRib->getGroupBitPosition()));

    ribOutAfter =
        serializeSharedRibOutTreesGroupKeyNormalized(targetGroup, ownerKey);
    /*
     * Break the peer<->group and group<->consumer cycles splitToNewGroup
     * created on the unmanaged target group so it (and its copied entries,
     * which reference the AdjRibPolicyCache singleton) is destroyed at end of
     * test instead of leaking past folly Singleton teardown.
     */
    targetGroup->unregisterPeer(adjRib);
    adjRib->setUpdateGroup(nullptr);
    if (auto consumer = targetGroup->getChangeListConsumer()) {
      consumer->resetBitmap();
      consumer->terminate();
      consumer->deregisterFromTracker();
    }
    targetGroup->resetChangeListConsumer();
  });

  EXPECT_NE(ribOutBefore, "Path{}, Lite{}");
  EXPECT_EQ(ribOutAfter, ribOutBefore);

  tearDown(ctx);
}

/*
 * A DETACHED_READY_TO_JOIN peer is out of sync: the split keeps it detached and
 * out of sync in the new group with its RIB-OUT preserved entry-for-entry.
 */
TEST_F(SplitToGroup, MovesDetachedReadyToJoinPeer) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  auto peerId1 = makePeerId(1);

  triggerPeerDetachedReadyToJoin(ctx, peerId1, /*isDFP=*/true);
  expectEventualStateOnEvb(
      ctx, peerId1, PeerUpdateState::DETACHED_READY_TO_JOIN);

  auto& evb = ctx.peerMgr->getEventBase();
  std::string ribOutBefore, ribOutAfter;
  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib = ctx.adjRibs.at(peerId1);
    auto sourceGroup = adjRib->getUpdateGroup();
    auto ownerKey = adjRib->getPeerOwnerKey();
    ribOutBefore =
        serializeSharedRibOutTreesGroupKeyNormalized(sourceGroup, ownerKey);

    auto targetGroup = makeSplitTargetGroup(ctx, evb, sourceGroup);
    sourceGroup->splitToNewGroup(targetGroup, {adjRib});

    EXPECT_EQ(adjRib->getUpdateGroup(), targetGroup);
    EXPECT_EQ(targetGroup->getMemberCount(), 1);
    EXPECT_EQ(sourceGroup->getMemberCount(), 1);
    EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::DETACHED_READY_TO_JOIN);
    EXPECT_TRUE(adjRib->isDetachedPeer());
    EXPECT_FALSE(targetGroup->isPeerInSync(adjRib->getGroupBitPosition()));

    ribOutAfter =
        serializeSharedRibOutTreesGroupKeyNormalized(targetGroup, ownerKey);
    /*
     * Break the peer<->group and group<->consumer cycles splitToNewGroup
     * created on the unmanaged target group so it (and its copied entries,
     * which reference the AdjRibPolicyCache singleton) is destroyed at end of
     * test instead of leaking past folly Singleton teardown.
     */
    targetGroup->unregisterPeer(adjRib);
    adjRib->setUpdateGroup(nullptr);
    if (auto consumer = targetGroup->getChangeListConsumer()) {
      consumer->resetBitmap();
      consumer->terminate();
      consumer->deregisterFromTracker();
    }
    targetGroup->resetChangeListConsumer();
  });

  EXPECT_NE(ribOutBefore, "Path{}, Lite{}");
  EXPECT_EQ(ribOutAfter, ribOutBefore);

  tearDown(ctx);
}

/*
 * A DETACHED_INIT_DUMP peer is out of sync: the split keeps it detached and out
 * of sync in the new group. The peer's own RIB-OUT may be empty mid initial
 * dump, but the group-owned (shared) entries are copied faithfully, so its
 * serialized RIB-OUT view is preserved entry-for-entry.
 */
TEST_F(SplitToGroup, MovesDetachedInitDumpPeer) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);

  auto peerId0 = makePeerId(0);
  expectEventualStateOnEvb(ctx, peerId0, PeerUpdateState::JOINED_RUNNING);

  /*
   * Detaches peer 0 into DETACHED_INIT_DUMP; peer 1 stays JOINED_RUNNING so the
   * source group keeps an in-sync member after the split.
   */
  auto detachedPeerId = triggerDetachedInitDump(ctx, 0);

  auto& evb = ctx.peerMgr->getEventBase();
  std::string ribOutBefore, ribOutAfter;
  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib = ctx.adjRibs.at(detachedPeerId);
    auto sourceGroup = adjRib->getUpdateGroup();
    auto ownerKey = adjRib->getPeerOwnerKey();
    ribOutBefore =
        serializeSharedRibOutTreesGroupKeyNormalized(sourceGroup, ownerKey);

    auto targetGroup = makeSplitTargetGroup(ctx, evb, sourceGroup);
    sourceGroup->splitToNewGroup(targetGroup, {adjRib});

    EXPECT_EQ(adjRib->getUpdateGroup(), targetGroup);
    EXPECT_EQ(targetGroup->getMemberCount(), 1);
    EXPECT_EQ(sourceGroup->getMemberCount(), 1);
    EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::DETACHED_INIT_DUMP);
    EXPECT_TRUE(adjRib->isDetachedPeer());
    EXPECT_FALSE(targetGroup->isPeerInSync(adjRib->getGroupBitPosition()));

    ribOutAfter =
        serializeSharedRibOutTreesGroupKeyNormalized(targetGroup, ownerKey);
    /*
     * Break the peer<->group and group<->consumer cycles splitToNewGroup
     * created on the unmanaged target group so it (and its copied entries,
     * which reference the AdjRibPolicyCache singleton) is destroyed at end of
     * test instead of leaking past folly Singleton teardown.
     */
    targetGroup->unregisterPeer(adjRib);
    adjRib->setUpdateGroup(nullptr);
    if (auto consumer = targetGroup->getChangeListConsumer()) {
      consumer->resetBitmap();
      consumer->terminate();
      consumer->deregisterFromTracker();
    }
    targetGroup->resetChangeListConsumer();
  });

  EXPECT_NE(ribOutBefore, "Path{}, Lite{}");
  EXPECT_EQ(ribOutAfter, ribOutBefore);

  tearDown(ctx);
}

/*
 * splitToNewGroup can move a set of peers -- a mix of in-sync and detached --
 * in one call: peer 1 (DETACHED_BLOCKED) and peer 2 (JOINED_RUNNING) are split
 * out together while peer 0 stays behind. Each moved peer is re-homed at its
 * own bit with its state, in-sync status, and RIB-OUT preserved
 * entry-for-entry, and the peer left behind is unaffected.
 */
TEST_F(SplitToGroup, MovesMultiplePeersAtOnce) {
  auto ctx = setUp(3);
  sendInitialRibDump(ctx);
  auto peerId0 = makePeerId(0);
  auto peerId1 = makePeerId(1);
  auto peerId2 = makePeerId(2);

  // Detach peer 1; peers 0 and 2 stay JOINED_RUNNING.
  triggerDetachedBlockedFromJoinedOnEvb(
      ctx, peerId1, /*useBlockFrequency=*/true);
  expectEventualStateOnEvb(ctx, peerId1, PeerUpdateState::DETACHED_BLOCKED);
  expectEventualStateOnEvb(ctx, peerId2, PeerUpdateState::JOINED_RUNNING);

  auto& evb = ctx.peerMgr->getEventBase();
  /*
   * Naming: <peer><group><when>, e.g. peer1TargetAfter = peer 1's view of the
   * target group after the split.
   */
  std::string peer0SourceBefore, peer0SourceAfter;
  std::string peer1SourceBefore, peer1TargetAfter, peer1SourceAfter;
  std::string peer2SourceBefore, peer2TargetAfter, peer2SourceAfter;
  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib0 = ctx.adjRibs.at(peerId0);
    auto& adjRib1 = ctx.adjRibs.at(peerId1);
    auto& adjRib2 = ctx.adjRibs.at(peerId2);
    auto sourceGroup = adjRib1->getUpdateGroup();
    ASSERT_EQ(adjRib0->getUpdateGroup(), sourceGroup);
    ASSERT_EQ(adjRib2->getUpdateGroup(), sourceGroup);
    auto ownerKey0 = adjRib0->getPeerOwnerKey();
    auto ownerKey1 = adjRib1->getPeerOwnerKey();
    auto ownerKey2 = adjRib2->getPeerOwnerKey();
    peer0SourceBefore =
        serializeSharedRibOutTreesGroupKeyNormalized(sourceGroup, ownerKey0);
    peer1SourceBefore =
        serializeSharedRibOutTreesGroupKeyNormalized(sourceGroup, ownerKey1);
    peer2SourceBefore =
        serializeSharedRibOutTreesGroupKeyNormalized(sourceGroup, ownerKey2);

    auto targetGroup = makeSplitTargetGroup(ctx, evb, sourceGroup);
    sourceGroup->splitToNewGroup(targetGroup, {adjRib1, adjRib2});

    // Both peers re-homed into the new group; peer 0 stays behind.
    EXPECT_EQ(adjRib1->getUpdateGroup(), targetGroup);
    EXPECT_EQ(adjRib2->getUpdateGroup(), targetGroup);
    EXPECT_EQ(adjRib0->getUpdateGroup(), sourceGroup);
    EXPECT_EQ(targetGroup->getMemberCount(), 2);
    EXPECT_EQ(sourceGroup->getMemberCount(), 1);
    /*
     * Moved peers land at distinct bits; each carries its own state and sync
     * status -- peer 1 detached/out-of-sync, peer 2 in-sync.
     */
    EXPECT_NE(adjRib1->getGroupBitPosition(), adjRib2->getGroupBitPosition());
    EXPECT_EQ(adjRib1->getPeerState(), PeerUpdateState::DETACHED_BLOCKED);
    EXPECT_TRUE(adjRib1->isDetachedPeer());
    EXPECT_FALSE(targetGroup->isPeerInSync(adjRib1->getGroupBitPosition()));
    EXPECT_EQ(adjRib2->getPeerState(), PeerUpdateState::JOINED_RUNNING);
    EXPECT_FALSE(adjRib2->isDetachedPeer());
    EXPECT_TRUE(targetGroup->isPeerInSync(adjRib2->getGroupBitPosition()));

    /*
     * Re-read each peer's RIB-OUT after the split: peer 0 from the source group
     * it stayed in, and peers 1 and 2 from both the source they left and their
     * new target group.
     */
    peer0SourceAfter =
        serializeSharedRibOutTreesGroupKeyNormalized(sourceGroup, ownerKey0);
    peer1SourceAfter =
        serializeSharedRibOutTreesGroupKeyNormalized(sourceGroup, ownerKey1);
    peer1TargetAfter =
        serializeSharedRibOutTreesGroupKeyNormalized(targetGroup, ownerKey1);
    peer2SourceAfter =
        serializeSharedRibOutTreesGroupKeyNormalized(sourceGroup, ownerKey2);
    peer2TargetAfter =
        serializeSharedRibOutTreesGroupKeyNormalized(targetGroup, ownerKey2);

    /*
     * Break the peer<->group and group<->consumer cycles splitToNewGroup
     * created on the unmanaged target group so it (and its copied entries,
     * which reference the AdjRibPolicyCache singleton) is destroyed at end of
     * test instead of leaking past folly Singleton teardown.
     */
    targetGroup->unregisterPeer(adjRib1);
    targetGroup->unregisterPeer(adjRib2);
    adjRib1->setUpdateGroup(nullptr);
    adjRib2->setUpdateGroup(nullptr);
    if (auto consumer = targetGroup->getChangeListConsumer()) {
      consumer->resetBitmap();
      consumer->terminate();
      consumer->deregisterFromTracker();
    }
    targetGroup->resetChangeListConsumer();
  });

  // Peer 0: in-sync (group-owned only) and untouched by the split.
  EXPECT_EQ(peer0SourceBefore.find("p_"), std::string::npos);
  EXPECT_EQ(peer0SourceAfter, peer0SourceBefore);
  // Peer 1: detached; left the source clean, RIB-OUT preserved in the target.
  EXPECT_EQ(peer1SourceAfter.find("p_"), std::string::npos);
  EXPECT_EQ(peer1TargetAfter, peer1SourceBefore);
  // Peer 2: in-sync; left the source clean, RIB-OUT preserved & group-owned.
  EXPECT_EQ(peer2SourceAfter.find("p_"), std::string::npos);
  EXPECT_EQ(peer2TargetAfter, peer2SourceBefore);
  EXPECT_EQ(peer2TargetAfter.find("p_"), std::string::npos);

  tearDown(ctx);
}

/*
 * When an update group completes its initial dump (with EoR), every member peer
 * must leave initial announcement. Otherwise setPendingEgressPolicyUpdate stays
 * a no-op (it ignores peers still in initial announcement) and egress policy
 * re-evaluation is silently suppressed for the group's peers.
 */
TEST_F(
    UpdateGroupPeerGroupAndPolicyReEvalTest,
    GroupInitialDump_ClearsInInitialAnnouncementForAllMembers) {
  auto ctx = setUp(2);
  auto& evb = ctx.peerMgr->getEventBase();

  // Before the group's initial dump, members are in initial announcement.
  evb.runInEventBaseThreadAndWait([&]() {
    for (auto& [peerId, adjRib] : ctx.adjRibs) {
      EXPECT_TRUE(adjRib->inInitialAnnouncement());
    }
  });

  sendInitialRibDump(ctx);

  // Completing the initial dump cleared it for every member.
  evb.runInEventBaseThreadAndWait([&]() {
    for (auto& [peerId, adjRib] : ctx.adjRibs) {
      EXPECT_FALSE(adjRib->inInitialAnnouncement());
    }
  });

  tearDown(ctx);
}

/*
 * End-to-end effect of clearing initial announcement after the dump: while a
 * member is still in initial announcement setPendingEgressPolicyUpdate is
 * suppressed; once the group's initial dump clears it, the same call is
 * honored. This is the behavior that unblocks egress policy re-evaluation.
 */
TEST_F(
    UpdateGroupPeerGroupAndPolicyReEvalTest,
    GroupInitialDump_EnablesPendingEgressPolicyUpdate) {
  auto ctx = setUp(2);
  auto& evb = ctx.peerMgr->getEventBase();
  auto peerId = makePeerId(0);

  // While in initial announcement, the pending egress flag is suppressed.
  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib = ctx.adjRibs.at(peerId);
    ASSERT_TRUE(adjRib->inInitialAnnouncement());
    adjRib->setPendingEgressPolicyUpdate(true);
    EXPECT_FALSE(adjRib->isEgressPolicyUpdateRequired());
  });

  sendInitialRibDump(ctx);

  // After the dump clears initial announcement, the same call is honored.
  evb.runInEventBaseThreadAndWait([&]() {
    auto& adjRib = ctx.adjRibs.at(peerId);
    ASSERT_FALSE(adjRib->inInitialAnnouncement());
    adjRib->setPendingEgressPolicyUpdate(true);
    EXPECT_TRUE(adjRib->isEgressPolicyUpdateRequired());
  });

  tearDown(ctx);
}

/*
 * Tests for the execution of PeerManager::processGroupEgressPolicyReEvaluation
 * on a group with one in-sync peer (JOINED_BLOCKED) and one detached peer. The
 * in-sync peer is served by the group-level walk (reEvaluateSyncPeersEgress-
 * Policy); the detached peer -- which the group walk does not serve -- is
 * re-evaluated by its own inline RIB dump. Each detached state is paired with
 * the JOINED_BLOCKED peer in its own test.
 */
class GroupEgressPolicyReEvalExecution : public UpdateGroupPolicyReEvalUTBase {
 protected:
  // Block peer0 (an in-sync peer) so it becomes JOINED_BLOCKED.
  void makePeer0JoinedBlocked(TestContext& ctx) {
    blockGroupViaPeerOnEvb(ctx, makePeerId(0));
    publishRouteUpdates(ctx, /*isInitialDump=*/false);
    expectEventualStateOnEvb(
        ctx, makePeerId(0), PeerUpdateState::JOINED_BLOCKED);
  }

  /*
   * Retarget the group to an accept-all policy, run the re-evaluation, and
   * verify both the in-sync JOINED_BLOCKED peer0 (served by the group walk) and
   * the detached peer1 (served by its inline dump) advertise every route with
   * the policy's modify action applied, and both pending flags are cleared.
   */
  void verifyReEvalAdvertisesToBothPeers(
      TestContext& ctx,
      const std::shared_ptr<AdjRibOutGroup>& group) {
    /*
     * Baseline: the default deny policy (kPNameMatchNoAdvtDeny) withholds the
     * 50 odd-indexed routes, so only the 50 even ones are advertised to peer0.
     * Probe just the even routes -- verifyAdvertised asserts the route is
     * advertised, so running it over the denied odd routes would fail.
     */
    EXPECT_EQ(
        verifyRibOutEntries(
            ctx,
            makePeerId(0),
            [](int i) { return i % 2 == 0; },
            verifyAdvertised()),
        50);

    // Retarget the group + members to an accept-all policy and re-evaluate.
    changeGroupEgressPolicyOnEvb(ctx, group, kPNameMatchModifyAppend);
    processGroupEgressPolicyReEvaluationOnEvb(ctx, group);

    // Both consumers (group + detached peer) settle at the end of the CL.
    expectPeersAtEndOfChangeList(ctx, group);

    for (const auto& peerId : {makePeerId(0), makePeerId(1)}) {
      /*
       * All kRouteCount routes (odd and even) are now advertised: peer0 via the
       * group walk, the detached peer via its inline dump.
       */
      EXPECT_EQ(
          verifyRibOutEntries(
              ctx, peerId, [](int) { return true; }, verifyAdvertised()),
          kRouteCount);
      /*
       * The egress policy's modify action is applied: the 34 kPCommModify
       * routes (i % 3 == 0) carry the appended community.
       */
      EXPECT_EQ(
          verifyRibOutEntries(
              ctx,
              peerId,
              [](int i) { return i % 3 == 0; },
              verifyCommOnAdvertisedRoute(kPCommAppend)),
          34);
    }

    // Every member's pending egress flag is cleared.
    auto& evb = ctx.peerMgr->getEventBase();
    evb.runInEventBaseThreadAndWait([&]() {
      EXPECT_FALSE(
          ctx.adjRibs.at(makePeerId(0))->isEgressPolicyUpdateRequired());
      EXPECT_FALSE(
          ctx.adjRibs.at(makePeerId(1))->isEgressPolicyUpdateRequired());
    });
  }

  /*
   * Generalization of verifyReEvalAdvertisesToBothPeers over an arbitrary set
   * of members: retarget the group to the accept-all modify policy,
   * re-evaluate, and verify (A)+(B) the group consumer and every detached peer
   * consumer reach the end of the change list, and (C) every peer -- in-sync
   * (served by the group walk) and detached (served by its inline dump) --
   * advertises every route with the new policy's modify action applied, with
   * all pending flags cleared.
   */
  void verifyReEvalAdvertisesToAllPeers(
      TestContext& ctx,
      const std::shared_ptr<AdjRibOutGroup>& group,
      const std::vector<BgpPeerId>& peerIds) {
    changeGroupEgressPolicyOnEvb(ctx, group, kPNameMatchModifyAppend);
    processGroupEgressPolicyReEvaluationOnEvb(ctx, group);

    // (A)+(B): group consumer + every detached peer consumer at end of the CL.
    expectPeersAtEndOfChangeList(ctx, group);

    // (C): every member advertises all routes under the new policy.
    for (const auto& peerId : peerIds) {
      EXPECT_EQ(
          verifyRibOutEntries(
              ctx, peerId, [](int) { return true; }, verifyAdvertised()),
          kRouteCount);
      EXPECT_EQ(
          verifyRibOutEntries(
              ctx,
              peerId,
              [](int i) { return i % 3 == 0; },
              verifyCommOnAdvertisedRoute(kPCommAppend)),
          34);
    }

    auto& evb = ctx.peerMgr->getEventBase();
    evb.runInEventBaseThreadAndWait([&]() {
      for (const auto& peerId : peerIds) {
        EXPECT_FALSE(ctx.adjRibs.at(peerId)->isEgressPolicyUpdateRequired());
      }
    });
  }
};

/*
 * JOINED_BLOCKED peer0 + DETACHED_BLOCKED peer1: re-evaluation re-advertises
 * all routes to both.
 */
TEST_F(GroupEgressPolicyReEvalExecution, JoinedBlockedAndDetachedBlocked) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  expectEventualStateOnEvb(ctx, makePeerId(0), PeerUpdateState::JOINED_RUNNING);

  triggerDetachedBlockedFromJoinedOnEvb(ctx, makePeerId(1));
  expectEventualStateOnEvb(
      ctx, makePeerId(1), PeerUpdateState::DETACHED_BLOCKED);
  makePeer0JoinedBlocked(ctx);

  verifyReEvalAdvertisesToBothPeers(
      ctx, ctx.adjRibs.at(makePeerId(0))->getUpdateGroup());

  realignPeerKeysToGroupsOnEvb(ctx);
  tearDown(ctx);
}

/*
 * JOINED_BLOCKED peer0 + DETACHED_READY_TO_JOIN peer1: re-evaluation
 * re-advertises all routes to both.
 */
TEST_F(GroupEgressPolicyReEvalExecution, JoinedBlockedAndDetachedReadyToJoin) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  expectEventualStateOnEvb(ctx, makePeerId(0), PeerUpdateState::JOINED_RUNNING);

  /*
   * triggerPeerDetachedReadyToJoin uses peer0 as the blocker, which leaves it
   * JOINED_BLOCKED (the only synced member, so it blocks but is not detached).
   */
  triggerPeerDetachedReadyToJoin(ctx, makePeerId(1), /*isDFP=*/true);
  expectEventualStateOnEvb(
      ctx, makePeerId(1), PeerUpdateState::DETACHED_READY_TO_JOIN);
  expectEventualStateOnEvb(ctx, makePeerId(0), PeerUpdateState::JOINED_BLOCKED);

  verifyReEvalAdvertisesToBothPeers(
      ctx, ctx.adjRibs.at(makePeerId(0))->getUpdateGroup());

  realignPeerKeysToGroupsOnEvb(ctx);
  tearDown(ctx);
}

/*
 * JOINED_BLOCKED peer0 + DETACHED_INIT_DUMP peer1: re-evaluation re-advertises
 * all routes to both.
 */
TEST_F(GroupEgressPolicyReEvalExecution, JoinedBlockedAndDetachedInitDump) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  expectEventualStateOnEvb(ctx, makePeerId(0), PeerUpdateState::JOINED_RUNNING);

  triggerDetachedInitDump(ctx, 1);
  expectEventualStateOnEvb(
      ctx, makePeerId(1), PeerUpdateState::DETACHED_INIT_DUMP);
  makePeer0JoinedBlocked(ctx);

  verifyReEvalAdvertisesToBothPeers(
      ctx, ctx.adjRibs.at(makePeerId(0))->getUpdateGroup());

  realignPeerKeysToGroupsOnEvb(ctx);
  tearDown(ctx);
}

/*
 * A rib dump already scheduled for the detached peer is cancelled and re-run
 * inline by the re-evaluation (so the peer can't rejoin with stale, old-policy
 * entries). Fake the "scheduled" state by arming the peer's rib-dump
 * cancellation source directly -- no async dump, so the cancel branch is
 * exercised deterministically.
 */
TEST_F(GroupEgressPolicyReEvalExecution, CancelsDetachedPeerScheduledRibDump) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  expectEventualStateOnEvb(ctx, makePeerId(0), PeerUpdateState::JOINED_RUNNING);
  triggerDetachedBlockedFromJoinedOnEvb(ctx, makePeerId(1));
  expectEventualStateOnEvb(
      ctx, makePeerId(1), PeerUpdateState::DETACHED_BLOCKED);
  auto group = ctx.adjRibs.at(makePeerId(0))->getUpdateGroup();
  ASSERT_NE(group, nullptr);

  auto detached = ctx.adjRibs.at(makePeerId(1));
  auto& evb = ctx.peerMgr->getEventBase();

  /*
   * Arm the detached peer's rib-dump cancellation source to mimic a scheduled
   * dump (getCancellationTokenForNewRibDump only emplaces the source).
   */
  evb.runInEventBaseThreadAndWait(
      [&]() { detached->getCancellationTokenForNewRibDump(); });
  EXPECT_TRUE(
      folly::via(&evb, [&]() { return detached->isRibDumpScheduled(); }).get());

  changeGroupEgressPolicyOnEvb(ctx, group, kPNameMatchModifyAppend);
  processGroupEgressPolicyReEvaluationOnEvb(ctx, group);

  // The scheduled dump was cancelled and the re-evaluation ran it inline.
  EXPECT_FALSE(
      folly::via(&evb, [&]() { return detached->isRibDumpScheduled(); }).get());

  realignPeerKeysToGroupsOnEvb(ctx);
  tearDown(ctx);
}

/*
 * processDetachedPeerEgressPolicyReEvaluation cancels a rib dump already
 * scheduled for the peer before running the re-evaluation dump inline (so the
 * peer can't rejoin with stale, old-policy entries).
 */
TEST_F(
    GroupEgressPolicyReEvalExecution,
    ProcessPeerEgressPolicyReEvaluation_CancelsScheduledRibDump) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  expectEventualStateOnEvb(ctx, makePeerId(0), PeerUpdateState::JOINED_RUNNING);
  triggerDetachedBlockedFromJoinedOnEvb(ctx, makePeerId(1));
  expectEventualStateOnEvb(
      ctx, makePeerId(1), PeerUpdateState::DETACHED_BLOCKED);

  auto detached = ctx.adjRibs.at(makePeerId(1));
  auto& evb = ctx.peerMgr->getEventBase();

  /*
   * Arm the peer's rib-dump cancellation source to mimic a scheduled dump
   * (getCancellationTokenForNewRibDump only emplaces the source).
   */
  evb.runInEventBaseThreadAndWait(
      [&]() { detached->getCancellationTokenForNewRibDump(); });
  ASSERT_TRUE(
      folly::via(&evb, [&]() { return detached->isRibDumpScheduled(); }).get());

  processDetachedPeerEgressPolicyReEvaluationOnEvb(ctx, detached);

  // The scheduled dump was cancelled and re-run inline.
  EXPECT_FALSE(
      folly::via(&evb, [&]() { return detached->isRibDumpScheduled(); }).get());

  realignPeerKeysToGroupsOnEvb(ctx);
  tearDown(ctx);
}

/*
 * processDetachedPeerEgressPolicyReEvaluation reprocesses the rib dump: after
 * the group policy is retargeted to the accept-all modify policy, re-evaluating
 * the detached peer rebuilds its RIB-OUT so every route is advertised (up from
 * the 50 even routes the deny baseline allowed) with the modify action applied.
 */
TEST_F(
    GroupEgressPolicyReEvalExecution,
    ProcessPeerEgressPolicyReEvaluation_ProcessesRibDumpReq) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  expectEventualStateOnEvb(ctx, makePeerId(0), PeerUpdateState::JOINED_RUNNING);
  triggerDetachedBlockedFromJoinedOnEvb(ctx, makePeerId(1));
  expectEventualStateOnEvb(
      ctx, makePeerId(1), PeerUpdateState::DETACHED_BLOCKED);
  auto detached = ctx.adjRibs.at(makePeerId(1));
  auto group = ctx.adjRibs.at(makePeerId(0))->getUpdateGroup();

  /*
   * Baseline: the deny policy withholds the 50 odd routes, so only the 50 even
   * routes are advertised to the detached peer.
   */
  EXPECT_EQ(
      verifyRibOutEntries(
          ctx,
          makePeerId(1),
          [](int i) { return i % 2 == 0; },
          verifyAdvertised()),
      50);

  // Retarget the group policy, then re-evaluate only the detached peer.
  changeGroupEgressPolicyOnEvb(ctx, group, kPNameMatchModifyAppend);
  processDetachedPeerEgressPolicyReEvaluationOnEvb(ctx, detached);

  // The peer's rib dump was reprocessed under the new policy.
  EXPECT_EQ(
      verifyRibOutEntries(
          ctx, makePeerId(1), [](int) { return true; }, verifyAdvertised()),
      kRouteCount);
  EXPECT_EQ(
      verifyRibOutEntries(
          ctx,
          makePeerId(1),
          [](int i) { return i % 3 == 0; },
          verifyCommOnAdvertisedRoute(kPCommAppend)),
      34);

  realignPeerKeysToGroupsOnEvb(ctx);
  tearDown(ctx);
}

/*
 * processDetachedPeerEgressPolicyReEvaluation forces the peer's change-list
 * consumer to the end of the change list (iterateChanges), so the peer is fully
 * caught up after the re-evaluation.
 */
TEST_F(
    GroupEgressPolicyReEvalExecution,
    ProcessPeerEgressPolicyReEvaluation_MovesConsumerToEndOfChangeList) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  expectEventualStateOnEvb(ctx, makePeerId(0), PeerUpdateState::JOINED_RUNNING);
  triggerDetachedBlockedFromJoinedOnEvb(ctx, makePeerId(1));
  expectEventualStateOnEvb(
      ctx, makePeerId(1), PeerUpdateState::DETACHED_BLOCKED);
  auto detached = ctx.adjRibs.at(makePeerId(1));

  // Publish incremental updates so the change list has entries to consume.
  publishRouteUpdates(ctx, /*isInitialDump=*/false);

  processDetachedPeerEgressPolicyReEvaluationOnEvb(ctx, detached);

  auto& evb = ctx.peerMgr->getEventBase();
  auto atEnd = folly::via(&evb, [&]() {
                 auto consumer = detached->getChangeListConsumer();
                 return consumer && consumer->getMarker() == nullptr;
               }).get();
  EXPECT_TRUE(atEnd);

  realignPeerKeysToGroupsOnEvb(ctx);
  tearDown(ctx);
}

/*
 * A detached peer's re-evaluation must leave it at the RIB's max version even
 * when the tail of the change list is not for its consumer.
 *
 * processRibDumpReq skips entries already pending on the peer's consumer and
 * therefore declines to advance the version off the dump, delegating that to
 * the drain that follows. But the drain only ever sees items this consumer
 * subscribes to, so a trailing add-path-only change -- published with the
 * add-path bitmap, which a non-add-path peer is not in -- carries
 * maxRibVersion past anything the peer can observe item by item.
 */
TEST_F(
    GroupEgressPolicyReEvalExecution,
    ProcessPeerEgressPolicyReEvaluation_AdvancesToMaxRibVersionWhenChangeListTailIsNotForPeer) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  expectEventualStateOnEvb(ctx, makePeerId(0), PeerUpdateState::JOINED_RUNNING);
  triggerDetachedBlockedFromJoinedOnEvb(ctx, makePeerId(1));
  expectEventualStateOnEvb(
      ctx, makePeerId(1), PeerUpdateState::DETACHED_BLOCKED);
  auto detached = ctx.adjRibs.at(makePeerId(1));
  ASSERT_FALSE(detached->sendAddPath());

  // Bestpath updates land on the peer's consumer, so the re-eval dump skips
  // them and leaves the version to the drain.
  publishRouteUpdates(ctx, /*isInitialDump=*/false);

  const uint64_t maxRibVersion = publishAddPathOnlyUpdate(ctx);

  processDetachedPeerEgressPolicyReEvaluationOnEvb(ctx, detached);

  /*
   * Not the version of the last item the peer was offered: the drain reached
   * the end of the change list, so the peer owns the RIB's max version.
   */
  auto& evb = ctx.peerMgr->getEventBase();
  auto peerVersion = folly::via(&evb, [&]() {
                       return detached->getLastSeenRibVersion();
                     }).get();
  EXPECT_EQ(maxRibVersion, peerVersion);

  realignPeerKeysToGroupsOnEvb(ctx);
  tearDown(ctx);
}

/*
 * In-sync JOINED_RUNNING peer0 (unblocked) + DETACHED_BLOCKED peer1:
 * re-evaluation advertises all routes to both and moves both consumers to the
 * end of the change list.
 */
TEST_F(GroupEgressPolicyReEvalExecution, JoinedRunningAndDetachedBlocked) {
  auto ctx = setUp(2);
  sendInitialRibDump(ctx);
  expectEventualStateOnEvb(ctx, makePeerId(0), PeerUpdateState::JOINED_RUNNING);

  triggerDetachedBlockedFromJoinedOnEvb(ctx, makePeerId(1));
  expectEventualStateOnEvb(
      ctx, makePeerId(1), PeerUpdateState::DETACHED_BLOCKED);
  // peer0 stays in-sync and unblocked.
  expectEventualStateOnEvb(ctx, makePeerId(0), PeerUpdateState::JOINED_RUNNING);

  verifyReEvalAdvertisesToAllPeers(
      ctx,
      ctx.adjRibs.at(makePeerId(0))->getUpdateGroup(),
      {makePeerId(0), makePeerId(1)});

  realignPeerKeysToGroupsOnEvb(ctx);
  tearDown(ctx);
}

/*
 * Multiple detached peers in different states (DETACHED_BLOCKED +
 * DETACHED_INIT_DUMP) alongside an in-sync peer: re-evaluation walks every
 * detached peer to the end of the change list and rebuilds each peer's RIB-OUT
 * under the new policy.
 */
TEST_F(GroupEgressPolicyReEvalExecution, MultipleDetachedPeersDifferentStates) {
  auto ctx = setUp(3);
  sendInitialRibDump(ctx);
  expectEventualStateOnEvb(ctx, makePeerId(0), PeerUpdateState::JOINED_RUNNING);

  triggerDetachedBlockedFromJoinedOnEvb(ctx, makePeerId(1));
  expectEventualStateOnEvb(
      ctx, makePeerId(1), PeerUpdateState::DETACHED_BLOCKED);
  triggerDetachedInitDump(ctx, 2);
  expectEventualStateOnEvb(
      ctx, makePeerId(2), PeerUpdateState::DETACHED_INIT_DUMP);

  verifyReEvalAdvertisesToAllPeers(
      ctx,
      ctx.adjRibs.at(makePeerId(0))->getUpdateGroup(),
      {makePeerId(0), makePeerId(1), makePeerId(2)});

  realignPeerKeysToGroupsOnEvb(ctx);
  tearDown(ctx);
}

/*
 * Multiple in-sync peers (peer0, peer1 JOINED_RUNNING) + a DETACHED_BLOCKED
 * peer2: the single group walk re-advertises to both in-sync peers while the
 * detached peer is served by its inline dump; all consumers reach the end of
 * the change list.
 */
TEST_F(GroupEgressPolicyReEvalExecution, MultipleInSyncPeers) {
  auto ctx = setUp(3);
  sendInitialRibDump(ctx);
  expectEventualStateOnEvb(ctx, makePeerId(0), PeerUpdateState::JOINED_RUNNING);
  expectEventualStateOnEvb(ctx, makePeerId(1), PeerUpdateState::JOINED_RUNNING);

  triggerDetachedBlockedFromJoinedOnEvb(ctx, makePeerId(2));
  expectEventualStateOnEvb(
      ctx, makePeerId(2), PeerUpdateState::DETACHED_BLOCKED);

  verifyReEvalAdvertisesToAllPeers(
      ctx,
      ctx.adjRibs.at(makePeerId(0))->getUpdateGroup(),
      {makePeerId(0), makePeerId(1), makePeerId(2)});

  realignPeerKeysToGroupsOnEvb(ctx);
  tearDown(ctx);
}

/*
 * processUpdateGroupsEgressPolicyReevaluation coverage, one behavior per test.
 * Each test stages policy changes (the async pipeline is disabled so the RPCs
 * only stage config), then drives the coroutine by hand once. The coroutine
 * rebuilds every peer's UpdateGroupKey, remaps members to their target key, and
 * reconciles each key into a single group -- keep an existing group at the key,
 * else rekey/split the most-in-sync contributor, then move the rest in --
 * re-evaluating a group only when its egress genuinely changed. Groups that
 * converge on one key (e.g. a partial-override group reset) are merged.
 */
class UpdateGroupsEgressReEvalTest : public UpdateGroupPolicyReEvalUTBase {
 protected:
  auto keyOf(TestContext& ctx, const BgpPeerId& id) {
    auto& evb = ctx.peerMgr->getEventBase();
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroupKey(); })
        .get();
  }
  auto groupOf(TestContext& ctx, const BgpPeerId& id) {
    auto& evb = ctx.peerMgr->getEventBase();
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroup(); })
        .get();
  }
  auto numInSyncOf(TestContext& ctx, const BgpPeerId& id) {
    return groupOf(ctx, id)->getNumInSyncPeers();
  }
  auto memberCountOf(TestContext& ctx, const BgpPeerId& id) {
    return groupOf(ctx, id)->getMemberCount();
  }
  auto stateOf(TestContext& ctx, const BgpPeerId& id) {
    auto& evb = ctx.peerMgr->getEventBase();
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getPeerState(); })
        .get();
  }
  /*
   * Whether peer `id`'s AdjRib is a member of `group` (in its bit map), as
   * opposed to merely having a stale pointer at it.
   */
  bool isMemberOf(
      TestContext& ctx,
      const std::shared_ptr<AdjRibOutGroup>& group,
      const BgpPeerId& id) {
    auto& evb = ctx.peerMgr->getEventBase();
    return folly::via(
               &evb,
               [&]() {
                 auto adjRib = ctx.adjRibs.at(id);
                 for (const auto& [bit, member] : group->getBitToAdjRibs()) {
                   if (member == adjRib) {
                     return true;
                   }
                 }
                 return false;
               })
        .get();
  }
};

/*
 * When processUpdateGroupsEgressPolicyReevaluation splits part of a group into
 * a newly created group (splitToNewGroup), the post-reconciliation recovery
 * pass restarts the change-list consume timer for every affected group that
 * still has an in-sync peer -- the split-off target group and the source group
 * the peer left behind.
 */
TEST_F(UpdateGroupsEgressReEvalTest, SchedulesConsumeTimerAfterSplit) {
  constexpr int kN = 2;
  const std::string kPg = "PEERGROUP_1";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;
  const std::string kPolicy1 = kPNameMatchModifyAppend;

  auto ctx = setUpGroups({{kPg, kN}}, true, kPolicy0);
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  expectEventualStateOnEvb(ctx, peer(0), PeerUpdateState::JOINED_RUNNING);
  expectEventualStateOnEvb(ctx, peer(1), PeerUpdateState::JOINED_RUNNING);

  auto& evb = ctx.peerMgr->getEventBase();
  auto groupOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroup(); })
        .get();
  };
  auto isConsumeTimerScheduled =
      [&](const std::shared_ptr<AdjRibOutGroup>& group) {
        return folly::via(
                   &evb,
                   [&]() {
                     return group->changeListConsumeTimer_ != nullptr &&
                         group->changeListConsumeTimer_->isScheduled();
                   })
            .get();
      };

  // Both peers converge into a single source group.
  auto sourceGroup = groupOf(peer(0));
  ASSERT_EQ(sourceGroup, groupOf(peer(1)));

  /*
   * Override peer 0 only. Its update group key diverges while peer 1's stays,
   * so the reconciliation moves only part of the source group -- taking the
   * splitToNewGroup path into a brand-new target group.
   */
  disableAsyncEgressReEvalOnEvb(ctx);
  updatePeerEgressPolicyOnEvb(ctx, peer(0), kPolicy1);
  markEgressPolicyUpdateRequiredOnEvb(ctx, {peer(0), peer(1)});
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);

  auto targetGroup = groupOf(peer(0));
  // Peer 0 split off into a distinct new group; peer 1 stayed behind.
  ASSERT_NE(targetGroup, sourceGroup);
  ASSERT_EQ(groupOf(peer(1)), sourceGroup);
  ASSERT_EQ(targetGroup->getNumInSyncPeers(), 1);
  ASSERT_EQ(sourceGroup->getNumInSyncPeers(), 1);

  /*
   * The recovery pass restarts the consume timer on both groups that retain an
   * in-sync peer: the split-off target and the source left behind.
   */
  EXPECT_TRUE(isConsumeTimerScheduled(targetGroup));
  EXPECT_TRUE(isConsumeTimerScheduled(sourceGroup));

  tearDown(ctx);
}

/*
 * processUpdateGroupsEgressPolicyReevaluation must ignore peers in the DOWN
 * state. When a session flaps, unregisterPeer removes the peer from its group's
 * member set and marks it DOWN, but the AdjRib stays in adjRibs_ with a stale
 * (non-null) update-group pointer. Re-keying/moving such a peer during
 * egress-policy re-evaluation would be incorrect -- it re-joins with a freshly
 * built key when its session comes back.
 *
 * Setup (3 peers in one source group): peer 1 is flapped DOWN (unregistered,
 * so no longer a member). peers 0 and 1 both get the same diverging override;
 * peer 2 keeps the original policy so the source group survives peer 0's split.
 * Only the UP peer (peer 0) may split out to a new group. The DOWN peer (peer
 * 1) must be left untouched: not a member of any group, not moved into peer 0's
 * new group, key never rebuilt, still DOWN. Without the DOWN guard, peer 1's
 * diverging key would drag it into the move.
 *
 * This exercises the splitToNewGroup reconcile path (the target key has no
 * existing group). The movePeers path is covered by
 * IgnoresDownPeerDuringEgressReevalMoveToExistingGroup below.
 */
TEST_F(UpdateGroupsEgressReEvalTest, IgnoresDownPeerDuringEgressReeval) {
  constexpr int kN = 3;
  const std::string kPg = "PEERGROUP_1";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;
  const std::string kPolicy1 = kPNameMatchModifyAppend;

  auto ctx = setUpGroups({{kPg, kN}}, true, kPolicy0);
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  expectEventualStateOnEvb(ctx, peer(0), PeerUpdateState::JOINED_RUNNING);
  expectEventualStateOnEvb(ctx, peer(1), PeerUpdateState::JOINED_RUNNING);
  expectEventualStateOnEvb(ctx, peer(2), PeerUpdateState::JOINED_RUNNING);

  // All three peers start in the same source group.
  auto sourceGroup = groupOf(ctx, peer(0));
  ASSERT_EQ(sourceGroup, groupOf(ctx, peer(1)));
  ASSERT_EQ(sourceGroup, groupOf(ctx, peer(2)));

  /*
   * Flap peer 1 through the real session-down path: an IDLE event drives
   * PeerManager::sessionTerminated, which unregisters the peer from its group
   * (dropped from the member set, marked DOWN) while leaving its stale group
   * pointer and its adjRibs_ entry in place.
   */
  triggerPeerDownOnEvb(ctx, peer(1));
  expectEventualStateOnEvb(ctx, peer(1), PeerUpdateState::DOWN);
  ASSERT_FALSE(isMemberOf(ctx, sourceGroup, peer(1)));

  /*
   * peers 0 and 1 both diverge to the same new policy; peer 2 stays on the
   * original policy so peer 0 splits out rather than rekeying in place.
   */
  disableAsyncEgressReEvalOnEvb(ctx);
  updatePeerEgressPolicyOnEvb(ctx, peer(0), kPolicy1);
  updatePeerEgressPolicyOnEvb(ctx, peer(1), kPolicy1);
  markEgressPolicyUpdateRequiredOnEvb(ctx, {peer(0), peer(1)});

  auto downGroupBefore = groupOf(ctx, peer(1));
  auto downKeyBefore = keyOf(ctx, peer(1));

  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);

  // peer 0 (UP) split out to a new group; peer 2 stayed behind.
  auto targetGroup = groupOf(ctx, peer(0));
  EXPECT_NE(targetGroup, sourceGroup);
  EXPECT_TRUE(isMemberOf(ctx, targetGroup, peer(0)));
  EXPECT_EQ(groupOf(ctx, peer(2)), sourceGroup);
  EXPECT_TRUE(isMemberOf(ctx, sourceGroup, peer(2)));

  /*
   * peer 1 (DOWN) was ignored: not a member of either group, not moved into
   * peer 0's new group, stale pointer/key untouched, still DOWN.
   */
  EXPECT_FALSE(isMemberOf(ctx, sourceGroup, peer(1)));
  EXPECT_FALSE(isMemberOf(ctx, targetGroup, peer(1)));
  EXPECT_NE(groupOf(ctx, peer(1)), targetGroup);
  EXPECT_EQ(groupOf(ctx, peer(1)), downGroupBefore);
  EXPECT_EQ(keyOf(ctx, peer(1)), downKeyBefore);
  EXPECT_EQ(stateOf(ctx, peer(1)), PeerUpdateState::DOWN);

  tearDown(ctx);
}

/*
 * Companion to IgnoresDownPeerDuringEgressReeval that exercises the OTHER
 * reconcile path: movePeers into an already-existing target group (Case 1),
 * rather than splitToNewGroup.
 *
 * Setup (4 peers in one source group): peer 3 is first overridden to policy 1
 * and re-evaluated, splitting it off into a standing group at that key. Then
 * peer 1 is flapped DOWN (unregistered, no longer a member) and peers 0 and 1
 * are both overridden to policy 1 -- the key of the standing group. On
 * re-evaluation the UP peer (peer 0) is detached from the source group and
 * moved into the standing group; peer 2 keeps the original policy and stays.
 * The DOWN peer (peer 1) must be left untouched: not a member of any group, not
 * moved into the standing group, key never rebuilt, still DOWN. Without the
 * DOWN guard, peer 1's diverging key would drag it into the move.
 */
TEST_F(
    UpdateGroupsEgressReEvalTest,
    IgnoresDownPeerDuringEgressReevalMoveToExistingGroup) {
  constexpr int kN = 4;
  const std::string kPg = "PEERGROUP_1";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;
  const std::string kPolicy1 = kPNameMatchModifyAppend;

  auto ctx = setUpGroups({{kPg, kN}}, true, kPolicy0);
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  for (int i = 0; i < kN; ++i) {
    expectEventualStateOnEvb(ctx, peer(i), PeerUpdateState::JOINED_RUNNING);
  }

  auto sourceGroup = groupOf(ctx, peer(0));
  ASSERT_EQ(sourceGroup, groupOf(ctx, peer(1)));
  ASSERT_EQ(sourceGroup, groupOf(ctx, peer(2)));
  ASSERT_EQ(sourceGroup, groupOf(ctx, peer(3)));

  /*
   * Split peer 3 out onto policy 1 so a standing target group exists at that
   * key before the peer under test moves.
   */
  updatePeerEgressPolicyOnEvb(ctx, peer(3), kPolicy1);
  WITH_RETRIES(
      { EXPECT_EVENTUALLY_TRUE(groupOf(ctx, peer(3)) != sourceGroup); });

  auto existingTarget = groupOf(ctx, peer(3));
  ASSERT_NE(existingTarget, sourceGroup);
  ASSERT_TRUE(isMemberOf(ctx, existingTarget, peer(3)));

  /*
   * Flap peer 1 through the real session-down path: an IDLE event drives
   * PeerManager::sessionTerminated, which unregisters the peer from its group
   * (dropped from the member set, marked DOWN) while leaving its stale group
   * pointer and its adjRibs_ entry in place.
   */
  triggerPeerDownOnEvb(ctx, peer(1));
  expectEventualStateOnEvb(ctx, peer(1), PeerUpdateState::DOWN);
  ASSERT_FALSE(isMemberOf(ctx, sourceGroup, peer(1)));

  /*
   * peers 0 and 1 both diverge to policy 1 -- the standing group's key -- so
   * they target movePeers into it; peer 2 stays on policy 0 so the source group
   * survives.
   */
  auto downGroupBefore = groupOf(ctx, peer(1));
  auto downKeyBefore = keyOf(ctx, peer(1));
  auto targetMembersBefore = existingTarget->getMemberCount();

  /*
   * One update for both peers, so they are evaluated against the standing
   * group together rather than one at a time.
   */
  updatePeersEgressPolicyOnEvb(ctx, {peer(0), peer(1)}, kPolicy1);
  WITH_RETRIES(
      { EXPECT_EVENTUALLY_EQ(groupOf(ctx, peer(0)), existingTarget); });

  // peer 0 (UP) was moved into the standing target group; peer 2 stayed behind.
  EXPECT_EQ(groupOf(ctx, peer(0)), existingTarget);
  EXPECT_TRUE(isMemberOf(ctx, existingTarget, peer(0)));
  EXPECT_EQ(groupOf(ctx, peer(2)), sourceGroup);
  EXPECT_TRUE(isMemberOf(ctx, sourceGroup, peer(2)));
  // Only peer 0 joined the standing group (peer 3 was already there).
  EXPECT_EQ(existingTarget->getMemberCount(), targetMembersBefore + 1);

  /*
   * peer 1 (DOWN) was ignored: not a member of either group, not moved into the
   * standing target, stale pointer/key untouched, still DOWN.
   */
  EXPECT_FALSE(isMemberOf(ctx, sourceGroup, peer(1)));
  EXPECT_FALSE(isMemberOf(ctx, existingTarget, peer(1)));
  EXPECT_NE(groupOf(ctx, peer(1)), existingTarget);
  EXPECT_EQ(groupOf(ctx, peer(1)), downGroupBefore);
  EXPECT_EQ(keyOf(ctx, peer(1)), downKeyBefore);
  EXPECT_EQ(stateOf(ctx, peer(1)), PeerUpdateState::DOWN);

  tearDown(ctx);
}

/*
 * Third companion covering the rekeyGroup reconcile path: when EVERY member of
 * a group diverges to the same new key and no group exists at that key, the
 * group is rekeyed in place (O(1)) rather than split or moved.
 *
 * Setup (4 peers in one group): peer 1 is flapped DOWN (unregistered, so no
 * longer a member -- the group has 3 live members). All 4 peers get the same
 * egress policy change. On re-evaluation the DOWN peer is skipped, so all 3
 * live members diverge together and the group is rekeyed in place. The result
 * must contain exactly the 3 UP peers; the DOWN peer (peer 1) is not pulled
 * back into the group, its key never rebuilt, still DOWN.
 */
TEST_F(UpdateGroupsEgressReEvalTest, IgnoresDownPeerDuringEgressReevalRekey) {
  constexpr int kN = 4;
  const std::string kPg = "PEERGROUP_1";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;
  const std::string kPolicy1 = kPNameMatchModifyAppend;

  auto ctx = setUpGroups({{kPg, kN}}, true, kPolicy0);
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  for (int i = 0; i < kN; ++i) {
    expectEventualStateOnEvb(ctx, peer(i), PeerUpdateState::JOINED_RUNNING);
  }

  // All four peers start in the same group.
  auto group = groupOf(ctx, peer(0));
  for (int i = 1; i < kN; ++i) {
    ASSERT_EQ(group, groupOf(ctx, peer(i)));
  }
  ASSERT_EQ(group->getMemberCount(), kN);

  /*
   * Flap peer 1 through the real session-down path: an IDLE event drives
   * PeerManager::sessionTerminated, which unregisters the peer from its group
   * (dropped from the member set, marked DOWN) while leaving its stale group
   * pointer and its adjRibs_ entry in place. The group now has 3 live members.
   */
  triggerPeerDownOnEvb(ctx, peer(1));
  expectEventualStateOnEvb(ctx, peer(1), PeerUpdateState::DOWN);
  ASSERT_FALSE(isMemberOf(ctx, group, peer(1)));
  ASSERT_EQ(group->getMemberCount(), kN - 1);

  auto downGroupBefore = groupOf(ctx, peer(1));
  auto downKeyBefore = keyOf(ctx, peer(1));

  /*
   * Every peer -- including the DOWN one -- gets the same egress policy change,
   * in one update so the live members diverge together. Applying the names
   * drives the re-evaluation itself, so there is nothing to run by hand.
   */
  updatePeersEgressPolicyOnEvb(
      ctx, {peer(0), peer(1), peer(2), peer(3)}, kPolicy1);

  /*
   * All 3 live members diverged together -> the group is rekeyed in place (same
   * object, new policy) and holds exactly those 3 peers.
   */
  EXPECT_EQ(groupOf(ctx, peer(0)), group);
  EXPECT_EQ(group->getGroupKey().egressPolicyName.value_or(""), kPolicy1);
  EXPECT_EQ(group->getMemberCount(), kN - 1);
  EXPECT_TRUE(isMemberOf(ctx, group, peer(0)));
  EXPECT_TRUE(isMemberOf(ctx, group, peer(2)));
  EXPECT_TRUE(isMemberOf(ctx, group, peer(3)));

  /*
   * peer 1 (DOWN) was ignored: not pulled back into the rekeyed group, key
   * never rebuilt, still DOWN.
   */
  EXPECT_FALSE(isMemberOf(ctx, group, peer(1)));
  EXPECT_EQ(groupOf(ctx, peer(1)), downGroupBefore);
  EXPECT_EQ(keyOf(ctx, peer(1)), downKeyBefore);
  EXPECT_EQ(stateOf(ctx, peer(1)), PeerUpdateState::DOWN);

  tearDown(ctx);
}

/*
 * Regression: sessionTerminated must drop a detached peer's buffered RibDumpReq
 * (cancelRibDumpForAdjRib) BEFORE its trailing maybeDestroyUpdateGroups
 * co_await. Otherwise, while suspended draining the destroyed group's async
 * scope, the concurrent handleBufferedRibDumpsForDetachedPeers drain could pull
 * the terminating peer off pendingRibDumpAdjRibs_ and service a torn-down peer.
 *
 * A single peer owns its update group. A buffered RibDumpReq is seeded for it,
 * then a task is parked on the group's async scope. Terminating the peer
 * empties and destroys the group; maybeDestroyUpdateGroups drains (and cancels)
 * the scope, resuming the parked task, which records whether the buffered
 * request is still present at that instant -- which must already be gone.
 */
TEST_F(
    UpdateGroupsEgressReEvalTest,
    BufferedRibDumpRemovedBeforeSessionTerminateDrain) {
  const std::string kPg = "PEERGROUP_1";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;

  auto ctx = setUpGroups({{kPg, 1}}, true, kPolicy0);
  sendInitialRibDump(ctx);
  auto peer0 = makePeerId(0);
  expectEventualStateOnEvb(ctx, peer0, PeerUpdateState::JOINED_RUNNING);

  auto& evb = ctx.peerMgr->getEventBase();
  auto group = groupOf(ctx, peer0);
  ASSERT_NE(group, nullptr);

  folly::coro::Baton baton;
  std::optional<bool> bufferedDuringDrain;
  std::shared_ptr<AdjRib> adjRib;

  /*
   * Seed a buffered detached-peer RibDumpReq for the sole peer, then park a
   * task on its (single-member) group's async scope. folly::coro::Baton is not
   * cancellation-aware, so a cancellation callback posts it: when
   * maybeDestroyUpdateGroups cancels the scope while draining the destroyed
   * group, the callback fires and resumes this task at exactly that point,
   * where it records whether the peer's buffered RibDumpReq is still present.
   */
  evb.runInEventBaseThreadAndWait([&]() {
    adjRib = ctx.peerMgr->adjRibs_.at(peer0);
    ctx.peerMgr->pendingRibDumpAdjRibs_.insert(adjRib);
    group->asyncScope_.add(
        folly::coro::co_withExecutor(
            &evb, folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
              auto token = co_await folly::coro::co_current_cancellation_token;
              folly::CancellationCallback cb(
                  token, [&baton]() noexcept { baton.post(); });
              co_await baton;
              bufferedDuringDrain =
                  ctx.peerMgr->pendingRibDumpAdjRibs_.contains(adjRib);
            })));
  });
  // Ensure the parked task has reached its baton wait before terminating.
  flushEventBase(ctx);

  // Precondition: the buffered dump is present before termination.
  ASSERT_TRUE(folly::via(&evb, [&]() {
                return ctx.peerMgr->pendingRibDumpAdjRibs_.contains(adjRib);
              }).get());

  // Terminate the sole peer through the production event path.
  triggerPeerDownOnEvb(ctx, peer0);

  WITH_RETRIES({
    EXPECT_EVENTUALLY_TRUE(folly::via(&evb, [&]() {
                             return bufferedDuringDrain.has_value();
                           }).get());
  });

  /*
   * The buffered RibDumpReq was removed before sessionTerminated suspended in
   * maybeDestroyUpdateGroups.
   */
  EXPECT_FALSE(*folly::via(&evb, [&]() { return bufferedDuringDrain; }).get());

  tearDown(ctx);
}

/*
 * G1: a peer-group change plus a per-peer override on EVERY member makes the
 * whole group override, so it splits into a new group and the original empties
 * and is destroyed. A reset (unset overrides + reset the peer group to Policy0)
 * then restores the original {PG, Policy0, override=false} group.
 */
TEST_F(
    UpdateGroupsEgressReEvalTest,
    CompositeMembershipReshuffle_AllOverrideGroupDestroyedThenResetRestores) {
  constexpr int kN = 10;
  const std::string kPg = "PEERGROUP_1";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;
  const std::string kPolicy1 = kPNameMatchModifyAppend;

  auto ctx = setUpGroups({{kPg, kN}}, true, kPolicy0);
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  expectEventualStateOnEvb(ctx, peer(0), PeerUpdateState::JOINED_RUNNING);

  auto& evb = ctx.peerMgr->getEventBase();
  auto keyOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroupKey(); })
        .get();
  };
  auto memberCountOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb,
               [&]() {
                 return ctx.adjRibs.at(id)->getUpdateGroup()->getMemberCount();
               })
        .get();
  };
  auto baseline = keyOf(peer(0));

  std::vector<BgpPeerId> peers;
  for (int i = 0; i < kN; ++i) {
    peers.push_back(peer(i));
  }

  // Mutate: peer-group change, then per-peer override on every member.
  disableAsyncEgressReEvalOnEvb(ctx);
  updatePeerGroupEgressPolicyOnEvb(ctx, kPg, kPolicy1);
  for (int i = 0; i < kN; ++i) {
    updatePeerEgressPolicyOnEvb(ctx, peer(i), kPolicy1);
  }
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);

  /*
   * Original group emptied and was destroyed; all members in one override
   * group.
   */
  EXPECT_FALSE(hasUpdateGroupOnEvb(ctx, baseline));
  EXPECT_TRUE(keyOf(peer(0)).peerOverride);
  EXPECT_EQ(memberCountOf(peer(0)), kN);

  // Reset: unset overrides + reset the peer group to Policy0.
  disableAsyncEgressReEvalOnEvb(ctx);
  for (int i = 0; i < kN; ++i) {
    unsetPeerEgressPolicyOnEvb(ctx, peer(i));
  }
  updatePeerGroupEgressPolicyOnEvb(ctx, kPg, kPolicy0);
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);

  // Back to the baseline group, all members restored.
  EXPECT_TRUE(keyOf(peer(0)) == baseline);
  EXPECT_TRUE(hasUpdateGroupOnEvb(ctx, baseline));
  EXPECT_EQ(memberCountOf(peer(0)), kN);

  // All members restored to Policy0's RIB-OUT.
  expectRibOutForPolicy(ctx, peer(0), kPolicy0);

  tearDown(ctx);
}

/*
 * G4: same net outcome as the all-override case, reached via a messier sequence
 * -- a per-peer override, a transient peer-group Policy2, override every
 * member, then peer-group Policy1. The group still ends fully overridden,
 * splits out, and the original is destroyed; reset restores it.
 */
TEST_F(
    UpdateGroupsEgressReEvalTest,
    CompositeMembershipReshuffle_OverrideViaMessySequenceDestroyedThenResetRestores) {
  constexpr int kN = 10;
  const std::string kPg = "PEERGROUP_4";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;
  const std::string kPolicy1 = kPNameMatchModifyAppend;
  const std::string kPolicy2 = kPNamePermitAll;

  auto ctx = setUpGroups({{kPg, kN}}, true, kPolicy0);
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  expectEventualStateOnEvb(ctx, peer(0), PeerUpdateState::JOINED_RUNNING);

  auto& evb = ctx.peerMgr->getEventBase();
  auto keyOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroupKey(); })
        .get();
  };
  auto memberCountOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb,
               [&]() {
                 return ctx.adjRibs.at(id)->getUpdateGroup()->getMemberCount();
               })
        .get();
  };
  auto baseline = keyOf(peer(0));

  std::vector<BgpPeerId> peers;
  for (int i = 0; i < kN; ++i) {
    peers.push_back(peer(i));
  }

  // Mutate: the messy sequence, net = all members overridden on Policy1.
  disableAsyncEgressReEvalOnEvb(ctx);
  updatePeerEgressPolicyOnEvb(ctx, peer(0), kPolicy1);
  updatePeerGroupEgressPolicyOnEvb(ctx, kPg, kPolicy2);
  for (int i = 0; i < kN; ++i) {
    updatePeerEgressPolicyOnEvb(ctx, peer(i), kPolicy1);
  }
  updatePeerGroupEgressPolicyOnEvb(ctx, kPg, kPolicy1);
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);

  EXPECT_FALSE(hasUpdateGroupOnEvb(ctx, baseline));
  EXPECT_TRUE(keyOf(peer(0)).peerOverride);
  EXPECT_EQ(memberCountOf(peer(0)), kN);

  // Reset: unset overrides + reset the peer group to Policy0.
  disableAsyncEgressReEvalOnEvb(ctx);
  for (int i = 0; i < kN; ++i) {
    unsetPeerEgressPolicyOnEvb(ctx, peer(i));
  }
  updatePeerGroupEgressPolicyOnEvb(ctx, kPg, kPolicy0);
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);

  EXPECT_TRUE(keyOf(peer(0)) == baseline);
  EXPECT_TRUE(hasUpdateGroupOnEvb(ctx, baseline));
  EXPECT_EQ(memberCountOf(peer(0)), kN);

  // All members restored to Policy0's RIB-OUT.
  expectRibOutForPolicy(ctx, peer(0), kPolicy0);

  tearDown(ctx);
}

/*
 * G3: a group that is never mutated is left completely alone -- same group
 * object, same key, same members -- even when a re-evaluation runs because a
 * different peer group changed. Two groups are set up; the "changed" one is
 * driven all-override (and destroyed) while the "untouched" one is verified
 * unaffected.
 */
TEST_F(
    UpdateGroupsEgressReEvalTest,
    CompositeMembershipReshuffle_UntouchedGroupUnaffectedByOtherGroupReEval) {
  constexpr int kN = 10;
  const std::string kChanged = "PEERGROUP_1";
  const std::string kUntouched = "PEERGROUP_3";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;
  const std::string kPolicy1 = kPNameMatchModifyAppend;

  // Changed peers -> [0,10), untouched peers -> [10,20).
  auto ctx = setUpGroups({{kChanged, kN}, {kUntouched, kN}}, true, kPolicy0);
  sendInitialRibDump(ctx);
  auto changed = [](int i) { return makePeerId(i); };
  auto untouched = [](int i) { return makePeerId(kN + i); };
  expectEventualStateOnEvb(ctx, changed(0), PeerUpdateState::JOINED_RUNNING);
  expectEventualStateOnEvb(ctx, untouched(0), PeerUpdateState::JOINED_RUNNING);

  auto& evb = ctx.peerMgr->getEventBase();
  auto keyOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroupKey(); })
        .get();
  };
  auto groupOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroup(); })
        .get();
  };
  auto memberCountOf = [&](const BgpPeerId& id) {
    return groupOf(id)->getMemberCount();
  };

  auto untouchedBaseline = keyOf(untouched(0));
  auto untouchedGroupBefore = groupOf(untouched(0));

  std::vector<BgpPeerId> changedPeers;
  for (int i = 0; i < kN; ++i) {
    changedPeers.push_back(changed(i));
  }

  // Drive the changed group all-override so a re-evaluation actually runs.
  disableAsyncEgressReEvalOnEvb(ctx);
  updatePeerGroupEgressPolicyOnEvb(ctx, kChanged, kPolicy1);
  for (int i = 0; i < kN; ++i) {
    updatePeerEgressPolicyOnEvb(ctx, changed(i), kPolicy1);
  }
  markEgressPolicyUpdateRequiredOnEvb(ctx, changedPeers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);

  // The untouched group is completely unaffected: same object, key, members.
  EXPECT_TRUE(keyOf(untouched(0)) == untouchedBaseline);
  EXPECT_TRUE(hasUpdateGroupOnEvb(ctx, untouchedBaseline));
  EXPECT_EQ(groupOf(untouched(0)), untouchedGroupBefore);
  EXPECT_EQ(memberCountOf(untouched(0)), kN);

  /*
   * The untouched group still advertises Policy0; the changed group
   * advertises Policy1.
   */
  expectRibOutForPolicy(ctx, untouched(0), kPolicy0);
  expectRibOutForPolicy(ctx, changed(0), kPolicy1);

  tearDown(ctx);
}

/*
 * G2: a PARTIAL override -- a peer-group change plus per-peer overrides on only
 * SOME members -- splits the group into a non-override half (rekeyed in place)
 * and an override half (split into a new group). Resetting (unset the overrides
 * so the override half falls back to the peer-group egress, then reset the peer
 * group to Policy0) makes BOTH halves converge on the same
 * {PG, Policy0, override=false} key. Both halves see an egress-name change
 * (Policy1 -> Policy0), so both groups are re-evaluated; the reconcile keeps
 * one at the target key and merges the other into it, ending in a single group
 * with all members. (This is the "G2" case the earlier move-only reconcile
 * could not handle -- it overwrote the colliding map slot; see "problem case"
 * notes.)
 */
TEST_F(
    UpdateGroupsEgressReEvalTest,
    CompositeMembershipReshuffle_PartialOverrideResetConvergesToSingleGroup) {
  constexpr int kN = 10;
  constexpr int kOverride = 5;
  const std::string kPg = "PEERGROUP_2";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;
  const std::string kPolicy1 = kPNameMatchModifyAppend;

  auto ctx = setUpGroups({{kPg, kN}}, true, kPolicy0);
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  expectEventualStateOnEvb(ctx, peer(0), PeerUpdateState::JOINED_RUNNING);

  auto& evb = ctx.peerMgr->getEventBase();
  auto keyOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroupKey(); })
        .get();
  };
  auto groupOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroup(); })
        .get();
  };
  auto memberCountOf = [&](const BgpPeerId& id) {
    return groupOf(id)->getMemberCount();
  };
  auto baseline = keyOf(peer(0));

  std::vector<BgpPeerId> peers;
  for (int i = 0; i < kN; ++i) {
    peers.push_back(peer(i));
  }

  /*
   * Reconcile #1: peer-group change to Policy1 + a per-peer override on the
   * first half. The non-override half is re-keyed in place; the override half
   * splits into a new group.
   */
  disableAsyncEgressReEvalOnEvb(ctx);
  updatePeerGroupEgressPolicyOnEvb(ctx, kPg, kPolicy1);
  for (int i = 0; i < kOverride; ++i) {
    updatePeerEgressPolicyOnEvb(ctx, peer(i), kPolicy1);
  }
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);

  /*
   * One peer group is now backed by two update groups: an override half and a
   * non-override half.
   */
  EXPECT_TRUE(keyOf(peer(0)).peerOverride);
  EXPECT_FALSE(keyOf(peer(kOverride)).peerOverride);
  EXPECT_EQ(memberCountOf(peer(0)), kOverride);
  EXPECT_EQ(memberCountOf(peer(kOverride)), kN - kOverride);

  /*
   * Reconcile #2: reset -- unset the overrides (override half falls back to the
   * peer-group egress) and reset the peer group to Policy0. Both halves now
   * want {PG, Policy0, override=false}.
   */
  disableAsyncEgressReEvalOnEvb(ctx);
  for (int i = 0; i < kOverride; ++i) {
    unsetPeerEgressPolicyOnEvb(ctx, peer(i));
  }
  updatePeerGroupEgressPolicyOnEvb(ctx, kPg, kPolicy0);
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);

  /*
   * Correct end state: the two halves merge back into a single group with all
   * members, matching the original baseline key.
   */
  EXPECT_TRUE(keyOf(peer(0)) == baseline);
  EXPECT_TRUE(hasUpdateGroupOnEvb(ctx, baseline));
  EXPECT_EQ(groupOf(peer(0)), groupOf(peer(kOverride)));
  EXPECT_EQ(memberCountOf(peer(0)), kN);
  // Both halves now advertise Policy0's RIB-OUT.
  expectRibOutForPolicy(ctx, peer(0), kPolicy0);
  expectRibOutForPolicy(ctx, peer(kOverride), kPolicy0);

  tearDown(ctx);
}

/*
 * Basic single-transition membership reshuffles between the base group states
 * (a group's key is {PeerGroup, egress, peerOverride}; same PeerGroup below):
 *   - S1: {Policy0, peerOverride=false}
 *   - S2: {Policy1, peerOverride=true}
 *   - S3: {Policy0, peerOverride=false} + {Policy1, peerOverride=true}
 *
 * Each test reaches its start state with one reconcile, then applies exactly
 * one transition and checks the resulting composition. Three of these are
 * merges -- S3 -> S2 (merge to override), S3 -> S1 (merge to non-override), and
 * the k-groups unset-all collapse -- and all merge correctly: the transition
 * changes the ex-override members' egress name, so their group is re-evaluated
 * and the reconcile merges its members into the group already at the target key
 * instead of overwriting it.
 */

/*
 * S3 -> S2 (merge to override): override the non-override half onto the
 * existing override policy so both halves converge on {PG, P1, true}. The
 * ex-non-override members flow through the bucketed move path into the existing
 * override group; the emptied non-override group is destroyed.
 */
TEST_F(
    UpdateGroupsEgressReEvalTest,
    BasicMembershipReshuffle_S3ToS2MergeToOverride) {
  constexpr int kN = 10;
  constexpr int kHalf = 5;
  const std::string kPg = "PEERGROUP_1";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;
  const std::string kPolicy1 = kPNameMatchModifyAppend;

  auto ctx = setUpGroups({{kPg, kN}}, true, kPolicy0);
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  expectEventualStateOnEvb(ctx, peer(0), PeerUpdateState::JOINED_RUNNING);

  auto& evb = ctx.peerMgr->getEventBase();
  auto keyOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroupKey(); })
        .get();
  };
  auto groupOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroup(); })
        .get();
  };
  auto memberCountOf = [&](const BgpPeerId& id) {
    return groupOf(id)->getMemberCount();
  };

  std::vector<BgpPeerId> peers;
  for (int i = 0; i < kN; ++i) {
    peers.push_back(peer(i));
  }

  // Reach S3: override the first half onto P1 (peer-group egress stays P0).
  disableAsyncEgressReEvalOnEvb(ctx);
  for (int i = 0; i < kHalf; ++i) {
    updatePeerEgressPolicyOnEvb(ctx, peer(i), kPolicy1);
  }
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);
  ASSERT_TRUE(keyOf(peer(0)).peerOverride);
  ASSERT_FALSE(keyOf(peer(kHalf)).peerOverride);

  // Transition S3 -> S2: override the non-override half onto the same P1.
  disableAsyncEgressReEvalOnEvb(ctx);
  for (int i = kHalf; i < kN; ++i) {
    updatePeerEgressPolicyOnEvb(ctx, peer(i), kPolicy1);
  }
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);

  // One override group holds everyone; the old non-override group is gone.
  EXPECT_TRUE(keyOf(peer(0)).peerOverride);
  EXPECT_EQ(groupOf(peer(0)), groupOf(peer(kHalf)));
  EXPECT_EQ(memberCountOf(peer(0)), kN);

  // The merged override group advertises Policy1's RIB-OUT.
  expectRibOutForPolicy(ctx, peer(0), kPolicy1);
  expectRibOutForPolicy(ctx, peer(kHalf), kPolicy1);

  tearDown(ctx);
}

/*
 * S3 -> S3, egress converges but the override flag does not: move the
 * peer-group egress onto P1 while the override half is already on P1. The
 * non-override half becomes {PG, P1, false} and the override half stays
 * {PG, P1, true} -- same egress, different override flag -- so it is NOT a
 * merge; the group stays split. (Guards that only the flag+egress pair keys a
 * group.)
 */
TEST_F(
    UpdateGroupsEgressReEvalTest,
    BasicMembershipReshuffle_SameEgressDistinctOverrideStaysSplit) {
  constexpr int kN = 10;
  constexpr int kHalf = 5;
  const std::string kPg = "PEERGROUP_1";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;
  const std::string kPolicy1 = kPNameMatchModifyAppend;

  auto ctx = setUpGroups({{kPg, kN}}, true, kPolicy0);
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  expectEventualStateOnEvb(ctx, peer(0), PeerUpdateState::JOINED_RUNNING);

  auto& evb = ctx.peerMgr->getEventBase();
  auto keyOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroupKey(); })
        .get();
  };
  auto groupOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroup(); })
        .get();
  };
  auto memberCountOf = [&](const BgpPeerId& id) {
    return groupOf(id)->getMemberCount();
  };

  std::vector<BgpPeerId> peers;
  for (int i = 0; i < kN; ++i) {
    peers.push_back(peer(i));
  }

  // Reach S3: override the first half onto P1 (peer-group egress stays P0).
  disableAsyncEgressReEvalOnEvb(ctx);
  for (int i = 0; i < kHalf; ++i) {
    updatePeerEgressPolicyOnEvb(ctx, peer(i), kPolicy1);
  }
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);
  ASSERT_TRUE(keyOf(peer(0)).peerOverride);
  ASSERT_FALSE(keyOf(peer(kHalf)).peerOverride);

  /*
   * Transition: move the peer-group egress to P1 too. The non-override half
   * becomes {PG, P1, false}; the override half stays {PG, P1, true}.
   */
  disableAsyncEgressReEvalOnEvb(ctx);
  updatePeerGroupEgressPolicyOnEvb(ctx, kPg, kPolicy1);
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);

  // Still two distinct groups (same egress, different override flag).
  EXPECT_NE(groupOf(peer(0)), groupOf(peer(kHalf)));
  EXPECT_TRUE(keyOf(peer(0)).peerOverride);
  EXPECT_FALSE(keyOf(peer(kHalf)).peerOverride);
  ASSERT_TRUE(keyOf(peer(0)).egressPolicyName.has_value());
  ASSERT_TRUE(keyOf(peer(kHalf)).egressPolicyName.has_value());
  EXPECT_EQ(*keyOf(peer(0)).egressPolicyName, kPolicy1);
  EXPECT_EQ(*keyOf(peer(kHalf)).egressPolicyName, kPolicy1);
  EXPECT_EQ(memberCountOf(peer(0)), kHalf);
  EXPECT_EQ(memberCountOf(peer(kHalf)), kN - kHalf);

  // Both groups advertise Policy1's RIB-OUT.
  expectRibOutForPolicy(ctx, peer(0), kPolicy1);
  expectRibOutForPolicy(ctx, peer(kHalf), kPolicy1);

  tearDown(ctx);
}

/*
 * S3 -> S1 (merge to non-override): unset the overrides with the peer-group
 * egress unchanged, so the ex-override members fall back from P1 to P0 and both
 * halves want {PG, P0, false}. That P1 -> P0 change re-evaluates the
 * ex-override group; the reconcile merges its members into the non-override
 * group already at {PG, P0, false} rather than overwriting it, ending in one
 * group.
 */
TEST_F(
    UpdateGroupsEgressReEvalTest,
    BasicMembershipReshuffle_S3ToS1MergeSameEgress) {
  constexpr int kN = 10;
  constexpr int kHalf = 5;
  const std::string kPg = "PEERGROUP_1";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;
  const std::string kPolicy1 = kPNameMatchModifyAppend;

  auto ctx = setUpGroups({{kPg, kN}}, true, kPolicy0);
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  expectEventualStateOnEvb(ctx, peer(0), PeerUpdateState::JOINED_RUNNING);

  auto& evb = ctx.peerMgr->getEventBase();
  auto keyOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroupKey(); })
        .get();
  };
  auto groupOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroup(); })
        .get();
  };
  auto memberCountOf = [&](const BgpPeerId& id) {
    return groupOf(id)->getMemberCount();
  };
  auto baseline = keyOf(peer(0));

  std::vector<BgpPeerId> peers;
  for (int i = 0; i < kN; ++i) {
    peers.push_back(peer(i));
  }

  // Reach S3: override the first half onto P1 (peer-group egress stays P0).
  disableAsyncEgressReEvalOnEvb(ctx);
  for (int i = 0; i < kHalf; ++i) {
    updatePeerEgressPolicyOnEvb(ctx, peer(i), kPolicy1);
  }
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);
  ASSERT_TRUE(keyOf(peer(0)).peerOverride);
  ASSERT_FALSE(keyOf(peer(kHalf)).peerOverride);

  // Transition S3 -> S1: unset the overrides; the peer-group egress stays P0.
  disableAsyncEgressReEvalOnEvb(ctx);
  for (int i = 0; i < kHalf; ++i) {
    unsetPeerEgressPolicyOnEvb(ctx, peer(i));
  }
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);

  // Correct end state: one merged non-override group with everyone.
  EXPECT_TRUE(keyOf(peer(0)) == baseline);
  EXPECT_EQ(groupOf(peer(0)), groupOf(peer(kHalf)));
  EXPECT_EQ(memberCountOf(peer(0)), kN);

  // Both halves now advertise Policy0's RIB-OUT.
  expectRibOutForPolicy(ctx, peer(0), kPolicy0);
  expectRibOutForPolicy(ctx, peer(kHalf), kPolicy0);

  tearDown(ctx);
}

/*
 * Same-egress-name move must re-activate the moved detached peers. Override the
 * second half with the SAME egress policy name (flips only the override flag),
 * splitting them into a separate override group with the peers still in sync.
 * Then unset the override so they move back into the original non-override
 * group: that move takes the case-1 branch where the moved peers' source group
 * and the target group share the egress policy name, so no per-peer
 * re-evaluation runs. The move detaches the (in-sync) peers into
 * DETACHED_INIT_DUMP; without an explicit re-activation they would be stranded
 * there (no consume timer, no dump, never rejoin). Assert they instead resume
 * consuming to the end of the change list and advertise the group's policy.
 */
TEST_F(UpdateGroupsEgressReEvalTest, SameEgressMoveReactivatesDetachedPeers) {
  constexpr int kN = 10;
  constexpr int kHalf = 5;
  const std::string kPg = "PEERGROUP_1";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;

  auto ctx = setUpGroups({{kPg, kN}}, true, kPolicy0);
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  expectEventualStateOnEvb(ctx, peer(0), PeerUpdateState::JOINED_RUNNING);

  std::vector<BgpPeerId> peers;
  for (int i = 0; i < kN; ++i) {
    peers.push_back(peer(i));
  }

  /*
   * Split the second half into a separate override group keyed on the SAME
   * egress policy (only the override flag differs).
   */
  disableAsyncEgressReEvalOnEvb(ctx);
  for (int i = kHalf; i < kN; ++i) {
    updatePeerEgressPolicyOnEvb(ctx, peer(i), kPolicy0);
  }
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);
  ASSERT_FALSE(
      folly::via(
          &ctx.peerMgr->getEventBase(),
          [&]() { return ctx.adjRibs.at(peer(0))->getUpdateGroupKey(); })
          .get()
          .peerOverride);
  ASSERT_TRUE(
      folly::via(
          &ctx.peerMgr->getEventBase(),
          [&]() { return ctx.adjRibs.at(peer(kHalf))->getUpdateGroupKey(); })
          .get()
          .peerOverride);

  /*
   * Unset the overrides: the second half moves back into the non-override
   * group, same egress name -> the case-1 same-egress branch (detach + move, no
   * per-peer re-evaluation), which must still re-activate the moved peers'
   * detached processing.
   */
  disableAsyncEgressReEvalOnEvb(ctx);
  for (int i = kHalf; i < kN; ++i) {
    unsetPeerEgressPolicyOnEvb(ctx, peer(i));
  }
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);

  /*
   * Publish fresh change-list entries after the move so a stranded peer (no
   * consume timer) would visibly fail to advance -- an already-caught-up peer
   * is trivially at the end regardless of the fix.
   */
  publishRouteUpdates(ctx, /*isInitialDump=*/false);

  auto group = folly::via(&ctx.peerMgr->getEventBase(), [&]() {
                 return ctx.adjRibs.at(peer(0))->getUpdateGroup();
               }).get();

  /*
   * The moved peers resume consuming and reach the end of the change list --
   * without re-activation they would be stranded in DETACHED_INIT_DUMP and
   * their consumers would never advance past the entries published above.
   */
  expectPeersAtEndOfChangeList(ctx, group);
  expectRibOutForPolicy(ctx, peer(kHalf), kPolicy0);

  tearDown(ctx);
}

/*
 * Unset-all collapse across THREE groups: a peer group split into a
 * non-override group + two distinct override groups (P1, P2). Unsetting every
 * override reverts P1 and P2 back to P0, so all three converge on {PG, P0,
 * false}. Both ex-override groups are re-evaluated (their egress name changed)
 * and the reconcile merges their members into the single non-override group at
 * the target key -- no overwrite; everyone ends in one group.
 */
TEST_F(
    UpdateGroupsEgressReEvalTest,
    BasicMembershipReshuffle_UnsetAllMergesGroups) {
  constexpr int kThird = 5;
  constexpr int kN = 3 * kThird;
  const std::string kPg = "PEERGROUP_1";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;
  const std::string kPolicy1 = kPNameMatchModifyAppend;
  const std::string kPolicy2 = kPNamePermitAll;

  auto ctx = setUpGroups({{kPg, kN}}, true, kPolicy0);
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  expectEventualStateOnEvb(ctx, peer(0), PeerUpdateState::JOINED_RUNNING);

  auto& evb = ctx.peerMgr->getEventBase();
  auto keyOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroupKey(); })
        .get();
  };
  auto groupOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroup(); })
        .get();
  };
  auto memberCountOf = [&](const BgpPeerId& id) {
    return groupOf(id)->getMemberCount();
  };
  auto baseline = keyOf(peer(2 * kThird));

  std::vector<BgpPeerId> peers;
  for (int i = 0; i < kN; ++i) {
    peers.push_back(peer(i));
  }

  /*
   * Reach 3 groups: override [0,kThird) onto P1 and [kThird,2*kThird) onto P2;
   * leave [2*kThird, kN) non-override on P0.
   */
  disableAsyncEgressReEvalOnEvb(ctx);
  for (int i = 0; i < kThird; ++i) {
    updatePeerEgressPolicyOnEvb(ctx, peer(i), kPolicy1);
  }
  for (int i = kThird; i < 2 * kThird; ++i) {
    updatePeerEgressPolicyOnEvb(ctx, peer(i), kPolicy2);
  }
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);
  ASSERT_NE(groupOf(peer(0)), groupOf(peer(kThird)));
  ASSERT_NE(groupOf(peer(0)), groupOf(peer(2 * kThird)));
  ASSERT_NE(groupOf(peer(kThird)), groupOf(peer(2 * kThird)));

  // Transition: unset every override -> all three groups want {PG, P0, false}.
  disableAsyncEgressReEvalOnEvb(ctx);
  for (int i = 0; i < 2 * kThird; ++i) {
    unsetPeerEgressPolicyOnEvb(ctx, peer(i));
  }
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);

  // Correct end state: one merged non-override group with everyone.
  EXPECT_TRUE(keyOf(peer(0)) == baseline);
  EXPECT_EQ(groupOf(peer(0)), groupOf(peer(2 * kThird)));
  EXPECT_EQ(memberCountOf(peer(0)), kN);

  // All members now advertise Policy0's RIB-OUT.
  expectRibOutForPolicy(ctx, peer(0), kPolicy0);
  expectRibOutForPolicy(ctx, peer(2 * kThird), kPolicy0);

  tearDown(ctx);
}

/*
 * Rapid oscillation: repeatedly split off an override half and reset it,
 * cycling the override policy (P1, P2, P1). Every reset must merge back to a
 * single Policy0 group; the final RIB-OUT must be correct.
 */
TEST_F(
    UpdateGroupsEgressReEvalTest,
    CompositeMembershipReshuffle_RapidOscillation) {
  constexpr int kN = 10;
  constexpr int kHalf = 5;
  const std::string kPg = "PEERGROUP_1";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;
  const std::string kPolicy1 = kPNameMatchModifyAppend;
  const std::string kPolicy2 = kPNamePermitAll;

  auto ctx = setUpGroups({{kPg, kN}}, true, kPolicy0);
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  expectEventualStateOnEvb(ctx, peer(0), PeerUpdateState::JOINED_RUNNING);
  auto& evb = ctx.peerMgr->getEventBase();
  auto keyOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroupKey(); })
        .get();
  };
  auto groupOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroup(); })
        .get();
  };
  auto memberCountOf = [&](const BgpPeerId& id) {
    return groupOf(id)->getMemberCount();
  };
  auto baseline = keyOf(peer(0));
  std::vector<BgpPeerId> peers;
  for (int i = 0; i < kN; ++i) {
    peers.push_back(peer(i));
  }

  for (const auto& overridePolicy : {kPolicy1, kPolicy2, kPolicy1}) {
    // Split off the override half.
    disableAsyncEgressReEvalOnEvb(ctx);
    for (int i = 0; i < kHalf; ++i) {
      updatePeerEgressPolicyOnEvb(ctx, peer(i), overridePolicy);
    }
    markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
    runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);
    EXPECT_TRUE(keyOf(peer(0)).peerOverride);
    EXPECT_NE(groupOf(peer(0)), groupOf(peer(kHalf)));

    // Reset: unset the override, merge back to one Policy0 group.
    disableAsyncEgressReEvalOnEvb(ctx);
    for (int i = 0; i < kHalf; ++i) {
      unsetPeerEgressPolicyOnEvb(ctx, peer(i));
    }
    markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
    runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);
    EXPECT_TRUE(keyOf(peer(0)) == baseline);
    EXPECT_EQ(groupOf(peer(0)), groupOf(peer(kHalf)));
    EXPECT_EQ(memberCountOf(peer(0)), kN);
  }

  expectRibOutForPolicy(ctx, peer(0), kPolicy0);
  expectRibOutForPolicy(ctx, peer(kHalf), kPolicy0);

  tearDown(ctx);
}

/*
 * Three-way split, then a partial merge: a non-override group plus two override
 * groups (P1, P2). Unsetting only the P2 third merges it into the non-override
 * group while the P1 override group is left intact.
 */
TEST_F(
    UpdateGroupsEgressReEvalTest,
    CompositeMembershipReshuffle_ThreeWaySplitPartialMerge) {
  constexpr int kThird = 5;
  constexpr int kN = 3 * kThird;
  const std::string kPg = "PEERGROUP_1";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;
  const std::string kPolicy1 = kPNameMatchModifyAppend;
  const std::string kPolicy2 = kPNamePermitAll;

  auto ctx = setUpGroups({{kPg, kN}}, true, kPolicy0);
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  expectEventualStateOnEvb(ctx, peer(0), PeerUpdateState::JOINED_RUNNING);
  auto& evb = ctx.peerMgr->getEventBase();
  auto keyOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroupKey(); })
        .get();
  };
  auto groupOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroup(); })
        .get();
  };
  auto memberCountOf = [&](const BgpPeerId& id) {
    return groupOf(id)->getMemberCount();
  };
  std::vector<BgpPeerId> peers;
  for (int i = 0; i < kN; ++i) {
    peers.push_back(peer(i));
  }

  /*
   * Reach three groups: [0,kThird) override P1, [kThird,2*kThird) override P2,
   * [2*kThird,kN) non-override P0.
   */
  disableAsyncEgressReEvalOnEvb(ctx);
  for (int i = 0; i < kThird; ++i) {
    updatePeerEgressPolicyOnEvb(ctx, peer(i), kPolicy1);
  }
  for (int i = kThird; i < 2 * kThird; ++i) {
    updatePeerEgressPolicyOnEvb(ctx, peer(i), kPolicy2);
  }
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);
  ASSERT_NE(groupOf(peer(0)), groupOf(peer(kThird)));
  ASSERT_NE(groupOf(peer(0)), groupOf(peer(2 * kThird)));
  ASSERT_NE(groupOf(peer(kThird)), groupOf(peer(2 * kThird)));
  expectRibOutForPolicy(ctx, peer(0), kPolicy1);
  expectRibOutForPolicy(ctx, peer(kThird), kPolicy2);
  expectRibOutForPolicy(ctx, peer(2 * kThird), kPolicy0);

  // Unset only the P2 third -> merges into the non-override P0 group.
  disableAsyncEgressReEvalOnEvb(ctx);
  for (int i = kThird; i < 2 * kThird; ++i) {
    unsetPeerEgressPolicyOnEvb(ctx, peer(i));
  }
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);

  // P2 third merged into the non-override group; P1 override group unchanged.
  EXPECT_EQ(groupOf(peer(kThird)), groupOf(peer(2 * kThird)));
  EXPECT_EQ(memberCountOf(peer(2 * kThird)), 2 * kThird);
  EXPECT_TRUE(keyOf(peer(0)).peerOverride);
  EXPECT_EQ(memberCountOf(peer(0)), kThird);
  EXPECT_NE(groupOf(peer(0)), groupOf(peer(2 * kThird)));
  expectRibOutForPolicy(ctx, peer(2 * kThird), kPolicy0);
  expectRibOutForPolicy(ctx, peer(kThird), kPolicy0);
  expectRibOutForPolicy(ctx, peer(0), kPolicy1);

  tearDown(ctx);
}

/*
 * Cross-policy churn: move the peer-group egress across P0 -> P1 -> P2 while a
 * subset overrides, exercising in-place rekey, split,
 * same-egress/different-flag (no merge), and a final unset-driven merge.
 */
TEST_F(
    UpdateGroupsEgressReEvalTest,
    CompositeMembershipReshuffle_CrossPolicyChurn) {
  constexpr int kN = 10;
  constexpr int kHalf = 5;
  const std::string kPg = "PEERGROUP_1";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;
  const std::string kPolicy1 = kPNameMatchModifyAppend;
  const std::string kPolicy2 = kPNamePermitAll;

  auto ctx = setUpGroups({{kPg, kN}}, true, kPolicy0);
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  expectEventualStateOnEvb(ctx, peer(0), PeerUpdateState::JOINED_RUNNING);
  auto& evb = ctx.peerMgr->getEventBase();
  auto keyOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroupKey(); })
        .get();
  };
  auto groupOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroup(); })
        .get();
  };
  auto memberCountOf = [&](const BgpPeerId& id) {
    return groupOf(id)->getMemberCount();
  };
  std::vector<BgpPeerId> peers;
  for (int i = 0; i < kN; ++i) {
    peers.push_back(peer(i));
  }

  // A: peer-group egress P0 -> P1 (all non-override rekey in place).
  disableAsyncEgressReEvalOnEvb(ctx);
  updatePeerGroupEgressPolicyOnEvb(ctx, kPg, kPolicy1);
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);
  EXPECT_FALSE(keyOf(peer(0)).peerOverride);
  EXPECT_EQ(memberCountOf(peer(0)), kN);
  expectRibOutForPolicy(ctx, peer(0), kPolicy1);

  // B: override the first half to P2 -> split.
  disableAsyncEgressReEvalOnEvb(ctx);
  for (int i = 0; i < kHalf; ++i) {
    updatePeerEgressPolicyOnEvb(ctx, peer(i), kPolicy2);
  }
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);
  EXPECT_NE(groupOf(peer(0)), groupOf(peer(kHalf)));
  EXPECT_TRUE(keyOf(peer(0)).peerOverride);
  expectRibOutForPolicy(ctx, peer(0), kPolicy2);
  expectRibOutForPolicy(ctx, peer(kHalf), kPolicy1);

  /*
   * C: peer-group egress P1 -> P2: non-override half becomes {P2,false}; the
   * override half is {P2,true} -- same egress, different flag -> stays split.
   */
  disableAsyncEgressReEvalOnEvb(ctx);
  updatePeerGroupEgressPolicyOnEvb(ctx, kPg, kPolicy2);
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);
  EXPECT_NE(groupOf(peer(0)), groupOf(peer(kHalf)));
  ASSERT_TRUE(keyOf(peer(kHalf)).egressPolicyName.has_value());
  EXPECT_EQ(*keyOf(peer(kHalf)).egressPolicyName, kPolicy2);
  expectRibOutForPolicy(ctx, peer(0), kPolicy2);
  expectRibOutForPolicy(ctx, peer(kHalf), kPolicy2);

  // D: unset overrides -> merge to one {P2,false} group.
  disableAsyncEgressReEvalOnEvb(ctx);
  for (int i = 0; i < kHalf; ++i) {
    unsetPeerEgressPolicyOnEvb(ctx, peer(i));
  }
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);
  EXPECT_EQ(groupOf(peer(0)), groupOf(peer(kHalf)));
  EXPECT_EQ(memberCountOf(peer(0)), kN);
  EXPECT_FALSE(keyOf(peer(0)).peerOverride);
  expectRibOutForPolicy(ctx, peer(0), kPolicy2);

  tearDown(ctx);
}

/*
 * Split-and-remerge: drive S1 -> S3 -> S1 repeatedly with the same override
 * policy, stressing the rekey-vs-merge survivor choice across rounds. Ends as
 * one Policy0 group with correct RIB-OUT.
 */
TEST_F(
    UpdateGroupsEgressReEvalTest,
    CompositeMembershipReshuffle_SplitAndRemerge) {
  constexpr int kN = 10;
  constexpr int kHalf = 5;
  const std::string kPg = "PEERGROUP_1";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;
  const std::string kPolicy1 = kPNameMatchModifyAppend;

  auto ctx = setUpGroups({{kPg, kN}}, true, kPolicy0);
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  expectEventualStateOnEvb(ctx, peer(0), PeerUpdateState::JOINED_RUNNING);
  auto& evb = ctx.peerMgr->getEventBase();
  auto keyOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroupKey(); })
        .get();
  };
  auto groupOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroup(); })
        .get();
  };
  auto memberCountOf = [&](const BgpPeerId& id) {
    return groupOf(id)->getMemberCount();
  };
  auto baseline = keyOf(peer(0));
  std::vector<BgpPeerId> peers;
  for (int i = 0; i < kN; ++i) {
    peers.push_back(peer(i));
  }

  for (int round = 0; round < 3; ++round) {
    // Split: override the first half to P1.
    disableAsyncEgressReEvalOnEvb(ctx);
    for (int i = 0; i < kHalf; ++i) {
      updatePeerEgressPolicyOnEvb(ctx, peer(i), kPolicy1);
    }
    markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
    runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);
    EXPECT_NE(groupOf(peer(0)), groupOf(peer(kHalf)));

    // Remerge: unset -> back to one Policy0 group.
    disableAsyncEgressReEvalOnEvb(ctx);
    for (int i = 0; i < kHalf; ++i) {
      unsetPeerEgressPolicyOnEvb(ctx, peer(i));
    }
    markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
    runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);
    EXPECT_EQ(groupOf(peer(0)), groupOf(peer(kHalf)));
    EXPECT_EQ(memberCountOf(peer(0)), kN);
    EXPECT_TRUE(keyOf(peer(0)) == baseline);
  }

  expectRibOutForPolicy(ctx, peer(0), kPolicy0);
  expectRibOutForPolicy(ctx, peer(kHalf), kPolicy0);

  tearDown(ctx);
}

/*
 * All group shapes reconciled in one pass: four peer groups driven into
 * distinct shapes -- fully overridden (A), partially overridden (B),
 * peer-group rekey (C), and untouched (D) -- then a single reconcile must land
 * each in its correct state with correct RIB-OUT.
 */
TEST_F(
    UpdateGroupsEgressReEvalTest,
    CompositeMembershipReshuffle_AllGroupShapesInOneReeval) {
  constexpr int kN = 10;
  constexpr int kHalf = 5;
  const std::string kAllOvr = "PEERGROUP_A";
  const std::string kPartial = "PEERGROUP_B";
  const std::string kRekey = "PEERGROUP_C";
  const std::string kUntouched = "PEERGROUP_D";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;
  const std::string kPolicy1 = kPNameMatchModifyAppend;
  const std::string kPolicy2 = kPNamePermitAll;

  auto ctx = setUpGroups(
      {{kAllOvr, kN}, {kPartial, kN}, {kRekey, kN}, {kUntouched, kN}},
      true,
      kPolicy0);
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  // Global index bases: A=[0,10), B=[10,20), C=[20,30), D=[30,40).
  auto a = [&](int i) { return peer(i); };
  auto b = [&](int i) { return peer(kN + i); };
  auto c = [&](int i) { return peer(2 * kN + i); };
  auto d = [&](int i) { return peer(3 * kN + i); };
  expectEventualStateOnEvb(ctx, a(0), PeerUpdateState::JOINED_RUNNING);
  expectEventualStateOnEvb(ctx, d(0), PeerUpdateState::JOINED_RUNNING);
  auto& evb = ctx.peerMgr->getEventBase();
  auto keyOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroupKey(); })
        .get();
  };
  auto groupOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroup(); })
        .get();
  };
  auto memberCountOf = [&](const BgpPeerId& id) {
    return groupOf(id)->getMemberCount();
  };
  auto dBaseline = keyOf(d(0));
  std::vector<BgpPeerId> allPeers;
  for (int i = 0; i < 4 * kN; ++i) {
    allPeers.push_back(peer(i));
  }

  // Stage all shapes, then run ONE reconcile.
  disableAsyncEgressReEvalOnEvb(ctx);
  // A: fully overridden to P1.
  for (int i = 0; i < kN; ++i) {
    updatePeerEgressPolicyOnEvb(ctx, a(i), kPolicy1);
  }
  // B: first half overridden to P1 (rest stays P0).
  for (int i = 0; i < kHalf; ++i) {
    updatePeerEgressPolicyOnEvb(ctx, b(i), kPolicy1);
  }
  // C: peer-group egress rekey P0 -> P2.
  updatePeerGroupEgressPolicyOnEvb(ctx, kRekey, kPolicy2);
  // D: untouched.
  markEgressPolicyUpdateRequiredOnEvb(ctx, allPeers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);

  // A: one override group, all members, P1.
  EXPECT_TRUE(keyOf(a(0)).peerOverride);
  EXPECT_EQ(memberCountOf(a(0)), kN);
  expectRibOutForPolicy(ctx, a(0), kPolicy1);
  // B: split -- override half P1, non-override half P0.
  EXPECT_NE(groupOf(b(0)), groupOf(b(kHalf)));
  EXPECT_EQ(memberCountOf(b(0)), kHalf);
  EXPECT_EQ(memberCountOf(b(kHalf)), kN - kHalf);
  expectRibOutForPolicy(ctx, b(0), kPolicy1);
  expectRibOutForPolicy(ctx, b(kHalf), kPolicy0);
  // C: rekeyed in place to P2, all members, non-override.
  EXPECT_FALSE(keyOf(c(0)).peerOverride);
  EXPECT_EQ(memberCountOf(c(0)), kN);
  expectRibOutForPolicy(ctx, c(0), kPolicy2);
  // D: untouched, still P0.
  EXPECT_TRUE(keyOf(d(0)) == dBaseline);
  EXPECT_EQ(memberCountOf(d(0)), kN);
  expectRibOutForPolicy(ctx, d(0), kPolicy0);

  tearDown(ctx);
}

/*
 * Drain/undrain sequence matrix. The six bundle/node drain-undrain orderings
 * from the "Stateful Policy Resolution for BGP++ Drain Workflows" design, each
 * run under all eight ways processUpdateGroupsEgressPolicyReevaluation can pick
 * up the intermediate config changes.
 *
 * The group starts under a filtering (non-permit-all) egress policy, and the
 * initial dump is verified to apply that policy (not a permit-all pass-through)
 * before the sequence runs. The matrix runs at 1000 peers, with the bundle
 * (per-peer) operations acting on the first 25% of them.
 *
 * Each sequence is four operations A B C D with three optional pick-up points
 * between them (after A, B, C); the coroutine ALWAYS runs once more after D.
 * That is 2^3 = 8 pick-up configurations (bit i set == re-evaluate after op i),
 * so 6 sequences x 8 configs = 48 cases. Because the re-evaluation reconciles
 * group membership from the full current config on every run, all eight configs
 * of a sequence converge to the same state -- and all six sequences end fully
 * undrained: one group holding every peer on the undrain policy, with the
 * matching RIB-OUT.
 */
enum class DrainOp { BundleDrain, BundleUndrain, NodeDrain, NodeUndrain };

inline constexpr std::array<std::array<DrainOp, 4>, 6> kDrainSequences = {{
    // Seq 1: Bundle Drain -> Bundle Undrain -> Node Drain -> Node Undrain
    {DrainOp::BundleDrain,
     DrainOp::BundleUndrain,
     DrainOp::NodeDrain,
     DrainOp::NodeUndrain},
    // Seq 2: Bundle Drain -> Node Drain -> Bundle Undrain -> Node Undrain
    {DrainOp::BundleDrain,
     DrainOp::NodeDrain,
     DrainOp::BundleUndrain,
     DrainOp::NodeUndrain},
    // Seq 3: Node Drain -> Node Undrain -> Bundle Drain -> Bundle Undrain
    {DrainOp::NodeDrain,
     DrainOp::NodeUndrain,
     DrainOp::BundleDrain,
     DrainOp::BundleUndrain},
    // Seq 4: Node Drain -> Bundle Drain -> Bundle Undrain -> Node Undrain
    {DrainOp::NodeDrain,
     DrainOp::BundleDrain,
     DrainOp::BundleUndrain,
     DrainOp::NodeUndrain},
    // Seq 5: Bundle Drain -> Node Drain -> Node Undrain -> Bundle Undrain
    {DrainOp::BundleDrain,
     DrainOp::NodeDrain,
     DrainOp::NodeUndrain,
     DrainOp::BundleUndrain},
    // Seq 6: Node Drain -> Bundle Drain -> Node Undrain -> Bundle Undrain
    {DrainOp::NodeDrain,
     DrainOp::BundleDrain,
     DrainOp::NodeUndrain,
     DrainOp::BundleUndrain},
}};

class DrainSequencePickupTest
    : public UpdateGroupPolicyReEvalUTBase,
      public ::testing::WithParamInterface<std::tuple<int, int>> {};

TEST_P(DrainSequencePickupTest, ConvergesRegardlessOfPickupTiming) {
  const int seqIdx = std::get<0>(GetParam());
  const int pickupCfg = std::get<1>(GetParam());
  const std::string kDrain = kPNameMatchNoAdvtDeny;
  const std::string kUndrain = kPNamePermitAll;
  const std::string kPg = "PEERGROUP_A";
  constexpr int kN = 1000;
  // Bundle (per-peer) drain/undrain acts on the first 25% of the group's peers.
  const int kBundleCount = kN * 25 / 100;

  /*
   * Start under a filtering (non-permit-all) egress policy so the initial dump
   * exercises real policy application, not a permit-all pass-through.
   */
  auto ctx = setUpGroups({{kPg, kN}}, /*initialDumpCompleted=*/true, kDrain);
  auto peer = [](int i) { return makePeerId(i); };
  sendInitialRibDump(ctx);
  expectEventualStateOnEvb(ctx, peer(0), PeerUpdateState::JOINED_RUNNING);
  // Initial dump under the drain policy: 50 routes advertised, 50 denied.
  expectRibOutForPolicy(ctx, peer(0), kDrain);

  std::vector<BgpPeerId> peers;
  for (int i = 0; i < kN; ++i) {
    peers.push_back(peer(i));
  }

  auto& evb = ctx.peerMgr->getEventBase();
  auto groupOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroup(); })
        .get();
  };
  auto keyOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroupKey(); })
        .get();
  };
  auto memberCountOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb,
               [&]() {
                 return ctx.adjRibs.at(id)->getUpdateGroup()->getMemberCount();
               })
        .get();
  };

  auto applyOp = [&](DrainOp op) {
    switch (op) {
      case DrainOp::BundleDrain:
        for (int i = 0; i < kBundleCount; ++i) {
          updatePeerEgressPolicyOnEvb(ctx, peer(i), kDrain);
        }
        break;
      case DrainOp::BundleUndrain:
        for (int i = 0; i < kBundleCount; ++i) {
          unsetPeerEgressPolicyOnEvb(ctx, peer(i));
        }
        break;
      case DrainOp::NodeDrain:
        updatePeerGroupEgressPolicyOnEvb(ctx, kPg, kDrain);
        break;
      case DrainOp::NodeUndrain:
        updatePeerGroupEgressPolicyOnEvb(ctx, kPg, kUndrain);
        break;
    }
  };

  const auto& seq = kDrainSequences[seqIdx];
  for (size_t i = 0; i < seq.size(); ++i) {
    /*
     * Re-arm the async re-eval guard before each API call so only our manual
     * pickups drive re-evaluation; a manual pickup clears the guard on exit.
     */
    disableAsyncEgressReEvalOnEvb(ctx);
    applyOp(seq[i]);
    // Pick up after earlier ops per the config bitmask; always after the last.
    if (i == seq.size() - 1 || (pickupCfg & (1 << i)) != 0) {
      markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
      runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);
    }
  }

  /*
   * Every sequence, under every pickup config, ends fully undrained: one group
   * holding all peers on the undrain policy, no per-peer override, matching
   * RIB-OUT.
   */
  for (const auto& p : peers) {
    EXPECT_EQ(groupOf(peer(0)), groupOf(p));
    expectRibOutForPolicy(ctx, p, kUndrain);
  }
  EXPECT_EQ(memberCountOf(peer(0)), static_cast<size_t>(kN));
  EXPECT_FALSE(keyOf(peer(0)).peerOverride);
  ASSERT_TRUE(keyOf(peer(0)).egressPolicyName.has_value());
  EXPECT_EQ(*keyOf(peer(0)).egressPolicyName, kUndrain);

  tearDown(ctx);
}

/*
 * A detached peer rejoins its group with the correct RIB-OUT and WITHOUT any
 * discrepancy collapse -- a clean, version-gated rejoin -- with no policy
 * re-evaluation involved.
 *
 * One permit-all group of 2 peers. peer(1) is detached via real backpressure
 * and driven behind the group's changelist (DETACHED_BLOCKED), while blocker
 * peer(0) freezes the group ahead of it. Draining everyone to the end of the
 * changelist makes the detached peer catch up to the group's rib version before
 * rejoining, so the rejoin collapses zero discrepancies -- the assertion that
 * distinguishes a clean version-gated rejoin from a marker-only rejoin (which
 * would rejoin with stale entries and queue discrepancy corrections).
 */
TEST_F(UpdateGroupsEgressReEvalTest, DetachedPeerRejoinsWithCorrectRibOut) {
  const std::string kPg = "PEERGROUP_1";
  const std::string kPolicy = kPNamePermitAll;

  auto ctx = setUpGroups({{kPg, 2}}, /*initialDumpCompleted=*/true, kPolicy);
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  for (int i = 0; i < 2; ++i) {
    expectEventualStateOnEvb(ctx, peer(i), PeerUpdateState::JOINED_RUNNING);
  }
  auto& evb = ctx.peerMgr->getEventBase();

  // Baseline: the group serves the permit-all RIB-OUT (all 100 advertised).
  expectRibOutForPolicy(ctx, peer(0), kPolicy);

  /*
   * Detach peer(1) via real distribution backpressure and drive the group past
   * it: peer(1) ends up DETACHED_BLOCKED and behind on the changelist, blocker
   * peer(0) is JOINED_BLOCKED so the group is frozen ahead of it.
   */
  triggerDetachedBlockedFromDetachedOnEvb(ctx, peer(1));
  expectEventualStateOnEvb(ctx, peer(1), PeerUpdateState::DETACHED_BLOCKED);

  /*
   * Flap the whole RIB once more (new localPref). The group is frozen (peer(0)
   * blocked), so these entries pile up on the changelist *beyond* the group's
   * marker.
   */
  publishRouteUpdates(ctx, /*isInitialDump=*/false);

  /*
   * Unblock the detached peer FIRST, while the group is still frozen: draining
   * only peer(1) lets it catch up to the group's frozen changelist marker (a
   * marker-only gate would mark it "ready" here, at the group's
   * soon-to-be-stale rib version). Best-effort: stop once it stops making
   * progress or reaches DETACHED_READY_TO_JOIN. peer(0) is left blocked so the
   * group stays frozen.
   */
  for (int round = 0; round < 20; ++round) {
    drainQueue(ctx, peer(1));
    auto st = folly::via(&evb, [&]() {
                return ctx.adjRibs.at(peer(1))->getPeerState();
              }).get();
    if (st == PeerUpdateState::DETACHED_READY_TO_JOIN) {
      break;
    }
    drainOne(ctx, peer(1));
  }

  /*
   * Now unblock the group: draining peer(0) lets the group consume the flap and
   * advance PAST the peer's ready position, so the peer is ready at a stale
   * version. Run everyone to the end of the changelist. A version-gated rejoin
   * re-checks the peer against the advanced group and makes it catch up (zero
   * discrepancies); a marker-only rejoin would accept the stale peer and queue
   * discrepancy corrections.
   */
  bool allSync = false;
  for (int round = 0; round < 300 && !allSync; ++round) {
    auto states = folly::via(&evb, [&]() {
                    std::array<PeerUpdateState, 2> s{};
                    for (int i = 0; i < 2; ++i) {
                      s[i] = ctx.adjRibs.at(peer(i))->getPeerState();
                    }
                    return s;
                  }).get();
    allSync = true;
    for (int i = 0; i < 2; ++i) {
      if (states[i] != PeerUpdateState::JOINED_RUNNING) {
        allSync = false;
        drainQueue(ctx, peer(i));
        drainOne(ctx, peer(i));
      }
    }
  }
  for (int i = 0; i < 2; ++i) {
    expectEventualStateOnEvb(ctx, peer(i), PeerUpdateState::JOINED_RUNNING);
  }

  /*
   * Both peers rejoined the same group in sync with the correct RIB-OUT, and
   * the rejoin was clean: the version-gated rejoin queued zero discrepancy
   * corrections.
   */
  auto result =
      folly::via(&evb, [&]() {
        auto group = ctx.adjRibs.at(peer(1))->getUpdateGroup();
        return std::make_tuple(
            ctx.adjRibs.at(peer(0))->getUpdateGroup().get() == group.get(),
            group->getNumInSyncPeers(),
            group->getTotalDiscrepancies());
      }).get();
  EXPECT_TRUE(std::get<0>(result));
  EXPECT_EQ(std::get<1>(result), 2u);
  EXPECT_EQ(std::get<2>(result), 0);
  expectRibOutForPolicy(ctx, peer(1), kPolicy);

  realignPeerKeysToGroupsOnEvb(ctx);
  tearDown(ctx);
}

INSTANTIATE_TEST_SUITE_P(
    DrainWorkflows,
    DrainSequencePickupTest,
    ::testing::Combine(::testing::Range(0, 6), ::testing::Range(0, 8)),
    [](const ::testing::TestParamInfo<std::tuple<int, int>>& info) {
      return "Seq" + std::to_string(std::get<0>(info.param) + 1) + "Cfg" +
          std::to_string(std::get<1>(info.param));
    });

/*
 * Exercises the destination-side no-sync-peer recovery sweep added to
 * processUpdateGroupsEgressPolicyReevaluation: after all moves, every group in
 * newGroups that has members but zero in-sync peers gets handleNoSyncPeers()
 * called on it. splitToNewGroup only recovers the group it takes peers FROM, so
 * a fresh group that detached movers land in is recovered solely by this sweep.
 *
 * A peer is detached (blocked) so that overriding its egress splits it -- still
 * detached -- into a fresh group that lands with one member and no in-sync
 * peers: the exact state the sweep's guard (members > 0 && in-sync == 0) fires
 * on. The other peer stays in-sync in the source group, so the source is never
 * left without a SYNC peer and only the split destination exercises the sweep.
 * The test verifies the destination ends in the correct recovered state and
 * shutdown is clean.
 */
TEST_F(
    UpdateGroupsEgressReEvalTest,
    SplitDestinationNoSyncPeersIsRecoveredBySweep) {
  constexpr int kN = 2;
  const std::string kPg = "PEERGROUP_1";
  const std::string kPolicy0 = kPNameMatchNoAdvtDeny;
  const std::string kPolicy1 = kPNameMatchModifyAppend;

  auto ctx = setUpGroups({{kPg, kN}}, true, kPolicy0);
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  expectEventualStateOnEvb(ctx, peer(0), PeerUpdateState::JOINED_RUNNING);

  auto& evb = ctx.peerMgr->getEventBase();
  auto groupOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getUpdateGroup(); })
        .get();
  };
  auto numInSyncOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb,
               [&]() {
                 return ctx.adjRibs.at(id)
                     ->getUpdateGroup()
                     ->getNumInSyncPeers();
               })
        .get();
  };
  auto memberCountOf = [&](const BgpPeerId& id) {
    return groupOf(id)->getMemberCount();
  };
  auto stateOf = [&](const BgpPeerId& id) {
    return folly::via(
               &evb, [&]() { return ctx.adjRibs.at(id)->getPeerState(); })
        .get();
  };

  std::vector<BgpPeerId> peers;
  for (int i = 0; i < kN; ++i) {
    peers.push_back(peer(i));
  }

  /*
   * peer(0) -> DETACHED_BLOCKED; peer(1) stays in-sync so the source group is
   * never left without a SYNC peer.
   */
  triggerDetachedBlockedFromJoinedOnEvb(
      ctx, peer(0), /*useBlockFrequency=*/true);
  expectEventualStateOnEvb(ctx, peer(0), PeerUpdateState::DETACHED_BLOCKED);

  /*
   * Override peer(0): its detached membership splits into a fresh group with
   * one member and no in-sync peers -- the state the newGroups sweep must
   * recover.
   */
  disableAsyncEgressReEvalOnEvb(ctx);
  updatePeerEgressPolicyOnEvb(ctx, peer(0), kPolicy1);
  markEgressPolicyUpdateRequiredOnEvb(ctx, peers);
  runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(ctx);

  /*
   * peer(0) split into its own single-member group with no in-sync peers. The
   * sweep ran handleNoSyncPeers on it (guard fired); with nothing promotable
   * while it is still DETACHED_BLOCKED it stays correctly frozen -- zero
   * in-sync peers -- rather than crashing or leaving inconsistent state.
   */
  EXPECT_NE(groupOf(peer(0)), groupOf(peer(1)));
  EXPECT_EQ(memberCountOf(peer(0)), 1);
  EXPECT_EQ(numInSyncOf(peer(0)), 0u);
  EXPECT_EQ(stateOf(peer(0)), PeerUpdateState::DETACHED_BLOCKED);
  // The source group keeps its in-sync peer and is unaffected.
  EXPECT_EQ(numInSyncOf(peer(1)), 1u);

  /*
   * Unblock peer(0): draining its egress queue relieves the backpressure that
   * held it DETACHED_BLOCKED, so it resumes consuming, catches up to the end of
   * the change list, and promotes itself to re-seed its single-member group.
   */
  bool recovered = false;
  for (int round = 0; round < 300 && !recovered; ++round) {
    if (stateOf(peer(0)) == PeerUpdateState::JOINED_RUNNING) {
      recovered = true;
      break;
    }
    drainQueue(ctx, peer(0));
    drainOne(ctx, peer(0));
  }
  expectEventualStateOnEvb(ctx, peer(0), PeerUpdateState::JOINED_RUNNING);
  EXPECT_EQ(numInSyncOf(peer(0)), 1u);

  /*
   * After recovery each peer's RIB-OUT reflects its group's egress policy: the
   * source group's peer keeps kPolicy0; the recovered split-off peer advertises
   * its override kPolicy1.
   */
  expectRibOutForPolicy(ctx, peer(1), kPolicy0);
  expectRibOutForPolicy(ctx, peer(0), kPolicy1);

  realignPeerKeysToGroupsOnEvb(ctx);
  tearDown(ctx);
}

/*
 * Source-side no-sync-peer recovery.
 *
 * Setup -- two groups on the same peer group PG1:
 *   G1 = {P1 JOINED_RUNNING, P2 JOINED_BLOCKED, P3 DETACHED_BLOCKED},
 *        {PG1, permit-all, override=false}   (peers 0,1,2)
 *   G2 = {P4 JOINED_RUNNING, P5 JOINED_BLOCKED, P6 DETACHED_BLOCKED},
 *        {PG1, no-advt-deny, override=true}  (peers 3,4,5)
 *
 * Peers 3,4,5 carry a per-peer no-advt-deny override in the initial config, so
 * G1 and G2 form as two separate update groups at initial group formation (no
 * split). Each group is then driven to {1 running, 1 blocked, 1 detached}.
 * G2's changelist is advanced with extra (selective) publishes before its
 * detach so P6 detaches at a higher rib version than P3 -- G1 is frozen by then
 * (P2 blocked), so those publishes only move G2.
 */
TEST_F(UpdateGroupsEgressReEvalTest, SourceGroupNoSyncPeersIsRecoveredBySweep) {
  const std::string kPg = "PEERGROUP_1";
  const std::string kInherited = kPNamePermitAll;
  const std::string kOverride = kPNameMatchNoAdvtDeny;

  /*
   * G1 = peers 0,1,2 {PG1, permit-all, override=false}. G2 = peers 3,4,5 with a
   * per-peer no-advt-deny override {PG1, no-advt-deny, override=true}. Both
   * form as their own update group at initial group formation -- no split.
   */
  auto ctx = setUpGroups(
      {{kPg, 6}},
      /*initialDumpCompleted=*/true,
      kInherited,
      /*perPeerEgressOverride=*/
      {{3, kOverride}, {4, kOverride}, {5, kOverride}});
  sendInitialRibDump(ctx);
  auto peer = [](int i) { return makePeerId(i); };
  for (int i = 0; i < 6; ++i) {
    expectEventualStateOnEvb(ctx, peer(i), PeerUpdateState::JOINED_RUNNING);
  }
  auto& evb = ctx.peerMgr->getEventBase();

  // Two separate update groups formed directly, each with its own RIB-OUT.
  ASSERT_NE(groupOf(ctx, peer(0)), groupOf(ctx, peer(3)));
  expectRibOutForPolicy(ctx, peer(0), kInherited); // G1: permit-all (100)
  expectRibOutForPolicy(ctx, peer(3), kOverride); // G2: no-advt-deny (50/50)

  /*
   * G1: detach P3 (peer 2) at the dump version, then block the group through
   * P2 (peer 1), leaving P1 (peer 0) JOINED_RUNNING.
   */
  triggerDetachedBlockedFromJoinedOnEvb(
      ctx, peer(2), /*useBlockFrequency=*/true);
  expectEventualStateOnEvb(ctx, peer(2), PeerUpdateState::DETACHED_BLOCKED);
  blockGroupViaPeerOnEvb(ctx, peer(1));
  publishRouteUpdates(ctx, /*isInitialDump=*/false);
  expectEventualStateOnEvb(ctx, peer(1), PeerUpdateState::JOINED_BLOCKED);

  /*
   * G2: advance its changelist (a few routes) so P6 detaches at a higher rib
   * version than P3. G1 is frozen (P2 blocked), so these only move G2.
   */
  publishRouteUpdates(
      ctx, /*isInitialDump=*/false, [](int i) { return i < 5; });
  publishRouteUpdates(
      ctx, /*isInitialDump=*/false, [](int i) { return i < 5; });
  triggerDetachedBlockedFromJoinedOnEvb(
      ctx, peer(5), /*useBlockFrequency=*/true);
  expectEventualStateOnEvb(ctx, peer(5), PeerUpdateState::DETACHED_BLOCKED);
  blockGroupViaPeerOnEvb(ctx, peer(4));
  publishRouteUpdates(ctx, /*isInitialDump=*/false);
  expectEventualStateOnEvb(ctx, peer(4), PeerUpdateState::JOINED_BLOCKED);

  // G1 target state.
  EXPECT_EQ(stateOf(ctx, peer(0)), PeerUpdateState::JOINED_RUNNING);
  EXPECT_EQ(stateOf(ctx, peer(1)), PeerUpdateState::JOINED_BLOCKED);
  EXPECT_EQ(stateOf(ctx, peer(2)), PeerUpdateState::DETACHED_BLOCKED);
  EXPECT_EQ(memberCountOf(ctx, peer(0)), 3);
  EXPECT_EQ(numInSyncOf(ctx, peer(0)), 2u);
  EXPECT_FALSE(keyOf(ctx, peer(0)).peerOverride);
  ASSERT_TRUE(keyOf(ctx, peer(0)).egressPolicyName.has_value());
  EXPECT_EQ(*keyOf(ctx, peer(0)).egressPolicyName, kInherited);

  // G2 target state.
  EXPECT_EQ(stateOf(ctx, peer(3)), PeerUpdateState::JOINED_RUNNING);
  EXPECT_EQ(stateOf(ctx, peer(4)), PeerUpdateState::JOINED_BLOCKED);
  EXPECT_EQ(stateOf(ctx, peer(5)), PeerUpdateState::DETACHED_BLOCKED);
  EXPECT_EQ(memberCountOf(ctx, peer(3)), 3);
  EXPECT_EQ(numInSyncOf(ctx, peer(3)), 2u);
  EXPECT_TRUE(keyOf(ctx, peer(3)).peerOverride);
  ASSERT_TRUE(keyOf(ctx, peer(3)).egressPolicyName.has_value());
  EXPECT_EQ(*keyOf(ctx, peer(3)).egressPolicyName, kOverride);

  // The two detached peers froze at different rib versions.
  auto clInfo =
      folly::via(&evb, [&]() {
        return std::make_tuple(
            ctx.adjRibs.at(peer(0))->getUpdateGroup()->getLastSeenRibVersion(),
            ctx.adjRibs.at(peer(2))->getDetachedRibVersion(),
            ctx.adjRibs.at(peer(3))->getUpdateGroup()->getLastSeenRibVersion(),
            ctx.adjRibs.at(peer(5))->getDetachedRibVersion());
      }).get();
  XLOGF(
      INFO,
      "CL positions -- G1 lsrv={}, P3 detachedRibVersion={}, G2 lsrv={}, "
      "P6 detachedRibVersion={}",
      std::get<0>(clInfo),
      std::get<1>(clInfo),
      std::get<2>(clInfo),
      std::get<3>(clInfo));
  EXPECT_NE(std::get<1>(clInfo), std::get<3>(clInfo));

  /*
   * --- Transition: three policy changes, reconciled by one reeval. ---
   *
   * All three are staged into the config and then applied as a single update,
   * because applying the policy names is what drives the re-evaluation: doing
   * them one at a time would reconcile after each, and the peers would move
   * separately instead of being reconciled together.
   *
   * P1,P2 (G1's joined peers) -> override to policy2: they join G2's key.
   * P4,P5 -> override to policy1: a NEW group G3 (override stickiness keeps
   * them out of G1's inheriting cohort even though the name matches).
   * Peer-group policy -> policy2: only P3 (the sole override=false peer) is
   * affected -> a NEW group G4.
   */
  std::map<std::string, std::map<bgp_policy::DIRECTION, std::string>>
      peersPolicy;
  peersPolicy[peer(0).peerAddr.str()][bgp_policy::DIRECTION::OUT] = kOverride;
  peersPolicy[peer(1).peerAddr.str()][bgp_policy::DIRECTION::OUT] = kOverride;
  peersPolicy[peer(3).peerAddr.str()][bgp_policy::DIRECTION::OUT] = kInherited;
  peersPolicy[peer(4).peerAddr.str()][bgp_policy::DIRECTION::OUT] = kInherited;
  ctx.configMgr->updatePeerPolicies(peersPolicy);

  std::map<std::string, std::map<bgp_policy::DIRECTION, std::string>>
      peerGroupsPolicy;
  peerGroupsPolicy[kPg][bgp_policy::DIRECTION::OUT] = kOverride;
  auto newConfig = ctx.configMgr->updatePeerGroupPolicies(peerGroupsPolicy);

  /*
   * P1 is the only G2-bound peer whose queue was never shrunk, so on the move
   * its ~11-message corrective packing list would flush in a single
   * sendBgpUpdates pass and P1 would rejoin within the reeval turn -- leaving
   * G2 at 1 in-sync. Shrink P1's queue to the tiny blocking size (capacity 3):
   * once its detached processing fills those 3 slots, waitForQueueSpace parks
   * on waitToPush like P2's and P6's, so P1 never reaches the inline rejoin.
   * That forces P1's recovery through the drain loop and makes G2 genuinely sit
   * at zero in-sync peers right after the reeval.
   */
  folly::via(&evb, [&]() {
    resizePeerQueue(
        ctx.adjRibs.at(peer(0)),
        kFillQueueCapacity,
        kFillQueueHighWm,
        kFillQueueLowWm);
  }).get();

  /*
   * Apply the staged config now that P1's queue is shrunk, so the single
   * re-evaluation this kicks off sees the blocking queue size.
   */
  applyResolvedPeerPolicies(
      ctx, newConfig, [](const folly::IPAddress&, const BgpPeerConfig&) {
        return true;
      });
  flushEventBase(ctx);

  /*
   * G3 = {P4, P5}, {PG1, policy1, override=true}. Split into a fresh group,
   * which preserves the joined state, so both stay in-sync.
   */
  EXPECT_EQ(groupOf(ctx, peer(3)), groupOf(ctx, peer(4)));
  EXPECT_EQ(memberCountOf(ctx, peer(3)), 2);
  EXPECT_EQ(numInSyncOf(ctx, peer(3)), 2u);
  EXPECT_TRUE(keyOf(ctx, peer(3)).peerOverride);
  EXPECT_EQ(*keyOf(ctx, peer(3)).egressPolicyName, kInherited);

  /*
   * G2 = {P6, P1, P2}, {PG1, policy2, override=true}. P1,P2 moved into the
   * existing G2 (detached on move); with P6 also detached the group has members
   * but zero in-sync peers -- the source-side no-sync state the sweep recovers.
   */
  EXPECT_EQ(groupOf(ctx, peer(0)), groupOf(ctx, peer(1)));
  EXPECT_EQ(groupOf(ctx, peer(0)), groupOf(ctx, peer(5)));
  EXPECT_EQ(memberCountOf(ctx, peer(0)), 3);
  EXPECT_EQ(numInSyncOf(ctx, peer(0)), 0u);
  EXPECT_TRUE(keyOf(ctx, peer(0)).peerOverride);
  EXPECT_EQ(*keyOf(ctx, peer(0)).egressPolicyName, kOverride);

  // G4 = {P3}, {PG1, policy2, override=false}. Split out of G1 (now emptied).
  EXPECT_EQ(memberCountOf(ctx, peer(2)), 1);
  EXPECT_FALSE(keyOf(ctx, peer(2)).peerOverride);
  EXPECT_EQ(*keyOf(ctx, peer(2)).egressPolicyName, kOverride);
  EXPECT_NE(groupOf(ctx, peer(2)), groupOf(ctx, peer(0)));
  EXPECT_NE(groupOf(ctx, peer(2)), groupOf(ctx, peer(3)));

  /*
   * Unblock everyone and run all three groups to the end of the changelist:
   * drain each peer's queue (releasing low-water backpressure) until every peer
   * is back in sync. A group whose consume timer was never scheduled would
   * stall here and never converge -- so reaching JOINED_RUNNING for all six is
   * the proof the timers were scheduled and the consumers ran to ready.
   */
  XLOG(INFO) << "Start drain loop";
  bool allSync = false;
  for (int round = 0; round < 300 && !allSync; ++round) {
    auto states = folly::via(&evb, [&]() {
                    std::array<PeerUpdateState, 6> s{};
                    for (int i = 0; i < 6; ++i) {
                      s[i] = ctx.adjRibs.at(peer(i))->getPeerState();
                    }
                    return s;
                  }).get();
    allSync = true;
    for (int i = 0; i < 6; ++i) {
      if (states[i] != PeerUpdateState::JOINED_RUNNING) {
        allSync = false;
        /*
         * Drain what is queued now, then block briefly for the next push so
         * the detached consume timer (mraiInterval) can flush the peer's
         * corrective packing list -- relieving egress-queue backpressure --
         * before we re-check. A fixed spin over empty queues would exit
         * before the timer ever fired.
         */
        drainQueue(ctx, peer(i));
        drainOne(ctx, peer(i));
      }
    }
  }
  XLOG(INFO) << "Wait for peers to become JOINED_RUNNING";
  for (int i = 0; i < 6; ++i) {
    expectEventualStateOnEvb(ctx, peer(i), PeerUpdateState::JOINED_RUNNING);
  }

  XLOG(INFO) << "Check all peers in sync";
  // Every peer rejoined its final group in sync.
  EXPECT_EQ(numInSyncOf(ctx, peer(3)), 2u); // G3
  EXPECT_EQ(numInSyncOf(ctx, peer(0)), 3u); // G2
  EXPECT_EQ(numInSyncOf(ctx, peer(2)), 1u); // G4

  XLOG(INFO) << "Check all peers in changelist pos";
  // All three groups consumed to the same end of the changelist.
  auto endVersions =
      folly::via(&evb, [&]() {
        return std::make_tuple(
            ctx.adjRibs.at(peer(3))->getUpdateGroup()->getLastSeenRibVersion(),
            ctx.adjRibs.at(peer(0))->getUpdateGroup()->getLastSeenRibVersion(),
            ctx.adjRibs.at(peer(2))->getUpdateGroup()->getLastSeenRibVersion());
      }).get();
  EXPECT_EQ(std::get<0>(endVersions), std::get<1>(endVersions));
  EXPECT_EQ(std::get<0>(endVersions), std::get<2>(endVersions));

  XLOG(INFO) << "Check RIB-OUTs";
  // Every peer's RIB-OUT reflects its final group's egress policy.
  expectRibOutForPolicy(ctx, peer(3), kInherited); // G3 permit-all: all 100
  expectRibOutForPolicy(ctx, peer(4), kInherited); // G3 permit-all: all 100
  expectRibOutForPolicy(ctx, peer(0), kOverride); // G2 no-advt-deny: 50/50
  expectRibOutForPolicy(ctx, peer(1), kOverride); // G2 no-advt-deny: 50/50
  expectRibOutForPolicy(ctx, peer(5), kOverride); // G2 no-advt-deny: 50/50
  expectRibOutForPolicy(ctx, peer(2), kOverride); // G4 no-advt-deny: 50/50

  realignPeerKeysToGroupsOnEvb(ctx);
  XLOG(INFO) << "Start tearDown";
  tearDown(ctx);
}

} // namespace facebook::bgp
