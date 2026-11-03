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

#include <boost/filesystem.hpp>

#include <fmt/format.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/Utils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

namespace facebook::bgp {

//------------------------ BgpStats ------------------------//

// Global BGP level stats
namespace BgpStats {

void initCounters() {
  /*
   * When BGP restarts it is important to differentiate values from previous
   * incarnation from those of this incarnation.  This is particularly true if
   * the counter is used in an NHS check during push.  Here are the options we
   * considered:
   *
   * -- Initialize to 0 or some "meaningful" value.  This is the wrong thing
   *    to do.  Consider the case of "statefulGR".  If we initialize to zero,
   *    after BGP restarts and before it has had time to reflect the
   *    statefulGR status, fbagent will export a value of zero, and NHS will
   *    have no idea about whether statefulGR failed or BGP is still being
   *    initialized.
   *
   * -- Do not initialize.  In this case fbagent will not populate any value
   *    until we set it to either 0 or 1.  This is correct as such, but we are
   *    missing useful information (at least within one minute of granularity)
   *    of how long it took the device to set this counter after it came up.
   *    For example, if we miss five minutes of data, did the BGP take four
   *    minutes to restart and one minute to set the counter, or did BGP take
   *    one minute to restart and four minutes before it set the counter?
   *
   * -- Initialize to -1 or some "flag" which is not a normal running value.
   *    Doing this takes care of both the above deficiencies.  We will know,
   *    for example, that it took approximately 1 minute for BGP to restart
   *    and four minutes before it set the counter, which can be useful
   *    information.  One potential problem is that NHS, or whatever
   *    monitoring system we use, now needs to differentiate a value of -1
   *    from the other values, but this is not difficult because the
   *    information is consistent.
   *
   * Caution: in some places in Stats we increment and decrement counters.  In
   * those cases, we need to initialize to zero in order to get an accurate
   * count.
   */
  fb303::ThreadCachedServiceData::get()->setCounter(kRunningSessions, -1);
  fb303::ThreadCachedServiceData::get()->setCounter(kNumLivePeerGroups, -1);
  fb303::ThreadCachedServiceData::get()->setCounter(kOpenRejectAsnMismatch, 0);
  fb303::ThreadCachedServiceData::get()->addStatExportType(
      kOpenRejectAsnMismatch, fb303::SUM);
  fb303::ThreadCachedServiceData::get()->setCounter(kRunningVipSessions, -1);
  fb303::ThreadCachedServiceData::get()->setCounter(kNoPrefixSent, -1);
  fb303::ThreadCachedServiceData::get()->setCounter(kStatefulGR, -1);
  fb303::ThreadCachedServiceData::get()->setCounter(kEorTimerExpired, -1);
  fb303::ThreadCachedServiceData::get()->setCounter(
      fmt::format(kIngressPolicyAllPeersLastReEvaluationTimeMs, kBgpcppTag),
      -1);
  fb303::ThreadCachedServiceData::get()->setCounter(kPolicyAsSetHitCount, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kPolicyAsConfedSetHitCount, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kPolicyNewAsMatchFailCount, 0);
  fb303::ThreadCachedServiceData::get()->addStatExportType(
      kSessionStateChanges, fb303::SUM);
  fb303::ThreadCachedServiceData::get()->setCounter(kUcmpActive, -1);
  fb303::ThreadCachedServiceData::get()->setCounter(kInitializedTimeout, -1);
  fb303::ThreadCachedServiceData::get()->setCounter(kIsSafeModeOn, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kThriftReject, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kThriftSuspend, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kStreamingSessionsRejected, 0);
  fb303::ThreadCachedServiceData::get()->addStatExportType(
      kStreamingSessionsRejected, fb303::SUM);
  fb303::ThreadCachedServiceData::get()->setCounter(kNumUpdateGroups, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kUpdateGroupEnabled, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kAdjRibOutGroupsCount, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kEstablishedGrPeersCount, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kPeerAddrToIdsCount, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kPendingRibDumpReqsCount, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kAllPeersCount, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kDynamicPeersCount, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kPassiveRejectLocalAddrMismatch, 0);
  fb303::ThreadCachedServiceData::get()->addStatExportType(
      kPassiveRejectLocalAddrMismatch, fb303::SUM);
  fb303::ThreadCachedServiceData::get()->setCounter(kDsfFastTearDownCount, 0);
  fb303::ThreadCachedServiceData::get()->addStatExportType(
      kDsfFastTearDownCount, fb303::SUM);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kDeduplicatedAttributesTotal, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kDeduplicatedAttributesAsPath, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kDeduplicatedAttributesCommunities, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kDeduplicatedAttributesClusterList, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kDeduplicatedAttributesExtCommunities, 0);

  fb303::ThreadCachedServiceData::get()->setCounter(
      kDecisionProcessRunsCount, 0);
  fb303::ThreadCachedServiceData::get()->addStatExportType(
      kDecisionProcessRunsCount, fb303::SUM);

  fb303::ThreadCachedServiceData::get()->setCounter(
      kSlowPeerDetectionCount + ".sum", 0);
  fb303::ThreadCachedServiceData::get()->addStatExportType(
      kSlowPeerDetectionCount, fb303::SUM);

  fb303::ThreadCachedServiceData::get()->setCounter(
      kNeighborPortIdStateMismatch, 0);
  fb303::ThreadCachedServiceData::get()->addStatExportType(
      kNeighborPortIdStateMismatch, fb303::SUM);

  fb303::ThreadCachedServiceData::get()->addStatExportType(
      kCrfForceUpdateBypass, fb303::SUM);

  initEgressBackpressureStats();
  initStreamSubscriberBackpressureStats();
  initWellKnownCommunityStats();
}

DEFINE_dynamic_timeseries(non_graceful_peers_count, kNonGraceful, fb303::COUNT);

void incrNumUpdateGroups() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(kNumUpdateGroups, 1);
}

void decrNumUpdateGroups() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(kNumUpdateGroups, -1);
}

void setUpdateGroupEnabled(bool val) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kUpdateGroupEnabled, val ? 1 : 0);
}

void setAdjRibOutGroupsCount(uint64_t count) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kAdjRibOutGroupsCount, count);
}

void incrAdjRibOutGroupsCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kAdjRibOutGroupsCount, 1);
}

void decrAdjRibOutGroupsCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kAdjRibOutGroupsCount, -1);
}

void incrEstablishedGrPeersCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kEstablishedGrPeersCount, 1);
}

void decrEstablishedGrPeersCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kEstablishedGrPeersCount, -1);
}

void incrPeerAddrToIdsCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kPeerAddrToIdsCount, 1);
}

void decrPeerAddrToIdsCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kPeerAddrToIdsCount, -1);
}

void incrPendingRibDumpReqsCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kPendingRibDumpReqsCount, 1);
}

void decrPendingRibDumpReqsCount(size_t count) {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kPendingRibDumpReqsCount, -static_cast<int64_t>(count));
}

void incrAllPeersCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(kAllPeersCount, 1);
}

void decrAllPeersCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(kAllPeersCount, -1);
}

void incrDynamicPeersCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kDynamicPeersCount, 1);
}

