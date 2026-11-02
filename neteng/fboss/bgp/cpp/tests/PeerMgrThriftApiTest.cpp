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

#define PeerManager_TEST_FRIENDS                                             \
  FRIEND_TEST(                                                               \
      PeerManagerFixtureCanaryKnobTestSuite,                                 \
      Exportfb303CounterSessionUpAndDownTest);                               \
  FRIEND_TEST(PeerManagerTestFixture, StopPeerToSessionMgrTest);             \
  FRIEND_TEST(PeerManagerTestFixture, GetBgpSessionAdjRibMessageCountsTest); \
  FRIEND_TEST(PeerManagerTestFixture, GetBgpSessionEoRStatusTest);           \
  FRIEND_TEST(PeerManagerTestFixture, RswSlbInRejectionCounterTest);

#define AdjRib_TEST_FRIENDS                                                  \
  friend class PeerManagerFixtureCanaryKnobTestSuite;                        \
  FRIEND_TEST(                                                               \
      PeerManagerFixtureCanaryKnobTestSuite,                                 \
      Exportfb303CounterSessionUpAndDownTest);                               \
  FRIEND_TEST(PeerManagerTestFixture, GetBgpSessionAdjRibMessageCountsTest); \
  FRIEND_TEST(PeerManagerTestFixture, GetBgpSessionEoRStatusTest);           \
  FRIEND_TEST(PeerManagerTestFixture, RswSlbInRejectionCounterTest);

#define AdjRibStats_TEST_FRIENDS                      \
  friend class PeerManagerFixtureCanaryKnobTestSuite; \
  FRIEND_TEST(                                        \
      PeerManagerFixtureCanaryKnobTestSuite,          \
      Exportfb303CounterSessionUpAndDownTest);

#define AdjRibOutGroup_TEST_FRIENDS \
  FRIEND_TEST(PeerManagerTestFixture, GetBgpSessionAdjRibMessageCountsTest);

#include <algorithm>

#include <fmt/core.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/fibers/FiberManagerMap.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fb303/ServiceData.h"
#include "neteng/fboss/bgp/cpp/tests/PeerManagerTestUtils.h"

using ::testing::_;

using namespace facebook::nettools::bgplib;
using namespace facebook::neteng::fboss::bgp::thrift;
using namespace facebook::neteng::fboss::bgp_attr;

namespace facebook::bgp {

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

INSTANTIATE_TEST_SUITE_P(
    PeerManagerTestFixture,
    PeerManagerFixtureCanaryKnobTestSuite,
    testing::Values(false, true /* knob */));

TEST_F(PeerManagerTestFixture, GetBgpSessionEoRStatusTest) {
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);
  const auto staticPeerAddr = folly::IPAddress(*staticPeer1_.peer_addr());
  const BgpPeerId peerId{staticPeerAddr, 0};
  auto adjRib = setupMockAdjRib(
      mockPeerMgr->getEventBase(),
      peerId,
      AsNum(kAsn1),
      sessionTerminateBaton_);
  mockPeerMgr->adjRibs_[peerId] = adjRib;

  EXPECT_CALL(*adjRib, stop()).WillRepeatedly([]() -> folly::coro::Task<void> {
    co_return;
  });
  EXPECT_CALL(*adjRib, cleanupGrState(testing::_))
      .WillRepeatedly([](bool) -> folly::coro::Task<void> { co_return; });

  auto peerMgrThread = mockPeerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();
  mockPeerMgr->addPeersToSessionMgr();

  auto getStatus = [&]() -> std::pair<bool, bool> {
    const auto sessions = getSessionsViaSessionMgr(*mockPeerMgr);
    const auto it =
        std::find_if(sessions.begin(), sessions.end(), [&](auto& s) {
          return s.peer_addr().value() == *staticPeer1_.peer_addr();
        });
    EXPECT_NE(it, sessions.end());
    if (it == sessions.end()) {
      return {false, false};
    }
    EXPECT_TRUE(it->eor_received().has_value());
    EXPECT_TRUE(it->eor_sent().has_value());
    return {it->eor_received().value_or(false), it->eor_sent().value_or(false)};
  };

  EXPECT_EQ(getStatus(), (std::pair<bool, bool>{false, false}));
  adjRib->eorReceivedTime_ = 1740000000000;
  EXPECT_EQ(getStatus(), (std::pair<bool, bool>{true, false}));
  adjRib->eorSentTime_ = 1740000000001;
  EXPECT_EQ(getStatus(), (std::pair<bool, bool>{true, true}));

  mockPeerMgr->stop();
  sessionMgr->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
}

TEST_F(PeerManagerTestFixture, RswSlbInRejectionCounterTest) {
  auto peerMgr = setupMockPeerManager(true, false);
  auto counters = fb303::ThreadCachedServiceData::getShared();
  PeerStats::setNumRswSlbInRejections(99);
  folly::coro::blockingWait(peerMgr->co_updateRswSlbInRejectionCounter());
  EXPECT_EQ(0, counters->getCounter("num_rsw_slb_in_rejections"));

  auto makeAdjRib = [&](const folly::IPAddress& peerAddr,
                        const std::string& policyName,
                        bool addPath) {
    const BgpPeerId peerId{peerAddr, 0};
    auto adjRib = setupMockAdjRib(
        peerMgr->getEventBase(), peerId, AsNum(kAsn1), sessionTerminateBaton_);
    adjRib->ingressPolicyName_ = policyName;
    adjRib->recAddPath_ = addPath;
    peerMgr->adjRibs_[peerId] = adjRib;
    return adjRib;
  };
  auto slb = makeAdjRib(kPeerAddr1, "PROPAGATE_RSW_SLB_IN", false);
  auto addPathSlb = makeAdjRib(kPeerAddr2, "PROPAGATE_RSW_SLB_IN", true);
  auto otherPeer = makeAdjRib(kPeerAddr3, "rsw_slb_in_other", false);
  auto attrs = std::make_shared<BgpPath>();
  attrs->setNexthop(kV4Nexthop1);
  attrs->publish();

  auto addEntry = [&](const std::shared_ptr<AdjRib>& adjRib,
                      const folly::CIDRNetwork& prefix,
                      uint32_t pathId,
                      const std::string& policyResult) {
    auto* entry = adjRib->addRibEntry(true, prefix, pathId);
    entry->setPreIn(attrs);
    entry->setPostInPolicy(policyResult);
    return entry;
  };
  auto* deniedV4 = addEntry(
      slb,
      kV4Prefix1,
      kDefaultPathID,
      "Denied by PROPAGATE_RSW_SLB_IN term deny");
  auto* deniedV6 = addEntry(
      slb,
      kV6Prefix1,
      kDefaultPathID,
      "Did not match any policy term in PROPAGATE_RSW_SLB_IN. Default denied");
  addEntry(slb, kV4Prefix2, kDefaultPathID, "Denied by Route Filter Policy");
  auto* unrelatedRejection = addEntry(
      slb,
      kV4Prefix3,
      kDefaultPathID,
      "Denied by PROPAGATE_RSW_SLB_IN_OTHER term deny");
  addEntry(
      slb,
      kV6Prefix2,
      kDefaultPathID,
      "Accepted/Modified by PROPAGATE_RSW_SLB_IN term allow");
  addEntry(
      otherPeer,
      kV4Prefix1,
      kDefaultPathID,
      "Denied by PROPAGATE_RSW_SLB_IN term deny");
  addEntry(
      addPathSlb, kV4Prefix1, 1, "Denied by PROPAGATE_RSW_SLB_IN term deny");
  auto* stalePath = addEntry(
      addPathSlb, kV4Prefix1, 2, "Denied by PROPAGATE_RSW_SLB_IN term deny");

  folly::coro::blockingWait(peerMgr->co_updateRswSlbInRejectionCounter());
  EXPECT_EQ(4, counters->getCounter("num_rsw_slb_in_rejections"));
  folly::coro::blockingWait(peerMgr->co_updateRswSlbInRejectionCounter());
  EXPECT_EQ(4, counters->getCounter("num_rsw_slb_in_rejections"));

  for (const auto& policyName :
       {"rsw_slb_in",
        "RSW_SLB_IN",
        "propagate_rsw_slb_in",
        "PROPAGATE_RSW_SLB_IN_OTHER"}) {
    otherPeer->ingressPolicyName_ = policyName;
    folly::coro::blockingWait(peerMgr->co_updateRswSlbInRejectionCounter());
    EXPECT_EQ(4, counters->getCounter("num_rsw_slb_in_rejections"));
  }
  for (
      const auto& policyResult :
      {"Denied by propagate_rsw_slb_in term deny",
       "Denied by rsw_slb_in term deny",
       "Did not match any policy term in propagate_rsw_slb_in. Default denied"}) {
    unrelatedRejection->setPostInPolicy(policyResult);
    folly::coro::blockingWait(peerMgr->co_updateRswSlbInRejectionCounter());
    EXPECT_EQ(4, counters->getCounter("num_rsw_slb_in_rejections"));
  }

  deniedV4->setPostAttr(attrs);
  deniedV6->setPreIn(nullptr);
  folly::coro::blockingWait(peerMgr->co_updateRswSlbInRejectionCounter());
  EXPECT_EQ(2, counters->getCounter("num_rsw_slb_in_rejections"));

  ON_CALL(*addPathSlb, isPeerGracefulRestarting())
      .WillByDefault(testing::Return(true));
  folly::coro::blockingWait(peerMgr->co_updateRswSlbInRejectionCounter());
  EXPECT_EQ(0, counters->getCounter("num_rsw_slb_in_rejections"));
  stalePath->setStale(true);
  ON_CALL(*addPathSlb, isPeerGracefulRestarting())
      .WillByDefault(testing::Return(false));
  folly::coro::blockingWait(peerMgr->co_updateRswSlbInRejectionCounter());
  EXPECT_EQ(1, counters->getCounter("num_rsw_slb_in_rejections"));
  stalePath->setStale(false);
  folly::coro::blockingWait(peerMgr->co_updateRswSlbInRejectionCounter());
  EXPECT_EQ(2, counters->getCounter("num_rsw_slb_in_rejections"));

  peerMgr->adjRibs_.erase(BgpPeerId{kPeerAddr2, 0});
  folly::coro::blockingWait(peerMgr->co_updateRswSlbInRejectionCounter());
  EXPECT_EQ(0, counters->getCounter("num_rsw_slb_in_rejections"));
}

