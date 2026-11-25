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

#define SessionManager_TEST_FRIENDS \
  FRIEND_TEST(PeerManagerTestFixture, SharedAdjRibOutGroupNameTest);

#define PeerManager_TEST_FRIENDS                                               \
  FRIEND_TEST(PeerManagerTestFixture, SharedAdjRibOutGroupNameTest);           \
  FRIEND_TEST(PeerManagerTestFixture, AdjRibOutGroupPeerEntriesTest);          \
  FRIEND_TEST(PeerManagerTestFixture, StatefulGrTest1);                        \
  FRIEND_TEST(PeerManagerTestFixture, StatefulGrTest2);                        \
  FRIEND_TEST(PeerManagerTestFixture, StatefulGrTest3);                        \
  FRIEND_TEST(PeerManagerTestFixture, StatefulGrTest4);                        \
  FRIEND_TEST(PeerManagerTestFixture, StatefulGrConvergenceTest);              \
  FRIEND_TEST(                                                                 \
      PeerManagerTestFixture, StatefulGrConvergenceTestWithNoGrNeighbor);      \
  FRIEND_TEST(                                                                 \
      PeerManagerTestFixture, StatefulGrConvergenceTestWithNoGrNeighbor2);     \
  FRIEND_TEST(PeerManagerTestFixture, AdjRibOutGroupAddPathPeerEntriesTest);   \
  FRIEND_TEST(PeerManagerTestFixture, StatefulGrConfigDisabled);               \
  FRIEND_TEST(PeerManagerTestFixture, StatefulGrConfigEnabled);                \
  FRIEND_TEST(PeerManagerTestFixture, StatefulGrKeptOnRestartBeforeRib);       \
  FRIEND_TEST(PeerManagerTestFixture, MultipleFlapTest);                       \
  FRIEND_TEST(PeerManagerTestFixture, LivePeerGroupsRefcountTest);             \
  FRIEND_TEST(PeerManagerTestFixture, LivePeerGroupsSessionLifecycleTest);     \
  FRIEND_TEST(PeerManagerTestFixture, MultipleFlapMultiplePeersTest);          \
  FRIEND_TEST(PeerManagerTestFixture, NullLinkBandwidthBpsTest);               \
  FRIEND_TEST(PeerManagerTestFixture, NullLinkBandwidthBpsReceiveTest);        \
  FRIEND_TEST(PeerManagerTestFixture, BusyWaitTest);                           \
  FRIEND_TEST(PeerManagerTestFixture, NbrDownAfterGRTest);                     \
  FRIEND_TEST(PeerManagerTestFixture, NeighborReachabilityMsgNoGrTest);        \
  FRIEND_TEST(PeerManagerTestFixture, NeighborReachabilityMsgWithGrTest);      \
  FRIEND_TEST(PeerManagerTestFixture, HandleNeighborReachabilityMsgTest);      \
  FRIEND_TEST(PeerManagerTestFixture, ShutdownPeerMsgTest);                    \
  FRIEND_TEST(                                                                 \
      PeerManagerTestFixture, ThriftStreamSubscribePreInitializationTest);     \
  FRIEND_TEST(PeerManagerTestFixture, EnableVipServerLimitTest);               \
  FRIEND_TEST(PeerManagerTestFixture, IsPeerDynamicTest);                      \
  FRIEND_TEST(PeerManagerTestFixture, SetRouteFilterPolicyTest);               \
  FRIEND_TEST(                                                                 \
      PeerManagerTestFixture,                                                  \
      SetRouteFilterPolicyMatchAgainstPeerGroupNameTest);                      \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      SetRouteFilterPolicyForPeerGroupIngressEgressTest);                      \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      SetRouteFilterPolicyForPeerGroupIngressTest);                            \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      SetRouteFilterPolicyForPeerGroupEgressTest);                             \
  FRIEND_TEST(PeerManagerTestFixture, GoldenPrefixesPolicyStatusTestDropMode); \
  FRIEND_TEST(                                                                 \
      PeerManagerTestFixture, GoldenPrefixesPolicyStatusTestSwitchLimitUnset); \
  FRIEND_TEST(                                                                 \
      ClearRouteFiltersFixture, ClearIngressEgressRouteFiltersPolicyTest);     \
  FRIEND_TEST(                                                                 \
      ClearRouteFiltersWithStmtFixture,                                        \
      ClearRouteFilterStatementUpdatesAdjRibAndKey);                           \
  FRIEND_TEST(PeerManagerTestFixture, ClearGoldenPrefixesPolicyTest);          \
  FRIEND_TEST(PeerManagerTestFixture, SetRouteFilterPolicyVersionCheckTest);   \
  FRIEND_TEST(                                                                 \
      PeerManagerTestFixture, IdenticalRouteFilterPolicyPreservesPendingWork); \
  FRIEND_TEST(PeerManagerTestFixture, SetRouteFilterPolicyForceUpdateTest);    \
  FRIEND_TEST(PeerManagerTestFixture, UpdateShadowRibEntryUtil);               \
  FRIEND_TEST(PeerManagerTestFixture, DisableScubaLoggingTest);                \
  FRIEND_TEST(PeerManagerTestFixture, TriggerSafeModeMsgTest);                 \
  FRIEND_TEST(PeerManagerTestFixture, SafeModeOnAllSessionsTest);              \
  FRIEND_TEST(PeerManagerTestFixture, TriggerRouteRefreshRequestNegativeTest); \
  FRIEND_TEST(PeerManagerTestFixture, TriggerRouteRefreshRequestTest);         \
  FRIEND_TEST(PeerManagerTestFixture, TriggerRouteRefreshRequestRrOnlyTest);   \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      UpdateIngressEgressPolicyNamesForIPAddressTest);                         \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      UpdateIngressEgressPolicyNamesForPeerGroupTest);                         \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      UpdateIngressEgressPolicyNamesEmptyMapTest);                             \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      PolicyVersionUpdatedAfterApplyingPolicies);                              \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture, StalePolicyUpdateIsSkipped);  \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      RapidPolicyUpdatesOnlyLatestApplied);                                    \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      InitialPolicyUpdateRequiresConfigVersionIncrement);                      \
  FRIEND_TEST(StreamSubscriberFixture, ThriftNoPeeringStreamSubscribeTest);    \
  FRIEND_TEST(                                                                 \
      StreamSubscriberFixture, ThriftStreamSubscribePostInitializationTest);   \
  FRIEND_TEST(                                                                 \
      StreamSubscriberFixture, ThriftStreamSubscribeIdlePeerQDrainedTest);     \
  FRIEND_TEST(                                                                 \
      StreamSubscriberFixture, ThriftStreamSubscribeForceCompletionTest);      \
  FRIEND_TEST(StreamSubscriberFixture, NumStreamSubscribersTest);              \
  FRIEND_TEST(StreamSubscriberFixture, ExceedsStreamSubscriberLimitTest);      \
  FRIEND_TEST(StreamSubscriberFixture, ConfiguredStreamSubscriberLimitTest);   \
  FRIEND_TEST(StreamSubscriberFixture, DefaultStreamSubscriberLimitTest);      \
  FRIEND_TEST(StreamSubscriberFixture, ReconnectAfterDisconnectAtLimitTest);   \
  FRIEND_TEST(StreamSubscriberFixture, ResubscribeAtLimitTest);                \
  FRIEND_TEST(StreamSubscriberFixture, DuplicateAtLimitReportsDuplicateTest);  \
  FRIEND_TEST(                                                                 \
      StreamSubscriberFixture, StreamSubscriberIdleTimeoutReclaimsSession);    \
  FRIEND_TEST(StreamSubscriberFixture, StreamSubscriberIdleTimeoutDisabled);   \
  FRIEND_TEST(                                                                 \
      StreamSubscriberFixture, StreamSubscriberIdleTimeoutDrainedQueue);       \
  FRIEND_TEST(StreamSubscriberFixture, StreamSubscriberIdleTimeoutSlowClient); \
  FRIEND_TEST(                                                                 \
      StreamSubscriberFixture, StreamSubscriberIdleTimeoutNotifiesClient);     \
  FRIEND_TEST(SafeModeTestFixture, InitializeAdjRibWithGoldenPrefixPolicy);    \
  FRIEND_TEST(PeerManagerTestFixture, PolicyCachePeriodicEvictionTest);        \
  FRIEND_TEST(PeerManagerTestFixture, GetSessionManagerTest);                  \
  FRIEND_TEST(PeerManagerTestFixture, WaitForSessionTerminateBatonTest);       \
  FRIEND_TEST(PeerManagerTestFixture, UpdateEntryStatsTest);                   \
  FRIEND_TEST(PeerManagerTestFixture, SessionFlapRaceConditionTest);           \
  FRIEND_TEST(                                                                 \
      PeerManagerTestFixture, MarkDaemonShutdownClearsAdjRibTreesTest);        \
  FRIEND_TEST(PeerManagerTestFixture, AddPeerTest);                            \
  FRIEND_TEST(                                                                 \
      PeerManagerTestFixture, SetRouteFilterPolicyScopeDeviceRegexTest);       \
  FRIEND_TEST(                                                                 \
      PeerManagerTestFixture, SetRouteFilterPolicyScopePeerGroupNameTest);     \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      UpdateIngressEgressPolicyNamesScopePeerTest);                            \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      UpdateIngressEgressPolicyNamesScopePeerGroupTest);

#define AdjRib_TEST_FRIENDS                                                    \
  friend class PeerManagerTestFixture;                                         \
  friend class StreamSubscriberFixture;                                        \
  FRIEND_TEST(                                                                 \
      StreamSubscriberFixture, ThriftStreamSubscribePostInitializationTest);   \
  FRIEND_TEST(PeerManagerTestFixture, AdjRibOutGroupPeerEntriesTest);          \
  FRIEND_TEST(PeerManagerTestFixture, AdjRibOutGroupAddPathPeerEntriesTest);   \
  FRIEND_TEST(PeerManagerTestFixture, NbrDownAfterGRTest);                     \
  FRIEND_TEST(PeerManagerTestFixture, StatefulGrConvergenceTest);              \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      UpdateIngressEgressPolicyNamesForPeerGroupTest);                         \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      UpdateIngressEgressPolicyNamesForIPAddressTest);                         \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      UpdateIngressEgressPolicyNamesEmptyMapTest);                             \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      PolicyVersionUpdatedAfterApplyingPolicies);                              \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture, StalePolicyUpdateIsSkipped);  \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      RapidPolicyUpdatesOnlyLatestApplied);                                    \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      InitialPolicyUpdateRequiresConfigVersionIncrement);                      \
  FRIEND_TEST(                                                                 \
      PeerManagerTestFixture, StatefulGrConvergenceTestWithNoGrNeighbor);      \
  FRIEND_TEST(PeerManagerTestFixture, SetRouteFilterPolicyTest);               \
  FRIEND_TEST(PeerManagerTestFixture, SetRouteFilterPolicyForceUpdateTest);    \
  FRIEND_TEST(                                                                 \
      PeerManagerTestFixture,                                                  \
      SetRouteFilterPolicyMatchAgainstPeerGroupNameTest);                      \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      SetRouteFilterPolicyForPeerGroupIngressEgressTest);                      \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      SetRouteFilterPolicyForPeerGroupIngressTest);                            \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      SetRouteFilterPolicyForPeerGroupEgressTest);                             \
  FRIEND_TEST(PeerManagerTestFixture, GoldenPrefixesPolicyStatusTestDropMode); \
  FRIEND_TEST(                                                                 \
      PeerManagerTestFixture, GoldenPrefixesPolicyStatusTestSwitchLimitUnset); \
  FRIEND_TEST(                                                                 \
      ClearRouteFiltersFixture, ClearIngressEgressRouteFiltersPolicyTest);     \
  FRIEND_TEST(                                                                 \
      ClearRouteFiltersWithStmtFixture,                                        \
      ClearRouteFilterStatementUpdatesAdjRibAndKey);                           \
  FRIEND_TEST(PeerManagerTestFixture, ClearGoldenPrefixesPolicyTest);          \
  FRIEND_TEST(PeerManagerTestFixture, SetRouteFilterPolicyVersionCheckTest);   \
  FRIEND_TEST(                                                                 \
      PeerManagerTestFixture, IdenticalRouteFilterPolicyPreservesPendingWork); \
  FRIEND_TEST(PeerManagerTestFixture, DisableScubaLoggingTest);                \
  FRIEND_TEST(PeerManagerTestFixture, NeighborReachabilityMsgNoGrTest);        \
  FRIEND_TEST(PeerManagerTestFixture, NeighborReachabilityMsgWithGrTest);      \
  FRIEND_TEST(SafeModeTestFixture, InitializeAdjRibWithGoldenPrefixPolicy);    \
  FRIEND_TEST(PeerManagerTestFixture, PolicyCachePeriodicEvictionTest);        \
  FRIEND_TEST(PeerManagerTestFixture, TriggerRouteRefreshRequestNegativeTest); \
  FRIEND_TEST(PeerManagerTestFixture, TriggerRouteRefreshRequestTest);         \
  FRIEND_TEST(PeerManagerTestFixture, TriggerRouteRefreshRequestRrOnlyTest);   \
  FRIEND_TEST(PeerManagerTestFixture, SessionFlapRaceConditionTest);           \
  FRIEND_TEST(                                                                 \
      PeerManagerTestFixture, MarkDaemonShutdownClearsAdjRibTreesTest);        \
  FRIEND_TEST(PeerManagerTestFixture, AddPeerTest);                            \
  FRIEND_TEST(                                                                 \
      PeerManagerTestFixture, SetRouteFilterPolicyScopeDeviceRegexTest);       \
  FRIEND_TEST(                                                                 \
      PeerManagerTestFixture, SetRouteFilterPolicyScopePeerGroupNameTest);     \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      UpdateIngressEgressPolicyNamesScopePeerTest);                            \
  FRIEND_TEST(PeerManagerTestFixture, GetEffectivePostOutPrefixCountTest);     \
  FRIEND_TEST(                                                                 \
      PeerManagerDynamicPolicyEvaluationFixture,                               \
      UpdateIngressEgressPolicyNamesScopePeerGroupTest);

#define VipPeerManager_TEST_FRIENDS \
  FRIEND_TEST(PeerManagerTestFixture, EnableVipServerLimitTest);

#define PeerManagerDerived_TEST_FRIENDS \
  FRIEND_TEST(PeerManagerTestFixture, EnableVipServerLimitTest);

#define StreamSubscriber_TEST_FRIENDS                                        \
  friend class StreamSubscriberFixture;                                      \
  FRIEND_TEST(StreamSubscriberFixture, ExceedsStreamSubscriberLimitTest);    \
  FRIEND_TEST(StreamSubscriberFixture, ConfiguredStreamSubscriberLimitTest); \
  FRIEND_TEST(StreamSubscriberFixture, DefaultStreamSubscriberLimitTest);    \
  FRIEND_TEST(StreamSubscriberFixture, ReconnectAfterDisconnectAtLimitTest); \
  FRIEND_TEST(StreamSubscriberFixture, ResubscribeAtLimitTest);              \
  FRIEND_TEST(StreamSubscriberFixture, DuplicateAtLimitReportsDuplicateTest);

#define AdjRibOutGroup_TEST_FRIENDS \
  FRIEND_TEST(PeerManagerTestFixture, GetEffectivePostOutPrefixCountTest);

#include <algorithm>
#include <unordered_set>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fmt/format.h>
#include <folly/Optional.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Task.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/fibers/FiberManagerMap.h>
#include <folly/logging/LoggerDB.h>
#include <folly/logging/test/TestLogHandler.h>
#include <folly/logging/xlog.h>
#include <folly/synchronization/Baton.h>
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"

#include <fb303/ThreadCachedServiceData.h>

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/config/ConfigManager.h"
#include "neteng/fboss/bgp/cpp/peer/SessionManager.h"
#include "neteng/fboss/bgp/cpp/tests/BoundedWaitUtils.h"
#include "neteng/fboss/bgp/cpp/tests/PeerManagerTestUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibPolicyUtils.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

DECLARE_bool(disable_route_filter_scuba_logging);
DECLARE_int32(fiber_stack_size);

using namespace facebook::nettools::bgplib;
using namespace facebook::nettools;
using namespace facebook::neteng::fboss::bgp::thrift;
using testing::internal::FloatingPoint;

using ::testing::_;

namespace facebook {
namespace bgp {

namespace {
/*
 * Helper to replicate getBgpSummary() via the split sessionMgr +
 * PeerManagerBase pattern
 */
std::vector<TBgpSession> getSessionsViaSessionMgr(PeerManagerBase& peerMgr) {
  auto allPeers = folly::coro::blockingWait(
      peerMgr.getSessionManager()->co_getAllPeerDisplayInfos());
  return peerMgr.getSessionInfos(allPeers);
}
} // namespace

/**
 * @brief  for a peer not associated to any peer group, adjRibOutGroup
 *         name should be created based on peer's address
 */
TEST_F(PeerManagerTestFixture, buildGroupNameWithoutPeerGroupTest) {
  const PeeringParams peeringParams{fboss::kDefaultVrf};

  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);

  auto groupName =
      PeerManagerVrf::buildAdjRibOutGroupName(kPeerId3, peeringParams);
  EXPECT_EQ(kPeerId3.peerAddr.str(), groupName);
}

/**
 * @brief  for a peer associated to a peer group, adjRibOutGroup
 *         name should be created based on peer group name
 */
TEST_F(PeerManagerTestFixture, buildGroupNameWithPeerGroupNameTest) {
  auto peeringParams = bgp::PeeringParams(fboss::kDefaultVrf);
  std::string testGroupName = "Test";
  peeringParams.peerGroupName = testGroupName;

  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);

  auto groupName =
      PeerManagerVrf::buildAdjRibOutGroupName(kPeerId3, peeringParams);
  EXPECT_EQ(testGroupName, groupName);
}

/**
 * @brief  The same group name in two VRFs creates a separate AdjRibOutGroup
 *         in each VRF
 */
TEST_F(PeerManagerTestFixture, SameAdjRibOutGroupNameInTwoVrfsTest) {
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto& defaultVrf = mockPeerMgr->getDefaultVrf();
  auto& otherVrf =
      mockPeerMgr->createVrf(fboss::kQuarantineVrfId, /*publishStats=*/false);
  const std::string groupName = "Test";

  auto defaultGroup = defaultVrf.createAdjRibOutGroup(groupName);
  EXPECT_EQ(nullptr, otherVrf.findAdjRibOutGroup(groupName));

  auto otherGroup = otherVrf.createAdjRibOutGroup(groupName);
  EXPECT_NE(defaultGroup, otherGroup);
  EXPECT_EQ(fboss::kDefaultVrf, defaultGroup->getVrfId());
  EXPECT_EQ(fboss::kQuarantineVrfId, otherGroup->getVrfId());
  EXPECT_EQ(defaultGroup, defaultVrf.findAdjRibOutGroup(groupName));
  EXPECT_EQ(otherGroup, otherVrf.findAdjRibOutGroup(groupName));
}

/**
 * @brief  Test creation of one AdjRibOutGroup shared by 2 different
 *         adjRibs that share same peer-group
 *         And 3rd different adjRib associated with different
 *         AdjRibOutGroup
 */
TEST_F(PeerManagerTestFixture, SharedAdjRibOutGroupNameTest) {
  auto NewPeer = createBgpPeer(
      kPeerAsn5,
      kLocalAddr1,
      kPeerAddr5,
      kNextHopV4_5,
      kNextHopV6_5,
      true /* isPassive */,
      kPeerTypeCsw);
  auto config = addPeerToConfig(
      getConfig(
          true /* includeStaticPeer */, false /* includeDynamicShivPeer */),
      NewPeer,
      kPeerGroupName2);
  auto globalConfig = config->getBgpGlobalConfig();

  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager,
      nullptr,
      ribInQ_, /* write to the queue */
      ribOutQ_, /* read from the queue */
      nbrRouteChangeQ_);
  auto localSessionMgr = std::make_shared<SessionManager>(
      *globalConfig,
      false, /* enableMessagesOverNotifyQueue */
      true); /* enableCoroNotifyQueue - required for PeerManagerBase's
                processPeerEventLoop */
  peerMgr->setSessionManager(localSessionMgr);

  folly::EventBase evb;

  folly::fibers::Baton stopPeerBaton, peerStoppedBaton;
  auto& fm = folly::fibers::getFiberManager(evb, options_);
  initThreeSessionMgrs(&fm);

  std::vector<folly::Future<folly::Unit>> taskFutures;

  // task to add peers
  const int peerMgrPort = *config->getConfig().listen_port();
  sessionMgr1_->setRestartingState(false);
  sessionMgr2_->setRestartingState(false);
  sessionMgr3_->setRestartingState(false);
  sessionMgr1_->addPeer(
      kLocalAddr1, kAsn1, kPeerAsn3, {kPeerAddr3, 0}, peerMgrPort);
  sessionMgr2_->addPeer(
      kLocalAddr1, kAsn1, kPeerAsn4, {kPeerAddr4, 0}, peerMgrPort);
  sessionMgr3_->addPeer(
      kLocalAddr1, kAsn1, kPeerAsn5, {kPeerAddr5, 0}, peerMgrPort);

  // Verify allPeers_ ODS counter after adding 3 peers
  {
    auto tcData = fb303::ThreadCachedServiceData::get();
    tcData->publishStats();
    EXPECT_EQ(3, tcData->getCounter(BgpStats::kAllPeersCount));
  }

  // task to wait session up and send EoRs
  {
    auto task = fm.addTaskFuture([&] {
      // getSessionsComeUpFuture and getEstablishedCallback().
      folly::collectAll(
          sessionMgr1_->getSessionsComeUpFuture({kLocalPeerId1}),
          sessionMgr2_->getSessionsComeUpFuture({kLocalPeerId1}),
          sessionMgr3_->getSessionsComeUpFuture({kLocalPeerId1}))
          .get();

      nettools::bgplib::fiberSleepFor(100ms);

      // confirm that session comes up
      EXPECT_TRUE(callback1_.isSessionUp(kLocalPeerId1));
      EXPECT_TRUE(callback2_.isSessionUp(kLocalPeerId1));
      EXPECT_TRUE(callback3_.isSessionUp(kLocalPeerId1));

      /*
       * Wait for PeerManagerBase to establish sessions (create adjRibs).
       * Sessions are established asynchronously via asyncScope_, so we need
       * to wait for them to complete before calling stop().
       * Use a timeout to avoid infinite loops if something goes wrong.
       * Access adjRibs_ via the PeerManagerBase's event base to avoid data
       * races.
       */
      int waitCount = 0;
      constexpr int maxWait = 500; // 5 seconds max (500 * 10ms)
      size_t adjRibsSize = 0;
      while (adjRibsSize < 3 && waitCount < maxWait) {
        nettools::bgplib::fiberSleepFor(10ms);
        adjRibsSize = folly::via(&peerMgr->getEventBase(), [&] {
                        return peerMgr->adjRibs_.size();
                      }).get();
        waitCount++;
      }
      EXPECT_EQ(3, adjRibsSize);

      // Verify ODS counters after sessions come up
      auto [grPeersSize, peerAddrToIdsSize] =
          folly::via(&peerMgr->getEventBase(), [&] {
            return std::make_pair(
                peerMgr->getDefaultVrf().getEstablishedGrPeers().size(),
                peerMgr->peerAddrToIds_.size());
          }).get();
      auto tcData = fb303::ThreadCachedServiceData::get();
      tcData->publishStats();
      EXPECT_EQ(
          grPeersSize, tcData->getCounter(BgpStats::kEstablishedGrPeersCount));
      EXPECT_EQ(
          peerAddrToIdsSize, tcData->getCounter(BgpStats::kPeerAddrToIdsCount));

      stopPeerBaton.post();
    });
    taskFutures.emplace_back(std::move(task));
  }
  {
    auto task = fm.addTaskFuture([&] {
      /*
       * Wait for sessions to be established and updates sent before
       * terminating the sessions.
       */
      facebook::bgp::test::boundedBatonWait(
          peerStoppedBaton,
          "peerStoppedBaton",
          facebook::bgp::test::kDefaultPopTimeout);

      /*
       * Verify adjRibOutGroups were created as expected
       */
      EXPECT_TRUE(peerMgr->getDefaultVrf().getAdjRibOutGroups().contains(
          kPeerGroupName1));
      EXPECT_TRUE(peerMgr->getDefaultVrf().getAdjRibOutGroups().contains(
          kPeerGroupName2));
      EXPECT_EQ(peerMgr->getDefaultVrf().getAdjRibOutGroups().size(), 2);

      // Verify ODS counter matches adjRibOutGroups_ size
      auto tcData = fb303::ThreadCachedServiceData::get();
      tcData->publishStats();
      EXPECT_EQ(
          peerMgr->getDefaultVrf().getAdjRibOutGroups().size(),
          tcData->getCounter(BgpStats::kAdjRibOutGroupsCount));

      /*
       * Verify ODS counter still matches establishedGrPeers_ size
       * (decrement doesn't happen here because PeerManagerBase's event base
       * stops before sessionTerminated() callbacks are delivered)
       */
      tcData->publishStats();
      EXPECT_EQ(
          peerMgr->getDefaultVrf().getEstablishedGrPeers().size(),
          tcData->getCounter(BgpStats::kEstablishedGrPeersCount));

      auto sessionDownFuture1 =
          sessionMgr1_->getSessionsGoDownFuture({kLocalPeerId1});
      auto sessionDownFuture2 =
          sessionMgr2_->getSessionsGoDownFuture({kLocalPeerId1});
      auto sessionDownFuture3 =
          sessionMgr3_->getSessionsGoDownFuture({kLocalPeerId1});

      sessionMgr1_->shutdownWithGR(false);
      sessionMgr2_->shutdownWithGR(false);
      sessionMgr3_->shutdownWithGR(false);

      folly::collectAll(
          sessionDownFuture1, sessionDownFuture2, sessionDownFuture3)
          .get();

      /*
       * shouldn't stop before the sessionDownFutures, otherwise we stop the evb
       * to run those futures
       */
      sessionMgr1_->stop();
      sessionMgr2_->stop();
      sessionMgr3_->stop();
    });
    taskFutures.emplace_back(std::move(task));
  }

  // create peer manager thread
  auto peerMgrThread = peerMgr->runInThread();
  auto localSessionMgrThread = localSessionMgr->runInThread();

  auto sessionMgr1Thread = sessionMgr1_->runInThread();
  auto sessionMgr2Thread = sessionMgr2_->runInThread();
  auto sessionMgr3Thread = sessionMgr3_->runInThread();

  // create evbThread to pump all of the fiber tasks
  auto evbThread = std::thread([&]() { evb.loop(); });
  evb.waitUntilRunning();

  facebook::bgp::test::boundedBatonWait(
      stopPeerBaton, "stopPeerBaton", facebook::bgp::test::kDefaultPopTimeout);
  /*
   * stop sessions by shutting down PeerManagerBase
   */
  peerMgr->markDaemonShutdown();
  localSessionMgr->stop();
  peerMgr->stop();
  peerStoppedBaton.post();

  /*
   * Step4: wait for all fiber task futures to be completed
   */
  folly::collectAll(taskFutures.begin(), taskFutures.end()).get();

  peerMgrThread.join();
  evbThread.join();
  localSessionMgrThread.join();
  sessionMgr1Thread.join();
  sessionMgr2Thread.join();
  sessionMgr3Thread.join();

  // release the session managers before evb is destroyed
  sessionMgr1_.reset();
  sessionMgr2_.reset();
  sessionMgr3_.reset();
}

/**
 * @brief  Test adjRibEntries in one unified radix tree per adjRibGroup
 *         For non add-path peers
 *         Check for number of entries per peer
 */
TEST_F(PeerManagerTestFixture, AdjRibOutGroupPeerEntriesTest) {
  auto config = getConfig(
      true,
      true,
      false,
      false,
      false,
      true /*enableVipService*/,
      0,
      false,
      false,
      false,
      {"enable_vip_server_limit"} /* bgp_setting_config */);
  auto globalConfig = config->getBgpGlobalConfig();

  // create MockPeerManager with the config
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  // create session manager
  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);

  peerMgr->setSessionManager(sessionMgr);

  // create peer manager thread
  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  folly::EventBase evb;
  auto& fm = folly::fibers::getFiberManager(evb, options_);

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  // start testing peer field by field
  fm.addTask([&] {
    // Create two adjRibs
    auto adjRib1 = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId1,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config);
    peerMgr->adjRibs_[kPeerId1] = adjRib1;
    EXPECT_NE(vrf->getChangeListTracker(), nullptr);
    adjRib1->adjRibOutGroup_ =
        std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "Group");
    auto adjRib2 = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId2,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config);
    peerMgr->adjRibs_[kPeerId2] = adjRib2;
    adjRib2->adjRibOutGroup_ = adjRib1->adjRibOutGroup_;

    /*
     * Add non add-path entries
     */
    adjRib1->addRibEntry(false, kPeerPrefix1, kDefaultPathID);
    adjRib2->addRibEntry(false, kPeerPrefix1, kDefaultPathID);
    adjRib2->addRibEntry(false, kPeerPrefix2, kDefaultPathID);

    /*
     * Verify radix-tree state
     */
    EXPECT_EQ(2, adjRib1->getRibTreeSize(false, false));
    EXPECT_EQ(1, adjRib1->getRibTreePeerEntriesCount(false, false));
    EXPECT_EQ(2, adjRib2->getRibTreePeerEntriesCount(false, false));

    /*
     * Delete one of the entries
     */
    adjRib2->deleteRibEntry(false, kPeerPrefix1, kDefaultPathID);

    /*
     * Verify radix-tree state
     */
    EXPECT_EQ(2, adjRib1->getRibTreeSize(false, false));
    EXPECT_EQ(1, adjRib1->getRibTreePeerEntriesCount(false, false));
    EXPECT_EQ(1, adjRib2->getRibTreePeerEntriesCount(false, false));

    /*
     * Delete all entries
     */
    adjRib1->deleteRibEntry(false, kPeerPrefix1, kDefaultPathID);
    adjRib2->deleteRibEntry(false, kPeerPrefix2, kDefaultPathID);

    /*
     * Verify radix-tree state
     */
    EXPECT_EQ(0, adjRib1->getRibTreeSize(false, false));
    EXPECT_EQ(0, adjRib1->getRibTreePeerEntriesCount(false, false));
    EXPECT_EQ(0, adjRib2->getRibTreePeerEntriesCount(false, false));

    // Stop PeerManagerBase
    fiberSleepFor(10ms);
    peerMgr->markDaemonShutdown();
    sessionMgr->stop();
    peerMgr->stop();
  });

  evb.loop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/**
 * @brief  Test adjRibEntries in one unified radix tree per adjRibGroup
 *         For add-path peers
 *         Check for number of entries per peer
 */
TEST_F(PeerManagerTestFixture, AdjRibOutGroupAddPathPeerEntriesTest) {
  auto config = getConfig(
      true,
      true,
      false,
      false,
      false,
      true /*enableVipService*/,
      0,
      false,
      false,
      false,
      {"enable_vip_server_limit"} /* bgp_setting_config */);
  auto globalConfig = config->getBgpGlobalConfig();

  // create MockPeerManager with the config
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  // create session manager
  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);

  peerMgr->setSessionManager(sessionMgr);

  // create peer manager thread
  auto peerMgrThread = peerMgr->runInThread();

  folly::EventBase evb;
  auto& fm = folly::fibers::getFiberManager(evb, options_);

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  // start testing peer field by field
  fm.addTask([&] {
    // Create two adjRibs
    auto adjRib1 = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId1,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config);
    peerMgr->adjRibs_[kPeerId1] = adjRib1;
    adjRib1->adjRibOutGroup_ =
        std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "Group");
    adjRib1->sendAddPath_ = true;
    auto adjRib2 = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId2,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config);
    peerMgr->adjRibs_[kPeerId2] = adjRib2;
    adjRib2->adjRibOutGroup_ = adjRib1->adjRibOutGroup_;
    adjRib2->sendAddPath_ = true;

    /*
     * Add add-path entries
     */
    adjRib1->addRibEntry(false, kPeerPrefix1, 10);
    adjRib1->addRibEntry(false, kPeerPrefix2, 20);
    adjRib2->addRibEntry(false, kPeerPrefix2, 10);

    /*
     * Verify radix-tree state
     */
    EXPECT_EQ(2, adjRib1->getRibTreeSize(false, true));
    EXPECT_EQ(2, adjRib1->getRibTreePeerEntriesCount(false, true));
    EXPECT_EQ(1, adjRib2->getRibTreePeerEntriesCount(false, true));

    /*
     * Delete one entry
     */
    adjRib1->deleteRibEntry(false, kPeerPrefix2, 20);

    /*
     * Verify radix-tree state
     */
    EXPECT_EQ(2, adjRib1->getRibTreeSize(false, true));
    EXPECT_EQ(1, adjRib1->getRibTreePeerEntriesCount(false, true));
    EXPECT_EQ(1, adjRib2->getRibTreePeerEntriesCount(false, true));

    /*
     * Delete all entries
     */
    adjRib1->deleteRibEntry(false, kPeerPrefix1, 10);
    adjRib2->deleteRibEntry(false, kPeerPrefix2, 10);

    /*
     * Verify radix-tree state
     */
    EXPECT_EQ(0, adjRib1->getRibTreeSize(false, true));
    EXPECT_EQ(0, adjRib1->getRibTreePeerEntriesCount(false, true));
    EXPECT_EQ(0, adjRib2->getRibTreePeerEntriesCount(false, true));

    // Stop PeerManagerBase
    fiberSleepFor(10ms);
    peerMgr->stop();
  });

  evb.loop();
  peerMgrThread.join();
  SUCCEED();
}

/**
 * @brief Test the isPeerDynamic() API.
 */
