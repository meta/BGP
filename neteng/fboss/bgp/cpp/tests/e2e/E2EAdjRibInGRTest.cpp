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
 * E2E tests for AdjRibIn graceful restart and stale path handling.
 *
 * These tests verify GR behavior, stale path cleanup, and session
 * termination handling through AdjRibIn.
 *
 * Derived from: AdjRibInTest.cpp
 * Mocked: FIB (TestFib), SessionManager (MockSessionManager)
 * Real: RIB, PeerManagerBase, AdjRib
 */

#include <gtest/gtest.h>

#include <folly/logging/xlog.h>

#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/E2ETestFixture.h"

using namespace facebook::bgp;

namespace facebook::bgp {

class E2EAdjRibInGRTest : public E2ETestFixture {
 protected:
  void SetUp() override {
    addPeer(kDefaultPeerSpec3);
    addPeer(kDefaultPeerSpec4);
    addPeer(kDefaultPeerSpec5);
    createRib();
    createPeerManager(/*enableUpdateGroup=*/false);
  }

  void bringUpAllPeersWithEor() {
    bringUpPeer(kPeerAddr3);
    bringUpPeer(kPeerAddr4);
    bringUpPeer(kPeerAddr5);
    BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
    BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
    BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};
    sendEoRToPeer(peerId3);
    sendEoRToPeer(peerId4);
    sendEoRToPeer(peerId5);
    EXPECT_TRUE(waitForEoR(peerId3));
    EXPECT_TRUE(waitForEoR(peerId4));
    EXPECT_TRUE(waitForEoR(peerId5));
  }
};

/*
 * Derived from: AdjRibInTest.cpp (GR session termination)
 */
TEST_F(E2EAdjRibInGRTest, RoutesWithdrawnOnPeerDown) {
  bringUpAllPeersWithEor();

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));

  // Peer3 goes down
  bringDownPeer(kPeerAddr3);

  // Routes from Peer3 should be withdrawn
  EXPECT_TRUE(verifyRouteWithdraw("v4", "10.0.0.0", 8, kPeerAddr5));
}

/*
 * Derived from:
 * PeerMgrRibTest.cpp::PeerSessionFlapsDuringRibInitialAnnouncementTest
 *
 * Verify routes are restored after peer restarts.
 */
TEST_F(E2EAdjRibInGRTest, RoutesRestoredAfterPeerRestart) {
  bringUpAllPeersWithEor();

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));

  // Verify initial route announcement
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));

  // Peer3 restarts
  bringDownPeer(kPeerAddr3);

  // Verify route is withdrawn from RIB (no bestpath)
  ASSERT_TRUE(waitForRouteWithdrawnFromRib("10.0.0.0/8"));

  // Bring peer back up
  bringUpPeer(kPeerAddr3);
  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  sendEoRToPeer(peerId3);
  EXPECT_TRUE(waitForEoR(peerId3));

  // Re-announce route
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");

  ASSERT_TRUE(waitForRouteInShadowRib(prefix));

  // Verify the route is back in RIB with bestpath set
  auto ribEntries =
      rib_->getRibEntryForPrefix(std::make_unique<std::string>("10.0.0.0/8"));
  ASSERT_FALSE(ribEntries.empty());
  EXPECT_TRUE(
      apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          ribEntries[0].best_next_hop()));
}

/*
 * Derived from: RibTest.cpp::BestpathChangeWithNexthopChangeTest
 */
TEST_F(E2EAdjRibInGRTest, BestpathChangesToBackupOnPeerDown) {
  bringUpAllPeersWithEor();

  // Primary route from Peer3 (higher local pref)
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001", "", 0, 200, 0);
  // Backup route from Peer4 (lower local pref)
  addRoute("v4", "10.0.0.0", 8, kPeerAddr4, "11.0.0.2", "65002");

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));

  // Primary peer goes down
  bringDownPeer(kPeerAddr3);

  // Backup route should become best path and be advertised
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));
}

/*
 * Derived from: RibTest.cpp::EORTest
 */
TEST_F(E2EAdjRibInGRTest, EorTriggersStalePathCleanup) {
  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr5);
  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};
  sendEoRToPeer(peerId5);
  EXPECT_TRUE(waitForEoR(peerId5));

  // Announce route before EoR
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));

  // Send EoR
  sendEoRToPeer(peerId3);
  EXPECT_TRUE(waitForEoR(peerId3));

  // Route should be advertised
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));
}

