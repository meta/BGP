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
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <folly/container/F14Set.h>
#include <folly/logging/xlog.h>
#include <gflags/gflags.h>

#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/rib_policy_types.h"
#include "neteng/fboss/bgp/cpp/common/platform/dc/PlatformConstant.h"
#include "neteng/fboss/bgp/cpp/fsdb/CanonicalRibUpdateQueue.h"
#include "neteng/fboss/bgp/cpp/rib/RibBase.h"
#include "neteng/fboss/bgp/cpp/rib/RibPolicyLogger.h"

DECLARE_bool(publish_multipaths_to_fsdb);
DECLARE_bool(publish_partial_drain_state_to_fsdb);
DECLARE_string(crf_policy_file);
DECLARE_string(cps_policy_file);

namespace facebook::bgp {

class CanonicalRibBuilder;
class CanonicalRibSnapshotRequests;
class FsdbSyncer;
class NeighborWatcher;

class RibDC : public RibBase {
 public:
  RibDC(
      const std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>&
          localRoutes,
      const BgpGlobalConfig& globalConfig,
      const std::optional<bgp_policy::BgpPolicies>& policyConfig,
      nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ,
      MonitoredMPMCQueue<RibOutMessage>& ribOutQ,
      const std::string& platform,
      FsdbSyncer* fsdbSyncer,
      std::shared_ptr<NexthopCache> nextHopCache = nullptr,
      uint16_t fibAgentPort = kPlatformFibAgentPort,
      uint32_t fibAgentRecvTimeout = kPlatformFibAgentRecvTimeout,
      /*
       * DC-only. When set together with nexthop tracking, the constructor wires
       * RIB-IN-learned nexthops to the FIB watcher for FSDB tracking. EBB
       * (RibBB) has no NeighborWatcher and leaves this null.
       */
      std::shared_ptr<NeighborWatcher> neighborWatcher = nullptr);
  ~RibDC() override = default;

  void run() noexcept override;

  /*
   * DC-only thrift surfaces for CPS. These do not exist on RibBase —
   * callers must hold a RibDC& (BgpServiceDC has one) to reach them.
   * `virtual` here is for test fixtures (MockRib) that need to wrap
   * set/clear, not for any DC-vs-BB dispatch — RibBB does not derive
   * from RibDC.
   */
  virtual neteng::fboss::bgp::thrift::TResult setPathSelectionPolicy(
      std::unique_ptr<rib_policy::TPathSelectionPolicy> policy,
      bool forceUpdate = false);
  rib_policy::TPathSelectionPolicy getPathSelectionPolicy();
  int64_t getPathSelectionPolicyVersion() const;
  virtual void clearPathSelectionPolicy();
  std::vector<rib_policy::TPathSelector> getActivePathSelectionCriteria(
      std::unique_ptr<std::vector<std::string>> prefixes);

  bool isCrfFileModeEnabled() const;
  void setCrfFileModeEnabled(bool fileModeActive);

  neteng::fboss::bgp::thrift::TCanonicalRibState getRibEntriesCanonical(
      neteng::fboss::bgp_attr::TBgpAfi afi,
      VrfId vrfId = fboss::kDefaultVrf);
  neteng::fboss::bgp::thrift::TCanonicalRibState getRibPrefixCanonical(
      std::unique_ptr<std::string> prefix,
      VrfId vrfId = fboss::kDefaultVrf);
  neteng::fboss::bgp::thrift::TCanonicalRibState
  getRibEntriesForCommunitiesCanonical(
      neteng::fboss::bgp_attr::TBgpAfi afi,
      const std::vector<nettools::bgplib::BgpAttrCommunityC>& communities,
      VrfId vrfId = fboss::kDefaultVrf);
  neteng::fboss::bgp::thrift::TCanonicalRibState
  getRibEntriesForCommunityCanonical(
      neteng::fboss::bgp_attr::TBgpAfi afi,
      std::unique_ptr<std::string> community,
      VrfId vrfId = fboss::kDefaultVrf);
  neteng::fboss::bgp::thrift::TCanonicalRibState getRibSubprefixesCanonical(
      std::unique_ptr<std::string> prefix,
      VrfId vrfId = fboss::kDefaultVrf);
  /*
   * Result of resolving the file-based CRF artifact against the cached
   * RibPolicy at startup, returned by resolveCrfPolicy(). Bundles the two
   * outputs the bootstrap path needs together:
   *   - ribPolicy: the RibPolicy to install. Either the cached policy with
   *     its route_filter_policy replaced by the artifact's, or (when no
   *     cached policy exists) a fresh RibPolicy holding only the artifact
   *     policy. Never null when the artifact applies; falls back to the
   *     cached policy unchanged when the artifact is absent/dryrun/invalid.
   *   - crfFileMode: whether FILE_MODE should be enabled, i.e. the artifact
   *     was present with dryrun=false and was successfully applied. Used to
   *     gate out Thrift-based CRF updates once file-mode is active.
   */
  struct CrfResolution {
    std::unique_ptr<RibPolicy> ribPolicy;
    bool crfFileMode;
  };
  static CrfResolution resolveCrfPolicy(
      std::unique_ptr<RibPolicy> cachedPolicy,
      const std::optional<rib_policy::CrfPolicyArtifact>& artifact);

