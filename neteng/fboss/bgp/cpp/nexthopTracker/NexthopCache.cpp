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
#include "fboss/agent/Constants.h"

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
  nexthopStatusMap_.withWLock([&](folly::F14NodeMap<
                                  folly::IPAddress,
                                  NexthopStatusWithRegistration>& statusMap) {
    // Iterate through the provided list and update or add entries
    for (const auto& nexthopStatus : nexthopStatusList) {
      const auto& nexthopIp = nexthopStatus.getNexthop();
      auto it = statusMap.find(nexthopIp);

      if (it != statusMap.end()) {
        // Get the current status and registration flag
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
              "Skipping update for {} from non-connected source"
              " — nexthop is directly connected",
              nexthopIp.str());
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
              "No change in nexthop status for {}, reachable: {}, igpCost: {}, registeredFromRib: {}",
              nexthopIp.str(),
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
            "Updating nexthop status for {} from reachable: {}, igpCost: {}, resolvedForSelection: {} to reachable: {}, igpCost: {}, resolvedForSelection: {}, registeredFromRib: {}",
            nexthopIp.str(),
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

        // Update the status while preserving the registration flag
        currentStatus = withNextEpoch(nexthopStatus);

        if (isReachabilityChanged) {
          if (nexthopStatus.isReachable()) {
            RibStats::incrNhtCacheNexthopReachable();
          } else {
            RibStats::incrNhtCacheNexthopUnreachable();
          }
        }

        // If it's registered from RIB, add to push list
        if (isRegisteredFromRib) {
          statusesToPush.emplace_back(currentStatus);
        }
      } else {
        // Add new entry with registration flag set to false
        XLOGF(
            DBG2,
            "Adding new nexthop status for {}, reachable: {}, igpCost: {}, isConnected: {} registeredFromRib: false",
            nexthopIp.str(),
            nexthopStatus.isReachable(),
            nexthopStatus.getIgpCost().has_value()
                ? std::to_string(nexthopStatus.getIgpCost().value())
                : "unset",
            nexthopStatus.isConnected().has_value()
                ? std::to_string(nexthopStatus.isConnected().value())
                : "unset");

        // Insert the new entry with registration flag set to false
        statusMap.emplace(
            nexthopIp, std::make_tuple(withNextEpoch(nexthopStatus), false));
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

NexthopStatus NexthopCache::registerAndGetNexthopStatus(
    const folly::IPAddress& nexthopIp) {
  XLOGF(
      DBG2,
      "Getting nexthop status for {} and registering from RIB",
      nexthopIp.str());

  /*
   * Whether to fire the registration hook after releasing the lock: true when
   * the source does not report the nexthop as reachable or its IGP cost is
   * missing. Selection eligibility is separate: a cost-optional source may
   * remain eligible while the on-demand resolver tries to supply the metric.
   */
  bool needsResolution = false;

  auto status = nexthopStatusMap_.withWLock(
      [&](folly::F14NodeMap<folly::IPAddress, NexthopStatusWithRegistration>&
              statusMap) -> NexthopStatus {
        auto it = statusMap.find(nexthopIp);
        // Return the entry if it exists
        if (it != statusMap.end()) {
          // Mark this nexthop as registered from RIB
          auto& [entryStatus, isRegisteredFromRib] = it->second;
          isRegisteredFromRib = true;
          needsResolution = !entryStatus.isReachable() ||
              !entryStatus.getIgpCost().has_value();

          XLOGF(
              DBG2,
              "Found nexthop status for {}, reachable: {}, igpCost: {}, registeredFromRib: true",
              nexthopIp.str(),
              entryStatus.isReachable(),
              entryStatus.getIgpCost().has_value()
                  ? std::to_string(entryStatus.getIgpCost().value())
                  : "unset");
          return entryStatus;
        }

        /*
         * Create a new entry with default values (unreachable, no IGP cost)
         * and set the registration flag to true
         */
        XLOGF(
            DBG2,
            "Nexthop status not found for {}, creating new entry with default values and registeredFromRib: true",
            nexthopIp.str());

        // Create a default NexthopStatus (unreachable, no IGP cost)
        const auto defaultStatus = withNextEpoch(NexthopStatus(
            NexthopKey{.ip = nexthopIp, .nextHopVrfId = fboss::kDefaultVrf},
            /*isReachable=*/false));

        // Insert the new entry with registration flag set to true
        statusMap.emplace(nexthopIp, std::make_tuple(defaultStatus, true));
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
    onNexthopRegistered_(nexthopIp);
  }

  return status;
}

bool NexthopCache::isRegistered(const folly::IPAddress& nexthopIp) const {
  return nexthopStatusMap_.withRLock(
      [&](const folly::F14NodeMap<
          folly::IPAddress,
          NexthopStatusWithRegistration>& statusMap) -> bool {
        auto it = statusMap.find(nexthopIp);
        return it != statusMap.end() && std::get<1>(it->second);
      });
}

std::optional<NexthopStatus> NexthopCache::getNexthopStatus(
    const folly::IPAddress& nexthopIp) const {
  return nexthopStatusMap_.withRLock(
      [&](const folly::
              F14NodeMap<folly::IPAddress, NexthopStatusWithRegistration>&
                  statusMap) -> std::optional<NexthopStatus> {
        auto it = statusMap.find(nexthopIp);
        if (it == statusMap.end()) {
          return std::nullopt;
        }
        return std::get<0>(it->second);
      });
}

std::vector<folly::IPAddress> NexthopCache::getRegisteredNexthopsInSubnet(
    const folly::CIDRNetwork& prefix) const {
  const auto& [subnet, prefixLen] = prefix;
  return nexthopStatusMap_.withRLock(
      [&](const folly::
              F14NodeMap<folly::IPAddress, NexthopStatusWithRegistration>&
                  statusMap) -> std::vector<folly::IPAddress> {
        std::vector<folly::IPAddress> result;
        for (const auto& [ip, statusWithReg] : statusMap) {
          if (std::get<1>(statusWithReg) && ip.isV4() == subnet.isV4() &&
              ip.inSubnet(subnet, prefixLen)) {
            result.push_back(ip);
          }
        }
        return result;
      });
}

std::optional<NexthopStatus> NexthopCache::clearConnectedStatus(
    const folly::IPAddress& nexthopIp) {
  return nexthopStatusMap_.withWLock(
      [&](folly::F14NodeMap<folly::IPAddress, NexthopStatusWithRegistration>&
              statusMap) -> std::optional<NexthopStatus> {
        auto it = statusMap.find(nexthopIp);
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
            "Clearing connected status for {} (no longer directly connected); was reachable: {}",
            nexthopIp.str(),
            status.isReachable());

        bool wasReachable = status.isReachable();
        /*
         * Reset to unreachable with isConnected unset so a non-connected source
         * (e.g. FsdbFibWatcher) can take over authority again.
         */
        status = withNextEpoch(NexthopStatus(
            NexthopKey{.ip = nexthopIp, .nextHopVrfId = fboss::kDefaultVrf},
            /*isReachable=*/false,
            std::nullopt,
            std::nullopt));
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
    std::function<void(folly::IPAddress)> callback) {
  onNexthopRegistered_ = std::move(callback);
}

bool NexthopCache::unregisterAndRemoveNexthopStatus(
    const folly::IPAddress& nexthopIp) {
  XLOGF(
      DBG1,
      "Attempting to unregister/remove nexthop status for {}",
      nexthopIp.str());

  return nexthopStatusMap_.withWLock(
      [&](folly::F14NodeMap<folly::IPAddress, NexthopStatusWithRegistration>&
              statusMap) -> bool {
        auto it = statusMap.find(nexthopIp);
        // Process the entry if it exists
        if (it != statusMap.end()) {
          auto& [status, isRegisteredFromRib] = it->second;

          if (!status.isReachable()) {
            // If nexthop is unreachable, remove it completely
            XLOGF(
                DBG2,
                "Unregistering and removing unreachable nexthop status for {}",
                nexthopIp.str());
            statusMap.erase(it);
            RibStats::decrNexthopStatusMapCount();
            return true;
          } else {
            // If nexthop is reachable, just unregister it from RIB
            XLOGF(
                DBG2,
                "Unregistering but NOT removing reachable nexthop status for {}",
                nexthopIp.str());
            isRegisteredFromRib = false;
          }
        } else {
          XLOGF(DBG2, "Nexthop status not found for {}", nexthopIp.str());
        }
        return false;
      });
}

} // namespace facebook::bgp
