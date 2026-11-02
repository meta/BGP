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

#include <string_view>

#include <folly/FixedString.h>

#include <fb303/ThreadCachedServiceData.h>
#include <fb303/detail/QuantileStatWrappers.h>
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

// see comment about exit-in-progress below
DECLARE_string(exit_in_progress_file);

namespace facebook::bgp {
using folly::string_literals::operator""_fs;

//------------------------ BgpStats ------------------------//

// Global BGP level stats
namespace BgpStats {
constexpr auto kNoPrefixSent = "bgpd.noPrefixSent"_fs;
constexpr auto kConfiguredPeers = "bgpd.configuredPeers"_fs;
// Peer groups that currently have at least one Established session
constexpr auto kNumLivePeerGroups = "bgpd.num_live_peer_groups"_fs;
constexpr auto kPolicySymlink = "bgpd.policySymlink"_fs;
constexpr auto kStatefulGR = "bgpd.statefulGR"_fs;
constexpr auto kEorTimerExpired = "bgpd.eorTimerExpired"_fs;
constexpr auto kNonGraceful = "bgpd.nonGraceful.{}";
// All running BGP sessions
constexpr auto kRunningSessions = "bgpd.runningSessions"_fs;
constexpr auto kOpenRejectAsnMismatch = "bgpd.open_reject.asn_mismatch"_fs;
// number of BGP session flaps
constexpr auto kSessionStateChanges = "bgpd.sessionStateChanges"_fs;
// Total VIP sessions which are UP
constexpr auto kRunningVipSessions = "bgpd.runningVipSessions"_fs;
// Initial convergence time in milliseconds
constexpr auto kConvergenceTime = "bgpd.convergenceTimeMs"_fs;
// whether ucmp is active on at least 1 route
constexpr auto kUcmpActive = "bgpd.ucmp.active"_fs;

// whether PeerManagerBase reaches the initialized_ timeout or not
constexpr auto kInitializedTimeout = "bgpd.initialized.timeout"_fs;

// Counter for how many times fast tear down is triggered on current node.
constexpr auto kDsfFastTearDownCount = "bgpd.dsfFastTearDownCount"_fs;

/*
 * Policy Match hit BGP route with AS_SET or AS_CONFED_SET
 */
constexpr auto kPolicyAsSetHitCount = "bgpd.policyAsSetHit"_fs;
constexpr auto kPolicyAsConfedSetHitCount = "bgpd.policyAsConfedSetHit"_fs;
constexpr auto kPolicyNewAsMatchFailCount = "bgpd.policyNewAsMatchFailCount"_fs;

/*
 * AdjRibPolicyCache related counters
 */
constexpr auto kPolicyCacheHitCount = "bgpd.policy_cache.hit"_fs;
constexpr auto kPolicyCacheMissCount = "bgpd.policy_cache.miss"_fs;
constexpr auto kPolicyCacheNumEntries = "bgpd.policy_cache.num_entries"_fs;
constexpr auto kPolicyCacheMemoryUsage = "bgpd.policy_cache.memory_usage"_fs;

// Number of entries in each deduplicated attributes collection
inline const auto kDeduplicatedAttributesTotal =
    fmt::format("{}.deduplicated_attributes.total", kBgpcppTag);
inline const auto kDeduplicatedAttributesAsPath =
    fmt::format("{}.deduplicated_attributes.as_path", kBgpcppTag);
inline const auto kDeduplicatedAttributesCommunities =
    fmt::format("{}.deduplicated_attributes.communities", kBgpcppTag);
inline const auto kDeduplicatedAttributesClusterList =
    fmt::format("{}.deduplicated_attributes.cluster_list", kBgpcppTag);
inline const auto kDeduplicatedAttributesExtCommunities =
    fmt::format("{}.deduplicated_attributes.ext_communities", kBgpcppTag);
inline const auto kDeduplicatedAttributesBgpPath =
    fmt::format("{}.deduplicated_attributes.bgp_path", kBgpcppTag);

// Configured switch prefix limits
constexpr auto kUniquePrefixLimit = "bgpd.unique_prefix_limit"_fs;
constexpr auto kTotalPathLimit = "bgpd.total_path_limit"_fs;
constexpr auto kOverloadProtectionMode = "bgpd.overload_protection_mode"_fs;

// whether safe mode is on or not
constexpr auto kIsSafeModeOn = "bgpd.isSafeModeOn"_fs;

constexpr auto kThriftReject = "bgpd.thriftReject"_fs;
constexpr auto kThriftSuspend = "bgpd.thriftSuspend"_fs;

/*
 * Watchdog related counters
 */
constexpr auto kRibInQueueSize = "bgpd.watchdog.rib_in_queue_size"_fs;
constexpr auto kRibOutQueueSize = "bgpd.watchdog.rib_out_queue_size"_fs;

/*
 * Bumped when a neighbor entry's portId and state fields disagree
 * (portId != 0 but state != Reachable, or vice versa); see
 * FsdbNeighborWatcher::isNeighborResolved.
 */
constexpr auto kNeighborPortIdStateMismatch =
    "bgpd.neighbor.portid_state_mismatch"_fs;

/**
 * This counter tracks the number of times streaming sessions are rejected,
 * due to being over configured limit.
 */
inline constexpr auto kStreamingSessionsRejected =
    "bgpd.streamingSessionsRejected";

// Watchdog counter to indicate how many times the queue size check has run
constexpr auto kWatchdogNumQueueSizeCheck =
    "bgpd.watchdog.num_queue_size_check"_fs;

// number of peers affected by ingress route filter policy updates
inline constexpr auto kIngressRouteFilterPolicyAffectedPeers =
    "bgpd.policy.route_filter.num_affected_peers_ingress";

// number of peers affected by ingress routing policy updates
inline constexpr auto kIngressRoutingPolicyAffectedPeers =
    "bgpd.policy.routing_policy.num_affected_peers_ingress";

// number of peers affected by egress route filter policy updates
inline constexpr auto kEgressRouteFilterPolicyAffectedPeers =
    "bgpd.policy.route_filter.num_affected_peers_egress";

// number of peers affected by egress routing policy updates
inline constexpr auto kEgressRoutingPolicyAffectedPeers =
    "bgpd.policy.routing_policy.num_affected_peers_egress";

// time for ingress CRF re-evaluation per peer group
inline constexpr auto kIngressRouteFilterPeerGroupProcessTimeMs =
    "{}.{}.policy.route_filter.ingress_processing_time_ms.{}";
DECLARE_dynamic_quantile_stat(ingressCrfProcessingTimeMs, 3);

// duration of the most recent ingress policy re-evaluation across all peers
inline constexpr auto kIngressPolicyAllPeersLastReEvaluationTimeMs =
    "{}.policy.ingress_all_peers_last_reevaluation_time_ms";

/*
 * Egress backpressure related counters.
 */
/*
 * Number of times a route update for a prefix changed without being
 * advertised to peer. This is taken as the sum across all AdjRibs.
 */
DECLARE_timeseries(egress_transient_route_updates_suppressed);
constexpr auto kEgressTransientUpdatesSuppressed =
    "bgpd.egress_transient_route_updates_suppressed"_fs;

/*
 * RFC 1997 well-known community egress suppression counters.
 *
 * Increment semantics differ between the two egress paths:
 *
 *   Per-peer path (AdjRib::canAnnounceEntry without update-grouping):
 *     Incremented once per (prefix, peer) suppression decision. For a
 *     route with N peers that all reject it, the counter advances by N.
 *
 *   Update-group path (AdjRibOutGroup::canAnnounceForGroup, reached when
 *   enableUpdateGroup_ is true):
 *     The group-level check is invoked once per (prefix, group) from the
 *     group's distribution loop (AdjRibOutGroup::processBuildAdjRibOutEntry
 *     -> canAnnounceForGroup). A group representing M peers suppresses the
 *     route once, advancing the counter by 1 for the entire group rather
 *     than by M. Per-peer fan-out into canAnnounceForGroup via
 *     AdjRib::canAnnounceEntry may add additional increments depending on
 *     enforcement points in the call graph; consumers reading these
 *     counters for "how many advertisements got suppressed" should treat
 *     the value as a lower bound that scales with affected groups, not
 *     with affected peers.
 *
 * Alerting on absolute thresholds is therefore unsafe across rollouts that
 * toggle update-grouping. Prefer relative-rate alerts (e.g., delta vs.
 * prior hour) which behave consistently under both topologies.
 */
DECLARE_timeseries(well_known_community_no_advertise_suppressed);
DECLARE_timeseries(well_known_community_no_export_suppressed);
DECLARE_timeseries(well_known_community_no_export_subconfed_suppressed);
constexpr auto kWellKnownCommunityNoAdvertiseSuppressed =
    "bgpd.well_known_community.no_advertise_suppressed"_fs;
constexpr auto kWellKnownCommunityNoExportSuppressed =
    "bgpd.well_known_community.no_export_suppressed"_fs;
constexpr auto kWellKnownCommunityNoExportSubconfedSuppressed =
    "bgpd.well_known_community.no_export_subconfed_suppressed"_fs;

/**
 * Number of times an egress queue blocked us from writing messages.
 * This is taken as the count across all egress (to FiberBgpPeer)
 * queues over all AdjRibs.
 */
DECLARE_timeseries(egress_queue_backpressured_events);
constexpr auto kEgressQueueBackpressuredEvents =
    "bgpd.egress_queue_backpressured_events"_fs;

/*
 * These counters report the egress backpressure of a thrift stream
 * subscriber. The MP-BGP monitor is such a subscriber.
 *
 * bgpd reports these counters only when the subscriber uses the bounded
 * egress path. In the unbounded mode the egress path has no limit to report.
 *
 * A periodic task in the peer manager samples the queue depth and the block
 * state. The task runs on the peer-manager EventBase and samples one time in
 * each period. FLAGS_thrift_stream_publish_gap_ms sets the length of the
 * period. Therefore a block that is shorter than one period can stay
 * uncounted. These counters are a health signal for a receiver that stays
 * slow. They are not an exact event log.
 */
DECLARE_timeseries(stream_subscriber_backpressured_events);
constexpr auto kStreamSubscriberBackpressuredEvents =
    "bgpd.stream_subscriber.backpressured_events"_fs;

/*
 * The largest bounded egress queue of all stream subscribers, in messages.
 * The initial value is -1. A value of 0 means that bgpd sampled the queue and
 * the queue is empty. Thus -1 means that bgpd did not sample the queue.
 */
constexpr auto kStreamSubscriberQueueDepth =
    "bgpd.stream_subscriber.queue_depth"_fs;

// The wall-clock time in ms when the queue of a subscriber last blocked.
constexpr auto kStreamSubscriberLastBlockTime =
    "bgpd.stream_subscriber.last_block_time_ms"_fs;

/*
 * The length of one block of the egress queue of a stream subscriber, in
 * milliseconds. bgpd records one sample for each block. It records the sample
 * when it sees the queue unblocked again. The quantile stat gives the
 * distribution of the block lengths in a sliding window. A running total only
 * increases, so a total does not show the present state.
 */
inline constexpr auto kStreamSubscriberBlockDuration =
    "bgpd.stream_subscriber.block_duration_ms";
DECLARE_quantile_stat(streamSubscriberBlockDurationMs);

/*
 * The count of the sessions that bgpd ended because their egress queue stayed
 * blocked for longer than FLAGS_stream_subscriber_idle_timeout_ms. A client
 * that holds its stream open and gives no thrift stream credit produces such
 * a session.
 *
 * A value above 0 means that a monitor could not keep up, or that a monitor
 * stopped to read. Each event frees one slot of streamSubscriberLimit.
 */
DECLARE_timeseries(stream_subscriber_idle_timeouts);
constexpr auto kStreamSubscriberIdleTimeouts =
    "bgpd.stream_subscriber.idle_timeouts"_fs;

void initStreamSubscriberBackpressureStats();
void setStreamSubscriberQueueDepth(int64_t depth);
void incStreamSubscriberBackpressuredEvents();
void setStreamSubscriberLastBlockTime(uint64_t lastBlockTimeMs);
void addStreamSubscriberBlockDuration(uint64_t durationMs);
void incStreamSubscriberIdleTimeouts();

// Set isSafeModeOn
void setIsSafeModeOn(bool val);

void initCounters();

DECLARE_dynamic_timeseries(non_graceful_peers_count, 1);

// Set number of established sessions
void setRunningSessions(uint32_t val);
void incOpenRejectAsnMismatch();
// Set number of VIP (bgp session + thrift-based) established sessions
void setRunningVipSessions(uint32_t val);
// add session state changes stats
void addSessionStateChanges();

/*
 * Set number of configured peers.
 * NOTE: Dynamic peers (subnet peer configs) are counted as 1, because of that
 * total number of peers established, running sessions could be higher than
 * configured peers.
 */
void setConfiguredPeers(uint32_t val);
void setNumLivePeerGroups(uint32_t val);

/*
 * Set 1 if --policy gFlag has valid symbolic link
 * Set 0 if --policy gFlag does not have any symbolic link set
 */
void setPolicySymlink(uint32_t val);

// Initialize non-graceful peers
void initNonGraceful(const std::string& peerTag);
// Increment / decrement number of non-graceful support peers.
void incNonGrPeers(const std::string& peerTag);
void decNonGrPeers(const std::string& peerTag);

// Set no prefixes sent
void setNoPrefixSent();

// Reset no prefixes sent
void resetNoPrefixSent();

void setStatefulGR(bool isStateful);

void setEorTimerExpired(bool timerExpired);

// Set convergence time
void setConvergenceTime(const int64_t duration);

// Set UCMP active status
void setUcmpActive(bool isActive);

// Set initialized timeout
void setPeerManagerReachesInitializedTimeout(bool isTimeout);

/*
 * Increment count when Policy match route with AS_SET or AS_CONFED_SET
 */
void incrPolicyAsSetHit();
void incrPolicyAsConfedSetHit();
void incrPolicyNewAsMatchFailCount();

/*
 * [AdjRibPolicyCache]
 *
 * This section covers adjrib policy cache util function
 */
void setPolicyCacheHit(uint64_t hit);
void setPolicyCacheMiss(uint64_t miss);
void setPolicyCacheNumEntries(uint64_t num);
void setPolicyCacheMemoryUsage(uint64_t memInBytes);

// Set deduplicated attributes entry counts per deduplicator
void setDeduplicatedAttributesTotal(uint64_t count);
void setDeduplicatedAttributesAsPath(uint64_t count);
void setDeduplicatedAttributesCommunities(uint64_t count);
void setDeduplicatedAttributesClusterList(uint64_t count);
void setDeduplicatedAttributesExtCommunities(uint64_t count);
void setDeduplicatedAttributesBgpPath(uint64_t count);

// Set counts for route-filter policy-affected peers
void setIngressRouteFilterPolicyAffectedPeers(uint64_t count);
void setEgressRouteFilterPolicyAffectedPeers(uint64_t count);

// Set counts for routing-policy affected peers
void setIngressRoutingPolicyAffectedPeers(uint64_t count);
void setEgressRoutingPolicyAffectedPeers(uint64_t count);

// Add per-peer-group processing time for ingress route filter re-evaluation
void addIngressRouteFilterPeerGroupProcessTimeMs(
    int64_t timeMs,
    const std::string& peerGroupName);

/*
 * Set the duration of the most recent ingress policy re-evaluation across all
 * peers
 */
void setIngressPolicyAllPeersLastReEvaluationTimeMs(int64_t timeMs);

// Configured switch prefix limits
void setUniquePrefixLimit(uint64_t uniquePrefixLimit);
void setTotalPathLimit(uint64_t totalPathLimit);
void setOverloadProtectionMode(
    std::optional<thrift::OverloadProtectionMode> mode);

/*
 * [Initialization] This section covers the BGP++ initialization util function
 */
void logInitializationEvent(
    const std::string& publisher,
    const neteng::fboss::bgp::thrift::BgpInitializationEvent event);

int64_t getInitializationDurationMs();

// Number of active update groups
inline const auto kNumUpdateGroups =
    fmt::format("{}.num_update_groups", kBgpcppTag);
void incrNumUpdateGroups();
void decrNumUpdateGroups();

/*
 * Whether the update group feature is enabled on this device, as resolved
 * from bgp_setting_config.enable_update_group. Set to 1 when enabled and 0
 * when disabled, and refreshed on every config load.
 */
inline const auto kUpdateGroupEnabled =
    fmt::format("{}.update_group_enabled", kBgpcppTag);
void setUpdateGroupEnabled(bool val);

// Number of adjacency RIB out groups
inline const auto kAdjRibOutGroupsCount =
    fmt::format("{}.adj_rib_out_groups.count", kBgpcppTag);
void setAdjRibOutGroupsCount(uint64_t count);
void incrAdjRibOutGroupsCount();
void decrAdjRibOutGroupsCount();

// Number of established peers with GR capability
inline const auto kEstablishedGrPeersCount =
    fmt::format("{}.established_gr_peers.count", kBgpcppTag);
void incrEstablishedGrPeersCount();
void decrEstablishedGrPeersCount();

// Number of entries in peerAddrToIds_ map
inline const auto kPeerAddrToIdsCount =
    fmt::format("{}.peer_addr_to_ids.count", kBgpcppTag);
void incrPeerAddrToIdsCount();
void decrPeerAddrToIdsCount();

// Number of pending RIB dump requests
inline const auto kPendingRibDumpReqsCount =
    fmt::format("{}.pending_rib_dump_reqs.count", kBgpcppTag);
void incrPendingRibDumpReqsCount();
void decrPendingRibDumpReqsCount(size_t count);

// Number of configured peers
inline const auto kAllPeersCount =
    fmt::format("{}.all_peers.count", kBgpcppTag);
void incrAllPeersCount();
void decrAllPeersCount();

// Number of dynamic peers
inline const auto kDynamicPeersCount =
    fmt::format("{}.dynamic_peers.count", kBgpcppTag);
void incrDynamicPeersCount();
void decrDynamicPeersCount(uint32_t count = 1);

// Number of passive connections rejected due to local address mismatch
inline const auto kPassiveRejectLocalAddrMismatch =
    fmt::format("{}.session.passiveRejectLocalAddrMismatch", kBgpcppTag);
void incPassiveRejectLocalAddrMismatch();

/**
 * Increment counter that tracks # of times fast teardown was triggered
 * from switches with dsf fast teardown enabled.
 */
void incDsfFastTearDownCount();

/**
 * Increment counter that tracks # of streaming sessions rejected because of
 * being over configured limit.
 */
void incStreamingSessionsRejected();

// Number of BGP decision process runs
inline const auto kDecisionProcessRunsCount =
    fmt::format("{}.decision_process.runs.count", kBgpcppTag);
void incrDecisionProcessRunsCount();

/**
 * Counter for number of slow peers detected. Incremented each time a peer
 * exceeds the block frequency or duration threshold, regardless of whether
 * detachment proceeds or is skipped (e.g., last synced peer in the group).
 */
inline const auto kSlowPeerDetectionCount =
    fmt::format("{}.slow_peer_detection_count", kBgpcppTag);
void incrSlowPeerDetectionCount();

/**
 * Methods to manipulate egress backpressure counters.
 */
void initEgressBackpressureStats();
void incrementEgressTransientRouteUpdatesSuppressed();
void incrementEgressQueueBackpressuredEvents();

/*
 * Initialize the RFC 1997 well-known community suppression counters to 0
 * so monitoring can distinguish "never incremented" from "missing".
 */
void initWellKnownCommunityStats();

/*
 * Increment the matching RFC 1997 well-known community suppression
 * counter. One function per community type to avoid coupling the stats
 * header to the AdjRib filter enum.
 */
void incrementWellKnownCommunityNoAdvertiseSuppressed();
void incrementWellKnownCommunityNoExportSuppressed();
void incrementWellKnownCommunityNoExportSubconfedSuppressed();

/**
 * Indicate exit-in-progress to differentiate between intentional exits and
 * crashes. Directly using ODS counters is unreliable as they may not update
 * before BGP shuts down. Instead, create a filesystem flag on planned exit to
 * check on the next startup. If the flag exists, no crash occurred, and we bump
 * the counter to indicate this; otherwise, a crash may have occurred, in which
 * case we don't bump the counter (in crash loop case, we can't rely on ODS
 * counters being published) and assume absence of signal is a potential sign of
 * issue (need to correlate with other signals to confirm). The very first run
 * will be treated like an unplanned exit occurred, but this is okay since
 * determining whether crash occurred requires correlation with other signals
 * anyway
 */
inline constexpr auto kPlannedExit = "bgpd.plannedExit"_fs;
void setPlannedExit();
void markPlannedExit();
void handlePreviousExit();

constexpr auto kCrfForceUpdateBypass = "bgpd.crf.force_update_bypass"_fs;
void incrCrfForceUpdateBypass();

} // namespace BgpStats

//------------------------ RibStats ------------------------//
namespace RibStats {

void initCounters();

// number of times route churn was detected
inline constexpr auto kTotalRouteChurnDetected =
    "bgpd.rib.kTotalRouteChurnDetected"_fs;
void incrRouteChurnDetected();

// total rib paths (route infos)
inline constexpr auto kTotalRibPaths = "bgpd.rib.totalRibPaths"_fs;
void incrRibPaths();
void decrRibPaths();

// total adjribs
inline constexpr auto kTotalAdjRibs = "bgpd.totalAdjRibs"_fs;
void setAdjRibCount(uint64_t adjRibsSize);
void incrAdjRibCount();
void decrAdjRibCount();

// total originated routes (local routes)
inline constexpr auto kTotalOriginatedRoutes =
    "bgpd.rib.totalOriginatedRoutes"_fs;
void setOriginatedRoutesSize(uint64_t localRoutesSize);

// Publish total number of shadow RIB entries.
void publishShadowRibSize(uint64_t routeCount);

// number of shadow RIB entries
inline constexpr auto kTotalShadowRibEntries = "bgpd.rib.totalShadowRibEntries";

/*
 * BGP table version (ribVersion) - monotonically increasing counter bumped once
 * per contiguous prefix chunk that PeerManager applies to the shadow RIB
 * (announcements and withdrawals), which is the sequence every peer's cached
 * version is stamped from. Note it advances per applied prefix, including
 * no-op FIB resyncs, so its rate tracks emission volume rather than only
 * material routing changes.
 */
inline const auto kRibTableVersion =
    fmt::format("{}.rib.tableVersion", kBgpcppTag);
void incrementRibTableVersion();

// RIB prefix count - total number of prefixes in the RIB
inline const auto kRibPrefixCount =
    fmt::format("{}.rib.prefix.count", kBgpcppTag);
void incrRibPrefixCount();
void decrRibPrefixCount();

// Number of unresolved nexthops referenced by at least one RIB path
inline const auto kRibUnresolvableNexthopsCount =
    fmt::format("{}.rib.unresolvable_nexthops.count", kBgpcppTag);
void incrUnresolvableNexthopsCount();
void decrUnresolvableNexthopsCount();

/*
 * Total number of BGP paths excluded from best-path selection because their
 * nexthops are unresolved. Set absolutely from the RibCounters aggregate
 * rather than incremented in place, so the gauge cannot accumulate drift even
 * if a delta is ever missed.
 */
inline const auto kInactivePathsUnresolvedNexthopsCount =
    fmt::format("{}.rib.inactive_paths.unresolved_nexthops.count", kBgpcppTag);

/* Per-address-family breakdown of the above. */
inline const auto kInactivePathsUnresolvedNexthopsCountIpv4 = fmt::format(
    "{}.rib.inactive_paths.unresolved_nexthops.ipv4.count",
    kBgpcppTag);
inline const auto kInactivePathsUnresolvedNexthopsCountIpv6 = fmt::format(
    "{}.rib.inactive_paths.unresolved_nexthops.ipv6.count",
    kBgpcppTag);

/* Compatibility aliases for existing dashboards. */
inline const auto kInactivePathCount =
    fmt::format("{}.rib.inactive_path.count", kBgpcppTag);
inline const auto kInactivePathCountIpv4 =
    fmt::format("{}.rib.inactive_path.ipv4.count", kBgpcppTag);
inline const auto kInactivePathCountIpv6 =
    fmt::format("{}.rib.inactive_path.ipv6.count", kBgpcppTag);
void setInactivePathCount(int64_t count);
void setInactivePathCountForAfi(bool isV4, int64_t count);

/*
 * Total BGP paths held in the Loc-RIB, add-path-correct: one per (prefix,
 * peer, path-id). Not comparable to kTotalRibPaths, which counts
 * (prefix, peer) pairs and so undercounts under ADD-PATH.
 */
inline const auto kTotalPathsCount =
    fmt::format("{}.rib.total_paths.count", kBgpcppTag);
void setTotalPathsCount(int64_t count);

inline const auto kTotalPathsCountIpv4 =
    fmt::format("{}.rib.total_paths.ipv4.count", kBgpcppTag);
inline const auto kTotalPathsCountIpv6 =
    fmt::format("{}.rib.total_paths.ipv6.count", kBgpcppTag);
void setTotalPathCountForAfi(bool isV4, int64_t count);

// Number of nexthop info entries tracked in the RIB
inline const auto kNexthopInfoCount =
    fmt::format("{}.rib.nexthop_info.count", kBgpcppTag);
void incrNexthopInfoCount();
void decrNexthopInfoCount();

// Number of nexthop status entries in the NexthopStatusMap
inline const auto kNexthopStatusMapCount =
    fmt::format("{}.nexthop_status_map.count", kBgpcppTag);
void incrNexthopStatusMapCount();
void decrNexthopStatusMapCount();

// NHT cache reachability transition counters
inline const auto kNhtCacheNexthopReachable =
    fmt::format("{}.nht.cache.nexthop_reachable", kBgpcppTag);
DECLARE_timeseries(nhtCacheNexthopReachable);
void incrNhtCacheNexthopReachable();

inline const auto kNhtCacheNexthopUnreachable =
    fmt::format("{}.nht.cache.nexthop_unreachable", kBgpcppTag);
DECLARE_timeseries(nhtCacheNexthopUnreachable);
void incrNhtCacheNexthopUnreachable();

// Queued nexthop updates the RIB skipped because it already read a newer status
inline const auto kNhtRibStaleUpdateSkipped =
    fmt::format("{}.nht.rib.stale_update_skipped", kBgpcppTag);
DECLARE_timeseries(nhtRibStaleUpdateSkipped);
void incrNhtRibStaleUpdateSkipped();

// RIB-tracked nexthop reachability transition counters
inline const auto kNhtRibNexthopReachable =
    fmt::format("{}.nht.rib.nexthop_reachable", kBgpcppTag);
DECLARE_timeseries(nhtRibNexthopReachable);
void incrNhtRibNexthopReachable();

inline const auto kNhtRibNexthopUnreachable =
    fmt::format("{}.nht.rib.nexthop_unreachable", kBgpcppTag);
DECLARE_timeseries(nhtRibNexthopUnreachable);
void incrNhtRibNexthopUnreachable();

inline const auto kNhtRibResolvabilityChangePrefixReevaluation = fmt::format(
    "{}.nht.rib.resolvability_change.prefix_reevaluation",
    kBgpcppTag);
DECLARE_timeseries(nhtRibResolvabilityChangePrefixReevaluation);
void incrNhtRibResolvabilityChangePrefixReevaluation();

// Average batch size of fibBatchList_ at each flush
inline const auto kFibBatchListSize =
    fmt::format("{}.rib.fib_batch_list.size", kBgpcppTag);
DECLARE_quantile_stat(fibBatchListSize);
void addFibBatchListSize(int64_t size);

/*
 * Device-level aggregate of adjRibIn entries across all peers.
 * Tracks combined size of adjRibInPathTree_ (add-path peers) and
 * adjRibInLiteTree_ (non-add-path peers).
 */
inline const auto kAdjRibInCount =
    fmt::format("{}.adj_rib_in.count", kBgpcppTag);
void incrAdjRibInCount();
void decrAdjRibInCount(uint32_t count);

// Total stale adjRibIn entries across all peers (device-level aggregate)
inline const auto kAdjRibInStaleCount =
    fmt::format("{}.adj_rib_in_stale.count", kBgpcppTag);
void incrAdjRibInStaleCount();
void decrAdjRibInStaleCount(uint32_t count);

// Total deferred updates across all peers (device-level aggregate)
inline const auto kDeferredUpdatesCount =
    fmt::format("{}.deferred_updates.count", kBgpcppTag);
void incrDeferredUpdatesCount();
void decrDeferredUpdatesCount(uint32_t count);

// Number of entries in postPolicyResultCache_ (device-level)
inline const auto kPostPolicyResultCacheCount =
    fmt::format("{}.post_policy_result_cache.count", kBgpcppTag);
void incrPostPolicyResultCacheCount();
void decrPostPolicyResultCacheCount();

// total number of received route filter policy
inline constexpr auto kRfPolicyRcvd = "bgpd.ribPolicy.numRcvdRfPolicy";
DECLARE_timeseries(rfPolicyRcvd);

// total number of route filter policy updates
inline constexpr auto kRfPolicyUpdate = "bgpd.ribPolicy.numUpdatedRfPolicy";
DECLARE_timeseries(rfPolicyUpdate);

// total rib policy messages enqueued onto the coalescing queue (all kinds)
inline constexpr auto kRibPolicyMsgEnqueued = "bgpd.ribPolicy.numEnqueuedMsg";
DECLARE_timeseries(ribPolicyMsgEnqueued);

/*
 * rib policy messages coalesced into an already-pending same-slot message --
 * the redundant applies the merge queue saved the consumer; a spike means the
 * producers are churning (e.g., the empty->full route-attribute thrash)
 */
inline constexpr auto kRibPolicyMsgCoalesced = "bgpd.ribPolicy.numCoalescedMsg";
DECLARE_timeseries(ribPolicyMsgCoalesced);

// rib policy clear-all messages that purged the queue
inline constexpr auto kRibPolicyMsgPurged = "bgpd.ribPolicy.numPurgedMsg";
DECLARE_timeseries(ribPolicyMsgPurged);

// time for rib to save rib policy to file
inline constexpr auto kSaveRibPolicyStateToFileTimeMs =
    "bgpd.ribPolicy.saveStateToFileTimeMs";
DECLARE_quantile_stat(saveRibPolicyStateToFileTimeMs);

// time for rib to perform a path selection per route
inline constexpr auto kRibPathSelectionTimeMs = "bgpd.rib.pathSelectionTimeMs";
DECLARE_quantile_stat(ribPathSelectionTimeMs);

// time for rib to perform a path selection in a full sync
inline constexpr auto ribFullSyncPathSelectionTimeMs =
    "bgpd.rib.fullSyncPathSelectionTimeMs";
DECLARE_quantile_stat(ribFullSyncPathSelectionTimeMs);

// Time for which best-path selection and FIB programming is paused
inline constexpr auto ribBestPathAndFibProgrammingPauseTimeMs =
    "bgpd.rib.bestPathAndFibProgrammingPauseTimeMs";
DECLARE_quantile_stat(ribBestPathAndFibProgrammingPauseTimeMs);

/*
 * RouteAttributePolicy cache preservation counters
 * Cache hit/miss during overwriteRouteAttributes()
 */
inline constexpr auto kRaPolicyCacheHit =
    "bgpd.ribPolicy.routeAttributePolicyCache.hit";
DECLARE_timeseries(raPolicyCacheHit);

inline constexpr auto kRaPolicyCacheMiss =
    "bgpd.ribPolicy.routeAttributePolicyCache.miss";
DECLARE_timeseries(raPolicyCacheMiss);

// RouteAttributePolicy inverted index counters
inline constexpr auto kRaPolicyCommunityIndexHit =
    "bgpd.ribPolicy.communityIndex.hit";
DECLARE_timeseries(raPolicyCommunityIndexHit);

inline constexpr auto kRaPolicyCommunityIndexMiss =
    "bgpd.ribPolicy.communityIndex.miss";
DECLARE_timeseries(raPolicyCommunityIndexMiss);

inline constexpr auto kRaPolicyWeightIndexHit =
    "bgpd.ribPolicy.weightIndex.hit";
DECLARE_timeseries(raPolicyWeightIndexHit);

inline constexpr auto kRaPolicyWeightIndexMiss =
    "bgpd.ribPolicy.weightIndex.miss";
DECLARE_timeseries(raPolicyWeightIndexMiss);

} // namespace RibStats

//------------------------ FibStats ------------------------//

// Bgp Fib level stats
namespace FibStats {
inline constexpr auto kAgentUpdateFailures = "fib.agentUpdateFailures";
inline constexpr auto kAgentStatusFailures = "fib.agentStatusFailures";
inline constexpr auto kFibUcastUpdates = "fib.fibUcastUpdates";
inline constexpr auto kTotalUcastRoutesKey = "fib.totalUcastRoutes";
inline constexpr auto kFibSyncStatus = "fib.synced";

/*
 * Is the fib agent currently programmable?
 * 1: yes, 0: no, -1: unknown
 */
inline constexpr auto kAgentProgrammable = "fib.agentProgrammable";

// Counter for FIB flush events (when all non-local routes are removed)
inline constexpr auto kFibFlushed = "fib.flushed";

void initCounters();

// Increment counter of agent update failures
void addAgentUpdateFailures();

// Increment counter of agent status failures
void addAgentStatusFailures();

// Increment counter of fib route churn
void addFibUcastUpdates();

// Publish total unicast Routes.
void publishTotalUCastRoutes(uint32_t routeCount);

// Set fib sync status
void setFibSyncStatus(bool isSynced);

/*
 * Set agentProgrammable to
 * true: 1 , false: 0
 */
void setAgentProgrammable(bool programmable);

// Increment counter for FIB flush events
void incrFibFlushed();

// FIB sync duration
inline const auto kFibSyncTimeMs =
    fmt::format("{}.rib.rib_sync_time_ms", kBgpcppTag);
DECLARE_quantile_stat(fibSyncTimeMs);
void addFibSyncTimeMs(int64_t timeMs);

} // namespace FibStats

//------------------------ PeerStats ------------------------//

// Bgp Peer level stats
namespace PeerStats {
constexpr auto kTotalPeerWithNoRouteExchange =
    "peer.totalPeerWithNoRouteExchange"_fs;
constexpr auto kTotalRcvdPrefixes = "peer.totalRcvdPrefixes"_fs;
constexpr auto kTotalAcceptedPrefixes = "peer.totalAcceptedPrefixes"_fs;
/* Current IPv4 + IPv6 rejected peer-prefix paths, not rejection events. */
constexpr auto kNumRswSlbInRejections = "num_rsw_slb_in_rejections"_fs;
constexpr auto kTotalSentPrefixes = "peer.totalSentPrefixes"_fs;
/*
 * Despite the "Prefixes" name, this counts dropped routes (each route is a
 * prefix + path), not unique prefixes. Every dropped path increments it, so
 * with add-path a single prefix can contribute multiple drops. The emitted ODS
 * key "peer.totalDroppedPrefixes" is kept for dashboard/alert continuity even
 * though the C++ symbol is named after the per-peer prefix-limit semantics.
 * See S676351.
 */
constexpr auto kTotalPrefixesDroppedByLimit = "peer.totalDroppedPrefixes"_fs;
constexpr auto kTotalPaths = "peer.totalPaths"_fs;
constexpr auto kTotalUniquePrefixes = "peer.totalUniquePrefixes"_fs;
constexpr auto kTotalVipPrefixes = "peer.totalVipPrefixes"_fs;
constexpr auto kTotalGoldenVipPrefixes = "peer.totalGoldenVipPrefixes"_fs;
constexpr auto kMaxPeerRcvdPrefixes = "peer.maxPeerRcvdPrefixes"_fs;
constexpr auto kTotalHoldTimerExpiry = "peer.totalHoldTimerExpiry"_fs;
constexpr auto kTotalPurgeForNonGr = "peer.totalPurgeForNonGr"_fs;
constexpr auto kTotalPurgeForGrDoubleFailure =
    "peer.totalPurgeForGrDoubleFailure"_fs;
constexpr auto kTotalPurgeForGrRestartTimer =
    "peer.totalPurgeForGrRestartTimer"_fs;
constexpr auto kTotalPurgeForStalePathTimer =
    "peer.totalPurgeForStalePathTimer"_fs;
constexpr auto kNoGrRestart = "peer.nonGrRestart"_fs;
constexpr auto kNoGrRestartPeer = "peer.nonGrRestart.{}";
constexpr auto kPeerPreInPrefixes = "peer_{}.prefilterRcvdPfxLen";
constexpr auto kPeerPostInPrefixes = "peer_{}.postfilterAcptPfxLen";
constexpr auto kPeerPreOutPrefixes = "peer_{}.prefilterAdvtPfxLen";
constexpr auto kPeerPostOutPrefixes = "peer_{}.postfilterAdvtPfxLen";
constexpr auto kPeerSessionStateChanges = "peer_{}.sessionStateChanges";
constexpr auto kPeerStatus = "peer_{}.isUp";
constexpr auto kPeerTableVersion = "{}.{}.peer_{}.tableVersion";
constexpr auto kPeerIngressRouteFilterDenied =
    "{}.{}.policy.route_filter.denied.peer_{}_ingress";
constexpr auto kPeerMessagesSentUpdate = "{}.{}.peer_{}.messagesSent.update";
constexpr auto kPeerMessagesSentAnnouncedIpv4 =
    "{}.{}.peer_{}.messagesSent.update.announced_ipv4";
constexpr auto kPeerMessagesSentAnnouncedIpv6 =
    "{}.{}.peer_{}.messagesSent.update.announced_ipv6";
constexpr auto kPeerMessagesSentWithdraw =
    "{}.{}.peer_{}.messagesSent.update.withdraw";
constexpr auto kPeerMessagesSentOpen = "{}.{}.peer_{}.messagesSent.open";
constexpr auto kPeerMessagesSentNotification =
    "{}.{}.peer_{}.messagesSent.notification";
constexpr auto kPeerMessagesSentKeepAlive =
    "{}.{}.peer_{}.messagesSent.keepAlive";
constexpr auto kPeerMessagesSentEndOfRib =
    "{}.{}.peer_{}.messagesSent.endOfRib";
constexpr auto kPeerMessagesSentRouteRefresh =
    "{}.{}.peer_{}.messagesSent.routeRefresh";
constexpr auto kPeerMessagesSentSocketFailure =
    "{}.{}.peer_{}.messagesSent.socketFailure";
constexpr auto kPeerMessagesRecvUpdate = "{}.{}.peer_{}.messagesRecv.update";
constexpr auto kPeerMessagesRecvAnnouncedIpv4 =
    "{}.{}.peer_{}.messagesRecv.update.announced_ipv4";
constexpr auto kPeerMessagesRecvAnnouncedIpv6 =
    "{}.{}.peer_{}.messagesRecv.update.announced_ipv6";
constexpr auto kPeerMessagesRecvWithdraw =
    "{}.{}.peer_{}.messagesRecv.update.withdraw";
constexpr auto kPeerMessagesRecvOpen = "{}.{}.peer_{}.messagesRecv.open";
constexpr auto kPeerMessagesRecvNotification =
    "{}.{}.peer_{}.messagesRecv.notification";
constexpr auto kPeerMessagesRecvKeepAlive =
    "{}.{}.peer_{}.messagesRecv.keepAlive";
constexpr auto kPeerMessagesRecvRouteRefresh =
    "{}.{}.peer_{}.messagesRecv.routeRefresh";

void initCounters();
void initPeerCounters(const std::string& peerId);
void clearPeerCounters(
    const std::string& peerIdOdsStr,
    const std::string& noGrRestartPeerId);
/*
 * Clears only the per-peer EGRESS (sent) message counters (not prefix gauges
 * or recv counters). Used by the clearEgressCounters debug RPC.
 */
void clearPeerEgressMessageCounters(const std::string& peerId);
/*
 * Clears only the per-peer INGRESS (recv) message counters (not prefix gauges
 * or sent counters). Used by the clearIngressCounters debug RPC.
 */
void clearPeerIngressMessageCounters(const std::string& peerId);
void setTotalRcvdPrefixes(uint32_t val);
void setTotalAcceptedPrefixes(uint32_t val);
void setNumRswSlbInRejections(int64_t val);
void incrementTotalPrefixesDroppedByLimit(uint32_t val);
void setTotalPaths(uint32_t val);
void setTotalSentPrefixes(uint32_t val);
void setTotalUniquePrefixes(uint32_t val);
void setTotalVipPrefixes(uint32_t val);
void setTotalGoldenVipPrefixes(uint32_t val);
void setTotalVipInjectorPrefixes(uint32_t val);
void setMaxPeerRcvdPrefixes(uint32_t val);
void setTotalPeerWithNoRouteExchange(uint32_t val);
void incrNoGrRestart();
void incrPeerNoGrRestart(const std::string& peerId);
void incrTotalHoldTimerExpiry();
void incrTotalPurgeForNonGr();
void incrTotalPurgeForGrDoubleFailure();
void incrTotalPurgeForGrRestartTimer();
void incrTotalPurgeForStalePathTimer();
void setPeerPreInPrefixes(const std::string& peerId, uint32_t val);
void setPeerPostInPrefixes(const std::string& peerId, uint32_t val);
void setPeerPostOutPrefixes(const std::string& peerId, uint32_t val);
void setPeerPreOutPrefixes(const std::string& peerId, uint32_t val);
void addPeerSessionStateChanges(const std::string& peerId);
void setPeerStatus(const std::string& peerId, uint32_t val);
void setPeerTableVersion(const std::string& peerId, uint64_t val);
void addPeerIngressRouteFilterDenied(const std::string& peerId);
void addPeerMessagesSentUpdate(const std::string& peerId, uint64_t count);
void addPeerMessagesRecvUpdate(const std::string& peerId);
void addPeerMessagesRecvOpen(const std::string& peerId);
void addPeerMessagesRecvNotification(const std::string& peerId);
void addPeerMessagesRecvKeepAlive(const std::string& peerId);
void addPeerMessagesRecvRouteRefresh(const std::string& peerId);

DECLARE_timeseries(peer_socket_bytes_read);
void initBytesReadAvg();
void addBytesReadToAvg(size_t bytesRead);
DECLARE_timeseries(peer_socket_bytes_written);
void initBytesWrittenAvg();
void addBytesWrittenToAvg(size_t bytesWritten);

DECLARE_dynamic_timeseries(messagesSent, 1);
inline constexpr auto kMessagesSent = "peer.messagesSent.{}";
void initMessagesSent(const std::string& subkey);
inline constexpr auto kMessagesSentOpen = "open"_fs;
void incrOpenMessagesSent(const std::string& peerIdOdsStr = "");
inline constexpr auto kMessagesSentNotification = "notification"_fs;
void incrNotificationMessagesSent(const std::string& peerIdOdsStr = "");
inline constexpr auto kMessagesSentKeepAlive = "keepAlive"_fs;
void incrKeepAliveMessagesSent(const std::string& peerIdOdsStr = "");
inline constexpr auto kMessagesSentUpdate = "update"_fs;
void incrUpdateMessagesSent();
inline constexpr auto kMessagesSentEndOfRib = "endOfRib"_fs;
void incrEndOfRibMessagesSent(const std::string& peerIdOdsStr = "");
inline constexpr auto kMessagesSentRouteRefresh = "routeRefresh"_fs;
void incrRouteRefreshMessagesSent(const std::string& peerIdOdsStr = "");
inline constexpr auto kMessagesSentSocketFailure = "socketFailure"_fs;
void incrMessagesSentSocketFailures(const std::string& peerIdOdsStr = "");

inline constexpr auto kMessageSentAnnouncedIpv4 = "update.announced_ipv4"_fs;
inline constexpr auto kMessageSentAnnouncedIpv6 = "update.announced_ipv6"_fs;
inline constexpr auto kMessageSentWithdraw = "update.withdraw"_fs;
void incrMessageSentAnnouncedIpv4(const std::string& peerIdOdsStr = "");
void incrMessageSentAnnouncedIpv6(const std::string& peerIdOdsStr = "");
void incrMessageSentWithdraw(const std::string& peerIdOdsStr = "");

DECLARE_dynamic_timeseries(messagesRecv, 1);
inline constexpr auto kMessagesRecv = "peer.messagesRecv.{}";

inline constexpr auto kMessageRecvOpen = "open"_fs;
inline constexpr auto kMessageRecvUpdate = "update"_fs;
inline constexpr auto kMessageRecvNotification = "notification"_fs;
inline constexpr auto kMessageRecvKeepAlive = "keepAlive"_fs;
inline constexpr auto kMessageRecvRouteRefresh = "routeRefresh"_fs;

void initMessagesRecv(const std::string& subkey);
void incrMessageRecvOpen();
void incrMessageRecvUpdate();
void incrMessageRecvNotification();
void incrMessageRecvKeepAlive();
void incrMessageRecvRouteRefresh();

inline constexpr auto kMessageRecvAnnouncedIpv4 = "update.announced_ipv4"_fs;
inline constexpr auto kMessageRecvAnnouncedIpv6 = "update.announced_ipv6"_fs;
inline constexpr auto kMessageRecvWithdraw = "update.withdraw"_fs;
void incrMessageRecvAnnouncedIpv4(const std::string& peerIdOdsStr = "");
void incrMessageRecvAnnouncedIpv6(const std::string& peerIdOdsStr = "");
void incrMessageRecvWithdraw(const std::string& peerIdOdsStr = "");

DECLARE_timeseries(peer_update_bytes_sent);
constexpr auto kUpdateBytesSent = "peer_update_bytes_sent"_fs;
void addUpdateBytesSentToAvg(size_t bytesSent);

DECLARE_timeseries(peer_update_bytes_recv);
constexpr auto kUpdateBytesRecv = "peer_update_bytes_recv"_fs;
void initUpdateBytesRecvAvg();
void addUpdateBytesRecvToAvg(size_t bytesRecv);

DECLARE_dynamic_timeseries(attributeSize, 1);
inline constexpr auto kAttributeSize = "attribute.size.{}";

inline constexpr auto kAttributeSizeAsPath = "as_path"_fs;
inline constexpr auto kAttributeSizeCommunity = "community"_fs;
inline constexpr auto kAttributeSizeExtendedCommunity = "extended_community"_fs;
inline constexpr auto kAttributeSizeClusterList = "cluster_list"_fs;
inline constexpr auto kAttributeSizeTopologyInfo = "aggregators"_fs;

void initAttributeSize(const std::string& subkey);
void addAsPathSizeToAvg(size_t asPathSize);
void addCommunitySizeToAvg(size_t communitySize);
void addExtendedCommunitySizeToAvg(size_t extCommunitySize);
void addClusterListSizeToAvg(size_t clusterListSize);
void addTopologyInfoSizeToAvg(size_t topologySize);

DECLARE_timeseries(peer_rejected_inbound_routes);
inline constexpr auto kRejectedInboundRoutes =
    "peer_rejected_inbound_routes"_fs;
void incrRejectedInboundRoutes();

DECLARE_timeseries(peer_rejected_outbound_routes);
inline constexpr auto kRejectectedOutboundRoutes =
    "peer_rejected_outbound_routes"_fs;
void incrRejectedOutboundRoutes();

/*
 * Outbound peer announcement events rejected because the resolved nexthop
 * cannot be encoded for the negotiated AFI.
 */
DECLARE_timeseries(peer_invalid_nexthop_encoding);
inline const auto kInvalidNexthopEncoding = fmt::format(
    "{}.{}.peer_invalid_nexthop_encoding",
    kEbbPlatform,
    kBgpcppTag);
void incrInvalidNexthopEncoding();

DECLARE_timeseries(empty_gar_weights_rejects);
inline constexpr auto kEmptyGarWeightsRejects = "empty_gar_weights_rejects"_fs;
void incrEmptyGarWeightsRejects();

/*
 * Device-level count of connection collisions resolved locally by closing the
 * losing connection (i.e. we sent a BN_CEASE_CONN_COLLISION_RES notification).
 */
DECLARE_timeseries(connection_collision_closed);
inline constexpr auto kConnectionCollisionClosed =
    "connection_collision_closed"_fs;
void incrConnectionCollisionClosed();

/*
 * Device-level count of BN_CEASE connection-collision-resolution NOTIFICATIONs
 * received from peers (i.e. the peer resolved the collision by closing us).
 */
DECLARE_timeseries(connection_collision_closed_by_peer);
inline constexpr auto kConnectionCollisionClosedByPeer =
    "connection_collision_closed_by_peer"_fs;
void incrConnectionCollisionClosedByPeer();

} // namespace PeerStats

void initStatsBase();
} // namespace facebook::bgp
