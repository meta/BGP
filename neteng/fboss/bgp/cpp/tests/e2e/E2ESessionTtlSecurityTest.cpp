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
 * E2E tests for TTL Security / GTSM (RFC 5082).
 *
 * Real-socket tests using TestFiberBgpPeerManager to verify GTSM socket
 * options (IP_TTL, IP_MINTTL) and kernel-level TTL enforcement on live
 * TCP/BGP sessions over loopback.
 */

/*
 * Enable friend access to FiberBgpPeer::sock_ for socket option verification.
 * Must be defined before any transitive include of FiberBgpPeer.h.
 */
namespace facebook::bgp {
class GtsmRealSocketTest;
}
#define FiberBgpPeer_TEST_FRIENDS \
  friend class facebook::bgp::GtsmRealSocketTest;

#include <gtest/gtest.h>

#include <folly/logging/xlog.h>

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/lib/tests/FiberBgpPeerManagerTestUtils.h"

extern "C" {
#include <netinet/ip.h>
}

#ifndef IP_MINTTL
#define IP_MINTTL 21
#endif

using facebook::nettools::bgplib::BgpPeerId;
using facebook::nettools::bgplib::FiberBgpPeer;
using facebook::nettools::bgplib::getFiberManagerOptions;

namespace facebook::bgp {

namespace {
const folly::IPAddress kGtsmR1 = folly::IPAddress("127.10.0.1");
const folly::IPAddress kGtsmR2 = folly::IPAddress("127.10.0.2");
const folly::IPAddress kGtsmR3 = folly::IPAddress("127.10.0.3");
constexpr uint32_t kTestAsn = 100;
} // namespace

/*
 * Test fixture for real-socket GTSM tests.
 * Creates TestFiberBgpPeerManagers that establish actual TCP/BGP sessions
 * over loopback, allowing verification of GTSM socket options.
 */
class GtsmRealSocketTest : public ::testing::Test {
 public:
  GtsmRealSocketTest()
      : fmWrapper_(
            folly::fibers::getFiberManager(evb_, getFiberManagerOptions(256))) {
  }

 protected:
  BgpGlobalConfig makeBgpGlobalConfig(
      folly::IPAddress routerId,
      folly::IPAddress clusterId) {
    return BgpGlobalConfig{
        kTestAsn,
        routerId,
        clusterId,
        std::chrono::seconds(180),
        std::nullopt, /* listenAddr — ephemeral */
        std::chrono::seconds(120),
        {}, /* networksV4 */
        {}, /* networksV6 */
        std::nullopt, /* localConfedAsn */
        ComputeUcmpFromLbwComm{false},
        0, /* ucmpWidth */
        std::nullopt, /* ucmpQuantizer */
        ValidateRemoteAs{true},
        SupportStatefulGr{true},
        EnableServerSocket{true},
        AllowLoopbackReflection{false},
        CountConfedsInAsPathLen{false},
        std::nullopt, /* deviceName */
        std::nullopt, /* switchLimitConfig */
        std::nullopt, /* dynamicPeerLimit */
        std::nullopt, /* streamSubscriberLimit */
        EnableNexthopTracking{false},
        {}, /* includeInterfaceRegexes */
        EnableDynamicPolicyEvaluation{false},
        std::nullopt, /* thriftServerConfig */
        false /* enableEgressQueueBackpressure */
    };
  }

  void initPeerManagers() {
    auto& fm = fmWrapper_.get();
    auto config1 = makeBgpGlobalConfig(kGtsmR1, kGtsmR1);
    auto config2 = makeBgpGlobalConfig(kGtsmR2, kGtsmR2);
    peerMgr1_ = std::make_shared<TestFiberBgpPeerManager>(
        config1, &callback1_, fm, evb_);
    fm.addTask([this] { peerMgr1_->run(); });
    peerMgr2_ = std::make_shared<TestFiberBgpPeerManager>(
        config2, &callback2_, fm, evb_);
    fm.addTask([this] { peerMgr2_->run(); });
  }

