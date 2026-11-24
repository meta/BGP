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
 * E2E tests for the per-address-family RIB path gauges:
 *   bgpd.rib.total_paths.{ipv4,ipv6}.count
 *   bgpcpp.rib.inactive_paths.unresolved_nexthops.{ipv4,ipv6}.count
 *
 * Both are published from RibCounters at RIB batch boundaries. The property
 * under test is that the two families always add up to their aggregate:
 *   - total_paths v4 + v6 equals the aggregate bgpd.rib.total_paths.count
 *     gauge. bgpd.rib.totalRibPaths is deliberately not compared: it counts
 *     (prefix, peer) pairs, not paths, so it undercounts under ADD-PATH.
 *   - inactive unresolved-nexthop paths v4 + v6 equals the aggregate
 *     bgpcpp.rib.inactive_paths.unresolved_nexthops.count gauge.
 * Each family is additionally checked against getRibSummary for that family.
 *
 * The sums are checked after every step of a 100-prefix (40 IPv4 / 60 IPv6)
 * multipath churn sequence -- announce, re-announce with changed attributes,
 * repeated per-peer flaps, partial withdrawal, full withdrawal -- because a
 * per-family gauge that is incremented on one path and decremented on another
 * only drifts under churn.
 *
 * Every wait here is a blocking queue read plus an event-base hop; see
 * syncPathCountersPublished(). Nothing sleeps or retries, so the assertions
 * are exact equalities rather than eventual ones.
 */

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <fmt/format.h>
#include <folly/logging/xlog.h>

#include "fb303/ServiceData.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopStatus.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/AdjRibOutUtils.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/E2ETestFixture.h"

using namespace facebook::bgp;
using facebook::neteng::fboss::bgp_attr::TBgpAfi;

namespace facebook::bgp {

namespace {
/* 100 prefixes total, split across the two families. */
constexpr int kV4PrefixCount = 40;
constexpr int kV6PrefixCount = 60;
constexpr uint8_t kV4PrefixLen = 16;
constexpr uint8_t kV6PrefixLen = 48;

/* Both peers advertise every prefix, so each prefix carries two paths. */
constexpr int kPeerCount = 2;

/*
 * Pipeline barrier prefix. Deliberately outside the churn set (which occupies
 * 10.0/16 .. 10.39/16) so it never perturbs the counts under test, and it is
 * withdrawn again before any assertion runs.
 */
constexpr auto kBarrierPrefix = "10.255.0.0";
constexpr uint8_t kBarrierPrefixLen = 24;

std::string v4Prefix(int i) {
  return fmt::format("10.{}.0.0", i);
}

std::string v6Prefix(int i) {
  return fmt::format("2001:db8:{:x}::", i);
}

/* An IPv4 / IPv6 pair of one of the per-family gauges. */
struct FamilyCounts {
  int64_t v4{0};
  int64_t v6{0};

  int64_t sum() const {
    return v4 + v6;
  }
};

int64_t readCounter(const std::string& key) {
  auto tcData = fb303::ThreadCachedServiceData::get();
  tcData->publishStats();
  return tcData->getCounter(key);
}
} // namespace

class RibPathCountersE2ETest : public E2ETestFixture {
 protected:
  void SetUp() override {
    setUpComponents(kDefaultPeerSpec3);
  }

  /* `peer3Spec` lets a derived fixture give peer3 the ADD-PATH capability. */
  void setUpComponents(const BgpPeerSpec& peer3Spec) {
    RibStats::initCounters();
    addPeer(peer3Spec);
    addPeer(kDefaultPeerSpec4);
    addPeer(kDefaultPeerSpec5);
    /*
     * Nexthop tracking is what makes a path inactive: prePathSelectionFiltering
     * excludes a path whose nexthop is unresolved only when the feature is on.
     */
    createRib(true /* enableNexthopTracking */);
    createPeerManager(
        /*enableUpdateGroup=*/false);
  }

  /* peer5 advertises nothing; it exists purely to observe the RIB's output. */
  BgpPeerId observerPeerId() const {
    return BgpPeerId{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};
  }

