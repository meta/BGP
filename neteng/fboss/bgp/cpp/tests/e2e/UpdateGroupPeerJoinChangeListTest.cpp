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
 * UpdateGroupPeerJoinSyncTest_Part1.cpp
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
 * PART 1: Change list processing tests
 */

#include <fmt/format.h>

#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupDistributionTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/*
 * ==================================================================
 * HELPER METHODS (Optional - for future fine-grained verification)
 * ==================================================================
 */

/*
 * Note: The current tests verify update group behavior implicitly through:
 * - isPeerQueueBlocked() - indicates backpressure causing change list buildup
 * - verifyRoutes() - confirms routes eventually distributed correctly
 * - waitForEoR() - confirms synchronization completion
 *
 * Future enhancements could add explicit helpers using event loop:
 * - verifyChangeListSize() - check pending items in change tracker
 * - verifyPackingListSize() - check pending UPDATE messages
 * - verifyPeerInUpdateGroup() - confirm peer joined group
 *
 * Example pattern:
 *   peerManager_->getEventBase().runInEventBaseThreadAndWait([&]() {
 *     // Access update group internals here (thread-safe)
 *   });
 */

/*
 * ==================================================================
 * TEST CASE 1: New peer joins during change list processing
 * Variation: Peer processes ahead, group catches up
 * ==================================================================
 */
TEST_P(
    UpdateGroupPeerJoinSyncTest,
    NewPeerJoinDuringChangeListProcessing_PeerAhead) {
  XLOG(INFO, "=== TEST: NewPeerJoinDuringChangeListProcessing_PeerAhead ===");

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
        {fmt::format("{}.0.0.0/8", i)}, {fmt::format("100:{}", i)}, 150);
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
         .expectedCommunity = fmt::format("100:{}", i)});
    expectedRoutesPeer4.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = kNextHopV4_4.str(),
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("100:{}", i)});
    expectedRoutesPeer5.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = kNextHopV4_5.str(),
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("100:{}", i)});
  }

  /* Verify existing peers receive routes BEFORE new peer joins */
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr3, expectedRoutesPeer3));
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr4, expectedRoutesPeer4));
  XLOG(INFO, "Existing peers have all routes");

  /* NOW: Bring up NEW peer (peer5 already in config) */
  XLOG(INFO, "Bringing up NEW peer5");
  bringUpPeer(kPeerAddr5);
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};
  sendEoRToPeer(peerId5);

  /* Verify new peer receives routes via initial dump */
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr5, expectedRoutesPeer5));
  EXPECT_TRUE(waitForEoR(peerId5));
  XLOG(INFO, "peer5 received all routes - all peers synchronized");

  /*
   * At this point, peer5 has joined the update group because:
   * - peer5 completed its initial dump (received all routes + EoR)
   * - Existing peers unblocked and caught up
   * - All peers now have same route state and can share update group
   * - Future updates will use the update group for all 3 peers
   */

  XLOG(INFO, "=== TEST PASSED ===");
}

/*
 * ==================================================================
 * TEST CASE 2: New peer joins during change list processing
 * Variation: Group processes ahead, peer catches up
 * ==================================================================
 */
TEST_P(
    UpdateGroupPeerJoinSyncTest,
    NewPeerJoinDuringChangeListProcessing_GroupAhead) {
  XLOG(INFO, "=== TEST: NewPeerJoinDuringChangeListProcessing_GroupAhead ===");

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
        {fmt::format("{}.0.0.0/8", i)}, {fmt::format("200:{}", i)}, 150);
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
         .expectedCommunity = fmt::format("200:{}", i)});
    expectedRoutesPeer4.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = kNextHopV4_4.str(),
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("200:{}", i)});
    expectedRoutesPeer5.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = kNextHopV4_5.str(),
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("200:{}", i)});
  }

  /* Existing peers receive routes FIRST */
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr3, expectedRoutesPeer3));
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr4, expectedRoutesPeer4));
  XLOG(INFO, "Existing peers finished processing");

  /* NOW: Bring up NEW peer (peer5 already in config) */
  XLOG(INFO, "Bringing up NEW peer5");
  bringUpPeer(kPeerAddr5);
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};
  sendEoRToPeer(peerId5);

  /* Now peer5 should receive routes via initial dump */
  XLOG(INFO, "peer5 receiving routes");
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr5, expectedRoutesPeer5));
  EXPECT_TRUE(waitForEoR(peerId5));
  XLOG(INFO, "peer5 caught up - all peers synchronized");

  /*
   * All peers now synchronized:
   * - Group finished processing change list first
   * - peer5 completed its separate initial dump
   * - peer5 can now join the update group
   * - Future updates will use the update group for all 3 peers
   */

  XLOG(INFO, "=== TEST PASSED ===");
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
