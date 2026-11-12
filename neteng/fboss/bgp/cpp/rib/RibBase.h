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

#include <atomic>
#include <functional>
#include <optional>
#include <vector>

#include <folly/Function.h>
#include <folly/IPAddress.h>
#include <folly/IntrusiveList.h>
#include <folly/Synchronized.h>
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/coro/AsyncScope.h>
#include <folly/coro/Task.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/EventBase.h>

#include "configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h"
#include "neteng/fboss/bgp/cpp/common/BgpModuleBase.h"
#include "neteng/fboss/bgp/cpp/common/BgpPath.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/common/RouteInfo.h"
#include "neteng/fboss/bgp/cpp/common/Structs.h"
#include "neteng/fboss/bgp/cpp/common/Types.h"
#include "neteng/fboss/bgp/cpp/common/Utils.h"
#include "neteng/fboss/bgp/cpp/config/ConfigStructs.h"
#include "neteng/fboss/bgp/cpp/lib/coro/BackPressuredQueue.h"
#include "neteng/fboss/bgp/cpp/lib/coro/MPMCQueue.h"
#include "neteng/fboss/bgp/cpp/lib/coro/MergeQueue.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopCache.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopInfo.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyManager.h"
#include "neteng/fboss/bgp/cpp/rib/Fib.h"
#include "neteng/fboss/bgp/cpp/rib/LocalRoute.h"
#include "neteng/fboss/bgp/cpp/rib/RibCounters.h"
#include "neteng/fboss/bgp/cpp/rib/RibEntry.h"
#include "neteng/fboss/bgp/cpp/rib/RibPolicy.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/watchdog/MonitoredModule.h"
#include "neteng/fboss/bgp/cpp/watchdog/MonitoredQueue.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

DECLARE_string(rp_state_file);
DECLARE_string(rp_change_history_file);
DECLARE_bool(enable_default_route_logging);

namespace facebook::bgp {

// Default path for the RIB policy change history file.
// Used by bgpd (RibBase.cpp) to write change history, and by fboss2 CLI to read
// it. patternlint-disable-next-line no-dev-shm-usage
constexpr auto kRpChangeHistoryFilePath = "/dev/shm/rp_change_history.txt";
/*
 * This is the class defintion of RIB, aka, Routing Information Base.
 *
 * As the name suggests, it implements RIB functionality of the BGP RFC and
 * maintain database of all routes received from all peers. It is with this
 * module that we do best path selection and create FIB (Forwarding Information
 * Base).
 *
 * NOTE: FIB essentially is the  Loc-RIB of the BGP RFC, except that it also
 * has ECMP paths.
 */
class RibBase : public BgpModuleBase, public MonitoredModule {
 public:
  /*
   * [Util Class for Iteration]
   *
   * Create an iterator by merging the local routes and fib-programmed
   * prefixes.
   *
   * The return order of the iterator is:
   *   1. (high-pri) any local route present in the pfxNhs;
   *   2. all other non-local routes present in the pfxNhs;
   */
  class FibProgrammedPrefixIterator {
   public:
    FibProgrammedPrefixIterator(
        const RibBase& rib,
        const Fib::FibProgrammedPfxToNexthops& pfxNhs);
    Fib::FibProgrammedPfxToNexthops::const_iterator cbegin();
    Fib::FibProgrammedPfxToNexthops::const_iterator cend();
    Fib::FibProgrammedPfxToNexthops::const_iterator next();

   private:
    // Whether iterator is currently handling summary routes or not.
    bool summaryRoutesPass_{true};
    bool inited_{false};
    // Reference back to parent RIB.
    const RibBase& rib_;
    // Iterator to track next localRoutes to process.
    folly::F14NodeMap<folly::CIDRNetwork, LocalRoute>::const_iterator
        localRouteIter_;
    // Iterator to track next nonLocalRoutes to process.
    Fib::FibProgrammedPfxToNexthops::const_iterator nonLocalRouteIter_;
    // prefix to nexthop map returned from FIB.
    const Fib::FibProgrammedPfxToNexthops& pfxNhs_;
  };

  struct RibPolicyClearMsg {};

  /**
   * [Route Attribute Policy]
   *
   * Define message structure used for route attribute policy, which can
   * override BGP route attributes before installing to FIB.
   */
  struct RouteAttributePolicySetMsg {
    const rib_policy::TRouteAttributePolicy policy;

    explicit RouteAttributePolicySetMsg(
        rib_policy::TRouteAttributePolicy policy)
        : policy(std::move(policy)) {}
  };

  struct RouteAttributePolicyClearMsg {};
  struct RouteAttributePolicyTimerMsg {};

  struct PathSelectionPolicySetMsg {
    const rib_policy::TPathSelectionPolicy policy;
    const bool forceUpdate;

    explicit PathSelectionPolicySetMsg(
        rib_policy::TPathSelectionPolicy policy,
        bool forceUpdate = false)
        : policy(std::move(policy)), forceUpdate(forceUpdate) {}
  };

  struct PathSelectionPolicyClearMsg {};

  /**
   * [Route Filter Policy]
   */
  struct RouteFilterPolicySetMsg {
    const rib_policy::TRouteFilterPolicy policy;
    const bool forceUpdate;

    explicit RouteFilterPolicySetMsg(
        rib_policy::TRouteFilterPolicy policy,
        bool forceUpdate = false)
        : policy(std::move(policy)), forceUpdate(forceUpdate) {}
  };

  struct RouteFilterPolicyClearMsg {};

  using RibPolicyMessage = std::variant<
      RibPolicyClearMsg,
      RouteAttributePolicySetMsg,
      RouteAttributePolicyClearMsg,
      RouteAttributePolicyTimerMsg,
      PathSelectionPolicySetMsg,
      PathSelectionPolicyClearMsg,
      RouteFilterPolicySetMsg,
      RouteFilterPolicyClearMsg>;

  /*
   * Creates RibBase instance
   */
  RibBase(
      const std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>&
          localRoutes,
      const BgpGlobalConfig& globalConfig,
      const std::optional<bgp_policy::BgpPolicies>& policyConfig,
      nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ,
      MonitoredMPMCQueue<RibOutMessage>& ribOutQ,
      const std::string& platform,
      std::shared_ptr<NexthopCache> nextHopCache = nullptr,
      uint16_t fibAgentPort = kFbossAgentPort,
      uint32_t fibAgentRecvTimeout = kFbossAgentRecvTimeout.count());

  virtual ~RibBase();

  void run() noexcept override;

  /**
   * @brief Core shutdown sequence for the Rib module.
   *
   * Template method shared by all platforms. The fixed order guarantees that
   * no coroutine can touch a resource after it is destroyed, and that the evb
   * loop stays alive until every evb-bound resource has been torn down:
   *   1. cancelAndJoinTasks() — join all coroutines (asyncScope_) FIRST, so
   *      none can touch resources destroyed afterwards.
   *   2. cleanupCommon() — reset core timers, stop/destroy FIB, and clear heavy
   *      data structures (ribEntries_, nexthopInfoMap_).
   *   3. cleanupPlatform() — destroy platform-specific resources (e.g. RibDC's
   *      routeAttributePolicyTimer_) on the evb thread; no-op on BB.
   *   4. terminate the evb loop, only after all evb-bound cleanup is done.
   */
  void stop() noexcept override;

