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

#include <folly/coro/AsyncScope.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Sleep.h>
#include <folly/coro/Task.h>
#include <folly/synchronization/Baton.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#define RibBase_TEST_FRIENDS                                                   \
  friend class RibFixtureAddPathTestSuite;                                     \
  friend class RibFsdbFixture;                                                 \
  FRIEND_TEST(RibFixtureAddPathTestSuite, LbwProgramFibTest);                  \
  FRIEND_TEST(                                                                 \
      RibFixtureAddPathTestSuite, SetGetClearRouteAttributePolicyTest);        \
  FRIEND_TEST(                                                                 \
      RibFixtureAddPathTestSuite,                                              \
      FallbackToECMPOnClearRouteAttributePolicyTest);                          \
  FRIEND_TEST(                                                                 \
      RibFixtureAddPathTestSuite,                                              \
      FallbackToECMPOnEmptyRouteAttributePolicyTest);                          \
  FRIEND_TEST(                                                                 \
      RibFixtureAddPathTestSuite,                                              \
      FallbackToUCMPOnClearRouteAttributePolicyTest);                          \
  FRIEND_TEST(RibFixtureAddPathTestSuite, ReApplyRouteAttributePolicyTest);    \
  FRIEND_TEST(RibFsdbAddPathTestSuite, ReplaceRouteAttributePolicyTest);       \
  FRIEND_TEST(RibFixtureAddPathTestSuite, CreateTRibEntryWithCteUcmpAction);   \
  FRIEND_TEST(                                                                 \
      RibFixtureAddPathTestSuite,                                              \
      CacheMigrationDetectsNewStatementsWithEqualCount);                       \
  FRIEND_TEST(                                                                 \
      RibFixtureAddPathTestSuite,                                              \
      CacheMigrationIdenticalPolicyPreservesOldCache);                         \
  FRIEND_TEST(                                                                 \
      RibFixtureAddPathTestSuite, ExpirationOnlyCachePreservationTest);        \
  FRIEND_TEST(RibFixtureAddPathTestSuite, CacheMigrationStatementRemoved);     \
  FRIEND_TEST(RibFixtureAddPathTestSuite, CacheMigrationMatcherChange);        \
  FRIEND_TEST(                                                                 \
      RibFixtureAddPathTestSuite,                                              \
      CacheMigrationNegativeCachePreservedWithoutNewStatements);               \
  FRIEND_TEST(RibFixtureAddPathTestSuite, CacheMigrationBothExpiredIdentical); \
  FRIEND_TEST(                                                                 \
      RibFixtureAddPathTestSuite, SelectiveReEvaluationOnActionChange);        \
  FRIEND_TEST(RibFixtureAddPathTestSuite, FullFallbackOnFirstPolicySet);       \
  FRIEND_TEST(RibFixtureAddPathTestSuite, FullFallbackOnPolicyCleared);        \
  FRIEND_TEST(                                                                 \
      RibFixtureAddPathTestSuite, SelectiveReEvaluationOnStatementRemoved);

#define MockRib_TEST_FRIENDS                                             \
  FRIEND_TEST(RibFsdbAddPathTestSuite, ReplaceRouteAttributePolicyTest); \
  FRIEND_TEST(                                                           \
      RibFixtureAddPathTestSuite, ExpirationOnlyCachePreservationTest);  \
  FRIEND_TEST(                                                           \
      RibFixtureAddPathTestSuite, SelectiveReEvaluationOnActionChange);  \
  FRIEND_TEST(RibFixtureAddPathTestSuite, FullFallbackOnFirstPolicySet); \
  FRIEND_TEST(RibFixtureAddPathTestSuite, FullFallbackOnPolicyCleared);  \
  FRIEND_TEST(                                                           \
      RibFixtureAddPathTestSuite, SelectiveReEvaluationOnStatementRemoved);

#define RouteAttributePolicy_TEST_FRIENDS \
  FRIEND_TEST(RibFixtureAddPathTestSuite, LbwProgramFibTest);

#define RibDC_TEST_FRIENDS \
  FRIEND_TEST(             \
      RibFixtureAddPathTestSuite, StopJoinsCoroutinesBeforeResettingTimer);

#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/rib_policy_types.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibFsdbPolicyTestFixture.h"
#include "neteng/fboss/bgp/cpp/tests/RibPolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibUtils.h"

using namespace facebook::bgp::rib_policy;
using namespace facebook::neteng::fboss::bgp::thrift;

namespace facebook {
namespace bgp {

INSTANTIATE_TEST_SUITE_P(
    RibFixture,
    RibFixtureAddPathTestSuite,
    testing::Values(true /* addPath */));

INSTANTIATE_TEST_SUITE_P(
    RibFixture,
    RibFsdbAddPathTestSuite,
    testing::Values(true /* addPath */));

TEST_P(
    RibFixtureAddPathTestSuite,
    SelectedPathBackupAddrUpdatesWithoutRouteAttributePolicy) {
  const auto prefix = folly::IPAddress::createNetwork("2001:db8:1::/64");
  PrefixPathIds prefixes{{prefix, kDefaultPathID}};
  const auto sendBackupUpdate =
      [&](std::optional<folly::IPAddress> backupAddr) {
        auto attrs = std::make_shared<facebook::bgp::BgpPath>(
            *buildBgpPathFields(4, 4, 4, 4));
        attrs->setBackupAddr(backupAddr);
        attrs->publish();
        auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();

        sendAnnouncement(prefixes, eBgpPeer1_, attrs);
        ribFuture.wait();

        ASSERT_TRUE(rib_->fibItems.contains(prefix));
        const auto bestPath = rib_->fibItems.at(prefix).getBestPath();
        ASSERT_NE(nullptr, bestPath);
        EXPECT_EQ(backupAddr, bestPath->attrs->getBackupAddr());
      };

  const auto firstBackupAddr = folly::IPAddress("2001:db8:ffff::1");
  auto initialRibFuture = rib_->getRibPrepareFibProgrammingFuture();
  auto initialAttrs =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  initialAttrs->setBackupAddr(firstBackupAddr);
  initialAttrs->publish();
  sendAnnouncement(prefixes, eBgpPeer1_, initialAttrs);
  sendInitialPathComputation();
  initialRibFuture.wait();

  ASSERT_TRUE(rib_->fibItems.contains(prefix));
  ASSERT_NE(nullptr, rib_->fibItems.at(prefix).getBestPath());
  EXPECT_EQ(
      firstBackupAddr,
      rib_->fibItems.at(prefix).getBestPath()->attrs->getBackupAddr());

  sendBackupUpdate(folly::IPAddress("2001:db8:ffff::2"));
  sendBackupUpdate(std::optional<folly::IPAddress>{});
}

/*
 * Test rib policy LBW set/update/clear effect on program fib logic
 * Scenario covered:
 * - set lbw rib policy before rib turns from read_only to write mode
 * - set identical lbw rib policy as existing one
 * - set different lbw rib policy as existing one
 * - force rib policy to expire
 * - update lbw rib policy with multiple rib entry update event
 * - clear rib policy
 */
TEST_P(RibFixtureAddPathTestSuite, LbwProgramFibTest) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  /*
   * Statement expiry offsets (seconds from now). Widened from the original
   * 2/4/6s: statement expiry is checked against the system wall clock
   * (RibPolicy::isActive() -> std::time), so each phase below must run its
   * post-wait checks before the NEXT statement's expiry. The large gaps
   * (~10s) between consecutive expiries give each phase a generous margin, so
   * a scheduling stall under load cannot let a later statement expire before
   * this phase observes it -- the source of a pre-existing flake. (The
   * deterministic alternative, injecting a controllable clock into the
   * expiration path, was deliberately deferred to keep this a test-only fix.)
   */
  constexpr int64_t kStmt2ExpirySec = 2; // expires first
  constexpr int64_t kStmt1ExpirySec = 12; // expires second (~10s after stmt2)
  constexpr int64_t kStmt3ExpirySec = 22; // expires third  (~10s after stmt1)

  auto prefix1 = folly::IPAddress::createNetwork("1::/64");
  auto prefixBatch1 = PrefixPathIds{{prefix1, kDefaultPathID}};
  auto prefix2 = folly::IPAddress::createNetwork("2::/64");
  auto prefixBatch2 = PrefixPathIds{{prefix2, kDefaultPathID}};

  /*
   * initial setup:
   * next hop update for prefix 1
   * rib policy set for prefix 1
   * sendInitialPathComputation to turn rib into write mode
   */
  {
    auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
    // send route from localPeer_
    auto attr = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attr->setNexthop(kLocalRouteV4Nexthop);
    attr->publish();
    sendAnnouncement(prefixBatch1, localPeer_, attr);

    // send rib policy over
    sendRouteAttributePolicySet(createTRouteAttributePolicyLbw(
        {prefix1},
        kLbw10G,
        "stmt1",
        std::chrono::seconds(std::time(nullptr)).count() + kStmt1ExpirySec));
    rib_->waitForRouteAttributePolicyUpdate();

    sendInitialPathComputation();
    // this wait() will be fulfilled by push of the policy itself
    ribFuture.wait();

    // stmt1's expiry timer is now running in the background

    const auto& v4Rib = rib_->defaultVrf_.ribEntries().find(prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v4Rib);
    const auto ribPolicyUcmpWeight = v4Rib->second.getRibPolicyUcmpWeight();
    EXPECT_TRUE(rib_->routeAttributePolicy_->match(v4Rib->second));
    EXPECT_TRUE(ribPolicyUcmpWeight.has_value());
    EXPECT_EQ(kLbw10G, ribPolicyUcmpWeight.value());

    /*
     * make sure fib batch list had only 1 update for prefix1
     * which has lbw change
     */
    EXPECT_EQ(1, rib_->fibItems.size());
    EXPECT_NE(rib_->fibItems.end(), rib_->fibItems.find(prefix1));
    EXPECT_EQ(
        kLocalRouteV4Nexthop,
        rib_->fibItems.find(prefix1)
            ->second.getBestPath()
            ->attrs->getNexthop());
    EXPECT_EQ(
        kLbw10G,
        rib_->fibItems.find(prefix1)->second.getRibPolicyUcmpWeight().value());
  }

  // push the same policy along with two irrelevant policies
  {
    auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
    /*
     * send the same statement over
     * additionally send two statements that expire before and after stmt1
     */
    auto policy = createTRouteAttributePolicyLbw(
        {prefix1},
        kLbw10G,
        "stmt1",
        std::chrono::seconds(std::time(nullptr)).count() + kStmt1ExpirySec);
    policy.statements()->emplace(
        "stmt2",
        createTRouteAttributeStatementLbw(
            {prefix2},
            kLbw10G,
            std::chrono::seconds(std::time(nullptr)).count() +
                kStmt2ExpirySec));
    policy.statements()->emplace(
        "stmt3",
        createTRouteAttributeStatementLbw(
            {prefix2},
            kLbw10G,
            std::chrono::seconds(std::time(nullptr)).count() +
                kStmt3ExpirySec));

    sendRouteAttributePolicySet(policy);
    rib_->waitForRouteAttributePolicyUpdate();

    // the next statement's expiry timer is now running in the background

    // this wait() will be fulfilled by the push of the new policy above
    ribFuture.wait();

    const auto& v4Rib = rib_->defaultVrf_.ribEntries().find(prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v4Rib);
    const auto ribPolicyUcmpWeight = v4Rib->second.getRibPolicyUcmpWeight();
    EXPECT_TRUE(rib_->routeAttributePolicy_->match(v4Rib->second));
    EXPECT_TRUE(ribPolicyUcmpWeight.has_value());
    EXPECT_EQ(kLbw10G, ribPolicyUcmpWeight.value());

    // make sure fib batch list is empty
    rib_->evb_.runInEventBaseThreadAndWait(
        [&]() { EXPECT_EQ(0, rib_->fibItems.size()); });
  }

