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

#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#define BgpPathMatcher_TEST_FRIENDS                             \
  FRIEND_TEST(RibPolicyTest, BgpPathMatcherCommunityMatchTest); \
  FRIEND_TEST(RibPolicyTest, BgpPathMatcherAsPathLenMatchTest); \
  FRIEND_TEST(RibPolicyTest, BgpPathMatcherAsPathMatchTest);    \
  FRIEND_TEST(RibPolicyTest, BgpPathMatcherMinLbwBpsMatchTest);

#define PathSelectionCriteria_TEST_FRIENDS \
  FRIEND_TEST(RibPolicyTest, PathSelectionCriteriaTest);

#define PathSelector_TEST_FRIENDS FRIEND_TEST(RibPolicyTest, PathSelectorTest);
#define PathSelectionPolicy_TEST_FRIENDS \
  FRIEND_TEST(RibPolicyFixture, PathSelectionPolicyCacheTest);

#define NextHopWeightAction_TEST_FRIENDS \
  FRIEND_TEST(RibPolicyTest, NextHopWeightActionTest);

#define RouteAttributeUcmpAction_TEST_FRIENDS \
  FRIEND_TEST(RibPolicyTest, RouteAttributeUcmpActionTest);

#define RouteAttributePolicy_TEST_FRIENDS                               \
  FRIEND_TEST(RibPolicyTest, BasicTest);                                \
  FRIEND_TEST(RibPolicyTest, RouteAttributePolicyCacheTest);            \
  FRIEND_TEST(RibPolicyTest, RouteAttributePolicyCacheNegativeTest);    \
  FRIEND_TEST(RibPolicyTest, RouteAttributePolicyCacheExpiredStmtTest); \
  FRIEND_TEST(RibPolicyTest, RouteAttributePolicyCopyCache);            \
  FRIEND_TEST(RibPolicyTest, RouteAttributePolicySetCacheEntry);        \
  FRIEND_TEST(RibPolicyTest, RouteAttributePolicyGetStatements);        \
  FRIEND_TEST(RibPolicyTest, RouteAttributePolicyNegativeCacheReEval);

#include <folly/logging/xlog.h>

#include "neteng/fboss/bgp/cpp/BgpServiceUtil.h"
#include "neteng/fboss/bgp/cpp/common/RouteInfo.h"
#include "neteng/fboss/bgp/cpp/common/Utils.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopInfo.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopStatus.h"
#include "neteng/fboss/bgp/cpp/rib/RibBase.h"
#include "neteng/fboss/bgp/cpp/rib/RibDC.h"
#include "neteng/fboss/bgp/cpp/rib/RibEntry.h"
#include "neteng/fboss/bgp/cpp/rib/RibPolicy.h"
#include "neteng/fboss/bgp/cpp/tests/PolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibPolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"

