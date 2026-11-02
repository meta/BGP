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

#define CommunityMatch_TEST_FRIENDS \
  FRIEND_TEST(PolicyTest, CommunityMatchStringSetOverlapTest);

#define ExtCommunityAction_TEST_FRIENDS \
  FRIEND_TEST(PolicyTest, ExtCommunityActionValidateActionTypeTest);

#define Policy_TEST_FRIENDS FRIEND_TEST(PolicyTest, populatePolicyTermsTest);

#include <folly/logging/xlog.h>

#include <fb303/ThreadCachedServiceData.h>

#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/bgp_policy_types.h"
#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/nsf_policy_types.h"
#include "neteng/fboss/bgp/cpp/common/BgpError.h"
#include "neteng/fboss/bgp/cpp/config/ConfigUtils.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyAttributesMask.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyManager.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyUtils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/PolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibPolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"

namespace facebook::bgp {

using namespace rib_policy;
using namespace nettools::bgplib;

using bgp_policy::BgpPolicyActionType;
using bgp_policy::BgpPolicyAtomicMatchType;
using folly::IPAddress;
using routing::classType;
using routing_policy::BooleanOperator;
using std::string;
using std::vector;

namespace {
const std::string& kMaterializedPolicyFileName =
    "neteng/fboss/bgp/cpp/tests/sample_configs/bgp_policy.materialized_JSON";
} // namespace

class PolicyTest : public ::testing::Test {};

TEST_F(PolicyTest, PolicyNameLookupIsCaseSensitive) {
  bgp_policy::BgpPolicies policies;
  bgp_policy::BgpPolicyStatement statement;
  statement.name() = "PROPAGATE_RSW_SLB_IN";
  policies.bgp_policy_statements()->push_back(statement);
  PolicyManager manager{policies};

  EXPECT_TRUE(manager.isPolicyPresent("PROPAGATE_RSW_SLB_IN"));
  EXPECT_NE(nullptr, manager.getPolicyFromName("PROPAGATE_RSW_SLB_IN"));
  for (const auto& name :
       {"propagate_rsw_slb_in",
        "rsw_slb_in",
        "RSW_SLB_IN",
        "PROPAGATE_RSW_SLB_IN_OTHER"}) {
    EXPECT_FALSE(manager.isPolicyPresent(name));
    EXPECT_EQ(nullptr, manager.getPolicyFromName(name));
  }
}

TEST_F(PolicyTest, BgpPolicyActionDataHashTest) {
  // LbwActionData
  LbwActionData data1{
      std::make_pair<uint16_t, float>(1, 100.f), 65530, 200.f, 300.f, 400.f};
  LbwActionData data1_same{
      std::make_pair<uint16_t, float>(1, 100.f), 65530, 200.f, 300.f, 400.f};
  LbwActionData data2{
      std::make_pair<uint16_t, float>(1, 100.f), 65530, 200.f, 300.f};
  LbwActionData data3{
      std::make_pair<uint16_t, float>(1, 100.f), 65530, 200.f, 300.f, 600.f};

  auto LbwActionDataHash = [](const LbwActionData& d) { return d.hash(); };
  std::unordered_set<LbwActionData, decltype(LbwActionDataHash)>
      lbwActionDatas{};
  lbwActionDatas.insert(data1);
  lbwActionDatas.insert(data2);
  EXPECT_EQ(2, lbwActionDatas.size());

  lbwActionDatas.insert(data1_same);
  EXPECT_EQ(2, lbwActionDatas.size());

  lbwActionDatas.insert(data3);
  EXPECT_EQ(3, lbwActionDatas.size());

  EXPECT_EQ(data1, data1_same);
  EXPECT_NE(data1, data2);

  // PolicyActionData
  BgpPolicyActionData policyDataNone{};
  BgpPolicyActionData policyData1{1, 1, data1};
  BgpPolicyActionData policyData1_same{1, 1, data1_same};
  BgpPolicyActionData policyData2{1, 1, data2};
  BgpPolicyActionData policyData3{1, 1, data3};
  BgpPolicyActionData policyData4{1, 2, data1};
  BgpPolicyActionData policyData5{2, 1, data1};

  auto BgpPolicyActionDataHash = [](const BgpPolicyActionData& d) {
    return d.hash();
  };

  std::unordered_set<BgpPolicyActionData, decltype(BgpPolicyActionDataHash)>
      policyDatas{};
  policyDatas.insert(policyData1);
  policyDatas.insert(policyData2);
  EXPECT_EQ(2, policyDatas.size());

  policyDatas.insert(policyData1_same);
  EXPECT_EQ(2, policyDatas.size());

  policyDatas.insert(policyData3);
  EXPECT_EQ(3, policyDatas.size());

  policyDatas.insert(policyDataNone);
  EXPECT_EQ(4, policyDatas.size());

  policyDatas.insert(policyData4);
  EXPECT_EQ(5, policyDatas.size());

  policyDatas.insert(policyData5);
  EXPECT_EQ(6, policyDatas.size());

  EXPECT_EQ(policyData1, policyData1_same);
  EXPECT_NE(policyData1, policyData2);
  EXPECT_NE(policyData1, policyDataNone);
  EXPECT_NE(policyData1, policyData4);
  EXPECT_NE(policyData1, policyData5);

  // Check that policy5 and policyData5WithMedFlag should be considered same.
  BgpPolicyActionData policyData5WithMedFlag{2, 1, data1};
  policyData5.isMedSetByPolicy = false;
  policyData5WithMedFlag.isMedSetByPolicy = true;

  policyDatas.insert(policyData5WithMedFlag);

  EXPECT_EQ(6, policyDatas.size());
  EXPECT_EQ(policyData5WithMedFlag, policyData5);

  EXPECT_EQ(policyData5.hash(), policyData5WithMedFlag.hash());

  /*
   * Reproduce P1: hash() does not include switchId or multiPathSize,
   * violating the hash/equality contract. Objects that differ only by
   * switchId or multiPathSize are != by operator== but produce the same
   * hash, causing excessive collisions in hash-based containers.
   */
  BgpPolicyActionData diffSwitchId1{1, 1, data1};
  BgpPolicyActionData diffSwitchId2{2, 1, data1};
  EXPECT_NE(diffSwitchId1, diffSwitchId2);
  EXPECT_NE(diffSwitchId1.hash(), diffSwitchId2.hash())
      << "Objects differing by switchId must have different hashes";

  BgpPolicyActionData diffMultiPath1{1, 1, data1};
  BgpPolicyActionData diffMultiPath2{1, 2, data1};
  EXPECT_NE(diffMultiPath1, diffMultiPath2);
  EXPECT_NE(diffMultiPath1.hash(), diffMultiPath2.hash())
      << "Objects differing by multiPathSize must have different hashes";
}

// TODO: Enable the test case after materialized JSON is modified.
TEST_F(PolicyTest, setPolicyFromFileTest) {
  PolicyManager policyManager(
      kMaterializedPolicyFileName, createTestBgpGlobalConfig());
  const auto& policy1 = policyManager.getPolicy();
  // test a few selected policy configs
  const auto& policyStatement = policy1.bgp_policy_statements()[0];
  EXPECT_EQ("FA-ESW-IN-20170531", *policyStatement.name());
  EXPECT_EQ("FAv3 - ESW Ingress policy", policyStatement.description());
  EXPECT_EQ("0", *policyStatement.policy_version());
  EXPECT_EQ(8, policyStatement.policy_entries()->size());
  EXPECT_EQ(
      "FATAGG-ADMIT-NOT-TO-FAS", *policyStatement.policy_entries()[0].name());
  EXPECT_EQ(
      "FATAGG-ADMIT-NOT-TO-FAS - reject token 65520:404",
      *policyStatement.policy_entries()[0].description());
  EXPECT_EQ(
      1,
      policyStatement.policy_entries()[0]
          .policy_match_entries()
          ->match_entries()
          ->size());
  EXPECT_EQ(
      "65520:404",
      policyStatement.policy_entries()[0]
          .policy_match_entries()
          ->match_entries()[0]
          .communities_filter()
          ->communities()
          ->at(0));
  EXPECT_EQ(
      3, policyStatement.policy_entries()[1].policy_action_entries()->size());
}

TEST_F(PolicyTest, setPolicyTest) {
  // create a new config
  bgp_policy::BgpPolicyStatement bgpPolicyStatement =
      createBgpPolicyStatement("Policy Statement 1");
  auto bgpTerm1 = createBgpPolicyTerm("match community 1");
  const auto& bgpTerm2 = createBgpPolicyTerm("empty term");
  const auto& bgpMatch1 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST, {kCommunity1, kCommunity2});
  const auto& bgpMatch2 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::AS_PATH,
      {kASPath1, kASPath2},
      BooleanOperator::AND);
  bgp_policy::BgpPolicyMatch bgpPolicyMatch;
  bgpPolicyMatch.match_entries()->emplace_back(bgpMatch1);
  bgpPolicyMatch.match_entries()->emplace_back(bgpMatch2);
  bgpTerm1.policy_match_entries() = bgpPolicyMatch;

  const auto& bgpAction1 =
      createBgpPolicyAction(BgpPolicyActionType::COMMUNITY_LIST, {kCommunity3});
  bgpTerm1.policy_action_entries()->emplace_back(bgpAction1);

  bgpPolicyStatement.policy_entries()->emplace_back(bgpTerm1);
  bgpPolicyStatement.policy_entries()->emplace_back(bgpTerm2);

  Policy policy(bgpPolicyStatement, createTestBgpGlobalConfig());
  const auto& bgpPolicyStatementFromPolicy = policy.getPolicyStatement();
  EXPECT_EQ(bgpPolicyStatement, bgpPolicyStatementFromPolicy);
}

TEST_F(PolicyTest, populatePolicyDatabaseTest) {
  PolicyManager policyManager(
      kMaterializedPolicyFileName, createTestBgpGlobalConfig());
  const auto& policyName = "FA-ESW-IN-20170531";
  const auto& policy1 = policyManager.getPolicyFromName(policyName);

  // test Policy structs
  EXPECT_EQ("FA-ESW-IN-20170531", policy1->getPolicyName());
  // test term structs
  const auto& bgpTerms = policy1->getPolicyTerms();
  EXPECT_EQ(8, bgpTerms.size());
  const auto& bgpTerm0 = bgpTerms[0];
  EXPECT_EQ("FATAGG-ADMIT-NOT-TO-FAS", bgpTerm0->getTermName());
  EXPECT_EQ(
      "FATAGG-ADMIT-NOT-TO-FAS - reject token 65520:404",
      bgpTerm0->getTermDescription());
  // test match structs
  const auto& bgpMatches = bgpTerm0->getPolicyAttributeMatches();
  EXPECT_EQ(1, bgpMatches.size());

  // test PolicyMatch elements
  const auto& firstMatch = bgpMatches[0];
  EXPECT_EQ(
      std::type_index(typeid(CommunityMatch)),
      std::type_index(typeid(*firstMatch)));

  // test action struct
  const auto& bgpTerm1 = bgpTerms[1];
  auto bgpActions = bgpTerm1->getPolicyActions();
  EXPECT_EQ(3, bgpActions.size());
  const auto& firstAction = bgpActions[0];
  //   auto& action = *firstAction;
  EXPECT_EQ(classType<SetAsPathPrependAction>(), firstAction->getClassType());

  const auto& thirdAction = bgpActions[2];
  EXPECT_EQ(classType<SetAsPath>(), thirdAction->getClassType());
}

TEST_F(PolicyTest, populatePolicyDatabaseWithNullConfigTest) {
  PolicyManager policyManager(
      kMaterializedPolicyFileName, nullptr /* BgpGlobalConfig */);
  const auto& policyName = "FA-ESW-IN-20170531";
  const auto& policy1 = policyManager.getPolicyFromName(policyName);

  // test Policy structs
  EXPECT_EQ("FA-ESW-IN-20170531", policy1->getPolicyName());
  // test term structs
  const auto& bgpTerms = policy1->getPolicyTerms();
  EXPECT_EQ(8, bgpTerms.size());
  const auto& bgpTerm0 = bgpTerms[0];
  EXPECT_EQ("FATAGG-ADMIT-NOT-TO-FAS", bgpTerm0->getTermName());
  EXPECT_EQ(
      "FATAGG-ADMIT-NOT-TO-FAS - reject token 65520:404",
      bgpTerm0->getTermDescription());
  // test match structs
  const auto& bgpMatches = bgpTerm0->getPolicyAttributeMatches();
  EXPECT_EQ(1, bgpMatches.size());

  // test PolicyMatch elements
  const auto& firstMatch = bgpMatches[0];
  EXPECT_EQ(
      std::type_index(typeid(CommunityMatch)),
      std::type_index(typeid(*firstMatch)));

  // test action struct
  const auto& bgpTerm1 = bgpTerms[1];
  auto bgpActions = bgpTerm1->getPolicyActions();
  EXPECT_EQ(3, bgpActions.size());
  const auto& firstAction = bgpActions[0];
  //   auto& action = *firstAction;
  EXPECT_EQ(classType<SetAsPathPrependAction>(), firstAction->getClassType());

  const auto& thirdAction = bgpActions[2];
  EXPECT_EQ(classType<SetAsPath>(), thirdAction->getClassType());
}

TEST_F(PolicyTest, setPolicyFromFileExceptionTest) {
  std::string policyFile =
      "neteng/fboss/bgp/cpp/tests/sample_configs/bgp_policy.materialized_JSO";
  try {
    PolicyManager policyManager(policyFile, createTestBgpGlobalConfig());
    ADD_FAILURE();
  } catch (const BgpError& error) {
    EXPECT_EQ(
        *error.message(),
        "Fail to read the BGP Policy file: "
        "neteng/fboss/bgp/cpp/tests/sample_configs/bgp_policy.materialized_JSO");
  }
}

/*
 * This test guards against changing the definition of
 * PolicyAttributesMask to unintended behavior.
 */
TEST_F(PolicyTest, InitializePolicyAttributesMaskTest) {
  PolicyAttributesMask mask;
  // All fields should be initialized to false.
  auto voidPtr = static_cast<void*>(&mask);
  bool* boolPtr = static_cast<bool*>(voidPtr);
  for (int i = 0; i < (sizeof(mask) / sizeof(bool)); ++i) {
    EXPECT_FALSE(*boolPtr);
    ++boolPtr;
  }
}

/*
 * Verify that the policy attributes mask sets the flag correctly
 * from the policy action atom.
 */
TEST_F(PolicyTest, SetPolicyAttributesMaskFromActionTest) {
  bgp_policy::BgpPolicyAction action;
  bgp_policy::BgpPolicyTerm term;
  {
    // SetLocalPreference
    term.policy_action_entries()->clear();
    // Construct action.
    action.type() = bgp_policy::BgpPolicyActionType::SET_LOCAL_PREF;
    bgp_policy::LocalPreference localPref;
    localPref.local_pref() = 10;
    action.set_local_pref() = localPref;
    term.policy_action_entries() = {action};

    // Check flag is set.
    PolicyAttributesMask observed;
    PolicyTerm pt(term, observed, createTestBgpGlobalConfig());

    PolicyAttributesMask expected{.localPref = true};
    EXPECT_EQ(expected, observed);
  }
  {
    // SetOrigin
    term.policy_action_entries()->clear();
    // Construct action.
    action.type() = bgp_policy::BgpPolicyActionType::ORIGIN;
    action.set_origin() = bgp_policy::Origin::IGP;
    term.policy_action_entries() = {action};

    // Check flag is set.
    PolicyAttributesMask observed;
    PolicyTerm pt(term, observed, createTestBgpGlobalConfig());

    PolicyAttributesMask expected{.origin = true};
    EXPECT_EQ(expected, observed);
  }
  {
    // SetNexthop
    term.policy_action_entries()->clear();
    // Construct action.
    action.type() = bgp_policy::BgpPolicyActionType::NEXT_HOP;
    bgp::routing_policy::NextHop nextHop;
    nextHop.version() = 1;
    nextHop.next_hop_prefix() = kV4Nexthop2.str();
    bgp_policy::SetNextHop setNexthop;
    setNexthop.set_self() = false;
    setNexthop.next_hop() = nextHop;
    action.set_nexthop() = setNexthop;
    term.policy_action_entries() = {action};

    // Check flag is set.
    PolicyAttributesMask observed;
    PolicyTerm pt(term, observed, createTestBgpGlobalConfig());

    PolicyAttributesMask expected{.nexthop = true};
    EXPECT_EQ(expected, observed);
  }
  {
    // SetMed
    term.policy_action_entries()->clear();
    // Construct action.
    action.type() = bgp_policy::BgpPolicyActionType::MED;
    bgp_policy::MedAction med;
    med.med_value() = 0;
    med.med_action_type() = bgp_policy::MedActionType::SET;
    action.med_action() = med;
    term.policy_action_entries() = {action};

    // Check flag is set.
    PolicyAttributesMask observed;
    PolicyTerm pt(term, observed, createTestBgpGlobalConfig());

    PolicyAttributesMask expected{.med = true};
    EXPECT_EQ(expected, observed);
  }
  {
    // SetWeight
    term.policy_action_entries()->clear();
    // Construct action.
    action.type() = bgp_policy::BgpPolicyActionType::WEIGHT;
    bgp_policy::WeightAction weight;
    weight.weight_value() = 0;
    weight.weight_action_type() = bgp_policy::WeightActionType::SET;
    action.weight_action() = weight;
    term.policy_action_entries() = {action};

    // Check flag is set.
    PolicyAttributesMask observed;
    PolicyTerm pt(term, observed, createTestBgpGlobalConfig());

    PolicyAttributesMask expected{.weight = true};
    EXPECT_EQ(expected, observed);
  }
  {
    // SetAsPath
    term.policy_action_entries()->clear();
    // Construct action.
    action.type() = bgp_policy::BgpPolicyActionType::AS_PATH;
    action.as_path_overwrite_list() = {};
    term.policy_action_entries() = {action};

    // Check flag is set only for asPath.
    PolicyAttributesMask observed;
    PolicyTerm pt(term, observed, createTestBgpGlobalConfig());

    PolicyAttributesMask expected{.asPath = true};
    EXPECT_EQ(expected, observed);
  }
  {
    // AsPathToAsSet
    term.policy_action_entries()->clear();
    // Construct action.
    action.type() = bgp_policy::BgpPolicyActionType::AS_PATH_TO_AS_SET;
    action.as_path_to_as_set_action() = bgp_policy::AsPathToAsSetAction();
    term.policy_action_entries() = {action};

    // Check flag is set only for asPath.
    PolicyAttributesMask observed;
    PolicyTerm pt(term, observed, createTestBgpGlobalConfig());

    PolicyAttributesMask expected{.asPath = true};
    EXPECT_EQ(expected, observed);
  }
  {
    // SetAsPathPrepend
    term.policy_action_entries()->clear();
    // Construct action.
    action.type() = bgp_policy::BgpPolicyActionType::AS_PATH_PREPEND;
    bgp_policy::SetAsPathPrepend asPrepend;
    asPrepend.asn() = 1;
    asPrepend.repeat_times() = 1;
    action.set_as_path_prepend() = asPrepend;
    term.policy_action_entries() = {action};

    // Check flag is set only for asPath.
    PolicyAttributesMask observed;
    PolicyTerm pt(term, observed, createTestBgpGlobalConfig());

    PolicyAttributesMask expected{.asPath = true};
    EXPECT_EQ(expected, observed);
  }
  {
    // CommunityAction
    term.policy_action_entries()->clear();
    // Construct action.
    action.type() = bgp_policy::BgpPolicyActionType::COMMUNITY_LIST;
    action.community_action() = bgp_policy::CommunityAction();

    // Case 1: SET
    {
      action.community_action()->action_type() =
          bgp_policy::CommunityActionType::SET;
      term.policy_action_entries() = {action};
      // Check flag is set.
      PolicyAttributesMask observed;
      PolicyTerm pt(term, observed, createTestBgpGlobalConfig());
      PolicyAttributesMask expected{.communities = true};
      EXPECT_EQ(expected, observed);
    }
    // Case 2: ADD/REMOVE
    {
      action.community_action()->action_type() =
          bgp_policy::CommunityActionType::ADD;
      term.policy_action_entries() = {action};
      // Check flag is set for communities.
      PolicyAttributesMask observed;
      PolicyTerm pt2(term, observed, createTestBgpGlobalConfig());
      PolicyAttributesMask expected{.communities = true};
      EXPECT_EQ(expected, observed);
    }
  }
  {
    // LbwExtCommunityAction
    term.policy_action_entries()->clear();
    PolicyAttributesMask extCommunityOnlyMask{
        .extCommunities = true, .customizedLbwEnabled = true};
    nsf_policy::NsfTeWeightEncoding encoding;
    const int encodingId = 1;

    auto lCheckLbwExtCommunityAction =
        [&](const bgp_policy::LbwExtCommunityActionType type) {
          action =
              createBgpPolicyLbwExtCommunityAction(type, encoding, encodingId);
          term.policy_action_entries() = {action};
          PolicyAttributesMask observed;
          PolicyTerm pt(term, observed, createTestBgpGlobalConfig());
          return observed;
        };
    EXPECT_EQ(
        extCommunityOnlyMask,
        lCheckLbwExtCommunityAction(
            bgp_policy::LbwExtCommunityActionType::DISABLE));
    EXPECT_EQ(
        extCommunityOnlyMask,
        lCheckLbwExtCommunityAction(
            bgp_policy::LbwExtCommunityActionType::ACCEPT));
    EXPECT_EQ(
        extCommunityOnlyMask,
        lCheckLbwExtCommunityAction(
            bgp_policy::LbwExtCommunityActionType::DECODE_ALL));
    EXPECT_EQ(
        extCommunityOnlyMask,
        lCheckLbwExtCommunityAction(
            bgp_policy::LbwExtCommunityActionType::SET_LINK_BPS));
    EXPECT_EQ(
        extCommunityOnlyMask,
        lCheckLbwExtCommunityAction(
            bgp_policy::LbwExtCommunityActionType::BEST_PATH));
    EXPECT_EQ(
        extCommunityOnlyMask,
        lCheckLbwExtCommunityAction(
            bgp_policy::LbwExtCommunityActionType::AGGREGATE_RECEIVED));
    EXPECT_EQ(
        extCommunityOnlyMask,
        lCheckLbwExtCommunityAction(
            bgp_policy::LbwExtCommunityActionType::
                ENCODE_AGGREGATE_RECEIVED_OVERWRITE));
    EXPECT_EQ(
        extCommunityOnlyMask,
        lCheckLbwExtCommunityAction(
            bgp_policy::LbwExtCommunityActionType::ENCODE_SWITCH_ID));
    EXPECT_EQ(
        extCommunityOnlyMask,
        lCheckLbwExtCommunityAction(
            bgp_policy::LbwExtCommunityActionType::ENCODE_MULTIPATH));
    EXPECT_EQ(
        extCommunityOnlyMask,
        lCheckLbwExtCommunityAction(
            bgp_policy::LbwExtCommunityActionType::
                DECODE_AGGREGATE_CAPACITY_OVERWRITE));
  }
  {
    // ExtCommunityAction
    term.policy_action_entries()->clear();
    // Case 1: EXT_COMMUNITY_LIST_SET
    {
      action = createBgpPolicyExtCommunityAction(
          bgp_policy::BgpAttrChangeActionType::EXT_COMMUNITY_LIST_SET,
          {} /* communities */);

      term.policy_action_entries() = {action};
      // Check flag is set.
      PolicyAttributesMask observed;
      PolicyTerm pt(term, observed, createTestBgpGlobalConfig());
      PolicyAttributesMask expected{.extCommunities = true};
      EXPECT_EQ(expected, observed);
    }
    // Case 2: EXT_COMMUNITY_LIST_ADD
    {
      action = createBgpPolicyExtCommunityAction(
          bgp_policy::BgpAttrChangeActionType::EXT_COMMUNITY_LIST_ADD,
          {} /* communities */);
      term.policy_action_entries() = {action};
      // Check flag is set.
      PolicyAttributesMask observed;
      PolicyTerm pt(term, observed, createTestBgpGlobalConfig());
      PolicyAttributesMask expected{.extCommunities = true};
      EXPECT_EQ(expected, observed);
    }
    // Case 3: EXT_COMMUNITY_LIST_REMOVE
    {
      action = createBgpPolicyExtCommunityAction(
          bgp_policy::BgpAttrChangeActionType::EXT_COMMUNITY_LIST_REMOVE,
          {} /* communities */);
      term.policy_action_entries() = {action};
      // Check flag is set.
      PolicyAttributesMask observed;
      PolicyTerm pt(term, observed, createTestBgpGlobalConfig());
      PolicyAttributesMask expected{.extCommunities = true};
      EXPECT_EQ(expected, observed);
    }
  }
}

/*
 * Verify that the policy attributes mask sets the flag correctly
 * from the policy match atom.
 */
TEST_F(PolicyTest, SetPolicyAttributesMaskFromMatchTest) {
  bgp_policy::BgpPolicyTerm term;
  bgp_policy::BgpPolicyMatch match;
  /*
   * Matches:
   * AS_PATH_LEN
   */
  {
    match.match_entries() = {
        createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::AS_PATH_LEN)};
    term.policy_match_entries() = match;
    // Check flag is set.
    PolicyAttributesMask observed;
    PolicyTerm pt(term, observed, createTestBgpGlobalConfig());
    PolicyAttributesMask expected{.asPath = true};
    EXPECT_EQ(expected, observed);
  }
  // AS_PATH
  {
    match.match_entries() = {createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::AS_PATH, {kASPathRegex5})};
    term.policy_match_entries() = match;
    // Check flag is set.
    PolicyAttributesMask observed;
    PolicyTerm pt(term, observed, createTestBgpGlobalConfig());
    PolicyAttributesMask expected{.asPath = true};
    EXPECT_EQ(expected, observed);
  }
  // COMMUNITY_LIST
  {
    match.match_entries() = {createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::COMMUNITY_LIST, {kCommunity1})};
    term.policy_match_entries() = match;
    // Check flag is set.
    PolicyAttributesMask observed;
    PolicyTerm pt(term, observed, createTestBgpGlobalConfig());
    PolicyAttributesMask expected{.communities = true};
    EXPECT_EQ(expected, observed);
  }
  // ORIGIN
  {
    match.match_entries() = {
        createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::ORIGIN)};
    term.policy_match_entries() = match;
    // Check flag is set.
    PolicyAttributesMask observed;
    PolicyTerm pt(term, observed, createTestBgpGlobalConfig());
    PolicyAttributesMask expected{.origin = true};
    EXPECT_EQ(expected, observed);
  }
  // PREFIX_LIST
  {
    routing_policy::CompareNumericValue compareStructEQ;
    compareStructEQ.compare_operator() = routing_policy::ComparisonOperator::EQ;
    compareStructEQ.value() = kV6Prefix1.second;
    const auto& prefixListEntry = createPrefixListEntry(
        IPAddress::networkToString(kV6Prefix1), {compareStructEQ});
    const auto& prefixMatch = createPrefixListMatch({prefixListEntry});
    match.match_entries() = {prefixMatch};
    term.policy_match_entries() = match;
    // Check flag is set.
    PolicyAttributesMask observed;
    PolicyTerm pt(term, observed, createTestBgpGlobalConfig());
    PolicyAttributesMask expected{.prefix = true};
    EXPECT_EQ(expected, observed);
  }
  // COMMUNITY_COUNT
  {
    match.match_entries() = {
        createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::COMMUNITY_COUNT)};
    term.policy_match_entries() = match;
    // Check flag is set.
    PolicyAttributesMask observed;
    PolicyTerm pt(term, observed, createTestBgpGlobalConfig());
    PolicyAttributesMask expected{.communities = true};
    EXPECT_EQ(expected, observed);
  }
  // AS_PATH_LEN_WITH_CONFED
  {
    match.match_entries() = {createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::AS_PATH_LEN_WITH_CONFED)};
    term.policy_match_entries() = match;
    // Check flag is set.
    PolicyAttributesMask observed;
    PolicyTerm pt(term, observed, createTestBgpGlobalConfig());
    PolicyAttributesMask expected{.asPath = true};
    EXPECT_EQ(expected, observed);
  }
  // ALWAYS
  {
    bgp_policy::BgpPolicyAtomicMatch alwaysMatch;
    alwaysMatch.type() = BgpPolicyAtomicMatchType::ALWAYS;
    match.match_entries() = {alwaysMatch};
    term.policy_match_entries() = match;
    // Check flag is not set.
    PolicyAttributesMask observed;
    PolicyTerm pt(term, observed, createTestBgpGlobalConfig());
    PolicyAttributesMask expected;
    EXPECT_EQ(expected, observed);
  }
}

TEST_F(PolicyTest, PopulatePolicyAttributesMaskTest) {
  std::string policyName = "policy";
  {
    // setupAcceptAllPolicy
    auto policyManager = setupAcceptAllPolicy(policyName);
    /*
     * All flags should be set to true as there is no criteria for permitting
     * and no mutation action.
     */
    const PolicyAttributesMask expected;
    auto observedMask = policyManager->getPolicyAttributesMask(policyName);
    EXPECT_TRUE(observedMask);
    EXPECT_EQ(expected, *observedMask);
  }
  {
    // setupDenyIgpOriginAcceptAllPolicy
    auto policyManager = setupDenyIgpOriginAcceptAllPolicy(policyName);
    // Existing origin value is used in match.
    const PolicyAttributesMask expected = {
        .origin = true,
    };
    auto observedMask = policyManager->getPolicyAttributesMask(policyName);
    EXPECT_TRUE(observedMask);
    EXPECT_EQ(expected, *observedMask);
  }
  {
    // setupMatchAllSetOriginIgpPolicy
    auto policyManager = setupMatchAllSetOriginIgpPolicy(policyName);
    // Origin is modified,
    const PolicyAttributesMask expected = {.origin = true};
    auto observedMask = policyManager->getPolicyAttributesMask(policyName);
    EXPECT_TRUE(observedMask);
    EXPECT_EQ(expected, *observedMask);
  }
  {
    // setupMatchAllSetCommunityPolicy
    auto policyManager = setupMatchAllSetCommunityPolicy(policyName);
    const PolicyAttributesMask expected = {.communities = true};
    auto observedMask = policyManager->getPolicyAttributesMask(policyName);
    EXPECT_TRUE(observedMask);
    EXPECT_EQ(expected, *observedMask);
  }
  {
    // bgp_policy.materialized_JSON
    policyName = "FA-ESW-IN-20170531";
    /*
     *   { policy_match_entries: communities_filter,
     *     policy_action_entries: deny },
     *   { policy_match_entries: communities_filter,
     *     policy_action_entries:
     *         set_as_path_prepend,
     *         remove community 65520:491,
     *         as_path_overwrite_list },
     *   { policy_match_entries: communities_filter,
     *     policy_action_entries:
     *         set_as_path_prepend,
     *         remove community 65520:492,
     *         explicit deny all
     *   }
     */
    PolicyManager policyManager(
        kMaterializedPolicyFileName, createTestBgpGlobalConfig());
    const PolicyAttributesMask expected = {
        .asPath = true,
        .communities = true,
    };
    auto observedMask = policyManager.getPolicyAttributesMask(policyName);
    EXPECT_TRUE(observedMask);
    EXPECT_EQ(expected, *observedMask);
  }
}

TEST_F(PolicyTest, CommunityMatchStringTest) {
  /*
   * set three communityMatch struct, including different communities and
   * logic operator (AND/OR), with exact strings.
   * bgpMatch3 is created to test the matching of sorted community string vector
   * Test three cases: update contains only one of the
   * communities in an AND match; update contrains exact set of communities;
   * update contains one of the communities in an OR match
   */
  const auto& bgpMatch1 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST,
      {kCommunity1, kCommunity2},
      BooleanOperator::OR,
      "community_match1");
  const auto& bgpMatch2 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST,
      {kCommunity1, kCommunity2},
      BooleanOperator::AND,
      "community_match2");
  // community in unsorted order with an OR
  const auto& bgpMatch3 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST,
      {kCommunity2, kCommunity3, kCommunity1},
      BooleanOperator::OR,
      "community_match3");
  // community in unsorted order with an AND
  const auto& bgpMatch4 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST,
      {kCommunity2, kCommunity3, kCommunity1},
      BooleanOperator::AND,
      "community_match4");
  string policyName1 = "Policy Statement 1";
  const auto& bgpPolicies = createBgpPolicies(
      policyName1, {bgpMatch1, bgpMatch2, bgpMatch3, bgpMatch4});

  PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
  const auto& policy1 = policyManager.getPolicyFromName(policyName1);
  const auto& bgpTerms = policy1->getPolicyTerms();
  const auto& orMatch = bgpTerms[0]->getPolicyAttributeMatches()[0];
  const auto& andMatch = bgpTerms[0]->getPolicyAttributeMatches()[1];
  // for testing the ordered vector matching of community strings
  const auto& sequenceOrMatch = bgpTerms[0]->getPolicyAttributeMatches()[2];
  const auto& sequenceAndMatch = bgpTerms[0]->getPolicyAttributeMatches()[3];

  std::vector<std::string> communities = {kCommunity1};
  const auto& attributes = createBgpPath(communities);
  EXPECT_TRUE(orMatch->Match(attributes));
  EXPECT_FALSE(andMatch->Match(attributes));
  EXPECT_TRUE(sequenceOrMatch->Match(attributes));
  EXPECT_FALSE(sequenceAndMatch->Match(attributes));
  // Test positive case for both AND and OR matches
  communities = {kCommunity1, kCommunity2};
  const auto& attributes2 = createBgpPath(communities);
  EXPECT_TRUE(orMatch->Match(attributes2));
  EXPECT_TRUE(andMatch->Match(attributes2));
  EXPECT_TRUE(sequenceOrMatch->Match(attributes2));
  EXPECT_FALSE(sequenceAndMatch->Match(attributes2));
  // test negative case for both AND and OR matches
  communities = {kCommunity3};
  const auto& attributes3 = createBgpPath(communities);
  EXPECT_FALSE(orMatch->Match(attributes3));
  EXPECT_FALSE(andMatch->Match(attributes3));
  EXPECT_TRUE(sequenceOrMatch->Match(attributes3));
  EXPECT_FALSE(sequenceAndMatch->Match(attributes3));
  /*
   * test positive cases for all four matches
   * attributes has a different order of communities than the order in match
   */
  communities = {kCommunity3, kCommunity1, kCommunity2};
  const auto& attributes4 = createBgpPath(communities);
  EXPECT_TRUE(orMatch->Match(attributes4));
  EXPECT_TRUE(andMatch->Match(attributes4));
  EXPECT_TRUE(sequenceOrMatch->Match(attributes4));
  EXPECT_TRUE(sequenceAndMatch->Match(attributes4));
}

TEST_F(PolicyTest, CommunityMatchStringExactTest) {
  /*
   * set three communityMatch struct, including different communities and
   * logic operator (AND/OR), with exact strings.
   * bgpMatch3 is created to test the matching of sorted community string vector
   * Test three cases: update contains only one of the
   * communities in an AND match; update contrains exact set of communities;
   * update contains one of the communities in an OR match
   */
  auto bgpMatch1 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST,
      {kCommunity1, kCommunity2},
      BooleanOperator::OR,
      "community_match1");
  bgpMatch1.communities_filter()->exact_match() = true;
  auto bgpMatch2 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST,
      {kCommunity1, kCommunity2},
      BooleanOperator::AND,
      "community_match2");
  bgpMatch2.communities_filter()->exact_match() = true;
  // community in unsorted order with an OR
  auto bgpMatch3 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST,
      {kCommunity2, kCommunity3, kCommunity1},
      BooleanOperator::OR,
      "community_match3");
  bgpMatch3.communities_filter()->exact_match() = false;
  // community in unsorted order with an AND
  auto bgpMatch4 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST,
      {kCommunity2, kCommunity3, kCommunity1},
      BooleanOperator::AND,
      "community_match4");
  bgpMatch4.communities_filter()->exact_match() = true;
  string policyName1 = "Policy Statement 1";
  const auto& bgpPolicies = createBgpPolicies(
      policyName1, {bgpMatch1, bgpMatch2, bgpMatch3, bgpMatch4});

  PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
  const auto& policy1 = policyManager.getPolicyFromName(policyName1);
  const auto& bgpTerms = policy1->getPolicyTerms();
  const auto& orMatch = bgpTerms[0]->getPolicyAttributeMatches()[0];
  const auto& andMatch = bgpTerms[0]->getPolicyAttributeMatches()[1];
  // for testing the ordered vector matching of community strings
  const auto& sequenceOrMatch = bgpTerms[0]->getPolicyAttributeMatches()[2];
  const auto& sequenceAndMatch = bgpTerms[0]->getPolicyAttributeMatches()[3];

  std::vector<std::string> communities = {kCommunity1};
  const auto& attributes = createBgpPath(communities);

  EXPECT_TRUE(orMatch->Match(attributes));
  EXPECT_FALSE(andMatch->Match(attributes));
  EXPECT_TRUE(sequenceOrMatch->Match(attributes));
  EXPECT_FALSE(sequenceAndMatch->Match(attributes));

  // Test positive case for both AND and OR matches
  communities = {kCommunity1, kCommunity2};
  const auto& attributes2 = createBgpPath(communities);
  EXPECT_TRUE(orMatch->Match(attributes2));
  EXPECT_TRUE(andMatch->Match(attributes2));
  EXPECT_TRUE(sequenceOrMatch->Match(attributes2));
  EXPECT_FALSE(sequenceAndMatch->Match(attributes2));

  // test negative case for both AND and OR matches
  communities = {kCommunity3};
  const auto& attributes3 = createBgpPath(communities);
  EXPECT_FALSE(orMatch->Match(attributes3));
  EXPECT_FALSE(andMatch->Match(attributes3));
  EXPECT_TRUE(sequenceOrMatch->Match(attributes3));
  EXPECT_FALSE(sequenceAndMatch->Match(attributes3));
  // attributes has a different order of communities than the order in match
  communities = {kCommunity3, kCommunity1, kCommunity2};
  const auto& attributes4 = createBgpPath(communities);
  // test negative for non-exact
  EXPECT_FALSE(orMatch->Match(attributes4));
  EXPECT_FALSE(andMatch->Match(attributes4));
  // test positive for exact
  EXPECT_TRUE(sequenceOrMatch->Match(attributes4));
  EXPECT_TRUE(sequenceAndMatch->Match(attributes4));
}

TEST_F(PolicyTest, CommunityMatchRegexTest) {
  /*
   * set two communities match struct, including different communities and
   * logic operator (AND/OR) with regex match.
   * Test update communities matches AND/OR regex;
   * update communities do not match AND/OR regex
   */
  const auto& bgpMatch1 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST,
      {kCommunityRegex1, kCommunityRegex2},
      BooleanOperator::OR,
      "community_regexmatch1");
  const auto& bgpMatch2 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST,
      {kCommunityRegex1, kCommunityRegex2},
      BooleanOperator::AND,
      "community_regexmatch2");
  const string policyName1 = "Policy Statement 1";
  const auto& bgpPolicies =
      createBgpPolicies(policyName1, {bgpMatch1, bgpMatch2});

  PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
  const auto& policy1 = policyManager.getPolicyFromName(policyName1);
  const auto& bgpTerms = policy1->getPolicyTerms();
  const auto& regexOrMatch = bgpTerms[0]->getPolicyAttributeMatches()[0];
  const auto& regexAndMatch = bgpTerms[0]->getPolicyAttributeMatches()[1];

  // test regex match, regex is 65500:1.*
  std::vector<std::string> communities = {kCommunityMatchingRegex1};
  const auto& attributes1 = createBgpPath(communities);
  EXPECT_TRUE(regexOrMatch->Match(attributes1));
  EXPECT_FALSE(regexAndMatch->Match(attributes1));

  // test negative case of both and/or regex match
  communities = {kCommunityNotMatchingRegex1};
  const auto& attributes2 = createBgpPath(communities);
  EXPECT_FALSE(regexOrMatch->Match(attributes2));
  EXPECT_FALSE(regexAndMatch->Match(attributes2));
}

