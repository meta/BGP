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

#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/fsdb/client/FsdbSubscriber.h"
#include "fboss/fsdb/client/instantiations/FsdbCowStateSubManager.h"
#include "fboss/fsdb/if/FsdbModel.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/lib/coro/BackPressuredQueue.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopCache.h"
#include "neteng/fboss/bgp/cpp/watchdog/MonitoredQueue.h"

#include <folly/IPAddress.h>
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/coro/Task.h>
#include <folly/io/async/EventBase.h>
#include <optional>
#include <utility>

namespace facebook::bgp {

/**
 * FsdbFibWatcher: Tracks FIB host routes for statically configured BGP peers.
 *
 * Subscribes to per-peer host-route paths in FSDB (e.g.,
 * agent/switchState/fibsInfoMap["0"]/fibsMap[0]/fibV4["10.0.0.1/32"]) for
 * all switch IDs on the device (fetched from the agent). Each key selects
 * its FIB VRF.
 * Only static peers (those with exact IP peer_addr from
 * getPeerToConfig()) are subscribed — dynamic peers (CIDR peer_addr from
 * getDynamicPeerToConfig()) are NOT tracked here.
 *
 * Dynamic peers are always directly connected: ARP/NDP must resolve before
 * a TCP/BGP session can form, so FsdbNeighborWatcher handles them with
 * isConnected=true. There is no need for FIB-based tracking.
 *
 * Updates pushed to NexthopCache use isConnected=nullopt (FIB entries do not
 * imply L2 adjacency). NexthopCache enforces source-priority: if a nexthop
 * is already marked isConnected=true by NeighborWatcher, FsdbFibWatcher
 * updates are silently skipped — NeighborWatcher is the sole authority for
 * directly connected nexthops (both reachability and igpCost).
 **/
class FsdbFibWatcher : public std::enable_shared_from_this<FsdbFibWatcher> {
 public:
  FsdbFibWatcher(
      std::shared_ptr<NexthopCache> nexthopCache,
      nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ,
      folly::EventBase* evb,
      std::shared_ptr<fboss::fsdb::FsdbCowStateSubManager> fsdbSubMgr,
      /*
       * Protocol/client whose nexthops the IGP cost is read from (min cost over
       * that client's nexthops, with no intersection against the resolved fwd
       * nexthops). Unset => read cost from fwd only (current behavior). Wired
       * from the next_hop_tracking_use_openr_igp_cost config in
       * NeighborWatcher; FBOSS-only.
       */
      std::optional<fboss::ClientID> igpCostClientId = std::nullopt);

  FsdbFibWatcher(const FsdbFibWatcher&) = delete;
  FsdbFibWatcher& operator=(const FsdbFibWatcher&) = delete;
  FsdbFibWatcher(FsdbFibWatcher&&) = delete;
  FsdbFibWatcher& operator=(FsdbFibWatcher&&) = delete;

  virtual ~FsdbFibWatcher() = default;

  /**
   * Add a peer address to track. Must be called before addPaths() to
   * include in the FSDB subscription. If called after addPaths(), logs a
   * warning (paths are already registered with the shared sub manager).
   **/
  void addPeerAddress(const NexthopKey& peerAddr);

  /**
   * Remove a peer address from tracking. If called after addPaths(), logs
   * a warning (paths are already registered with the shared sub manager).
   **/
  void removePeerAddress(const NexthopKey& peerAddr);

  /**
   * Add peer addresses and register their FSDB FIB host-route paths on the
   * shared sub manager. Schedules addPaths() on evb_. Must be called before
   * NeighborWatcher::subscribe() so paths are registered before subscribe().
   **/
  void registerPeers(const std::vector<NexthopKey>& peerAddresses);

  /**
   * Return keys not already tracked and de-dupe the input. Runs on evb_.
   * Used by NeighborWatcher to decide whether a
   * re-subscribe is needed for newly-learned nexthops.
   **/
  std::vector<NexthopKey> filterNewNexthops(
      const std::vector<NexthopKey>& nexthops) const;

