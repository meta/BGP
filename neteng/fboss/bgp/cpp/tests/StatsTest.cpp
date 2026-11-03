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

#include <gtest/gtest.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include "fb303/ServiceData.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

using facebook::neteng::fboss::bgp::thrift::BgpInitializationEvent;

namespace facebook::bgp {

// helper to get the peer message sent key
const std::string createPeerMessageSentKey(const std::string& subkey) {
  return fmt::format(PeerStats::kMessagesSent, subkey) + ".count";
}
// helper to get the peer message sent key
const std::string createPeerMessageRecvKey(const std::string& subkey) {
  return fmt::format(PeerStats::kMessagesRecv, subkey) + ".count";
}

const std::string createAttributeSizeKey(const std::string& subkey) {
  return fmt::format(PeerStats::kAttributeSize, subkey) + ".avg.60";
}

TEST(StatsTest, RibStatsInitCounterTest) {
  {
    // validate empty counters
    auto counters = fb303::ThreadCachedServiceData::getShared();
    EXPECT_FALSE(counters->hasCounter(RibStats::kTotalShadowRibEntries));
    EXPECT_FALSE(counters->hasCounter(RibStats::kTotalRibPaths));
    EXPECT_FALSE(counters->hasCounter(RibStats::kTotalAdjRibs));
    EXPECT_FALSE(counters->hasCounter(RibStats::kTotalOriginatedRoutes));
    EXPECT_FALSE(counters->hasCounter(RibStats::kTotalRouteChurnDetected));
    EXPECT_FALSE(counters->hasCounter(RibStats::kRibTableVersion));
    EXPECT_FALSE(counters->hasCounter(RibStats::kRibPrefixCount));
    EXPECT_FALSE(
        counters->hasCounter(RibStats::kInactivePathsUnresolvedNexthopsCount));
    EXPECT_FALSE(counters->hasCounter(
        RibStats::kInactivePathsUnresolvedNexthopsCountIpv4));
    EXPECT_FALSE(counters->hasCounter(
        RibStats::kInactivePathsUnresolvedNexthopsCountIpv6));
    EXPECT_FALSE(counters->hasCounter(RibStats::kInactivePathCount));
    EXPECT_FALSE(counters->hasCounter(RibStats::kInactivePathCountIpv4));
    EXPECT_FALSE(counters->hasCounter(RibStats::kInactivePathCountIpv6));
    EXPECT_FALSE(counters->hasCounter(RibStats::kTotalPathsCount));
    EXPECT_FALSE(counters->hasCounter(RibStats::kTotalPathsCountIpv4));
    EXPECT_FALSE(counters->hasCounter(RibStats::kTotalPathsCountIpv6));
    EXPECT_FALSE(counters->hasCounter(RibStats::kNexthopInfoCount));
    EXPECT_FALSE(counters->hasCounter(RibStats::kNexthopStatusMapCount));
    EXPECT_FALSE(counters->hasCounter(RibStats::kAdjRibInCount));
    EXPECT_FALSE(counters->hasCounter(RibStats::kAdjRibInStaleCount));
    EXPECT_FALSE(counters->hasCounter(RibStats::kDeferredUpdatesCount));
    EXPECT_FALSE(counters->hasCounter(RibStats::kPostPolicyResultCacheCount));
  }
  {
    // initialize RibStats counters
    RibStats::initCounters();
  }
  {
    auto counters = fb303::ThreadCachedServiceData::getShared();
    EXPECT_EQ(-1, counters->getCounter(RibStats::kTotalShadowRibEntries));
    EXPECT_EQ(0, counters->getCounter(RibStats::kTotalRibPaths));
    EXPECT_EQ(-1, counters->getCounter(RibStats::kTotalAdjRibs));
    EXPECT_EQ(-1, counters->getCounter(RibStats::kTotalOriginatedRoutes));
    EXPECT_EQ(0, counters->getCounter(RibStats::kTotalRouteChurnDetected));
    EXPECT_EQ(0, counters->getCounter(RibStats::kRibTableVersion));
    EXPECT_EQ(0, counters->getCounter(RibStats::kRibPrefixCount));
    EXPECT_EQ(
        0,
        counters->getCounter(RibStats::kInactivePathsUnresolvedNexthopsCount));
    EXPECT_EQ(
        0,
        counters->getCounter(
            RibStats::kInactivePathsUnresolvedNexthopsCountIpv4));
    EXPECT_EQ(
        0,
        counters->getCounter(
            RibStats::kInactivePathsUnresolvedNexthopsCountIpv6));
    EXPECT_EQ(0, counters->getCounter(RibStats::kInactivePathCount));
    EXPECT_EQ(0, counters->getCounter(RibStats::kInactivePathCountIpv4));
    EXPECT_EQ(0, counters->getCounter(RibStats::kInactivePathCountIpv6));
    EXPECT_EQ(0, counters->getCounter(RibStats::kTotalPathsCount));
    EXPECT_EQ(0, counters->getCounter(RibStats::kTotalPathsCountIpv4));
    EXPECT_EQ(0, counters->getCounter(RibStats::kTotalPathsCountIpv6));
    EXPECT_EQ(0, counters->getCounter(RibStats::kNexthopInfoCount));
    EXPECT_EQ(0, counters->getCounter(RibStats::kNexthopStatusMapCount));
    EXPECT_EQ(0, counters->getCounter(RibStats::kAdjRibInCount));
    EXPECT_EQ(0, counters->getCounter(RibStats::kAdjRibInStaleCount));
    EXPECT_EQ(0, counters->getCounter(RibStats::kDeferredUpdatesCount));
    EXPECT_EQ(0, counters->getCounter(RibStats::kPostPolicyResultCacheCount));
  }
}

TEST(StatsTest, FibStatsInitCounterTest) {
  // For SUM-type timeseries stats, read with ".sum" suffix
  const std::string kFibFlushedSum =
      std::string(FibStats::kFibFlushed) + ".sum";
  {
    // validate empty counters
    auto counters = fb303::ThreadCachedServiceData::getShared();
    EXPECT_FALSE(counters->hasCounter(FibStats::kAgentUpdateFailures));
    EXPECT_FALSE(counters->hasCounter(FibStats::kAgentStatusFailures));
    EXPECT_FALSE(counters->hasCounter(FibStats::kTotalUcastRoutesKey));
    EXPECT_FALSE(counters->hasCounter(FibStats::kFibSyncStatus));
    EXPECT_FALSE(counters->hasCounter(FibStats::kAgentProgrammable));
    EXPECT_FALSE(counters->hasCounter(kFibFlushedSum));
  }
  {
    // initialize FibStats counters
    FibStats::initCounters();
  }
  {
    auto counters = fb303::ThreadCachedServiceData::getShared();
    EXPECT_EQ(0, counters->getCounter(FibStats::kAgentUpdateFailures));
    EXPECT_EQ(0, counters->getCounter(FibStats::kAgentStatusFailures));
    EXPECT_EQ(-1, counters->getCounter(FibStats::kTotalUcastRoutesKey));
    EXPECT_EQ(-1, counters->getCounter(FibStats::kFibSyncStatus));
    EXPECT_EQ(-1, counters->getCounter(FibStats::kAgentProgrammable));
    EXPECT_EQ(0, counters->getCounter(kFibFlushedSum));
  }
}

TEST(StatsTest, SafeModeCounterTest) {
  {
    auto counters = fb303::ThreadCachedServiceData::getShared();
    EXPECT_FALSE(counters->hasCounter(BgpStats::kIsSafeModeOn));
  }
  {
    // initialize BgpStats counters
    BgpStats::initCounters();
  }
  {
    auto counters = fb303::ThreadCachedServiceData::getShared();
    EXPECT_EQ(0, counters->getCounter(BgpStats::kIsSafeModeOn));
  }
}

TEST(StatsTest, EgressBackpressureStatsTest) {
  auto counters = fb303::ThreadCachedServiceData::getShared();
  EXPECT_FALSE(
      counters->hasCounter(BgpStats::kEgressTransientUpdatesSuppressed));
  EXPECT_FALSE(counters->hasCounter(BgpStats::kEgressQueueBackpressuredEvents));

  BgpStats::initCounters();

  /*
   * Guard against the prior bug where DEFINE_timeseries omitted the key
   * argument and counters were published under the bare symbol name
   * (without the "bgpd." prefix). Those names must never appear.
   */
  EXPECT_FALSE(
      counters->hasCounter("egress_transient_route_updates_suppressed.count"));
  EXPECT_FALSE(counters->hasCounter(
      "egress_transient_route_updates_suppressed.count.60"));
  EXPECT_FALSE(counters->hasCounter("egress_queue_backpressured_events.count"));
  EXPECT_FALSE(
      counters->hasCounter("egress_queue_backpressured_events.count.60"));
  EXPECT_EQ(
      0,
      counters->getCounter(
          BgpStats::kEgressTransientUpdatesSuppressed + ".count"));
  EXPECT_EQ(
      0,
      counters->getCounter(
          BgpStats::kEgressTransientUpdatesSuppressed + ".count.60"));
  EXPECT_EQ(
      0,
      counters->getCounter(
          BgpStats::kEgressQueueBackpressuredEvents + ".count"));
  EXPECT_EQ(
      0,
      counters->getCounter(
          BgpStats::kEgressQueueBackpressuredEvents + ".count.60"));

  /* Check counters are incremented. */
  BgpStats::incrementEgressQueueBackpressuredEvents();
  BgpStats::incrementEgressTransientRouteUpdatesSuppressed();
  fb303::ThreadCachedServiceData::get()->publishStats();

  EXPECT_EQ(
      1,
      counters->getCounter(
          BgpStats::kEgressTransientUpdatesSuppressed + ".count"));
  EXPECT_EQ(
      1,
      counters->getCounter(
          BgpStats::kEgressTransientUpdatesSuppressed + ".count.60"));
  EXPECT_EQ(
      1,
      counters->getCounter(
          BgpStats::kEgressQueueBackpressuredEvents + ".count"));
  EXPECT_EQ(
      1,
      counters->getCounter(
          BgpStats::kEgressQueueBackpressuredEvents + ".count.60"));
}

TEST(StatsTest, RibPrefixCountTest) {
  RibStats::initCounters();
  auto tcData = fb303::ThreadCachedServiceData::get();

  // Verify initial value is 0
  EXPECT_EQ(0, tcData->getCounter(RibStats::kRibPrefixCount));

  // Increment prefix count
  RibStats::incrRibPrefixCount();
  RibStats::incrRibPrefixCount();
  tcData->publishStats();
  EXPECT_EQ(2, tcData->getCounter(RibStats::kRibPrefixCount));

  // Decrement prefix count
  RibStats::decrRibPrefixCount();
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kRibPrefixCount));
}

