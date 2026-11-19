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
 * E2E test: verifies the full pipeline from thrift BgpConfig -> Config parsing
 * -> PeeringParams (with correct bindAddr/local_addr) -> FiberBgpPeerManager ->
 * passiveConnectLoop local address validation.
 *
 * This test catches the production bug where a remote peer connects to the
 * wrong local IP address and the passive accept path fails to validate it.
 *
 * Test topology (Config_CrossSubnetReject):
 *
 *   thrift::BgpConfig (in-memory)
 *   +------------------------------------------+
 *   | router_id: 127.2.0.1                     |
 *   | listen_addr: 0.0.0.0 (all interfaces)    |
 *   | peers:                                    |
 *   |   peer_addr=127.1.0.1 local_addr=127.2.0.1 (PASSIVE) |
 *   |   peer_addr=127.3.0.2 local_addr=127.3.0.1 (PASSIVE) |
 *   +------------------------------------------+
 *         |
 *         v  Config(thriftConfig) -> getPeeringParamsForPeer()
 *         |
 *   +------------------------------------------+
 *   |  peerMgr1  (routerId=127.2.0.1)          |
 *   |  listens on 0.0.0.0:port1                |
 *   |                                          |
 *   |  Configured peers:                       |
 *   |    127.1.0.1  local_addr=127.2.0.1  PO   |
 *   |    127.3.0.2  local_addr=127.3.0.1  PO   |
 *   +-----+--------------------+---------------+
 *         ^                    ^
 *         |                    |
 *    CORRECT conn         WRONG conn
 *    local=127.2.0.1      local=127.2.0.1
 *    matches config       config expects 127.3.0.1
 *    -> ACCEPT            -> REJECT
 *         |                    |
 *   +-----+------+      +-----+------+
 *   |  peerMgr2  |      |  peerMgr3  |
 *   |  rid=      |      |  rid=      |
 *   |  127.1.0.1 |      |  127.3.0.2 |
 *   |            |      |            |
 *   |  peer:     |      |  peer:     |
 *   |  127.2.0.1 |      |  127.2.0.1 |  <-- connects to wrong addr!
 *   |  bind=     |      |  bind=     |      should be 127.3.0.1
 *   |  127.1.0.1 |      |  127.3.0.2 |
 *   |  AO        |      |  AO        |
 *   +------------+      +------------+
 *
 *   PO = PASSIVE_ONLY, AO = ACTIVE_ONLY
 *   peerMgr3 connects to peerMgr1 at 127.2.0.1 instead of 127.3.0.1,
 *   reproducing the production bug. The fix rejects this connection.
 */

#include <gtest/gtest.h>

#include <folly/fibers/FiberManagerMap.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/logging/xlog.h>

#include "configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h"
#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/config/Config.h"
#include "neteng/fboss/bgp/cpp/config/ConfigStructs.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberBgpPeerManager.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/Utils.h"
#include "neteng/fboss/bgp/cpp/lib/tests/FiberBgpPeerManagerTestUtils.h"

