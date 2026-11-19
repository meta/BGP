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

#define PeerManager_TEST_FRIENDS                                              \
  FRIEND_TEST(PeerManagerLoopTestFixture, HandleNeighborEventMsgTest);        \
  FRIEND_TEST(PeerManagerLoopTestFixture, HandleNeighborReachabilityMsgTest); \
  FRIEND_TEST(                                                                \
      PeerManagerLoopTestFixture, ProcessNeighborRouteChangeLoopMixMsgTest);  \
  FRIEND_TEST(PeerManagerLoopTestFixture, ProcessAdjRibEventTest);

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/fibers/Semaphore.h>

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/config/Config.h"
#include "neteng/fboss/bgp/cpp/config/ConfigManager.h"
#include "neteng/fboss/bgp/cpp/peer/PeerManagerBase.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"

namespace facebook::bgp {

/*
 * Peer manager loop tests forcuses on each loop functions in peer manager.
 * We mock the functions to verify the correctness of each loop
 */

#define SET_CALLED_FUNCTION(functionName)                          \
  functionsCalled_.withWLock(                                      \
      [&](auto& strList) { strList.emplace_back(functionName); }); \
  sem_.signal();

#define SET_CALLED_FUNCTION_IN_CORO(functionName) \
  SET_CALLED_FUNCTION(functionName);              \
  co_return

std::string getString(const NeighborEventMsg& evt) {
  std::stringstream ss;
  ss << evt.nbrAddr.str() << "," << evt.isUp;
  return ss.str();
}

class MockLoopPeerManager : public PeerManagerBase {
 public:
  MockLoopPeerManager(
      std::shared_ptr<Config> config,
      nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ,
      MonitoredMPMCQueue<RibOutMessage>& ribOutQ,
      std::optional<MonitoredMPMCQueue<NeighborWatcherMessage>>&
          nbrRouteChangeQ)
      : PeerManagerBase(
            std::make_shared<ConfigManager>(config),
            nullptr,
            ribInQ,
            ribOutQ,
            nbrRouteChangeQ) {}

  using retTypeCoroVoid = folly::coro::Task<void>;

  retTypeCoroVoid processAdjRibEvent(
      AdjRib::ObservableMessageT&& evt) noexcept override {
    SET_CALLED_FUNCTION_IN_CORO("processAdjRibEvent: " + evt.peerId.str());
  }

  retTypeCoroVoid handleNeighborEventMsg(
      const NeighborEventMsg& evt) noexcept override {
    SET_CALLED_FUNCTION_IN_CORO("handleNeighborEventMsg: " + getString(evt));
  }

  retTypeCoroVoid handleNeighborReachabilityMsg() noexcept override {
    SET_CALLED_FUNCTION_IN_CORO("handleNeighborReachabilityMsg");
  }

  void checkCalledFunction(const std::string& expectedFunction) {
    sem_.wait();

    functionsCalled_.withWLock([&](auto& strList) {
      EXPECT_EQ(strList.front(), expectedFunction);
      strList.pop_front();
    });
  }

  void run() noexcept override {
    evb_.loopForever();
  }

  void stop() noexcept override {
    folly::coro::blockingWait(asyncScope_.cancelAndJoinAsync());
    evb_.terminateLoopSoon();
  }

  template <typename T>
  void scheduleLoop(folly::coro::Task<T>&& task) {
    asyncScope_.add(co_withExecutor(&evb_, std::move(task)));
  }

  folly::fibers::Semaphore sem_{0};
  folly::Synchronized<std::list<std::string>> functionsCalled_{};

  PeerManager_TEST_FRIENDS
};

class PeerManagerLoopTestFixture : public ::testing::Test {
 public:
  void SetUp() override {
    mockPeerMgr_ = std::make_shared<MockLoopPeerManager>(
        getConfig(),
        ribInQ_, /* write to the queue */
        ribOutQ_, /* read from the queue */
        nbrRouteChangeQ_);
  }

  void TearDown() override {
    mockPeerMgr_.reset();
  }