TEST(StatsTest, AdjRibOutGroupsCountTest) {
  BgpStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();

  // Verify initial value is 0
  EXPECT_EQ(0, tcData->getCounter(BgpStats::kAdjRibOutGroupsCount));

  // Increment adj rib out groups count
  BgpStats::incrAdjRibOutGroupsCount();
  BgpStats::incrAdjRibOutGroupsCount();
  tcData->publishStats();
  EXPECT_EQ(2, tcData->getCounter(BgpStats::kAdjRibOutGroupsCount));

  // Decrement adj rib out groups count
  BgpStats::decrAdjRibOutGroupsCount();
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(BgpStats::kAdjRibOutGroupsCount));

  // Reset to 0 in one step, as done during teardown
  BgpStats::setAdjRibOutGroupsCount(0);
  tcData->publishStats();
  EXPECT_EQ(0, tcData->getCounter(BgpStats::kAdjRibOutGroupsCount));
}

TEST(StatsTest, UpdateGroupEnabledCounterTest) {
  BgpStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();

  /*
   * The counter name is the ODS key contract: it is scraped by collection
   * model Arista_Ebb_Bgpcpp_Fb303_Dynamic via the ^bgpcpp\..* fb303 regex
   * and published as FBNet:bgpcpp.update_group_enabled. Renaming it breaks
   * every chart and detector built on that key.
   */
  EXPECT_EQ("bgpcpp.update_group_enabled", BgpStats::kUpdateGroupEnabled);

  // Disabled is the default, so the counter must exist and read 0
  EXPECT_EQ(0, tcData->getCounter(BgpStats::kUpdateGroupEnabled));

  BgpStats::setUpdateGroupEnabled(true);
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(BgpStats::kUpdateGroupEnabled));

  // A later config load that turns the feature off must clear the counter
  BgpStats::setUpdateGroupEnabled(false);
  tcData->publishStats();
  EXPECT_EQ(0, tcData->getCounter(BgpStats::kUpdateGroupEnabled));
}

TEST(StatsTest, EstablishedGrPeersCountTest) {
  BgpStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();

  // Verify initial value is 0
  EXPECT_EQ(0, tcData->getCounter(BgpStats::kEstablishedGrPeersCount));

  // Increment established GR peers count
  BgpStats::incrEstablishedGrPeersCount();
  BgpStats::incrEstablishedGrPeersCount();
  tcData->publishStats();
  EXPECT_EQ(2, tcData->getCounter(BgpStats::kEstablishedGrPeersCount));

  // Decrement established GR peers count
  BgpStats::decrEstablishedGrPeersCount();
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(BgpStats::kEstablishedGrPeersCount));
}

TEST(StatsTest, PeerAddrToIdsCountTest) {
  BgpStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();

  EXPECT_EQ(0, tcData->getCounter(BgpStats::kPeerAddrToIdsCount));

  BgpStats::incrPeerAddrToIdsCount();
  BgpStats::incrPeerAddrToIdsCount();
  tcData->publishStats();
  EXPECT_EQ(2, tcData->getCounter(BgpStats::kPeerAddrToIdsCount));
}

TEST(StatsTest, PendingRibDumpReqsCountTest) {
  BgpStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();

  EXPECT_EQ(0, tcData->getCounter(BgpStats::kPendingRibDumpReqsCount));

  BgpStats::incrPendingRibDumpReqsCount();
  BgpStats::incrPendingRibDumpReqsCount();
  BgpStats::incrPendingRibDumpReqsCount();
  tcData->publishStats();
  EXPECT_EQ(3, tcData->getCounter(BgpStats::kPendingRibDumpReqsCount));

  BgpStats::decrPendingRibDumpReqsCount(2);
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(BgpStats::kPendingRibDumpReqsCount));
}

TEST(StatsTest, AllPeersCountTest) {
  BgpStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();

  EXPECT_EQ(0, tcData->getCounter(BgpStats::kAllPeersCount));

  BgpStats::incrAllPeersCount();
  BgpStats::incrAllPeersCount();
  tcData->publishStats();
  EXPECT_EQ(2, tcData->getCounter(BgpStats::kAllPeersCount));

  BgpStats::decrAllPeersCount();
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(BgpStats::kAllPeersCount));
}

TEST(StatsTest, NexthopInfoCountTest) {
  RibStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();

  // Verify initial value is 0
  EXPECT_EQ(0, tcData->getCounter(RibStats::kNexthopInfoCount));

  // Increment nexthop info count
  RibStats::incrNexthopInfoCount();
  RibStats::incrNexthopInfoCount();
  tcData->publishStats();
  EXPECT_EQ(2, tcData->getCounter(RibStats::kNexthopInfoCount));

  // Decrement nexthop info count
  RibStats::decrNexthopInfoCount();
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kNexthopInfoCount));
}

TEST(StatsTest, NexthopStatusMapCountTest) {
  RibStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();

  // Verify initial value is 0
  EXPECT_EQ(0, tcData->getCounter(RibStats::kNexthopStatusMapCount));

  // Increment nexthop cache count
  RibStats::incrNexthopStatusMapCount();
  RibStats::incrNexthopStatusMapCount();
  RibStats::incrNexthopStatusMapCount();
  tcData->publishStats();
  EXPECT_EQ(3, tcData->getCounter(RibStats::kNexthopStatusMapCount));

  // Decrement nexthop cache count
  RibStats::decrNexthopStatusMapCount();
  tcData->publishStats();
  EXPECT_EQ(2, tcData->getCounter(RibStats::kNexthopStatusMapCount));
}

TEST(StatsTest, FibBatchListSizeTest) {
  RibStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();

  const std::string avgKey =
      std::string(RibStats::kFibBatchListSize) + ".avg.60";

  // Record batch sizes
  RibStats::addFibBatchListSize(100);
  RibStats::addFibBatchListSize(200);
  tcData->publishStats();
  EXPECT_TRUE(tcData->hasCounter(avgKey));
}

TEST(StatsTest, AdjRibInStaleCountTest) {
  RibStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();

  EXPECT_EQ(0, tcData->getCounter(RibStats::kAdjRibInStaleCount));

  RibStats::incrAdjRibInStaleCount();
  RibStats::incrAdjRibInStaleCount();
  tcData->publishStats();
  EXPECT_EQ(2, tcData->getCounter(RibStats::kAdjRibInStaleCount));

  RibStats::decrAdjRibInStaleCount(2);
  tcData->publishStats();
  EXPECT_EQ(0, tcData->getCounter(RibStats::kAdjRibInStaleCount));
}

TEST(StatsTest, DeferredUpdatesCountTest) {
  RibStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();

  EXPECT_EQ(0, tcData->getCounter(RibStats::kDeferredUpdatesCount));

  RibStats::incrDeferredUpdatesCount();
  RibStats::incrDeferredUpdatesCount();
  tcData->publishStats();
  EXPECT_EQ(2, tcData->getCounter(RibStats::kDeferredUpdatesCount));

  RibStats::decrDeferredUpdatesCount(2);
  tcData->publishStats();
  EXPECT_EQ(0, tcData->getCounter(RibStats::kDeferredUpdatesCount));
}

TEST(StatsTest, PostPolicyResultCacheCountTest) {
  RibStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();

  EXPECT_EQ(0, tcData->getCounter(RibStats::kPostPolicyResultCacheCount));

  RibStats::incrPostPolicyResultCacheCount();
  RibStats::incrPostPolicyResultCacheCount();
  tcData->publishStats();
  EXPECT_EQ(2, tcData->getCounter(RibStats::kPostPolicyResultCacheCount));

  RibStats::decrPostPolicyResultCacheCount();
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kPostPolicyResultCacheCount));
}