/*
 * Derived from: PeerMgrRibTest.cpp (multi-peer session scenarios)
 */
TEST_F(E2EAdjRibInGRTest, MultiplePeersDownAndUp) {
  bringUpAllPeersWithEor();

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");
  addRoute("v4", "20.0.0.0", 8, kPeerAddr4, "11.0.0.2", "65002");

  auto prefix1 = folly::IPAddress::createNetwork("10.0.0.0/8");
  auto prefix2 = folly::IPAddress::createNetwork("20.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix1));
  ASSERT_TRUE(waitForRouteInShadowRib(prefix2));

  // Use batch verification - BGP may batch multiple prefixes into one UPDATE
  std::vector<VerifySpec> expectedRoutes = {
      {.prefix = "10.0.0.0", .prefixLen = 8, .expectedNexthop = "127.5.0.4"},
      {.prefix = "20.0.0.0", .prefixLen = 8, .expectedNexthop = "127.5.0.4"},
  };
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr5, expectedRoutes));

  // Both peers go down
  bringDownPeer(kPeerAddr3);
  bringDownPeer(kPeerAddr4);

  // Use batch verification for withdrawals
  std::vector<WithdrawSpec> expectedWithdraws = {
      {.prefix = "10.0.0.0", .prefixLen = 8},
      {.prefix = "20.0.0.0", .prefixLen = 8},
  };
  EXPECT_TRUE(verifyRouteWithdraws("v4", kPeerAddr5, expectedWithdraws));
}

/*
 * Derived from:
 * AdjRibInTest.cpp::VerifyPolicyReEvaluationWithSessionTermination
 */
TEST_F(E2EAdjRibInGRTest, SessionTerminationDuringProcessing) {
  bringUpAllPeersWithEor();

  // Send multiple routes
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");
  addRoute("v4", "20.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");
  addRoute("v4", "30.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");

  auto prefix1 = folly::IPAddress::createNetwork("10.0.0.0/8");
  auto prefix2 = folly::IPAddress::createNetwork("20.0.0.0/8");
  auto prefix3 = folly::IPAddress::createNetwork("30.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix1));
  ASSERT_TRUE(waitForRouteInShadowRib(prefix2));
  ASSERT_TRUE(waitForRouteInShadowRib(prefix3));

  // Peer goes down
  bringDownPeer(kPeerAddr3);

  // All routes should be withdrawn - use batch verification
  std::vector<WithdrawSpec> expectedWithdraws = {
      {.prefix = "10.0.0.0", .prefixLen = 8},
      {.prefix = "20.0.0.0", .prefixLen = 8},
      {.prefix = "30.0.0.0", .prefixLen = 8},
  };
  EXPECT_TRUE(verifyRouteWithdraws("v4", kPeerAddr5, expectedWithdraws));
}

/*
 * Derived from: AdjRibInTest.cpp::V6UpdateProcessingMultiple
 */
TEST_F(E2EAdjRibInGRTest, IPv6RoutesWithdrawnOnPeerDown) {
  bringUpAllPeersWithEor();

  addRoute("v6", "2001:db8::", 32, kPeerAddr3, "2001:db8::1", "65001");

  auto prefix = folly::IPAddress::createNetwork("2001:db8::/32");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  EXPECT_TRUE(verifyRouteAdd(
      "v6", "2001:db8::", 32, kPeerAddr5, "2401:db00:e011:411:1000::2d"));

  bringDownPeer(kPeerAddr3);

  EXPECT_TRUE(verifyRouteWithdraw("v6", "2001:db8::", 32, kPeerAddr5));
}

/*
 * Derived from:
 * PeerMgrRibTest.cpp::PeerSessionFlapsDuringRibInitialAnnouncementTest
 *
 * Verify that a rapid peer flap is handled correctly.
 */
TEST_F(E2EAdjRibInGRTest, PeerFlap) {
  bringUpAllPeersWithEor();

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));

  // Rapid flap
  bringDownPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr3);
  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  sendEoRToPeer(peerId3);
  EXPECT_TRUE(waitForEoR(peerId3));

  // Re-announce route
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");

  ASSERT_TRUE(waitForRouteInShadowRib(prefix));

  // Verify the route is in RIB with bestpath set
  auto ribEntries =
      rib_->getRibEntryForPrefix(std::make_unique<std::string>("10.0.0.0/8"));
  ASSERT_EQ(ribEntries.size(), 1);
  EXPECT_TRUE(
      apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          ribEntries[0].best_next_hop()));

  // Verify peer5 receives the route
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));
}

