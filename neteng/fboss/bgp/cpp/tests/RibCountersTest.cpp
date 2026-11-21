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

#include <cstdint>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "fb303/ServiceData.h"
#include "neteng/fboss/bgp/cpp/common/Types.h"
#include "neteng/fboss/bgp/cpp/rib/RibCounters.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

namespace facebook::bgp {

namespace {
int64_t counter(const std::string& key) {
  auto tcData = fb303::ThreadCachedServiceData::get();
  tcData->publishStats();
  return tcData->getCounter(key);
}
} // namespace

/*
 * Each mutator must update the in-memory field AND mirror the matching fb303
 * ODS counter, so existing dashboards / HealthValidator observe identical
 * values.
 */
TEST(RibCountersTest, PrefixCountTracksFieldAndFb303) {
  RibStats::initCounters();
  RibCounters c;
  EXPECT_EQ(0, c.totalPrefixes());
  EXPECT_EQ(0, counter(RibStats::kRibPrefixCount));

  c.onPrefixAdded(/*isV4=*/true, 24);
  c.onPrefixAdded(/*isV4=*/false, 64);
  EXPECT_EQ(2, c.totalPrefixes());
  EXPECT_EQ(2, counter(RibStats::kRibPrefixCount));

  c.onPrefixRemoved(/*isV4=*/true, 24, /*inactivePaths=*/0);
  EXPECT_EQ(1, c.totalPrefixes());
  EXPECT_EQ(1, counter(RibStats::kRibPrefixCount));
}

// Counters built with publishToFb303 = false keep their counts in memory only.
TEST(RibCountersTest, NonPublishingCountersLeaveFb303Unchanged) {
  RibStats::initCounters();
  RibCounters c{/*publishToFb303=*/false};

  c.onPrefixAdded(/*isV4=*/true, 24);
  c.onPathsDelta(/*isV4=*/true, 3);
  c.onInactivePathsDelta(/*isV4=*/true, 1);
  c.publishTotalPathCounters();
  c.setOriginatedRoutes(2);
  c.incrementUnresolvableNexthopCount();

  EXPECT_EQ(1, c.totalPrefixes());
  EXPECT_EQ(3, c.totalPaths());
  EXPECT_EQ(1, c.inactivePaths());
  EXPECT_EQ(2, c.originatedRoutes());
  EXPECT_EQ(1, c.unresolvableNexthops());
  EXPECT_EQ(0, counter(RibStats::kRibPrefixCount));
  EXPECT_EQ(0, counter(RibStats::kTotalPathsCount));
  EXPECT_EQ(0, counter(RibStats::kInactivePathCount));
  EXPECT_EQ(0, counter(RibStats::kRibUnresolvableNexthopsCount));
}

// Total and per-prefix-length counts are split per address family.
TEST(RibCountersTest, PerAfiPrefixAndPerLengthCounts) {
  RibStats::initCounters();
  RibCounters c;

  c.onPrefixAdded(/*isV4=*/true, 24);
  c.onPrefixAdded(/*isV4=*/true, 24);
  c.onPrefixAdded(/*isV4=*/true, 32);
  c.onPrefixAdded(/*isV4=*/false, 64);

  EXPECT_EQ(3, c.totalPrefixes(/*isV4=*/true));
  EXPECT_EQ(1, c.totalPrefixes(/*isV4=*/false));
  EXPECT_EQ(4, c.totalPrefixes());

  EXPECT_EQ(2, c.prefixLenCounts(/*isV4=*/true)[24]);
  EXPECT_EQ(1, c.prefixLenCounts(/*isV4=*/true)[32]);
  EXPECT_EQ(0, c.prefixLenCounts(/*isV4=*/true)[64]);
  EXPECT_EQ(1, c.prefixLenCounts(/*isV4=*/false)[64]);
  // v4 /24 must not leak into the v6 counts.
  EXPECT_EQ(0, c.prefixLenCounts(/*isV4=*/false)[24]);

  c.onPrefixRemoved(/*isV4=*/true, 24, /*inactivePaths=*/0);
  EXPECT_EQ(1, c.prefixLenCounts(/*isV4=*/true)[24]);
  EXPECT_EQ(2, c.totalPrefixes(/*isV4=*/true));
}

/*
 * Total paths are tracked per address family from signed deltas (positive on
 * announce, negative on withdraw), so add-path -- multiple paths from one peer
 * for a prefix, surfacing as a delta > 1 -- is counted correctly. The fb303
 * mirrors are published once per inbound batch rather than once per path.
 */
TEST(RibCountersTest, TotalPathsPerAfiFromDeltas) {
  RibStats::initCounters();
  RibCounters c;
  EXPECT_EQ(0, c.totalPaths());

  // v4 prefix gains a first path, then a second add-path from the same peer.
  c.onPathsDelta(/*isV4=*/true, 1);
  c.onPathsDelta(/*isV4=*/true, 1);
  // v6 prefix gains two paths in one update (e.g. two peers).
  c.onPathsDelta(/*isV4=*/false, 2);
  c.publishTotalPathCounters();

  EXPECT_EQ(2, c.totalPaths(/*isV4=*/true));
  EXPECT_EQ(2, c.totalPaths(/*isV4=*/false));
  EXPECT_EQ(4, c.totalPaths());
  EXPECT_EQ(4, counter(RibStats::kTotalPathsCount));
  EXPECT_EQ(2, counter(RibStats::kTotalPathsCountIpv4));
  EXPECT_EQ(2, counter(RibStats::kTotalPathsCountIpv6));

  // Withdrawing one v4 path decrements only v4; v6 is untouched.
  c.onPathsDelta(/*isV4=*/true, -1);
  c.publishTotalPathCounters();
  EXPECT_EQ(1, c.totalPaths(/*isV4=*/true));
  EXPECT_EQ(2, c.totalPaths(/*isV4=*/false));
  EXPECT_EQ(3, counter(RibStats::kTotalPathsCount));
  EXPECT_EQ(1, counter(RibStats::kTotalPathsCountIpv4));
  EXPECT_EQ(2, counter(RibStats::kTotalPathsCountIpv6));

  // A zero delta (an update that did not change the path count) is a no-op.
  c.onPathsDelta(/*isV4=*/true, 0);
  EXPECT_EQ(1, c.totalPaths(/*isV4=*/true));
}

/*
 * Inactive paths are tracked per address family from signed deltas, and the
 * fb303 gauge is SET from the aggregate rather than incremented -- so the
 * exported value always equals the in-memory total and cannot accumulate drift.
 */
TEST(RibCountersTest, InactivePathsPerAfiFromDeltas) {
  RibStats::initCounters();
  RibCounters c;
  EXPECT_EQ(0, c.inactivePaths());
  EXPECT_EQ(0, counter(RibStats::kInactivePathsUnresolvedNexthopsCount));
  EXPECT_EQ(0, counter(RibStats::kInactivePathCount));

  // Two v4 prefixes each gain an inactive path; one v6 prefix gains two.
  c.onInactivePathsDelta(/*isV4=*/true, 1);
  c.onInactivePathsDelta(/*isV4=*/true, 1);
  c.onInactivePathsDelta(/*isV4=*/false, 2);
  c.publishInactivePathCounters();

  EXPECT_EQ(2, c.inactivePaths(/*isV4=*/true));
  EXPECT_EQ(2, c.inactivePaths(/*isV4=*/false));
  EXPECT_EQ(4, c.inactivePaths());
  EXPECT_EQ(4, counter(RibStats::kInactivePathsUnresolvedNexthopsCount));
  EXPECT_EQ(2, counter(RibStats::kInactivePathsUnresolvedNexthopsCountIpv4));
  EXPECT_EQ(2, counter(RibStats::kInactivePathsUnresolvedNexthopsCountIpv6));
  EXPECT_EQ(4, counter(RibStats::kInactivePathCount));
  EXPECT_EQ(2, counter(RibStats::kInactivePathCountIpv4));
  EXPECT_EQ(2, counter(RibStats::kInactivePathCountIpv6));

  // A next-hop resolving decrements only its own address family.
  c.onInactivePathsDelta(/*isV4=*/true, -2);
  c.publishInactivePathCounters();
  EXPECT_EQ(0, c.inactivePaths(/*isV4=*/true));
  EXPECT_EQ(2, c.inactivePaths(/*isV4=*/false));
  EXPECT_EQ(2, counter(RibStats::kInactivePathsUnresolvedNexthopsCount));
  EXPECT_EQ(0, counter(RibStats::kInactivePathsUnresolvedNexthopsCountIpv4));
  EXPECT_EQ(2, counter(RibStats::kInactivePathsUnresolvedNexthopsCountIpv6));
  EXPECT_EQ(2, counter(RibStats::kInactivePathCount));
  EXPECT_EQ(0, counter(RibStats::kInactivePathCountIpv4));
  EXPECT_EQ(2, counter(RibStats::kInactivePathCountIpv6));

  /*
   * A zero delta -- a prefix re-selected with unchanged eligibility, which is
   * the common case on every path-selection pass -- is a no-op.
   */
  c.onInactivePathsDelta(/*isV4=*/false, 0);
  EXPECT_EQ(2, c.inactivePaths());
  EXPECT_EQ(2, counter(RibStats::kInactivePathCount));
}

TEST(RibCountersTest, PrefixRemovalReleasesCachedInactivePaths) {
  RibStats::initCounters();
  RibCounters c;

  c.onPrefixAdded(/*isV4=*/true, 24);
  c.onInactivePathsDelta(/*isV4=*/true, 3);
  ASSERT_EQ(3, c.inactivePaths());

  c.onPrefixRemoved(/*isV4=*/true, 24, /*inactivePaths=*/3);
  EXPECT_EQ(0, c.totalPrefixes());
  EXPECT_EQ(0, c.inactivePaths());
  EXPECT_EQ(0, counter(RibStats::kInactivePathsUnresolvedNexthopsCount));
  EXPECT_EQ(0, counter(RibStats::kInactivePathCount));
}

TEST(RibCountersTest, OriginatedRoutesTracksFieldAndFb303) {
  RibStats::initCounters();
  RibCounters c;

  c.setOriginatedRoutes(7);
  EXPECT_EQ(7, c.originatedRoutes());
  EXPECT_EQ(7, counter(RibStats::kTotalOriginatedRoutes));

  c.setOriginatedRoutes(3);
  EXPECT_EQ(3, c.originatedRoutes());
  EXPECT_EQ(3, counter(RibStats::kTotalOriginatedRoutes));
}

TEST(RibCountersTest, UnresolvableNexthopsTracksFieldAndFb303) {
  RibStats::initCounters();
  RibCounters c;

  c.incrementUnresolvableNexthopCount();
  c.incrementUnresolvableNexthopCount();
  c.incrementUnresolvableNexthopCount();
  EXPECT_EQ(3, c.unresolvableNexthops());
  EXPECT_EQ(3, counter(RibStats::kRibUnresolvableNexthopsCount));

  c.decrementUnresolvableNexthopCount();
  EXPECT_EQ(2, c.unresolvableNexthops());
  EXPECT_EQ(2, counter(RibStats::kRibUnresolvableNexthopsCount));
}

/*
 * Best-path source breakdown tracks appear / flip / withdrawal transitions,
 * is a no-op on unchanged class, and is isolated per address family.
 */
TEST(RibCountersTest, BestpathSourceBreakdown) {
  using RT = BgpRouteType;
  RibStats::initCounters();
  RibCounters c;

  // Prefixes appear with each source class (std::nullopt = no best path).
  c.onBestpathSourceChanged(/*isV4=*/true, std::nullopt, RT::EBGP);
  c.onBestpathSourceChanged(/*isV4=*/true, std::nullopt, RT::IBGP);
  c.onBestpathSourceChanged(/*isV4=*/true, std::nullopt, RT::LOCAL);
  c.onBestpathSourceChanged(/*isV4=*/false, std::nullopt, RT::ConfedEBGP);
  EXPECT_EQ(1, c.ebgpPrefixes(/*isV4=*/true));
  EXPECT_EQ(1, c.ibgpPrefixes(/*isV4=*/true));
  EXPECT_EQ(1, c.localPrefixes(/*isV4=*/true));
  EXPECT_EQ(1, c.confedEbgpPrefixes(/*isV4=*/false));
  // Per-AFI isolation: v4 confed and v6 ebgp untouched.
  EXPECT_EQ(0, c.confedEbgpPrefixes(/*isV4=*/true));
  EXPECT_EQ(0, c.ebgpPrefixes(/*isV4=*/false));

  // Flip eBGP -> iBGP moves one from ebgp to ibgp.
  c.onBestpathSourceChanged(/*isV4=*/true, RT::EBGP, RT::IBGP);
  EXPECT_EQ(0, c.ebgpPrefixes(/*isV4=*/true));
  EXPECT_EQ(2, c.ibgpPrefixes(/*isV4=*/true));

  // No-op when class is unchanged (full-RIB re-selection safety).
  c.onBestpathSourceChanged(/*isV4=*/true, RT::IBGP, RT::IBGP);
  EXPECT_EQ(2, c.ibgpPrefixes(/*isV4=*/true));

  // Withdrawal / unresolvable: iBGP -> no best path decrements.
  c.onBestpathSourceChanged(/*isV4=*/true, RT::IBGP, std::nullopt);
  EXPECT_EQ(1, c.ibgpPrefixes(/*isV4=*/true));

  /*
   * A winner classified UNKNOWN maps 1:1 to its own bucket, never folded into
   * iBGP (getBgpPathType does not emit UNKNOWN in practice; passed directly
   * here).
   */
  c.onBestpathSourceChanged(/*isV4=*/true, std::nullopt, RT::UNKNOWN);
  EXPECT_EQ(1, c.unknownPrefixes(/*isV4=*/true));
  EXPECT_EQ(0, c.ebgpPrefixes(/*isV4=*/true));
  EXPECT_EQ(1, c.ibgpPrefixes(/*isV4=*/true));
  EXPECT_EQ(1, c.localPrefixes(/*isV4=*/true));
}

/*
 * routesWithUnresolvedNexthops = total prefixes minus the prefixes counted in
 * the four best-path source buckets, i.e. prefixes left with no best path
 * because all their next-hops are unresolvable.
 */
TEST(RibCountersTest, RoutesWithUnresolvedNexthops) {
  RibStats::initCounters();
  RibCounters c;

  /*
   * Two v6 prefixes are in the RIB; only one wins a best path (iBGP). The other
   * has no best path (all next-hops unresolvable) -> in no source bucket.
   */
  c.onPrefixAdded(/*isV4=*/false, 64);
  c.onPrefixAdded(/*isV4=*/false, 64);
  c.onBestpathSourceChanged(/*isV4=*/false, std::nullopt, BgpRouteType::IBGP);
  EXPECT_EQ(1, c.routesWithUnresolvedNexthops(/*isV4=*/false));
  // v4 has no prefixes, so none are unresolved.
  EXPECT_EQ(0, c.routesWithUnresolvedNexthops(/*isV4=*/true));

  /*
   * Once the second prefix also resolves to a best path, none remain
   * unresolved.
   */
  c.onBestpathSourceChanged(/*isV4=*/false, std::nullopt, BgpRouteType::EBGP);
  EXPECT_EQ(0, c.routesWithUnresolvedNexthops(/*isV4=*/false));

  /*
   * An UNKNOWN-classified winner still has a best path, so the prefix is not
   * counted as unresolved (the unknown bucket is excluded from the remainder).
   */
  c.onPrefixAdded(/*isV4=*/false, 64);
  c.onBestpathSourceChanged(
      /*isV4=*/false, std::nullopt, BgpRouteType::UNKNOWN);
  EXPECT_EQ(0, c.routesWithUnresolvedNexthops(/*isV4=*/false));
}

// reset() zeroes the in-memory fields (used on the shutdown bulk-clear path).
TEST(RibCountersTest, ResetZeroesInMemoryFields) {
  RibStats::initCounters();
  RibCounters c;
  c.onPrefixAdded(/*isV4=*/true, 24);
  c.onPrefixAdded(/*isV4=*/false, 64);
  c.onPathsDelta(/*isV4=*/true, 3);
  c.onPathsDelta(/*isV4=*/false, 4);
  c.onInactivePathsDelta(/*isV4=*/true, 2);
  c.onInactivePathsDelta(/*isV4=*/false, 1);
  c.publishTotalPathCounters();
  c.publishInactivePathCounters();
  c.setOriginatedRoutes(5);
  c.incrementUnresolvableNexthopCount();
  c.onBestpathSourceChanged(
      /*isV4=*/true, std::nullopt, BgpRouteType::EBGP);

  c.reset();
  EXPECT_EQ(0, c.totalPrefixes());
  EXPECT_EQ(0, c.totalPrefixes(/*isV4=*/true));
  EXPECT_EQ(0, c.totalPaths());
  EXPECT_EQ(0, c.inactivePaths());
  EXPECT_EQ(0, c.inactivePaths(/*isV4=*/true));
  EXPECT_EQ(0, c.prefixLenCounts(/*isV4=*/false)[64]);
  EXPECT_EQ(0, c.originatedRoutes());
  EXPECT_EQ(0, c.unresolvableNexthops());
  EXPECT_EQ(0, c.ebgpPrefixes(/*isV4=*/true));
  EXPECT_EQ(0, counter(RibStats::kInactivePathsUnresolvedNexthopsCount));
  EXPECT_EQ(0, counter(RibStats::kInactivePathsUnresolvedNexthopsCountIpv4));
  EXPECT_EQ(0, counter(RibStats::kInactivePathsUnresolvedNexthopsCountIpv6));
  EXPECT_EQ(0, counter(RibStats::kInactivePathCount));
  EXPECT_EQ(0, counter(RibStats::kInactivePathCountIpv4));
  EXPECT_EQ(0, counter(RibStats::kInactivePathCountIpv6));
  EXPECT_EQ(0, counter(RibStats::kTotalPathsCount));
  EXPECT_EQ(0, counter(RibStats::kTotalPathsCountIpv4));
  EXPECT_EQ(0, counter(RibStats::kTotalPathsCountIpv6));
}

} // namespace facebook::bgp