void decrDynamicPeersCount(uint32_t count) {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kDynamicPeersCount, -1 * static_cast<int64_t>(count));
}

void setRunningSessions(uint32_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(kRunningSessions, val);
}

void incOpenRejectAsnMismatch() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kOpenRejectAsnMismatch, 1);
  fb303::ThreadCachedServiceData::get()->addStatValue(
      kOpenRejectAsnMismatch, 1);
}

void setRunningVipSessions(uint32_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(kRunningVipSessions, val);
}

void addSessionStateChanges() {
  fb303::ThreadCachedServiceData::get()->addStatValue(kSessionStateChanges, 1);
}

void setConfiguredPeers(uint32_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(kConfiguredPeers, val);
}

void setNumLivePeerGroups(uint32_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(kNumLivePeerGroups, val);
}

void setPolicySymlink(uint32_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(kPolicySymlink, val);
}

void initNonGraceful(const std::string& peerTag) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      fmt::format(kNonGraceful, peerTag) + ".count", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      fmt::format(kNonGraceful, peerTag) + ".count.60", 0);
}

void incNonGrPeers(const std::string& peerTag) {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(kNonGraceful, peerTag) + ".count", 1);
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(kNonGraceful, peerTag) + ".count.60", 1);
}

void decNonGrPeers(const std::string& peerTag) {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(kNonGraceful, peerTag) + ".count", -1);
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(kNonGraceful, peerTag) + ".count.60", -1);
}

DEFINE_timeseries(
    egress_queue_backpressured_events,
    kEgressQueueBackpressuredEvents,
    fb303::COUNT);
DEFINE_timeseries(
    egress_transient_route_updates_suppressed,
    kEgressTransientUpdatesSuppressed,
    fb303::COUNT);
DEFINE_timeseries(
    stream_subscriber_backpressured_events,
    kStreamSubscriberBackpressuredEvents,
    fb303::COUNT);
DEFINE_timeseries(
    stream_subscriber_idle_timeouts,
    kStreamSubscriberIdleTimeouts,
    fb303::COUNT);
// One sample of the block length of a stream subscriber egress queue.
DEFINE_quantile_stat(
    streamSubscriberBlockDurationMs,
    kStreamSubscriberBlockDuration,
    fb303::ExportTypeConsts::kAvg,
    fb303::QuantileConsts::kP50_P95_P99,
    fb303::SlidingWindowPeriodConsts::kOneMinTenMin);
DEFINE_timeseries(
    well_known_community_no_advertise_suppressed,
    kWellKnownCommunityNoAdvertiseSuppressed,
    fb303::COUNT);
DEFINE_timeseries(
    well_known_community_no_export_suppressed,
    kWellKnownCommunityNoExportSuppressed,
    fb303::COUNT);
DEFINE_timeseries(
    well_known_community_no_export_subconfed_suppressed,
    kWellKnownCommunityNoExportSubconfedSuppressed,
    fb303::COUNT);

void initWellKnownCommunityStats() {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kWellKnownCommunityNoAdvertiseSuppressed + ".count", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kWellKnownCommunityNoAdvertiseSuppressed + ".count.60", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kWellKnownCommunityNoExportSuppressed + ".count", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kWellKnownCommunityNoExportSuppressed + ".count.60", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kWellKnownCommunityNoExportSubconfedSuppressed + ".count", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kWellKnownCommunityNoExportSubconfedSuppressed + ".count.60", 0);
}

void incrementWellKnownCommunityNoAdvertiseSuppressed() {
  STATS_well_known_community_no_advertise_suppressed.add(1);
}

void incrementWellKnownCommunityNoExportSuppressed() {
  STATS_well_known_community_no_export_suppressed.add(1);
}

void incrementWellKnownCommunityNoExportSubconfedSuppressed() {
  STATS_well_known_community_no_export_subconfed_suppressed.add(1);
}

void initEgressBackpressureStats() {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kEgressTransientUpdatesSuppressed + ".count", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kEgressTransientUpdatesSuppressed + ".count.60", 0);

  fb303::ThreadCachedServiceData::get()->setCounter(
      kEgressQueueBackpressuredEvents + ".count", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kEgressQueueBackpressuredEvents + ".count.60", 0);
}

void incrementEgressQueueBackpressuredEvents() {
  STATS_egress_queue_backpressured_events.add(1);
}

void initStreamSubscriberBackpressureStats() {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kStreamSubscriberBackpressuredEvents + ".count", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kStreamSubscriberBackpressuredEvents + ".count.60", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kStreamSubscriberIdleTimeouts + ".count", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kStreamSubscriberIdleTimeouts + ".count.60", 0);
  /*
   * The initial value is -1. A value of 0 means that bgpd sampled the queue
   * and the queue is empty. Thus -1 means that bgpd did not sample the queue.
   */
  fb303::ThreadCachedServiceData::get()->setCounter(
      kStreamSubscriberQueueDepth, -1);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kStreamSubscriberLastBlockTime, -1);
}

void setStreamSubscriberQueueDepth(int64_t depth) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kStreamSubscriberQueueDepth, depth);
}

void incStreamSubscriberBackpressuredEvents() {
  STATS_stream_subscriber_backpressured_events.add(1);
}

void setStreamSubscriberLastBlockTime(uint64_t lastBlockTimeMs) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kStreamSubscriberLastBlockTime, static_cast<int64_t>(lastBlockTimeMs));
}

void addStreamSubscriberBlockDuration(uint64_t durationMs) {
  STATS_streamSubscriberBlockDurationMs.addValue(
      static_cast<int64_t>(durationMs));
}

void incStreamSubscriberIdleTimeouts() {
  STATS_stream_subscriber_idle_timeouts.add(1);
}

void incrementEgressTransientRouteUpdatesSuppressed() {
  STATS_egress_transient_route_updates_suppressed.add(1);
}

void setNoPrefixSent() {
  fb303::ThreadCachedServiceData::get()->setCounter(kNoPrefixSent, 1);
}

void resetNoPrefixSent() {
  fb303::ThreadCachedServiceData::get()->setCounter(kNoPrefixSent, 0);
}

void setStatefulGR(bool isStateful) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kStatefulGR, isStateful ? 1 : 0);
}

void setEorTimerExpired(bool timerExpired) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kEorTimerExpired, timerExpired ? 1 : 0);
}

void setConvergenceTime(const int64_t duration) {
  fb303::ThreadCachedServiceData::get()->setCounter(kConvergenceTime, duration);
}

void setUcmpActive(bool isActive) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kUcmpActive, isActive ? 1 : 0);
}

void incrPolicyAsSetHit() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kPolicyAsSetHitCount, 1);
}

void incrPolicyAsConfedSetHit() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kPolicyAsConfedSetHitCount, 1);
}

void incrPolicyNewAsMatchFailCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kPolicyNewAsMatchFailCount, 1);
}

void setPolicyCacheHit(uint64_t hit) {
  fb303::ThreadCachedServiceData::get()->setCounter(kPolicyCacheHitCount, hit);
}

void setPolicyCacheMiss(uint64_t miss) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kPolicyCacheMissCount, miss);
}

void setPolicyCacheNumEntries(uint64_t num) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kPolicyCacheNumEntries, num);
}