TEST_F(PeerManagerTestFixture, IsPeerDynamicTest) {
  // create session manager
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);

  /*
   * The includeDynamicShivPeer boolean configuration option will configured the
   * dynamic peers for mockPeerManager of under kPeerPrefix1 which is
   * 127.1.0.0/30 and kPeerPrefix2 which is 127.2.0.0/30
   */

  // configured dynamic peers under kPeerPrefix1 CIDR
  EXPECT_TRUE(mockPeerMgr->isPeerDynamic(folly::IPAddress("127.1.0.0")));
  EXPECT_TRUE(mockPeerMgr->isPeerDynamic(folly::IPAddress("127.1.0.1")));
  EXPECT_TRUE(mockPeerMgr->isPeerDynamic(folly::IPAddress("127.1.0.2")));
  EXPECT_TRUE(mockPeerMgr->isPeerDynamic(folly::IPAddress("127.1.0.3")));

  // configured dynamic peers under kPeerPrefix2 CIDR
  EXPECT_TRUE(mockPeerMgr->isPeerDynamic(folly::IPAddress("127.2.0.0")));
  EXPECT_TRUE(mockPeerMgr->isPeerDynamic(folly::IPAddress("127.2.0.1")));
  EXPECT_TRUE(mockPeerMgr->isPeerDynamic(folly::IPAddress("127.2.0.2")));
  EXPECT_TRUE(mockPeerMgr->isPeerDynamic(folly::IPAddress("127.2.0.3")));

  /*
   * These IPAddresses do not fall under any configured kPeerPrefix1 or
   * kPeerPrefix2 CIDR
   */
  EXPECT_FALSE(mockPeerMgr->isPeerDynamic(kDynamicPeerAddr4));
  EXPECT_FALSE(mockPeerMgr->isPeerDynamic(kPeerAddr3));
  EXPECT_FALSE(mockPeerMgr->isPeerDynamic(folly::IPAddress("127.0.0.0")));
  EXPECT_FALSE(mockPeerMgr->isPeerDynamic(folly::IPAddress("127.0.0.255")));
  EXPECT_FALSE(mockPeerMgr->isPeerDynamic(folly::IPAddress("127.1.0.4")));
  EXPECT_FALSE(mockPeerMgr->isPeerDynamic(folly::IPAddress("127.1.0.255")));
  EXPECT_FALSE(mockPeerMgr->isPeerDynamic(folly::IPAddress("127.2.0.4")));
}

/**
 * @brief  Validate subscription works even when monitoring relevant peering
 *         configuration is not configured, since stream peering params are now
 *         synthetically generated.
 */
TEST_F(StreamSubscriberFixture, ThriftNoPeeringStreamSubscribeTest) {
  // Initial setup
  SetUp(false /* configureMonitorPeer */, true /* initialAnnouncementDone */);

  // Verify subscription succeeds (stream params are now always available)
  const std::unique_ptr<std::string> myName =
      std::make_unique<std::string>("testClient");
  auto stream = peerMgr->subscribe(myName);

  EXPECT_EQ(1, peerMgr->streamSubscribers_.size());
  EXPECT_TRUE(peerMgr->streamSubscribers_.contains(*myName));

  const BgpPeerId kStreamPeerId1{kStreamPeerAddr, 1};
  auto& evb = peerMgr->getEventBase();

  auto subscription =
      std::move(stream).toClientStreamUnsafeDoNotUse().subscribeExTry(
          &evb, [](auto&&) {
            // Do nothing
          });
  // Close channel on client side
  subscription.cancel();
  std::move(subscription).detach();

  auto terminateBaton = peerMgr->sessionTerminateBatons_[kStreamPeerId1];
  folly::coro::blockingWait(
      [&]() -> folly::coro::Task<void> { co_await *terminateBaton; }());
  EXPECT_EQ(TBgpPeerState::IDLE, peerMgr->streamSubscribers_.at(*myName).state);
}

/**
 * @brief Basic thrift streaming subscription test, mimicing timing of
 *        subscription before RIB convergence. Verify reception of
 *        bgp updates and eor
 */
TEST_F(PeerManagerTestFixture, ThriftStreamSubscribePreInitializationTest) {
  /*
   * This test assumes all other components of the pipeline is working
   * i.e, Rib, AdjRib, SessionManager
   */
  auto config = getConfig(false, false, true /* include BgpMonitorPeer */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);
  auto& evb = peerMgr->getEventBase();

  auto globalConfig = config->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);

  peerMgr->setSessionManager(sessionMgr);

  // Simulate that EOR is sent out due to all sessions sent EOR in time.
  peerMgr->getDefaultVrf().setEorTimerExpired(false);
  peerMgr->getDefaultVrf().setInitialized(true);
  peerMgr->getDefaultVrf().setRibInitialAnnouncementStarted(false);
  peerMgr->getDefaultVrf().setRibInitialAnnouncementDone(false);

  // create peer manager thread
  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  const BgpPeerId kStreamPeerId1{kStreamPeerAddr, 1};

  /*
   * First test whether subscription functionality works
   * Have one client subscribe to peerManager, see if initial fullDumpRequest
   * was received by RIB and RIB can send out deltas to client
   */
  const std::unique_ptr<std::string> myName =
      std::make_unique<std::string>("testClient");
  auto start_time = std::chrono::steady_clock::now();
  auto stream = peerMgr->subscribe(myName);

  EXPECT_EQ(1, peerMgr->streamSubscribers_.size());
  EXPECT_TRUE(peerMgr->streamSubscribers_.contains(*myName));
  EXPECT_EQ(
      TBgpPeerState::ESTABLISHED,
      peerMgr->streamSubscribers_.at(*myName).state);
  EXPECT_EQ(kStreamPeerId1, peerMgr->streamSubscribers_.at(*myName).peerId);

  auto terminateBaton = peerMgr->sessionTerminateBatons_[kStreamPeerId1];
  // Ensure that upSince is later than start_time, i.e, start_time < upSince
  EXPECT_LT(start_time, peerMgr->streamSubscribers_.at(*myName).upSince);

  // Verify v4OverV6Nexthop for streaming session
  EXPECT_TRUE(
      peerMgr->adjRibs_.at(kStreamPeerId1)->isV4OverV6NexthopNegotiated());

  /*
   * Send one announcement from RIB, see if client gets it
   * AdjRib needs to receive Eor from RIB before it starts publishing
   * So the first announcement's update part will be ignored
   */
  evb.runInEventBaseThreadAndWait([&]() {
    auto message = createRibSingleAnnounce(
        kV4Prefix1, kV4Nexthop1, kLocalRouteAs, true, true, kPlaceholderPathID);
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(message));
    auto announcement = std::get<RibOutAnnouncement>(message);
    EXPECT_TRUE(announcement.initialDump);
    ribOutQ_.push(RibOutMessage{fboss::kDefaultVrf, announcement});
  });

  folly::Synchronized<std::optional<neteng::fboss::bgp::thrift::TBgpRouteDelta>>
      delta_update, delta_eor;
  auto subscription =
      std::move(stream).toClientStreamUnsafeDoNotUse().subscribeExTry(
          &evb, [&delta_update, &delta_eor](auto&& t) {
            if (t.value().update2OrEor()->eor().has_value()) {
              delta_eor.withWLock([&t](auto& delta_eor) { delta_eor = *t; });
            }

            if (t.value().update2OrEor()->update2().has_value()) {
              delta_update.withWLock(
                  [&t](auto& delta_update) { delta_update = *t; });
            }
          });

  while (!delta_eor.rlock()->has_value() ||
         !delta_update.rlock()->has_value()) {
    std::this_thread::yield();
  }

  // Check delta contents
  delta_update.withRLock([](const auto& delta_update) {
    EXPECT_EQ(true, delta_update->update2OrEor()->update2().has_value());
    auto updates = toBgpUpdate(*delta_update->update2OrEor()->update2());
    EXPECT_EQ(1, updates.size());
    EXPECT_EQ(
        folly::IPAddress::networkToString(kV4Prefix1), *updates.at(0).prefix());
    EXPECT_EQ(kV4Nexthop1.str(), *updates.at(0).attrs()->nexthop());
  });
  delta_eor.withRLock([](const auto& delta_eor) {
    EXPECT_EQ(true, delta_eor->update2OrEor()->eor().has_value());
  });
  XLOG(INFO, "successfully received correct delta");

  // Close channel on client side
  subscription.cancel();
  std::move(subscription).detach();

  /*
   * The baton is posted by AdjRib::sessionTerminated() which is called
   * for example when subscription is terminated
   * Wait for post to terminateBaton in this case
   */
  folly::coro::blockingWait(
      [&]() -> folly::coro::Task<void> { co_await *terminateBaton; }());
  EXPECT_EQ(TBgpPeerState::IDLE, peerMgr->streamSubscribers_.at(*myName).state);

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
}

/**
 * @brief Basic thrift streaming subscription test, mimicing timing of
 *        subscription after RIB convergence. Verify reception of
 *        bgp updates and eor
 */
TEST_F(StreamSubscriberFixture, ThriftStreamSubscribePostInitializationTest) {
  // Initial setup
  SetUp(true /* configureMonitorPeer */, true /* initialAnnouncementDone */);
  auto& evb = peerMgr->getEventBase();

  const BgpPeerId kStreamPeerId1{kStreamPeerAddr, 1};

  /*
   * First test whether subscription functionality works
   * Have one client subscribe to peerManager, see if initial fullDumpRequest
   * was received by RIB and RIB can send out deltas to client
   */
  const std::unique_ptr<std::string> myName =
      std::make_unique<std::string>("testClient");
  auto start_time = std::chrono::steady_clock::now();
  auto stream1 = peerMgr->subscribe(myName);

  EXPECT_EQ(1, peerMgr->streamSubscribers_.size());
  EXPECT_TRUE(peerMgr->streamSubscribers_.contains(*myName));
  EXPECT_EQ(
      TBgpPeerState::ESTABLISHED,
      peerMgr->streamSubscribers_.at(*myName).state);
  EXPECT_EQ(kStreamPeerId1, peerMgr->streamSubscribers_.at(*myName).peerId);
  auto adjRib = peerMgr->adjRibs_.at(kStreamPeerId1);
  auto terminateBaton = peerMgr->sessionTerminateBatons_[kStreamPeerId1];
  // Ensure that upSince is later than start_time, i.e, start_time < upSince
  EXPECT_LT(start_time, peerMgr->streamSubscribers_.at(*myName).upSince);

  // Verify v4OverV6Nexthop for streaming session
  EXPECT_TRUE(adjRib->isV4OverV6NexthopNegotiated());

  // asyncScope should be active (not cancelled) after subscribe
  EXPECT_FALSE(adjRib->asyncScope_->isScopeCancellationRequested());

  /*
   * Send one announcement from RIB, see if client gets it
   * AdjRib needs to receive Eor from RIB before it starts publishing
   * So the first announcement's update part will be ignored
   */
  evb.runInEventBaseThreadAndWait([&]() {
    auto message = createRibSingleAnnounce(
        kV4Prefix1, kV4Nexthop1, kLocalRouteAs, true, true, kPlaceholderPathID);
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(message));
    auto announcement = std::get<RibOutAnnouncement>(message);
    EXPECT_TRUE(announcement.initialDump);
    ribOutQ_.push(RibOutMessage{fboss::kDefaultVrf, announcement});
  });

  folly::Synchronized<std::optional<neteng::fboss::bgp::thrift::TBgpRouteDelta>>
      delta;
  auto subscription1 =
      std::move(stream1).toClientStreamUnsafeDoNotUse().subscribeExTry(
          &evb, [&delta](auto&& t) {
            // Ignore the Eor updates
            if (!t.hasValue() || t.value().update2OrEor()->eor().has_value()) {
              return;
            }
            delta.withWLock([&t](auto& delta) { delta = *t; });
          });

  while (!delta.rlock()->has_value()) {
    std::this_thread::yield();
  }

  // Check delta contents
  delta.withRLock([](const auto& delta) {
    EXPECT_EQ(true, delta->update2OrEor()->update2().has_value());
    auto updates = toBgpUpdate(*delta->update2OrEor()->update2());
    EXPECT_EQ(1, updates.size());
    EXPECT_EQ(
        folly::IPAddress::networkToString(kV4Prefix1), *updates.at(0).prefix());
    EXPECT_EQ(kV4Nexthop1.str(), *updates.at(0).attrs()->nexthop());
  });
  XLOG(INFO, "successfully received correct delta");

  // Close channel on client side
  subscription1.cancel();
  std::move(subscription1).detach();

  /*
   * The baton is posted by AdjRib::sessionTerminated() which is called
   * for example when subscription is terminated.
   * Wait for post to terminateBaton in this case.
   * With folly::coro::Baton latch semantics, the baton stays posted
   * until reset(), so no re-signaling is needed.
   */
  folly::coro::blockingWait(
      [&]() -> folly::coro::Task<void> { co_await *terminateBaton; }());
  EXPECT_EQ(TBgpPeerState::IDLE, peerMgr->streamSubscribers_.at(*myName).state);

  // After session terminates, cancellation was requested on the scope
  EXPECT_TRUE(adjRib->asyncScope_->isScopeCancellationRequested());

  /*
   * Now try subscribing with the same subscriberName again.  Ensure that it
   * works, and ensure that we use the same peerId in the second connection
   */
  auto start_time2 = std::chrono::steady_clock::now();
  auto stream2 = peerMgr->subscribe(myName);

  // Ensure that we do not create a second instance of StreamSubscriber
  EXPECT_EQ(1, peerMgr->streamSubscribers_.size());
  EXPECT_TRUE(peerMgr->streamSubscribers_.contains(*myName));
  EXPECT_EQ(
      TBgpPeerState::ESTABLISHED,
      peerMgr->streamSubscribers_.at(*myName).state);
  EXPECT_EQ(kStreamPeerId1, peerMgr->streamSubscribers_.at(*myName).peerId);
  // Ensure that upSince is later than start_time2, i.e, start_time2 < upSince
  EXPECT_LT(start_time2, peerMgr->streamSubscribers_.at(*myName).upSince);

  /*
   * After re-subscribe, ensureAsyncScopeInitialized replaced the cancelled
   * scope with a fresh one
   */
  EXPECT_FALSE(adjRib->asyncScope_->isScopeCancellationRequested());

  auto subscription2 =
      std::move(stream2).toClientStreamUnsafeDoNotUse().subscribeExTry(
          &evb, [](auto&&) {
            // Do nothing
          });

  /*
   * cleanup channel
   * Kill channel on client side
   */
  subscription2.cancel();
  std::move(subscription2).detach();

  /*
   * The baton is posted by AdjRib::sessionTerminated() which is called
   * for example when subscription is terminated
   * Wait for post to terminateBaton in this case
   */
  folly::coro::blockingWait(
      [&]() -> folly::coro::Task<void> { co_await *terminateBaton; }());
  EXPECT_EQ(TBgpPeerState::IDLE, peerMgr->streamSubscribers_.at(*myName).state);
}

TEST_F(PeerManagerTestFixture, EoRTestNeitherSessionRestarting) {
  runEoRTest(false, false);
}

TEST_F(PeerManagerTestFixture, EoRTestSecondSessionRestarting) {
  runEoRTest(false, true);
}

TEST_F(PeerManagerTestFixture, EoRTestFirstSessionRestarting) {
  runEoRTest(true, false);
}

TEST_F(PeerManagerTestFixture, EoRTestBothSessionsRestarting) {
  runEoRTest(true, true);
}

// Verify that stateful GR file is created only if configured
TEST_F(PeerManagerTestFixture, StatefulGrConfigEnabled) {
  std::vector<folly::Future<folly::Unit>> taskFutures;

  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);

  EXPECT_TRUE(config_->getBgpGlobalConfig()->supportStatefulGr);

  // adds peers to sessionMgr
  mockPeerMgr->addPeersToSessionMgr();

  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);
  auto fiber = fm.addTaskFuture([&] {
    fiberSleepFor(10ms);

    AdjRib::ObservableMessageT EoRFromStaticPeer1{
        fboss::kDefaultVrf, kPeerId3, AdjRib::EoR{}};
    folly::coro::blockingWait(
        mockPeerMgr->processAdjRibEvent(std::move(EoRFromStaticPeer1)));
    AdjRib::ObservableMessageT EoRFromStaticPeer2{
        fboss::kDefaultVrf, kPeerId4, AdjRib::EoR{}};
    folly::coro::blockingWait(
        mockPeerMgr->processAdjRibEvent(std::move(EoRFromStaticPeer2)));
  });
  taskFutures.emplace_back(std::move(fiber));

  // create peer manager thread and implicitly pump evb
  auto peerMgrThread = mockPeerMgr->runInThread();

  //  wait for all futures to be executed
  folly::collectAll(taskFutures.begin(), taskFutures.end()).get();

  // Save GR state before stop(), mirroring Main.cpp shutdown sequence
  mockPeerMgr->markDaemonShutdown();
  mockPeerMgr->saveGrState();
  mockPeerMgr->stop();

  // Verify that stateful GR file is created.
  EXPECT_TRUE(isGrStateExists());
  std::remove(FLAGS_gr_state_file.c_str());

  peerMgrThread.join();
  SUCCEED();
}

/*
 * A restart before the initial RIB computation, e.g. back-to-back restarts,
 * leaves the GR state file in place, so the next incarnation still waits only
 * for the peers established at the last clean shutdown.
 */
TEST_F(PeerManagerTestFixture, StatefulGrKeptOnRestartBeforeRib) {
  createGrState({kPeerId3});

  // First incarnation: loads the GR state, then exits before any EoR.
  auto firstPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  firstPeerMgr->addPeersToSessionMgr();
  auto firstThread = firstPeerMgr->runInThread();
  ASSERT_FALSE(
      firstPeerMgr->getDefaultVrf().isRibInitPathComputationNotified());
  firstPeerMgr->markDaemonShutdown();
  firstPeerMgr->saveGrState();
  firstPeerMgr->stop();
  firstThread.join();

  // Next incarnation finds the same state.
  ASSERT_TRUE(isGrStateExists());
  const auto reloaded = firstPeerMgr->readGrState();
  ASSERT_TRUE(reloaded.loaded);
  EXPECT_EQ(std::unordered_set<BgpPeerId>{kPeerId3}, *reloaded.peers);
}

TEST_F(PeerManagerTestFixture, StatefulGrConfigDisabled) {
  std::vector<folly::Future<folly::Unit>> taskFutures;

  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);

  // Disable support of stateful GR. Overiding config value
  const_cast<SupportStatefulGr&>(
      config_->getBgpGlobalConfig()->supportStatefulGr) =
      SupportStatefulGr{false};

  // adds peers to sessionMgr
  mockPeerMgr->addPeersToSessionMgr();

  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);
  auto fiber = fm.addTaskFuture([&] {
    fiberSleepFor(10ms);

    AdjRib::ObservableMessageT EoRFromStaticPeer1{
        fboss::kDefaultVrf, kPeerId3, AdjRib::EoR{}};
    folly::coro::blockingWait(
        mockPeerMgr->processAdjRibEvent(std::move(EoRFromStaticPeer1)));
    AdjRib::ObservableMessageT EoRFromStaticPeer2{
        fboss::kDefaultVrf, kPeerId4, AdjRib::EoR{}};
    folly::coro::blockingWait(
        mockPeerMgr->processAdjRibEvent(std::move(EoRFromStaticPeer2)));
  });
  taskFutures.emplace_back(std::move(fiber));

  // create peer manager thread and implicitly pump evb
  auto peerMgrThread = mockPeerMgr->runInThread();

  //  wait for all futures to be executed
  folly::collectAll(taskFutures.begin(), taskFutures.end()).get();

  mockPeerMgr->stop();

  // Verify that stateful GR file is not created.
  EXPECT_FALSE(isGrStateExists());

  peerMgrThread.join();
  SUCCEED();
}

// Verify shutdownPeer is called upon receiving an ShutdownPeer{} from the peer
TEST_F(PeerManagerTestFixture, ShutdownPeerMsgTest) {
  // create static peer config
  auto mockPeerMgr = setupMockPeerManagerWithSeparateThread(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto mockSessionMgr = setupMockSessionManager(mockPeerMgr);
  auto config = config_;

  auto staticPeerToConfig = config->getPeerToConfig();
  EXPECT_EQ(2, staticPeerToConfig.size());

  EXPECT_CALL(*mockSessionMgr, shutdownPeer(kPeerAddr3, _)).Times(1);
  auto peerMgrThread = mockPeerMgr->runInThread();

  folly::EventBase evb;
  auto& fm = folly::fibers::getFiberManager(evb, options_);
  fm.addTask([&] {
    // give VipService a bit of time to start
    fiberSleepFor(100ms);
    // adds peers to sessionMgr
    mockPeerMgr->addPeersToSessionMgr();

    // Notice that session manager can only be run after addPeersToSessionMgr
    auto sessionMgrThread = mockSessionMgr->runInThread();

    // verify we have 2 sessions for those 2 static peers
    auto sessions = getSessionsViaSessionMgr(*mockPeerMgr);
    EXPECT_EQ(2, sessions.size());

    AdjRib::ObservableMessageT shutDownMsgFromStaticPeer1{
        fboss::kDefaultVrf, kPeerId3, AdjRib::Shutdown{}};
    folly::coro::blockingWait(
        mockPeerMgr->processAdjRibEvent(std::move(shutDownMsgFromStaticPeer1)));

    // stop PeerManagerBase
    mockPeerMgr->stop();
    mockSessionMgr->stop();
    sessionMgrThread.join();
  });

  evb.loop();
  peerMgrThread.join();
  SUCCEED();
}

/*
 * Case 1: Verify that only when all static and dynamic peers of previous
 * incarnation EOR is seen, We send out EOR (without timeout kicking in)
 */
TEST_F(PeerManagerTestFixture, StatefulGrTest1) {
  std::vector<folly::Future<folly::Unit>> taskFutures;

  // create config with static and dynamic peer
  auto mockPeerMgr = setupMockPeerManager(
      true, // includeStaticPeer
      true // includeDynamicShivPeer
  );

  EXPECT_EQ(2, config_->getPeerToConfig().size());
  EXPECT_EQ(2, config_->getDynamicPeerToConfig().size());

  /*
   * Create previous incarnation file with two static and two dynamic peers
   * Static and dynamic peers are interleaved, as order doesn't matter
   */
  createGrState({kPeerId3, kDynamicPeerId1, kPeerId4, kDynamicPeerId2});

  EXPECT_TRUE(isGrStateExists());
  // adds peers to sessionMgr
  mockPeerMgr->addPeersToSessionMgr();

  // GR state is kept until the initial RIB computation.
  EXPECT_TRUE(isGrStateExists());

  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);
  auto fiber = fm.addTaskFuture([&] {
    fiberSleepFor(10ms);

    for (const auto& peerId :
         {kPeerId3, kDynamicPeerId1, kPeerId4, kDynamicPeerId2}) {
      // Verify EoR won't be sent when only subset of EORs are received
      EXPECT_FALSE(
          mockPeerMgr->getDefaultVrf().isRibInitPathComputationNotified());
      // Verify that EOR timer has not yet expired
      EXPECT_FALSE(mockPeerMgr->getDefaultVrf().isEorTimerExpired());
      AdjRib::ObservableMessageT EoRFromPeer{
          fboss::kDefaultVrf, peerId, AdjRib::EoR()};
      folly::coro::blockingWait(
          mockPeerMgr->processAdjRibEvent(std::move(EoRFromPeer)));
    }

    /*
     * make sure EoR will be sent without any delay once
     * all static and dynamic peers EORs received
     */
    EXPECT_TRUE(
        mockPeerMgr->getDefaultVrf().isRibInitPathComputationNotified());
    // Removed once RIB was notified.
    EXPECT_FALSE(isGrStateExists());
    /*
     * Ensure that RIB was notified about EOR without timer needing to be
     * fired
     */
    EXPECT_FALSE(mockPeerMgr->getDefaultVrf().isEorTimerExpired());
  });
  taskFutures.emplace_back(std::move(fiber));

  // create peer manager thread and implicitly pump evb
  auto peerMgrThread = mockPeerMgr->runInThread();

  //  wait for all futures to be executed
  folly::collectAll(taskFutures.begin(), taskFutures.end()).get();

  // this will stop the evb implicitly
  mockPeerMgr->stop();
  peerMgrThread.join();
  SUCCEED();
}

/*
 * Case 2: Verify that stale GR state info is not used
 * In stale info only 1 peer is needed for EOR, in config
 * there are two static peers which need to come up.
 */
TEST_F(PeerManagerTestFixture, StatefulGrTest2) {
  std::vector<folly::Future<folly::Unit>> taskFutures;

  // create config with static peer
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  EXPECT_EQ(2, config_->getPeerToConfig().size());

  /*
   * Create previous incarnation file with only one static peer active
   * but the information is stale
   */
  createGrState({kPeerId3}, true);

  EXPECT_TRUE(isGrStateExists());
  // adds peers to sessionMgr
  mockPeerMgr->addPeersToSessionMgr();

  // GR state is kept until the initial RIB computation.
  EXPECT_TRUE(isGrStateExists());

  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);
  auto fiber = fm.addTaskFuture([&] {
    fiberSleepFor(10ms);

    EXPECT_FALSE(
        mockPeerMgr->getDefaultVrf().isRibInitPathComputationNotified());

    AdjRib::ObservableMessageT EoRFromStaticPeer1{
        fboss::kDefaultVrf, kPeerId3, AdjRib::EoR()};
    folly::coro::blockingWait(
        mockPeerMgr->processAdjRibEvent(std::move(EoRFromStaticPeer1)));

    /*
     * Verifying that we are not using stale saved GR state.
     * Stale info had only kPeerAddr3 but we shouldn't send EOR just
     * by seeing kPeerAddr3
     */
    EXPECT_FALSE(
        mockPeerMgr->getDefaultVrf().isRibInitPathComputationNotified());

    AdjRib::ObservableMessageT EoRFromStaticPeer2{
        fboss::kDefaultVrf, kPeerId4, AdjRib::EoR()};
    folly::coro::blockingWait(
        mockPeerMgr->processAdjRibEvent(std::move(EoRFromStaticPeer2)));

    /*
     * make sure EoR will be sent without any delay once
     * all static peers EORs received (we ignored stale saved info)
     */
    EXPECT_TRUE(
        mockPeerMgr->getDefaultVrf().isRibInitPathComputationNotified());
    // Removed once RIB was notified.
    EXPECT_FALSE(isGrStateExists());
  });
  taskFutures.emplace_back(std::move(fiber));

  // create peer manager thread and implicitly pump evb
  auto peerMgrThread = mockPeerMgr->runInThread();

  //  wait for all futures to be executed
  folly::collectAll(taskFutures.begin(), taskFutures.end()).get();

  // this will stop the evb implicitly
  mockPeerMgr->stop();
  peerMgrThread.join();
  SUCCEED();
}

/*
 * Case 3: Verify that we do not wait for peers from the previous
 * incarnation that were either not Established or didn't advertise GR
 * capability.
 */
TEST_F(PeerManagerTestFixture, StatefulGrTest3) {
  std::vector<folly::Future<folly::Unit>> taskFutures;

  // create config with static and dynamic peer
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      true /* includeDynamicMonitorPeer */);
  EXPECT_EQ(2, config_->getPeerToConfig().size());
  EXPECT_EQ(3, config_->getDynamicPeerToConfig().size());

  /*
   * Create stateful GR file from previous incarnation, with one statically
   * configred peer and one dynamic SHIV peer. Skip adding dynamic bgp monitor
   * peer to the file to simulate it not advertising GR capability (similar to
   * prod).
   */
  createGrState({kPeerId3, kDynamicPeerId1});

  // adds peers to sessionMgr
  mockPeerMgr->addPeersToSessionMgr();

  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);
  auto fiber = fm.addTaskFuture([&] {
    fiberSleepFor(10ms);

    for (const auto& peerId : {kPeerId3, kDynamicPeerId1}) {
      // Verify EoR won't be sent when only subset of EORs are received
      EXPECT_FALSE(
          mockPeerMgr->getDefaultVrf().isRibInitPathComputationNotified());

      AdjRib::ObservableMessageT EoRFromPeer{
          fboss::kDefaultVrf, peerId, AdjRib::EoR()};
      folly::coro::blockingWait(
          mockPeerMgr->processAdjRibEvent(std::move(EoRFromPeer)));
    }

    /*
     * Verify that we are not waiting for BGP-Monitor peerings to
     * re-establish. RIB EOR is sent out when we receive peer EoRs
     * from the previous Established and GR capable peers.
     */
    EXPECT_TRUE(
        mockPeerMgr->getDefaultVrf().isRibInitPathComputationNotified());
  });
  taskFutures.emplace_back(std::move(fiber));

  // create peer manager thread and implicitly pump evb
  auto peerMgrThread = mockPeerMgr->runInThread();

  //  wait for all futures to be executed
  folly::collectAll(taskFutures.begin(), taskFutures.end()).get();

  // this will stop the evb implicitly
  mockPeerMgr->stop();
  peerMgrThread.join();
  SUCCEED();
}

/*
 * Case 4: In presence of dynamic peers, EOR is processed correctly.
 */
TEST_F(PeerManagerTestFixture, StatefulGrTest4) {
  std::vector<folly::Future<folly::Unit>> taskFutures;

  // create config with static and dynamic peer
  auto mockPeerMgr = setupMockPeerManager(
      false /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */,
      false /* includeDynamicVipInjectorPeer */);
  EXPECT_EQ(2, config_->getDynamicPeerToConfig().size());

  // Create previous incarnation file one dynamic peer (no SPR peer)
  createGrState({kDynamicPeerId1});

  EXPECT_TRUE(isGrStateExists());
  // adds peers to sessionMgr
  mockPeerMgr->addPeersToSessionMgr();

  // GR state is kept until the initial RIB computation.
  EXPECT_TRUE(isGrStateExists());

  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);
  auto fiber = fm.addTaskFuture([&] {
    fiberSleepFor(10ms);

    /*
     * We are sending a extra dynamic peer, which was not present in
     * previous incarnation. No rib EoR should be notified.
     */
    mockPeerMgr->getDefaultVrf().processPeerEoR(kDynamicPeerId2);
    EXPECT_FALSE(
        mockPeerMgr->getDefaultVrf().isRibInitPathComputationNotified());

    /*
     * make sure EoR will be sent without any delay once
     * all dynamic peers EORs received
     */
    mockPeerMgr->getDefaultVrf().processPeerEoR(kDynamicPeerId1);
    EXPECT_TRUE(
        mockPeerMgr->getDefaultVrf().isRibInitPathComputationNotified());
    // Removed once RIB was notified.
    EXPECT_FALSE(isGrStateExists());
  });
  taskFutures.emplace_back(std::move(fiber));

  // create peer manager thread and implicitly pump evb
  auto peerMgrThread = mockPeerMgr->runInThread();

  //  wait for all futures to be executed
  folly::collectAll(taskFutures.begin(), taskFutures.end()).get();

  // this will stop the evb implicitly
  mockPeerMgr->stop();
  peerMgrThread.join();
  SUCCEED();
}

/**
 * Verifies that PeerManagerBase does not wait for peers from the previous
 * incarnation when NeighborWatcher reports a neighbor as down. Additionally,
 * this test ensures that other processing, such as purging stale routes, is
 * not impacted.
 */
TEST_F(PeerManagerTestFixture, StatefulGrConvergenceTest) {
  auto config = getConfig(true, false);
  auto globalConfig = config->getBgpGlobalConfig();

  // Create MockPeerManager with the config
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  // Create session manager
  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);

  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();

  // Start coroutine task
  folly::coro::CancellableAsyncScope asyncScope;
  asyncScope.add(
      co_withExecutor(&evb, peerMgr->processNeighborRouteChangeLoop()));

  folly::fibers::Baton adjRibReadyBaton, nbrDownSentBaton;
  auto& fm = folly::fibers::getFiberManager(evb, options_);
  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  fm.addTask([&] {
    // Create previous incarnation file with one static peer
    createGrState({kPeerId3});
    EXPECT_TRUE(isGrStateExists());
    peerMgr->addPeersToSessionMgr();

    // Notice that session manager can only be run after addPeersToSessionMgr
    auto sessionMgrThread = sessionMgr->runInThread();

    // Verify RIB is not notified yet
    EXPECT_FALSE(peerMgr->getDefaultVrf().isRibInitPathComputationNotified());

    // Create an adjRib for this peer
    auto adjRib = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId3,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config,
        false, // isRrClient
        kV4Nexthop1,
        kV6Nexthop1,
        true); // enableStatefulHa
    peerMgr->adjRibs_[kPeerId3] = adjRib;

    // GR state is kept until the initial RIB computation.
    EXPECT_TRUE(isGrStateExists());

    // Verify we have 1 peer in static EOR waiting list
    EXPECT_EQ(1, peerMgr->getDefaultVrf().getStaticPeerEoRReceived().size());

    {
      // Create an entry in AdjRibIn
      auto attrsFields1 = buildBgpPathFields(2, 2, 3, 2);
      auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(*attrsFields1);
      auto adjRibEntry1 = std::make_unique<AdjRibEntry>(kDefaultPathID);
      adjRibEntry1->setPreIn(std::move(attrs1));
      adjRib->adjRibInLiteTree_.insert(
          kPeerPrefix1.first, kPeerPrefix1.second, std::move(adjRibEntry1));
      // Verify 1 entry in AdjRibIn, and none in AdjRibInStale
      EXPECT_EQ(1, adjRib->adjRibInLiteTree_.size());
      EXPECT_EQ(0, adjRib->adjRibInStale_.size());
      // Mark the entries stale and start GR timer to simulate getting into GR
      adjRib->markLearntRoutesStale();
      adjRib->remoteGrRestartTimer_ = folly::AsyncTimeout::schedule(
          std::chrono::milliseconds(200), evb, [&]() noexcept {
            XLOG(INFO, "Remote GR timer fired");
          });
      // Verify entry has been moved to AdjRibInStale
      EXPECT_EQ(0, adjRib->adjRibInLiteTree_.size());
      EXPECT_EQ(1, adjRib->adjRibInStale_.size());
      XLOG(INFO, "Posting adjRibReady baton");
      adjRibReadyBaton.post();
      XLOG(INFO, "Waiting for nbrDownSent baton");
      facebook::bgp::test::boundedBatonWait(
          nbrDownSentBaton, "nbrDownSentBaton");

      // Verify peer is removed from static EOR waiting list
      EXPECT_EQ(0, peerMgr->getDefaultVrf().getStaticPeerEoRReceived().size());

      // Ensure RIB is notified immediately without any delay
      EXPECT_TRUE(peerMgr->getDefaultVrf().isRibInitPathComputationNotified());
      // Removed once RIB was notified.
      EXPECT_FALSE(isGrStateExists());

      // Verify that the entry has been removed from AdjRibInStale
      EXPECT_EQ(0, adjRib->adjRibInLiteTree_.size());
      EXPECT_EQ(0, adjRib->adjRibInStale_.size());
      // Clean up for next test case
      adjRib->adjRibInLiteTree_.clear();
    }

    /*
     * Stop PeerManagerBase — cancel local coroutines before stop() since
     * stop() terminates the evb and coroutines can't process cancellation
     * on a dead event loop.
     */
    fiberSleepFor(10ms);
    peerMgr->markDaemonShutdown();
    folly::coro::blockingWait(asyncScope.cancelAndJoinAsync());
    sessionMgr->stop();
    peerMgr->stop();
    sessionMgrThread.join();
  });

  fm.addTask([&] {
    XLOG(INFO, "Waiting for adjRibReady baton");
    facebook::bgp::test::boundedBatonWait(adjRibReadyBaton, "adjRibReadyBaton");
    nbrRouteChangeQ_->push(
        NeighborEventMsg(fboss::kDefaultVrf, kPeerId3.peerAddr, false));
    // Sleep so that processNeighborRouteChangeLoop can run
    fiberSleepFor(10ms);
    XLOG(INFO, "Posting nbrDownSent Baton");
    nbrDownSentBaton.post();
  });

  evb.loop();
  SUCCEED();
}

