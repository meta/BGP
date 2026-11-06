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

// Forward declare MockFiberBgpPeer before the header inclusion
namespace facebook {
namespace nettools {
namespace bgplib {
class MockFiberBgpPeer;
} // namespace bgplib
} // namespace nettools
} // namespace facebook

#define MonitoredModule_TEST_FRIENDS \
  FRIEND_TEST(FiberBgpPeerFixture, MonitoredQueueTest);

#define FiberBgpPeer_TEST_FRIENDS                                             \
  friend class FiberBgpPeerFixture;                                           \
  friend class facebook::nettools::bgplib::MockFiberBgpPeer;                  \
  FRIEND_TEST(FiberBgpPeerFixture, SendNotificationWhenHoldTimerExpiredTest); \
  FRIEND_TEST(FiberBgpPeerFixture, SocketCloseTimerTest);                     \
  FRIEND_TEST(FiberBgpPeerFixture, MonitoredQueueTest);                       \
  FRIEND_TEST(FiberBgpPeerFixture, ProcessIngressLoopTerminationTest);        \
  FRIEND_TEST(FiberBgpPeerFixture, ProcessEgressLoopTerminationTest);         \
  FRIEND_TEST(                                                                \
      FiberBgpPeerFixture, ProcessEgressLoopWithBackpressureTerminationTest); \
  FRIEND_TEST(FiberBgpPeerFixture, SocketSendLoopYieldTest);                  \
  FRIEND_TEST(FiberBgpPeerFixture, RcvdQueueBackpressureTest);                \
  FRIEND_TEST(FiberBgpPeerFixture, SerializeGroupPduFlagTest);                \
  FRIEND_TEST(FiberBgpPeerFixture, RcvdQueueConsumerScopeTest);               \
  FRIEND_TEST(FiberBgpPeerFixture, RcvdQueueConsumerScopeExceptionPathTest);  \
  FRIEND_TEST(FiberBgpPeerFixture, StopPeerDeleteFlagPropagatesToIdleEvent);  \
  friend class EgressBackpressureFixture;                                     \
  FRIEND_TEST(EgressBackpressureFixture, FillAndDrainAllBoundedQueuesTest);   \
  FRIEND_TEST(EgressBackpressureFixture, SendSocketLoopFromBoundedSendQueue); \
  FRIEND_TEST(                                                                \
      EgressBackpressureFixture, SendInitialKeepAliveWithBlockedQueueTest);   \
  FRIEND_TEST(                                                                \
      EgressBackpressureFixture, SkipScheduledKeepAliveWithBlockedQueueTest); \
  FRIEND_TEST(EgressBackpressureFixture, QueueBgpNotificationMsgTest);        \
  FRIEND_TEST(EgressBackpressureFixture, QueueCloseTest);

#include <fb303/ThreadCachedServiceData.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/GmockHelpers.h>
#include <folly/fibers/FiberManagerMap.h>
#include <folly/io/async/test/SocketPair.h>

#include "common/network/AddressUtil.h"
#include "neteng/fboss/bgp/cpp/lib/BgpException.h"
#include "neteng/fboss/bgp/cpp/lib/BgpUtil.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/BgpSerializer.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberBgpPeer.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberBgpPeerManager.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/Utils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/BoundedWaitUtils.h"

DEFINE_bool(
    enable_egress_queue_backpressure,
    false,
    "Flag to parameterize tests with egress backpressure enabled/disabled in BUCK targets.");

using namespace std::chrono_literals;

using facebook::bgp::AfiIpv4Configured;
using facebook::bgp::AfiIpv6Configured;
using facebook::bgp::PeeringParams;
using facebook::neteng::fboss::bgp_attr::AdvertiseLinkBandwidth;
using facebook::neteng::fboss::bgp_attr::ReceiveLinkBandwidth;
using facebook::network::toBinaryAddress;
using facebook::network::toIPPrefix;
using folly::IPAddress;
using folly::IPAddressV4;

namespace facebook {
namespace nettools {
namespace bgplib {
class MockFiberBgpPeer : public FiberBgpPeer {
 public:
  using FiberBgpPeer::FiberBgpPeer;
  explicit MockFiberBgpPeer(
      const bgp::PeeringParams& peeringParams,
      folly::fibers::FiberManager& fm,
      folly::EventBase& evb,
      FiberSocket&& sock,
      std::shared_ptr<InputQueueT> iqueue,
      std::shared_ptr<BoundedInputQueueT> boundedIqueue,
      std::shared_ptr<OutputQueueT> oqueue,
      const bool isRestarting = false,
      const bool enableEgressQueueBackpressure =
          FLAGS_enable_egress_queue_backpressure,
      const bool enableSerializeGroupPdu = false)
      : FiberBgpPeer(
            peeringParams,
            fm,
            evb,
            std::move(sock),
            iqueue,
            boundedIqueue,
            oqueue,
            isRestarting,
            enableEgressQueueBackpressure,
            enableSerializeGroupPdu) {}

  bool getSerializeGroupPdu() const noexcept {
    return enableSerializeGroupPdu_;
  }

  BgpSessionState getBgpSessionState() const noexcept {
    return peeringState_.state;
  }
  std::chrono::seconds getHoldTime() const noexcept {
    return peeringState_.holdTime;
  }
  std::chrono::seconds getKeepAliveTime() const noexcept {
    return peeringState_.keepAliveTime;
  }
  std::chrono::seconds getRemoteHoldTime() const noexcept {
    return peeringState_.remoteHoldTime;
  }
  BgpCapabilities getRemoteCapabilities() const noexcept {
    return peeringState_.remoteCapabilities;
  }

  // Stands in for an ESTABLISHED session with the RFC 8538 N bit negotiated
  void setGrNotification(bool grNotification) noexcept {
    grNotification_ = grNotification;
  }

  bool getValidateRemoteAs() {
    return peeringParams_.validateRemoteAs;
  }

  // Explicitly override socketAddress retrieval for testing purpose
  folly::SocketAddress getLocalSocketAddress() const noexcept override {
    return localAddress_;
  }

 private:
  folly::SocketAddress localAddress_{
      "::1",
      nettools::bgplib::constants::kBgpPort};
};

class FiberBgpPeerFixture : public ::testing::Test {
 public:
  FiberBgpPeerFixture()
      : fmWrapper_(
            folly::fibers::getFiberManager(evb_, getFiberManagerOptions(256))) {
  }

  std::optional<FiberBgpParser::BgpMessageT> processOpenAndGetResponse(
      const std::shared_ptr<MockFiberBgpPeer>& peer,
      uint32_t remoteAs,
      bool as4Byte) {
    BgpOpenMsg openMsg;
    openMsg.version() = kBgpVersion;
    openMsg.asn() = static_cast<int32_t>(
        as4Byte && remoteAs > k2BytesAsnLimit ? kAsTrans : remoteAs);
    openMsg.holdTime() = static_cast<int32_t>(params2_.holdTime.count());
    openMsg.bgpID() = static_cast<uint32_t>(params2_.localBgpId.toLongHBO());
    openMsg.capabilities() = peer->caps_;
    openMsg.capabilities()->as4byte() = as4Byte;
    openMsg.capabilities()->asn() =
        as4Byte ? static_cast<int64_t>(remoteAs) : 0;

    peer->peeringState_.state = BgpSessionState::OPEN_SENT;
    peer->processOpenMsg(openMsg);

    if (peer->sendQueue_.size() == 0) {
      return std::nullopt;
    }
    return peer->sendQueue_.get();
  }

  void processKeepAlive(const std::shared_ptr<MockFiberBgpPeer>& peer) {
    peer->processKeepAliveMsg();
  }

  void setNegotiatedGrNotification(
      const std::shared_ptr<MockFiberBgpPeer>& peer) {
    peer->peeringState_.negotiatedCapabilities.grNotification() = true;
  }

  bool processNotificationError(
      const std::shared_ptr<MockFiberBgpPeer>& peer,
      const BgpNotification& notif) {
    return peer->processBgpNotificationError(
        FiberBgpPeer::BgpNotificationError{notif});
  }

  folly::EventBase evb_;

  std::unique_ptr<FiberSocket> fs0_, fs1_;

  // queue definition for peerInput and peerOutput queues
  std::shared_ptr<FiberBgpPeer::InputQueueT> peer1Input_ =
      std::make_shared<FiberBgpPeer::InputQueueT>();
  std::shared_ptr<FiberBgpPeer::InputQueueT> peer2Input_ =
      std::make_shared<FiberBgpPeer::InputQueueT>();

  std::shared_ptr<FiberBgpPeer::BoundedInputQueueT> peer1BoundedInput_ =
      std::make_shared<FiberBgpPeer::BoundedInputQueueT>(
          kMaxEgressQueueSize,
          kEgressQueueHighWatermark,
          kEgressQueueLowWatermark);
  std::shared_ptr<FiberBgpPeer::BoundedInputQueueT> peer2BoundedInput_ =
      std::make_shared<FiberBgpPeer::BoundedInputQueueT>(
          kMaxEgressQueueSize,
          kEgressQueueHighWatermark,
          kEgressQueueLowWatermark);

  std::shared_ptr<FiberBgpPeer::OutputQueueT> peer1Output_ =
      std::make_shared<FiberBgpPeer::OutputQueueT>(kMaxIngressQueueSize);
  std::shared_ptr<FiberBgpPeer::OutputQueueT> peer2Output_ =
      std::make_shared<FiberBgpPeer::OutputQueueT>(kMaxIngressQueueSize);

  std::reference_wrapper<folly::fibers::FiberManager> fmWrapper_;

  std::shared_ptr<BgpUpdate2> update1_;
  std::shared_ptr<BgpUpdate2> update2_;
  BgpNotification notifyMsg1_;
  BgpRouteRefresh routeRefreshMsg1_;

  PeeringParams params1_{
      folly::IPAddress("1.1.1.1"), // peerAddr
      std::nullopt, // peerPrefix
      4200000001, // globalAs
      4200000001, // localAs
      4200000002, // remoteAs
      IPAddressV4("2.2.2.2"), // routerId
      std::chrono::seconds(180), // holdTime
      std::chrono::seconds(120) // grRestartTime
  };
  PeeringParams params2_{
      folly::IPAddress("2.2.2.2"), // peerAddr
      std::nullopt, // peerPrefix
      4200000002, // globalAs
      4200000002, // localAs
      4200000001, // remoteAs
      IPAddressV4("1.1.1.1"), // routerId
      std::chrono::seconds(180), // holdTime
      std::chrono::seconds(120) // grRestartTime
  };
  PeeringParams params3_{
      folly::IPAddress("1.1.1.1"), // peerAddr
      std::nullopt, // peerPrefix
      1234, // globalAs
      1234, // localAs
      1234, // remoteAs
      IPAddressV4("2.2.2.2"), // routerId
      std::chrono::seconds(3), // holdTime
      std::chrono::seconds(10) // grRestartTime
  };
  PeeringParams params4_{
      folly::IPAddress("2.2.2.2"), // peerAddr
      std::nullopt, // peerPrefix
      1234, // globalAs
      1234, // localAs
      1234, // remoteAs
      IPAddressV4("1.1.1.1"), // routerId
      std::chrono::seconds(3), // holdTime
      std::chrono::seconds(10) // grRestartTime
  };

  // instantiate and run two peers: peer1 and peer2
  std::pair<
      std::shared_ptr<MockFiberBgpPeer> /* peer1 */,
      std::shared_ptr<MockFiberBgpPeer> /* peer2 */>
  startTwoPeers(const PeeringParams& params1, const PeeringParams& params2) {
    auto& fm = fmWrapper_.get();
    folly::SocketPair sp;
    auto fs0 = std::make_unique<FiberSocket>(
        folly::AsyncSocket::newSocket(
            &evb_, folly::NetworkSocket::fromFd(sp.extractFD0())),
        &evb_);
    auto fs1 = std::make_unique<FiberSocket>(
        folly::AsyncSocket::newSocket(
            &evb_, folly::NetworkSocket::fromFd(sp.extractFD1())),
        &evb_);

    auto peer1 = std::make_shared<MockFiberBgpPeer>(
        params1,
        fm,
        evb_,
        std::move(*fs0),
        peer1Input_,
        peer1BoundedInput_,
        peer1Output_);
    auto peer2 = std::make_shared<MockFiberBgpPeer>(
        params2,
        fm,
        evb_,
        std::move(*fs1),
        peer2Input_,
        peer2BoundedInput_,
        peer2Output_);

    fm.addTask([&fm, peer1, peer2] {
      // start all fibers
      fm.addTask([peer1] { peer1->run(); });
      fm.addTask([peer2] { peer2->run(); });
    });

    return std::make_pair(peer1, peer2);
  }

  void stopTwoPeers(
      const std::shared_ptr<MockFiberBgpPeer>& peer1,
      const std::shared_ptr<MockFiberBgpPeer>& peer2) {
    peer1->stop();
    peer2->stop();
  }

  /*
   * Establish two peers with the RFC 8538 N bit, have peer1 send notif and
   * return whether peer2 stops with graceful restart
   */
  bool peer2GrOnNotification(const BgpNotification& notif) {
    bool gracefulRestart{true};
    auto& fm = fmWrapper_.get();
    fm.addTask([&] {
      auto [peer1, peer2] = startTwoPeers(params3_, params4_);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);
      peer1->setGrNotification(true);
      peer2->setGrNotification(true);

      auto copyNotif = notif;
      if (FLAGS_enable_egress_queue_backpressure) {
        EXPECT_TRUE(peer1BoundedInput_->push(std::move(copyNotif)));
      } else {
        peer1Input_->push(std::move(copyNotif));
      }
      auto msg = facebook::bgp::test::boundedBlockingPop(
          *peer2Output_, "peer2Output_");
      EXPECT_TRUE(std::holds_alternative<FiberBgpPeer::BgpSessionStop>(msg));
      gracefulRestart =
          std::get<FiberBgpPeer::BgpSessionStop>(msg).gracefulRestart;

      stopTwoPeers(peer1, peer2);
      EXPECT_EQ(ResetReason::NOTIFICATION_RCVD, peer2->getResetReason());
    });
    evb_.loop();
    return gracefulRestart;
  }

  int runUntilTargetStateOrTimeout(
      const std::shared_ptr<MockFiberBgpPeer>& peer,
      const BgpSessionState targetState,
      const std::chrono::milliseconds duration) {
    /*
     * this function blocks the underlying fiber until the peer reaches the
     * target state or the function would timeout after duration
     */

    auto& fm = fmWrapper_.get();
    folly::fibers::Baton bt;
    std::atomic<int> ret = 0;

    fm.addTask([&fm, &peer, &targetState, &duration, &bt, &ret] {
      const auto timeout = std::chrono::steady_clock::now() + duration;
      while (std::chrono::steady_clock::now() < timeout) {
        if (peer->getBgpSessionState() == targetState) {
          // reach the target state
          bt.post();
          return;
        }
        /*
         * check every ms. Don't just yield here or we might get stuck as some
         * other fibers might depend on time
         */
        folly::fibers::yield();
        fiberSleepFor(1ms);
      }
      // timeout
      ret = -1;
      bt.post();
    });

    // block the current fiber until the target state is reached or timeout
    bt.wait();
    return ret;
  }

  void SetUp() override {
    folly::SocketPair sp;
    auto fd0 = sp.extractFD0();
    auto fd1 = sp.extractFD1();
    auto as0 = folly::to_shared_ptr(
        folly::AsyncSocket::newSocket(
            &evb_, folly::NetworkSocket::fromFd(fd0)));
    auto as1 = folly::to_shared_ptr(
        folly::AsyncSocket::newSocket(
            &evb_, folly::NetworkSocket::fromFd(fd1)));

    fs0_ = std::make_unique<FiberSocket>(as0, &evb_);
    fs1_ = std::make_unique<FiberSocket>(as1, &evb_);

    update1_ = std::make_shared<BgpUpdate2>();
    // sent from peer1 (2.2.2.2)
    auto prefix = toIPPrefix(folly::IPAddress::createNetwork("2.2.2.0/24"));
    RiggedIPPrefix rigPrefix;
    *rigPrefix.prefix() = prefix;
    update1_->v4Announced2()->push_back(rigPrefix);

    *update1_->attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
    BgpAttrAsPathSegment segment1;
    segment1.asSequence()->push_back(2222);
    update1_->attrs()->asPath()->push_back(segment1);
    *update1_->attrs()->nexthop() = "2.2.2.2";
    update1_->attrs()->med() = 32;
    update1_->attrs()->isMedSet() = true;
    update1_->attrs()->localPref() = 100;
    BgpAttrCommunity community1;
    *community1.asn() = 65530;
    *community1.value() = 15800;
    update1_->attrs()->communities()->push_back(community1);
    *update1_->v4Nexthop() = toBinaryAddress(folly::IPAddress("2.2.2.2"));

    update2_ = std::make_shared<BgpUpdate2>();
    // sent from peer2 (1.1.1.1)
    auto prefix2 = toIPPrefix(folly::IPAddress::createNetwork("1.1.1.0/24"));
    RiggedIPPrefix rigPrefix2;
    *rigPrefix2.prefix() = prefix2;
    update2_->v4Announced2()->push_back(rigPrefix2);
    *update2_->attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
    BgpAttrAsPathSegment segment2;
    segment2.asSequence()->push_back(1111);
    update2_->attrs()->asPath()->push_back(segment2);
    *update2_->attrs()->nexthop() = "1.1.1.1";
    *update2_->attrs()->med() = 32;
    update2_->attrs()->isMedSet() = true;
    update2_->attrs()->localPref() = 100;
    BgpAttrCommunity community2;
    *community2.asn() = 65530;
    *community2.value() = 15800;
    update2_->attrs()->communities()->push_back(community2);
    *update2_->v4Nexthop() = toBinaryAddress(folly::IPAddress("1.1.1.1"));

    // Administrative shutdown notification
    *notifyMsg1_.errCode() = BgpNotifErrCode::BN_CEASE;
    *notifyMsg1_.errSubCode() =
        static_cast<uint16_t>(BgpNotifCeaseErrSubCode::BN_CEASE_ADMIN_SHUTDOWN);

    // Route Refresh request message
    routeRefreshMsg1_.afi() = BgpUpdateAfi::AFI_IPv6;
    routeRefreshMsg1_.msgSubType() =
        BgpRouteRefreshMessageSubtype::ROUTE_REFRESH_REQUEST;
    routeRefreshMsg1_.safi() = BgpUpdateSafi::SAFI_UNICAST;
  }

