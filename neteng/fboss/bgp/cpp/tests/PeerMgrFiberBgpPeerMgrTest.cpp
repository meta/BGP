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
 * Forward-declare test classes so the friend declarations in
 * FiberBgpPeerManager.h resolve across namespaces.
 */
namespace facebook::bgp {
class PeerManagerTestFixture_DelPeersTest_Test;
class PeerManagerTestFixture_DelPeersNonExistentSkipsTest_Test;
class PeerManagerTestFixture_CleanupPeerState_DelPeersIntegration_Test;
class PeerManagerTestFixture_DelPeers_NotEstablishedPeer_FallbackCleansUp_Test;
class PeerManagerTestFixture_DelPeers_AfterFlap_FallbackCleansUp_Test;
class PeerManagerTestFixture_SessionTerminated_VipWithoutAdjRibKeepsPeer_Test;
class PeerManagerTestFixture_SessionTerminated_ShutdownSkipsProcessing_Test;
} // namespace facebook::bgp

#define FiberBgpPeerManager_TEST_FRIENDS                                        \
  friend class facebook::bgp::PeerManagerTestFixture_DelPeersTest_Test;         \
  friend class facebook::bgp::                                                  \
      PeerManagerTestFixture_DelPeersNonExistentSkipsTest_Test;                 \
  friend class facebook::bgp::                                                  \
      PeerManagerTestFixture_CleanupPeerState_DelPeersIntegration_Test;         \
  friend class facebook::bgp::                                                  \
      PeerManagerTestFixture_DelPeers_NotEstablishedPeer_FallbackCleansUp_Test; \
  friend class facebook::bgp::                                                  \
      PeerManagerTestFixture_DelPeers_AfterFlap_FallbackCleansUp_Test;          \
  friend class facebook::bgp::                                                  \
      PeerManagerTestFixture_SessionTerminated_VipWithoutAdjRibKeepsPeer_Test;  \
  friend class facebook::bgp::                                                  \
      PeerManagerTestFixture_SessionTerminated_ShutdownSkipsProcessing_Test;

#define PeerManager_TEST_FRIENDS                                              \
  FRIEND_TEST(PeerManagerTestFixture, AddPeersToSessionMgrTest);              \
  FRIEND_TEST(PeerManagerTestFixture, DelPeersTest);                          \
  FRIEND_TEST(PeerManagerTestFixture, DelPeersNonExistentSkipsTest);          \
  FRIEND_TEST(                                                                \
      PeerManagerTestFixture,                                                 \
      SessionEstablishTerminateWithoutPeerConfiguredTest);                    \
  FRIEND_TEST(PeerManagerTestFixture, WaitForSessionTerminateBatonTest);      \
  FRIEND_TEST(                                                                \
      PeerManagerTestFixture, RapidSessionFlapWithVersionCompressionTest);    \
  FRIEND_TEST(PeerManagerTestFixture, CleanupPeerState_NeverEstablished);     \
  FRIEND_TEST(PeerManagerTestFixture, CleanupPeerState_BatonWaitCompletes);   \
  FRIEND_TEST(PeerManagerTestFixture, CleanupPeerState_GenerationCheck);      \
  FRIEND_TEST(                                                                \
      PeerManagerTestFixture, CleanupPeerState_GenerationCheckDuringStop);    \
  FRIEND_TEST(PeerManagerTestFixture, CleanupPeerState_NormalErase);          \
  FRIEND_TEST(                                                                \
      PeerManagerTestFixture, CleanupPeerState_ResetsChangeListConsumer);     \
  FRIEND_TEST(                                                                \
      PeerManagerTestFixture, SessionTerminated_PeerDeleteRunsCleanupInline); \
  FRIEND_TEST(                                                                \
      PeerManagerTestFixture, SessionTerminated_VipWithoutAdjRibKeepsPeer);   \
  FRIEND_TEST(                                                                \
      PeerManagerTestFixture, SessionTerminated_VipErasesDynamicEorState);    \
  FRIEND_TEST(                                                                \
      PeerManagerTestFixture,                                                 \
      SessionTerminated_VipWithGrKeepsAdjRibAndSession);                      \
  FRIEND_TEST(                                                                \
      PeerManagerTestFixture, SessionTerminated_ShutdownSkipsProcessing);     \
  FRIEND_TEST(                                                                \
      PeerManagerTestFixture,                                                 \
      SessionTerminated_StaticPeerWithVipAsnKeepsAdjRib);                     \
  FRIEND_TEST(                                                                \
      PeerManagerTestFixture, SessionTerminated_NoPeerDeleteKeepsAdjRib);     \
  FRIEND_TEST(                                                                \
      PeerManagerTestFixture, DelPeers_NotEstablishedPeer_FallbackCleansUp);  \
  FRIEND_TEST(PeerManagerTestFixture, DelPeers_AfterFlap_FallbackCleansUp);   \
  FRIEND_TEST(PeerManagerTestFixture, CleanupPeerState_DelPeersIntegration);

#include <thread>

#include <folly/coro/Baton.h>
#include <folly/coro/Collect.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Invoke.h>
#include <folly/fibers/BatchSemaphore.h>
#include <folly/fibers/FiberManagerMap.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/PeerManagerTestUtils.h"

using namespace facebook::nettools::bgplib;
using namespace folly::fibers;
using ::testing::_;

namespace facebook::bgp {

/*
 * Test function to add peers to fiberBgpPeerManager, aka, sessionMgr.
 * Expect mock fiberBgpPeerMgr to have internal addPeer_ called.
 */
TEST_F(PeerManagerTestFixture, AddPeersToSessionMgrTest) {
  // reset all of the cached data
  facebook::fb303::ThreadCachedServiceData::getShared()->zeroStats();

  // counters for ThreadCachedData
  std::map<std::string, int64_t> counters;

  // create mock peerMgr along with mock fiberBgpPeerMgr
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto& evb = mockPeerMgr->getEventBase();
  auto config = config_;
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);

  auto dynamicPeerToConfig = config->getDynamicPeerToConfig();
  EXPECT_EQ(2, dynamicPeerToConfig.size());
  auto peerToConfig = config->getPeerToConfig();
  EXPECT_EQ(2, peerToConfig.size());

  // get PeeringParams and set expectations
  auto params1 = config->getPeeringParamsForDynamicPeer(
      *dynamicPeerToConfig.at(kPeerPrefix1));
  EXPECT_CALL(*sessionMgr, addDynamicPeer_(kPeerPrefix1, params1)).Times(1);
  auto params2 = config->getPeeringParamsForDynamicPeer(
      *dynamicPeerToConfig.at(kPeerPrefix2));
  EXPECT_CALL(*sessionMgr, addDynamicPeer_(kPeerPrefix2, params2)).Times(1);
  auto params3 = config->getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr3));
  EXPECT_CALL(*sessionMgr, addPeer_(kPeerAddr3, params3, _)).Times(1);
  auto params4 = config->getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr4));
  EXPECT_CALL(*sessionMgr, addPeer_(kPeerAddr4, params4, _)).Times(1);

  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);
  fm.addTask([&] {
    // adds peers to sessionMgr
    mockPeerMgr->addPeersToSessionMgr();
    facebook::fb303::ThreadCachedServiceData::getShared()->getCounters(
        counters);

    EXPECT_EQ(2, mockPeerMgr->staticPeerEoRReceived_.size());

    // dynamicPeerEoR size will be empty since there is no GR subnet reloaded
    EXPECT_EQ(0, counters.at(BgpStats::kStatefulGR));
    EXPECT_EQ(0, mockPeerMgr->dynamicPeerEoRReceived_.size());

    // Verify ODS counter for allPeers_
    auto tcData = fb303::ThreadCachedServiceData::get();
    tcData->publishStats();
    EXPECT_EQ(2, tcData->getCounter(BgpStats::kAllPeersCount));
  });

  evb.loop();
  SUCCEED();
}

