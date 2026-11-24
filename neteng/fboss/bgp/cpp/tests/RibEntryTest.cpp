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

#define RibEntry_TEST_FRIENDS                                      \
  friend class PathIdGeneratorFixture;                             \
  FRIEND_TEST(RibEntryTest, UpdatePathPreservesPathIdToSend);      \
  FRIEND_TEST(RibEntryTest, SelectBestPathPreservesPathIdsToSend); \
  FRIEND_TEST(RibEntryTest, SelectBestPathAssignsPathIdsToSend);   \
  FRIEND_TEST(RibEntryTest, HasIgpCostTest);

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <random>

#include <folly/logging/xlog.h>

#include "neteng/fboss/bgp/cpp/common/FeatureFlags.h"
#include "neteng/fboss/bgp/cpp/common/RouteInfo.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopInfo.h"
#include "neteng/fboss/bgp/cpp/rib/RibBase.h"
#include "neteng/fboss/bgp/cpp/rib/RibEntry.h"
#include "neteng/fboss/bgp/cpp/rib/RibPolicy.h"
#include "neteng/fboss/bgp/cpp/rib/Utils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"

namespace facebook::bgp {

/*
 * Test RouteInfo's nexthop retrieval and comparison between them.
 */
TEST(RibEntryTest, SelectBestPathNexthopTest) {
  auto attr1 = std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(
      4 /* as_count */,
      4 /* community_count */,
      4 /* ext_community_count */,
      4 /* cluster_list_count */,
      0 /* confed_as_count */,
      kV4Nexthop1 /* nexthop 11.0.0.1 */));
  attr1->publish();

  auto attr2 = std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(
      4 /* as_count */,
      4 /* community_count */,
      4 /* ext_community_count */,
      4 /* cluster_list_count */,
      0 /* confed_as_count */,
      kV4Nexthop2 /* nexthop 11.0.0.2 */));
  attr2->publish();

  // create a RibEntry with addpath capability
  RibEntry ribEntry(kV4Prefix1);
  auto peerInfo = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  EXPECT_TRUE(ribEntry.updatePath(peerInfo, attr1, true, 0));
  EXPECT_TRUE(ribEntry.updatePath(peerInfo, attr2, true, 1));
  EXPECT_EQ(2, ribEntry.getAllPathsCnt());

  // retrieve per peer routeInfos
  auto routeInfos = ribEntry.getRouteInfos(
      nettools::bgplib::BgpPeerId{kPeerAddr1, kPeerRouterId1});
  EXPECT_EQ(2, routeInfos.size());
  const auto routeInfo1 = routeInfos.at(0);
  const auto routeInfo2 = routeInfos.at(1);

  // make sure nexthop comparison as expected
  EXPECT_LE(routeInfo1->getBgpNexthopAsInt(), routeInfo2->getBgpNexthopAsInt());

  // Now, trigger the bestpath selection
  bool bestpathChanged, nexthopChanged;
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry,
      multipathSelector,
      bestpathSelector,
      false /* compute ucmp */,
      0 /* ucmp width */);
  getAndCheckAllocatedPathIds({}, ribEntry);

  /*
   * 1. Make sure there is no crash of BGP instance.
   * 2. Verify tie-breaking mechanism chooses the nexthop with a smaller
   *    integer representation.
   */
  EXPECT_EQ(routeInfo1, ribEntry.getBestPath());
}

TEST(RibEntryTest, SelectBestPathTest) {
  auto attrs =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs->publish();

  auto peer1 = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  auto peer2 = TinyPeerInfo(
      kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);

  RibEntry ribEntry(kV4Prefix1);

  // Prefix is set correctly
  EXPECT_EQ(kV4Prefix1, ribEntry.getPrefix());

  // all set to null by default
  EXPECT_EQ(nullptr, ribEntry.getBestPath());
  EXPECT_EQ(nullptr, ribEntry.getMultipathWeightedNexthops());

  EXPECT_TRUE(ribEntry.updatePath(peer1, attrs));
  EXPECT_TRUE(ribEntry.updatePath(peer2, attrs));

  // bestpath, nexthops and class id are not changed
  EXPECT_EQ(nullptr, ribEntry.getBestPath());
  EXPECT_EQ(nullptr, ribEntry.getMultipathWeightedNexthops());

  /*
   * Now, trigger the bestpath selection
   * Case 1: bestpathChanged == true and nexthopChanged == true
   */
  bool bestpathChanged, nexthopChanged;
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  auto allocatedPathIds = getAndCheckAllocatedPathIds({}, ribEntry);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_TRUE(nexthopChanged);

  /*
   * Doing bestpath selection again shall not change anything
   * Case 2: bestpathChanged == false and nexthopChanged == false
   */
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
  EXPECT_FALSE(bestpathChanged);
  EXPECT_FALSE(nexthopChanged);

  auto bestpath = ribEntry.getBestPath();
  EXPECT_EQ(kV4Prefix1, bestpath->prefix);
  EXPECT_EQ(peer1, bestpath->peer);
  EXPECT_EQ(attrs, bestpath->attrs);
  auto multipathNexthops = ribEntry.getMultipathWeightedNexthops();
  // Because of the nexthops for both paths are same, there is only one nexthop
  EXPECT_EQ(1, multipathNexthops->size());
  EXPECT_THAT(*multipathNexthops, (WeightedNexthopMap{{kV4Nexthop1, 0}}));
  EXPECT_TRUE(ribEntry.getInstallToFib());

  /*
   * change the nexthop of peer2's prefix, expect multipath change
   * Case 3: bestpathChanged == false and nexthopChanged == true
   */
  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs2->setNexthop(kV4Nexthop2);
  attrs2->publish();

  EXPECT_TRUE(ribEntry.updatePath(peer2, attrs2));

  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
  EXPECT_FALSE(bestpathChanged);
  EXPECT_TRUE(nexthopChanged);

  multipathNexthops = ribEntry.getMultipathWeightedNexthops();
  // Because of the nexthopof peer2 changed, there are two nexthops
  EXPECT_EQ(2, multipathNexthops->size());
  EXPECT_THAT(
      *multipathNexthops,
      (WeightedNexthopMap{{kV4Nexthop1, 0}, {kV4Nexthop2, 0}}));
  EXPECT_TRUE(ribEntry.getInstallToFib());

  // reset peer2 attrs back to old nexthop
  EXPECT_TRUE(ribEntry.updatePath(peer2, attrs));

  // trigger the bestpath selection
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
  EXPECT_FALSE(bestpathChanged);
  EXPECT_TRUE(nexthopChanged);
  multipathNexthops = ribEntry.getMultipathWeightedNexthops();
  // nexthops should only contain one nexthop
  EXPECT_EQ(1, multipathNexthops->size());
  EXPECT_THAT(*multipathNexthops, (WeightedNexthopMap{{kV4Nexthop1, 0}}));
  EXPECT_TRUE(ribEntry.getInstallToFib());

  /*
   * change local preference for peer1 to 0
   * Case 4: bestpathChanged == true and nexthopChanged == false
   */
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs1->setLocalPref(0);
  attrs1->publish();

  // update peer 1 path
  EXPECT_TRUE(ribEntry.updatePath(peer1, attrs1));

  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_FALSE(nexthopChanged);

  // because local preference is lower, peer2 should be chosen as bestpath
  bestpath = ribEntry.getBestPath();
  EXPECT_EQ(kV4Prefix1, bestpath->prefix);
  EXPECT_EQ(peer2, bestpath->peer);
  EXPECT_EQ(attrs, bestpath->attrs);
  multipathNexthops = ribEntry.getMultipathWeightedNexthops();
  EXPECT_EQ(1, multipathNexthops->size());
  EXPECT_THAT(*multipathNexthops, (WeightedNexthopMap{{kV4Nexthop1, 0}}));
  EXPECT_TRUE(ribEntry.getInstallToFib());
}