/**
 * Verifies that neighbor down reported from NeighborWatcher does not impact
 * GR convergence time if the neighbor was not present in previous
 * incarnation(GR state file).
 */
TEST_F(PeerManagerTestFixture, StatefulGrConvergenceTestWithNoGrNeighbor) {
  auto config = getConfig(true, true);
  auto globalConfig = config->getBgpGlobalConfig();

  // Create MockPeerManager with the config
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  // Create session manager
  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);

  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();

  // Start coroutine task
  folly::coro::CancellableAsyncScope asyncScope;
  asyncScope.add(
      co_withExecutor(&evb, peerMgr->processNeighborRouteChangeLoop()));

  folly::fibers::Baton adjRibReadyBaton, nbrDownSentBaton;
  auto& fm = folly::fibers::getFiberManager(evb, options_);
  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  fm.addTask([&] {
    /*
     * Create previous incarnation file with one static peer and one dynamic
     * peer
     */
    createGrState({kPeerId3, kDynamicPeerId1});
    EXPECT_TRUE(isGrStateExists());
    peerMgr->addPeersToSessionMgr();

    // Notice that session manager can only be run after addPeersToSessionMgr
    auto sessionMgrThread = sessionMgr->runInThread();

    // Verify RIB is not notified yet
    EXPECT_FALSE(peerMgr->getDefaultVrf().isRibInitPathComputationNotified());

    // Create an adjRib for a new Peer
    auto adjRib = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId3,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config,
        false, // isRrClient
        kV4Nexthop1,
        kV6Nexthop1,
        true); // enableStatefulHa
    peerMgr->adjRibs_[kPeerId3] = adjRib;

    // GR state is kept until the initial RIB computation.
    EXPECT_TRUE(isGrStateExists());

    // Verify we have 1 peer each in static/dynamic EOR waiting list
    EXPECT_EQ(1, peerMgr->getDefaultVrf().getStaticPeerEoRReceived().size());
    EXPECT_EQ(1, peerMgr->getDefaultVrf().getDynamicPeerEoRReceived().size());

    {
      XLOG(INFO, "Posting adjRibReady baton");
      adjRibReadyBaton.post();
      XLOG(INFO, "Waiting for nbrDownSent baton");
      facebook::bgp::test::boundedBatonWait(
          nbrDownSentBaton, "nbrDownSentBaton");

      // one static peer is removed
      EXPECT_EQ(0, peerMgr->getDefaultVrf().getStaticPeerEoRReceived().size());
      EXPECT_EQ(1, peerMgr->getDefaultVrf().getDynamicPeerEoRReceived().size());

      // Ensure RIB is not notified yet
      EXPECT_FALSE(peerMgr->getDefaultVrf().isRibInitPathComputationNotified());

      for (const auto& peerId : {kPeerId3, kDynamicPeerId1}) {
        // Verify EoR won't be sent when only subset of EORs are received
        EXPECT_FALSE(
            peerMgr->getDefaultVrf().isRibInitPathComputationNotified());
        // Verify that EOR timer has not yet expired
        EXPECT_FALSE(peerMgr->getDefaultVrf().isEorTimerExpired());
        AdjRib::ObservableMessageT EoRFromPeer{
            fboss::kDefaultVrf, peerId, AdjRib::EoR()};
        folly::coro::blockingWait(
            peerMgr->processAdjRibEvent(std::move(EoRFromPeer)));
      }
      /*
       * make sure EoR will be sent without any delay once
       * all static and dynamic peers EORs received
       */
      EXPECT_TRUE(peerMgr->getDefaultVrf().isRibInitPathComputationNotified());
      // Removed once RIB was notified.
      EXPECT_FALSE(isGrStateExists());
      /*
       * Ensure that RIB was notified about EOR without timer needing to be
       * fired
       */
      EXPECT_FALSE(peerMgr->getDefaultVrf().isEorTimerExpired());
    }

    /*
     * Stop PeerManagerBase — cancel local coroutines before stop() since
     * stop() terminates the evb and coroutines can't process cancellation
     * on a dead event loop.
     */
    fiberSleepFor(10ms);
    peerMgr->markDaemonShutdown();
    folly::coro::blockingWait(asyncScope.cancelAndJoinAsync());
    sessionMgr->stop();
    peerMgr->stop();

    sessionMgrThread.join();
  });

  fm.addTask([&] {
    XLOG(INFO, "Waiting for adjRibReady baton");
    facebook::bgp::test::boundedBatonWait(adjRibReadyBaton, "adjRibReadyBaton");

    /*
     * Initiate neighbor down event for a peer that was not present in previous
     * incarnation
     */
    nbrRouteChangeQ_->push(
        NeighborEventMsg(fboss::kDefaultVrf, kPeerId3.peerAddr, false));
    // Sleep so that processNeighborRouteChangeLoop can run
    fiberSleepFor(10ms);
    XLOG(INFO, "Posting nbrDownSent Baton");
    nbrDownSentBaton.post();
  });

  evb.loop();
  SUCCEED();
}

/**
 * Verifies that neighbor down reported from NeighborWatcher does not impact
 * GR convergence time if the neighbor was not present in previous
 * incarnation(GR state file).
 */
TEST_F(PeerManagerTestFixture, StatefulGrConvergenceTestWithNoGrNeighbor2) {
  const auto eorTimeS{1};
  auto config = getConfig(
      true /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */,
      false /* includeDynamicVipInjectorPeer */,
      false /* enableStatefulHa */,
      false /* enableVipServer */,
      eorTimeS);
  auto globalConfig = config->getBgpGlobalConfig();

  // Create MockPeerManager with the config
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  // Create session manager
  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);

  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();

  // Start coroutine task
  folly::coro::CancellableAsyncScope asyncScope;
  asyncScope.add(
      co_withExecutor(&evb, peerMgr->processNeighborRouteChangeLoop()));

  folly::fibers::Baton adjRibReadyBaton, nbrDownSentBaton;
  auto& fm = folly::fibers::getFiberManager(evb, options_);
  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  fm.addTask([&] {
    /*
     * Create previous incarnation file with one static peer and one dynamic
     * peer
     */
    createGrState({kPeerId3, kDynamicPeerId1});
    EXPECT_TRUE(isGrStateExists());
    peerMgr->addPeersToSessionMgr();

    // Verify RIB is not notified yet
    EXPECT_FALSE(peerMgr->getDefaultVrf().isRibInitPathComputationNotified());

    // Create an adjRib for a new Peer
    auto adjRib = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId3,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config,
        false, // isRrClient
        kV4Nexthop1,
        kV6Nexthop1,
        true); // enableStatefulHa
    peerMgr->adjRibs_[kPeerId3] = adjRib;

    // GR state is kept until the initial RIB computation.
    EXPECT_TRUE(isGrStateExists());

    // Verify we have 1 peer each in static/dynamic EOR waiting list
    EXPECT_EQ(1, peerMgr->getDefaultVrf().getStaticPeerEoRReceived().size());
    EXPECT_EQ(1, peerMgr->getDefaultVrf().getDynamicPeerEoRReceived().size());

    {
      // Ensure RIB is not notified yet
      EXPECT_FALSE(peerMgr->getDefaultVrf().isRibInitPathComputationNotified());

      for (const auto& peerId : {kPeerId3}) {
        // Verify EoR won't be sent when only subset of EORs are received
        EXPECT_FALSE(
            peerMgr->getDefaultVrf().isRibInitPathComputationNotified());
        // Verify that EOR timer has not yet expired
        EXPECT_FALSE(peerMgr->getDefaultVrf().isEorTimerExpired());
        AdjRib::ObservableMessageT EoRFromPeer{
            fboss::kDefaultVrf, peerId, AdjRib::EoR()};
        folly::coro::blockingWait(
            peerMgr->processAdjRibEvent(std::move(EoRFromPeer)));
      }
      /*
       * make sure EoR will be sent without any delay once
       * all static and dynamic peers EORs received
       */
      EXPECT_FALSE(peerMgr->getDefaultVrf().isRibInitPathComputationNotified());

      // Mock EOR_TIMER_EXPIRED
      peerMgr->getDefaultVrf().notifyRibInitialPathComputation(
          /*timerFired=*/true);

      /*
       * Initiate neighbor down event for a peer that was not present in
       * previous incarnation
       */
      nbrRouteChangeQ_->push(
          NeighborEventMsg(fboss::kDefaultVrf, kPeerId3.peerAddr, false));
      // Sleep so that processNeighborRouteChangeLoop can run
      fiberSleepFor(10ms);
      XLOG(INFO, "Posting nbrDownSent Baton");
      nbrDownSentBaton.post();
    }
  });

  fm.addTask([&] {
    // Notice that session manager can only be run after addPeersToSessionMgr
    auto sessionMgrThread = sessionMgr->runInThread();

    XLOG(INFO, "Waiting for nbrDownSent baton");
    facebook::bgp::test::boundedBatonWait(nbrDownSentBaton, "nbrDownSentBaton");

    peerMgr->getDefaultVrf().setRibInitialAnnouncementStarted(true);
    for (const auto& peerId : {kPeerId3}) {
      AdjRib::ObservableMessageT peerEgressEoR{
          fboss::kDefaultVrf, peerId, AdjRib::EgressEoR()};
      folly::coro::blockingWait(
          peerMgr->processAdjRibEvent(std::move(peerEgressEoR)));
    }

    EXPECT_TRUE(peerMgr->getIsInitialized());

    /*
     * Stop PeerManagerBase — cancel local coroutines before stop() since
     * stop() terminates the evb and coroutines can't process cancellation
     * on a dead event loop.
     */
    fiberSleepFor(10ms);
    peerMgr->markDaemonShutdown();
    folly::coro::blockingWait(asyncScope.cancelAndJoinAsync());
    sessionMgr->stop();
    peerMgr->stop();

    sessionMgrThread.join();
  });

  evb.loop();
  SUCCEED();
}

/*
 * Verify that if UCMP SET_LINK_BPS is specified in peering param
 * and linkBandwidthBps is null, we crash
 */
TEST_F(PeerManagerTestFixture, NullLinkBandwidthBpsTest) {
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);

  // Simulate that peerVersion is no longer valid when established is called.
  uint64_t version = 0x100;

  auto mockInfo = mockInfo1_;
  mockInfo.peeringParams.advertiseLinkBandwidth =
      AdvertiseLinkBandwidth::SET_LINK_BPS;
  ON_CALL(*sessionMgr, getEstablishedPeerDisplayInfo(kPeerId3))
      .WillByDefault(testing::Return(mockInfo));

  auto sessionInfo = std::make_shared<FiberBgpPeer::ObservableSessionInfo>();
  sessionInfo->peerInfo = mockInfo;
  sessionInfo->currentVersion = std::make_shared<VersionNumber>();

  FiberBgpPeer::ObservableStateT stateEvent{
      .peerId = kPeerId3,
      .state = BgpSessionState::ESTABLISHED,
      .versionNumber = version,
      .remoteAs = mockInfo1_.peeringParams.remoteAs,
      .sessionInfo = sessionInfo};

  EXPECT_DEATH(
      folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEvent)),
      "UCMP SET_LINK_BPS is specified for peer.*");
}

/**
 * @brief Verify that sessionEstablished fails with CHECK when
 *        receiveLinkBandwidth is set to SET_LINK_BPS but linkBandwidthBps
 *        is not set.
 */
TEST_F(PeerManagerTestFixture, NullLinkBandwidthBpsReceiveTest) {
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);

  // Simulate that peerVersion is no longer valid when established is called.
  uint64_t version = 0x100;

  auto mockInfo = mockInfo1_;
  mockInfo.peeringParams.receiveLinkBandwidth =
      ReceiveLinkBandwidth::SET_LINK_BPS;
  ON_CALL(*sessionMgr, getEstablishedPeerDisplayInfo(kPeerId3))
      .WillByDefault(testing::Return(mockInfo));

  auto sessionInfo = std::make_shared<FiberBgpPeer::ObservableSessionInfo>();
  sessionInfo->peerInfo = mockInfo;
  sessionInfo->currentVersion = std::make_shared<VersionNumber>();

  FiberBgpPeer::ObservableStateT stateEvent{
      .peerId = kPeerId3,
      .state = BgpSessionState::ESTABLISHED,
      .versionNumber = version,
      .remoteAs = mockInfo1_.peeringParams.remoteAs,
      .sessionInfo = sessionInfo};

  EXPECT_DEATH(
      folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEvent)),
      "UCMP SET_LINK_BPS is specified for peer.*");
}

/*
 * This test manipulates the `isPeerVersionValid` mocking result to simulate:
 *  1. Set `isPeerVersionValid = true`. Establish and terminate a session.
 *  2. Set `isPeerVersionValid = false`. Verify sessionEstablished() call will
 *     be ignored due to invalid version. This mimicks a quick flap of sesison.
 *  3. Bump up session and reset `isPeerVersionValid = true`. Make sure session
 *     establishment and termination can now pass through.
 */
TEST_F(PeerManagerTestFixture, MultipleFlapTest) {
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */);
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);
  uint64_t version = 0x100;
  auto versionNumber = std::make_shared<VersionNumber>(version);

  auto sessionMgrThread = sessionMgr->runInThread();

  auto& evb = mockPeerMgr->getEventBase();
  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);
  fm.addTask([&] {
    mockPeerMgr->getDefaultVrf().setRibInitPathComputationNotified(false);
    /*
     * Step 0: Simulate that FiberBgpPeer has the same version
     */

    auto sessionInfo = FiberBgpPeer::getObservableSessionInfo(
        mockInfo1_,
        nullptr /* unbounded input queue, unused */,
        sessionMgr->boundedIqueue_,
        sessionMgr->oQueue_,
        versionNumber);

    /*
     * Step 1: Call sessionEstablished() and verify AdjRib is created and
     * marked as established
     */

    FiberBgpPeer::ObservableStateT stateEvent{
        .peerId = kPeerId3,
        .versionNumber = version,
        .remoteAs = mockInfo1_.peeringParams.remoteAs,
        .sessionInfo = sessionInfo};

    folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEvent));
    auto adjRib = mockPeerMgr->findAdjRib(kPeerId3);
    EXPECT_EQ(true, adjRib->isStateEstablished());
    EXPECT_EQ(true, adjRib->inInitialAnnouncement());
    EXPECT_NE(nullptr, adjRib->getChangeListConsumer());
    /*
     * Verify Group name, this adjRib belongs, is constructed off of
     * kPeerId3, in absence of peer-group configuration for this peer
     */
    EXPECT_EQ(kPeerId3.peerAddr.str(), adjRib->getAdjRibOutGroupName());
    EXPECT_TRUE(mockPeerMgr->getDefaultVrf().getAdjRibOutGroups().contains(
        kPeerId3.peerAddr.str()));

    fiberSleepFor(20ms);

    /*
     * Step2: Terminate the session. Let adjRib and peerMgr both see the events
     */
    sessionMgr->oQueue_->open();
    sessionMgr->oQueue_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    stateEvent.versionNumber =
        ++version; // Simulate FiberBgpPeerManager increasing version

    sessionInfo->currentVersion = std::make_shared<VersionNumber>(version);
    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEvent));
    EXPECT_EQ(false, adjRib->inInitialAnnouncement());
    EXPECT_EQ(false, adjRib->isStateEstablished());
    /*
     * Verify Group name adjRib associated to remains same
     */
    EXPECT_EQ(kPeerId3.peerAddr.str(), adjRib->getAdjRibOutGroupName());
    EXPECT_TRUE(mockPeerMgr->getDefaultVrf().getAdjRibOutGroups().contains(
        kPeerId3.peerAddr.str()));

    /*
     * Post baton to simulate message loops exiting. In production,
     * postTerminateBaton() posts the baton after both loops signal the local
     * semaphore.
     */
    auto batonIt = mockPeerMgr->sessionTerminateBatons_.find(kPeerId3);
    ASSERT_NE(batonIt, mockPeerMgr->sessionTerminateBatons_.end());
    batonIt->second->post();

    fiberSleepFor(20ms);

    /*
     * Step3: Simulate the FiberBgpPeer has a different version, aka, this
     *        mimicks the situation that PeerMgr has a slow pace reading
     *        the previous session establishment. When the reading happens,
     *        the FiberBgpPeer has flapped, hence with an invalid version.
     */
    stateEvent.versionNumber =
        ++version; // Simulate FiberBgpPeerManager increasing version

    folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEvent));
    fiberSleepFor(10ms);
    EXPECT_EQ(false, adjRib->isStateEstablished());

    // sessionTermination call will be ignored as adjRib state is down
    stateEvent.versionNumber =
        ++version; // Simulate FiberBgpPeerManager increasing version
    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEvent));
    fiberSleepFor(10ms);

    /*
     * With baton latch semantics, no re-signal is needed here.
     * The baton stays posted between post() and reset(), so Step 3's
     * waitForSessionTerminateBaton passes through without consuming
     * tokens, and Step 4 will also pass through.
     */

    /*
     * Step 4: Simulate sessionTermination with version++ and then session
     *         establishment with version++. Verify the sessionEstablished
     *         call can be handled correctly.
     */
    stateEvent.versionNumber =
        ++version; // Simulate FiberBgpPeerManager increasing version
    sessionInfo->currentVersion = std::make_shared<VersionNumber>(version);

    // verify adjRib ptr is still valid
    EXPECT_NE(nullptr, adjRib);

    /*
     * Subscribe to the PeerManagerVrf category, not the root category.
     * TestLogHandler is not thread-safe, and with a root handler at DBG1 the
     * thrift and configerator client threads also log into it while this
     * thread does. The expected message comes from PeerManagerVrf, which runs
     * on this thread only.
     */
    const std::string logCategory{"neteng.fboss.bgp.cpp.peer.PeerManagerVrf"};
    auto& messages = subscribeToLogMessages(logCategory, folly::LogLevel::DBG1);
    folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEvent));
    fiberSleepFor(10ms);

    // verify logging as well as state
    auto expectedMsg = fmt::format(
        "Not sending RibDumpReq for {}; Rib has not started initial announcement.",
        kPeerId3.str());
    auto it =
        std::find_if(messages.begin(), messages.end(), [&](const auto& entry) {
          return entry.first.getMessage() == expectedMsg;
        });
    EXPECT_NE(it, messages.end());
    EXPECT_EQ(true, adjRib->isStateEstablished());
    ASSERT_TRUE(getRunningSessions());
    EXPECT_EQ(1, *getRunningSessions());

    /*
     * Step 5: Make sure normal termination with the version++ will correctly
     *         mark this adjRib session terminated.
     */
    stateEvent.versionNumber =
        ++version; // Simulate FiberBgpPeerManager increasing version
    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEvent));
    sessionMgr->oQueue_->open();
    sessionMgr->oQueue_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    fiberSleepFor(10ms);
    EXPECT_EQ(false, adjRib->isStateEstablished());
    ASSERT_TRUE(getRunningSessions());
    EXPECT_EQ(0, *getRunningSessions());

    folly::LoggerDB::get().getCategory(logCategory)->clearHandlers();

    // Explicitly stop the peer manager to cancel all coro tasks
    mockPeerMgr->stop();
    sessionMgr->stop();
  });

  evb.loop();
  sessionMgrThread.join();
  SUCCEED();
}

TEST_F(PeerManagerTestFixture, MultipleFlapMultiplePeersTest) {
  facebook::fb303::ThreadCachedServiceData::getShared()->zeroStats();
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */);
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);
  uint64_t version = 0x100;
  auto versionNumber = std::make_shared<VersionNumber>(version);

  auto sessionMgrThread = sessionMgr->runInThread();

  auto& evb = mockPeerMgr->getEventBase();
  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);
  fm.addTask([&] {
    mockPeerMgr->getDefaultVrf().setRibInitPathComputationNotified(false);

    /*
     * Step 1:
     * Establish 2 sessions - PeerId3 and PeerId4,
     * both are in initial announcement state
     */

    auto sessionInfo = FiberBgpPeer::getObservableSessionInfo(
        mockInfo1_,
        nullptr /* unbounded input queue, unused */,
        sessionMgr->boundedIqueue_,
        sessionMgr->oQueue_,
        versionNumber);

    FiberBgpPeer::ObservableStateT stateEventPeer3{
        .peerId = kPeerId3,
        .versionNumber = version,
        .remoteAs = mockInfo1_.peeringParams.remoteAs,
        .sessionInfo = sessionInfo};

    FiberBgpPeer::ObservableStateT stateEventPeer4{
        .peerId = kPeerId4,
        .versionNumber = version,
        .remoteAs = mockInfo1_.peeringParams.remoteAs,
        .sessionInfo = sessionInfo};

    folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEventPeer3));
    auto adjRib3 = mockPeerMgr->findAdjRib(kPeerId3);
    EXPECT_EQ(true, adjRib3->isStateEstablished());
    EXPECT_EQ(true, adjRib3->inInitialAnnouncement());
    /*
     * Verify Group name, this adjRib belongs, is constructed off of
     * kPeerId3, in absence of peer-group configuration for this peer
     */
    EXPECT_EQ(kPeerId3.peerAddr.str(), adjRib3->getAdjRibOutGroupName());
    EXPECT_TRUE(mockPeerMgr->getDefaultVrf().getAdjRibOutGroups().contains(
        kPeerId3.peerAddr.str()));

    fiberSleepFor(20ms);

    stateEventPeer4.versionNumber = ++version;
    sessionInfo->currentVersion = std::make_shared<VersionNumber>(version);
    ON_CALL(*sessionMgr, getEstablishedPeerDisplayInfo(kPeerId4))
        .WillByDefault(testing::Return(mockInfo1_));
    folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEventPeer4));
    auto adjRib4 = mockPeerMgr->findAdjRib(kPeerId4);
    EXPECT_EQ(true, adjRib4->isStateEstablished());
    EXPECT_EQ(true, adjRib4->inInitialAnnouncement());
    /*
     * Verify Group name, this adjRib belongs, is constructed off of
     * kPeerId3, in absence of peer-group configuration for this peer
     */
    EXPECT_EQ(kPeerId4.peerAddr.str(), adjRib4->getAdjRibOutGroupName());
    EXPECT_TRUE(mockPeerMgr->getDefaultVrf().getAdjRibOutGroups().contains(
        kPeerId4.peerAddr.str()));

    fiberSleepFor(20ms);

    /*
     * Step 2:
     * Terminate PeerId3
     * PeerId3 now should be reset from initial announcement
     * while PeerId4 still in initial announcement state
     */
    sessionMgr->oQueue_->open();
    sessionMgr->oQueue_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    stateEventPeer3.versionNumber =
        ++version; // Simulate FiberBgpPeerManager increasing version

    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEventPeer3));
    EXPECT_EQ(false, adjRib3->inInitialAnnouncement());
    EXPECT_EQ(true, adjRib4->inInitialAnnouncement());

    fiberSleepFor(20ms);

    /*
     * Step 3:
     * Set all the state to mimic RIB convergence and EOR_SENT
     */
    mockPeerMgr->getDefaultVrf().setRibInitPathComputationNotified(true);
    mockPeerMgr->getDefaultVrf().setEorTimerExpired(false);
    mockPeerMgr->getDefaultVrf().setInitialized(true);
    mockPeerMgr->getDefaultVrf().setRibInitialAnnouncementStarted(true);
    mockPeerMgr->getDefaultVrf().setRibInitialAnnouncementDone(true);

    /*
     * Step 4:
     * Establish PeerId3
     * Since EOR_SENT already, PeerId3 should not be in initial announcement
     * state Since this is mock, EoR would not have changed state of PeerId4
     */
    stateEventPeer3.versionNumber =
        ++version; // Simulate FiberBgpPeerManager increasing version
    sessionInfo->currentVersion = std::make_shared<VersionNumber>(version);

    folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEventPeer3));
    EXPECT_EQ(false, adjRib3->inInitialAnnouncement());
    EXPECT_EQ(true, adjRib4->inInitialAnnouncement());

    fiberSleepFor(20ms);

    /*
     * Step 5:
     * Terminate both PeerId3 and PeerId4
     * Both sessions should be now reset from initial announcement
     */
    stateEventPeer3.versionNumber =
        ++version; // Simulate FiberBgpPeerManager increasing version
    sessionMgr->oQueue_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEventPeer3));
    fiberSleepFor(10ms);
    EXPECT_EQ(false, adjRib3->isStateEstablished());
    EXPECT_EQ(false, adjRib3->inInitialAnnouncement());
    stateEventPeer4.versionNumber =
        ++version; // Simulate FiberBgpPeerManager increasing version
    sessionMgr->oQueue_->open();
    sessionMgr->oQueue_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEventPeer4));
    fiberSleepFor(10ms);
    EXPECT_EQ(false, adjRib4->isStateEstablished());
    EXPECT_EQ(false, adjRib4->inInitialAnnouncement());

    fiberSleepFor(20ms);

    /*
     * Step 6:
     * Establish both sessions
     * Both sessions should not be in initial announcement
     */
    stateEventPeer3.versionNumber = ++version;
    sessionInfo->currentVersion = std::make_shared<VersionNumber>(version);
    folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEventPeer3));
    EXPECT_EQ(false, adjRib3->inInitialAnnouncement());

    fiberSleepFor(20ms);

    stateEventPeer4.versionNumber = ++version;
    sessionInfo->currentVersion = std::make_shared<VersionNumber>(version);
    folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEventPeer4));
    EXPECT_EQ(false, adjRib4->inInitialAnnouncement());

    fiberSleepFor(20ms);

    /*
     * Step 7:
     * Terminate both the sessions to complete the test
     */
    stateEventPeer3.versionNumber =
        ++version; // Simulate FiberBgpPeerManager increasing version
    sessionMgr->oQueue_->open();
    sessionMgr->oQueue_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEventPeer3));
    fiberSleepFor(10ms);
    EXPECT_EQ(false, adjRib3->isStateEstablished());
    EXPECT_EQ(false, adjRib3->inInitialAnnouncement());
    stateEventPeer4.versionNumber =
        ++version; // Simulate FiberBgpPeerManager increasing version
    sessionMgr->oQueue_->open();
    sessionMgr->oQueue_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEventPeer4));
    fiberSleepFor(10ms);
    EXPECT_EQ(false, adjRib4->isStateEstablished());
    EXPECT_EQ(false, adjRib4->inInitialAnnouncement());

    ASSERT_TRUE(getRunningSessions());
    EXPECT_EQ(0, *getRunningSessions());

    // Explicitly stop the peer manager to cancel all coro tasks
    mockPeerMgr->stop();
    sessionMgr->stop();
  });

  evb.loop();
  sessionMgrThread.join();
  SUCCEED();
}

/*
 * Verify that if peer manager sees terminate and next establish before
 * adjRib sees first terminate it will do busy wait and not misbehave in
 * next establishment.
 */
TEST_F(PeerManagerTestFixture, BusyWaitTest) {
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */);
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);
  uint64_t version = 0x100;
  auto versionNumber = std::make_shared<VersionNumber>(version);

  auto& evb = mockPeerMgr->getEventBase();

  /*
   * NOTE: Ideally sessionEstablished and sessionTerminated calls will happen
   * from same fiber. But for this test case, we split into two fibers as
   * during busy wait the whole fiber sleeps. To do validations even with
   * sleep we do sessionEstablished from one fiber and sessionTerminated
   * from another fiber.
   */
  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);
  fm.addTask([&] {
    // Simulate FiberBgpPeer is consistent as seen by peerManager
    auto sessionInfo = FiberBgpPeer::getObservableSessionInfo(
        mockInfo1_,
        nullptr /* unbounded input queue, unused */,
        sessionMgr->boundedIqueue_,
        sessionMgr->oQueue_,
        versionNumber);

    FiberBgpPeer::ObservableStateT stateEvent{
        .peerId = kPeerId3,
        .versionNumber = version,
        .remoteAs = mockInfo1_.peeringParams.remoteAs,
        .sessionInfo = sessionInfo};

    folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEvent));

    // Verify session gets established
    auto adjRib = mockPeerMgr->findAdjRib(kPeerId3);
    EXPECT_EQ(true, adjRib->isStateEstablished());

    /*
     * This will cause busy wait, as adjRib has a sleep of 100ms before
     * it processes sessionTerminated notification
     * Simulate FiberBgpPeer is consistent as seen by peerManager
     */
    stateEvent.versionNumber = version + 2;
    sessionInfo->currentVersion = std::make_shared<VersionNumber>(version + 2);
    folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEvent));
  });

  fm.addTask([&] {
    // This sleep ensures that both sessionEstablished
    fiberSleepFor(100ms);
    auto adjRib = mockPeerMgr->findAdjRib(kPeerId3);
    EXPECT_EQ(true, adjRib->isStateEstablished());

    /*
     * Verify that only first sessionEstablished call went through
     * and increased the counter by 1. If 2nd call went through counter
     * would increase to 2.
     */
    ASSERT_TRUE(getRunningSessions());
    EXPECT_EQ(1, *getRunningSessions());

    FiberBgpPeer::ObservableStateT stateEvent{
        .peerId = kPeerId3, .versionNumber = version + 1};

    /*
     * Terminate session, peer manager will come out of busy wait and
     * establish again immediately due to pending establishment message
     */
    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEvent));
    sessionMgr->oQueue_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    fiberSleepFor(50ms);

    /*
     * Verify that peerManager was in busy wait and did not process the 2nd
     * Establish message till after adjRib has terminated 1st connection.
     */
    EXPECT_EQ(true, adjRib->isStateEstablished());
    ASSERT_TRUE(getRunningSessions());
    EXPECT_EQ(1, *getRunningSessions());

    /*
     * Terminate session, this will terminate the 2nd establishement
     * Needed for clean exist of test case
     */
    stateEvent.versionNumber = version + 3;
    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEvent));
    sessionMgr->oQueue_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    fiberSleepFor(20ms);
    EXPECT_EQ(false, adjRib->isStateEstablished());

    ASSERT_TRUE(getRunningSessions());
    EXPECT_EQ(0, *getRunningSessions());

    // Explicitly stop the peer manager to cancel all coro tasks
    mockPeerMgr->stop();
  });

  evb.loop();
  SUCCEED();
}

// test enable vipServerLimit through bgpSettingConfig
TEST_F(PeerManagerTestFixture, EnableVipServerLimitTest) {
  auto config = getConfig(
      true,
      true,
      false,
      false,
      false,
      true /*enableVipService*/,
      0,
      false,
      false,
      false,
      {"enable_vip_server_limit"} /* bgp_setting_config */);
  auto globalConfig = config->getBgpGlobalConfig();

  // create MockPeerManager with the config
  auto configManager = std::make_shared<ConfigManager>(config);
  MockPeerManager peerMgr(configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);
  EXPECT_TRUE(peerMgr.vipPeerMgr_->isVipServerLimitEnabled());
}

/*
 * If we get nbrDown message when we are in GR state, where we have marked
 * routes stale waiting for peer to come up, we should clean up the stale
 * routes and withdraw them from RIB
 */
TEST_F(PeerManagerTestFixture, NbrDownAfterGRTest) {
  auto config = getConfig(true, false);
  auto globalConfig = config->getBgpGlobalConfig();

  // create MockPeerManager with the config
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  // create session manager
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);

  peerMgr->setSessionManager(sessionMgr);

  // create peer manager thread
  auto peerMgrThread = peerMgr->runInThread();

  folly::EventBase evb;

  // start corotine task
  folly::coro::CancellableAsyncScope asyncScope;
  asyncScope.add(
      co_withExecutor(&evb, peerMgr->processNeighborRouteChangeLoop()));

  folly::fibers::Baton adjRibReadyBaton, nbrDownSentBaton;
  auto& fm = folly::fibers::getFiberManager(evb, options_);
  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  fm.addTask([&] {
    peerMgr->addPeersToSessionMgr();

    // Notice that session manager can only be run after addPeersToSessionMgr
    auto sessionMgrThread = sessionMgr->runInThread();

    // Create an adjRib
    auto adjRib1 = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId1,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config,
        false, // isRrClient
        kV4Nexthop1,
        kV6Nexthop1,
        true); // enableStatefulHa
    peerMgr->adjRibs_[kPeerId1] = adjRib1;
    {
      // Create an entry in AdjRibIn
      auto attrsFields1 = buildBgpPathFields(2, 2, 3, 2);
      auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(*attrsFields1);
      auto adjRibEntry1 = std::make_unique<AdjRibEntry>(kDefaultPathID);
      adjRibEntry1->setPreIn(std::move(attrs1));
      adjRib1->adjRibInLiteTree_.insert(
          kPeerPrefix1.first, kPeerPrefix1.second, std::move(adjRibEntry1));
      // Verify 1 entry in AdjRibIn, and none in AdjRibInStale
      EXPECT_EQ(1, adjRib1->adjRibInLiteTree_.size());
      EXPECT_EQ(0, adjRib1->adjRibInStale_.size());
      // Mark the entries stale and start GR timer to simulate getting into GR
      adjRib1->markLearntRoutesStale();
      adjRib1->remoteGrRestartTimer_ = folly::AsyncTimeout::schedule(
          std::chrono::milliseconds(200), evb, [&]() noexcept {
            XLOG(INFO, "Remote GR timer fired");
          });
      // Verify entry has been moved to AdjRibInStale
      EXPECT_EQ(0, adjRib1->adjRibInLiteTree_.size());
      EXPECT_EQ(1, adjRib1->adjRibInStale_.size());
      XLOG(INFO, "Posting adjRibReady baton");
      adjRibReadyBaton.post();
      XLOG(INFO, "Waiting for nbrDownSent baton");
      facebook::bgp::test::boundedBatonWait(
          nbrDownSentBaton, "nbrDownSentBaton");
      // Verify that the entry has been removed from AdjRibInStale
      EXPECT_EQ(0, adjRib1->adjRibInLiteTree_.size());
      EXPECT_EQ(0, adjRib1->adjRibInStale_.size());
      // Clean up for next test case
      adjRib1->adjRibInLiteTree_.clear();
    }

    // Cancel local coroutines (processNeighborRouteChangeLoop).
    fiberSleepFor(10ms);
    folly::coro::blockingWait(asyncScope.cancelAndJoinAsync());
    peerMgr->markDaemonShutdown();
    sessionMgr->stop();
    sessionMgrThread.join();
    peerMgr->stop();
  });

  fm.addTask([&] {
    XLOG(INFO, "Waiting for adjRibReady baton");
    facebook::bgp::test::boundedBatonWait(adjRibReadyBaton, "adjRibReadyBaton");
    nbrRouteChangeQ_->push(
        NeighborEventMsg(fboss::kDefaultVrf, kPeerId1.peerAddr, false));
    // Sleep so that processNeighborRouteChangeLoop can run
    fiberSleepFor(10ms);
    XLOG(INFO, "Posting nbrDownSent Baton");
    nbrDownSentBaton.post();
  });

  evb.loop();
  peerMgrThread.join();
  SUCCEED();
}

