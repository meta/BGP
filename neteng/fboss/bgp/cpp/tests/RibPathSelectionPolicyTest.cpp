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

#define RibBase_TEST_FRIENDS                                                   \
  friend class RibFixtureAddPathTestSuite;                                     \
  friend class RibFsdbFixture;                                                 \
  FRIEND_TEST(RibFixtureAddPathTestSuite, PathSelectionReadOnlyTest);          \
  FRIEND_TEST(RibFixtureAddPathTestSuite, PathSelectionIdenticalPolicyTest);   \
  FRIEND_TEST(RibFixtureAddPathTestSuite, PathSelectionReplaceTest);           \
  FRIEND_TEST(RibFixtureAddPathTestSuite, PathSelectionExpirationTest);        \
  FRIEND_TEST(RibFixtureAddPathTestSuite, PathSelectionMultipleUpdateTest);    \
  FRIEND_TEST(RibFixtureAddPathTestSuite, PathSelectionClearTest);             \
  FRIEND_TEST(                                                                 \
      RibFixtureAddPathTestSuite,                                              \
      PathSelectionRollOutBackTestRelaxDefaultMNH);                            \
  FRIEND_TEST(RibFixtureAddPathTestSuite, PathSelectionRollOutBackTest);       \
  FRIEND_TEST(RibFixtureAddPathTestSuite, PathSelectionMinAggLbwbpsRelaxTest); \
  FRIEND_TEST(RibFixtureAddPathTestSuite, PathSelectionMinAggLbwbpsTest);      \
  FRIEND_TEST(                                                                 \
      RibFixtureAddPathTestSuite, PathSelectionMinAggLbwbpsMissingLbwTest);    \
  FRIEND_TEST(RibFixtureAddPathTestSuite, AnnouncementTest);                   \
  FRIEND_TEST(RibFixtureAddPathTestSuite, SetGetClearPathSelectionPolicyTest); \
  FRIEND_TEST(RibFsdbAddPathTestSuite, ReplacePathSelectionPolicyTest);        \
  FRIEND_TEST(                                                                 \
      RibFsdbAddPathTestSuite, ReplacePathSelectionPolicyForceUpdateTest);

#define MockRib_TEST_FRIENDS                                            \
  FRIEND_TEST(RibFsdbAddPathTestSuite, ReplacePathSelectionPolicyTest); \
  FRIEND_TEST(                                                          \
      RibFsdbAddPathTestSuite, ReplacePathSelectionPolicyForceUpdateTest);

/*
 * pathSelectionPolicy_ moved from RibBase to RibDC in diff 9/10. Tests that
 * access the field directly need RibDC friend access too.
 */
#define RibDC_TEST_FRIENDS                                                     \
  friend class RibFixtureAddPathTestSuite;                                     \
  friend class RibFsdbFixture;                                                 \
  FRIEND_TEST(RibFixtureAddPathTestSuite, PathSelectionReadOnlyTest);          \
  FRIEND_TEST(RibFixtureAddPathTestSuite, PathSelectionIdenticalPolicyTest);   \
  FRIEND_TEST(RibFixtureAddPathTestSuite, PathSelectionReplaceTest);           \
  FRIEND_TEST(RibFixtureAddPathTestSuite, PathSelectionExpirationTest);        \
  FRIEND_TEST(RibFixtureAddPathTestSuite, PathSelectionMultipleUpdateTest);    \
  FRIEND_TEST(RibFixtureAddPathTestSuite, PathSelectionClearTest);             \
  FRIEND_TEST(                                                                 \
      RibFixtureAddPathTestSuite,                                              \
      PathSelectionRollOutBackTestRelaxDefaultMNH);                            \
  FRIEND_TEST(RibFixtureAddPathTestSuite, PathSelectionRollOutBackTest);       \
  FRIEND_TEST(RibFixtureAddPathTestSuite, PathSelectionMinAggLbwbpsRelaxTest); \
  FRIEND_TEST(RibFixtureAddPathTestSuite, PathSelectionMinAggLbwbpsTest);      \
  FRIEND_TEST(                                                                 \
      RibFixtureAddPathTestSuite, PathSelectionMinAggLbwbpsMissingLbwTest);    \
  FRIEND_TEST(RibFixtureAddPathTestSuite, AnnouncementTest);                   \
  FRIEND_TEST(RibFixtureAddPathTestSuite, SetGetClearPathSelectionPolicyTest); \
  FRIEND_TEST(RibFsdbAddPathTestSuite, ReplacePathSelectionPolicyTest);        \
  FRIEND_TEST(                                                                 \
      RibFsdbAddPathTestSuite, ReplacePathSelectionPolicyForceUpdateTest);

#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/rib_policy_types.h"
#include "neteng/fboss/bgp/cpp/BgpServiceUtil.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibFsdbPolicyTestFixture.h"
#include "neteng/fboss/bgp/cpp/tests/RibPolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibUtils.h"

using namespace facebook::bgp::rib_policy;
using namespace facebook::neteng::fboss::bgp::thrift;

namespace facebook {
namespace bgp {

class RibFibOutPresenceTest : public RibFixture,
                              public testing::WithParamInterface<bool> {
 public:
  void SetUp() override {
    ribFixtureDefaultSetup(
        ComputeUcmpFromLbwComm{true},
        CountConfedsInAsPathLen{false},
        EnableNexthopTracking{false},
        nullptr,
        GetParam());
  }

  void installStrictCpsPolicy(const folly::CIDRNetwork& prefix) {
    TPathSelector pathSelector;
    pathSelector.bgp_native_path_selection_min_nexthop() = 2;
    pathSelector.drain_on_min_nexthop_violation() = false;
    sendPathSelectionPolicySet(
        createTPathSelectionPolicyWithPathSelector({prefix}, pathSelector));
    rib_->waitForPathSelectionPolicyUpdate();
  }
};

INSTANTIATE_TEST_SUITE_P(
    RibFixture,
    RibFixtureAddPathTestSuite,
    testing::Values(true /* addPath */));

INSTANTIATE_TEST_SUITE_P(
    RibFixture,
    RibFsdbAddPathTestSuite,
    testing::Values(true /* addPath */));

INSTANTIATE_TEST_SUITE_P(
    FibOutTracking,
    RibFibOutPresenceTest,
    testing::Values(false, true),
    [](const testing::TestParamInfo<bool>& info) {
      return info.param ? "Enabled" : "Disabled";
    });

/*
 * Test rib policy: set path selection before rib turns from read_only to write
 * mode
 */
TEST_F(RibFixtureAddPathTestSuite, PathSelectionReadOnlyTest) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  nettools::bgplib::BgpAttrCommunitiesC communities;
  communities.emplace_back(200, 666);
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  // criteria min nexthop = 2, default min nexthop = 3
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);

  /*
   * setup:
   * next hop update for prefix
   * rib policy set for prefix
   * sendInitialPathComputation to turn rib into write mode
   */

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  // send route from localPeer_
  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attr->setNexthop(kLocalRouteV4Nexthop);
  attr->setCommunities(communities);
  attr->setOrigin(nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP);
  attr->publish();

  auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch, localPeer_, attr);

  // send rib policy over
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();

  sendInitialPathComputation();
  ribFuture.wait();

  const auto& ribEntryIter = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
  EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), ribEntryIter);
  const auto& ribEntry = ribEntryIter->second;

  /*
   * RibPolicy will be applied after EOR is received. Since we only have one
   * path here, both min nexthop criteria will be violated and we receive no
   * path. As a result, no fib programming should be triggered in this case
   */
  EXPECT_EQ(nullptr, ribEntry.getBestPath());

  /*
   * Right after EOR, a fullSync will be issued and there is only one
   * route in fibBatchList_
   */
  EXPECT_EQ(1, rib_->fibItems.size());
}

/*
 * Test rib policy: set identical path selection as existing one (which would
 * only refresh the ttl)
 */
TEST_P(RibFixtureAddPathTestSuite, PathSelectionIdenticalPolicyTest) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  nettools::bgplib::BgpAttrCommunitiesC communities;
  communities.emplace_back(200, 666);
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  // criteria min nexthop = 2, default min nexthop = 3
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  // send route from localPeer_
  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attr->setNexthop(kLocalRouteV4Nexthop);
  attr->setCommunities(communities);
  attr->setOrigin(nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP);
  attr->publish();

  auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch, localPeer_, attr);
  sendInitialPathComputation();

  /*
   * Ensure that we have finished the route installation to investigate only
   * the results from sendRibPolicySet
   */
  ribFuture.wait();

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();

  // send rib policy over
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  // reset fibItems
  rib_->fibItems.clear();

  // send the same rib policy over, which refreshes the TTL
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();

  // prepareFibProgramming will not be called as hasUpdate = false

  // Since prepareFibProgramming is not called, fibItems is still 0
  EXPECT_EQ(0, rib_->fibItems.size());
}

/*
 * Test rib policy: replace path selection
 */
TEST_P(RibFixtureAddPathTestSuite, PathSelectionReplaceTest) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  nettools::bgplib::BgpAttrCommunitiesC communities;
  communities.emplace_back(200, 666);
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  // criteria min nexthop = 2, default min nexthop = 3
  auto tPathSelector1 = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);
  // criteria min nexthop = 1, default min nexthop = 3
  auto tPathSelector2 = createTPathSlectorWithOneMatcher(tMatcher, 1, 3);

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  // send route from localPeer_
  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attr->setNexthop(kLocalRouteV4Nexthop);
  attr->setCommunities(communities);
  attr->setOrigin(nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP);
  attr->publish();

  auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch, localPeer_, attr);
  sendInitialPathComputation();

  /*
   * Ensure that we have finished the route installation to investigate only
   * the results from sendRibPolicySet
   */
  ribFuture.wait();

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();

  // send rib policy over
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, tPathSelector1));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  // push a different rib policy
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();

  /*
   * change the min nexthop = 1 for the centralized criteria
   * send the different rib policy over
   */
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, tPathSelector2));
  rib_->waitForPathSelectionPolicyUpdate();

  ribFuture.wait();

  const auto& ribEntryIter = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
  const auto& ribEntry = ribEntryIter->second;

  // Now we should have a match and the best path is selected
  EXPECT_NE(nullptr, ribEntry.getBestPath());

  // The new path should trigger a fib programming
  EXPECT_EQ(1, rib_->fibItems.size());
}

/*
 * Test rib policy: policy expiration should NOT affect installed path selection
 * criteria
 */
TEST_P(RibFixtureAddPathTestSuite, PathSelectionExpirationTest) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  nettools::bgplib::BgpAttrCommunitiesC communities;
  communities.emplace_back(200, 666);
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  // criteria min nexthop = 1, default min nexthop = 3
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 1, 3);

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  // send route from localPeer_
  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attr->setNexthop(kLocalRouteV4Nexthop);
  attr->setCommunities(communities);
  attr->setOrigin(nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP);
  attr->publish();

  auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch, localPeer_, attr);
  sendInitialPathComputation();

  /*
   * Ensure that we have finished the route installation to investigate only
   * the results from sendRibPolicySet
   */
  ribFuture.wait();

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();

  // send rib policy over
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  const auto& ribEntryIter = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
  const auto& ribEntry = ribEntryIter->second;
  auto oldPath = ribEntry.getBestPath();

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();

  // send 0 ttl RA policy over, which is similar to policy expiration
  {
    // include the route attribute policy with 0 ttl
    sendRouteAttributePolicySet(
        createTRouteAttributePolicyLbw({kV6Prefix1}, 10, "stmt1", 0));
  }

  ribFuture.wait();

  rib_->waitForRouteAttributePolicyUpdate();
  EXPECT_NE(nullptr, rib_->pathSelectionPolicy_);

  // The same best path should still be selected
  EXPECT_EQ(oldPath, ribEntry.getBestPath());

  // The best path is still the same, no fib programming will be triggered
  EXPECT_EQ(0, rib_->fibItems.size());
}

/*
 * Test rib policy: update path selection with multiple rib entry update event
 */
TEST_P(RibFixtureAddPathTestSuite, PathSelectionMultipleUpdateTest) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  nettools::bgplib::BgpAttrCommunitiesC communities;
  communities.emplace_back(200, 666);
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  // criteria min nexthop = 2, default min nexthop = 3
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  // send route from localPeer_
  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attr->setNexthop(kLocalRouteV4Nexthop);
  attr->setCommunities(communities);
  attr->setOrigin(nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP);
  attr->publish();

  // update multiple entries
  auto prefixBatch1 = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch1, localPeer_, attr);
  auto prefixBatch2 = PrefixPathIds{{kV6Prefix2, kDefaultPathID}};
  sendAnnouncement(prefixBatch2, localPeer_, attr);
  sendInitialPathComputation();

  /*
   * Ensure that we have finished the route installation to investigate only
   * the results from sendRibPolicySet
   */
  ribFuture.wait();

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();

  // send rib policy over on kV6Prefix1, which selects no path
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  {
    // kV6Prefix1 rib should be altered by rib policy
    const auto& ribEntryIter = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), ribEntryIter);
    const auto& ribEntry = ribEntryIter->second;

    EXPECT_EQ(nullptr, ribEntry.getBestPath());
  }
  {
    // kV6Prefix2 rib should not be altered by rib policy
    const auto& ribEntryIter = rib_->defaultVrf_.ribEntries().find(kV6Prefix2);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), ribEntryIter);
    const auto& ribEntry = ribEntryIter->second;

    EXPECT_NE(nullptr, ribEntry.getBestPath());
  }
  // Check the active criteria
  {
    auto prefixes = std::make_unique<std::vector<std::string>>();
    prefixes->emplace_back(folly::IPAddress::networkToString(kV6Prefix1));
    prefixes->emplace_back(folly::IPAddress::networkToString(kV6Prefix2));

    auto activePathSelectionCriteria = *folly::coro::blockingWait(
        service_->co_getActivePathSelectionCriteria(std::move(prefixes)));

    EXPECT_EQ(activePathSelectionCriteria.size(), 2);
    {
      // For kV6Prefix1
      auto& item = activePathSelectionCriteria[0];
      /*
       * Match the criteria, no criteria matched
       * BGP native min nexhop filtered out all the paths
       */
      EXPECT_EQ(item.criteria_list()->size(), 0);
      EXPECT_EQ(*item.bgp_native_path_selection_min_nexthop(), 3);
    }
    {
      /*
       * For kV6Prefix2
       * Does not match a statement, the resulting TPathSelector is empty
       */
      EXPECT_EQ(activePathSelectionCriteria[1], TPathSelector());
    }
  }
}

/*
 * Test rib policy: clear rib policy
 */