TEST_F(PolicyTest, CommunityMatchRegexExactTest) {
  /*
   * set two communities match struct, including different communities and
   * logic operator (AND/OR) with regex match.
   * Test update communities matches AND/OR regex;
   * update communities do not match AND/OR regex
   */
  return;
  auto bgpMatch1 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST,
      {kCommunityRegex1, kCommunityRegex2},
      BooleanOperator::OR,
      "community_regexmatch1");
  bgpMatch1.communities_filter()->exact_match() = true;
  auto bgpMatch2 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST,
      {kCommunityRegex1, kCommunityRegex2},
      BooleanOperator::AND,
      "community_regexmatch2");
  bgpMatch2.communities_filter()->exact_match() = true;
  const string policyName1 = "Policy Statement 1";
  const auto& bgpPolicies =
      createBgpPolicies(policyName1, {bgpMatch1, bgpMatch2});

  PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
  const auto& policy1 = policyManager.getPolicyFromName(policyName1);
  const auto& bgpTerms = policy1->getPolicyTerms();
  const auto& regexOrMatch = bgpTerms[0]->getPolicyAttributeMatches()[0];
  const auto& regexAndMatch = bgpTerms[0]->getPolicyAttributeMatches()[1];

  // test regex match, regex are 65500:1.* and 65500:2.*
  std::vector<std::string> communities = {
      kCommunityMatchingRegex1, kCommunityMatchingRegex2};
  const auto& attributes1 = createBgpPath(communities);
  EXPECT_TRUE(regexOrMatch->Match(attributes1));
  EXPECT_TRUE(regexAndMatch->Match(attributes1));

  // test regex non-exact, regex is 65500:1.*
  communities = {
      kCommunityMatchingRegex1,
      kCommunityMatchingRegex2,
      kCommunityNotMatchingRegex1};
  const auto& attributes2 = createBgpPath(communities);
  EXPECT_FALSE(regexOrMatch->Match(attributes2));
  EXPECT_FALSE(regexAndMatch->Match(attributes2));

  // test negative case of both and/or regex match
  communities = {kCommunityNotMatchingRegex1};
  const auto& attributes3 = createBgpPath(communities);
  EXPECT_FALSE(regexOrMatch->Match(attributes3));
  EXPECT_FALSE(regexAndMatch->Match(attributes3));
}

TEST_F(PolicyTest, CommunityMatchNegativeTest) {
  /*
   * test two negative cases:
   * 1: empty community list, should throw BgpError;
   * 2: malformed community match, should throw BgpError
   */
  {
    // empty clause for update with and without community attributes
    const auto& bgpMatch1 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::COMMUNITY_LIST,
        {},
        BooleanOperator::OR,
        "community_match1");
    const auto& bgpMatch2 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::COMMUNITY_LIST,
        {},
        BooleanOperator::AND,
        "community_match2");
    const string policyName1 = "Policy Statement 1";
    const auto& bgpPolicies =
        createBgpPolicies(policyName1, {bgpMatch1, bgpMatch2});

    // Empty match would throw BgpError
    EXPECT_THROW(
        PolicyManager(bgpPolicies, createTestBgpGlobalConfig()), BgpError);
  }
  {
    /*
     * update with no community attributes for a match with community attributes
     * and a match with empty community list
     */
    const auto& bgpMatch1 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::COMMUNITY_LIST,
        {kCommunity1},
        BooleanOperator::OR,
        "community_match1");
    const auto& bgpMatch2 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::COMMUNITY_LIST,
        {},
        BooleanOperator::OR,
        "community_match2");
    const string policyName1 = "Policy Statement 1";
    const auto& bgpPolicies =
        createBgpPolicies(policyName1, {bgpMatch1, bgpMatch2});

    // Including an empty match would throw BgpError
    EXPECT_THROW(
        PolicyManager(bgpPolicies, createTestBgpGlobalConfig()), BgpError);
  }
  {
    // malformed input
    const auto& bgpMatch1 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::COMMUNITY_LIST,
        {"1+++"},
        BooleanOperator::OR,
        "community_match1");
    const string policyName1 = "Policy Statement 1";
    const auto& bgpPolicies = createBgpPolicies(policyName1, {bgpMatch1});
    try {
      PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
      ADD_FAILURE();
    } catch (const BgpError& error) {
      EXPECT_EQ(*error.message(), "Malformed community config: 1+++");
    }
  }
}

TEST_F(PolicyTest, CommunityCountMatchTest) {
  /*
   * set six CommunityCountMatch match struct, with same count but different
   * comparison metrics: EQ, GT, GE, LE, LT, NE.
   * Test update with the same value of community count, it will pass EQ, LE, GE
   * fail GT, LE, NE test
   * test update with no community attributes
   */
  auto bgpMatch1 =
      createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::COMMUNITY_COUNT);
  *bgpMatch1.community_count()->compare_numeric_value()->compare_operator() =
      routing_policy::ComparisonOperator::EQ;
  *bgpMatch1.community_count()->compare_numeric_value()->value() = 2;
  auto bgpMatch2 =
      createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::COMMUNITY_COUNT);
  *bgpMatch2.community_count()->compare_numeric_value()->compare_operator() =
      routing_policy::ComparisonOperator::GT;
  *bgpMatch2.community_count()->compare_numeric_value()->value() = 2;
  auto bgpMatch3 =
      createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::COMMUNITY_COUNT);
  *bgpMatch3.community_count()->compare_numeric_value()->compare_operator() =
      routing_policy::ComparisonOperator::GE;
  *bgpMatch3.community_count()->compare_numeric_value()->value() = 2;
  auto bgpMatch4 =
      createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::COMMUNITY_COUNT);
  *bgpMatch4.community_count()->compare_numeric_value()->compare_operator() =
      routing_policy::ComparisonOperator::LT;
  *bgpMatch4.community_count()->compare_numeric_value()->value() = 2;
  auto bgpMatch5 =
      createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::COMMUNITY_COUNT);
  *bgpMatch5.community_count()->compare_numeric_value()->compare_operator() =
      routing_policy::ComparisonOperator::LE;
  *bgpMatch5.community_count()->compare_numeric_value()->value() = 2;
  auto bgpMatch6 =
      createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::COMMUNITY_COUNT);
  *bgpMatch6.community_count()->compare_numeric_value()->compare_operator() =
      routing_policy::ComparisonOperator::NE;
  *bgpMatch6.community_count()->compare_numeric_value()->value() = 2;
  const string policyName1 = "Policy Statement 1";
  const auto& bgpPolicies = createBgpPolicies(
      policyName1,
      {bgpMatch1, bgpMatch2, bgpMatch3, bgpMatch4, bgpMatch5, bgpMatch6});
  PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
  const auto& policy1 = policyManager.getPolicyFromName(policyName1);
  const auto& bgpTerms = policy1->getPolicyTerms();
  const auto& commnityCountMatchEQ =
      bgpTerms[0]->getPolicyAttributeMatches()[0];
  const auto& commnityCountMatchGT =
      bgpTerms[0]->getPolicyAttributeMatches()[1];
  const auto& commnityCountMatchGE =
      bgpTerms[0]->getPolicyAttributeMatches()[2];
  const auto& commnityCountMatchLT =
      bgpTerms[0]->getPolicyAttributeMatches()[3];
  const auto& commnityCountMatchLE =
      bgpTerms[0]->getPolicyAttributeMatches()[4];
  const auto& commnityCountMatchNE =
      bgpTerms[0]->getPolicyAttributeMatches()[5];

  std::vector<std::string> communities = {kCommunity1, kCommunity2};
  const auto& attributes = createBgpPath(communities);
  // test positive case for EQ
  EXPECT_TRUE(commnityCountMatchEQ->Match(attributes));
  // test negative case for GT
  EXPECT_FALSE(commnityCountMatchGT->Match(attributes));
  // test positive case for GE
  EXPECT_TRUE(commnityCountMatchGE->Match(attributes));
  // test positive case for LE
  EXPECT_TRUE(commnityCountMatchLE->Match(attributes));
  // test negative case for LT
  EXPECT_FALSE(commnityCountMatchLT->Match(attributes));
  // test negative case for NE
  EXPECT_FALSE(commnityCountMatchNE->Match(attributes));
  // test attributes with empty community
  const auto& attributes2 = createBgpPath();
  // test negative case for EQ
  EXPECT_FALSE(commnityCountMatchEQ->Match(attributes2));
  // test negative case for GT
  EXPECT_FALSE(commnityCountMatchGT->Match(attributes2));
  // test negative case for GE
  EXPECT_FALSE(commnityCountMatchGE->Match(attributes2));
  // test positive case for LE
  EXPECT_TRUE(commnityCountMatchLE->Match(attributes2));
  // test positive case for LT
  EXPECT_TRUE(commnityCountMatchLE->Match(attributes2));
  // test possitive case for NE
  EXPECT_TRUE(commnityCountMatchNE->Match(attributes2));
}

TEST_F(PolicyTest, CommunityMatchStringSetOverlapTest) {
  // check areOverlapped is correct
  std::set<std::string> set1 = {"120:1", "200:2", "300:3"};
  std::set<std::string> set2 = {"150:1", "200:2", "400:4"};
  std::set<std::string> set3 = {"100:1", "220:2", "250:3"};

  EXPECT_TRUE(CommunityMatch::areOverlapped(set1, set2));
  EXPECT_FALSE(CommunityMatch::areOverlapped(set1, set3));
}

TEST_F(PolicyTest, AsPathLenMatchNegativeTest) {
  const auto& expectedStr =
      "BgpPolicyAtomicMatch Config input error for type: 1";
  {
    // Verify as path length must be set
    bgp_policy::BgpPolicyAtomicMatch bgpMatch;
    *bgpMatch.type() = BgpPolicyAtomicMatchType::AS_PATH_LEN;
    bgpMatch.as_path_len_filter().reset();
    const auto& receivedStr = parseMatchConfigGetError(bgpMatch);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
  {
    // Verify as path length cannot be empty
    auto bgpMatch =
        createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::AS_PATH_LEN);
    bgpMatch.as_path_len_filter().reset();
    const auto& receivedStr = parseMatchConfigGetError(bgpMatch);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
}

TEST_F(PolicyTest, AsPathLenMatchTest) {
  /*
   * set three AsPathLenMatch match struct, with same count but different
   * comparison metrics: EQ, GT, GE, LT, LE, NE.
   * Test update with the same aspath length, it will pass EQ, LE, GE,
   * fail GT, LT, NE test
   * test update with no aspath attribute
   */
  auto bgpMatch1 =
      createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::AS_PATH_LEN);
  *bgpMatch1.as_path_len_filter()->at(0).compare_operator() =
      routing_policy::ComparisonOperator::EQ;
  *bgpMatch1.as_path_len_filter()->at(0).value() = 4;
  auto bgpMatch2 =
      createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::AS_PATH_LEN);
  *bgpMatch2.as_path_len_filter()->at(0).compare_operator() =
      routing_policy::ComparisonOperator::GT;
  *bgpMatch2.as_path_len_filter()->at(0).value() = 4;
  auto bgpMatch3 =
      createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::AS_PATH_LEN);
  *bgpMatch3.as_path_len_filter()->at(0).compare_operator() =
      routing_policy::ComparisonOperator::GE;
  *bgpMatch3.as_path_len_filter()->at(0).value() = 4;
  auto bgpMatch4 =
      createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::AS_PATH_LEN);
  *bgpMatch4.as_path_len_filter()->at(0).compare_operator() =
      routing_policy::ComparisonOperator::LT;
  *bgpMatch4.as_path_len_filter()->at(0).value() = 4;
  auto bgpMatch5 =
      createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::AS_PATH_LEN);
  *bgpMatch5.as_path_len_filter()->at(0).compare_operator() =
      routing_policy::ComparisonOperator::LE;
  *bgpMatch5.as_path_len_filter()->at(0).value() = 4;
  auto bgpMatch6 =
      createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::AS_PATH_LEN);
  *bgpMatch6.as_path_len_filter()->at(0).compare_operator() =
      routing_policy::ComparisonOperator::NE;
  *bgpMatch6.as_path_len_filter()->at(0).value() = 4;

  const string policyName1 = "Policy Statement 1";
  const auto& bgpPolicies = createBgpPolicies(
      policyName1,
      {bgpMatch1, bgpMatch2, bgpMatch3, bgpMatch4, bgpMatch5, bgpMatch6});
  PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
  auto policy1 = policyManager.getPolicyFromName(policyName1);
  auto bgpTerms = policy1->getPolicyTerms();
  const auto& asPathLenMatchEQ = bgpTerms[0]->getPolicyAttributeMatches()[0];
  const auto& asPathLenMatchGT = bgpTerms[0]->getPolicyAttributeMatches()[1];
  const auto& asPathLenMatchGE = bgpTerms[0]->getPolicyAttributeMatches()[2];
  const auto& asPathLenMatchLT = bgpTerms[0]->getPolicyAttributeMatches()[3];
  const auto& asPathLenMatchLE = bgpTerms[0]->getPolicyAttributeMatches()[4];
  const auto& asPathLenMatchNE = bgpTerms[0]->getPolicyAttributeMatches()[5];

  // set as path with 4 segment
  const auto& attrsFields = buildBgpPathFields(4, 1, 0, 0);
  const auto& attributes = createBgpPath({}, {}, attrsFields);
  // test positive case for EQ
  EXPECT_TRUE(asPathLenMatchEQ->Match(attributes));
  // test negative case for GT
  EXPECT_FALSE(asPathLenMatchGT->Match(attributes));
  // test positive case for GE
  EXPECT_TRUE(asPathLenMatchGE->Match(attributes));
  // test negative case for LT
  EXPECT_FALSE(asPathLenMatchLT->Match(attributes));
  // test positive case for LE
  EXPECT_TRUE(asPathLenMatchLE->Match(attributes));
  // test negative case for NE
  EXPECT_FALSE(asPathLenMatchNE->Match(attributes));

  const auto& attributes2 = createBgpPath();
  // test negative case for EQ
  EXPECT_FALSE(asPathLenMatchEQ->Match(attributes2));
  // test negative case for GT
  EXPECT_FALSE(asPathLenMatchGT->Match(attributes2));
  // test negative case for GE
  EXPECT_FALSE(asPathLenMatchGE->Match(attributes2));
  // test positive case for LT
  EXPECT_TRUE(asPathLenMatchLT->Match(attributes2));
  // test positive case for LE
  EXPECT_TRUE(asPathLenMatchLE->Match(attributes2));
  // test positive case for NE
  EXPECT_TRUE(asPathLenMatchNE->Match(attributes2));
}

TEST_F(PolicyTest, AsPathLenWithConfedMatchTest) {
  /*
   * set three AsConfedPathLenMatch match struct, with same count but different
   * comparison metrics: EQ, GT, GE, LT, LE, NE.
   * Test update with the same aspath length, it will pass EQ, LE, GE,
   * fail GT, LT, NE test
   * test update with no aspath attribute
   */
  auto bgpMatch1 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::AS_PATH_LEN_WITH_CONFED);
  *(*bgpMatch1.as_path_len_with_confed_filter())[0].compare_operator() =
      routing_policy::ComparisonOperator::EQ;
  *(*bgpMatch1.as_path_len_with_confed_filter())[0].value() = 4;
  auto bgpMatch2 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::AS_PATH_LEN_WITH_CONFED);
  *(*bgpMatch2.as_path_len_with_confed_filter())[0].compare_operator() =
      routing_policy::ComparisonOperator::GT;
  *(*bgpMatch2.as_path_len_with_confed_filter())[0].value() = 4;
  auto bgpMatch3 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::AS_PATH_LEN_WITH_CONFED);
  *(*bgpMatch3.as_path_len_with_confed_filter())[0].compare_operator() =
      routing_policy::ComparisonOperator::GE;
  *(*bgpMatch3.as_path_len_with_confed_filter())[0].value() = 4;
  auto bgpMatch4 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::AS_PATH_LEN_WITH_CONFED);
  *(*bgpMatch4.as_path_len_with_confed_filter())[0].compare_operator() =
      routing_policy::ComparisonOperator::LT;
  *(*bgpMatch4.as_path_len_with_confed_filter())[0].value() = 4;
  auto bgpMatch5 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::AS_PATH_LEN_WITH_CONFED);
  *(*bgpMatch5.as_path_len_with_confed_filter())[0].compare_operator() =
      routing_policy::ComparisonOperator::LE;
  *(*bgpMatch5.as_path_len_with_confed_filter())[0].value() = 4;
  auto bgpMatch6 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::AS_PATH_LEN_WITH_CONFED);
  *(*bgpMatch6.as_path_len_with_confed_filter())[0].compare_operator() =
      routing_policy::ComparisonOperator::NE;
  *(*bgpMatch6.as_path_len_with_confed_filter())[0].value() = 4;

  const string policyName1 = "Policy Statement 1";
  const auto& bgpPolicies = createBgpPolicies(
      policyName1,
      {bgpMatch1, bgpMatch2, bgpMatch3, bgpMatch4, bgpMatch5, bgpMatch6});
  PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
  auto policy1 = policyManager.getPolicyFromName(policyName1);
  auto bgpTerms = policy1->getPolicyTerms();
  const auto& asPathLenWithConfedMatchEQ =
      bgpTerms[0]->getPolicyAttributeMatches()[0];
  const auto& asPathLenWithConfedMatchGT =
      bgpTerms[0]->getPolicyAttributeMatches()[1];
  const auto& asPathLenWithConfedMatchGE =
      bgpTerms[0]->getPolicyAttributeMatches()[2];
  const auto& asPathLenWithConfedMatchLT =
      bgpTerms[0]->getPolicyAttributeMatches()[3];
  const auto& asPathLenWithConfedMatchLE =
      bgpTerms[0]->getPolicyAttributeMatches()[4];
  const auto& asPathLenWithConfedMatchNE =
      bgpTerms[0]->getPolicyAttributeMatches()[5];

  // set as path with 4 segment
  const auto& attrsFields = buildBgpPathFields(4, 1, 0, 0);
  const auto& attributes = createBgpPath({}, {}, attrsFields);
  // test positive case for EQ
  EXPECT_TRUE(asPathLenWithConfedMatchEQ->Match(attributes));
  // test negative case for GT
  EXPECT_FALSE(asPathLenWithConfedMatchGT->Match(attributes));
  // test positive case for GE
  EXPECT_TRUE(asPathLenWithConfedMatchGE->Match(attributes));
  // test negative case for LT
  EXPECT_FALSE(asPathLenWithConfedMatchLT->Match(attributes));
  // test positive case for LE
  EXPECT_TRUE(asPathLenWithConfedMatchLE->Match(attributes));
  // test negative case for NE
  EXPECT_FALSE(asPathLenWithConfedMatchNE->Match(attributes));

  const auto& attributes2 = createBgpPath();
  // test negative case for EQ
  EXPECT_FALSE(asPathLenWithConfedMatchEQ->Match(attributes2));
  // test negative case for GT
  EXPECT_FALSE(asPathLenWithConfedMatchGT->Match(attributes2));
  // test negative case for GE
  EXPECT_FALSE(asPathLenWithConfedMatchGE->Match(attributes2));
  // test positive case for LT
  EXPECT_TRUE(asPathLenWithConfedMatchLT->Match(attributes2));
  // test positive case for LE
  EXPECT_TRUE(asPathLenWithConfedMatchLE->Match(attributes2));
  // test positive case for NE
  EXPECT_TRUE(asPathLenWithConfedMatchNE->Match(attributes2));
}

/**
 * This test-case is to validate BgpPolicyAtomicMatchType of type ALWAYS
 */
TEST_F(PolicyTest, AlwaysMatchTest) {
  // Create 1st statement with Orign, next with Always and last with Origin
  auto match1 = createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::ORIGIN);
  match1.origin() = policyOriginTypes[0];

  auto match2 = createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::ALWAYS);

  auto match3 = createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::ORIGIN);
  match3.origin() = policyOriginTypes[2];

  const string policyName1 = "Policy Statement 1";
  const auto& bgpPolicies =
      createBgpPolicies(policyName1, {match1, match2, match3});
  PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
  auto policy1 = policyManager.getPolicyFromName(policyName1);
  auto bgpTerms = policy1->getPolicyTerms();

  const auto& attributes = createBgpPath({}, {}, nullptr, attrOriginTypes[2]);

  const auto& policyMatch1 = bgpTerms[0]->getPolicyAttributeMatches()[0];
  const auto& policyMatch2 = bgpTerms[0]->getPolicyAttributeMatches()[1];
  const auto& policyMatch3 = bgpTerms[0]->getPolicyAttributeMatches()[2];

  EXPECT_FALSE(policyMatch1->Match(attributes));
  EXPECT_TRUE(policyMatch2->Match(attributes));
  EXPECT_TRUE(policyMatch3->Match(attributes));
}

TEST_F(PolicyTest, OriginMatchTest) {
  /*
   * set three OriginMatch match struct, with IGP, EGP, INCOMPLETE.
   * Test update with the EGP, it will fail IGP, INCOMPLETE matches,
   * pass EGP match
   * In createBgpPolicyAtomicMatch, default value is IGP
   */

  // For each of 3 origin types
  for (int i = 0; i < 3; i++) {
    auto match = createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::ORIGIN);
    match.origin() = policyOriginTypes[i];

    const string policyName = "Policy Statement 1";
    const auto& bgpPolicies = createBgpPolicies(policyName, {match});
    PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
    const auto& policy = policyManager.getPolicyFromName(policyName);
    const auto& bgpTerms = policy->getPolicyTerms();
    const auto& policyMatch = bgpTerms[0]->getPolicyAttributeMatches()[0];

    for (int j = 0; j < 3; j++) {
      const auto& attributes =
          createBgpPath({}, {}, nullptr, attrOriginTypes[j]);
      if (j == i) {
        EXPECT_TRUE(policyMatch->Match(attributes));
      } else {
        EXPECT_FALSE(policyMatch->Match(attributes));
      }
    }
  }
}

TEST_F(PolicyTest, WeightMatchTest) {
  {
    // Check Equality Match
    auto match = createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::WEIGHT);
    auto weightValue = routing_policy::CompareNumericValue();
    weightValue.value() = kWeight;
    weightValue.compare_operator() = routing_policy::ComparisonOperator::EQ;
    match.weight() = weightValue;

    const string policyName = "Policy Statement 1";
    const auto& bgpPolicies = createBgpPolicies(policyName, {match});
    PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
    const auto& policy = policyManager.getPolicyFromName(policyName);
    const auto& bgpTerms = policy->getPolicyTerms();
    const auto& policyMatch = bgpTerms[0]->getPolicyAttributeMatches()[0];

    const auto& attributes = createBgpPathWithWeight(kWeight2);
    EXPECT_FALSE(policyMatch->Match(attributes));

    const auto& attributes2 = createBgpPathWithWeight(kWeight);
    EXPECT_TRUE(policyMatch->Match(attributes2));
  }
  {
    // Check Greater Than or Equal to Match
    auto match = createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::WEIGHT);
    auto weightValue = routing_policy::CompareNumericValue();
    weightValue.value() = kWeight;
    weightValue.compare_operator() = routing_policy::ComparisonOperator::GE;
    match.weight() = weightValue;

    const string policyName = "Policy Statement 1";
    const auto& bgpPolicies = createBgpPolicies(policyName, {match});
    PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
    const auto& policy = policyManager.getPolicyFromName(policyName);
    const auto& bgpTerms = policy->getPolicyTerms();
    const auto& policyMatch = bgpTerms[0]->getPolicyAttributeMatches()[0];

    const auto& attributes = createBgpPathWithWeight(kWeight + 1);
    EXPECT_TRUE(policyMatch->Match(attributes));

    const auto& attributes2 = createBgpPathWithWeight(kWeight);
    EXPECT_TRUE(policyMatch->Match(attributes2));

    const auto& attributes3 = createBgpPathWithWeight(kWeight - 1);
    EXPECT_FALSE(policyMatch->Match(attributes3));
  }
  {
    // Check Less Than or Equal to Match
    auto match = createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::WEIGHT);
    auto weightValue = routing_policy::CompareNumericValue();
    weightValue.value() = kWeight;
    weightValue.compare_operator() = routing_policy::ComparisonOperator::LE;
    match.weight() = weightValue;

    const string policyName = "Policy Statement 1";
    const auto& bgpPolicies = createBgpPolicies(policyName, {match});
    PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
    const auto& policy = policyManager.getPolicyFromName(policyName);
    const auto& bgpTerms = policy->getPolicyTerms();
    const auto& policyMatch = bgpTerms[0]->getPolicyAttributeMatches()[0];

    const auto& attributes = createBgpPathWithWeight(kWeight + 1);
    EXPECT_FALSE(policyMatch->Match(attributes));

    const auto& attributes2 = createBgpPathWithWeight(kWeight);
    EXPECT_TRUE(policyMatch->Match(attributes2));

    const auto& attributes3 = createBgpPathWithWeight(kWeight - 1);
    EXPECT_TRUE(policyMatch->Match(attributes3));
  }
  {
    // Check Not Equal to Match
    auto match = createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::WEIGHT);
    auto weightValue = routing_policy::CompareNumericValue();
    weightValue.value() = kWeight;
    weightValue.compare_operator() = routing_policy::ComparisonOperator::LE;
    match.weight() = weightValue;

    const string policyName = "Policy Statement 1";
    const auto& bgpPolicies = createBgpPolicies(policyName, {match});
    PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
    const auto& policy = policyManager.getPolicyFromName(policyName);
    const auto& bgpTerms = policy->getPolicyTerms();
    const auto& policyMatch = bgpTerms[0]->getPolicyAttributeMatches()[0];

    const auto& attributes = createBgpPathWithWeight(kWeight + 1);
    EXPECT_FALSE(policyMatch->Match(attributes));

    const auto& attributes2 = createBgpPathWithWeight(kWeight);
    EXPECT_TRUE(policyMatch->Match(attributes2));
  }
}

TEST_F(PolicyTest, WeightBoundaryConditionsMatchTest) {
  auto match = createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::WEIGHT);
  auto weightValue = routing_policy::CompareNumericValue();
  weightValue.value() = kWeightMax;
  weightValue.compare_operator() = routing_policy::ComparisonOperator::EQ;
  match.weight() = weightValue;

  const string policyName = "Policy Statement 1";
  const auto& bgpPolicies = createBgpPolicies(policyName, {match});
  PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
  const auto& policy = policyManager.getPolicyFromName(policyName);
  const auto& bgpTerms = policy->getPolicyTerms();
  const auto& policyMatch = bgpTerms[0]->getPolicyAttributeMatches()[0];

  const auto& attributes2 = createBgpPathWithWeight(kWeightMax);
  EXPECT_TRUE(policyMatch->Match(attributes2));
}

TEST_F(PolicyTest, OriginActionTest) {
  // For each of 3 origin types
  for (int i = 0; i < 3; i++) {
    // Create a policy to match all and set action to a type
    auto bgpAction = createBgpPolicyAction(
        BgpPolicyActionType::ORIGIN, {}, "", policyOriginTypes[i]);

    const string policyName = "Policy Statement";
    const auto& policyConfig = createBgpPolicies(policyName, {}, {bgpAction});
    PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());
    const auto& policy = policyManager.getPolicyFromName(policyName);
    const auto& terms = policy->getPolicyTerms();
    const auto& action = terms[0]->getPolicyActions()[0];

    auto attrFields = buildBgpPathFields(0, 0, 0, 0);
    auto mutableAttrs = attrFields->attrs.get();
    // Set origin to a different value than expected
    mutableAttrs.origin = attrOriginTypes[(i + 1) % 3];
    attrFields->attrs = std::move(mutableAttrs);
    auto attrs = std::make_shared<BgpPath>(*attrFields);
    // Verify that before applying, origin is not set to expected value
    EXPECT_NE(attrs->getOrigin(), attrOriginTypes[i]);
    action->applyAction(attrs);
    // Verify that after applying origin is set to expected value
    EXPECT_EQ(attrs->getOrigin(), attrOriginTypes[i]);
  }
}

TEST_F(PolicyTest, NexthopActionTest) {
  std::array<folly::IPAddress, 2> nexthops = {kV4Nexthop2, kV6Nexthop2};

  for (const auto& nexthop : nexthops) {
    auto bgpAction = createBgpPolicyNexthopAction(nexthop);
    const string policyName = "Policy Statement";
    const auto& policyConfig = createBgpPolicies(policyName, {}, {bgpAction});
    PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());
    const auto& policy = policyManager.getPolicyFromName(policyName);
    const auto& terms = policy->getPolicyTerms();
    const auto& action = terms[0]->getPolicyActions()[0];

    auto attrFields = buildBgpPathFields(0, 0, 0, 0);
    auto attrs = std::make_shared<BgpPath>(*attrFields);
    // Verify that before applying, nexthop is not set to expected value
    EXPECT_NE(attrs->getNexthop(), nexthop);
    action->applyAction(attrs);
    // Verify that after applying nexthop is set to expected value
    EXPECT_EQ(attrs->getNexthop(), nexthop);
  }
}

TEST_F(PolicyTest, AddBackupAddrActionTest) {
  const auto backupAddr = folly::IPAddress("2001:db8::1");
  bgp_policy::BgpPolicyAction bgpAction;
  bgpAction.type() = BgpPolicyActionType::ADD_BACKUP_ADDR;
  bgp_policy::AddBackupAddr addBackupAddr;
  addBackupAddr.address() = backupAddr.str();
  bgpAction.add_backup_addr() = addBackupAddr;

  const std::string policyName = "Policy Statement";
  const auto policyConfig = createBgpPolicies(policyName, {}, {bgpAction});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());
  const auto& action = policyManager.getPolicyFromName(policyName)
                           ->getPolicyTerms()[0]
                           ->getPolicyActions()[0];

  auto original = std::make_shared<BgpPath>(*buildBgpPathFields(0, 0, 0, 0));
  auto attrs = original->clone();
  action->applyAction(attrs);

  EXPECT_EQ(backupAddr, attrs->getBackupAddr());
  EXPECT_NE(*original, *attrs);
  EXPECT_NE(original->hash(), attrs->hash());
}

TEST_F(PolicyTest, AddBackupAddrActionRejectsInvalidAddressTest) {
  auto makeAction = [](const std::string& address) {
    bgp_policy::BgpPolicyAction action;
    action.type() = BgpPolicyActionType::ADD_BACKUP_ADDR;
    bgp_policy::AddBackupAddr addBackupAddr;
    addBackupAddr.address() = address;
    action.add_backup_addr() = addBackupAddr;
    return action;
  };

  bgp_policy::BgpPolicyAction missingAddressAction;
  missingAddressAction.type() = BgpPolicyActionType::ADD_BACKUP_ADDR;
  missingAddressAction.add_backup_addr() = bgp_policy::AddBackupAddr();
  auto missingAddressError = parseActionConfigGetError(missingAddressAction);
  ASSERT_TRUE(missingAddressError);
  EXPECT_EQ(
      "Malformed add_backup_addr config. address missing",
      *missingAddressError);

  auto malformedError = parseActionConfigGetError(makeAction("not-an-ip"));
  ASSERT_TRUE(malformedError);
  EXPECT_EQ("Malformed backup address: not-an-ip", *malformedError);

  auto ipv4Error = parseActionConfigGetError(makeAction("192.0.2.1"));
  ASSERT_TRUE(ipv4Error);
  EXPECT_EQ("Backup address must be IPv6: 192.0.2.1", *ipv4Error);
}

// Test we can overwrite as path overwrite using correct input
TEST_F(PolicyTest, AsPathOverwriteTest) {
  std::vector<std::vector<int64_t>> asPathOverwriteLists = {
      {}, {0, 1, 2, 3, 5}};

  for (const auto& asPathOverwriteList : asPathOverwriteLists) {
    auto bgpAction = createBgpPolicyAsPathOverwriteAction(asPathOverwriteList);
    const string policyName = "Policy Statement";
    const auto& policyConfig = createBgpPolicies(policyName, {}, {bgpAction});
    PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());
    const auto& policy = policyManager.getPolicyFromName(policyName);
    const auto& terms = policy->getPolicyTerms();
    const auto& action = terms[0]->getPolicyActions()[0];

    auto attrFields = buildBgpPathFields(4 /* as sequence length */, 0, 0, 0);
    auto attrs = std::make_shared<BgpPath>(*attrFields);
    // Verify that before applying, as sequence is not set to expected value
    EXPECT_EQ(attrs->getAsPath()->at(0).asSequence.size(), 4);
    action->applyAction(attrs);
    // Verify that after applying as sequence is set to expected value
    if (asPathOverwriteList.empty()) {
      EXPECT_EQ(attrs->getAsPath().nullOrEmpty(), true);
    } else {
      EXPECT_EQ(
          attrs->getAsPath()->at(0).asSequence.size(),
          asPathOverwriteList.size());
      for (int i = 0; i < asPathOverwriteList.size(); i++) {
        EXPECT_EQ(
            attrs->getAsPath()->at(0).asSequence.at(i),
            asPathOverwriteList.at(i));
      }
    }
  }
}

