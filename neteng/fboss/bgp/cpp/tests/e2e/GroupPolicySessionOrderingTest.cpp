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

#include <folly/ScopeGuard.h>
#include <folly/coro/Baton.h>
#include <folly/coro/BlockingWait.h>

#include <thrift/lib/cpp2/Flags.h>
#include <thrift/lib/cpp2/util/ScopedServerInterfaceThread.h>

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/BgpServiceBB.h"
#include "neteng/fboss/bgp/cpp/tests/PolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/E2ESessionTestFixture.h"
#include "neteng/fboss/bgp/cpp/watchdog/Watchdog.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"

using facebook::nettools::bgplib::BgpPeerId;

THRIFT_FLAG_DECLARE_bool(server_check_unimplemented_extra_interfaces);

namespace facebook::bgp {
namespace {

constexpr auto kPeerGroupName = "session-ordering-peer-group";
constexpr auto kTagPolicyName = "session-ordering-tag-policy";
constexpr auto kIngressPolicyName = "permit-all-1";
constexpr auto kTagCommunity = "65000:754";
constexpr auto kPrefix = "66.1.0.0/16";
constexpr auto kLearnedPrefix = "70.1.0.0/16";

class GroupPolicySessionOrderingTest : public E2ESessionTestFixture {
 protected:
  void SetUp() override {
    THRIFT_FLAG_SET_MOCK(server_check_unimplemented_extra_interfaces, false);

    auto tagStatement = createBgpPolicyStatement(
        kTagPolicyName,
        {createBgpPolicyTerm(
            "tag-all",
            "",
            {},
            {createBgpPolicyCommunityAction(
                 bgp_policy::CommunityActionType::ADD, {kTagCommunity}),
             createBgpPolicyAction(bgp_policy::BgpPolicyActionType::PERMIT)},
            bgp_policy::FlowControlAction::NEXT_TERM)});
    /*
     * Permit-all accepts what the peers were already sending, so the ingress
     * half changes nothing about the routes; it only serves to make the update
     * affect both directions.
     */
    auto ingressStatement = createBgpPolicyStatement(
        kIngressPolicyName,
        {createBgpPolicyTerm(
            "permit-all",
            "",
            {},
            {createBgpPolicyAction(bgp_policy::BgpPolicyActionType::PERMIT)},
            bgp_policy::FlowControlAction::NEXT_TERM)});
    bgp_policy::BgpPolicies policies;
    policies.bgp_policy_statements()->emplace_back(std::move(tagStatement));
    policies.bgp_policy_statements()->emplace_back(std::move(ingressStatement));
    setPolicyConfig(policies);

    auto peer3 = kDefaultPeerSpec3;
    auto peer4 = kDefaultPeerSpec4;
    peer3.peerGroupName = kPeerGroupName;
    peer4.peerGroupName = kPeerGroupName;
    addPeer(peer3);
    addPeer(peer4);

    createRib();
    createPeerManager(
        /*enableUpdateGroup=*/true,
        /*enableSerializeGroupPdu=*/true);

    watchdog_ = std::make_unique<Watchdog>(config_);
    bgpService_ = std::make_shared<BgpServiceBB>(
        *peerManager_,
        configManager_,
        *rib_,
        *watchdog_,
        /*nlWrapper=*/nullptr,
        /*enable_thrift_protection=*/false);
    bgpClient_ = apache::thrift::makeTestClient<
        apache::thrift::Client<neteng::fboss::bgp::thrift::TBgpService>>(
        bgpService_);
  }

  void TearDown() override {
    bgpClient_.reset();
    bgpService_.reset();
    watchdog_.reset();
    THRIFT_FLAG_UNMOCK(server_check_unimplemented_extra_interfaces);
    E2ESessionTestFixture::TearDown();
  }

  bool waitForPeerState(
      const folly::IPAddress& peerAddr,
      PeerUpdateState expected,
      size_t maxRetries = 100) {
    PeerUpdateState actual = PeerUpdateState::DOWN;
    WITH_RETRIES_N(maxRetries, {
      actual = folly::via(&peerManager_->getEventBase(), [this, peerAddr]() {
                 auto adjRib = getAdjRibByAddr(peerAddr);
                 return adjRib ? adjRib->getPeerState() : PeerUpdateState::DOWN;
               }).get();
      EXPECT_EVENTUALLY_EQ(expected, actual);
    });
    return actual == expected;
  }

