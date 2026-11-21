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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#define RibBase_TEST_FRIENDS                                                  \
  FRIEND_TEST(RibFixtureAddPathTestSuite, FromFibMessageLoop);                \
  FRIEND_TEST(FibOutRibFixture, FibOutPrefixReturnsOnlyExactMatch);           \
  FRIEND_TEST(FibOutRibFixture, ChangesAtPlatformStagingBoundary);            \
  FRIEND_TEST(                                                                \
      RibFixture, RibAnnouncementDuringPauseBestPathAndFibProgrammingTest);   \
  FRIEND_TEST(                                                                \
      RibFixture, RibWithdrawalDuringPauseBestPathAndFibProgrammingTest);     \
  FRIEND_TEST(                                                                \
      RibFixture, MultiplePauseAndResumeBestPathAndFibProgrammingTest);       \
  FRIEND_TEST(RibFixture, PauseAndResumeBestPathAndFibProgrammingTest);       \
  FRIEND_TEST(RibFixture, DefaultStateAndPauseBestPathAndFibProgrammingTest); \
  FRIEND_TEST(RibFixture, FibSyncReqWhilePausedTest);                         \
  FRIEND_TEST(RibFixture, FibFlushedCounterTest);

#include "neteng/fboss/bgp/cpp/BgpServiceUtil.h"
#include "neteng/fboss/bgp/cpp/rib/RibBase.h"
#include "neteng/fboss/bgp/cpp/rib/Utils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibUtils.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"

using namespace facebook::bgp;
using namespace facebook::neteng::fboss::bgp_attr;
using namespace facebook::nettools::bgplib;
using namespace std::chrono;
using folly::IPAddress;
using folly::Promise;
using folly::Unit;
using ::testing::_;
using ::testing::Eq;
using ::testing::InSequence;

namespace facebook {
namespace bgp {

INSTANTIATE_TEST_SUITE_P(
    RibFibInteraction,
    RibFixtureAddPathTestSuite,
    testing::Values(true /* addPath */));

class FibOutRibFixture : public RibFixture {
 protected:
  /** Start a RIB with FIB-out tracking enabled. */
  void SetUp() override {
    ribFixtureDefaultSetup(
        ComputeUcmpFromLbwComm{true},
        CountConfedsInAsPathLen{false},
        EnableNexthopTracking{false},
        nullptr,
        true);
  }
};

TEST_F(RibFixture, FibOutPrefixReportsTrackingDisabled) {
  const auto table =
      rib_->getFibOutPrefix(folly::IPAddress::networkToString(kV4Prefix1));

  EXPECT_FALSE(*table.enabled());
  EXPECT_TRUE(table.entries()->empty());
}

TEST_F(RibFixture, FibOutPrefixRejectsInvalidInputWhenTrackingDisabled) {
  EXPECT_THROW(rib_->getFibOutPrefix("invalid"), std::invalid_argument);
}

TEST_F(FibOutRibFixture, FibOutPrefixReturnsOnlyExactMatch) {
  const PrefixPathId prefixPathId{kV4Prefix1, kDefaultPathID};
  EXPECT_CALL(*rib_, prepareFibProgramming_());
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _));
  EXPECT_CALL(*fib_, program_(true));

  auto fibFuture = fib_->getFibProgramFuture();
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->processSingleRibInUpdateForTest(eBgpPeer1_, attr_, prefixPathId);
    rib_->prepareFibProgramming(fboss::kDefaultVrf, true);
  });
  fibFuture.wait();

  const auto present =
      rib_->getFibOutPrefix(folly::IPAddress::networkToString(kV4Prefix1));
  facebook::neteng::fboss::bgp::thrift::TFibOutNextHop expectedNexthop;
  expectedNexthop.next_hop() = buildTPrefix({kV4Nexthop1, 32});
  expectedNexthop.weight() = 0;
  expectedNexthop.role() =
      facebook::neteng::fboss::bgp::thrift::TFibOutNextHopRole::PRIMARY;
  facebook::neteng::fboss::bgp::thrift::TFibOutRoute expectedRoute;
  expectedRoute.operation() =
      facebook::neteng::fboss::bgp::thrift::TFibOutOperation::PROGRAM;
  expectedRoute.next_hops() = {std::move(expectedNexthop)};
  expectedRoute.nexthop_set_ref_count() = 1;
  facebook::neteng::fboss::bgp::thrift::TFibOutEntry expectedEntry;
  expectedEntry.prefix() = buildTPrefix(kV4Prefix1);
  expectedEntry.fib_out() = std::move(expectedRoute);
  EXPECT_EQ(
      *present.entries(),
      std::vector<facebook::neteng::fboss::bgp::thrift::TFibOutEntry>{
          std::move(expectedEntry)});

  const auto presentWithHostBits = rib_->getFibOutPrefix("8.0.0.42/24");
  EXPECT_EQ(*present.entries(), *presentWithHostBits.entries());

  const auto absent = rib_->getFibOutPrefix("192.0.2.0/24");
  EXPECT_TRUE(*absent.enabled());
  EXPECT_TRUE(absent.entries()->empty());
}

TEST_F(FibOutRibFixture, FibOutPrefixRejectsInvalidInput) {
  EXPECT_THROW(rib_->getFibOutPrefix("invalid"), std::invalid_argument);
}