  void setFibBatchTime(std::chrono::milliseconds d);
  std::chrono::milliseconds getFibBatchTime() const {
    return fibBatchTime_;
  }

  /**
   * @brief Return the asyncScope of RIB
   */
  inline folly::coro::CancellableAsyncScope& getRibAsyncScope() {
    return asyncScope_;
  }

  /**
   * Set the callback invoked (on the RIB thread) with nexthops newly learned
   * from RIB-IN, so they can be registered for FSDB tracking. Wired in Main.cpp
   * to NeighborWatcher::requestNexthopSubscribe when nexthop tracking is
   * enabled. When unset (default), no RIB-IN-driven tracking is performed.
   */
  void setNexthopSubscribeRequester(
      std::function<void(std::vector<folly::IPAddress>)> requester) {
    nexthopSubscribeRequester_ = std::move(requester);
  }

  /*
   * Thrift service handlers
   */

  // Get the timestamp
  inline int64_t getLastProgrammedRoutesTimeStamp() {
    return lastProgrammedRoutesTimeStamp_;
  }

  /**
   * Check if egress rib queue has crossed high water mark
   * If so pause RIB best path computation. It will be
   * resumed by peer mgr when the queue size hits lower
   * watermark.
   */
  template <typename T>
  void ribOutQPushAndMayPauseBestPathAndFibProgramming(T&& val) {
    ribOutQ_.push(std::forward<T>(val));

    if (ribOutQ_.size() > ribOutQHighWatermark_) {
      XLOGF(INFO, "Pause RIB as ribOutQ size : {}", ribOutQ_.size());
      processPauseBestPathAndFibProgramming(
          PauseBestPathAndFibProgramming(RibPauseResumeCause::BACKPRESSURE));
    }
  }

  /** Return current RIB entries for one address family. */
  std::vector<neteng::fboss::bgp::thrift::TRibEntry> getRibEntries(
      neteng::fboss::bgp_attr::TBgpAfi afi);

  /**
   * Return compact FIB-out state for one exact prefix.
   *
   * This method performs one RIB lookup and copies only the prefix and FIB-out
   * fields. Its RIB event-base work does not grow with total RIB size. Invalid
   * prefix text raises `std::invalid_argument`.
   */
  neteng::fboss::bgp::thrift::TFibOutTable getFibOutPrefix(
      const std::string& prefix);

  /** Return the immutable FIB-out tracking configuration value. */
  bool isFibOutTrackingEnabled() const noexcept {
    return globalConfig_.enableFibOutTracking;
  }

  /*
   * Get a compact summary (total prefixes + per-prefix-length histogram) of the
   * RIB for one address family, read from ribCounters_ on this module's evb.
   */
  neteng::fboss::bgp::thrift::TRibSummary getRibSummary(
      neteng::fboss::bgp_attr::TBgpAfi afi);

  /**
   * Return canonical FIB-out nexthop sets and their shared owner counts.
   *
   * The method returns immediately when the immutable startup flag is off.
   * When enabled, it traverses the canonical-set registry on the RIB event-base
   * thread and does not scan the RIB.
   */
  neteng::fboss::bgp::thrift::TFibNexthopDatabase getFibNexthopDatabase();

  /*
   * Get ribEntries by prefixes:
   * prefix: prefix to match from the ribEntries.
   */
  std::vector<neteng::fboss::bgp::thrift::TRibEntry> getRibEntryForPrefix(
      std::unique_ptr<std::string> prefix);

  /*
   * Get ribEntries by community
   * afi: AFI to match or all AFIs
   * comms: community list with match-any logic (in contrast of match-all)
   * rib entry returned should match at least 1 community in comms.
   * when comms is emtpy, no rib entry should be returned.
   */
  std::vector<neteng::fboss::bgp::thrift::TRibEntry>
  getRibEntriesForCommunities(
      neteng::fboss::bgp_attr::TBgpAfi afi,
      const std::vector<nettools::bgplib::BgpAttrCommunityC>& comms);

  /*
   * Get ribEntries whose prefix is within a given prefix:
   * prefix: prefix to match from the ribEntries.
   */
  std::vector<neteng::fboss::bgp::thrift::TRibEntry>
  getRibEntriesForSubprefixes(std::unique_ptr<std::string> prefix);

  void injectLocalRoutes(
      const std::map<
          facebook::neteng::fboss::bgp_attr::TIpPrefix,
          facebook::neteng::fboss::bgp::thrift::TBgpAttributes>& networks);
  void removeLocalRoutes(
      const std::set<facebook::neteng::fboss::bgp_attr::TIpPrefix>& prefixes);

  folly::F14NodeMap<folly::CIDRNetwork, LocalRoute> getLocalRoutes() {
    return localRoutes_;
  }

  /*
   * True when at least one local route is configured to originate
   * conditionally on nexthop resolution (populated in the ctor from each
   * route's require_nexthop_resolution flag). The composition root uses this
   * to arm PeerManagerBase's nexthop-resolution gate without a separate config
   * knob — conditionalLocalRoutes_ is the single source of truth.
   */
  bool hasConditionalLocalRoutes() const {
    return !conditionalLocalRoutes_.empty();
  }

  /**
   * @brief Retrieves NexthopInfo for a given nexthop IP address from the
   * nexthopInfoMap_.
   *
   * @param nexthop The IP address of the nexthop to look up
   * @return std::optional<TNexthopInfo> containing nexthop information if
   *         found, std::nullopt if nexthop doesn't exist in the map
   */
  std::optional<neteng::fboss::bgp::thrift::TNexthopInfo>
  getNexthopInfoForNexthop(const folly::IPAddress& nexthop);

  /**
   * @brief Retrieves NexthopInfo for the given nexthop IP addresses from the
   * nexthopInfoMap_. If @p nexthops is empty, returns info for ALL entries
   * currently in the nexthop cache.
   *
   * @param nexthops The nexthop IP addresses to look up; empty = all entries
   * @return vector of TNexthopInfo, one per matching nexthop-cache entry
   */
  std::vector<neteng::fboss::bgp::thrift::TNexthopInfo> getNexthopInfos(
      const std::vector<folly::IPAddress>& nexthops);
  std::vector<neteng::fboss::bgp::thrift::TOriginatedRoute>
  getOriginatedRoutes();

  virtual rib_policy::TRibPolicy getRibPolicy();

  // update entry stats
  void updateEntryStats(
      neteng::fboss::bgp::thrift::TEntryStats& stats) noexcept;