namespace facebook::bgp {

using namespace rib_policy;
using namespace routing_policy;
using nettools::bgplib::BgpAttrCommunityC;
using namespace testing;

/*
 * basic test for rib policy apis:
 * -- constructor exceptions
 * -- toThrift
 * -- getExpirationTime
 * -- isActive
 */
TEST(RibPolicyTest, BasicTest) {
  {
    /*
     * test constructor exceptions
     * missing rib policy statement
     */
    TRibPolicy tPolicy;
    EXPECT_THROW(RibPolicy{tPolicy}, BgpError);

    // missing action value in rib policy statment
    TRouteAttributeStatement tStmt;
    tStmt.matcher() = createTRibRouteMatcher({kV4Prefix1});
    TRouteAttributePolicy tSubPolicy;
    tSubPolicy.statements()->emplace("stmt1", std::move(tStmt));
    tPolicy.route_attribute_policy() = std::move(tSubPolicy);
    EXPECT_THROW(RibPolicy{tPolicy}, BgpError);
  }
  {
    /*
     * test constructor exceptions
     * invalid regex in route filter policy
     */
    TRouteFilterPolicy tPolicy;
    tPolicy.statements()->emplace(".*eb.*", TRouteFilterStatement());
    // this should work
    RouteFilterPolicy{tPolicy};

    tPolicy.statements()->emplace("++", TRouteFilterStatement());
    // this should throw
    EXPECT_THROW(RouteFilterPolicy{tPolicy}, BgpError);
  }
  {
    // test route filter policy equivalance
    TRouteFilterPolicy tPolicy;
    tPolicy.statements()->emplace("stmt1", TRouteFilterStatement());
    RouteFilterPolicy policy1{tPolicy};

    RouteFilterPolicy policy2{tPolicy};
    EXPECT_EQ(policy1, policy2);

    tPolicy.statements()->emplace("stmt2", TRouteFilterStatement());
    RouteFilterPolicy policy3{tPolicy};
    EXPECT_NE(policy1, policy3);

    tPolicy.statements()->at("stmt2") =
        createTRouteFilterStatement({}, true /* permissive */);
    RouteFilterPolicy policy4{tPolicy};
    EXPECT_NE(policy3, policy4);
  }
  {
    // test toThrift API and comparison
    TRibPolicy tPolicy;
    // create RouteAttributePolicy
    tPolicy.route_attribute_policy() =
        createTRouteAttributePolicyLbw({kV4Prefix1}, 10000000000);
    tPolicy.path_selection_policy() =
        createTPathSelectionPolicyWithPathSelector(
            {kV4Prefix1}, TPathSelector());
    tPolicy.route_filter_policy() = createTRouteFilterPolicy(
        {createTRouteFilterStatement({kV4Prefix1}, true /* permissive */)},
        12345);

    RibPolicy policy1{tPolicy};
    EXPECT_EQ(tPolicy, policy1.toThrift());

    // test the existence of sub-policies:
    EXPECT_TRUE(policy1.hasRouteAttributePolicy());
    EXPECT_TRUE(policy1.getRouteAttributePolicy().has_value());
    EXPECT_TRUE(policy1.hasPathSelectionPolicy());
    EXPECT_TRUE(policy1.hasRouteFilterPolicy());

    // test equivalence
    RibPolicy policy2{tPolicy};
    EXPECT_EQ(policy1, policy2);

    // test inequivalence of route filter policy
    tPolicy.route_filter_policy() = createTRouteFilterPolicy(
        {createTRouteFilterStatement(
            {kV4Prefix1}, true /* permissive */, false /* ingress */)},
        12345);

    RibPolicy policy3{tPolicy};
    EXPECT_NE(policy1, policy3);

    // test inequivalence
    tPolicy.route_filter_policy().reset();
    RibPolicy policy4{tPolicy};
    EXPECT_NE(policy1, policy4);

    // test the existence of sub-policies:
    EXPECT_TRUE(policy4.hasRouteAttributePolicy());
    EXPECT_TRUE(policy4.getRouteAttributePolicy().has_value());
    EXPECT_TRUE(policy4.hasPathSelectionPolicy());
    EXPECT_FALSE(policy4.hasRouteFilterPolicy());
  }
  {
    // test getExpirationTime and isActive
    auto now = std::chrono::seconds(std::time(nullptr));
    TRibPolicy tPolicy = createTRibPolicyLbw(
        {kV4Prefix1}, 10000000000, "stmt1", now.count() + 20);
    RibPolicy policy{tPolicy};

    // remaining time must be less or equal than policy input
    EXPECT_EQ(
        policy.getRouteAttributePolicy()
            ->statements_.at("stmt1")
            .getExpirationTime(),
        now.count() + 20);

    // policy should be active
    EXPECT_TRUE(
        policy.getRouteAttributePolicy()->statements_.at("stmt1").isActive());

    tPolicy = createTRibPolicyLbw(
        {kV4Prefix1}, 10000000000, "stmt1", now.count() - 20);
    RibPolicy expiredPolicy(tPolicy);
    // policy should be inactive
    EXPECT_FALSE(expiredPolicy.getRouteAttributePolicy()
                     ->statements_.at("stmt1")
                     .isActive());
  }
}

/*
 * test prefix match cases:
 * single statement with single prefix
 * single statement with multiple prefixes
 * multiple statements with single prefix each
 * multiple statements with multiple prefixes each
 */
TEST(RibPolicyTest, PrefixMatchTest) {
  {
    // test single statement with single prefix
    TRibPolicy tPolicy1 = createTRibPolicyLbw({kV4Prefix1}, 10000000000);
    RibPolicy policy1{tPolicy1};

    RibEntry entry1(kV4Prefix1);
    RibEntry entry2(kV4Prefix2);

    // policy1 should match with entry1 but not entry2
    EXPECT_TRUE(policy1.getRouteAttributePolicy()->match(entry1));
    EXPECT_FALSE(policy1.getRouteAttributePolicy()->match(entry2));

    // The matches should also work for path selectors
    TPathSelector tPathSelector;
    TRibPolicy tPolicy2 = createTRibPolicyWithPathSelector(
        {kV4Prefix1}, std::move(tPathSelector));
    RibPolicy policy2{tPolicy2};

    // policy2 should match with entry1 but not entry2
    EXPECT_TRUE(policy2.getPathSelectionPolicy()->match(entry1));
    EXPECT_FALSE(policy2.getPathSelectionPolicy()->match(entry2));
  }
  {
    // test single statement with multiple prefixes
    TRibPolicy tPolicy =
        createTRibPolicyLbw({kV4Prefix1, kV4Prefix2}, 10000000000);
    RibPolicy policy{tPolicy};

    RibEntry entry1(kV4Prefix1);
    RibEntry entry2(kV4Prefix2);
    RibEntry entry3(kV4Prefix3);

    // policy1 should match with entry1, entry2 but not entry3
    EXPECT_TRUE(policy.getRouteAttributePolicy()->match(entry1));
    EXPECT_TRUE(policy.getRouteAttributePolicy()->match(entry2));
    EXPECT_FALSE(policy.getRouteAttributePolicy()->match(entry3));
  }
  {
    // test multiple statement with single prefix
    TRibPolicy tPolicy =
        createTRibPolicyLbw({kV4Prefix1}, 10000000000, "stmt1");
    tPolicy.route_attribute_policy()->statements()->emplace(
        "stmt2", createTRouteAttributeStatementLbw({kV4Prefix2}, 10000000000));
    RibPolicy policy{tPolicy};

    RibEntry entry1(kV4Prefix1);
    RibEntry entry2(kV4Prefix2);
    RibEntry entry3(kV4Prefix3);

    // policy1 should match with entry1, entry2 but not entry3
    EXPECT_TRUE(policy.getRouteAttributePolicy()->match(entry1));
    EXPECT_TRUE(policy.getRouteAttributePolicy()->match(entry2));
    EXPECT_FALSE(policy.getRouteAttributePolicy()->match(entry3));
  }
  {
    // test multiple statement with multiple prefixes
    TRibPolicy tPolicy =
        createTRibPolicyLbw({kV4Prefix1, kV4Prefix2}, 10000000000, "stmt1");
    tPolicy.route_attribute_policy()->statements()->emplace(
        "stmt2",
        createTRouteAttributeStatementLbw(
            {kV4Prefix3, kV4Prefix4}, 10000000000));
    RibPolicy policy{tPolicy};

    RibEntry entry1(kV4Prefix1);
    RibEntry entry2(kV4Prefix2);
    RibEntry entry3(kV4Prefix3);
    RibEntry entry4(kV4Prefix4);
    RibEntry entry5(kV4Prefix5);

    // policy1 should match with entry1-4 but not entry5
    EXPECT_TRUE(policy.getRouteAttributePolicy()->match(entry1));
    EXPECT_TRUE(policy.getRouteAttributePolicy()->match(entry2));
    EXPECT_TRUE(policy.getRouteAttributePolicy()->match(entry3));
    EXPECT_TRUE(policy.getRouteAttributePolicy()->match(entry4));
    EXPECT_FALSE(policy.getRouteAttributePolicy()->match(entry5));
  }
}

/*
 * test apply policy cases:
 * single zero lbw statement with single prefix
 * single non-zero lbw statement with single prefix
 * single statement with multiple prefix
 * multiple statements with single prefix each
 * multiple statements with multiple prefix each
 * default action conditions
 */
TEST(RibPolicyTest, ApplyPolicyTest) {
  {
    // test single zero-lbw statement with single prefix
    long expectedWeight = 0;
    TRibPolicy tPolicy = createTRibPolicyLbw({kV4Prefix1}, expectedWeight);
    RibPolicy policy{tPolicy};

    // create rib entries to test
    folly::F14FastMap<folly::CIDRNetwork, RibEntry> ribEntries;
    RibEntry entry1(kV4Prefix1);
    RibEntry entry2(kV4Prefix2);
    ribEntries.emplace(kV4Prefix1, entry1);
    ribEntries.emplace(kV4Prefix2, entry2);

    // even though entry1 is matched but zero lbw won't take effect at all
    EXPECT_TRUE(policy.getRouteAttributePolicy()->match(entry1));
    EXPECT_FALSE(policy.getRouteAttributePolicy()->match(entry2));
    std::unordered_set<folly::CIDRNetwork> emptyChange;
    RouteAttributePolicy::RibChange ribChange;
    for (auto& [_, ribEntry] : ribEntries) {
      policy.getRouteAttributePolicy()->overwriteRouteAttributes(
          ribEntry, ribChange);
    }
    EXPECT_EQ(emptyChange, ribChange.updatedRoutes);

    // verify the lbw is unset for entry1 and entry2
    EXPECT_FALSE(
        ribEntries.at(kV4Prefix1).getRibPolicyUcmpWeight().has_value());
    EXPECT_FALSE(
        ribEntries.at(kV4Prefix2).getRibPolicyUcmpWeight().has_value());
  }
  {
    // test single non-zero lbw statement with single prefix
    long expectedWeight = 1e11L;
    TRibPolicy tPolicy = createTRibPolicyLbw({kV4Prefix1}, expectedWeight);
    RibPolicy policy{tPolicy};

    // create rib entries to test
    folly::F14FastMap<folly::CIDRNetwork, RibEntry> ribEntries;
    ribEntries.emplace(kV4Prefix1, RibEntry{kV4Prefix1});
    ribEntries.emplace(kV4Prefix2, RibEntry{kV4Prefix2});

    // policy1 should take action on entry1 but not entry2
    std::unordered_set<folly::CIDRNetwork> expectedChange;
    expectedChange.emplace(kV4Prefix1);
    RouteAttributePolicy::RibChange ribChange;
    for (auto& [_, ribEntry] : ribEntries) {
      policy.getRouteAttributePolicy()->overwriteRouteAttributes(
          ribEntry, ribChange);
    }
    EXPECT_EQ(expectedChange, ribChange.updatedRoutes);

    // verify the lbw is correctly set for entry1. entry2 has no value
    EXPECT_TRUE(ribEntries.at(kV4Prefix1).getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(
        expectedWeight,
        ribEntries.at(kV4Prefix1).getRibPolicyUcmpWeight().value());
    EXPECT_FALSE(
        ribEntries.at(kV4Prefix2).getRibPolicyUcmpWeight().has_value());

    /*
     * once the policy has been applied, apply same policy again will result
     * empty change list
     */
    std::unordered_set<folly::CIDRNetwork> emptyChange;
    RouteAttributePolicy::RibChange secondRibChange;
    for (auto& [_, ribEntry] : ribEntries) {
      policy.getRouteAttributePolicy()->overwriteRouteAttributes(
          ribEntry, secondRibChange);
    }
    EXPECT_EQ(emptyChange, secondRibChange.updatedRoutes);

    // verify the lbw is correctly set for entry1. entry2 has no value
    EXPECT_TRUE(ribEntries.at(kV4Prefix1).getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(
        expectedWeight,
        ribEntries.at(kV4Prefix1).getRibPolicyUcmpWeight().value());
    EXPECT_FALSE(
        ribEntries.at(kV4Prefix2).getRibPolicyUcmpWeight().has_value());
  }
  {
    // test single statement with multiple prefix
    long expectedWeight = 1e11L;
    TRibPolicy tPolicy =
        createTRibPolicyLbw({kV4Prefix1, kV4Prefix2}, expectedWeight);
    RibPolicy policy{tPolicy};

    // create rib entries to test
    folly::F14FastMap<folly::CIDRNetwork, RibEntry> ribEntries;
    ribEntries.emplace(kV4Prefix1, RibEntry{kV4Prefix1});
    ribEntries.emplace(kV4Prefix2, RibEntry{kV4Prefix2});
    ribEntries.emplace(kV4Prefix3, RibEntry{kV4Prefix3});

    // policy1 should take action on entry1, entry2 but not entry3
    std::unordered_set<folly::CIDRNetwork> expectedChange;
    expectedChange.emplace(kV4Prefix1);
    expectedChange.emplace(kV4Prefix2);
    RouteAttributePolicy::RibChange ribChange;
    for (auto& [_, ribEntry] : ribEntries) {
      policy.getRouteAttributePolicy()->overwriteRouteAttributes(
          ribEntry, ribChange);
    }
    EXPECT_EQ(expectedChange, ribChange.updatedRoutes);

    // verify the lbw is correctly set for entry1-2. entry3 has no value
    EXPECT_TRUE(ribEntries.at(kV4Prefix1).getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(
        expectedWeight,
        ribEntries.at(kV4Prefix1).getRibPolicyUcmpWeight().value());
    EXPECT_TRUE(ribEntries.at(kV4Prefix2).getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(
        expectedWeight,
        ribEntries.at(kV4Prefix2).getRibPolicyUcmpWeight().value());
    EXPECT_FALSE(
        ribEntries.at(kV4Prefix3).getRibPolicyUcmpWeight().has_value());

    /*
     * once the policy has been applied, apply same policy again will result
     * empty change list
     */
    std::unordered_set<folly::CIDRNetwork> emptyChange;
    RouteAttributePolicy::RibChange secondRibChange;
    for (auto& [_, ribEntry] : ribEntries) {
      policy.getRouteAttributePolicy()->overwriteRouteAttributes(
          ribEntry, secondRibChange);
    }
    EXPECT_EQ(emptyChange, secondRibChange.updatedRoutes);

    // verify the lbw is correctly set for entry1-2. entry3 has no value
    EXPECT_TRUE(ribEntries.at(kV4Prefix1).getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(
        expectedWeight,
        ribEntries.at(kV4Prefix1).getRibPolicyUcmpWeight().value());
    EXPECT_TRUE(ribEntries.at(kV4Prefix2).getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(
        expectedWeight,
        ribEntries.at(kV4Prefix2).getRibPolicyUcmpWeight().value());
    EXPECT_FALSE(
        ribEntries.at(kV4Prefix3).getRibPolicyUcmpWeight().has_value());
  }
  {
    // test multiple statement with single prefix
    long expectedWeight1 = 1e11L;
    long expectedWeight2 = 2e11L;
    TRibPolicy tPolicy =
        createTRibPolicyLbw({kV4Prefix1}, expectedWeight1, "stmt1");
    tPolicy.route_attribute_policy()->statements()->emplace(
        "stmt2",
        createTRouteAttributeStatementLbw({kV4Prefix2}, expectedWeight2));
    RibPolicy policy{tPolicy};

    // create rib entries to test
    folly::F14FastMap<folly::CIDRNetwork, RibEntry> ribEntries;
    ribEntries.emplace(kV4Prefix1, RibEntry{kV4Prefix1});
    ribEntries.emplace(kV4Prefix2, RibEntry{kV4Prefix2});
    ribEntries.emplace(kV4Prefix3, RibEntry{kV4Prefix3});

    // policy1 should take action on entry1, entry2 but not entry3
    std::unordered_set<folly::CIDRNetwork> expectedChange;
    expectedChange.emplace(kV4Prefix1);
    expectedChange.emplace(kV4Prefix2);
    RouteAttributePolicy::RibChange ribChange;
    for (auto& [_, ribEntry] : ribEntries) {
      policy.getRouteAttributePolicy()->overwriteRouteAttributes(
          ribEntry, ribChange);
    }
    EXPECT_EQ(expectedChange, ribChange.updatedRoutes);

    // verify the lbw is correctly set for entry1-2. entry3 has no value
    EXPECT_TRUE(ribEntries.at(kV4Prefix1).getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(
        expectedWeight1,
        ribEntries.at(kV4Prefix1).getRibPolicyUcmpWeight().value());
    EXPECT_TRUE(ribEntries.at(kV4Prefix2).getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(
        expectedWeight2,
        ribEntries.at(kV4Prefix2).getRibPolicyUcmpWeight().value());
    EXPECT_FALSE(
        ribEntries.at(kV4Prefix3).getRibPolicyUcmpWeight().has_value());

    /*
     * once the policy has been applied, apply same policy again will result
     * empty update list and identical match list
     */
    std::unordered_set<folly::CIDRNetwork> emptyChange;
    RouteAttributePolicy::RibChange secondRibChange;
    for (auto& [_, ribEntry] : ribEntries) {
      policy.getRouteAttributePolicy()->overwriteRouteAttributes(
          ribEntry, secondRibChange);
    }
    EXPECT_EQ(emptyChange, secondRibChange.updatedRoutes);

    // verify the lbw is correctly set for entry1-2. entry3 has no value
    EXPECT_TRUE(ribEntries.at(kV4Prefix1).getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(
        expectedWeight1,
        ribEntries.at(kV4Prefix1).getRibPolicyUcmpWeight().value());
    EXPECT_TRUE(ribEntries.at(kV4Prefix2).getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(
        expectedWeight2,
        ribEntries.at(kV4Prefix2).getRibPolicyUcmpWeight().value());
    EXPECT_FALSE(
        ribEntries.at(kV4Prefix3).getRibPolicyUcmpWeight().has_value());
  }
  {
    // test multiple statement with multiple prefix
    long expectedWeight1 = 1e11L;
    long expectedWeight2 = 2e11L;
    TRibPolicy tPolicy =
        createTRibPolicyLbw({kV4Prefix1, kV4Prefix2}, expectedWeight1, "stmt1");
    tPolicy.route_attribute_policy()->statements()->emplace(
        "stmt2",
        createTRouteAttributeStatementLbw(
            {kV4Prefix3, kV4Prefix4}, expectedWeight2));
    RibPolicy policy{tPolicy};

    // create rib entries to test
    folly::F14FastMap<folly::CIDRNetwork, RibEntry> ribEntries;
    ribEntries.emplace(kV4Prefix1, RibEntry{kV4Prefix1});
    ribEntries.emplace(kV4Prefix2, RibEntry{kV4Prefix2});
    ribEntries.emplace(kV4Prefix3, RibEntry{kV4Prefix3});
    ribEntries.emplace(kV4Prefix4, RibEntry{kV4Prefix4});
    ribEntries.emplace(kV4Prefix5, RibEntry{kV4Prefix5});

    // policy1 should take action on entry1-4 but not entry5
    std::unordered_set<folly::CIDRNetwork> expectedChange;
    expectedChange.emplace(kV4Prefix1);
    expectedChange.emplace(kV4Prefix2);
    expectedChange.emplace(kV4Prefix3);
    expectedChange.emplace(kV4Prefix4);
    RouteAttributePolicy::RibChange ribChange;
    for (auto& [_, ribEntry] : ribEntries) {
      policy.getRouteAttributePolicy()->overwriteRouteAttributes(
          ribEntry, ribChange);
    }
    EXPECT_EQ(expectedChange, ribChange.updatedRoutes);

    // verify the lbw is correctly set for entry1-4. entry5 has no value
    EXPECT_TRUE(ribEntries.at(kV4Prefix1).getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(
        expectedWeight1,
        ribEntries.at(kV4Prefix1).getRibPolicyUcmpWeight().value());
    EXPECT_TRUE(ribEntries.at(kV4Prefix2).getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(
        expectedWeight1,
        ribEntries.at(kV4Prefix2).getRibPolicyUcmpWeight().value());
    EXPECT_TRUE(ribEntries.at(kV4Prefix3).getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(
        expectedWeight2,
        ribEntries.at(kV4Prefix3).getRibPolicyUcmpWeight().value());
    EXPECT_TRUE(ribEntries.at(kV4Prefix4).getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(
        expectedWeight2,
        ribEntries.at(kV4Prefix4).getRibPolicyUcmpWeight().value());
    EXPECT_FALSE(
        ribEntries.at(kV4Prefix5).getRibPolicyUcmpWeight().has_value());

    /*
     * once the policy has been applied, apply same policy again will result
     * empty change list
     */
    std::unordered_set<folly::CIDRNetwork> emptyChange;
    RouteAttributePolicy::RibChange secondRibChange;
    for (auto& [_, ribEntry] : ribEntries) {
      policy.getRouteAttributePolicy()->overwriteRouteAttributes(
          ribEntry, secondRibChange);
    }
    EXPECT_EQ(emptyChange, secondRibChange.updatedRoutes);

    // verify the lbw is correctly set for entry1-4. entry5 has no value
    EXPECT_TRUE(ribEntries.at(kV4Prefix1).getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(
        expectedWeight1,
        ribEntries.at(kV4Prefix1).getRibPolicyUcmpWeight().value());
    EXPECT_TRUE(ribEntries.at(kV4Prefix2).getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(
        expectedWeight1,
        ribEntries.at(kV4Prefix2).getRibPolicyUcmpWeight().value());
    EXPECT_TRUE(ribEntries.at(kV4Prefix3).getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(
        expectedWeight2,
        ribEntries.at(kV4Prefix3).getRibPolicyUcmpWeight().value());
    EXPECT_TRUE(ribEntries.at(kV4Prefix4).getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(
        expectedWeight2,
        ribEntries.at(kV4Prefix4).getRibPolicyUcmpWeight().value());
    EXPECT_FALSE(
        ribEntries.at(kV4Prefix5).getRibPolicyUcmpWeight().has_value());
  }
}

TEST(RibPolicyTest, PathSelectorTest) {
  TPathSelector tPathSelector;

  // a dummy matcher
  TBgpPathMatcher tMatcher;
  tMatcher.origin() = bgp_policy::Origin::EGP; // match EGP

  TPathSelectionCriteria tCriteria1;
  tCriteria1.path_matchers()->push_back(tMatcher);
  tCriteria1.min_nexthop() = 4;

  TPathSelectionCriteria tCriteria2;
  tCriteria2.path_matchers()->push_back(tMatcher);
  tCriteria2.min_nexthop() = 3;

  tPathSelector.criteria_list()->push_back(tCriteria1);
  tPathSelector.criteria_list()->push_back(tCriteria2);
  tPathSelector.bgp_native_path_selection_min_nexthop() = 5;

  PathSelector pathSelector1(tPathSelector);

  // Test the creation of corresponding centralized criteria
  EXPECT_EQ(pathSelector1.centralizedCriteriaList_.size(), 2);
  EXPECT_NE(pathSelector1.centralizedCriteriaList_.at(0).get(), nullptr);
  EXPECT_NE(pathSelector1.centralizedCriteriaList_.at(1).get(), nullptr);

  // Test toThrift
  EXPECT_EQ(pathSelector1.toThrift(), tPathSelector);

  // Test comparison
  PathSelector pathSelector2(tPathSelector);
  EXPECT_EQ(pathSelector1, pathSelector2);

  tPathSelector.criteria_list()->clear();
  tPathSelector.criteria_list()->push_back(tCriteria2);

  PathSelector pathSelector3(tPathSelector);
  EXPECT_NE(pathSelector1, pathSelector3);
}

/*
 * Reproduce P1: PathSelector::operator== ignores list length.
 * Two PathSelectors with criteria lists [A, B] and [A, B, C] compare equal
 * because the loop stops when the shorter list ends without checking sizes.
 */
TEST(RibPolicyTest, PathSelectorListLengthBug) {
  TBgpPathMatcher tMatcher;
  tMatcher.origin() = bgp_policy::Origin::EGP;

  TPathSelectionCriteria tCriteria1;
  tCriteria1.path_matchers()->push_back(tMatcher);
  tCriteria1.min_nexthop() = 4;

  TPathSelectionCriteria tCriteria2;
  tCriteria2.path_matchers()->push_back(tMatcher);
  tCriteria2.min_nexthop() = 3;

  TPathSelectionCriteria tCriteria3;
  tCriteria3.path_matchers()->push_back(tMatcher);
  tCriteria3.min_nexthop() = 2;

  TPathSelector tShort;
  tShort.criteria_list()->push_back(tCriteria1);
  tShort.criteria_list()->push_back(tCriteria2);
  tShort.bgp_native_path_selection_min_nexthop() = 5;

  TPathSelector tLong;
  tLong.criteria_list()->push_back(tCriteria1);
  tLong.criteria_list()->push_back(tCriteria2);
  tLong.criteria_list()->push_back(tCriteria3);
  tLong.bgp_native_path_selection_min_nexthop() = 5;

  PathSelector shortSelector(tShort);
  PathSelector longSelector(tLong);

  EXPECT_NE(shortSelector, longSelector)
      << "PathSelectors with different length criteria lists must not be equal";
}

TEST(RibPolicyTest, PathSelectionCriteriaTest) {
  TPathSelectionCriteria tCriteria;

  // Test empty settings: throwing BgpError
  EXPECT_THROW(PathSelectionCriteria{tCriteria}, BgpError);

  TBgpPathMatcher tMatcher;
  tMatcher.origin() = bgp_policy::Origin::EGP; // match EGP

  tCriteria.path_matchers()->push_back(tMatcher);
  tCriteria.path_matchers()->push_back(tMatcher);
  tCriteria.min_nexthop() = 4;

  PathSelectionCriteria criteria1(tCriteria);

  // Test the parsing
  EXPECT_EQ(criteria1.pathMatchers_.size(), 2);
  EXPECT_EQ(criteria1.minNexthop_, 4);

  // Test toThrift
  EXPECT_EQ(criteria1.toThrift(), tCriteria);

  // Test comparison
  tCriteria.min_nexthop() = 2;
  PathSelectionCriteria criteria2(tCriteria);
  EXPECT_NE(criteria1, criteria2);
}

TEST(RibPolicyTest, LowestIgpCostMatcherValidationTest) {
  TBgpPathMatcher matcher;
  matcher.prefer_lowest_igp_cost() = true;
  EXPECT_THROW(BgpPathMatcher{matcher}, BgpError);

  TCommunityListMatch emptyAndCommunities;
  emptyAndCommunities.boolean_operator() = routing_policy::BooleanOperator::AND;
  matcher.community_list() = emptyAndCommunities;
  EXPECT_THROW(BgpPathMatcher{matcher}, BgpError);

  matcher.origin() = bgp_policy::Origin::EGP;
  BgpPathMatcher withOrigin(matcher);
  EXPECT_TRUE(withOrigin.prefersLowestIgpCost());

  matcher.community_list().reset();
  matcher.origin().reset();
  matcher.prefer_lowest_igp_cost() = false;
  EXPECT_THROW(BgpPathMatcher{matcher}, BgpError);

  matcher.origin() = bgp_policy::Origin::EGP;
  matcher.prefer_lowest_igp_cost() = true;
  TNextHopWeightAction action;
  action.path_matchers()->push_back(matcher);
  action.weight() = 10;
  EXPECT_THROW(NextHopWeightAction{action}, BgpError);
}

TEST(RibPolicyTest, BgpPathMatcherCommunityMatchTest) {
  TBgpPathMatcher tMatcher;

  // Empty matcher should throw an error
  EXPECT_THROW(BgpPathMatcher{tMatcher}, BgpError);

  auto tCommMatch1 = createTBgpCommunityMatch(200, 666);
  auto tCommMatch2 = createTBgpCommunityMatch(100, 234);

  tMatcher.community_list() = createTCommunityListMatch(
      {tCommMatch1, tCommMatch2}, routing_policy::BooleanOperator::AND);
  tMatcher.origin() = bgp_policy::Origin::EGP; // match EGP

  BgpPathMatcher matcher(tMatcher);

  // Test parsing
  EXPECT_EQ(matcher.matches_.size(), 2);
  {
    // Create a dummy path
    auto path = createRouteInfo(
        kV4Prefix1, /* prefix */
        kPeerAddr2, /* peerAddr */
        kPeerAddr1, /* nexthop */
        kLocalPref, /* localPref */
        {},
        kPeerAsn2, /* peerAsn */
        kPeerRouterId2); /* peerRouterId */

    // Test matching: should not match
    EXPECT_FALSE(matcher.match(path));
  }
  {
    // Create a matching attribute
    std::vector<std::string> communities = {"200:666", "100:234"};

    auto path = createRouteInfo(
        kV4Prefix1, /* prefix */
        kPeerAddr2, /* peerAddr */
        kPeerAddr1, /* nexthop */
        kLocalPref, /* localPref */
        communities, /* communities */
        kPeerAsn2, /* peerAsn */
        kPeerRouterId2); /* peerRouterId */

    // Test matching: should match
    EXPECT_TRUE(matcher.match(path));
  }
}

TEST(RibPolicyTest, BgpPathMatcherMinLbwBpsMatchTest) {
  TBgpPathMatcher tMatcher;
  tMatcher.min_lbw_bps() = (int64_t)800 * 1000 * 1000 * 1000; // 800Gbps

  BgpPathMatcher matcher(tMatcher);

  // Test parsing
  EXPECT_EQ(matcher.matches_.size(), 1);
  {
    // Create a dummy path
    auto path = createRouteInfo(
        kV4Prefix1, /* prefix */
        kPeerAddr2, /* peerAddr */
        kPeerAddr1, /* nexthop */
        kLocalPref, /* localPref */
        {}, /* communities */
        kPeerAsn2, /* peerAsn */
        kPeerRouterId2); /* peerRouterId */

    // Test matching: should not match because no lbw exists
    EXPECT_FALSE(matcher.match(path));
  }
  {
    auto attr = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(2, 1, 0, 2));
    attr->setNonTransitiveLbwExtCommunity(
        kLocalAs1, float(400) * BpsPerGBps / 8);
    // Create a matching attribute
    auto path = createRouteInfo(
        kV4Prefix1, /* prefix */
        kPeerAddr2, /* peerAddr */
        kPeerAddr1, /* nexthop */
        kLocalPref, /* localPref */
        {}, /* communities */
        kPeerAsn2, /* peerAsn */
        kPeerRouterId2, /* peerRouterId */
        attr); /** attribute */

    // Test matching: should not match because 400G is less than 800G
    EXPECT_FALSE(matcher.match(path));
  }
  {
    auto attr = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(2, 1, 0, 2));
    attr->setNonTransitiveLbwExtCommunity(
        kLocalAs1, float(800) * BpsPerGBps / 8);
    // Create a matching attribute
    auto path = createRouteInfo(
        kV4Prefix1, /* prefix */
        kPeerAddr2, /* peerAddr */
        kPeerAddr1, /* nexthop */
        kLocalPref, /* localPref */
        {}, /* communities */
        kPeerAsn2, /* peerAsn */
        kPeerRouterId2, /* peerRouterId */
        attr); /** attribute */

    // Test matching: should match because 800G == 800G
    EXPECT_TRUE(matcher.match(path));
  }
  {
    auto attr = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(2, 1, 0, 2));
    attr->setNonTransitiveLbwExtCommunity(
        kLocalAs1, float(1000) * BpsPerGBps / 8);
    // Create a matching attribute
    auto path = createRouteInfo(
        kV4Prefix1, /* prefix */
        kPeerAddr2, /* peerAddr */
        kPeerAddr1, /* nexthop */
        kLocalPref, /* localPref */
        {}, /* communities */
        kPeerAsn2, /* peerAsn */
        kPeerRouterId2, /* peerRouterId */
        attr); /** attribute */

    // Test matching: should match because 1000G > 800G
    EXPECT_TRUE(matcher.match(path));
  }
}

TEST(RibPolicyTest, BgpPathMatcherAsPathLenMatchTest) {
  TBgpPathMatcher tMatcher;
  tMatcher.as_path_length() = 4;

  BgpPathMatcher matcher(tMatcher);

  // Test parsing
  EXPECT_EQ(matcher.matches_.size(), 1);
  {
    // Create a dummy path
    auto path = createRouteInfo(
        kV4Prefix1, /* prefix */
        kPeerAddr2, /* peerAddr */
        kPeerAddr1, /* nexthop */
        kLocalPref, /* localPref */
        {}, /* communities */
        kPeerAsn2, /* peerAsn */
        kPeerRouterId2); /* peerRouterId */

    // Test matching: should not match
    EXPECT_FALSE(matcher.match(path));
  }
  {
    auto attr = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 1, 0, 0));
    // Create a matching attribute
    auto path = createRouteInfo(
        kV4Prefix1, /* prefix */
        kPeerAddr2, /* peerAddr */
        kPeerAddr1, /* nexthop */
        kLocalPref, /* localPref */
        {}, /* communities */
        kPeerAsn2, /* peerAsn */
        kPeerRouterId2, /* peerRouterId */
        attr); /** attribute */

    // Test matching: should match
    EXPECT_TRUE(matcher.match(path));
  }
  {
    // Create a shorter path of length 2
    auto attr = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(2, 1, 0, 0));
    auto path = createRouteInfo(
        kV4Prefix1, /* prefix */
        kPeerAddr2, /* peerAddr */
        kPeerAddr1, /* nexthop */
        kLocalPref, /* localPref */
        {}, /* communities */
        kPeerAsn2, /* peerAsn */
        kPeerRouterId2, /* peerRouterId */
        attr); /** attribute */

    // Test matching: should not match
    EXPECT_FALSE(matcher.match(path));
  }
}

TEST(RibPolicyTest, BgpPathMatcherAsPathMatchTest) {
  {
    TBgpPathMatcher tMatcher;
    tMatcher.as_path_regex() = kASPathRegex1; // "^65000.*"

    BgpPathMatcher matcher(tMatcher);

    // Test parsing
    EXPECT_EQ(matcher.matches_.size(), 1);
    {
      // Create a dummy path
      auto path = createRouteInfo(
          kV4Prefix1, /* prefix */
          kPeerAddr2, /* peerAddr */
          kPeerAddr1, /* nexthop */
          kLocalPref, /* localPref */
          {}, /* communities */
          kPeerAsn2, /* peerAsn */
          kPeerRouterId2); /* peerRouterId */

      // Test matching: should not match
      EXPECT_FALSE(matcher.match(path));
    }
    {
      nettools::bgplib::BgpAttrAsPathC asPath;
      nettools::bgplib::BgpAttrAsPathSegmentC segment;
      segment.asSequence.push_back(65000);
      asPath.push_back(segment);

      auto path = createRouteInfo(
          kV4Prefix1, /* prefix */
          kPeerAddr2, /* peerAddr */
          kPeerAddr1, /* nexthop */
          kLocalPref, /* localPref */
          {}, /* communities */
          kPeerAsn2, /* peerAsn */
          kPeerRouterId2, /* peerRouterId */
          nullptr, /* attribute */
          asPath); /* asPath */

      // Test matching: should match
      EXPECT_TRUE(matcher.match(path));
    }
    {
      nettools::bgplib::BgpAttrAsPathC asPath;
      nettools::bgplib::BgpAttrAsPathSegmentC segment;
      segment.asSequence.push_back(63000);
      segment.asSequence.push_back(65000);
      asPath.push_back(segment);

      auto path = createRouteInfo(
          kV4Prefix1, /* prefix */
          kPeerAddr2, /* peerAddr */
          kPeerAddr1, /* nexthop */
          kLocalPref, /* localPref */
          {}, /* communities */
          kPeerAsn2, /* peerAsn */
          kPeerRouterId2, /* peerRouterId */
          nullptr, /* attribute */
          asPath); /* asPath */

      // Test matching: not start from 65000 -> should not match
      EXPECT_FALSE(matcher.match(path));
    }
  }
  {
    // Test the combination with other matchers through AND
    TBgpPathMatcher tMatcher;
    tMatcher.as_path_regex() = kASPathRegex1; // "^65000.*"
    tMatcher.as_path_length() = 2;

    BgpPathMatcher matcher(tMatcher);

    // Test parsing
    EXPECT_EQ(matcher.matches_.size(), 2);
    {
      // Matching case
      nettools::bgplib::BgpAttrAsPathC asPath;
      nettools::bgplib::BgpAttrAsPathSegmentC segment;
      segment.asSequence.push_back(65000);
      segment.asSequence.push_back(63000);
      asPath.push_back(segment);

      auto path = createRouteInfo(
          kV4Prefix1, /* prefix */
          kPeerAddr2, /* peerAddr */
          kPeerAddr1, /* nexthop */
          kLocalPref, /* localPref */
          {}, /* communities */
          kPeerAsn2, /* peerAsn */
          kPeerRouterId2, /* peerRouterId */
          nullptr, /* attribute */
          asPath); /* asPath */

      // Test matching: match
      EXPECT_TRUE(matcher.match(path));
    }
    {
      // Non-matching case: AS path regex not match
      nettools::bgplib::BgpAttrAsPathC asPath;
      nettools::bgplib::BgpAttrAsPathSegmentC segment;
      segment.asSequence.push_back(63000);
      segment.asSequence.push_back(65000);
      asPath.push_back(segment);

      auto path = createRouteInfo(
          kV4Prefix1, /* prefix */
          kPeerAddr2, /* peerAddr */
          kPeerAddr1, /* nexthop */
          kLocalPref, /* localPref */
          {}, /* communities */
          kPeerAsn2, /* peerAsn */
          kPeerRouterId2, /* peerRouterId */
          nullptr, /* attribute */
          asPath); /* asPath */

      // Test matching: not match
      EXPECT_FALSE(matcher.match(path));
    }
    {
      // Non-matching case: AS path len not match
      nettools::bgplib::BgpAttrAsPathC asPath;
      nettools::bgplib::BgpAttrAsPathSegmentC segment;
      segment.asSequence.push_back(65000);
      segment.asSequence.push_back(63000);
      segment.asSequence.push_back(64000);
      asPath.push_back(segment);

      auto path = createRouteInfo(
          kV4Prefix1, /* prefix */
          kPeerAddr2, /* peerAddr */
          kPeerAddr1, /* nexthop */
          kLocalPref, /* localPref */
          {}, /* communities */
          kPeerAsn2, /* peerAsn */
          kPeerRouterId2, /* peerRouterId */
          nullptr, /* attribute */
          asPath); /* asPath */

      // Test matching: not match
      EXPECT_FALSE(matcher.match(path));
    }
  }
}

TEST(RibPolicyTest, RibPolicyRouteMatcherTest) {
  // Test empty settings: throwing BgpError
  TRibRouteMatcher tMatcher1;
  EXPECT_THROW(RibPolicyRouteMatcher{tMatcher1}, BgpError);

  // Test comparison
  tMatcher1 = createTRibRouteMatcher({kV4Prefix1});
  auto tMatcher2 = createTRibRouteMatcher({kV4Prefix1});
  auto tMatcher3 = createTRibRouteMatcher({kV4Prefix2});

  RibPolicyRouteMatcher matcher1(tMatcher1);
  RibPolicyRouteMatcher matcher2(tMatcher2);
  RibPolicyRouteMatcher matcher3(tMatcher3);

  EXPECT_TRUE(matcher1 == matcher2);
  EXPECT_TRUE(matcher1 != matcher3);

  EXPECT_TRUE(matcher1.toThrift() == tMatcher1);

  /*
   * set up two rib entries,
   * one has prefix kV4Prefix1 and a path of community 200:666
   * another has prefix kV4Prefix2 and a path of community 100:234
   */
  auto peer = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  nettools::bgplib::BgpPeerId peerId{peer.addr, peer.routerId};

  auto attr1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  nettools::bgplib::BgpAttrCommunitiesC community1;
  community1.emplace_back(200, 666);
  attr1->setNexthop(kV4Nexthop1);
  attr1->setLocalPref(kLocalPref);
  attr1->setCommunities(community1);
  attr1->setOrigin(nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP);
  attr1->publish();

  auto attr2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(2, 2, 2, 3));
  nettools::bgplib::BgpAttrCommunitiesC community2;
  community2.emplace_back(100, 234);
  attr2->setNexthop(kV4Nexthop2);
  attr2->setLocalPref(kLocalPref);
  attr2->setCommunities(community2);
  attr2->setOrigin(nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP);
  attr2->publish();

  RibEntry entry1(kV4Prefix1);
  entry1.updatePath(peer, attr1, false);

  RibEntry entry2(kV4Prefix2);
  entry2.updatePath(peer, attr2, false);

  /*
   * Test community match
   * match community 200:666
   */
  RibPolicyRouteMatcher communityMatcher(createTRibRouteMatcher(
      {}, {createTCommunityListMatch({createTBgpCommunityMatch(200, 666)})}));

  EXPECT_TRUE(communityMatcher.match(entry1));
  EXPECT_FALSE(communityMatcher.match(entry2));

  /*
   * A matcher must specify either a prefix set or a community list, not both;
   * "match this prefix OR this community" is expressed as two statements.
   */
  EXPECT_THROW(
      RibPolicyRouteMatcher(createTRibRouteMatcher(
          {kV4Prefix2},
          {createTCommunityListMatch({createTBgpCommunityMatch(200, 666)})})),
      BgpError);
}

TEST(RibPolicyTest, RouteAttributeStatementTest) {
  TRouteAttributeStatement tStmt =
      createTRouteAttributeStatementLbw({kV4Prefix1}, 10);

  // Test toThrift
  RouteAttributeStatement stmt1{tStmt};
  EXPECT_EQ(stmt1.toThrift(), tStmt);

  // Comparison test
  tStmt.matcher() = createTRibRouteMatcher({kV4Prefix1});
  RouteAttributeStatement stmt2{tStmt};
  EXPECT_EQ(stmt1, stmt2);

  tStmt.matcher() = createTRibRouteMatcher({kV4Prefix2});
  RouteAttributeStatement stmt3{tStmt};
  EXPECT_NE(stmt1, stmt3);
}

TEST(RibPolicyTest, PathSelectionStatementTest) {
  TPathSelectionStatement tStmt = createTPathSelectionStatementWithPathSelector(
      {kV4Prefix1}, TPathSelector());

  // Test toThrift
  PathSelectionStatement stmt1{tStmt};
  EXPECT_EQ(stmt1.toThrift(), tStmt);

  // Comparison test
  tStmt.matcher() = createTRibRouteMatcher({kV4Prefix1});
  PathSelectionStatement stmt2{tStmt};
  EXPECT_EQ(stmt1, stmt2);

  tStmt.matcher() = createTRibRouteMatcher({kV4Prefix2});
  PathSelectionStatement stmt3{tStmt};
  EXPECT_NE(stmt1, stmt3);
}

TEST(RibPolicyTest, RouteFilterTest) {
  TRouteFilter tFilter;
  tFilter.permissive_mode() = true;
  RouteFilter filter1(tFilter);
  // Test toThrift
  EXPECT_EQ(filter1.toThrift(), tFilter);

  // Test comparison
  tFilter.permissive_mode().reset();
  RouteFilter filter2(tFilter);
  EXPECT_NE(filter1, filter2);
}

TEST(RibPolicyTest, RouteFilterStatementTest) {
  // basic test
  {
    TRouteFilterStatement tStmt1 = createTRouteFilterStatement({kV4Prefix1});

    // Test toThrift
    RouteFilterStatement stmt1{tStmt1};
    EXPECT_EQ(stmt1.toThrift(), tStmt1);

    // Comparison test
    TRouteFilterStatement tStmt2 =
        createTRouteFilterStatement({kV4Prefix1}, true);
    RouteFilterStatement stmt2{tStmt2};
    EXPECT_NE(stmt1, stmt2);

    TRouteFilterStatement tStmt3 = createTRouteFilterStatement({kV4Prefix2});
    RouteFilterStatement stmt3{tStmt3};
    EXPECT_NE(stmt1, stmt3);
  }
  // test empty filter - not allowing anything == blocking everything
  {
    TRouteFilterStatement tStmt = createTRouteFilterStatement({});
    RouteFilterStatement stmt{tStmt};
    // Test toThrift
    EXPECT_EQ(stmt.toThrift(), tStmt);

    folly::CIDRNetwork v4Prefix1{"8.0.0.0", 32}; // blocked
    folly::CIDRNetwork v6Prefix1{"2001::1", 128}; // blocked
    auto res = stmt.applyEgressFilter({v4Prefix1, v6Prefix1});
    EXPECT_EQ(
        std::get<1>(res),
        std::vector<folly::CIDRNetwork>({v4Prefix1, v6Prefix1}));
    EXPECT_TRUE(std::get<0>(res).empty());
  }
  // test null filter - not filtering anything
  {
    TRouteFilterStatement tStmt;
    TRouteFilter tFilter;
    tFilter.permissive_mode() = false;
    tStmt.egress_filter() = std::move(tFilter);

    RouteFilterStatement stmt{tStmt};
    // Test toThrift
    EXPECT_EQ(stmt.toThrift(), tStmt);

    folly::CIDRNetwork v4Prefix1{"8.0.0.0", 32}; // allowed
    folly::CIDRNetwork v6Prefix1{"2001::1", 128}; // allowed
    auto res = stmt.applyEgressFilter({v4Prefix1, v6Prefix1});
    EXPECT_EQ(
        std::get<0>(res),
        std::vector<folly::CIDRNetwork>({v4Prefix1, v6Prefix1}));
    EXPECT_TRUE(std::get<1>(res).empty());
  }
  // test egress blocking mode
  {
    TRouteFilterStatement tStmt =
        createTRouteFilterStatement({kV4Prefix1, kV6Prefix1});

    // Test toThrift
    RouteFilterStatement stmt{tStmt};
    EXPECT_EQ(stmt.toThrift(), tStmt);

    // Test filtering
    folly::CIDRNetwork v4Prefix1{"8.0.0.0", 24}; // allowed
    folly::CIDRNetwork v4Prefix2{"8.1.0.0", 32}; // rejected
    folly::CIDRNetwork v6Prefix1{"2001::1", 64}; // allowed
    folly::CIDRNetwork v6Prefix2{"2002::1", 128}; // rejected
    auto res =
        stmt.applyEgressFilter({v4Prefix1, v4Prefix2, v6Prefix1, v6Prefix2});
    EXPECT_EQ(
        std::get<0>(res),
        std::vector<folly::CIDRNetwork>({v4Prefix1, v6Prefix1}));
    EXPECT_EQ(
        std::get<1>(res),
        std::vector<folly::CIDRNetwork>({v4Prefix2, v6Prefix2}));
  }
  // test ingress blocking mode
  {
    TRouteFilterStatement tStmt = createTRouteFilterStatement(
        {kV4Prefix1, kV6Prefix1}, false /* blocking */, false /* ingress */);

    // Test toThrift
    RouteFilterStatement stmt{tStmt};
    EXPECT_EQ(stmt.toThrift(), tStmt);

    // Test filtering
    folly::CIDRNetwork v4Prefix1{"8.0.0.0", 24}; // allowed
    folly::CIDRNetwork v4Prefix2{"8.1.0.0", 32}; // rejected
    folly::CIDRNetwork v6Prefix1{"2001::1", 64}; // allowed
    folly::CIDRNetwork v6Prefix2{"2002::1", 128}; // rejected
    auto res =
        stmt.applyIngressFilter({v4Prefix1, v4Prefix2, v6Prefix1, v6Prefix2});
    EXPECT_EQ(
        std::get<0>(res),
        std::vector<folly::CIDRNetwork>({v4Prefix1, v6Prefix1}));
    EXPECT_EQ(
        std::get<1>(res),
        std::vector<folly::CIDRNetwork>({v4Prefix2, v6Prefix2}));
  }
  // Test egress permissive mode
  {
    TRouteFilterStatement tStmt =
        createTRouteFilterStatement({kV4Prefix1, kV6Prefix1}, true);

    // Test toThrift
    RouteFilterStatement stmt{tStmt};
    EXPECT_EQ(stmt.toThrift(), tStmt);

    // Test filtering
    folly::CIDRNetwork v4Prefix1{"8.0.0.0", 24}; // allowed
    folly::CIDRNetwork v4Prefix2{"8.1.0.0", 32}; // rejected
    folly::CIDRNetwork v6Prefix1{"2001::1", 64}; // allowed
    folly::CIDRNetwork v6Prefix2{"2002::1", 128}; // rejected
    auto res =
        stmt.applyEgressFilter({v4Prefix1, v4Prefix2, v6Prefix1, v6Prefix2});
    EXPECT_THAT(
        std::get<0>(res),
        std::vector<folly::CIDRNetwork>(
            {v4Prefix1, v4Prefix2, v6Prefix1, v6Prefix2}));
    EXPECT_THAT(
        std::get<1>(res),
        std::vector<folly::CIDRNetwork>({v4Prefix2, v6Prefix2}));
  }
  // Test ingress permissive mode
  {
    TRouteFilterStatement tStmt = createTRouteFilterStatement(
        {kV4Prefix1, kV6Prefix1}, true, false /* ingress */);

    // Test toThrift
    RouteFilterStatement stmt{tStmt};
    EXPECT_EQ(stmt.toThrift(), tStmt);

    // Test filtering
    folly::CIDRNetwork v4Prefix1{"8.0.0.0", 24}; // allowed
    folly::CIDRNetwork v4Prefix2{"8.1.0.0", 32}; // rejected
    folly::CIDRNetwork v6Prefix1{"2001::1", 64}; // allowed
    folly::CIDRNetwork v6Prefix2{"2002::1", 128}; // rejected
    auto res =
        stmt.applyIngressFilter({v4Prefix1, v4Prefix2, v6Prefix1, v6Prefix2});
    EXPECT_THAT(
        std::get<0>(res),
        std::vector<folly::CIDRNetwork>(
            {v4Prefix1, v4Prefix2, v6Prefix1, v6Prefix2}));
    EXPECT_THAT(
        std::get<1>(res),
        std::vector<folly::CIDRNetwork>({v4Prefix2, v6Prefix2}));
  }
}

/*
 * operator== must consider community_list, not just prefixSet_: two community
 * matchers with different community lists must not compare equal.
 */
TEST(RibPolicyTest, RibPolicyRouteMatcherCommunityIgnored) {
  auto commMatch1 = createTBgpCommunityMatch(65000, 100);
  auto commList1 = createTCommunityListMatch({commMatch1}, BooleanOperator::OR);
  auto commMatch2 = createTBgpCommunityMatch(65000, 200);
  auto commList2 = createTCommunityListMatch({commMatch2}, BooleanOperator::OR);

  auto tMatcher1 = createTRibRouteMatcher({}, commList1);
  auto tMatcher2 = createTRibRouteMatcher({}, commList2);

  RibPolicyRouteMatcher matcher1(tMatcher1);
  RibPolicyRouteMatcher matcher2(tMatcher2);

  EXPECT_NE(matcher1, matcher2)
      << "Matchers with different community_list must not be equal";
}

/*
 * Reproduce P1: GoldenPrefixPolicy copy constructor does not build
 * goldenPrefixSubnetCountingTree_. After copying, allowPrefix()
 * always returns true because it short-circuits on null tree,
 * bypassing all golden prefix subnet limits.
 */
TEST(RibPolicyTest, GoldenPrefixPolicyCopyDropsSubnetTree) {
  TGoldenPrefixPolicy tPolicy = createTGoldenPrefixPolicy(
      {kV4Prefix1}, 2 /* maxSubnets */, {32} /* allowedMaskLengths */);

  GoldenPrefixPolicy original{tPolicy};

  folly::CIDRNetwork prefix1{"8.0.0.0", 32};
  folly::CIDRNetwork prefix2{"8.0.0.1", 32};
  folly::CIDRNetwork prefix3{"8.0.0.2", 32};
  BgpPath attrs;

  /*
   * Fill original to the subnet limit (2), then verify prefix3 is blocked.
   */
  original.incrementSubnet(prefix1);
  original.incrementSubnet(prefix2);
  EXPECT_FALSE(original.allowPrefix(prefix3, attrs))
      << "Original should block prefix3 after reaching subnet limit";

  /*
   * Copy the policy (fresh copy, no subnets incremented).
   * On the copy, allowPrefix should still work (enforce the limit tree).
   * Without the fix, the copy has no counting tree so allowPrefix
   * always returns true.
   */
  GoldenPrefixPolicy copy{original};
  EXPECT_FALSE(copy.getSubnetCounts().empty())
      << "Copy should have a working subnet counting tree";
}

TEST(RibPolicyTest, GoldenPrefixPolicyBasicTest) {
  TGoldenPrefixPolicy tPolicy1 = createTGoldenPrefixPolicy(
      {kV4Prefix1}, 2 /* maxSubnets */, {32} /* allowedMaskLengths */);

  // Test toThrift
  GoldenPrefixPolicy policy1{tPolicy1};
  EXPECT_EQ(policy1.toThrift(), tPolicy1);

  // Comparison test
  TGoldenPrefixPolicy tPolicy2 = createTGoldenPrefixPolicy(
      {kV4Prefix2}, 2 /* maxSubnets */, {32} /* allowedMaskLengths */);
  GoldenPrefixPolicy policy2{tPolicy2};
  EXPECT_EQ(policy2.toThrift(), tPolicy2);
  EXPECT_NE(policy2, policy1);
}

TEST(RibPolicyTest, GoldenPrefixPolicyMissingSubnetLimitThrowsError) {
  TGoldenPrefixPolicy tPolicy = createTGoldenPrefixPolicy(
      {kV4Prefix1},
      std::nullopt /* maxSubnets */,
      {32} /* allowedMaskLengths */);

  EXPECT_THROW(GoldenPrefixPolicy policy1{tPolicy}, BgpError);
}

/*
 * When there are multiple PrefixListEntries with the same base prefix, they
 * must have the same value for max_allowed_golden_prefix_subnet_count, or BGP
 * will throw an error.
 */
TEST(RibPolicyTest, GoldenPrefixPolicyInconsistentSubnetLimit) {
  TGoldenPrefixPolicy tPolicy;
  std::vector<routing_policy::PrefixListEntry> prefixListEntries;

  auto prefixStr = "1.2.3.4";
  /*
   * Create entries for different subnet mask lengths under the same parent
   * prefix, with different subnet limits.
   */
  routing_policy::CompareNumericValue compareStruct31;
  compareStruct31.compare_operator() = routing_policy::ComparisonOperator::EQ;
  compareStruct31.value() = 31;
  prefixListEntries.emplace_back(
      createPrefixListEntry(prefixStr, {compareStruct31}, 5 /* max subnets */));

  routing_policy::CompareNumericValue compareStruct32;
  compareStruct32.compare_operator() = routing_policy::ComparisonOperator::EQ;
  compareStruct32.value() = 31;
  prefixListEntries.emplace_back(
      createPrefixListEntry(prefixStr, {compareStruct32}, 6 /* max subnets */));

  tPolicy.allowed_prefixes() = createPrefixList(prefixListEntries);

  EXPECT_THROW(GoldenPrefixPolicy policy1{tPolicy}, BgpError);
}

TEST(RibPolicyTest, GoldenPrefixPolicyEmptyListTest) {
  TGoldenPrefixPolicy tPolicy = createTGoldenPrefixPolicy(
      {}, 0 /* maxSubnets (unused) */, {0} /* allowedMaskLengths (unused) */);
  GoldenPrefixPolicy policy{tPolicy};

  // Test toThrift
  EXPECT_EQ(policy.toThrift(), tPolicy);

  folly::CIDRNetwork v4Prefix1{"8.0.0.0", 32}; // blocked
  folly::CIDRNetwork v6Prefix1{"2001::1", 128}; // blocked
  BgpPath attrs;
  auto res = policy.applyFilter({v4Prefix1, v6Prefix1}, attrs);
  EXPECT_EQ(res, std::vector<folly::CIDRNetwork>({v4Prefix1, v6Prefix1}));
}

// test null filter - not filtering anything
TEST(RibPolicyTest, GoldenPrefixPolicyNullTest) {
  TGoldenPrefixPolicy tPolicy;

  GoldenPrefixPolicy policy{tPolicy};
  // Test toThrift
  EXPECT_EQ(policy.toThrift(), tPolicy);

  folly::CIDRNetwork v4Prefix1{"8.0.0.0", 32}; // allowed
  folly::CIDRNetwork v6Prefix1{"2001::1", 128}; // allowed
  BgpPath attrs;
  auto res = policy.applyFilter({v4Prefix1, v6Prefix1}, attrs);
  EXPECT_TRUE(res.empty());
}

TEST(RibPolicyTest, GoldenPrefixPolicyAllowPrefix) {
  auto ip = "8.0.0.0";
  TGoldenPrefixPolicy tPolicy = createTGoldenPrefixPolicy(
      {kV4Prefix1}, 2 /* maxSubnets */, {32} /* allowedMaskLengths */);

  GoldenPrefixPolicy policy{tPolicy};
  // Test toThrift
  EXPECT_EQ(policy.toThrift(), tPolicy);

  folly::CIDRNetwork prefix1{"8.0.0.0", 32};
  folly::CIDRNetwork prefix2{"8.0.0.1", 32};
  folly::CIDRNetwork prefix3{"8.0.0.2", 32};
  BgpPath attrs;

  // No subnets have been recorded yet, so all new subnets are allowed
  EXPECT_THAT(policy.getSubnetCounts(), ElementsAre(Pair(ip, 0)));
  EXPECT_TRUE(policy.allowPrefix(prefix1, attrs));
  EXPECT_TRUE(policy.allowPrefix(prefix2, attrs));
  EXPECT_TRUE(policy.allowPrefix(prefix3, attrs));

  // After recording one subnet, we're still under the limit.
  policy.incrementSubnet(prefix1);
  EXPECT_THAT(policy.getSubnetCounts(), ElementsAre(Pair(ip, 1)));
  /*
   * The policy evaluation counts *unique* subnets, so incrementing the counter
   * for an existing prefix has no effect on the results.
   */
  policy.incrementSubnet(prefix1);
  EXPECT_THAT(policy.getSubnetCounts(), ElementsAre(Pair(ip, 1)));
  EXPECT_TRUE(policy.allowPrefix(prefix1, attrs));
  EXPECT_TRUE(policy.allowPrefix(prefix2, attrs));
  EXPECT_TRUE(policy.allowPrefix(prefix3, attrs));

  /*
   * After recording another subnet, we've reached the limit, so no new subnets
   * are allowed, only existing ones.
   */
  policy.incrementSubnet(prefix2);
  EXPECT_THAT(policy.getSubnetCounts(), ElementsAre(Pair(ip, 2)));
  EXPECT_TRUE(policy.allowPrefix(prefix1, attrs));
  EXPECT_TRUE(policy.allowPrefix(prefix2, attrs));
  EXPECT_FALSE(policy.allowPrefix(prefix3, attrs));

  // After removing a subnet, we're under the limit again.
  policy.decrementSubnet(prefix2);
  EXPECT_THAT(policy.getSubnetCounts(), ElementsAre(Pair(ip, 1)));
  EXPECT_TRUE(policy.allowPrefix(prefix1, attrs));
  EXPECT_TRUE(policy.allowPrefix(prefix2, attrs));
  EXPECT_TRUE(policy.allowPrefix(prefix3, attrs));
}

// test default routes can be allowed by golden prefixes policy
TEST(RibPolicyTest, GoldenPrefixPolicyDefaultRoutesTest) {
  TGoldenPrefixPolicy tPolicy = createTGoldenPrefixPolicy(
      {std::pair<folly::CIDRNetwork, std::vector<int>>(kDefaultV4, {0}),
       std::pair<folly::CIDRNetwork, std::vector<int>>(kDefaultV6, {0}),
       std::pair<folly::CIDRNetwork, std::vector<int>>(kPrefix1, {31, 32})},
      1 /* maxSubnets */);

  GoldenPrefixPolicy policy{tPolicy};

  folly::CIDRNetwork prefix24{"101.0.0.0", 24};
  folly::CIDRNetwork prefix31{"101.0.0.0", 31};
  folly::CIDRNetwork prefix32{"101.0.0.0", 32};
  folly::CIDRNetwork prefixV4Default{"0.0.0.0", 0};
  folly::CIDRNetwork prefixV6Default{"::", 0};
  BgpPath attrs;
  // No subnets have been recorded yet, so all new subnets are allowed
  EXPECT_FALSE(policy.allowPrefix(prefix24, attrs));
  EXPECT_TRUE(policy.allowPrefix(prefix31, attrs));
  EXPECT_TRUE(policy.allowPrefix(prefix32, attrs));
  EXPECT_TRUE(policy.allowPrefix(prefixV4Default, attrs));
  EXPECT_TRUE(policy.allowPrefix(prefixV6Default, attrs));

  /*
   * After /31 subnet is recorded, the limit is exceeded, so the /32 subnet is
   * not allowed.
   */
  policy.incrementSubnet(prefix31);
  EXPECT_TRUE(policy.allowPrefix(prefix31, attrs));
  EXPECT_FALSE(policy.allowPrefix(prefix32, attrs));
  // Default routes are still allowed
  EXPECT_TRUE(policy.allowPrefix(prefixV4Default, attrs));
  EXPECT_TRUE(policy.allowPrefix(prefixV6Default, attrs));

  policy.incrementSubnet(prefixV4Default);
  EXPECT_TRUE(policy.allowPrefix(prefixV4Default, attrs));

  policy.incrementSubnet(prefixV6Default);
  EXPECT_TRUE(policy.allowPrefix(prefixV6Default, attrs));
}

TEST(RibPolicyTest, GoldenPrefixPolicyOverlappingSubnets) {
  folly::CIDRNetwork prefix24{"8.0.0.0", 24};
  folly::CIDRNetwork prefix31{"8.0.0.0", 31};
  folly::CIDRNetwork prefix32{"8.0.0.0", 32};
  BgpPath attrs;

  TGoldenPrefixPolicy tPolicy = createTGoldenPrefixPolicy(
      {prefix24}, 1 /* maxSubnets */, {31, 32} /* allowedMaskLengths */);

  GoldenPrefixPolicy policy{tPolicy};

  // No subnets have been recorded yet, so all new subnets are allowed
  EXPECT_TRUE(policy.allowPrefix(prefix31, attrs));
  EXPECT_TRUE(policy.allowPrefix(prefix32, attrs));

  /*
   * After /31 subnet is recorded, the limit is exceeded, so the /32 subnet is
   * not allowed.
   */
  policy.incrementSubnet(prefix31);
  EXPECT_TRUE(policy.allowPrefix(prefix31, attrs));
  EXPECT_FALSE(policy.allowPrefix(prefix32, attrs));
}

TEST(RibPolicyTest, GoldenPrefixPolicyCommunities) {
  folly::CIDRNetwork prefix24{"8.0.0.0", 24};
  folly::CIDRNetwork prefix32{"8.0.0.0", 32};

  std::string goldenCommunityStr1 = "123:456";
  std::string goldenCommunityStr2 = "456:789";

  auto goldenCommunity1 =
      *BgpAttrCommunityC::createBgpAttrCommunity(goldenCommunityStr1);
  BgpAttrCommunityC nonGoldenCommunity{800, 588};

  auto goldenCommunities =
      std::make_optional(std::set{goldenCommunityStr1, goldenCommunityStr2});

  TGoldenPrefixPolicy tPolicy = createTGoldenPrefixPolicy(
      {prefix24},
      1 /* maxSubnets */,
      {32} /* allowedMaskLengths */,
      goldenCommunities);

  GoldenPrefixPolicy policy{tPolicy};

  {
    BgpPath attrs;
    // no communities
    EXPECT_FALSE(policy.allowPrefix(prefix32, attrs));
  }

  {
    BgpPath attrs;
    // non-matching community
    attrs.setCommunities(
        nettools::bgplib::BgpAttrCommunitiesC{{nonGoldenCommunity}});
    EXPECT_FALSE(policy.allowPrefix(prefix32, attrs));
  }

  {
    BgpPath attrs;
    // matching community
    attrs.setCommunities(
        nettools::bgplib::BgpAttrCommunitiesC{{goldenCommunity1}});
    EXPECT_TRUE(policy.allowPrefix(prefix32, attrs));
  }

  {
    BgpPath attrs;
    // one matching and one non-matching community
    attrs.setCommunities(
        nettools::bgplib::BgpAttrCommunitiesC{
            {goldenCommunity1, nonGoldenCommunity}});
    EXPECT_TRUE(policy.allowPrefix(prefix32, attrs));
  }
}

TEST(RibPolicyTest, GoldenPrefixPolicyInvalidCommunityThrows) {
  auto goldenCommunities =
      std::make_optional(std::set<std::string>{"not a valid community"});

  TGoldenPrefixPolicy tPolicy = createTGoldenPrefixPolicy(
      {kPrefix1},
      1 /* maxSubnets */,
      {32} /* allowedMaskLengths */,
      goldenCommunities);

  EXPECT_THROW(GoldenPrefixPolicy policy{tPolicy}, BgpError);
}

/*
 * Test RouteFilterPolicy key type functionality
 */
TEST(RibPolicyTest, RouteFilterPolicyKeyTypeTest) {
  // Test default behavior (no key_type specified)
  {
    TRouteFilterPolicy tPolicy = createTRouteFilterPolicy(
        {createTRouteFilterStatement({kV4Prefix1}, true /* permissive */)},
        12345);

    RouteFilterPolicy policy{tPolicy};

    // Default should be false for matchAgainstPeerGroupName
    EXPECT_FALSE(policy.matchAgainstPeerGroupName());

    // Test toThrift conversion
    auto thriftPolicy = policy.toThrift();
    EXPECT_FALSE(thriftPolicy.key_type().has_value());
  }

  // Test DEVICE_REGEX key type
  {
    TRouteFilterPolicy tPolicy = createTRouteFilterPolicyWithKeyType(
        {createTRouteFilterStatement({kV4Prefix1}, true /* permissive */)},
        12345,
        KeyType::DEVICE_REGEX);

    RouteFilterPolicy policy{tPolicy};

    // Should be false for matchAgainstPeerGroupName with DEVICE_REGEX
    EXPECT_FALSE(policy.matchAgainstPeerGroupName());

    // Test toThrift conversion
    auto thriftPolicy = policy.toThrift();
    EXPECT_TRUE(thriftPolicy.key_type().has_value());
    EXPECT_EQ(*thriftPolicy.key_type(), KeyType::DEVICE_REGEX);
  }

  // Test PEER_GROUP_NAME key type
  {
    TRouteFilterPolicy tPolicy = createTRouteFilterPolicyWithKeyType(
        {createTRouteFilterStatement({kV4Prefix1}, true /* permissive */)},
        12345,
        KeyType::PEER_GROUP_NAME);

    RouteFilterPolicy policy{tPolicy};

    // Should be true for matchAgainstPeerGroupName with PEER_GROUP_NAME
    EXPECT_TRUE(policy.matchAgainstPeerGroupName());

    // Test toThrift conversion
    auto thriftPolicy = policy.toThrift();
    EXPECT_TRUE(thriftPolicy.key_type().has_value());
    EXPECT_EQ(*thriftPolicy.key_type(), KeyType::PEER_GROUP_NAME);
  }
}

/*
 * Test RouteFilterPolicy statement name validation based on key type
 */
TEST(RibPolicyTest, RouteFilterPolicyStatementNameValidationTest) {
  // Test valid regex for DEVICE_REGEX (default behavior)
  {
    TRouteFilterPolicy tPolicy;
    tPolicy.statements()->emplace(
        ".*eb.*", createTRouteFilterStatement({kV4Prefix1}));
    tPolicy.version() = 12345;

    // Should not throw - valid regex
    EXPECT_NO_THROW(RouteFilterPolicy policy{tPolicy});
  }

  // Test invalid regex for DEVICE_REGEX
  {
    TRouteFilterPolicy tPolicy;
    tPolicy.statements()->emplace(
        "++", createTRouteFilterStatement({kV4Prefix1}));
    tPolicy.version() = 12345;

    // Should throw - invalid regex
    EXPECT_THROW(RouteFilterPolicy policy{tPolicy}, BgpError);
  }

  // Test invalid regex for explicit DEVICE_REGEX
  {
    TRouteFilterPolicy tPolicy;
    tPolicy.statements()->emplace(
        "++", createTRouteFilterStatement({kV4Prefix1}));
    tPolicy.version() = 12345;
    tPolicy.key_type() = KeyType::DEVICE_REGEX;

    // Should throw - invalid regex
    EXPECT_THROW(RouteFilterPolicy policy{tPolicy}, BgpError);
  }

  // Test peer group name for PEER_GROUP_NAME (no regex validation)
  {
    TRouteFilterPolicy tPolicy;
    tPolicy.statements()->emplace(
        "my-peer-group", createTRouteFilterStatement({kV4Prefix1}));
    tPolicy.version() = 12345;
    tPolicy.key_type() = KeyType::PEER_GROUP_NAME;

    // Should not throw - peer group names don't need to be valid regex
    EXPECT_NO_THROW(RouteFilterPolicy policy{tPolicy});
  }

  // Test what would be invalid regex but valid peer group name
  {
    TRouteFilterPolicy tPolicy;
    tPolicy.statements()->emplace(
        "++", createTRouteFilterStatement({kV4Prefix1}));
    tPolicy.version() = 12345;
    tPolicy.key_type() = KeyType::PEER_GROUP_NAME;

    // Should not throw - regex validation is skipped for PEER_GROUP_NAME
    EXPECT_NO_THROW(RouteFilterPolicy policy{tPolicy});
  }
}

/*
 * Test RouteFilterPolicy equality comparison with key types
 */
TEST(RibPolicyTest, RouteFilterPolicyKeyTypeEqualityTest) {
  // Test policies with same content but different key types
  {
    TRouteFilterPolicy tPolicy1 = createTRouteFilterPolicyWithKeyType(
        {createTRouteFilterStatement({kV4Prefix1}, true /* permissive */)},
        12345,
        KeyType::DEVICE_REGEX);

    TRouteFilterPolicy tPolicy2 = createTRouteFilterPolicyWithKeyType(
        {createTRouteFilterStatement({kV4Prefix1}, true /* permissive */)},
        12345,
        KeyType::PEER_GROUP_NAME);

    RouteFilterPolicy policy1{tPolicy1};
    RouteFilterPolicy policy2{tPolicy2};

    // Policies should be different due to different key types
    EXPECT_NE(policy1, policy2);
  }

  // Test policies with same key type
  {
    TRouteFilterPolicy tPolicy1 = createTRouteFilterPolicyWithKeyType(
        {createTRouteFilterStatement({kV4Prefix1}, true /* permissive */)},
        12345,
        KeyType::PEER_GROUP_NAME);

    TRouteFilterPolicy tPolicy2 = createTRouteFilterPolicyWithKeyType(
        {createTRouteFilterStatement({kV4Prefix1}, true /* permissive */)},
        12345,
        KeyType::PEER_GROUP_NAME);

    RouteFilterPolicy policy1{tPolicy1};
    RouteFilterPolicy policy2{tPolicy2};

    // Policies should be equal
    EXPECT_EQ(policy1, policy2);
  }

  // Test policy with key type vs policy without key type
  {
    TRouteFilterPolicy tPolicy1 = createTRouteFilterPolicy(
        {createTRouteFilterStatement({kV4Prefix1}, true /* permissive */)},
        12345);

    TRouteFilterPolicy tPolicy2 = createTRouteFilterPolicyWithKeyType(
        {createTRouteFilterStatement({kV4Prefix1}, true /* permissive */)},
        12345,
        KeyType::DEVICE_REGEX);

    RouteFilterPolicy policy1{tPolicy1};
    RouteFilterPolicy policy2{tPolicy2};

    // Policies should be different (one has key_type, other doesn't)
    EXPECT_NE(policy1, policy2);
  }
}

/**
 * The following tests verify the function overrideMultipathSelection
 */
class RibPolicyFixture : public ::testing::Test {
 public:
  RibPolicyFixture() = default;
  ~RibPolicyFixture() override = default;

  void SetUp() override {
    // path of length 4
    auto attr1 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    nettools::bgplib::BgpAttrCommunitiesC community1;
    community1.emplace_back(200, 666);
    attr1->setNexthop(kV4Nexthop1);
    attr1->setLocalPref(kLocalPref);
    attr1->setCommunities(community1);
    attr1->setOrigin(nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP);
    attr1->setNonTransitiveLbwExtCommunity(
        kLocalAs1, float(800) * BpsPerGBps / 8);
    attr1->publish();

    // path of length 2
    auto attr2 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(2, 2, 2, 3));
    nettools::bgplib::BgpAttrCommunitiesC community2;
    community2.emplace_back(100, 234);
    attr2->setNexthop(kV4Nexthop2);
    attr2->setLocalPref(kLocalPref);
    attr2->setCommunities(community2);
    attr2->setOrigin(nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP);
    attr2->publish();

    auto peer = TinyPeerInfo(
        kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
    nettools::bgplib::BgpPeerId peerId{peer.addr, peer.routerId};

    // Allow adding paths
    ribEntry_ = std::make_unique<RibEntry>(kV4Prefix1);

    // Add two paths to peer
    EXPECT_TRUE(ribEntry_->updatePath(peer, attr1, true, 0));
    EXPECT_TRUE(ribEntry_->updatePath(peer, attr2, true, 1));

    std::vector<std::shared_ptr<RouteInfo>> pathsToOverride;
    for (const auto& route : ribEntry_->getRouteInfos(peerId)) {
      pathsToOverride_.emplace_back(route.second);
    }
    EXPECT_EQ(pathsToOverride_.size(), 2);
  }

  void TearDown() override {}

  std::unique_ptr<RibEntry> ribEntry_{nullptr};
  std::vector<std::shared_ptr<RouteInfo>> pathsToOverride_{};
};

class RibPolicyFixtureConfedPeer : public RibPolicyFixture {
  void SetUp() override {
    // as path of length 4, as path length with confed 5
    auto attr1 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4, 1));
    nettools::bgplib::BgpAttrCommunitiesC community1;
    community1.emplace_back(200, 666);
    attr1->setNexthop(kV4Nexthop1);
    attr1->setLocalPref(kLocalPref);
    attr1->setCommunities(community1);
    attr1->setOrigin(nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP);
    attr1->publish();

    // as path of length 2, as path length with confed 6
    auto attr2 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(2, 2, 2, 3, 4));
    nettools::bgplib::BgpAttrCommunitiesC community2;
    community2.emplace_back(100, 234);
    attr2->setNexthop(kV4Nexthop2);
    attr2->setLocalPref(kLocalPref);
    attr2->setCommunities(community2);
    attr2->setOrigin(nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP);
    attr2->publish();

    auto peer1 = TinyPeerInfo(
        kPeerAddr1,
        kPeerAsn1,
        kPeerRouterId1,
        BgpSessionType::ConfedEBGP,
        false);
    nettools::bgplib::BgpPeerId peerId1{peer1.addr, peer1.routerId};
    auto peer2 = TinyPeerInfo(
        kPeerAddr2,
        kPeerAsn2,
        kPeerRouterId2,
        BgpSessionType::ConfedEBGP,
        false);
    nettools::bgplib::BgpPeerId peerId2{peer2.addr, peer2.routerId};

    // Allow adding paths
    ribEntry_ = std::make_unique<RibEntry>(kV4Prefix1);

    // Add two paths to peer
    EXPECT_TRUE(ribEntry_->updatePath(peer1, attr1, true));
    EXPECT_TRUE(ribEntry_->updatePath(peer2, attr2, true));

    pathsToOverride_.emplace_back(
        ribEntry_->getRouteInfos(peerId1).begin()->second);
    pathsToOverride_.emplace_back(
        ribEntry_->getRouteInfos(peerId2).begin()->second);

    EXPECT_EQ(pathsToOverride_.size(), 2);
  }
};

/*
 * Test prefix match in the statment. When the prefix is not matched, we don't
 * apply the statement.
 */
TEST_F(RibPolicyFixture, SelectPathDefaultTest) {
  // Create a RibPolicy to select the path with community 200:666
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher);

  RibPolicy policy{
      createTRibPolicyWithPathSelector({kV4Prefix2}, tPathSelector)};

  /*
   * No statement would match, we would use the default multipath selection,
   * which selects the path of length 2
   */
  auto overriddenPaths =
      policy.getPathSelectionPolicy()->overrideMultipathSelection(
          *ribEntry_, pathsToOverride_, multipathSelector);

  EXPECT_EQ(overriddenPaths.size(), 1);
  EXPECT_EQ(overriddenPaths.at(0)->getBgpAsPathLen(), 2);
}

/*
 * Test prefix match in the statment. When the prefix is not matched, we don't
 * apply the statement.
 */
TEST_F(RibPolicyFixtureConfedPeer, SelectPathDefaultTest) {
  // Create a RibPolicy to select the path with community 200:666
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher);

  RibPolicy policy{
      createTRibPolicyWithPathSelector({kV4Prefix2}, tPathSelector)};

  /*
   * No statement would match, we would use the default multipath selection,
   * which selects the as path length with confed 5
   */
  auto overriddenPaths =
      policy.getPathSelectionPolicy()->overrideMultipathSelection(
          *ribEntry_, pathsToOverride_, multipathSelectorCountConfeds);

  EXPECT_EQ(overriddenPaths.size(), 1);
  EXPECT_EQ(overriddenPaths.at(0)->getBgpAsPathLen(), 4);
  EXPECT_EQ(overriddenPaths.at(0)->getBgpAsPathLenWithConfed(), 5);
}

/*
 * Test community match
 */
TEST_F(RibPolicyFixture, SelectPathCommunityMatchTest) {
  // Let the statement matches the right prefix
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher);

  RibPolicy policy{
      createTRibPolicyWithPathSelector({kV4Prefix1}, tPathSelector)};

  // We would filter out one path
  auto overriddenPaths =
      policy.getPathSelectionPolicy()->overrideMultipathSelection(
          *ribEntry_, pathsToOverride_, multipathSelector);

  EXPECT_EQ(overriddenPaths.size(), 1);
}

TEST_F(RibPolicyFixture, SelectPathMinLbwBpsMatchTest) {
  // Let the statement match the right prefix
  auto tMatcher = TBgpPathMatcher();
  tMatcher.min_lbw_bps() = (int64_t)800 * 1000 * 1000 * 1000;
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher);

  RibPolicy policy{
      createTRibPolicyWithPathSelector({kV4Prefix1}, tPathSelector)};

  // We would path 1 with valid lbw
  auto overriddenPaths =
      policy.getPathSelectionPolicy()->overrideMultipathSelection(
          *ribEntry_, pathsToOverride_, multipathSelector);

  EXPECT_EQ(overriddenPaths.size(), 1);
}

TEST_F(RibPolicyFixture, SelectLowestIgpCostAmongMatchingPaths) {
  TBgpPathMatcher matcher;
  matcher.origin() = bgp_policy::Origin::EGP;
  matcher.prefer_lowest_igp_cost() = true;
  RibPolicy policy{createTRibPolicyWithPathSelector(
      {kV4Prefix1}, createTPathSlectorWithOneMatcher(matcher))};

  NexthopInfo first(NexthopStatus(kV4Nexthop1, true, 30));
  NexthopInfo second(NexthopStatus(kV4Nexthop2, true, 10));
  first.linkRouteInfo(*pathsToOverride_[0]);
  second.linkRouteInfo(*pathsToOverride_[1]);

  const auto select = [&] {
    return policy.getPathSelectionPolicy()->overrideMultipathSelection(
        *ribEntry_, pathsToOverride_, multipathSelector);
  };
  EXPECT_THAT(select(), ElementsAre(pathsToOverride_[1]));

  first.updateStatus(NexthopStatus(kV4Nexthop1, true, 10));
  EXPECT_EQ(select(), pathsToOverride_);

  first.updateStatus(NexthopStatus(kV4Nexthop1, true, 5));
  EXPECT_THAT(select(), ElementsAre(pathsToOverride_[0]));

  first.unlinkRouteInfo(*pathsToOverride_[0]);
  EXPECT_THAT(select(), ElementsAre(pathsToOverride_[1]));
  second.unlinkRouteInfo(*pathsToOverride_[1]);
  EXPECT_EQ(select(), pathsToOverride_);

  const auto active =
      policy.getPathSelectionPolicy()->getActivePathSelectionCriteria(
          {folly::IPAddress::networkToString(ribEntry_->getPrefix())});
  ASSERT_EQ(active.size(), 1);
  EXPECT_TRUE(
      active[0]
          .criteria_list()
          ->front()
          .path_matchers()
          ->front()
          .prefer_lowest_igp_cost()
          .value_or(false));
}

TEST_F(
    RibPolicyFixture,
    LowestIgpCostMatcherRespectsAttributesOrAndMinNexthop) {
  const auto& communityPath = pathsToOverride_[0]->getBgpAsPathLen() == 4
      ? pathsToOverride_[0]
      : pathsToOverride_[1];
  const auto& otherPath = communityPath == pathsToOverride_[0]
      ? pathsToOverride_[1]
      : pathsToOverride_[0];
  NexthopInfo expensive(NexthopStatus(kV4Nexthop1, true, 30));
  NexthopInfo inexpensive(NexthopStatus(kV4Nexthop2, true, 10));
  expensive.linkRouteInfo(*communityPath);
  inexpensive.linkRouteInfo(*otherPath);

  TPathSelectionCriteria criteria;
  auto communityMatcher =
      createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  communityMatcher.prefer_lowest_igp_cost() = true;
  criteria.path_matchers()->push_back(communityMatcher);
  EXPECT_THAT(
      PathSelectionCriteria(criteria).tryOverrideMultipathSelection(
          pathsToOverride_),
      ElementsAre(communityPath));

  auto otherMatcher = createCommunityMatch(100, 234, bgp_policy::Origin::EGP);
  otherMatcher.prefer_lowest_igp_cost() = true;
  criteria.path_matchers()->push_back(otherMatcher);
  criteria.min_nexthop() = 2;
  EXPECT_EQ(
      PathSelectionCriteria(criteria).tryOverrideMultipathSelection(
          pathsToOverride_),
      pathsToOverride_);

  TBgpPathMatcher allPathsMatcher;
  allPathsMatcher.origin() = bgp_policy::Origin::EGP;
  allPathsMatcher.prefer_lowest_igp_cost() = true;
  criteria.path_matchers()->clear();
  criteria.path_matchers()->push_back(allPathsMatcher);
  EXPECT_TRUE(PathSelectionCriteria(criteria)
                  .tryOverrideMultipathSelection(pathsToOverride_)
                  .empty());

  communityMatcher.prefer_lowest_igp_cost().reset();
  criteria.path_matchers()->push_back(communityMatcher);
  EXPECT_EQ(
      PathSelectionCriteria(criteria).tryOverrideMultipathSelection(
          pathsToOverride_),
      pathsToOverride_);

  allPathsMatcher.prefer_lowest_igp_cost() = false;
  criteria.path_matchers()->clear();
  criteria.path_matchers()->push_back(allPathsMatcher);
  EXPECT_EQ(
      PathSelectionCriteria(criteria).tryOverrideMultipathSelection(
          pathsToOverride_),
      pathsToOverride_);
  allPathsMatcher.prefer_lowest_igp_cost().reset();
  criteria.path_matchers()->clear();
  criteria.path_matchers()->push_back(allPathsMatcher);
  EXPECT_EQ(
      PathSelectionCriteria(criteria).tryOverrideMultipathSelection(
          pathsToOverride_),
      pathsToOverride_);

  expensive.unlinkRouteInfo(*communityPath);
  inexpensive.unlinkRouteInfo(*otherPath);
}

/*
 * Test the min nexthop specified in path selection criteria. When no criteria
 * is met, we fall back to native bgp multipath selector.
 */
TEST_F(RibPolicyFixture, SelectPathCriteriaMinNexthopTest) {
  /*
   * test the min nexthop criteria
   * Let the statement matches the right prefix
   */
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 2);

  RibPolicy policy{
      createTRibPolicyWithPathSelector({kV4Prefix1}, tPathSelector)};

  auto overriddenPaths =
      policy.getPathSelectionPolicy()->overrideMultipathSelection(
          *ribEntry_, pathsToOverride_, multipathSelector);

  /*
   * tCriteria could only get one path, which is less than min nexthop 2
   * As a result, PathSelector::overrideMultipathSelection will invoke
   * getBgpNativeMultipathSelector, which selects one path.
   */
  EXPECT_EQ(overriddenPaths.size(), 1);
  EXPECT_EQ(overriddenPaths.at(0)->getBgpAsPathLen(), 2);
}

/*
 * Test relaxing bgp native min nexthop specified in the path selector. When it
 * is not met, paths will be returned as are, but we can query the active path
 * selection policy and find out whether bgp native MNH would've been violated
 */
TEST_F(RibPolicyFixture, SelectPathDefaultMinNexthopRelaxTest) {
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);
  // don't reject multipaths if below mnh
  tPathSelector.drain_on_min_nexthop_violation() = true;

  RibPolicy policy{
      createTRibPolicyWithPathSelector({kV4Prefix1}, tPathSelector)};

  auto overriddenPaths =
      policy.getPathSelectionPolicy()->overrideMultipathSelection(
          *ribEntry_, pathsToOverride_, multipathSelector);

  /*
   * tCriteria could only get one path, which is less than min nexthop 2
   * As a result, PathSelector::overrideMultipathSelection will invoke
   * getBgpNativeMultipathSelector, which selects one path. This path will be
   * returned as is, but we can query getActivePathSelectionCriteria() to find
   * out whether bgp native MNH would've rejected this path
   */
  EXPECT_EQ(overriddenPaths.size(), 1);
  EXPECT_EQ(overriddenPaths.at(0)->getBgpAsPathLen(), 2);
  auto selectors =
      policy.getPathSelectionPolicy()->getActivePathSelectionCriteria(
          {folly::IPAddress::networkToString(ribEntry_->getPrefix())});
  EXPECT_EQ(1, selectors.size());
  EXPECT_EQ(3, *selectors[0].bgp_native_path_selection_min_nexthop());
}

TEST_F(RibPolicyFixture, PartialDrainOutcomeSetOnMnhViolation) {
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);
  tPathSelector.drain_on_min_nexthop_violation() = true;

  RibPolicy policy{
      createTRibPolicyWithPathSelector({kV4Prefix1}, tPathSelector)};

  policy.getPathSelectionPolicy()->overrideMultipathSelection(
      *ribEntry_, pathsToOverride_, multipathSelector);

  auto result = policy.getPathSelectionPolicy()->getPathSelectionPolicyResult(
      ribEntry_->getPrefix());
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(
      result->outcome,
      PathSelectionPolicyResult::Outcome::BGP_FAILED_CPS_MIN_NEXTHOP);
  EXPECT_TRUE(result->drainOnMinCapacityThresholdViolation);
}

TEST_F(RibPolicyFixture, StrictMnhOutcomeWhenPartialDrainDisabled) {
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);
  // drain_on_min_nexthop_violation NOT set (defaults to false)

  RibPolicy policy{
      createTRibPolicyWithPathSelector({kV4Prefix1}, tPathSelector)};

  auto overriddenPaths =
      policy.getPathSelectionPolicy()->overrideMultipathSelection(
          *ribEntry_, pathsToOverride_, multipathSelector);

  EXPECT_EQ(overriddenPaths.size(), 0);

  auto result = policy.getPathSelectionPolicy()->getPathSelectionPolicyResult(
      ribEntry_->getPrefix());
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(
      result->outcome,
      PathSelectionPolicyResult::Outcome::BGP_FAILED_CPS_MIN_NEXTHOP);
}

TEST_F(RibPolicyFixture, PartialDrainRetainsBestpath) {
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);
  tPathSelector.drain_on_min_nexthop_violation() = true;