  /*
   * Establish two peers, and then execute the test logic from lambda
   * Optionally send updates from either direction before test logic
   * is executed.
   */
  void establishTwoPeersAndTest(
      std::optional<std::function<void()>> testLambda,
      const std::optional<std::shared_ptr<BgpUpdate2>>& updateFromPeer1,
      const std::optional<std::shared_ptr<BgpUpdate2>>& updateFromPeer2,
      std::shared_ptr<FiberBgpPeer> peer1 = nullptr,
      std::shared_ptr<FiberBgpPeer> peer2 = nullptr) {
    auto& fm = fmWrapper_.get();
    if (!peer1) {
      peer1 = std::make_shared<FiberBgpPeer>(
          params3_,
          fm,
          evb_,
          std::move(*fs0_),
          peer1Input_,
          peer1BoundedInput_,
          peer1Output_,
          false /* isRestarting */,
          FLAGS_enable_egress_queue_backpressure);
    }
    if (!peer2) {
      peer2 = std::make_shared<FiberBgpPeer>(
          params4_,
          fm,
          evb_,
          std::move(*fs1_),
          peer2Input_,
          peer2BoundedInput_,
          peer2Output_,
          false /* isRestarting */,
          FLAGS_enable_egress_queue_backpressure);
    }

    fm.addTask([this,
                &fm,
                peer1,
                peer2,
                testLambda,
                updateFromPeer1,
                updateFromPeer2] {
      // start all fibers
      fm.addTask([peer1] { peer1->run(); });
      fm.addTask([peer2] { peer2->run(); });

      // Observe state changes for both peers
      RWQueue<FiberBgpPeer::ObservableStateT> combined;
      fm.addTask([&combined, peer1, peer2]() mutable {
        auto writer = combined.getWriter();
        mergeQueuesStatic(
            writer,
            peer1->getObserverStateQueue(),
            peer2->getObserverStateQueue());
      });

      // handler of observer received Bgp messages
      struct ObservedMessageProcessor {
        const std::shared_ptr<const BgpUpdate2> givenUpdate;
        explicit ObservedMessageProcessor(
            std::shared_ptr<const BgpUpdate2> const& update)
            : givenUpdate{update} {}

        void operator()(std::shared_ptr<const BgpUpdate2> const& obsvdUpdate) {
          EXPECT_EQ(*givenUpdate, *obsvdUpdate);
        }

        void operator()(BgpEndOfRib const&) {}

        void operator()(FiberBgpPeer::BgpSessionStop const&) {}

        void operator()(BgpRouteRefresh const&) {}
      };

      /*
       * Hold timer would be set by now but not keep alive timer
       */
      auto lastResetHoldTimerPeer1 = peer1->getLastResetHoldTimer();
      auto lastResetHoldTimerPeer2 = peer2->getLastResetHoldTimer();
      auto lastResetKeepAliveTimerPeer1 = peer1->getLastResetKeepAliveTimer();
      auto lastResetKeepAliveTimerPeer2 = peer2->getLastResetKeepAliveTimer();

      std::set<folly::IPAddress> establishedPeers;
      while (true) {
        auto msg = combined.get();
        if (!msg) {
          XLOG(INFO, "Both peers terminated");
          break;
        }
        XLOGF(
            INFO,
            "Peer {} state is now {}",
            msg->peerId.peerAddr.str(),
            static_cast<int>(msg->state));
        if (msg->state == BgpSessionState::IDLE) {
          continue;
        }
        if (msg->state == BgpSessionState::ESTABLISHED) {
          establishedPeers.insert(msg->peerId.peerAddr);
        }
        if (establishedPeers.size() == 2) {
          XLOG(INFO, "Both peers established, exchanging updates");
          if (updateFromPeer1) {
            EXPECT_NE(lastResetHoldTimerPeer2, peer2->getLastResetHoldTimer());
            EXPECT_NE(
                lastResetKeepAliveTimerPeer1,
                peer1->getLastResetKeepAliveTimer());
            auto copy_update1 = *updateFromPeer1;
            if (FLAGS_enable_egress_queue_backpressure) {
              ASSERT_TRUE(peer1BoundedInput_->push(std::move(copy_update1)));
            } else {
              peer1Input_->push(std::move(copy_update1));
            }
            auto rcvd_update1 = facebook::bgp::test::boundedBlockingPop(
                *peer2Output_, "peer2Output_");

            /*
             * We will populate both v4Announced and v4Announced2 for backward
             * compatibility
             */
            if (updateFromPeer1.value()->v4Announced()->empty()) {
              for (const auto& rigPfix :
                   *updateFromPeer1.value()->v4Announced2()) {
                updateFromPeer1.value()->v4Announced()->push_back(
                    *rigPfix.prefix());
              }
            }
            ObservedMessageProcessor visitor1(*updateFromPeer1);
            std::visit(visitor1, rcvd_update1);
            auto obsvd_msg1 = peer2->getObserverRcvdMessageQueue().get();
            std::visit(visitor1, obsvd_msg1->message);
          }

          if (updateFromPeer2) {
            EXPECT_NE(lastResetHoldTimerPeer1, peer1->getLastResetHoldTimer());
            EXPECT_NE(
                lastResetKeepAliveTimerPeer2,
                peer2->getLastResetKeepAliveTimer());
            auto copy_update2 = *updateFromPeer2;
            if (FLAGS_enable_egress_queue_backpressure) {
              ASSERT_TRUE(peer2BoundedInput_->push(std::move(copy_update2)));
            } else {
              peer2Input_->push(std::move(copy_update2));
            }
            ObservedMessageProcessor visitor2(*updateFromPeer2);
            auto rcvd_update2 = facebook::bgp::test::boundedBlockingPop(
                *peer1Output_, "peer1Output_");
            /*
             * We will populate both v4Announced and v4Announced2 for backward
             * compatibility
             */
            if (updateFromPeer2.value()->v4Announced()->empty()) {
              for (const auto& rigPfix :
                   *updateFromPeer2.value()->v4Announced2()) {
                updateFromPeer2.value()->v4Announced()->push_back(
                    *rigPfix.prefix());
              }
            }
            std::visit(visitor2, rcvd_update2);
            auto obsvd_msg2 = peer1->getObserverRcvdMessageQueue().get();
            std::visit(visitor2, obsvd_msg2->message);
          }

          XLOG(INFO, "Both peers received updates, Now sending notification");

          if (testLambda) {
            (*testLambda)();
          }
          peer1->stop();
          peer2->stop();
        }
      }
    });

    evb_.loop();
    SUCCEED();
  }

  void enhancedRouteRefreshErrorTest(
      const std::optional<std::shared_ptr<BgpUpdate2>>& updateFromPeer1,
      const std::optional<std::shared_ptr<BgpUpdate2>>& updateFromPeer2) {
    auto lambdaVerifyEnhancedRouteRefreshError = [&]() {
      /*
       * Send an invalid route refresh request without SAFI from peer1
       * Verify if peer1 received a BGP notification with the right error code
       */
      BgpRouteRefresh routeRefreshMsgInvalid;
      routeRefreshMsgInvalid.afi() = BgpUpdateAfi::AFI_IPv6;
      routeRefreshMsgInvalid.msgSubType() =
          BgpRouteRefreshMessageSubtype::BEGINNING_OF_ROUTE_REFRESH;
      if (FLAGS_enable_egress_queue_backpressure) {
        ASSERT_TRUE(peer1BoundedInput_->push(routeRefreshMsgInvalid));
      } else {
        peer1Input_->push(routeRefreshMsgInvalid);
      }
      // Invalid route refresh message should not lead to session stop
      auto msg = facebook::bgp::test::boundedBlockingPop(
          *peer2Output_, "peer2Output_");
      ASSERT_FALSE(std::holds_alternative<FiberBgpPeer::BgpSessionStop>(msg));
    };
    // Enable Enhanced Route Refresh on peers
    params3_.isEnhancedRouteRefreshConfigured = true;
    params4_.isEnhancedRouteRefreshConfigured = true;
    establishTwoPeersAndTest(
        std::optional<std::function<void()>>(
            lambdaVerifyEnhancedRouteRefreshError),
        updateFromPeer1,
        updateFromPeer2);
  }