  /**
   * [Partial Drain]
   *
   * Partial drain is a DC-only feature. RibBase keeps empty virtual
   * defaults so BgpService can dispatch through `RibBase&` without a
   * dynamic_cast; RibDC overrides with real bodies and BB inherits the
   * no-op default (returns an empty status).
   */
  virtual neteng::fboss::bgp::thrift::TPartialDrainStatus
  getPartialDrainStatus() const {
    return {};
  }
  virtual neteng::fboss::bgp::thrift::TPartialDrainState getPartialDrainState()
      const {
    return {};
  }
  virtual std::vector<neteng::fboss::bgp::thrift::TPartiallyDrainedPrefix>
  getPartiallyDrainedPrefixes() const {
    return {};
  }

  /**
   * Get the current RIB version -- a monotonically increasing counter bumped
   * once per prefix at queue-emission time (see incrementRibVersion for the
   * ordering rules).
   *
   * ribVersion_ is confined to the RIB event base: it is written only on the
   * RIB thread, and its sole cross-thread reader -- the thrift
   * BgpServiceBase::co_getRibVersion() handler -- reaches it through a
   * timeout-protected evb hop (co_runOnEvbWithTimeout, like co_getRibSummary);
   * this getter's own runImmediatelyOrRunInEventBaseThreadAndWait then runs
   * inline once on that evb. Confining it lets ribVersion_ stay a plain
   * uint64_t, so the frequent per-prefix bumps on the hot path remain a plain
   * increment rather than an atomic read-modify-write.
   */
  uint64_t getRibVersion() {
    uint64_t version = 0;
    evb_.runImmediatelyOrRunInEventBaseThreadAndWait(
        [&]() { version = ribVersion_; });
    return version;
  }

  /**
   * Bump the RIB version counter and return the new value. Called at
   * queue-emission time -- once per emitted prefix, and again at each
   * kRibChunkSize chunk boundary. Also advances the published RIB table-version
   * stat.
   *
   * ribVersion is stamped on every RibOut{Announcement,Withdrawal} entry and
   * consumed by AdjRib / AdjRibOutGroup (notably AdjRibGroup::isEntryShared) to
   * decide how caught-up each peer/group is. It is stamped at emission time
   * (here, and in handleFibProgrammedMessage / prepareFibProgramming), NOT at
   * path selection, so a prefix's version reflects the order its entries are
   * pushed onto ribOutQ_. The emit sites in RibBase.cpp reference three rules
   * by name; they are defined here:
   *
   *   Rule 1: a prefix's paths emitted within one chunk share one version.
   *   Rule 2: a prefix whose paths span more than one chunk strictly increases
   *           across those chunks.
   *   Rule 3: different prefixes within a message strictly increase.
   *
   * Net: non-decreasing within a chunk, strictly increasing across
   * chunks/messages. This is what isEntryShared relies on so a detached peer is
   * never wrongly treated as having already seen a post-detach entry.
   */
  uint64_t incrementRibVersion() {
    /*
     * Temporarily commented out until we fix the RIB versioning in RIB. The
     * bgpcpp.rib.tableVersion counter is advanced by
     * PeerManagerBase::incrementMaxRibVersion() in the meantime, so it reports
     * the sequence peers' cached versions are stamped from.
     *
     * RibStats::incrementRibTableVersion();
     */
    // Plain increment: ribVersion_ is mutated only on the RIB event base.
    return ++ribVersion_;
  }

  /**
   * Get the total number of prefixes in the loc-RIB across all address
   * families. Backed by ribCounters_, which is mutated only on the RIB event
   * base; this reads it via an evb hop (like getRibSummary), so it is safe to
   * call from the thrift handler thread. For the per-family (IPv4 / IPv6)
   * split, use getRibSummary (surfaced via "show bgp table summary").
   */
  uint64_t getNumPrefixes();

  virtual void clearRibPolicy();

  /*
   * Append a timestamped entry to the RIB policy change history file.
   * Keeps at most the last 50 entries.
   */
  static void appendRibPolicyChangeHistory(
      const std::string& policyType,
      int64_t version) noexcept;

  /**
   * [Route Attribute Policy]
   */
  virtual neteng::fboss::bgp::thrift::TResult setRouteAttributePolicy(
      std::unique_ptr<rib_policy::TRouteAttributePolicy> policy);

  rib_policy::TRouteAttributePolicy getRouteAttributePolicy();

  virtual void clearRouteAttributePolicy();

  /**
   * [Route Filter Policy]
   *
   * Filters the routes that are accepted from peers and announced to
   * peers. Each replace triggers a persist-to-disk and (when not in
   * read-only mode) a fib re-program.
   */
  virtual void setRouteFilterPolicy(
      std::unique_ptr<rib_policy::TRouteFilterPolicy> policy,
      bool forceUpdate = false);

  rib_policy::TRouteFilterPolicy getRouteFilterPolicy();
  // get route filter policy version. Return -1 if policy is not set.
  int64_t getRouteFilterPolicyVersion() const;

  virtual void clearRouteFilterPolicy();

  /*
   * Generic hook invoked once at the end of each prepareFibProgramming pass
   * (after path selection and route-attribute overwrite, before the
   * fibBatchList_ early-exit). No-op default; subclasses override to run
   * platform-specific end-of-pass work. RibBase performs no such work itself,
   * keeping the base class free of platform-domain logic.
   */
  virtual void onPrepareFibProgrammingComplete(bool /*fullSync*/) noexcept {}
  virtual void postRouteFilterPolicyReplaced() {}

  /*
   * Persistence logic for rib policy during restart. Save rib policy
   * state upon receipt of any rib policy updates. Virtual so platform
   * subclasses can extend the set of sub-policies persisted. Public so
   * tests and intra-rib callers can invoke it directly.
   */
  virtual void saveRibPolicyState() noexcept;

 protected:
  /*
   * [Exit] Step 1: request cancellation of and join all tasks in asyncScope_.
   * After this returns no Rib coroutine is running, so timers and data
   * structures they may access can be destroyed safely in the steps that
   * follow.
   */
  void cancelAndJoinTasks() noexcept;

  /*
   * [Exit] Step 2: common (platform-agnostic) teardown — reset core timers,
   * stop and destroy the FIB, and clear heavy data structures. Runs after
   * cancelAndJoinTasks() and while the evb loop is still alive (stop()
   * terminates the loop only after cleanupPlatform()).
   */
  void cleanupCommon() noexcept;

  /*
   * [Exit] Step 3: destroy platform-specific stop resources on the evb thread.
   * Called by stop() after cancelAndJoinTasks() (so no coroutine can race the
   * teardown) and before the evb loop is terminated (so evb-bound resources
   * such as RibDC's routeAttributePolicyTimer_ can still be reset). BB has no
   * such resources and implements this as a no-op.
   */
  virtual void cleanupPlatform() noexcept = 0;

  virtual void createFib();
  virtual void prepareFibProgramming(bool fullSync = false) noexcept;
  void handleFullAddPathWithdrawal(
      RibEntry& ribEntry,
      RibOutWithdrawal& withdrawalAddPath,
      uint64_t& currentRibVersion);

