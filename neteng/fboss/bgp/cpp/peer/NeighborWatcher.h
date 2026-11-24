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

#include <folly/io/async/AsyncSocket.h>

#include <folly/IPAddress.h>
#include <folly/container/F14Set.h>
#include <gflags/gflags_declare.h>
#include "fboss/fsdb/client/FsdbPubSubManager.h"
#include "fboss/fsdb/client/FsdbSubManager.h"
#include "fboss/fsdb/client/instantiations/FsdbCowStateSubManager.h"
#include "fboss/fsdb/if/FsdbModel.h"
#include "neteng/fboss/bgp/cpp/common/BgpModuleBase.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/common/Structs.h"
#include "neteng/fboss/bgp/cpp/lib/coro/BackPressuredQueue.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopKey.h"
#include "neteng/fboss/bgp/cpp/watchdog/MonitoredQueue.h"

DECLARE_int32(fsdb_config_timeout_s);
DECLARE_bool(enable_fsdb_patch_subscriber);
DECLARE_int32(fsdbPort);

namespace facebook::bgp {

class FsdbFibWatcher;
class NexthopCache;

class FsdbConfigWatcher
    : public std::enable_shared_from_this<FsdbConfigWatcher> {
 public:
  FsdbConfigWatcher(
      folly::EventBase* evb,
      std::shared_ptr<folly::fibers::Baton> fsdbCfgBaton,
      const int32_t fsdbPort = FLAGS_fsdbPort)
      : fsdbPubSubMgr_(
            FLAGS_enable_fsdb_patch_subscriber
                ? nullptr
                : std::make_unique<fboss::fsdb::FsdbPubSubManager>("bgpd")),
        fsdbSubMgr_(
            FLAGS_enable_fsdb_patch_subscriber
                ? std::make_unique<fboss::fsdb::FsdbCowStateSubManager>(
                      fboss::fsdb::SubscriptionOptions("bgpd"),
                      fboss::utils::ConnectionOptions("::1", fsdbPort))
                : nullptr),
        neighborWatcherEvb_(evb),
        fsdbCfgBaton_(fsdbCfgBaton),
        fsdbPort_(fsdbPort) {}

  FsdbConfigWatcher(const FsdbConfigWatcher&) = delete;

  FsdbConfigWatcher& operator=(const FsdbConfigWatcher&) = delete;

  ~FsdbConfigWatcher() = default;

  void run() noexcept;

  void stop() noexcept;

  /*
   * Return mapping of peer subnet to link bandwidth based on port and vlan
   * config
   */
  std::optional<folly::F14NodeMap<folly::CIDRNetwork, int64_t>>
  getPeerSubnetLbwMap() noexcept;

 private:
  void processSwitchCfgChanges(const fboss::cfg::SwitchConfig& switchConfig);
  void fsdbSwitchCfgCb(const fboss::cfg::SwitchConfig& switchConfig);
  void stopFsdbSubscription() noexcept;

  /*
   * [FSDB] variables for FSDB subscription
   */
  std::shared_ptr<folly::F14NodeMap<folly::CIDRNetwork, int64_t>>
      peerSubnetLbwMap_;
  std::unique_ptr<fboss::fsdb::FsdbPubSubManager> fsdbPubSubMgr_;
  std::unique_ptr<fboss::fsdb::FsdbCowStateSubManager> fsdbSubMgr_;
  thriftpath::RootThriftPath<fboss::fsdb::FsdbOperStateRoot> fsdbStateRootPath_;
  std::shared_ptr<fboss::cfg::SwitchConfig> switchConfig_{nullptr};
  bool fsdbSubscribed_{false};
  folly::EventBase* neighborWatcherEvb_;

  // Baton to coordinate the first time FSDB publishes config
  std::shared_ptr<folly::fibers::Baton> fsdbCfgBaton_;

  const int32_t fsdbPort_;

/*
 * per class placeholder for test code injection
 * only need to be setup once here
 */
#ifdef NeighborWatcher_TEST_FRIENDS
  NeighborWatcher_TEST_FRIENDS
#endif
};

class FsdbNeighborWatcher
    : public std::enable_shared_from_this<FsdbNeighborWatcher> {
 public:
  FsdbNeighborWatcher(
      MonitoredMPMCQueue<facebook::bgp::NeighborWatcherMessage>& neighborEventQ,
      nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ,
      folly::EventBase* evb,
      folly::coro::CancellableAsyncScope& asyncScope,
      const int32_t fsdbPort = FLAGS_fsdbPort)
      : fsdbPubSubMgr_(
            FLAGS_enable_fsdb_patch_subscriber
                ? nullptr
                : std::make_unique<fboss::fsdb::FsdbPubSubManager>("bgpd")),
        fsdbSubMgr_(
            FLAGS_enable_fsdb_patch_subscriber
                ? std::make_unique<fboss::fsdb::FsdbCowStateSubManager>(
                      fboss::fsdb::SubscriptionOptions("bgpd"),
                      fboss::utils::ConnectionOptions("::1", fsdbPort))
                : nullptr),
        neighborEventQ_(neighborEventQ),
        neighborWatcherEvb_(evb),
        asyncScope_(asyncScope),
        ribInQ_(ribInQ),
        fsdbPort_(fsdbPort) {}

  FsdbNeighborWatcher(const FsdbNeighborWatcher&) = delete;

  FsdbNeighborWatcher& operator=(const FsdbNeighborWatcher&) = delete;

  ~FsdbNeighborWatcher() = default;

  void run() noexcept;

  void stop() noexcept;

  /*
   * Callback type for forwarding resolved ARP/NDP neighbor IPs to
   * NexthopCache. Called on evb_ with the full set of resolved neighbor IPs
   * (portId != 0, non-link-local) from all interfaces' arpTable/ndpTable.
   * The callback should only emit "reachable" for newly resolved neighbors;
   * removals are handled by the session-teardown path
   * (processInterfaceMapChanges → neighborEventQ_).
   */
  using ResolvedNeighborCallback =
      std::function<void(folly::F14FastSet<NexthopKey, NexthopKey::Hash>)>;

  /*
   * Set a callback to receive resolved neighbor updates. Must be called
   * before subscription callbacks fire (i.e., before evb_ loop starts).
   */
  void setResolvedNeighborCallback(ResolvedNeighborCallback cb) {
    resolvedNeighborCb_ = std::move(cb);
  }

  /**
   * Process a COW subscription update. Extracts interfaceMap changes and
   * resolved neighbor IPs. Called from the unified subscribe callback when
   * sharing a sub manager with FsdbFibWatcher. Must be called on evb_.
   **/
  void handleCowUpdate(
      const fboss::fsdb::FsdbCowStateSubManager::SubUpdate& update);

  /*
   * A neighbor is considered resolved only when both portId != 0 AND
   * state == Reachable. portId alone has been observed to lag the
   * actual reachability state -- e.g. SEV S661800, where the FBOSS
   * agent left portId set on a PENDING entry, causing BGP to miss the
   * link-down signal until the hold timer expired. Checking state
   * directly avoids relying on the portId convention. If the two
   * signals disagree, log a warning so the inconsistency surfaces.
   */
  static bool isNeighborResolved(
      const fboss::state::NeighborEntryFields& nbrFields);

 private:
  void getNbrEntryChanges(
      const std::map<std::string, fboss::state::NeighborEntryFields>&
          oldNbrEntry,
      const std::map<std::string, fboss::state::NeighborEntryFields>&
          newNbrEntry,
      std::vector<folly::IPAddress>& deletedAddrs,
      std::vector<folly::IPAddress>& addedAddrs);
  folly::coro::Task<void> processInterfaceMapChanges(
      std::map<int32_t, fboss::state::InterfaceFields> newInterfaceMap);
  void fsdbInterfaceStateCb(
      const std::map<int32_t, fboss::state::InterfaceFields>& newInterfaceMap);
  void logNeighborTable(
      const fboss::state::InterfaceFields& interfaceFields,
      const std::map<std::string, fboss::state::NeighborEntryFields>& nbrTable);

  /**
   * Extract resolved (portId != 0, state == Reachable, non-link-local)
   * IPs from a single neighbor table (arpTable or ndpTable) and insert
   * them into resolvedNexthops.
   **/
  static void collectResolvedIpsFromTable(
      const std::map<std::string, fboss::state::NeighborEntryFields>& nbrTable,
      folly::F14FastSet<NexthopKey, NexthopKey::Hash>& resolvedNexthops);

  /*
   * Bumps BgpStats::kNeighborPortIdStateMismatch and emits a rate-limited
   * WARNING when portId and state disagree. Called from
   * collectResolvedIpsFromTable only, so the
   * counter fires once per entry per scan.
   */
  static void reportPortIdStateMismatch(
      const fboss::state::NeighborEntryFields& nbrFields);

  /**
   * Collect all resolved neighbor IPs from an interfaceMap by scanning
   * both arpTable and ndpTable of every interface. Invokes
   * resolvedNeighborCb_ with the result if the callback is set.
   **/
  void forwardResolvedNeighborIps(
      const std::map<
          fboss::state::SwitchIdList,
          std::map<int32_t, fboss::state::InterfaceFields>>& interfaceMap);

  /*
   * [FSDB] variables for FSDB subscription
   */
  std::unique_ptr<fboss::fsdb::FsdbPubSubManager> fsdbPubSubMgr_;
  std::unique_ptr<fboss::fsdb::FsdbCowStateSubManager> fsdbSubMgr_;
  thriftpath::RootThriftPath<fboss::fsdb::FsdbOperStateRoot> fsdbStateRootPath_;
  std::unique_ptr<std::map<int32_t, fboss::state::InterfaceFields>>
      interfaceMap_{nullptr};

  /*
   * [Queue] Single-Producer-Single-Consumer(SPSC Queue) for neighborEvent
   */
  MonitoredMPMCQueue<facebook::bgp::NeighborWatcherMessage>& neighborEventQ_;
  folly::EventBase* neighborWatcherEvb_;
  folly::coro::CancellableAsyncScope& asyncScope_;

  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ_;

  const int32_t fsdbPort_;

  // Optional callback to forward resolved neighbor IPs to NexthopCache
  ResolvedNeighborCallback resolvedNeighborCb_;

/*
 * per class placeholder for test code injection
 * only need to be setup once here
 */
#ifdef NeighborWatcher_TEST_FRIENDS
  NeighborWatcher_TEST_FRIENDS
#endif
};

class FsdbSwitchReachabilityWatcher
    : public std::enable_shared_from_this<FsdbSwitchReachabilityWatcher> {
 public:
  FsdbSwitchReachabilityWatcher(
      MonitoredMPMCQueue<facebook::bgp::NeighborWatcherMessage>& neighborEventQ,
      folly::EventBase* evb,
      const int32_t fsdbPort = FLAGS_fsdbPort)
      : fsdbPubSubMgr_(
            FLAGS_enable_fsdb_patch_subscriber
                ? nullptr
                : std::make_unique<fboss::fsdb::FsdbPubSubManager>("bgpd")),
        fsdbSubMgr_(
            FLAGS_enable_fsdb_patch_subscriber
                ? std::make_unique<fboss::fsdb::FsdbCowStateSubManager>(
                      fboss::fsdb::SubscriptionOptions("bgpd"),
                      fboss::utils::ConnectionOptions("::1", fsdbPort))
                : nullptr),
        neighborEventQ_(neighborEventQ),
        neighborWatcherEvb_(evb),
        fsdbPort_(fsdbPort) {}

  ~FsdbSwitchReachabilityWatcher() = default;
  FsdbSwitchReachabilityWatcher(const FsdbSwitchReachabilityWatcher&) = delete;
  FsdbSwitchReachabilityWatcher& operator=(
      const FsdbSwitchReachabilityWatcher&) = delete;
  FsdbSwitchReachabilityWatcher(FsdbSwitchReachabilityWatcher&&) = delete;
  FsdbSwitchReachabilityWatcher& operator=(FsdbSwitchReachabilityWatcher&&) =
      delete;

  void run() noexcept;
  void stop() noexcept;

 private:
  void processSwitchReachability();

  // [FSDB] variables for FSDB subscription
  std::unique_ptr<fboss::fsdb::FsdbPubSubManager> fsdbPubSubMgr_;
  std::unique_ptr<fboss::fsdb::FsdbCowStateSubManager> fsdbSubMgr_;
  thriftpath::RootThriftPath<fboss::fsdb::FsdbOperStateRoot> fsdbStateRootPath_;

  // [Queue] Single-Producer-Single-Consumer(SPSC Queue) for neighborEvent
  MonitoredMPMCQueue<facebook::bgp::NeighborWatcherMessage>& neighborEventQ_;
  folly::EventBase* neighborWatcherEvb_;

  const int32_t fsdbPort_;

/*
 * per class placeholder for test code injection
 * only need to be setup once here
 */
#ifdef NeighborWatcher_TEST_FRIENDS
  NeighborWatcher_TEST_FRIENDS
#endif
};

class NeighborWatcher : public BgpModuleBase {
 public:
  explicit NeighborWatcher(
      MonitoredMPMCQueue<NeighborWatcherMessage>& neighborEventQ,
      nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ,
      const bool enableSwitchReachabilityWatcher = false,
      std::shared_ptr<fboss::fsdb::FsdbCowStateSubManager> sharedFsdbSubMgr =
          nullptr,
      const int32_t fsdbPort = FLAGS_fsdbPort);