  std::shared_ptr<AdjRibOutGroup> getUpdateGroup(
      const folly::IPAddress& peerAddr) {
    return folly::via(
               &peerManager_->getEventBase(),
               [this, peerAddr]() {
                 auto adjRib = getAdjRibByAddr(peerAddr);
                 return adjRib ? adjRib->getUpdateGroup() : nullptr;
               })
        .get();
  }

  neteng::fboss::bgp::thrift::BgpPolicyChangeResult setPeerGroupPolicy(
      const std::string& ingressPolicyName,
      const std::string& egressPolicyName) {
    std::map<std::string, std::map<bgp_policy::DIRECTION, std::string>>
        peerGroupsPolicy;
    peerGroupsPolicy[kPeerGroupName][bgp_policy::DIRECTION::IN] =
        ingressPolicyName;
    peerGroupsPolicy[kPeerGroupName][bgp_policy::DIRECTION::OUT] =
        egressPolicyName;
    return folly::coro::blockingWait(
        bgpClient_->co_setPeerGroupsPolicy(peerGroupsPolicy));
  }

  /*
   * Settle both peers in the group and give peer 3 a learned route.
   *
   * The learned route is what makes the ingress half of the policy update
   * register: setPendingIngressPolicyUpdate() drops the flag unless the adjRib
   * holds entries to re-evaluate, and a locally injected route leaves every
   * Adj-RIB-In empty. The group re-advertises it to both members, peer 3
   * included, so the announcements are drained here rather than left sitting
   * ahead of later traffic in the outbound queues.
   */
  void bringUpPeersWithLearnedRoute(
      const BgpPeerId& peer3,
      const BgpPeerId& peer4) {
    bringUpPeerAndWait(kPeerAddr3);
    bringUpPeerAndWait(kPeerAddr4);
    sendEoRToPeer(peer3);
    sendEoRToPeer(peer4);
    // Each dual-stack session emits one EoR for IPv4 and one for IPv6.
    ASSERT_TRUE(waitForEoR(peer3));
    ASSERT_TRUE(waitForEoR(peer3));
    ASSERT_TRUE(waitForEoR(peer4));
    ASSERT_TRUE(waitForEoR(peer4));
    ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
    ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

    sendUpdateToPeer(peer3, folly::IPAddress::createNetwork(kLearnedPrefix));
    ASSERT_TRUE(waitForRouteInShadowRib(
        folly::IPAddress::createNetwork(kLearnedPrefix)));
    ASSERT_TRUE(drainAndFindRouteAdvertised(
        "v4", "70.1.0.0", 16, kPeerAddr3, kNextHopV4_3.str()));
    ASSERT_TRUE(drainAndFindRouteAdvertised(
        "v4", "70.1.0.0", 16, kPeerAddr4, kNextHopV4_4.str()));
  }

  std::unique_ptr<Watchdog> watchdog_;
  std::shared_ptr<BgpServiceBB> bgpService_;
  std::unique_ptr<
      apache::thrift::Client<neteng::fboss::bgp::thrift::TBgpService>>
      bgpClient_;
};

TEST_F(
    GroupPolicySessionOrderingTest,
    ReconnectingPeerCannotObservePolicyBeforeGroupReconciliation) {
  const BgpPeerId peer3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  const BgpPeerId peer4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  ASSERT_NO_FATAL_FAILURE(bringUpPeersWithLearnedRoute(peer3, peer4));

  injectLocalRoutesAtRuntime({kPrefix}, {}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork(kPrefix)));
  ASSERT_TRUE(verifyRouteAdd(
      "v4", "66.1.0.0", 16, kPeerAddr3, kNextHopV4_3.str(), "4200000001"));
  ASSERT_TRUE(verifyRouteAdd(
      "v4", "66.1.0.0", 16, kPeerAddr4, kNextHopV4_4.str(), "4200000001"));

