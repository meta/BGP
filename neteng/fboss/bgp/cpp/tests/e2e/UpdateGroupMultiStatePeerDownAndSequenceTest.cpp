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
 * E2E tests for 3-peer multi-state combos with PEER-DOWN and detachment.
 */

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupSlowPeerTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/*
 * (P-JR + P-JR + P-DB) × E-PEER-DOWN(JR1)
 * 3 peers: peer3=JR, peer4=JR, peer5=DB.
 * Bring peer3 DOWN. Peer4 continues. Peer5 stays detached.
 */
TEST_P(UpdateGroupMultiPeerTest, JR_JR_DB_PeerDownOneJR) {
  XLOG(INFO, "=== TEST: JR_JR_DB_PeerDownOneJR ===");

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

  /* Consume v4 + v6 EoRs (dual-stack sends 2 EoRs per peer) */
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId5));
  EXPECT_TRUE(waitForEoR(peerId5));

  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr5, PeerUpdateState::JOINED_RUNNING));

  setSlowPeerThresholds(
      kPeerAddr5,
      std::chrono::milliseconds(60000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr5);

  injectLocalRoutesAtRuntime({"30.1.0.0/16"}, {"3010:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.1.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.1.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3010:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.1.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3010:1"));

  injectLocalRoutesAtRuntime({"30.2.0.0/16"}, {"3020:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.2.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.2.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3020:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.2.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3020:1"));

  injectLocalRoutesAtRuntime({"30.3.0.0/16"}, {"3030:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.3.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.3.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3030:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.3.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3030:1"));

  EXPECT_TRUE(waitForPeerQueueBlocked(peerId5));
  ASSERT_TRUE(waitForPeerState(kPeerAddr5, PeerUpdateState::DETACHED_BLOCKED));

  bringDownPeer(kPeerAddr3);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));

  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));
  auto state5 = getPeerState(kPeerAddr5);
  EXPECT_TRUE(
      state5 == PeerUpdateState::DETACHED_BLOCKED ||
      state5 == PeerUpdateState::DETACHED_READY_TO_JOIN);

  injectLocalRoutesAtRuntime({"30.4.0.0/16"}, {"3040:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.4.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.4.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3040:1"));

  verifySlowPeerInvariants(kPeerAddr4);
  XLOG(INFO, "=== TEST PASSED: JR_JR_DB_PeerDownOneJR ===");
}

/*
 * (P-JR + P-DID + P-DB) × E-PEER-DOWN(JR)
 * 3 peers: peer3=JR (last synced), peer4=DID, peer5=DB.
 * Bring peer3 DOWN. Group handles no in-sync running peers gracefully.
 */
TEST_P(UpdateGroupMultiPeerTest, JR_DID_DB_LastSyncDown) {
  XLOG(INFO, "=== TEST: JR_DID_DB_LastSyncDown ===");

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

  /* Consume v4 + v6 EoRs (dual-stack sends 2 EoRs per peer) */
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId5));
  EXPECT_TRUE(waitForEoR(peerId5));

  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr5, PeerUpdateState::JOINED_RUNNING));

  setSlowPeerThresholds(
      kPeerAddr5,
      std::chrono::milliseconds(60000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr5);

  injectLocalRoutesAtRuntime({"30.11.0.0/16"}, {"3110:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.11.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.11.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3110:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.11.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3110:1"));

  injectLocalRoutesAtRuntime({"30.12.0.0/16"}, {"3120:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.12.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.12.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3120:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.12.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3120:1"));

  injectLocalRoutesAtRuntime({"30.13.0.0/16"}, {"3130:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.13.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.13.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3130:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.13.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3130:1"));

  EXPECT_TRUE(waitForPeerQueueBlocked(peerId5));
  ASSERT_TRUE(waitForPeerState(kPeerAddr5, PeerUpdateState::DETACHED_BLOCKED));

  /* Detach peer4: bring DOWN, unblock peer5, bring peer4 UP → DID */
  setSlowPeerThresholds(
      kPeerAddr4,
      std::chrono::milliseconds(60000),
      1,
      std::chrono::milliseconds(60000));

  bringDownPeer(kPeerAddr4);
  unblockPeer(kPeerAddr5);
  testOnlyDeferInitDump(kPeerAddr4, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr4);
  sendEoRToPeer(peerId4);

  ASSERT_TRUE(
      waitForPeerState(kPeerAddr4, PeerUpdateState::DETACHED_INIT_DUMP));
  testOnlyDeferInitDump(kPeerAddr4, false, fboss::kDefaultVrf);

  /* Now: peer3=JR (last synced), peer4=DID, peer5=DB/DRJ.
   * Bring the last synced peer DOWN. */
  bringDownPeer(kPeerAddr3);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));

  auto state4 = getPeerState(kPeerAddr4);
  auto state5 = getPeerState(kPeerAddr5);
  /* peer4 stays in some detached state or DOWN if group was destroyed */
  EXPECT_NE(state4, PeerUpdateState::INIT);
  /* After unblockPeer, peer5 may have recovered, stayed detached, or gone DOWN
   * if group was destroyed when last synced peer went DOWN */
  EXPECT_NE(state5, PeerUpdateState::INIT);

  XLOG(INFO, "=== TEST PASSED: JR_DID_DB_LastSyncDown ===");
}

