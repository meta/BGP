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

#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <folly/Benchmark.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Task.h>
#include <folly/init/Init.h>
#include <folly/io/async/EventBase.h>

#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <neteng/fboss/bgp/cpp/common/BgpPath.h>
#include <neteng/fboss/bgp/cpp/common/RibMessage.h>
#include <neteng/fboss/bgp/cpp/lib/BgpStructs.h>
#include <neteng/fboss/bgp/cpp/rib/canonical/CanonicalRibBuilder.h>
#include <neteng/fboss/bgp/cpp/tests/RibUtils.h>
#include <neteng/fboss/bgp/cpp/tests/Utils.h>
#include "fboss/agent/Constants.h"

/*
 * Micro-benchmark: RibBase::getRibEntries (legacy std::vector<TRibEntry>) vs
 * RibBase::getRibEntriesCanonical (deduplicated TCanonicalRibState). Compares
 * export CPU (folly Benchmark) and serialized wire size (Compact protocol)
 * across RIB shapes with different attribute sharing.
 *
 * Caveat: the synthetic RIB is populated through the announcement pipeline,
 * which does not run best-path selection, so every received path lands in the
 * canonical/legacy default group. This is immaterial to the comparison -- both
 * export costs are driven by path count and attribute deduplication, not by
 * which group a path is filed under -- and keeps the benchmark
 * dependency-light.
 */

using namespace facebook::bgp;
using namespace facebook::neteng::fboss::bgp_attr;
using namespace facebook::nettools::bgplib;

enum class AttrMode {
  SHARED, // one attrs shared by every path -> 1 deduped path total
  SUBATTR, // distinct next_hop per peer, shared AS_PATH/community sub-attrs
  DISTINCT, // unique attrs per (prefix, peer) -> no dedup (worst case)
};

namespace {

const char* modeName(AttrMode mode) {
  switch (mode) {
    case AttrMode::SHARED:
      return "SHARED";
    case AttrMode::SUBATTR:
      return "SUBATTR";
    case AttrMode::DISTINCT:
      return "DISTINCT";
  }
  return "?";
}

} // namespace

class GetRibCanonicalBenchmarkFixture {
 public:
  GetRibCanonicalBenchmarkFixture() {
    /* Each fixture is a fresh MockRib starting empty. */
    BgpGlobalConfig bgpGlobalConfig(
        kAsn1,
        kLocalAddr1,
        kPeerAddr3,
        kHoldTime,
        std::nullopt,
        kGrRestartTime,
        {},
        {});

    rib_ = std::make_unique<MockRib>(
        std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>(),
        bgpGlobalConfig,
        std::nullopt,
        ribInQ_,
        ribOutQ_,
        kDevPlatform,
        nullptr);

    ribThread_ = std::thread([this]() { rib_->run(); });
    try {
      rib_->getEventBase().waitUntilRunning();
    } catch (...) {
      /*
       * The constructor did not finish, so the destructor will not run. Stop
       * the RIB and join here so the joinable std::thread is not destroyed
       * (which would std::terminate). stop() is swallowed for the same reason.
       */
      try {
        rib_->stop();
      } catch (...) {
      }
      if (ribThread_.joinable()) {
        ribThread_.join();
      }
      throw;
    }
  }

  ~GetRibCanonicalBenchmarkFixture() {
    try {
      if (rib_) {
        rib_->stop();
      }
    } catch (...) {
      /*
       * Swallow so the RIB thread is still joined below -- a throw escaping the
       * destructor would std::terminate before the join runs.
       */
    }
    if (ribThread_.joinable()) {
      ribThread_.join();
    }
  }

  static void clearAllDeduplicators() {
    /*
     * The canonical converter interns by object ADDRESS into its own per-build
     * pools and never consults the global deduplicator, so clearing between
     * fixtures is safe for the export under test.
     */
    DeDuplicatedBgpPath::clearDeduplicator();
    DeDuplicatedBgpAttributesC::clearDeduplicator();
    DeDuplicatedAsPath::clearDeduplicator();
    DeDuplicatedCommunities::clearDeduplicator();
    DeDuplicatedExtCommunities::clearDeduplicator();
    DeDuplicatedClusterList::clearDeduplicator();
  }

  std::shared_ptr<const BgpPath> makePath(
      uint32_t asCount,
      uint32_t commCount,
      const folly::IPAddress& nexthop) {
    auto fields = buildBgpPathFields(asCount, commCount, 0, 0, 0, nexthop);
    if (!fields) {
      throw std::runtime_error("buildBgpPathFields returned null");
    }
    auto path = std::make_shared<BgpPath>(*fields);
    path->publish();
    return DeDuplicatedBgpPath(path).getSharedPtr();
  }