TEST(StatsTest, PeerStatsInitCounterTest) {
  auto messageSentOpenKey =
      createPeerMessageSentKey(PeerStats::kMessagesSentOpen);
  auto messageSentNotificationKey =
      createPeerMessageSentKey(PeerStats::kMessagesSentNotification);
  auto messageSentKeepAliveKey =
      createPeerMessageSentKey(PeerStats::kMessagesSentKeepAlive);
  auto messageSentUpdateKey =
      createPeerMessageSentKey(PeerStats::kMessagesSentUpdate);
  auto messageSentEndOfRibKey =
      createPeerMessageSentKey(PeerStats::kMessagesSentEndOfRib);
  auto messageSentRouteRefreshKey =
      createPeerMessageSentKey(PeerStats::kMessagesSentRouteRefresh);
  auto messagesSentSocketFailureKey =
      createPeerMessageSentKey(PeerStats::kMessagesSentSocketFailure);
  auto messagesSentAnnouncedIpv4Key =
      createPeerMessageSentKey(PeerStats::kMessageSentAnnouncedIpv4);
  auto messagesSentAnnouncedIpv6Key =
      createPeerMessageSentKey(PeerStats::kMessageSentAnnouncedIpv6);
  auto messagesSentWithdrawKey =
      createPeerMessageSentKey(PeerStats::kMessageSentWithdraw);

  auto messageRecvOpenKey =
      createPeerMessageRecvKey(PeerStats::kMessageRecvOpen);
  auto messageRecvUpdateKey =
      createPeerMessageRecvKey(PeerStats::kMessageRecvUpdate);
  auto messageRecvNotificationKey =
      createPeerMessageRecvKey(PeerStats::kMessageRecvNotification);
  auto messageRecvKeepAliveKey =
      createPeerMessageRecvKey(PeerStats::kMessageRecvKeepAlive);
  auto messageRecvRouteRefreshKey =
      createPeerMessageRecvKey(PeerStats::kMessageRecvRouteRefresh);
  auto messagesRecvAnnouncedIpv4Key =
      createPeerMessageRecvKey(PeerStats::kMessageRecvAnnouncedIpv4);
  auto messagesRecvAnnouncedIpv6Key =
      createPeerMessageRecvKey(PeerStats::kMessageRecvAnnouncedIpv6);
  auto messagesRecvWithdrawKey =
      createPeerMessageRecvKey(PeerStats::kMessageRecvWithdraw);

  auto attributeSizeAsPathKey =
      createAttributeSizeKey(PeerStats::kAttributeSizeAsPath);
  auto attributeSizeCommunityKey =
      createAttributeSizeKey(PeerStats::kAttributeSizeCommunity);
  auto attributeSizeExtendedCommunityKey =
      createAttributeSizeKey(PeerStats::kAttributeSizeExtendedCommunity);
  auto attributeSizeClusterListKey =
      createAttributeSizeKey(PeerStats::kAttributeSizeClusterList);
  auto attributeSizeTopologyInfoKey =
      createAttributeSizeKey(PeerStats::kAttributeSizeTopologyInfo);
  {
    // validate empty counters
    auto counters = fb303::ThreadCachedServiceData::getShared();
    EXPECT_FALSE(counters->hasCounter(PeerStats::kTotalRcvdPrefixes));
    EXPECT_FALSE(counters->hasCounter(PeerStats::kTotalAcceptedPrefixes));
    EXPECT_FALSE(counters->hasCounter(PeerStats::kNumRswSlbInRejections));
    EXPECT_FALSE(counters->hasCounter(PeerStats::kTotalSentPrefixes));
    EXPECT_FALSE(counters->hasCounter(PeerStats::kTotalPaths));
    EXPECT_FALSE(counters->hasCounter(PeerStats::kTotalUniquePrefixes));
    EXPECT_FALSE(counters->hasCounter(PeerStats::kTotalVipPrefixes));
    EXPECT_FALSE(counters->hasCounter(PeerStats::kMaxPeerRcvdPrefixes));
    EXPECT_FALSE(counters->hasCounter(PeerStats::kNoGrRestart));
    EXPECT_FALSE(
        counters->hasCounter(PeerStats::kTotalPeerWithNoRouteExchange));
    EXPECT_FALSE(counters->hasCounter(PeerStats::kTotalPrefixesDroppedByLimit));
    // Message sent keys
    EXPECT_FALSE(counters->hasCounter(messageSentOpenKey));
    EXPECT_FALSE(counters->hasCounter(messageSentNotificationKey));
    EXPECT_FALSE(counters->hasCounter(messageSentKeepAliveKey));
    EXPECT_FALSE(counters->hasCounter(messageSentUpdateKey));
    EXPECT_FALSE(counters->hasCounter(messageSentEndOfRibKey));
    EXPECT_FALSE(counters->hasCounter(messageSentRouteRefreshKey));
    EXPECT_FALSE(counters->hasCounter(messagesSentSocketFailureKey));
    EXPECT_FALSE(counters->hasCounter(messagesSentAnnouncedIpv4Key));
    EXPECT_FALSE(counters->hasCounter(messagesSentAnnouncedIpv6Key));
    EXPECT_FALSE(counters->hasCounter(messagesSentWithdrawKey));

    // Message received keys
    EXPECT_FALSE(counters->hasCounter(messageRecvOpenKey));
    EXPECT_FALSE(counters->hasCounter(messageRecvUpdateKey));
    EXPECT_FALSE(counters->hasCounter(messageRecvNotificationKey));
    EXPECT_FALSE(counters->hasCounter(messageRecvKeepAliveKey));
    EXPECT_FALSE(counters->hasCounter(messageRecvRouteRefreshKey));
    EXPECT_FALSE(counters->hasCounter(messagesRecvAnnouncedIpv4Key));
    EXPECT_FALSE(counters->hasCounter(messagesRecvAnnouncedIpv6Key));
    EXPECT_FALSE(counters->hasCounter(messagesRecvWithdrawKey));

    EXPECT_EQ(0, counters->getCounter(PeerStats::kUpdateBytesSent + ".avg.60"));
    EXPECT_EQ(0, counters->getCounter(PeerStats::kUpdateBytesRecv + ".avg.60"));
    EXPECT_EQ(
        0, counters->getCounter(PeerStats::kUpdateBytesSent + ".avg.86400"));
    EXPECT_EQ(
        0, counters->getCounter(PeerStats::kUpdateBytesRecv + ".avg.86400"));

    EXPECT_FALSE(counters->hasCounter(attributeSizeAsPathKey));
    EXPECT_FALSE(counters->hasCounter(attributeSizeCommunityKey));
    EXPECT_FALSE(counters->hasCounter(attributeSizeExtendedCommunityKey));
    EXPECT_FALSE(counters->hasCounter(attributeSizeClusterListKey));
    EXPECT_FALSE(counters->hasCounter(attributeSizeTopologyInfoKey));

    EXPECT_TRUE(
        counters->hasCounter(PeerStats::kRejectedInboundRoutes + ".count"));
    EXPECT_TRUE(
        counters->hasCounter(PeerStats::kRejectectedOutboundRoutes + ".count"));
  }
  {
    // initialize PeerStats counters
    PeerStats::initCounters();
  }
  {
    auto counters = fb303::ThreadCachedServiceData::getShared();
    EXPECT_EQ(-1, counters->getCounter(PeerStats::kTotalRcvdPrefixes));
    EXPECT_EQ(-1, counters->getCounter(PeerStats::kTotalAcceptedPrefixes));
    EXPECT_EQ(0, counters->getCounter(PeerStats::kNumRswSlbInRejections));
    /*
     * droppedPrefixes is initialized to 0 (always populated), not -1. See
     * S676351.
     */
    EXPECT_EQ(0, counters->getCounter(PeerStats::kTotalPrefixesDroppedByLimit));
    EXPECT_EQ(-1, counters->getCounter(PeerStats::kTotalSentPrefixes));
    EXPECT_EQ(-1, counters->getCounter(PeerStats::kTotalPaths));
    EXPECT_EQ(-1, counters->getCounter(PeerStats::kTotalUniquePrefixes));
    EXPECT_EQ(-1, counters->getCounter(PeerStats::kTotalVipPrefixes));
    EXPECT_EQ(-1, counters->getCounter(PeerStats::kMaxPeerRcvdPrefixes));
    EXPECT_EQ(0, counters->getCounter(PeerStats::kNoGrRestart + ".count"));
    EXPECT_EQ(
        0, counters->getCounter(PeerStats::kTotalPeerWithNoRouteExchange));
    // Message sent keys
    EXPECT_EQ(0, counters->getCounter(messageSentOpenKey));
    EXPECT_EQ(0, counters->getCounter(messageSentNotificationKey));
    EXPECT_EQ(0, counters->getCounter(messageSentKeepAliveKey));
    EXPECT_EQ(0, counters->getCounter(messageSentUpdateKey));
    EXPECT_EQ(0, counters->getCounter(messageSentEndOfRibKey));
    EXPECT_EQ(0, counters->getCounter(messageSentRouteRefreshKey));
    EXPECT_EQ(0, counters->getCounter(messagesSentSocketFailureKey));
    EXPECT_EQ(0, counters->getCounter(messagesSentAnnouncedIpv4Key));
    EXPECT_EQ(0, counters->getCounter(messagesSentAnnouncedIpv6Key));
    EXPECT_EQ(0, counters->getCounter(messagesSentWithdrawKey));

    // Message recieved keys
    EXPECT_EQ(0, counters->getCounter(messageRecvOpenKey));
    EXPECT_EQ(0, counters->getCounter(messageRecvUpdateKey));
    EXPECT_EQ(0, counters->getCounter(messageRecvNotificationKey));
    EXPECT_EQ(0, counters->getCounter(messageRecvKeepAliveKey));
    EXPECT_EQ(0, counters->getCounter(messageRecvRouteRefreshKey));
    EXPECT_EQ(0, counters->getCounter(messagesRecvAnnouncedIpv4Key));
    EXPECT_EQ(0, counters->getCounter(messagesRecvAnnouncedIpv6Key));
    EXPECT_EQ(0, counters->getCounter(messagesRecvWithdrawKey));

    EXPECT_EQ(0, counters->getCounter(attributeSizeAsPathKey));
    EXPECT_EQ(0, counters->getCounter(attributeSizeCommunityKey));
    EXPECT_EQ(0, counters->getCounter(attributeSizeExtendedCommunityKey));
    EXPECT_EQ(0, counters->getCounter(attributeSizeClusterListKey));
    EXPECT_EQ(0, counters->getCounter(attributeSizeTopologyInfoKey));

    EXPECT_EQ(
        0, counters->getCounter(PeerStats::kRejectedInboundRoutes + ".count"));
    EXPECT_EQ(
        0,
        counters->getCounter(PeerStats::kRejectectedOutboundRoutes + ".count"));
  }
}