TEST_P(RibFixtureAddPathTestSuite, PathSelectionClearTest) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  nettools::bgplib::BgpAttrCommunitiesC communities;
  communities.emplace_back(200, 666);
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  // criteria min nexthop = 2, default min nexthop = 3
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  // send route from localPeer_
  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attr->setNexthop(kLocalRouteV4Nexthop);
  attr->setCommunities(communities);
  attr->setOrigin(nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP);
  attr->publish();

  auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch, localPeer_, attr);
  sendInitialPathComputation();

  /*
   * Ensure that we have finished the route installation to investigate only
   * the results from sendRibPolicySet
   */
  ribFuture.wait();

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();

  // send rib policy over on kV6Prefix1, which selects no path
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  {
    // rib should be altered by rib policy
    const auto& ribEntryIter = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), ribEntryIter);
    const auto& ribEntry = ribEntryIter->second;

    EXPECT_EQ(nullptr, ribEntry.getBestPath());
  }

  // calling clear rib policy api should remove rib-policy
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();

  // clear rib policy
  rib_->clearRibPolicy();
  rib_->waitForRibPolicyClear();

  ribFuture.wait();

  {
    const auto& ribEntryIter = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
    const auto& ribEntry = ribEntryIter->second;

    // The rib policy no longer exists. The best path is now selected
    EXPECT_NE(nullptr, ribEntry.getBestPath());
  }

  // The new path should trigger a fib programming (fullSync)
  EXPECT_EQ(1, rib_->fibItems.size());
}

/*
 * Test CPS roll out/back cases:
 * CPS roll out case:
 * - start with empty FIB
 * - inject path selector with default MNH 3 and relax MNH set to true
 * - got route with 2 nexthops -> expect route in FIB but no announcement
 * - same route got updated with 3 more nexthops -> expect announcement
 * - same route dropped 3 nexthops -> expect withdrawal from peers
 * CPS roll back case:
 * - clear rib-policy -> expect announcement to peers
 * - clear MNH -> expect announcement to peers
 */
TEST_P(
    RibFixtureAddPathTestSuite,
    PathSelectionRollOutBackTestRelaxDefaultMNH) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  nettools::bgplib::BgpAttrCommunitiesC communities;
  communities.emplace_back(200, 666);
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  /*
   * criteria min nexthop = 2, default min nexthop = 3
   * centralized criteria does not match as no communities are specified for
   * paths
   */
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);
  tPathSelector.drain_on_min_nexthop_violation() = true;

  /*
   * CPS roll out case:
   * - start with empty FIB
   */
  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendInitialPathComputation();
  ribFuture.wait();

  {
    // Message marker for start of initial announcements
    auto eorMsg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(eorMsg));

    // Initial RibOutAnnouncements
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    EXPECT_TRUE(announcement.entries.empty());
    EXPECT_TRUE(announcement.sendWithEoR);
    EXPECT_TRUE(announcement.initialDump);
  }

  // - inject path selector with default MNH 3
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();

  // send rib policy over
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  // Nothing is programmed to FIB yet
  EXPECT_EQ(0, rib_->fibItems.size());

  // - got route with 2 nexthops -> expect route in FIB but no announcement
  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(2, 1, 0, 2));
  attr->setNexthop(kV4Nexthop1);
  attr->publish();

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr);
  sendAnnouncement(prefixBatch, eBgpPeer2_, attr);
  ribFuture.wait();

  const auto& ribEntryIter = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
  EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), ribEntryIter);
  const auto& ribEntry = ribEntryIter->second;

  // two paths
  EXPECT_EQ(2, ribEntry.getMultipaths().size());

  // one route
  EXPECT_EQ(1, rib_->fibItems.size());

  // Check the active criteria
  {
    auto prefixes = std::make_unique<std::vector<std::string>>();
    prefixes->emplace_back(folly::IPAddress::networkToString(kV6Prefix1));

    auto activePathSelectionCriteria = *folly::coro::blockingWait(
        service_->co_getActivePathSelectionCriteria(std::move(prefixes)));

    EXPECT_EQ(activePathSelectionCriteria.size(), 1);
    auto& item = activePathSelectionCriteria[0];
    // No criteria matches
    EXPECT_EQ(item.criteria_list()->size(), 0);
    // The bgp native minimum nexthop in effect
    EXPECT_EQ(*item.bgp_native_path_selection_min_nexthop(), 3);
  }

  // With partial drain, bestpath is retained and announced
  WITH_RETRIES({ ASSERT_EVENTUALLY_GE(ribOutQ_.size(), 1); });
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    ASSERT_EQ(1, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
  }

  // - same route got updated with 3 more nexthops -> expect announcement
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendAnnouncement(prefixBatch, eBgpPeer3_, attr);
  sendAnnouncement(prefixBatch, eBgpPeer4_, attr);
  sendAnnouncement(prefixBatch, eBgpPeer5_, attr);
  ribFuture.wait();

  // rib reflects # of paths
  EXPECT_EQ(5, ribEntry.getMultipaths().size());

  // expect announcement to peers
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    ASSERT_EQ(1, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
  }

  // - same route dropped 3 nexthops -> back to partial drain
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendWithdrawal(prefixBatch, eBgpPeer3_);
  sendWithdrawal(prefixBatch, eBgpPeer4_);
  sendWithdrawal(prefixBatch, eBgpPeer5_);
  ribFuture.wait();

  // back to two paths
  EXPECT_EQ(2, ribEntry.getMultipaths().size());

  // With partial drain, bestpath is retained — re-announcement, not withdrawal
  WITH_RETRIES({ ASSERT_EVENTUALLY_GE(ribOutQ_.size(), 1); });
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    ASSERT_EQ(1, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
  }

  /*
   * CPS roll back case:
   * - clear rib-policy -> expect announcement to peers (drain → normal)
   */
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  rib_->clearRibPolicy();
  rib_->waitForRibPolicyClear();
  ribFuture.wait();

  // no change to multipaths
  EXPECT_EQ(2, ribEntry.getMultipaths().size());
  EXPECT_FALSE(ribEntry.getIsPartialDrain());

  WITH_RETRIES({ ASSERT_EVENTUALLY_GE(ribOutQ_.size(), 1); });
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    ASSERT_EQ(1, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
  }

  /*
   * alternatively, we could also clear MNH
   * we first inject path selector with default MNH 3
   */
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  EXPECT_EQ(2, ribEntry.getMultipaths().size());

  // With partial drain, bestpath is retained — re-announcement, not withdrawal
  WITH_RETRIES({ ASSERT_EVENTUALLY_GE(ribOutQ_.size(), 1); });
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    ASSERT_EQ(1, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
  }

  // - clear MNH -> expect announcement to peers
  tPathSelector.bgp_native_path_selection_min_nexthop().reset();
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  EXPECT_EQ(2, ribEntry.getMultipaths().size());

  // expect announcement to peers
  WITH_RETRIES({ ASSERT_EVENTUALLY_GE(ribOutQ_.size(), 1); });
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    ASSERT_EQ(1, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
  }
}

/*
 * Test CPS roll out/back cases:
 * CPS roll out case:
 * - start with empty FIB
 * - inject path selector with MNH 3
 * - got route with 2 nexthops -> expect empty FIB
 * - same route got updated with 3 more nexthops -> expect FIB installation
 * - same route dropped 3 nexthops -> expect FIB withdrawal
 * CPS roll back case:
 * - clear rib-policy -> expect FIB installation
 * - clear MNH -> expect FIB installation
 */
