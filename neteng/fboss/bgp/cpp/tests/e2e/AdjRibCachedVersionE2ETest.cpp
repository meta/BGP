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
 * E2E tests for AdjRib cachedRibVersion feature
 *
 * Tests verify:
 * 1. Different peers are at different versions due to backpressure
 * 2. Blocked/slow peers catch up when unblocked
 * 3. Version tracking works correctly with change list tracker and egress
 *    backpressure enabled
 *
 * Note: Update groups are disabled since slow peer handling in update groups
 * is not implemented yet.
 */

#include <gtest/gtest.h>

#include <chrono>

#include <folly/logging/xlog.h>
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibGroup.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/E2ETestFixture.h"

using namespace facebook::bgp;
using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

/*
 * v4-only peer specs for simpler EoR handling (single EoR per peer)
 */
inline const BgpPeerSpec kDefaultPeerSpec5_v4only = {
    .asn = kPeerAsn5,
    .localAddr = kLocalAddr5,
    .peerAddr = kPeerAddr5,
    .v4Nexthop = kNextHopV4_5,
    .v6Nexthop = kEmptyV6Nexthop,
    .disableIpv6Afi = true,
};

class AdjRibCachedVersionE2ETest : public E2ETestFixture {
 protected:
  void setupComponents() {
    addPeer(kDefaultPeerSpec3_v4only); /* Route source */
    addPeer(kDefaultPeerSpec4_v4only); /* Receiver 1 */
    addPeer(kDefaultPeerSpec5_v4only); /* Receiver 2 */

    createRib();

    /*
     * Small queue to trigger backpressure easily
     * capacity=2, highWm=1, lowWm=0 means:
     * - Queue blocks after 1 message (at high watermark)
     * - Queue can hold up to 2 messages total
     */
    setDefaultQueueSizes(2 /* capacity */, 1 /* highWm */, 0 /* lowWm */);

    createPeerManager(
        false /* enableUpdateGroup - disabled per user request */);
  }

  /*
   * Helper to get the current RIB version.
   * Runs in RIB's event base thread to avoid TSAN data race.
   */
  uint64_t getRibVersion() {
    uint64_t version = 0;
    auto* vrf = peerManager_->findVrf(fboss::kDefaultVrf);
    if (!vrf) {
      XLOGF(ERR, "Non-existent VRF {}", fboss::kDefaultVrf);
      return 0;
    }
    peerManager_->getEventBase().runInEventBaseThreadAndWait(
        [&]() { version = vrf->getMaxRibVersion(); });
    return version;
  }

  /*
   * Helper to read the PeerManager's max seen RibVersion.
   * Runs in the PeerManager event base thread to avoid a TSAN data race.
   */
  uint64_t getMaxRibVersion() {
    uint64_t version = 0;
    auto* vrf = peerManager_->findVrf(fboss::kDefaultVrf);
    if (!vrf) {
      XLOGF(ERR, "Non-existent VRF {}", fboss::kDefaultVrf);
      return 0;
    }
    peerManager_->getEventBase().runInEventBaseThreadAndWait(
        [&]() { version = vrf->getMaxRibVersion(); });
    return version;
  }

  /*
   * Wait for a peer's cached version to reach at least the target version.
   */
  bool waitForPeerCachedVersion(
      const folly::IPAddress& peerAddr,
      uint64_t targetVersion,
      int maxRetries = 50) {
    bool result = false;
    facebook::bgp::test::checkWithRetry(
        [&]() {
          result = getPeerCachedRibVersion(peerAddr) >= targetVersion;
          return result;
        },
        maxRetries,
        std::chrono::milliseconds(100));
    return result;
  }
};

/*
 * Test: Cached RIB version starts at 0 for new peer.
 *
 * Steps:
 * 1. Bring up a peer
 * 2. Verify cachedRibVersion is 0 before any routes processed
 */
TEST_F(AdjRibCachedVersionE2ETest, CachedVersionStartsAtZero) {
  setupComponents();

  bringUpPeer(kPeerAddr4);

  /* Initial cached version should be 0 */
  uint64_t cachedVersion = getPeerCachedRibVersion(kPeerAddr4);
  EXPECT_EQ(cachedVersion, 0);
  XLOGF(INFO, "Peer4 initial cachedRibVersion: {}", cachedVersion);
}

/*
 * Test: Cached RIB version increments after consuming route update.
 *
 * Steps:
 * 1. Bring up peers
 * 2. Consume EoR before testing
 * 3. Inject route from peer3
 * 4. Verify peer4's cachedRibVersion increases after consuming update
 */