  /*
   * Flush one full (== kRibChunkSize) RibOut chunk onto ribOutQ_ (may pause),
   * then reset the buffer and reserve capacity on the vector selected by
   * reserveAddPath for the next chunk. The XLOGF stays inside so formatRibOut*
   * is only built under DBG1. Callers keep their post-flush
   * incrementRibVersion() at the call site, so per-chunk RIB versions and
   * ribOutQ_ push order are unchanged.
   */
  void flushAnnouncementChunk(
      RibOutAnnouncement& announcement,
      bool reserveAddPath,
      bool sendWithEoR);
  void flushWithdrawalChunk(
      RibOutWithdrawal& withdrawal,
      bool reserveAddPath,
      bool logAsAddPath);

  /*
   * Captured prior state used as the baseline for change detection at the
   * end of selectBestPath. Produced by snapshotAndResetForPathSelection()
   * (Phase 1 of the path-selection pipeline) which also clears the
   * mutable aggregates and topo map so the subsequent phases write into a
   * clean slate.
   */
  struct PathSelectionInput {
    std::shared_ptr<RouteInfo> oldBestpath;
    float oldAggregateReceivedUcmpWeight{0};
    float oldAggregateLocalUcmpWeight{0};
    std::shared_ptr<const WeightedNexthopMap> oldMultipathWeightedNexthops;
    std::shared_ptr<const NexthopTopoInfoMap> oldNexthopAndTopoInfo;
  };

  /*
   * Shared result struct that accumulates across the middle phases of
   * selectBestPath. Phase 3 (multiPathSelection) fills in selectedPaths;
   * Phase 4 (accumulateAggregateWeightsAndTopoInfo) extends it with
   * lbwMultiplier (used by Phase 6 when normalizing UCMP weights) and
   * topoInfoChanged (used by Phase 7 change detection).
   */
  struct MultiPathSelectionResult {
    std::vector<std::shared_ptr<RouteInfo>> selectedPaths;
    float lbwMultiplier{1.0};
    bool topoInfoChanged{false};
  };

 public:
  /*
   * Static orchestrator. Drives the 7 phase helpers in order.
   */
  static std::pair<bool, bool> selectBestPath(
      RibEntry& entry,
      const std::unique_ptr<RouteInfoSelector>& multipathSelector,
      const std::unique_ptr<RouteInfoSelector>& bestpathSelector,
      bool computeUcmp,
      uint32_t ucmpWidth,
      const std::optional<BgpUcmpQuantizer>& quantizer = std::nullopt) noexcept;

  /*
   * Instance entry point used by prepareFibProgramming. Reads
   * selectors and UCMP knobs from member state and runs the static
   * selectBestPath orchestrator.
   */
  virtual std::pair<bool, bool> runBestPathSelection(RibEntry& entry) noexcept;

 protected:
  /**
   * Classify a selected best path into its eBGP / iBGP / confed-eBGP / local
   * source class, or std::nullopt when there is no best path. Takes a raw
   * pointer so a caller can classify the current best path without copying the
   * owning shared_ptr on the per-RibEntry best-path hot path.
   */
  static std::optional<BgpRouteType> bestpathSource(
      const RouteInfo* bestpath) noexcept;

  /**
   * Update the eBGP / iBGP / confed-eBGP / local best-path source counters for
   * `prefix`, given the source class of the best path BEFORE the pass
   * (`oldSource`, std::nullopt when there was none) and the best path AFTER the
   * pass (`newBestpath`, may be null). Safe to call on every pass: it is a
   * no-op when the source class is unchanged. The old class is passed by value
   * (not the old shared_ptr) because the previous best path may be freed during
   * selection; both args avoid shared_ptr refcount traffic on the hot path.
   * Defined on RibBase -- where the path-type classifier lives -- so RibDC's
   * selectBestPath override can reuse it.
   */
  void recordBestpathSourceDelta(
      const folly::CIDRNetwork& prefix,
      std::optional<BgpRouteType> oldSource,
      const RouteInfo* newBestpath) noexcept;

  /**
   * Fold one prefix's change in inactive-path count into the RIB-wide
   * aggregate, given the entry's cached count BEFORE the pass (`oldCount`) and
   * the entry AFTER it. Safe to call on every pass: a no-op when the count is
   * unchanged, which is the common case. Like recordBestpathSourceDelta this
   * lives on RibBase so RibDC's runBestPathSelection override can reuse it --
   * a hook placed only in RibBase::runBestPathSelection would be skipped on DC.
   */
  void recordInactivePathDelta(
      const folly::CIDRNetwork& prefix,
      uint32_t oldCount,
      const RibEntry& entry) noexcept;

  static PathSelectionInput snapshotAndResetForPathSelection(
      RibEntry& entry) noexcept;

  /**
   * Return the candidate paths for best-path selection, dropping the ones
   * whose nexthops are unresolved, and cache the number dropped on the entry.
   * Non-const because of that cache: this is the one place both the RibBase and
   * RibDC orchestrators funnel through, so the count is taken at the exact
   * point where selection decides candidate eligibility.
   */
  static std::vector<std::shared_ptr<RouteInfo>> prePathSelectionFiltering(
      RibEntry& entry) noexcept;

  static MultiPathSelectionResult multiPathSelection(
      RibEntry& entry,
      const std::vector<std::shared_ptr<RouteInfo>>& routes,
      const std::unique_ptr<RouteInfoSelector>& multipathSelector) noexcept;

  static void accumulateAggregateWeightsAndTopoInfo(
      RibEntry& entry,
      MultiPathSelectionResult& mp,
      const std::shared_ptr<const NexthopTopoInfoMap>& oldNexthopAndTopoInfo,
      const std::optional<BgpUcmpQuantizer>& quantizer) noexcept;

  static void bestPathSelection(
      RibEntry& entry,
      const std::vector<std::shared_ptr<RouteInfo>>& selectedPaths,
      const std::unique_ptr<RouteInfoSelector>& bestpathSelector) noexcept;

  static WeightedNexthopMap buildAndNormalizeWeightedNexthops(
      RibEntry& entry,
      std::vector<std::shared_ptr<RouteInfo>>& selectedPaths,
      bool computeUcmp,
      uint32_t ucmpWidth,
      float lbwMultiplier) noexcept;

  static std::pair<bool, bool> computeChangePair(
      RibEntry& entry,
      const PathSelectionInput& input,
      bool topoInfoChanged,
      WeightedNexthopMap&& newNhWtMap) noexcept;

  std::chrono::milliseconds ribPauseTime_{kRibPauseTimeout};

  // Periodic interval in seconds to check for route churn
  std::chrono::seconds routeChurnCheckInterval_{kRouteChurnCheckInterval};

  /*
   * Higher watermark value for number of prefixes that are allowed within the
   * interval
   */
  static inline uint64_t highWatermarkForRouteChurn_{
      kHighWaterMarkForRouteChurn};

  /*
   * Lower watermark value for number of prefixes to fall to after threshold
   * exceeded
   */
  static inline uint64_t lowWatermarkForRouteChurn_{kLowWaterMarkForRouteChurn};

  std::unique_ptr<Fib> fib_;

