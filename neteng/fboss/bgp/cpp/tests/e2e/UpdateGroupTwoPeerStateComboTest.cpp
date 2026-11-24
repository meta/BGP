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

/* Two-peer state combo tests.
 * Stack A prefix range: 30.x.0.0/16.
 * Stack B prefix range: 86-87.x.0.0/16.
 */

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupSlowPeerTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/* (P-DB + P-DRJ) x E-ROUTE-ADD - 0 in-sync, CL grows */
TEST_P(UpdateGroupMultiPeerTest, DB_DRJ_RouteAdd) {
  XLOG(INFO, "=== TEST: DB_DRJ_RouteAdd ===");

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
  /* Detach peer3 via freq threshold (1 block = detach) */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(60000),
      1,
      std::chrono::milliseconds(60000));
  blockPeer(kPeerAddr3);
  injectLocalRoutesAtRuntime({"30.1.0.0/16"}, {"3001:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.1.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.1.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3001:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.1.0.0",
      16,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "3001:1"));
  injectLocalRoutesAtRuntime({"30.2.0.0/16"}, {"3002:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.2.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.2.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3002:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.2.0.0",
      16,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "3002:1"));
  injectLocalRoutesAtRuntime({"30.3.0.0/16"}, {"3003:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.3.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.3.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3003:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.3.0.0",
      16,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "3003:1"));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));
  /* Detach peer4 via freq threshold (1 block = detach) */
  setSlowPeerThresholds(
      kPeerAddr4,
      std::chrono::milliseconds(60000),
      1,
      std::chrono::milliseconds(60000));
  blockPeer(kPeerAddr4);
  injectLocalRoutesAtRuntime({"30.4.0.0/16"}, {"3004:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.4.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.4.0.0",
      16,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "3004:1"));
  injectLocalRoutesAtRuntime({"30.5.0.0/16"}, {"3005:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.5.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.5.0.0",
      16,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "3005:1"));
  injectLocalRoutesAtRuntime({"30.6.0.0/16"}, {"3006:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.6.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.6.0.0",
      16,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "3006:1"));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::DETACHED_BLOCKED));
  /* Populate CL while peer4 is detached — prevents immediate recovery */
  injectLocalRoutesAtRuntime({"30.6.1.0/24"}, {"30611:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.6.1.0/24")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.6.1.0",
      24,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "30611:1"));
  injectLocalRoutesAtRuntime({"30.6.2.0/24"}, {"30612:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.6.2.0/24")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.6.2.0",
      24,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "30612:1"));
  injectLocalRoutesAtRuntime({"30.6.3.0/24"}, {"30613:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.6.3.0/24")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.6.3.0",
      24,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "30613:1"));
  /* Now unblock peer4 to get DRJ — CL has 3 items */
  unblockPeer(kPeerAddr4);
  /* After unblock, peer4 may be actively consuming (DETACHED_RUNNING),
   * still blocked (DETACHED_BLOCKED), ready to join (DRJ), or already
   * recovered to JOINED_RUNNING via DFP fast path */
  auto state4 = getPeerState(kPeerAddr4);
  EXPECT_TRUE(
      state4 == PeerUpdateState::DETACHED_BLOCKED ||
      state4 == PeerUpdateState::DETACHED_READY_TO_JOIN ||
      state4 == PeerUpdateState::DETACHED_RUNNING ||
      state4 == PeerUpdateState::JOINED_RUNNING);

  /* Inject route — peer5 receives (always in-sync) */
  injectLocalRoutesAtRuntime({"30.7.0.0/16"}, {"3007:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.7.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.7.0.0",
      16,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "3007:1"));

  /* Verify state */
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));
  ASSERT_TRUE(waitForPeerState(kPeerAddr5, PeerUpdateState::JOINED_RUNNING));
  verifySlowPeerInvariants(kPeerAddr5);

  XLOG(INFO, "=== TEST PASSED: DB_DRJ_RouteAdd ===");
}