  /*
   * Build PeeringParams for a peer with optional GTSM.
   * localId: our routerId, peerPort: remote listen port,
   * ttlSecurityHops: GTSM hop count (nullopt = no GTSM).
   */
  PeeringParams makePeeringParams(
      const folly::IPAddress& peerAddr,
      const folly::IPAddress& localId,
      uint16_t peerPort,
      std::optional<int32_t> ttlSecurityHops = std::nullopt) {
    PeeringParams params{fboss::kDefaultVrf};
    params.peerAddr = peerAddr;
    params.globalAs = kTestAsn;
    params.localAs = kTestAsn;
    params.remoteAs = kTestAsn;
    params.localBgpId = localId.asV4();
    params.localClusterId = localId.asV4();
    params.holdTime = std::chrono::seconds(180);
    params.peerPort = peerPort;
    params.bindAddr = folly::SocketAddress(localId, 0);
    params.connectMode =
        neteng::fboss::bgp::thrift::TBgpSessionConnectMode::PASSIVE_ACTIVE;
    params.ttlSecurityHops = ttlSecurityHops;
    return params;
  }

  /*
   * Verify socket options on an established peer's socket.
   * Uses friend access to FiberBgpPeer::sock_.
   */
  void verifySocketOption(
      std::shared_ptr<TestFiberBgpPeerManager> peerMgr,
      const BgpPeerId& peerId,
      int level,
      int optname,
      int expectedValue) {
    auto result = peerMgr->getEstablishedSessionInfo(peerId);
    ASSERT_TRUE(result.hasValue()) << "Peer not established";
    auto& peer = result.value()->peer;
    int optval = 0;
    socklen_t optlen = sizeof(optval);
    int ret = peer->sock_.getSockOpt(level, optname, &optval, &optlen);
    ASSERT_EQ(ret, 0) << "getsockopt failed, errno=" << errno;
    EXPECT_EQ(optval, expectedValue)
        << "level=" << level << " optname=" << optname;
  }

  /*
   * Poll to confirm a session does NOT establish.
   * Uses callback counters to fail fast if the session unexpectedly comes up,
   * rather than a fixed sleep. Falls back to a timeout as a backstop.
   */
  void waitForSessionNotEstablished(
      const BgpPeerId& peerId1,
      const BgpPeerId& peerId2,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(5000),
      std::chrono::milliseconds pollInterval = std::chrono::milliseconds(100)) {
    const auto maxRetries = timeout / pollInterval;
    for (int i = 0; i < maxRetries; ++i) {
      // Fail immediately if session comes up unexpectedly
      ASSERT_EQ(callback1_.getEstablishedCallbackCount(peerId1), 0u)
          << "Session unexpectedly established on peer1";
      ASSERT_EQ(callback2_.getEstablishedCallbackCount(peerId2), 0u)
          << "Session unexpectedly established on peer2";
      nettools::bgplib::fiberSleepFor(pollInterval);
    }
  }