TEST(RibEntryTest, SelectBestPathEncodedLbwTest) {
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(0, 0, 0, 0));
  attrs1->publish();
  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(0, 0, 0, 0));
  attrs2->setNexthop(kV4Nexthop2);
  attrs2->publish();

  auto peer1 = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  auto peer2 = TinyPeerInfo(
      kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);

  RibEntry ribEntry(kV4Prefix1);

  // Prefix is set correctly
  EXPECT_EQ(kV4Prefix1, ribEntry.getPrefix());

  // all set to null by default
  EXPECT_EQ(nullptr, ribEntry.getBestPath());
  EXPECT_EQ(nullptr, ribEntry.getMultipathWeightedNexthops());
  EXPECT_EQ(nullptr, ribEntry.getNexthopTopoInfoMap());

  EXPECT_TRUE(ribEntry.updatePath(peer1, attrs1, false));
  EXPECT_TRUE(ribEntry.updatePath(peer2, attrs2, false));

  // bestpath, nexthops and class id are not changed
  EXPECT_EQ(nullptr, ribEntry.getBestPath());
  EXPECT_EQ(nullptr, ribEntry.getMultipathWeightedNexthops());
  EXPECT_EQ(nullptr, ribEntry.getNexthopTopoInfoMap());

  /*
   * Now, trigger the bestpath selection
   * bestpathChanged == true and nexthopChanged == true
   */
  bool bestpathChanged, nexthopChanged;
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  auto allocatedPathIds = getAndCheckAllocatedPathIds({}, ribEntry);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_TRUE(nexthopChanged);

  // now update the lbw value of one of the paths
  auto attrs3 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(0, 0, 0, 0));
  attrs3->setNonTransitiveRawLbwExtCommunity(
      65530, 103031941 /* 110 00100100 00100100 1000 0101 */);
  attrs3->publish();
  EXPECT_TRUE(ribEntry.updatePath(peer1, attrs3, false));

  // try bestpath selection again
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);

  /*
   * weighted nexthops did not change because we didn't specify
   * computeUcmp == true
   */
  EXPECT_FALSE(nexthopChanged);
  /*
   * nexthops and topo info map is still nullptr because there's no topology
   * info in bgp attrs
   */
  EXPECT_EQ(nullptr, ribEntry.getNexthopTopoInfoMap());

  // now set topology info in both attrs
  std::unordered_map<std::string, int64_t> topoInfo1 = {{"rack_id", 1}};
  auto attrs4 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(0, 0, 0, 0));
  attrs4->setTopologyInfo(topoInfo1);
  attrs4->publish();
  EXPECT_TRUE(ribEntry.updatePath(peer1, attrs4, false));

  std::unordered_map<std::string, int64_t> topoInfo2 = {{"rack_id", 2}};
  auto attrs5 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(0, 0, 0, 0));
  attrs5->setTopologyInfo(topoInfo2);
  attrs5->setNexthop(kV4Nexthop2);
  attrs5->publish();
  EXPECT_TRUE(ribEntry.updatePath(peer2, attrs5, false));

  // try bestpath selection again
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);

  // this time nexthop changed because topology info changed
  EXPECT_TRUE(nexthopChanged);

  auto topoInfoMap = ribEntry.getNexthopTopoInfoMap();
  EXPECT_EQ(2, topoInfoMap->size());
  EXPECT_EQ(1, topoInfoMap->at(kV4Nexthop1).at("rack_id"));
  EXPECT_EQ(2, topoInfoMap->at(kV4Nexthop2).at("rack_id"));
}

TEST(RibEntryTest, SelectBestPathTestConfedPeer) {
  // attrs1 and attrs2 differ in confed as sequence
  auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(
      *buildBgpPathFields(4, 4, 4, 4, 2));
  attrs1->publish();
  auto attrs2 = std::make_shared<facebook::bgp::BgpPath>(
      *buildBgpPathFields(4, 4, 4, 4, 3));
  attrs2->setNexthop(kV4Nexthop2);
  attrs2->publish();

  auto peer1 = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::ConfedEBGP, false);
  auto peer2 = TinyPeerInfo(
      kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::ConfedEBGP, false);

  RibEntry ribEntry(kV4Prefix1);

  // Prefix is set correctly
  EXPECT_EQ(kV4Prefix1, ribEntry.getPrefix());

  // all set to null by default
  EXPECT_EQ(nullptr, ribEntry.getBestPath());
  EXPECT_EQ(nullptr, ribEntry.getMultipathWeightedNexthops());

  EXPECT_TRUE(ribEntry.updatePath(peer1, attrs1));
  EXPECT_TRUE(ribEntry.updatePath(peer2, attrs2));

  // bestpath and nexthops are not changed
  EXPECT_EQ(nullptr, ribEntry.getBestPath());
  EXPECT_EQ(nullptr, ribEntry.getMultipathWeightedNexthops());

  // check getBgpAsPathLen and getBgpAsPathLenWithConfed returns different value
  auto peer1routeInfos = ribEntry.getRouteInfos(
      nettools::bgplib::BgpPeerId{kPeerAddr1, kPeerRouterId1});
  EXPECT_EQ(1, peer1routeInfos.size());
  const auto& peer1RouteInfo = peer1routeInfos.begin()->second;
  EXPECT_EQ(4, peer1RouteInfo->getBgpAsPathLen());
  EXPECT_EQ(6, peer1RouteInfo->getBgpAsPathLenWithConfed());

  auto peer2routeInfos = ribEntry.getRouteInfos(
      nettools::bgplib::BgpPeerId{kPeerAddr2, kPeerRouterId2});
  EXPECT_EQ(1, peer2routeInfos.size());
  const auto& peer2RouteInfo = peer2routeInfos.begin()->second;
  EXPECT_EQ(4, peer2RouteInfo->getBgpAsPathLen());
  EXPECT_EQ(7, peer2RouteInfo->getBgpAsPathLenWithConfed());

  folly::F14FastMap<std::shared_ptr<RouteInfo>, uint32_t> allocatedPathIds;
  {
    /*
     * with default best path selector, confeds are ignored in as path length
     * calcuation, both will be chosen as multipath
     */
    bool bestpathChanged, nexthopChanged;
    std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
        ribEntry, multipathSelector, bestpathSelector, false, 0);
    allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);

    EXPECT_TRUE(bestpathChanged);
    EXPECT_TRUE(nexthopChanged);

    // check best path point to peer 1
    auto bestpath = ribEntry.getBestPath();
    EXPECT_EQ(kV4Prefix1, bestpath->prefix);
    EXPECT_EQ(peer1, bestpath->peer);
    EXPECT_EQ(attrs1, bestpath->attrs);

    // check multipath have 2 nexthops
    auto multipathNexthops = ribEntry.getMultipathWeightedNexthops();
    EXPECT_EQ(2, multipathNexthops->size());
    EXPECT_THAT(
        *multipathNexthops,
        (WeightedNexthopMap{{kV4Nexthop1, 0}, {kV4Nexthop2, 0}}));
    EXPECT_TRUE(ribEntry.getInstallToFib());
  }

  {
    /*
     * with count confeds in as path len enabled, only peer 1 should be chosen
     * peer 1 is chosen as best path in last run, so bestpathChanged should be
     * false
     */
    bool bestpathChanged, nexthopChanged;
    std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
        ribEntry, multipathSelectorCountConfeds, bestpathSelector, false, 0);
    allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
    EXPECT_FALSE(bestpathChanged);
    EXPECT_TRUE(nexthopChanged);

    // check multipath have only 1 nexthop
    auto multipathNexthops = ribEntry.getMultipathWeightedNexthops();
    EXPECT_EQ(1, multipathNexthops->size());
    EXPECT_THAT(*multipathNexthops, (WeightedNexthopMap{{kV4Nexthop1, 0}}));
    EXPECT_TRUE(ribEntry.getInstallToFib());
  }
}

/**
 * This test verifies all aspects of UCMP weight computation for paths
 * 1. LBW values of paths are in Gbps with common multiplier
 * 2. LBW values of paths are in Mbps with common multiplier
 * 3. LBW values of paths are in Gbps & Mbps
 * 4. LBW values exceeds the ECMP Width
 */