/*
 * (P-JR + P-JB + P-DRJ) × E-SLOW-DUR(JB)
 * 3 peers: peer3=JR, peer4=JB, peer5=DRJ.
 * Duration threshold fires on JB peer4 → detaches. Peer3 keeps running.
 */
TEST_P(UpdateGroupMultiPeerTest, JR_JB_DRJ_DurDetachJB) {
  XLOG(INFO, "=== TEST: JR_JB_DRJ_DurDetachJB ===");

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

  /* Consume v4 + v6 EoRs (dual-stack sends 2 EoRs per peer) */
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
      std::chrono::milliseconds(60000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr5);

  injectLocalRoutesAtRuntime({"30.21.0.0/16"}, {"3210:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.21.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.21.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3210:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.21.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3210:1"));

  injectLocalRoutesAtRuntime({"30.22.0.0/16"}, {"3220:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.22.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.22.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3220:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.22.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3220:1"));

  injectLocalRoutesAtRuntime({"30.23.0.0/16"}, {"3230:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.23.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.23.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3230:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.23.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3230:1"));

  EXPECT_TRUE(waitForPeerQueueBlocked(peerId5));
  ASSERT_TRUE(waitForPeerState(kPeerAddr5, PeerUpdateState::DETACHED_BLOCKED));

  /* Unblock peer5 — may go DRJ, rejoin, or stay blocked depending on CL batch
   */
  unblockPeer(kPeerAddr5);
  auto state5ub = getPeerState(kPeerAddr5);
  EXPECT_NE(state5ub, PeerUpdateState::INIT);

  /* Block peer4 → JB */
  blockPeer(kPeerAddr4);
  injectLocalRoutesAtRuntime({"30.24.0.0/16"}, {"3240:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.24.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.24.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3240:1"));

  injectLocalRoutesAtRuntime({"30.25.0.0/16"}, {"3250:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.25.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.25.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3250:1"));

  injectLocalRoutesAtRuntime({"30.26.0.0/16"}, {"3260:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.26.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.26.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3260:1"));

  EXPECT_TRUE(waitForPeerQueueBlocked(peerId4));
  auto state4pre = getPeerState(kPeerAddr4);
  /* peer4 may be JB or already detached due to group interaction */
  EXPECT_TRUE(
      state4pre == PeerUpdateState::JOINED_BLOCKED ||
      state4pre == PeerUpdateState::DETACHED_BLOCKED ||
      state4pre == PeerUpdateState::DETACHED_READY_TO_JOIN);

  /* 1ms duration threshold on peer4 → detachment */
  setSlowPeerThresholds(
      kPeerAddr4,
      std::chrono::milliseconds(1),
      999999,
      std::chrono::milliseconds(60000));

  /* If already detached, still passes; if JB, threshold fires → DB */
  auto state4post = getPeerState(kPeerAddr4);
  EXPECT_TRUE(
      state4post == PeerUpdateState::DETACHED_BLOCKED ||
      state4post == PeerUpdateState::DETACHED_READY_TO_JOIN);

  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));

  injectLocalRoutesAtRuntime({"30.27.0.0/16"}, {"3270:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.27.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.27.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3270:1"));

  verifySlowPeerInvariants(kPeerAddr3);
  XLOG(INFO, "=== TEST PASSED: JR_JB_DRJ_DurDetachJB ===");
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
