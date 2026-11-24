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
 * E2E tests for nexthop resolution changes (Gap 7).
 *
 * Tests verify correct behavior when nexthops become unreachable,
 * recover, or flap. With nexthop tracking enabled, routes with
 * unreachable nexthops should not be selected as bestpath.
 *
 * SEV pattern: Nexthop becomes unreachable → routes should be withdrawn.
 * Nexthop comes back → routes should be re-announced.
 */

#include <gtest/gtest.h>

#include <folly/logging/xlog.h>

#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopStatus.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/E2ETestFixture.h"

using namespace facebook::bgp;

namespace facebook::bgp {

class E2ENexthopFailureTest : public E2ETestFixture {
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

  void markNexthopReachable(const std::string& nexthop, uint32_t igpCost) {
    std::vector<NexthopStatus> statuses;
    statuses.emplace_back(
        NexthopKey{.ip = folly::IPAddress(nexthop)}, true, igpCost);
    injectNexthopStatuses(statuses);
  }

  void markNexthopUnreachable(const std::string& nexthop) {
    std::vector<NexthopStatus> statuses;
    statuses.emplace_back(
        NexthopKey{.ip = folly::IPAddress(nexthop)}, false, std::nullopt);
    injectNexthopStatuses(statuses);
  }
};

/*
 * Start with NH unreachable → routes not active → NH reachable → routes active
 * → NH unreachable again → routes not active.
 *
 * Tests full cycle: unreachable -> reachable -> unreachable
 */
TEST_F(E2ENexthopFailureTest, NexthopUnreachableToReachableToUnreachable) {
  bringUpAllPeersWithEor();

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");

  // Step 1: Start with NH unreachable
  markNexthopUnreachable("11.0.0.1");
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");

  // Verify routes are NOT active (not in shadow RIB) - route never was in RIB
  EXPECT_TRUE(verifyRouteNotInShadowRib(prefix));
  EXPECT_TRUE(verifyNexthopRouteCount(folly::IPAddress("11.0.0.1"), 1));
  XLOG(INFO, "Step 1: NH unreachable - route not active");

  // Step 2: Change NH to reachable
  markNexthopReachable("11.0.0.1", 100);

  // E2E: Verify route IS advertised to downstream peer
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));
  XLOG(INFO, "Step 2: NH reachable - route advertised to peer5");

  // Step 3: Change NH to unreachable again
  markNexthopUnreachable("11.0.0.1");

  // E2E: Verify route IS withdrawn from downstream peer
  EXPECT_TRUE(verifyRouteWithdraw("v4", "10.0.0.0", 8, kPeerAddr5));
  XLOG(INFO, "Step 3: NH unreachable again - route withdrawn from peer5");
}

/*
 * Start with NH reachable → routes active → NH unreachable → routes not active
 * → NH reachable again → routes active.
 *
 * Tests full cycle: reachable -> unreachable -> reachable
 */
TEST_F(E2ENexthopFailureTest, NexthopReachableToUnreachableToReachable) {
  bringUpAllPeersWithEor();

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");

  // Step 1: Start with NH reachable
  markNexthopReachable("11.0.0.1", 100);
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");

  // E2E: Verify route IS advertised to downstream peer
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));
  XLOG(INFO, "Step 1: NH reachable - route advertised to peer5");

  // Step 2: Change NH to unreachable
  markNexthopUnreachable("11.0.0.1");

  // E2E: Verify route IS withdrawn from downstream peer
  EXPECT_TRUE(verifyRouteWithdraw("v4", "10.0.0.0", 8, kPeerAddr5));
  XLOG(INFO, "Step 2: NH unreachable - route withdrawn from peer5");

  // Step 3: Change NH to reachable again
  markNexthopReachable("11.0.0.1", 100);

  // E2E: Verify route IS re-advertised to downstream peer
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));
  XLOG(INFO, "Step 3: NH reachable again - route re-advertised to peer5");
}

/*
 * Multiple routes share same nexthop → all affected when nexthop changes.
 *
 * Tests: NH unreachable → routes not active → NH reachable → all routes active
 * → NH unreachable → all routes not active again.
 */
TEST_F(E2ENexthopFailureTest, MultipleRoutesShareNexthop) {
  bringUpAllPeersWithEor();

  auto prefix1 = folly::IPAddress::createNetwork("10.0.0.0/8");
  auto prefix2 = folly::IPAddress::createNetwork("20.0.0.0/8");
  auto prefix3 = folly::IPAddress::createNetwork("30.0.0.0/8");

  // Step 1: Start with NH unreachable, add multiple routes
  markNexthopUnreachable("11.0.0.1");
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");
  addRoute("v4", "20.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");
  addRoute("v4", "30.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");

  // Verify all routes are NOT active - routes never were in RIB
  EXPECT_TRUE(verifyRouteNotInShadowRib(prefix1));
  EXPECT_TRUE(verifyRouteNotInShadowRib(prefix2));
  EXPECT_TRUE(verifyRouteNotInShadowRib(prefix3));
  EXPECT_TRUE(verifyNexthopRouteCount(folly::IPAddress("11.0.0.1"), 3));
  XLOG(INFO, "Step 1: NH unreachable - all 3 routes not active");

  // Step 2: Change NH to reachable
  markNexthopReachable("11.0.0.1", 100);

  // E2E: Verify all routes ARE advertised to downstream peer
  ASSERT_TRUE(waitForRouteInShadowRib(prefix1));
  ASSERT_TRUE(waitForRouteInShadowRib(prefix2));
  ASSERT_TRUE(waitForRouteInShadowRib(prefix3));
  EXPECT_TRUE(verifyRoutes(
      "v4",
      kPeerAddr5,
      {{"10.0.0.0", 8, "127.5.0.4", "", "", 0},
       {"20.0.0.0", 8, "127.5.0.4", "", "", 0},
       {"30.0.0.0", 8, "127.5.0.4", "", "", 0}}));
  XLOG(INFO, "Step 2: NH reachable - all 3 routes advertised to peer5");

  // Step 3: Change NH to unreachable again
  markNexthopUnreachable("11.0.0.1");

  // E2E: Verify all routes ARE withdrawn from downstream peer
  EXPECT_TRUE(verifyRouteWithdraws(
      "v4",
      kPeerAddr5,
      {{"10.0.0.0", 8, 0}, {"20.0.0.0", 8, 0}, {"30.0.0.0", 8, 0}}));
  XLOG(
      INFO, "Step 3: NH unreachable again - all 3 routes withdrawn from peer5");
}

} // namespace facebook::bgp
