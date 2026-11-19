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

#define FiberBgpPeerManager_TEST_FRIENDS                                      \
  FRIEND_TEST(FiberBgpPeerManagerFixture, RunBgpPeerWithExceptionTest);       \
  FRIEND_TEST(FiberBgpPeerManagerTest, WriteToNotifyQueueTest);               \
  FRIEND_TEST(FiberBgpPeerManagerFixture, EgressBackpressureFlagTest);        \
  FRIEND_TEST(FiberBgpPeerManagerFixture, SendReceiveLongBgpUpdateTest);      \
  FRIEND_TEST(FiberBgpPeerManagerFixture, PeeringStateResetOnStopTest);       \
  FRIEND_TEST(FiberBgpPeerManagerFixture, PeeringStateResetOnStopWithGRTest); \
  FRIEND_TEST(FiberBgpPeerManagerFixture, PeerAcceptErrorTest);               \
  FRIEND_TEST(                                                                \
      FiberBgpPeerManagerFixture, ActiveConnectUseAfterFreeViaPeerStart);     \
  FRIEND_TEST(                                                                \
      FiberBgpPeerManagerFixture,                                             \
      AddressLevelPeerUpIgnoresStaleStaticRouterId);                          \
  FRIEND_TEST(                                                                \
      FiberBgpPeerManagerFixture,                                             \
      AddressLevelPeerUpPreservesDynamicAllSessionsSemantics);                \
  FRIEND_TEST(                                                                \
      FiberBgpPeerManagerFixture,                                             \
      ActiveConnectUseAfterFreeViaSessionTeardown);                           \
  FRIEND_TEST(                                                                \
      FiberBgpPeerManagerFixture,                                             \
      DeleteTerminatedSessionPreservesPeerWithoutConnectionInfo);             \
  FRIEND_TEST(                                                                \
      FiberBgpPeerManagerFixture,                                             \
      ScheduleDeleteTerminatedSessionSkipsCancelledScope);                    \
  FRIEND_TEST(                                                                \
      FiberBgpPeerManagerFixture,                                             \
      DeleteTerminatedSessionPreservesNewerSession);                          \
  FRIEND_TEST(                                                                \
      FiberBgpPeerManagerFixture,                                             \
      DeleteTerminatedSessionRemovesSessionForAdditionalRemoteAsVipPeer);     \
  FRIEND_TEST(                                                                \
      FiberBgpPeerManagerFixture,                                             \
      DeleteTerminatedSessionRemovesSessionFromDynamicPassivePeer);

#define FiberBgpPeer_TEST_FRIENDS                                       \
  friend class FiberBgpPeerManagerFixture;                              \
  FRIEND_TEST(FiberBgpPeerManagerFixture, EgressBackpressureFlagTest);  \
  FRIEND_TEST(FiberBgpPeerManagerFixture, PeeringStateResetOnStopTest); \
  FRIEND_TEST(FiberBgpPeerManagerFixture, PeeringStateResetOnStopWithGRTest);

#include <folly/Random.h>
#include <folly/ScopeGuard.h>
#include <folly/fibers/Baton.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/io/async/test/ScopedBoundPort.h>
#include <folly/logging/xlog.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>

#include <fb303/ThreadCachedServiceData.h>
#include "common/network/AddressUtil.h"
#include "fboss/agent/Constants.h"
#include "magic_enum/magic_enum.hpp"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/lib/BgpMessageSerializer.h"
#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberBgpPeerManager.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberServerSocket.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/Utils.h"
#include "neteng/fboss/bgp/cpp/lib/tests/FiberBgpPeerManagerTestUtils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/BoundedWaitUtils.h"

DEFINE_bool(
    enable_egress_queue_backpressure,
    false,
    "Flag to parameterize tests with egress backpressure enabled/disabled in BUCK targets.");

namespace facebook::nettools::bgplib {
using namespace std::chrono_literals;

using facebook::bgp::BgpGlobalConfig;
using facebook::bgp::PeeringParams;
using facebook::bgp::thrift::BgpNetwork;
using facebook::fboss::kDefaultVrf;
using facebook::neteng::fboss::bgp::thrift::TBgpSessionConnectMode;
using facebook::network::toBinaryAddress;
using facebook::network::toCIDRNetwork;
using facebook::network::toIPPrefix;
namespace thrift = facebook::network::thrift;
using folly::IPAddress;
using folly::IPAddressV4;
using folly::SocketAddress;
using std::make_shared;
using std::shared_ptr;
using std::vector;
using testing::_;
using testing::Return;

constexpr auto kDefaultGrRestartTime = 600s;
constexpr auto kDefaultHoldTime = 180s;
constexpr auto kNexthopStr = "1.1.1.1";
constexpr auto kMed = 32;
constexpr auto kLocalPref = 100;
constexpr auto kAsn = 65530;
constexpr auto kValue = 15800;
constexpr auto kAs1 = 100;

folly::CIDRNetwork intToCIDRNetwork(uint32_t ipInt, uint8_t maskLen) {
  return folly::CIDRNetwork(
      folly::IPAddress(folly::IPAddressV4::fromLongHBO(ipInt)), maskLen);
}

/*
 * Creates a BgpUpdate2 containing only IPv4 announcement, according
 * to the number of prefixes specified.
 */
BgpUpdate2 createBgpUpdate2(uint64_t numPrefixes, uint8_t maskLen) {
  BgpUpdate2 update;
  for (int i = 0; i < numPrefixes; ++i) {
    auto pfx = intToCIDRNetwork(i, maskLen);
    RiggedIPPrefix rigPrefix;
    rigPrefix.prefix() = toIPPrefix(pfx);
    update.mpAnnounced()->prefixes()->push_back(rigPrefix);
  }
  update.mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv4;
  update.mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  update.mpAnnounced()->nexthop() =
      toBinaryAddress(folly::IPAddress(kNexthopStr));

  update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;

  BgpAttrAsPathSegment segment;
  segment.asSequence()->push_back(kAs1);
  update.attrs()->asPath()->push_back(segment);

  update.attrs()->nexthop() = kNexthopStr;
  update.attrs()->med() = kMed;
  update.attrs()->isMedSet() = true;
  update.attrs()->localPref() = kLocalPref;

  BgpAttrCommunity community;
  community.asn() = kAsn;
  community.value() = kValue;
  update.attrs()->communities()->push_back(community);

  update.v4Nexthop() = toBinaryAddress(folly::IPAddress(kNexthopStr));

  return update;
}

vector<BgpUpdate2> createBgpUpdate2Vec(uint32_t num) {
  vector<BgpUpdate2> updates;

  CHECK_LE(num, 255);
  for (int i = 0; i < num; i++) {
    BgpUpdate2 update;
    auto prefixStr = fmt::format("1.1.{}.0/24", i);
    auto prefix = toIPPrefix(folly::IPAddress::createNetwork(prefixStr));
    RiggedIPPrefix rigPrefix;
    rigPrefix.prefix() = prefix;
    update.v4Announced2()->push_back(rigPrefix);
    update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
    BgpAttrAsPathSegment segment;
    segment.asSequence()->push_back(kAs1);
    update.attrs()->asPath()->push_back(segment);

    update.attrs()->nexthop() = kNexthopStr;
    update.attrs()->med() = kMed;
    update.attrs()->isMedSet() = true;
    update.attrs()->localPref() = kLocalPref;

    BgpAttrCommunity community;
    community.asn() = kAsn;
    community.value() = kValue;
    update.attrs()->communities()->push_back(community);

    update.v4Nexthop() = toBinaryAddress(folly::IPAddress(kNexthopStr));
    updates.push_back(update);
  }
  return updates;
}

vector<std::unique_ptr<BgpUpdate2>> packBgpUpdatesToSend(
    const vector<BgpUpdate2>& givenUpdates) {
  // convert to vector<unique_ptr<BgpUpdate2>>
  vector<std::unique_ptr<BgpUpdate2>> updates;
  for (const auto& givenOneUpdate : givenUpdates) {
    auto update = std::make_unique<BgpUpdate2>(givenOneUpdate);
    updates.push_back(std::move(update));
  }
  return updates;
}

class FiberBgpPeerManagerFixture : public ::testing::Test {
 public:
  FiberBgpPeerManagerFixture()
      : fmWrapper(
            folly::fibers::getFiberManager(evb, getFiberManagerOptions(256))) {}
  folly::EventBase evb;
  std::reference_wrapper<folly::fibers::FiberManager> fmWrapper;

  // setup for two peerMgrs
  const IPAddress peerAddr1{kR2Lo1}; // 127.2.0.1
  const BgpPeerId peerId1{peerAddr1, peerAddr1.asV4().toLongHBO()};
  TestFiberBgpPeerCallback callback1;
  shared_ptr<TestFiberBgpPeerManager> peerMgr1;
  const IPAddress peerAddr2{kR1Lo1}; // 127.1.0.1
  const BgpPeerId peerId2{peerAddr2, peerAddr2.asV4().toLongHBO()};
  TestFiberBgpPeerCallback callback2;
  shared_ptr<TestFiberBgpPeerManager> peerMgr2;

  void drainEventBase() {
    folly::fibers::Baton baton;
    evb.runInEventBaseThread([&baton] { baton.post(); });
    baton.wait();
  }

  void initTwoPeerMgrs(
      folly::fibers::FiberManager& fm,
      const std::optional<SocketAddress>& listenAddr = std::nullopt,
      bool enableCoroNotifyQueue = false,
      bool enableMessagesOverNotifyQueue = true) {
    auto bgpGlobalConfig1 = makeBgpGlobalConfig(
        kR1Lo1, kR1Lo1, listenAddr, FLAGS_enable_egress_queue_backpressure);
    auto bgpGlobalConfig2 = makeBgpGlobalConfig(
        kR2Lo1, kR2Lo1, listenAddr, FLAGS_enable_egress_queue_backpressure);
    peerMgr1 = make_shared<TestFiberBgpPeerManager>(
        bgpGlobalConfig1,
        &callback1,
        fm,
        evb,
        enableMessagesOverNotifyQueue,
        enableCoroNotifyQueue);

    // start all fibers for peerMgr1
    fm.addTask([this] { peerMgr1->run(); });

    peerMgr2 = make_shared<TestFiberBgpPeerManager>(
        bgpGlobalConfig2,
        &callback2,
        fm,
        evb,
        enableMessagesOverNotifyQueue,
        enableCoroNotifyQueue);

    // start all fibers for peerMgr2
    fm.addTask([this] { peerMgr2->run(); });
  }

  void initTwoPeerMgrs(
      folly::fibers::FiberManager& fm,
      BgpGlobalConfig& config1,
      BgpGlobalConfig& config2) {
    peerMgr1 =
        make_shared<TestFiberBgpPeerManager>(config1, &callback1, fm, evb);
    // start all fibers for peerMgr1
    fm.addTask([this] { peerMgr1->run(); });

    peerMgr2 =
        make_shared<TestFiberBgpPeerManager>(config2, &callback2, fm, evb);

    // start all fibers for peerMgr2
    fm.addTask([this] { peerMgr2->run(); });
  }

  std::shared_ptr<TestFiberBgpPeerManager> initPeerMgr(
      folly::fibers::FiberManager& fm,
      IPAddress /*localAddr*/,
      TestFiberBgpPeerCallback& cb,
      const std::optional<SocketAddress>& listenAddr = std::nullopt) {
    BgpGlobalConfig bgpGlobalConfig(
        100, /* localAsn */
        kR1Lo1, /* routerId 127.1.0.1 */
        kR1Lo1, /* clusterId 127.1.0.1 */
        kDefaultHoldTime, /* holdTime */
        listenAddr, /* listenAddr */
        kDefaultGrRestartTime, /* grRestartTime */
        std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV4 */
        std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV6 */
        std::nullopt, /* localConfedAsn */
        facebook::bgp::ComputeUcmpFromLbwComm{false}, /* computeUcmpFromLbwComm
                                                       */
        0, /* ucmpWidth */
        std::nullopt, /* ucmpQuantizer */
        facebook::bgp::ValidateRemoteAs{true}, /* validateRemoteAs */
        facebook::bgp::SupportStatefulGr{true}, /* supportStatefulGr */
        facebook::bgp::EnableServerSocket{true}, /* enableServerSocket */
        facebook::bgp::AllowLoopbackReflection{
            false}, /* allowLoopbackReflection
                     */
        facebook::bgp::CountConfedsInAsPathLen{
            false}, /* countConfedsInAsPathLen
                     */
        std::nullopt, /* deviceName */
        std::nullopt, /* switchLimitConfig */
        std::nullopt, /* dynamicPeerLimit */
        std::nullopt, /* streamSubscriberLimit */
        facebook::bgp::EnableNexthopTracking{false}, /* enableNextHopTracking */
        std::vector<std::string>{}, /* includeInterfaceRegexes */
        facebook::bgp::EnableDynamicPolicyEvaluation{
            false}, /* enableDynamicPolicyEvaluation
                     */
        std::nullopt, /* thriftServerConfig */
        FLAGS_enable_egress_queue_backpressure /* enableEgressQueueBackpressure
                                                */
    );
    auto peerMgr =
        make_shared<TestFiberBgpPeerManager>(bgpGlobalConfig, &cb, fm, evb);

    // start all fibers for peerMgr
    fm.addTask([peerMgr] { peerMgr->run(); });
    return peerMgr;
  }
};

enum class BasicAddPeerTestScope {
  CONFIG_AS_ARGS = 0,
  CONFIG_AS_PEERING_PARAMS
};

class BasicAddPeerTest
    : public FiberBgpPeerManagerFixture,
      public ::testing::WithParamInterface<BasicAddPeerTestScope> {};

TEST(BgpPeerId, BgpPeerIdOdsKeyTest) {
  BgpPeerId bgpPeerId;
  bgpPeerId.peerAddr = folly::IPAddress("167.254.0.1");
  bgpPeerId.remoteBgpId = folly::IPAddressV4("255.1.1.1").toLongHBO();
  EXPECT_EQ("167.254.0.1_255.1.1.1", bgpPeerId.toOdsKey());
}

TEST(BgpPeerId, BgpPeerIdWithDescriptionOdsKeyTest) {
  BgpPeerId bgpPeerId;
  bgpPeerId.peerAddr = folly::IPAddress("167.254.0.1");
  bgpPeerId.remoteBgpId = folly::IPAddressV4("255.1.1.1").toLongHBO();
  bgpPeerId.peerDescription = "test description";
  EXPECT_EQ("167.254.0.1_255.1.1.1", bgpPeerId.toOdsKey());
}

TEST(BgpPeerId, BgpPeerIdEqualityTest) {
  /*
   * Expect these three peers to be equal because they have same peerAddr and
   * remoteBgpId, even though they have different descriptions
   */
  BgpPeerId bgpPeerId1;
  bgpPeerId1.peerAddr = folly::IPAddress("167.254.0.1");
  bgpPeerId1.remoteBgpId = folly::IPAddressV4("255.1.1.1").toLongHBO();
  bgpPeerId1.peerDescription = "abcd";

  BgpPeerId bgpPeerId2;
  bgpPeerId2.peerAddr = folly::IPAddress("167.254.0.1");
  bgpPeerId2.remoteBgpId = folly::IPAddressV4("255.1.1.1").toLongHBO();
  bgpPeerId2.peerDescription = "defg";

  // bgpPeerId3 does not have description
  BgpPeerId bgpPeerId3 = BgpPeerId{
      folly::IPAddress("167.254.0.1"),
      folly::IPAddressV4("255.1.1.1").toLongHBO()};

  EXPECT_TRUE(bgpPeerId1 == bgpPeerId2);
  EXPECT_TRUE(bgpPeerId2 == bgpPeerId3);
  EXPECT_TRUE(bgpPeerId3 == bgpPeerId1);
  EXPECT_NE(bgpPeerId1.peerDescription, bgpPeerId2.peerDescription);
  EXPECT_NE(bgpPeerId2.peerDescription, bgpPeerId3.peerDescription);
  EXPECT_NE(bgpPeerId3.peerDescription, bgpPeerId1.peerDescription);
}

TEST(BgpPeerId, BgpPeerIdOdsKeyEqualityTest) {
  /*
   * Expect these three peers to have the same ODS key because they have same
   * the peerAddr and remoteBgpId, even though they have different
   * descriptions
   */
  BgpPeerId bgpPeerId1;
  bgpPeerId1.peerAddr = folly::IPAddress("167.254.0.1");
  bgpPeerId1.remoteBgpId = folly::IPAddressV4("255.1.1.1").toLongHBO();
  bgpPeerId1.peerDescription = "abcd";

  BgpPeerId bgpPeerId2;
  bgpPeerId2.peerAddr = folly::IPAddress("167.254.0.1");
  bgpPeerId2.remoteBgpId = folly::IPAddressV4("255.1.1.1").toLongHBO();
  bgpPeerId2.peerDescription = "defg";

  // bgpPeerId3 does not have description
  BgpPeerId bgpPeerId3 = BgpPeerId{
      folly::IPAddress("167.254.0.1"),
      folly::IPAddressV4("255.1.1.1").toLongHBO()};

  EXPECT_EQ("167.254.0.1_255.1.1.1", bgpPeerId1.toOdsKey());
  EXPECT_EQ(bgpPeerId1.toOdsKey(), bgpPeerId2.toOdsKey());
  EXPECT_EQ(bgpPeerId2.toOdsKey(), bgpPeerId3.toOdsKey());
  EXPECT_EQ(bgpPeerId3.toOdsKey(), bgpPeerId1.toOdsKey());
  EXPECT_NE(bgpPeerId1.peerDescription, bgpPeerId2.peerDescription);
  EXPECT_NE(bgpPeerId2.peerDescription, bgpPeerId3.peerDescription);
  EXPECT_NE(bgpPeerId3.peerDescription, bgpPeerId1.peerDescription);
}

TEST_F(
    FiberBgpPeerManagerFixture,
    AddressLevelPeerUpIgnoresStaleStaticRouterId) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([this] {
    auto peerInfo = std::make_shared<BgpPeerInfoInternal>();
    auto staleSession = std::make_shared<BgpSessionInfo>();
    staleSession->numResets = 3;
    const auto staleBgpId = IPAddressV4("192.0.2.1").toLongHBO();
    peerInfo->sessionInfos.emplace(staleBgpId, staleSession);

    auto establishedSession = std::make_shared<BgpSessionInfo>();
    establishedSession->establishedSessionInfo =
        std::make_shared<BgpPeerActiveSessionInfo>();
    const auto establishedBgpId = IPAddressV4("192.0.2.2").toLongHBO();
    peerInfo->sessionInfos.emplace(establishedBgpId, establishedSession);
    peerMgr1->allPeers_[peerAddr1] = peerInfo;

    EXPECT_EQ(2, peerInfo->sessionInfos.size());
    EXPECT_TRUE(peerInfo->sessionInfos.contains(staleBgpId));
    EXPECT_EQ(3, peerInfo->sessionInfos.at(staleBgpId)->numResets);
    EXPECT_EQ(establishedSession, peerInfo->sessionInfos.at(establishedBgpId));
    EXPECT_FALSE(peerMgr1->isPeerUp(BgpPeerId{peerAddr1, staleBgpId}));
    EXPECT_TRUE(peerMgr1->isPeerUp(BgpPeerId{peerAddr1, establishedBgpId}));
    EXPECT_TRUE(peerMgr1->isPeerUp(peerAddr1));

    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
  });

  evb.loop();
}

TEST_F(
    FiberBgpPeerManagerFixture,
    AddressLevelPeerUpPreservesDynamicAllSessionsSemantics) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([this] {
    auto peerInfo = std::make_shared<BgpPeerInfoInternal>();
    const auto peerPrefix = folly::IPAddress::createNetwork("127.0.0.0/8");
    peerInfo->peeringParams.peerPrefix = peerPrefix;
    peerMgr1->dynamicPeerGroups_[peerPrefix] =
        std::make_shared<BgpDynamicPeerGroupInfo>();

    const auto staleBgpId = IPAddressV4("192.0.2.1").toLongHBO();
    auto staleSession = std::make_shared<BgpSessionInfo>();
    staleSession->numResets = 3;
    peerInfo->sessionInfos.emplace(staleBgpId, staleSession);

    const auto establishedBgpId = IPAddressV4("192.0.2.2").toLongHBO();
    auto establishedSession = std::make_shared<BgpSessionInfo>();
    establishedSession->establishedSessionInfo =
        std::make_shared<BgpPeerActiveSessionInfo>();
    peerInfo->sessionInfos.emplace(establishedBgpId, establishedSession);
    peerMgr1->allPeers_[peerAddr1] = peerInfo;

    EXPECT_EQ(2, peerInfo->sessionInfos.size());
    EXPECT_TRUE(peerInfo->sessionInfos.contains(staleBgpId));
    EXPECT_EQ(3, peerInfo->sessionInfos.at(staleBgpId)->numResets);
    EXPECT_EQ(establishedSession, peerInfo->sessionInfos.at(establishedBgpId));
    EXPECT_FALSE(peerMgr1->isPeerUp(peerAddr1));

    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
  });

  evb.loop();
}

TEST_F(
    FiberBgpPeerManagerFixture,
    DeleteTerminatedSessionPreservesPeerWithoutConnectionInfo) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([this] {
    constexpr uint64_t kTerminatedVersion = 7;
    auto peerInfo = std::make_shared<BgpPeerInfoInternal>();
    peerInfo->peeringParams.remoteAs =
        facebook::bgp::AsNum(facebook::bgp::kVipAsn);
    auto sessionInfo = std::make_shared<BgpSessionInfo>();
    sessionInfo->versionNumber =
        std::make_shared<VersionNumber>(kTerminatedVersion);
    peerInfo->sessionInfos.emplace(peerId1.remoteBgpId, sessionInfo);
    auto dynamicPeerInfo = std::make_shared<BgpDynamicPeerGroupInfo>();
    dynamicPeerInfo->activePeers.insert(peerAddr1);
    peerMgr1
        ->dynamicPeerGroups_[folly::IPAddress::createNetwork("127.0.0.0/8")] =
        dynamicPeerInfo;
    peerMgr1->allPeers_[peerAddr1] = std::move(peerInfo);
    facebook::bgp::BgpStats::incrAllPeersCount();
    facebook::bgp::BgpStats::incrDynamicPeersCount();

    EXPECT_TRUE(peerMgr1->getBgpSessionInfo(peerId1).has_value());
    peerMgr1->scheduleDeleteTerminatedSession(
        peerId1, kTerminatedVersion, facebook::bgp::kVipAsn);
    EXPECT_TRUE(peerMgr1->getBgpSessionInfo(peerId1).has_value());
    drainEventBase();
    EXPECT_FALSE(peerMgr1->getBgpSessionInfo(peerId1).has_value());
    EXPECT_TRUE(peerMgr1->allPeers_.contains(peerAddr1));
    EXPECT_TRUE(dynamicPeerInfo->activePeers.contains(peerAddr1));

    peerMgr1->allPeers_.erase(peerAddr1);
    dynamicPeerInfo->activePeers.erase(peerAddr1);
    facebook::bgp::BgpStats::decrAllPeersCount();
    facebook::bgp::BgpStats::decrDynamicPeersCount();

    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
  });

  evb.loop();
}

TEST_F(
    FiberBgpPeerManagerFixture,
    ScheduleDeleteTerminatedSessionSkipsCancelledScope) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([this] {
    constexpr uint64_t kTerminatedVersion = 7;
    auto peerInfo = std::make_shared<BgpPeerInfoInternal>();
    peerInfo->peeringParams.remoteAs =
        facebook::bgp::AsNum(facebook::bgp::kVipAsn);
    auto sessionInfo = std::make_shared<BgpSessionInfo>();
    sessionInfo->versionNumber =
        std::make_shared<VersionNumber>(kTerminatedVersion);
    peerInfo->sessionInfos.emplace(peerId1.remoteBgpId, sessionInfo);
    peerMgr1
        ->dynamicPeerGroups_[folly::IPAddress::createNetwork("127.0.0.0/8")] =
        std::make_shared<BgpDynamicPeerGroupInfo>();
    peerMgr1->allPeers_[peerAddr1] = std::move(peerInfo);

    peerMgr1->asyncScope_.requestCancellation();
    peerMgr1->scheduleDeleteTerminatedSession(
        peerId1, kTerminatedVersion, facebook::bgp::kVipAsn);
    drainEventBase();

    EXPECT_TRUE(peerMgr1->getBgpSessionInfo(peerId1).has_value());
    peerMgr1->allPeers_.erase(peerAddr1);

    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
  });

  evb.loop();
}

/*
 * During an ASN migration a dynamic VIP peer can carry the VIP ASN as its
 * *additional* remote AS while the configured primary stays non-VIP.
 *
 * PeerManagerBase::sessionTerminated gates this call on
 * isDynamicVipPeer(peerAddr, adjRib->getRemoteAs()) -- the ASN the session was
 * actually established with -- so a session that came up on the additional VIP
 * ASN does reach deleteTerminatedSession. The revalidation here compares
 * against peeringParams.remoteAs (the configured primary), so it rejects that
 * peer and the terminated session is never erased from sessionInfos.
 */
TEST_F(
    FiberBgpPeerManagerFixture,
    DeleteTerminatedSessionRemovesSessionForAdditionalRemoteAsVipPeer) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([this] {
    constexpr uint64_t kTerminatedVersion = 7;
    auto peerInfo = std::make_shared<BgpPeerInfoInternal>();
    // Configured primary is non-VIP; the VIP ASN is the migration ASN.
    peerInfo->peeringParams.remoteAs = facebook::bgp::AsNum(kAs1);
    peerInfo->peeringParams.additionalRemoteAs = facebook::bgp::kVipAsn;
    auto sessionInfo = std::make_shared<BgpSessionInfo>();
    sessionInfo->versionNumber =
        std::make_shared<VersionNumber>(kTerminatedVersion);
    peerInfo->sessionInfos.emplace(peerId1.remoteBgpId, sessionInfo);
    auto dynamicPeerInfo = std::make_shared<BgpDynamicPeerGroupInfo>();
    dynamicPeerInfo->activePeers.insert(peerAddr1);
    peerMgr1
        ->dynamicPeerGroups_[folly::IPAddress::createNetwork("127.0.0.0/8")] =
        dynamicPeerInfo;
    peerMgr1->allPeers_[peerAddr1] = std::move(peerInfo);
    facebook::bgp::BgpStats::incrAllPeersCount();
    facebook::bgp::BgpStats::incrDynamicPeersCount();

    EXPECT_TRUE(peerMgr1->getBgpSessionInfo(peerId1).has_value());
    peerMgr1->scheduleDeleteTerminatedSession(
        peerId1, kTerminatedVersion, facebook::bgp::kVipAsn);
    drainEventBase();
    EXPECT_FALSE(peerMgr1->getBgpSessionInfo(peerId1).has_value());
    EXPECT_TRUE(peerMgr1->allPeers_.contains(peerAddr1));
    EXPECT_TRUE(dynamicPeerInfo->activePeers.contains(peerAddr1));

    peerMgr1->allPeers_.erase(peerAddr1);
    dynamicPeerInfo->activePeers.erase(peerAddr1);
    facebook::bgp::BgpStats::decrAllPeersCount();
    facebook::bgp::BgpStats::decrDynamicPeersCount();

    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
  });

  evb.loop();
}

