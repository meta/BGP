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

/*
 * E2E tests for CTE UCMP weights.
 *
 * The route attribute policy is built the way CTE builds its VIP statements:
 * the statement matches the VIP community, each DC's weight action matches
 * the DC's ASN in the AS path, and weights are divided by the number of
 * selected nexthops matching the action. DC1 (peer3) and DC2 (peer4) are
 * ADD-PATH peers, so one peer can announce several paths with distinct
 * nexthops.
 *
 * Mocked: FIB (TestFib), SessionManager (MockSessionManager)
 * Real: RIB (RibDC), PeerManagerBase, AdjRib, RibPolicy
 */

#include <gtest/gtest.h>

#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/rib_policy_types.h"
#include "neteng/fboss/bgp/cpp/rib/RibPolicy.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/E2ETestFixture.h"

namespace facebook::bgp {

namespace {

constexpr auto kVipCommunity = "65527:14666";
constexpr int32_t kVipCommAsn = 65527;
constexpr int32_t kVipCommValue = 14666;
// CTE multiplies each DC weight by 5040.
constexpr int32_t kWeightMultiplier = 5040;
const auto kVipPrefix = folly::IPAddress::createNetwork("10.0.0.0/8");
const auto kDc1Nexthop1 = folly::IPAddress("11.0.0.1");
const auto kDc1Nexthop2 = folly::IPAddress("11.0.0.3");
const auto kDc1Nexthop3 = folly::IPAddress("11.0.0.5");
const auto kDc2Nexthop = folly::IPAddress("11.0.0.2");

rib_policy::TRouteAttributePolicy
cteVipPolicy(int32_t dc1Weight, int32_t dc2Weight, int32_t commValue) {
  rib_policy::TRouteAttributeUcmpAction ucmpAction;
  for (const auto& [asPathRegex, weight] :
       {std::pair{"^65001", dc1Weight}, std::pair{"^65002", dc2Weight}}) {
    rib_policy::TBgpPathMatcher pathMatcher;
    pathMatcher.as_path_regex() = asPathRegex;
    rib_policy::TNextHopWeightAction weightAction;
    weightAction.path_matchers()->push_back(std::move(pathMatcher));
    weightAction.weight() = kWeightMultiplier * weight;
    ucmpAction.nexthop_weight_actions()->push_back(std::move(weightAction));
  }
  ucmpAction.apply_all_actions_or_fallback_to_ecmp() = false;
  ucmpAction.divide_weights_by_matching_path_count() = true;

  rib_policy::TBgpCommunityMatch commMatch;
  commMatch.community()->asn() = kVipCommAsn;
  commMatch.community()->value() = commValue;
  rib_policy::TRibRouteMatcher matcher;
  matcher.community_list().ensure().communities()->push_back(
      std::move(commMatch));

  rib_policy::TRouteAttributeStatement stmt;
  stmt.matcher() = std::move(matcher);
  stmt.actions()->set_ucmp_weights() = std::move(ucmpAction);

  rib_policy::TRouteAttributePolicy policy;
  policy.statements()->emplace("cte-vip-65527:14666", std::move(stmt));
  return policy;
}

} // namespace

class E2ECteUcmpTest : public E2ETestFixture {
 protected:
  void SetUp() override {
    addPeer(kDefaultPeerSpec3_AddPath);
    addPeer(kDefaultPeerSpec4_AddPath);
    addPeer(kDefaultPeerSpec5);
    createRib();
    createPeerManager(/*enableUpdateGroup=*/false);
  }

  void bringUpAllPeersWithEor() {
    for (const auto& peerAddr : {kPeerAddr3, kPeerAddr4, kPeerAddr5}) {
      bringUpPeer(peerAddr);
      BgpPeerId peerId{peerAddr, peerAddr.asV4().toLongHBO()};
      sendEoRToPeer(peerId);
      EXPECT_TRUE(waitForEoR(peerId));
    }
  }