TEST(RibEntryTest, UcmpWeightComputation) {
  const auto attrs = *buildBgpPathFields(4, 4, 4, 4);
  auto peer1 = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  auto peer2 = TinyPeerInfo(
      kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);
  RibEntry ribEntry(kV4Prefix1);
  folly::F14FastMap<std::shared_ptr<RouteInfo>, uint32_t> allocatedPathIds;

  /*
   * 1. LBW values of paths are in Gbps with
   */
  {
    auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(attrs);
    attrs1->setNonTransitiveLbwExtCommunity(kLocalAs1, 24 * BpsPerGBps / 8);
    attrs1->setNexthop(kPeerAddr1);
    attrs1->publish();
    EXPECT_TRUE(ribEntry.updatePath(peer1, attrs1, false));

    auto attrs2 = std::make_shared<facebook::bgp::BgpPath>(attrs);
    attrs2->setNonTransitiveLbwExtCommunity(kLocalAs1, 36 * BpsPerGBps / 8);
    attrs2->setNexthop(kPeerAddr2);
    attrs2->publish();
    EXPECT_TRUE(ribEntry.updatePath(peer2, attrs2, false));

    RibBase::selectBestPath(
        ribEntry,
        multipathSelector,
        bestpathSelector,
        true,
        1024 /* ucmp-width */);
    allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
    WeightedNexthopMap nhWts = {{kPeerAddr1, 2}, {kPeerAddr2, 3}};
    EXPECT_EQ(nhWts, *ribEntry.getMultipathWeightedNexthops());
  }

  /*
   * 1. LBW values of paths are in Gbps with
   */
  {
    auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(attrs);
    attrs1->setNonTransitiveLbwExtCommunity(kLocalAs1, 2400 * BpsPerGBps / 8);
    attrs1->setNexthop(kPeerAddr1);
    attrs1->publish();
    EXPECT_TRUE(ribEntry.updatePath(peer1, attrs1, false));

    auto attrs2 = std::make_shared<facebook::bgp::BgpPath>(attrs);
    attrs2->setNonTransitiveLbwExtCommunity(kLocalAs1, 3600 * BpsPerGBps / 8);
    attrs2->setNexthop(kPeerAddr2);
    attrs2->publish();
    EXPECT_TRUE(ribEntry.updatePath(peer2, attrs2, false));

    RibBase::selectBestPath(
        ribEntry,
        multipathSelector,
        bestpathSelector,
        true,
        1024 /* ucmp-width */);
    allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
    WeightedNexthopMap nhWts = {{kPeerAddr1, 2}, {kPeerAddr2, 3}};
    EXPECT_EQ(nhWts, *ribEntry.getMultipathWeightedNexthops());
  }

  /*
   * 2. LBW values of paths are in Mbps with common multiplier
   */
  {
    auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(attrs);
    attrs1->setNonTransitiveLbwExtCommunity(kLocalAs1, 7 * BpsPerMBps / 8);
    attrs1->setNexthop(kPeerAddr1);
    attrs1->publish();
    EXPECT_TRUE(ribEntry.updatePath(peer1, attrs1, false));

    auto attrs2 = std::make_shared<facebook::bgp::BgpPath>(attrs);
    attrs2->setNonTransitiveLbwExtCommunity(kLocalAs1, 63 * BpsPerMBps / 8);
    attrs2->setNexthop(kPeerAddr2);
    attrs2->publish();
    EXPECT_TRUE(ribEntry.updatePath(peer2, attrs2, false));

    RibBase::selectBestPath(
        ribEntry,
        multipathSelector,
        bestpathSelector,
        true,
        1024 /* ucmp-width */);
    allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
    WeightedNexthopMap nhWts = {{kPeerAddr1, 1}, {kPeerAddr2, 9}};
    EXPECT_EQ(nhWts, *ribEntry.getMultipathWeightedNexthops());
  }

  /*
   * 3. LBW values of paths are in Gbps & Mbps. Weights gets scaled down to
   *    3000:1. The `ucmp-width=1024` kicks in and reduces the ratio to 1024:0.
   *    This means the next-hop with weight=0 is removed.
   */
  {
    auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(attrs);
    attrs1->setNonTransitiveLbwExtCommunity(kLocalAs1, 3 * BpsPerGBps / 8);
    attrs1->setNexthop(kPeerAddr1);
    attrs1->publish();
    EXPECT_TRUE(ribEntry.updatePath(peer1, attrs1, false));

    auto attrs2 = std::make_shared<facebook::bgp::BgpPath>(attrs);
    attrs2->setNonTransitiveLbwExtCommunity(kLocalAs1, 1 * BpsPerMBps / 8);
    attrs2->setNexthop(kPeerAddr2);
    attrs2->publish();
    EXPECT_TRUE(ribEntry.updatePath(peer2, attrs2, false));

    RibBase::selectBestPath(
        ribEntry,
        multipathSelector,
        bestpathSelector,
        true,
        1024 /* ucmp-width */);
    allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
    WeightedNexthopMap nhWts = {{kPeerAddr1, 1024}};
    EXPECT_EQ(nhWts, *ribEntry.getMultipathWeightedNexthops());
  }

  /*
   * 4. LBW values exceeds the ECMP Width. In this case, we'll approximate
   *    weights to approximately fit it into specified UCMP width
   */
  {
    auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(attrs);
    attrs1->setNonTransitiveLbwExtCommunity(kLocalAs1, 1021 * BpsPerGBps / 8);
    attrs1->setNexthop(kPeerAddr1);
    attrs1->publish();
    EXPECT_TRUE(ribEntry.updatePath(peer1, attrs1, false));

    auto attrs2 = std::make_shared<facebook::bgp::BgpPath>(attrs);
    attrs2->setNonTransitiveLbwExtCommunity(kLocalAs1, 1024 * BpsPerGBps / 8);
    attrs2->setNexthop(kPeerAddr2);
    attrs2->publish();
    EXPECT_TRUE(ribEntry.updatePath(peer2, attrs2, false));

    RibBase::selectBestPath(
        ribEntry,
        multipathSelector,
        bestpathSelector,
        true,
        1024 /* ucmp-width */);
    allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
    WeightedNexthopMap nhWts = {{kPeerAddr1, 511}, {kPeerAddr2, 513}};
    EXPECT_EQ(nhWts, *ribEntry.getMultipathWeightedNexthops());
  }
}

/**
 * This test verifies all aspects of local weight aggregation
 * - All peers have link-bandwidth-bps configured
 * - One peer is missing link-bandwidth-bps
 * - Link-bandwidth-bps of peer changes
 */
TEST(RibEntryTest, AggregateLocalUcmpWeight) {
  auto attrs =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs->publish();

  TinyPeerInfo peer1(
      kPeerAddr1,
      kPeerAsn1,
      kPeerRouterId1,
      BgpSessionType::EBGP,
      false,
      false,
      10.f); // NOTE: link-bandwidth-bps = 10
  TinyPeerInfo peer1Updated(
      kPeerAddr1,
      kPeerAsn1,
      kPeerRouterId1,
      BgpSessionType::EBGP,
      false,
      false,
      100.f); // NOTE: link-bandwidth-bps = 100
  TinyPeerInfo peer2(
      kPeerAddr2,
      kPeerAsn2,
      kPeerRouterId2,
      BgpSessionType::EBGP,
      false,
      false,
      20.f); // NOTE: link-bandwidth-bps = 20
  TinyPeerInfo peer2Updated(
      kPeerAddr2,
      kPeerAsn2,
      kPeerRouterId2,
      BgpSessionType::EBGP,
      false,
      false,
      std::nullopt); // NOTE: link-bandwidth-bps is none
  RibEntry ribEntry(kV4Prefix1);
  bool bestpathChanged, nexthopChanged;

  // Case 1: both paths have link-bandwidth-bps
  EXPECT_TRUE(ribEntry.updatePath(peer1, attrs, false));
  EXPECT_TRUE(ribEntry.updatePath(peer2, attrs, false));
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  auto allocatedPathIds = getAndCheckAllocatedPathIds({}, ribEntry);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_TRUE(nexthopChanged);
  ASSERT_TRUE(ribEntry.getAggregateLocalUcmpWeight().has_value());
  EXPECT_EQ(30.f, ribEntry.getAggregateLocalUcmpWeight().value());
  EXPECT_TRUE(ribEntry.commitBestpath());

  // Case 2: No change
  EXPECT_FALSE(ribEntry.updatePath(peer1, attrs, false));
  EXPECT_FALSE(ribEntry.updatePath(peer2, attrs, false));
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
  EXPECT_FALSE(bestpathChanged);
  EXPECT_FALSE(nexthopChanged);
  ASSERT_TRUE(ribEntry.getAggregateLocalUcmpWeight().has_value());
  EXPECT_EQ(30.f, ribEntry.getAggregateLocalUcmpWeight().value());
  EXPECT_FALSE(ribEntry.commitBestpath());

  // Case 3: Update link-bandwidth-bps of peer1 to another value
  EXPECT_TRUE(ribEntry.updatePath(peer1Updated, attrs, false));
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_FALSE(nexthopChanged);
  ASSERT_TRUE(ribEntry.getAggregateLocalUcmpWeight().has_value());
  EXPECT_EQ(120.f, ribEntry.getAggregateLocalUcmpWeight().value());
  EXPECT_TRUE(ribEntry.commitBestpath());

  // Case 4: Set link-bandwidth-bps of peer2 to none
  EXPECT_TRUE(ribEntry.updatePath(peer2Updated, attrs, false));
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_FALSE(nexthopChanged);
  ASSERT_FALSE(ribEntry.getAggregateLocalUcmpWeight().has_value());
  EXPECT_TRUE(ribEntry.commitBestpath());
}

/**
 * Test AggregateLocalUcmp with Quantizer
 */