// Test SetAsPath action will fail when passed in wrong inputs
TEST_F(PolicyTest, AsPathOverwriteNegativeTest) {
  // Prepare correct AsPath overwrite action
  auto correctAction = createBgpPolicyAsPathOverwriteAction({});
  correctAction.type() = BgpPolicyActionType::AS_PATH;

  // Verify we do not take negative asn numbers
  {
    auto incorrectAction = correctAction;
    incorrectAction.as_path_overwrite_list() = {-1};
    const auto& expectedStr = "Malformed SetAsPath config: asn = -1";
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }

  // Verify we do not take asn numbers greater than UINT32_MAX
  {
    auto incorrectAction = correctAction;
    incorrectAction.as_path_overwrite_list() = {int64_t(UINT32_MAX) + 1};
    const auto& expectedStr = "Malformed SetAsPath config: asn = 4294967296";
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
}

TEST_F(PolicyTest, NexthopNegativeTest) {
  // Prepare correct nexthop action
  bgp_policy::BgpPolicyAction correctAction;
  *correctAction.type() = BgpPolicyActionType::NEXT_HOP;
  routing_policy::NextHop tPolicyNexthopCorrect;
  tPolicyNexthopCorrect.next_hop_prefix() = "10.10.10.10";
  bgp_policy::SetNextHop tBgpNexthopCorrect;
  tBgpNexthopCorrect.next_hop() = tPolicyNexthopCorrect;
  correctAction.set_nexthop() = tBgpNexthopCorrect;

  {
    // Verify empty nexthop is not accepted
    auto incorrectAction = correctAction;
    incorrectAction.set_nexthop().reset();
    const auto& expectedStr = "BgpPolicyAction Config input error for type: 8";
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
  {
    // Verify empty policy nexthop is not accepted
    auto incorrectAction = correctAction;
    incorrectAction.set_nexthop()->next_hop().reset();
    const auto& expectedStr = "Malformed nexthop config. next_hop missing";
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
  {
    // Verify nexthop self is not supported
    auto incorrectAction = correctAction;
    *incorrectAction.set_nexthop()->set_self() = true;
    const auto& expectedStr = "Unsupported nexthop config. set_self";
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
  {
    // Verify nexthop interface is not supported
    auto incorrectAction = correctAction;
    incorrectAction.set_nexthop()->next_hop()->next_hop_interface() =
        "fboss4001";
    const auto& expectedStr = "Unsupported nexthop config. next_hop_interface";
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
  {
    // Verify if nexthop is not set, config is not accepted
    auto incorrectAction = correctAction;
    incorrectAction.set_nexthop()->next_hop()->next_hop_prefix().reset();
    const auto& expectedStr =
        "Malformed nexthop config. next_hop_prefix missing";
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
  {
    // Verify malformed nexthop is not accepted
    auto incorrectAction = correctAction;
    incorrectAction.set_nexthop()->next_hop()->next_hop_prefix() = "10..10.10";
    const auto& expectedStr = "Malformed nexthop config: 10..10.10";
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
}

TEST_F(PolicyTest, MedActionTest) {
  auto bgpAction = createBgpPolicyMedAction(kMed2);

  const string policyName = "Policy Statement";
  const auto& policyConfig = createBgpPolicies(policyName, {}, {bgpAction});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());
  const auto& policy = policyManager.getPolicyFromName(policyName);
  const auto& terms = policy->getPolicyTerms();
  const auto& action = terms[0]->getPolicyActions()[0];

  auto attrFields = buildBgpPathFields(0, 0, 0, 0);
  auto attrs = std::make_shared<BgpPath>(*attrFields);
  // Verify that before applying, MED is not set to expected value
  EXPECT_NE(attrs->getMed(), kMed2);
  auto actionData = std::make_shared<BgpPolicyActionData>();
  action->applyAction(attrs, actionData);
  // Verify that after applying med is set to expected value
  EXPECT_EQ(attrs->getMed(), kMed2);
  EXPECT_TRUE(actionData->isMedSetByPolicy);
}

TEST_F(PolicyTest, MedNegativeTest) {
  // Prepare correct MED action
  bgp_policy::BgpPolicyAction correctAction;
  *correctAction.type() = BgpPolicyActionType::MED;
  bgp_policy::MedAction medAction;
  *medAction.med_action_type() = bgp_policy::MedActionType::SET;
  *medAction.med_value() = kMed;
  correctAction.med_action() = medAction;

  {
    // Verify we do not accept med action type if med_action is not set
    auto incorrectAction = correctAction;
    incorrectAction.med_action().reset();
    const auto& expectedStr = "BgpPolicyAction Config input error for type: 10";
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
  {
    // Verify we do not support med update pattern
    auto incorrectAction = correctAction;
    incorrectAction.med_action()->update_pattern() = "+10";
    const auto& expectedStr = "Unsupported MED config. update_pattern";
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
  {
    // Verify we do not support med type IGP or UPDATE
    auto incorrectAction = correctAction;
    *incorrectAction.med_action()->med_action_type() =
        bgp_policy::MedActionType::IGP;
    auto receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(
        "Unsupported MED config. med_action_type: IGP", receivedStr->c_str());

    *incorrectAction.med_action()->med_action_type() =
        bgp_policy::MedActionType::UPDATE;
    receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(
        "Unsupported MED config. med_action_type: UPDATE",
        receivedStr->c_str());
  }
  {
    // Verify we do not accept invalid med values
    auto incorrectAction = correctAction;
    *incorrectAction.med_action()->med_value() = -1;
    const auto& expectedStr = "Malformed MED config: -1";
    auto receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
  {
    // Verify we do not accept invalid med values
    auto incorrectAction = correctAction;
    *incorrectAction.med_action()->med_value() = (int64_t)UINT32_MAX + 10;
    const auto& expectedStr = "Malformed MED config: 4294967305";
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
}

TEST_F(PolicyTest, WeightActionTest) {
  auto bgpAction = createBgpPolicyWeightAction(kWeight2);

  const string policyName = "Policy Statement";
  const auto& policyConfig = createBgpPolicies(policyName, {}, {bgpAction});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());
  const auto& policy = policyManager.getPolicyFromName(policyName);
  const auto& terms = policy->getPolicyTerms();
  const auto& action = terms[0]->getPolicyActions()[0];

  auto attrFields = buildBgpPathFields(0, 0, 0, 0);
  auto attrs = std::make_shared<BgpPath>(*attrFields);
  // Verify that before applying, Weight is not set to expected value
  EXPECT_NE(attrs->getWeight(), kWeight2);
  auto actionData = std::make_shared<BgpPolicyActionData>();
  action->applyAction(attrs, actionData);
  // Verify that after applying Weight is set to expected value
  EXPECT_EQ(attrs->getWeight(), kWeight2);
}

TEST_F(PolicyTest, WeightNegativeTest) {
  // Prepare correct Weight action
  bgp_policy::BgpPolicyAction correctAction;
  *correctAction.type() = BgpPolicyActionType::WEIGHT;
  bgp_policy::WeightAction weightAction;
  *weightAction.weight_action_type() = bgp_policy::WeightActionType::SET;
  *weightAction.weight_value() = kWeight;
  correctAction.weight_action() = weightAction;

  {
    // Verify we do not accept Weight action type if weight_action is not set
    auto incorrectAction = correctAction;
    incorrectAction.weight_action().reset();
    const auto& expectedStr = "BgpPolicyAction Config input error for type: 15";
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
  {
    // Verify we do not accept invalid weight values
    auto incorrectAction = correctAction;
    *incorrectAction.weight_action()->weight_value() = -1;
    const auto& expectedStr = "Malformed Weight config: -1";
    auto receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
  {
    // Verify we do not accept invalid weight values
    auto incorrectAction = correctAction;
    *incorrectAction.weight_action()->weight_value() = (int32_t)UINT16_MAX + 10;
    const auto& expectedStr = "Malformed Weight config: 65545";
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
}

TEST_F(PolicyTest, PrefixListMatchTest) {
  /*
   * test the implementation of prefixlist match:
   * prefixlist with EQ compare_operator: match and not match
   * prefixlist with LT compare_operator: match and not match
   * prefixlist with GT compare_operator: match and not match
   * prefixlist with LE compare_operator: match and not match
   * prefixlist with GE compare_operator: match and not match
   * prefixlist with NE compare_operator: match and not match
   * prefixlist with GT and LT compare_operator, i.e. p1<p0<p2
   * prefixlist with GE and LE compare_operator, i.e. p1<=p0<=p2
   * prefixlist with short form regex, match and not match
   * prefixlist with full form regex, match and not match
   */
  struct CompareStruct {
    explicit CompareStruct(
        int32_t compValue,
        routing_policy::ComparisonOperator compOpr)
        : compValue(compValue), compOpr(compOpr) {}
    explicit CompareStruct(const std::string& regex) : regex(regex) {}

    int32_t compValue;
    routing_policy::ComparisonOperator compOpr;
    std::optional<std::string> regex{std::nullopt};
  };
  struct PrefixTestData {
    PrefixTestData(
        std::vector<CompareStruct> compStructs,
        folly::CIDRNetwork prefixEntryPrefix,
        folly::CIDRNetwork prefixInput,
        bool expectedResult)
        : compStructs(compStructs),
          prefixEntryPrefix(prefixEntryPrefix),
          prefixInput(prefixInput),
          expectedResult(expectedResult),
          hasRegex(compStructs.size() == 1 && compStructs[0].regex) {}

    routing_policy::PrefixListEntry makePrefixListEntry() const {
      if (hasRegex) {
        return createPrefixListEntry(
            IPAddress::networkToString(prefixEntryPrefix),
            *compStructs[0].regex);
      } else {
        std::vector<routing_policy::CompareNumericValue> compareNumericValues;
        for (const auto& compareStruct : compStructs) {
          routing_policy::CompareNumericValue compareNumVal;
          compareNumVal.compare_operator() = compareStruct.compOpr;
          compareNumVal.value() = compareStruct.compValue;
          compareNumericValues.emplace_back(compareNumVal);
        }
        return createPrefixListEntry(
            IPAddress::networkToString(prefixEntryPrefix),
            compareNumericValues);
      }
    }

    const std::vector<CompareStruct> compStructs;
    const folly::CIDRNetwork prefixEntryPrefix;
    const folly::CIDRNetwork prefixInput;
    const bool expectedResult;
    const bool hasRegex{false};
  };
  const vector<PrefixTestData> dataPoints({
      // test data for IPv4 prefixes
      {{CompareStruct(24, routing_policy::ComparisonOperator::EQ)},
       kV4Prefix1Base,
       kV4Prefix1,
       true},
      {{CompareStruct(24, routing_policy::ComparisonOperator::EQ)},
       kV4Prefix1Base,
       kV4Prefix1Slash25,
       false},
      {{CompareStruct(24, routing_policy::ComparisonOperator::LE)},
       kV4Prefix1Base,
       kV4Prefix1Slash23,
       true},
      {{CompareStruct(24, routing_policy::ComparisonOperator::LE)},
       kV4Prefix1Base,
       kV4Prefix1Slash25,
       false},
      {{CompareStruct(24, routing_policy::ComparisonOperator::LE)},
       kV4Prefix1Base,
       kV4Prefix1,
       true},
      {{CompareStruct(24, routing_policy::ComparisonOperator::LT)},
       kV4Prefix1Base,
       kV4Prefix1Slash23,
       true},
      {{CompareStruct(24, routing_policy::ComparisonOperator::LT)},
       kV4Prefix1Base,
       kV4Prefix1Slash25,
       false},
      {{CompareStruct(24, routing_policy::ComparisonOperator::LT)},
       kV4Prefix1Base,
       kV4Prefix1,
       false},
      {{CompareStruct(24, routing_policy::ComparisonOperator::GT)},
       kV4Prefix1Base,
       kV4Prefix1Slash23,
       false},
      {{CompareStruct(24, routing_policy::ComparisonOperator::GT)},
       kV4Prefix1Base,
       kV4Prefix1Slash25,
       true},
      {{CompareStruct(24, routing_policy::ComparisonOperator::GT)},
       kV4Prefix1Base,
       kV4Prefix1,
       false},
      {{CompareStruct(24, routing_policy::ComparisonOperator::GE)},
       kV4Prefix1Base,
       kV4Prefix1Slash23,
       false},
      {{CompareStruct(24, routing_policy::ComparisonOperator::GE)},
       kV4Prefix1Base,
       kV4Prefix1Slash25,
       true},
      {{CompareStruct(24, routing_policy::ComparisonOperator::GE)},
       kV4Prefix1Base,
       kV4Prefix1,
       true},
      {{CompareStruct(24, routing_policy::ComparisonOperator::NE)},
       kV4Prefix1Base,
       kV4Prefix1Slash23,
       true},
      {{CompareStruct(24, routing_policy::ComparisonOperator::NE)},
       kV4Prefix1Base,
       kV4Prefix1,
       false},
      // test a case base prefix is more specific than prefix
      {{CompareStruct(24, routing_policy::ComparisonOperator::LE)},
       kV4Prefix1,
       kV4Prefix1Slash23,
       false},
      // base prefix is 0.0.0.0/0, so only prefix length matters
      {{CompareStruct(24, routing_policy::ComparisonOperator::LE)},
       kV4PrefixZero,
       kV4Prefix1Slash23,
       true},
      {{CompareStruct(24, routing_policy::ComparisonOperator::GT)},
       kV4PrefixZero,
       kV4Prefix1Slash23,
       false},
      /*
       * Verify that IPv4 prefix length (0.0.0.0/0) filter will not match ipv6
       * addresses
       */
      {{CompareStruct(24, routing_policy::ComparisonOperator::GT)},
       kV4PrefixZero,
       kV6Prefix1Slash63,
       false},
      {{CompareStruct(24, routing_policy::ComparisonOperator::GT),
        CompareStruct(26, routing_policy::ComparisonOperator::LT)},
       kV4Prefix1Base,
       kV4Prefix1Slash23,
       false},
      {{CompareStruct(24, routing_policy::ComparisonOperator::GT),
        CompareStruct(26, routing_policy::ComparisonOperator::LT)},
       kV4Prefix1Base,
       kV4Prefix1Slash25,
       true},
      {{CompareStruct(24, routing_policy::ComparisonOperator::GT),
        CompareStruct(26, routing_policy::ComparisonOperator::LT)},
       kV4Prefix1Base,
       kV4Prefix1Slash27,
       false},
      {{CompareStruct(24, routing_policy::ComparisonOperator::GT),
        CompareStruct(26, routing_policy::ComparisonOperator::LT)},
       kV4Prefix1Base,
       kV4Prefix1,
       false},
      {{CompareStruct(24, routing_policy::ComparisonOperator::GT),
        CompareStruct(26, routing_policy::ComparisonOperator::LT)},
       kV4Prefix1Base,
       kV4Prefix1Slash26,
       false},
      // test data for IPv6 prefixes
      {{CompareStruct(64, routing_policy::ComparisonOperator::EQ)},
       kV6Prefix1Base,
       kV6Prefix1,
       true},
      {{CompareStruct(64, routing_policy::ComparisonOperator::EQ)},
       kV6Prefix1Base,
       kV6Prefix1Slash65,
       false},
      {{CompareStruct(64, routing_policy::ComparisonOperator::LE)},
       kV6Prefix1Base,
       kV6Prefix1Slash63,
       true},
      {{CompareStruct(64, routing_policy::ComparisonOperator::LE)},
       kV6Prefix1Base,
       kV6Prefix1Slash65,
       false},
      {{CompareStruct(64, routing_policy::ComparisonOperator::LE)},
       kV6Prefix1Base,
       kV6Prefix1,
       true},
      {{CompareStruct(64, routing_policy::ComparisonOperator::LT)},
       kV6Prefix1Base,
       kV6Prefix1Slash63,
       true},
      {{CompareStruct(64, routing_policy::ComparisonOperator::LT)},
       kV6Prefix1Base,
       kV6Prefix1Slash65,
       false},
      {{CompareStruct(64, routing_policy::ComparisonOperator::LT)},
       kV6Prefix1Base,
       kV6Prefix1,
       false},
      {{CompareStruct(64, routing_policy::ComparisonOperator::GT)},
       kV6Prefix1Base,
       kV6Prefix1Slash63,
       false},
      {{CompareStruct(64, routing_policy::ComparisonOperator::GT)},
       kV6Prefix1Base,
       kV6Prefix1Slash65,
       true},
      {{CompareStruct(64, routing_policy::ComparisonOperator::GT)},
       kV6Prefix1Base,
       kV6Prefix1,
       false},
      {{CompareStruct(64, routing_policy::ComparisonOperator::GE)},
       kV6Prefix1Base,
       kV6Prefix1Slash63,
       false},
      {{CompareStruct(64, routing_policy::ComparisonOperator::GE)},
       kV6Prefix1Base,
       kV6Prefix1Slash65,
       true},
      {{CompareStruct(64, routing_policy::ComparisonOperator::GE)},
       kV6Prefix1Base,
       kV6Prefix1,
       true},
      {{CompareStruct(64, routing_policy::ComparisonOperator::NE)},
       kV6Prefix1Base,
       kV6Prefix1Slash63,
       true},
      {{CompareStruct(64, routing_policy::ComparisonOperator::NE)},
       kV6Prefix1Base,
       kV6Prefix1,
       false},
      // test base prefix is more specific than prefix
      {{CompareStruct(64, routing_policy::ComparisonOperator::LE)},
       kV6Prefix1,
       kV6Prefix1Slash63,
       false},
      // base prefix is 0::/0, so only prefix length matters
      {{CompareStruct(64, routing_policy::ComparisonOperator::LE)},
       kV6PrefixZero,
       kV6Prefix1Slash63,
       true},
      // test base prefix is more specific than prefix
      {{CompareStruct(64, routing_policy::ComparisonOperator::GT)},
       kV6PrefixZero,
       kV6Prefix1Slash63,
       false},
      /*
       * Verify that IPv6 prefix length (::/0) filter will not match ipv4
       * addresses
       */
      {{CompareStruct(64, routing_policy::ComparisonOperator::LE)},
       kV6PrefixZero,
       kV4Prefix1Slash23,
       false},
      {{CompareStruct(64, routing_policy::ComparisonOperator::GT),
        CompareStruct(66, routing_policy::ComparisonOperator::LT)},
       kV6Prefix1Base,
       kV6Prefix1Slash63,
       false},
      {{CompareStruct(64, routing_policy::ComparisonOperator::GT),
        CompareStruct(66, routing_policy::ComparisonOperator::LT)},
       kV6Prefix1Base,
       kV6Prefix1Slash65,
       true},
      {{CompareStruct(64, routing_policy::ComparisonOperator::GT),
        CompareStruct(66, routing_policy::ComparisonOperator::LT)},
       kV6Prefix1Base,
       kV6Prefix1Slash67,
       false},
      {{CompareStruct(64, routing_policy::ComparisonOperator::GT),
        CompareStruct(66, routing_policy::ComparisonOperator::LT)},
       kV6Prefix1Base,
       kV6Prefix1,
       false},
      {{CompareStruct(64, routing_policy::ComparisonOperator::GT),
        CompareStruct(66, routing_policy::ComparisonOperator::LT)},
       kV6Prefix1Base,
       kV6Prefix1Slash66,
       false},
      // test corner cases
      {{CompareStruct(129, routing_policy::ComparisonOperator::LT)},
       kV6Prefix1Base,
       kV6Prefix1Slash65,
       true},
      {{CompareStruct(33, routing_policy::ComparisonOperator::LT)},
       kV4Prefix1Base,
       kV4Prefix1,
       true},
      // test regex cases
      {{CompareStruct("8.0.*/24")}, kV4Prefix1Base, kV4Prefix1, true},
      {{CompareStruct("8.0.*/32")}, kV4Prefix1Base, kV4Prefix1, false},
      {{CompareStruct("8.0.*/32")}, kV4Prefix1Base, kV6Prefix1, false},
      {{CompareStruct("2001:.*/64")}, kV6Prefix1Base, kV6Prefix1, true},
      {{CompareStruct("2001:.*/64")}, kV6Prefix1Base, kV4Prefix1, false},
      {{CompareStruct("2001:0000:1.*/64")}, kV6Prefix1Base, kV6Prefix1, false},
      {{CompareStruct("2001:0000:0000:.*/64")},
       kV6Prefix1Base,
       kV6Prefix1,
       true},
      {{CompareStruct("2001:0000:0001:.*/64")},
       kV6Prefix1Base,
       kV6Prefix1,
       false},
      {{CompareStruct("2401:db00:.*")},
       kV6Prefix2Base,
       kV6Prefix2Slash64,
       true},
      {{CompareStruct("2401:db00:01ff:.*")},
       kV6Prefix2Base,
       kV6Prefix2Slash64,
       true},
      {{CompareStruct("2401:db00:[0-4][a-f0-9]ff:.*")},
       kV6Prefix2Base,
       kV6Prefix2Slash64,
       true},
      {{CompareStruct("2401:db00:[0-4][a-f0-9]ff:.*")},
       kV6Prefix2Base,
       kV6Prefix2Slash128,
       true},
      {{CompareStruct("2401:db00:[0-4][a-f0-9]ff:8000:.*")},
       kV6Prefix2Base,
       kV6Prefix2Slash64,
       false},
      {{CompareStruct("2401:db00:[0-4][a-f0-9]ff:8000:.*")},
       kV6Prefix2Base,
       kV6Prefix2Slash128,
       false},
  });

  for (const auto& prefixTestData : dataPoints) {
    routing_policy::PrefixListEntry prefixListEntry =
        prefixTestData.makePrefixListEntry();
    const auto& bgpMatch1 = createPrefixListMatch({prefixListEntry});
    const string policyName1 = "Policy Statement 1";
    const auto& bgpPolicies = createBgpPolicies(policyName1, {bgpMatch1});
    PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
    const auto& policy1 = policyManager.getPolicyFromName(policyName1);
    const auto& bgpTerms = policy1->getPolicyTerms();
    const auto& bgpEQPrefixMatch = bgpTerms[0]->getPolicyPrefixMatches()[0];
    // test update matches the expected result
    EXPECT_EQ(
        bgpEQPrefixMatch->Match(prefixTestData.prefixInput),
        prefixTestData.expectedResult);
  }
}

TEST_F(PolicyTest, PrefixListEntryWithBothRegexAndLenRanges) {
  // create a prefix list entry with regex
  auto prefixListEntry = createPrefixListEntry("8.0.0.0/16", "8.0.*/24");
  // populate this entry with prefix len ranges (not supposed to)
  std::vector<routing_policy::CompareNumericValue> compareNumericValues;
  routing_policy::CompareNumericValue compareNumVal;
  compareNumVal.compare_operator() = routing_policy::ComparisonOperator::GT;
  compareNumVal.value() = 16;
  compareNumericValues.emplace_back(compareNumVal);
  prefixListEntry.prefix_len_ranges() = std::move(compareNumericValues);
  // create prefix list match
  auto bgpMatch = createPrefixListMatch({prefixListEntry});
  const string policyName = "Policy Statement 1";
  const auto bgpPolicies = createBgpPolicies(policyName, {bgpMatch});
  PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
  const auto policy = policyManager.getPolicyFromName(policyName);
  const auto bgpTerms = policy->getPolicyTerms();
  const auto bgpEQPrefixMatch = bgpTerms[0]->getPolicyPrefixMatches()[0];
  /*
   * we expect 8.0.0.0/17 to match because of prefix len ranges
   * because prefix len ranges supercededs regex
   * and this prefix wouldn't match the regex
   */
  EXPECT_TRUE(bgpEQPrefixMatch->Match(folly::CIDRNetwork("8.0.0.0", 17)));
}

TEST_F(PolicyTest, MultiplePrefixListMatchTest) {
  /*
   * Create a term with two matches
   * Each match has a prefix list
   * Verify cases where prefix matches
   * none, first match, second match, matches both
   */

  // Create a prefix list entry of 8.0.0.0/16 GT 24, LT 27
  routing_policy::CompareNumericValue comparer1;
  comparer1.value() = 24;
  comparer1.compare_operator() = routing_policy::ComparisonOperator::GT;
  routing_policy::CompareNumericValue comparer2;
  comparer2.value() = 27;
  comparer2.compare_operator() = routing_policy::ComparisonOperator::LT;
  std::vector<routing_policy::CompareNumericValue> compareNumericValues1;
  compareNumericValues1.push_back(std::move(comparer1));
  compareNumericValues1.push_back(std::move(comparer2));

  const auto& prefixListEntry1 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix1Base), compareNumericValues1);
  const auto& match1 = createPrefixListMatch({prefixListEntry1});

  // Create a prefix list entry of 8.0.0.0/16 GT 25, LT 28
  routing_policy::CompareNumericValue comparer3;
  comparer3.value() = 25;
  comparer3.compare_operator() = routing_policy::ComparisonOperator::GT;
  routing_policy::CompareNumericValue comparer4;
  comparer4.value() = 28;
  comparer4.compare_operator() = routing_policy::ComparisonOperator::LT;
  std::vector<routing_policy::CompareNumericValue> compareNumericValues2;
  compareNumericValues2.push_back(std::move(comparer3));
  compareNumericValues2.push_back(std::move(comparer4));

  const auto& prefixListEntry2 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix1Base), compareNumericValues2);

  const auto& match2 = createPrefixListMatch({prefixListEntry2});
  auto actionIgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::IGP);
  const string policyName = "Policy Statement";
  const auto& bgpPolicies =
      createBgpPolicies(policyName, {match1, match2}, {actionIgp});
  PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());

  const std::vector<folly::CIDRNetwork> prefixSetIn{
      kV4Prefix1Slash23,
      kV4Prefix1Slash25, // Matches match1
      kV4Prefix1Slash26, // Matches match1 and match2
      kV4Prefix1Slash27, // Matches match2
      kV4Prefix1Slash28};

  auto attrs = createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_EGP);
  attrs->setCommunities(createBgpAttrCommunitiesC({kCommunity1}));
  attrs->publish();

  // Apply policy
  PolicyInMessage policyIn(prefixSetIn, attrs);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  /*
   * Verify that only prefix which matches both prefix lists
   * (kV4Prefix1Slash26) is permitted
   */
  EXPECT_EQ(5, policyOut.result.size());
  EXPECT_NE(policyOut.result.find(kV4Prefix1Slash26), policyOut.result.end());
  EXPECT_NE(policyOut.result.find(kV4Prefix1Slash23), policyOut.result.end());
  EXPECT_NE(policyOut.result.find(kV4Prefix1Slash25), policyOut.result.end());
  EXPECT_NE(policyOut.result.find(kV4Prefix1Slash27), policyOut.result.end());
  EXPECT_NE(policyOut.result.find(kV4Prefix1Slash28), policyOut.result.end());

  auto attrsOut = policyOut.result[kV4Prefix1Slash26];
  // Verify attributes are unpublished
  EXPECT_FALSE(attrsOut->attrs->isPublished());
  // Verify origin action is properly applied
  EXPECT_EQ(attrsOut->attrs->getOrigin(), BgpAttrOrigin::BGP_ORIGIN_IGP);
}

TEST_F(PolicyTest, PrefixListNegativeTest) {
  auto lambdaVerifyPrefixListEntryParseError =
      [&](const routing_policy::PrefixListEntry& prefixListEntry,
          const std::string& expectedError) {
        const auto& bgpMatch1 = createPrefixListMatch({prefixListEntry});

        const string policyName1 = "Policy Statement 1";
        const auto& bgpPolicies = createBgpPolicies(policyName1, {bgpMatch1});
        try {
          PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
          ADD_FAILURE();
        } catch (const BgpError& error) {
          EXPECT_EQ(*error.message(), expectedError);
          return;
        }
      };

  auto lambdaVerifyPrefixListParseError =
      [&](const bgp_policy::BgpPolicyAtomicMatch& bgpMatch,
          const std::string& expectedError) {
        const string policyName1 = "Policy Statement 1";
        const auto& bgpPolicies = createBgpPolicies(policyName1, {bgpMatch});
        try {
          PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
          ADD_FAILURE();
        } catch (const BgpError& error) {
          EXPECT_EQ(*error.message(), expectedError);
          return;
        }
      };
  {
    // test empty prefixlist, expect to return false
    const auto& bgpMatch1 = createPrefixListMatch();
    const string policyName1 = "Policy Statement 1";
    const auto& bgpPolicies = createBgpPolicies(policyName1, {bgpMatch1});
    PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
    const auto& policy1 = policyManager.getPolicyFromName(policyName1);
    const auto& bgpTerms = policy1->getPolicyTerms();
    const auto& emptyPrefixMatch = bgpTerms[0]->getPolicyPrefixMatches()[0];
    const auto& prefix = folly::IPAddress::createNetwork("2::/64");
    EXPECT_FALSE(emptyPrefixMatch->Match(prefix));
  }
  {
    // test malformed prefix with invalid mask length for ipv4
    routing_policy::CompareNumericValue compareStructEQ;
    *compareStructEQ.compare_operator() =
        routing_policy::ComparisonOperator::EQ;
    *compareStructEQ.value() = 33;
    const auto& prefixListEntry =
        createPrefixListEntry("10.1.1.0/33", {compareStructEQ});
    lambdaVerifyPrefixListEntryParseError(
        prefixListEntry, "Malformed base prefix in config: 10.1.1.0/33");
  }
  {
    // test malformed prefix with invalid prefix base for ipv4
    routing_policy::CompareNumericValue compareStructEQ;
    *compareStructEQ.compare_operator() =
        routing_policy::ComparisonOperator::EQ;
    *compareStructEQ.value() = 24;
    // malformed random string for prefix
    const auto& prefixListEntry =
        createPrefixListEntry("1.#7&1/24", {compareStructEQ});
    lambdaVerifyPrefixListEntryParseError(
        prefixListEntry, "Malformed base prefix in config: 1.#7&1/24");
  }
  {
    // test malformed prefix with invalid mask length ipv6
    routing_policy::CompareNumericValue compareStructEQ;
    *compareStructEQ.compare_operator() =
        routing_policy::ComparisonOperator::EQ;
    *compareStructEQ.value() = 130;
    const auto& prefixListEntry =
        createPrefixListEntry("2001::/130", {compareStructEQ});
    lambdaVerifyPrefixListEntryParseError(
        prefixListEntry, "Malformed base prefix in config: 2001::/130");
  }
  {
    // test unsupported feature: sequence number
    auto prefixListEntry = createDefaultPrefixListEntry();
    prefixListEntry.seq_num() = 1;
    lambdaVerifyPrefixListEntryParseError(
        prefixListEntry, "Unsupported Prefix configuration: seq_num");
  }
  {
    // test unsupported feature: match value logic is not EQUAL
    auto prefixListEntry = createDefaultPrefixListEntry();
    *prefixListEntry.match_logic() =
        routing_policy::MatchValueLogicOperator::NOT_EQUAL;
    lambdaVerifyPrefixListEntryParseError(
        prefixListEntry, "Unsupported Prefix configuration: match_logic");
  }
  {
    // test unsupported feature: compare_operator in PrefixList
    const auto& prefixListEntry = createDefaultPrefixListEntry();
    auto bgpMatch = createPrefixListMatch({prefixListEntry});
    bgpMatch.prefix_filters()->compare_operator() =
        routing_policy::ComparisonOperator::EQ;
    lambdaVerifyPrefixListParseError(
        bgpMatch, "Unsupported PrefixList configuration: compare_operator");
  }
  {
    /*
     * test unsupported feature:  operators other than BooleanOperator.OR in
     * PrefixList
     */
    const auto& prefixListEntry = createDefaultPrefixListEntry();
    auto bgpMatch = createPrefixListMatch({prefixListEntry});
    *bgpMatch.prefix_filters()->boolean_operator() =
        routing_policy::BooleanOperator::AND;
    lambdaVerifyPrefixListParseError(
        bgpMatch, "PrefixList BooleanOperator can only be OR");
  }
  {
    // test base prefix in the form of ip without mask length
    routing_policy::CompareNumericValue compareStructEQ;
    *compareStructEQ.compare_operator() =
        routing_policy::ComparisonOperator::EQ;
    *compareStructEQ.value() = 32;
    const string policyName1 = "Policy Statement 1";
    const auto& prefixListEntry =
        createPrefixListEntry(kV4PrefixNoMaskStr, {compareStructEQ});
    const auto& bgpMatch1 = createPrefixListMatch({prefixListEntry});
    const auto& bgpPolicies = createBgpPolicies(policyName1, {bgpMatch1});
    PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
    const auto& policy1 = policyManager.getPolicyFromName(policyName1);
    const auto& bgpTerms = policy1->getPolicyTerms();
    const auto& bgpEQPrefixMatch = bgpTerms[0]->getPolicyPrefixMatches()[0];
    // it should match if the mask length is 32
    EXPECT_TRUE(bgpEQPrefixMatch->Match(kV4PrefixNoMask));
  }
}

TEST_F(PolicyTest, AsPathMatchTest) {
  BgpStats::initCounters();
  {
    /*
     * Set two match struct, with different logic operator (AND/OR)
     * Test update communities matches regex;
     * Test update communities do not match regex
     * kASPathRegex1 = "^65000.*", kASPathRegex2 = ".*65001$"
     */
    const auto& bgpMatch1 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::AS_PATH,
        {kASPathRegex1, kASPathRegex2},
        BooleanOperator::OR);
    const auto& bgpMatch2 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::AS_PATH,
        {kASPathRegex1, kASPathRegex2},
        BooleanOperator::AND);
    const string policyName1 = "Policy Statement 1";
    const auto& bgpPolicies =
        createBgpPolicies(policyName1, {bgpMatch1, bgpMatch2});
    PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
    const auto& policy1 = policyManager.getPolicyFromName(policyName1);
    const auto& bgpTerms = policy1->getPolicyTerms();

    const auto& asPathORMatch = bgpTerms[0]->getPolicyAttributeMatches()[0];
    const auto& asPathANDMatch = bgpTerms[0]->getPolicyAttributeMatches()[1];

    std::vector<BgpAttrAsPathSegmentC> asPath;
    BgpAttrAsPathSegmentC segment1;
    segment1.asSequence.push_back(65000);
    asPath.push_back(segment1);

    const auto& attributes1 = createBgpPath({}, asPath);

    EXPECT_TRUE(asPathORMatch->Match(attributes1));
    EXPECT_FALSE(asPathANDMatch->Match(attributes1));
    asPath.clear();
    // asPath is now 65000 65001
    segment1.asSequence.push_back(65001);
    asPath.push_back(segment1);
    const auto& attributes2 = createBgpPath({}, asPath);
    EXPECT_TRUE(asPathORMatch->Match(attributes2));
    EXPECT_TRUE(asPathANDMatch->Match(attributes2));
  }
  {
    // Test update with asSet
    const auto& bgpMatch1 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::AS_PATH,
        {kASPathSetRegex1, kASPathRegex2},
        BooleanOperator::OR);
    const auto& bgpMatch2 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::AS_PATH,
        {kASPathSetRegex1, kASPathRegex2},
        BooleanOperator::AND);
    const string policyName1 = "Policy Statement 1";
    const auto& bgpPolicies =
        createBgpPolicies(policyName1, {bgpMatch1, bgpMatch2});
    PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
    const auto& policy1 = policyManager.getPolicyFromName(policyName1);
    const auto& bgpTerms = policy1->getPolicyTerms();
    const auto& asPathORMatch = bgpTerms[0]->getPolicyAttributeMatches()[0];
    const auto& asPathANDMatch = bgpTerms[0]->getPolicyAttributeMatches()[1];

    std::vector<BgpAttrAsPathSegmentC> asPath;
    BgpAttrAsPathSegmentC segment1;
    segment1.asSet = {65000, 65002};
    asPath.push_back(segment1);

    const auto& attributes1 = createBgpPath({}, asPath);

    EXPECT_TRUE(asPathORMatch->Match(attributes1));
    EXPECT_FALSE(asPathANDMatch->Match(attributes1));
    // asPath is now {65000 65002} {65001}
    BgpAttrAsPathSegmentC segment2;
    segment2.asSet = {65001};
    asPath.push_back(segment2);
    const auto& attributes2 = createBgpPath({}, asPath);
    EXPECT_TRUE(asPathORMatch->Match(attributes2));
    EXPECT_FALSE(asPathANDMatch->Match(attributes2));

    // asPath is now {65000 65002} {65001, 65003}
    asPath.clear();
    segment2.asSet = {65001, 65003};
    asPath.push_back(segment1);
    asPath.push_back(segment2);
    const auto& attributes3 = createBgpPath({}, asPath);
    EXPECT_TRUE(asPathORMatch->Match(attributes3));
    EXPECT_FALSE(asPathANDMatch->Match(attributes3));
  }
  {
    /*
     * test with multiple as Segment
     * kASPathRegexMultiSeq = "^\\(2[0-9][0-9][0-9]\\)_65000_65000_65000$";
     */
    const auto& bgpMatch1 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::AS_PATH,
        {kASPathRegexMultiSeq},
        BooleanOperator::OR);
    const string policyName1 = "Policy Statement 1";
    const auto& bgpPolicies = createBgpPolicies(policyName1, {bgpMatch1});
    PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
    const auto& policy1 = policyManager.getPolicyFromName(policyName1);
    const auto& bgpTerms = policy1->getPolicyTerms();
    const auto& asPathORMatch = bgpTerms[0]->getPolicyAttributeMatches()[0];

    std::vector<BgpAttrAsPathSegmentC> asPath;
    BgpAttrAsPathSegmentC segment1;
    segment1.asConfedSequence = {2001};
    asPath.push_back(segment1);
    BgpAttrAsPathSegmentC segment2;
    segment2.asSequence = {65000, 65000, 65000};
    asPath.push_back(segment2);
    // asPath is (2001)_65000_65000_65000
    const auto& attributes1 = createBgpPath({}, asPath);
    EXPECT_TRUE(asPathORMatch->Match(attributes1));

    asPath.clear();
    asPath.push_back(segment1);
    segment2.asSequence = {65000, 65000};
    asPath.push_back(segment2);
    // asPath is (2001)_65000_65000
    const auto& attributes2 = createBgpPath({}, asPath);
    EXPECT_FALSE(asPathORMatch->Match(attributes2));
  }
  {
    // kASPathRegex5 = "65000", kASPathRegex6 = "65000_65000"
    const auto& bgpMatch1 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::AS_PATH,
        {kASPathRegex5},
        BooleanOperator::OR);
    const auto& bgpMatch2 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::AS_PATH,
        {kASPathRegex6},
        BooleanOperator::OR);
    const string policyName1 = "Policy Statement 1";
    const auto& bgpPolicies =
        createBgpPolicies(policyName1, {bgpMatch1, bgpMatch2});
    PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
    const auto& policy1 = policyManager.getPolicyFromName(policyName1);
    const auto& bgpTerms = policy1->getPolicyTerms();
    const auto& asPathMatch1 = bgpTerms[0]->getPolicyAttributeMatches()[0];
    const auto& asPathMatch2 = bgpTerms[0]->getPolicyAttributeMatches()[1];

    std::vector<BgpAttrAsPathSegmentC> asPath;
    BgpAttrAsPathSegmentC segment1;
    // asPath 65001 65000
    segment1.asSequence.push_back(65001);
    segment1.asSequence.push_back(65000);
    asPath.push_back(segment1);

    const auto& attributes1 = createBgpPath({}, asPath);

    EXPECT_TRUE(asPathMatch1->Match(attributes1));
    EXPECT_FALSE(asPathMatch2->Match(attributes1));
    asPath.clear();

    // asPath is now 65001 65000 65000
    segment1.asSequence.push_back(65000);
    asPath.push_back(segment1);
    const auto& attributes2 = createBgpPath({}, asPath);
    EXPECT_TRUE(asPathMatch1->Match(attributes2));
    EXPECT_TRUE(asPathMatch2->Match(attributes2));
  }
}

TEST_F(PolicyTest, AsPathMatchNegativeTest) {
  BgpStats::initCounters();
  /*
   * test three corner cases: empty aspath match
   * malformed aspath regex
   * attributes without aspath attributes
   */
  {
    // test AsPathMatch with empty regex, return false
    const auto& bgpMatch1 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::AS_PATH, {}, BooleanOperator::OR);
    const auto& bgpMatch2 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::AS_PATH, {}, BooleanOperator::AND);
    const string policyName1 = "Policy Statement 1";
    const auto& bgpPolicies =
        createBgpPolicies(policyName1, {bgpMatch1, bgpMatch2});
    PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
    const auto& policy1 = policyManager.getPolicyFromName(policyName1);
    const auto& bgpTerms = policy1->getPolicyTerms();
    const auto& asPathORMatch = bgpTerms[0]->getPolicyAttributeMatches()[0];
    const auto& asPathANDMatch = bgpTerms[0]->getPolicyAttributeMatches()[1];

    std::vector<BgpAttrAsPathSegmentC> asPath;
    BgpAttrAsPathSegmentC segment1;
    segment1.asSequence.push_back(65000);
    asPath.push_back(segment1);
    const auto& attributes1 = createBgpPath({}, asPath);
    EXPECT_FALSE(asPathORMatch->Match(attributes1));
    EXPECT_FALSE(asPathANDMatch->Match(attributes1));
  }
  {
    // test config with malformed aspath regex, raise exception
    const auto& bgpMatch1 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::AS_PATH, {"+a76!$3"}, BooleanOperator::OR);
    const string policyName1 = "Policy Statement 1";
    const auto& bgpPolicies = createBgpPolicies(policyName1, {bgpMatch1});
    try {
      PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
      ADD_FAILURE();
    } catch (const BgpError& error) {
      EXPECT_EQ(*error.message(), "Malformed regex in aspath config: +a76!$3");
    }
  }
  {
    /*
     * testing attributes without aspath attribute,
     * kASPathRegex3 = ".*"
     * return false if no regex under OR
     * return false if no regex under AND
     * return false if any ".*" regex and one is not ".*" under AND
     * return true if any ".*" regex  and one is not ".*" under OR
     * return true if only one ".*" regex under AND
     */

    const auto& bgpMatch1 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::AS_PATH, {}, BooleanOperator::OR);
    const auto& bgpMatch2 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::AS_PATH, {}, BooleanOperator::AND);
    const auto& bgpMatch3 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::AS_PATH,
        {kASPathRegex1, kASPathRegex3},
        BooleanOperator::AND);
    const auto& bgpMatch4 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::AS_PATH,
        {kASPathRegex1, kASPathRegex3},
        BooleanOperator::OR);
    const auto& bgpMatch5 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::AS_PATH,
        {kASPathRegex3},
        BooleanOperator::AND);
    // std::nullopt as_paths same behaviour as empty as_paths
    bgp_policy::BgpPolicyAtomicMatch bgpMatch6;
    *bgpMatch6.type() = BgpPolicyAtomicMatchType::AS_PATH;
    bgp_policy::AsPathList asPathList;
    *asPathList.boolean_operator() = BooleanOperator::OR;
    bgpMatch6.as_path_filters() = asPathList;

    const string policyName1 = "Policy Statement 1";
    const auto& bgpPolicies = createBgpPolicies(
        policyName1,
        {bgpMatch1, bgpMatch2, bgpMatch3, bgpMatch4, bgpMatch5, bgpMatch6});
    PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
    const auto& policy1 = policyManager.getPolicyFromName(policyName1);
    const auto& bgpTerms = policy1->getPolicyTerms();
    const auto& asPathEmptyORMatch =
        bgpTerms[0]->getPolicyAttributeMatches()[0];
    const auto& asPathEmptyANDMatch =
        bgpTerms[0]->getPolicyAttributeMatches()[1];
    const auto& asPathOneStarANDMatch =
        bgpTerms[0]->getPolicyAttributeMatches()[2];
    const auto& asPathOneStarORMatch =
        bgpTerms[0]->getPolicyAttributeMatches()[3];
    const auto& asPathOnlyStarANDMatch =
        bgpTerms[0]->getPolicyAttributeMatches()[4];
    const auto& asPathNoneORMatch = bgpTerms[0]->getPolicyAttributeMatches()[5];

    const auto& attributes1 = createBgpPath();
    EXPECT_FALSE(asPathEmptyORMatch->Match(attributes1));
    EXPECT_FALSE(asPathEmptyANDMatch->Match(attributes1));
    EXPECT_FALSE(asPathOneStarANDMatch->Match(attributes1));
    EXPECT_TRUE(asPathOneStarORMatch->Match(attributes1));
    EXPECT_TRUE(asPathOnlyStarANDMatch->Match(attributes1));
    EXPECT_FALSE(asPathNoneORMatch->Match(attributes1));
  }
  {
    /*
     * test other regex
     * kASPathRegexDot = "6.000"
     */
    const auto& bgpMatch1 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::AS_PATH,
        {kASPathRegexDot},
        BooleanOperator::OR);
    const string policyName1 = "Policy Statement 1";
    const auto& bgpPolicies = createBgpPolicies(policyName1, {bgpMatch1});
    PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
    const auto& policy1 = policyManager.getPolicyFromName(policyName1);
    const auto& bgpTerms = policy1->getPolicyTerms();
    const auto& asPathORMatch = bgpTerms[0]->getPolicyAttributeMatches()[0];
    std::vector<BgpAttrAsPathSegmentC> asPath;
    BgpAttrAsPathSegmentC segment1;
    segment1.asSequence = {61000};
    asPath.push_back(segment1);
    const auto& attributes1 = createBgpPath({}, asPath);
    EXPECT_TRUE(asPathORMatch->Match(attributes1));
    asPath.clear();
    segment1.asSequence = {6000};
    asPath.push_back(segment1);
    const auto& attributes2 = createBgpPath({}, asPath);
    EXPECT_FALSE(asPathORMatch->Match(attributes2));
  }
  {
    /*
     * test other regex
     * kASPathRegexNum = "\\d{5}"
     */
    const auto& bgpMatch1 = createBgpPolicyAtomicMatch(
        BgpPolicyAtomicMatchType::AS_PATH,
        {kASPathRegexNum},
        BooleanOperator::OR);
    const string policyName1 = "Policy Statement 1";
    const auto& bgpPolicies = createBgpPolicies(policyName1, {bgpMatch1});
    PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
    const auto& policy1 = policyManager.getPolicyFromName(policyName1);
    const auto& bgpTerms = policy1->getPolicyTerms();
    const auto& asPathORMatch = bgpTerms[0]->getPolicyAttributeMatches()[0];
    std::vector<BgpAttrAsPathSegmentC> asPath;
    BgpAttrAsPathSegmentC segment1;
    segment1.asSequence = {61111};
    asPath.push_back(segment1);
    const auto& attributes1 = createBgpPath({}, asPath);
    EXPECT_TRUE(asPathORMatch->Match(attributes1));
    asPath.clear();
    segment1.asSequence = {6000};
    asPath.push_back(segment1);
    const auto& attributes2 = createBgpPath({}, asPath);
    EXPECT_FALSE(asPathORMatch->Match(attributes2));
  }
}

