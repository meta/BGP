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
 * E2E tests for RIB Versioning feature
 *
 * Tests verify:
 * 1. RIB version increments on best path changes
 * 2. RIB version increments on each new route
 * 3. RIB version increments on route withdrawals
 * 4. Version increments on best path changes due to better route
 * 5. Version does not change on duplicate route (no-op)
 * 6. Version increments on multipath/ECMP changes
 * 7. Version increments on nexthop changes (same peer, different nexthop)
 * 8. Update groups track RIB version when consuming changes
 */

#include <gtest/gtest.h>

#include <folly/logging/xlog.h>
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/E2ETestFixture.h"

using namespace facebook::bgp;
using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

class RibVersioningE2ETest : public E2ETestFixture {
 protected:
  void setupComponents() {
    addPeer(kDefaultPeerSpec3);
    addPeer(kDefaultPeerSpec4);
    addPeer(kDefaultPeerSpec5);

    createRib();
    createPeerManager(true /* enableUpdateGroup */);
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
};

/*
 * Test: RIB version starts at 0 and increments on best path change.
 *
 * Steps:
 * 1. Verify initial RIB version is 0
 * 2. Inject a route from peer3
 * 3. Verify RIB version incremented
 */
TEST_F(RibVersioningE2ETest, VersionIncrementsOnBestPathChange) {
  setupComponents();

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr5);

  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

  /* Initial RIB version should be 0 */
  uint64_t initialVersion = getRibVersion();
  EXPECT_EQ(initialVersion, 0);

  /* Inject a route from peer3 */
  addRoute(
      "v4" /* protocol */,
      "10.0.1.0" /* prefix */,
      24 /* prefixLen */,
      kPeerAddr3 /* peer */,
      kNextHopV4_3.str() /* nexthop */);

  /* Verify peer5 receives the announcement */
  EXPECT_TRUE(verifyRouteAdd(
      "v4",
      "10.0.1.0",
      24,
      kPeerAddr5,
      kNextHopV4_5.str() /* expectedNexthop */));

  /* RIB version should have incremented */
  uint64_t newVersion = getRibVersion();
  EXPECT_GT(newVersion, initialVersion);
  XLOGF(
      INFO,
      "RIB version incremented from {} to {}",
      initialVersion,
      newVersion);
}

/*
 * Test: RIB version increments on each best path change.
 *
 * Steps:
 * 1. Inject route A from peer3
 * 2. Record version V1
 * 3. Inject route B from peer3
 * 4. Record version V2
 * 5. Verify V2 > V1
 */
TEST_F(RibVersioningE2ETest, VersionIncrementsOnMultipleChanges) {
  setupComponents();

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr5);

  /* Inject first route */
  addRoute("v4", "10.0.1.0", 24, kPeerAddr3, kNextHopV4_3.str());

  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr5, kNextHopV4_5.str()));

  uint64_t versionAfterFirst = getRibVersion();
  EXPECT_GT(versionAfterFirst, 0);

  /* Inject second route */
  addRoute("v4", "10.0.2.0", 24, kPeerAddr3, kNextHopV4_3.str());

  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.2.0", 24, kPeerAddr5, kNextHopV4_5.str()));

  uint64_t versionAfterSecond = getRibVersion();
  EXPECT_GT(versionAfterSecond, versionAfterFirst);
  XLOGF(
      INFO,
      "RIB version after first route: {}, after second: {}",
      versionAfterFirst,
      versionAfterSecond);
}

/*
 * Test: Version increments on withdrawal (best path change).
 *
 * Steps:
 * 1. Inject route from peer3
 * 2. Record version V1
 * 3. Withdraw the route
 * 4. Record version V2
 * 5. Verify V2 > V1
 */
TEST_F(RibVersioningE2ETest, VersionIncrementsOnWithdrawal) {
  setupComponents();

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr5);

  /* Inject route */
  addRoute("v4", "10.0.1.0", 24, kPeerAddr3, kNextHopV4_3.str());

  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr5, kNextHopV4_5.str()));

  uint64_t versionAfterAdd = getRibVersion();

  /* Withdraw the route */
  deleteRoute("v4", "10.0.1.0", 24, kPeerAddr3);

  EXPECT_TRUE(verifyRouteWithdraw("v4", "10.0.1.0", 24, kPeerAddr5));

  uint64_t versionAfterWithdraw = getRibVersion();
  EXPECT_GT(versionAfterWithdraw, versionAfterAdd);
  XLOGF(
      INFO,
      "RIB version after add: {}, after withdraw: {}",
      versionAfterAdd,
      versionAfterWithdraw);
}

/*
 * Test: Version increments on best path change due to better route.
 *
 * Steps:
 * 1. Inject route from peer3 with lower local-pref
 * 2. Record version V1
 * 3. Inject same prefix from peer4 with higher local-pref
 * 4. Record version V2 (best path changed)
 * 5. Verify V2 > V1
 */