  void runLocalAsSessionTest(
      uint16_t peer1LocalAs,
      uint16_t peer1GlobalAs,
      uint16_t peer1RemoteAs,
      uint16_t peer2LocalAs,
      uint16_t peer2GlobalAs,
      uint16_t peer2RemoteAs,
      std::set<folly::IPAddress>& establishedPeers) {
    auto params1 = params1_;
    auto params2 = params2_;
    params1.localAs = peer1LocalAs;
    params1.globalAs = peer1GlobalAs;
    params1.remoteAs = peer1RemoteAs;

    params2.localAs = peer2LocalAs;
    params2.globalAs = peer2GlobalAs;
    params2.remoteAs = peer2RemoteAs;
    auto& fm = fmWrapper_.get();
    auto peer1 = std::make_shared<FiberBgpPeer>(
        params1,
        fm,
        evb_,
        std::move(*fs0_),
        peer1Input_,
        peer1BoundedInput_,
        peer1Output_,
        false /* isRestarting */,
        FLAGS_enable_egress_queue_backpressure);
    auto peer2 = std::make_shared<FiberBgpPeer>(
        params2,
        fm,
        evb_,
        std::move(*fs1_),
        peer2Input_,
        peer2BoundedInput_,
        peer2Output_,
        false /* isRestarting */,
        FLAGS_enable_egress_queue_backpressure);
    auto asMismatchAtPeer2 = peer1LocalAs != peer2RemoteAs;
    auto asMismatchAtPeer1 = peer2LocalAs != peer1RemoteAs;
    fm.addTask([&establishedPeers,
                &fm,
                peer1,
                peer2,
                asMismatchAtPeer2,
                asMismatchAtPeer1] {
      // start all fibers
      fm.addTask([peer1] { peer1->run(); });
      fm.addTask([peer2] { peer2->run(); });

      /*
       * Observe state changes for both peers
       */

      RWQueue<FiberBgpPeer::ObservableStateT> combined;
      fm.addTask([&combined, peer1, peer2]() mutable {
        auto writer = combined.getWriter();
        mergeQueuesStatic(
            writer,
            peer1->getObserverStateQueue(),
            peer2->getObserverStateQueue());
      });

      while (true) {
        auto msg = combined.get();
        if (!msg) {
          XLOG(INFO, "Both peers terminated");
          break;
        }
        XLOGF(
            INFO,
            "Peer {} state is now {}",
            msg->peerId.peerAddr.str(),
            static_cast<int>(msg->state));
        if (msg->state == BgpSessionState::ESTABLISHED) {
          establishedPeers.insert(msg->peerId.peerAddr);
        }
        if (establishedPeers.size() == 2) {
          XLOG(INFO, "Both peers established, asking to terminate");
          peer1->stop();
          peer2->stop();
        }
      }
      if (!asMismatchAtPeer2 && !asMismatchAtPeer1) {
        EXPECT_EQ(ResetReason::MANUAL_STOP, peer1->getResetReason());
        EXPECT_EQ(
            "MANUAL_STOP", getResetReasonName(peer1->getResetReason().value()));
        EXPECT_EQ(ResetReason::MANUAL_STOP, peer2->getResetReason());
        EXPECT_EQ(
            "MANUAL_STOP", getResetReasonName(peer2->getResetReason().value()));
      } else if (asMismatchAtPeer2 && !asMismatchAtPeer1) {
        EXPECT_EQ(ResetReason::SESSION_ERR, peer2->getResetReason());
        EXPECT_EQ(
            "SESSION_ERR", getResetReasonName(peer2->getResetReason().value()));
        /*
         * There is a race condition whereby peer1 may get socket error before
         * it gets notification from peer, so check that we get one or the
         * other.
         */
        auto peer1ResetReasonName =
            getResetReasonName(peer1->getResetReason().value());
        XLOGF(INFO, "Reset Reason for Peer1 is {}", peer1ResetReasonName);
        EXPECT_TRUE(
            peer1->getResetReason() == ResetReason::SOCKET_ERR ||
            peer1->getResetReason() == ResetReason::NOTIFICATION_RCVD);
        EXPECT_TRUE(
            (peer1ResetReasonName.compare("SOCKET_ERR") == 0) ||
            (peer1ResetReasonName.compare("NOTIFICATION_RCVD") == 0));
      } else if (!asMismatchAtPeer2 && asMismatchAtPeer1) {
        // Ideally, peer should receive notification. But we currently have a
        // known issue where socket error happens before receipt of notification
        // EXPECT_EQ(ResetReason::NOTIFICATION_RCVD, peer2->getResetReason());
        EXPECT_EQ(ResetReason::SOCKET_ERR, peer2->getResetReason());
        EXPECT_EQ(
            "SOCKET_ERR", getResetReasonName(peer2->getResetReason().value()));
        EXPECT_EQ(ResetReason::SESSION_ERR, peer1->getResetReason());
        EXPECT_EQ(
            "SESSION_ERR", getResetReasonName(peer1->getResetReason().value()));
      } else {
        EXPECT_EQ(ResetReason::SESSION_ERR, peer1->getResetReason());
        EXPECT_EQ(
            "SESSION_ERR", getResetReasonName(peer1->getResetReason().value()));
        EXPECT_EQ(ResetReason::SESSION_ERR, peer2->getResetReason());
        EXPECT_EQ(
            "SESSION_ERR", getResetReasonName(peer2->getResetReason().value()));
      }
    });
    evb_.loop();
  }
};

class BgpPeerSessionEstablishmentFixture
    : public FiberBgpPeerFixture,
      public ::testing::WithParamInterface<std::pair<uint32_t, uint32_t>> {};

INSTANTIATE_TEST_CASE_P(
    BgpPeerSessionEstablishmentInstance,
    BgpPeerSessionEstablishmentFixture,
    ::testing::Values(
        std::make_pair<uint32_t, uint32_t>(4200000001, 4200000002),
        std::make_pair<uint32_t, uint32_t>(4200000001, 1234),
        std::make_pair<uint32_t, uint32_t>(1234, 2345)));

TEST_P(BgpPeerSessionEstablishmentFixture, SessionEstablishmentAsn) {
  /*
   * Try establishing a basic session and make sure
   * both peers advance to established state
   * use a mixture of 4 bytes and 2 bytes asn
   */
  const auto& param = GetParam();
  const auto& peer1LocalAs = std::get<0>(param);
  const auto& peer2LocalAs = std::get<1>(param);

  auto params1 = params1_;
  params1.localAs = peer1LocalAs;
  params1.remoteAs = peer2LocalAs;
  auto params2 = params2_;
  params2.localAs = peer2LocalAs;
  params2.remoteAs = peer1LocalAs;

  // Establish connection
  auto& fm = fmWrapper_.get();
  auto peer1 = std::make_shared<FiberBgpPeer>(
      params1,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_,
      false /* isRestarting */,
      FLAGS_enable_egress_queue_backpressure);
  auto peer2 = std::make_shared<FiberBgpPeer>(
      params2,
      fm,
      evb_,
      std::move(*fs1_),
      peer2Input_,
      peer2BoundedInput_,
      peer2Output_,
      false /* isRestarting */,
      FLAGS_enable_egress_queue_backpressure);

  fm.addTask([this, &fm, peer1, peer2] {
    // start all fibers
    fm.addTask([peer1] { peer1->run(); });
    fm.addTask([peer2] { peer2->run(); });

    /*
     * Observe state changes for both peers
     */

    RWQueue<FiberBgpPeer::ObservableStateT> combined;
    fm.addTask([&combined, peer1, peer2]() mutable {
      auto writer = combined.getWriter();
      mergeQueuesStatic(
          writer,
          peer1->getObserverStateQueue(),
          peer2->getObserverStateQueue());
    });

    std::set<folly::IPAddress> establishedPeers;
    while (true) {
      auto msg = combined.get();
      if (!msg) {
        XLOG(INFO, "Both peers terminated");
        break;
      }
      XLOGF(
          INFO,
          "Peer {} state is now {}",
          msg->peerId.peerAddr.str(),
          static_cast<int>(msg->state));
      if (msg->state == BgpSessionState::ESTABLISHED) {
        establishedPeers.insert(msg->peerId.peerAddr);
      }
      if (establishedPeers.size() == 2) {
        XLOG(INFO, "Both peers established, asking to terminate");
        peer1->stop();
        peer2->stop();
      }
    }
    EXPECT_EQ(ResetReason::MANUAL_STOP, peer1->getResetReason());
    EXPECT_EQ(ResetReason::MANUAL_STOP, peer2->getResetReason());
  });

  evb_.loop();
  SUCCEED();
}

TEST_F(FiberBgpPeerFixture, LocalAsSessionEstablishment) {
  std::set<folly::IPAddress> establishedPeers;
  runLocalAsSessionTest(1235, 1234, 5678, 5678, 5678, 1235, establishedPeers);
  EXPECT_EQ(establishedPeers.size(), 2);
  SUCCEED();
}

TEST_F(FiberBgpPeerFixture, LocalAsBothPeerSessionEstablishment) {
  std::set<folly::IPAddress> establishedPeers;
  runLocalAsSessionTest(1235, 1234, 5679, 5679, 5678, 1235, establishedPeers);
  EXPECT_EQ(establishedPeers.size(), 2);
  SUCCEED();
}

TEST_F(FiberBgpPeerFixture, LocalAsMismatchedRemoteAsSessionEstablishment) {
  std::set<folly::IPAddress> establishedPeers;
  runLocalAsSessionTest(1235, 1234, 5678, 5678, 5678, 1234, establishedPeers);
  EXPECT_EQ(establishedPeers.size(), 0);
  SUCCEED();
}

/*
 * Start one peer, sockets wont connect, expect termination
 */
TEST_F(FiberBgpPeerFixture, OnePeerStartsNoConnect) {
  auto& fm = fmWrapper_.get();
  auto peer1 = std::make_shared<FiberBgpPeer>(
      params3_,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_,
      false /* isRestarting */,
      FLAGS_enable_egress_queue_backpressure);

  fs1_->close();
  fm.addTask([this, &fm, peer1] {
    // start all fibers
    fm.addTask([peer1] { peer1->run(); });

    /*
     * Observe state changes for both peers
     */

    auto queue = peer1->getObserverStateQueue();
    while (true) {
      XLOG(INFO, "waiting for peer 1 to terminate");
      auto msg = queue.get();
      if (!msg) {
        XLOG(INFO, "Finished waiting for the first peer");
        break;
      }
    }
    EXPECT_EQ(ResetReason::SOCKET_ERR, peer1->getResetReason());
  });

  evb_.loop();
  SUCCEED();
}

/*
 * stop(peerDelete=true) puts BgpSessionStop{peerDelete=true} on errorQueue_;
 * FSM consumes it via processBgpSessionStop and emits the IDLE
 * ObservableStateT with peerDelete propagated. Verifies the FIFO wiring
 * dropPeer -> stop -> errorQueue_ -> IDLE event.
 */
TEST_F(FiberBgpPeerFixture, StopPeerDeleteFlagPropagatesToIdleEvent) {
  auto& fm = fmWrapper_.get();
  auto peer1 = std::make_shared<FiberBgpPeer>(
      params3_,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_,
      false /* isRestarting */,
      FLAGS_enable_egress_queue_backpressure);

  bool sawIdle = false;
  bool idlePeerDelete = false;
  fm.addTask([&fm, peer1, &sawIdle, &idlePeerDelete] {
    fm.addTask([peer1] { peer1->run(); });
    // Schedule stop() with peerDelete=true.
    fm.addTask([peer1] {
      peer1->stop(std::nullopt, /*gracefulRestart=*/true, /*peerDelete=*/true);
    });

    auto queue = peer1->getObserverStateQueue();
    while (true) {
      auto msg = queue.get();
      if (!msg) {
        break;
      }
      if (msg->state == BgpSessionState::IDLE) {
        sawIdle = true;
        idlePeerDelete = msg->peerDelete;
      }
    }
  });

  evb_.loop();
  EXPECT_TRUE(sawIdle);
  EXPECT_TRUE(idlePeerDelete)
      << "peerDelete=true on stop() did not propagate onto the IDLE event";
}

/*
 * Start one peer, sockets connected, expect timeout
 */
TEST_F(FiberBgpPeerFixture, OnePeerStartsWithConnection) {
  auto& fm = fmWrapper_.get();
  auto peer1 = std::make_shared<FiberBgpPeer>(
      params3_,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_,
      false /* isRestarting */,
      FLAGS_enable_egress_queue_backpressure);

  fm.addTask([this, &fm, peer1] {
    // start all fibers
    fm.addTask([peer1] { peer1->run(); });

    auto queue = peer1->getObserverStateQueue();
    while (true) {
      XLOG(INFO, "waiting for peer 1 to terminate");
      auto msg = queue.get();
      if (!msg) {
        XLOG(INFO, "Finished waiting for the first peer");
        break;
      }
    }
    EXPECT_EQ(ResetReason::OPEN_MSG_TIMER_EXPIRE, peer1->getResetReason());
  });

  evb_.loop();
  SUCCEED();
}

/*
 * Start two peers, terminate one of them; hold time set to 3 seconds
 */
TEST_F(FiberBgpPeerFixture, AnnouncementWithSocketErr) {
  auto& fm = fmWrapper_.get();
  auto peer1 = std::make_shared<FiberBgpPeer>(
      params3_,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_,
      false /* isRestarting */,
      FLAGS_enable_egress_queue_backpressure);
  auto peer2 = std::make_shared<FiberBgpPeer>(
      params4_,
      fm,
      evb_,
      std::move(*fs1_),
      peer2Input_,
      peer2BoundedInput_,
      peer2Output_,
      false /* isRestarting */,
      FLAGS_enable_egress_queue_backpressure);

  // start all fibers
  fm.addTask([&peer1] { peer1->run(); });
  fm.addTask([&peer2] { peer2->run(); });

  // Observe state changes for both peers
  RWQueue<FiberBgpPeer::ObservableStateT> combined;
  fm.addTask([&combined, &peer1, &peer2]() mutable {
    auto writer = combined.getWriter();
    mergeQueuesStatic(
        writer, peer1->getObserverStateQueue(), peer2->getObserverStateQueue());
  });

  fm.addTask([this, &combined, &peer1] {
    std::set<folly::IPAddress> establishedPeers;
    while (true) {
      auto msg = combined.get();
      if (!msg) {
        XLOG(INFO, "Aborting combined wait loop");
        break;
      }
      XLOGF(
          INFO,
          "Peer {} state is now {}",
          msg->peerId.peerAddr.str(),
          static_cast<int>(msg->state));
      if (msg->state == BgpSessionState::ESTABLISHED) {
        establishedPeers.insert(msg->peerId.peerAddr);
      }
      if (establishedPeers.size() == 2) {
        XLOG(INFO, "Both peers established, exchanging updates");

        // Build 2 different BgpUpdate2 struct
        BgpAttrCommunity community;
        community.asn() = 65530;
        community.value() = 15800;

        // 1 v4 announcement sent from peer1 (2.2.2.2)
        auto tmpBgpUpdate1 = std::make_shared<BgpUpdate2>();
        tmpBgpUpdate1->attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
        tmpBgpUpdate1->attrs()->nexthop() = "2.2.2.2";
        tmpBgpUpdate1->attrs()->med() = 32;
        tmpBgpUpdate1->attrs()->isMedSet() = true;
        tmpBgpUpdate1->attrs()->localPref() = 100;
        tmpBgpUpdate1->attrs()->communities()->emplace_back(community);
        auto prefix =
            toIPPrefix(folly::IPAddress::createNetwork("10.1.1.0/24"));
        RiggedIPPrefix rigPrefix;
        rigPrefix.prefix() = prefix;
        tmpBgpUpdate1->v4Announced2()->emplace_back(rigPrefix);
        tmpBgpUpdate1->v4Nexthop() =
            toBinaryAddress(folly::IPAddress("2.2.2.2"));

        // another v4 announcement sent from peer1 (2.2.2.2)
        auto tmpBgpUpdate2 = std::make_shared<BgpUpdate2>();
        tmpBgpUpdate2->attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
        tmpBgpUpdate2->attrs()->nexthop() = "2.2.2.2";
        tmpBgpUpdate2->attrs()->med() = 32;
        tmpBgpUpdate2->attrs()->isMedSet() = true;
        tmpBgpUpdate2->attrs()->localPref() = 100;
        tmpBgpUpdate2->attrs()->communities()->emplace_back(community);
        auto prefix2 =
            toIPPrefix(folly::IPAddress::createNetwork("11.1.1.0/24"));
        RiggedIPPrefix rigPrefix2;
        rigPrefix2.prefix() = prefix2;
        tmpBgpUpdate2->v4Announced2()->emplace_back(rigPrefix2);
        tmpBgpUpdate2->v4Nexthop() =
            toBinaryAddress(folly::IPAddress("2.2.2.2"));

        // send 2 BGP update in separate messages from peer1
        if (FLAGS_enable_egress_queue_backpressure) {
          ASSERT_TRUE(peer1BoundedInput_->push(std::move(tmpBgpUpdate1)));
          ASSERT_TRUE(peer1BoundedInput_->push(std::move(tmpBgpUpdate2)));
        } else {
          peer1Input_->push(std::move(tmpBgpUpdate1));
          peer1Input_->push(std::move(tmpBgpUpdate2));
        }

        XLOG(INFO, "Successfully sent 2 updates to peer1");

        // Terminate peer1 to mimick socket error
        peer1->stop();

        // Clean up
        establishedPeers.clear();
      }
    }

    /*
     * Confirm that both advertisement is received and processed before
     * socket error happened.
     */
    facebook::bgp::test::boundedBlockingPop(*peer2Output_, "peer2Output_");
    facebook::bgp::test::boundedBlockingPop(*peer2Output_, "peer2Output_");
  });

  evb_.loop();
  SUCCEED();
}

/*
 * Start two peers, terminate one of them; hold time set to 3 seconds
 */
TEST_F(FiberBgpPeerFixture, OnePeerTerminates) {
  auto& fm = fmWrapper_.get();
  auto peer1 = std::make_shared<FiberBgpPeer>(
      params3_,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_,
      false /* isRestarting */,
      FLAGS_enable_egress_queue_backpressure);
  auto peer2 = std::make_shared<FiberBgpPeer>(
      params4_,
      fm,
      evb_,
      std::move(*fs1_),
      peer2Input_,
      peer2BoundedInput_,
      peer2Output_,
      false /* isRestarting */,
      FLAGS_enable_egress_queue_backpressure);

  fm.addTask([this, &fm, peer1, peer2] {
    // start all fibers
    fm.addTask([peer1] { peer1->run(); });
    fm.addTask([peer2] { peer2->run(); });

    /*
     * Observe state changes for both peers
     */
    RWQueue<FiberBgpPeer::ObservableStateT> combined;
    fm.addTask([&combined, peer1, peer2]() mutable {
      auto writer = combined.getWriter();
      mergeQueuesStatic(
          writer,
          peer1->getObserverStateQueue(),
          peer2->getObserverStateQueue());
    });

    std::set<folly::IPAddress> establishedPeers;
    while (true) {
      auto msg = combined.get();
      if (!msg) {
        XLOG(INFO, "Aborting combined wait loop");
        break;
      }
      XLOGF(
          INFO,
          "Peer {} state is now {}",
          msg->peerId.peerAddr.str(),
          static_cast<int>(msg->state));
      if (msg->state == BgpSessionState::ESTABLISHED) {
        establishedPeers.insert(msg->peerId.peerAddr);
      }
      if (establishedPeers.size() == 2) {
        fiberSleepFor(6s);
        XLOG(INFO, "Telling peer2 to terminate");
        peer2->stop();
      }
    }
    EXPECT_EQ(ResetReason::SOCKET_ERR, peer1->getResetReason());
    EXPECT_EQ(ResetReason::MANUAL_STOP, peer2->getResetReason());
  });

  evb_.loop();
  SUCCEED();
}

/*
 * Try establishing a basic session and make sure
 * both peers advance to established state.
 * Then, exchange one BgpUpdate2 each other.
 */
TEST_F(FiberBgpPeerFixture, ExchangeBgpUpdate) {
  establishTwoPeersAndTest(std::nullopt, update1_, update2_);
}

/*
 * We try establishing a basic session with various hold time configurations.
 */
TEST_F(FiberBgpPeerFixture, HoldTimeNegotiationTest) {
  auto& fm = fmWrapper_.get();

  // peer1 is set with hold time 180 sec always.
  PeeringParams params1{
      folly::IPAddress("1.1.1.1"), // peerAddr
      std::nullopt, // peerPrefix
      1234, // globalAs
      1234, // localAs
      1234, // remoteAs
      IPAddressV4("2.2.2.2"), // routerId
      std::chrono::seconds(180), // holdTime
      std::chrono::seconds(120) // grRestartTime
  };

  {
    /*
     * peer2 is set with hold time 30 sec.
     * negotiated hold time should be 30 sec.
     */
    PeeringParams params2{
        folly::IPAddress("2.2.2.2"), // peerAddr
        std::nullopt, // peerPrefix
        1234, // globalAs
        1234, // localAs
        1234, // remoteAs
        IPAddressV4("1.1.1.1"), // routerId
        std::chrono::seconds(30), // holdTime
        std::chrono::seconds(20) // grRestartTime
    };

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      EXPECT_EQ(30, peer1->getHoldTime().count());
      EXPECT_EQ(10, peer1->getKeepAliveTime().count());
      EXPECT_EQ(30, peer1->getRemoteHoldTime().count());
      EXPECT_EQ(30, peer1->getNegotiatedHoldTime()->count());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      EXPECT_EQ(30, peer2->getHoldTime().count());
      EXPECT_EQ(10, peer2->getKeepAliveTime().count());
      EXPECT_EQ(180, peer2->getRemoteHoldTime().count());
      EXPECT_EQ(30, peer2->getNegotiatedHoldTime()->count());

      stopTwoPeers(peer1, peer2);
    });
    evb_.loop();
  }
  {
    /*
     * peer2 is set with hold time 3 sec.
     * negotiated hold time should be 3 sec.
     */
    PeeringParams params2{
        folly::IPAddress("2.2.2.2"), // peerAddr
        std::nullopt, // peerPrefix
        1234, // globalAs
        1234, // localAs
        1234, // remoteAs
        IPAddressV4("1.1.1.1"), // routerId
        std::chrono::seconds(3), // holdTime
        std::chrono::seconds(2) // grRestartTime
    };

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      EXPECT_EQ(3, peer1->getHoldTime().count());
      EXPECT_EQ(1, peer1->getKeepAliveTime().count());
      EXPECT_EQ(3, peer1->getRemoteHoldTime().count());
      EXPECT_EQ(3, peer1->getNegotiatedHoldTime()->count());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      EXPECT_EQ(3, peer2->getHoldTime().count());
      EXPECT_EQ(1, peer2->getKeepAliveTime().count());
      EXPECT_EQ(180, peer2->getRemoteHoldTime().count());
      EXPECT_EQ(3, peer2->getNegotiatedHoldTime()->count());

      stopTwoPeers(peer1, peer2);
    });
    evb_.loop();
  }
  {
    /*
     * peer2 is set with hold time 2 sec.
     * session should not come up.
     */
    PeeringParams params2{
        folly::IPAddress("2.2.2.2"), // peerAddr
        std::nullopt, // peerPrefix
        1234, // globalAs
        1234, // localAs
        1234, // remoteAs
        IPAddressV4("1.1.1.1"), // routerId
        std::chrono::seconds(2), // holdTime
        std::chrono::seconds(2) // grRestartTime
    };

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      /*
       * wait till session comes up, but it won't come up.
       * as a result, it would stuck on OPEN_SENT after 100ms
       */
      EXPECT_NE(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_NE(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::OPEN_SENT, peer1->getBgpSessionState());
      EXPECT_EQ(0, peer1->getHoldTime().count());
      EXPECT_EQ(0, peer1->getKeepAliveTime().count());
      EXPECT_EQ(0, peer1->getRemoteHoldTime().count());
      EXPECT_FALSE(peer1->getNegotiatedHoldTime());

      EXPECT_EQ(BgpSessionState::OPEN_SENT, peer2->getBgpSessionState());
      EXPECT_EQ(0, peer2->getHoldTime().count());
      EXPECT_EQ(0, peer2->getKeepAliveTime().count());
      EXPECT_EQ(0, peer2->getRemoteHoldTime().count());
      EXPECT_FALSE(peer2->getNegotiatedHoldTime());

      stopTwoPeers(peer1, peer2);
    });
    evb_.loop();
  }
  {
    /*
     * peer2 is set with hold time 0 sec.
     * session should come up.
     */
    PeeringParams params2{
        folly::IPAddress("2.2.2.2"), // peerAddr
        std::nullopt, // peerPrefix
        1234, // globalAs
        1234, // localAs
        1234, // remoteAs
        IPAddressV4("1.1.1.1"), // routerId
        std::chrono::seconds(0), // holdTime
        std::chrono::seconds(120) // grRestartTime
    };

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      EXPECT_EQ(0, peer1->getHoldTime().count());
      EXPECT_EQ(0, peer1->getKeepAliveTime().count());
      EXPECT_EQ(0, peer1->getRemoteHoldTime().count());
      EXPECT_EQ(0, peer1->getNegotiatedHoldTime()->count());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      EXPECT_EQ(0, peer2->getHoldTime().count());
      EXPECT_EQ(0, peer2->getKeepAliveTime().count());
      EXPECT_EQ(180, peer2->getRemoteHoldTime().count());
      EXPECT_EQ(0, peer1->getNegotiatedHoldTime()->count());

      stopTwoPeers(peer1, peer2);
    });
    evb_.loop();
  }
  SUCCEED();
}

/*
 * We try establishing a basic session with various graceful restart
 * configurations.
 */
TEST_F(FiberBgpPeerFixture, GRCapabilityTest) {
  auto& fm = fmWrapper_.get();

  // peer1 is set with grRestartTime 120 sec always.
  PeeringParams params1{
      folly::IPAddress("1.1.1.1"), // peerAddr
      std::nullopt, // peerPrefix
      1234, // globalAs
      1234, // localAs
      1234, // remoteAs
      IPAddressV4("2.2.2.2"), // routerId
      std::chrono::seconds(180), // holdTime
      std::chrono::seconds(120) // grRestartTime
  };

  {
    // peer2 is set with grRestartTime 30 sec.
    PeeringParams params2{
        folly::IPAddress("2.2.2.2"), // peerAddr
        std::nullopt, // peerPrefix
        1234, // globalAs
        1234, // localAs
        1234, // remoteAs
        IPAddressV4("1.1.1.1"), // routerId
        std::chrono::seconds(30), // holdTime
        std::chrono::seconds(30) // grRestartTime
    };

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      EXPECT_TRUE(*peer1->getRemoteCapabilities().gracefulRestart());
      EXPECT_EQ(30, *(peer1->getRemoteGrRestartTime()));

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      EXPECT_TRUE(*peer2->getRemoteCapabilities().gracefulRestart());
      EXPECT_EQ(120, *(peer2->getRemoteGrRestartTime()));

      stopTwoPeers(peer1, peer2);
    });
    evb_.loop();
  }
  {
    // peer2 has not GR config.
    PeeringParams params2{
        folly::IPAddress("2.2.2.2"), // peerAddr
        std::nullopt, // peerPrefix
        1234, // globalAs
        1234, // localAs
        1234, // remoteAs
        IPAddressV4("1.1.1.1"), // routerId
        std::chrono::seconds(3), // holdTime
        std::nullopt, // grRestartTime
    };

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      EXPECT_FALSE(*peer1->getRemoteCapabilities().gracefulRestart());
      EXPECT_EQ(std::nullopt, peer1->getRemoteGrRestartTime());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      EXPECT_TRUE(*peer2->getRemoteCapabilities().gracefulRestart());
      EXPECT_EQ(120, *(peer2->getRemoteGrRestartTime()));

      stopTwoPeers(peer1, peer2);
    });
    evb_.loop();
  }
  SUCCEED();
}

/*
 * When grRestartTime is not configured (nullopt), GR capability TLV must not
 * be advertised. The negotiated result should have gracefulRestart = false,
 * acting as a no-GR-helper.
 */
TEST_F(FiberBgpPeerFixture, NoGRCapabilityWhenUnconfigured) {
  auto& fm = fmWrapper_.get();

  // Neither peer has grRestartTime configured
  PeeringParams params1{
      folly::IPAddress("1.1.1.1"), // peerAddr
      std::nullopt, // peerPrefix
      1234, // globalAs
      1234, // localAs
      1234, // remoteAs
      IPAddressV4("2.2.2.2"), // routerId
      std::chrono::seconds(180), // holdTime
      std::nullopt // grRestartTime - not configured
  };

  PeeringParams params2{
      folly::IPAddress("2.2.2.2"), // peerAddr
      std::nullopt, // peerPrefix
      1234, // globalAs
      1234, // localAs
      1234, // remoteAs
      IPAddressV4("1.1.1.1"), // routerId
      std::chrono::seconds(180), // holdTime
      std::nullopt // grRestartTime - not configured
  };

  fm.addTask([&] {
    std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
    std::tie(peer1, peer2) = startTwoPeers(params1, params2);

    // wait till session comes up
    EXPECT_EQ(
        runUntilTargetStateOrTimeout(
            peer1, BgpSessionState::ESTABLISHED, 100ms),
        0);
    EXPECT_EQ(
        runUntilTargetStateOrTimeout(
            peer2, BgpSessionState::ESTABLISHED, 100ms),
        0);

    // Neither peer should see GR capability from the other
    EXPECT_FALSE(*peer1->getRemoteCapabilities().gracefulRestart());
    EXPECT_EQ(std::nullopt, peer1->getRemoteGrRestartTime());

    EXPECT_FALSE(*peer2->getRemoteCapabilities().gracefulRestart());
    EXPECT_EQ(std::nullopt, peer2->getRemoteGrRestartTime());

    // Negotiated result should also have GR disabled
    EXPECT_FALSE(*peer1->getNegotiatedCapabilities().gracefulRestart());
    EXPECT_FALSE(*peer2->getNegotiatedCapabilities().gracefulRestart());

    stopTwoPeers(peer1, peer2);
  });
  evb_.loop();
  SUCCEED();
}

/*
 * We try to establish a basic session with Extended Next Hop Encoding
 * capabilities as specified in RFC 5549
 */