TEST_P(RibFixtureAddPathTestSuite, PathSelectionRollOutBackTest) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  nettools::bgplib::BgpAttrCommunitiesC communities;
  communities.emplace_back(200, 666);
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  /*
   * criteria min nexthop = 2, default min nexthop = 3
   * centralized criteria does not match as no communities are specified for
   * paths
   */
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);

  /*
   * CPS roll out case:
   * - start with empty FIB
   */
  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendInitialPathComputation();
  ribFuture.wait();

  // - inject path selector with default MNH 3
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();

  // send rib policy over
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  // Nothing is programmed to FIB
  EXPECT_EQ(0, rib_->fibItems.size());

  // - got route with 2 nexthops -> expect empty FIB
  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(2, 1, 0, 2));
  attr->setNexthop(kV4Nexthop1);
  attr->publish();

  auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr);
  sendAnnouncement(prefixBatch, eBgpPeer2_, attr);
  ribFuture.wait();

  const auto& ribEntryIter = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
  EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), ribEntryIter);
  const auto& ribEntry = ribEntryIter->second;

  // No path
  EXPECT_EQ(0, ribEntry.getMultipaths().size());

  // Nothing is programmed to FIB
  EXPECT_EQ(0, rib_->fibItems.size());

  // Check the active criteria
  {
    auto prefixes = std::make_unique<std::vector<std::string>>();
    prefixes->emplace_back(folly::IPAddress::networkToString(kV6Prefix1));

    auto activePathSelectionCriteria = *folly::coro::blockingWait(
        service_->co_getActivePathSelectionCriteria(std::move(prefixes)));

    EXPECT_EQ(activePathSelectionCriteria.size(), 1);
    auto& item = activePathSelectionCriteria[0];
    // No criteria matches
    EXPECT_EQ(item.criteria_list()->size(), 0);
    // The bgp native minimum nexthop in effect
    EXPECT_EQ(*item.bgp_native_path_selection_min_nexthop(), 3);
  }

  // - same route got updated with 3 more nexthops -> expect FIB installation
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendAnnouncement(prefixBatch, eBgpPeer3_, attr);
  sendAnnouncement(prefixBatch, eBgpPeer4_, attr);
  sendAnnouncement(prefixBatch, eBgpPeer5_, attr);
  ribFuture.wait();

  /*
   * rib should be altered by rib policy
   * The paths will be programmed to FIB
   */
  EXPECT_EQ(5, ribEntry.getMultipaths().size());

  // - same route dropped 3 nexthops -> expect FIB withdrawal
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendWithdrawal(prefixBatch, eBgpPeer3_);
  sendWithdrawal(prefixBatch, eBgpPeer4_);
  sendWithdrawal(prefixBatch, eBgpPeer5_);
  ribFuture.wait();

  // no path available
  EXPECT_EQ(0, ribEntry.getMultipaths().size());

  /*
   * CPS roll back case:
   * - clear rib-policy -> expect FIB installation
   */
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  rib_->clearRibPolicy();
  rib_->waitForRibPolicyClear();
  EXPECT_EQ(nullptr, rib_->pathSelectionPolicy_);
  ribFuture.wait();

  // The rib policy no longer exists
  EXPECT_EQ(2, ribEntry.getMultipaths().size());

  // The route is installed to fib
  EXPECT_EQ(1, rib_->fibItems.size());

  /*
   * alternatively, we could also clear MNH
   * we first inject path selector with default MNH 3
   */
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  // The rib policy is in effect
  EXPECT_EQ(0, ribEntry.getMultipaths().size());

  // - clear MNH -> expect FIB installation
  tPathSelector.bgp_native_path_selection_min_nexthop().reset();
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  // The rib policy no longer exists
  EXPECT_EQ(2, ribEntry.getMultipaths().size());

  // The route is installed to fib
  EXPECT_EQ(1, rib_->fibItems.size());
}

/*
 * Test min aggregate link bandwidth with relax mode enabled.
 * When aggregate LBW is below threshold:
 * - Routes should still be installed to FIB (keeping it warm)
 * - Routes should not be announced to peers
 */
TEST_P(RibFixtureAddPathTestSuite, PathSelectionMinAggLbwbpsRelaxTest) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  nettools::bgplib::BgpAttrCommunitiesC communities;
  communities.emplace_back(200, 666);
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  /*
   * Set min aggregate LBW threshold to 30 Gbps with relax mode enabled
   * Using nullopt for min nexthop params, 30 Gbps threshold, relax=true
   */
  auto tPathSelector = createTPathSlectorWithOneMatcher(
      tMatcher,
      std::nullopt, // criteriaMinNextHop
      std::nullopt, // defaultMinNextHop
      static_cast<int64_t>(30 * BpsPerGBps), // 30 Gbps threshold
      true); // relaxMinAggLbwbps

  /*
   * CPS roll out case:
   * - start with empty FIB
   */
  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendInitialPathComputation();
  ribFuture.wait();

  {
    // Message marker for start of initial announcements
    auto eorMsg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(eorMsg));

    // Initial RibOutAnnouncements
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    EXPECT_TRUE(announcement.entries.empty());
    EXPECT_TRUE(announcement.sendWithEoR);
    EXPECT_TRUE(announcement.initialDump);
  }

  // - inject path selector with min agg LBW 30 Gbps
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();

  // send rib policy over
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  // Nothing is programmed to FIB yet
  EXPECT_EQ(0, rib_->fibItems.size());

  /*
   * - got route with 2 nexthops, each with 10 Gbps LBW (total 20 Gbps < 30 Gbps
   * threshold) -> expect route in FIB but no announcement (relax mode)
   */
  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(2, 1, 0, 2));
  attr->setNexthop(kV4Nexthop1);
  attr->setNonTransitiveLbwExtCommunity(kLocalAs1, kLbw10G);
  attr->publish();

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr);
  sendAnnouncement(prefixBatch, eBgpPeer2_, attr);
  ribFuture.wait();

  const auto& ribEntryIter = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
  EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), ribEntryIter);
  const auto& ribEntry = ribEntryIter->second;

  // two paths
  EXPECT_EQ(2, ribEntry.getMultipaths().size());

  // one route in FIB (relax mode keeps FIB warm)
  EXPECT_EQ(1, rib_->fibItems.size());

  // Expect no announcement (aggregate LBW 20 Gbps < 30 Gbps threshold)
  EXPECT_TRUE(ribOutQ_.empty());

  /*
   * - same route got updated with 1 more nexthop with 10 Gbps LBW
   * (total 30 Gbps >= 30 Gbps threshold) -> expect announcement
   */
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendAnnouncement(prefixBatch, eBgpPeer3_, attr);
  ribFuture.wait();

  // rib reflects # of paths
  EXPECT_EQ(3, ribEntry.getMultipaths().size());

  // expect announcement to peers
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    ASSERT_EQ(1, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
  }

  // - same route dropped 1 nexthop -> expect withdrawal from peers
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendWithdrawal(prefixBatch, eBgpPeer3_);
  ribFuture.wait();

  // back to two paths
  EXPECT_EQ(2, ribEntry.getMultipaths().size());

  // Expect withdrawal from peers
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
    auto withdrawal = std::get<RibOutWithdrawal>(msg);
    ASSERT_EQ(1, withdrawal.entries.size());
    EXPECT_EQ(kDefaultPathID, withdrawal.entries[0].pathIdToSend);
  }

  // FIB should still have the route (relax mode)
  EXPECT_EQ(1, rib_->fibItems.size());

  /*
   * CPS roll back case:
   * - clear rib-policy -> expect announcement to peers
   */
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  rib_->clearRibPolicy();
  rib_->waitForRibPolicyClear();
  ribFuture.wait();

  // The rib policy no longer exists
  EXPECT_EQ(2, ribEntry.getMultipaths().size());

  // The route is still installed to fib
  EXPECT_EQ(1, rib_->fibItems.size());

  // expect announcement to peers since policy is cleared
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  }
}