  bringDownPeerAndWait(kPeerAddr4);
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::DOWN));

  testOnlyDeferInitDump(kPeerAddr4, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr4);
  ASSERT_TRUE(waitForSessionEstablished(kPeerAddr4));
  sendEoRToPeer(peer4);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr4, PeerUpdateState::DETACHED_INIT_DUMP, 30));

  auto policyProcessingEntered = std::make_shared<folly::coro::Baton>();
  auto releasePolicyProcessing = std::make_shared<folly::coro::Baton>();
  peerManager_->testOnlyDeferPolicyUpdateProcessing(
      policyProcessingEntered, releasePolicyProcessing);
  auto releasePolicyProcessingGuard = folly::makeGuard(
      [&releasePolicyProcessing]() { releasePolicyProcessing->post(); });

  const auto policyResult =
      setPeerGroupPolicy(kIngressPolicyName, kTagPolicyName);
  ASSERT_EQ(
      neteng::fboss::bgp::thrift::BgpPolicyChangeResult::POLICIES_APPLIED,
      policyResult);

  bool policyProcessingPaused = false;
  WITH_RETRIES_N(30, {
    policyProcessingPaused = policyProcessingEntered->ready();
    EXPECT_EVENTUALLY_TRUE(policyProcessingPaused);
  });
  ASSERT_TRUE(policyProcessingPaused);

  testOnlyDeferInitDump(kPeerAddr4, false, fboss::kDefaultVrf);
  const bool receivedV4Eor = waitForEoR(peer4);
  const bool receivedV6Eor = waitForEoR(peer4);
  /*
   * The reconnecting peer finished its initial dump while the policy update
   * was applied but not yet reconciled, so it must join the group outright
   * rather than land in detached mode. Asserted before the policy processing
   * is released, so it cannot be satisfied by the reconciliation that follows.
   */
  const bool joinedAfterDump =
      waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING);

  releasePolicyProcessing->post();
  releasePolicyProcessingGuard.dismiss();

  ASSERT_TRUE(receivedV4Eor);
  ASSERT_TRUE(receivedV6Eor);

  EXPECT_TRUE(joinedAfterDump);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  auto group3 = getUpdateGroup(kPeerAddr3);
  auto group4 = getUpdateGroup(kPeerAddr4);
  ASSERT_NE(nullptr, group3);
  EXPECT_EQ(group3, group4);

  /*
   * The race this test guards: without the ordering fix the reconnecting peer
   * materializes its RIB-OUT under the new policy while still registered in a
   * group keyed on the old one, so the collapse on its rejoin attempt finds
   * entry discrepancies and refuses the rejoin (DETACHED_READY_TO_JOIN ->
   * DETACHED_RUNNING). It still reaches JOINED_RUNNING on a later attempt, so
   * peer state alone does not witness the race -- this counter does.
   */
  EXPECT_EQ(group3->getTotalDiscrepancies(), 0);

  /*
   * Retry around the drain: it gives up as soon as the queue is empty, and the
   * group re-advertises on its own batching interval, so a single pass can run
   * before the re-tagged announcement is built.
   */
  WITH_RETRIES_N(30, {
    EXPECT_EVENTUALLY_TRUE(drainAndFindRouteAdvertised(
        "v4",
        "66.1.0.0",
        16,
        kPeerAddr3,
        kNextHopV4_3.str(),
        "4200000001",
        kTagCommunity));
  });
  // The locally injected prefix plus the one learned from peer 3.
  EXPECT_EQ(
      2,
      verifyRibOutEntries(
          kPeerAddr4,
          [](int) { return true; },
          verifyCommOnAdvertisedRoute(kTagCommunity)));
}

/*
 * Same race against the changelist instead of the initial dump.
 *
 * Here the reconnecting peer is allowed to run its dump -- against a RIB that
 * holds only the learned prefix -- and is pinned at DETACHED_READY_TO_JOIN
 * instead. The prefix under test is published while the policy update is
 * parked, so it reaches the group and the detached peer as changelist entries,
 * and the peer collapses against the group off those entries rather than off a
 * RIB walk.
 */
