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

#include "neteng/fboss/bgp/cpp/rib/RibVrf.h"

#include <folly/logging/xlog.h>

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/BgpProfiler.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/Utils.h"
#include "neteng/fboss/bgp/cpp/rib/RibBase.h"
#include "neteng/fboss/bgp/cpp/rib/Utils.h"

namespace facebook::bgp {

RibVrf::RibVrf(VrfId vrfId, RibBase& owner)
    : vrfId_(vrfId),
      owner_(owner),
      /*
       * Only the default VRF publishes its counts to fb303.
       * TODO_CTR_VRF: Publish the counts of every VRF.
       */
      ribCounters_(vrfId == fboss::kDefaultVrf) {
  /*
   * initialize fibBatchTimer_ for batch processing of fib programming
   * requests
   */
  fibBatchTimer_ = folly::AsyncTimeout::make(
      owner_.evb_, [this]() noexcept { owner_.prepareFibProgramming(vrfId_); });
  // bounds the deferred initial full-sync's wait for nexthop resolution
  nexthopResolutionTimer_ = folly::AsyncTimeout::make(
      owner_.evb_,
      [this]() noexcept { owner_.onNexthopResolutionTimeout(vrfId_); });
}

void RibVrf::addToFibBatch(RibEntry& entry) noexcept {
  if (!entry.isOnFibBatchList()) {
    fibBatchList_.push_back(entry);
  }
}

void RibVrf::replaceFibOut(RibEntry& entry, FibOutState state) {
  DCHECK(owner_.isFibOutTrackingEnabled());
  CHECK(state.nexthops);
  entry.fibOutState_ = std::make_shared<const FibOutState>(std::move(state));
}

void RibVrf::eraseFibOut(RibEntry& entry) {
  entry.fibOutState_.reset();
  entry.markFibOutStateAbsent();
}

const FibOutState* RibVrf::findFibOut(const RibEntry& entry) const {
  return entry.fibOutState_.get();
}

void RibVrf::clear() {
  XLOGF(
      INFO,
      "[Exit] Clearing ribEntries_ for VRF {} ({} entries)...",
      vrfId_,
      ribEntries_.size());
  ribEntries_.clear();
  fibNexthopSets_.clear();
  ribCounters_.reset();
  localRoutes_.clear();
  conditionalLocalRoutes_.clear();
}

void RibVrf::remove(RibEntry& entry) {
  eraseFibOut(entry);
  // The callback runs only if the entry is erased, before it is destroyed.
  ribEntries_.eraseInto(
      entry.getPrefix(),
      [this](const folly::CIDRNetwork& prefix, const RibEntry& erased) {
        ribCounters_.onPrefixRemoved(
            prefix.first.isV4(), prefix.second, erased.getInactivePathCnt());
      });
}

RibVrf::AggregateRoutes RibVrf::processSingleRibInUpdate(
    const TinyPeerInfo& peer,
    std::shared_ptr<const BgpPath> attrs,
    const PrefixPathId& pfxPid,
    NexthopInfo* FOLLY_NULLABLE nexthopInfo,
    RibEntry::UnlinkRouteInfoFn unlinkRouteInfoFn) noexcept {
  auto prefix = get<0>(pfxPid);
  auto receivedPathId = get<1>(pfxPid);

  /*
   * ** NOTE **
   * ribEntries_ are managed in two places asynchronously:
   *   1. here
   *   2. In handleFibProgrammedMessage(): erase prefix after withdrawal is
   *   programmed
   *
   * It's likely a ribEntry still exists with getAllPathsCnt() == 0.
   * This should be considered as if the prefix does not exist before.
   */

  auto& ribEntries = ribEntries_;
  // check whether prefix exists in ribEntries_
  auto kv = ribEntries.find(prefix);
  if (kv == ribEntries.cend()) {
    // withdraw route of a prefix that does not exist, nothing to do
    if (!attrs) {
      return {};
    }
    // add prefix to ribEntries_
    kv = ribEntries.emplace(std::make_pair(prefix, RibEntry(prefix))).first;
    ribCounters_.onPrefixAdded(prefix.first.isV4(), prefix.second);
  }
  auto& entry = kv->second;
  /*
   * oldAllPathCnt is used in aggregate route logic to indentify
   * prefix announcement/withdrawal (to distinguish with ecmp change)
   */
  auto oldAllPathCnt = entry.getAllPathsCnt();

  if (!attrs && oldAllPathCnt == 0) {
    /*
     * A previous withdrawn has been processed, but has not been programmed yet.
     * this is equal to withdraw route of a prefix that does not exist.
     * nothing more to do here.
     */
    return {};
  }

  bool isAggRoute{false}, installToFib{(!peer.isRedistributePeer)};
  if (peer == kV4LocalPeerInfo) {
    const auto& lr = localRoutes_.find(prefix);
    if (lr != localRoutes_.cend()) {
      installToFib = getInstallToFib(lr->second.network);
      isAggRoute = (getMinSupportRoutes(lr->second.network) > 0);
    }
  }

  // update new path to the entry.
  entry.updatePath(
      peer,
      attrs,
      installToFib,
      receivedPathId,
      nexthopInfo ? std::make_optional(nexthopInfo) : std::nullopt,
      unlinkRouteInfoFn);

  /*
   * Get new path counts now. entry will be gone out of scope if
   * removed under (!attr) condition. Be careful, do not access
   * entry reference after this call
   */
  auto newAllPathCnt = entry.getAllPathsCnt();

  /*
   * Single maintenance point for the per-AFI total path count: the change in
   * this prefix's path count is add-path-correct here, and aggregate routes
   * also funnel through this method.
   */
  ribCounters_.onPathsDelta(
      prefix.first.isV4(),
      static_cast<int64_t>(newAllPathCnt) -
          static_cast<int64_t>(oldAllPathCnt));

  if (!attrs) {
    checkWithdrawalBeforeRouteProgrammed(prefix, entry);
  }

  XLOGF(
      DBG3,
      "Finish updating RibEntry of prefix {} from peer [{}]",
      folly::IPAddress::networkToString(prefix),
      nettools::bgplib::BgpPeerId{peer.addr, peer.routerId}.str());

  /*
   * If route is configured with minimum_supporting_routes > 0, do not
   * aggregate For all other cases run aggregate logic:
   * 1. local route with minimum_supporting_routes = 0
   * 2. local injected routes (peer == localPeer, but not in localRoutes_)
   * 3. peer learnt routes
   */

  // skip aggreagate route
  if (isAggRoute) {
    return {};
  }

  /*
   * determine if this is supporting route announcement/withdrawn
   * TODO: Is sameAsLocalRoute logic expected behavior in RFC?
   */
  bool sameAsLocalRoute = localRoutes_.contains(prefix);
  /*
   * supporting route withdrawn:
   * 1. prefix != agg (prefix withdrawn):
   *      oldAllPathCnt == 1 and newAllPathCnt == 0
   * 2. prefix == agg (path withdrawn):
   *      getAllPathsCnt() = oldPathCnt - 1
   *    e.g. agg = <1.0.0.0/24, nh=0.0.0.0, supportPfxCnt=1>
   *         supporting prefix = <1.0.0.0/24, nh=peer1>
   *    If peer1 withdrawn supporting prefix => agg.supportPfxCnt--
   */
  bool supportingRouteWithdrawn =
      (!sameAsLocalRoute && oldAllPathCnt == 1 && newAllPathCnt == 0) ||
      (sameAsLocalRoute && newAllPathCnt == oldAllPathCnt - 1);
  /*
   * supporting route announcement:
   * 1. prefix != agg (prefix announcement):
   *      oldAllPathCnt == 0 and newAllPathCnt == 1
   * 2. prefix == agg (path announcement):
   *      getAllPathsCnt() = oldPathCnt + 1
   *    e.g. agg = <1.0.0.0/24, nh=0.0.0.0, supportPfxCnt=0>
   *         supporting prefix = <1.0.0.0/24, nh=peer1>
   *    If peer1 announce supporting prefix, agg.supportPfxCnt++
   */
  bool supportingRouteAnnouncement =
      (!sameAsLocalRoute && oldAllPathCnt == 0 && newAllPathCnt == 1) ||
      (sameAsLocalRoute && newAllPathCnt == oldAllPathCnt + 1);

  if (supportingRouteWithdrawn || supportingRouteAnnouncement) {
    return aggregateRoute(prefix, supportingRouteAnnouncement);
  }

  return {};
}

void RibVrf::checkWithdrawalBeforeRouteProgrammed(
    folly::CIDRNetwork& prefix,
    RibEntry& entry) noexcept {
  /*
   * Skip the normal selection/FIB cycle only when the entry has no FibOut
   * state. A null best path does not prove that FIB has no state for the
   * prefix.
   */
  if (!entry.hasFibOutState() && entry.routeInfos_.empty()) {
    XLOGF(
        DBG1,
        "The case of new prefix {} announcement followed by withdrawal during Fib-programming timer.",
        folly::IPAddress::networkToString(prefix));
    remove(entry);
  }
}

RibVrf::AggregateRoutes RibVrf::aggregateRoute(
    const folly::CIDRNetwork& prefix,
    bool supportingRouteAnnouncement) noexcept {
  AggregateRoutes aggRoutes;
  // local route aggregation
  for (auto& [localPrefix, localRoute] : localRoutes_) {
    if (!isSubnet(prefix, localPrefix)) {
      continue;
    }

    /*
     * no need to aggregate local route that do not need
     * minimum_supporting_routes assumption here is local routes does not
     * overlap with each other in configurations
     */
    auto minSupportRoutes = getMinSupportRoutes(localRoute.network);
    if (minSupportRoutes == 0) {
      continue;
    }

    // withdrawal of non-existing supporting routes, simply ignore
    if (!supportingRouteAnnouncement && localRoute.supportPfxCnt == 0) {
      continue;
    }

    // supporting prefix withdrawal
    if (!supportingRouteAnnouncement) {
      // decrease aggregate supporting route count
      --localRoute.supportPfxCnt;
      XLOGF(
          DBG1,
          "Supporting route withdrawal for {}: {}, counter decreased to {}.",
          folly::IPAddress::networkToString(localPrefix),
          folly::IPAddress::networkToString(prefix),
          localRoute.supportPfxCnt);

      /*
       * minimum_supporting_routes drop down to threshold,
       * withdrawn aggregated RouteInfo
       */
      if (localRoute.supportPfxCnt == minSupportRoutes - 1) {
        aggRoutes.emplace_back(localPrefix, nullptr);
      }
    } else { // supporting prefix announcement
      ++localRoute.supportPfxCnt;
      XLOGF(
          DBG1,
          "Supporting route announcement for {}: {}, counter increased to {}.",
          folly::IPAddress::networkToString(localPrefix),
          folly::IPAddress::networkToString(prefix),
          localRoute.supportPfxCnt);
      /*
       * prefix announcement causes count reaches minimum_supporting_routes,
       * advertise aggregated route
       */
      if (localRoute.supportPfxCnt == minSupportRoutes) {
        aggRoutes.emplace_back(localPrefix, localRoute.attrs);
      }
    }
  }
  return aggRoutes;
}

size_t RibVrf::populateFibBatch(Fib* FOLLY_NONNULL fib) {
  size_t toFibTotal = 0;
  auto iter = fibBatchList_.begin();
  auto endIter = fibBatchList_.end();

  {
    ScopedProfile profile("RibBase::fibProgramming_batch");
    while (iter != endIter) {
      auto& entry = *iter;
      const auto& prefix = entry.getPrefix();
      const auto& weightedNexthops = entry.getMultipathWeightedNexthops();
      const auto& bestPath = entry.getBestPath();
      const auto& nexthopTopoInfoMap = entry.getNexthopTopoInfoMap();
      auto routeType = BgpRouteType::UNKNOWN;
      bool isLocalRouteBest = false;
      if (bestPath) {
        const auto& bestPathNexthop = bestPath->attrs->getNexthop();
        isLocalRouteBest = (bestPathNexthop == kLocalRouteV4Nexthop) ||
            (bestPathNexthop == kLocalRouteV6Nexthop);

        routeType = *RibBase::bestpathSource(bestPath.get());
      }
      const auto& installToFib = entry.getInstallToFib();
      ++iter;

      auto fibOutRoute = fib->updateUnicastRouteWithBackup(
          prefix,
          (bestPath) ? bestPath->attrs : nullptr,
          weightedNexthops,
          isLocalRouteBest,
          installToFib,
          owner_.nexthopInfoMap_,
          std::nullopt, /* classId */
          nexthopTopoInfoMap,
          routeType,
          bestPath ? bestPath->attrs->getBackupAddr()
                   : std::optional<folly::IPAddress>{},
          owner_.isFibOutTrackingEnabled());
      if (fibOutRoute) {
        replaceFibOut(
            entry,
            FibOutState{
                .nexthops = fibNexthopSets_.getOrCreate(
                    std::move(fibOutRoute->nexthops)),
                .metadata = fibOutRoute->metadata,
                .topologyInfo = std::move(fibOutRoute->topologyInfo),
            });
      }
      /*
       * When enableFibOutTracking is false, the FIB update is still added to
       * the programming batch, but no retained payload is returned. Record
       * FibOut presence independently; explicit NONE/removal updates also
       * count.
       */
      entry.markFibOutStatePresent();
      toFibTotal++;

      // log per prefix
      XLOG_IF(
          DBG1,
          owner_.enableUnicastRouteLogging(prefix),
          logRouteWithNexthops(prefix, weightedNexthops));
    }
  } // ScopedProfile for fibProgramming_batch

  fibBatchList_.clear();
  return toFibTotal;
}

void RibVrf::schedulePrepareFibProgrammingTimer() noexcept {
  if (owner_.pauseBestPathAndFibProgramming_ || fibBatchTimer_->isScheduled()) {
    /*
     * Best path selection and Fib programming is paused or timer has been
     * scheduled already
     */
    return;
  }

  // Schedule fib programming in a batch fashion
  fibBatchTimer_->scheduleTimeout(owner_.getFibBatchTime());
}

void RibVrf::VrfProgrammingRequests::push(RibVrf& vrf) noexcept {
  if (vrf.programmingRequestHook_.is_linked()) {
    return;
  }
  requests.push_back(vrf);
  if (waiter_) {
    auto waiter = std::move(*waiter_);
    waiter_.reset();
    waiter.setValue();
  }
}

folly::coro::Task<RibVrf&> RibVrf::VrfProgrammingRequests::pop() {
  /*
   * Recheck after every wakeup: a VRF destroyed while pending unlinks itself
   * and can leave the list empty again.
   */
  while (requests.empty()) {
    auto [waiter, woken] = folly::coro::makePromiseContract<void>();
    waiter_ = std::move(waiter);
    // Awaiting the future completes it with OperationCancelled on cancellation.
    auto result = co_await folly::coro::co_awaitTry(std::move(woken));
    if (result.hasException()) {
      waiter_.reset();
      co_yield folly::coro::co_error(std::move(result).exception());
    }
  }
  auto& vrf = requests.front();
  requests.pop_front();
  co_return vrf;
}

} // namespace facebook::bgp