  NeighborWatcher(const NeighborWatcher&) = delete;
  NeighborWatcher& operator=(const NeighborWatcher&) = delete;

  ~NeighborWatcher() = default;

  void run() noexcept override;
  void stop() noexcept override;

  /**
   * Register a callback on FsdbNeighborWatcher that pushes resolved ARP/NDP
   * neighbor IPs into NexthopCache as directly connected (isConnected=true).
   **/
  void learnConnectedNeighbors(
      std::shared_ptr<NexthopCache> nexthopCache,
      nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ);

  /**
   * Create FsdbFibWatcher and register FIB host-route paths for static
   * peers. Must be called before subscribe() so FIB paths are registered
   * before the subscription starts.
   **/
  void startFibWatcher(
      std::shared_ptr<NexthopCache> nexthopCache,
      nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ,
      const std::vector<folly::IPAddress>& peerAddresses);

  /**
   * Subscribe the shared FSDB sub manager for all registered paths
   * (interfaceMaps from run(), plus FIB paths if startFibWatcher() was
   * called). Dispatches updates to FsdbNeighborWatcher and, if present,
   * FsdbFibWatcher.
   **/
  void subscribe();

  /**
   * Request that the given nexthops (learned at runtime, e.g. from RIB-IN) be
   * tracked in FSDB. Marshals onto evb_, filters out already-tracked nexthops,
   * and — if any are new — registers their FIB paths. When the connected FSDB
   * server supports live add-path, the paths are appended to the running shared
   * subscription (no teardown); otherwise it falls back to
   * stop() -> addPath() -> subscribeLocked() (a full re-subscribe). Safe to
   * call from any thread (e.g. the RIB thread).
   **/
  void requestNexthopSubscribe(std::vector<folly::IPAddress> nexthops);