/* (P-JR + P-DRJ) x E-ROUTE-WD - withdrawal, peer3 gets it */
TEST_P(UpdateGroupMultiPeerTest, JR_DRJ_RouteWithdraw) {
  XLOG(INFO, "=== TEST: JR_DRJ_RouteWithdraw ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  /* Small queue (3, 2, 0) so blockPeer + 3 routes fills > highWm */
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
  injectLocalRoutesAtRuntime({"30.10.0.0/16"}, {"3010:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.10.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.10.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3010:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.10.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3010:1"));

  /* Detach peer4 via freq threshold + blockPeer + queue fill */
  setSlowPeerThresholds(
      kPeerAddr4,
      std::chrono::milliseconds(60000),
      1,
      std::chrono::milliseconds(60000));
  blockPeer(kPeerAddr4);
  injectLocalRoutesAtRuntime({"30.11.0.0/16"}, {"3011:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.11.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.11.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3011:1"));
  injectLocalRoutesAtRuntime({"30.12.0.0/16"}, {"3012:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.12.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.12.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3012:1"));
  injectLocalRoutesAtRuntime({"30.13.0.0/16"}, {"3013:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.13.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.13.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3013:1"));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::DETACHED_BLOCKED));
  /* Populate CL while peer4 is detached — prevents immediate recovery */
  injectLocalRoutesAtRuntime({"30.14.0.0/16"}, {"3014:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.14.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.14.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3014:1"));
  injectLocalRoutesAtRuntime({"30.15.0.0/16"}, {"3015:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.15.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.15.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3015:1"));
  injectLocalRoutesAtRuntime({"30.16.0.0/16"}, {"3016:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.16.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.16.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3016:1"));
  /* Unblock to get DRJ — CL has 3 items */
  unblockPeer(kPeerAddr4);

  /* Withdraw the shared route */
  withdrawLocalRoutesAtRuntime({"30.10.0.0/16"});
  EXPECT_TRUE(verifyRouteWithdraw("v4", "30.10.0.0", 16, kPeerAddr3));

  /* Verify state -- peer4 may still be detached or may have recovered via DSP
   */
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  auto state4 = getPeerState(kPeerAddr4);
  EXPECT_TRUE(
      isPeerDetached(kPeerAddr4) || state4 == PeerUpdateState::JOINED_RUNNING);
  verifySlowPeerInvariants(kPeerAddr3);

  XLOG(INFO, "=== TEST PASSED: JR_DRJ_RouteWithdraw ===");
}

/* (P-DID + P-DRJ) x E-ROUTE-ADD - peer5 is in-sync verifier */
TEST_P(UpdateGroupMultiPeerTest, DID_DRJ_RouteAdd) {
  XLOG(INFO, "=== TEST: DID_DRJ_RouteAdd ===");

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

  /* Detach peer3 via freq threshold */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(60000),
      1,
      std::chrono::milliseconds(60000));
  blockPeer(kPeerAddr3);
  injectLocalRoutesAtRuntime({"30.20.0.0/16"}, {"3020:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.20.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.20.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3020:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.20.0.0",
      16,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "3020:1"));
  injectLocalRoutesAtRuntime({"30.21.0.0/16"}, {"3021:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.21.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.21.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3021:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.21.0.0",
      16,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "3021:1"));
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
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.22.0.0",
      16,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "3022:1"));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Bring peer3 down then back up to enter DETACHED_INIT_DUMP */
  unblockPeer(kPeerAddr3);
  bringDownPeer(kPeerAddr3);
  testOnlyDeferInitDump(kPeerAddr3, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_INIT_DUMP));
  testOnlyDeferInitDump(kPeerAddr3, false, fboss::kDefaultVrf);

  /* Detach peer4 via freq threshold */
  setSlowPeerThresholds(
      kPeerAddr4,
      std::chrono::milliseconds(60000),
      1,
      std::chrono::milliseconds(60000));
  blockPeer(kPeerAddr4);
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
  injectLocalRoutesAtRuntime({"30.25.0.0/16"}, {"3025:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.25.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.25.0.0",
      16,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "3025:1"));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::DETACHED_BLOCKED));

  /* Unblock peer4 to get DRJ */
  unblockPeer(kPeerAddr4);

  /* Inject route - peer5 receives */
  injectLocalRoutesAtRuntime({"30.26.0.0/16"}, {"3026:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.26.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.26.0.0",
      16,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "3026:1"));

  /* Verify state */
  ASSERT_TRUE(waitForPeerState(kPeerAddr5, PeerUpdateState::JOINED_RUNNING));
  verifySlowPeerInvariants(kPeerAddr5);

  XLOG(INFO, "=== TEST PASSED: DID_DRJ_RouteAdd ===");
}