TEST_F(AdjRibCachedVersionE2ETest, CachedVersionIncrementsOnRouteConsumption) {
  setupComponents();

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  /* Consume EoR first (v4-only peer gets single EoR) */
  EXPECT_TRUE(waitForEoR(peerId4));

  uint64_t initialCachedVersion = getPeerCachedRibVersion(kPeerAddr4);

  /* Inject route from peer3 */
  addRoute("v4", "10.0.1.0", 24, kPeerAddr3, kNextHopV4_3.str());

  /* Verify peer4 receives the route */
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr4, kNextHopV4_4.str()));

  /* Verify cachedRibVersion increased */
  uint64_t newCachedVersion = getPeerCachedRibVersion(kPeerAddr4);
  EXPECT_GT(newCachedVersion, initialCachedVersion);
  XLOGF(
      INFO,
      "Peer4 cachedRibVersion: {} -> {}",
      initialCachedVersion,
      newCachedVersion);
}

/*
 * Test: Blocked peer falls behind in cachedRibVersion.
 *
 * With change list tracker enabled, cachedRibVersion is updated when AdjRib
 * processes entries from the change list. When a peer's egress queue fills up,
 * the send coroutine suspends and cancels the change list consume timer. This
 * prevents the peer from processing further changes until backpressure clears.
 *
 * This test uses local routes with different communities to force separate
 * UPDATE messages. The test does NOT consume updates from peer4, so its queue
 * fills up naturally causing backpressure. Meanwhile peer5's queue is drained
 * by verifying its routes.
 *
 * Steps:
 * 1. Bring up peers, consume EoR from peer5 only
 * 2. Inject route 1 - verify peer5 receives it (drains peer5's queue)
 * 3. Inject routes 2, 3, 4 with different communities (3 separate UPDATEs)
 * 4. Verify peer5 receives all (draining its queue)
 * 5. peer4's queue fills up with UPDATEs (we never consume from it)
 * 6. Wait for peer4's queue to block
 * 7. Verify peer4.version < peer5.version
 */