TEST_F(FiberBgpPeerFixture, ExtNHEncodingCapabilityTest) {
  auto& fm = fmWrapper_.get();

  PeeringParams params1;
  params1.peerAddr = folly::IPAddress("1.1.1.1");
  params1.peerPrefix = std::nullopt;
  params1.globalAs = 1234;
  params1.localAs = 1234;
  params1.remoteAs = 1234;
  params1.localBgpId = folly::IPAddressV4("2.2.2.2");
  params1.localClusterId = folly::IPAddressV4("2.2.2.2");
  params1.holdTime = std::chrono::seconds(30);
  params1.isAfiIpv4Configured = true;
  params1.v4OverV6Nexthop = true;

  {
    // Peer support RFC 5549
    PeeringParams params2 = params1;
    params2.peerAddr = folly::IPAddress("3.3.3.3");
    params2.localBgpId = folly::IPAddressV4("4.4.4.4");
    params2.localClusterId = folly::IPAddressV4("4.4.4.4");

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      auto capa1 =
          *peer1->getNegotiatedCapabilities().extNHEncodingCapabilities();
      EXPECT_EQ(1, capa1.size()); // <1,1,2>
      BgpExtNHEncodingCapability capability;
      capability.nlriAfi() = BgpUpdateAfi::AFI_IPv4;
      capability.nlriSafi() = BgpUpdateSafi::SAFI_UNICAST;
      capability.nhAfi() = BgpUpdateAfi::AFI_IPv6;
      std::vector<BgpExtNHEncodingCapability> expect;
      expect.push_back(std::move(capability));
      EXPECT_THAT(expect, capa1);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      auto capa2 =
          *peer2->getNegotiatedCapabilities().extNHEncodingCapabilities();
      EXPECT_THAT(expect, capa2);

      stopTwoPeers(peer1, peer2);
    });

    evb_.loop();
  }

  {
    // Peer doesn't support RFC 5549
    PeeringParams params2 = params1;
    params2.peerAddr = folly::IPAddress("3.3.3.3");
    params2.localBgpId = folly::IPAddressV4("4.4.4.4");
    params2.localClusterId = folly::IPAddressV4("4.4.4.4");
    params2.v4OverV6Nexthop = false;

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      auto capa1 =
          *peer1->getNegotiatedCapabilities().extNHEncodingCapabilities();
      EXPECT_EQ(0, capa1.size());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      auto capa2 =
          *peer2->getNegotiatedCapabilities().extNHEncodingCapabilities();
      EXPECT_EQ(0, capa2.size());

      stopTwoPeers(peer1, peer2);
    });

    evb_.loop();
  }

  {
    // Peer supports RFC 5549, but local doesn't by disable ipv4 announcement
    PeeringParams params2 = params1;
    params2.peerAddr = folly::IPAddress("3.3.3.3");
    params2.localBgpId = folly::IPAddressV4("4.4.4.4");
    params2.localClusterId = folly::IPAddressV4("4.4.4.4");

    params1.isAfiIpv4Configured = false;

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      auto capa1 =
          *peer1->getNegotiatedCapabilities().extNHEncodingCapabilities();
      EXPECT_EQ(0, capa1.size());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      auto capa2 =
          *peer2->getNegotiatedCapabilities().extNHEncodingCapabilities();
      EXPECT_EQ(0, capa2.size());

      stopTwoPeers(peer1, peer2);
    });
    evb_.loop();
  }

  SUCCEED();
}

/*
 * We try establishing a basic session with various Add path
 * configurations.
 */
TEST_F(FiberBgpPeerFixture, AddPathCapabilityTest) {
  auto& fm = fmWrapper_.get();

  PeeringParams params1{
      folly::IPAddress("1.1.1.1"), // peerAddr
      std::nullopt, // peerPrefix
      1234, // globalAs
      1234, // localAs
      1234, // remoteAs
      IPAddressV4("2.2.2.2"), // routerId
      neteng::fboss::bgp_attr::AddPath::BOTH,
      std::chrono::seconds(180) // holdTime
  };

  {
    PeeringParams params2{
        folly::IPAddress("2.2.2.2"), // peerAddr
        std::nullopt, // peerPrefix
        1234, // globalAs
        1234, // localAs
        1234, // remoteAs
        IPAddressV4("1.1.1.1"), // routerId
        neteng::fboss::bgp_attr::AddPath::BOTH,
        std::chrono::seconds(30) // holdTime
    };

    // BOTH | BOTH
    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      auto apCapa1 = *peer1->getNegotiatedCapabilities().addPathCapabilities();
      EXPECT_EQ(2, apCapa1.size());
      BgpAddPathCapability capability1;
      capability1.afi() = BgpUpdateAfi::AFI_IPv4;
      capability1.safi() = BgpUpdateSafi::SAFI_UNICAST;
      capability1.sor() = BgpAddPathSendRec::BOTH;
      BgpAddPathCapability capability2;
      capability2.afi() = BgpUpdateAfi::AFI_IPv6;
      capability2.safi() = BgpUpdateSafi::SAFI_UNICAST;
      capability2.sor() = BgpAddPathSendRec::BOTH;
      std::vector<BgpAddPathCapability> expect;
      expect.push_back(std::move(capability1));
      expect.push_back(std::move(capability2));
      EXPECT_THAT(apCapa1, expect);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      auto apCapa2 = *peer2->getNegotiatedCapabilities().addPathCapabilities();
      EXPECT_THAT(apCapa2, expect);
      stopTwoPeers(peer1, peer2);
    });
    evb_.loop();
  }

  {
    // BOTH | none
    PeeringParams params2{
        folly::IPAddress("2.2.2.2"), // peerAddr
        std::nullopt, // peerPrefix
        1234, // globalAs
        1234, // localAs
        1234, // remoteAs
        IPAddressV4("1.1.1.1"), // routerId
        std::nullopt, // addPath
        std::chrono::seconds(3), // holdTime
    };

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());

      auto apCapa1 = *peer1->getNegotiatedCapabilities().addPathCapabilities();
      EXPECT_EQ(0, apCapa1.size());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      auto apCapa2 = *peer2->getNegotiatedCapabilities().addPathCapabilities();
      EXPECT_EQ(0, apCapa2.size());

      stopTwoPeers(peer1, peer2);
    });
    evb_.loop();
  }

  {
    // BOTH | SEND
    PeeringParams params2{
        folly::IPAddress("2.2.2.2"), // peerAddr
        std::nullopt, // peerPrefix
        1234, // globalAs
        1234, // localAs
        1234, // remoteAs
        IPAddressV4("1.1.1.1"), // routerId
        neteng::fboss::bgp_attr::AddPath::SEND, // addpath capability
        std::chrono::seconds(3), // holdTime
    };

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      auto apCapa1 = *peer1->getNegotiatedCapabilities().addPathCapabilities();
      EXPECT_EQ(2, apCapa1.size());
      BgpAddPathCapability capability1;
      capability1.afi() = BgpUpdateAfi::AFI_IPv4;
      capability1.safi() = BgpUpdateSafi::SAFI_UNICAST;
      capability1.sor() = BgpAddPathSendRec::RECEIVE;
      BgpAddPathCapability capability2;
      capability2.afi() = BgpUpdateAfi::AFI_IPv6;
      capability2.safi() = BgpUpdateSafi::SAFI_UNICAST;
      capability2.sor() = BgpAddPathSendRec::RECEIVE;
      std::vector<BgpAddPathCapability> expect;
      expect.push_back(std::move(capability1));
      expect.push_back(std::move(capability2));
      EXPECT_THAT(apCapa1, expect);

      BgpAddPathCapability capability3;
      capability3.afi() = BgpUpdateAfi::AFI_IPv4;
      capability3.safi() = BgpUpdateSafi::SAFI_UNICAST;
      capability3.sor() = BgpAddPathSendRec::SEND;
      BgpAddPathCapability capability4;
      capability4.afi() = BgpUpdateAfi::AFI_IPv6;
      capability4.safi() = BgpUpdateSafi::SAFI_UNICAST;
      capability4.sor() = BgpAddPathSendRec::SEND;
      std::vector<BgpAddPathCapability> expect2;
      expect2.push_back(std::move(capability3));
      expect2.push_back(std::move(capability4));
      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      auto apCapa2 = *peer2->getNegotiatedCapabilities().addPathCapabilities();
      EXPECT_THAT(apCapa2, expect2);

      stopTwoPeers(peer1, peer2);
    });
    evb_.loop();
  }

  // change peer 1 to be able to send add path
  params1.addPath = neteng::fboss::bgp_attr::AddPath::SEND;

  {
    // SEND | RECEIVE
    PeeringParams params2{
        folly::IPAddress("2.2.2.2"), // peerAddr
        std::nullopt, // peerPrefix
        1234, // globalAs
        1234, // localAs
        1234, // remoteAs
        IPAddressV4("1.1.1.1"), // routerId
        neteng::fboss::bgp_attr::AddPath::RECEIVE, // addpath capability
        std::chrono::seconds(3), // holdTime
    };

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      auto apCapa1 = *peer1->getNegotiatedCapabilities().addPathCapabilities();
      EXPECT_EQ(2, apCapa1.size());
      BgpAddPathCapability capability1;
      capability1.afi() = BgpUpdateAfi::AFI_IPv4;
      capability1.safi() = BgpUpdateSafi::SAFI_UNICAST;
      capability1.sor() = BgpAddPathSendRec::SEND;
      BgpAddPathCapability capability2;
      capability2.afi() = BgpUpdateAfi::AFI_IPv6;
      capability2.safi() = BgpUpdateSafi::SAFI_UNICAST;
      capability2.sor() = BgpAddPathSendRec::SEND;
      std::vector<BgpAddPathCapability> expect;
      expect.push_back(std::move(capability1));
      expect.push_back(std::move(capability2));
      EXPECT_THAT(apCapa1, expect);

      BgpAddPathCapability capability3;
      capability3.afi() = BgpUpdateAfi::AFI_IPv4;
      capability3.safi() = BgpUpdateSafi::SAFI_UNICAST;
      capability3.sor() = BgpAddPathSendRec::RECEIVE;
      BgpAddPathCapability capability4;
      capability4.afi() = BgpUpdateAfi::AFI_IPv6;
      capability4.safi() = BgpUpdateSafi::SAFI_UNICAST;
      capability4.sor() = BgpAddPathSendRec::RECEIVE;
      std::vector<BgpAddPathCapability> expect2;
      expect2.push_back(std::move(capability3));
      expect2.push_back(std::move(capability4));
      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      auto apCapa2 = *peer2->getNegotiatedCapabilities().addPathCapabilities();
      EXPECT_THAT(apCapa2, expect2);

      stopTwoPeers(peer1, peer2);
    });
    evb_.loop();
  }

  {
    // SEND | SEND
    PeeringParams params2{
        folly::IPAddress("2.2.2.2"), // peerAddr
        std::nullopt, // peerPrefix
        1234, // globalAs
        1234, // localAs
        1234, // remoteAs
        IPAddressV4("1.1.1.1"), // routerId
        neteng::fboss::bgp_attr::AddPath::SEND, // addpath capability
        std::chrono::seconds(3), // holdTime
    };

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      auto apCapa1 = *peer1->getNegotiatedCapabilities().addPathCapabilities();
      EXPECT_EQ(0, apCapa1.size());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      auto apCapa2 = *peer2->getNegotiatedCapabilities().addPathCapabilities();
      EXPECT_EQ(0, apCapa2.size());
      stopTwoPeers(peer1, peer2);
    });
    evb_.loop();
  }

  // change peer 1 to be able to receive add path
  params1.addPath = neteng::fboss::bgp_attr::AddPath::RECEIVE;
  {
    // RECEIVE | RECEIVE
    PeeringParams params2{
        folly::IPAddress("2.2.2.2"), // peerAddr
        std::nullopt, // peerPrefix
        1234, // globalAs
        1234, // localAs
        1234, // remoteAs
        IPAddressV4("1.1.1.1"), // routerId
        neteng::fboss::bgp_attr::AddPath::RECEIVE, // addpath capability
        std::chrono::seconds(3), // holdTime
    };

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      auto apCapa1 = *peer1->getNegotiatedCapabilities().addPathCapabilities();
      EXPECT_EQ(0, apCapa1.size());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      auto apCapa2 = *peer2->getNegotiatedCapabilities().addPathCapabilities();
      EXPECT_EQ(0, apCapa2.size());
      stopTwoPeers(peer1, peer2);
    });
    evb_.loop();
  }

  SUCCEED();
}

/*
 * Establish a basic session to test Enhanced Route Refresh capability(ERR)
 */
TEST_F(FiberBgpPeerFixture, EnhancedRouteRefreshCapabilityTest) {
  auto& fm = fmWrapper_.get();

  PeeringParams params1;
  params1.peerAddr = folly::IPAddress("1.1.1.1");
  params1.peerPrefix = std::nullopt;
  params1.globalAs = 1234;
  params1.localAs = 1234;
  params1.remoteAs = 1234;
  params1.localBgpId = folly::IPAddressV4("2.2.2.2");
  params1.localClusterId = folly::IPAddressV4("2.2.2.2");
  params1.holdTime = std::chrono::seconds(30);
  params1.isAfiIpv4Configured = true;

  PeeringParams params2 = params1;
  params2.peerAddr = folly::IPAddress("3.3.3.3");
  params2.localBgpId = folly::IPAddressV4("4.4.4.4");
  params2.localClusterId = folly::IPAddressV4("4.4.4.4");

  {
    // Default behavior - ERR not enabled
    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      EXPECT_FALSE(*peer1->getRemoteCapabilities().enhancedRouteRefresh());
      EXPECT_FALSE(*peer1->getNegotiatedCapabilities().enhancedRouteRefresh());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      EXPECT_FALSE(*peer2->getRemoteCapabilities().enhancedRouteRefresh());
      EXPECT_FALSE(*peer2->getNegotiatedCapabilities().enhancedRouteRefresh());

      stopTwoPeers(peer1, peer2);
    });

    evb_.loop();
  }
  {
    // ERR configured for local and peer
    params1.isEnhancedRouteRefreshConfigured = true;
    params2.isEnhancedRouteRefreshConfigured = true;

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      EXPECT_TRUE(*peer1->getRemoteCapabilities().enhancedRouteRefresh());
      EXPECT_TRUE(*peer1->getNegotiatedCapabilities().enhancedRouteRefresh());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      EXPECT_TRUE(*peer2->getRemoteCapabilities().enhancedRouteRefresh());
      EXPECT_TRUE(*peer2->getNegotiatedCapabilities().enhancedRouteRefresh());

      stopTwoPeers(peer1, peer2);
    });

    evb_.loop();
  }

  {
    // ERR configured for local, but not for peer
    params1.isEnhancedRouteRefreshConfigured = true;
    params2.isEnhancedRouteRefreshConfigured = false;

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      EXPECT_FALSE(*peer1->getRemoteCapabilities().enhancedRouteRefresh());
      EXPECT_FALSE(*peer1->getNegotiatedCapabilities().enhancedRouteRefresh());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      EXPECT_TRUE(*peer2->getRemoteCapabilities().enhancedRouteRefresh());
      EXPECT_FALSE(*peer2->getNegotiatedCapabilities().enhancedRouteRefresh());

      stopTwoPeers(peer1, peer2);
    });

    evb_.loop();
  }

  {
    // ERR configured for peer, but not for local
    params2.isEnhancedRouteRefreshConfigured = true;
    params1.isEnhancedRouteRefreshConfigured = false;

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      EXPECT_TRUE(*peer1->getRemoteCapabilities().enhancedRouteRefresh());
      EXPECT_FALSE(*peer1->getNegotiatedCapabilities().enhancedRouteRefresh());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      EXPECT_FALSE(*peer2->getRemoteCapabilities().enhancedRouteRefresh());
      EXPECT_FALSE(*peer2->getNegotiatedCapabilities().enhancedRouteRefresh());

      stopTwoPeers(peer1, peer2);
    });
    evb_.loop();
  }

  SUCCEED();
}

/*
 * Establish a basic session to test Route Refresh capability (RFC 2918).
 * Mirrors EnhancedRouteRefreshCapabilityTest above, exercising the 4-case
 * negotiation matrix: neither / both / one-sided (each direction).
 */
TEST_F(FiberBgpPeerFixture, RouteRefreshCapabilityTest) {
  auto& fm = fmWrapper_.get();

  PeeringParams params1;
  params1.peerAddr = folly::IPAddress("1.1.1.1");
  params1.peerPrefix = std::nullopt;
  params1.globalAs = 1234;
  params1.localAs = 1234;
  params1.remoteAs = 1234;
  params1.localBgpId = folly::IPAddressV4("2.2.2.2");
  params1.localClusterId = folly::IPAddressV4("2.2.2.2");
  params1.holdTime = std::chrono::seconds(30);
  params1.isAfiIpv4Configured = true;

  PeeringParams params2 = params1;
  params2.peerAddr = folly::IPAddress("3.3.3.3");
  params2.localBgpId = folly::IPAddressV4("4.4.4.4");
  params2.localClusterId = folly::IPAddressV4("4.4.4.4");

  {
    // Default behavior - RR not enabled
    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      EXPECT_FALSE(*peer1->getRemoteCapabilities().routeRefresh());
      EXPECT_FALSE(*peer1->getNegotiatedCapabilities().routeRefresh());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      EXPECT_FALSE(*peer2->getRemoteCapabilities().routeRefresh());
      EXPECT_FALSE(*peer2->getNegotiatedCapabilities().routeRefresh());

      stopTwoPeers(peer1, peer2);
    });

    evb_.loop();
  }
  {
    // RR configured for local and peer
    params1.isRouteRefreshConfigured = true;
    params2.isRouteRefreshConfigured = true;

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      EXPECT_TRUE(*peer1->getRemoteCapabilities().routeRefresh());
      EXPECT_TRUE(*peer1->getNegotiatedCapabilities().routeRefresh());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      EXPECT_TRUE(*peer2->getRemoteCapabilities().routeRefresh());
      EXPECT_TRUE(*peer2->getNegotiatedCapabilities().routeRefresh());

      stopTwoPeers(peer1, peer2);
    });

    evb_.loop();
  }

  {
    // RR configured for local, but not for peer
    params1.isRouteRefreshConfigured = true;
    params2.isRouteRefreshConfigured = false;

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      EXPECT_FALSE(*peer1->getRemoteCapabilities().routeRefresh());
      EXPECT_FALSE(*peer1->getNegotiatedCapabilities().routeRefresh());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      EXPECT_TRUE(*peer2->getRemoteCapabilities().routeRefresh());
      EXPECT_FALSE(*peer2->getNegotiatedCapabilities().routeRefresh());

      stopTwoPeers(peer1, peer2);
    });

    evb_.loop();
  }

  {
    // RR configured for peer, but not for local
    params2.isRouteRefreshConfigured = true;
    params1.isRouteRefreshConfigured = false;

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      EXPECT_TRUE(*peer1->getRemoteCapabilities().routeRefresh());
      EXPECT_FALSE(*peer1->getNegotiatedCapabilities().routeRefresh());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      EXPECT_FALSE(*peer2->getRemoteCapabilities().routeRefresh());
      EXPECT_FALSE(*peer2->getNegotiatedCapabilities().routeRefresh());

      stopTwoPeers(peer1, peer2);
    });
    evb_.loop();
  }

  SUCCEED();
}

/*
 * We try establishing a basic session with various address family capability
 * configurations.
 */