TEST_F(FibOutRibFixture, ChangesAtPlatformStagingBoundary) {
  const PrefixPathId prefixPathId{kV4Prefix1, kDefaultPathID};
  auto firstAttrs = attr_->clone();
  firstAttrs->setNexthop(kV4Nexthop1);
  firstAttrs->publish();
  const FibNexthopSet firstExpected{{
      .address = kV4Nexthop1,
      .weight = 0,
      .role = FibOutNexthopRole::PRIMARY,
  }};

  auto secondAttrs = attr_->clone();
  secondAttrs->setNexthop(kV4Nexthop2);
  secondAttrs->publish();
  const FibNexthopSet secondExpected{{
      .address = kV4Nexthop2,
      .weight = 0,
      .role = FibOutNexthopRole::PRIMARY,
  }};

  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(2);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
      .Times(2);
  {
    InSequence sequence;
    EXPECT_CALL(*fib_, program_(true)).WillOnce([this, &firstExpected](bool) {
      const auto& entry = rib_->defaultVrf_.ribEntries().at(kV4Prefix1);
      EXPECT_FALSE(entry.isOnFibBatchList());
      const auto* fibOut = rib_->findFibOut(entry);
      ASSERT_NE(fibOut, nullptr);
      EXPECT_EQ(*fibOut->nexthops, firstExpected);
      EXPECT_EQ(fibOut->metadata, FibOutRouteMetadata::program());
      const auto table =
          rib_->getFibOutPrefix(folly::IPAddress::networkToString(kV4Prefix1));
      ASSERT_TRUE(*table.enabled());
      ASSERT_EQ(table.entries()->size(), 1);
      ASSERT_TRUE(table.entries()->front().fib_out().has_value());
      EXPECT_FALSE(table.entries()->front().fib_out_pending().has_value());
      EXPECT_EQ(
          *table.entries()->front().fib_out()->nexthop_set_ref_count(), 1);
      const auto database = rib_->getFibNexthopDatabase();
      ASSERT_EQ(database.nexthop_sets()->size(), 1);
      ASSERT_EQ(database.nexthop_sets()->front().next_hops()->size(), 1);
      EXPECT_EQ(
          *database.nexthop_sets()->front().next_hops()->front().next_hop(),
          createTIpPrefix(kV4Nexthop1));
      EXPECT_EQ(*database.nexthop_sets()->front().ref_count(), 1);
    });
    EXPECT_CALL(*fib_, program_(false)).WillOnce([this, &secondExpected](bool) {
      const auto& entry = rib_->defaultVrf_.ribEntries().at(kV4Prefix1);
      EXPECT_FALSE(entry.isOnFibBatchList());
      const auto* fibOut = rib_->findFibOut(entry);
      ASSERT_NE(fibOut, nullptr);
      EXPECT_EQ(*fibOut->nexthops, secondExpected);
      EXPECT_EQ(fibOut->metadata, FibOutRouteMetadata::program());
      const auto table =
          rib_->getFibOutPrefix(folly::IPAddress::networkToString(kV4Prefix1));
      ASSERT_EQ(table.entries()->size(), 1);
      ASSERT_TRUE(table.entries()->front().fib_out().has_value());
      EXPECT_FALSE(table.entries()->front().fib_out_pending().has_value());
      EXPECT_EQ(
          *table.entries()->front().fib_out()->nexthop_set_ref_count(), 1);
      const auto database = rib_->getFibNexthopDatabase();
      ASSERT_EQ(database.nexthop_sets()->size(), 1);
      ASSERT_EQ(database.nexthop_sets()->front().next_hops()->size(), 1);
      EXPECT_EQ(
          *database.nexthop_sets()->front().next_hops()->front().next_hop(),
          createTIpPrefix(kV4Nexthop2));
      EXPECT_EQ(*database.nexthop_sets()->front().ref_count(), 1);
    });
  }

  auto fibFuture = fib_->getFibProgramFuture();
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->processSingleRibInUpdateForTest(eBgpPeer1_, firstAttrs, prefixPathId);
    rib_->prepareFibProgramming(fboss::kDefaultVrf, true);
    const auto& entry = rib_->defaultVrf_.ribEntries().at(kV4Prefix1);
    EXPECT_TRUE(entry.isOnFibBatchList());
    EXPECT_EQ(rib_->findFibOut(entry), nullptr);
    const auto table =
        rib_->getFibOutPrefix(folly::IPAddress::networkToString(kV4Prefix1));
    ASSERT_EQ(table.entries()->size(), 1);
    EXPECT_FALSE(table.entries()->front().fib_out().has_value());
    EXPECT_FALSE(table.entries()->front().fib_out_pending().has_value());
    EXPECT_TRUE(rib_->getFibNexthopDatabase().nexthop_sets()->empty());
  });
  fibFuture.wait();

  fibFuture = fib_->getFibProgramFuture();
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->processSingleRibInUpdateForTest(
        eBgpPeer1_, secondAttrs, prefixPathId);
    rib_->prepareFibProgramming(fboss::kDefaultVrf, false);
    const auto& entry = rib_->defaultVrf_.ribEntries().at(kV4Prefix1);
    EXPECT_TRUE(entry.isOnFibBatchList());
    const auto* fibOut = rib_->findFibOut(entry);
    ASSERT_NE(fibOut, nullptr);
    EXPECT_EQ(*fibOut->nexthops, firstExpected);
    const auto table =
        rib_->getFibOutPrefix(folly::IPAddress::networkToString(kV4Prefix1));
    ASSERT_EQ(table.entries()->size(), 1);
    ASSERT_TRUE(table.entries()->front().fib_out().has_value());
    EXPECT_FALSE(table.entries()->front().fib_out_pending().has_value());
    EXPECT_EQ(*table.entries()->front().fib_out()->nexthop_set_ref_count(), 1);
    const auto database = rib_->getFibNexthopDatabase();
    ASSERT_EQ(database.nexthop_sets()->size(), 1);
    ASSERT_EQ(database.nexthop_sets()->front().next_hops()->size(), 1);
    EXPECT_EQ(
        *database.nexthop_sets()->front().next_hops()->front().next_hop(),
        createTIpPrefix(kV4Nexthop1));
  });
  fibFuture.wait();

  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    const auto& entry = rib_->defaultVrf_.ribEntries().at(kV4Prefix1);
    EXPECT_FALSE(entry.isOnFibBatchList());
    const auto* fibOut = rib_->findFibOut(entry);
    ASSERT_NE(fibOut, nullptr);
    EXPECT_EQ(*fibOut->nexthops, secondExpected);
  });

  const auto table =
      rib_->getFibOutPrefix(folly::IPAddress::networkToString(kV4Prefix1));
  ASSERT_TRUE(*table.enabled());
  ASSERT_EQ(table.entries()->size(), 1);
  ASSERT_TRUE(table.entries()->front().fib_out().has_value());
  EXPECT_FALSE(table.entries()->front().fib_out_pending().has_value());
}

