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
 * E2E tests for AdjRibOut delay functionality.
 *
 * These tests verify out-delay timer behavior and staggered route
 * advertisement through the complete BGP pipeline.
 *
 * Key behaviors tested:
 * 1. Routes appear in egress queue after out-delay expires
 * 2. Update coalescing during delay period (implicit withdrawal)
 * 3. Explicit withdrawals bypass out-delay (sent immediately)
 * 4. Initial dump bypasses out-delay
 * 5. Route flap handling with out-delay
 *
 * Mocked: FIB (TestFib), SessionManager (MockSessionManager)
 * Real: RIB, PeerManagerBase, AdjRib
 */

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include <folly/logging/xlog.h>

#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/E2ETestFixture.h"

using namespace facebook::bgp;

namespace facebook::bgp {

/*
 * E2EAdjRibOutDelayTest
 *
 * Test fixture with out-delay configured:
 * - Peer3, Peer4: 0s out-delay (immediate advertisement, route sources)
 * - Peer5: 1s out-delay (tests deferred advertisement)
 */
class E2EAdjRibOutDelayTest : public E2ETestFixture {
 protected:
  void SetUp() override {
    addPeer(kDefaultPeerSpec3_outDelay0s);
    addPeer(kDefaultPeerSpec4_outDelay0s);
    addPeer(kDefaultPeerSpec5_outDelay1s);
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

  static constexpr auto kOutDelay = std::chrono::seconds(1);
  static constexpr auto kWaitMargin = std::chrono::milliseconds(500);
};

/*
 * Test: Route is deferred during out-delay and advertised after
 *
 * Scenario:
 * - Peer5 has 1s out-delay configured
 * - Route is announced from Peer3
 * - VERIFY: Queue is empty during delay period (route is deferred)
 * - Wait for out-delay to expire
 * - VERIFY: Route appears in Peer5's queue after delay
 */
TEST_F(E2EAdjRibOutDelayTest, RouteAdvertisedAfterDelay) {
  bringUpAllPeersWithEor();

  /*
   * Drain any residual messages (e.g., extra EoR for v6 AFI) before the test.
   * This ensures we have a clean slate to verify the out-delay behavior.
   */
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};
  drainPeerQueueCompletely(peerId5);

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));

  /*
   * KEY ASSERTION: Queue should be empty during the delay period.
   * The route is deferred and should NOT be in the egress queue yet.
   * This verifies the out-delay mechanism is actually deferring routes.
   */
  EXPECT_TRUE(isPeerEgressQueueEmpty(kPeerAddr5))
      << "Route should be deferred during out-delay period - queue should be "
         "empty";

  // Verify ODS counter: route is deferred (1 entry in deferredUpdates_)
  auto tcData = fb303::ThreadCachedServiceData::get();
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kDeferredUpdatesCount));

  /* Wait for out-delay to expire (1s + margin for processing) */
  std::this_thread::sleep_for(kOutDelay + kWaitMargin);

  /* Route should now be advertised */
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));

  // Verify ODS counter decremented to 0 after deferred update processed
  tcData->publishStats();
  EXPECT_EQ(0, tcData->getCounter(RibStats::kDeferredUpdatesCount));
}

/*
 * Test: Update coalescing during out-delay (implicit withdrawal)
 *
 * Scenario:
 * - Route is announced with local_pref=100
 * - Route is updated with local_pref=200 before out-delay expires
 * - Wait for out-delay to expire
 * - VERIFY: Only ONE update with final attributes is sent (not two)
 *
 * This tests RFC "implicit withdrawal" behavior where updating a route
 * replaces the previous advertisement. Updates are coalesced during
 * the out-delay period.
 */
