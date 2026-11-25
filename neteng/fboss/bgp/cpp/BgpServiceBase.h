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

#include <chrono>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <folly/IPAddress.h>
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/coro/Task.h>
#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/rib_policy_types.h"
#include "neteng/fboss/bgp/cpp/config/Config.h"
#include "neteng/fboss/bgp/cpp/config/ConfigManager.h"
#include "neteng/fboss/bgp/cpp/health/HealthValidator.h"
#include "neteng/fboss/bgp/cpp/peer/PeerManagerBase.h"
#include "neteng/fboss/bgp/cpp/peer/SessionManager.h"
#include "neteng/fboss/bgp/cpp/rib/RibBase.h"
#include "neteng/fboss/bgp/cpp/watchdog/Watchdog.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"
#include "neteng/fboss/bgp/if/gen-cpp2/policy_thrift_types.h"

namespace facebook::bgp {

using facebook::neteng::fboss::bgp::thrift::BgpInitializationEvent;
using BgpInitializationMap =
    std::unordered_map<BgpInitializationEvent, int64_t>;

class RejectedNetworksAccumulator;

class BgpServiceBase
    : public facebook::neteng::fboss::bgp::thrift::TBgpServiceSvIf {
 public:
  BgpServiceBase(
      PeerManagerBase& peerMgr,
      std::shared_ptr<ConfigManager> configManager,
      RibBase& rib,
      Watchdog& watchdog,
      bool enable_thrift_protection);
  ~BgpServiceBase() override = default;

  /* [Peer and peer-group routing policies] */
  folly::coro::Task<facebook::neteng::fboss::bgp::thrift::BgpPolicyChangeResult>
  co_setPeersPolicy(
      std::unique_ptr<std::map<
          std::string,
          std::map<facebook::bgp::bgp_policy::DIRECTION, std::string>>>
          peersPolicy) override;

  folly::coro::Task<facebook::neteng::fboss::bgp::thrift::BgpPolicyChangeResult>
  co_setPeerGroupsPolicy(
      std::unique_ptr<std::map<
          std::string,
          std::map<facebook::bgp::bgp_policy::DIRECTION, std::string>>>
          peerGroupsPolicy) override;

  folly::coro::Task<facebook::neteng::fboss::bgp::thrift::BgpPolicyChangeResult>
  co_unsetPeersPolicy(
      std::unique_ptr<
          std::map<std::string, std::set<facebook::bgp::bgp_policy::DIRECTION>>>
          peersToUnset) override;

  /**
   * [Logging]
   *
   * Dynamically set log level for BGP (i.e., change log level without
   * restarting BGP)
   *
   * @param levelString: string representation of log level
   *                     (e.g. ".=DBG1,foo.bar=INFO")
   */
  void setLogLevel(std::unique_ptr<std::string> levelString) override;

  /*
   * [Config]
   *
   * Config (same file includes Policy) parsing with sanity checks
   */
  void validateConfig(
      neteng::fboss::bgp::thrift::TResult& ret,
      std::unique_ptr<std::string> file_name) override;

  /*
   * [Config]
   *
   * Parse and Sanity check Config and Policy as separate artifacts
   */
  void validateConfigAndPolicy(
      neteng::fboss::bgp::thrift::TResult& ret,
      std::unique_ptr<std::string> config_file_name,
      std::unique_ptr<std::string> policy_file_name) override;

  /*
   * [Config]
   *
   * Expose BGP++ node drain state from run time
   */
  void getDrainState(neteng::fboss::bgp::thrift::TBgpDrainState& ret) override;

  /*
   * [Initialization]
   *
   * Check if BGP++ mark itself converged.
   */
  bool initializationConverged() override;

  /*
   * [Initialization]
   *
   * Fetch the initialization event and its corresponding time duration.
   */
  void getInitializationEvents(BgpInitializationMap& _return) override;

  /*
   * [Initialization]
   *
   * Fetch the timestamp of last programmed(FIB-ACKed) routes
   * ATTN: return negative value when there is NO fib update(e.g.
   * initialization)
   */
  int64_t getTimeElapsedSinceLastFibUpdate() override;

  /*
   * Get the current RIB version: PeerManager's maxRibVersion_, which is the
   * counter the shadow RIB, the change list and every peer's cached version
   * are stamped from -- so it is the sequence TBgpSession.rib_version can be
   * compared against. The loc-RIB keeps its own counter, but the RIB does not
   * yet guarantee it increases monotonically across separately flushed
   * RibOut buffers, so it is not what we report. maxRibVersion_ is confined
   * to the PeerManager event base, so this handler reads it through a
   * timeout-protected evb hop (co_runOnEvbWithTimeout, mirroring
   * co_getRibSummary). Returns -1 if the value is unavailable (session
   * exiting, or the evb hop timed out / failed); real versions are
   * non-negative.
   */
  folly::coro::Task<int64_t> co_getRibVersion() override;

  /*
   * Get the total number of prefixes currently installed in the loc-RIB
   * (number of RibVrf::ribEntries_), read from the RIB's ribCounters_ through
   * a timeout-protected evb hop (co_runOnEvbWithTimeout, mirroring
   * co_getRibSummary). Returns -1 if the value is unavailable (session
   * exiting, or the evb hop timed out / failed); real counts are non-negative.
   * For the per-family (IPv4 / IPv6) split, use getRibSummary (surfaced via
   * "show bgp table summary").
   */
  folly::coro::Task<int64_t> co_getNumPrefixes() override;

  /*
   * Get the BGP++ process uptime in seconds, sourced from the Watchdog
   * process start time.
   */
  int64_t getProcessUptimeSeconds() override;

  // bgp originated-routes
  folly::coro::Task<std::unique_ptr<
      std::vector<neteng::fboss::bgp::thrift::TOriginatedRoute>>>
  co_getOriginatedRoutes() override;

  // changeList
  folly::coro::Task<std::unique_ptr<
      std::vector<facebook::neteng::fboss::bgp::thrift::TRibEntry>>>
  co_getChangeListEntries(
      facebook::neteng::fboss::bgp_attr::TBgpAfi afi) override;

  folly::coro::Task<
      std::unique_ptr<facebook::neteng::fboss::bgp::thrift::TCanonicalRibState>>
  co_getChangeListEntriesCanonical(
      facebook::neteng::fboss::bgp_attr::TBgpAfi afi) override;

  // shadow RiB
  folly::coro::Task<std::unique_ptr<
      std::vector<facebook::neteng::fboss::bgp::thrift::TRibEntry>>>
  co_getShadowRibEntries(
      facebook::neteng::fboss::bgp_attr::TBgpAfi afi) override;

  folly::coro::Task<
      std::unique_ptr<facebook::neteng::fboss::bgp::thrift::TCanonicalRibState>>
  co_getShadowRibEntriesCanonical(
      facebook::neteng::fboss::bgp_attr::TBgpAfi afi) override;

  /* egress stats */
  folly::coro::Task<std::unique_ptr<
      std::vector<facebook::neteng::fboss::bgp::thrift::TPeerEgressStats>>>
  co_getPeerEgressStats() override;

  /* update group info */
  folly::coro::Task<std::unique_ptr<
      facebook::neteng::fboss::bgp::thrift::TGetUpdateGroupSummariesResponse>>
  co_getUpdateGroupSummaries() override;

  folly::coro::Task<std::unique_ptr<
      facebook::neteng::fboss::bgp::thrift::TGetUpdateGroupInfoResponse>>
  co_getUpdateGroupInfo(
      std::unique_ptr<
          facebook::neteng::fboss::bgp::thrift::TGetUpdateGroupInfoRequest>
          request) override;

  // bgp table
  folly::coro::Task<std::unique_ptr<
      std::vector<facebook::neteng::fboss::bgp::thrift::TRibEntry>>>
  co_getRibEntries(facebook::neteng::fboss::bgp_attr::TBgpAfi afi) override;

  /**
   * Return compact FIB-out state for one exact prefix.
   *
   * The coroutine propagates exit, admission, timeout, and RIB failures to the
   * client. Only a successful response can report tracking as disabled.
   */
  folly::coro::Task<
      std::unique_ptr<facebook::neteng::fboss::bgp::thrift::TFibOutTable>>
  co_getFibOutPrefix(
      std::unique_ptr<
          facebook::neteng::fboss::bgp::thrift::TFibOutPrefixRequest> request)
      override;

  folly::coro::Task<
      std::unique_ptr<facebook::neteng::fboss::bgp::thrift::TRibSummary>>
  co_getRibSummary(facebook::neteng::fboss::bgp_attr::TBgpAfi afi) override;

  /**
   * Return canonical FIB-out nexthop sets and their route reference counts.
   *
   * The RIB traverses its event-base-owned canonical-set registry. Work grows
   * with unique complete sets and does not scan RIB entries.
   */
  folly::coro::Task<std::unique_ptr<
      facebook::neteng::fboss::bgp::thrift::TFibNexthopDatabase>>
  co_getFibNexthopDatabase() override;

  folly::coro::Task<std::unique_ptr<
      std::vector<facebook::neteng::fboss::bgp::thrift::TRibEntry>>>
  co_getRibEntriesForCommunity(
      facebook::neteng::fboss::bgp_attr::TBgpAfi afi,
      std::unique_ptr<std::string> community) override;

  folly::coro::Task<std::unique_ptr<
      std::vector<facebook::neteng::fboss::bgp::thrift::TRibEntry>>>
  co_getRibEntriesForCommunities(
      facebook::neteng::fboss::bgp_attr::TBgpAfi afi,
      std::unique_ptr<std::vector<std::string>> communities) override;

  folly::coro::Task<std::unique_ptr<
      std::vector<facebook::neteng::fboss::bgp::thrift::TRibEntry>>>
  co_getRibPrefix(std::unique_ptr<std::string> prefix) override;

  folly::coro::Task<std::unique_ptr<
      std::vector<facebook::neteng::fboss::bgp::thrift::TRibEntry>>>
  co_getRibSubprefixes(std::unique_ptr<std::string> prefix) override;

  folly::coro::Task<
      std::unique_ptr<facebook::neteng::fboss::bgp::thrift::TEntryStats>>
  co_getEntryStats() override;

  /**
   * [Route Filter Policy]
   */
  folly::coro::Task<std::unique_ptr<neteng::fboss::bgp::thrift::TResult>>
  co_setRouteFilterPolicy(
      std::unique_ptr<rib_policy::TRouteFilterPolicy> policy) override;

  folly::coro::Task<std::unique_ptr<rib_policy::TRouteFilterPolicy>>
  co_getRouteFilterPolicy() override;

  folly::coro::Task<void> co_clearRouteFilterPolicy() override;

  /*
   * [Watchdog]
   */
  void getMonitoredQueueSizes(
      QueueSizeMapT& ret,
      std::unique_ptr<std::vector<std::string>> paths) override;

  /*
   * [Nexthop Info]
   */
  folly::coro::Task<std::unique_ptr<neteng::fboss::bgp::thrift::TNexthopInfo>>
  co_getNexthopInfoForNexthop(std::unique_ptr<std::string> nexthop) override;

  folly::coro::Task<
      std::unique_ptr<std::vector<neteng::fboss::bgp::thrift::TNexthopInfo>>>
  co_getNexthopInfos(
      std::unique_ptr<std::vector<std::string>> nexthops) override;

  folly::coro::Task<
      std::unique_ptr<facebook::neteng::fboss::bgp::thrift::TAttributeStats>>
  co_getAttributeStatsFiltered(
      std::unique_ptr<
          facebook::neteng::fboss::bgp::thrift::TAttributeStatsFilter> filter)
      override;

  /*
   * [Profiler]
   */
  void startProfiler(bool enable) override;

  void setProfilerFilter(std::unique_ptr<std::string> regex) override;

  void getProfilerStats(
      std::vector<facebook::neteng::fboss::bgp::thrift::TBgpProfilerStat>&
          _return) override;

  void clearProfilerStats() override;

  folly::coro::Task<void> co_clearCounters(
      std::unique_ptr<std::vector<std::string>> peers) override;

  /**
   * [Health]
   *
   * Run all registered health checks and return a comprehensive
   * health report.
   */
  folly::coro::Task<std::unique_ptr<neteng::fboss::bgp::thrift::THealthReport>>
  co_getHealthReport() override;

  // Get running config
  void getRunningConfig(std::string& configStr) override;

  // Get running config as Thrift struct
  void getRunningConfigStruct(thrift::BgpConfig& config) override;

  // Has policy config artifact
  bool hasPolicySymlink() override;

  // Get policy config
  void getPolicyConfig(std::string& configStr) override;

  // bgp summary
  folly::coro::Task<
      std::unique_ptr<std::vector<neteng::fboss::bgp::thrift::TBgpSession>>>
  co_getBgpSessions() override;

  // bgp stream summary
  folly::coro::Task<std::unique_ptr<
      std::vector<neteng::fboss::bgp::thrift::TBgpStreamSession>>>
  co_getBgpStreamSessions() override;

  // bgp neighbors
  folly::coro::Task<
      std::unique_ptr<std::vector<neteng::fboss::bgp::thrift::TBgpSession>>>
  co_getBgpNeighbors(
      std::unique_ptr<std::vector<std::string>> peerAddresses) override;

  // bgp neighbors from session (coro)
  folly::coro::Task<
      std::unique_ptr<std::vector<neteng::fboss::bgp::thrift::TBgpSession>>>
  co_getBgpNeighborsFromSession(
      std::unique_ptr<std::string> peerId,
      std::unique_ptr<std::string> sessionBgpId) override;

  // hold timer info
  folly::coro::Task<
      std::unique_ptr<std::vector<neteng::fboss::bgp::thrift::THoldTimerInfo>>>
  co_getHoldTimers(
      std::unique_ptr<std::vector<std::string>> peerAddresses) override;

  // bgp local config
  void getBgpLocalConfig(
      neteng::fboss::bgp::thrift::TBgpLocalConfig& retConfig) override;

  // bgp prefilter-received
  folly::coro::Task<std::unique_ptr<std::map<
      facebook::neteng::fboss::bgp_attr::TIpPrefix,
      facebook::neteng::fboss::bgp::thrift::TBgpPath>>>
  co_getPrefilterReceivedNetworks(std::unique_ptr<std::string> peer) override;

  // bgp prefilter-received with add path
  folly::coro::Task<std::unique_ptr<std::map<
      facebook::neteng::fboss::bgp_attr::TIpPrefix,
      std::vector<facebook::neteng::fboss::bgp::thrift::TBgpPath>>>>
  co_getPrefilterReceivedNetworks2(std::unique_ptr<std::string> peer) override;

  folly::coro::Task<std::unique_ptr<std::map<
      facebook::neteng::fboss::bgp_attr::TIpPrefix,
      facebook::neteng::fboss::bgp::thrift::TBgpPath>>>
  co_getPrefilterReceivedNetworksFromSession(
      std::unique_ptr<std::string> peer,
      std::unique_ptr<std::string> sessionBgpId) override;

  folly::coro::Task<std::unique_ptr<std::map<
      facebook::neteng::fboss::bgp_attr::TIpPrefix,
      std::vector<facebook::neteng::fboss::bgp::thrift::TBgpPath>>>>
  co_getPrefilterReceivedNetworksFromSession2(
      std::unique_ptr<std::string> peer,
      std::unique_ptr<std::string> sessionBgpId) override;

  // bgp postfilter-received
  folly::coro::Task<std::unique_ptr<std::map<
      facebook::neteng::fboss::bgp_attr::TIpPrefix,
      facebook::neteng::fboss::bgp::thrift::TBgpPath>>>
  co_getPostfilterReceivedNetworks(std::unique_ptr<std::string> peer) override;

  // bgp postfilter-received with add path
  folly::coro::Task<std::unique_ptr<std::map<
      facebook::neteng::fboss::bgp_attr::TIpPrefix,
      std::vector<facebook::neteng::fboss::bgp::thrift::TBgpPath>>>>
  co_getPostfilterReceivedNetworks2(std::unique_ptr<std::string> peer) override;

  folly::coro::Task<std::unique_ptr<std::map<
      facebook::neteng::fboss::bgp_attr::TIpPrefix,
      facebook::neteng::fboss::bgp::thrift::TBgpPath>>>
  co_getPostfilterReceivedNetworksFromSession(
      std::unique_ptr<std::string> peer,
      std::unique_ptr<std::string> sessionBgpId) override;

  folly::coro::Task<std::unique_ptr<std::map<
      facebook::neteng::fboss::bgp_attr::TIpPrefix,
      std::vector<facebook::neteng::fboss::bgp::thrift::TBgpPath>>>>
  co_getPostfilterReceivedNetworksFromSession2(
      std::unique_ptr<std::string> peer,
      std::unique_ptr<std::string> sessionBgpId) override;

  // bgp prefilter-advertised
  folly::coro::Task<std::unique_ptr<std::map<
      facebook::neteng::fboss::bgp_attr::TIpPrefix,
      facebook::neteng::fboss::bgp::thrift::TBgpPath>>>
  co_getPrefilterAdvertisedNetworks(std::unique_ptr<std::string> peer) override;

  // bgp prefilter-advertised with add path
  folly::coro::Task<std::unique_ptr<std::map<
      facebook::neteng::fboss::bgp_attr::TIpPrefix,
      std::vector<facebook::neteng::fboss::bgp::thrift::TBgpPath>>>>
  co_getPrefilterAdvertisedNetworks2(
      std::unique_ptr<std::string> peer) override;

  // bgp postfilter-advertised
  folly::coro::Task<std::unique_ptr<std::map<
      facebook::neteng::fboss::bgp_attr::TIpPrefix,
      facebook::neteng::fboss::bgp::thrift::TBgpPath>>>
  co_getPostfilterAdvertisedNetworks(
      std::unique_ptr<std::string> peer) override;

  // bgp postfilter-advertised with add path
  folly::coro::Task<std::unique_ptr<std::map<
      facebook::neteng::fboss::bgp_attr::TIpPrefix,
      std::vector<facebook::neteng::fboss::bgp::thrift::TBgpPath>>>>
  co_getPostfilterAdvertisedNetworks2(
      std::unique_ptr<std::string> peer) override;

  /*
   * Per-policy rollup of the routes policy denied across the matching
   * neighbors, aggregated on the device. Unlike the prefilter/postfilter pairs
   * above, the response is proportional to the number of rejecting policies
   * rather than to RIB size.
   *
   * Hops onto the PeerManager event base once per peer rather than once for
   * the whole scan, so a wide request cannot monopolize that event base.
   */
  folly::coro::Task<std::unique_ptr<
      facebook::neteng::fboss::bgp::thrift::TRejectedNetworksSummaryResponse>>
  co_getRejectedNetworksSummary(
      std::unique_ptr<
          facebook::neteng::fboss::bgp::thrift::TRejectedNetworksRequest>
          request) override;

  /* As above, plus the per-(prefix, policy) breakdown. */
  folly::coro::Task<std::unique_ptr<
      facebook::neteng::fboss::bgp::thrift::TRejectedNetworksDetailsResponse>>
  co_getRejectedNetworksDetails(
      std::unique_ptr<
          facebook::neteng::fboss::bgp::thrift::TRejectedNetworksRequest>
          request) override;

  folly::coro::Task<std::unique_ptr<std::map<
      facebook::neteng::fboss::bgp_attr::TIpPrefix,
      std::vector<facebook::neteng::fboss::bgp::thrift::TBgpPath>>>>
  co_getSubscriberNetworkInfo(
      int32_t peerID,
      std::unique_ptr<std::string> policy_type) override;

  // used by fbossdeploy
  folly::coro::Task<std::unique_ptr<std::map<
      facebook::neteng::fboss::bgp_attr::TIpPrefix,
      facebook::neteng::fboss::bgp::thrift::TBgpNetwork>>>
  co_getAdvertisedNetworksFiltered(
      std::unique_ptr<std::string> peer,
      std::unique_ptr<std::vector<facebook::neteng::fboss::bgp_attr::TIpPrefix>>
          prefixes) override;

  // used by fcr
  folly::coro::Task<std::unique_ptr<
      std::vector<facebook::neteng::fboss::bgp::thrift::TBgpNetwork>>>
  co_getReceivedNetworks(std::unique_ptr<std::string> peer) override;

  folly::coro::Task<std::unique_ptr<
      std::vector<facebook::neteng::fboss::bgp::thrift::TBgpNetwork>>>
  co_getAdvertisedNetworks(std::unique_ptr<std::string> peer) override;

  // used by fboss/bgp/consistency_check
  folly::coro::Task<void> co_shutdownSession(
      std::unique_ptr<std::string> peer) override;

  folly::coro::Task<void> co_restartSession(
      std::unique_ptr<std::string> peer) override;

  folly::coro::Task<void> co_startSession(
      std::unique_ptr<std::string> peer) override;

  folly::coro::Task<void> co_clearBgpNeighbor(
      std::unique_ptr<std::string> peer,
      facebook::neteng::fboss::bgp::thrift::ClearBgpNeighborDirection direction,
      facebook::neteng::fboss::bgp_attr::TBgpAfi afi) override;

  /*
   * Non-handler entry point with the actual clearBgpNeighbor logic. Tests
   * must call this instead of co_clearBgpNeighbor (CLANGTIDY rule
   * facebook-thrift-handler-direct-call forbids direct invocation of
   * generated handler methods).
   */
  folly::coro::Task<void> co_clearBgpNeighborImpl(
      std::unique_ptr<std::string> peer,
      facebook::neteng::fboss::bgp::thrift::ClearBgpNeighborDirection direction,
      facebook::neteng::fboss::bgp_attr::TBgpAfi afi);

  /*
   * Resolve exactly one established peerId for the given address. Throws
   * TApplicationException if there are zero or multiple matches. Must be
   * called on the PeerManager EventBase thread.
   */
  nettools::bgplib::BgpPeerId resolveSingleEstablishedPeerId(
      const folly::IPAddress& peerAddr);

  void changeSessionStateHelper(
      const std::string& peer,
      std::function<void(folly::CIDRNetwork)> fnNetwork,
      std::function<void(folly::IPAddress)> fnIpaddress);

  folly::coro::Task<
      std::unique_ptr<facebook::neteng::fboss::bgp::thrift::TAttributeStats>>
  co_getAttributeStats() override;

  folly::coro::Task<std::unique_ptr<
      facebook::neteng::fboss::bgp::thrift::TGetDeduplicatorStatsResponse>>
  co_getDeduplicatorStats(
      std::unique_ptr<
          facebook::neteng::fboss::bgp::thrift::TGetDeduplicatorStatsRequest>
          request) override;

  folly::coro::Task<std::unique_ptr<
      facebook::neteng::fboss::bgp::thrift::TGetAdjRibStatsResponse>>
  co_getAdjRibStats(
      std::unique_ptr<
          facebook::neteng::fboss::bgp::thrift::TGetAdjRibStatsRequest> request)
      override;

  folly::coro::Task<
      std::unique_ptr<facebook::neteng::routing::policy::thrift::TPolicyStats>>
  co_getPolicyStats() override;

  void setDebugLevel(
      facebook::neteng::fboss::bgp::thrift::TBgpDebugLevel level) override;

  /*
   * Every thrift API call MUST go through this gate-keeper
   */
  bool continueExecution(bool allowSuspend) noexcept;

  uint32_t numRequestsInExecution() {
    return requestsInExecution_;
  }

  uint32_t getAllowedBufferWindow() {
    return allowedWindow_;
  }

  uint32_t getRejectRequestWindow() {
    return rejectWindow_;
  }

  void setRejectRequestWindow(const uint32_t windowInMs) {
    rejectWindow_ = windowInMs;
  }

  uint32_t getIdleWindow_() {
    return idleWindow_;
  }

  void incrRequestsInExecution() {
    requestsInExecution_++;
  }

  void decrRequestsInExecution(void) {
    if (requestsInExecution_) {
      requestsInExecution_--;
    }

    lastRequestCompletionTime_ =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
  }

  bool rejectRequest(const uint32_t curTime, const uint32_t windowStartTime) {
    /*
     * enough time spent waiting to process a specific request?, give up
     */
    return ((curTime - windowStartTime) > rejectWindow_);
  }

  bool canStartNewRequestsWindow(const uint32_t curTime) {
    if (numRequestsInExecution()) {
      return false;
    }

    /**
     * Allow yield before batch of new requests can get into execution
     */
    if ((curTime - lastRequestCompletionTime_) > idleWindow_) {
      firstInwindowRequestTime_ = curTime;
      return true;
    }

    return false;
  }

  bool canAbsorbRequestInCurrentWindow(const uint32_t curTime) {
    /**
     * Allow yield before batch of new requests can get into execution
     */
    return ((curTime - firstInwindowRequestTime_) < allowedWindow_);
  }

  bool isThriftProtectionEnabled() {
    return thriftProtectionEnabled_;
  }

  bool setThriftProtection(bool enable) {
    return thriftProtectionEnabled_ = enable;
  }

 protected:
  /**
   * Hook for subclasses to augment per-platform fields of TEntryStats.
   * Invoked from co_getEntryStats() AFTER the exitInitiated_ and
   * continueExecution(true) guards have passed and the rib-derived counters
   * have been populated. Subclasses that need to layer additional data (e.g.
   * NetlinkWrapper kernel-FIB counters on BB) override this hook rather than
   * the Thrift handler method, which avoids the
   * facebook-thrift-handler-direct-call lint and preserves the original
   * pre-split behavior where the augmentation only runs under the same guard
   * conditions as the base implementation.
   */
  virtual void augmentEntryStatsForPlatform(
      facebook::neteng::fboss::bgp::thrift::TEntryStats& /* stats */) {}

  std::atomic<bool> exitInitiated_{false};
  PeerManagerBase& peerMgr_;
  const std::shared_ptr<SessionManager>& sessionMgr_;
  std::shared_ptr<ConfigManager> configManager_;
  RibBase& rib_;
  Watchdog& watchdog_;
  std::unique_ptr<HealthValidator> healthValidator_;

  /**
   * Current number of active requests received by BgpService and
   * already in the execution. Suspended ones, waiting to start
   * execution are not accounted
   */
  std::atomic<uint32_t> requestsInExecution_{0};
  /**
   * The reference time of a request that successfully started
   * execution in the window that is considered in the formula
   * to allow or suspend subsequent requests
   */
  std::atomic<uint32_t> firstInwindowRequestTime_{0};
  /**
   * Time when last request successfully completed execution
   */
  std::atomic<uint32_t> lastRequestCompletionTime_{0};

  /*
   * Allow atleast few ms gap after completing last batch of requests
   */
  uint32_t idleWindow_{100};

  /*
   * If new request is within this window, entertain servicing this request
   * otherwise yield
   */
  uint32_t allowedWindow_{5000};

  /*
   * Maximum time a request will wait while yielding, reject request after that
   * if request still can not be served due to dampening
   *
   * Until all the thrift endpoints support failure return status, keep this
   * window sufficiently large just to avoid suspending service forever.
   */
  uint32_t rejectWindow_{120000};

  bool thriftProtectionEnabled_{false};

#ifdef BgpServiceBase_TEST_FRIENDS
  BgpServiceBase_TEST_FRIENDS
#endif
};

/*
 * The per-peer filters of a getRejectedNetworks* request. Held by value, with
 * the regex behind a shared_ptr, so an event-base lambda can own a copy that
 * outlives the coroutine that built it.
 */
struct RejectedNetworksWalk {
  facebook::neteng::fboss::bgp::thrift::TDirectionFilter direction{
      facebook::neteng::fboss::bgp::thrift::TDirectionFilter::EGRESS};
  std::shared_ptr<re2::RE2> policyFilter;
  std::set<facebook::neteng::fboss::bgp_attr::TIpPrefix> prefixFilter;
  std::optional<facebook::neteng::fboss::bgp_attr::TBgpAfi> afi;
};

/*
 * Walks each of `peerIds` on the PeerManager event base, one hop per peer,
 * feeding the results into `accumulator`. Each hop is bounded by `timeout`.
 * The first hop to time out ends the walk: it and every peer not yet walked
 * are reported as failed rather than queued behind an event base that is not
 * keeping up.
 *
 * The getRejectedNetworks* handlers pass kPeerMgrThriftHandlerTimeout; the
 * timeout is a parameter so a test can drive a wedged event base.
 */
folly::coro::Task<void> co_walkRejectedNetworkPeers(
    PeerManagerBase& peerMgr,
    const std::vector<nettools::bgplib::BgpPeerId>& peerIds,
    const RejectedNetworksWalk& walk,
    RejectedNetworksAccumulator& accumulator,
    std::chrono::milliseconds timeout);

} // namespace facebook::bgp