  /*
   * Based on configuration, we use different multipath and bestpath selectors.
   * These will be passed to rib entry during best path selection.
   */
  std::unique_ptr<RouteInfoSelector> multipathSelector_{nullptr};
  std::unique_ptr<RouteInfoSelector> bestpathSelector_{nullptr};

  // prefix -> RibEntry map
  folly::F14NodeMap<folly::CIDRNetwork, RibEntry> ribEntries_;

  // Canonical immutable nexthop sets referenced by FIB-out route state.
  FibNexthopSets fibNexthopSets_;
  /*
   * Single authoritative aggregate of RIB-wide counts (mirrors fb303 ODS
   * counters). Mutated only on this module's EventBase alongside ribEntries_.
   */
  RibCounters ribCounters_;

  /*
   * [Fib -> Rib]
   *
   * Single-Producer-Single-Consumer(SPSC Queue) from Fib to Rib:
   *  - FibProgrammedMessage: routes programmed in HW or Fib agent connection
   *                          has been established;
   *  - FibSyncReq: Fib has disconnected from agent. Force a full-sync;
   */
  bgp::coro::MPMCQueue<Fib::FibMessage> fromFibMessageQ_;

  /*
   * [Rib -> Fib]
   *
   * Single-Producer-Single-Consumer(SPSC Queue) from Rib to Fib.
   *
   * Trigger the Fib programming task under the following conditions:
   *  - incremental route update: triggers on fibBatchTimer_'s expiration;
   *  - full-sync route update:
   *    1) initial FIB programming
   *    2) request received from Fib upon bgp-agent connection failure
   */
  struct TriggerFibProgMessage {
    const bool fullSync{false};
    explicit TriggerFibProgMessage(bool fullSync) : fullSync(fullSync) {}
  };
  bgp::coro::MPMCQueue<TriggerFibProgMessage> toFibMessageQ_;

  /*
   * [RibPolicy]
   *
   * Single-consumer coalescing queue: pushes for the same sub-policy slot merge
   * in place (latest wins) and a clear-all purges everything already pending,
   * so a superseded push (e.g. an empty route-attribute policy immediately
   * followed by a full one) never reaches the consumer. See
   * enqueueRibPolicyMsg.
   */
  bgp::coro::MergeQueue<RibPolicyMessage> ribPolicyMsgQ_;

  /* TODO: Move out of RibBase once overwriteRouteAttributes,
     createTRibEntry, and thrift getters no longer access it from RibBase */
  std::unique_ptr<RouteAttributePolicy> routeAttributePolicy_{nullptr};
  /*
   * Route filter policy — controls which routes are accepted from
   * peers and which are advertised. Owned by RibBase since both
   * platform subclasses use it.
   */
  std::unique_ptr<RouteFilterPolicy> routeFilterPolicy_{nullptr};

  /*
   * The list to store all RibEntry that have been updated and needs to
   * be programmed in Fib and/or send to peers.
   *
   * Using a non-intrusive list requires storing either a key or a shared
   * pointer of the entry. It could contribute to some performance overload.
   * Using folly::IntrusiveList could help in this case. Measurement shows
   * IntrusiveList has about 4x performance gain on constructing the list
   * and 7x gain on looping, comparing with std::forward_list.
   */
  folly::IntrusiveList<RibEntry, &RibEntry::fibBatchListHook_> fibBatchList_;

 private:
  /*
   * Member functions
   *
   * apply policy and get new attrs
   * nullptr indicates the policy rejected the prefix
   */
  std::shared_ptr<BgpPath> getBgpPathFromPolicy(
      const std::string& policyName,
      const folly::CIDRNetwork& prefixes,
      const std::shared_ptr<BgpPath>& preInAttrs);

  /** Replace one entry's FIB-out state with a canonical nexthop set. */
  void replaceFibOut(RibEntry& entry, FibOutState state);

  /** Remove one entry's FIB-out state on the RIB event-base thread. */
  void eraseFibOut(RibEntry& entry);

  /** Return an entry's submitted FIB-out state, or null if none exists. */
  const FibOutState* FOLLY_NULLABLE findFibOut(const RibEntry& entry);

  // given config, create local route object
  std::optional<facebook::bgp::LocalRoute> createLocalRoute(
      const folly::CIDRNetwork& prefix,
      const facebook::bgp::thrift::BgpNetwork& network);

  // logging utils function
  bool enableUnicastRouteLogging(const folly::CIDRNetwork& prefix);

  /**
   * Utility method to turn a RibEntry to tRibEntry
   * entry: prefix and associated ribEntry
   */
  neteng::fboss::bgp::thrift::TRibEntry createTRibEntry(
      const std::pair<const folly::CIDRNetwork, facebook::bgp::RibEntry>&
          entry);

  /**
   * Utility method to turn a RibEntry to tRibEntry after applying pathFilter.
   *
   * entry: prefix and associated ribEntry
   * pathFilter: unary predicate to check if a path should be in o/p list.
   */
  virtual std::optional<neteng::fboss::bgp::thrift::TRibEntry>
  createTRibEntryWithFilter(
      const std::pair<const folly::CIDRNetwork, facebook::bgp::RibEntry>& entry,
      const std::function<bool(const RouteInfo&)>& pathFilter);

  // The coro task to asynchronously process local routes
  folly::coro::Task<void> processLocalRoutesRoutine() noexcept;

  /*
   * [Rib <-> PeerManagerBase]
   *
   * The loop to process incoming messages from PeerManagerBase(AdjRib).
   *
   * This coro task deals with the following message types:
   *  - RibInAnnoucement
   *  - RibInWithdrawal
   *  - RibInInitialPathComputation
   *  - PauseBestPathAndFibProgramming
   *  - ResumeBestPathAndFibProgramming
   */
  folly::coro::Task<void> processRibInMsgLoop() noexcept;

  void processRibInAnnouncement(
      const TinyPeerInfo& peer,
      std::shared_ptr<const BgpPath> attr,
      const PrefixPathIds& pfxPathIds) noexcept;
  void checkWithdrawalBeforeRouteProgrammed(
      folly::CIDRNetwork& prefix,
      RibEntry& entry) noexcept;
  void processRibInWithdrawal(
      const TinyPeerInfo& peer,
      const PrefixPathIds& pfxPathIds) noexcept;
  /*
   * Handle the RibInInitialPathComputation trigger. When
   * nexthopResolutionTimeout is non-zero and not all registered nexthops are
   * resolved yet, defer the initial full-sync (set
   * pendingInitialPathComputation_) and arm nexthopResolutionTimer_ for that
   * duration; the sync then runs from maybeRunPendingInitialPathComputation()
   * once resolution completes, or from onNexthopResolutionTimeout() if the
   * timer fires first. A zero timeout runs the sync immediately.
   */
  void processRibInInitialPathComputation(
      std::chrono::milliseconds nexthopResolutionTimeout) noexcept;

  /*
   * Best-path selection + initial full-sync to FIB, RIB_COMPUTED logging, and
   * route-churn monitor scheduling. Cancels nexthopResolutionTimer_ first so no
   * timer is live when the full-sync runs.
   */
  void runInitialPathComputation() noexcept;