  /*
   * wait and confirm the statements expire one by one
   */
  {
    auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
    EXPECT_TRUE(
        rib_->routeAttributePolicy_->statements_.at("stmt1").isActive());
    EXPECT_TRUE(
        rib_->routeAttributePolicy_->statements_.at("stmt2").isActive());
    EXPECT_TRUE(
        rib_->routeAttributePolicy_->statements_.at("stmt3").isActive());

    // this wait() will be fulfilled by stmt2's expiry (the earliest)
    ribFuture.wait();

    // the next statement's expiry timer is now running in the background

    // Ensure that only stmt2 has expired
    EXPECT_TRUE(
        rib_->routeAttributePolicy_->statements_.at("stmt1").isActive());
    EXPECT_FALSE(
        rib_->routeAttributePolicy_->statements_.at("stmt2").isActive());
    EXPECT_TRUE(
        rib_->routeAttributePolicy_->statements_.at("stmt3").isActive());

    const auto& v4Rib = rib_->defaultVrf_.ribEntries().find(prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v4Rib);
    const auto ribPolicyUcmpWeight = v4Rib->second.getRibPolicyUcmpWeight();
    EXPECT_TRUE(rib_->routeAttributePolicy_->match(v4Rib->second));
    EXPECT_TRUE(ribPolicyUcmpWeight.has_value());
    EXPECT_EQ(kLbw10G, ribPolicyUcmpWeight.value());

    // make sure fib batch list is empty
    rib_->evb_.runInEventBaseThreadAndWait(
        [&]() { EXPECT_EQ(0, rib_->fibItems.size()); });
  }
  {
    auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
    // this wait() will be fulfilled by stmt1's expiry
    ribFuture.wait();

    // the next statement's expiry timer is now running

    // Ensure that both stmt1 and stmt2 have expired
    EXPECT_FALSE(
        rib_->routeAttributePolicy_->statements_.at("stmt1").isActive());
    EXPECT_FALSE(
        rib_->routeAttributePolicy_->statements_.at("stmt2").isActive());
    EXPECT_TRUE(
        rib_->routeAttributePolicy_->statements_.at("stmt3").isActive());

    const auto& v4Rib = rib_->defaultVrf_.ribEntries().find(prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v4Rib);
    const auto ribPolicyUcmpWeight = v4Rib->second.getRibPolicyUcmpWeight();
    EXPECT_TRUE(rib_->routeAttributePolicy_->match(v4Rib->second));
    EXPECT_FALSE(ribPolicyUcmpWeight.has_value());

    /*
     * make sure fib batch list had only 1 update
     * which has empty lbw
     */
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      EXPECT_EQ(1, rib_->fibItems.size());
      EXPECT_NE(rib_->fibItems.end(), rib_->fibItems.find(prefix1));
      EXPECT_EQ(
          kLocalRouteV4Nexthop,
          rib_->fibItems.find(prefix1)
              ->second.getBestPath()
              ->attrs->getNexthop());
      EXPECT_FALSE(rib_->fibItems.find(prefix1)
                       ->second.getRibPolicyUcmpWeight()
                       .has_value());
    });
  }
  {
    auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
    // this wait() will be fulfilled by stmt3's expiry
    ribFuture.wait();

    // Lastly all 3 statements expire
    EXPECT_FALSE(
        rib_->routeAttributePolicy_->statements_.at("stmt1").isActive());
    EXPECT_FALSE(
        rib_->routeAttributePolicy_->statements_.at("stmt2").isActive());
    EXPECT_FALSE(
        rib_->routeAttributePolicy_->statements_.at("stmt3").isActive());
  }

  // push a different rib policy value 5G instead of 10G
  {
    auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();

    // send different rib policy over
    sendRouteAttributePolicySet(
        createTRouteAttributePolicyLbw({prefix1}, kLbw5G));
    rib_->waitForRouteAttributePolicyUpdate();

    ribFuture.wait();

    const auto& v4Rib = rib_->defaultVrf_.ribEntries().find(prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v4Rib);
    const auto ribPolicyUcmpWeight = v4Rib->second.getRibPolicyUcmpWeight();
    EXPECT_TRUE(rib_->routeAttributePolicy_->match(v4Rib->second));
    EXPECT_TRUE(ribPolicyUcmpWeight.has_value());
    EXPECT_EQ(kLbw5G, ribPolicyUcmpWeight.value());

    /*
     * make sure fib batch list had only 1 update
     * which has new lbw 5G
     */
    EXPECT_EQ(1, rib_->fibItems.size());
    EXPECT_NE(rib_->fibItems.end(), rib_->fibItems.find(prefix1));
    EXPECT_EQ(
        kLocalRouteV4Nexthop,
        rib_->fibItems.find(prefix1)
            ->second.getBestPath()
            ->attrs->getNexthop());
    EXPECT_EQ(
        kLbw5G,
        rib_->fibItems.find(prefix1)->second.getRibPolicyUcmpWeight().value());
  }

  // force rib policy to expire and lbw should fall back to default unset state
  {
    auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
    // send 0 ttl rib policy over, which is equivalent to policy expiration
    sendRouteAttributePolicySet(
        createTRouteAttributePolicyLbw({prefix1}, kLbw10G, "stmt1", 0));
    rib_->waitForRouteAttributePolicyUpdate();

    ribFuture.wait();

    // Ensure that the policy has expired
    EXPECT_FALSE(
        rib_->routeAttributePolicy_->statements_.at("stmt1").isActive());

    const auto& v4Rib = rib_->defaultVrf_.ribEntries().find(prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v4Rib);
    const auto ribPolicyUcmpWeight = v4Rib->second.getRibPolicyUcmpWeight();
    EXPECT_TRUE(rib_->routeAttributePolicy_->match(v4Rib->second));
    EXPECT_FALSE(ribPolicyUcmpWeight.has_value());

    /*
     * make sure fib batch list had only 1 update
     * which has empty lbw
     */
    EXPECT_EQ(1, rib_->fibItems.size());
    EXPECT_NE(rib_->fibItems.end(), rib_->fibItems.find(prefix1));
    EXPECT_EQ(
        kLocalRouteV4Nexthop,
        rib_->fibItems.find(prefix1)
            ->second.getBestPath()
            ->attrs->getNexthop());
    EXPECT_FALSE(rib_->fibItems.find(prefix1)
                     ->second.getRibPolicyUcmpWeight()
                     .has_value());
  }

  // multiple rib entries annoucenments with rib policy update on only 1 entry
  {
    auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
    // send route from localPeer_
    auto attr = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attr->setNexthop(kLocalRouteV4Nexthop);
    attr->publish();
    sendAnnouncement(prefixBatch1, localPeer_, attr);
    sendAnnouncement(prefixBatch2, localPeer_, attr);

    /*
     * Ensure that we have finished the route installation to investigate only
     * the results from sendRibPolicySet
     */
    ribFuture.wait();

    ribFuture = rib_->getRibPrepareFibProgrammingFuture();

    // send rib policy over on prefix 1
    sendRouteAttributePolicySet(
        createTRouteAttributePolicyLbw({prefix1}, kLbw10G));
    rib_->waitForRouteAttributePolicyUpdate();

    ribFuture.wait();

    // prefix 1 rib should have rib policy set
    const auto& v4Rib1 = rib_->defaultVrf_.ribEntries().find(prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v4Rib1);
    const auto ribPolicyUcmpWeight = v4Rib1->second.getRibPolicyUcmpWeight();
    EXPECT_TRUE(rib_->routeAttributePolicy_->match(v4Rib1->second));
    EXPECT_TRUE(ribPolicyUcmpWeight.has_value());
    EXPECT_EQ(kLbw10G, ribPolicyUcmpWeight.value());

    // prefix 2 rib should not have rib policy set
    const auto& v4Rib2 = rib_->defaultVrf_.ribEntries().find(prefix2);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v4Rib2);
    EXPECT_FALSE(rib_->routeAttributePolicy_->match(v4Rib2->second));
    EXPECT_FALSE(v4Rib2->second.getRibPolicyUcmpWeight().has_value());

    /*
     * make sure fib batch list had 1 update for prefix1 (prefix2 is not
     * changed) update for prefix1 combined both next-hop(the same) and 10G lbw
     */
    EXPECT_EQ(1, rib_->fibItems.size());
    const auto& fib1 = rib_->fibItems.find(prefix1);
    const auto& fib2 = rib_->fibItems.find(prefix2);
    EXPECT_NE(rib_->fibItems.end(), fib1);
    EXPECT_EQ(rib_->fibItems.end(), fib2);

    EXPECT_EQ(
        kLocalRouteV4Nexthop, fib1->second.getBestPath()->attrs->getNexthop());
    EXPECT_TRUE(fib1->second.getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(kLbw10G, fib1->second.getRibPolicyUcmpWeight().value());
  }

  // calling clear rib policy api should remove rib-policy lbw from any rib
  {
    auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
    // send rib policy over on prefix 1
    rib_->clearRibPolicy();
    rib_->waitForRibPolicyClear();

    ribFuture.wait();

    const auto& v4Rib = rib_->defaultVrf_.ribEntries().find(prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v4Rib);
    const auto ribPolicyUcmpWeight = v4Rib->second.getRibPolicyUcmpWeight();
    EXPECT_FALSE(ribPolicyUcmpWeight.has_value());

    // make sure fib batch list had only 1 update (for prefix 1)
    EXPECT_EQ(1, rib_->fibItems.size());
    const auto& fib1 = rib_->fibItems.find(prefix1);
    EXPECT_NE(rib_->fibItems.end(), fib1);

    EXPECT_EQ(
        kLocalRouteV4Nexthop, fib1->second.getBestPath()->attrs->getNexthop());
    EXPECT_FALSE(fib1->second.getRibPolicyUcmpWeight().has_value());
  }
}

TEST_P(RibFixtureAddPathTestSuite, SetGetClearRouteAttributePolicyTest) {
  /*
   * test setRouteAttributePolicy, getRouteAttributePolicy, and
   * clearRouteAttributePolicy
   */
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  // Create the tRouteAttributePolicy for testing
  TRouteAttributePolicy tRouteAttributePolicy =
      createTRouteAttributePolicyLbw({kV6Prefix1}, 10, "stmt1", 0);

  // send EoR
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  // setRouteAttributePolicy
  {
    // policy with empty action should fail
    TRouteAttributeStatement tStmt;
    tStmt.matcher() = createTRibRouteMatcher({kV4Prefix1});
    TRouteAttributePolicy tEmptyPolicy;
    tEmptyPolicy.statements()->emplace("stmt1", std::move(tStmt));

    const auto expectedError =
        "facebook::bgp::BgpError: Empty route attribute action";

    auto result = sendRouteAttributePolicySet(tEmptyPolicy);
    EXPECT_FALSE(*result.success());
    EXPECT_EQ(expectedError, *result.err());

    // nothing is set
    EXPECT_EQ(TRouteAttributePolicy{}, rib_->getRouteAttributePolicy());
    EXPECT_EQ(nullptr, rib_->routeAttributePolicy_);
  }
  {
    // correct policy would succeed
    auto result = sendRouteAttributePolicySet(tRouteAttributePolicy);
    EXPECT_TRUE(*result.success());

    // wait till rib policy output queue got item
    auto outputPolicy = rib_->waitForRouteAttributePolicyUpdate();

    // new policy is set
    EXPECT_EQ(tRouteAttributePolicy, outputPolicy);
    EXPECT_EQ(tRouteAttributePolicy, rib_->getRouteAttributePolicy());
  }
  // clearRouteAttributePolicy
  {
    rib_->clearRouteAttributePolicy();
    // wait till rib policy output queue got item
    auto outputPolicy = rib_->waitForRouteAttributePolicyClear();

    // nothing is set
    EXPECT_EQ(TRouteAttributePolicy{}, outputPolicy);
    EXPECT_EQ(TRouteAttributePolicy{}, rib_->getRouteAttributePolicy());
  }
  // Test BgpService
  {
    TResult result;

    // policy with empty route matcher should fail
    TRouteAttributePolicy tEmptyPolicy;
    tEmptyPolicy.statements()->emplace("stmt1", TRouteAttributeStatement{});

    // set empty route attribute policy should fail
    result = *folly::coro::blockingWait(service_->co_setRouteAttributePolicy(
        std::make_unique<TRouteAttributePolicy>(tEmptyPolicy)));
    EXPECT_FALSE(*result.success());

    auto tPolicy =
        *folly::coro::blockingWait(service_->co_getRouteAttributePolicy());
    // get nothing
    EXPECT_EQ(tPolicy, TRouteAttributePolicy{});
    EXPECT_EQ(nullptr, rib_->routeAttributePolicy_);

    // successfully set route attribute policy
    auto ribPolicyReplaceFuture = rib_->getRibPolicyReplaceFuture();
    result = *folly::coro::blockingWait(service_->co_setRouteAttributePolicy(
        std::make_unique<TRouteAttributePolicy>(tRouteAttributePolicy)));
    EXPECT_TRUE(*result.success());
    ribPolicyReplaceFuture.wait();

    tPolicy =
        *folly::coro::blockingWait(service_->co_getRouteAttributePolicy());
    EXPECT_EQ(tPolicy, tRouteAttributePolicy);
    EXPECT_NE(nullptr, rib_->routeAttributePolicy_);

    // clear set policy
    ribPolicyReplaceFuture = rib_->getRibPolicyReplaceFuture();
    folly::coro::blockingWait(service_->co_clearRouteAttributePolicy());
    ribPolicyReplaceFuture.wait();
    tPolicy =
        *folly::coro::blockingWait(service_->co_getRouteAttributePolicy());
    EXPECT_EQ(tPolicy, TRouteAttributePolicy{});
    EXPECT_EQ(nullptr, rib_->routeAttributePolicy_);
  }
}