  void bringUpAllPeersWithEor() {
    bringUpPeer(kPeerAddr3);
    bringUpPeer(kPeerAddr4);
    bringUpPeer(kPeerAddr5);
    const BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
    const BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
    sendEoRToPeer(peerId3);
    sendEoRToPeer(peerId4);
    sendEoRToPeer(observerPeerId());
    ASSERT_TRUE(waitForEoR(peerId3));
    ASSERT_TRUE(waitForEoR(peerId4));
    ASSERT_TRUE(waitForEoR(observerPeerId()));
  }

  void setNexthopReachable(const folly::IPAddress& nexthop, bool reachable) {
    std::vector<NexthopStatus> statuses;
    statuses.emplace_back(
        NexthopKey{.ip = nexthop},
        reachable,
        reachable ? std::make_optional<uint32_t>(100) : std::nullopt);
    injectNexthopStatuses(statuses);
  }

  /*
   * Every nexthop the two advertising peers use, resolved with an IGP cost.
   * kNextHopV4_3 carries the barrier prefix and must stay resolved for the
   * whole test; the inactive-path cases only ever unresolve the other three.
   */
  void resolveAllNexthops() {
    for (const auto& nexthop :
         {kNextHopV4_3, kNextHopV4_4, kNextHopV6_3, kNextHopV6_4}) {
      setNexthopReachable(nexthop, true);
    }
  }

  // ==================== SYNCHRONIZATION ====================

  /*
   * Pop the observer peer's egress queue until `prefix` shows up, announced or
   * withdrawn as requested.
   *
   * readOutboundUpdateToPeer blocks on the queue (boundedBlockingPop, 90s
   * budget) instead of polling, so an UPDATE that has not been produced yet
   * parks this thread rather than spinning or sleeping. A producer that dies
   * surfaces as a BoundedWaitTimeout naming the queue.
   */
  void awaitObserverSees(const folly::CIDRNetwork& prefix, bool announced) {
    while (true) {
      auto update = readOutboundUpdateToPeer(observerPeerId());
      ASSERT_TRUE(update.has_value())
          << "observer queue closed while waiting for "
          << folly::IPAddress::networkToString(prefix);
      const bool seen = announced
          ? findPrefixInAnnouncements(**update, /*isV4=*/true, prefix)
          : findPrefixInWithdrawals(**update, /*isV4=*/true, prefix);
      if (seen) {
        return;
      }
    }
  }

  /*
   * Return once every route change queued so far has reached the RIB-side
   * counter publication points.
   *
   *  1. Announce, then withdraw, a barrier prefix. ribInQ_ is FIFO and
   *     injectNexthopStatuses feeds the same queue, so the barrier is
   *     consumed strictly after everything the caller queued. Its withdrawal
   *     restores the total-path gauges before any assertion runs.
   *  2. Seeing the withdrawal proves the corresponding best-path-selection
   *     pass started. Hop the RIB event base so that pass must return, after
   *     publishing the per-family inactive-path gauges.
   *
   * Both steps block on a real signal, so no polling or sleeping is involved
   * and callers can assert exact values.
   */
  void syncPathCountersPublished() {
    const auto barrier = folly::IPAddress::createNetwork(
        fmt::format("{}/{}", kBarrierPrefix, kBarrierPrefixLen));

    addRoute(
        "v4",
        kBarrierPrefix,
        kBarrierPrefixLen,
        kPeerAddr3,
        kNextHopV4_3.str(),
        "65001");
    ASSERT_NO_FATAL_FAILURE(awaitObserverSees(barrier, /*announced=*/true));

    deleteRoute("v4", kBarrierPrefix, kBarrierPrefixLen, kPeerAddr3);
    ASSERT_NO_FATAL_FAILURE(awaitObserverSees(barrier, /*announced=*/false));

    rib_->getNumPrefixes();
  }

  // ==================== ROUTE CHURN ====================

  /*
   * Announce `count` prefixes of one family from `peer`. `med` distinguishes
   * a re-announcement from the original: it changes the path's attributes
   * without adding a path, so the path counts must not move.
   */
  void announceV4(
      const folly::IPAddress& peer,
      const folly::IPAddress& nexthop,
      const std::string& asPath,
      int count = kV4PrefixCount,
      uint32_t med = 0,
      int step = 1,
      uint32_t addPathId = 0) {
    for (int i = 0; i < count; i += step) {
      addRoute(
          "v4",
          v4Prefix(i),
          kV4PrefixLen,
          peer,
          nexthop.str(),
          asPath,
          /*community=*/"",
          addPathId,
          /*localPref=*/100,
          med);
    }
  }