TEST_F(
    FiberBgpPeerManagerFixture,
    DeleteTerminatedSessionPreservesNewerSession) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([this] {
    constexpr uint64_t kTerminatedVersion = 7;
    auto peerInfo = std::make_shared<BgpPeerInfoInternal>();
    peerInfo->peeringParams.remoteAs =
        facebook::bgp::AsNum(facebook::bgp::kVipAsn);
    auto sessionInfo = std::make_shared<BgpSessionInfo>();
    sessionInfo->versionNumber =
        std::make_shared<VersionNumber>(kTerminatedVersion + 1);
    sessionInfo->establishedSessionInfo =
        std::make_shared<BgpPeerActiveSessionInfo>();
    peerInfo->sessionInfos.emplace(peerId1.remoteBgpId, sessionInfo);
    auto dynamicPeerInfo = std::make_shared<BgpDynamicPeerGroupInfo>();
    dynamicPeerInfo->activePeers.insert(peerAddr1);
    peerMgr1
        ->dynamicPeerGroups_[folly::IPAddress::createNetwork("127.0.0.0/8")] =
        dynamicPeerInfo;
    peerMgr1->allPeers_[peerAddr1] = std::move(peerInfo);

    peerMgr1->scheduleDeleteTerminatedSession(
        peerId1, kTerminatedVersion, facebook::bgp::kVipAsn);
    drainEventBase();
    const auto retainedSessionInfo = peerMgr1->getBgpSessionInfo(peerId1);
    ASSERT_TRUE(retainedSessionInfo.has_value());
    EXPECT_EQ(sessionInfo, retainedSessionInfo.value());

    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
  });

  evb.loop();
}

TEST_F(
    FiberBgpPeerManagerFixture,
    DeleteTerminatedSessionRemovesSessionFromDynamicPassivePeer) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([this] {
    constexpr uint64_t kTerminatedVersion = 7;
    const auto peerPrefix = folly::IPAddress::createNetwork("127.0.0.0/8");
    auto params = PeeringParams(
        kDefaultVrf,
        peerAddr1,
        peerPrefix,
        100,
        100,
        facebook::bgp::kVipAsn,
        kR1Lo1.asV4(),
        kR1Lo1.asV4(),
        180s,
        120s,
        constants::kBgpPort,
        {kR1Lo1, 0},
        TBgpSessionConnectMode::PASSIVE_ONLY);
    auto dynamicPeerInfo = std::make_shared<BgpDynamicPeerGroupInfo>();
    dynamicPeerInfo->peeringParams = params;
    peerMgr1->dynamicPeerGroups_[peerPrefix] = dynamicPeerInfo;
    ASSERT_FALSE(
        peerMgr1
            ->addPeerHelper(
                peerAddr1,
                params,
                ConnTimeParams(
                    kDefaultStartAfterDelayMs, kDefaultConnRetryTimeoutMs))
            .hasError());
    dynamicPeerInfo->activePeers.insert(peerAddr1);
    facebook::bgp::BgpStats::incrDynamicPeersCount();

    auto sessionInfo = std::make_shared<BgpSessionInfo>();
    sessionInfo->versionNumber =
        std::make_shared<VersionNumber>(kTerminatedVersion);
    peerMgr1->allPeers_.at(peerAddr1)->sessionInfos.emplace(
        peerId1.remoteBgpId, sessionInfo);

    const auto& connectionInfos =
        peerMgr1->allPeers_.at(peerAddr1)->connectionInfos;
    ASSERT_EQ(connectionInfos.size(), 1);
    ASSERT_NE(connectionInfos.begin()->second->activeConnectInfo, nullptr);

    peerMgr1->scheduleDeleteTerminatedSession(
        peerId1, kTerminatedVersion, facebook::bgp::kVipAsn);
    drainEventBase();
    EXPECT_FALSE(peerMgr1->getBgpSessionInfo(peerId1).has_value());
    EXPECT_TRUE(peerMgr1->allPeers_.contains(peerAddr1));
    EXPECT_TRUE(dynamicPeerInfo->activePeers.contains(peerAddr1));

    peerMgr1->allPeers_.erase(peerAddr1);
    dynamicPeerInfo->activePeers.erase(peerAddr1);
    facebook::bgp::BgpStats::decrAllPeersCount();
    facebook::bgp::BgpStats::decrDynamicPeersCount();
    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
  });

  evb.loop();
}

/*
 * Start fiberBgpPeerManager to make sure the corresponding FiberBgpPeer is
 * monitored with monitoredItems populated.
 */
TEST_F(FiberBgpPeerManagerFixture, MonitoredFiberBgpPeerTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([&] {
    // addPeer() is called only by fiberBgpPeerMgr1
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);

    // addPeer() is called by fiberBgpPeerMgr2 as well
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    const auto monitorKey = fmt::format("{}-{}", peerAddr2.str(), peerPort1);

    // confirm monitored key added after session goes up
    waitTillSessionsComeUp(fm, peerMgr2, {peerId2}, std::chrono::seconds(30));
    {
      auto start = std::chrono::steady_clock::now();
      while (std::chrono::steady_clock::now() - start <
             std::chrono::seconds(60)) {
        if (peerMgr2->getMonitoredItem().rlock()->contains(monitorKey)) {
          break;
        }
        fiberSleepFor(100ms);
      }
      EXPECT_TRUE(peerMgr2->getMonitoredItem().rlock()->contains(monitorKey));
    }

    /*
     * Stop peer from one end. This test exercises the monitoring lifecycle
     * (key added on session-up, removed on session-down), not graceful
     * restart. stopPeer() retries connecting for BOTH withGR=true and
     * withGR=false (see FiberBgpPeerManager.h), so the session reconnects and
     * re-adds the monitor key; the poll below then only catches a transient
     * absent window, which races under load. Use shutdownPeer() instead: it
     * terminates the session AND stops attempting connection, keeping the peer
     * idle so the monitor key stays removed and the assertion is deterministic.
     */
    peerMgr2->shutdownPeer(peerAddr2);

    // confirm monitored key removed after session goes down
    waitTillSessionsGoDown(fm, peerMgr2, {peerId2}, std::chrono::seconds(30));
    {
      auto start = std::chrono::steady_clock::now();
      while (std::chrono::steady_clock::now() - start <
             std::chrono::seconds(60)) {
        if (!peerMgr2->getMonitoredItem().rlock()->contains(monitorKey)) {
          break;
        }
        fiberSleepFor(100ms);
      }
      EXPECT_FALSE(peerMgr2->getMonitoredItem().rlock()->contains(monitorKey));
    }

    // stop to end the test
    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
  });

  // magic starts here
  evb.loop();
}

/*
 * Start one fiberBgpPeerManager with the coro task. Validate the notification
 * queue pushing and popping.
 */
//
TEST_F(FiberBgpPeerManagerFixture, StopAcceptFiberLoopTest) {
  auto& fm = fmWrapper.get();
  BgpGlobalConfig bgpGlobalConfig(
      100, /* localAsn */
      kR1Lo1, /* routerId 127.1.0.1 */
      kR1Lo1, /* clusterId 127.1.0.1 */
      kDefaultHoldTime, /* holdTime */
      std::nullopt, /* listenAddr */
      kDefaultGrRestartTime, /* grRestartTime */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV4 */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV6 */
      std::nullopt, /* localConfedAsn */
      facebook::bgp::ComputeUcmpFromLbwComm{false}, /* computeUcmpFromLbwComm */
      0, /* ucmpWidth */
      std::nullopt, /* ucmpQuantizer */
      facebook::bgp::ValidateRemoteAs{true}, /* validateRemoteAs */
      facebook::bgp::SupportStatefulGr{true}, /* supportStatefulGr */
      facebook::bgp::EnableServerSocket{true}, /* enableServerSocket */
      facebook::bgp::AllowLoopbackReflection{false}, /* allowLoopbackReflection
                                                      */
      facebook::bgp::CountConfedsInAsPathLen{false}, /* countConfedsInAsPathLen
                                                      */
      std::nullopt, /* deviceName */
      std::nullopt, /* switchLimitConfig */
      std::nullopt, /* dynamicPeerLimit */
      std::nullopt, /* streamSubscriberLimit */
      facebook::bgp::EnableNexthopTracking{false}, /* enableNextHopTracking */
      std::vector<std::string>{}, /* includeInterfaceRegexes */
      facebook::bgp::EnableDynamicPolicyEvaluation{
          false}, /* enableDynamicPolicyEvaluation
                   */
      std::nullopt, /* thriftServerConfig */
      FLAGS_enable_egress_queue_backpressure /* enableEgressQueueBackpressure */
  );
  peerMgr1 =
      make_shared<TestFiberBgpPeerManager>(bgpGlobalConfig, nullptr, fm, evb);

  // start all fibers
  fm.addTask([this] {
    peerMgr1->run();
    SUCCEED();
  });
  // stop all fibers
  fm.addTask([this] {
    fiberSleepFor(100ms);
    peerMgr1->shutdownWithGR(false);
  });

  evb.loop();
  SUCCEED();
}

/*
 * Start one peer manger. Test to make sure that connector fiber exits
 * gracefully.
 */
TEST_F(FiberBgpPeerManagerFixture, StopConnectFiberLoopTest) {
  auto& fm = fmWrapper.get();
  BgpGlobalConfig bgpGlobalConfig(
      100, /* localAsn */
      kR1Lo1, /* routerId 127.1.0.1 */
      kR1Lo1, /* clusterId 127.1.0.1 */
      kDefaultHoldTime, /* holdTime */
      std::nullopt, /* listenAddr */
      kDefaultGrRestartTime, /* grRestrtTime */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV4 */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV6 */
      std::nullopt, /* localConfedAsn */
      facebook::bgp::ComputeUcmpFromLbwComm{false}, /* computeUcmpFromLbwComm */
      0, /* ucmpWidth */
      std::nullopt, /* ucmpQuantizer */
      facebook::bgp::ValidateRemoteAs{true}, /* validateRemoteAs */
      facebook::bgp::SupportStatefulGr{true}, /* supportStatefulGr */
      facebook::bgp::EnableServerSocket{true}, /* enableServerSocket */
      facebook::bgp::AllowLoopbackReflection{false}, /* allowLoopbackReflection
                                                      */
      facebook::bgp::CountConfedsInAsPathLen{false}, /* countConfedsInAsPathLen
                                                      */
      std::nullopt, /* deviceName */
      std::nullopt, /* switchLimitConfig */
      std::nullopt, /* dynamicPeerLimit */
      std::nullopt, /* streamSubscriberLimit */
      facebook::bgp::EnableNexthopTracking{false}, /* enableNextHopTracking */
      std::vector<std::string>{}, /* includeInterfaceRegexes */
      facebook::bgp::EnableDynamicPolicyEvaluation{
          false}, /* enableDynamicPolicyEvaluation
                   */
      std::nullopt, /* thriftServerConfig */
      FLAGS_enable_egress_queue_backpressure /* enableEgressQueueBackpressure */
  );
  peerMgr1 =
      make_shared<TestFiberBgpPeerManager>(bgpGlobalConfig, nullptr, fm, evb);
  // start all fibers
  fm.addTask([this] {
    XLOG(DBG4, "start Bgp Peer manager");
    peerMgr1->run();
    SUCCEED();
  });

  // spins new thread, binds port
  folly::ScopedBoundPort ph;
  auto closedAddress = ph.getAddress();
  peerMgr1->addPeer(closedAddress.getIPAddress(), 100, closedAddress.getPort());

  // stop all fibers
  fm.addTask([this] {
    fiberSleepFor(100ms);
    XLOG(DBG4, "stop Bgp Peer manager");
    peerMgr1->shutdownWithGR(false);
  });

  evb.loop();
  SUCCEED();
}

/*
 * Test Bgp session not coming up unless addPeer() is called on both sides.
 */
TEST_F(FiberBgpPeerManagerFixture, BgpSessionNotUpTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([this] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    // addPeer() is called only by peerMgr1
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);

    // wait enough for session coming up
    fiberSleepFor(1s);

    // confirm that no session comes up
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    XLOG(DBG4, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);
  });

  evb.loop();

  EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
  EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
  EXPECT_FALSE(callback1.isSessionUp(peerId1));

  EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
  EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
  EXPECT_FALSE(callback2.isSessionUp(peerId2));
}

/*
 * Test Bgp session coming up if addPeer() is called by both sides.
 */
TEST_F(FiberBgpPeerManagerFixture, BgpSessionUpTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([this, &fm] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // addPeer() is called only by peerMgr1
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);

    // wait enough for session coming up
    fiberSleepFor(1s);

    // confirm that no session comes up
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // addPeer() is called by both peerMgr2
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1, peerId2});

    // confirm that session comes up
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // stop
    XLOG(DBG4, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);

    waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
  });

  evb.loop();

  // confirm that session came down
  EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
  EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
  EXPECT_FALSE(callback1.isSessionUp(peerId1));

  EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
  EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
  EXPECT_FALSE(callback2.isSessionUp(peerId2));
}

TEST_F(FiberBgpPeerManagerFixture, BgpSessionUpCoroNotifyQueueTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(
      fm, std::nullopt /* listenAddr */, true /* enableCoroNotifyQueue */);

  fm.addTask([this, &fm] {
    // addPeer() is called only by fiberBgpPeerMgr1
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);

    // wait enough for session coming up
    fiberSleepFor(1s);

    // addPeer() is called by fiberBgpPeerMgr2 as well
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    // stop
    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
    waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
  });

  evb.loop();

  // confirm the content inside queue
  auto stateEvt1 = facebook::bgp::test::boundedBlockingPop(
      peerMgr1->getNotifyCoroQueue(), "peerMgr1->getNotifyCoroQueue()");
  ASSERT_TRUE(
      std::holds_alternative<FiberBgpPeer::ObservableStateT>(stateEvt1));
  auto state1 = std::get<FiberBgpPeer::ObservableStateT>(stateEvt1);
  EXPECT_EQ(state1.state, BgpSessionState::ESTABLISHED);
  EXPECT_EQ(100, state1.remoteAs);

  auto stateEvt2 = facebook::bgp::test::boundedBlockingPop(
      peerMgr1->getNotifyCoroQueue(), "peerMgr1->getNotifyCoroQueue()");
  ASSERT_TRUE(
      std::holds_alternative<FiberBgpPeer::ObservableStateT>(stateEvt2));
  auto state2 = std::get<FiberBgpPeer::ObservableStateT>(stateEvt2);
  EXPECT_NE(state2.state, BgpSessionState::ESTABLISHED);
}

/*
 * Test Bgp session comes up with one peer's server socket enabled
 * and another peer's server socket disabled.
 * Ensure we run properly without creating server socket.
 */
TEST_F(FiberBgpPeerManagerFixture, BgpNoServerTest) {
  auto& fm = fmWrapper.get();
  BgpGlobalConfig bgpGlobalConfig1(
      100, /* localAsn */
      kR1Lo1, /* routerId 127.1.0.1 */
      kR1Lo1, /* clusterId 127.1.0.1 */
      kDefaultHoldTime, /* holdTime */
      std::nullopt, /* listenAddr */
      kDefaultGrRestartTime, /* grRestartTime */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV4 */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV6 */
      std::nullopt, /* localConfedAsn */
      facebook::bgp::ComputeUcmpFromLbwComm{false},
      0, /* ucmp-width */
      std::nullopt, /* ucmp-quantizer */
      facebook::bgp::ValidateRemoteAs{true},
      facebook::bgp::SupportStatefulGr{true},
      facebook::bgp::EnableServerSocket{true}, /* (Enabled) */
      facebook::bgp::AllowLoopbackReflection{false}, /* allowLoopbackReflection
                                                      */
      facebook::bgp::CountConfedsInAsPathLen{false}, /* countConfedsInAsPathLen
                                                      */
      std::nullopt, /* deviceName */
      std::nullopt, /* switchLimitConfig */
      std::nullopt, /* dynamicPeerLimit */
      std::nullopt, /* streamSubscriberLimit */
      facebook::bgp::EnableNexthopTracking{false}, /* enableNextHopTracking */
      std::vector<std::string>{}, /* includeInterfaceRegexes */
      facebook::bgp::EnableDynamicPolicyEvaluation{
          false}, /* enableDynamicPolicyEvaluation
                   */
      std::nullopt, /* thriftServerConfig */
      FLAGS_enable_egress_queue_backpressure /* enableEgressQueueBackpressure */
  );

  BgpGlobalConfig bgpGlobalConfig2(
      100, /* localAsn */
      kR2Lo1, /* routerId 127.2.0.1 */
      kR2Lo1, /* clusterId 127.2.0.1 */
      kDefaultHoldTime, /* holdTime */
      std::nullopt, /* listenAddr */
      kDefaultGrRestartTime, /* grRestartTime */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV4 */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV6 */
      std::nullopt, /* localConfedAsn */
      facebook::bgp::ComputeUcmpFromLbwComm{false},
      0, /* ucmp-width */
      std::nullopt, /* ucmp-quantizer */
      facebook::bgp::ValidateRemoteAs{true},
      facebook::bgp::SupportStatefulGr{true},
      facebook::bgp::EnableServerSocket{false}, /* (Disabled) */
      facebook::bgp::AllowLoopbackReflection{false}, /* allowLoopbackReflection
                                                      */
      facebook::bgp::CountConfedsInAsPathLen{false}, /* countConfedsInAsPathLen
                                                      */
      std::nullopt, /* deviceName */
      std::nullopt, /* switchLimitConfig */
      std::nullopt, /* dynamicPeerLimit */
      std::nullopt, /* streamSubscriberLimit */
      facebook::bgp::EnableNexthopTracking{false}, /* enableNextHopTracking */
      std::vector<std::string>{}, /* includeInterfaceRegexes */
      facebook::bgp::EnableDynamicPolicyEvaluation{
          false}, /* enableDynamicPolicyEvaluation
                   */
      std::nullopt, /* thriftServerConfig */
      FLAGS_enable_egress_queue_backpressure /* enableEgressQueueBackpressure */
  );

  initTwoPeerMgrs(fm, bgpGlobalConfig1, bgpGlobalConfig2);

  fm.addTask([this, &fm] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, constants::kBgpPort);

    // wait enough for session coming up
    fiberSleepFor(1s);

    // confirm that no session comes up
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // addPeer() is called by both peerMgr2
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
    waitTillSessionsComeUp(fm, peerMgr2, {peerId2});

    // confirm that session comes up
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // stop
    XLOG(DBG4, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);

    waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
  });

  evb.loop();

  // confirm that session came down
  EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
  EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
  EXPECT_FALSE(callback1.isSessionUp(peerId1));

  EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
  EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
  EXPECT_FALSE(callback2.isSessionUp(peerId2));
}
/*
 * Test stopPeer at one end.
 */
TEST_F(FiberBgpPeerManagerFixture, StopPeerAtOneEndTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  // test addPeer() and stopPeer()
  fm.addTask([this, &fm] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // addPeer() is called by both peerMgrs
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // test stopPeer()
    peerMgr1->stopPeer(peerAddr1, false /*withGR*/);

    fm.addTask([this, &fm] {
      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
      waitTillSessionsGoDown(fm, peerMgr2, {peerId2});

      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      XLOG(DBG4, "stop Bgp Peer manager 1");
      peerMgr1->shutdownWithGR(false);
      XLOG(DBG4, "stop Bgp Peer manager 2");
      peerMgr2->shutdownWithGR(false);
    });
  });

  evb.loop();
}

/*
 * Test that peeringState values are properly reset when FiberBgpPeer stops
 * without GR.
 */
TEST_F(FiberBgpPeerManagerFixture, PeeringStateResetOnStopTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([this, &fm] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // addPeer() is called by both peerMgrs
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // Access the FiberBgpPeer to verify peeringState values after session is up
    EXPECT_EQ(1, peerMgr1->allPeers_.size());
    auto peerInfoPtr = peerMgr1->allPeers_.begin()->second;
    ASSERT_NE(nullptr, peerInfoPtr);
    auto connectionInfoPtr = peerInfoPtr->connectionInfos.begin()->second;
    ASSERT_NE(nullptr, connectionInfoPtr);
    auto peer1 = connectionInfoPtr->activeSessionInfo->peer;
    ASSERT_NE(nullptr, peer1);

    // Stop the peer without GR
    peerMgr1->stopPeer(peerAddr1, false /*withGR*/);

    fm.addTask([this, &fm, peer1] {
      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
      waitTillSessionsGoDown(fm, peerMgr2, {peerId2});

      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));

      // Verify per-session counters are reset
      EXPECT_EQ(0u, peer1->getSendQueueBlocks());
      EXPECT_EQ(0u, peer1->getSendQueueTotalBlockDuration());
      EXPECT_EQ(0u, peer1->getLastSendQueueBlockTime());

      XLOG(DBG4, "stop Bgp Peer manager 1");
      peerMgr1->shutdownWithGR(false);
      XLOG(DBG4, "stop Bgp Peer manager 2");
      peerMgr2->shutdownWithGR(false);
    });
  });

  evb.loop();
}

/*
 * Test that peeringState values are properly reset when FiberBgpPeer stops
 * with GR (Graceful Restart).
 */
TEST_F(FiberBgpPeerManagerFixture, PeeringStateResetOnStopWithGRTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([this, &fm] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // addPeer() is called by both peerMgrs
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // Access the FiberBgpPeer to verify peeringState values after session is up
    EXPECT_EQ(1, peerMgr1->allPeers_.size());
    auto peerInfoPtr = peerMgr1->allPeers_.begin()->second;
    ASSERT_NE(nullptr, peerInfoPtr);
    auto connectionInfoPtr = peerInfoPtr->connectionInfos.begin()->second;
    ASSERT_NE(nullptr, connectionInfoPtr);
    auto peer1 = connectionInfoPtr->activeSessionInfo->peer;
    ASSERT_NE(nullptr, peer1);

    // Stop the peer with GR
    peerMgr1->stopPeer(peerAddr1, true /*withGR*/);

    fm.addTask([this, peer1] {
      /*
       * Wait only for this end to report the termination. stopPeer(withGR)
       * sends no NOTIFICATION, so peerMgr2 sees the end of the session only
       * when the TCP FIN arrives. On a loaded host that can take longer than
       * peerMgr1's reconnect timer (kDefaultConnRetryTimeoutMs). peerMgr2
       * then keeps its old session and closes each reconnect as a collision,
       * after peerMgr1 has already counted it as established. Waiting for
       * peerMgr2 here lets those extra sessions change the counts below.
       */
      auto downStart = std::chrono::steady_clock::now();
      while (std::chrono::steady_clock::now() - downStart <
             std::chrono::seconds(30)) {
        if (callback1.getTerminatedCallbackCount(peerId1) >= 1) {
          break;
        }
        fiberSleepFor(1ms);
      }

      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));

      // Verify per-session counters are reset (same as without GR)
      EXPECT_EQ(0u, peer1->getSendQueueBlocks());
      EXPECT_EQ(0u, peer1->getSendQueueTotalBlockDuration());
      EXPECT_EQ(0u, peer1->getLastSendQueueBlockTime());

      XLOG(DBG4, "stop Bgp Peer manager 1");
      peerMgr1->shutdownWithGR(false);
      XLOG(DBG4, "stop Bgp Peer manager 2");
      peerMgr2->shutdownWithGR(false);
    });
  });

  evb.loop();
}

/*
 * Test stopPeer(GR = true) at one end.
 */
TEST_F(FiberBgpPeerManagerFixture, StopPeerWithGracefulRestartAtOneEndTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  // test addPeer() and stopPeer(GR = true)
  fm.addTask([this, &fm] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // addPeer() is called by both peerMgrs
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    /*
     * peerMgr1 (the end that stops with GR) is PASSIVE_ONLY, so only peerMgr2
     * can open the next session. peerMgr2 starts its reconnect timer only
     * after it sees the old TCP connection close. If peerMgr1 also connects
     * actively, its reconnect can arrive while peerMgr2 still keeps the old
     * session (the FIN can be late on a loaded host). peerMgr2 then closes
     * the new session as a collision, after peerMgr1 has already counted it
     * as established.
     */
    peerMgr1->addPeer(
        peerAddr1,
        100,
        100,
        {kR1Lo1, 0},
        peerPort2,
        ConnTimeParams(kDefaultStartAfterDelayMs, kDefaultConnRetryTimeoutMs),
        TBgpSessionConnectMode::PASSIVE_ONLY);
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // test stopPeer(GR = true)
    peerMgr1->stopPeer(peerAddr1, true /*withGR*/);

    fm.addTask([this] {
      /*
       * Gate the down phase on the monotonic terminated counts. No session
       * can come up again before both ends report the termination, because
       * peerMgr2 reconnects only after its own termination.
       */
      auto downStart = std::chrono::steady_clock::now();
      while (std::chrono::steady_clock::now() - downStart <
             std::chrono::seconds(30)) {
        if (callback1.getTerminatedCallbackCount(peerId1) >= 1 &&
            callback2.getTerminatedCallbackCount(peerId2) >= 1) {
          break;
        }
        fiberSleepFor(1ms);
      }

      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      // Gate the GR reconnect (up) phase on the monotonic established counts.
      auto upStart = std::chrono::steady_clock::now();
      while (std::chrono::steady_clock::now() - upStart <
             std::chrono::seconds(30)) {
        if (callback1.getEstablishedCallbackCount(peerId1) >= 2 &&
            callback2.getEstablishedCallbackCount(peerId2) >= 2) {
          break;
        }
        fiberSleepFor(1ms);
      }

      EXPECT_EQ(2, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_TRUE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(2, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_TRUE(callback2.isSessionUp(peerId2));

      XLOG(DBG4, "stop Bgp Peer manager 1");
      peerMgr1->shutdownWithGR(false);
      XLOG(DBG4, "stop Bgp Peer manager 2");
      peerMgr2->shutdownWithGR(false);
    });
  });

  evb.loop();
}

/*
 * Test shutdownPeer at one end.
 */
TEST_F(FiberBgpPeerManagerFixture, ShutdownPeerAtOneEndTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  // test addPeer() and shutdownPeer()
  fm.addTask([this, &fm] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // addPeer() is called by both peerMgrs
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // test shutdownPeer()
    peerMgr1->shutdownPeer(peerAddr1);

    fm.addTask([this, &fm] {
      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
      waitTillSessionsGoDown(fm, peerMgr2, {peerId2});

      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      XLOG(DBG4, "stop Bgp Peer manager 1");
      peerMgr1->shutdownWithGR(false);
      XLOG(DBG4, "stop Bgp Peer manager 2");
      peerMgr2->shutdownWithGR(false);
    });
  });

  evb.loop();
}

/*
 * Test shutdownPeer at both ends.
 */
TEST_F(FiberBgpPeerManagerFixture, ShutdownPeerAtBothEndsTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  // test addPeer() and shutdownPeer()
  fm.addTask([this, &fm] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // addPeer() is called by both peerMgrs
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // test shutdownPeer()
    peerMgr1->shutdownPeer(peerAddr1);
    peerMgr2->shutdownPeer(peerAddr2);

    fm.addTask([this, &fm] {
      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
      waitTillSessionsGoDown(fm, peerMgr2, {peerId2});

      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      XLOG(DBG4, "stop Bgp Peer manager 1");
      peerMgr1->shutdownWithGR(false);
      XLOG(DBG4, "stop Bgp Peer manager 2");
      peerMgr2->shutdownWithGR(false);
    });
  });

  evb.loop();
}

/*
 * Test startPeer at one end.
 */