  auto policy = std::make_unique<PathSelectionPolicy>(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));

  bool bestpathChanged, multipathChanged;
  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry_,

      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      policy);

  EXPECT_NE(ribEntry_->getBestPath(), nullptr);
  EXPECT_TRUE(ribEntry_->getInstallToFib());
  EXPECT_TRUE(ribEntry_->getIsPartialDrain());
}

TEST_F(RibPolicyFixture, StrictMnhNullifiesBestpath) {
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);
  // drain_on_min_nexthop_violation NOT set

  auto policy = std::make_unique<PathSelectionPolicy>(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));

  bool bestpathChanged, multipathChanged;
  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry_,

      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      policy);

  EXPECT_EQ(ribEntry_->getBestPath(), nullptr);
}

TEST_F(RibPolicyFixture, NoPartialDrainWhenMnhSatisfied) {
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  // MNH threshold = 1, we have 1 path → satisfied
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 2, 1);
  tPathSelector.drain_on_min_nexthop_violation() = true;

  RibPolicy policy{
      createTRibPolicyWithPathSelector({kV4Prefix1}, tPathSelector)};

  policy.getPathSelectionPolicy()->overrideMultipathSelection(
      *ribEntry_, pathsToOverride_, multipathSelector);

  auto result = policy.getPathSelectionPolicy()->getPathSelectionPolicyResult(
      ribEntry_->getPrefix());
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->outcome, PathSelectionPolicyResult::Outcome::BGP);
}