  // Sets the policy and waits until the RIB has installed it.
  void setCtePolicy(
      int32_t dc1Weight,
      int32_t dc2Weight,
      int32_t commValue = kVipCommValue) {
    auto policy = cteVipPolicy(dc1Weight, dc2Weight, commValue);
    const auto expected = RouteAttributePolicy{policy}.toThrift();
    auto result = rib_->setRouteAttributePolicy(
        std::make_unique<rib_policy::TRouteAttributePolicy>(std::move(policy)));
    ASSERT_TRUE(*result.success());
    WITH_RETRIES_N_TIMED(100, std::chrono::milliseconds(20), {
      EXPECT_EVENTUALLY_TRUE(rib_->getRouteAttributePolicy() == expected);
    });
  }

  // Announces the VIP from DC1 (peer3, AS 65001) with the given ADD-PATH id.
  void announceVipFromDc1(
      const folly::IPAddress& nexthop,
      uint32_t addPathId,
      const std::string& asPath = "65001") {
    addRoute(
        "v4",
        "10.0.0.0",
        8,
        kPeerAddr3,
        nexthop.str(),
        asPath,
        kVipCommunity,
        addPathId);
  }

  // Announces the VIP from DC2 (peer4) with the given AS path.
  void announceVipFromDc2(const std::string& asPath = "65002") {
    addRoute(
        "v4",
        "10.0.0.0",
        8,
        kPeerAddr4,
        kDc2Nexthop.str(),
        asPath,
        kVipCommunity,
        /*addPathId=*/1);
  }

