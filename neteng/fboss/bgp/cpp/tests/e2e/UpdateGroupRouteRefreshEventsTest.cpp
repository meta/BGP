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
 * E2E tests: Route refresh events across different peer states
 *
 * Prefix range: 30.x.0.0/16
 *
 * Tests:
 *   Route refresh for JOINED_RUNNING peer — simulate with burst
 *   Route refresh for DETACHED_BLOCKED peer — already detached
 *   Route refresh for DETACHED_INIT_DUMP peer — defer
 *   Route refresh during acceptance — defer RR
 *   Route refresh for all peers simultaneously — all detach
 */

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupSlowPeerTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/*
 * Route refresh for DETACHED_INIT_DUMP peer
 * Reach DID via freq-detach → down → unblock + up → peer enters DID.
 * Simulate route refresh (inject routes) while peer3 is in DID.
 * Routes go to CL, peer4 receives them. Peer3 stays in DID.
 */
TEST_P(UpdateGroupMultiPeerTest, DetachedInitDump_RouteRefresh) {
  XLOG(INFO, "=== TEST: DetachedInitDump_RouteRefresh ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  setupSlowPeerComponents(3, 2, 0);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);

  /* Consume v4 + v6 EoRs (dual-stack sends 2 EoRs per peer) */
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
  for (int i = 17; i <= 19; ++i) {
    auto prefix = fmt::format("30.{}.0.0/16", i);
    auto community = fmt::format("30{:02d}:1", i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("30.{}.0.0", i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }

  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Down → unblock + up → enters DETACHED_INIT_DUMP */
  bringDownPeer(kPeerAddr3);
  unblockPeer(kPeerAddr3);
  testOnlyDeferInitDump(kPeerAddr3, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr3);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_INIT_DUMP));
  testOnlyDeferInitDump(kPeerAddr3, false, fboss::kDefaultVrf);

  /* Simulate route refresh while peer3 is in DID */
  for (int i = 21; i <= 23; ++i) {
    auto prefix = fmt::format("30.{}.0.0/16", i);
    auto community = fmt::format("30{:02d}:1", i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    /* Only peer4 receives — peer3 is in DID */
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("30.{}.0.0", i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }

  /* Peer3 stays detached (DID or DB if init dump overflowed queue) */
  auto finalState3 = getPeerState(kPeerAddr3);
  EXPECT_TRUE(
      finalState3 == PeerUpdateState::DETACHED_INIT_DUMP ||
      finalState3 == PeerUpdateState::DETACHED_BLOCKED);
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  XLOG(INFO, "=== TEST PASSED: DetachedInitDump_RouteRefresh ===");
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
