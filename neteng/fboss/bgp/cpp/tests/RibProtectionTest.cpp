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

#define AdjRib_TEST_FRIENDS                                                    \
  friend class AdjRibInboundFixture;                                           \
  FRIEND_TEST(AdjRibInboundFixture, AdjRibStatsBasicTest);                     \
  FRIEND_TEST(                                                                 \
      AdjRibInboundFixture, SafeModeSubnetLimitAppliesToNewPrefixUpdates);     \
  FRIEND_TEST(AdjRibInboundFixture, PrefixLimitSetSingletonTest);              \
  FRIEND_TEST(AdjRibInboundFixture, SwitchLimitWithTotalPathLimitTest);        \
  FRIEND_TEST(AdjRibInboundFixture, SwitchLimitWithPrefixLimitTest);           \
  FRIEND_TEST(                                                                 \
      AdjRibInboundFixture,                                                    \
      DropPrefixForOverloadProtectionApplyGoldenPrefixPolicy);                 \
  FRIEND_TEST(                                                                 \
      AdjRibInboundFixture,                                                    \
      DropPrefixForOverloadProtectionStickySafeModeTest);                      \
  FRIEND_TEST(                                                                 \
      AdjRibInboundFixture,                                                    \
      ProcessAdjRibReEvaluationForSafeModeWithRecAddPathTest);                 \
  FRIEND_TEST(AdjRibInboundFixture, ProcessAdjRibReEvaluationForSafeModeTest); \
  FRIEND_TEST(                                                                 \
      AdjRibInboundFixture,                                                    \
      ProcessAdjRibReEvaluationForSafeModeGoldenVIPsTest);                     \
  FRIEND_TEST(                                                                 \
      AdjRibInboundFixture, ProcessAdjRibReEvaluationForSafeModeGrHelperTest); \
  FRIEND_TEST(FallbackToSwitchPrefixLimit, Test);                              \
  FRIEND_TEST(AdjRibInboundFixture, AllowGoldenVipTest);                       \
  FRIEND_TEST(AdjRibInboundFixture, IsGoldenVipTest);                          \
  FRIEND_TEST(                                                                 \
      AdjRibInboundFixture,                                                    \
      DropPrefixForOverloadProtectionApplyGoldenVipLimitTest);

#define AdjRibStats_TEST_FRIENDS                           \
  friend class AdjRibInboundFixture;                       \
  FRIEND_TEST(AdjRibInboundFixture, AdjRibStatsBasicTest); \
  FRIEND_TEST(                                             \
      AdjRibInboundFixture,                                \
      CheckLimitAndAlarmWarningOnlyWithWarningLimitTest);

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/IPAddress.h>
#include <folly/coro/BlockingWait.h>
#include <folly/io/async/EventBase.h>

#include "common/network/AddressUtil.h"
#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyManager.h"
#include "neteng/fboss/bgp/cpp/rib/RibPolicy.h"
#include "neteng/fboss/bgp/cpp/tests/AdjRibInUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibPolicyUtils.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

using namespace facebook::bgp;
using namespace facebook::nettools::bgplib;
using namespace facebook::neteng::fboss::bgp::thrift;
using namespace facebook::neteng::fboss::bgp_attr;
using namespace folly::fibers;
using namespace testing;

using folly::IPAddress;
using folly::IPAddressV4;