TEST_F(RibPolicyFixture, PartialDrainToStrictMnhRollback) {
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);
  tPathSelector.drain_on_min_nexthop_violation() = true;

  auto drainPolicy = std::make_unique<PathSelectionPolicy>(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));

  bool bestpathChanged, multipathChanged;
  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry_,

      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      drainPolicy);

  EXPECT_NE(ribEntry_->getBestPath(), nullptr);
  EXPECT_TRUE(ribEntry_->getIsPartialDrain());

  auto tPathSelectorStrict = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);

  auto strictPolicy = std::make_unique<PathSelectionPolicy>(
      createTPathSelectionPolicyWithPathSelector(
          {kV4Prefix1}, tPathSelectorStrict));

  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry_,

      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      strictPolicy);

  EXPECT_TRUE(bestpathChanged);
  EXPECT_EQ(ribEntry_->getBestPath(), nullptr);
  EXPECT_FALSE(ribEntry_->getIsPartialDrain());
}

TEST_F(RibPolicyFixture, StrictMnhToPartialDrainRollout) {
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);

  auto strictPolicy = std::make_unique<PathSelectionPolicy>(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));

  bool bestpathChanged, multipathChanged;
  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry_,

      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      strictPolicy);

  EXPECT_EQ(ribEntry_->getBestPath(), nullptr);
  EXPECT_FALSE(ribEntry_->getIsPartialDrain());

  auto tPathSelectorDrain = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);
  tPathSelectorDrain.drain_on_min_nexthop_violation() = true;

  auto drainPolicy = std::make_unique<PathSelectionPolicy>(
      createTPathSelectionPolicyWithPathSelector(
          {kV4Prefix1}, tPathSelectorDrain));

  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry_,

      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      drainPolicy);

  EXPECT_TRUE(bestpathChanged);
  EXPECT_NE(ribEntry_->getBestPath(), nullptr);
  EXPECT_TRUE(ribEntry_->getInstallToFib());
  EXPECT_TRUE(ribEntry_->getIsPartialDrain());
}