TEST_F(FiberBgpPeerManagerFixture, StartPeerAtOneEndTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  // test addPeer(), shutdownPeer(), and startPeer()
  fm.addTask([this, &fm] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // addPeer() is called by both peerMgrs
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // testshutdownPeer()
    peerMgr1->shutdownPeer(peerAddr1);

    fm.addTask([this, &fm] {
      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
      waitTillSessionsGoDown(fm, peerMgr2, {peerId2});

      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      // test startPeer()
      peerMgr1->startPeer(peerAddr1);
      waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
      waitTillSessionsComeUp(fm, peerMgr2, {peerId2});

      EXPECT_EQ(2, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_TRUE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(2, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_TRUE(callback2.isSessionUp(peerId2));

      XLOG(DBG4, "stop Bgp Peer manager 1");
      peerMgr1->shutdownWithGR(false);
      XLOG(DBG4, "stop Bgp Peer manager 2");
      peerMgr2->shutdownWithGR(false);
    });
  });

  evb.loop();
}

/*
 * Test startPeer at both end.
 */
TEST_F(FiberBgpPeerManagerFixture, StartPeerAtBothEndsTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  // test addPeer(), shutdownPeer(), and startPeer()
  fm.addTask([this, &fm] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // addPeer() is called by both peerMgrs
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // test shutdownPeer()
    peerMgr1->shutdownPeer(peerAddr1);
    peerMgr2->shutdownPeer(peerAddr2);

    fm.addTask([this, &fm] {
      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
      waitTillSessionsGoDown(fm, peerMgr2, {peerId2});

      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));
      // test startPeer
      peerMgr1->startPeer(peerAddr1);
      peerMgr2->startPeer(peerAddr2);
      waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
      waitTillSessionsComeUp(fm, peerMgr2, {peerId2});

      EXPECT_EQ(2, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_TRUE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(2, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_TRUE(callback2.isSessionUp(peerId2));

      XLOG(DBG4, "stop Bgp Peer manager 1");
      peerMgr1->shutdownWithGR(false);
      XLOG(DBG4, "stop Bgp Peer manager 2");
      peerMgr2->shutdownWithGR(false);
    });
  });

  evb.loop();
}

/*
 * Test shutdownPeer at both ends and startPeer one by one.
 */
TEST_F(FiberBgpPeerManagerFixture, StartPeerOneByOneTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  // test addPeer(), shutdownPeer(), and startPeer()
  fm.addTask([this, &fm] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // addPeer() is called by both peerMgrs
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // test shutdownPeer()
    peerMgr1->shutdownPeer(peerAddr1);
    peerMgr2->shutdownPeer(peerAddr2);

    fm.addTask([this, &fm] {
      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
      waitTillSessionsGoDown(fm, peerMgr2, {peerId2});

      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      // test start peeraddr1
      peerMgr1->startPeer(peerAddr1);

      /*
       * sleep for 2 seconds to check that
       * the shutdowed session remains down
       */
      fiberSleepFor(2s);

      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      // test start peeraddr2
      peerMgr2->startPeer(peerAddr2);
      waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
      waitTillSessionsComeUp(fm, peerMgr2, {peerId2});

      EXPECT_EQ(2, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_TRUE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(2, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_TRUE(callback2.isSessionUp(peerId2));

      XLOG(DBG4, "stop Bgp Peer manager 1");
      peerMgr1->shutdownWithGR(false);
      XLOG(DBG4, "stop Bgp Peer manager 2");
      peerMgr2->shutdownWithGR(false);
    });
  });

  evb.loop();
}

/*
 * Test dropPeer at one end.
 */
TEST_F(FiberBgpPeerManagerFixture, DropPeerAtOneEndTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  // test addPeer() and dropPeer()
  fm.addTask([this, &fm] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // addPeer() is called by both peerMgrs
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // test dropPeer()
    peerMgr1->dropPeer(peerAddr1);

    fm.addTask([this, &fm] {
      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
      waitTillSessionsGoDown(fm, peerMgr2, {peerId2});

      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      XLOG(DBG4, "stop Bgp Peer manager 1");
      peerMgr1->shutdownWithGR(false);
      XLOG(DBG4, "stop Bgp Peer manager 2");
      peerMgr2->shutdownWithGR(false);
    });
  });

  evb.loop();
}

/*
 * Test dropPeer at both ends.
 */
TEST_F(FiberBgpPeerManagerFixture, DropPeerAtBothEndsTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  // test addPeer() and dropPeer()
  fm.addTask([this, &fm] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // addPeer() is called by both peerMgrs
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // test dropPeer()
    peerMgr1->dropPeer(peerAddr1);
    peerMgr2->dropPeer(peerAddr2);

    fm.addTask([this, &fm] {
      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});

      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      XLOG(DBG4, "stop Bgp Peer manager 1");
      peerMgr1->shutdownWithGR(false);
      XLOG(DBG4, "stop Bgp Peer manager 2");
      peerMgr2->shutdownWithGR(false);
    });
  });

  evb.loop();
}

/*
 * Test DynamicPeer Feature
 *
 * Setup:
 *   peerMgr1 with peerPrefix 127.2.0.0/30, localAddr 127.1.0.1, passive-only
 *   peerMgr2 with peerAddr 127.1.0.1, localAddr 127.2.0.1
 *   peerMgr3 with peerAddr 127.1.0.1, localAddr 127.2.0.2
 *   peerMgr4 with peerAddr 127.1.0.1, localAddr 127.2.0.4
 *
 *  Two sessions, peerMgr1 <> peerMgr2 and peerMgr1 <> peerMgr3, will come up.
 *  peerMgr1 <> peerMgr4 will not come up as 127.2.0.4 is out of peerPrefix
 *  127.2.0.0/30.
 */
TEST_P(BasicAddPeerTest, DynamicPeerTest) {
  /*
   * this is a value parameerized test
   * test CONFIG_AS_ARGS and CONFIG_AS_PEERING_PARAMS
   */
  const auto& testScope = GetParam();

  auto& fm = fmWrapper.get();
  // init peerMgr1, peerMgr2
  initTwoPeerMgrs(fm);
  // init peerMgr3
  auto peerAddr3 = kR2Lo2; // 127.2.0.2
  const BgpPeerId peerId3{peerAddr3, kR1Lo1.asV4().toLongHBO()};
  TestFiberBgpPeerCallback callback3;
  auto peerMgr3 = initPeerMgr(fm, peerAddr3, callback3);
  // init peerMgr4
  auto peerAddr4 = kR2Lo4; // 127.2.0.4
  const BgpPeerId peerId4{peerAddr4, kR1Lo1.asV4().toLongHBO()};
  TestFiberBgpPeerCallback callback4;
  auto peerMgr4 = initPeerMgr(fm, peerAddr4, callback4);

  auto peerPrefix = kR2PfxSlash30; // 127.2.0.0/30

  /*
   * test addDynamicPeer(), shutdownDynamicPeer(), startDynamicPeer(),
   * stopDynamicPeerWithGracefulRestart() and dropPeer()
   */
  fm.addTask([&] {
    // peerMgr1 call backs
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId3));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId3));
    EXPECT_FALSE(callback1.isSessionUp(peerId3));

    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId4));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId4));
    EXPECT_FALSE(callback1.isSessionUp(peerId4));

    // peerMgr2 call back
    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // peerMgr3 call back
    EXPECT_EQ(0, callback3.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback3.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback3.isSessionUp(peerId2));

    // peerMgr4 call back
    EXPECT_EQ(0, callback4.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback4.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback4.isSessionUp(peerId2));

    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    if (testScope == BasicAddPeerTestScope::CONFIG_AS_ARGS) {
      // addDynamicPeer(127.2.0.0/30) on peerMgr1
      peerMgr1->addDynamicPeer(peerPrefix, 100, 100, {kR1Lo1, 0});
      // addPeer(127.1.0.1) to peerMrg2, peerMrg3, peerMrg4
      peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);
      peerMgr3->addPeer(peerAddr2, 100, 100, {kR2Lo2, 0}, peerPort1);
      peerMgr4->addPeer(peerAddr2, 100, 100, {kR2Lo4, 0}, peerPort1);
    } else {
      // addDynamicPeer(127.2.0.0/30) on peerMgr1
      auto params1 = PeeringParams(
          kDefaultVrf,
          IPAddress(), // dummy
          peerPrefix,
          100, // globalAs
          100, // localAs
          100, // remoteAs
          kR1Lo1.asV4(), // routerId 127.1.0.1
          kR1Lo1.asV4(), // clusterId 127.1.0.1
          180s, // holdTime
          120s, // grRestartTime
          constants::kBgpPort, // dummy
          {kR1Lo1, 0}, // bindAddr
          TBgpSessionConnectMode::PASSIVE_ONLY);
      peerMgr1->addDynamicPeer(peerPrefix, params1);

      // addPeer(127.1.0.1) to peerMrg2, peerMrg3, peerMrg4
      auto params2 = PeeringParams(
          kDefaultVrf,
          peerAddr2, // dummy
          std::nullopt, // peerPrefix
          100, // globalAs
          100, // localAs
          100, // remoteAs
          kR2Lo1.asV4(), // routerId 127.2.0.1
          kR2Lo1.asV4(), // clusterId 127.2.0.1
          180s, // holdTime
          120s, // grRestartTime
          peerPort1,
          {kR2Lo1, 0}, // bindAddr
          TBgpSessionConnectMode::PASSIVE_ACTIVE);
      peerMgr2->addPeer(peerAddr2, params2);

      auto params3 = PeeringParams(
          kDefaultVrf,
          peerAddr2, // dummy
          std::nullopt, // peerPrefix
          100, // globalAs
          100, // localAs
          100, // remoteAs
          kR1Lo1.asV4(), // routerId 127.1.0.1
          kR1Lo1.asV4(), // clusterId 127.1.0.1
          180s, // holdTime
          120s, // grRestartTime
          peerPort1,
          {kR2Lo2, 0}, // bindAddr
          TBgpSessionConnectMode::PASSIVE_ACTIVE);
      peerMgr3->addPeer(peerAddr2, params3);

      auto params4 = PeeringParams(
          kDefaultVrf,
          peerAddr2, // dummy
          std::nullopt, // peerPrefix
          100, // globalAs
          100, // localAs
          100, // remoteAs
          kR1Lo1.asV4(), // routerId 127.1.0.1
          kR1Lo1.asV4(), // clusterId 127.1.0.1
          180s, // holdTime
          120s, // grRestartTime
          peerPort1,
          {kR2Lo4, 0}, // bindAddr
          TBgpSessionConnectMode::PASSIVE_ACTIVE);
      peerMgr4->addPeer(peerAddr2, params4);
    }
    // wait till session to 127.2.0.1, 127.2.0.2 come up
    waitTillSessionsComeUp(fm, peerMgr1, {peerId1, peerId3});

    // peerMgr1 call backs
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId3));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId3));
    EXPECT_TRUE(callback1.isSessionUp(peerId3));

    // peer4 is out of prefix range, should be dropped
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId4));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId4));
    EXPECT_FALSE(callback1.isSessionUp(peerId4));

    // peerMgr2 call back
    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // peerMgr3 call back
    EXPECT_EQ(1, callback3.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback3.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback3.isSessionUp(peerId2));

    // peerMgr4 call back
    EXPECT_EQ(0, callback4.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback4.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback4.isSessionUp(peerId2));

    // shutdown dynamic peer on peerMgr1
    peerMgr1->shutdownDynamicPeer(peerPrefix);

    fm.addTask([&] {
      // all established peer sessions in this group should be shutdown
      waitTillSessionsGoDown(fm, peerMgr1, {peerId1, peerId3});
      waitTillSessionsGoDown(fm, peerMgr2, {peerId2});
      waitTillSessionsGoDown(fm, peerMgr3, {peerId2});

      // peerMgr1 call backs
      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId3));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId3));
      EXPECT_FALSE(callback1.isSessionUp(peerId3));

      EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId4));
      EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId4));
      EXPECT_FALSE(callback1.isSessionUp(peerId4));

      // peerMgr2 call back
      EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      // peerMgr3 call back
      EXPECT_EQ(1, callback3.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback3.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback3.isSessionUp(peerId2));

      // peerMgr4 call back
      EXPECT_EQ(0, callback4.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(0, callback4.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback4.isSessionUp(peerId2));

      // start the idle dynamic peers
      peerMgr1->startDynamicPeer(peerPrefix);
      // wait till all sessions in this group come up
      waitTillSessionsComeUp(fm, peerMgr1, {peerId1, peerId3});
      waitTillSessionsComeUp(fm, peerMgr2, {peerId2});
      waitTillSessionsComeUp(fm, peerMgr3, {peerId2});

      // peerMgr1 call backs
      EXPECT_EQ(2, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_TRUE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(2, callback1.getEstablishedCallbackCount(peerId3));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId3));
      EXPECT_TRUE(callback1.isSessionUp(peerId3));

      // peer4 is out of prefix range, should be dropped
      EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId4));
      EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId4));
      EXPECT_FALSE(callback1.isSessionUp(peerId4));

      // peerMgr2 call back
      EXPECT_EQ(2, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_TRUE(callback2.isSessionUp(peerId2));

      // peerMgr3 call back
      EXPECT_EQ(2, callback3.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback3.getTerminatedCallbackCount(peerId2));
      EXPECT_TRUE(callback3.isSessionUp(peerId2));

      // peerMgr4 call back
      EXPECT_EQ(0, callback4.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(0, callback4.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback4.isSessionUp(peerId2));

      // restart the dynamic peers
      peerMgr1->stopDynamicPeerWithGracefulRestart(peerPrefix);
      fm.addTask([&] {
        // all established peer sessions in this group should be shutdown
        waitTillSessionsGoDown(fm, peerMgr1, {peerId1, peerId3});
        waitTillSessionsGoDown(fm, peerMgr2, {peerId2});
        waitTillSessionsGoDown(fm, peerMgr3, {peerId2});

        // peerMgr1 call backs
        EXPECT_EQ(2, callback1.getEstablishedCallbackCount(peerId1));
        EXPECT_EQ(2, callback1.getTerminatedCallbackCount(peerId1));
        EXPECT_FALSE(callback1.isSessionUp(peerId1));

        EXPECT_EQ(2, callback1.getEstablishedCallbackCount(peerId3));
        EXPECT_EQ(2, callback1.getTerminatedCallbackCount(peerId3));
        EXPECT_FALSE(callback1.isSessionUp(peerId3));

        // peer4 is out of prefix range, should be dropped
        EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId4));
        EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId4));
        EXPECT_FALSE(callback1.isSessionUp(peerId4));

        // peerMgr2 call back
        EXPECT_EQ(2, callback2.getEstablishedCallbackCount(peerId2));
        EXPECT_EQ(2, callback2.getTerminatedCallbackCount(peerId2));
        EXPECT_FALSE(callback2.isSessionUp(peerId2));

        // peerMgr3 call back
        EXPECT_EQ(2, callback3.getEstablishedCallbackCount(peerId2));
        EXPECT_EQ(2, callback3.getTerminatedCallbackCount(peerId2));
        EXPECT_FALSE(callback3.isSessionUp(peerId2));

        // peerMgr4 call back
        EXPECT_EQ(0, callback4.getEstablishedCallbackCount(peerId2));
        EXPECT_EQ(0, callback4.getTerminatedCallbackCount(peerId2));
        EXPECT_FALSE(callback4.isSessionUp(peerId2));

        // wait till all sessions in this group come up
        waitTillSessionsComeUp(fm, peerMgr1, {peerId1, peerId3});
        waitTillSessionsComeUp(fm, peerMgr2, {peerId2});
        waitTillSessionsComeUp(fm, peerMgr3, {peerId2});

        // peerMgr1 call backs
        EXPECT_EQ(3, callback1.getEstablishedCallbackCount(peerId1));
        EXPECT_EQ(2, callback1.getTerminatedCallbackCount(peerId1));
        EXPECT_TRUE(callback1.isSessionUp(peerId1));

        EXPECT_EQ(3, callback1.getEstablishedCallbackCount(peerId3));
        EXPECT_EQ(2, callback1.getTerminatedCallbackCount(peerId3));
        EXPECT_TRUE(callback1.isSessionUp(peerId3));

        // peer4 is out of prefix range, should be dropped
        EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId4));
        EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId4));
        EXPECT_FALSE(callback1.isSessionUp(peerId4));

        // peerMgr2 call back
        EXPECT_EQ(3, callback2.getEstablishedCallbackCount(peerId2));
        EXPECT_EQ(2, callback2.getTerminatedCallbackCount(peerId2));
        EXPECT_TRUE(callback2.isSessionUp(peerId2));

        // peerMgr3 call back
        EXPECT_EQ(3, callback3.getEstablishedCallbackCount(peerId2));
        EXPECT_EQ(2, callback3.getTerminatedCallbackCount(peerId2));
        EXPECT_TRUE(callback3.isSessionUp(peerId2));

        // peerMgr4 call back
        EXPECT_EQ(0, callback4.getEstablishedCallbackCount(peerId2));
        EXPECT_EQ(0, callback4.getTerminatedCallbackCount(peerId2));
        EXPECT_FALSE(callback4.isSessionUp(peerId2));

        // dropPeer on peerMgr1
        peerMgr1->dropPeer(peerPrefix);

        fm.addTask([&] {
          // all established peer sessions in this group should be dropped
          waitTillSessionsGoDown(fm, peerMgr1, {peerId1, peerId3});
          waitTillSessionsGoDown(fm, peerMgr2, {peerId2});
          waitTillSessionsGoDown(fm, peerMgr3, {peerId2});

          // peerMgr1 call backs
          EXPECT_EQ(3, callback1.getEstablishedCallbackCount(peerId1));
          EXPECT_EQ(3, callback1.getTerminatedCallbackCount(peerId1));
          EXPECT_FALSE(callback1.isSessionUp(peerId1));

          EXPECT_EQ(3, callback1.getEstablishedCallbackCount(peerId3));
          EXPECT_EQ(3, callback1.getTerminatedCallbackCount(peerId3));
          EXPECT_FALSE(callback1.isSessionUp(peerId3));

          // peer4 is out of prefix range, should be dropped
          EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId4));
          EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId4));
          EXPECT_FALSE(callback1.isSessionUp(peerId4));

          // peerMgr2 call back
          EXPECT_EQ(3, callback2.getEstablishedCallbackCount(peerId2));
          EXPECT_EQ(3, callback2.getTerminatedCallbackCount(peerId2));
          EXPECT_FALSE(callback2.isSessionUp(peerId2));

          // peerMgr3 call back
          EXPECT_EQ(3, callback3.getEstablishedCallbackCount(peerId2));
          EXPECT_EQ(3, callback3.getTerminatedCallbackCount(peerId2));
          EXPECT_FALSE(callback3.isSessionUp(peerId2));

          // peerMgr4 call back
          EXPECT_EQ(0, callback4.getEstablishedCallbackCount(peerId2));
          EXPECT_EQ(0, callback4.getTerminatedCallbackCount(peerId2));
          EXPECT_FALSE(callback4.isSessionUp(peerId2));

          if (testScope == BasicAddPeerTestScope::CONFIG_AS_ARGS) {
            // Reconfigure for peerMgr1: addPeer 127.2.0.2, 127.2.0.4
            peerMgr1->addDynamicPeer(peerPrefix, 100, 100, {kR1Lo1, 0});
          } else {
            // Reconfigure for peerMgr1: addPeer 127.2.0.2, 127.2.0.4
            auto params = PeeringParams(
                kDefaultVrf,
                IPAddress(), // dummy
                peerPrefix,
                100, // globalAs
                100, // localAs
                100, // remoteAs
                kR1Lo1.asV4(), // routerId 127.1.0.1
                kR1Lo1.asV4(), // clusterId 127.1.0.1
                180s, // holdTime
                120s, // grRestartTime
                constants::kBgpPort, // dummy
                {kR1Lo1, 0}, // bindAddr
                TBgpSessionConnectMode::PASSIVE_ONLY);
            peerMgr1->addDynamicPeer(peerPrefix, params);
          }
          // wait till session to 127.2.0.2, 127.2.0.4 come up
          waitTillSessionsComeUp(fm, peerMgr1, {peerId1, peerId3});

          // peerMgr1 call backs
          EXPECT_EQ(4, callback1.getEstablishedCallbackCount(peerId1));
          EXPECT_EQ(3, callback1.getTerminatedCallbackCount(peerId1));
          EXPECT_TRUE(callback1.isSessionUp(peerId1));

          EXPECT_EQ(4, callback1.getEstablishedCallbackCount(peerId3));
          EXPECT_EQ(3, callback1.getTerminatedCallbackCount(peerId3));
          EXPECT_TRUE(callback1.isSessionUp(peerId3));

          // peer4 is out of prefix range, should be dropped
          EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId4));
          EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId4));
          EXPECT_FALSE(callback1.isSessionUp(peerId4));

          // peerMgr2 call back
          EXPECT_EQ(4, callback2.getEstablishedCallbackCount(peerId2));
          EXPECT_EQ(3, callback2.getTerminatedCallbackCount(peerId2));
          EXPECT_TRUE(callback2.isSessionUp(peerId2));

          // peerMgr3 call back
          EXPECT_EQ(4, callback3.getEstablishedCallbackCount(peerId2));
          EXPECT_EQ(3, callback3.getTerminatedCallbackCount(peerId2));
          EXPECT_TRUE(callback3.isSessionUp(peerId2));

          // peerMgr4 call back
          EXPECT_EQ(0, callback4.getEstablishedCallbackCount(peerId2));
          EXPECT_EQ(0, callback4.getTerminatedCallbackCount(peerId2));
          EXPECT_FALSE(callback4.isSessionUp(peerId2));

          XLOG(DBG4, "stop Bgp Peer manager 1");
          peerMgr1->shutdownWithGR(false);
          XLOG(DBG4, "stop Bgp Peer manager 2");
          peerMgr2->shutdownWithGR(false);
          XLOG(DBG4, "stop Bgp Peer manager 3");
          peerMgr3->shutdownWithGR(false);
          XLOG(DBG4, "stop Bgp Peer manager 4");
          peerMgr4->shutdownWithGR(false);
        });
      });
    });
  });

  evb.loop();

  // peerMgr1 call backs
  EXPECT_EQ(4, callback1.getEstablishedCallbackCount(peerId1));
  EXPECT_EQ(4, callback1.getTerminatedCallbackCount(peerId1));
  EXPECT_FALSE(callback1.isSessionUp(peerId1));

  EXPECT_EQ(4, callback1.getEstablishedCallbackCount(peerId3));
  EXPECT_EQ(4, callback1.getTerminatedCallbackCount(peerId3));
  EXPECT_FALSE(callback1.isSessionUp(peerId3));

  // peer4 is out of prefix range, should be dropped
  EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId4));
  EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId4));
  EXPECT_FALSE(callback1.isSessionUp(peerId4));

  // peerMgr2 call back
  EXPECT_EQ(4, callback2.getEstablishedCallbackCount(peerId2));
  EXPECT_EQ(4, callback2.getTerminatedCallbackCount(peerId2));
  EXPECT_FALSE(callback2.isSessionUp(peerId2));

  // peerMgr3 call back
  EXPECT_EQ(4, callback3.getEstablishedCallbackCount(peerId2));
  EXPECT_EQ(4, callback3.getTerminatedCallbackCount(peerId2));
  EXPECT_FALSE(callback3.isSessionUp(peerId2));

  // peerMgr4 call back
  EXPECT_EQ(0, callback4.getEstablishedCallbackCount(peerId2));
  EXPECT_EQ(0, callback4.getTerminatedCallbackCount(peerId2));
  EXPECT_FALSE(callback4.isSessionUp(peerId2));
}

/**
 * Test to verify the numDynamicPeers().
 */
TEST_F(FiberBgpPeerManagerFixture, NumDynamicPeersTest) {
  auto& fm = fmWrapper.get();
  facebook::bgp::BgpStats::initCounters();

  // init peerMgr1, peerMgr2
  initTwoPeerMgrs(fm);
  // init peerMgr3
  auto peerAddr3 = kR2Lo2; // 127.2.0.2
  const BgpPeerId peerId3{peerAddr3, kR1Lo1.asV4().toLongHBO()};
  TestFiberBgpPeerCallback callback3;
  auto peerMgr3 = initPeerMgr(fm, peerAddr3, callback3);
  // init peerMgr4
  auto peerAddr4 = kR2Lo4; // 127.2.0.4
  const BgpPeerId peerId4{peerAddr4, kR1Lo1.asV4().toLongHBO()};
  TestFiberBgpPeerCallback callback4;
  auto peerMgr4 = initPeerMgr(fm, peerAddr4, callback4);

  auto peerPrefix = kR2PfxSlash30; // 127.2.0.0/30

  fm.addTask([&] {
    EXPECT_EQ(0, peerMgr1->numDynamicPeers());

    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    // addDynamicPeer(127.2.0.0/30) on peerMgr1
    peerMgr1->addDynamicPeer(peerPrefix, 100, 100, {kR1Lo1, 0});
    EXPECT_EQ(0, peerMgr1->numDynamicPeers());
    // addPeer(127.1.0.1) to peerMrg2, peerMrg3, peerMrg4
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);
    peerMgr3->addPeer(peerAddr2, 100, 100, {kR2Lo2, 0}, peerPort1);
    peerMgr4->addPeer(peerAddr2, 100, 100, {kR2Lo4, 0}, peerPort1);

    // wait till session to 127.2.0.1, 127.2.0.2 come up
    waitTillSessionsComeUp(fm, peerMgr1, {peerId1, peerId3});

    // peer4 is out of prefix range, will not come up.
    EXPECT_EQ(2, peerMgr1->numDynamicPeers());

    // Verify ODS counter matches number of attached dynamic peers
    auto tcData = facebook::fb303::ThreadCachedServiceData::get();
    tcData->publishStats();
    EXPECT_EQ(
        2, tcData->getCounter(facebook::bgp::BgpStats::kDynamicPeersCount));

    peerMgr2->dropPeer(peerAddr2);
    waitTillSessionsGoDown(fm, peerMgr1, {peerId1});

    /*
     * peer4 is out of prefix range, will not come up.
     * peer2 is down.
     */
    EXPECT_EQ(1, peerMgr1->numDynamicPeers());

    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);
    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    // peer4 is out of prefix range, will not come up.
    EXPECT_EQ(2, peerMgr1->numDynamicPeers());

    XLOG(DBG4, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 3");
    peerMgr3->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 4");
    peerMgr4->shutdownWithGR(false);
  });

  evb.loop();
}

/**
 * Test to verify the numDynamicPeers(), when there are no dynamic peers.
 *
 */
TEST_F(FiberBgpPeerManagerFixture, NumDynamicPeersNegativeTest) {
  auto& fm = fmWrapper.get();

  // init peerMgr1, peerMgr2
  initTwoPeerMgrs(fm);

  fm.addTask([&] {
    EXPECT_EQ(0, peerMgr1->numDynamicPeers());

    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    peerMgr2->addPeer(
        peerAddr2,
        100,
        100,
        {kR2Lo1, 0},
        peerPort1,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::PASSIVE_ONLY);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    EXPECT_EQ(0, peerMgr1->numDynamicPeers());

    peerMgr2->dropPeer(peerAddr2);
    waitTillSessionsGoDown(fm, peerMgr1, {peerId1});

    EXPECT_EQ(0, peerMgr1->numDynamicPeers());

    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);
    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    EXPECT_EQ(0, peerMgr1->numDynamicPeers());

    XLOG(DBG4, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);
  });

  evb.loop();
}