TEST_F(PeerManagerTestFixture, StartSessionTest) {
  auto mockPeerMgr = setupMockPeerManagerWithSeparateThread(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto config = config_;
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);

  auto dynamicPeerToConfig = config->getDynamicPeerToConfig();
  EXPECT_EQ(2, dynamicPeerToConfig.size());
  // get PeeringParams and set expectations
  auto params1 = config->getPeeringParamsForDynamicPeer(
      *dynamicPeerToConfig.at(kPeerPrefix1));
  EXPECT_CALL(*sessionMgr, addDynamicPeer_(kPeerPrefix1, params1)).Times(1);
  auto params2 = config->getPeeringParamsForDynamicPeer(
      *dynamicPeerToConfig.at(kPeerPrefix2));
  EXPECT_CALL(*sessionMgr, addDynamicPeer_(kPeerPrefix2, params2)).Times(1);

  auto peerToConfig = config->getPeerToConfig();
  EXPECT_EQ(2, peerToConfig.size());
  auto params3 = config->getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr3));
  EXPECT_CALL(*sessionMgr, addPeer_(kPeerAddr3, params3, _)).Times(1);
  auto params4 = config->getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr4));
  EXPECT_CALL(*sessionMgr, addPeer_(kPeerAddr4, params4, _)).Times(1);

  EXPECT_CALL(*sessionMgr, shutdownPeer(kPeerAddr3, _)).Times(1);
  EXPECT_CALL(*sessionMgr, startPeer(kPeerAddr3)).Times(1);
  EXPECT_CALL(*sessionMgr, startPeer(kPeerAddr5)).Times(1);
  EXPECT_CALL(*sessionMgr, shutdownDynamicPeer(kPeerPrefix1)).Times(1);
  EXPECT_CALL(*sessionMgr, startDynamicPeer(kPeerPrefix1)).Times(1);
  EXPECT_CALL(*sessionMgr, startDynamicPeer(kV6Prefix1)).Times(1);
  // create peer manager thread
  auto mockPeerMgrThread = mockPeerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  folly::EventBase evb;
  auto& fm = folly::fibers::getFiberManager(evb, options_);
  // start testing peer field by feild
  fm.addTask([&] {
    // add configured static peers (kPeerAddr3,kPeerAddr4) to session manager
    mockPeerMgr->addPeersToSessionMgr();
    auto sessions = getSessionsViaSessionMgr(*mockPeerMgr);
    EXPECT_EQ(4, sessions.size());
    // shutdown an existing peer session
    sessionMgr->shutdownSession(kPeerAddr3);
    sessions = getSessionsViaSessionMgr(*mockPeerMgr);
    EXPECT_EQ(4, sessions.size());
    // start a down peer session
    sessionMgr->startSession(kPeerAddr3);
    sessions = getSessionsViaSessionMgr(*mockPeerMgr);
    EXPECT_EQ(4, sessions.size());
    // start an unconfigured peer session
    sessionMgr->startSession(kPeerAddr5);
    sessions = getSessionsViaSessionMgr(*mockPeerMgr);
    EXPECT_EQ(4, sessions.size());
    // shutdown a configured dynamic peer session
    sessionMgr->shutdownSession(kPeerPrefix1);
    sessions = getSessionsViaSessionMgr(*mockPeerMgr);
    EXPECT_EQ(4, sessions.size());
    // start an idle dynamic peer session
    sessionMgr->startSession(kPeerPrefix1);
    sessions = getSessionsViaSessionMgr(*mockPeerMgr);
    EXPECT_EQ(4, sessions.size());
    // start an unconfigured dynamic peer session
    sessionMgr->startSession(kV6Prefix1);
    sessions = getSessionsViaSessionMgr(*mockPeerMgr);
    EXPECT_EQ(4, sessions.size());

    // stop PeerManagerBase
    mockPeerMgr->stop();
    sessionMgr->stop();
  });

  evb.loop();
  mockPeerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

TEST_F(PeerManagerTestFixture, StopPeerToSessionMgrTest) {
  // create session manager
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto& evb = mockPeerMgr->getEventBase();

  auto config = config_;
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);
  auto sessionMgrThread = sessionMgr->runInThread();

  auto peerToConfig = config->getPeerToConfig();
  EXPECT_EQ(2, peerToConfig.size());
  auto params3 = config->getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr3));
  EXPECT_CALL(*sessionMgr, addPeer_(kPeerAddr3, params3, _)).Times(1);
  auto params4 = config->getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr4));
  EXPECT_CALL(*sessionMgr, addPeer_(kPeerAddr4, params4, _)).Times(1);

  // Use a baton to coordinate when stopPeer has been called for both peers
  auto stopPeerBaton = std::make_shared<folly::fibers::Baton>();
  std::atomic<int> stopPeerCount{0};

  EXPECT_CALL(*sessionMgr, stopPeer(kPeerAddr3, false /*withGR*/))
      .Times(1)
      .WillOnce([stopPeerBaton, &stopPeerCount](auto, auto) {
        if (++stopPeerCount == 2) {
          stopPeerBaton->post();
        }
        return folly::makeExpected<SessionManager::ErrorCode>(folly::unit);
      });
  EXPECT_CALL(*sessionMgr, stopPeer(kPeerAddr4, false /*withGR*/))
      .Times(1)
      .WillOnce([stopPeerBaton, &stopPeerCount](auto, auto) {
        if (++stopPeerCount == 2) {
          stopPeerBaton->post();
        }
        return folly::makeExpected<SessionManager::ErrorCode>(folly::unit);
      });

  // adds peers to sessionMgr
  mockPeerMgr->addPeersToSessionMgr();

  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);
  fm.addTask([&, stopPeerBaton] {
    folly::coro::CancellableAsyncScope asyncScope;

    // Start coro tasks to process neighbor route change messages
    asyncScope.add(
        co_withExecutor(&evb, mockPeerMgr->processNeighborRouteChangeLoop()));

    nbrRouteChangeQ_->push(NeighborEventMsg(kPeerAddr3, false));
    nbrRouteChangeQ_->push(NeighborEventMsg(kPeerAddr4, false));

    // Wait for both stopPeer calls to complete
    stopPeerBaton->wait();

    // Stop everything from within the fiber context
    mockPeerMgr->stop();
    sessionMgr->stop();

    // cancel coroutines
    folly::coro::blockingWait(asyncScope.cancelAndJoinAsync());
  });

  evb.loop();
  sessionMgrThread.join();
  SUCCEED();
}

TEST_F(PeerManagerTestFixture, ShutdownSessionTest) {
  auto mockPeerMgr = setupMockPeerManagerWithSeparateThread(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto config = config_;
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);

  auto dynamicPeerToConfig = config->getDynamicPeerToConfig();
  EXPECT_EQ(2, dynamicPeerToConfig.size());
  // get PeeringParams and set expectations
  auto params1 = config->getPeeringParamsForDynamicPeer(
      *dynamicPeerToConfig.at(kPeerPrefix1));
  EXPECT_CALL(*sessionMgr, addDynamicPeer_(kPeerPrefix1, params1)).Times(1);
  auto params2 = config->getPeeringParamsForDynamicPeer(
      *dynamicPeerToConfig.at(kPeerPrefix2));
  EXPECT_CALL(*sessionMgr, addDynamicPeer_(kPeerPrefix2, params2)).Times(1);

  auto peerToConfig = config->getPeerToConfig();
  EXPECT_EQ(2, peerToConfig.size());
  auto params3 = config->getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr3));
  EXPECT_CALL(*sessionMgr, addPeer_(kPeerAddr3, params3, _)).Times(1);
  auto params4 = config->getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr4));
  EXPECT_CALL(*sessionMgr, addPeer_(kPeerAddr4, params4, _)).Times(1);

  EXPECT_CALL(*sessionMgr, shutdownDynamicPeer(kPeerPrefix1)).Times(1);
  EXPECT_CALL(*sessionMgr, shutdownDynamicPeer(kV6Prefix1)).Times(1);
  EXPECT_CALL(*sessionMgr, shutdownPeer(kPeerAddr3, _)).Times(1);
  EXPECT_CALL(*sessionMgr, shutdownPeer(kPeerAddr5, _)).Times(1);
  // create peer manager thread
  auto mockPeerMgrThread = mockPeerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  folly::EventBase evb;
  auto& fm = folly::fibers::getFiberManager(evb, options_);
  // start testing peer field by field
  fm.addTask([&] {
    // add configured static peers (kPeerAddr3,kPeerAddr4) to session manager
    mockPeerMgr->addPeersToSessionMgr();
    auto sessions = getSessionsViaSessionMgr(*mockPeerMgr);
    EXPECT_EQ(4, sessions.size());
    // shutdown a configured peer session
    sessionMgr->shutdownSession(kPeerAddr3);
    sessions = getSessionsViaSessionMgr(*mockPeerMgr);
    EXPECT_EQ(4, sessions.size());
    // shutdown an un-configured peer session
    sessionMgr->shutdownSession(kPeerAddr5);
    sessions = getSessionsViaSessionMgr(*mockPeerMgr);
    EXPECT_EQ(4, sessions.size());
    // shutdown a configured dynamic peer session
    sessionMgr->shutdownSession(kPeerPrefix1);
    sessions = getSessionsViaSessionMgr(*mockPeerMgr);
    EXPECT_EQ(4, sessions.size());
    // shutdown an un-configured dynamic peer session
    sessionMgr->shutdownSession(kV6Prefix1);
    sessions = getSessionsViaSessionMgr(*mockPeerMgr);
    EXPECT_EQ(4, sessions.size());

    // stop PeerManagerBase
    mockPeerMgr->stop();
    sessionMgr->stop();
  });

  evb.loop();
  mockPeerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

TEST_F(PeerManagerTestFixture, RestartSessionTest) {
  auto mockPeerMgr = setupMockPeerManagerWithSeparateThread(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto config = config_;
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);

  auto dynamicPeerToConfig = config->getDynamicPeerToConfig();
  EXPECT_EQ(2, dynamicPeerToConfig.size());
  // get PeeringParams and set expectations
  auto params1 = config->getPeeringParamsForDynamicPeer(
      *dynamicPeerToConfig.at(kPeerPrefix1));
  EXPECT_CALL(*sessionMgr, addDynamicPeer_(kPeerPrefix1, params1)).Times(1);
  auto params2 = config->getPeeringParamsForDynamicPeer(
      *dynamicPeerToConfig.at(kPeerPrefix2));
  EXPECT_CALL(*sessionMgr, addDynamicPeer_(kPeerPrefix2, params2)).Times(1);

  auto peerToConfig = config->getPeerToConfig();
  EXPECT_EQ(2, peerToConfig.size());
  auto params3 = config->getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr3));
  EXPECT_CALL(*sessionMgr, addPeer_(kPeerAddr3, params3, _)).Times(1);
  auto params4 = config->getPeeringParamsForPeer(*peerToConfig.at(kPeerAddr4));
  EXPECT_CALL(*sessionMgr, addPeer_(kPeerAddr4, params4, _)).Times(1);

  EXPECT_CALL(*sessionMgr, stopPeer(kPeerAddr3, true /*withGR*/)).Times(1);
  EXPECT_CALL(*sessionMgr, stopPeer(kPeerAddr5, true /*withGR*/)).Times(1);
  EXPECT_CALL(*sessionMgr, stopDynamicPeerWithGracefulRestart(kPeerPrefix1))
      .Times(1);
  EXPECT_CALL(*sessionMgr, stopDynamicPeerWithGracefulRestart(kV6Prefix1))
      .Times(1);
  // create peer manager thread
  auto mockPeerMgrThread = mockPeerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  folly::EventBase evb;
  auto& fm = folly::fibers::getFiberManager(evb, options_);
  // start testing peer field by feild
  fm.addTask([&] {
    // add configured static peers (kPeerAddr3,kPeerAddr4) to session manager
    mockPeerMgr->addPeersToSessionMgr();
    auto sessions = getSessionsViaSessionMgr(*mockPeerMgr);
    EXPECT_EQ(4, sessions.size());
    // restart a  peer session
    sessionMgr->restartSession(kPeerAddr3);
    sessions = getSessionsViaSessionMgr(*mockPeerMgr);
    EXPECT_EQ(4, sessions.size());
    // restart an unconfigured peer session
    sessionMgr->restartSession(kPeerAddr5);
    sessions = getSessionsViaSessionMgr(*mockPeerMgr);
    EXPECT_EQ(4, sessions.size());
    // restart a dynamic peer session
    sessionMgr->restartSession(kPeerPrefix1);
    sessions = getSessionsViaSessionMgr(*mockPeerMgr);
    EXPECT_EQ(4, sessions.size());
    // restart an unconfigured dynamic peer session
    sessionMgr->restartSession(kV6Prefix1);
    sessions = getSessionsViaSessionMgr(*mockPeerMgr);
    EXPECT_EQ(4, sessions.size());

    // stop PeerManagerBase
    mockPeerMgr->stop();
    sessionMgr->stop();
  });

  evb.loop();
  mockPeerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}