/**
 * Test that when TRouteAttributePolicy is cleared, the fib falls back to ECMP.
 */
TEST_P(
    RibFixtureAddPathTestSuite,
    FallbackToECMPOnClearRouteAttributePolicyTest) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  // 1. Create route attribute policy to override next-hop weights.
  TRouteAttributeUcmpAction tRouteAttributeUcmpAction;
  std::shared_ptr<facebook::bgp::BgpPath> attrs1, attrs2;
  // Weights that will be assigned to next-hops.
  int32_t nhWt1 = 10e2;
  int32_t nhWt2 = 20e2;

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

  // Create the tRouteAttributePolicy for testing
  TRouteAttributePolicy tRouteAttributePolicy = createTRouteAttributePolicyUcmp(
      {kV6Prefix1}, tRouteAttributeUcmpAction, "stmt1");
  // correct policy would succeed
  auto result = sendRouteAttributePolicySet(tRouteAttributePolicy);
  EXPECT_TRUE(*result.success());
  // wait till rib policy output queue got item
  auto outputPolicy = rib_->waitForRouteAttributePolicyUpdate();
  // New policy is set.
  EXPECT_EQ(tRouteAttributePolicy, outputPolicy);
  auto ribRouteAttributePolicy = rib_->getRouteAttributePolicy();
  EXPECT_EQ(tRouteAttributePolicy, ribRouteAttributePolicy);
  EXPECT_NE(nullptr, rib_->routeAttributePolicy_);

  /*
   * 2. Create a prefix with 2 routes.
   * Create attributes for two paths.
   */
  nettools::bgplib::BgpAttrCommunitiesC communities1;
  communities1.emplace_back(200, 100);
  attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs1->setCommunities(communities1);
  attrs1->publish();

  nettools::bgplib::BgpAttrCommunitiesC communities2;
  communities2.emplace_back(200, 200);
  attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs2->setNexthop(kV4Nexthop2);
  attrs2->setCommunities(communities2);
  attrs2->publish();
  auto prefixBatch1 = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch1, eBgpPeer1_, attrs1);
  sendAnnouncement(prefixBatch1, eBgpPeer2_, attrs2);

  // Send EoR.
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  // 3. Check weights are updated per policy.
  {
    const auto& v6Rib = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v6Rib);
    EXPECT_TRUE(rib_->routeAttributePolicy_->match(v6Rib->second));

    /*
     * Make sure fib batch list has only 1 update for kV6Prefix1,
     * and that next-hops have UCMP weights from the policy.
     */
    EXPECT_EQ(1, rib_->fibItems.size());
    EXPECT_NE(rib_->fibItems.end(), rib_->fibItems.find(kV6Prefix1));
    auto& ribEntry = rib_->fibItems.at(kV6Prefix1);
    EXPECT_EQ(kV4Nexthop1, ribEntry.getBestPath()->attrs->getNexthop());

    EXPECT_EQ(ribEntry.getMultipathWeightedNexthops()->size(), 2);
    for (auto& nhwt : *ribEntry.getMultipathWeightedNexthops()) {
      if (nhwt.first == kV4Nexthop1) {
        EXPECT_EQ(nhwt.second, nhWt1);
      } else if (nhwt.first == kV4Nexthop2) {
        EXPECT_EQ(nhwt.second, nhWt2);
      }
    }
  }
  // 4. clearRouteAttributePolicy and check route falls back to ECMP.
  {
    fibFuture = fib_->getFibProgramFuture();
    rib_->clearRouteAttributePolicy();
    // Wait till rib policy output queue got item.
    rib_->waitForRouteAttributePolicyClear();

    // No policy should exist.
    EXPECT_EQ(TRouteAttributePolicy{}, rib_->getRouteAttributePolicy());
    fibFuture.wait();

    const auto& v6Rib = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v6Rib);

    /*
     * Make sure fib batch list has only 1 update for kV6Prefix1,
     * and that next-hops have no weights.
     */
    EXPECT_EQ(1, rib_->fibItems.size());
    EXPECT_NE(rib_->fibItems.end(), rib_->fibItems.find(kV6Prefix1));
    auto& ribEntry = rib_->fibItems.at(kV6Prefix1);
    EXPECT_EQ(kV4Nexthop1, ribEntry.getBestPath()->attrs->getNexthop());

    EXPECT_EQ(ribEntry.getMultipathWeightedNexthops()->size(), 2);
    for (auto& nhwt : *ribEntry.getMultipathWeightedNexthops()) {
      if (nhwt.first == kV4Nexthop1) {
        EXPECT_EQ(nhwt.second, 0);
      } else if (nhwt.first == kV4Nexthop2) {
        EXPECT_EQ(nhwt.second, 0);
      }
    }
  }
}

/**
 * Test that when an empty route attribute policy is set, the fib falls back to
 * ECMP.
 */
TEST_P(
    RibFixtureAddPathTestSuite,
    FallbackToECMPOnEmptyRouteAttributePolicyTest) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  // 1. Create route attribute policy to override next-hop weights.
  TRouteAttributeUcmpAction tRouteAttributeUcmpAction;
  std::shared_ptr<facebook::bgp::BgpPath> attrs1, attrs2;
  // Weights that will be assigned to next-hops.
  int32_t nhWt1 = 10e2;
  int32_t nhWt2 = 20e2;

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

  // Create the tRouteAttributePolicy for testing
  TRouteAttributePolicy tRouteAttributePolicy = createTRouteAttributePolicyUcmp(
      {kV6Prefix1}, tRouteAttributeUcmpAction, "stmt1");
  // correct policy would succeed
  auto result = sendRouteAttributePolicySet(tRouteAttributePolicy);
  EXPECT_TRUE(*result.success());
  // wait till rib policy output queue got item
  auto outputPolicy = rib_->waitForRouteAttributePolicyUpdate();
  // New policy is set.
  EXPECT_EQ(tRouteAttributePolicy, outputPolicy);
  auto ribRouteAttributePolicy = rib_->getRouteAttributePolicy();
  EXPECT_EQ(tRouteAttributePolicy, ribRouteAttributePolicy);
  EXPECT_NE(nullptr, rib_->routeAttributePolicy_);

  /*
   * 2. Create a prefix with 2 routes.
   * Create attributes for two paths.
   */
  nettools::bgplib::BgpAttrCommunitiesC communities1;
  communities1.emplace_back(200, 100);
  attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs1->setCommunities(communities1);
  attrs1->publish();

  nettools::bgplib::BgpAttrCommunitiesC communities2;
  communities2.emplace_back(200, 200);
  attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs2->setNexthop(kV4Nexthop2);
  attrs2->setCommunities(communities2);
  attrs2->publish();
  auto prefixBatch1 = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch1, eBgpPeer1_, attrs1);
  sendAnnouncement(prefixBatch1, eBgpPeer2_, attrs2);

  // Send EoR.
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  // 3. Check weights are updated per policy.
  {
    const auto& v6Rib = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v6Rib);
    EXPECT_TRUE(rib_->routeAttributePolicy_->match(v6Rib->second));

    /*
     * Make sure fib batch list has only 1 update for kV6Prefix1,
     * and that next-hops have UCMP weights from the policy.
     */
    EXPECT_EQ(1, rib_->fibItems.size());
    EXPECT_NE(rib_->fibItems.end(), rib_->fibItems.find(kV6Prefix1));
    auto& ribEntry = rib_->fibItems.at(kV6Prefix1);
    EXPECT_EQ(kV4Nexthop1, ribEntry.getBestPath()->attrs->getNexthop());

    EXPECT_EQ(ribEntry.getMultipathWeightedNexthops()->size(), 2);
    for (auto& nhwt : *ribEntry.getMultipathWeightedNexthops()) {
      if (nhwt.first == kV4Nexthop1) {
        EXPECT_EQ(nhwt.second, nhWt1);
      } else if (nhwt.first == kV4Nexthop2) {
        EXPECT_EQ(nhwt.second, nhWt2);
      }
    }
  }
  // 4. Send empty route attribute policy and check route falls back to ECMP.
  {
    fibFuture = fib_->getFibProgramFuture();
    // Create the tRouteAttributePolicy for testing
    TRouteAttributePolicy tRouteAttributePolicyEmpty;
    // Correct policy would succeed.
    result = sendRouteAttributePolicySet(tRouteAttributePolicyEmpty);
    EXPECT_TRUE(*result.success());
    // wait till rib policy output queue got item
    outputPolicy = rib_->waitForRouteAttributePolicyUpdate();
    // New policy is set.
    EXPECT_EQ(tRouteAttributePolicyEmpty, outputPolicy);
    ribRouteAttributePolicy = rib_->getRouteAttributePolicy();

    EXPECT_EQ(tRouteAttributePolicyEmpty, ribRouteAttributePolicy);
    EXPECT_NE(nullptr, rib_->routeAttributePolicy_);

    fibFuture.wait();

    const auto& v6Rib = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v6Rib);

    /*
     * Make sure fib batch list has only 1 update for kV6Prefix1,
     * and that next-hops have no weights.
     */
    EXPECT_EQ(1, rib_->fibItems.size());
    EXPECT_NE(rib_->fibItems.end(), rib_->fibItems.find(kV6Prefix1));
    auto& ribEntry = rib_->fibItems.at(kV6Prefix1);
    EXPECT_EQ(kV4Nexthop1, ribEntry.getBestPath()->attrs->getNexthop());

    EXPECT_EQ(ribEntry.getMultipathWeightedNexthops()->size(), 2);
    for (auto& nhwt : *ribEntry.getMultipathWeightedNexthops()) {
      if (nhwt.first == kV4Nexthop1) {
        EXPECT_EQ(nhwt.second, 0);
      } else if (nhwt.first == kV4Nexthop2) {
        EXPECT_EQ(nhwt.second, 0);
      }
    }
  }
}

/**
 * Test that when TRouteAttributePolicy is cleared, the fib falls back to BGP
 * UCMP.
 *
 * @details
 * When rib-policy is removed BGP will fall back to its native multipath
 * behavior, which in this case will be UCMP. That is so because the two
 * contributing BGP multipath routes are received with link-bandwidth extended
 * communities. When sending the multipath route to the FIB, BGP will create
 * weights for the next-hops in the ration of those link-bandwidth communities.
 */
