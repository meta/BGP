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
 * E2E tests: Missing P-DID (Detached Init Dump) state x Event coverage
 *
 * Prefix range: 31.x.0.0/16
 *
 * Tests:
 *   P-DID x E-ROUTE-ADD -- Route added, CL item appended
 *   P-DID x E-BLOCK -- Peer blocks during detached init dump
 *   P-DID x E-SLOW-DUR -- N/A (already detached)
 *   P-DID x E-PEER-UP -- N/A (already up)
 *   P-DID x E-CL-END -- CL consumed to end during init dump
 */

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupSlowPeerTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {
/*
 * P-DID x E-BLOCK
 * Peer blocks during DETACHED_INIT_DUMP. The init dump uses a buffered
 * independent RibDumpReq that processes asynchronously. Block during DID
 * is either a no-op or transitions to DB non-deterministically. Accept
 * either DID or DB as valid states.
 */
TEST_P(UpdateGroupMultiPeerTest, DetachedInitDump_Block) {
  XLOG(INFO, "=== TEST: DetachedInitDump_Block ===");

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
    auto prefix = fmt::format("31.{}.0.0/16", 10 + i);
    auto community = fmt::format("{}:1", 3110 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("31.{}.0.0", 10 + i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* DOWN + unblock + UP -> DID */
  bringDownPeer(kPeerAddr3);
  unblockPeer(kPeerAddr3);
  testOnlyDeferInitDump(kPeerAddr3, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_INIT_DUMP, 30));
  testOnlyDeferInitDump(kPeerAddr3, false, fboss::kDefaultVrf);

  /* Block peer3 during DID -- accept DID or DB as valid */
  blockPeer(kPeerAddr3);
  ASSERT_TRUE(waitForPeerStateAny(
      kPeerAddr3,
      {PeerUpdateState::DETACHED_INIT_DUMP,
       PeerUpdateState::DETACHED_BLOCKED}));

  /* Peer4 still works */
  injectLocalRoutesAtRuntime({"31.15.0.0/16"}, {"3115:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.15.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.15.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3115:1"));
  EXPECT_TRUE(isPeerInSync(kPeerAddr4));

  XLOG(INFO, "=== TEST PASSED: DetachedInitDump_Block ===");
}

/*
 * P-DID x E-SLOW-DUR -- N/A (already detached)
 * Setting a duration threshold on an already-detached peer doesn't trigger
 * double-detachment. No crash, peer stays in DID.
 */
TEST_P(UpdateGroupMultiPeerTest, DetachedInitDump_SlowDur_NA) {
  XLOG(INFO, "=== TEST: DetachedInitDump_SlowDur_NA ===");

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
    auto prefix = fmt::format("31.{}.0.0/16", 20 + i);
    auto community = fmt::format("{}:1", 3120 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("31.{}.0.0", 20 + i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* DOWN + unblock + UP -> DID */
  bringDownPeer(kPeerAddr3);
  unblockPeer(kPeerAddr3);
  /* Larger queue for reconnection so init dump (3 routes + 2 EoRs) fits */
  setDefaultQueueSizes(10, 8, 0);
  testOnlyDeferInitDump(kPeerAddr3, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_INIT_DUMP, 30));
  testOnlyDeferInitDump(kPeerAddr3, false, fboss::kDefaultVrf);

  /* Set aggressive duration threshold on already-detached peer -- no-op */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(1),
      999999,
      std::chrono::milliseconds(60000));

  /* No double-detachment -- peer recovers normally to JOINED_RUNNING */
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));

  /* Peer4 still works fine */
  injectLocalRoutesAtRuntime({"31.25.0.0/16"}, {"3125:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.25.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.25.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3125:1"));

  XLOG(INFO, "=== TEST PASSED: DetachedInitDump_SlowDur_NA ===");
}

/*
 * P-DID x E-CL-END
 * CL consumed to end during init dump. Drain the init dump queue to
 * let PL drain and CL consume. Verify peer3 is not DOWN and peer4
 * continues working.
 */
TEST_P(UpdateGroupMultiPeerTest, DetachedInitDump_ClEnd) {
  XLOG(INFO, "=== TEST: DetachedInitDump_ClEnd ===");

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
    auto prefix = fmt::format("31.{}.0.0/16", 40 + i);
    auto community = fmt::format("{}:1", 3140 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("31.{}.0.0", 40 + i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* DOWN + unblock + UP -> DID */
  bringDownPeer(kPeerAddr3);
  unblockPeer(kPeerAddr3);
  testOnlyDeferInitDump(kPeerAddr3, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_INIT_DUMP, 30));
  testOnlyDeferInitDump(kPeerAddr3, false, fboss::kDefaultVrf);

  /* Inject a route during DID -- creates CL item */
  injectLocalRoutesAtRuntime({"31.45.0.0/16"}, {"3145:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.45.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.45.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3145:1"));

  /* Drain peer3's init dump queue to allow PL drain and CL consumption */
  drainPeerQueueCompletely(peerId3);

  /* After drain, peer3 may be DRJ or still processing. Verify not DOWN */
  auto state = getPeerState(kPeerAddr3);
  EXPECT_NE(state, PeerUpdateState::DOWN);

  /* Peer4 keeps working */
  injectLocalRoutesAtRuntime({"31.46.0.0/16"}, {"3146:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("31.46.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "31.46.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3146:1"));
  EXPECT_TRUE(isPeerInSync(kPeerAddr4));

  XLOG(INFO, "=== TEST PASSED: DetachedInitDump_ClEnd ===");
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