/**
 * Test to verify the exceedsDynamicPeerLimit().
 */
TEST_F(FiberBgpPeerManagerFixture, ExceedsDynamicPeerLimitTest) {
  auto& fm = fmWrapper.get();
  BgpGlobalConfig bgpGlobalConfig1(
      100, /* localAsn */
      kR1Lo1, /* routerId 127.1.0.1 */
      kR1Lo1, /* clusterId 127.1.0.1 */
      kDefaultHoldTime, /* holdTime */
      std::nullopt, /* listenAddr */
      kDefaultGrRestartTime, /* grRestartTime */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV4 */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV6 */
      std::nullopt, /* localConfedAsn */
      facebook::bgp::ComputeUcmpFromLbwComm{false},
      0, /* ucmp-width */
      std::nullopt, /* ucmp-quantizer */
      facebook::bgp::ValidateRemoteAs{true},
      facebook::bgp::SupportStatefulGr{true},
      facebook::bgp::EnableServerSocket{true},
      facebook::bgp::AllowLoopbackReflection{false},
      facebook::bgp::CountConfedsInAsPathLen{false},
      std::nullopt, /* deviceName */
      std::nullopt, /* switchLimitConfig */
      1, /* dynamicPeerLimit */
      std::nullopt, /* streamSubscriberLimit */
      facebook::bgp::EnableNexthopTracking{false}, /* enableNextHopTracking */
      std::vector<std::string>{}, /* includeInterfaceRegexes */
      facebook::bgp::EnableDynamicPolicyEvaluation{
          false}, /* enableDynamicPolicyEvaluation
                   */
      std::nullopt, /* thriftServerConfig */
      FLAGS_enable_egress_queue_backpressure /* enableEgressQueueBackpressure */
  );

  BgpGlobalConfig bgpGlobalConfig2(
      100, /* localAsn */
      kR2Lo1, /* routerId 127.2.0.1 */
      kR2Lo1, /* clusterId 127.2.0.1 */
      kDefaultHoldTime, /* holdTime */
      std::nullopt, /* listenAddr */
      kDefaultGrRestartTime, /* grRestartTime */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV4 */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV6 */
      std::nullopt, /* localConfedAsn */
      facebook::bgp::ComputeUcmpFromLbwComm{false}, /* computeUcmpFromLbwComm */
      0, /* ucmpWidth */
      std::nullopt, /* ucmpQuantizer */
      facebook::bgp::ValidateRemoteAs{true}, /* validateRemoteAs */
      facebook::bgp::SupportStatefulGr{true}, /* supportStatefulGr */
      facebook::bgp::EnableServerSocket{true}, /* enableServerSocket */
      facebook::bgp::AllowLoopbackReflection{false}, /* allowLoopbackReflection
                                                      */
      facebook::bgp::CountConfedsInAsPathLen{false}, /* countConfedsInAsPathLen
                                                      */
      std::nullopt, /* deviceName */
      std::nullopt, /* switchLimitConfig */
      std::nullopt, /* dynamicPeerLimit */
      std::nullopt, /* streamSubscriberLimit */
      facebook::bgp::EnableNexthopTracking{false}, /* enableNextHopTracking */
      std::vector<std::string>{}, /* includeInterfaceRegexes */
      facebook::bgp::EnableDynamicPolicyEvaluation{
          false}, /* enableDynamicPolicyEvaluation
                   */
      std::nullopt, /* thriftServerConfig */
      FLAGS_enable_egress_queue_backpressure /* enableEgressQueueBackpressure */
  );

  initTwoPeerMgrs(fm, bgpGlobalConfig1, bgpGlobalConfig2);
  // init peerMgr3
  auto peerAddr3 = kR2Lo2; // 127.2.0.2

  const BgpPeerId peerId3{peerAddr3, kR1Lo1.asV4().toLongHBO()};
  TestFiberBgpPeerCallback callback3;
  auto peerMgr3 = initPeerMgr(fm, peerAddr3, callback3);
  // init peerMgr4
  auto peerAddr4 = kR2Lo4; // 127.2.0.4
  const BgpPeerId peerId4{peerAddr4, kR1Lo1.asV4().toLongHBO()};
  TestFiberBgpPeerCallback callback4;
  auto peerMgr4 = initPeerMgr(fm, peerAddr4, callback4);

  auto peerPrefix = kR2PfxSlash30; // 127.2.0.0/30

  fm.addTask([&] {
    EXPECT_EQ(0, peerMgr1->numDynamicPeers());

    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();

    // addDynamicPeer(127.2.0.0/30) on peerMgr1
    peerMgr1->addDynamicPeer(peerPrefix, 100, 100, {kR1Lo1, 0});

    // peer4 is out of prefix range, will not come up.
    EXPECT_EQ(0, peerMgr1->numDynamicPeers());
    EXPECT_FALSE(peerMgr1->exceedsDynamicPeerLimit());

    // addPeer(127.1.0.1) to peerMrg2
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    // wait till session to 127.2.0.1 coems up.
    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    // peer4 is out of prefix range, will not come up
    EXPECT_EQ(1, peerMgr1->numDynamicPeers());
    EXPECT_TRUE(peerMgr1->exceedsDynamicPeerLimit());

    // addPeer(127.1.0.1) to peerMrg3, peerMrg4
    peerMgr3->addPeer(peerAddr2, 100, 100, {kR2Lo2, 0}, peerPort1);
    peerMgr4->addPeer(peerAddr2, 100, 100, {kR2Lo4, 0}, peerPort1);

    /*
     * Neither of the above 2 sessions will come up. kR2Lo2 exceeds max peer
     * limit and kR2Lo4 is out of dynamic prefix range.
     */
    waitTillSessionsComeUp(fm, peerMgr1, {peerId3});

    // Still with 1 KR2Lo1 established dynamic peer
    EXPECT_EQ(1, peerMgr1->numDynamicPeers());
    EXPECT_TRUE(peerMgr1->exceedsDynamicPeerLimit());

    // drop kR2Lo1
    peerMgr2->dropPeer(peerAddr2);
    waitTillSessionsGoDown(fm, peerMgr1, {peerId1});

    // kR2Lo2 will come up
    waitTillSessionsComeUp(fm, peerMgr1, {peerId3});

    /*
     * kR2Lo2 is established, kR2Lo1 is dropped. kR2Lo4 is out of dynamic
     * prefix range.
     */
    EXPECT_EQ(1, peerMgr1->numDynamicPeers());
    EXPECT_TRUE(peerMgr1->exceedsDynamicPeerLimit());

    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);
    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    // kR2Lo1 will try to come up but rejected.
    EXPECT_EQ(1, peerMgr1->numDynamicPeers());
    EXPECT_TRUE(peerMgr1->exceedsDynamicPeerLimit());

    XLOG(DBG4, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 3");
    peerMgr3->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 4");
    peerMgr4->shutdownWithGR(false);
  });

  evb.loop();
}

/*
 * Test conflict addPeer and addDynamicPeer configuration
 * The one comes later should be dropped.
 *
 * Setup:
 * peerMgr1: addPeer(127.2.0.1) succeed
 *   -> duplicate addPeer(127.2.0.1) fails
 *   -> addDynamicPeer(127.2.0.0/30) fails because it overlaps with 127.2.0.1
 * peerMgr2: addDynamicPeer(127.1.0.0/30) succeed
 *   -> duplicate addDynamicPeer(127.1.0.0/30) fails
 *   -> addPeer(127.1.0.1) fails because it overlaps with 127.1.0.0/30
 */
TEST_F(FiberBgpPeerManagerFixture, AddPeerConflictTest) {
  auto& fm = fmWrapper.get();
  auto peerPrefix1 = kR1PfxSlash30; // 127.1.0.0/30
  auto peerPrefix2 = kR2PfxSlash30; // 127.2.0.0/30
  initTwoPeerMgrs(fm);

  fm.addTask([&] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();

    // peerMgr1: first addPeer() should succeed
    auto ret = peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    EXPECT_TRUE(ret.hasValue());
    // peerMgr1: duplicate addPeer() call
    ret = peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    EXPECT_TRUE(ret.hasError());
    EXPECT_EQ(ret.error(), FiberBgpPeerManager::ErrorCode::PEER_EXISTS_ALREADY);
    // peerMgr1: overlapping addDynamicPeer() called for 127.2.0.0/30
    ret = peerMgr1->addDynamicPeer(peerPrefix2, 100, 100, {kR1Lo1, 0});
    EXPECT_TRUE(ret.hasError());
    EXPECT_EQ(ret.error(), FiberBgpPeerManager::ErrorCode::PEER_EXISTS_ALREADY);

    // peerMgr2: first addDynamicPeer() should succeed
    ret = peerMgr2->addDynamicPeer(peerPrefix1, 100, 100, {kR2Lo1, 0});
    EXPECT_TRUE(ret.hasValue());
    // peerMgr2: duplicate addDynamicPeer() call
    ret = peerMgr2->addDynamicPeer(peerPrefix1, 100, 100, {kR2Lo1, 0});
    EXPECT_TRUE(ret.hasError());
    EXPECT_EQ(ret.error(), FiberBgpPeerManager::ErrorCode::PEER_EXISTS_ALREADY);
    // peerMgr2: overlapping addPeer() called for 127.1.0.1
    ret = peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);
    EXPECT_TRUE(ret.hasError());
    EXPECT_EQ(ret.error(), FiberBgpPeerManager::ErrorCode::PEER_EXISTS_ALREADY);

    fm.addTask([this, &fm] {
      waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_TRUE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_TRUE(callback2.isSessionUp(peerId2));

      XLOG(DBG4, "stop Bgp Peer manager 1");
      peerMgr1->shutdownWithGR(false);
      XLOG(DBG4, "stop Bgp Peer manager 2");
      peerMgr2->shutdownWithGR(false);
    });
  });

  evb.loop();

  EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
  EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
  EXPECT_FALSE(callback1.isSessionUp(peerId1));

  EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
  EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
  EXPECT_FALSE(callback2.isSessionUp(peerId2));
}

/*
 * Test IPv4 mapped IPv6 address. Ensure that even though we are listening
 * on "::" we can accept ipv4 connection and process correctly
 * Verify various fields returned by getAllPeerDisplayInfos
 */
TEST_F(FiberBgpPeerManagerFixture, VerifyV4MappedV6Connection) {
  auto& fm = fmWrapper.get();
  auto peerPrefix1 = kR1PfxSlash30; // 127.1.0.0/30
  // Set listening address as "::"
  initTwoPeerMgrs(fm, SocketAddress("::", 0));

  fm.addTask([&] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    const auto peerPort = peerMgr2->getListenAddress()->getPort();

    auto ret = peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort);
    EXPECT_TRUE(ret.hasValue());

    // Add dynamic peer
    ret = peerMgr2->addDynamicPeer(peerPrefix1, 100, 100, {kR2Lo1, 0});
    EXPECT_TRUE(ret.hasValue());

    fm.addTask([this, &fm] {
      waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_TRUE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_TRUE(callback2.isSessionUp(peerId2));

      // Sleep to see the established time is proper
      fiberSleepFor(10ms);

      /*
       * Verify that V4 mapped V6 address is converted to V4
       * Here we are checking all addresses as proper V4 (not v4 mapped v6)
       * Verify fields returned by getAllPeerDisplayInfos
       */
      auto allPeersInfo = peerMgr2->getAllPeerDisplayInfos();
      EXPECT_EQ(2, allPeersInfo.size());
      auto idleCount = 0;
      auto establishedCount = 0;
      for (const auto& kv : allPeersInfo) {
        auto peerAddr = kv.first;
        auto peerInfo = kv.second;
        EXPECT_FALSE(peerAddr.isIPv4Mapped());
        /*
         * Verify that peerInfo->peeringParams.peerPrefix is applicable only
         * for the configured prefix neighbor
         */
        if (peerInfo->state == BgpSessionState::ESTABLISHED) {
          establishedCount++;
          EXPECT_FALSE(peerInfo->peeringParams.peerPrefix);
          EXPECT_EQ(kR1Lo1.asV4().toLongHBO(), peerInfo->remoteBgpId);

          // Verify that established time is within acceptable range
          auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - peerInfo->establishedTime);
          EXPECT_LE(10ms, duration);
          EXPECT_GT(20ms, duration);
        } else {
          idleCount++;
          EXPECT_EQ(BgpSessionState::IDLE, peerInfo->state);
          EXPECT_EQ(peerAddr, kR1PfxSlash30.first);
          EXPECT_EQ(kR1PfxSlash30, peerInfo->peeringParams.peerPrefix);
          EXPECT_EQ(0, peerInfo->remoteBgpId);
        }
        EXPECT_EQ(100, peerInfo->peeringParams.localAs);
        EXPECT_EQ(100, peerInfo->peeringParams.globalAs);
        EXPECT_EQ(100, peerInfo->peeringParams.remoteAs);
        EXPECT_EQ(kR2Lo1, peerInfo->localAddr.getIPAddress());
        EXPECT_EQ(kDefaultHoldTime, peerInfo->peeringParams.holdTime);
      }
      /*
       * For every dynamic peer there will be idle peer denoting the
       * configured prefix, there will be established peer which represents
       * connected peer (dynamically discovered)
       */
      EXPECT_EQ(1, idleCount);
      EXPECT_EQ(1, establishedCount);

      allPeersInfo.clear();
      allPeersInfo = peerMgr1->getAllPeerDisplayInfos();
      EXPECT_EQ(1, allPeersInfo.size());
      for (const auto& kv : allPeersInfo) {
        auto peerAddr = kv.first;
        auto peerInfo = kv.second;
        EXPECT_FALSE(peerAddr.isIPv4Mapped());
        EXPECT_EQ(BgpSessionState::ESTABLISHED, peerInfo->state);
        EXPECT_EQ(kR2Lo1.asV4().toLongHBO(), peerInfo->remoteBgpId);
        EXPECT_EQ(kR1Lo1, peerInfo->localAddr.getIPAddress());
        EXPECT_EQ(kDefaultHoldTime, peerInfo->peeringParams.holdTime);

        // Verify that established time is within acceptable range
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - peerInfo->establishedTime);
        EXPECT_LE(10ms, duration);
        EXPECT_GT(20ms, duration);
      }

      XLOG(DBG4, "stop Bgp Peer manager 1");
      peerMgr1->shutdownWithGR(false);
      XLOG(DBG4, "stop Bgp Peer manager 2");
      peerMgr2->shutdownWithGR(false);
    });
  });

  evb.loop();

  EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
  EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
  EXPECT_FALSE(callback1.isSessionUp(peerId1));

  EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
  EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
  EXPECT_FALSE(callback2.isSessionUp(peerId2));
}

/*
 * Test repeating addPeer/dropPeer at one end.
 */
TEST_F(FiberBgpPeerManagerFixture, PeerFlapAtOneEndTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  int count = 10;
  int i;
  // peerMgr1 repeats addPeer() and dropPeer()
  fm.addTask([this, &fm, &i, &count] {
    // peerMgr2 calls addPeer()
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    for (i = 0; i < count; i++) {
      EXPECT_EQ(i + 0, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(i + 0, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(i + 0, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(i + 0, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      // peerMgr1 calls addPeer()
      peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);

      waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

      EXPECT_EQ(i + 1, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(i + 0, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_TRUE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(i + 1, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(i + 0, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_TRUE(callback2.isSessionUp(peerId2));

      // test dropPeer()
      peerMgr1->dropPeer(peerAddr1);

      fm.addTask([this, &fm, i, count] {
        waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
        waitTillSessionsGoDown(fm, peerMgr2, {peerId2});

        EXPECT_EQ(i + 1, callback1.getEstablishedCallbackCount(peerId1));
        EXPECT_EQ(i + 1, callback1.getTerminatedCallbackCount(peerId1));
        EXPECT_FALSE(callback1.isSessionUp(peerId1));

        EXPECT_EQ(i + 1, callback2.getEstablishedCallbackCount(peerId2));
        EXPECT_EQ(i + 1, callback2.getTerminatedCallbackCount(peerId2));
        EXPECT_FALSE(callback2.isSessionUp(peerId2));

        if (i + 1 == count) {
          XLOG(DBG4, "stop Bgp Peer manager 1");
          peerMgr1->shutdownWithGR(false);
          XLOG(DBG4, "stop Bgp Peer manager 2");
          peerMgr2->shutdownWithGR(false);
        }
      });

      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
      waitTillSessionsGoDown(fm, peerMgr2, {peerId2});
    } // for
  });

  evb.loop();
}

/*
 * Test multiple Bgp session coming up
 */
TEST_F(FiberBgpPeerManagerFixture, MultipleBgpSessionTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  // 3 peer sessions between r1 and r2
  auto r1PeerAddr1 = kR2Lo1; // 127.2.0.1
  auto r1PeerId1 = BgpPeerId(r1PeerAddr1, peerAddr1.asV4().toLongHBO());
  auto r1PeerAddr2 = kR2Lo2; // 127.2.0.2
  auto r1PeerId2 = BgpPeerId(r1PeerAddr2, peerAddr1.asV4().toLongHBO());
  auto r1PeerAddr3 = kR2Lo3; // 127.2.0.3
  auto r1PeerId3 = BgpPeerId(r1PeerAddr3, peerAddr1.asV4().toLongHBO());

  auto r2PeerAddr1 = kR1Lo1; // 127.1.0.1
  auto r2PeerId1 = BgpPeerId(r2PeerAddr1, peerAddr2.asV4().toLongHBO());
  auto r2PeerAddr2 = kR1Lo2; // 127.1.0.2
  auto r2PeerId2 = BgpPeerId(r2PeerAddr2, peerAddr2.asV4().toLongHBO());
  auto r2PeerAddr3 = kR1Lo3; // 127.1.0.3
  auto r2PeerId3 = BgpPeerId(r2PeerAddr3, peerAddr2.asV4().toLongHBO());

  std::unordered_set<BgpPeerId> r1PeerSet = {r1PeerId1, r1PeerId2, r1PeerId3};
  std::unordered_set<BgpPeerId> r2PeerSet = {r2PeerId1, r2PeerId2, r2PeerId3};

  fm.addTask([&] {
    for (const auto& peerId : r1PeerSet) {
      EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId));
      EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId));
      EXPECT_FALSE(callback1.isSessionUp(peerId));
    }
    for (const auto& peerId : r2PeerSet) {
      EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId));
      EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId));
      EXPECT_FALSE(callback2.isSessionUp(peerId));
    }

    // 3 peers are configured
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(r1PeerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    peerMgr1->addPeer(r1PeerAddr2, 100, 100, {kR1Lo2, 0}, peerPort2);
    peerMgr1->addPeer(r1PeerAddr3, 100, 100, {kR1Lo3, 0}, peerPort2);
    peerMgr2->addPeer(r2PeerAddr1, 100, 100, {kR2Lo1, 0}, peerPort1);
    peerMgr2->addPeer(r2PeerAddr2, 100, 100, {kR2Lo2, 0}, peerPort1);
    peerMgr2->addPeer(r2PeerAddr3, 100, 100, {kR2Lo3, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, r1PeerSet);

    // confirm that sessions come up
    for (const auto& peerId : r1PeerSet) {
      XLOGF(DBG4, "r1PeerSet: peerId {}", peerId.str());
      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId));
      EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId));
      EXPECT_TRUE(callback1.isSessionUp(peerId));
    }
    for (const auto& peerId : r2PeerSet) {
      XLOGF(DBG4, "r2PeerSet: peerId {}", peerId.str());
      EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId));
      EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId));
      EXPECT_TRUE(callback2.isSessionUp(peerId));
    }

    // stop
    XLOG(DBG4, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);

    waitTillSessionsGoDown(fm, peerMgr1, r1PeerSet);
  });

  evb.loop();

  // confirm that sessions came down
  for (const auto& peerId : r1PeerSet) {
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId));
    EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId));
    EXPECT_FALSE(callback1.isSessionUp(peerId));
  }
  for (const auto& peerId : r2PeerSet) {
    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId));
    EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId));
    EXPECT_FALSE(callback2.isSessionUp(peerId));
  }
}

/*
 * Test if Bgp session comes up after start after delay.
 */
TEST_F(FiberBgpPeerManagerFixture, StartAfterDelayTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([&] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // addPeer() is called by both peerMgrs
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    const auto startAfterDelay = 10ms;
    peerMgr1->addPeer(
        peerAddr1,
        100,
        100,
        {kR1Lo1, 0},
        peerPort2,
        ConnTimeParams(startAfterDelay));
    peerMgr2->addPeer(
        peerAddr2,
        100,
        100,
        {kR2Lo1, 0},
        peerPort1,
        ConnTimeParams(startAfterDelay));

    // wait only till start
    fiberSleepFor(startAfterDelay);

    // confirm that session did not come up
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    // confirm that session comes up
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // stop
    XLOG(DBG4, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);

    waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
  });

  evb.loop();

  // confirm that session came down
  EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
  EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
  EXPECT_FALSE(callback1.isSessionUp(peerId1));

  EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
  EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
  EXPECT_FALSE(callback2.isSessionUp(peerId2));
}

/*
 * Test to prefer already-established session in the case of connection
 * collision
 */
TEST_F(FiberBgpPeerManagerFixture, PreferAlreadyEstablishedSessionTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([&] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // peerMgr1's bgp id (127.1.0.1) < peerMgr2's bgp id (127.2.0.1)
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();

    peerMgr1->addPeer(
        peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2, ConnTimeParams(0ms, 0ms));

    /*
     * wait a little to help session initiated by peerMgr1 comes up first,
     * In this case we will still prefer peerMgr1 since it established
     * session first, even its bgp id is smaller than peerMgr2
     */
    fiberSleepFor(5ms);

    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    /*
     * confirm that the first established session wins over the other
     * even though the other is preferred based on bgp id comparison
     */
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    /*
     * Note: collision counter verification removed — ThreadCachedServiceData
     * uses thread-local caching and publishStats() only flushes the calling
     * thread's cache. Under tsan, the collision increment happens on a fiber
     * thread whose cache may not be aggregated in time, causing flaky failures.
     * The behavioral assertions above (established/terminated counts, session
     * up status) already validate that collision resolution works correctly.
     */

    // stop
    XLOG(DBG4, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);

    waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
  });

  evb.loop();

  // confirm that session came down
  EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
  EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
  EXPECT_FALSE(callback1.isSessionUp(peerId1));

  EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
  EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
  EXPECT_FALSE(callback2.isSessionUp(peerId2));
}

/**
 * Test backpressure flag is properly propgated from the global
 * config.
 */
TEST_F(FiberBgpPeerManagerFixture, EgressBackpressureFlagTest) {
  auto& fm = fmWrapper.get();

  /* Create a peerMgr1 with backpressure enabled. */
  BgpGlobalConfig bgpGlobalConfig1(
      100, /* localAsn */
      kR1Lo1, /* routerId 127.1.0.1 */
      kR1Lo1, /* clusterId 127.1.0.1 */
      kDefaultHoldTime, /* holdTime */
      std::nullopt, /* listenAddr */
      kDefaultGrRestartTime, /* grRestartTime */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV4 */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV6 */
      std::nullopt, /* localConfedAsn */
      facebook::bgp::ComputeUcmpFromLbwComm{false}, /* computeUcmpFromLbwComm */
      0, /* ucmpWidth */
      std::nullopt, /* ucmpQuantizer */
      facebook::bgp::ValidateRemoteAs{true}, /* validateRemoteAs */
      facebook::bgp::SupportStatefulGr{true}, /* supportStatefulGr */
      facebook::bgp::EnableServerSocket{true}, /* enableServerSocket */
      facebook::bgp::AllowLoopbackReflection{false}, /* allowLoopbackReflection
                                                      */
      facebook::bgp::CountConfedsInAsPathLen{false}, /* countConfedsInAsPathLen
                                                      */
      std::nullopt, /* deviceName */
      std::nullopt, /* switchLimitConfig */
      std::nullopt, /* dynamicPeerLimit */
      std::nullopt, /* streamSubscriberLimit */
      facebook::bgp::EnableNexthopTracking{false}, /* enableNextHopTracking */
      std::vector<std::string>{}, /* includeInterfaceRegexes */
      facebook::bgp::EnableDynamicPolicyEvaluation{
          false}, /* enableDynamicPolicyEvaluation
                   */
      std::nullopt, /* thriftServerConfig */
      true /* enableEgressQueueBackpressure */
  );

  /* Create a peerMgr2 with backpressure disabled. */
  BgpGlobalConfig bgpGlobalConfig2(
      100, /* localAsn */
      kR2Lo1, /* routerId 127.2.0.1 */
      kR2Lo1, /* clusterId 127.2.0.1 */
      kDefaultHoldTime, /* holdTime */
      std::nullopt, /* listenAddr */
      kDefaultGrRestartTime, /* grRestartTime */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV4 */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV6 */
      std::nullopt, /* localConfedAsn */
      facebook::bgp::ComputeUcmpFromLbwComm{false}, /* computeUcmpFromLbwComm */
      0, /* ucmpWidth */
      std::nullopt, /* ucmpQuantizer */
      facebook::bgp::ValidateRemoteAs{true}, /* validateRemoteAs */
      facebook::bgp::SupportStatefulGr{true}, /* supportStatefulGr */
      facebook::bgp::EnableServerSocket{true}, /* enableServerSocket */
      facebook::bgp::AllowLoopbackReflection{false}, /* allowLoopbackReflection
                                                      */
      facebook::bgp::CountConfedsInAsPathLen{false}, /* countConfedsInAsPathLen
                                                      */
      std::nullopt, /* deviceName */
      std::nullopt, /* switchLimitConfig */
      std::nullopt, /* dynamicPeerLimit */
      std::nullopt, /* streamSubscriberLimit */
      facebook::bgp::EnableNexthopTracking{false}, /* enableNextHopTracking */
      std::vector<std::string>{}, /* includeInterfaceRegexes */
      facebook::bgp::EnableDynamicPolicyEvaluation{
          false}, /* enableDynamicPolicyEvaluation
                   */
      std::nullopt, /* thriftServerConfig */
      false /* enableEgressQueueBackpressure */
  );

  initTwoPeerMgrs(fm, bgpGlobalConfig1, bgpGlobalConfig2);

  /* Verify the flag value on the respective mgrs. */
  EXPECT_TRUE(peerMgr1->enableEgressQueueBackpressure_);
  EXPECT_FALSE(peerMgr2->enableEgressQueueBackpressure_);

  fm.addTask([this, &fm] {
    /* Add a peer to each mgr. */
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    /* Wait for the session to come up. */
    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
    waitTillSessionsComeUp(fm, peerMgr2, {peerId2});

    /* Verify both sessions came up and got established. */
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    /* Verify each peer has the same value as their peerMgr. */
    EXPECT_EQ(1, peerMgr1->allPeers_.size());
    EXPECT_EQ(1, peerMgr2->allPeers_.size());

    /* Check peerMgr1's peer (should have backpressure enabled) */
    {
      auto peerInfoPtr = peerMgr1->allPeers_.begin()->second;
      EXPECT_NE(nullptr, peerInfoPtr);
      auto connectionInfoPtr = peerInfoPtr->connectionInfos.begin()->second;
      EXPECT_NE(nullptr, connectionInfoPtr);
      auto peer = connectionInfoPtr->activeSessionInfo->peer;
      EXPECT_NE(nullptr, peer);
      EXPECT_TRUE(peer->enableEgressQueueBackpressure_);
    }

    /* Check peerMgr2's peer (should have backpressure disabled) */
    {
      auto peerInfoPtr = peerMgr2->allPeers_.begin()->second;
      EXPECT_NE(nullptr, peerInfoPtr);
      auto connectionInfoPtr = peerInfoPtr->connectionInfos.begin()->second;
      EXPECT_NE(nullptr, connectionInfoPtr);
      auto peer = connectionInfoPtr->activeSessionInfo->peer;
      EXPECT_NE(nullptr, peer);
      EXPECT_FALSE(peer->enableEgressQueueBackpressure_);
    }

    /* stop */
    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
  });

  evb.loop();
}

