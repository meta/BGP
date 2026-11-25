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

#define PeerManager_TEST_FRIENDS                                               \
  FRIEND_TEST(                                                                 \
      PeerManagerInitializationTestFixture, DuplicateRibEoRNotificationTest);  \
  FRIEND_TEST(                                                                 \
      PeerManagerInitializationTestFixture, InitializedSignalTimeoutTest);     \
  FRIEND_TEST(                                                                 \
      PeerManagerInitializationTestFixture,                                    \
      InitializedSignalWithoutIngressEoRTest);                                 \
  FRIEND_TEST(PeerManagerInitializationTestFixture, ProcessAdjRibMsgLoopTest); \
  FRIEND_TEST(                                                                 \
      PeerManagerInitializationTestFixture, InitializedSignalPublicationTest);

#include <folly/fibers/FiberManagerMap.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/config/ConfigManager.h"
#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/BoundedWaitUtils.h"
#include "neteng/fboss/bgp/cpp/tests/PeerManagerTestUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"

using ::testing::_;
using namespace facebook::neteng::fboss::bgp::thrift;

namespace facebook::bgp {

class PeerManagerInitializationTestFixture : public PeerManagerTestFixture {
 public:
  void SetUp() override {
    PeerManagerTestFixture::SetUp();

    // reset all of the cached data
    facebook::fb303::ThreadCachedServiceData::getShared()->clearCounter(
        BgpStats::kEorTimerExpired);
    facebook::fb303::ThreadCachedServiceData::getShared()->clearCounter(
        fmt::format(
            kInitEventCounterFormat,
            apache::thrift::util::enumNameSafe(
                BgpInitializationEvent::ALL_EOR_RECEIVED)));
    facebook::fb303::ThreadCachedServiceData::getShared()->clearCounter(
        fmt::format(
            kInitEventCounterFormat,
            apache::thrift::util::enumNameSafe(
                BgpInitializationEvent::EOR_TIMER_EXPIRED)));
    facebook::fb303::ThreadCachedServiceData::getShared()->clearCounter(
        fmt::format(
            kInitEventCounterFormat,
            apache::thrift::util::enumNameSafe(
                BgpInitializationEvent::INITIALIZED)));
  }
};

/*
 * This test:
 *  - sets a short eorTime (1s) to mimick EoR expiration case
 *  - explicitly calls notifyRibInitialPathComputation(false) to trigger
 * ALL_EOR_RECEIVED
 *  - makes sure that the subsequent call is a no-op and does not reset
 *    eorTimerExpired flag
 */
TEST_F(PeerManagerInitializationTestFixture, DuplicateRibEoRNotificationTest) {
  // counters for ThreadCachedData
  std::map<std::string, int64_t> counters;

  folly::EventBase testEvb;
  const auto eorTimeS{1};
  auto config = getConfig(
      true /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */,
      false /* includeDynamicVipInjectorPeer */,
      false /* enableStatefulHa */,
      false /* enableVipServer */,
      eorTimeS);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto sessionMgr =
      std::make_shared<SessionManager>(*config->getBgpGlobalConfig(), false);
  peerMgr->setSessionManager(sessionMgr);

  /*
   * create peer manager thread
   * Attention: this will internally pump evb_
   */
  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  testEvb.scheduleAt(
      [&]() noexcept {
        // dump counters to check
        facebook::fb303::ThreadCachedServiceData::getShared()->getCounters(
            counters);

        // eorTimer expired and populates EOR_TIMER_EXPIRED event
        EXPECT_TRUE(counters.contains(BgpStats::kEorTimerExpired));
        EXPECT_EQ(1, counters.at(BgpStats::kEorTimerExpired));

        /*
         * explicitly issue duplicate notifyEoR with no expiration. Make sure
         * it is a no-op.
         */
        peerMgr->getDefaultVrf().notifyRibInitialPathComputation(
            /*timerFired=*/false);

        // dump counters to check
        facebook::fb303::ThreadCachedServiceData::getShared()->getCounters(
            counters);
        EXPECT_EQ(1, counters.at(BgpStats::kEorTimerExpired));
      },
      std::chrono::steady_clock::now() + std::chrono::seconds(2 * eorTimeS));

  // let magic happen
  testEvb.loop();

  peerMgr->stop();
  sessionMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/*
 * A VRF other than the default gets its own EoR timer. A static peer in that
 * VRF that never sends EoR must not hold back the VRF's initial RIB path
 * computation past eor_time_s, and the VRF's initialized max-wait timer must
 * start then.
 */
TEST_F(PeerManagerInitializationTestFixture, NonDefaultVrfEorTimerTest) {
  folly::EventBase testEvb;
  const auto eorTimeS{1};
  auto config = getConfig(
      true /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */,
      false /* includeDynamicVipInjectorPeer */,
      false /* enableStatefulHa */,
      false /* enableVipServer */,
      eorTimeS);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  const VrfId vrfId{5};
  auto& vrf = peerMgr->createVrf(vrfId, /*publishStats=*/false);
  vrf.getStaticPeerEoRReceived().emplace(
      folly::IPAddress("10.5.0.1"), std::make_pair(false, false));

  auto sessionMgr =
      std::make_shared<SessionManager>(*config->getBgpGlobalConfig(), false);
  peerMgr->setSessionManager(sessionMgr);

  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  testEvb.scheduleAt(
      [&]() noexcept {
        peerMgr->getEventBase().runInEventBaseThreadAndWait([&]() {
          EXPECT_TRUE(vrf.isRibInitPathComputationNotified());
          EXPECT_TRUE(vrf.isEorTimerExpired());
          EXPECT_FALSE(vrf.isInitialized());
          ASSERT_NE(nullptr, vrf.getInitializedMaxWaitTimer());
          EXPECT_TRUE(vrf.getInitializedMaxWaitTimer()->isScheduled());
        });
      },
      std::chrono::steady_clock::now() + std::chrono::seconds(2 * eorTimeS));

  testEvb.loop();

  peerMgr->stop();
  sessionMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
}

/*
 * This test set a short EoR time (1s) to simulate:
 *  1) EOR_TIMER_EXPIRED event(typically takes 120s)
 *  2) INITITALIZED event publication when max timer is hit.
 *
 * This test schedules lambda callbacks with a dedicated test eventbase to
 * control the timestamp to do check.
 */
TEST_F(PeerManagerInitializationTestFixture, InitializedSignalTimeoutTest) {
  // counters for ThreadCachedData
  std::map<std::string, int64_t> counters;

  folly::EventBase testEvb;
  const auto eorTimeS{1};
  auto config = getConfig(
      true /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */,
      false /* includeDynamicVipInjectorPeer */,
      false /* enableStatefulHa */,
      false /* enableVipServer */,
      eorTimeS);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto sessionMgr =
      std::make_shared<SessionManager>(*config->getBgpGlobalConfig(), false);
  peerMgr->setSessionManager(sessionMgr);

  /*
   * create peer manager thread
   * Attention: this will internally pump evb_
   */
  auto peerMgrThread = peerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  testEvb.scheduleAt(
      [&]() noexcept {
        // dump counters to check
        facebook::fb303::ThreadCachedServiceData::getShared()->getCounters(
            counters);
        // No peer sends EoR
        EXPECT_FALSE(counters.contains(
            fmt::format(
                kInitEventCounterFormat,
                apache::thrift::util::enumNameSafe(
                    BgpInitializationEvent::ALL_EOR_RECEIVED))));

        // eorTimer expired and populates EOR_TIMER_EXPIRED event
        EXPECT_TRUE(counters.contains(
            fmt::format(
                kInitEventCounterFormat,
                apache::thrift::util::enumNameSafe(
                    BgpInitializationEvent::EOR_TIMER_EXPIRED))));
      },
      std::chrono::steady_clock::now() + std::chrono::seconds(2 * eorTimeS));

  // double the waiting time of eor_time to expect INITIALIZED signal
  testEvb.scheduleAt(
      [&]() noexcept {
        // dump counters to check
        facebook::fb303::ThreadCachedServiceData::getShared()->getCounters(
            counters);

        // initializedMaxWaitTimer_ expired and populates INITIALIZED event
        EXPECT_TRUE(counters.contains(
            fmt::format(
                kInitEventCounterFormat,
                apache::thrift::util::enumNameSafe(
                    BgpInitializationEvent::INITIALIZED))));

        // initialized_ flag is set
        EXPECT_TRUE(peerMgr->getIsInitialized());

        // no adjRib spawn, hence no prefix sent
        EXPECT_TRUE(counters.contains(facebook::bgp::BgpStats::kNoPrefixSent));
        EXPECT_EQ(1, counters.at(facebook::bgp::BgpStats::kNoPrefixSent));

        // terminate eventbase to unblock termination sequence
        testEvb.terminateLoopSoon();
      },
      std::chrono::steady_clock::now() + std::chrono::seconds(10 * eorTimeS));

  // let magic happen
  testEvb.loop();

  peerMgr->stop();
  sessionMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/*
 * This test simulates a scenario that initialized signal is published when
 *  1. the ingress EoR has not been received from all peers
 *  2. egressEoR received from one or some peers.
 *
 * Make sure BGP will ignore the peers without ingress EoR received and mark
 * initialization. This is to make sure BGP only waits for up peers and does
 * not wait endlessly.
 */
TEST_F(
    PeerManagerInitializationTestFixture,
    InitializedSignalWithoutIngressEoRTest) {
  std::vector<folly::Future<folly::Unit>> taskFutures;

  // counters for ThreadCachedData
  std::map<std::string, int64_t> counters;

  // create config for static peers only
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  setupMockSessionManager(mockPeerMgr);
  auto sessionMgr = mockPeerMgr->getSessionManager();

  EXPECT_EQ(2, config_->getPeerToConfig().size());

  // adds peers to sessionMgr
  mockPeerMgr->addPeersToSessionMgr();

  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);
  auto fiber = fm.addTaskFuture([&] {
    // make sure EoR won't be sent
    EXPECT_FALSE(
        mockPeerMgr->getDefaultVrf().isRibInitPathComputationNotified());

    mockPeerMgr->getDefaultVrf().setRibInitialAnnouncementStarted(true);
    // Send egressEoR from one static peer without ingressEoR sent
    AdjRib::ObservableMessageT eoRToStaticPeer1{
        fboss::kDefaultVrf, kPeerId3, AdjRib::EgressEoR{}};
    folly::coro::blockingWait(
        mockPeerMgr->processAdjRibEvent(std::move(eoRToStaticPeer1)));

    // No ribEoRNotified since ingress EoR was not received
    EXPECT_FALSE(
        mockPeerMgr->getDefaultVrf().isRibInitPathComputationNotified());

    // INITIALIZED signal should be published by ignoring those peers
    facebook::fb303::ThreadCachedServiceData::getShared()->getCounters(
        counters);
    EXPECT_TRUE(counters.contains(
        fmt::format(
            kInitEventCounterFormat,
            apache::thrift::util::enumNameSafe(
                BgpInitializationEvent::INITIALIZED))));
  });
  taskFutures.emplace_back(std::move(fiber));

  // create peer manager thread and implicitly pump evb_
  auto peerMgrThread = mockPeerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  //  wait for all futures to be executed
  folly::collectAll(taskFutures.begin(), taskFutures.end()).get();

  mockPeerMgr->stop();
  sessionMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/*
 * This test starts a coro task for processAdjRibMsgLoop to handle the
 * following messages:
 *  - AdjRib::EoR
 *  - AdjRib::EgressEoR
 *  - AdjRib::Shutdown
 */
TEST_F(PeerManagerInitializationTestFixture, ProcessAdjRibMsgLoopTest) {
  // create and setup MockPeerManager instance
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  setupMockSessionManager(mockPeerMgr);
  auto sessionMgr = mockPeerMgr->getSessionManager();

  auto& evb = mockPeerMgr->getEventBase();
  mockPeerMgr->addPeersToSessionMgr();

  auto sessionMgrThread = sessionMgr->runInThread();

  // counters for ThreadCachedData
  std::map<std::string, int64_t> counters;

  // Synchronization primitive
  folly::fibers::Baton stopPeerBaton;
  std::vector<folly::Future<folly::Unit>> taskFutures;

  // start corotine task
  folly::coro::CancellableAsyncScope asyncScope;
  asyncScope.add(co_withExecutor(&evb, mockPeerMgr->processAdjRibMsgLoop()));

  // fiber task to inject EgressEoR event
  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);
  auto task = fm.addTaskFuture([&]() {
    AdjRib::ObservableMessageT eoRPeerId3{
        fboss::kDefaultVrf, kPeerId3, AdjRib::EoR{}};
    AdjRib::ObservableMessageT eoRPeerId4{
        fboss::kDefaultVrf, kPeerId4, AdjRib::EoR{}};

    // inject both eoRs into the queue
    mockPeerMgr->fromAdjRibQ_.push(eoRPeerId3);
    mockPeerMgr->fromAdjRibQ_.push(eoRPeerId4);

    // trigger destruction sequence
    stopPeerBaton.post();
  });
  taskFutures.emplace_back(std::move(task));

  std::thread stopPeerThread([&]() {
    facebook::bgp::test::boundedBatonWait(stopPeerBaton, "stopPeerBaton");

    const auto eorReceivedStr = fmt::format(
        kInitEventCounterFormat,
        apache::thrift::util::enumNameSafe(
            BgpInitializationEvent::ALL_EOR_RECEIVED));

    // retrying to check the counter publication
    WITH_RETRIES({
      facebook::fb303::ThreadCachedServiceData::getShared()->getCounters(
          counters);
      ASSERT_EVENTUALLY_TRUE(counters.contains(eorReceivedStr));
    });

    // stop peerMgr
    mockPeerMgr->stop();
    sessionMgr->stop();

    // cancel coroutines
    folly::coro::blockingWait(asyncScope.cancelAndJoinAsync());
  });

  evb.loop();
  stopPeerThread.join();
  sessionMgrThread.join();
  folly::collectAll(taskFutures.begin(), taskFutures.end()).get();
}

/*
 * This test create both static and dynamic peers (> 1). It verifies
 *  - ALL_EOR_RECEIVED won't be published if only 1 EoR is received.
 *  - ALL_EOR_RECEIVED will be published if all peers' EoR is received.
 *  - EOR_TIMER_EXPIRATION won't be published.
 *  - INITIALIZED will be published if all peers' EgressEoR is sent.
 */
TEST_F(PeerManagerInitializationTestFixture, InitializedSignalPublicationTest) {
  // counters for ThreadCachedData
  std::map<std::string, int64_t> counters;

  std::vector<folly::Future<folly::Unit>> taskFutures;

  // create config both static and dynamic peer config
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  setupMockSessionManager(mockPeerMgr);
  auto sessionMgr = mockPeerMgr->getSessionManager();

  const auto& dynamicPeerToConfig = config_->getDynamicPeerToConfig();
  EXPECT_EQ(2, dynamicPeerToConfig.size());
  const auto& staticPeerToConfig = config_->getPeerToConfig();
  EXPECT_EQ(2, staticPeerToConfig.size());

  // adds peers to sessionMgr
  mockPeerMgr->addPeersToSessionMgr();

  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);
  auto fiber = fm.addTaskFuture([&] {
    // make sure EoR won't be sent
    facebook::fb303::ThreadCachedServiceData::getShared()->getCounters(
        counters);

    EXPECT_FALSE(
        mockPeerMgr->getDefaultVrf().isRibInitPathComputationNotified());
    EXPECT_FALSE(counters.contains(
        fmt::format(
            kInitEventCounterFormat,
            apache::thrift::util::enumNameSafe(
                BgpInitializationEvent::ALL_EOR_RECEIVED))));

    AdjRib::ObservableMessageT EoRFromStaticPeer1{
        fboss::kDefaultVrf, kPeerId3, AdjRib::EoR{}};
    folly::coro::blockingWait(
        mockPeerMgr->processAdjRibEvent(std::move(EoRFromStaticPeer1)));

    // make sure EoR won't be sent if does not receive all EoRs
    facebook::fb303::ThreadCachedServiceData::getShared()->getCounters(
        counters);
    EXPECT_FALSE(
        mockPeerMgr->getDefaultVrf().isRibInitPathComputationNotified());
    EXPECT_FALSE(counters.contains(
        fmt::format(
            kInitEventCounterFormat,
            apache::thrift::util::enumNameSafe(
                BgpInitializationEvent::ALL_EOR_RECEIVED))));

    AdjRib::ObservableMessageT EoRFromStaticPeer2{
        fboss::kDefaultVrf, kPeerId4, AdjRib::EoR{}};
    folly::coro::blockingWait(
        mockPeerMgr->processAdjRibEvent(std::move(EoRFromStaticPeer2)));

    /*
     * make sure EoR will be sent finally without waiting for eorTimer
     * (120sec) expiration. PM is constructed with the default
     * requireNexthopResolution=false, so RIB computes immediately; the
     * nexthop-resolution gating (when opted in) is covered in RibTest.
     */
    facebook::fb303::ThreadCachedServiceData::getShared()->getCounters(
        counters);
    EXPECT_TRUE(
        mockPeerMgr->getDefaultVrf().isRibInitPathComputationNotified());
    EXPECT_TRUE(counters.contains(
        fmt::format(
            kInitEventCounterFormat,
            apache::thrift::util::enumNameSafe(
                BgpInitializationEvent::ALL_EOR_RECEIVED))));
    EXPECT_FALSE(counters.contains(
        fmt::format(
            kInitEventCounterFormat,
            apache::thrift::util::enumNameSafe(
                BgpInitializationEvent::EOR_TIMER_EXPIRED))));

    mockPeerMgr->getDefaultVrf().setRibInitialAnnouncementStarted(true);
    // Send egressEoR from one static peer
    AdjRib::ObservableMessageT eoRToStaticPeer1{
        fboss::kDefaultVrf, kPeerId3, AdjRib::EgressEoR{}};
    folly::coro::blockingWait(
        mockPeerMgr->processAdjRibEvent(std::move(eoRToStaticPeer1)));

    // Send egressEoR from another static peer
    AdjRib::ObservableMessageT eoRToStaticPeer2{
        fboss::kDefaultVrf, kPeerId4, AdjRib::EgressEoR{}};
    folly::coro::blockingWait(
        mockPeerMgr->processAdjRibEvent(std::move(eoRToStaticPeer2)));

    facebook::fb303::ThreadCachedServiceData::getShared()->getCounters(
        counters);
    EXPECT_TRUE(counters.contains(
        fmt::format(
            kInitEventCounterFormat,
            apache::thrift::util::enumNameSafe(
                BgpInitializationEvent::INITIALIZED))));

    // Verify that convergence value is set.
    EXPECT_TRUE(getConvergenceTimeMs());
    // Expect value greater than zero.
    EXPECT_LT(0, *getConvergenceTimeMs());
  });
  taskFutures.emplace_back(std::move(fiber));

  // create peer manager thread and implicitly pump evb_
  auto peerMgrThread = mockPeerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  //  wait for all futures to be executed
  folly::collectAll(taskFutures.begin(), taskFutures.end()).get();

  mockPeerMgr->stop();
  sessionMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/*
 * Note: the nexthop-resolution gating for the initial path computation now
 * lives entirely in RIB (RibBase::processRibInInitialPathComputation defers the
 * initial full-sync until all registered nexthops resolve, bounded by the
 * remaining EoR budget PeerManagerBase passes in RibInInitialPathComputation).
 * See RibTest's InitialPathComputationDefersUntilNexthopResolved /
 * InitialPathComputationRunsImmediatelyWithZeroTimeout for that coverage.
 * PeerManagerBase's own responsibility — triggering the computation once all
 * peer EoRs are received — is covered by the BasicInitializationFlow test
 * above.
 */

} // namespace facebook::bgp