TEST_P(
    PeerManagerFixtureCanaryKnobTestSuite,
    Exportfb303CounterSessionUpAndDownTest) {
  // counters for ThreadCachedData
  std::map<std::string, int64_t> counters;
  counters.clear();

  /*
   * Set up
   */
  auto mockPeerMgr = setupMockPeerManager(true, true, false);
  auto& evb = mockPeerMgr->getEventBase();

  mockPeerMgr->ribInitPathComputationNotified_ = false;
  auto sessionMgr = setupMockSessionManager(mockPeerMgr);
  auto adjRibInQueue = sessionMgr->oQueue_;
  auto sessionMgrThread = sessionMgr->runInThread();

  uint64_t version = 0x100;
  auto versionNumber = std::make_shared<VersionNumber>(version);
  auto& fm =
      folly::fibers::getFiberManager(mockPeerMgr->getEventBase(), options_);

  folly::fibers::Baton enableLoggingBaton, finishBaton;

  auto sessionInfo = FiberBgpPeer::getObservableSessionInfo(
      mockInfo1_,
      nullptr /* unbounded input queue, unused */,
      sessionMgr->boundedIqueue_,
      adjRibInQueue,
      versionNumber);

  FLAGS_enable_peer_status_logging = GetParam();
  fm.addTask([&] {
    FiberBgpPeer::ObservableStateT stateEvent{
        .peerId = kPeerId3,
        .state = BgpSessionState::ESTABLISHED,
        .versionNumber = version,
        .remoteAs = mockInfo1_.peeringParams.remoteAs,
        .sessionInfo = sessionInfo};

    {
      /*
       * Establish BGP session
       */
      folly::coro::blockingWait(mockPeerMgr->sessionEstablished(stateEvent));

      auto adjRib3 = mockPeerMgr->findAdjRib(kPeerId3);
      ASSERT_TRUE(adjRib3);

      fiberSleepFor(20ms);
      EXPECT_EQ(true, adjRib3->isStateEstablished());
      EXPECT_EQ(true, adjRib3->inInitialAnnouncement());

      counters.clear();
      facebook::fb303::ThreadCachedServiceData::getShared()->getCounters(
          counters);

      /*
       * The peeringParams for this adjRib3 peer is {kDescription1}:v4:1 which
       * will be the ODS key
       */
      EXPECT_EQ(
          fmt::format("{}:v4:1", kDescription1),
          adjRib3->getStats().peerIdOdsStr);

      if (FLAGS_enable_peer_status_logging) {
        /*
         * FLAGS_enable_peer_status_logging = true;
         * Verify that the fb303 counter is published
         */
        EXPECT_EQ(
            1,
            counters.count(
                fmt::format(
                    PeerStats::kPeerStatus, adjRib3->getStats().peerIdOdsStr)));
        /*
         * Verify that the fb303 counter is published with value 1 (session goes
         * up)
         */
        EXPECT_EQ(
            1,
            counters.at(
                fmt::format(
                    PeerStats::kPeerStatus, adjRib3->getStats().peerIdOdsStr)));
      } else {
        /*
         * FLAGS_enable_peer_status_logging = false;
         * Verify that the fb303 counter is NOT published
         */
        EXPECT_EQ(
            0,
            counters.count(
                fmt::format(
                    PeerStats::kPeerStatus, adjRib3->getStats().peerIdOdsStr)));
      }
    }
    {
      /*
       * Shutdown BGP session
       */
      auto adjRib3 = mockPeerMgr->findAdjRib(kPeerId3);
      ASSERT_TRUE(adjRib3);
      adjRibInQueue->fiberPush(
          FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{
              false}}); // need this line as a signal to stop BGP session
      // version += 1;

      stateEvent.state = BgpSessionState::IDLE;
      stateEvent.versionNumber = version;

      folly::coro::blockingWait(mockPeerMgr->sessionTerminated(stateEvent));
      EXPECT_EQ(false, adjRib3->inInitialAnnouncement());

      fiberSleepFor(20ms);

      // Verify session terminated
      EXPECT_EQ(false, adjRib3->isStateEstablished());

      counters.clear();
      facebook::fb303::ThreadCachedServiceData::getShared()->getCounters(
          counters);

      if (FLAGS_enable_peer_status_logging) {
        // Verify that the fb303 counter is published
        EXPECT_EQ(
            1,
            counters.count(
                fmt::format(
                    PeerStats::kPeerStatus, adjRib3->getStats().peerIdOdsStr)));
        /*
         * Verify that the fb303 counter is published with value 0 (session goes
         * down)
         */
        EXPECT_EQ(
            0,
            counters.at(
                fmt::format(
                    PeerStats::kPeerStatus, adjRib3->getStats().peerIdOdsStr)));
      } else {
        /*
         * FLAGS_enable_peer_status_logging = false;
         * Verify that the fb303 counter is NOT published
         */
        EXPECT_EQ(
            0,
            counters.count(
                fmt::format(
                    PeerStats::kPeerStatus, adjRib3->getStats().peerIdOdsStr)));
      }
    }
    mockPeerMgr->stop();
    sessionMgr->stop();
  });
  evb.loop();

  sessionMgrThread.join();

  SUCCEED();
}

TEST_F(PeerManagerTestFixture, GetBgpSummaryTest) {
  auto config = getConfig(
      true /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */,
      false /* includeDynamicVipInjectorPeer */,
      false /* enableStatefulHa */,
      false /* enableVipServer */);

  auto globalConfig = config->getBgpGlobalConfig();
  auto configManager = std::make_shared<ConfigManager>(config);
  auto mockPeerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);
  mockPeerMgr->setSessionManager(sessionMgr);

  // create peer manager thread
  auto mockPeerMgrThread = mockPeerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  // since we use mockPeerMgr, we have to manually add sessions to sessionMgr
  mockPeerMgr->addPeersToSessionMgr();

  auto sessions = getSessionsViaSessionMgr(*mockPeerMgr);
  // there are 4 peers in the setup
  EXPECT_EQ(4, sessions.size());
  // pick staticPeer1_ to verify fields are set correctly
  std::optional<TBgpSession> output;
  for (const auto& session : sessions) {
    if (session.peer_addr().value() == *staticPeer1_.peer_addr()) {
      output = session;
      break;
    }
  }

  // staticPeer1_ should be in the return vector
  EXPECT_TRUE(output);
  EXPECT_EQ(output->my_addr().value(), *staticPeer1_.local_addr());
  EXPECT_EQ(output->peer_addr().value(), *staticPeer1_.peer_addr());
  EXPECT_EQ(0, output->sent_update_msgs().value());
  EXPECT_EQ(0, output->recv_update_msgs().value());
  EXPECT_EQ(kDescription1, staticPeer1_.description().value());
  auto peer = output->peer().value();
  EXPECT_EQ(*peer.local_as(), *config->getConfig().local_as());
  EXPECT_EQ(*peer.remote_as_4_byte(), *staticPeer1_.remote_as_4_byte());
  EXPECT_EQ(*peer.hold_time(), *config->getConfig().hold_time());

  // stop PeerManagerBase gracefully
  mockPeerMgr->stop();
  sessionMgr->stop();

  // stop the thread
  mockPeerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/*
 * The per-message-type socket tx/rx counts in TBgpSessionDetail come from the
 * I/O (SessionManager) snapshot -- the source of truth for messages actually
 * written to / read from the socket. Inject counts on the snapshot and verify
 * getDetailSessionInfos surfaces them into TBgpSessionDetail.
 */
TEST_F(PeerManagerTestFixture, GetBgpSessionSocketMessageCountsTest) {
  auto config = getConfig(
      true /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */,
      false /* includeDynamicVipInjectorPeer */,
      false /* enableStatefulHa */,
      false /* enableVipServer */);

  auto globalConfig = config->getBgpGlobalConfig();
  auto configManager = std::make_shared<ConfigManager>(config);
  auto mockPeerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);
  mockPeerMgr->setSessionManager(sessionMgr);

  auto mockPeerMgrThread = mockPeerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  mockPeerMgr->addPeersToSessionMgr();

  /*
   * Fetch the I/O-thread snapshot and inject per-type tx/rx socket counts for
   * staticPeer1_, then render the detail view from the (modified) snapshot.
   */
  auto allPeers = folly::coro::blockingWait(
      mockPeerMgr->getSessionManager()->co_getAllPeerDisplayInfos());
  const auto staticPeer1Addr = folly::IPAddress(*staticPeer1_.peer_addr());
  for (auto& [addr, info] : allPeers) {
    if (addr == staticPeer1Addr) {
      info->txMsgs.open = 1;
      info->txMsgs.update = 7;
      info->txMsgs.keepAlive = 3;
      info->txMsgs.notification = 2;
      info->txMsgs.routeRefresh = 4;
      info->txMsgs.endOfRib = 5;
      info->rxMsgs.open = 10;
      info->rxMsgs.update = 70;
      info->rxMsgs.keepAlive = 30;
      info->rxMsgs.notification = 20;
      info->rxMsgs.routeRefresh = 40;
      info->rxMsgs.endOfRib = 50;
    }
  }
  auto sessions = mockPeerMgr->getDetailSessionInfos(allPeers);

  std::optional<TBgpSession> output;
  for (const auto& session : sessions) {
    if (session.peer_addr().value() == *staticPeer1_.peer_addr()) {
      output = session;
      break;
    }
  }
  ASSERT_TRUE(output);
  ASSERT_TRUE(output->details().has_value());
  const auto& d = *output->details();
  EXPECT_EQ(1, d.socket_tx_open_msgs().value());
  EXPECT_EQ(7, d.socket_tx_update_msgs().value());
  EXPECT_EQ(3, d.socket_tx_keepalive_msgs().value());
  EXPECT_EQ(2, d.socket_tx_notification_msgs().value());
  EXPECT_EQ(4, d.socket_tx_route_refresh_msgs().value());
  EXPECT_EQ(5, d.socket_tx_eor_msgs().value());
  EXPECT_EQ(10, d.socket_rx_open_msgs().value());
  EXPECT_EQ(70, d.socket_rx_update_msgs().value());
  EXPECT_EQ(30, d.socket_rx_keepalive_msgs().value());
  EXPECT_EQ(20, d.socket_rx_notification_msgs().value());
  EXPECT_EQ(40, d.socket_rx_route_refresh_msgs().value());
  EXPECT_EQ(50, d.socket_rx_eor_msgs().value());

  mockPeerMgr->stop();
  sessionMgr->stop();
  mockPeerMgrThread.join();
  sessionMgrThread.join();
}

/*
 * The control-plane per-message-type counts in TBgpSessionDetail
 * (adjrib_recv_update_msgs / adjrib_recv_eor_msgs / adjrib_sent_update_msgs /
 * adjrib_sent_eor_msgs) come from the PeerManager-side AdjRib stats -- the
 * counterpart to the socket_* counts validated above. The recv side is always
 * per-peer, as is the sent side for every update-group state. Drive distinct
 * counts into a peer's AdjRib and its update-group and verify the peer APIs
 * continue to surface the AdjRib values after the peer joins the group.
 */