  folly::EventBase evb_;
  std::reference_wrapper<folly::fibers::FiberManager> fmWrapper_;
  std::shared_ptr<TestFiberBgpPeerManager> peerMgr1_;
  std::shared_ptr<TestFiberBgpPeerManager> peerMgr2_;
  TestFiberBgpPeerCallback callback1_;
  TestFiberBgpPeerCallback callback2_;
};

/*
 * Both peers configured with GTSM (hops=1). Verify session establishes
 * over a real TCP connection on loopback.
 */
TEST_F(GtsmRealSocketTest, GtsmSessionEstablishWithRealSocket) {
  auto& fm = fmWrapper_.get();
  initPeerManagers();

  // peerAddr1 is the address of peerMgr2 as seen by peerMgr1
  const auto peerAddr1 = kGtsmR2;
  const BgpPeerId peerId1{peerAddr1, peerAddr1.asV4().toLongHBO()};
  // peerAddr2 is the address of peerMgr1 as seen by peerMgr2
  const auto peerAddr2 = kGtsmR1;
  const BgpPeerId peerId2{peerAddr2, peerAddr2.asV4().toLongHBO()};

  fm.addTask([&] {
    const auto port2 = peerMgr2_->getListenAddress()->getPort();
    auto params1 =
        makePeeringParams(peerAddr1, kGtsmR1, port2, /*ttlSecurityHops=*/1);
    peerMgr1_->addPeer(peerAddr1, params1);

    const auto port1 = peerMgr1_->getListenAddress()->getPort();
    auto params2 =
        makePeeringParams(peerAddr2, kGtsmR2, port1, /*ttlSecurityHops=*/1);
    peerMgr2_->addPeer(peerAddr2, params2);

    waitTillSessionsComeUp(fm, peerMgr1_, {peerId1});

    EXPECT_TRUE(callback1_.isSessionUp(peerId1));
    EXPECT_TRUE(callback2_.isSessionUp(peerId2));

    peerMgr1_->shutdownWithGR(false);
    peerMgr2_->shutdownWithGR(false);
    waitTillSessionsGoDown(fm, peerMgr1_, {peerId1});
  });

  evb_.loop();
}

/*
 * After GTSM session establishment with hops=1, verify IP_TTL=255
 * and IP_MINTTL=255 on the live socket via getsockopt.
 */
TEST_F(GtsmRealSocketTest, GtsmSocketOptionsVerified) {
  auto& fm = fmWrapper_.get();
  initPeerManagers();

  const auto peerAddr1 = kGtsmR2;
  const BgpPeerId peerId1{peerAddr1, peerAddr1.asV4().toLongHBO()};
  const auto peerAddr2 = kGtsmR1;
  const BgpPeerId peerId2{peerAddr2, peerAddr2.asV4().toLongHBO()};

  fm.addTask([&] {
    const auto port2 = peerMgr2_->getListenAddress()->getPort();
    auto params1 =
        makePeeringParams(peerAddr1, kGtsmR1, port2, /*ttlSecurityHops=*/1);
    peerMgr1_->addPeer(peerAddr1, params1);

    const auto port1 = peerMgr1_->getListenAddress()->getPort();
    auto params2 =
        makePeeringParams(peerAddr2, kGtsmR2, port1, /*ttlSecurityHops=*/1);
    peerMgr2_->addPeer(peerAddr2, params2);

    waitTillSessionsComeUp(fm, peerMgr1_, {peerId1});

    // Verify GTSM socket options on both peers
    verifySocketOption(peerMgr1_, peerId1, IPPROTO_IP, IP_TTL, 255);
    verifySocketOption(peerMgr1_, peerId1, IPPROTO_IP, IP_MINTTL, 255);
    verifySocketOption(peerMgr2_, peerId2, IPPROTO_IP, IP_TTL, 255);
    verifySocketOption(peerMgr2_, peerId2, IPPROTO_IP, IP_MINTTL, 255);

    peerMgr1_->shutdownWithGR(false);
    peerMgr2_->shutdownWithGR(false);
    waitTillSessionsGoDown(fm, peerMgr1_, {peerId1});
  });

  evb_.loop();
}

/*
 * Mismatched GTSM config: peerMgr1 has GTSM (hops=1, MINTTL=255),
 * peerMgr2 has no GTSM (default TTL, no MINTTL).
 *
 * On loopback, the non-GTSM side sends with the kernel default TTL
 * (typically 64 or 96), which is below 255. The GTSM side's kernel
 * drops these packets via IP_MINTTL enforcement. The BGP session
 * should NOT establish.
 */
TEST_F(GtsmRealSocketTest, GtsmMismatchConfigSessionFails) {
  auto& fm = fmWrapper_.get();
  initPeerManagers();

  const auto peerAddr1 = kGtsmR2;
  const BgpPeerId peerId1{peerAddr1, peerAddr1.asV4().toLongHBO()};
  const auto peerAddr2 = kGtsmR1;
  const BgpPeerId peerId2{peerAddr2, peerAddr2.asV4().toLongHBO()};

  fm.addTask([&] {
    const auto port2 = peerMgr2_->getListenAddress()->getPort();
    // peerMgr1: GTSM enabled (hops=1 → IP_TTL=255, IP_MINTTL=255)
    auto params1 =
        makePeeringParams(peerAddr1, kGtsmR1, port2, /*ttlSecurityHops=*/1);
    peerMgr1_->addPeer(peerAddr1, params1);

    const auto port1 = peerMgr1_->getListenAddress()->getPort();
    // peerMgr2: No GTSM (default TTL, no MINTTL)
    auto params2 = makePeeringParams(peerAddr2, kGtsmR2, port1);
    peerMgr2_->addPeer(peerAddr2, params2);

    /*
     * Poll to confirm session does NOT establish, using callback counters
     * to fail fast if session unexpectedly comes up.
     */
    waitForSessionNotEstablished(peerId1, peerId2);

    EXPECT_FALSE(callback1_.isSessionUp(peerId1))
        << "GTSM mismatch: session should NOT establish";
    EXPECT_FALSE(callback2_.isSessionUp(peerId2))
        << "GTSM mismatch: session should NOT establish";

    peerMgr1_->shutdownWithGR(false);
    peerMgr2_->shutdownWithGR(false);
  });

  evb_.loop();
}

/*
 * Mixed-peer coexistence: peerMgr1 has two peers —
 *   one GTSM (to peerMgr2, hops=1) and one non-GTSM (to peerMgr3).
 * Both sessions should establish. GTSM peer's socket should have
 * IP_TTL=255 / IP_MINTTL=255; non-GTSM peer's socket should not.
 */
TEST_F(GtsmRealSocketTest, GtsmMixedPeerCoexistence) {
  auto& fm = fmWrapper_.get();
  initPeerManagers(); // creates peerMgr1_ (kGtsmR1) and peerMgr2_ (kGtsmR2)

  // Third peer manager for the non-GTSM peer
  TestFiberBgpPeerCallback callback3;
  auto config3 = makeBgpGlobalConfig(kGtsmR3, kGtsmR3);
  auto peerMgr3 =
      std::make_shared<TestFiberBgpPeerManager>(config3, &callback3, fm, evb_);
  fm.addTask([peerMgr3] { peerMgr3->run(); });

  // PeerMgr1's view: two peers
  const auto peerAddr1ToR2 = kGtsmR2; // GTSM peer
  const BgpPeerId peerId1ToR2{peerAddr1ToR2, peerAddr1ToR2.asV4().toLongHBO()};
  const auto peerAddr1ToR3 = kGtsmR3; // non-GTSM peer
  const BgpPeerId peerId1ToR3{peerAddr1ToR3, peerAddr1ToR3.asV4().toLongHBO()};

  // PeerMgr2's view: one GTSM peer back to PeerMgr1
  const auto peerAddr2ToR1 = kGtsmR1;
  const BgpPeerId peerId2ToR1{peerAddr2ToR1, peerAddr2ToR1.asV4().toLongHBO()};

  // PeerMgr3's view: one non-GTSM peer back to PeerMgr1
  const auto peerAddr3ToR1 = kGtsmR1;
  const BgpPeerId peerId3ToR1{peerAddr3ToR1, peerAddr3ToR1.asV4().toLongHBO()};

  fm.addTask([&] {
    const auto port2 = peerMgr2_->getListenAddress()->getPort();
    const auto port3 = peerMgr3->getListenAddress()->getPort();
    const auto port1 = peerMgr1_->getListenAddress()->getPort();

    // PeerMgr1: GTSM peer to PeerMgr2, non-GTSM peer to PeerMgr3
    peerMgr1_->addPeer(
        peerAddr1ToR2, makePeeringParams(peerAddr1ToR2, kGtsmR1, port2, 1));
    peerMgr1_->addPeer(
        peerAddr1ToR3, makePeeringParams(peerAddr1ToR3, kGtsmR1, port3));

    // PeerMgr2: GTSM peer back to PeerMgr1
    peerMgr2_->addPeer(
        peerAddr2ToR1, makePeeringParams(peerAddr2ToR1, kGtsmR2, port1, 1));

    // PeerMgr3: non-GTSM peer back to PeerMgr1
    peerMgr3->addPeer(
        peerAddr3ToR1, makePeeringParams(peerAddr3ToR1, kGtsmR3, port1));

    // Both sessions should come up
    waitTillSessionsComeUp(fm, peerMgr1_, {peerId1ToR2, peerId1ToR3});

    EXPECT_TRUE(callback1_.isSessionUp(peerId1ToR2))
        << "GTSM peer session should be up";
    EXPECT_TRUE(callback1_.isSessionUp(peerId1ToR3))
        << "Non-GTSM peer session should be up";
    EXPECT_TRUE(callback2_.isSessionUp(peerId2ToR1));
    EXPECT_TRUE(callback3.isSessionUp(peerId3ToR1));

    // Verify socket options: GTSM peer has TTL security, non-GTSM does not
    verifySocketOption(peerMgr1_, peerId1ToR2, IPPROTO_IP, IP_TTL, 255);
    verifySocketOption(peerMgr1_, peerId1ToR2, IPPROTO_IP, IP_MINTTL, 255);
    verifySocketOption(peerMgr1_, peerId1ToR3, IPPROTO_IP, IP_MINTTL, 0);

    peerMgr1_->shutdownWithGR(false);
    peerMgr2_->shutdownWithGR(false);
    peerMgr3->shutdownWithGR(false);
    waitTillSessionsGoDown(fm, peerMgr1_, {peerId1ToR2, peerId1ToR3});
  });

  evb_.loop();
}

} // namespace facebook::bgp
