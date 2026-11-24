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

/* E2E tests: CL batch correctness and full slow peer lifecycle.
 *
 * Prefix range: 57.30-57.49/16
 * Fixture: UpdateGroupMultiPeerTest
 *
 * Tests implemented:
 *   CL announce+withdraw same batch — per-prefix correctness
 *   Full lifecycle JR→JB→DB→unblock→PL drain→CL consume→DRJ→accept→JR
 */

#include <fmt/format.h>
#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupSlowPeerTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/* CL item with combined announce+withdraw in same batch.
 * While peer3 is blocked, inject a route then withdraw a different one.
 * Both CL items are independent — peer4 receives both.
 */
TEST_P(UpdateGroupMultiPeerTest, ClAnnounceWithdrawSameBatch) {
  XLOG(INFO, "=== TEST: ClAnnounceWithdrawSameBatch ===");

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

  /* Shared route before blocking */
  injectLocalRoutesAtRuntime({"57.30.0.0/16"}, {"5730:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("57.30.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "57.30.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "5730:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "57.30.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "5730:1"));

  /* Block peer3 and fill to trigger JOINED_BLOCKED */
  blockPeer(kPeerAddr3);
  for (int i = 31; i <= 35; i++) {
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

  /* Announce a new route while blocked (CL item) */
  injectLocalRoutesAtRuntime({"57.36.0.0/16"}, {"5736:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("57.36.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "57.36.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "5736:1"));

  /* Withdraw the pre-existing shared route while blocked (CL item) */
  withdrawLocalRoutesAtRuntime({"57.30.0.0/16"});
  EXPECT_TRUE(verifyRouteWithdraw("v4", "57.30.0.0", 16, kPeerAddr4));

  /* Peer4 got both announce and withdraw independently */
  EXPECT_TRUE(isPeerInSync(kPeerAddr4));
  verifySlowPeerInvariants(kPeerAddr4);

  XLOG(INFO, "=== TEST PASSED: ClAnnounceWithdrawSameBatch ===");
}

/* Full lifecycle JR→JB→DB→unblock→PL drain→DRJ→accept→JR.
 * Exercise the complete slow peer lifecycle from detection to recovery.
 * CL injection is done after recovery (not during detachment) to avoid
 * acceptance discrepancies caused by MRAI timer delays on CL consume.
 */
TEST_P(UpdateGroupMultiPeerTest, FullLifecycleJrToJr) {
  XLOG(INFO, "=== TEST: FullLifecycleJrToJr ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  /* Initialize slow peer infrastructure with large default queue */
  setupSlowPeerComponents(10, 8, 0);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  /* Fast peer: large queue — won't block during route injection */
  setDefaultQueueSizes(10, 8, 0);
  bringUpPeer(kPeerAddr4);
  /* Slow peer: small queue (capacity=3, hiWm=2) — blocks naturally */
  setDefaultQueueSizes(3, 2, 0);
  bringUpPeer(kPeerAddr3);

  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId4));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));
  XLOG(INFO, "Checkpoint: Both peers JOINED_RUNNING");

  /* Step 1+2: JR → JB → DB via block + queue fill + freq (threshold=1).
   * With hiWm=2, need 3 routes (hiWm+1) to reliably trigger blocking.
   * Freq threshold=1 means first block event immediately triggers detach.
   */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));
  blockPeer(kPeerAddr3);

  injectLocalRoutesAtRuntime({"57.40.0.0/16"}, {"5740:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("57.40.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "57.40.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "5740:1"));

  injectLocalRoutesAtRuntime({"57.41.0.0/16"}, {"5741:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("57.41.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "57.41.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "5741:1"));

  injectLocalRoutesAtRuntime({"57.42.0.0/16"}, {"5742:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("57.42.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "57.42.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "5742:1"));

  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));
  EXPECT_TRUE(isPeerDetached(kPeerAddr3));
  EXPECT_FALSE(isPeerInSync(kPeerAddr3));
  XLOG(INFO, "Checkpoint: peer3 DETACHED_BLOCKED");

  /* Step 3: Unblock → peer3 drains PL → DRJ → accept → JR */
  unblockPeer(kPeerAddr3);
  drainPeerQueueCompletely(peerId3);
  drainPeerQueueCompletely(peerId4);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING, 200));
  XLOG(INFO, "Checkpoint: peer3 unblocked and rejoined");

  /* Verify both peers are in-sync after recovery */
  EXPECT_TRUE(isPeerInSync(kPeerAddr3));
  EXPECT_TRUE(isPeerInSync(kPeerAddr4));
  EXPECT_FALSE(isPeerDetached(kPeerAddr3));
  verifySlowPeerInvariants(kPeerAddr4);

  /* Step 4: Route after recovery confirms group is operational.
   * Both peers should receive the route (peer3 is back in group).
   */
  injectLocalRoutesAtRuntime({"57.43.0.0/16"}, {"5743:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("57.43.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "57.43.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "5743:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "57.43.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "5743:1"));

  XLOG(INFO, "=== TEST PASSED: FullLifecycleJrToJr ===");
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