TEST_F(FiberBgpPeerFixture, AfiCapabilityNegotiationTest) {
  auto& fm = fmWrapper_.get();

  // peer1 is set with ipv4/ipv6 unicast and ls
  PeeringParams params1{
      folly::IPAddress("1.1.1.1"), // peerAddr
      std::nullopt, // peerPrefix
      1234, // globalAs
      1234, // localAs
      1234, // remoteAs
      IPAddressV4("2.2.2.2"), // routerId
      std::chrono::seconds(180), // holdTime
      std::chrono::seconds(120), // grRestartTime
      AfiIpv4Configured(true),
      AfiIpv6Configured(true)};
  {
    /*
     * peer2 is set with ipv4 unicast only
     * negotiated address family should be only ipv4 unicast
     */
    PeeringParams params2{
        folly::IPAddress("2.2.2.2"), // peerAddr
        std::nullopt, // peerPrefix
        1234, // globalAs
        1234, // localAs
        1234, // remoteAs
        IPAddressV4("1.1.1.1"), // routerId
        std::chrono::seconds(30), // holdTime
        std::chrono::seconds(20), // grRestartTime
        AfiIpv4Configured(true),
        AfiIpv6Configured(false)};

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      EXPECT_TRUE(*peer1->getRemoteCapabilities().mpExtV4Unicast());
      EXPECT_FALSE(*peer1->getRemoteCapabilities().mpExtV6Unicast());
      EXPECT_TRUE(*peer1->getNegotiatedCapabilities().mpExtV4Unicast());
      EXPECT_FALSE(*peer1->getNegotiatedCapabilities().mpExtV6Unicast());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      EXPECT_TRUE(*peer2->getRemoteCapabilities().mpExtV4Unicast());
      EXPECT_TRUE(*peer2->getRemoteCapabilities().mpExtV6Unicast());
      EXPECT_TRUE(*peer2->getNegotiatedCapabilities().mpExtV4Unicast());
      EXPECT_FALSE(*peer2->getNegotiatedCapabilities().mpExtV6Unicast());

      stopTwoPeers(peer1, peer2);
    });
    evb_.loop();
  }
  {
    /*
     * peer2 is set with ipv6 unicast only
     * negotiated address family should be only ipv6 unicast
     */
    PeeringParams params2{
        folly::IPAddress("2.2.2.2"), // peerAddr
        std::nullopt, // peerPrefix
        1234, // globalAs
        1234, // localAs
        1234, // remoteAs
        IPAddressV4("1.1.1.1"), // routerId
        std::chrono::seconds(30), // holdTime
        std::chrono::seconds(20), // grRestartTime
        AfiIpv4Configured(false),
        AfiIpv6Configured(true)};

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      EXPECT_FALSE(*peer1->getRemoteCapabilities().mpExtV4Unicast());
      EXPECT_TRUE(*peer1->getRemoteCapabilities().mpExtV6Unicast());
      EXPECT_FALSE(*peer1->getNegotiatedCapabilities().mpExtV4Unicast());
      EXPECT_TRUE(*peer1->getNegotiatedCapabilities().mpExtV6Unicast());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      EXPECT_TRUE(*peer2->getRemoteCapabilities().mpExtV4Unicast());
      EXPECT_TRUE(*peer2->getRemoteCapabilities().mpExtV6Unicast());
      EXPECT_FALSE(*peer2->getNegotiatedCapabilities().mpExtV4Unicast());
      EXPECT_TRUE(*peer2->getNegotiatedCapabilities().mpExtV6Unicast());

      stopTwoPeers(peer1, peer2);
    });
    evb_.loop();
  }
  {
    /*
     * peer2 is set with ipv4/ipv6 unicast
     * negotiated address family should be ipv4/ipv6 unicast
     */
    PeeringParams params2{
        folly::IPAddress("2.2.2.2"), // peerAddr
        std::nullopt, // peerPrefix
        1234, // globalAs
        1234, // localAs
        1234, // remoteAs
        IPAddressV4("1.1.1.1"), // routerId
        std::chrono::seconds(30), // holdTime
        std::chrono::seconds(20), // grRestartTime
        AfiIpv4Configured(true),
        AfiIpv6Configured(true)};

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      EXPECT_TRUE(*peer1->getRemoteCapabilities().mpExtV4Unicast());
      EXPECT_TRUE(*peer1->getRemoteCapabilities().mpExtV6Unicast());
      EXPECT_TRUE(*peer1->getNegotiatedCapabilities().mpExtV4Unicast());
      EXPECT_TRUE(*peer1->getNegotiatedCapabilities().mpExtV6Unicast());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      EXPECT_TRUE(*peer2->getRemoteCapabilities().mpExtV4Unicast());
      EXPECT_TRUE(*peer2->getRemoteCapabilities().mpExtV6Unicast());
      EXPECT_TRUE(*peer2->getNegotiatedCapabilities().mpExtV4Unicast());
      EXPECT_TRUE(*peer2->getNegotiatedCapabilities().mpExtV6Unicast());

      stopTwoPeers(peer1, peer2);
    });
    evb_.loop();
  }
  {
    /*
     * peer2 is set with ipv4/ipv6 unicast and ls
     * negotiated address family should be ipv4/ipv6 unicast and ls
     */
    PeeringParams params2{
        folly::IPAddress("2.2.2.2"), // peerAddr
        std::nullopt, // peerPrefix
        1234, // globalAs
        1234, // localAs
        1234, // remoteAs
        IPAddressV4("1.1.1.1"), // routerId
        std::chrono::seconds(30), // holdTime
        std::chrono::seconds(20), // grRestartTime
        AfiIpv4Configured(true),
        AfiIpv6Configured(true)};

    fm.addTask([&] {
      std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
      std::tie(peer1, peer2) = startTwoPeers(params1, params2);

      // wait till session comes up
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer1, BgpSessionState::ESTABLISHED, 100ms),
          0);
      EXPECT_EQ(
          runUntilTargetStateOrTimeout(
              peer2, BgpSessionState::ESTABLISHED, 100ms),
          0);

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer1->getBgpSessionState());
      EXPECT_TRUE(*peer1->getRemoteCapabilities().mpExtV4Unicast());
      EXPECT_TRUE(*peer1->getRemoteCapabilities().mpExtV6Unicast());
      EXPECT_TRUE(*peer1->getNegotiatedCapabilities().mpExtV4Unicast());
      EXPECT_TRUE(*peer1->getNegotiatedCapabilities().mpExtV6Unicast());

      EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
      EXPECT_TRUE(*peer2->getRemoteCapabilities().mpExtV4Unicast());
      EXPECT_TRUE(*peer2->getRemoteCapabilities().mpExtV6Unicast());
      EXPECT_TRUE(*peer2->getNegotiatedCapabilities().mpExtV4Unicast());
      EXPECT_TRUE(*peer2->getNegotiatedCapabilities().mpExtV6Unicast());

      stopTwoPeers(peer1, peer2);
    });
    evb_.loop();
  }

  SUCCEED();
}

// Verify that NLRIs of non-negotiated Afi are not allowed
TEST_F(FiberBgpPeerFixture, RejectNlriOfNonNegotiatedAfi) {
  { // valid BgpUpdate2 containing ipv4 unicast announcement
    auto update = std::make_shared<BgpUpdate2>();
    update->v4Announced()->push_back(
        toIPPrefix(folly::IPAddress::createNetwork("6.5.4.3/32")));
    update->attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
    BgpAttrAsPathSegment segment;
    segment.asSequence()->push_back(32934);
    update->attrs()->asPath()->push_back(segment);
    update->v4Nexthop() = toBinaryAddress(folly::IPAddress("1.2.3.4"));
    update->attrs()->localPref() = 100;

    EXPECT_THROW(
        {
          try {
            // ipv4 unicast is not negotiated
            BgpCapabilities negotiatedCapabilities;
            negotiatedCapabilities.mpExtV4Unicast() = false;
            BgpSerializer visitor(negotiatedCapabilities);

            FiberBgpParser::BgpMessageT msg = update;
            auto result = std::visit(visitor, msg);
          } catch (BgpSerializerException& e) {
            EXPECT_EQ(BgpSerializerExceptionCode::AFI_MISMATCH, e.getCode());
            throw;
          }
        },
        BgpSerializerException);
  }
  {
    // valid BgpUpdate2 containing ipv6 unicast announcement
    auto update = std::make_shared<BgpUpdate2>();
    update->attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
    BgpAttrAsPathSegment segment;
    segment.asSequence()->push_back(32934);
    update->attrs()->asPath()->push_back(segment);
    update->attrs()->localPref() = 100;
    update->mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
    update->mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
    update->mpAnnounced()->nexthop() =
        toBinaryAddress(folly::IPAddress("fd00::1"));
    RiggedIPPrefix prefix;
    prefix.prefix() =
        toIPPrefix(folly::IPAddress::createNetwork("fd00::3000/122"));
    update->mpAnnounced()->prefixes()->push_back(prefix);

    EXPECT_THROW(
        {
          try {
            // ipv6 unicast is not negotiated
            BgpCapabilities negotiatedCapabilities;
            negotiatedCapabilities.mpExtV6Unicast() = false;
            BgpSerializer visitor(negotiatedCapabilities);

            FiberBgpParser::BgpMessageT msg = update;
            auto result = std::visit(visitor, msg);
          } catch (BgpSerializerException& e) {
            EXPECT_EQ(BgpSerializerExceptionCode::AFI_MISMATCH, e.getCode());
            throw;
          }
        },
        BgpSerializerException);
  }
}

/*
 * Establish a basic session and make sure both peers advance to established
 * state. After exchange of one BgpUpdate2 and send a notification. Verify
 * that in oQueue we see session stop with graceful restart false.
 */
TEST_F(FiberBgpPeerFixture, GracefulRestartNotificationTest) {
  auto lambdaVerifyGRNotification = [&]() {
    // Send a notification and verify gracefulRestart false
    auto copy_notify = notifyMsg1_;
    if (FLAGS_enable_egress_queue_backpressure) {
      ASSERT_TRUE(peer1BoundedInput_->push(std::move(copy_notify)));
    } else {
      peer1Input_->push(std::move(copy_notify));
    }

    auto stopMsg =
        facebook::bgp::test::boundedBlockingPop(*peer2Output_, "peer2Output_");
    ASSERT_TRUE(std::holds_alternative<FiberBgpPeer::BgpSessionStop>(stopMsg));
    auto sessionStop = std::get<FiberBgpPeer::BgpSessionStop>(stopMsg);
    EXPECT_EQ(false, sessionStop.gracefulRestart);
  };

  establishTwoPeersAndTest(
      std::optional<std::function<void()>>(lambdaVerifyGRNotification),
      update1_,
      update2_);
}

// RFC 8538 Sec. 4
TEST_F(FiberBgpPeerFixture, GrNotificationReceivedTest) {
  EXPECT_TRUE(peer2GrOnNotification(notifyMsg1_));
}

// RFC 8538 Sec. 3
TEST_F(FiberBgpPeerFixture, GrNotificationHardResetReceivedTest) {
  EXPECT_FALSE(
      peer2GrOnNotification(buildBgpHardResetNotification(notifyMsg1_)));
}

/**
 * Set up two peers and send a route refresh request from peer1
 * Verify serialization passes and if peer2 is able to receive this
 */
TEST_F(FiberBgpPeerFixture, EnhancedRouteRefreshMessageTest) {
  // Set EnhancedRouteRefreshConfigured to true
  params3_.isEnhancedRouteRefreshConfigured = true;
  params4_.isEnhancedRouteRefreshConfigured = true;

  auto& fm = fmWrapper_.get();
  auto peer1 = std::make_shared<FiberBgpPeer>(
      params3_,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_,
      false /* isRestarting */,
      FLAGS_enable_egress_queue_backpressure);
  auto peer2 = std::make_shared<FiberBgpPeer>(
      params4_,
      fm,
      evb_,
      std::move(*fs1_),
      peer2Input_,
      peer2BoundedInput_,
      peer2Output_,
      false /* isRestarting */,
      FLAGS_enable_egress_queue_backpressure);
  auto lastResetKeepAliveTimer = peer1->getLastResetKeepAliveTimer();
  auto lambdaVerifyEnhancedRouteRefresh = [&]() {
    /*
     * Send a route refresh request from peer1
     * Verify if peer2 is able to receive this
     */
    if (FLAGS_enable_egress_queue_backpressure) {
      ASSERT_TRUE(peer1BoundedInput_->push(routeRefreshMsg1_));
    } else {
      peer1Input_->push(routeRefreshMsg1_);
    }

    auto rcvd_msg =
        facebook::bgp::test::boundedBlockingPop(*peer2Output_, "peer2Output_");
    auto obsvd_msg = peer2->getObserverRcvdMessageQueue().get();

    /*
     * When Route Refresh message was sent, keepalive timer should have
     * reset
     */
    EXPECT_NE(lastResetKeepAliveTimer, peer1->getLastResetKeepAliveTimer());

    // Verify contents of the received message
    ASSERT_TRUE(std::holds_alternative<BgpRouteRefresh>(rcvd_msg));
    auto rcvd_route_refresh_msg = std::get<BgpRouteRefresh>(rcvd_msg);
    EXPECT_EQ(routeRefreshMsg1_, rcvd_route_refresh_msg);

    // Verify contents of the observed message
    ASSERT_TRUE(std::holds_alternative<BgpRouteRefresh>(obsvd_msg->message));
    auto obsvd_route_refresh_msg =
        std::get<BgpRouteRefresh>(obsvd_msg->message);
    EXPECT_EQ(rcvd_route_refresh_msg, obsvd_route_refresh_msg);
  };
  establishTwoPeersAndTest(
      std::optional<std::function<void()>>(lambdaVerifyEnhancedRouteRefresh),
      update1_,
      update2_,
      peer1,
      peer2);
}

/**
 * Establish a basic session and make sure both peers advance to established
 * state. After exchange of one BgpUpdate2, send Route refresh request with
 * wrong length. Verify peer2 receives this and generates a BgpNotification with
 * the right error code.
 */
TEST_F(FiberBgpPeerFixture, EnhancedRouteRefreshErrorAfterUpdatesTest) {
  enhancedRouteRefreshErrorTest(update1_, update2_);
}

/**
 * Establish a basic session and make sure both peers advance to established
 * state. Before exchange of any BgpUpdate2, send Route refresh request with
 * wrong length. Verify peer2 receives this and generates a BgpNotification with
 * the right error code.
 */
TEST_F(FiberBgpPeerFixture, EnhancedRouteRefreshErrorBeforeUpdatesTest) {
  enhancedRouteRefreshErrorTest(std::nullopt, std::nullopt);
}

/*
 * the UTs in parser only verify we are throwing exceptions with the right
 * subCode, this UT is to pick one and verify we are generating the right
 * notification (doing the exception handling) -- BgpOpenMsgException
 */