  /*
   * Run the deferred initial path computation if it is pending and all
   * registered nexthops have since become resolved. Called after nexthop
   * reachability changes.
   */
  void maybeRunPendingInitialPathComputation() noexcept;

  /*
   * nexthopResolutionTimer_ callback: the wait for nexthop resolution hit its
   * max-cap; run the deferred initial path computation regardless.
   */
  void onNexthopResolutionTimeout() noexcept;

  /*
   * Whether every nexthop currently registered in nexthopInfoMap_ is resolved.
   *
   * Interim semantics: "resolved" means resolved AND reachable
   * (isResolvedForSelection()). A genuinely unreachable nexthop therefore holds
   * the initial computation until the PeerManagerBase EoR timer forces it.
   * TODO: relax to "verdict received" (reachable OR definitively unreachable)
   * once NexthopStatus can distinguish "pending / never heard from FSDB" from
   * "heard: unreachable", so a legitimately-down nexthop no longer forces a
   * full eor_time_s wait at startup.
   */
  bool areAllRegisteredNexthopsResolved() const noexcept;

  /*
   * Whether the initial full-sync may run: all registered nexthops resolved
   * (areAllRegisteredNexthopsResolved) AND, when conditional local routes
   * exist, the first NexthopResolutionUpdate has been processed
   * (firstNexthopResolutionProcessed_) so those routes are advertised before
   * the sync. The conditional-routes precondition is independent of nexthop
   * tracking.
   */
  bool isInitialPathComputationReady() const noexcept;
  void processPauseBestPathAndFibProgramming(
      const PauseBestPathAndFibProgramming&
          pauseBestPathAndFibProgramming) noexcept;
  void processResumeBestPathAndFibProgramming(
      const ResumeBestPathAndFibProgramming&
          resumeBestPathAndFibProgramming) noexcept;
  /**
   * @brief Process nexthop updates from the nexthopHandler thread
   * @details This method iterates over the nexthopIps and iterates over the
   * associated RouteInfo entries to trigger best-path selection for the
   * corresponding RibEntry.
   * @param nexthopUpdate The nexthop update message containing the list of
   * nexthop IPs
   */
  void processRibInNexthopUpdate(
      const RibInNexthopUpdate& nexthopUpdate) noexcept;

  /**
   * @brief Process nexthop resolution updates from NeighborWatcher
   * @details Virtual hook called from the RibInMessage dispatch loop when a
   * NexthopResolutionUpdate arrives. Default is a no-op; subclasses that
   * originate conditional routes override to advertise/withdraw prefixes from
   * conditionalLocalRoutes_. The initial full-sync is gated separately inside
   * RIB (see processRibInInitialPathComputation), so this hook emits no
   * cross-component signal.
   * @param nexthopResolutionUpdate The update message containing both resolved
   * and unresolved nexthop ips
   */
  virtual void processNexthopResolutionUpdate(
      const NexthopResolutionUpdate& /* nexthopResolutionUpdate */) noexcept {}

  /*
   * Util function to return aggregated route(locally originated) update to be
   * processed if any
   */
  std::vector<std::pair<folly::CIDRNetwork, std::shared_ptr<const BgpPath>>>
  processSingleRibInUpdate(
      const TinyPeerInfo& peer,
      std::shared_ptr<const BgpPath> attrs,
      const PrefixPathId& pfxPid) noexcept;

  /*
   * @brief: helper function called in processSingleRibInUpdate
   * @return:
   *  1. local route announcement if >= minimum_supporting_routes number of
   *     subnet are processed.
   *  2. local routes withdrawal if < minimum_supporting_routes number of
   *     subnet are processed.
   */
  std::vector<std::pair<folly::CIDRNetwork, std::shared_ptr<const BgpPath>>>
  aggregateRoute(
      const folly::CIDRNetwork& prefix,
      bool supportingRouteAnnouncement) noexcept;

  // Util function to resume bestpath/fib programming
  void mayResumeBestPathAndFibProgramming(
      std::optional<RibPauseResumeCause> cause = std::nullopt) noexcept;

  /*
   * [Rib <-> Fib]
   *
   * This section contains the methods to handle interaction between RIB and
   * FibFboss/FibDev
   */
  folly::coro::Task<void> processFibProgrammingMsgLoop() noexcept;
  folly::coro::Task<void> processFibMsgLoop() noexcept;

  void handleFibProgrammedMessage(
      const Fib::FibProgrammedMessage& msg) noexcept;

  // Steps of handleFibProgrammedMessage for one Fib-programmed prefix.
  bool handleDeletedUnicastRoute(
      RibEntry& entry,
      const std::shared_ptr<const WeightedNexthopMap>&
          weightedNexthops) noexcept;
  bool handleStaleUnicastRoute(
      const RibEntry& entry,
      const std::shared_ptr<const WeightedNexthopMap>&
          weightedNexthops) noexcept;
  void handleAddUpdatedUnicastRoute(
      RibEntry& entry,
      bool sendWithEoR,
      RibOutAnnouncement& announcement,
      RibOutAnnouncement& announcementAddPath,
      RibOutWithdrawal& withdrawalAddPath) noexcept;
  void handleAddUpdatedMultipaths(
      RibEntry& entry,
      bool sendWithEoR,
      bool newlyInstalledInLocalRib,
      uint64_t& currentRibVersion,
      RibOutAnnouncement& announcementAddPath,
      RibOutWithdrawal& withdrawalAddPath) noexcept;
  void handleAddUpdatedMultipathDelta(
      RibEntry& entry,
      bool sendWithEoR,
      bool newlyInstalledInLocalRib,
      uint64_t& currentRibVersion,
      RibOutAnnouncement& announcementAddPath,
      RibOutWithdrawal& withdrawalAddPath) noexcept;

  void handleFibSyncReq(const Fib::FibSyncReq& req) noexcept;

 protected:
  /*
   * Platform hook for multipath cardinality changes that affect best-path
   * advertisement without changing generic best-path state.
   */
  virtual bool shouldReadvertiseBestpathOnMultipathSizeChange(
      const RibEntry& /*entry*/) const noexcept {
    return false;
  }

  void schedulePrepareFibProgrammingTimer() noexcept;

 private:
  /**
   * @brief Get the backoff timeout for Fib programming, during churn.
   *
   * @return std::chrono::seconds - backoff timeout in seconds.
   */
  virtual std::chrono::seconds getFibBackoffTimeout() const noexcept;

 protected:
  /**
   * Process RibPolicy message loop. Pure virtual — each subclass provides
   * its own dispatch covering the policy types it supports.
   */
  virtual folly::coro::Task<void> processRibPolicyMsgLoop() noexcept = 0;

  /*
   * Shared rib-policy message handlers used by every platform
   * subclass's processRibPolicyMsgLoop. Platform-specific message
   * handlers live on the respective subclass.
   */
  void handleRibPolicyClearMsg() noexcept;
  void handleRouteFilterPolicySetMsg(
      const RouteFilterPolicySetMsg& msg) noexcept;
  void handleRouteFilterPolicyClearMsg() noexcept;

