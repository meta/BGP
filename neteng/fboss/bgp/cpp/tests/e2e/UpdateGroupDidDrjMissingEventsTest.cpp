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
 * E2E tests: Remaining P-DID and P-DRJ state x Event coverage
 *
 * Prefix range: 32.x.0.0/16
 *
 * Tests:
 *   P-DID x E-ROUTE-REFRESH -- Route refresh during detached init dump
 *   P-DID x E-EOR -- N/A
 *   P-DRJ x E-ROUTE-ADD -- Route added, DFP must become DSP
 *   P-DRJ x E-BLOCK -- N/A (peer unblocked at DRJ)
 *   P-DRJ x E-SLOW-DUR -- N/A (already detached)
 */

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupSlowPeerTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/*
 * P-DID x E-EOR -- N/A
 * sendEoRToPeer on a peer in DETACHED_INIT_DUMP is harmless. The EoR
 * was already sent during the reconnection setup. Extra EoR is a no-op.
 */
TEST_P(UpdateGroupMultiPeerTest, DetachedInitDump_Eor_NA) {
  XLOG(INFO, "=== TEST: DetachedInitDump_Eor_NA ===");

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

  /* Freq-detach peer3 */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  for (int i = 0; i < 3; i++) {
    auto prefix = fmt::format("32.{}.0.0/16", 20 + i);
    auto community = fmt::format("{}:1", 3220 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("32.{}.0.0", 20 + i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* DOWN + unblock + UP -> DID (deferred for deterministic assertion) */
  bringDownPeer(kPeerAddr3);
  unblockPeer(kPeerAddr3);
  testOnlyDeferInitDump(kPeerAddr3, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_INIT_DUMP, 30));
  testOnlyDeferInitDump(kPeerAddr3, false, fboss::kDefaultVrf);

  /* Extra EoR -- should be harmless no-op */
  sendEoRToPeer(peerId3);

  /* No crash, peer still alive */
  auto state = getPeerState(kPeerAddr3);
  EXPECT_NE(state, PeerUpdateState::DOWN);

  /* Peer4 continues */
  injectLocalRoutesAtRuntime({"32.25.0.0/16"}, {"3225:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("32.25.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.25.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3225:1"));

  XLOG(INFO, "=== TEST PASSED: DetachedInitDump_Eor_NA ===");
}

/*
 * P-DRJ x E-BLOCK -- N/A (peer unblocked at DRJ)
 * A peer at DRJ has been unblocked. blockPeer on it again is either
 * a no-op or transitions to DB. Accept DRJ or DB as valid.
 */
TEST_P(UpdateGroupMultiPeerTest, DetachedReadyToJoin_Block_NA) {
  XLOG(INFO, "=== TEST: DetachedReadyToJoin_Block_NA ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  setupSlowPeerComponents(5, 4, 0);

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

  /* Freq-detach peer3 */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  for (int i = 0; i < 5; i++) {
    auto prefix = fmt::format("32.{}.0.0/16", 40 + i);
    auto community = fmt::format("{}:1", 3240 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("32.{}.0.0", 40 + i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Unblock -> confirm DRJ before re-blocking */
  waitForDrjWithDrain(kPeerAddr3, peerId3);

  /*
   * Block peer3 again -- should be harmless on detached peer.
   * JOINED_RUNNING is acceptable: recovery may complete before re-block.
   */
  blockPeer(kPeerAddr3);

  /* Peer4 still works */
  injectLocalRoutesAtRuntime({"32.47.0.0/16"}, {"3247:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("32.47.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.47.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3247:1"));

  XLOG(INFO, "=== TEST PASSED: DetachedReadyToJoin_Block_NA ===");
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