TEST_F(PeerManagerTestFixture, GetBgpSessionAdjRibMessageCountsTest) {
  auto config = getConfig(
      true /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */,
      false /* includeDynamicVipInjectorPeer */,
      false /* enableStatefulHa */,
      false /* enableVipServer */);

  auto globalConfig = config->getBgpGlobalConfig();
  auto configManager = std::make_shared<ConfigManager>(config);
  auto mockPeerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);
  mockPeerMgr->setSessionManager(sessionMgr);

  /*
   * addPeersToSessionMgr() does not create AdjRibs (those are created on
   * session establishment), so insert one for staticPeer1_ ourselves.
   * getDetailSessionInfo keys adjRibs_ with {peerAddr, remoteBgpId}; an
   * added-but-not-established peer reports remoteBgpId 0 in the display
   * snapshot, so key on 0.
   */
  auto& evb = mockPeerMgr->getEventBase();
  const auto staticPeer1Addr = folly::IPAddress(*staticPeer1_.peer_addr());
  const BgpPeerId adjRibKey{staticPeer1Addr, 0};
  auto adjRib =
      setupMockAdjRib(evb, adjRibKey, AsNum(kAsn1), sessionTerminateBaton_);

  constexpr uint64_t kPerPeerRecvUpdates = 3;
  constexpr uint64_t kPerPeerRecvEoRs = 2;
  constexpr uint64_t kPerPeerSentUpdates = 5;
  constexpr uint64_t kPerPeerSentEoRs = 1;
  for (uint64_t i = 0; i < kPerPeerRecvUpdates; ++i) {
    adjRib->stats_.incrementRecvUpdateMsgs();
  }
  for (uint64_t i = 0; i < kPerPeerRecvEoRs; ++i) {
    adjRib->stats_.incrementRecvEndOfRibMsgs();
  }
  adjRib->stats_.incrementSentUpdateMsgs(kPerPeerSentUpdates);
  adjRib->stats_.incrementSentEndOfRibMsgs(kPerPeerSentEoRs);

  constexpr uint64_t kPerPeerSentAnnouncementsV4 = 7;
  constexpr uint64_t kPerPeerSentAnnouncementsV6 = 8;
  constexpr uint64_t kPerPeerSentWithdrawals = 9;
  for (uint64_t i = 0; i < kPerPeerSentAnnouncementsV4; ++i) {
    adjRib->stats_.incrementSentAnnouncementsIpv4();
  }
  for (uint64_t i = 0; i < kPerPeerSentAnnouncementsV6; ++i) {
    adjRib->stats_.incrementSentAnnouncementsIpv6();
  }
  for (uint64_t i = 0; i < kPerPeerSentWithdrawals; ++i) {
    adjRib->stats_.incrementSentWithdrawals();
  }
  adjRib->stats_.incrementPreInPrefixCount(
      folly::IPAddress::createNetwork("192.0.2.0/24"), false, false);
  adjRib->stats_.incrementPreInPrefixCount(
      folly::IPAddress::createNetwork("2001:db8::/64"), false, false);
  constexpr uint32_t kPerPeerPreOutV4 = 10;
  constexpr uint32_t kPerPeerPreOutV6 = 11;
  for (uint32_t i = 0; i < kPerPeerPreOutV4; ++i) {
    adjRib->stats_.incrementPreOutPrefixCount(/*isIpv4=*/true);
  }
  for (uint32_t i = 0; i < kPerPeerPreOutV6; ++i) {
    adjRib->stats_.incrementPreOutPrefixCount(/*isIpv4=*/false);
  }
  constexpr uint32_t kPerPeerPostOutV4 = 2;
  constexpr uint32_t kPerPeerPostOutV6 = 3;
  for (uint32_t i = 0; i < kPerPeerPostOutV4; ++i) {
    adjRib->stats_.incrementPostOutPrefixCount(true);
  }
  for (uint32_t i = 0; i < kPerPeerPostOutV6; ++i) {
    adjRib->stats_.incrementPostOutPrefixCount(false);
  }
  mockPeerMgr->adjRibs_[adjRibKey] = adjRib;

  /*
   * The manually-inserted MockAdjRib's stop()/cleanupGrState() may be invoked
   * during peer-manager shutdown; delegate them to no-op coroutines so awaiting
   * them does not hit a default-constructed Task.
   */
  EXPECT_CALL(*adjRib, stop()).WillRepeatedly([]() -> folly::coro::Task<void> {
    co_return;
  });
  EXPECT_CALL(*adjRib, cleanupGrState(testing::_))
      .WillRepeatedly([](bool) -> folly::coro::Task<void> { co_return; });

  auto mockPeerMgrThread = mockPeerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();
  mockPeerMgr->addPeersToSessionMgr();

  auto allPeers = folly::coro::blockingWait(
      mockPeerMgr->getSessionManager()->co_getAllPeerDisplayInfos());

  auto detailFor =
      [&](const std::vector<TBgpSession>& sessions) -> TBgpSessionDetail {
    for (const auto& session : sessions) {
      if (session.peer_addr().value() == *staticPeer1_.peer_addr()) {
        EXPECT_TRUE(session.details().has_value());
        return session.details().value();
      }
    }
    ADD_FAILURE() << "staticPeer1_ session not found";
    return TBgpSessionDetail{};
  };

  /*
   * Phase 1: no update-group attached -> per-peer control-plane counts flow
   * straight through to the adjrib_* fields (sent side is per-peer).
   */
  {
    auto sessions = mockPeerMgr->getDetailSessionInfos(allPeers);
    const auto d = detailFor(sessions);
    EXPECT_EQ(kPerPeerRecvUpdates, d.adjrib_recv_update_msgs().value());
    EXPECT_EQ(kPerPeerRecvEoRs, d.adjrib_recv_eor_msgs().value());
    EXPECT_EQ(kPerPeerSentUpdates, d.adjrib_sent_update_msgs().value());
    EXPECT_EQ(kPerPeerSentEoRs, d.adjrib_sent_eor_msgs().value());
    /*
     * legacy_v4_nlri_encoding is surfaced from the AdjRib update-group key
     * (default false for an MP-capable peer).
     */
    EXPECT_FALSE(d.legacy_v4_nlri_encoding().value());
    // No group yet: announcement/withdrawal PDU counts are per-peer.
    EXPECT_EQ(
        kPerPeerSentAnnouncementsV4,
        d.sent_update_announcements_ipv4().value());
    EXPECT_EQ(
        kPerPeerSentAnnouncementsV6,
        d.sent_update_announcements_ipv6().value());
    EXPECT_EQ(kPerPeerSentWithdrawals, d.sent_update_withdrawals().value());
    EXPECT_EQ(1, d.prepolicy_rcvd_prefix_count_ipv4().value());
    EXPECT_EQ(1, d.prepolicy_rcvd_prefix_count_ipv6().value());
    EXPECT_EQ(kPerPeerPreOutV4, d.prepolicy_sent_prefix_count_ipv4().value());
    EXPECT_EQ(kPerPeerPreOutV6, d.prepolicy_sent_prefix_count_ipv6().value());
    EXPECT_EQ(kPerPeerPostOutV4, d.postpolicy_sent_prefix_count_ipv4().value());
    EXPECT_EQ(kPerPeerPostOutV6, d.postpolicy_sent_prefix_count_ipv6().value());
  }

  /*
   * Phase 2: peer JOINED_RUNNING in an update-group. The group has distinct
   * lifetime totals, but peer-scoped APIs must continue to report the peer's
   * own cumulative counts.
   */
  folly::EventBase groupEvb;
  auto group = std::make_shared<AdjRibOutGroup>(groupEvb, "test_group", 1);
  constexpr uint64_t kGroupSentUpdates = 40;
  constexpr uint64_t kGroupSentEoRs = 4;
  group->stats_.incrementSentUpdateMsgs(kGroupSentUpdates);
  group->stats_.incrementSentEndOfRibMsgs(kGroupSentEoRs);
  constexpr uint64_t kGroupSentAnnouncementsV4 = 41;
  constexpr uint64_t kGroupSentAnnouncementsV6 = 42;
  constexpr uint64_t kGroupSentWithdrawals = 43;
  for (uint64_t i = 0; i < kGroupSentAnnouncementsV4; ++i) {
    group->stats_.incrementSentAnnouncementsIpv4();
  }
  for (uint64_t i = 0; i < kGroupSentAnnouncementsV6; ++i) {
    group->stats_.incrementSentAnnouncementsIpv6();
  }
  for (uint64_t i = 0; i < kGroupSentWithdrawals; ++i) {
    group->stats_.incrementSentWithdrawals();
  }
  constexpr uint32_t kGroupPreOutV4 = 6;
  constexpr uint32_t kGroupPreOutV6 = 7;
  for (uint32_t i = 0; i < kGroupPreOutV4; ++i) {
    group->stats_.incrementPreOutPrefixCount(/*isIpv4=*/true);
  }
  for (uint32_t i = 0; i < kGroupPreOutV6; ++i) {
    group->stats_.incrementPreOutPrefixCount(/*isIpv4=*/false);
  }
  constexpr uint32_t kGroupPostOutV4 = 4;
  constexpr uint32_t kGroupPostOutV6 = 5;
  for (uint32_t i = 0; i < kGroupPostOutV4; ++i) {
    group->stats_.incrementPostOutPrefixCount(true);
  }
  for (uint32_t i = 0; i < kGroupPostOutV6; ++i) {
    group->stats_.incrementPostOutPrefixCount(false);
  }
  adjRib->setUpdateGroup(group);
  adjRib->setPeerState(PeerUpdateState::JOINED_RUNNING);
  {
    auto sessions = mockPeerMgr->getDetailSessionInfos(allPeers);
    const auto d = detailFor(sessions);
    EXPECT_EQ(kPerPeerRecvUpdates, d.adjrib_recv_update_msgs().value());
    EXPECT_EQ(kPerPeerRecvEoRs, d.adjrib_recv_eor_msgs().value());
    EXPECT_EQ(kPerPeerSentUpdates, d.adjrib_sent_update_msgs().value());
    EXPECT_EQ(kPerPeerSentEoRs, d.adjrib_sent_eor_msgs().value());
    EXPECT_EQ(
        kPerPeerSentAnnouncementsV4,
        d.sent_update_announcements_ipv4().value());
    EXPECT_EQ(
        kPerPeerSentAnnouncementsV6,
        d.sent_update_announcements_ipv6().value());
    EXPECT_EQ(kPerPeerSentWithdrawals, d.sent_update_withdrawals().value());
    EXPECT_EQ(1, d.prepolicy_rcvd_prefix_count_ipv4().value());
    EXPECT_EQ(1, d.prepolicy_rcvd_prefix_count_ipv6().value());
    EXPECT_EQ(kGroupPreOutV4, d.prepolicy_sent_prefix_count_ipv4().value());
    EXPECT_EQ(kGroupPreOutV6, d.prepolicy_sent_prefix_count_ipv6().value());
    EXPECT_EQ(kGroupPostOutV4, d.postpolicy_sent_prefix_count_ipv4().value());
    EXPECT_EQ(kGroupPostOutV6, d.postpolicy_sent_prefix_count_ipv6().value());

    auto session = std::find_if(
        sessions.begin(), sessions.end(), [&](const auto& candidate) {
          return candidate.peer_addr().value() == *staticPeer1_.peer_addr();
        });
    ASSERT_NE(sessions.end(), session);
    EXPECT_EQ(kPerPeerSentUpdates, session->sent_update_msgs().value());
    EXPECT_EQ(2, session->prepolicy_rcvd_prefix_count().value());
    EXPECT_EQ(
        kGroupPostOutV4 + kGroupPostOutV6,
        session->postpolicy_sent_prefix_count().value());
  }

  adjRib->setPeerState(PeerUpdateState::JOINED_BLOCKED);
  {
    const auto d = detailFor(mockPeerMgr->getDetailSessionInfos(allPeers));
    EXPECT_EQ(kGroupPreOutV4, d.prepolicy_sent_prefix_count_ipv4().value());
    EXPECT_EQ(kGroupPreOutV6, d.prepolicy_sent_prefix_count_ipv6().value());
    EXPECT_EQ(kGroupPostOutV4, d.postpolicy_sent_prefix_count_ipv4().value());
    EXPECT_EQ(kGroupPostOutV6, d.postpolicy_sent_prefix_count_ipv6().value());
  }

  adjRib->setPeerState(PeerUpdateState::DETACHED_BLOCKED);
  {
    const auto d = detailFor(mockPeerMgr->getDetailSessionInfos(allPeers));
    EXPECT_EQ(kPerPeerPreOutV4, d.prepolicy_sent_prefix_count_ipv4().value());
    EXPECT_EQ(kPerPeerPreOutV6, d.prepolicy_sent_prefix_count_ipv6().value());
    EXPECT_EQ(kPerPeerPostOutV4, d.postpolicy_sent_prefix_count_ipv4().value());
    EXPECT_EQ(kPerPeerPostOutV6, d.postpolicy_sent_prefix_count_ipv6().value());
  }

  // Detach the group before tearing down groupEvb, which must outlive it.
  folly::coro::blockingWait(group->drainAsyncScope());
  adjRib->setUpdateGroup(nullptr);
  group.reset();

  mockPeerMgr->stop();
  sessionMgr->stop();
  mockPeerMgrThread.join();
  sessionMgrThread.join();
}

