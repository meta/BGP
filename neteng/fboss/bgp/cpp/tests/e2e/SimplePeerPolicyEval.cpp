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
 * E2E tests: Update Group SINGLE-PEER (per-peer override) egress policy
 * re-evaluation.
 *
 * kNumPeers peers (peer3..peer12) share one update group with NO egress policy
 * applied, so every route is propagated to every member (propagate-everything).
 * Peers are numbered from 3 (not 1) because this suite's shared addressing
 * scheme (tests/Utils.h) reserves 127.1.0.x for the LOCAL BGP speaker
 * (kLocalAddr1) and starts remote peers at kPeerAddr3 = 127.3.0.1, running
 * 127.(3+i).0.1 through kPeerAddr12 — the peer set that the reusable
 * setupNPeersInGroupJoined helper and every sibling update-group test are built
 * on. (kPeerAddr1/kPeerAddr2 are 1.1.1.1/2.2.2.2, unrelated peers with no group
 * nexthop/spec plumbing.)
 * Only the SUBJECT peer (peer3) then gets a per-peer egress override via the
 * co_setPeersPolicy thrift API. Because a per-peer override changes only that
 * peer's UpdateGroupKey, the group re-evaluation SPLITS the subject out of the
 * shared group into its own new group and re-walks it under the new policy,
 * while the other peers stay behind together in the original group, unchanged.
 *
 * Each test drives the subject to a different PeerUpdateState before the policy
 * change and asserts the split + re-eval is correct for that state:
 *   PeerPolicyChangeJoinedRunningPeerSplit:      subject JOINED_RUNNING.
 *   PeerPolicyChangeJoinedBlockedPeerSplit:      subject JOINED_BLOCKED.
 *   PeerPolicyChangeDetachedReadyToJoinPeerSplit:subject DETACHED_READY_TO_JOIN
 *                                                (pinned via defer hook).
 *   PeerPolicyChangeDetachedBlockedPeerSplit:    subject DETACHED_BLOCKED.
 *
 * DETACHED_RUNNING is intentionally not covered — it is a transient state that
 * cannot be deterministically captured in an E2E.
 *
 * The policy change is always propagate-everything -> match-modify-append: the
 * subject's split group re-advertises every route, appending kCommAppend to the
 * i % 3 == 0 routes. The subject's RIB-OUT reflects the new policy
 * synchronously inside the re-eval turn (in-sync peers via the group walk,
 * detached peers via an inline per-peer dump), so RIB-OUT is asserted in every
 * state regardless of whether the peer can currently send. Wire-level delivery
 * is additionally verified for JOINED_RUNNING (the state that drains cleanly
 * end-to-end).
 *
 * All cases run in both serialization modes.
 *
 * Prefix range: 36.0.x.0/24 (community-tagged scale routes, shared with the
 * group-level suite's format so routeIndexFromPrefix works).
 */

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupPolicyReEvalE2ECommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/*
 * Single-peer (per-peer egress override) policy re-evaluation fixture. Subject
 * peer is always peer3; the remaining kNumPeers-1 peers are the group members
 * left behind by the split.
 */
class SinglePeerPolicyReEvalE2ETest : public UpdateGroupPolicyReEvalE2EBase {
 protected:
  /*
   * Apply a match-modify-append per-peer egress override to the subject (peer3)
   * via co_setPeersPolicy and wait for the scheduled group re-evaluation to
   * run.
   */
  void applyAppendOverrideToSubject() {
    auto result = setPeerPolicy(kPeerAddr3.str(), kMatchModifyAppendPolicyName);
    EXPECT_EQ(
        result,
        neteng::fboss::bgp::thrift::BgpPolicyChangeResult::POLICIES_APPLIED);
  }

  /*
   * Assert the subject split into its own single-member group while the other
   * peers stayed together in the original group.
   *
   * co_setPeersPolicy only stages config and schedules the split re-evaluation
   * asynchronously, so poll for the split OUTCOME (peer3 observed in a new
   * group) rather than a flag: the scheduled flag is unreliable to gate on
   * because the re-eval can be scheduled and completed between polls (seen on
   * the fast DETACHED_INIT_DUMP re-eval).
   */
  void expectSubjectSplitFromGroup(
      const std::shared_ptr<AdjRibOutGroup>& originalGroup,
      const std::vector<folly::IPAddress>& otherAddrs) {
    WITH_RETRIES_N(60, {
      auto group = getUpdateGroupForPeer(kPeerAddr3);
      EXPECT_EVENTUALLY_TRUE(group != nullptr && group != originalGroup);
    });
    auto subjectGroup = getUpdateGroupForPeer(kPeerAddr3);
    ASSERT_NE(subjectGroup, nullptr);
    EXPECT_NE(subjectGroup, originalGroup)
        << "subject peer did not split into a new update group";
    EXPECT_EQ(getGroupMemberCount(kPeerAddr3), 1u);

    for (const auto& other : otherAddrs) {
      EXPECT_EQ(getUpdateGroupForPeer(other), originalGroup)
          << "non-subject peer unexpectedly moved groups";
    }
    EXPECT_EQ(getGroupMemberCount(otherAddrs.front()), otherAddrs.size());
  }

  /*
   * Assert the subject's RIB-OUT follows the append policy (all routes
   * advertised, kCommAppend on the i % 3 == 0 routes) while the other peers
   * remain on propagate-everything (all routes advertised, unmodified).
   */
  void expectSubjectAppendOthersPropagateEverything(
      const std::vector<folly::IPAddress>& otherAddrs,
      int count) {
    waitForRibOutAdvertisedCount(kPeerAddr3, count);
    expectRibOutForPolicy(kPeerAddr3, kMatchModifyAppendPolicyName, count);
    for (const auto& other : otherAddrs) {
      waitForRibOutAdvertisedCount(other, count);
      expectRibOutForPolicy(other, kPermitAllPolicyName, count);
    }
  }

  /*
   * Block peer3 and inject the community-tagged routes so its tiny queue fills
   * and it detaches (DETACHED_BLOCKED, slow-peer block-count threshold 1). The
   * other peers receive and are drained. Returns the injected prefixes via
   * out-param.
   */
  void detachSubjectBlockedWithCommunityRoutes(
      const std::vector<BgpPeerId>& otherIds,
      std::vector<folly::CIDRNetwork>* injectedOut) {
    BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
    setSlowPeerThresholds(
        kPeerAddr3,
        std::chrono::milliseconds(600000),
        1,
        std::chrono::milliseconds(60000));
    blockPeer(kPeerAddr3);
    injectCommunityTaggedRoutes(kNumRoutes, injectedOut);
    for (const auto& otherId : otherIds) {
      recordDrainedRoutes(otherId);
    }
    EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
    ASSERT_TRUE(
        waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));
    ASSERT_TRUE(isPeerDetached(kPeerAddr3));
  }

  /*
   * Verify the routes each peer actually received on the wire: the subject
   * (peer3) got every route with kCommAppend on the i % 3 == 0 routes, and
   * every other peer still holds the full propagate-everything set (no appended
   * community). Drains the subject a few extra rounds first so the split
   * group's re-advertisements are fully captured once the peer is back at
   * JOINED_RUNNING; re-drains each other peer to capture anything flushed after
   * the split.
   */
  void expectFullWirePerPeer(
      const BgpPeerId& subjectId,
      const std::vector<BgpPeerId>& otherIds,
      const std::vector<folly::CIDRNetwork>& injectedPrefixes) {
    for (int r = 0; r < 10; ++r) {
      peerManager_->getEventBase().runInEventBaseThreadAndWait([]() {});
      recordDrainedRoutes(subjectId);
    }
    expectReceivedRoutesForPolicy(
        subjectId, kMatchModifyAppendPolicyName, injectedPrefixes);
    for (const auto& otherId : otherIds) {
      recordDrainedRoutes(otherId);
      expectReceivedRoutesForPolicy(
          otherId, kPermitAllPolicyName, injectedPrefixes);
    }
  }

  /* Split peerIds (subject first) into the subject id and the other peers. */
  static void splitSubjectAndOthers(
      const std::vector<BgpPeerId>& peerIds,
      BgpPeerId& subjectIdOut,
      std::vector<BgpPeerId>& otherIdsOut,
      std::vector<folly::IPAddress>& otherAddrsOut) {
    subjectIdOut = peerIds.front();
    for (size_t i = 1; i < peerIds.size(); ++i) {
      otherIdsOut.push_back(peerIds[i]);
      otherAddrsOut.push_back(peerIds[i].peerAddr);
    }
  }
};