TEST_P(RibFixtureAddPathTestSuite, FromFibMessageLoop) {
  auto& inputQ = rib_->fromFibMessageQ_;

  // populate ribEntries_ with non-local routes
  RibEntry entry(kV4Prefix1);
  entry.updatePath(eBgpPeer1_, attr_, false);
  RibBase::selectBestPath(entry, multipathSelector, bestpathSelector, false, 0);
  RibEntry entry2(kV6Prefix1);
  entry2.updatePath(eBgpPeer1_, attr_, false);
  RibBase::selectBestPath(
      entry2, multipathSelector, bestpathSelector, false, 0);
  {
    /*
     * initialise a FibProgrammedMessage with same route in ribEntries
     * positive test case, adding an announcement after fib is programmed
     */
    folly::F14NodeMap<
        folly::CIDRNetwork,
        std::shared_ptr<const WeightedNexthopMap>>
        prefixToNexthops = {{kV4Prefix1, entry.getMultipathWeightedNexthops()}};
    folly::F14FastMap<
        std::shared_ptr<const BgpPath>,
        folly::F14NodeMap<
            folly::CIDRNetwork,
            std::shared_ptr<const WeightedNexthopMap>>>
        FibProgrammedPfxs;
    FibProgrammedPfxs[nullptr] = prefixToNexthops;

    Fib::FibProgrammedMessage fibMsg(
        fboss::kDefaultVrf, FibProgrammedPfxs, false /* fullSync */);
    rib_->defaultVrf_.ribEntries().emplace(kV4Prefix1, entry);
    inputQ.push(std::move(fibMsg));

    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);

    ASSERT_EQ(1, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
    EXPECT_EQ(kV4Prefix1, announcement.entries[0].prefix);
    ASSERT_EQ(GetParam() ? 1 : 0, announcement.addPathEntries.size());
    if (GetParam()) {
      EXPECT_EQ(kMinPathIDToSend, announcement.addPathEntries[0].pathIdToSend);
      EXPECT_EQ(kV4Prefix1, announcement.addPathEntries[0].prefix);
      EXPECT_EQ(
          kV4Nexthop1, announcement.addPathEntries[0].attrs->getNexthop());
    }
    EXPECT_EQ(false, announcement.initialDump);
    EXPECT_EQ(false, announcement.sendWithEoR);

    // expect no more announcement
    REPEAT_N(5, { EXPECT_EQ(0, ribOutQ_.size()); });
  }
  {
    /*
     * initialise a FibProgrammedMessage with null nexthops
     * withdrawl, not adding announcement
     */
    folly::F14NodeMap<
        folly::CIDRNetwork,
        std::shared_ptr<const WeightedNexthopMap>>
        prefixToNexthops = {{kV4Prefix1, nullptr}};
    folly::F14FastMap<
        std::shared_ptr<const BgpPath>,
        folly::F14NodeMap<
            folly::CIDRNetwork,
            std::shared_ptr<const WeightedNexthopMap>>>
        FibProgrammedPfxs;
    FibProgrammedPfxs[nullptr] = prefixToNexthops;

    Fib::FibProgrammedMessage msg(
        fboss::kDefaultVrf, FibProgrammedPfxs, false /* fullSync */);
    inputQ.push(std::move(msg));

    // expect 0 element in the ribOutQ_
    REPEAT_N(5, { EXPECT_EQ(0, ribOutQ_.size()); });
  }
  {
    /*
     * initialise a FibProgrammedMessage with prefix not in ribEntries_
     * not adding announcement
     */
    folly::F14NodeMap<
        folly::CIDRNetwork,
        std::shared_ptr<const WeightedNexthopMap>>
        prefixToNexthops = {
            {kV4Prefix2, std::make_shared<const WeightedNexthopMap>()}};
    folly::F14FastMap<
        std::shared_ptr<const BgpPath>,
        folly::F14NodeMap<
            folly::CIDRNetwork,
            std::shared_ptr<const WeightedNexthopMap>>>
        FibProgrammedPfxs;
    FibProgrammedPfxs[nullptr] = prefixToNexthops;

    Fib::FibProgrammedMessage msg(
        fboss::kDefaultVrf, FibProgrammedPfxs, false /* fullSync */);
    inputQ.push(std::move(msg));

    // expect 0 element in the ribOutQ_
    REPEAT_N(5, { EXPECT_EQ(0, ribOutQ_.size()); });
  }
  {
    /*
     * initialise a FibProgrammedMessage with MultipathNexthops() != nexthops
     * not adding announcement
     */
    folly::F14NodeMap<
        folly::CIDRNetwork,
        std::shared_ptr<const WeightedNexthopMap>>
        prefixToNexthops = {
            {kV4Prefix1, std::make_shared<WeightedNexthopMap>()}};
    folly::F14FastMap<
        std::shared_ptr<const BgpPath>,
        folly::F14NodeMap<
            folly::CIDRNetwork,
            std::shared_ptr<const WeightedNexthopMap>>>
        FibProgrammedPfxs;
    FibProgrammedPfxs[nullptr] = prefixToNexthops;

    Fib::FibProgrammedMessage msg(
        fboss::kDefaultVrf, FibProgrammedPfxs, false /* fullSync */);
    inputQ.push(std::move(msg));

    // expect 0 element in the ribOutQ_
    REPEAT_N(5, { EXPECT_EQ(0, ribOutQ_.size()); });
  }

  {
    /*
     * adding another path for entry. expecting to have both announced for
     * add path
     */
    auto newAttr = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    newAttr->setNexthop(kV4Nexthop2);
    newAttr->publish();
    entry.updatePath(eBgpPeer2_, newAttr, false);
    RibBase::selectBestPath(
        entry, multipathSelector, bestpathSelector, false, 0);
    /*
     * initialise a FibProgrammedMessage without EoR
     * positive test case, adding an announcement after fib is programmed
     */
    folly::F14NodeMap<
        folly::CIDRNetwork,
        std::shared_ptr<const WeightedNexthopMap>>
        prefixToNexthops = {{kV4Prefix1, entry.getMultipathWeightedNexthops()}};
    folly::F14FastMap<
        std::shared_ptr<const BgpPath>,
        folly::F14NodeMap<
            folly::CIDRNetwork,
            std::shared_ptr<const WeightedNexthopMap>>>
        FibProgrammedPfxs;
    FibProgrammedPfxs[nullptr] = prefixToNexthops;

    rib_->defaultVrf_.ribEntries().clear();
    rib_->defaultVrf_.ribEntries().emplace(
        make_pair(kV4Prefix1, std::move(entry)));
    Fib::FibProgrammedMessage fibMsg(
        fboss::kDefaultVrf, FibProgrammedPfxs, false /* fullSync */);
    inputQ.push(std::move(fibMsg));

    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    ASSERT_EQ(1, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
    ASSERT_EQ(GetParam() ? 2 : 0, announcement.addPathEntries.size());
    if (GetParam()) {
      checkRibOutEntriesAddPathIds(announcement);
      // one entry should have nh1, one should have nh2, doesn't matter which
      auto entry1 = announcement.addPathEntries[0];
      auto addPathEntry2 = announcement.addPathEntries[1];
      EXPECT_TRUE(
          entry1.attrs->getNexthop() == kV4Nexthop1 ||
          addPathEntry2.attrs->getNexthop() == kV4Nexthop1);
      EXPECT_TRUE(
          entry1.attrs->getNexthop() == kV4Nexthop2 ||
          addPathEntry2.attrs->getNexthop() == kV4Nexthop2);
    }

    // expect no more announcement
    REPEAT_N(5, { EXPECT_EQ(0, ribOutQ_.size()); });

    /*
     * updating bgp attributes (asPathLength) for one of multipath.
     * as a result the multinexthop is gonna change.
     * expecting to send one announcement and one withdraw
     */
    auto newAttr2 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(5, 4, 4, 4));
    newAttr2->setNexthop(kV4Nexthop2);
    newAttr2->publish();
    rib_->defaultVrf_.ribEntries()
        .find(kV4Prefix1)
        ->second.updatePath(eBgpPeer2_, newAttr2, false);
    RibBase::selectBestPath(
        rib_->defaultVrf_.ribEntries().find(kV4Prefix1)->second,
        multipathSelector,
        bestpathSelector,
        false,
        0);

    prefixToNexthops = {
        {kV4Prefix1,
         rib_->defaultVrf_.ribEntries()
             .find(kV4Prefix1)
             ->second.getMultipathWeightedNexthops()}};
    FibProgrammedPfxs[nullptr] = prefixToNexthops;

    Fib::FibProgrammedMessage fibMsg2(
        fboss::kDefaultVrf, FibProgrammedPfxs, false /* fullSync */);
    inputQ.push(std::move(fibMsg2));

    if (GetParam()) {
      // expect one withdraw
      msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
      ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
      auto withdrawal = std::get<RibOutWithdrawal>(msg);
      ASSERT_EQ(1, withdrawal.addPathEntries.size());
      EXPECT_EQ(kPlaceholderPathID, withdrawal.addPathEntries[0].pathIdToSend);
      EXPECT_EQ(kV4Nexthop2, withdrawal.addPathEntries[0].nh.value());
    }

    // expect one announcement
    msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement3 = std::get<RibOutAnnouncement>(msg);
    ASSERT_EQ(1, announcement3.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement3.entries[0].pathIdToSend);
    ASSERT_EQ(GetParam() ? 1 : 0, announcement3.addPathEntries.size());
    if (GetParam()) {
      /*
       * the path not withdrawn is re-announced. minId and minId+1 (now
       * withdrawn) were the previously announced paths, so it must be minId
       * that's re-announced
       */
      EXPECT_EQ(kMinPathIDToSend, announcement3.addPathEntries[0].pathIdToSend);
      EXPECT_EQ(
          kV4Nexthop1, announcement3.addPathEntries[0].attrs->getNexthop());
    }

    // expect no more announcement
    REPEAT_N(5, { EXPECT_EQ(0, ribOutQ_.size()); });
  }
  {
    /*
     * adding another entry for testing because "entry" is announced
     * initialise a FibProgrammedMessage with send EoR
     * positive test case, adding an announcement after fib is programmed
     */
    folly::F14NodeMap<
        folly::CIDRNetwork,
        std::shared_ptr<const WeightedNexthopMap>>
        prefixToNexthops = {
            {kV6Prefix1, entry2.getMultipathWeightedNexthops()}};
    folly::F14FastMap<
        std::shared_ptr<const BgpPath>,
        folly::F14NodeMap<
            folly::CIDRNetwork,
            std::shared_ptr<const WeightedNexthopMap>>>
        FibProgrammedPfxs;
    FibProgrammedPfxs[nullptr] = prefixToNexthops;

    Fib::FibProgrammedMessage fibMsg(
        fboss::kDefaultVrf, FibProgrammedPfxs, true /* fullSync */);
    rib_->defaultVrf_.ribEntries().emplace(kV6Prefix1, entry2);
    inputQ.push(std::move(fibMsg));

    // Expect RibInitialAnnouncementStart before initial announcement.
    auto ribInitialAnnouncementStart =
        folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(
        std::holds_alternative<RibInitialAnnouncementStart>(
            ribInitialAnnouncementStart));

    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    ASSERT_EQ(1, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
    EXPECT_EQ(kV6Prefix1, announcement.entries[0].prefix);
    ASSERT_EQ(GetParam() ? 1 : 0, announcement.addPathEntries.size());
    if (GetParam()) {
      /*
       * only one path for ribEntry2 has been sent, hence its allocated pathId
       * is the min ID
       */
      EXPECT_EQ(kMinPathIDToSend, announcement.addPathEntries[0].pathIdToSend);
      EXPECT_EQ(kV6Prefix1, announcement.addPathEntries[0].prefix);
    }
    EXPECT_EQ(true, announcement.initialDump);
    EXPECT_EQ(true, announcement.sendWithEoR);

    // expect no more announcement
    REPEAT_N(5, { EXPECT_EQ(0, ribOutQ_.size()); });
  }
}

TEST_F(RibFixture, RibAnnouncementDuringPauseBestPathAndFibProgrammingTest) {
  auto prefixBatch1 = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto prefixBatch2 = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};

  // Expect 1 call for Fib programming during Initial EOR.
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
  EXPECT_CALL(*fib_, program_(true)).Times(1);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
      .Times(0);
  EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV6Prefix1), _, _, _, _, _))
      .Times(0);

  auto fibFuture = fib_->getFibProgramFuture();
  // Step 1: Send EoR
  sendInitialPathComputation();

  // Step 2: Pause best path computation and fib programming
  sendPauseBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE);

  // Step 3: Send 2 announcements to rib
  sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
  sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);

  fibFuture.wait();
}