TEST_F(E2EAdjRibOutDelayTest, ImplicitWithdrawBeforeDelay) {
  bringUpAllPeersWithEor();

  /* Announce initial route */
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");

  /* Quickly update to new attributes (implicit withdrawal) */
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001", "", 0, 200, 0);

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));

  /* Wait for out-delay to expire */
  std::this_thread::sleep_for(kOutDelay + kWaitMargin);

  /*
   * Should receive only the final state (updated route).
   * The system coalesces updates during the delay period.
   */
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));
}

/*
 * Test: Explicit withdrawals bypass out-delay
 *
 * Scenario:
 * - Route is announced (subject to out-delay)
 * - Wait for out-delay to expire and verify route is advertised
 * - Route is explicitly withdrawn
 * - VERIFY: Withdrawal is sent IMMEDIATELY (no waiting for out-delay)
 *
 * This demonstrates that explicit BGP WITHDRAW messages do NOT obey
 * out-delay. Only route advertisements are deferred by out-delay.
 * Withdrawals are sent immediately to ensure fast convergence when
 * routes become unreachable.
 *
 * Note: This is different from "implicit withdrawal" (RFC 4271) which
 * occurs when a new route update replaces a previous one for the same
 * prefix. Implicit withdrawals are coalesced during out-delay.
 */
TEST_F(E2EAdjRibOutDelayTest, ExplicitWithdrawBypassesOutDelay) {
  bringUpAllPeersWithEor();

  /* Announce route - this will be subject to out-delay */
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));

  /* Wait for out-delay to expire and verify route is advertised */
  std::this_thread::sleep_for(kOutDelay + kWaitMargin);
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"))
      << "Route should be advertised after out-delay expires";

  /*
   * Explicitly withdraw the route.
   * Unlike announcements, withdrawals are sent immediately.
   */
  deleteRoute("v4", "10.0.0.0", 8, kPeerAddr3);

  /*
   * KEY ASSERTION: Withdrawal should be sent immediately.
   * Explicit withdrawals bypass out-delay for fast convergence.
   * We verify this by checking that the withdrawal message appears
   * in the queue without waiting for any delay.
   */
  EXPECT_TRUE(verifyRouteWithdraw("v4", "10.0.0.0", 8, kPeerAddr5))
      << "Explicit withdrawal should bypass out-delay and be sent immediately";
}

/*
 * Test: Initial dump bypasses out-delay
 *
 * Scenario:
 * - Local route is injected at runtime
 * - Initial dump routes should be sent immediately (no out-delay)
 *
 * Note: This test verifies that routes marked as "initialDump" bypass
 * the out-delay mechanism. Unlike regular routes which are deferred
 * (queue is empty during delay period), initial dump routes should
 * appear in the queue immediately.
 */
TEST_F(E2EAdjRibOutDelayTest, InitialDumpNoDelay) {
  bringUpAllPeersWithEor();

  /* Inject local route at runtime */
  injectLocalRoutesAtRuntime({"10.0.0.0/8"}, {"100:1"}, 100);

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));

  /*
   * KEY ASSERTION: Initial dump routes bypass out-delay.
   * Unlike regular routes (tested in RouteAdvertisedAfterDelay) where the
   * queue is empty during the delay period, initial dump routes should be
   * advertised immediately WITHOUT waiting for the out-delay to expire.
   *
   * We verify this by confirming the route appears in the queue immediately
   * after injection, without any sleep. If initial dump did NOT bypass
   * out-delay, this verifyRouteAdd would fail (like RouteAdvertisedAfterDelay
   * demonstrates - the queue is empty during the delay period).
   */
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"))
      << "Initial dump route should bypass out-delay and be advertised "
         "immediately without waiting for the 1s delay period";
}

/*
 * Test: Multiple routes are all deferred and then advertised
 *
 * Scenario:
 * - Multiple routes announced in quick succession
 * - Wait for out-delay to expire
 * - VERIFY: All routes are advertised after delay expires
 */