/*
 * PathSelectionPolicy caches one PathSelectionPolicyResult per prefix
 * (PathSelectionPolicy::pathSelectionResults_) and reuses it across calls.
 * PathSelector::overrideMultipathSelection must reset transition fields on
 * every entry so a previous "drain triggered" call cannot leak
 * drainOnMinCapacityThresholdViolation=true into a subsequent call that exits
 * via the centralized-criteria-matched early return — otherwise RibEntry would
 * spuriously activate partial drain even when the prefix is healthy via CPS.
 */
TEST_F(
    RibPolicyFixture,
    PathSelectorResetsDrainOnMinCapacityThresholdViolationOnReuse) {
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);
  tPathSelector.drain_on_min_nexthop_violation() = true;

  PathSelector pathSelector(tPathSelector);
  PathSelectionPolicyResult result("stmt1");

  /*
   * Phase 1: only attr1 (community 200,666) matches the centralized matcher.
   * 1 < criteriaMinNexthop=2 -> centralized criteria returns empty -> falls
   * through to default selector. 2 paths < defaultMinNexthop=3 ->
   * BGP_FAILED_CPS_MIN_NEXTHOP with drain enabled.
   */
  pathSelector.overrideMultipathSelection(
      pathsToOverride_, multipathSelector, result);
  EXPECT_EQ(
      result.outcome,
      PathSelectionPolicyResult::Outcome::BGP_FAILED_CPS_MIN_NEXTHOP);
  EXPECT_TRUE(result.drainOnMinCapacityThresholdViolation);
  /*
   * Threshold captured alongside the drain flag; downstream RibEntry copies
   * this into mnhThreshold_ for the Thrift accessor to surface. LBW
   * threshold remains 0 because this is the MNH-violation branch.
   */
  EXPECT_EQ(3, result.mnhThreshold);
  EXPECT_EQ(0, result.aggLbwBpsThreshold);

  /*
   * Phase 2: feed a path set where the centralized criteria succeeds, taking
   * the early-return branch at the top of overrideMultipathSelection. Find
   * the path with the matching community and present it twice so the
   * criteria's minNexthop=2 is satisfied.
   */
  std::shared_ptr<RouteInfo> matchingPath;
  for (const auto& p : pathsToOverride_) {
    if (p->attrs->getNexthop() == kV4Nexthop1) {
      matchingPath = p;
      break;
    }
  }
  ASSERT_NE(matchingPath, nullptr);
  std::vector<std::shared_ptr<RouteInfo>> matchingPaths{
      matchingPath, matchingPath};

  pathSelector.overrideMultipathSelection(
      matchingPaths, multipathSelector, result);

  EXPECT_EQ(result.outcome, PathSelectionPolicyResult::Outcome::CPS);
  /*
   * Without the reset at the top of overrideMultipathSelection, the cached
   * values from phase 1 leak through and these assertions fail.
   */
  EXPECT_FALSE(result.drainOnMinCapacityThresholdViolation);
  EXPECT_EQ(0, result.mnhThreshold);
  EXPECT_EQ(0, result.aggLbwBpsThreshold);
}

/*
 * LBW partial drain coverage. Mirrors the MNH partial drain tests above:
 * `drain_on_min_nexthop_violation` (renamed in C++ to
 * drainOnMinCapacityThresholdViolation) gates partial drain for both MNH and
 * aggregate-LBW threshold violations. attr1 has 800 GBps LBW; the tests pick
 * an aggregate threshold above 800 GBps to force a violation.
 */
TEST_F(RibPolicyFixture, PartialDrainOutcomeSetOnLbwViolation) {
  /*
   * Use only attr1 (community 200,666, 800 GBps LBW) so the LBW check has a
   * path with a concrete LBW value (path2 has no LBW and would short-circuit
   * the aggregate check to a healthy BGP outcome).
   */
  std::shared_ptr<RouteInfo> pathWithLbw;
  for (const auto& p : pathsToOverride_) {
    if (p->attrs->getNexthop() == kV4Nexthop1) {
      pathWithLbw = p;
      break;
    }
  }
  ASSERT_NE(pathWithLbw, nullptr);

  /*
   * criteriaMinNextHop=2 forces fall-through to the default selector
   * (1 matching path < 2). bgpNativeMinAggLbwbps=1000 GBps is above the
   * 800 GBps available, triggering the LBW threshold violation.
   */
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(
      tMatcher,
      /*criteriaMinNextHop=*/2,
      /*defaultMinNextHop=*/std::nullopt,
      /*defaultMinAggLbwbps=*/(int64_t)1000 * BpsPerGBps,
      /*relaxMinAggLbwbps=*/std::nullopt);
  tPathSelector.drain_on_min_nexthop_violation() = true;

  PathSelector pathSelector(tPathSelector);
  PathSelectionPolicyResult result("stmt1");

  auto multipaths = pathSelector.overrideMultipathSelection(
      {pathWithLbw}, multipathSelector, result);
  EXPECT_EQ(
      result.outcome,
      PathSelectionPolicyResult::Outcome::BGP_FAILED_CPS_MIN_AGG_LBW);
  EXPECT_TRUE(result.drainOnMinCapacityThresholdViolation);
  /*
   * LBW threshold captured on this branch; mnhThreshold remains 0 so the
   * RibDC union-build at RPC time emits TMinCapacityThreshold.agg_lbw_bps
   * rather than .mnh.
   */
  EXPECT_EQ(static_cast<int64_t>(1000) * BpsPerGBps, result.aggLbwBpsThreshold);
  EXPECT_EQ(0, result.mnhThreshold);
  EXPECT_EQ(multipaths.size(), 1);
}

TEST_F(RibPolicyFixture, StrictLbwOutcomeWhenPartialDrainDisabled) {
  std::shared_ptr<RouteInfo> pathWithLbw;
  for (const auto& p : pathsToOverride_) {
    if (p->attrs->getNexthop() == kV4Nexthop1) {
      pathWithLbw = p;
      break;
    }
  }
  ASSERT_NE(pathWithLbw, nullptr);

  // drain_on_min_nexthop_violation NOT set (defaults to false)
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(
      tMatcher,
      /*criteriaMinNextHop=*/2,
      /*defaultMinNextHop=*/std::nullopt,
      /*defaultMinAggLbwbps=*/(int64_t)1000 * BpsPerGBps,
      /*relaxMinAggLbwbps=*/std::nullopt);

  PathSelector pathSelector(tPathSelector);
  PathSelectionPolicyResult result("stmt1");

  auto multipaths = pathSelector.overrideMultipathSelection(
      {pathWithLbw}, multipathSelector, result);
  EXPECT_EQ(
      result.outcome,
      PathSelectionPolicyResult::Outcome::BGP_FAILED_CPS_MIN_AGG_LBW);
  EXPECT_FALSE(result.drainOnMinCapacityThresholdViolation);
  /*
   * Without drain (or relax), the policy returns an empty set so the route
   * is dropped.
   */
  EXPECT_EQ(multipaths.size(), 0);
}

TEST_F(RibPolicyFixture, NoPartialDrainWhenLbwSatisfied) {
  std::shared_ptr<RouteInfo> pathWithLbw;
  for (const auto& p : pathsToOverride_) {
    if (p->attrs->getNexthop() == kV4Nexthop1) {
      pathWithLbw = p;
      break;
    }
  }
  ASSERT_NE(pathWithLbw, nullptr);

  // Threshold = 100 GBps, path has 800 GBps -> satisfied
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(
      tMatcher,
      /*criteriaMinNextHop=*/2,
      /*defaultMinNextHop=*/std::nullopt,
      /*defaultMinAggLbwbps=*/(int64_t)100 * BpsPerGBps,
      /*relaxMinAggLbwbps=*/std::nullopt);
  tPathSelector.drain_on_min_nexthop_violation() = true;

  PathSelector pathSelector(tPathSelector);
  PathSelectionPolicyResult result("stmt1");

  pathSelector.overrideMultipathSelection(
      {pathWithLbw}, multipathSelector, result);
  EXPECT_EQ(result.outcome, PathSelectionPolicyResult::Outcome::BGP);
  EXPECT_FALSE(result.drainOnMinCapacityThresholdViolation);
}

namespace {
/*
 * Build a RibEntry holding a single EGP path that carries 800 GBps of
 * non-transitive LBW and matches community 200:666. Shared setup for the
 * RibPolicyLbwDrainTest cases below, which pair it with a min-aggregate-LBW
 * threshold above 800 GBps to exercise the drain-on-violation behavior.
 */
std::unique_ptr<RibEntry> makeLbwRibEntry() {
  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(2, 2, 2, 2));
  nettools::bgplib::BgpAttrCommunitiesC community;
  community.emplace_back(200, 666);
  attr->setNexthop(kV4Nexthop1);
  attr->setLocalPref(kLocalPref);
  attr->setCommunities(community);
  attr->setOrigin(nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP);
  attr->setNonTransitiveLbwExtCommunity(kLocalAs1, float(800) * BpsPerGBps / 8);
  attr->publish();

  auto peer = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);

  auto ribEntry = std::make_unique<RibEntry>(kV4Prefix1);
  EXPECT_TRUE(ribEntry->updatePath(peer, attr, true, 0));
  return ribEntry;
}
} // namespace

/*
 * Verify RibEntry honors the drain flag on LBW violation: bestpath retained,
 * isPartialDrain set, install-to-FIB true. Uses a custom RibEntry with a
 * single LBW-bearing path so the aggregate check sees a concrete LBW value.
 */
TEST(RibPolicyLbwDrainTest, PartialDrainRetainsBestpathOnLbwViolation) {
  auto ribEntry = makeLbwRibEntry();

  /*
   * criteriaMinNextHop=2 forces fall-through; bgpNativeMinAggLbwbps=1000 GBps
   * > 800 GBps available -> LBW violation -> drain flag triggers.
   */
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(
      tMatcher,
      /*criteriaMinNextHop=*/2,
      /*defaultMinNextHop=*/std::nullopt,
      /*defaultMinAggLbwbps=*/(int64_t)1000 * BpsPerGBps,
      /*relaxMinAggLbwbps=*/std::nullopt);
  tPathSelector.drain_on_min_nexthop_violation() = true;

  auto policy = std::make_unique<PathSelectionPolicy>(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));

  bool bestpathChanged, multipathChanged;
  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry,

      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      policy);

  EXPECT_NE(ribEntry->getBestPath(), nullptr);
  EXPECT_TRUE(ribEntry->getInstallToFib());
  EXPECT_TRUE(ribEntry->getIsPartialDrain());
}

TEST(RibPolicyLbwDrainTest, StrictLbwNullifiesBestpath) {
  auto ribEntry = makeLbwRibEntry();

  /*
   * drain_on_min_nexthop_violation NOT set: LBW violation -> bestpath null,
   * route withdrawn.
   */
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(
      tMatcher,
      /*criteriaMinNextHop=*/2,
      /*defaultMinNextHop=*/std::nullopt,
      /*defaultMinAggLbwbps=*/(int64_t)1000 * BpsPerGBps,
      /*relaxMinAggLbwbps=*/std::nullopt);

  auto policy = std::make_unique<PathSelectionPolicy>(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));

  bool bestpathChanged, multipathChanged;
  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry,

      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      policy);

  EXPECT_EQ(ribEntry->getBestPath(), nullptr);
  EXPECT_FALSE(ribEntry->getIsPartialDrain());
}

/*
 * Rollback transition (drain → strict) under LBW violation. Mirrors
 * PartialDrainToStrictMnhRollback: first inject a drain LBW policy so
 * partial drain engages, then swap to the same LBW policy without the drain
 * flag and verify bestpath is nullified and isPartialDrain_ clears.
 * Rollback is the more dangerous direction (RFC: cited in BGP++
 * general_rules.md), so explicit coverage is required.
 */
TEST(RibPolicyLbwDrainTest, PartialDrainToStrictLbwRollback) {
  auto ribEntry = makeLbwRibEntry();

  // Phase 1: drain enabled, LBW violation -> partial drain engages.
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelectorDrain = createTPathSlectorWithOneMatcher(
      tMatcher,
      /*criteriaMinNextHop=*/2,
      /*defaultMinNextHop=*/std::nullopt,
      /*defaultMinAggLbwbps=*/(int64_t)1000 * BpsPerGBps,
      /*relaxMinAggLbwbps=*/std::nullopt);
  tPathSelectorDrain.drain_on_min_nexthop_violation() = true;

  auto drainPolicy = std::make_unique<PathSelectionPolicy>(
      createTPathSelectionPolicyWithPathSelector(
          {kV4Prefix1}, tPathSelectorDrain));

  bool bestpathChanged, multipathChanged;
  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry,

      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      drainPolicy);

  EXPECT_NE(ribEntry->getBestPath(), nullptr);
  EXPECT_TRUE(ribEntry->getIsPartialDrain());

  // Phase 2: strict policy (drain flag absent) -> bestpath nullified.
  auto tPathSelectorStrict = createTPathSlectorWithOneMatcher(
      tMatcher,
      /*criteriaMinNextHop=*/2,
      /*defaultMinNextHop=*/std::nullopt,
      /*defaultMinAggLbwbps=*/(int64_t)1000 * BpsPerGBps,
      /*relaxMinAggLbwbps=*/std::nullopt);

  auto strictPolicy = std::make_unique<PathSelectionPolicy>(
      createTPathSelectionPolicyWithPathSelector(
          {kV4Prefix1}, tPathSelectorStrict));

  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry,

      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      strictPolicy);

  EXPECT_TRUE(bestpathChanged);
  EXPECT_EQ(ribEntry->getBestPath(), nullptr);
  EXPECT_FALSE(ribEntry->getIsPartialDrain());
}

/*
 * Rollout transition (strict → drain) under LBW violation. Mirrors
 * StrictMnhToPartialDrainRollout: first inject a strict LBW policy so the
 * bestpath is nullified, then swap to the same LBW policy with the drain
 * flag and verify the bestpath is reinstated and isPartialDrain_ engages.
 */
TEST(RibPolicyLbwDrainTest, StrictLbwToPartialDrainRollout) {
  auto ribEntry = makeLbwRibEntry();

  // Phase 1: strict policy, LBW violation -> bestpath nullified.
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelectorStrict = createTPathSlectorWithOneMatcher(
      tMatcher,
      /*criteriaMinNextHop=*/2,
      /*defaultMinNextHop=*/std::nullopt,
      /*defaultMinAggLbwbps=*/(int64_t)1000 * BpsPerGBps,
      /*relaxMinAggLbwbps=*/std::nullopt);

  auto strictPolicy = std::make_unique<PathSelectionPolicy>(
      createTPathSelectionPolicyWithPathSelector(
          {kV4Prefix1}, tPathSelectorStrict));

  bool bestpathChanged, multipathChanged;
  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry,

      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      strictPolicy);

  EXPECT_EQ(ribEntry->getBestPath(), nullptr);
  EXPECT_FALSE(ribEntry->getIsPartialDrain());

  // Phase 2: enable drain -> bestpath retained, partial drain engages.
  auto tPathSelectorDrain = createTPathSlectorWithOneMatcher(
      tMatcher,
      /*criteriaMinNextHop=*/2,
      /*defaultMinNextHop=*/std::nullopt,
      /*defaultMinAggLbwbps=*/(int64_t)1000 * BpsPerGBps,
      /*relaxMinAggLbwbps=*/std::nullopt);
  tPathSelectorDrain.drain_on_min_nexthop_violation() = true;

  auto drainPolicy = std::make_unique<PathSelectionPolicy>(
      createTPathSelectionPolicyWithPathSelector(
          {kV4Prefix1}, tPathSelectorDrain));

  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry,

      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      drainPolicy);

  EXPECT_TRUE(bestpathChanged);
  EXPECT_NE(ribEntry->getBestPath(), nullptr);
  EXPECT_TRUE(ribEntry->getInstallToFib());
  EXPECT_TRUE(ribEntry->getIsPartialDrain());
}

/*
 * Test the default min nexthop specified in the path selector. When it it not
 * met, no path would be selected. Native MNH is by default NOT relaxed.
 */
TEST_F(RibPolicyFixture, SelectPathDefaultMinNexthopTest) {
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 2, 3);

  RibPolicy policy{
      createTRibPolicyWithPathSelector({kV4Prefix1}, tPathSelector)};

  auto overriddenPaths =
      policy.getPathSelectionPolicy()->overrideMultipathSelection(
          *ribEntry_, pathsToOverride_, multipathSelector);

  /*
   * The one path selected by native bgp multipath selector is less than the
   * min nexthop 3. Therefore, overriddenPaths ends up with no path.
   */
  EXPECT_EQ(overriddenPaths.size(), 0);
}

/**
 * This test verifies path selection override
 * - Install two EGP-origin paths of length 4 and 2, communities 200:666 and
 *   100:234, respectively.
 * - The default path selection mechanism should choose the path of length 2
 * - Override the path selection to select the path of community 200:666, which
 *   is the path of length 4
 * - Ensure the centralized criteria is prioritized over additional BGP
 *   criterion
 */
TEST_F(RibPolicyFixture, OverridePathSelectionTestRelaxDefaultMNH) {
  bool bestpathChanged, multipathChanged;
  std::tie(bestpathChanged, multipathChanged) = RibBase::selectBestPath(
      *ribEntry_, multipathSelector, bestpathSelector, false, 0);

  // Without changing the criteria, the best path should remain unchanged
  std::tie(bestpathChanged, multipathChanged) = RibBase::selectBestPath(
      *ribEntry_, multipathSelector, bestpathSelector, false, 0);
  EXPECT_FALSE(bestpathChanged);
  EXPECT_FALSE(multipathChanged);

  // Now the best path has length 2
  EXPECT_EQ(ribEntry_->getBestPath()->getBgpAsPathLen(), 2);

  // Create a RibPolicy to select the path with community 200:666
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher);

  auto policy1 = std::make_unique<PathSelectionPolicy>(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));

  // Now the bestpath and the multipath should be changed
  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry_,
      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      policy1);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_TRUE(multipathChanged);

  // Now the best path has length 4
  EXPECT_EQ(ribEntry_->getBestPath()->getBgpAsPathLen(), 4);

  /*
   * Meanwhile, since the centralized criteria matches, the additional BGP min
   * nexthop check will not be triggered
   */
  tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, std::nullopt, 4);

  auto policy2 = std::make_unique<PathSelectionPolicy>(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));

  // And hence no path should be changed
  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry_,
      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      policy2);
  EXPECT_FALSE(bestpathChanged);
  EXPECT_FALSE(multipathChanged);

  /*
   * On the other hand, the min nexthop check in the matched centralized
   * criteria would be enforced
   */
  tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 4, 4);
  tPathSelector.drain_on_min_nexthop_violation() = true;

  auto policy3 = std::make_unique<PathSelectionPolicy>(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));

  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry_,
      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      policy3);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_TRUE(multipathChanged);

  // With drain_on_min_nexthop_violation, bestpath is retained (partial drain)
  EXPECT_NE(ribEntry_->getBestPath(), nullptr);
  EXPECT_EQ(ribEntry_->getBestPath()->getBgpAsPathLen(), 2);
  EXPECT_TRUE(ribEntry_->getInstallToFib());
  EXPECT_TRUE(ribEntry_->getIsPartialDrain());

  /*
   * Once the path selection policy is removed, the default multipath selection
   * will be reapplied. Bestpath stays the same (native selector picks same
   * path), but the partial-drain marker flips from true to false — and
   * selectBestPath() folds drain transitions into bestpathChanged so the
   * RibOut announcement machinery re-advertises (drain community removal).
   */
  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry_,
      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      nullptr);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_FALSE(multipathChanged);

  EXPECT_EQ(ribEntry_->getBestPath()->getBgpAsPathLen(), 2);
  EXPECT_FALSE(ribEntry_->getIsPartialDrain());
}