TEST_F(PolicyTest, SetLocalPreferenceActionTest) {
  // test setting localPreference to kLocalPref2=200
  auto bgpAction = createActionSetLocalPreference(kLocalPref2);
  const string policyName = "Policy Statement";
  const auto& policyConfig = createBgpPolicies(policyName, {}, {bgpAction});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());
  const auto& policy = policyManager.getPolicyFromName(policyName);
  const auto& terms = policy->getPolicyTerms();
  const auto& action = terms[0]->getPolicyActions()[0];

  auto attrFields = buildBgpPathFields(0, 0, 0, 0);
  auto mutableAttrs = attrFields->attrs.get();
  // Set localPref to kLocalPref = 100
  mutableAttrs.localPref = kLocalPref;
  attrFields->attrs = std::move(mutableAttrs);
  auto attrs = std::make_shared<BgpPath>(*attrFields);
  // Verify that before applying, localPref is not set to expected value
  EXPECT_NE(attrs->getLocalPref(), kLocalPref2);
  action->applyAction(attrs);
  // Verify that after applying local preference is set to expected value
  EXPECT_EQ(attrs->getLocalPref(), kLocalPref2);
}

TEST_F(PolicyTest, SetLocalPreferenceActionNegativeTest) {
  {
    // test local_pref cannot be none
    bgp_policy::BgpPolicyAction bgpAction;
    *bgpAction.type() = BgpPolicyActionType::SET_LOCAL_PREF;
    bgp_policy::LocalPreference bgpLocalPref;
    *bgpLocalPref.name() = "empty set_local_pref";
    bgpAction.set_local_pref() = bgpLocalPref;
    const auto& expectedStr =
        "Malformed SetLocalPreference config: empty local_pref";
    const auto& receivedStr = parseActionConfigGetError(bgpAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
  {
    // test local_pref out of range
    bgp_policy::BgpPolicyAction bgpAction;
    *bgpAction.type() = BgpPolicyActionType::SET_LOCAL_PREF;
    bgp_policy::LocalPreference bgpLocalPref;
    bgpLocalPref.local_pref() = -1;
    bgpAction.set_local_pref() = bgpLocalPref;
    const auto& expectedStr = "Malformed SetLocalPreference config: -1";
    const auto& receivedStr = parseActionConfigGetError(bgpAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
  {
    // test add_value is unsupported
    auto bgpAction = createActionSetLocalPreference(kLocalPref2);
    bgpAction.set_local_pref()->add_value() = 10;
    const auto& expectedStr =
        "Unsupported SetLocalPreference config: add_value";
    const auto& receivedStr = parseActionConfigGetError(bgpAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
  {
    // test local_preference_list_names is unsupported
    auto bgpAction = createActionSetLocalPreference(kLocalPref2);
    std::vector<std::string> localPrefList = {"localPrefList"};
    bgpAction.set_local_pref()->local_preference_list_names() = localPrefList;
    const auto& expectedStr =
        "Unsupported SetLocalPreference config: local_preference_list_names";
    const auto& receivedStr = parseActionConfigGetError(bgpAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
}

/*
 * Test if a term has no match condition, it will match all prefixes
 * and do actions for all prefixes
 */
TEST_F(PolicyTest, applyPolicyTermHasNoMatches) {
  /*
   * Create a policy with one term no matches and
   * set action to a type (Modify origin)
   */
  auto action = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::IGP);

  const string policyName = "Policy Statement";
  const auto& policyConfig = createBgpPolicies(policyName, {}, {action});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());

  // Create input (attributes, prefix)
  auto attrs = createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE);
  attrs->publish();
  std::vector<folly::CIDRNetwork> prefixSetIn{kV6Prefix1, kV6Prefix2};

  // Apply policy
  PolicyInMessage policyIn(prefixSetIn, attrs);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  // Verify both the prefixes are permitted
  ASSERT_EQ(prefixSetIn.size(), policyOut.result.size());
  ASSERT_NE(policyOut.result.find(prefixSetIn[0]), policyOut.result.end());
  ASSERT_NE(policyOut.result.find(prefixSetIn[1]), policyOut.result.end());
  // Verify both prefixes share same BgpPath pointer (shallow compare)
  EXPECT_EQ(
      policyOut.result[prefixSetIn[0]]->attrs,
      policyOut.result[prefixSetIn[1]]->attrs);
  // Verify new attributes are created
  EXPECT_NE(attrs, policyOut.result[prefixSetIn[0]]->attrs);

  auto attrsOut = policyOut.result[prefixSetIn[0]];
  // Verify origin action is properly applied
  EXPECT_EQ(attrsOut->attrs->getOrigin(), BgpAttrOrigin::BGP_ORIGIN_IGP);
  // Verify attributes are unpublished
  EXPECT_FALSE(attrsOut->attrs->isPublished());
}

/*
 * Test if a term has match condition (matching all prefixes)
 * without any action, it will be permitted without any changes in attributes
 * Verify same shared_ptr<BgpPath> will be returned. (Save memory)
 */
TEST_F(PolicyTest, applyPolicyTermHasNoActions) {
  // Create a policy with one term, match (origin IGP) and has no actions
  auto matchIgp = createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::ORIGIN);
  matchIgp.origin() = bgp_policy::Origin::IGP;

  const string policyName = "Policy Statement";
  const auto& policyConfig = createBgpPolicies(policyName, {matchIgp}, {});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());

  // Create input (attributes, prefix)
  auto attrsIn = createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
  attrsIn->publish();
  std::vector<folly::CIDRNetwork> prefixSetIn{kV6Prefix1, kV6Prefix2};

  // Apply policy
  PolicyInMessage policyIn(prefixSetIn, attrsIn);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  // Verify both the prefixes are permitted
  ASSERT_EQ(prefixSetIn.size(), policyOut.result.size());
  ASSERT_NE(policyOut.result.find(prefixSetIn[0]), policyOut.result.end());
  ASSERT_NE(policyOut.result.find(prefixSetIn[1]), policyOut.result.end());
  // Verify both prefixes share same BgpPath pointer (shallow compare)
  EXPECT_EQ(
      policyOut.result[prefixSetIn[0]]->attrs,
      policyOut.result[prefixSetIn[1]]->attrs);
  // Verify input attributes only are returned (no new BgpPath created)
  EXPECT_EQ(attrsIn, policyOut.result[prefixSetIn[0]]->attrs);

  auto attrsOut = policyOut.result[prefixSetIn[0]];
  // Verify attributes are published (same as input)
  EXPECT_TRUE(attrsOut->attrs->isPublished());
}

// Test if none of the terms match, we drop the prefixes
TEST_F(PolicyTest, applyPolicyNoneOfTheTermsMatch) {
  /*
   * Create a policy with one term (one origin match, one origin action)
   * Input attributes does not match the term
   */
  auto matchEgp = createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::ORIGIN);
  matchEgp.origin() = bgp_policy::Origin::EGP;

  auto actionIgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::IGP);

  const string policyName = "Policy Statement";
  const auto& bgpPolicies =
      createBgpPolicies(policyName, {matchEgp}, {actionIgp});
  PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());

  // Create input (attributes, prefix)
  auto attrsIn = createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
  attrsIn->publish();
  std::vector<folly::CIDRNetwork> prefixSetIn{kV6Prefix1, kV6Prefix2};

  // Apply policy
  PolicyInMessage policyIn(prefixSetIn, attrsIn);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  /*
   * Verify both the prefixes are not permitted
   * But we keep track of those prefixes as well.
   */
  ASSERT_EQ(2, policyOut.result.size());
}

// test term miss action deny
TEST_F(PolicyTest, applyPolicyTermMissActionDeny) {
  // TERM1 (match kCommunity1, origin action IGP)
  const auto& match1 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST, {kCommunity1});
  auto actionIgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::IGP);

  // TERM2 (match kV6Prefix1 and apply origin action(EGP))
  routing_policy::CompareNumericValue compareStructEQ;
  compareStructEQ.compare_operator() = routing_policy::ComparisonOperator::EQ;
  compareStructEQ.value() = kV6Prefix1.second;
  const auto& prefixListEntry = createPrefixListEntry(
      IPAddress::networkToString(kV6Prefix1), {compareStructEQ});
  const auto& match2 = createPrefixListMatch({prefixListEntry});
  auto actionEgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::EGP);

  /*
   * Case 1: Policy with TERM1 (termMissAction DENY), TERM2
   * Input prefix match TERM2, attributes does not match community in TERM1.
   * Prefix is denied in TERM1
   */
  {
    // Create TERM1 with termMissAction DENY
    auto term1MissActionDeny = createBgpPolicyTerm(
        "Term1MissActionDeny",
        "",
        {match1},
        {actionIgp},
        bgp_policy::FlowControlAction::DENY);
    auto term2 = createBgpPolicyTerm("Term2", "", {match2}, {actionEgp});

    const string policyName = "Policy Statement";
    const auto& policyConfig =
        createBgpPolicies(policyName, {term1MissActionDeny, term2});
    PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());

    // prefixSetMatch has kV6Prefix1, match TERM2
    std::vector<folly::CIDRNetwork> prefixSetMatch{kV6Prefix1};
    auto attrsNotMatch =
        createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE);
    // community dies bit natch kCommunity2 in TERM1
    attrsNotMatch->setCommunities(createBgpAttrCommunitiesC({kCommunity2}));
    attrsNotMatch->publish();

    // Apply policy
    PolicyInMessage policyIn(prefixSetMatch, attrsNotMatch);
    auto policyOut = policyManager.applyPolicy(policyName, policyIn);

    // Verify prefix is denied
    ASSERT_EQ(prefixSetMatch.size(), policyOut.result.size());
    EXPECT_EQ(nullptr, policyOut.result.at(kV6Prefix1)->attrs);
  }

  /*
   * Case 2: Policy with TERM2 (termMissAction DENY), TERM1
   * Input attributes matches community in TERM1, prefix does not match TERM1,
   * Prefix is denied in TERM1
   */
  {
    // Create TERM1 with termMissAction DENY
    auto term1 = createBgpPolicyTerm("Term1", "", {match1}, {actionIgp});
    auto term2MissActionDeny = createBgpPolicyTerm(
        "Term2MissActionDeny",
        "",
        {match2},
        {actionEgp},
        bgp_policy::FlowControlAction::DENY);

    const string policyName = "Policy Statement";
    const auto& policyConfig =
        createBgpPolicies(policyName, {term2MissActionDeny, term1});
    PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());

    // prefixSetMatch has kV6Prefix2, does not match TERM2
    std::vector<folly::CIDRNetwork> prefixSetMatch{kV6Prefix2};
    auto attrsMatch =
        createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE);
    // community natches kCommunity1 in TERM1
    attrsMatch->setCommunities(createBgpAttrCommunitiesC({kCommunity1}));
    attrsMatch->publish();

    // Apply policy
    PolicyInMessage policyIn(prefixSetMatch, attrsMatch);
    auto policyOut = policyManager.applyPolicy(policyName, policyIn);

    // Verify prefix is denied
    ASSERT_EQ(prefixSetMatch.size(), policyOut.result.size());
    EXPECT_EQ(nullptr, policyOut.result.at(kV6Prefix2)->attrs);
  }
}

// test term miss action LOG_AND_NEXT_TERM
TEST_F(PolicyTest, applyPolicyTermMissActionLogAndNextTerm) {
  /*
   * kV4Prefix1 will be denied and kV4Prefix2 will log and go to next term
   * - Term1 matches kV4Prefix1 (match = next, miss = LogAndNextTerm)
   * (Default deny)
   */
  routing_policy::CompareNumericValue compareStructEQ;
  *compareStructEQ.compare_operator() = routing_policy::ComparisonOperator::EQ;
  *compareStructEQ.value() = kV4Prefix1.second;
  const auto& prefixListEntry1 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix1), {compareStructEQ});

  const auto& match1 = createPrefixListMatch({prefixListEntry1});
  auto actionContinue = createBgpPolicyAction(BgpPolicyActionType::CONTINUE);
  auto term1 = createBgpPolicyTerm(
      "Term1 - Match PrefixList",
      "",
      {match1},
      {actionContinue},
      bgp_policy::FlowControlAction::LOG_AND_NEXT_TERM);

  // Create policy composed of the three terms.
  const string policyName = "Policy Statement";
  const auto& policyConfig = createBgpPolicies(policyName, {term1});
  PolicyManager policyManager{policyConfig, createTestBgpGlobalConfig()};

  // Prefixes to be evaluated against policy..
  std::vector<folly::CIDRNetwork> prefixSetIn{kV4Prefix1, kV4Prefix2};

  auto attrsIn = createBgpPathWithOrigin();
  attrsIn->publish();

  // Apply policy.
  PolicyInMessage policyIn(prefixSetIn, attrsIn);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  ASSERT_EQ(policyOut.result[kV4Prefix1]->attrs, nullptr); // default deny
  ASSERT_EQ(
      policyOut.result[kV4Prefix2]->attrs, nullptr); // log and next term deny
}

/*
 * Test all match-conditions in a term must match for action to be applied
 * Case 1: Both match-conditions matched, actions applied
 * Case 2, 3: Only one match-condition matches, so actions are not applied
 */
TEST_F(PolicyTest, applyPolicyMultipleMatchesInATermAllMustMatch) {
  /*
   * Create a policy with one term
   * (origin match, community match, origin action)
   */
  auto match1 = createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::ORIGIN);
  match1.origin() = bgp_policy::Origin::EGP;
  const auto& match2 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST, {kCommunity1});

  auto actionIgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::IGP);

  const string policyName = "Policy Statement";
  const auto& policyConfig =
      createBgpPolicies(policyName, {match1, match2}, {actionIgp});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());

  std::vector<folly::CIDRNetwork> prefixSetIn{kV6Prefix1, kV6Prefix2};

  /*
   * Case 1: Input attributes matches both community and Origin
   *         Prefixes are permitted
   */
  {
    auto attrsMatchAll = createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_EGP);
    attrsMatchAll->setCommunities(createBgpAttrCommunitiesC({kCommunity1}));
    attrsMatchAll->publish();

    // Apply policy
    PolicyInMessage policyIn(prefixSetIn, attrsMatchAll);
    auto policyOut = policyManager.applyPolicy(policyName, policyIn);

    // Verify both the prefixes are permitted
    ASSERT_EQ(prefixSetIn.size(), policyOut.result.size());
    ASSERT_NE(policyOut.result.find(prefixSetIn[0]), policyOut.result.end());
    ASSERT_NE(policyOut.result.find(prefixSetIn[1]), policyOut.result.end());
    // Verify both prefixes share same BgpPath pointer (shallow compare)
    EXPECT_EQ(
        policyOut.result[prefixSetIn[0]]->attrs,
        policyOut.result[prefixSetIn[1]]->attrs);
    // Verify new attributes are created (set action)
    EXPECT_NE(attrsMatchAll, policyOut.result[prefixSetIn[0]]->attrs);

    auto attrsOut = policyOut.result[prefixSetIn[0]];
    // Verify attributes are unpublished
    EXPECT_FALSE(attrsOut->attrs->isPublished());
    // Verify origin action is properly applied
    EXPECT_EQ(attrsOut->attrs->getOrigin(), BgpAttrOrigin::BGP_ORIGIN_IGP);
  }

  /*
   * Case 2: Input matches only origin (community does not match).
   *         So, prefixes are dropped.
   */
  {
    auto attrsMatchOrigin =
        createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_EGP);
    // Community does not match with the match-condition of the term
    attrsMatchOrigin->setCommunities(createBgpAttrCommunitiesC({kCommunity2}));
    attrsMatchOrigin->publish();

    PolicyInMessage policyIn(prefixSetIn, attrsMatchOrigin);
    auto policyOut = policyManager.applyPolicy(policyName, policyIn);

    // Verify that none of the prefixes are permitted
    ASSERT_EQ(2, policyOut.result.size());
  }

  /*
   * Case 3: Input matches only community (Origin does not match).
   *         So, prefixes are dropped.
   */
  {
    auto attrsMatchCommunity =
        createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
    attrsMatchCommunity->setCommunities(
        createBgpAttrCommunitiesC({kCommunity1}));
    attrsMatchCommunity->publish();

    PolicyInMessage policyIn(prefixSetIn, attrsMatchCommunity);
    auto policyOut = policyManager.applyPolicy(policyName, policyIn);

    // Verify that none of the prefixes are permitted
    ASSERT_EQ(2, policyOut.result.size());
  }
}

// Test that we skip first term and match 2nd term properly
TEST_F(PolicyTest, applyPolicyMultipleTermsMatch2ndTerm) {
  /*
   * Create a policy with two terms
   * Term1 match kCommunity1 action origin IGP
   * Term2 match kCommunity2 action origin EGP
   */
  const auto& match1 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST, {kCommunity1});
  auto actionIgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::IGP);

  const auto& match2 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST, {kCommunity2});
  auto actionEgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::EGP);

  auto term1 = createBgpPolicyTerm("Term1", "", {match1}, {actionIgp});
  auto term2 = createBgpPolicyTerm("Term2", "", {match2}, {actionEgp});

  const string policyName = "Policy Statement";
  const auto& policyConfig = createBgpPolicies(policyName, {term1, term2});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());

  std::vector<folly::CIDRNetwork> prefixSetIn{kV6Prefix1, kV6Prefix2};

  // Input attributes which match 2nd term
  auto attrsIn = createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE);
  attrsIn->setCommunities(createBgpAttrCommunitiesC({kCommunity2}));
  attrsIn->publish();

  // Apply policy
  PolicyInMessage policyIn(prefixSetIn, attrsIn);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  // Verify both the prefixes are permitted
  ASSERT_EQ(prefixSetIn.size(), policyOut.result.size());
  ASSERT_NE(policyOut.result.find(prefixSetIn[0]), policyOut.result.end());
  ASSERT_NE(policyOut.result.find(prefixSetIn[1]), policyOut.result.end());
  // Verify both prefixes share same BgpPath pointer (shallow compare)
  EXPECT_EQ(
      policyOut.result[prefixSetIn[0]]->attrs,
      policyOut.result[prefixSetIn[1]]->attrs);
  // Verify new attributes are created (set action)
  EXPECT_NE(attrsIn, policyOut.result[prefixSetIn[0]]->attrs);

  auto attrsOut = policyOut.result[prefixSetIn[0]];
  // Verify attributes are unpublished
  EXPECT_FALSE(attrsOut->attrs->isPublished());
  // Verify origin action is properly applied (2nd Term's action)
  EXPECT_EQ(attrsOut->attrs->getOrigin(), BgpAttrOrigin::BGP_ORIGIN_EGP);
}

/*
 * Test policy stats are proper.
 * Verify stats when we skip first term and match 2nd term.
 */
TEST_F(PolicyTest, verifyPolicyStats) {
  /*
   * Create a policy with two terms
   * Term1 match kCommunity1 action origin IGP
   * Term2 match kCommunity2 action origin EGP
   */
  const auto& match1 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST, {kCommunity1});
  auto actionIgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::IGP);

  const auto& match2 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST, {kCommunity2});
  auto actionEgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::EGP);

  auto term1 = createBgpPolicyTerm("Term1", "", {match1}, {actionIgp});
  auto term2 = createBgpPolicyTerm("Term2", "", {match2}, {actionEgp});

  const string policyName = "Policy Statement";
  const auto& policyConfig = createBgpPolicies(policyName, {term1, term2});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());

  std::vector<folly::CIDRNetwork> prefixSetIn{kV6Prefix1, kV6Prefix2};

  // Input attributes which match 2nd term
  auto attrsIn = createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE);
  attrsIn->setCommunities(createBgpAttrCommunitiesC({kCommunity2}));
  attrsIn->publish();

  // Apply policy
  PolicyInMessage policyIn(prefixSetIn, attrsIn);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  // Verify both the prefixes are permitted
  ASSERT_EQ(prefixSetIn.size(), policyOut.result.size());
  ASSERT_NE(policyOut.result.find(prefixSetIn[0]), policyOut.result.end());
  ASSERT_NE(policyOut.result.find(prefixSetIn[1]), policyOut.result.end());

  neteng::routing::policy::thrift::TPolicyStats stats;
  policyManager.getPolicyStats(stats);
  // Verify only one policy statement
  ASSERT_EQ(1, stats.policy_statement_stats()->size());
  EXPECT_EQ(policyName, *stats.policy_statement_stats()[0].name());
  // Verify two prefixes are counted as hit count
  EXPECT_EQ(
      prefixSetIn.size(),
      *stats.policy_statement_stats()[0].prefix_hit_count());
  // Verify that only single run of policy is executed
  EXPECT_EQ(1, *stats.policy_statement_stats()[0].num_of_runs());
  // Verify two terms are present
  ASSERT_EQ(2, stats.policy_statement_stats()[0].term_stats()->size());
  // Verify only 2nd term hit count is increased
  EXPECT_EQ("Term1", *stats.policy_statement_stats()[0].term_stats()[0].name());
  EXPECT_EQ(
      0, *stats.policy_statement_stats()[0].term_stats()[0].prefix_hit_count());
  EXPECT_EQ("Term2", *stats.policy_statement_stats()[0].term_stats()[1].name());
  EXPECT_EQ(
      2, *stats.policy_statement_stats()[0].term_stats()[1].prefix_hit_count());
}

TEST_F(PolicyTest, SetMedGenuineActionTest) {
  auto medAction = createBgpPolicyMedAction(kMed2);
  auto term = createBgpPolicyTerm("Term1", "", {}, {medAction});

  const std::string policyName = "Policy Statement";
  const auto& policyConfig = createBgpPolicies(policyName, {term});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());

  std::vector<folly::CIDRNetwork> prefixSetIn{kV4Prefix1};

  // Attributes before applying policy
  auto attrsIn = createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
  attrsIn->setMed(kMed);
  attrsIn->setLocalPref(kLocalPref);
  attrsIn->publish();

  auto actionData = std::make_shared<BgpPolicyActionData>();
  EXPECT_FALSE(actionData->isMedSetByPolicy);

  // Apply policy with proper actionData.
  PolicyInMessage policyIn{prefixSetIn, attrsIn, actionData};
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);
  EXPECT_EQ(kMed2, policyOut.result[kV4Prefix1]->attrs->getMed());
  EXPECT_TRUE(actionData->isMedSetByPolicy);

  // Apply policy with no actionData.
  PolicyInMessage policyInNoActionData{prefixSetIn, attrsIn};
  EXPECT_DEATH(policyManager.applyPolicy(policyName, policyInNoActionData), "");

  // Apply policy with nullptr actionData.
  PolicyInMessage policyInNullActionData{
      prefixSetIn, attrsIn, nullptr /* actionData */};
  EXPECT_DEATH(
      policyManager.applyPolicy(policyName, policyInNullActionData),
      "Expected BgpPolicyActionData for SetMed");
}

TEST_F(PolicyTest, SetMedSkippedActionTest) {
  /*
   * Create a policy with two terms
   * Term1 match origin IGP and set med to kMed2
   * Term2 match all and set localpref to kLocalPref2
   */
  const auto& match1 = createOriginMatch(bgp_policy::Origin::IGP);
  auto actionSetMed = createBgpPolicyMedAction(kMed2);
  auto term1 = createBgpPolicyTerm("Term1", "", {match1}, {actionSetMed});
  auto actionSetLocalPref = createActionSetLocalPreference(kLocalPref2);
  auto term2 = createBgpPolicyTerm("Term2", "", {}, {actionSetLocalPref});

  const std::string policyName = "Policy Statement";
  const auto& policyConfig = createBgpPolicies(policyName, {term1, term2});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());

  std::vector<folly::CIDRNetwork> prefixSetIn{kV4Prefix1};

  // Attributes before applying policy.
  auto attrsIn = createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_EGP);
  attrsIn->setMed(kMed);
  attrsIn->setLocalPref(kLocalPref);
  attrsIn->publish();

  auto actionData = std::make_shared<BgpPolicyActionData>();
  EXPECT_FALSE(actionData->isMedSetByPolicy);

  // Apply policy.
  PolicyInMessage policyIn(prefixSetIn, attrsIn, actionData);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);
  EXPECT_EQ(kMed, policyOut.result[kV4Prefix1]->attrs->getMed());

  EXPECT_FALSE(actionData->isMedSetByPolicy);
}

/*
 * Test that prefix won't fall through terms
 * We only match each prefix once without CONTINUE
 */
TEST_F(PolicyTest, applyPolicyMultipleTermsPrefixOnlyMatchedOnce) {
  /*
   * Create a policy with two terms
   * Term1 match origin IGP and set med to kMed2
   * Term2 match all and set localpref to kLocalPref2
   */
  const auto& match1 = createOriginMatch(bgp_policy::Origin::IGP);
  auto actionSetMed = createBgpPolicyMedAction(kMed2);
  auto term1 = createBgpPolicyTerm("Term1", "", {match1}, {actionSetMed});
  auto actionSetLocalPref = createActionSetLocalPreference(kLocalPref2);
  auto term2 = createBgpPolicyTerm("Term2", "", {}, {actionSetLocalPref});

  // Create policy
  const std::string policyName = "Policy Statement";
  const auto& policyConfig = createBgpPolicies(policyName, {term1, term2});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());

  std::vector<folly::CIDRNetwork> prefixSetIn{kV4Prefix1};

  // Attributes before applying policy
  auto attrsIn = createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
  attrsIn->setMed(kMed);
  attrsIn->setLocalPref(kLocalPref);
  attrsIn->publish();

  // Apply policy
  auto actionData = std::make_shared<BgpPolicyActionData>();
  PolicyInMessage policyIn(prefixSetIn, attrsIn, actionData);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  // Verify prefix is permitted (Explicit PERMIT) and med action is taken
  ASSERT_EQ(prefixSetIn.size(), policyOut.result.size());
  ASSERT_NE(policyOut.result.find(prefixSetIn[0]), policyOut.result.end());

  auto attrsOut = policyOut.result[prefixSetIn[0]];
  // Verify new attributes are created (set action)
  EXPECT_NE(attrsIn, attrsOut->attrs);
  // Verify attributes are unpublished
  EXPECT_FALSE(attrsOut->attrs->isPublished());
  // Verify med action is properly applied (1st Term's action)
  EXPECT_EQ(kMed2, attrsOut->attrs->getMed());
  EXPECT_TRUE(actionData->isMedSetByPolicy);
  // Not keep matching it with term2 so localPref still kLocalPref
  EXPECT_EQ(kLocalPref, attrsOut->attrs->getLocalPref());
}

// Test that prefix can fall through multiple terms with MatchAction = CONTINUE
TEST_F(PolicyTest, applyPolicyMultipleTermsWithContinue) {
  /*
   * helper lambda to verify multiple terms with continue
   * @param continueAsFirstAction: where CONTINUE is placed in a term
   *  true - term1:[CONTINUE, action1], term2:[action2]
   * false - term1:[action1, CONTINUE], term2:[action2]
   */
  auto verifyTermsWithContinue = [&](bool continueAsFirstAction) {
    /*
     * Create a policy with two terms
     * Term1 match origin IGP, set med to kMed2 and CONTINUE
     * Term2 match all and set localpref to kLocalPref2
     */
    const auto& match1 = createOriginMatch(bgp_policy::Origin::IGP);
    auto actionSetMed = createBgpPolicyMedAction(kMed2);
    auto actionContinue = createBgpPolicyAction(BgpPolicyActionType::CONTINUE);
    std::vector<bgp_policy::BgpPolicyAction> actions{};
    if (continueAsFirstAction) {
      actions = {actionContinue, actionSetMed};
    } else {
      actions = {actionSetMed, actionContinue};
    }
    auto term1 = createBgpPolicyTerm("Term1", "", {match1}, actions);
    auto actionSetLocalPref = createActionSetLocalPreference(kLocalPref2);
    auto term2 = createBgpPolicyTerm("Term2", "", {}, {actionSetLocalPref});

    // Create policy
    const std::string policyName = "Policy Statement";
    const auto& policyConfig = createBgpPolicies(policyName, {term1, term2});
    PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());

    std::vector<folly::CIDRNetwork> prefixSetIn{kV4Prefix1};

    /*
     * Attributes before applying policy:
     * origin = IGP, med = kMed, local_pref = kLocalPref
     */
    auto attrsIn = createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
    attrsIn->setMed(kMed);
    attrsIn->setLocalPref(kLocalPref);
    attrsIn->publish();

    // Apply policy
    auto actionData = std::make_shared<BgpPolicyActionData>();
    PolicyInMessage policyIn(prefixSetIn, attrsIn, actionData);
    auto policyOut = policyManager.applyPolicy(policyName, policyIn);

    /*
     * Verify prefix is permitted (Explicit PERMIT)
     * Both med action and localpref action are applied
     */
    ASSERT_EQ(prefixSetIn.size(), policyOut.result.size());
    ASSERT_NE(policyOut.result.find(prefixSetIn[0]), policyOut.result.end());

    auto attrsOut = policyOut.result[prefixSetIn[0]];
    // Verify new attributes are created (set action)
    EXPECT_NE(attrsIn, attrsOut->attrs);
    // Verify attributes are unpublished
    EXPECT_FALSE(attrsOut->attrs->isPublished());
    // Verify med action is properly applied (1st Term's action)
    EXPECT_EQ(kMed2, attrsOut->attrs->getMed());
    EXPECT_TRUE(actionData->isMedSetByPolicy);
    // Not keep matching it with term2 so localPref still kLocalPref
    EXPECT_EQ(kLocalPref2, attrsOut->attrs->getLocalPref());
  };

  // CONTINUE's position within a term does NOT matter
  verifyTermsWithContinue(true);
  verifyTermsWithContinue(false);
}

/*
 * Test multiple actions are applied
 * Test explicit PERMIT, DENY actions
 */
TEST_F(PolicyTest, applyPolicyMultipleActionsInATerm) {
  /*
   * Create a policy with two terms
   * Term1 match kCommunity1 action origin IGP, action PERMIT
   * Term2 match kCommunity2 action DENY
   */
  const auto& match1 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST, {kCommunity1});
  auto actionIgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::IGP);
  auto actionPermit = createBgpPolicyAction(BgpPolicyActionType::PERMIT);

  const auto& match2 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST, {kCommunity2});
  auto actionDeny = createBgpPolicyAction(BgpPolicyActionType::DENY);

  auto term1 =
      createBgpPolicyTerm("Term1", "", {match1}, {actionIgp, actionPermit});
  auto term2 = createBgpPolicyTerm("Term2", "", {match2}, {actionDeny});

  const string policyName = "Policy Statement";
  const auto& policyConfig = createBgpPolicies(policyName, {term1, term2});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());

  std::vector<folly::CIDRNetwork> prefixSetIn{kV4Prefix1};

  // Case 1: Input attributes which match 1st term (apply two actions)
  {
    auto attrsIn =
        createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE);
    attrsIn->setCommunities(createBgpAttrCommunitiesC({kCommunity1}));
    attrsIn->publish();

    // Apply policy
    PolicyInMessage policyIn(prefixSetIn, attrsIn);
    auto policyOut = policyManager.applyPolicy(policyName, policyIn);

    // Verify prefix is permitted (Explicit PERMIT) and origin action is taken
    ASSERT_EQ(prefixSetIn.size(), policyOut.result.size());
    ASSERT_NE(policyOut.result.find(prefixSetIn[0]), policyOut.result.end());
    // Verify new attributes are created (set action)
    EXPECT_NE(attrsIn, policyOut.result[prefixSetIn[0]]->attrs);
    auto attrsOut = policyOut.result[prefixSetIn[0]];
    // Verify attributes are unpublished
    EXPECT_FALSE(attrsOut->attrs->isPublished());
    // Verify origin action is properly applied (1st Term's action)
    EXPECT_EQ(attrsOut->attrs->getOrigin(), BgpAttrOrigin::BGP_ORIGIN_IGP);
  }

  // Case 2: Input attributes which match 2nd term
  {
    auto attrsIn =
        createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE);
    attrsIn->setCommunities(createBgpAttrCommunitiesC({kCommunity2}));
    attrsIn->publish();

    // Apply policy
    PolicyInMessage policyIn(prefixSetIn, attrsIn);
    auto policyOut = policyManager.applyPolicy(policyName, policyIn);

    // Verify prefix is denied (Explicit DENY)
    ASSERT_EQ(1, policyOut.result.size());
    for (const auto& kv : policyOut.result) {
      auto str = (*kv.second).policyName;
      ASSERT_NE(str.find("Denied"), std::string::npos);
    }
  }
}

TEST_F(PolicyTest, CommunityListActionTest) {
  /*
   * Create a catch all term and ADD/SET/REMOVE communities
   * Input attributes with some communities
   * Result should have modified attributes according to community action type
   */
  auto lambdaVerifyCommunityAction =
      [&](const bgp_policy::CommunityActionType type,
          const vector<string>& actionCommunities,
          const vector<string>& attrCommunities,
          const vector<string>& expectedCommunities) {
        auto bgpAction =
            createBgpPolicyCommunityAction(type, actionCommunities);

        const string policyName = "Policy Statement";
        const auto& policyConfig =
            createBgpPolicies(policyName, {}, {bgpAction});
        PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());
        const auto& policy = policyManager.getPolicyFromName(policyName);
        const auto& terms = policy->getPolicyTerms();
        const auto& action = terms[0]->getPolicyActions()[0];

        auto attrFields = buildBgpPathFields(0, 0, 0, 0);
        auto attrs = std::make_shared<BgpPath>(*attrFields);
        attrs->setCommunities(createBgpAttrCommunitiesC(attrCommunities));
        action->applyAction(attrs);
        /*
         * Verify that after applying, communities are appended to existing
         * values. Verify the order of communities is also as expected.
         */

        const auto testCommunities = attrs->getCommunities().get();

        EXPECT_THAT(
            ConvertAttrCommunitiesToStrings(testCommunities),
            testing::ElementsAreArray(expectedCommunities));
      };

  /*
   * Verify that Add action adds missing communities to existing communities
   * Ignores common communities (Does not repeat)
   * Ensure that new communities are added to END of existing communities
   */
  lambdaVerifyCommunityAction(
      bgp_policy::CommunityActionType::ADD,
      {kCommunity1, kCommunity2}, // Communities in action
      {kCommunity3, kCommunity2}, // Communities in attributes
      {kCommunity1, kCommunity2, kCommunity3}); // Expected communities

  // Verify that set action replaces existing communities with new communities
  lambdaVerifyCommunityAction(
      bgp_policy::CommunityActionType::SET,
      {kCommunity1, kCommunity2}, // Communities in action
      {kCommunity3, kCommunity4}, // Communities in attributes
      {kCommunity1, kCommunity2}); // Expected communities

  /*
   * Verify that remove action removes communities matching from input,
   * ignores any action communities that did not match any in attributes
   */
  lambdaVerifyCommunityAction(
      bgp_policy::CommunityActionType::REMOVE,
      {kCommunity1, kCommunity2}, // Communities in action
      {kCommunity3, kCommunity2}, // Communities in attributes
      {kCommunity3}); // Expected communities

  // Verify that remove action can remove all attributes
  lambdaVerifyCommunityAction(
      bgp_policy::CommunityActionType::REMOVE,
      {kCommunity1, kCommunity2, kCommunity3}, // Communities in action
      {kCommunity1, kCommunity2}, // Communities in attributes
      {}); // Expected communities

  /*
   * Verify that remove action with regEx removes communities matching from
   * input, this verifies multiple regExs in a action, mix of regEx and
   * non-regEx
   */
  lambdaVerifyCommunityAction(
      bgp_policy::CommunityActionType::REMOVE,
      {kCommunityRegex1,
       kCommunityRegex2,
       kCommunity3}, // Communities in action
      {kCommunityMatchingRegex1,
       kCommunityMatchingRegex2,
       kCommunity3,
       kCommunity4}, // Communities in attributes
      {kCommunity4}); // Expected communities

  // Verify that we accept and set communities to empty
  lambdaVerifyCommunityAction(
      bgp_policy::CommunityActionType::SET,
      {}, // Communities in action
      {kCommunity3, kCommunity4}, // Communities in attributes
      {}); // Expected communities

  // Verify that we operate correctly on empty community attribute
  std::array<bgp_policy::CommunityActionType, 2> action_types = {
      bgp_policy::CommunityActionType::SET,
      bgp_policy::CommunityActionType::ADD};
  for (const auto action_type : action_types) {
    lambdaVerifyCommunityAction(
        action_type,
        {kCommunity1, kCommunity2}, // Communities in action
        {}, // Communities in attributes
        {kCommunity1, kCommunity2}); // Expected communities
  }

  // Verify that remove action on empty community attribute
  lambdaVerifyCommunityAction(
      bgp_policy::CommunityActionType::REMOVE,
      {kCommunity1, kCommunity2, kCommunity3}, // Communities in action
      {}, // Communities in attributes
      {}); // Expected communities
}

/**
 * Test Link bandwidth ExtCommunity Policy
 * Cover all use cases:
 * - DISABLE
 * - SET_LINK_BPS
 * - BEST_PATH
 * - AGGREGATE_RECEIVED
 * - AGGREGATE_LOCAL
 */