  std::shared_ptr<Config> getConfig() {
    thrift::BgpConfig thriftConfig;
    thriftConfig.router_id() = kLocalAddr1.str();
    thriftConfig.local_as() = kAsn1;
    thriftConfig.hold_time() = kHoldTime.count();
    thriftConfig.graceful_restart_convergence_seconds() =
        kGrRestartTime.count();
    thriftConfig.listen_addr() = kLocalAddr1.str();
    thriftConfig.eor_time_s() = 120;
    /*
     * Stress-test will run multiple instance of tests simultaneously.
     * Binding to a static port is bound to fail in that situation.
     * Picking a pseudo random port > 1024 based on getpid().
     * There is a remote possibility this might also result in a collision but
     * that probability is very very low.
     */
    std::srand((uint16_t)getpid());
    thriftConfig.listen_port() = 1179 + (folly::Random::rand32() % 60000);

    return std::make_shared<Config>(thriftConfig);
  }

  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ_{
      nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<RibOutMessage> ribOutQ_;
  std::optional<MonitoredMPMCQueue<NeighborWatcherMessage>> nbrRouteChangeQ_ =
      std::make_optional<MonitoredMPMCQueue<NeighborWatcherMessage>>();

  std::shared_ptr<MockLoopPeerManager> mockPeerMgr_;
};

/*
 * Schedule processNeighborRouteChangeLoop()
 * and test NeighborEventMsg is processed by handleNeighborEventMsg
 */
TEST_F(PeerManagerLoopTestFixture, HandleNeighborEventMsgTest) {
  mockPeerMgr_->scheduleLoop(mockPeerMgr_->processNeighborRouteChangeLoop());

  NeighborEventMsg msg{fboss::kDefaultVrf, kLocalAddr1, false};

  nbrRouteChangeQ_->push(msg);

  auto thread = mockPeerMgr_->runInThread();

  mockPeerMgr_->checkCalledFunction(
      "handleNeighborEventMsg: " + getString(msg));

  mockPeerMgr_->stop();
  thread.join();
}

/*
 * Schedule processNeighborRouteChangeLoop()
 * and test NeighborReachabilityMsg triggers handleNeighborReachabilityMsg
 */
TEST_F(PeerManagerLoopTestFixture, HandleNeighborReachabilityMsgTest) {
  mockPeerMgr_->scheduleLoop(mockPeerMgr_->processNeighborRouteChangeLoop());

  nbrRouteChangeQ_->push(NeighborReachabilityMsg{});

  auto thread = mockPeerMgr_->runInThread();

  mockPeerMgr_->checkCalledFunction("handleNeighborReachabilityMsg");

  mockPeerMgr_->stop();
  thread.join();
}

/*
 * Schedule processNeighborRouteChangeLoop()
 * test when both NeighborEventMsg and NeighborReachabilityMsg are in the queue
 */
TEST_F(PeerManagerLoopTestFixture, ProcessNeighborRouteChangeLoopMixMsgTest) {
  mockPeerMgr_->scheduleLoop(mockPeerMgr_->processNeighborRouteChangeLoop());

  NeighborEventMsg msg{fboss::kDefaultVrf, kLocalAddr1, false};

  nbrRouteChangeQ_->push(msg);
  nbrRouteChangeQ_->push(NeighborReachabilityMsg{});

  auto thread = mockPeerMgr_->runInThread();

  mockPeerMgr_->checkCalledFunction(
      "handleNeighborEventMsg: " + getString(msg));
  mockPeerMgr_->checkCalledFunction("handleNeighborReachabilityMsg");

  mockPeerMgr_->stop();
  thread.join();
}

/*
 * Schedule processAdjRibMsgLoop()
 * and test ObservableMessageT is processed by processAdjRibEvent
 */
TEST_F(PeerManagerLoopTestFixture, ProcessAdjRibEventTest) {
  mockPeerMgr_->scheduleLoop(mockPeerMgr_->processAdjRibMsgLoop());
  AdjRib::ObservableMessageT evt{fboss::kDefaultVrf, kPeerId1, AdjRib::EoR{}};

  mockPeerMgr_->fromAdjRibQ_.push(std::move(evt));

  auto thread = mockPeerMgr_->runInThread();

  mockPeerMgr_->checkCalledFunction("processAdjRibEvent: " + kPeerId1.str());

  mockPeerMgr_->stop();
  thread.join();
}

} // namespace facebook::bgp