  bool isCpsFileModeEnabled() const;
  void setCpsFileModeEnabled(bool fileModeActive);

  /*
   * Result of resolving the file-based CPS artifact against the cached
   * RibPolicy at startup, returned by resolveCpsPolicy(). Bundles the two
   * outputs the bootstrap path needs together:
   *   - ribPolicy: the RibPolicy to install. Either the cached policy with
   *     its path_selection_policy replaced by the artifact's, or (when no
   *     cached policy exists) a fresh RibPolicy holding only the artifact
   *     policy. Never null when the artifact applies; falls back to the
   *     cached policy unchanged when the artifact is absent/dryrun/invalid.
   *   - cpsFileMode: whether FILE_MODE should be enabled, i.e. the artifact
   *     was present with dryrun=false and was successfully applied. Used to
   *     gate out Thrift-based CPS updates once file-mode is active.
   */
  struct CpsResolution {
    std::unique_ptr<RibPolicy> ribPolicy;
    bool cpsFileMode;
  };
  static CpsResolution resolveCpsPolicy(
      std::unique_ptr<RibPolicy> cachedPolicy,
      const std::optional<rib_policy::CpsPolicyArtifact>& artifact);

  /**
   * [Partial Drain] DC-only device-level accessors.
   * Surfaced via TBgpService thrift; BgpService dispatches through
   * `RibBase&` and BB inherits the empty defaults from RibBase.
   */
  neteng::fboss::bgp::thrift::TPartialDrainStatus getPartialDrainStatus()
      const override;
  neteng::fboss::bgp::thrift::TPartialDrainState getPartialDrainState()
      const override;
  std::vector<neteng::fboss::bgp::thrift::TPartiallyDrainedPrefix>
  getPartiallyDrainedPrefixes(VrfId vrfId = fboss::kDefaultVrf) const override;

 protected:
  void createFib() override;

  bool shouldReadvertiseBestpathOnMultipathSizeChange(
      const RibEntry& entry) const noexcept override;

  /*
   * [Exit] Destroy the DC-specific routeAttributePolicyTimer_ on the evb
   * thread. Invoked by RibBase::stop() after coroutines are joined and before
   * the evb loop is terminated. See RibBase::cleanupPlatform().
   */
  void cleanupPlatform() noexcept override;

  /**
   * Record a per-prefix partial-drain transition. DC-only; called from
   * RibDC::runBestPathSelection immediately after the CPS-aware
   * selectBestPath() (which is the sole producer of isPartialDrain_), so
   * all partial-drain bookkeeping stays inside RibDC and RibBase remains
   * platform-agnostic.
   *
   * Updates `drainedPrefixCount_` (per-prefix population) and bumps
   * `partialDrainTransitionCount_` only when the device-level drain state
   * crosses zero (no prefix drained <-> at least one prefix drained),
   * matching the IDL contract on `partial_drain_transition_count`.
   *
   * @return true if a transition occurred (newIsPartialDrain !=
   * oldIsPartialDrain), so callers can decide whether to publish the
   * updated state.
   */
  bool recordPartialDrainTransition(
      bool oldIsPartialDrain,
      bool newIsPartialDrain);

  /*
   * DC-only end-of-pass hook. Overrides the generic
   * RibBase::onPrepareFibProgrammingComplete. It queues pending partial-drain
   * state, releases a deferred canonical snapshot, and starts FsdbSyncer after
   * the initial full-RIB computation so its first snapshot is complete.
   */
  void onPrepareFibProgrammingComplete(bool fullSync) noexcept override;
  /*
   * Enqueue the current partial-drain state into the cache owned by
   * FsdbSyncer. DC-only, called only from
   * onPrepareFibProgrammingComplete() — never via RibBase&, so it is not a
   * RibBase virtual. Returns true if it was enqueued, false if the feature is
   * disabled or no syncer is wired.
   */
  bool enqueuePartialDrainState();
  void postRouteFilterPolicyReplaced() override;
  void processNexthopResolutionUpdate(
      const NexthopResolutionUpdate& nexthopResolutionUpdate) noexcept override;
  void replaceRibPolicy(
      std::unique_ptr<RibPolicy> newRibPolicy,
      bool isBootstrap = false) override;