TEST_P(RibFibOutPresenceTest, RelaxedLbwFinalWithdrawalProgramsFibDelete) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  const auto matcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  const auto pathSelector = createTPathSlectorWithOneMatcher(
      matcher,
      std::nullopt,
      std::nullopt,
      static_cast<int64_t>(30 * BpsPerGBps),
      true);

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  ribFuture.wait();
  fibFuture.wait();

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, pathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  auto path =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(2, 1, 0, 2));
  path->setNexthop(kV4Nexthop1);
  path->setNonTransitiveLbwExtCommunity(kLocalAs1, kLbw10G);
  path->publish();
  const auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, path);
  sendAnnouncement(prefixBatch, eBgpPeer2_, path);
  ribFuture.wait();
  fibFuture.wait();

  EXPECT_EQ(nullptr, rib_->getBestPath(kV6Prefix1));
  EXPECT_EQ(2, rib_->getMultipath(kV6Prefix1).size());
  ASSERT_EQ(1, rib_->fibItems.size());
  ASSERT_TRUE(rib_->fibItems.contains(kV6Prefix1));
  ASSERT_NE(
      nullptr, rib_->fibItems.at(kV6Prefix1).getMultipathWeightedNexthops());

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(prefixBatch, eBgpPeer1_);
  sendWithdrawal(prefixBatch, eBgpPeer2_);
  ribFuture.wait();

  ASSERT_EQ(1, rib_->fibItems.size());
  ASSERT_TRUE(rib_->fibItems.contains(kV6Prefix1));
  EXPECT_EQ(
      nullptr, rib_->fibItems.at(kV6Prefix1).getMultipathWeightedNexthops());

  fibFuture.wait();
  WITH_RETRIES({
    EXPECT_EVENTUALLY_TRUE(
        rib_->getRibEntryForPrefix(
                std::make_unique<std::string>(
                    folly::IPAddress::networkToString(kV6Prefix1)))
            .empty());
  });
}

TEST_P(
    RibFibOutPresenceTest,
    FinalWithdrawalWithoutPriorFibOutErasesImmediately) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  ribFuture.wait();
  fibFuture.wait();

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  installStrictCpsPolicy(kV6Prefix1);
  ribFuture.wait();

  const auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  ribFuture.wait();

  EXPECT_EQ(nullptr, rib_->getBestPath(kV6Prefix1));
  EXPECT_TRUE(rib_->getMultipath(kV6Prefix1).empty());
  EXPECT_TRUE(rib_->fibItems.empty());

  sendWithdrawal(prefixBatch, eBgpPeer1_);
  WITH_RETRIES({
    EXPECT_EVENTUALLY_TRUE(
        rib_->getRibEntryForPrefix(
                std::make_unique<std::string>(
                    folly::IPAddress::networkToString(kV6Prefix1)))
            .empty());
  });
  EXPECT_TRUE(rib_->fibItems.empty());
}

TEST_P(RibFibOutPresenceTest, EmptyFibOutFinalWithdrawalUsesFibCleanup) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));
  installStrictCpsPolicy(kV6Prefix1);

  const auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);

  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  EXPECT_EQ(nullptr, rib_->getBestPath(kV6Prefix1));
  EXPECT_TRUE(rib_->getMultipath(kV6Prefix1).empty());
  ASSERT_EQ(1, rib_->fibItems.size());
  ASSERT_TRUE(rib_->fibItems.contains(kV6Prefix1));
  EXPECT_EQ(
      nullptr, rib_->fibItems.at(kV6Prefix1).getMultipathWeightedNexthops());

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(prefixBatch, eBgpPeer1_);
  ribFuture.wait();

  ASSERT_EQ(1, rib_->fibItems.size());
  ASSERT_TRUE(rib_->fibItems.contains(kV6Prefix1));
  EXPECT_EQ(
      nullptr, rib_->fibItems.at(kV6Prefix1).getMultipathWeightedNexthops());

  fibFuture.wait();
  WITH_RETRIES({
    EXPECT_EVENTUALLY_TRUE(
        rib_->getRibEntryForPrefix(
                std::make_unique<std::string>(
                    folly::IPAddress::networkToString(kV6Prefix1)))
            .empty());
  });
}

/*
 * Test min aggregate link bandwidth without relax mode.
 * When aggregate LBW is below threshold:
 * - Routes should NOT be installed to FIB
 * - Routes should NOT be announced to peers
 */
TEST_P(RibFixtureAddPathTestSuite, PathSelectionMinAggLbwbpsTest) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  nettools::bgplib::BgpAttrCommunitiesC communities;
  communities.emplace_back(200, 666);
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  // Set min aggregate LBW threshold to 30 Gbps without relax mode
  auto tPathSelector = createTPathSlectorWithOneMatcher(
      tMatcher,
      std::nullopt, // criteriaMinNextHop
      std::nullopt, // defaultMinNextHop
      static_cast<int64_t>(30 * BpsPerGBps), // 30 Gbps threshold
      false); // relaxMinAggLbwbps = false

  /*
   * CPS roll out case:
   * - start with empty FIB
   */
  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendInitialPathComputation();
  ribFuture.wait();

  {
    // Message marker for start of initial announcements
    auto eorMsg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(eorMsg));

    // Initial RibOutAnnouncements
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    EXPECT_TRUE(announcement.entries.empty());
    EXPECT_TRUE(announcement.sendWithEoR);
    EXPECT_TRUE(announcement.initialDump);
  }

  // - inject path selector with min agg LBW 30 Gbps
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();

  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  EXPECT_EQ(0, rib_->fibItems.size());

  /*
   * - got route with 2 nexthops, each with 10 Gbps LBW (total 20 Gbps < 30 Gbps
   * threshold) -> expect no FIB installation and no announcement
   */
  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(2, 1, 0, 2));
  attr->setNexthop(kV4Nexthop1);
  attr->setNonTransitiveLbwExtCommunity(kLocalAs1, kLbw10G);
  attr->publish();

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr);
  sendAnnouncement(prefixBatch, eBgpPeer2_, attr);
  ribFuture.wait();

  const auto& ribEntryIter = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
  EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), ribEntryIter);
  const auto& ribEntry = ribEntryIter->second;

  // no multipath paths selected (below threshold)
  EXPECT_EQ(0, ribEntry.getMultipaths().size());

  // no route in FIB (not relax mode)
  EXPECT_EQ(0, rib_->fibItems.size());

  /*
   * - same route got updated with 1 more nexthop with 10 Gbps LBW
   * (total 30 Gbps >= 30 Gbps threshold) -> expect FIB install and announcement
   */
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendAnnouncement(prefixBatch, eBgpPeer3_, attr);
  ribFuture.wait();

  // rib reflects # of paths
  EXPECT_EQ(3, ribEntry.getMultipaths().size());

  // one route in FIB
  EXPECT_EQ(1, rib_->fibItems.size());

  // expect announcement to peers
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    ASSERT_EQ(1, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
  }

  // - same route dropped 1 nexthop -> expect FIB withdrawal and peer withdrawal
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendWithdrawal(prefixBatch, eBgpPeer3_);
  ribFuture.wait();

  // no paths available (below threshold)
  EXPECT_EQ(0, ribEntry.getMultipaths().size());

  // Expect withdrawal from peers
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
  }

  /*
   * CPS roll back case:
   * - clear rib-policy -> expect FIB installation
   */
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  rib_->clearRibPolicy();
  rib_->waitForRibPolicyClear();
  ribFuture.wait();

  // The rib policy no longer exists
  EXPECT_EQ(2, ribEntry.getMultipaths().size());

  // The route is installed to fib
  EXPECT_EQ(1, rib_->fibItems.size());
}