TEST_P(
    RibFixtureAddPathTestSuite,
    FallbackToUCMPOnClearRouteAttributePolicyTest) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  // 1. Create route attribute policy to override next-hop weights.
  TRouteAttributeUcmpAction tRouteAttributeUcmpAction;
  std::shared_ptr<facebook::bgp::BgpPath> attrs1, attrs2;
  // Weights that will be assigned to next-hops.
  int32_t nhWt1 = 10e2;
  int32_t nhWt2 = 20e2;

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

  // Create the tRouteAttributePolicy for testing
  TRouteAttributePolicy tRouteAttributePolicy = createTRouteAttributePolicyUcmp(
      {kV6Prefix1}, tRouteAttributeUcmpAction, "stmt1");
  // correct policy would succeed
  auto result = sendRouteAttributePolicySet(tRouteAttributePolicy);
  EXPECT_TRUE(*result.success());
  // wait till rib policy output queue got item
  auto outputPolicy = rib_->waitForRouteAttributePolicyUpdate();
  // New policy is set.
  EXPECT_EQ(tRouteAttributePolicy, outputPolicy);
  auto ribRouteAttributePolicy = rib_->getRouteAttributePolicy();
  EXPECT_EQ(tRouteAttributePolicy, ribRouteAttributePolicy);
  EXPECT_NE(nullptr, rib_->routeAttributePolicy_);

  /*
   * 2. Create a prefix with 2 routes.
   * Create attributes for two paths.
   */
  nettools::bgplib::BgpAttrCommunitiesC communities1;
  communities1.emplace_back(200, 100);
  attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs1->setCommunities(communities1);
  nettools::bgplib::BgpAttrExtCommunitiesC extCommunities1;
  extCommunities1.emplace_back(
      nettools::bgplib::BgpExtCommunityLinkBandWidthTypeC(
          uint16_t(kLocalAs1), kLbw100G));
  attrs1->setExtCommunities(extCommunities1);
  attrs1->publish();

  nettools::bgplib::BgpAttrCommunitiesC communities2;
  communities2.emplace_back(200, 200);
  attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs2->setNexthop(kV4Nexthop2);
  attrs2->setCommunities(communities2);
  nettools::bgplib::BgpAttrExtCommunitiesC extCommunities2;
  extCommunities2.emplace_back(
      nettools::bgplib::BgpExtCommunityLinkBandWidthTypeC(
          uint16_t(kLocalAs1), kLbw150G));
  attrs2->setExtCommunities(extCommunities2);
  attrs2->publish();
  auto prefixBatch1 = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch1, eBgpPeer1_, attrs1);
  sendAnnouncement(prefixBatch1, eBgpPeer2_, attrs2);

  // Send EoR.
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  // 3. Check weights are updated per policy.
  {
    const auto& v6Rib = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v6Rib);
    EXPECT_TRUE(rib_->routeAttributePolicy_->match(v6Rib->second));

    /*
     * Make sure fib batch list has only 1 update for kV6Prefix1,
     * and that next-hops have UCMP weights from the policy.
     */
    EXPECT_EQ(1, rib_->fibItems.size());
    EXPECT_NE(rib_->fibItems.end(), rib_->fibItems.find(kV6Prefix1));
    auto& ribEntry = rib_->fibItems.at(kV6Prefix1);
    EXPECT_EQ(kV4Nexthop1, ribEntry.getBestPath()->attrs->getNexthop());

    EXPECT_EQ(ribEntry.getMultipathWeightedNexthops()->size(), 2);
    for (auto& nhwt : *ribEntry.getMultipathWeightedNexthops()) {
      if (nhwt.first == kV4Nexthop1) {
        EXPECT_EQ(nhwt.second, nhWt1);
      } else if (nhwt.first == kV4Nexthop2) {
        EXPECT_EQ(nhwt.second, nhWt2);
      }
    }
  }
  // 4. clearRouteAttributePolicy and check route falls back to UCMP.
  {
    fibFuture = fib_->getFibProgramFuture();
    rib_->clearRouteAttributePolicy();
    rib_->waitForRouteAttributePolicyClear();

    // No policy should exist.
    EXPECT_EQ(TRouteAttributePolicy{}, rib_->getRouteAttributePolicy());
    fibFuture.wait();

    const auto& v6Rib = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v6Rib);

    /*
     * Make sure fib batch list has only 1 update for kV6Prefix1,
     * and that next-hops have no weights.
     */
    EXPECT_EQ(1, rib_->fibItems.size());
    EXPECT_NE(rib_->fibItems.end(), rib_->fibItems.find(kV6Prefix1));
    auto& ribEntry = rib_->fibItems.at(kV6Prefix1);
    EXPECT_EQ(kV4Nexthop1, ribEntry.getBestPath()->attrs->getNexthop());

    EXPECT_EQ(ribEntry.getMultipathWeightedNexthops()->size(), 2);
    for (auto& nhwt : *ribEntry.getMultipathWeightedNexthops()) {
      if (nhwt.first == kV4Nexthop1) {
        EXPECT_EQ(nhwt.second, 2);
      } else if (nhwt.first == kV4Nexthop2) {
        EXPECT_EQ(nhwt.second, 3);
      }
    }
  }
}

/*
 * verify that when routes get updated, route attribute policy is re-evaluated
 * only on the changing routes
 */
TEST_P(RibFixtureAddPathTestSuite, ReApplyRouteAttributePolicyTest) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  // 1. Create route attribute policy to override next-hop weights.
  TRouteAttributeUcmpAction tRouteAttributeUcmpAction;
  std::shared_ptr<facebook::bgp::BgpPath> attrs1, attrs2, attrs3;
  // Weights that will be assigned to next-hops.
  int32_t nhWt1 = 10e2;
  int32_t nhWt2 = 20e2;

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
  tRouteAttributeUcmpAction.apply_all_actions_or_fallback_to_ecmp() = true;

  // Create the tRouteAttributePolicy for testing
  TRouteAttributePolicy tRouteAttributePolicy = createTRouteAttributePolicyUcmp(
      {kV6Prefix1}, tRouteAttributeUcmpAction, "stmt1");
  // correct policy would succeed
  auto result = sendRouteAttributePolicySet(tRouteAttributePolicy);
  EXPECT_TRUE(*result.success());
  // wait till rib policy output queue got item
  auto outputPolicy = rib_->waitForRouteAttributePolicyUpdate();
  // New policy is set.
  EXPECT_EQ(tRouteAttributePolicy, outputPolicy);
  auto ribRouteAttributePolicy = rib_->getRouteAttributePolicy();
  EXPECT_EQ(tRouteAttributePolicy, ribRouteAttributePolicy);
  EXPECT_NE(nullptr, rib_->routeAttributePolicy_);

  /*
   * 2. Create a prefix with 2 routes.
   * Create attributes for two paths.
   */
  auto prefixBatch1 = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  {
    nettools::bgplib::BgpAttrCommunitiesC communities1;
    communities1.emplace_back(200, 100);
    attrs1 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs1->setCommunities(communities1);
    nettools::bgplib::BgpAttrExtCommunitiesC extCommunities1;
    extCommunities1.emplace_back(
        nettools::bgplib::BgpExtCommunityLinkBandWidthTypeC(
            uint16_t(kLocalAs1), kLbw100G));
    attrs1->setExtCommunities(extCommunities1);
    attrs1->publish();

    nettools::bgplib::BgpAttrCommunitiesC communities2;
    communities2.emplace_back(200, 200);
    attrs2 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs2->setNexthop(kV4Nexthop2);
    attrs2->setCommunities(communities2);
    nettools::bgplib::BgpAttrExtCommunitiesC extCommunities2;
    extCommunities2.emplace_back(
        nettools::bgplib::BgpExtCommunityLinkBandWidthTypeC(
            uint16_t(kLocalAs1), kLbw150G));
    attrs2->setExtCommunities(extCommunities2);
    attrs2->publish();
    sendAnnouncement(prefixBatch1, eBgpPeer1_, attrs1);
    sendAnnouncement(prefixBatch1, eBgpPeer2_, attrs2);

    // Send EoR.
    auto fibFuture = fib_->getFibProgramFuture();
    sendInitialPathComputation();
    fibFuture.wait();
  }

  // 3. Check weights are updated per policy.
  {
    const auto& v6Rib = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v6Rib);
    EXPECT_TRUE(rib_->routeAttributePolicy_->match(v6Rib->second));

    /*
     * Make sure fib batch list has only 1 update for kV6Prefix1,
     * and that next-hops have UCMP weights from the policy.
     */
    EXPECT_EQ(1, rib_->fibItems.size());
    EXPECT_NE(rib_->fibItems.end(), rib_->fibItems.find(kV6Prefix1));
    auto& ribEntry = rib_->fibItems.at(kV6Prefix1);
    EXPECT_EQ(kV4Nexthop1, ribEntry.getBestPath()->attrs->getNexthop());

    EXPECT_EQ(ribEntry.getMultipathWeightedNexthops()->size(), 2);
    for (auto& nhwt : *ribEntry.getMultipathWeightedNexthops()) {
      if (nhwt.first == kV4Nexthop1) {
        EXPECT_EQ(nhwt.second, nhWt1);
      } else if (nhwt.first == kV4Nexthop2) {
        EXPECT_EQ(nhwt.second, nhWt2);
      }
    }
  }

  // 4. Create another prefix that does not match the route attribute policy
  {
    nettools::bgplib::BgpAttrCommunitiesC communities3;
    communities3.emplace_back(300, 300);
    attrs3 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs3->setCommunities(communities3);
    nettools::bgplib::BgpAttrExtCommunitiesC extCommunities3;
    extCommunities3.emplace_back(
        nettools::bgplib::BgpExtCommunityLinkBandWidthTypeC(
            uint16_t(kLocalAs3), kLbw100G));
    attrs3->setExtCommunities(extCommunities3);
    attrs3->publish();

    auto prefixBatch2 = PrefixPathIds{{kV6Prefix2, kDefaultPathID}};
    sendAnnouncement(prefixBatch2, eBgpPeer2_, attrs3);

    // Send EoR.
    auto fibFuture = fib_->getFibProgramFuture();
    sendInitialPathComputation();
    fibFuture.wait();
  }

  // 5. verify only one prefix in the fib batch list
  {
    const auto& v6Rib = rib_->defaultVrf_.ribEntries().find(kV6Prefix2);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v6Rib);

    /*
     * Make sure fib batch list has only 1 update for kV6Prefix2,
     * which has native UCMP weight
     */
    EXPECT_EQ(1, rib_->fibItems.size());
    EXPECT_NE(rib_->fibItems.end(), rib_->fibItems.find(kV6Prefix2));
    auto& ribEntry = rib_->fibItems.at(kV6Prefix2);
    EXPECT_EQ(kV4Nexthop1, ribEntry.getBestPath()->attrs->getNexthop());

    EXPECT_EQ(ribEntry.getMultipathWeightedNexthops()->size(), 1);
    for (auto& nhwt : *ribEntry.getMultipathWeightedNexthops()) {
      if (nhwt.first == kV4Nexthop1) {
        EXPECT_EQ(nhwt.second, 1);
      }
    }
  }

  // 6. Withdraw one route from the first prefix (matching RA policy)
  {
    sendWithdrawal(prefixBatch1, eBgpPeer1_);
    // Send EoR.
    auto fibFuture = fib_->getFibProgramFuture();
    sendInitialPathComputation();
    fibFuture.wait();
  }

  // 7. verify one prefix in the fib batch list and it falls back to native UCMP
  {
    const auto& v6Rib = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v6Rib);

    /*
     * Make sure fib batch list has only 1 update for kV6Prefix1,
     * and that next-hops have no weights.
     */
    EXPECT_EQ(1, rib_->fibItems.size());
    EXPECT_NE(rib_->fibItems.end(), rib_->fibItems.find(kV6Prefix1));
    auto& ribEntry = rib_->fibItems.at(kV6Prefix1);
    EXPECT_EQ(kV4Nexthop2, ribEntry.getBestPath()->attrs->getNexthop());

    EXPECT_EQ(ribEntry.getMultipathWeightedNexthops()->size(), 1);
    for (auto& nhwt : *ribEntry.getMultipathWeightedNexthops()) {
      if (nhwt.first == kV4Nexthop2) {
        EXPECT_EQ(nhwt.second, 1);
      }
    }
  }
}

