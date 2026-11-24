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

/* E2E tests: Peer DOWN edge cases — rapid flap, all-sync-down, route refresh,
 * lazy clone, and re-block after reconnect.
 * Prefix range: 31.x.0.0/16
 *
 * Peer DOWN then immediately UP (rapid flap) — clean restart
 * All in-sync peers DOWN, only detached peers remain
 * Peer DOWN during route refresh processing — clean state
 * Peer DOWN during lazy clone iteration — bitmap handles dead peer
 * Peer DOWN from P-DB, comes back, immediately blocks again
 */

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupSlowPeerTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/*
 * Peer DOWN then immediately UP (rapid flap).
 * Bring peer3 to JOINED_RUNNING, inject a route to confirm delivery,
 * then bringDownPeer + bringUpPeer in rapid succession. The reconnected
 * peer enters DETACHED_INIT_DUMP (reconnecting to existing group).
 * Drain init dump, verify peer4 continues normally throughout.
 * No stale state from the previous session leaks into the new one.
 */
TEST_P(UpdateGroupMultiPeerTest, RapidFlap_CleanRestart) {
  XLOG(INFO, "=== TEST: RapidFlap_CleanRestart ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  setupSlowPeerComponents(8, 6, 2);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId4));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  /* Inject a route — both peers receive it */
  injectLocalRoutesAtRuntime({"31.1.0.0/16"}, {"3101:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.1.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.1.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3101:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.1.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3101:1"));

  /* Rapid flap: DOWN then immediately UP */
  bringDownPeer(kPeerAddr3);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);

  /* Reconnected peer enters DETACHED_INIT_DUMP (existing group) */
  drainPeerQueueCompletely(peerId3);

  /* Peer4 continues receiving routes during and after the flap */
  injectLocalRoutesAtRuntime({"31.2.0.0/16"}, {"3102:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.2.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.2.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3102:1"));

  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));
  EXPECT_TRUE(isPeerInSync(kPeerAddr4));

  XLOG(INFO, "=== TEST PASSED: RapidFlap_CleanRestart ===");
}

/*
 * All in-sync peers DOWN, only detached peers remain.
 * 3-peer group: detach peer5 via freq threshold, then bring both in-sync
 * peers (3,4) DOWN. Only the detached peer5 remains. Verify no crash
 * and group handles the "no in-sync peers" state gracefully.
 */
TEST_P(UpdateGroupMultiPeerTest, AllSyncPeersDown_OnlyDetachedRemain) {
  XLOG(INFO, "=== TEST: AllSyncPeersDown_OnlyDetachedRemain ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  addPeer(kDefaultPeerSpec5);
  setupSlowPeerComponents(3, 2, 0);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  bringUpPeer(kPeerAddr5);
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

  /* Detach peer5 via freq threshold (1 block = detach) */
  setSlowPeerThresholds(
      kPeerAddr5,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));
  blockPeer(kPeerAddr5);
  injectLocalRoutesAtRuntime({"31.10.0.0/16"}, {"3110:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.10.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.10.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3110:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.10.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3110:1"));
  injectLocalRoutesAtRuntime({"31.11.0.0/16"}, {"3111:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.11.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.11.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3111:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.11.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3111:1"));
  injectLocalRoutesAtRuntime({"31.12.0.0/16"}, {"3112:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.12.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.12.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3112:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.12.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3112:1"));
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId5));
  ASSERT_TRUE(waitForPeerState(kPeerAddr5, PeerUpdateState::DETACHED_BLOCKED));

  /* Now bring BOTH in-sync peers DOWN */
  bringDownPeer(kPeerAddr3);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));
  bringDownPeer(kPeerAddr4);
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::DOWN));

  /* Only detached peer5 remains — no crash, group handles gracefully */
  auto state5 = getPeerState(kPeerAddr5);
  EXPECT_NE(state5, PeerUpdateState::DOWN);

  XLOG(INFO, "=== TEST PASSED: AllSyncPeersDown_OnlyDetachedRemain ===");
}

/*
 * Peer DOWN during route refresh processing.
 * No sendRouteRefresh helper in E2E, so simulate route refresh activity
 * by injecting a burst of routes (each with different community triggers
 * separate MRAI cycles). While this "refresh-like" activity is ongoing
 * for peer4, bring peer3 DOWN. Verify clean state: peer4 receives all
 * routes, peer3 cleanup doesn't affect peer4.
 */
TEST_P(UpdateGroupMultiPeerTest, PeerDown_DuringRouteRefreshActivity) {
  XLOG(INFO, "=== TEST: PeerDown_DuringRouteRefreshActivity ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  setupSlowPeerComponents(8, 6, 2);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId4));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  /* Inject first route — both peers receive */
  injectLocalRoutesAtRuntime({"31.20.0.0/16"}, {"3120:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.20.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.20.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3120:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.20.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3120:1"));

  /* Simulate route refresh burst: inject 2 more routes, then DOWN peer3 */
  injectLocalRoutesAtRuntime({"31.21.0.0/16"}, {"3121:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.21.0.0/16")));

  /* Bring peer3 DOWN mid-refresh activity */
  bringDownPeer(kPeerAddr3);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));

  /* Drain peer4 — should have received the route */
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.21.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3121:1"));

  /* Continue injecting after peer3 is DOWN — peer4 still works */
  injectLocalRoutesAtRuntime({"31.22.0.0/16"}, {"3122:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.22.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.22.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3122:1"));

  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));
  EXPECT_TRUE(isPeerInSync(kPeerAddr4));

  XLOG(INFO, "=== TEST PASSED: PeerDown_DuringRouteRefreshActivity ===");
}