/*
 * Test delPeers: populate allPeers_ directly, then remove them via delPeers
 * coroutine. Verifies peers are removed from the session manager.
 */
CO_TEST_F(PeerManagerTestFixture, DelPeersTest) {
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);

  /*
   * Directly populate allPeers_ to avoid spawning real FiberBgpPeer fibers
   * (which hang during shutdown in a mock environment)
   */
  auto peerToConfig = config_->getPeerToConfig();
  auto params3 = config_->getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr3));
  auto params4 = config_->getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr4));
  sessionMgr->allPeers_[kPeerAddr3] =
      std::make_shared<nettools::bgplib::BgpPeerInfoInternal>(
          nettools::bgplib::BgpPeerInfoInternal{params3, {}, {}});
  sessionMgr->allPeers_[kPeerAddr4] =
      std::make_shared<nettools::bgplib::BgpPeerInfoInternal>(
          nettools::bgplib::BgpPeerInfoInternal{params4, {}, {}});

  // Run session manager's evb in a thread so co_dropPeer can dispatch to it
  auto& sessionEvb = sessionMgr->getEventBase();
  auto sessionThread = std::thread([&sessionEvb] { sessionEvb.loopForever(); });
  sessionEvb.waitUntilRunning();

  // Remove one peer
  auto result = co_await mockPeerMgr->delPeers({kPeerAddr3});
  EXPECT_TRUE(result.hasValue());

  // Verify peer is gone: dropping again should be a no-op (skipped)
  auto result2 = co_await mockPeerMgr->delPeers({kPeerAddr3});
  EXPECT_TRUE(result2.hasValue());

  // Remove the second peer
  auto result3 = co_await mockPeerMgr->delPeers({kPeerAddr4});
  EXPECT_TRUE(result3.hasValue());

  sessionEvb.terminateLoopSoon();
  sessionThread.join();
}

/*
 * Test delPeers with non-existent peers: verifies PEER_DOES_NOT_EXIST
 * is skipped and other peers are still removed.
 */
CO_TEST_F(PeerManagerTestFixture, DelPeersNonExistentSkipsTest) {
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);

  // Only add kPeerAddr3 to test mix of existing and non-existent
  auto peerToConfig = config_->getPeerToConfig();
  auto params3 = config_->getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr3));
  sessionMgr->allPeers_[kPeerAddr3] =
      std::make_shared<nettools::bgplib::BgpPeerInfoInternal>(
          nettools::bgplib::BgpPeerInfoInternal{params3, {}, {}});

  auto& sessionEvb = sessionMgr->getEventBase();
  auto sessionThread = std::thread([&sessionEvb] { sessionEvb.loopForever(); });
  sessionEvb.waitUntilRunning();

  /*
   * Remove a mix of existing and non-existent peers.
   * Non-existent peer should be skipped, existing peer removed.
   */
  auto result = co_await mockPeerMgr->delPeers(
      {folly::IPAddress("192.168.99.99"), kPeerAddr3});
  EXPECT_TRUE(result.hasValue());

  // Verify kPeerAddr3 was removed: dropping again is a no-op
  auto verify = co_await mockPeerMgr->delPeers({kPeerAddr3});
  EXPECT_TRUE(verify.hasValue());

  // Empty list is a no-op
  auto result2 = co_await mockPeerMgr->delPeers({});
  EXPECT_TRUE(result2.hasValue());

  sessionEvb.terminateLoopSoon();
  sessionThread.join();
}

/*
 * This test verifies the behavior of sessionEstablished()/sessionTerminated()
 * call inside PeerManagerBase when the peer is not configured.
 *  - If sessionEstablished is called without peer configured, peerMgr will
 *    return early and ignore the transition.
 *  - If sessionTerminated is called when no sesion established, peerMgr will
 *    ignore and make it a no-op.
 */
TEST_F(
    PeerManagerTestFixture,
    SessionEstablishTerminateWithoutPeerConfiguredTest) {
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);

  // Collect running sessins number before
  auto runningSessionBefore = mockPeerMgr->runningSessions_;

  // Simulate that peerVersion is no longer valid when established is called.
  uint64_t version = 0x100;
  auto versionNumber = std::make_shared<VersionNumber>(0);
  auto oQueue = std::make_shared<AdjRib::AdjRibInQueueT>();
  auto biQueue = std::make_shared<AdjRib::AdjRibOutQueueT>(
      kMaxEgressQueueSize, kEgressQueueHighWatermark, kEgressQueueLowWatermark);

  auto& evb = mockPeerMgr->getEventBase();
  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);
  fm.addTask([&] {
    auto sessionInfo = FiberBgpPeer::getObservableSessionInfo(
        mockInfo1_,
        nullptr /* unbounded input queue, unused */,
        biQueue,
        oQueue,
        versionNumber);

    FiberBgpPeer::ObservableStateT stateEvent{
        .peerId = kPeerId3,
        .state = BgpSessionState::ESTABLISHED,
        .versionNumber = version,
        .remoteAs = mockInfo1_.peeringParams.remoteAs,
        .sessionInfo = sessionInfo};

    (void)mockPeerMgr->sessionEstablished(stateEvent);

    // Verify that adjRib is not created
    auto adjRib = mockPeerMgr->findAdjRib(kPeerId3);
    EXPECT_FALSE(adjRib);

    // Verify running peers counter is never set for established
    auto runningSessionAfter = mockPeerMgr->runningSessions_;
    EXPECT_EQ(runningSessionBefore, runningSessionAfter);

    // Verify that terminate message is ignored too
    stateEvent.state = BgpSessionState::IDLE;
    folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEvent));
    EXPECT_EQ(runningSessionAfter, mockPeerMgr->runningSessions_);

    // stop mockPeerMgr to guarantee all scheduled coro tasks are finished
    mockPeerMgr->stop();
  });

  evb.loop();
  SUCCEED();
}

/*
 * This test verifies that PeerManagerBase::waitForSessionTerminateBaton()
 * blocks until the semaphore is signaled. This mimics the case where a
 * session re-establishes while a previous incarnation is still shutting down.
 *
 * The expectation is:
 *  - waitForSessionTerminateBaton() blocks waiting for 2 signals
 *  - When the semaphore is signaled (e.g., when adjRib message loops exit),
 *    the coroutine unblocks and proceeds
 */