TEST_F(RibVersioningE2ETest, VersionIncrementsOnBetterRoute) {
  setupComponents();

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  bringUpPeer(kPeerAddr5);

  /* Inject route from peer3 with local-pref 100 */
  addRoute(
      "v4",
      "10.0.1.0",
      24,
      kPeerAddr3,
      kNextHopV4_3.str(),
      "" /* asPath */,
      "" /* community */,
      0 /* addPathId */,
      100 /* localPref */);

  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr5, kNextHopV4_5.str()));

  uint64_t versionAfterFirst = getRibVersion();

  /* Inject same prefix from peer4 with higher local-pref 200 */
  addRoute(
      "v4",
      "10.0.1.0",
      24,
      kPeerAddr4,
      kNextHopV4_4.str(),
      "" /* asPath */,
      "" /* community */,
      0 /* addPathId */,
      200 /* localPref */);

  /*
   * Best path changes from peer3 to peer4 due to higher local-pref.
   * Wait for the RIB version to increment rather than waiting for an update,
   * since the update to peer5 might have the same nexthop-self value.
   */
  uint64_t versionAfterBetterRoute = 0;
  facebook::bgp::test::checkWithRetry(
      [&]() {
        versionAfterBetterRoute = getRibVersion();
        return versionAfterBetterRoute > versionAfterFirst;
      },
      10 /* retries */,
      std::chrono::milliseconds(500));

  EXPECT_GT(versionAfterBetterRoute, versionAfterFirst);
  XLOGF(
      INFO,
      "RIB version after first route: {}, after better route: {}",
      versionAfterFirst,
      versionAfterBetterRoute);
}

/*
 * Test: Version does not change when no material change occurs.
 *
 * Steps:
 * 1. Inject route from peer3
 * 2. Record version V1
 * 3. Re-inject same route from peer3 (same attributes)
 * 4. Record version V2
 * 5. Verify V2 == V1 (no change)
 */
TEST_F(RibVersioningE2ETest, VersionDoesNotChangeOnDuplicateRoute) {
  setupComponents();

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr5);

  /* Inject route */
  addRoute("v4", "10.0.1.0", 24, kPeerAddr3, kNextHopV4_3.str());

  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr5, kNextHopV4_5.str()));

  uint64_t versionAfterFirst = getRibVersion();

  /* Re-inject the same route (should be no-op) */
  addRoute("v4", "10.0.1.0", 24, kPeerAddr3, kNextHopV4_3.str());

  /*
   * We don't expect a new update to peer5 since nothing changed.
   * Just verify version didn't change.
   */
  uint64_t versionAfterDuplicate = getRibVersion();
  EXPECT_EQ(versionAfterDuplicate, versionAfterFirst);
  XLOGF(
      INFO,
      "RIB version after duplicate route: {} (unchanged from {})",
      versionAfterDuplicate,
      versionAfterFirst);
}

/*
 * Test: Version increments on multipath change (ECMP scenario).
 *
 * When multiple peers advertise the same prefix with equal attributes,
 * the RIB creates ECMP (multipath). Adding a second path changes the
 * multipath set, which should trigger a version increment.
 *
 * Steps:
 * 1. Inject route from peer3 (becomes bestpath, multipath=[peer3])
 * 2. Record version V1
 * 3. Inject same prefix from peer4 with same local-pref (ECMP, multipath
 * changes)
 * 4. Record version V2
 * 5. Verify V2 > V1 (multipath change triggers version increment)
 */
TEST_F(RibVersioningE2ETest, VersionIncrementsOnMultipathChange) {
  setupComponents();

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  bringUpPeer(kPeerAddr5);

  /* Inject route from peer3 - becomes bestpath */
  addRoute(
      "v4",
      "10.0.1.0",
      24,
      kPeerAddr3,
      kNextHopV4_3.str(),
      "" /* asPath */,
      "" /* community */,
      0 /* addPathId */,
      100 /* localPref */);

  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr5, kNextHopV4_5.str()));

  uint64_t versionAfterFirst = getRibVersion();
  EXPECT_GT(versionAfterFirst, 0);

  /*
   * Inject same prefix from peer4 with same local-pref.
   * This creates an ECMP scenario - multipath changes even if bestpath
   * stays the same (peer3 wins tie-breaker).
   */
  addRoute(
      "v4",
      "10.0.1.0",
      24,
      kPeerAddr4,
      kNextHopV4_4.str(),
      "" /* asPath */,
      "" /* community */,
      0 /* addPathId */,
      100 /* localPref */);

  /*
   * Wait for version to increment. Multipath changed (added peer4 to ECMP)
   * even if bestpath is still peer3.
   */
  uint64_t versionAfterMultipath = 0;
  facebook::bgp::test::checkWithRetry(
      [&]() {
        versionAfterMultipath = getRibVersion();
        return versionAfterMultipath > versionAfterFirst;
      },
      10 /* retries */,
      std::chrono::milliseconds(500));

  EXPECT_GT(versionAfterMultipath, versionAfterFirst);
  XLOGF(
      INFO,
      "RIB version after multipath change: {} (was {})",
      versionAfterMultipath,
      versionAfterFirst);
}