/*
 * Directly call handleNeighborReachabilityMsg on peerMgr
 * and verify log lines and adjRib shut down behavior.
 */
TEST_F(PeerManagerTestFixture, HandleNeighborReachabilityMsgTest) {
  auto dsfPeer = createBgpPeer(
      kPeerAsn1,
      kLocalAddr1,
      kPeerAddr1,
      kNextHopV4_1,
      kNextHopV6_1,
      true /* isPassive */,
      kPeerTypeEdsw);
  auto config = addPeerToConfig(
      getConfig(
          true /* includeStaticPeer */, false /* includeDynamicShivPeer */),
      dsfPeer,
      kPeerGroupNameDsf);
  auto globalConfig = config->getBgpGlobalConfig();

  // create MockPeerManager with the config
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  // create session manager
  auto& evb = peerMgr->getEventBase();
  auto sessionMgr = std::make_shared<MockSessionManager>(
      *globalConfig, false /* enableMessagesOverNotifyQueue */);

  peerMgr->setSessionManager(sessionMgr);

  auto& messages = subscribeToLogMessages("", folly::LogLevel::INFO);

  /*
   * Set up 1 peer (adjRib) without GR.
   * Set up MockAdjRib on peerMgr.
   */
  peerMgr->addPeersToSessionMgr();
  auto adjRib =
      setupMockAdjRib(evb, kPeerId1, AsNum(kAsn1), sessionTerminateBaton_);
  peerMgr->adjRibs_[kPeerId1] = adjRib;
  EXPECT_EQ(1, peerMgr->adjRibs_.size());
  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  EXPECT_NE(vrf->getChangeListTracker(), nullptr);
  // Only one peer's session is stopped.
  EXPECT_CALL(*sessionMgr, stopPeer(kPeerAddr1, false /* withGR */)).Times(1);
  // adjRib should be called once on peerMgr->stop() and nowhere else.
  EXPECT_CALL(*adjRib, stop())
      .Times(1)
      .WillOnce([]() -> folly::coro::Task<void> { co_return; });

  // Notice that session manager can only be run after addPeersToSessionMgr
  auto sessionMgrThread = sessionMgr->runInThread();

  messages.clear();

  // Ensure that session manager could run co_stopPeer
  std::thread evbThread([&]() { evb.loopForever(); });

  // Directly call handleNeighborReachabilityMsg.
  folly::coro::blockingWait(peerMgr->handleNeighborReachabilityMsg());

  /*
   * Check that all peers have been shut down on receipt of
   * NeighborReachabilityMsg.
   * Verify stopping peer log line was printed.
   */
  EXPECT_EQ(2, messages.size());
  EXPECT_TRUE(
      messages[0].first.getMessage().starts_with(
          "Received NeighborReachabilityMsg."));
  EXPECT_TRUE(messages[1].first.getMessage().starts_with("Stopping peer:"));

  folly::LoggerDB::get().getCategory("")->clearHandlers();

  sessionMgr->stop();
  sessionMgrThread.join();

  peerMgr->stop();

  // Release gmock hold on shared_ptr of MockAdjRib
  testing::Mock::VerifyAndClearExpectations(adjRib.get());

  evb.terminateLoopSoon();
  evbThread.join();
}

// If we get NeighborReachabilityMsg, we should stop all neighbors
TEST_F(PeerManagerTestFixture, NeighborReachabilityMsgNoGrTest) {
  auto dsfPeer = createBgpPeer(
      kPeerAsn1,
      kLocalAddr1,
      kPeerAddr1,
      kNextHopV4_1,
      kNextHopV6_1,
      true /* isPassive */,
      kPeerTypeEdsw);
  auto config = addPeerToConfig(
      getConfig(
          true /* includeStaticPeer */, false /* includeDynamicShivPeer */),
      dsfPeer,
      kPeerGroupNameDsf);
  auto globalConfig = config->getBgpGlobalConfig();

  // create MockPeerManager with the config
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  // create session manager
  auto sessionMgr = std::make_shared<MockSessionManager>(
      *globalConfig, false /* enableMessagesOverNotifyQueue */);

  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();
  auto& fm = folly::fibers::getFiberManager(evb, options_);
  folly::coro::CancellableAsyncScope asyncScope;

  auto& messages = subscribeToLogMessages("", folly::LogLevel::INFO);

  /*
   * Set up 2 peers (adjRib) without GR. One is DSF peer
   * and the other is not.
   */
  fm.addTask([&] {
    // Set up MockAdjRib on peerMgr.
    peerMgr->addPeersToSessionMgr();

    auto dsfAdjRib =
        setupMockAdjRib(evb, kPeerId1, AsNum(kAsn1), sessionTerminateBaton_);
    auto adjRib =
        setupMockAdjRib(evb, kPeerId3, AsNum(kAsn3), sessionTerminateBaton_);
    peerMgr->adjRibs_[kPeerId1] = dsfAdjRib;
    peerMgr->adjRibs_[kPeerId3] = adjRib;
    EXPECT_EQ(2, peerMgr->adjRibs_.size());
    // Only one peer's session is stopped.
    EXPECT_CALL(*sessionMgr, stopPeer(kPeerAddr1, false /* withGR */)).Times(1);
    // Each adjRib should be called once on peerMgr->stop() and nowhere else.
    EXPECT_CALL(*dsfAdjRib, stop())
        .Times(1)
        .WillOnce([]() -> folly::coro::Task<void> { co_return; });
    EXPECT_CALL(*adjRib, stop())
        .Times(1)
        .WillOnce([]() -> folly::coro::Task<void> { co_return; });

    // Notice that session manager can only be run after addPeersToSessionMgr
    auto sessionMgrThread = sessionMgr->runInThread();

    messages.clear();

    // Start NeighborRouteChangeLoop coroutine.
    asyncScope.add(
        co_withExecutor(&evb, peerMgr->processNeighborRouteChangeLoop()));
    // Update queue with NeighborReachabilityMsg.
    nbrRouteChangeQ_->push(NeighborReachabilityMsg());
    // Sleep so that processNeighborRouteChangeLoop can run
    fiberSleepFor(10ms);
    /*
     * Check that all peers have been shut down on receipt of
     * NeighborReachabilityMsg.
     * Verify stopping peer log line was printed.
     */
    EXPECT_EQ(2, messages.size());
    EXPECT_TRUE(
        messages[0].first.getMessage().starts_with(
            "Received NeighborReachabilityMsg."));
    EXPECT_TRUE(messages[1].first.getMessage().starts_with("Stopping peer:"));

    /*
     * Remove test log handler before concurrent shutdown to avoid TSAN race
     * between main thread (PeerManagerBase::stop -> logEoRPeers) and session
     * manager thread (FiberBgpPeerManager shutdown logging).
     */
    folly::LoggerDB::get().getCategory("")->clearHandlers();

    // Finish coroutine.
    folly::coro::blockingWait(asyncScope.cancelAndJoinAsync());
    /*
     * Stop sessionMgr before peerMgr to match Main.cpp shutdown sequence.
     * peerMgr->stop() terminates the evb, and sessionMgr->stop() needs
     * the evb alive to destroy FiberServerSocket on the correct thread.
     */
    sessionMgr->stop();
    peerMgr->stop();
    // Release gmock hold on shared_ptr of MockAdjRib
    testing::Mock::VerifyAndClearExpectations(adjRib.get());

    sessionMgrThread.join();
  });

  evb.loop();
  SUCCEED();
}

TEST_F(PeerManagerTestFixture, NeighborReachabilityMsgWithGrTest) {
  auto dsfPeer = createBgpPeer(
      kPeerAsn1,
      kLocalAddr1,
      kPeerAddr1,
      kNextHopV4_1,
      kNextHopV6_1,
      true /* isPassive */,
      kPeerTypeRdsw);
  auto config = addPeerToConfig(
      getConfig(
          true /* includeStaticPeer */, false /* includeDynamicShivPeer */),
      dsfPeer,
      kPeerGroupNameDsf);
  auto globalConfig = config->getBgpGlobalConfig();

  // create MockPeerManager with the config
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  // create session manager
  auto sessionMgr = std::make_shared<MockSessionManager>(
      *globalConfig, false /* enableMessagesOverNotifyQueue */);

  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();
  auto& fm = folly::fibers::getFiberManager(evb, options_);
  folly::coro::CancellableAsyncScope asyncScope;

  auto& messages = subscribeToLogMessages("", folly::LogLevel::INFO);

  /*
   * Set up 2 peers (adjRib) with GR. One is DSF peer and
   * the other is not.
   */
  fm.addTask([&] {
    // Set up MockAdjRib on peerMgr.
    peerMgr->addPeersToSessionMgr();
    auto dsfAdjRib =
        setupMockAdjRib(evb, kPeerId1, AsNum(kAsn1), sessionTerminateBaton_);
    auto adjRib =
        setupMockAdjRib(evb, kPeerId3, AsNum(kAsn3), sessionTerminateBaton_);
    dsfAdjRib->remoteGrRestartTimer_ = folly::AsyncTimeout::schedule(
        std::chrono::milliseconds(200), evb, [&]() noexcept {
          XLOG(INFO, "Remote GR timer fired");
        });
    peerMgr->adjRibs_[kPeerId1] = dsfAdjRib;
    peerMgr->adjRibs_[kPeerId3] = adjRib;
    EXPECT_EQ(2, peerMgr->adjRibs_.size());
    // Only one peer's session is stopped.
    EXPECT_CALL(*sessionMgr, stopPeer(kPeerAddr1, false /* withGR */)).Times(1);
    /*
     * adjRib->cleanupGrState() is called once for dsf GR peer in
     * handleNeighborReachabilityMsg, and adjRib->stop() is called once
     * when peerMgr is stopped.
     */
    EXPECT_CALL(*dsfAdjRib, cleanupGrState(/*isDaemonShutdown=*/false))
        .Times(1)
        .WillOnce([](bool) -> folly::coro::Task<void> { co_return; });
    EXPECT_CALL(*dsfAdjRib, stop())
        .Times(1)
        .WillOnce([]() -> folly::coro::Task<void> { co_return; });
    // adjRib->stop() is called once when peerMgr is stopped.
    EXPECT_CALL(*adjRib, stop())
        .Times(1)
        .WillOnce([]() -> folly::coro::Task<void> { co_return; });

    // Notice that session manager can only be run after addPeersToSessionMgr
    auto sessionMgrThread = sessionMgr->runInThread();

    messages.clear();

    // Start NeighborRouteChangeLoop coroutine.
    asyncScope.add(
        co_withExecutor(&evb, peerMgr->processNeighborRouteChangeLoop()));
    // Update queue with NeighborReachabilityMsg.
    nbrRouteChangeQ_->push(NeighborReachabilityMsg());
    // Sleep so that processNeighborRouteChangeLoop can run
    fiberSleepFor(10ms);
    /*
     * Check that all peers have been shut down on receipt of
     * NeighborReachabilityMsg.
     * Verify stopping peer log line was printed.
     */
    EXPECT_EQ(2, messages.size());
    EXPECT_TRUE(
        messages[0].first.getMessage().starts_with(
            "Received NeighborReachabilityMsg."));
    EXPECT_TRUE(
        messages[1].first.getMessage().starts_with(
            "Stopping peer while peer in GR state:"));

    /*
     * Remove test log handler before concurrent shutdown to avoid TSAN race
     * between main thread (PeerManagerBase::stop -> logEoRPeers) and session
     * manager thread (FiberBgpPeerManager shutdown logging).
     */
    folly::LoggerDB::get().getCategory("")->clearHandlers();

    // Finish coroutine.
    folly::coro::blockingWait(asyncScope.cancelAndJoinAsync());
    /*
     * Stop sessionMgr before peerMgr to match Main.cpp shutdown sequence.
     * peerMgr->stop() terminates the evb, and sessionMgr->stop() needs
     * the evb alive to destroy FiberServerSocket on the correct thread.
     */
    sessionMgr->stop();
    peerMgr->stop();

    // Release gmock hold on shared_ptr of MockAdjRib
    testing::Mock::VerifyAndClearExpectations(adjRib.get());

    sessionMgrThread.join();
  });

  evb.loop();
  SUCCEED();
}

/*
 * 1. Verify that different AdjRibs get their matching router filter statements.
 * 2. Verify that PeerManagerBase sends RibDumpReqs for AdjRibs that receive a
 *    different route filter statement.
 * 3. Verify that AdjRib receiving the same statement is a no-op.
 * 4. Verify that ALL AdjRibs get the same golden prefix policy.
 * 5. Verify the golden prefix policy status when switch limit overload mode is
 *    APPLY_GOLDEN_PREFIX_POLICY.
 */
TEST_F(PeerManagerTestFixture, SetRouteFilterPolicyTest) {
  auto config = getConfig(
      true,
      true,
      false,
      false,
      false,
      true,
      kDefaultEorTimeS,
      false,
      true /* enable switch level limit */,
      true /* enable golden prefixes policy */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  EXPECT_FALSE(peerMgr->goldenPrefixesPolicyActive_);

  auto& evb = peerMgr->getEventBase();

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib1->peeringParams_.description = "adjRib1";
  adjRib1->isSafeModeOn_ = std::make_shared<std::atomic<bool>>(false);
  adjRib1->adjRibOutGroup_ =
      std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "Group1");

  auto adjRib2 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId2,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib2->peeringParams_.description = "adjRib2";
  adjRib2->isSafeModeOn_ = std::make_shared<std::atomic<bool>>(false);
  adjRib2->adjRibOutGroup_ =
      std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "Group2");

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  peerMgr->adjRibs_[kPeerId2] = adjRib2;

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  rib_policy::TRouteFilterPolicy tPolicy;
  tPolicy.statements()->emplace("adjRib1", createTRouteFilterStatement({}));
  tPolicy.statements()->emplace(
      "adjRib2", createTRouteFilterStatement({}, true /* permissive */));

  // Version counter for the policy to ensure each update is accepted
  int64_t policyVersion = 1;

  {
    // set a different route filter statement per adjrib
    tPolicy.version() = policyVersion++;
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    peerMgr->setRouteFilterPolicy(std::move(policy));

    /*
     * expect both adjribs get different route filter statements
     * expect both adjribs to have null golden prefix policy
     */
    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_NE(nullptr, adjRib1->routeFilterStmt_);
        EXPECT_EVENTUALLY_EQ(nullptr, adjRib1->goldenPrefixPolicy_);
        EXPECT_EQ(
            tPolicy.statements()->at("adjRib1"),
            adjRib1->routeFilterStmt_->toThrift());
      });
    });

    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_NE(nullptr, adjRib2->routeFilterStmt_);
        EXPECT_EVENTUALLY_EQ(nullptr, adjRib2->routeFilterLogger_);
        EXPECT_EQ(
            tPolicy.statements()->at("adjRib2"),
            adjRib2->routeFilterStmt_->toThrift());
      });
    });
    EXPECT_NE(
        tPolicy.statements()->at("adjRib2"),
        tPolicy.statements()->at("adjRib1"));

    EXPECT_FALSE(peerMgr->goldenPrefixesPolicyActive_);
  }
  {
    // set a non-null golden prefix policy
    tPolicy.golden_prefix_policy() = createTGoldenPrefixPolicy(
        {}, 2 /* maxSubnets */, {32} /* allowedMaskLengths*/);
    tPolicy.version() = policyVersion++;
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    EXPECT_NE(nullptr, policy->getGoldenPrefixPolicy());
    peerMgr->setRouteFilterPolicy(std::move(policy));

    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_NE(nullptr, adjRib1->goldenPrefixPolicy_);
        EXPECT_EVENTUALLY_NE(nullptr, adjRib2->goldenPrefixPolicy_);
      });
    });
    EXPECT_EQ(adjRib1->goldenPrefixPolicy_, adjRib2->goldenPrefixPolicy_);
    EXPECT_TRUE(peerMgr->goldenPrefixesPolicyActive_);
  }
  {
    // remove golden prefix policy
    tPolicy.golden_prefix_policy().reset();
    tPolicy.version() = policyVersion++;
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    peerMgr->setRouteFilterPolicy(std::move(policy));

    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_EQ(nullptr, adjRib1->goldenPrefixPolicy_);
        EXPECT_EVENTUALLY_EQ(nullptr, adjRib2->goldenPrefixPolicy_);
      });
    });
    EXPECT_FALSE(peerMgr->goldenPrefixesPolicyActive_);
  }
  {
    // verify that in safe mode we don't update golden prefix policy
    adjRib1->setSafeModeOn();
    adjRib2->setSafeModeOn();

    // set a non-null golden prefix policy
    tPolicy.golden_prefix_policy() = createTGoldenPrefixPolicy(
        {}, 2 /* maxSubnets */, {32} /* allowedMaskLengths */);
    tPolicy.version() = policyVersion++;
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    EXPECT_NE(nullptr, policy->getGoldenPrefixPolicy());
    peerMgr->setRouteFilterPolicy(std::move(policy));

    REPEAT({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EQ(nullptr, adjRib1->goldenPrefixPolicy_);
        EXPECT_EQ(nullptr, adjRib2->goldenPrefixPolicy_);
      });
    });
    EXPECT_FALSE(peerMgr->goldenPrefixesPolicyActive_);
  }
  {
    // remove one statement from previous policy
    tPolicy.statements()->erase("adjRib1");
    tPolicy.version() = policyVersion++;
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    peerMgr->setRouteFilterPolicy(std::move(policy));

    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_EQ(nullptr, adjRib1->routeFilterStmt_);
        EXPECT_EVENTUALLY_NE(nullptr, adjRib2->routeFilterStmt_);
      });
    });

    EXPECT_EQ(
        tPolicy.statements()->at("adjRib2"),
        adjRib2->routeFilterStmt_->toThrift());
  }
  {
    // add an irrelevant statement from previous policy
    tPolicy.statements()->emplace("adjRib3", createTRouteFilterStatement({}));
    tPolicy.version() = policyVersion++;
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    peerMgr->setRouteFilterPolicy(std::move(policy));

    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_EQ(nullptr, adjRib1->routeFilterStmt_);
        EXPECT_EVENTUALLY_NE(nullptr, adjRib2->routeFilterStmt_);
      });
    });
    EXPECT_EQ(
        tPolicy.statements()->at("adjRib2"),
        adjRib2->routeFilterStmt_->toThrift());
  }
  {
    // purge all
    peerMgr->setRouteFilterPolicy(nullptr);
    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_EQ(nullptr, adjRib1->routeFilterStmt_);
        EXPECT_EVENTUALLY_EQ(nullptr, adjRib2->routeFilterStmt_);
      });
    });
    EXPECT_FALSE(peerMgr->goldenPrefixesPolicyActive_);
  }

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/*
 * This test verifies that setRouteFilterPolicy() ignores policy updates
 * when the new policy's version is not higher than the cached policy's version.
 * - Policy with higher version should be applied
 * - Policy with same version should be ignored
 * - Policy with lower version should be ignored
 */
TEST_F(PeerManagerTestFixture, SetRouteFilterPolicyVersionCheckTest) {
  auto config = getConfig(
      true,
      true,
      false,
      false,
      false,
      true,
      kDefaultEorTimeS,
      false,
      true /* enable switch level limit */,
      true /* enable golden prefixes policy */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib1->peeringParams_.description = "adjRib1";
  adjRib1->isSafeModeOn_ = std::make_shared<std::atomic<bool>>(false);
  adjRib1->adjRibOutGroup_ =
      std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "Group1");

  peerMgr->adjRibs_[kPeerId1] = adjRib1;

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  // Set initial policy with version 10
  {
    rib_policy::TRouteFilterPolicy tPolicy;
    tPolicy.statements()->emplace("adjRib1", createTRouteFilterStatement({}));
    tPolicy.version() = 10;
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    peerMgr->setRouteFilterPolicy(std::move(policy));

    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_NE(nullptr, adjRib1->routeFilterStmt_);
        EXPECT_EQ(
            tPolicy.statements()->at("adjRib1"),
            adjRib1->routeFilterStmt_->toThrift());
      });
    });

    // Verify the cached policy version
    evb.runInEventBaseThreadAndWait([&]() {
      EXPECT_NE(nullptr, peerMgr->routeFilterPolicy_);
      EXPECT_EQ(10, peerMgr->routeFilterPolicy_->getVersion());
    });
  }

  // Try to set policy with lower version (9) - should be ignored
  {
    rib_policy::TRouteFilterPolicy tPolicy;
    tPolicy.statements()->emplace(
        "adjRib1", createTRouteFilterStatement({}, true /* permissive */));
    tPolicy.version() = 9;
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    peerMgr->setRouteFilterPolicy(std::move(policy));

    // Wait for the event to be processed and verify policy was NOT updated
    REPEAT({
      evb.runInEventBaseThreadAndWait([&]() {
        // Version should still be 10
        EXPECT_EQ(10, peerMgr->routeFilterPolicy_->getVersion());
      });
    });
  }

  // Same version (10) without forceUpdate - should be accepted
  {
    rib_policy::TRouteFilterPolicy tPolicy;
    tPolicy.statements()->emplace(
        "adjRib1", createTRouteFilterStatement({}, true /* permissive */));
    tPolicy.version() = 10;
    auto expectedStmt = tPolicy.statements()->at("adjRib1");
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    peerMgr->setRouteFilterPolicy(std::move(policy));

    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_EQ(10, peerMgr->routeFilterPolicy_->getVersion());
        EXPECT_EVENTUALLY_EQ(
            expectedStmt,
            peerMgr->routeFilterPolicy_->toThrift().statements()->at(
                "adjRib1"));
      });
    });
  }

  // Set policy with higher version (11) - should be applied
  {
    rib_policy::TRouteFilterPolicy tPolicy;
    tPolicy.statements()->emplace(
        "adjRib1", createTRouteFilterStatement({}, true /* permissive */));
    tPolicy.version() = 11;
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    peerMgr->setRouteFilterPolicy(std::move(policy));

    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        // Version should now be 11
        EXPECT_EVENTUALLY_EQ(11, peerMgr->routeFilterPolicy_->getVersion());
      });
    });
  }

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

TEST_F(PeerManagerTestFixture, IdenticalRouteFilterPolicyPreservesPendingWork) {
  auto config = getConfig(
      true,
      true,
      false,
      false,
      false,
      true,
      kDefaultEorTimeS,
      false,
      true /* enable switch level limit */,
      true /* enable golden prefixes policy */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();
  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  auto adjRib = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib->peeringParams_.description = "adjRib1";
  adjRib->isSafeModeOn_ = std::make_shared<std::atomic<bool>>(false);
  adjRib->adjRibOutGroup_ =
      std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "Group1");
  peerMgr->adjRibs_[kPeerId1] = adjRib;

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  rib_policy::TRouteFilterPolicy tPolicy;
  tPolicy.statements()->emplace("adjRib1", createTRouteFilterStatement({}));
  tPolicy.version() = 10;

  auto processingEntered = std::make_shared<folly::coro::Baton>();
  auto processingRelease = std::make_shared<folly::coro::Baton>();
  peerMgr->testOnlyDeferPolicyUpdateProcessing(
      processingEntered, processingRelease);

  evb.runInEventBaseThreadAndWait([&]() {
    peerMgr->routeFilterPolicy_ = std::make_unique<RouteFilterPolicy>(tPolicy);
    adjRib->setRouteFilterStatement(
        peerMgr->routeFilterPolicy_->getStatements().at("adjRib1"));

    // Model reevaluation scheduled by the first CRF write.
    adjRib->pendingIngressPolicyUpdate_ = true;
    adjRib->egressPolicyUpdateRequired_ = true;
    auto* installedPolicy = peerMgr->routeFilterPolicy_.get();

    peerMgr->applyRouteFilterPolicy(
        std::make_unique<RouteFilterPolicy>(tPolicy),
        /*forceUpdate=*/false);

    EXPECT_EQ(installedPolicy, peerMgr->routeFilterPolicy_.get());
    EXPECT_TRUE(adjRib->isPendingIngressPolicyUpdate());
    EXPECT_TRUE(adjRib->isEgressPolicyUpdateRequired());
  });

  // An identical update must return before scheduling another coroutine.
  evb.runInEventBaseThreadAndWait([&]() {
    EXPECT_EQ(
        processingEntered, peerMgr->testOnlyPolicyUpdateProcessingEntered_);
    EXPECT_EQ(
        processingRelease, peerMgr->testOnlyPolicyUpdateProcessingRelease_);
    peerMgr->testOnlyPolicyUpdateProcessingEntered_.reset();
    peerMgr->testOnlyPolicyUpdateProcessingRelease_.reset();
  });

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
}

/*
 * This test verifies that setRouteFilterPolicy() with forceUpdate=true
 * bypasses the version check and applies the policy even when the new
 * version is the same as the cached version. This is used by FILE_MODE
 * (setCrfPolicyFromFile) where the version may not increment.
 */
TEST_F(PeerManagerTestFixture, SetRouteFilterPolicyForceUpdateTest) {
  auto config = getConfig(
      true,
      true,
      false,
      false,
      false,
      true,
      kDefaultEorTimeS,
      false,
      true /* enable switch level limit */,
      true /* enable golden prefixes policy */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib1->peeringParams_.description = "adjRib1";
  adjRib1->isSafeModeOn_ = std::make_shared<std::atomic<bool>>(false);
  adjRib1->adjRibOutGroup_ =
      std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "Group1");

  peerMgr->adjRibs_[kPeerId1] = adjRib1;

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  // Set initial policy with version 10
  {
    rib_policy::TRouteFilterPolicy tPolicy;
    tPolicy.statements()->emplace("adjRib1", createTRouteFilterStatement({}));
    tPolicy.version() = 10;
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    peerMgr->setRouteFilterPolicy(std::move(policy));

    evb.runInEventBaseThreadAndWait([&]() {
      EXPECT_NE(nullptr, peerMgr->routeFilterPolicy_);
      EXPECT_EQ(10, peerMgr->routeFilterPolicy_->getVersion());
    });
  }

  // Same version (10) without forceUpdate - should be accepted
  {
    rib_policy::TRouteFilterPolicy tPolicy;
    tPolicy.statements()->emplace(
        "adjRib1", createTRouteFilterStatement({}, true /* permissive */));
    tPolicy.version() = 10;
    auto expectedStmt = tPolicy.statements()->at("adjRib1");
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    peerMgr->setRouteFilterPolicy(std::move(policy));

    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_EQ(10, peerMgr->routeFilterPolicy_->getVersion());
        EXPECT_EVENTUALLY_EQ(
            expectedStmt,
            peerMgr->routeFilterPolicy_->toThrift().statements()->at(
                "adjRib1"));
      });
    });
  }

  // Same version (10) with forceUpdate=true - should also be accepted
  {
    rib_policy::TRouteFilterPolicy tPolicy;
    tPolicy.statements()->emplace(
        "adjRib1", createTRouteFilterStatement({}, true /* permissive */));
    tPolicy.version() = 10;
    auto expectedStmt = tPolicy.statements()->at("adjRib1");
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    peerMgr->setRouteFilterPolicy(std::move(policy), /*forceUpdate=*/true);

    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_NE(nullptr, peerMgr->routeFilterPolicy_);
        EXPECT_EVENTUALLY_EQ(10, peerMgr->routeFilterPolicy_->getVersion());
        // Verify the policy content was actually replaced (permissive stmt)
        EXPECT_EVENTUALLY_EQ(
            expectedStmt,
            peerMgr->routeFilterPolicy_->toThrift().statements()->at(
                "adjRib1"));
      });
    });
  }

  /*
   * Lower version (5) with forceUpdate=true - should be accepted
   * This is the primary FILE_MODE scenario (daemon restart, version resets)
   */
  {
    rib_policy::TRouteFilterPolicy tPolicy;
    tPolicy.statements()->emplace("adjRib1", createTRouteFilterStatement({}));
    tPolicy.version() = 5;
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    peerMgr->setRouteFilterPolicy(std::move(policy), /*forceUpdate=*/true);

    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_EQ(5, peerMgr->routeFilterPolicy_->getVersion());
      });
    });
  }

  peerMgr->stop();
  sessionMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/*
 * This test verifies that route filter policies can match against peer group
 * names by setting the key_type field in TRouteFilterPolicy to PEER_GROUP_NAME.
 * It creates different AdjRibs with different peer group names and verifies
 * that they receive their matching route filter statements based on peer group
 * name.
 */
TEST_F(
    PeerManagerTestFixture,
    SetRouteFilterPolicyMatchAgainstPeerGroupNameTest) {
  auto config = getConfig(
      true,
      true,
      false,
      false,
      false,
      true,
      kDefaultEorTimeS,
      false,
      false /* enable switch level limit */,
      false /* enable golden prefixes policy */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib1->peeringParams_.description = "adjRib1";
  adjRib1->peeringParams_.peerGroupName = "PEERGROUP_RSW_FSW_V4";

  auto adjRib2 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId2,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib2->peeringParams_.description = "adjRib2";
  adjRib2->peeringParams_.peerGroupName = "PEERGROUP_FSW_RSW_V4";

  auto adjRib3 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId3,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib3->peeringParams_.description = "adjRib3";
  adjRib3->peeringParams_.peerGroupName = "PEERGROUP_RSW_CSW_V4";

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  peerMgr->adjRibs_[kPeerId2] = adjRib2;
  peerMgr->adjRibs_[kPeerId3] = adjRib3;

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  rib_policy::TRouteFilterPolicy tPolicy;
  tPolicy.statements()->emplace(
      "PEERGROUP_RSW_FSW_V4",
      createTRouteFilterStatement(
          {}, false /* permissive */, false /* egress */));
  tPolicy.statements()->emplace(
      "PEERGROUP_FSW_RSW_V4",
      createTRouteFilterStatement(
          {}, true /* permissive */, false /* egress */));
  tPolicy.key_type() = rib_policy::KeyType::PEER_GROUP_NAME;

  {
    // set a different route filter statement per peer group
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    peerMgr->setRouteFilterPolicy(std::move(policy));

    /*
     * expect both adjribs get different route filter statements based on peer
     * group name
     */
    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_NE(nullptr, adjRib1->routeFilterStmt_);
        EXPECT_EQ(
            tPolicy.statements()->at("PEERGROUP_RSW_FSW_V4"),
            adjRib1->routeFilterStmt_->toThrift());
      });
    });

    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_NE(nullptr, adjRib2->routeFilterStmt_);
        EXPECT_EQ(
            tPolicy.statements()->at("PEERGROUP_FSW_RSW_V4"),
            adjRib2->routeFilterStmt_->toThrift());
      });
    });

    // adjRib3 should not have a route filter statement
    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait(
          [&]() { EXPECT_EQ(nullptr, adjRib3->routeFilterStmt_); });
    });

    // Verify the statements are different for adjRib1 and adjRib2
    EXPECT_NE(
        tPolicy.statements()->at("PEERGROUP_RSW_FSW_V4"),
        tPolicy.statements()->at("PEERGROUP_FSW_RSW_V4"));
  }

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/**
 * Test route filter policy updates affecting both ingress and egress
 * processing.
 *
 * Test Flow:
 * 1. Setup: Create 3 AdjRibs in different peer groups (2 affected, 1
 * unaffected)
 * 2. Apply policy with both ingress/egress filters for 2 peer groups
 * 3. Verify: Policy applied correctly + async operations completed (steady
 * state)
 * 4. Re-apply same policy → Verify: No-op, remains in steady state
 * 5. Apply different policy → Verify: New policy applied + steady state reached
 *
 */