  /*
   * Merge slots for ribPolicyMsgQ_. Set and Clear of the same sub-policy share
   * a slot (latest wins); the route-attribute TTL timer is its own idempotent
   * slot. RibPolicyClearMsg maps to kRibPolicyPurgeAllSlot: it takes no merge
   * slot and purges everything queued.
   */
  static constexpr int kRibPolicyPurgeAllSlot = -1;
  static constexpr int kRibPolicyMergeSlotRouteAttribute = 0;
  static constexpr int kRibPolicyMergeSlotPathSelection = 1;
  static constexpr int kRibPolicyMergeSlotRouteFilter = 2;
  static constexpr int kRibPolicyMergeSlotRouteAttributeTimer = 3;

  /*
   * Classify a rib-policy message and enqueue it into the coalescing queue: a
   * clear-all supersedes everything already pending (purge); every other
   * message merges by slot. All producers push through here.
   */
  void enqueueRibPolicyMsg(RibPolicyMessage msg) noexcept;

  virtual void overwriteRouteAttributes(
      const folly::F14FastSet<folly::CIDRNetwork>& /* prefixes */) {}

  /**
   * Replace RibPolicy: save to disk, then trigger fib programming when there
   * is delta and not in read-only mode.
   *
   * Pure virtual — each platform subclass provides its own dispatch to the
   * relevant replace*Policy methods for its platform.
   *
   * @param newRibPolicy the new RibPolicy to apply, or nullptr to clear.
   * @param isBootstrap if true, skip appending to change history file
   *        (used during constructor restore from disk).
   */
  virtual void replaceRibPolicy(
      std::unique_ptr<RibPolicy> newRibPolicy,
      bool isBootstrap = false) = 0;
  /*
   * We only replace instead of updating route filter policy
   * Each time the route filter policy is replaced, we also need to save route
   * filter policy to disk. After that, when there is delta and not in read-only
   * mode, trigger fib programming and force routes to be sent to AdjRibs
   * @return: hasUpdate: whether the newPolicy contains update
   */
  virtual bool replaceRouteFilterPolicy(
      std::unique_ptr<RouteFilterPolicy> newPolicy,
      bool isBootstrap = false,
      bool forceUpdate = false);

 protected:
  std::unique_ptr<RibPolicy> readRibPolicyState() noexcept;

 private:
  /*
   * Remove existing rp_state_file on disk. Protected so subclass
   * saveRibPolicyState overrides can reuse the file-management helpers.
   */
  void removeExistingRibPolicyStore() noexcept;
  /*
   * Serialize and save tRibPolicyStore to rp_state_file. Protected
   * for the same reason as removeExistingRibPolicyStore.
   */
  void saveTRibPolicyStore(
      const neteng::fboss::bgp::thrift::TRibPolicyStore&
          tRibPolicyStore) noexcept;

  /**
   * @brief Periodically monitor the number of prefixes learnt and detect if
   * route churn has occurred.
   *
   * @details This function periodically checks for route churn and
   * pauses/resumes best-path computation and Fib programming accordingly.
   *
   * @return None.
   */
  folly::coro::Task<void> monitorRouteChurn() noexcept;

  /*
   * Periodically export BgpProfiler stats to ODS counters.
   * Only active when --bgp_coro_profiler_export_ods is set.
   */
  folly::coro::Task<void> co_exportProfilerStatsLoop() noexcept;

  // Increment the number of prefixes learnt/withdrawn for route churn detection
  void incrementPrefixCountForRouteChurn(uint64_t count) noexcept;

  /**
   * @brief Checks if the number of prefixes learnt has exceeded the allowed
   * threshold.
   *
   * This function uses a high-watermark/low-watermark mechanism to track the
   * number of prefixes learnt. If the number of prefixes learnt exceeds the
   * maximum allowed (high watermark), it will remain in an exceeded state until
   * the number of prefixes learnt falls below the minimum allowed (low
   * watermark).
   *
   * @return True if the number of prefixes learnt exceeds the allowed
   * threshold, false otherwise.
   */
  bool isRouteChurnDetected() noexcept;

  /**
   * Helper method to get a NexthopInfo for a given nexthop.
   *
   * If the nexthop is not in nexthopInfoMap_, it will get the status from
   * nexthopCache_ and create a new NexthopInfo in the map.
   *
   * @param attrs The route attributes for an announcement
   * @return A pointer to the NexthopInfo for the nexthop, or nullptr if the
   * nexthop should not be tracked
   */
  NexthopInfo* FOLLY_NULLABLE getNexthopInfo(const BgpPath& attrs);

  /**
   * @brief Helper method to check if a NexthopInfo should be deleted from
   * nexthopInfoMap_ and nexthopCache. This is executed
   * after a RouteInfo is unlinked when enableNexthopTracking_ is true.
   *
   * @details NexthopInfo will be deleted from nexthopInfoMap_ if no routeInfo
   * objects are associated with it.
   * NexthopInfo will be deleted from nexthopCache ONLY if the nexthop is
   * unreachable.
   *
   * While fibBatchList_ is not empty, the NexthopInfo is kept. The FIB loop
   * deletes it after it takes the batch.
   *
   * @param nexthop The nexthop IP address to check for deletion
   * @return True if the NexthopInfo was deleted, false otherwise
   */
  bool checkAndDeleteNexthopInfo(const folly::IPAddress& nexthop);

  using NexthopInfoMap =
      folly::F14NodeMap<folly::IPAddress, facebook::bgp::NexthopInfo>;

  /*
   * Unregisters the nexthop from NexthopCache and erases its NexthopInfo.
   * Returns the iterator after the erased element.
   */
  NexthopInfoMap::iterator deleteNexthopInfo(NexthopInfoMap::iterator it);

  /*
   * Deletes every NexthopInfo with no RouteInfo. The FIB loop calls it after it
   * takes fibBatchList_.
   */
  void deleteUnusedNexthopInfos();

  /**
   * @brief Unlink a RouteInfo from its NexthopInfo, then call
   * checkAndDeleteNexthopInfo() for that nexthop.
   *
   * @param routeInfo The RouteInfo to unlink
   * @return True if the RouteInfo was linked to a NexthopInfo, false otherwise
   */
  bool unlinkRouteInfoAndCleanupNexthop(RouteInfo& routeInfo);

  /*
   * Member variables
   */
  const BgpGlobalConfig globalConfig_;

 protected:
  const std::string platform_;

 private:
  std::shared_ptr<facebook::bgp::PolicyManager> policyManager_{nullptr};

  folly::F14NodeMap<folly::CIDRNetwork, LocalRoute> localRoutes_{};
  /*
   * There could be more than one route pointing to the same nexthop ip,
   * hence we store a map of nexthop ip to a list of routes.
   * Routes in each list conditionally originate based on resolution of the
   * nexthop ip
   */
  folly::F14NodeMap<
      folly::IPAddress /* nexthop ip */,
      std::vector<folly::CIDRNetwork> /* route */>
      conditionalLocalRoutes_{};
  std::chrono::steady_clock::time_point
      pauseBestPathAndFibProgrammingStartTime_;