/**
 * This tests Rib withdrawals sent when when local RIB thread operations such
 * as best path computation and Fib programming are paused. After the best path
 * computation and Fib programming are resumed, the withdrawn prefixes are
 * indeed removed from RIB
 */
TEST_F(RibFixture, RibWithdrawalDuringPauseBestPathAndFibProgrammingTest) {
  auto prefixBatch1 = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto prefixBatch2 = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  {
    auto fibFuture = fib_->getFibProgramFuture();
    // Step 1: Send EoR. Wait for Fib programming to send this to RibOut
    sendInitialPathComputation();
    fibFuture.wait();

    /*
     * Step 2: Send 1 announcement before pause, and again wait for Fib
     * programming
     */
    auto fibFuture2 = fib_->getFibProgramFuture();
    sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
    fibFuture2.wait();

    /*
     * Expect RibInitialAnnouncementStart, first RibOutAnnouncement (no entry
     * because EOR sent with nothing in Rib), and second RibOutAnnouncement (has
     * an entry for prefixBatch1)
     */
    WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(ribOutQ_.size(), 3); });
    // Step 3: Send PauseBestPathAndFibProgramming message to rib
    sendPauseBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE);

    /*
     * Step 4: Send another announcement to rib during the pause period, the
     * announcement will be in RIB only
     */
    sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);

    // this is the programming triggered by sendInitialPathComputation
    WITH_RETRIES(
        { ASSERT_EVENTUALLY_TRUE(isBestPathAndFibProgrammingPaused()); });
  }
  {
    auto fibFuture3 = fib_->getFibProgramFuture();

    // Step 4: Send 1 withdrawal to rib
    sendWithdrawal(prefixBatch1, iBgpPeer_);

    // Step 5: Send ResumeBestPathAndFibProgramming message to rib
    sendResumeBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE);
    // this is the programming triggered by sendResumeBestPathAndFibProgramming
    fibFuture3.wait();
    /*
     * Must wait for FIB ack has been received and processed to avoid data race
     * Expect the 3 from before, plus RibOutWithdrawal (has an entry for
     * prefixBatch1), and third RibOutAnnouncement (has an entry for
     * prefixBatch2)
     */
    WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(ribOutQ_.size(), 5); });

    /*
     * Step 6: Verify that the withdrawn prefix is removed from RIB, and the
     * non-withdrawn prefix remains
     */
    WITH_RETRIES({
      rib_->evb_.runInEventBaseThreadAndWait([&]() {
        ASSERT_EVENTUALLY_TRUE(
            rib_->defaultVrf_.ribEntries().find(kV4Prefix1) ==
            rib_->defaultVrf_.ribEntries().end());
        EXPECT_TRUE(
            rib_->defaultVrf_.ribEntries().find(kV6Prefix1) !=
            rib_->defaultVrf_.ribEntries().end());
      });
    });
  }
}

