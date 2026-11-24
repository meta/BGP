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

#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopCache.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopStatus.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/E2ETestFixture.h"

using namespace facebook::bgp;

namespace facebook::bgp {

class E2ENexthopCacheTest : public E2ETestFixture {
 protected:
  bool enableUpdateGroups_{false};
  void SetUp() override {
    addPeer(kDefaultPeerSpec3);
    addPeer(kDefaultPeerSpec4);
    addPeer(kDefaultPeerSpec5);
    createRib(true /* enableNexthopTracking */);
    createPeerManager(
        /*enableUpdateGroup=*/enableUpdateGroups_,
        /*enableSerializeGroupPdu=*/enableUpdateGroups_);
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

/* Exercise EBB's cost requirement with individual and serialized groups. */
class E2ENexthopVrfCompatibilityTest
    : public E2ENexthopCacheTest,
      public testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    enableUpdateGroups_ = GetParam();
    E2ENexthopCacheTest::SetUp();
  }

  void expectForwarding(bool reachable) {
    const auto fib = getFibWeightedNexthops("10.0.0.0/8");
    if (reachable) {
      ASSERT_NE(nullptr, fib);
      EXPECT_TRUE(fib->contains(folly::IPAddress("11.0.0.1")));
    } else {
      EXPECT_TRUE(!fib || fib->empty());
    }
  }
};

TEST_P(
    E2ENexthopVrfCompatibilityTest,
    LosingCostWithdrawsAndRestoringCostRecovers) {
  bringUpAllPeersWithEor();
  const folly::IPAddress nh{"11.0.0.1"};
  const auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  injectNexthopStatuses(
      {NexthopStatus(NexthopKey{.ip = nh}, /*isReachable=*/true, 10)});
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, nh.str(), "65001");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  ASSERT_TRUE(
      verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));
  expectForwarding(/*reachable=*/true);

  injectNexthopStatuses({NexthopStatus(
      NexthopKey{.ip = nh}, /*isReachable=*/true, std::nullopt)});
  ASSERT_TRUE(verifyRouteWithdraw("v4", "10.0.0.0", 8, kPeerAddr5));
  const auto status = getNexthopCache()->getNexthopStatus(NexthopKey{.ip = nh});
  ASSERT_TRUE(status.has_value());
  EXPECT_TRUE(status->isReachable());
  EXPECT_FALSE(status->isResolvedForSelection());
  EXPECT_EQ(std::nullopt, status->getIgpCost());
  EXPECT_EQ(nullptr, getBestPath(prefix));
  expectForwarding(/*reachable=*/false);

  injectNexthopStatuses(
      {NexthopStatus(NexthopKey{.ip = nh}, /*isReachable=*/true, 20)});
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));
  expectForwarding(/*reachable=*/true);
}

/*
 * The cost-optional FBOSS result in VRF 7 must not satisfy EBB's default IGP
 * lookup, even when the two registrations use the same nexthop address.
 */
