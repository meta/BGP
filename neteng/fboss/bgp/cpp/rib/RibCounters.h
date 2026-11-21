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

#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "neteng/fboss/bgp/cpp/common/Types.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

namespace facebook::bgp {

/**
 * Single authoritative in-memory aggregate of RIB-wide counts.
 *
 * Owned by each RibVrf and mutated only on the RIB EventBase, so it needs no
 * locking. Each prefix/nexthop mutator updates the in-memory field(s) AND
 * mirrors the matching fb303 ODS counter (via RibStats) in one place, so
 * callers no longer sprinkle RibStats increment/decrement calls across the RIB
 * and existing dashboards / HealthValidator keep observing identical fb303
 * values.
 *
 * Counts that the fb303 layer does not (and cannot cheaply) provide -- the
 * per-AFI prefix split and the per-prefix-length counts -- are maintained
 * here as in-memory state for the `getRibSummary` thrift RPC. This is the
 * single home for RIB count maintenance: future summary dimensions (best-path
 * source breakdown, paths/ECMP, ...) are added here rather than as parallel
 * ad-hoc counters.
 */
class RibCounters {
 public:
  /*
   * publishToFb303 controls whether mutators mirror counts to fb303. Only one
   * RibCounters should publish, because the path gauges are set to its own
   * totals rather than incremented.
   */
  explicit RibCounters(bool publishToFb303 = true)
      : publishToFb303_(publishToFb303) {}

  /**
   * IPv6 carries the longest prefixes; IPv4 lengths (0..32) index the same
   * table. Counts are indexed directly by prefix length [0, kMaxPrefixLen].
   */
  static constexpr uint8_t kMaxPrefixLen = 128;

  /** A prefix of the given family/length was inserted into the Loc-RIB. */
  void onPrefixAdded(bool isV4, uint8_t prefixLen) {
    auto& a = afiOf(isV4);
    ++a.totalPrefixes;
    ++a.prefixLenCounts[bucket(prefixLen)];
    if (publishToFb303_) {
      RibStats::incrRibPrefixCount();
    }
  }

  /**
   * A prefix of the given family/length was removed from the Loc-RIB.
   *
   * `inactivePaths` is the entry's last cached contribution to the aggregate.
   * Releasing it here makes prefix removal one bookkeeping operation: an erase
   * site cannot decrement the prefix count while accidentally stranding the
   * entry's inactive paths.
   */
  void onPrefixRemoved(bool isV4, uint8_t prefixLen, uint32_t inactivePaths) {
    auto& a = afiOf(isV4);
    --a.totalPrefixes;
    --a.prefixLenCounts[bucket(prefixLen)];
    if (publishToFb303_) {
      RibStats::decrRibPrefixCount();
    }
    onInactivePathsDelta(isV4, -static_cast<int64_t>(inactivePaths));
  }

  /**
   * The number of paths held for one prefix changed by `delta` (positive when
   * paths are announced, negative when withdrawn). Driven from the single
   * RIB-in choke point as the change in that prefix's total path count, so it
   * is add-path-correct: one peer advertising several add-path IDs for a prefix
   * contributes one per ID. A no-op when `delta` is 0.
   */
  void onPathsDelta(bool isV4, int64_t delta) {
    if (delta == 0) {
      return;
    }
    afiOf(isV4).totalPaths += delta;
  }

  /**
   * The number of paths excluded from best-path selection for one prefix
   * changed by `delta`. Driven from runBestPathSelection as the change in that
   * prefix's cached count (recomputed by prePathSelectionFiltering, which is
   * where the exclusion decision is made), and from the prefix-erase sites as
   * the removal of the whole cached count.
   *
   * A no-op when `delta` is 0 -- the common case, since most prefixes are
   * re-selected without any change in eligibility. The fb303 mirrors are set
   * absolutely by publishInactivePathCounters rather than incremented, so they
   * cannot drift.
   */
  void onInactivePathsDelta(bool isV4, int64_t delta) {
    if (delta == 0) {
      return;
    }
    auto& afi = afiOf(isV4);
    afi.inactivePaths += delta;
    publishInactivePathCounters();
  }