namespace facebook::bgp {
/*
 * This test verified the configured switchLimitConfig with total path limit.
 *
 * When the total total path limit is reached, we expect BGP to ignore the
 * processing of incoming BgpUpdate.
 */
TEST_F(AdjRibInboundFixture, SwitchLimitWithTotalPathLimitTest) {
  const uint64_t totalPathLimit = 200;
  const auto network = folly::CIDRNetwork("10.0.0.1", 32);
  BgpPath attrs; // empty
  auto switchLimitConfig = std::make_shared<thrift::BgpSwitchLimitConfig>();

  // Instantiate adjRib object
  setupAdjRib(
      kShortGrRestartTime, /* localGrRestartTime */
      std::nullopt, /* remoteGrRestartTime */
      false /* no establish session call */);

  {
    /*
     * Test 1.1: received a path count without switch-limit config specified.
     * Expect to return false to indicate switch-limit is not reached.
     */
    EXPECT_FALSE(adjRib_->dropPrefixForOverloadProtection(
        2 * totalPathLimit, network, attrs, nullptr));

    /*
     * Test 1.2: received a path count with switch-limit config specified, but
     * neither toal_path_limit nor prefix_limit is specified. Expect to
     * return false to indicate switch-limit is not reached.
     */
    EXPECT_FALSE(adjRib_->dropPrefixForOverloadProtection(
        2 * totalPathLimit, network, attrs, switchLimitConfig));
  }
  {
    /*
     * Test 2.1: receivd path count < total path limit.
     * Expect to return false to indicate switch-limit is not reached.
     */
    switchLimitConfig->total_path_limit() = totalPathLimit;
    EXPECT_FALSE(adjRib_->dropPrefixForOverloadProtection(
        totalPathLimit - 1, network, attrs, switchLimitConfig));

    /*
     * Test 2.2: receivd path count == total path limit
     * Expect to return false to indicate switch-limit is not reached.
     */
    EXPECT_FALSE(adjRib_->dropPrefixForOverloadProtection(
        totalPathLimit, network, attrs, switchLimitConfig));

    /*
     * Test 2.3: receivd path count > total path limit
     * Expect to return true to indicate switch-limit is reached.
     */
    EXPECT_TRUE(adjRib_->dropPrefixForOverloadProtection(
        totalPathLimit + 1, network, attrs, switchLimitConfig));
  }
}

/*
 * This test verified the configured switchLimitConfig with prefix limit.
 *
 * When the prefix limit is reached, we expect BGP to ignore the processing of
 * incoming BgpUpdate.
 */
TEST_F(AdjRibInboundFixture, SwitchLimitWithPrefixLimitTest) {
  const int64_t prefixLimit = 1;
  auto switchLimitConfig = std::make_shared<thrift::BgpSwitchLimitConfig>();
  auto prefixSet = AdjRibPrefixSet::get();
  const auto network1 = folly::CIDRNetwork("10.0.0.1", 32);
  const auto network2 = folly::CIDRNetwork("10.0.0.1", 24);
  BgpPath attrs; // empty

  // Instantiate adjRib object
  setupAdjRib(
      kShortGrRestartTime, /* localGrRestartTime */
      std::nullopt, /* remoteGrRestartTime */
      false /* no establish session call */);

  {
    /*
     * Test 1.1: received a prefixlimit
     * Expect to return false since no swithcLimitConfig specified.
     */
    EXPECT_FALSE(
        adjRib_->dropPrefixForOverloadProtection(0, network1, attrs, nullptr));

    /*
     * Test 1.2: received a nullptr of prefixLimit with switch-limit config
     * specified. Expected to return false as a negative test case.
     */
    EXPECT_FALSE(adjRib_->dropPrefixForOverloadProtection(
        0, network2, attrs, switchLimitConfig));
  }
  {
    /*
     * Test 2.1: prefixSet size < prefix limit.
     * Expect to return false to not block BgpUpdate processing.
     */
    switchLimitConfig->prefix_limit() = prefixLimit;
    EXPECT_FALSE(adjRib_->dropPrefixForOverloadProtection(
        0, network1, attrs, switchLimitConfig));

    /*
     * Test 2.2: prefixSet size == prefix limit.
     * - if receiving an existing prefix, expect to return false to not block.
     * - if receiving a new prefix, expect to return true to block.
     */
    prefixSet->addPrefix(network1, false);
    EXPECT_FALSE(adjRib_->dropPrefixForOverloadProtection(
        0, network1, attrs, switchLimitConfig));
    EXPECT_TRUE(adjRib_->dropPrefixForOverloadProtection(
        0, network2, attrs, switchLimitConfig));

    /*
     * Test 2.3 prefixSet size > prefix limit
     * Expect to return false to not block BgpUpdate processing.
     */
    prefixSet->addPrefix(network2, false);
    EXPECT_TRUE(adjRib_->dropPrefixForOverloadProtection(
        0, network1, attrs, switchLimitConfig));
    EXPECT_TRUE(adjRib_->dropPrefixForOverloadProtection(
        0, network2, attrs, switchLimitConfig));
  }
}

TEST_F(
    AdjRibInboundFixture,
    DropPrefixForOverloadProtectionApplyGoldenPrefixPolicy) {
  const int64_t prefixLimit = 1;
  auto switchLimitConfig = std::make_shared<thrift::BgpSwitchLimitConfig>();
  switchLimitConfig->overload_protection_mode() =
      thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY;
  auto prefixSet = AdjRibPrefixSet::get();
  const auto network1 = folly::CIDRNetwork("10.0.0.1", 32);
  const auto network2 = folly::CIDRNetwork("10.0.0.1", 24);
  BgpPath attrs; // empty

  // Instantiate adjRib object
  setupAdjRib();

  // Set golden prefix policy
  auto thriftPolicy = createTGoldenPrefixPolicy(
      {kV4Prefix1}, 10 /* maxSubnets */, {32} /* allowedMaskLengths */);
  auto policy = std::make_shared<GoldenPrefixPolicy>(thriftPolicy);
  adjRib_->setGoldenPrefixPolicy(policy);

  switchLimitConfig->prefix_limit() = prefixLimit;
  EXPECT_FALSE(adjRib_->dropPrefixForOverloadProtection(
      0, network1, attrs, switchLimitConfig));

  prefixSet->addPrefix(network1, false);
  EXPECT_FALSE(adjRib_->dropPrefixForOverloadProtection(
      0, network1, attrs, switchLimitConfig));

  prefixSet->addPrefix(network2, false);
  /*
   * Golden prefix policy is now applied, so non-golden prefix is dropped.
   * For more tests, see RibPolicyTest.cpp.
   */
  EXPECT_TRUE(adjRib_->dropPrefixForOverloadProtection(
      0, network2, attrs, switchLimitConfig));

  // The prefix limit is exceeded and safe mode is triggered.
  EXPECT_TRUE(adjRib_->isSafeModeOn());

  EXPECT_EQ(fromAdjRibQ_.size(), 1);
  auto msg = folly::coro::blockingWait(fromAdjRibQ_.pop());
  EXPECT_TRUE(std::holds_alternative<AdjRib::TriggerSafeMode>(msg.message));

  // explicitly closing fibers to avoid unclean exit of adjrib
  fm_->addTask([&] { terminateAdjRib(); });
  evb_.loop();
}

TEST_F(
    AdjRibInboundFixture,
    DropPrefixForOverloadProtectionApplyGoldenVipLimitTest) {
  const int64_t prefixLimit = 1;
  auto switchLimitConfig = std::make_shared<thrift::BgpSwitchLimitConfig>();
  switchLimitConfig->overload_protection_mode() =
      thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY;
  switchLimitConfig->max_golden_vips() = 1;
  auto prefixSet = AdjRibPrefixSet::get();
  const auto network1 = folly::CIDRNetwork("10.0.0.1", 32);
  const auto network2 = folly::CIDRNetwork("10.0.0.1", 24);
  const auto network3 = folly::CIDRNetwork("13.0.0.1", 24);
  const auto network4 = folly::CIDRNetwork("14.0.0.1", 24);
  nettools::bgplib::BgpAttrCommunitiesC goldenCommunities;

  goldenCommunities.emplace_back(65446, 400);
  goldenCommunities.emplace_back(200, 666);
  BgpPath emptyAttrs, goldenAttrs;
  goldenAttrs.setCommunities(goldenCommunities);
  setupAdjRib(
      kShortGrRestartTime, /* localGrRestartTime */
      std::nullopt, /* remoteGrRestartTime */
      false /* no establish session call */);
  adjRib_->switchLimitConfig_ = switchLimitConfig;

  // Set golden prefix policy
  auto thriftPolicy = createTGoldenPrefixPolicy(
      {kV4Prefix1}, 10 /* maxSubnets */, {32} /* allowedMaskLengths */);
  auto policy = std::make_shared<GoldenPrefixPolicy>(thriftPolicy);
  adjRib_->setGoldenPrefixPolicy(policy);

  switchLimitConfig->prefix_limit() = prefixLimit;
  EXPECT_FALSE(adjRib_->dropPrefixForOverloadProtection(
      0, network1, emptyAttrs, switchLimitConfig));

  prefixSet->addPrefix(network1, false);
  EXPECT_FALSE(adjRib_->dropPrefixForOverloadProtection(
      0, network1, emptyAttrs, switchLimitConfig));

  prefixSet->addPrefix(network2, false);
  /*
   * Golden prefix policy is now applied, so non-golden prefix is dropped.
   * For more tests, see RibPolicyTest.cpp.
   */
  EXPECT_TRUE(adjRib_->dropPrefixForOverloadProtection(
      0, network2, emptyAttrs, switchLimitConfig));

  // The prefix limit is exceeded and safe mode is triggered.
  EXPECT_TRUE(adjRib_->isSafeModeOn());

  // Attempt to add a golden VIP, which should be allowed.
  EXPECT_FALSE(adjRib_->dropPrefixForOverloadProtection(
      0, network3, goldenAttrs, switchLimitConfig));
  prefixSet->addPrefix(network3, true);
  EXPECT_EQ(1, prefixSet->goldenVipSize());

  // Existing golden VIPs are allowed
  EXPECT_FALSE(adjRib_->dropPrefixForOverloadProtection(
      0, network3, goldenAttrs, switchLimitConfig));
  prefixSet->addPrefix(network3, true);
  EXPECT_EQ(1, prefixSet->goldenVipSize());

  /*
   * Attempt to add a golden VIP, which exceeds golden VIP limit, should be
   * disallowed.
   */
  EXPECT_TRUE(adjRib_->dropPrefixForOverloadProtection(
      0, network4, goldenAttrs, switchLimitConfig));
  EXPECT_EQ(1, prefixSet->goldenVipSize());
}

/*
 * verify that when bgp starts and safe mode file already exists, we will run in
 * safe mode immediately without checking switch limit
 */
TEST_F(
    AdjRibInboundFixture,
    DropPrefixForOverloadProtectionStickySafeModeTest) {
  const int64_t prefixLimit = 1;
  auto switchLimitConfig = std::make_shared<thrift::BgpSwitchLimitConfig>();
  switchLimitConfig->overload_protection_mode() =
      thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY;
  auto prefixSet = AdjRibPrefixSet::get();
  const auto network1 = folly::CIDRNetwork("8.0.0.1", 32);
  const auto network2 = folly::CIDRNetwork("10.0.0.1", 24);
  BgpPath attrs; // empty

  // Instantiate adjRib object
  setupAdjRib();

  // Set golden prefix policy
  auto thriftPolicy = createTGoldenPrefixPolicy(
      {kV4Prefix1}, 10 /* maxSubnets */, {32} /* allowedMaskLengths */);
  auto policy = std::make_shared<GoldenPrefixPolicy>(thriftPolicy);
  adjRib_->setGoldenPrefixPolicy(policy);

  switchLimitConfig->prefix_limit() = prefixLimit;
  adjRib_->setSafeModeOn();
  EXPECT_TRUE(adjRib_->isSafeModeOn());

  /*
   * non-golden prefix is dropped immediately without reaching switch level
   * limit
   */
  EXPECT_TRUE(adjRib_->dropPrefixForOverloadProtection(
      0, network2, attrs, switchLimitConfig));

  // golden prefix is allowed
  EXPECT_FALSE(adjRib_->dropPrefixForOverloadProtection(
      0, network1, attrs, switchLimitConfig));
  prefixSet->addPrefix(network1, false);
  EXPECT_FALSE(adjRib_->dropPrefixForOverloadProtection(
      0, network1, attrs, switchLimitConfig));

  // no message sent out
  EXPECT_EQ(fromAdjRibQ_.size(), 0);

  // explicitly closing fibers to avoid unclean exit of adjrib
  fm_->addTask([&] { terminateAdjRib(); });
  evb_.loop();
}

/*
 * This test verifies the re-evaluation of AdjRib when recAddPath_ is
 * false(re-evaluation works on AdjRibInLiteTree_ tree) when safe mode is
 * triggered.
 */
TEST_F(AdjRibInboundFixture, ProcessAdjRibReEvaluationForSafeModeTest) {
  setupAdjRib();

  // Announce 4 prefixes

  folly::CIDRNetwork v4Prefix1{"8.0.0.0", 32};
  const std::vector<folly::CIDRNetwork> inputPrefixSet{
      v4Prefix1, kV6Prefix1, kV4Prefix2, kV6Prefix2};
  fm_->addTask([&] {
    // Set golden prefix policy
    auto thriftPolicy = createTGoldenPrefixPolicy(
        {v4Prefix1}, 10 /* maxSubnets */, {32} /* allowedMaskLengths */);
    auto policy = std::make_shared<GoldenPrefixPolicy>(thriftPolicy);
    adjRib_->setGoldenPrefixPolicy(policy);
    // Announce prefixSet
    auto update = createV4AndV6BgpUpdateMultipleAnnounce(inputPrefixSet);
    adjRibInQ_->fiberPush(std::move(update));

    // Expect 2 RibInAnnouncements - 1 for v4 and 1 for v6
    auto msg1 = folly::coro::blockingWait(ribInQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg1));

    auto msg2 = folly::coro::blockingWait(ribInQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg2));
  });

  fm_->addTask([&] {
    fiberSleepFor(10ms);
    /*
     * Verify stats
     * received 4
     */
    EXPECT_EQ(4, adjRib_->getStats().getPreInPrefixCount());
    // accepted by BGP 4
    EXPECT_EQ(4, adjRib_->getStats().getPostInPrefixCount());

    // Execute policy re-evaluation
    folly::coro::blockingWait(
        adjRib_->processAdjRibReEvaluation(RibPauseResumeCause::SAFE_MODE));

    // Expect adjRibEntry to be kept for 1 golden prefix
    EXPECT_NE(nullptr, adjRib_->getRibEntry(/*ingress=*/true, v4Prefix1));

    // Expect adjRibEntry to be cleared for 3 non-golden prefixes
    EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix1));
    EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2));
    EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix2));

    // Expect 1 withdrawal to be generated with 3 non-golden prefixes
    auto msg = folly::coro::blockingWait(ribInQ_.pop());
    auto withdrawal = std::get<RibInWithdrawal>(msg);
    EXPECT_EQ(3, withdrawal.pfxPathIds.size());

    // Expect counter to be updated
    EXPECT_EQ(
        3,
        fb303::ThreadCachedServiceData::get()->getCounter(
            PeerStats::kTotalPrefixesDroppedByLimit));

    // End session to complete test
    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * This test verifies the re-evaluation of AdjRib for golden VIP when
 * recAddPath_ is false(re-evaluation works on AdjRibInLiteTree_ tree) when safe
 * mode is triggered.
 */