TEST_F(PeerManagerTestFixture, WaitForSessionTerminateBatonTest) {
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);
  auto& evb = peerMgr->getEventBase();

  /*
   * Ensure FiberManager is attached to evb (required for
   * BatchSemaphore::co_wait)
   */
  auto& fm = folly::fibers::getFiberManager(evb, options_);

  // Create baton (starts un-posted / blocked)
  auto terminateBaton = std::make_shared<folly::coro::Baton>();

  // Register the baton in peerManager for kPeerId3
  peerMgr->sessionTerminateBatons_[kPeerId3] = terminateBaton;

  std::atomic<bool> waitCompleted{false};
  folly::fibers::BatchSemaphore baton(0);

  // Fiber task that waits for the coroutine to start, then posts the baton
  fm.addTask([&terminateBaton, &baton] {
    // Wait for the coroutine to start
    baton.wait(1);
    // Post the baton to unblock waitForSessionTerminateBaton
    terminateBaton->post();
  });

  // Schedule waitForSessionTerminateBaton coroutine
  auto waitTask = [&]() -> folly::coro::Task<void> {
    // Signal baton to let fiber know we're about to wait
    baton.signal(1);
    // Wait for baton (latch semantics: passes through if already posted)
    co_await peerMgr->waitForSessionTerminateBaton(kPeerId3);
    waitCompleted = true;
    co_return;
  };

  folly::coro::co_withExecutor(&evb, waitTask()).start();

  // Run the event loop until all work is done
  evb.loop();

  // After evb.loop() returns, wait should have completed
  EXPECT_TRUE(waitCompleted);

  SUCCEED();
}

TEST(FiberManager, BatchSemaphoreWaitAndSignal) {
  folly::EventBase evb;
  auto& fiberManager = folly::fibers::getFiberManager(evb);
  folly::fibers::BatchSemaphore sem(0);
  folly::fibers::BatchSemaphore baton(0);
  std::atomic<bool> fiber_unblocked{false};
  // Schedule the fiber task on the FiberManager
  fiberManager.addTask([&] {
    XLOG(INFO, "Signal baton to unblock coro task");
    baton.signal(1);
    XLOG(INFO, "Wait for semaphore token from coro task");
    sem.wait(1);
    fiber_unblocked = true;
  });
  // Coroutine task that signals the semaphore
  auto coroTask = [&sem, &baton]() -> folly::coro::Task<void> {
    XLOG(INFO, "Wait for fiber task to run first");
    co_await baton.co_wait(1);
    XLOG(INFO, "Signal fiber task for semaphore token");
    sem.signal(1);
    co_return;
  };
  // Schedule the coroutine task on the EventBase
  folly::coro::co_withExecutor(&evb, coroTask()).start();
  // Run the EventBase loop until all work is done
  evb.loop();
  // Validate that the fiber was unblocked
  EXPECT_TRUE(fiber_unblocked);
}

/*
 * This test verifies that the folly::coro::Baton latch semantics prevent hangs
 * during rapid session flaps with version compression (S619541).
 *
 * The failure scenario with the old BatchSemaphore:
 *  1. Session DOWN  -> AdjRib loops exit -> semaphore gets 2 signals
 *  2. Session UP (stale version) -> wait(2) CONSUMES 2 tokens -> early return
 *  3. Session UP (valid version) -> wait(2) -> 0 tokens -> HANGS FOREVER
 *
 * With folly::coro::Baton (latch semantics):
 *  1. Session DOWN  -> baton posted
 *  2. Session UP (stale version) -> co_await *baton passes through -> early
 *     return (baton NOT reset because version check failed)
 *  3. Session UP (valid version) -> co_await *baton passes through again
 *     (baton still posted) -> version check passes -> baton reset -> proceed
 *
 * The test directly exercises waitForSessionTerminateBaton() twice after
 * a single post() without an intermediate reset(), verifying latch behavior.
 */
CO_TEST_F(PeerManagerTestFixture, RapidSessionFlapWithVersionCompressionTest) {
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  // Create baton (starts un-posted)
  auto terminateBaton = std::make_shared<folly::coro::Baton>();

  // Register the baton in peerManager for kPeerId3
  peerMgr->sessionTerminateBatons_[kPeerId3] = terminateBaton;

  /*
   * Step 1: Simulate session DOWN -- AdjRib loops exit and post the baton.
   * In production, postTerminateBaton() posts after both loops signal the
   * local semaphore. Here we directly post() to simulate that.
   */
  terminateBaton->post();

  /*
   * Step 2: Session UP (stale version) -- PeerManagerBase waits for the
   * previous session's loops to terminate. With latch semantics, this
   * passes through immediately because the baton is still posted.
   */
  co_await peerMgr->waitForSessionTerminateBaton(kPeerId3);

  /*
   * Simulate: version check fails -> early return.
   * Critically, we do NOT reset the baton here. In production,
   * baton->reset() only happens after the version check passes
   * (inside adjRib->sessionEstablished()). A stale version causes
   * an early return, leaving the baton posted.
   */

  /*
   * Step 3: Session UP (valid version) -- Another session established
   * event arrives. PeerManagerBase again waits for termination.
   * With latch semantics, co_await *baton passes through immediately
   * because the baton is STILL posted (no reset on early return).
   * With the old BatchSemaphore, wait(2) in step 2 consumed the tokens,
   * so this second wait(2) would hang forever with 0 tokens.
   */
  co_await peerMgr->waitForSessionTerminateBaton(kPeerId3);

  /*
   * Step 4: Version check passes -> reset baton for the next cycle.
   * This is what adjRib->sessionEstablished() does in production.
   */
  terminateBaton->reset();
}

/*
 * Peer was configured but never established — no AdjRib, no baton.
 * cleanupPeerState should complete without crash.
 */