TEST_F(AdjRibCachedVersionE2ETest, BlockedPeerFallsBehind) {
  /*
   * Use a TINY queue (2, 1, 0) so it blocks quickly.
   * With v4-only peers: 1 initial route + 1 EoR = 2 messages.
   * If we don't consume from peer4, it blocks after 1 message (highWm).
   *
   * The key insight: when AdjRib is blocked on waitForQueueSpace(),
   * the MRAI timer is cancelled, so it stops consuming from the changeList.
   * The cachedRibVersion reflects what was consumed BEFORE blocking.
   */
  setDefaultQueueSizes(2, 1, 0);
  addPeer(kDefaultPeerSpec4_v4only);
  addPeer(kDefaultPeerSpec5_v4only);

  /* Pre-add a local route that will be in shadowRIB at startup */
  addLocalRoute("70.0.0.0/8", {"700:1"}, 100);

  createRib();

  createPeerManager(false /* enableUpdateGroup */);

  bringUpPeer(kPeerAddr4);
  bringUpPeer(kPeerAddr5);

  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  sendEoRToPeer(peerId4);
  sendEoRToPeer(peerId5);

  /* Verify route reached shadowRIB */
  auto routePrefix = folly::IPAddress::createNetwork("70.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(routePrefix));

  /*
   * ONLY consume from peer5, NOT from peer4.
   * peer4's queue will fill up with initial route + EoR (2 messages = capacity)
   */
  EXPECT_TRUE(
      verifyRouteAdd("v4", "70.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));
  EXPECT_TRUE(waitForEoR(peerId5));

  /* Wait for peer4's queue to block from initial dump */
  XLOGF(INFO, "Waiting for peer4's queue to block from initial dump");
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId4));
  XLOGF(INFO, "peer4's queue is now blocked");

  /* Record peer4's version while blocked */
  uint64_t peer4BlockedVersion = getPeerCachedRibVersion(kPeerAddr4);
  uint64_t peer5CurrentVersion = getPeerCachedRibVersion(kPeerAddr5);
  XLOGF(
      INFO,
      "After initial dump: peer4 blocked at version {}, peer5 at version {}",
      peer4BlockedVersion,
      peer5CurrentVersion);

  /*
   * Now inject more routes. Since peer4's queue is already blocked,
   * it cannot consume these new routes. peer5 will consume them.
   */
  XLOGF(INFO, "Injecting route 71.0.0.0/8");
  injectLocalRoutesAtRuntime({"71.0.0.0/8"}, {"710:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("71.0.0.0/8")));
  EXPECT_TRUE(
      verifyRouteAdd("v4", "71.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));

  XLOGF(INFO, "Injecting route 72.0.0.0/8");
  injectLocalRoutesAtRuntime({"72.0.0.0/8"}, {"720:2"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("72.0.0.0/8")));
  EXPECT_TRUE(
      verifyRouteAdd("v4", "72.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));

  /* Get final cached versions */
  uint64_t peer4FinalVersion = getPeerCachedRibVersion(kPeerAddr4);
  uint64_t peer5FinalVersion = getPeerCachedRibVersion(kPeerAddr5);

  XLOGF(
      INFO,
      "Final: peer4 cachedRibVersion: {}, peer5: {}",
      peer4FinalVersion,
      peer5FinalVersion);

  /*
   * peer4 was blocked during initial dump, so it couldn't consume the
   * runtime routes. peer5 consumed all routes. peer4's version should
   * be frozen at or near the initial dump level, peer5's should be higher.
   */
  EXPECT_LT(peer4FinalVersion, peer5FinalVersion);
}

/*
 * Test: Slow peer catches up when unblocked.
 *
 * When a peer is blocked and falls behind, consuming messages from its queue
 * allows:
 * 1. The egress queue to drain (messages sent to peer)
 * 2. The send coroutine to complete and reschedule the change list timer
 * 3. The peer to resume consuming from the change list and catch up
 *
 * Steps:
 * 1. Create blocked peer4 scenario where it falls behind peer5
 * 2. Record peer4's version while blocked (should be < peer5's)
 * 3. Drain peer4's queue by consuming its routes
 * 4. Wait for peer4 to catch up
 * 5. Verify peer4's version >= peer5's version
 */
TEST_F(AdjRibCachedVersionE2ETest, SlowPeerCatchesUpWhenUnblocked) {
  /*
   * Use a TINY queue (2, 1, 0) so it blocks quickly during initial dump.
   */
  setDefaultQueueSizes(2, 1, 0);
  addPeer(kDefaultPeerSpec4_v4only);
  addPeer(kDefaultPeerSpec5_v4only);

  /* Pre-add a local route that will be in shadowRIB at startup */
  addLocalRoute("80.0.0.0/8", {"800:1"}, 100);

  createRib();

  createPeerManager(false /* enableUpdateGroup */);

  bringUpPeer(kPeerAddr4);
  bringUpPeer(kPeerAddr5);

  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  sendEoRToPeer(peerId4);
  sendEoRToPeer(peerId5);

  auto routePrefix = folly::IPAddress::createNetwork("80.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(routePrefix));

  /*
   * ONLY consume from peer5, NOT from peer4.
   * peer4's queue will fill up with initial route + EoR.
   */
  EXPECT_TRUE(
      verifyRouteAdd("v4", "80.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));
  EXPECT_TRUE(waitForEoR(peerId5));

  /* Wait for peer4's queue to block */
  XLOGF(INFO, "Waiting for peer4's queue to block from initial dump");
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId4));

  /*
   * Inject more routes while peer4 is blocked.
   * peer4 cannot consume them, peer5 will consume them.
   */
  injectLocalRoutesAtRuntime({"81.0.0.0/8"}, {"810:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("81.0.0.0/8")));
  EXPECT_TRUE(
      verifyRouteAdd("v4", "81.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));

  injectLocalRoutesAtRuntime({"82.0.0.0/8"}, {"820:2"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("82.0.0.0/8")));
  EXPECT_TRUE(
      verifyRouteAdd("v4", "82.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));

  /* Record versions while peer4 is blocked */
  uint64_t peer4BeforeUnblock = getPeerCachedRibVersion(kPeerAddr4);
  uint64_t peer5BeforeUnblock = getPeerCachedRibVersion(kPeerAddr5);

  XLOGF(
      INFO,
      "Before unblock - peer4: {}, peer5: {}",
      peer4BeforeUnblock,
      peer5BeforeUnblock);

  /* peer4 should be behind */
  EXPECT_LT(peer4BeforeUnblock, peer5BeforeUnblock);

  /*
   * Now drain peer4's queue completely by consuming all its updates.
   * This allows the send coroutine to complete and reschedule timers.
   */
  XLOGF(INFO, "Draining peer4's queue");
  size_t peer4Drained = drainPeerQueueCompletely(peerId4, 10, 10);
  XLOGF(INFO, "Drained {} messages from peer4's queue", peer4Drained);

  /* Wait for peer4 to catch up to peer5's version */
  EXPECT_TRUE(waitForPeerCachedVersion(kPeerAddr4, peer5BeforeUnblock));

  uint64_t peer4AfterUnblock = getPeerCachedRibVersion(kPeerAddr4);
  XLOGF(
      INFO,
      "After unblock - peer4 caught up: {} (was {})",
      peer4AfterUnblock,
      peer4BeforeUnblock);

  EXPECT_GE(peer4AfterUnblock, peer5BeforeUnblock);
}

/*
 * Test: Cached version tracks RIB version on consumption.
 *
 * Steps:
 * 1. Inject route, get RIB version V1
 * 2. Verify peer's cachedRibVersion == V1 after consuming
 * 3. Inject another route, get RIB version V2
 * 4. Verify peer's cachedRibVersion == V2 after consuming
 */
TEST_F(AdjRibCachedVersionE2ETest, VersionTracksRibVersionOnConsumption) {
  setupComponents();

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  /* Consume EoR first */
  EXPECT_TRUE(waitForEoR(peerId4));

  /* Inject first route */
  addRoute("v4", "10.0.1.0", 24, kPeerAddr3, kNextHopV4_3.str());

  /* Verify peer4 receives the route */
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr4, kNextHopV4_4.str()));

  uint64_t ribVersionV1 = getRibVersion();
  uint64_t peer4CachedV1 = getPeerCachedRibVersion(kPeerAddr4);

  XLOGF(
      INFO,
      "After first route: RIB version = {}, peer4 cached = {}",
      ribVersionV1,
      peer4CachedV1);

  /* Peer's cached version should match RIB version after consuming */
  EXPECT_EQ(peer4CachedV1, ribVersionV1);

  /* Inject second route */
  addRoute("v4", "10.0.2.0", 24, kPeerAddr3, kNextHopV4_3.str());

  /* Verify peer4 receives the route */
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.2.0", 24, kPeerAddr4, kNextHopV4_4.str()));

  uint64_t ribVersionV2 = getRibVersion();
  uint64_t peer4CachedV2 = getPeerCachedRibVersion(kPeerAddr4);

  XLOGF(
      INFO,
      "After second route: RIB version = {}, peer4 cached = {}",
      ribVersionV2,
      peer4CachedV2);

  EXPECT_GT(ribVersionV2, ribVersionV1);
  EXPECT_EQ(peer4CachedV2, ribVersionV2);
}

/*
 * Test: Multiple peers at different versions due to different consumption.
 *
 * This test verifies that peers who consumed different amounts of updates
 * end up at different cachedRibVersion values.
 *
 * Steps:
 * 1. Bring up peers, consume EoR from peer5 only
 * 2. Wait for peer4's queue to block from initial dump
 * 3. Inject more routes - peer4 cannot consume, peer5 can
 * 4. Verify peer5.version > peer4.version (peer5 consumed more)
 */
TEST_F(AdjRibCachedVersionE2ETest, MultiplePeersAtDifferentVersions) {
  /*
   * Use a TINY queue (2, 1, 0) so it blocks quickly during initial dump.
   */
  setDefaultQueueSizes(2, 1, 0);
  addPeer(kDefaultPeerSpec4_v4only);
  addPeer(kDefaultPeerSpec5_v4only);

  /* Pre-add a local route that will be in shadowRIB at startup */
  addLocalRoute("60.0.0.0/8", {"600:1"}, 100);

  createRib();

  createPeerManager(false /* enableUpdateGroup */);

  bringUpPeer(kPeerAddr4);
  bringUpPeer(kPeerAddr5);

  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  sendEoRToPeer(peerId4);
  sendEoRToPeer(peerId5);

  auto routePrefix = folly::IPAddress::createNetwork("60.0.0.0/8");
  ASSERT_TRUE(waitForRouteInShadowRib(routePrefix));

  /*
   * ONLY consume from peer5, NOT from peer4.
   * peer4's queue will fill up with initial route + EoR.
   */
  EXPECT_TRUE(
      verifyRouteAdd("v4", "60.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));
  EXPECT_TRUE(waitForEoR(peerId5));

  /* Wait for peer4's queue to block from initial dump */
  XLOGF(INFO, "Waiting for peer4's queue to block from initial dump");
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId4));

  /*
   * Now inject routes while peer4 is blocked.
   * peer4 cannot consume them, peer5 will consume them.
   */
  injectLocalRoutesAtRuntime({"61.0.0.0/8"}, {"610:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("61.0.0.0/8")));
  EXPECT_TRUE(
      verifyRouteAdd("v4", "61.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));

  injectLocalRoutesAtRuntime({"62.0.0.0/8"}, {"620:2"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("62.0.0.0/8")));
  EXPECT_TRUE(
      verifyRouteAdd("v4", "62.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));

  injectLocalRoutesAtRuntime({"63.0.0.0/8"}, {"630:3"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("63.0.0.0/8")));
  EXPECT_TRUE(
      verifyRouteAdd("v4", "63.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));

  injectLocalRoutesAtRuntime({"64.0.0.0/8"}, {"640:4"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("64.0.0.0/8")));
  EXPECT_TRUE(
      verifyRouteAdd("v4", "64.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));

  /* Get cached versions - peer5 should be ahead of peer4 */
  uint64_t peer4CachedVersion = getPeerCachedRibVersion(kPeerAddr4);
  uint64_t peer5CachedVersion = getPeerCachedRibVersion(kPeerAddr5);

  XLOGF(
      INFO,
      "Peer4 cachedRibVersion: {}, Peer5 cachedRibVersion: {}",
      peer4CachedVersion,
      peer5CachedVersion);

  /* peer5 consumed all routes, peer4's queue is blocked */
  EXPECT_GT(peer5CachedVersion, peer4CachedVersion);
}

/*
 * Test: After rib dump, peer's cached version matches RIB version.
 *
 * Verifies that processRibDumpReq propagates ribVersion from the ShadowRib
 * entries to the peer's AdjRib (via setLastSeenRibVersion).
 *
 * Steps:
 * 1. Bring up both peers, complete initialization (no 45s EoR timer wait)
 * 2. Inject routes via peer3, verify RIB version
 * 3. Verify peer4 receives routes via change list (normal flow)
 * 4. Tear down peer4
 * 5. Bring up peer4 again — arrives after init, triggers rib dump
 * 6. Assert peer4's cachedRibVersion == RIB version
 */
TEST_F(AdjRibCachedVersionE2ETest, RibDumpSetsCorrectCachedVersion) {
  addPeer(kDefaultPeerSpec3_v4only);
  addPeer(kDefaultPeerSpec4_v4only);

  createRib(false /* enableNexthopTracking */);
  setDefaultQueueSizes(100 /* capacity */, 80 /* highWm */, 20 /* lowWm */);
  createPeerManager(false /* enableUpdateGroup */);

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  /* Consume EoR from peer4 to complete initialization */
  EXPECT_TRUE(waitForEoR(peerId4));

  /* Inject routes from peer3 */
  addRoute("v4", "10.0.1.0", 24, kPeerAddr3, kNextHopV4_3.str());
  addRoute("v4", "10.0.2.0", 24, kPeerAddr3, kNextHopV4_3.str());
  addRoute("v4", "10.0.3.0", 24, kPeerAddr3, kNextHopV4_3.str());

  /*
   * Routes with the same attributes are batched into a single UPDATE.
   * Verify one route arrives; the rest are in the same message.
   */
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr4, kNextHopV4_4.str()));

  const uint64_t ribVersion = getRibVersion();
  EXPECT_GT(ribVersion, 0);

  /* Tear down peer4 and bring it back up — triggers rib dump */
  bringDownPeer(kPeerAddr4);
  bringUpPeer(kPeerAddr4);

  /*
   * Wait for peer4's cached version to reach the RIB version.
   * This confirms the rib dump completed and ribVersion was propagated
   * from ShadowRibEntry to the peer's AdjRib.
   */
  EXPECT_TRUE(waitForPeerCachedVersion(kPeerAddr4, ribVersion));
  EXPECT_EQ(getPeerCachedRibVersion(kPeerAddr4), ribVersion);
}

/*
 * Test: lastSeenRibVersion is reset to 0 when the peer session is terminated,
 * with update group DISABLED.
 *
 * Steps:
 * 1. Bring up source peer3 and receiver peer4 (update group off)
 * 2. Inject a route from peer3, verify peer4 receives it
 * 3. Verify peer4's cachedRibVersion is non-zero
 * 4. Terminate peer4's session (AdjRib retained, peerDelete=false)
 * 5. Verify peer4's cachedRibVersion is reset to 0
 */
TEST_F(AdjRibCachedVersionE2ETest, VersionClearedOnTerminateNoUpdateGroup) {
  addPeer(kDefaultPeerSpec3_v4only); /* Route source */
  addPeer(kDefaultPeerSpec4_v4only); /* Receiver */

  createRib();
  createPeerManager(false /* enableUpdateGroup */);

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);
  EXPECT_TRUE(waitForEoR(peerId4));

  /* Inject a route from peer3 and verify peer4 receives it */
  addRoute("v4", "10.0.1.0", 24, kPeerAddr3, kNextHopV4_3.str());
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr4, kNextHopV4_4.str()));

  /* peer4 consumed an update, so its cached version must be non-zero */
  EXPECT_TRUE(waitForPeerCachedVersion(kPeerAddr4, 1));
  EXPECT_GT(getPeerCachedRibVersion(kPeerAddr4), 0);

  /* Terminate peer4's session; AdjRib is retained (peerDelete=false) */
  bringDownPeer(kPeerAddr4);

  /* lastSeenRibVersion must be reset to 0 on session termination */
  EXPECT_EQ(getPeerCachedRibVersion(kPeerAddr4), 0);
}

/*
 * Test: lastSeenRibVersion is reset to 0 when the peer session is terminated,
 * with update group ENABLED.
 *
 * getLastSeenRibVersion() returns the group's cached version while a peer is
 * in-sync with its update group. On termination, unregisterPeer() detaches the
 * peer (clears its group bit position) before clearLastSeenRibVersion() zeroes
 * the per-peer value, so the getter falls through to the cleared per-peer
 * value.
 *
 * Steps:
 * 1. Bring up source peer3 and receivers peer4/peer5 (update group on)
 * 2. Inject a route from peer3, verify both receivers get it via the group
 * 3. Verify both receivers' cachedRibVersion is non-zero
 * 4. Terminate peer4's session (AdjRib retained, peerDelete=false)
 * 5. Verify peer4's cachedRibVersion is reset to 0
 * 6. Verify peer5 (still up) retains its non-zero version
 */
TEST_F(AdjRibCachedVersionE2ETest, VersionClearedOnTerminateWithUpdateGroup) {
  addPeer(kDefaultPeerSpec3_v4only); /* Route source */
  addPeer(kDefaultPeerSpec4_v4only); /* Receiver 1 */
  addPeer(kDefaultPeerSpec5_v4only); /* Receiver 2 */

  createRib();
  createPeerManager(true /* enableUpdateGroup */);

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  bringUpPeer(kPeerAddr5);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);
  sendEoRToPeer(peerId5);
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId5));

  /* Inject a route from peer3 and verify both receivers get it via the group */
  addRoute("v4", "10.0.1.0", 24, kPeerAddr3, kNextHopV4_3.str());
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr4, kNextHopV4_4.str()));
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr5, kNextHopV4_5.str()));

  /* Both receivers consumed the group update; versions must be non-zero */
  EXPECT_TRUE(waitForPeerCachedVersion(kPeerAddr4, 1));
  EXPECT_TRUE(waitForPeerCachedVersion(kPeerAddr5, 1));
  EXPECT_GT(getPeerCachedRibVersion(kPeerAddr4), 0);
  EXPECT_GT(getPeerCachedRibVersion(kPeerAddr5), 0);

  /* Terminate peer4's session; unregisters it from the update group */
  bringDownPeer(kPeerAddr4);

  /*
   * peer4's cached version must be reset to 0 even though it was part of an
   * update group: after unregisterPeer, getLastSeenRibVersion() returns the
   * per-peer value (cleared to 0), not the group's version.
   */
  EXPECT_EQ(getPeerCachedRibVersion(kPeerAddr4), 0);

  /* peer5 is still up and must retain its non-zero version */
  EXPECT_GT(getPeerCachedRibVersion(kPeerAddr5), 0);
}

/*
 * Test: the update group's lastSeenRibVersion is reset to 0 when the group is
 * destroyed (its last member's session is terminated).
 *
 * UpdateGroupManager::maybeDestroyUpdateGroups() calls the group's
 * clearLastSeenRibVersion() right before erasing an empty group. We keep the
 * group alive via a shared_ptr so its version is observable after teardown.
 *
 * Steps:
 * 1. Bring up source peer3 and receivers peer4/peer5 (update group on)
 * 2. Inject a route so the group's lastSeenRibVersion advances past 0
 * 3. Capture peer4's update group (held alive by a shared_ptr)
 * 4. Verify the group's lastSeenRibVersion is non-zero
 * 5. Terminate every member's session so the group is destroyed
 * 6. Verify the group's lastSeenRibVersion is reset to 0
 */
TEST_F(AdjRibCachedVersionE2ETest, GroupVersionClearedOnGroupDestroy) {
  addPeer(kDefaultPeerSpec3_v4only); /* Route source */
  addPeer(kDefaultPeerSpec4_v4only); /* Receiver 1 */
  addPeer(kDefaultPeerSpec5_v4only); /* Receiver 2 */

  createRib();
  createPeerManager(true /* enableUpdateGroup */);

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  bringUpPeer(kPeerAddr5);

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);
  sendEoRToPeer(peerId5);
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId5));

  /* Inject a route so the group's lastSeenRibVersion advances past 0 */
  addRoute("v4", "10.0.1.0", 24, kPeerAddr3, kNextHopV4_3.str());
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr4, kNextHopV4_4.str()));
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr5, kNextHopV4_5.str()));
  EXPECT_TRUE(waitForPeerCachedVersion(kPeerAddr4, 1));

  /*
   * Capture the receiver's update group. Holding this shared_ptr keeps the
   * group object alive after it is erased on destruction, so we can read its
   * version. Access AdjRib/group state on the PeerManager EVB to avoid a
   * TSAN data race.
   */
  auto adjRib = getAdjRibByAddr(kPeerAddr4);
  ASSERT_NE(adjRib, nullptr);

  std::shared_ptr<AdjRibOutGroup> group;
  uint64_t groupId = 0;
  peerManager_->getEventBase().runInEventBaseThreadAndWait([&]() {
    group = adjRib->getUpdateGroup();
    if (group) {
      groupId = group->getGroupId();
    }
  });
  ASSERT_NE(group, nullptr);

  auto groupVersion = [&]() {
    uint64_t version = 0;
    peerManager_->getEventBase().runInEventBaseThreadAndWait(
        [&]() { version = group->getLastSeenRibVersion(); });
    return version;
  };

  /*
   * getUpdateGroupInfo() returns live groups from the UpdateGroupManager;
   * filtering by this group's id yields an entry only while it exists.
   */
  auto groupExistsInManager = [&]() {
    return !peerManager_->getUpdateGroupInfo(static_cast<int64_t>(groupId))
                .empty();
  };

  /* Before teardown: group is live in the manager with a non-zero version */
  EXPECT_TRUE(groupExistsInManager());
  EXPECT_GT(groupVersion(), 0);

  /* Terminate every member so the group's member count drops to 0 */
  bringDownPeer(kPeerAddr3);
  bringDownPeer(kPeerAddr4);
  bringDownPeer(kPeerAddr5);

  /*
   * The group is removed from the UpdateGroupManager on destruction, and its
   * cached version is reset to 0 by clearLastSeenRibVersion() beforehand.
   */
  EXPECT_FALSE(groupExistsInManager());
  EXPECT_EQ(groupVersion(), 0);
}

/*
 * Test: PeerManager's max seen RibVersion starts at 0 before any RIB updates.
 */
TEST_F(AdjRibCachedVersionE2ETest, MaxRibVersionStartsAtZero) {
  setupComponents();

  bringUpPeer(kPeerAddr4);

  EXPECT_EQ(getMaxRibVersion(), 0);
}

/*
 * Test: PeerManager's max seen RibVersion advances as RibOutAnnouncement
 * entries are processed, tracking the RIB version.
 *
 * Steps:
 * 1. Bring up source peer3 and receiver peer4, consume EoR
 * 2. Inject a route from peer3, verify peer4 receives it
 * 3. Verify maxRibVersion advanced and equals the RIB version
 * 4. Inject a second route and verify it advances again, still == RIB version
 */
TEST_F(AdjRibCachedVersionE2ETest, MaxRibVersionAdvancesOnAnnouncement) {
  setupComponents();

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);

  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  EXPECT_TRUE(waitForEoR(peerId4));

  /* Inject first route */
  addRoute("v4", "10.0.1.0", 24, kPeerAddr3, kNextHopV4_3.str());
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr4, kNextHopV4_4.str()));

  uint64_t maxRibV1 = getMaxRibVersion();
  EXPECT_GT(maxRibV1, 0);
  EXPECT_EQ(maxRibV1, getRibVersion());

  /* Inject second route */
  addRoute("v4", "10.0.2.0", 24, kPeerAddr3, kNextHopV4_3.str());
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.2.0", 24, kPeerAddr4, kNextHopV4_4.str()));

  uint64_t maxRibV2 = getMaxRibVersion();
  EXPECT_GT(maxRibV2, maxRibV1);
  EXPECT_EQ(maxRibV2, getRibVersion());
}