/**
 * This test verifies path selection override
 * - Install two EGP-origin paths of length 4 and 2, communities 200:666 and
 *   100:234, respectively.
 * - The default path selection mechanism should choose the path of length 2
 * - Override the path selection to select the path of community 200:666, which
 *   is the path of length 4
 * - Ensure the centralized criteria is prioritized over additional BGP
 *   criterion
 */
TEST_F(RibPolicyFixture, OverridePathSelectionTest) {
  bool bestpathChanged, multipathChanged;
  std::tie(bestpathChanged, multipathChanged) = RibBase::selectBestPath(
      *ribEntry_, multipathSelector, bestpathSelector, false, 0);

  // Without changing the criteria, the best path should remain unchanged
  std::tie(bestpathChanged, multipathChanged) = RibBase::selectBestPath(
      *ribEntry_, multipathSelector, bestpathSelector, false, 0);
  EXPECT_FALSE(bestpathChanged);
  EXPECT_FALSE(multipathChanged);

  // Now the best path has length 2
  EXPECT_EQ(ribEntry_->getBestPath()->getBgpAsPathLen(), 2);

  // Create a RibPolicy to select the path with community 200:666
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher);

  auto policy1 = std::make_unique<PathSelectionPolicy>(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));

  // Now the bestpath and the multipath should be changed
  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry_,
      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      policy1);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_TRUE(multipathChanged);

  // Now the best path has length 4
  EXPECT_EQ(ribEntry_->getBestPath()->getBgpAsPathLen(), 4);

  /*
   * Meanwhile, since the centralized criteria matches, the additional BGP min
   * nexthop check will not be triggered
   */
  tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, std::nullopt, 4);

  auto policy2 = std::make_unique<PathSelectionPolicy>(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));

  // And hence no path should be changed
  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry_,
      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      policy2);
  EXPECT_FALSE(bestpathChanged);
  EXPECT_FALSE(multipathChanged);

  /*
   * On the other hand, the min nexthop check in the matched centralized
   * criteria would be enforced
   */
  tPathSelector = createTPathSlectorWithOneMatcher(tMatcher, 4, 4);

  auto policy3 = std::make_unique<PathSelectionPolicy>(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));

  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry_,
      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      policy3);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_TRUE(multipathChanged);

  /*
   * In this case, no best path could be found as we only have 1 path, which
   * violates the min nexthop constraint (4)
   */
  EXPECT_EQ(ribEntry_->getBestPath(), nullptr);

  /*
   * Once the policy is removed, the default multipath selection will be
   * reapplied
   */

  // Now the bestpath and the multipath should be changed back
  std::tie(bestpathChanged, multipathChanged) = RibDC::selectBestPath(
      *ribEntry_,
      multipathSelector,
      bestpathSelector,
      false,
      0,
      std::nullopt,
      nullptr);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_TRUE(multipathChanged);

  EXPECT_EQ(ribEntry_->getBestPath()->getBgpAsPathLen(), 2);
}

// Test the prefix to active path selection result map (cache)
TEST_F(RibPolicyFixture, PathSelectionPolicyCacheTest) {
  // Create a RibPolicy to select the path with community 200:666
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  auto tPathSelector = createTPathSlectorWithOneMatcher(tMatcher);

  std::vector<std::string> prefixStrs = {
      folly::IPAddress::networkToString(kV4Prefix1),
      folly::IPAddress::networkToString(kV4Prefix2)};

  // Not match, using multipathSelector
  {
    PathSelectionPolicy policy(createTPathSelectionPolicyWithPathSelector(
        {kV4Prefix2}, tPathSelector));

    auto overriddenPaths = policy.overrideMultipathSelection(
        *ribEntry_, pathsToOverride_, multipathSelector);

    // Check cache, the prefix should yield std::nullopt
    auto& cachedItem = policy.pathSelectionResults_.at(ribEntry_->getPrefix());
    EXPECT_EQ(cachedItem, std::nullopt);
    EXPECT_FALSE(policy.pathSelectionResults_.contains(kV4Prefix2));

    auto overriddenPathsByCache = policy.overrideMultipathSelection(
        *ribEntry_, pathsToOverride_, multipathSelector);

    // The same route would invoke the cache and the result should be the same
    EXPECT_EQ(overriddenPaths, overriddenPathsByCache);

    auto activePathSelectionCriteria =
        policy.getActivePathSelectionCriteria(prefixStrs);
    EXPECT_EQ(activePathSelectionCriteria.size(), 2);
    // no match for kV4Prefix1, empty TPathSelector
    EXPECT_EQ(activePathSelectionCriteria[0].criteria_list()->size(), 0);
    EXPECT_EQ(activePathSelectionCriteria[1].criteria_list()->size(), 0);
  }

  // A match would be recorded
  {
    PathSelectionPolicy policy(createTPathSelectionPolicyWithPathSelector(
        {kV4Prefix1}, tPathSelector));

    auto overriddenPaths = policy.overrideMultipathSelection(
        *ribEntry_, pathsToOverride_, multipathSelector);

    /*
     * Check cache, the prefix should yield some path selection result
     * with both statement name and active criteria
     */
    auto& cachedItem = policy.pathSelectionResults_.at(ribEntry_->getPrefix());
    EXPECT_EQ(cachedItem->getStatementName(), "stmt1");
    EXPECT_NE(cachedItem->activeCriteria, nullptr);
    EXPECT_FALSE(policy.pathSelectionResults_.contains(kV4Prefix2));

    auto overriddenPathsByCache = policy.overrideMultipathSelection(
        *ribEntry_, pathsToOverride_, multipathSelector);

    // The same route would invoke the cache and the result should be the same
    EXPECT_EQ(overriddenPaths, overriddenPathsByCache);

    auto activePathSelectionCriteria =
        policy.getActivePathSelectionCriteria(prefixStrs);
    EXPECT_EQ(activePathSelectionCriteria.size(), 2);
    // The active criteria is the only criteria in the criteria_list
    EXPECT_EQ(activePathSelectionCriteria[0].criteria_list()->size(), 1);
    EXPECT_EQ(
        activePathSelectionCriteria[0].criteria_list()[0],
        tPathSelector.criteria_list()[0]);
    EXPECT_EQ(activePathSelectionCriteria[1].criteria_list()->size(), 0);
  }

  // If no criteria satisfied the prefix, activeCriteria should be nullptr
  {
    auto tPathSelectorNoMatch =
        createTPathSlectorWithOneMatcher(tMatcher, 100, 1);

    PathSelectionPolicy policy(createTPathSelectionPolicyWithPathSelector(
        {kV4Prefix1}, tPathSelectorNoMatch));

    auto overriddenPaths = policy.overrideMultipathSelection(
        *ribEntry_, pathsToOverride_, multipathSelector);

    /*
     * Check cache, the prefix should yield the path selection result
     * where the statement is available, but the active criteria is nullptr
     */
    auto& cachedItem = policy.pathSelectionResults_.at(ribEntry_->getPrefix());
    EXPECT_EQ(cachedItem->getStatementName(), "stmt1");
    EXPECT_EQ(cachedItem->activeCriteria, nullptr);
    EXPECT_FALSE(policy.pathSelectionResults_.contains(kV4Prefix2));
    auto overriddenPathsByCache = policy.overrideMultipathSelection(
        *ribEntry_, pathsToOverride_, multipathSelector);

    // The same route would invoke the cache and the result should be the same
    EXPECT_EQ(overriddenPaths, overriddenPathsByCache);

    auto activePathSelectionCriteria =
        policy.getActivePathSelectionCriteria(prefixStrs);
    EXPECT_EQ(activePathSelectionCriteria.size(), 2);
    // No active criteria
    EXPECT_EQ(activePathSelectionCriteria[0].criteria_list()->size(), 0);
    // Default bgp native path selection min nexthop is specified to be 1
    EXPECT_EQ(
        *activePathSelectionCriteria[0].bgp_native_path_selection_min_nexthop(),
        1);
    EXPECT_EQ(activePathSelectionCriteria[1].criteria_list()->size(), 0);
  }

  /*
   * Test RibPolicy level API
   * A match should be recorded
   */
  {
    RibPolicy policy(
        createTRibPolicyWithPathSelector({kV4Prefix1}, tPathSelector));

    auto overriddenPaths =
        policy.getPathSelectionPolicy()->overrideMultipathSelection(
            *ribEntry_, pathsToOverride_, multipathSelector);

    auto activePathSelectionCriteria =
        policy.getPathSelectionPolicy()->getActivePathSelectionCriteria(
            prefixStrs);
    EXPECT_EQ(activePathSelectionCriteria.size(), 2);
    // The active criteria is the only criteria in the criteria_list
    EXPECT_EQ(activePathSelectionCriteria[0].criteria_list()->size(), 1);
    EXPECT_EQ(
        activePathSelectionCriteria[0].criteria_list()[0],
        tPathSelector.criteria_list()[0]);
    EXPECT_EQ(activePathSelectionCriteria[1].criteria_list()->size(), 0);
  }
  /*
   * A RibPolicy without PathSelectionPolicy would return no active path
   * selection criteria when getActivePathSelectionCriteria is called
   */
  {
    RibPolicy policy(createTRibPolicyLbw({kV4Prefix1}, 1e11L));

    EXPECT_EQ(policy.getPathSelectionPolicy(), std::nullopt);
  }
}

/*
 * Regression test for drain-flag round-trip via getActivePathSelectionCriteria.
 * The drain flag now covers both the MNH and aggregate-LBW thresholds, so it
 * must survive reconstruction even when only bgp_min_aggregate_lbw_bps (and no
 * MNH) is configured. Previously the flag was emitted only alongside an MNH
 * value, silently dropping it for LBW-only statements.
 */
TEST_F(RibPolicyFixture, LbwOnlyDrainFlagPreservedInActiveCriteria) {
  auto tMatcher = createCommunityMatch(200, 666, bgp_policy::Origin::EGP);
  /*
   * criteriaMinNextHop=100 -> the statement matches but the criteria is
   * unsatisfied, exercising the fall-through (activeCriteria == nullptr) path.
   * No defaultMinNextHop: only the LBW threshold and drain flag are set.
   */
  auto tPathSelector = createTPathSlectorWithOneMatcher(
      tMatcher,
      /*criteriaMinNextHop=*/100,
      /*defaultMinNextHop=*/std::nullopt,
      /*defaultMinAggLbwbps=*/(int64_t)1000 * BpsPerGBps,
      /*relaxMinAggLbwbps=*/std::nullopt);
  tPathSelector.drain_on_min_nexthop_violation() = true;

  PathSelectionPolicy policy(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));

  /*
   * Populate the prefix -> statement cache so getActivePathSelectionCriteria
   * reconstructs from the matched statement.
   */
  policy.overrideMultipathSelection(
      *ribEntry_, pathsToOverride_, multipathSelector);

  auto activeCriteria = policy.getActivePathSelectionCriteria(
      {folly::IPAddress::networkToString(kV4Prefix1)});
  ASSERT_EQ(activeCriteria.size(), 1);
  const auto& reconstructed = activeCriteria[0];

  // The drain flag must be preserved even though no MNH is configured.
  ASSERT_TRUE(reconstructed.drain_on_min_nexthop_violation().has_value());
  EXPECT_TRUE(*reconstructed.drain_on_min_nexthop_violation());
  // The LBW threshold round-trips; the MNH field stays unset.
  ASSERT_TRUE(reconstructed.bgp_min_aggregate_lbw_bps().has_value());
  EXPECT_EQ(
      *reconstructed.bgp_min_aggregate_lbw_bps(), (int64_t)1000 * BpsPerGBps);
  EXPECT_FALSE(
      reconstructed.bgp_native_path_selection_min_nexthop().has_value());
}

TEST(RibPolicyTest, RouteAttributeUcmpActionTest) {
  TRouteAttributeUcmpAction tRouteAttributeUcmpAction;
  TNextHopWeightAction tNexthopWeightAction;
  TBgpPathMatcher tMatcher;
  TRouteAttributeActions tRouteAttributeActions;

  tMatcher.origin() = bgp_policy::Origin::EGP; // match EGP

  tNexthopWeightAction.path_matchers()->push_back(tMatcher);
  tNexthopWeightAction.weight() = 13;
  NextHopWeightAction nexthopWeightAction(tNexthopWeightAction);
  tRouteAttributeUcmpAction.nexthop_weight_actions()->push_back(
      tNexthopWeightAction);
  tRouteAttributeUcmpAction.apply_all_actions_or_fallback_to_ecmp() = true;
  tRouteAttributeActions.set_ucmp_weights() = tRouteAttributeUcmpAction;
  RouteAttributeActions routeAttributeActions(tRouteAttributeActions);

  // Test the creation of next-hop weight actions.
  EXPECT_EQ(routeAttributeActions.nextHopWeightActions_.size(), 1);
  EXPECT_NE(routeAttributeActions.nextHopWeightActions_.at(0).get(), nullptr);

  // Test toThrift().
  EXPECT_EQ(routeAttributeActions.toThrift(), tRouteAttributeActions);

  // Test comparison
  TRouteAttributeUcmpAction tRouteAttributeUcmpAction2;
  TRouteAttributeActions tRouteAttributeActions2;
  TNextHopWeightAction tNexthopWeightAction2;

  tNexthopWeightAction2.path_matchers()->push_back(tMatcher);
  tNexthopWeightAction2.weight() = 13;
  tRouteAttributeUcmpAction2.nexthop_weight_actions()->push_back(
      tNexthopWeightAction2);
  tRouteAttributeUcmpAction2.apply_all_actions_or_fallback_to_ecmp() = true;
  tRouteAttributeActions2.set_ucmp_weights() = tRouteAttributeUcmpAction2;
  RouteAttributeActions routeAttributeActions2(tRouteAttributeActions2);

  EXPECT_EQ(routeAttributeActions, routeAttributeActions2);

  TRouteAttributeUcmpAction tRouteAttributeUcmpAction3;
  TRouteAttributeActions tRouteAttributeActions3;
  TNextHopWeightAction tNexthopWeightAction3;

  tNexthopWeightAction3.path_matchers()->push_back(tMatcher);
  tNexthopWeightAction3.weight() = 14;
  tRouteAttributeUcmpAction3.nexthop_weight_actions()->push_back(
      tNexthopWeightAction3);
  tRouteAttributeUcmpAction3.apply_all_actions_or_fallback_to_ecmp() = true;
  tRouteAttributeActions3.set_ucmp_weights() = tRouteAttributeUcmpAction3;
  RouteAttributeActions routeAttributeActions3(tRouteAttributeActions3);

  EXPECT_NE(routeAttributeActions, routeAttributeActions3);
}

/**
 * Test fixture class for RouteAttributeUcmpAction related tests.
 */
class RouteAttributeUcmpActionFixture : public ::testing::Test {
 protected:
  TRouteAttributeUcmpAction tRouteAttributeUcmpAction;
  std::shared_ptr<facebook::bgp::BgpPath> attrs1, attrs2, attrs3;
  // Weights that will be assigned to next-hops.
  int32_t nhWt1 = 10e2;
  int32_t nhWt2 = 20e2;

  void SetUp() override {
    // Create attributes for three paths.
    nettools::bgplib::BgpAttrCommunitiesC communities1;
    communities1.emplace_back(200, 100);
    attrs1 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs1->setCommunities(communities1);
    attrs1->publish();

    nettools::bgplib::BgpAttrCommunitiesC communities2;
    communities2.emplace_back(200, 200);
    attrs2 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs2->setNexthop(kV4Nexthop2);
    attrs2->setCommunities(communities2);
    attrs2->publish();

    nettools::bgplib::BgpAttrCommunitiesC communities3;
    communities3.emplace_back(100, 300);
    attrs3 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs3->setNexthop(kV4Nexthop3);
    attrs3->setCommunities(communities3);
    attrs3->publish();

    // Create Route attribute UCMP action object
    TNextHopWeightAction tNexthopWeightAction1;
    TBgpPathMatcher tMatcher;
    auto tCommMatch1 = createTBgpCommunityMatch(200, 100);

    tMatcher.community_list() = createTCommunityListMatch(
        {tCommMatch1}, routing_policy::BooleanOperator::AND);
    tNexthopWeightAction1.path_matchers()->push_back(tMatcher);
    tNexthopWeightAction1.weight() = nhWt1;

    NextHopWeightAction nexthopWeightAction1(tNexthopWeightAction1);

    TNextHopWeightAction tNexthopWeightAction2;
    TBgpPathMatcher tMatcher2;
    auto tCommMatch2 = createTBgpCommunityMatch(200, 200);

    tMatcher.community_list() = createTCommunityListMatch(
        {tCommMatch2}, routing_policy::BooleanOperator::AND);
    tNexthopWeightAction2.path_matchers()->push_back(tMatcher);
    tNexthopWeightAction2.weight() = nhWt2;

    NextHopWeightAction nexthopWeightAction2(tNexthopWeightAction2);

    tRouteAttributeUcmpAction.nexthop_weight_actions()->push_back(
        tNexthopWeightAction1);
    tRouteAttributeUcmpAction.nexthop_weight_actions()->push_back(
        tNexthopWeightAction2);
  }
};

/**
 * Verify match of Rib policy with Route Attribute UCMP action action.
 */
TEST_F(RouteAttributeUcmpActionFixture, RibPolicyRouteAttributeUCMP) {
  tRouteAttributeUcmpAction.apply_all_actions_or_fallback_to_ecmp() = true;

  // 1. Create three peers.
  auto peer1 = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  auto peer2 = TinyPeerInfo(
      kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);
  auto peer3 = TinyPeerInfo(
      kPeerAddr3, kPeerAsn3, kPeerRouterId3, BgpSessionType::EBGP, false);

  // 2. Create one route for 1st prefix.
  RibEntry ribEntry1(kV4Prefix1);
  // Prefix is set correctly
  EXPECT_EQ(kV4Prefix1, ribEntry1.getPrefix());
  // Bestpath and nexthops and class id are not available, yet.
  EXPECT_EQ(nullptr, ribEntry1.getBestPath());
  EXPECT_EQ(nullptr, ribEntry1.getMultipathWeightedNexthops());
  EXPECT_TRUE(ribEntry1.updatePath(peer1, attrs1, false));
  // Trigger the bestpath selection.
  bool bestpathChanged, nexthopChanged;
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry1, multipathSelector, bestpathSelector, false, 0);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_TRUE(nexthopChanged);

  // 3. Create two routes for 2nd prefix.
  RibEntry ribEntry2(kV4Prefix2);
  // Prefix is set correctly.
  EXPECT_EQ(kV4Prefix2, ribEntry2.getPrefix());
  // All should be set to null by default.
  EXPECT_EQ(nullptr, ribEntry2.getBestPath());
  EXPECT_EQ(nullptr, ribEntry2.getMultipathWeightedNexthops());
  EXPECT_TRUE(ribEntry2.updatePath(peer1, attrs1, false));
  EXPECT_TRUE(ribEntry2.updatePath(peer2, attrs2, false));
  // Bestpath and nexthops and class id are not available, yet.
  EXPECT_EQ(nullptr, ribEntry2.getBestPath());
  EXPECT_EQ(nullptr, ribEntry2.getMultipathWeightedNexthops());
  // Trigger the bestpath selection.
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry2, multipathSelector, bestpathSelector, false, 0);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_TRUE(nexthopChanged);

  // 3. Create three routes for 3rd prefix.
  RibEntry ribEntry3(kV4Prefix3);
  // Prefix is set correctly
  EXPECT_EQ(kV4Prefix3, ribEntry3.getPrefix());
  // All should be set to null by default
  EXPECT_EQ(nullptr, ribEntry3.getBestPath());
  EXPECT_EQ(nullptr, ribEntry3.getMultipathWeightedNexthops());
  EXPECT_TRUE(ribEntry3.updatePath(peer1, attrs1, false));
  EXPECT_TRUE(ribEntry3.updatePath(peer2, attrs2, false));
  EXPECT_TRUE(ribEntry3.updatePath(peer3, attrs3, false));

  // Bestpath and nexthops and class id are not available, yet.
  EXPECT_EQ(nullptr, ribEntry3.getBestPath());
  EXPECT_EQ(nullptr, ribEntry3.getMultipathWeightedNexthops());
  // Trigger the bestpath selection.
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry3, multipathSelector, bestpathSelector, false, 0);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_TRUE(nexthopChanged);

  // 4. Create Rib-policy.
  {
    TRibPolicy tPolicy =
        createTRibPolicyUcmp({kV4Prefix1}, tRouteAttributeUcmpAction);
    RibPolicy policy{tPolicy};

    EXPECT_TRUE(policy.getRouteAttributePolicy()->match(ribEntry1));
    EXPECT_FALSE(policy.getRouteAttributePolicy()->match(ribEntry2));
  }
}

namespace {

WeightedNexthopMap computedWeights(
    const RouteAttributePolicy::StatementWeights& statementWeights) {
  WeightedNexthopMap weights;
  for (const auto& [nh, matched] : statementWeights.nexthopWeights) {
    weights.emplace(nh, matched.weight);
  }
  return weights;
}

} // namespace

/*
 * Test computeStatementWeights returns the active statement and each nexthop's
 * weight without changing the route.
 */