/*
 * Subject JOINED_RUNNING when the per-peer override is applied.
 *
 * peer3 is a healthy in-sync member of the 10-peer group. The append override
 * splits it into its own group, re-walked under the new policy: it
 * re-advertises every route with the appended community on the i % 3 == 0
 * routes, both in RIB-OUT and on the wire. The other peers are untouched (still
 * propagate-everything, same group).
 */
TEST_P(SinglePeerPolicyReEvalE2ETest, PeerPolicyChangeJoinedRunningPeerSplit) {
  XLOGF(INFO, "=== TEST: PeerPolicyChangeJoinedRunningPeerSplit ===");

  setupPolicies();
  auto peerIds = setupNPeersInGroupJoined(
      kNumPeers, /*queueCapacity=*/10, /*queueHighWm=*/8, /*queueLowWm=*/2);
  BgpPeerId subjectId;
  std::vector<BgpPeerId> otherIds;
  std::vector<folly::IPAddress> otherAddrs;
  splitSubjectAndOthers(peerIds, subjectId, otherIds, otherAddrs);

  auto originalGroup = getUpdateGroupForPeer(kPeerAddr3);
  ASSERT_NE(originalGroup, nullptr);
  EXPECT_EQ(getGroupMemberCount(kPeerAddr3), static_cast<size_t>(kNumPeers));

  /* Propagate-everything baseline: all routes advertised to every member. */
  std::vector<folly::CIDRNetwork> injectedPrefixes;
  injectCommunityTaggedRoutes(kNumRoutes, &injectedPrefixes);
  for (const auto& peerId : peerIds) {
    recordDrainedRoutes(peerId);
  }
  for (const auto& peerId : peerIds) {
    waitForRibOutAdvertisedCount(peerId.peerAddr, kNumRoutes);
    expectRibOutForPolicy(peerId.peerAddr, kPermitAllPolicyName, kNumRoutes);
  }

  /* Per-peer append override on peer3 -> split + re-eval under append. */
  applyAppendOverrideToSubject();
  expectSubjectSplitFromGroup(originalGroup, otherAddrs);
  expectSubjectAppendOthersPropagateEverything(otherAddrs, kNumRoutes);

  /* Wire: peer3 re-received every route (append community on every 3rd); the
   * other peers still hold the propagate-everything set, no appended community.
   */
  expectFullWirePerPeer(subjectId, otherIds, injectedPrefixes);

  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  EXPECT_TRUE(isPeerInSync(kPeerAddr3));
  verifySlowPeerInvariants(kPeerAddr3);

  XLOGF(INFO, "=== TEST PASSED: PeerPolicyChangeJoinedRunningPeerSplit ===");
}