  /*
   * Mirror the aggregate and per-family total path gauges to fb303 once per
   * inbound route batch rather than once per path. This publication is
   * independent of FIB programming, which may be paused.
   */
  void publishTotalPathCounters() const {
    if (!publishToFb303_) {
      return;
    }
    RibStats::setTotalPathsCount(static_cast<int64_t>(totalPaths()));
    RibStats::setTotalPathCountForAfi(
        /*isV4=*/true, static_cast<int64_t>(totalPaths(/*isV4=*/true)));
    RibStats::setTotalPathCountForAfi(
        /*isV4=*/false, static_cast<int64_t>(totalPaths(/*isV4=*/false)));
  }

  /*
   * Mirror the aggregate and per-family inactive path gauges together, so the
   * two families always sum to the aggregate.
   */
  void publishInactivePathCounters() const {
    if (!publishToFb303_) {
      return;
    }
    RibStats::setInactivePathCount(inactivePaths());
    RibStats::setInactivePathCountForAfi(
        /*isV4=*/true, inactivePaths(/*isV4=*/true));
    RibStats::setInactivePathCountForAfi(
        /*isV4=*/false, inactivePaths(/*isV4=*/false));
  }

  /** The set of locally-originated routes was (re)computed to `count`. */
  void setOriginatedRoutes(uint64_t count) {
    originatedRoutes_ = count;
    if (publishToFb303_) {
      RibStats::setOriginatedRoutesSize(count);
    }
  }

  void incrementUnresolvableNexthopCount() {
    ++unresolvableNexthops_;
    if (publishToFb303_) {
      RibStats::incrUnresolvableNexthopsCount();
    }
  }

  void decrementUnresolvableNexthopCount() {
    --unresolvableNexthops_;
    if (publishToFb303_) {
      RibStats::decrUnresolvableNexthopsCount();
    }
  }

  /**
   * The selected best path for a prefix changed source class (including
   * appearing from / disappearing to std::nullopt, i.e. no best path). No-op
   * when the class is unchanged, so a full-RIB re-selection that leaves each
   * winner's class the same nets to zero -- callers may invoke this on every
   * path-selection pass. A null best path is carried as std::nullopt rather
   * than a sentinel enum value, so it contributes to no source bucket.
   */
  void onBestpathSourceChanged(
      bool isV4,
      std::optional<BgpRouteType> oldSrc,
      std::optional<BgpRouteType> newSrc) {
    if (oldSrc == newSrc) {
      return;
    }
    auto& a = afiOf(isV4);
    adjustSource(a, oldSrc, -1);
    adjustSource(a, newSrc, +1);
  }

  /**
   * Zero the in-memory counts. Called on the RIB EventBase when the Loc-RIB is
   * bulk-cleared during controlled shutdown. Clear the path gauges as part of
   * the same operation so they cannot retain values from the pre-clear RIB.
   */
  void reset() {
    afis_ = {};
    originatedRoutes_ = 0;
    unresolvableNexthops_ = 0;
    publishInactivePathCounters();
    publishTotalPathCounters();
  }

  /** Prefix count for one address family (true = IPv4, false = IPv6). */
  uint64_t totalPrefixes(bool isV4) const {
    return afiOf(isV4).totalPrefixes;
  }

  /** Path count for one address family (add-path-correct; see onPathsDelta). */
  uint64_t totalPaths(bool isV4) const {
    return afiOf(isV4).totalPaths;
  }

  /**
   * Paths excluded from best-path selection, for one address family. Always a
   * subset of totalPaths(isV4): the remainder are the candidates that entered
   * selection.
   */
  int64_t inactivePaths(bool isV4) const {
    return afiOf(isV4).inactivePaths;
  }

  /**
   * Per-prefix-length counts for one address family, indexed by prefix
   * length. Index i holds the number of /i prefixes in that family.
   */
  const std::array<int64_t, kMaxPrefixLen + 1>& prefixLenCounts(
      bool isV4) const {
    return afiOf(isV4).prefixLenCounts;
  }

  /** Best-path counts for one address family by selected best path's source. */
  int64_t ebgpPrefixes(bool isV4) const {
    return afiOf(isV4).ebgp;
  }
  int64_t ibgpPrefixes(bool isV4) const {
    return afiOf(isV4).ibgp;
  }
  int64_t confedEbgpPrefixes(bool isV4) const {
    return afiOf(isV4).confedEbgp;
  }
  int64_t localPrefixes(bool isV4) const {
    return afiOf(isV4).local;
  }
  int64_t unknownPrefixes(bool isV4) const {
    return afiOf(isV4).unknown;
  }

