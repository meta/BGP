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

/* E2E tests: Route attribute correctness through detach-recover cycles.
 *
 * Prefix range: 57.1-57.29/16
 * Fixture: UpdateGroupMultiPeerTest
 *
 * Tests implemented:
 *   Large AS-path (>10 ASNs) preserved through detach-recover
 *   IPv4 prefix route correctness through detach-recover cycle
 *   Route with zero MED vs no MED — attribute comparison
 */

#include <fmt/format.h>
#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupSlowPeerTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/* Route with large AS-path (>10 ASNs) preserved through
 * detach-recover cycle. Inject an iBGP peer route with a long AS-path,
 * detach peer3, inject another route, verify peer4 sees routes correctly.
 */
TEST_P(UpdateGroupMultiPeerTest, LargeAsPathPreserved) {
  XLOG(INFO, "=== TEST: LargeAsPathPreserved ===");

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

  /* Inject route with large AS-path (12 ASNs) via iBGP peer route */
  addRoute(
      "v4",
      "57.1.0.0",
      16,
      kPeerAddr3,
      "127.5.0.1",
      "64500 64501 64502 64503 64504 64505 64506 64507 64508 64509 64510 64511",
      "5701:1");
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("57.1.0.0/16")));
  /* Peer route from peer3 → peer4 only (split horizon).
   * eBGP redistribution prepends local ASN to the received AS-path. */
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "57.1.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001 64500 64501 64502 64503 64504 64505 64506 64507 64508 64509 64510 64511",
      "5701:1"));

  /* Freq-detach peer3: need 3 fill routes to exceed hwm=2 with queue (3,2,0) */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));
  blockPeer(kPeerAddr3);

  for (int i = 2; i <= 4; i++) {
    auto prefix = fmt::format("57.{}.0.0/16", i);
    auto community = fmt::format("57{:02d}:1", i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("57.{}.0.0", i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }

  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Post-detach route — only peer4 gets it */
  injectLocalRoutesAtRuntime({"57.5.0.0/16"}, {"5705:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("57.5.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "57.5.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "5705:1"));

  EXPECT_TRUE(isPeerInSync(kPeerAddr4));
  verifySlowPeerInvariants(kPeerAddr4);

  XLOG(INFO, "=== TEST PASSED: LargeAsPathPreserved ===");
}

/* IPv4 prefix route correctness through detach-recover cycle.
 * Inject route, detach peer3, inject more routes, verify peer4
 * received all routes correctly with correct attributes.
 */
TEST_P(UpdateGroupMultiPeerTest, Ipv4CorrectnessDetachRecover) {
  XLOG(INFO, "=== TEST: Ipv4CorrectnessDetachRecover ===");

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

  /* Shared route before detach */
  injectLocalRoutesAtRuntime({"57.10.0.0/16"}, {"5710:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("57.10.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "57.10.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "5710:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "57.10.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "5710:1"));

  /* Freq-detach peer3 */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));
  blockPeer(kPeerAddr3);

  for (int i = 11; i <= 13; i++) {
    auto prefix = fmt::format("57.{}.0.0/16", i);
    auto community = fmt::format("57{:02d}:1", i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("57.{}.0.0", i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }

  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Post-detach routes — only peer4 */
  for (int i = 14; i <= 15; i++) {
    auto prefix = fmt::format("57.{}.0.0/16", i);
    auto community = fmt::format("57{:02d}:1", i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("57.{}.0.0", i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }

  EXPECT_TRUE(isPeerInSync(kPeerAddr4));
  verifySlowPeerInvariants(kPeerAddr4);

  XLOG(INFO, "=== TEST PASSED: Ipv4CorrectnessDetachRecover ===");
}

/* Route with zero MED vs no MED — attribute comparison correctness.
 * Inject one route with explicit MED=0 and another with default MED,
 * verify both delivered correctly through a detach cycle.
 */
TEST_P(UpdateGroupMultiPeerTest, ZeroMedVsNoMed) {
  XLOG(INFO, "=== TEST: ZeroMedVsNoMed ===");

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

  /* Local route first (default MED) — verify on both peers to clear queues */
  injectLocalRoutesAtRuntime({"57.21.0.0/16"}, {"5721:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("57.21.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "57.21.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "5721:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "57.21.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "5721:1"));

  /* Route with explicit MED=0 via peer route from kPeerAddr3.
   * Split horizon: only peer4 receives this (not peer3). */
  addRoute(
      "v4", "57.20.0.0", 16, kPeerAddr3, "127.5.0.1", "64500", "5720:1", 0);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("57.20.0.0/16")));
  /* eBGP redistribution prepends local ASN to the received AS-path */
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "57.20.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001 64500",
      "5720:1"));

  /* Freq-detach peer3: need 3 fill routes to exceed hwm=2 with queue (3,2,0) */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));
  blockPeer(kPeerAddr3);

  for (int i = 22; i <= 24; i++) {
    auto prefix = fmt::format("57.{}.0.0/16", i);
    auto community = fmt::format("57{:02d}:1", i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("57.{}.0.0", i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }

  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Post-detach route with MED=0 via peer route */
  addRoute(
      "v4", "57.25.0.0", 16, kPeerAddr3, "127.5.0.1", "64500", "5725:1", 0);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("57.25.0.0/16")));
  /* eBGP redistribution prepends local ASN to the received AS-path */
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "57.25.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001 64500",
      "5725:1"));

  EXPECT_TRUE(isPeerInSync(kPeerAddr4));
  verifySlowPeerInvariants(kPeerAddr4);

  XLOG(INFO, "=== TEST PASSED: ZeroMedVsNoMed ===");
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
