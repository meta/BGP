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

#include <folly/coro/BlockingWait.h>

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberBgpPeerManager.h"
#include "neteng/fboss/bgp/cpp/lib/tests/FiberBgpPeerManagerTestUtils.h"

namespace facebook::nettools::bgplib {
using namespace facebook::bgp;
using facebook::bgp::thrift::BgpNetwork;

class MockFiberBgpPeerManager : public FiberBgpPeerManager {
 public:
  using FiberBgpPeerManager::FiberBgpPeerManager;

  using retTypeGetAllPeerDisplayInfos = std::
      unordered_multimap<folly::IPAddress, std::shared_ptr<BgpPeerDisplayInfo>>;

  MOCK_METHOD(retTypeGetAllPeerDisplayInfos, getAllPeerDisplayInfos, ());

  using retTypeGetPeerDisplayInfo =
      std::optional<std::vector<BgpPeerDisplayInfo>>;

  MOCK_METHOD(
      retTypeGetPeerDisplayInfo,
      getPeerDisplayInfo,
      (const folly::IPAddress&));

  MOCK_METHOD(
      retTypeGetPeerDisplayInfo,
      getPeerDisplayInfo,
      (const BgpPeerId&));

  using retTypeGetAllEstablishedPeerDisplayInfo =
      std::vector<BgpPeerDisplayInfo>;

  MOCK_METHOD(
      retTypeGetAllEstablishedPeerDisplayInfo,
      getAllEstablishedPeerDisplayInfo,
      ());

  using retTypePeerOperation = folly::Expected<folly::Unit, ErrorCode>;

  MOCK_METHOD(retTypePeerOperation, stopPeer, (const folly::IPAddress&, bool));

  MOCK_METHOD(
      retTypePeerOperation,
      shutdownPeer,
      (const folly::IPAddress&, bool));

  MOCK_METHOD(
      retTypePeerOperation,
      addPeer,
      (const folly::IPAddress&,
       const bgp::PeeringParams&,
       const ConnTimeParams&));
};

class CoroTestFixture : public ::testing::Test {
 public:
  CoroTestFixture() : fm_(folly::fibers::getFiberManager(evb_, {})) {}

  void SetUp() override {
    BgpGlobalConfig bgpGlobalConfig(
        100, // localAsn
        kR1Lo1, // routerId 127.1.0.1
        kR1Lo1, // clusterId 127.1.0.1
        kDefaultHoldTime, // holdTime
        std::nullopt, // listenAddr
        kDefaultGrRestartTime, // grRestartTime
        std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), // networksV4
        std::unordered_map<folly::CIDRNetwork, BgpNetwork>() // networksV6
    );
    mockSessionMgr_ = std::make_unique<MockFiberBgpPeerManager>(
        bgpGlobalConfig, fm_, evb_, true, true);

    sessionMgrThread_ = std::thread([&]() { evb_.loopForever(); });

    evb_.waitUntilRunning();
  }

  void TearDown() override {
    evb_.runInEventBaseThreadAndWait([&]() { evb_.terminateLoopSoon(); });
    mockSessionMgr_.reset();
    sessionMgrThread_.join();
  }

  std::unique_ptr<MockFiberBgpPeerManager> mockSessionMgr_;
  folly::EventBase evb_;
  folly::fibers::FiberManager& fm_;

  std::thread sessionMgrThread_;
};

TEST_F(CoroTestFixture, CoGetAllPeerDisplayInfosTest) {
  EXPECT_CALL(*mockSessionMgr_, getAllPeerDisplayInfos()).Times(1);

  folly::coro::blockingWait(mockSessionMgr_->co_getAllPeerDisplayInfos());
}

TEST_F(CoroTestFixture, CoGetPeerDisplayInfoPeerAddrTest) {
  EXPECT_CALL(*mockSessionMgr_, getPeerDisplayInfo(kR1Lo1)).Times(1);

  folly::coro::blockingWait(mockSessionMgr_->co_getPeerDisplayInfo(kR1Lo1));
}

TEST_F(CoroTestFixture, CoGetPeerDisplayInfoPeerIdTest) {
  const folly::IPAddress peerAddr{kR2Lo1}; // 127.2.0.1
  const BgpPeerId peerId{peerAddr, peerAddr.asV4().toLongHBO()};

  EXPECT_CALL(*mockSessionMgr_, getPeerDisplayInfo(peerId)).Times(1);

  folly::coro::blockingWait(mockSessionMgr_->co_getPeerDisplayInfo(peerId));
}

TEST_F(CoroTestFixture, CoGetAllEstablishedPeerDisplayInfoTest) {
  EXPECT_CALL(*mockSessionMgr_, getAllEstablishedPeerDisplayInfo()).Times(1);

  folly::coro::blockingWait(
      mockSessionMgr_->co_getAllEstablishedPeerDisplayInfo());
}

TEST_F(CoroTestFixture, CoStopPeerTest) {
  const folly::IPAddress peerAddr{kR2Lo1}; // 127.2.0.1

  EXPECT_CALL(*mockSessionMgr_, stopPeer(peerAddr, true)).Times(1);

  folly::coro::blockingWait(mockSessionMgr_->co_stopPeer(peerAddr, true));
}

TEST_F(CoroTestFixture, CoShutdownPeerTest) {
  const folly::IPAddress peerAddr{kR2Lo1}; // 127.2.0.1

  EXPECT_CALL(*mockSessionMgr_, shutdownPeer(peerAddr, ::testing::_)).Times(1);

  folly::coro::blockingWait(mockSessionMgr_->co_shutdownPeer(peerAddr));
}

TEST_F(CoroTestFixture, CoAddPeerTest) {
  const folly::IPAddress peerAddr{kR2Lo1}; // 127.2.0.1
  bgp::PeeringParams params{fboss::kDefaultVrf};
  params.peerAddr = peerAddr;
  params.remoteAs = 200;
  params.localAs = 100;
  ConnTimeParams connTimeParams(
      std::chrono::milliseconds{0}, std::chrono::milliseconds{500});

  EXPECT_CALL(*mockSessionMgr_, addPeer(peerAddr, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(
          ::testing::Return(
              folly::Expected<folly::Unit, FiberBgpPeerManager::ErrorCode>(
                  folly::unit)));

  auto result = folly::coro::blockingWait(
      mockSessionMgr_->co_addPeer(peerAddr, params, connTimeParams));
  EXPECT_TRUE(result.hasValue());
}

TEST_F(CoroTestFixture, CoAddPeerDuplicateTest) {
  const folly::IPAddress peerAddr{kR2Lo1}; // 127.2.0.1
  bgp::PeeringParams params{fboss::kDefaultVrf};
  params.peerAddr = peerAddr;
  params.remoteAs = 200;
  params.localAs = 100;
  ConnTimeParams connTimeParams(
      std::chrono::milliseconds{0}, std::chrono::milliseconds{500});

  EXPECT_CALL(*mockSessionMgr_, addPeer(peerAddr, ::testing::_, ::testing::_))
      .Times(1)
      .WillOnce(
          ::testing::Return(
              folly::makeUnexpected(
                  FiberBgpPeerManager::ErrorCode::PEER_EXISTS_ALREADY)));

  auto result = folly::coro::blockingWait(
      mockSessionMgr_->co_addPeer(peerAddr, params, connTimeParams));
  EXPECT_TRUE(result.hasError());
  EXPECT_EQ(
      result.error(), FiberBgpPeerManager::ErrorCode::PEER_EXISTS_ALREADY);
}

} // namespace facebook::nettools::bgplib