TEST_F(
    PeerManagerDynamicPolicyEvaluationFixture,
    SetRouteFilterPolicyForPeerGroupIngressEgressTest) {
  // Enable dynamic policy evaluation
  SetUp(true /* enableDynamicPolicyEvaluation */);
  auto configManager = std::make_shared<ConfigManager>(config_);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config_->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_);
  adjRib1->peeringParams_.description = "adjRib1";
  adjRib1->peeringParams_.peerGroupName = "PEERGROUP_RSW_FSW_V4";

  auto adjRib2 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId2,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_);
  adjRib2->peeringParams_.description = "adjRib2";
  adjRib2->peeringParams_.peerGroupName = "PEERGROUP_FSW_RSW_V4";

  auto adjRib3 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId3,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_);
  adjRib3->peeringParams_.description = "adjRib3";
  adjRib3->peeringParams_.peerGroupName = "PEERGROUP_RSW_CSW_V4";

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  peerMgr->adjRibs_[kPeerId2] = adjRib2;
  peerMgr->adjRibs_[kPeerId3] = adjRib3;

  adjRib1->markStateEstablished();
  adjRib2->markStateEstablished();
  adjRib3->markStateEstablished();

  // Add a stale entry to adjRib1
  auto dummyPathId = 5;
  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>>
      adjRibStaleTreeEntry;
  adjRibStaleTreeEntry.emplace(
      dummyPathId, std::make_unique<AdjRibEntry>(dummyPathId));
  adjRib1->adjRibInStale_.insert(
      kV4Prefix1.first, kV4Prefix1.second, std::move(adjRibStaleTreeEntry));

  // Add an entry to adjRib2
  auto dummyPathId1 = 0;
  auto pfx2Entry = std::make_unique<AdjRibEntry>(dummyPathId1);
  auto pfx2Path = std::make_shared<BgpPath>();
  pfx2Path->setNexthop(kV4Nexthop1);
  pfx2Entry->setPreIn(pfx2Path);
  adjRib2->adjRibInLiteTree_.insert(
      kV4Prefix2.first, kV4Prefix2.second, std::move(pfx2Entry));

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  // Version counter for the policy to ensure each update is accepted
  int64_t policyVersion = 1;

  // Create a new policy that affects both ingress and egress processing
  rib_policy::TRouteFilterPolicy policy1;
  policy1.statements()->emplace(
      "PEERGROUP_RSW_FSW_V4",
      createTRouteFilterStatementWithIngressAndEgressFilters(
          {kV4Prefix1}, /* ingressPrefixes */
          {kV4Prefix5}, /* egressPrefixes */
          false, /* ingressPermissive */
          false /* egressPermissive */));
  policy1.statements()->emplace(
      "PEERGROUP_FSW_RSW_V4",
      createTRouteFilterStatementWithIngressAndEgressFilters(
          {kV4Prefix2}, /* ingressPrefixes */
          {kV4Prefix6}, /* egressPrefixes */
          true, /* ingressPermissive */
          true /* egressPermissive */));
  policy1.key_type() = rib_policy::KeyType::PEER_GROUP_NAME;
  policy1.version() = policyVersion++;

  /*
   * Expected final state: all pending flags should be false after async
   * operations complete
   */
  std::vector<AdjRibPolicyUpdateState> steadyStates = {
      {adjRib1, false, false}, // adjRib1: no pending flags
      {adjRib2, false, false}, // adjRib2: no pending flags
      {adjRib3, false, false} // adjRib3: no pending flags
  };

  // Apply the new policy and wait for async operations to complete
  auto newPolicy = std::make_unique<RouteFilterPolicy>(policy1);
  peerMgr->setRouteFilterPolicy(std::move(newPolicy));

  // Verify final state: async operations completed and policy correctly applied
  verifyStateWithRetries(evb, steadyStates);
  verifyRouteFilterStatement(
      evb,
      {
          {adjRib1, policy1.statements()->at("PEERGROUP_RSW_FSW_V4")},
          {adjRib2, policy1.statements()->at("PEERGROUP_FSW_RSW_V4")},
      });

  // Verify counters for ingress and egress policy affected peers
  EXPECT_EQ(
      2,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kIngressRouteFilterPolicyAffectedPeers));
  EXPECT_EQ(
      2,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kEgressRouteFilterPolicyAffectedPeers));

  // Verify per-peer-group processing time stats were emitted for ingress
  {
    fb303::ThreadCachedServiceData::get()->publishStats();
    auto peerGroup1Key = fmt::format(
        BgpStats::kIngressRouteFilterPeerGroupProcessTimeMs,
        kEbbPlatform,
        kBgpcppTag,
        "PEERGROUP_RSW_FSW_V4");
    auto peerGroup2Key = fmt::format(
        BgpStats::kIngressRouteFilterPeerGroupProcessTimeMs,
        kEbbPlatform,
        kBgpcppTag,
        "PEERGROUP_FSW_RSW_V4");
    EXPECT_GE(
        fb303::ThreadCachedServiceData::get()->getCounter(
            peerGroup1Key + ".p50.60"),
        0);
    EXPECT_GE(
        fb303::ThreadCachedServiceData::get()->getCounter(
            peerGroup2Key + ".p50.60"),
        0);
  }

  // Verify last all-peers re-evaluation time counter was set
  EXPECT_GE(
      fb303::ThreadCachedServiceData::get()->getCounter(
          fmt::format(
              BgpStats::kIngressPolicyAllPeersLastReEvaluationTimeMs,
              kBgpcppTag)),
      0);

  // Update with the same policy again - should remain in steady state
  auto samePolicyPtr = std::make_unique<RouteFilterPolicy>(policy1);
  peerMgr->setRouteFilterPolicy(std::move(samePolicyPtr));
  verifyStateWithRetries(evb, steadyStates);

  // Update policy with different prefixes
  rib_policy::TRouteFilterPolicy policy2;
  policy2.statements()->emplace(
      "PEERGROUP_RSW_FSW_V4",
      createTRouteFilterStatementWithIngressAndEgressFilters(
          {kV4Prefix3}, /* ingressPrefixes */
          {kV4Prefix7}, /* egressPrefixes */
          true, /* ingressPermissive */
          true /* egressPermissive */));
  policy2.statements()->emplace(
      "PEERGROUP_FSW_RSW_V4",
      createTRouteFilterStatementWithIngressAndEgressFilters(
          {kV4Prefix4}, /* ingressPrefixes */
          {kV4Prefix1}, /* egressPrefixes */
          false, /* ingressPermissive */
          false /* egressPermissive */));
  policy2.key_type() = rib_policy::KeyType::PEER_GROUP_NAME;
  policy2.version() = policyVersion++;

  auto policy2Ptr = std::make_unique<RouteFilterPolicy>(policy2);
  peerMgr->setRouteFilterPolicy(std::move(policy2Ptr));

  /*
   * Verify final state: async operations completed and new policy correctly
   * applied
   */
  verifyStateWithRetries(evb, steadyStates);
  verifyRouteFilterStatement(
      evb,
      {
          {adjRib1, policy2.statements()->at("PEERGROUP_RSW_FSW_V4")},
          {adjRib2, policy2.statements()->at("PEERGROUP_FSW_RSW_V4")},
      });

  /*
   * Verify counters for ingress and egress policy affected peers with new
   * policy
   */
  EXPECT_EQ(
      2,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kIngressRouteFilterPolicyAffectedPeers));
  EXPECT_EQ(
      2,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kEgressRouteFilterPolicyAffectedPeers));

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/**
 * Test route filter policy updates affecting only egress processing.
 *
 * Test Flow:
 * 1. Setup: Create 3 AdjRibs in different peer groups (2 affected, 1
 * unaffected)
 * 2. Apply egress-only policy for 2 peer groups (triggers RibDumpReq)
 * 3. Verify: Policy applied correctly + async operations completed (steady
 * state)
 * 4. Re-apply same policy → Verify: No-op, remains in steady state
 * 5. Apply different egress policy → Verify: New policy applied + steady state
 * reached
 *
 * Validates that dynamic policy evaluation correctly handles egress-only route
 * filters and reaches consistent final state after async RibDumpReq processing.
 */
TEST_F(
    PeerManagerDynamicPolicyEvaluationFixture,
    SetRouteFilterPolicyForPeerGroupEgressTest) {
  // Enable dynamic policy evaluation
  SetUp(true /* enableDynamicPolicyEvaluation */);
  auto configManager = std::make_shared<ConfigManager>(config_);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config_->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_);
  adjRib1->peeringParams_.description = "adjRib1";
  adjRib1->peeringParams_.peerGroupName = "PEERGROUP_RSW_FSW_V4";

  auto adjRib2 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId2,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_);
  adjRib2->peeringParams_.description = "adjRib2";
  adjRib2->peeringParams_.peerGroupName = "PEERGROUP_FSW_RSW_V4";

  auto adjRib3 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId3,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_);
  adjRib3->peeringParams_.description = "adjRib3";
  adjRib3->peeringParams_.peerGroupName = "PEERGROUP_RSW_CSW_V4";

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  peerMgr->adjRibs_[kPeerId2] = adjRib2;
  peerMgr->adjRibs_[kPeerId3] = adjRib3;

  adjRib1->markStateEstablished();
  adjRib2->markStateEstablished();
  adjRib3->markStateEstablished();

  /*
   * Mark adjRibs as not in initial announcement to make them eligible for
   * ribDumpReq
   */
  adjRib1->resetInInitialAnnouncement();
  adjRib2->resetInInitialAnnouncement();
  adjRib3->resetInInitialAnnouncement();

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  /*
   * Expected final state: all pending flags should be false after async
   * operations complete
   */
  std::vector<AdjRibPolicyUpdateState> steadyStates = {
      {adjRib1, false, false}, // adjRib1: no pending flags
      {adjRib2, false, false}, // adjRib2: no pending flags
      {adjRib3, false, false} // adjRib3: no pending flags
  };

  // Version counter for the policy to ensure each update is accepted
  int64_t egressPolicyVersion = 1;

  // Create a policy that affects only egress processing
  rib_policy::TRouteFilterPolicy egressPolicy1;
  egressPolicy1.statements()->emplace(
      "PEERGROUP_RSW_FSW_V4", createTRouteFilterStatement({kV4Prefix3}));
  egressPolicy1.statements()->emplace(
      "PEERGROUP_FSW_RSW_V4", createTRouteFilterStatement({kV4Prefix4}));
  egressPolicy1.key_type() = rib_policy::KeyType::PEER_GROUP_NAME;
  egressPolicy1.version() = egressPolicyVersion++;

  // Apply policy and wait for async operations to complete
  auto egressPolicy1Ptr = std::make_unique<RouteFilterPolicy>(egressPolicy1);
  peerMgr->setRouteFilterPolicy(std::move(egressPolicy1Ptr));

  // Verify final state: async operations completed and policy correctly applied
  verifyStateWithRetries(evb, steadyStates);
  verifyRouteFilterStatement(
      evb,
      {
          {adjRib1, egressPolicy1.statements()->at("PEERGROUP_RSW_FSW_V4")},
          {adjRib2, egressPolicy1.statements()->at("PEERGROUP_FSW_RSW_V4")},
      });

  // Verify counter for ingress and egress policy affected peers
  EXPECT_EQ(
      0,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kIngressRouteFilterPolicyAffectedPeers));
  EXPECT_EQ(
      2,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kEgressRouteFilterPolicyAffectedPeers));

  // Apply the same policy again - should remain in steady state
  auto samePolicyPtr = std::make_unique<RouteFilterPolicy>(egressPolicy1);
  peerMgr->setRouteFilterPolicy(std::move(samePolicyPtr));
  verifyStateWithRetries(evb, steadyStates);

  // Update policy with different prefixes
  rib_policy::TRouteFilterPolicy egressPolicy2;
  egressPolicy2.statements()->emplace(
      "PEERGROUP_RSW_FSW_V4", createTRouteFilterStatement({kV4Prefix5}));
  egressPolicy2.statements()->emplace(
      "PEERGROUP_FSW_RSW_V4", createTRouteFilterStatement({kV4Prefix6}));
  egressPolicy2.key_type() = rib_policy::KeyType::PEER_GROUP_NAME;
  egressPolicy2.version() = egressPolicyVersion++;

  auto egressPolicy2Ptr = std::make_unique<RouteFilterPolicy>(egressPolicy2);
  peerMgr->setRouteFilterPolicy(std::move(egressPolicy2Ptr));

  /*
   * Verify final state: async operations completed and new policy correctly
   * applied
   */
  verifyStateWithRetries(evb, steadyStates);
  verifyRouteFilterStatement(
      evb,
      {
          {adjRib1, egressPolicy2.statements()->at("PEERGROUP_RSW_FSW_V4")},
          {adjRib2, egressPolicy2.statements()->at("PEERGROUP_FSW_RSW_V4")},
      });

  // Verify counter for ingress and egress policy affected peers with new policy
  EXPECT_EQ(
      0,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kIngressRouteFilterPolicyAffectedPeers));
  EXPECT_EQ(
      2,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kEgressRouteFilterPolicyAffectedPeers));

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/**
 * Test route filter policy updates affecting only ingress processing.
 *
 * Test Flow:
 * 1. Setup: Create 3 AdjRibs in different peer groups (2 affected, 1
 * unaffected)
 * 2. Apply ingress-only policy for 2 peer groups (triggers re-evaluation)
 * 3. Verify: Policy applied correctly + async operations completed (steady
 * state)
 * 4. Re-apply same policy → Verify: No-op, remains in steady state
 * 5. Apply different ingress policy → Verify: New policy applied + steady state
 * reached
 *
 * Validates that dynamic policy evaluation correctly handles ingress-only route
 * filters and reaches consistent final state after async re-evaluation
 * processing.
 */
TEST_F(
    PeerManagerDynamicPolicyEvaluationFixture,
    SetRouteFilterPolicyForPeerGroupIngressTest) {
  // Enable dynamic policy evaluation
  SetUp(true /* enableDynamicPolicyEvaluation */);
  auto configManager = std::make_shared<ConfigManager>(config_);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config_->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_);
  adjRib1->peeringParams_.description = "adjRib1";
  adjRib1->peeringParams_.peerGroupName = "PEERGROUP_RSW_FSW_V4";

  auto adjRib2 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId2,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_);
  adjRib2->peeringParams_.description = "adjRib2";
  adjRib2->peeringParams_.peerGroupName = "PEERGROUP_FSW_RSW_V4";

  auto adjRib3 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId3,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_);
  adjRib3->peeringParams_.description = "adjRib3";
  adjRib3->peeringParams_.peerGroupName = "PEERGROUP_RSW_CSW_V4";

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  peerMgr->adjRibs_[kPeerId2] = adjRib2;
  peerMgr->adjRibs_[kPeerId3] = adjRib3;

  adjRib1->markStateEstablished();
  adjRib2->markStateEstablished();
  adjRib3->markStateEstablished();

  // Add a stale entry to adjRib1
  auto dummyPathId = 5;
  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>>
      adjRibStaleTreeEntry;
  adjRibStaleTreeEntry.emplace(
      dummyPathId, std::make_unique<AdjRibEntry>(dummyPathId));
  adjRib1->adjRibInStale_.insert(
      kV4Prefix1.first, kV4Prefix1.second, std::move(adjRibStaleTreeEntry));

  // Add an entry to adjRib2
  auto dummyPathId1 = 0;
  auto pfx2Entry = std::make_unique<AdjRibEntry>(dummyPathId1);
  auto pfx2Path = std::make_shared<BgpPath>();
  pfx2Path->setNexthop(kV4Nexthop1);
  pfx2Entry->setPreIn(pfx2Path);
  adjRib2->adjRibInLiteTree_.insert(
      kV4Prefix2.first, kV4Prefix2.second, std::move(pfx2Entry));

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  /*
   * Expected final state: all pending flags should be false after async
   * operations complete
   */
  std::vector<AdjRibPolicyUpdateState> steadyStates = {
      {adjRib1, false, false}, // adjRib1: no pending flags
      {adjRib2, false, false}, // adjRib2: no pending flags
      {adjRib3, false, false} // adjRib3: no pending flags
  };

  // Version counter for the policy to ensure each update is accepted
  int64_t ingressPolicyVersion = 1;

  // Create a policy that affects only ingress processing
  rib_policy::TRouteFilterPolicy ingressPolicy1;
  ingressPolicy1.statements()->emplace(
      "PEERGROUP_RSW_FSW_V4",
      createTRouteFilterStatement(
          {kV4Prefix3}, false /* permissive */, false /* egress */));
  ingressPolicy1.statements()->emplace(
      "PEERGROUP_FSW_RSW_V4",
      createTRouteFilterStatement(
          {kV4Prefix4}, false /* permissive */, false /* egress */));
  ingressPolicy1.key_type() = rib_policy::KeyType::PEER_GROUP_NAME;
  ingressPolicy1.version() = ingressPolicyVersion++;

  // Apply policy and wait for async operations to complete
  auto ingressPolicy1Ptr = std::make_unique<RouteFilterPolicy>(ingressPolicy1);
  peerMgr->setRouteFilterPolicy(std::move(ingressPolicy1Ptr));

  // Verify final state: async operations completed and policy correctly applied
  verifyStateWithRetries(evb, steadyStates);
  verifyRouteFilterStatement(
      evb,
      {
          {adjRib1, ingressPolicy1.statements()->at("PEERGROUP_RSW_FSW_V4")},
          {adjRib2, ingressPolicy1.statements()->at("PEERGROUP_FSW_RSW_V4")},
      });

  // Verify counter for ingress and egress policy affected peers
  EXPECT_EQ(
      2,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kIngressRouteFilterPolicyAffectedPeers));
  EXPECT_EQ(
      0,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kEgressRouteFilterPolicyAffectedPeers));

  // Verify per-peer-group processing time stats were emitted
  {
    fb303::ThreadCachedServiceData::get()->publishStats();
    auto peerGroup1Key = fmt::format(
        BgpStats::kIngressRouteFilterPeerGroupProcessTimeMs,
        kEbbPlatform,
        kBgpcppTag,
        "PEERGROUP_RSW_FSW_V4");
    auto peerGroup2Key = fmt::format(
        BgpStats::kIngressRouteFilterPeerGroupProcessTimeMs,
        kEbbPlatform,
        kBgpcppTag,
        "PEERGROUP_FSW_RSW_V4");
    EXPECT_GE(
        fb303::ThreadCachedServiceData::get()->getCounter(
            peerGroup1Key + ".p50.60"),
        0);
    EXPECT_GE(
        fb303::ThreadCachedServiceData::get()->getCounter(
            peerGroup2Key + ".p50.60"),
        0);
  }

  // Verify last all-peers re-evaluation time counter was set
  EXPECT_GE(
      fb303::ThreadCachedServiceData::get()->getCounter(
          fmt::format(
              BgpStats::kIngressPolicyAllPeersLastReEvaluationTimeMs,
              kBgpcppTag)),
      0);

  // Apply the same policy again - should remain in steady state
  auto samePolicyPtr = std::make_unique<RouteFilterPolicy>(ingressPolicy1);
  peerMgr->setRouteFilterPolicy(std::move(samePolicyPtr));
  verifyStateWithRetries(evb, steadyStates);

  // Update policy with different prefixes
  rib_policy::TRouteFilterPolicy ingressPolicy2;
  ingressPolicy2.statements()->emplace(
      "PEERGROUP_RSW_FSW_V4",
      createTRouteFilterStatement(
          {kV4Prefix5}, true /* permissive */, false /* egress */));
  ingressPolicy2.statements()->emplace(
      "PEERGROUP_FSW_RSW_V4",
      createTRouteFilterStatement(
          {kV4Prefix6}, true /* permissive */, false /* egress */));
  ingressPolicy2.key_type() = rib_policy::KeyType::PEER_GROUP_NAME;
  ingressPolicy2.version() = ingressPolicyVersion++;

  auto ingressPolicy2Ptr = std::make_unique<RouteFilterPolicy>(ingressPolicy2);
  peerMgr->setRouteFilterPolicy(std::move(ingressPolicy2Ptr));

  /*
   * Verify final state: async operations completed and new policy correctly
   * applied
   */
  verifyStateWithRetries(evb, steadyStates);
  verifyRouteFilterStatement(
      evb,
      {
          {adjRib1, ingressPolicy2.statements()->at("PEERGROUP_RSW_FSW_V4")},
          {adjRib2, ingressPolicy2.statements()->at("PEERGROUP_FSW_RSW_V4")},
      });

  // Verify counter for ingress and egress policy affected peers with new policy
  EXPECT_EQ(
      2,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kIngressRouteFilterPolicyAffectedPeers));
  EXPECT_EQ(
      0,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kEgressRouteFilterPolicyAffectedPeers));

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/*
 * If switch level limit is enabled, but in Drop mode, golden prefix policy
 * should always be inactive.
 */
TEST_F(PeerManagerTestFixture, GoldenPrefixesPolicyStatusTestDropMode) {
  auto config = getConfig(
      true,
      true,
      false,
      false,
      false,
      true,
      kDefaultEorTimeS,
      false,
      true /* enable switch level limit */,
      false /* disable golden prefixes policy */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  EXPECT_FALSE(peerMgr->goldenPrefixesPolicyActive_);
  auto& evb = peerMgr->getEventBase();

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib1->peeringParams_.description = "adjRib1";
  adjRib1->isSafeModeOn_ = std::make_shared<std::atomic<bool>>(false);
  adjRib1->adjRibOutGroup_ =
      std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "Group1");

  auto adjRib2 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId2,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib2->peeringParams_.description = "adjRib2";
  adjRib2->isSafeModeOn_ = std::make_shared<std::atomic<bool>>(false);
  adjRib2->adjRibOutGroup_ =
      std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "Group2");

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  peerMgr->adjRibs_[kPeerId2] = adjRib2;

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  rib_policy::TRouteFilterPolicy tPolicy;
  tPolicy.statements()->emplace("adjRib1", createTRouteFilterStatement({}));
  tPolicy.statements()->emplace(
      "adjRib2", createTRouteFilterStatement({}, true /* permissive */));

  {
    // set a non-null golden prefix policy
    tPolicy.golden_prefix_policy() = createTGoldenPrefixPolicy(
        {}, 2 /* maxSubnets */, {32} /* allowedMaskLengths*/);
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    EXPECT_NE(nullptr, policy->getGoldenPrefixPolicy());
    peerMgr->setRouteFilterPolicy(std::move(policy));

    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_NE(nullptr, adjRib1->goldenPrefixPolicy_);
        EXPECT_EVENTUALLY_NE(nullptr, adjRib2->goldenPrefixPolicy_);
      });
    });
    EXPECT_EQ(adjRib1->goldenPrefixPolicy_, adjRib2->goldenPrefixPolicy_);
    EXPECT_FALSE(peerMgr->goldenPrefixesPolicyActive_);
  }

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/*
 * If switch level limit isn't set, golden prefix policy should always be
 * inactive.
 */
TEST_F(PeerManagerTestFixture, GoldenPrefixesPolicyStatusTestSwitchLimitUnset) {
  auto config = getConfig(
      true,
      true,
      false,
      false,
      false,
      true,
      kDefaultEorTimeS,
      false,
      false /* disable switch level limit*/);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  EXPECT_FALSE(peerMgr->goldenPrefixesPolicyActive_);

  auto& evb = peerMgr->getEventBase();
  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib1->peeringParams_.description = "adjRib1";
  adjRib1->isSafeModeOn_ = std::make_shared<std::atomic<bool>>(false);
  adjRib1->adjRibOutGroup_ =
      std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "Group1");

  auto adjRib2 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId2,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib2->peeringParams_.description = "adjRib2";
  adjRib2->isSafeModeOn_ = std::make_shared<std::atomic<bool>>(false);
  adjRib2->adjRibOutGroup_ =
      std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "Group2");

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  peerMgr->adjRibs_[kPeerId2] = adjRib2;

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  rib_policy::TRouteFilterPolicy tPolicy;
  tPolicy.statements()->emplace("adjRib1", createTRouteFilterStatement({}));
  tPolicy.statements()->emplace(
      "adjRib2", createTRouteFilterStatement({}, true /* permissive */));

  {
    // set a non-null golden prefix policy
    tPolicy.golden_prefix_policy() = createTGoldenPrefixPolicy(
        {}, 2 /* maxSubnets */, {32} /* allowedMaskLengths*/);
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    EXPECT_NE(nullptr, policy->getGoldenPrefixPolicy());
    peerMgr->setRouteFilterPolicy(std::move(policy));

    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_NE(nullptr, adjRib1->goldenPrefixPolicy_);
        EXPECT_EVENTUALLY_NE(nullptr, adjRib2->goldenPrefixPolicy_);
      });
    });
    EXPECT_EQ(adjRib1->goldenPrefixPolicy_, adjRib2->goldenPrefixPolicy_);
    EXPECT_FALSE(peerMgr->goldenPrefixesPolicyActive_);
  }

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/*
 * This test verifies periodic eviction of stale entries in policy Cache which
 * is a singleton object shared across all adjRibs
 */
TEST_F(PeerManagerTestFixture, PolicyCachePeriodicEvictionTest) {
  auto config = getConfig(true, false);
  auto globalConfig = config->getBgpGlobalConfig();

  // set policyCacheSize
  auto policyCache = AdjRibPolicyCache::get();
  policyCache->setCacheSize(2);
  policyCache->setLruClearSize(1);

  // Create MockPeerManager with the config
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  // Create session manager
  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);

  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();

  // Schedule periodic eviction with 1s interval
  auto evictionInterval = std::chrono::seconds(1);
  peerMgr->periodicPolicyCacheEvictionInterval_ = evictionInterval;

  // Start coroutine task
  folly::coro::CancellableAsyncScope asyncScope;
  asyncScope.add(co_withExecutor(
      &evb, peerMgr->startPeriodicPolicyCacheEvictionRoutine()));
  auto& fm = folly::fibers::getFiberManager(evb, options_);

  const PolicyAttributesMask dummyMask1;
  const PolicyAttributesMask dummyMask2;
  std::string kDummyPolicyName = "DummyPolicy";

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  fm.addTask([&] {
    peerMgr->addPeersToSessionMgr();

    // Notice that session manager can only be run after addPeersToSessionMgr
    auto sessionMgrThread = sessionMgr->runInThread();

    // Create two adjRibs
    auto adjRib1 = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId1,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config);
    auto adjRib2 = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId2,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config);

    peerMgr->adjRibs_[kPeerId1] = adjRib1;
    peerMgr->adjRibs_[kPeerId2] = adjRib2;

    // Add 2 cache entries to policy cache
    auto attrs =
        std::make_shared<const BgpPath>(*buildBgpPathFields(4, 4, 4, 4));

    policyCache->addToPolicyCache(
        kDummyPolicyName + "1",
        &dummyMask1,
        kV4Prefix1,
        attrs,
        nullptr /* policyActionData */,
        nullptr /* postAttrsAndTerm */);
    policyCache->addToPolicyCache(
        kDummyPolicyName + "2",
        &dummyMask2,
        kV4Prefix2,
        attrs,
        nullptr /* policyActionData */,
        nullptr /* postAttrsAndTerm */);

    EXPECT_EQ(2, policyCache->size());

    // Wait for 2s to wait for periodic task to kick in
    fiberSleepFor(2s);

    // Expect stale entries to be cleared by policy cache
    EXPECT_EQ(0, policyCache->size());

    // call stop on both adjRibs
    folly::coro::blockingWait(adjRib1->stop());
    folly::coro::blockingWait(adjRib2->stop());

    // Wait for 2s to wait for periodic task to kick in
    fiberSleepFor(2s);

    // Ensure access to policyCache is safe
    EXPECT_NE(nullptr, policyCache);
    EXPECT_EQ(0, policyCache->size());

    /*
     * Stop PeerManagerBase — cancel local coroutines before stop() since
     * stop() terminates the evb and coroutines can't process cancellation
     * on a dead event loop.
     */
    peerMgr->markDaemonShutdown();
    folly::coro::blockingWait(asyncScope.cancelAndJoinAsync());
    sessionMgr->stop();
    peerMgr->stop();

    sessionMgrThread.join();
  });

  evb.loop();
  SUCCEED();
}

// Parameterize tests to toggle safe mode on and off.
class SafeModeTestFixture : public PeerManagerTestFixture,
                            public testing::WithParamInterface<bool> {};

/*
 * When a new AdjRib is created, verify that it gets the golden prefix policy
 * from PeerManagerBase.
 */
TEST_P(SafeModeTestFixture, InitializeAdjRibWithGoldenPrefixPolicy) {
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */);
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);
  *mockPeerMgr->isSafeModeOn_ = GetParam();

  uint64_t version = 0x100;
  auto versionNumber = std::make_shared<VersionNumber>(version);

  auto sessionMgrThread = sessionMgr->runInThread();

  auto& evb = mockPeerMgr->getEventBase();
  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);

  fm.addTask([&] {
    // Set an arbitrary golden prefix policy
    rib_policy::TRouteFilterPolicy tPolicy;
    tPolicy.golden_prefix_policy() = createTGoldenPrefixPolicy(
        {kV4Prefix1}, 2 /* maxSubnets */, {32} /* allowedMaskLengths */);
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    /*
     * Set routeFilterPolicy_ instead of calling setRouteFilterPolicy, which is
     * asynchronous.
     */
    mockPeerMgr->routeFilterPolicy_ = std::move(policy);

    auto sessionInfo = FiberBgpPeer::getObservableSessionInfo(
        mockInfo1_,
        nullptr /* unbounded input queue, unused */,
        sessionMgr->boundedIqueue_,
        sessionMgr->oQueue_,
        versionNumber);

    FiberBgpPeer::ObservableStateT stateEvent{
        .peerId = kPeerId3,
        .versionNumber = version,
        .remoteAs = mockInfo1_.peeringParams.remoteAs,
        .sessionInfo = sessionInfo};

    /*
     * Establish session, which is expected to create a new AdjRib with
     * goldenPrefixPolicy set.
     */
    folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEvent));

    auto adjRib = mockPeerMgr->findAdjRib(kPeerId3);
    EXPECT_NE(nullptr, adjRib->goldenPrefixPolicy_);

    // Teardown
    sessionMgr->oQueue_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEvent));

    mockPeerMgr->stop();
    sessionMgr->stop();
  });
  evb.loop();
  sessionMgrThread.join();
  SUCCEED();
}

INSTANTIATE_TEST_SUITE_P(
    SafeModeTestFixture,
    SafeModeTestFixture,
    testing::Bool() /* isSafeModeOn */);

/*
 * Parameterized on whether update groups are enabled. The non-update-group path
 * clears each statement and issues per-peer rib dumps directly; the
 * update-group path routes through applyRouteFilterPolicy instead.
 */
class ClearRouteFiltersFixture : public PeerManagerTestFixture,
                                 public testing::WithParamInterface<bool> {};

/*
 * Verify that PeerManagerBase clear the Ingress Egress Route Filter statements
 * in each adjrib, and trigger RIB Dump
 */
TEST_P(ClearRouteFiltersFixture, ClearIngressEgressRouteFiltersPolicyTest) {
  const bool enableUpdateGroup = GetParam();
  auto config = getConfig(
      /*includeStaticPeer=*/true,
      /*includeDynamicShivPeer=*/true,
      /*includeDynamicMonitorPeer=*/false,
      /*includeDynamicVipInjectorPeer=*/false,
      /*enableStatefulHa=*/false,
      /*enableVipServer=*/true,
      kDefaultEorTimeS,
      /*enableSubscriberLimit=*/false,
      /*enableSwitchLimit=*/false,
      /*applyGoldenPrefixPolicy=*/false,
      /*bgpFeatures=*/{},
      /*enableDynamicPolicyEvaluation=*/false,
      /*enableUpdateGroup=*/enableUpdateGroup);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config->getBgpGlobalConfig();
  // attach sessionMgr just to allow PeerManagerBase to run
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();
  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib1->peeringParams_.description = "adjRib1";
  adjRib1->isSafeModeOn_ = std::make_shared<std::atomic<bool>>(false);
  adjRib1->adjRibOutGroup_ =
      std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "Group1");

  auto adjRib2 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId2,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib2->peeringParams_.description = "adjRib2";
  adjRib2->isSafeModeOn_ = std::make_shared<std::atomic<bool>>(false);
  adjRib2->adjRibOutGroup_ =
      std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "Group2");

  /*
   * A golden prefix policy is set on each adjRib; clearing the route filters
   * must leave it untouched.
   */
  adjRib1->goldenPrefixPolicy_ = std::make_shared<GoldenPrefixPolicy>(
      createTGoldenPrefixPolicy({kV4Prefix1}, 2 /* maxSubnets */, {32}));
  adjRib2->goldenPrefixPolicy_ = std::make_shared<GoldenPrefixPolicy>(
      createTGoldenPrefixPolicy({kV4Prefix1}, 2 /* maxSubnets */, {32}));

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  peerMgr->adjRibs_[kPeerId2] = adjRib2;

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  rib_policy::TRouteFilterPolicy tPolicy;
  tPolicy.statements()->emplace("adjRib1", createTRouteFilterStatement({}));
  tPolicy.statements()->emplace(
      "adjRib2", createTRouteFilterStatement({}, true /* permissive */));

  {
    // purge all
    peerMgr->clearIngressEgressRouteFiltersPolicy();

    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_EQ(nullptr, adjRib1->routeFilterStmt_);
        EXPECT_EVENTUALLY_EQ(nullptr, adjRib2->routeFilterStmt_);
      });
    });
  }

  // The route filter clear must not touch the golden prefix policy.
  evb.runInEventBaseThreadAndWait([&]() {
    EXPECT_NE(nullptr, adjRib1->goldenPrefixPolicy_);
    EXPECT_NE(nullptr, adjRib2->goldenPrefixPolicy_);
  });

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

INSTANTIATE_TEST_SUITE_P(
    ClearRouteFiltersFixture,
    ClearRouteFiltersFixture,
    testing::Bool() /* enableUpdateGroup */);

/*
 * Set a non-null route filter statement, then clear it. Verify the adjRib's
 * statement is dropped and observe whether the update group key changes.
 * Parameterized on {enableUpdateGroup, includeEgressFilter}: scenario (1) is
 * ingress-only, scenario (2) is ingress + egress.
 */
class ClearRouteFiltersWithStmtFixture
    : public PeerManagerTestFixture,
      public testing::WithParamInterface<std::tuple<bool, bool>> {};