  void expectFibEventually(const WeightedNexthopMap& expected) {
    WITH_RETRIES_N_TIMED(100, std::chrono::milliseconds(50), {
      auto fibNexthops = getFibWeightedNexthops("10.0.0.0/8");
      ASSERT_EVENTUALLY_TRUE(fibNexthops != nullptr);
      EXPECT_EVENTUALLY_EQ(expected, *fibNexthops);
    });
  }
};

/*
 * Test: each DC's weight is divided among its selected nexthops, and the
 * route is advertised downstream.
 *
 * DC1 has two nexthops, so each gets half of DC1's weight. DC2's single
 * nexthop gets DC2's full weight. The bestpath is a DC1 path (lowest router
 * ID), advertised to peer5.
 */
TEST_F(E2ECteUcmpTest, WeightsDividedAmongEachDcsNexthops) {
  setCtePolicy(/*dc1Weight=*/1, /*dc2Weight=*/2);
  bringUpAllPeersWithEor();
  announceVipFromDc1(kDc1Nexthop1, /*addPathId=*/1);
  announceVipFromDc1(kDc1Nexthop2, /*addPathId=*/2);
  announceVipFromDc2();
  ASSERT_TRUE(waitForRouteInShadowRib(kVipPrefix));

  expectFibEventually(
      {{kDc1Nexthop1, kWeightMultiplier / 2},
       {kDc1Nexthop2, kWeightMultiplier / 2},
       {kDc2Nexthop, 2 * kWeightMultiplier}});
  EXPECT_TRUE(verifyRouteAdd(
      "v4", "10.0.0.0", 8, kPeerAddr5, "127.5.0.4", "4200000001 65001"));
}

/*
 * Test: a path that matches a weight action but is not selected does not
 * receive a weight and does not count toward the divisor.
 *
 * DC1's third path has a longer AS path, so path selection drops it. DC1's
 * weight is still split two ways, not three.
 */
TEST_F(E2ECteUcmpTest, NonSelectedPathDoesNotChangeDivisor) {
  setCtePolicy(/*dc1Weight=*/1, /*dc2Weight=*/2);
  bringUpAllPeersWithEor();
  announceVipFromDc1(kDc1Nexthop1, /*addPathId=*/1);
  announceVipFromDc1(kDc1Nexthop2, /*addPathId=*/2);
  announceVipFromDc1(kDc1Nexthop3, /*addPathId=*/3, /*asPath=*/"65001 65100");
  announceVipFromDc2();
  ASSERT_TRUE(waitForRouteInShadowRib(kVipPrefix));

  expectFibEventually(
      {{kDc1Nexthop1, kWeightMultiplier / 2},
       {kDc1Nexthop2, kWeightMultiplier / 2},
       {kDc2Nexthop, 2 * kWeightMultiplier}});
}

/*
 * Test: a selected nexthop that matches no weight action disables CTE
 * weights for the route, which falls back to ECMP.
 */
TEST_F(E2ECteUcmpTest, UnmatchedSelectedNexthopFallsBackToEcmp) {
  setCtePolicy(/*dc1Weight=*/1, /*dc2Weight=*/2);
  bringUpAllPeersWithEor();
  announceVipFromDc1(kDc1Nexthop1, /*addPathId=*/1);
  announceVipFromDc2(/*asPath=*/"65003");
  ASSERT_TRUE(waitForRouteInShadowRib(kVipPrefix));

  expectFibEventually({{kDc1Nexthop1, 0}, {kDc2Nexthop, 0}});
}

/*
 * Test: a route that stops matching the CTE statement loses its CTE weights.
 *
 * The policy update moves the statement to another community, so the VIP no
 * longer matches and falls back to ECMP.
 */
TEST_F(E2ECteUcmpTest, RouteThatStopsMatchingFallsBackToEcmp) {
  setCtePolicy(/*dc1Weight=*/1, /*dc2Weight=*/2);
  bringUpAllPeersWithEor();
  announceVipFromDc1(kDc1Nexthop1, /*addPathId=*/1);
  announceVipFromDc2();
  ASSERT_TRUE(waitForRouteInShadowRib(kVipPrefix));
  expectFibEventually(
      {{kDc1Nexthop1, kWeightMultiplier},
       {kDc2Nexthop, 2 * kWeightMultiplier}});

  setCtePolicy(/*dc1Weight=*/1, /*dc2Weight=*/2, /*commValue=*/14555);
  expectFibEventually({{kDc1Nexthop1, 0}, {kDc2Nexthop, 0}});
}

/*
 * Test: a DC with CTE weight 0 stays selected with the minimum weight of 1.
 */
TEST_F(E2ECteUcmpTest, ZeroWeightDcKeepsMinimumWeight) {
  setCtePolicy(/*dc1Weight=*/0, /*dc2Weight=*/2);
  bringUpAllPeersWithEor();
  announceVipFromDc1(kDc1Nexthop1, /*addPathId=*/1);
  announceVipFromDc2();
  ASSERT_TRUE(waitForRouteInShadowRib(kVipPrefix));

  expectFibEventually(
      {{kDc1Nexthop1, 1}, {kDc2Nexthop, 2 * kWeightMultiplier}});
}

/*
 * Test: FIB weights follow policy updates, path changes and policy removal.
 *
 * A new DC1 path re-divides DC1's weight, a policy update changes both DCs'
 * weights, and clearing the policy restores ECMP.
 */
TEST_F(E2ECteUcmpTest, WeightsFollowPolicyAndPathChanges) {
  setCtePolicy(/*dc1Weight=*/1, /*dc2Weight=*/2);
  bringUpAllPeersWithEor();
  announceVipFromDc1(kDc1Nexthop1, /*addPathId=*/1);
  announceVipFromDc2();
  ASSERT_TRUE(waitForRouteInShadowRib(kVipPrefix));
  expectFibEventually(
      {{kDc1Nexthop1, kWeightMultiplier},
       {kDc2Nexthop, 2 * kWeightMultiplier}});

  announceVipFromDc1(kDc1Nexthop2, /*addPathId=*/2);
  expectFibEventually(
      {{kDc1Nexthop1, kWeightMultiplier / 2},
       {kDc1Nexthop2, kWeightMultiplier / 2},
       {kDc2Nexthop, 2 * kWeightMultiplier}});

  setCtePolicy(/*dc1Weight=*/4, /*dc2Weight=*/1);
  expectFibEventually(
      {{kDc1Nexthop1, 2 * kWeightMultiplier},
       {kDc1Nexthop2, 2 * kWeightMultiplier},
       {kDc2Nexthop, kWeightMultiplier}});

  rib_->clearRouteAttributePolicy();
  expectFibEventually({{kDc1Nexthop1, 0}, {kDc1Nexthop2, 0}, {kDc2Nexthop, 0}});
}

} // namespace facebook::bgp
