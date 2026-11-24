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
 * E2E tests for peer down from every PeerUpdateState.
 * Tests cleanup verification and resource leak checks.
 */

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupSlowPeerTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/*
 * Test: Peer down from JOINED_RUNNING state.
 * Verify clean state transition to DOWN, group continues functioning.
 */
TEST_P(UpdateGroupPeerDownTest, PeerDownFromJoinedRunning) {
  XLOG(INFO, "=== TEST: PeerDownFromJoinedRunning ===");

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

  /* Bring down peer3 from JOINED_RUNNING */
  bringDownPeer(kPeerAddr3);
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));

  /* Peer4 should still be functional */
  injectLocalRoutesAtRuntime({"110.0.0.0/8"}, {"1100:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("110.0.0.0/8")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "110.0.0.0",
      8,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1100:1"));

  XLOG(INFO, "=== TEST PASSED: PeerDownFromJoinedRunning ===");
}

/*
 * Test: Peer down from JOINED_BLOCKED state.
 * Verify blocked state is cleaned up properly on peer down.
 */
TEST_P(UpdateGroupPeerDownTest, PeerDownFromJoinedBlocked) {
  XLOG(INFO, "=== TEST: PeerDownFromJoinedBlocked ===");

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

  /* Set high threshold to avoid detachment */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(60000),
      100,
      std::chrono::milliseconds(60000));

  /* Block peer3 and fill queue */
  blockPeer(kPeerAddr3);
  injectLocalRoutesAtRuntime({"120.0.0.0/8"}, {"1200:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("120.0.0.0/8")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "120.0.0.0",
      8,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1200:1"));
  injectLocalRoutesAtRuntime({"121.0.0.0/8"}, {"1210:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("121.0.0.0/8")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "121.0.0.0",
      8,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1210:1"));
  injectLocalRoutesAtRuntime({"122.0.0.0/8"}, {"1220:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("122.0.0.0/8")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "122.0.0.0",
      8,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1220:1"));

  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_BLOCKED));

  /* Bring down peer3 while BLOCKED */
  bringDownPeer(kPeerAddr3);
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));

  /* Peer4 should still be functional */
  injectLocalRoutesAtRuntime({"123.0.0.0/8"}, {"1230:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("123.0.0.0/8")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "123.0.0.0",
      8,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1230:1"));

  XLOG(INFO, "=== TEST PASSED: PeerDownFromJoinedBlocked ===");
}

/*
 * Test: Peer down from DETACHED_BLOCKED state.
 * Detach peer3 via frequency, then bring it down.
 * Verify detached bits cleaned up, remaining peer functional.
 */
TEST_P(UpdateGroupPeerDownTest, PeerDownFromDetachedBlocked) {
  XLOG(INFO, "=== TEST: PeerDownFromDetachedBlocked ===");

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

  /* Trigger frequency-based detachment: threshold = 1 */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(60000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  injectLocalRoutesAtRuntime({"130.0.0.0/8"}, {"1300:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("130.0.0.0/8")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "130.0.0.0",
      8,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1300:1"));
  injectLocalRoutesAtRuntime({"131.0.0.0/8"}, {"1310:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("131.0.0.0/8")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "131.0.0.0",
      8,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1310:1"));
  injectLocalRoutesAtRuntime({"132.0.0.0/8"}, {"1320:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("132.0.0.0/8")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "132.0.0.0",
      8,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1320:1"));

  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));
  EXPECT_TRUE(isPeerDetached(kPeerAddr3));
  EXPECT_EQ(getDetachedPeerCount(kPeerAddr3), 1);

  /* Bring down peer3 from DETACHED_BLOCKED */
  bringDownPeer(kPeerAddr3);
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));

  /* Detached bits should be cleaned up */
  EXPECT_EQ(getDetachedPeerCount(kPeerAddr4), 0);

  /* Peer4 still functional */
  injectLocalRoutesAtRuntime({"133.0.0.0/8"}, {"1330:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("133.0.0.0/8")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "133.0.0.0",
      8,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1330:1"));

  verifySlowPeerInvariants(kPeerAddr4);

  XLOG(INFO, "=== TEST PASSED: PeerDownFromDetachedBlocked ===");
}

/*
 * TODO: DetachedPeerDownAndReconnect test
 * When a peer reconnects to an existing group, it enters
 * DETACHED_INIT_DUMP state. The individual initial dump is async
 * and may not complete deterministically in the current E2E framework.
 * See ~/save/e2e_bugs_found.txt for details.
 */

/*
 * Test: Both peers go down simultaneously.
 * Verify clean teardown with no crashes or leaks.
 */
TEST_P(UpdateGroupPeerDownTest, BothPeersDown) {
  XLOG(INFO, "=== TEST: BothPeersDown ===");

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

  /* Inject a route so both peers have state */
  injectLocalRoutesAtRuntime({"230.0.0.0/8"}, {"2300:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("230.0.0.0/8")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "230.0.0.0",
      8,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "2300:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "230.0.0.0",
      8,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "2300:1"));

  /* Bring both peers down */
  bringDownPeer(kPeerAddr3);
  bringDownPeer(kPeerAddr4);

  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));
  EXPECT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::DOWN));

  XLOG(INFO, "=== TEST PASSED: BothPeersDown ===");
}

/*
 * Test: Detached peer down, then remaining peer also goes down.
 * Tests the "last peer in group goes down" edge case.
 */
TEST_P(UpdateGroupPeerDownTest, DetachedThenLastPeerDown) {
  XLOG(INFO, "=== TEST: DetachedThenLastPeerDown ===");

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

  /* Detach peer3 */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(60000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  injectLocalRoutesAtRuntime({"231.0.0.0/8"}, {"2310:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("231.0.0.0/8")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "231.0.0.0",
      8,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "2310:1"));
  injectLocalRoutesAtRuntime({"232.0.0.0/8"}, {"2320:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("232.0.0.0/8")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "232.0.0.0",
      8,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "2320:1"));
  injectLocalRoutesAtRuntime({"233.0.0.0/8"}, {"2330:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("233.0.0.0/8")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "233.0.0.0",
      8,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "2330:1"));

  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Bring down detached peer3 */
  bringDownPeer(kPeerAddr3);
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));

  /* Now bring down the last remaining peer4 */
  bringDownPeer(kPeerAddr4);
  EXPECT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::DOWN));

  /* No crashes — clean shutdown verified by TearDown */
  XLOG(INFO, "=== TEST PASSED: DetachedThenLastPeerDown ===");
}

/*
 * Test: Peer goes down immediately after session up, before initial
 * dump completes (INIT state). Verify clean cleanup.
 */
TEST_P(UpdateGroupPeerDownTest, PeerDownDuringInitState) {
  XLOG(INFO, "=== TEST: PeerDownDuringInitState ===");

  addPeer(kDefaultPeerSpec3);
  addLocalRoute("234.0.0.0/8", {"2340:1"}, 100);

  setupSlowPeerComponents(3, 2, 0);

  auto routePrefix = folly::IPAddress::createNetwork("234.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(routePrefix));

  /* Bring up peer but don't send EoR or consume initial dump */
  bringUpPeer(kPeerAddr3);

  /* Immediately bring it down before initial dump/EoR cycle */
  bringDownPeer(kPeerAddr3);

  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));

  /* No crashes — clean shutdown */
  XLOG(INFO, "=== TEST PASSED: PeerDownDuringInitState ===");
}

/*
 * Peer DOWN from DETACHED_INIT_DUMP state.
 * Reach DID by: detach peer3 via freq threshold, bring DOWN,
 * unblock + bring UP (reconnects to existing group → enters DID),
 * then immediately bring DOWN again. Verify clean cleanup.
 */
TEST_P(UpdateGroupPeerDownTest, PeerDownFromDetachedInitDump) {
  XLOG(INFO, "=== TEST: PeerDownFromDetachedInitDump ===");

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

  /* Step 1: Detach peer3 via frequency threshold (count=1) */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(60000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  injectLocalRoutesAtRuntime({"16.1.0.0/16"}, {"1601:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("16.1.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "16.1.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1601:1"));
  injectLocalRoutesAtRuntime({"16.2.0.0/16"}, {"1602:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("16.2.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "16.2.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1602:1"));
  injectLocalRoutesAtRuntime({"16.3.0.0/16"}, {"1603:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("16.3.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "16.3.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1603:1"));

  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));
  EXPECT_TRUE(isPeerDetached(kPeerAddr3));

  /* Step 2: Bring peer3 DOWN from DETACHED_BLOCKED */
  bringDownPeer(kPeerAddr3);
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));

  /* Step 3: Unblock + bring peer3 back UP + EoR → enters DETACHED_INIT_DUMP */
  unblockPeer(kPeerAddr3);
  testOnlyDeferInitDump(kPeerAddr3, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_INIT_DUMP, 30));
  testOnlyDeferInitDump(kPeerAddr3, false, fboss::kDefaultVrf);

  /* Step 4: Immediately bring peer3 DOWN from DETACHED_INIT_DUMP */
  bringDownPeer(kPeerAddr3);
  EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));

  /* Detached count should be 0 after cleanup */
  EXPECT_EQ(getDetachedPeerCount(kPeerAddr4), 0);

  /* Peer4 should still be functional */
  injectLocalRoutesAtRuntime({"16.4.0.0/16"}, {"1604:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("16.4.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "16.4.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "1604:1"));

  verifySlowPeerInvariants(kPeerAddr4);

  XLOG(INFO, "=== TEST PASSED: PeerDownFromDetachedInitDump ===");
}

INSTANTIATE_TEST_SUITE_P(
    SerializationModes,
    UpdateGroupPeerDownTest,
    ::testing::Values(kNoSerialization, kWithSerialization),
    [](const ::testing::TestParamInfo<SerializationParams>& info) {
      return info.param.name;
    });

} // namespace bgp
} // namespace facebook