  void overwriteRouteAttributes(
      RibVrf& vrf,
      const folly::F14FastSet<folly::CIDRNetwork>& prefixes) override;

  folly::coro::Task<void> processRibPolicyMsgLoop() noexcept override;
  /* CPS message handlers — DC-only. No virtuals on RibBase; RibDC's
   * processRibPolicyMsgLoop dispatches PathSelectionPolicy{Set,Clear}Msg
   * to these directly. */
  void handlePathSelectionPolicySetMsg(
      const PathSelectionPolicySetMsg& msg) noexcept;
  void handlePathSelectionPolicyClearMsg() noexcept;

  /**
   * Result of single-pass cache migration that combines policy comparison
   * and cache migration to avoid triple iteration over statements.
   */
  struct CacheMigrationResult {
    bool hasUpdate{false};
    bool needsReEvaluation{false};
    std::vector<folly::CIDRNetwork> affectedPrefixes;
  };

  /**
   * Replace route attribute policy (CTE). Each time the route attribute
   * policy is replaced, when there is delta and not in read-only mode,
   * trigger fib programming.
   *
   * @return hasUpdate — whether the newPolicy contains update (including
   *         the expiration time change).
   */
  virtual bool replaceRouteAttributePolicy(
      std::unique_ptr<RouteAttributePolicy> newPolicy);

  /**
   * Single-pass cache migration that combines policy comparison and cache
   * migration. Compares statements ONCE and migrates cache entries, returning
   * all information needed by replaceRouteAttributePolicy().
   *
   * This eliminates the triple statement comparison that previously occurred:
   *   1. operator!= (compares statements)
   *   2. needsReEvaluation() (iterates statements again)
   *   3. migrateRouteAttributePolicyCache() (iterates statements third time)
   */
  CacheMigrationResult migrateRouteAttributePolicyCache(
      RouteAttributePolicy& oldPolicy,
      RouteAttributePolicy& newPolicy);

  void scheduleRouteAttributePolicyTimer() noexcept;

  /**
   * Replace path selection policy (CPS). Each time the path selection
   * policy is replaced, save to disk and trigger fib programming when
   * there is delta and not in read-only mode.
   *
   * @param newPolicy the new PathSelectionPolicy to apply, or nullptr to clear.
   * @param isBootstrap if true, skip saving to disk and appending to
   *        change history (used during constructor restore).
   * @param forceUpdate if true, bypass the version check (the content-change
   *        check still applies); used by CPS FILE_MODE delivery.
   * @return hasUpdate — whether the newPolicy contains update.
   */
  virtual bool replacePathSelectionPolicy(
      std::unique_ptr<PathSelectionPolicy> newPolicy,
      bool isBootstrap = false,
      bool forceUpdate = false);

  /*
   * Shared helper behind setCrfFileModeEnabled / setCpsFileModeEnabled: flip
   * the atomic mode flag and log only on an actual transition. policyName
   * ("CRF" / "CPS") is used purely for the log message.
   */
  static void setFileModeEnabled(
      std::atomic<bool>& flag,
      bool fileModeActive,
      std::string_view policyName);

 public:
  /*
   * Instance entry point. Runs the CPS-aware orchestrator using the
   * RibDC instance's owned pathSelectionPolicy_.
   */
  std::pair<bool, bool> runBestPathSelection(
      RibCounters& ribCounters,
      RibEntry& entry) noexcept override;

  /*
   * Static orchestrator with an explicit policy parameter. Used by
   * tests that drive a bare RibEntry without constructing a Rib
   * instance and want to evaluate a particular policy against it.
   * Runs the same 7-phase pipeline as RibBase::selectBestPath but
   * uses the policy (when non-null) to override the multipath phase.
   */
  static std::pair<bool, bool> selectBestPath(
      RibEntry& entry,
      const std::unique_ptr<RouteInfoSelector>& multipathSelector,
      const std::unique_ptr<RouteInfoSelector>& bestpathSelector,
      bool computeUcmp,
      uint32_t ucmpWidth,
      const std::optional<BgpUcmpQuantizer>& quantizer = std::nullopt,
      const std::unique_ptr<PathSelectionPolicy>& pathSelectionPolicy =
          nullptr) noexcept;