/*
 * Subject JOINED_BLOCKED when the per-peer override is applied.
 *
 * peer3's tiny queue is filled past the high watermark so it is JOINED_BLOCKED
 * but still an in-sync member of the 10-peer group. The append override splits
 * it into its own group (carrying its in-sync + blocked state) and re-walks the
 * group-owned RIB-OUT under the new policy synchronously, so its RIB-OUT
 * already reflects append before it can send. Once unblocked it converges back
 * to JOINED_RUNNING. The other peers are untouched.
 */
TEST_P(SinglePeerPolicyReEvalE2ETest, PeerPolicyChangeJoinedBlockedPeerSplit) {
  XLOGF(INFO, "=== TEST: PeerPolicyChangeJoinedBlockedPeerSplit ===");

  setupPolicies();
  /* peer3 gets a tiny queue so it blocks under load; the others stay large. */
  setQueueSizeForPeer(kPeerAddr3, /*capacity=*/3, /*highWm=*/2, /*lowWm=*/0);
  auto peerIds = setupNPeersInGroupJoined(
      kNumPeers, /*queueCapacity=*/10, /*queueHighWm=*/8, /*queueLowWm=*/2);
  BgpPeerId subjectId;
  std::vector<BgpPeerId> otherIds;
  std::vector<folly::IPAddress> otherAddrs;
  splitSubjectAndOthers(peerIds, subjectId, otherIds, otherAddrs);

  auto originalGroup = getUpdateGroupForPeer(kPeerAddr3);
  ASSERT_NE(originalGroup, nullptr);
  EXPECT_EQ(getGroupMemberCount(kPeerAddr3), static_cast<size_t>(kNumPeers));

  /* High slow-peer thresholds so peer3 stays JOINED_BLOCKED, never detached. */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1000000,
      std::chrono::milliseconds(600000));

  /*
   * Block peer3, then inject the routes: the other peers receive them (drained)
   * while peer3's queue fills past the high watermark -> JOINED_BLOCKED, still
   * in sync.
   */
  blockPeer(kPeerAddr3);
  std::vector<folly::CIDRNetwork> injectedPrefixes;
  injectCommunityTaggedRoutes(kNumRoutes, &injectedPrefixes);
  for (const auto& peerId : otherIds) {
    recordDrainedRoutes(peerId);
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(subjectId));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_BLOCKED));
  EXPECT_TRUE(isPeerInSync(kPeerAddr3));
  EXPECT_FALSE(isPeerDetached(kPeerAddr3));

  /*
   * Per-peer append override while peer3 is blocked -> split + synchronous
   * RIB-OUT re-eval. The re-evaluated RIB-OUT (the authoritative output of the
   * re-eval) carries the appended community per route even though a blocked
   * peer cannot send until it is unblocked.
   */
  applyAppendOverrideToSubject();
  expectSubjectSplitFromGroup(originalGroup, otherAddrs);
  expectSubjectAppendOthersPropagateEverything(otherAddrs, kNumRoutes);

  /*
   * Unblock peer3: it drains and converges back to JOINED_RUNNING, in sync, in
   * its own split group.
   */
  unblockAndDrainToJoined(kPeerAddr3, subjectId);
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  EXPECT_TRUE(isPeerInSync(kPeerAddr3));
  /*
   * Wire: peer3 delivered the re-evaluated append routes once unblocked; the
   * other peers still hold the full propagate-everything set. (While peer3 was
   * JOINED_BLOCKED it head-of-line-blocked the shared group's send, so the
   * other peers only received a partial set at inject time;
   * expectFullWirePerPeer re-drains them to capture the remainder the group
   * flushed after the split.)
   */
  expectFullWirePerPeer(subjectId, otherIds, injectedPrefixes);
  verifySlowPeerInvariants(kPeerAddr3);

  XLOGF(INFO, "=== TEST PASSED: PeerPolicyChangeJoinedBlockedPeerSplit ===");
}

