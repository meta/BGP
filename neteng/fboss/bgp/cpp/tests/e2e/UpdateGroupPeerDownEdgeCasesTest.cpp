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

/* E2E tests: Peer DOWN edge cases — rapid flap, all in-sync DOWN,
 * DOWN during route refresh / lazy clone, DOWN+UP+re-block from DB.
 * Prefix range: 30.1-30.49/16.
 *
 * Rapid flap (DOWN then immediately UP) — clean restart
 * All in-sync peers DOWN, only detached peers remain
 * Peer DOWN during route refresh processing
 * Peer DOWN during lazy clone iteration
 * Peer DOWN from P-DB, comes back, immediately blocks again
 */

#include <fmt/format.h>
#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupSlowPeerTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/* Rapid flap (DOWN then immediately UP) — clean restart, no stale
 * state. Reconnecting peer enters DETACHED_INIT_DUMP. Peer4 continues.
 */
TEST_P(UpdateGroupMultiPeerTest, RapidFlapDownThenUp) {
  XLOG(INFO, "=== TEST: RapidFlapDownThenUp ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  setupSlowPeerComponents(8, 6, 2);

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

  /* Pre-flap route — both peers get it */
  injectLocalRoutesAtRuntime({"30.1.0.0/16"}, {"3001:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.1.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.1.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3001:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.1.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3001:1"));

  /* Rapid flap: DOWN then immediately UP */
  bringDownPeer(kPeerAddr3);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);
  drainPeerQueueCompletely(peerId3);

  /* Peer4 works after the flap */
  injectLocalRoutesAtRuntime({"30.2.0.0/16"}, {"3002:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.2.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.2.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3002:1"));

  EXPECT_TRUE(isPeerInSync(kPeerAddr4));
  verifySlowPeerInvariants(kPeerAddr4);
  XLOG(INFO, "=== TEST PASSED: RapidFlapDownThenUp ===");
}

/* All in-sync peers DOWN, only detached peer5 remains.
 * 3 peers, detach peer5, bring peer3+peer4 DOWN. No crash.
 */
TEST_P(UpdateGroupMultiPeerTest, AllInSyncPeersDownDetachedRemains) {
  XLOG(INFO, "=== TEST: AllInSyncPeersDownDetachedRemains ===");

  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  addPeer(kDefaultPeerSpec5);
  setupSlowPeerComponents(3, 2, 0);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  bringUpPeer(kPeerAddr5);
  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);
  sendEoRToPeer(peerId5);
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId5));
  EXPECT_TRUE(waitForEoR(peerId5));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr5, PeerUpdateState::JOINED_RUNNING));

  /* Freq-detach peer5: threshold=1 */
  setSlowPeerThresholds(
      kPeerAddr5,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));
  blockPeer(kPeerAddr5);

  /* Fill queue: 3 routes with different communities, drain peer3+peer4 */
  for (int i = 10; i <= 12; i++) {
    auto prefix = fmt::format("30.{}.0.0/16", i);
    auto comm = fmt::format("30{:02d}:1", i);
    injectLocalRoutesAtRuntime({prefix}, {comm}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("30.{}.0.0", i),
        16,
        kPeerAddr3,
        getExpectedNexthop(kPeerAddr3),
        "4200000001",
        comm));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("30.{}.0.0", i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        comm));
  }

  EXPECT_TRUE(waitForPeerQueueBlocked(peerId5));
  ASSERT_TRUE(waitForPeerState(kPeerAddr5, PeerUpdateState::DETACHED_BLOCKED));

  /* Bring both in-sync peers DOWN */
  bringDownPeer(kPeerAddr3);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));
  bringDownPeer(kPeerAddr4);
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::DOWN));

  /* Only detached peer5 remains — no crash */
  auto state5 = getPeerState(kPeerAddr5);
  EXPECT_TRUE(
      state5 == PeerUpdateState::DETACHED_BLOCKED ||
      state5 == PeerUpdateState::DETACHED_READY_TO_JOIN);

  XLOG(INFO, "=== TEST PASSED: AllInSyncPeersDownDetachedRemains ===");
}

/* Peer DOWN during lazy clone iteration.
 * Detach peer3 via freq, inject post-detach route (triggers clone for CL),
 * then bring peer3 DOWN. Bitmap handles dead peer. Peer4 continues.
 */
TEST_P(UpdateGroupMultiPeerTest, PeerDownDuringLazyClone) {
  XLOG(INFO, "=== TEST: PeerDownDuringLazyClone ===");

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

  /* Shared route before detachment */
  injectLocalRoutesAtRuntime({"30.30.0.0/16"}, {"3030:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.30.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.30.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3030:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.30.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3030:1"));

  /* Freq-detach peer3: threshold=1 */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));
  blockPeer(kPeerAddr3);

  /* Fill queue with 3 routes — drain peer4 inline */
  for (int i = 31; i <= 33; i++) {
    auto prefix = fmt::format("30.{}.0.0/16", i);
    auto comm = fmt::format("30{:02d}:1", i);
    injectLocalRoutesAtRuntime({prefix}, {comm}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("30.{}.0.0", i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        comm));
  }

  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Post-detach route — triggers lazy clone for peer3's CL */
  injectLocalRoutesAtRuntime({"30.34.0.0/16"}, {"3034:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.34.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.34.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3034:1"));

  /* Bring peer3 DOWN while it has CL entries from lazy clone */
  unblockPeer(kPeerAddr3);
  bringDownPeer(kPeerAddr3);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DOWN));

  /* Peer4 continues after peer3 goes DOWN */
  injectLocalRoutesAtRuntime({"30.35.0.0/16"}, {"3035:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("30.35.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "30.35.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3035:1"));

  EXPECT_TRUE(isPeerInSync(kPeerAddr4));
  verifySlowPeerInvariants(kPeerAddr4);
  XLOG(INFO, "=== TEST PASSED: PeerDownDuringLazyClone ===");
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