TEST_P(
    ClearRouteFiltersWithStmtFixture,
    ClearRouteFilterStatementUpdatesAdjRibAndKey) {
  const bool enableUpdateGroup = std::get<0>(GetParam());
  const bool includeEgressFilter = std::get<1>(GetParam());
  auto config = getConfig(
      /*includeStaticPeer=*/true,
      /*includeDynamicShivPeer=*/true,
      /*includeDynamicMonitorPeer=*/false,
      /*includeDynamicVipInjectorPeer=*/false,
      /*enableStatefulHa=*/false,
      /*enableVipServer=*/true,
      kDefaultEorTimeS,
      /*enableSubscriberLimit=*/false,
      /*enableSwitchLimit=*/false,
      /*applyGoldenPrefixPolicy=*/false,
      /*bgpFeatures=*/{},
      /*enableDynamicPolicyEvaluation=*/false,
      /*enableUpdateGroup=*/enableUpdateGroup);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();
  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib1->peeringParams_.description = "adjRib1";
  adjRib1->isSafeModeOn_ = std::make_shared<std::atomic<bool>>(false);
  /*
   * Build the initial group key so we can observe whether clearing the route
   * filter changes it.
   */
  adjRib1->buildAndSetUpdateGroupKey();

  peerMgr->adjRibs_[kPeerId1] = adjRib1;

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  rib_policy::TRouteFilterStatement tStmt = includeEgressFilter
      ? createTRouteFilterStatementWithIngressAndEgressFilters(
            {kV4Prefix1}, {kV4Prefix2})
      : createTRouteFilterStatement(
            {kV4Prefix1}, /*permissive=*/false, /*egress=*/false);
  rib_policy::TRouteFilterPolicy tPolicy;
  tPolicy.statements()->emplace("adjRib1", tStmt);
  tPolicy.version() = 1;
  peerMgr->setRouteFilterPolicy(std::make_unique<RouteFilterPolicy>(tPolicy));

  // The adjRib now has a non-null route filter statement.
  WITH_RETRIES({
    evb.runInEventBaseThreadAndWait(
        [&]() { EXPECT_EVENTUALLY_NE(nullptr, adjRib1->routeFilterStmt_); });
  });

  UpdateGroupKey keyBeforeClear;
  evb.runInEventBaseThreadAndWait(
      [&]() { keyBeforeClear = adjRib1->getUpdateGroupKey(); });

  peerMgr->clearIngressEgressRouteFiltersPolicy();

  // The route filter statement is dropped from the adjRib in both paths.
  WITH_RETRIES({
    evb.runInEventBaseThreadAndWait(
        [&]() { EXPECT_EVENTUALLY_EQ(nullptr, adjRib1->routeFilterStmt_); });
  });

  /*
   * The update group key is not derived from the route filter statement (the
   * route filter name is not yet wired into buildUpdateGroupKey), so clearing
   * the filter -- ingress-only or ingress+egress -- leaves the key unchanged.
   */
  evb.runInEventBaseThreadAndWait(
      [&]() { EXPECT_EQ(keyBeforeClear, adjRib1->getUpdateGroupKey()); });

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

INSTANTIATE_TEST_SUITE_P(
    ClearRouteFiltersWithStmtFixture,
    ClearRouteFiltersWithStmtFixture,
    testing::Combine(
        testing::Bool() /* enableUpdateGroup */,
        testing::Bool() /* includeEgressFilter */));

/*
 * Verify that PeerManagerBase clear the Golden Prefixes policy in
 * each adjrib.
 */
TEST_F(PeerManagerTestFixture, ClearGoldenPrefixesPolicyTest) {
  auto config =
      getConfig(true, true, false, false, false, false /* enableVipService */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config->getBgpGlobalConfig();
  // just to allow PeerManagerBase to start
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();
  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib1->peeringParams_.description = "adjRib1";
  adjRib1->isSafeModeOn_ = std::make_shared<std::atomic<bool>>(false);
  adjRib1->adjRibOutGroup_ =
      std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "Group1");

  auto adjRib2 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId2,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib2->peeringParams_.description = "adjRib2";
  adjRib2->isSafeModeOn_ = std::make_shared<std::atomic<bool>>(false);
  adjRib2->adjRibOutGroup_ =
      std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "Group2");

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  peerMgr->adjRibs_[kPeerId2] = adjRib2;

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  rib_policy::TRouteFilterPolicy tPolicy;
  tPolicy.statements()->emplace("adjRib1", createTRouteFilterStatement({}));
  tPolicy.statements()->emplace(
      "adjRib2", createTRouteFilterStatement({}, true /* permissive */));

  {
    // set a non-null golden prefix policy
    tPolicy.golden_prefix_policy() = createTGoldenPrefixPolicy(
        {}, 0 /* maxSubnets (unused) */, {0} /* allowedMaskLengths (unused) */);
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    EXPECT_NE(nullptr, policy->getGoldenPrefixPolicy());
    peerMgr->setRouteFilterPolicy(std::move(policy));

    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_NE(nullptr, adjRib1->goldenPrefixPolicy_);
        EXPECT_EVENTUALLY_NE(nullptr, adjRib2->goldenPrefixPolicy_);
      });
    });
    EXPECT_EQ(adjRib1->goldenPrefixPolicy_, adjRib2->goldenPrefixPolicy_);
  }

  {
    /*
     * purge all
     * one adjrib set safe mode on, all adjribs should have safe mode on
     */
    adjRib1->setSafeModeOn();
    peerMgr->clearGoldenPrefixesPolicy();

    // expect rib dump requests from both adjrib
    EXPECT_EQ(0, ribInQ_.size());

    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_EQ(nullptr, adjRib1->goldenPrefixPolicy_);
        EXPECT_EVENTUALLY_EQ(nullptr, adjRib2->goldenPrefixPolicy_);
      });
    });
  }

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

TEST_F(PeerManagerTestFixture, DisableScubaLoggingTest) {
  FLAGS_disable_route_filter_scuba_logging = true;

  SCOPE_EXIT {
    FLAGS_disable_route_filter_scuba_logging = false;
  };

  auto config =
      getConfig(true, true, false, false, false, false /* enableVipService */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();
  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib1->peeringParams_.description = "adjRib1";
  adjRib1->adjRibOutGroup_ =
      std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "Group1");

  auto adjRib2 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId2,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib2->peeringParams_.description = "adjRib2";
  adjRib2->adjRibOutGroup_ =
      std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "Group2");

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  peerMgr->adjRibs_[kPeerId2] = adjRib2;

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  rib_policy::TRouteFilterPolicy tPolicy;
  tPolicy.statements()->emplace("adjRib1", createTRouteFilterStatement({}));
  tPolicy.statements()->emplace(
      "adjRib2", createTRouteFilterStatement({}, true /* permissive */));

  {
    // set a different route filter statement per adjrib
    auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);
    peerMgr->setRouteFilterPolicy(std::move(policy));

    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_NE(nullptr, adjRib1->routeFilterStmt_);
        EXPECT_EVENTUALLY_EQ(nullptr, adjRib1->routeFilterLogger_);
        EXPECT_EQ(
            tPolicy.statements()->at("adjRib1"),
            adjRib1->routeFilterStmt_->toThrift());
      });
    });

    WITH_RETRIES({
      evb.runInEventBaseThreadAndWait([&]() {
        EXPECT_EVENTUALLY_NE(nullptr, adjRib2->routeFilterStmt_);
        EXPECT_EVENTUALLY_EQ(nullptr, adjRib2->routeFilterLogger_);
        EXPECT_EQ(
            tPolicy.statements()->at("adjRib1"),
            adjRib1->routeFilterStmt_->toThrift());
        EXPECT_EQ(
            tPolicy.statements()->at("adjRib2"),
            adjRib2->routeFilterStmt_->toThrift());
      });
    });

    EXPECT_NE(
        tPolicy.statements()->at("adjRib2"),
        tPolicy.statements()->at("adjRib1"));
  }

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/*
 * This test verifies setting safe mode in one session sets all the sessions in
 * safe mode
 */
TEST_F(PeerManagerTestFixture, SafeModeOnAllSessionsTest) {
  gflags::FlagSaver fs;
  int randomNumber = folly::Random::rand32(); // generate a random integer
  FLAGS_safemode_file =
      "/var/tmp/safemode_test_file_SafeModeOnAllSessionsTest_" +
      std::to_string(randomNumber);
  facebook::fb303::ThreadCachedServiceData::getShared()->zeroStats();
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */);
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);
  uint64_t version = 0x100;
  auto versionNumber = std::make_shared<VersionNumber>(version);

  auto sessionMgrThread = sessionMgr->runInThread();

  auto& evb = mockPeerMgr->getEventBase();
  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);
  fm.addTask([&] {
    mockPeerMgr->getDefaultVrf().setRibInitPathComputationNotified(false);

    EXPECT_FALSE(mockPeerMgr->getIsSafeModeOn());
    /*
     * Step 1:
     * Establish 2 sessions - PeerId3 and PeerId4,
     * both are in initial announcement state
     */
    auto sessionInfo = FiberBgpPeer::getObservableSessionInfo(
        mockInfo1_,
        nullptr /* unbounded input queue, unused */,
        sessionMgr->boundedIqueue_,
        sessionMgr->oQueue_,
        versionNumber);

    FiberBgpPeer::ObservableStateT stateEventPeer3{
        .peerId = kPeerId3,
        .versionNumber = version,
        .remoteAs = mockInfo1_.peeringParams.remoteAs,
        .sessionInfo = sessionInfo};

    folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEventPeer3));
    auto adjRib3 = mockPeerMgr->findAdjRib(kPeerId3);
    EXPECT_TRUE(adjRib3->isStateEstablished());
    EXPECT_TRUE(adjRib3->inInitialAnnouncement());

    fiberSleepFor(20ms);

    FiberBgpPeer::ObservableStateT stateEventPeer4{
        .peerId = kPeerId4,
        .versionNumber = ++version,
        .remoteAs = mockInfo1_.peeringParams.remoteAs,
        .sessionInfo = sessionInfo};

    sessionInfo->currentVersion = std::make_shared<VersionNumber>(version);

    folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEventPeer4));
    auto adjRib4 = mockPeerMgr->findAdjRib(kPeerId4);
    EXPECT_TRUE(adjRib4->isStateEstablished());
    EXPECT_TRUE(adjRib4->inInitialAnnouncement());

    fiberSleepFor(20ms);

    /**
     * Step 2:
     * Set safe mode on one of the sessions
     */
    adjRib3->setSafeModeOn();
    EXPECT_TRUE(mockPeerMgr->getIsSafeModeOn());

    /**
     * Step 3:
     * Expect all the sessions to be in safe mode
     */
    EXPECT_TRUE(adjRib3->isSafeModeOn());
    EXPECT_TRUE(adjRib4->isSafeModeOn());
    EXPECT_TRUE(mockPeerMgr->isSafeModeOn_);

    fiberSleepFor(20ms);

    /**
     * Step 4: Terminate PeerId3 and verify PeerId4 is not affected
     */
    sessionMgr->oQueue_->open();
    sessionMgr->oQueue_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    stateEventPeer3.versionNumber =
        ++version; // Simulate FiberBgpPeerManager increasing version
    sessionInfo->currentVersion = std::make_shared<VersionNumber>(version);
    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEventPeer3));
    EXPECT_FALSE(adjRib3->isStateEstablished());
    EXPECT_TRUE(adjRib4->isSafeModeOn());
    EXPECT_TRUE(mockPeerMgr->isSafeModeOn_);

    fiberSleepFor(20ms);

    /**
     * Step 5:
     * Establish PeerId3 again and verify it comes up in safe mode
     */
    stateEventPeer3.versionNumber = ++version;
    sessionInfo->currentVersion = std::make_shared<VersionNumber>(version);

    folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEventPeer3));
    EXPECT_TRUE(adjRib3->isStateEstablished());
    EXPECT_TRUE(adjRib3->inInitialAnnouncement());
    EXPECT_TRUE(adjRib3->isSafeModeOn());

    fiberSleepFor(20ms);

    /*
     * Step 6:
     * Terminate both to complete the test
     */
    stateEventPeer3.versionNumber = ++version;
    sessionInfo->currentVersion = std::make_shared<VersionNumber>(version);
    sessionMgr->oQueue_->open();
    sessionMgr->oQueue_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEventPeer3));
    fiberSleepFor(10ms);
    EXPECT_FALSE(adjRib3->isStateEstablished());
    EXPECT_FALSE(adjRib3->inInitialAnnouncement());

    stateEventPeer4.versionNumber = ++version;
    sessionInfo->currentVersion = std::make_shared<VersionNumber>(version);
    sessionMgr->oQueue_->open();
    sessionMgr->oQueue_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEventPeer4));
    fiberSleepFor(10ms);
    EXPECT_FALSE(adjRib4->isStateEstablished());
    EXPECT_FALSE(adjRib4->inInitialAnnouncement());

    ASSERT_TRUE(getRunningSessions());
    EXPECT_EQ(0, *getRunningSessions());

    // Explicitly stop the peer manager to cancel all coro tasks
    mockPeerMgr->stop();
    sessionMgr->stop();
  });

  evb.loop();
  sessionMgrThread.join();
  SUCCEED();
}

/*
 * Verify the following safe mode operations when TriggerSafeMode message is
 * received
 * 1. Save Safe mode file
 * 2. Trigger PauseBestPathAndFibProgramming message to RIB
 * 3. Trigger ResumeBestPathAndFibProgramming message to RIB
 */
TEST_F(PeerManagerTestFixture, TriggerSafeModeMsgTest) {
  gflags::FlagSaver fs;
  int randomNumber = folly::Random::rand32(); // generate a random integer
  FLAGS_safemode_file = "/var/tmp/safemode_test_file_TriggerSafeModeMsgTest_" +
      std::to_string(randomNumber);

  auto config = getConfig(true, true);
  auto globalConfig = config->getBgpGlobalConfig();
  // Create MockPeerManager with the config
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  // Create session manager
  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);

  peerMgr->setSessionManager(sessionMgr);

  // create peer manager thread
  auto peerMgrThread = peerMgr->runInThread();

  folly::EventBase evb;
  auto& fm = folly::fibers::getFiberManager(evb, options_);
  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  fm.addTask([&] {
    peerMgr->addPeersToSessionMgr();

    // Create two adjRibs
    auto adjRib1 = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId1,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config);
    auto adjRib2 = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId2,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config);

    peerMgr->adjRibs_[kPeerId1] = adjRib1;
    peerMgr->adjRibs_[kPeerId2] = adjRib2;

    // Trigger SafeMode from peerId1
    AdjRib::ObservableMessageT triggerSafeModeMsg{
        fboss::kDefaultVrf, kPeerId1, AdjRib::TriggerSafeMode{}};
    folly::coro::blockingWait(
        peerMgr->processAdjRibEvent(std::move(triggerSafeModeMsg)));

    // Verify safe mode file exists
    EXPECT_TRUE(boost::filesystem::exists(FLAGS_safemode_file));
    peerMgr->removeSafeModeFile();

    fiberSleepFor(10ms);

    /*
     * Verify PauseBestPathAndFibProgramming and ResumeBestPathAndFibProgramming
     * message is sent to RIB
     */
    WITH_RETRIES(EXPECT_EVENTUALLY_EQ(2, ribInQ_.size()));
    auto ribInQItem0 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(
        std::holds_alternative<PauseBestPathAndFibProgramming>(ribInQItem0));

    auto ribInQItem1 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(
        std::holds_alternative<ResumeBestPathAndFibProgramming>(ribInQItem1));

    // Stop PeerManagerBase
    fiberSleepFor(10ms);
    peerMgr->stop();
  });
  evb.loop();
  peerMgrThread.join();
  SUCCEED();
}

// Verify removeSafeModeFile()
TEST_F(PeerManagerTestFixture, RemoveSafeModeFileTest) {
  gflags::FlagSaver fs;
  int randomNumber = folly::Random::rand32(); // generate a random integer
  FLAGS_safemode_file = "/var/tmp/safemode_test_file_RemoveSafeModeFileTest_" +
      std::to_string(randomNumber);
  try {
    folly::writeFileAtomic(FLAGS_safemode_file, "");
  } catch (const std::exception& ex) {
    XLOGF(
        ERR,
        "Could not create safemode file {}. Exception: {}",
        FLAGS_safemode_file,
        folly::exceptionStr(ex));
  }
  auto config = getConfig(true, true);
  auto globalConfig = config->getBgpGlobalConfig();
  // Create MockPeerManager with the config
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);
  // Create session manager
  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);

  peerMgr->setSessionManager(sessionMgr);

  // create peer manager thread
  auto peerMgrThread = peerMgr->runInThread();

  folly::EventBase evb;
  auto& fm = folly::fibers::getFiberManager(evb, options_);
  fm.addTask([&] {
    // Verify safe mode file exists
    EXPECT_TRUE(boost::filesystem::exists(FLAGS_safemode_file));
    // Verify safe mode is activated
    EXPECT_TRUE(peerMgr->getIsSafeModeOn());
    // remove safe mode file
    peerMgr->removeSafeModeFile();
    // Verify safe mode file is removed
    EXPECT_FALSE(boost::filesystem::exists(FLAGS_safemode_file));
    // Stop PeerManagerBase
    fiberSleepFor(200ms);
    peerMgr->stop();
  });

  evb.loop();
  peerMgrThread.join();

  SUCCEED();
}

/**
 * Verify route refresh request is not sent to peers under the following
 * conditions
 * 1. Bgp is not in initialized state
 * 2. Peer is not in established state
 * 3. Enhanced route refresh request is not present in the negotiated
 * capabilities
 */
TEST_F(PeerManagerTestFixture, TriggerRouteRefreshRequestNegativeTest) {
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */);
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);
  uint64_t version = 0x100;
  auto versionNumber = std::make_shared<VersionNumber>(version);

  auto sessionMgrThread = sessionMgr->runInThread();

  auto& evb = mockPeerMgr->getEventBase();
  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);

  fm.addTask([&] {
    mockPeerMgr->getDefaultVrf().setInitialized(false);

    /*
     * Case 1: Trigger a route refresh request for a peer when BGP is not
     * initialized
     */
    auto failedPeers =
        mockPeerMgr->triggerRouteRefreshRequestsForPeers({kPeerId3});

    EXPECT_EQ(failedPeers.size(), 1);

    /*
     * Case 2: Trigger a route refresh request for a peer when peer is not in
     * established state
     */
    mockPeerMgr->getDefaultVrf().setRibInitPathComputationNotified(true);
    mockPeerMgr->getDefaultVrf().setEorTimerExpired(false);
    mockPeerMgr->getDefaultVrf().setInitialized(true);
    mockPeerMgr->getDefaultVrf().setRibInitialAnnouncementStarted(true);
    mockPeerMgr->getDefaultVrf().setRibInitialAnnouncementDone(true);

    failedPeers = mockPeerMgr->triggerRouteRefreshRequestsForPeers({kPeerId3});

    EXPECT_EQ(failedPeers.size(), 1);

    /*
     * Case 3: Trigger a route refresh request for a peer when enhanced route
     * refresh is not present in the negotiated capabilities
     */
    mockInfo1_.negotiatedCapabilities.enhancedRouteRefresh() = false;

    auto sessionInfo = FiberBgpPeer::getObservableSessionInfo(
        mockInfo1_,
        nullptr /* unbounded input queue, unused */,
        sessionMgr->boundedIqueue_,
        sessionMgr->oQueue_,
        versionNumber);

    FiberBgpPeer::ObservableStateT stateEvent{
        .peerId = kPeerId3,
        .versionNumber = version,
        .remoteAs = mockInfo1_.peeringParams.remoteAs,
        .sessionInfo = sessionInfo};

    folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEvent));
    auto adjRib3 = mockPeerMgr->findAdjRib(kPeerId3);
    EXPECT_TRUE(adjRib3->isStateEstablished());
    fiberSleepFor(20ms);
    failedPeers = mockPeerMgr->triggerRouteRefreshRequestsForPeers({kPeerId3});
    EXPECT_EQ(failedPeers.size(), 1);

    sessionMgr->oQueue_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    stateEvent.versionNumber = ++version;
    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEvent));

    // Explicitly stop the peer manager to cancel all coro tasks
    mockPeerMgr->stop();
    sessionMgr->stop();
  });

  evb.loop();
  sessionMgrThread.join();
  SUCCEED();
}

TEST_F(PeerManagerTestFixture, TriggerRouteRefreshRequestTest) {
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */);
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);
  uint64_t version = 0x100;
  auto versionNumber = std::make_shared<VersionNumber>(version);

  auto sessionMgrThread = sessionMgr->runInThread();

  auto& evb = mockPeerMgr->getEventBase();
  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);

  mockInfo1_.negotiatedCapabilities.routeRefresh() = true;
  mockInfo1_.negotiatedCapabilities.enhancedRouteRefresh() = false;
  mockInfo1_.negotiatedCapabilities.mpExtV4Unicast() = true;

  fm.addTask([&] {
    auto sessionInfo = FiberBgpPeer::getObservableSessionInfo(
        mockInfo1_,
        nullptr /* unbounded input queue, unused */,
        sessionMgr->boundedIqueue_,
        sessionMgr->oQueue_,
        versionNumber);

    FiberBgpPeer::ObservableStateT stateEvent{
        .peerId = kPeerId3,
        .versionNumber = version,
        .remoteAs = mockInfo1_.peeringParams.remoteAs,
        .sessionInfo = sessionInfo};

    folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEvent));
    auto adjRib3 = mockPeerMgr->findAdjRib(kPeerId3);
    EXPECT_TRUE(adjRib3->isStateEstablished());
    EXPECT_TRUE(adjRib3->isRouteRefreshNegotiated());
    EXPECT_FALSE(adjRib3->isEnhancedRouteRefreshNegotiated());

    mockPeerMgr->getDefaultVrf().setRibInitPathComputationNotified(true);
    mockPeerMgr->getDefaultVrf().setEorTimerExpired(false);
    mockPeerMgr->getDefaultVrf().setInitialized(true);
    mockPeerMgr->getDefaultVrf().setRibInitialAnnouncementStarted(true);
    mockPeerMgr->getDefaultVrf().setRibInitialAnnouncementDone(true);

    /*
     * No sleep needed: every gate input is set synchronously on this fiber,
     * and triggerRouteRefreshRequestsForPeers runs synchronously on the same
     * fiber. Per general_rules.md: "MUST never use sleep-based
     * synchronization."
     */
    auto failedPeers =
        mockPeerMgr->triggerRouteRefreshRequestsForPeers({kPeerId3});

    EXPECT_TRUE(failedPeers.empty());
    EXPECT_EQ(sessionMgr->boundedIqueue_->size(), 1);
    if (!sessionMgr->boundedIqueue_->empty()) {
      const auto routeRefresh = facebook::bgp::test::boundedBlockingPop(
          *sessionMgr->boundedIqueue_, "boundedIqueue_");
      EXPECT_TRUE(std::holds_alternative<BgpRouteRefresh>(*routeRefresh));
      if (std::holds_alternative<BgpRouteRefresh>(*routeRefresh)) {
        EXPECT_EQ(
            BgpUpdateAfi::AFI_IPv4,
            std::get<BgpRouteRefresh>(*routeRefresh).afi().value());
      }
    }

    while (!sessionMgr->boundedIqueue_->isBlocked()) {
      if (!sessionMgr->boundedIqueue_->push(nullptr)) {
        ADD_FAILURE() << "boundedIqueue_ became full before it was blocked";
        break;
      }
    }
    failedPeers = mockPeerMgr->triggerRouteRefreshRequestsForPeers({kPeerId3});
    EXPECT_EQ(failedPeers, std::vector<BgpPeerId>{kPeerId3});

    sessionMgr->oQueue_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    stateEvent.versionNumber = ++version;
    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEvent));

    // Explicitly stop the peer manager to cancel all coro tasks
    mockPeerMgr->stop();
    sessionMgr->stop();
  });

  evb.loop();
  sessionMgrThread.join();
  SUCCEED();
}

/**
 * Verify numStreamSubscribers() functionality.
 */
TEST_F(StreamSubscriberFixture, NumStreamSubscribersTest) {
  // Initial setup.
  SetUp(true /* configureMonitorPeer */, true /* initialAnnouncementDone */);
  auto& evb = peerMgr->getEventBase();

  const std::unique_ptr<std::string> subscriber1 =
      std::make_unique<std::string>("subscriber1");
  auto stream1 = peerMgr->subscribe(subscriber1);
  EXPECT_EQ(1, peerMgr->numStreamSubscribers());

  const std::unique_ptr<std::string> subscriber2 =
      std::make_unique<std::string>("subscriber2");
  auto stream2 = peerMgr->subscribe(subscriber2);
  EXPECT_EQ(2, peerMgr->numStreamSubscribers());

  auto subscription1 =
      std::move(stream1).toClientStreamUnsafeDoNotUse().subscribeExTry(
          &evb, [](auto&&) {
            // Do nothing
          });
  // Close channel on client side so that we close stream1.
  subscription1.cancel();
  std::move(subscription1).detach();

  const BgpPeerId kStreamPeerId1{kStreamPeerAddr, 1};
  auto terminateBaton = peerMgr->sessionTerminateBatons_[kStreamPeerId1];
  // Wait for session to terminate.
  folly::coro::blockingWait(
      [&]() -> folly::coro::Task<void> { co_await *terminateBaton; }());
  EXPECT_EQ(
      TBgpPeerState::IDLE, peerMgr->streamSubscribers_.at(*subscriber1).state);
  EXPECT_EQ(1, peerMgr->numStreamSubscribers());

  auto subscription2 =
      std::move(stream2).toClientStreamUnsafeDoNotUse().subscribeExTry(
          &evb, [](auto&&) {
            // Do nothing
          });
  // Close channel on client side so that we close stream2.
  subscription2.cancel();
  std::move(subscription2).detach();

  const BgpPeerId kStreamPeerId2{kStreamPeerAddr, 2};
  terminateBaton = peerMgr->sessionTerminateBatons_[kStreamPeerId2];
  // Wait for session to terminate.
  folly::coro::blockingWait(
      [&]() -> folly::coro::Task<void> { co_await *terminateBaton; }());
  EXPECT_EQ(
      TBgpPeerState::IDLE, peerMgr->streamSubscribers_.at(*subscriber2).state);
  EXPECT_EQ(0, peerMgr->numStreamSubscribers());
  XLOG(INFO, "Done");
}

/**
 * Verify exceedsStreamSubscriberLimit() functionality.
 */
TEST_F(StreamSubscriberFixture, ExceedsStreamSubscriberLimitTest) {
  // Initial setup.
  SetUp(
      true /* configureMonitorPeer */,
      true /* initialAnnouncementDone */,
      true);
  auto& evb = peerMgr->getEventBase();
  EXPECT_FALSE(peerMgr->exceedsStreamSubscriberLimit());

  const std::unique_ptr<std::string> subscriber1 =
      std::make_unique<std::string>("subscriber1");
  auto stream1 = peerMgr->subscribe(subscriber1);
  EXPECT_EQ(1, peerMgr->numStreamSubscribers());
  EXPECT_TRUE(peerMgr->exceedsStreamSubscriberLimit());

  const std::unique_ptr<std::string> subscriber2 =
      std::make_unique<std::string>("subscriber2");

  try {
    peerMgr->subscribe(subscriber2);
    FAIL() << "Expected TBgpServiceException";
  } catch (const TBgpServiceException& e) {
    EXPECT_STREQ(
        "Subscription failed: Max stream subscribers reached", e.what());
  }
  EXPECT_EQ(1, peerMgr->numStreamSubscribers());
  EXPECT_FALSE(peerMgr->streamSubscribers_.contains(*subscriber2));
  EXPECT_TRUE(peerMgr->exceedsStreamSubscriberLimit());

  auto subscription1 =
      std::move(stream1).toClientStreamUnsafeDoNotUse().subscribeExTry(
          &evb, [](auto&&) {
            // Do nothing
          });
  // Close channel on client side so that we close stream1.
  subscription1.cancel();
  std::move(subscription1).detach();

  const BgpPeerId kStreamPeerId1{kStreamPeerAddr, 1};
  auto terminateBaton = peerMgr->sessionTerminateBatons_[kStreamPeerId1];
  // Wait for session to terminate.
  folly::coro::blockingWait(
      [&]() -> folly::coro::Task<void> { co_await *terminateBaton; }());
  EXPECT_EQ(
      TBgpPeerState::IDLE, peerMgr->streamSubscribers_.at(*subscriber1).state);
  EXPECT_EQ(0, peerMgr->numStreamSubscribers());
  EXPECT_FALSE(peerMgr->exceedsStreamSubscriberLimit());

  auto stream2 = peerMgr->subscribe(subscriber2);
  EXPECT_EQ(1, peerMgr->numStreamSubscribers());
  EXPECT_TRUE(peerMgr->exceedsStreamSubscriberLimit());

  auto subscription2 =
      std::move(stream2).toClientStreamUnsafeDoNotUse().subscribeExTry(
          &evb, [](auto&&) {
            // Do nothing
          });
  // Close channel on client side so that we close stream2.
  subscription2.cancel();
  std::move(subscription2).detach();

  const BgpPeerId kStreamPeerId2{kStreamPeerAddr, 2};
  terminateBaton = peerMgr->sessionTerminateBatons_[kStreamPeerId2];
  // Wait for session to terminate.
  folly::coro::blockingWait(
      [&]() -> folly::coro::Task<void> { co_await *terminateBaton; }());
  EXPECT_EQ(
      TBgpPeerState::IDLE, peerMgr->streamSubscribers_.at(*subscriber2).state);
  EXPECT_EQ(0, peerMgr->numStreamSubscribers());
  EXPECT_FALSE(peerMgr->exceedsStreamSubscriberLimit());

  XLOG(INFO, "Done");
}

/**
 * @brief A config-supplied stream_subscriber_limit is honored: the device
 * admits exactly that many subscribers and rejects the next one.
 *
 * @details Test steps:
 * 1. Set up with the limit delivered through the bgp_setting_config field.
 * 2. Subscribe up to the limit; each is admitted.
 * 3. Assert the next subscription is rejected.
 */
TEST_F(StreamSubscriberFixture, ConfiguredStreamSubscriberLimitTest) {
  /*
   * Arbitrary representative value. The implementation does not branch on the
   * number, so the FE (3) and BE (2) values need no separate coverage.
   */
  constexpr uint32_t kLimit = 3;
  /*
   * The limit arrives through the bgp_setting_config field, so this exercises
   * the real config path rather than a directly-poked BgpGlobalConfig.
   */
  SetUp(
      true /* configureMonitorPeer */,
      true /* initialAnnouncementDone */,
      true /* enableSubscriberLimit */,
      kLimit);

  EXPECT_FALSE(peerMgr->exceedsStreamSubscriberLimit());

  /*
   * The streams must outlive the loop. A destroyed stream ends its session,
   * which frees the slot and would let the over-limit subscription through.
   */
  std::vector<apache::thrift::ServerStream<TBgpRouteDelta>> streams;
  for (uint32_t i = 0; i < kLimit; ++i) {
    const auto name = std::make_unique<std::string>(fmt::format("sub{}", i));
    streams.emplace_back(peerMgr->subscribe(name));
    EXPECT_EQ(i + 1, peerMgr->numStreamSubscribers());
  }
  EXPECT_TRUE(peerMgr->exceedsStreamSubscriberLimit());

  /*
   * subscribe() rejects synchronously. It runs the admission check inside
   * evb_.runInEventBaseThreadAndWait(), so the throw reaches this thread
   * before subscribe() returns. No baton, counter poll or retry is needed.
   */
  const auto overLimit = std::make_unique<std::string>("subOverLimit");
  EXPECT_THROW(peerMgr->subscribe(overLimit), TBgpServiceException);
  EXPECT_EQ(kLimit, peerMgr->numStreamSubscribers());
  EXPECT_FALSE(peerMgr->streamSubscribers_.contains(*overLimit));
}

/**
 * @brief With the stream_subscriber_limit feature off, the device still
 * enforces the default limit instead of admitting every subscriber.
 *
 * @details Test steps:
 * 1. Set up through the real config path with the feature off and no value.
 * 2. Assert the resolved limit is set.
 * 3. Subscribe up to that limit; each is admitted.
 * 4. Assert the next subscription is rejected.
 */
TEST_F(StreamSubscriberFixture, DefaultStreamSubscriberLimitTest) {
  SetUp(
      true /* configureMonitorPeer */,
      true /* initialAnnouncementDone */,
      false /* enableSubscriberLimit */);

  const auto limit =
      config_->getBgpGlobalConfig()->streamSubscriberLimit.value_or(0);
  ASSERT_GT(limit, 0);
  EXPECT_FALSE(peerMgr->exceedsStreamSubscriberLimit());

  /*
   * The streams must outlive the loop. A destroyed stream ends its session,
   * which frees the slot and would let the over-limit subscription through.
   */
  std::vector<apache::thrift::ServerStream<TBgpRouteDelta>> streams;
  for (uint32_t i = 0; i < limit; ++i) {
    const auto name = std::make_unique<std::string>(fmt::format("sub{}", i));
    streams.emplace_back(peerMgr->subscribe(name));
  }
  EXPECT_EQ(limit, peerMgr->numStreamSubscribers());
  EXPECT_TRUE(peerMgr->exceedsStreamSubscriberLimit());

  const auto overLimit = std::make_unique<std::string>("subOverLimit");
  EXPECT_THROW(peerMgr->subscribe(overLimit), TBgpServiceException);
  EXPECT_EQ(limit, peerMgr->numStreamSubscribers());
  EXPECT_FALSE(peerMgr->streamSubscribers_.contains(*overLimit));
}

/**
 * @brief A subscriber that drops while the device is at its configured limit
 * can reconnect into the slot it freed.
 *
 * @details Test steps:
 * 1. Set up with a limit of 2 and fill both slots.
 * 2. Drop the first subscriber and wait for its session to terminate.
 * 3. Assert the device is no longer at the limit.
 * 4. Re-subscribe under the same name and assert it is admitted.
 */
TEST_F(StreamSubscriberFixture, ReconnectAfterDisconnectAtLimitTest) {
  constexpr uint32_t kLimit = 2;
  SetUp(
      true /* configureMonitorPeer */,
      true /* initialAnnouncementDone */,
      true /* enableSubscriberLimit */,
      kLimit);
  auto& evb = peerMgr->getEventBase();

  /*
   * Hold the names alongside the streams. subscribe() keys streamSubscribers_
   * by name, and the reconnect below must present the same name to take the
   * re-subscribe path rather than insert a new subscriber.
   */
  std::vector<std::unique_ptr<std::string>> names;
  std::vector<apache::thrift::ServerStream<TBgpRouteDelta>> streams;
  for (uint32_t i = 0; i < kLimit; ++i) {
    names.emplace_back(std::make_unique<std::string>(fmt::format("sub{}", i)));
    streams.emplace_back(peerMgr->subscribe(names.back()));
  }
  EXPECT_EQ(kLimit, peerMgr->numStreamSubscribers());
  EXPECT_TRUE(peerMgr->exceedsStreamSubscriberLimit());

  /*
   * Drop the first subscriber by cancelling its client-side subscription.
   * toClientStreamUnsafeDoNotUse is deprecated in favour of
   * ScopedServerInterfaceThread, but the other 12 stream subscriber tests in
   * this file cancel a subscription the same way. Migrating one call site
   * alone would spin up a real server and change the threading model of the
   * teardown path under test, so the whole file should move together.
   *
   * The deprecated call is pulled out of the chain onto its own line because
   * @lint-ignore only suppresses the line directly below it, and in chained
   * form the diagnostic is reported on the .toClientStream... line.
   */
  // @lint-ignore CLANGTIDY clang-diagnostic-deprecated-declarations
  // @lint-ignore CLANGTIDY facebook-hte-Deprecated
  auto clientStream = std::move(streams[0]).toClientStreamUnsafeDoNotUse();
  auto subscription =
      std::move(clientStream).subscribeExTry(&evb, [](auto&&) {});
  subscription.cancel();
  std::move(subscription).detach();

  /*
   * Read the peer id from the subscriber entry rather than assuming the first
   * subscriber got id 1, and use at() so a missing baton fails the test
   * instead of default-inserting a null shared_ptr that would be dereferenced.
   */
  const auto& firstSubscriber = peerMgr->streamSubscribers_.at(*names[0]);
  ASSERT_TRUE(
      peerMgr->sessionTerminateBatons_.contains(firstSubscriber.peerId));
  auto terminateBaton =
      peerMgr->sessionTerminateBatons_.at(firstSubscriber.peerId);
  ASSERT_NE(nullptr, terminateBaton);
  folly::coro::blockingWait(
      [&]() -> folly::coro::Task<void> { co_await *terminateBaton; }());

  /*
   * The entry survives the disconnect in IDLE state, so the slot is free even
   * though streamSubscribers_ still contains the name.
   */
  EXPECT_EQ(
      TBgpPeerState::IDLE, peerMgr->streamSubscribers_.at(*names[0]).state);
  EXPECT_EQ(kLimit - 1, peerMgr->numStreamSubscribers());
  EXPECT_FALSE(peerMgr->exceedsStreamSubscriberLimit());

  /*
   * Reusing the same name is what makes this distinct from
   * ExceedsStreamSubscriberLimitTest, which only ever gives a freed slot to a
   * name that has never connected. This takes the re-subscribe path instead,
   * reusing the retained IDLE entry.
   */
  auto reconnected = peerMgr->subscribe(names[0]);
  EXPECT_EQ(kLimit, peerMgr->numStreamSubscribers());
  EXPECT_TRUE(peerMgr->exceedsStreamSubscriberLimit());
  EXPECT_NE(
      TBgpPeerState::IDLE, peerMgr->streamSubscribers_.at(*names[0]).state);
}

