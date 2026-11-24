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
 * E2E regression tests for "advance to maxRibVersion once the change list has
 * been consumed to the end", covering both halves:
 *
 *   GroupVersionAdvancesOnAddPathOnlyChange -- AdjRibOutGroup's change list
 *     consume timer.
 *   DetachedPeerRejoinsAfterAddPathOnlyChurn --
 * AdjRib::registerDetachedConsumer, the per-peer detached consumer.
 *
 * Mechanics (see AdjRibGroup / ChangeTracker / Consumer):
 *   - A non-add-path consumer registers its bit in nonAddPathConsumerBitmap_
 *     (AdjRibOutGroupConsumer::setBitmap / AdjRibOutConsumer::setBitmap). An
 *     add-path-only change is published with addPathConsumerBitmap_
 *     (PeerManagerVrf::getConsumerBitmapForChange(isBestpathChange=false)), so
 *     such a consumer is never marked for it.
 *   - Adding a second, equal-cost path for an existing prefix is exactly that
 *     change: RibBase emits only addPathEntries because commitMultipaths() is
 *     true while commitBestpath() is false (the ECMP set grew, but the best
 *     path and the aggregate UCMP / partial-drain fields did not change).
 *   - PeerManagerBase::setMaxRibVersion runs up front for every announcement
 *     batch, before any publish/skip decision, so maxRibVersion_ advances
 *     regardless of who ends up marked.
 *   - Consumer::markProcessed skips items whose bit is not set, and
 *     ChangeTracker::shouldSkipChangePublication drops a change whose bitmap is
 *     empty for a prefix no longer on the change list. Either way the consumer
 *     never sees it, so per-item advancement alone permanently trails the RIB.
 *
 * All peers in the group under test are non-add-path on purpose. Adding a real
 * add-path peer would mask the bug rather than sharpen it: a consumer sitting
 * at the tail of the change list is in readyConsumers_, and
 * ChangeTracker::notifyReadyConsumers sets a ready consumer's marker to a newly
 * published item WITHOUT checking the consumer bit -- it would then consume the
 * add-path item and advance anyway.
 */

#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupSlowPeerTestCommon.h"

using namespace facebook::nettools::bgplib;

namespace facebook {
namespace bgp {

namespace {
/* Group-level test: one prefix that grows a second equal-cost path. */
constexpr auto kPrefix = "10.0.1.0";
constexpr int kPrefixLen = 24;
constexpr auto kPrefixStr = "10.0.1.0/24";

/* Detached test: local routes that fill peer4's queue and freeze the group. */
constexpr int kNumPlugRoutes = 9; /* peer4 highWm 8, + 1 */

/*
 * Detached test: prefixes that later grow a second equal-cost path. The first
 * half is flapped before peer5 joins (A -> B), the second half after it parks
 * in DETACHED_READY_TO_JOIN (B -> C).
 */
constexpr int kNumEcmpBeforeJoin = 5;
constexpr int kNumEcmpAfterJoin = 5;
constexpr int kNumEcmpPrefixes = kNumEcmpBeforeJoin + kNumEcmpAfterJoin;
constexpr int kEcmpPrefixLen = 16;

/* Equal local-pref on both paths so the second joins ECMP without winning. */
constexpr uint32_t kEqualLocalPref = 100;

constexpr int kRetries = 30;

std::string plugPrefix(int index) {
  return fmt::format("27.{}.0.0/16", index);
}
std::string ecmpPrefix(int index) {
  return fmt::format("28.{}.0.0", index);
}
std::string ecmpPrefixStr(int index) {
  return fmt::format("28.{}.0.0/16", index);
}
} // namespace

class UpdateGroupAddPathOnlyVersionE2ETest : public SlowPeerTestBase {
 protected:
  /* Current RIB version, read on the RIB event base to avoid a TSAN race. */
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

  /* PeerManager's max seen RIB version, read on its event base. */
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

