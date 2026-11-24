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
 * UpdateGroupPeerJoinSyncTest_Part2.cpp
 *
 * Critical tests for peer joining update group during active processing
 * Tests synchronization when new peers join a busy update group with:
 * - Active change list processing
 * - Active packing list processing
 * - Peers blocking/unblocking
 *
 * Key scenarios:
 * - New peer joins → gets separate initial dump
 * - Peer catches up to group OR group catches up to peer
 * - Peer down/up events during synchronization
 * - Route changes (withdraw/attr change) during catch-up
 *
 * Parameterized by serialization mode (enableSerializeGroupPdu)
 *
 * PART 2: Peer down/up tests
 */

#include <fmt/format.h>

#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupDistributionTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/*
 * ==================================================================
 * TEST CASE 3: New peer joins, then existing peer goes down
 * ==================================================================
 */
TEST_P(UpdateGroupPeerJoinSyncTest, NewPeerJoinThenPeerDown) {
  XLOG(INFO, "=== TEST: NewPeerJoinThenPeerDown ===");

  /* Setup: Create update group with 2 peers (but peer5 in config for later) */
  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  BgpPeerSpec spec5 = {
      .asn = kPeerAsn5,
      .localAddr = kLocalAddr1,
      .peerAddr = kPeerAddr5,
      .v4Nexthop = kNextHopV4_5,
      .v6Nexthop = kNextHopV6_5,
      .description = kDescription1};
  addPeer(spec5);
  setupComponents();

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));

  XLOG(INFO, "Initial setup complete - 2 peers in update group");

  /* Inject routes - keep count small for reliable async completion */
  XLOG(INFO, "Injecting 3 routes");
  for (int i = 1; i <= 3; i++) {
    injectLocalRoutesAtRuntime(
        {fmt::format("{}.0.0.0/8", i)}, {fmt::format("300:{}", i)}, 150);
    ASSERT_TRUE(waitForRouteInShadowRib(
        folly::IPAddress::createNetwork(fmt::format("{}.0.0.0/8", i))));
  }

  XLOG(INFO, "Routes injected");

  /* Build expected routes for each peer (different nexthops for eBGP) */
  std::vector<E2ETestFixture::VerifySpec> expectedRoutesPeer3;
  std::vector<E2ETestFixture::VerifySpec> expectedRoutesPeer4;
  std::vector<E2ETestFixture::VerifySpec> expectedRoutesPeer5;
  for (int i = 1; i <= 3; i++) {
    expectedRoutesPeer3.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = kNextHopV4_3.str(),
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("300:{}", i)});
    expectedRoutesPeer4.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = kNextHopV4_4.str(),
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("300:{}", i)});
    expectedRoutesPeer5.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = kNextHopV4_5.str(),
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("300:{}", i)});
  }

  /* Verify existing peers receive routes BEFORE new peer joins */
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr3, expectedRoutesPeer3));
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr4, expectedRoutesPeer4));
  XLOG(INFO, "Existing peers have all routes");

  /* Bring up NEW peer (peer5) */
  XLOG(INFO, "Bringing up NEW peer5");
  bringUpPeer(kPeerAddr5);
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};
  sendEoRToPeer(peerId5);

  /* NOW: peer3 goes down */
  XLOG(INFO, "peer3 goes down");
  bringDownPeer(kPeerAddr3);

  /* Verify peer4 and peer5 continue correctly */
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr5, expectedRoutesPeer5));
  EXPECT_TRUE(waitForEoR(peerId5));

  XLOG(INFO, "=== TEST PASSED ===");
}

/*
 * ==================================================================
 * TEST CASE 4: New peer joins, then peer goes down and comes back up
 * ==================================================================
 */
TEST_P(UpdateGroupPeerJoinSyncTest, NewPeerJoinThenPeerBackUp) {
  XLOG(INFO, "=== TEST: NewPeerJoinThenPeerBackUp ===");

  /* Setup: Create update group with 2 peers (but peer5 in config for later) */
  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  BgpPeerSpec spec5 = {
      .asn = kPeerAsn5,
      .localAddr = kLocalAddr1,
      .peerAddr = kPeerAddr5,
      .v4Nexthop = kNextHopV4_5,
      .v6Nexthop = kNextHopV6_5,
      .description = kDescription1};
  addPeer(spec5);
  setupComponents();

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));

  /* Inject routes */
  XLOG(INFO, "Injecting 3 routes");
  for (int i = 1; i <= 3; i++) {
    injectLocalRoutesAtRuntime(
        {fmt::format("{}.0.0.0/8", i)}, {fmt::format("400:{}", i)}, 150);
    ASSERT_TRUE(waitForRouteInShadowRib(
        folly::IPAddress::createNetwork(fmt::format("{}.0.0.0/8", i))));
  }

  XLOG(INFO, "Routes injected");

  /* Build expected routes for each peer (different nexthops for eBGP) */
  std::vector<E2ETestFixture::VerifySpec> expectedRoutesPeer3;
  std::vector<E2ETestFixture::VerifySpec> expectedRoutesPeer4;
  std::vector<E2ETestFixture::VerifySpec> expectedRoutesPeer5;
  for (int i = 1; i <= 3; i++) {
    expectedRoutesPeer3.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = kNextHopV4_3.str(),
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("400:{}", i)});
    expectedRoutesPeer4.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = kNextHopV4_4.str(),
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("400:{}", i)});
    expectedRoutesPeer5.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = kNextHopV4_5.str(),
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("400:{}", i)});
  }

  /* Verify existing peers receive routes BEFORE new peer joins */
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr3, expectedRoutesPeer3));
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr4, expectedRoutesPeer4));
  XLOG(INFO, "Existing peers have all routes");

  /* Bring up NEW peer (peer5) */
  XLOG(INFO, "Bringing up NEW peer5");
  bringUpPeer(kPeerAddr5);
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};
  sendEoRToPeer(peerId5);

  /* Verify peer5 gets routes via initial dump */
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr5, expectedRoutesPeer5));
  EXPECT_TRUE(waitForEoR(peerId5));
  XLOG(INFO, "peer5 has all routes");

  /* peer3 goes down */
  XLOG(INFO, "peer3 goes down");
  bringDownPeer(kPeerAddr3);

  /* peer3 comes back up */
  XLOG(INFO, "peer3 comes back up");
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);

  /* Verify peer3 receives routes again via initial dump */
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr3, expectedRoutesPeer3));
  EXPECT_TRUE(waitForEoR(peerId3));

  XLOG(INFO, "=== TEST PASSED ===");
}