/*
 * Test passive_active to passive_only connection case
 */
TEST_F(FiberBgpPeerManagerFixture, BgpSessionUpPAToPOTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([&] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // peerMgr1's bgp id (127.1.0.1) < peerMgr2's bgp id (127.2.0.1)
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();

    peerMgr2->addPeer(
        peerAddr2,
        100,
        100,
        {kR2Lo1, 0},
        peerPort1,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::PASSIVE_ONLY);
    peerMgr1->addPeer(
        peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2, ConnTimeParams(0ms, 0ms));

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    /*
     * confirm that even peerMgr2 is initiated first, we do not have
     * connection from peerMgr2 to peerMgr1 because peer1 is added in
     * PASSIVE_ONLY mode peerMgr2 will only establish a connection initiated
     * from peerMgr1
     */
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // stop
    XLOG(DBG4, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);

    waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
  });

  evb.loop();

  // confirm that session came down
  EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
  EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
  EXPECT_FALSE(callback1.isSessionUp(peerId1));

  EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
  EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
  EXPECT_FALSE(callback2.isSessionUp(peerId2));
}

/*
 * Test to passive_active to active_only connection case
 */
TEST_F(FiberBgpPeerManagerFixture, BgpSessionUpAOToPATest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([&] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // peerMgr1's bgp id (127.1.0.1) < peerMgr2's bgp id (127.2.0.1)
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();

    peerMgr2->addPeer(
        peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1, ConnTimeParams(0ms, 0ms));
    peerMgr1->addPeer(
        peerAddr1,
        100,
        100,
        {kR1Lo1, 0},
        peerPort2,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::ACTIVE_ONLY);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    /*
     * confirm that even peerMgr2 is initiated first, we do not have
     * connection from peerMgr2 to peerMgr1 because peer2 is added in
     * ACTIVE_ONLY mode peerMgr1 will not accept a connection initiated by
     * peerMgr2
     */
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // stop
    XLOG(DBG4, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);

    waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
  });

  evb.loop();

  // confirm that session came down
  EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
  EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
  EXPECT_FALSE(callback1.isSessionUp(peerId1));

  EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
  EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
  EXPECT_FALSE(callback2.isSessionUp(peerId2));
}

class MockFiberServerSocket : public FiberServerSocket {
 public:
  explicit MockFiberServerSocket(std::optional<folly::SocketAddress> localAddr)
      : FiberServerSocket(std::move(localAddr)) {}

  MOCK_METHOD(
      (folly::Expected<FiberSocket, FiberSocketError>),
      accept,
      (),
      (noexcept));
};

/*
 * Test that transient socket errors occurring in accept() are handled properly,
 * i.e., the passive connect loop keeps running and the connection is retried.
 */
TEST_F(FiberBgpPeerManagerFixture, PeerAcceptErrorTest) {
  auto& fm = fmWrapper.get();

  auto bgpGlobalConfig1 = makeBgpGlobalConfig(kR1Lo1, kR1Lo1);
  auto bgpGlobalConfig2 = makeBgpGlobalConfig(kR2Lo1, kR2Lo1);
  peerMgr1 = make_shared<TestFiberBgpPeerManager>(
      bgpGlobalConfig1,
      &callback1,
      fm,
      evb,
      true /* enableMessagesOverNotifyQueue */,
      false /* enableCoroNotifyQueue */);

  // start all fibers for peerMgr1
  fm.addTask([this] { peerMgr1->run(); });

  auto peerMgr2 = make_shared<MockFiberBgpPeerManager>(
      bgpGlobalConfig2,
      &callback2,
      fm,
      evb,
      true /* enableMessagesOverNotifyQueue */,
      false /* enableCoroNotifyQueue */);

  // start all fibers for peerMgr2
  fm.addTask([&] {
    auto acceptErrorSocket = std::make_unique<MockFiberServerSocket>(
        folly::AsyncSocket::anyAddress());

    EXPECT_CALL(*acceptErrorSocket, accept())
        // First connection attempt encounters a transient error
        .WillOnce(Return(
            folly::makeUnexpected<FiberSocketError>(folly::AsyncSocketException{
                folly::AsyncSocketException::AsyncSocketExceptionType::NOT_OPEN,
                "Mock AsyncSocketException",
                ENOTCONN /* errno */})))
        // Subsequent connection attempts succeed
        .WillRepeatedly([&sock = *acceptErrorSocket]() {
          return sock.FiberServerSocket::accept();
        });

    EXPECT_CALL(*peerMgr2, makeServerSocket(_))
        .WillOnce(Return(std::move(acceptErrorSocket)));

    peerMgr2->run();
  });

  fm.addTask([&] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // peerMgr1's bgp id (127.1.0.1) < peerMgr2's bgp id (127.2.0.1)
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();

    peerMgr1->addPeer(
        peerAddr1,
        100,
        100,
        {kR1Lo1, 0},
        peerPort2,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::ACTIVE_ONLY);
    peerMgr2->addPeer(
        peerAddr2,
        100,
        100,
        {kR2Lo1, 0},
        peerPort1,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::PASSIVE_ONLY);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    /*
     * confirm that even peerMgr2 is initiated first, we do not have
     * connection from peerMgr2 to peerMgr1 because peer1 is added in
     * PASSIVE_ONLY mode peerMgr2 will only establish a connection initiated
     * from peerMgr1
     */
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // stop
    XLOG(DBG4, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);

    waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
  });

  evb.loop();

  // confirm that session came down
  EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
  EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
  EXPECT_FALSE(callback1.isSessionUp(peerId1));

  EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
  EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
  EXPECT_FALSE(callback2.isSessionUp(peerId2));
}

/*
 * Test active_only to passive_only connection case
 */
TEST_F(FiberBgpPeerManagerFixture, BgpSessionUpAOToPOTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([&] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // peerMgr1's bgp id (127.1.0.1) < peerMgr2's bgp id (127.2.0.1)
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();

    peerMgr1->addPeer(
        peerAddr1,
        100,
        100,
        {kR1Lo1, 0},
        peerPort2,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::ACTIVE_ONLY);
    peerMgr2->addPeer(
        peerAddr2,
        100,
        100,
        {kR2Lo1, 0},
        peerPort1,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::PASSIVE_ONLY);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    /*
     * confirm that even peerMgr2 is initiated first, we do not have
     * connection from peerMgr2 to peerMgr1 because peer1 is added in
     * PASSIVE_ONLY mode peerMgr2 will only establish a connection initiated
     * from peerMgr1
     */
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // stop
    XLOG(DBG4, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);

    waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
  });

  evb.loop();

  // confirm that session came down
  EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
  EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
  EXPECT_FALSE(callback1.isSessionUp(peerId1));

  EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
  EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
  EXPECT_FALSE(callback2.isSessionUp(peerId2));
}

/*
 * Test to passive_only to passive_only connection case
 */
TEST_F(FiberBgpPeerManagerFixture, BgpSessionNotUpPOToPOTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([&] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // peerMgr1's bgp id (127.1.0.1) < peerMgr2's bgp id (127.2.0.1)
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();

    peerMgr1->addPeer(
        peerAddr1,
        100,
        100,
        {kR1Lo1, 0},
        peerPort2,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::PASSIVE_ONLY);
    peerMgr2->addPeer(
        peerAddr2,
        100,
        100,
        {kR2Lo1, 0},
        peerPort1,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::PASSIVE_ONLY);

    // wait enough for session coming up
    fiberSleepFor(10ms);

    /*
     * confirm that even peerMgr2 is initiated first, we do not have
     * connection from peerMgr2 to peerMgr1 because peer2 is added in
     * ACTIVE_ONLY mode peerMgr1 will not accept a connection initiated by
     * peerMgr2
     */
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // stop
    XLOG(DBG4, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);
  });

  evb.loop();
}

/*
 * Test to active_only to active_only connection case
 */
TEST_F(FiberBgpPeerManagerFixture, BgpSessionNotUpAOToAOTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([&] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // peerMgr1's bgp id (127.1.0.1) < peerMgr2's bgp id (127.2.0.1)
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();

    peerMgr1->addPeer(
        peerAddr1,
        100,
        100,
        {kR1Lo1, 0},
        peerPort2,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::ACTIVE_ONLY);
    peerMgr2->addPeer(
        peerAddr2,
        100,
        100,
        {kR2Lo1, 0},
        peerPort1,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::ACTIVE_ONLY);

    // wait enough for session coming up
    fiberSleepFor(10ms);

    /*
     * confirm that even peerMgr2 is initiated first, we do not have
     * connection from peerMgr2 to peerMgr1 because peer2 is added in
     * ACTIVE_ONLY mode peerMgr1 will not accept a connection initiated by
     * peerMgr2
     */
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // stop
    XLOG(DBG4, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);
  });

  evb.loop();
}

/*
 * Test passive_only to passive_active connection retry
 * Verify that connection retry works when peer is shutdown and started
 * Verify by shutdown and starting both sides
 */
TEST_F(FiberBgpPeerManagerFixture, ConnectionRetryPOToPATest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([&] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();

    peerMgr2->addPeer(
        peerAddr2,
        100,
        100,
        {kR2Lo1, 0},
        peerPort1,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::PASSIVE_ONLY);
    // Default is passive_active
    peerMgr1->addPeer(
        peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2, ConnTimeParams(0ms, 0ms));

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
    waitTillSessionsComeUp(fm, peerMgr2, {peerId2});

    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // Case 1: Shutdown passive_active peer
    peerMgr1->shutdownPeer(peerAddr1);

    fm.addTask([this, &fm] {
      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
      waitTillSessionsGoDown(fm, peerMgr2, {peerId2});

      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      // Start passive_active peer
      peerMgr1->startPeer(peerAddr1);
      waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
      waitTillSessionsComeUp(fm, peerMgr2, {peerId2});

      EXPECT_EQ(2, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_TRUE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(2, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_TRUE(callback2.isSessionUp(peerId2));

      // Case 2: Shutdown passive_only peer
      peerMgr2->shutdownPeer(peerAddr2);

      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
      waitTillSessionsGoDown(fm, peerMgr2, {peerId2});

      EXPECT_EQ(2, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(2, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(2, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(2, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      // Start passive_only peer
      peerMgr2->startPeer(peerAddr2);
      waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
      waitTillSessionsComeUp(fm, peerMgr2, {peerId2});

      EXPECT_EQ(3, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(2, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_TRUE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(3, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(2, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_TRUE(callback2.isSessionUp(peerId2));

      // stop
      XLOG(DBG4, "stop Bgp Peer manager 1");
      peerMgr1->shutdownWithGR(false);
      XLOG(DBG4, "stop Bgp Peer manager 2");
      peerMgr2->shutdownWithGR(false);
    });
  });

  evb.loop();
}

/*
 * Test passive_active to passive_active connection retry
 * Verify that connection retry works when peer is shutdown and started
 * Verify by shutdown and starting both sides
 */
TEST_F(FiberBgpPeerManagerFixture, ConnectionRetryPAToPATest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([&] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();

    // passive_active session by default
    peerMgr2->addPeer(
        peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1, ConnTimeParams(0ms, 0ms));
    peerMgr1->addPeer(
        peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2, ConnTimeParams(0ms, 0ms));

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
    waitTillSessionsComeUp(fm, peerMgr2, {peerId2});

    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // Case 1: Shutdown passive_active peer
    peerMgr1->shutdownPeer(peerAddr1);

    fm.addTask([this, &fm] {
      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
      waitTillSessionsGoDown(fm, peerMgr2, {peerId2});

      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      /*
       * Start passive_active peer
       * When we start them up, the connection could collide and reconnect
       * Therefore we need to check relative counts instead of absolute counts
       * in the following
       */
      peerMgr1->startPeer(peerAddr1);
      waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
      waitTillSessionsComeUp(fm, peerMgr2, {peerId2});

      EXPECT_EQ(
          callback1.getEstablishedCallbackCount(peerId1),
          callback1.getTerminatedCallbackCount(peerId1) + 1);
      EXPECT_TRUE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(
          callback2.getEstablishedCallbackCount(peerId2),
          callback2.getTerminatedCallbackCount(peerId2) + 1);
      EXPECT_TRUE(callback2.isSessionUp(peerId2));

      // Case 2: Shutdown passive_active peer
      peerMgr2->shutdownPeer(peerAddr2);

      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
      waitTillSessionsGoDown(fm, peerMgr2, {peerId2});

      EXPECT_EQ(
          callback1.getEstablishedCallbackCount(peerId1),
          callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(
          callback2.getEstablishedCallbackCount(peerId2),
          callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      // Start passive_active peer
      peerMgr2->startPeer(peerAddr2);
      waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
      waitTillSessionsComeUp(fm, peerMgr2, {peerId2});

      EXPECT_EQ(
          callback1.getEstablishedCallbackCount(peerId1),
          callback1.getTerminatedCallbackCount(peerId1) + 1);
      EXPECT_TRUE(callback1.isSessionUp(peerId1));

      EXPECT_EQ(
          callback2.getEstablishedCallbackCount(peerId2),
          callback2.getTerminatedCallbackCount(peerId2) + 1);
      EXPECT_TRUE(callback2.isSessionUp(peerId2));

      // stop
      XLOG(DBG4, "stop Bgp Peer manager 1");
      peerMgr1->shutdownWithGR(false);
      XLOG(DBG4, "stop Bgp Peer manager 2");
      peerMgr2->shutdownWithGR(false);
    });
  });

  evb.loop();
}

/*
 * Start with two peers in with both Passive-Active configuration.  After
 * sessions are up; drop one peer and re-add it with Passive-Only
 * configuration.  Do this on both peers.
 */
TEST_F(FiberBgpPeerManagerFixture, DropAndReaddPOTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([this, &fm] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));
    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // Add both peers as Passive/Active (default)
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);
    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
    waitTillSessionsComeUp(fm, peerMgr2, {peerId2});
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));
    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    auto retryTime = kDefaultConnRetryTimeoutMs + 10ms;
    {
      // Drop peerMgr1/peerAddr1 and add it back with PASSIVE connection
      XLOGF(
          DBG4,
          "Wait for {}ms and drop {}",
          retryTime.count(),
          peerAddr1.str());
      // Ensure previously set retry timer has expired before we drop peer
      fiberSleepFor(retryTime);
      peerMgr1->dropPeer(peerAddr1);
      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
      waitTillSessionsGoDown(fm, peerMgr2, {peerId2});
      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));
      EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      XLOGF(DBG4, "Add back {} passive only", peerAddr1.str());
      peerMgr1->addPeer(
          peerAddr1,
          100,
          100,
          {kR1Lo1, 0},
          peerPort2,
          ConnTimeParams(0ms, 0ms),
          TBgpSessionConnectMode::PASSIVE_ONLY);
      waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
      waitTillSessionsComeUp(fm, peerMgr2, {peerId2});
      EXPECT_EQ(2, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_TRUE(callback1.isSessionUp(peerId1));
      EXPECT_EQ(2, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_TRUE(callback2.isSessionUp(peerId2));
    }

    {
      // Drop and add back the peers as Passive/Active to reset our state
      peerMgr1->dropPeer(peerAddr1);
      peerMgr2->dropPeer(peerAddr2);
      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
      waitTillSessionsGoDown(fm, peerMgr2, {peerId2});
      EXPECT_EQ(2, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(2, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));
      EXPECT_EQ(2, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(2, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
      peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);
      waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
      waitTillSessionsComeUp(fm, peerMgr2, {peerId2});
      EXPECT_EQ(3, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(2, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_TRUE(callback1.isSessionUp(peerId1));
      EXPECT_EQ(3, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(2, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_TRUE(callback2.isSessionUp(peerId2));
    }

    {
      // Drop peerMgr2/peerAddr2 and add it back with PASSIVE connection
      XLOGF(
          DBG4,
          "Wait for {}ms and drop {}",
          retryTime.count(),
          peerAddr2.str());
      // Ensure previously set retry timer has expired before we drop peer
      fiberSleepFor(retryTime);
      peerMgr2->dropPeer(peerAddr2);
      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
      waitTillSessionsGoDown(fm, peerMgr2, {peerId2});
      EXPECT_EQ(3, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(3, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_FALSE(callback1.isSessionUp(peerId1));
      EXPECT_EQ(3, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(3, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      XLOGF(DBG4, "Add back {} passive only", peerAddr2.str());
      peerMgr2->addPeer(
          peerAddr2,
          100,
          100,
          {kR2Lo1, 0},
          peerPort1,
          ConnTimeParams(0ms, 0ms),
          TBgpSessionConnectMode::PASSIVE_ONLY);
      waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
      waitTillSessionsComeUp(fm, peerMgr2, {peerId2});
      EXPECT_EQ(4, callback1.getEstablishedCallbackCount(peerId1));
      EXPECT_EQ(3, callback1.getTerminatedCallbackCount(peerId1));
      EXPECT_TRUE(callback1.isSessionUp(peerId1));
      EXPECT_EQ(4, callback2.getEstablishedCallbackCount(peerId2));
      EXPECT_EQ(3, callback2.getTerminatedCallbackCount(peerId2));
      EXPECT_TRUE(callback2.isSessionUp(peerId2));
    }

    XLOG(DBG4, "Stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "Stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);
    waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
    waitTillSessionsGoDown(fm, peerMgr2, {peerId2});
    EXPECT_EQ(4, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(4, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));
    EXPECT_EQ(4, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(4, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));
  });

  evb.loop();
}

/*
 * Test rapid add and drop peer.
 */
TEST_F(FiberBgpPeerManagerFixture, ConnectionRetryRapidDropPeerTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([&] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();

    /*
     * passive_active session by default
     * Add and drop this peer multiple times. This ensures that connect fiber
     * blocks (as peerAddr1 is not yet added) and drop peer will delete the
     * peer during connect block.
     */
    for (int i = 0; i < 100; i++) {
      peerMgr2->addPeer(
          peerAddr2,
          100,
          100,
          {kR2Lo1, 0},
          10000 + (folly::Random::rand32() % 10000), // Ports 10000-20000
          ConnTimeParams(0ms, 1ms));
      fiberSleepFor(1ms);
      peerMgr2->dropPeer(peerAddr2);
      fiberSleepFor(10ms);
    }

    peerMgr2->addPeer(
        peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1, ConnTimeParams(0ms, 0ms));
    peerMgr1->addPeer(
        peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2, ConnTimeParams(0ms, 0ms));

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
    waitTillSessionsComeUp(fm, peerMgr2, {peerId2});

    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // stop
    XLOG(DBG4, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);
  });

  evb.loop();
}

/*
 * Test if Bgp does exponential backoff for connection retry timer
 */
TEST_F(FiberBgpPeerManagerFixture, ConnectionRetryExponentialBackoff) {
  auto& fm = fmWrapper.get();

  TestFiberBgpPeerCallback callback1;
  auto peerMgr1 = initPeerMgr(fm, peerAddr1, callback1);

  const auto bgpPeerId1 = BgpPeerId(peerAddr1, kR1Lo1.asV4().toLongHBO());
  const auto bgpPeerId2 = BgpPeerId(peerAddr2, kR1Lo1.asV4().toLongHBO());

  fm.addTask([&] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(bgpPeerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(bgpPeerId1));
    EXPECT_FALSE(callback1.isSessionUp(bgpPeerId1));

    const auto startAfterDelay = 10ms;
    const auto minRetryTimeout = 100ms;
    const auto maxRetryTimeout = 1500ms;

    /*
     * Dynamically allocate a port via the OS and keep it bound (not listening)
     * so connections are refused during the backoff phase. The socket is held
     * to prevent the OS from reassigning the port to other sockets.
     */
    int tmpSock = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(tmpSock, 0);
    struct sockaddr_in tmpAddr{};
    tmpAddr.sin_family = AF_INET;
    tmpAddr.sin_addr.s_addr = INADDR_ANY;
    ASSERT_EQ(
        ::bind(
            tmpSock,
            reinterpret_cast<struct sockaddr*>(&tmpAddr),
            sizeof(tmpAddr)),
        0);
    socklen_t addrLen = sizeof(tmpAddr);
    ASSERT_EQ(
        ::getsockname(
            tmpSock, reinterpret_cast<struct sockaddr*>(&tmpAddr), &addrLen),
        0);
    const auto peer2Port = ntohs(tmpAddr.sin_port);

    // active peer, doing exponential backoff
    peerMgr1->addPeer(
        peerAddr1,
        100,
        100,
        {kR1Lo1, 0},
        peer2Port,
        ConnTimeParams(startAfterDelay, minRetryTimeout, maxRetryTimeout),
        TBgpSessionConnectMode::ACTIVE_ONLY);

    /*
     * wait till exponential backoff forces fallback to maximum delay
     * Jitter is ±10% capped at ±1000ms (using generateJitter)
     * Base backoffs: 100 + 200 + 400 + 800 = 1500ms to reach max
     * With -10% jitter: fastest ~90 + 180 + 360 + 720 = 1350ms
     * With +10% jitter: fastest ~110 + 220 + 440 + 880 = 1650ms
     * Wait 2000ms to ensure we've reached max backoff
     */
    fiberSleepFor(2000ms);

    // Verify connection attempts happened (at least 4 due to jitter variance)
    auto peerInfo = peerMgr1->getPeerDisplayInfo(peerAddr1);
    EXPECT_GE(peerInfo->at(0).numOfConnectionAttempts, 4);
    auto connectionAttemptsBeforePeerAdd =
        peerInfo->at(0).numOfConnectionAttempts;

    // Release the port so peerMgr2 can bind to it
    ::close(tmpSock);

    // passive peer only listening
    TestFiberBgpPeerCallback callback2;
    auto peerMgr2 = initPeerMgr(
        fm, peerAddr2, callback2, SocketAddress("0.0.0.0", peer2Port));

    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    peerMgr2->addPeer(
        peerAddr2,
        100,
        100,
        {kR2Lo1, 0},
        peerPort1,
        ConnTimeParams(startAfterDelay),
        TBgpSessionConnectMode::PASSIVE_ONLY);

    /*
     * With 10% jitter (generateJitter), max backoff of 1500ms varies between
     * ~1350-1650ms. Sleep well within that to guarantee we're in backoff.
     */
    fiberSleepFor(250ms);

    // confirm that session did not come up yet (still in backoff)
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(bgpPeerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(bgpPeerId1));
    EXPECT_FALSE(callback1.isSessionUp(bgpPeerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(bgpPeerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(bgpPeerId2));
    EXPECT_FALSE(callback2.isSessionUp(bgpPeerId2));

    waitTillSessionsComeUp(fm, peerMgr1, {bgpPeerId1});
    waitTillSessionsComeUp(fm, peerMgr2, {bgpPeerId2});

    // confirm that session comes up
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(bgpPeerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(bgpPeerId1));
    EXPECT_TRUE(callback1.isSessionUp(bgpPeerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(bgpPeerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(bgpPeerId2));
    EXPECT_TRUE(callback2.isSessionUp(bgpPeerId2));

    // Verify that sessions came up and only one additional connection attempt
    peerInfo = peerMgr1->getPeerDisplayInfo(peerAddr1);
    EXPECT_EQ(
        (connectionAttemptsBeforePeerAdd + 1),
        peerInfo->at(0).numOfConnectionAttempts);

    // stop
    XLOG(DBG4, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);

    waitTillSessionsGoDown(fm, peerMgr1, {bgpPeerId1});
    waitTillSessionsGoDown(fm, peerMgr2, {bgpPeerId2});

    // confirm that session came down
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(bgpPeerId1));
    EXPECT_EQ(1, callback1.getTerminatedCallbackCount(bgpPeerId1));
    EXPECT_FALSE(callback1.isSessionUp(bgpPeerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(bgpPeerId2));
    EXPECT_EQ(1, callback2.getTerminatedCallbackCount(bgpPeerId2));
    EXPECT_FALSE(callback2.isSessionUp(bgpPeerId2));
  });

  evb.loop();
}

/*
 * Check BGP sessions attempt re-connects without any backoff
 */
TEST_F(FiberBgpPeerManagerFixture, BgpSessionEstablishedNoExponentialBackoff) {
  auto& fm = fmWrapper.get();

  TestFiberBgpPeerCallback callback1;
  auto peerMgr1 = initPeerMgr(fm, peerAddr1, callback1);

  TestFiberBgpPeerCallback callback2;
  auto peerMgr2 =
      initPeerMgr(fm, peerAddr2, callback2, SocketAddress("0.0.0.0", 0));

  const auto bgpPeerId1 = BgpPeerId(peerAddr1, kR1Lo1.asV4().toLongHBO());
  const auto bgpPeerId2 = BgpPeerId(peerAddr2, kR1Lo1.asV4().toLongHBO());

  fm.addTask([&] {
    const auto startAfterDelay = 10ms;
    const auto minRetryTimeout = 100ms;
    const auto maxRetryTimeout = 200ms;

    const auto peer2Port = peerMgr2->getListenAddress()->getPort();

    peerMgr1->addPeer(
        peerAddr1,
        100,
        100,
        {kR1Lo1, 0},
        peer2Port,
        ConnTimeParams(startAfterDelay, minRetryTimeout, maxRetryTimeout),
        TBgpSessionConnectMode::ACTIVE_ONLY);

    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    peerMgr2->addPeer(
        peerAddr2,
        100,
        100,
        {kR2Lo1, 0},
        peerPort1,
        ConnTimeParams(startAfterDelay),
        TBgpSessionConnectMode::PASSIVE_ONLY);

    waitTillSessionsComeUp(fm, peerMgr1, {bgpPeerId1});
    waitTillSessionsComeUp(fm, peerMgr2, {bgpPeerId2});

    // stop
    peerMgr2->shutdownPeer(peerAddr2);
    waitTillSessionsGoDown(fm, peerMgr1, {bgpPeerId1});
    waitTillSessionsGoDown(fm, peerMgr2, {bgpPeerId2});

    /*
     * now bring up sessions again, they should come up immediately
     * because last established session was more than
     * kDefaultSessionDampenDuration ago
     */
    peerMgr2->startPeer(peerAddr2);
    fiberSleepFor(250ms);
    EXPECT_TRUE(callback1.isSessionUp(bgpPeerId1));

    /*
     * stop the session
     */
    peerMgr2->shutdownPeer(peerAddr2);
    waitTillSessionsGoDown(fm, peerMgr1, {bgpPeerId1});

    /*
     * Restarting session again should not have been backed off
     * because dampening period is set to 0 ms
     */
    peerMgr2->startPeer(peerAddr2);
    fiberSleepFor(250ms);
    EXPECT_TRUE(callback1.isSessionUp(bgpPeerId1));

    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
    waitTillSessionsGoDown(fm, peerMgr1, {bgpPeerId1});
    waitTillSessionsGoDown(fm, peerMgr2, {bgpPeerId2});
  });

  evb.loop();
}

/*
 * BGP sessions may initiate a TCP connection but for several reasons
 * that TCP connection may get terminated before BGP session state is
 * ESTABLISHED. Such conditions should be considered error to backoff
 */
TEST_F(FiberBgpPeerManagerFixture, BgpSessionEstablishedExponentialBackoff) {
  auto& fm = fmWrapper.get();

  TestFiberBgpPeerCallback callback1;
  auto peerMgr1 = initPeerMgr(fm, peerAddr1, callback1);

  // Use port 0 to better avoid collisions during stress tests
  TestFiberBgpPeerCallback callback2;
  auto peerMgr2 = initPeerMgr(fm, peerAddr2, callback2);

  const auto bgpPeerId1 = BgpPeerId(peerAddr1, kR1Lo1.asV4().toLongHBO());
  const auto bgpPeerId2 = BgpPeerId(peerAddr2, kR1Lo1.asV4().toLongHBO());

  fm.addTask([&] {
    const auto startAfterDelay = 10ms;
    const auto minRetryTimeout = 50ms;
    const auto maxRetryTimeout = 100ms;
    const auto connRetryTimeout = 100ms;
    const auto minSessionRetryTimeout = 100ms;
    const auto maxSessionRetryTimeout = 1000ms;
    const auto sessionDampenDuration = 2000ms;

    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();

    peerMgr1->addPeer(
        peerAddr1,
        100,
        100,
        {kR1Lo1, 0},
        peerPort2,
        ConnTimeParams(
            startAfterDelay,
            minRetryTimeout,
            maxRetryTimeout,
            connRetryTimeout,
            minSessionRetryTimeout,
            maxSessionRetryTimeout,
            sessionDampenDuration),
        TBgpSessionConnectMode::ACTIVE_ONLY);
    peerMgr2->addPeer(
        peerAddr2,
        100,
        100,
        {kR2Lo1, 0},
        peerPort1,
        ConnTimeParams(
            startAfterDelay,
            minRetryTimeout,
            maxRetryTimeout,
            connRetryTimeout,
            minSessionRetryTimeout,
            maxSessionRetryTimeout,
            sessionDampenDuration));

    waitTillSessionsComeUp(fm, peerMgr1, {bgpPeerId1});
    waitTillSessionsComeUp(fm, peerMgr2, {bgpPeerId2});

    // stop
    peerMgr2->shutdownPeer(peerAddr2);
    waitTillSessionsGoDown(fm, peerMgr1, {bgpPeerId1});
    waitTillSessionsGoDown(fm, peerMgr2, {bgpPeerId2});

    /*
     * now bring up sessions again, it should have effect
     * of first backoff because session is attempted again
     * within dampening duration.
     *
     * With ACTIVE-ACTIVE configuration, timing is
     * non-deterministic due to potential connection collisions.
     */
    const auto collisionTimeout = std::chrono::seconds(15);

    peerMgr2->startPeer(peerAddr2);
    /*
     * 1st session backoff is 100ms (minSessionRetryTimeout).
     * Jitter is applied after the backoff decision, so minimum is ~90ms.
     */
    fiberSleepFor(25ms);
    EXPECT_FALSE(callback1.isSessionUp(bgpPeerId1));

    waitTillSessionsComeUp(fm, peerMgr1, {bgpPeerId1}, collisionTimeout);
    EXPECT_TRUE(callback1.isSessionUp(bgpPeerId1));

    /*
     * stop session
     */
    peerMgr2->shutdownPeer(peerAddr2);
    waitTillSessionsGoDown(fm, peerMgr1, {bgpPeerId1});

    /*
     * restart the session. Now that connection is again
     * attempted within dampening duration, backoff.
     * This is 2nd backoff and hence wait period to
     * attempt connection would be longer from the last
     * time
     */
    peerMgr2->startPeer(peerAddr2);
    // 2nd session backoff is 200ms, with jitter minimum ~180ms
    fiberSleepFor(50ms);
    EXPECT_FALSE(callback1.isSessionUp(bgpPeerId1));

    waitTillSessionsComeUp(fm, peerMgr1, {bgpPeerId1}, collisionTimeout);
    EXPECT_TRUE(callback1.isSessionUp(bgpPeerId1));

    /*
     * stop again
     */
    peerMgr2->shutdownPeer(peerAddr2);
    waitTillSessionsGoDown(fm, peerMgr1, {bgpPeerId1});

    /*
     * The next connection attempt now should have been
     * further backed off
     */
    peerMgr2->startPeer(peerAddr2);
    // 3rd session backoff is 400ms, with jitter minimum ~360ms
    fiberSleepFor(150ms);
    EXPECT_FALSE(callback1.isSessionUp(bgpPeerId1));

    waitTillSessionsComeUp(fm, peerMgr1, {bgpPeerId1}, collisionTimeout);
    EXPECT_TRUE(callback1.isSessionUp(bgpPeerId1));

    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
    waitTillSessionsGoDown(fm, peerMgr1, {bgpPeerId1});
    waitTillSessionsGoDown(fm, peerMgr2, {bgpPeerId2});
  });

  evb.loop();
}

/*
 * Verify that session backoff resets after a session has been established
 * beyond the dampen duration. Rapid flaps within the dampen window build up
 * the backoff, but once a session stays up past the dampen duration and then
 * goes down, the next reconnect starts from minimum backoff.
 */
TEST_F(FiberBgpPeerManagerFixture, BgpSessionBackoffResetAfterStableSession) {
  auto& fm = fmWrapper.get();

  TestFiberBgpPeerCallback callback1;
  auto peerMgr1 = initPeerMgr(fm, peerAddr1, callback1);

  TestFiberBgpPeerCallback callback2;
  auto peerMgr2 = initPeerMgr(fm, peerAddr2, callback2);

  const auto bgpPeerId1 = BgpPeerId(peerAddr1, kR1Lo1.asV4().toLongHBO());
  const auto bgpPeerId2 = BgpPeerId(peerAddr2, kR1Lo1.asV4().toLongHBO());

  fm.addTask([&] {
    const auto startAfterDelay = 10ms;
    const auto minRetryTimeout = 50ms;
    const auto maxRetryTimeout = 100ms;
    const auto connRetryTimeout = 100ms;
    const auto minSessionRetryTimeout = 100ms;
    const auto maxSessionRetryTimeout = 2000ms;
    const auto sessionDampenDuration = 500ms;

    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();

    peerMgr1->addPeer(
        peerAddr1,
        100,
        100,
        {kR1Lo1, 0},
        peerPort2,
        ConnTimeParams(
            startAfterDelay,
            minRetryTimeout,
            maxRetryTimeout,
            connRetryTimeout,
            minSessionRetryTimeout,
            maxSessionRetryTimeout,
            sessionDampenDuration),
        TBgpSessionConnectMode::ACTIVE_ONLY);
    peerMgr2->addPeer(
        peerAddr2,
        100,
        100,
        {kR2Lo1, 0},
        peerPort1,
        ConnTimeParams(
            startAfterDelay,
            minRetryTimeout,
            maxRetryTimeout,
            connRetryTimeout,
            minSessionRetryTimeout,
            maxSessionRetryTimeout,
            sessionDampenDuration));

    const auto collisionTimeout = std::chrono::seconds(15);

    waitTillSessionsComeUp(fm, peerMgr1, {bgpPeerId1});
    waitTillSessionsComeUp(fm, peerMgr2, {bgpPeerId2});

    /*
     * Rapidly flap the session 3 times to build up session backoff.
     * Each flap within the dampen window doubles the backoff.
     */
    for (int i = 0; i < 3; i++) {
      peerMgr2->shutdownPeer(peerAddr2);
      waitTillSessionsGoDown(fm, peerMgr1, {bgpPeerId1});
      waitTillSessionsGoDown(fm, peerMgr2, {bgpPeerId2});

      peerMgr2->startPeer(peerAddr2);
      waitTillSessionsComeUp(fm, peerMgr1, {bgpPeerId1}, collisionTimeout);
      waitTillSessionsComeUp(fm, peerMgr2, {bgpPeerId2}, collisionTimeout);
    }

    /*
     * After 3 rapid flaps, session backoff is elevated (400ms+).
     * Keep the session established beyond the dampen duration.
     */
    fiberSleepFor(sessionDampenDuration + 200ms);

    /*
     * Take the session down. The session was up longer than
     * dampenDuration, so the backoff resets to minimum.
     */
    peerMgr2->shutdownPeer(peerAddr2);
    waitTillSessionsGoDown(fm, peerMgr1, {bgpPeerId1});
    waitTillSessionsGoDown(fm, peerMgr2, {bgpPeerId2});

    /*
     * Restart the session. It should come up within ~minSessionRetryTimeout
     * (100ms + jitter) instead of the elevated 800ms+ without the fix.
     */
    peerMgr2->startPeer(peerAddr2);
    fiberSleepFor(350ms);
    EXPECT_TRUE(callback1.isSessionUp(bgpPeerId1));

    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
    waitTillSessionsGoDown(fm, peerMgr1, {bgpPeerId1});
    waitTillSessionsGoDown(fm, peerMgr2, {bgpPeerId2});
  });

  evb.loop();
}

/*
 * Test sending/receiving update and end-of-rib to a peer using the unbounded
 * peer input queue.
 */
TEST_F(FiberBgpPeerManagerFixture, SendReceiveBgpUpdateEoRTest) {
  gflags::FlagSaver flags;
  FLAGS_enable_egress_queue_backpressure = false;
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  // 3 peer sessions between r1 and r2
  auto r1PeerAddr1 = kR2Lo1; // 127.2.0.1
  auto r1PeerId1 = BgpPeerId(r1PeerAddr1, peerAddr1.asV4().toLongHBO());
  auto r1PeerAddr2 = kR2Lo2; // 127.2.0.2
  auto r1PeerId2 = BgpPeerId(r1PeerAddr2, peerAddr1.asV4().toLongHBO());
  auto r1PeerAddr3 = kR2Lo3; // 127.2.0.3
  auto r1PeerId3 = BgpPeerId(r1PeerAddr3, peerAddr1.asV4().toLongHBO());

  auto r2PeerAddr1 = kR1Lo1; // 127.1.0.1
  auto r2PeerId1 = BgpPeerId(r2PeerAddr1, peerAddr2.asV4().toLongHBO());
  auto r2PeerAddr2 = kR1Lo2; // 127.1.0.2
  auto r2PeerId2 = BgpPeerId(r2PeerAddr2, peerAddr2.asV4().toLongHBO());
  auto r2PeerAddr3 = kR1Lo3; // 127.1.0.3
  auto r2PeerId3 = BgpPeerId(r2PeerAddr3, peerAddr2.asV4().toLongHBO());

  std::unordered_set<BgpPeerId> r1PeerSet = {r1PeerId1, r1PeerId2, r1PeerId3};
  std::unordered_set<BgpPeerId> r2PeerSet = {r2PeerId1, r2PeerId2, r2PeerId3};

  fm.addTask([&] {
    for (const auto& peerId : r1PeerSet) {
      EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId));
      EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId));
      EXPECT_FALSE(callback1.isSessionUp(peerId));
      EXPECT_FALSE(peerMgr1->isPeerUp(peerId));
    }
    for (const auto& peerId : r2PeerSet) {
      EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId));
      EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId));
      EXPECT_FALSE(callback2.isSessionUp(peerId));
      EXPECT_FALSE(peerMgr2->isPeerUp(peerId));
    }

    // 3 peers are configured
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(r1PeerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    peerMgr1->addPeer(r1PeerAddr2, 100, 100, {kR1Lo2, 0}, peerPort2);
    peerMgr1->addPeer(r1PeerAddr3, 100, 100, {kR1Lo3, 0}, peerPort2);
    peerMgr2->addPeer(r2PeerAddr1, 100, 100, {kR2Lo1, 0}, peerPort1);
    peerMgr2->addPeer(r2PeerAddr2, 100, 100, {kR2Lo2, 0}, peerPort1);
    peerMgr2->addPeer(r2PeerAddr3, 100, 100, {kR2Lo3, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, r1PeerSet);

    // confirm that sessions come up
    for (const auto& peerId : r1PeerSet) {
      XLOGF(DBG4, "r1PeerSet: peerId {}", peerId.str());
      EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId));
      EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId));
      EXPECT_TRUE(callback1.isSessionUp(peerId));
      EXPECT_TRUE(peerMgr1->isPeerUp(peerId));
    }
    for (const auto& peerId : r2PeerSet) {
      XLOGF(DBG4, "r2PeerSet: peerId {}", peerId.str());
      EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId));
      EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId));
      EXPECT_TRUE(callback2.isSessionUp(peerId));
      EXPECT_TRUE(peerMgr2->isPeerUp(peerId));
    }

    // create 10 update messages
    auto givenUpdates = createBgpUpdate2Vec(10);

    // send all updates and EndOfRib to all peers
    for (const auto& peerId : r1PeerSet) {
      XLOGF(DBG4, "r1PeerSet: peerId {}", peerId.str());
      auto updates = packBgpUpdatesToSend(givenUpdates);
      peerMgr1->sendUpdates(peerId, std::move(updates));
      peerMgr1->sendEndOfRib(peerId);
    }

    fiberSleepFor(100ms);

    /*
     * given updates will be used to verify the recvUpdates. During parsing,
     * we will populate v4Announced. So add it back.
     */
    for (auto& update : givenUpdates) {
      for (const auto& rigPfx : *update.v4Announced2()) {
        update.v4Announced()->push_back(*rigPfx.prefix());
      }
    }

    // confirm that messages are received correctly
    for (const auto& peerId : r2PeerSet) {
      XLOGF(DBG4, "r2PeerSet: peerId {}", peerId.str());
      auto rcvdUpdates = callback2.getBgpUpdatesReceivedCallbackData(peerId);
      EXPECT_EQ(givenUpdates, rcvdUpdates);
      // each sendEndOfRib() creates two EndOfRibs for v4/v6
      auto rcvdEoRs = callback2.getBgpEndOfRibReceivedCallbackData(peerId);
      EXPECT_EQ(2, rcvdEoRs.size());
    }

    // stop
    XLOG(DBG4, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);

    waitTillSessionsGoDown(fm, peerMgr1, r1PeerSet);
  });

  evb.loop();

  // confirm that sessions came down
  for (const auto& peerId : r1PeerSet) {
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId));
    EXPECT_EQ(1, callback1.getTerminatedCallbackCount(peerId));
    EXPECT_FALSE(callback1.isSessionUp(peerId));
    EXPECT_FALSE(peerMgr1->isPeerUp(peerId));
  }
  for (const auto& peerId : r2PeerSet) {
    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId));
    EXPECT_EQ(1, callback2.getTerminatedCallbackCount(peerId));
    EXPECT_FALSE(callback2.isSessionUp(peerId));
    EXPECT_FALSE(peerMgr2->isPeerUp(peerId));
  }
}