TEST(RibEntryTest, AggregateLocalUcmpWeightWithQuantizer) {
  auto attrs =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs->publish();

  TinyPeerInfo peer1( // the best path
      kPeerAddr1,
      kPeerAsn1,
      kPeerRouterId1,
      BgpSessionType::EBGP,
      false,
      false,
      100.f / 8); // NOTE: link-bandwidth-bps = 100
  TinyPeerInfo peer2(
      kPeerAddr2,
      kPeerAsn2,
      kPeerRouterId2,
      BgpSessionType::EBGP,
      false,
      false,
      3600.f / 8); // NOTE: link-bandwidth-bps = 3600
  TinyPeerInfo peer2MinorLoss(
      kPeerAddr2,
      kPeerAsn2,
      kPeerRouterId2,
      BgpSessionType::EBGP,
      false,
      false,
      3400.f / 8); // NOTE: link-bandwidth-bps = 3400
  TinyPeerInfo peer2MajorLoss(
      kPeerAddr2,
      kPeerAsn2,
      kPeerRouterId2,
      BgpSessionType::EBGP,
      false,
      false,
      3000.f / 8); // NOTE: link-bandwidth-bps = 3000
  RibEntry ribEntry(kV4Prefix1);
  bool bestpathChanged, nexthopChanged;

  /*
   * config with a quantizer with step-size 100, error (0.1), {3600} quantized
   * bps list, we expect
   */
  BgpUcmpQuantizer quantizer{100, 0.1f, {3600}};

  /*
   * Case 1: path1 (3500), path2 (100)
   * selectBestPath change
   * new weight announced 3600
   */
  EXPECT_TRUE(ribEntry.updatePath(peer1, attrs, false));
  EXPECT_TRUE(ribEntry.updatePath(peer2, attrs, false));
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0, quantizer);
  auto allocatedPathIds = getAndCheckAllocatedPathIds({}, ribEntry);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_TRUE(nexthopChanged);
  ASSERT_TRUE(ribEntry.getAggregateLocalUcmpWeight().has_value());
  EXPECT_EQ(3600.f / 8, ribEntry.getAggregateLocalUcmpWeight().value());
  EXPECT_TRUE(ribEntry.commitBestpath());

  /*
   * Case 2: peer2 update lbw 3500 -> 3400 (minor capacity loss)
   * selectBestPath: no change
   * weight: no change
   */
  EXPECT_TRUE(ribEntry.updatePath(peer2MinorLoss, attrs, false));
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0, quantizer);
  allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
  EXPECT_FALSE(bestpathChanged);
  EXPECT_FALSE(nexthopChanged);
  ASSERT_TRUE(ribEntry.getAggregateLocalUcmpWeight().has_value());
  EXPECT_EQ(3600.f / 8, ribEntry.getAggregateLocalUcmpWeight().value());
  EXPECT_FALSE(ribEntry.commitBestpath());

  /*
   * Case 3: peer2 update lbw 3400 -> 3000 (major capacity loss)
   * selectBestPath: changed due to weight update
   * weight: changed -> 3200
   */
  EXPECT_TRUE(ribEntry.updatePath(peer2MajorLoss, attrs, false));
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0, quantizer);
  allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_FALSE(nexthopChanged);
  ASSERT_TRUE(ribEntry.getAggregateLocalUcmpWeight().has_value());
  EXPECT_EQ(3200.f / 8, ribEntry.getAggregateLocalUcmpWeight().value());
  EXPECT_TRUE(ribEntry.commitBestpath());
}

TEST(RibEntryTest, UpdatePathTest) {
  RibStats::initCounters();
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs2->setNexthop(kV4Nexthop2);
  attrs1->publish();
  attrs2->publish();

  auto peer1 = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  auto peer2 = TinyPeerInfo(
      kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);

  RibEntry ribEntry(kV4Prefix1);
  // Prefix is set correctly
  EXPECT_EQ(kV4Prefix1, ribEntry.getPrefix());
  // needPathSelection_ default to false
  EXPECT_TRUE(ribEntry.needPathSelection());

  // 1. Add two paths with same attrs but different peers
  EXPECT_TRUE(ribEntry.updatePath(peer1, attrs1));
  // needPathSelection_ set to true after updatePath succeed
  EXPECT_TRUE(ribEntry.needPathSelection());

  EXPECT_TRUE(ribEntry.updatePath(peer2, attrs2));
  EXPECT_TRUE(ribEntry.needPathSelection());

  fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_EQ(
      2,
      fb303::ThreadCachedServiceData::getShared()->getCounter(
          RibStats::kTotalRibPaths));

  // 2. Duplicate update should return false
  EXPECT_FALSE(ribEntry.updatePath(peer1, attrs1));
  EXPECT_TRUE(ribEntry.needPathSelection());

  // Now, trigger the bestpath selection
  RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  auto allocatedPathIds = getAndCheckAllocatedPathIds({}, ribEntry);
  // needPathSelection_ reset to false
  EXPECT_FALSE(ribEntry.needPathSelection());

  // we should select peer1 path as bestpath
  auto bestpath = ribEntry.getBestPath();
  EXPECT_EQ(kV4Prefix1, bestpath->prefix);
  EXPECT_EQ(peer1, bestpath->peer);
  EXPECT_EQ(attrs1, bestpath->attrs);
  // two multipaths from both peers
  auto multipathNexthops = ribEntry.getMultipathWeightedNexthops();
  EXPECT_EQ(2, multipathNexthops->size());
  EXPECT_THAT(
      *multipathNexthops,
      (WeightedNexthopMap{{kV4Nexthop1, 0}, {kV4Nexthop2, 0}}));
  EXPECT_TRUE(ribEntry.getInstallToFib());
  fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_EQ(
      2,
      fb303::ThreadCachedServiceData::getShared()->getCounter(
          RibStats::kTotalRibPaths));

  // 3. Withdraw path from peer1
  EXPECT_TRUE(ribEntry.updatePath(peer1, nullptr));
  EXPECT_TRUE(ribEntry.needPathSelection());

  // Trigger bestpath selection again
  RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
  EXPECT_FALSE(ribEntry.needPathSelection());

  // peer2 path should be the best path now
  bestpath = ribEntry.getBestPath();
  EXPECT_EQ(kV4Prefix1, bestpath->prefix);
  EXPECT_EQ(peer2, bestpath->peer);
  EXPECT_EQ(attrs2, bestpath->attrs);
  // we should only have one multiple right now
  multipathNexthops = ribEntry.getMultipathWeightedNexthops();
  EXPECT_EQ(1, multipathNexthops->size());
  EXPECT_THAT(*multipathNexthops, (WeightedNexthopMap{{kV4Nexthop2, 0}}));
  EXPECT_TRUE(ribEntry.getInstallToFib());

  fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_EQ(
      1,
      fb303::ThreadCachedServiceData::getShared()->getCounter(
          RibStats::kTotalRibPaths));

  // 4. duplicate withdrawn from peer1
  EXPECT_FALSE(ribEntry.updatePath(peer1, nullptr));
  EXPECT_FALSE(ribEntry.needPathSelection());

  fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_EQ(
      1,
      fb303::ThreadCachedServiceData::getShared()->getCounter(
          RibStats::kTotalRibPaths));

  // 5. Withdraw path from peer2
  EXPECT_TRUE(ribEntry.updatePath(peer2, nullptr));
  EXPECT_TRUE(ribEntry.needPathSelection());

  // Trigger bestpath selection
  RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
  EXPECT_FALSE(ribEntry.needPathSelection());

  // bestpath and nexthops should be null
  EXPECT_EQ(nullptr, ribEntry.getBestPath());
  EXPECT_EQ(nullptr, ribEntry.getMultipathWeightedNexthops());
  fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_EQ(
      0,
      fb303::ThreadCachedServiceData::getShared()->getCounter(
          RibStats::kTotalRibPaths));
}

TEST(RibEntryTest, UpdatePathTracksBackupAddressChanges) {
  auto attrs =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  auto peer = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  const auto backupAddr1 = folly::IPAddress("2001:db8::1");
  const auto backupAddr2 = folly::IPAddress("2001:db8::2");
  RibEntry ribEntry(kV4Prefix1);
  attrs->setBackupAddr(backupAddr1);
  attrs->publish();

  EXPECT_TRUE(ribEntry.updatePath(peer, attrs));
  RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  ASSERT_NE(nullptr, ribEntry.getBestPath());
  EXPECT_EQ(backupAddr1, ribEntry.getBestPath()->attrs->getBackupAddr());
  EXPECT_FALSE(ribEntry.needPathSelection());

  EXPECT_FALSE(ribEntry.updatePath(peer, attrs));
  EXPECT_FALSE(ribEntry.needPathSelection());

  auto updatedAttrs = attrs->clone();
  updatedAttrs->setBackupAddr(backupAddr2);
  updatedAttrs->publish();
  EXPECT_TRUE(ribEntry.updatePath(peer, updatedAttrs));
  EXPECT_TRUE(ribEntry.needPathSelection());
  RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  EXPECT_EQ(backupAddr2, ribEntry.getBestPath()->attrs->getBackupAddr());

  auto attrsWithoutBackup = updatedAttrs->clone();
  attrsWithoutBackup->setBackupAddr(std::nullopt);
  attrsWithoutBackup->publish();
  EXPECT_TRUE(ribEntry.updatePath(peer, attrsWithoutBackup));
  RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  EXPECT_FALSE(ribEntry.getBestPath()->attrs->getBackupAddr().has_value());
}

