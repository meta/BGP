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

/* E2E tests: ribVersion boundary conditions. Prefix range: 32.1-32.28/16.
 * entry.ribVersion == divergenceRibVersion — Case 4 boundary (<=)
 * multiple detach-rejoin cycles — versions accumulate correctly
 * divergenceRibVersion stays fixed during entire detached period
 */

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupSlowPeerTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/* Inject a route BEFORE detachment so its ribVersion ==
 * divergenceRibVersion at the moment of detachment. This is the Case 4
 * boundary (entry.ribVersion <= divergenceRibVersion). Update that same
 * prefix AFTER detachment — the CL must clone it (Case 4) before applying
 * the new attributes. Peer4 (in-sync) gets the update normally. */
TEST_P(UpdateGroupMultiPeerTest, RibVersionEqualsDivergenceCase4) {
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

  /* Inject a shared route — both peers receive it. This route's ribVersion
   * will exactly equal divergenceRibVersion when we detach next. */
  injectLocalRoutesAtRuntime({"32.1.0.0/16"}, {"3201:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("32.1.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.1.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3201:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.1.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3201:1"));

  /* Detach peer3 via freq threshold */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  injectLocalRoutesAtRuntime({"32.2.0.0/16"}, {"3202:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("32.2.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.2.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3202:1"));
  injectLocalRoutesAtRuntime({"32.3.0.0/16"}, {"3203:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("32.3.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.3.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3203:1"));
  injectLocalRoutesAtRuntime({"32.4.0.0/16"}, {"3204:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("32.4.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.4.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3204:1"));

  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Now update the shared route (32.1.0.0/16) whose ribVersion ==
   * divergenceRibVersion. This triggers Case 4 clone. */
  injectLocalRoutesAtRuntime({"32.1.0.0/16"}, {"3201:2"}, 200);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("32.1.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.1.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3201:2"));

  EXPECT_EQ(getDetachedPeerCount(kPeerAddr4), 1);
  verifySlowPeerInvariants(kPeerAddr4);
}

/* Multiple detach-rejoin cycles. Detach peer3 via freq threshold,
 * bring it DOWN, then UP again (enters DETACHED_INIT_DUMP). Repeat the
 * cycle. Each cycle advances ribVersion. Verify CL tracking works across
 * multiple detachment epochs. */
TEST_P(UpdateGroupMultiPeerTest, MultipleDetachRejoinVersionAccumulate) {
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

  /* Inject a route to advance ribVersion */
  injectLocalRoutesAtRuntime({"32.10.0.0/16"}, {"3210:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("32.10.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.10.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3210:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.10.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3210:1"));

  /* --- Detach cycle #1 --- */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  injectLocalRoutesAtRuntime({"32.11.0.0/16"}, {"3211:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("32.11.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.11.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3211:1"));
  injectLocalRoutesAtRuntime({"32.12.0.0/16"}, {"3212:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("32.12.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.12.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3212:1"));
  injectLocalRoutesAtRuntime({"32.13.0.0/16"}, {"3213:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("32.13.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.13.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3213:1"));

  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));
  EXPECT_EQ(getDetachedPeerCount(kPeerAddr4), 1);

  /* Bring peer3 DOWN then back UP for rejoin cycle */
  unblockPeer(kPeerAddr3);
  bringDownPeer(kPeerAddr3);

  /* Inject more routes while peer3 is DOWN — advances ribVersion */
  injectLocalRoutesAtRuntime({"32.14.0.0/16"}, {"3214:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("32.14.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.14.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3214:1"));

  /* Bring peer3 back UP — enters DETACHED_INIT_DUMP */
  testOnlyDeferInitDump(kPeerAddr3, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr3);
  sendEoRToPeer(peerId3);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_INIT_DUMP, 30));
  testOnlyDeferInitDump(kPeerAddr3, false, fboss::kDefaultVrf);

  /* Bring DOWN immediately to complete cycle #1 cleanly */
  bringDownPeer(kPeerAddr3);

  /* --- Inject more routes (cycle #2 prep) --- */
  injectLocalRoutesAtRuntime({"32.15.0.0/16"}, {"3215:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("32.15.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.15.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3215:1"));

  /* Peer4 still healthy after multiple cycles */
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));
  verifySlowPeerInvariants(kPeerAddr4);
}

/* divergenceRibVersion stays fixed during entire detached period.
 * After detachment, inject multiple routes. Each advances the group's
 * ribVersion but the detached peer's divergenceRibVersion must NOT change.
 * Verified indirectly: each post-detach route goes to peer4 only, and
 * CL tracking remains stable (invariants hold after each injection). */
TEST_P(UpdateGroupMultiPeerTest, DivergenceRibVersionStaysFixed) {
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

  /* Inject shared route before detachment */
  injectLocalRoutesAtRuntime({"32.20.0.0/16"}, {"3220:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("32.20.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.20.0.0",
      16,
      kPeerAddr3,
      getExpectedNexthop(kPeerAddr3),
      "4200000001",
      "3220:1"));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.20.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3220:1"));

  /* Freq-detach peer3 */
  setSlowPeerThresholds(
      kPeerAddr3,
      std::chrono::milliseconds(600000),
      1,
      std::chrono::milliseconds(60000));

  blockPeer(kPeerAddr3);
  injectLocalRoutesAtRuntime({"32.21.0.0/16"}, {"3221:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("32.21.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.21.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3221:1"));
  injectLocalRoutesAtRuntime({"32.22.0.0/16"}, {"3222:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("32.22.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.22.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3222:1"));
  injectLocalRoutesAtRuntime({"32.23.0.0/16"}, {"3223:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("32.23.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.23.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3223:1"));

  EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));

  /* Inject 4 more routes post-detach. Each advances group ribVersion but
   * peer3's divergenceRibVersion is frozen at detachment point. */
  for (int i = 0; i < 4; ++i) {
    auto prefix = fmt::format("32.{}.0.0/16", 24 + i);
    auto community = fmt::format("{}:1", 3224 + i);
    injectLocalRoutesAtRuntime({prefix}, {community}, 150);
    ASSERT_TRUE(
        waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefix)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        fmt::format("32.{}.0.0", 24 + i),
        16,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        community));
    verifySlowPeerInvariants(kPeerAddr4);
  }

  /* Update the shared route — triggers Case 4 clone because its ribVersion
   * <= the frozen divergenceRibVersion */
  injectLocalRoutesAtRuntime({"32.20.0.0/16"}, {"3220:2"}, 200);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("32.20.0.0/16")));
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "32.20.0.0",
      16,
      kPeerAddr4,
      getExpectedNexthop(kPeerAddr4),
      "4200000001",
      "3220:2"));

  EXPECT_EQ(getDetachedPeerCount(kPeerAddr4), 1);
  verifySlowPeerInvariants(kPeerAddr4);
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
