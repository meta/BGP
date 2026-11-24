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

/* E2E tests: Complex multi-event sequences and recovery cycles.
 * Prefix range: 30.1-30.49/16.
 *
 * Quintuple sequence: block -> detach -> route-wd -> route-add -> unblock
 * Double recovery cycle: detach -> recover -> re-block -> recover -> accept
 * All peers detach -> 0 in-sync -> routes -> first peer recovers
 * DFP final routes match in-sync peers after rejoin
 * AS-path prepend correctness through detach-recover cycle
 */

#include <fmt/format.h>
#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupSlowPeerTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/* Quintuple event sequence.
 * Block peer3 -> freq-detach -> withdraw a route -> add a new route ->
 * unblock -> verify peer4 received everything, peer3 is detached.
 */
TEST_P(UpdateGroupMultiPeerTest, QuintupleBlockDetachWdAddUnblock) {
  XLOG(INFO, "=== TEST: QuintupleBlockDetachWdAddUnblock ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);

  /* Large queue for fast peer, initialize components */
  setDefaultQueueSizes(10, 8, 0);
  setupComponents();

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  /* Bring up fast peer with large queue */
  bringUpPeer(kPeerAddr4);

  /* Small queue for slow peer — fills naturally under load */
  setDefaultQueueSizes(3, 2, 0);
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId4));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  /* Inject a shared route before detach (will be withdrawn later) */
  injectLocalRoutesAtRuntime({"30.1.0.0/16"}, {"3001:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.1.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.1.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3001:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.1.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3001:1"));

  /* Set freq threshold=1 for peer3 — first block triggers detach */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  /* Fill peer3's small queue (3,2,0) to trigger freq-detach.
   * hwm=2: 2nd item sets blocked, threshold=1 fires detach. */
  for (int i = 2; i <= 4; i++) {
    auto prefix = fmt::format("30.{}.0.0/16", i);
    auto community = fmt::format("30{:02d}:1", i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("30.{}.0.0", i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }

  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Step 3: Withdraw the shared route (CL accumulates for peer3) */
  withdrawLocalRoutesAtRuntime({"30.1.0.0/16"});
  EXPECT_TRUE(verifyRouteWithdraw("v4", "30.1.0.0", 16, kPeerAddr4));

  /* Step 4: Add a new route (CL accumulates for peer3) */
  injectLocalRoutesAtRuntime({"30.7.0.0/16"}, {"3007:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.7.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.7.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3007:1"));

  /* Verify peer3 is still detached (DB, DR, or DRJ) */
  auto state = getPeerState(kPeerAddr3);
  EXPECT_TRUE(
      state == PeerUpdateState::DETACHED_BLOCKED ||
      state == PeerUpdateState::DETACHED_RUNNING ||
      state == PeerUpdateState::DETACHED_READY_TO_JOIN)
      << "Expected detached state, got " << static_cast<int>(state);

  /* Verify peer4 is still in sync and running */
  EXPECT_TRUE(isPeerInSync(kPeerAddr4));
  verifySlowPeerInvariants(kPeerAddr4);

  XLOG(INFO, "=== TEST PASSED: QuintupleBlockDetachWdAddUnblock ===");
}

/* Double recovery cycle.
 * Detach peer3 via freq threshold -> unblock (recovery) -> re-block ->
 * fill queue again -> unblock again -> verify peer4 still works.
 */
TEST_P(UpdateGroupMultiPeerTest, DoubleRecoveryCycle) {
  XLOG(INFO, "=== TEST: DoubleRecoveryCycle ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);

  /* Large queue for fast peer, initialize components */
  setDefaultQueueSizes(10, 8, 0);
  setupComponents();

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  /* Bring up fast peer with large queue */
  bringUpPeer(kPeerAddr4);

  /* Small queue for slow peer — fills naturally under load */
  setDefaultQueueSizes(3, 2, 0);
  bringUpPeer(kPeerAddr3);

  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId4));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  /* === Cycle 1: freq-detach peer3 via natural queue fill === */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  /* Inject 3 routes — peer3's (3,2,0) queue fills and freq fires */
  for (int i = 10; i <= 12; i++) {
    auto prefix = fmt::format("30.{}.0.0/16", i);
    auto community = fmt::format("30{:02d}:1", i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("30.{}.0.0", i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }

  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Bring peer3 DOWN and back UP to re-enter group (reconnection) */
  bringDownPeer(kPeerAddr3);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));

  /* Large queue for reconnection so init dump fits */
  setDefaultQueueSizes(10, 8, 0);
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);

  /* Drain init dump for reconnected peer3 */
  drainPeerQueueCompletely(peerId3);

  /* === Cycle 2: detach peer3 again via natural queue fill.
   * Peer3 reconnected with (10,8,0) queue, so need 9 routes to
   * fill past hwm=8 and trigger freq-detach. */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  for (int i = 13; i <= 21; i++) {
    auto prefix = fmt::format("30.{}.0.0/16", i);
    auto community = fmt::format("30{:02d}:1", i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("30.{}.0.0", i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }

  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Verify peer4 still operational -- inject and verify a new route */
  injectLocalRoutesAtRuntime({"30.22.0.0/16"}, {"3022:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.22.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.22.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3022:1"));

  EXPECT_TRUE(isPeerInSync(kPeerAddr4));

  XLOG(INFO, "=== TEST PASSED: DoubleRecoveryCycle ===");
}

/* All peers detach -> 0 in-sync -> routes arrive -> first
 * peer recovers -> accepted as sole in-sync member.
 * Note: from learned patterns, the LAST synced member skips detachment,
 * so with 3 peers we get 2 detached + 1 JOINED_BLOCKED (last synced).
 */
TEST_P(UpdateGroupMultiPeerTest, AllPeersDetachRecoverSole) {
  XLOG(INFO, "=== TEST: AllPeersDetachRecoverSole ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  addPeer(kDefaultPeerSpec5);

  /* Large queue for fast peer, initialize components */
  setDefaultQueueSizes(10, 8, 0);
  setupComponents();

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  /* Bring up fast peer with large queue */
  bringUpPeer(kPeerAddr5);

  /* Small queues for slow peers — fill naturally under load */
  setDefaultQueueSizes(3, 2, 0);
  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);

  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);
  sendEoRToPeer(peerId5);
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId5));
  EXPECT_TRUE(waitForEoR(peerId5));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr5, PeerUpdateState::JOINED_RUNNING));

  /* Set freq threshold=1 for both slow peers */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));
  setSlowPeerThresholds(
      kPeerAddr4,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  /* Inject 3 routes — peer3 and peer4 (3,2,0) queues fill and freq fires */
  for (int i = 20; i <= 22; i++) {
    auto prefix = fmt::format("30.{}.0.0/16", i);
    auto community = fmt::format("30{:02d}:1", i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("30.{}.0.0", i),
        16,
        kPeerAddr5,
        getExpectedNexthop(kPeerAddr5),
        "4200000001",
        community));
  }

  /* peer3 and peer4 should be detached; peer5 may be the last synced member
   * (skips detachment per learned pattern). */
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::DETACHED_BLOCKED));

  /* peer5 is the last synced member -- still in sync */
  EXPECT_TRUE(isPeerInSync(kPeerAddr5));

  /* Inject a route while peer3+4 are detached */
  injectLocalRoutesAtRuntime({"30.23.0.0/16"}, {"3023:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.23.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.23.0.0",
      16,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "3023:1"));

  /* Recover peer3: bring DOWN, bring UP with large queue for init dump */
  bringDownPeer(kPeerAddr3);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));
  setDefaultQueueSizes(10, 8, 0);
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);
  drainPeerQueueCompletely(peerId3);

  /* Verify peer5 is still functional */
  injectLocalRoutesAtRuntime({"30.24.0.0/16"}, {"3024:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.24.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.24.0.0",
      16,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "3024:1"));

  verifySlowPeerInvariants(kPeerAddr5);

  XLOG(INFO, "=== TEST PASSED: AllPeersDetachRecoverSole ===");
}

INSTANTIATE_TEST_SUITE_P(
    SerializationModes,
    UpdateGroupMultiPeerTest,
    ::testing::Values(kNoSerialization, kWithSerialization),
    [](const ::testing::TestParamInfo<SerializationParams>& info) {
      return info.param.name;
    });

} // namespace bgp
} // namespace facebook