/*
 * Test: a peer whose initial dump runs against a completely empty shadow RIB
 * (all routes advertised then withdrawn) still ends up at the PeerManager's max
 * seen RIB version -- it must not fall back to 0.
 *
 * This is the key property: a version derived by walking shadowRibEntries_
 * would be lost once every route is withdrawn, but maxRibVersion_ is
 * updated from each RibOutWithdrawal entry, so it still reflects the latest RIB
 * version even when the shadow RIB is empty -- and a full-walk dump advances a
 * fresh peer to it.
 *
 * Steps:
 * 1. Bring up source peer3 and early receiver peer4.
 * 2. Advertise routes from peer3; peer4 consumes them.
 * 3. Withdraw all routes; peer4 consumes the withdrawals so the shadow RIB
 *    entries are erased. shadowRibEntries_ is now empty but maxRibVersion_
 *    is non-zero (it survives an emptied RIB).
 * 4. Bring up peer5 late: its initial dump walks the empty shadow RIB.
 * 5. Verify peer5's AdjRib cached version == PeerManager max seen RIB version.
 */
TEST_F(AdjRibCachedVersionE2ETest, MaxRibVersionSurvivesEmptyShadowRib) {
  addPeer(kDefaultPeerSpec3_v4only); /* Route source */
  addPeer(kDefaultPeerSpec4_v4only); /* Early receiver */
  addPeer(kDefaultPeerSpec5_v4only); /* Late receiver: dumps empty shadow RIB */

  createRib(false /* enableNexthopTracking */);
  setDefaultQueueSizes(100 /* capacity */, 80 /* highWm */, 20 /* lowWm */);
  createPeerManager(false /* enableUpdateGroup */);

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);

  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  EXPECT_TRUE(waitForEoR(peerId4));

  /* Advertise two routes from peer3; confirm peer4 receives them. */
  addRoute("v4", "10.0.1.0", 24, kPeerAddr3, kNextHopV4_3.str());
  addRoute("v4", "10.0.2.0", 24, kPeerAddr3, kNextHopV4_3.str());
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("10.0.1.0/24")));
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("10.0.2.0/24")));

  uint64_t maxRibAfterAdvertise = getMaxRibVersion();
  EXPECT_GT(maxRibAfterAdvertise, 0);
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr4, kNextHopV4_4.str()));

  /*
   * Withdraw all routes. deleteRoute is asynchronous (RIB path selection +
   * batch), so gate on peer4 actually receiving the withdrawal before checking
   * the shadow RIB -- that confirms it propagated through the RIB and was
   * consumed off the change list, which erases the shadow RIB entries. The two
   * same-attribute withdrawals batch into a single UPDATE.
   */
  deleteRoute("v4", "10.0.1.0", 24, kPeerAddr3);
  deleteRoute("v4", "10.0.2.0", 24, kPeerAddr3);
  EXPECT_TRUE(verifyRouteWithdraw("v4", "10.0.1.0", 24, kPeerAddr4));
  EXPECT_TRUE(verifyRouteNotInShadowRib(
      folly::IPAddress::createNetwork("10.0.1.0/24"), 20 /* waitRetries */));
  EXPECT_TRUE(verifyRouteNotInShadowRib(
      folly::IPAddress::createNetwork("10.0.2.0/24"), 20 /* waitRetries */));

  /* The shadow RIB is now empty, but the max RIB version is non-zero. */
  EXPECT_EQ(getShadowRibEntriesCount(), 0);

  uint64_t maxRibVersion = getMaxRibVersion();
  EXPECT_GT(maxRibVersion, 0);
  /* The withdrawals advanced the version past the advertise point. */
  EXPECT_GT(maxRibVersion, maxRibAfterAdvertise);
  /* maxRibVersion_ tracks the RIB-provided version of the last update. */
  EXPECT_EQ(maxRibVersion, getRibVersion());

  /*
   * Bring up peer5 now: its initial dump walks the empty shadow RIB. With
   * nothing skipped onto its change list, the full walk must advance its AdjRib
   * cached version to maxRibVersion_ rather than leaving it at 0.
   */
  bringUpPeer(kPeerAddr5);
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};
  EXPECT_TRUE(waitForEoR(peerId5));

  EXPECT_TRUE(waitForPeerCachedVersion(kPeerAddr5, maxRibVersion));
  EXPECT_EQ(getPeerCachedRibVersion(kPeerAddr5), maxRibVersion);
}