TEST(RibEntryTest, UpdateAddPathTest) {
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));

  auto attrs3 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 5));
  attrs2->setNexthop(kV4Nexthop2);
  attrs3->setNexthop(kV4Nexthop3);
  attrs1->publish();
  attrs2->publish();
  attrs3->publish();

  auto peer1 = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  auto peer2 = TinyPeerInfo(
      kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);

  RibEntry ribEntry(kV4Prefix1);
  // Prefix is set correctly
  EXPECT_EQ(kV4Prefix1, ribEntry.getPrefix());
  // needPathSelection_ default to false
  EXPECT_TRUE(ribEntry.needPathSelection());

  // 1. Add two paths with same attrs but different peers
  EXPECT_TRUE(ribEntry.updatePath(peer1, attrs1, true, 0));
  // needPathSelection_ set to true after updatePath succeed
  EXPECT_TRUE(ribEntry.needPathSelection());

  EXPECT_TRUE(ribEntry.updatePath(peer2, attrs2, true, 0));
  EXPECT_TRUE(ribEntry.needPathSelection());

  // 2. Duplicate update should return false
  EXPECT_FALSE(ribEntry.updatePath(peer1, attrs1, true, 0));
  EXPECT_TRUE(ribEntry.needPathSelection());

  // 3. Add additional path from peer1
  EXPECT_TRUE(ribEntry.updatePath(peer1, attrs3, true, 1));
  EXPECT_TRUE(ribEntry.needPathSelection());

  // Now, trigger the bestpath selection
  RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  auto allocatedPathIds = getAndCheckAllocatedPathIds({}, ribEntry);
  // needPathSelection_ reset to false
  EXPECT_FALSE(ribEntry.needPathSelection());

  // we should select peer1 path as bestpath
  auto bestpath = ribEntry.getBestPath();
  EXPECT_EQ(kV4Prefix1, bestpath->prefix);
  EXPECT_EQ(peer1, bestpath->peer);
  EXPECT_EQ(attrs1, bestpath->attrs);
  // 3 multipaths from both peers
  auto multipathNexthops = ribEntry.getMultipathWeightedNexthops();
  EXPECT_EQ(3, multipathNexthops->size());
  EXPECT_THAT(
      *multipathNexthops,
      (WeightedNexthopMap{
          {kV4Nexthop1, 0}, {kV4Nexthop3, 0}, {kV4Nexthop2, 0}}));

  EXPECT_TRUE(ribEntry.getInstallToFib());

  // 3. Add additional path kV4Nexthop4 from peer2
  auto attrs4 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 5));
  attrs4->setNexthop(kV4Nexthop4);
  attrs4->publish();

  EXPECT_TRUE(ribEntry.updatePath(peer2, attrs4, true, 1));
  EXPECT_TRUE(ribEntry.needPathSelection());
  // Now, trigger the bestpath selection
  RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
  // needPathSelection_ reset to false
  EXPECT_FALSE(ribEntry.needPathSelection());
  // we should select peer1 path as bestpath
  bestpath = ribEntry.getBestPath();
  EXPECT_EQ(kV4Prefix1, bestpath->prefix);
  EXPECT_EQ(peer1, bestpath->peer);
  EXPECT_EQ(attrs1, bestpath->attrs);
  // 4 multipaths from both peers
  multipathNexthops = ribEntry.getMultipathWeightedNexthops();
  EXPECT_EQ(4, multipathNexthops->size());
  EXPECT_THAT(
      *multipathNexthops,
      (WeightedNexthopMap{
          {kV4Nexthop1, 0},
          {kV4Nexthop3, 0},
          {kV4Nexthop2, 0},
          {kV4Nexthop4, 0}}));

  // 4. Add additional path kV4Nexthop5 from peer2
  auto attrs5 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 5));
  attrs5->setNexthop(kV4Nexthop5);
  attrs5->publish();

  EXPECT_TRUE(ribEntry.updatePath(peer2, attrs5, true, 2));
  EXPECT_TRUE(ribEntry.needPathSelection());
  // Now, trigger the bestpath selection
  RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
  // needPathSelection reset to false
  EXPECT_FALSE(ribEntry.needPathSelection());
  // we should select peer1 path as bestpath
  bestpath = ribEntry.getBestPath();
  EXPECT_EQ(kV4Prefix1, bestpath->prefix);
  EXPECT_EQ(peer1, bestpath->peer);
  EXPECT_EQ(attrs1, bestpath->attrs);
  // 5 multipaths from both peers
  multipathNexthops = ribEntry.getMultipathWeightedNexthops();
  EXPECT_EQ(5, multipathNexthops->size());
  EXPECT_THAT(
      *multipathNexthops,
      (WeightedNexthopMap{
          {kV4Nexthop1, 0},
          {kV4Nexthop3, 0},
          {kV4Nexthop2, 0},
          {kV4Nexthop4, 0},
          {kV4Nexthop5, 0}}));

  // 5. Withdraw kV4Nexthop1 from peer1
  EXPECT_TRUE(ribEntry.updatePath(peer1, nullptr, true, 0));
  EXPECT_TRUE(ribEntry.needPathSelection());

  // Trigger bestpath selection again
  RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
  EXPECT_FALSE(ribEntry.needPathSelection());

  // peer1 path should be the best path now
  bestpath = ribEntry.getBestPath();
  EXPECT_EQ(kV4Prefix1, bestpath->prefix);
  EXPECT_EQ(peer1, bestpath->peer);
  EXPECT_EQ(attrs3, bestpath->attrs);
  // we should only have 4 multiple right now
  multipathNexthops = ribEntry.getMultipathWeightedNexthops();
  EXPECT_EQ(4, multipathNexthops->size());
  EXPECT_THAT(
      *multipathNexthops,
      (WeightedNexthopMap{
          {kV4Nexthop3, 0},
          {kV4Nexthop2, 0},
          {kV4Nexthop4, 0},
          {kV4Nexthop5, 0}}));
  EXPECT_TRUE(ribEntry.getInstallToFib());

  // 6. duplicate withdrawn from peer1
  EXPECT_FALSE(ribEntry.updatePath(peer1, nullptr, true, 0));
  EXPECT_FALSE(ribEntry.needPathSelection());

  // 7. Withdraw kV4Nexthop4 from peer2
  EXPECT_TRUE(ribEntry.updatePath(peer2, nullptr, true, 1));
  EXPECT_TRUE(ribEntry.needPathSelection());

  // Trigger bestpath selection
  RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);
  EXPECT_FALSE(ribEntry.needPathSelection());

  // peer1 path should be the best path now
  bestpath = ribEntry.getBestPath();
  EXPECT_EQ(kV4Prefix1, bestpath->prefix);
  EXPECT_EQ(peer1, bestpath->peer);
  EXPECT_EQ(attrs3, bestpath->attrs);
  // we should only have 3 multiple right now
  multipathNexthops = ribEntry.getMultipathWeightedNexthops();
  EXPECT_EQ(3, multipathNexthops->size());
  EXPECT_THAT(
      *multipathNexthops,
      (WeightedNexthopMap{
          {kV4Nexthop3, 0}, {kV4Nexthop2, 0}, {kV4Nexthop5, 0}}));
  EXPECT_TRUE(ribEntry.getInstallToFib());
}

TEST(RibEntryTest, UpdatePathMixedAddPathTest) {
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  auto attrs3 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 5));
  attrs2->setNexthop(kV4Nexthop2);
  attrs3->setNexthop(kV4Nexthop3);
  attrs1->publish();
  attrs2->publish();
  attrs3->publish();

  auto peer1 = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  auto peer2 = TinyPeerInfo(
      kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);

  RibEntry ribEntry(kV4Prefix1);

  // 1. Add path from peer1 with addPath=false
  EXPECT_TRUE(ribEntry.updatePath(peer1, attrs1, true, kDefaultPathID));

  // 2. Add two paths from peer2 with addPath=true
  EXPECT_TRUE(ribEntry.updatePath(peer2, attrs2, true, 0));
  EXPECT_TRUE(ribEntry.updatePath(peer2, attrs3, true, 1));

  // verify ribEntry routeInfos has path for peer1 and both paths for peer2
  auto peer1Routes = ribEntry.getRouteInfos(
      nettools::bgplib::BgpPeerId(peer1.addr, peer1.routerId));
  auto peer2Routes = ribEntry.getRouteInfos(
      nettools::bgplib::BgpPeerId(peer2.addr, peer2.routerId));
  EXPECT_TRUE(peer1Routes.contains(kDefaultPathID));
  EXPECT_TRUE(peer2Routes.contains(0));
  EXPECT_TRUE(peer2Routes.contains(1));
  EXPECT_EQ(peer1Routes[kDefaultPathID]->attrs, attrs1);
  EXPECT_EQ(peer2Routes[0]->attrs, attrs2);
  EXPECT_EQ(peer2Routes[1]->attrs, attrs3);
}