void setPolicyCacheMemoryUsage(uint64_t memInBytes) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kPolicyCacheMemoryUsage, memInBytes);
}

void setDeduplicatedAttributesTotal(uint64_t count) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kDeduplicatedAttributesTotal, count);
}

void setDeduplicatedAttributesAsPath(uint64_t count) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kDeduplicatedAttributesAsPath, count);
}

void setDeduplicatedAttributesCommunities(uint64_t count) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kDeduplicatedAttributesCommunities, count);
}

void setDeduplicatedAttributesClusterList(uint64_t count) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kDeduplicatedAttributesClusterList, count);
}

void setDeduplicatedAttributesExtCommunities(uint64_t count) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kDeduplicatedAttributesExtCommunities, count);
}

void setDeduplicatedAttributesBgpPath(uint64_t count) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kDeduplicatedAttributesBgpPath, count);
}

void setIngressRouteFilterPolicyAffectedPeers(uint64_t count) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kIngressRouteFilterPolicyAffectedPeers, count);
}

void setEgressRouteFilterPolicyAffectedPeers(uint64_t count) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kEgressRouteFilterPolicyAffectedPeers, count);
}

void setIngressRoutingPolicyAffectedPeers(uint64_t count) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kIngressRoutingPolicyAffectedPeers, count);
}

void setEgressRoutingPolicyAffectedPeers(uint64_t count) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kEgressRoutingPolicyAffectedPeers, count);
}

void setUniquePrefixLimit(uint64_t uniquePrefixLimit) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kUniquePrefixLimit, uniquePrefixLimit);
}

void setTotalPathLimit(uint64_t totalPathLimit) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kTotalPathLimit, totalPathLimit);
}

void setOverloadProtectionMode(
    std::optional<thrift::OverloadProtectionMode> mode) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kOverloadProtectionMode, mode ? static_cast<int>(*mode) : -1);
}

void logInitializationEvent(
    const std::string& publisher,
    const neteng::fboss::bgp::thrift::BgpInitializationEvent event) {
  // BGP++ starting time. Initializaed first time function is called.
  const static auto kBgpStartTime = std::chrono::steady_clock::now();

  // duration in milliseconds since BGP++ started.
  auto durationSinceStart =
      std::chrono::ceil<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - kBgpStartTime)
          .count();

  // transform the duration into string for counter exposure purpose
  auto durationStr = durationSinceStart >= 1000
      ? fmt::format("{}s", durationSinceStart * 1.0 / 1000)
      : fmt::format("{}ms", durationSinceStart);

  // logging the initialization event
  XLOGF(
      INFO,
      "[Initialization] event: {}, publisher: {}, durationSinceStart: {}",
      apache::thrift::util::enumNameSafe(event),
      publisher,
      durationStr);

  // Log BGP++ initialization event to ThreadCachedServiceData.
  fb303::ThreadCachedServiceData::get()->setCounter(
      fmt::format(
          kInitEventCounterFormat, apache::thrift::util::enumNameSafe(event)),
      durationSinceStart);
}

int64_t getInitializationDurationMs() {
  auto convergenceKey = fmt::format(
      kInitEventCounterFormat,
      apache::thrift::util::enumNameSafe(
          neteng::fboss::bgp::thrift::BgpInitializationEvent::INITIALIZED));
  return fb303::ThreadCachedServiceData::get()->getCounter(convergenceKey);
}

void setPeerManagerReachesInitializedTimeout(bool reachesTimeout) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kInitializedTimeout, reachesTimeout ? 1 : 0);
}

void incPassiveRejectLocalAddrMismatch() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kPassiveRejectLocalAddrMismatch, 1);
}

void incDsfFastTearDownCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kDsfFastTearDownCount, 1);
}

void setIsSafeModeOn(bool val) {
  fb303::ThreadCachedServiceData::get()->setCounter(kIsSafeModeOn, val ? 1 : 0);
}

/**
 * Increment counter that tracks # of streaming sessions rejected because of
 * being over configured limit.
 */
void incStreamingSessionsRejected() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kStreamingSessionsRejected, 1);
}

void incrDecisionProcessRunsCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kDecisionProcessRunsCount, 1);
}

/**
 * Increment counter tracking the number of slow peers detected.
 * Called at the top of AdjRibOutGroup::detachSlowPeer(), before the
 * early-exit check, so every detection is counted regardless of whether
 * detachment proceeds or is skipped.
 */
void incrSlowPeerDetectionCount() {
  fb303::ThreadCachedServiceData::get()->addStatValue(
      kSlowPeerDetectionCount, 1);
}

/**
 * Indicate exit-in-progress to help differentiate between unclean exits and
 * crashes. Directly using ODS counters is unreliable as they may not update
 * before BGP shuts down. Instead, create a filesystem flag on planned exit to
 * check on the next startup. If the flag exists, previous exit was planned, and
 * we set the counter to 1 to indicate this; otherwise, previous exit was not
 * planned, and we set the counter to 0.
 */
DEFINE_string(
    exit_in_progress_file,
    // patternlint-disable-next-line no-dev-shm-usage
    "/dev/shm/bgp_exit_in_progress",
    "Empty file to persist exit-in-progress across BGP shutdown, to be handled on subsequent startup");

void setPlannedExit(bool plannedExitOccurred) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kPlannedExit, plannedExitOccurred ? 1 : 0);
}

void markPlannedExit() {
  writeFileAtomic(FLAGS_exit_in_progress_file);
}

void handlePreviousExit() {
  /*
   * check whether exit-in-progress file exists. If so, remove
   * and emit plannedExit=1. Otherwise emit plannedExit=0 (previous run
   * of bgpd potentially crashed. Need to correlate with other signals to
   * confirm)
   */
  if (boost::filesystem::exists(FLAGS_exit_in_progress_file)) {
    setPlannedExit(true);
    try {
      boost::filesystem::remove(FLAGS_exit_in_progress_file);
      XLOGF(
          INFO,
          "Previous exit-in-progress file {} removed",
          FLAGS_exit_in_progress_file);
    } catch (const std::exception& ex) {
      XLOGF(
          ERR,
          "Could not remove exit-in-progress file {}. Exception: {}",
          FLAGS_exit_in_progress_file,
          folly::exceptionStr(ex));
    }
  } else {
    setPlannedExit(false);
  }
}

DEFINE_dynamic_quantile_stat(
    ingressCrfProcessingTimeMs,
    kIngressRouteFilterPeerGroupProcessTimeMs,
    fb303::ExportTypeConsts::kAvg,
    std::array<double, 3>{{.5, .90, 1}},
    fb303::SlidingWindowPeriodConsts::kOneMinTenMin);

void addIngressRouteFilterPeerGroupProcessTimeMs(
    int64_t timeMs,
    const std::string& peerGroupName) {
  STATS_ingressCrfProcessingTimeMs.addValue(
      timeMs, kEbbPlatform, kBgpcppTag, peerGroupName);
}

void setIngressPolicyAllPeersLastReEvaluationTimeMs(int64_t timeMs) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      fmt::format(kIngressPolicyAllPeersLastReEvaluationTimeMs, kBgpcppTag),
      timeMs);
}

