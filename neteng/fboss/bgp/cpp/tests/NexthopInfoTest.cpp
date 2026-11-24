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

#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopInfo.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopStatus.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"

using namespace facebook::bgp;
using namespace testing;

class NexthopInfoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a NexthopStatus with a known IGP cost
    folly::IPAddress nexthopIp("2620:0:1cff:dead:bef1:ffff:ffff:1");
    nexthopStatus_ =
        std::make_unique<NexthopStatus>(NexthopKey{.ip = nexthopIp}, true, 100);

    // Create a NexthopInfo with the nexthop status
    nexthopInfo_ = std::make_unique<NexthopInfo>(*nexthopStatus_);
  }

  std::unique_ptr<NexthopStatus> nexthopStatus_;
  std::unique_ptr<NexthopInfo> nexthopInfo_;
};

// Test that RouteInfo correctly gets IGP cost from NexthopInfo
TEST_F(NexthopInfoTest, RouteInfoGetsIgpCostFromNexthopInfo) {
  // Create a RouteInfo
  auto routeInfo = createRouteInfo(
      kV4Prefix1,
      kPeerAddr2,
      kPeerAddr1,
      kLocalPref,
      {},
      kPeerAsn2,
      kPeerRouterId2);

  // Initially, the IGP cost should be max value since no NexthopInfo is linked
  EXPECT_EQ(routeInfo->getIgpCostValue(), std::numeric_limits<uint32_t>::max());

  // Link the RouteInfo to the NexthopInfo
  nexthopInfo_->linkRouteInfo(*routeInfo);

  // Now the IGP cost should come from the NexthopInfo
  EXPECT_EQ(routeInfo->getIgpCostValue(), 100);

  // Unlink the RouteInfo
  nexthopInfo_->unlinkRouteInfo(*routeInfo);

  // After unlinking, the IGP cost should be max value again
  EXPECT_EQ(routeInfo->getIgpCostValue(), std::numeric_limits<uint32_t>::max());
}

// Test that NexthopInfo correctly tracks linked RouteInfo objects
TEST_F(NexthopInfoTest, NexthopInfoTracksLinkedRouteInfo) {
  // Create RouteInfo objects
  auto routeInfo1 = createRouteInfo(
      kV4Prefix1,
      kPeerAddr2,
      kPeerAddr1,
      kLocalPref,
      {},
      kPeerAsn2,
      kPeerRouterId2);

  auto routeInfo2 = createRouteInfo(
      kV6Prefix1,
      kPeerAddr2,
      kPeerAddr1,
      kLocalPref,
      {},
      kPeerAsn2,
      kPeerRouterId2);

  // Initially, the association list should be empty
  EXPECT_EQ(nexthopInfo_->getRouteInfoListSize(), 0);

  // Link the first RouteInfo
  nexthopInfo_->linkRouteInfo(*routeInfo1);
  EXPECT_EQ(nexthopInfo_->getRouteInfoListSize(), 1);

  // Link the second RouteInfo
  nexthopInfo_->linkRouteInfo(*routeInfo2);
  EXPECT_EQ(nexthopInfo_->getRouteInfoListSize(), 2);

  // Unlink the first RouteInfo
  nexthopInfo_->unlinkRouteInfo(*routeInfo1);
  EXPECT_EQ(nexthopInfo_->getRouteInfoListSize(), 1);

  // Unlink the second RouteInfo
  nexthopInfo_->unlinkRouteInfo(*routeInfo2);
  EXPECT_EQ(nexthopInfo_->getRouteInfoListSize(), 0);
}

TEST_F(NexthopInfoTest, DestroyingRouteInfoUnlinksAssociation) {
  auto routeInfo = createRouteInfo(
      kV4Prefix1,
      kPeerAddr2,
      kPeerAddr1,
      kLocalPref,
      {},
      kPeerAsn2,
      kPeerRouterId2);
  nexthopInfo_->linkRouteInfo(*routeInfo);
  ASSERT_EQ(nexthopInfo_->getRouteInfoListSize(), 1);

  routeInfo.reset();

  EXPECT_TRUE(nexthopInfo_->isRouteInfoListEmpty());
  EXPECT_EQ(nexthopInfo_->getRouteInfoListSize(), 0);
}

TEST_F(NexthopInfoTest, DestroyingNexthopInfoUnlinksAssociation) {
  auto routeInfo = createRouteInfo(
      kV4Prefix1,
      kPeerAddr2,
      kPeerAddr1,
      kLocalPref,
      {},
      kPeerAsn2,
      kPeerRouterId2);
  nexthopInfo_->linkRouteInfo(*routeInfo);
  ASSERT_TRUE(routeInfo->isOnNextHopList());

  nexthopInfo_.reset();

  EXPECT_FALSE(routeInfo->isOnNextHopList());
  EXPECT_EQ(routeInfo->getIgpCostValue(), std::numeric_limits<uint32_t>::max());
}

TEST_F(NexthopInfoTest, MoveConstructionPreservesAssociation) {
  auto routeInfo = createRouteInfo(
      kV4Prefix1,
      kPeerAddr2,
      kPeerAddr1,
      kLocalPref,
      {},
      kPeerAsn2,
      kPeerRouterId2);
  nexthopInfo_->linkRouteInfo(*routeInfo);

  NexthopInfo movedNexthopInfo(std::move(*nexthopInfo_));
  nexthopInfo_.reset();
  movedNexthopInfo.updateStatus(
      NexthopStatus({.ip = nexthopStatus_->getNexthop()}, true, 200));

  EXPECT_TRUE(routeInfo->isOnNextHopList());
  EXPECT_EQ(routeInfo->getIgpCostValue(), 200);
  EXPECT_EQ(movedNexthopInfo.getRouteInfoListSize(), 1);

  routeInfo.reset();
  EXPECT_TRUE(movedNexthopInfo.isRouteInfoListEmpty());
}