TEST_F(PolicyTest, LbwExtCommunityActionTest) {
  auto verifyLbwAction =
      [&](
          // (input) lbw action data
          BgpPolicyActionData policyActionData,
          // (input) lbw action type
          const bgp_policy::LbwExtCommunityActionType type,
          // (expected-output) expected postfilter lbw
          std::optional<std::pair<uint16_t, float>> expectedLbwOut) {
        // 1. create attrs
        auto attrFields = buildBgpPathFields(0, 0, 0, 0);
        auto attrs = std::make_shared<BgpPath>(*attrFields);

        // 2. create an action
        auto bgpAction = createBgpPolicyLbwExtCommunityAction(type);
        const string policyName = "Policy Statement";
        const auto& policyConfig =
            createBgpPolicies(policyName, {}, {bgpAction});
        PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());
        const auto& policy = policyManager.getPolicyFromName(policyName);
        const auto& terms = policy->getPolicyTerms();
        const auto& action = terms[0]->getPolicyActions()[0];

        // 3. apply action
        action->applyAction(
            attrs, std::make_shared<BgpPolicyActionData>(policyActionData));

        // 4. verify lbwOut
        if (!expectedLbwOut.has_value()) {
          EXPECT_FALSE(attrs->hasNonTransitiveLbwExtCommunity());
          XLOG(DBG1, "lbwOut has no lbw");
        } else {
          EXPECT_TRUE(attrs->hasNonTransitiveLbwExtCommunity());
          EXPECT_EQ(attrs->getNonTransitiveLbwAsn(), expectedLbwOut->first);
          EXPECT_EQ(attrs->getNonTransitiveLbwValue(), expectedLbwOut->second);
          XLOGF(
              DBG1,
              "lbwOut asn:{}, lbw: {}",
              expectedLbwOut->first,
              expectedLbwOut->second);
        }
      };

  /*
   * action: DISABLE (apply to both receive and advertise)
   * lbw should be pruned from attr regardless input-lbw is set or not
   */
  verifyLbwAction(
      createLbwActionData(std::make_pair(65530, 100), 65530),
      bgp_policy::LbwExtCommunityActionType::DISABLE,
      std::nullopt);

  verifyLbwAction(
      createLbwActionData(std::nullopt, 65530),
      bgp_policy::LbwExtCommunityActionType::DISABLE,
      std::nullopt);

  /*
   * action: SET_LINK_BPS (apply to both receive and advertise)
   * lbw shall be overwritten as lbw from config
   */
  verifyLbwAction(
      createLbwActionData(
          std::make_pair(65530, 100), 65530, std::nullopt, std::nullopt, 200),
      bgp_policy::LbwExtCommunityActionType::SET_LINK_BPS,
      std::make_pair(65530, 200));

  verifyLbwAction(
      createLbwActionData(std::nullopt, 65530, std::nullopt, std::nullopt, 300),
      bgp_policy::LbwExtCommunityActionType::SET_LINK_BPS,
      std::make_pair(65530, 300));

  /*
   * action: ACCEPT (receive-only)
   * action does nothing, keep as is
   */
  verifyLbwAction(
      createLbwActionData(std::make_pair(65530, 100), 65530),
      bgp_policy::LbwExtCommunityActionType::ACCEPT,
      std::make_pair(65530, 100));

  verifyLbwAction(
      createLbwActionData(std::nullopt, 65530),
      bgp_policy::LbwExtCommunityActionType::ACCEPT,
      std::nullopt);

  /*
   * action: BEST_PATH (advertise-only)
   * keep best-path lbw as is if received lbw from all peers
   * (aggregate-received-lbw), otherwise, prune lbw
   */
  verifyLbwAction(
      createLbwActionData(
          std::make_pair(65530, 100),
          65530,
          std::nullopt,
          std::nullopt,
          200,
          400 /* agg-recv */),
      bgp_policy::LbwExtCommunityActionType::BEST_PATH,
      std::make_pair(65530, 100));

  verifyLbwAction(
      createLbwActionData(
          std::make_pair(65530, 100),
          65530,
          std::nullopt,
          std::nullopt,
          200,
          std::nullopt /* agg-recv */),
      bgp_policy::LbwExtCommunityActionType::BEST_PATH,
      std::nullopt);

  verifyLbwAction(
      createLbwActionData(
          std::nullopt,
          65530,
          std::nullopt,
          std::nullopt,
          200,
          400 /* agg-recv */),
      bgp_policy::LbwExtCommunityActionType::BEST_PATH,
      std::nullopt);

  verifyLbwAction(
      createLbwActionData(
          std::nullopt,
          65530,
          std::nullopt,
          std::nullopt,
          200,
          std::nullopt /* agg-recv */),
      bgp_policy::LbwExtCommunityActionType::BEST_PATH,
      std::nullopt);

  /*
   * action: AGGREGATE_LOCAL (advertise-only)
   * set lbw from aggregate-local-lbw if all peers has link-bps set,
   * otherwise prune lbw
   */
  verifyLbwAction(
      createLbwActionData(
          std::make_pair(65530, 100),
          65530,
          std::nullopt,
          std::nullopt,
          200,
          400,
          600),
      bgp_policy::LbwExtCommunityActionType::AGGREGATE_LOCAL,
      std::make_pair(65530, 600));

  verifyLbwAction(
      createLbwActionData(
          std::make_pair(65530, 100),
          65530,
          std::nullopt,
          std::nullopt,
          200,
          400,
          std::nullopt),
      bgp_policy::LbwExtCommunityActionType::AGGREGATE_LOCAL,
      std::nullopt);

  verifyLbwAction(
      createLbwActionData(
          std::nullopt, 65530, std::nullopt, std::nullopt, 200, 400, 600),
      bgp_policy::LbwExtCommunityActionType::AGGREGATE_LOCAL,
      std::make_pair(65530, 600));

  verifyLbwAction(
      createLbwActionData(
          std::nullopt,
          65530,
          std::nullopt,
          std::nullopt,
          200,
          400,
          std::nullopt),
      bgp_policy::LbwExtCommunityActionType::AGGREGATE_LOCAL,
      std::nullopt);

  /*
   * action: AGGREGATE_RECEIVED (advertise-only)
   * set lbw from aggregate-received-lbw if received lbw from all peers
   * otherwise prune lbw
   */
  verifyLbwAction(
      createLbwActionData(
          std::make_pair(65530, 100),
          65530,
          std::nullopt,
          std::nullopt,
          200,
          400),
      bgp_policy::LbwExtCommunityActionType::AGGREGATE_RECEIVED,
      std::make_pair(65530, 400));

  verifyLbwAction(
      createLbwActionData(
          std::make_pair(65530, 100),
          65530,
          std::nullopt,
          std::nullopt,
          200,
          std::nullopt),
      bgp_policy::LbwExtCommunityActionType::AGGREGATE_RECEIVED,
      std::nullopt);

  verifyLbwAction(
      createLbwActionData(
          std::nullopt, 65530, std::nullopt, std::nullopt, 200, 400),
      bgp_policy::LbwExtCommunityActionType::AGGREGATE_RECEIVED,
      std::make_pair(65530, 400));

  verifyLbwAction(
      createLbwActionData(
          std::nullopt, 65530, std::nullopt, std::nullopt, 200, std::nullopt),
      bgp_policy::LbwExtCommunityActionType::AGGREGATE_RECEIVED,
      std::nullopt);

  nsf_policy::NsfTeWeightEncoding encoding;
  encoding.l2_encoding() = nsf_policy::NsfL2TeWeightEncoding();
  encoding.l2_encoding()->rack_id() = 4;
  encoding.l2_encoding()->plane_id() = 4;
  encoding.l2_encoding()->remote_rack_capacity() = 8;
  encoding.l2_encoding()->spine_capacity() = 8;
  encoding.l2_encoding()->local_rack_capacity() = 8;

  auto verifyLbwEncodeAction =
      [&](
          // (input) lbw action data
          BgpPolicyActionData policyActionData,
          // (input) lbw action type
          const bgp_policy::LbwExtCommunityActionType type,
          std::optional<int32_t> encodingId,
          // (expected-output) expected postfilter lbw
          std::optional<std::pair<uint16_t, uint32_t>> expectedLbwOut) {
        // 1. create attrs
        auto attrFields = buildBgpPathFields(0, 0, 0, 0);
        auto attrs = std::make_shared<BgpPath>(*attrFields);

        // 2. create an action
        auto bgpAction =
            createBgpPolicyLbwExtCommunityAction(type, encoding, encodingId);
        const string policyName = "Policy Statement";
        const auto& policyConfig =
            createBgpPolicies(policyName, {}, {bgpAction});
        PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());
        const auto& policy = policyManager.getPolicyFromName(policyName);
        const auto& terms = policy->getPolicyTerms();
        const auto& action = terms[0]->getPolicyActions()[0];

        // 3. apply action
        action->applyAction(
            attrs, std::make_shared<BgpPolicyActionData>(policyActionData));

        // 4. verify lbwOut
        if (!expectedLbwOut.has_value()) {
          EXPECT_FALSE(attrs->hasNonTransitiveLbwExtCommunity());
          XLOG(DBG1, "lbwOut has no lbw");
        } else {
          EXPECT_TRUE(attrs->hasNonTransitiveLbwExtCommunity());
          EXPECT_EQ(attrs->getNonTransitiveLbwAsn(), expectedLbwOut->first);
          EXPECT_EQ(
              attrs->getNonTransitiveRawLbwValue(), expectedLbwOut->second);
          XLOGF(
              DBG1,
              "lbwOut asn:{}, lbw: {}",
              expectedLbwOut->first,
              expectedLbwOut->second);
        }
      };

  /*
   * action: ENCODE_AGGREGATE_RECEIVED_OVERWRITE (advertise-only)
   * encode aggregate-received standard lbw and overwrite lbw if received lbw
   * from all peers otherwise prune lbw
   */
  verifyLbwEncodeAction(
      createLbwActionData(
          std::make_pair(65530, 100),
          65530,
          std::nullopt,
          std::nullopt,
          200,
          8),
      bgp_policy::LbwExtCommunityActionType::
          ENCODE_AGGREGATE_RECEIVED_OVERWRITE,
      2,
      std::make_pair(65530, 8 << 8));

  verifyLbwEncodeAction(
      createLbwActionData(
          std::make_pair(65530, 100),
          65530,
          std::nullopt,
          std::nullopt,
          200,
          std::nullopt),
      bgp_policy::LbwExtCommunityActionType::
          ENCODE_AGGREGATE_RECEIVED_OVERWRITE,
      2,
      std::nullopt);

  verifyLbwEncodeAction(
      createLbwActionData(
          std::nullopt, 65530, std::nullopt, std::nullopt, 200, 8),
      bgp_policy::LbwExtCommunityActionType::
          ENCODE_AGGREGATE_RECEIVED_OVERWRITE,
      2,
      std::make_pair(65530, 8 << 8));

  verifyLbwEncodeAction(
      createLbwActionData(
          std::nullopt, 65530, std::nullopt, std::nullopt, 200, std::nullopt),
      bgp_policy::LbwExtCommunityActionType::
          ENCODE_AGGREGATE_RECEIVED_OVERWRITE,
      2,
      std::nullopt);

  /*
   * action: ENCODE_MULTIPATH (advertise-only)
   * encode size of multipath and modify encoded lbw
   */
  verifyLbwEncodeAction(
      createLbwActionData(std::nullopt, 65530, std::nullopt, 8),
      bgp_policy::LbwExtCommunityActionType::ENCODE_MULTIPATH,
      2,
      std::make_pair(65530, 8 << 8));

  EXPECT_DEATH(
      verifyLbwEncodeAction(
          createLbwActionData(std::nullopt, 65530),
          bgp_policy::LbwExtCommunityActionType::ENCODE_MULTIPATH,
          2,
          std::make_pair(65530, 8 << 8)),
      "multiPathSize unset for ENCODE_MULTIPATH");

  /*
   * action: ENCODE_SWITCH_ID (advertise-only)
   * encode switch_id and modify encoded lbw
   */
  verifyLbwEncodeAction(
      createLbwActionData(std::nullopt, 65530, 8),
      bgp_policy::LbwExtCommunityActionType::ENCODE_SWITCH_ID,
      2,
      std::make_pair(65530, 8 << 8));

  EXPECT_DEATH(
      verifyLbwEncodeAction(
          createLbwActionData(std::nullopt, 65530),
          bgp_policy::LbwExtCommunityActionType::ENCODE_SWITCH_ID,
          2,
          std::make_pair(65530, 8 << 8)),
      "switchId unset for ENCODE_SWITCH_ID");

  /*
   * action: DECODE_AGGREGATE_CAPACITY_OVERWRITE (advertise-only)
   * decode all capacity values and overwrite agg capacity as standard lbw
   */
  union {
    uint32_t intVal;
    float floatVal;
  } tmp1{// the 8th - 15th bits are 2
         // the ucmp lbw is 2
         .intVal = 0b01001110100101010000001011111001};

  union {
    uint32_t intVal;
    float floatVal;
  } tmp2{.floatVal = 2.0f};

  verifyLbwEncodeAction(
      createLbwActionData(
          std::make_pair(65510, tmp1.floatVal),
          65530,
          std::nullopt,
          std::nullopt),
      bgp_policy::LbwExtCommunityActionType::
          DECODE_AGGREGATE_CAPACITY_OVERWRITE,
      std::nullopt,
      std::make_pair(65530, tmp2.intVal));

  // if original asn and lbw is null, then lbw will be 0
  verifyLbwEncodeAction(
      createLbwActionData(std::nullopt, 65530, std::nullopt, std::nullopt),
      bgp_policy::LbwExtCommunityActionType::
          DECODE_AGGREGATE_CAPACITY_OVERWRITE,
      std::nullopt,
      std::make_pair(65530, 0.0f));
}

TEST_F(PolicyTest, CommunityListAddAndRemoveTest) {
  /*
   * Verify that in one term we can add and remove communities
   * Create a catch all term with
   *    ADD kCommunity2 and
   *    REMOVE kCommunity3
   * Input attributes with (kCommunity1, kCommunity3)
   * Result should have (kCommunity1, kCommunity2)
   */
  auto bgpAction1 = createBgpPolicyCommunityAction(
      bgp_policy::CommunityActionType::ADD, {kCommunity2});
  auto bgpAction2 = createBgpPolicyCommunityAction(
      bgp_policy::CommunityActionType::REMOVE, {kCommunity3});
  const string policyName = "Policy Statement";
  const auto& policyConfig =
      createBgpPolicies(policyName, {}, {bgpAction1, bgpAction2});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());

  // Create input (attributes, prefix)
  auto attrFields = buildBgpPathFields(0, 0, 0, 0);
  auto attrs = std::make_shared<BgpPath>(*attrFields);
  attrs->setCommunities(createBgpAttrCommunitiesC({kCommunity1, kCommunity3}));
  std::vector<folly::CIDRNetwork> prefixSetIn{kV6Prefix1, kV6Prefix2};

  // Apply policy
  PolicyInMessage policyIn(prefixSetIn, attrs);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  // Verify both the prefixes are permitted
  ASSERT_EQ(prefixSetIn.size(), policyOut.result.size());
  // Verify both prefixes share same BgpPath pointer (shallow compare)
  EXPECT_EQ(
      policyOut.result[prefixSetIn[0]]->attrs,
      policyOut.result[prefixSetIn[1]]->attrs);
  // Verify no new attributes are created (shallow compare)
  auto attrsOut = policyOut.result[prefixSetIn[0]];
  EXPECT_EQ(attrs, attrsOut->attrs);

  /*
   * Verify that after applying actions
   * kCommunity1 is retained, kCommunity2 is added, kCommunity3 is removed
   */
  EXPECT_THAT(
      ConvertAttrCommunitiesToStrings(attrsOut->attrs->getCommunities().get()),
      testing::UnorderedElementsAreArray({kCommunity1, kCommunity2}));
}

TEST_F(PolicyTest, CommunityListNegativeTest) {
  // Prepare correct Community action
  bgp_policy::BgpPolicyAction correctAction;
  *correctAction.type() = BgpPolicyActionType::COMMUNITY_LIST;
  bgp_policy::CommunityAction commAction;
  *commAction.action_type() = bgp_policy::CommunityActionType::SET;
  commAction.communities() = std::vector<std::string>{kCommunity1};
  correctAction.community_action() = commAction;
  {
    /*
     * Verify we do not accept community list action type
     * if community list is not set
     */
    auto incorrectAction = correctAction;
    incorrectAction.community_action().reset();
    const auto& expectedStr = "BgpPolicyAction Config input error for type: 2";
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
  {
    /*
     * Verify we do not accept invalid community input.
     * As invalid community string could be a valid regEx, we cannot
     * always differentiate between invalid regEx vs invalid community string,
     * so expected error is regex not allowed.
     */
    auto incorrectAction = correctAction;
    incorrectAction.community_action()->communities() =
        std::vector<std::string>{kCommunity1, "abcd"};
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(
        "Community action with regex allowed only for "
        "CommunityActionType::REMOVE. action_name: , action_type: SET"
        ", regex patterns: abcd",
        receivedStr->c_str());

    incorrectAction.community_action()->communities() =
        std::vector<std::string>{kCommunity1, "12389898:123"}; // Overflow
    const auto& receivedStr2 = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr2);
    EXPECT_STREQ(
        "Community action with regex allowed only for "
        "CommunityActionType::REMOVE. action_name: , action_type: SET"
        ", regex patterns: 12389898:123",
        receivedStr2->c_str());
  }
  {
    // Verify we accept if communities are not set (std::nullopt)
    auto emptyCommunityAction = correctAction;
    emptyCommunityAction.community_action()->communities().reset();
    const auto& receivedStr = parseActionConfigGetError(emptyCommunityAction);
    ASSERT_FALSE(receivedStr);
  }
  {
    // Verify we accept empty communities (without any values)
    auto emptyCommunityAction = correctAction;
    emptyCommunityAction.community_action()->communities().reset();
    const auto& receivedStr = parseActionConfigGetError(emptyCommunityAction);
    ASSERT_FALSE(receivedStr);
  }
  {
    // Verify we do not accept community regEx for action type ADD
    auto incorrectAction = correctAction;
    *incorrectAction.community_action()->action_type() =
        bgp_policy::CommunityActionType::ADD;
    incorrectAction.community_action()->communities() =
        std::vector<std::string>{kCommunityRegex1, kCommunity1};
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(
        "Community action with regex allowed only for "
        "CommunityActionType::REMOVE. action_name: , action_type: ADD"
        ", regex patterns: 65500:1.*",
        receivedStr->c_str());
  }
}

TEST_F(PolicyTest, ExtCommunityActionTest) {
  bgp_policy::ExtCommunity ext = createExtCommunity(
      0x40, // type_high
      0x04, // type_low
      "100G");
  // Case 1: add action
  {
    auto addAction = createBgpPolicyExtCommunityAction(
        bgp_policy::BgpAttrChangeActionType::EXT_COMMUNITY_LIST_ADD, {ext});
    EXPECT_FALSE(parseActionConfigGetError(addAction));
  }

  // Case 2: remove action
  {
    auto removeAction = createBgpPolicyExtCommunityAction(
        bgp_policy::BgpAttrChangeActionType::EXT_COMMUNITY_LIST_REMOVE, {ext});
    const auto& ret = parseActionConfigGetError(removeAction);
    EXPECT_FALSE(ret);
  }

  // Case 3. set action
  {
    // a: Empty ext communities list.
    auto setEmptyAction = createBgpPolicyExtCommunityAction(
        bgp_policy::BgpAttrChangeActionType::EXT_COMMUNITY_LIST_SET,
        {} /* communities */);
    EXPECT_FALSE(parseActionConfigGetError(setEmptyAction));

    // Case b: Non-empty ext communities list.
    auto setAction = createBgpPolicyExtCommunityAction(
        bgp_policy::BgpAttrChangeActionType::EXT_COMMUNITY_LIST_SET, {ext});
    EXPECT_FALSE(parseActionConfigGetError(setAction));
  }
  // Case 4: Unimplemented action_type action
  {
    auto badAction = createBgpPolicyExtCommunityAction(
        bgp_policy::BgpAttrChangeActionType::COMMUNITY_LIST_SET,
        {} /* communities */);
    const auto& ret = parseActionConfigGetError(badAction);
    EXPECT_TRUE(ret);
    EXPECT_EQ(
        "Unexpected BgpAttrChangeActionType COMMUNITY_LIST_SET for ExtCommunityAction",
        *ret);
  }
  // Case 5: EXT_COMMUNITY_LIST is set but ExtCommunityAction is not set.
  {
    bgp_policy::BgpPolicyAction badAction;
    badAction.type() = bgp_policy::BgpPolicyActionType::EXT_COMMUNITY_LIST;
    const auto& ret = parseActionConfigGetError(badAction);
    EXPECT_TRUE(ret);
    EXPECT_EQ(
        "Expected ExtCommunityAction set on BgpPolicyAction for EXT_COMMUNITY_LIST action type.",
        *ret);
  }
}

// Test ExtCommunityAction::validateActionType
TEST_F(PolicyTest, ExtCommunityActionValidateActionTypeTest) {
  bgp_policy::ExtCommunity ext = createExtCommunity(
      0x40, // type_high
      0x04, // type_low
      "100G");

  // Case 1: Valid action type - EXT_COMMUNITY_LIST_ADD
  {
    auto addAction = createBgpPolicyExtCommunityAction(
        bgp_policy::BgpAttrChangeActionType::EXT_COMMUNITY_LIST_ADD, {ext});
    // Validation happens during construction
    EXPECT_NO_THROW(std::make_shared<ExtCommunityAction>(addAction));
  }

  // Case 2: Valid action type - EXT_COMMUNITY_LIST_SET
  {
    auto setAction = createBgpPolicyExtCommunityAction(
        bgp_policy::BgpAttrChangeActionType::EXT_COMMUNITY_LIST_SET, {ext});
    // Validation happens during construction
    EXPECT_NO_THROW(std::make_shared<ExtCommunityAction>(setAction));
  }

  // Case 3: Valid action type - EXT_COMMUNITY_LIST_REMOVE
  {
    auto removeAction = createBgpPolicyExtCommunityAction(
        bgp_policy::BgpAttrChangeActionType::EXT_COMMUNITY_LIST_REMOVE, {ext});
    // Validation happens during construction
    EXPECT_NO_THROW(std::make_shared<ExtCommunityAction>(removeAction));
  }

  // Case 4: Invalid action type - COMMUNITY_LIST_ADD
  {
    auto badAction = createBgpPolicyExtCommunityAction(
        bgp_policy::BgpAttrChangeActionType::COMMUNITY_LIST_ADD, {});
    const auto& ret = parseActionConfigGetError(badAction);
    EXPECT_TRUE(ret);
    EXPECT_EQ(
        "Unexpected BgpAttrChangeActionType COMMUNITY_LIST_ADD for ExtCommunityAction",
        *ret);
  }

  // Case 5: Invalid action type - COMMUNITY_LIST_SET
  {
    auto badAction = createBgpPolicyExtCommunityAction(
        bgp_policy::BgpAttrChangeActionType::COMMUNITY_LIST_SET, {});
    const auto& ret = parseActionConfigGetError(badAction);
    EXPECT_TRUE(ret);
    EXPECT_EQ(
        "Unexpected BgpAttrChangeActionType COMMUNITY_LIST_SET for ExtCommunityAction",
        *ret);
  }

  // Case 6: Invalid action type - COMMUNITY_LIST_REMOVE
  {
    auto badAction = createBgpPolicyExtCommunityAction(
        bgp_policy::BgpAttrChangeActionType::COMMUNITY_LIST_REMOVE, {});
    const auto& ret = parseActionConfigGetError(badAction);
    EXPECT_TRUE(ret);
    EXPECT_EQ(
        "Unexpected BgpAttrChangeActionType COMMUNITY_LIST_REMOVE for ExtCommunityAction",
        *ret);
  }
}

// Test apply policy with terms having prefix list
TEST_F(PolicyTest, applyPolicyWithPrefixList) {
  /*
   * Create a policy with five terms
   * Term1 match kV4Prefix1, kV4Prefix2 and apply origin action(IGP)
   * Term2 match kV4Prefix3 and discard
   * Term3 match kV4Prefix4 and apply origin action(EGP)
   * Term4 match kV4Prefix5 which doesn't match any of input prefix
   *       and apply origin action(EGP). This will cover case where
   *       a term with prefix list doesn't match any prefixes
   * Term5 match kV4Prefix6 and PERMIT (no attribute modification)
   * kV4Prefix7 Doesn't match any term and discard
   */

  // Creating TERM1 (match kV4Prefix1, kV4Prefix2 and apply origin action(IGP))
  routing_policy::CompareNumericValue compareStructEQ;
  *compareStructEQ.compare_operator() = routing_policy::ComparisonOperator::EQ;
  *compareStructEQ.value() = kV4Prefix1.second;
  const auto& prefixListEntry1 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix1), {compareStructEQ});

  *compareStructEQ.value() = kV4Prefix2.second;
  const auto& prefixListEntry2 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix2), {compareStructEQ});
  const auto& match1 =
      createPrefixListMatch({prefixListEntry1, prefixListEntry2});
  auto actionIgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::IGP);
  auto term1 = createBgpPolicyTerm("Term1", "", {match1}, {actionIgp});

  // Creating TERM2 (match kV4Prefix3 and discard)
  *compareStructEQ.value() = kV4Prefix3.second;
  const auto& prefixListEntry3 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix3), {compareStructEQ});
  const auto& match2 = createPrefixListMatch({prefixListEntry3});
  auto actionDeny = createBgpPolicyAction(BgpPolicyActionType::DENY);
  auto term2 = createBgpPolicyTerm("Term2", "", {match2}, {actionDeny});

  // Creating TERM3 (match kV4Prefix4 and apply origin action(EGP))
  *compareStructEQ.value() = kV4Prefix4.second;
  const auto& prefixListEntry4 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix4), {compareStructEQ});
  const auto& match3 = createPrefixListMatch({prefixListEntry4});
  auto actionEgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::EGP);
  auto term3 = createBgpPolicyTerm("Term3", "", {match3}, {actionEgp});

  /*
   * Creating TERM4 (match kV4Prefix5 and apply origin action(EGP))
   * This term doesn't match any input prefixes
   * Verify it doesn't effect kV4Prefix6 in any way
   */
  *compareStructEQ.value() = kV4Prefix5.second;
  const auto& prefixListEntry5 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix5), {compareStructEQ});
  const auto& match4 = createPrefixListMatch({prefixListEntry5});
  auto term4 = createBgpPolicyTerm("Term4", "", {match4}, {actionEgp});

  // Creating TERM5 (match kV4Prefix6 and PERMIT (no attribute modification))
  *compareStructEQ.value() = kV4Prefix6.second;
  const auto& prefixListEntry6 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix6), {compareStructEQ});
  const auto& match5 = createPrefixListMatch({prefixListEntry6});
  auto actionPermit = createBgpPolicyAction(BgpPolicyActionType::PERMIT);
  auto term5 = createBgpPolicyTerm("Term5", "", {match5}, {actionPermit});

  // Create policy
  const string policyName = "Policy Statement";
  const auto& policyConfig =
      createBgpPolicies(policyName, {term1, term2, term3, term4, term5});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());

  // NOTE there is no kV4Prefix5 in input
  std::vector<folly::CIDRNetwork> prefixSetIn{
      kV4Prefix1, kV4Prefix2, kV4Prefix3, kV4Prefix4, kV4Prefix6, kV4Prefix7};

  // Atributes with incomplete origin
  auto attrsIn = createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE);
  attrsIn->publish();

  // Apply policy
  PolicyInMessage policyIn(prefixSetIn, attrsIn);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  /*
   * Verify that a total of 4 prefixes are permitted
   * term2 discards kV4Prefix3, kV4Prefix6 will be discard due to catch all
   */
  int count = 0;
  for (auto& kv : policyOut.result) {
    const auto& str = kv.second->policyName;
    if (str.find("Accepted") != std::string::npos) {
      count++;
    }
  }
  ASSERT_EQ(4, count);

  /*
   * Verify TERM1 actions
   * Verify kV4Prefix1, kV4Prefix2 are permitted and have IGP origin
   */
  ASSERT_NE(policyOut.result.find(kV4Prefix1), policyOut.result.end());
  ASSERT_NE(policyOut.result.find(kV4Prefix2), policyOut.result.end());
  // Verify new attributes are created (set action)
  EXPECT_NE(attrsIn, policyOut.result[kV4Prefix1]->attrs);
  EXPECT_NE(attrsIn, policyOut.result[kV4Prefix2]->attrs);
  // Verify kV4Prefix1, kV4Prefix2 shared same attributes
  EXPECT_EQ(
      policyOut.result[kV4Prefix1]->attrs, policyOut.result[kV4Prefix2]->attrs);
  auto attrsOut = policyOut.result[kV4Prefix1];
  // Verify attributes are unpublished
  EXPECT_FALSE(attrsOut->attrs->isPublished());
  // Verify origin action is properly applied (1st Term's action)
  EXPECT_EQ(attrsOut->attrs->getOrigin(), BgpAttrOrigin::BGP_ORIGIN_IGP);

  /*
   * Verify TERM2 actions
   * Verify kV4Prefix3 is discarded
   */
  EXPECT_NE(policyOut.result.find(kV4Prefix3), policyOut.result.end());

  /*
   * Verify TERM3 actions
   * Verify kV4Prefix4 accepted and origin action(EGP) applied
   */
  ASSERT_NE(policyOut.result.find(kV4Prefix4), policyOut.result.end());
  EXPECT_NE(attrsIn, policyOut.result[kV4Prefix4]->attrs);
  attrsOut = policyOut.result[kV4Prefix4];
  // Verify attributes are unpublished
  EXPECT_FALSE(attrsOut->attrs->isPublished());
  // Verify origin action is properly applied (3rd Term's action)
  EXPECT_EQ(attrsOut->attrs->getOrigin(), BgpAttrOrigin::BGP_ORIGIN_EGP);

  /*
   * Verify TERM5 actions
   * Verify kV4Prefix6 accepted and no attribute modification
   */
  ASSERT_NE(policyOut.result.find(kV4Prefix6), policyOut.result.end());
  EXPECT_EQ(attrsIn, policyOut.result[kV4Prefix6]->attrs);
  attrsOut = policyOut.result[kV4Prefix6];
  EXPECT_TRUE(attrsOut->attrs->isPublished());

  /*
   * Verify catch all term (default)
   * Verify kV4Prefix7 discarded
   */
  EXPECT_NE(policyOut.result.find(kV4Prefix7), policyOut.result.end());
}

/*
 * Test policy with a LogAndAccept action as a miss
 * TODO: logging part will be tested in future diff
 */
TEST_F(PolicyTest, applyPolicyLogAndAccept) {
  /*
   * kV4Prefix1 will be denied and kV4Prefix2 will be accepted
   * - Term1 matches kV4Prefix2 (match = next, miss = LogAndAccept)
   * (Default deny)
   */
  routing_policy::CompareNumericValue compareStructEQ;
  *compareStructEQ.compare_operator() = routing_policy::ComparisonOperator::EQ;
  *compareStructEQ.value() = kV4Prefix1.second;
  const auto& prefixListEntry1 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix1), {compareStructEQ});

  const auto& match1 = createPrefixListMatch({prefixListEntry1});
  auto actionContinue = createBgpPolicyAction(BgpPolicyActionType::CONTINUE);
  auto term1 = createBgpPolicyTerm(
      "Term1 - Match P1 (next_term), Miss p2 (log_n_accept)",
      "",
      {match1},
      {actionContinue},
      bgp_policy::FlowControlAction::LOG_AND_ACCEPT);

  // Create policy composed of the three terms.
  const string policyName = "Policy Statement";
  const auto& policyConfig = createBgpPolicies(policyName, {term1});
  PolicyManager policyManager{policyConfig, createTestBgpGlobalConfig()};

  // Prefixes to be evaluated against policy..
  std::vector<folly::CIDRNetwork> prefixSetIn{kV4Prefix1, kV4Prefix2};

  auto attrsIn = createBgpPathWithOrigin();
  attrsIn->publish();

  // Apply policy.
  PolicyInMessage policyIn(prefixSetIn, attrsIn);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  ASSERT_EQ(policyOut.result[kV4Prefix1]->attrs, nullptr); // deny
  ASSERT_EQ(policyOut.result[kV4Prefix2]->attrs, attrsIn); // permit
}

/*
 * Test policy with a LogAndDeny action as a miss
 * TODO: logging part will be tested in future diff
 */
TEST_F(PolicyTest, applyPolicyLogAndDeny) {
  /*
   * kV4Prefix1 will be denied and kV4Prefix2 will be accepted
   * - Term1 matches kV4Prefix2 (match = accept, miss = log and deny)
   * - Term2 accepts kV4Prefix1
   */

  routing_policy::CompareNumericValue compareStructEQ;
  *compareStructEQ.compare_operator() = routing_policy::ComparisonOperator::EQ;
  *compareStructEQ.value() = kV4Prefix1.second;
  const auto& prefixListEntry1 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix1), {compareStructEQ});
  *compareStructEQ.value() = kV4Prefix2.second;
  const auto& prefixListEntry2 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix2), {compareStructEQ});

  const auto& match1 = createPrefixListMatch({prefixListEntry1});
  const auto& match2 = createPrefixListMatch({prefixListEntry2});
  auto actionPermit = createBgpPolicyAction(BgpPolicyActionType::PERMIT);
  auto term1 = createBgpPolicyTerm(
      "Term1 - Accept P2, Missed prefix (P1) will be denied",
      "",
      {match2},
      {actionPermit},
      bgp_policy::FlowControlAction::LOG_AND_DENY);

  auto term2 = createBgpPolicyTerm(
      "Term2 - Accept P1",
      "",
      {match1},
      {actionPermit},
      bgp_policy::FlowControlAction::DENY);

  // Create policy composed of the three terms.
  const string policyName = "Policy Statement";
  const auto& policyConfig = createBgpPolicies(policyName, {term1, term2});
  PolicyManager policyManager{policyConfig, createTestBgpGlobalConfig()};

  // Prefixes to be evaluated against policy..
  std::vector<folly::CIDRNetwork> prefixSetIn{kV4Prefix1, kV4Prefix2};

  auto attrsIn = createBgpPathWithOrigin();
  attrsIn->publish();

  // Apply policy.
  PolicyInMessage policyIn(prefixSetIn, attrsIn);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  ASSERT_EQ(policyOut.result[kV4Prefix1]->attrs, nullptr); // deny
  ASSERT_EQ(policyOut.result[kV4Prefix2]->attrs, attrsIn); // permit
}

/*
 * Test policy with terms having prefix-list based match, where each term's
 * miss acction is permissive - continue evaluating but log prefixes that
 * didn't match the term.
 */
TEST_F(PolicyTest, applyPermissivePolicyWithPrefixList) {
  /*
   * Evaluate kV4Prefix1 through kV4Prefix5 against test policy.
   * Test policy has four terms, where each term's miss action is log and
   * continue evaluating against next term.
   * - Term1 matches kV4Prefix1 through kV4Prefix4 and continues evaluation
   *   against term2 on match. Unmatched kV4Prefix5 will get logged, but all
   *   5 will continue evaluation against term2
   * - Term2 matches kV4Prefix1 and applies origin action (IGP). The
   *   4 unmatched prefixes will get logged and continue evaluation against
   *   term3.
   * - Term3 matches kV4Prefix2 and kV4Prefix3 and applies discard action. The
   *   remaining two prefixes will be logged and continue evaluation against
   *   term4.
   * - Term4 matches kV4Prefix4 and applies permit action (without any attribute
   *   modification). The last remaing prefix will be logged and get discarded
   *   (since no more terms).
   */

  /*
   * Creating TERM1 - match kV4Prefix1 through kV4Prefix4 and continue
   * evaluating to next term.
   */
  routing_policy::CompareNumericValue compareStructEQ;
  *compareStructEQ.compare_operator() = routing_policy::ComparisonOperator::EQ;
  *compareStructEQ.value() = kV4Prefix1.second;
  const auto& prefixListEntry1 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix1), {compareStructEQ});
  *compareStructEQ.value() = kV4Prefix2.second;
  const auto& prefixListEntry2 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix2), {compareStructEQ});
  *compareStructEQ.value() = kV4Prefix3.second;
  const auto& prefixListEntry3 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix3), {compareStructEQ});
  *compareStructEQ.value() = kV4Prefix4.second;
  const auto& prefixListEntry4 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix4), {compareStructEQ});

  const auto& match1 = createPrefixListMatch(
      {prefixListEntry1, prefixListEntry2, prefixListEntry3, prefixListEntry4});
  auto actionContinue = createBgpPolicyAction(BgpPolicyActionType::CONTINUE);
  auto term1 = createBgpPolicyTerm(
      "Term1 - Registry filter",
      "",
      {match1},
      {actionContinue},
      bgp_policy::FlowControlAction::LOG_AND_NEXT_TERM);

  // Creating TERM2 - match kV4Prefix1 and apply origin action (IGP).
  const auto& match2 = createPrefixListMatch({prefixListEntry1});
  auto actionIgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::IGP);
  auto term2 = createBgpPolicyTerm(
      "Term2",
      "",
      {match2},
      {actionIgp},
      bgp_policy::FlowControlAction::LOG_AND_NEXT_TERM);

  // Creating TERM3 - match kV4Prefix2 and kV4Prefix3 and discard.
  const auto& match3 =
      createPrefixListMatch({prefixListEntry2, prefixListEntry3});
  auto actionDeny = createBgpPolicyAction(BgpPolicyActionType::DENY);
  auto term3 = createBgpPolicyTerm(
      "Term3",
      "",
      {match3},
      {actionDeny},
      bgp_policy::FlowControlAction::LOG_AND_NEXT_TERM);

  // Creating TERM4 - match kV4Prefix4 and PERMIT (no attribute modification).
  const auto& match4 = createPrefixListMatch({prefixListEntry4});
  auto actionPermit = createBgpPolicyAction(BgpPolicyActionType::PERMIT);
  auto term4 = createBgpPolicyTerm(
      "Term4",
      "",
      {match4},
      {actionPermit},
      bgp_policy::FlowControlAction::LOG_AND_NEXT_TERM);

  // Create policy composed of the three terms.
  const string policyName = "Policy Statement";
  const auto& policyConfig =
      createBgpPolicies(policyName, {term1, term2, term3, term4});
  PolicyManager policyManager{policyConfig, createTestBgpGlobalConfig()};

  // Prefixes to be evaluated against policy..
  std::vector<folly::CIDRNetwork> prefixSetIn{
      kV4Prefix1, kV4Prefix2, kV4Prefix3, kV4Prefix4, kV4Prefix5};

  // Atributes with incomplete origin.
  auto attrsIn = createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE);
  attrsIn->publish();

  // Apply policy.
  PolicyInMessage policyIn(prefixSetIn, attrsIn);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  /*
   * Verify that a total of 2 prefixes are permitted.
   * - term3 discards kV4Prefix2 and kV4Prefix3,
   * - kV4Prefix5 will be discard due to not matching any (non-Continue) term.
   */
  int count = 0;
  for (auto& kv : policyOut.result) {
    const auto& str = kv.second->policyName;
    if (str.find("Accepted") != std::string::npos) {
      count++;
    }
  }
  ASSERT_EQ(2, count);

  // Verify TERM2 actions.

  // Verify kV4Prefix1 is permitted and has IGP origin.
  ASSERT_NE(policyOut.result.find(kV4Prefix1), policyOut.result.end());
  // Verify new attributes are created (set action)
  EXPECT_NE(attrsIn, policyOut.result[kV4Prefix1]->attrs);
  // Verify attributes are unpublished
  auto attrsOut = policyOut.result[kV4Prefix1];
  EXPECT_FALSE(attrsOut->attrs->isPublished());
  // Verify origin action is properly applied (1st Term's action)
  EXPECT_EQ(attrsOut->attrs->getOrigin(), BgpAttrOrigin::BGP_ORIGIN_IGP);

  // Verify TERM3 actions.

  // Verify kV4Prefix2 and kV4Prefix3 are discarded.
  EXPECT_NE(policyOut.result.find(kV4Prefix2), policyOut.result.end());
  EXPECT_NE(policyOut.result.find(kV4Prefix3), policyOut.result.end());

  // Verify TERM4 actions.

  // Verify kV4Prefix6 accepted and no attribute modification
  ASSERT_NE(policyOut.result.find(kV4Prefix4), policyOut.result.end());
  EXPECT_EQ(attrsIn, policyOut.result[kV4Prefix4]->attrs);
  attrsOut = policyOut.result[kV4Prefix4];
  EXPECT_TRUE(attrsOut->attrs->isPublished());

  // Verify default discard action.

  /*
   * Verify kV4Prefix5 discarded due to not matchig any non-Continue action
   * terms.
   */
  EXPECT_NE(policyOut.result.find(kV4Prefix5), policyOut.result.end());
}

