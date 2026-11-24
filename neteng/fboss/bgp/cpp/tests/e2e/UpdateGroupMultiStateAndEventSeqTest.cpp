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

/* E2E tests: Multi-peer mixed state combos and event sequence pairs.
 * Prefix range: 31.x.0.0/16.
 *
 * (P-JR + P-JR + P-DB) × E-PEER-DOWN(JR1)
 * (P-JR + P-DID + P-DB) × E-PEER-DOWN(JR)
 * (P-JR + P-JB + P-DRJ) × E-SLOW-DUR(JB)
 */

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupSlowPeerTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/* 3 peers — peer3 JOINED_RUNNING, peer4 JOINED_RUNNING, peer5
 * DETACHED_BLOCKED. Bring peer3 DOWN. Peer4 should continue receiving
 * routes normally. Peer5 stays DETACHED_BLOCKED. No crash.
 */
TEST_P(UpdateGroupMultiPeerTest, JrJrDb_PeerDownOnJr) {
  XLOG(INFO, "=== TEST: JrJrDb_PeerDownOnJr ===");

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

  /* Detach peer5 via freq threshold=1 */
  setSlowPeerThresholds(
      kPeerAddr5,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr5);
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
  injectLocalRoutesAtRuntime({"31.2.0.0/16"}, {"3102:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.2.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.2.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3102:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.2.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3102:1"));
  injectLocalRoutesAtRuntime({"31.3.0.0/16"}, {"3103:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.3.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.3.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3103:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.3.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3103:1"));

  ASSERT_TRUE(waitForPeerState(kPeerAddr5, PeerUpdateState::DETACHED_BLOCKED));
  EXPECT_TRUE(isPeerDetached(kPeerAddr5));

  /* Bring peer3 DOWN */
  bringDownPeer(kPeerAddr3);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));

  /* Peer4 should still be running, peer5 still detached */
  EXPECT_TRUE(isPeerInSync(kPeerAddr4));
  EXPECT_TRUE(isPeerDetached(kPeerAddr5));

  /* Inject a route — peer4 receives it */
  injectLocalRoutesAtRuntime({"31.4.0.0/16"}, {"3104:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.4.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.4.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3104:1"));

  verifySlowPeerInvariants(kPeerAddr4);

  XLOG(INFO, "=== TEST PASSED: JrJrDb_PeerDownOnJr ===");
}

/* 3 peers — peer3 JOINED_RUNNING (last in-sync), peer4
 * DETACHED_INIT_DUMP, peer5 DETACHED_BLOCKED. Bring peer3 DOWN.
 * All peers are now non-functional. Verify no crash.
 * Use freq-detach → down → unblock+up pattern for DID state.
 */
TEST_P(UpdateGroupMultiPeerTest, JrDidDb_PeerDownOnLastInSync) {
  XLOG(INFO, "=== TEST: JrDidDb_PeerDownOnLastInSync ===");

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

  /* Detach peer5 via freq threshold=1 */
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

  ASSERT_TRUE(waitForPeerState(kPeerAddr5, PeerUpdateState::DETACHED_BLOCKED));

  /* Get peer4 into DETACHED_INIT_DUMP: freq-detach → down → unblock+up */
  setSlowPeerThresholds(
      kPeerAddr4,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr4);
  injectLocalRoutesAtRuntime({"31.13.0.0/16"}, {"3113:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.13.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.13.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3113:1"));
  injectLocalRoutesAtRuntime({"31.14.0.0/16"}, {"3114:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.14.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.14.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3114:1"));
  injectLocalRoutesAtRuntime({"31.15.0.0/16"}, {"3115:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.15.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.15.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3115:1"));

  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::DETACHED_BLOCKED));

  /* Bring peer4 down, then back up to enter DETACHED_INIT_DUMP */
  bringDownPeer(kPeerAddr4);
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::DOWN));
  unblockPeer(kPeerAddr4);
  testOnlyDeferInitDump(kPeerAddr4, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr4);
  sendEoRToPeer(peerId4);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr4, PeerUpdateState::DETACHED_INIT_DUMP));
  testOnlyDeferInitDump(kPeerAddr4, false, fboss::kDefaultVrf);

  /* Now: peer3=JR (last in-sync), peer4=DID, peer5=DB.
   * Bring peer3 DOWN — last in-sync peer goes away.
   */
  bringDownPeer(kPeerAddr3);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));

  /* No crash. Peer4/5 were detached — after last in-sync goes DOWN they
   * may transition through acceptance or stay detached; any non-INIT state
   * is valid (the test exercises the no-crash invariant).
   */
  auto state4 = getPeerState(kPeerAddr4);
  auto state5 = getPeerState(kPeerAddr5);
  EXPECT_NE(state4, PeerUpdateState::INIT);
  EXPECT_NE(state5, PeerUpdateState::INIT);

  XLOG(INFO, "=== TEST PASSED: JrDidDb_PeerDownOnLastInSync ===");
}

/* 3 peers — peer3 JOINED_RUNNING, peer4 JOINED_BLOCKED, peer5
 * DETACHED_READY_TO_JOIN (freq-detached then unblocked). Fire duration
 * threshold on peer4 (JB → DB). Verify group still has peer3 in-sync.
 */
TEST_P(UpdateGroupMultiPeerTest, JrJbDrj_DurationDetachJb) {
  XLOG(INFO, "=== TEST: JrJbDrj_DurationDetachJb ===");

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

  /* Detach peer5 via freq threshold=1, then unblock to reach DRJ */
  setSlowPeerThresholds(
      kPeerAddr5,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr5);
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
  injectLocalRoutesAtRuntime({"31.21.0.0/16"}, {"3121:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.21.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.21.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3121:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.21.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3121:1"));
  injectLocalRoutesAtRuntime({"31.22.0.0/16"}, {"3122:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.22.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.22.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3122:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.22.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3122:1"));

  ASSERT_TRUE(waitForPeerState(kPeerAddr5, PeerUpdateState::DETACHED_BLOCKED));

  /* Unblock peer5 — confirm DRJ before proceeding */
  waitForDrjWithDrain(kPeerAddr5, peerId5);

  /* Block peer4 and set 1ms duration threshold — fires immediately */
  blockPeer(kPeerAddr4);
  setSlowPeerThresholds(
      kPeerAddr4,
      std::chrono::milliseconds(1),
      999999,
      std::chrono::milliseconds(60000));

  injectLocalRoutesAtRuntime({"31.23.0.0/16"}, {"3123:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.23.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.23.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3123:1"));
  injectLocalRoutesAtRuntime({"31.24.0.0/16"}, {"3124:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.24.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.24.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3124:1"));
  injectLocalRoutesAtRuntime({"31.25.0.0/16"}, {"3125:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.25.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.25.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3125:1"));

  /* Peer4 should now be DETACHED_BLOCKED via duration */
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::DETACHED_BLOCKED));
  EXPECT_TRUE(isPeerDetached(kPeerAddr4));

  /* Peer3 is still the only in-sync peer */
  EXPECT_TRUE(isPeerInSync(kPeerAddr3));
  EXPECT_EQ(getDetachedPeerCount(kPeerAddr3), 2);

  /* Verify peer3 continues receiving routes */
  injectLocalRoutesAtRuntime({"31.26.0.0/16"}, {"3126:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.26.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.26.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3126:1"));

  verifySlowPeerInvariants(kPeerAddr3);

  XLOG(INFO, "=== TEST PASSED: JrJbDrj_DurationDetachJb ===");
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