TEST(RibEntryTest, commitBestpathTest) {
  auto attrs =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs->publish();
  auto peer1 = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  auto peer2 = TinyPeerInfo(
      kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);

  RibEntry ribEntry(kV4Prefix1);

  // Prefix is set correctly
  EXPECT_EQ(kV4Prefix1, ribEntry.getPrefix());

  EXPECT_TRUE(ribEntry.updatePath(peer1, attrs));
  EXPECT_TRUE(ribEntry.updatePath(peer2, attrs));

  // Now, trigger the bestpath selection
  RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  auto allocatedPathIds = getAndCheckAllocatedPathIds({}, ribEntry);

  /*
   * advertisedBestpath and advertisedMultipathNexthops should still be
   * nullptr before commitBestpathSelection happens
   */
  EXPECT_EQ(nullptr, ribEntry.getAdvertisedBestPath());
  EXPECT_EQ(nullptr, ribEntry.getAdvertisedMultipathWeightedNexthops());

  auto bestpath = ribEntry.getBestPath();
  EXPECT_EQ(kV4Prefix1, bestpath->prefix);
  EXPECT_EQ(peer1, bestpath->peer);
  EXPECT_EQ(attrs, bestpath->attrs);
  auto multipathNexthops = ribEntry.getMultipathWeightedNexthops();
  /*
   * Because of the nexthops for both paths are same, there is only one
   * nexthop
   */
  EXPECT_EQ(1, multipathNexthops->size());
  EXPECT_THAT(*multipathNexthops, (WeightedNexthopMap{{kV4Nexthop1, 0}}));
  EXPECT_TRUE(ribEntry.getInstallToFib());

  // commit bestpath selection
  EXPECT_TRUE(ribEntry.commitBestpath());
  EXPECT_TRUE(ribEntry.commitMultipathNexthops());

  // check advertisedBestpath and multipathNexthops have the correct value
  EXPECT_EQ(ribEntry.getBestPath(), ribEntry.getAdvertisedBestPath());
  EXPECT_EQ(
      ribEntry.getMultipathWeightedNexthops(),
      ribEntry.getAdvertisedMultipathWeightedNexthops());

  // duplicate commit call should return false
  EXPECT_FALSE(ribEntry.commitBestpath());
  EXPECT_FALSE(ribEntry.commitMultipathNexthops());

  // Withdraw path from peer1 and peer2
  EXPECT_TRUE(ribEntry.updatePath(peer1, nullptr));
  EXPECT_TRUE(ribEntry.updatePath(peer2, nullptr));

  // trigger bestpath selection again
  RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  allocatedPathIds = getAndCheckAllocatedPathIds(allocatedPathIds, ribEntry);

  EXPECT_EQ(nullptr, ribEntry.getBestPath());
  EXPECT_EQ(nullptr, ribEntry.getMultipathWeightedNexthops());

  // check advertisedBestpath and advertisedMultipathNexthops still have value
  auto advertisedBestpath = ribEntry.getAdvertisedBestPath();
  EXPECT_EQ(kV4Prefix1, advertisedBestpath->prefix);
  EXPECT_EQ(peer1, advertisedBestpath->peer);
  EXPECT_EQ(attrs, advertisedBestpath->attrs);
  auto advertisedMultipathNexthops =
      ribEntry.getAdvertisedMultipathWeightedNexthops();
  EXPECT_EQ(1, advertisedMultipathNexthops->size());
  EXPECT_THAT(
      *advertisedMultipathNexthops, (WeightedNexthopMap{{kV4Nexthop1, 0}}));
  EXPECT_TRUE(ribEntry.getInstallToFib());

  EXPECT_TRUE(ribEntry.commitBestpath());
  EXPECT_TRUE(ribEntry.commitMultipathNexthops());
  EXPECT_FALSE(ribEntry.commitBestpath());
  EXPECT_FALSE(ribEntry.commitMultipathNexthops());

  EXPECT_EQ(nullptr, ribEntry.getAdvertisedBestPath());
  EXPECT_EQ(nullptr, ribEntry.getAdvertisedMultipathWeightedNexthops());
}

/**
 * Test selectBestPath with NextHop tracking functionality
 * This test verifies the change that filters routes based on next hop
 * reachability when enableNextHopTracking is enabled.
 */
TEST(RibEntryTest, SelectBestPathWithNextHopTrackingTest) {
  // Helper function to load BGP bestpath features
  auto loadBgpBestpathFeatures = [](bool enableNextHopTracking) {
    thrift::BgpConfig thriftConfig;
    thrift::BgpSettingConfig tBgpSettingConfig;
    tBgpSettingConfig.enable_med_comparison() = false;
    tBgpSettingConfig.enable_med_missing_as_worst() = false;
    tBgpSettingConfig.enable_weight_comparison() = false;
    tBgpSettingConfig.enable_next_hop_tracking() = enableNextHopTracking;
    thriftConfig.bgp_setting_config() = std::move(tBgpSettingConfig);
    FeatureFlags::LoadFromThriftConfig(thriftConfig);
  };

  // Helper function to create BGP path with specific nexthop
  auto createBgpPath = [](const folly::IPAddress& nexthop) {
    auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
    attrs->setNexthop(nexthop);
    attrs->publish();
    return attrs;
  };

  // Helper function to add path with NexthopInfo
  auto addPathWithNexthopInfo = [](RibEntry& ribEntry,
                                   const TinyPeerInfo& peer,
                                   std::shared_ptr<BgpPath> attrs,
                                   bool isReachable,
                                   uint32_t pathId = kDefaultPathID) {
    // Create NexthopInfo
    auto nexthopInfo = std::make_shared<NexthopInfo>(NexthopStatus(
        {.ip = attrs->getNexthop()}, isReachable, 100 /* igpCost */));

    // Add the path to RibEntry
    EXPECT_TRUE(
        ribEntry.updatePath(peer, attrs, true, pathId, nexthopInfo.get()));

    // Get the RouteInfo and set the NexthopInfo
    auto routeInfos = ribEntry.getRouteInfos(
        nettools::bgplib::BgpPeerId{peer.addr, peer.routerId});
    EXPECT_TRUE(routeInfos.contains(pathId));
    routeInfos[pathId]->setNexthopInfo(nexthopInfo.get());

    return nexthopInfo;
  };

  // Test Case 1: NextHop tracking DISABLED - all routes should be included
  {
    loadBgpBestpathFeatures(false); // Disable next hop tracking

    RibEntry ribEntry(kV4Prefix1);

    // Create peers
    auto peer1 = TinyPeerInfo(
        kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
    auto peer2 = TinyPeerInfo(
        kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);

    // Create BGP paths with different nexthops
    auto attrs1 = createBgpPath(kV4Nexthop1);
    auto attrs2 = createBgpPath(kV4Nexthop2);

    // Add paths - one reachable, one unreachable
    auto nexthopInfo1 =
        addPathWithNexthopInfo(ribEntry, peer1, attrs1, true); // reachable
    auto nexthopInfo2 =
        addPathWithNexthopInfo(ribEntry, peer2, attrs2, false); // unreachable

    // Trigger best path selection
    bool bestpathChanged, nexthopChanged;
    std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
        ribEntry, multipathSelector, bestpathSelector, false, 0);
    getAndCheckAllocatedPathIds({}, ribEntry);

    EXPECT_TRUE(bestpathChanged);
    EXPECT_TRUE(nexthopChanged);

    /*
     * Both paths should be selected as multipaths since NextHop tracking is
     * disabled
     */
    auto multipathNexthops = ribEntry.getMultipathWeightedNexthops();
    EXPECT_EQ(2, multipathNexthops->size());
    EXPECT_THAT(
        *multipathNexthops,
        (WeightedNexthopMap{{kV4Nexthop1, 0}, {kV4Nexthop2, 0}}));

    // Best path should be selected (peer1 due to lower router ID)
    auto bestpath = ribEntry.getBestPath();
    EXPECT_NE(nullptr, bestpath);
    EXPECT_EQ(peer1, bestpath->peer);
    EXPECT_EQ(attrs1, bestpath->attrs);
  }

  // Test Case 2: NextHop tracking ENABLED with all reachable nexthops
  {
    loadBgpBestpathFeatures(true); // Enable next hop tracking

    RibEntry ribEntry(kV4Prefix1);

    // Create peers
    auto peer1 = TinyPeerInfo(
        kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
    auto peer2 = TinyPeerInfo(
        kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);

    // Create BGP paths with different nexthops
    auto attrs1 = createBgpPath(kV4Nexthop1);
    auto attrs2 = createBgpPath(kV4Nexthop2);

    // Add paths - both reachable
    auto nexthopInfo1 =
        addPathWithNexthopInfo(ribEntry, peer1, attrs1, true); // reachable
    auto nexthopInfo2 =
        addPathWithNexthopInfo(ribEntry, peer2, attrs2, true); // reachable

    // Trigger best path selection
    bool bestpathChanged, nexthopChanged;
    std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
        ribEntry, multipathSelector, bestpathSelector, false, 0);
    getAndCheckAllocatedPathIds({}, ribEntry);

    EXPECT_TRUE(bestpathChanged);
    EXPECT_TRUE(nexthopChanged);

    // Both paths should be selected as multipaths since both are reachable
    auto multipathNexthops = ribEntry.getMultipathWeightedNexthops();
    EXPECT_EQ(2, multipathNexthops->size());
    EXPECT_THAT(
        *multipathNexthops,
        (WeightedNexthopMap{{kV4Nexthop1, 0}, {kV4Nexthop2, 0}}));

    // Best path should be selected (peer1 due to lower router ID)
    auto bestpath = ribEntry.getBestPath();
    EXPECT_NE(nullptr, bestpath);
    EXPECT_EQ(peer1, bestpath->peer);
    EXPECT_EQ(attrs1, bestpath->attrs);
  }

  // Test Case 3: NextHop tracking ENABLED with one unreachable nexthop
  {
    loadBgpBestpathFeatures(true); // Enable next hop tracking

    RibEntry ribEntry(kV4Prefix1);

    // Create peers
    auto peer1 = TinyPeerInfo(
        kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
    auto peer2 = TinyPeerInfo(
        kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);

    // Create BGP paths with different nexthops
    auto attrs1 = createBgpPath(kV4Nexthop1);
    auto attrs2 = createBgpPath(kV4Nexthop2);

    // Add paths - one reachable, one unreachable
    auto nexthopInfo1 =
        addPathWithNexthopInfo(ribEntry, peer1, attrs1, true); // reachable
    auto nexthopInfo2 =
        addPathWithNexthopInfo(ribEntry, peer2, attrs2, false); // unreachable

    // Trigger best path selection
    bool bestpathChanged, nexthopChanged;
    std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
        ribEntry, multipathSelector, bestpathSelector, false, 0);
    getAndCheckAllocatedPathIds({}, ribEntry);

    EXPECT_TRUE(bestpathChanged);
    EXPECT_TRUE(nexthopChanged);

    // Only the reachable path should be selected as multipath
    auto multipathNexthops = ribEntry.getMultipathWeightedNexthops();
    EXPECT_EQ(1, multipathNexthops->size());
    EXPECT_THAT(*multipathNexthops, (WeightedNexthopMap{{kV4Nexthop1, 0}}));

    // Best path should be the reachable one
    auto bestpath = ribEntry.getBestPath();
    EXPECT_NE(nullptr, bestpath);
    EXPECT_EQ(peer1, bestpath->peer);
    EXPECT_EQ(attrs1, bestpath->attrs);
  }

  // Test Case 4: NextHop tracking ENABLED with all unreachable nexthops
  {
    loadBgpBestpathFeatures(true); // Enable next hop tracking

    RibEntry ribEntry(kV4Prefix1);

    // Create peers
    auto peer1 = TinyPeerInfo(
        kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
    auto peer2 = TinyPeerInfo(
        kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);

    // Create BGP paths with different nexthops
    auto attrs1 = createBgpPath(kV4Nexthop1);
    auto attrs2 = createBgpPath(kV4Nexthop2);

    // Add paths - both unreachable
    auto nexthopInfo1 =
        addPathWithNexthopInfo(ribEntry, peer1, attrs1, false); // unreachable
    auto nexthopInfo2 =
        addPathWithNexthopInfo(ribEntry, peer2, attrs2, false); // unreachable

    // Trigger best path selection
    bool bestpathChanged, nexthopChanged;
    std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
        ribEntry, multipathSelector, bestpathSelector, false, 0);
    getAndCheckAllocatedPathIds({}, ribEntry);

    EXPECT_FALSE(bestpathChanged);
    EXPECT_FALSE(nexthopChanged);

    // No paths should be selected since all are unreachable
    auto multipathNexthops = ribEntry.getMultipathWeightedNexthops();
    EXPECT_EQ(nullptr, multipathNexthops);

    // No best path should be selected
    auto bestpath = ribEntry.getBestPath();
    EXPECT_EQ(nullptr, bestpath);
  }

  // Test Case 5: NextHop tracking ENABLED with multiple paths from same peer
  {
    loadBgpBestpathFeatures(true); // Enable next hop tracking

    RibEntry ribEntry(kV4Prefix1);

    // Create peer
    auto peer1 = TinyPeerInfo(
        kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);

    // Create BGP paths with different nexthops
    auto attrs1 = createBgpPath(kV4Nexthop1);
    auto attrs2 = createBgpPath(kV4Nexthop2);
    auto attrs3 = createBgpPath(kV4Nexthop3);

    // Add paths with different reachability - using addPath
    auto nexthopInfo1 =
        addPathWithNexthopInfo(ribEntry, peer1, attrs1, true, 0); // reachable
    auto nexthopInfo2 = addPathWithNexthopInfo(
        ribEntry, peer1, attrs2, false, 1); // unreachable
    auto nexthopInfo3 =
        addPathWithNexthopInfo(ribEntry, peer1, attrs3, true, 2); // reachable

    // Trigger best path selection
    bool bestpathChanged, nexthopChanged;
    std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
        ribEntry, multipathSelector, bestpathSelector, false, 0);
    getAndCheckAllocatedPathIds({}, ribEntry);

    EXPECT_TRUE(bestpathChanged);
    EXPECT_TRUE(nexthopChanged);

    // Only the reachable paths should be selected as multipaths
    auto multipathNexthops = ribEntry.getMultipathWeightedNexthops();
    EXPECT_EQ(2, multipathNexthops->size());
    EXPECT_THAT(
        *multipathNexthops,
        (WeightedNexthopMap{{kV4Nexthop1, 0}, {kV4Nexthop3, 0}}));

    // Best path should be one of the reachable ones (first one due to path ID)
    auto bestpath = ribEntry.getBestPath();
    EXPECT_NE(nullptr, bestpath);
    EXPECT_EQ(peer1, bestpath->peer);
    EXPECT_EQ(attrs1, bestpath->attrs);
  }
}