TEST_F(FiberBgpPeerFixture, WrongHoldTimeOpenMsgErrorNotificationTest) {
  auto& fm = fmWrapper_.get();

  // make an openMsg with invalid holdTime, 2s
  std::vector<uint8_t> openMsg{
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0x00,
      0x3d, // Length of BGP PDU
      0x01, // Bgp message type (OPEN)
      0x04, // BGP Version
      0x80,
      0xa6, // ASN
      0x00,
      0x02, // Hold Time (2 seconds)
      0x01,
      0x02,
      0x03,
      0x04, // BGP ID
      0x20, // Optional Param Length
      // Params
      0x02, // Capabilities
      0x1e, // Length
      // Bgp Capabilities
      0x01,
      0x04, // MP Ext, Length-4
      0x00,
      0x01,
      0x00,
      0x01, // V4 + Unicast
      0x01,
      0x04, // MP Ext, Length-4
      0x00,
      0x02,
      0x00,
      0x04, // V6 + Labelled Unicast
      0x41,
      0x04, // 4 byte ASN
      0x01,
      0x02,
      0x03,
      0x04, // ASN value
      0x40,
      0x0a, // Graceful Restart, Length-10
      0x81,
      0x01, // state = true, time = 257
      0x00,
      0x01,
      0x01,
      0x80, // v4 + Unicast
      0x00,
      0x02,
      0x04,
      0x00, // v6 + Labeled Unicast
  };

  // setup the sockets so we can send the rigged message
  folly::SocketPair sp;
  auto fs0 = std::make_unique<FiberSocket>(
      folly::AsyncSocket::newSocket(
          &evb_, folly::NetworkSocket::fromFd(sp.extractFD0())),
      &evb_);
  auto fs1 = std::make_unique<FiberSocket>(
      folly::AsyncSocket::newSocket(
          &evb_, folly::NetworkSocket::fromFd(sp.extractFD1())),
      &evb_);
  auto peer1 = std::make_shared<MockFiberBgpPeer>(
      params1_,
      fm,
      evb_,
      std::move(*fs0),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  // peer1 get an invalid open message
  fs1->write(
      std::make_unique<folly::IOBuf>(
          folly::IOBuf::wrapBufferAsValue(openMsg.data(), openMsg.size())));

  fm.addTask([&] { peer1->run(); });

  fm.addTask([&] {
    try {
      /*
       * peer2 will get an openMsg from peer1, they all have same size
       * Read whole length of an Open Message (61 Bytes)
       */
      auto buf = fs1->read(openMsg.size()).value();
      auto open = BgpMessageParser2::parseBgpOpenMsgRaw(
          folly::IOBuf::wrapBufferAsValue(buf->data(), buf->length()));

      /*
       * then verified on peer2 side we get a Notification with correct subCode
       * Read minimum length of a Notification (21 Bytes)
       */
      buf = fs1->read(21).value();
      auto notification = BgpMessageParser2::parseBgpNotificationRaw(
          folly::IOBuf::wrapBufferAsValue(buf->data(), buf->length()));

      EXPECT_EQ(BgpNotifErrCode::BN_OPEN_MSG_ERR, *notification.errCode());
      EXPECT_EQ(
          static_cast<uint16_t>(
              BgpNotifOpenMsgErrSubCode::BN_OM_UNACCEPTABLE_HOLD_TIME),
          *notification.errSubCode());
    } catch (...) {
      /*
       * any reading exception means we did't receive those messages in the
       * correct order, UT failed
       */
      FAIL();
    }
    peer1->stop();
  });

  evb_.loop();
}

class BgpOpenMsgAsnFieldFixture
    : public FiberBgpPeerFixture,
      public ::testing::WithParamInterface<std::pair<uint32_t, uint32_t>> {};

INSTANTIATE_TEST_CASE_P(
    BgpOpenMsgAsnFieldFixtureInstance,
    BgpOpenMsgAsnFieldFixture,
    ::testing::Values(
        // local_asn, expected_asn_in_the_open_msg
        std::make_pair<uint32_t, uint32_t>(4200000001, 23456), // non-mappable
        std::make_pair<uint32_t, uint32_t>(65535, 65535), // 2 bytes asn limit
        std::make_pair<uint32_t, uint32_t>(65536, 23456), // non-mappable (edge)
        std::make_pair<uint32_t, uint32_t>(1234, 1234))); // mappable

/*
 * rfc4893
 * Open Msg's ASN field should be a placeholder (23456) only if the actual ASN
 * is non-mappable (greater than the limit of 2 bytes ASN)
 */
TEST_P(BgpOpenMsgAsnFieldFixture, OpenMsgAsFieldValue) {
  constexpr size_t openMsgLength{61};
  const auto& param = GetParam();
  const auto& peer1LocalAs = std::get<0>(param);
  const auto& expectedValue = std::get<1>(param);
  auto params1 = params1_;
  params1.localAs = peer1LocalAs;

  auto& fm = fmWrapper_.get();

  folly::SocketPair sp;
  auto fs0 = std::make_unique<FiberSocket>(
      folly::AsyncSocket::newSocket(
          &evb_, folly::NetworkSocket::fromFd(sp.extractFD0())),
      &evb_);

  auto fs1 = std::make_unique<FiberSocket>(
      folly::AsyncSocket::newSocket(
          &evb_, folly::NetworkSocket::fromFd(sp.extractFD1())),
      &evb_);

  auto peer1 = std::make_shared<MockFiberBgpPeer>(
      params1,
      fm,
      evb_,
      std::move(*fs0),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  fm.addTask([&] { peer1->run(); });
  fm.addTask([&] {
    try {
      auto buf = fs1->read(openMsgLength).value();
      auto open = BgpMessageParser2::parseBgpOpenMsgRaw(
          folly::IOBuf::wrapBufferAsValue(buf->data(), buf->length()));
      EXPECT_EQ(expectedValue, *open.asn());
    } catch (...) {
      /*
       * any reading exception means we did't receive those messages in the
       * correct order, UT failed
       */
      FAIL();
    }
    peer1->stop();
  });

  evb_.loop();
}

/*
 * the UTs in parser only verified we are throwing exceptions with the right
 * subCode, this UT is to pick one and verified we are generating the right
 * notification (doing the exception handling) -- BgpHeaderException
 */
TEST_F(FiberBgpPeerFixture, WrongHeaderLengthNotificationTest) {
  auto& fm = fmWrapper_.get();

  // make an header message
  std::vector<uint8_t> hdrMsg = {
      // clang-format off
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0x00, 0x10, // Length of BGP PDU
      0x01, // Bgp message type (OPEN)
      0x01, // pretend we have content to pass the check
      // clang-format on
  };

  // setup the sockets so we can send the rigged message
  folly::SocketPair sp;
  auto fs0 = std::make_unique<FiberSocket>(
      folly::AsyncSocket::newSocket(
          &evb_, folly::NetworkSocket::fromFd(sp.extractFD0())),
      &evb_);
  auto fs1 = std::make_unique<FiberSocket>(
      folly::AsyncSocket::newSocket(
          &evb_, folly::NetworkSocket::fromFd(sp.extractFD1())),
      &evb_);
  auto peer1 = std::make_shared<MockFiberBgpPeer>(
      params1_,
      fm,
      evb_,
      std::move(*fs0),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  // peer1 get an invalid open message with wrong header length
  fs1->write(
      std::make_unique<folly::IOBuf>(
          folly::IOBuf::wrapBufferAsValue(hdrMsg.data(), hdrMsg.size())));

  fm.addTask([&] { peer1->run(); });

  fm.addTask([&] {
    try {
      /*
       * peer2 will get an openMsg from peer1, they all have same size
       * Read whole length of an Open Message (61 Bytes)
       */
      auto buf = fs1->read(61).value();
      auto open = BgpMessageParser2::parseBgpOpenMsgRaw(
          folly::IOBuf::wrapBufferAsValue(buf->data(), buf->length()));

      /*
       * then verified on peer2 side we get a Notification with correct subCode
       * Read minimum length of a Notification (21 Bytes) + 2 byte of data feild
       */
      buf = fs1->read(23).value();
      auto notification = BgpMessageParser2::parseBgpNotificationRaw(
          folly::IOBuf::wrapBufferAsValue(buf->data(), buf->length()));

      EXPECT_EQ(BgpNotifErrCode::BN_MSG_HDR_ERR, *notification.errCode());
      EXPECT_EQ(
          static_cast<uint16_t>(BgpNotifMsgHdrErrSubCode::BN_MH_BAD_MSG_LEN),
          *notification.errSubCode());
      // this will be a short so should be of size 2
      EXPECT_EQ(notification.data()->size(), 2);
      /*
       * also verify it equals 4096 in hex is 1000 -> flip 0010,
       * 16 in hex is 10 -> flip 0010
       */
      auto errData = htons(16);
      EXPECT_EQ(
          *notification.data(),
          std::string(
              reinterpret_cast<const char*>(&errData), sizeof(errData)));
    } catch (...) {
      /*
       * any reading exception means we did't receive those messages in the
       * correct order, UT failed
       */
      FAIL();
    }
    peer1->stop();
  });

  evb_.loop();
}

// verify that we can skip checking diffferent ASN
TEST_F(FiberBgpPeerFixture, ValidateRemoteAsTest) {
  auto& fm = fmWrapper_.get();
  /*
   * create 3 peers
   * peer1 in AS 1234 and validateRemoteAs = true
   * peer2 in AS 2345 and validateRemoteAs = false
   */
  PeeringParams params2{
      folly::IPAddress("2.2.2.2"), // peerAddr
      std::nullopt, // peerPrefix
      2345, // globalAs
      2345, // localAs
      2345, // remoteAs
      IPAddressV4("2.2.2.2"), // routerId
      std::chrono::seconds(180), // holdTime
      std::chrono::seconds(120), // grRestartTime
      AdvertiseLinkBandwidth::DISABLE, // advertiseLinkBandwidth
      ReceiveLinkBandwidth::ACCEPT, // receiveLinkBandwidth
      std::nullopt, // linkBandwidthBps
      facebook::bgp::ValidateRemoteAs{false}};
  // peer3 in AS 3456 and validateRemoteAs = false,
  PeeringParams params3{
      folly::IPAddress("3.3.3.3"), // peerAddr
      std::nullopt, // peerPrefix
      3456, // globalAs
      3456, // localAs
      3456, // remoteAs
      IPAddressV4("3.3.3.3"), // routerId
      std::chrono::seconds(3), // holdTime
      std::chrono::seconds(10), // grRestartTime
      AdvertiseLinkBandwidth::DISABLE, // advertiseLinkBandwidth
      ReceiveLinkBandwidth::ACCEPT, // receiveLinkBandwidth
      std::nullopt, // linkBandwidthBps
      facebook::bgp::ValidateRemoteAs{false}};

  // verified that peer1 & peer2 cannot connect
  fm.addTask([&] {
    std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
    std::tie(peer1, peer2) = startTwoPeers(params1_, params2);

    // wait till session comes up, but it won't come up
    EXPECT_NE(
        runUntilTargetStateOrTimeout(
            peer1, BgpSessionState::ESTABLISHED, 100ms),
        0);
    EXPECT_NE(
        runUntilTargetStateOrTimeout(
            peer2, BgpSessionState::ESTABLISHED, 100ms),
        0);

    ASSERT_EQ(peer1->getValidateRemoteAs(), true);
    ASSERT_EQ(peer2->getValidateRemoteAs(), false);

    {
      /*
       * becasue peer1 is verifiying asn, it will send a BN_OPEN_MSG_ERR
       * notification and close socket after the notification is out
       */
      auto msg = facebook::bgp::test::boundedBlockingPop(
          *peer2Output_, "peer2Output_");
      /*
       * On peer2 side, we should get NOTIFICATION and set GR=False.  However,
       * due to race condition we sometimes get Socket Error, and set GR=True.
       */
      ASSERT_TRUE(std::holds_alternative<FiberBgpPeer::BgpSessionStop>(msg));
      // Uncomment below two lines if we can figure out reason for race
      // auto sessionStop = std::get<FiberBgpPeer::BgpSessionStop>(msg);
      // condition EXPECT_EQ(false, sessionStop.gracefulRestart);
    }
    {
      // check to see if peer1 is also chock on BgpSessionError
      auto msg = facebook::bgp::test::boundedBlockingPop(
          *peer1Output_, "peer1Output_");
      ASSERT_TRUE(std::holds_alternative<FiberBgpPeer::BgpSessionStop>(msg));
      // Uncomment below two lines if we can figure out reason for race
      // auto sessionStop = std::get<FiberBgpPeer::BgpSessionStop>(msg);
      // EXPECT_EQ(false, sessionStop.gracefulRestart);
    }
    stopTwoPeers(peer1, peer2);
  });

  // verified that peer2 & peer3 can connect
  fm.addTask([&] {
    std::shared_ptr<MockFiberBgpPeer> peer2, peer3;
    std::tie(peer2, peer3) = startTwoPeers(params2, params3);

    // wait till session comes up
    EXPECT_EQ(
        runUntilTargetStateOrTimeout(
            peer2, BgpSessionState::ESTABLISHED, 100ms),
        0);
    EXPECT_EQ(
        runUntilTargetStateOrTimeout(
            peer3, BgpSessionState::ESTABLISHED, 100ms),
        0);

    ASSERT_EQ(peer2->getValidateRemoteAs(), false);
    ASSERT_EQ(peer3->getValidateRemoteAs(), false);
    EXPECT_EQ(BgpSessionState::ESTABLISHED, peer2->getBgpSessionState());
    EXPECT_EQ(BgpSessionState::ESTABLISHED, peer3->getBgpSessionState());
    EXPECT_EQ(params3.localAs, peer2->getRemoteAs());
    EXPECT_EQ(params2.localAs, peer3->getRemoteAs());
    stopTwoPeers(peer2, peer3);
  });

  evb_.loop();
  SUCCEED();
}

TEST_F(FiberBgpPeerFixture, AdditionalRemoteAsEstablishesSessionTest) {
  auto acceptingPeerParams = params1_;
  acceptingPeerParams.remoteAs = 1234;
  acceptingPeerParams.additionalRemoteAs = params2_.localAs;

  auto& fm = fmWrapper_.get();
  fm.addTask([&] {
    std::shared_ptr<MockFiberBgpPeer> acceptingPeer, migratingPeer;
    std::tie(acceptingPeer, migratingPeer) =
        startTwoPeers(acceptingPeerParams, params2_);

    EXPECT_EQ(
        0,
        runUntilTargetStateOrTimeout(
            acceptingPeer, BgpSessionState::ESTABLISHED, 1s));
    EXPECT_EQ(
        0,
        runUntilTargetStateOrTimeout(
            migratingPeer, BgpSessionState::ESTABLISHED, 1s));
    EXPECT_EQ(params2_.localAs, acceptingPeer->getRemoteAs());
    EXPECT_EQ(acceptingPeerParams.localAs, migratingPeer->getRemoteAs());

    stopTwoPeers(acceptingPeer, migratingPeer);
  });

  evb_.loop();
}

TEST_F(FiberBgpPeerFixture, RemoteAsOutsideAcceptedSetSendsBadPeerAsTest) {
  gflags::FlagSaver flags;
  FLAGS_enable_egress_queue_backpressure = false;

  auto acceptingPeerParams = params1_;
  acceptingPeerParams.remoteAs = 1234;
  acceptingPeerParams.additionalRemoteAs = 2345;

  auto& fm = fmWrapper_.get();
  auto acceptingPeer = std::make_shared<MockFiberBgpPeer>(
      acceptingPeerParams,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  bgp::BgpStats::initCounters();
  auto counters = fb303::ThreadCachedServiceData::getShared();
  counters->publishStats();
  const std::string asnMismatchSum60Key =
      bgp::BgpStats::kOpenRejectAsnMismatch + ".sum.60";
  const auto initialAsnMismatchCount =
      counters->hasCounter(bgp::BgpStats::kOpenRejectAsnMismatch)
      ? counters->getCounter(bgp::BgpStats::kOpenRejectAsnMismatch)
      : 0;
  const auto initialAsnMismatchCount60 =
      counters->hasCounter(asnMismatchSum60Key)
      ? counters->getCounter(asnMismatchSum60Key)
      : 0;
  const auto response = processOpenAndGetResponse(
      acceptingPeer, params2_.localAs, /*as4Byte=*/true);
  ASSERT_TRUE(response.has_value());
  ASSERT_TRUE(std::holds_alternative<BgpNotification>(*response));
  const auto& notification = std::get<BgpNotification>(*response);
  EXPECT_EQ(BgpNotifErrCode::BN_OPEN_MSG_ERR, *notification.errCode());
  EXPECT_EQ(
      static_cast<uint16_t>(BgpNotifOpenMsgErrSubCode::BN_OM_BAD_PEER_AS),
      *notification.errSubCode());
  counters->publishStats();
  EXPECT_EQ(
      initialAsnMismatchCount + 1,
      counters->getCounter(bgp::BgpStats::kOpenRejectAsnMismatch));
  EXPECT_EQ(
      initialAsnMismatchCount60 + 1, counters->getCounter(asnMismatchSum60Key));
}

class BgpPeerAsnCapabilityFixture
    : public FiberBgpPeerFixture,
      public ::testing::WithParamInterface<std::pair<uint32_t, bool>> {};

INSTANTIATE_TEST_CASE_P(
    BgpPeerAsnCapabilityInstance,
    BgpPeerAsnCapabilityFixture,
    ::testing::Values(
        std::make_pair<uint32_t, bool>(2345, false),
        std::make_pair<uint32_t, bool>(4200000002, true)));

TEST_P(BgpPeerAsnCapabilityFixture, AcceptedRemoteAsEstablishesSessionTest) {
  gflags::FlagSaver flags;
  FLAGS_enable_egress_queue_backpressure = false;

  const auto& [remoteAs, as4Byte] = GetParam();
  auto acceptingPeerParams = params1_;
  acceptingPeerParams.remoteAs = 1234;
  acceptingPeerParams.additionalRemoteAs = remoteAs;
  /* Avoid scheduling timers while driving the FSM synchronously. */
  acceptingPeerParams.holdTime = 0s;

  auto& fm = fmWrapper_.get();
  auto acceptingPeer = std::make_shared<MockFiberBgpPeer>(
      acceptingPeerParams,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  const auto response =
      processOpenAndGetResponse(acceptingPeer, remoteAs, as4Byte);
  ASSERT_TRUE(response.has_value());
  ASSERT_TRUE(std::holds_alternative<BgpKeepAlive>(*response));
  EXPECT_EQ(BgpSessionState::OPEN_CONFIRM, acceptingPeer->getBgpSessionState());
  EXPECT_EQ(remoteAs, acceptingPeer->getRemoteAs());
  EXPECT_EQ(as4Byte, *acceptingPeer->getRemoteCapabilities().as4byte());
  EXPECT_EQ(as4Byte, *acceptingPeer->getNegotiatedCapabilities().as4byte());

  processKeepAlive(acceptingPeer);
  EXPECT_EQ(BgpSessionState::ESTABLISHED, acceptingPeer->getBgpSessionState());
}

/*
 * RFC 8538 Sec. 4: on an ESTABLISHED session with the Graceful Restart and
 * Notification (N) bits negotiated, a received NOTIFICATION other than
 * Cease/Hard Reset is eligible for graceful restart
 */
TEST_F(FiberBgpPeerFixture, GrNotificationEligibleTest) {
  auto params = params1_;
  /* Avoid scheduling timers while driving the FSM synchronously. */
  params.holdTime = 0s;

  auto& fm = fmWrapper_.get();
  auto peer = std::make_shared<MockFiberBgpPeer>(
      params,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  processOpenAndGetResponse(peer, params.remoteAs, true);
  ASSERT_EQ(BgpSessionState::OPEN_CONFIRM, peer->getBgpSessionState());
  ASSERT_TRUE(*peer->getNegotiatedCapabilities().gracefulRestart());
  setNegotiatedGrNotification(peer);
  processKeepAlive(peer);
  ASSERT_EQ(BgpSessionState::ESTABLISHED, peer->getBgpSessionState());

  EXPECT_TRUE(processNotificationError(peer, notifyMsg1_));
  EXPECT_EQ(ResetReason::NOTIFICATION_RCVD, peer->getResetReason());
}

TEST_F(FiberBgpPeerFixture, SendNotificationWhenHoldTimerExpiredTest) {
  auto& fm = fmWrapper_.get();
  fm.addTask([&] {
    std::shared_ptr<MockFiberBgpPeer> peer1, peer2;
    std::tie(peer1, peer2) = startTwoPeers(params3_, params4_);
    // wait till session comes up
    EXPECT_EQ(
        runUntilTargetStateOrTimeout(
            peer1, BgpSessionState::ESTABLISHED, 100ms),
        0);
    EXPECT_EQ(
        runUntilTargetStateOrTimeout(
            peer2, BgpSessionState::ESTABLISHED, 100ms),
        0);

    // stop peer2 from sending keepAlive
    peer2->keepAliveTimer_.reset();

    {
      /*
       * now peer1's holdtimer for peer2 will expire, it will sent a
       * BN_HOLD_TIMER_EXPIRED notification to peer2
       * validate peer2 get a notification
       */
      auto msg = facebook::bgp::test::boundedBlockingPop(
          *peer2Output_, "peer2Output_");
      /*
       * on peer2 side we are going to get a BgpSessionStop with gr = ture
       * becasue we still have keepAlive following this notification message,
       * we actually stopped by the notification error instead of socket error
       */
      ASSERT_TRUE(std::holds_alternative<FiberBgpPeer::BgpSessionStop>(msg));
      auto sessionStop = std::get<FiberBgpPeer::BgpSessionStop>(msg);
      EXPECT_EQ(false, sessionStop.gracefulRestart);
    }
    {
      /*
       * Check peer1 does a non-graceful stop. For holdtime expire we shouldn't
       * do GR to avoid blackholing of traffic.
       */
      auto msg = facebook::bgp::test::boundedBlockingPop(
          *peer1Output_, "peer1Output_");
      ASSERT_TRUE(std::holds_alternative<FiberBgpPeer::BgpSessionStop>(msg));
      auto sessionStop = std::get<FiberBgpPeer::BgpSessionStop>(msg);
      EXPECT_EQ(false, sessionStop.gracefulRestart);
    }

    stopTwoPeers(peer1, peer2);
    EXPECT_EQ(ResetReason::HOLD_TIMER_EXPIRE, peer1->getResetReason());
    EXPECT_EQ(ResetReason::NOTIFICATION_RCVD, peer2->getResetReason());
  });

  evb_.loop();
  SUCCEED();
}

TEST_F(FiberBgpPeerFixture, NeedToKeepThisPeerTest) {
  PeeringParams params1{
      folly::IPAddress("1.1.1.1"), // peerAddr
      std::nullopt, // peerPrefix
      4200000001, // globalAs
      4200000001, // localAs
      4200000002, // remoteAs
      IPAddressV4("2.2.2.2"), // routerId
      std::chrono::seconds(180), // holdTime
      std::chrono::seconds(120) // grRestartTime
  };
  PeeringParams params2{
      folly::IPAddress("1.1.1.1"), // peerAddr
      std::nullopt, // peerPrefix
      4200000001, // globalAs
      4200000001, // localAs
      4200000002, // remoteAs
      IPAddressV4("0.0.0.0"), // routerId
      std::chrono::seconds(180), // holdTime
      std::chrono::seconds(120) // grRestartTime
  };
  /*
   * [Test case 1]
   * @condition 1: local port == socket address port
   * @condition 2: local bgpId > remote BgpId(the default remoteBgpId is 0)
   * @return: false
   */
  {
    auto localListenAddress =
        folly::SocketAddress("::1", nettools::bgplib::constants::kBgpPort);
    auto& fm = fmWrapper_.get();
    auto peer = std::make_shared<MockFiberBgpPeer>(
        /*
         * ATTN: local address from params is not used here since
         * getLocalSocketAddress() is overridden in MockFiberBgpPeer
         */
        params1,
        fm,
        evb_,
        std::move(*fs0_),
        peer1Input_,
        peer1BoundedInput_,
        peer1Output_);
    EXPECT_FALSE(
        FiberBgpPeerManager::needToKeepThisPeer(localListenAddress, peer));
  }

  /*
   * [Test case 2]
   * @condition 1: local port != socket address port
   * @condition 2: local bgpId > remote BgpId(the default remoteBgpId is 0)
   * @return: true
   */
  {
    auto localListenAddress =
        folly::SocketAddress("::1", nettools::bgplib::constants::kBgpPort + 1);
    auto& fm = fmWrapper_.get();
    auto peer = std::make_shared<MockFiberBgpPeer>(
        /*
         * ATTN: local address from params is not used here since
         * getLocalSocketAddress() is overridden in MockFiberBgpPeer
         */
        params1,
        fm,
        evb_,
        std::move(*fs0_),
        peer1Input_,
        peer1BoundedInput_,
        peer1Output_);
    EXPECT_TRUE(
        FiberBgpPeerManager::needToKeepThisPeer(localListenAddress, peer));
  }

  /*
   * [Test case 3]
   * @condition 1: local port == socket address port
   * @condition 2: local bgpId == remote BgpId(the default remoteBgpId is 0)
   * @return: true
   */
  {
    auto localListenAddress =
        folly::SocketAddress("::1", nettools::bgplib::constants::kBgpPort);
    auto& fm = fmWrapper_.get();
    auto peer = std::make_shared<MockFiberBgpPeer>(
        /*
         * ATTN: local address from params is not used here since
         * getLocalSocketAddress() is overridden in MockFiberBgpPeer
         */
        params2,
        fm,
        evb_,
        std::move(*fs0_),
        peer1Input_,
        peer1BoundedInput_,
        peer1Output_);
    EXPECT_TRUE(
        FiberBgpPeerManager::needToKeepThisPeer(localListenAddress, peer));
  }

  /*
   * [Test case 4]
   * @condition 1: local port != socket address port
   * @condition 2: local bgpId == remote BgpId(the default remoteBgpId is 0)
   * @return: false
   */
  {
    auto localListenAddress =
        folly::SocketAddress("::1", nettools::bgplib::constants::kBgpPort + 1);
    auto& fm = fmWrapper_.get();
    auto peer = std::make_shared<MockFiberBgpPeer>(
        /*
         * ATTN: local address from params is not used here since
         * getLocalSocketAddress() is overridden in MockFiberBgpPeer
         */
        params2,
        fm,
        evb_,
        std::move(*fs0_),
        peer1Input_,
        peer1BoundedInput_,
        peer1Output_);
    EXPECT_FALSE(
        FiberBgpPeerManager::needToKeepThisPeer(localListenAddress, peer));
  }
}

TEST_F(FiberBgpPeerFixture, SocketCloseTimerTest) {
  PeeringParams params{
      folly::IPAddress("1.1.1.1"), // peerAddr
      std::nullopt, // peerPrefix
      4200000001, // globalAs
      4200000001, // localAs
      4200000002, // remoteAs
      IPAddressV4("2.2.2.2"), // routerId
      std::chrono::seconds(180), // holdTime
      std::chrono::seconds(120) // grRestartTime
  };
  auto& fm = fmWrapper_.get();
  auto peer = std::make_shared<MockFiberBgpPeer>(
      /*
       * ATTN: local address from params is not used here since
       * getLocalSocketAddress() is overridden in MockFiberBgpPeer
       */
      params,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  // Timer is not scheduled when FiberBgpPeer is initialized
  EXPECT_FALSE(peer->socketCloseTimer_->isScheduled());

  // Destruct FiberBgpPeer explicitly to make sure no unexpected crash seen
  peer.reset();
}

TEST(MonitoredQueueTest, RcvdQueueTest) {
  bgp::MonitoredRWQueue<std::optional<folly::Try<FiberBgpParser::BgpMessageT>>>
      testQueue;

  auto msg = FiberGenericSocketError(
      FiberGenericSocketErrorType::UNKNOWN, "Test error");
  testQueue.put(folly::Try<FiberBgpParser::BgpMessageT>(msg));
  testQueue.putNull();

  EXPECT_EQ(2, testQueue.size());
}

TEST_F(FiberBgpPeerFixture, MonitoredQueueTest) {
  auto& fm = fmWrapper_.get();
  auto peer = std::make_shared<MockFiberBgpPeer>(
      params1_,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  if (FLAGS_enable_egress_queue_backpressure) {
    EXPECT_EQ(
        &std::get<std::reference_wrapper<bgp::MonitoredQueueBase>>(
             peer->monitoredItems_.rlock()->at(bgp::kQueueNameSocketOut))
             .get(),
        &(peer->boundedSendQueue_));
    EXPECT_EQ(
        &std::get<std::reference_wrapper<bgp::MonitoredQueueBase>>(
             peer->monitoredItems_.rlock()->at(bgp::kQueueNameAdjRibOut))
             .get(),
        peer->boundedIqueue_.get());
  } else {
    EXPECT_EQ(
        &std::get<std::reference_wrapper<bgp::MonitoredQueueBase>>(
             peer->monitoredItems_.rlock()->at(bgp::kQueueNameSocketOut))
             .get(),
        &(peer->sendQueue_));
    EXPECT_EQ(
        &std::get<std::reference_wrapper<bgp::MonitoredQueueBase>>(
             peer->monitoredItems_.rlock()->at(bgp::kQueueNameAdjRibOut))
             .get(),
        peer->iqueue_.get());
  }
  /*
   * Attention:
   *  - *peer1->oqueue will unpack the std::optional<> wrapper.
   *  - &(...) will take the address of the queue for ptr comparison.
   */
  EXPECT_EQ(
      &std::get<std::reference_wrapper<bgp::MonitoredQueueBase>>(
           peer->monitoredItems_.rlock()->at(bgp::kQueueNameAdjRibIn))
           .get(),
      &(*peer->oqueue_));
  EXPECT_EQ(
      &std::get<std::reference_wrapper<bgp::MonitoredQueueBase>>(
           peer->monitoredItems_.rlock()->at(bgp::kQueueNameParserOut))
           .get(),
      &(peer->rcvdQueue_));
}

TEST_F(FiberBgpPeerFixture, SocketSendLoopYieldTest) {
  gflags::FlagSaver flags;
  /*
   * There is no way to put a null front to the bounded queue, so this
   * test mechanism will not pass for backpressure enabled scenario.
   */
  FLAGS_enable_egress_queue_backpressure = false;
  std::vector<folly::Future<folly::Unit>> workers;

  auto& fm = fmWrapper_.get();
  auto peer = std::make_shared<MockFiberBgpPeer>(
      params1_,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  /*
   * Inject kMsgBatchSizeToYield + 1 message to the queue to make sure
   * fiber task can yield after processing kMsgBatchSizeToYield messages.
   */
  for (uint16_t i = 0; i < kMsgBatchSizeToYield + 1; ++i) {
    peer->sendQueue_.put(BgpKeepAlive{});
  }

  {
    /*
     * Fiber task to keep processing messages to sendQueue_. It will not closed
     * until a null message it injected from the other fiber task.
     */
    auto fiber = fm.addTaskFuture([&]() { peer->sendSocketLoop(); });
    workers.emplace_back(std::move(fiber));
  }
  {
    /*
     * Fiber task to conditionally queue termination signal. This signal will
     * be sent only when the other fiber started processing messages, aka,
     * monitored by checking the counter.
     *
     * If fiber does not yield in the middle, but processes all messages in the
     * queue, the counter check will fail.
     */
    auto fiber = fm.addTaskFuture([&]() {
      const auto key = fmt::format(
          "peer.messagesSent.{}.count",
          facebook::bgp::PeerStats::kMessagesSentKeepAlive);

      while (true) {
        facebook::fb303::ThreadCachedServiceData::get()->publishStats();
        auto counters = facebook::fb303::ThreadCachedServiceData::getShared();

        if (counters->hasCounter(key)) {
          // stop the other fiber task are processed
          peer->sendQueue_.putNullFront();

          /*
           * make sure fiber yield and processed kMsgBatchSizeToYield_
           * messages with a yielding.
           */
          EXPECT_EQ(counters->getCounter(key), kMsgBatchSizeToYield);
          break;
        }
        fiberSleepFor(1ms);
      }
    });
    workers.emplace_back(std::move(fiber));
  }

  evb_.loop();

  // NOTE: here means asyncScope_ has no remaining tasks.
  folly::collectAll(workers.begin(), workers.end()).get();
}

TEST_F(FiberBgpPeerFixture, ProcessEgressLoopTerminationTest) {
  std::vector<folly::Future<folly::Unit>> workers;

  auto& fm = fmWrapper_.get();
  auto peer = std::make_shared<MockFiberBgpPeer>(
      params1_,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  for (uint16_t i = 0; i < kMsgBatchSizeToYield + 1; ++i) {
    peer1Input_->push(BgpEndOfRib{});
  }

  /*
   * Coro task to keep processing messages from iqueue_. It will not close
   * until task is cancelled.
   */
  peer->asyncScope_.add(
      co_withExecutor(&evb_, peer->processEgressBgpMessageLoop()));

  {
    auto fiber = fm.addTaskFuture([&]() {
      while (peer1Input_->size()) {
        // yield fiber task
        fiberSleepFor(1ms);
      }
      // enforce for all coro tasks to complete.
      folly::coro::blockingWait(peer->asyncScope_.cancelAndJoinAsync());
    });
    workers.emplace_back(std::move(fiber));
  }

  evb_.loop();
  folly::collectAll(workers.begin(), workers.end()).get();
}

TEST_F(FiberBgpPeerFixture, ProcessEgressLoopWithBackpressureTerminationTest) {
  std::vector<folly::Future<folly::Unit>> workers;

  auto& fm = fmWrapper_.get();
  auto peer = std::make_shared<MockFiberBgpPeer>(
      params1_,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  for (uint16_t i = 0; i < kEgressQueueHighWatermark; ++i) {
    peer1BoundedInput_->push(BgpEndOfRib{});
  }

  /*
   * Coro task to keep processing messages from boundedIqueue_. It will not
   * close until task is cancelled.
   */
  peer->asyncScope_.add(co_withExecutor(
      &evb_, peer->processEgressBgpMessageLoopWithBackpressure()));

  {
    auto fiber = fm.addTaskFuture([&]() {
      while (peer1BoundedInput_->size()) {
        // yield fiber task
        fiberSleepFor(1ms);
      }
      // enforce for all coro tasks to complete.
      folly::coro::blockingWait(peer->asyncScope_.cancelAndJoinAsync());
    });
    workers.emplace_back(std::move(fiber));
  }

  evb_.loop();
  folly::collectAll(workers.begin(), workers.end()).get();
}

TEST_F(FiberBgpPeerFixture, ProcessIngressLoopTerminationTest) {
  std::vector<folly::Future<folly::Unit>> workers;
  std::vector<folly::Future<folly::coro::Task<void>>> results;

  auto& fm = fmWrapper_.get();
  auto peer = std::make_shared<MockFiberBgpPeer>(
      params1_,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  // Add a task to the async scope
  peer->asyncScope_.add(
      co_withExecutor(&evb_, peer->processIngressBgpMessageLoop()));

  {
    auto fiber = fm.addTaskFuture([&]() {
      EXPECT_EQ(1, peer->asyncScope_.remaining());

      // explicitly push signal to close coro task
      peer->rcvdQueue_.fiberPush(std::nullopt);
    });
    workers.emplace_back(std::move(fiber));
  }

  {
    auto fiber = fm.addTaskFuture([&]() {
      while (peer->asyncScope_.remaining() > 0) {
        // folly::fibers::yield() won't be able to yield but rerun this task.
        fiberSleepFor(std::chrono::milliseconds(100));
      }
    });
    workers.emplace_back(std::move(fiber));
  }
  evb_.loop();

  // enforce all fiber tasks to complete
  folly::collectAll(workers.begin(), workers.end()).get();

  // enforce for all coro tasks to complete.
  folly::coro::blockingWait(peer->asyncScope_.cancelAndJoinAsync());
}

TEST(FiberBgpPeerTest, GetObservableSessionInfoTest) {
  BgpPeerDisplayInfo mockInfo;
  mockInfo.remoteBgpId = 1234;
  auto iQueue = std::make_shared<FiberBgpPeer::InputQueueT>();
  auto boundedIqueue = std::make_shared<FiberBgpPeer::BoundedInputQueueT>(
      kMaxEgressQueueSize, kEgressQueueHighWatermark, kEgressQueueLowWatermark);

  auto oQueue =
      std::make_shared<FiberBgpPeer::OutputQueueT>(kMaxIngressQueueSize);
  auto versionNumber = std::make_shared<VersionNumber>(42);

  // inject some value into the queue and make sure it can be retrieved
  iQueue->push(BgpEndOfRib{});
  auto sessionInfo = FiberBgpPeer::getObservableSessionInfo(
      mockInfo, iQueue, boundedIqueue, oQueue, versionNumber);

  EXPECT_TRUE(sessionInfo->peerInfo.has_value());
  EXPECT_EQ(sessionInfo->peerInfo.value().remoteBgpId, 1234);

  EXPECT_NE(sessionInfo->inputQueue, nullptr);
  EXPECT_EQ(1, sessionInfo->inputQueue->size());

  EXPECT_NE(sessionInfo->outputQueue, nullptr);
  EXPECT_EQ(0, sessionInfo->outputQueue->size());

  EXPECT_EQ(sessionInfo->currentVersion->get(), 42);
}

class EgressBackpressureFixture : public FiberBgpPeerFixture {
 public:
  template <typename T>
  void FillQueueToSize(bgp::MonitoredMPMCWQueue<T>& queue, int sz) {
    for (int i = 0; i < sz; ++i) {
      /* Pad the queue with BgpUpdates. */
      queue.push(update1_);
    }
  }
};

/**
 * Verifies scenario where both boundedIqueue_ and boundedSendQueue_ enter
 * write-blocked state due to socket backpressure.
 *
 * This scenario can occur when:
 *   1. Socket buffer fills and we cannot write.
 *   2. SendSocketLoop yields because socket is not accepting writes.
 *   3. boundedSendQueue_ is not drained because SendSocketLoop yielded,
 *      while processEgressBgpMessageLoopWithBackpressure continues to push.
 *   4. boundedSendQueue_ reaches highWm and blocks writes.
 *   5. processEgressBgpMessageLoopWithBackpressure yields and stops
 *      reading from boundedIqueue_.
 *   6. boundedIqueue_ eventually reaches highWm and blocks writes.
 *
 * This test fills both queues with the same message @update1_ and verifies
 * both boundedSendQueue_ and boundedIqueue_ are in blocked state.
 * (1) and (2) is simulated by simply not reading from boundedSendQueue_.
 *
 * Then once both queues are blocked, it attempts to write std::nullopt.
 * We verify when draining the queue that we only see @update1_ messages
 * across both queues, and the final termination signal std::nullopt.
 */
TEST_F(EgressBackpressureFixture, FillAndDrainAllBoundedQueuesTest) {
  std::vector<folly::Future<folly::Unit>> workers;

  auto& fm = fmWrapper_.get();

  auto peer = std::make_shared<MockFiberBgpPeer>(
      params1_,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  /* Record initial sendQueue block stats before backpressure */
  auto initialBlocks = peer->getSendQueueBlocks();
  auto initialTotalBlockDuration = peer->getSendQueueTotalBlockDuration();
  auto initialLastBlockTime = peer->getLastSendQueueBlockTime();

  /* Start the coroutine that will transfer messages from boundedIqueue_ to
   * boundedSendQueue
   */
  peer->asyncScope_.add(co_withExecutor(
      &evb_, peer->processEgressBgpMessageLoopWithBackpressure()));

  auto fiber = fm.addTaskFuture([&]() {
    /* Incrementally add messages until both queues are blocked
     * We need to coordinate with the coroutine processing
     */
    while (!peer->boundedIqueue_->isBlocked() ||
           !peer->boundedSendQueue_.isBlocked()) {
      peer->boundedIqueue_->push(update1_);
      fiberSleepFor(1ms);
    }

    /* Verify both queues are blocked and at high watermark. */
    EXPECT_TRUE(peer->boundedIqueue_->isBlocked());
    EXPECT_TRUE(peer->boundedSendQueue_.isBlocked());
    EXPECT_EQ(kEgressQueueHighWatermark, peer->boundedIqueue_->size());
    EXPECT_EQ(kEgressQueueHighWatermark, peer->boundedSendQueue_.size());

    /* Send termination signal to stop the coroutine. Since we are
     * only at the high watermark there is guaranteed space.
     */
    EXPECT_TRUE(peer->boundedIqueue_->push(std::nullopt));
    EXPECT_EQ(kEgressQueueHighWatermark + 1, peer->boundedIqueue_->size());
    /*
     * At this point, the boundedSendQueue_ is at the high wm,
     * and the boundedIqueue_ is 1 above the high wm.
     * However, there is one additional message that we haven't accounted
     * for, which is the message we popped before checking
     * that boundedSendQueue_ is blocked.
     */
    int messagesRemaining = kEgressQueueHighWatermark * 2 + 1 + 1;

    /* Now start draining boundedSendQueue_ to unblock the system
     * Continue until both queues are empty.
     */
    while (!peer->boundedIqueue_->empty() || !peer->boundedSendQueue_.empty()) {
      /* get waits for a message to be available. */
      auto msg = peer->boundedSendQueue_.get();
      if (msg) {
        EXPECT_EQ(update1_, std::get<std::shared_ptr<const BgpUpdate2>>(*msg));
      } else {
        EXPECT_TRUE(peer->boundedSendQueue_.empty());
        EXPECT_TRUE(peer->boundedIqueue_->empty());
      }
      --messagesRemaining;
      XLOGF(
          DBG3,
          "Remaining sizes: sq={}, iq={}",
          peer->boundedSendQueue_.size(),
          peer->boundedIqueue_->size());
      fiberSleepFor(1ms);
    }

    /* We should have filled both queues with update1 messages. */
    EXPECT_EQ(0, messagesRemaining);

    /* Verify both queues are now empty and unblocked */
    EXPECT_FALSE(peer->boundedIqueue_->isBlocked());
    EXPECT_FALSE(peer->boundedSendQueue_.isBlocked());
    EXPECT_EQ(0, peer->boundedIqueue_->size());
    EXPECT_EQ(0, peer->boundedSendQueue_.size());

    // Wait for coroutine to complete
    folly::coro::blockingWait(peer->asyncScope_.cancelAndJoinAsync());
  });
  workers.emplace_back(std::move(fiber));

  evb_.loop();
  folly::collectAll(workers.begin(), workers.end()).get();
  /* Verify sendQueue block stats have been updated due to backpressure */
  EXPECT_GT(peer->getSendQueueBlocks(), initialBlocks);
  EXPECT_GT(peer->getSendQueueTotalBlockDuration(), initialTotalBlockDuration);
  EXPECT_GT(peer->getLastSendQueueBlockTime(), initialLastBlockTime);
}

/**
 * Verifies that sendSocketLoop is reading messages from
 * boundedSendQueue_ when the flag is enabled.
 */
TEST_F(EgressBackpressureFixture, SendSocketLoopFromBoundedSendQueue) {
  gflags::FlagSaver flags;
  FLAGS_enable_egress_queue_backpressure = true;

  std::vector<folly::Future<folly::Unit>> workers;

  auto& fm = fmWrapper_.get();
  auto peer = std::make_shared<MockFiberBgpPeer>(
      params1_,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  peer->boundedSendQueue_.push(BgpKeepAlive{});
  peer->boundedSendQueue_.push(std::nullopt);
  EXPECT_EQ(2, peer->boundedSendQueue_.size());

  peer->sendQueue_.put(BgpKeepAlive{});
  peer->sendQueue_.putNull();
  EXPECT_EQ(2, peer->sendQueue_.size());

  {
    auto fiber = fm.addTaskFuture([&]() { peer->sendSocketLoop(); });
    workers.emplace_back(std::move(fiber));
  }

  evb_.loop();

  // NOTE: here means asyncScope_ has no remaining tasks.
  folly::collectAll(workers.begin(), workers.end()).get();

  /* The boundedSendQueue size should have decreased to 0
   * while the sendQueue size should not have changed.
   */
  EXPECT_EQ(0, peer->boundedSendQueue_.size());
  EXPECT_EQ(2, peer->sendQueue_.size());
}

/**
 * Verifies that we do send KeepAlive even when the queue is blocked
 * while establishing session.
 */
TEST_F(EgressBackpressureFixture, SendInitialKeepAliveWithBlockedQueueTest) {
  gflags::FlagSaver flags;
  FLAGS_enable_egress_queue_backpressure = true;

  auto& fm = fmWrapper_.get();
  auto peer = std::make_shared<MockFiberBgpPeer>(
      params1_,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  /* Create an open message for a peer. */
  BgpOpenMsg openMsg;
  openMsg.version() = kBgpVersion;
  // openMsg.asn() = static_cast<int64_t>(params1_.remoteAs);
  openMsg.holdTime() = static_cast<int32_t>(params1_.holdTime.count());
  openMsg.bgpID() = static_cast<uint32_t>(params1_.localBgpId.toLongHBO());
  openMsg.capabilities() = peer->caps_;
  openMsg.capabilities()->asn() = static_cast<int64_t>(params1_.remoteAs);

  peer->peeringState_.state = BgpSessionState::OPEN_SENT;
  /* Fill the queue to just under the high wm. */
  FillQueueToSize(peer->boundedSendQueue_, kEgressQueueHighWatermark);
  EXPECT_TRUE(peer->boundedSendQueue_.isBlocked());
  peer->processOpenMsg(openMsg);

  EXPECT_EQ(kEgressQueueHighWatermark + 1, peer->boundedSendQueue_.size());
  /* Verify the keep alive made it in. */
  while (peer->boundedSendQueue_.size() > 1) {
    auto msg = peer->boundedSendQueue_.get();
    EXPECT_EQ(update1_, std::get<std::shared_ptr<const BgpUpdate2>>(*msg));
  }

  auto sentKeepAlive = peer->boundedSendQueue_.get();
  EXPECT_TRUE(std::holds_alternative<BgpKeepAlive>(*sentKeepAlive));
}

TEST_F(EgressBackpressureFixture, SkipScheduledKeepAliveWithBlockedQueueTest) {
  gflags::FlagSaver flags;
  FLAGS_enable_egress_queue_backpressure = true;

  auto& fm = fmWrapper_.get();
  auto peer = std::make_shared<MockFiberBgpPeer>(
      params1_,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  /* Let's say the keep alive is just 1 second. */
  peer->peeringState_.keepAliveTime = std::chrono::seconds(1);

  fm.addTask([&] {
    FillQueueToSize(peer->boundedSendQueue_, kEgressQueueHighWatermark);
    EXPECT_TRUE(peer->boundedSendQueue_.isBlocked());
    peer->scheduleBgpKeepAliveTimer();
    EXPECT_EQ(0, peer->peeringState_.lastResetKeepAliveTimer);
    auto lastSentKeepAlive = peer->peeringState_.lastSentKeepAlive;

    /*
     * Suspend the fiber for at least twice as long as the keep alive.
     * The timeout must have fired.
     */
    fiberSleepFor(
        std::chrono::milliseconds(
            peer->peeringState_.keepAliveTime.count() * 1000 * 2));

    EXPECT_GT(peer->peeringState_.lastResetKeepAliveTimer, 0);
    EXPECT_EQ(lastSentKeepAlive, peer->peeringState_.lastSentKeepAlive);

    /* End the test by cleaning up the keep alive timer. */
    peer->keepAliveTimer_->cancelTimeout();
  });

  evb_.loop();
}

TEST_F(EgressBackpressureFixture, QueueBgpNotificationMsgTest) {
  auto& fm = fmWrapper_.get();
  auto peer = std::make_shared<MockFiberBgpPeer>(
      params1_,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  peer->sendBgpMessage(BgpNotification{});

  if (FLAGS_enable_egress_queue_backpressure) {
    /* Check that the notification was queued to bounded queue. */
    EXPECT_EQ(1, peer->boundedSendQueue_.size());
    auto msg = peer->boundedSendQueue_.get();
    EXPECT_TRUE(std::holds_alternative<BgpNotification>(*msg));
  } else {
    /* Check that the notification was queued to unbounded queue. */
    EXPECT_EQ(1, peer->sendQueue_.size());
    auto msg = peer->sendQueue_.get();
    EXPECT_TRUE(std::holds_alternative<BgpNotification>(*msg));
  }
}

/**
 * End-to-end test that rcvdQueue_ handles back pressure correctly when the
 * queue is full. This test simulates a realistic scenario where:
 * 1. Messages are received from async socket faster than processing
 * 2. rcvdQueue_ fills up to exactly maxIngressQueueSize capacity
 * 3. Parser blocks on fiberPush when queue is full (back pressure)
 * 4. Verify parser is actually blocked (additional messages not consumed)
 * 5. Once ingress loop starts draining, processing continues normally
 */
TEST_F(FiberBgpPeerFixture, RcvdQueueBackpressureTest) {
  size_t maxIngressQueueSize = 10;

  std::vector<folly::Future<folly::Unit>> workers;

  auto& fm = fmWrapper_.get();

  // Create socket pair - we'll write to mockSocket, peer reads from peerSocket
  folly::SocketPair sp;
  auto peerSocket = std::make_unique<FiberSocket>(
      folly::AsyncSocket::newSocket(
          &evb_, folly::NetworkSocket::fromFd(sp.extractFD0())),
      &evb_);
  auto mockSocket = std::make_unique<FiberSocket>(
      folly::AsyncSocket::newSocket(
          &evb_, folly::NetworkSocket::fromFd(sp.extractFD1())),
      &evb_);

  auto peer = std::make_shared<MockFiberBgpPeer>(
      params3_,
      fm,
      evb_,
      std::move(*peerSocket),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  // update peer queue size
  peer->rcvdQueue_ = nettools::bgplib::MonitoredBackPressuredQueue<
      std::optional<folly::Try<FiberBgpParser::BgpMessageT>>>(
      maxIngressQueueSize);

  // Create a KeepAlive message to send repeatedly
  auto serializedMsg = BgpMessageSerializer::serializeBgpKeepAlive();

  {
    auto fiber = fm.addTaskFuture([&]() {
      /*
       * Start only socket reading - NOT the ingress processing loop yet
       * This will cause messages to accumulate in rcvdQueue_
       */
      fm.addTask([peer] { peer->readSocketLoop(); });

      // Send enough messages to fill the queue to capacity
      for (size_t i = 0; i < maxIngressQueueSize + 2; ++i) {
        mockSocket->write(serializedMsg->clone());
      }

      /*
       * Wait for queue to fill to capacity by polling the queue size
       * Use fiberSleepFor(0ms) to allow event loop to process socket I/O
       */
      while (peer->rcvdQueue_.size() < maxIngressQueueSize) {
        fiberSleepFor(0ms);
      }

      // Verify rcvdQueue_ is exactly at capacity
      auto queueSize = peer->rcvdQueue_.size();
      XLOGF(INFO, "rcvdQueue_ size after filling: {}", queueSize);
      EXPECT_EQ(maxIngressQueueSize, queueSize);

      /*
       * Now send additional messages and verify parser is blocked
       * (messages won't be consumed because queue is full)
       */
      const size_t additionalMessages = 3;
      for (size_t i = 0; i < additionalMessages; ++i) {
        mockSocket->write(serializedMsg->clone());
      }

      /*
       * Yield multiple times and verify queue size stays at capacity
       * Use fiberSleepFor(0ms) to allow event loop to run
       */
      for (int i = 0; i < 10; ++i) {
        fiberSleepFor(0ms);
        queueSize = peer->rcvdQueue_.size();
        EXPECT_EQ(maxIngressQueueSize, queueSize);
      }

      XLOGF(
          INFO,
          "rcvdQueue_ size after sending more (should be blocked): {}",
          queueSize);
      EXPECT_EQ(maxIngressQueueSize, queueSize);

      // Now start the ingress processing loop to drain the queue
      XLOG(INFO, "Starting ingress loop to drain queue");
      peer->asyncScope_.add(
          co_withExecutor(&evb_, peer->processIngressBgpMessageLoop()));

      // Wait for queue to start draining (unblocks the parser)
      while (peer->rcvdQueue_.size() >= maxIngressQueueSize) {
        fiberSleepFor(0ms);
      }

      XLOG(INFO, "Queue started draining, parser should now be unblocked");

      // Wait for all messages (including the additional ones) to be processed
      while (peer->rcvdQueue_.size() > 0) {
        fiberSleepFor(0ms);
      }

      XLOG(INFO, "Queue fully drained");
      EXPECT_EQ(0, peer->rcvdQueue_.size());

      // Push termination signal and clean up
      peer->rcvdQueue_.fiberPush(std::nullopt);
      folly::coro::blockingWait(peer->asyncScope_.cancelAndJoinAsync());

      // Close mock socket
      mockSocket->close();
    });
    workers.emplace_back(std::move(fiber));
  }

  evb_.loop();
  folly::collectAll(workers.begin(), workers.end()).get();
}

TEST_F(FiberBgpPeerFixture, SerializeGroupPduFlagTest) {
  auto& fm = fmWrapper_.get();

  // Test with enableSerializeGroupPdu=false
  {
    auto peer = std::make_shared<MockFiberBgpPeer>(
        params1_,
        fm,
        evb_,
        std::move(*fs0_),
        peer1Input_,
        peer1BoundedInput_,
        peer1Output_,
        false /* isRestarting */,
        false /* enableEgressQueueBackpressure */,
        false /* enableSerializeGroupPdu */);
    EXPECT_FALSE(peer->getSerializeGroupPdu());
  }

  // Test with enableSerializeGroupPdu=true
  {
    // Create a new socket pair for the second peer
    folly::SocketPair sp;
    auto fs = std::make_unique<FiberSocket>(
        folly::AsyncSocket::newSocket(
            &evb_, folly::NetworkSocket::fromFd(sp.extractFD0())),
        &evb_);

    auto peer = std::make_shared<MockFiberBgpPeer>(
        params1_,
        fm,
        evb_,
        std::move(*fs),
        peer1Input_,
        peer1BoundedInput_,
        peer1Output_,
        false /* isRestarting */,
        false /* enableEgressQueueBackpressure */,
        true /* enableSerializeGroupPdu */);
    EXPECT_TRUE(peer->getSerializeGroupPdu());

    // Verify UpdateDescriptor can be queued
    UpdateDescriptor descriptor;
    descriptor.serializedGroupPDU = std::make_shared<folly::IOBuf>();
    descriptor.v4Nexthop = folly::IPAddress("1.2.3.4");

    peer1Input_->push(descriptor);
    EXPECT_EQ(1, peer1Input_->size());
  }

  SUCCEED();
}

/**
 * Test that processIngressBgpMessageLoop properly manages rcvdQueue_'s
 * open/close state via ConsumerScope.
 *
 * This test verifies the ConsumerScope RAII pattern:
 * 1. By default, rcvdQueue_ is open
 * 2. Start processIngressBgpMessageLoop - it creates ConsumerScope
 * 3. Verify pushes work when the loop is running (queue is open)
 * 4. Fill queue to capacity to test back pressure scenario
 * 5. Send termination signal (std::nullopt) to end the loop
 * 6. Verify the loop has ended
 */
TEST_F(FiberBgpPeerFixture, RcvdQueueConsumerScopeTest) {
  std::vector<folly::Future<folly::Unit>> workers;

  auto& fm = fmWrapper_.get();
  auto peer = std::make_shared<MockFiberBgpPeer>(
      params1_,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  // make the queue very shallow
  peer->rcvdQueue_ = nettools::bgplib::MonitoredBackPressuredQueue<
      std::optional<folly::Try<FiberBgpParser::BgpMessageT>>>(1);

  // Step 1: Verify rcvdQueue_ is open by default (pushes work)
  peer->rcvdQueue_.fiberPush(
      folly::Try<FiberBgpParser::BgpMessageT>(BgpKeepAlive{}));
  EXPECT_EQ(1, peer->rcvdQueue_.size());

  // Step 2: Close the queue manually and verify pushes are dropped
  peer->rcvdQueue_.close();

  peer->rcvdQueue_.fiberPush(
      folly::Try<FiberBgpParser::BgpMessageT>(BgpKeepAlive{}));
  EXPECT_EQ(1, peer->rcvdQueue_.size()); // Push was dropped

  /*
   * Step 3: Start processIngressBgpMessageLoop
   * This creates a ConsumerScope which should open the queue
   */
  peer->asyncScope_.add(
      co_withExecutor(&evb_, peer->processIngressBgpMessageLoop()));

  {
    auto fiber = fm.addTaskFuture([&]() {
      /*
       * Step 4: Verify pushes work now that queue is open (ConsumerScope opened
       * it)
       */
      for (int i = 0; i < 10; ++i) {
        peer->rcvdQueue_.fiberPush(
            folly::Try<FiberBgpParser::BgpMessageT>(BgpKeepAlive{}));
      }

      // Step 5: Send termination signal to end the loop
      peer->rcvdQueue_.fiberPush(std::nullopt);

      // Wait for the loop to process termination and exit
      while (peer->asyncScope_.remaining() > 0) {
        fiberSleepFor(10ms);
      }

      // Step 6: Verify loop has ended
      EXPECT_EQ(0, peer->asyncScope_.remaining());

      // Queue has been closed, push would be dropped
      size_t sizeBeforePush = peer->rcvdQueue_.size();
      for (int i = 0; i < 10; ++i) {
        peer->rcvdQueue_.fiberPush(
            folly::Try<FiberBgpParser::BgpMessageT>(BgpKeepAlive{}));
      }
      EXPECT_EQ(sizeBeforePush, peer->rcvdQueue_.size()); // Push was dropped
    });
    workers.emplace_back(std::move(fiber));
  }

  evb_.loop();

  // Enforce all fiber tasks to complete
  folly::collectAll(workers.begin(), workers.end()).get();

  // Enforce for all coro tasks to complete
  folly::coro::blockingWait(peer->asyncScope_.cancelAndJoinAsync());
}

/**
 * Test that ConsumerScope properly closes the queue even when the
 * processing loop is cancelled abruptly (exception path).
 *
 * This test verifies the RAII guarantee by:
 * 1. Starting processIngressBgpMessageLoop (creates ConsumerScope, opens queue)
 * 2. Cancelling the async scope without normal termination (std::nullopt)
 * 3. Verifying the queue is closed by ConsumerScope destructor (pushes dropped)
 *
 * This tests that even in exceptional circumstances (abrupt shutdown, crash),
 * the ConsumerScope destructor properly closes the queue.
 */
TEST_F(FiberBgpPeerFixture, RcvdQueueConsumerScopeExceptionPathTest) {
  std::vector<folly::Future<folly::Unit>> workers;

  auto& fm = fmWrapper_.get();
  auto peer = std::make_shared<MockFiberBgpPeer>(
      params1_,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_);

  // make the queue very shallow
  peer->rcvdQueue_ = nettools::bgplib::MonitoredBackPressuredQueue<
      std::optional<folly::Try<FiberBgpParser::BgpMessageT>>>(1);

  // Step 1: Close the queue to establish baseline
  peer->rcvdQueue_.close();

  // Step 2: Verify queue is closed (pushes dropped)
  peer->rcvdQueue_.fiberPush(
      folly::Try<FiberBgpParser::BgpMessageT>(BgpKeepAlive{}));
  EXPECT_EQ(0, peer->rcvdQueue_.size()); // Push was dropped

  /*
   * Step 3: Start processIngressBgpMessageLoop
   * This creates a ConsumerScope which should open the queue
   */
  peer->asyncScope_.add(
      co_withExecutor(&evb_, peer->processIngressBgpMessageLoop()));

  {
    auto fiber = fm.addTaskFuture([&]() {
      /*
       * Step 4: Verify pushes work now that queue is open (ConsumerScope opened
       * it)
       */
      peer->rcvdQueue_.fiberPush(
          folly::Try<FiberBgpParser::BgpMessageT>(BgpKeepAlive{}));
      EXPECT_EQ(1, peer->rcvdQueue_.size()); // Push succeeded

      /*
       * Step 5: Cancel async scope ABRUPTLY (without sending std::nullopt)
       * This simulates an exception path - the loop exits via cancellation
       * and ConsumerScope destructor should close the queue via RAII
       */
      peer->asyncScope_.requestCancellation();

      // Wait for the loop to process cancellation and exit
      while (peer->asyncScope_.remaining() > 0) {
        fiberSleepFor(10ms);
      }

      /*
       * Step 6: Verify queue is closed by ConsumerScope destructor
       * (RAII guarantee - queue closed even on abrupt exit)
       */
      size_t sizeBeforePush = peer->rcvdQueue_.size();
      for (int i = 0; i < 10; ++i) {
        peer->rcvdQueue_.fiberPush(
            folly::Try<FiberBgpParser::BgpMessageT>(BgpKeepAlive{}));
      }
      EXPECT_EQ(sizeBeforePush, peer->rcvdQueue_.size()); // Pushes were dropped
    });
    workers.emplace_back(std::move(fiber));
  }

  evb_.loop();

  // Enforce all fiber tasks to complete
  folly::collectAll(workers.begin(), workers.end()).get();

  // Enforce for all coro tasks to complete
  folly::coro::blockingWait(peer->asyncScope_.cancelAndJoinAsync());
}

/**
 * Test that verifies the close() API is called correctly during peer
 * termination when the queue is in a blocked state.
 *
 * This test:
 * 1. Creates a FiberBgpPeer with egress backpressure enabled
 * 2. Starts the message processing loops
 * 3. Puts termination signal into boundedIqueue_, which should trigger
 *    boundedSendQueue_ to eventually close when the termination signal
 *    is processed in sendSocketLoop.
 * 4. Verifies all tasks have exited (asyncScope has 0 remaining tasks)
 * 5. Verifies the queue has been closed
 */
TEST_F(EgressBackpressureFixture, QueueCloseTest) {
  gflags::FlagSaver flags;
  FLAGS_enable_egress_queue_backpressure = true;

  std::vector<folly::Future<folly::Unit>> workers;

  auto& fm = fmWrapper_.get();

  auto peer = std::make_shared<MockFiberBgpPeer>(
      params1_,
      fm,
      evb_,
      std::move(*fs0_),
      peer1Input_,
      peer1BoundedInput_,
      peer1Output_,
      false /* isRestarting */,
      true /* enableEgressQueueBackpressure */);

  auto runPeer = fm.addTaskFuture([&]() { peer->run(); });
  auto terminatePeer = fm.addTaskFuture([&]() {
    /*
     * Verify that we have 2 coros running on asyncScope_:
     * processIngressBgpMessageLoop and
     * processEgressBgpMessageLoopWithBackpressure.
     */
    EXPECT_EQ(2, peer->asyncScope_.remaining());
    // Fill the boundedSendQueue_ to high watermark to force blocked state
    FillQueueToSize(peer->boundedSendQueue_, kEgressQueueHighWatermark);
    EXPECT_FALSE(peer->boundedSendQueue_.isClosed());

    // Push an error into the errorQueue to force termination.
    peer->errorQueue_.put(FiberBgpPeer::BgpHoldTimerExpired{});
  });

  workers.emplace_back(std::move(runPeer));
  workers.emplace_back(std::move(terminatePeer));

  evb_.loop();

  folly::collectAll(workers.begin(), workers.end()).get();

  /*
   * After cancellation completes, all tasks should have exited.
   * Check that the final queue state is closed.
   */
  EXPECT_EQ(0, peer->asyncScope_.remaining());
  EXPECT_TRUE(peer->boundedSendQueue_.isClosed());
}

} // namespace bgplib
} // namespace nettools
} // namespace facebook