void incrCrfForceUpdateBypass() {
  fb303::ThreadCachedServiceData::get()->addStatValue(kCrfForceUpdateBypass, 1);
}

} // namespace BgpStats

//------------------------ RibStats ------------------------//
namespace RibStats {

void initCounters() {
  // init shadowRib entries size to be -1
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalShadowRibEntries, -1);

  fb303::ThreadCachedServiceData::get()->setCounter(kTotalRibPaths, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalAdjRibs, -1);
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalOriginatedRoutes, -1);

  fb303::ThreadCachedServiceData::get()->setCounter(
      kTotalRouteChurnDetected, 0);

  fb303::ThreadCachedServiceData::get()->setCounter(kRibTableVersion, 0);

  fb303::ThreadCachedServiceData::get()->setCounter(kRibPrefixCount, 0);

  fb303::ThreadCachedServiceData::get()->setCounter(
      kRibUnresolvableNexthopsCount, 0);

  fb303::ThreadCachedServiceData::get()->setCounter(
      kInactivePathsUnresolvedNexthopsCount, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kInactivePathsUnresolvedNexthopsCountIpv4, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kInactivePathsUnresolvedNexthopsCountIpv6, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kInactivePathCount, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kInactivePathCountIpv4, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kInactivePathCountIpv6, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalPathsCount, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalPathsCountIpv4, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalPathsCountIpv6, 0);

  fb303::ThreadCachedServiceData::get()->setCounter(kNexthopInfoCount, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kNexthopStatusMapCount, 0);

  // NHT cache reachability transition counters
  fb303::ThreadCachedServiceData::get()->setCounter(
      kNhtCacheNexthopReachable + ".count", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kNhtCacheNexthopReachable + ".count.60", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kNhtCacheNexthopUnreachable + ".count", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kNhtCacheNexthopUnreachable + ".count.60", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kNhtRibStaleUpdateSkipped + ".count", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kNhtRibStaleUpdateSkipped + ".count.60", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kNhtRibNexthopReachable + ".count", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kNhtRibNexthopReachable + ".count.60", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kNhtRibNexthopUnreachable + ".count", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kNhtRibNexthopUnreachable + ".count.60", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kNhtRibResolvabilityChangePrefixReevaluation + ".count", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kNhtRibResolvabilityChangePrefixReevaluation + ".count.60", 0);

  fb303::ThreadCachedServiceData::get()->setCounter(kAdjRibInCount, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kAdjRibInStaleCount, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kDeferredUpdatesCount, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kPostPolicyResultCacheCount, 0);
}

DEFINE_timeseries(rfPolicyRcvd, kRfPolicyRcvd, fb303::COUNT);
DEFINE_timeseries(rfPolicyUpdate, kRfPolicyUpdate, fb303::COUNT);
DEFINE_timeseries(ribPolicyMsgEnqueued, kRibPolicyMsgEnqueued, fb303::COUNT);
DEFINE_timeseries(ribPolicyMsgCoalesced, kRibPolicyMsgCoalesced, fb303::COUNT);
DEFINE_timeseries(ribPolicyMsgPurged, kRibPolicyMsgPurged, fb303::COUNT);

// time for rib to save rib policy to file
DEFINE_quantile_stat(
    saveRibPolicyStateToFileTimeMs,
    kSaveRibPolicyStateToFileTimeMs,
    fb303::ExportTypeConsts::kAvg,
    fb303::QuantileConsts::kP50_P95_P99,
    fb303::SlidingWindowPeriodConsts::kOneMinTenMin);
// time for rib to perform a path selection per route
DEFINE_quantile_stat(
    ribPathSelectionTimeMs,
    kRibPathSelectionTimeMs,
    fb303::ExportTypeConsts::kAvg,
    fb303::QuantileConsts::kP50_P95_P99,
    fb303::SlidingWindowPeriodConsts::kOneMinTenMin);
// time for rib to perform a path selection in a full sync
DEFINE_quantile_stat(
    ribFullSyncPathSelectionTimeMs,
    ribFullSyncPathSelectionTimeMs,
    fb303::ExportTypeConsts::kAvg,
    fb303::QuantileConsts::kP50_P95_P99,
    fb303::SlidingWindowPeriodConsts::kOneMinTenMin);
// time for which best path selection and fib programming is paused
DEFINE_quantile_stat(
    ribBestPathAndFibProgrammingPauseTimeMs,
    ribBestPathAndFibProgrammingPauseTimeMs,
    fb303::ExportTypeConsts::kAvg,
    fb303::QuantileConsts::kP50_P95_P99,
    fb303::SlidingWindowPeriodConsts::kOneMinTenMin);

// RouteAttributePolicy cache preservation counters
DEFINE_timeseries(raPolicyCacheHit, kRaPolicyCacheHit, fb303::COUNT);
DEFINE_timeseries(raPolicyCacheMiss, kRaPolicyCacheMiss, fb303::COUNT);
DEFINE_timeseries(
    raPolicyCommunityIndexHit,
    kRaPolicyCommunityIndexHit,
    fb303::COUNT);
DEFINE_timeseries(
    raPolicyCommunityIndexMiss,
    kRaPolicyCommunityIndexMiss,
    fb303::COUNT);
DEFINE_timeseries(
    raPolicyWeightIndexHit,
    kRaPolicyWeightIndexHit,
    fb303::COUNT);
DEFINE_timeseries(
    raPolicyWeightIndexMiss,
    kRaPolicyWeightIndexMiss,
    fb303::COUNT);

void publishShadowRibSize(uint64_t shadowRibEntryCount) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kTotalShadowRibEntries, shadowRibEntryCount);
}

void incrRouteChurnDetected() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kTotalRouteChurnDetected, 1);
}
void incrRibPaths() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(kTotalRibPaths, 1);
}
void decrRibPaths() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(kTotalRibPaths, -1);
}

void setAdjRibCount(uint64_t adjRibsSize) {
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalAdjRibs, adjRibsSize);
}

void incrAdjRibCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(kTotalAdjRibs, 1);
}

void decrAdjRibCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(kTotalAdjRibs, -1);
}

void setOriginatedRoutesSize(uint64_t localRoutesSize) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kTotalOriginatedRoutes, localRoutesSize);
}

void incrementRibTableVersion() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(kRibTableVersion);
}

void incrRibPrefixCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(kRibPrefixCount, 1);
}

void decrRibPrefixCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(kRibPrefixCount, -1);
}

void incrUnresolvableNexthopsCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kRibUnresolvableNexthopsCount, 1);
}
void decrUnresolvableNexthopsCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kRibUnresolvableNexthopsCount, -1);
}

void setInactivePathCount(int64_t count) {
  auto tcData = fb303::ThreadCachedServiceData::get();
  tcData->setCounter(kInactivePathsUnresolvedNexthopsCount, count);
  tcData->setCounter(kInactivePathCount, count);
}

void setInactivePathCountForAfi(bool isV4, int64_t count) {
  auto tcData = fb303::ThreadCachedServiceData::get();
  tcData->setCounter(
      isV4 ? kInactivePathsUnresolvedNexthopsCountIpv4
           : kInactivePathsUnresolvedNexthopsCountIpv6,
      count);
  tcData->setCounter(
      isV4 ? kInactivePathCountIpv4 : kInactivePathCountIpv6, count);
}

