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

#include "neteng/fboss/bgp/cpp/tests/e2e/E2ETestUtils.h"

#include <folly/coro/Sleep.h>

#include "neteng/fboss/bgp/cpp/lib/BgpUtil.h"

namespace facebook {
namespace bgp {

// TestFib Implementation
TestFib::TestFib(FibMessageQueue& toRibQ) : toRibQ_(toRibQ) {}

std::optional<FibOutRoute> TestFib::updateUnicastRoute(
    const folly::CIDRNetwork& prefix,
    std::shared_ptr<const BgpPath> attrsToBeAdvertised,
    std::shared_ptr<const WeightedNexthopMap> weightedNexthops,
    const bool isLocalRouteBest,
    const bool installToFib,
    const folly::F14NodeMap<folly::IPAddress, NexthopInfo>& /*nextHopInfoMap*/,
    const std::optional<uint32_t>& /*classId*/,
    std::shared_ptr<const NexthopTopoInfoMap> /*nhtTopo*/,
    const BgpRouteType /*routeType*/,
    bool enableFibOutTracking) {
  staged_[attrsToBeAdvertised][prefix] = weightedNexthops;
  /*
   * Also store in programmedRoutes_ for test verification.
   * This persists across program() calls unlike staged_.
   */
  programmedRoutes_[prefix] = weightedNexthops;
  programCallCount_++;

  if (!weightedNexthops || weightedNexthops->empty() || !installToFib) {
    return enableFibOutTracking ? std::optional<FibOutRoute>{FibOutRoute{}}
                                : std::nullopt;
  }

  FibNexthopSet nexthops;
  if (enableFibOutTracking && !isLocalRouteBest) {
    nexthops.reserve(weightedNexthops->size());
    for (const auto& [nexthop, weight] : *weightedNexthops) {
      nexthops.push_back(
          FibOutNexthop{
              .address = nexthop,
              .weight = weight,
              .role = FibOutNexthopRole::PRIMARY,
          });
    }
  }
  return enableFibOutTracking ? std::optional<FibOutRoute>{FibOutRoute{
                                    .nexthops = std::move(nexthops),
                                    .metadata = FibOutRouteMetadata::program(),
                                }}
                              : std::nullopt;
}

bool TestFib::isConnected() const {
  return connected_;
}

bool TestFib::isFullSynced() const {
  return fullSynced_;
}

folly::coro::Task<void> TestFib::program(bool isSync, VrfId vrfId) {
  if (programDelay_.count() > 0) {
    co_await folly::coro::sleep(programDelay_);
  }
  if (failNextProgram_) {
    failNextProgram_ = false;
    failedProgramCount_++;
    XLOGF(INFO, "[TestFib] program() FAILED (simulated), isSync={}", isSync);
    staged_.clear();
    co_return;
  }
  fullSynced_ |= isSync;
  XLOGF(INFO, "[TestFib] program() called, isSync={}", isSync);
  toRibQ_.push(Fib::FibProgrammedMessage(vrfId, staged_, isSync));
  staged_.clear();
  co_return;
}

void TestFib::stop() {}

// Helper Functions
std::shared_ptr<BgpPath> buildBgpPath(const folly::IPAddress& nexthop) {
  auto attrs = std::make_shared<BgpPath>(
      BgpPathFields(*facebook::nettools::bgplib::BgpUpdate2toBgpPathC(
          facebook::bgp::buildBgpUpdateAttributes(nexthop))));
  attrs->publish();
  return attrs;
}

} // namespace bgp
} // namespace facebook