/*
 * Evaluate a prefix (9.0.0.0/26) against a prefix list with two entries, that
 * have the same base prefix (of 9.0.0.0/24). The prefix will match both entry's
 * base prefix. However, first entry's base prefix is 9.0.0.0/24 has a prefix
 * len range specicified as 24, while the second entry's base prefix
 * of 9.0.0.0/24 has prefix len range specified as 26. The expected policy
 * evaluation result is a match since there is matching prefix-list entry with
 * len 26. The actual results is that if the two entry's are in lexicographic
 * order (i.e. 9.0.0.0/24 with len 24 followed by 9.0.0.0/24 with len 26) in the
 * prefix-list, the match fails. However, if the entry's are in the reverse
 * order (9.0.0.0/24 with len 26 followed by 9.0.0.0/24 with len 24) match
 * succeeds.
 */
TEST_F(PolicyTest, verifyMatchAnyOpForPrefixList) {
  routing_policy::CompareNumericValue compareStructEQ;
  *compareStructEQ.compare_operator() = routing_policy::ComparisonOperator::EQ;
  *compareStructEQ.value() = kV4Prefix8_0Slash24.second;
  const auto& prefixListEntry1 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix8_0Slash24), {compareStructEQ});
  *compareStructEQ.value() = kV4Prefix8_0Slash26.second;
  /*
   * This base prefix is purposefully /24, as the entry is simulating to permit
   * all /26 prefixes within this /24.
   */
  const auto& prefixListEntry2 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix8_0Slash24), {compareStructEQ});

  const auto& lexicographicMatch =
      createPrefixListMatch({prefixListEntry1, prefixListEntry2});
  auto actionPermit = createBgpPolicyAction(BgpPolicyActionType::PERMIT);
  auto term = createBgpPolicyTerm(
      "Policy Term",
      "",
      {lexicographicMatch},
      {actionPermit},
      bgp_policy::FlowControlAction::DENY);

  // Create policy composed of the term.
  const string policyName = "Policy Statement";
  const auto& policyConfig = createBgpPolicies(policyName, {term});
  PolicyManager policyManager{policyConfig, createTestBgpGlobalConfig()};

  // Prefix to be evaluated against policy..
  std::vector<folly::CIDRNetwork> prefixSetIn{kV4Prefix8_0Slash26};

  // Atributes with incomplete origin.
  auto attrsIn = createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE);
  attrsIn->publish();

  // Apply policy.
  PolicyInMessage policyIn(prefixSetIn, attrsIn);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  int count = 0;
  for (auto& kv : policyOut.result) {
    const auto& str = kv.second->policyName;
    if (str.find("Accepted") != std::string::npos) {
      count++;
    }
  }
  ASSERT_EQ(1, count);

  const auto& reverseLexicographicMatch =
      createPrefixListMatch({prefixListEntry2, prefixListEntry1});
  auto reverseMatchTerm = createBgpPolicyTerm(
      "Policy Term - new",
      "",
      {reverseLexicographicMatch},
      {actionPermit},
      bgp_policy::FlowControlAction::DENY);

  // Create policy composed of the new term.
  const auto& newPolicyConfig =
      createBgpPolicies(policyName, {reverseMatchTerm});
  PolicyManager newPolicyManager{newPolicyConfig, createTestBgpGlobalConfig()};

  // Apply policy.
  auto reversePolicyOut = newPolicyManager.applyPolicy(policyName, policyIn);

  count = 0;
  for (auto& kv : reversePolicyOut.result) {
    const auto& str = kv.second->policyName;
    if (str.find("Accepted") != std::string::npos) {
      count++;
    }
  }
  ASSERT_EQ(1, count);
}

/*
 * Test prefix list with additional match conditions
 * Verify that both match-conditions must match (matching one is not sufficient)
 */
TEST_F(PolicyTest, applyPolicyWithPrefixListAndCommunity) {
  /*
   * Create a term with prefix-list and community matches
   * Verify that only if both are matched, will action be taken
   */
  routing_policy::CompareNumericValue compareStructEQ;
  *compareStructEQ.compare_operator() = routing_policy::ComparisonOperator::EQ;
  *compareStructEQ.value() = kV4Prefix1.second;
  const auto& prefixListEntry1 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix1), {compareStructEQ});
  const auto& match1 = createPrefixListMatch({prefixListEntry1});
  const auto& match2 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST, {kCommunity1});
  auto actionIgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::IGP);
  auto term = createBgpPolicyTerm("Term", "", {match1, match2}, {actionIgp});

  // Create policy
  const string policyName = "Policy Statement";
  const auto& policyConfig = createBgpPolicies(policyName, {term});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());

  auto attrsComm1 =
      createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE);
  attrsComm1->setCommunities(createBgpAttrCommunitiesC({kCommunity1}));
  attrsComm1->publish();

  auto attrsComm2 =
      createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE);
  attrsComm2->setCommunities(createBgpAttrCommunitiesC({kCommunity2}));
  attrsComm2->publish();

  {
    // CASE 1: Both community and prefix-list match
    std::vector<folly::CIDRNetwork> prefixSetIn{kV4Prefix1};

    // Apply policy
    PolicyInMessage policyIn(prefixSetIn, attrsComm1);
    auto policyOut = policyManager.applyPolicy(policyName, policyIn);

    // Verify actions are applied
    ASSERT_EQ(prefixSetIn.size(), policyOut.result.size());
    ASSERT_NE(policyOut.result.find(prefixSetIn[0]), policyOut.result.end());
    // Verify new attributes are created (set action)
    EXPECT_NE(attrsComm1, policyOut.result[prefixSetIn[0]]->attrs);
    auto attrsOut = policyOut.result[prefixSetIn[0]];
    // Verify attributes are unpublished
    EXPECT_FALSE(attrsOut->attrs->isPublished());
    // Verify origin action is properly applied (1st Term's action)
    EXPECT_EQ(attrsOut->attrs->getOrigin(), BgpAttrOrigin::BGP_ORIGIN_IGP);
  }
  {
    // CASE 2: Only community matches, but prefix list doesn't match
    std::vector<folly::CIDRNetwork> prefixSetIn{kV4Prefix2};

    // Apply policy
    PolicyInMessage policyIn(prefixSetIn, attrsComm1);
    auto policyOut = policyManager.applyPolicy(policyName, policyIn);

    ASSERT_EQ(1, policyOut.result.size());
  }
  {
    // CASE 3: Only prefix matches, but not community
    std::vector<folly::CIDRNetwork> prefixSetIn{kV4Prefix1};

    // Apply policy
    PolicyInMessage policyIn(prefixSetIn, attrsComm2);
    auto policyOut = policyManager.applyPolicy(policyName, policyIn);

    ASSERT_EQ(1, policyOut.result.size());
  }
}

TEST_F(PolicyTest, SetAsPathPrependActionTest) {
  // kAsn1 = 64551, kRepeatedTimes1 = 5
  auto bgpAction = createPolicySetAsPathPrependAction();
  const string policyName = "Policy Statement";
  const auto& policyConfig = createBgpPolicies(policyName, {}, {bgpAction});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());
  const auto& policy = policyManager.getPolicyFromName(policyName);
  const auto& terms = policy->getPolicyTerms();
  const auto& action = terms[0]->getPolicyActions()[0];

  auto attrFields = buildBgpPathFields(0, 0, 0, 0);
  auto attrs = std::make_shared<BgpPath>(*attrFields);

  {
    // test the AS is prepended to the first asSequence
    BgpAttrAsPathC asPath;
    BgpAttrAsPathSegmentC segment1;
    segment1.asSequence = {kAsn2};
    asPath.emplace_back(segment1);
    BgpAttrAsPathSegmentC segment2;
    segment2.asSequence = {kAsn3};
    asPath.emplace_back(segment2);
    // asPath = {64550, 64552}
    attrs->setAsPath(asPath);
    action->applyAction(attrs);

    segment1.asSequence = {kAsn2};
    segment1.asSequence.insert(
        segment1.asSequence.begin(), kRepeatedTimes1, kAsn1);
    BgpAttrAsPathC expectedAsPath = static_cast<BgpAttrAsPathC>(
        std::vector<BgpAttrAsPathSegmentC>{segment1, segment2});
    EXPECT_THAT(*attrs->getAsPath(), testing::ElementsAreArray(expectedAsPath));
  }
  {
    // test the AS is prepended to a new asSequence
    BgpAttrAsPathC asPath;
    BgpAttrAsPathSegmentC segment1;
    segment1.asSet = {kAsn2};
    asPath.emplace_back(segment1);
    // asPath = {64550}
    attrs->setAsPath(asPath);
    action->applyAction(attrs);

    BgpAttrAsPathC expectedAsPath = asPath;
    segment1.asSet = {};
    segment1.asSequence.insert(
        segment1.asSequence.begin(), kRepeatedTimes1, kAsn1);
    expectedAsPath.insert(expectedAsPath.begin(), segment1);
    EXPECT_THAT(*attrs->getAsPath(), testing::ElementsAreArray(expectedAsPath));
  }
  {
    /*
     * test the AS is prepended to both first asSequence and a new asSequence
     * kRepeatedTimes2 = 253
     */
    BgpAttrAsPathC asPath;
    BgpAttrAsPathSegmentC segment1;
    for (int insertTimes = 0; insertTimes < kRepeatedTimes2; insertTimes++) {
      segment1.asSequence.emplace_back(kAsn2);
    }

    asPath.emplace_back(segment1);
    // asPath contains 253 of 64550
    attrs->setAsPath(asPath);
    action->applyAction(attrs);

    BgpAttrAsPathC expectedAsPath = asPath;
    segment1.asSequence = {};
    // insert 2 to the first segment
    expectedAsPath[0].asSequence.insert(
        expectedAsPath[0].asSequence.begin(), 255 - kRepeatedTimes2, kAsn1);

    segment1.asSequence = {};
    // prepend remaining 3 to the new segment
    for (int insertTimes = 0;
         insertTimes < kRepeatedTimes1 - (255 - kRepeatedTimes2);
         insertTimes++) {
      segment1.asSequence.emplace_back(kAsn1);
    }
    expectedAsPath.insert(expectedAsPath.begin(), segment1);
    EXPECT_THAT(*attrs->getAsPath(), testing::ElementsAreArray(expectedAsPath));
  }
}

TEST_F(PolicyTest, SetAsPathPrependNegativeTest) {
  // Prepare setAsPathPrepend action
  bgp_policy::BgpPolicyAction correctAction;
  *correctAction.type() = BgpPolicyActionType::AS_PATH_PREPEND;
  bgp_policy::SetAsPathPrepend tSetAsPathPrependCorrect;
  *tSetAsPathPrependCorrect.asn() = kAsn1;
  *tSetAsPathPrependCorrect.repeat_times() = 2;
  correctAction.set_as_path_prepend() = tSetAsPathPrependCorrect;

  {
    // Verify empty set_as_path_prepend is not accepted
    auto incorrectAction = correctAction;
    incorrectAction.set_as_path_prepend().reset();
    const auto& expectedStr = "BgpPolicyAction Config input error for type: 1";
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
  {
    // Verify invalid asn in set_as_path_prepend
    auto incorrectAction = correctAction;
    *incorrectAction.set_as_path_prepend()->asn() = -1;
    const auto& expectedStr =
        "Malformed SetAsPathPrepend config: set_as_path_prepend.asn = -1";
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
  {
    // Verify invalid repeated_times in set_as_path_prepend
    auto incorrectAction = correctAction;
    *incorrectAction.set_as_path_prepend()->repeat_times() = -1;
    const auto& expectedStr =
        "Malformed SetAsPathPrepend config: repeat_times = -1";
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
  {
    // Verify invalid repeated_times in set_as_path_prepend
    auto incorrectAction = correctAction;
    *incorrectAction.set_as_path_prepend()->repeat_times() = 256;
    const auto& expectedStr =
        "Malformed SetAsPathPrepend config: repeat_times = 256";
    const auto& receivedStr = parseActionConfigGetError(incorrectAction);
    ASSERT_TRUE(receivedStr);
    EXPECT_STREQ(expectedStr, receivedStr->c_str());
  }
}

using AsPath = std::vector<BgpAttrAsPathSegmentC>;

class AsPathToAsSetTest
    : public PolicyTest,
      public testing::WithParamInterface<std::pair<AsPath, AsPath>> {};

TEST_P(AsPathToAsSetTest, Test) {
  // Create action
  auto bgpAction =
      createBgpPolicyAction(BgpPolicyActionType::AS_PATH_TO_AS_SET);
  const string policyName = "Policy Statement";
  const auto& policyConfig = createBgpPolicies(policyName, {}, {bgpAction});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());
  const auto& policy = policyManager.getPolicyFromName(policyName);
  const auto& terms = policy->getPolicyTerms();
  const auto& action = terms[0]->getPolicyActions()[0];

  // Create attributes
  auto attrFields = buildBgpPathFields(0, 0, 0, 0);
  auto attrs = std::make_shared<BgpPath>(*attrFields);
  const auto& [inputAsPath, outputAsPath] = GetParam();
  attrs->setAsPath(static_cast<BgpAttrAsPathC>(inputAsPath));

  // Apply action
  action->applyAction(attrs);

  // Verify result
  EXPECT_THAT(
      attrs->getAsPath().get(), testing::ElementsAreArray(outputAsPath));
}

INSTANTIATE_TEST_SUITE_P(
    AsPathToAsSetTest,
    AsPathToAsSetTest,
    testing::Values(
        // pair {input path, output path}
        std::pair{AsPath{}, AsPath{}},
        std::pair{
            AsPath{BgpAttrAsPathSegmentC::fromAsSet({1})},
            AsPath{BgpAttrAsPathSegmentC::fromAsSet({1})}},
        std::pair{
            AsPath{BgpAttrAsPathSegmentC::fromAsSeq({1})},
            AsPath{BgpAttrAsPathSegmentC::fromAsSet({1})}},
        std::pair{
            AsPath{BgpAttrAsPathSegmentC::fromAsSeq({1, 2})},
            AsPath{BgpAttrAsPathSegmentC::fromAsSet({1, 2})}},
        std::pair{
            AsPath{
                BgpAttrAsPathSegmentC::fromAsSeq({1}),
                BgpAttrAsPathSegmentC::fromAsSeq({2})},
            AsPath{BgpAttrAsPathSegmentC::fromAsSet({1, 2})}},
        std::pair{
            AsPath{
                BgpAttrAsPathSegmentC::fromAsSeq({1}),
                BgpAttrAsPathSegmentC::fromAsSet({2})},
            AsPath{BgpAttrAsPathSegmentC::fromAsSet({1, 2})}},
        std::pair{
            AsPath{BgpAttrAsPathSegmentC::fromConfedSeq({1})},
            AsPath{BgpAttrAsPathSegmentC::fromConfedSeq({1})}},
        std::pair{
            AsPath{BgpAttrAsPathSegmentC::fromConfedSet({1})},
            AsPath{BgpAttrAsPathSegmentC::fromConfedSet({1})}},
        std::pair{
            AsPath{
                BgpAttrAsPathSegmentC::fromConfedSeq({1}),
                BgpAttrAsPathSegmentC::fromAsSeq({1})},
            AsPath{
                BgpAttrAsPathSegmentC::fromConfedSeq({1}),
                BgpAttrAsPathSegmentC::fromAsSet({1})}},
        std::pair{
            AsPath{
                BgpAttrAsPathSegmentC::fromConfedSeq({1}),
                BgpAttrAsPathSegmentC::fromAsSeq({2}),
                BgpAttrAsPathSegmentC::fromConfedSeq({3}),
                BgpAttrAsPathSegmentC::fromAsSeq({4})},
            // No change
            AsPath{
                BgpAttrAsPathSegmentC::fromConfedSeq({1}),
                BgpAttrAsPathSegmentC::fromAsSeq({2}),
                BgpAttrAsPathSegmentC::fromConfedSeq({3}),
                BgpAttrAsPathSegmentC::fromAsSeq({4})}},
        std::pair{
            AsPath{
                BgpAttrAsPathSegmentC::fromConfedSeq({1}),
                BgpAttrAsPathSegmentC::fromConfedSet({2}),
                BgpAttrAsPathSegmentC::fromAsSeq({3})},
            AsPath{
                BgpAttrAsPathSegmentC::fromConfedSeq({1}),
                BgpAttrAsPathSegmentC::fromConfedSet({2}),
                BgpAttrAsPathSegmentC::fromAsSet({3})}}));

/*
 * Test prefix match TERM1 (with GOTO action) will be evaluated by TERM3
 * TERM2 is skipped
 *
 * Test prefix: kV6Prefix1 with kCommunity1
 *
 * TERM1: match kCommunity1, goto TERM3
 * TERM2: match kV6Prefix1, set origin IGP
 * TERM3: match kV6Prefix1, set origin INCOMPLETE
 *
 * Verify attributes has origin EGP
 */
TEST_F(PolicyTest, GotoTermTest) {
  // TERM1: match kCommunity1, goto TERM3
  const auto& match1 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST, {kCommunity1});
  auto actionGoto1 = createBgpPolicyGotoAction("Term3");
  auto term1 = createBgpPolicyTerm("Term1", "", {match1}, {actionGoto1});

  // TERM2: match kV6Prefix1, set origin IGP
  routing_policy::CompareNumericValue compareStructEQ;
  compareStructEQ.compare_operator() = routing_policy::ComparisonOperator::EQ;
  compareStructEQ.value() = kV6Prefix1.second;
  const auto& prefixListEntry = createPrefixListEntry(
      IPAddress::networkToString(kV6Prefix1), {compareStructEQ});
  const auto& match2 = createPrefixListMatch({prefixListEntry});
  auto actionIgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::IGP);
  auto term2 = createBgpPolicyTerm("Term2", "", {match2}, {actionIgp});

  // TERM3: match kV6Prefix1, set origin INCOMPLETE
  auto actionIncomplete = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::INCOMPLETE);
  auto term3 = createBgpPolicyTerm("Term3", "", {match2}, {actionIncomplete});

  // Create policy with TERM1, TERM2, TERM3
  const auto policyName = "Policy Statement";
  const auto& policyConfig =
      createBgpPolicies(policyName, {term1, term2, term3});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());

  /*
   * Test case: kV6Prefix1 with kCommunity1
   * It matches TERM1, GOTO TERM3, change origin to INCOMPLETE
   */
  std::vector<folly::CIDRNetwork> prefixSetMatch{kV6Prefix1};
  auto attrs = createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE);
  // community dies bit natch TERM1
  attrs->setCommunities(createBgpAttrCommunitiesC({kCommunity1}));
  attrs->publish();

  // Apply policy
  PolicyInMessage policyIn(prefixSetMatch, attrs);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  // Verify prefix has origin change to INCOMPLETE
  ASSERT_EQ(prefixSetMatch.size(), policyOut.result.size());
  EXPECT_EQ(
      BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE,
      policyOut.result.at(kV6Prefix1)->attrs->getOrigin());
}

/*
 * Test prefix that did not match TERM1 (with GOTO action) will continue to
 * be evaluated by remaining terms (TERM2), and will not be processed by
 * GOTO term
 *
 * prefix: kV6Prefix1, attributes: kCommunityNotMatchingRegex1
 *
 * TERM1: match community and community regex -> goto term3
 * TERM2: match kV6Prefix1, set origin IGP
 * TERM3: match kV6Prefix1, set origin INCOMPLETE
 *
 * prefix should not match term1, it should be processed by term2
 */
TEST_F(PolicyTest, GotoTermNegativeTest) {
  /*
   * TERM1: match kCommunityRegex1 -> goto term3
   * attributes should not match this term1
   */
  auto match1 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST,
      {kCommunityRegex1},
      BooleanOperator::OR,
      "community_match_ibn");
  auto actionGoto1 = createBgpPolicyGotoAction("Term3");
  auto term1 = createBgpPolicyTerm("Term1", "", {match1}, {actionGoto1});

  // TERM2: match kV6Prefix1, set origin IGP
  routing_policy::CompareNumericValue compareStructEQ;
  compareStructEQ.compare_operator() = routing_policy::ComparisonOperator::EQ;
  compareStructEQ.value() = kV6Prefix1.second;
  const auto& prefixListEntry = createPrefixListEntry(
      IPAddress::networkToString(kV6Prefix1), {compareStructEQ});
  const auto& match2 = createPrefixListMatch({prefixListEntry});
  auto actionIgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::IGP);
  auto term2 = createBgpPolicyTerm("Term2", "", {match2}, {actionIgp});

  // TERM3: match kV6Prefix1, set origin INCOMPLETE
  auto actionIncomplete = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::INCOMPLETE);
  auto term3 = createBgpPolicyTerm("Term3", "", {match2}, {actionIncomplete});

  // create policy
  const string policyName = "Policy Statement";
  const auto& policyConfig =
      createBgpPolicies(policyName, {term1, term2, term3});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());

  // prefix: kV6Prefix1, attributes: kCommunityNotMatchingRegex1
  const std::vector<folly::CIDRNetwork> prefixSetIn{kV6Prefix1};
  std::vector<std::string> communities = {kCommunityNotMatchingRegex1};
  auto attributes1 = createBgpPath(communities);
  attributes1->publish();

  // Apply policy
  PolicyInMessage policyIn(prefixSetIn, attributes1);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  // verify prefix processed by term2, origin = IGP
  EXPECT_EQ(
      BgpAttrOrigin::BGP_ORIGIN_IGP,
      policyOut.result.at(kV6Prefix1)->attrs->getOrigin());
}

TEST_F(PolicyTest, PopulateCommunitiesTest) {
  // create match with a reference to the community list
  const auto& bgpMatch1 = createCommunityListMatchWithReference(
      {kCommunity3}, {"community_list1", "community_list2"});
  // create a global community list definition
  const auto& communityList1 = createCommunityList(
      {kCommunity1}, BooleanOperator::OR, "community_list1");
  const auto& communityList2 = createCommunityList(
      {kCommunity2}, BooleanOperator::OR, "community_list2");
  const string policyName1 = "Policy Statement 1";
  const auto& bgpPolicies = createBgpPoliciesWithReferences(
      policyName1, {bgpMatch1}, {}, {communityList1, communityList2});
  PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
  const auto& policy1 = policyManager.getPolicyFromName(policyName1);
  const auto& bgpTerms = policy1->getPolicyTerms();
  const auto& matchFromReference = bgpTerms[0]->getPolicyAttributeMatches()[0];
  // match kCommunity1, kCommunity2, kCommunity3 not kCommunity4
  const auto& attributes1 = createBgpPath({kCommunity1});
  EXPECT_TRUE(matchFromReference->Match(attributes1));
  const auto& attributes2 = createBgpPath({kCommunity2});
  EXPECT_TRUE(matchFromReference->Match(attributes2));
  const auto& attributes3 = createBgpPath({kCommunity3});
  EXPECT_TRUE(matchFromReference->Match(attributes3));
  const auto& attributes4 = createBgpPath({kCommunity4});
  EXPECT_FALSE(matchFromReference->Match(attributes4));
}

TEST_F(PolicyTest, PopulateCommunitiesNegativeTest) {
  auto lambdaVerifyCommunityReferenceParseError =
      [&](const bgp_policy::BgpPolicyAtomicMatch& bgpMatch,
          const std::vector<bgp_policy::CommunityList>& communityLists,
          std::string expectedError) {
        const string policyName1 = "Policy Statement";
        const auto& bgpPolicies = createBgpPoliciesWithReferences(
            policyName1, {bgpMatch}, {}, communityLists);
        try {
          PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
          ADD_FAILURE();
        } catch (const BgpError& error) {
          EXPECT_EQ(*error.message(), expectedError);
          return;
        }
      };
  {
    // test missing communityList definition
    const auto& bgpMatch1 =
        createCommunityListMatchWithReference({"65000"}, {"community_list1"});
    const auto& communityList = createCommunityList(
        {kCommunity1, kCommunity2}, BooleanOperator::OR, "community_list2");
    lambdaVerifyCommunityReferenceParseError(
        bgpMatch1,
        {communityList},
        "Could not find CommunityList reference: community_list1");
  }
  {
    // test duplicate community names
    const auto& bgpMatch1 =
        createCommunityListMatchWithReference({"65000"}, {"community_list1"});
    const auto& communityList = createCommunityList(
        {kCommunity1, kCommunity2}, BooleanOperator::OR, "community_list1");
    lambdaVerifyCommunityReferenceParseError(
        bgpMatch1,
        {communityList, communityList},
        "Duplicate CommunityList name: community_list1");
  }
  {
    // test recursive references not allowed
    const auto& bgpMatch1 =
        createCommunityListMatchWithReference({"65000"}, {"community_list1"});
    bgp_policy::CommunityList communityList;
    *communityList.name() = "community_list1";
    communityList.communities() =
        std::vector<std::string>{kCommunity1, kCommunity2};
    *communityList.boolean_operator() = BooleanOperator::OR;
    communityList.community_list_names() =
        std::vector<std::string>{"community_list2"};
    lambdaVerifyCommunityReferenceParseError(
        bgpMatch1,
        {communityList, communityList},
        "CommunityList recursive references not allowed: community_list1");
  }
  {
    // test malformed communities in global community list definitions
    const auto& bgpMatch1 =
        createCommunityListMatchWithReference({"65000"}, {"community_list1"});
    const auto& communityList =
        createCommunityList({"++"}, BooleanOperator::OR, "community_list1");
    lambdaVerifyCommunityReferenceParseError(
        bgpMatch1, {communityList}, "Malformed community config: ++");
  }
  {
    // test conflicting boolean_operator in global community list definitions
    auto bgpMatch1 =
        createCommunityListMatchWithReference({"65000"}, {"community_list1"});
    *bgpMatch1.communities_filter()->boolean_operator() =
        routing_policy::BooleanOperator::AND;
    const auto& communityList = createCommunityList(
        {kCommunity1, kCommunity2}, BooleanOperator::OR, "community_list1");
    lambdaVerifyCommunityReferenceParseError(
        bgpMatch1,
        {communityList},
        "Conflicting boolean_operator from the reference: community_list1");
  }
}

TEST_F(PolicyTest, PopulateAsPathListTest) {
  // create match with a reference to the aspath list
  const auto& bgpMatch1 = createAsPathListMatchWithReference(
      {kASPathRegex4}, {"aspath_list1", "aspath_list2"});
  // create a global aspath list definition
  const auto& asPathList1 =
      createAsPathList({kASPathRegex1}, BooleanOperator::OR, "aspath_list1");
  const auto& asPathList2 =
      createAsPathList({kASPathRegex2}, BooleanOperator::OR, "aspath_list2");
  const string policyName1 = "Policy Statement 1";
  const auto& bgpPolicies = createBgpPoliciesWithReferences(
      policyName1, {bgpMatch1}, {}, {}, {asPathList1, asPathList2});
  PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
  const auto& policy1 = policyManager.getPolicyFromName(policyName1);
  const auto& bgpTerms = policy1->getPolicyTerms();
  const auto& matchFromReference = bgpTerms[0]->getPolicyAttributeMatches()[0];
  const auto& attributes1 =
      createBgpPath({}, {BgpAttrAsPathSegmentC({{}, {kAsn4}, {}, {}})});
  EXPECT_TRUE(matchFromReference->Match(attributes1));
  // {65001} matching kASPathRegex2
  const auto& attributes2 =
      createBgpPath({}, {BgpAttrAsPathSegmentC({{}, {kAsn5}, {}, {}})});
  EXPECT_TRUE(matchFromReference->Match(attributes2));
  // {65002} matching kASPathRegex3
  const auto& attributes3 =
      createBgpPath({}, {BgpAttrAsPathSegmentC({{}, {kAsn6}, {}, {}})});
  EXPECT_TRUE(matchFromReference->Match(attributes3));
  // {64551} not matching anything
  const auto& attributes4 =
      createBgpPath({}, {BgpAttrAsPathSegmentC({{}, {kAsn1}, {}, {}})});
  EXPECT_FALSE(matchFromReference->Match(attributes4));
}

TEST_F(PolicyTest, PopulateAsPathListReferenceNegativeTest) {
  auto lambdaVerifyAsPathListReferenceParseError =
      [&](const bgp_policy::BgpPolicyAtomicMatch& bgpMatch,
          const std::vector<bgp_policy::AsPathList>& asPathLists,
          std::string expectedError) {
        const string policyName1 = "Policy Statement";
        const auto& bgpPolicies = createBgpPoliciesWithReferences(
            policyName1, {bgpMatch}, {}, {}, {asPathLists});
        try {
          PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
          ADD_FAILURE();
        } catch (const BgpError& error) {
          EXPECT_EQ(*error.message(), expectedError);
          return;
        }
      };
  {
    // test missing asPathList definition
    const auto& bgpMatch1 =
        createAsPathListMatchWithReference({}, {"aspath_list1"});
    // create a global aspath list definition
    const auto& asPathList = createAsPathList(
        {kASPathRegex1, kASPathRegex2}, BooleanOperator::OR, "aspath_list2");
    lambdaVerifyAsPathListReferenceParseError(
        bgpMatch1,
        {asPathList},
        "Could not find AsPathList reference: aspath_list1");
  }
  {
    // test duplicate asPathList names
    const auto& bgpMatch1 =
        createAsPathListMatchWithReference({}, {"aspath_list1"});
    // create a global aspath list definition
    const auto& asPathList = createAsPathList(
        {kASPathRegex1, kASPathRegex2}, BooleanOperator::OR, "aspath_list1");
    lambdaVerifyAsPathListReferenceParseError(
        bgpMatch1,
        {asPathList, asPathList},
        "Duplicate AsPathList name: aspath_list1");
  }
  {
    // test recursive references not allowed
    const auto& bgpMatch1 =
        createAsPathListMatchWithReference({}, {"aspath_list1"});
    // create a global aspath list definition
    bgp_policy::AsPathList asPathList;
    *asPathList.name() = "aspath_list1";
    asPathList.as_paths() =
        std::vector<std::string>{kASPathRegex1, kASPathRegex2};
    *asPathList.boolean_operator() = BooleanOperator::OR;
    asPathList.as_path_list_names() = std::vector<std::string>{"aspath_list2"};
    lambdaVerifyAsPathListReferenceParseError(
        bgpMatch1,
        {asPathList, asPathList},
        "AsPathList recursive references not allowed: aspath_list1");
  }
  {
    // test malformed asPath regex in global asPathList definitions
    const auto& bgpMatch1 =
        createAsPathListMatchWithReference({}, {"aspath_list1"});
    // create a global aspath list definition
    const auto& asPathList =
        createAsPathList({"++"}, BooleanOperator::OR, "aspath_list1");

    lambdaVerifyAsPathListReferenceParseError(
        bgpMatch1, {asPathList}, "Malformed regex in aspath config: ++");
  }
  {
    // test conflict boolean_operator in global asPathList definitions
    auto bgpMatch1 = createAsPathListMatchWithReference({}, {"aspath_list1"});
    *bgpMatch1.as_path_filters()->boolean_operator() =
        routing_policy::BooleanOperator::AND;
    // create a global aspath list definition
    const auto& asPathList = createAsPathList(
        {kASPathRegex1, kASPathRegex2}, BooleanOperator::OR, "aspath_list1");

    lambdaVerifyAsPathListReferenceParseError(
        bgpMatch1,
        {asPathList},
        "Conflicting boolean_operator from the reference: aspath_list1");
  }
}

TEST_F(PolicyTest, PopulatePrefixListTest) {
  // create match with a reference to the prefix list
  routing_policy::CompareNumericValue compareNumVal;
  *compareNumVal.compare_operator() = routing_policy::ComparisonOperator::EQ;
  *compareNumVal.value() = 24;
  std::vector<routing_policy::CompareNumericValue> compareNumericValues = {
      compareNumVal};
  // define inline prefix matching
  const auto& prefixListEntry = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix4), compareNumericValues);
  const auto& bgpMatch1 = createPrefixListMatch(
      {prefixListEntry}, {"prefixlist_1", "prefixlist_2"});
  // create a global PrefixList definition
  const auto& prefixListEntry1 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix1Base), compareNumericValues);
  routing_policy::PrefixList prefixlist1;
  *prefixlist1.prefixes() = {prefixListEntry1};
  *prefixlist1.name() = "prefixlist_1";
  const auto& prefixListEntry2 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix2), compareNumericValues);
  routing_policy::PrefixList prefixlist2;
  *prefixlist2.prefixes() = {prefixListEntry2};
  *prefixlist2.name() = "prefixlist_2";
  const string policyName1 = "Policy Statement 1";
  const auto& bgpPolicies = createBgpPoliciesWithReferences(
      policyName1, {bgpMatch1}, {}, {}, {}, {prefixlist1, prefixlist2});

  PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
  const auto& policy1 = policyManager.getPolicyFromName(policyName1);
  const auto& bgpTerms = policy1->getPolicyTerms();
  const auto& matchFromReference = bgpTerms[0]->getPolicyPrefixMatches()[0];
  // test update matches kV4Prefix1Base
  EXPECT_TRUE(matchFromReference->Match(kV4Prefix1));
  // test update matches kV4Prefix2
  EXPECT_TRUE(matchFromReference->Match(kV4Prefix2));
  // test update matches kV4Prefix4
  EXPECT_TRUE(matchFromReference->Match(kV4Prefix4));
  // test update does not match kV4Prefix5
  EXPECT_FALSE(matchFromReference->Match(kV4Prefix5));
}

TEST_F(PolicyTest, PopulatePrefixListReferenceNegativeTest) {
  auto lambdaVerifyPrefixListReferenceParseError =
      [&](const bgp_policy::BgpPolicyAtomicMatch& bgpMatch,
          const std::vector<routing_policy::PrefixList>& prefixList,
          std::string expectedError) {
        const string policyName1 = "Policy Statement";
        const auto& bgpPolicies = createBgpPoliciesWithReferences(
            policyName1, {bgpMatch}, {}, {}, {}, {prefixList});
        try {
          PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
          ADD_FAILURE();
        } catch (const BgpError& error) {
          EXPECT_EQ(*error.message(), expectedError);
          return;
        }
      };
  routing_policy::CompareNumericValue compareNumVal;
  *compareNumVal.compare_operator() = routing_policy::ComparisonOperator::EQ;
  *compareNumVal.value() = 24;
  std::vector<routing_policy::CompareNumericValue> compareNumericValues = {
      compareNumVal};
  {
    // test missing PrefixList definition
    const auto& bgpMatch1 = createPrefixListMatch({}, {"prefixlist_1"});
    // create a global prefix list definition
    const auto& prefixListEntry = createPrefixListEntry(
        IPAddress::networkToString(kV4Prefix1Base), compareNumericValues);
    routing_policy::PrefixList prefixlist;
    *prefixlist.prefixes() = {prefixListEntry};
    *prefixlist.name() = "prefixlist_2";

    lambdaVerifyPrefixListReferenceParseError(
        bgpMatch1,
        {prefixlist},
        "Could not find PrefixList reference: prefixlist_1");
  }
  {
    // test duplicate PrefixList names
    const auto& bgpMatch1 = createPrefixListMatch({}, {"prefixlist_1"});
    // create a global aspath list definition
    const auto& prefixListEntry = createPrefixListEntry(
        IPAddress::networkToString(kV4Prefix1Base), compareNumericValues);
    routing_policy::PrefixList prefixlist;
    *prefixlist.prefixes() = {prefixListEntry};
    *prefixlist.name() = "prefixlist_1";

    lambdaVerifyPrefixListReferenceParseError(
        bgpMatch1,
        {prefixlist, prefixlist},
        "Duplicate PrefixList name: prefixlist_1");
  }
  {
    // test recursive references not allowed
    const auto& bgpMatch1 = createPrefixListMatch({}, {"prefixlist_1"});
    // create a global aspath list definition
    const auto& prefixListEntry = createPrefixListEntry(
        IPAddress::networkToString(kV4Prefix1Base), compareNumericValues);
    routing_policy::PrefixList prefixlist;
    *prefixlist.prefixes() = {prefixListEntry};
    *prefixlist.name() = "prefixlist_1";
    *prefixlist.prefix_list_names() = {"prefixlist_2"};
    lambdaVerifyPrefixListReferenceParseError(
        bgpMatch1,
        {prefixlist, prefixlist},
        "PrefixList recursive references not allowed: prefixlist_1");
  }
  {
    // test malformed communities in global PrefixList definitions
    const auto& bgpMatch1 = createPrefixListMatch({}, {"prefixlist_1"});
    // create a global aspath list definition
    const auto& prefixListEntry =
        createPrefixListEntry("1.#7&1/24", compareNumericValues);
    routing_policy::PrefixList prefixlist;
    *prefixlist.prefixes() = {prefixListEntry};
    *prefixlist.name() = "prefixlist_1";
    lambdaVerifyPrefixListReferenceParseError(
        bgpMatch1, {prefixlist}, "Malformed base prefix in config: 1.#7&1/24");
  }
}

TEST_F(PolicyTest, PopulateCommunitiesActionTest) {
  // create action with a reference to the community list
  const auto& bgpAction = createBgpPolicyCommunityAction(
      bgp_policy::CommunityActionType::ADD,
      {kCommunity3},
      {"community_list1", "community_list2"});
  // create a global community list definition
  const auto& communityList1 = createCommunityList(
      {kCommunity1}, BooleanOperator::OR, "community_list1");
  const auto& communityList2 = createCommunityList(
      {kCommunity2}, BooleanOperator::OR, "community_list2");
  const string policyName = "Policy Statement";
  const auto& bgpPolicies = createBgpPoliciesWithReferences(
      policyName, {}, {bgpAction}, {communityList1, communityList2});
  PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
  const auto& policy = policyManager.getPolicyFromName(policyName);
  const auto& terms = policy->getPolicyTerms();
  const auto& action = terms[0]->getPolicyActions()[0];

  auto attrFields = buildBgpPathFields(0, 0, 0, 0);
  auto attrs = std::make_shared<BgpPath>(*attrFields);
  attrs->setCommunities(createBgpAttrCommunitiesC({kCommunity4}));
  action->applyAction(attrs);
  /*
   * Verify that after applying, communities are appended to existing
   * values. Verify the order of communities is also as expected.
   * the order of communities from communitylist reference follows the order of
   * the references
   */
  EXPECT_THAT(
      ConvertAttrCommunitiesToStrings(attrs->getCommunities().get()),
      testing::ElementsAreArray(
          {kCommunity1, kCommunity2, kCommunity3, kCommunity4}));
}

