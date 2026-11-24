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

#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopCache.h"

#include <vector>
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

namespace facebook::bgp {

std::vector<NexthopStatus> NexthopCache::addOrUpdateNextHopStatus(
    const std::vector<NexthopStatus>& nexthopStatusList) {
  XLOGF(
      DBG2,
      "Adding or updating {} nexthop statuses from list",
      nexthopStatusList.size());

  // Vector to store nexthop statuses that need to be pushed to RibInQ_
  std::vector<NexthopStatus> statusesToPush;
  statusesToPush.reserve(nexthopStatusList.size());

  /*
   * Update the nexthopStatusMap_ with the provided list of NexthopStatus
   * objects and track which entries were changed or newly added
   */
  nexthopStatusMap_.withWLock([&](NexthopStatusMap& statusMap) {
    // Iterate through the provided list and update or add entries
    for (const auto& nexthopStatus : nexthopStatusList) {
      const auto& nexthop = nexthopStatus.getNexthopKey();
      const auto& nexthopIp = nexthop.ip;
      auto it = statusMap.find(nexthop);

      if (it != statusMap.end()) {
        auto& [currentStatus, isRegisteredFromRib] = it->second;

        /**
         * On FBOSS, directly connected nexthops are learned via both
         * FsdbNeighborWatcher and FsdbFibWatcher
         *
         * For directly connected nexthops (learned via
         * FsdbNeighborWatcher with isConnected=true), NeighborWatcher is
         * the sole authority for both reachability and igpCost. Ignore
         * updates from non-connected sources (FsdbFibWatcher sends
         * isConnected=nullopt) to prevent stale FIB data from overriding
         * authoritative ARP/NDP-based connectivity.
         *
         * TODO: In future, FsdbNeighborWatcher when removed, only
         * FsdbFibWatcher can sole be notifier. In that case, following block
         * won't be required anymore, and thus can go away
         **/
        if (currentStatus.isConnected() == true &&
            nexthopStatus.isConnected() != true) {
          XLOGF(
              DBG2,
              "Skipping update for {} (vrf={}) from non-connected source"
              " — nexthop is directly connected",
              nexthopIp.str(),
              nexthop.nextHopVrfId);
          continue;
        }

        const bool isReachabilityChanged =
            currentStatus.isReachable() != nexthopStatus.isReachable();
        const bool isIgpCostChanged =
            currentStatus.getIgpCost() != nexthopStatus.getIgpCost();
        const bool isResolvabilityChanged =
            currentStatus.isResolvedForSelection() !=
            nexthopStatus.isResolvedForSelection();
        const bool isNexthopStatusChanged =
            isReachabilityChanged || isIgpCostChanged || isResolvabilityChanged;

        if (!isNexthopStatusChanged) {
          XLOGF(
              DBG2,
              "No change in nexthop status for {} (vrf={}), reachable: {}, igpCost: {}, registeredFromRib: {}",
              nexthopIp.str(),
              nexthop.nextHopVrfId,
              currentStatus.isReachable(),
              currentStatus.getIgpCost().has_value()
                  ? std::to_string(currentStatus.getIgpCost().value())
                  : "unset",
              isRegisteredFromRib);
          continue;
        }

        // Log the change in status
        XLOGF(
            DBG2,
            "Updating nexthop status for {} (vrf={}) from reachable: {}, igpCost: {}, resolvedForSelection: {} to reachable: {}, igpCost: {}, resolvedForSelection: {}, registeredFromRib: {}",
            nexthopIp.str(),
            nexthop.nextHopVrfId,
            currentStatus.isReachable(),
            currentStatus.getIgpCost().has_value()
                ? std::to_string(currentStatus.getIgpCost().value())
                : "unset",
            currentStatus.isResolvedForSelection(),
            nexthopStatus.isReachable(),
            nexthopStatus.getIgpCost().has_value()
                ? std::to_string(nexthopStatus.getIgpCost().value())
                : "unset",
            nexthopStatus.isResolvedForSelection(),
            isRegisteredFromRib);

        currentStatus = withNextEpoch(nexthopStatus);

        if (isReachabilityChanged) {
          if (nexthopStatus.isReachable()) {
            RibStats::incrNhtCacheNexthopReachable();
          } else {
            RibStats::incrNhtCacheNexthopUnreachable();
          }
        }

        if (isRegisteredFromRib) {
          statusesToPush.push_back(currentStatus);
        }
      } else {
        XLOGF(
            DBG2,
            "Adding new nexthop status for {} (vrf={}), reachable: {}, igpCost: {}, isConnected: {} registeredFromRib: false",
            nexthopIp.str(),
            nexthop.nextHopVrfId,
            nexthopStatus.isReachable(),
            nexthopStatus.getIgpCost().has_value()
                ? std::to_string(nexthopStatus.getIgpCost().value())
                : "unset",
            nexthopStatus.isConnected().has_value()
                ? std::to_string(nexthopStatus.isConnected().value())
                : "unset");

        statusMap.emplace(
            nexthop, std::make_tuple(withNextEpoch(nexthopStatus), false));
        RibStats::incrNexthopStatusMapCount();

        /*
         * New entries are not registered from RIB yet, so no need to add to
         * push list
         */
      }
    }
  });

  return statusesToPush;
}

/* Registration is idempotent per (address, resolution VRF). */
NexthopStatus NexthopCache::registerAndGetNexthopStatus(
    const NexthopKey& nexthop) {
  const auto& nexthopIp = nexthop.ip;
  XLOGF(
      DBG2,
      "Getting nexthop status for {} (vrf={}) and registering from RIB",
      nexthopIp.str(),
      nexthop.nextHopVrfId);

  /*
   * Whether to fire the registration hook after releasing the lock: true when
   * the source does not report the nexthop as reachable or its IGP cost is
   * missing. Selection eligibility is separate: a cost-optional source may
   * remain eligible while the on-demand resolver tries to supply the metric.
   */
  bool needsResolution = false;

  auto status = nexthopStatusMap_.withWLock(
      [&](NexthopStatusMap& statusMap) -> NexthopStatus {
        auto it = statusMap.find(nexthop);
        // Return the entry if it exists
        if (it != statusMap.end()) {
          auto& [entryStatus, isRegisteredFromRib] = it->second;
          isRegisteredFromRib = true;
          needsResolution = !entryStatus.isReachable() ||
              !entryStatus.getIgpCost().has_value();

          XLOGF(
              DBG2,
              "Found nexthop status for {} (vrf={}), reachable: {}, igpCost: {}, registeredFromRib: true",
              nexthopIp.str(),
              nexthop.nextHopVrfId,
              entryStatus.isReachable(),
              entryStatus.getIgpCost().has_value()
                  ? std::to_string(entryStatus.getIgpCost().value())
                  : "unset");
          return entryStatus;
        }

        XLOGF(
            DBG2,
            "Nexthop status not found for {} (vrf={}), creating new entry with default values and registeredFromRib: true",
            nexthopIp.str(),
            nexthop.nextHopVrfId);

        // Create a default NexthopStatus (unreachable, no IGP cost)
        const auto defaultStatus =
            withNextEpoch(NexthopStatus(nexthop, /*isReachable=*/false));

        statusMap.emplace(nexthop, std::make_tuple(defaultStatus, true));
        RibStats::incrNexthopStatusMapCount();
        needsResolution = true;

        return defaultStatus;
      });

  /*
   * Fire the on-demand resolution hook OUTSIDE the lock (the hook is
   * non-blocking and may, in turn, call back into the cache). It is only set
   * when the neighbor-event resolution flag is on.
   */
  if (needsResolution && onNexthopRegistered_) {
    onNexthopRegistered_(nexthop);
  }

  return status;
}

bool NexthopCache::isRegistered(const NexthopKey& nexthop) const {
  return nexthopStatusMap_.withRLock(
      [&](const NexthopStatusMap& statusMap) -> bool {
        auto it = statusMap.find(nexthop);
        return it != statusMap.end() && std::get<1>(it->second);
      });
}

std::optional<NexthopStatus> NexthopCache::getNexthopStatus(
    const NexthopKey& nexthop) const {
  return nexthopStatusMap_.withRLock(
      [&](const NexthopStatusMap& statusMap) -> std::optional<NexthopStatus> {
        auto it = statusMap.find(nexthop);
        if (it == statusMap.end()) {
          return std::nullopt;
        }
        return std::get<0>(it->second);
      });
}

std::vector<NexthopKey> NexthopCache::getRegisteredNexthopsInSubnet(
    const NexthopKey& subnet,
    uint8_t prefixLen) const {
  return nexthopStatusMap_.withRLock(
      [&](const NexthopStatusMap& statusMap) -> std::vector<NexthopKey> {
        std::vector<NexthopKey> result;
        for (const auto& [key, statusWithReg] : statusMap) {
          if (key.nextHopVrfId == subnet.nextHopVrfId &&
              std::get<1>(statusWithReg) && key.ip.isV4() == subnet.ip.isV4() &&
              key.ip.inSubnet(subnet.ip, prefixLen)) {
            result.push_back(key);
          }
        }
        return result;
      });
}

std::optional<NexthopStatus> NexthopCache::clearConnectedStatus(
    const NexthopKey& nexthop) {
  const auto& nexthopIp = nexthop.ip;
  return nexthopStatusMap_.withWLock(
      [&](NexthopStatusMap& statusMap) -> std::optional<NexthopStatus> {
        auto it = statusMap.find(nexthop);
        if (it == statusMap.end()) {
          return std::nullopt;
        }
        auto& [status, isRegisteredFromRib] = it->second;
        // Only act on entries we currently own as directly connected.
        if (status.isConnected() != true) {
          return std::nullopt;
        }

        XLOGF(
            DBG2,
            "Clearing connected status for {} (vrf={}) (no longer directly connected); was reachable: {}",
            nexthopIp.str(),
            nexthop.nextHopVrfId,
            status.isReachable());

        bool wasReachable = status.isReachable();
        /*
         * Reset to unreachable with isConnected unset so a non-connected source
         * (e.g. FsdbFibWatcher) can take over authority again.
         */
        status = withNextEpoch(NexthopStatus(
            nexthop, /*isReachable=*/false, std::nullopt, std::nullopt));
        if (wasReachable) {
          RibStats::incrNhtCacheNexthopUnreachable();
        }

        if (isRegisteredFromRib) {
          return status;
        }
        return std::nullopt;
      });
}

NexthopStatus NexthopCache::withNextEpoch(NexthopStatus status) {
  status.epoch_ = ++lastEpoch_;
  return status;
}

void NexthopCache::setOnNexthopRegistered(
    std::function<void(NexthopKey)> callback) {
  onNexthopRegistered_ = std::move(callback);
}

/* Reachable results remain cached after the RIB releases its interest. */
bool NexthopCache::unregisterAndRemoveNexthopStatus(const NexthopKey& nexthop) {
  return nexthopStatusMap_.withWLock([&](auto& statusMap) {
    auto it = statusMap.find(nexthop);
    if (it == statusMap.end()) {
      return false;
    }
    auto& [status, isRegisteredFromRib] = it->second;
    isRegisteredFromRib = false;
    if (status.isReachable()) {
      return false;
    }
    statusMap.erase(it);
    RibStats::decrNexthopStatusMapCount();
    return true;
  });
}

} // namespace facebook::bgp