namespace facebook::nettools::bgplib {

using namespace std::chrono_literals;
using facebook::bgp::BgpGlobalConfig;
using facebook::bgp::Config;
using facebook::bgp::PeeringParams;
using facebook::bgp::thrift::BgpConfig;
using facebook::bgp::thrift::BgpPeer;
using facebook::fboss::kDefaultVrf;

namespace {

const auto kRouterIdStr = std::string("127.2.0.1");
const auto kRouterId = folly::IPAddress("127.2.0.1");
const uint32_t kLocalAsn = 100;

// Peer 1: on subnet 127.2.x.x
const auto kPeer1Addr = folly::IPAddress("127.1.0.1");
const auto kPeer1LocalAddr = std::string("127.2.0.1");

// Peer 2: on subnet 127.3.x.x
const auto kPeer2Addr = folly::IPAddress("127.3.0.2");
const auto kPeer2LocalAddr = std::string("127.3.0.1");

BgpConfig buildTestBgpConfig() {
  BgpConfig config;
  config.router_id() = kRouterIdStr;
  config.local_as_4_byte() = kLocalAsn;
  config.hold_time() = 30;
  config.listen_addr() = "0.0.0.0";
  config.listen_port() = 0; // OS-assigned
  config.graceful_restart_convergence_seconds() = 60;

  BgpPeer peer1;
  peer1.peer_addr() = kPeer1Addr.str();
  peer1.local_addr() = kPeer1LocalAddr;
  peer1.remote_as_4_byte() = kLocalAsn;
  peer1.next_hop4() = kPeer1LocalAddr;
  peer1.next_hop6() = "::";
  peer1.is_passive() = true;

  BgpPeer peer2;
  peer2.peer_addr() = kPeer2Addr.str();
  peer2.local_addr() = kPeer2LocalAddr;
  peer2.remote_as_4_byte() = kLocalAsn;
  peer2.next_hop4() = kPeer2LocalAddr;
  peer2.next_hop6() = "::";
  peer2.is_passive() = true;

  config.peers() = {peer1, peer2};
  return config;
}

} // namespace

/*
 * E2E test: Build thrift BgpConfig in-memory, pass to Config constructor,
 * verify that local_addr is correctly parsed into PeeringParams.bindAddr,
 * create FiberBgpPeerManager using those PeeringParams, and verify that
 * passive connections to the wrong local address are rejected while correct
 * connections succeed.
 */
TEST(PassiveConnectConfigE2ETest, Config_CrossSubnetReject) {
  // ---- Step 1: Build thrift config and load via Config constructor ----
  auto thriftConfig = buildTestBgpConfig();

  /*
   * ---- Step 2: Load config via in-memory thrift struct (production pipeline)
   * ----
   */
  Config bgpConfig(thriftConfig);

  // ---- Step 3: Verify PeeringParams have correct bindAddr ----
  const auto& peerToConfig = bgpConfig.getPeerToConfig();

  // Peer 1: 127.1.0.1 should have bindAddr = 127.2.0.1
  auto it1 = peerToConfig.find(kPeer1Addr);
  ASSERT_NE(it1, peerToConfig.end())
      << "Peer " << kPeer1Addr.str() << " not found in config";
  auto params1 = bgpConfig.getPeeringParamsForPeer(*it1->second);
  EXPECT_EQ(params1.bindAddr.getIPAddress(), folly::IPAddress(kPeer1LocalAddr))
      << "Peer 1 bindAddr should be " << kPeer1LocalAddr
      << " (from local_addr in config)";
  EXPECT_EQ(
      params1.connectMode, facebook::bgp::TBgpSessionConnectMode::PASSIVE_ONLY)
      << "Peer 1 should be PASSIVE_ONLY (is_passive=true in config)";

  // Peer 2: 127.3.0.2 should have bindAddr = 127.3.0.1
  auto it2 = peerToConfig.find(kPeer2Addr);
  ASSERT_NE(it2, peerToConfig.end())
      << "Peer " << kPeer2Addr.str() << " not found in config";
  auto params2 = bgpConfig.getPeeringParamsForPeer(*it2->second);
  EXPECT_EQ(params2.bindAddr.getIPAddress(), folly::IPAddress(kPeer2LocalAddr))
      << "Peer 2 bindAddr should be " << kPeer2LocalAddr
      << " (from local_addr in config)";
  EXPECT_EQ(
      params2.connectMode, facebook::bgp::TBgpSessionConnectMode::PASSIVE_ONLY)
      << "Peer 2 should be PASSIVE_ONLY (is_passive=true in config)";

  XLOGF(
      INFO,
      "Config pipeline verification passed: peer {} bindAddr={}, peer {} bindAddr={}",
      kPeer1Addr.str(),
      params1.bindAddr.describe(),
      kPeer2Addr.str(),
      params2.bindAddr.describe());

  // ---- Step 4: Create FiberBgpPeerManagers and run TCP test ----
  folly::EventBase evb;
  auto& fm = folly::fibers::getFiberManager(evb);

  /*
   * peerMgr1: the main router, uses Config's globalConfig (routerId=127.2.0.1)
   * Listens on 0.0.0.0 (all interfaces) with OS-assigned port
   */
  auto globalConfig1 = *bgpConfig.getBgpGlobalConfig();
  TestFiberBgpPeerCallback callback1;
  auto peerMgr1 = std::make_shared<TestFiberBgpPeerManager>(
      globalConfig1, &callback1, fm, evb);
  fm.addTask([&peerMgr1] { peerMgr1->run(); });

  // peerMgr2: remote peer at 127.1.0.1 (will connect correctly to 127.2.0.1)
  auto globalConfig2 = makeBgpGlobalConfig(kPeer1Addr, kPeer1Addr);
  TestFiberBgpPeerCallback callback2;
  auto peerMgr2 = std::make_shared<TestFiberBgpPeerManager>(
      globalConfig2, &callback2, fm, evb);
  fm.addTask([&peerMgr2] { peerMgr2->run(); });

  // peerMgr3: remote peer at 127.3.0.2 (will connect to WRONG local address)
  auto globalConfig3 = makeBgpGlobalConfig(kPeer2Addr, kPeer2Addr);
  TestFiberBgpPeerCallback callback3;
  auto peerMgr3 = std::make_shared<TestFiberBgpPeerManager>(
      globalConfig3, &callback3, fm, evb);
  fm.addTask([&peerMgr3] { peerMgr3->run(); });

  // Subscribe to log messages for reject verification
  auto& logMessages = subscribeToLogMessages("");
  logMessages.clear();

  fm.addTask([&] {
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    const auto peerPort3 = peerMgr3->getListenAddress()->getPort();

    /*
     * ---- Add peers to peerMgr1 using PeeringParams from Config ----
     * Override peerPort to use OS-assigned ports (can't use 179 in tests)
     */
    params1.peerPort = peerPort2;
    auto result1 =
        peerMgr1->addPeer(kPeer1Addr, params1, ConnTimeParams(0ms, 0ms));
    ASSERT_FALSE(result1.hasError())
        << "Failed to add peer " << kPeer1Addr.str();

    params2.peerPort = peerPort3;
    auto result2 =
        peerMgr1->addPeer(kPeer2Addr, params2, ConnTimeParams(0ms, 0ms));
    ASSERT_FALSE(result2.hasError())
        << "Failed to add peer " << kPeer2Addr.str();

    /*
     * ---- peerMgr2: connects to peerMgr1 at 127.2.0.1 (CORRECT) ----
     * On peerMgr1's accepted socket: remote=127.1.0.1, local=127.2.0.1
     * configuredLocalAddr for peer 127.1.0.1 = 127.2.0.1 → MATCH → accept
     */
    PeeringParams remotePeerParams2{kDefaultVrf};
    remotePeerParams2.localAs = kLocalAsn;
    remotePeerParams2.remoteAs = kLocalAsn;
    remotePeerParams2.localBgpId = kPeer1Addr.asV4();
    remotePeerParams2.holdTime = std::chrono::seconds(30);
    remotePeerParams2.peerPort = peerPort1;
    remotePeerParams2.bindAddr = folly::SocketAddress(kPeer1Addr, 0);
    remotePeerParams2.connectMode =
        facebook::bgp::TBgpSessionConnectMode::ACTIVE_ONLY;
    remotePeerParams2.peerAddr = kRouterId;
    remotePeerParams2.nexthopV4 = kPeer1Addr.asV4();
    remotePeerParams2.nexthopV6 = folly::IPAddressV6("::");
    peerMgr2->addPeer(kRouterId, remotePeerParams2, ConnTimeParams(0ms, 0ms));

    /*
     * ---- peerMgr3: connects to peerMgr1 at 127.2.0.1 (WRONG config!) ----
     * This is an intentionally WRONG configuration: peerMgr3 should connect
     * to 127.3.0.1 but connects to 127.2.0.1 instead.
     * On peerMgr1's accepted socket: remote=127.3.0.2, local=127.2.0.1
     * configuredLocalAddr for peer 127.3.0.2 = 127.3.0.1
     *   → 127.2.0.1 != 127.3.0.1 → REJECT
     */
    PeeringParams remotePeerParams3{kDefaultVrf};
    remotePeerParams3.localAs = kLocalAsn;
    remotePeerParams3.remoteAs = kLocalAsn;
    remotePeerParams3.localBgpId = kPeer2Addr.asV4();
    remotePeerParams3.holdTime = std::chrono::seconds(30);
    remotePeerParams3.peerPort = peerPort1;
    remotePeerParams3.bindAddr = folly::SocketAddress(kPeer2Addr, 0);
    remotePeerParams3.connectMode =
        facebook::bgp::TBgpSessionConnectMode::ACTIVE_ONLY;
    remotePeerParams3.peerAddr = kRouterId;
    remotePeerParams3.nexthopV4 = kPeer2Addr.asV4();
    remotePeerParams3.nexthopV6 = folly::IPAddressV6("::");
    peerMgr3->addPeer(kRouterId, remotePeerParams3, ConnTimeParams(0ms, 0ms));

    // ---- Verify correct peer session comes up ----
    BgpPeerId peerId1{kPeer1Addr, kPeer1Addr.asV4().toLongHBO()};
    BgpPeerId peerId2{kRouterId, kRouterId.asV4().toLongHBO()};

    waitTillSessionsComeUp(fm, peerMgr2, {peerId2});
    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // peerMgr1 should have session up for peer 127.1.0.1
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    // ---- Verify cross-subnet connection is REJECTED ----
    fiberSleepFor(100ms);

    BgpPeerId peerId3{kPeer2Addr, kPeer2Addr.asV4().toLongHBO()};
    // peerMgr1 should NOT have session up for peer 127.3.0.2
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId3))
        << "Cross-subnet connection should be rejected: peer " << kPeer2Addr
        << " connected to local addr 127.2.0.1 but config has local_addr="
        << kPeer2LocalAddr;
    EXPECT_FALSE(callback1.isSessionUp(peerId3));

    // peerMgr3 should also not have session up
    BgpPeerId peerId1From3{kRouterId, kRouterId.asV4().toLongHBO()};
    EXPECT_EQ(0, callback3.getEstablishedCallbackCount(peerId1From3));
    EXPECT_FALSE(callback3.isSessionUp(peerId1From3));

    // Verify the rejection log was emitted
    bool foundRejectLog = false;
    for (const auto& [msg, cat] : logMessages) {
      if (msg.getMessage().find("Reject tcp connection from peer 127.3.0.2") !=
          std::string::npos) {
        foundRejectLog = true;
        break;
      }
    }
    EXPECT_TRUE(foundRejectLog)
        << "Expected rejection log for cross-subnet connection from "
        << kPeer2Addr.str() << " (connected to wrong local address)";

    // Cleanup
    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
    peerMgr3->shutdownWithGR(false);

    waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
    waitTillSessionsGoDown(fm, peerMgr2, {peerId2});
  });

  evb.loop();
}

} // namespace facebook::nettools::bgplib