void setTotalPathsCount(int64_t count) {
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalPathsCount, count);
}

void setTotalPathCountForAfi(bool isV4, int64_t count) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      isV4 ? kTotalPathsCountIpv4 : kTotalPathsCountIpv6, count);
}

void incrNexthopInfoCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(kNexthopInfoCount, 1);
}

void decrNexthopInfoCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kNexthopInfoCount, -1);
}

void incrNexthopStatusMapCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kNexthopStatusMapCount, 1);
}

void decrNexthopStatusMapCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kNexthopStatusMapCount, -1);
}

DEFINE_timeseries(
    nhtCacheNexthopReachable,
    kNhtCacheNexthopReachable,
    fb303::COUNT);
DEFINE_timeseries(
    nhtCacheNexthopUnreachable,
    kNhtCacheNexthopUnreachable,
    fb303::COUNT);
void incrNhtCacheNexthopReachable() {
  STATS_nhtCacheNexthopReachable.add(1);
}

void incrNhtCacheNexthopUnreachable() {
  STATS_nhtCacheNexthopUnreachable.add(1);
}

DEFINE_timeseries(
    nhtRibStaleUpdateSkipped,
    kNhtRibStaleUpdateSkipped,
    fb303::COUNT);
void incrNhtRibStaleUpdateSkipped() {
  STATS_nhtRibStaleUpdateSkipped.add(1);
}

DEFINE_timeseries(
    nhtRibNexthopReachable,
    kNhtRibNexthopReachable,
    fb303::COUNT);
DEFINE_timeseries(
    nhtRibNexthopUnreachable,
    kNhtRibNexthopUnreachable,
    fb303::COUNT);
DEFINE_timeseries(
    nhtRibResolvabilityChangePrefixReevaluation,
    kNhtRibResolvabilityChangePrefixReevaluation,
    fb303::COUNT);

void incrNhtRibNexthopReachable() {
  STATS_nhtRibNexthopReachable.add(1);
}

void incrNhtRibNexthopUnreachable() {
  STATS_nhtRibNexthopUnreachable.add(1);
}

void incrNhtRibResolvabilityChangePrefixReevaluation() {
  STATS_nhtRibResolvabilityChangePrefixReevaluation.add(1);
}

DEFINE_quantile_stat(
    fibBatchListSize,
    kFibBatchListSize,
    fb303::ExportTypeConsts::kAvg,
    fb303::QuantileConsts::kP50_P95_P99,
    fb303::SlidingWindowPeriodConsts::kOneMinTenMin);

void addFibBatchListSize(int64_t size) {
  STATS_fibBatchListSize.addValue(size);
}

void incrAdjRibInCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(kAdjRibInCount, 1);
}

void decrAdjRibInCount(uint32_t count) {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kAdjRibInCount, -static_cast<int64_t>(count));
}

void incrAdjRibInStaleCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kAdjRibInStaleCount, 1);
}

void decrAdjRibInStaleCount(uint32_t count) {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kAdjRibInStaleCount, -static_cast<int64_t>(count));
}

void incrDeferredUpdatesCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kDeferredUpdatesCount, 1);
}

void decrDeferredUpdatesCount(uint32_t count) {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kDeferredUpdatesCount, -static_cast<int64_t>(count));
}

void incrPostPolicyResultCacheCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kPostPolicyResultCacheCount, 1);
}

void decrPostPolicyResultCacheCount() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kPostPolicyResultCacheCount, -1);
}

} // namespace RibStats

//------------------------ FibStats ------------------------//

// Bgp Fib level stats
namespace FibStats {

void initCounters() {
  fb303::ThreadCachedServiceData::get()->setCounter(kAgentUpdateFailures, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kAgentStatusFailures, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalUcastRoutesKey, -1);
  fb303::ThreadCachedServiceData::get()->setCounter(kFibSyncStatus, -1);
  fb303::ThreadCachedServiceData::get()->setCounter(kAgentProgrammable, -1);
  fb303::ThreadCachedServiceData::get()->addStatExportType(
      kFibUcastUpdates, fb303::SUM);
  /*
   * Initialize FIB flush counter - set both base and .sum counters
   * so getCounter works before addStatValue is called
   */
  fb303::ThreadCachedServiceData::get()->setCounter(
      std::string(kFibFlushed) + ".sum", 0);
  fb303::ThreadCachedServiceData::get()->addStatExportType(
      kFibFlushed, fb303::SUM);
}

void addAgentUpdateFailures() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kAgentUpdateFailures, 1);
}

void addAgentStatusFailures() {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      kAgentStatusFailures, 1);
}

void addFibUcastUpdates() {
  fb303::ThreadCachedServiceData::get()->addStatValue(kFibUcastUpdates, 1);
}

void publishTotalUCastRoutes(uint32_t routeCount) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kTotalUcastRoutesKey, routeCount);
}

void setFibSyncStatus(bool isSynced) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kFibSyncStatus, isSynced ? 1 : 0);
}

/*
 * Set agentProgrammable to
 * true: 1 , false: 0
 */
void setAgentProgrammable(bool programmable) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kAgentProgrammable, programmable ? 1 : 0);
}

void incrFibFlushed() {
  fb303::ThreadCachedServiceData::get()->addStatValue(kFibFlushed, 1);
}

// FIB sync duration
DEFINE_quantile_stat(
    fibSyncTimeMs,
    kFibSyncTimeMs,
    fb303::ExportTypeConsts::kAvg,
    std::array<double, 3>{{.5, .90, 1}}, // p50, p90, p100
    fb303::SlidingWindowPeriodConsts::kOneMinTenMin);

void addFibSyncTimeMs(int64_t timeMs) {
  STATS_fibSyncTimeMs.addValue(timeMs);
}

} // namespace FibStats

//------------------------ PeerStats ------------------------//