/* (P-JB + P-DRJ) x E-UNBLOCK(JB) - group resumes */
TEST_P(UpdateGroupMultiPeerTest, JB_DRJ_UnblockJB) {
  XLOG(INFO, "=== TEST: JB_DRJ_UnblockJB ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  /* All peers: queue (3, 2, 0) — small enough for blockPeer detachment */
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

  /* Detach peer4 via freq threshold (capacity=3, hwm=2) */
  setSlowPeerThresholds(
      kPeerAddr4,
      std::chrono::milliseconds(60000),
      1,
      std::chrono::milliseconds(60000));
  blockPeer(kPeerAddr4);
  injectLocalRoutesAtRuntime({"30.40.0.0/16"}, {"3040:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.40.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.40.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3040:1"));
  injectLocalRoutesAtRuntime({"30.41.0.0/16"}, {"3041:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.41.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.41.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3041:1"));
  injectLocalRoutesAtRuntime({"30.42.0.0/16"}, {"3042:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.42.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.42.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3042:1"));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::DETACHED_BLOCKED));

  /* Unblock peer4 to get DRJ */
  unblockPeer(kPeerAddr4);

  /* Now block peer3 to get JB -- inject 3 routes to fill queue (capacity=3) */
  blockPeer(kPeerAddr3);
  injectLocalRoutesAtRuntime({"30.45.0.0/16"}, {"3045:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.45.0.0/16")));
  injectLocalRoutesAtRuntime({"30.46.0.0/16"}, {"3046:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.46.0.0/16")));
  injectLocalRoutesAtRuntime({"30.47.0.0/16"}, {"3047:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.47.0.0/16")));
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_BLOCKED));

  /* Unblock peer3 -- this drains queued messages automatically */
  unblockPeer(kPeerAddr3);

  /* Peer3 should recover to JOINED_RUNNING after queue drain */
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));

  /* Verify recovery */
  injectLocalRoutesAtRuntime({"30.50.0.0/16"}, {"3050:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.50.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.50.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3050:1"));
  verifySlowPeerInvariants(kPeerAddr3);

  XLOG(INFO, "=== TEST PASSED: JB_DRJ_UnblockJB ===");
}

/*
 * (P-JR + P-DB) x E-ROUTE-ADD
 * Peer3 is DETACHED_BLOCKED, peer4 is JOINED_RUNNING.
 * Route injection: peer4 receives normally, peer3's CL accumulates.
 */