TEST_P(RibFsdbAddPathTestSuite, ReplaceRouteAttributePolicyTest) {
  auto subscribedPolicy = fsdbSubscriber_->subscribe(
      fsdbSubscriber_->getRootStatePath().bgp().routeAttributePolicy());

  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  // Create the tRouteAttributePolicy and tRibPolicy for testing
  auto now = std::chrono::seconds(std::time(nullptr));
  TRouteAttributePolicy tRouteAttributePolicy =
      createTRouteAttributePolicyLbw({kV6Prefix1}, 10, "stmt1", now.count());
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
      EXPECT_EQ(rib_->routeAttributePolicy_, nullptr);
    });

    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      // empty rib policy
      EXPECT_THROW(
          rib_->replaceRibPolicy(std::make_unique<RibPolicy>(tRibPolicy)),
          BgpError);
      EXPECT_EQ(rib_->routeAttributePolicy_, nullptr);
    });

    tRibPolicy.route_attribute_policy() = tRouteAttributePolicy;
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      // with route attribute policy
      rib_->replaceRibPolicy(std::make_unique<RibPolicy>(tRibPolicy));
      EXPECT_NE(rib_->routeAttributePolicy_, nullptr);
    });

    WITH_RETRIES_N(5, {
      auto policyLk = subscribedPolicy.rlock();
      ASSERT_EVENTUALLY_TRUE(policyLk->has_value());
      EXPECT_EVENTUALLY_EQ(*policyLk, tRouteAttributePolicy);
    });

    // clean up the route attribute policy
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      rib_->replaceRibPolicy(nullptr);
      EXPECT_EQ(rib_->routeAttributePolicy_, nullptr);
    });

    WITH_RETRIES_N(
        5, ASSERT_EVENTUALLY_FALSE(subscribedPolicy.rlock()->has_value()));
  }
  {
    /*
     * Add route attribute policy
     * It should succeed as we have no route attribute policy before
     */
    auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
    /*
     * We need to run replaceRouteAttributePolicy in runInEventBaseThreadAndWait
     * as we could potentially call schedulePrepareFibProgrammingTimer, which
     * should be run in the Rib event base thread
     */
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      tRouteAttributePolicy = createTRouteAttributePolicyLbw(
          {kV6Prefix1}, 10, "stmt1", now.count() - 100);
      rib_->replaceRouteAttributePolicy(
          std::make_unique<RouteAttributePolicy>(tRouteAttributePolicy));
    });
    ribFuture.wait();
    EXPECT_NE(rib_->routeAttributePolicy_, nullptr);

    // The existing one expires, and hence pushing a new one should replace it
    ribFuture = rib_->getRibPrepareFibProgrammingFuture();
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      tRouteAttributePolicy = createTRouteAttributePolicyLbw(
          {kV6Prefix1}, 10, "stmt1", now.count());
      auto hasUpdate = rib_->replaceRouteAttributePolicy(
          std::make_unique<RouteAttributePolicy>(tRouteAttributePolicy));

      EXPECT_TRUE(hasUpdate);
    });
    ribFuture.wait();
    EXPECT_NE(rib_->routeAttributePolicy_, nullptr);

    // change the route attribute policy
    tRouteAttributePolicy.statements()->emplace(
        "stmt2", createTRouteAttributeStatementLbw({kV6Prefix2}, 10));
    ribFuture = rib_->getRibPrepareFibProgrammingFuture();
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      rib_->replaceRouteAttributePolicy(
          std::make_unique<RouteAttributePolicy>(tRouteAttributePolicy));
    });
    /*
     * the policy is changed, and hence we should trigger fib programming
     * preparation
     */
    ribFuture.wait();
    EXPECT_NE(rib_->routeAttributePolicy_, nullptr);

    // Clear route attribute policy
    ribFuture = rib_->getRibPrepareFibProgrammingFuture();
    rib_->evb_.runInEventBaseThreadAndWait(
        [&]() { rib_->replaceRouteAttributePolicy(nullptr); });
    ribFuture.wait();
    EXPECT_EQ(rib_->routeAttributePolicy_, nullptr);
  }
}

/**
 * Test that createTRibEntry populates active_cte_ucmp_action when a CTE
 * (route attribute) UCMP policy matches a route.
 */
TEST_P(RibFixtureAddPathTestSuite, CreateTRibEntryWithCteUcmpAction) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  // 1. Create route attribute policy with UCMP weight actions.
  TRouteAttributeUcmpAction tRouteAttributeUcmpAction;
  int32_t nhWt1 = 100;
  int32_t nhWt2 = 200;

  TNextHopWeightAction tNexthopWeightAction1;
  TBgpPathMatcher tMatcher1;
  auto tCommMatch1 = createTBgpCommunityMatch(200, 100);
  tMatcher1.community_list() = createTCommunityListMatch(
      {tCommMatch1}, routing_policy::BooleanOperator::AND);
  tNexthopWeightAction1.path_matchers()->push_back(tMatcher1);
  tNexthopWeightAction1.weight() = nhWt1;

  TNextHopWeightAction tNexthopWeightAction2;
  TBgpPathMatcher tMatcher2;
  auto tCommMatch2 = createTBgpCommunityMatch(200, 200);
  tMatcher2.community_list() = createTCommunityListMatch(
      {tCommMatch2}, routing_policy::BooleanOperator::AND);
  tNexthopWeightAction2.path_matchers()->push_back(tMatcher2);
  tNexthopWeightAction2.weight() = nhWt2;

  tRouteAttributeUcmpAction.nexthop_weight_actions()->push_back(
      tNexthopWeightAction1);
  tRouteAttributeUcmpAction.nexthop_weight_actions()->push_back(
      tNexthopWeightAction2);

  TRouteAttributePolicy tRouteAttributePolicy = createTRouteAttributePolicyUcmp(
      {kV6Prefix1}, tRouteAttributeUcmpAction, "stmt1");
  auto result = sendRouteAttributePolicySet(tRouteAttributePolicy);
  EXPECT_TRUE(*result.success());
  rib_->waitForRouteAttributePolicyUpdate();

  // 2. Create routes that match the policy.
  nettools::bgplib::BgpAttrCommunitiesC communities1;
  communities1.emplace_back(200, 100);
  auto attrs1 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs1->setCommunities(communities1);
  attrs1->publish();

  nettools::bgplib::BgpAttrCommunitiesC communities2;
  communities2.emplace_back(200, 200);
  auto attrs2 =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrs2->setNexthop(kV4Nexthop2);
  attrs2->setCommunities(communities2);
  attrs2->publish();

  auto prefixBatch1 = PrefixPathIds{{kV6Prefix1, kDefaultPathID}};
  sendAnnouncement(prefixBatch1, eBgpPeer1_, attrs1);
  sendAnnouncement(prefixBatch1, eBgpPeer2_, attrs2);

  // Send EoR to trigger FIB programming (which populates the match cache).
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  /*
   * 3. Verify createTRibEntry populates active_cte_ucmp_action. ribEntries_ and
   * createTRibEntry read RIB-evb-confined state (routeAttributePolicy_ and the
   * RibEntry, including its ribVersion / advertised paths), so the read must
   * run on the RIB evb: reading from the test thread races the RIB thread still
   * finishing handleFibProgrammedMessage after fibFuture.wait() (a TSAN data
   * race). This mirrors how production reads entries -- via an evb hop in
   * RibBase::getRibEntries* / BgpServiceBase.
   */
  rib_->evb_.runInEventBaseThreadAndWait([&]() {
    const auto& v6Rib = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
    EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v6Rib);

    auto tRibEntry = rib_->createTRibEntry(*v6Rib);
    EXPECT_TRUE(tRibEntry.active_cte_ucmp_action().has_value());

    const auto& ucmpAction = *tRibEntry.active_cte_ucmp_action();
    EXPECT_EQ(ucmpAction.nexthop_weight_actions()->size(), 2);
    EXPECT_EQ(*ucmpAction.nexthop_weight_actions()[0].weight(), nhWt1);
    EXPECT_EQ(*ucmpAction.nexthop_weight_actions()[1].weight(), nhWt2);
  });

  // 4. Verify that a non-matching prefix does NOT have active_cte_ucmp_action.
  {
    auto attrs3 = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attrs3->publish();
    auto prefixBatch2 = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
    sendAnnouncement(prefixBatch2, eBgpPeer1_, attrs3);

    fibFuture = fib_->getFibProgramFuture();
    rib_->evb_.runInEventBaseThreadAndWait(
        [&]() { rib_->schedulePrepareFibProgrammingTimer(); });
    fibFuture.wait();

    // Read on the RIB evb to serialize with the RIB thread (see step 3 above).
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      const auto& v4Rib = rib_->defaultVrf_.ribEntries().find(kV4Prefix1);
      EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v4Rib);

      auto tRibEntry = rib_->createTRibEntry(*v4Rib);
      EXPECT_FALSE(tRibEntry.active_cte_ucmp_action().has_value());
    });
  }

  // 5. Clear policy and verify active_cte_ucmp_action is no longer set.
  {
    fibFuture = fib_->getFibProgramFuture();
    rib_->clearRouteAttributePolicy();
    rib_->waitForRouteAttributePolicyClear();
    fibFuture.wait();

    // Read on the RIB evb to serialize with the RIB thread (see step 3 above).
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      const auto& v6Rib = rib_->defaultVrf_.ribEntries().find(kV6Prefix1);
      EXPECT_NE(rib_->defaultVrf_.ribEntries().end(), v6Rib);

      auto tRibEntry = rib_->createTRibEntry(*v6Rib);
      EXPECT_FALSE(tRibEntry.active_cte_ucmp_action().has_value());
    });
  }
}

/**
 * Test: CacheMigrationDetectsNewStatementsWithEqualCount
 *
 * Verifies that migrateRouteAttributePolicyCache correctly detects new
 * statements when one statement is removed and another is added (equal total
 * count). This exercises the size-based optimization:
 *   newStmts.size() + removedStatements.size() > oldStmts.size()
 *
 * With oldStmts={A,B}, newStmts={B,C}: removedStatements={A}, so
 * 2 + 1 > 2 => true (new statement C detected).
 */
TEST_P(
    RibFixtureAddPathTestSuite,
    CacheMigrationDetectsNewStatementsWithEqualCount) {
  int64_t weight = 100;

  // Old policy: stmt1 (prefix1) + stmt2 (prefix2)
  auto tOldPolicy =
      createTRouteAttributePolicyLbw({kV4Prefix1}, weight, "stmt1");
  tOldPolicy.statements()->emplace(
      "stmt2", createTRouteAttributeStatementLbw({kV4Prefix2}, weight));
  RouteAttributePolicy oldPolicy{tOldPolicy};

  // Populate cache: prefix1 matches stmt1, prefix3 has negative cache
  RibEntry entry1(kV4Prefix1);
  RibEntry entry3(kV4Prefix3);
  RouteAttributePolicy::RibChange ribChange;
  oldPolicy.overwriteRouteAttributes(entry1, ribChange);
  oldPolicy.overwriteRouteAttributes(entry3, ribChange);

  EXPECT_TRUE(oldPolicy.getCache().at(kV4Prefix1).has_value());
  EXPECT_FALSE(oldPolicy.getCache().at(kV4Prefix3).has_value());

  // New policy: stmt2 (prefix2) + stmt3 (prefix3) — stmt1 removed, stmt3 added
  auto tNewPolicy =
      createTRouteAttributePolicyLbw({kV4Prefix2}, weight, "stmt2");
  tNewPolicy.statements()->emplace(
      "stmt3", createTRouteAttributeStatementLbw({kV4Prefix3}, weight));
  RouteAttributePolicy newPolicy{tNewPolicy};

  // Same statement count (2 == 2), but stmt1 removed and stmt3 added
  auto result = rib_->migrateRouteAttributePolicyCache(oldPolicy, newPolicy);

  EXPECT_TRUE(result.hasUpdate);
  EXPECT_TRUE(result.needsReEvaluation);
  EXPECT_FALSE(result.affectedPrefixes.empty());
}

/**
 * Test: CacheMigrationIdenticalPolicyPreservesOldCache
 *
 * Verifies that when migrateRouteAttributePolicyCache is called with two
 * identical policies, the old policy's cache remains intact and usable.
 *
 * This is a regression test for a bug where moveCache() in the !hasUpdate
 * branch would move the cache unique_ptr from the old (live) policy to the
 * new (local) policy. Since hasUpdate=false, replaceRouteAttributePolicy()
 * would not replace routeAttributePolicy_, leaving the live policy with a
 * null matchCache_. The next overwriteRouteAttributes() call would crash.
 */
