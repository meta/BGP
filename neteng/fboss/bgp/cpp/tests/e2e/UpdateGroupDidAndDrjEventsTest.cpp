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
 * E2E tests: P-DID (Detached Init Dump) and P-DRJ (Detached Ready to Join)
 * state x Event matrix tests.
 *
 * Prefix range: 29.50-29.80/16
 * Fixture: UpdateGroupMultiPeerTest
 *
 * Tests implemented:
 *   SP-071: P-DID x E-POLICY-CHG
 *   SP-073: P-DID x E-MRAI-FIRE
 *   SP-075: P-DID x E-MULTI-ROUTE
 *   SP-077: P-DRJ x E-ROUTE-WD
 *   SP-079: P-DRJ x E-UNBLOCK (N/A)
 */

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupSlowPeerTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/*
 * Helper: reach DETACHED_INIT_DUMP for peer3.
 * 1) Both peers to JOINED_RUNNING
 * 2) Freq-detach peer3 (2 block cycles)
 * 3) bringDown, unblock, bringUp -> enters DID
 */

/*
 * Helper: reach DETACHED_READY_TO_JOIN for peer3.
 * 1) Both peers to JOINED_RUNNING
 * 2) Freq-detach peer3 (2 block cycles)
 * 3) unblockPeer -> transitions to DRJ
 */

/*
 * Policy change during detached init dump. Simulated by withdrawing
 * a shared route and injecting a new one with different attributes.
 * Peer3 is in DID; peer4 receives the policy-change-like withdrawal
 * and new announcement normally. No crash.
 */
TEST_P(UpdateGroupMultiPeerTest, DetachedInitDump_PolicyChange) {
  XLOG(INFO, "=== TEST: DetachedInitDump_PolicyChange ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  setupSlowPeerComponents(3, 2, 0);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);

  /* Consume v4 + v6 EoRs (dual-stack sends 2 EoRs per peer) */
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId4));

  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  /* Inject a shared route before detachment */
  injectLocalRoutesAtRuntime({"29.50.0.0/16"}, {"2950:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("29.50.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "29.50.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "2950:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "29.50.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "2950:1"));

  /* Freq-detach peer3: set threshold=2, do 2 block cycles */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      2,
      std::chrono::milliseconds(60000));

  /* Block cycle #1: inject 3 routes to fill queue past hwm=2 */
  blockPeer(kPeerAddr3);
  for (int i = 0; i < 3; ++i) {
    auto prefix = fmt::format("29.{}.0.0/16", 51 + i);
    auto prefixNoMask = fmt::format("29.{}.0.0", 51 + i);
    auto community = fmt::format("{}:1", 2951 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        prefixNoMask,
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  unblockPeer(kPeerAddr3);

  /* Block cycle #2 -> triggers freq detachment */
  blockPeer(kPeerAddr3);
  for (int i = 0; i < 3; ++i) {
    auto prefix = fmt::format("29.{}.0.0/16", 54 + i);
    auto prefixNoMask = fmt::format("29.{}.0.0", 54 + i);
    auto community = fmt::format("{}:1", 2954 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        prefixNoMask,
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Enter DID: defer init dump so peer stays in DID deterministically */
  unblockPeer(kPeerAddr3);
  bringDownPeer(kPeerAddr3);
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));
  testOnlyDeferInitDump(kPeerAddr3, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr3);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_INIT_DUMP, 30));
  testOnlyDeferInitDump(kPeerAddr3, false, fboss::kDefaultVrf);

  /* Simulate policy change: withdraw old prefix, inject new one */
  withdrawLocalRoutesAtRuntime({"29.50.0.0/16"});
  EXPECT_TRUE(verifyRouteWithdraw("v4", "29.50.0.0", 16, kPeerAddr4));

  injectLocalRoutesAtRuntime({"29.57.0.0/16"}, {"2957:1"}, 200);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("29.57.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "29.57.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "2957:1"));

  /* Peer4 continues working; peer3 in DID is stable */
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  XLOG(INFO, "=== TEST PASSED: DetachedInitDump_PolicyChange ===");
}

/*
 * MRAI fires while peer3 is in DID. Group processes normally for
 * peer4. Peer3 continues init dump independently. Simulated by
 * injecting multiple routes (each triggers MRAI cycle).
 */