/*
 * two paths with no pre-existing pathID assignment would get two unique
 * assignments upon being selected as best paths
 */
TEST(RibEntryTest, SelectBestPathAssignsPathIdsToSend) {
  // create two identical paths from two different peers
  auto attrs =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs->publish();
  auto peer1 = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  auto peer2 = TinyPeerInfo(
      kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);
  nettools::bgplib::BgpPeerId peer1Id{peer1.addr, peer1.routerId};
  nettools::bgplib::BgpPeerId peer2Id{peer2.addr, peer2.routerId};
  RibEntry ribEntry(kV4Prefix1);
  EXPECT_TRUE(ribEntry.updatePath(peer1, attrs));
  EXPECT_TRUE(ribEntry.updatePath(peer2, attrs));

  // new paths should have no path ID assignment yet
  EXPECT_EQ(
      ribEntry.routeInfos_[peer1Id][kDefaultPathID]->pathIdToSend,
      std::nullopt);
  EXPECT_EQ(
      ribEntry.routeInfos_[peer2Id][kDefaultPathID]->pathIdToSend,
      std::nullopt);

  // run best path selection. both should get selected
  RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  EXPECT_EQ(ribEntry.getMultipaths().size(), 2);

  // now due to being selected, both have unique assignments
  auto path1Id = ribEntry.routeInfos_[peer1Id][kDefaultPathID]->pathIdToSend;
  auto path2Id = ribEntry.routeInfos_[peer2Id][kDefaultPathID]->pathIdToSend;
  EXPECT_NE(path1Id, std::nullopt);
  EXPECT_NE(path2Id, std::nullopt);
  EXPECT_NE(path1Id, path2Id);
}

/*
 * ribEntry.updatePath instantiates a new RouteInfo when an existing path is
 * updated. We want to make sure pathIdToSend is not lost
 */
TEST(RibEntryTest, UpdatePathPreservesPathIdToSend) {
  // create a routeInfo in ribEntry and set its pathId to something, say 4
  auto attrs =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs->publish();
  auto peer1 = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  nettools::bgplib::BgpPeerId peer1Id{peer1.addr, peer1.routerId};
  RibEntry ribEntry(kV4Prefix1);
  ribEntry.updatePath(peer1, attrs);
  ribEntry.routeInfos_[peer1Id][kDefaultPathID]->pathIdToSend = 4;

  /*
   * now if an update comes in for this path, it should have the same allocated
   * pathID
   */
  attrs =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(5, 4, 4, 4));
  attrs->publish();
  ribEntry.updatePath(peer1, attrs);
  EXPECT_EQ(ribEntry.routeInfos_[peer1Id][kDefaultPathID]->pathIdToSend, 4);
}

/*
 * if a path has a pathIdToSend already allocated, best path selection should
 * not change the allocated ID, whether the path is selected or not.
 * if a previously selected path is not selected, we still want to preserve
 * the path ID at least until the corresponding RibOut message is created
 */
TEST(RibEntryTest, SelectBestPathPreservesPathIdsToSend) {
  /*
   * create two paths from different peers. The one from peer 2 has longer
   * AS-PATH so that it won't be selected
   */
  auto attrs =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs->publish();
  auto peer1 = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  auto peer2 = TinyPeerInfo(
      kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);
  nettools::bgplib::BgpPeerId peer1Id{peer1.addr, peer1.routerId};
  nettools::bgplib::BgpPeerId peer2Id{peer2.addr, peer2.routerId};
  RibEntry ribEntry(kV4Prefix1);
  EXPECT_TRUE(ribEntry.updatePath(peer1, attrs));
  attrs =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(5, 4, 4, 4));
  attrs->publish();
  EXPECT_TRUE(ribEntry.updatePath(peer2, attrs));

  // manually set their pathIdToSend values
  ribEntry.routeInfos_[peer1Id][kDefaultPathID]->pathIdToSend = 4;
  ribEntry.routeInfos_[peer2Id][kDefaultPathID]->pathIdToSend = 5;

  /*
   * run selectBestPath and make sure the IDs are preserved, for both the
   * selected path and the non-selected one
   */
  RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  EXPECT_EQ(ribEntry.getMultipaths().size(), 1);
  EXPECT_EQ(ribEntry.routeInfos_[peer1Id][kDefaultPathID]->pathIdToSend, 4);
  EXPECT_EQ(ribEntry.routeInfos_[peer2Id][kDefaultPathID]->pathIdToSend, 5);
}