/*
 * Test: Version increments on nexthop change (same peer, different nexthop).
 *
 * This test verifies that when a peer re-announces the same prefix with a
 * different nexthop, the RIB version increments even though bestpath doesn't
 * change (same peer is still best).
 *
 * Steps:
 * 1. Inject route from peer3 with nexthop A
 * 2. Record version V1
 * 3. Re-inject same prefix from peer3 with nexthop B
 * 4. Record version V2
 * 5. Verify V2 > V1 (nexthop change triggers version increment)
 */
TEST_F(RibVersioningE2ETest, VersionIncrementsOnNexthopChange) {
  setupComponents();

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr5);

  /* Inject route from peer3 with first nexthop */
  addRoute("v4", "10.0.1.0", 24, kPeerAddr3, kNextHopV4_3.str());

  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr5, kNextHopV4_5.str()));

  uint64_t versionAfterFirst = getRibVersion();
  EXPECT_GT(versionAfterFirst, 0);

  /*
   * Re-inject same prefix from same peer (peer3) but with a different nexthop.
   * Use kNextHopV4_4 as the new nexthop to simulate a nexthop change.
   * The bestpath doesn't change (still peer3) but nexthop changes.
   */
  addRoute("v4", "10.0.1.0", 24, kPeerAddr3, kNextHopV4_4.str());

  /*
   * Wait for version to increment. The nexthop changed even though bestpath
   * (peer3) is still the same.
   */
  uint64_t versionAfterNexthopChange = 0;
  facebook::bgp::test::checkWithRetry(
      [&]() {
        versionAfterNexthopChange = getRibVersion();
        return versionAfterNexthopChange > versionAfterFirst;
      },
      10 /* retries */,
      std::chrono::milliseconds(500));

  EXPECT_GT(versionAfterNexthopChange, versionAfterFirst);
  XLOGF(
      INFO,
      "RIB version after nexthop change: {} (was {})",
      versionAfterNexthopChange,
      versionAfterFirst);
}

/*
 * Test: Update group tracks RIB version after consuming changes.
 *
 * This test verifies that when an update group consumes route changes,
 * the group's cachedRibVersion is updated. Multiple peers in the same
 * group share the same version since they consume updates as a unit.
 *
 * Steps:
 * 1. Create peers that will be in the same update group
 * 2. Inject a route - peers receive it via the group
 * 3. Inject another route
 * 4. Verify the RIB version increased after each change
 */
TEST_F(RibVersioningE2ETest, UpdateGroupTracksRibVersion) {
  setupComponents();

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  bringUpPeer(kPeerAddr5);

  /* Initial RIB version should be 0 */
  uint64_t initialVersion = getRibVersion();
  EXPECT_EQ(initialVersion, 0);

  /* Inject first route */
  addRoute("v4", "10.0.1.0", 24, kPeerAddr3, kNextHopV4_3.str());

  /* Verify peer4 and peer5 receive the announcement via update group */
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr4, kNextHopV4_4.str()));
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr5, kNextHopV4_5.str()));

  uint64_t versionAfterFirst = getRibVersion();
  EXPECT_GT(versionAfterFirst, 0);

  /* Inject second route */
  addRoute("v4", "10.0.2.0", 24, kPeerAddr3, kNextHopV4_3.str());

  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.2.0", 24, kPeerAddr4, kNextHopV4_4.str()));
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.2.0", 24, kPeerAddr5, kNextHopV4_5.str()));

  uint64_t versionAfterSecond = getRibVersion();
  EXPECT_GT(versionAfterSecond, versionAfterFirst);

  /* Verify per-peer cached version is non-zero (consumed from RIB) */
  EXPECT_GT(getPeerCachedRibVersion(kPeerAddr4), 0);
  EXPECT_GT(getPeerCachedRibVersion(kPeerAddr5), 0);

  XLOGF(
      INFO,
      "Update group test: RIB version {} -> {} after consuming changes",
      versionAfterFirst,
      versionAfterSecond);
}

