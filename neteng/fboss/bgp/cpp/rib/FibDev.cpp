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

#include <folly/logging/xlog.h>

#include <neteng/fboss/bgp/cpp/rib/FibDev.h>

namespace facebook::bgp {

FibDev::FibDev(FibMessageQueue& toRibQ) : toRibQ_(toRibQ) {
  waitForAck_ = std::make_unique<FibProgrammedPfxs>();
}

FibDev::~FibDev() = default;

std::unique_ptr<FibDev> FibDev::createFibDev(Fib::FibMessageQueue& toRibQ) {
  return std::unique_ptr<FibDev>(new FibDev(toRibQ));
}

void FibDev::stop() {
  XLOG(INFO, "Signal FibDev fibers to stop ...");
  fullSynced_ = false;
  waitForAck_.reset();
}

std::optional<FibOutRoute> FibDev::updateUnicastRoute(
    const folly::CIDRNetwork& prefix,
    std::shared_ptr<const BgpPath> attrsToBeAdvertised,
    std::shared_ptr<const WeightedNexthopMap> weightedNexthops,
    const bool isLocalRouteBest,
    const bool installToFib,
    const folly::F14NodeMap<folly::IPAddress, facebook::bgp::NexthopInfo>&,
    const std::optional<uint32_t>& /* classId */,
    std::shared_ptr<const NexthopTopoInfoMap> /* nexthopTopoInfoMap */,
    const BgpRouteType,
    bool enableFibOutTracking) {
  if (!weightedNexthops || weightedNexthops->empty() ||
      (isLocalRouteBest && !installToFib)) {
    XLOGF(
        DBG3,
        "Deleting unicast prefix {}",
        folly::IPAddress::networkToString(prefix));
    (*waitForAck_)[attrsToBeAdvertised][prefix] = std::move(weightedNexthops);
    return enableFibOutTracking ? std::optional<FibOutRoute>{FibOutRoute{}}
                                : std::nullopt;
  }

  if (isLocalRouteBest) {
    XLOGF(
        DBG1,
        "Local route programming with empty nexthop for prefix {}",
        folly::IPAddress::networkToString(prefix));
  }
  XLOGF(
      DBG3,
      "Adding unicast prefix {} with {} nexthops",
      folly::IPAddress::networkToString(prefix),
      isLocalRouteBest ? 0 : weightedNexthops->size());

  FibNexthopSet fibOutNextHops;
  if (enableFibOutTracking && !isLocalRouteBest) {
    fibOutNextHops.reserve(weightedNexthops->size());
    for (const auto& [nextHop, weight] : *weightedNexthops) {
      fibOutNextHops.push_back(
          FibOutNexthop{
              .address = nextHop,
              .weight = weight,
              .role = FibOutNexthopRole::PRIMARY,
          });
    }
  }
  (*waitForAck_)[attrsToBeAdvertised][prefix] = std::move(weightedNexthops);
  return enableFibOutTracking ? std::optional<FibOutRoute>{FibOutRoute{
                                    .nexthops = std::move(fibOutNextHops),
                                    .metadata = FibOutRouteMetadata::program(),
                                }}
                              : std::nullopt;
}

folly::coro::Task<void> FibDev::program(bool isSync, VrfId vrfId) {
  auto waitForAck = std::move(waitForAck_);
  waitForAck_ = std::make_unique<FibProgrammedPfxs>();

  // populate one-time flag to mark initial FIB synced
  if (isSync && (!initialFibSynced_)) {
    initialFibSynced_ = true;

    // log BGP++ initialization event
    BgpStats::logInitializationEvent(
        "FibFboss",
        neteng::fboss::bgp::thrift::BgpInitializationEvent::FIB_SYNCED);
  }

  // update fullSynced_ flag
  fullSynced_ |= isSync;

  // notify back to Rib that all prefixes have been installed in HW
  toRibQ_.push(FibProgrammedMessage(vrfId, std::move(*waitForAck), isSync));

  co_return;
}

} // namespace facebook::bgp