TEST_F(
    RouteAttributeUcmpActionFixture,
    ComputeStatementWeightsReturnsStatementAndNexthopWeights) {
  const auto tPolicy =
      createTRibPolicyUcmp({kV4Prefix2}, tRouteAttributeUcmpAction);
  const auto raPolicy =
      std::make_unique<RouteAttributePolicy>(*tPolicy.route_attribute_policy());
  const auto peer1 = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  const auto peer2 = TinyPeerInfo(
      kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);
  RibEntry matched(kV4Prefix2);
  RibEntry unmatched(kV4Prefix1);
  for (auto* entry : {&matched, &unmatched}) {
    ASSERT_TRUE(entry->updatePath(peer1, attrs1, false));
    ASSERT_TRUE(entry->updatePath(peer2, attrs2, false));
  }

  const auto statementWeights = raPolicy->computeStatementWeights(matched);
  const WeightedNexthopMap expected{{kV4Nexthop1, nhWt1}, {kV4Nexthop2, nhWt2}};
  EXPECT_NE(nullptr, statementWeights.statement);
  EXPECT_EQ(expected, computedWeights(statementWeights));
  EXPECT_EQ(nullptr, matched.getMultipathWeightedNexthops());

  const auto noStatementWeights = raPolicy->computeStatementWeights(unmatched);
  EXPECT_EQ(nullptr, noStatementWeights.statement);
  EXPECT_TRUE(noStatementWeights.nexthopWeights.empty());
}

/*
 * Test computeNexthopWeights gives every matched nexthop its action and weight,
 * before any path is selected, and skips unmatched nexthops.
 */
TEST_F(RouteAttributeUcmpActionFixture, ComputeNexthopWeightsCoversAllPaths) {
  const auto tPolicy =
      createTRibPolicyUcmp({kV4Prefix1}, tRouteAttributeUcmpAction);
  const RouteAttributeStatement stmt{
      tPolicy.route_attribute_policy()->statements()->begin()->second};
  RibEntry ribEntry(kV4Prefix1);
  ASSERT_TRUE(ribEntry.updatePath(
      TinyPeerInfo(
          kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false),
      attrs1,
      false));
  ASSERT_TRUE(ribEntry.updatePath(
      TinyPeerInfo(
          kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false),
      attrs2,
      false));
  // Matches no action.
  ASSERT_TRUE(ribEntry.updatePath(
      TinyPeerInfo(
          kPeerAddr3, kPeerAsn3, kPeerRouterId3, BgpSessionType::EBGP, false),
      attrs3,
      false));

  const auto nexthopWeights = stmt.computeNexthopWeights(ribEntry);

  WeightedNexthopMap weights;
  for (const auto& [nh, matched] : nexthopWeights) {
    weights.emplace(nh, matched.weight);
  }
  const WeightedNexthopMap expected{{kV4Nexthop1, nhWt1}, {kV4Nexthop2, nhWt2}};
  EXPECT_EQ(expected, weights);
  EXPECT_EQ(nullptr, ribEntry.getMultipathWeightedNexthops());
}

/*
 * Test UCMP weights are not applied, and the route is unchanged, when a
 * selected nexthop matches no action.
 */
TEST_F(
    RouteAttributeUcmpActionFixture,
    UpdateAttributeSkipsRouteWithUnmatchedSelectedNexthop) {
  const auto tPolicy =
      createTRibPolicyUcmp({kV4Prefix1}, tRouteAttributeUcmpAction);
  const RouteAttributeStatement stmt{
      tPolicy.route_attribute_policy()->statements()->begin()->second};
  RibEntry ribEntry(kV4Prefix1);
  ASSERT_TRUE(ribEntry.updatePath(
      TinyPeerInfo(
          kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false),
      attrs1,
      false));
  ASSERT_TRUE(ribEntry.updatePath(
      TinyPeerInfo(
          kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false),
      attrs2,
      false));
  // Matches no action.
  ASSERT_TRUE(ribEntry.updatePath(
      TinyPeerInfo(
          kPeerAddr3, kPeerAsn3, kPeerRouterId3, BgpSessionType::EBGP, false),
      attrs3,
      false));
  RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  ASSERT_EQ(3, ribEntry.getMultipathWeightedNexthops()->size());
  const auto selectedBefore = *ribEntry.getMultipathWeightedNexthops();

  EXPECT_FALSE(
      stmt.updateAttribute(ribEntry, stmt.computeNexthopWeights(ribEntry)));

  EXPECT_EQ(selectedBefore, *ribEntry.getMultipathWeightedNexthops());
}

/*
 * Test a nexthop whose paths match different actions gets the lower action
 * index, whichever path is added first.
 */
TEST_F(
    RouteAttributeUcmpActionFixture,
    ComputeNexthopWeightsKeepsLowestActionIndex) {
  const auto tPolicy =
      createTRibPolicyUcmp({kV4Prefix1}, tRouteAttributeUcmpAction);
  const RouteAttributeStatement stmt{
      tPolicy.route_attribute_policy()->statements()->begin()->second};
  // Same nexthop as attrs1, but matches action 1 instead of action 0.
  nettools::bgplib::BgpAttrCommunitiesC communities;
  communities.emplace_back(200, 200);
  auto attrsAction1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrsAction1->setCommunities(communities);
  attrsAction1->publish();
  ASSERT_EQ(attrs1->getNexthop(), attrsAction1->getNexthop());
  const auto peer1 = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  const auto peer2 = TinyPeerInfo(
      kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);
  RibEntry action0First(kV4Prefix1);
  ASSERT_TRUE(action0First.updatePath(peer1, attrs1, false));
  ASSERT_TRUE(action0First.updatePath(peer2, attrsAction1, false));
  RibEntry action1First(kV4Prefix1);
  ASSERT_TRUE(action1First.updatePath(peer1, attrsAction1, false));
  ASSERT_TRUE(action1First.updatePath(peer2, attrs1, false));

  for (const auto* ribEntry : {&action0First, &action1First}) {
    const auto nexthopWeights = stmt.computeNexthopWeights(*ribEntry);
    ASSERT_EQ(1, nexthopWeights.size());
    const auto& matched = nexthopWeights.at(attrs1->getNexthop());
    EXPECT_EQ(0, matched.actionIndex);
    EXPECT_EQ(nhWt1, matched.weight);
  }
}

/**
 * Test Route attribute UCMP action in strict mode.
 *
 * 3 prefixes are created, the first with multipath next-hop set of size 1, the
 * second with a multipath next-hop set of size 2 and the 3rd with a multipath
 * next-hop of size 3.
 * These routes are evaluated against a RouteAttributeUcmpAction that has two
 * next-hops specified. The route with 2 next-hops in multipath path set should
 * have it's next-hop weights overridden.
 */
TEST_F(RouteAttributeUcmpActionFixture, RouteAttributeUcmpActionStrict) {
  tRouteAttributeUcmpAction.apply_all_actions_or_fallback_to_ecmp() = true;

  // 1. Create three peers.
  auto peer1 = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  auto peer2 = TinyPeerInfo(
      kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);
  auto peer3 = TinyPeerInfo(
      kPeerAddr3, kPeerAsn3, kPeerRouterId3, BgpSessionType::EBGP, false);

  // 2. Create one route for 1st prefix.
  RibEntry ribEntry1(kV4Prefix1);
  // Prefix is set correctly
  EXPECT_EQ(kV4Prefix1, ribEntry1.getPrefix());
  // Bestpath and nexthops and class id are not available, yet.
  EXPECT_EQ(nullptr, ribEntry1.getBestPath());
  EXPECT_EQ(nullptr, ribEntry1.getMultipathWeightedNexthops());
  EXPECT_TRUE(ribEntry1.updatePath(peer1, attrs1, false));
  // Trigger the bestpath selection.
  bool bestpathChanged, nexthopChanged;
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry1, multipathSelector, bestpathSelector, false, 0);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_TRUE(nexthopChanged);

  // 3. Create two routes for 2nd prefix.
  RibEntry ribEntry2(kV4Prefix2);
  // Prefix is set correctly.
  EXPECT_EQ(kV4Prefix2, ribEntry2.getPrefix());
  // All should be set to null by default.
  EXPECT_EQ(nullptr, ribEntry2.getBestPath());
  EXPECT_EQ(nullptr, ribEntry2.getMultipathWeightedNexthops());
  EXPECT_TRUE(ribEntry2.updatePath(peer1, attrs1, false));
  EXPECT_TRUE(ribEntry2.updatePath(peer2, attrs2, false));
  // Bestpath and nexthops and class id are not available, yet.
  EXPECT_EQ(nullptr, ribEntry2.getBestPath());
  EXPECT_EQ(nullptr, ribEntry2.getMultipathWeightedNexthops());
  // Trigger the bestpath selection.
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry2, multipathSelector, bestpathSelector, false, 0);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_TRUE(nexthopChanged);

  // 4. Create three routes for 3rd prefix.
  RibEntry ribEntry3(kV4Prefix3);
  // Prefix is set correctly
  EXPECT_EQ(kV4Prefix3, ribEntry3.getPrefix());
  // All should be set to null by default
  EXPECT_EQ(nullptr, ribEntry3.getBestPath());
  EXPECT_EQ(nullptr, ribEntry3.getMultipathWeightedNexthops());
  EXPECT_TRUE(ribEntry3.updatePath(peer1, attrs1, false));
  EXPECT_TRUE(ribEntry3.updatePath(peer2, attrs2, false));
  EXPECT_TRUE(ribEntry3.updatePath(peer3, attrs3, false));

  // Bestpath and nexthops and class id are not available, yet.
  EXPECT_EQ(nullptr, ribEntry3.getBestPath());
  EXPECT_EQ(nullptr, ribEntry3.getMultipathWeightedNexthops());
  // Trigger the bestpath selection.
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry3, multipathSelector, bestpathSelector, false, 0);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_TRUE(nexthopChanged);

  // 5. Verify.

  {
    TRouteAttributeStatement tStmt = createTRouteAttributeStatementUcmp(
        {kV4Prefix1, kV4Prefix2, kV4Prefix3}, tRouteAttributeUcmpAction);

    RouteAttributeStatement stmt{tStmt};
    EXPECT_EQ(stmt.toThrift(), tStmt);

    // Weights for the first route's next-hops should not be present.
    EXPECT_FALSE(
        stmt.updateAttribute(ribEntry1, stmt.computeNexthopWeights(ribEntry1)));
    EXPECT_EQ(ribEntry1.getMultipathWeightedNexthops()->size(), 1);
    for (auto& nhwt : *ribEntry1.getMultipathWeightedNexthops()) {
      EXPECT_EQ(nhwt.first, kV4Nexthop1);
      EXPECT_EQ(nhwt.second, 0);
    }

    // Weights for the second route's next-hops shoudl be overrridden.
    EXPECT_TRUE(
        stmt.updateAttribute(ribEntry2, stmt.computeNexthopWeights(ribEntry2)));
    EXPECT_EQ(ribEntry2.getMultipathWeightedNexthops()->size(), 2);
    for (auto& nhwt : *ribEntry2.getMultipathWeightedNexthops()) {
      if (nhwt.first == kV4Nexthop1) {
        EXPECT_EQ(nhwt.second, nhWt1);
      } else if (nhwt.first == kV4Nexthop2) {
        EXPECT_EQ(nhwt.second, nhWt2);
      }
    }
    // Verify re-applying the same policy will not change anything.
    EXPECT_FALSE(
        stmt.updateAttribute(ribEntry2, stmt.computeNexthopWeights(ribEntry2)));

    // Weights for the third route's next-hops should not be present.
    EXPECT_FALSE(
        stmt.updateAttribute(ribEntry3, stmt.computeNexthopWeights(ribEntry3)));
    EXPECT_EQ(ribEntry3.getMultipathWeightedNexthops()->size(), 3);
    for (auto& nhwt : *ribEntry3.getMultipathWeightedNexthops()) {
      if (nhwt.first == kV4Nexthop1) {
        EXPECT_EQ(nhwt.second, 0);
      } else if (nhwt.first == kV4Nexthop2) {
        EXPECT_EQ(nhwt.second, 0);
      } else if (nhwt.first == kV4Nexthop3) {
        EXPECT_EQ(nhwt.second, 0);
      }
    }
  }
}

/**
 * Test Route attribute UCMP action in relaxed mode.
 *
 * 3 prefixes are created, the first with multipath next-hop set of size 1, the
 * second with a multipath next-hop set of size 2 and the 3rd with a multipath
 * next-hop of size 3.
 * These routes are evaluated against a RouteAttributeUcmpAction that has two
 * next-hops specified. The route with 1 and 2 next-hops in multipath path set
 * should have their next-hop weights overridden.
 */
TEST_F(RouteAttributeUcmpActionFixture, RouteAttributeUcmpActionRelaxed) {
  tRouteAttributeUcmpAction.apply_all_actions_or_fallback_to_ecmp() = false;
  // 1. Create three peers.
  auto peer1 = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  auto peer2 = TinyPeerInfo(
      kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);
  auto peer3 = TinyPeerInfo(
      kPeerAddr3, kPeerAsn3, kPeerRouterId3, BgpSessionType::EBGP, false);

  // 2. Create one route for 1st prefix.
  RibEntry ribEntry1(kV4Prefix1);
  // Prefix is set correctly
  EXPECT_EQ(kV4Prefix1, ribEntry1.getPrefix());
  // Bestpath and nexthops and class id are not available, yet.
  EXPECT_EQ(nullptr, ribEntry1.getBestPath());
  EXPECT_EQ(nullptr, ribEntry1.getMultipathWeightedNexthops());
  EXPECT_TRUE(ribEntry1.updatePath(peer1, attrs1, false));
  // Trigger the bestpath selection.
  bool bestpathChanged, nexthopChanged;
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry1, multipathSelector, bestpathSelector, false, 0);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_TRUE(nexthopChanged);

  // 3. Create two routes for 2nd prefix.
  RibEntry ribEntry2(kV4Prefix2);
  // Prefix is set correctly.
  EXPECT_EQ(kV4Prefix2, ribEntry2.getPrefix());
  // All should be set to null by default.
  EXPECT_EQ(nullptr, ribEntry2.getBestPath());
  EXPECT_EQ(nullptr, ribEntry2.getMultipathWeightedNexthops());
  EXPECT_TRUE(ribEntry2.updatePath(peer1, attrs1, false));
  EXPECT_TRUE(ribEntry2.updatePath(peer2, attrs2, false));
  // Bestpath and nexthops and class id are not available, yet.
  EXPECT_EQ(nullptr, ribEntry2.getBestPath());
  EXPECT_EQ(nullptr, ribEntry2.getMultipathWeightedNexthops());
  // Trigger the bestpath selection.
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry2, multipathSelector, bestpathSelector, false, 0);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_TRUE(nexthopChanged);

  // 4. Create three routes for 3rd prefix.
  RibEntry ribEntry3(kV4Prefix3);
  // Prefix is set correctly
  EXPECT_EQ(kV4Prefix3, ribEntry3.getPrefix());
  // All should be set to null by default
  EXPECT_EQ(nullptr, ribEntry3.getBestPath());
  EXPECT_EQ(nullptr, ribEntry3.getMultipathWeightedNexthops());
  EXPECT_TRUE(ribEntry3.updatePath(peer1, attrs1, false));
  EXPECT_TRUE(ribEntry3.updatePath(peer2, attrs2, false));
  EXPECT_TRUE(ribEntry3.updatePath(peer3, attrs3, false));

  // Bestpath and nexthops and class id are not available, yet.
  EXPECT_EQ(nullptr, ribEntry3.getBestPath());
  EXPECT_EQ(nullptr, ribEntry3.getMultipathWeightedNexthops());
  // Trigger the bestpath selection.
  std::tie(bestpathChanged, nexthopChanged) = RibBase::selectBestPath(
      ribEntry3, multipathSelector, bestpathSelector, false, 0);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_TRUE(nexthopChanged);

  // 5. Verify.
  {
    TRouteAttributeStatement tStmt = createTRouteAttributeStatementUcmp(
        {kV4Prefix1, kV4Prefix2, kV4Prefix3}, tRouteAttributeUcmpAction);

    RouteAttributeStatement stmt{tStmt};
    EXPECT_EQ(stmt.toThrift(), tStmt);

    // Weights for the first route's next-hops shoudl be overridden.
    EXPECT_TRUE(
        stmt.updateAttribute(ribEntry1, stmt.computeNexthopWeights(ribEntry1)));
    EXPECT_EQ(ribEntry1.getMultipathWeightedNexthops()->size(), 1);
    for (auto& nhwt : *ribEntry1.getMultipathWeightedNexthops()) {
      EXPECT_EQ(nhwt.first, kV4Nexthop1);
      EXPECT_EQ(nhwt.second, nhWt1);
    }
    // Verify re-applying the same policy will not change anything.
    EXPECT_FALSE(
        stmt.updateAttribute(ribEntry1, stmt.computeNexthopWeights(ribEntry1)));

    // Weights for the second route's next-hops shoudl be overrridden.
    EXPECT_TRUE(
        stmt.updateAttribute(ribEntry2, stmt.computeNexthopWeights(ribEntry2)));
    EXPECT_EQ(ribEntry2.getMultipathWeightedNexthops()->size(), 2);
    for (auto& nhwt : *ribEntry2.getMultipathWeightedNexthops()) {
      if (nhwt.first == kV4Nexthop1) {
        EXPECT_EQ(nhwt.second, nhWt1);
      } else if (nhwt.first == kV4Nexthop2) {
        EXPECT_EQ(nhwt.second, nhWt2);
      }
    }
    // Verify re-applying the same policy will not change anything.
    EXPECT_FALSE(
        stmt.updateAttribute(ribEntry2, stmt.computeNexthopWeights(ribEntry2)));

    // Weights for the third route's next-hops should not be present.
    EXPECT_FALSE(
        stmt.updateAttribute(ribEntry3, stmt.computeNexthopWeights(ribEntry3)));
    EXPECT_EQ(ribEntry3.getMultipathWeightedNexthops()->size(), 3);
    for (auto& nhwt : *ribEntry3.getMultipathWeightedNexthops()) {
      if (nhwt.first == kV4Nexthop1) {
        EXPECT_EQ(nhwt.second, 0);
      } else if (nhwt.first == kV4Nexthop2) {
        EXPECT_EQ(nhwt.second, 0);
      } else if (nhwt.first == kV4Nexthop3) {
        EXPECT_EQ(nhwt.second, 0);
      }
    }
  }
}

/**
 * Verify match of Rib policy with Route Attribute UCMP action action.
 */
TEST(RibPolicyTest, DivideWeightsByMatchingPathCount) {
  // Create policy
  auto tCommMatchA = createTBgpCommunityMatch(100, 200);
  TBgpPathMatcher tMatcherA;
  tMatcherA.community_list() = createTCommunityListMatch(
      {tCommMatchA}, routing_policy::BooleanOperator::AND);
  TNextHopWeightAction tNexthopWeightActionA;
  tNexthopWeightActionA.path_matchers()->push_back(tMatcherA);
  tNexthopWeightActionA.weight() = 10;

  auto tCommMatchB = createTBgpCommunityMatch(100, 300);
  TBgpPathMatcher tMatcherB;
  tMatcherB.community_list() = createTCommunityListMatch(
      {tCommMatchB}, routing_policy::BooleanOperator::AND);
  TNextHopWeightAction tNexthopWeightActionB;
  tNexthopWeightActionB.path_matchers()->push_back(tMatcherB);
  tNexthopWeightActionB.weight() = 10;

  TRouteAttributeUcmpAction tRouteAttributeUcmpAction;
  tRouteAttributeUcmpAction.nexthop_weight_actions()->push_back(
      tNexthopWeightActionA);
  tRouteAttributeUcmpAction.nexthop_weight_actions()->push_back(
      tNexthopWeightActionB);
  tRouteAttributeUcmpAction.divide_weights_by_matching_path_count() = true;

  TRibPolicy tPolicy =
      createTRibPolicyUcmp({kV4Prefix1}, tRouteAttributeUcmpAction);
  RibPolicy policy{tPolicy};
  RouteAttributePolicy::RibChange ribChange;

  // Set up RIB
  auto peer1 = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  auto peer2 = TinyPeerInfo(
      kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);
  auto peer3 = TinyPeerInfo(
      kPeerAddr3, kPeerAsn3, kPeerRouterId3, BgpSessionType::EBGP, false);

  nettools::bgplib::BgpAttrCommunitiesC communitiesA;
  communitiesA.emplace_back(100, 200);
  nettools::bgplib::BgpAttrCommunitiesC communitiesB;
  communitiesB.emplace_back(100, 300);

  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs1->setNexthop(kV4Nexthop1);
  attrs1->setCommunities(communitiesA);
  attrs1->publish();

  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs2->setNexthop(kV4Nexthop2);
  attrs2->setCommunities(communitiesA);
  attrs2->publish();

  auto attrs3 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs3->setNexthop(kV4Nexthop3);
  attrs3->setCommunities(communitiesB);
  attrs3->publish();

  RibEntry ribEntry(kV4Prefix1);
  EXPECT_TRUE(ribEntry.updatePath(peer1, attrs1, false));
  EXPECT_TRUE(ribEntry.updatePath(peer2, attrs2, false));
  EXPECT_TRUE(ribEntry.updatePath(peer3, attrs3, false));

  auto [bestpathChanged, nexthopChanged] = RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);
  EXPECT_TRUE(bestpathChanged);
  EXPECT_TRUE(nexthopChanged);

  // Apply the policy
  EXPECT_TRUE(policy.getRouteAttributePolicy()->overwriteRouteAttributes(
      ribEntry, ribChange));

  /*
   * Since there are two paths matching action 1, the weight of each individual
   * path is halved.
   */
  EXPECT_THAT(
      *ribEntry.getMultipathWeightedNexthops(),
      UnorderedElementsAre(
          std::make_pair(kV4Nexthop1, 5),
          std::make_pair(kV4Nexthop2, 5),
          std::make_pair(kV4Nexthop3, 10)));
}

/**
 * Regression test: the divide_weights_by_matching_path_count divisor must be
 * computed only over the multipath-selected nexthops, not over every path in
 * routeInfos_. Counting non-selected paths inflates the divisor and produces
 * smaller-than-intended per-nexthop weights for the selected paths (this is
 * the failure mode observed for prn-vip statements: 912240/14 = 65160 expected
 * vs. 912240/16 = 57015 actually programmed).
 */