  /* The update group's cached version, read on the PeerManager event base. */
  uint64_t getGroupRibVersion(const folly::IPAddress& peerAddr) {
    uint64_t version = 0;
    peerManager_->getEventBase().runInEventBaseThreadAndWait([&]() {
      auto group = getUpdateGroupForPeer(peerAddr);
      if (group) {
        version = group->getLastSeenRibVersion();
      }
    });
    return version;
  }

  /*
   * Grow prefix `index` to two equal-cost paths: peer3 already advertises it,
   * peer6 adds a second path at the same local-pref.
   */
  void addPathOnlyFlap(int index) {
    addRoute(
        "v4",
        ecmpPrefix(index),
        kEcmpPrefixLen,
        kPeerAddr6,
        kNextHopV4_6.str(),
        "" /* asPath */,
        "" /* community */,
        0 /* addPathId */,
        kEqualLocalPref);
    ASSERT_TRUE(waitForMultipathNexthopCount(ecmpPrefixStr(index), 2))
        << "second equal-cost path did not join the ECMP set for "
        << ecmpPrefixStr(index);
  }

  /* peer6 sources second equal-cost paths from outside the group under test. */
  BgpPeerSpec makeEcmpSourcePeer() {
    BgpPeerSpec spec{};
    spec.asn = kPeerAsn6;
    spec.localAddr = kLocalAddr6;
    spec.peerAddr = kPeerAddr6;
    spec.v4Nexthop = kNextHopV4_6;
    spec.v6Nexthop = kEmptyV6Nexthop;
    spec.disableIpv6Afi = true;
    spec.peerGroupName = "ecmp-source-group";
    return spec;
  }
};

/*
 * The group must report the max RIB version once it has iterated to the end of
 * the change list, not the version of the last item it happened to be marked
 * for.
 */
TEST_P(
    UpdateGroupAddPathOnlyVersionE2ETest,
    GroupVersionAdvancesOnAddPathOnlyChange) {
  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  addPeer(kDefaultPeerSpec5);
  setupComponents();

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  bringUpPeer(kPeerAddr5);

  /*
   * A best-path announcement from peer3: a non-add-path change, so the group's
   * consumer is marked for it and ends up fully caught up -- the baseline the
   * add-path-only change is measured against.
   */
  addRoute(
      "v4",
      kPrefix,
      kPrefixLen,
      kPeerAddr3,
      kNextHopV4_3.str(),
      "" /* asPath */,
      "" /* community */,
      0 /* addPathId */,
      kEqualLocalPref);
  ASSERT_TRUE(verifyRouteAdd(
      "v4", kPrefix, kPrefixLen, kPeerAddr5, kNextHopV4_5.str()));

  uint64_t baselineVersion = 0;
  WITH_RETRIES_N(kRetries, {
    baselineVersion = getMaxRibVersion();
    EXPECT_EVENTUALLY_TRUE(
        baselineVersion > 0 &&
        getGroupRibVersion(kPeerAddr5) == baselineVersion);
  });
  ASSERT_GT(baselineVersion, 0);
  ASSERT_EQ(getGroupRibVersion(kPeerAddr5), baselineVersion)
      << "group must be caught up before the add-path-only change, otherwise "
         "the test cannot attribute a later lag to the skipped change";

  /*
   * peer4 advertises the same prefix at the same local-pref. peer3 keeps best
   * path, so the ECMP set grows without a best-path change.
   */
  addRoute(
      "v4",
      kPrefix,
      kPrefixLen,
      kPeerAddr4,
      kNextHopV4_4.str(),
      "" /* asPath */,
      "" /* community */,
      0 /* addPathId */,
      kEqualLocalPref);
  ASSERT_TRUE(waitForMultipathNexthopCount(kPrefixStr, 2))
      << "second equal-cost path did not join the ECMP set";

  /* The RIB / shadow RIB versions advance even though nobody consumed it. */
  uint64_t maxVersionAfterEcmp = 0;
  WITH_RETRIES_N(kRetries, {
    maxVersionAfterEcmp = getMaxRibVersion();
    EXPECT_EVENTUALLY_GT(maxVersionAfterEcmp, baselineVersion);
  });
  ASSERT_GT(maxVersionAfterEcmp, baselineVersion)
      << "add-path-only change should still bump the shadow RIB max version";
  EXPECT_EQ(getRibVersion(), maxVersionAfterEcmp);

  uint64_t groupVersion = 0;
  WITH_RETRIES_N(kRetries, {
    groupVersion = getGroupRibVersion(kPeerAddr5);
    EXPECT_EVENTUALLY_GE(groupVersion, maxVersionAfterEcmp);
  });

  EXPECT_EQ(groupVersion, maxVersionAfterEcmp)
      << "group RIB version stuck at " << groupVersion << " (max is "
      << maxVersionAfterEcmp
      << "): the add-path-only change was never marked for the non-add-path "
         "group's consumer, so the group must pick up maxRibVersion after "
         "iterating to the end of the change list";
}

/*
 * Per-peer half. Every version bump after the freeze is add-path-only, so a
 * detached non-add-path peer is never marked for any of them:
 *
 *   1. The group freezes at version A behind a JOINED_BLOCKED (not detached)
 *      peer4.
 *   2. Add-path-only churn takes the RIB to B > A. The group stays at A.
 *   3. peer5 joins late. The frozen group forces an independent initial dump,
 *      which lands it at B -- ahead of the group -- so it parks in
 *      DETACHED_READY_TO_JOIN.
 *   4. More add-path-only churn takes the RIB to C > B.
 *   5. peer4 unblocks; the group consumes to the end of the change list and
 *      claims C.
 *
 * peer5 can only rejoin when its version equals the group's
 * (AdjRib::canWaitForGroupToRejoin, strict equality). Nothing between B and C
 * is marked for its consumer, so per-item advancement leaves it pinned at B:
 * the only way to reach C is to claim maxRibVersion on reaching the end of the
 * change list. The rejoin is the assertion rather than the version itself,
 * because once back in sync AdjRib::getLastSeenRibVersion() reports the
 * group's version rather than the peer's own.
 */
TEST_P(
    UpdateGroupAddPathOnlyVersionE2ETest,
    DetachedPeerRejoinsAfterAddPathOnlyChurn) {
  addPeer(kDefaultPeerSpec3);
  addPeer(kDefaultPeerSpec4);
  addPeer(kDefaultPeerSpec5);
  addPeer(makeEcmpSourcePeer());

  setDefaultQueueSizes(/*capacity=*/100, /*highWm=*/80, /*lowWm=*/2);
  /* Only peer4 gets a small queue -- it is what freezes the group. */
  setQueueSizeForPeer(kPeerAddr4, /*capacity=*/10, /*highWm=*/8, /*lowWm=*/2);
  setupComponents();

  BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
  BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};
  BgpPeerId peerId6{kPeerAddr6, kPeerAddr6.asV4().toLongHBO()};