TEST(StatsTest, NonGracefulPeersTest) {
  // Validate counters not set
  const auto& tcData = fb303::ThreadCachedServiceData::getShared();
  const auto& serviceData = tcData->getServiceData();
  EXPECT_TRUE(serviceData->getRegexCounters("bgpd.nonGraceful.*").empty());

  const std::string rsw = "RSW";
  const std::string ssw = "SSW";
  std::string rswCounter = fmt::format(BgpStats::kNonGraceful, rsw) + ".count";
  std::string sswCounter = fmt::format(BgpStats::kNonGraceful, ssw) + ".count";
  std::string rswCounter60 = rswCounter + ".60";
  std::string sswCounter60 = sswCounter + ".60";

  // init counters
  BgpStats::initNonGraceful(rsw);
  BgpStats::initNonGraceful(ssw);

  /*
   * Dynamic timeseries produces counterName.count
   * and counterName.count.60. 2 counters x 2 peer groups = 4
   */
  EXPECT_EQ(4, serviceData->getRegexCounters("bgpd.nonGraceful.*").size());
  EXPECT_EQ(0, serviceData->getCounter(rswCounter));
  EXPECT_EQ(0, serviceData->getCounter(sswCounter));
  EXPECT_EQ(0, serviceData->getCounter(rswCounter60));
  EXPECT_EQ(0, serviceData->getCounter(sswCounter60));

  // Inc counters
  BgpStats::incNonGrPeers(rsw);
  BgpStats::incNonGrPeers(ssw);
  BgpStats::incNonGrPeers(ssw);
  tcData->publishStats();

  EXPECT_EQ(1, serviceData->getCounter(rswCounter));
  EXPECT_EQ(1, serviceData->getCounter(rswCounter60));
  EXPECT_EQ(2, serviceData->getCounter(sswCounter));
  EXPECT_EQ(2, serviceData->getCounter(sswCounter60));

  // Dec counters
  BgpStats::decNonGrPeers(ssw);
  tcData->publishStats();
  EXPECT_EQ(1, serviceData->getCounter(rswCounter));
  EXPECT_EQ(1, serviceData->getCounter(rswCounter60));
  EXPECT_EQ(1, serviceData->getCounter(sswCounter));
  EXPECT_EQ(1, serviceData->getCounter(sswCounter60));
}

TEST(StatsTest, DeduplicatedAttributesCountersTest) {
  BgpStats::initCounters();
  auto tcData = fb303::ThreadCachedServiceData::get();

  // Verify initial values are 0
  EXPECT_EQ(0, tcData->getCounter(BgpStats::kDeduplicatedAttributesTotal));
  EXPECT_EQ(0, tcData->getCounter(BgpStats::kDeduplicatedAttributesAsPath));
  EXPECT_EQ(
      0, tcData->getCounter(BgpStats::kDeduplicatedAttributesCommunities));
  EXPECT_EQ(
      0, tcData->getCounter(BgpStats::kDeduplicatedAttributesClusterList));
  EXPECT_EQ(
      0, tcData->getCounter(BgpStats::kDeduplicatedAttributesExtCommunities));

  // Set counter values
  BgpStats::setDeduplicatedAttributesTotal(100);
  BgpStats::setDeduplicatedAttributesAsPath(20);
  BgpStats::setDeduplicatedAttributesCommunities(30);
  BgpStats::setDeduplicatedAttributesClusterList(10);
  BgpStats::setDeduplicatedAttributesExtCommunities(15);
  tcData->publishStats();

  EXPECT_EQ(100, tcData->getCounter(BgpStats::kDeduplicatedAttributesTotal));
  EXPECT_EQ(20, tcData->getCounter(BgpStats::kDeduplicatedAttributesAsPath));
  EXPECT_EQ(
      30, tcData->getCounter(BgpStats::kDeduplicatedAttributesCommunities));
  EXPECT_EQ(
      10, tcData->getCounter(BgpStats::kDeduplicatedAttributesClusterList));
  EXPECT_EQ(
      15, tcData->getCounter(BgpStats::kDeduplicatedAttributesExtCommunities));

  // Update counter values
  BgpStats::setDeduplicatedAttributesTotal(50);
  BgpStats::setDeduplicatedAttributesAsPath(10);
  tcData->publishStats();

  EXPECT_EQ(50, tcData->getCounter(BgpStats::kDeduplicatedAttributesTotal));
  EXPECT_EQ(10, tcData->getCounter(BgpStats::kDeduplicatedAttributesAsPath));
}

TEST(StatsTest, PolicyCacheNumAndMemUsageTest) {
  // cache hit/miss set
  const uint64_t numEntries{10};
  const uint64_t memoryUsed{6400};

  {
    // validate empty counters
    auto counters = fb303::ThreadCachedServiceData::getShared();
    EXPECT_FALSE(counters->hasCounter(BgpStats::kPolicyCacheNumEntries));
    EXPECT_FALSE(counters->hasCounter(BgpStats::kPolicyCacheMemoryUsage));
  }
  {
    // call method about number of cache hit/miss
    BgpStats::setPolicyCacheNumEntries(numEntries);
    BgpStats::setPolicyCacheMemoryUsage(memoryUsed);
  }
  {
    // validate counters after cache hit/miss set
    auto counters = fb303::ThreadCachedServiceData::getShared();
    EXPECT_TRUE(counters->hasCounter(BgpStats::kPolicyCacheNumEntries));
    EXPECT_TRUE(counters->hasCounter(BgpStats::kPolicyCacheMemoryUsage));
    EXPECT_EQ(
        numEntries, counters->getCounter(BgpStats::kPolicyCacheNumEntries));
    EXPECT_EQ(
        memoryUsed, counters->getCounter(BgpStats::kPolicyCacheMemoryUsage));
  }
}

TEST(StatsTest, PolicyCacheMissHitTest) {
  // cache hit/miss set
  std::map<std::string, int64_t> counters;
  const auto cacheHit{10};
  const auto cacheMiss{5};

  // validate empty counters
  fb303::ThreadCachedServiceData::getShared()->getCounters(counters);
  EXPECT_FALSE(counters.contains(BgpStats::kPolicyCacheHitCount));
  EXPECT_FALSE(counters.contains(BgpStats::kPolicyCacheMissCount));

  // call method about number of cache hit/miss
  BgpStats::setPolicyCacheHit(cacheHit);
  BgpStats::setPolicyCacheMiss(cacheMiss);

  // validate counters after cache hit/miss set
  fb303::ThreadCachedServiceData::getShared()->getCounters(counters);
  EXPECT_TRUE(counters.contains(BgpStats::kPolicyCacheHitCount));
  EXPECT_TRUE(counters.contains(BgpStats::kPolicyCacheMissCount));
  EXPECT_EQ(cacheHit, counters.at(BgpStats::kPolicyCacheHitCount));
  EXPECT_EQ(cacheMiss, counters.at(BgpStats::kPolicyCacheMissCount));
}