TEST_F(
    AdjRibInboundFixture,
    ProcessAdjRibReEvaluationForSafeModeGoldenVIPsTest) {
  folly::CIDRNetwork goldenNonVip{"8.0.0.0", 24};
  auto nonGoldenNonVip = kV6Prefix1;
  auto goldenVip1 = kV6Prefix2;
  auto goldenVip2 = kV4Prefix2;

  auto prefixSet = AdjRibPrefixSet::get();
  auto switchLimitConfig = std::make_shared<thrift::BgpSwitchLimitConfig>();
  switchLimitConfig->overload_protection_mode() =
      thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY;
  switchLimitConfig->max_golden_vips() = 1;
  setupAdjRib();
  adjRib_->switchLimitConfig_ = switchLimitConfig;

  /*
   * Announce 4 prefixes, including 1 golden non-VIP prefix, and 2 golden VIPs
   * and 1 non-golden non-VIP prefix
   */

  const std::vector<folly::CIDRNetwork> inputPrefixSet{
      goldenNonVip, nonGoldenNonVip};
  fm_->addTask([&] {
    // Set golden prefix policy
    auto thriftPolicy = createTGoldenPrefixPolicy(
        {goldenNonVip}, 10 /* maxSubnets */, {24} /* allowedMaskLengths */);
    auto policy = std::make_shared<GoldenPrefixPolicy>(thriftPolicy);
    adjRib_->setGoldenPrefixPolicy(policy);
    // Announce prefixSet
    auto update1 = createV4AndV6BgpUpdateMultipleAnnounce(inputPrefixSet);
    adjRibInQ_->fiberPush(std::move(update1));
    // Announce golden VIPs
    std::vector<BgpAttrCommunityC> comms = {kGoldenVipCommunity};
    auto update2 = createV4AndV6BgpUpdateSingleAnnounce(goldenVip1, comms);
    adjRibInQ_->fiberPush(std::move(update2));
    auto update3 = createV4AndV6BgpUpdateSingleAnnounce(goldenVip2, comms);
    adjRibInQ_->fiberPush(std::move(update3));
  });

  fm_->addTask([&] {
    // Sleep for 50ms to allow all updates to be processed
    fiberSleepFor(50ms);
    /*
     * Verify stats
     * received 4
     */
    EXPECT_EQ(4, adjRib_->getStats().getPreInPrefixCount());
    // accepted by BGP 4
    EXPECT_EQ(4, adjRib_->getStats().getPostInPrefixCount());

    // Execute policy re-evaluation
    folly::coro::blockingWait(
        adjRib_->processAdjRibReEvaluation(RibPauseResumeCause::SAFE_MODE));

    // Expect adjRibEntry to be kept for 1 golden prefix
    EXPECT_NE(nullptr, adjRib_->getRibEntry(/*ingress=*/true, goldenNonVip));

    // non-golden non-VIP prefix is removed
    EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, nonGoldenNonVip));

    // Only one golden VIP is allowed, expect the other one to be dropped.
    auto goldenVip1Entry = adjRib_->getRibEntry(/*ingress=*/true, goldenVip1);
    auto goldenVip2Entry = adjRib_->getRibEntry(/*ingress=*/true, goldenVip2);
    EXPECT_NE(goldenVip1Entry == nullptr, goldenVip2Entry == nullptr);

    // Expect counter to be updated
    EXPECT_EQ(
        2,
        fb303::ThreadCachedServiceData::get()->getCounter(
            PeerStats::kTotalPrefixesDroppedByLimit));
    EXPECT_EQ(
        1,
        fb303::ThreadCachedServiceData::get()->getCounter(
            PeerStats::kTotalGoldenVipPrefixes));
    EXPECT_EQ(1, prefixSet->goldenVipSize());
    /*
     * verify mark an existing golden VIP won't cause kTotalGoldenVipPrefixes
     * increase
     */
    auto markedGoldenVip = goldenVip1Entry ? goldenVip1 : goldenVip2;
    prefixSet->markGoldenVip(markedGoldenVip);
    EXPECT_EQ(
        1,
        fb303::ThreadCachedServiceData::get()->getCounter(
            PeerStats::kTotalGoldenVipPrefixes));
    EXPECT_EQ(1, prefixSet->goldenVipSize());

    // End session to complete test
    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * This test verifies the re-evaluation of AdjRib when recAddPath_ is
 * true(re-evaluation works on AdjRibInPathTree_ tree) when safe mode is
 * triggered.
 */
TEST_F(
    AdjRibInboundFixture,
    ProcessAdjRibReEvaluationForSafeModeWithRecAddPathTest) {
  setupAdjRib();

  // Announce 2 prefixes

  folly::CIDRNetwork goldenV4Prefix1{"8.0.0.0", 32};
  folly::CIDRNetwork goldenV4Prefix2{"8.0.0.1", 32};
  const std::vector<folly::CIDRNetwork> inputPrefixSet{
      goldenV4Prefix1, goldenV4Prefix2, kV6Prefix1, kV4Prefix2, kV6Prefix2};
  fm_->addTask([&] {
    adjRib_->recAddPath_ = true;
    // Set golden prefix policy
    auto thriftPolicy = createTGoldenPrefixPolicy(
        {kV4Prefix1}, 1 /* maxSubnets */, {32} /* allowedMaskLengths */);
    auto policy = std::make_shared<GoldenPrefixPolicy>(thriftPolicy);
    adjRib_->setGoldenPrefixPolicy(policy);
    // Announce prefixSet
    auto update = createV4AndV6BgpUpdateMultipleAnnounce(inputPrefixSet);
    adjRibInQ_->fiberPush(std::move(update));

    // Expect 2 RibInAnnouncements - 1 for v4 and 1 for v6
    auto msg1 = folly::coro::blockingWait(ribInQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg1));

    auto msg2 = folly::coro::blockingWait(ribInQ_.pop());
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg2));
  });

  fm_->addTask([&] {
    fiberSleepFor(10ms);
    /*
     * Verify stats
     * received 5
     */
    EXPECT_EQ(5, adjRib_->getStats().getPreInPrefixCount());
    // accepted by BGP 5
    EXPECT_EQ(5, adjRib_->getStats().getPostInPrefixCount());

    // Execute policy re-evaluation
    folly::coro::blockingWait(
        adjRib_->processAdjRibReEvaluation(RibPauseResumeCause::SAFE_MODE));

    // Expect adjRibEntry to be kept for 1 golden prefix
    EXPECT_NE(nullptr, adjRib_->getRibEntry(/*ingress=*/true, goldenV4Prefix1));

    /*
     * Since the subnet limit of 1 is exceeded, expect adjRibEntry to be cleared
     * for the second golden prefix.
     */
    EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, goldenV4Prefix2));

    // Expect adjRibEntry to be cleared for 3 non-golden prefixes
    EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix1));
    EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2));
    EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix2));

    // Expect 1 withdrawal to be generated with 4 non-golden prefixes
    auto msg = folly::coro::blockingWait(ribInQ_.pop());
    auto withdrawal = std::get<RibInWithdrawal>(msg);
    EXPECT_EQ(4, withdrawal.pfxPathIds.size());

    // End session to complete test
    terminateAdjRib();
  });
  evb_.loop();
}