/*
 * This testlet verify the session detail information retrieve from
 * PeerManagerBase::getDetailSessionInfos via the split sessionMgr +
 * PeerManagerBase coro pattern for each neighbor.
 */
CO_TEST_F(PeerManagerTestFixture, GetBgpNeighborsTest) {
  auto config = getConfig(
      true /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */,
      false /* includeDynamicVipInjectorPeer */,
      false /* enableStatefulHa */,
      false /* enableVipServer */);

  auto globalConfig = config->getBgpGlobalConfig();
  auto configManager = std::make_shared<ConfigManager>(config);
  auto mockPeerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);
  mockPeerMgr->setSessionManager(sessionMgr);

  // create peer manager thread
  auto mockPeerMgrThread = mockPeerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  // since we use mockPeerMgr, we have to manually add sessions to sessionMgr
  mockPeerMgr->addPeersToSessionMgr();

  /*
   * Checking each BGP neighbor/session if it has the valid value
   *
   * 1st of 4 sessions
   *
   * co_getPeerDisplayInfo does not support dynamic peers yet (it does not take
   * CIDR)
   * TODO: T190236431
   */
  std::string peerPrefix1Str =
      fmt::format("{}/{}", kPeerPrefix1.first.str(), kPeerPrefix1.second);
  // CIDR is not a valid IP address, so it gets filtered out in the coro path
  EXPECT_FALSE(folly::IPAddress::validate(peerPrefix1Str));

  /*
   * 2nd of 4 sessions
   *
   * co_getPeerDisplayInfo does not support dynamic peers yet (it does not take
   * CIDR)
   * TODO: T190236431
   */
  std::string peerPrefix2Str =
      fmt::format("{}/{}", kPeerPrefix2.first.str(), kPeerPrefix2.second);
  EXPECT_FALSE(folly::IPAddress::validate(peerPrefix2Str));

  /*
   * 3rd of 4 sessions
   *
   * statciPeer1_ (remote peerAddr3) does HAVE the description
   */
  {
    std::unordered_multimap<
        folly::IPAddress,
        std::shared_ptr<BgpPeerDisplayInfo>>
        peerInfoMap;
    auto peerInfoVector =
        co_await sessionMgr->co_getPeerDisplayInfo(kPeerAddr3);
    EXPECT_TRUE(peerInfoVector.has_value());
    if (peerInfoVector.has_value()) {
      peerInfoMap.emplace(
          kPeerAddr3,
          std::make_shared<BgpPeerDisplayInfo>(peerInfoVector.value()[0]));
      auto sessions = mockPeerMgr->getDetailSessionInfos(peerInfoMap);
      /*
       * Even though the return value is a vector, it should
       * have 1 session in this case
       */
      EXPECT_EQ(1, sessions.size());
      // pick staticPeer1_ to verify fields are set correctly
      auto queriedSession = sessions.at(0);

      EXPECT_EQ(queriedSession.my_addr(), *staticPeer1_.local_addr());
      EXPECT_EQ(queriedSession.peer_addr(), *staticPeer1_.peer_addr());
      EXPECT_EQ(queriedSession.description(), *staticPeer1_.description());
      auto peer = *queriedSession.peer();
      EXPECT_EQ(peer.local_as(), *config->getConfig().local_as());
      EXPECT_EQ(peer.remote_as_4_byte(), *staticPeer1_.remote_as_4_byte());
      EXPECT_EQ(*peer.hold_time(), *config->getConfig().hold_time());
    }
  }

  /*
   * 4th of 4 sessions
   *
   * statciPeer2_ (remote peerAddr4) does NOT have description
   */
  {
    std::unordered_multimap<
        folly::IPAddress,
        std::shared_ptr<BgpPeerDisplayInfo>>
        peerInfoMap;
    auto peerInfoVector =
        co_await sessionMgr->co_getPeerDisplayInfo(kPeerAddr4);
    EXPECT_TRUE(peerInfoVector.has_value());
    if (peerInfoVector.has_value()) {
      peerInfoMap.emplace(
          kPeerAddr4,
          std::make_shared<BgpPeerDisplayInfo>(peerInfoVector.value()[0]));
      auto sessions = mockPeerMgr->getDetailSessionInfos(peerInfoMap);
      /*
       * Even though the return value is a vector, it should
       * have 1 session in this case
       */
      EXPECT_EQ(1, sessions.size());
      // pick statciPeer2_ to verify fields are set correctly
      auto queriedSession = sessions.at(0);

      EXPECT_EQ(queriedSession.my_addr(), *staticPeer2_.local_addr());
      EXPECT_EQ(queriedSession.peer_addr(), *staticPeer2_.peer_addr());
      EXPECT_EQ(queriedSession.description(), "");
      auto peer = *queriedSession.peer();
      EXPECT_EQ(peer.local_as(), *config->getConfig().local_as());
      EXPECT_EQ(peer.remote_as_4_byte(), *staticPeer2_.remote_as_4_byte());
      EXPECT_EQ(*peer.hold_time(), *config->getConfig().hold_time());
    }
  }

  // Unknown peerAddr should not yield anything
  {
    auto peerInfoVector = co_await sessionMgr->co_getPeerDisplayInfo(
        folly::IPAddress("202.47.242.123"));
    EXPECT_FALSE(peerInfoVector.has_value());
  }

  // Unknown prefix should not yield anything (CIDR is not a valid IP)
  EXPECT_FALSE(folly::IPAddress::validate("202.47.242.123/24"));

  // invalid IP address format should not yield anything
  EXPECT_FALSE(folly::IPAddress::validate("hello welcome"));

  // empty string should not yield anything since it's not a valid IP address
  EXPECT_FALSE(folly::IPAddress::validate(""));

  // stop PeerManagerBase gracefully
  mockPeerMgr->stop();
  sessionMgr->stop();

  // stop the thread
  mockPeerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/*
 * This test verifies if an empty peers list is provided, getDetailSessionInfos
 * via co_getAllPeerDisplayInfos returns information of every BGP
 * neighbor/session (similarly to getBgpSummary), but with session detail.
 */
CO_TEST_F(PeerManagerTestFixture, GetAllBgpNeighborsTest) {
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);

  auto globalConfig = config->getBgpGlobalConfig();

  auto configManager = std::make_shared<ConfigManager>(config);
  auto mockPeerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);
  mockPeerMgr->setSessionManager(sessionMgr);

  // create peer manager thread
  auto mockPeerMgrThread = mockPeerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  mockPeerMgr->addPeersToSessionMgr();

  auto allPeers = co_await sessionMgr->co_getAllPeerDisplayInfos();
  auto sessions = mockPeerMgr->getDetailSessionInfos(allPeers);
  EXPECT_EQ(sessions.size(), 4);

  // stop PeerManagerBase gracefully
  mockPeerMgr->stop();
  sessionMgr->stop();

  // stop the thread
  mockPeerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/*
 * Test getDetailSessionInfos() with specific peer addresses via the split
 * sessionMgr + PeerManagerBase coro pattern (mirrors GetBgpNeighborsTest)
 */
CO_TEST_F(PeerManagerTestFixture, GetDetailSessionInfosTest) {
  auto config = getConfig(
      true /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */,
      false /* includeDynamicVipInjectorPeer */,
      false /* enableStatefulHa */,
      false /* enableVipServer */);

  auto globalConfig = config->getBgpGlobalConfig();
  auto configManager = std::make_shared<ConfigManager>(config);
  auto mockPeerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);
  mockPeerMgr->setSessionManager(sessionMgr);

  auto mockPeerMgrThread = mockPeerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  mockPeerMgr->addPeersToSessionMgr();

  // Static peer should return 1 session with detail info
  {
    std::unordered_multimap<
        folly::IPAddress,
        std::shared_ptr<BgpPeerDisplayInfo>>
        peerInfoMap;
    auto peerInfoVector =
        co_await sessionMgr->co_getPeerDisplayInfo(kPeerAddr3);
    EXPECT_TRUE(peerInfoVector.has_value());
    if (peerInfoVector.has_value()) {
      peerInfoMap.emplace(
          kPeerAddr3,
          std::make_shared<BgpPeerDisplayInfo>(peerInfoVector.value()[0]));
      auto sessions = mockPeerMgr->getDetailSessionInfos(peerInfoMap);
      EXPECT_EQ(1, sessions.size());
      auto queriedSession = sessions.at(0);
      EXPECT_EQ(queriedSession.my_addr(), *staticPeer1_.local_addr());
      EXPECT_EQ(queriedSession.peer_addr(), *staticPeer1_.peer_addr());
      EXPECT_EQ(queriedSession.description(), *staticPeer1_.description());
      EXPECT_TRUE(queriedSession.details().has_value());
    }
  }

  // Second static peer
  {
    std::unordered_multimap<
        folly::IPAddress,
        std::shared_ptr<BgpPeerDisplayInfo>>
        peerInfoMap;
    auto peerInfoVector =
        co_await sessionMgr->co_getPeerDisplayInfo(kPeerAddr4);
    EXPECT_TRUE(peerInfoVector.has_value());
    if (peerInfoVector.has_value()) {
      peerInfoMap.emplace(
          kPeerAddr4,
          std::make_shared<BgpPeerDisplayInfo>(peerInfoVector.value()[0]));
      auto sessions = mockPeerMgr->getDetailSessionInfos(peerInfoMap);
      EXPECT_EQ(1, sessions.size());
      auto queriedSession = sessions.at(0);
      EXPECT_EQ(queriedSession.my_addr(), *staticPeer2_.local_addr());
      EXPECT_EQ(queriedSession.peer_addr(), *staticPeer2_.peer_addr());
      EXPECT_EQ(queriedSession.description(), "");
      EXPECT_TRUE(queriedSession.details().has_value());
    }
  }

  // Nonexistent peer should return no value from sessionMgr
  {
    auto peerInfoVector = co_await sessionMgr->co_getPeerDisplayInfo(
        folly::IPAddress("202.47.242.123"));
    EXPECT_FALSE(peerInfoVector.has_value());
  }

  // Empty multimap should return empty result
  {
    std::unordered_multimap<
        folly::IPAddress,
        std::shared_ptr<BgpPeerDisplayInfo>>
        emptyMap;
    auto sessions = mockPeerMgr->getDetailSessionInfos(emptyMap);
    EXPECT_EQ(0, sessions.size());
  }

  mockPeerMgr->stop();
  sessionMgr->stop();
  mockPeerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/*
 * Test getDetailSessionInfos() with all peers via the split sessionMgr +
 * PeerManagerBase coro pattern (mirrors GetAllBgpNeighborsTest)
 */