TEST_P(
    E2ENexthopVrfCompatibilityTest,
    OtherNextHopVrfCannotChangeDefaultEbbSelection) {
  bringUpAllPeersWithEor();
  const folly::IPAddress nh{"11.0.0.1"};
  const folly::IPAddress markerNh{"11.0.0.2"};
  const auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  auto cache = getNexthopCache();
  const NexthopKey otherVrfNexthop{.ip = nh, .nextHopVrfId = 7};
  injectNexthopStatuses(
      {NexthopStatus(NexthopKey{.ip = nh}, /*isReachable=*/true, 10)});
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, nh.str(), "65001");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  ASSERT_TRUE(
      verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));
  addRoute("v4", "20.0.0.0", 8, kPeerAddr3, markerNh.str(), "65001");
  injectNexthopStatuses(
      {NexthopStatus(NexthopKey{.ip = markerNh}, /*isReachable=*/true, 10)});
  ASSERT_TRUE(
      verifyRouteAdd("v4", "20.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));
  const auto defaultEpoch =
      cache->getNexthopStatus(NexthopKey{.ip = nh})->getEpoch();
  const auto ribEpoch = getRibNexthopEpoch(nh);
  ASSERT_TRUE(ribEpoch.has_value());
  EXPECT_EQ(defaultEpoch, *ribEpoch);
  const auto bestPath = getBestPath(prefix);
  ASSERT_NE(nullptr, bestPath);
  const auto defaultFib = getFibWeightedNexthops("10.0.0.0/8");
  ASSERT_NE(nullptr, defaultFib);

  cache->registerAndGetNexthopStatus(otherVrfNexthop);
  const auto updates = cache->addOrUpdateNextHopStatus({NexthopStatus(
      otherVrfNexthop,
      /*isReachable=*/true,
      std::nullopt,
      /*isConnected=*/std::nullopt,
      /*excludeNexthopWithoutCost=*/false)});
  ASSERT_EQ(1, updates.size());
  EXPECT_TRUE(
      cache->getNexthopStatus(otherVrfNexthop)->isResolvedForSelection());

  EXPECT_EQ(otherVrfNexthop, updates.front().getNexthopKey());
  ribInQ_.fiberPush(RibInNexthopUpdate(updates));
  /* The marker's withdrawal acknowledges the preceding VRF 7 queue message. */
  injectNexthopStatuses(
      {NexthopStatus(NexthopKey{.ip = markerNh}, /*isReachable=*/false)});
  ASSERT_TRUE(verifyRouteWithdraw("v4", "20.0.0.0", 8, kPeerAddr5));
  EXPECT_EQ(bestPath, getBestPath(prefix));
  EXPECT_EQ(ribEpoch, getRibNexthopEpoch(nh));
  const auto fib = getFibWeightedNexthops("10.0.0.0/8");
  ASSERT_NE(nullptr, fib);
  EXPECT_EQ(*defaultFib, *fib);
  EXPECT_EQ(
      defaultEpoch, cache->getNexthopStatus(NexthopKey{.ip = nh})->getEpoch());
  EXPECT_EQ(10, cache->getNexthopStatus(NexthopKey{.ip = nh})->getIgpCost());
  expectForwarding(/*reachable=*/true);

  injectNexthopStatuses({NexthopStatus(
      NexthopKey{.ip = nh}, /*isReachable=*/true, std::nullopt)});
  ASSERT_TRUE(verifyRouteWithdraw("v4", "10.0.0.0", 8, kPeerAddr5));
  EXPECT_EQ(nullptr, getBestPath(prefix));
  expectForwarding(/*reachable=*/false);
  const auto status = cache->getNexthopStatus(NexthopKey{.ip = nh});
  ASSERT_TRUE(status.has_value());
  EXPECT_TRUE(status->isReachable());
  EXPECT_FALSE(status->isResolvedForSelection());
  EXPECT_EQ(std::nullopt, status->getIgpCost());
  EXPECT_TRUE(
      cache->getNexthopStatus(otherVrfNexthop)->isResolvedForSelection());

  cache->unregisterAndRemoveNexthopStatus(otherVrfNexthop);
  injectNexthopStatuses(
      {NexthopStatus(NexthopKey{.ip = nh}, /*isReachable=*/true, 20)});
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));
  expectForwarding(/*reachable=*/true);
}

/* EBB retains a cost-bearing route until its resolution key receives the
 * update.
 */
TEST_P(
    E2ENexthopVrfCompatibilityTest,
    UntrackedResolutionVrfUpdatePreservesDefaultEbbRoutes) {
  bringUpAllPeersWithEor();
  const folly::IPAddress nh{"11.0.0.1"};
  const folly::IPAddress markerNh{"11.0.0.2"};
  const auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  injectNexthopStatuses(
      {NexthopStatus(NexthopKey{.ip = nh}, /*isReachable=*/true, 10),
       NexthopStatus(NexthopKey{.ip = markerNh}, /*isReachable=*/true, 10)});
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, nh.str(), "65001");
  ASSERT_TRUE(
      verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));
  addRoute("v4", "20.0.0.0", 8, kPeerAddr3, markerNh.str(), "65001");
  ASSERT_TRUE(
      verifyRouteAdd("v4", "20.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));

  /* Reachable without an IGP cost is pending withdrawal in the EBB RIB. */
  const auto pending =
      getNexthopCache()->addOrUpdateNextHopStatus({NexthopStatus(
          NexthopKey{.ip = nh}, /*isReachable=*/true, std::nullopt)});
  ASSERT_EQ(1, pending.size());
  ribInQ_.fiberPush(RibInNexthopUpdate({NexthopStatus(
      NexthopKey{.ip = nh, .nextHopVrfId = 7},
      /*isReachable=*/true,
      std::nullopt)}));
  /* A separate route's withdrawal acknowledges the preceding queue messages. */
  injectNexthopStatuses(
      {NexthopStatus(NexthopKey{.ip = markerNh}, /*isReachable=*/false)});
  ASSERT_TRUE(verifyRouteWithdraw("v4", "20.0.0.0", 8, kPeerAddr5));
  EXPECT_NE(nullptr, getBestPath(prefix));
  ASSERT_NO_FATAL_FAILURE(expectForwarding(/*reachable=*/true));

  ribInQ_.fiberPush(RibInNexthopUpdate(pending));
  ASSERT_TRUE(verifyRouteWithdraw("v4", "10.0.0.0", 8, kPeerAddr5));
  EXPECT_EQ(nullptr, getBestPath(prefix));
  ASSERT_NO_FATAL_FAILURE(expectForwarding(/*reachable=*/false));
}

INSTANTIATE_TEST_SUITE_P(
    EbbUpdateModes,
    E2ENexthopVrfCompatibilityTest,
    testing::Bool(),
    [](const testing::TestParamInfo<bool>& info) {
      return info.param ? "SerializedGroups" : "IndividualPeers";
    });

} // namespace facebook::bgp