TEST_F(
    RibFixtureAddPathTestSuite,
    CacheMigrationIdenticalPolicyPreservesOldCache) {
  int64_t weight = 100;
  auto now = std::chrono::seconds(std::time(nullptr));
  auto timestamp = now.count() + 3600;

  // Create old policy and warm its cache
  auto tOldPolicy =
      createTRouteAttributePolicyLbw({kV4Prefix1}, weight, "stmt1", timestamp);
  RouteAttributePolicy oldPolicy{tOldPolicy};

  RibEntry entry1(kV4Prefix1);
  RibEntry entry2(kV4Prefix2); // non-matching prefix for negative cache
  RouteAttributePolicy::RibChange warmupChange;
  oldPolicy.overwriteRouteAttributes(entry1, warmupChange);
  oldPolicy.overwriteRouteAttributes(entry2, warmupChange);

  // Verify old policy cache is populated
  EXPECT_EQ(oldPolicy.getCache().size(), 2);
  EXPECT_TRUE(oldPolicy.getCache().at(kV4Prefix1).has_value());
  EXPECT_FALSE(oldPolicy.getCache().at(kV4Prefix2).has_value());

  // Create identical new policy
  auto tNewPolicy =
      createTRouteAttributePolicyLbw({kV4Prefix1}, weight, "stmt1", timestamp);
  RouteAttributePolicy newPolicy{tNewPolicy};

  // Migrate — should detect no update
  auto result = rib_->migrateRouteAttributePolicyCache(oldPolicy, newPolicy);
  EXPECT_FALSE(result.hasUpdate);
  EXPECT_FALSE(result.needsReEvaluation);
  EXPECT_TRUE(result.affectedPrefixes.empty());

  // Old policy's cache must still be valid (not moved-from / null)
  EXPECT_EQ(oldPolicy.getCache().size(), 2);
  EXPECT_TRUE(oldPolicy.getCache().at(kV4Prefix1).has_value());
  EXPECT_EQ(oldPolicy.getCache().at(kV4Prefix1)->getStatementName(), "stmt1");
  EXPECT_FALSE(oldPolicy.getCache().at(kV4Prefix2).has_value());

  // Old policy must still be usable — overwriteRouteAttributes must not crash
  RibEntry entry3(kV4Prefix1);
  RouteAttributePolicy::RibChange ribChange;
  EXPECT_NO_THROW(oldPolicy.overwriteRouteAttributes(entry3, ribChange));
}

/**
 * Test: ExpirationOnlyCachePreservationTest
 *
 * Verifies the full Rib-level cache preservation flow for expiration-only
 * policy updates — the most common scenario in production (CTE hourly
 * refreshes). When a policy is replaced with identical content but a different
 * expiration time:
 *   1. migrateRouteAttributePolicyCache() returns hasUpdate=true,
 *      needsReEvaluation=false
 *   2. Cache is moved (not rebuilt) to the new policy via moveCache()
 *   3. No FIB programming is triggered
 *   4. Subsequent route evaluations use the moved cache (cache hits)
 */
TEST_P(RibFixtureAddPathTestSuite, ExpirationOnlyCachePreservationTest) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  auto now = std::chrono::seconds(std::time(nullptr));
  auto expirationT1 = now.count() + 3600;
  auto expirationT2 = now.count() + 7200;

  auto prefix1 = kV4Prefix1; // matches the policy
  auto prefix2 = kV4Prefix2; // does NOT match — negative cache

  // Step 1: Install routes and initial policy
  {
    auto attr = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attr->setNexthop(kLocalRouteV4Nexthop);
    attr->publish();

    auto prefixBatch1 = PrefixPathIds{{prefix1, kDefaultPathID}};
    auto prefixBatch2 = PrefixPathIds{{prefix2, kDefaultPathID}};
    sendAnnouncement(prefixBatch1, localPeer_, attr);
    sendAnnouncement(prefixBatch2, localPeer_, attr);

    sendRouteAttributePolicySet(createTRouteAttributePolicyLbw(
        {prefix1}, kLbw10G, "stmt1", expirationT1));
    rib_->waitForRouteAttributePolicyUpdate();

    auto fibFuture = fib_->getFibProgramFuture();
    sendInitialPathComputation();
    fibFuture.wait();
  }

  // Step 2: Verify initial state — cache warm, LBW applied
  {
    ASSERT_NE(rib_->routeAttributePolicy_, nullptr);

    const auto& cache = rib_->routeAttributePolicy_->getCache();
    ASSERT_EQ(cache.size(), 2);
    ASSERT_TRUE(cache.at(prefix1).has_value());
    EXPECT_EQ(cache.at(prefix1)->getStatementName(), "stmt1");
    EXPECT_FALSE(cache.at(prefix2).has_value());

    const auto& ribEntry1 = rib_->defaultVrf_.ribEntries().find(prefix1);
    ASSERT_NE(rib_->defaultVrf_.ribEntries().end(), ribEntry1);
    EXPECT_TRUE(ribEntry1->second.getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(kLbw10G, ribEntry1->second.getRibPolicyUcmpWeight().value());

    EXPECT_NE(rib_->fibItems.end(), rib_->fibItems.find(prefix1));
  }

  // Step 3: Verify migration result via migrateRouteAttributePolicyCache()
  {
    auto tNewPolicy = createTRouteAttributePolicyLbw(
        {prefix1}, kLbw10G, "stmt1", expirationT2);
    RouteAttributePolicy newPolicyForMigrationCheck{tNewPolicy};

    auto result = rib_->migrateRouteAttributePolicyCache(
        *rib_->routeAttributePolicy_, newPolicyForMigrationCheck);

    EXPECT_TRUE(result.hasUpdate);
    EXPECT_FALSE(result.needsReEvaluation);
    EXPECT_TRUE(result.affectedPrefixes.empty());

    // Cache was moved to the new policy
    const auto& migratedCache = newPolicyForMigrationCheck.getCache();
    EXPECT_EQ(migratedCache.size(), 2);
    ASSERT_TRUE(migratedCache.at(prefix1).has_value());
    EXPECT_EQ(migratedCache.at(prefix1)->getStatementName(), "stmt1");
    EXPECT_FALSE(migratedCache.at(prefix2).has_value());

    // Restore cache to the live policy for subsequent end-to-end test
    rib_->routeAttributePolicy_->moveCache(newPolicyForMigrationCheck);
  }

  // Step 4: Verify end-to-end via replaceRouteAttributePolicy()
  {
    // Clear fibItems so we can verify no new FIB programming occurs
    rib_->evb_.runInEventBaseThreadAndWait([&]() { rib_->fibItems.clear(); });

    // Replace with identical content but different expiration
    auto tPolicy2 = createTRouteAttributePolicyLbw(
        {prefix1}, kLbw10G, "stmt1", expirationT2);
    bool hasUpdate = false;
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      hasUpdate = rib_->replaceRouteAttributePolicy(
          std::make_unique<RouteAttributePolicy>(tPolicy2));
    });

    EXPECT_TRUE(hasUpdate);
    rib_->evb_.runInEventBaseThreadAndWait(
        [&]() { EXPECT_EQ(0, rib_->fibItems.size()); });
  }

  // Step 5: Verify cache on the newly installed policy
  {
    ASSERT_NE(rib_->routeAttributePolicy_, nullptr);
    const auto& cache = rib_->routeAttributePolicy_->getCache();
    EXPECT_EQ(cache.size(), 2);
    ASSERT_TRUE(cache.at(prefix1).has_value());
    EXPECT_EQ(cache.at(prefix1)->getStatementName(), "stmt1");
    EXPECT_FALSE(cache.at(prefix2).has_value());
  }

  // Step 6: Verify subsequent evaluations use moved cache (cache hits)
  {
    RibEntry entry(prefix1);
    entry.setRibPolicyUcmpWeight(kLbw10G);
    RouteAttributePolicy::RibChange ribChange;
    rib_->routeAttributePolicy_->overwriteRouteAttributes(entry, ribChange);
    EXPECT_TRUE(ribChange.updatedRoutes.empty());
  }
}

/**
 * Test: CacheMigrationStatementRemoved
 *
 * Verifies that when a statement is removed from the policy, cache entries
 * for prefixes that matched the removed statement are NOT preserved and the
 * prefix is added to affectedPrefixes for re-evaluation.
 */
TEST_P(RibFixtureAddPathTestSuite, CacheMigrationStatementRemoved) {
  int64_t weight = 100;
  auto now = std::chrono::seconds(std::time(nullptr));
  auto timestamp = now.count() + 3600;

  // Old policy: stmt1 (prefix1) + stmt2 (prefix2)
  auto tOldPolicy =
      createTRouteAttributePolicyLbw({kV4Prefix1}, weight, "stmt1", timestamp);
  tOldPolicy.statements()->emplace(
      "stmt2",
      createTRouteAttributeStatementLbw({kV4Prefix2}, weight, timestamp));
  RouteAttributePolicy oldPolicy{tOldPolicy};

  // Warm old cache: prefix1 -> stmt1, prefix2 -> stmt2
  RibEntry entry1(kV4Prefix1);
  RibEntry entry2(kV4Prefix2);
  RouteAttributePolicy::RibChange warmupChange;
  oldPolicy.overwriteRouteAttributes(entry1, warmupChange);
  oldPolicy.overwriteRouteAttributes(entry2, warmupChange);

  EXPECT_EQ(oldPolicy.getCache().size(), 2);
  EXPECT_EQ(oldPolicy.getCache().at(kV4Prefix1)->getStatementName(), "stmt1");
  EXPECT_EQ(oldPolicy.getCache().at(kV4Prefix2)->getStatementName(), "stmt2");

  // New policy: only stmt2 (stmt1 removed)
  auto tNewPolicy =
      createTRouteAttributePolicyLbw({kV4Prefix2}, weight, "stmt2", timestamp);
  RouteAttributePolicy newPolicy{tNewPolicy};

  auto result = rib_->migrateRouteAttributePolicyCache(oldPolicy, newPolicy);

  EXPECT_TRUE(result.hasUpdate);
  EXPECT_TRUE(result.needsReEvaluation);

  // prefix1 should be in affectedPrefixes (its statement was removed)
  EXPECT_THAT(result.affectedPrefixes, ::testing::Contains(kV4Prefix1));

  // prefix1 cache should NOT be in the new policy (removed statement)
  EXPECT_FALSE(newPolicy.getCache().contains(kV4Prefix1));

  // prefix2 cache should be preserved (stmt2 unchanged)
  EXPECT_TRUE(newPolicy.getCache().contains(kV4Prefix2));
  EXPECT_EQ(newPolicy.getCache().at(kV4Prefix2)->getStatementName(), "stmt2");
}

/**
 * Test: CacheMigrationMatcherChange
 *
 * Verifies that when a statement's matcher changes, the cache entry is
 * invalidated (NOT preserved) and the prefix is added to affectedPrefixes.
 * This forces re-evaluation through the cache-miss path which re-checks the
 * matcher, preventing the bug where actions were applied to prefixes that
 * no longer match.
 */
TEST_P(RibFixtureAddPathTestSuite, CacheMigrationMatcherChange) {
  int64_t weight = 100;
  auto now = std::chrono::seconds(std::time(nullptr));
  auto timestamp = now.count() + 3600;

  // Old policy: stmt1 matches prefix1
  auto tOldPolicy =
      createTRouteAttributePolicyLbw({kV4Prefix1}, weight, "stmt1", timestamp);
  RouteAttributePolicy oldPolicy{tOldPolicy};

  // Warm old cache: prefix1 -> stmt1
  RibEntry entry1(kV4Prefix1);
  RouteAttributePolicy::RibChange warmupChange;
  oldPolicy.overwriteRouteAttributes(entry1, warmupChange);

  EXPECT_EQ(oldPolicy.getCache().size(), 1);
  EXPECT_EQ(oldPolicy.getCache().at(kV4Prefix1)->getStatementName(), "stmt1");

  // New policy: stmt1 now matches prefix2 (matcher changed)
  auto tNewPolicy =
      createTRouteAttributePolicyLbw({kV4Prefix2}, weight, "stmt1", timestamp);
  RouteAttributePolicy newPolicy{tNewPolicy};

  auto result = rib_->migrateRouteAttributePolicyCache(oldPolicy, newPolicy);

  EXPECT_TRUE(result.hasUpdate);
  EXPECT_TRUE(result.needsReEvaluation);

  // prefix1 should be in affectedPrefixes
  EXPECT_THAT(result.affectedPrefixes, ::testing::Contains(kV4Prefix1));

  /*
   * Cache for prefix1 should NOT be preserved (matcher changed — must go
   * through cache-miss path to re-check matcher)
   */
  EXPECT_FALSE(newPolicy.getCache().contains(kV4Prefix1));
}