/*
 * Test: a re-dump for a peer suspended on the change list must NOT advance its
 * cached version. When entries are pending on the peer's change list they are
 * skipped by processRibDumpReq, so the peer's version is left for change-list
 * consumption to advance -- it is not jumped ahead to maxRibVersion_.
 *
 * Steps:
 * 1. Bring up receivers peer4 (tiny queue) and peer5; converge on a route.
 * 2. Consume from peer5 only so peer4's queue blocks (peer4 is suspended).
 * 3. Inject more routes; they pile on peer4's change list while peer5 consumes,
 *    so maxRibVersion advances past peer4's cached version.
 * 4. Re-dump peer4 while it is suspended (entries on its change list).
 * 5. Verify peer4's cached version is unchanged and still below
 *    maxRibVersion.
 */
TEST_F(
    AdjRibCachedVersionE2ETest,
    RedumpWithSuspendedChangeListDoesNotChangeVersion) {
  /* Tiny queue so peer4 blocks quickly from the initial dump. */
  setDefaultQueueSizes(2, 1, 0);
  addPeer(kDefaultPeerSpec4_v4only); /* Receiver that gets suspended */
  addPeer(kDefaultPeerSpec5_v4only); /* Receiver that keeps consuming */

  /* Pre-add a local route that will be in shadowRIB at startup */
  addLocalRoute("90.0.0.0/8", {"900:1"}, 100);

  createRib();
  createPeerManager(false /* enableUpdateGroup */);

  bringUpPeer(kPeerAddr4);
  bringUpPeer(kPeerAddr5);

  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  sendEoRToPeer(peerId4);
  sendEoRToPeer(peerId5);

  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("90.0.0.0/8")));

  /* Consume from peer5 only; peer4's queue fills and blocks (suspended). */
  EXPECT_TRUE(
      verifyRouteAdd("v4", "90.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));
  EXPECT_TRUE(waitForEoR(peerId5));
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId4));

  uint64_t peer4VersionBefore = getPeerCachedRibVersion(kPeerAddr4);
  /*
   * peer4 consumed the single startup route (90.0.0.0/8) before its queue
   * blocked, so it sits at whatever version that prefix chunk got.
   */
  EXPECT_GT(peer4VersionBefore, 0);

  /*
   * Inject more routes while peer4 is suspended. peer4 cannot consume them, so
   * they stay on its change list; peer5 consumes them, advancing
   * maxRibVersion past peer4's cached version.
   */
  injectLocalRoutesAtRuntime({"91.0.0.0/8"}, {"910:1"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("91.0.0.0/8")));
  EXPECT_TRUE(
      verifyRouteAdd("v4", "91.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));
  injectLocalRoutesAtRuntime({"92.0.0.0/8"}, {"920:2"}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork("92.0.0.0/8")));
  EXPECT_TRUE(
      verifyRouteAdd("v4", "92.0.0.0", 8, kPeerAddr5, kNextHopV4_5.str()));

  const uint64_t maxRibVersion = getMaxRibVersion();
  /* Routes 91 and 92 advanced PeerManager's max past where peer4 stalled. */
  EXPECT_LT(peer4VersionBefore, maxRibVersion);

  /*
   * Re-dump peer4 while entries are suspended on its change list (simulating an
   * egress-policy re-evaluation). The skipped entries mean processRibDumpReq
   * must NOT advance peer4's version to maxRibVersion_.
   */
  triggerRibDumpForPeer(kPeerAddr4);

  uint64_t peer4VersionAfter = getPeerCachedRibVersion(kPeerAddr4);
  /*
   * The suspended change-list entries (91/92) were skipped, so the re-dump must
   * NOT advance peer4 to the current max RIB version -- those changes reach
   * peer4 only via change-list consumption once it is unblocked.
   *
   * The dump does re-send the entries that were not skipped, and peer4 records
   * their versions, so its cached version may move: a prefix announced as a
   * bestpath and then as an add-path arrives in two RibOutAnnouncement buffers
   * and so occupies two versions, while a non-add-path peer only ever consumed
   * the first. What must not happen is peer4 claiming the current max while
   * changes are still pending for it.
   */
  EXPECT_LT(peer4VersionAfter, maxRibVersion);
}

} // namespace bgp
} // namespace facebook