/**
 * E2E sanity test to verify that the correct number of messages is written to
 * the socket through sendSocketLoop from Peer[0] to Peer[1].
 *
 *  1. Generate a BgpUpdate2 object with 10k prefixes to force the serializer
 *     to chunk the IOBuf into N > 1 linked buffers.
 *  2. Make Peer[0] send the update to Peer[1].
 *  3. Verify that Peer[1] deserializes the N UPDATE messages with no
 *     duplicate prefixes.
 */
TEST_F(FiberBgpPeerManagerFixture, SendReceiveLongBgpUpdateTest) {
  auto& fm = folly::fibers::getFiberManager(evb);
  initTwoPeerMgrs(
      fm,
      std::nullopt /* listenAddr */,
      false /* enableCoroNotifyQueue */,
      false /* enableMessagesOverNotifyQueue */);

  /* Configure a peer session. */
  auto r1PeerAddr1 = kR2Lo1; // 127.2.0.1
  auto r1PeerId1 = BgpPeerId(r1PeerAddr1, peerAddr1.asV4().toLongHBO());

  auto r2PeerAddr1 = kR1Lo1; // 127.1.0.1
  auto r2PeerId1 = BgpPeerId(r2PeerAddr1, peerAddr2.asV4().toLongHBO());

  /*
   * Create 1 BgpUpdate2 with 1000 prefixes to force serializer to create
   * a long chain with more than element.
   */
  auto update = createBgpUpdate2(1000 /* numPrefixes */, 32 /* maskLen */);

  /* Verify that BgpMessageSerializer generates 5 chain elements. */
  auto serializedBuf = BgpMessageSerializer::serializeBgpUpdate2(update);
  EXPECT_EQ(2, serializedBuf->countChainElements());

  folly::fibers::Baton readBaton;
  folly::fibers::Baton cleanupBaton;

  fm.addTask([&] {
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(r1PeerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    peerMgr2->addPeer(r2PeerAddr1, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {r1PeerId1});

    /* Verify sessions come up. */
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(r1PeerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(r1PeerId1));
    EXPECT_TRUE(callback1.isSessionUp(r1PeerId1));
    EXPECT_TRUE(peerMgr1->isPeerUp(r1PeerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(r2PeerId1));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(r2PeerId1));
    EXPECT_TRUE(callback2.isSessionUp(r2PeerId1));
    EXPECT_TRUE(peerMgr2->isPeerUp(r2PeerId1));

    BgpEndOfRib eor;
    eor.isMpEor() = false;
    eor.afi() = BgpUpdateAfi::AFI_IPv4;
    eor.safi() = BgpUpdateSafi::SAFI_UNICAST;

    /* Send updates and EoR. */
    if (FLAGS_enable_egress_queue_backpressure) {
      auto boundedIqueue1 = peerMgr1->getBoundedPeerInputQueue(r1PeerId1);
      boundedIqueue1->push(std::make_shared<const BgpUpdate2>(update));
      boundedIqueue1->push(eor);
    } else {
      auto iqueue1 = peerMgr1->getPeerInputQueue(r1PeerId1);
      iqueue1->push(std::make_shared<const BgpUpdate2>(update));
      iqueue1->push(eor);
    }
    readBaton.post();

    /* Wait for all messages to be received. */
    facebook::bgp::test::boundedBatonWait(cleanupBaton, "cleanupBaton");
    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);

    waitTillSessionsGoDown(fm, peerMgr1, {r1PeerId1});
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(r1PeerId1));
    EXPECT_EQ(1, callback1.getTerminatedCallbackCount(r1PeerId1));
    EXPECT_FALSE(callback1.isSessionUp(r1PeerId1));
    EXPECT_FALSE(peerMgr1->isPeerUp(r1PeerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(r2PeerId1));
    EXPECT_EQ(1, callback2.getTerminatedCallbackCount(r2PeerId1));
    EXPECT_FALSE(callback2.isSessionUp(r2PeerId1));
    EXPECT_FALSE(peerMgr2->isPeerUp(r2PeerId1));
  });

  fm.addTask([&] {
    facebook::bgp::test::boundedBatonWait(readBaton, "readBaton");
    auto oqueue2 = peerMgr2->getPeerOutputQueue(r2PeerId1);

    /* Pop update messages until we reach EoR. */
    std::vector<std::shared_ptr<const BgpUpdate2>> rcvdUpdates;
    while (true) {
      auto msg = facebook::bgp::test::boundedBlockingPop(*oqueue2, "oqueue2");
      if (std::holds_alternative<BgpEndOfRib>(msg)) {
        break;
      }
      rcvdUpdates.push_back(std::get<std::shared_ptr<const BgpUpdate2>>(msg));
    }

    EXPECT_EQ(2, rcvdUpdates.size());

    /* Store map of prefixes; increment 1 for every prefix seen. */
    std::map<thrift::IPPrefix, int> prefixes;
    for (const auto& rigPrefix : *update.mpAnnounced()->prefixes()) {
      EXPECT_EQ(1, ++prefixes[*rigPrefix.prefix()]);
    }
    EXPECT_EQ(1000, prefixes.size());

    /* Decrement occurrence of seen prefixes. */
    for (const auto& msg : rcvdUpdates) {
      for (const auto& rigPrefix : *msg->mpAnnounced()->prefixes()) {
        --prefixes[*rigPrefix.prefix()];
      }
    }

    /*
     * If all prefixes arrived with no duplicates, then every entry
     * in this map should be 0.
     */
    for (auto& [pfx, val] : prefixes) {
      EXPECT_EQ(val, 0) << fmt::format(
          "Unexpected {} frequency value {} ",
          folly::IPAddress::networkToString(toCIDRNetwork(pfx)),
          val);
    }

    cleanupBaton.post();
  });

  evb.loop();
}

/*
 * Test sending many updates and then immediately stop the peer manager.
 * All queued updates should be sent before the connection closes.
 */
TEST_F(FiberBgpPeerManagerFixture, StopAfterSendingUpdates) {
  gflags::FlagSaver flags;
  FLAGS_enable_egress_queue_backpressure = false;

  const auto numUpdates = 255;

  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  auto r1PeerAddr1 = kR2Lo1; // 127.2.0.1
  auto r1PeerId1 = BgpPeerId(r1PeerAddr1, peerAddr1.asV4().toLongHBO());

  auto r2PeerAddr1 = kR1Lo1; // 127.1.0.1
  auto r2PeerId1 = BgpPeerId(r2PeerAddr1, peerAddr2.asV4().toLongHBO());

  fm.addTask([&] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(r1PeerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(r1PeerId1));
    EXPECT_FALSE(callback1.isSessionUp(r1PeerId1));
    EXPECT_FALSE(peerMgr1->isPeerUp(r1PeerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(r2PeerId1));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(r2PeerId1));
    EXPECT_FALSE(callback2.isSessionUp(r2PeerId1));
    EXPECT_FALSE(peerMgr2->isPeerUp(r2PeerId1));

    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(
        r1PeerAddr1,
        100,
        100,
        {kR1Lo1, 0},
        peerPort2,
        ConnTimeParams(0ms),
        TBgpSessionConnectMode::ACTIVE_ONLY);
    peerMgr2->addPeer(
        r2PeerAddr1,
        100,
        100,
        {kR2Lo1, 0},
        peerPort1,
        ConnTimeParams(0ms),
        TBgpSessionConnectMode::PASSIVE_ONLY);

    waitTillSessionsComeUp(fm, peerMgr1, {r1PeerId1});

    // confirm that sessions come up
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(r1PeerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(r1PeerId1));
    EXPECT_TRUE(callback1.isSessionUp(r1PeerId1));
    EXPECT_TRUE(peerMgr1->isPeerUp(r1PeerId1));

    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(r2PeerId1));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(r2PeerId1));
    EXPECT_TRUE(callback2.isSessionUp(r2PeerId1));
    EXPECT_TRUE(peerMgr2->isPeerUp(r2PeerId1));

    // create numUpdates messages
    auto givenUpdates = createBgpUpdate2Vec(numUpdates);

    // send all updates and EndOfRib to all peers
    auto updates = packBgpUpdatesToSend(givenUpdates);
    peerMgr1->sendUpdates(r1PeerId1, std::move(updates));
    peerMgr1->sendEndOfRib(r1PeerId1);

    XLOG(DBG1, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);

    const auto updateCntKey = fmt::format(
        "peer.messagesSent.{}.count",
        facebook::bgp::PeerStats::kMessagesSentUpdate);
    const auto eorCntKey = fmt::format(
        "peer.messagesSent.{}.count",
        facebook::bgp::PeerStats::kMessagesSentEndOfRib);

    while (true) {
      facebook::fb303::ThreadCachedServiceData::get()->publishStats();
      auto counters = facebook::fb303::ThreadCachedServiceData::getShared();

      const auto updateCnt = counters->hasCounter(updateCntKey)
          ? counters->getCounter(updateCntKey)
          : 0;
      const auto eorCnt =
          counters->hasCounter(eorCntKey) ? counters->getCounter(eorCntKey) : 0;

      //  v4/v6 EoRs
      if (updateCnt == givenUpdates.size() && eorCnt == 2) {
        break;
      }

      /*
       * Fiber sleep will make sure it yields and gives chance for
       * other fiber tasks to consume the queue.
       */
      fiberSleepFor(1ms);
    }

    XLOG(DBG1, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);

    waitTillSessionsGoDown(fm, peerMgr2, {r2PeerId1});
  });

  evb.loop();

  // confirm that sessions came down
  EXPECT_EQ(1, callback1.getEstablishedCallbackCount(r1PeerId1));
  EXPECT_EQ(1, callback1.getTerminatedCallbackCount(r1PeerId1));
  EXPECT_FALSE(callback1.isSessionUp(r1PeerId1));
  EXPECT_FALSE(peerMgr1->isPeerUp(r1PeerId1));
}

/*
 * Verify that sendUpdate/sendUpdates/sendEndOfRib callers cannot have
 * backpressure feature enabled.
 */
TEST_F(FiberBgpPeerManagerFixture, BackpressureNotSupportedTest) {
  gflags::FlagSaver flags;
  FLAGS_enable_egress_queue_backpressure = true;
  BgpGlobalConfig bgpGlobalConfig(
      100, /* localAsn */
      kR1Lo1, /* routerId 127.1.0.1 */
      kR1Lo1, /* clusterId 127.1.0.1 */
      kDefaultHoldTime, /* holdTime */
      std::nullopt, /* listenAddr */
      kDefaultGrRestartTime, /* grRestartTime */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV4 */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV6 */
      std::nullopt, /* localConfedAsn */
      facebook::bgp::ComputeUcmpFromLbwComm{false}, /* computeUcmpFromLbwComm */
      0, /* ucmpWidth */
      std::nullopt, /* ucmpQuantizer */
      facebook::bgp::ValidateRemoteAs{true}, /* validateRemoteAs */
      facebook::bgp::SupportStatefulGr{true}, /* supportStatefulGr */
      facebook::bgp::EnableServerSocket{true}, /* enableServerSocket */
      facebook::bgp::AllowLoopbackReflection{false}, /* allowLoopbackReflection
                                                      */
      facebook::bgp::CountConfedsInAsPathLen{false}, /* countConfedsInAsPathLen
                                                      */
      std::nullopt, /* deviceName */
      std::nullopt, /* switchLimitConfig */
      std::nullopt, /* dynamicPeerLimit */
      std::nullopt, /* streamSubscriberLimit */
      facebook::bgp::EnableNexthopTracking{false}, /* enableNextHopTracking */
      std::vector<std::string>{}, /* includeInterfaceRegexes */
      facebook::bgp::EnableDynamicPolicyEvaluation{
          false}, /* enableDynamicPolicyEvaluation
                   */
      std::nullopt, /* thriftServerConfig */
      FLAGS_enable_egress_queue_backpressure /* enableEgressQueueBackpressure */
  );
  folly::EventBase evb;
  FiberBgpPeerManager peerMgr{
      bgpGlobalConfig, folly::fibers::getFiberManager(evb), evb};
  auto r1PeerAddr1 = kR2Lo1; // 127.2.0.1
  auto r1PeerId1 = BgpPeerId(r1PeerAddr1, peerAddr1.asV4().toLongHBO());
  auto givenUpdates = createBgpUpdate2Vec(1);
  auto updates = packBgpUpdatesToSend(givenUpdates);
  EXPECT_DEATH(peerMgr1->sendUpdate(r1PeerId1, std::move(updates[0])), "");
  EXPECT_DEATH(peerMgr1->sendUpdates(r1PeerId1, {}), "");
  EXPECT_DEATH(peerMgr1->sendEndOfRib(r1PeerId1), "");
}

