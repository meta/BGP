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

#include "neteng/fboss/bgp/cpp/nexthopTracker/FsdbFibWatcher.h"
#include <fboss/agent/if/gen-cpp2/FbossCtrlAsyncClient.h>
#include <fboss/agent/if/gen-cpp2/common_types.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <folly/CancellationToken.h>
#include <folly/Conv.h>
#include <folly/coro/Task.h>
#include <folly/logging/xlog.h>
#include <algorithm>
#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/ThriftClientUtils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsDC.h"
namespace facebook::bgp {
namespace thrift_tags = apache::thrift::ident;

namespace {
std::string formatNexthop(const NexthopKey& nexthop) {
  return fmt::format("{} (vrf={})", nexthop.ip.str(), nexthop.nextHopVrfId);
}
} // namespace

FsdbFibWatcher::FsdbFibWatcher(
    std::shared_ptr<NexthopCache> nexthopCache,
    nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ,
    folly::EventBase* evb,
    std::shared_ptr<fboss::fsdb::FsdbCowStateSubManager> fsdbSubMgr,
    std::optional<fboss::ClientID> igpCostClientId)
    : fsdbSubMgr_(std::move(fsdbSubMgr)),
      nexthopCache_(std::move(nexthopCache)),
      ribInQ_(ribInQ),
      evb_(evb),
      igpCostClientId_(igpCostClientId) {}

void FsdbFibWatcher::addNexthop(const NexthopKey& nexthop) {
  if (pathsAdded_) {
    XLOGF(
        WARNING,
        "[FsdbFibWatcher] Cannot add nexthop {} after paths have been registered"
        " — runtime subscription changes are not yet supported",
        formatNexthop(nexthop));
    return;
  }
  if (subscribedNexthops_.insert(nexthop).second) {
    XLOGF(INFO, "[FsdbFibWatcher] Added nexthop: {}", formatNexthop(nexthop));
  }
}

void FsdbFibWatcher::removeNexthop(const NexthopKey& nexthop) {
  if (pathsAdded_) {
    XLOGF(
        WARNING,
        "[FsdbFibWatcher] Cannot remove nexthop {} after paths have been"
        " registered — runtime subscription changes are not yet supported",
        formatNexthop(nexthop));
    return;
  }
  if (subscribedNexthops_.erase(nexthop)) {
    XLOGF(INFO, "[FsdbFibWatcher] Removed nexthop: {}", formatNexthop(nexthop));
  }
}

void FsdbFibWatcher::ensureSwitchIds() noexcept {
  if (!switchIds_.empty()) {
    return;
  }
  /**
   * Fetch switch IDs from the FBOSS agent so we subscribe to FIB host
   * routes under every switch ID on the device, not just "0".
   **/
  try {
    folly::EventBase evb;
    auto client =
        createThriftClient<apache::thrift::Client<facebook::fboss::FbossCtrl>>(
            evb,
            kLoopBackAddressV6,
            kFbossAgentPort,
            kFbossAgentConnTimeout,
            kFbossAgentSendTimeout,
            kFbossAgentRecvTimeout);
    std::map<int64_t, facebook::fboss::cfg::SwitchInfo> switchInfoMap;
    client->sync_getSwitchIdToSwitchInfo(switchInfoMap);
    for (const auto& [switchId, _] : switchInfoMap) {
      switchIds_.push_back(fmt::format("id={}", switchId));
    }
    XLOGF(
        INFO,
        "[FsdbFibWatcher] Retrieved {} switch IDs from agent: [{}]",
        switchIds_.size(),
        fmt::join(switchIds_, ", "));
  } catch (const std::exception& ex) {
    XLOGF(
        WARNING,
        "[FsdbFibWatcher] Failed to get switch IDs from agent: {}."
        " Defaulting to \"0\"",
        ex.what());
    switchIds_.emplace_back("id=0");
  }
}

void FsdbFibWatcher::addFsdbPathsForNexthop(
    const NexthopKey& nexthop,
    bool liveAdd) {
  bool isV4 = nexthop.ip.isV4();
  auto hostPrefix =
      fmt::format("{}/{}", nexthop.ip.str(), nexthop.ip.bitCount());
  /*
   * Route each path to the live add-path API for RIB-IN-learned nexthops (the
   * subscription is already up), or to the pre-subscribe addPath for statically
   * configured peers. Templated on the concrete v4/v6 thriftpath type.
   */
  auto addOne = [this, liveAdd](auto&& path) {
    if (liveAdd) {
      fsdbSubMgr_->addPathToLiveSubscription(
          std::forward<decltype(path)>(path));
    } else {
      fsdbSubMgr_->addPath(std::forward<decltype(path)>(path));
    }
  };
  /* A successful path can deliver updates before other switch paths exist. */
  subscribedPrefixes_.emplace(
      PrefixKey{hostPrefix, nexthop.nextHopVrfId}, nexthop);
  for (const auto& switchId : switchIds_) {
    /*
     * Idempotent guard: skip a (switchId, nextHopVrfId, prefix) path registered
     * with the sub manager. Without this, a retry after a partial failure (see
     * addNexthopPaths) would re-add an already-registered path and trip the sub
     * manager's fatal "Duplicate path added" CHECK. The key is recorded only
     * after the add succeeds, so a throwing add leaves the path eligible for a
     * clean retry.
     */
    /*
     * Include nextHopVrfId in both dedup and subscription paths: the same
     * address can be tracked through different FIB containers.
     */
    auto pathKey =
        fmt::format("{}|{}|{}", switchId, nexthop.nextHopVrfId, hostPrefix);
    if (registeredFsdbPaths_.contains(pathKey)) {
      continue;
    }
    auto basePath = fsdbStateRootPath_.agent()
                        .switchState()
                        .fibsInfoMap()[switchId]
                        .fibsMap()[nexthop.nextHopVrfId];
    if (isV4) {
      addOne(basePath.fibV4()[hostPrefix]);
    } else {
      addOne(basePath.fibV6()[hostPrefix]);
    }
    registeredFsdbPaths_.insert(pathKey);
    auto pathTokens = isV4 ? basePath.fibV4()[hostPrefix].tokens()
                           : basePath.fibV6()[hostPrefix].tokens();
    XLOGF(
        INFO,
        "[FsdbFibWatcher] Added FSDB path: [{}]",
        fmt::join(pathTokens, "/"));
  }
}

void FsdbFibWatcher::addPaths() noexcept {
  ensureSwitchIds();
  size_t failed = 0;
  for (const auto& nexthop : subscribedNexthops_) {
    /*
     * Best-effort: addFsdbPathsForNexthop() can throw (e.g. allocation
     * failure). This method is noexcept, so an escaping exception would
     * std::terminate the process — catch per-nexthop, log, and keep going so a
     * single failure cannot crash the daemon nor block the remaining paths.
     */
    try {
      addFsdbPathsForNexthop(nexthop, /*liveAdd=*/false);
    } catch (const std::exception& ex) {
      ++failed;
      XLOGF(
          ERR,
          "[FsdbFibWatcher] Failed to add FSDB paths for {}: {}",
          formatNexthop(nexthop),
          ex.what());
    }
  }
  pathsAdded_ = true;

  XLOGF(
      INFO,
      "[FsdbFibWatcher] Added FSDB paths for {} nexthops x {} switch IDs"
      " ({} nexthop(s) failed)",
      subscribedNexthops_.size(),
      switchIds_.size(),
      failed);
}

std::vector<NexthopKey> FsdbFibWatcher::filterNewNexthops(
    const std::vector<NexthopKey>& nexthops) const {
  std::vector<NexthopKey> newNexthops;
  folly::F14FastSet<NexthopKey, NexthopKey::Hash> seen;
  for (const auto& nh : nexthops) {
    // Skip nexthops already tracked, and de-dupe within the input.
    if (!subscribedNexthops_.contains(nh) && seen.insert(nh).second) {
      newNexthops.push_back(nh);
    }
  }
  return newNexthops;
}

void FsdbFibWatcher::addNexthopPaths(
    const std::vector<NexthopKey>& newNexthops,
    bool liveAdd) {
  /**
   * Register FSDB FIB host-route paths for nexthops learned at runtime (e.g.,
   * from RIB-IN). When liveAdd is true the paths are appended to the
   * already-active subscription via addPathToLiveSubscription (no
   * stop/re-subscribe; a full-state initial sync arrives on the existing
   * stream). When liveAdd is false (older server) the paths are staged with the
   * pre-subscribe addPath(); the caller wraps this in stop()/subscribe() to
   * deliver them.
   **/
  ensureSwitchIds();
  size_t added = 0;
  for (const auto& nexthop : newNexthops) {
    if (subscribedNexthops_.contains(nexthop)) {
      // Already tracked — paths already registered.
      continue;
    }
    /*
     * Register paths first, then commit to subscribedNexthops_ only on success.
     * If addFsdbPathsForNexthop throws, the nexthop is left OUT of
     * subscribedNexthops_ so filterNewNexthops will surface it again on a
     * future request (retry) rather than silently skipping it forever.
     * addFsdbPathsForNexthop is idempotent, so the retry re-adds only the paths
     * not registered on the failed attempt. The exception is swallowed (logged)
     * so it can neither crash the process nor abort the remaining nexthops.
     */
    try {
      addFsdbPathsForNexthop(nexthop, liveAdd);
      subscribedNexthops_.insert(nexthop);
      ++added;
    } catch (const std::exception& ex) {
      XLOGF(
          ERR,
          "[FsdbFibWatcher] Failed to add FSDB paths for nexthop {}: {}"
          " — will retry on a future request",
          formatNexthop(nexthop),
          ex.what());
    }
  }
  if (added > 0) {
    XLOGF(
        INFO,
        "[FsdbFibWatcher] Added {} new nexthop(s) for tracking ({} total,"
        " liveAdd={})",
        added,
        subscribedNexthops_.size(),
        liveAdd);
  }
}

void FsdbFibWatcher::registerNexthops(const std::vector<NexthopKey>& nexthops) {
  for (const auto& nexthop : nexthops) {
    addNexthop(nexthop);
  }

  evb_->runInEventBaseThread([this]() { addPaths(); });

  XLOGF(
      INFO,
      "[FsdbFibWatcher] {} static nexthops queued for subscription",
      nexthops.size());
}

void FsdbFibWatcher::stop() noexcept {
  /*
   * Subscription lifecycle is managed by the shared FsdbCowStateSubManager
   * owner
   */
}

folly::coro::Task<void> FsdbFibWatcher::co_updateCacheAndPushToRib(
    const std::vector<NexthopStatus>& updates) {
  auto statuses = nexthopCache_->addOrUpdateNextHopStatus(updates);
  if (!statuses.empty()) {
    co_await ribInQ_.nonCancellablePush(
        RibInNexthopUpdate(std::move(statuses)));
  }
}

folly::coro::Task<void> FsdbFibWatcher::co_markNeedsReconcile() {
  /*
   * Arm a full reconcile against the next snapshot instead of eagerly clearing
   * reachable state. co_processFibUpdate then diffs every subscribed prefix's
   * reachability against reachableNexthops_ and pushes only the deltas, so a
   * still-reachable nexthop is never transiently marked unreachable — an FSDB
   * restart under a live bgpd does not churn/withdraw its routes. Genuinely
   * gone nexthops (routes removed while disconnected) are withdrawn by the
   * reconcile diff.
   */
  needsReconcile_ = true;
  FsdbStatsDC::incrFsdbNhtDisconnects();
  FsdbStatsDC::setFsdbNhtConnected(0);
  XLOGF(
      INFO,
      "[FsdbFibWatcher] Reconnect: reconcile armed ({} nexthops currently"
      " reachable)",
      reachableNexthops_.size());
  co_return;
}

std::optional<uint32_t> FsdbFibWatcher::lookupIgpCostFromRoute(
    const facebook::fboss::state::RouteFields& route,
    const std::optional<fboss::ClientID>& igpCostClientId) {
  /*
   * A FIB route node carries two distinct nexthop sets:
   *  - nexthopsmulti.client2NextHopEntry: ALL nexthops contributed per source
   *    protocol/client (e.g. Open/R, TE Agent, BGP). Each client's entry holds
   *    that protocol's own per-nexthop cost (its IGP metric).
   *  - fwd: the nexthops the agent actually selected for forwarding, after
   *    resolving/tie-breaking across all clients (lowest admin distance wins).
   *
   * When igpCostClientId is unset, the cost is the minimum over fwd.nexthops[]
   * (legacy behavior).
   *
   * When igpCostClientId is set, the IGP cost is taken SOLELY from that
   * client's entry: we read the configured client's (e.g. Open/R) nexthops and
   * return the minimum cost over them.
   *
   * NOTE -- no intersection with fwd. We intentionally do NOT intersect the
   * configured client's nexthops with the resolved fwd nexthops, and we do NOT
   * check whether the two sets overlap at all. When the flag is set we blindly
   * trust the configured IGP: the cost comes only from that client's nexthops,
   * regardless of which nexthops the agent ultimately selected for forwarding.
   * This is deliberate for the current limited single-IGP (Open/R) use-case --
   * the fwd nexthops are not consulted for cost, only for the legacy
   * (flag-unset) path above.
   *
   * If the client has no entry (or none of its nexthops carry a cost), the
   * cost is left unset (nullopt). The route still stays reachable; an unset
   * cost just means the path takes part in best-path selection without an
   * IGP-cost preference. During tie-breaking a path that has a cost is
   * preferred over one that does not (an unset cost is treated as the worst
   * possible cost, UINT32_MAX).
   */

  // Minimum cost over a nexthop list (nexthops without a cost are ignored).
  auto minCost = [](const std::vector<facebook::fboss::NextHopThrift>& nexthops)
      -> std::optional<uint32_t> {
    std::optional<uint32_t> result;
    for (const auto& nh : nexthops) {
      if (nh.cost().has_value()) {
        auto cost = static_cast<uint32_t>(*nh.cost());
        if (!result || cost < *result) {
          result = cost;
        }
      }
    }
    return result;
  };

  // No protocol configured: keep current behavior (min cost over fwd nexthops).
  if (!igpCostClientId.has_value()) {
    return minCost(*route.fwd()->nexthops());
  }

  /*
   * Protocol configured: read the IGP cost solely from that client's entry,
   * with no intersection against fwd (see NOTE above). Missing client entry =>
   * nullopt (route stays reachable, cost unset).
   */
  const auto& client2NextHopEntry =
      *route.nexthopsmulti()->client2NextHopEntry();
  auto it = client2NextHopEntry.find(*igpCostClientId);
  if (it == client2NextHopEntry.end()) {
    return std::nullopt;
  }
  return minCost(*it->second.nexthops());
}

FsdbFibWatcher::SubscribedPrefixMap::const_iterator
FsdbFibWatcher::findUpdatedPrefix(
    const std::vector<std::string>& pathTokens) const {
  for (const auto& switchId : switchIds_) {
    auto fibsMapPath = fsdbStateRootPath_.agent()
                           .switchState()
                           .fibsInfoMap()[switchId]
                           .fibsMap();
    const auto& mapTokens = fibsMapPath.idTokens();
    const auto prefixIndex =
        fibsMapPath[fboss::kDefaultVrf].fibV4().idTokens().size();
    if (pathTokens.size() <= prefixIndex ||
        !std::equal(mapTokens.begin(), mapTokens.end(), pathTokens.begin())) {
      continue;
    }
    auto vrfId = folly::tryTo<uint16_t>(pathTokens[mapTokens.size()]);
    if (vrfId.hasError()) {
      continue;
    }
    const auto& prefixStr = pathTokens[prefixIndex];
    auto it = subscribedPrefixes_.find(PrefixKey{prefixStr, vrfId.value()});
    if (it == subscribedPrefixes_.end()) {
      continue;
    }
    auto vrfPath = fibsMapPath[vrfId.value()];
    const auto routeTokens = it->second.ip.isV4()
        ? vrfPath.fibV4()[prefixStr].idTokens()
        : vrfPath.fibV6()[prefixStr].idTokens();
    if (std::equal(
            routeTokens.begin(), routeTokens.end(), pathTokens.begin())) {
      return it;
    }
  }
  return subscribedPrefixes_.end();
}

std::pair<bool, std::optional<uint32_t>> FsdbFibWatcher::routeExistsInCowTree(
    const PrefixKey& prefix,
    const NexthopKey& nexthop,
    const fboss::fsdb::FsdbCowStateSubManager::Data& data) {
  const bool isV4 = nexthop.ip.isV4();
  const auto& [prefixStr, nextHopVrfId] = prefix;
  const auto prefixForLog = fmt::format("{} (vrf={})", prefixStr, nextHopVrfId);
  auto agentNode = data->safe_cref<thrift_tags::agent>();
  if (!agentNode) {
    XLOGF(
        INFO, "[FsdbFibWatcher] routeExists({}): no agent node", prefixForLog);
    return {false, std::nullopt};
  }
  auto switchStateNode =
      agentNode->template safe_cref<thrift_tags::switchState>();
  if (!switchStateNode) {
    XLOGF(
        INFO,
        "[FsdbFibWatcher] routeExists({}): no switchState node",
        prefixForLog);
    return {false, std::nullopt};
  }
  auto fibsInfoMapNode =
      switchStateNode->template safe_cref<thrift_tags::fibsInfoMap>();
  if (!fibsInfoMapNode) {
    XLOGF(
        INFO,
        "[FsdbFibWatcher] routeExists({}): no fibsInfoMap node",
        prefixForLog);
    return {false, std::nullopt};
  }

  for (const auto& switchId : switchIds_) {
    if (!fibsInfoMapNode->count(switchId)) {
      XLOGF(
          INFO,
          "[FsdbFibWatcher] routeExists({}): switchId={} not in fibsInfoMap",
          prefixForLog,
          switchId);
      continue;
    }
    const auto& fibInfoNode = fibsInfoMapNode->cref(switchId);
    auto fibsMapNode = fibInfoNode->template safe_cref<thrift_tags::fibsMap>();
    if (!fibsMapNode) {
      XLOGF(
          INFO,
          "[FsdbFibWatcher] routeExists({}): switchId={} no fibsMap",
          prefixForLog,
          switchId);
      continue;
    }
    if (!fibsMapNode->count(nextHopVrfId)) {
      XLOGF(
          INFO,
          "[FsdbFibWatcher] routeExists({}): switchId={} vrfId={} not in"
          " fibsMap",
          prefixForLog,
          switchId,
          nextHopVrfId);
      continue;
    }
    const auto& fibContainerNode = fibsMapNode->cref(nextHopVrfId);
    auto mapNode = isV4
        ? fibContainerNode->template safe_cref<thrift_tags::fibV4>()
        : fibContainerNode->template safe_cref<thrift_tags::fibV6>();
    if (!mapNode) {
      XLOGF(
          INFO,
          "[FsdbFibWatcher] routeExists({}): switchId={} no {} node",
          prefixForLog,
          switchId,
          isV4 ? "fibV4" : "fibV6");
      continue;
    }
    if (mapNode->count(prefixStr) > 0) {
      const auto& routeNode = mapNode->cref(prefixStr);
      auto igpCost =
          lookupIgpCostFromRoute(routeNode->toThrift(), igpCostClientId_);
      XLOGF(
          INFO,
          "[FsdbFibWatcher] routeExists({}): found in switchId={} {}"
          " igpCost={}",
          prefixForLog,
          switchId,
          isV4 ? "fibV4" : "fibV6",
          igpCost ? fmt::format("{}", *igpCost) : "none");
      return {true, igpCost};
    }
    XLOGF(
        INFO,
        "[FsdbFibWatcher] routeExists({}): prefix not in switchId={} {}",
        prefixForLog,
        switchId,
        isV4 ? "fibV4" : "fibV6");
  }
  return {false, std::nullopt};
}

namespace {
/*
 * Emit a single nexthop reachability delta shared by co_processFibUpdate's
 * reconcile and event-driven branches: bump the transition stat, queue a
 * NexthopStatus, and log. wasReachable is the pre-update reachability; the
 * reachable/unreachable counter is bumped only on a genuine transition. In
 * reconcile mode (isReconcile=true) only genuine transitions are logged -- that
 * branch visits every subscribed prefix, so logging steady nexthops on each
 * reconnect would be noise; the event-driven branch also logs same-state cost
 * updates. The reachability-set bookkeeping (reachableNexthops_ vs the
 * reconcile's newReachable) stays with each branch.
 */
void emitReachabilityUpdate(
    const NexthopKey& nexthop,
    bool exists,
    const std::optional<uint32_t>& igpCost,
    bool wasReachable,
    bool isReconcile,
    std::vector<NexthopStatus>& batchUpdates) {
  const char* const logPrefix = isReconcile ? "Reconcile: " : "";
  if (exists) {
    if (!wasReachable) {
      FsdbStatsDC::incrFsdbNhtNexthopReachable();
    }
    batchUpdates.emplace_back(
        nexthop,
        /*isReachable=*/true,
        igpCost,
        /*isConnected=*/std::nullopt,
        /*excludeNexthopWithoutCost=*/false);
    if (!wasReachable || !isReconcile) {
      XLOGF(
          INFO,
          "[FsdbFibWatcher] {}Nexthop {}: {} igpCost={}",
          logPrefix,
          formatNexthop(nexthop),
          wasReachable ? "cost update" : "became reachable",
          igpCost ? fmt::format("{}", *igpCost) : "none");
    }
  } else {
    if (wasReachable) {
      FsdbStatsDC::incrFsdbNhtNexthopUnreachable();
    }
    batchUpdates.emplace_back(
        nexthop,
        /*isReachable=*/false,
        /*igpCost=*/std::nullopt,
        /*isConnected=*/std::nullopt,
        /*excludeNexthopWithoutCost=*/false);
    if (wasReachable || !isReconcile) {
      XLOGF(
          INFO,
          "[FsdbFibWatcher] {}Nexthop unreachable: {} igpCost=none",
          logPrefix,
          formatNexthop(nexthop));
    }
  }
}
} // namespace

folly::coro::Task<void> FsdbFibWatcher::co_processFibUpdate(
    fboss::fsdb::FsdbCowStateSubManager::SubUpdate update) {
  /**
   * Two update modes:
   *  - Event-driven (steady state): match the FIB prefix and VRF in each
   *    updatedPath to subscribedPrefixes_ and update that nexthop.
   *  - Reconcile (first update after a (re)connect, when needsReconcile_ is
   *    armed by co_markNeedsReconcile): recompute reachability for every
   *    subscribed prefix against the full snapshot and emit only the delta vs
   *    reachableNexthops_, so a still-reachable nexthop is never transiently
   *    withdrawn (no route churn on an FSDB restart).
   *
   * No notification / no route for a prefix means the nexthop is unreachable.
   **/
  co_await folly::coro::co_safe_point;
  FsdbStatsDC::setFsdbNhtConnected(1);
  std::vector<NexthopStatus> batchUpdates;

  if (needsReconcile_) {
    /*
     * First update after (re)connect: reconcile against the full snapshot.
     * Recompute reachability for every subscribed prefix and emit only the
     * delta vs reachableNexthops_. NexthopCache dedups unchanged status, so a
     * still-reachable, same-cost nexthop yields no RIB update (no churn); only
     * nexthops that actually changed (newly reachable/unreachable, or cost
     * changed while disconnected) reach the RIB.
     */
    folly::F14FastSet<NexthopKey, NexthopKey::Hash> newReachable;
    newReachable.reserve(subscribedPrefixes_.size());
    /*
     * Each (address, nextHopVrfId) is visited once. reachableNexthops_ still
     * holds the pre-reconcile state during the
     * loop -- it is replaced only after -- so contains() detects genuine
     * transitions.
     */
    for (const auto& [prefix, nexthop] : subscribedPrefixes_) {
      auto [exists, igpCost] =
          routeExistsInCowTree(prefix, nexthop, update.data);
      const bool wasReachable = reachableNexthops_.contains(nexthop);
      if (exists) {
        newReachable.insert(nexthop);
      } else if (!wasReachable) {
        continue;
      }
      emitReachabilityUpdate(
          nexthop,
          exists,
          igpCost,
          wasReachable,
          /*isReconcile=*/true,
          batchUpdates);
    }
    reachableNexthops_ = std::move(newReachable);
    /*
     * Clear needsReconcile_ only after the reconcile completes successfully. If
     * the loop above throws, the flag stays armed so the next
     * co_processFibUpdate retries the reconcile rather than silently taking the
     * event-driven branch with stale reachability.
     */
    needsReconcile_ = false;
    XLOGF(
        INFO,
        "[FsdbFibWatcher] Reconcile: {} nexthops reachable after snapshot",
        reachableNexthops_.size());
  } else {
    for (const auto& pathTokens : update.updatedPaths) {
      auto it = findUpdatedPrefix(pathTokens);
      if (it == subscribedPrefixes_.end()) {
        continue;
      }
      const auto& [prefix, nexthop] = *it;
      auto [exists, igpCost] =
          routeExistsInCowTree(prefix, nexthop, update.data);
      const bool wasReachable = reachableNexthops_.contains(nexthop);
      if (exists) {
        reachableNexthops_.insert(nexthop);
      } else {
        reachableNexthops_.erase(nexthop);
      }
      emitReachabilityUpdate(
          nexthop,
          exists,
          igpCost,
          wasReachable,
          /*isReconcile=*/false,
          batchUpdates);
    }
  }

  if (!batchUpdates.empty()) {
    XLOGF(
        INFO,
        "[FsdbFibWatcher] Pushing {} nexthop updates to cache"
        " ({} total reachable)",
        batchUpdates.size(),
        reachableNexthops_.size());
    auto result =
        co_await co_awaitTry(co_updateCacheAndPushToRib(batchUpdates));
    if (result.hasException<folly::OperationCancelled>()) {
      co_yield folly::coro::co_error(std::move(result).exception());
    }
    if (result.hasException()) {
      XLOGF(
          ERR,
          "[FsdbFibWatcher] co_processFibUpdate failed: {}",
          result.exception().what());
    }
  }
}

} // namespace facebook::bgp