  /**
   * Register FSDB FIB host-route paths for newly-learned nexthops (e.g., from
   * RIB-IN). When liveAdd is true (server supports live add-path) the paths are
   * appended to the already-active subscription via addPathToLiveSubscription,
   * which stays up and delivers a full-state initial sync on the existing
   * stream. When liveAdd is false (older server) the paths are staged with the
   * pre-subscribe addPath(); the caller is responsible for the surrounding
   * stop()/subscribe() (see NeighborWatcher::requestNexthopSubscribe). Runs on
   * evb_. Each key selects its FSDB FIB container and cached result.
   **/
  void addNexthopPaths(
      const std::vector<NexthopKey>& newNexthops,
      bool liveAdd);

  void stop() noexcept;

  /**
   * Called on evb_ for each FSDB update. Purely event-driven: scans
   * updatedPaths for subscribed FIB prefixes and their VRFs, using the
   * numeric field IDs registered with FSDB. Checks only affected nexthops;
   * non-FIB updates (e.g., interfaceMap) are skipped.
   *
   * No notification for a prefix means the route does not exist yet.
   * As notifications arrive, nexthop status is updated. After a reconnect
   * (co_markNeedsReconcile), the next update reconciles against the full
   * snapshot, pushing only reachability deltas.
   **/
  folly::coro::Task<void> co_processFibUpdate(
      fboss::fsdb::FsdbCowStateSubManager::SubUpdate update);

  /**
   * Arm a full reconcile against the next FSDB snapshot. Called from the FSDB
   * connection state callback on (re)connect: the next co_processFibUpdate
   * diffs every subscribed prefix's reachability against reachableNexthops_
   * and pushes only the deltas, so still-reachable nexthops are never
   * transiently marked unreachable — an FSDB restart under a live bgpd does
   * not churn/withdraw their routes.
   **/
  folly::coro::Task<void> co_markNeedsReconcile();

  /**
   * Look up the IGP cost for a FIB route node.
   *
   * If igpCostClientId is set, the cost is read solely from that
   * client/protocol's entry in nexthopsmulti.client2NextHopEntry: the minimum
   * cost over that client's nexthops. The resolved fwd nexthops are not
   * consulted -- there is no intersection between the client's nexthops and
   * fwd. Returns nullopt when the client has no entry or none of its nexthops
   * carry a cost.
   *
   * If igpCostClientId is unset, the cost is the minimum over fwd.nexthops[]
   * (current behavior).
   *
   * Static and pure (no member access) to keep the selection logic unit
   * testable independent of the FSDB COW tree.
   */
  static std::optional<uint32_t> lookupIgpCostFromRoute(
      const facebook::fboss::state::RouteFields& route,
      const std::optional<fboss::ClientID>& igpCostClientId);

 private:
  /**
   * Update nexthop cache and push changed statuses to ribInQ.
   **/
  folly::coro::Task<void> co_updateCacheAndPushToRib(
      const std::vector<NexthopStatus>& updates);

  /*
   * Used as a key to identify a unique FIB route for a given prefix across
   * all switchIds. First element is the prefix string, second is the VRF ID.
   */
  using PrefixKey = std::pair<std::string, uint16_t>;
  using SubscribedPrefixMap = folly::F14FastMap<PrefixKey, NexthopKey>;

  SubscribedPrefixMap::const_iterator findUpdatedPrefix(
      const std::vector<std::string>& pathTokens) const;

  /**
   * Add FSDB paths for all tracked peer addresses to the shared sub manager.
   * Fetches switch IDs from the agent and registers FIB host-route paths.
   **/
  void addPaths() noexcept;

  /**
   * Fetch switch IDs from the agent once; no-op if already fetched.
   **/
  void ensureSwitchIds() noexcept;

