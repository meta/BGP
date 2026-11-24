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
 * E2E repro for T282746526: an update-group peer stuck permanently in
 * DETACHED_READY_TO_JOIN because the group consumes the change list with an
 * empty packing list and so never drains.
 *
 * Mechanics of the deadlock (see AdjRibGroup):
 *   - A DETACHED_READY_TO_JOIN (DRJ) peer that is AHEAD of the group on the
 *     change list cancels its own packing timers and sits still. It relies on
 *     the group to pick it back up.
 *   - The group only rejoins detached peers from
 * checkAndAcceptReadyToJoinPeers, which runs ONLY after a packing-list drain
 * completes (WAITING -> IDLE).
 *   - The drain is scheduled ONLY when the packing list is non-empty. When the
 *     egress policy denies every route on the change list, each consumption
 *     leaves the packing list empty, so the group advances its RIB version but
 *     never drains, never reaches IDLE, and never rejoins the DRJ peer.
 *
 * Reproduction recipe (no test-only DRJ defer hook — the deadlock itself keeps
 * the peer pinned in DRJ):
 *   1. Freeze the group at V1 behind a JOINED_BLOCKED peer (peer4's egress
 * queue is filled with PERMITTED plug routes; egress backpressure pends the
 *      group's change-list consumer, so the group stops advancing).
 *   2. Publish more (DENIED) routes so the RIB version climbs to V2 > V1. The
 *      group stays frozen at V1.
 *   3. Bring up a new peer (peer5). Because the group is frozen, it runs an
 *      independent initial dump -> DETACHED_INIT_DUMP.
 *   4. Drain peer5 so it consumes to the end of the change list (V2).
 *   5. peer5 is now ahead of the frozen group -> DETACHED_READY_TO_JOIN
 *      (verified via the real peer state, no defer flag).
 *   6. Publish more DENIED routes, bumping the RIB version to V3 > V2.
 *   7. Unblock the group and let it consume to the end of the change list.
 * Every route between V1 and V3 is denied, so the packing list stays empty: the
 *      group never drains and never rejoins peer5.
 *
 * RIB-OUT is verified throughout (via verifyRibOutEntries): every permitted
 * plug route is advertised and no denied churn route is — this is what makes
 * the group's catch-up an empty-packing-list consumption.
 *
 * The final assertion is the CORRECT (post-fix) behavior: peer5 escapes DRJ and
 * reaches JOINED_RUNNING and the group settles to IDLE. It FAILS on current
 * code (peer5 pinned in DRJ, group pinned in READY), which is the repro.
 *
 * Route layout: 36.0.x.0/24 (route index in the 3rd octet, matching what
 * verifyRibOutEntries keys on). Plug routes take indices [0, kNumPlugRoutes);
 * denied churn routes take the indices above that.
 */

#include <fmt/format.h>

#include "neteng/fboss/bgp/cpp/tests/PolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupSlowPeerTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

namespace {
/*
 * Churn routes carry kDenyCommunity and are denied by the egress policy, so
 * consuming them advances the group's RIB version but adds nothing to the
 * packing list. Plug routes carry unique 100:i communities (permitted), so each
 * is a distinct UPDATE that fills the blocked peer's queue.
 */
constexpr auto kDenyCommunity = "65500:999";
constexpr auto kDenyChurnPolicyName = "deny-churn-permit-rest";
/* Shared peer-group name so peer3/peer4/peer5 form one update group. */
constexpr auto kDeadlockPeerGroupName = "drj-empty-drain-group";

/*
 * peer4's queue blocks at size >= highWm and the group checks isBlocked()
 * before each push, so injecting highWm + 1 permitted plug routes consumes all
 * of them (the (highWm+1)-th push finds the queue blocked, marks peer4
 * JOINED_BLOCKED, and parks that route in a per-peer deferred push) with
 * nothing left in the group packing list.
 */
constexpr int kPeer4HighWm = 8;
constexpr int kNumPlugRoutes = kPeer4HighWm + 1;
/* Denied churn published in two batches to bump the RIB version past the frozen
 * group, then past the DRJ peer. */
constexpr int kNumChurnStep2 = 100;
constexpr int kNumChurnStep6 = 20;
constexpr int kTotalChurnRoutes = kNumChurnStep2 + kNumChurnStep6;

/* /24 route so the route index lives in the 3rd octet (what
 * verifyRibOutEntries' route-index predicate reads). */
std::string routePrefix(int index) {
  return fmt::format("36.0.{}.0/24", index);
}
} // namespace

class UpdateGroupDrjAheadEmptyDrainDeadlockTest : public SlowPeerTestBase {
 protected:
  /*
   * Register an egress policy that DENIES routes carrying kDenyCommunity and
   * permits everything else. Call before setupComponents() (i.e. before
   * createPeerManager). This is a static config policy — no runtime re-eval.
   */
  void setupDenyChurnPolicy() {
    auto denyMatch = createBgpPolicyAtomicMatch(
        bgp_policy::BgpPolicyAtomicMatchType::COMMUNITY_LIST, {kDenyCommunity});
    auto denyTerm = createBgpPolicyTerm(
        "deny-churn",
        "Deny routes carrying the churn community",
        {std::move(denyMatch)},
        {createBgpPolicyAction(bgp_policy::BgpPolicyActionType::DENY)},
        bgp_policy::FlowControlAction::NEXT_TERM);
    auto permitTerm = createBgpPolicyTerm(
        "permit-rest",
        "Permit everything else",
        {},
        {createBgpPolicyAction(bgp_policy::BgpPolicyActionType::PERMIT)},
        bgp_policy::FlowControlAction::NEXT_TERM);
    setPolicyConfig(
        createBgpPolicies(kDenyChurnPolicyName, {denyTerm, permitTerm}));
  }

  /* v4-only eBGP peer sharing the deadlock peer group and the deny policy. */
  BgpPeerSpec makeGroupPeer(
      uint32_t asn,
      const folly::IPAddress& peerAddr,
      const folly::IPAddress& v4Nexthop) {
    BgpPeerSpec spec{};
    spec.asn = asn;
    spec.localAddr = kLocalAddr1;
    spec.peerAddr = peerAddr;
    spec.v4Nexthop = v4Nexthop;
    spec.v6Nexthop = kEmptyV6Nexthop;
    spec.disableIpv6Afi = true;
    spec.peerGroupName = kDeadlockPeerGroupName;
    spec.egressPolicyName = kDenyChurnPolicyName;
    return spec;
  }

  /*
   * Publish `count` DENIED churn routes starting at route index `startIdx` in a
   * single injection, then wait for the last to reach the shadow RIB.
   */
  void injectDeniedChurn(int startIdx, int count) {
    std::vector<std::string> prefixes;
    prefixes.reserve(count);
    for (int i = 0; i < count; ++i) {
      prefixes.push_back(routePrefix(startIdx + i));
    }
    injectLocalRoutesAtRuntime(prefixes, {kDenyCommunity}, 150);
    ASSERT_TRUE(waitForRouteInShadowRib(
        folly::IPAddress::createNetwork(routePrefix(startIdx + count - 1))))
        << "churn routes did not reach shadow RIB";
  }

  /*
   * Verify the peer's/group's RIB-OUT reflects the deny-churn policy:
   *   - every permitted plug route has a RIB-OUT entry AND is advertised;
   *   - every denied churn route has a RIB-OUT entry but is NOT advertised
   *     (present in the RIB-OUT, postAttr null) — this is what makes the
   * group's change-list consumption an empty-packing-list drain.
   * `expectedChurn` is how many churn routes should have reached this RIB-OUT
   * so far. Reuses the policy-eval RIB-OUT API (verifyRibOutEntries +
   * verifyAdvertised / verifyNotAdvertised).
   */
  void verifyRibOut(const folly::IPAddress& peerAddr, int expectedChurn) {
    auto isPlug = [](int index) { return index < kNumPlugRoutes; };
    auto isChurn = [](int index) { return index >= kNumPlugRoutes; };
    auto advertised = [](const AdjRibEntry& entry, const folly::CIDRNetwork&) {
      return entry.getPostAttr() != nullptr;
    };
    auto notAdvertised = [](const AdjRibEntry& entry,
                            const folly::CIDRNetwork&) {
      return entry.getPostAttr() == nullptr;
    };
    /* Wait for the RIB-OUT to settle using plain predicates (no per-entry
     * EXPECT during retries). */
    WITH_RETRIES_N(30, {
      EXPECT_EVENTUALLY_EQ(
          verifyRibOutEntries(peerAddr, isPlug, advertised),
          static_cast<size_t>(kNumPlugRoutes));
      EXPECT_EVENTUALLY_EQ(
          verifyRibOutEntries(peerAddr, isChurn, notAdvertised),
          static_cast<size_t>(expectedChurn));
    });
    /* One-shot per-entry assertions: every plug route present + advertised,
     * every churn route present but NOT advertised. */
    EXPECT_EQ(
        verifyRibOutEntries(peerAddr, isPlug, verifyAdvertised()),
        static_cast<size_t>(kNumPlugRoutes))
        << "all plug routes should be present and advertised for "
        << peerAddr.str();
    EXPECT_EQ(
        verifyRibOutEntries(peerAddr, isChurn, verifyNotAdvertised()),
        static_cast<size_t>(expectedChurn))
        << "all churn routes should be present but not advertised for "
        << peerAddr.str();
  }
};

TEST_P(
    UpdateGroupDrjAheadEmptyDrainDeadlockTest,
    DrjPeerAheadWithEmptyDrainNeverRejoins) {
  XLOGF(INFO, "=== TEST: DrjPeerAheadWithEmptyDrainNeverRejoins ===");

  /* peer3: healthy in-sync anchor (large queue). peer4: freezes the group when
   * blocked (small queue, highWm kPeer4HighWm). peer5: the new peer that
   * becomes DRJ ahead of the group. All share one update group + the deny-churn
   * egress policy. */
  addPeer(makeGroupPeer(kPeerAsn3, kPeerAddr3, kNextHopV4_3));
  addPeer(makeGroupPeer(kPeerAsn4, kPeerAddr4, kNextHopV4_4));
  addPeer(makeGroupPeer(kPeerAsn5, kPeerAddr5, kNextHopV4_5));
  setupDenyChurnPolicy();
  setDefaultQueueSizes(/*capacity=*/100, /*highWm=*/80, /*lowWm=*/2);
  setQueueSizeForPeer(
      kPeerAddr4, /*capacity=*/10, /*highWm=*/kPeer4HighWm, /*lowWm=*/2);
  setupComponents();

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  /* Bring up peer3 + peer4 (peer5 joins later). v4-only -> one EoR each. */
  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  /*
   * Step 1: freeze the group at V1 behind a JOINED_BLOCKED peer4. High
   * slow-peer thresholds keep peer4 blocked (never detached). Block peer4, then
   * inject kNumPlugRoutes (= highWm + 1) PERMITTED plug routes (distinct
   * communities so each is a separate UPDATE). The group consumes ALL of them —
   * nothing permitted is left in the change list, and none land in the group
   * packing list — while filling peer4's queue past the watermark, so egress
   * backpressure pends the group's change-list consumer and the group stops
   * advancing. peer3 (drained) stays in sync so the group keeps a sync member
   * (the DRJ peer never self-reseeds).
   */
  setSlowPeerThresholds(
      kPeerAddr4,
      std::chrono::milliseconds(600000),
      1000000,
      std::chrono::milliseconds(600000));
  blockPeer(kPeerAddr4);
  std::vector<std::string> plugPrefixes;
  for (int i = 0; i < kNumPlugRoutes; ++i) {
    plugPrefixes.push_back(routePrefix(i));
  }
  injectDistinctRoutes(plugPrefixes, /*communityBase=*/100);

  XLOGF(INFO, "Plug routes injected; waiting for peer4 to block the group");
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId4));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_BLOCKED));
  EXPECT_TRUE(isPeerInSync(kPeerAddr4));
  EXPECT_FALSE(isPeerDetached(kPeerAddr4));
  recordDrainedRoutes(peerId3, 1, 100);

  /* peer3's RIB-OUT already reflects the policy: all plug routes present +
   * advertised, and no churn routes yet. */
  verifyRibOut(kPeerAddr3, /*expectedChurn=*/0);

  /*
   * Step 2: publish DENIED churn routes so the RIB version climbs to V2 > V1.
   * The group is frozen behind peer4, so it does not consume these.
   */
  injectDeniedChurn(/*startIdx=*/kNumPlugRoutes, /*count=*/kNumChurnStep2);

  /*
   * Step 3: bring up peer5. The frozen group forces an independent initial dump
   * -> DETACHED_INIT_DUMP.
   */
  bringUpPeer(kPeerAddr5);
  sendEoRToPeer(peerId5);

  /*
   * Steps 4 + 5: drain peer5 as it consumes to the end of the change list (V2)
   * and races ahead of the frozen group (V1). It should settle in
   * DETACHED_READY_TO_JOIN — verified via the real peer state, no defer hook.
   */
  WITH_RETRIES_N(30, {
    recordDrainedRoutes(peerId5, 1, 100);
    ASSERT_EVENTUALLY_EQ(
        getPeerState(kPeerAddr5), PeerUpdateState::DETACHED_READY_TO_JOIN);
  });
  EXPECT_TRUE(isPeerDetached(kPeerAddr5));
  /* peer5's detached dump materialized every prefix it has seen so far: the
   * plug routes advertised, and the step-2 churn present but not advertised. */
  verifyRibOut(kPeerAddr5, /*expectedChurn=*/kNumChurnStep2);

  /*
   * Step 6: publish more DENIED churn routes, bumping the RIB version to
   * V3 > V2.
   */
  injectDeniedChurn(
      /*startIdx=*/kNumPlugRoutes + kNumChurnStep2, /*count=*/kNumChurnStep6);

  /*
   * Step 7: unblock the group and let it consume to the end of the change list.
   * Every route between V1 and V3 is denied, so the packing list stays empty:
   * the group advances its version past peer5 but never drains, so it never
   * runs checkAndAcceptReadyToJoinPeers.
   */
  unblockPeer(kPeerAddr4);

  /*
   * Once backpressure clears, the group resumes and consumes the ENTIRE
   * (denied) change list. Drain the peers so it catches up; the group's RIB-OUT
   * settles with every prefix present — plug routes advertised, ALL churn
   * routes present but not advertised. This holds whether or not the DRJ
   * deadlock is fixed: the group always finishes consuming the change list (it
   * just never drains it).
   */
  auto isChurnIdx = [](int index) { return index >= kNumPlugRoutes; };
  auto churnNotAdvertised = [](const AdjRibEntry& entry,
                               const folly::CIDRNetwork&) {
    return entry.getPostAttr() == nullptr;
  };
  WITH_RETRIES_N(30, {
    recordDrainedRoutes(peerId3, 1, 100);
    recordDrainedRoutes(peerId4, 1, 100);
    recordDrainedRoutes(peerId5, 1, 100);
    EXPECT_EVENTUALLY_EQ(
        verifyRibOutEntries(kPeerAddr3, isChurnIdx, churnNotAdvertised),
        static_cast<size_t>(kTotalChurnRoutes));
  });

  /* The group's RIB-OUT (read via in-sync peer3): every route present — all
   * plug routes advertised, all churn routes present but NOT advertised. */
  verifyRibOut(kPeerAddr3, /*expectedChurn=*/kTotalChurnRoutes);

  /*
   * Correct (post-fix) behavior: with the change list fully consumed, peer5
   * escapes DETACHED_READY_TO_JOIN and rejoins the group at JOINED_RUNNING.
   * On current code this never happens — peer5 is pinned in DRJ and the group
   * is pinned in READY — so these assertions fail, reproducing T282746526.
   */
  WITH_RETRIES_N(30, {
    recordDrainedRoutes(peerId5, 1, 100);
    EXPECT_EVENTUALLY_EQ(
        getPeerState(kPeerAddr5), PeerUpdateState::JOINED_RUNNING)
        << "peer5 stuck in DETACHED_READY_TO_JOIN (T282746526): the group "
           "consumed the change list with an empty packing list, so it never "
           "drained and never rejoined the ahead DRJ peer";
  });
  EXPECT_EQ(getGroupState(kPeerAddr3), UpdateGroupState::IDLE)
      << "group pinned in READY: it consumed the change list but never drained "
         "an empty packing list to reach IDLE (T282746526)";

  /* After the rejoin peer5's RIB-OUT matches the group: all plug routes
   * advertised, all churn routes present but not advertised. */
  verifyRibOut(kPeerAddr5, /*expectedChurn=*/kTotalChurnRoutes);

  XLOGF(INFO, "=== TEST DONE: DrjPeerAheadWithEmptyDrainNeverRejoins ===");
}

INSTANTIATE_TEST_SUITE_P(
    SerializationModes,
    UpdateGroupDrjAheadEmptyDrainDeadlockTest,
    ::testing::Values(kNoSerialization, kWithSerialization),
    [](const ::testing::TestParamInfo<SerializationParams>& info) {
      return info.param.name;
    });

} // namespace bgp
} // namespace facebook
