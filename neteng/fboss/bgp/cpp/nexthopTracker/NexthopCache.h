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

#include <folly/IPAddress.h>
#include <folly/Synchronized.h>
#include <folly/container/F14Map.h>
#include <folly/hash/Hash.h>
#include <folly/logging/xlog.h>
#include <cstdint>
#include <functional>
#include <optional>
#include <tuple>
#include <vector>
#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopKey.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopStatus.h"

namespace facebook::bgp {

/**
 * NexthopCache: Thread-safe cache of nexthop reachability and IGP cost.
 *
 * Two independent sources feed this cache:
 *
 *   1. FsdbFibWatcher — subscribes to FSDB FIB host routes for statically
 *      configured BGP peers (multi-hop or directly connected). Updates carry
 *      isConnected=nullopt because FIB entries do not imply L2 adjacency.
 *
 *   2. FsdbNeighborWatcher — watches ARP/NDP resolution on local interfaces.
 *      Updates carry isConnected=true, marking the nexthop as directly
 *      connected. This is the sole authority for directly connected nexthops.
 *
 * Source-priority rule (enforced in addOrUpdateNextHopStatus):
 *   Once a nexthop is marked isConnected=true (by NeighborWatcher), only
 *   another isConnected=true update can modify it. Non-connected sources
 *   (FsdbFibWatcher with isConnected=nullopt) are silently skipped. This
 *   prevents stale FIB data from overriding authoritative ARP/NDP-based
 *   connectivity and IGP cost for directly connected nexthops.
 *
 * Dynamic peers:
 *   Dynamic peers (configured with CIDR peer_addr) are always directly
 *   connected — ARP/NDP must resolve before a TCP/BGP session can form.
 *   NeighborWatcher reports them with isConnected=true before any routes
 *   arrive. They are NOT subscribed in FsdbFibWatcher (which only tracks
 *   static peers with exact IP peer_addr), so there is no conflict.
 **/
class NexthopCache {
 public:
  NexthopCache() = default;

  /**
   * @brief Updates the cache with new or modified NextHop statuses.
   *
   * Called by FsdbFibWatcher (with isConnected=nullopt) and by
   * FsdbNeighborWatcher (with isConnected=true). Enforces source-priority:
   * when an entry is already marked isConnected=true, updates from
   * non-connected sources are skipped to preserve NeighborWatcher authority.
   *
   * @param nexthopStatusList A vector of NexthopStatus objects containing
   *                          the updated status information
   * @return Changed registered statuses, each retaining its resolution key.
   **/
  std::vector<NexthopStatus> addOrUpdateNextHopStatus(
      const std::vector<NexthopStatus>& nexthopStatusList);

  /*
   * Register the key and return its current status, creating an unreachable
   * entry if needed. Registration is idempotent.
   */
  NexthopStatus registerAndGetNexthopStatus(const NexthopKey& nexthop);

  /*
   * Release the key's registration. Reachable entries remain cached;
   * unreachable entries are erased. Returns true if an entry was erased.
   */
  bool unregisterAndRemoveNexthopStatus(const NexthopKey& nexthop);

  bool isRegistered(const NexthopKey& nexthop) const;

  /* Return the key's cached status, or std::nullopt if absent. */
  std::optional<NexthopStatus> getNexthopStatus(
      const NexthopKey& nexthop) const;

  /**
   * @brief Returns registered keys within the subnet's address family and
   *        resolution VRF.
   *
   * Used by NetlinkWrapper to find which registered nexthops are affected when
   * an interface prefix is added or removed, so they can be re-evaluated.
   * Netlink callers select the default resolution VRF.
   */
  std::vector<NexthopKey> getRegisteredNexthopsInSubnet(
      const NexthopKey& subnet,
      uint8_t prefixLen) const;

  /**
   * @brief Relinquishes directly-connected ownership of a nexthop.
   *
   * If the entry exists and is currently marked isConnected=true, resets it to
   * unreachable with isConnected=unset, so a non-connected source (e.g.
   * FsdbFibWatcher) can become authoritative again. This is the only way to
   * clear a connected status, since the source-priority rule in
   * addOrUpdateNextHopStatus otherwise blocks non-connected updates from
   * modifying a connected entry.
   *
   * Called by NetlinkWrapper when an interface prefix is removed and a nexthop
   * is no longer directly connected on any interface.
   *
   * @return The updated status if the entry was connected and registered,
   *         std::nullopt otherwise.
   */
  std::optional<NexthopStatus> clearConnectedStatus(const NexthopKey& nexthop);

  /**
   * @brief Registers a hook invoked when a nexthop is registered from the RIB
   *        and its source reports it as unreachable or without an IGP cost.
   *
   * The hook carries the registered resolution key.
   * NetlinkWrapper uses this to drive the pull resolution path: when the RIB
   * registers interest in such a nexthop, the hook lets NetlinkWrapper check
   * interface link state and supply any missing status.
   *
   * The hook is invoked on the RIB thread, OUTSIDE the cache lock, and must be
   * non-blocking. It is set once at startup (only when the interface-state
   * resolution flag is on) before any thread registers nexthops, so no
   * synchronization is required for the pointer itself.
   */
  void setOnNexthopRegistered(std::function<void(NexthopKey)> callback);

 private:
  /**
   * (Nexthop address, nextHopVrfId) -> NexthopStatusWithRegistration map
   *
   * This map stores the reachability, IGP cost, isConnected flag, and
   * registration flag for each nexthop in its own resolution VRF. Identical
   * addresses in different nexthop VRFs have independent resolution state.
   *
   * Write calls: FsdbFibWatcher and FsdbNeighborWatcher push updates via
   *              addOrUpdateNextHopStatus(). Source-priority ensures
   *              NeighborWatcher (isConnected=true) is authoritative for
   *              directly connected nexthops.
   *
   * Read calls: the RIB thread reads a status via
   *             registerAndGetNexthopStatus() when it creates a NexthopInfo,
   *             and via getNexthopStatus() when it processes a
   *             RibInNexthopUpdate.
   **/
  using NexthopStatusMap = folly::
      F14NodeMap<NexthopKey, NexthopStatusWithRegistration, NexthopKey::Hash>;
  folly::Synchronized<NexthopStatusMap> nexthopStatusMap_;

  /*
   * Epoch of the latest write to nexthopStatusMap_. Used only under the
   * nexthopStatusMap_ write lock. It is never reset, so a status created again
   * after an erase still gets a larger epoch.
   */
  uint64_t lastEpoch_{0};

  /*
   * Returns the status stamped with the epoch of a new write. Every status
   * stored in nexthopStatusMap_ goes through this. The caller must hold the
   * nexthopStatusMap_ write lock.
   */
  NexthopStatus withNextEpoch(NexthopStatus status);

  /*
   * Hook fired (outside the map lock) from registerAndGetNexthopStatus when a
   * registered nexthop is unreachable or has no IGP cost. Set once at startup
   * before any thread registers nexthops (see setOnNexthopRegistered), so it
   * needs no synchronization. Empty unless the interface-state resolution
   * flag is on.
   */
  std::function<void(NexthopKey)> onNexthopRegistered_;

  friend class NexthopCacheTestFixture;

  /*
   * per class placeholder for test code injection
   * only need to be setup once here
   */
#ifdef NexthopCache_TEST_FRIENDS
  NexthopCache_TEST_FRIENDS
#endif
};

} // namespace facebook::bgp