/*
 * Subject DETACHED_READY_TO_JOIN when the per-peer override is applied.
 *
 * peer3 is detached via slow-peer backpressure, then driven to
 * DETACHED_READY_TO_JOIN with acceptance deferred so it stays pinned there. The
 * append override splits it out of the 10-peer group into its own group and
 * re-evaluates its materialized RIB-OUT inline under the new policy, so its
 * RIB-OUT reflects append while it remains pinned at DRJ and healthy. The other
 * peers are untouched. Releasing the defer leaves peer3 not DOWN.
 */
TEST_P(
    SinglePeerPolicyReEvalE2ETest,
    PeerPolicyChangeDetachedReadyToJoinPeerSplit) {
  XLOGF(INFO, "=== TEST: PeerPolicyChangeDetachedReadyToJoinPeerSplit ===");

  setupPolicies();
  setQueueSizeForPeer(kPeerAddr3, /*capacity=*/3, /*highWm=*/2, /*lowWm=*/0);
  auto peerIds = setupNPeersInGroupJoined(
      kNumPeers, /*queueCapacity=*/10, /*queueHighWm=*/8, /*queueLowWm=*/2);
  BgpPeerId subjectId;
  std::vector<BgpPeerId> otherIds;
  std::vector<folly::IPAddress> otherAddrs;
  splitSubjectAndOthers(peerIds, subjectId, otherIds, otherAddrs);

  auto originalGroup = getUpdateGroupForPeer(kPeerAddr3);
  ASSERT_NE(originalGroup, nullptr);
  EXPECT_EQ(getGroupMemberCount(kPeerAddr3), static_cast<size_t>(kNumPeers));

  /* Detach peer3 (DETACHED_BLOCKED) off the community-tagged dump. */
  std::vector<folly::CIDRNetwork> injectedPrefixes;
  detachSubjectBlockedWithCommunityRoutes(otherIds, &injectedPrefixes);

  /* Drive peer3 to DETACHED_READY_TO_JOIN with acceptance deferred (pinned).
   * Record (not discard) what it receives while draining, so its
   * received-routes map reflects the deliveries a production peer would keep
   * during recovery. */
  testOnlyDeferDrjAcceptance(kPeerAddr3, true);
  unblockPeer(kPeerAddr3, /*maxRetries=*/0);
  /*
   * Drain peer3's queue while it drives itself to DETACHED_READY_TO_JOIN,
   * recording (not discarding) the deliveries a production peer would keep.
   *
   * This polls (via WITH_RETRIES_N, the fixture's standard async-test wait --
   * same primitive as waitForPeerState, not a raw sleep) rather than waiting on
   * an event/baton, for two reasons specific to this transition:
   *   1. DRJ is not transient here: testOnlyDeferDrjAcceptance(true) pins peer3
   *      at DETACHED_READY_TO_JOIN until released below, so any poll after it
   *      arrives observes it -- sampling cannot miss the state.
   *   2. The peer only advances to DRJ as its egress queue is drained, so the
   *      wait must actively pump recordDrainedRoutes each iteration; a passive
   *      baton/condition-variable wait cannot drive that draining, and no
   *      production-side signal is emitted on this transition to wait on.
   */
  WITH_RETRIES_N(60, {
    recordDrainedRoutes(subjectId, 1, 100);
    EXPECT_EVENTUALLY_EQ(
        getPeerState(kPeerAddr3), PeerUpdateState::DETACHED_READY_TO_JOIN);
  });

  /* Per-peer append override while peer3 is pinned at DRJ -> split + inline
   * per-peer RIB-OUT re-eval under append. */
  applyAppendOverrideToSubject();
  expectSubjectSplitFromGroup(originalGroup, otherAddrs);
  expectSubjectAppendOthersPropagateEverything(otherAddrs, kNumRoutes);

  /* peer3 stays pinned at DRJ, detached and healthy (not DOWN). */
  EXPECT_TRUE(isPeerDetached(kPeerAddr3));
  EXPECT_NE(getPeerState(kPeerAddr3), PeerUpdateState::DOWN);

  /* Release acceptance: peer3 rejoins and remains healthy. */
  testOnlyDeferDrjAcceptance(kPeerAddr3, false);
  EXPECT_NE(getPeerState(kPeerAddr3), PeerUpdateState::DOWN);

  /*
   * Recover peer3 to JOINED_RUNNING and verify the append routes reach the
   * wire; the other peers still hold the full propagate-everything set.
   */
  unblockAndDrainToJoined(kPeerAddr3, subjectId);
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  EXPECT_TRUE(isPeerInSync(kPeerAddr3));
  expectFullWirePerPeer(subjectId, otherIds, injectedPrefixes);
  verifySlowPeerInvariants(kPeerAddr3);

  XLOGF(
      INFO,
      "=== TEST PASSED: PeerPolicyChangeDetachedReadyToJoinPeerSplit ===");
}