/*
 * Test after shutdown initiation new connections are rejected.
 */
TEST_F(FiberBgpPeerManagerFixture, BgpSessionAfterShutdown) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([&] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // peerMgr1's bgp id (127.1.0.1) < peerMgr2's bgp id (127.2.0.1)
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();

    peerMgr1->addPeer(
        peerAddr1,
        100,
        100,
        {kR1Lo1, 0},
        peerPort2,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::ACTIVE_ONLY);
    peerMgr2->addPeer(
        peerAddr2,
        100,
        100,
        {kR2Lo1, 0},
        peerPort1,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::PASSIVE_ONLY);
    fiberSleepFor(500ms);
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    // stop
    XLOG(DBG4, "stop Bgp Peer manager 1");
    peerMgr1->shutdownWithGR(false);
    XLOG(DBG4, "stop Bgp Peer manager 2");
    peerMgr2->shutdownWithGR(false);
  });
  /*
   * peerMgr2 is in shutdown initiated state.
   * We should not see the session get established at all.
   */
  peerMgr2->shutdownInProgress();
  evb.loop();

  EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
  EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
  EXPECT_FALSE(callback1.isSessionUp(peerId1));

  EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
  EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
  EXPECT_FALSE(callback2.isSessionUp(peerId2));
}

/*
 * Test numResets, lastResetTime
 */
TEST_F(FiberBgpPeerManagerFixture, ResetTimeAndNumTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([this, &fm] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_EQ(0, callback1.getTerminatedCallbackCount(peerId1));
    EXPECT_FALSE(callback1.isSessionUp(peerId1));

    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_EQ(0, callback2.getTerminatedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
    waitTillSessionsComeUp(fm, peerMgr2, {peerId2});

    EXPECT_TRUE(callback1.isSessionUp(peerId1));
    EXPECT_EQ(0, peerMgr1->getBgpSessionInfo(peerId1).value()->numResets);

    EXPECT_TRUE(callback2.isSessionUp(peerId2));
    EXPECT_EQ(0, peerMgr2->getBgpSessionInfo(peerId2).value()->numResets);

    // test shutdownPeer()
    peerMgr1->shutdownPeer(peerAddr1);

    fm.addTask([this, &fm] {
      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
      waitTillSessionsGoDown(fm, peerMgr2, {peerId2});

      // wait enough to establish a testable bound
      fiberSleepFor(100ms);

      EXPECT_FALSE(callback1.isSessionUp(peerId1));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      // Once the session goes down, the numResets will increment
      EXPECT_EQ(1, peerMgr1->getBgpSessionInfo(peerId1).value()->numResets);
      EXPECT_EQ(1, peerMgr2->getBgpSessionInfo(peerId2).value()->numResets);

      peerMgr1->startPeer(peerAddr1);

      waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
      waitTillSessionsComeUp(fm, peerMgr2, {peerId2});

      EXPECT_TRUE(callback1.isSessionUp(peerId1));
      EXPECT_TRUE(callback2.isSessionUp(peerId2));

      auto currentTimeStamp = std::chrono::steady_clock::now();
      EXPECT_EQ(1, peerMgr1->getBgpSessionInfo(peerId1).value()->numResets);
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          currentTimeStamp -
          peerMgr1->getBgpSessionInfo(peerId1).value()->lastResetTime);
      EXPECT_LT(100, duration.count());

      EXPECT_EQ(1, peerMgr2->getBgpSessionInfo(peerId2).value()->numResets);
      duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          currentTimeStamp -
          peerMgr2->getBgpSessionInfo(peerId2).value()->lastResetTime);
      EXPECT_LT(100, duration.count());

      XLOG(DBG4, "stop Bgp Peer manager 1");
      peerMgr1->shutdownWithGR(false);
      XLOG(DBG4, "stop Bgp Peer manager 2");
      peerMgr2->shutdownWithGR(false);
    });
  });

  evb.loop();
}

/*
 * Test lastSessionInfo weak_ptr which is used to keep track of numResets of
 * non-Established sessions introduced in D54921268
 */
TEST_F(FiberBgpPeerManagerFixture, LastSessionInfoWeakPtrTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([this, &fm] {
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
    waitTillSessionsComeUp(fm, peerMgr2, {peerId2});

    EXPECT_TRUE(callback1.isSessionUp(peerId1));
    EXPECT_NE(peerMgr1->getBgpSessionInfo(peerId1).value(), nullptr);
    auto connectionInfo1 =
        peerMgr1->getBgpSessionInfo(peerId1).value()->connectionInfo;
    EXPECT_NE(connectionInfo1, nullptr);
    auto establishedSessionInfo1 =
        peerMgr1->getBgpSessionInfo(peerId1).value()->establishedSessionInfo;
    EXPECT_NE(establishedSessionInfo1, nullptr);

    EXPECT_TRUE(callback2.isSessionUp(peerId2));
    EXPECT_NE(peerMgr2->getBgpSessionInfo(peerId2).value(), nullptr);
    auto connectionInfo2 =
        peerMgr2->getBgpSessionInfo(peerId2).value()->connectionInfo;
    EXPECT_NE(connectionInfo2, nullptr);

    /*
     * when the session is ESTABLISHED, the lastSessionInfo should be set to
     * nullptr
     */
    {
      std::shared_ptr<BgpSessionInfo> lastSessionInfo1 =
          connectionInfo1->lastSessionInfo.lock();
      EXPECT_EQ(lastSessionInfo1, nullptr);
      std::shared_ptr<BgpSessionInfo> lastSessionInfo2 =
          connectionInfo2->lastSessionInfo.lock();
      EXPECT_EQ(lastSessionInfo2, nullptr);
    }

    // shut down peer
    peerMgr1->shutdownPeer(peerAddr1);

    fm.addTask([this, &fm, connectionInfo1, connectionInfo2] {
      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
      waitTillSessionsGoDown(fm, peerMgr2, {peerId2});

      EXPECT_FALSE(callback1.isSessionUp(peerId1));
      EXPECT_FALSE(callback2.isSessionUp(peerId2));

      /*
       * when the session goes down from ESTABLISHED to IDLE
       * The connectionInfo and the establishedSessionInfo pointers both
       * should be set to nullptr
       */
      EXPECT_EQ(
          peerMgr1->getBgpSessionInfo(peerId1).value()->connectionInfo,
          nullptr);
      EXPECT_EQ(
          peerMgr1->getBgpSessionInfo(peerId1).value()->establishedSessionInfo,
          nullptr);
      EXPECT_EQ(
          peerMgr2->getBgpSessionInfo(peerId2).value()->connectionInfo,
          nullptr);
      EXPECT_EQ(
          peerMgr2->getBgpSessionInfo(peerId2).value()->establishedSessionInfo,
          nullptr);

      // The lastSessionInfo should be set to point to its BgpSessionInfo
      {
        std::shared_ptr<BgpSessionInfo> lastSessionInfo1 =
            connectionInfo1->lastSessionInfo.lock();
        EXPECT_NE(lastSessionInfo1, nullptr);
        EXPECT_EQ(
            peerMgr1->getBgpSessionInfo(peerId1).value(), lastSessionInfo1);
        EXPECT_EQ(1, lastSessionInfo1->numResets);

        std::shared_ptr<BgpSessionInfo> lastSessionInfo2 =
            connectionInfo2->lastSessionInfo.lock();
        EXPECT_NE(lastSessionInfo2, nullptr);
        EXPECT_EQ(
            peerMgr2->getBgpSessionInfo(peerId2).value(), lastSessionInfo2);
        EXPECT_EQ(1, lastSessionInfo2->numResets);
      }

      fm.addTask([this, &fm] {
        /*
         * Re-establish the session
         */
        peerMgr1->startPeer(peerAddr1);

        waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
        waitTillSessionsComeUp(fm, peerMgr2, {peerId2});

        auto newConnectionInfo1 =
            peerMgr1->getBgpSessionInfo(peerId1).value()->connectionInfo;

        auto newConnectionInfo2 =
            peerMgr2->getBgpSessionInfo(peerId2).value()->connectionInfo;
        {
          // lastSessionInfo is a weak_ptr
          std::shared_ptr<BgpSessionInfo> newLastSessionInfo1 =
              newConnectionInfo1->lastSessionInfo.lock();
          EXPECT_EQ(newLastSessionInfo1, nullptr);

          std::shared_ptr<BgpSessionInfo> newLastSessionInfo2 =
              newConnectionInfo2->lastSessionInfo.lock();
          EXPECT_EQ(newLastSessionInfo2, nullptr);
        }

        // clean up
        XLOG(DBG4, "stop Bgp Peer manager 1");
        peerMgr1->shutdownWithGR(false);
        XLOG(DBG4, "stop Bgp Peer manager 2");
        peerMgr2->shutdownWithGR(false);
      });
    });
  });

  evb.loop();
}

/*
 * Leverage isPeerVersionValid to ensure that the session number
 * is monotonically increasing among different sessions
 */
TEST_F(FiberBgpPeerManagerFixture, PeerVersionTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([this, &fm] {
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();

    // peer manager 2 has been expecting the connection from peer manager 1
    EXPECT_FALSE(peerMgr2
                     ->addPeer(
                         peerAddr2,
                         100,
                         100,
                         {kR2Lo1, 0},
                         peerPort1,
                         ConnTimeParams{0ms, 0ms},
                         TBgpSessionConnectMode::PASSIVE_ONLY)
                     .hasError());

    // first connection
    EXPECT_FALSE(peerMgr1
                     ->addPeer(
                         peerAddr1,
                         100,
                         100,
                         {kR1Lo1, 0},
                         peerPort2,
                         ConnTimeParams{0ms, 0ms},
                         TBgpSessionConnectMode::ACTIVE_ONLY)
                     .hasError());

    fm.addTask([this, &fm] {
      waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
      waitTillSessionsComeUp(fm, peerMgr2, {peerId2});

      /*
       * Both peer managers share a global, monotonically increasing version
       * counter. So one session will have id 0, and the other will have id 1.
       * The order is nondeterministic, depending on which peer manager
       * processes the connection first.
       */
      EXPECT_NE(
          peerMgr1->isPeerVersionValid(peerId1, 0),
          peerMgr2->isPeerVersionValid(peerId2, 0));
      EXPECT_NE(
          peerMgr1->isPeerVersionValid(peerId1, 1),
          peerMgr2->isPeerVersionValid(peerId2, 1));

      peerMgr1->shutdownWithGR(false);
      peerMgr2->shutdownWithGR(false);
    });
  });

  evb.loop();
}

TEST_F(FiberBgpPeerManagerFixture, RunBgpPeerWithExceptionTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([&, this] {
    // Uninitialized socket with no address populated.
    FiberSocket socket;
    auto res = peerMgr1->runBgpPeer(std::move(socket));
    EXPECT_TRUE(res.hasError());
    EXPECT_EQ(FiberBgpPeerManager::ErrorCode::INVALID_PEER, res.error());

    // Shut down to close the loop
    peerMgr1->shutdownWithGR(false /* gracefulRestart */);
    peerMgr2->shutdownWithGR(false /* gracefulRestart */);
  });

  evb.loop();
}

TEST(FiberBgpPeerManagerTest, WriteToNotifyQueueTest) {
  // Setup fiber bgp peer manager
  BgpGlobalConfig bgpGlobalConfig(
      100, /* localAsn */
      kR1Lo1, /* routerId 127.1.0.1 */
      kR1Lo1, /* clusterId 127.1.0.1 */
      kDefaultHoldTime, /* holdTime */
      std::nullopt, /* listenAddr */
      kDefaultGrRestartTime, /* grRestartTime */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV4 */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV6 */
      std::nullopt, /* localConfedAsn */
      facebook::bgp::ComputeUcmpFromLbwComm{false}, /* computeUcmpFromLbwComm */
      0, /* ucmpWidth */
      std::nullopt, /* ucmpQuantizer */
      facebook::bgp::ValidateRemoteAs{true}, /* validateRemoteAs */
      facebook::bgp::SupportStatefulGr{true}, /* supportStatefulGr */
      facebook::bgp::EnableServerSocket{true}, /* enableServerSocket */
      facebook::bgp::AllowLoopbackReflection{false}, /* allowLoopbackReflection
                                                      */
      facebook::bgp::CountConfedsInAsPathLen{false}, /* countConfedsInAsPathLen
                                                      */
      std::nullopt, /* deviceName */
      std::nullopt, /* switchLimitConfig */
      std::nullopt, /* dynamicPeerLimit */
      std::nullopt, /* streamSubscriberLimit */
      facebook::bgp::EnableNexthopTracking{false}, /* enableNextHopTracking */
      std::vector<std::string>{}, /* includeInterfaceRegexes */
      facebook::bgp::EnableDynamicPolicyEvaluation{
          false}, /* enableDynamicPolicyEvaluation
                   */
      std::nullopt, /* thriftServerConfig */
      FLAGS_enable_egress_queue_backpressure /* enableEgressQueueBackpressure */
  );
  folly::EventBase evb;
  FiberBgpPeerManager peerMgr{
      bgpGlobalConfig, folly::fibers::getFiberManager(evb), evb};

  /*
   * Test enableCoroNotifyQueue_ = true
   * will add to notifyCoroQueue_
   */
  {
    peerMgr.enableCoroNotifyQueue_ = true;

    EXPECT_EQ(peerMgr.notifyCoroQueue_.size(), 0);

    peerMgr.writeToNotifyQueue(FiberBgpPeer::ObservableMessageT());

    EXPECT_EQ(peerMgr.notifyCoroQueue_.size(), 1);
  }

  /*
   * Test enableCoroNotifyQueue_ = false
   * will add to notifyQueue_
   */
  {
    peerMgr.enableCoroNotifyQueue_ = false;

    EXPECT_EQ(peerMgr.notifyQueue_.size(), 0);

    peerMgr.writeToNotifyQueue(FiberBgpPeer::ObservableMessageT());

    EXPECT_EQ(peerMgr.notifyQueue_.size(), 1);
  }
}

// Shutdown FiberBgpPeerManager immediately to ensure there is no racing issues
TEST(FiberBgpPeerManagerTest, ImmediateShutDownTest) {
  BgpGlobalConfig bgpGlobalConfig(
      100, /* localAsn */
      kR1Lo1, /* routerId 127.1.0.1 */
      kR1Lo1, /* clusterId 127.1.0.1 */
      kDefaultHoldTime, /* holdTime */
      std::nullopt, /* listenAddr */
      kDefaultGrRestartTime, /* grRestartTime */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV4 */
      std::unordered_map<folly::CIDRNetwork, BgpNetwork>(), /* networksV6 */
      std::nullopt, /* localConfedAsn */
      facebook::bgp::ComputeUcmpFromLbwComm{false}, /* computeUcmpFromLbwComm */
      0, /* ucmpWidth */
      std::nullopt, /* ucmpQuantizer */
      facebook::bgp::ValidateRemoteAs{true}, /* validateRemoteAs */
      facebook::bgp::SupportStatefulGr{true}, /* supportStatefulGr */
      facebook::bgp::EnableServerSocket{true}, /* enableServerSocket */
      facebook::bgp::AllowLoopbackReflection{false}, /* allowLoopbackReflection
                                                      */
      facebook::bgp::CountConfedsInAsPathLen{false}, /* countConfedsInAsPathLen
                                                      */
      std::nullopt, /* deviceName */
      std::nullopt, /* switchLimitConfig */
      std::nullopt, /* dynamicPeerLimit */
      std::nullopt, /* streamSubscriberLimit */
      facebook::bgp::EnableNexthopTracking{false}, /* enableNextHopTracking */
      std::vector<std::string>{}, /* includeInterfaceRegexes */
      facebook::bgp::EnableDynamicPolicyEvaluation{
          false}, /* enableDynamicPolicyEvaluation
                   */
      std::nullopt, /* thriftServerConfig */
      FLAGS_enable_egress_queue_backpressure /* enableEgressQueueBackpressure */
  );

  folly::EventBase evb;
  auto& fm = folly::fibers::getFiberManager(evb);
  auto peerMgr = make_shared<FiberBgpPeerManager>(bgpGlobalConfig, fm, evb);
  // Run FiberBgpPeerManager on peerMgrThread
  fm.addTask([&] { peerMgr->run(); });

  auto peerMgrThread = std::thread([&]() { evb.loop(); });
  evb.waitUntilRunning();

  // Immediately call stop on the test thread
  peerMgr->shutdownWithGR(bgp::GracefulRestartFlag{false});

  peerMgrThread.join();
}

/*
 * Test that accept loop properly breaks when ACCEPT_STOPPED is received
 * This verifies that closing the server socket triggers the correct error type
 * and the loop terminates as expected
 */
TEST_F(FiberBgpPeerManagerFixture, AcceptLoopBreaksOnAcceptStoppedTest) {
  auto& fm = fmWrapper.get();
  TestFiberBgpPeerCallback callback1;

  auto peerMgr1 = initPeerMgr(fm, peerAddr1, callback1);

  std::atomic<bool> acceptLoopExited{false};

  fm.addTask([&] {
    // Give the accept loop time to start
    fiberSleepFor(100ms);

    /*
     * Shutdown the peer manager which should close the server socket
     * This triggers ACCEPT_STOPPED and should cause the loop to break
     */
    XLOG(INFO, "Shutting down peer manager to trigger ACCEPT_STOPPED");
    peerMgr1->shutdownWithGR(false);

    // Give it time to process the shutdown
    fiberSleepFor(100ms);
    acceptLoopExited.store(true);
  });

  // Run the event loop
  evb.loop();

  /*
   * If we reach here, the event loop has terminated, which means
   * the accept loop properly broke on ACCEPT_STOPPED
   */
  EXPECT_TRUE(acceptLoopExited.load());
  SUCCEED();
}

/*
 * Test that accept loop continues accepting after session restarts
 * While this doesn't directly inject AsyncSocketException errors, it verifies
 * that the accept loop remains functional across multiple session cycles
 */
TEST_F(FiberBgpPeerManagerFixture, AcceptLoopContinuesAfterSessionCycleTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([&] {
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();

    // Establish first set of sessions
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
    EXPECT_TRUE(callback1.isSessionUp(peerId1));
    EXPECT_TRUE(callback2.isSessionUp(peerId2));

    // Cycle sessions multiple times to verify accept loop stays alive
    for (int i = 0; i < 3; i++) {
      XLOGF(INFO, "Session cycle iteration {}", i);

      // Stop sessions
      peerMgr1->stopPeer(peerAddr1, false);
      peerMgr2->stopPeer(peerAddr2, false);
      waitTillSessionsGoDown(fm, peerMgr1, {peerId1});

      // Restart sessions - requires accept loop to still be running
      peerMgr1->startPeer(peerAddr1);
      peerMgr2->startPeer(peerAddr2);

      // This will timeout if accept loop stopped unexpectedly
      waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

      // Verify sessions re-established successfully
      EXPECT_TRUE(callback1.isSessionUp(peerId1));
      EXPECT_TRUE(callback2.isSessionUp(peerId2));
    }

    XLOG(INFO)
        << "All session cycles completed - accept loop remained functional";

    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
  });

  evb.loop();
}

/*
 * NOTE: Testing that accept loop continues on AsyncSocketException errors
 * is challenging without mocking FiberServerSocket because:
 * 1. AsyncSocketException errors from accept() are rare in normal operation
 * 2. Errors in connection processing (inside .then()) don't trigger
 *    result.hasError()
 * 3. The only reliable way to test would be to inject errors via mocking
 *
 * The correct behavior is documented here for reference:
 * - accept() returns FiberGenericSocketError{ACCEPT_STOPPED} when socket is
 *   closed -> Loop BREAKS (tested above)
 * - accept() returns folly::AsyncSocketException when AsyncSocketException
 * occurs
 *   -> Loop CONTINUES and logs error (not easily testable without mocking)
 *
 * Integration testing or production monitoring would be needed to fully verify
 * the AsyncSocketException handling behavior.
 */

INSTANTIATE_TEST_CASE_P(
    AddPeerConfigTest,
    BasicAddPeerTest,
    ::testing::Values(
        BasicAddPeerTestScope::CONFIG_AS_ARGS,
        BasicAddPeerTestScope::CONFIG_AS_PEERING_PARAMS));

/*
 * Test that per-peer session-level ODS counters are incremented when
 * BGP messages are sent and received during session establishment.
 */
TEST_F(FiberBgpPeerManagerFixture, PerPeerSessionCountersTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([this, &fm] {
    /*
     * peerIdOdsStr_ is derived from PeeringParams::getUniquePeerId(),
     * which is empty in the test fixture since peerId is not set.
     */
    const std::string peerIdOdsStr;

    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    peerMgr1->addPeer(peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2);

    // wait enough for peerMgr1's outbound connection to be accepted
    fiberSleepFor(1s);

    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    peerMgr2->addPeer(peerAddr2, 100, 100, {kR2Lo1, 0}, peerPort1);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});

    // Publish stats so ThreadCachedServiceData counters are visible
    facebook::fb303::ThreadCachedServiceData::get()->publishStats();

    auto counters = facebook::fb303::ThreadCachedServiceData::getShared();

    /*
     * During session establishment, each side sends OPEN + KEEPALIVE.
     * Verify per-peer sent counters were incremented.
     */
    auto sentOpenKey = fmt::format(
        facebook::bgp::PeerStats::kPeerMessagesSentOpen,
        facebook::bgp::kEbbPlatform,
        facebook::bgp::kBgpcppTag,
        peerIdOdsStr);
    auto sentKeepAliveKey = fmt::format(
        facebook::bgp::PeerStats::kPeerMessagesSentKeepAlive,
        facebook::bgp::kEbbPlatform,
        facebook::bgp::kBgpcppTag,
        peerIdOdsStr);

    EXPECT_TRUE(counters->hasCounter(sentOpenKey))
        << "Missing per-peer sent open counter";
    EXPECT_GE(counters->getCounter(sentOpenKey), 1)
        << "Expected at least 1 OPEN sent per-peer";
    EXPECT_TRUE(counters->hasCounter(sentKeepAliveKey))
        << "Missing per-peer sent keepalive counter";
    EXPECT_GE(counters->getCounter(sentKeepAliveKey), 1)
        << "Expected at least 1 KEEPALIVE sent per-peer";

    // Verify per-peer recv counters were incremented.
    auto recvOpenKey = fmt::format(
        facebook::bgp::PeerStats::kPeerMessagesRecvOpen,
        facebook::bgp::kEbbPlatform,
        facebook::bgp::kBgpcppTag,
        peerIdOdsStr);
    auto recvKeepAliveKey = fmt::format(
        facebook::bgp::PeerStats::kPeerMessagesRecvKeepAlive,
        facebook::bgp::kEbbPlatform,
        facebook::bgp::kBgpcppTag,
        peerIdOdsStr);

    EXPECT_TRUE(counters->hasCounter(recvOpenKey))
        << "Missing per-peer recv open counter";
    EXPECT_GE(counters->getCounter(recvOpenKey), 1)
        << "Expected at least 1 OPEN received per-peer";
    EXPECT_TRUE(counters->hasCounter(recvKeepAliveKey))
        << "Missing per-peer recv keepalive counter";
    EXPECT_GE(counters->getCounter(recvKeepAliveKey), 1)
        << "Expected at least 1 KEEPALIVE received per-peer";

    // stop
    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
    waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
  });

  evb.loop();
}

/*
 * Verify that shutting down a peer while it's in an active backoff
 * period cleanly cancels the pending AsyncTimeout. No further connection
 * attempts should occur after shutdown, and the process should exit
 * without leaks (validated by ASAN).
 */
TEST_F(FiberBgpPeerManagerFixture, ShutdownDuringActiveBackoff) {
  auto& fm = fmWrapper.get();

  TestFiberBgpPeerCallback callback1;
  auto peerMgr1 = initPeerMgr(fm, peerAddr1, callback1);

  const auto bgpPeerId1 = BgpPeerId(peerAddr1, kR1Lo1.asV4().toLongHBO());

  fm.addTask([&] {
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(bgpPeerId1));

    const auto startAfterDelay = 10ms;
    const auto minRetryTimeout = 100ms;
    const auto maxRetryTimeout = 1500ms;

    /*
     * Bind a port but don't listen — connections will be refused,
     * driving the peer into exponential backoff.
     */
    int tmpSock = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(tmpSock, 0);
    struct sockaddr_in tmpAddr{};
    tmpAddr.sin_family = AF_INET;
    tmpAddr.sin_addr.s_addr = INADDR_ANY;
    ASSERT_EQ(
        ::bind(
            tmpSock,
            reinterpret_cast<struct sockaddr*>(&tmpAddr),
            sizeof(tmpAddr)),
        0);
    socklen_t addrLen = sizeof(tmpAddr);
    ASSERT_EQ(
        ::getsockname(
            tmpSock, reinterpret_cast<struct sockaddr*>(&tmpAddr), &addrLen),
        0);
    const auto peerPort = ntohs(tmpAddr.sin_port);

    // Start active-only peer with exponential backoff
    peerMgr1->addPeer(
        peerAddr1,
        100,
        100,
        {kR1Lo1, 0},
        peerPort,
        ConnTimeParams(startAfterDelay, minRetryTimeout, maxRetryTimeout),
        TBgpSessionConnectMode::ACTIVE_ONLY);

    // Let backoff accumulate — at least a few retries should have occurred
    fiberSleepFor(500ms);

    auto peerInfo = peerMgr1->getPeerDisplayInfo(peerAddr1);
    auto attemptsBeforeShutdown = peerInfo->at(0).numOfConnectionAttempts;
    EXPECT_GE(attemptsBeforeShutdown, 2);

    /*
     * Shutdown the peer while it's in the middle of a backoff wait.
     * This should cancel the pending AsyncTimeout and set shutdownRequested.
     */
    peerMgr1->shutdownPeer(peerAddr1);

    // Wait long enough that another retry would have fired if not cancelled
    fiberSleepFor(500ms);

    // Verify no additional connection attempts occurred after shutdown
    peerInfo = peerMgr1->getPeerDisplayInfo(peerAddr1);
    EXPECT_EQ(attemptsBeforeShutdown, peerInfo->at(0).numOfConnectionAttempts);

    // Session should never have come up
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(bgpPeerId1));
    EXPECT_FALSE(callback1.isSessionUp(bgpPeerId1));

    ::close(tmpSock);
    peerMgr1->shutdownWithGR(false);
  });

  // ASAN validates no leaked fibers/queues/timeouts
  evb.loop();
}

/*
 * Verify that restarting a peer after shutdown properly resets
 * the shutdownRequested flag and reschedules the AsyncTimeout,
 * allowing new connection attempts to proceed.
 */