  void populate(size_t numPrefixes, size_t pathsPerPrefix, AttrMode mode) {
    clearAllDeduplicators();
    if (pathsPerPrefix != 0 &&
        numPrefixes > std::numeric_limits<size_t>::max() / pathsPerPrefix) {
      throw std::overflow_error("populate() path count overflows size_t");
    }
    const auto expectedPaths = numPrefixes * pathsPerPrefix;
    /*
     * Bound the convergence poll below so a stuck RIB thread fails observably
     * instead of hanging the benchmark indefinitely.
     */
    constexpr std::chrono::seconds kPopulateTimeout{300};

    std::vector<TinyPeerInfo> peers;
    peers.reserve(pathsPerPrefix);
    for (size_t j = 0; j < pathsPerPrefix; ++j) {
      peers.emplace_back(
          folly::IPAddress::fromLong(static_cast<uint32_t>(0x0a000000 + j)),
          kLocalRouteAs,
          0,
          BgpSessionType::IBGP,
          false);
    }

    /*
     * Feed announcements through the coro push(), which applies the queue's
     * real back pressure -- it suspends when the bounded queue is full and
     * resumes as the RIB drains. Driven from this setup thread via a single
     * blockingWait, so no fiber Baton is waited on from a non-fiber context
     * (unlike fiberPush()).
     */
    folly::coro::blockingWait([&]() -> folly::coro::Task<void> {
      for (size_t i = 0; i < numPrefixes; ++i) {
        /* Put i in the network portion of the /24 to avoid host-bit collisions
         */
        auto prefix = folly::CIDRNetwork(
            folly::IPAddress::fromLong(
                static_cast<uint32_t>(0x01000000 + (i << 8))),
            24);

        for (size_t j = 0; j < pathsPerPrefix; ++j) {
          std::shared_ptr<const BgpPath> path;
          switch (mode) {
            case AttrMode::SHARED:
              path = makePath(
                  4,
                  4,
                  folly::IPAddress::fromLong(
                      static_cast<uint32_t>(0x0b000000)));
              break;
            case AttrMode::SUBATTR:
              path = makePath(
                  4,
                  4,
                  folly::IPAddress::fromLong(
                      static_cast<uint32_t>(0x0b000000 + j)));
              break;
            case AttrMode::DISTINCT:
              path = makePath(
                  4,
                  4,
                  folly::IPAddress::fromLong(
                      static_cast<uint32_t>(
                          0x0b000000 + i * pathsPerPrefix + j)));
              break;
          }

          PrefixPathIds pfxPathIds;
          pfxPathIds.emplace_back(prefix, kDefaultPathID);
          co_await ribInQ_.push(RibInAnnouncement(
              facebook::fboss::kDefaultVrf,
              peers[j],
              std::move(pfxPathIds),
              path));
        }
      }
    }());

    /*
     * Wait for the RIB to actually apply the entries, not just dequeue them.
     * Yield between checks (a baton would need RIB cooperation, out of scope
     * for the bench). The deadline turns a stuck RIB thread into an observable
     * failure instead of an indefinite hang.
     */
    const auto convergeDeadline =
        std::chrono::steady_clock::now() + kPopulateTimeout;
    while (true) {
      const auto entries = rib_->getRibEntries(TBgpAfi::AFI_IPV4);
      size_t appliedPaths = 0;
      for (const auto& entry : entries) {
        for (const auto& groupPaths : entry.paths().value()) {
          appliedPaths += groupPaths.second.size();
        }
      }
      if (entries.size() == numPrefixes && appliedPaths == expectedPaths) {
        break;
      }
      if (std::chrono::steady_clock::now() > convergeDeadline) {
        throw std::runtime_error(
            "populate() timed out waiting for RIB to converge");
      }
      std::this_thread::yield();
    }
  }

  std::unique_ptr<MockRib> rib_;
  std::thread ribThread_;
  MonitoredBackPressuredQueue<RibInMessage> ribInQ_{kMaxIngressQueueSize};
  MonitoredMPMCQueue<RibOutMessage> ribOutQ_;
};

namespace {

struct BenchConfig {
  size_t n;
  size_t m;
  AttrMode mode;
};

const std::vector<BenchConfig>& benchConfigs() {
  static const std::vector<BenchConfig> kConfigs = {
      {5000, 1, AttrMode::SUBATTR},
      {5000, 8, AttrMode::SUBATTR},
      {5000, 8, AttrMode::SHARED},
      {5000, 8, AttrMode::DISTINCT},
      {20000, 16, AttrMode::SUBATTR},
  };
  return kConfigs;
}

std::string keyOf(size_t n, size_t m, AttrMode mode) {
  return std::to_string(n) + "_" + std::to_string(m) + "_" +
      std::to_string(static_cast<int>(mode));
}

/*
 * Announcement population is far slower than the export under test, and folly
 * re-invokes each benchmark function during calibration -- so each RIB shape is
 * built exactly once (in main) and the timed loops below reuse it.
 */
std::map<std::string, std::unique_ptr<GetRibCanonicalBenchmarkFixture>>&
fixtureRegistry() {
  static std::map<std::string, std::unique_ptr<GetRibCanonicalBenchmarkFixture>>
      registry;
  return registry;
}

GetRibCanonicalBenchmarkFixture& fixtureFor(size_t n, size_t m, AttrMode mode) {
  return *fixtureRegistry().at(keyOf(n, m, mode));
}

} // namespace