// Bgp Peer level stats
namespace PeerStats {
void initCounters() {
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalRcvdPrefixes, -1);
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalAcceptedPrefixes, -1);
  setNumRswSlbInRejections(0);
  /*
   * Initialize to 0 (not -1 like the sibling "not yet reported" counters) so
   * droppedPrefixes always shows a real number. Every capacity/overload drop
   * path increments it, so 0 unambiguously means "no prefixes dropped" rather
   * than "never evaluated". See S676351.
   */
  fb303::ThreadCachedServiceData::get()->setCounter(
      kTotalPrefixesDroppedByLimit, 0);
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalPaths, -1);
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalSentPrefixes, -1);
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalUniquePrefixes, -1);
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalVipPrefixes, -1);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kTotalGoldenVipPrefixes, -1);
  fb303::ThreadCachedServiceData::get()->setCounter(kMaxPeerRcvdPrefixes, -1);
  fb303::ThreadCachedServiceData::get()->setCounter(
      kTotalPeerWithNoRouteExchange, 0);

  initBytesReadAvg();
  initBytesWrittenAvg();
  initMessagesSent(kMessagesSentOpen);
  initMessagesSent(kMessagesSentNotification);
  initMessagesSent(kMessagesSentKeepAlive);
  initMessagesSent(kMessagesSentUpdate);
  initMessagesSent(kMessagesSentEndOfRib);
  initMessagesSent(kMessagesSentRouteRefresh);
  initMessagesSent(kMessagesSentSocketFailure);
  initMessagesSent(kMessageSentAnnouncedIpv4);
  initMessagesSent(kMessageSentAnnouncedIpv6);
  initMessagesSent(kMessageSentWithdraw);

  initMessagesRecv(kMessageRecvOpen);
  initMessagesRecv(kMessageRecvUpdate);
  initMessagesRecv(kMessageRecvNotification);
  initMessagesRecv(kMessageRecvKeepAlive);
  initMessagesRecv(kMessageRecvRouteRefresh);
  initMessagesRecv(kMessageRecvAnnouncedIpv4);
  initMessagesRecv(kMessageRecvAnnouncedIpv6);
  initMessagesRecv(kMessageRecvWithdraw);

  fb303::ThreadCachedServiceData::get()->addStatExportType(
      kNoGrRestart, fb303::COUNT);
  fb303::ThreadCachedServiceData::get()->addStatExportType(
      kTotalHoldTimerExpiry, fb303::COUNT);
  fb303::ThreadCachedServiceData::get()->addStatExportType(
      kTotalPurgeForNonGr, fb303::COUNT);
  fb303::ThreadCachedServiceData::get()->addStatExportType(
      kTotalPurgeForGrDoubleFailure, fb303::COUNT);
  fb303::ThreadCachedServiceData::get()->addStatExportType(
      kTotalPurgeForGrRestartTimer, fb303::COUNT);
  fb303::ThreadCachedServiceData::get()->addStatExportType(
      kTotalPurgeForStalePathTimer, fb303::COUNT);

  initAttributeSize(kAttributeSizeAsPath);
  initAttributeSize(kAttributeSizeCommunity);
  initAttributeSize(kAttributeSizeExtendedCommunity);
  initAttributeSize(kAttributeSizeClusterList);
  initAttributeSize(kAttributeSizeTopologyInfo);

  fb303::ThreadCachedServiceData::get()->addStatExportType(
      kRejectedInboundRoutes, fb303::COUNT);
  fb303::ThreadCachedServiceData::get()->addStatExportType(
      kRejectectedOutboundRoutes, fb303::COUNT);
}