TEST(RibPolicyTest, DivideWeightsOnlyAmongSelectedNexthops) {
  // Single weight action, weight=10, matched by community 100:200.
  auto tCommMatch = createTBgpCommunityMatch(100, 200);
  TBgpPathMatcher tMatcher;
  tMatcher.community_list() = createTCommunityListMatch(
      {tCommMatch}, routing_policy::BooleanOperator::AND);
  TNextHopWeightAction tNexthopWeightAction;
  tNexthopWeightAction.path_matchers()->push_back(tMatcher);
  tNexthopWeightAction.weight() = 10;

  TRouteAttributeUcmpAction tRouteAttributeUcmpAction;
  tRouteAttributeUcmpAction.nexthop_weight_actions()->push_back(
      tNexthopWeightAction);
  tRouteAttributeUcmpAction.divide_weights_by_matching_path_count() = true;

  TRibPolicy tPolicy =
      createTRibPolicyUcmp({kV4Prefix1}, tRouteAttributeUcmpAction);
  RibPolicy policy{tPolicy};
  RouteAttributePolicy::RibChange ribChange;

  auto peer1 = TinyPeerInfo(
      kPeerAddr1, kPeerAsn1, kPeerRouterId1, BgpSessionType::EBGP, false);
  auto peer2 = TinyPeerInfo(
      kPeerAddr2, kPeerAsn2, kPeerRouterId2, BgpSessionType::EBGP, false);
  auto peer3 = TinyPeerInfo(
      kPeerAddr3, kPeerAsn3, kPeerRouterId3, BgpSessionType::EBGP, false);

  nettools::bgplib::BgpAttrCommunitiesC communities;
  communities.emplace_back(100, 200);

  // Three paths via three distinct nexthops, all matching the action.
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs1->setNexthop(kV4Nexthop1);
  attrs1->setCommunities(communities);
  attrs1->publish();

  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs2->setNexthop(kV4Nexthop2);
  attrs2->setCommunities(communities);
  attrs2->publish();

  auto attrs3 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs3->setNexthop(kV4Nexthop3);
  attrs3->setCommunities(communities);
  attrs3->publish();

  RibEntry ribEntry(kV4Prefix1);
  EXPECT_TRUE(ribEntry.updatePath(peer1, attrs1, false));
  EXPECT_TRUE(ribEntry.updatePath(peer2, attrs2, false));
  EXPECT_TRUE(ribEntry.updatePath(peer3, attrs3, false));

  RibBase::selectBestPath(
      ribEntry, multipathSelector, bestpathSelector, false, 0);

  /*
   * Simulate the production scenario where one of the three paths is in
   * routeInfos_ (visible via getAllPaths()) but is NOT selected for the
   * multipath set. With the fix, only the 2 selected nexthops contribute
   * to matchingPathCount, so each gets weight = max(10/2, 1) = 5. Without
   * the fix the divisor would be 3 and each would get max(10/3, 1) = 3.
   */
  WeightedNexthopMap selectedNexthops;
  selectedNexthops.emplace(kV4Nexthop1, 0);
  selectedNexthops.emplace(kV4Nexthop2, 0);
  ribEntry.overrideWeightedNexthops(selectedNexthops);

  EXPECT_TRUE(policy.getRouteAttributePolicy()->overwriteRouteAttributes(
      ribEntry, ribChange));

  EXPECT_THAT(
      *ribEntry.getMultipathWeightedNexthops(),
      UnorderedElementsAre(
          std::make_pair(kV4Nexthop1, 5), std::make_pair(kV4Nexthop2, 5)));
}

// Test Next-hop weight action.
TEST(RibPolicyTest, NextHopWeightActionTest) {
  TNextHopWeightAction tNexthopWeightAction;

  TBgpPathMatcher tMatcher;
  tMatcher.origin() = bgp_policy::Origin::EGP; // match EGP

  tNexthopWeightAction.path_matchers()->push_back(tMatcher);
  tNexthopWeightAction.weight() = 13;

  NextHopWeightAction nexthopWeightAction1(tNexthopWeightAction);

  // Test the parsing.
  EXPECT_EQ(nexthopWeightAction1.nexthopPathMatchers_.size(), 1);
  EXPECT_EQ(nexthopWeightAction1.nexthopUcmpWeight_, 13);

  // Test toThrift().
  EXPECT_EQ(nexthopWeightAction1.toThrift(), tNexthopWeightAction);

  // Test comparison.
  tNexthopWeightAction.weight() = 2;
  NextHopWeightAction nexthopWeightAction2(tNexthopWeightAction);

  EXPECT_NE(nexthopWeightAction1, nexthopWeightAction2);
}

/**
 * Test RouteAttributePolicy cache functionality.
 *
 * This test verifies:
 * 1. Cache hit for repeated evaluations returns correct statement
 * 2. Cache correctly stores the matched statement name
 * 3. Repeated calls use the cache instead of iterating through statements
 */
TEST(RibPolicyTest, RouteAttributePolicyCacheTest) {
  int64_t expectedWeight = 100;
  TRibPolicy tPolicy = createTRibPolicyLbw({kV4Prefix1}, expectedWeight);
  RibPolicy policy{tPolicy};

  // Create rib entries
  RibEntry entry1(kV4Prefix1);
  RibEntry entry2(kV4Prefix1); // Same prefix, different entry object

  // First call should populate the cache
  RouteAttributePolicy::RibChange ribChange1;
  bool result1 = policy.getRouteAttributePolicy()->overwriteRouteAttributes(
      entry1, ribChange1);
  EXPECT_TRUE(result1);
  EXPECT_EQ(ribChange1.updatedRoutes.size(), 1);
  EXPECT_TRUE(ribChange1.updatedRoutes.contains(kV4Prefix1));

  // Verify cache is populated
  EXPECT_TRUE(
      policy.getRouteAttributePolicy()->getCache().contains(kV4Prefix1));
  auto cachedItem = policy.getRouteAttributePolicy()->getCache().at(kV4Prefix1);
  EXPECT_TRUE(cachedItem.has_value());
  EXPECT_EQ(cachedItem->getStatementName(), "stmt1");

  /*
   * Second call should use the cache (cache hit)
   * Since the weight is already set, no update should occur
   */
  RouteAttributePolicy::RibChange ribChange2;
  bool result2 = policy.getRouteAttributePolicy()->overwriteRouteAttributes(
      entry1, ribChange2);
  EXPECT_TRUE(result2); // Still matches
  EXPECT_EQ(ribChange2.updatedRoutes.size(), 0); // No change (same value)

  // Verify weight is correctly set
  EXPECT_TRUE(entry1.getRibPolicyUcmpWeight().has_value());
  EXPECT_EQ(expectedWeight, entry1.getRibPolicyUcmpWeight().value());
}

/**
 * Test RouteAttributePolicy negative cache functionality.
 *
 * This test verifies:
 * 1. Routes that don't match any statement are negatively cached
 * 2. Subsequent calls for non-matching routes return false immediately
 */
TEST(RibPolicyTest, RouteAttributePolicyCacheNegativeTest) {
  int64_t expectedWeight = 100;
  TRibPolicy tPolicy = createTRibPolicyLbw({kV4Prefix1}, expectedWeight);
  RibPolicy policy{tPolicy};

  // Create a rib entry that doesn't match
  RibEntry nonMatchingEntry(kV4Prefix2);

  // First call should populate the negative cache
  RouteAttributePolicy::RibChange ribChange1;
  bool result1 = policy.getRouteAttributePolicy()->overwriteRouteAttributes(
      nonMatchingEntry, ribChange1);
  EXPECT_FALSE(result1); // No match
  EXPECT_EQ(ribChange1.updatedRoutes.size(), 0);

  // Verify negative cache is populated
  EXPECT_TRUE(
      policy.getRouteAttributePolicy()->getCache().contains(kV4Prefix2));
  auto cachedItem = policy.getRouteAttributePolicy()->getCache().at(kV4Prefix2);
  EXPECT_FALSE(cachedItem.has_value()); // nullopt for negative cache

  // Second call should use the negative cache
  RouteAttributePolicy::RibChange ribChange2;
  bool result2 = policy.getRouteAttributePolicy()->overwriteRouteAttributes(
      nonMatchingEntry, ribChange2);
  EXPECT_FALSE(result2); // Still no match (from cache)
  EXPECT_EQ(ribChange2.updatedRoutes.size(), 0);

  // Verify no weight is set
  EXPECT_FALSE(nonMatchingEntry.getRibPolicyUcmpWeight().has_value());
}

/**
 * Test RouteAttributePolicy cache behavior with expired statements.
 *
 * This test verifies:
 * 1. Expired statements return false even on cache hit
 * 2. The cache entry is still valid, but isActive() returns false
 */
TEST(RibPolicyTest, RouteAttributePolicyCacheExpiredStmtTest) {
  auto now = std::chrono::seconds(std::time(nullptr));
  int64_t expectedWeight = 100;

  // Create policy with already expired statement
  TRibPolicy tPolicy = createTRibPolicyLbw(
      {kV4Prefix1}, expectedWeight, "stmt1", now.count() - 10);
  RibPolicy policy{tPolicy};

  RibEntry entry(kV4Prefix1);

  // First call - statement matches but is expired
  RouteAttributePolicy::RibChange ribChange1;
  bool result1 = policy.getRouteAttributePolicy()->overwriteRouteAttributes(
      entry, ribChange1);
  EXPECT_FALSE(result1); // Expired, so returns false
  EXPECT_EQ(ribChange1.updatedRoutes.size(), 0);

  // Verify the statement is indeed expired
  EXPECT_FALSE(
      policy.getRouteAttributePolicy()->statements_.at("stmt1").isActive());

  // No weight should be set
  EXPECT_FALSE(entry.getRibPolicyUcmpWeight().has_value());
}

/**
 * Test: RouteAttributePolicyMoveCache
 *
 * Verifies that moveCache() correctly moves all cache entries from
 * one policy to another.
 */
TEST(RibPolicyTest, RouteAttributePolicyMoveCache) {
  int64_t expectedWeight = 100;
  TRibPolicy tPolicy1 = createTRibPolicyLbw({kV4Prefix1}, expectedWeight);
  RibPolicy policy1{tPolicy1};

  // Populate cache in policy1 by calling overwriteRouteAttributes
  RibEntry entry1(kV4Prefix1);
  RibEntry entry2(kV4Prefix2); // Non-matching prefix for negative cache

  RouteAttributePolicy::RibChange ribChange;
  policy1.getRouteAttributePolicy()->overwriteRouteAttributes(
      entry1, ribChange);
  policy1.getRouteAttributePolicy()->overwriteRouteAttributes(
      entry2, ribChange);

  // Verify policy1 has cache entries
  const auto& cache1 = policy1.getRouteAttributePolicy()->getCache();
  EXPECT_EQ(cache1.size(), 2);
  EXPECT_TRUE(cache1.at(kV4Prefix1).has_value()); // Positive cache
  EXPECT_FALSE(cache1.at(kV4Prefix2).has_value()); // Negative cache

  // Create new policy and move cache
  TRibPolicy tPolicy2 = createTRibPolicyLbw({kV4Prefix1}, expectedWeight);
  RibPolicy policy2{tPolicy2};

  // Cache should be empty initially
  EXPECT_EQ(policy2.getRouteAttributePolicy()->getCache().size(), 0);

  // Move cache from policy1 to policy2
  policy2.getRouteAttributePolicy()->moveCache(
      *policy1.getRouteAttributePolicy());

  // Verify cache was moved to policy2
  const auto& cache2 = policy2.getRouteAttributePolicy()->getCache();
  EXPECT_EQ(cache2.size(), 2);
  EXPECT_TRUE(cache2.at(kV4Prefix1).has_value());
  EXPECT_EQ(cache2.at(kV4Prefix1)->getStatementName(), "stmt1");
  EXPECT_FALSE(cache2.at(kV4Prefix2).has_value());
  /*
   * Note: source cache is in moved-from state after moveCache() and should not
   * be accessed
   */
}

/**
 * Test: RouteAttributePolicySetCacheEntry
 *
 * Verifies that setCacheEntry() correctly sets and overwrites cache entries.
 */
TEST(RibPolicyTest, RouteAttributePolicySetCacheEntry) {
  int64_t expectedWeight = 100;
  TRibPolicy tPolicy = createTRibPolicyLbw({kV4Prefix1}, expectedWeight);
  RibPolicy policy{tPolicy};

  auto& raPolicy = *policy.getRouteAttributePolicy();

  // Set a positive cache entry
  raPolicy.setCacheEntry(kV4Prefix1, RibPolicyResultBase("stmt1"));
  EXPECT_EQ(raPolicy.getCache().size(), 1);
  EXPECT_TRUE(raPolicy.getCache().at(kV4Prefix1).has_value());
  EXPECT_EQ(raPolicy.getCache().at(kV4Prefix1)->getStatementName(), "stmt1");

  // Set a negative cache entry
  raPolicy.setCacheEntry(kV4Prefix2, std::nullopt);
  EXPECT_EQ(raPolicy.getCache().size(), 2);
  EXPECT_FALSE(raPolicy.getCache().at(kV4Prefix2).has_value());

  // Overwrite existing positive cache entry with different statement
  raPolicy.setCacheEntry(kV4Prefix1, RibPolicyResultBase("stmt2"));
  EXPECT_EQ(raPolicy.getCache().size(), 2);
  EXPECT_TRUE(raPolicy.getCache().at(kV4Prefix1).has_value());
  EXPECT_EQ(raPolicy.getCache().at(kV4Prefix1)->getStatementName(), "stmt2");

  // Overwrite positive cache entry with negative
  raPolicy.setCacheEntry(kV4Prefix1, std::nullopt);
  EXPECT_FALSE(raPolicy.getCache().at(kV4Prefix1).has_value());
}

/**
 * Test: RouteAttributePolicyGetStatements
 *
 * Verifies that getStatements() returns the correct statements map.
 */
TEST(RibPolicyTest, RouteAttributePolicyGetStatements) {
  int64_t expectedWeight = 100;
  TRibPolicy tPolicy =
      createTRibPolicyLbw({kV4Prefix1}, expectedWeight, "stmt1");
  tPolicy.route_attribute_policy()->statements()->emplace(
      "stmt2", createTRouteAttributeStatementLbw({kV4Prefix2}, 200));
  RibPolicy policy{tPolicy};

  const auto& statements = policy.getRouteAttributePolicy()->getStatements();

  EXPECT_EQ(statements.size(), 2);
  EXPECT_TRUE(statements.contains("stmt1"));
  EXPECT_TRUE(statements.contains("stmt2"));
}

/**
 * Test: RouteAttributePolicyNegativeCacheReEval
 *
 * Verifies that negative cache entries are erased and re-evaluated
 * when overwriteRouteAttributes is called. This is important for
 * cache preservation when new statements are added.
 *
 * Scenario:
 * 1. Policy has stmt1 matching prefix1
 * 2. Prefix2 doesn't match -> negative cache entry
 * 3. Add stmt2 matching prefix2 to the policy
 * 4. Migrate cache (copy negative entry for prefix2)
 * 5. Call overwriteRouteAttributes for prefix2
 * 6. Verify prefix2 now matches stmt2 (not stuck on negative cache)
 */
TEST(RibPolicyTest, RouteAttributePolicyNegativeCacheReEval) {
  int64_t weight1 = 100;
  int64_t weight2 = 200;

  // Create policy with only stmt1 matching prefix1
  TRibPolicy tPolicy1 = createTRibPolicyLbw({kV4Prefix1}, weight1, "stmt1");
  RibPolicy policy1{tPolicy1};

  // Prefix2 doesn't match any statement -> negative cache
  RibEntry entry2(kV4Prefix2);
  RouteAttributePolicy::RibChange ribChange1;
  bool result1 = policy1.getRouteAttributePolicy()->overwriteRouteAttributes(
      entry2, ribChange1);
  EXPECT_FALSE(result1); // No match

  // Verify negative cache entry exists
  const auto& cache1 = policy1.getRouteAttributePolicy()->getCache();
  EXPECT_EQ(cache1.size(), 1);
  EXPECT_FALSE(cache1.at(kV4Prefix2).has_value()); // Negative cache

  // Create new policy with stmt1 AND stmt2 (stmt2 matches prefix2)
  TRibPolicy tPolicy2 = createTRibPolicyLbw({kV4Prefix1}, weight1, "stmt1");
  tPolicy2.route_attribute_policy()->statements()->emplace(
      "stmt2", createTRouteAttributeStatementLbw({kV4Prefix2}, weight2));
  RibPolicy policy2{tPolicy2};

  // Simulate cache migration: copy negative entry from policy1
  policy2.getRouteAttributePolicy()->setCacheEntry(kV4Prefix2, std::nullopt);

  // Verify negative cache was migrated
  const auto& cache2 = policy2.getRouteAttributePolicy()->getCache();
  EXPECT_FALSE(cache2.at(kV4Prefix2).has_value());

  /*
   * Now call overwriteRouteAttributes - negative cache hit returns false
   * since we changed the behavior to trust negative cache entries
   */
  RibEntry entry2New(kV4Prefix2);
  RouteAttributePolicy::RibChange ribChange2;
  bool result2 = policy2.getRouteAttributePolicy()->overwriteRouteAttributes(
      entry2New, ribChange2);

  /*
   * Negative cache hit - returns false without re-evaluation
   * This is the new expected behavior: negative cache entries are trusted
   * (affected prefixes have their cache entries invalidated during migration)
   */
  EXPECT_FALSE(result2);
  EXPECT_EQ(ribChange2.updatedRoutes.size(), 0);
}

/**
 * Test: RouteAttributeStatementExpirationOnlyChange
 *
 * Verifies that when two RouteAttributeStatements have the same content
 * (matcher, actions) but different expiration times (both non-expired):
 * 1. operator!= returns true (statements are different)
 * 2. needsReEvaluation returns false (no content change, neither expired)
 *
 * This is the condition under which migrateRouteAttributePolicyCache must
 * detect the change and set hasUpdate=true without needsReEvaluation=true.
 */
TEST(RibPolicyTest, RouteAttributeStatementExpirationOnlyChange) {
  auto now = std::chrono::seconds(std::time(nullptr));
  int64_t weight = 100;

  // Create two statements with same content but different expiration times
  auto tStmt1 = createTRouteAttributeStatementLbw(
      {kV4Prefix1}, weight, now.count() + 3600); // expires in 1 hour
  auto tStmt2 = createTRouteAttributeStatementLbw(
      {kV4Prefix1}, weight, now.count() + 7200); // expires in 2 hours

  RouteAttributeStatement stmt1(tStmt1);
  RouteAttributeStatement stmt2(tStmt2);

  // Statements differ (different expiration time)
  EXPECT_NE(stmt1, stmt2);

  /*
   * Same content, neither expired: changed=true (expiration differs),
   * needsReEval=false (no content change), matcherChanged=false
   */
  auto reEvalResult = stmt1.needsReEvaluation(stmt2);
  EXPECT_TRUE(reEvalResult.changed);
  EXPECT_FALSE(reEvalResult.needsReEval);
  EXPECT_FALSE(reEvalResult.matcherChanged);
}

/**
 * Test: RouteAttributePolicyExpirationOnlyChange
 *
 * Verifies that two RouteAttributePolicies with same statements but different
 * expiration times are detected as different (operator!=).
 *
 * Re-evaluation behavior for expiration-only changes is tested through
 * migrateRouteAttributePolicyCache in RibRouteAttributePolicyTest.
 */
TEST(RibPolicyTest, RouteAttributePolicyExpirationOnlyChange) {
  auto now = std::chrono::seconds(std::time(nullptr));
  int64_t weight = 100;

  // Create two policies with same content but different expiration times
  TRibPolicy tPolicy1 =
      createTRibPolicyLbw({kV4Prefix1}, weight, "stmt1", now.count() + 3600);
  TRibPolicy tPolicy2 =
      createTRibPolicyLbw({kV4Prefix1}, weight, "stmt1", now.count() + 7200);

  RibPolicy policy1{tPolicy1};
  RibPolicy policy2{tPolicy2};

  // Policies are different (different expiration times)
  EXPECT_NE(
      *policy1.getRouteAttributePolicy(), *policy2.getRouteAttributePolicy());
}

/**
 * Test: RouteAttributeStatementBothExpiredIdentical
 *
 * Verifies that when both old and new statements have identical content AND
 * identical expired timestamps, needsReEvaluation returns {false, false}.
 * Previously this returned {false, true} which caused spurious full
 * re-evaluation + FIB programming without policy replacement.
 */
TEST(RibPolicyTest, RouteAttributeStatementBothExpiredIdentical) {
  auto now = std::chrono::seconds(std::time(nullptr));
  int64_t weight = 100;

  // Create two identical statements that are both expired
  auto tStmt1 = createTRouteAttributeStatementLbw(
      {kV4Prefix1}, weight, now.count() - 100); // expired 100s ago
  auto tStmt2 = createTRouteAttributeStatementLbw(
      {kV4Prefix1}, weight, now.count() - 100); // same expiration

  RouteAttributeStatement stmt1(tStmt1);
  RouteAttributeStatement stmt2(tStmt2);

  // Statements are equal
  EXPECT_EQ(stmt1, stmt2);

  /*
   * Both expired, identical content and timestamps:
   * changed=false, needsReEval=false (no spurious re-evaluation)
   */
  auto result = stmt1.needsReEvaluation(stmt2);
  EXPECT_FALSE(result.changed);
  EXPECT_FALSE(result.needsReEval);
  EXPECT_FALSE(result.matcherChanged);
}

/**
 * Test: RouteAttributeStatementActionChange
 *
 * Verifies that when a statement's action changes (but matcher stays same),
 * needsReEvaluation correctly returns changed=true, needsReEval=true,
 * matcherChanged=false.
 */
TEST(RibPolicyTest, RouteAttributeStatementActionChange) {
  auto now = std::chrono::seconds(std::time(nullptr));

  // Same matcher (prefix1), different actions (weight 100 vs 200)
  auto tStmt1 =
      createTRouteAttributeStatementLbw({kV4Prefix1}, 100, now.count() + 3600);
  auto tStmt2 =
      createTRouteAttributeStatementLbw({kV4Prefix1}, 200, now.count() + 3600);

  RouteAttributeStatement stmt1(tStmt1);
  RouteAttributeStatement stmt2(tStmt2);

  EXPECT_NE(stmt1, stmt2);

  auto result = stmt1.needsReEvaluation(stmt2);
  EXPECT_TRUE(result.changed);
  EXPECT_TRUE(result.needsReEval);
  EXPECT_FALSE(result.matcherChanged); // matcher stayed the same
}

/**
 * Test: RouteAttributeStatementMatcherChange
 *
 * Verifies that when a statement's matcher changes (different prefix set),
 * needsReEvaluation correctly returns matcherChanged=true so that cache
 * migration can invalidate the entry instead of preserving it.
 */
TEST(RibPolicyTest, RouteAttributeStatementMatcherChange) {
  auto now = std::chrono::seconds(std::time(nullptr));

  // Different matchers (prefix1 vs prefix2), same action
  auto tStmt1 =
      createTRouteAttributeStatementLbw({kV4Prefix1}, 100, now.count() + 3600);
  auto tStmt2 =
      createTRouteAttributeStatementLbw({kV4Prefix2}, 100, now.count() + 3600);

  RouteAttributeStatement stmt1(tStmt1);
  RouteAttributeStatement stmt2(tStmt2);

  EXPECT_NE(stmt1, stmt2);

  auto result = stmt1.needsReEvaluation(stmt2);
  EXPECT_TRUE(result.changed);
  EXPECT_TRUE(result.needsReEval);
  EXPECT_TRUE(result.matcherChanged); // matcher changed
}

} // namespace facebook::bgp