/**
 * This test verifies the case where DUT is a GR helper and now needs to enter
 * safe mode.
 * Test is simulating the following steps
 * 1. Peer announces 2 prefixes(both non-golden)
 * 2. Peer goes down, DUT becomes GR helper
 * 3. Peer comes back up, announces 2 more prefixes(1 golden and 1 non-golden)
 * 4. DUT enters safe mode
 * 5. Verify that only 1 golden prefix is kept in AdjRib
 */
TEST_F(AdjRibInboundFixture, ProcessAdjRibReEvaluationForSafeModeGrHelperTest) {
  setupAdjRib(
      kShortGrRestartTime, // local
      kLongGrRestartTime // remote
  );

  std::vector<folly::CIDRNetwork> prefixSet1{kV6Prefix1, kV6Prefix2};
  folly::CIDRNetwork v4Prefix1{"8.0.0.0", 32};
  std::vector<folly::CIDRNetwork> prefixSet2{v4Prefix1, kV6Prefix3};
  fm_->addTask([&] {
    adjRib_->recAddPath_ = true;
    // Set golden prefix policy
    auto thriftPolicy = createTGoldenPrefixPolicy(
        {v4Prefix1}, 10 /* maxSubnets */, {32} /* allowedMaskLengths */);
    auto policy = std::make_shared<GoldenPrefixPolicy>(thriftPolicy);
    adjRib_->setGoldenPrefixPolicy(policy);
    // Announce prefixSet1
    auto update = createV6BgpUpdateMultipleAnnounce(prefixSet1);
    adjRibInQ_->fiberPush(std::move(update));
    fiberSleepFor(50ms);
    // Announce prefixSet2
    update = createV4AndV6BgpUpdateMultipleAnnounce(prefixSet2);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    // Step 1: Simulate Session down with GR
    {
      // wait till RibInAnnouncement route message is sent
      folly::coro::blockingWait(ribInQ_.pop());
      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPostInPrefixCount());

      // Terminate the session
      terminateAdjRib(true);

      // wait for sometime
      fiberSleepFor(10ms);

      /*
       * Verify AdjRibStaleTree size is non-zero since we are in GR
       * Note that size here is for num prefixes because only prefix
       * is used as key to radix tree.
       */
      EXPECT_EQ(prefixSet1.size(), adjRib_->getRibInStaleTreeSize());
      // Verify AdjRibLiteTree size has gone to zero
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(
              /*ingress=*/true, /*isAddPathEnabled=*/false));
      // Verify AdjRibTree size is zero
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPostInPrefixCount());
    }
    // Step 2: Establish the session and enter Safe mode
    {
      reEstablishSession(kLongGrRestartTime);
      // wait till RibInAnnouncement route message is sent
      auto msg1 = folly::coro::blockingWait(ribInQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg1));

      auto msg2 = folly::coro::blockingWait(ribInQ_.pop());
      ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg2));

      // Expect AdjRib_ to prefixSet2.size()
      EXPECT_EQ(
          prefixSet2.size(),
          adjRib_->getRibTreeSize(
              /*ingress=*/true, /*isAddPathEnabled=*/false));

      EXPECT_EQ(prefixSet1.size(), adjRib_->getRibInStaleTreeSize());

      /*
       * Execute policy re-evaluation
       * Note: Here re-evaluation happens on prefixes present in AdjRibIn.
       * prefixes in stale tree are purged after GR timer
       */
      folly::coro::blockingWait(
          adjRib_->processAdjRibReEvaluation(RibPauseResumeCause::SAFE_MODE));

      // Expect adjRibEntry to be kept for 1 golden prefix
      EXPECT_NE(nullptr, adjRib_->getRibEntry(/*ingress=*/true, v4Prefix1));

      // Expect adjRibEntry to be cleared for non-golden prefix
      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix2));

      // Expect 1 withdrawal to be generated with 1 non-golden prefixes
      auto msg = folly::coro::blockingWait(ribInQ_.pop());
      auto withdrawal = std::get<RibInWithdrawal>(msg);
      EXPECT_EQ(1, withdrawal.pfxPathIds.size());

      // End session to complete test
      terminateAdjRib();
    }
  });
  evb_.loop();
}

TEST_F(AdjRibInboundFixture, SafeModeSubnetLimitAppliesToNewPrefixUpdates) {
  setupAdjRib();

  adjRib_->switchLimitConfig_ =
      std::make_shared<thrift::BgpSwitchLimitConfig>();
  adjRib_->switchLimitConfig_->overload_protection_mode() =
      thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY;

  // Set golden prefix policy
  auto thriftPolicy = createTGoldenPrefixPolicy(
      {kV4Prefix1}, 1 /* maxSubnets */, {32} /* allowedMaskLengths */);
  auto policy = std::make_shared<GoldenPrefixPolicy>(thriftPolicy);
  adjRib_->setGoldenPrefixPolicy(policy);
  adjRib_->setSafeModeOn();

  folly::CIDRNetwork goldenV4Prefix1{"8.0.0.0", 32};
  folly::CIDRNetwork goldenV4Prefix2{"8.0.0.1", 32};
  const std::vector<folly::CIDRNetwork> inputPrefixSet{
      goldenV4Prefix1, goldenV4Prefix2};
  fm_->addTask([&] {
    // Announce 2 golden prefixes
    adjRibInQ_->fiberPush(
        createV4AndV6BgpUpdateMultipleAnnounce(inputPrefixSet));
    ASSERT_TRUE(
        std::holds_alternative<RibInAnnouncement>(
            folly::coro::blockingWait(ribInQ_.pop())));
    fiberSleepFor(10ms); // sleep to yield thread to adjrib

    /*
     * 2 prefixes were sent, but the second is dropped because the subnet limit
     * of 1 is exceeded.
     */
    EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(
        1,
        fb303::ThreadCachedServiceData::get()->getCounter(
            PeerStats::kTotalPrefixesDroppedByLimit));

    // Expect adjRibEntry to be kept for 1 golden prefix
    EXPECT_NE(nullptr, adjRib_->getRibEntry(/*ingress=*/true, goldenV4Prefix1));

    // adjRibEntry is never created for the second golden prefix.
    EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, goldenV4Prefix2));

    // Withdraw goldenV4Prefix1
    adjRibInQ_->fiberPush(createV4BgpUpdateSingleWithdraw(goldenV4Prefix1));
    ASSERT_TRUE(
        std::holds_alternative<RibInWithdrawal>(
            folly::coro::blockingWait(ribInQ_.pop())));
    fiberSleepFor(10ms); // sleep to yield thread to adjrib

    // AdjRibEntry is removed for withdrawn prefix
    EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, goldenV4Prefix1));

    // Re-announce goldenV4Prefix2.
    adjRibInQ_->fiberPush(createV4BgpUpdateSingleAnnounce(goldenV4Prefix2));
    /*
     * Since goldenV4Prefix1 is withdrawn, goldenV4Prefix2 is the only subnet,
     * so it's now accepted and announced.
     */
    ASSERT_TRUE(
        std::holds_alternative<RibInAnnouncement>(
            folly::coro::blockingWait(ribInQ_.pop())));
    fiberSleepFor(10ms); // sleep to yield thread to adjrib

    EXPECT_NE(nullptr, adjRib_->getRibEntry(/*ingress=*/true, goldenV4Prefix2));

    // Dropped prefixes counter is unchanged from earlier.
    EXPECT_EQ(
        1,
        fb303::ThreadCachedServiceData::get()->getCounter(
            PeerStats::kTotalPrefixesDroppedByLimit));

    // End session to complete test
    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * This test verified the configured switchLimitConfig with prefix limit.
 *
 * When the prefix limit is reached, we expect BGP to ignore the processing of
 * incoming BgpUpdate.
 */