/**
 * This test ensures receiving PauseBestPathAndFibProgramming and
 * ResumeBestPathAndFibProgramming from the same task name multiple times is
 * idempotent
 */
TEST_F(RibFixture, MultiplePauseAndResumeBestPathAndFibProgrammingTest) {
  auto prefixBatch1 = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto prefixBatch2 = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  // Case 1: Send multiple PauseBestPathAndFibProgramming messages
  {
    // Expect 1 call for Fib programming during Initial EOR.
    EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
    EXPECT_CALL(*fib_, program_(true)).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
        .Times(0);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV6Prefix1), _, _, _, _, _))
        .Times(0);
    auto fibFuture = fib_->getFibProgramFuture();
    // Step 1: Send EOR to simulate RIB in steady state
    sendInitialPathComputation();
    fibFuture.wait();

    // Step 2: Send PauseBestPathAndFibProgramming message to rib
    sendPauseBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE);

    /*
     * Step 3: Send PauseBestPathAndFibProgramming message to rib, this will
     * be ignored
     */
    sendPauseBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE);

    // Step 4: Send 2 announcements to rib, which won't be programmed to fib
    sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
    sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);

    WITH_RETRIES(
        { ASSERT_EVENTUALLY_TRUE(isBestPathAndFibProgrammingPaused()); });
  }
  /*
   * Case 2: Send multiple ResumeBestPathAndFibProgramming messages and verify
   * they are all ignored since best-path and fib programming was not paused
   */
  {
    // Expect no interuptions to Fib programming or unicast route updates
    EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(1);
    EXPECT_CALL(*fib_, program_(false)).Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV4Prefix1), _, _, _, _, _))
        .Times(1);
    EXPECT_CALL(*fib_, updateUnicastRoute_(Eq(kV6Prefix1), _, _, _, _, _))
        .Times(1);
    auto fibFuture = fib_->getFibProgramFuture();

    /*
     * Step 1: Send ResumeBestPathAndFibProgramming message to rib, this will
     * not be ignored
     */
    sendResumeBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE);

    /*
     * Step 2: Send ResumeBestPathAndFibProgramming message to rib, this will
     * be ignored
     */
    sendResumeBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE);

    /*
     * Step 3: Send 2 announcements to Rib. Since there is no attrs updates,
     * these announcements will be ignored
     */
    sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
    sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);
    fibFuture.wait();

    EXPECT_FALSE(isBestPathAndFibProgrammingPaused());
  }
}