/**
 * @brief A subscriber whose connection dies without the server noticing can
 * re-subscribe while the device is at its limit, because a re-subscribe under
 * the same name occupies no new slot.
 *
 * @details Test steps:
 * 1. Set up with a limit of 2 and fill both slots.
 * 2. Assert the first subscriber is still ESTABLISHED.
 * 3. Re-subscribe under the same name and assert it is admitted.
 * 4. Assert the subscriber count is unchanged.
 */
TEST_F(StreamSubscriberFixture, ResubscribeAtLimitTest) {
  constexpr uint32_t kLimit = 2;
  SetUp(
      true /* configureMonitorPeer */,
      true /* initialAnnouncementDone */,
      true /* enableSubscriberLimit */,
      kLimit);

  std::vector<std::unique_ptr<std::string>> names;
  std::vector<apache::thrift::ServerStream<TBgpRouteDelta>> streams;
  for (uint32_t i = 0; i < kLimit; ++i) {
    names.emplace_back(std::make_unique<std::string>(fmt::format("sub{}", i)));
    streams.emplace_back(peerMgr->subscribe(names.back()));
  }
  EXPECT_EQ(kLimit, peerMgr->numStreamSubscribers());
  EXPECT_TRUE(peerMgr->exceedsStreamSubscriberLimit());

  /*
   * Still ESTABLISHED is what a network blackhole looks like from the server:
   * no FIN or RST arrives, so nothing tells the peer manager the client is
   * gone and the entry keeps counting toward numStreamSubscribers(). Every
   * other disconnect drives the entry to IDLE and frees the slot on its own.
   */
  EXPECT_EQ(
      TBgpPeerState::ESTABLISHED,
      peerMgr->streamSubscribers_.at(*names[0]).state);

  /*
   * Same name and address, so this takes over instead of needing a slot. The
   * returned stream has to outlive the count check: dropping it tears the
   * subscription down again, which races the assertion and can show the slot
   * already freed.
   */
  apache::thrift::ServerStream<TBgpRouteDelta> reconnected =
      peerMgr->subscribe(names[0]);
  EXPECT_EQ(kLimit, peerMgr->numStreamSubscribers());
}

/**
 * @brief At the limit, a name collision is reported as a duplicate rather
 * than as a capacity problem.
 *
 * @details Test steps:
 * 1. Set up with a limit of 1 and fill the only slot.
 * 2. Point the peer manager at a different stream peer address.
 * 3. Subscribe again under the same name.
 * 4. Assert the error names the duplicate, not the limit.
 */
TEST_F(StreamSubscriberFixture, DuplicateAtLimitReportsDuplicateTest) {
  SetUp(
      true /* configureMonitorPeer */,
      true /* initialAnnouncementDone */,
      true /* enableSubscriberLimit */,
      1 /* streamSubscriberLimit */);

  const auto name = std::make_unique<std::string>("sub0");
  auto stream = peerMgr->subscribe(name);
  EXPECT_EQ(1, peerMgr->numStreamSubscribers());
  EXPECT_TRUE(peerMgr->exceedsStreamSubscriberLimit());

  /*
   * Same name from a different address is a collision, not a re-subscribe.
   * The limit is skipped whenever an ESTABLISHED entry exists, so this error
   * survives; checking the limit first would mask it with "Max stream
   * subscribers reached" and send the operator after capacity instead of
   * after two hosts sharing a name. At a BE limit of 2 that masking would be
   * the usual outcome, since the device sits at its limit normally.
   */
  peerMgr->streamPeerAddr_ = folly::IPAddress("::2");

  try {
    peerMgr->subscribe(name);
    FAIL() << "expected the duplicate subscription to be rejected";
  } catch (const TBgpServiceException& e) {
    EXPECT_STREQ(
        "Subscription failed: duplicate subscription attempted", e.what());
  }
}

namespace {

/*
 * The number of milliseconds after which the peer manager ends the session of
 * a subscriber whose egress queue stays blocked. The tests below set the
 * gflag to this value.
 */
constexpr int32_t kTestIdleTimeoutMs = 5000;

/*
 * The number of milliseconds by which a test moves the start of a block into
 * the past. The value is above kTestIdleTimeoutMs, so the block counts as too
 * long. A test moves the recorded time instead of a sleep. Thus the test is
 * deterministic and it is fast.
 */
constexpr uint64_t kTestElapsedMs = kTestIdleTimeoutMs + 1000;

/*
 * Push messages into the bounded egress queue of a subscriber until the queue
 * reaches its high watermark. Then the queue reports the blocked state. The
 * caller must run on the peer-manager EventBase.
 */
void fillEgressQueueToHighWatermark(StreamSubscriber& subscriber) {
  for (size_t i = 0; i < kEgressQueueHighWatermark; ++i) {
    subscriber.peerInputQ->push(BgpEndOfRib{});
  }
}

/*
 * Move the recorded start of the block and the recorded start of the run of
 * no progress further into the past. A test uses this instead of a sleep, so
 * the test is deterministic and fast. The subtraction stops at 0, because the
 * values are unsigned. The caller must run on the peer-manager EventBase.
 */
void ageBlockStart(StreamSubscriber& subscriber, uint64_t elapsedMs) {
  const uint64_t blockStart = subscriber.blockStartTimeMs.value_or(0);
  subscriber.blockStartTimeMs =
      blockStart > elapsedMs ? blockStart - elapsedMs : 0;
  const uint64_t noProgressStart = subscriber.noProgressStartTimeMs.value_or(0);
  subscriber.noProgressStartTimeMs =
      noProgressStart > elapsedMs ? noProgressStart - elapsedMs : 0;
}

/*
 * Stand in for the generator of the subscriber. The generator takes a message
 * out of the queue and then raises the count of the consumed messages. The
 * caller must run on the peer-manager EventBase.
 */
void consumeOneEgressMessage(StreamSubscriber& subscriber) {
  subscriber.peerInputQ->get();
  subscriber.sessionState->consumedCount.fetch_add(
      1, std::memory_order_relaxed);
}

} // namespace

/*
 * A client of a bounded stream subscriber can hold its stream open and can
 * give no thrift stream credit. Then the generator of that subscriber waits
 * at its co_yield and only thrift can resume it. The peer manager must end
 * such a session after FLAGS_stream_subscriber_idle_timeout_ms and must free
 * the slot of the subscriber.
 */
TEST_F(StreamSubscriberFixture, StreamSubscriberIdleTimeoutReclaimsSession) {
  gflags::FlagSaver flags;
  FLAGS_stream_subscriber_idle_timeout_ms = kTestIdleTimeoutMs;

  SetUp(true /* configureMonitorPeer */, true /* initialAnnouncementDone */);
  auto& evb = peerMgr->getEventBase();

  const std::unique_ptr<std::string> subscriberName =
      std::make_unique<std::string>("subscriber1");
  /*
   * The test keeps the stream and never subscribes a client to it. Therefore
   * the generator never gets credit and never starts. This is the state that
   * the timeout must reclaim.
   */
  auto stream = peerMgr->subscribe(subscriberName);
  EXPECT_EQ(1, peerMgr->numStreamSubscribers());

  /*
   * All of the work below runs in one hop on the peer-manager EventBase.
   * reportStreamSubscriberStatsRoutine() also runs on that EventBase, so one
   * hop keeps the periodic task out of the middle of the sequence.
   */
  evb.runInEventBaseThreadAndWait([&]() {
    auto& subscriber = peerMgr->streamSubscribers_.at(*subscriberName);
    EXPECT_NE(nullptr, subscriber.peerInputQ);

    fillEgressQueueToHighWatermark(subscriber);
    EXPECT_TRUE(subscriber.peerInputQ->isBlocked());

    // The first sample records the start of the block.
    peerMgr->reportStreamSubscriberBackpressureStats();
    EXPECT_TRUE(subscriber.blockStartTimeMs.has_value());
    EXPECT_EQ(TBgpPeerState::ESTABLISHED, subscriber.state);

    // The next sample sees a block that is longer than the limit.
    ageBlockStart(subscriber, kTestElapsedMs);
    peerMgr->reportStreamSubscriberBackpressureStats();

    EXPECT_EQ(TBgpPeerState::IDLE, subscriber.state);
    EXPECT_FALSE(subscriber.blockStartTimeMs.has_value());

    /*
     * The reclaimed session no longer holds a slot of
     * streamSubscriberLimit. The entry of the subscriber stays, because the
     * client still holds the stream.
     */
    EXPECT_EQ(0, peerMgr->numStreamSubscribers());
    EXPECT_TRUE(peerMgr->streamSubscribers_.contains(*subscriberName));
  });

  XLOG(INFO, "Done");
}

/*
 * The value 0 switches the reclamation off. Then a block of any length keeps
 * the session.
 */
TEST_F(StreamSubscriberFixture, StreamSubscriberIdleTimeoutDisabled) {
  gflags::FlagSaver flags;
  FLAGS_stream_subscriber_idle_timeout_ms = 0;

  SetUp(true /* configureMonitorPeer */, true /* initialAnnouncementDone */);
  auto& evb = peerMgr->getEventBase();

  const std::unique_ptr<std::string> subscriberName =
      std::make_unique<std::string>("subscriber1");
  auto stream = peerMgr->subscribe(subscriberName);
  EXPECT_EQ(1, peerMgr->numStreamSubscribers());

  evb.runInEventBaseThreadAndWait([&]() {
    auto& subscriber = peerMgr->streamSubscribers_.at(*subscriberName);
    fillEgressQueueToHighWatermark(subscriber);
    EXPECT_TRUE(subscriber.peerInputQ->isBlocked());

    peerMgr->reportStreamSubscriberBackpressureStats();
    EXPECT_TRUE(subscriber.blockStartTimeMs.has_value());

    ageBlockStart(subscriber, kTestElapsedMs);
    peerMgr->reportStreamSubscriberBackpressureStats();

    EXPECT_EQ(TBgpPeerState::ESTABLISHED, subscriber.state);
    EXPECT_TRUE(subscriber.blockStartTimeMs.has_value());
    EXPECT_EQ(1, peerMgr->numStreamSubscribers());
  });

  XLOG(INFO, "Done");
}

/*
 * A client that reads slowly but makes progress takes the queue below the low
 * watermark. That break clears the record of the block. Therefore the length
 * of the next block starts again from 0 and the session survives.
 */
TEST_F(StreamSubscriberFixture, StreamSubscriberIdleTimeoutDrainedQueue) {
  gflags::FlagSaver flags;
  FLAGS_stream_subscriber_idle_timeout_ms = kTestIdleTimeoutMs;

  SetUp(true /* configureMonitorPeer */, true /* initialAnnouncementDone */);
  auto& evb = peerMgr->getEventBase();

  const std::unique_ptr<std::string> subscriberName =
      std::make_unique<std::string>("subscriber1");
  auto stream = peerMgr->subscribe(subscriberName);
  EXPECT_EQ(1, peerMgr->numStreamSubscribers());

  evb.runInEventBaseThreadAndWait([&]() {
    auto& subscriber = peerMgr->streamSubscribers_.at(*subscriberName);
    fillEgressQueueToHighWatermark(subscriber);
    peerMgr->reportStreamSubscriberBackpressureStats();
    EXPECT_TRUE(subscriber.blockStartTimeMs.has_value());

    /*
     * Take the queue to the low watermark. The queue leaves the blocked state
     * at that depth.
     */
    while (subscriber.peerInputQ->isBlocked()) {
      consumeOneEgressMessage(subscriber);
    }

    /*
     * This sample sees the queue unblocked, so it clears the record of the
     * block. A later sample therefore measures a new block from its own
     * start.
     */
    peerMgr->reportStreamSubscriberBackpressureStats();
    EXPECT_FALSE(subscriber.blockStartTimeMs.has_value());
    EXPECT_EQ(TBgpPeerState::ESTABLISHED, subscriber.state);

    /*
     * A block that starts now is far shorter than the limit, even though the
     * earlier block plus this one would be longer.
     */
    fillEgressQueueToHighWatermark(subscriber);
    peerMgr->reportStreamSubscriberBackpressureStats();
    peerMgr->reportStreamSubscriberBackpressureStats();
    EXPECT_EQ(TBgpPeerState::ESTABLISHED, subscriber.state);
    EXPECT_EQ(1, peerMgr->numStreamSubscribers());
  });

  XLOG(INFO, "Done");
}

/*
 * A client that reads slowly can take the queue below the low watermark and
 * let the producer refill it between two samples of the stats task. Then
 * every sample reads the queue as blocked, although the client reads. The
 * count of the consumed messages must keep such a session.
 */
TEST_F(StreamSubscriberFixture, StreamSubscriberIdleTimeoutSlowClient) {
  gflags::FlagSaver flags;
  FLAGS_stream_subscriber_idle_timeout_ms = kTestIdleTimeoutMs;

  SetUp(true /* configureMonitorPeer */, true /* initialAnnouncementDone */);
  auto& evb = peerMgr->getEventBase();

  const std::unique_ptr<std::string> subscriberName =
      std::make_unique<std::string>("subscriber1");
  auto stream = peerMgr->subscribe(subscriberName);

  evb.runInEventBaseThreadAndWait([&]() {
    auto& subscriber = peerMgr->streamSubscribers_.at(*subscriberName);
    fillEgressQueueToHighWatermark(subscriber);
    peerMgr->reportStreamSubscriberBackpressureStats();
    EXPECT_TRUE(subscriber.blockStartTimeMs.has_value());
    EXPECT_TRUE(subscriber.noProgressStartTimeMs.has_value());
    EXPECT_TRUE(subscriber.noProgressConsumedCount.has_value());

    /*
     * The client takes one message and the producer puts one back. The queue
     * holds the same depth and stays blocked, so the sampler never sees it
     * unblocked.
     */
    consumeOneEgressMessage(subscriber);
    subscriber.peerInputQ->push(BgpEndOfRib{});
    EXPECT_TRUE(subscriber.peerInputQ->isBlocked());

    ageBlockStart(subscriber, kTestElapsedMs);
    peerMgr->reportStreamSubscriberBackpressureStats();

    // The count of the consumed messages rose, so the session continues.
    EXPECT_EQ(TBgpPeerState::ESTABLISHED, subscriber.state);
    EXPECT_EQ(1, peerMgr->numStreamSubscribers());

    /*
     * The sampler measures the next run of no progress from the sample above.
     * A client that then stops to read loses its session.
     */
    EXPECT_TRUE(subscriber.blockStartTimeMs.has_value());
    ageBlockStart(subscriber, kTestElapsedMs);
    peerMgr->reportStreamSubscriberBackpressureStats();

    EXPECT_EQ(TBgpPeerState::IDLE, subscriber.state);
    EXPECT_EQ(0, peerMgr->numStreamSubscribers());
  });

  XLOG(INFO, "Done");
}

/*
 * The server cannot reach a client that gives no credit. Such a client learns
 * about the eviction when it gives credit again. It must then get an error
 * and not an orderly end of the stream, so that it can subscribe again.
 */
TEST_F(StreamSubscriberFixture, StreamSubscriberIdleTimeoutNotifiesClient) {
  gflags::FlagSaver flags;
  FLAGS_stream_subscriber_idle_timeout_ms = kTestIdleTimeoutMs;

  SetUp(true /* configureMonitorPeer */, true /* initialAnnouncementDone */);
  auto& evb = peerMgr->getEventBase();

  const std::unique_ptr<std::string> subscriberName =
      std::make_unique<std::string>("subscriber1");
  auto stream = peerMgr->subscribe(subscriberName);

  // Evict the session while the client has given no credit.
  evb.runInEventBaseThreadAndWait([&]() {
    auto& subscriber = peerMgr->streamSubscribers_.at(*subscriberName);
    fillEgressQueueToHighWatermark(subscriber);
    peerMgr->reportStreamSubscriberBackpressureStats();
    ageBlockStart(subscriber, kTestElapsedMs);
    peerMgr->reportStreamSubscriberBackpressureStats();

    EXPECT_EQ(TBgpPeerState::IDLE, subscriber.state);
    EXPECT_TRUE(
        subscriber.sessionState->evicted.load(std::memory_order_relaxed));
  });

  /*
   * The client gives credit now. Thrift starts the generator, the generator
   * sees the eviction, and it ends the stream with an error.
   */
  folly::Baton<> streamEnded;
  std::optional<std::string> streamError;
  bool streamEndedCleanly = false;
  auto subscription =
      std::move(stream).toClientStreamUnsafeDoNotUse().subscribeExTry(
          &evb, [&](auto&& t) {
            if (t.hasException()) {
              streamError = t.exception().what().toStdString();
              streamEnded.post();
            } else if (!t.hasValue()) {
              streamEndedCleanly = true;
              streamEnded.post();
            }
          });

  ASSERT_TRUE(streamEnded.try_wait_for(std::chrono::seconds(30)))
      << "the stream of the evicted subscriber neither ended nor failed";
  std::move(subscription).detach();

  ASSERT_TRUE(streamError.has_value())
      << "the evicted stream ended cleanly (endedCleanly=" << streamEndedCleanly
      << "), so the client cannot tell an eviction from an orderly end and "
         "its reconnect logic does not run";
  EXPECT_NE(std::string::npos, streamError->find("evicted"))
      << "actual stream error: " << *streamError;

  XLOG(INFO, "Done");
}

TEST_F(PeerManagerTestFixture, GetSessionManagerTest) {
  const PeeringParams peeringParams{fboss::kDefaultVrf};

  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);

  EXPECT_EQ(&mockPeerMgr->sessionMgr_, &mockPeerMgr->getSessionManager());
}

TEST_F(PeerManagerTestFixture, WaitForSessionTerminateBatonTest) {
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);

  const BgpPeerId kPeerId{kPeerAddr1, 1};

  // Create session terminate baton for kPeerId
  auto sessionTerminateBaton = std::make_shared<folly::coro::Baton>();
  mockPeerMgr->sessionTerminateBatons_[kPeerId] = sessionTerminateBaton;

  /*
   * Scope the subscription to the PeerManagerBase log category.
   * PeerManagerBase.cpp has no XLOG_SET_CATEGORY_NAME, so its XLOGF messages
   * land in the path-derived category below. Subscribing to the root ("")
   * instead captured stray DBG1 logs emitted by other components active during
   * the wait, which made messages.size() exceed 2 and shifted the expected
   * ordering (observed ~20/300 failures, all with size == 3). This matches the
   * scoping the other PeerManagerBase log-assertion tests in this file already
   * use.
   */
  auto& messages = subscribeToLogMessages(
      "neteng.fboss.bgp.cpp.peer.PeerManagerBase", folly::LogLevel::DBG1);

  /*
   * Drive the coroutine under test on a ManualExecutor on THIS thread. This
   * keeps all logging on a single thread (TestLogHandler is not thread-safe)
   * and makes the wait deterministic: drain() runs the coroutine until it
   * suspends on the (un-posted) baton, i.e. until it has emitted the entry log
   * and recorded its wait start time. Only after that confirmed suspension do
   * we let the production 1ms duration threshold elapse and post the baton, so
   * the "blocked" duration log fires every time.
   *
   * Earlier versions raced the post against the coroutine reaching co_await:
   * a two-thread version timed a sleep from thread creation, and an
   * EventBase-timer version relied on the queued coroutine being serviced
   * before a delayed timer. Under load the baton was occasionally posted
   * first, making the measured wait ~0ms (baton latch passes through) and
   * dropping the duration log (observed ~3-5/300 failures with size == 1 once
   * the stray-log pollution above was removed).
   */
  folly::ManualExecutor executor;
  auto future =
      folly::coro::co_withExecutor(
          &executor, mockPeerMgr->waitForSessionTerminateBaton(kPeerId))
          .start();

  /*
   * Run the coroutine until it suspends on co_await *baton. After this the
   * coroutine has emitted the entry log and recorded its start time.
   */
  executor.drain();
  ASSERT_FALSE(future.isReady());

  /*
   * The coroutine is now provably blocked on the baton, so there is no race
   * left. Spin until strictly more than the production 1ms threshold has
   * elapsed, then post. The coroutine's startTime was recorded at or before
   * gateStart, so the wait it measures (postTime - startTime) is guaranteed to
   * exceed 1ms and the duration log is emitted. This is a deterministic time
   * gate exercising the production threshold, not a sleep used to paper over
   * missing synchronization.
   */
  const auto gateStart = std::chrono::steady_clock::now();
  while (std::chrono::steady_clock::now() - gateStart <=
         std::chrono::milliseconds(2)) {
  }
  sessionTerminateBaton->post();

  // Resume the coroutine to completion; emits the duration log.
  executor.drain();
  ASSERT_TRUE(future.isReady());

  /*
   * Read the captured messages only after the coroutine has completed — no
   * concurrent access to the non-thread-safe handler.
   * Expect 2 log messages:
   * 1. Entry log: "Peer Manager waiting for adjRib..."
   * 2. Duration log: "Peer Manager blocked waiting for adjRib..." (since
   * duration > 1ms)
   */
  ASSERT_EQ(2, messages.size());
  EXPECT_TRUE(
      messages[0].first.getMessage().starts_with(
          "Peer Manager waiting for adjRib"));
  EXPECT_TRUE(
      messages[1].first.getMessage().starts_with(
          "Peer Manager blocked waiting for adjRib"));
}

TEST_F(PeerManagerTestFixture, UpdateEntryStatsTest) {
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  TEntryStats stats;
  mockPeerMgr->updateEntryStats(stats, fboss::kDefaultVrf);

  EXPECT_EQ(stats.total_adj_ribs(), mockPeerMgr->adjRibs_.size());
  auto* vrf = mockPeerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  EXPECT_EQ(
      stats.total_shadow_rib_entries(), vrf->getShadowRibEntries().size());
}

/**
 * Test updateIngressEgressPolicyNames method for peer group-based routing
 * policy updates.
 *
 * Test Flow:
 * 1. Setup: Create 3 AdjRibs with different peer groups
 * 2. Apply routing policies for different peer groups
 * 3. Verify: Policy names correctly updated + pending flags set/cleared
 * 4. Re-apply same policies → Verify: No changes detected (steady state)
 * 5. Apply different policies → Verify: Changes detected + stats updated
 */
TEST_F(
    PeerManagerDynamicPolicyEvaluationFixture,
    UpdateIngressEgressPolicyNamesForPeerGroupTest) {
  // Enable dynamic policy evaluation
  SetUp(true /* enableDynamicPolicyEvaluation */);

  // Create PolicyManager with all policy names needed by the test
  auto policyManager = setupPolicyManagerWithMultiplePolicies(
      {"ingress_policy_v1",
       "egress_policy_v1",
       "ingress_policy_v2",
       "egress_policy_v2",
       "ingress_policy_v3",
       "egress_policy_v3",
       "ingress_policy_v4",
       "egress_policy_v4"});

  auto configManager = std::make_shared<ConfigManager>(config_);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, policyManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config_->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  // Setup AdjRibs with different peer groups
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_,
      false, // isRrClient
      kV4Nexthop1, // v4Nexthop
      kV6Nexthop1, // v6Nexthop
      false, // enableStatefulHa
      false, // v4OverV6Nexthop
      policyManager);
  adjRib1->peeringParams_.description = "adjRib1";
  adjRib1->peeringParams_.peerGroupName = "PEERGROUP_RSW_FSW_V4";

  auto adjRib2 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId2,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_,
      false, // isRrClient
      kV4Nexthop1, // v4Nexthop
      kV6Nexthop1, // v6Nexthop
      false, // enableStatefulHa
      false, // v4OverV6Nexthop
      policyManager);
  adjRib2->peeringParams_.description = "adjRib2";
  adjRib2->peeringParams_.peerGroupName = "PEERGROUP_FSW_RSW_V4";

  // Add a stale entry to adjRib1
  auto dummyPathId = 5;
  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>>
      adjRibStaleTreeEntry;
  adjRibStaleTreeEntry.emplace(
      dummyPathId, std::make_unique<AdjRibEntry>(dummyPathId));
  adjRib1->adjRibInStale_.insert(
      kV4Prefix1.first, kV4Prefix1.second, std::move(adjRibStaleTreeEntry));

  // Add an entry to adjRib2
  auto dummyPathId1 = 0;
  auto pfx2Entry = std::make_unique<AdjRibEntry>(dummyPathId1);
  auto pfx2Path = std::make_shared<BgpPath>();
  pfx2Path->setNexthop(kV4Nexthop1);
  pfx2Entry->setPreIn(pfx2Path);
  adjRib2->adjRibInLiteTree_.insert(
      kV4Prefix2.first, kV4Prefix2.second, std::move(pfx2Entry));

  auto adjRib3 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId3,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_,
      false, // isRrClient
      kV4Nexthop1, // v4Nexthop
      kV6Nexthop1, // v6Nexthop
      false, // enableStatefulHa
      false, // v4OverV6Nexthop
      policyManager);

  adjRib3->peeringParams_.description = "adjRib3";
  adjRib3->peeringParams_.peerGroupName = "PEERGROUP_CSW_RSW_V4";

  auto adjRib4 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId4,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_,
      false, // isRrClient
      kV4Nexthop1, // v4Nexthop
      kV6Nexthop1, // v6Nexthop
      false, // enableStatefulHa
      false, // v4OverV6Nexthop
      policyManager);
  adjRib4->peeringParams_.description = "adjRib4";
  adjRib4->peeringParams_.peerGroupName = "PEERGROUP_FSW_RSW_V4";

  auto dummyPathId2 = 0;
  auto pfx3Entry = std::make_unique<AdjRibEntry>(dummyPathId2);
  auto pfx3Path = std::make_shared<BgpPath>();
  pfx3Path->setNexthop(kV4Nexthop1);
  pfx3Entry->setPreIn(pfx3Path);
  adjRib4->adjRibInLiteTree_.insert(
      kV4Prefix3.first, kV4Prefix3.second, std::move(pfx3Entry));

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  peerMgr->adjRibs_[kPeerId2] = adjRib2;
  peerMgr->adjRibs_[kPeerId3] = adjRib3;
  peerMgr->adjRibs_[kPeerId4] = adjRib4;

  adjRib1->markStateEstablished();
  adjRib2->markStateEstablished();
  adjRib3->markStateEstablished();
  adjRib4->markStateEstablished();

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  /*
   * Create policy name map keyed by peer IP addresses.
   * BgpServiceBase is now responsible for resolving group → per-peer
   * policies, so PeerManagerBase receives per-peer maps.
   */
  std::string peerAddr1 = adjRib1->peeringParams_.peerAddr.str(); // 1.1.1.1
  std::string peerAddr2 = adjRib2->peeringParams_.peerAddr.str(); // 2.2.2.2
  std::string peerAddr4 = adjRib4->peeringParams_.peerAddr.str(); // 127.4.0.1

  auto policyMap1 = createPolicyMap(
      {{peerAddr1, "ingress_policy_v1", "egress_policy_v1"},
       {peerAddr2, "ingress_policy_v2", "egress_policy_v2"},
       {peerAddr4, "ingress_policy_v2", "egress_policy_v2"}});

  /*
   * Increment config version before applying policies (version check requires
   * currentVersion > lastAppliedPolicyVersion_)
   */
  configManager->updateConfig(config_);

  // Apply the policies
  peerMgr->updateIngressEgressPolicyNames(std::move(policyMap1));

  // Verify final state: policies correctly applied
  evb.runInEventBaseThreadAndWait([&]() {
    // Check that policy names were updated in AdjRibs
    EXPECT_EQ("ingress_policy_v1", adjRib1->ingressPolicyName_.value());
    EXPECT_EQ("egress_policy_v1", adjRib1->egressPolicyName_.value());
    EXPECT_EQ("ingress_policy_v2", adjRib2->ingressPolicyName_.value());
    EXPECT_EQ("egress_policy_v2", adjRib2->egressPolicyName_.value());
    EXPECT_EQ("ingress_policy_v2", adjRib4->ingressPolicyName_.value());
    EXPECT_EQ("egress_policy_v2", adjRib4->egressPolicyName_.value());

    // adjRib3 should have no policy names set (not in policy map)
    EXPECT_FALSE(adjRib3->ingressPolicyName_.has_value());
    EXPECT_FALSE(adjRib3->egressPolicyName_.has_value());
  });

  // Verify stats counters for ingress and egress routing policy affected peers
  EXPECT_EQ(
      3,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kIngressRoutingPolicyAffectedPeers));
  EXPECT_EQ(
      3,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kEgressRoutingPolicyAffectedPeers));

  // Re-apply same policies - no changes should be detected
  auto samePolicyMap = createPolicyMap(
      {{peerAddr1, "ingress_policy_v1", "egress_policy_v1"},
       {peerAddr2, "ingress_policy_v2", "egress_policy_v2"},
       {peerAddr4, "ingress_policy_v2", "egress_policy_v2"}});

  configManager->updateConfig(config_);
  peerMgr->updateIngressEgressPolicyNames(std::move(samePolicyMap));

  /*
   * Stats should remain the same (no new changes) - check before EVB
   * processes the update (same policies = 0 affected, which would reset
   * counters)
   */
  EXPECT_EQ(
      3,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kIngressRoutingPolicyAffectedPeers));
  EXPECT_EQ(
      3,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kEgressRoutingPolicyAffectedPeers));

  /*
   * Wait for EVB to process the same-policy update before bumping version
   * again. Without this sync, the next updateConfig() would bump the version
   * before the lambda runs, causing the subsequent update to be skipped.
   */
  evb.runInEventBaseThreadAndWait([&]() {
    // Verify same policies still applied (unchanged)
    EXPECT_EQ("ingress_policy_v1", adjRib1->ingressPolicyName_.value());
    EXPECT_EQ("egress_policy_v1", adjRib1->egressPolicyName_.value());
  });

  // Apply different policies - changes should be detected
  auto policyMap2 = createPolicyMap(
      {{peerAddr1, "ingress_policy_v3", "egress_policy_v3"},
       {peerAddr2, "ingress_policy_v4", "egress_policy_v4"},
       {peerAddr4, "ingress_policy_v4", "egress_policy_v4"}});

  configManager->updateConfig(config_);
  peerMgr->updateIngressEgressPolicyNames(std::move(policyMap2));

  // Verify final state: new policies correctly applied
  evb.runInEventBaseThreadAndWait([&]() {
    EXPECT_EQ("ingress_policy_v3", adjRib1->ingressPolicyName_.value());
    EXPECT_EQ("egress_policy_v3", adjRib1->egressPolicyName_.value());
    EXPECT_EQ("ingress_policy_v4", adjRib2->ingressPolicyName_.value());
    EXPECT_EQ("egress_policy_v4", adjRib2->egressPolicyName_.value());
    EXPECT_EQ("ingress_policy_v4", adjRib4->ingressPolicyName_.value());
    EXPECT_EQ("egress_policy_v4", adjRib4->egressPolicyName_.value());
  });

  // Verify stats counters with new policies
  EXPECT_EQ(
      3,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kIngressRoutingPolicyAffectedPeers));
  EXPECT_EQ(
      3,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kEgressRoutingPolicyAffectedPeers));

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/**
 * Test updateIngressEgressPolicyNames method for IP address-based routing
 * policy updates.
 *
 * Test Flow:
 * 1. Setup: Create 3 AdjRibs with different IP addresses
 * 2. Apply routing policies for specific IP addresses
 * 3. Verify: Policy names correctly updated + stats updated
 * 4. Test partial updates (only ingress or egress)
 * 5. Test policy removal by not including in new policy map
 */