/*
 * Test min aggregate link bandwidth when paths don't have LBW.
 * When paths are missing LBW:
 * - Should fall back to BGP native path selection
 * - Routes should be installed to FIB and announced normally
 */
TEST_P(RibFixtureAddPathTestSuite, PathSelectionMinAggLbwbpsMissingLbwTest) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  nettools::bgplib::BgpAttrCommunitiesC communities;
  communities.emplace_back(200, 666);
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  // Set min aggregate LBW threshold to 30 Gbps
  auto tPathSelector = createTPathSlectorWithOneMatcher(
      tMatcher,
      std::nullopt, // criteriaMinNextHop
      std::nullopt, // defaultMinNextHop
      static_cast<int64_t>(30 * BpsPerGBps), // 30 Gbps threshold
      false); // relaxMinAggLbwbps = false

  /*
   * CPS roll out case:
   * - start with empty FIB
   */
  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendInitialPathComputation();
  ribFuture.wait();

  {
    // Message marker for start of initial announcements
    auto eorMsg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(eorMsg));

    // Initial RibOutAnnouncements
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
  }

  // - inject path selector with min agg LBW 30 Gbps
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();

  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  EXPECT_EQ(0, rib_->fibItems.size());

  /*
   * - got route with 2 nexthops, NO LBW set on paths
   * -> should fall back to BGP native path selection, install to FIB
   */
  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(2, 1, 0, 2));
  attr->setNexthop(kV4Nexthop1);
  // Note: NOT setting LBW on paths
  attr->publish();

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr);
  sendAnnouncement(prefixBatch, eBgpPeer2_, attr);
  ribFuture.wait();

  const auto& ribEntryIter = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
  EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), ribEntryIter);
  const auto& ribEntry = ribEntryIter->second;

  // two paths (BGP fallback, no LBW check since paths missing LBW)
  EXPECT_EQ(2, ribEntry.getMultipaths().size());

  // route in FIB
  EXPECT_EQ(1, rib_->fibItems.size());

  // expect announcement to peers (BGP fallback, not filtered)
  {
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    ASSERT_EQ(1, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
  }
}

/*
 * Add path selection policy and clear it. Meanwhile, test if the changes get
 * announced.
 */
TEST_P(RibFixtureAddPathTestSuite, AnnouncementTest) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  nettools::bgplib::BgpAttrCommunitiesC communities;
  communities.emplace_back(200, 666);
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  // criteria min nexthop = 2, default min nexthop = 3
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  // send route from localPeer_
  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attr->setNexthop(kLocalRouteV4Nexthop);
  attr->setCommunities(communities);
  attr->setOrigin(nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP);
  attr->publish();

  auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch, localPeer_, attr);

  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  /*
   * Ensure that we have finished the route installation to investigate only
   * the results from sendPathSelectionPolicySet
   */
  ribFuture.wait();

  {
    // Message marker for start of initial announcements
    auto eorMsg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(eorMsg));

    // Initial RibOutAnnouncements
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    ASSERT_EQ(1, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
    EXPECT_EQ(true, announcement.sendWithEoR);
    EXPECT_EQ(true, announcement.initialDump);
  }

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();

  /*
   * send rib policy over on prefix 1, which yields no best path due to default
   * MNH
   */
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  {
    // rib should be altered by rib policy
    const auto& ribEntryIter = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), ribEntryIter);
    const auto& ribEntry = ribEntryIter->second;

    EXPECT_EQ(nullptr, ribEntry.getBestPath());

    // Ensure that the change be announced to the neighbor
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
    auto withdrawal = std::get<RibOutWithdrawal>(msg);
    ASSERT_EQ(1, withdrawal.entries.size());
    EXPECT_EQ(kDefaultPathID, withdrawal.entries[0].pathIdToSend);
  }

  // calling clear rib policy api should remove rib-policy
  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  // clear rib policy
  rib_->clearRibPolicy();
  rib_->waitForRibPolicyClear();

  ribFuture.wait();

  {
    const auto& ribEntryIter = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
    const auto& ribEntry = ribEntryIter->second;

    // The rib policy no longer exists. The best path is now selected
    EXPECT_NE(nullptr, ribEntry.getBestPath());

    // The new path should trigger a fib programming (fullSync)
    EXPECT_EQ(1, rib_->fibItems.size());

    // Ensure that the change be announced to the neighbor
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    auto announcement = std::get<RibOutAnnouncement>(msg);
    ASSERT_EQ(1, announcement.entries.size());
    EXPECT_EQ(kDefaultPathID, announcement.entries[0].pathIdToSend);
  }
}