/**
 * This test verifies RibAnnouncements and RibWithdrawals when local RIB
 * thread operations are paused and then resumed with SAFE_MODE
 */
TEST_F(RibFixture, PauseAndResumeBestPathAndFibProgrammingTest) {
  /*
   * Case 1: Send 1 RibAnnouncement when local Rib thread operations are
   * paused and 1 RibAnnouncement when it is resumed
   */
  auto prefixBatch1 = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  {
    auto fibFuture = fib_->getFibProgramFuture();
    // Step 1: Send EOR to simulate RIB in steady state
    sendInitialPathComputation();

    // Step 2: Send PauseBestPathAndFibProgramming message to rib
    sendPauseBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE);

    // Step 3: Send 1 announcement when local RIB thread is paused
    sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);

    // Step 4: Send withdrawal of the same prefix
    sendWithdrawal(prefixBatch1, iBgpPeer_);

    // Step 5: Resume best path computation and fib programming
    sendResumeBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE);

    fibFuture.wait();
    EXPECT_TRUE(
        rib_->defaultVrf_.ribEntries().find(kV4Prefix1) ==
        rib_->defaultVrf_.ribEntries().end());

    fibFuture = fib_->getFibProgramFuture();
    // Step 6: Send PauseBestPathAndFibProgramming message to rib
    sendPauseBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE);

    // Step 7: Announce same prefix batch again
    sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);

    // Step 8: Resume best path computation and fib programming
    sendResumeBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE);

    // Step 9: Send 1 another announcement
    auto prefixBatch2 = PrefixPathIds{{kV6Prefix2, kDefaultPathID}};
    sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);

    fibFuture.wait();

    /*
     * Test expects to see RibInitialAnnouncementStart and both the
     * announcements
     */
    WITH_RETRIES({ ASSERT_EVENTUALLY_TRUE(ribOutQ_.size() == 4); });
    // RibInitialAnnouncementStart
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));

    std::vector<RibOutAnnouncement> rcvdAnnouncements;

    // Announcement 1
    msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    rcvdAnnouncements.emplace_back(std::get<RibOutAnnouncement>(msg));

    // Announcement 2
    msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    rcvdAnnouncements.emplace_back(std::get<RibOutAnnouncement>(msg));

    EXPECT_EQ(2, rcvdAnnouncements.size());
    // Check first announcement
    EXPECT_TRUE(rcvdAnnouncements[0].sendWithEoR);
    EXPECT_TRUE(rcvdAnnouncements[0].initialDump);
    // Check second announcement
    EXPECT_FALSE(rcvdAnnouncements[1].sendWithEoR);
    EXPECT_FALSE(rcvdAnnouncements[1].initialDump);

    // pop last message out
    msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
  }
  /*
   * Case 2: Send 1 RibAnnouncement when local Rib thread operations are
   * paused and 1 RibWithdrawal when it is resumed
   */
  {
    auto fibFuture = fib_->getFibProgramFuture();

    // Step 1: Send PauseBestPathAndFibProgramming message to rib
    sendPauseBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE);

    // Step 2: Send 1 announcement when local RIB thread is paused
    sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);

    // Step 3: Send ResumeBestPathAndFibProgramming message to rib
    sendResumeBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE);

    // Test expects to see 1 announcement
    WITH_RETRIES({ ASSERT_EVENTUALLY_TRUE(ribOutQ_.size() == 0); });

    // Step 5: Send withdrawal of the announced prefix
    sendWithdrawal(prefixBatch1, iBgpPeer_);

    fibFuture.wait();

    // Test expects to see 1 withdrawal
    WITH_RETRIES({ ASSERT_EVENTUALLY_TRUE(ribOutQ_.size() == 1); });
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
  }
}