  /*
   * return nullopt if connection is not ready
   * return empty map if IO fails (cannot construct map)
   * otherwise return full map
   */
  folly::coro::Task<
      std::optional<folly::F14NodeMap<folly::CIDRNetwork, int64_t>>>
  co_getPeerSubnetLbwMap() noexcept;

  /*
   * Thrift service handlers
   */

 private:
  /*
   * Body of subscribe(); must run on evb_. Also invoked by
   * requestNexthopSubscribe() on the older-server fallback path to re-subscribe
   * the shared subscription after stop()/addPath().
   */
  void subscribeLocked();

  std::atomic_bool isRunning_{false};

  /*
   * Set true once the shared FSDB subscription has been started (end of
   * subscribeLocked()) and never cleared afterwards: it records that startup
   * has run, NOT that the subscription is healthy right now. Accessed only on
   * evb_. Gates requestNexthopSubscribe() against pre-startup requests, for
   * which there is nothing to add paths to; whether a live subscription
   * currently exists is answered by FsdbSubManagerBase::supportsLiveAddPath(),
   * which reads the subscriber itself.
   */
  bool fsdbSubscribed_{false};

  // Watchers on FSDB.
  std::shared_ptr<FsdbNeighborWatcher> fsdbNbrWatcher_;
  std::shared_ptr<FsdbConfigWatcher> fsdbConfigWatcher_;
  std::shared_ptr<FsdbSwitchReachabilityWatcher> fsdbReachabilityWatcher_;
  std::shared_ptr<FsdbFibWatcher> fsdbFibWatcher_;

  /**
   * Shared COW subscription manager used by both FsdbNeighborWatcher and
   * FsdbFibWatcher. Passed in from Main.cpp when patch subscriber mode is
   * enabled. All addPath() calls (interfaceMaps + FIB routes) complete
   * before the unified subscribe() call.
   **/
  std::shared_ptr<fboss::fsdb::FsdbCowStateSubManager> sharedFsdbSubMgr_;

  // Baton to coordinate the first time FSDB publishes config
  std::shared_ptr<folly::fibers::Baton> fsdbCfgBaton_;

  std::optional<folly::F14NodeMap<folly::CIDRNetwork, int64_t>>
  getFsdbPeerSubnetLbwMap() noexcept;

/*
 * per class placeholder for code injection
 * only need to setup once
 */
#ifdef NeighborWatcher_TEST_FRIENDS
  NeighborWatcher_TEST_FRIENDS
#endif
};

} // namespace facebook::bgp