/*
 * Test (scenario 4, end-to-end): ECMP shrink -- the consuming peer catches up
 * and the version stays monotonic.
 *
 * A prefix with two ECMP paths (peer3, peer4) has one path (peer3) withdrawn.
 * The shrink makes the RIB emit, for that single prefix, an add-path withdrawal
 * AND a re-announcement in one pass, sharing one RIB version. This verifies
 * end-to-end that the receiving peer materializes the shrink and its cached
 * lastSeenRibVersion advances to the current RIB version (never regresses) --
 * i.e. the same-version withdraw+re-announce is consumed correctly.
 */
TEST_F(RibVersioningE2ETest, VersionAndPeerCatchUpOnEcmpShrink) {
  setupComponents();
  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  bringUpPeer(kPeerAddr5);

  /* Two equal-cost paths -> ECMP with two nexthops for the prefix. */
  addRoute(
      "v4", "10.0.1.0", 24, kPeerAddr3, kNextHopV4_3.str(), "", "", 0, 100);
  addRoute(
      "v4", "10.0.1.0", 24, kPeerAddr4, kNextHopV4_4.str(), "", "", 0, 100);
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr5, kNextHopV4_5.str()));
  ASSERT_TRUE(waitForMultipathNexthopCount("10.0.1.0/24", 2));

  /* Peer catches up to the RIB version before the shrink. */
  uint64_t versionBeforeShrink = 0;
  facebook::bgp::test::checkWithRetry(
      [&]() {
        versionBeforeShrink = getRibVersion();
        return versionBeforeShrink > 0 &&
            getPeerCachedRibVersion(kPeerAddr5) == versionBeforeShrink;
      },
      10 /* retries */,
      std::chrono::milliseconds(500));
  ASSERT_GT(versionBeforeShrink, 0);

  /* Shrink the ECMP set: withdraw the peer3 path, leaving a single path. */
  deleteRoute("v4", "10.0.1.0", 24, kPeerAddr3);
  ASSERT_TRUE(waitForMultipathNexthopCount("10.0.1.0/24", 1));

  /*
   * The RIB version advances once for the shrink pass and the peer catches back
   * up to it: the add-path withdrawal and the re-announcement (same version)
   * are both materialized, and the cached version never moves backward.
   */
  uint64_t versionAfterShrink = 0;
  facebook::bgp::test::checkWithRetry(
      [&]() {
        versionAfterShrink = getRibVersion();
        return versionAfterShrink > versionBeforeShrink &&
            getPeerCachedRibVersion(kPeerAddr5) == versionAfterShrink;
      },
      10 /* retries */,
      std::chrono::milliseconds(500));

  EXPECT_GT(versionAfterShrink, versionBeforeShrink);
  EXPECT_EQ(getPeerCachedRibVersion(kPeerAddr5), versionAfterShrink);
}

/*
 * Test (scenario 5, end-to-end): across a mix of announcement, multipath grow,
 * a second prefix, and a withdrawal, the peer's cached lastSeenRibVersion is
 * always monotonic (never decreases) and never exceeds the RIB version. This is
 * the end-to-end "reader always sees monotonically rising versions" guarantee.
 */
TEST_F(RibVersioningE2ETest, PeerCachedVersionMonotonicAcrossMixedUpdates) {
  setupComponents();
  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  bringUpPeer(kPeerAddr5);

  uint64_t lastPeerVersion = 0;
  auto observe = [&]() {
    const uint64_t ribV = getRibVersion();
    const uint64_t peerV = getPeerCachedRibVersion(kPeerAddr5);
    /* The peer is never ahead of the RIB, and never regresses. */
    EXPECT_LE(peerV, ribV);
    EXPECT_GE(peerV, lastPeerVersion);
    lastPeerVersion = peerV;
  };

  /* 1) Announce prefix A. */
  addRoute("v4", "10.0.1.0", 24, kPeerAddr3, kNextHopV4_3.str());
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.1.0", 24, kPeerAddr5, kNextHopV4_5.str()));
  observe();

  /* 2) Grow prefix A to two ECMP paths. */
  addRoute(
      "v4", "10.0.1.0", 24, kPeerAddr4, kNextHopV4_4.str(), "", "", 0, 100);
  ASSERT_TRUE(waitForMultipathNexthopCount("10.0.1.0/24", 2));
  observe();

  /* 3) Announce a second prefix B. */
  addRoute("v4", "10.0.2.0", 24, kPeerAddr3, kNextHopV4_3.str());
  EXPECT_TRUE(
      verifyRouteAdd("v4", "10.0.2.0", 24, kPeerAddr5, kNextHopV4_5.str()));
  observe();

  /* 4) Withdraw prefix B. */
  deleteRoute("v4", "10.0.2.0", 24, kPeerAddr3);
  EXPECT_TRUE(verifyRouteWithdraw("v4", "10.0.2.0", 24, kPeerAddr5));
  observe();

  EXPECT_GT(lastPeerVersion, 0);
}

} // namespace bgp
} // namespace facebook