/**
 * This test verifies RibAnnouncements and RibDumpReq in default
 * state(PauseBestPathAndFibProgramming_ is False) and after sending
 * PauseBestPathAndFibProgramming message.
 */
TEST_F(RibFixture, DefaultStateAndPauseBestPathAndFibProgrammingTest) {
  auto fibFuture = fib_->getFibProgramFuture();
  /*
   * Step 1: Send 2 announcements and EOR to simulate RIB in steady state
   * By default, PauseBestPathAndFibProgramming_ is False.
   */
  auto prefixBatch1 = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto prefixBatch2 = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  auto prefixBatch3 = PrefixPathIds{{kV4Prefix2, kDefaultPathID}};
  auto prefixBatch4 = PrefixPathIds{{kV6Prefix2, kDefaultPathID}};
  sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
  sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);
  sendInitialPathComputation();

  // Step 2: Send PauseBestPathAndFibProgramming message to rib
  sendPauseBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE);

  // Step 3: Send 2 more announcements to rib
  sendAnnouncement(prefixBatch3, iBgpPeer_, attr_);
  sendAnnouncement(prefixBatch4, iBgpPeer_, attr_);

  fibFuture.wait();

  /*
   * Test expects to see RibInitialAnnouncementStart and then
   * only one announcement (One before local RIB thread was paused).
   */
  WITH_RETRIES({ ASSERT_EVENTUALLY_TRUE(ribOutQ_.size() == 2); });
  auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
  ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));

  msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
  ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  auto announcement = std::get<RibOutAnnouncement>(msg);
  ASSERT_EQ(2, announcement.entries.size());
  EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
  EXPECT_EQ(kDefaultPathID, announcement.entries[1].pathIdToSend);
  EXPECT_TRUE(announcement.sendWithEoR);
  EXPECT_TRUE(announcement.initialDump);
}

/**
 * This test simulates FibSyncReq received while best path computation and Fib
 * programming are paused. Verifies that a fullSync is performed when operations
 * are resumed.
 */
TEST_F(RibFixture, FibSyncReqWhilePausedTest) {
  auto prefixBatch1 = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto prefixBatch2 = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};

  /*
   * Expect prepareFibProgramming called twice:
   * 1) During initial EOR
   * 2) During resume (with fullSync due to fibSyncReqPending_)
   */
  EXPECT_CALL(*rib_, prepareFibProgramming_()).Times(2);

  // Use InSequence to ensure program_ calls happen in the expected order
  {
    InSequence seq;
    // Expect 1st call for Initial EOR with fullSync=true
    EXPECT_CALL(*fib_, program_(true)).Times(1);
    // Expect 2nd call for resume with fullSync=true (due to fibSyncReqPending_)
    EXPECT_CALL(*fib_, program_(true)).Times(1);
  }

  // Step 1: Send EOR to simulate RIB in steady state
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  // Step 2: Send PauseBestPathAndFibProgramming message to rib
  sendPauseBestPathAndFibProgramming(RibPauseResumeCause::ROUTE_CHURN);

  // Verify that best path and Fib programming is paused
  WITH_RETRIES(
      { ASSERT_EVENTUALLY_TRUE(isBestPathAndFibProgrammingPaused()); });
  EXPECT_EQ(1, rib_->bestPathAndFibProgrammingPausedBy_.rlock()->size());

  // Step 3: Send some route announcements while paused
  sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
  sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);

  // Wait for ribInQ to be processed
  while (ribInQ_.size() != 0) {
  }

  // Step 4: Simulate FibAgent reconnection and request fullSync while paused
  rib_->fromFibMessageQ_.push(Fib::FibSyncReq{});

  // Verify fibSyncReqPending_ flag is set
  WITH_RETRIES({ ASSERT_EVENTUALLY_TRUE(rib_->fibSyncReqPending_); });

  /*
   * Step 5: Resume best path computation and fib programming
   * This should trigger prepareFibProgramming(true) because fibSyncReqPending_
   * is set
   */
  fibFuture = fib_->getFibProgramFuture();
  sendResumeBestPathAndFibProgramming(RibPauseResumeCause::ROUTE_CHURN);

  // Wait for fib programming to complete
  fibFuture.wait();

  // Verify that best path and Fib programming is resumed
  WITH_RETRIES(
      { ASSERT_EVENTUALLY_FALSE(isBestPathAndFibProgrammingPaused()); });
  EXPECT_EQ(0, rib_->bestPathAndFibProgrammingPausedBy_.rlock()->size());

  // Verify fibSyncReqPending_ flag is cleared after resume
  EXPECT_FALSE(rib_->fibSyncReqPending_);
}