namespace {
/*
 * All per-peer counters keyed by peerIdOdsStr. Both initPeerCounters and
 * clearPeerCounters apply their operation through this helper, ensuring
 * symmetry. Add new per-peer counters here to get init + cleanup for free.
 *
 * kPeerSessionStateChanges and kNoGrRestartPeer are Stats rather than
 * Counters. clearPeerCounters removes their global and thread-local storage
 * separately after this helper clears the flat counters.
 */
template <typename Fn>
void forEachPeerCounterKey(const std::string& peerIdOdsStr, Fn&& fn) {
  // peer_{} — single arg (peerId)
  fn(fmt::format(kPeerPreInPrefixes, peerIdOdsStr));
  fn(fmt::format(kPeerPostInPrefixes, peerIdOdsStr));
  fn(fmt::format(kPeerPreOutPrefixes, peerIdOdsStr));
  fn(fmt::format(kPeerPostOutPrefixes, peerIdOdsStr));
  // {}.{}.peer_{} — three args (platform, tag, peerId)
  fn(fmt::format(
      kPeerIngressRouteFilterDenied, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(kPeerTableVersion, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesSentUpdate, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesSentAnnouncedIpv4, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesSentAnnouncedIpv6, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesSentWithdraw, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesSentOpen, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesSentNotification, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesSentKeepAlive, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesSentEndOfRib, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesSentRouteRefresh, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesSentSocketFailure, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerSocketEgressBuffered, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesRecvUpdate, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesRecvAnnouncedIpv4, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesRecvAnnouncedIpv6, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesRecvWithdraw, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesRecvOpen, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesRecvNotification, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesRecvKeepAlive, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesRecvRouteRefresh, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
}

/*
 * Per-peer EGRESS (sent) message counters only. Used by
 * clearPeerEgressMessageCounters. Does NOT include prefix gauges (which are
 * live state) or recv counters.
 */
template <typename Fn>
void forEachPeerEgressMessageKey(const std::string& peerIdOdsStr, Fn&& fn) {
  fn(fmt::format(
      kPeerMessagesSentUpdate, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesSentAnnouncedIpv4, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesSentAnnouncedIpv6, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesSentWithdraw, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesSentOpen, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesSentNotification, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesSentKeepAlive, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesSentEndOfRib, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesSentRouteRefresh, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesSentSocketFailure, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerSocketEgressBuffered, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
}

/*
 * Per-peer INGRESS (recv) message counters only. Used by
 * clearPeerIngressMessageCounters. Does NOT include prefix gauges or sent
 * counters.
 */
template <typename Fn>
void forEachPeerIngressMessageKey(const std::string& peerIdOdsStr, Fn&& fn) {
  fn(fmt::format(
      kPeerMessagesRecvUpdate, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesRecvAnnouncedIpv4, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesRecvAnnouncedIpv6, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesRecvWithdraw, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesRecvOpen, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesRecvNotification, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesRecvKeepAlive, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
  fn(fmt::format(
      kPeerMessagesRecvRouteRefresh, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
}

void removeCurrentThreadTimeseries(const std::string& key) {
  auto& serviceData = *CHECK_NOTNULL(fb303::ThreadCachedServiceData::get());
  serviceData.getStatMap()->unExportStatAll(key);
  serviceData.getThreadStats()->clearTimeseriesSafe(key);
}
} // namespace

void initPeerCounters(const std::string& peerIdOdsStr) {
  forEachPeerCounterKey(peerIdOdsStr, [](const std::string& key) {
    fb303::ThreadCachedServiceData::get()->setCounter(key, 0);
  });
}

void clearPeerCounters(
    const std::string& peerIdOdsStr,
    const std::string& noGrRestartPeerId) {
  forEachPeerCounterKey(peerIdOdsStr, [](const std::string& key) {
    fb303::ThreadCachedServiceData::get()->clearCounter(key);
  });
  fb303::ThreadCachedServiceData::get()->clearCounter(
      fmt::format(kPeerStatus, peerIdOdsStr));
  removeCurrentThreadTimeseries(
      fmt::format(kPeerSessionStateChanges, peerIdOdsStr));
  removeCurrentThreadTimeseries(
      fmt::format(kNoGrRestartPeer, noGrRestartPeerId));
}

void clearPeerEgressMessageCounters(const std::string& peerIdOdsStr) {
  forEachPeerEgressMessageKey(peerIdOdsStr, [](const std::string& key) {
    fb303::ThreadCachedServiceData::get()->clearCounter(key);
  });
}

void clearPeerIngressMessageCounters(const std::string& peerIdOdsStr) {
  forEachPeerIngressMessageKey(peerIdOdsStr, [](const std::string& key) {
    fb303::ThreadCachedServiceData::get()->clearCounter(key);
  });
}

void setTotalRcvdPrefixes(uint32_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalRcvdPrefixes, val);
}
void setTotalAcceptedPrefixes(uint32_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kTotalAcceptedPrefixes, val);
}
void setNumRswSlbInRejections(int64_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kNumRswSlbInRejections, val);
}
void incrementTotalPrefixesDroppedByLimit(uint32_t val) {
  /*
   * Use the global ServiceData (not the thread-cached wrapper) so the increment
   * writes through immediately and is visible to getCounter() on any thread.
   */
  fb303::fbData->incrementCounter(kTotalPrefixesDroppedByLimit, val);
}
void setTotalPaths(uint32_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalPaths, val);
}
void setTotalSentPrefixes(uint32_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalSentPrefixes, val);
}
void setTotalUniquePrefixes(uint32_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalUniquePrefixes, val);
}

void setTotalVipPrefixes(uint32_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(kTotalVipPrefixes, val);
}
void setTotalGoldenVipPrefixes(uint32_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kTotalGoldenVipPrefixes, val);
}
void setTotalPeerWithNoRouteExchange(uint32_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      kTotalPeerWithNoRouteExchange, val);
}
void setMaxPeerRcvdPrefixes(uint32_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(kMaxPeerRcvdPrefixes, val);
}
void incrNoGrRestart() {
  fb303::ThreadCachedServiceData::get()->addStatValue(
      kNoGrRestart, 1, fb303::COUNT);
}
void incrPeerNoGrRestart(const std::string& peerId) {
  /*
   * This dynamically named stat is deleted by PeerManager. Keep it out of
   * SessionManager TLS so deletion never mutates another thread's named map.
   */
  fb303::fbData->addStatValue(
      fmt::format(kNoGrRestartPeer, peerId), 1, fb303::COUNT);
}
void incrTotalHoldTimerExpiry() {
  fb303::ThreadCachedServiceData::get()->addStatValue(
      kTotalHoldTimerExpiry, 1, fb303::COUNT);
}
void incrTotalPurgeForNonGr() {
  fb303::ThreadCachedServiceData::get()->addStatValue(
      kTotalPurgeForNonGr, 1, fb303::COUNT);
}
void incrTotalPurgeForGrDoubleFailure() {
  fb303::ThreadCachedServiceData::get()->addStatValue(
      kTotalPurgeForGrDoubleFailure, 1, fb303::COUNT);
}
void incrTotalPurgeForGrRestartTimer() {
  fb303::ThreadCachedServiceData::get()->addStatValue(
      kTotalPurgeForGrRestartTimer, 1, fb303::COUNT);
}
void incrTotalPurgeForStalePathTimer() {
  fb303::ThreadCachedServiceData::get()->addStatValue(
      kTotalPurgeForStalePathTimer, 1, fb303::COUNT);
}
void setPeerPreInPrefixes(const std::string& peerIdOdsStr, uint32_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      fmt::format(kPeerPreInPrefixes, peerIdOdsStr), val);
}
void setPeerPostInPrefixes(const std::string& peerIdOdsStr, uint32_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      fmt::format(kPeerPostInPrefixes, peerIdOdsStr), val);
}
void setPeerPreOutPrefixes(const std::string& peerIdOdsStr, uint32_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      fmt::format(kPeerPreOutPrefixes, peerIdOdsStr), val);
}
void setPeerPostOutPrefixes(const std::string& peerIdOdsStr, uint32_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      fmt::format(kPeerPostOutPrefixes, peerIdOdsStr), val);
}
void addPeerSessionStateChanges(const std::string& peerIdOdsStr) {
  fb303::ThreadCachedServiceData::get()->addStatValue(
      fmt::format(kPeerSessionStateChanges, peerIdOdsStr), 1);
}
void setPeerStatus(const std::string& peerIdOdsStr, uint32_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      fmt::format(kPeerStatus, peerIdOdsStr), val);
}
void setPeerTableVersion(const std::string& peerIdOdsStr, uint64_t val) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      fmt::format(kPeerTableVersion, kEbbPlatform, kBgpcppTag, peerIdOdsStr),
      val);
}
void addPeerIngressRouteFilterDenied(const std::string& peerIdOdsStr) {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerIngressRouteFilterDenied,
          kEbbPlatform,
          kBgpcppTag,
          peerIdOdsStr));
}
void addPeerMessagesSentUpdate(
    const std::string& peerIdOdsStr,
    uint64_t count) {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerMessagesSentUpdate, kEbbPlatform, kBgpcppTag, peerIdOdsStr),
      count);
}
void addPeerMessagesRecvUpdate(const std::string& peerIdOdsStr) {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerMessagesRecvUpdate, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
}
void addPeerMessagesRecvOpen(const std::string& peerIdOdsStr) {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerMessagesRecvOpen, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
}
void addPeerMessagesRecvNotification(const std::string& peerIdOdsStr) {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerMessagesRecvNotification,
          kEbbPlatform,
          kBgpcppTag,
          peerIdOdsStr));
}
void addPeerMessagesRecvKeepAlive(const std::string& peerIdOdsStr) {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerMessagesRecvKeepAlive, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
}
void addPeerMessagesRecvRouteRefresh(const std::string& peerIdOdsStr) {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerMessagesRecvRouteRefresh,
          kEbbPlatform,
          kBgpcppTag,
          peerIdOdsStr));
}

DEFINE_timeseries(
    peer_socket_bytes_read,
    fb303::MinuteHourDayTimeSeries<int64_t>(),
    fb303::AVG);
void initBytesReadAvg() {
  fb303::ThreadCachedServiceData::get()->setCounter(
      "peer_socket_bytes_read.avg.60", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      "peer_socket_bytes_read.avg.86400", 0);
}
void addBytesReadToAvg(size_t bytesRead) {
  STATS_peer_socket_bytes_read.add(bytesRead);
}
DEFINE_timeseries(
    peer_socket_bytes_written,
    fb303::MinuteHourDayTimeSeries<int64_t>(),
    fb303::AVG);
void initBytesWrittenAvg() {
  fb303::ThreadCachedServiceData::get()->setCounter(
      "peer_socket_bytes_written.avg.60", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      "peer_socket_bytes_written.avg.86400", 0);
}
void addBytesWrittenToAvg(size_t bytesWritten) {
  STATS_peer_socket_bytes_written.add(bytesWritten);
}