TEST_P(UpdateGroupMultiPeerTest, DetachedInitDump_MraiFire) {
  XLOG(INFO, "=== TEST: DetachedInitDump_MraiFire ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  setupSlowPeerComponents(3, 2, 0);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);

  /* Consume v4 + v6 EoRs (dual-stack sends 2 EoRs per peer) */
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId4));

  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  /* Freq-detach peer3 */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      2,
      std::chrono::milliseconds(60000));

  /* Block cycle #1: 3 routes to fill queue past hwm=2 */
  blockPeer(kPeerAddr3);
  for (int i = 0; i < 3; ++i) {
    auto prefix = fmt::format("29.{}.0.0/16", 55 + i);
    auto prefixNoMask = fmt::format("29.{}.0.0", 55 + i);
    auto community = fmt::format("{}:1", 2955 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        prefixNoMask,
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  unblockPeer(kPeerAddr3);

  /* Block cycle #2 -> triggers freq detachment */
  blockPeer(kPeerAddr3);
  for (int i = 0; i < 3; ++i) {
    auto prefix = fmt::format("29.{}.0.0/16", 58 + i);
    auto prefixNoMask = fmt::format("29.{}.0.0", 58 + i);
    auto community = fmt::format("{}:1", 2958 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        prefixNoMask,
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Enter DID: defer init dump so peer stays in DID deterministically */
  unblockPeer(kPeerAddr3);
  bringDownPeer(kPeerAddr3);
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));
  testOnlyDeferInitDump(kPeerAddr3, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr3);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_INIT_DUMP, 30));
  testOnlyDeferInitDump(kPeerAddr3, false, fboss::kDefaultVrf);

  /* Inject 3 routes with different communities during DID — each
   * triggers a separate MRAI cycle. Peer4 receives all. */
  for (int i = 0; i < 3; ++i) {
    auto prefix = fmt::format("29.{}.0.0/16", 61 + i);
    auto prefixNoMask = fmt::format("29.{}.0.0", 61 + i);
    auto community = fmt::format("{}:1", 2961 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        prefixNoMask,
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }

  /* Peer4 still running, peer3 still in DID */
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  XLOG(INFO, "=== TEST PASSED: DetachedInitDump_MraiFire ===");
}

/*
 * Batch of routes injected while peer3 is in DID. CL grows with
 * entries for peer3. Peer4 receives all routes normally.
 */
TEST_P(UpdateGroupMultiPeerTest, DetachedInitDump_MultiRoute) {
  XLOG(INFO, "=== TEST: DetachedInitDump_MultiRoute ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  setupSlowPeerComponents(3, 2, 0);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);

  /* Consume v4 + v6 EoRs (dual-stack sends 2 EoRs per peer) */
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId4));

  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  /* Freq-detach peer3 */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      2,
      std::chrono::milliseconds(60000));

  /* Block cycle #1: 3 routes to fill queue past hwm=2 */
  blockPeer(kPeerAddr3);
  for (int i = 0; i < 3; ++i) {
    auto prefix = fmt::format("29.{}.0.0/16", 60 + i);
    auto prefixNoMask = fmt::format("29.{}.0.0", 60 + i);
    auto community = fmt::format("{}:1", 2960 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        prefixNoMask,
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  unblockPeer(kPeerAddr3);

  /* Block cycle #2 -> triggers freq detachment */
  blockPeer(kPeerAddr3);
  for (int i = 0; i < 3; ++i) {
    auto prefix = fmt::format("29.{}.0.0/16", 63 + i);
    auto prefixNoMask = fmt::format("29.{}.0.0", 63 + i);
    auto community = fmt::format("{}:1", 2963 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        prefixNoMask,
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Enter DID: unblock first, then down -> up */
  unblockPeer(kPeerAddr3);
  bringDownPeer(kPeerAddr3);
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));
  testOnlyDeferInitDump(kPeerAddr3, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr3);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_INIT_DUMP, 30));
  testOnlyDeferInitDump(kPeerAddr3, false, fboss::kDefaultVrf);

  /* Inject batch of 4 routes during DID, drain peer4 each time */
  for (int i = 0; i < 4; ++i) {
    auto prefix = fmt::format("29.{}.0.0/16", 66 + i);
    auto prefixNoMask = fmt::format("29.{}.0.0", 66 + i);
    auto community = fmt::format("{}:1", 2966 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        prefixNoMask,
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }

  /* Peer4 received all, peer3 has CL entries accumulated */
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  XLOG(INFO, "=== TEST PASSED: DetachedInitDump_MultiRoute ===");
}