TEST_F(AdjRibInboundFixture, PrefixLimitSetSingletonTest) {
  PeeringParams params1{
      fboss::kDefaultVrf,
      folly::IPAddress("1.1.1.1"), // peerAddr
      std::nullopt, // peerPrefix
      4200000001, // globalAs
      4200000001, // localAs
      4200000002, // remoteAs
      IPAddressV4("2.2.2.2"), // routerId
      std::chrono::seconds(180), // holdTime
      std::chrono::seconds(120) // grRestartTime
  };
  PeeringParams params2{
      fboss::kDefaultVrf,
      folly::IPAddress("2.2.2.2"), // peerAddr
      std::nullopt, // peerPrefix
      4200000002, // globalAs
      4200000002, // localAs
      4200000001, // remoteAs
      IPAddressV4("1.1.1.1"), // routerId
      std::chrono::seconds(180), // holdTime
      std::chrono::seconds(120) // grRestartTime
  };

  // Instantiate adjRib object
  auto adjRib1 = setupAdjRib(kPeerId1, params1);
  auto adjRib2 = setupAdjRib(kPeerId2, params2);
  auto prefixSet1 = AdjRibPrefixSet::get();
  auto prefixSet2 = AdjRibPrefixSet::get();

  // Ensure adjrib1 and adjrib2 are different object
  EXPECT_NE(adjRib1, adjRib2);

  // Ensure the ingressPrefixSet shared_ptr to the same memory address.
  EXPECT_EQ(prefixSet1, prefixSet2);
}

/*
 * This test verified the mutator and accessor of AdjRibStats, which including:
 *  1. mutation of preIn counter, aka, per-adjrib
 *  2. mutation of total ingress path counter, aka, all adjribs
 *  3. mutation of Singleton of AdjRibPrefixSet
 *  4. mutation of collections for unique VIP prefix count
 */
TEST_F(AdjRibInboundFixture, AdjRibStatsBasicTest) {
  auto prefixSet = AdjRibPrefixSet::get();
  const auto network1 = folly::CIDRNetwork("10.0.0.1", 32);
  const auto network2 = folly::CIDRNetwork("10.0.0.1", 24);
  const auto network3 = folly::CIDRNetwork("10.0.0.1", 16);
  const auto v6Network = folly::IPAddress::createNetwork("2001:db8::/32");

  // Instantiate adjRib object
  setupAdjRib(
      kShortGrRestartTime, /* localGrRestartTime */
      std::nullopt, /* remoteGrRestartTime */
      false /* no establish session call */);

  // Validate empty counters
  auto counters = facebook::fb303::ThreadCachedServiceData::getShared();
  EXPECT_FALSE(counters->hasCounter(PeerStats::kTotalUniquePrefixes));

  {
    /*
     * Test 1.1: increment preIn with a new prefix.
     *  - Verify the preIn counter is increased.
     *  - Verify the total received prefix counter is increased.
     *  - Verify the prefixSet collection size is incremented.
     */
    adjRib_->stats_.incrementPreInPrefixCount(
        network1, false /* isVipPrefix */, false /* isGoldenVip */);
    EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCountIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCountIpv6());
    EXPECT_EQ(1, totalRcvdPrefixCount); /* global variable access */
    EXPECT_EQ(1, prefixSet->size());

    // Check counters
    EXPECT_EQ(
        1,
        facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
            PeerStats::kTotalUniquePrefixes));
  }

  {
    /*
     * Test 1.2: increment preIn with the same prefix.
     *  - Verify the preIn counter is increased.
     *  - Verify the total received prefix counter is increased.
     *  - Verify the prefixSet collection size does not increase.
     */
    adjRib_->stats_.incrementPreInPrefixCount(
        network1, false /* isVipPrefix */, false /* isGoldenVip */);
    EXPECT_EQ(2, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(2, totalRcvdPrefixCount); /* global variable access */
    EXPECT_EQ(0, totalVipPrefixesCount);
    EXPECT_EQ(1, prefixSet->size());
    EXPECT_EQ(2, prefixSet->getRefCount(network1).second.refCount_);
    EXPECT_FALSE(prefixSet->getRefCount(network2).second.isGoldenVip_);
    EXPECT_EQ(0, prefixSet->goldenVipSize());

    // Check counters
    EXPECT_EQ(
        1,
        facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
            PeerStats::kTotalUniquePrefixes));
  }

  {
    /*
     * Test 1.3: increment preIn with another VIP prefix.
     *  - Verify the preIn counter is increased.
     *  - Verify the total received prefix counter is increased.
     *  - Verify the vip prefix counter is increased.
     *  - Verify the prefixSet collection size is increased.
     */
    adjRib_->stats_.incrementPreInPrefixCount(
        network2, true /* isVipPrefix */, false /* isGoldenVip */);
    EXPECT_EQ(3, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(3, totalRcvdPrefixCount); /* global variable access */
    EXPECT_EQ(1, totalVipPrefixesCount);
    EXPECT_EQ(2, prefixSet->size());
    EXPECT_EQ(1, prefixSet->getRefCount(network2).second.refCount_);
    EXPECT_FALSE(prefixSet->getRefCount(network2).second.isGoldenVip_);
    EXPECT_EQ(0, prefixSet->goldenVipSize());

    // Check counters
    EXPECT_EQ(
        2,
        facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
            PeerStats::kTotalUniquePrefixes));
  }

  {
    /*
     * Test 1.4: increment preIn with another Golden VIP prefix.
     *  - Verify the preIn counter is increased.
     *  - Verify the total received prefix counter is increased.
     *  - Verify the vip prefix counter is increased.
     *  - Verify the prefixSet collection size is increased.
     *  - Verify the prefixSet goldenVipSize is increased.
     */
    adjRib_->stats_.incrementPreInPrefixCount(
        network3, true /* isVipPrefix */, true /* isGoldenVip */);
    EXPECT_EQ(4, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(4, totalRcvdPrefixCount); /* global variable access */
    EXPECT_EQ(2, totalVipPrefixesCount);
    EXPECT_EQ(3, prefixSet->size());
    EXPECT_EQ(1, prefixSet->getRefCount(network3).second.refCount_);
    EXPECT_TRUE(prefixSet->getRefCount(network3).second.isGoldenVip_);
    EXPECT_EQ(1, prefixSet->goldenVipSize());

    // Check counters
    EXPECT_EQ(
        3,
        facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
            PeerStats::kTotalUniquePrefixes));
    // Check counters
    EXPECT_EQ(
        1,
        facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
            PeerStats::kTotalGoldenVipPrefixes));
  }

  {
    /*
     * Test 2.1: decrement preIn with an existing prefix.
     *  - Verify the preIn counter is decreased.
     *  - Verify the total received prefix counter is decreased.
     *  - Verify the vip prefix counter is unchanged.
     *  - Verify the prefixSet size does not decrease due to refCount.
     */
    adjRib_->stats_.decrementPreInPrefixCount(network1);
    EXPECT_EQ(3, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(3, totalRcvdPrefixCount); /* global variable access */
    EXPECT_EQ(2, totalVipPrefixesCount);
    EXPECT_EQ(3, prefixSet->size()); /* refCount > 0 */
    EXPECT_EQ(1, prefixSet->getRefCount(network1).second.refCount_);

    // Check counters
    EXPECT_EQ(
        3,
        facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
            PeerStats::kTotalUniquePrefixes));
  }

  {
    /*
     * Test 2.2: decrement preIn with the VIP prefix.
     *  - Verify the preIn counter is decreased.
     *  - Verify the total received prefix counter is decreased.
     *  - Verify the vip prefix counter is decreased.
     *  - Verify the prefixSet collection size decrease.
     */
    adjRib_->stats_.decrementPreInPrefixCount(network2);
    EXPECT_EQ(2, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(2, totalRcvdPrefixCount); /* global variable access */
    EXPECT_EQ(1, totalVipPrefixesCount);
    EXPECT_EQ(2, prefixSet->size()); /* refCount == 0 */
    EXPECT_FALSE(prefixSet->getRefCount(network2).first);

    // Check counters
    EXPECT_EQ(
        2,
        facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
            PeerStats::kTotalUniquePrefixes));
  }

  {
    /*
     * Test 3.1: clear prefixSet
     *  - Verify radixTree is empty.
     *  - Verify counter is updated.
     */
    adjRib_->stats_.decrementPreInPrefixCount(network1);
    adjRib_->stats_.decrementPreInPrefixCount(network3);
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(0, totalRcvdPrefixCount); /* global variable access */
    EXPECT_EQ(0, totalVipPrefixesCount);
    EXPECT_EQ(0, prefixSet->size()); /* refCount > 0 */
    EXPECT_EQ(0, prefixSet->goldenVipSize()); /* refCount > 0 */

    // Check counters
    EXPECT_EQ(
        0,
        facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
            PeerStats::kTotalUniquePrefixes));
  }

  {
    /*
     * Test 4: v4/v6 prefix counting for all counter types.
     *  - Verify preIn v4/v6 counters.
     *  - Verify postIn v4/v6 counters.
     *  - Verify preOut v4/v6 counters.
     *  - Verify postOut v4/v6 counters.
     */

    // --- preIn ---
    adjRib_->stats_.incrementPreInPrefixCount(
        v6Network, false /* isVipPrefix */, false /* isGoldenVip */);
    EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCountIpv4());
    EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCountIpv6());

    adjRib_->stats_.incrementPreInPrefixCount(
        network1, false /* isVipPrefix */, false /* isGoldenVip */);
    EXPECT_EQ(2, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCountIpv4());
    EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCountIpv6());

    adjRib_->stats_.decrementPreInPrefixCount(v6Network);
    EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCountIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCountIpv6());

    adjRib_->stats_.decrementPreInPrefixCount(network1);
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCountIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCountIpv6());

    // --- postIn ---
    adjRib_->stats_.incrementPostInPrefixCount(false /* isIpv4 (v6) */);
    EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCountIpv4());
    EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCountIpv6());

    adjRib_->stats_.incrementPostInPrefixCount(true /* isIpv4 */);
    EXPECT_EQ(2, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCountIpv4());
    EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCountIpv6());

    adjRib_->stats_.decrementPostInPrefixCount(false /* isIpv4 (v6) */);
    EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCountIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCountIpv6());

    adjRib_->stats_.decrementPostInPrefixCount(true /* isIpv4 */);
    EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCountIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCountIpv6());

    // --- preOut ---
    adjRib_->stats_.incrementPreOutPrefixCount(false /* isIpv4 (v6) */);
    EXPECT_EQ(1, adjRib_->getStats().getPreOutPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPreOutPrefixCountIpv4());
    EXPECT_EQ(1, adjRib_->getStats().getPreOutPrefixCountIpv6());

    adjRib_->stats_.incrementPreOutPrefixCount(true /* isIpv4 */);
    EXPECT_EQ(2, adjRib_->getStats().getPreOutPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getPreOutPrefixCountIpv4());
    EXPECT_EQ(1, adjRib_->getStats().getPreOutPrefixCountIpv6());

    adjRib_->stats_.decrementPreOutPrefixCount(false /* isIpv4 (v6) */);
    EXPECT_EQ(1, adjRib_->getStats().getPreOutPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getPreOutPrefixCountIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getPreOutPrefixCountIpv6());

    adjRib_->stats_.decrementPreOutPrefixCount(true /* isIpv4 */);
    EXPECT_EQ(0, adjRib_->getStats().getPreOutPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPreOutPrefixCountIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getPreOutPrefixCountIpv6());

    // --- postOut ---
    adjRib_->stats_.incrementPostOutPrefixCount(false /* isIpv4 (v6) */);
    EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPostOutPrefixCountIpv4());
    EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCountIpv6());

    adjRib_->stats_.incrementPostOutPrefixCount(true /* isIpv4 */);
    EXPECT_EQ(2, adjRib_->getStats().getPostOutPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCountIpv4());
    EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCountIpv6());

    adjRib_->stats_.decrementPostOutPrefixCount(false /* isIpv4 (v6) */);
    EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCountIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getPostOutPrefixCountIpv6());

    adjRib_->stats_.decrementPostOutPrefixCount(true /* isIpv4 */);
    EXPECT_EQ(0, adjRib_->getStats().getPostOutPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPostOutPrefixCountIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getPostOutPrefixCountIpv6());
  }
}

