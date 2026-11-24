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
 * UpdateGroupPeerJoinSyncTest_Part3.cpp
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
 * PART 3: Route change tests (withdraw and attribute changes)
 */

#include <fmt/format.h>

#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupDistributionTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/*
 * ==================================================================
 * TEST CASE 5: New peer joins, then routes are withdrawn
 * ==================================================================
 */
TEST_P(UpdateGroupPeerJoinSyncTest, NewPeerJoinThenWithdraw) {
  XLOG(INFO, "=== TEST: NewPeerJoinThenWithdraw ===");

  /* Setup */
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
  std::vector<std::string> prefixes;
  for (int i = 1; i <= 3; i++) {
    prefixes.push_back(fmt::format("{}.0.0.0/8", i));
    injectLocalRoutesAtRuntime(
        {prefixes.back()}, {fmt::format("500:{}", i)}, 150);
    ASSERT_TRUE(waitForRouteInShadowRib(
        folly::IPAddress::createNetwork(prefixes.back())));
  }

  XLOG(INFO, "Routes injected");

  /* Build expected routes for ALL 3 routes initially */
  std::vector<E2ETestFixture::VerifySpec> allRoutesPeer3;
  std::vector<E2ETestFixture::VerifySpec> allRoutesPeer4;
  for (int i = 1; i <= 3; i++) {
    allRoutesPeer3.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = kNextHopV4_3.str(),
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("500:{}", i)});
    allRoutesPeer4.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = kNextHopV4_4.str(),
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("500:{}", i)});
  }

  /* Verify existing peers receive all 3 routes BEFORE new peer joins */
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr3, allRoutesPeer3));
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr4, allRoutesPeer4));
  XLOG(INFO, "Existing peers have all routes");

  /* Bring up NEW peer (peer5 already in config) */
  XLOG(INFO, "Bringing up NEW peer5");
  bringUpPeer(kPeerAddr5);
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};
  sendEoRToPeer(peerId5);

  /* Build expected routes for peer5 - all 3 routes */
  std::vector<E2ETestFixture::VerifySpec> allRoutesPeer5;
  for (int i = 1; i <= 3; i++) {
    allRoutesPeer5.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = kNextHopV4_5.str(),
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("500:{}", i)});
  }

  /* Verify peer5 receives all 3 routes via initial dump */
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr5, allRoutesPeer5));
  EXPECT_TRUE(waitForEoR(peerId5));
  XLOG(INFO, "peer5 has all routes");

  /* Withdraw first 2 routes */
  XLOG(INFO, "Withdrawing first 2 routes");
  std::vector<std::string> withdrawPrefixes(
      prefixes.begin(), prefixes.begin() + 2);
  withdrawLocalRoutesAtRuntime(withdrawPrefixes);

  /* Build expected withdrawals */
  std::vector<E2ETestFixture::WithdrawSpec> withdrawSpecs;
  for (int i = 1; i <= 2; i++) {
    withdrawSpecs.push_back(
        {.prefix = fmt::format("{}.0.0.0", i), .prefixLen = 8});
  }

  /* Verify all peers receive withdrawals */
  EXPECT_TRUE(verifyRouteWithdraws("v4", kPeerAddr3, withdrawSpecs));
  EXPECT_TRUE(verifyRouteWithdraws("v4", kPeerAddr4, withdrawSpecs));
  EXPECT_TRUE(verifyRouteWithdraws("v4", kPeerAddr5, withdrawSpecs));

  XLOG(INFO, "=== TEST PASSED ===");
}

/*
 * ==================================================================
 * TEST CASE 6: New peer joins, then route attributes change
 * ==================================================================
 */
TEST_P(UpdateGroupPeerJoinSyncTest, NewPeerJoinThenAttrChange) {
  XLOG(INFO, "=== TEST: NewPeerJoinThenAttrChange ===");

  /* Setup: Add all peers to config before setupComponents, bring up only 2 */
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
  for (int i = 1; i <= 3; i++) {
    injectLocalRoutesAtRuntime(
        {fmt::format("{}.0.0.0/8", i)}, {fmt::format("600:{}", i)}, 150);
    ASSERT_TRUE(waitForRouteInShadowRib(
        folly::IPAddress::createNetwork(fmt::format("{}.0.0.0/8", i))));
  }

  /* Build expected routes for original attributes */
  std::vector<E2ETestFixture::VerifySpec> originalRoutesPeer3;
  std::vector<E2ETestFixture::VerifySpec> originalRoutesPeer4;
  for (int i = 1; i <= 3; i++) {
    originalRoutesPeer3.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = kNextHopV4_3.str(),
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("600:{}", i)});
    originalRoutesPeer4.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = kNextHopV4_4.str(),
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("600:{}", i)});
  }

  /* Verify existing peers receive original routes BEFORE new peer joins */
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr3, originalRoutesPeer3));
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr4, originalRoutesPeer4));
  XLOG(INFO, "Existing peers have original routes");

  /* Bring up NEW peer (peer5 already in config) */
  XLOG(INFO, "Bringing up NEW peer5");
  bringUpPeer(kPeerAddr5);
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};
  sendEoRToPeer(peerId5);

  /* Build expected routes for peer5 - original attributes */
  std::vector<E2ETestFixture::VerifySpec> originalRoutesPeer5;
  for (int i = 1; i <= 3; i++) {
    originalRoutesPeer5.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = kNextHopV4_5.str(),
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("600:{}", i)});
  }

  /* Verify peer5 receives original routes via initial dump */
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr5, originalRoutesPeer5));
  EXPECT_TRUE(waitForEoR(peerId5));
  XLOG(INFO, "peer5 has original routes");

  /* Change attributes - change community and local pref */
  XLOG(INFO, "Changing route attributes");
  for (int i = 1; i <= 3; i++) {
    injectLocalRoutesAtRuntime(
        {fmt::format("{}.0.0.0/8", i)},
        {fmt::format("600:{}", i + 100)}, // Changed community
        200); // Changed local pref
  }

  /* Build expected routes for updated attributes */
  std::vector<E2ETestFixture::VerifySpec> updatedRoutesPeer3;
  std::vector<E2ETestFixture::VerifySpec> updatedRoutesPeer4;
  std::vector<E2ETestFixture::VerifySpec> updatedRoutesPeer5;

  for (int i = 1; i <= 3; i++) {
    updatedRoutesPeer3.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = kNextHopV4_3.str(),
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("600:{}", i + 100)});

    updatedRoutesPeer4.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = kNextHopV4_4.str(),
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("600:{}", i + 100)});

    updatedRoutesPeer5.push_back(
        {.prefix = fmt::format("{}.0.0.0", i),
         .prefixLen = 8,
         .expectedNexthop = kNextHopV4_5.str(),
         .expectedAsPath = "4200000001",
         .expectedCommunity = fmt::format("600:{}", i + 100)});
  }

  /* Verify all peers receive updated routes */
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr3, updatedRoutesPeer3));
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr4, updatedRoutesPeer4));
  EXPECT_TRUE(verifyRoutes("v4", kPeerAddr5, updatedRoutesPeer5));

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
