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

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupSlowPeerTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/*
 * Policy-like re-evaluation with all peers JOINED_RUNNING.
 * Simulate policy change by re-injecting a route with different community
 * (attribute change). Both in-sync peers should receive the updated route.
 * Real policy changes crash the fixture (CHECK failure in AdjRibCommon),
 * so we exercise the re-eval code path via attribute changes instead.
 */
TEST_P(UpdateGroupLifecycleTest, PolicyReEvalAllRunning) {
  XLOGF(INFO, "=== TEST: PolicyReEvalAllRunning ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);

  setupSlowPeerComponents(5, 4, 0);

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

  /* Inject initial route -- both peers receive it */
  injectLocalRoutesAtRuntime({"18.1.0.0/16"}, {"1801:1"}, 100);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("18.1.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "18.1.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "1801:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "18.1.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1801:1"));

  /*
   * Re-inject same prefix with different community + LP (simulates
   * policy re-evaluation). Both peers should receive the update since
   * both are JOINED_RUNNING -- cost is 1 ShadowRib walk.
   */
  injectLocalRoutesAtRuntime({"18.1.0.0/16"}, {"1801:99"}, 200);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("18.1.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "18.1.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "1801:99"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "18.1.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1801:99"));

  /* Verify both peers remain JOINED_RUNNING after re-eval */
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  EXPECT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));
  verifySlowPeerInvariants(kPeerAddr3);
  verifySlowPeerInvariants(kPeerAddr4);

  XLOGF(INFO, "=== TEST PASSED: PolicyReEvalAllRunning ===");
}
/*
 * Route changes while detached peer has accumulated CL entries.
 * Inject multiple routes while peer3 is detached (each goes to CL),
 * then do an attribute change on an earlier route. Verify peer4
 * receives all updates correctly -- the current batch completes before
 * the re-eval is processed.
 */
TEST_P(UpdateGroupLifecycleTest, RouteChangeDuringCLActivity) {
  XLOGF(INFO, "=== TEST: RouteChangeDuringCLActivity ===");

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

  /* Both peers receive initial route */
  injectLocalRoutesAtRuntime({"18.7.0.0/16"}, {"1807:1"}, 100);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("18.7.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "18.7.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "1807:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "18.7.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1807:1"));

  /* Detach peer3 */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  injectLocalRoutesAtRuntime({"18.8.0.0/16"}, {"1808:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("18.8.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "18.8.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1808:1"));
  injectLocalRoutesAtRuntime({"18.9.0.0/16"}, {"1809:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("18.9.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "18.9.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1809:1"));
  injectLocalRoutesAtRuntime({"18.10.0.0/16"}, {"1810:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("18.10.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "18.10.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1810:1"));

  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /*
   * CL now has entries for peer3. Inject more routes and do an
   * attribute change on the pre-detach route -- simulates re-eval
   * while CL is active. peer4 should receive everything in order.
   */
  injectLocalRoutesAtRuntime({"18.11.0.0/16"}, {"1811:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("18.11.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "18.11.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1811:1"));

  /* Attribute change on pre-detach route */
  injectLocalRoutesAtRuntime({"18.7.0.0/16"}, {"1807:99"}, 200);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("18.7.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "18.7.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1807:99"));

  verifySlowPeerInvariants(kPeerAddr4);
  XLOGF(INFO, "=== TEST PASSED: RouteChangeDuringCLActivity ===");
}
/*
 * Route change while peer is in DETACHED_INIT_DUMP.
 * Detach peer3 via freq threshold, bring DOWN, unblock, bring UP
 * (enters DETACHED_INIT_DUMP on reconnect to existing group).
 * Inject a route while peer3 is in init dump, then bring DOWN
 * again. Verify peer4 continues to function.
 */
TEST_P(UpdateGroupLifecycleTest, RouteChangeDuringInitDump) {
  XLOGF(INFO, "=== TEST: RouteChangeDuringInitDump ===");

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

  /* Detach peer3 via frequency threshold */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  injectLocalRoutesAtRuntime({"18.20.0.0/16"}, {"1820:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("18.20.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "18.20.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1820:1"));
  injectLocalRoutesAtRuntime({"18.21.0.0/16"}, {"1821:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("18.21.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "18.21.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1821:1"));
  injectLocalRoutesAtRuntime({"18.22.0.0/16"}, {"1822:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("18.22.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "18.22.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1822:1"));

  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Bring peer3 DOWN while DETACHED_BLOCKED */
  bringDownPeer(kPeerAddr3);
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));

  /* Unblock peer3 (clear blocked state from previous session) */
  unblockPeer(kPeerAddr3);

  /* Bring peer3 UP -- reconnects to existing group → DETACHED_INIT_DUMP */
  testOnlyDeferInitDump(kPeerAddr3, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_INIT_DUMP, 30));
  testOnlyDeferInitDump(kPeerAddr3, false, fboss::kDefaultVrf);

  /*
   * While peer3 is in DETACHED_INIT_DUMP, inject a route.
   * peer4 should receive it. peer3 should not (pendingEgressPolicyUpdate
   * flag or init dump in progress).
   */
  injectLocalRoutesAtRuntime({"18.23.0.0/16"}, {"1823:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("18.23.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "18.23.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1823:1"));

  /* Bring peer3 DOWN again to clean up */
  bringDownPeer(kPeerAddr3);
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));

  /* Verify peer4 still functional after all the lifecycle changes */
  injectLocalRoutesAtRuntime({"18.24.0.0/16"}, {"1824:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("18.24.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "18.24.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1824:1"));

  verifySlowPeerInvariants(kPeerAddr4);
  XLOGF(INFO, "=== TEST PASSED: RouteChangeDuringInitDump ===");
}

INSTANTIATE_TEST_SUITE_P(
    SerializationModes,
    UpdateGroupLifecycleTest,
    ::testing::Values(kNoSerialization, kWithSerialization),
    [](const ::testing::TestParamInfo<SerializationParams>& info) {
      return info.param.name;
    });

} // namespace bgp
} // namespace facebook