/*
 * Subject DETACHED_BLOCKED when the per-peer override is applied.
 *
 * peer3 is detached via frequency-based slow-peer detection (block-count
 * threshold 1) off the community-tagged dump. The append override splits it out
 * of the 10-peer group into its own group and re-evaluates its materialized
 * RIB-OUT inline under the new policy, so its RIB-OUT reflects append even
 * though a DETACHED_BLOCKED peer is not re-activated (it waits for unblock
 * before sending). The other peers are untouched; peer3 stays detached and
 * healthy.
 */
TEST_P(
    SinglePeerPolicyReEvalE2ETest,
    PeerPolicyChangeDetachedBlockedPeerSplit) {
  XLOGF(INFO, "=== TEST: PeerPolicyChangeDetachedBlockedPeerSplit ===");

  setupPolicies();
  setQueueSizeForPeer(kPeerAddr3, /*capacity=*/3, /*highWm=*/2, /*lowWm=*/0);
  auto peerIds = setupNPeersInGroupJoined(
      kNumPeers, /*queueCapacity=*/10, /*queueHighWm=*/8, /*queueLowWm=*/2);
  BgpPeerId subjectId;
  std::vector<BgpPeerId> otherIds;
  std::vector<folly::IPAddress> otherAddrs;
  splitSubjectAndOthers(peerIds, subjectId, otherIds, otherAddrs);

  auto originalGroup = getUpdateGroupForPeer(kPeerAddr3);
  ASSERT_NE(originalGroup, nullptr);
  EXPECT_EQ(getGroupMemberCount(kPeerAddr3), static_cast<size_t>(kNumPeers));

  /* Detach peer3 (DETACHED_BLOCKED) off the community-tagged dump. */
  std::vector<folly::CIDRNetwork> injectedPrefixes;
  detachSubjectBlockedWithCommunityRoutes(otherIds, &injectedPrefixes);

  /* Per-peer append override while peer3 is DETACHED_BLOCKED -> split + inline
   * per-peer RIB-OUT re-eval under append. */
  applyAppendOverrideToSubject();
  expectSubjectSplitFromGroup(originalGroup, otherAddrs);
  expectSubjectAppendOthersPropagateEverything(otherAddrs, kNumRoutes);

  /* peer3 stays detached and healthy (not DOWN); the other peers stay in sync.
   */
  EXPECT_TRUE(isPeerDetached(kPeerAddr3));
  EXPECT_NE(getPeerState(kPeerAddr3), PeerUpdateState::DOWN);
  for (const auto& other : otherAddrs) {
    EXPECT_TRUE(isPeerInSync(other));
  }

  /*
   * Recover peer3 to JOINED_RUNNING and verify the append routes reach the
   * wire; the other peers still hold the full propagate-everything set.
   */
  unblockAndDrainToJoined(kPeerAddr3, subjectId);
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  EXPECT_TRUE(isPeerInSync(kPeerAddr3));
  expectFullWirePerPeer(subjectId, otherIds, injectedPrefixes);
  verifySlowPeerInvariants(kPeerAddr3);

  XLOGF(INFO, "=== TEST PASSED: PeerPolicyChangeDetachedBlockedPeerSplit ===");
}