  void announceV6(
      const folly::IPAddress& peer,
      const folly::IPAddress& nexthop,
      const std::string& asPath,
      int count = kV6PrefixCount,
      uint32_t med = 0,
      int step = 1,
      uint32_t addPathId = 0) {
    for (int i = 0; i < count; i += step) {
      addRoute(
          "v6",
          v6Prefix(i),
          kV6PrefixLen,
          peer,
          nexthop.str(),
          asPath,
          /*community=*/"",
          addPathId,
          /*localPref=*/100,
          med);
    }
  }

  void withdrawV4(
      const folly::IPAddress& peer,
      int count = kV4PrefixCount,
      int step = 1,
      uint32_t addPathId = 0) {
    for (int i = 0; i < count; i += step) {
      deleteRoute("v4", v4Prefix(i), kV4PrefixLen, peer, addPathId);
    }
  }

  void withdrawV6(
      const folly::IPAddress& peer,
      int count = kV6PrefixCount,
      int step = 1,
      uint32_t addPathId = 0) {
    for (int i = 0; i < count; i += step) {
      deleteRoute("v6", v6Prefix(i), kV6PrefixLen, peer, addPathId);
    }
  }

  /* Announce all 100 prefixes from one peer, using that peer's nexthops. */
  void announceAllFrom(
      const folly::IPAddress& peer,
      const folly::IPAddress& v4Nexthop,
      const folly::IPAddress& v6Nexthop,
      const std::string& asPath,
      uint32_t med = 0) {
    announceV4(peer, v4Nexthop, asPath, kV4PrefixCount, med);
    announceV6(peer, v6Nexthop, asPath, kV6PrefixCount, med);
  }

  void withdrawAllFrom(const folly::IPAddress& peer) {
    withdrawV4(peer);
    withdrawV6(peer);
  }

  // ==================== COUNTER READERS ====================

  FamilyCounts fb303TotalPaths() {
    return {
        readCounter(RibStats::kTotalPathsCountIpv4),
        readCounter(RibStats::kTotalPathsCountIpv6)};
  }

  FamilyCounts fb303InactivePaths() {
    return {
        readCounter(RibStats::kInactivePathsUnresolvedNexthopsCountIpv4),
        readCounter(RibStats::kInactivePathsUnresolvedNexthopsCountIpv6)};
  }

  int64_t fb303InactivePathAggregate() {
    return readCounter(RibStats::kInactivePathsUnresolvedNexthopsCount);
  }

  int64_t fb303TotalPathsAggregate() {
    return readCounter(RibStats::kTotalPathsCount);
  }

  /*
   * The RIB's own per-family view, the reference the gauges mirror.
   */
  FamilyCounts ribTotalPaths() {
    return {
        *rib_->getRibSummary(TBgpAfi::AFI_IPV4).total_paths(),
        *rib_->getRibSummary(TBgpAfi::AFI_IPV6).total_paths()};
  }

  FamilyCounts ribInactivePaths() {
    return {
        *rib_->getRibSummary(TBgpAfi::AFI_IPV4).inactive_paths(),
        *rib_->getRibSummary(TBgpAfi::AFI_IPV6).inactive_paths()};
  }

  // ==================== ASSERTIONS ====================

  /*
   * Assert the per-family total_paths gauges add up to the aggregate.
   * `expected` is derived from what the test announced, never read back from
   * the implementation.
   */
  void expectTotalPathCounters(const FamilyCounts& expected) {
    ASSERT_NO_FATAL_FAILURE(syncPathCountersPublished());

    const auto gauges = fb303TotalPaths();
    EXPECT_EQ(expected.v4, gauges.v4);
    EXPECT_EQ(expected.v6, gauges.v6);
    EXPECT_EQ(expected.sum(), gauges.sum());
    EXPECT_EQ(fb303TotalPathsAggregate(), gauges.sum());
    EXPECT_EQ(ribTotalPaths().sum(), gauges.sum());
  }