/* Test whether a route has an IGP cost. */
TEST(RibEntryTest, HasIgpCostTest) {
  auto attrs = std::make_shared<facebook::bgp::BgpPath>(
      *buildBgpPathFields(4, 4, 4, 4, 0, kV4Nexthop1));
  attrs->publish();

  RibEntry ribEntry(kV4Prefix1);

  // Create a local route using the local route peer address
  auto localPeer = TinyPeerInfo(
      kLocalRoutePeerAddr,
      kLocalRouteAs,
      kLocalRoutePeerRouterId,
      BgpSessionType::IBGP,
      false);
  EXPECT_TRUE(ribEntry.updatePath(localPeer, attrs));
  nettools::bgplib::BgpPeerId localPeerId{localPeer.addr, localPeer.routerId};
  auto localRouteInfo = ribEntry.routeInfos_[localPeerId][kDefaultPathID];

  // Local routes without nexthop information have no IGP cost.
  EXPECT_TRUE(localRouteInfo->getIsRouteLocal());
  EXPECT_TRUE(localRouteInfo->isNexthopReachable());
  EXPECT_FALSE(localRouteInfo->hasIgpCost());
  EXPECT_TRUE(localRouteInfo->isResolvedForSelection());

  // Create a non-local route (EBGP)
  RibEntry ribEntry2(kV4Prefix2);
  auto ebgpPeer = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  EXPECT_TRUE(ribEntry2.updatePath(ebgpPeer, attrs));
  nettools::bgplib::BgpPeerId ebgpPeerId{ebgpPeer.addr, ebgpPeer.routerId};
  auto ebgpRouteInfo = ribEntry2.routeInfos_[ebgpPeerId][kDefaultPathID];

  // A non-local route also needs linked nexthop information for a cost.
  EXPECT_FALSE(ebgpRouteInfo->getIsRouteLocal());
  EXPECT_FALSE(ebgpRouteInfo->isNexthopReachable());
  EXPECT_FALSE(ebgpRouteInfo->hasIgpCost());

  // Set up a reachable nexthop for the non-local route
  NexthopInfo reachableNexthopInfo(NexthopStatus(
      {.ip = kV4Nexthop1},
      true, // isReachable
      100)); // igpCost
  ebgpRouteInfo->setNexthopInfo(&reachableNexthopInfo);
  EXPECT_TRUE(ebgpRouteInfo->isNexthopReachable());
  EXPECT_TRUE(ebgpRouteInfo->hasIgpCost());

  // Source reachability alone does not provide an IGP cost.
  NexthopInfo reachableWithoutCost(
      NexthopStatus({.ip = kV4Nexthop1}, true, std::nullopt));
  ebgpRouteInfo->setNexthopInfo(&reachableWithoutCost);
  EXPECT_TRUE(ebgpRouteInfo->isNexthopReachable());
  EXPECT_FALSE(ebgpRouteInfo->hasIgpCost());

  // Set up an unreachable nexthop (no IGP cost)
  NexthopInfo unreachableNexthopInfo(NexthopStatus(
      {.ip = kV4Nexthop1},
      false, // isReachable
      std::nullopt)); // igpCost
  ebgpRouteInfo->setNexthopInfo(&unreachableNexthopInfo);
  EXPECT_FALSE(ebgpRouteInfo->isNexthopReachable());
  EXPECT_FALSE(ebgpRouteInfo->hasIgpCost());

  // Clean up the pointer before NexthopInfo goes out of scope
  ebgpRouteInfo->setNexthopInfo(nullptr);
}

class PathIdGeneratorFixture : public ::testing::Test {
 protected:
  RibEntry ribEntry_ = RibEntry(kDefaultV4);

  std::pair<uint32_t, uint32_t> getInterval() {
    return ribEntry_.freePathIdInterval_;
  }

  void setInterval(std::pair<uint32_t, uint32_t> interval) {
    ribEntry_.freePathIdInterval_ = interval;
  }

  void getAndCheckId(uint32_t expectedId, int lineNum) {
    auto id = ribEntry_.getPathIdToSend();
    EXPECT_EQ(id, expectedId) << lineNum;
  }

  /*
   * we test useLargestFreeInterval with various pathID vectors which are
   * initialized in sorted order for readability, but they should be unordered
   * for testing purposes, hence this convenient helper
   */
  void shuffle(std::vector<uint32_t>& vec) {
    unsigned seed = 123; // we don't need a different order every run
    std::shuffle(vec.begin(), vec.end(), std::default_random_engine(seed));
  }

  void useAndCheckLargestFreeInterval(
      std::vector<uint32_t>& pathIds,
      std::pair<uint32_t, uint32_t> expectedInterval,
      uint32_t minPathId,
      uint32_t maxPathId,
      int lineNum) {
    shuffle(pathIds);
    folly::F14NodeMap<
        nettools::bgplib::BgpPeerId,
        folly::F14NodeMap<uint32_t, std::shared_ptr<RouteInfo>>>
        routeInfos;
    folly::F14NodeMap<uint32_t, std::shared_ptr<RouteInfo>> paths;
    uint32_t dummyRcvdPathId = 0;
    for (auto id : pathIds) {
      auto info = createRouteInfo(kV4Prefix1, kLocalRoutePeerAddr, kV4Nexthop1);
      info->pathIdToSend = id;
      auto info2 = createRouteInfo(
          kV4Prefix1,
          kLocalRoutePeerAddr,
          kV4Nexthop1); // add a routeInfo with no assigned pathID. These
                        // shouldn't affect findLargestFreePathIdInterval
      paths.insert_or_assign(dummyRcvdPathId++, std::move(info));
      paths.insert_or_assign(dummyRcvdPathId++, std::move(info2));
    }
    routeInfos.insert_or_assign(kPeerId1, paths);
    ribEntry_.routeInfos_ = routeInfos;
    ribEntry_.freePathIdInterval_ = findLargestFreePathIdInterval(
        ribEntry_.routeInfos_, minPathId, maxPathId);
    EXPECT_EQ(ribEntry_.freePathIdInterval_, expectedInterval) << lineNum;
  }
};

TEST_F(PathIdGeneratorFixture, GetPathIdToSendTest) {
  auto minId = 0;
  auto maxId = 5;

  /*
   * generate all IDs from min to max for some prefix's interval. Ensure the
   * given interval is shrinking as expected as IDs are generated
   */
  std::pair<uint32_t, uint32_t> interval = std::make_pair(minId, maxId);
  setInterval(interval);
  for (int i = minId; i <= maxId; i++) {
    getAndCheckId(i, __LINE__);
    auto expectedInterval = std::make_pair<uint32_t, uint32_t>(i + 1, maxId);
    EXPECT_EQ(getInterval(), expectedInterval);
  }

  /*
   * now the interval should be exhausted. Additional calls to getPathIdToSend
   * will reset the interval with no path currently being sent (no routeInfos in
   * ribEntry_)
   * ribEntry_ doesn't know about minId here, so technically it would reset to
   * kMinpathIDToSend, the default
   */
  auto exhaustedInterval = std::make_pair<uint32_t, uint32_t>(maxId + 1, maxId);
  EXPECT_EQ(getInterval(), exhaustedInterval);
  EXPECT_EQ(ribEntry_.getPathIdToSend(), kMinPathIDToSend);
}

TEST_F(PathIdGeneratorFixture, GetPathIdAfterExhaustionTest) {
  auto minId = 0;
  auto maxId = 5;

  /*
   * generate all IDs from min to max for some prefix's interval. Ensure the
   * given interval is shrinking as expected as IDs are generated. Afterwards,
   * interval is exhausted
   */
  std::pair<uint32_t, uint32_t> interval = std::make_pair(minId, maxId);
  std::pair<uint32_t, uint32_t> exhaustedInterval =
      std::make_pair(maxId + 1, maxId);
  setInterval(interval);
  std::pair<uint32_t, uint32_t> expectedInterval;
  for (int i = minId; i <= maxId; i++) {
    getAndCheckId(i, __LINE__);
    expectedInterval = std::make_pair<uint32_t, uint32_t>(i + 1, maxId);
    EXPECT_EQ(getInterval(), expectedInterval);
  }
  EXPECT_EQ(getInterval(), exhaustedInterval);

  /*
   * re-generate the interval by invoking findLargestFreePathIdInterval directly
   * with a few pathIDs, say 0,1,5. Largest interval would be [2, 4]. we have to
   * call the method directly because internally wider bounds are used for
   * pathID
   */
  expectedInterval = std::make_pair<uint32_t, uint32_t>(2, 4);
  exhaustedInterval = std::make_pair<uint32_t, uint32_t>(5, 4);
  auto sentPathIds = std::vector<uint32_t>{0, 1, 5};
  useAndCheckLargestFreeInterval(
      sentPathIds, expectedInterval, minId, maxId, __LINE__);

  /*
   * now we should be able to again generate all path IDs from 2 to 4, with
   * interval shrinking as expected and ultimately becoming exhausted
   */
  for (int i = expectedInterval.first; i <= expectedInterval.second; i++) {
    getAndCheckId(i, __LINE__);
    expectedInterval = std::make_pair<uint32_t, uint32_t>(i + 1, 4);
    EXPECT_EQ(getInterval(), expectedInterval);
  }
  EXPECT_EQ(getInterval(), exhaustedInterval);
}

} // namespace facebook::bgp