TEST_F(PolicyTest, PopulateCommunitiesActionNegativeTest) {
  auto lambdaVerifyCommunityReferenceActionParseError =
      [&](const bgp_policy::BgpPolicyAction& bgpAction,
          const std::vector<bgp_policy::CommunityList>& communityLists,
          std::string expectedError) {
        const string policyName1 = "Policy Statement";
        auto bgpPolicies = createBgpPoliciesWithReferences(
            policyName1, {}, {bgpAction}, communityLists);
        try {
          PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
          ADD_FAILURE();
        } catch (const BgpError& error) {
          EXPECT_EQ(*error.message(), expectedError);
        }
      };
  {
    // test missing communityList definition
    const auto& bgpAction = createBgpPolicyCommunityAction(
        bgp_policy::CommunityActionType::ADD, {}, {"community_list1"});
    const auto& communityList = createCommunityList(
        {kCommunity1, kCommunity2}, BooleanOperator::OR, "community_list2");
    lambdaVerifyCommunityReferenceActionParseError(
        bgpAction,
        {communityList},
        "Could not find CommunityList Action reference: community_list1");
  }
  {
    // test duplicate community names
    const auto& bgpAction = createBgpPolicyCommunityAction(
        bgp_policy::CommunityActionType::ADD, {}, {"community_list1"});
    const auto& communityList = createCommunityList(
        {kCommunity1, kCommunity2}, BooleanOperator::OR, "community_list1");
    lambdaVerifyCommunityReferenceActionParseError(
        bgpAction,
        {communityList, communityList},
        "Duplicate CommunityList name: community_list1");
  }
  {
    // test malformed communities in global community list definitions
    const auto& bgpAction = createBgpPolicyCommunityAction(
        bgp_policy::CommunityActionType::ADD, {}, {"community_list1"});
    const auto& communityList =
        createCommunityList({"++"}, BooleanOperator::OR, "community_list1");
    lambdaVerifyCommunityReferenceActionParseError(
        bgpAction, {communityList}, "Malformed community config: ++");
  }
}

TEST_F(PolicyTest, PopulatePolicyTermsNegativeTest) {
  // valid match
  const auto& match1 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST, {kCommunity1});
  auto actionIgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::IGP);
  auto term1 = createBgpPolicyTerm("Term1", "", {match1}, {actionIgp});

  // CASE 1: goto point to undefined terms
  const auto& match2 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST, {kCommunity2});
  auto actionGoto1 = createBgpPolicyGotoAction("Term4");
  auto term2 = createBgpPolicyTerm("Term2", "", {match2}, {actionGoto1});

  // Create policy with term1, term2
  const auto& policyStatement1 =
      createBgpPolicyStatement("Policy Statement", {term1, term2});
  EXPECT_THROW(
      auto policy = Policy(policyStatement1, createTestBgpGlobalConfig()),
      std::out_of_range);

  // CASE 2: circulation condition: term2 point to term1
  const auto& match3 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST, {kCommunity3});
  auto actionGoto2 = createBgpPolicyGotoAction("Term1");
  auto term3 = createBgpPolicyTerm("Term3", "", {match3}, {actionGoto2});

  // Create policy with term1, term2
  const auto& policyStatement2 =
      createBgpPolicyStatement("Policy Statement", {term1, term3});
  EXPECT_THROW(
      auto policy = Policy(policyStatement2, createTestBgpGlobalConfig()),
      std::logic_error);

  // CASE 2: empty goto term name
  const auto& match4 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::COMMUNITY_LIST, {kCommunity4});
  auto actionGoto3 = createBgpPolicyGotoAction("");
  auto term4 = createBgpPolicyTerm("Term3", "", {match4}, {actionGoto3});

  // Create policy with term1, term2
  const auto& policyStatement3 =
      createBgpPolicyStatement("Policy Statement", {term1, term4});
  EXPECT_THROW(
      auto policy = Policy(policyStatement3, createTestBgpGlobalConfig()),
      std::invalid_argument);
}

TEST_F(PolicyTest, MatchLogicTypeUnsupportedTest) {
  const auto& bgpMatch1 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::AS_PATH, {kASPathRegex1}, BooleanOperator::OR);
  const string policyName = "Policy Statement";

  /*
   * Case 1: test match_logic_type OR is ignored if there is only single match
   * condition in the term
   */
  try {
    auto bgpPolicies = createBgpPolicies(policyName, {bgpMatch1});

    // Set match logic type which is not supported
    *bgpPolicies.bgp_policy_statements()
         ->at(0)
         .policy_entries()
         ->at(0)
         .policy_match_entries()
         ->match_logic_type() = routing_policy::BooleanOperator::OR;

    PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
  } catch (const BgpError&) {
    ADD_FAILURE();
  }

  const auto& bgpMatch2 = createBgpPolicyAtomicMatch(
      BgpPolicyAtomicMatchType::AS_PATH, {kASPathRegex2}, BooleanOperator::OR);
  /*
   * Case 2: test match_logic_type OR is unsupported when there are multiple
   * match conditions
   */
  try {
    auto bgpPolicies = createBgpPolicies(policyName, {bgpMatch1, bgpMatch2});

    // Set match logic type which is not supported
    *bgpPolicies.bgp_policy_statements()
         ->at(0)
         .policy_entries()
         ->at(0)
         .policy_match_entries()
         ->match_logic_type() = routing_policy::BooleanOperator::OR;

    PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());
    ADD_FAILURE();
  } catch (const BgpError& error) {
    EXPECT_EQ(
        *error.message(),
        "Unsupported match_logic_type BooleanOperator. Only AND is supported");
  }
}

CommunityMatch createCommunityMatchFromList(
    const std::vector<TBgpCommunityMatch>& communities,
    const BooleanOperator& booleanOperator = BooleanOperator::OR) {
  TBgpPathMatcher tMatcher;
  tMatcher.community_list() =
      createTCommunityListMatch(communities, booleanOperator);
  return CommunityMatch(tMatcher);
}

TEST_F(PolicyTest, TBgpPathMatcherCommunityListMatchTest) {
  /*
   * Similar to CommunityMatchStringExactTest but using TBgpPathMatcher to
   * generate the match
   */
  auto tCommunity1 = getTBgpCommunityMatch(kCommunity1);
  auto tCommunity2 = getTBgpCommunityMatch(kCommunity2);
  auto tCommunity3 = getTBgpCommunityMatch(kCommunity3);
  auto tCommunityNeg1 = getTBgpCommunityMatch(
      kCommunity1, routing_policy::MatchValueLogicOperator::NOT_EQUAL);
  auto tCommunityNeg2 = getTBgpCommunityMatch(
      kCommunity2, routing_policy::MatchValueLogicOperator::NOT_EQUAL);

  TBgpPathMatcher tMatcher;
  // BgpError due to no attribute
  EXPECT_THROW(CommunityMatch{tMatcher}, BgpError);

  // BgpError conflict community matches
  EXPECT_THROW(
      createCommunityMatchFromList(
          {tCommunity1, tCommunityNeg1}, BooleanOperator::AND),
      BgpError);
  // BgpError conflict community matches for another constructor
  EXPECT_THROW(
      CommunityMatch{createTCommunityListMatch(
          {tCommunity1, tCommunityNeg1}, BooleanOperator::AND)},
      BgpError);

  const auto andMatch12 = createCommunityMatchFromList(
      {tCommunity1, tCommunity2}, BooleanOperator::AND);

  const auto andMatch231 = createCommunityMatchFromList(
      {tCommunity2, tCommunity3, tCommunity1}, BooleanOperator::AND);

  // duplicated communnities would be merged
  const auto andMatch11 = createCommunityMatchFromList(
      {tCommunity1, tCommunity1}, BooleanOperator::AND);

  const auto andMatchNeg1 =
      createCommunityMatchFromList({tCommunityNeg1}, BooleanOperator::AND);

  const auto andMatchNeg13 = createCommunityMatchFromList(
      {tCommunityNeg1, tCommunity3}, BooleanOperator::AND);

  const auto orMatch12 = createCommunityMatchFromList(
      {tCommunity1, tCommunity2}, BooleanOperator::OR);

  const auto orMatchNeg12 = createCommunityMatchFromList(
      {tCommunityNeg1, tCommunity2}, BooleanOperator::OR);

  const auto orMatchNeg1 =
      createCommunityMatchFromList({tCommunityNeg1}, BooleanOperator::OR);

  const auto orMatchNeg1Neg2 = createCommunityMatchFromList(
      {tCommunityNeg1, tCommunityNeg2}, BooleanOperator::OR);

  {
    const auto& attr1 = createBgpPath({kCommunity1});
    EXPECT_FALSE(andMatch12.Match(attr1));
    EXPECT_FALSE(andMatch231.Match(attr1));
    EXPECT_TRUE(andMatch11.Match(attr1));
    EXPECT_FALSE(andMatchNeg1.Match(attr1));
    EXPECT_FALSE(andMatchNeg13.Match(attr1));
    EXPECT_TRUE(orMatch12.Match(attr1));
    EXPECT_FALSE(orMatchNeg12.Match(attr1));
    EXPECT_FALSE(orMatchNeg1.Match(attr1));
    EXPECT_TRUE(orMatchNeg1Neg2.Match(attr1));
  }
  {
    const auto& attr12 = createBgpPath({kCommunity1, kCommunity2});
    EXPECT_TRUE(andMatch12.Match(attr12));
    EXPECT_FALSE(andMatch231.Match(attr12));
    EXPECT_TRUE(andMatch11.Match(attr12));
    EXPECT_FALSE(andMatchNeg1.Match(attr12));
    EXPECT_FALSE(andMatchNeg13.Match(attr12));
    EXPECT_TRUE(orMatch12.Match(attr12));
    EXPECT_TRUE(orMatchNeg12.Match(attr12));
    EXPECT_FALSE(orMatchNeg1.Match(attr12));
    EXPECT_FALSE(orMatchNeg1Neg2.Match(attr12));
  }
  {
    const auto& attr2 = createBgpPath({kCommunity2});
    EXPECT_FALSE(andMatch12.Match(attr2));
    EXPECT_FALSE(andMatch231.Match(attr2));
    EXPECT_FALSE(andMatch11.Match(attr2));
    EXPECT_TRUE(andMatchNeg1.Match(attr2));
    EXPECT_FALSE(andMatchNeg13.Match(attr2));
    EXPECT_TRUE(orMatch12.Match(attr2));
    EXPECT_TRUE(orMatchNeg12.Match(attr2));
    EXPECT_TRUE(orMatchNeg1.Match(attr2));
    EXPECT_TRUE(orMatchNeg1Neg2.Match(attr2));
  }
  {
    const auto& attr3 = createBgpPath({kCommunity3});
    EXPECT_FALSE(andMatch12.Match(attr3));
    EXPECT_FALSE(andMatch231.Match(attr3));
    EXPECT_FALSE(andMatch11.Match(attr3));
    EXPECT_TRUE(andMatchNeg1.Match(attr3));
    EXPECT_TRUE(andMatchNeg13.Match(attr3));
    EXPECT_FALSE(orMatch12.Match(attr3));
    EXPECT_TRUE(orMatchNeg12.Match(attr3));
    EXPECT_TRUE(orMatchNeg1.Match(attr3));
    EXPECT_TRUE(orMatchNeg1Neg2.Match(attr3));
  }
  {
    const auto& attr312 =
        createBgpPath({kCommunity3, kCommunity1, kCommunity2});
    EXPECT_TRUE(andMatch12.Match(attr312));
    EXPECT_TRUE(andMatch231.Match(attr312));
    EXPECT_TRUE(andMatch11.Match(attr312));
    EXPECT_FALSE(andMatchNeg1.Match(attr312));
    EXPECT_FALSE(andMatchNeg13.Match(attr312));
    EXPECT_TRUE(orMatch12.Match(attr312));
    EXPECT_TRUE(orMatchNeg12.Match(attr312));
    EXPECT_FALSE(orMatchNeg1.Match(attr312));
    EXPECT_FALSE(orMatchNeg1Neg2.Match(attr312));
  }
}

TEST_F(PolicyTest, TBgpPathMatcherOriginMatchTest) {
  /*
   * Similar to OriginMatchTest but using TBgpPathMatcher to set three
   * OriginMatch match struct, with IGP, EGP, INCOMPLETE. Test update with the
   * EGP, it will fail IGP, INCOMPLETE matches, pass EGP match In
   * createBgpPolicyAtomicMatch, default value is IGP
   */

  // For each of 3 origin types
  for (int i = 0; i < 3; i++) {
    TBgpPathMatcher tMatcher;
    tMatcher.origin() = policyOriginTypes[i];

    const auto& policyMatch = OriginMatch(tMatcher);

    for (int j = 0; j < 3; j++) {
      const auto& attributes =
          createBgpPath({}, {}, nullptr, attrOriginTypes[j]);
      if (j == i) {
        EXPECT_TRUE(policyMatch.Match(attributes));
      } else {
        EXPECT_FALSE(policyMatch.Match(attributes));
      }
    }
  }
}

TEST_F(PolicyTest, TBgpPathMatcherAsPathMatchTest) {
  /*
   * Similar to AsPathMatchTest and AsPathMatchNegativeTest but using
   * TBgpPathMatcher
   */
  {
    // kASPathRegex1 = "^65000.*", kASPathRegex2 = ".*65001$"
    TBgpPathMatcher tMatcher;
    tMatcher.as_path_regex() = kASPathRegex1;
    const auto& pathMatch1 = AsPathMatch(tMatcher);

    tMatcher.as_path_regex() = kASPathRegex2;
    const auto& pathMatch2 = AsPathMatch(tMatcher);

    std::vector<BgpAttrAsPathSegmentC> asPath;
    BgpAttrAsPathSegmentC segment1;
    segment1.asSequence.push_back(65000);
    asPath.push_back(segment1);

    const auto& attributes1 = createBgpPath({}, asPath);

    // Test update communities matches regex;
    EXPECT_TRUE(pathMatch1.Match(attributes1));
    EXPECT_FALSE(pathMatch2.Match(attributes1));

    asPath.clear();
    // asPath is now 65000 65001
    segment1.asSequence.push_back(65001);
    asPath.push_back(segment1);

    // Test update communities do not match regex
    const auto& attributes2 = createBgpPath({}, asPath);
    EXPECT_TRUE(pathMatch1.Match(attributes2));
    EXPECT_TRUE(pathMatch2.Match(attributes2));
  }
  {
    // Test update with asSet
    TBgpPathMatcher tMatcher;
    tMatcher.as_path_regex() = kASPathRegex1;
    const auto& pathMatch1 = AsPathMatch(tMatcher);

    tMatcher.as_path_regex() = kASPathSetRegex2;
    const auto& pathMatch2 = AsPathMatch(tMatcher);

    // asPath is now {65000 65002}
    std::vector<BgpAttrAsPathSegmentC> asPath;
    BgpAttrAsPathSegmentC segment1;
    segment1.asSet = {65000, 65002};
    asPath.push_back(segment1);
    const auto& attributes1 = createBgpPath({}, asPath);

    EXPECT_FALSE(pathMatch1.Match(attributes1));
    EXPECT_FALSE(pathMatch2.Match(attributes1));

    // asPath is now {65000 65002} {65001}
    BgpAttrAsPathSegmentC segment2;
    segment2.asSet = {65001};
    asPath.push_back(segment2);
    const auto& attributes2 = createBgpPath({}, asPath);

    EXPECT_FALSE(pathMatch1.Match(attributes2));
    EXPECT_TRUE(pathMatch2.Match(attributes2));

    // asPath is now {65000 65002} {65001, 65003}
    asPath.clear();
    segment2.asSet = {65001, 65003};
    asPath.push_back(segment1);
    asPath.push_back(segment2);
    const auto& attributes3 = createBgpPath({}, asPath);
    EXPECT_FALSE(pathMatch1.Match(attributes3));
    EXPECT_FALSE(pathMatch2.Match(attributes3));
  }
  {
    /*
     * test with multiple as Segment
     * kASPathRegexMultiSeq = "^\\(2[0-9][0-9][0-9]\\)_65000_65000_65000$";
     */
    TBgpPathMatcher tMatcher;
    tMatcher.as_path_regex() = kASPathRegexMultiSeq;
    const auto& pathMatch = AsPathMatch(tMatcher);

    std::vector<BgpAttrAsPathSegmentC> asPath;
    BgpAttrAsPathSegmentC segment1;
    segment1.asConfedSequence = {2001};
    asPath.push_back(segment1);
    BgpAttrAsPathSegmentC segment2;
    segment2.asSequence = {65000, 65000, 65000};
    asPath.push_back(segment2);

    // asPath is (2001)_65000_65000_65000
    const auto& attributes1 = createBgpPath({}, asPath);

    EXPECT_TRUE(pathMatch.Match(attributes1));

    asPath.clear();
    asPath.push_back(segment1);
    segment2.asSequence = {65000, 65000};
    asPath.push_back(segment2);
    // asPath is (2001)_65000_65000
    const auto& attributes2 = createBgpPath({}, asPath);
    EXPECT_FALSE(pathMatch.Match(attributes2));
  }
  {
    TBgpPathMatcher tMatcher;
    tMatcher.as_path_regex() = kASPathRegex5;
    const auto& pathMatch1 = AsPathMatch(tMatcher);

    tMatcher.as_path_regex() = kASPathRegex6;
    const auto& pathMatch2 = AsPathMatch(tMatcher);

    std::vector<BgpAttrAsPathSegmentC> asPath;
    BgpAttrAsPathSegmentC segment1;
    // asPath 65001 65000
    segment1.asSequence.push_back(65001);
    segment1.asSequence.push_back(65000);
    asPath.push_back(segment1);

    const auto& attributes1 = createBgpPath({}, asPath);

    EXPECT_TRUE(pathMatch1.Match(attributes1));
    EXPECT_FALSE(pathMatch2.Match(attributes1));
    asPath.clear();

    // asPath is now 65001 65000 65000
    segment1.asSequence.push_back(65000);
    asPath.push_back(segment1);
    const auto& attributes2 = createBgpPath({}, asPath);
    EXPECT_TRUE(pathMatch1.Match(attributes2));
    EXPECT_TRUE(pathMatch2.Match(attributes2));
  }
  {
    /*
     * test other regex
     * kASPathRegexDot = "6.000"
     */
    TBgpPathMatcher tMatcher;
    tMatcher.as_path_regex() = kASPathRegexDot;
    const auto& pathMatch = AsPathMatch(tMatcher);

    std::vector<BgpAttrAsPathSegmentC> asPath;
    BgpAttrAsPathSegmentC segment1;

    segment1.asSequence = {61000};
    asPath.push_back(segment1);
    const auto& attributes1 = createBgpPath({}, asPath);
    EXPECT_TRUE(pathMatch.Match(attributes1));

    asPath.clear();
    segment1.asSequence = {6000};
    asPath.push_back(segment1);
    const auto& attributes2 = createBgpPath({}, asPath);
    EXPECT_FALSE(pathMatch.Match(attributes2));
  }
  {
    /*
     * test other regex
     * kASPathRegexNum = "\\d{5}"
     */
    TBgpPathMatcher tMatcher;
    tMatcher.as_path_regex() = kASPathRegexNum;
    const auto& pathMatch = AsPathMatch(tMatcher);

    std::vector<BgpAttrAsPathSegmentC> asPath;
    BgpAttrAsPathSegmentC segment1;
    segment1.asSequence = {61111};
    asPath.push_back(segment1);
    const auto& attributes1 = createBgpPath({}, asPath);
    EXPECT_TRUE(pathMatch.Match(attributes1));

    asPath.clear();
    segment1.asSequence = {6000};
    asPath.push_back(segment1);
    const auto& attributes2 = createBgpPath({}, asPath);
    EXPECT_FALSE(pathMatch.Match(attributes2));
  }
  /*
   * test three corner cases:
   * - empty aspath match
   * - malformed aspath regex
   * - attributes without aspath attributes
   */
  {
    // test AsPathMatch with empty as_path, throw BgpError
    TBgpPathMatcher tMatcher;
    EXPECT_THROW(AsPathMatch{tMatcher}, BgpError);
  }
  {
    // test config with malformed aspath regex, raise exception
    TBgpPathMatcher tMatcher;
    tMatcher.as_path_regex() = "+a76!$3";

    try {
      AsPathMatch{tMatcher};
      ADD_FAILURE();
    } catch (const BgpError& error) {
      EXPECT_EQ(*error.message(), "Malformed regex in aspath config: +a76!$3");
    }
  }
  {
    /*
     * testing attributes without aspath attribute,
     * kASPathRegex3 = ".*"
     */
    TBgpPathMatcher tMatcher;
    tMatcher.as_path_regex() = kASPathRegex3;
    const auto& pathMatch = AsPathMatch(tMatcher);

    const auto& attributes = createBgpPath();
    EXPECT_TRUE(pathMatch.Match(attributes));
  }
}

TEST_F(PolicyTest, TBgpPathMatcherAsPathLenMatchTest) {
  /*
   * Similar to AsPathLenMatchTest but using TBgpPathMatcher to test
   * AsPathLenMatch
   */
  TBgpPathMatcher tMatcher;

  // as_path_length is not specified  -- throw BgpError
  EXPECT_THROW(AsPathLenMatch{tMatcher}, BgpError);

  tMatcher.as_path_length() = 4;

  const auto& asPathLenMatch = AsPathLenMatch(tMatcher);

  // set as path with 4 segment
  {
    const auto& attrsFields = buildBgpPathFields(4, 1, 0, 0);
    const auto& attributes = createBgpPath({}, {}, attrsFields);
    EXPECT_TRUE(asPathLenMatch.Match(attributes));
  }
  {
    const auto& attributes = createBgpPath();
    // test negative case
    EXPECT_FALSE(asPathLenMatch.Match(attributes));
  }
}

TEST_F(PolicyTest, NsfEncodingSchemeToVectorTest) {
  nsf_policy::NsfTeWeightEncoding encoding;
  encoding.l2_encoding() = nsf_policy::NsfL2TeWeightEncoding();
  encoding.l2_encoding()->rack_id() = 1;
  encoding.l2_encoding()->plane_id() = 2;
  encoding.l2_encoding()->remote_rack_capacity() = 3;
  encoding.l2_encoding()->spine_capacity() = 4;
  encoding.l2_encoding()->local_rack_capacity() = 5;

  auto lengths = encodingSchemeToVector(encoding);
  EXPECT_EQ(5, lengths.size());
  EXPECT_EQ(1, lengths[0]);
  EXPECT_EQ(2, lengths[1]);
  EXPECT_EQ(3, lengths[2]);
  EXPECT_EQ(4, lengths[3]);
  EXPECT_EQ(5, lengths[4]);
}

TEST_F(PolicyTest, NsfFpfEncodingSchemeTest) {
  nsf_policy::NsfTeWeightEncoding encoding;
  encoding.fpf_l2_encoding() = nsf_policy::NsfFpfL2TeWeightEncoding();
  encoding.fpf_l2_encoding()->rack_id() = 8;
  encoding.fpf_l2_encoding()->spine_id() = 16;
  encoding.fpf_l2_encoding()->remote_rack_capacity() = 8;

  const auto lengths = encodingSchemeToVector(encoding);
  EXPECT_EQ(std::vector<size_t>({8, 16, 8}), lengths);

  auto encodedLbw = encodeValue(0, 0xab, 0, lengths);
  encodedLbw = encodeValue(encodedLbw, 0x1234, 1, lengths);
  encodedLbw = encodeValue(encodedLbw, 0xcd, 2, lengths);
  EXPECT_EQ(0xcd1234ab, encodedLbw);

  const auto values = decodeValues(encodedLbw, encoding);
  EXPECT_EQ(0xab, values.at("rack_id"));
  EXPECT_EQ(0xcd, values.at("remote_rack_capacity"));
  EXPECT_EQ(0x1234, values.at("spine_id"));

  const auto encodeTerm = createBgpPolicyTerm(
      "encode-spine-id",
      "",
      {},
      {createBgpPolicyLbwExtCommunityAction(
          bgp_policy::LbwExtCommunityActionType::ENCODE_SWITCH_ID,
          encoding,
          1)},
      bgp_policy::FlowControlAction::NEXT_TERM);
  const auto policies = createBgpPolicies("fpf-policy", {encodeTerm});
  EXPECT_NO_THROW(
      validateSwitchIdEncoding(policies, {"fpf-policy"}, size_t{65535}));
  EXPECT_THROW(
      validateSwitchIdEncoding(policies, {"fpf-policy"}, size_t{65536}),
      BgpError);
}

TEST_F(PolicyTest, ValidateSwitchIdEncodingTest) {
  nsf_policy::NsfTeWeightEncoding encoding;
  encoding.l2_encoding() = nsf_policy::NsfL2TeWeightEncoding();
  encoding.l2_encoding()->rack_id() = 4;
  encoding.l2_encoding()->plane_id() = 4;
  encoding.l2_encoding()->remote_rack_capacity() = 8;
  encoding.l2_encoding()->spine_capacity() = 8;
  encoding.l2_encoding()->local_rack_capacity() = 8;

  auto encodeTerm = createBgpPolicyTerm(
      "encode-switch-id",
      "",
      {},
      {createBgpPolicyLbwExtCommunityAction(
          bgp_policy::LbwExtCommunityActionType::ENCODE_SWITCH_ID,
          encoding,
          0)},
      bgp_policy::FlowControlAction::NEXT_TERM);
  auto encodePolicy = createBgpPolicyStatement("encode-policy");
  encodePolicy.policy_entries() = {encodeTerm};

  auto unrelatedPolicy = createBgpPolicyStatement("unrelated-policy");
  bgp_policy::BgpPolicies policies;
  policies.bgp_policy_statements() = {encodePolicy, unrelatedPolicy};

  EXPECT_NO_THROW(
      validateSwitchIdEncoding(policies, {"encode-policy"}, size_t{15}));
  EXPECT_THROW(
      validateSwitchIdEncoding(policies, {"encode-policy"}, size_t{16}),
      BgpError);
  EXPECT_THROW(
      validateSwitchIdEncoding(policies, {"encode-policy"}, size_t{1024}),
      BgpError);
  EXPECT_THROW(
      validateSwitchIdEncoding(policies, {"encode-policy"}, std::nullopt),
      BgpError);
  EXPECT_NO_THROW(
      validateSwitchIdEncoding(policies, {"unrelated-policy"}, size_t{1024}));
}

TEST_F(PolicyTest, ValidateSwitchIdEncodingFollowsNextPolicyTest) {
  nsf_policy::NsfTeWeightEncoding encoding;
  encoding.l2_encoding() = nsf_policy::NsfL2TeWeightEncoding();
  encoding.l2_encoding()->rack_id() = 4;
  encoding.l2_encoding()->plane_id() = 4;
  encoding.l2_encoding()->remote_rack_capacity() = 8;
  encoding.l2_encoding()->spine_capacity() = 8;
  encoding.l2_encoding()->local_rack_capacity() = 8;

  bgp_policy::BgpPolicyAction nextPolicyAction;
  nextPolicyAction.next_policy_id() = "encode-policy";
  auto entryPolicy = createBgpPolicyStatement("entry-policy");
  entryPolicy.policy_entries() = {createBgpPolicyTerm(
      "next-policy",
      "",
      {},
      {nextPolicyAction},
      bgp_policy::FlowControlAction::NEXT_TERM)};
  auto encodePolicy = createBgpPolicyStatement("encode-policy");
  encodePolicy.policy_entries() = {createBgpPolicyTerm(
      "encode-switch-id",
      "",
      {},
      {createBgpPolicyLbwExtCommunityAction(
          bgp_policy::LbwExtCommunityActionType::ENCODE_SWITCH_ID,
          encoding,
          0)},
      bgp_policy::FlowControlAction::NEXT_TERM)};
  bgp_policy::BgpPolicies policies;
  policies.bgp_policy_statements() = {entryPolicy, encodePolicy};

  EXPECT_THROW(
      validateSwitchIdEncoding(policies, {"entry-policy"}, size_t{16}),
      BgpError);
}

TEST_F(PolicyTest, NsfEncodeValueTest) {
  // prepare a scheme
  nsf_policy::NsfTeWeightEncoding encoding;
  encoding.l2_encoding() = nsf_policy::NsfL2TeWeightEncoding();
  encoding.l2_encoding()->rack_id() = 4;
  encoding.l2_encoding()->plane_id() = 4;
  encoding.l2_encoding()->remote_rack_capacity() = 8;
  encoding.l2_encoding()->spine_capacity() = 8;
  encoding.l2_encoding()->local_rack_capacity() = 8;
  auto lengths = encodingSchemeToVector(encoding);
  {
    /*
     * try encoding rack_id
     * 5 is 0xb101
     */
    auto encodedLbw = encodeValue(0, 5, 0, lengths);
    EXPECT_EQ(5, encodedLbw);
  }

  {
    /*
     * try encoding plane_id
     * 7 is 0xb111
     * 112 is 0xb1110000
     */
    auto encodedLbw = encodeValue(0, 7, 1, lengths);
    EXPECT_EQ(112, encodedLbw);
  }

  {
    /*
     * try encoding remote_rack_capacity
     * 36 is 0xb100100
     * 9216 is 0xb10010000000000
     */
    auto encodedLbw = encodeValue(0, 36, 2, lengths);
    EXPECT_EQ(9216, encodedLbw);
  }

  {
    /*
     * try encoding spine_capacity
     * 36 is 0xb100100
     * 2359296 is 0xb1001000000000000000000
     */
    auto encodedLbw = encodeValue(0, 36, 3, lengths);
    EXPECT_EQ(2359296, encodedLbw);
  }

  {
    /*
     * try encoding local_rack_capacity
     * 6 is 0xb110
     * 100663296 is 0xb110000000000000000000000000
     */
    auto encodedLbw = encodeValue(0, 6, 4, lengths);
    EXPECT_EQ(100663296, encodedLbw);
  }
}

TEST_F(PolicyTest, NsfDecodeValueTest) {
  // prepare a scheme
  nsf_policy::NsfTeWeightEncoding encoding;
  encoding.l2_encoding() = nsf_policy::NsfL2TeWeightEncoding();
  encoding.l2_encoding()->rack_id() = 4;
  encoding.l2_encoding()->plane_id() = 4;
  encoding.l2_encoding()->remote_rack_capacity() = 8;
  encoding.l2_encoding()->spine_capacity() = 8;
  encoding.l2_encoding()->local_rack_capacity() = 8;

  // encode all fields
  auto lengths = encodingSchemeToVector(encoding);
  auto encodedLbw = encodeValue(0, 6, 0, lengths);
  encodedLbw = encodeValue(encodedLbw, 8, 1, lengths);
  encodedLbw = encodeValue(encodedLbw, 36, 2, lengths);
  encodedLbw = encodeValue(encodedLbw, 36, 3, lengths);
  encodedLbw = encodeValue(encodedLbw, 6, 4, lengths);

  // 103031942 is 0xb(110)(00100100)(00100100)(1000)(0110)
  EXPECT_EQ(103031942, encodedLbw);

  auto values = decodeValues(encodedLbw, encoding);
  EXPECT_EQ(6, values["rack_id"]);
  EXPECT_EQ(8, values["plane_id"]);
  EXPECT_EQ(36, values["remote_rack_capacity"]);
  EXPECT_EQ(36, values["spine_capacity"]);
  EXPECT_EQ(6, values["local_rack_capacity"]);
}

TEST_F(PolicyTest, NsfDecodeCapacityTest) {
  // prepare a scheme
  nsf_policy::NsfTeWeightEncoding encoding;
  encoding.l2_encoding() = nsf_policy::NsfL2TeWeightEncoding();
  encoding.l2_encoding()->rack_id() = 4;
  encoding.l2_encoding()->plane_id() = 4;
  encoding.l2_encoding()->remote_rack_capacity() = 8;
  encoding.l2_encoding()->spine_capacity() = 8;
  encoding.l2_encoding()->local_rack_capacity() = 8;

  // encode all fields
  auto lengths = encodingSchemeToVector(encoding);
  auto encodedLbw = encodeValue(0, 6, 0, lengths);
  encodedLbw = encodeValue(encodedLbw, 8, 1, lengths);
  encodedLbw = encodeValue(encodedLbw, 36, 2, lengths);
  encodedLbw = encodeValue(encodedLbw, 36, 3, lengths);
  encodedLbw = encodeValue(encodedLbw, 6, 4, lengths);

  auto aggCapacity = decodeAndAggregateCapacity(encodedLbw, encoding);
  EXPECT_EQ(36.0f, aggCapacity);

  // test with 0 capacity
  encodedLbw = encodeValue(encodedLbw, 0, 2, lengths);

  aggCapacity = decodeAndAggregateCapacity(encodedLbw, encoding);
  EXPECT_EQ(0.0f, aggCapacity);
}

TEST_F(PolicyTest, NsfEncodingBorderTest) {
  // prepare a scheme that uses all 32 bits
  nsf_policy::NsfTeWeightEncoding encoding;
  encoding.l2_encoding() = nsf_policy::NsfL2TeWeightEncoding();
  encoding.l2_encoding()->rack_id() = 4;
  encoding.l2_encoding()->plane_id() = 4;
  encoding.l2_encoding()->remote_rack_capacity() = 8;
  encoding.l2_encoding()->spine_capacity() = 8;
  encoding.l2_encoding()->local_rack_capacity() = 8;

  // encode all fields using max possible value
  auto lengths = encodingSchemeToVector(encoding);
  auto encodedLbw = encodeValue(0, 15, 0, lengths);
  encodedLbw = encodeValue(encodedLbw, 15, 1, lengths);
  encodedLbw = encodeValue(encodedLbw, 255, 2, lengths);
  encodedLbw = encodeValue(encodedLbw, 255, 3, lengths);
  encodedLbw = encodeValue(encodedLbw, 255, 4, lengths);

  EXPECT_EQ(UINT32_MAX, encodedLbw);
  auto values = decodeValues(encodedLbw, encoding);
  EXPECT_EQ(15, values["rack_id"]);
  EXPECT_EQ(15, values["plane_id"]);
  EXPECT_EQ(255, values["remote_rack_capacity"]);
  EXPECT_EQ(255, values["spine_capacity"]);
  EXPECT_EQ(255, values["local_rack_capacity"]);
}

TEST_F(PolicyTest, NsfEncodingOverflowTest) {
  // prepare a scheme that uses all 32 bits
  nsf_policy::NsfTeWeightEncoding encoding;
  encoding.l2_encoding() = nsf_policy::NsfL2TeWeightEncoding();
  encoding.l2_encoding()->rack_id() = 4;
  encoding.l2_encoding()->plane_id() = 4;
  encoding.l2_encoding()->remote_rack_capacity() = 8;
  encoding.l2_encoding()->spine_capacity() = 8;
  encoding.l2_encoding()->local_rack_capacity() = 8;
  auto lengths = encodingSchemeToVector(encoding);

  // encode a value over the field's limit 1000 > 255
  EXPECT_DEATH(
      encodeValue(0, 1000, 0, lengths),
      "value to encode exceeds number of bits");
}