CO_TEST_F(PeerManagerTestFixture, GetAllDetailSessionInfosTest) {
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);

  auto globalConfig = config->getBgpGlobalConfig();

  auto configManager = std::make_shared<ConfigManager>(config);
  auto mockPeerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);
  mockPeerMgr->setSessionManager(sessionMgr);

  auto mockPeerMgrThread = mockPeerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  mockPeerMgr->addPeersToSessionMgr();

  // co_await to get all peers, then render via getDetailSessionInfos
  auto allPeers = co_await sessionMgr->co_getAllPeerDisplayInfos();
  auto sessions = mockPeerMgr->getDetailSessionInfos(allPeers);
  EXPECT_EQ(sessions.size(), 4);

  // Verify each session has detail info populated
  for (const auto& session : sessions) {
    EXPECT_TRUE(session.details().has_value());
  }

  mockPeerMgr->stop();
  sessionMgr->stop();
  mockPeerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/*
 * This test verifies getBgpNeighborsFromSession via the split sessionMgr +
 * PeerManagerBase coro pattern: co_await sessionMgr->co_getPeerDisplayInfo(
 * BgpPeerId), then call getDetailSessionInfos.
 */
CO_TEST_F(PeerManagerTestFixture, GetBgpNeighborsFromSessionTest) {
  auto config = getConfig(
      true /* includeStaticPeer */,
      true /* includeDynamicShivPeer */,
      false /* includeDynamicMonitorPeer */,
      false /* includeDynamicVipInjectorPeer */,
      false /* enableStatefulHa */,
      false /* enableVipServer */);
  auto globalConfig = config->getBgpGlobalConfig();

  auto configManager = std::make_shared<ConfigManager>(config);
  auto mockPeerMgr = std::make_shared<MockPeerManager>(
      configManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto sessionMgr = std::make_shared<MockSessionManager>(*globalConfig, false);
  mockPeerMgr->setSessionManager(sessionMgr);

  // create peer manager thread
  auto mockPeerMgrThread = mockPeerMgr->runInThread();
  auto sessionMgrThread = sessionMgr->runInThread();

  // since we use mockPeerMgr, we have to manually add sessions to sessionMgr
  mockPeerMgr->addPeersToSessionMgr();

  /*
   * The staticPeer1_ with remote peerAddr3 has is added to sessionMgr by
   * mockPeerMgr and the configuration has its remoteBgpId as "0.0.0.0"
   *
   * statciPeer1_ (remote peerAddr3) does HAVE the description
   */

  auto sessions = getSessionsViaSessionMgr(*mockPeerMgr);
  EXPECT_EQ(4, sessions.size());

  /*
   * co_getPeerDisplayInfo(BgpPeerId) with valid peer + session
   * The session is not established so the query by specific BgpPeerId
   * may not find anything
   */
  {
    BgpPeerId bgpPeerId{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
    auto peerInfoVector = co_await sessionMgr->co_getPeerDisplayInfo(bgpPeerId);
    std::vector<TBgpSession> result;
    if (peerInfoVector.has_value()) {
      std::unordered_multimap<
          folly::IPAddress,
          std::shared_ptr<BgpPeerDisplayInfo>>
          peerInfoMap;
      peerInfoMap.emplace(
          kPeerAddr3,
          std::make_shared<BgpPeerDisplayInfo>(peerInfoVector.value()[0]));
      result = mockPeerMgr->getDetailSessionInfos(peerInfoMap);
    }
    /*
     * getBgpNeighborsFromSession can only retrieve the "established" session,
     * which requires the work on the FiberBgpPeerManager
     *
     * This functionality of session is established is tested in
     * FiberBgpPeerManagerTest
     */
    EXPECT_EQ(0, result.size());
  }

  /*
   * Nonexistent BGP session
   */
  {
    BgpPeerId bgpPeerId{
        folly::IPAddress("5.6.7.8"), kPeerAddr3.asV4().toLongHBO()};
    auto peerInfoVector = co_await sessionMgr->co_getPeerDisplayInfo(bgpPeerId);
    EXPECT_FALSE(peerInfoVector.has_value());
  }

  {
    BgpPeerId bgpPeerId{kPeerAddr3, folly::IPAddressV4("5.6.7.8").toLongHBO()};
    auto peerInfoVector = co_await sessionMgr->co_getPeerDisplayInfo(bgpPeerId);
    EXPECT_FALSE(peerInfoVector.has_value());
  }

  /*
   * Invalid peerAddr or invalid sessionId (remoteBgpId) are caught by
   * folly::IPAddress::validate before constructing BgpPeerId
   */
  EXPECT_FALSE(folly::IPAddress::validate("hello"));
  EXPECT_FALSE(folly::IPAddress::validate("invalid"));

  // stop PeerManagerBase gracefully
  mockPeerMgr->stop();
  sessionMgr->stop();

  // stop the thread
  mockPeerMgrThread.join();
  sessionMgrThread.join();
  SUCCEED();
}

/*
 * This test setup static peers by the MockPeerManager.
 * The testlet will query the `sessionInfo` of one static peer and verify its
 * result
 *
 * Note: the `getSessionInfo` is also called by the `getDetailedSessionInfo`
 */
TEST_F(PeerManagerTestFixture, GetSessionInfoStaticPeerTest) {
  auto mockPeerMgr = setupMockPeerManagerWithSeparateThread(true, false);
  auto mockSessionMgr = setupMockSessionManager(mockPeerMgr);
  auto mockPeerMgrThread = mockPeerMgr->runInThread();

  // Static peer
  uint64_t kNumReset = 3;
  uint64_t kLastWentDownHoursAgo = 1;
  auto kLastResetReason = ResetReason::NOTIFICATION_RCVD;
  auto mockPeerInfo3 = getMockPeerInfo(
      kPeerAddr3,
      kPeerId3.remoteBgpId,
      kNumReset,
      kLastWentDownHoursAgo,
      kLastResetReason);
  mockPeerInfo3->remoteAs = kAsn2;

  // Let this static peer be up for at least 5 seconds
  const std::chrono::seconds kUptime{5};

  folly::EventBase evb;
  // schdule to call the `getSessionInfo` after kUptime (5 seconds) has passed
  evb.scheduleAt(
      [&]() {
        // Static peer
        TBgpSession tBgpSession3 =
            mockPeerMgr->getSessionInfo(kPeerAddr3, mockPeerInfo3);
        // Verify that the returned TBgpSession object has the expected values
        EXPECT_EQ(
            tBgpSession3.my_addr(), getAddressStr(mockPeerInfo3->localAddr));
        EXPECT_EQ(tBgpSession3.peer_addr(), kPeerAddr3.str());
        EXPECT_EQ(tBgpSession3.peer_bgp_id(), kPeerAddr3.str());
        EXPECT_EQ(tBgpSession3.rcvd_prefix_count(), 0);
        EXPECT_EQ(tBgpSession3.sent_prefix_count(), 0);
        EXPECT_EQ(
            tBgpSession3.description(),
            kDescription1); // staticPeer1_ (remote peerAddr is kPeerAddr3) has
                            // the description
        EXPECT_GE(tBgpSession3.uptime(), kUptime.count());
        EXPECT_EQ(tBgpSession3.num_resets(), kNumReset);
        EXPECT_GE(
            tBgpSession3.reset_time(),
            std::chrono::hours(kLastWentDownHoursAgo).count());
        EXPECT_EQ(
            tBgpSession3.last_reset_reason(),
            getResetReasonName(kLastResetReason));
        EXPECT_EQ(0, tBgpSession3.sent_update_msgs().value());
        EXPECT_EQ(0, tBgpSession3.recv_update_msgs().value());
        auto tBgpPeer3 = *tBgpSession3.peer();
        EXPECT_EQ(tBgpPeer3.local_as_4_byte(), kAsn1);
        EXPECT_EQ(tBgpPeer3.remote_as(), mockPeerInfo3->peeringParams.remoteAs);
        EXPECT_EQ(tBgpPeer3.remote_as_4_byte(), kAsn2);
        EXPECT_EQ(
            tBgpPeer3.hold_time(),
            config_->getBgpGlobalConfig()->holdTime.count());
        EXPECT_EQ(tBgpPeer3.lastResetHoldTimer(), 0);
        EXPECT_EQ(tBgpPeer3.lastResetKeepAliveTimer(), 0);
        EXPECT_EQ(tBgpPeer3.lastSentKeepAlive(), 0);
        EXPECT_EQ(tBgpPeer3.lastRcvdKeepAlive(), 0);
        EXPECT_EQ(tBgpPeer3.peer_state(), TBgpPeerState::ESTABLISHED);
        mockPeerMgr->stop();
      },
      std::chrono::steady_clock::now() + kUptime);

  // let the magic happen
  evb.loop();
  // Do not forget to `join` the mockPeerMgrThread
  mockPeerMgrThread.join();
  SUCCEED();
}

/*
 * This test setup dynamic peers by the MockPeerManager.
 * The testlet will query the `getSessionInfo` of one dynamic peer and
 * verify its result
 *
 * Note: the `getSessionInfo` is also called by the `getDetailedSessionInfo`
 */
TEST_F(PeerManagerTestFixture, GetSessionInfoDynamicPeerTest) {
  auto mockPeerMgr = setupMockPeerManagerWithSeparateThread(true, false);
  auto mockSessionMgr = setupMockSessionManager(mockPeerMgr);
  auto mockPeerMgrThread = mockPeerMgr->runInThread();
  // Dynamic peer
  auto mockPeerInfo1 =
      getMockPeerInfo(kPeerPrefix1, kDynamicRouterId1, false, true);

  // Let this dynamic peer be up for at least 5 seconds
  const std::chrono::seconds kUptime{5};

  folly::EventBase evb;
  // schdule to call the `getSessionInfo` after kUptime (5 seconds) has passed
  evb.scheduleAt(
      [&]() {
        // Dynamic peer
        TBgpSession tBgpSession1 =
            mockPeerMgr->getSessionInfo(kPeerPrefix1.first, mockPeerInfo1);
        // Verify that the returned TBgpSession object has the expected values
        EXPECT_EQ(
            tBgpSession1.my_addr(), getAddressStr(mockPeerInfo1->localAddr));
        EXPECT_EQ(
            tBgpSession1.peer_addr(),
            fmt::format(
                "{}/{}", kPeerPrefix1.first.str(), kPeerPrefix1.second));
        EXPECT_EQ(tBgpSession1.peer_bgp_id(), kDynamicPeerAddr1.str());
        EXPECT_EQ(tBgpSession1.rcvd_prefix_count(), 0);
        EXPECT_EQ(tBgpSession1.sent_prefix_count(), 0);
        EXPECT_EQ(tBgpSession1.description(), "");
        EXPECT_GE(tBgpSession1.uptime(), kUptime.count());
        EXPECT_EQ(tBgpSession1.num_resets(), 0);
        EXPECT_EQ(tBgpSession1.last_reset_reason(), "");
        auto tBgpPeer1 = *tBgpSession1.peer();
        EXPECT_EQ(tBgpPeer1.local_as_4_byte(), kAsn1);
        EXPECT_EQ(
            tBgpPeer1.remote_as_4_byte(),
            mockPeerInfo1->peeringParams.remoteAs);
        EXPECT_EQ(
            tBgpPeer1.hold_time(),
            config_->getBgpGlobalConfig()->holdTime.count());
        EXPECT_EQ(tBgpPeer1.lastResetHoldTimer(), 0);
        EXPECT_EQ(tBgpPeer1.lastResetKeepAliveTimer(), 0);
        EXPECT_EQ(tBgpPeer1.lastSentKeepAlive(), 0);
        EXPECT_EQ(tBgpPeer1.lastRcvdKeepAlive(), 0);
        EXPECT_EQ(tBgpPeer1.peer_state(), TBgpPeerState::ESTABLISHED);
        mockPeerMgr->stop();
      },
      std::chrono::steady_clock::now() + kUptime);

  // let the magic happen
  evb.loop();
  // Do not forget to `join` the mockPeerMgrThread
  mockPeerMgrThread.join();
  SUCCEED();
}

//
TEST_F(PeerManagerTestFixture, GetDetailSessionInfoDynamicPeerTest) {
  auto config = getConfig(
      false, /* includeStaticPeer */
      true, /* includeDynamicShivPeer */
      false, /* includeDynamicMonitorPeer */
      false, /* includeDynamicVipInjectorPeer */
      false, /* enableStatefulHa */
      2 /* eorTimeS */);

  auto globalConfig = config->getBgpGlobalConfig();
  auto mockPeerMgr = setupMockPeerManager(true, true, false, true);

  // Set up the necessary objects and data for the test
  auto mockPeerInfo2 =
      getMockPeerInfo(kPeerPrefix2, kDynamicRouterId2, false, true);

  // Call the getDetailSessionInfo function with the mock objects and data
  TBgpSession tBgpSession2 = mockPeerMgr->getDetailSessionInfo(
      kPeerPrefix2.first, mockPeerInfo2, globalConfig);

  EXPECT_NE(tBgpSession2.details(), std::nullopt);

  // Verify that the returned TBgpSessionDetail object has the expected values
  TBgpSessionDetail tBgpSessionDetail2 = *tBgpSession2.details();
  EXPECT_EQ(
      *tBgpSessionDetail2.peer_port(),
      htons(mockPeerInfo2->peeringParams.peerPort));
  EXPECT_EQ(
      *tBgpSessionDetail2.local_router_id(), globalConfig->routerId.str());
  EXPECT_EQ(
      *tBgpSessionDetail2.local_port(),
      htons(mockPeerInfo2->localAddr.getPort()));
  EXPECT_EQ(
      *tBgpSessionDetail2.confed_peer(),
      mockPeerInfo2->peeringParams.isConfedPeer);
  EXPECT_EQ(
      *tBgpSessionDetail2.remote_bgp_id(), htonl(mockPeerInfo2->remoteBgpId));
  EXPECT_EQ(
      *tBgpSessionDetail2.ipv4_unicast(),
      *mockPeerInfo2->negotiatedCapabilities.mpExtV4Unicast());
  EXPECT_EQ(
      *tBgpSessionDetail2.ipv6_unicast(),
      *mockPeerInfo2->negotiatedCapabilities.mpExtV6Unicast());
  EXPECT_EQ(
      *tBgpSessionDetail2.gr_restart_time(),
      mockPeerInfo2->peeringParams.grRestartTime.value().count());
  EXPECT_EQ(
      *tBgpSessionDetail2.rr_client(), mockPeerInfo2->peeringParams.isRrClient);
  EXPECT_EQ(
      *tBgpSessionDetail2.connect_mode(),
      mockPeerInfo2->peeringParams.connectMode);
}

// Verify TTL security fields in TBgpSessionDetail when ttlSecurityHops is set
TEST_F(PeerManagerTestFixture, GetDetailSessionInfoTtlSecurityEnabledTest) {
  auto config = getConfig(
      false, /* includeStaticPeer */
      true, /* includeDynamicShivPeer */
      false, /* includeDynamicMonitorPeer */
      false, /* includeDynamicVipInjectorPeer */
      false, /* enableStatefulHa */
      2 /* eorTimeS */);

  auto globalConfig = config->getBgpGlobalConfig();
  auto mockPeerMgr = setupMockPeerManager(true, true, false, true);

  auto mockPeerInfo =
      getMockPeerInfo(kPeerPrefix2, kDynamicRouterId2, false, true);
  mockPeerInfo->peeringParams.ttlSecurityHops = 1;

  TBgpSession tBgpSession = mockPeerMgr->getDetailSessionInfo(
      kPeerPrefix2.first, mockPeerInfo, globalConfig);

  EXPECT_NE(tBgpSession.details(), std::nullopt);
  TBgpSessionDetail detail = *tBgpSession.details();
  EXPECT_TRUE(detail.ttl_security_enabled().has_value());
  EXPECT_EQ(*detail.ttl_security_enabled(), true);
  EXPECT_TRUE(detail.ttl_security_hops().has_value());
  EXPECT_EQ(*detail.ttl_security_hops(), 1);
}

// Verify TTL security fields are absent when ttlSecurityHops is not configured
TEST_F(PeerManagerTestFixture, GetDetailSessionInfoTtlSecurityDisabledTest) {
  auto config = getConfig(
      false, /* includeStaticPeer */
      true, /* includeDynamicShivPeer */
      false, /* includeDynamicMonitorPeer */
      false, /* includeDynamicVipInjectorPeer */
      false, /* enableStatefulHa */
      2 /* eorTimeS */);

  auto globalConfig = config->getBgpGlobalConfig();
  auto mockPeerMgr = setupMockPeerManager(true, true, false, true);

  auto mockPeerInfo =
      getMockPeerInfo(kPeerPrefix2, kDynamicRouterId2, false, true);
  // ttlSecurityHops is std::nullopt by default

  TBgpSession tBgpSession = mockPeerMgr->getDetailSessionInfo(
      kPeerPrefix2.first, mockPeerInfo, globalConfig);

  EXPECT_NE(tBgpSession.details(), std::nullopt);
  TBgpSessionDetail detail = *tBgpSession.details();
  EXPECT_FALSE(detail.ttl_security_enabled().has_value());
  EXPECT_FALSE(detail.ttl_security_hops().has_value());
}

/*
 * This test verify the getNetworks function correctness when it receives
 * an empty input/argument
 */
TEST_F(PeerManagerTestFixture, GetNetworksEmptyInputTest) {
  // Create static peer and dynamic peers mockPeerManager
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  const std::unique_ptr<std::string> emptyPeer(nullptr);

  // Test invalid peerAddr is an empty argument on getNetworks() function
  std::map<TIpPrefix, TBgpPath> output;
  mockPeerMgr->getNetworks(
      output, emptyPeer /* null ptr*/, RouteFilterType::PRE_FILTER_RECEIVED);
  EXPECT_EQ(0, output.size());
}

/*
 * This test verify the getNetworks2 function correctness when it receives
 * an empty input/argument
 */
TEST_F(PeerManagerTestFixture, GetNetworks2EmptyInputTest) {
  // Create static peer and dynamic peers mockPeerManager
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  const std::unique_ptr<std::string> emptyPeer(nullptr);

  // Test invalid peerAddr is an empty argument on getNetworks2() function
  std::map<TIpPrefix, std::vector<TBgpPath>> output2;
  mockPeerMgr->getNetworks2(
      output2, emptyPeer /* null ptr*/, RouteFilterType::PRE_FILTER_RECEIVED);
  EXPECT_EQ(0, output2.size());
}

/*
 * This test verify the getNetworks function correctness when it receives
 * an invalid IP address as an argument
 */
TEST_F(PeerManagerTestFixture, GetNetworksInvalidIpAddressTest) {
  // Create static peer and dynamic peers mockPeerManager
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  const auto invalidPeerAddr =
      std::make_unique<std::string>(std::string("hello"));

  // Test invalid peerAddr is an invalid IP address on getNetworks() function
  std::map<TIpPrefix, TBgpPath> output;
  mockPeerMgr->getNetworks(
      output,
      invalidPeerAddr /* invalid IP address */,
      RouteFilterType::PRE_FILTER_RECEIVED);
  EXPECT_EQ(0, output.size());
}

/*
 * This test verify the getNetworks2 function correctness when it receives
 * an invalid IP address as an argument
 */
TEST_F(PeerManagerTestFixture, GetNetworks2InvalidIpAddressTest) {
  // Create static peer and dynamic peers mockPeerManager
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  const auto invalidPeerAddr =
      std::make_unique<std::string>(std::string("hello"));

  // Test invalid peerAddr is an invalid IP address on getNetworks2() function
  std::map<TIpPrefix, std::vector<TBgpPath>> output2;
  mockPeerMgr->getNetworks2(
      output2,
      invalidPeerAddr /* invalid IP address */,
      RouteFilterType::PRE_FILTER_RECEIVED);
  EXPECT_EQ(0, output2.size());
}

/*
 * This test verify the getNetworks functions when it receives
 * an invalid remoteBgpID as an argument
 */
TEST_F(PeerManagerTestFixture, GetNetworksInvalidSessionBgpIdTest) {
  // Create static peer and dynamic peers mockPeerManager
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  const auto invalidSessionBgpId =
      std::make_unique<std::string>(std::string("hello"));

  /*
   * Test invalid sessionBgpId argument on getNetworks()
   * function
   */
  std::map<TIpPrefix, TBgpPath> output;
  mockPeerMgr->getNetworks(
      output,
      std::make_unique<std::string>(kPeerId1.peerAddr.str()),
      invalidSessionBgpId /* invalid session bgp id */,
      RouteFilterType::PRE_FILTER_RECEIVED);
  EXPECT_EQ(0, output.size());
}

/*
 * This test verify the getNetworks2 functions when it receives
 * an invalid remoteBgpID as an argument
 */
TEST_F(PeerManagerTestFixture, GetNetworks2InvalidSessionBgpIdTest) {
  // Create static peer and dynamic peers mockPeerManager
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  const auto invalidSessionBgpId =
      std::make_unique<std::string>(std::string("hello"));

  /*
   * Test invalid sessionBgpId argument on getNetworks2()
   * function
   */
  std::map<TIpPrefix, std::vector<TBgpPath>> output2;
  mockPeerMgr->getNetworks2(
      output2,
      std::make_unique<std::string>(kPeerId1.peerAddr.str()),
      invalidSessionBgpId /* invalid session bgp id */,
      RouteFilterType::PRE_FILTER_RECEIVED);
  EXPECT_EQ(0, output2.size());
}

/*
 * This test runs for GetNetworks functions, it has 2 overloading GetNetworks
 * functions
 */
TEST_F(PeerManagerTestFixture, GetNetworksTest) {
  // Create static peer and dynamic peers mockPeerManager
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);

  EXPECT_CALL(*mockPeerMgr, getNetworks(_, _, _))
      .WillRepeatedly([](std::map<TIpPrefix, TBgpPath>& prefixToPath,
                         const std::unique_ptr<std::string>& peer,
                         const RouteFilterType&) {
        // Invalid input
        if (!peer) {
          return;
        }
        // Invalid neighbor address
        if (!folly::IPAddress::validate(*peer)) {
          return;
        }
        // Fill in the prefixToPath map with some test data
        prefixToPath[TIpPrefix()] = TBgpPath();
      });

  EXPECT_CALL(*mockPeerMgr, getNetworks(_, _, _, _))
      .WillRepeatedly([](std::map<TIpPrefix, TBgpPath>& prefixToPath,
                         const std::unique_ptr<std::string>& peer,
                         const std::unique_ptr<std::string>& sessionBgpId,
                         const RouteFilterType&) {
        // Invalid input
        if (!peer || !sessionBgpId) {
          return;
        }
        // Invalid neighbor address or invalid sessionBgpId
        if (!folly::IPAddress::validate(*peer) ||
            !folly::IPAddress::validate(*sessionBgpId)) {
          return;
        }
        // Fill in the prefixToPath map with some test data
        prefixToPath[TIpPrefix()] = TBgpPath();
      });

  std::map<TIpPrefix, TBgpPath> output;

  // Test valid getNetworks() WITHOUT sessionBgpId argument
  mockPeerMgr->getNetworks(
      output,
      std::make_unique<std::string>(kPeerId1.peerAddr.str()),
      RouteFilterType::PRE_FILTER_RECEIVED);
  EXPECT_EQ(1, output.size());

  // Test valid getNetworks() WITH sessionBgpId argument (overload)
  mockPeerMgr->getNetworks(
      output,
      std::make_unique<std::string>(kPeerId1.peerAddr.str()),
      std::make_unique<std::string>(
          folly::IPAddressV4::fromLongHBO(kPeerId1.remoteBgpId).str()),
      RouteFilterType::PRE_FILTER_RECEIVED);
  EXPECT_EQ(1, output.size());
}

/*
 * This test runs for GetNetworks2 functions, it has 2 overloading GetNetworks2
 * functions
 */
TEST_F(PeerManagerTestFixture, GetNetworks2Test) {
  // Create static peer and dynamic peers mockPeerManager
  auto mockPeerMgr = setupMockPeerManager(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);

  EXPECT_CALL(*mockPeerMgr, getNetworks2(_, _, _))
      .WillRepeatedly(
          [](std::map<TIpPrefix, std::vector<TBgpPath>>& prefixToPath,
             const std::unique_ptr<std::string>& peer,
             const RouteFilterType&) {
            // Invalid input
            if (!peer) {
              return;
            }
            // Invalid neighbor address
            if (!folly::IPAddress::validate(*peer)) {
              return;
            }
            // Fill in the prefixToPath map with some test data
            prefixToPath[TIpPrefix()] = std::vector<TBgpPath>({TBgpPath()});
          });
  EXPECT_CALL(*mockPeerMgr, getNetworks2(_, _, _, _))
      .WillRepeatedly(
          [](std::map<TIpPrefix, std::vector<TBgpPath>>& prefixToPath,
             const std::unique_ptr<std::string>& peer,
             const std::unique_ptr<std::string>& sessionBgpId,
             const RouteFilterType&) {
            // Invalid input
            if (!peer || !sessionBgpId) {
              return;
            }
            // Invalid neighbor address or invalid sessionBgpId
            if (!folly::IPAddress::validate(*peer) ||
                !folly::IPAddress::validate(*sessionBgpId)) {
              return;
            }
            // Fill in the prefixToPath map with some test data
            prefixToPath[TIpPrefix()] = std::vector<TBgpPath>({TBgpPath()});
          });

  std::map<TIpPrefix, std::vector<TBgpPath>> output2;

  // Test valid getNetworks2() WITHOUT sessionBgpId argument
  mockPeerMgr->getNetworks2(
      output2,
      std::make_unique<std::string>(kPeerId1.peerAddr.str()),
      RouteFilterType::PRE_FILTER_RECEIVED);
  EXPECT_EQ(1, output2.size());
  // Test valid getNetworks2() WITH sessionBgpId argument (overload)
  mockPeerMgr->getNetworks2(
      output2,
      std::make_unique<std::string>(kPeerId1.peerAddr.str()),
      std::make_unique<std::string>(
          folly::IPAddressV4::fromLongHBO(kPeerId1.remoteBgpId).str()),
      RouteFilterType::PRE_FILTER_RECEIVED);
  EXPECT_EQ(1, output2.size());
}

/**
 * @brief  Verify API that retrieves Bgp Thrift Streaming Sessions info
 *         All we need to verify here is that we can retrieve and read
 *         each field. Various values of each field verified in various
 *         tests in PeerManagerTest.cpp
 */
TEST_F(PeerManagerTestFixture, GetBgpStreamSessionsTest) {
  auto mockPeerMgr = setupMockPeerManager(true, true, true, false);
  auto& evb = mockPeerMgr->getEventBase();

  // create peer manager thread
  auto mockPeerMgrThread = mockPeerMgr->runInThread();

  const std::unique_ptr<std::string> myName =
      std::make_unique<std::string>("testClient");
  auto stream1 = mockPeerMgr->subscribe(myName);

  // Call the getBgpStreamSummary function
  std::vector<TBgpStreamSession> tBgpStreamSessions =
      mockPeerMgr->getBgpStreamSummary();
  EXPECT_EQ(1, tBgpStreamSessions.size());
  EXPECT_EQ(kStreamPeerAddr.str(), tBgpStreamSessions[0].peer_addr());
  EXPECT_EQ("testClient", tBgpStreamSessions[0].subscriber_name());
  EXPECT_EQ(TBgpPeerState::ESTABLISHED, tBgpStreamSessions[0].state());
  /*
   * since we just have subscribed, uptime for this session should be pretty
   * minimal Verify if it has been less than a second
   */
  EXPECT_GT(1000, tBgpStreamSessions[0].uptime());
  EXPECT_EQ(0, tBgpStreamSessions[0].sent_prefix_count());
  EXPECT_EQ(0, tBgpStreamSessions[0].num_flaps());

  /*
   * Following subscription call simply required to be able to cancel
   * the stream that is subscribed earlier
   */
  auto subscription1 =
      std::move(stream1).toClientStreamUnsafeDoNotUse().subscribeExTry(
          &evb, [](auto&&) {
            // Do nothing
          });

  /*
   * cleanup channel
   * Kill channel on client side
   */
  subscription1.cancel();
  std::move(subscription1).detach();

  // stop PeerManagerBase gracefully
  mockPeerMgr->stop();

  // stop the thread
  mockPeerMgrThread.join();
  SUCCEED();
}

/*
 * Test that getSessionInfo correctly handles optional UCMP configuration fields
 * (advertiseLinkBandwidth and receiveLinkBandwidth). When these fields have
 * values, they should be set on TBgpSession; when nullopt, they should not be
 * set (leaving the thrift default values).
 */
TEST_F(PeerManagerTestFixture, GetSessionInfoUcmpOptionalFieldsTest) {
  auto mockPeerMgr = setupMockPeerManager(true, false);

  // Static peer with UCMP fields set
  auto mockPeerInfoWithUcmp = getMockPeerInfo(
      kPeerAddr3,
      kPeerId3.remoteBgpId,
      0, // numReset
      0, // lastWentDown
      ResetReason::NOTIFICATION_RCVD);

  // Set UCMP fields on the peeringParams
  mockPeerInfoWithUcmp->peeringParams.advertiseLinkBandwidth =
      AdvertiseLinkBandwidth::BEST_PATH;
  mockPeerInfoWithUcmp->peeringParams.receiveLinkBandwidth =
      ReceiveLinkBandwidth::ACCEPT;
  mockPeerInfoWithUcmp->peeringParams.linkBandwidthBps = 10000000000.0f; // 10G

  // Get session info for peer with UCMP configured
  TBgpSession tBgpSessionWithUcmp =
      mockPeerMgr->getSessionInfo(kPeerAddr3, mockPeerInfoWithUcmp);

  // Verify UCMP fields are set when they have values
  EXPECT_EQ(
      tBgpSessionWithUcmp.advertise_link_bandwidth(),
      AdvertiseLinkBandwidth::BEST_PATH);
  EXPECT_EQ(
      tBgpSessionWithUcmp.receive_link_bandwidth(),
      ReceiveLinkBandwidth::ACCEPT);
  EXPECT_TRUE(tBgpSessionWithUcmp.link_bandwidth_bps().has_value());
  EXPECT_FLOAT_EQ(*tBgpSessionWithUcmp.link_bandwidth_bps(), 10000000000.0f);

  // Static peer without UCMP fields (nullopt)
  auto mockPeerInfoWithoutUcmp = getMockPeerInfo(
      kPeerAddr3,
      kPeerId3.remoteBgpId,
      0, // numReset
      0, // lastWentDown
      ResetReason::NOTIFICATION_RCVD);

  // Explicitly ensure UCMP fields are not set (nullopt)
  mockPeerInfoWithoutUcmp->peeringParams.advertiseLinkBandwidth = std::nullopt;
  mockPeerInfoWithoutUcmp->peeringParams.receiveLinkBandwidth = std::nullopt;
  mockPeerInfoWithoutUcmp->peeringParams.linkBandwidthBps = std::nullopt;

  // Get session info for peer without UCMP configured
  TBgpSession tBgpSessionWithoutUcmp =
      mockPeerMgr->getSessionInfo(kPeerAddr3, mockPeerInfoWithoutUcmp);

  /*
   * Verify UCMP fields use thrift default values when not configured
   * (DISABLE = 0 is the default for both enums per bgp_attr.thrift)
   */
  EXPECT_EQ(
      tBgpSessionWithoutUcmp.advertise_link_bandwidth(),
      AdvertiseLinkBandwidth::DISABLE);
  EXPECT_EQ(
      tBgpSessionWithoutUcmp.receive_link_bandwidth(),
      ReceiveLinkBandwidth::DISABLE);
  EXPECT_FALSE(tBgpSessionWithoutUcmp.link_bandwidth_bps().has_value());
}

/*
 * Test that getHoldTimerInfos correctly returns remaining hold timer
 * information for static peers and skips unattached dynamic peer entries
 */
TEST_F(PeerManagerTestFixture, GetHoldTimerInfosTest) {
  auto mockPeerMgr = setupMockPeerManagerWithSeparateThread(true, false);
  auto mockSessionMgr = setupMockSessionManager(mockPeerMgr);
  auto mockPeerMgrThread = mockPeerMgr->runInThread();

  // Static peer with negotiated hold time and recent timer reset
  auto mockPeerInfo3 = getMockPeerInfo(kPeerAddr3, kPeerId3.remoteBgpId);
  mockPeerInfo3->negotiatedHoldTime = std::chrono::seconds(30);
  // Set lastResetHoldTimer to "now" so remaining time should be ~30s
  mockPeerInfo3->lastResetHoldTimer =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();

  /*
   * Dynamic peer IDLE entry (unattached prefix-range template) — should be
   * skipped
   */
  auto mockPeerInfo1 =
      getMockPeerInfo(kPeerPrefix1, kDynamicRouterId1, false, true);

  // Static peer with hold time disabled (negotiated to 0)
  auto mockPeerInfo4 = getMockPeerInfo(kPeerAddr4, kPeerId4.remoteBgpId);
  mockPeerInfo4->negotiatedHoldTime = std::chrono::seconds(0);

  folly::EventBase evb;
  evb.scheduleAt(
      [&]() {
        std::unordered_multimap<
            folly::IPAddress,
            std::shared_ptr<BgpPeerDisplayInfo>>
            allPeers;
        allPeers.emplace(kPeerAddr3, mockPeerInfo3);
        allPeers.emplace(kPeerPrefix1.first, mockPeerInfo1);
        allPeers.emplace(kPeerAddr4, mockPeerInfo4);

        auto holdTimerInfos = mockPeerMgr->getHoldTimerInfos(allPeers);
        // Dynamic peer IDLE entry should be skipped, only 2 static peers
        EXPECT_EQ(2, holdTimerInfos.size());

        // Find each peer's info
        const THoldTimerInfo* activePeerInfo = nullptr;
        const THoldTimerInfo* disabledPeerInfo = nullptr;
        for (const auto& info : holdTimerInfos) {
          if (*info.peer_address() == kPeerAddr3.str()) {
            activePeerInfo = &info;
          } else if (*info.peer_address() == kPeerAddr4.str()) {
            disabledPeerInfo = &info;
          }
        }
        ASSERT_NE(nullptr, activePeerInfo);
        ASSERT_NE(nullptr, disabledPeerInfo);

        // Active peer: remaining time should be close to 30s (within 5s)
        EXPECT_GT(activePeerInfo->hold_time_remaining_ms(), 25000);
        EXPECT_LE(activePeerInfo->hold_time_remaining_ms(), 30000);

        // Peer with hold time disabled (negotiated to 0): returns 0
        EXPECT_EQ(0, disabledPeerInfo->hold_time_remaining_ms());

        mockPeerMgr->stop();
      },
      std::chrono::steady_clock::now() + std::chrono::seconds{1});

  evb.loop();
  mockPeerMgrThread.join();
  SUCCEED();
}

} // namespace facebook::bgp