/*
 * Fixture for tests that exercise the remoteGrRestartTimer_ /
 * stalePathTimer_ -> schedulePendingRibInPush -> AdjRib::stop() drain
 * path.
 *
 * Uses 1s GR convergence + 1s remote GR restart so timer expiry happens
 * within test runtime.
 */
class E2EAdjRibInGrTimerCleanupTest : public E2ETestFixture {
 protected:
  void SetUp() override {
    setGrConvergenceSeconds(1);
    setPeerGrRestartTimeSeconds(1);
    addPeer(kDefaultPeerSpec3);
    addPeer(kDefaultPeerSpec4);
    addPeer(kDefaultPeerSpec5);
    createRib();
    createPeerManager(/*enableUpdateGroup=*/false);
  }

  /*
   * Read isPeerGracefulRestarting on PeerManagerBase's evb_ thread to avoid
   * a TSAN data race against remoteGrRestartTimer_.reset() inside the
   * GR-restart timer callback (also on evb_).
   */
  bool isPeerGrOnEvb(const std::shared_ptr<AdjRib>& adjRib) {
    bool result = false;
    peerManager_->getEventBase().runInEventBaseThreadAndWait(
        [&] { result = adjRib->isPeerGracefulRestarting(); });
    return result;
  }
};

/*
 * remoteGrRestartTimer_ expiry schedules a detached push via
 * pendingRibInPushes_; AdjRib::stop() must drain that vector before the
 * AdjRib is destroyed (otherwise the suspended push UAFs on resume).
 *
 * Verifies end-to-end behaviour:
 *  - peer enters GR helper mode after GR sessionStop
 *  - stale routes get withdrawn after the timer fires
 *  - AdjRib::stop() completes cleanly and the route reaches RIB + peer5
 */
TEST_F(E2EAdjRibInGrTimerCleanupTest, RemoteGrRestartTimerExpiryDrainsCleanly) {
  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr5);
  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};
  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId5);
  ASSERT_TRUE(waitForEoR(peerId3));
  ASSERT_TRUE(waitForEoR(peerId5));

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");
  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  ASSERT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));

  /*
   * Drop session with GR — peer enters GR helper mode and schedules
   * remoteGrRestartTimer_ for ~1s.
   */
  bringDownPeerWithGr(kPeerAddr3);
  auto adjRib3 = getAdjRibByAddr(kPeerAddr3);
  ASSERT_NE(adjRib3, nullptr);
  EXPECT_TRUE(isPeerGrOnEvb(adjRib3));

  /*
   * Wait for the GR timer to fire. After expiry the timer callback runs
   * schedulePendingRibInPush(), which appends a SemiFuture to
   * pendingRibInPushes_, and resets remoteGrRestartTimer_.
   */
  WITH_RETRIES_N_TIMED(50, std::chrono::milliseconds(100), {
    EXPECT_EVENTUALLY_FALSE(isPeerGrOnEvb(adjRib3));
  });

  /*
   * Force AdjRib::stop() while the detached push may still be in flight.
   * stop()'s collectAllRange must drain pendingRibInPushes_ before any
   * teardown can race the suspended push.
   */
  runAdjRibStop(adjRib3);

  /*
   * Stale route was withdrawn from RIB and re-advertised as a withdraw to
   * peer5.
   */
  EXPECT_TRUE(waitForRouteWithdrawnFromRib("10.0.0.0/8"));
  EXPECT_TRUE(verifyRouteWithdraw("v4", "10.0.0.0", 8, kPeerAddr5));
}