/*
 * Subject DETACHED_INIT_DUMP when the per-peer override is applied.
 *
 * peer3 joins long after the group is established and its routes are injected,
 * and its initial dump is deferred (testOnlyDeferInitDump) so it stays pinned
 * in DETACHED_INIT_DUMP. The append override is applied while peer3 is still
 * pinned; the re-eval splits peer3 into its own group and cancels its deferred
 * dump, re-evaluating it inline under the new policy -- so peer3's RIB-OUT
 * reflects append and it stays healthy (not DOWN), while the other peers stay
 * on propagate-everything.
 *
 * Unlike the DETACHED_READY_TO_JOIN and DETACHED_BLOCKED cases, nothing holds
 * peer3 detached past the re-eval: the deferred dump IS the pin, and the
 * re-eval cancels it. The inline dump leaves peer3 with a materialized RIB-OUT
 * at the group's RIB version and a registered detached consumer, so it drains
 * and rejoins its own split group. That dump also delivers peer3's full
 * re-evaluated set to its egress queue, so its on-wire delivery is verified by
 * draining directly rather than via the JOINED_RUNNING-gated helper the other
 * cases use.
 */
TEST_P(
    SinglePeerPolicyReEvalE2ETest,
    PeerPolicyChangeDetachedInitDumpPeerSplit) {
  XLOGF(INFO, "=== TEST: PeerPolicyChangeDetachedInitDumpPeerSplit ===");

  setupPolicies();
  auto peerIds = setupNPeersInGroupJoined(
      kNumPeers, /*queueCapacity=*/10, /*queueHighWm=*/8, /*queueLowWm=*/2);
  BgpPeerId subjectId;
  std::vector<BgpPeerId> otherIds;
  std::vector<folly::IPAddress> otherAddrs;
  splitSubjectAndOthers(peerIds, subjectId, otherIds, otherAddrs);

  auto originalGroup = getUpdateGroupForPeer(kPeerAddr3);
  ASSERT_NE(originalGroup, nullptr);
  EXPECT_EQ(getGroupMemberCount(kPeerAddr3), static_cast<size_t>(kNumPeers));

  /* Propagate-everything baseline: every peer receives the routes. */
  std::vector<folly::CIDRNetwork> injectedPrefixes;
  injectCommunityTaggedRoutes(kNumRoutes, &injectedPrefixes);
  for (const auto& peerId : peerIds) {
    recordDrainedRoutes(peerId);
  }

  /*
   * Bring peer3 up long after the group: reconnect it with its initial dump
   * deferred so it is pinned in DETACHED_INIT_DUMP.
   */
  bringDownPeer(kPeerAddr3);
  unblockPeer(kPeerAddr3);
  testOnlyDeferInitDump(kPeerAddr3, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(subjectId);
  ASSERT_TRUE(waitForPeerState(
      kPeerAddr3, PeerUpdateState::DETACHED_INIT_DUMP, /*maxRetries=*/30));

  /*
   * Apply the append override while peer3 is pinned in DETACHED_INIT_DUMP (its
   * dump stays deferred until the re-evaluation cancels it). peer3 splits into
   * its own group and stays healthy; the other peers stay on
   * propagate-everything.
   */
  applyAppendOverrideToSubject();
  expectSubjectSplitFromGroup(originalGroup, otherAddrs);
  /*
   * The re-eval cancels peer3's deferred init dump and re-evaluates it inline
   * under the new policy, so peer3's RIB-OUT reflects append (all routes, the
   * appended community on the i % 3 == 0 routes) and it stays healthy; the
   * other peers stay on propagate-everything.
   */
  waitForRibOutAdvertisedCount(kPeerAddr3, kNumRoutes);
  expectRibOutForPolicy(kPeerAddr3, kMatchModifyAppendPolicyName, kNumRoutes);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  EXPECT_TRUE(isPeerInSync(kPeerAddr3));
  EXPECT_NE(getPeerState(kPeerAddr3), PeerUpdateState::DOWN);
  /*
   * Wire: the inline re-eval dump delivers peer3's full re-evaluated set to its
   * egress queue without waiting on a rejoin (not gated on JOINED_RUNNING), so
   * draining captures every route with the appended community on the i % 3 == 0
   * routes.
   */
  recordDrainedRoutes(subjectId);
  expectReceivedRoutesForPolicy(
      subjectId, kMatchModifyAppendPolicyName, injectedPrefixes);
  for (const auto& other : otherAddrs) {
    waitForRibOutAdvertisedCount(other, kNumRoutes);
    expectRibOutForPolicy(other, kPermitAllPolicyName, kNumRoutes);
  }

  /*
   * Release the defer -- a no-op for the dump the re-eval already cancelled and
   * ran inline -- and confirm peer3 converges back into its own split group as
   * a normal in-sync member, with the other peers still in sync in theirs.
   */
  testOnlyDeferInitDump(kPeerAddr3, false, fboss::kDefaultVrf);
  EXPECT_EQ(getPeerState(kPeerAddr3), PeerUpdateState::JOINED_RUNNING);
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  EXPECT_TRUE(isPeerInSync(kPeerAddr3));
  for (const auto& other : otherAddrs) {
    EXPECT_TRUE(isPeerInSync(other));
  }
  verifySlowPeerInvariants(kPeerAddr3);

  XLOGF(INFO, "=== TEST PASSED: PeerPolicyChangeDetachedInitDumpPeerSplit ===");
}

INSTANTIATE_TEST_SUITE_P(
    SerializationModes,
    SinglePeerPolicyReEvalE2ETest,
    ::testing::Values(kNoSerialization, kWithSerialization),
    [](const ::testing::TestParamInfo<SerializationParams>& info) {
      return info.param.name;
    });

} // namespace bgp
} // namespace facebook