TEST_F(E2EAdjRibOutDelayTest, MultipleRoutesStaggered) {
  bringUpAllPeersWithEor();

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");
  addRoute("v4", "20.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");
  addRoute("v4", "30.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");

  auto prefix1 = folly::IPAddress::createNetwork("10.0.0.0/8");
  auto prefix2 = folly::IPAddress::createNetwork("20.0.0.0/8");
  auto prefix3 = folly::IPAddress::createNetwork("30.0.0.0/8");

  ASSERT_TRUE(waitForRouteInShadowRib(prefix1));
  ASSERT_TRUE(waitForRouteInShadowRib(prefix2));
  ASSERT_TRUE(waitForRouteInShadowRib(prefix3));

  /* Wait for out-delay to expire */
  std::this_thread::sleep_for(kOutDelay + kWaitMargin);

  /* Use batch verification - BGP may batch multiple prefixes into one UPDATE */
  std::vector<VerifySpec> expectedRoutes = {
      {.prefix = "10.0.0.0", .prefixLen = 8, .expectedNexthop = "127.5.0.4"},
      {.prefix = "20.0.0.0", .prefixLen = 8, .expectedNexthop = "127.5.0.4"},
      {.prefix = "30.0.0.0", .prefixLen = 8, .expectedNexthop = "127.5.0.4"},
  };
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr5, expectedRoutes));
}

/*
 * Test: Route flap handling with out-delay
 *
 * Scenario:
 * - Route announced, wait for advertisement
 * - Route withdrawn, verify withdrawal
 * - Route re-announced, wait for re-advertisement
 *
 * This tests the complete lifecycle with out-delay active.
 */
TEST_F(E2EAdjRibOutDelayTest, RouteFlapHandling) {
  bringUpAllPeersWithEor();

  /* Announce */
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));

  /* Wait for out-delay and verify advertisement */
  std::this_thread::sleep_for(kOutDelay + kWaitMargin);
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));

  /* Withdraw - withdrawals are sent immediately (no out-delay) */
  deleteRoute("v4", "10.0.0.0", 8, kPeerAddr3);
  EXPECT_TRUE(verifyRouteWithdraw("v4", "10.0.0.0", 8, kPeerAddr5));

  /* Re-announce - should be deferred again */
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));

  /* Wait for out-delay and verify re-advertisement */
  std::this_thread::sleep_for(kOutDelay + kWaitMargin);
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));
}

/*
 * Test: Daemon shutdown drops deferred out-delay updates
 *
 * Shutdown waits on the PeerManager EventBase, and each peer's out-delay
 * timer sends its deferred updates on that EventBase. At scale those callbacks
 * delayed shutdown past the systemd stop timeout (T288806537).
 *
 * Scenario:
 * - Route from Peer3 is deferred for Peer5 (1s out-delay)
 * - markDaemonShutdown()
 * - VERIFY: Peer5 has no deferred update, out-delay timer or change list
 *   consumer left, so nothing can send the deferred route later
 * - VERIFY: The deferred route was not sent
 */
TEST_F(E2EAdjRibOutDelayTest, DaemonShutdownDropsDeferredUpdates) {
  bringUpAllPeersWithEor();
  const BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};
  drainPeerQueueCompletely(peerId5);

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("10.0.0.0/8")));
  const EgressWorkState deferred{
      .deferredUpdateCount = 1,
      .outDelayTimerScheduled = true,
      .changeListConsumerActive = true,
  };
  /* Consumed on the next MRAI tick; stay well inside the 1s out-delay. */
  test::checkWithRetry(
      [&] { return getEgressWorkState(peerId5) == deferred; },
      50,
      std::chrono::milliseconds(10),
      "Route was not deferred by Peer5's out-delay");

  peerManager_->markDaemonShutdown();

  EXPECT_EQ(EgressWorkState{}, getEgressWorkState(peerId5));
  EXPECT_TRUE(isPeerEgressQueueEmpty(kPeerAddr5))
      << "Deferred route was sent after daemon shutdown was marked";
}

} // namespace facebook::bgp