/*
 * This test verify the golden VIP limit control:
 *  1. If a VIP is already in prefixSet, allow it;
 *  2. If a VIP is not in prefixSet, and total golden vip count is below
 * max_golden_vips, allow it;
 *  2. If a VIP is not in prefixSet, and total golden vip count above
 * max_golden_vips, disallow it;
 */
TEST_F(AdjRibInboundFixture, AllowGoldenVipTest) {
  auto prefixSet = AdjRibPrefixSet::get();
  const auto network1 = folly::CIDRNetwork("10.0.0.1", 32);
  const auto network2 = folly::CIDRNetwork("10.0.0.1", 24);
  const auto network3 = folly::CIDRNetwork("10.0.0.1", 16);
  auto switchLimitConfig = std::make_shared<thrift::BgpSwitchLimitConfig>();

  // Instantiate adjRib object
  setupAdjRib(
      kShortGrRestartTime, /* localGrRestartTime */
      std::nullopt, /* remoteGrRestartTime */
      false /* no establish session call */);
  adjRib_->switchLimitConfig_ = switchLimitConfig;

  {
    /*
     * Test 1:
     * If golden VIP limit is 0 or not set, disallow any golden VIP
     */
    EXPECT_FALSE(adjRib_->allowGoldenVip(network1));
    switchLimitConfig->max_golden_vips() = 0;
    EXPECT_FALSE(adjRib_->allowGoldenVip(network1));
  }

  {
    /*
     * Test 2:
     * Add one golden VIP to prefixSet.
     * Add the same golden VIP, which should be allowed
     */
    switchLimitConfig->max_golden_vips() = 2;
    prefixSet->addPrefix(network1, true /* isGoldenVip */);
    EXPECT_EQ(1, prefixSet->getRefCount(network1).second.refCount_);
    EXPECT_TRUE(prefixSet->getRefCount(network1).second.isGoldenVip_);
    EXPECT_EQ(1, prefixSet->goldenVipSize());
    EXPECT_TRUE(adjRib_->allowGoldenVip(network1));
  }

  {
    /*
     * Test 3:
     * Remain the added golden VIP in prefixSet.
     * Add a different golden VIP, which should be allowed
     */
    EXPECT_TRUE(adjRib_->allowGoldenVip(network2));
    prefixSet->addPrefix(network2, true /* isGoldenVip */);
    EXPECT_EQ(1, prefixSet->getRefCount(network2).second.refCount_);
    EXPECT_TRUE(prefixSet->getRefCount(network2).second.isGoldenVip_);
    EXPECT_EQ(2, prefixSet->goldenVipSize());
  }

  {
    /*
     * Test 4:
     * Remain the added golden VIPs in prefixSet.
     * Add a different golden VIP, which should be disallowed
     */
    EXPECT_FALSE(adjRib_->allowGoldenVip(network3));
  }
}

// Verify a VIP is golden VIP
TEST_F(AdjRibInboundFixture, IsGoldenVipTest) {
  std::vector<nettools::bgplib::BgpAttrCommunityC> nonGoldenCommunities,
      goldenCommunities;

  nonGoldenCommunities.emplace_back(200, 666);
  nonGoldenCommunities.emplace_back(100, 234);
  goldenCommunities.emplace_back(65446, 400);
  goldenCommunities.emplace_back(200, 666);

  EXPECT_TRUE(AdjRib::isGoldenVip(goldenCommunities));
  EXPECT_FALSE(AdjRib::isGoldenVip(nonGoldenCommunities));
}

/*
 * Verify percentage set to default if invalid one is provided and verify the
 * case when set to unlimitted
 */