  bringUpPeer(kPeerAddr3);
  bringUpPeer(kPeerAddr4);
  bringUpPeer(kPeerAddr6);
  sendEoRToPeer(peerId3);
  sendEoRToPeer(peerId4);
  sendEoRToPeer(peerId6);
  EXPECT_TRUE(waitForEoR(peerId3));
  EXPECT_TRUE(waitForEoR(peerId4));
  EXPECT_TRUE(waitForEoR(peerId6));
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  /*
   * Seed every ECMP prefix with its first (best) path while the group is
   * healthy, so the later second paths are pure multipath changes.
   */
  for (int i = 1; i <= kNumEcmpPrefixes; ++i) {
    addRoute(
        "v4",
        ecmpPrefix(i),
        kEcmpPrefixLen,
        kPeerAddr3,
        kNextHopV4_3.str(),
        "" /* asPath */,
        "" /* community */,
        0 /* addPathId */,
        kEqualLocalPref);
    ASSERT_TRUE(waitForRouteInShadowRib(
        folly::IPAddress::createNetwork(ecmpPrefixStr(i))));
  }
  recordDrainedRoutes(peerId3, 1, 200);

  /* Step 1: freeze the group at version A behind a JOINED_BLOCKED peer4. */
  setSlowPeerThresholds(
      kPeerAddr4,
      std::chrono::milliseconds(600000),
      1000000,
      std::chrono::milliseconds(600000));
  blockPeer(kPeerAddr4);
  for (int i = 1; i <= kNumPlugRoutes; ++i) {
    injectLocalRoutesAtRuntime(
        {plugPrefix(i)}, {fmt::format("270{}:1", i)}, 150);
    ASSERT_TRUE(waitForRouteInShadowRib(
        folly::IPAddress::createNetwork(plugPrefix(i))));
  }
  EXPECT_TRUE(waitForPeerQueueBlocked(peerId4));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_BLOCKED));
  EXPECT_FALSE(isPeerDetached(kPeerAddr4));
  recordDrainedRoutes(peerId3, 1, 200);

  const uint64_t versionA = getGroupRibVersion(kPeerAddr3);

  /* Step 2: add-path-only churn takes the RIB from A to B. */
  for (int i = 1; i <= kNumEcmpBeforeJoin; ++i) {
    addPathOnlyFlap(i);
  }
  const uint64_t versionB = getMaxRibVersion();
  ASSERT_GT(versionB, versionA)
      << "add-path-only churn did not advance the RIB past the frozen group";

  /*
   * Step 3: peer5 joins late. The frozen group forces an independent initial
   * dump, landing peer5 at B -- ahead of the group at A -- so it parks in
   * DETACHED_READY_TO_JOIN.
   */
  bringUpPeer(kPeerAddr5);
  sendEoRToPeer(peerId5);
  WITH_RETRIES_N(kRetries, {
    recordDrainedRoutes(peerId5, 1, 200);
    ASSERT_EVENTUALLY_EQ(
        getPeerState(kPeerAddr5), PeerUpdateState::DETACHED_READY_TO_JOIN);
  });
  ASSERT_TRUE(isPeerDetached(kPeerAddr5));

  /* Step 4: more add-path-only churn takes the RIB from B to C. */
  for (int i = kNumEcmpBeforeJoin + 1; i <= kNumEcmpPrefixes; ++i) {
    addPathOnlyFlap(i);
  }
  const uint64_t versionC = getMaxRibVersion();
  ASSERT_GT(versionC, versionB);

  /* Step 5: unblock peer4 so the group consumes to the end and claims C. */
  unblockPeer(kPeerAddr4);
  WITH_RETRIES_N(kRetries, {
    recordDrainedRoutes(peerId3, 1, 200);
    recordDrainedRoutes(peerId4, 1, 200);
    recordDrainedRoutes(peerId5, 1, 200);
    ASSERT_EVENTUALLY_EQ(
        getPeerState(kPeerAddr5), PeerUpdateState::JOINED_RUNNING);
  });

  EXPECT_TRUE(isPeerInSync(kPeerAddr5))
      << "peer5 stuck detached at version " << versionB << " while the group "
      << "reached " << versionC
      << ": every bump in between was add-path-only and so was never marked "
         "for peer5's consumer, leaving rejoin gated forever";
}

INSTANTIATE_TEST_SUITE_P(
    SerializationModes,
    UpdateGroupAddPathOnlyVersionE2ETest,
    ::testing::Values(kNoSerialization, kWithSerialization),
    [](const ::testing::TestParamInfo<SerializationParams>& info) {
      return info.param.name;
    });

} // namespace bgp
} // namespace facebook