TEST(StatsTest, PeerPrefixCountTest) {
  // Set prefix count for pre-in, post-in, pre-out, post-out
  std::map<std::string, int64_t> counters;
  uint32_t preInPrefixCount{100};
  uint32_t postInPrefixCount{50};
  uint32_t preOutPrefixCount{50};
  uint32_t postOutPrefixCount{25};

  // Set counter names for pre-in, post-in, pre-out, post-out
  auto peerId = "peer_fa001-du001.abc1";
  auto peerPreInPrefixes = fmt::format(PeerStats::kPeerPreInPrefixes, peerId);
  auto peerPostInPrefixes = fmt::format(PeerStats::kPeerPostInPrefixes, peerId);
  auto peerPreOutPrefixes = fmt::format(PeerStats::kPeerPreOutPrefixes, peerId);
  auto peerPostOutPrefixes =
      fmt::format(PeerStats::kPeerPostOutPrefixes, peerId);

  // Validate empty counters
  fb303::ThreadCachedServiceData::getShared()->getCounters(counters);
  EXPECT_FALSE(counters.contains(peerPreInPrefixes));
  EXPECT_FALSE(counters.contains(peerPostInPrefixes));
  EXPECT_FALSE(counters.contains(peerPreOutPrefixes));
  EXPECT_FALSE(counters.contains(peerPostOutPrefixes));

  // Set counters for received, accepted, pre-out, sent
  PeerStats::setPeerPreInPrefixes(peerId, preInPrefixCount);
  PeerStats::setPeerPostInPrefixes(peerId, postInPrefixCount);
  PeerStats::setPeerPreOutPrefixes(peerId, preOutPrefixCount);
  PeerStats::setPeerPostOutPrefixes(peerId, postOutPrefixCount);

  // validate counters after prefix count set
  fb303::ThreadCachedServiceData::getShared()->getCounters(counters);
  EXPECT_TRUE(counters.contains(peerPreInPrefixes));
  EXPECT_TRUE(counters.contains(peerPostInPrefixes));
  EXPECT_TRUE(counters.contains(peerPreOutPrefixes));
  EXPECT_TRUE(counters.contains(peerPostOutPrefixes));
  EXPECT_EQ(preInPrefixCount, counters.at(peerPreInPrefixes));
  EXPECT_EQ(postInPrefixCount, counters.at(peerPostInPrefixes));
  EXPECT_EQ(preOutPrefixCount, counters.at(peerPreOutPrefixes));
  EXPECT_EQ(postOutPrefixCount, counters.at(peerPostOutPrefixes));
}

TEST(StatsTest, LogInitializationEvent) {
  // define events for checking
  const auto initializingEvent = BgpInitializationEvent::INITIALIZING;
  const auto initializedEvent = BgpInitializationEvent::INITIALIZED;

  // inject INITIALIZING event but not the other
  BgpStats::logInitializationEvent("Main", initializingEvent);

  // validate counters
  std::map<std::string, int64_t> counters;
  fb303::ThreadCachedServiceData::getShared()->getCounters(counters);
  EXPECT_TRUE(counters.count(
      fmt::format(
          kInitEventCounterFormat,
          apache::thrift::util::enumNameSafe(initializingEvent))));
  EXPECT_FALSE(counters.count(
      fmt::format(
          kInitEventCounterFormat,
          apache::thrift::util::enumNameSafe(initializedEvent))));

  // inject INITIALIZED event
  BgpStats::logInitializationEvent("Main", initializedEvent);
  fb303::ThreadCachedServiceData::getShared()->getCounters(counters);
  EXPECT_TRUE(counters.count(
      fmt::format(
          kInitEventCounterFormat,
          apache::thrift::util::enumNameSafe(initializingEvent))));
  EXPECT_TRUE(counters.count(
      fmt::format(
          kInitEventCounterFormat,
          apache::thrift::util::enumNameSafe(initializedEvent))));
}

/**
 * Verify the initialization and increment functionality of the streaming
 * session reject counter.
 */
TEST(StatsTest, StreamingSessionsRejectedCounterTest) {
  // Validate the counter doesn't exist before initialization.
  auto counters = fb303::ThreadCachedServiceData::getShared();
  EXPECT_FALSE(counters->hasCounter(BgpStats::kStreamingSessionsRejected));

  // Validate the counter exists after initialization.
  BgpStats::initCounters();
  EXPECT_TRUE(counters->hasCounter(BgpStats::kStreamingSessionsRejected));
  // Validate the counter starts at 0.
  EXPECT_EQ(0, counters->getCounter(BgpStats::kStreamingSessionsRejected));

  // Validate the counter increments correctly.
  BgpStats::incStreamingSessionsRejected();
  fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_EQ(1, counters->getCounter(BgpStats::kStreamingSessionsRejected));
}

TEST(StatsTest, DsfFastTearDownCounterTest) {
  // Validate the counter doesn't exist before initialization.
  auto counters = fb303::ThreadCachedServiceData::getShared();
  EXPECT_FALSE(counters->hasCounter(BgpStats::kDsfFastTearDownCount));

  // Validate the counter exists after initialization.
  BgpStats::initCounters();
  EXPECT_TRUE(counters->hasCounter(BgpStats::kDsfFastTearDownCount));
  // Validate the counter starts at 0.
  EXPECT_EQ(0, counters->getCounter(BgpStats::kDsfFastTearDownCount));

  // Validate the counter increments correctly.
  BgpStats::incDsfFastTearDownCount();
  fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_EQ(1, counters->getCounter(BgpStats::kDsfFastTearDownCount));
}

/**
 * Verify that message sent counters are being incremented properly
 */
TEST(StatsTest, MessageSentIncrTest) {
  {
    PeerStats::initCounters();
    PeerStats::incrOpenMessagesSent();
    PeerStats::incrNotificationMessagesSent();
    PeerStats::incrKeepAliveMessagesSent();
    PeerStats::incrUpdateMessagesSent();
    PeerStats::incrEndOfRibMessagesSent();
    PeerStats::incrRouteRefreshMessagesSent();
    PeerStats::incrMessagesSentSocketFailures();
    PeerStats::incrMessageSentAnnouncedIpv4();
    PeerStats::incrMessageSentAnnouncedIpv6();
    PeerStats::incrMessageSentWithdraw();
    PeerStats::addUpdateBytesSentToAvg(100);
  }
  {
    fb303::ThreadCachedServiceData::get()->publishStats();
    auto counters = fb303::ThreadCachedServiceData::getShared();
    // Message sent keys
    EXPECT_EQ(
        1,
        counters->getCounter(
            createPeerMessageSentKey(PeerStats::kMessagesSentOpen)));
    EXPECT_EQ(
        1,
        counters->getCounter(
            createPeerMessageSentKey(PeerStats::kMessagesSentNotification)));
    EXPECT_EQ(
        1,
        counters->getCounter(
            createPeerMessageSentKey(PeerStats::kMessagesSentKeepAlive)));
    EXPECT_EQ(
        1,
        counters->getCounter(
            createPeerMessageSentKey(PeerStats::kMessagesSentUpdate)));
    EXPECT_EQ(
        1,
        counters->getCounter(
            createPeerMessageSentKey(PeerStats::kMessagesSentEndOfRib)));
    EXPECT_EQ(
        1,
        counters->getCounter(
            createPeerMessageSentKey(PeerStats::kMessagesSentRouteRefresh)));
    EXPECT_EQ(
        1,
        counters->getCounter(
            createPeerMessageSentKey(PeerStats::kMessagesSentSocketFailure)));
    EXPECT_EQ(
        1,
        counters->getCounter(
            createPeerMessageSentKey(PeerStats::kMessageSentAnnouncedIpv4)));
    EXPECT_EQ(
        1,
        counters->getCounter(
            createPeerMessageSentKey(PeerStats::kMessageSentAnnouncedIpv6)));
    EXPECT_EQ(
        1,
        counters->getCounter(
            createPeerMessageSentKey(PeerStats::kMessageSentWithdraw)));
    EXPECT_EQ(
        100, counters->getCounter(PeerStats::kUpdateBytesSent + ".avg.60"));
    EXPECT_EQ(
        100, counters->getCounter(PeerStats::kUpdateBytesSent + ".avg.86400"));
  }
}

/**
 * Verify that message received counters are being incremented properly
 */
TEST(StatsTest, MessageRecvIncrTest) {
  {
    PeerStats::initCounters();

    PeerStats::incrMessageRecvOpen();
    PeerStats::incrMessageRecvUpdate();
    PeerStats::incrMessageRecvNotification();
    PeerStats::incrMessageRecvKeepAlive();
    PeerStats::incrMessageRecvRouteRefresh();
    PeerStats::incrMessageRecvAnnouncedIpv4();
    PeerStats::incrMessageRecvAnnouncedIpv6();
    PeerStats::incrMessageRecvWithdraw();
    PeerStats::addUpdateBytesRecvToAvg(100);
  }
  {
    fb303::ThreadCachedServiceData::get()->publishStats();
    auto counters = fb303::ThreadCachedServiceData::getShared();

    EXPECT_EQ(
        1,
        counters->getCounter(
            createPeerMessageRecvKey(PeerStats::kMessageRecvOpen)));
    EXPECT_EQ(
        1,
        counters->getCounter(
            createPeerMessageRecvKey(PeerStats::kMessageRecvUpdate)));
    EXPECT_EQ(
        1,
        counters->getCounter(
            createPeerMessageRecvKey(PeerStats::kMessageRecvNotification)));
    EXPECT_EQ(
        1,
        counters->getCounter(
            createPeerMessageRecvKey(PeerStats::kMessageRecvKeepAlive)));
    EXPECT_EQ(
        1,
        counters->getCounter(
            createPeerMessageRecvKey(PeerStats::kMessageRecvRouteRefresh)));
    EXPECT_EQ(
        1,
        counters->getCounter(
            createPeerMessageRecvKey(PeerStats::kMessageRecvAnnouncedIpv4)));
    EXPECT_EQ(
        1,
        counters->getCounter(
            createPeerMessageRecvKey(PeerStats::kMessageRecvAnnouncedIpv6)));
    EXPECT_EQ(
        1,
        counters->getCounter(
            createPeerMessageRecvKey(PeerStats::kMessageRecvWithdraw)));
    EXPECT_EQ(
        100, counters->getCounter(PeerStats::kUpdateBytesRecv + ".avg.60"));
    EXPECT_EQ(
        100, counters->getCounter(PeerStats::kUpdateBytesRecv + ".avg.86400"));
  }
}

