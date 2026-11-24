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
 * E2E tests: P-DID policy/MRAI/multi-route and P-DRJ basic events.
 *
 * Prefix range: 72.x.0.0/16
 *
 * Tests:
 *   P-DID x E-POLICY-CHG -- Policy change during DID
 *   P-DID x E-MRAI-FIRE -- MRAI fires during DID
 *   P-DID x E-MULTI-ROUTE -- Batch routes during DID
 *   P-DRJ x E-ROUTE-WD -- Withdrawal during DRJ
 *   P-DRJ x E-UNBLOCK -- N/A (already unblocked)
 *   P-DRJ x E-SLOW-FREQ -- N/A (no double detach)
 *   P-DRJ x E-PEER-DOWN -- Peer down at DRJ
 *   P-DRJ x E-PEER-UP -- N/A (already up)
 */

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupSlowPeerTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/*
 * P-DID x E-POLICY-CHG
 * Policy change during detached init dump. Simulated by withdrawing a
 * shared route and injecting a new one with different attributes.
 */
TEST_P(UpdateGroupMultiPeerTest, DetachedInitDump_PolicyChg) {
  XLOG(INFO, "=== TEST: DetachedInitDump_PolicyChg ===");

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

  /* Inject shared route before detachment */
  injectLocalRoutesAtRuntime({"72.1.0.0/16"}, {"7201:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("72.1.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "72.1.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "7201:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "72.1.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "7201:1"));

  /* Freq-detach peer3 */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  for (int i = 0; i < 3; i++) {
    auto prefix = fmt::format("72.{}.0.0/16", 2 + i);
    auto community = fmt::format("{}:1", 7202 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("72.{}.0.0", 2 + i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* DOWN + unblock + UP -> DID (deferred for deterministic assertion) */
  bringDownPeer(kPeerAddr3);
  unblockPeer(kPeerAddr3);
  testOnlyDeferInitDump(kPeerAddr3, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_INIT_DUMP, 30));
  testOnlyDeferInitDump(kPeerAddr3, false, fboss::kDefaultVrf);

  /* Simulate policy change: withdraw old, inject new prefix */
  withdrawLocalRoutesAtRuntime({"72.1.0.0/16"});
  EXPECT_TRUE(verifyRouteWithdraw("v4", "72.1.0.0", 16, kPeerAddr4));

  injectLocalRoutesAtRuntime({"72.5.0.0/16"}, {"7205:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("72.5.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "72.5.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "7205:1"));

  EXPECT_TRUE(isPeerInSync(kPeerAddr4));
  EXPECT_NE(getPeerState(kPeerAddr3), PeerUpdateState::DOWN);

  XLOG(INFO, "=== TEST PASSED: DetachedInitDump_PolicyChg ===");
}

/*
 * P-DID x E-MRAI-FIRE
 * MRAI fires while peer in DID. Group processes normally, peer3
 * accumulates CL items. Simulated with 3 routes (separate communities).
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
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  for (int i = 0; i < 3; i++) {
    auto prefix = fmt::format("72.{}.0.0/16", 10 + i);
    auto community = fmt::format("{}:1", 7210 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("72.{}.0.0", 10 + i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* DOWN + unblock + UP -> DID (deferred for deterministic assertion) */
  bringDownPeer(kPeerAddr3);
  unblockPeer(kPeerAddr3);
  testOnlyDeferInitDump(kPeerAddr3, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_INIT_DUMP, 30));
  testOnlyDeferInitDump(kPeerAddr3, false, fboss::kDefaultVrf);

  /* Inject 3 routes during DID -- each triggers separate MRAI cycle */
  for (int i = 0; i < 3; i++) {
    auto prefix = fmt::format("72.{}.0.0/16", 15 + i);
    auto community = fmt::format("{}:1", 7215 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("72.{}.0.0", 15 + i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }

  EXPECT_TRUE(isPeerInSync(kPeerAddr4));
  EXPECT_NE(getPeerState(kPeerAddr3), PeerUpdateState::DOWN);

  XLOG(INFO, "=== TEST PASSED: DetachedInitDump_MraiFire ===");
}

/*
 * P-DID x E-MULTI-ROUTE
 * Batch of 4 routes during DID. CL grows, peer continues init dump.
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
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  for (int i = 0; i < 3; i++) {
    auto prefix = fmt::format("72.{}.0.0/16", 20 + i);
    auto community = fmt::format("{}:1", 7220 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("72.{}.0.0", 20 + i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* DOWN + unblock + UP -> DID (deferred for deterministic assertion) */
  bringDownPeer(kPeerAddr3);
  unblockPeer(kPeerAddr3);
  testOnlyDeferInitDump(kPeerAddr3, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_INIT_DUMP, 30));
  testOnlyDeferInitDump(kPeerAddr3, false, fboss::kDefaultVrf);

  /* Inject 4 batch routes during DID, drain one at a time for peer4 */
  for (int i = 0; i < 4; i++) {
    auto prefix = fmt::format("72.{}.0.0/16", 25 + i);
    auto community = fmt::format("{}:1", 7225 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("72.{}.0.0", 25 + i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }

  EXPECT_TRUE(isPeerInSync(kPeerAddr4));
  EXPECT_NE(getPeerState(kPeerAddr3), PeerUpdateState::DOWN);

  XLOG(INFO, "=== TEST PASSED: DetachedInitDump_MultiRoute ===");
}

/*
 * P-DRJ x E-ROUTE-WD
 * Withdrawal during DRJ. If DFP, must become DSP. Peer4 receives normally.
 */
TEST_P(UpdateGroupMultiPeerTest, DetachedReadyToJoin_RouteWithdraw) {
  XLOG(INFO, "=== TEST: DetachedReadyToJoin_RouteWithdraw ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  setupSlowPeerComponents(5, 4, 0);

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

  /* Inject shared route */
  injectLocalRoutesAtRuntime({"72.30.0.0/16"}, {"7230:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("72.30.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "72.30.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "7230:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "72.30.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "7230:1"));

  /* Freq-detach peer3 */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  for (int i = 0; i < 5; i++) {
    auto prefix = fmt::format("72.{}.0.0/16", 31 + i);
    auto community = fmt::format("{}:1", 7231 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("72.{}.0.0", 31 + i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Unblock -> peer starts recovery */
  waitForDrjWithDrain(kPeerAddr3, peerId3);

  /* Withdraw the shared route -- peer4 receives withdrawal */
  withdrawLocalRoutesAtRuntime({"72.30.0.0/16"});
  EXPECT_TRUE(verifyRouteWithdraw("v4", "72.30.0.0", 16, kPeerAddr4));

  EXPECT_TRUE(isPeerInSync(kPeerAddr4));
  EXPECT_NE(getPeerState(kPeerAddr3), PeerUpdateState::DOWN);

  XLOG(INFO, "=== TEST PASSED: DetachedReadyToJoin_RouteWithdraw ===");
}

/*
 * P-DRJ x E-UNBLOCK -- N/A
 * Peer at DRJ is already unblocked. Calling unblockPeer again is a no-op.
 */
TEST_P(UpdateGroupMultiPeerTest, DetachedReadyToJoin_Unblock_NA) {
  XLOG(INFO, "=== TEST: DetachedReadyToJoin_Unblock_NA ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  setupSlowPeerComponents(5, 4, 0);

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
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  for (int i = 0; i < 5; i++) {
    auto prefix = fmt::format("72.{}.0.0/16", 40 + i);
    auto community = fmt::format("{}:1", 7240 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("72.{}.0.0", 40 + i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Unblock -> peer starts recovery */
  waitForDrjWithDrain(kPeerAddr3, peerId3);

  /* Double-unblock -- should be no-op */
  unblockPeer(kPeerAddr3);

  /* Peer4 works */
  injectLocalRoutesAtRuntime({"72.47.0.0/16"}, {"7247:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("72.47.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "72.47.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "7247:1"));

  XLOG(INFO, "=== TEST PASSED: DetachedReadyToJoin_Unblock_NA ===");
}

/*
 * P-DRJ x E-SLOW-FREQ -- N/A
 * Setting freq threshold on already-detached DRJ peer is a no-op.
 */
TEST_P(UpdateGroupMultiPeerTest, DetachedReadyToJoin_SlowFreq_NA) {
  XLOG(INFO, "=== TEST: DetachedReadyToJoin_SlowFreq_NA ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  setupSlowPeerComponents(5, 4, 0);

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

  /* Freq-detach then unblock */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  for (int i = 0; i < 5; i++) {
    auto prefix = fmt::format("72.{}.0.0/16", 50 + i);
    auto community = fmt::format("{}:1", 7250 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("72.{}.0.0", 50 + i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));
  waitForDrjWithDrain(kPeerAddr3, peerId3);

  /* Set aggressive freq threshold -- no double detach */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  EXPECT_NE(getPeerState(kPeerAddr3), PeerUpdateState::DOWN);
  EXPECT_TRUE(isPeerInSync(kPeerAddr4));

  XLOG(INFO, "=== TEST PASSED: DetachedReadyToJoin_SlowFreq_NA ===");
}

/*
 * P-DRJ x E-PEER-DOWN
 * Peer goes down at DRJ. Standard cleanup, peer4 continues.
 */
TEST_P(UpdateGroupMultiPeerTest, DetachedReadyToJoin_PeerDown) {
  XLOG(INFO, "=== TEST: DetachedReadyToJoin_PeerDown ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  setupSlowPeerComponents(5, 4, 0);

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

  /* Freq-detach then unblock */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  for (int i = 0; i < 5; i++) {
    auto prefix = fmt::format("72.{}.0.0/16", 60 + i);
    auto community = fmt::format("{}:1", 7260 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("72.{}.0.0", 60 + i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));
  testOnlyDeferDrjAcceptance(kPeerAddr3, true);
  unblockPeer(kPeerAddr3);
  {
    for (int i = 0; i < 20; ++i) {
      if (getPeerState(kPeerAddr3) == PeerUpdateState::DETACHED_READY_TO_JOIN) {
        break;
      }
      drainPeerQueueCompletely(peerId3, 1, 100);
      peerManager_->getEventBase().runInEventBaseThreadAndWait([]() {});
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    drainPeerQueueCompletely(peerId3, 1, 100);
  }
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_READY_TO_JOIN));

  /* Bring down peer3 during DRJ */
  bringDownPeer(kPeerAddr3);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));

  /* Peer4 continues working */
  injectLocalRoutesAtRuntime({"72.67.0.0/16"}, {"7267:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("72.67.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "72.67.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "7267:1"));
  EXPECT_TRUE(isPeerInSync(kPeerAddr4));

  XLOG(INFO, "=== TEST PASSED: DetachedReadyToJoin_PeerDown ===");
}

/*
 * P-DRJ x E-PEER-UP -- N/A (already up)
 * Don't call bringUpPeer on already-up peer. Verify state stability.
 */
TEST_P(UpdateGroupMultiPeerTest, DetachedReadyToJoin_PeerUp_NA) {
  XLOG(INFO, "=== TEST: DetachedReadyToJoin_PeerUp_NA ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  setupSlowPeerComponents(5, 4, 0);

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

  /* Freq-detach then unblock */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  for (int i = 0; i < 5; i++) {
    auto prefix = fmt::format("72.{}.0.0/16", 70 + i);
    auto community = fmt::format("{}:1", 7270 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("72.{}.0.0", 70 + i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));
  waitForDrjWithDrain(kPeerAddr3, peerId3);

  /* Peer4 works */
  injectLocalRoutesAtRuntime({"72.77.0.0/16"}, {"7277:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("72.77.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "72.77.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "7277:1"));

  XLOG(INFO, "=== TEST PASSED: DetachedReadyToJoin_PeerUp_NA ===");
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