/**
 * Test that fib.flushed counter is incremented only when ribEntries_.size() <=
 * localRoutes_.size() (i.e., only local routes remain after a flush).
 * Tests both negative case (counter NOT incremented when ribEntries > local)
 * and positive case (counter incremented when ribEntries <= local).
 */
TEST_F(RibFixture, FibFlushedCounterTest) {
  auto& inputQ = rib_->fromFibMessageQ_;

  // Initialize FIB counters from within the Rib's event base thread
  rib_->evb_.runInEventBaseThreadAndWait([&]() { FibStats::initCounters(); });

  // Force publish to ensure counter initialization is visible
  fb303::ThreadCachedServiceData::get()->publishStats();

  // Set up test state with ribEntries_.size() (2) > localRoutes_.size() (1)
  auto localPrefix = folly::CIDRNetwork(IPAddress("10.0.0.0"), 24);
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->defaultVrf_.ribEntries().clear();
    rib_->localRoutes_.clear();

    // Set up a local route
    thrift::BgpNetwork network;
    network.prefix() = "10.0.0.0/24";
    LocalRoute localRoute(network, 0, nullptr);
    rib_->localRoutes_.emplace(localPrefix, localRoute);

    // Add one rib entry for the local route
    RibEntry localEntry(localPrefix);
    localEntry.updatePath(eBgpPeer1_, attr_, false);
    RibBase::selectBestPath(
        localEntry, multipathSelector, bestpathSelector, false, 0);
    rib_->defaultVrf_.ribEntries().emplace(localPrefix, localEntry);

    /*
     * Add a non-local rib entry WITHOUT a path (getAllPathsCnt() == 0)
     * This simulates a route that has been withdrawn but not yet erased
     */
    RibEntry nonLocalEntry(kV4Prefix1);
    rib_->defaultVrf_.ribEntries().emplace(kV4Prefix1, nonLocalEntry);
  });

  // Verify setup: ribEntries_.size() > localRoutes_.size()
  EXPECT_EQ(2, rib_->defaultVrf_.ribEntries().size());
  EXPECT_EQ(1, rib_->localRoutes_.size());

  /*
   * Negative case: ribEntries_.size() > localRoutes_.size()
   * Counter should NOT be incremented
   *
   * For SUM-type timeseries stats, read with ".sum" suffix
   */
  const std::string kFibFlushedSum =
      std::string(FibStats::kFibFlushed) + ".sum";
  {
    fb303::ThreadCachedServiceData::get()->publishStats();
    auto beforeCounter =
        fb303::ThreadCachedServiceData::get()->getCounter(kFibFlushedSum);

    // Get nexthops for local entry only
    std::shared_ptr<const WeightedNexthopMap> localNexthops;
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      localNexthops = rib_->defaultVrf_.ribEntries()
                          .find(localPrefix)
                          ->second.getMultipathWeightedNexthops();
    });

    /*
     * Create FibProgrammedMessage with only localPrefix
     * (non-local entry still exists but not in this message)
     */
    folly::F14NodeMap<
        folly::CIDRNetwork,
        std::shared_ptr<const WeightedNexthopMap>>
        prefixToNexthops = {{localPrefix, localNexthops}};
    Fib::FibProgrammedPfxs waitForAck;
    waitForAck[nullptr] = prefixToNexthops;

    Fib::FibProgrammedMessage fibMsg(
        fboss::kDefaultVrf, waitForAck, false /* fullSync */);
    inputQ.push(std::move(fibMsg));

    // Wait for the message to be processed
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));

    // Verify counter was NOT incremented (ribEntries_ still has 2 entries)
    fb303::ThreadCachedServiceData::get()->publishStats();
    auto afterCounter =
        fb303::ThreadCachedServiceData::get()->getCounter(kFibFlushedSum);
    EXPECT_EQ(beforeCounter, afterCounter)
        << "Counter should NOT be incremented when ribEntries_.size() > "
           "localRoutes_.size()";
  }

  /*
   * Positive case: send withdrawal (nullptr nexthops) for non-local entry
   * handleFibProgrammedMessage will erase it since getAllPathsCnt() == 0
   * Counter SHOULD be incremented
   */
  {
    fb303::ThreadCachedServiceData::get()->publishStats();
    auto beforeCounter =
        fb303::ThreadCachedServiceData::get()->getCounter(kFibFlushedSum);

    /*
     * Create FibProgrammedMessage with nullptr nexthops for non-local prefix
     * (withdrawal). Since entry has getAllPathsCnt() == 0, it will be erased.
     */
    folly::F14NodeMap<
        folly::CIDRNetwork,
        std::shared_ptr<const WeightedNexthopMap>>
        prefixToNexthops = {{kV4Prefix1, nullptr}};
    Fib::FibProgrammedPfxs withdrawalMsg;
    withdrawalMsg[nullptr] = prefixToNexthops;

    Fib::FibProgrammedMessage fibMsg(
        fboss::kDefaultVrf, withdrawalMsg, false /* fullSync */);
    inputQ.push(std::move(fibMsg));

    // Verify counter WAS incremented
    WITH_RETRIES({
      fb303::ThreadCachedServiceData::get()->publishStats();
      auto afterCounter =
          fb303::ThreadCachedServiceData::get()->getCounter(kFibFlushedSum);
      ASSERT_EVENTUALLY_GT(afterCounter, beforeCounter);
    });
  }
}

} // namespace bgp
} // namespace facebook