TEST(StatsTest, EntriesSizeTest) {
  RibStats::initCounters();
  {
    RibStats::incrRibPaths();
    RibStats::incrRibPaths();
    RibStats::incrRibPaths();
    RibStats::decrRibPaths();

    RibStats::setAdjRibCount(100);
    RibStats::incrAdjRibCount();
    RibStats::incrAdjRibCount();
    RibStats::decrAdjRibCount();
    RibStats::setOriginatedRoutesSize(50);
    RibStats::publishShadowRibSize(10);
  }
  {
    fb303::ThreadCachedServiceData::get()->publishStats();
    auto counters = fb303::ThreadCachedServiceData::getShared();

    EXPECT_EQ(2, counters->getCounter(RibStats::kTotalRibPaths));
    EXPECT_EQ(101, counters->getCounter(RibStats::kTotalAdjRibs));
    EXPECT_EQ(50, counters->getCounter(RibStats::kTotalOriginatedRoutes));
    EXPECT_EQ(10, counters->getCounter(RibStats::kTotalShadowRibEntries));
  }
}

TEST(StatsTest, MarkPlannedExitTest) {
  auto exit_in_progress_file = "/dev/shm/bgp_exit_in_progress";
  using data = fb303::ThreadCachedServiceData;
  boost::filesystem::remove(exit_in_progress_file);
  data::get()->setCounter(BgpStats::kPlannedExit, 0);

  // No exit file yet, no counter bump
  BgpStats::handlePreviousExit();
  EXPECT_EQ(data::get()->getCounter(BgpStats::kPlannedExit), 0);

  /*
   * Mark planned exit creates exit-in-progress file,
   * handle previous exit should emit planned exit counter and remove
   * exit-in-progress file
   */
  BgpStats::markPlannedExit();
  EXPECT_TRUE(boost::filesystem::exists(exit_in_progress_file));
  BgpStats::handlePreviousExit();
  EXPECT_EQ(data::get()->getCounter(BgpStats::kPlannedExit), 1);
  EXPECT_FALSE(boost::filesystem::exists(exit_in_progress_file));
  // next run of bgpd will have fresh counter
  data::get()->setCounter(BgpStats::kPlannedExit, 0);

  /*
   * Handle previous exit now should emit 0 (indicating potential issue),
   * since in-progress file is gone from previous call. Mark planned exit
   * creates the in-progress file
   */
  BgpStats::handlePreviousExit();
  EXPECT_EQ(data::get()->getCounter(BgpStats::kPlannedExit), 0);
  BgpStats::markPlannedExit();
  EXPECT_TRUE(boost::filesystem::exists(exit_in_progress_file));

  boost::filesystem::remove(exit_in_progress_file);
}

TEST(StatsTest, AttributeSizeTest) {
  PeerStats::initCounters();
  {
    PeerStats::addAsPathSizeToAvg(50);
    PeerStats::addCommunitySizeToAvg(50);
    PeerStats::addExtendedCommunitySizeToAvg(50);
    PeerStats::addClusterListSizeToAvg(50);
    PeerStats::addTopologyInfoSizeToAvg(50);
  }
  {
    fb303::ThreadCachedServiceData::get()->publishStats();
    auto counters = fb303::ThreadCachedServiceData::getShared();
    EXPECT_EQ(
        50,
        counters->getCounter(
            createAttributeSizeKey(PeerStats::kAttributeSizeAsPath)));
    EXPECT_EQ(
        50,
        counters->getCounter(
            createAttributeSizeKey(PeerStats::kAttributeSizeCommunity)));
    EXPECT_EQ(
        50,
        counters->getCounter(createAttributeSizeKey(
            PeerStats::kAttributeSizeExtendedCommunity)));
    EXPECT_EQ(
        50,
        counters->getCounter(
            createAttributeSizeKey(PeerStats::kAttributeSizeClusterList)));
    EXPECT_EQ(
        50,
        counters->getCounter(
            createAttributeSizeKey(PeerStats::kAttributeSizeTopologyInfo)));
  }
}

TEST(StatsTest, PeerPolicyRejectIncrTest) {
  PeerStats::initCounters();
  {
    PeerStats::incrRejectedInboundRoutes();
    PeerStats::incrRejectedOutboundRoutes();
  }
  {
    fb303::ThreadCachedServiceData::get()->publishStats();
    auto counters = fb303::ThreadCachedServiceData::getShared();
    EXPECT_EQ(
        1, counters->getCounter(PeerStats::kRejectedInboundRoutes + ".count"));
    EXPECT_EQ(
        1,
        counters->getCounter(PeerStats::kRejectectedOutboundRoutes + ".count"));
  }
}

TEST(StatsTest, InvalidNexthopEncodingIncrTest) {
  PeerStats::initCounters();
  PeerStats::incrInvalidNexthopEncoding();

  fb303::ThreadCachedServiceData::get()->publishStats();
  auto counters = fb303::ThreadCachedServiceData::getShared();
  EXPECT_EQ(
      1, counters->getCounter(PeerStats::kInvalidNexthopEncoding + ".count"));
}

TEST(StatsTest, ConnectionCollisionIncrTest) {
  PeerStats::initCounters();
  {
    // Distinct counts confirm the two events map to separate global counters.
    PeerStats::incrConnectionCollisionClosed();
    PeerStats::incrConnectionCollisionClosed();
    PeerStats::incrConnectionCollisionClosedByPeer();
  }
  {
    fb303::ThreadCachedServiceData::get()->publishStats();
    auto counters = fb303::ThreadCachedServiceData::getShared();
    EXPECT_EQ(
        2,
        counters->getCounter(PeerStats::kConnectionCollisionClosed + ".count"));
    EXPECT_EQ(
        1,
        counters->getCounter(
            PeerStats::kConnectionCollisionClosedByPeer + ".count"));
  }
}

TEST(StatsTest, PeerIngressRouteFilterDeniedTest) {
  const std::string peerId = "10.0.0.1";
  PeerStats::initPeerCounters(peerId);

  auto key = fmt::format(
      PeerStats::kPeerIngressRouteFilterDenied,
      kEbbPlatform,
      kBgpcppTag,
      peerId);
  auto tcData = fb303::ThreadCachedServiceData::get();

  EXPECT_EQ(0, tcData->getCounter(key));

  PeerStats::addPeerIngressRouteFilterDenied(peerId);
  PeerStats::addPeerIngressRouteFilterDenied(peerId);
  PeerStats::addPeerIngressRouteFilterDenied(peerId);
  tcData->publishStats();

  EXPECT_EQ(3, tcData->getCounter(key));
}

TEST(StatsTest, IngressRouteFilterPeerGroupProcessTimeMsTest) {
  const std::string peerGroup = "PEERGROUP_RSW_FSW_V4";
  auto key = fmt::format(
      BgpStats::kIngressRouteFilterPeerGroupProcessTimeMs,
      kEbbPlatform,
      kBgpcppTag,
      peerGroup);

  BgpStats::addIngressRouteFilterPeerGroupProcessTimeMs(100, peerGroup);
  BgpStats::addIngressRouteFilterPeerGroupProcessTimeMs(200, peerGroup);
  facebook::fb303::ServiceData::get()->getQuantileStatMap()->flushAll();

  EXPECT_GT(
      fb303::ThreadCachedServiceData::get()->getCounter(key + ".avg.60"), 0);
  EXPECT_GE(
      fb303::ThreadCachedServiceData::get()->getCounter(key + ".p50.60"), 0);
  EXPECT_GE(
      fb303::ThreadCachedServiceData::get()->getCounter(key + ".p90.60"), 0);
  EXPECT_GE(
      fb303::ThreadCachedServiceData::get()->getCounter(key + ".p100.60"), 0);
}

TEST(StatsTest, RibTableVersionTest) {
  RibStats::initCounters();

  auto tcData = fb303::ThreadCachedServiceData::get();

  EXPECT_EQ(0, tcData->getCounter(RibStats::kRibTableVersion));

  RibStats::incrementRibTableVersion();
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kRibTableVersion));

  RibStats::incrementRibTableVersion();
  RibStats::incrementRibTableVersion();
  tcData->publishStats();
  EXPECT_EQ(3, tcData->getCounter(RibStats::kRibTableVersion));
}

TEST(StatsTest, PeerTableVersionTest) {
  const std::string peerId = "10.0.0.1";
  PeerStats::initPeerCounters(peerId);

  auto key = fmt::format(
      PeerStats::kPeerTableVersion, kEbbPlatform, kBgpcppTag, peerId);
  auto tcData = fb303::ThreadCachedServiceData::get();

  EXPECT_EQ(0, tcData->getCounter(key));

  PeerStats::setPeerTableVersion(peerId, 5);
  tcData->publishStats();
  EXPECT_EQ(5, tcData->getCounter(key));

  PeerStats::setPeerTableVersion(peerId, 42);
  tcData->publishStats();
  EXPECT_EQ(42, tcData->getCounter(key));
}