  /**
   * Add FSDB FIB host-route paths (across all switch IDs) for a single
   * nexthop and record it in subscribedPrefixes_. When liveAdd is true the
   * paths are appended to the already-active subscription via
   * addPathToLiveSubscription (used for RIB-IN-learned nexthops); when false
   * they are staged with addPath before the initial subscribe (used for
   * statically configured peers). May throw (e.g. allocation failure or a
   * client-side validation error inside the sub manager); callers handle that
   * without crashing. virtual so tests can inject a failure to exercise the
   * no-crash/retry path.
   **/
  virtual void addFsdbPathsForNexthop(const NexthopKey& nexthop, bool liveAdd);

  /**
   * Check if a route exists in the COW tree for the given prefix and
   * extract its IGP cost via lookupIgpCostFromRoute() (honoring
   * igpCostClientId_). Returns {exists, igpCost} where igpCost is nullopt
   * when the route does not exist or no applicable nexthop carries a cost.
   **/
  std::pair<bool, std::optional<uint32_t>> routeExistsInCowTree(
      const PrefixKey& prefix,
      const NexthopKey& nexthop,
      const fboss::fsdb::FsdbCowStateSubManager::Data& data);

  /**
   * Shared FSDB COW subscription manager — the same instance used by
   * FsdbNeighborWatcher. All addPath() calls (interfaceMaps + FIB routes)
   * must complete before the unified subscribe() in NeighborWatcher.
   **/
  std::shared_ptr<fboss::fsdb::FsdbCowStateSubManager> fsdbSubMgr_;
  thriftpath::RootThriftPath<fboss::fsdb::FsdbOperStateRoot> fsdbStateRootPath_;

  // Switch IDs fetched from agent in addPaths(), used in routeExistsInCowTree()
  std::vector<std::string> switchIds_;

  // Reachable nexthop identities from subscribed FIB host routes
  folly::F14FastSet<NexthopKey, NexthopKey::Hash> reachableNexthops_;

  /**
   * The authoritative set of nexthop addresses whose FIB host routes are
   * subscribed (or pending subscription) in FSDB. Populated from configured
   * BGP peers at startup (before the initial subscribe) and from RIB-IN
   * learned nexthops at runtime. Used for dedup so a nexthop is subscribed
   * exactly once per nextHopVrfId.
   **/
  folly::F14FastSet<NexthopKey, NexthopKey::Hash> subscribedPeers_;

  /* For example, ("2001:db8::1/128", 7) identifies one resolution VRF. */
  SubscribedPrefixMap subscribedPrefixes_;

  /**
   * Per-(switchId, nextHopVrfId, prefix) paths already registered with the sub
   * manager, recorded only after a successful addPath(). Makes
   * addFsdbPathsForNexthop() idempotent so that a retry following a partial
   * failure does not re-add an already-registered path (which would trip the
   * sub manager's fatal "Duplicate path added" CHECK). Key format:
   * "<switchId>|<nextHopVrfId>|<prefix>".
   **/
  folly::F14FastSet<std::string> registeredFsdbPaths_;

  /**
   * Whether addPaths() has been called. After this point, peer addresses
   * cannot be added or removed (paths are already registered with the
   * shared sub manager).
   **/
  bool pathsAdded_{false};

  /**
   * Set on (re)connect by co_markNeedsReconcile(). When true, the next
   * co_processFibUpdate reconciles reachability against the full snapshot
   * (diff vs reachableNexthops_) instead of the event-driven per-updatedPath
   * scan, so an FSDB restart does not churn still-reachable nexthops.
   **/
  bool needsReconcile_{false};

  std::shared_ptr<NexthopCache> nexthopCache_;
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ_;
  folly::EventBase* evb_;

  /*
   * Protocol/client whose nexthops the IGP cost is read from. Unset => cost is
   * read from the resolved fwd nexthops only (current behavior). See
   * lookupIgpCostFromRoute().
   */
  std::optional<fboss::ClientID> igpCostClientId_;

#ifdef FsdbFibWatcher_TEST_FRIENDS
  FsdbFibWatcher_TEST_FRIENDS
#endif
};

} // namespace facebook::bgp