/*
 * ==================================================================
 * TEST CASE 5: Peer reconnects and receives incremental updates
 *
 * This test verifies that when a peer goes down and comes back up,
 * it can receive INCREMENTAL route updates (not just the initial dump).
 *
 * The bug: When peer leaves update group (session down), its
 * changeListConsumer is reset to nullptr. When peer reconnects,
 * the consumer must be recreated for incremental updates to work.
 *
 * Without the fix, the peer would receive initial dump but miss
 * any routes injected while it's in DETACHED_INIT_DUMP state.
 * ==================================================================
 */
TEST_P(
    UpdateGroupPeerJoinSyncTest,
    ReconnectingPeerReceivesIncrementalUpdates) {
  XLOG(INFO, "=== TEST: ReconnectingPeerReceivesIncrementalUpdates ===");

  /* Setup: Create update group with 2 peers */
  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  setupComponents();

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));

  XLOG(INFO, "Initial setup complete - 2 peers in update group");

  /* Inject initial routes */
  XLOG(INFO, "Injecting initial 2 routes");
  for (int i = 1; i <= 2; i++) {
    injectLocalRoutesAtRuntime(
        {fmt::format("{}.0.0.0/8", i)}, {fmt::format("500:{}", i)}, 150);
    ASSERT_TRUE(waitForRouteInShadowRib(
        folly::IPAddress::createNetwork(fmt::format("{}.0.0.0/8", i))));
  }

  /* Build expected routes for initial set */
  std::vector<E2ETestFixture::VerifySpec> initialRoutesPeer3;
  std::vector<E2ETestFixture::VerifySpec> initialRoutesPeer4;
  for (int i = 1; i <= 2; i++) {
    initialRoutesPeer3.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = "127.5.0.1",
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("500:{}", i)});
    initialRoutesPeer4.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = "127.5.0.3",
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("500:{}", i)});
  }

  /* Verify both peers receive initial routes */
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr3, initialRoutesPeer3));
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr4, initialRoutesPeer4));
  XLOG(INFO, "Both peers have initial routes");

  /* peer3 goes down */
  XLOG(INFO, "peer3 goes down");
  bringDownPeer(kPeerAddr3);

  /* peer3 comes back up - enters DETACHED_INIT_DUMP state */
  XLOG(INFO, "peer3 comes back up (enters DETACHED_INIT_DUMP state)");
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);

  /*
   * Inject NEW routes while peer3 is catching up.
   * These routes must be delivered via the changeListConsumer.
   * Without the fix, peer3's consumer is nullptr and it will miss these.
   */
  XLOG(INFO, "Injecting 2 NEW routes while peer3 is catching up");
  for (int i = 3; i <= 4; i++) {
    injectLocalRoutesAtRuntime(
        {fmt::format("{}.0.0.0/8", i)}, {fmt::format("500:{}", i)}, 150);
    ASSERT_TRUE(waitForRouteInShadowRib(
        folly::IPAddress::createNetwork(fmt::format("{}.0.0.0/8", i))));
  }

  /* Build expected routes for ALL routes (initial + new) */
  std::vector<E2ETestFixture::VerifySpec> allRoutesPeer3;
  std::vector<E2ETestFixture::VerifySpec> allRoutesPeer4;
  for (int i = 1; i <= 4; i++) {
    allRoutesPeer3.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = "127.5.0.1",
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("500:{}", i)});
    allRoutesPeer4.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = "127.5.0.3",
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("500:{}", i)});
  }

  /*
   * CRITICAL: Verify peer3 (reconnected) receives all routes.
   * This includes the NEW routes injected after reconnect.
   * Without the consumer recreation fix, this would FAIL.
   */
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr3, allRoutesPeer3));

  XLOG(INFO, "=== TEST PASSED: peer3 received incremental updates ===");
}

/*
 * Instantiate tests for both serialization modes.
 */
INSTANTIATE_TEST_SUITE_P(
    SerializationModes,
    UpdateGroupPeerJoinSyncTest,
    ::testing::Values(kNoSerialization, kWithSerialization),
    [](const ::testing::TestParamInfo<SerializationParams>& info) {
      return info.param.name;
    });

} // namespace bgp
} // namespace facebook