  /*
   * The per-family inactive gauges must add up to the aggregate gauge the
   * dashboards already read, and each family must match the RIB.
   */
  void expectInactivePathCounters(const FamilyCounts& expected) {
    ASSERT_NO_FATAL_FAILURE(syncPathCountersPublished());

    const auto gauges = fb303InactivePaths();
    const auto ribCounts = ribInactivePaths();
    EXPECT_EQ(expected.v4, gauges.v4);
    EXPECT_EQ(expected.v6, gauges.v6);
    EXPECT_EQ(expected.sum(), gauges.sum());
    EXPECT_EQ(fb303InactivePathAggregate(), gauges.sum());
    EXPECT_EQ(ribCounts.v4, gauges.v4);
    EXPECT_EQ(ribCounts.v6, gauges.v6);
  }
};

/*
 * 100 prefixes (40 IPv4 / 60 IPv6) advertised by two peers, then churned:
 * re-advertised with changed attributes, flapped peer-by-peer, partially
 * withdrawn, fully withdrawn. After every step the per-family total_paths
 * gauges must add up to the paths the test injected.
 */
TEST_F(RibPathCountersE2ETest, PerFamilyTotalPathGaugesSumAcrossChurn) {
  bringUpAllPeersWithEor();
  resolveAllNexthops();

  expectTotalPathCounters({0, 0});

  /* One path per prefix, from peer3. */
  announceAllFrom(kPeerAddr3, kNextHopV4_3, kNextHopV6_3, "65001");
  expectTotalPathCounters({kV4PrefixCount, kV6PrefixCount});

  /* peer4 adds a second path to every prefix. */
  announceAllFrom(kPeerAddr4, kNextHopV4_4, kNextHopV6_4, "65004");
  expectTotalPathCounters(
      {kV4PrefixCount * kPeerCount, kV6PrefixCount * kPeerCount});

  /*
   * Re-advertise every prefix from both peers with a different MED. This
   * replaces each path's attributes rather than adding a path, so the counts
   * must be unchanged -- a re-announcement that double-counted would show up
   * here.
   */
  announceAllFrom(kPeerAddr3, kNextHopV4_3, kNextHopV6_3, "65001", /*med=*/50);
  announceAllFrom(kPeerAddr4, kNextHopV4_4, kNextHopV6_4, "65004", /*med=*/60);
  expectTotalPathCounters(
      {kV4PrefixCount * kPeerCount, kV6PrefixCount * kPeerCount});

  /* Flap peer4's full contribution three times. */
  constexpr int kFlapCycles = 3;
  for (int cycle = 0; cycle < kFlapCycles; ++cycle) {
    withdrawAllFrom(kPeerAddr4);
    expectTotalPathCounters({kV4PrefixCount, kV6PrefixCount});

    announceAllFrom(kPeerAddr4, kNextHopV4_4, kNextHopV6_4, "65004");
    expectTotalPathCounters(
        {kV4PrefixCount * kPeerCount, kV6PrefixCount * kPeerCount});
  }

  /*
   * Withdraw every other prefix from peer3 only. Those prefixes survive on
   * peer4's path, so this exercises the per-path decrement on a prefix that is
   * not removed.
   */
  constexpr int kStride = 2;
  withdrawV4(kPeerAddr3, kV4PrefixCount, kStride);
  withdrawV6(kPeerAddr3, kV6PrefixCount, kStride);
  expectTotalPathCounters(
      {kV4PrefixCount * kPeerCount - kV4PrefixCount / kStride,
       kV6PrefixCount * kPeerCount - kV6PrefixCount / kStride});

  /* Everything gone: both families drain to zero. */
  withdrawAllFrom(kPeerAddr3);
  withdrawAllFrom(kPeerAddr4);
  expectTotalPathCounters({0, 0});
}

/*
 * Making a nexthop unresolvable moves every path behind it out of best-path
 * selection. The per-family inactive unresolved-nexthop path gauges must
 * always add up to the aggregate gauge as the unresolvable set changes.
 */
TEST_F(RibPathCountersE2ETest, PerFamilyInactivePathGaugesSumToAggregate) {
  bringUpAllPeersWithEor();
  resolveAllNexthops();

  announceAllFrom(kPeerAddr3, kNextHopV4_3, kNextHopV6_3, "65001");
  announceAllFrom(kPeerAddr4, kNextHopV4_4, kNextHopV6_4, "65004");
  expectTotalPathCounters(
      {kV4PrefixCount * kPeerCount, kV6PrefixCount * kPeerCount});

  /* Every nexthop resolves, so no path is excluded. */
  expectInactivePathCounters({0, 0});

  /* peer4's IPv4 nexthop goes away: its IPv4 paths become inactive. */
  setNexthopReachable(kNextHopV4_4, false);
  expectInactivePathCounters({kV4PrefixCount, 0});

  /* peer3's IPv6 nexthop goes away too: both families now contribute. */
  setNexthopReachable(kNextHopV6_3, false);
  expectInactivePathCounters({kV4PrefixCount, kV6PrefixCount});

  /* Withdrawing an inactive path releases its contribution. */
  withdrawV4(kPeerAddr4);
  expectInactivePathCounters({0, kV6PrefixCount});

  /* Resolving the remaining unreachable nexthop returns its paths. */
  setNexthopReachable(kNextHopV6_3, true);
  expectInactivePathCounters({0, 0});
  expectTotalPathCounters({kV4PrefixCount, kV6PrefixCount * kPeerCount});
}

/* peer3 negotiates ADD-PATH, so it can hold several paths per prefix. */
class RibPathCountersAddPathE2ETest : public RibPathCountersE2ETest {
 protected:
  void SetUp() override {
    setUpComponents(kDefaultPeerSpec3_AddPath);
  }