TEST_F(PolicyTest, populatePolicyTermsTest) {
  /* This test directly tests Policy::populatePolicyTerms
   * which is called during Policy construction to populate
   * the internal policy terms from BgpPolicyStatement thrift objects.
   */

  /* Create one of every action type to ensure populatePolicyTerms
   * handles all action types correctly
   */
  std::vector<bgp_policy::BgpPolicyAction> actions;

  /* 1. PERMIT action */
  actions.push_back(createBgpPolicyAction(BgpPolicyActionType::PERMIT));

  /* 2. DENY action */
  actions.push_back(createBgpPolicyAction(BgpPolicyActionType::DENY));

  /* 3. SET_LOCAL_PREF action */
  actions.push_back(createActionSetLocalPreference(kLocalPref));

  /* 4. ORIGIN action */
  actions.push_back(createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::IGP));

  /* 5. NEXT_HOP action */
  actions.push_back(createBgpPolicyNexthopAction(kV4Nexthop1));

  /* 6. MED action */
  actions.push_back(createBgpPolicyMedAction(kMed));

  /* 7. WEIGHT action */
  actions.push_back(createBgpPolicyWeightAction(kWeight));

  /* 8. AS_PATH action (AS path overwrite) */
  actions.push_back(createBgpPolicyAsPathOverwriteAction({kAsn1, kAsn2}));

  /* 9. AS_PATH_PREPEND action */
  actions.push_back(createPolicySetAsPathPrependAction(kAsn1, kRepeatedTimes1));

  /* 10. COMMUNITY_LIST action */
  actions.push_back(createBgpPolicyCommunityAction(
      bgp_policy::CommunityActionType::SET, {kCommunity1, kCommunity2}));

  /* 11. EXT_COMMUNITY_LIST action */
  std::vector<bgp_policy::ExtCommunity> extCommunities;
  extCommunities.push_back(
      createExtCommunity(0x40, 0x04, "100G", "ext_comm_1", "test ext comm"));
  actions.push_back(createBgpPolicyExtCommunityAction(
      bgp_policy::BgpAttrChangeActionType::EXT_COMMUNITY_LIST_SET,
      extCommunities));

  /* 12. LBW_EXT_COMMUNITY action */
  actions.push_back(createBgpPolicyLbwExtCommunityAction(
      bgp_policy::LbwExtCommunityActionType::DECODE_ALL));

  /* 13. GOTO action */
  actions.push_back(createBgpPolicyGotoAction("term2"));

  /* Create terms with different action subsets to avoid conflicts */
  /* Term1: Basic actions */
  /* SET_LOCAL_PREF, MED, WEIGHT */
  bgp_policy::BgpPolicyTerm term1 = createBgpPolicyTerm(
      "term1",
      "Test term with basic actions",
      {},
      {actions[2], actions[5], actions[6]},
      bgp_policy::FlowControlAction::NEXT_TERM);

  /* Term2: Path manipulation actions */
  /* ORIGIN, AS_PATH, AS_PATH_PREPEND */
  bgp_policy::BgpPolicyTerm term2 = createBgpPolicyTerm(
      "term2",
      "Test term with path actions",
      {},
      {actions[3], actions[7], actions[8]},
      bgp_policy::FlowControlAction::NEXT_TERM);

  /* Term3: Community actions */
  /* COMMUNITY, EXT_COMMUNITY, ORIGIN */
  bgp_policy::BgpPolicyTerm term3 = createBgpPolicyTerm(
      "term3",
      "Test term with community actions",
      {},
      {actions[9], actions[10], actions[3]},
      bgp_policy::FlowControlAction::NEXT_TERM);

  /* Term4: Flow control actions */
  /* NEXT_HOP, PERMIT */
  bgp_policy::BgpPolicyTerm term4 = createBgpPolicyTerm(
      "term4",
      "Test term with flow control",
      {},
      {actions[4], actions[0]},
      bgp_policy::FlowControlAction::LOG_AND_ACCEPT);

  /* Create BgpPolicyStatement with all terms */
  bgp_policy::BgpPolicyStatement policyStatement;
  policyStatement.name() = "TestPolicyStatement";
  policyStatement.description() = "Test policy with all action types";
  policyStatement.policy_version() = "1";
  policyStatement.policy_entries() = {term1, term2, term3, term4};

  /* Test case: Verify populatePolicyTerms works correctly with nullptr config
   * The Policy constructor calls populatePolicyTerms(config) internally
   */
  {
    Policy policy(policyStatement, nullptr);

    /* Verify that populatePolicyTerms successfully created the policy terms */
    EXPECT_EQ("TestPolicyStatement", policy.getPolicyName());

    /* Verify that populatePolicyTerms correctly populated all terms */
    const auto& terms = policy.getPolicyTerms();
    EXPECT_EQ(4, terms.size());

    /* Verify each term was populated correctly by populatePolicyTerms */
    EXPECT_EQ("term1", terms[0]->getTermName());
    EXPECT_EQ("term2", terms[1]->getTermName());
    EXPECT_EQ("term3", terms[2]->getTermName());
    EXPECT_EQ("term4", terms[3]->getTermName());

    /* Verify that populatePolicyTerms correctly created actions for each term
     * Note: getPolicyActions() only returns attribute actions, not flow control
     * actions (PERMIT/DENY/GOTO/CONTINUE)
     */
    /* term1 has 3 actions */
    EXPECT_EQ(3, terms[0]->getPolicyActions().size());
    /* term2 has 3 actions */
    EXPECT_EQ(3, terms[1]->getPolicyActions().size());
    /* term3 has 3 actions */
    EXPECT_EQ(3, terms[2]->getPolicyActions().size());
    /* term4 has 1 attribute action (NEXT_HOP), PERMIT is flow control */
    EXPECT_EQ(1, terms[3]->getPolicyActions().size());
  }

  /* Test case: Verify populatePolicyTerms works correctly with non-nullptr
   * config The Policy constructor calls populatePolicyTerms(config) internally
   */
  {
    Policy policy(policyStatement, createTestBgpGlobalConfig());

    /* Verify that populatePolicyTerms successfully created the policy terms */
    EXPECT_EQ("TestPolicyStatement", policy.getPolicyName());

    /* Verify that populatePolicyTerms correctly populated all terms */
    const auto& terms = policy.getPolicyTerms();
    EXPECT_EQ(4, terms.size());

    /* Verify each term was populated correctly by populatePolicyTerms */
    EXPECT_EQ("term1", terms[0]->getTermName());
    EXPECT_EQ("term2", terms[1]->getTermName());
    EXPECT_EQ("term3", terms[2]->getTermName());
    EXPECT_EQ("term4", terms[3]->getTermName());

    /* Verify that populatePolicyTerms correctly created actions for each term
     * Note: getPolicyActions() only returns attribute actions, not flow control
     * actions (PERMIT/DENY/GOTO/CONTINUE)
     */
    /* term1 has 3 actions */
    EXPECT_EQ(3, terms[0]->getPolicyActions().size());
    /* term2 has 3 actions */
    EXPECT_EQ(3, terms[1]->getPolicyActions().size());
    /* term3 has 3 actions */
    EXPECT_EQ(3, terms[2]->getPolicyActions().size());
    /* term4 has 1 attribute action (NEXT_HOP), PERMIT is flow control */
    EXPECT_EQ(1, terms[3]->getPolicyActions().size());
  }

  /* Test case: Verify populatePolicyTerms works correctly with
   * LbwExtCommunityAction only (separate from ExtCommunityAction)
   */
  {
    /* Create a policy statement with only LbwExtCommunityAction */
    bgp_policy::BgpPolicyTerm lbwTerm = createBgpPolicyTerm(
        "lbwTerm",
        "Test term with LbwExtCommunityAction",
        {},
        {createBgpPolicyLbwExtCommunityAction(
            bgp_policy::LbwExtCommunityActionType::DECODE_ALL)},
        bgp_policy::FlowControlAction::LOG_AND_ACCEPT);

    bgp_policy::BgpPolicyStatement lbwPolicyStatement;
    lbwPolicyStatement.name() = "LbwOnlyPolicyStatement";
    lbwPolicyStatement.description() = "Test policy with LbwExtCommunityAction";
    lbwPolicyStatement.policy_version() = "1";
    lbwPolicyStatement.policy_entries() = {lbwTerm};

    /* Create policy with nullptr config */
    Policy lbwPolicy(lbwPolicyStatement, nullptr);

    /* Verify that populatePolicyTerms successfully created the policy terms */
    EXPECT_EQ("LbwOnlyPolicyStatement", lbwPolicy.getPolicyName());

    /* Verify that populatePolicyTerms correctly populated the term */
    const auto& lbwTerms = lbwPolicy.getPolicyTerms();
    EXPECT_EQ(1, lbwTerms.size());

    /* Verify the term was populated correctly */
    EXPECT_EQ("lbwTerm", lbwTerms[0]->getTermName());

    /* Verify that LbwExtCommunityAction was created */
    EXPECT_EQ(1, lbwTerms[0]->getPolicyActions().size());

    /* Also test with non-nullptr config */
    Policy lbwPolicyWithConfig(lbwPolicyStatement, createTestBgpGlobalConfig());

    /* Verify basic properties */
    EXPECT_EQ("LbwOnlyPolicyStatement", lbwPolicyWithConfig.getPolicyName());
    const auto& lbwTermsWithConfig = lbwPolicyWithConfig.getPolicyTerms();
    EXPECT_EQ(1, lbwTermsWithConfig.size());
    EXPECT_EQ("lbwTerm", lbwTermsWithConfig[0]->getTermName());
    EXPECT_EQ(1, lbwTermsWithConfig[0]->getPolicyActions().size());
  }
}

TEST_F(PolicyTest, ValidateConflictingExtCommunityActions) {
  const std::string policyName = "TestPolicy";

  /*
   * Test case 1: ExtCommunityAction and LbwExtCommunityAction in different
   * terms should throw an error
   */
  {
    auto extCommunityAction = createBgpPolicyExtCommunityAction(
        bgp_policy::BgpAttrChangeActionType::EXT_COMMUNITY_LIST_SET,
        {createExtCommunity(0x40, 0x04, "100G")});

    auto lbwExtCommunityAction = createBgpPolicyLbwExtCommunityAction(
        bgp_policy::LbwExtCommunityActionType::DISABLE);

    auto term1 = createBgpPolicyTerm(
        "Term1", "Term with ExtCommunityAction", {}, {extCommunityAction});
    auto term2 = createBgpPolicyTerm(
        "Term2",
        "Term with LbwExtCommunityAction",
        {},
        {lbwExtCommunityAction});

    const auto& policyConfig = createBgpPolicies(policyName, {term1, term2});

    EXPECT_THROW(
        {
          try {
            PolicyManager policyManager(
                policyConfig, createTestBgpGlobalConfig());
          } catch (const BgpError& e) {
            EXPECT_EQ(
                *e.message(),
                "PolicyStatement 'TestPolicy' cannot contain both "
                "ExtCommunityAction (in term 'Term1') and "
                "LbwExtCommunityAction (in term 'Term2')");
            throw;
          }
        },
        BgpError);
  }

  /*
   * Test case 2: ExtCommunityAction and LbwExtCommunityAction in the same term
   * should throw an error
   */
  {
    auto extCommunityAction = createBgpPolicyExtCommunityAction(
        bgp_policy::BgpAttrChangeActionType::EXT_COMMUNITY_LIST_SET,
        {createExtCommunity(0x40, 0x04, "100G")});

    auto lbwExtCommunityAction = createBgpPolicyLbwExtCommunityAction(
        bgp_policy::LbwExtCommunityActionType::DISABLE);

    auto term1 = createBgpPolicyTerm(
        "Term1",
        "Term with both actions",
        {},
        {extCommunityAction, lbwExtCommunityAction});

    const auto& policyConfig = createBgpPolicies(policyName, {term1});

    EXPECT_THROW(
        {
          try {
            PolicyManager policyManager(
                policyConfig, createTestBgpGlobalConfig());
          } catch (const BgpError& e) {
            EXPECT_EQ(
                *e.message(),
                "PolicyStatement 'TestPolicy' cannot contain both "
                "ExtCommunityAction (in term 'Term1') and "
                "LbwExtCommunityAction (in term 'Term1')");
            throw;
          }
        },
        BgpError);
  }

  // Test case 3: Only ExtCommunityAction should work fine
  {
    auto extCommunityAction = createBgpPolicyExtCommunityAction(
        bgp_policy::BgpAttrChangeActionType::EXT_COMMUNITY_LIST_SET,
        {createExtCommunity(0x40, 0x04, "100G")});

    auto term1 = createBgpPolicyTerm(
        "Term1", "Term with only ExtCommunityAction", {}, {extCommunityAction});

    const auto& policyConfig = createBgpPolicies(policyName, {term1});

    EXPECT_NO_THROW({
      PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());
    });
  }

  // Test case 4: Only LbwExtCommunityAction should work fine
  {
    auto lbwExtCommunityAction = createBgpPolicyLbwExtCommunityAction(
        bgp_policy::LbwExtCommunityActionType::DISABLE);

    auto term1 = createBgpPolicyTerm(
        "Term1",
        "Term with only LbwExtCommunityAction",
        {},
        {lbwExtCommunityAction});

    const auto& policyConfig = createBgpPolicies(policyName, {term1});

    EXPECT_NO_THROW({
      PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());
    });
  }
}

/*
 * ExtCommunity related tests (parseLinkBandwidth*,
 * handleLinkBandwidthExtCommunity*, getBgpAttrExtCommunityC*) have been moved
 * to ExtCommunityPolicyTest.cpp
 */

/*
 * hasCommunity Tests
 */

TEST_F(PolicyTest, HasCommunityFoundTest) {
  // Test that hasCommunity returns true when community is found
  std::vector<BgpAttrCommunityC> communities = {
      BgpAttrCommunityC{100, 200},
      BgpAttrCommunityC{300, 400},
      BgpAttrCommunityC{500, 600},
  };

  EXPECT_TRUE(hasCommunity(communities, BgpAttrCommunityC{100, 200}));
  EXPECT_TRUE(hasCommunity(communities, BgpAttrCommunityC{300, 400}));
  EXPECT_TRUE(hasCommunity(communities, BgpAttrCommunityC{500, 600}));
}

TEST_F(PolicyTest, HasCommunityNotFoundTest) {
  // Test that hasCommunity returns false when community is not found
  std::vector<BgpAttrCommunityC> communities = {
      BgpAttrCommunityC{100, 200},
      BgpAttrCommunityC{300, 400},
  };

  EXPECT_FALSE(hasCommunity(communities, BgpAttrCommunityC{100, 201}));
  EXPECT_FALSE(hasCommunity(communities, BgpAttrCommunityC{999, 999}));
}

TEST_F(PolicyTest, HasCommunityEmptyListTest) {
  // Test that hasCommunity returns false for empty list
  std::vector<BgpAttrCommunityC> communities;

  EXPECT_FALSE(hasCommunity(communities, BgpAttrCommunityC{100, 200}));
}

/*
 * addCommunities Tests
 */

TEST_F(PolicyTest, AddCommunitiesToEmptyTest) {
  // Test adding communities to an empty list
  BgpAttrCommunitiesC current;
  BgpAttrCommunitiesC toAdd{{
      BgpAttrCommunityC{100, 200},
      BgpAttrCommunityC{300, 400},
  }};

  auto result = addCommunities(current, toAdd);

  EXPECT_EQ(2, result.size());
  EXPECT_TRUE(hasCommunity(result, BgpAttrCommunityC{100, 200}));
  EXPECT_TRUE(hasCommunity(result, BgpAttrCommunityC{300, 400}));
}

TEST_F(PolicyTest, AddCommunitiesToExistingTest) {
  // Test adding communities to existing list
  BgpAttrCommunitiesC current{{
      BgpAttrCommunityC{100, 200},
  }};
  BgpAttrCommunitiesC toAdd{{
      BgpAttrCommunityC{300, 400},
  }};

  auto result = addCommunities(current, toAdd);

  EXPECT_EQ(2, result.size());
  EXPECT_TRUE(hasCommunity(result, BgpAttrCommunityC{100, 200}));
  EXPECT_TRUE(hasCommunity(result, BgpAttrCommunityC{300, 400}));
}

TEST_F(PolicyTest, AddCommunitiesNoDuplicatesTest) {
  // Test that duplicates are not added
  BgpAttrCommunitiesC current{{
      BgpAttrCommunityC{100, 200},
      BgpAttrCommunityC{300, 400},
  }};
  BgpAttrCommunitiesC toAdd{{
      BgpAttrCommunityC{300, 400}, // duplicate
      BgpAttrCommunityC{500, 600}, // new
  }};

  auto result = addCommunities(current, toAdd);

  EXPECT_EQ(3, result.size());
  EXPECT_TRUE(hasCommunity(result, BgpAttrCommunityC{100, 200}));
  EXPECT_TRUE(hasCommunity(result, BgpAttrCommunityC{300, 400}));
  EXPECT_TRUE(hasCommunity(result, BgpAttrCommunityC{500, 600}));
}

TEST_F(PolicyTest, AddCommunitiesEmptyToAddTest) {
  // Test adding empty list
  BgpAttrCommunitiesC current{{
      BgpAttrCommunityC{100, 200},
  }};
  BgpAttrCommunitiesC toAdd;

  auto result = addCommunities(current, toAdd);

  EXPECT_EQ(1, result.size());
  EXPECT_TRUE(hasCommunity(result, BgpAttrCommunityC{100, 200}));
}

/*
 * removeCommunities Tests
 */

TEST_F(PolicyTest, RemoveCommunitiesSpecificTest) {
  // Test removing specific communities
  BgpAttrCommunitiesC input{{
      BgpAttrCommunityC{100, 200},
      BgpAttrCommunityC{300, 400},
      BgpAttrCommunityC{500, 600},
  }};
  BgpAttrCommunitiesC toRemove{{
      BgpAttrCommunityC{300, 400},
  }};

  auto result = removeCommunities(input, toRemove);

  EXPECT_EQ(2, result.size());
  EXPECT_TRUE(hasCommunity(result, BgpAttrCommunityC{100, 200}));
  EXPECT_FALSE(hasCommunity(result, BgpAttrCommunityC{300, 400}));
  EXPECT_TRUE(hasCommunity(result, BgpAttrCommunityC{500, 600}));
}

TEST_F(PolicyTest, RemoveCommunitiesNonExistentTest) {
  // Test removing communities that don't exist
  BgpAttrCommunitiesC input{{
      BgpAttrCommunityC{100, 200},
      BgpAttrCommunityC{300, 400},
  }};
  BgpAttrCommunitiesC toRemove{{
      BgpAttrCommunityC{500, 600}, // doesn't exist
  }};

  auto result = removeCommunities(input, toRemove);

  EXPECT_EQ(2, result.size());
  EXPECT_TRUE(hasCommunity(result, BgpAttrCommunityC{100, 200}));
  EXPECT_TRUE(hasCommunity(result, BgpAttrCommunityC{300, 400}));
}

TEST_F(PolicyTest, RemoveCommunitiesAllTest) {
  // Test removing all communities
  BgpAttrCommunitiesC input{{
      BgpAttrCommunityC{100, 200},
      BgpAttrCommunityC{300, 400},
  }};
  BgpAttrCommunitiesC toRemove{{
      BgpAttrCommunityC{100, 200},
      BgpAttrCommunityC{300, 400},
  }};

  auto result = removeCommunities(input, toRemove);

  EXPECT_EQ(0, result.size());
}

TEST_F(PolicyTest, RemoveCommunitiesFromEmptyTest) {
  // Test removing from empty list
  BgpAttrCommunitiesC input;
  BgpAttrCommunitiesC toRemove{{
      BgpAttrCommunityC{100, 200},
  }};

  auto result = removeCommunities(input, toRemove);

  EXPECT_EQ(0, result.size());
}

TEST_F(PolicyTest, RemoveCommunitiesEmptyToRemoveTest) {
  // Test removing empty list
  BgpAttrCommunitiesC input{{
      BgpAttrCommunityC{100, 200},
      BgpAttrCommunityC{300, 400},
  }};
  BgpAttrCommunitiesC toRemove;

  auto result = removeCommunities(input, toRemove);

  EXPECT_EQ(2, result.size());
  EXPECT_TRUE(hasCommunity(result, BgpAttrCommunityC{100, 200}));
  EXPECT_TRUE(hasCommunity(result, BgpAttrCommunityC{300, 400}));
}

/*
 * Verify routes with zero/missing LBW are rejected when a DECODE/ENCODE action
 * is applied.
 */

struct LbwRejectionTestResult {
  bool accepted;
  std::shared_ptr<BgpPolicyActionData> actionData;
};

static LbwRejectionTestResult applyLbwPolicyAndCheck(
    BgpPolicyActionData policyActionDataIn,
    const bgp_policy::LbwExtCommunityActionType actionType,
    const std::optional<nsf_policy::NsfTeWeightEncoding>& encoding =
        std::nullopt,
    const std::optional<int32_t> encodingId = std::nullopt) {
  auto bgpAction =
      createBgpPolicyLbwExtCommunityAction(actionType, encoding, encodingId);
  const string policyName = "LbwRejectionPolicy";
  const auto& policyConfig = createBgpPolicies(policyName, {}, {bgpAction});
  PolicyManager policyManager(policyConfig, createTestBgpGlobalConfig());

  auto attrsIn = createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
  attrsIn->publish();
  std::vector<folly::CIDRNetwork> prefixSetIn{kV4Prefix1};

  auto actionData = std::make_shared<BgpPolicyActionData>(policyActionDataIn);
  PolicyInMessage policyIn(prefixSetIn, attrsIn, actionData);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  EXPECT_EQ(1, policyOut.result.size());
  bool accepted = (policyOut.result.at(kV4Prefix1)->attrs != nullptr);
  return {accepted, actionData};
}

// Shared encoding scheme used by DECODE/ENCODE tests
static nsf_policy::NsfTeWeightEncoding makeTestEncoding() {
  nsf_policy::NsfTeWeightEncoding encoding;
  encoding.l2_encoding() = nsf_policy::NsfL2TeWeightEncoding();
  encoding.l2_encoding()->rack_id() = 4;
  encoding.l2_encoding()->plane_id() = 4;
  encoding.l2_encoding()->remote_rack_capacity() = 8;
  encoding.l2_encoding()->spine_capacity() = 8;
  encoding.l2_encoding()->local_rack_capacity() = 8;
  return encoding;
}

// ---------- Ingress: DECODE_ALL ----------

// Reject when originalAsnLbw is missing
TEST_F(PolicyTest, LbwRejection_DecodeAll_MissingLbw) {
  auto encoding = makeTestEncoding();
  auto result = applyLbwPolicyAndCheck(
      createLbwActionData(std::nullopt, 65530),
      bgp_policy::LbwExtCommunityActionType::DECODE_ALL,
      encoding);
  EXPECT_FALSE(result.accepted) << "Route with missing LBW should be denied";
  EXPECT_TRUE(result.actionData->isLbwRejected);
}

// Reject when raw LBW value is zero
TEST_F(PolicyTest, LbwRejection_DecodeAll_ZeroLbw) {
  auto encoding = makeTestEncoding();
  auto result = applyLbwPolicyAndCheck(
      createLbwActionData(std::make_pair<uint16_t, float>(65530, 0.0f), 65530),
      bgp_policy::LbwExtCommunityActionType::DECODE_ALL,
      encoding);
  EXPECT_FALSE(result.accepted) << "Route with zero LBW should be denied";
  EXPECT_TRUE(result.actionData->isLbwRejected);
}

// Accept when LBW is valid and non-zero
TEST_F(PolicyTest, LbwRejection_DecodeAll_ValidLbw) {
  auto encoding = makeTestEncoding();
  auto result = applyLbwPolicyAndCheck(
      createLbwActionData(
          std::make_pair<uint16_t, float>(65530, 100.0f), 65530),
      bgp_policy::LbwExtCommunityActionType::DECODE_ALL,
      encoding);
  EXPECT_TRUE(result.accepted) << "Route with valid LBW should be accepted";
  EXPECT_FALSE(result.actionData->isLbwRejected);
}

/*
 * Reproduce P0: DECODE_ALL crashes when originalAsnLbw has a negative float.
 * setNonTransitiveLbwExtCommunity stores the negative value, but
 * getNonTransitiveRawLbwValue() skips it (lbwValue < 0) and returns nullopt.
 * The code then dereferences the nullopt at PolicyAction.cpp line 270.
 */
TEST_F(PolicyTest, LbwRejection_DecodeAll_NegativeLbwCrash) {
  auto encoding = makeTestEncoding();
  auto result = applyLbwPolicyAndCheck(
      createLbwActionData(std::make_pair<uint16_t, float>(65530, -1.0f), 65530),
      bgp_policy::LbwExtCommunityActionType::DECODE_ALL,
      encoding);
  EXPECT_FALSE(result.accepted) << "Route with negative LBW should be denied";
  EXPECT_TRUE(result.actionData->isLbwRejected);
}

// ---------- Ingress: DECODE_AGGREGATE_CAPACITY_OVERWRITE ----------

// Reject when curLbwValue is missing after restore
TEST_F(PolicyTest, LbwRejection_DecodeAggCapacity_MissingLbw) {
  auto encoding = makeTestEncoding();
  auto result = applyLbwPolicyAndCheck(
      createLbwActionData(std::nullopt, 65530),
      bgp_policy::LbwExtCommunityActionType::
          DECODE_AGGREGATE_CAPACITY_OVERWRITE,
      encoding);
  EXPECT_FALSE(result.accepted) << "Route with missing LBW should be denied";
  EXPECT_TRUE(result.actionData->isLbwRejected);
}

// ---------- Egress: ENCODE_AGGREGATE_RECEIVED_OVERWRITE ----------

// Reject when aggregateReceivedUcmpWeight is missing
TEST_F(PolicyTest, LbwRejection_EncodeAggReceived_MissingAggregate) {
  auto encoding = makeTestEncoding();
  auto result = applyLbwPolicyAndCheck(
      createLbwActionData(
          std::make_pair<uint16_t, float>(65530, 100.0f),
          65530,
          std::nullopt, /* switchId */
          std::nullopt, /* multiPathSize */
          std::nullopt, /* linkBandwidthBps */
          std::nullopt /* aggregateReceivedUcmpWeight - missing */),
      bgp_policy::LbwExtCommunityActionType::
          ENCODE_AGGREGATE_RECEIVED_OVERWRITE,
      encoding,
      2 /* encodingId */);
  EXPECT_FALSE(result.accepted)
      << "Route with missing aggregate received should be denied";
  EXPECT_TRUE(result.actionData->isLbwRejected);
}

// Reject when aggregateReceivedUcmpWeight is zero
TEST_F(PolicyTest, LbwRejection_EncodeAggReceived_ZeroAggregate) {
  auto encoding = makeTestEncoding();
  auto result = applyLbwPolicyAndCheck(
      createLbwActionData(
          std::make_pair<uint16_t, float>(65530, 100.0f),
          65530,
          std::nullopt, /* switchId */
          std::nullopt, /* multiPathSize */
          std::nullopt, /* linkBandwidthBps */
          0.0f /* aggregateReceivedUcmpWeight - zero */),
      bgp_policy::LbwExtCommunityActionType::
          ENCODE_AGGREGATE_RECEIVED_OVERWRITE,
      encoding,
      2 /* encodingId */);
  EXPECT_FALSE(result.accepted)
      << "Route with zero aggregate received should be denied";
  EXPECT_TRUE(result.actionData->isLbwRejected);
}

// Accept when aggregateReceivedUcmpWeight is valid
TEST_F(PolicyTest, LbwRejection_EncodeAggReceived_ValidAggregate) {
  auto encoding = makeTestEncoding();
  auto result = applyLbwPolicyAndCheck(
      createLbwActionData(
          std::make_pair<uint16_t, float>(65530, 100.0f),
          65530,
          std::nullopt, /* switchId */
          std::nullopt, /* multiPathSize */
          std::nullopt, /* linkBandwidthBps */
          8.0f /* aggregateReceivedUcmpWeight - valid */),
      bgp_policy::LbwExtCommunityActionType::
          ENCODE_AGGREGATE_RECEIVED_OVERWRITE,
      encoding,
      2 /* encodingId */);
  EXPECT_TRUE(result.accepted)
      << "Route with valid aggregate received should be accepted";
  EXPECT_FALSE(result.actionData->isLbwRejected);
}

// ---------- Egress: ENCODE_SWITCH_ID ----------

// Accept with missing base LBW — normal at first encode hop
TEST_F(PolicyTest, LbwRejection_EncodeSwitchId_MissingLbw_NoReject) {
  auto encoding = makeTestEncoding();
  auto result = applyLbwPolicyAndCheck(
      createLbwActionData(
          std::nullopt /* no original LBW */, 65530, 1 /* switchId */),
      bgp_policy::LbwExtCommunityActionType::ENCODE_SWITCH_ID,
      encoding,
      1 /* encodingId */);
  EXPECT_TRUE(result.accepted)
      << "ENCODE_SWITCH_ID should accept even with missing base LBW";
  EXPECT_FALSE(result.actionData->isLbwRejected);
}

// Accept with zero base LBW — encode produces valid output with switch_id
TEST_F(PolicyTest, LbwRejection_EncodeSwitchId_ZeroLbw_NoReject) {
  auto encoding = makeTestEncoding();
  auto result = applyLbwPolicyAndCheck(
      createLbwActionData(
          std::make_pair<uint16_t, float>(65530, 0.0f),
          65530,
          1 /* switchId */),
      bgp_policy::LbwExtCommunityActionType::ENCODE_SWITCH_ID,
      encoding,
      1 /* encodingId */);
  EXPECT_TRUE(result.accepted)
      << "ENCODE_SWITCH_ID should accept even with zero base LBW";
  EXPECT_FALSE(result.actionData->isLbwRejected);
}

// Accept with valid base LBW and switch_id
TEST_F(PolicyTest, LbwRejection_EncodeSwitchId_ValidLbw) {
  auto encoding = makeTestEncoding();
  auto result = applyLbwPolicyAndCheck(
      createLbwActionData(
          std::make_pair<uint16_t, float>(65530, 100.0f),
          65530,
          1 /* switchId */),
      bgp_policy::LbwExtCommunityActionType::ENCODE_SWITCH_ID,
      encoding,
      1 /* encodingId */);
  EXPECT_TRUE(result.accepted) << "Route with valid LBW should be accepted";
  EXPECT_FALSE(result.actionData->isLbwRejected);
}

// ---------- Egress: ENCODE_MULTIPATH ----------

// Accept with missing base LBW — normal at first encode hop
TEST_F(PolicyTest, LbwRejection_EncodeMultipath_MissingLbw_NoReject) {
  auto encoding = makeTestEncoding();
  auto result = applyLbwPolicyAndCheck(
      createLbwActionData(
          std::nullopt /* no original LBW */,
          65530,
          std::nullopt, /* switchId */
          8 /* multiPathSize */),
      bgp_policy::LbwExtCommunityActionType::ENCODE_MULTIPATH,
      encoding,
      2 /* encodingId */);
  EXPECT_TRUE(result.accepted)
      << "ENCODE_MULTIPATH should accept even with missing base LBW";
  EXPECT_FALSE(result.actionData->isLbwRejected);
}

// Accept with valid base LBW and multiPathSize
TEST_F(PolicyTest, LbwRejection_EncodeMultipath_ValidLbw) {
  auto encoding = makeTestEncoding();
  auto result = applyLbwPolicyAndCheck(
      createLbwActionData(
          std::make_pair<uint16_t, float>(65530, 100.0f),
          65530,
          std::nullopt, /* switchId */
          8 /* multiPathSize */),
      bgp_policy::LbwExtCommunityActionType::ENCODE_MULTIPATH,
      encoding,
      2 /* encodingId */);
  EXPECT_TRUE(result.accepted) << "Route with valid LBW should be accepted";
  EXPECT_FALSE(result.actionData->isLbwRejected);
}

// Generic ACCEPT remains permissive when no GAR encoding scheme is attached.
TEST_F(PolicyTest, LbwRejection_AcceptAction_NoEncoding_NoRejection) {
  auto result = applyLbwPolicyAndCheck(
      createLbwActionData(std::nullopt /* no original LBW */, 65530),
      bgp_policy::LbwExtCommunityActionType::ACCEPT);
  EXPECT_TRUE(result.accepted);
  EXPECT_FALSE(result.actionData->isLbwRejected);
}

TEST_F(PolicyTest, LbwRejection_GarAcceptAction_MissingLbw) {
  auto encoding = makeTestEncoding();
  auto result = applyLbwPolicyAndCheck(
      createLbwActionData(std::nullopt /* no original LBW */, 65530),
      bgp_policy::LbwExtCommunityActionType::ACCEPT,
      encoding);
  EXPECT_FALSE(result.accepted);
  EXPECT_TRUE(result.actionData->isLbwRejected);
}

TEST_F(PolicyTest, LbwRejection_GarAcceptAction_ZeroLbw) {
  auto encoding = makeTestEncoding();
  auto result = applyLbwPolicyAndCheck(
      createLbwActionData(std::make_pair<uint16_t, float>(65530, 0.0f), 65530),
      bgp_policy::LbwExtCommunityActionType::ACCEPT,
      encoding);
  EXPECT_FALSE(result.accepted);
  EXPECT_TRUE(result.actionData->isLbwRejected);
}

TEST_F(PolicyTest, LbwRejection_GarAcceptAction_ValidLbw) {
  auto encoding = makeTestEncoding();
  auto result = applyLbwPolicyAndCheck(
      createLbwActionData(
          std::make_pair<uint16_t, float>(65530, 100.0f), 65530),
      bgp_policy::LbwExtCommunityActionType::ACCEPT,
      encoding);
  EXPECT_TRUE(result.accepted);
  EXPECT_FALSE(result.actionData->isLbwRejected);
}

// DISABLE action should never reject, even with missing LBW
TEST_F(PolicyTest, LbwRejection_DisableAction_NoRejection) {
  auto result = applyLbwPolicyAndCheck(
      createLbwActionData(std::nullopt /* no original LBW */, 65530),
      bgp_policy::LbwExtCommunityActionType::DISABLE);
  EXPECT_TRUE(result.accepted)
      << "Route with DISABLE action should never be rejected";
  EXPECT_FALSE(result.actionData->isLbwRejected);
}

// Verify empty_gar_weights_rejects counter increments on rejection
TEST_F(PolicyTest, LbwRejection_CounterIncrements) {
  PeerStats::initCounters();
  auto counters = facebook::fb303::ThreadCachedServiceData::getShared();
  facebook::fb303::ThreadCachedServiceData::get()->publishStats();
  auto before =
      counters->getCounter(PeerStats::kEmptyGarWeightsRejects + ".count");

  auto encoding = makeTestEncoding();
  applyLbwPolicyAndCheck(
      createLbwActionData(std::nullopt, 65530),
      bgp_policy::LbwExtCommunityActionType::ACCEPT,
      encoding);

  facebook::fb303::ThreadCachedServiceData::get()->publishStats();
  auto after =
      counters->getCounter(PeerStats::kEmptyGarWeightsRejects + ".count");
  EXPECT_EQ(after, before + 1);
}

// Verify counter does not increment when route is accepted
TEST_F(PolicyTest, LbwRejection_CounterNoIncrementOnAccept) {
  PeerStats::initCounters();
  auto counters = facebook::fb303::ThreadCachedServiceData::getShared();
  facebook::fb303::ThreadCachedServiceData::get()->publishStats();
  auto before =
      counters->getCounter(PeerStats::kEmptyGarWeightsRejects + ".count");

  applyLbwPolicyAndCheck(
      createLbwActionData(std::nullopt, 65530),
      bgp_policy::LbwExtCommunityActionType::DISABLE);

  facebook::fb303::ThreadCachedServiceData::get()->publishStats();
  auto after =
      counters->getCounter(PeerStats::kEmptyGarWeightsRejects + ".count");
  EXPECT_EQ(after, before);
}

/*
 * Reproduce P1: Duplicate policy statement names are silently dropped.
 * CommunityList, AsPathList, PrefixList all throw on duplicates,
 * but PolicyStatement only logs. This allows silently broken configs.
 */
TEST_F(PolicyTest, DuplicatePolicyStatementNameShouldThrow) {
  auto policies = createBgpPolicies("TestPolicy", {}, {});
  auto stmt = policies.bgp_policy_statements()->at(0);
  policies.bgp_policy_statements()->push_back(stmt);

  EXPECT_THROW(PolicyManager(policies, createTestBgpGlobalConfig()), BgpError)
      << "Duplicate PolicyStatement names should throw, not be silently dropped";
}

/*
 * Reproduce P1: CommunityMatch::PopulateReferences crashes when a
 * referenced CommunityList has no inline communities field.
 * PopulateCommunities dereferences communities() without null check.
 */
TEST_F(PolicyTest, CommunityMatchPopulateReferencesNullCommunities) {
  bgp_policy::CommunityList mainList;
  mainList.name() = "main_list";
  mainList.boolean_operator() = routing_policy::BooleanOperator::OR;
  mainList.communities() = {"65000:100"};
  mainList.community_list_names() = {"ref_list"};

  bgp_policy::BgpPolicyAtomicMatch atomicMatch;
  atomicMatch.communities_filter() = mainList;

  CommunityMatch match(atomicMatch);

  bgp_policy::CommunityList refList;
  refList.name() = "ref_list";
  refList.boolean_operator() = routing_policy::BooleanOperator::OR;

  folly::F14NodeMap<std::string, bgp_policy::CommunityList> communityListMap;
  communityListMap["ref_list"] = refList;

  EXPECT_NO_THROW(match.PopulateReferences(communityListMap))
      << "Should handle referenced CommunityList with no inline communities";
}

/*
 * Test policy default action with enablePolicyDefaultAction=true and
 * result=ACCEPT. Unmatched prefixes should be allowed (attrs != nullptr).
 */
TEST_F(PolicyTest, PolicyDefaultActionAccept) {
  /*
   * Create a policy with one term that matches EGP origin.
   * Input will be IGP so it won't match — triggering the default action.
   */
  auto matchEgp = createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::ORIGIN);
  matchEgp.origin() = bgp_policy::Origin::EGP;

  auto actionIgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::IGP);

  const string policyName = "DefaultActionAcceptPolicy";
  auto bgpPolicies = createBgpPolicies(policyName, {matchEgp}, {actionIgp});

  // Set the policy statement result to ACCEPT
  bgpPolicies.bgp_policy_statements()[0].result() =
      bgp_policy::FlowControlAction::ACCEPT;

  auto configWithDefaultAction = createConfigWithPolicyDefaultAction();
  PolicyManager policyManager(bgpPolicies, &configWithDefaultAction);

  // Create input with IGP origin — won't match the EGP term
  auto attrsIn = createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
  attrsIn->publish();
  std::vector<folly::CIDRNetwork> prefixSetIn{kV6Prefix1, kV6Prefix2};

  PolicyInMessage policyIn(prefixSetIn, attrsIn);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  // Both prefixes should be in result
  ASSERT_EQ(prefixSetIn.size(), policyOut.result.size());
  // Default action is ACCEPT — unmatched prefixes should be allowed
  EXPECT_NE(nullptr, policyOut.result.at(kV6Prefix1)->attrs);
  EXPECT_NE(nullptr, policyOut.result.at(kV6Prefix2)->attrs);
}

/*
 * Test policy default action with enablePolicyDefaultAction=true and
 * result=DENY (default). Unmatched prefixes should be denied (attrs ==
 * nullptr).
 */
TEST_F(PolicyTest, PolicyDefaultActionDeny) {
  /*
   * Create a policy with one term that matches EGP origin.
   * Input will be IGP so it won't match — triggering the default action.
   */
  auto matchEgp = createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::ORIGIN);
  matchEgp.origin() = bgp_policy::Origin::EGP;

  auto actionIgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::IGP);

  const string policyName = "DefaultActionDenyPolicy";
  auto bgpPolicies = createBgpPolicies(policyName, {matchEgp}, {actionIgp});

  // result defaults to DENY (per thrift definition), no need to set it

  auto configWithDefaultAction = createConfigWithPolicyDefaultAction();
  PolicyManager policyManager(bgpPolicies, &configWithDefaultAction);

  // Create input with IGP origin — won't match the EGP term
  auto attrsIn = createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
  attrsIn->publish();
  std::vector<folly::CIDRNetwork> prefixSetIn{kV6Prefix1, kV6Prefix2};

  PolicyInMessage policyIn(prefixSetIn, attrsIn);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  // Both prefixes should be in result
  ASSERT_EQ(prefixSetIn.size(), policyOut.result.size());
  // Default action is DENY — unmatched prefixes should be denied
  EXPECT_EQ(nullptr, policyOut.result.at(kV6Prefix1)->attrs);
  EXPECT_EQ(nullptr, policyOut.result.at(kV6Prefix2)->attrs);
}

/*
 * Test that the feature flag gates the behavior. With result=ACCEPT but
 * enablePolicyDefaultAction=false, unmatched prefixes should still be denied.
 */
TEST_F(PolicyTest, PolicyDefaultActionFeatureFlagDisabled) {
  /*
   * Create a policy with one term that matches EGP origin.
   * Input will be IGP so it won't match — triggering the default action.
   */
  auto matchEgp = createBgpPolicyAtomicMatch(BgpPolicyAtomicMatchType::ORIGIN);
  matchEgp.origin() = bgp_policy::Origin::EGP;

  auto actionIgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::IGP);

  const string policyName = "DefaultActionFlagDisabledPolicy";
  auto bgpPolicies = createBgpPolicies(policyName, {matchEgp}, {actionIgp});

  // Set the policy statement result to ACCEPT
  bgpPolicies.bgp_policy_statements()[0].result() =
      bgp_policy::FlowControlAction::ACCEPT;

  // Use createTestBgpGlobalConfig() which has enablePolicyDefaultAction=false
  PolicyManager policyManager(bgpPolicies, createTestBgpGlobalConfig());

  // Create input with IGP origin — won't match the EGP term
  auto attrsIn = createBgpPathWithOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
  attrsIn->publish();
  std::vector<folly::CIDRNetwork> prefixSetIn{kV6Prefix1, kV6Prefix2};

  PolicyInMessage policyIn(prefixSetIn, attrsIn);
  auto policyOut = policyManager.applyPolicy(policyName, policyIn);

  // Both prefixes should be in result
  ASSERT_EQ(prefixSetIn.size(), policyOut.result.size());
  /*
   * Feature flag is disabled — even though result=ACCEPT, unmatched prefixes
   * should be denied (backwards compatible behavior)
   */
  EXPECT_EQ(nullptr, policyOut.result.at(kV6Prefix1)->attrs);
  EXPECT_EQ(nullptr, policyOut.result.at(kV6Prefix2)->attrs);
}

} // namespace facebook::bgp
