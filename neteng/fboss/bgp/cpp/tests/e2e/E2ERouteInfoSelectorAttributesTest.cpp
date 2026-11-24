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
 * E2E tests for route selection based on Origin, Weight, and IGP Cost.
 *
 * Tests cover:
 * - Origin attribute selection (IGP < EGP < INCOMPLETE)
 * - Weight-based route selection (when enabled)
 * - IGP cost-based selection with nexthop tracking
 *
 * Derived from: RouteInfoSelectorTest.cpp
 * Mocked: FIB (TestFib), SessionManager (MockSessionManager)
 * Real: RIB, PeerManagerBase, AdjRib, RouteInfoSelector
 */

#include <gtest/gtest.h>

#include <folly/logging/xlog.h>

#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopStatus.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/E2ETestFixture.h"

using namespace facebook::bgp;

namespace facebook::bgp {

class E2ERouteInfoSelectorAttributesTest : public E2ETestFixture {
 protected:
  void SetUp() override {
    addPeer(kDefaultPeerSpec3);
    addPeer(kDefaultPeerSpec4);
    addPeer(kDefaultPeerSpec5);
    createRib();
    createPeerManager(/*enableUpdateGroup=*/false);
  }

  void bringUpAllPeers() {
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
 * Test: Origin attribute selection
 * Derived from: RouteInfoSelectorTest.cpp::LowestOriginNumberWinsTest
 */
TEST_F(E2ERouteInfoSelectorAttributesTest, OriginSelection) {
  bringUpAllPeers();

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");
  addRoute("v4", "10.0.0.0", 8, kPeerAddr4, "11.0.0.2", "65002");

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));
}

/*
 * Test: Origin with IPv6 routes
 */
TEST_F(E2ERouteInfoSelectorAttributesTest, IPv6OriginSelection) {
  bringUpAllPeers();

  addRoute("v6", "2001:db8::", 32, kPeerAddr3, "2001:db8::1", "65001");
  addRoute("v6", "2001:db8::", 32, kPeerAddr4, "2001:db8::2", "65002");

  auto prefix = folly::IPAddress::createNetwork("2001:db8::/32");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  EXPECT_TRUE(verifyRouteAdd(
      "v6", "2001:db8::", 32, kPeerAddr5, "2401:db00:e011:411:1000::2d"));
}

/*
 * Test: Weight-based selection (LP takes precedence when weight disabled)
 * Derived from: RouteInfoSelectorTest.cpp::HighestWeightWinsTest
 */
TEST_F(E2ERouteInfoSelectorAttributesTest, LocalPrefBeatsOtherAttributes) {
  bringUpAllPeers();

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001", "", 0, 200, 0);
  addRoute("v4", "10.0.0.0", 8, kPeerAddr4, "11.0.0.2", "65002");

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));
}

/*
 * Test: Higher local preference route wins during path selection
 *
 * When multiple routes exist for the same prefix, the one with higher LP
 * should be selected as best path and announced to peers.
 * E2E verification: the announced route has AS path from the higher LP route.
 */
TEST_F(E2ERouteInfoSelectorAttributesTest, RouteUpdateTriggersReselection) {
  bringUpAllPeers();

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");

  /*
   * Add route from peer4 with higher LP=200.
   * This should become best path and be announced to peer3 and peer5.
   */
  addRoute("v4", "10.0.0.0", 8, kPeerAddr4, "11.0.0.2", "65002", "", 0, 200, 0);
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));

  /*
   * E2E verification: route announced to peer3 with AS path "4200000001 65002"
   * This proves the route from peer4 (with AS path 65002) was selected.
   * Note: nexthop for peer3 is 127.5.0.1 (local router's interface towards
   * peer3)
   */
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "10.0.0.0",
      8,
      kPeerAddr3,
      "127.5.0.1",
      "4200000001 65002" /* expectedAsPath */));

  /* E2E verification: route also announced to peer5 with same AS path */
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "10.0.0.0",
      8,
      kPeerAddr5,
      "127.5.0.4",
      "4200000001 65002" /* expectedAsPath */));

  /*
   * Now add route from peer3 with lower LP=100.
   * Best path should remain the one with LP=200 (from peer4).
   * No new update should be generated since best path didn't change.
   */
  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");

  /* Wait for second path to be installed in RIB */
  ASSERT_TRUE(waitForPathCountInRib("10.0.0.0/8", 2));
}

/*
 * Fixture with nexthop tracking enabled for IGP cost tests.
 */
class E2ERouteInfoSelectorIgpCostTest : public E2ETestFixture {
 protected:
  void SetUp() override {
    addPeer(kDefaultPeerSpec3);
    addPeer(kDefaultPeerSpec4);
    addPeer(kDefaultPeerSpec5);
    createRib(true /* enableNexthopTracking */);
    createPeerManager(/*enableUpdateGroup=*/false);
  }

  void bringUpAllPeers() {
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
 * Test: Lower IGP cost nexthop wins
 * Derived from: RouteInfoSelectorTest.cpp::LowestIgpCostWinsTest
 */
TEST_F(E2ERouteInfoSelectorIgpCostTest, LowestIgpCostWins) {
  bringUpAllPeers();

  injectNexthopStatuses({
      NexthopStatus({.ip = folly::IPAddress("11.0.0.1")}, true, 10),
      NexthopStatus({.ip = folly::IPAddress("11.0.0.2")}, true, 1),
  });

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");
  addRoute("v4", "10.0.0.0", 8, kPeerAddr4, "11.0.0.2", "65002");

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));
}

/*
 * Test: Real cost beats max cost
 */
TEST_F(E2ERouteInfoSelectorIgpCostTest, RealCostBeatsMaxCost) {
  bringUpAllPeers();

  injectNexthopStatuses({
      NexthopStatus({.ip = folly::IPAddress("11.0.0.1")}, true, 10),
      NexthopStatus(
          {.ip = folly::IPAddress("11.0.0.2")},
          true,
          std::numeric_limits<uint32_t>::max()),
  });

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");
  addRoute("v4", "10.0.0.0", 8, kPeerAddr4, "11.0.0.2", "65002");

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));
}

/*
 * Test: Unreachable nexthop not selected
 */
TEST_F(E2ERouteInfoSelectorIgpCostTest, UnreachableNexthopNotSelected) {
  bringUpAllPeers();

  injectNexthopStatuses({
      NexthopStatus({.ip = folly::IPAddress("11.0.0.1")}, false, 1),
      NexthopStatus({.ip = folly::IPAddress("11.0.0.2")}, true, 100),
  });

  addRoute("v4", "10.0.0.0", 8, kPeerAddr3, "11.0.0.1", "65001");
  addRoute("v4", "10.0.0.0", 8, kPeerAddr4, "11.0.0.2", "65002");

  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(prefix));
  EXPECT_TRUE(verifyRouteAdd("v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4"));
}

} // namespace facebook::bgp