TEST_P(UpdateGroupMultiPeerTest, JrPlusDb_RouteAdd) {
  XLOG(INFO, "=== TEST: JrPlusDb_RouteAdd ===");

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

  /* Freq-detach peer3 -> DETACHED_BLOCKED */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(60000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  for (int i = 0; i < 3; i++) {
    auto prefix = fmt::format("86.{}.0.0/16", 1 + i);
    auto community = fmt::format("{}:1", 8601 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("86.{}.0.0", 1 + i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* State: peer3=DB, peer4=JR. Inject route -- peer4 receives, CL for peer3 */
  injectLocalRoutesAtRuntime({"86.10.0.0/16"}, {"8610:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("86.10.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "86.10.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "8610:1"));

  /* Verify states unchanged */
  EXPECT_TRUE(isPeerDetached(kPeerAddr3));
  EXPECT_TRUE(isPeerInSync(kPeerAddr4));
  verifySlowPeerInvariants(kPeerAddr3);

  XLOG(INFO, "=== TEST PASSED: JrPlusDb_RouteAdd ===");
}

/*
 * (P-JR + P-DRJ) x E-ROUTE-ADD
 * Peer3 freq-detached then unblocked (recovery in progress), peer4 is JR.
 * Route injection: peer4 receives, peer3's CL grows (DFP->DSP if new items).
 */
TEST_P(UpdateGroupMultiPeerTest, JrPlusDrj_RouteAdd) {
  XLOG(INFO, "=== TEST: JrPlusDrj_RouteAdd ===");

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

  /* Freq-detach peer3 -> DETACHED_BLOCKED */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(60000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  for (int i = 0; i < 3; i++) {
    auto prefix = fmt::format("86.{}.0.0/16", 20 + i);
    auto community = fmt::format("{}:1", 8620 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("86.{}.0.0", 20 + i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Unblock -> peer3 starts recovery (DRJ/DSP path) */
  unblockPeer(kPeerAddr3);
  auto state3 = getPeerState(kPeerAddr3);
  EXPECT_NE(state3, PeerUpdateState::DOWN);

  /* State: peer3=recovering (non-DOWN detached), peer4=JR */
  injectLocalRoutesAtRuntime({"86.30.0.0/16"}, {"8630:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("86.30.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "86.30.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "8630:1"));

  /* Peer4 in sync, peer3 not DOWN */
  EXPECT_TRUE(isPeerInSync(kPeerAddr4));
  EXPECT_NE(getPeerState(kPeerAddr3), PeerUpdateState::DOWN);

  XLOG(INFO, "=== TEST PASSED: JrPlusDrj_RouteAdd ===");
}

/*
 * (P-JR + P-DID) x E-ROUTE-ADD
 * Peer3 in DETACHED_INIT_DUMP (reconnected after detach), peer4 is JR.
 * Route injection: peer4 receives, CL item appended for peer3.
 */
TEST_P(UpdateGroupMultiPeerTest, JrPlusDid_RouteAdd) {
  XLOG(INFO, "=== TEST: JrPlusDid_RouteAdd ===");

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

  /* Freq-detach peer3 */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(60000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  for (int i = 0; i < 3; i++) {
    auto prefix = fmt::format("86.{}.0.0/16", 40 + i);
    auto community = fmt::format("{}:1", 8640 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("86.{}.0.0", 40 + i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Down -> unblock+up -> DETACHED_INIT_DUMP */
  bringDownPeer(kPeerAddr3);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));
  unblockPeer(kPeerAddr3);
  testOnlyDeferInitDump(kPeerAddr3, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_INIT_DUMP, 30));
  testOnlyDeferInitDump(kPeerAddr3, false, fboss::kDefaultVrf);

  /* State: peer3=DID, peer4=JR. Inject route -- peer4 receives, CL for peer3
   */
  injectLocalRoutesAtRuntime({"86.50.0.0/16"}, {"8650:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("86.50.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "86.50.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "8650:1"));

  EXPECT_TRUE(isPeerInSync(kPeerAddr4));

  XLOG(INFO, "=== TEST PASSED: JrPlusDid_RouteAdd ===");
}

/*
 * (P-JB + P-DB) x E-ROUTE-ADD (3-peer test)
 * Peer3=JB (blocked, not detached), peer4=DB (detached+blocked),
 * peer5=JR (in-sync verifier).
 * Group is WAITING (peer3 blocks PL drain). New route goes to CL.
 * After unblocking peer3, peer5 receives the CL-origin route.
 */
TEST_P(UpdateGroupMultiPeerTest, JbPlusDb_RouteAdd) {
  XLOG(INFO, "=== TEST: JbPlusDb_RouteAdd ===");

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

  /*
   * Detach peer4: set freq=1, block+fill -> DB.
   * Must drain peer3 AND peer5 after each fill route.
   */
  setSlowPeerThresholds(
      kPeerAddr4,
      std::chrono::milliseconds(60000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr4);
  for (int i = 0; i < 3; i++) {
    auto prefix = fmt::format("86.{}.0.0/16", 60 + i);
    auto community = fmt::format("{}:1", 8660 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("86.{}.0.0", 60 + i),
        16,
        kPeerAddr3,
        getExpectedNexthop(kPeerAddr3),
        "4200000001",
        community));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("86.{}.0.0", 60 + i),
        16,
        kPeerAddr5,
        getExpectedNexthop(kPeerAddr5),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId4));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::DETACHED_BLOCKED));

  /* Raise threshold so peer3 won't detach when blocked */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(60000),
      999999,
      std::chrono::milliseconds(60000));

  /* Block peer3 -> JOINED_BLOCKED. Group enters WAITING. */
  blockPeer(kPeerAddr3);
  injectLocalRoutesAtRuntime({"86.70.0.0/16"}, {"8670:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("86.70.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "86.70.0.0",
      16,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "8670:1"));
  injectLocalRoutesAtRuntime({"86.71.0.0/16"}, {"8671:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("86.71.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "86.71.0.0",
      16,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "8671:1"));
  injectLocalRoutesAtRuntime({"86.72.0.0/16"}, {"8672:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("86.72.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "86.72.0.0",
      16,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "8672:1"));
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_BLOCKED));

  /*
   * State: peer3=JB, peer4=DB, peer5=JR. Group is WAITING.
   * Inject new route -> goes to CL (cannot verify delivery while WAITING).
   */
  injectLocalRoutesAtRuntime({"86.80.0.0/16"}, {"8680:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("86.80.0.0/16")));

  /* Verify states stable */
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_BLOCKED));
  EXPECT_TRUE(isPeerDetached(kPeerAddr4));

  /* Unblock peer3 -> PL drains -> CL processes -> peer5 receives CL-origin
   * route
   */
  unblockPeer(kPeerAddr3);

  /* Consume the CL-origin route on peer5 (was queued during WAITING) */
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "86.80.0.0",
      16,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "8680:1"));

  EXPECT_TRUE(isPeerInSync(kPeerAddr5));

  XLOG(INFO, "=== TEST PASSED: JbPlusDb_RouteAdd ===");
}

/*
 * (P-JB + P-DRJ) x E-ROUTE-ADD (3-peer test)
 * Peer3=JB, peer4=recovering (unblocked after detach), peer5=JR.
 * Group WAITING due to peer3 blocked. Route to CL, DFP->DSP for peer4.
 *
 * Order: detach peer4 -> raise threshold -> block peer3 (JB) -> unblock peer4
 * (starts recovery) -> inject route -> unblock peer3 -> verify peer5 receives.
 */
TEST_P(UpdateGroupMultiPeerTest, JbPlusDrj_RouteAdd) {
  XLOG(INFO, "=== TEST: JbPlusDrj_RouteAdd ===");

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

  /* Detach peer4: freq=1, block+fill -> DB */
  setSlowPeerThresholds(
      kPeerAddr4,
      std::chrono::milliseconds(60000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr4);
  for (int i = 0; i < 3; i++) {
    auto prefix = fmt::format("86.{}.0.0/16", 90 + i);
    auto community = fmt::format("{}:1", 8690 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("86.{}.0.0", 90 + i),
        16,
        kPeerAddr3,
        getExpectedNexthop(kPeerAddr3),
        "4200000001",
        community));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("86.{}.0.0", 90 + i),
        16,
        kPeerAddr5,
        getExpectedNexthop(kPeerAddr5),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId4));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::DETACHED_BLOCKED));

  /* Raise threshold so peer3 won't detach when blocked */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(60000),
      999999,
      std::chrono::milliseconds(60000));

  /* Block peer3 -> JOINED_BLOCKED BEFORE unblocking peer4 */
  blockPeer(kPeerAddr3);
  for (int i = 0; i < 3; i++) {
    auto prefix = fmt::format("86.{}.0.0/16", 100 + i);
    auto community = fmt::format("{}:1", 8700 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("86.{}.0.0", 100 + i),
        16,
        kPeerAddr5,
        getExpectedNexthop(kPeerAddr5),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_BLOCKED));

  /* Now unblock peer4 -> starts recovery (DRJ path) */
  unblockPeer(kPeerAddr4);
  auto state4 = getPeerState(kPeerAddr4);
  EXPECT_NE(state4, PeerUpdateState::DOWN);

  /*
   * State: peer3=JB, peer4=recovering (non-DOWN), peer5=JR.
   * Group is WAITING (peer3 blocks PL). Inject route -> CL item.
   */
  injectLocalRoutesAtRuntime({"86.110.0.0/16"}, {"8710:1"}, 150);
  ASSERT_TRUE(waitForRouteInShadowRib(
      folly::IPAddress::createNetwork("86.110.0.0/16")));

  /* Verify states: peer3 still JB, peer4 not DOWN */
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_BLOCKED));
  EXPECT_NE(getPeerState(kPeerAddr4), PeerUpdateState::DOWN);

  /* Unblock peer3 -> PL drains -> CL processes -> peer5 receives */
  unblockPeer(kPeerAddr3);
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "86.110.0.0",
      16,
      kPeerAddr5,
      getExpectedNexthop(kPeerAddr5),
      "4200000001",
      "8710:1"));

  EXPECT_TRUE(isPeerInSync(kPeerAddr5));

  XLOG(INFO, "=== TEST PASSED: JbPlusDrj_RouteAdd ===");
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