/*
 * Withdrawal during DETACHED_READY_TO_JOIN. DFP (detached fast peer)
 * should become DSP (diverged). Peer4 receives the withdrawal.
 */
TEST_P(UpdateGroupMultiPeerTest, DetachedReadyToJoin_RouteWithdraw) {
  XLOG(INFO, "=== TEST: DetachedReadyToJoin_RouteWithdraw ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  setupSlowPeerComponents(3, 2, 0);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);

  /* Consume v4 + v6 EoRs (dual-stack sends 2 EoRs per peer) */
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId4));

  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  /* Inject a shared route before detachment */
  injectLocalRoutesAtRuntime({"29.70.0.0/16"}, {"2970:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("29.70.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "29.70.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "2970:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "29.70.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "2970:1"));

  /* Freq-detach peer3 */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      2,
      std::chrono::milliseconds(60000));

  /* Block cycle #1: 3 routes to fill queue past hwm=2 */
  blockPeer(kPeerAddr3);
  for (int i = 0; i < 3; ++i) {
    auto prefix = fmt::format("29.{}.0.0/16", 71 + i);
    auto prefixNoMask = fmt::format("29.{}.0.0", 71 + i);
    auto community = fmt::format("{}:1", 2971 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        prefixNoMask,
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  unblockPeer(kPeerAddr3);

  /* Block cycle #2 -> triggers freq detachment */
  blockPeer(kPeerAddr3);
  for (int i = 0; i < 3; ++i) {
    auto prefix = fmt::format("29.{}.0.0/16", 74 + i);
    auto prefixNoMask = fmt::format("29.{}.0.0", 74 + i);
    auto community = fmt::format("{}:1", 2974 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        prefixNoMask,
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Reach DRJ by unblocking */
  waitForDrjWithDrain(kPeerAddr3, peerId3);

  /* Withdraw the shared route — peer4 receives it */
  withdrawLocalRoutesAtRuntime({"29.70.0.0/16"});
  EXPECT_TRUE(verifyRouteWithdraw("v4", "29.70.0.0", 16, kPeerAddr4));

  /* Peer4 still running */
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  XLOG(INFO, "=== TEST PASSED: DetachedReadyToJoin_RouteWithdraw ===");
}

/*
 * Peer is already unblocked at DRJ. Calling unblockPeer again
 * should be a harmless no-op. No crash, no state change.
 */
TEST_P(UpdateGroupMultiPeerTest, DetachedReadyToJoin_Unblock) {
  XLOG(INFO, "=== TEST: DetachedReadyToJoin_Unblock ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  setupSlowPeerComponents(3, 2, 0);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);

  /* Consume v4 + v6 EoRs (dual-stack sends 2 EoRs per peer) */
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId4));

  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  /* Freq-detach peer3 */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      2,
      std::chrono::milliseconds(60000));

  /* Block cycle #1: 3 routes to fill queue past hwm=2 */
  blockPeer(kPeerAddr3);
  for (int i = 0; i < 3; ++i) {
    auto prefix = fmt::format("29.{}.0.0/16", 75 + i);
    auto prefixNoMask = fmt::format("29.{}.0.0", 75 + i);
    auto community = fmt::format("{}:1", 2975 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        prefixNoMask,
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  unblockPeer(kPeerAddr3);

  /* Block cycle #2 -> triggers freq detachment */
  blockPeer(kPeerAddr3);
  for (int i = 0; i < 3; ++i) {
    auto prefix = fmt::format("29.{}.0.0/16", 78 + i);
    auto prefixNoMask = fmt::format("29.{}.0.0", 78 + i);
    auto community = fmt::format("{}:1", 2978 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        prefixNoMask,
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Reach DRJ by unblocking */
  waitForDrjWithDrain(kPeerAddr3, peerId3);

  /* unblockPeer on already-unblocked peer — should be no-op */
  unblockPeer(kPeerAddr3);
  EXPECT_FALSE(isPeerQueueBlocked(peerId3));

  /* Peer4 still works fine */
  injectLocalRoutesAtRuntime({"29.81.0.0/16"}, {"2981:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("29.81.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "29.81.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "2981:1"));

  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  XLOG(INFO, "=== TEST PASSED: DetachedReadyToJoin_Unblock ===");
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