TEST_F(AdjRibInboundFixture, CheckLimitAndAlarmInvalidInputTest) {
  /*
   * send 4 prefixes
   * premax = 0, unlimited, not alarm
   * % = 200, will be set to default (100%), not alarm
   */
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      true, // do not establish
      kLocalAs1, // global as
      kLocalAs1, // local as
      kRemoteAs1,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      nullptr, // policy manager, not used
      std::nullopt, // ingress policy name, not used
      kIsConfedPeerFalse,
      std::nullopt, // local confed as, not used
      std::nullopt, // as confed id, not used
      AdvertiseLinkBandwidth::DISABLE, // advertiseLinkBandwidth, not
                                       // used
      ReceiveLinkBandwidth::ACCEPT, // receiveLinkBandwidth
      std::nullopt, // linkBandwidthBps
      ValidateRemoteAs{true}, // not used
      0, // maxRoutes
      true, // warningOnly
      1 // warningLimit
  );

  const std::vector<folly::CIDRNetwork> inputPrefixSet{
      kV4Prefix1, kV6Prefix1, kV4Prefix2, kV6Prefix2};

  fm_->addTask([&] {
    auto updates = createV4AndV6BgpUpdateMultipleAnnounce(inputPrefixSet);
    adjRibInQ_->fiberPush(std::move(updates));
  });

  fm_->addTask([&] {
    fiberSleepFor(10ms);
    /*
     * Verify stats
     * received 4
     */
    EXPECT_EQ(4, adjRib_->getStats().getPreInPrefixCount());
    // accepted 4
    EXPECT_EQ(4, adjRib_->getStats().getPostInPrefixCount());

    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * Verify work flow
 *
 * This test case tested all the prefiltered limits, the postfilter go through
 * same code path, should be good to verify one of them.
 *
 * Settings : Max = 3, WarningLimit = 1 (50%), warningOnly = true
 * 1. send 4 prefixes
 *  - verify we only receive 3
 *  - verify indicators
 *    - exceed max, should be True
 *    - exceed warning, should be True
 *    - loggedOnce, should be True
 *
 * 2. withdraw 1 prefix
 *  - verify indicators
 *    - exceed max, should be False
 *    - exceed warning, should be True
 *    - loggedOnce, should be True
 *
 * 3. withdraw 1 more prefix
 *  - verify indicators
 *    - exceed max, should be False
 *    - exceed warning, should be False
 *    - loggedOnce, should be False
 *
 * 4. send 1 prefix
 *  - verify indicators
 *    - exceed max, should be False
 *    - exceed warning, should be True
 *    - loggedOnce, should be True
 */
TEST_F(
    AdjRibInboundFixture,
    CheckLimitAndAlarmWarningOnlyWithWarningLimitTest) {
  // set max = 3, % = 50, warning-only
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      true, // do not establish
      kLocalAs1, // global as
      kLocalAs1, // local as
      kRemoteAs1,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      nullptr, // policy manager, not used
      std::nullopt, // ingress policy name, not used
      kIsConfedPeerFalse,
      std::nullopt, // local confed as, not used
      std::nullopt, // as confed id, not used
      AdvertiseLinkBandwidth::DISABLE, // advertiseLinkBandwidth, not
                                       // used
      ReceiveLinkBandwidth::ACCEPT, // receiveLinkBandwidth
      std::nullopt, // linkBandwidthBps
      ValidateRemoteAs{true}, // not used
      3, // maxRoutes
      true, // warningOnly
      50, // warningLimit
      3, // maxRoutes
      true, // warningOnly
      50 // warningLimit
  );

  const std::vector<folly::CIDRNetwork> inputPrefixSet{
      kV4Prefix1, kV6Prefix1, kV4Prefix2, kV6Prefix2};
  folly::fibers::Baton okToSend1;
  folly::fibers::Baton okToSend2;
  folly::fibers::Baton okToSend3;
  folly::fibers::Baton okToSend4;
  folly::fibers::Baton okToCheck;

  fm_->addTask([&] {
    okToSend1.wait();

    auto updates = createV4AndV6BgpUpdateMultipleAnnounce(inputPrefixSet);
    adjRibInQ_->fiberPush(std::move(updates));
    okToCheck.post();
  });

  fm_->addTask([&] {
    okToSend2.wait();

    auto withdrawl = createV4BgpUpdateSingleWithdraw(kV4Prefix1);
    adjRibInQ_->fiberPush(std::move(withdrawl));
    okToCheck.post();
  });

  fm_->addTask([&] {
    okToSend3.wait();

    auto withdrawl = createV4BgpUpdateSingleWithdraw(kV6Prefix1);
    adjRibInQ_->fiberPush(std::move(withdrawl));
    okToCheck.post();
  });

  fm_->addTask([&] {
    okToSend4.wait();

    auto update = createV4BgpUpdateSingleAnnounce(kV4Prefix1);
    adjRibInQ_->fiberPush(std::move(update));
    okToCheck.post();
  });

  fm_->addTask([&] {
    okToSend1.post();
    okToCheck.wait();
    fiberSleepFor(2ms);
    /*
     * route counts going up
     * received 3, 1 of them get dropped because pre max = 3
     */
    EXPECT_EQ(3, adjRib_->getStats().getPreInPrefixCount());
    // accepted 3
    EXPECT_EQ(3, adjRib_->getStats().getPostInPrefixCount());

    okToSend2.post();
    okToCheck.wait();
    fiberSleepFor(2ms);
    /*
     * route counts going down (by sending withdrawl)
     * received 2
     */
    EXPECT_EQ(2, adjRib_->getStats().getPreInPrefixCount());
    // accepted 2
    EXPECT_EQ(2, adjRib_->getStats().getPostInPrefixCount());

    // Negative test, ensure that VIP counters are not increasing
    EXPECT_EQ(0, totalVipPrefixesCount);

    okToSend3.post();
    okToCheck.wait();
    fiberSleepFor(2ms);
    /*
     * route counts going down again (by sending withdrawl)
     * received 1
     */
    EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCount());
    // accepted 1
    EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCount());

    okToSend4.post();
    okToCheck.wait();
    fiberSleepFor(2ms);
    /*
     * route counts going up again
     * received 2
     */
    EXPECT_EQ(2, adjRib_->getStats().getPreInPrefixCount());
    // accepted 2
    EXPECT_EQ(2, adjRib_->getStats().getPostInPrefixCount());

    terminateAdjRib();
  });
  evb_.loop();
}

// Verify if we set both pre & post limit we still function correctly
TEST_F(AdjRibInboundFixture, CheckLimitAndAlarmEnableBothTest) {
  // preMax = 5, postmax = 3, send 4 prefixes
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      true, // do not establish
      kLocalAs1, // global as
      kLocalAs1, // local as
      kRemoteAs1,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      nullptr, // policy manager, not used
      std::nullopt, // ingress policy name, not used
      kIsConfedPeerFalse,
      std::nullopt, // local confed as, not used
      std::nullopt, // as confed id, not used
      AdvertiseLinkBandwidth::DISABLE, // advertiseLinkBandwidth, not
                                       // used
      ReceiveLinkBandwidth::ACCEPT, // receiveLinkBandwidth
      std::nullopt, // linkBandwidthBps
      ValidateRemoteAs{true}, // not used
      5, // maxRoutes
      true, // warningOnly
      0, // warningLimit, not used
      2, // maxAcceptedRoutes
      true, // acptWarningOnly
      0 // acptWarningLimit, not used
  );

  const std::vector<folly::CIDRNetwork> inputPrefixSet{kV4Prefix1, kV6Prefix1};

  folly::fibers::Baton okToSend1;
  folly::fibers::Baton okToCheck;

  folly::fibers::Baton okToSend2;
  folly::fibers::Baton okToCheck2;

  fm_->addTask([&] {
    okToSend1.wait();
    auto updates = createV4AndV6BgpUpdateMultipleAnnounce(inputPrefixSet);
    adjRibInQ_->fiberPush(std::move(updates));
    okToCheck.post();
  });

  fm_->addTask([&] {
    okToSend2.wait();
    auto modifiedUpdate =
        createV4AndV6BgpUpdateMultipleAnnounce(inputPrefixSet);
    modifiedUpdate->attrs()->localPref() = 80;
    adjRibInQ_->fiberPush(std::move(modifiedUpdate));
    okToCheck2.post();
  });

  fm_->addTask([&] {
    okToSend1.post();
    okToCheck.wait();
    fiberSleepFor(2ms);
    /*
     * route counts going up
     * received 4, nothing got dropped because we set max to be 5
     */
    EXPECT_EQ(2, adjRib_->getStats().getPreInPrefixCount());
    // accepted 2
    EXPECT_EQ(2, adjRib_->getStats().getPostInPrefixCount());

    okToSend2.post();
    okToCheck2.wait();
    fiberSleepFor(2ms);

    // both counter should be as same before since it is just update
    EXPECT_EQ(2, adjRib_->getStats().getPreInPrefixCount());
    // accepted 2
    EXPECT_EQ(2, adjRib_->getStats().getPostInPrefixCount());

    terminateAdjRib();
  });
  evb_.loop();
}