DEFINE_dynamic_timeseries(messagesSent, kMessagesSent, fb303::COUNT);
void initMessagesSent(const std::string& subkey) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      fmt::format(kMessagesSent, subkey) + ".count", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      fmt::format(kMessagesSent, subkey) + ".count.60", 0);
}
void incrOpenMessagesSent(const std::string& peerIdOdsStr) {
  STATS_messagesSent.add(1, kMessagesSentOpen);
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerMessagesSentOpen, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
}
void incrNotificationMessagesSent(const std::string& peerIdOdsStr) {
  STATS_messagesSent.add(1, kMessagesSentNotification);
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerMessagesSentNotification,
          kEbbPlatform,
          kBgpcppTag,
          peerIdOdsStr));
}
void incrKeepAliveMessagesSent(const std::string& peerIdOdsStr) {
  STATS_messagesSent.add(1, kMessagesSentKeepAlive);
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerMessagesSentKeepAlive, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
}
void incrUpdateMessagesSent() {
  STATS_messagesSent.add(1, kMessagesSentUpdate);
}
void incrEndOfRibMessagesSent(const std::string& peerIdOdsStr) {
  STATS_messagesSent.add(1, kMessagesSentEndOfRib);
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerMessagesSentEndOfRib, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
}
void incrRouteRefreshMessagesSent(const std::string& peerIdOdsStr) {
  STATS_messagesSent.add(1, kMessagesSentRouteRefresh);
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerMessagesSentRouteRefresh,
          kEbbPlatform,
          kBgpcppTag,
          peerIdOdsStr));
}
void incrMessagesSentSocketFailures(const std::string& peerIdOdsStr) {
  STATS_messagesSent.add(1, kMessagesSentSocketFailure);
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerMessagesSentSocketFailure,
          kEbbPlatform,
          kBgpcppTag,
          peerIdOdsStr));
}
void incrSocketEgressBuffered(std::string_view peerIdOdsStr) {
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerSocketEgressBuffered, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
}
void incrMessageSentAnnouncedIpv4(const std::string& peerIdOdsStr) {
  STATS_messagesSent.add(1, kMessageSentAnnouncedIpv4);
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerMessagesSentAnnouncedIpv4,
          kEbbPlatform,
          kBgpcppTag,
          peerIdOdsStr));
}
void incrMessageSentAnnouncedIpv6(const std::string& peerIdOdsStr) {
  STATS_messagesSent.add(1, kMessageSentAnnouncedIpv6);
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerMessagesSentAnnouncedIpv6,
          kEbbPlatform,
          kBgpcppTag,
          peerIdOdsStr));
}
void incrMessageSentWithdraw(const std::string& peerIdOdsStr) {
  STATS_messagesSent.add(1, kMessageSentWithdraw);
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerMessagesSentWithdraw, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
}

DEFINE_dynamic_timeseries(messagesRecv, kMessagesRecv, fb303::COUNT);
void initMessagesRecv(const std::string& subkey) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      fmt::format(kMessagesRecv, subkey) + ".count", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      fmt::format(kMessagesRecv, subkey) + ".count.60", 0);
}
void incrMessageRecvOpen() {
  STATS_messagesRecv.add(1, kMessageRecvOpen);
}
void incrMessageRecvUpdate() {
  STATS_messagesRecv.add(1, kMessageRecvUpdate);
}
void incrMessageRecvNotification() {
  STATS_messagesRecv.add(1, kMessageRecvNotification);
}
void incrMessageRecvKeepAlive() {
  STATS_messagesRecv.add(1, kMessageRecvKeepAlive);
}
void incrMessageRecvRouteRefresh() {
  STATS_messagesRecv.add(1, kMessageRecvRouteRefresh);
}
void incrMessageRecvAnnouncedIpv4(const std::string& peerIdOdsStr) {
  STATS_messagesRecv.add(1, kMessageRecvAnnouncedIpv4);
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerMessagesRecvAnnouncedIpv4,
          kEbbPlatform,
          kBgpcppTag,
          peerIdOdsStr));
}
void incrMessageRecvAnnouncedIpv6(const std::string& peerIdOdsStr) {
  STATS_messagesRecv.add(1, kMessageRecvAnnouncedIpv6);
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerMessagesRecvAnnouncedIpv6,
          kEbbPlatform,
          kBgpcppTag,
          peerIdOdsStr));
}
void incrMessageRecvWithdraw(const std::string& peerIdOdsStr) {
  STATS_messagesRecv.add(1, kMessageRecvWithdraw);
  fb303::ThreadCachedServiceData::get()->incrementCounter(
      fmt::format(
          kPeerMessagesRecvWithdraw, kEbbPlatform, kBgpcppTag, peerIdOdsStr));
}

DEFINE_timeseries(
    peer_update_bytes_sent,
    fb303::MinuteHourDayTimeSeries<int64_t>(),
    fb303::AVG);
void addUpdateBytesSentToAvg(size_t bytesSent) {
  STATS_peer_update_bytes_sent.add(bytesSent);
}

DEFINE_timeseries(
    peer_update_bytes_recv,
    fb303::MinuteHourDayTimeSeries<int64_t>(),
    fb303::AVG);
void addUpdateBytesRecvToAvg(size_t bytesRecv) {
  STATS_peer_update_bytes_recv.add(bytesRecv);
}

DEFINE_dynamic_timeseries(
    attributeSize,
    kAttributeSize,
    fb303::MinuteHourDayTimeSeries<int64_t>(),
    fb303::AVG);
void initAttributeSize(const std::string& subkey) {
  fb303::ThreadCachedServiceData::get()->setCounter(
      fmt::format(kAttributeSize, subkey) + ".avg.60", 0);
  fb303::ThreadCachedServiceData::get()->setCounter(
      fmt::format(kAttributeSize, subkey) + ".avg.86400", 0);
}

void addAsPathSizeToAvg(size_t asPathSize) {
  STATS_attributeSize.add(asPathSize, kAttributeSizeAsPath);
}
void addCommunitySizeToAvg(size_t communitySize) {
  STATS_attributeSize.add(communitySize, kAttributeSizeCommunity);
}
void addExtendedCommunitySizeToAvg(size_t extendedCommunitySize) {
  STATS_attributeSize.add(
      extendedCommunitySize, kAttributeSizeExtendedCommunity);
}
void addClusterListSizeToAvg(size_t clusterListSize) {
  STATS_attributeSize.add(clusterListSize, kAttributeSizeClusterList);
}
void addTopologyInfoSizeToAvg(size_t topologyInfoSize) {
  STATS_attributeSize.add(topologyInfoSize, kAttributeSizeTopologyInfo);
}

DEFINE_timeseries(
    peer_rejected_inbound_routes,
    kRejectedInboundRoutes,
    fb303::COUNT);
void incrRejectedInboundRoutes() {
  STATS_peer_rejected_inbound_routes.add(1);
}

DEFINE_timeseries(
    peer_rejected_outbound_routes,
    kRejectectedOutboundRoutes,
    fb303::COUNT);
void incrRejectedOutboundRoutes() {
  STATS_peer_rejected_outbound_routes.add(1);
}

DEFINE_timeseries(
    peer_invalid_nexthop_encoding,
    kInvalidNexthopEncoding,
    fb303::COUNT);
void incrInvalidNexthopEncoding() {
  STATS_peer_invalid_nexthop_encoding.add(1);
}

DEFINE_timeseries(
    empty_gar_weights_rejects,
    kEmptyGarWeightsRejects,
    fb303::COUNT);
void incrEmptyGarWeightsRejects() {
  STATS_empty_gar_weights_rejects.add(1);
}

DEFINE_timeseries(
    connection_collision_closed,
    kConnectionCollisionClosed,
    fb303::COUNT);
void incrConnectionCollisionClosed() {
  STATS_connection_collision_closed.add(1);
}

DEFINE_timeseries(
    connection_collision_closed_by_peer,
    kConnectionCollisionClosedByPeer,
    fb303::COUNT);
void incrConnectionCollisionClosedByPeer() {
  STATS_connection_collision_closed_by_peer.add(1);
}

} // namespace PeerStats

void initStatsBase() {
  BgpStats::initCounters();
  RibStats::initCounters();
  FibStats::initCounters();
  PeerStats::initCounters();
}

} // namespace facebook::bgp