void BM_GetRibEntries(uint32_t iters, size_t n, size_t m, AttrMode mode) {
  auto& f = fixtureFor(n, m, mode);
  while (iters--) {
    auto out = f.rib_->getRibEntries(TBgpAfi::AFI_IPV4);
    folly::doNotOptimizeAway(out);
  }
}

void BM_GetRibEntriesCanonical(
    uint32_t iters,
    size_t n,
    size_t m,
    AttrMode mode) {
  auto& f = fixtureFor(n, m, mode);
  while (iters--) {
    auto out = f.rib_->getRibEntriesCanonical(TBgpAfi::AFI_IPV4);
    folly::doNotOptimizeAway(out);
  }
}

BENCHMARK_NAMED_PARAM(
    BM_GetRibEntries,
    N5000_M1_SUBATTR,
    5000,
    1,
    AttrMode::SUBATTR);
BENCHMARK_RELATIVE_NAMED_PARAM(
    BM_GetRibEntriesCanonical,
    N5000_M1_SUBATTR,
    5000,
    1,
    AttrMode::SUBATTR);

BENCHMARK_DRAW_LINE();

BENCHMARK_NAMED_PARAM(
    BM_GetRibEntries,
    N5000_M8_SUBATTR,
    5000,
    8,
    AttrMode::SUBATTR);
BENCHMARK_RELATIVE_NAMED_PARAM(
    BM_GetRibEntriesCanonical,
    N5000_M8_SUBATTR,
    5000,
    8,
    AttrMode::SUBATTR);

BENCHMARK_DRAW_LINE();

BENCHMARK_NAMED_PARAM(
    BM_GetRibEntries,
    N5000_M8_SHARED,
    5000,
    8,
    AttrMode::SHARED);
BENCHMARK_RELATIVE_NAMED_PARAM(
    BM_GetRibEntriesCanonical,
    N5000_M8_SHARED,
    5000,
    8,
    AttrMode::SHARED);

BENCHMARK_DRAW_LINE();

BENCHMARK_NAMED_PARAM(
    BM_GetRibEntries,
    N5000_M8_DISTINCT,
    5000,
    8,
    AttrMode::DISTINCT);
BENCHMARK_RELATIVE_NAMED_PARAM(
    BM_GetRibEntriesCanonical,
    N5000_M8_DISTINCT,
    5000,
    8,
    AttrMode::DISTINCT);

BENCHMARK_DRAW_LINE();

BENCHMARK_NAMED_PARAM(
    BM_GetRibEntries,
    N20000_M16_SUBATTR,
    20000,
    16,
    AttrMode::SUBATTR);
BENCHMARK_RELATIVE_NAMED_PARAM(
    BM_GetRibEntriesCanonical,
    N20000_M16_SUBATTR,
    20000,
    16,
    AttrMode::SUBATTR);

namespace {

void reportWireSizes() {
  for (const auto& cfg : benchConfigs()) {
    auto& f = fixtureFor(cfg.n, cfg.m, cfg.mode);

    auto legacy = f.rib_->getRibEntries(TBgpAfi::AFI_IPV4);
    auto canonical = f.rib_->getRibEntriesCanonical(TBgpAfi::AFI_IPV4);

    std::string legacySer, canonicalSer;
    apache::thrift::CompactSerializer::serialize(legacy, &legacySer);
    apache::thrift::CompactSerializer::serialize(canonical, &canonicalSer);

    double ratio = static_cast<double>(legacySer.size()) /
        static_cast<double>(canonicalSer.size());

    std::fprintf(
        stderr,
        "[wire] N=%zu M=%zu %s: legacy=%zu canonical=%zu ratio=%.2fx  (paths=%zu, dedupedPaths=%zu)\n",
        cfg.n,
        cfg.m,
        modeName(cfg.mode),
        legacySer.size(),
        canonicalSer.size(),
        ratio,
        cfg.n * cfg.m,
        canonical.deduped_paths()->size());
  }
}

} // namespace

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);

  // Build each RIB shape once; the benchmarks and wire-size report reuse them.
  for (const auto& cfg : benchConfigs()) {
    auto f = std::make_unique<GetRibCanonicalBenchmarkFixture>();
    f->populate(cfg.n, cfg.m, cfg.mode);
    fixtureRegistry()[keyOf(cfg.n, cfg.m, cfg.mode)] = std::move(f);
  }

  reportWireSizes();
  folly::runBenchmarks();

  // Stop the rib threads before exit.
  fixtureRegistry().clear();
  return 0;
}