TEST(StatsTest, FibSyncTimeMsTest) {
  FibStats::addFibSyncTimeMs(100);
  FibStats::addFibSyncTimeMs(200);
  facebook::fb303::ServiceData::get()->getQuantileStatMap()->flushAll();

  auto key = FibStats::kFibSyncTimeMs;
  EXPECT_GT(
      fb303::ThreadCachedServiceData::get()->getCounter(key + ".avg.60"), 0);
  EXPECT_GE(
      fb303::ThreadCachedServiceData::get()->getCounter(key + ".p50.60"), 0);
  EXPECT_GE(
      fb303::ThreadCachedServiceData::get()->getCounter(key + ".p90.60"), 0);
  EXPECT_GE(
      fb303::ThreadCachedServiceData::get()->getCounter(key + ".p100.60"), 0);
}

TEST(StatsTest, SlowPeerDetectionCountTest) {
  BgpStats::initCounters();

  auto tcData = fb303::ThreadCachedServiceData::get();
  const auto sumKey = BgpStats::kSlowPeerDetectionCount + ".sum";
  const auto sum60Key = BgpStats::kSlowPeerDetectionCount + ".sum.60";
  const auto sum3600Key = BgpStats::kSlowPeerDetectionCount + ".sum.3600";

  EXPECT_EQ(0, tcData->getCounter(sumKey));

  BgpStats::incrSlowPeerDetectionCount();
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(sumKey));
  EXPECT_EQ(1, tcData->getCounter(sum60Key));
  EXPECT_EQ(1, tcData->getCounter(sum3600Key));

  BgpStats::incrSlowPeerDetectionCount();
  BgpStats::incrSlowPeerDetectionCount();
  tcData->publishStats();
  EXPECT_EQ(3, tcData->getCounter(sumKey));
  EXPECT_EQ(3, tcData->getCounter(sum60Key));
  EXPECT_EQ(3, tcData->getCounter(sum3600Key));
}

TEST(StatsTest, PeerMessagesSentCountersTest) {
  const std::string peerId = "10.0.0.1";
  PeerStats::initPeerCounters(peerId);

  auto tcData = fb303::ThreadCachedServiceData::get();

  auto updateKey = fmt::format(
      PeerStats::kPeerMessagesSentUpdate, kEbbPlatform, kBgpcppTag, peerId);
  auto ipv4Key = fmt::format(
      PeerStats::kPeerMessagesSentAnnouncedIpv4,
      kEbbPlatform,
      kBgpcppTag,
      peerId);
  auto ipv6Key = fmt::format(
      PeerStats::kPeerMessagesSentAnnouncedIpv6,
      kEbbPlatform,
      kBgpcppTag,
      peerId);
  auto withdrawKey = fmt::format(
      PeerStats::kPeerMessagesSentWithdraw, kEbbPlatform, kBgpcppTag, peerId);

  EXPECT_EQ(0, tcData->getCounter(updateKey));
  EXPECT_EQ(0, tcData->getCounter(ipv4Key));
  EXPECT_EQ(0, tcData->getCounter(ipv6Key));
  EXPECT_EQ(0, tcData->getCounter(withdrawKey));

  PeerStats::addPeerMessagesSentUpdate(peerId, 5);
  PeerStats::incrMessageSentAnnouncedIpv4(peerId);
  PeerStats::incrMessageSentAnnouncedIpv4(peerId);
  PeerStats::incrMessageSentAnnouncedIpv6(peerId);
  PeerStats::incrMessageSentWithdraw(peerId);
  PeerStats::incrMessageSentWithdraw(peerId);
  PeerStats::incrMessageSentWithdraw(peerId);
  tcData->publishStats();

  EXPECT_EQ(5, tcData->getCounter(updateKey));
  EXPECT_EQ(2, tcData->getCounter(ipv4Key));
  EXPECT_EQ(1, tcData->getCounter(ipv6Key));
  EXPECT_EQ(3, tcData->getCounter(withdrawKey));
}

TEST(StatsTest, PeerMessagesSentAllTypesCountersTest) {
  const std::string peerId = "10.0.0.3";
  PeerStats::initPeerCounters(peerId);

  auto tcData = fb303::ThreadCachedServiceData::get();

  auto openKey = fmt::format(
      PeerStats::kPeerMessagesSentOpen, kEbbPlatform, kBgpcppTag, peerId);
  auto notifKey = fmt::format(
      PeerStats::kPeerMessagesSentNotification,
      kEbbPlatform,
      kBgpcppTag,
      peerId);
  auto keepAliveKey = fmt::format(
      PeerStats::kPeerMessagesSentKeepAlive, kEbbPlatform, kBgpcppTag, peerId);
  auto eorKey = fmt::format(
      PeerStats::kPeerMessagesSentEndOfRib, kEbbPlatform, kBgpcppTag, peerId);
  auto rrKey = fmt::format(
      PeerStats::kPeerMessagesSentRouteRefresh,
      kEbbPlatform,
      kBgpcppTag,
      peerId);
  auto sfKey = fmt::format(
      PeerStats::kPeerMessagesSentSocketFailure,
      kEbbPlatform,
      kBgpcppTag,
      peerId);
  auto sbKey = fmt::format(
      PeerStats::kPeerSocketEgressBuffered, kEbbPlatform, kBgpcppTag, peerId);

  EXPECT_EQ(0, tcData->getCounter(openKey));
  EXPECT_EQ(0, tcData->getCounter(notifKey));
  EXPECT_EQ(0, tcData->getCounter(keepAliveKey));
  EXPECT_EQ(0, tcData->getCounter(eorKey));
  EXPECT_EQ(0, tcData->getCounter(rrKey));
  EXPECT_EQ(0, tcData->getCounter(sfKey));
  EXPECT_EQ(0, tcData->getCounter(sbKey));

  PeerStats::incrOpenMessagesSent(peerId);
  PeerStats::incrNotificationMessagesSent(peerId);
  PeerStats::incrNotificationMessagesSent(peerId);
  PeerStats::incrKeepAliveMessagesSent(peerId);
  PeerStats::incrKeepAliveMessagesSent(peerId);
  PeerStats::incrKeepAliveMessagesSent(peerId);
  PeerStats::incrEndOfRibMessagesSent(peerId);
  PeerStats::incrRouteRefreshMessagesSent(peerId);
  PeerStats::incrMessagesSentSocketFailures(peerId);
  PeerStats::incrSocketEgressBuffered(peerId);
  PeerStats::incrSocketEgressBuffered(peerId);
  tcData->publishStats();

  EXPECT_EQ(1, tcData->getCounter(openKey));
  EXPECT_EQ(2, tcData->getCounter(notifKey));
  EXPECT_EQ(3, tcData->getCounter(keepAliveKey));
  EXPECT_EQ(1, tcData->getCounter(eorKey));
  EXPECT_EQ(1, tcData->getCounter(rrKey));
  EXPECT_EQ(1, tcData->getCounter(sfKey));
  EXPECT_EQ(2, tcData->getCounter(sbKey));
}

TEST(StatsTest, PeerMessagesRecvCountersTest) {
  const std::string peerId = "10.0.0.2";
  PeerStats::initPeerCounters(peerId);

  auto tcData = fb303::ThreadCachedServiceData::get();

  auto updateKey = fmt::format(
      PeerStats::kPeerMessagesRecvUpdate, kEbbPlatform, kBgpcppTag, peerId);
  auto ipv4Key = fmt::format(
      PeerStats::kPeerMessagesRecvAnnouncedIpv4,
      kEbbPlatform,
      kBgpcppTag,
      peerId);
  auto ipv6Key = fmt::format(
      PeerStats::kPeerMessagesRecvAnnouncedIpv6,
      kEbbPlatform,
      kBgpcppTag,
      peerId);
  auto withdrawKey = fmt::format(
      PeerStats::kPeerMessagesRecvWithdraw, kEbbPlatform, kBgpcppTag, peerId);

  EXPECT_EQ(0, tcData->getCounter(updateKey));
  EXPECT_EQ(0, tcData->getCounter(ipv4Key));
  EXPECT_EQ(0, tcData->getCounter(ipv6Key));
  EXPECT_EQ(0, tcData->getCounter(withdrawKey));

  PeerStats::addPeerMessagesRecvUpdate(peerId);
  PeerStats::addPeerMessagesRecvUpdate(peerId);
  PeerStats::incrMessageRecvAnnouncedIpv4(peerId);
  PeerStats::incrMessageRecvAnnouncedIpv6(peerId);
  PeerStats::incrMessageRecvAnnouncedIpv6(peerId);
  PeerStats::incrMessageRecvWithdraw(peerId);
  tcData->publishStats();

  EXPECT_EQ(2, tcData->getCounter(updateKey));
  EXPECT_EQ(1, tcData->getCounter(ipv4Key));
  EXPECT_EQ(2, tcData->getCounter(ipv6Key));
  EXPECT_EQ(1, tcData->getCounter(withdrawKey));
}