TEST_F(
    GroupPolicySessionOrderingTest,
    ReconnectingPeerCannotObservePolicyBeforeChangelistCatchUp) {
  const BgpPeerId peer3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  const BgpPeerId peer4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

  ASSERT_NO_FATAL_FAILURE(bringUpPeersWithLearnedRoute(peer3, peer4));

  bringDownPeerAndWait(kPeerAddr4);
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::DOWN));

  /*
   * Pin the rejoin rather than the dump, so that peer 4 dumps, activates
   * detached mode and then parks at DETACHED_READY_TO_JOIN -- everything it
   * takes on from there it takes on as a changelist consumer.
   *
   * The dump is deferred only long enough to set the rejoin pin:
   * testOnlySetDeferDrjAcceptance() reaches the peer through the group's
   * registered members, and a peer that is DOWN has been unregistered, so the
   * pin cannot be set before the session is back up and in the group.
   */
  testOnlyDeferInitDump(kPeerAddr4, true, fboss::kDefaultVrf);
  bringUpPeer(kPeerAddr4);
  ASSERT_TRUE(waitForSessionEstablished(kPeerAddr4));
  sendEoRToPeer(peer4);
  ASSERT_TRUE(
      waitForPeerState(kPeerAddr4, PeerUpdateState::DETACHED_INIT_DUMP, 30));

  testOnlyDeferDrjAcceptance(kPeerAddr4, true);
  testOnlyDeferInitDump(kPeerAddr4, false, fboss::kDefaultVrf);
  ASSERT_TRUE(waitForEoR(peer4));
  ASSERT_TRUE(waitForEoR(peer4));
  ASSERT_TRUE(waitForPeerState(
      kPeerAddr4, PeerUpdateState::DETACHED_READY_TO_JOIN, 60));

  auto policyProcessingEntered = std::make_shared<folly::coro::Baton>();
  auto releasePolicyProcessing = std::make_shared<folly::coro::Baton>();
  peerManager_->testOnlyDeferPolicyUpdateProcessing(
      policyProcessingEntered, releasePolicyProcessing);
  auto releasePolicyProcessingGuard = folly::makeGuard(
      [&releasePolicyProcessing]() { releasePolicyProcessing->post(); });

  const auto policyResult =
      setPeerGroupPolicy(kIngressPolicyName, kTagPolicyName);
  ASSERT_EQ(
      neteng::fboss::bgp::thrift::BgpPolicyChangeResult::POLICIES_APPLIED,
      policyResult);

  bool policyProcessingPaused = false;
  WITH_RETRIES_N(30, {
    policyProcessingPaused = policyProcessingEntered->ready();
    EXPECT_EVENTUALLY_TRUE(policyProcessingPaused);
  });
  ASSERT_TRUE(policyProcessingPaused);

  // Publish on the changelist with the policy update parked mid-flight.
  injectLocalRoutesAtRuntime({kPrefix}, {}, 150);
  ASSERT_TRUE(
      waitForRouteInShadowRib(folly::IPAddress::createNetwork(kPrefix)));

  /*
   * Release the rejoin while the policy update is still parked, so the collapse
   * runs against a group that has taken the same changelist entries. Asserted
   * before the policy processing is released, so it cannot be satisfied by the
   * reconciliation that follows.
   */
  testOnlyDeferDrjAcceptance(kPeerAddr4, false);
  const bool rejoinedFromChangelist =
      waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING);

  releasePolicyProcessing->post();
  releasePolicyProcessingGuard.dismiss();

  EXPECT_TRUE(rejoinedFromChangelist);
  ASSERT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
  ASSERT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

  auto group3 = getUpdateGroup(kPeerAddr3);
  auto group4 = getUpdateGroup(kPeerAddr4);
  ASSERT_NE(nullptr, group3);
  EXPECT_EQ(group3, group4);

  /*
   * Same guard as the dump variant above, reached the other way: the peer
   * caught up on the changelist and collapsed against a group holding the same
   * entries, so the collapse finds nothing to reconcile.
   */
  EXPECT_EQ(group3->getTotalDiscrepancies(), 0);

  WITH_RETRIES_N(30, {
    EXPECT_EVENTUALLY_TRUE(drainAndFindRouteAdvertised(
        "v4",
        "66.1.0.0",
        16,
        kPeerAddr3,
        kNextHopV4_3.str(),
        "4200000001",
        kTagCommunity));
  });
  // The prefix published on the changelist plus the one learned from peer 3.
  EXPECT_EQ(
      2,
      verifyRibOutEntries(
          kPeerAddr4,
          [](int) { return true; },
          verifyCommOnAdvertisedRoute(kTagCommunity)));
}

} // namespace
} // namespace facebook::bgp