  /*
   * DC-only overrides of the mixed-content methods that RibBase keeps
   * native-only. Each override adds the CPS-specific fields the DC
   * binary needs (path_selection_policy on thrift, active_cps_criteria
   * on TRibEntry, persisted CPS policy on disk). Public to match
   * RibBase's public signatures and let tests / thrift handlers call
   * them directly.
   */
  rib_policy::TRibPolicy getRibPolicy() override;
  void saveRibPolicyState() noexcept override;

  std::optional<::facebook::bgp::rib_policy::TPathSelector>
  getCanonicalActiveCpsCriteria(const facebook::bgp::RibEntry& ribEntry) const;
  bool canonicalFailedCpsNativeCriteria(
      const facebook::bgp::RibEntry& ribEntry) const;

 protected:
  std::optional<neteng::fboss::bgp::thrift::TRibEntry>
  createTRibEntryWithFilter(
      const std::pair<const folly::CIDRNetwork, facebook::bgp::RibEntry>& entry,
      const std::function<bool(const RouteInfo&)>& pathFilter) override;

  /*
   * Whether CPS native criteria (e.g. bgp_native_path_selection_min_nexthop /
   * min_agg_lbw) are violated for this prefix -- when true, no path is "best
   * path". Used by both legacy Thrift entry construction and canonical entry
   * construction so the two surfaces apply the same demotion.
   */
  bool computeFailedCpsNativeCriteria(
      const facebook::bgp::RibEntry& ribEntry) const;
  std::unique_ptr<RibPolicyLogger> ribPolicyLogger_{nullptr};

  /*
   * Centralized Path Selection (CPS) policy — DC-only. Owned by RibDC
   * so the BB binary never compiles in CPS data or its
   * PathSelectionPolicy reads.
   */
  std::unique_ptr<PathSelectionPolicy> pathSelectionPolicy_{nullptr};

  /* Raw pointer owned by Main.cpp; lifetime managed externally. */
  FsdbSyncer* fsdbSyncer_{nullptr};

 private:
  // Whether the VRF's routes are published to canonical FSDB.
  static bool isVrfCanonicalFsdbCapable(VrfId vrfId) noexcept {
    return vrfId == fboss::kDefaultVrf;
  }

  /* FSDB reconnect notification and canonical update-queue wiring. */
  void configureCanonicalRibExport(FsdbSyncer& fsdbSyncer);
  folly::coro::Task<void> consumeCanonicalRibSnapshotRequests(
      std::shared_ptr<CanonicalRibSnapshotRequests> requests) noexcept;

  /*
   * Encoding shared by one-shot getters and continuous FSDB export.
   * includePath supports community-scoped getters; singlePath avoids an
   * all-path walk for best-path-only publication.
   */
  std::vector<CanonicalPathInput> buildCanonicalPathInputs(
      const RibEntry& ribEntry,
      bool failedCpsNativeCriteria,
      folly::FunctionRef<bool(const RouteInfo&)> includePath,
      RouteInfo* singlePath = nullptr);

  CanonicalEntryFields buildCanonicalEntryFields(const RibEntry& ribEntry);
  bool addCanonicalGetterEntry(
      CanonicalRibBuilder& builder,
      const folly::CIDRNetwork& prefix,
      const RibEntry& ribEntry,
      folly::FunctionRef<bool(const RouteInfo&)> includePath);

  /* Continuous FSDB export after the current RIB computation completes. */
  std::optional<CanonicalRibEntryInput> buildCanonicalRibEntryInput(
      const RibEntry& ribEntry);
  void enqueueCanonicalRibUpdatesAfterCompute(
      const folly::F14FastSet<folly::CIDRNetwork>& processedPrefixes) noexcept;
  void enqueueCanonicalRibFullSnapshot(std::string_view reason);

  /* Null when canonical RIB publication is disabled. */
  std::shared_ptr<CanonicalRibUpdateQueue> canonicalRibUpdateQueue_;
  /* A reconnect received while the current RIB state is unsafe to snapshot. */
  bool canonicalSnapshotPending_{false};

  /* DC-only CTE message handlers called from processRibPolicyMsgLoop. */
  void handleRouteAttributePolicySetMsg(
      const RouteAttributePolicySetMsg& msg) noexcept;
  void handleRouteAttributePolicyClearMsg() noexcept;
  void handleRouteAttributePolicyTimerMsg() noexcept;