TEST_F(
    PeerManagerDynamicPolicyEvaluationFixture,
    UpdateIngressEgressPolicyNamesForIPAddressTest) {
  // Enable dynamic policy evaluation
  SetUp(true /* enableDynamicPolicyEvaluation */);

  // Create PolicyManager with all policy names needed by the test
  auto policyManager = setupPolicyManagerWithMultiplePolicies(
      {"ingress_policy_peer1",
       "egress_policy_peer1",
       "ingress_policy_peer2",
       "egress_policy_peer2"});

  auto configManager = std::make_shared<ConfigManager>(config_);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, policyManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config_->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  // Setup AdjRibs with different IP addresses
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_,
      false, // isRrClient
      kV4Nexthop1, // v4Nexthop
      kV6Nexthop1, // v6Nexthop
      false, // enableStatefulHa
      false, // v4OverV6Nexthop
      policyManager);
  adjRib1->peeringParams_.description = "adjRib1";

  auto adjRib2 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId2,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_,
      false, // isRrClient
      kV4Nexthop1, // v4Nexthop
      kV6Nexthop1, // v6Nexthop
      false, // enableStatefulHa
      false, // v4OverV6Nexthop
      policyManager);
  adjRib2->peeringParams_.description = "adjRib2";

  auto adjRib3 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId3,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_);
  adjRib3->peeringParams_.description = "adjRib3";

  // Add a stale entry to adjRib1
  auto dummyPathId = 5;
  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>>
      adjRibStaleTreeEntry;
  adjRibStaleTreeEntry.emplace(
      dummyPathId, std::make_unique<AdjRibEntry>(dummyPathId));
  adjRib1->adjRibInStale_.insert(
      kV4Prefix1.first, kV4Prefix1.second, std::move(adjRibStaleTreeEntry));

  // Add an entry to adjRib2
  auto dummyPathId1 = 0;
  auto pfx2Entry = std::make_unique<AdjRibEntry>(dummyPathId1);
  auto pfx2Path = std::make_shared<BgpPath>();
  pfx2Path->setNexthop(kV4Nexthop1);
  pfx2Entry->setPreIn(pfx2Path);
  adjRib2->adjRibInLiteTree_.insert(
      kV4Prefix2.first, kV4Prefix2.second, std::move(pfx2Entry));

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  peerMgr->adjRibs_[kPeerId2] = adjRib2;
  peerMgr->adjRibs_[kPeerId3] = adjRib3;

  adjRib1->markStateEstablished();
  adjRib2->markStateEstablished();
  adjRib3->markStateEstablished();

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  /*
   * Create policy name map for IP addresses
   * Use the actual peerAddr from AdjRib peeringParams to match
   * PeerManagerBase's matchKey logic
   */
  std::string peerAddr1 = adjRib1->peeringParams_.peerAddr.str();
  std::string peerAddr2 = adjRib2->peeringParams_.peerAddr.str();

  auto policyMap1 = createPolicyMap({
      {peerAddr1, "ingress_policy_peer1", "egress_policy_peer1"},
      {peerAddr2, "ingress_policy_peer2", ""}
      // Intentionally omit egress policy for peer2 to test partial updates
  });

  // Increment config version before applying policies
  configManager->updateConfig(config_);

  // Apply the policies
  peerMgr->updateIngressEgressPolicyNames(std::move(policyMap1));

  // Verify final state: policies correctly applied
  evb.runInEventBaseThreadAndWait([&]() {
    // Check that policy names were updated in AdjRibs
    EXPECT_EQ("ingress_policy_peer1", adjRib1->ingressPolicyName_.value());
    EXPECT_EQ("egress_policy_peer1", adjRib1->egressPolicyName_.value());
    EXPECT_EQ("ingress_policy_peer2", adjRib2->ingressPolicyName_.value());
    EXPECT_FALSE(
        adjRib2->egressPolicyName_.has_value()); // No egress policy set

    // adjRib3 should have no policy names set (not in policy map)
    EXPECT_FALSE(adjRib3->ingressPolicyName_.has_value());
    EXPECT_FALSE(adjRib3->egressPolicyName_.has_value());
  });

  // Verify stats counters
  EXPECT_EQ(
      2,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kIngressRoutingPolicyAffectedPeers));
  EXPECT_EQ(
      1,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kEgressRoutingPolicyAffectedPeers));

  // Test policy updates with egress policy added for peer2
  auto policyMap2 = createPolicyMap({
      {peerAddr1, "ingress_policy_peer1", "egress_policy_peer1"},
      {peerAddr2, "ingress_policy_peer2", "egress_policy_peer2"} // Now added
  });

  configManager->updateConfig(config_);
  peerMgr->updateIngressEgressPolicyNames(std::move(policyMap2));

  // Verify final state: egress policy now set for peer2
  evb.runInEventBaseThreadAndWait([&]() {
    EXPECT_EQ("ingress_policy_peer1", adjRib1->ingressPolicyName_.value());
    EXPECT_EQ("egress_policy_peer1", adjRib1->egressPolicyName_.value());
    EXPECT_EQ("ingress_policy_peer2", adjRib2->ingressPolicyName_.value());
    EXPECT_EQ(
        "egress_policy_peer2", adjRib2->egressPolicyName_.value()); // Now set
  });

  // Verify updated stats counters
  EXPECT_EQ(
      0,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kIngressRoutingPolicyAffectedPeers));
  EXPECT_EQ(
      1,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kEgressRoutingPolicyAffectedPeers));

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/**
 * Test updateIngressEgressPolicyNames method with empty policy map.
 *
 * Test Flow:
 * 1. Setup: Create AdjRibs with existing policies
 * 2. Apply empty policy map
 * 3. Verify: No changes are made to existing policies
 * 4. Verify: Stats remain unchanged
 */
TEST_F(
    PeerManagerDynamicPolicyEvaluationFixture,
    UpdateIngressEgressPolicyNamesEmptyMapTest) {
  // Enable dynamic policy evaluation
  SetUp(true /* enableDynamicPolicyEvaluation */);

  /*
   * Create a basic PolicyManager (for consistency, even though this test uses
   * empty maps)
   */
  auto policyManager = setupPolicyManagerWithMultiplePolicies({});
  auto configManager = std::make_shared<ConfigManager>(config_);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, policyManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config_->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  // Setup AdjRib with existing policies
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_,
      false, // isRrClient
      kV4Nexthop1, // v4Nexthop
      kV6Nexthop1, // v6Nexthop
      false, // enableStatefulHa
      false, // v4OverV6Nexthop
      policyManager);
  adjRib1->peeringParams_.description = "adjRib1";
  adjRib1->peeringParams_.peerGroupName = "PEERGROUP_RSW_FSW_V4";

  // Set existing policies
  adjRib1->ingressPolicyName_ = "existing_ingress_policy";
  adjRib1->egressPolicyName_ = "existing_egress_policy";

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  adjRib1->markStateEstablished();

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  // Increment config version before applying policies
  configManager->updateConfig(config_);

  // Apply empty policy map
  auto emptyPolicyMap = std::make_unique<PeerToPolicyMap>();

  peerMgr->updateIngressEgressPolicyNames(std::move(emptyPolicyMap));

  // Verify final state: existing policies unchanged
  evb.runInEventBaseThreadAndWait([&]() {
    EXPECT_EQ("existing_ingress_policy", adjRib1->ingressPolicyName_.value());
    EXPECT_EQ("existing_egress_policy", adjRib1->egressPolicyName_.value());
  });

  // Verify stats counters (should be 0 since no changes were made)
  EXPECT_EQ(
      0,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kIngressRoutingPolicyAffectedPeers));
  EXPECT_EQ(
      0,
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kEgressRoutingPolicyAffectedPeers));

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/******************************************************************************
 *      START   -   Policy Version Race Avoidance Tests                       *
 ******************************************************************************/

/**
 * Test that lastAppliedPolicyVersion_ is updated after applying policy updates.
 *
 * This test verifies the basic flow where:
 * 1. Initial config version is 0
 * 2. After first policy update, version is applied and tracked
 * 3. The policy update is processed (not skipped as stale)
 */
TEST_F(
    PeerManagerDynamicPolicyEvaluationFixture,
    PolicyVersionUpdatedAfterApplyingPolicies) {
  // Enable dynamic policy evaluation
  SetUp(true /* enableDynamicPolicyEvaluation */);

  auto policyManager = setupPolicyManagerWithMultiplePolicies(
      {"ingress_policy_v1", "egress_policy_v1"});

  auto configManager = std::make_shared<ConfigManager>(config_);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, policyManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config_->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  // Setup AdjRib
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_,
      false, // isRrClient
      kV4Nexthop1,
      kV6Nexthop1,
      false, // enableStatefulHa
      false, // v4OverV6Nexthop
      policyManager);
  adjRib1->peeringParams_.description = "adjRib1";
  adjRib1->peeringParams_.peerGroupName = "PEERGROUP_RSW_FSW_V4";

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  adjRib1->markStateEstablished();

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  // Verify initial state: config version is 0, lastAppliedPolicyVersion_ is 0
  EXPECT_EQ(0, configManager->getConfigVersion());
  evb.runInEventBaseThreadAndWait(
      [&]() { EXPECT_EQ(0, peerMgr->lastAppliedPolicyVersion_); });

  /*
   * Trigger a config update to increment the version
   * This simulates BgpServiceBase updating config before calling
   * updateIngressEgressPolicyNames
   */
  configManager->updateConfig(config_);
  EXPECT_EQ(1, configManager->getConfigVersion());

  // Apply policy update
  std::string peerAddr1 = adjRib1->peeringParams_.peerAddr.str();
  auto policyMap =
      createPolicyMap({{peerAddr1, "ingress_policy_v1", "egress_policy_v1"}});
  peerMgr->updateIngressEgressPolicyNames(std::move(policyMap));

  // Verify: lastAppliedPolicyVersion_ updated to match config version
  WITH_RETRIES({
    evb.runInEventBaseThreadAndWait(
        [&]() { EXPECT_EVENTUALLY_EQ(1, peerMgr->lastAppliedPolicyVersion_); });
  });

  // Verify policies were applied
  evb.runInEventBaseThreadAndWait([&]() {
    EXPECT_EQ("ingress_policy_v1", adjRib1->ingressPolicyName_.value());
    EXPECT_EQ("egress_policy_v1", adjRib1->egressPolicyName_.value());
  });

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/**
 * Test that stale policy updates (version <= lastApplied) are skipped.
 *
 * This test simulates a race condition scenario where:
 * 1. Config is updated to version 2
 * 2. Policy update with version 2 is applied successfully
 * 3. A "stale" policy update arrives (queued before version 2 was applied)
 * 4. The stale update should be skipped because version hasn't increased
 *
 * This validates the fix for concurrent setPeersPolicy/unsetPeersPolicy calls.
 */
TEST_F(PeerManagerDynamicPolicyEvaluationFixture, StalePolicyUpdateIsSkipped) {
  // Enable dynamic policy evaluation
  SetUp(true /* enableDynamicPolicyEvaluation */);

  auto policyManager = setupPolicyManagerWithMultiplePolicies(
      {"policy_v1", "policy_v2", "stale_policy"});

  auto configManager = std::make_shared<ConfigManager>(config_);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, policyManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config_->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  // Setup AdjRib
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_,
      false, // isRrClient
      kV4Nexthop1,
      kV6Nexthop1,
      false, // enableStatefulHa
      false, // v4OverV6Nexthop
      policyManager);
  adjRib1->peeringParams_.description = "adjRib1";
  adjRib1->peeringParams_.peerGroupName = "PEERGROUP_RSW_FSW_V4";

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  adjRib1->markStateEstablished();

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  std::string peerAddr1 = adjRib1->peeringParams_.peerAddr.str();

  // Step 1: Apply initial policy with version 1
  configManager->updateConfig(config_);
  EXPECT_EQ(1, configManager->getConfigVersion());

  auto policyMap1 = createPolicyMap({{peerAddr1, "policy_v1", ""}});
  peerMgr->updateIngressEgressPolicyNames(std::move(policyMap1));

  WITH_RETRIES({
    evb.runInEventBaseThreadAndWait([&]() {
      EXPECT_EVENTUALLY_EQ(1, peerMgr->lastAppliedPolicyVersion_);
      EXPECT_EVENTUALLY_EQ("policy_v1", adjRib1->ingressPolicyName_.value());
    });
  });

  // Step 2: Apply second policy with version 2
  configManager->updateConfig(config_);
  EXPECT_EQ(2, configManager->getConfigVersion());

  auto policyMap2 = createPolicyMap({{peerAddr1, "policy_v2", ""}});
  peerMgr->updateIngressEgressPolicyNames(std::move(policyMap2));

  WITH_RETRIES({
    evb.runInEventBaseThreadAndWait([&]() {
      EXPECT_EVENTUALLY_EQ(2, peerMgr->lastAppliedPolicyVersion_);
      EXPECT_EVENTUALLY_EQ("policy_v2", adjRib1->ingressPolicyName_.value());
    });
  });

  /*
   * Step 3: Try to apply a "stale" update without incrementing config version
   * This simulates a race where an older update arrives after a newer one
   * The config version is still 2, same as lastAppliedPolicyVersion_
   */
  auto stalePolicyMap = createPolicyMap({{peerAddr1, "stale_policy", ""}});
  peerMgr->updateIngressEgressPolicyNames(std::move(stalePolicyMap));

  // Synchronize with EVB to ensure the stale update has been processed
  evb.runInEventBaseThreadAndWait([&]() {
    // Version should still be 2
    EXPECT_EQ(2, peerMgr->lastAppliedPolicyVersion_);
    // Policy should still be "policy_v2", NOT "stale_policy"
    EXPECT_EQ("policy_v2", adjRib1->ingressPolicyName_.value());
  });

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/**
 * Test rapid policy updates where only the latest should be applied.
 *
 * This test simulates the race condition scenario:
 * 1. Multiple config updates happen rapidly (versions 1, 2, 3)
 * 2. Policy updates are posted to EVB but may arrive out of order
 * 3. Only updates with version > lastApplied should be applied
 * 4. Final state should reflect the most recent policy
 */
TEST_F(
    PeerManagerDynamicPolicyEvaluationFixture,
    RapidPolicyUpdatesOnlyLatestApplied) {
  // Enable dynamic policy evaluation
  SetUp(true /* enableDynamicPolicyEvaluation */);

  auto policyManager = setupPolicyManagerWithMultiplePolicies(
      {"rapid_policy_v1", "rapid_policy_v2", "rapid_policy_v3"});

  auto configManager = std::make_shared<ConfigManager>(config_);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, policyManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config_->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  // Setup AdjRib
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_,
      false, // isRrClient
      kV4Nexthop1,
      kV6Nexthop1,
      false, // enableStatefulHa
      false, // v4OverV6Nexthop
      policyManager);
  adjRib1->peeringParams_.description = "adjRib1";
  adjRib1->peeringParams_.peerGroupName = "PEERGROUP_RSW_FSW_V4";

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  adjRib1->markStateEstablished();

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  std::string peerAddr1 = adjRib1->peeringParams_.peerAddr.str();

  // Rapid succession of config updates
  configManager->updateConfig(config_); // version 1
  configManager->updateConfig(config_); // version 2
  configManager->updateConfig(config_); // version 3
  EXPECT_EQ(3, configManager->getConfigVersion());

  /*
   * Post all three policy updates
   * In a real race, these could arrive on EVB in any order
   */
  auto policyMap1 = createPolicyMap({{peerAddr1, "rapid_policy_v1", ""}});
  auto policyMap2 = createPolicyMap({{peerAddr1, "rapid_policy_v2", ""}});
  auto policyMap3 = createPolicyMap({{peerAddr1, "rapid_policy_v3", ""}});

  peerMgr->updateIngressEgressPolicyNames(std::move(policyMap1));
  peerMgr->updateIngressEgressPolicyNames(std::move(policyMap2));
  peerMgr->updateIngressEgressPolicyNames(std::move(policyMap3));

  // Wait for all EVB tasks to complete
  WITH_RETRIES({
    evb.runInEventBaseThreadAndWait([&]() {
      /*
       * The first update (checking version 3 > 0) should apply
       * All subsequent updates should be skipped (version 3 <= 3)
       */
      EXPECT_EVENTUALLY_EQ(3, peerMgr->lastAppliedPolicyVersion_);
    });
  });

  /*
   * The first update that executes (with current version 3) wins.
   * Since all updates check the same version 3, only the first one
   * that runs will be applied (version 3 > 0).
   * All others will see version 3 <= lastApplied (3) and be skipped.
   */
  evb.runInEventBaseThreadAndWait([&]() {
    /*
     * Policy should be one of the rapid policies (whichever ran first)
     * Most likely rapid_policy_v1 since it was posted first
     */
    EXPECT_TRUE(adjRib1->ingressPolicyName_.has_value());
  });

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/**
 * Test that config version 0 policy update is applied (initial update).
 *
 * Verifies that when lastAppliedPolicyVersion_ is 0 and config version is also
 * 0, the update is skipped (version 0 <= 0). This ensures proper
 * initialization.
 */
TEST_F(
    PeerManagerDynamicPolicyEvaluationFixture,
    InitialPolicyUpdateRequiresConfigVersionIncrement) {
  // Enable dynamic policy evaluation
  SetUp(true /* enableDynamicPolicyEvaluation */);

  auto policyManager =
      setupPolicyManagerWithMultiplePolicies({"initial_policy"});

  auto configManager = std::make_shared<ConfigManager>(config_);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, policyManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto globalConfig = config_->getBgpGlobalConfig();
  auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto& evb = peerMgr->getEventBase();

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  // Setup AdjRib
  auto adjRib1 = setupAdjRib(
      evb,
      vrf->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config_,
      false,
      kV4Nexthop1,
      kV6Nexthop1,
      false,
      false,
      policyManager);
  adjRib1->peeringParams_.description = "adjRib1";
  adjRib1->peeringParams_.peerGroupName = "PEERGROUP_RSW_FSW_V4";

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  adjRib1->markStateEstablished();

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  std::string peerAddr1 = adjRib1->peeringParams_.peerAddr.str();

  // Initial state: both config version and lastAppliedPolicyVersion_ are 0
  EXPECT_EQ(0, configManager->getConfigVersion());
  evb.runInEventBaseThreadAndWait(
      [&]() { EXPECT_EQ(0, peerMgr->lastAppliedPolicyVersion_); });

  /*
   * Try to apply policy without incrementing config version
   * This should be skipped because version 0 <= lastApplied 0
   */
  auto policyMap = createPolicyMap({{peerAddr1, "initial_policy", ""}});
  peerMgr->updateIngressEgressPolicyNames(std::move(policyMap));

  // Synchronize with EVB to ensure the update has been processed
  evb.runInEventBaseThreadAndWait([&]() {
    // Verify: policy should NOT be applied (version 0 <= 0)
    EXPECT_EQ(0, peerMgr->lastAppliedPolicyVersion_);
    EXPECT_FALSE(adjRib1->ingressPolicyName_.has_value());
  });

  // Now increment config version and retry
  configManager->updateConfig(config_);
  EXPECT_EQ(1, configManager->getConfigVersion());

  auto policyMap2 = createPolicyMap({{peerAddr1, "initial_policy", ""}});
  peerMgr->updateIngressEgressPolicyNames(std::move(policyMap2));

  // Verify: now policy should be applied (version 1 > 0)
  WITH_RETRIES({
    evb.runInEventBaseThreadAndWait([&]() {
      EXPECT_EVENTUALLY_EQ(1, peerMgr->lastAppliedPolicyVersion_);
      EXPECT_EVENTUALLY_EQ(
          "initial_policy", adjRib1->ingressPolicyName_.value());
    });
  });

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/******************************************************************************
 *      END   -   Policy Version Race Avoidance Tests                         *
 ******************************************************************************/

/**
 * Test for race condition fix in S596523
 * Root cause: postTerminateBaton used a cancellable wait that could be
 * cancelled by asyncScope_->requestCancellation(), posting baton early.
 *
 * Fix: Use folly::CancellationToken{} (non-cancellable) in postTerminateBaton.
 *
 * Test behavior:
 *   - HANGS before the fix (deadlock due to early baton post)
 *   - PASSES after the fix (proper cleanup ordering)
 */
TEST_F(PeerManagerTestFixture, SessionFlapRaceConditionTest) {
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */);
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);
  uint64_t version = 0x100;
  auto versionNumber = std::make_shared<VersionNumber>(version);

  auto& evb = mockPeerMgr->getEventBase();
  auto& fm = folly::fibers::getFiberManager(evb, options_);
  fm.addTask([&] {
    mockPeerMgr->getDefaultVrf().setRibInitPathComputationNotified(false);

    auto sessionInfo = FiberBgpPeer::getObservableSessionInfo(
        mockInfo1_,
        nullptr /* unbounded input queue, unused */,
        sessionMgr->boundedIqueue_,
        sessionMgr->oQueue_,
        versionNumber);

    FiberBgpPeer::ObservableStateT stateEvent{
        .peerId = kPeerId3,
        .versionNumber = version,
        .remoteAs = mockInfo1_.peeringParams.remoteAs,
        .sessionInfo = sessionInfo};

    // Establish the "old" session
    folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEvent));
    auto adjRib = mockPeerMgr->findAdjRib(kPeerId3);
    ASSERT_NE(nullptr, adjRib);
    EXPECT_TRUE(adjRib->isStateEstablished());

    fiberSleepFor(30ms);

    // Terminate the session
    sessionMgr->oQueue_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    stateEvent.versionNumber = ++version;
    sessionInfo->currentVersion = std::make_shared<VersionNumber>(version);

    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEvent));
    EXPECT_FALSE(adjRib->isStateEstablished());

    // Re-establish session IMMEDIATELY to trigger the race
    stateEvent.versionNumber = ++version;
    sessionInfo->currentVersion = std::make_shared<VersionNumber>(version);

    folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEvent));
    EXPECT_TRUE(adjRib->isStateEstablished());

    // Wait for any pending cleanup from old session
    fiberSleepFor(100ms);

    // Verify new session is still valid after old session cleanup completes
    EXPECT_TRUE(adjRib->isStateEstablished());

    // Cleanup
    stateEvent.versionNumber = ++version;
    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEvent));
    sessionMgr->oQueue_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    fiberSleepFor(50ms);

    mockPeerMgr->stop();
  });

  evb.loop();
  SUCCEED();
}

/**
 * @brief Test that markDaemonShutdown() sets isDaemonShutdown_ on all AdjRibs
 *        and sessionTerminated() clears AdjRib trees when isDaemonShutdown_ is
 * true.
 *
 * This test verifies the fast-path shutdown behavior:
 * 1. Setup two AdjRibs with entries in both ingress and egress trees
 * 2. Call markDaemonShutdown() to set isDaemonShutdown_ on all AdjRibs
 * 3. Trigger sessionTerminated() on each AdjRib
 * 4. Verify adjribInTrees are cleared
 */
TEST_F(PeerManagerTestFixture, MarkDaemonShutdownClearsAdjRibTreesTest) {
  auto config = getConfig(
      true,
      true,
      false,
      false,
      false,
      true /*enableVipService*/,
      0,
      false,
      false,
      false,
      {"enable_vip_server_limit"} /* bgp_setting_config */);
  auto globalConfig = config->getBgpGlobalConfig();

  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);

  peerMgr->setSessionManager(sessionMgr);

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  folly::EventBase evb;
  auto& fm = folly::fibers::getFiberManager(evb, options_);

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  fm.addTask([&] {
    // Create two AdjRibs with a shared AdjRibOutGroup
    auto adjRib1 = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId1,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config);
    peerMgr->adjRibs_[kPeerId1] = adjRib1;

    auto adjRibOutGroup =
        std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "TestGroup");
    adjRib1->adjRibOutGroup_ = adjRibOutGroup;

    auto adjRib2 = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId2,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config);
    peerMgr->adjRibs_[kPeerId2] = adjRib2;
    adjRib2->adjRibOutGroup_ = adjRibOutGroup;

    // Add entries to ingress trees (adjRibInLiteTree_)
    auto attrs1 = std::make_shared<BgpPath>();
    auto adjRibEntry1 = std::make_unique<AdjRibEntry>(kDefaultPathID);
    adjRibEntry1->setPreIn(attrs1);
    adjRib1->adjRibInLiteTree_.insert(
        kPeerPrefix1.first, kPeerPrefix1.second, std::move(adjRibEntry1));

    auto attrs2 = std::make_shared<BgpPath>();
    auto adjRibEntry2 = std::make_unique<AdjRibEntry>(kDefaultPathID);
    adjRibEntry2->setPreIn(attrs2);
    adjRib2->adjRibInLiteTree_.insert(
        kPeerPrefix2.first, kPeerPrefix2.second, std::move(adjRibEntry2));

    // Add entries to egress trees (via addRibEntry which adds to LiteTree)
    adjRib1->addRibEntry(false, kPeerPrefix1, kDefaultPathID);
    adjRib2->addRibEntry(false, kPeerPrefix2, kDefaultPathID);

    // Verify trees have entries before shutdown
    EXPECT_EQ(1, adjRib1->adjRibInLiteTree_.size());
    EXPECT_EQ(1, adjRib2->adjRibInLiteTree_.size());
    EXPECT_EQ(2, adjRibOutGroup->LiteTree_.size());

    // Verify isDaemonShutdown_ is initially false
    EXPECT_FALSE(adjRib1->isDaemonShutdown_);
    EXPECT_FALSE(adjRib2->isDaemonShutdown_);

    /*
     * Call markDaemonShutdown() - this should set isDaemonShutdown_ on all
     * AdjRibs
     */
    peerMgr->markDaemonShutdown();

    // Verify isDaemonShutdown_ is now true on all AdjRibs
    EXPECT_TRUE(adjRib1->isDaemonShutdown_);
    EXPECT_TRUE(adjRib2->isDaemonShutdown_);

    /*
     * Trigger sessionTerminated on both AdjRibs
     * With isDaemonShutdown_=true, this should clear trees via fast-path
     */
    folly::coro::blockingWait(
        adjRib1->sessionTerminated(FiberBgpPeer::BgpSessionStop{}));
    folly::coro::blockingWait(
        adjRib2->sessionTerminated(FiberBgpPeer::BgpSessionStop{}));

    // Verify all ingress trees are cleared
    EXPECT_EQ(0, adjRib1->adjRibInLiteTree_.size());
    EXPECT_EQ(0, adjRib1->adjRibInPathTree_.size());
    EXPECT_EQ(0, adjRib1->adjRibInStale_.size());
    EXPECT_EQ(0, adjRib2->adjRibInLiteTree_.size());
    EXPECT_EQ(0, adjRib2->adjRibInPathTree_.size());
    EXPECT_EQ(0, adjRib2->adjRibInStale_.size());

    /*
     * Note: adjRibOutGroup_ trees are NOT cleared by sessionTerminated()
     * because they are shared across peers. They are cleared by
     * PeerManagerBase::~PeerManagerBase() destructor during BGP daemon
     * shutdown.
     */

    fiberSleepFor(10ms);
    peerMgr->stop();
    sessionMgr->stop();
  });

  evb.loop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/**
 * @brief Test PeerManagerBase::addPeers() adds peers successfully and returns
 *        PEER_EXISTS_ALREADY on duplicate.
 */
TEST_F(PeerManagerTestFixture, AddPeersTest) {
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto globalConfig = config->getBgpGlobalConfig();

  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  // Add a single peer
  const auto& peerToConfig = config->getPeerToConfig();
  auto it = peerToConfig.begin();

  std::vector<std::shared_ptr<BgpPeerConfig>> singlePeer = {it->second};
  auto result = folly::coro::blockingWait(peerMgr->addPeers(singlePeer));
  EXPECT_TRUE(result.hasValue());

  // Adding the same peer again should return PEER_EXISTS_ALREADY
  auto result2 = folly::coro::blockingWait(peerMgr->addPeers(singlePeer));
  EXPECT_TRUE(result2.hasError());
  EXPECT_EQ(
      result2.error(),
      nettools::bgplib::FiberBgpPeerManager::ErrorCode::PEER_EXISTS_ALREADY);

  // Add multiple new peers in a single batch
  ASSERT_GE(peerToConfig.size(), 2u);
  ++it;
  std::vector<std::shared_ptr<BgpPeerConfig>> multiplePeers = {it->second};
  // Add remaining peers if available
  while (++it != peerToConfig.end()) {
    multiplePeers.push_back(it->second);
  }
  auto result3 = folly::coro::blockingWait(peerMgr->addPeers(multiplePeers));
  EXPECT_TRUE(result3.hasValue());

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/*
 * Test AdjRib::getEffectivePostOutPrefixCount() — the accessor that returns
 * the correct post-out count whether the peer is in an update-group or not.
 * This prevents the bug where an in-sync UG peer (per-peer post-out cleared
 * to 0, count lives on the group) was wrongly seen as "no routes advertised."
 */
TEST_F(PeerManagerTestFixture, GetEffectivePostOutPrefixCountTest) {
  auto config = getConfig(
      true,
      true,
      false,
      false,
      false,
      true /*enableVipService*/,
      0,
      false,
      false,
      false,
      {"enable_vip_server_limit"} /* bgp_setting_config */);
  auto globalConfig = config->getBgpGlobalConfig();

  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);
  peerMgr->setSessionManager(sessionMgr);

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  folly::EventBase evb;
  auto& fm = folly::fibers::getFiberManager(evb, options_);

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  ASSERT_NE(nullptr, vrf);
  fm.addTask([&] {
    /*
     * Case 1: In-sync update-group peer (per-peer=0, group>0).
     * The accessor should return the group count, not 0.
     */
    auto adjRib1 = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId1,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config);

    /* Attach an update-group. */
    adjRib1->adjRibOutGroup_ =
        std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "Group");

    /* Bump group post-out count to 100 (IPv4). */
    for (uint32_t i = 0; i < 100; ++i) {
      adjRib1->adjRibOutGroup_->stats_.incrementPostOutPrefixCount(
          /*isIpv4=*/true);
    }

    /*
     * Per-peer post-out is 0 (an in-sync peer's count was cleared when it
     * joined the group). Verify the accessor returns the group count, NOT 0.
     */
    EXPECT_EQ(0, adjRib1->stats_.getPostOutPrefixCount());
    EXPECT_EQ(
        100, adjRib1->adjRibOutGroup_->getStats().getPostOutPrefixCount());
    EXPECT_EQ(100, adjRib1->getEffectivePostOutPrefixCount());

    /*
     * Case 2: No update-group (update-group disabled or peer detached).
     * The accessor should return the per-peer count.
     */
    auto adjRib2 = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId2,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config);

    /*
     * Detach the group to simulate update-group disabled.
     * setupAdjRib always creates a group, so we null it out manually.
     */
    adjRib2->adjRibOutGroup_ = nullptr;

    /* Bump per-peer post-out count to 50 (IPv4). */
    for (uint32_t i = 0; i < 50; ++i) {
      adjRib2->incrementPostOutPrefixCount(/*isIpv4=*/true);
    }

    EXPECT_EQ(50, adjRib2->stats_.getPostOutPrefixCount());
    EXPECT_EQ(50, adjRib2->getEffectivePostOutPrefixCount());

    /*
     * Case 3: Detached peer (both group and per-peer counts set, per-peer
     * count is higher). The accessor should return the max.
     */
    auto adjRib3 = setupAdjRib(
        evb,
        vrf->getChangeListTracker(),
        kPeerId3,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config);

    /* Attach an update-group with a count of 20. */
    adjRib3->adjRibOutGroup_ =
        std::make_shared<AdjRibOutGroup>(evb, fboss::kDefaultVrf, "Group");
    for (uint32_t i = 0; i < 20; ++i) {
      adjRib3->adjRibOutGroup_->stats_.incrementPostOutPrefixCount(
          /*isIpv4=*/true);
    }

    /*
     * Per-peer count is 30 (higher than the group count).
     * This mimics a detached peer diverging from the group.
     */
    for (uint32_t i = 0; i < 30; ++i) {
      adjRib3->incrementPostOutPrefixCount(/*isIpv4=*/true);
    }

    EXPECT_EQ(30, adjRib3->stats_.getPostOutPrefixCount());
    EXPECT_EQ(20, adjRib3->adjRibOutGroup_->getStats().getPostOutPrefixCount());
    EXPECT_EQ(30, adjRib3->getEffectivePostOutPrefixCount());

    /*
     * These ribs are local (not owned by peerMgr->adjRibs_), so break the
     * AdjRib <-> changeListConsumer reference cycle explicitly before they go
     * out of scope; otherwise the ribs outlive the test and LeakSanitizer
     * fails the run.
     */
    adjRib1->resetChangeListConsumer();
    adjRib2->resetChangeListConsumer();
    adjRib3->resetChangeListConsumer();
  });

  fm.loopUntilNoReady();

  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/**
 * @brief  bgpd.num_live_peer_groups counts DISTINCT groups holding at least one
 *         session, so a group must survive until its last session leaves.
 */
TEST_F(PeerManagerTestFixture, LivePeerGroupsRefcountTest) {
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);

  auto liveGroups = []() {
    auto tcData = fb303::ThreadCachedServiceData::get();
    tcData->publishStats();
    return tcData->getCounter(BgpStats::kNumLivePeerGroups);
  };
  const std::string groupA{"GROUP_A"};
  const std::string groupB{"GROUP_B"};

  // Two sessions in one group count that group once
  mockPeerMgr->addLivePeerGroup(groupA);
  EXPECT_EQ(1, liveGroups());
  mockPeerMgr->addLivePeerGroup(groupA);
  EXPECT_EQ(1, liveGroups());

  mockPeerMgr->addLivePeerGroup(groupB);
  EXPECT_EQ(2, liveGroups());

  // GROUP_A still has one session left, so it stays live
  mockPeerMgr->removeLivePeerGroup(groupA);
  EXPECT_EQ(2, liveGroups());
  mockPeerMgr->removeLivePeerGroup(groupA);
  EXPECT_EQ(1, liveGroups());

  /*
   * A terminate for a session that was never counted (its establish was
   * dropped on a fast flap) must not underflow the surviving group.
   */
  mockPeerMgr->removeLivePeerGroup(groupA);
  EXPECT_EQ(1, liveGroups());

  mockPeerMgr->removeLivePeerGroup(groupB);
  EXPECT_EQ(0, liveGroups());
}

/**
 * @brief  The gauge is driven by real session state: it rises on
 *         sessionEstablished and falls again on sessionTerminated.
 */
TEST_F(PeerManagerTestFixture, LivePeerGroupsSessionLifecycleTest) {
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);
  uint64_t version = 0x100;
  auto versionNumber = std::make_shared<VersionNumber>(version);

  auto sessionMgrThread = sessionMgr->runInThread();

  auto& evb = mockPeerMgr->getEventBase();
  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);
  fm.addTask([&] {
    mockPeerMgr->getDefaultVrf().setRibInitPathComputationNotified(false);

    auto liveGroups = []() {
      auto tcData = fb303::ThreadCachedServiceData::get();
      tcData->publishStats();
      return tcData->getCounter(BgpStats::kNumLivePeerGroups);
    };

    auto mockInfo = mockInfo1_;
    mockInfo.peeringParams.peerGroupName = "PEERGROUP_LIVE_TEST";

    auto sessionInfo = FiberBgpPeer::getObservableSessionInfo(
        mockInfo,
        nullptr /* unbounded input queue, unused */,
        sessionMgr->boundedIqueue_,
        sessionMgr->oQueue_,
        versionNumber);

    FiberBgpPeer::ObservableStateT stateEvent{
        .peerId = kPeerId3,
        .versionNumber = version,
        .remoteAs = mockInfo.peeringParams.remoteAs,
        .sessionInfo = sessionInfo};

    folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEvent));
    auto adjRib = mockPeerMgr->findAdjRib(kPeerId3);
    ASSERT_EQ(true, adjRib->isStateEstablished());
    EXPECT_EQ(1, liveGroups());

    fiberSleepFor(20ms);

    sessionMgr->oQueue_->open();
    sessionMgr->oQueue_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    stateEvent.versionNumber = ++version;
    sessionInfo->currentVersion = std::make_shared<VersionNumber>(version);

    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEvent));
    ASSERT_EQ(false, adjRib->isStateEstablished());
    EXPECT_EQ(0, liveGroups());

    auto batonIt = mockPeerMgr->sessionTerminateBatons_.find(kPeerId3);
    ASSERT_NE(batonIt, mockPeerMgr->sessionTerminateBatons_.end());
    batonIt->second->post();

    folly::LoggerDB::get().getCategory("")->clearHandlers();

    // Explicitly stop the peer manager to cancel all coro tasks
    mockPeerMgr->stop();
    sessionMgr->stop();
  });

  evb.loop();
  sessionMgrThread.join();
  SUCCEED();
}

} // namespace bgp
} // namespace facebook