  /*
   * Max-cap timer for pausing best-path computation and Fib programming in Rib.
   * This is used to avoid deadlocks or extra long locking time for RIB/FIB
   * programming. If the pause time exceeds the set limit, Rib will resume
   * operations.
   */
  std::unique_ptr<folly::AsyncTimeout> ribPauseTimer_;

  // Number of prefixes learnt since the last reset.
  uint64_t prefixCountForRouteChurn_{0};

  // Flag to indicate if route churn is detected.
  std::atomic<bool> routeChurnDetected_{false};

  // ---------------- PeerManagerBase <-> Rib ----------------

  // rib in and out message queue
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ_;
  MonitoredMPMCQueue<RibOutMessage>& ribOutQ_;

 protected:
  /*
   * Flag used to pause Rib operations such as best-path computation and Fib
   * programming while policy re-evaluation is in progress
   * TODO: revisit this collection since it is changed by single thread.
   */
  std::atomic<bool> pauseBestPathAndFibProgramming_{false};

 private:
  /*
   * Set containing all the operations due to which best
   * path computation and Fib programming are paused
   */
  folly::Synchronized<folly::F14NodeSet<RibPauseResumeCause>>
      bestPathAndFibProgrammingPausedBy_{};

  // ---------------- Rib <-> Fib ----------------

  // fib update with batch processing
  std::chrono::milliseconds fibBatchTime_{kFibBatchTimeDefault};
  std::unique_ptr<folly::AsyncTimeout> fibBatchTimer_;

  /*
   * [Initialization]
   *
   * One time flags:
   *  - initialEorSent_: the flag indicates Rib has successfully programed
   *          initial FULL_SYNC to fib and EoR has been sent out from RIB;
   *  - ribEoRReceived_: the flag indicates Rib has received the notification
   *                    from PeerManagerBase to unblock initial RIB computation;
   */
  bool initialEorSent_{false};
  bool ribEoRReceived_{false};

  /*
   * Set when a RibInInitialPathComputation with a non-zero
   * nexthopResolutionTimeout arrives but not all registered nexthops are
   * resolved yet. The initial full-sync is held off and run later by
   * maybeRunPendingInitialPathComputation() once nexthop resolution completes,
   * or by onNexthopResolutionTimeout() if nexthopResolutionTimer_ fires first.
   * RIB-thread only.
   */
  bool pendingInitialPathComputation_{false};

  /*
   * Set once the first NexthopResolutionUpdate has been processed (conditional
   * local routes, if any, advertised into ribEntries_). Gates the initial
   * full-sync when hasConditionalLocalRoutes() so the sync does not wipe
   * GR-retained conditional routes — see isInitialPathComputationReady().
   * RIB-thread only.
   */
  bool firstNexthopResolutionProcessed_{false};

  /*
   * Bounds the deferred initial full-sync's wait for nexthop resolution. Armed
   * with the remaining EoR budget carried in RibInInitialPathComputation.
   */
  std::unique_ptr<folly::AsyncTimeout> nexthopResolutionTimer_;

  /*
   * Flag to track if a FIB sync request was received while best-path
   * computation and FIB programming were paused. This ensures that when
   * the pause is lifted, we trigger a full FIB sync (instead of incremental)
   * to honor the FibAgent's reconnection request that occurred during the
   * pause period. Without this flag, the fullSync request from FibAgent
   * would be lost, leading to inconsistent FIB state.
   */
  std::atomic<bool> fibSyncReqPending_{false};

  /*
   * - Timestamp representing last fib update;
   * - Initialized to -1 representing NO fib update yet(e.g. initialization);
   */
  int64_t lastProgrammedRoutesTimeStamp_{-1};

 private:
  friend class RibDC;
  friend class RibBB;
  friend class MockRib;
  friend class RibBaseFixture;

  /*
   * e.g., for rsw001.p002.f03.abc4, switchId is 1
   * e.g., for fa001-uu002.abc3, switchId is 2
   */
  std::optional<size_t> switchId_{std::nullopt};

  /**
   * Nexthop tracking related variables
   */
  // Flag to track if nexthop tracking is enabled
  const bool enableNexthopTracking_{false};

 protected:
  // Pointer to the NexthopCache for nexthop status tracking
  std::shared_ptr<NexthopCache> nexthopCache_{nullptr};

 private:
  /**
   * Map to store nexthop prefix to NexthopInfo accessed only by Rib thread.
   * NexthopInfo makes a local copy of the nexthop status from nexthopCache_ and
   * stores the list of BgpRouteInfo objects associated with the nexthop prefix.
   *
   * NexthopInfo carries information about:
   *  1. nexthop reachability
   *  2. IGP cost
   *  3. List of all BgpRouteInfo objects associated with the nexthop prefix.
   */
  folly::F14NodeMap<folly::IPAddress, facebook::bgp::NexthopInfo>
      nexthopInfoMap_;

  /**
   * RIB-IN-driven nexthop tracking (all RIB-thread only).
   *
   * As routes are processed, getNexthopInfo() discovers nexthops; the ones not
   * already requested are accumulated per RIB-IN batch and handed to
   * nexthopSubscribeRequester_ at the end of the batch, which asks the watcher
   * to subscribe them in FSDB. requestedNexthops_ de-dupes so each nexthop is
   * requested at most once.
   */
  std::function<void(std::vector<folly::IPAddress>)> nexthopSubscribeRequester_{
      nullptr};
  folly::F14FastSet<folly::IPAddress> requestedNexthops_;
  std::vector<folly::IPAddress> pendingNexthopSubscriptions_;

  // Flush accumulated newly-learned nexthops to nexthopSubscribeRequester_.
  void maybeFlushNexthopSubscriptions() noexcept;

 protected:
  // Fib Agent Related Information
  uint16_t fibAgentPort_;
  uint32_t fibAgentRecvTimeout_;

  /*
   * Use the path IDs allocated at multipath selection (pathIdToSend) for
   * RibOut messages instead of nexthops. See
   * BgpGlobalConfig::enableRibAllocatedPathId.
   */
  const bool enableRibAllocatedPathId_{false};

 private:
  uint32_t ribOutQHighWatermark_{kRibOutQueueSizePauseThreshold};

  /*
   * RIB version counter - monotonically increasing value bumped once per prefix
   * at queue-emission time (see incrementRibVersion). Used for tracking how
   * caught up each peer is with RIB state. Written only on the RIB event base
   * (incrementRibVersion); the sole cross-thread reader (the thrift
   * getRibVersion handler) hops onto the RIB evb, so a plain uint64_t is
   * race-free -- no atomic needed, which keeps the per-prefix bumps a plain
   * increment on the hot path.
   */
  uint64_t ribVersion_{0};

/*
 * per class placeholder for test code injection
 * only need to be setup once here
 */
#ifdef RibBase_TEST_FRIENDS
  RibBase_TEST_FRIENDS
#endif
      friend class RibFixture;
};

} // namespace facebook::bgp