TEST(StatsTest, PeerMessagesRecvAllTypesCountersTest) {
  const std::string peerId = "10.0.0.4";
  PeerStats::initPeerCounters(peerId);

  auto tcData = fb303::ThreadCachedServiceData::get();

  auto openKey = fmt::format(
      PeerStats::kPeerMessagesRecvOpen, kEbbPlatform, kBgpcppTag, peerId);
  auto notifKey = fmt::format(
      PeerStats::kPeerMessagesRecvNotification,
      kEbbPlatform,
      kBgpcppTag,
      peerId);
  auto keepAliveKey = fmt::format(
      PeerStats::kPeerMessagesRecvKeepAlive, kEbbPlatform, kBgpcppTag, peerId);
  auto rrKey = fmt::format(
      PeerStats::kPeerMessagesRecvRouteRefresh,
      kEbbPlatform,
      kBgpcppTag,
      peerId);

  EXPECT_EQ(0, tcData->getCounter(openKey));
  EXPECT_EQ(0, tcData->getCounter(notifKey));
  EXPECT_EQ(0, tcData->getCounter(keepAliveKey));
  EXPECT_EQ(0, tcData->getCounter(rrKey));

  PeerStats::addPeerMessagesRecvOpen(peerId);
  PeerStats::addPeerMessagesRecvNotification(peerId);
  PeerStats::addPeerMessagesRecvNotification(peerId);
  PeerStats::addPeerMessagesRecvKeepAlive(peerId);
  PeerStats::addPeerMessagesRecvKeepAlive(peerId);
  PeerStats::addPeerMessagesRecvKeepAlive(peerId);
  PeerStats::addPeerMessagesRecvRouteRefresh(peerId);
  tcData->publishStats();

  EXPECT_EQ(1, tcData->getCounter(openKey));
  EXPECT_EQ(2, tcData->getCounter(notifKey));
  EXPECT_EQ(3, tcData->getCounter(keepAliveKey));
  EXPECT_EQ(1, tcData->getCounter(rrKey));
}

TEST(StatsTest, RibUnresolvableNexthopsCountTest) {
  RibStats::initCounters();

  auto tcData = fb303::ThreadCachedServiceData::get();

  EXPECT_EQ(0, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));

  RibStats::incrUnresolvableNexthopsCount();
  RibStats::incrUnresolvableNexthopsCount();
  RibStats::incrUnresolvableNexthopsCount();
  tcData->publishStats();
  EXPECT_EQ(3, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));

  RibStats::decrUnresolvableNexthopsCount();
  RibStats::decrUnresolvableNexthopsCount();
  RibStats::decrUnresolvableNexthopsCount();
  tcData->publishStats();
  EXPECT_EQ(0, tcData->getCounter(RibStats::kRibUnresolvableNexthopsCount));
}

TEST(StatsTest, DecisionProcessRunsCountTest) {
  BgpStats::initCounters();

  auto tcData = fb303::ThreadCachedServiceData::get();

  EXPECT_EQ(0, tcData->getCounter(BgpStats::kDecisionProcessRunsCount));

  BgpStats::incrDecisionProcessRunsCount();
  BgpStats::incrDecisionProcessRunsCount();
  BgpStats::incrDecisionProcessRunsCount();
  tcData->publishStats();
  EXPECT_EQ(3, tcData->getCounter(BgpStats::kDecisionProcessRunsCount));
}

TEST(StatsTest, ClearPeerCountersTest) {
  const std::string peerIdOdsStr = "peer_10.99.0.1_192.0.2.1";
  const std::string noGrRestartPeerId =
      "peerAddr 10.99.0.1, remoteBgpId 192.0.2.1";
  auto& tcData = *CHECK_NOTNULL(fb303::ThreadCachedServiceData::get());

  // Initialize counters via initPeerCounters.
  PeerStats::initPeerCounters(peerIdOdsStr);

  /*
   * Counters set elsewhere in production (not via initPeerCounters) are
   * also expected to be cleared. Set them here to simulate that.
   */
  auto postInKey = fmt::format(PeerStats::kPeerPostInPrefixes, peerIdOdsStr);
  auto statusKey = fmt::format(PeerStats::kPeerStatus, peerIdOdsStr);
  tcData.setCounter(postInKey, 7);
  tcData.setCounter(statusKey, 1);

  // Sample of counters set by initPeerCounters.
  auto preInKey = fmt::format(PeerStats::kPeerPreInPrefixes, peerIdOdsStr);
  auto postOutKey = fmt::format(PeerStats::kPeerPostOutPrefixes, peerIdOdsStr);
  auto preOutKey = fmt::format(PeerStats::kPeerPreOutPrefixes, peerIdOdsStr);
  auto updateKey = fmt::format(
      PeerStats::kPeerMessagesSentUpdate,
      kEbbPlatform,
      kBgpcppTag,
      peerIdOdsStr);
  auto recvUpdateKey = fmt::format(
      PeerStats::kPeerMessagesRecvUpdate,
      kEbbPlatform,
      kBgpcppTag,
      peerIdOdsStr);

  EXPECT_TRUE(tcData.hasCounter(preInKey));
  EXPECT_TRUE(tcData.hasCounter(postInKey));
  EXPECT_TRUE(tcData.hasCounter(postOutKey));
  EXPECT_TRUE(tcData.hasCounter(preOutKey));
  EXPECT_TRUE(tcData.hasCounter(statusKey));
  EXPECT_TRUE(tcData.hasCounter(updateKey));
  EXPECT_TRUE(tcData.hasCounter(recvUpdateKey));

  // Set some non-zero values to verify clear is unconditional.
  tcData.setCounter(preInKey, 42);
  tcData.setCounter(updateKey, 10);

  PeerStats::clearPeerCounters(peerIdOdsStr, noGrRestartPeerId);

  EXPECT_FALSE(tcData.hasCounter(preInKey));
  EXPECT_FALSE(tcData.hasCounter(postInKey));
  EXPECT_FALSE(tcData.hasCounter(postOutKey));
  EXPECT_FALSE(tcData.hasCounter(preOutKey));
  EXPECT_FALSE(tcData.hasCounter(statusKey));
  EXPECT_FALSE(tcData.hasCounter(updateKey));
  EXPECT_FALSE(tcData.hasCounter(recvUpdateKey));
}

TEST(StatsTest, ClearPeerCountersDestroysPerPeerTimeseries) {
  const std::string peerIdOdsStr = "peer_10.99.0.1_192.0.2.1";
  const std::string noGrRestartPeerId =
      "peerAddr 10.99.0.1, remoteBgpId 192.0.2.1";
  const auto sessionStateKey =
      fmt::format(PeerStats::kPeerSessionStateChanges, peerIdOdsStr);
  const auto noGrRestartKey =
      fmt::format(PeerStats::kNoGrRestartPeer, noGrRestartPeerId);
  auto& tcData = *CHECK_NOTNULL(fb303::ThreadCachedServiceData::get());

  PeerStats::addPeerSessionStateChanges(peerIdOdsStr);
  PeerStats::incrPeerNoGrRestart(noGrRestartPeerId);

  auto sessionStateTimeseries =
      tcData.getThreadStats()->getTimeseriesSafe(sessionStateKey);
  std::weak_ptr sessionStateWeak = sessionStateTimeseries;
  sessionStateTimeseries.reset();

  EXPECT_TRUE(tcData.getStatMap()->contains(sessionStateKey));
  EXPECT_TRUE(tcData.getStatMap()->contains(noGrRestartKey));
  EXPECT_EQ(1, tcData.getCounter(noGrRestartKey + ".count"));

  PeerStats::clearPeerCounters(peerIdOdsStr, noGrRestartPeerId);

  EXPECT_FALSE(tcData.getStatMap()->contains(sessionStateKey));
  EXPECT_FALSE(tcData.getStatMap()->contains(noGrRestartKey));
  EXPECT_TRUE(sessionStateWeak.expired());
}

TEST(StatsTest, NhtCacheInitCounterTest) {
  RibStats::initCounters();
  auto counters = fb303::ThreadCachedServiceData::getShared();

  EXPECT_FALSE(counters->hasCounter("nhtCacheNexthopReachable.count"));
  EXPECT_FALSE(counters->hasCounter("nhtCacheNexthopUnreachable.count"));

  EXPECT_EQ(
      0, counters->getCounter(RibStats::kNhtCacheNexthopReachable + ".count"));
  EXPECT_EQ(
      0,
      counters->getCounter(RibStats::kNhtCacheNexthopReachable + ".count.60"));
  EXPECT_EQ(
      0,
      counters->getCounter(RibStats::kNhtCacheNexthopUnreachable + ".count"));
  EXPECT_EQ(
      0,
      counters->getCounter(
          RibStats::kNhtCacheNexthopUnreachable + ".count.60"));
}

TEST(StatsTest, NhtCacheCounterIncrementTest) {
  RibStats::initCounters();
  auto tcData = fb303::ThreadCachedServiceData::get();

  RibStats::incrNhtCacheNexthopReachable();
  RibStats::incrNhtCacheNexthopReachable();
  RibStats::incrNhtCacheNexthopUnreachable();
  tcData->publishStats();

  EXPECT_EQ(
      2, tcData->getCounter(RibStats::kNhtCacheNexthopReachable + ".count"));
  EXPECT_EQ(
      1, tcData->getCounter(RibStats::kNhtCacheNexthopUnreachable + ".count"));
}

} // namespace facebook::bgp