  /*
   * Helper: iterate over the nexthop IPs in a NexthopResolutionUpdate,
   * look each up in conditionalLocalRoutes_, and invoke processMessage for
   * every (prefix, PrefixPathIds) pair that matches. Direct invocation (not
   * ribInQ_.push) avoids a deadlock — this is called from processRibInMsgLoop
   * which is the sole consumer of ribInQ_, so a push there would block
   * forever if the queue is full.
   *
   * TODO_NH_VRF: Look up the conditional local routes of every VRF.
   */
  template <typename MessageProcessor>
  void processConditionalRoutesForNexthops(
      const std::vector<folly::IPAddress>& nexthopIps,
      MessageProcessor&& processMessage) noexcept;

  /*
   * Set after the initial full RIB computation starts FsdbSyncer. Continuous
   * canonical updates are ignored until this becomes true.
   */
  bool fsdbSyncerStarted_{false};

  std::unique_ptr<folly::AsyncTimeout> routeAttributePolicyTimer_;

  /*
   * Whether CRF FILE_MODE is active. Atomic because it is accessed from two
   * threads: written on the Rib/startup thread (via setCrfFileModeEnabled()
   * during bootstrap) and read on the BgpService Thrift-handler thread (via
   * isCrfFileModeEnabled()) to gate out Thrift-based CRF updates while
   * file-mode owns the policy. The atomic prevents a data race between those
   * accesses; no stronger ordering is required since the flag is independent
   * of other state.
   */
  std::atomic<bool> crfFileModeEnabled_{false};

  /*
   * Whether CPS FILE_MODE is active. Atomic because it is accessed from two
   * threads: written on the Rib/startup thread (via setCpsFileModeEnabled()
   * during bootstrap) and read on the BgpService Thrift-handler thread (via
   * isCpsFileModeEnabled()) to gate out Thrift-based CPS updates while
   * file-mode owns the policy. The atomic prevents a data race between those
   * accesses; no stronger ordering is required since the flag is independent
   * of other state.
   */
  std::atomic<bool> cpsFileModeEnabled_{false};

  friend class RibFixture;
  friend class RibFsdbFixture;
  friend class MockRib;

  /*
   * Build a single TPartiallyDrainedPrefix from a (prefix, ribEntry) pair.
   * Reads only RibEntry/CIDRNetwork state. Used by the on-demand Thrift RPC
   * path getPartiallyDrainedPrefixes() and (transitively) by the
   * FSDB path enqueuePartialDrainState(). DC-only: partial drain is a
   * device concept, so this helper lives here rather than in RibBase.
   */
  neteng::fboss::bgp::thrift::TPartiallyDrainedPrefix
  buildPartialDrainPrefixEntry(
      const folly::CIDRNetwork& prefix,
      const RibEntry& ribEntry) const;

  bool isDevicePartiallyDrained() const;

  int64_t drainedPrefixCount_{0};
  int64_t partialDrainTransitionCount_{0};

  /*
   * Dirty bit gating the device partial-drain FSDB publish. Constructed true so
   * the first completed FIB pass publishes its computed state: true with the
   * affected prefixes if that pass enters partial drain, or false with an empty
   * prefix set otherwise. Set true again in runBestPathSelection() on each
   * drain transition; cleared once the latest state is handed to FsdbSyncer.
   */
  bool partialDrainEnqueuePending_{true};

/*
 * Per-class placeholder for test code injection. Test files that need to
 * reach DC-only members (e.g. pathSelectionPolicy_) #define this macro
 * with the appropriate FRIEND_TEST entries before including this header.
 */
#ifdef RibDC_TEST_FRIENDS
  RibDC_TEST_FRIENDS
#endif
};

template <typename MessageProcessor>
void RibDC::processConditionalRoutesForNexthops(
    const std::vector<folly::IPAddress>& nexthopIps,
    MessageProcessor&& processMessage) noexcept {
  XLOGF(
      INFO, "Processing conditional routes for {} nexthops", nexthopIps.size());
  const auto& conditionalLocalRoutes = defaultVrf_.conditionalLocalRoutes();
  for (const auto& ipaddr : nexthopIps) {
    auto it = conditionalLocalRoutes.find(ipaddr);
    if (it != conditionalLocalRoutes.end()) {
      for (const auto& prefix : it->second) {
        PrefixPathIds pfxPathIds{{prefix, kDefaultPathID}};
        processMessage(prefix, std::move(pfxPathIds));
      }
    }
  }
}

} // namespace facebook::bgp