CO_TEST_F(PeerManagerTestFixture, CleanupPeerState_NeverEstablished) {
  auto config = getConfig(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  /*
   * No AdjRib in adjRibs_ for kPeerId3 — peer was never established.
   * No baton in sessionTerminateBatons_ either.
   */
  EXPECT_FALSE(peerMgr->findAdjRib(kPeerId3));

  // Should complete without crash — baton wait is skipped.
  co_await peerMgr->cleanupPeerState(kPeerId3, kPeerAddr3);

  SUCCEED();
}

/*
 * Peer was established and terminated — baton is already posted.
 * cleanupPeerState should pass through the baton wait immediately.
 */
CO_TEST_F(PeerManagerTestFixture, CleanupPeerState_BatonWaitCompletes) {
  auto config = getConfig(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  // Set up a mock AdjRib so findAdjRib returns non-null
  auto terminateBaton = std::make_shared<folly::coro::Baton>();
  auto mockAdjRib = setupMockAdjRib(
      peerMgr->getEventBase(), kPeerId3, AsNum(kPeerAsn3), terminateBaton);
  peerMgr->adjRibs_[kPeerId3] = mockAdjRib;
  peerMgr->sessionTerminateBatons_[kPeerId3] = terminateBaton;

  EXPECT_CALL(*mockAdjRib, stop())
      .Times(1)
      .WillOnce([]() -> folly::coro::Task<void> { co_return; });

  // Simulate AdjRib loops already exited — baton already posted
  terminateBaton->post();

  // Should pass through immediately
  co_await peerMgr->cleanupPeerState(kPeerId3, kPeerAddr3);

  SUCCEED();
}

/*
 * cleanupPeerState captures oldAdjRib, suspends on baton. A fiber task replaces
 * the map entry with newAdjRib AND populates the new session's per-peer
 * state. When cleanupPeerState resumes, identity check detects the
 * replacement and skips both AdjRib erase AND per-peer map cleanup, so the
 * new session's state survives.
 */
TEST_F(PeerManagerTestFixture, CleanupPeerState_GenerationCheck) {
  auto config = getConfig(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);
  auto& evb = peerMgr->getEventBase();

  // Initialize counters and capture baseline (counters are process-global).
  RibStats::initCounters();
  BgpStats::initCounters();
  auto tcData = fb303::ThreadCachedServiceData::get();
  tcData->publishStats();
  auto baseAdjRibs = tcData->getCounter(RibStats::kTotalAdjRibs);
  auto basePeerAddrToIds = tcData->getCounter(BgpStats::kPeerAddrToIdsCount);
  auto baseEstablishedGr =
      tcData->getCounter(BgpStats::kEstablishedGrPeersCount);
  auto basePendingRibDump =
      tcData->getCounter(BgpStats::kPendingRibDumpReqsCount);

  // Original session state — cleanupPeerState captures oldAdjRib
  auto oldBaton = std::make_shared<folly::coro::Baton>();
  auto oldAdjRib = setupMockAdjRib(evb, kPeerId3, AsNum(kPeerAsn3), oldBaton);
  peerMgr->adjRibs_[kPeerId3] = oldAdjRib;
  RibStats::incrAdjRibCount();
  peerMgr->sessionTerminateBatons_[kPeerId3] = oldBaton;

  // New AdjRib — simulates addPeers replacing the map entry during the wait
  auto newBaton = std::make_shared<folly::coro::Baton>();
  auto newAdjRib = setupMockAdjRib(evb, kPeerId3, AsNum(kPeerAsn3), newBaton);

  std::atomic<bool> cleanupDone{false};
  auto readyBaton = std::make_shared<folly::coro::Baton>();

  /*
   * Racer: simulates sessionEstablished + createAdjRib running for the new
   * session during cleanup's baton wait — replaces map entry AND populates
   * the new session's per-peer state.
   */
  auto racerTask =
      [&, readyBaton, oldBaton, newBaton]() -> folly::coro::Task<void> {
    co_await *readyBaton;
    peerMgr->adjRibs_[kPeerId3] = newAdjRib;
    peerMgr->sessionTerminateBatons_[kPeerId3] = newBaton;
    peerMgr->peerAddrToIds_[kPeerAddr3].insert(kPeerId3);
    BgpStats::incrPeerAddrToIdsCount();
    peerMgr->establishedGrPeers_.insert(kPeerId3);
    BgpStats::incrEstablishedGrPeersCount();
    peerMgr->getDefaultVrf().getPendingRibDumpReqs()[kPeerId3] =
        false /* sendAddPath */;
    BgpStats::incrPendingRibDumpReqsCount();
    oldBaton->post();
  };

  auto cleanupTask = [&, readyBaton]() -> folly::coro::Task<void> {
    readyBaton->post();
    co_await peerMgr->cleanupPeerState(kPeerId3, kPeerAddr3);
    cleanupDone = true;
    co_return;
  };

  folly::coro::co_withExecutor(&evb, racerTask()).start();
  folly::coro::co_withExecutor(&evb, cleanupTask()).start();

  evb.loop();

  EXPECT_TRUE(cleanupDone);

  /*
   * Identity check should preserve EVERYTHING the new session set up:
   * AdjRib, sessionTerminateBatons_, peerAddrToIds_, establishedGrPeers_,
   * pendingRibDumpReqs_, and all matching counters.
   */
  tcData->publishStats();
  EXPECT_EQ(1, peerMgr->adjRibs_.count(kPeerId3));
  EXPECT_EQ(newAdjRib, peerMgr->adjRibs_[kPeerId3]);
  EXPECT_EQ(newBaton, peerMgr->sessionTerminateBatons_[kPeerId3]);
  EXPECT_EQ(1, peerMgr->peerAddrToIds_[kPeerAddr3].count(kPeerId3));
  EXPECT_EQ(1, peerMgr->establishedGrPeers_.count(kPeerId3));
  EXPECT_EQ(
      1, peerMgr->getDefaultVrf().getPendingRibDumpReqs().count(kPeerId3));
  EXPECT_EQ(baseAdjRibs + 1, tcData->getCounter(RibStats::kTotalAdjRibs));
  EXPECT_EQ(
      basePeerAddrToIds + 1, tcData->getCounter(BgpStats::kPeerAddrToIdsCount));
  EXPECT_EQ(
      baseEstablishedGr + 1,
      tcData->getCounter(BgpStats::kEstablishedGrPeersCount));
  EXPECT_EQ(
      basePendingRibDump + 1,
      tcData->getCounter(BgpStats::kPendingRibDumpReqsCount));
}

/*
 * cleanupPeerState co_awaits stop() when isPeerGracefulRestarting. During that
 * suspension, it's possible that a new AdjRib replaces the old one. So after
 * the stop() we check if it's replaced. If it is we skip the erase so we don't
 * erase the new adjrib.
 */
TEST_F(PeerManagerTestFixture, CleanupPeerState_GenerationCheckDuringStop) {
  auto config = getConfig(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);
  auto& evb = peerMgr->getEventBase();

  auto oldBaton = std::make_shared<folly::coro::Baton>();
  auto oldAdjRib = setupMockAdjRib(evb, kPeerId3, AsNum(kPeerAsn3), oldBaton);
  peerMgr->adjRibs_[kPeerId3] = oldAdjRib;
  peerMgr->sessionTerminateBatons_[kPeerId3] = oldBaton;
  oldBaton->post();

  auto stopBaton = std::make_shared<folly::coro::Baton>();
  EXPECT_CALL(*oldAdjRib, isPeerGracefulRestarting())
      .WillRepeatedly(::testing::Return(true));
  EXPECT_CALL(*oldAdjRib, stop()).WillOnce([stopBaton]() {
    return folly::coro::co_invoke(
        [stopBaton]() -> folly::coro::Task<void> { co_await *stopBaton; });
  });

  // New AdjRib — replaces the entry mid-stop().
  auto newBaton = std::make_shared<folly::coro::Baton>();
  auto newAdjRib = setupMockAdjRib(evb, kPeerId3, AsNum(kPeerAsn3), newBaton);

  std::atomic<bool> cleanupDone{false};
  auto readyBaton = std::make_shared<folly::coro::Baton>();

  /*
   * Racer: wait until cleanup is suspended in stop(), then swap the map
   * entry and unblock stop().
   */
  auto racerTask = [&, readyBaton, stopBaton]() -> folly::coro::Task<void> {
    co_await *readyBaton;
    peerMgr->adjRibs_[kPeerId3] = newAdjRib;
    stopBaton->post();
  };

  auto cleanupTask = [&, readyBaton]() -> folly::coro::Task<void> {
    readyBaton->post();
    co_await peerMgr->cleanupPeerState(kPeerId3, kPeerAddr3);
    cleanupDone = true;
    co_return;
  };

  folly::coro::co_withExecutor(&evb, racerTask()).start();
  folly::coro::co_withExecutor(&evb, cleanupTask()).start();

  evb.loop();

  EXPECT_TRUE(cleanupDone);
  // Post-stop recheck should preserve the new AdjRib.
  EXPECT_EQ(1, peerMgr->adjRibs_.count(kPeerId3));
  EXPECT_EQ(newAdjRib, peerMgr->adjRibs_[kPeerId3]);
}

/*
 * Normal case: AdjRib identity matches after baton wait, non-GR peer.
 * Populates adjRibs_, peerAddrToIds_, sessionTerminateBatons_,
 * staticPeerEoRReceived_, establishedGrPeers_, and pendingRibDumpReqs_,
 * then verifies all per-peer state erased and counters decremented.
 */
CO_TEST_F(PeerManagerTestFixture, CleanupPeerState_NormalErase) {
  auto config = getConfig(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto terminateBaton = std::make_shared<folly::coro::Baton>();
  auto mockAdjRib = setupMockAdjRib(
      peerMgr->getEventBase(), kPeerId3, AsNum(kPeerAsn3), terminateBaton);

  /*
   * Initialize counters and capture baseline (counters are process-global,
   * so absolute values aren't reliable across tests).
   */
  RibStats::initCounters();
  BgpStats::initCounters();
  auto tcData = fb303::ThreadCachedServiceData::get();
  tcData->publishStats();
  auto baseAdjRibs = tcData->getCounter(RibStats::kTotalAdjRibs);
  auto basePeerAddrToIds = tcData->getCounter(BgpStats::kPeerAddrToIdsCount);
  auto baseEstablishedGr =
      tcData->getCounter(BgpStats::kEstablishedGrPeersCount);
  auto basePendingRibDump =
      tcData->getCounter(BgpStats::kPendingRibDumpReqsCount);

  // Populate all per-peer maps + matching counter increments
  peerMgr->adjRibs_[kPeerId3] = mockAdjRib;
  RibStats::incrAdjRibCount();
  peerMgr->sessionTerminateBatons_[kPeerId3] = terminateBaton;
  peerMgr->peerAddrToIds_[kPeerAddr3].insert(kPeerId3);
  BgpStats::incrPeerAddrToIdsCount();
  peerMgr->staticPeerEoRReceived_[kPeerAddr3] = {false, false};
  peerMgr->establishedGrPeers_.insert(kPeerId3);
  BgpStats::incrEstablishedGrPeersCount();
  peerMgr->getDefaultVrf().getPendingRibDumpReqs()[kPeerId3] =
      false /* sendAddPath */;
  BgpStats::incrPendingRibDumpReqsCount();

  /*
   * Initialize per-peer fb303 counters (cleanupPeerState calls
   * PeerStats::clearPeerCounters which should erase all of them). Use the
   * same ODS key the AdjRib was constructed with — for non-VIP peers this
   * is peeringParams.getUniquePeerId(), not peerId.peerAddr.str().
   */
  const auto& peerOdsKey = mockAdjRib->getStats().getPeerIdOdsStr();
  PeerStats::initPeerCounters(peerOdsKey);
  auto preInKey = fmt::format(PeerStats::kPeerPreInPrefixes, peerOdsKey);
  auto statusKey = fmt::format(PeerStats::kPeerStatus, peerOdsKey);
  auto sentUpdateKey = fmt::format(
      PeerStats::kPeerMessagesSentUpdate, kEbbPlatform, kBgpcppTag, peerOdsKey);
  tcData->setCounter(statusKey, 1);
  EXPECT_TRUE(tcData->hasCounter(preInKey));
  EXPECT_TRUE(tcData->hasCounter(statusKey));
  EXPECT_TRUE(tcData->hasCounter(sentUpdateKey));

  EXPECT_CALL(*mockAdjRib, stop())
      .Times(1)
      .WillOnce([]() -> folly::coro::Task<void> { co_return; });

  terminateBaton->post();

  co_await peerMgr->cleanupPeerState(kPeerId3, kPeerAddr3);

  /*
   * Verify everything erased; counters returned to baseline (incr matched
   * by cleanupPeerState's decr).
   */
  tcData->publishStats();
  EXPECT_EQ(0, peerMgr->adjRibs_.count(kPeerId3));
  EXPECT_EQ(0, peerMgr->peerAddrToIds_.count(kPeerAddr3));
  EXPECT_EQ(0, peerMgr->sessionTerminateBatons_.count(kPeerId3));
  EXPECT_EQ(0, peerMgr->staticPeerEoRReceived_.count(kPeerAddr3));
  EXPECT_EQ(0, peerMgr->establishedGrPeers_.count(kPeerId3));
  EXPECT_EQ(
      0, peerMgr->getDefaultVrf().getPendingRibDumpReqs().count(kPeerId3));
  EXPECT_EQ(baseAdjRibs, tcData->getCounter(RibStats::kTotalAdjRibs));
  EXPECT_EQ(
      basePeerAddrToIds, tcData->getCounter(BgpStats::kPeerAddrToIdsCount));
  EXPECT_EQ(
      baseEstablishedGr,
      tcData->getCounter(BgpStats::kEstablishedGrPeersCount));
  EXPECT_EQ(
      basePendingRibDump,
      tcData->getCounter(BgpStats::kPendingRibDumpReqsCount));
  // Per-peer fb303 counters should be cleared by clearPeerCounters.
  EXPECT_FALSE(tcData->hasCounter(preInKey));
  EXPECT_FALSE(tcData->hasCounter(statusKey));
  EXPECT_FALSE(tcData->hasCounter(sentUpdateKey));
}

/*
 * With update_group disabled, the per-peer changeListConsumer_
 * is held for the whole session and forms a self-cycle:
 *   AdjRib::changeListConsumer_  -> shared_ptr<AdjRibOutConsumer>
 *   AdjRibOutConsumer::adjRib_   -> shared_ptr<AdjRib> (same AdjRib)
 * cleanupPeerState must break this cycle before erasing the AdjRib from
 * adjRibs_, otherwise the AdjRib (and its AdjRibPolicyCache singleton ref)
 * leak past process exit.
 *
 * Wires a real AdjRibOutConsumer onto a MockAdjRib (mirroring the
 * UG-disabled steady state — UG-enabled paths null this out early via
 * AdjRibGroup::registerPeer), runs cleanupPeerState, and asserts the
 * consumer is null on the externally-held AdjRib.
 */
CO_TEST_F(PeerManagerTestFixture, CleanupPeerState_ResetsChangeListConsumer) {
  auto config = getConfig(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);
  auto& evb = peerMgr->getEventBase();

  auto terminateBaton = std::make_shared<folly::coro::Baton>();
  auto mockAdjRib =
      setupMockAdjRib(evb, kPeerId3, AsNum(kPeerAsn3), terminateBaton);

  auto* vrf = peerMgr->findVrf(fboss::kDefaultVrf);
  CO_ASSERT_NE(nullptr, vrf);
  /*
   * Wire a real AdjRibOutConsumer back to the AdjRib — the same shape
   * PeerManagerBase::createAdjRib produces in the UG-disabled path. This is
   * the cycle cleanupPeerState must break.
   */
  std::shared_ptr<ChangeTracker<ShadowRibEntry>> changeListTracker =
      vrf->getChangeListTracker();
  std::shared_ptr<AdjRib> adjRibBase = mockAdjRib;
  auto changeListConsumer = std::make_shared<AdjRibOutConsumer>(
      changeListTracker,
      adjRibBase,
      "ChangeList Consumer",
      evb,
      vrf->getAddPathConsumerBitmap(),
      vrf->getNonAddPathConsumerBitmap());
  mockAdjRib->setChangeListConsumer(changeListConsumer);
  CO_ASSERT_NE(nullptr, mockAdjRib->getChangeListConsumer());

  peerMgr->adjRibs_[kPeerId3] = mockAdjRib;
  RibStats::incrAdjRibCount();
  peerMgr->sessionTerminateBatons_[kPeerId3] = terminateBaton;
  peerMgr->peerAddrToIds_[kPeerAddr3].insert(kPeerId3);

  EXPECT_CALL(*mockAdjRib, stop())
      .Times(1)
      .WillOnce([]() -> folly::coro::Task<void> { co_return; });
  terminateBaton->post();

  co_await peerMgr->cleanupPeerState(kPeerId3, kPeerAddr3);

  /*
   * Cycle broken even though we still hold mockAdjRib outside the map —
   * resetChangeListConsumer() ran before adjRibs_.erase().
   */
  EXPECT_EQ(nullptr, mockAdjRib->getChangeListConsumer());
  EXPECT_EQ(0, peerMgr->adjRibs_.count(kPeerId3));
}

/*
 * sessionTerminated with evt.peerDelete=true co_awaits cleanupPeerState
 * inline, so the AdjRib is erased from adjRibs_ before sessionTerminated
 * returns.
 */
CO_TEST_F(
    PeerManagerTestFixture,
    SessionTerminated_PeerDeleteRunsCleanupInline) {
  auto config = getConfig(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);
  auto& evb = peerMgr->getEventBase();

  auto terminateBaton = std::make_shared<folly::coro::Baton>();
  auto mockAdjRib =
      setupMockAdjRib(evb, kPeerId3, AsNum(kPeerAsn3), terminateBaton);
  // sessionTerminated early-returns unless the AdjRib reports established.
  mockAdjRib->markStateEstablished();

  peerMgr->adjRibs_[kPeerId3] = mockAdjRib;
  RibStats::incrAdjRibCount();
  peerMgr->sessionTerminateBatons_[kPeerId3] = terminateBaton;
  peerMgr->peerAddrToIds_[kPeerAddr3].insert(kPeerId3);
  terminateBaton->post(); // cleanupPeerState's wait passes through

  EXPECT_CALL(*mockAdjRib, stop())
      .Times(1)
      .WillOnce([]() -> folly::coro::Task<void> { co_return; });

  FiberBgpPeer::ObservableStateT evt{
      .peerId = kPeerId3,
      .state = nettools::bgplib::BgpSessionState::IDLE,
      .versionNumber = 1,
      .peerDelete = true,
  };
  co_await peerMgr->sessionTerminated(evt);

  EXPECT_EQ(0, peerMgr->adjRibs_.count(kPeerId3));
  EXPECT_EQ(0, peerMgr->sessionTerminateBatons_.count(kPeerId3));
}

CO_TEST_F(PeerManagerTestFixture, SessionTerminated_VipWithoutAdjRibKeepsPeer) {
  auto peerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto sessionMgr = setupMockSessionManager(peerMgr);

  constexpr uint64_t kTerminatedVersion = 7;
  auto peerInfo = std::make_shared<nettools::bgplib::BgpPeerInfoInternal>();
  peerInfo->peeringParams.remoteAs = kVipAsn;
  auto sessionInfo = std::make_shared<nettools::bgplib::BgpSessionInfo>();
  sessionInfo->versionNumber =
      std::make_shared<VersionNumber>(kTerminatedVersion);
  peerInfo->sessionInfos.emplace(kPeerId3.remoteBgpId, sessionInfo);
  sessionMgr->allPeers_[kPeerAddr3] = std::move(peerInfo);

  auto nonVipPeerInfo =
      std::make_shared<nettools::bgplib::BgpPeerInfoInternal>();
  nonVipPeerInfo->peeringParams.remoteAs = kPeerAsn4;
  auto nonVipSessionInfo = std::make_shared<nettools::bgplib::BgpSessionInfo>();
  nonVipSessionInfo->versionNumber =
      std::make_shared<VersionNumber>(kTerminatedVersion);
  nonVipPeerInfo->sessionInfos.emplace(kPeerId4.remoteBgpId, nonVipSessionInfo);
  sessionMgr->allPeers_[kPeerAddr4] = std::move(nonVipPeerInfo);
  BgpStats::incrAllPeersCount();
  BgpStats::incrAllPeersCount();

  auto& sessionEvb = sessionMgr->getEventBase();
  auto sessionThread = std::thread([&sessionEvb] { sessionEvb.loopForever(); });
  sessionEvb.waitUntilRunning();

  FiberBgpPeer::ObservableStateT evt{
      .peerId = kPeerId3,
      .state = nettools::bgplib::BgpSessionState::IDLE,
      .versionNumber = kTerminatedVersion,
      .peerDelete = false,
  };
  co_await peerMgr->sessionTerminated(evt);

  FiberBgpPeer::ObservableStateT nonVipEvt{
      .peerId = kPeerId4,
      .state = nettools::bgplib::BgpSessionState::IDLE,
      .versionNumber = kTerminatedVersion,
      .peerDelete = false,
  };
  co_await peerMgr->sessionTerminated(nonVipEvt);

  sessionEvb.terminateLoopSoon();
  sessionThread.join();

  EXPECT_TRUE(sessionMgr->allPeers_.contains(kPeerAddr3));
  EXPECT_TRUE(sessionMgr->allPeers_.contains(kPeerAddr4));
  sessionMgr->allPeers_.erase(kPeerAddr3);
  sessionMgr->allPeers_.erase(kPeerAddr4);
  BgpStats::decrAllPeersCount();
  BgpStats::decrAllPeersCount();
}

CO_TEST_F(PeerManagerTestFixture, SessionTerminated_VipErasesDynamicEorState) {
  auto peerMgr = setupMockPeerManager(
      false /* includeStaticPeer */,
      false /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */,
      true /* includeDynamicVipInjectorPeer */);
  auto sessionMgr = peerMgr->getSessionManager();
  auto sessionMgrThread = sessionMgr->runInThread();
  auto& evb = peerMgr->getEventBase();

  auto terminateBaton = std::make_shared<folly::coro::Baton>();
  auto mockAdjRib =
      setupMockAdjRib(evb, kDynamicPeerId4, AsNum(kVipAsn), terminateBaton);
  mockAdjRib->sessionEstablished(
      kVipAsn,
      std::nullopt,
      std::make_shared<AdjRib::AdjRibInQueueT>(),
      std::make_shared<AdjRib::AdjRibOutQueueT>(
          kMaxEgressQueueSize,
          kEgressQueueHighWatermark,
          kEgressQueueLowWatermark));
  mockAdjRib->markStateEstablished();

  peerMgr->initialized_ = true;
  peerMgr->adjRibs_[kDynamicPeerId4] = mockAdjRib;
  RibStats::incrAdjRibCount();
  peerMgr->sessionTerminateBatons_[kDynamicPeerId4] = terminateBaton;
  peerMgr->peerAddrToIds_[kDynamicPeerAddr4].insert(kDynamicPeerId4);
  peerMgr->dynamicPeerEoRReceived_[kDynamicPeerId4] = {true, true};
  terminateBaton->post();

  EXPECT_CALL(*mockAdjRib, stop())
      .Times(1)
      .WillOnce([]() -> folly::coro::Task<void> { co_return; });

  FiberBgpPeer::ObservableStateT evt{
      .peerId = kDynamicPeerId4,
      .state = nettools::bgplib::BgpSessionState::IDLE,
      .versionNumber = 1,
      .peerDelete = false,
  };
  co_await peerMgr->sessionTerminated(evt);

  sessionMgr->stop();
  sessionMgrThread.join();

  EXPECT_EQ(0, peerMgr->adjRibs_.count(kDynamicPeerId4));
  EXPECT_EQ(0, peerMgr->dynamicPeerEoRReceived_.count(kDynamicPeerId4));
}

/*
 * A dynamic VIP peer that advertised GR keeps its AdjRib and SessionManager
 * session on disconnect, so its Remote-GR timer can withdraw the stale routes.
 */
CO_TEST_F(
    PeerManagerTestFixture,
    SessionTerminated_VipWithGrKeepsAdjRibAndSession) {
  auto peerMgr = setupMockPeerManager(
      false /* includeStaticPeer */,
      false /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */,
      true /* includeDynamicVipInjectorPeer */);
  auto sessionMgr = std::dynamic_pointer_cast<MockSessionManager>(
      peerMgr->getSessionManager());
  CO_ASSERT_NE(nullptr, sessionMgr);
  auto& evb = peerMgr->getEventBase();

  auto terminateBaton = std::make_shared<folly::coro::Baton>();
  auto mockAdjRib =
      setupMockAdjRib(evb, kDynamicPeerId4, AsNum(kVipAsn), terminateBaton);
  mockAdjRib->sessionEstablished(
      kVipAsn,
      std::optional<uint16_t>(30),
      std::make_shared<AdjRib::AdjRibInQueueT>(),
      std::make_shared<AdjRib::AdjRibOutQueueT>(
          kMaxEgressQueueSize,
          kEgressQueueHighWatermark,
          kEgressQueueLowWatermark));
  mockAdjRib->markStateEstablished();

  peerMgr->initialized_ = true;
  peerMgr->runningSessions_ = 1;
  peerMgr->runningVipSessions_ = 1;
  peerMgr->adjRibs_[kDynamicPeerId4] = mockAdjRib;
  RibStats::incrAdjRibCount();
  peerMgr->sessionTerminateBatons_[kDynamicPeerId4] = terminateBaton;
  peerMgr->peerAddrToIds_[kDynamicPeerAddr4].insert(kDynamicPeerId4);
  terminateBaton->post();

  EXPECT_CALL(*mockAdjRib, stop()).Times(0);
  EXPECT_CALL(
      *sessionMgr,
      scheduleDeleteTerminatedSession(kDynamicPeerId4, testing::_, testing::_))
      .Times(0);

  FiberBgpPeer::ObservableStateT evt{
      .peerId = kDynamicPeerId4,
      .state = nettools::bgplib::BgpSessionState::IDLE,
      .versionNumber = 1,
      .peerDelete = false,
  };
  co_await peerMgr->sessionTerminated(evt);

  EXPECT_EQ(mockAdjRib, peerMgr->adjRibs_.at(kDynamicPeerId4));
  EXPECT_FALSE(mockAdjRib->isStateEstablished());
  EXPECT_EQ(0, peerMgr->runningSessions_);
  EXPECT_EQ(0, peerMgr->runningVipSessions_);
}

CO_TEST_F(PeerManagerTestFixture, SessionTerminated_ShutdownSkipsProcessing) {
  auto peerMgr = setupMockPeerManager(
      false /* includeStaticPeer */,
      false /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */,
      true /* includeDynamicVipInjectorPeer */);
  auto sessionMgr = std::dynamic_pointer_cast<MockSessionManager>(
      peerMgr->getSessionManager());
  CO_ASSERT_NE(nullptr, sessionMgr);
  auto& evb = peerMgr->getEventBase();

  constexpr uint64_t kTerminatedVersion = 7;
  auto peerInfo = std::make_shared<nettools::bgplib::BgpPeerInfoInternal>();
  peerInfo->peeringParams.remoteAs = kVipAsn;
  auto sessionInfo = std::make_shared<nettools::bgplib::BgpSessionInfo>();
  sessionInfo->versionNumber =
      std::make_shared<VersionNumber>(kTerminatedVersion);
  peerInfo->sessionInfos.emplace(kDynamicPeerId4.remoteBgpId, sessionInfo);
  sessionMgr->allPeers_[kDynamicPeerAddr4] = std::move(peerInfo);
  BgpStats::incrAllPeersCount();

  auto terminateBaton = std::make_shared<folly::coro::Baton>();
  auto mockAdjRib =
      setupMockAdjRib(evb, kDynamicPeerId4, AsNum(kVipAsn), terminateBaton);
  mockAdjRib->sessionEstablished(
      kVipAsn,
      std::nullopt,
      std::make_shared<AdjRib::AdjRibInQueueT>(),
      std::make_shared<AdjRib::AdjRibOutQueueT>(
          kMaxEgressQueueSize,
          kEgressQueueHighWatermark,
          kEgressQueueLowWatermark));
  mockAdjRib->markStateEstablished();

  peerMgr->initialized_ = true;
  peerMgr->daemonShutdown_ = true;
  peerMgr->runningSessions_ = 1;
  peerMgr->runningVipSessions_ = 1;
  peerMgr->adjRibs_[kDynamicPeerId4] = mockAdjRib;
  RibStats::incrAdjRibCount();
  peerMgr->sessionTerminateBatons_[kDynamicPeerId4] = terminateBaton;
  peerMgr->peerAddrToIds_[kDynamicPeerAddr4].insert(kDynamicPeerId4);
  peerMgr->establishedGrPeers_.insert(kDynamicPeerId4);
  EXPECT_CALL(*mockAdjRib, stop()).Times(0);

  FiberBgpPeer::ObservableStateT evt{
      .peerId = kDynamicPeerId4,
      .state = nettools::bgplib::BgpSessionState::IDLE,
      .versionNumber = kTerminatedVersion,
      .peerDelete = false,
  };
  co_await peerMgr->sessionTerminated(evt);

  EXPECT_TRUE(sessionMgr->getBgpSessionInfo(kDynamicPeerId4).has_value());
  EXPECT_EQ(1, peerMgr->adjRibs_.count(kDynamicPeerId4));
  EXPECT_TRUE(mockAdjRib->isStateEstablished());
  EXPECT_EQ(1, peerMgr->runningSessions_);
  EXPECT_EQ(1, peerMgr->runningVipSessions_);
  EXPECT_EQ(1, peerMgr->establishedGrPeers_.count(kDynamicPeerId4));
  EXPECT_EQ(1, peerMgr->sessionTerminateBatons_.count(kDynamicPeerId4));

  sessionMgr->allPeers_.erase(kDynamicPeerAddr4);
  BgpStats::decrAllPeersCount();
}

CO_TEST_F(
    PeerManagerTestFixture,
    SessionTerminated_StaticPeerWithVipAsnKeepsAdjRib) {
  auto config = getConfig(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);
  auto& evb = peerMgr->getEventBase();

  auto terminateBaton = std::make_shared<folly::coro::Baton>();
  auto mockAdjRib =
      setupMockAdjRib(evb, kPeerId3, AsNum(kVipAsn), terminateBaton);
  mockAdjRib->markStateEstablished();

  peerMgr->adjRibs_[kPeerId3] = mockAdjRib;
  RibStats::incrAdjRibCount();
  peerMgr->sessionTerminateBatons_[kPeerId3] = terminateBaton;
  peerMgr->peerAddrToIds_[kPeerAddr3].insert(kPeerId3);
  terminateBaton->post();

  EXPECT_CALL(*mockAdjRib, stop()).Times(0);

  FiberBgpPeer::ObservableStateT evt{
      .peerId = kPeerId3,
      .state = nettools::bgplib::BgpSessionState::IDLE,
      .versionNumber = 1,
      .peerDelete = false,
  };
  co_await peerMgr->sessionTerminated(evt);

  EXPECT_EQ(1, peerMgr->adjRibs_.count(kPeerId3));
  EXPECT_EQ(1, peerMgr->sessionTerminateBatons_.count(kPeerId3));
}

/*
 * evt.peerDelete=false (natural session-down, no delPeers in flight)
 * must NOT run cleanupPeerState. AdjRib stays in adjRibs_ for re-establish.
 */
CO_TEST_F(PeerManagerTestFixture, SessionTerminated_NoPeerDeleteKeepsAdjRib) {
  auto config = getConfig(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);
  auto& evb = peerMgr->getEventBase();

  auto terminateBaton = std::make_shared<folly::coro::Baton>();
  auto mockAdjRib =
      setupMockAdjRib(evb, kPeerId3, AsNum(kPeerAsn3), terminateBaton);
  mockAdjRib->markStateEstablished();

  peerMgr->adjRibs_[kPeerId3] = mockAdjRib;
  RibStats::incrAdjRibCount();
  peerMgr->sessionTerminateBatons_[kPeerId3] = terminateBaton;
  peerMgr->peerAddrToIds_[kPeerAddr3].insert(kPeerId3);
  terminateBaton->post();

  // stop() must NOT be called — that's only invoked from cleanupPeerState.
  EXPECT_CALL(*mockAdjRib, stop()).Times(0);

  FiberBgpPeer::ObservableStateT evt{
      .peerId = kPeerId3,
      .state = nettools::bgplib::BgpSessionState::IDLE,
      .versionNumber = 1,
      .peerDelete = false,
  };
  co_await peerMgr->sessionTerminated(evt);

  EXPECT_EQ(1, peerMgr->adjRibs_.count(kPeerId3));
  EXPECT_EQ(1, peerMgr->sessionTerminateBatons_.count(kPeerId3));
}

/*
 * delPeers fallback for non-established peers (IDLE / GR helper mode):
 * shutdownPeer skips peer->stop() when activeSessionInfo is null, so no
 * BgpSessionStop fires and sessionTerminated → cleanupPeerState never
 * runs via the FSM. delPeers must drive cleanupPeerState directly for
 * any non-established peerId, otherwise per-peer state leaks past the
 * delete.
 */
CO_TEST_F(
    PeerManagerTestFixture,
    DelPeers_NotEstablishedPeer_FallbackCleansUp) {
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);

  // Populate sessionMgr->allPeers_ so co_dropPeer succeeds.
  auto peerToConfig = config_->getPeerToConfig();
  auto params3 = config_->getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr3));
  sessionMgr->allPeers_[kPeerAddr3] =
      std::make_shared<nettools::bgplib::BgpPeerInfoInternal>(
          nettools::bgplib::BgpPeerInfoInternal{params3, {}, {}});

  /*
   * Set up an AdjRib for peer3 that is NOT in established state — mirrors
   * a peer that was previously up, went IDLE, and is now being deleted.
   */
  auto& evb = mockPeerMgr->getEventBase();
  auto terminateBaton = std::make_shared<folly::coro::Baton>();
  auto mockAdjRib =
      setupMockAdjRib(evb, kPeerId3, AsNum(kPeerAsn3), terminateBaton);
  // Note: NO markStateEstablished() — peer is IDLE.

  mockPeerMgr->adjRibs_[kPeerId3] = mockAdjRib;
  RibStats::incrAdjRibCount();
  mockPeerMgr->sessionTerminateBatons_[kPeerId3] = terminateBaton;
  mockPeerMgr->peerAddrToIds_[kPeerAddr3].insert(kPeerId3);
  // Pre-post — cleanupPeerState's wait passes through (latch semantics).
  terminateBaton->post();

  EXPECT_CALL(*mockAdjRib, stop())
      .Times(1)
      .WillOnce([]() -> folly::coro::Task<void> { co_return; });

  auto& sessionEvb = sessionMgr->getEventBase();
  auto sessionThread = std::thread([&sessionEvb] { sessionEvb.loopForever(); });
  sessionEvb.waitUntilRunning();

  /*
   * delPeers's fallback fires because !isStateEstablished — runs
   * cleanupPeerState inline before returning.
   */
  auto result = co_await mockPeerMgr->delPeers({kPeerAddr3});
  EXPECT_TRUE(result.hasValue());

  EXPECT_EQ(0, mockPeerMgr->adjRibs_.count(kPeerId3));
  EXPECT_EQ(0, mockPeerMgr->sessionTerminateBatons_.count(kPeerId3));
  EXPECT_EQ(0, mockPeerMgr->peerAddrToIds_.count(kPeerAddr3));

  sessionEvb.terminateLoopSoon();
  sessionThread.join();
}