  int64_t fb303TotalRibPaths() {
    return readCounter(RibStats::kTotalRibPaths);
  }
};

/*
 * With ADD-PATH, one peer contributes a path per path-id. total_paths must
 * count every one of them, while totalRibPaths -- which counts (prefix, peer)
 * pairs -- stays at one per peer. The two are asserted separately, never
 * against each other.
 */
TEST_F(RibPathCountersAddPathE2ETest, TotalPathsCountsEveryAddPathId) {
  bringUpAllPeersWithEor();
  resolveAllNexthops();

  constexpr uint32_t kAddPathIds = 3;
  constexpr int kPrefixCount = kV4PrefixCount + kV6PrefixCount;

  /* peer3 sends path-ids 1..3 for every prefix, each with distinct attrs. */
  for (uint32_t pathId = 1; pathId <= kAddPathIds; ++pathId) {
    announceV4(
        kPeerAddr3, kNextHopV4_3, "65001", kV4PrefixCount, pathId, 1, pathId);
    announceV6(
        kPeerAddr3, kNextHopV6_3, "65001", kV6PrefixCount, pathId, 1, pathId);
  }
  expectTotalPathCounters(
      {kV4PrefixCount * kAddPathIds, kV6PrefixCount * kAddPathIds});
  EXPECT_EQ(kPrefixCount, fb303TotalRibPaths());

  /* peer4 adds one plain path to every prefix. */
  announceAllFrom(kPeerAddr4, kNextHopV4_4, kNextHopV6_4, "65004");
  expectTotalPathCounters(
      {kV4PrefixCount * (kAddPathIds + 1), kV6PrefixCount * (kAddPathIds + 1)});
  EXPECT_EQ(kPrefixCount * kPeerCount, fb303TotalRibPaths());

  /*
   * Withdraw a single path-id from peer3. peer3 still holds the other two, so
   * total_paths drops by one per prefix and totalRibPaths does not move.
   */
  constexpr uint32_t kWithdrawnPathId = 2;
  withdrawV4(kPeerAddr3, kV4PrefixCount, 1, kWithdrawnPathId);
  withdrawV6(kPeerAddr3, kV6PrefixCount, 1, kWithdrawnPathId);
  expectTotalPathCounters(
      {kV4PrefixCount * kAddPathIds, kV6PrefixCount * kAddPathIds});
  EXPECT_EQ(kPrefixCount * kPeerCount, fb303TotalRibPaths());

  /* Everything gone: both counters drain to zero. */
  for (uint32_t pathId = 1; pathId <= kAddPathIds; ++pathId) {
    if (pathId == kWithdrawnPathId) {
      continue;
    }
    withdrawV4(kPeerAddr3, kV4PrefixCount, 1, pathId);
    withdrawV6(kPeerAddr3, kV6PrefixCount, 1, pathId);
  }
  withdrawAllFrom(kPeerAddr4);
  expectTotalPathCounters({0, 0});
  EXPECT_EQ(0, fb303TotalRibPaths());
}

} // namespace facebook::bgp