/*
 * Source reachability and best-path eligibility are separate. A reachable
 * nexthop without an IGP cost is eligible only when the platform allows
 * selection without a cost.
 */
TEST_F(NexthopInfoTest, IsResolvedForSelection) {
  folly::IPAddress nh("2620:0:1cff:dead:bef1:ffff:ffff:2");

  // Reachable with an IGP cost: resolved regardless of policy.
  const NexthopStatus reachableWithCost(
      {.ip = nh}, /*isReachable=*/true, /*igpCost=*/10);
  EXPECT_TRUE(reachableWithCost.isReachable());
  EXPECT_TRUE(reachableWithCost.isResolvedForSelection());

  // EBB reports the source reachability but requires a cost for selection.
  const NexthopStatus ebbNoCost(
      {.ip = nh},
      /*isReachable=*/true,
      /*igpCost=*/std::nullopt,
      /*isConnected=*/std::nullopt,
      /*excludeNexthopWithoutCost=*/true);
  EXPECT_TRUE(ebbNoCost.isReachable());
  EXPECT_FALSE(ebbNoCost.getIgpCost().has_value());
  EXPECT_FALSE(ebbNoCost.isResolvedForSelection());

  // FBOSS reports the same reachability and allows selection without a cost.
  const NexthopStatus fbossNoCost(
      {.ip = nh},
      /*isReachable=*/true,
      /*igpCost=*/std::nullopt,
      /*isConnected=*/std::nullopt,
      /*excludeNexthopWithoutCost=*/false);
  EXPECT_TRUE(fbossNoCost.isReachable());
  EXPECT_FALSE(fbossNoCost.getIgpCost().has_value());
  EXPECT_TRUE(fbossNoCost.isResolvedForSelection());

  // Unreachable: excluded under either policy.
  const NexthopStatus unreachable(
      {.ip = nh},
      /*isReachable=*/false,
      /*igpCost=*/std::nullopt,
      /*isConnected=*/std::nullopt,
      /*excludeNexthopWithoutCost=*/false);
  EXPECT_FALSE(unreachable.isReachable());
  EXPECT_FALSE(unreachable.isResolvedForSelection());
}

/*
 * RouteInfo uses the linked nexthop's platform policy when it checks whether
 * the route is eligible for best-path selection.
 */
TEST_F(NexthopInfoTest, RouteInfoReachabilityAndResolutionFromNexthop) {
  auto routeInfo = createRouteInfo(
      kV4Prefix1,
      kPeerAddr2,
      kPeerAddr1,
      kLocalPref,
      {},
      kPeerAsn2,
      kPeerRouterId2);
  folly::IPAddress nh("2620:0:1cff:dead:bef1:ffff:ffff:3");

  // Non-local route with no linked nexthop: not resolved.
  EXPECT_FALSE(routeInfo->getIsRouteLocal());
  EXPECT_FALSE(routeInfo->isNexthopReachable());
  EXPECT_FALSE(routeInfo->isResolvedForSelection());

  // A reachable FBOSS nexthop does not require a cost for selection.
  const NexthopStatus fbossNoCost(
      {.ip = nh},
      /*isReachable=*/true,
      /*igpCost=*/std::nullopt,
      /*isConnected=*/std::nullopt,
      /*excludeNexthopWithoutCost=*/false);
  NexthopInfo fbossInfo(fbossNoCost);
  fbossInfo.linkRouteInfo(*routeInfo);
  EXPECT_TRUE(routeInfo->isNexthopReachable());
  EXPECT_FALSE(routeInfo->hasIgpCost());
  EXPECT_TRUE(routeInfo->isResolvedForSelection());
  fbossInfo.unlinkRouteInfo(*routeInfo);

  // Reachable-no-cost EBB nexthop: excluded from selection.
  const NexthopStatus ebbNoCost(
      {.ip = nh},
      /*isReachable=*/true,
      /*igpCost=*/std::nullopt,
      /*isConnected=*/std::nullopt,
      /*excludeNexthopWithoutCost=*/true);
  NexthopInfo ebbInfo(ebbNoCost);
  ebbInfo.linkRouteInfo(*routeInfo);
  EXPECT_TRUE(routeInfo->isNexthopReachable());
  EXPECT_FALSE(routeInfo->hasIgpCost());
  EXPECT_FALSE(routeInfo->isResolvedForSelection());
  ebbInfo.unlinkRouteInfo(*routeInfo);

  const NexthopStatus unreachable(
      {.ip = nh}, /*isReachable=*/false, /*igpCost=*/std::nullopt);
  NexthopInfo unreachableInfo(unreachable);
  unreachableInfo.linkRouteInfo(*routeInfo);
  EXPECT_FALSE(routeInfo->isNexthopReachable());
  EXPECT_FALSE(routeInfo->hasIgpCost());
  EXPECT_FALSE(routeInfo->isResolvedForSelection());
  unreachableInfo.unlinkRouteInfo(*routeInfo);
}