TEST_P(RibFixtureAddPathTestSuite, SetGetClearPathSelectionPolicyTest) {
  /*
   * test setPathSelectionPolicy, getPathSelectionPolicy, and
   * clearPathSelectionPolicy
   */
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  /*
   * Create the tPathSelectionPolicy and tRibPolicy for testing
   * Create a RibPolicy to select the path with community 200:666
   */
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher);

  TPathSelectionPolicy tPathSelectionPolicy =
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector);

  // send EoR
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  // setPathSelectionPolicy
  {
    // policy with empty route matcher should fail
    TPathSelectionPolicy tEmptyPolicy;
    tEmptyPolicy.statements()->emplace("stmt1", TPathSelectionStatement{});

    const auto expectedError =
        "facebook::bgp::BgpError: Missing matching attribute in RibPolicyRouteMatcher";

    auto result = sendPathSelectionPolicySet(tEmptyPolicy);
    EXPECT_FALSE(*result.success());
    EXPECT_EQ(expectedError, *result.err());

    // nothing is set
    EXPECT_EQ(TPathSelectionPolicy{}, rib_->getPathSelectionPolicy());
    EXPECT_EQ(nullptr, rib_->pathSelectionPolicy_);
    EXPECT_EQ(-1, rib_->getPathSelectionPolicyVersion());
  }
  {
    auto result = sendPathSelectionPolicySet(tPathSelectionPolicy);
    EXPECT_TRUE(*result.success());

    // wait till rib policy output queue got item
    auto outputPolicy = rib_->waitForPathSelectionPolicyUpdate();

    // new policy is set
    EXPECT_EQ(tPathSelectionPolicy, outputPolicy);
    EXPECT_EQ(tPathSelectionPolicy, rib_->getPathSelectionPolicy());
    EXPECT_EQ(
        *tPathSelectionPolicy.version(), rib_->getPathSelectionPolicyVersion());
  }
  // clearPathSelectionPolicy
  {
    rib_->clearPathSelectionPolicy();
    rib_->waitForPathSelectionPolicyClear();

    // nothing is set
    EXPECT_EQ(TPathSelectionPolicy{}, rib_->getPathSelectionPolicy());
  }
  // Test BgpService
  {
    TResult result;

    // policy with empty route matcher should fail
    TPathSelectionPolicy tEmptyPolicy;
    tEmptyPolicy.statements()->emplace("stmt1", TPathSelectionStatement{});

    result = *folly::coro::blockingWait(service_->co_setPathSelectionPolicy(
        std::make_unique<TPathSelectionPolicy>(tEmptyPolicy)));
    EXPECT_FALSE(*result.success());

    auto tPolicy =
        *folly::coro::blockingWait(service_->co_getPathSelectionPolicy());
    // get nothing
    EXPECT_EQ(tPolicy, TPathSelectionPolicy{});
    EXPECT_EQ(nullptr, rib_->pathSelectionPolicy_);

    // successfully set path selection policy
    auto ribPolicyReplaceFuture = rib_->getRibPolicyReplaceFuture();
    result = *folly::coro::blockingWait(service_->co_setPathSelectionPolicy(
        std::make_unique<TPathSelectionPolicy>(tPathSelectionPolicy)));
    EXPECT_TRUE(*result.success());
    ribPolicyReplaceFuture.wait();

    tPolicy = *folly::coro::blockingWait(service_->co_getPathSelectionPolicy());
    EXPECT_EQ(tPolicy, tPathSelectionPolicy);
    EXPECT_NE(nullptr, rib_->pathSelectionPolicy_);

    // set a smaller version would fail
    auto stalePsPolicy =
        createTPathSelectionPolicyWithPathSelector({kV4Prefix3}, tPathSelector);
    stalePsPolicy.version() = 0;
    result = *folly::coro::blockingWait(service_->co_setPathSelectionPolicy(
        std::make_unique<TPathSelectionPolicy>(stalePsPolicy)));
    EXPECT_TRUE(*result.success());

    tPolicy = *folly::coro::blockingWait(service_->co_getPathSelectionPolicy());
    // stalePsPolicy should not be applied
    EXPECT_EQ(tPolicy, tPathSelectionPolicy);
    EXPECT_NE(nullptr, rib_->pathSelectionPolicy_);
    EXPECT_EQ(
        *tPathSelectionPolicy.version(), rib_->getPathSelectionPolicyVersion());

    // equal version should succeed
    auto newPsPolicyWithSameVersion =
        createTPathSelectionPolicyWithPathSelector({kV4Prefix3}, tPathSelector);
    ribPolicyReplaceFuture = rib_->getRibPolicyReplaceFuture();
    result = *folly::coro::blockingWait(service_->co_setPathSelectionPolicy(
        std::make_unique<TPathSelectionPolicy>(newPsPolicyWithSameVersion)));
    EXPECT_TRUE(*result.success());
    ribPolicyReplaceFuture.wait();

    tPolicy = *folly::coro::blockingWait(service_->co_getPathSelectionPolicy());
    // newPsPolicyWithSameVersion should be applied
    EXPECT_EQ(tPolicy, newPsPolicyWithSameVersion);
    EXPECT_NE(nullptr, rib_->pathSelectionPolicy_);
    EXPECT_EQ(
        *newPsPolicyWithSameVersion.version(),
        rib_->getPathSelectionPolicyVersion());

    // clear set policy
    ribPolicyReplaceFuture = rib_->getRibPolicyReplaceFuture();
    folly::coro::blockingWait(service_->co_clearPathSelectionPolicy());
    ribPolicyReplaceFuture.wait();
    tPolicy = *folly::coro::blockingWait(service_->co_getPathSelectionPolicy());
    EXPECT_EQ(tPolicy, TPathSelectionPolicy{});
    EXPECT_EQ(nullptr, rib_->pathSelectionPolicy_);
  }
}

TEST_P(RibFsdbAddPathTestSuite, ReplacePathSelectionPolicyTest) {
  auto subscribedPolicy = fsdbSubscriber_->subscribe(
      fsdbSubscriber_->getRootStatePath().bgp().pathSelectionPolicy());

  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  /*
   * Create the tPathSelectionPolicy and tRibPolicy for testing
   * Create a RibPolicy to select the path with community 200:666
   */
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher);

  TPathSelectionPolicy tPathSelectionPolicy =
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector);
  TRibPolicy tRibPolicy;

  // send EoR
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  // test backward compatibility, should not crash
  {
    // nullptr
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      rib_->replaceRibPolicy(nullptr);
      EXPECT_EQ(rib_->pathSelectionPolicy_, nullptr);
    });

    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      // empty rib policy
      EXPECT_THROW(
          rib_->replaceRibPolicy(std::make_unique<RibPolicy>(tRibPolicy)),
          BgpError);
      EXPECT_EQ(rib_->pathSelectionPolicy_, nullptr);
    });

    tRibPolicy.path_selection_policy() = tPathSelectionPolicy;
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      // with path selection policy
      rib_->replaceRibPolicy(std::make_unique<RibPolicy>(tRibPolicy));
      EXPECT_NE(rib_->pathSelectionPolicy_, nullptr);
    });

    WITH_RETRIES_N(5, {
      auto policyLk = subscribedPolicy.rlock();
      ASSERT_EVENTUALLY_TRUE(policyLk->has_value());
      EXPECT_EVENTUALLY_EQ(*policyLk, tPathSelectionPolicy);
    });

    // clean up the path selection policy
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      rib_->replaceRibPolicy(nullptr);
      EXPECT_EQ(rib_->pathSelectionPolicy_, nullptr);
    });

    WITH_RETRIES_N(
        5, ASSERT_EVENTUALLY_TRUE(subscribedPolicy.rlock()->has_value()));
  }
  {
    /*
     * Add path selection policy
     * It should succeed as we have no path selection policy before
     */
    auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
    /*
     * We need to run replacePathSelectionPolicy in runInEventBaseThreadAndWait
     * as we could potentially call schedulePrepareFibProgrammingTimer, which
     * should be run in the Rib event base thread
     */
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      rib_->replacePathSelectionPolicy(
          std::make_unique<PathSelectionPolicy>(tPathSelectionPolicy));
    });
    ribFuture.wait();
    EXPECT_NE(rib_->pathSelectionPolicy_, nullptr);

    // Pushing the same one should not replace it
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      auto hasUpdate = rib_->replacePathSelectionPolicy(
          std::make_unique<PathSelectionPolicy>(tPathSelectionPolicy));

      EXPECT_FALSE(hasUpdate);
    });

    // change the path selection policy
    tPathSelectionPolicy.statements()->emplace(
        "stmt2",
        createTPathSelectionStatementWithPathSelector(
            {kV6Prefix2}, std::move(tPathSelector)));
    ribFuture = rib_->getRibPrepareFibProgrammingFuture();
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      rib_->replacePathSelectionPolicy(
          std::make_unique<PathSelectionPolicy>(tPathSelectionPolicy));
    });
    /*
     * the policy is changed, and hence we should trigger fib programming
     * preparation
     */
    ribFuture.wait();
    EXPECT_NE(rib_->pathSelectionPolicy_, nullptr);

    // Clear path selection policy
    ribFuture = rib_->getRibPrepareFibProgrammingFuture();
    rib_->evb_.runInEventBaseThreadAndWait(
        [&]() { rib_->replacePathSelectionPolicy(nullptr); });
    ribFuture.wait();
    EXPECT_EQ(rib_->pathSelectionPolicy_, nullptr);
  }
}

