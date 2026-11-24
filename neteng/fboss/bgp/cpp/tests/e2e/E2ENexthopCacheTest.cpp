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
 * E2E tests for NexthopCache nexthop status tracking.
 *
 * These tests verify nexthop status updates are correctly tracked
 * and propagated through the RIB for nexthop tracking scenarios.
 *
 * Mocked: FIB (TestFib), SessionManager (MockSessionManager)
 * Real: RIB, PeerManagerBase, AdjRib, NexthopCache
 */

#include <gtest/gtest.h>

#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopStatus.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/E2ETestFixture.h"

using namespace facebook::bgp;

namespace facebook::bgp {

class E2ENexthopCacheTest : public E2ETestFixture {
 protected:
  void SetUp() override {
    addPeer(kDefaultPeerSpec3);
    addPeer(kDefaultPeerSpec4);
    addPeer(kDefaultPeerSpec5);
    createRib(true /* enableNexthopTracking */);
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
 * Test: Nexthop status update affects route selection
 *
 * Scenario:
 * - Route with tracked nexthop is announced
 * - Nexthop status is injected as reachable
 * - Route should be advertised
 */
TEST_F(E2ENexthopCacheTest, NexthopStatusUpdateAffectsRouteSelection) {
  bringUpAllPeersWithEor();

  // Announce route with nexthop that will be tracked
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");

  // Inject nexthop status as reachable
  std::vector<NexthopStatus> nexthopStatuses;
  nexthopStatuses.emplace_back(
      NexthopKey{.ip = folly::IPAddress("11.0.0.1")},
      true /* reachable */,
      100 /* igpCost */);
  injectNexthopStatuses(nexthopStatuses);

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));

  // Route should be advertised with reachable nexthop
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));
}

/*
 * Test: Multiple nexthop status updates
 *
 * Scenario:
 * - Multiple routes with different nexthops
 * - Batch nexthop status update
 */
TEST_F(E2ENexthopCacheTest, BatchNexthopStatusUpdate) {
  bringUpAllPeersWithEor();

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");
  addRoute("v4", "20.0.0.0", 8, kPeerAddr4, "11.0.0.2", "65002");

  // Batch inject nexthop statuses
  std::vector<NexthopStatus> nexthopStatuses;
  nexthopStatuses.emplace_back(
      NexthopKey{.ip = folly::IPAddress("11.0.0.1")}, true, 100);
  nexthopStatuses.emplace_back(
      NexthopKey{.ip = folly::IPAddress("11.0.0.2")}, true, 200);
  injectNexthopStatuses(nexthopStatuses);

  // Verify ODS counter after nexthop status map insertions (2 entries tracked)
  auto tcData = fb303::ThreadCachedServiceData::get();
  tcData->publishStats();
  EXPECT_EQ(2, tcData->getCounter(RibStats::kNexthopStatusMapCount));

  auto prefix1 = folly::IPAddress::createNetwork("10.0.0.0/8");
  auto prefix2 = folly::IPAddress::createNetwork("20.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix1));
  ASSERT_TRUE(waitForRouteInShadowRib(prefix2));

  // Routes may arrive in any order - use verifyRoutes
  EXPECT_TRUE(verifyRoutes(
      "v4",
      kPeerAddr5,
      {{"10.0.0.0", 8, "127.5.0.4", "", "", 0},
       {"20.0.0.0", 8, "127.5.0.4", "", "", 0}}));
}

/*
 * Test: Unreachable nexthop prevents route advertisement
 *
 * Scenario:
 * - Mark nexthop as unreachable FIRST
 * - Route announced with that nexthop
 * - Route should not be selected for advertisement (no bestpath)
 *
 * This tests E2E observable behavior: route with unreachable nexthop
 * should NOT be propagated to other peers.
 */
TEST_F(E2ENexthopCacheTest, UnreachableNexthopFilteredOut) {
  bringUpAllPeersWithEor();

  /*
   * First, inject the nexthop as UNREACHABLE BEFORE announcing the route.
   * This ensures the route is evaluated with unreachable status immediately.
   */
  std::vector<NexthopStatus> nexthopStatuses;
  nexthopStatuses.emplace_back(
      NexthopKey{.ip = folly::IPAddress("11.0.0.1")},
      false /* unreachable */,
      std::nullopt);
  injectNexthopStatuses(nexthopStatuses);

  /* Announce route with unreachable nexthop */
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");

  /* Wait for route processing to complete */
  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");

  /*
   * Route should NOT be in shadowRIB (no bestpath selected due to
   * unreachable nexthop). With nexthop tracking enabled, routes with
   * unreachable nexthops are not selected as bestpath.
   */
  EXPECT_TRUE(verifyRouteNotInShadowRib(prefix));

  /* Verify the nexthop is being tracked (route exists, just not as bestpath) */
  EXPECT_TRUE(verifyNexthopRouteCount(folly::IPAddress("11.0.0.1"), 1));
}

/*
 * Test: Nexthop becomes reachable, route gets propagated
 *
 * Scenario:
 * - Mark nexthop as unreachable
 * - Route announced with that nexthop (not propagated)
 * - Nexthop becomes reachable
 * - Route should now be propagated
 *
 * This is the complement to UnreachableNexthopFilteredOut - verifies
 * that when a nexthop transitions from unreachable to reachable,
 * the route becomes eligible for bestpath and is advertised.
 */
TEST_F(E2ENexthopCacheTest, NexthopBecomesReachableRouteAdvertised) {
  bringUpAllPeersWithEor();

  /* Start with unreachable nexthop */
  std::vector<NexthopStatus> nexthopStatuses;
  nexthopStatuses.emplace_back(
      NexthopKey{.ip = folly::IPAddress("11.0.0.1")},
      false /* unreachable */,
      std::nullopt);
  injectNexthopStatuses(nexthopStatuses);

  /* Announce route */
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");

  /* Route should not be in shadowRIB yet */
  EXPECT_TRUE(verifyRouteNotInShadowRib(prefix));

  /* Now mark nexthop as reachable */
  nexthopStatuses.clear();
  nexthopStatuses.emplace_back(
      NexthopKey{.ip = folly::IPAddress("11.0.0.1")},
      true /* reachable */,
      100 /* igpCost */);
  injectNexthopStatuses(nexthopStatuses);

  /* Route should now appear in shadowRIB and be advertised */
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));
}

} // namespace facebook::bgp