  /**
   * Routes (prefixes) in one address family that have no best path because all
   * their next-hops are unresolvable. Derived: total prefixes minus the
   * prefixes counted in the best-path source buckets (eBGP / iBGP / confed-eBGP
   * / local / unknown). A prefix with a best path is always counted in exactly
   * one of those buckets, so the remainder is precisely the prefixes left with
   * no best path. Clamped at 0 defensively.
   */
  int64_t routesWithUnresolvedNexthops(bool isV4) const {
    const auto& a = afiOf(isV4);
    const int64_t withBestpath =
        a.ebgp + a.ibgp + a.confedEbgp + a.local + a.unknown;
    const int64_t total = static_cast<int64_t>(a.totalPrefixes);
    return total > withBestpath ? total - withBestpath : 0;
  }

  /** Prefix count across both address families. */
  uint64_t totalPrefixes() const {
    return afis_[0].totalPrefixes + afis_[1].totalPrefixes;
  }

  /** Path count across both address families (add-path-correct). */
  uint64_t totalPaths() const {
    return afis_[0].totalPaths + afis_[1].totalPaths;
  }

  /** Paths excluded from best-path selection across both address families. */
  int64_t inactivePaths() const {
    return afis_[0].inactivePaths + afis_[1].inactivePaths;
  }
  uint64_t originatedRoutes() const {
    return originatedRoutes_;
  }
  uint64_t unresolvableNexthops() const {
    return unresolvableNexthops_;
  }

 private:
  struct AfiCounters {
    uint64_t totalPrefixes{0};
    uint64_t totalPaths{0};
    // Subset of totalPaths that best-path selection excluded as candidates.
    int64_t inactivePaths{0};
    std::array<int64_t, kMaxPrefixLen + 1> prefixLenCounts{};
    /*
     * Best-path source breakdown, by the selected best path's class. The
     * `unknown` bucket maps 1:1 to BgpRouteType::UNKNOWN. getBgpPathType does
     * not emit UNKNOWN today (its residual case is IBGP, the internal-peer
     * class), so this bucket stays 0 in practice; it exists only so an UNKNOWN
     * winner would land here rather than be folded into another source bucket.
     */
    int64_t ebgp{0};
    int64_t ibgp{0};
    int64_t confedEbgp{0};
    int64_t local{0};
    int64_t unknown{0};
  };

  // Clamp defensively; RIB prefix lengths are already within range.
  static uint8_t bucket(uint8_t prefixLen) {
    return prefixLen > kMaxPrefixLen ? kMaxPrefixLen : prefixLen;
  }

  static void
  adjustSource(AfiCounters& a, std::optional<BgpRouteType> src, int64_t delta) {
    if (!src) {
      // No best path: not attributed to any source bucket.
      return;
    }
    switch (*src) {
      case BgpRouteType::EBGP:
        a.ebgp += delta;
        break;
      case BgpRouteType::IBGP:
        a.ibgp += delta;
        break;
      case BgpRouteType::ConfedEBGP:
        a.confedEbgp += delta;
        break;
      case BgpRouteType::LOCAL:
        a.local += delta;
        break;
      case BgpRouteType::UNKNOWN:
        /*
         * 1:1 with the enum. getBgpPathType does not return UNKNOWN today (its
         * residual case is IBGP), so this is reached only by direct callers; it
         * keeps any UNKNOWN winner out of the other source buckets.
         */
        a.unknown += delta;
        break;
    }
  }
  AfiCounters& afiOf(bool isV4) {
    return afis_[isV4 ? 0 : 1];
  }
  const AfiCounters& afiOf(bool isV4) const {
    return afis_[isV4 ? 0 : 1];
  }

  const bool publishToFb303_;
  // [0] = IPv4, [1] = IPv6.
  std::array<AfiCounters, 2> afis_{};
  uint64_t originatedRoutes_{0};
  uint64_t unresolvableNexthops_{0};
};

} // namespace facebook::bgp