/**
 * Test: CacheMigrationNegativeCachePreservedWithoutNewStatements
 *
 * Verifies that negative cache entries survive migration when only actions
 * change (no new statements added). Prefixes that matched no statement in
 * the old policy still match no statement when only actions differ.
 */
TEST_P(
    RibFixtureAddPathTestSuite,
    CacheMigrationNegativeCachePreservedWithoutNewStatements) {
  int64_t weight = 100;
  auto now = std::chrono::seconds(std::time(nullptr));
  auto timestamp = now.count() + 3600;

  // Old policy: stmt1 matches prefix1 with weight 100
  auto tOldPolicy =
      createTRouteAttributePolicyLbw({kV4Prefix1}, weight, "stmt1", timestamp);
  RouteAttributePolicy oldPolicy{tOldPolicy};

  // Warm old cache: prefix1 -> stmt1, prefix2 -> negative
  RibEntry entry1(kV4Prefix1);
  RibEntry entry2(kV4Prefix2); // non-matching
  RouteAttributePolicy::RibChange warmupChange;
  oldPolicy.overwriteRouteAttributes(entry1, warmupChange);
  oldPolicy.overwriteRouteAttributes(entry2, warmupChange);

  EXPECT_EQ(oldPolicy.getCache().size(), 2);
  EXPECT_TRUE(oldPolicy.getCache().at(kV4Prefix1).has_value());
  EXPECT_FALSE(oldPolicy.getCache().at(kV4Prefix2).has_value()); // negative

  // New policy: stmt1 same matcher but different action (weight 200)
  int64_t newWeight = 200;
  auto tNewPolicy = createTRouteAttributePolicyLbw(
      {kV4Prefix1}, newWeight, "stmt1", timestamp);
  RouteAttributePolicy newPolicy{tNewPolicy};

  auto result = rib_->migrateRouteAttributePolicyCache(oldPolicy, newPolicy);

  EXPECT_TRUE(result.hasUpdate);
  EXPECT_TRUE(result.needsReEvaluation);

  // prefix1 should be in affectedPrefixes (action changed)
  EXPECT_THAT(result.affectedPrefixes, ::testing::Contains(kV4Prefix1));

  // prefix1 cache should be preserved (action-only change, matcher same)
  EXPECT_TRUE(newPolicy.getCache().contains(kV4Prefix1));

  // prefix2 negative cache should be preserved (no new statements)
  EXPECT_TRUE(newPolicy.getCache().contains(kV4Prefix2));
  EXPECT_FALSE(newPolicy.getCache().at(kV4Prefix2).has_value());
}

/**
 * Test: CacheMigrationReactivatedStatementReEvaluatesNegativeCache
 *
 * A statement that is expired (inactive) when a prefix is first evaluated
 * caches that prefix as a negative (no-match) entry. When the statement is
 * later reactivated by extending its expiration -- an expiration-only content
 * delta, with no new statement added -- those negative entries must be
 * re-evaluated so the prefix picks up the now-active statement. Otherwise the
 * prefix stays stuck at "no match" and never receives the statement's
 * attributes (under-application).
 */
TEST_F(
    RibFixtureAddPathTestSuite,
    CacheMigrationReactivatedStatementReEvaluatesNegativeCache) {
  int64_t weight = 100;
  auto now = std::chrono::seconds(std::time(nullptr));
  auto pastExpiration = now.count() - 100; // stale: expired 100s ago
  auto futureExpiration = now.count() + 3600; // active

  // Old policy: stmt1 structurally matches prefix1, but is expired (inactive).
  auto tOldPolicy = createTRouteAttributePolicyLbw(
      {kV4Prefix1}, weight, "stmt1", pastExpiration);
  RouteAttributePolicy oldPolicy{tOldPolicy};

  /*
   * Warm the cache: prefix1 matches stmt1's matcher, but because stmt1 is
   * inactive it resolves to no match -> negative cache entry.
   */
  RibEntry entry1(kV4Prefix1);
  RouteAttributePolicy::RibChange warmupChange;
  EXPECT_FALSE(oldPolicy.overwriteRouteAttributes(entry1, warmupChange));
  ASSERT_TRUE(oldPolicy.getCache().contains(kV4Prefix1));
  EXPECT_FALSE(oldPolicy.getCache().at(kV4Prefix1).has_value()); // negative

  /*
   * New policy: same stmt1 (same matcher and action), expiration extended into
   * the future -> reactivated (stale -> active), no other content change.
   */
  auto tNewPolicy = createTRouteAttributePolicyLbw(
      {kV4Prefix1}, weight, "stmt1", futureExpiration);
  RouteAttributePolicy newPolicy{tNewPolicy};

  auto result = rib_->migrateRouteAttributePolicyCache(oldPolicy, newPolicy);

  EXPECT_TRUE(result.hasUpdate);
  EXPECT_TRUE(result.needsReEvaluation);
  /*
   * The stale negative entry must be invalidated + re-evaluated so it can pick
   * up the reactivated statement (rather than being preserved as no-match).
   */
  EXPECT_THAT(result.affectedPrefixes, ::testing::Contains(kV4Prefix1));
  EXPECT_FALSE(newPolicy.getCache().contains(kV4Prefix1));

  /*
   * End-to-end: re-evaluating prefix1 against the new policy now matches the
   * reactivated statement.
   */
  RibEntry reEvalEntry(kV4Prefix1);
  RouteAttributePolicy::RibChange reEvalChange;
  EXPECT_TRUE(newPolicy.overwriteRouteAttributes(reEvalEntry, reEvalChange));
  ASSERT_TRUE(newPolicy.getCache().at(kV4Prefix1).has_value());
  EXPECT_EQ("stmt1", newPolicy.getCache().at(kV4Prefix1)->getStatementName());
}

/**
 * Test: CacheMigrationBothExpiredIdentical
 *
 * Verifies that when both old and new policies have identical content AND
 * identical expired timestamps, migrateRouteAttributePolicyCache returns
 * hasUpdate=false and needsReEvaluation=false, with no spurious full
 * re-evaluation.
 */
TEST_F(RibFixtureAddPathTestSuite, CacheMigrationBothExpiredIdentical) {
  int64_t weight = 100;
  auto now = std::chrono::seconds(std::time(nullptr));
  auto expiredTimestamp = now.count() - 100; // expired 100s ago

  // Create two identical policies with same expired timestamp
  auto tOldPolicy = createTRouteAttributePolicyLbw(
      {kV4Prefix1}, weight, "stmt1", expiredTimestamp);
  RouteAttributePolicy oldPolicy{tOldPolicy};

  auto tNewPolicy = createTRouteAttributePolicyLbw(
      {kV4Prefix1}, weight, "stmt1", expiredTimestamp);
  RouteAttributePolicy newPolicy{tNewPolicy};

  auto result = rib_->migrateRouteAttributePolicyCache(oldPolicy, newPolicy);

  // No update, no re-evaluation (identical policies, even though expired)
  EXPECT_FALSE(result.hasUpdate);
  EXPECT_FALSE(result.needsReEvaluation);
  EXPECT_TRUE(result.affectedPrefixes.empty());
}

/**
 * Test: SelectiveReEvaluationOnActionChange
 *
 * Verifies the end-to-end selective re-evaluation path through
 * replaceRouteAttributePolicy(). When a policy is updated with a content
 * change (action only, matcher unchanged):
 *   1. Only the prefix bound to the changed statement gets re-evaluated
 *   2. Prefixes bound to unchanged statements are NOT re-evaluated
 *   3. Cache entries for unaffected statements are preserved
 *   4. FIB is programmed only for the affected prefix
 */
TEST_P(RibFixtureAddPathTestSuite, SelectiveReEvaluationOnActionChange) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  auto now = std::chrono::seconds(std::time(nullptr));
  auto timestamp = now.count() + 3600;

  auto prefix1 = kV4Prefix1; // matches stmt1
  auto prefix2 = kV4Prefix2; // matches stmt2
  auto prefix3 = kV4Prefix3; // matches nothing (negative cache)

  // Step 1: Install routes and initial policy
  {
    auto attr = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attr->setNexthop(kLocalRouteV4Nexthop);
    attr->publish();

    sendAnnouncement(
        PrefixPathIds{{prefix1, kDefaultPathID}}, localPeer_, attr);
    sendAnnouncement(
        PrefixPathIds{{prefix2, kDefaultPathID}}, localPeer_, attr);
    sendAnnouncement(
        PrefixPathIds{{prefix3, kDefaultPathID}}, localPeer_, attr);

    auto policy =
        createTRouteAttributePolicyLbw({prefix1}, kLbw10G, "stmt1", timestamp);
    policy.statements()->emplace(
        "stmt2",
        createTRouteAttributeStatementLbw({prefix2}, kLbw10G, timestamp));
    sendRouteAttributePolicySet(policy);
    rib_->waitForRouteAttributePolicyUpdate();

    auto fibFuture = fib_->getFibProgramFuture();
    sendInitialPathComputation();
    fibFuture.wait();
  }

  // Step 2: Verify initial state — cache warm, LBW applied to prefix1 & prefix2
  {
    ASSERT_NE(rib_->routeAttributePolicy_, nullptr);
    const auto& cache = rib_->routeAttributePolicy_->getCache();
    EXPECT_GE(cache.size(), 2);
    EXPECT_TRUE(cache.at(prefix1).has_value());
    EXPECT_TRUE(cache.at(prefix2).has_value());

    auto it1 = rib_->defaultVrf_.ribEntries().find(prefix1);
    ASSERT_NE(rib_->defaultVrf_.ribEntries().end(), it1);
    EXPECT_EQ(kLbw10G, it1->second.getRibPolicyUcmpWeight().value());

    auto it2 = rib_->defaultVrf_.ribEntries().find(prefix2);
    ASSERT_NE(rib_->defaultVrf_.ribEntries().end(), it2);
    EXPECT_EQ(kLbw10G, it2->second.getRibPolicyUcmpWeight().value());
  }

  /*
   * Step 3: Replace policy — stmt1 action changes (LBW 10G → 5G), stmt2
   * unchanged
   */
  {
    rib_->evb_.runInEventBaseThreadAndWait([&]() { rib_->fibItems.clear(); });

    auto newPolicy =
        createTRouteAttributePolicyLbw({prefix1}, kLbw5G, "stmt1", timestamp);
    newPolicy.statements()->emplace(
        "stmt2",
        createTRouteAttributeStatementLbw({prefix2}, kLbw10G, timestamp));

    auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
    auto fibFuture = fib_->getFibProgramFuture();
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      rib_->replaceRouteAttributePolicy(
          std::make_unique<RouteAttributePolicy>(newPolicy));
    });
    ribFuture.wait();
    fibFuture.wait();
  }

  // Step 4: Verify selective re-evaluation
  {
    // prefix1 should be in fibItems (LBW changed from 10G to 5G)
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      EXPECT_NE(rib_->fibItems.end(), rib_->fibItems.find(prefix1));
      // prefix2 should NOT be in fibItems (stmt2 unchanged)
      EXPECT_EQ(rib_->fibItems.end(), rib_->fibItems.find(prefix2));
      // prefix3 should NOT be in fibItems (negative cache preserved)
      EXPECT_EQ(rib_->fibItems.end(), rib_->fibItems.find(prefix3));
    });

    // prefix1 should have new LBW value
    auto it1 = rib_->defaultVrf_.ribEntries().find(prefix1);
    ASSERT_NE(rib_->defaultVrf_.ribEntries().end(), it1);
    EXPECT_EQ(kLbw5G, it1->second.getRibPolicyUcmpWeight().value());

    // prefix2 should retain old LBW value
    auto it2 = rib_->defaultVrf_.ribEntries().find(prefix2);
    ASSERT_NE(rib_->defaultVrf_.ribEntries().end(), it2);
    EXPECT_EQ(kLbw10G, it2->second.getRibPolicyUcmpWeight().value());

    // Cache should be preserved for all entries
    const auto& cache = rib_->routeAttributePolicy_->getCache();
    EXPECT_TRUE(cache.at(prefix1).has_value());
    EXPECT_EQ(cache.at(prefix1)->getStatementName(), "stmt1");
    EXPECT_TRUE(cache.at(prefix2).has_value());
    EXPECT_EQ(cache.at(prefix2)->getStatementName(), "stmt2");
  }
}