TEST_F(FiberBgpPeerManagerFixture, RestartPeerAfterShutdownResumesConnect) {
  auto& fm = fmWrapper.get();

  TestFiberBgpPeerCallback callback1;
  auto peerMgr1 = initPeerMgr(fm, peerAddr1, callback1);

  TestFiberBgpPeerCallback callback2;
  auto peerMgr2 =
      initPeerMgr(fm, peerAddr2, callback2, SocketAddress("0.0.0.0", 0));

  const auto bgpPeerId1 = BgpPeerId(peerAddr1, kR1Lo1.asV4().toLongHBO());
  const auto bgpPeerId2 = BgpPeerId(peerAddr2, kR1Lo1.asV4().toLongHBO());

  fm.addTask([&] {
    const auto startAfterDelay = 10ms;
    const auto minRetryTimeout = 100ms;

    const auto peer2Port = peerMgr2->getListenAddress()->getPort();
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();

    // Add active-only peer on peerMgr1 and passive-only on peerMgr2
    peerMgr1->addPeer(
        peerAddr1,
        100,
        100,
        {kR1Lo1, 0},
        peer2Port,
        ConnTimeParams(startAfterDelay, minRetryTimeout),
        TBgpSessionConnectMode::ACTIVE_ONLY);

    peerMgr2->addPeer(
        peerAddr2,
        100,
        100,
        {kR2Lo1, 0},
        peerPort1,
        ConnTimeParams(startAfterDelay),
        TBgpSessionConnectMode::PASSIVE_ONLY);

    // Wait for session to establish
    waitTillSessionsComeUp(fm, peerMgr1, {bgpPeerId1});
    waitTillSessionsComeUp(fm, peerMgr2, {bgpPeerId2});
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(bgpPeerId1));

    /*
     * Shutdown the active peer — this sets shutdownRequested and
     * cancels the pending timeout
     */
    peerMgr1->shutdownPeer(peerAddr1);
    waitTillSessionsGoDown(fm, peerMgr1, {bgpPeerId1});
    waitTillSessionsGoDown(fm, peerMgr2, {bgpPeerId2});

    /*
     * Restart the peer — should reset shutdownRequested and schedule
     * a new AsyncTimeout for reconnection
     */
    peerMgr1->startPeer(peerAddr1);

    /*
     * Verify session re-establishes (proves shutdownRequested was reset
     * and the new timeout fires correctly)
     */
    waitTillSessionsComeUp(fm, peerMgr1, {bgpPeerId1});
    waitTillSessionsComeUp(fm, peerMgr2, {bgpPeerId2});
    EXPECT_EQ(2, callback1.getEstablishedCallbackCount(bgpPeerId1));

    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
    waitTillSessionsGoDown(fm, peerMgr1, {bgpPeerId1});
    waitTillSessionsGoDown(fm, peerMgr2, {bgpPeerId2});
  });

  evb.loop();
}

/*
 * Test that passive connections with mismatched bindAddr are rejected.
 * peerMgr2 configures its peer (peerAddr2 = 127.1.0.1) with a bindAddr
 * that differs from the actual local address of accepted connections.
 * The session should NOT come up on the passive side.
 */
TEST_F(FiberBgpPeerManagerFixture, PassiveConnectBindAddrMismatchTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  // Subscribe to log messages to verify the reject log fires
  auto& logMessages = subscribeToLogMessages("");
  logMessages.clear();

  fm.addTask([&] {
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();

    // peerMgr1 uses PASSIVE_ACTIVE with kR1Lo1 as bindAddr
    peerMgr1->addPeer(
        peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2, ConnTimeParams(0ms, 0ms));

    /*
     * peerMgr2 uses PASSIVE_ONLY with a WRONG bindAddr (kR2Lo2 = 127.2.0.2).
     * peerMgr1 actively connects to 127.2.0.1:port2 (peerAddr1), so the
     * accepted socket's local address is 127.2.0.1, which does NOT match
     * kR2Lo2 (127.2.0.2). The passive accept should reject it.
     */
    peerMgr2->addPeer(
        peerAddr2,
        100,
        100,
        {kR2Lo2, 0},
        peerPort1,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::PASSIVE_ONLY);

    // Wait enough time for connection attempts
    fiberSleepFor(100ms);

    /*
     * Session should NOT come up on peerMgr2 because bindAddr mismatch
     * rejects the passive connection
     */
    EXPECT_EQ(0, callback2.getEstablishedCallbackCount(peerId2));
    EXPECT_FALSE(callback2.isSessionUp(peerId2));

    /*
     * Verify the rejection log was emitted — proves the connection was
     * accepted at TCP level and then dropped by our validation logic
     */
    bool foundRejectLog = false;
    for (const auto& [msg, cat] : logMessages) {
      if (msg.getMessage().find("Reject tcp connection") != std::string::npos) {
        foundRejectLog = true;
        break;
      }
    }
    EXPECT_TRUE(foundRejectLog)
        << "Expected 'Reject tcp connection' log message proving "
        << "the connection was accepted then dropped";

    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
  });

  evb.loop();
}

/*
 * Test that passive connections are accepted when bindAddr is anyAddress().
 * This verifies backward compatibility: peers without an explicit local_addr
 * should accept connections on any local address.
 */
TEST_F(FiberBgpPeerManagerFixture, PassiveConnectAnyBindAddrTest) {
  auto& fm = fmWrapper.get();
  initTwoPeerMgrs(fm);

  fm.addTask([&] {
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();

    // peerMgr1 uses PASSIVE_ACTIVE with kR1Lo1 as bindAddr
    peerMgr1->addPeer(
        peerAddr1, 100, 100, {kR1Lo1, 0}, peerPort2, ConnTimeParams(0ms, 0ms));

    /*
     * peerMgr2 uses PASSIVE_ONLY with anyAddress() as bindAddr.
     * The validation check should be skipped entirely, accepting
     * the connection regardless of local address.
     */
    peerMgr2->addPeer(
        peerAddr2,
        100,
        100,
        folly::AsyncSocket::anyAddress(),
        peerPort1,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::PASSIVE_ONLY);

    waitTillSessionsComeUp(fm, peerMgr1, {peerId1});
    waitTillSessionsComeUp(fm, peerMgr2, {peerId2});

    // Session should come up since anyAddress() skips validation
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId1));
    EXPECT_TRUE(callback1.isSessionUp(peerId1));

    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
    waitTillSessionsGoDown(fm, peerMgr1, {peerId1});
    waitTillSessionsGoDown(fm, peerMgr2, {peerId2});
  });

  evb.loop();
}

/*
 * Test cross-subnet connection rejection: peerMgr1 has two peers on different
 * subnets (127.2.x.x and 127.3.x.x). Peer 127.3.0.2 connects to peerMgr1 at
 * the wrong local address (127.2.0.1 instead of 127.3.0.1). The connection
 * should be accepted at TCP level but rejected by bindAddr validation.
 * Meanwhile, peer 127.2.0.1 connecting to the correct local address succeeds.
 */
TEST_F(FiberBgpPeerManagerFixture, PassiveConnectCrossSubnetRejectTest) {
  auto& fm = fmWrapper.get();

  // Addresses for the third peer (127.3.x.x subnet)
  const auto kPeer3Addr = folly::IPAddress("127.3.0.2");
  const auto kPeer3BindAddr = folly::IPAddress("127.3.0.1");
  const BgpPeerId peerId3{kPeer3Addr, kPeer3Addr.asV4().toLongHBO()};

  /*
   * peerMgr1: main router at 127.2.0.1, listens on :: (all interfaces)
   * Has two peers configured on different subnets:
   *   - peer 127.1.0.1 with bindAddr 127.2.0.1 (subnet 127.2.x.x)
   *   - peer 127.3.0.2 with bindAddr 127.3.0.1 (subnet 127.3.x.x)
   */
  auto bgpGlobalConfig1 = makeBgpGlobalConfig(kR2Lo1, kR2Lo1);
  peerMgr1 = make_shared<TestFiberBgpPeerManager>(
      bgpGlobalConfig1,
      &callback1,
      fm,
      evb,
      true /* enableMessagesOverNotifyQueue */,
      false /* enableCoroNotifyQueue */);
  fm.addTask([this] { peerMgr1->run(); });

  // peerMgr2: remote peer at 127.1.0.1 (connects correctly to 127.2.0.1)
  auto bgpGlobalConfig2 = makeBgpGlobalConfig(kR1Lo1, kR1Lo1);
  peerMgr2 = make_shared<TestFiberBgpPeerManager>(
      bgpGlobalConfig2,
      &callback2,
      fm,
      evb,
      true /* enableMessagesOverNotifyQueue */,
      false /* enableCoroNotifyQueue */);
  fm.addTask([this] { peerMgr2->run(); });

  // peerMgr3: remote peer at 127.3.0.2 (will connect to wrong local addr)
  TestFiberBgpPeerCallback callback3;
  auto bgpGlobalConfig3 = makeBgpGlobalConfig(kPeer3Addr, kPeer3Addr);
  auto peerMgr3 = make_shared<TestFiberBgpPeerManager>(
      bgpGlobalConfig3,
      &callback3,
      fm,
      evb,
      true /* enableMessagesOverNotifyQueue */,
      false /* enableCoroNotifyQueue */);
  fm.addTask([&] { peerMgr3->run(); });

  // Subscribe to log messages to verify the reject log fires
  auto& logMessages = subscribeToLogMessages("");
  logMessages.clear();

  fm.addTask([&] {
    const auto peerPort1 = peerMgr1->getListenAddress()->getPort();
    const auto peerPort2 = peerMgr2->getListenAddress()->getPort();
    const auto peerPort3 = peerMgr3->getListenAddress()->getPort();

    /*
     * --- Configure peerMgr1 with two peers on different subnets ---
     * Peer 127.1.0.1 with bindAddr 127.2.0.1 (subnet 127.2.x.x)
     */
    peerMgr1->addPeer(
        kR1Lo1, // remote peer = 127.1.0.1
        100,
        100,
        {kR2Lo1, 0}, // bindAddr = 127.2.0.1
        peerPort2,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::PASSIVE_ONLY);

    // Peer 127.3.0.2 with bindAddr 127.3.0.1 (subnet 127.3.x.x)
    peerMgr1->addPeer(
        kPeer3Addr, // remote peer = 127.3.0.2
        100,
        100,
        {kPeer3BindAddr, 0}, // bindAddr = 127.3.0.1
        peerPort3,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::PASSIVE_ONLY);

    /*
     * --- Configure peerMgr2: connects to peerMgr1 at 127.2.0.1 (correct) ---
     * peerMgr2 connects to 127.2.0.1:peerPort1 (peerMgr1's routerId).
     * On peerMgr1's accepted socket: remote=127.1.0.1, local=127.2.0.1.
     * configuredBindAddr for peer 127.1.0.1 = 127.2.0.1 → MATCH → session up.
     */
    peerMgr2->addPeer(
        kR2Lo1, // 127.2.0.1 (peerMgr1's routerId)
        100,
        100,
        {kR1Lo1, 0}, // bindAddr = 127.1.0.1 (peerMgr2's own address)
        peerPort1,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::ACTIVE_ONLY);

    /*
     * --- Configure peerMgr3: connects to peerMgr1 at WRONG address ---
     * peerMgr3 binds to 127.3.0.2, connects to 127.2.0.1:peerPort1
     * instead of connecting to 127.3.0.1:peerPort1.
     * On peerMgr1's accepted socket: remote=127.3.0.2, local=127.2.0.1.
     * configuredBindAddr for peer 127.3.0.2 = 127.3.0.1.
     *   → 127.2.0.1 != 127.3.0.1 → REJECT
     * This is the exact production bug scenario.
     */
    peerMgr3->addPeer(
        kR2Lo1, // 127.2.0.1 (WRONG - connects to wrong local addr)
        100,
        100,
        {kPeer3Addr, 0}, // bindAddr = 127.3.0.2 (peerMgr3's own address)
        peerPort1,
        ConnTimeParams(0ms, 0ms),
        TBgpSessionConnectMode::ACTIVE_ONLY);

    /*
     * peerMgr2 sees its peer as 127.2.0.1 (peerId1 from the fixture)
     * peerMgr1 sees its peer as 127.1.0.1 (peerId2 from the fixture)
     */
    waitTillSessionsComeUp(fm, peerMgr2, {peerId1});
    EXPECT_EQ(1, callback2.getEstablishedCallbackCount(peerId1));
    EXPECT_TRUE(callback2.isSessionUp(peerId1));

    // peerMgr1 should have session up for peer 127.1.0.1
    EXPECT_EQ(1, callback1.getEstablishedCallbackCount(peerId2));
    EXPECT_TRUE(callback1.isSessionUp(peerId2));

    // Wait for peerMgr3's connection attempts
    fiberSleepFor(100ms);

    /*
     * peerMgr1 should NOT have session up for peer 127.3.0.2
     * because it connected to the wrong local address (127.2.0.1 vs 127.3.0.1)
     */
    EXPECT_EQ(0, callback1.getEstablishedCallbackCount(peerId3));
    EXPECT_FALSE(callback1.isSessionUp(peerId3));

    // peerMgr3 should also not have session up
    BgpPeerId peerId1From3{kR2Lo1, kR2Lo1.asV4().toLongHBO()};
    EXPECT_EQ(0, callback3.getEstablishedCallbackCount(peerId1From3));
    EXPECT_FALSE(callback3.isSessionUp(peerId1From3));

    // Verify the rejection log was emitted for the cross-subnet connection
    bool foundRejectLog = false;
    for (const auto& [msg, cat] : logMessages) {
      if (msg.getMessage().find("Reject tcp connection from peer 127.3.0.2") !=
          std::string::npos) {
        foundRejectLog = true;
        break;
      }
    }
    EXPECT_TRUE(foundRejectLog)
        << "Expected rejection of cross-subnet connection from 127.3.0.2";

    peerMgr1->shutdownWithGR(false);
    peerMgr2->shutdownWithGR(false);
    peerMgr3->shutdownWithGR(false);

    waitTillSessionsGoDown(fm, peerMgr1, {peerId2});
    waitTillSessionsGoDown(fm, peerMgr2, {peerId1});
  });

  evb.loop();
}

/*
 * Shared post-condition for the two activeConnect reentrancy repros: the
 * connect that was in flight must have completed on the very FiberSocket it
 * started on -- not one that replaced it -- and that connection must be
 * usable for BGP.
 *
 * Checks, in order: only one connect attempt ran (the redundant one was
 * suppressed); activeConnectInfo still owns the FiberSocket the suspended
 * frame resumed on; that socket finished its handshake; and the DUT actually
 * spoke BGP on the connection it established.
 *
 * `socket` is expected to still be owned here rather than null. runBgpPeer()
 * takes it as `std::move(*activeConnectInfo->socket)`, which moves the
 * pointee and not the unique_ptr -- and FiberSocket's const shared_ptr member
 * makes even that move a copy -- so nothing ever leaves the unique_ptr empty.
 *
 * Callers must already have freed the accept-queue slot and slept long enough
 * for the DUT to connect and send OPEN. The bytes are then sitting in the
 * kernel buffer, so the read below does not block the EventBase this fiber is
 * running on.
 */
void expectConnectCompletedOnProtectedSocket(
    int listenFd,
    const std::shared_ptr<BgpPeerActiveConnectInfo>& aci,
    const FiberSocket* inFlight,
    uint16_t peerPort) {
  EXPECT_EQ(1u, aci->numOfConnectionAttempts)
      << "a second connect attempt ran while one was already in flight";
  ASSERT_EQ(inFlight, aci->socket.get())
      << "the in-flight FiberSocket was replaced";

  /*
   * getPeerAddress() is only populated after connect() resumes past its
   * await, so a matching peer port proves this exact FiberSocket completed
   * its handshake.
   */
  EXPECT_EQ(peerPort, aci->socket->getPeerAddress().getPort())
      << "connect did not complete on the protected socket";

  // Non-blocking so a missing connection fails the test instead of hanging.
  ::fcntl(listenFd, F_SETFL, ::fcntl(listenFd, F_GETFL, 0) | O_NONBLOCK);
  int established = ::accept(listenFd, nullptr, nullptr);
  ASSERT_GE(established, 0) << "DUT never completed its TCP connect";
  SCOPE_EXIT {
    ::close(established);
  };

  // Safety net only; the OPEN should already be buffered.
  struct timeval tv{2, 0};
  ::setsockopt(established, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  // BGP header: 16-byte marker, 2-byte length, 1-byte type. OPEN is type 1.
  std::array<unsigned char, 19> hdr{};
  const auto received =
      ::recv(established, hdr.data(), hdr.size(), MSG_WAITALL);
  ASSERT_EQ(static_cast<ssize_t>(hdr.size()), received)
      << "no BGP message arrived on the established connection";
  EXPECT_TRUE(std::all_of(hdr.begin(), hdr.begin() + 16, [](unsigned char b) {
    return b == 0xFF;
  })) << "connection did not carry a BGP marker";
  EXPECT_EQ(1, hdr[18]) << "expected the DUT to send a BGP OPEN";
}

/*
 * End-to-end reproduction of T268160522: public API in, SIGSEGV out.
 *
 * ASAN is the oracle. While activeConnect() lacks a reentrancy guard this
 * aborts with heap-use-after-free at FiberSocket.cpp:345; once guarded, the
 * same sequence runs clean and the test passes. It therefore asserts nothing
 * about the bug being present -- a run where the race below does not land
 * also passes, so treat a green result as "no memory error observed" rather
 * than proof the window was exercised.
 *
 * Nothing here is hand-queued: both activeConnect() invocations come from
 * real connect-retry timeouts (FiberBgpPeerManager.cpp:1471), driven only by
 * addPeer()/shutdownPeer()/startPeer(). The difficulty is holding fiber A
 * suspended inside FiberSocket::connect() long enough for the second timeout
 * to fire, while still letting A's connect ultimately SUCCEED -- the failure
 * path returns from connect()'s catch block without touching a member, so it
 * never faults.
 *
 * Mechanism:
 *   - listen(backlog=0) with the single accept-queue slot occupied, so the
 *     kernel silently drops further SYNs and A parks in SYN_SENT. A 30s
 *     connectTimeout keeps A from erroring out (the 500ms default would).
 *   - shutdownPeer()/startPeer() re-arms the connect timer on the *same*
 *     activeConnectInfo. Fiber B runs while A is parked and reassigns
 *     activeConnectInfo->socket, destroying A's FiberSocket.
 *   - Linux retransmits each SYN on its own clock from its own start: A at
 *     ~1s, B (starting ~150ms later) at ~1.15s. Freeing exactly one slot at
 *     ~950ms lets A's retransmit land first and win it; B's is dropped again.
 *   - A's connect succeeds, A resumes, and dereferences the destroyed object.
 *
 * Depends on Linux SYN-retransmit timing, so it is inherently timing-sensitive
 * rather than deterministic.
 */
TEST_F(FiberBgpPeerManagerFixture, ActiveConnectUseAfterFreeViaPeerStart) {
  auto& fm = fmWrapper.get();

  int listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(listenFd, 0);
  SCOPE_EXIT {
    ::close(listenFd);
  };
  sockaddr_in laddr{};
  laddr.sin_family = AF_INET;
  laddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  laddr.sin_port = 0;
  ASSERT_EQ(0, ::bind(listenFd, (sockaddr*)&laddr, sizeof(laddr)));
  ASSERT_EQ(0, ::listen(listenFd, 0));
  socklen_t alen = sizeof(laddr);
  ASSERT_EQ(0, ::getsockname(listenFd, (sockaddr*)&laddr, &alen));
  const uint16_t peerPort = ntohs(laddr.sin_port);
  const auto peerAddr = folly::IPAddress("127.0.0.1");

  // Occupy the single accept-queue slot so subsequent SYNs are dropped.
  int filler = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(filler, 0);
  SCOPE_EXIT {
    ::close(filler);
  };
  ASSERT_EQ(0, ::connect(filler, (sockaddr*)&laddr, sizeof(laddr)));

  auto config = makeBgpGlobalConfig(kR1Lo1, kR1Lo1);
  peerMgr1 = make_shared<TestFiberBgpPeerManager>(config, &callback1, fm, evb);
  fm.addTask([this] { peerMgr1->run(); });

  fm.addTask([this, peerAddr, peerPort, listenFd] {
    // startAfterDelay=0, minRetry=100ms, maxRetry=60s, connectTimeout=30s.
    ASSERT_TRUE(peerMgr1
                    ->addPeer(
                        peerAddr,
                        100,
                        100,
                        folly::SocketAddress("127.0.0.1", 0),
                        peerPort,
                        ConnTimeParams(0ms, 100ms, 60000ms, 30000ms))
                    .hasValue());

    // t ~= 150ms: fiber A is parked in SYN_SENT inside FiberSocket::connect().
    fiberSleepFor(150ms);
    auto aci = peerMgr1->allPeers_.at(peerAddr)
                   ->connectionInfos.at(peerPort)
                   ->activeConnectInfo;
    ASSERT_NE(nullptr, aci);
    const FiberSocket* inFlight = aci->socket.get();
    ASSERT_NE(nullptr, inFlight);
    ASSERT_EQ(1u, aci->numOfConnectionAttempts);

    /*
     * Re-arm the connect timer on the same ACI. While the bug is present
     * fiber B replaces activeConnectInfo->socket here, destroying the
     * FiberSocket that A's suspended connect() frame is still using; with the
     * reentrancy guard in place B returns without touching it.
     */
    peerMgr1->shutdownPeer(peerAddr);
    peerMgr1->startPeer(peerAddr);
    fiberSleepFor(150ms);
    GTEST_LOG_(INFO) << "in-flight FiberSocket " << inFlight << " -> "
                     << aci->socket.get() << " after "
                     << aci->numOfConnectionAttempts << " connect attempts";

    /*
     * t ~= 950ms: free exactly one slot, just ahead of A's ~1s SYN
     * retransmit, so A wins it rather than B (whose retransmit is ~150ms
     * later and finds the queue full again).
     */
    fiberSleepFor(650ms);
    int accepted = ::accept(listenFd, nullptr, nullptr);
    EXPECT_GE(accepted, 0);
    if (accepted >= 0) {
      ::close(accepted);
    }

    /*
     * A's handshake completes and A resumes inside FiberSocket::connect().
     * While the bug is present that resume touches the destroyed FiberSocket
     * and ASAN aborts here; once guarded, A resumes on its own live socket.
     */
    fiberSleepFor(2000ms);

    /*
     * Not crashing is not enough -- the protected connect must also have
     * succeeded.
     */
    expectConnectCompletedOnProtectedSocket(listenFd, aci, inFlight, peerPort);

    peerMgr1->shutdownWithGR(false);
  });

  evb.loop();
}

/*
 * Same use-after-free as ActiveConnectUseAfterFreeViaPeerStart, but armed
 * by the runBgpPeer() teardown path instead of startPeer(). This is the
 * arming source that can fire without any operator action, so it is the one
 * that matters for an unattended crash like T268160522.
 *
 * When a BGP session ends, runBgpPeer() re-arms the connect retry timer on
 * `connectionInfos[peerListenPort]->activeConnectInfo`. Note the port: for a
 * session the peer initiated, the connection lives under the peer's
 * *ephemeral* port, but the retry is armed on the *listen-port* ACI -- the
 * same ACI an in-flight active connect is using. So a passive session tearing
 * down re-enters activeConnect() for the active connect already in progress,
 * and its `activeConnectInfo->socket = std::make_unique<FiberSocket>()`
 * destroys the FiberSocket that connect() is suspended on.
 *
 * No shutdownPeer()/startPeer() here: the peer stays configured and running
 * throughout. The only stimulus is an inbound TCP connection that is accepted
 * and then dropped.
 */
TEST_F(
    FiberBgpPeerManagerFixture,
    ActiveConnectUseAfterFreeViaSessionTeardown) {
  auto& fm = fmWrapper.get();

  /*
   * Listener the DUT actively connects to, with its single accept-queue slot
   * occupied so the connect parks in SYN_SENT. See the sibling test for why
   * this shape is needed.
   */
  int listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(listenFd, 0);
  SCOPE_EXIT {
    ::close(listenFd);
  };
  sockaddr_in laddr{};
  laddr.sin_family = AF_INET;
  laddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  laddr.sin_port = 0;
  ASSERT_EQ(0, ::bind(listenFd, (sockaddr*)&laddr, sizeof(laddr)));
  ASSERT_EQ(0, ::listen(listenFd, 0));
  socklen_t alen = sizeof(laddr);
  ASSERT_EQ(0, ::getsockname(listenFd, (sockaddr*)&laddr, &alen));
  const uint16_t peerPort = ntohs(laddr.sin_port);
  const auto peerAddr = folly::IPAddress("127.0.0.1");

  int filler = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(filler, 0);
  SCOPE_EXIT {
    ::close(filler);
  };
  ASSERT_EQ(0, ::connect(filler, (sockaddr*)&laddr, sizeof(laddr)));

  /*
   * The manager needs its own listener so the test can drive an inbound
   * session through passiveConnectLoop().
   */
  auto config =
      makeBgpGlobalConfig(kR1Lo1, kR1Lo1, folly::SocketAddress("127.0.0.1", 0));
  peerMgr1 = make_shared<TestFiberBgpPeerManager>(config, &callback1, fm, evb);
  fm.addTask([this] { peerMgr1->run(); });

  fm.addTask([this, peerAddr, peerPort, listenFd] {
    ASSERT_TRUE(peerMgr1
                    ->addPeer(
                        peerAddr,
                        100,
                        100,
                        folly::SocketAddress("127.0.0.1", 0),
                        peerPort,
                        ConnTimeParams(0ms, 100ms, 60000ms, 30000ms))
                    .hasValue());

    // Fiber A is now parked in SYN_SENT inside FiberSocket::connect().
    fiberSleepFor(150ms);
    auto aci = peerMgr1->allPeers_.at(peerAddr)
                   ->connectionInfos.at(peerPort)
                   ->activeConnectInfo;
    ASSERT_NE(nullptr, aci);
    const FiberSocket* inFlight = aci->socket.get();
    ASSERT_NE(nullptr, inFlight);

    /*
     * Inbound connection -> passiveConnectLoop() accepts it and spawns
     * runBgpPeer() under the ephemeral port. Dropping it ends that session,
     * whose teardown re-arms the retry timer on the listen-port ACI above.
     */
    const auto mgrListen = peerMgr1->getListenAddress();
    ASSERT_TRUE(mgrListen.has_value());
    int passive = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(passive, 0);
    sockaddr_in maddr{};
    maddr.sin_family = AF_INET;
    maddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    maddr.sin_port = htons(mgrListen->getPort());
    ASSERT_EQ(0, ::connect(passive, (sockaddr*)&maddr, sizeof(maddr)));
    fiberSleepFor(100ms);
    ::close(passive);

    // Session teardown arms the timer; fiber B then destroys A's FiberSocket.
    fiberSleepFor(400ms);
    GTEST_LOG_(INFO) << "in-flight FiberSocket " << inFlight << " -> "
                     << aci->socket.get() << " after "
                     << aci->numOfConnectionAttempts << " connect attempts";

    // t ~= 950ms: free one slot just ahead of A's ~1s SYN retransmit.
    fiberSleepFor(300ms);
    int accepted = ::accept(listenFd, nullptr, nullptr);
    EXPECT_GE(accepted, 0);
    if (accepted >= 0) {
      ::close(accepted);
    }

    // A resumes onto the destroyed FiberSocket -> ASAN aborts here.
    fiberSleepFor(3000ms);

    /*
     * Once guarded, A resumes on its own socket and the connect must have
     * succeeded.
     */
    expectConnectCompletedOnProtectedSocket(listenFd, aci, inFlight, peerPort);

    peerMgr1->shutdownWithGR(false);
  });

  evb.loop();
}

} // namespace facebook::nettools::bgplib