/*
 * Peer DOWN during lazy clone iteration.
 * Detach peer3 via freq threshold. Inject a route post-detachment (triggers
 * lazy clone for peer3's CL). Then bring peer3 DOWN while the CL has
 * accumulated entries. Verify bitmap check handles dead peer gracefully:
 * no crash, peer4 continues, detached count goes to 0.
 */
TEST_P(UpdateGroupMultiPeerTest, PeerDown_DuringLazyCloneIteration) {
  XLOG(INFO, "=== TEST: PeerDown_DuringLazyCloneIteration ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  setupSlowPeerComponents(3, 2, 0);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId4));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  /* Inject a shared route before detachment */
  injectLocalRoutesAtRuntime({"31.30.0.0/16"}, {"3130:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.30.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.30.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3130:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.30.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3130:1"));

  /* Detach peer3 via freq threshold */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));
  blockPeer(kPeerAddr3);
  injectLocalRoutesAtRuntime({"31.31.0.0/16"}, {"3131:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.31.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.31.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3131:1"));
  injectLocalRoutesAtRuntime({"31.32.0.0/16"}, {"3132:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.32.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.32.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3132:1"));
  injectLocalRoutesAtRuntime({"31.33.0.0/16"}, {"3133:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.33.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.33.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3133:1"));
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Inject post-detach route — triggers lazy clone (Case 4) for peer3's CL */
  injectLocalRoutesAtRuntime({"31.34.0.0/16"}, {"3134:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.34.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.34.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3134:1"));

  /* Bring peer3 DOWN while CL has lazy clone entries */
  bringDownPeer(kPeerAddr3);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));

  /* Verify cleanup: no crash, detached count 0, peer4 functional */
  EXPECT_EQ(getDetachedPeerCount(kPeerAddr4), 0);
  injectLocalRoutesAtRuntime({"31.35.0.0/16"}, {"3135:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.35.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.35.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3135:1"));

  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));
  EXPECT_TRUE(isPeerInSync(kPeerAddr4));

  XLOG(INFO, "=== TEST PASSED: PeerDown_DuringLazyCloneIteration ===");
}

/*
 * Peer DOWN from P-DB, comes back, freq counter resets on reconnect.
 * Freq-detach peer3 to DB via 2 block cycles, bring DOWN, unblock,
 * bring UP. The reconnected peer enters DETACHED_INIT_DUMP (existing
 * group). Drain init dump, verify peer3 is NOT instantly re-detached
 * (freq counter reset on reconnect) and peer4 continues normally.
 */
TEST_P(UpdateGroupMultiPeerTest, ReconnectAndReblock_NoInstantRedetach) {
  XLOG(INFO, "=== TEST: ReconnectAndReblock_NoInstantRedetach ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  /* Large queue for fast peer + slow peer detection infra */
  setupSlowPeerComponents(10, 8, 0);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  /* Fast peer with large queue */
  bringUpPeer(kPeerAddr4);
  /* Slow peer with small queue — fills naturally */
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

  /* Detach peer3 via freq threshold (2 blocks = detach) */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      2,
      std::chrono::milliseconds(60000));

  /* Block cycle 1: 3 routes to exceed hwm=2 */
  blockPeer(kPeerAddr3);
  injectLocalRoutesAtRuntime({"31.40.0.0/16"}, {"3140:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.40.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.40.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3140:1"));
  injectLocalRoutesAtRuntime({"31.41.0.0/16"}, {"3141:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.41.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.41.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3141:1"));
  injectLocalRoutesAtRuntime({"31.42.0.0/16"}, {"3142:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.42.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.42.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3142:1"));
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  /* unblockPeer auto-drains fill routes */
  unblockPeer(kPeerAddr3);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));

  /* Block cycle 2 → triggers freq detachment */
  blockPeer(kPeerAddr3);
  injectLocalRoutesAtRuntime({"31.43.0.0/16"}, {"3143:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.43.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.43.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3143:1"));
  injectLocalRoutesAtRuntime({"31.44.0.0/16"}, {"3144:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.44.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.44.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3144:1"));
  injectLocalRoutesAtRuntime({"31.45.0.0/16"}, {"3145:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.45.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.45.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3145:1"));
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Bring DOWN, unblock, bring UP — reconnect cycle */
  bringDownPeer(kPeerAddr3);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));
  unblockPeer(kPeerAddr3);
  /* Larger queue for reconnection so init dump fits */
  setDefaultQueueSizes(10, 8, 0);
  testOnlyDeferInitDump(kPeerAddr3, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);

  /* Wait for init dump to start before draining */
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_INIT_DUMP));
  testOnlyDeferInitDump(kPeerAddr3, false, fboss::kDefaultVrf);
  drainPeerQueueCompletely(peerId3);

  /* Peer3 reconnected — NOT instantly re-detached (freq counter reset) */
  auto state3 = getPeerState(kPeerAddr3);
  EXPECT_NE(state3, PeerUpdateState::DETACHED_BLOCKED);
  EXPECT_NE(state3, PeerUpdateState::DOWN);

  /* Peer4 continues normally throughout */
  injectLocalRoutesAtRuntime({"31.46.0.0/16"}, {"3146:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.46.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.46.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3146:1"));

  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));
  EXPECT_TRUE(isPeerInSync(kPeerAddr4));

  XLOG(INFO, "=== TEST PASSED: ReconnectAndReblock_NoInstantRedetach ===");
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
