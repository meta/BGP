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

#include <gtest/gtest.h>

#include <fb303/ThreadCachedServiceData.h>

#include "neteng/fboss/bgp/cpp/stats/StatsBB.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

namespace facebook::bgp {

TEST(StatsBBTest, InitStatsBB_PreservesOdsKeysAndSupportsPlatformMutators) {
  EXPECT_EQ("bgpd.addPeers.success", std::string(BgpStatsBB::kAddPeersSuccess));
  EXPECT_EQ(
      "bgpd.ribPolicy.numUnsupportedPolicyMsg",
      std::string(RibStatsBB::kUnsupportedPolicyMsg));

  initStatsBB();
  auto* stats = fb303::ThreadCachedServiceData::get();

  EXPECT_EQ(0, stats->getCounter(BgpStatsBB::kAddPeersSuccess));
  EXPECT_EQ(
      0,
      stats->getCounter(
          std::string(RibStatsBB::kUnsupportedPolicyMsg) + ".count"));

  BgpStatsBB::incrAddPeersSuccess();
  RibStatsBB::STATS_unsupportedPolicyMsg.add(1);
  stats->publishStats();

  EXPECT_EQ(1, stats->getCounter(BgpStatsBB::kAddPeersSuccess));
  EXPECT_EQ(
      1,
      stats->getCounter(
          std::string(RibStatsBB::kUnsupportedPolicyMsg) + ".count"));
}

TEST(StatsBBTest, AddPeersCounterTest) {
  auto counters = fb303::ThreadCachedServiceData::getShared();

  BgpStatsBB::initCounters();
  EXPECT_TRUE(counters->hasCounter(BgpStatsBB::kAddPeersSuccess));
  EXPECT_TRUE(counters->hasCounter(BgpStatsBB::kAddPeersRejected));
  EXPECT_EQ(0, counters->getCounter(BgpStatsBB::kAddPeersSuccess));
  EXPECT_EQ(0, counters->getCounter(BgpStatsBB::kAddPeersRejected));

  BgpStatsBB::incrAddPeersSuccess();
  BgpStatsBB::incrAddPeersSuccess();
  BgpStatsBB::incrAddPeersRejected();
  fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_EQ(2, counters->getCounter(BgpStatsBB::kAddPeersSuccess));
  EXPECT_EQ(1, counters->getCounter(BgpStatsBB::kAddPeersRejected));
}

TEST(StatsBBTest, DelPeersCounterTest) {
  auto counters = fb303::ThreadCachedServiceData::getShared();

  BgpStatsBB::initCounters();
  EXPECT_TRUE(counters->hasCounter(BgpStatsBB::kDelPeersSuccess));
  EXPECT_TRUE(counters->hasCounter(BgpStatsBB::kDelPeersRejected));
  EXPECT_EQ(0, counters->getCounter(BgpStatsBB::kDelPeersSuccess));
  EXPECT_EQ(0, counters->getCounter(BgpStatsBB::kDelPeersRejected));

  BgpStatsBB::incrDelPeersSuccess();
  BgpStatsBB::incrDelPeersSuccess();
  BgpStatsBB::incrDelPeersRejected();
  fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_EQ(2, counters->getCounter(BgpStatsBB::kDelPeersSuccess));
  EXPECT_EQ(1, counters->getCounter(BgpStatsBB::kDelPeersRejected));
}

TEST(StatsBBTest, BbInitializationPreservesSharedPolicyApiCounters) {
  EXPECT_EQ(
      "bgpd.setPeersPolicy.failure",
      std::string(BgpStats::kSetPeersPolicyFailure));
  BgpStats::initPolicyApiCounters();

  auto tcData = fb303::ThreadCachedServiceData::get();

  EXPECT_EQ(0, tcData->getCounter(BgpStats::kSetPeersPolicySuccess));
  EXPECT_EQ(0, tcData->getCounter(BgpStats::kSetPeersPolicyFailure));
  EXPECT_EQ(0, tcData->getCounter(BgpStats::kSetPeerGroupsPolicySuccess));
  EXPECT_EQ(0, tcData->getCounter(BgpStats::kSetPeerGroupsPolicyFailure));
  EXPECT_EQ(0, tcData->getCounter(BgpStats::kUnsetPeersPolicySuccess));
  EXPECT_EQ(0, tcData->getCounter(BgpStats::kUnsetPeersPolicyFailure));

  BgpStats::incrSetPeersPolicySuccess();
  BgpStats::incrSetPeersPolicyFailure();
  BgpStats::incrSetPeersPolicyFailure();
  BgpStats::incrSetPeerGroupsPolicySuccess();
  BgpStats::incrSetPeerGroupsPolicyFailure();
  BgpStats::incrUnsetPeersPolicySuccess();
  BgpStats::incrUnsetPeersPolicySuccess();
  BgpStats::incrUnsetPeersPolicyFailure();
  tcData->publishStats();
  initStatsBB();

  EXPECT_EQ(1, tcData->getCounter(BgpStats::kSetPeersPolicySuccess));
  EXPECT_EQ(2, tcData->getCounter(BgpStats::kSetPeersPolicyFailure));
  EXPECT_EQ(1, tcData->getCounter(BgpStats::kSetPeerGroupsPolicySuccess));
  EXPECT_EQ(1, tcData->getCounter(BgpStats::kSetPeerGroupsPolicyFailure));
  EXPECT_EQ(2, tcData->getCounter(BgpStats::kUnsetPeersPolicySuccess));
  EXPECT_EQ(1, tcData->getCounter(BgpStats::kUnsetPeersPolicyFailure));
}

} // namespace facebook::bgp