/*
 * forceUpdate version matrix for replacePathSelectionPolicy, mirroring the CRF
 * ReplaceRouteFilterPolicyForceUpdateTest. forceUpdate bypasses the version
 * check (used by CPS FILE_MODE) but the content-change check still applies, so
 * an identical policy never updates even with forceUpdate=true.
 */
TEST_P(RibFsdbAddPathTestSuite, ReplacePathSelectionPolicyForceUpdateTest) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher);

  // Helper to build a CPS policy with a given prefix (content) and version.
  auto makePolicy = [&](const folly::CIDRNetwork& prefix, int64_t version) {
    auto policy =
        createTPathSelectionPolicyWithPathSelector({prefix}, tPathSelector);
    policy.version() = version;
    return policy;
  };

  sendInitialPathComputation();

  // Set initial policy with version 12345
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    rib_->replacePathSelectionPolicy(
        std::make_unique<PathSelectionPolicy>(makePolicy(kV4Prefix1, 12345)));
  });
  EXPECT_NE(rib_->pathSelectionPolicy_, nullptr);
  EXPECT_EQ(12345, rib_->pathSelectionPolicy_->getVersion());

  // Same policy without forceUpdate - should NOT update (hasUpdate=false)
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto hasUpdate = rib_->replacePathSelectionPolicy(
        std::make_unique<PathSelectionPolicy>(makePolicy(kV4Prefix1, 12345)));
    EXPECT_FALSE(hasUpdate);
  });

  // Different content, same version, without forceUpdate - should update
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto hasUpdate = rib_->replacePathSelectionPolicy(
        std::make_unique<PathSelectionPolicy>(makePolicy(kV6Prefix2, 12345)));
    EXPECT_TRUE(hasUpdate);
  });

  // Same content, lower version, without forceUpdate - should NOT update
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto hasUpdate = rib_->replacePathSelectionPolicy(
        std::make_unique<PathSelectionPolicy>(makePolicy(kV6Prefix2, 100)));
    EXPECT_FALSE(hasUpdate);
    EXPECT_EQ(12345, rib_->pathSelectionPolicy_->getVersion());
  });

  // Different content, lower version, with forceUpdate=true - should update
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto hasUpdate = rib_->replacePathSelectionPolicy(
        std::make_unique<PathSelectionPolicy>(makePolicy(kV4Prefix1, 100)),
        /*isBootstrap=*/false,
        /*forceUpdate=*/true);
    EXPECT_TRUE(hasUpdate);
    EXPECT_EQ(100, rib_->pathSelectionPolicy_->getVersion());
  });

  // Same content, same version, with forceUpdate=true - should NOT update
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto hasUpdate = rib_->replacePathSelectionPolicy(
        std::make_unique<PathSelectionPolicy>(makePolicy(kV4Prefix1, 100)),
        /*isBootstrap=*/false,
        /*forceUpdate=*/true);
    EXPECT_FALSE(hasUpdate);
  });

  /*
   * Same statements, different (lower) version, with forceUpdate=true - the
   * version is part of policy equality, so this counts as a content change and
   * forceUpdate bypasses the version-monotonicity check, so it should update.
   */
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto hasUpdate = rib_->replacePathSelectionPolicy(
        std::make_unique<PathSelectionPolicy>(makePolicy(kV4Prefix1, 50)),
        /*isBootstrap=*/false,
        /*forceUpdate=*/true);
    EXPECT_TRUE(hasUpdate);
    EXPECT_EQ(50, rib_->pathSelectionPolicy_->getVersion());
  });

  // nullptr with forceUpdate=true - clearing should always work
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    auto hasUpdate = rib_->replacePathSelectionPolicy(
        nullptr,
        /*isBootstrap=*/false,
        /*forceUpdate=*/true);
    EXPECT_TRUE(hasUpdate);
    EXPECT_EQ(rib_->pathSelectionPolicy_, nullptr);
  });
}

TEST_F(RibFixtureCountConfedsInAsPathLen, LongestPathFirstTest) {
  /*
   * Build 3 paths:
   *  1st: 2 asns in AsSequence, 1 confed in ConfedAsSequence
   *  2nd: 1 asn, 2 Confeds
   *  3rd: 0 asn, 4 confeds
   * With ribPolicy selects all 3 paths as multipaths, verify that :
   *  * all 3 paths are selected as multipaths
   *  * 3th should be announced due to longest as + confed path len.
   */
  auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(
      *buildBgpPathFields(2, 1, 0, 0, 1));
  auto attrs2 = std::make_shared<facebook::bgp::BgpPath>(
      *buildBgpPathFields(1, 1, 0, 0, 2));
  auto attrs3 = std::make_shared<facebook::bgp::BgpPath>(
      *buildBgpPathFields(0, 1, 0, 0, 4));

  attrs1->setNexthop(kV6Nexthop1);
  attrs2->setNexthop(kV6Nexthop2);
  attrs3->setNexthop(kV6Nexthop3);

  attrs1->publish();
  attrs2->publish();
  attrs3->publish();

  // build peer2 and peer3 as confed peer
  TinyPeerInfo confedEBgpPeer1{
      kPeerAddr1,
      kPeerAsn1,
      kPeerRouterId1,
      BgpSessionType::ConfedEBGP,
      false /*isRrClient*/,
      false /*isRedistributePeer*/};
  TinyPeerInfo confedEBgpPeer2{
      kPeerAddr2,
      kPeerAsn2,
      kPeerRouterId2,
      BgpSessionType::ConfedEBGP,
      false /*isRrClient*/,
      false /*isRedistributePeer*/};
  TinyPeerInfo confedEBgpPeer3{
      kPeerAddr3,
      kPeerAsn3,
      kPeerRouterId3,
      BgpSessionType::ConfedEBGP,
      false /*isRrClient*/,
      false /*isRedistributePeer*/};

  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  auto prefixBatch = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch, confedEBgpPeer1, attrs1);
  sendAnnouncement(prefixBatch, confedEBgpPeer2, attrs2);
  sendAnnouncement(prefixBatch, confedEBgpPeer3, attrs3);
  sendInitialPathComputation();
  ribFuture.wait();

  /*
   * Without ribPolicy, the result should be the same as
   * nativeBestPathCompute
   */
  {
    auto prefix = std::make_unique<std::string>(
        folly::IPAddress::networkToString(kV6Prefix1));
    auto output = rib_->getRibEntryForPrefix(std::move(prefix));
    auto paths = output[0].paths()->find(kBestPathGroup)->second;
    EXPECT_EQ(2, paths.size());
  }

  // Send ribPolicy over to override the best path selection
  auto tMatcher = createCommunityMatch(65530, 15800, bgp_policy::Origin::EGP);
  // criteria min nexthop = 2, default min nexthop = 3
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);

  ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV6Prefix1}, tPathSelector));
  ribFuture.wait();

  // Now all 3 paths should be selected
  {
    auto prefix = std::make_unique<std::string>(
        folly::IPAddress::networkToString(kV6Prefix1));
    auto output = rib_->getRibEntryForPrefix(std::move(prefix));
    auto paths = output[0].paths()->find(kBestPathGroup)->second;
    EXPECT_EQ(3, paths.size());

    // best_next_hop should be path 3
    EXPECT_EQ(
        network::toBinaryAddress(kV6Nexthop3).addr()->toStdString(),
        *output[0].best_next_hop()->prefix_bin());
  }
}
} // namespace bgp
} // namespace facebook