/**
 * Test: FullFallbackOnFirstPolicySet
 *
 * Verifies the full re-evaluation fallback path when a policy is set for the
 * first time (no existing policy). Since there is no old policy, there is no
 * cache to migrate, and affectedPrefixes is empty — triggering the full
 * fallback that calls requirePathSelection() on all ribEntries.
 */
TEST_P(RibFixtureAddPathTestSuite, FullFallbackOnFirstPolicySet) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  auto now = std::chrono::seconds(std::time(nullptr));
  auto timestamp = now.count() + 3600;

  auto prefix1 = kV4Prefix1; // will match the policy
  auto prefix2 = kV4Prefix2; // will not match

  // Step 1: Announce routes, send EOR — no policy yet
  {
    auto attr = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attr->setNexthop(kLocalRouteV4Nexthop);
    attr->publish();

    sendAnnouncement(
        PrefixPathIds{{prefix1, kDefaultPathID}}, localPeer_, attr);
    sendAnnouncement(
        PrefixPathIds{{prefix2, kDefaultPathID}}, localPeer_, attr);

    auto fibFuture = fib_->getFibProgramFuture();
    sendInitialPathComputation();
    fibFuture.wait();
  }

  // Step 2: Verify no LBW on either prefix
  {
    EXPECT_EQ(rib_->routeAttributePolicy_, nullptr);
    auto it1 = rib_->defaultVrf_.ribEntries().find(prefix1);
    ASSERT_NE(rib_->defaultVrf_.ribEntries().end(), it1);
    EXPECT_FALSE(it1->second.getRibPolicyUcmpWeight().has_value());
  }

  // Step 3: Set first policy via replaceRouteAttributePolicy (full fallback)
  {
    rib_->evb_.runInEventBaseThreadAndWait([&]() { rib_->fibItems.clear(); });

    auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
    auto fibFuture = fib_->getFibProgramFuture();
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      auto hasUpdate = rib_->replaceRouteAttributePolicy(
          std::make_unique<RouteAttributePolicy>(createTRouteAttributePolicyLbw(
              {prefix1}, kLbw10G, "stmt1", timestamp)));
      EXPECT_TRUE(hasUpdate);
    });
    ribFuture.wait();
    fibFuture.wait();
  }

  // Step 4: Verify — matching prefix has LBW, FIB updated
  {
    ASSERT_NE(rib_->routeAttributePolicy_, nullptr);

    // prefix1 should have LBW applied
    auto it1 = rib_->defaultVrf_.ribEntries().find(prefix1);
    ASSERT_NE(rib_->defaultVrf_.ribEntries().end(), it1);
    EXPECT_TRUE(it1->second.getRibPolicyUcmpWeight().has_value());
    EXPECT_EQ(kLbw10G, it1->second.getRibPolicyUcmpWeight().value());

    // prefix1 should be in fibItems
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      EXPECT_NE(rib_->fibItems.end(), rib_->fibItems.find(prefix1));
    });
  }
}

/**
 * Test: FullFallbackOnPolicyCleared
 *
 * Verifies the full re-evaluation fallback path when a policy is cleared
 * (newPolicy == nullptr). All ribEntries get requirePathSelection() to remove
 * the LBW attributes that were applied by the old policy.
 */
TEST_P(RibFixtureAddPathTestSuite, FullFallbackOnPolicyCleared) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  auto now = std::chrono::seconds(std::time(nullptr));
  auto timestamp = now.count() + 3600;

  auto prefix1 = kV4Prefix1; // matches the policy
  auto prefix2 = kV4Prefix2; // does not match

  // Step 1: Install routes and policy
  {
    auto attr = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attr->setNexthop(kLocalRouteV4Nexthop);
    attr->publish();

    sendAnnouncement(
        PrefixPathIds{{prefix1, kDefaultPathID}}, localPeer_, attr);
    sendAnnouncement(
        PrefixPathIds{{prefix2, kDefaultPathID}}, localPeer_, attr);

    sendRouteAttributePolicySet(
        createTRouteAttributePolicyLbw({prefix1}, kLbw10G, "stmt1", timestamp));
    rib_->waitForRouteAttributePolicyUpdate();

    auto fibFuture = fib_->getFibProgramFuture();
    sendInitialPathComputation();
    fibFuture.wait();
  }

  // Step 2: Verify prefix1 has LBW
  {
    auto it1 = rib_->defaultVrf_.ribEntries().find(prefix1);
    ASSERT_NE(rib_->defaultVrf_.ribEntries().end(), it1);
    EXPECT_EQ(kLbw10G, it1->second.getRibPolicyUcmpWeight().value());
  }

  // Step 3: Clear policy via replaceRouteAttributePolicy(nullptr)
  {
    rib_->evb_.runInEventBaseThreadAndWait([&]() { rib_->fibItems.clear(); });

    auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
    auto fibFuture = fib_->getFibProgramFuture();
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      auto hasUpdate = rib_->replaceRouteAttributePolicy(nullptr);
      EXPECT_TRUE(hasUpdate);
    });
    ribFuture.wait();
    fibFuture.wait();
  }

  // Step 4: Verify — policy is nullptr, LBW removed, FIB updated
  {
    EXPECT_EQ(rib_->routeAttributePolicy_, nullptr);

    // prefix1 should have LBW cleared
    auto it1 = rib_->defaultVrf_.ribEntries().find(prefix1);
    ASSERT_NE(rib_->defaultVrf_.ribEntries().end(), it1);
    EXPECT_FALSE(it1->second.getRibPolicyUcmpWeight().has_value());

    // prefix1 should be in fibItems (LBW was removed)
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      EXPECT_NE(rib_->fibItems.end(), rib_->fibItems.find(prefix1));
    });
  }
}

/**
 * Test: SelectiveReEvaluationOnStatementRemoved
 *
 * Verifies the selective re-evaluation path when a statement is removed from
 * the policy. The prefix that was bound to the removed statement should be
 * re-evaluated (LBW removed), while prefixes bound to surviving statements
 * should be unaffected.
 */
TEST_P(RibFixtureAddPathTestSuite, SelectiveReEvaluationOnStatementRemoved) {
  rib_->setFibBatchTime(std::chrono::milliseconds(2));

  auto now = std::chrono::seconds(std::time(nullptr));
  auto timestamp = now.count() + 3600;

  auto prefix1 = kV4Prefix1; // matches stmt1 (will be removed)
  auto prefix2 = kV4Prefix2; // matches stmt2 (will survive)

  // Step 1: Install routes and policy with two statements
  {
    auto attr = std::make_shared<facebook::bgp::BgpPath>(
        *buildBgpPathFields(4, 4, 4, 4));
    attr->setNexthop(kLocalRouteV4Nexthop);
    attr->publish();

    sendAnnouncement(
        PrefixPathIds{{prefix1, kDefaultPathID}}, localPeer_, attr);
    sendAnnouncement(
        PrefixPathIds{{prefix2, kDefaultPathID}}, localPeer_, attr);

    auto policy =
        createTRouteAttributePolicyLbw({prefix1}, kLbw10G, "stmt1", timestamp);
    policy.statements()->emplace(
        "stmt2",
        createTRouteAttributeStatementLbw({prefix2}, kLbw5G, timestamp));
    sendRouteAttributePolicySet(policy);
    rib_->waitForRouteAttributePolicyUpdate();

    auto fibFuture = fib_->getFibProgramFuture();
    sendInitialPathComputation();
    fibFuture.wait();
  }

  // Step 2: Verify both prefixes have LBW
  {
    auto it1 = rib_->defaultVrf_.ribEntries().find(prefix1);
    ASSERT_NE(rib_->defaultVrf_.ribEntries().end(), it1);
    EXPECT_EQ(kLbw10G, it1->second.getRibPolicyUcmpWeight().value());

    auto it2 = rib_->defaultVrf_.ribEntries().find(prefix2);
    ASSERT_NE(rib_->defaultVrf_.ribEntries().end(), it2);
    EXPECT_EQ(kLbw5G, it2->second.getRibPolicyUcmpWeight().value());
  }

  // Step 3: Replace policy — remove stmt1, keep stmt2
  {
    rib_->evb_.runInEventBaseThreadAndWait([&]() { rib_->fibItems.clear(); });

    auto newPolicy =
        createTRouteAttributePolicyLbw({prefix2}, kLbw5G, "stmt2", timestamp);

    auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
    auto fibFuture = fib_->getFibProgramFuture();
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      rib_->replaceRouteAttributePolicy(
          std::make_unique<RouteAttributePolicy>(newPolicy));
    });
    ribFuture.wait();
    fibFuture.wait();
  }

  // Step 4: Verify — prefix1 LBW removed (stmt1 gone), prefix2 unchanged
  {
    // prefix1 should have LBW cleared
    auto it1 = rib_->defaultVrf_.ribEntries().find(prefix1);
    ASSERT_NE(rib_->defaultVrf_.ribEntries().end(), it1);
    EXPECT_FALSE(it1->second.getRibPolicyUcmpWeight().has_value());

    // prefix2 should retain its LBW (stmt2 unchanged)
    auto it2 = rib_->defaultVrf_.ribEntries().find(prefix2);
    ASSERT_NE(rib_->defaultVrf_.ribEntries().end(), it2);
    EXPECT_EQ(kLbw5G, it2->second.getRibPolicyUcmpWeight().value());

    /*
     * prefix1 should be in fibItems (LBW was removed)
     * prefix2 should NOT be in fibItems (stmt2 unchanged)
     */
    rib_->evb_.runInEventBaseThreadAndWait([&]() {
      EXPECT_NE(rib_->fibItems.end(), rib_->fibItems.find(prefix1));
      EXPECT_EQ(rib_->fibItems.end(), rib_->fibItems.find(prefix2));
    });

    // Cache should be updated correctly on the new policy
    const auto& cache = rib_->routeAttributePolicy_->getCache();
    EXPECT_TRUE(cache.at(prefix2).has_value());
    EXPECT_EQ(cache.at(prefix2)->getStatementName(), "stmt2");
    // prefix1 gets a negative cache entry after re-evaluation (no match)
    EXPECT_TRUE(cache.contains(prefix1));
    EXPECT_FALSE(cache.at(prefix1).has_value());
  }
}

/*
 * Regression test for D105639072.
 *
 * RibBase::stop() MUST cancel and join all coroutines (cancelAndJoinTasks)
 * BEFORE RibDC::cleanupPlatform() destroys the DC-specific
 * routeAttributePolicyTimer_. Otherwise a still-running coroutine (e.g.
 * processRibPolicyMsgLoop -> scheduleRouteAttributePolicyTimer) could
 * dereference the timer after it was reset.
 *
 * We register a probe coroutine on the Rib's async scope. It suspends until
 * cancelAndJoinTasks() cancels it; on resume it asserts the DC timer is still
 * alive (cleanupPlatform() has not run yet). Under the buggy ordering the timer
 * would already be null here. The single rib_->stop() is driven by TearDown().
 */
TEST_P(RibFixtureAddPathTestSuite, StopJoinsCoroutinesBeforeResettingTimer) {
  folly::Baton<> probeRunning;

  auto probe = [](MockRib* rib,
                  folly::Baton<>* running) -> folly::coro::Task<void> {
    running->post();
    co_await folly::coro::sleepReturnEarlyOnCancel(std::chrono::hours(1));

    EXPECT_NE(rib->routeAttributePolicyTimer_, nullptr)
        << "cleanupPlatform() reset routeAttributePolicyTimer_ before "
           "cancelAndJoinTasks() joined coroutines - use-after-free risk.";
    co_return;
  };

  rib_->getEventBase().runInEventBaseThreadAndWait([&]() {
    rib_->getRibAsyncScope().add(
        folly::coro::co_withExecutor(
            &rib_->getEventBase(), probe(rib_.get(), &probeRunning)));
  });

  /*
   * Ensure the probe is registered and suspended before TearDown drives the
   * single rib_->stop(), which cancels + joins it and runs the assertion.
   */
  probeRunning.wait();
}

} // namespace bgp
} // namespace facebook