/*
 * Flap-then-delPeers scenario.
 *
 * Production sequence: peer3 was up, session flapped to IDLE (natural
 * session-down — sessionTerminated ran with peerDelete=false, called
 * markStateTerminated(), but kept the AdjRib in adjRibs_ for re-establish).
 * Operator then calls delPeers.
 *
 * Path through the code:
 *  - shutdownPeer skips peer->stop() (activeSessionInfo is null after the
 *    flap), so no BgpSessionStop fires → no IDLE event from delPeers.
 *  - The FSM-driven sessionTerminated path won't run (no event).
 *  - delPeers' fallback fires because !isStateEstablished(), drives
 *    cleanupPeerState directly.
 *  - cleanupPeerState's baton wait passes through (latch — the baton was
 *    posted during the flap's session-down).
 *
 * Distinct from DelPeers_NotEstablishedPeer_FallbackCleansUp: that test
 * covers a peer that was NEVER established. This one covers a peer that
 * WAS established and went IDLE — the more realistic production case.
 */
CO_TEST_F(PeerManagerTestFixture, DelPeers_AfterFlap_FallbackCleansUp) {
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);

  // Populate sessionMgr->allPeers_ so co_dropPeer succeeds.
  auto peerToConfig = config_->getPeerToConfig();
  auto params3 = config_->getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr3));
  sessionMgr->allPeers_[kPeerAddr3] =
      std::make_shared<nettools::bgplib::BgpPeerInfoInternal>(
          nettools::bgplib::BgpPeerInfoInternal{params3, {}, {}});

  // AdjRib was established, then flapped to IDLE.
  auto& evb = mockPeerMgr->getEventBase();
  auto terminateBaton = std::make_shared<folly::coro::Baton>();
  auto mockAdjRib =
      setupMockAdjRib(evb, kPeerId3, AsNum(kPeerAsn3), terminateBaton);
  mockAdjRib->markStateEstablished();
  mockAdjRib->markStateTerminated();
  CO_ASSERT_FALSE(mockAdjRib->isStateEstablished())
      << "Setup precondition: post-flap AdjRib must report !isStateEstablished";

  mockPeerMgr->adjRibs_[kPeerId3] = mockAdjRib;
  RibStats::incrAdjRibCount();
  mockPeerMgr->sessionTerminateBatons_[kPeerId3] = terminateBaton;
  mockPeerMgr->peerAddrToIds_[kPeerAddr3].insert(kPeerId3);
  // Baton was posted during the flap's natural session-down.
  terminateBaton->post();

  EXPECT_CALL(*mockAdjRib, stop())
      .Times(1)
      .WillOnce([]() -> folly::coro::Task<void> { co_return; });

  auto& sessionEvb = sessionMgr->getEventBase();
  auto sessionThread = std::thread([&sessionEvb] { sessionEvb.loopForever(); });
  sessionEvb.waitUntilRunning();

  /*
   * delPeers's fallback fires because the AdjRib is in adjRibs_ but
   * !isStateEstablished — runs cleanupPeerState inline before returning.
   */
  auto result = co_await mockPeerMgr->delPeers({kPeerAddr3});
  EXPECT_TRUE(result.hasValue());

  EXPECT_EQ(0, mockPeerMgr->adjRibs_.count(kPeerId3));
  EXPECT_EQ(0, mockPeerMgr->sessionTerminateBatons_.count(kPeerId3));
  EXPECT_EQ(0, mockPeerMgr->peerAddrToIds_.count(kPeerAddr3));

  sessionEvb.terminateLoopSoon();
  sessionThread.join();
}
} // namespace facebook::bgp