/*
 * Two peers in GR helper mode, both timers fire, both AdjRibs stopped.
 *
 * Each peer's timer-callback path drops a SemiFuture into its AdjRib's
 * pendingRibInPushes_ vector. With multiple pushes in flight across
 * multiple AdjRibs, AdjRib::stop() must drain its own vector independently
 * — no shared state, no missed drains, no UAF.
 */
TEST_F(E2EAdjRibInGrTimerCleanupTest, ConcurrentTimerExpiry_BothPeersDrained) {
  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  bringUpPeer(kPeerAddr5);
  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};
  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);
  sendEoRToPeer(peerId5);
  ASSERT_TRUE(waitForEoR(peerId3));
  ASSERT_TRUE(waitForEoR(peerId4));
  ASSERT_TRUE(waitForEoR(peerId5));

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");
  addRoute("v4", "20.0.0.0", 8, kPeerAddr4, "11.0.0.2", "65002");
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("10.0.0.0/8")));
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("20.0.0.0/8")));

  // Both peers GR-down — two remoteGrRestartTimer_ timers are now armed.
  bringDownPeerWithGr(kPeerAddr3);
  bringDownPeerWithGr(kPeerAddr4);
  auto adjRib3 = getAdjRibByAddr(kPeerAddr3);
  auto adjRib4 = getAdjRibByAddr(kPeerAddr4);
  ASSERT_NE(adjRib3, nullptr);
  ASSERT_NE(adjRib4, nullptr);
  EXPECT_TRUE(isPeerGrOnEvb(adjRib3));
  EXPECT_TRUE(isPeerGrOnEvb(adjRib4));

  // Wait for BOTH timers to fire (each schedules its own pending push).
  WITH_RETRIES_N_TIMED(50, std::chrono::milliseconds(100), {
    EXPECT_EVENTUALLY_FALSE(isPeerGrOnEvb(adjRib3));
    EXPECT_EVENTUALLY_FALSE(isPeerGrOnEvb(adjRib4));
  });

  /*
   * Stop both AdjRibs. Each must independently drain its own
   * pendingRibInPushes_ vector.
   */
  runAdjRibStop(adjRib3);
  runAdjRibStop(adjRib4);

  // Both stale routes withdrawn from RIB and from peer5.
  std::vector<WithdrawSpec> expectedWithdraws = {
      {.prefix = "10.0.0.0", .prefixLen = 8},
      {.prefix = "20.0.0.0", .prefixLen = 8},
  };
  EXPECT_TRUE(waitForRouteWithdrawnFromRib("10.0.0.0/8"));
  EXPECT_TRUE(waitForRouteWithdrawnFromRib("20.0.0.0/8"));
  EXPECT_TRUE(verifyRouteWithdraws("v4", kPeerAddr5, expectedWithdraws));
}

/*
 * Same peers as E2EAdjRibInGrTimerCleanupTest, with a remote GR restart time
 * long enough that the timer is still armed when the test marks daemon
 * shutdown.
 */
class E2EAdjRibInGrDaemonShutdownTest : public E2EAdjRibInGrTimerCleanupTest {
 protected:
  void SetUp() override {
    setPeerGrRestartTimeSeconds(120);
    addPeer(kDefaultPeerSpec3);
    addPeer(kDefaultPeerSpec5);
    createRib();
    createPeerManager(/*enableUpdateGroup=*/false);
  }
};

/*
 * Peer3 goes down with GR and its AdjRib arms the remote GR restart timer.
 * markDaemonShutdown() must cancel that timer, so no stale path purge runs on
 * the PeerManager EventBase during the shutdown.
 */
TEST_F(
    E2EAdjRibInGrDaemonShutdownTest,
    DaemonShutdownCancelsRemoteGrRestartTimer) {
  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr5);
  const BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  const BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};
  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId5);
  ASSERT_TRUE(waitForEoR(peerId3));
  ASSERT_TRUE(waitForEoR(peerId5));

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("10.0.0.0/8")));

  bringDownPeerWithGr(kPeerAddr3);
  const auto adjRib3 = getAdjRibByAddr(kPeerAddr3);
  ASSERT_NE(adjRib3, nullptr);
  ASSERT_TRUE(isPeerGrOnEvb(adjRib3));

  peerManager_->markDaemonShutdown();

  EXPECT_FALSE(isPeerGrOnEvb(adjRib3));
}

} // namespace facebook::bgp