// Verify if we set both pre & post limit we still function correctly
TEST_F(AdjRibInboundFixture, CheckRecCounterCorrectness) {
  // preMax = 5, postmax = 3, send 4 prefixes
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      true, // do not establish
      kLocalAs1, // global as
      kLocalAs1, // local as
      kRemoteAs1,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      nullptr, // policy manager, not used
      std::nullopt, // ingress policy name, not used
      kIsConfedPeerFalse,
      std::nullopt, // local confed as, not used
      std::nullopt, // as confed id, not used
      AdvertiseLinkBandwidth::DISABLE, // advertiseLinkBandwidth, not
                                       // used
      ReceiveLinkBandwidth::ACCEPT, // receiveLinkBandwidth
      std::nullopt, // linkBandwidthBps
      ValidateRemoteAs{true}, // not used
      10, // maxRoutes
      true, // warningOnly
      0, // warningLimit, not used
      2, // maxAcceptedRoutes
      true, // acptWarningOnly
      0 // acptWarningLimit, not used
  );

  const std::vector<folly::CIDRNetwork> inputPrefixSet{
      kV4Prefix1, kV6Prefix1, kV4Prefix2, kV6Prefix2};
  folly::fibers::Baton okToSend1;
  folly::fibers::Baton okToCheck;

  fm_->addTask([&] {
    okToSend1.wait();
    auto updates = createV4AndV6BgpUpdateMultipleAnnounce(inputPrefixSet);
    adjRibInQ_->fiberPush(std::move(updates));
    okToCheck.post();
  });

  fm_->addTask([&] {
    okToSend1.post();
    okToCheck.wait();
    fiberSleepFor(2ms);
    /*
     * route counts going up
     * received 4, nothing got dropped because we set max to be 5
     */
    EXPECT_EQ(4, adjRib_->getStats().getPreInPrefixCount());
    // accepted 2
    EXPECT_EQ(2, adjRib_->getStats().getPostInPrefixCount());

    terminateAdjRib();
  });
  evb_.loop();
}

// Verify we do shutdown peer when exceed max limit
TEST_F(AdjRibInboundFixture, CheckLimitAndAlarmNoWarningOnlyTest) {
  /*
   * send 4 prefixes
   * max = 3, warning-limit not used, warning-only false
   */
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      true, // do not establish
      kLocalAs1, // global as
      kLocalAs1, // local as
      kRemoteAs1,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      nullptr, // policy manager, not used
      std::nullopt, // ingress policy name, not used
      kIsConfedPeerFalse,
      std::nullopt, // local confed as, not used
      std::nullopt, // as confed id, not used
      AdvertiseLinkBandwidth::DISABLE, // advertiseLinkBandwidth, not
                                       // used
      ReceiveLinkBandwidth::ACCEPT, // receiveLinkBandwidth
      std::nullopt, // linkBandwidthBps
      ValidateRemoteAs{true}, // not used
      3 // maxRoutes
  );

  const std::vector<folly::CIDRNetwork> inputPrefixSet{
      kV4Prefix1, kV6Prefix1, kV4Prefix2, kV6Prefix2};

  fm_->addTask([&] {
    auto updates = createV4AndV6BgpUpdateMultipleAnnounce(inputPrefixSet);
    adjRibInQ_->fiberPush(std::move(updates));
  });

  fm_->addTask([&] {
    fiberSleepFor(10ms);
    /*
     * Verify stats
     * received 3
     */
    EXPECT_EQ(3, adjRib_->getStats().getPreInPrefixCount());
    // accepted 3
    EXPECT_EQ(3, adjRib_->getStats().getPostInPrefixCount());

    /*
     * The 4th prefix is dropped by the per-peer route cap. No switch-limit /
     * overload-protection config is set here, so this verifies droppedPrefixes
     * is populated even when NOT in overload protection mode. See S676351.
     */
    EXPECT_EQ(
        1,
        fb303::ThreadCachedServiceData::get()->getCounter(
            PeerStats::kTotalPrefixesDroppedByLimit));

    // verify ShutDownPeer message is sent
    EXPECT_EQ(1, fromAdjRibQ_.size());
    auto msg = folly::coro::blockingWait(fromAdjRibQ_.pop());
    ASSERT_TRUE(std::holds_alternative<AdjRib::Shutdown>(msg.message));

    terminateAdjRib();
  });
  evb_.loop();
}

// Test case for parameterized tests.
struct Param {
  using TupleT = std::tuple<thrift::OverloadProtectionMode, bool, bool>;

  explicit Param(const TupleT& t)
      : overloadProtectionMode(std::get<0>(t)),
        isSafeModeOn(std::get<1>(t)),
        hasGoldenPrefixPolicy(std::get<2>(t)) {}

  thrift::OverloadProtectionMode overloadProtectionMode;
  bool isSafeModeOn;
  bool hasGoldenPrefixPolicy;

 private:
  // Get a nice human-readable name for each test case
  friend std::ostream& operator<<(std::ostream& output, const Param& test) {
    auto protectionMode = test.overloadProtectionMode ==
            thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY
        ? "APPLY_GOLDEN_PREFIX_POLICY"
        : "DROP_EXCESS_PREFIXES";
    auto safeMode = test.isSafeModeOn ? "SafeModeOn" : "SafeModeOff";
    auto goldenPolicy = test.hasGoldenPrefixPolicy
        ? "WithGoldenPrefixPolicy"
        : "WithoutGoldenPrefixPolicy";
    output << fmt::format("{}_{}_{}", protectionMode, safeMode, goldenPolicy);
    return output;
  }
};

class FallbackToSwitchPrefixLimit : public AdjRibInboundFixture,
                                    public WithParamInterface<Param> {};

TEST_P(FallbackToSwitchPrefixLimit, Test) {
  setupAdjRib(
      kShortGrRestartTime, /* localGrRestartTime */
      std::nullopt, /* remoteGrRestartTime */
      false /* no establish session call */);

  auto switchLimitConfig = std::make_shared<thrift::BgpSwitchLimitConfig>();
  switchLimitConfig->prefix_limit() = 1;
  switchLimitConfig->overload_protection_mode() =
      GetParam().overloadProtectionMode;
  adjRib_->switchLimitConfig_ = switchLimitConfig;

  if (GetParam().hasGoldenPrefixPolicy) {
    // Set golden prefix policy with a high subnet limit we won't reach
    auto thriftPolicy = createTGoldenPrefixPolicy(
        {kV4Prefix1}, 1000 /* maxSubnets */, {32} /* allowedMaskLengths */);
    auto policy = std::make_shared<GoldenPrefixPolicy>(thriftPolicy);
    adjRib_->setGoldenPrefixPolicy(policy);
  }

  if (GetParam().isSafeModeOn) {
    adjRib_->setSafeModeOn();
  }

  folly::CIDRNetwork goldenV4Prefix1{"8.0.0.0", 32};
  folly::CIDRNetwork goldenV4Prefix2{"8.0.0.1", 32};
  BgpPath attrs; // empty

  // Add 1 prefix to reach the switch prefix_limit.
  AdjRibPrefixSet::get()->addPrefix(goldenV4Prefix1, false);

  // Don't drop known/existing prefix.
  EXPECT_FALSE(adjRib_->dropPrefixForOverloadProtection(
      0 /* totalPathCount */, goldenV4Prefix1, attrs, switchLimitConfig));

  // Drop new prefix.
  EXPECT_TRUE(adjRib_->dropPrefixForOverloadProtection(
      0 /* totalPathCount */, goldenV4Prefix2, attrs, switchLimitConfig));
}

INSTANTIATE_TEST_SUITE_P(
    FallbackToSwitchPrefixLimit,
    FallbackToSwitchPrefixLimit,
    // Generate all possible test cases
    ConvertGenerator<Param::TupleT>(Combine(
        Values(
            thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY,
            thrift::OverloadProtectionMode::DROP_EXCESS_PREFIXES),
        Bool() /* isSafeModeOn */,
        Bool() /* hasGoldenPrefixPolicy */)),
    PrintToStringParamName());

} // namespace facebook::bgp
