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

#include <algorithm>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#define AdjRib_TEST_FRIENDS                                                      \
  friend class AdjRibOutboundFixture;                                            \
  FRIEND_TEST(AdjRibOutboundFixture, VerifyAnnounceFiltering);                   \
  FRIEND_TEST(AdjRibOutboundFixture, AdvertiseLinkBandwidth);                    \
  FRIEND_TEST(                                                                   \
      AdjRibOutboundFixture, EgressPolicyLinkBandwidthPropagationEBGPTest);      \
  FRIEND_TEST(                                                                   \
      AdjRibOutboundFixture, EgressPolicyLinkBandwidthPropagationIBGPTest);      \
  FRIEND_TEST(AdjRibOutboundFixture, VerifyV4OverV6);                            \
  FRIEND_TEST(AdjRibOutboundFixture, VerifyAfiNegotiation);                      \
  FRIEND_TEST(                                                                   \
      AdjRibOutboundFixture, VerifyEgressEoRFlagsResetSessionEstablished);       \
  FRIEND_TEST(                                                                   \
      AdjRibOutboundFixture, VerifyEgressEoRsPendingSetDuringRibInitialDump);    \
  FRIEND_TEST(AdjRibOutboundFixture, SswFaduOutUndrainedTest);                   \
  FRIEND_TEST(AdjRibOutboundFixture, SswFaduOutDrainedTest);                     \
  FRIEND_TEST(AdjRibOutboundFixture, SswFswOutDrainedTest);                      \
  FRIEND_TEST(AdjRibOutboundFixture, UpdatePolicyProcessingIBgpPeer);            \
  FRIEND_TEST(AdjRibOutboundFixture, V4LocalRouteIBgpPeerAddPath);               \
  FRIEND_TEST(AdjRibOutboundFixture, V6LocalRouteIBgpPeerAddPath);               \
  FRIEND_TEST(EgressUcmpPolicyFixture, EgressUcmpPolicy);                        \
  FRIEND_TEST(AdjRibOutboundFixture, SenderSuppressAsLoop);                      \
  FRIEND_TEST(AdjRibOutboundFixture, SetRouteFilterStatementTest);               \
  FRIEND_TEST(AdjRibOutboundFixture, ProcessRibMessageTest);                     \
  FRIEND_TEST(AdjRibOutboundFixture, BuildAndSendBgpMessagesTest);               \
  FRIEND_TEST(AdjRibOutboundFixture, ProcessRibWithdrawTest);                    \
  FRIEND_TEST(AdjRibOutboundFixture, TryInsertWithdrawalTest);                   \
  FRIEND_TEST(AdjRibOutboundFixture, TryInsertRibOutEntryTest);                  \
  FRIEND_TEST(AdjRibOutboundFixture, GetPostOutPolicyAttributesTest);            \
  FRIEND_TEST(AdjRibOutboundFixture, GetPostOutPolicyAttributesAndInfoTest);     \
  FRIEND_TEST(                                                                   \
      AdjRibOutboundFixture, GetPostPolicyAttributesPolicyTermAndInfoTest);      \
  FRIEND_TEST(                                                                   \
      AdjRibOutboundFixture,                                                     \
      GetPostPolicyAttributesPolicyTermAndInfoTest_RejectedByInvalidGarWeights); \
  FRIEND_TEST(AdjRibOutboundFixture, OverridePrePolicyAttributesNegativeTest);   \
  FRIEND_TEST(AdjRibOutboundFixture, OverridePrePolicyAttributesPositiveTest);   \
  FRIEND_TEST(AdjRibOutboundFixture, PolicyCacheEvaluationTest);                 \
  FRIEND_TEST(AdjRibOutboundFixture, TryDeleteRibOutEntryTest);                  \
  FRIEND_TEST(AdjRibOutboundFixture, TryDeleteRibOutEntryTestAddPath);           \
  FRIEND_TEST(                                                                   \
      AdjRibOutboundFixture, GetPostPolicyOutAttrsAndPolicyFromMessageTest);     \
  FRIEND_TEST(AdjRibOutboundFixture, SuppressLoopedAdvertisementsTest);          \
  FRIEND_TEST(                                                                   \
      AdjRibOutboundFixture,                                                     \
      SuppressLoopedAdvertisementsUsesAcceptedRemoteAs);                         \
  FRIEND_TEST(AdjRibOutboundFixture, VerifyCowNhAttributeModification);          \
  FRIEND_TEST(                                                                   \
      UpdateExtCommunitiesCommonTest, CustomizedLbwEnabledNoModification);       \
  FRIEND_TEST(                                                                   \
      UpdateExtCommunitiesCommonTest, ReceiveAdvertiseLbwNoModification);        \
  FRIEND_TEST(UpdateExtCommunitiesCommonTest, EBgpPrunesNonTransitiveLbw);       \
  FRIEND_TEST(                                                                   \
      UpdateExtCommunitiesCommonTest,                                            \
      EBgpKeepsNonTransitiveLbwWithAdvertiseLinkBandwidth);                      \
  FRIEND_TEST(UpdateExtCommunitiesCommonTest, IBgpConfedKeepsAllLbwTypes);

#define AdjRibStats_TEST_FRIENDS      \
  friend class AdjRibOutboundFixture; \
  FRIEND_TEST(AdjRibOutboundFixture, VipInjectorPeerIdTest);

#include <folly/coro/BlockingWait.h>
#include <folly/logging/xlog.h>

#include "neteng/fboss/bgp/cpp/adjrib/AdjRibCommon.h"
#include "neteng/fboss/bgp/cpp/adjrib/facebook/RouteFilterLogger.h"
#include "neteng/fboss/bgp/cpp/tests/AdjRibOutUtils.h"
#include "neteng/fboss/bgp/cpp/tests/BoundedWaitUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibPolicyUtils.h"
#include "rfe/scubadata/ScubaData.h"

using namespace facebook::nettools::bgplib;
using namespace facebook::neteng::fboss::bgp_attr;
using namespace facebook::neteng::fboss::bgp::thrift;

using facebook::network::toBinaryAddress;
using facebook::network::toIPPrefix;
using folly::IPAddress;
using folly::IPAddressV4;

namespace facebook::bgp {

using nettools::bgplib::DeDuplicatedBgpPath;
using namespace ::testing;

TEST_F(AdjRibOutboundFixture, VipInjectorPeerIdTest) {
  BgpPeerId bgpPeerId;
  bgpPeerId.peerAddr = folly::IPAddress("169.254.0.1");
  bgpPeerId.remoteBgpId = folly::IPAddressV4("255.0.0.1").toLongHBO();
  setupAdjRib(bgpPeerId, 65000 /* VIP peer ASN */);
  EXPECT_EQ(bgpPeerId.toOdsKey(), adjRib_->getStats().getPeerIdOdsStr());
}

TEST_F(AdjRibOutboundFixture, VipInjectorAdditionalRemoteAsPeerIdTest) {
  BgpPeerId bgpPeerId;
  bgpPeerId.peerAddr = folly::IPAddress("169.254.0.1");
  bgpPeerId.remoteBgpId = folly::IPAddressV4("255.0.0.1").toLongHBO();

  PeeringParams peeringParams;
  peeringParams.peerAddr = bgpPeerId.peerAddr;
  peeringParams.remoteAs = kRemoteAs1;
  peeringParams.additionalRemoteAs = kVipAsn;
  adjRib_ = std::make_shared<AdjRib>(
      bgpPeerId,
      peeringParams,
      evb_,
      ribInQ_,
      observerQ_,
      std::make_shared<folly::coro::Baton>(),
      nullptr,
      std::make_shared<std::atomic<bool>>(false));

  EXPECT_EQ(bgpPeerId.toOdsKey(), adjRib_->getStats().getPeerIdOdsStr());
}

/*
 * Verify that a v4 local route is announced to a IBGP peer
 * Verify that a v4 local route is withdrawn
 */
TEST_F(AdjRibOutboundFixture, V4LocalRouteIBgpPeer) {
  // IBGP peer
  setupAdjRib();

  fm_->addTask([&] {
    auto ribMsg =
        createRibSingleAnnounce(kV4Prefix1, kV4Nexthop1, localPeerV4_, true);
    pushRibOutMsgToAdjRib(ribMsg);
    fiberSleepFor(50ms); // Sleep so we can verify both announce and withdraw
    ribMsg = createRibSingleWithdrawal(kV4Prefix1);
    pushRibOutMsgToAdjRib(ribMsg);
  });

  fm_->addTask([&] {
    /* Let sendBgpUpdates coro run if scheduled. */
    fiberSleepFor(10ms);
    auto msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(
        std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
    auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    /*
     * Verify that announcement message is notified to Fiber Bgp Peer
     * properly
     */
    ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
    EXPECT_EQ(
        toIPPrefix(kV4Prefix1),
        *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
    EXPECT_EQ(kLocalPref, bgpUpdate->attrs()->localPref());
    // originatorId is stored in network byte order
    EXPECT_EQ(htonl(kOriginatorId), *bgpUpdate->attrs()->originatorId());
    EXPECT_EQ(kV4Nexthop1.str(), *bgpUpdate->attrs()->nexthop());

    // Verify v4 and v6 EoRs
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    auto bgpEndOfRib = std::get<BgpEndOfRib>(*msg);
    EXPECT_EQ(false, *bgpEndOfRib.isMpEor());
    EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, *bgpEndOfRib.afi());
    EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *bgpEndOfRib.safi());

    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    bgpEndOfRib = std::get<BgpEndOfRib>(*msg);
    EXPECT_EQ(true, *bgpEndOfRib.isMpEor());
    EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *bgpEndOfRib.afi());
    EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *bgpEndOfRib.safi());

    // Verify RIB entry is created. Match various fields from input
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry);
    EXPECT_NE(nullptr, adjRibEntry->getPreOut());
    ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
    EXPECT_EQ(adjRibEntry->getPreOut(), adjRibEntry->getPostAttr());
    EXPECT_EQ(kLocalPref, adjRibEntry->getPostAttr()->getLocalPref());
    EXPECT_EQ(kOriginatorId, adjRibEntry->getPostAttr()->getOriginatorId());
    EXPECT_EQ(DeDuplicatedBgpPath::deduplicatorSize(), 1);

    // Verify AdjRibLiteTree size is non-zero
    EXPECT_EQ(
        1,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/false));
    // Verify AdjRibPathTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/true));

    // Verify stats
    EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCount());

    // Verify that withdrawal message is notified properly
    msg = folly::coro::blockingWait(popFromEgressQueue());
    bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);
    ASSERT_EQ(1, bgpUpdate->v4Withdrawn2()->size());
    EXPECT_EQ(toIPPrefix(kV4Prefix1), *bgpUpdate->v4Withdrawn2()[0].prefix());
    // Verify AdjRibLiteTree size is now zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/false));
    // Verify stats
    EXPECT_EQ(0, adjRib_->getStats().getPostOutPrefixCount());
    EXPECT_EQ(DeDuplicatedBgpPath::deduplicatorSize(), 1);

    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * Verify that a v4 local route is announced to a IBGP peer
 * Verify that a v4 local route is withdrawn
 */
TEST_F(AdjRibOutboundFixture, V4LocalRouteIBgpPeerAddPath) {
  // IBGP peer
  setupAdjRib(true);
  std::array<folly::fibers::Baton, 4> syncBaton;

  fm_->addTask([&] {
    facebook::bgp::test::boundedBatonWait(syncBaton[0], "syncBaton[0]");
    auto ribMsg = createRibSingleAnnounce(
        kV4Prefix1, kV4Nexthop1, localPeerV4_, true, true, kPlaceholderPathID);
    pushRibOutMsgToAdjRib(ribMsg);
    facebook::bgp::test::boundedBatonWait(syncBaton[1], "syncBaton[1]");
    ribMsg = createRibSingleAnnounce(
        kV4Prefix1, kV4Nexthop2, localPeerV4_, false, true, kPlaceholderPathID);
    pushRibOutMsgToAdjRib(ribMsg);
    facebook::bgp::test::boundedBatonWait(syncBaton[2], "syncBaton[2]");
    ribMsg = createRibSingleWithdrawalForAddPath(
        kV4Prefix1, kV4Nexthop1, kPlaceholderPathID);
    pushRibOutMsgToAdjRib(ribMsg);

    facebook::bgp::test::boundedBatonWait(syncBaton[3], "syncBaton[3]");
    ribMsg = createRibSingleWithdrawalForAddPath(
        kV4Prefix1, kV4Nexthop2, kPlaceholderPathID);
    pushRibOutMsgToAdjRib(ribMsg);
  });

  fm_->addTask([&] {
    /* Let sendBgpUpdates coro run if scheduled. */
    fiberSleepFor(10ms);
    syncBaton[0].post();
    auto msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(
        std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
    auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    /*
     * Verify that announcement message is notified to Fiber Bgp Peer
     * properly
     */
    ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
    EXPECT_EQ(
        toIPPrefix(kV4Prefix1),
        *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
    EXPECT_EQ(kLocalPref, *bgpUpdate->attrs()->localPref());
    // originatorId is stored in network byte order
    EXPECT_EQ(htonl(kOriginatorId), *bgpUpdate->attrs()->originatorId());
    EXPECT_EQ(kV4Nexthop1.str(), *bgpUpdate->attrs()->nexthop());

    // Verify v4 and v6 EoRs
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    auto bgpEndOfRib = std::get<BgpEndOfRib>(*msg);
    EXPECT_EQ(false, *bgpEndOfRib.isMpEor());
    EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, *bgpEndOfRib.afi());
    EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *bgpEndOfRib.safi());

    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    bgpEndOfRib = std::get<BgpEndOfRib>(*msg);
    EXPECT_EQ(true, *bgpEndOfRib.isMpEor());
    EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *bgpEndOfRib.afi());
    EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *bgpEndOfRib.safi());

    // Verify RIB entry is created. Match various fields from input
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1, 0);
    EXPECT_NE(nullptr, adjRibEntry);
    EXPECT_NE(nullptr, adjRibEntry->getPreOut());
    ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
    EXPECT_EQ(adjRibEntry->getPreOut(), adjRibEntry->getPostAttr());
    EXPECT_EQ(kLocalPref, adjRibEntry->getPostAttr()->getLocalPref());
    EXPECT_EQ(kOriginatorId, adjRibEntry->getPostAttr()->getOriginatorId());

    // Verify stats
    EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCount());

    syncBaton[1].post();

    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(
        std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
    bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    /*
     * Verify that announcement message is notified to Fiber Bgp Peer
     * properly
     */
    ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
    EXPECT_EQ(
        toIPPrefix(kV4Prefix1),
        *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
    EXPECT_EQ(kLocalPref, *bgpUpdate->attrs()->localPref());
    // originatorId is stored in network byte order
    EXPECT_EQ(htonl(kOriginatorId), *bgpUpdate->attrs()->originatorId());
    EXPECT_EQ(kV4Nexthop2.str(), *bgpUpdate->attrs()->nexthop());

    // Verify RIB entry is created. Match various fields from input
    adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1, 1);
    EXPECT_NE(nullptr, adjRibEntry);
    EXPECT_NE(nullptr, adjRibEntry->getPreOut());
    ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
    EXPECT_EQ(adjRibEntry->getPreOut(), adjRibEntry->getPostAttr());
    EXPECT_EQ(kLocalPref, adjRibEntry->getPostAttr()->getLocalPref());
    EXPECT_EQ(kOriginatorId, adjRibEntry->getPostAttr()->getOriginatorId());

    // Verify AdjRibLiteTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/false));
    // Verify AdjRibPathTree size is non-zero
    EXPECT_EQ(
        1,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/true));
    // Verify path entries for this adjRib is 2
    EXPECT_EQ(
        2,
        adjRib_->getRibTreePeerEntriesCount(
            false, /* ingress */
            true /* isAddPathEnabled */));

    // Verify stats
    EXPECT_EQ(2, adjRib_->getStats().getPostOutPrefixCount());
    /*
     * Rib announcement itself is with different next-hops and so expect
     * different copy of attributes in the set
     */
    EXPECT_EQ(DeDuplicatedBgpPath::deduplicatorSize(), 2);

    syncBaton[2].post();
    // Verify that withdrawal message is notified properly
    msg = folly::coro::blockingWait(popFromEgressQueue());
    bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1, 0);
    EXPECT_EQ(nullptr, adjRibEntry);
    ASSERT_EQ(1, bgpUpdate->v4Withdrawn2()->size());
    EXPECT_EQ(toIPPrefix(kV4Prefix1), *bgpUpdate->v4Withdrawn2()[0].prefix());
    EXPECT_EQ(0, *bgpUpdate->v4Withdrawn2()[0].pathId());
    EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCount());
    EXPECT_EQ(DeDuplicatedBgpPath::deduplicatorSize(), 2);
    // Verify path entries for this adjRib is now 1
    EXPECT_EQ(
        1,
        adjRib_->getRibTreePeerEntriesCount(
            false, /* ingress */
            true /* isAddPathEnabled */));

    syncBaton[3].post();
    // Verify that withdrawal message is notified properly
    msg = folly::coro::blockingWait(popFromEgressQueue());
    bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1, 1);
    EXPECT_EQ(nullptr, adjRibEntry);
    ASSERT_EQ(1, bgpUpdate->v4Withdrawn2()->size());
    EXPECT_EQ(toIPPrefix(kV4Prefix1), *bgpUpdate->v4Withdrawn2()[0].prefix());
    EXPECT_EQ(1, *bgpUpdate->v4Withdrawn2()[0].pathId());
    // Verify AdjRibPathTree size is now zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/true));
    // Verify path entries for this adjRib is 0
    EXPECT_EQ(
        0,
        adjRib_->getRibTreePeerEntriesCount(
            false, /* ingress */
            true /* isAddPathEnabled */));

    // Verify stats
    EXPECT_EQ(0, adjRib_->getStats().getPostOutPrefixCount());
    EXPECT_EQ(DeDuplicatedBgpPath::deduplicatorSize(), 2);

    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * Verify that a v6 local route is announced to a IBGP peer
 * Verify that a v6 local route is withdrawn
 * We are advertising ipv6 routes on ipv4 peering
 */
TEST_F(AdjRibOutboundFixture, V6LocalRouteIBgpPeer) {
  // IBGP peer
  setupAdjRib();

  fm_->addTask([&] {
    auto ribMsg = createRibSingleAnnounce(
        kV6Prefix1,
        kV6Nexthop1,
        TinyPeerInfo(
            kLocalV6RoutePeerAddr,
            kLocalRouteAs,
            0,
            BgpSessionType::IBGP,
            false),
        true);
    pushRibOutMsgToAdjRib(ribMsg);
    fiberSleepFor(50ms); // Sleep so we can verify both announce and withdraw
    ribMsg = createRibSingleWithdrawal(kV6Prefix1);
    pushRibOutMsgToAdjRib(ribMsg);
  });

  fm_->addTask([&] {
    /* Let sendBgpUpdates coro run if scheduled. */
    fiberSleepFor(10ms);
    auto msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(
        std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
    auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    // Verify that message is notified to Fiber Bgp Peer properly
    EXPECT_EQ(kLocalPref, bgpUpdate->attrs()->localPref());
    // originatorId is stored in network byte order
    EXPECT_EQ(htonl(kOriginatorId), *bgpUpdate->attrs()->originatorId());
    ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
    EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *bgpUpdate->mpAnnounced()->afi());
    EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *bgpUpdate->mpAnnounced()->safi());
    EXPECT_EQ(
        toBinaryAddress(kV6Nexthop1), *bgpUpdate->mpAnnounced()->nexthop());
    EXPECT_EQ(
        toIPPrefix(kV6Prefix1),
        *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());

    // Verify v4 and v6 EoRs
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    auto bgpEndOfRib = std::get<BgpEndOfRib>(*msg);
    EXPECT_EQ(false, *bgpEndOfRib.isMpEor());
    EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, *bgpEndOfRib.afi());
    EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *bgpEndOfRib.safi());

    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    bgpEndOfRib = std::get<BgpEndOfRib>(*msg);
    EXPECT_EQ(true, *bgpEndOfRib.isMpEor());
    EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *bgpEndOfRib.afi());
    EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *bgpEndOfRib.safi());

    // Verify RIB entry is created. Match various fields from input
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV6Prefix1);
    EXPECT_NE(nullptr, adjRibEntry);
    EXPECT_NE(nullptr, adjRibEntry->getPreOut());
    ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
    EXPECT_EQ(adjRibEntry->getPreOut(), adjRibEntry->getPostAttr());
    EXPECT_EQ(kLocalPref, adjRibEntry->getPostAttr()->getLocalPref());
    EXPECT_EQ(kOriginatorId, adjRibEntry->getPostAttr()->getOriginatorId());

    // verify stats
    EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCount());

    // Verify that withdrawal message is notified properly
    msg = folly::coro::blockingWait(popFromEgressQueue());
    bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV6Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);
    ASSERT_EQ(1, bgpUpdate->mpWithdrawn()->prefixes()->size());
    EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *bgpUpdate->mpWithdrawn()->afi());
    EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *bgpUpdate->mpWithdrawn()->safi());
    EXPECT_EQ(
        toIPPrefix(kV6Prefix1),
        *bgpUpdate->mpWithdrawn()->prefixes()[0].prefix());
    EXPECT_EQ(0, adjRib_->getStats().getPostOutPrefixCount());

    terminateAdjRib();
  });
  evb_.loop();
}

TEST_F(AdjRibOutboundFixture, V6LocalRouteIBgpPeerAddPath) {
  // IBGP peer
  setupAdjRib(true);
  std::array<folly::fibers::Baton, 4> syncBaton;

  fm_->addTask([&] {
    facebook::bgp::test::boundedBatonWait(syncBaton[0], "syncBaton[0]");
    auto ribMsg = createRibSingleAnnounce(
        kV6Prefix1,
        kV6Nexthop1,
        TinyPeerInfo(
            kLocalV6RoutePeerAddr,
            kLocalRouteAs,
            0,
            BgpSessionType::IBGP,
            false),
        true,
        true,
        kPlaceholderPathID);
    pushRibOutMsgToAdjRib(ribMsg);

    facebook::bgp::test::boundedBatonWait(syncBaton[1], "syncBaton[1]");
    ribMsg = createRibSingleAnnounce(
        kV6Prefix1,
        kV6Nexthop2,
        TinyPeerInfo(
            kLocalV6RoutePeerAddr,
            kLocalRouteAs,
            0,
            BgpSessionType::IBGP,
            false),
        false,
        true,
        kPlaceholderPathID);
    pushRibOutMsgToAdjRib(ribMsg);

    facebook::bgp::test::boundedBatonWait(syncBaton[2], "syncBaton[2]");
    ribMsg = createRibSingleWithdrawalForAddPath(
        kV6Prefix1, kV6Nexthop1, kPlaceholderPathID);
    pushRibOutMsgToAdjRib(ribMsg);

    facebook::bgp::test::boundedBatonWait(syncBaton[3], "syncBaton[3]");
    ribMsg = createRibSingleWithdrawalForAddPath(
        kV6Prefix1, kV6Nexthop2, kPlaceholderPathID);
    pushRibOutMsgToAdjRib(ribMsg);
  });

  fm_->addTask([&] {
    syncBaton[0].post();
    auto msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(
        std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
    auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    // Verify that message is notified to Fiber Bgp Peer properly
    EXPECT_EQ(kLocalPref, *bgpUpdate->attrs()->localPref());
    // originatorId is stored in network byte order
    EXPECT_EQ(htonl(kOriginatorId), *bgpUpdate->attrs()->originatorId());
    ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
    EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *bgpUpdate->mpAnnounced()->afi());
    EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *bgpUpdate->mpAnnounced()->safi());
    EXPECT_EQ(
        toBinaryAddress(kV6Nexthop1), *bgpUpdate->mpAnnounced()->nexthop());
    EXPECT_EQ(
        toIPPrefix(kV6Prefix1),
        *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
    EXPECT_EQ(0, *bgpUpdate->mpAnnounced()->prefixes()[0].pathId());

    // Verify v4 and v6 EoRs
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    auto bgpEndOfRib = std::get<BgpEndOfRib>(*msg);
    EXPECT_EQ(false, *bgpEndOfRib.isMpEor());
    EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, *bgpEndOfRib.afi());
    EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *bgpEndOfRib.safi());

    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    bgpEndOfRib = std::get<BgpEndOfRib>(*msg);
    EXPECT_EQ(true, *bgpEndOfRib.isMpEor());
    EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *bgpEndOfRib.afi());
    EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *bgpEndOfRib.safi());

    // Verify RIB entry is created. Match various fields from input
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV6Prefix1, 0);
    EXPECT_NE(nullptr, adjRibEntry);
    EXPECT_NE(nullptr, adjRibEntry->getPreOut());
    ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
    EXPECT_EQ(adjRibEntry->getPreOut(), adjRibEntry->getPostAttr());
    EXPECT_EQ(kLocalPref, adjRibEntry->getPostAttr()->getLocalPref());
    EXPECT_EQ(kOriginatorId, adjRibEntry->getPostAttr()->getOriginatorId());

    // verify stats
    EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCount());

    syncBaton[1].post();
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(
        std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
    bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    // Verify that message is notified to Fiber Bgp Peer properly
    EXPECT_EQ(kLocalPref, *bgpUpdate->attrs()->localPref());
    // originatorId is stored in network byte order
    EXPECT_EQ(htonl(kOriginatorId), *bgpUpdate->attrs()->originatorId());
    ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
    EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *bgpUpdate->mpAnnounced()->afi());
    EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *bgpUpdate->mpAnnounced()->safi());
    EXPECT_EQ(
        toBinaryAddress(kV6Nexthop2), *bgpUpdate->mpAnnounced()->nexthop());
    EXPECT_EQ(
        toIPPrefix(kV6Prefix1),
        *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
    EXPECT_EQ(1, *bgpUpdate->mpAnnounced()->prefixes()[0].pathId());

    // Verify RIB entry is created. Match various fields from input
    adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV6Prefix1, 1);
    EXPECT_NE(nullptr, adjRibEntry);
    EXPECT_NE(nullptr, adjRibEntry->getPreOut());
    ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
    EXPECT_EQ(adjRibEntry->getPreOut(), adjRibEntry->getPostAttr());
    EXPECT_EQ(kLocalPref, adjRibEntry->getPostAttr()->getLocalPref());
    EXPECT_EQ(kOriginatorId, adjRibEntry->getPostAttr()->getOriginatorId());

    // verify stats
    EXPECT_EQ(2, adjRib_->getStats().getPostOutPrefixCount());

    syncBaton[2].post();
    // Verify that withdrawal message is notified properly
    msg = folly::coro::blockingWait(popFromEgressQueue());
    bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV6Prefix1, 0);
    EXPECT_EQ(nullptr, adjRibEntry);
    ASSERT_EQ(1, bgpUpdate->mpWithdrawn()->prefixes()->size());
    EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *bgpUpdate->mpWithdrawn()->afi());
    EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *bgpUpdate->mpWithdrawn()->safi());
    EXPECT_EQ(
        toIPPrefix(kV6Prefix1),
        *bgpUpdate->mpWithdrawn()->prefixes()[0].prefix());
    EXPECT_EQ(0, *bgpUpdate->mpWithdrawn()->prefixes()[0].pathId());
    EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCount());

    syncBaton[3].post();
    // Verify that withdrawal message is notified properly
    msg = folly::coro::blockingWait(popFromEgressQueue());
    bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV6Prefix1, 1);
    EXPECT_EQ(nullptr, adjRibEntry);
    ASSERT_EQ(1, bgpUpdate->mpWithdrawn()->prefixes()->size());
    EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *bgpUpdate->mpWithdrawn()->afi());
    EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *bgpUpdate->mpWithdrawn()->safi());
    EXPECT_EQ(
        toIPPrefix(kV6Prefix1),
        *bgpUpdate->mpWithdrawn()->prefixes()[0].prefix());
    EXPECT_EQ(1, *bgpUpdate->mpWithdrawn()->prefixes()[0].pathId());
    EXPECT_EQ(0, adjRib_->getStats().getPostOutPrefixCount());

    terminateAdjRib();
  });
  evb_.loop();
}

// Verify preOut and postOut are cleared when session goes down
TEST_F(AdjRibOutboundFixture, SessionGoingDown) {
  setupAdjRib();

  fm_->addTask([&] {
    auto ribMsg =
        createRibSingleAnnounce(kV4Prefix1, kV4Nexthop1, localPeerV4_, true);
    pushRibOutMsgToAdjRib(ribMsg);
    fiberSleepFor(50ms); // Sleep so we can verify both announce and withdraw
  });

  fm_->addTask([&] {
    /* Let sendBgpUpdates coro run if scheduled. */
    fiberSleepFor(10ms);
    auto msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(
        std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
    auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);

    // Verify v4 and v6 EoRs
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));

    // Verify RIB entry is created. Match various fields from input
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry);
    ASSERT_NE(nullptr, adjRibEntry->getPreOut());
    EXPECT_NE(nullptr, adjRibEntry->getPostAttr());
    EXPECT_EQ(adjRibEntry->getPreOut(), adjRibEntry->getPostAttr());
    EXPECT_EQ(kLocalPref, adjRibEntry->getPreOut()->getLocalPref());
    EXPECT_EQ(kOriginatorId, adjRibEntry->getPreOut()->getOriginatorId());

    // Verify stats
    EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCount());

    terminateAdjRib();
    fiberSleepFor(50ms);

    // verify routes are cleared
    adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/true));

    // Verify that postOut clearing reset the stats
    EXPECT_EQ(0, adjRib_->getStats().getPostOutPrefixCount());
  });
  evb_.loop();
}

/*
 *  1. Setup AdjRib with send add path capability
 *  2. create single announcement with EoR
 *  3. Verify egress adjrib entry created with relevant params,
 *     including preOut and postOut attributes
 *  4. Verify size of the AdjRib Tree with path enabled
 *  5. Bring the bgp session down
 *  6. adjrib entry must be cleared. Verify
 *  7. Also, verify that adjRib Tree is cleaned up and has 0 size
 */
TEST_F(AdjRibOutboundFixture, SessionGoingDownAddPath) {
  setupAdjRib(true);

  fm_->addTask([&] {
    auto ribMsg = createRibSingleAnnounce(
        kV4Prefix1, kV4Nexthop1, localPeerV4_, true, true, kPlaceholderPathID);
    pushRibOutMsgToAdjRib(ribMsg);
    fiberSleepFor(50ms);
  });

  fm_->addTask([&] {
    /* Let sendBgpUpdates coro run if scheduled. */
    fiberSleepFor(10ms);
    auto msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(
        std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
    auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);

    // Verify v4 and v6 EoRs
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));

    // Verify RIB entry is created. Match various fields from input
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry);
    ASSERT_NE(nullptr, adjRibEntry->getPreOut());
    EXPECT_NE(nullptr, adjRibEntry->getPostAttr());
    EXPECT_EQ(adjRibEntry->getPreOut(), adjRibEntry->getPostAttr());
    EXPECT_EQ(kLocalPref, adjRibEntry->getPreOut()->getLocalPref());
    EXPECT_EQ(kOriginatorId, adjRibEntry->getPreOut()->getOriginatorId());

    // Verify AdjRibLiteTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/false));
    // Verify AdjRibTree size is non-zero
    EXPECT_EQ(
        1,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/true));

    // Verify stats
    EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCount());

    terminateAdjRib();
    fiberSleepFor(50ms);

    // verify routes are cleared
    adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);

    // Verify AdjRibTree size is now zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/true));

    // Verify that postOut clearing reset the stats
    EXPECT_EQ(0, adjRib_->getStats().getPostOutPrefixCount());
  });
  evb_.loop();
}

// Verify grouping of prefixes based on attributes
TEST_F(AdjRibOutboundFixture, groupingPrefixesbyAttributes) {
  setupAdjRib();

  fm_->addTask([&] {
    RibOutAnnouncement ribMsg;
    BgpUpdate2 update = buildBgpUpdateAttributes(kV4Nexthop1);
    // Creating two shared_ptr<BgpPath> with same content
    auto attrs1 = std::make_shared<facebook::bgp::BgpPath>(
        BgpPathFields(*BgpUpdate2toBgpPathC(update)));
    auto attrs2 = std::make_shared<facebook::bgp::BgpPath>(
        BgpPathFields(*BgpUpdate2toBgpPathC(update)));

    ribMsg.entries.emplace_back(
        kV4Prefix1, kDefaultPathID, localPeerV4_, attrs1);
    ribMsg.entries.emplace_back(
        kV4Prefix2, kDefaultPathID, localPeerV4_, attrs2);
    ribMsg.initialDump = true;
    ribMsg.sendWithEoR = true;
    pushRibOutMsgToAdjRib(ribMsg);
  });

  fm_->addTask([&] {
    /* Let sendBgpUpdates coro run if scheduled. */
    fiberSleepFor(10ms);
    auto msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(
        std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
    auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);

    // Verify that only one BgpUpdate2 with both prefixes is generated
    std::set<network::thrift::IPPrefix> expectedPfxs;
    expectedPfxs.insert(toIPPrefix(kV4Prefix1));
    expectedPfxs.insert(toIPPrefix(kV4Prefix2));

    ASSERT_EQ(2, bgpUpdate->mpAnnounced()->prefixes()->size());
    std::set<network::thrift::IPPrefix> observedPfxs;
    observedPfxs.insert(*bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
    observedPfxs.insert(*bgpUpdate->mpAnnounced()->prefixes()[1].prefix());

    EXPECT_EQ(observedPfxs, expectedPfxs);

    EXPECT_EQ(kLocalPref, bgpUpdate->attrs()->localPref());
    // originatorId is stored in network byte order
    EXPECT_EQ(htonl(kOriginatorId), *bgpUpdate->attrs()->originatorId());
    EXPECT_EQ(kV4Nexthop1.str(), *bgpUpdate->attrs()->nexthop());

    // Verify v4 and v6 EoRs
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));

    // Verify that both rib entries are created
    auto adjRibEntry1 = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix2);
    ASSERT_NE(nullptr, adjRibEntry1);
    auto adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1);
    ASSERT_NE(nullptr, adjRibEntry2);
    // Verify that both entry share attributes
    EXPECT_EQ(adjRibEntry1->getPostAttr(), adjRibEntry2->getPostAttr());
    EXPECT_EQ(
        *adjRibEntry1->getPostAttr()->getFields(),
        *adjRibEntry2->getPostAttr()->getFields());
    EXPECT_EQ(DeDuplicatedBgpPath::deduplicatorSize(), 1);

    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * Verify that using COW attributes are modified properly
 * Verify RR client = True behavior (we announce)
 */
TEST_F(AdjRibOutboundFixture, VerifyCowNhAttributeModification) {
  setupAdjRib(
      kLocalAs1,
      kLocalAs1,
      kRemoteAs1,
      kIsRrClientTrue,
      kIsConfedPeerFalse,
      NextHopSelfConfigured(true),
      kV4Nexthop2,
      kV6Nexthop1);

  BgpUpdate2 update = buildBgpUpdateAttributes(kV4Nexthop1);
  // Creating two shared_ptr<BgpPath> with same content
  auto attrs = std::make_shared<facebook::bgp::BgpPath>(
      BgpPathFields(*BgpUpdate2toBgpPathC(update)));

  fm_->addTask([&] {
    RibOutAnnouncement ribMsg;
    ribMsg.initialDump = true;
    ribMsg.sendWithEoR = true;

    // learnt this route from non-RRC IBGP peer
    ribMsg.entries.emplace_back(kV4Prefix1, kDefaultPathID, iBgpPeer_, attrs);
    pushRibOutMsgToAdjRib(ribMsg);
  });

  fm_->addTask([&] {
    /* Let sendBgpUpdates coro run if scheduled. */
    fiberSleepFor(10ms);
    auto msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(
        std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
    auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    // See that next hop is modified as per config
    EXPECT_EQ(kV4Nexthop2.str(), *bgpUpdate->attrs()->nexthop());

    // Verify v4 and v6 EoRs
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));

    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry);
    // Verify that postOut shallow compare differs from what RIB passed
    EXPECT_EQ(attrs, adjRibEntry->getPreOut());
    EXPECT_NE(attrs, adjRibEntry->getPostAttr());
    EXPECT_TRUE(adjRibEntry->getPostAttr()->isPublished());
    EXPECT_EQ(
        kV4Nexthop2,
        adjRib_->getNewNexthopFromAttributesOut(
            kV4Prefix1.first.isV4(), adjRibEntry->getPostAttr()));

    terminateAdjRib();
  });
  evb_.loop();
}

// Verify AdjRib Egress Filtering Fiber pipeline workflow
TEST_F(AdjRibOutboundFixture, VerifyEgressFilteringFiber) {
  // IBGP peer
  setupAdjRib();

  // We do not announce non RR iBGP learnt routes to other iBGP peers
  fm_->addTask([&] {
    RibOutAnnouncement ribMsg;
    BgpUpdate2 update = buildBgpUpdateAttributes(kV4Nexthop1);
    // Creating two shared_ptr<BgpPath> with same content
    auto attrs = std::make_shared<facebook::bgp::BgpPath>(
        BgpPathFields(*BgpUpdate2toBgpPathC(update)));

    // learnt this route from non-RRC IBGP peer
    ribMsg.entries.emplace_back(kV4Prefix1, kDefaultPathID, iBgpPeer_, attrs);
    ribMsg.sendWithEoR = false;
    pushRibOutMsgToAdjRib(ribMsg);
  });

  fm_->addTask([&] {
    /*
     * As we are checking for queue to be empty, sleeping for a while
     * to ensure processing is completed
     */
    fiberSleepFor(50ms);
    EXPECT_TRUE(boundedAdjRibOutQ_->empty());
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);
    EXPECT_EQ(0, adjRib_->getStats().getPostOutPrefixCount());

    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * Test sender suppress as loop feature: do not send to peer if AS-path
 * contains neighbor ASN
 */
TEST_F(AdjRibOutboundFixture, SenderSuppressAsLoop) {
  // Setup up paths that contains remote As and one that does not
  std::array<uint32_t, 2> seg1Arr = {kRemoteAs1, 65000};
  std::array<uint32_t, 3> seg2Arr = {65000, kRemoteAs1, 65000};
  std::array<uint32_t, 3> seg3Arr = {65000, 65000, kRemoteAs1};
  std::array<uint32_t, 1> seg4Arr = {65000};

  // Create asSequence segments from the input arrays
  BgpAttrAsPathSegment asSeq1;
  BgpAttrAsPathSegment asSeq2;
  BgpAttrAsPathSegment asSeq3;
  BgpAttrAsPathSegment asSeqClean;

  asSeq1.asSequence()->assign(seg1Arr.begin(), seg1Arr.end());
  asSeq2.asSequence()->assign(seg2Arr.begin(), seg2Arr.end());
  asSeq3.asSequence()->assign(seg3Arr.begin(), seg3Arr.end());
  asSeqClean.asSequence()->assign(seg4Arr.begin(), seg4Arr.end());

  BgpAttrAsPathSegment asConfedSeq1;
  BgpAttrAsPathSegment asConfedSeq2;
  BgpAttrAsPathSegment asConfedSeq3;
  BgpAttrAsPathSegment asConfedSeqClean;

  asConfedSeq1.asConfedSequence()->assign(seg1Arr.begin(), seg1Arr.end());
  asConfedSeq2.asConfedSequence()->assign(seg2Arr.begin(), seg2Arr.end());
  asConfedSeq3.asConfedSequence()->assign(seg3Arr.begin(), seg3Arr.end());
  asConfedSeqClean.asConfedSequence()->assign(seg4Arr.begin(), seg4Arr.end());

  // Test EBGP
  setupAdjRib(
      kLocalAs2,
      kLocalAs2,
      kRemoteAs1,
      kIsRrClientFalse,
      kIsConfedPeerFalse,
      NextHopSelfConfigured(false),
      kV4Nexthop1,
      kV6Nexthop1,
      false // sessionEstablish
  );
  auto ebgpPeerTests = {asSeq1, asSeq2, asSeq3};
  for (const auto& asSeq : ebgpPeerTests) {
    std::vector<BgpAttrAsPathSegment> asPath;
    asPath.push_back(asConfedSeqClean);
    asPath.push_back(asSeq);

    BgpUpdate2 update = buildBgpUpdateAttributesAsPath(kV4Nexthop2, asPath);
    auto attrs = std::make_shared<facebook::bgp::BgpPath>(
        BgpPathFields(*BgpUpdate2toBgpPathC(update)));

    ASSERT_TRUE(adjRib_->suppressLoopedAdvertisements(attrs));
  }

  // Confed Ebgp
  setupAdjRib(
      kLocalAs2,
      kLocalAs2,
      kRemoteAs1,
      kIsRrClientFalse,
      kIsConfedPeerTrue,
      NextHopSelfConfigured(false),
      kV4Nexthop1,
      kV6Nexthop1,
      false // sessionEstablish
  );
  auto confedEbgpPeerTests = {asConfedSeq1, asConfedSeq2, asConfedSeq3};
  for (const auto& asConfedSeq : confedEbgpPeerTests) {
    std::vector<BgpAttrAsPathSegment> asPath;
    asPath.push_back(asSeqClean);
    asPath.push_back(asConfedSeq);

    BgpUpdate2 update = buildBgpUpdateAttributesAsPath(kV4Nexthop2, asPath);
    auto attrs = std::make_shared<facebook::bgp::BgpPath>(
        BgpPathFields(*BgpUpdate2toBgpPathC(update)));

    ASSERT_TRUE(adjRib_->suppressLoopedAdvertisements(attrs));
  }

  // Remote AS is not in asPath
  {
    std::vector<BgpAttrAsPathSegment> asPath;
    asPath.push_back(asSeqClean);
    asPath.push_back(asConfedSeqClean);

    BgpUpdate2 update = buildBgpUpdateAttributesAsPath(kV4Nexthop2, asPath);
    auto attrs = std::make_shared<facebook::bgp::BgpPath>(
        BgpPathFields(*BgpUpdate2toBgpPathC(update)));
    ASSERT_FALSE(adjRib_->suppressLoopedAdvertisements(attrs));
  }
}

/*
 * Verify various cases when we can announce a prefix
 * Testing canAnnounce function
 */
TEST_F(AdjRibOutboundFixture, VerifyAnnounceFiltering) {
  BgpUpdate2 update = buildBgpUpdateAttributes(kV4Nexthop1);
  auto attrs = std::make_shared<facebook::bgp::BgpPath>(
      BgpPathFields(*BgpUpdate2toBgpPathC(update)));

  /*
   * NOTE: setupAdjRib set's peer address as kPeerAddr1
   * kPeerAddr1 ("1.1.1.1"), kLocalAddr1("10.50.139.10")
   * kLocalAs1(1), kRemoteAs1(1), kRemoteAs2(2), kLocalRouteAs(0)
   * kLocalV4RoutePeerAddr("0.0.0.0"), kLocalV6RoutePeerAddr("::")
   */

  // iBGP non RRC Peer
  {
    setupAdjRib(
        kLocalAs1,
        kLocalAs1,
        kRemoteAs1,
        kIsRrClientFalse,
        kIsConfedPeerFalse,
        NextHopSelfConfigured(false),
        kV4Nexthop1,
        kV6Nexthop1,
        false // sessionEstablish
    );

    // Verify not to advertise to the peer from which we learnt this path
    auto samePeerRibEntry = RibOutAnnouncementEntry(
        kV4Prefix1,
        kDefaultPathID,
        TinyPeerInfo(
            kPeerAddr1, kLocalAs1, kPeerRouterId1, BgpSessionType::IBGP, false),
        attrs);
    EXPECT_FALSE(adjRib_->canAnnounce(samePeerRibEntry));

    // Verify local routes are announced to IBGP peer which is not RR client
    auto localRouteRibEntry = RibOutAnnouncementEntry(
        kV4Prefix1, kDefaultPathID, localPeerV4_, attrs);
    EXPECT_TRUE(adjRib_->canAnnounce(localRouteRibEntry));

    /*
     * Verify route learnt from non RRC iBGP peer is not advertised to
     * non RR iBgp peer
     */
    auto nonRrClientRibEntry =
        RibOutAnnouncementEntry(kV4Prefix1, kDefaultPathID, iBgpPeer_, attrs);
    EXPECT_FALSE(adjRib_->canAnnounce(nonRrClientRibEntry));

    // Verify route learnt from RR client is advertised to non RR iBgp peer
    auto rrClientRibEntry =
        RibOutAnnouncementEntry(kV4Prefix1, kDefaultPathID, rrcPeer_, attrs);
    EXPECT_TRUE(adjRib_->canAnnounce(rrClientRibEntry));
  }
  // RRC
  {
    setupAdjRib(
        kLocalAs1,
        kLocalAs1,
        kRemoteAs1,
        kIsRrClientTrue,
        kIsConfedPeerFalse,
        NextHopSelfConfigured(false),
        kV4Nexthop1,
        kV6Nexthop1,
        false);

    // Verify not to advertise to the peer from which we learnt this path
    auto samePeerRibEntry = RibOutAnnouncementEntry(
        kV4Prefix1,
        kDefaultPathID,
        TinyPeerInfo(
            kPeerAddr1, kLocalAs1, kPeerRouterId1, BgpSessionType::IBGP, true),
        attrs);
    EXPECT_FALSE(adjRib_->canAnnounce(samePeerRibEntry));

    // Verify local routes are announced to IBGP peer which is RR client
    auto localRouteRibEntry = RibOutAnnouncementEntry(
        kV4Prefix1, kDefaultPathID, localPeerV4_, attrs);
    EXPECT_TRUE(adjRib_->canAnnounce(localRouteRibEntry));

    // Verify route learnt from non RRC iBGP peer is advertised to RR client
    auto nonRrClientRibEntry =
        RibOutAnnouncementEntry(kV4Prefix1, kDefaultPathID, iBgpPeer_, attrs);
    EXPECT_TRUE(adjRib_->canAnnounce(nonRrClientRibEntry));

    // Verify route learnt from RR client is advertised to RR client
    auto rrClientRibEntry =
        RibOutAnnouncementEntry(kV4Prefix1, kDefaultPathID, rrcPeer_, attrs);
    EXPECT_TRUE(adjRib_->canAnnounce(rrClientRibEntry));
  }
  // eBGP peer
  {
    setupAdjRib(
        kLocalAs1,
        kLocalAs1,
        kRemoteAs2,
        kIsRrClientFalse,
        kIsConfedPeerFalse,
        NextHopSelfConfigured(false),
        kV4Nexthop1,
        kV6Nexthop1,
        false);

    // Verify we advertise local route to EBGP peers
    auto localRouteRibEntry = RibOutAnnouncementEntry(
        kV4Prefix1, kDefaultPathID, localPeerV4_, attrs);
    EXPECT_TRUE(adjRib_->canAnnounce(localRouteRibEntry));
    // Verify we advertise non-local route to EBGP peers
    auto nonRrClientRibEntry =
        RibOutAnnouncementEntry(kV4Prefix1, kDefaultPathID, iBgpPeer_, attrs);
    EXPECT_TRUE(adjRib_->canAnnounce(nonRrClientRibEntry));
  }
  {
    // Verify local routes are announced to IBGP peer which is not RR client
    setupAdjRib(
        kLocalAs1,
        kLocalAs1,
        kRemoteAs1,
        kIsRrClientFalse,
        kIsConfedPeerFalse,
        NextHopSelfConfigured(false),
        kV4Nexthop1,
        kV6Nexthop1,
        false);
    auto adjRibEntry = RibOutAnnouncementEntry(
        kV4Prefix1, kDefaultPathID, localPeerV4_, attrs);
    auto ret = adjRib_->canAnnounce(adjRibEntry);
    EXPECT_EQ(true, ret);
  }
  {
    // Verify local routes are announced to IBGP peer which is RR client
    setupAdjRib(
        kLocalAs1,
        kLocalAs1,
        kRemoteAs1,
        kIsRrClientTrue,
        kIsConfedPeerFalse,
        NextHopSelfConfigured(false),
        kV4Nexthop1,
        kV6Nexthop1,
        false);
    auto adjRibEntry = RibOutAnnouncementEntry(
        kV4Prefix1, kDefaultPathID, localPeerV4_, attrs);
    auto ret = adjRib_->canAnnounce(adjRibEntry);
    EXPECT_EQ(true, ret);
  }
  {
    /*
     * Verify route learnt from IBGP peer is not advertised if RRclient is
     * false
     */
    setupAdjRib(
        kLocalAs1,
        kLocalAs1,
        kRemoteAs1,
        kIsRrClientFalse,
        kIsConfedPeerFalse,
        NextHopSelfConfigured(false),
        kV4Nexthop1,
        kV6Nexthop1,
        false);
    auto adjRibEntry =
        RibOutAnnouncementEntry(kV4Prefix1, kDefaultPathID, iBgpPeer_, attrs);
    auto ret = adjRib_->canAnnounce(adjRibEntry);
    EXPECT_EQ(false, ret);
  }
  {
    // Verify route learnt from IBGP peer is advertised if RR client is true
    setupAdjRib(
        kLocalAs1,
        kLocalAs1,
        kRemoteAs1,
        kIsRrClientTrue,
        kIsConfedPeerFalse,
        NextHopSelfConfigured(false),
        kV4Nexthop1,
        kV6Nexthop1,
        false);
    auto adjRibEntry =
        RibOutAnnouncementEntry(kV4Prefix1, kDefaultPathID, iBgpPeer_, attrs);
    auto ret = adjRib_->canAnnounce(adjRibEntry);
    EXPECT_EQ(true, ret);
  }

  {
    /*
     * Set up a v4 loopback address peering
     * IBGP peer, RR client true
     * allowLoopbackReflection = true (openr setting)
     */
    setupAdjRib(
        kLocalAs1,
        kLocalAs1,
        kRemoteAs1,
        kIsRrClientTrue,
        kIsConfedPeerFalse,
        NextHopSelfConfigured(false),
        kV4Nexthop1,
        kV6Nexthop1,
        false,
        AfiIpv4Negotiated(true),
        AfiIpv6Negotiated(true),
        V4OverV6Nexthop(false),
        EnhancedRouteRefreshNegotiated(false),
        RouteRefreshNegotiated(false),
        RemovePrivateAsConfigured(false),
        nullptr,
        std::nullopt,
        kLoopBackAddressV4,
        true // allowLoopbackReflection
    );
    auto adjRibEntry = RibOutAnnouncementEntry(
        kV4Prefix1, kDefaultPathID, loopbackPeerV4_, attrs);
    auto ret = adjRib_->canAnnounce(adjRibEntry);
    EXPECT_EQ(true, ret);
  }
  {
    /*
     * Set up a v6 loopback address peering
     * IBGP peer, RR client true
     * allowLoopbackReflection = true (openr setting)
     */
    setupAdjRib(
        kLocalAs1,
        kLocalAs1,
        kRemoteAs1,
        kIsRrClientTrue,
        kIsConfedPeerFalse,
        NextHopSelfConfigured(false),
        kV4Nexthop1,
        kV6Nexthop1,
        false,
        AfiIpv4Negotiated(true),
        AfiIpv6Negotiated(true),
        V4OverV6Nexthop(false),
        EnhancedRouteRefreshNegotiated(false),
        RouteRefreshNegotiated(false),
        RemovePrivateAsConfigured(false),
        nullptr,
        std::nullopt,
        kLoopBackAddressV6,
        true // allowLoopbackReflection
    );
    auto adjRibEntry = RibOutAnnouncementEntry(
        kV4Prefix1, kDefaultPathID, loopbackPeerV6_, attrs);
    auto ret = adjRib_->canAnnounce(adjRibEntry);
    EXPECT_EQ(true, ret);
  }
  {
    /*
     * Set up a v4 loopback address peering
     * IBGP peer, RR client true
     * allowLoopbackReflection is false (bgp++ setting)
     */
    setupAdjRib(
        kLocalAs1,
        kLocalAs1,
        kRemoteAs1,
        kIsRrClientTrue,
        kIsConfedPeerFalse,
        NextHopSelfConfigured(false),
        kV4Nexthop1,
        kV6Nexthop1,
        false,
        AfiIpv4Negotiated(true),
        AfiIpv6Negotiated(true),
        V4OverV6Nexthop(false),
        EnhancedRouteRefreshNegotiated(false),
        RouteRefreshNegotiated(false),
        RemovePrivateAsConfigured(false),
        nullptr,
        std::nullopt,
        kLoopBackAddressV4,
        false // allowLoopbackReflection
    );
    auto adjRibEntry = RibOutAnnouncementEntry(
        kV4Prefix1, kDefaultPathID, loopbackPeerV4_, attrs);
    auto ret = adjRib_->canAnnounce(adjRibEntry);
    EXPECT_EQ(false, ret);
  }
}

/*
 * Verify Implicit withdrawal to a IBGP peer
 * AdjRib learns an announcement from RIB for a EBGP orginated route,
 * we advertise this route. Later due to bestpath change, same prefix
 * will be announced by RIB to be orginated from IBGP peer. Verify that
 * we withdraw previously announced route.
 */
TEST_F(AdjRibOutboundFixture, ImplicitWithdrawalOfChangedRoute) {
  // IBGP peer
  setupAdjRib();

  fm_->addTask([&] {
    // RIB is sending a EBGP learnt route. Ensure we advertise to IBGP peer
    auto ribMsg =
        createRibSingleAnnounce(kV6Prefix1, kV6Nexthop1, eBgpPeer_, true);
    pushRibOutMsgToAdjRib(ribMsg);
    fiberSleepFor(50ms);
    /*
     * RIB is updating the prefix to point to IBGP peer and different
     * nexthop Ensure that AdjRib withdraws previous route
     */
    ribMsg = createRibSingleAnnounce(kV6Prefix1, kV6Nexthop2, iBgpPeer_, false);
    pushRibOutMsgToAdjRib(ribMsg);
  });

  fm_->addTask([&] {
    /* Let sendBgpUpdates coro run if scheduled. */
    fiberSleepFor(10ms);
    auto msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(
        std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
    auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    // Verify that message is notified to Fiber Bgp Peer properly
    EXPECT_EQ(kLocalPref, bgpUpdate->attrs()->localPref());
    // originatorId is stored in network byte order
    EXPECT_EQ(htonl(kOriginatorId), *bgpUpdate->attrs()->originatorId());
    ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
    EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *bgpUpdate->mpAnnounced()->afi());
    EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *bgpUpdate->mpAnnounced()->safi());
    EXPECT_EQ(
        toBinaryAddress(kV6Nexthop1), *bgpUpdate->mpAnnounced()->nexthop());
    EXPECT_EQ(
        toIPPrefix(kV6Prefix1),
        *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());

    // Verify v4 and v6 EoRs
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));

    // Verify RIB entry is created. Match various fields from input
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV6Prefix1);
    EXPECT_NE(nullptr, adjRibEntry);
    ASSERT_NE(nullptr, adjRibEntry->getPreOut());
    EXPECT_NE(nullptr, adjRibEntry->getPostAttr());
    EXPECT_EQ(adjRibEntry->getPreOut(), adjRibEntry->getPostAttr());
    EXPECT_EQ(kLocalPref, adjRibEntry->getPreOut()->getLocalPref());
    EXPECT_EQ(kOriginatorId, adjRibEntry->getPreOut()->getOriginatorId());

    // verify stats
    EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCount());

    // Verify that implicit withdrawal message is notified properly
    msg = folly::coro::blockingWait(popFromEgressQueue());
    bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV6Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);
    ASSERT_EQ(1, bgpUpdate->mpWithdrawn()->prefixes()->size());
    EXPECT_EQ(
        toIPPrefix(kV6Prefix1),
        *bgpUpdate->mpWithdrawn()->prefixes()[0].prefix());
    EXPECT_EQ(0, adjRib_->getStats().getPostOutPrefixCount());

    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * Verify EoR is sent only for negotiated address families
 * The same setup of ImplicitWithdrawalOfChangedRoute is used to make sure
 * that only one EoR is sent.
 */
TEST_F(AdjRibOutboundFixture, EoRSentForNegotiatedAfis) {
  // Setting up a peering with ipv6 only
  setupAdjRib(
      kLocalAs1,
      kLocalAs1,
      kRemoteAs1,
      kIsRrClientFalse,
      kIsConfedPeerFalse,
      NextHopSelfConfigured(false),
      kV4Nexthop1,
      kV6Nexthop1,
      true,
      AfiIpv4Negotiated(false),
      AfiIpv6Negotiated(true));

  fm_->addTask([&] {
    // RIB is sending a EBGP learnt route. Ensure we advertise to IBGP peer
    auto ribMsg =
        createRibSingleAnnounce(kV6Prefix1, kV6Nexthop1, eBgpPeer_, true);
    pushRibOutMsgToAdjRib(ribMsg);
    fiberSleepFor(50ms);
    /*
     * RIB is updating the prefix to point to IBGP peer and different
     * nexthop Ensure that AdjRib withdraws previous route
     */
    ribMsg = createRibSingleAnnounce(kV6Prefix1, kV6Nexthop2, iBgpPeer_, false);
    pushRibOutMsgToAdjRib(ribMsg);
  });

  fm_->addTask([&] {
    /* Let sendBgpUpdates coro run if scheduled. */
    fiberSleepFor(10ms);
    auto msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(
        std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));

    // Verify v6 EoR
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    auto bgpEndOfRib = std::get<BgpEndOfRib>(*msg);
    EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *bgpEndOfRib.afi());
    EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *bgpEndOfRib.safi());

    // Verify that implicit withdrawal message is notified properly
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(
        std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));

    terminateAdjRib();
  });
  evb_.loop();
}

// Verify that EORs are sent even if no prefixes are there to advertise
TEST_F(AdjRibOutboundFixture, VerifySendingEoRWithoutPrefixes) {
  // IBGP peer
  setupAdjRib();

  fm_->addTask([&] {
    // Send RibOutAnnouncement without any prefixes. Send EORs only.
    RibOutAnnouncement ribMsg;
    ribMsg.sendWithEoR = true;
    ribMsg.initialDump = true;
    pushRibOutMsgToAdjRib(ribMsg);
  });

  fm_->addTask([&] {
    /* Let sendBgpUpdates coro run if scheduled. */
    fiberSleepFor(10ms);
    /*
     * Announcement will not lead to any bgp update but
     * we should see v4 and v6 EoRs
     */
    auto msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));

    fiberSleepFor(20ms);
    EXPECT_TRUE(boundedAdjRibOutQ_->empty());

    // Verify stats
    EXPECT_EQ(0, adjRib_->getStats().getPostOutPrefixCount());

    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * Ensure that a RibOutAnnouncement with multiple prefix is properly
 * processed. Verify the following: Prefix discarded by policy is processed
 * properly Accepted prefixes with policy modified attributes are processed
 * properly Accepted prefix without changes to attributes is processed
 * properly Verify grouping of updates based on modified attributes at
 * egress
 */
TEST_F(AdjRibOutboundFixture, UpdatePolicyProcessingIBgpPeer) {
  /*
   * Create a policy with three terms
   * Term1 match kV4Prefix1, kV4Prefix2 and apply origin action (EGP) & as
   * path overwrite action as_path_overwrite_list set to {0, 0}, AdjRib will
   * override 0 asns based on ingress or egress routes; Term2 match
   * kV4Prefix3 and discard Term3 match kV4Prefix4 and PERMIT (do not modify
   * any attributes)
   */
  const std::string policyName = kEgressPolicyName;
  auto policyManager = setup3TermPolicy(policyName);
  // IBGP peer
  setupAdjRib(policyManager, policyName);
  const std::vector<folly::CIDRNetwork> prefixSet{
      kV4Prefix1, kV4Prefix2, kV4Prefix3, kV4Prefix4};

  fm_->addTask([&] {
    auto ribMsg = createRibMultipleAnnounce(
        prefixSet,
        kV4Nexthop1,
        localPeerV4_,
        true // send EORs
    );
    pushRibOutMsgToAdjRib(ribMsg);
  });

  fm_->addTask([&] {
    /* Let sendBgpUpdates coro run if scheduled. */
    fiberSleepFor(10ms);
    // Prefixes will be announced as two BgpUpdate2 messages
    std::vector<RiggedIPPrefix> permittedPrefixSet1 = {
        RiggedIPPrefix(), RiggedIPPrefix()};
    *permittedPrefixSet1[0].prefix() = toIPPrefix(kV4Prefix1);
    *permittedPrefixSet1[1].prefix() = toIPPrefix(kV4Prefix2);

    std::vector<RiggedIPPrefix> permittedPrefixSet2 = {RiggedIPPrefix()};
    *permittedPrefixSet2[0].prefix() = toIPPrefix(kV4Prefix4);

    auto msg1 = folly::coro::blockingWait(popFromEgressQueue());
    auto msg2 = folly::coro::blockingWait(popFromEgressQueue());
    for (auto& msg : {msg1, msg2}) {
      ASSERT_TRUE(
          std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
      auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);

      if (1 == bgpUpdate->mpAnnounced()->prefixes()->size()) {
        /*
         * Verify that announcement message is notified to Fiber Bgp Peer
         * properly
         */
        ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
        EXPECT_THAT(
            permittedPrefixSet2,
            testing::UnorderedElementsAreArray(
                *bgpUpdate->mpAnnounced()->prefixes()));

        EXPECT_EQ(kLocalPref, bgpUpdate->attrs()->localPref());
        // originatorId is stored in network byte order
        EXPECT_EQ(htonl(kOriginatorId), *bgpUpdate->attrs()->originatorId());
        EXPECT_EQ(kV4Nexthop1.str(), *bgpUpdate->attrs()->nexthop());
        EXPECT_EQ(BgpAttrOrigin::BGP_ORIGIN_IGP, *bgpUpdate->attrs()->origin());
      }

      if (2 == bgpUpdate->mpAnnounced()->prefixes()->size()) {
        /*
         * Verify that announcement message is notified to Fiber Bgp Peer
         * properly
         * Verify that grouping of announcement based on modified attributes
         */
        EXPECT_THAT(
            permittedPrefixSet1,
            testing::UnorderedElementsAreArray(
                *bgpUpdate->mpAnnounced()->prefixes()));
        EXPECT_EQ(kLocalPref, bgpUpdate->attrs()->localPref());
        // originatorId is stored in network byte order
        EXPECT_EQ(htonl(kOriginatorId), *bgpUpdate->attrs()->originatorId());
        EXPECT_EQ(kV4Nexthop1.str(), *bgpUpdate->attrs()->nexthop());
        EXPECT_EQ(BgpAttrOrigin::BGP_ORIGIN_EGP, *bgpUpdate->attrs()->origin());
      }
    }

    // v4 and v6 EoRs
    folly::coro::blockingWait(popFromEgressQueue());
    folly::coro::blockingWait(popFromEgressQueue());

    auto adjRibEntry1 = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1);
    auto adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix2);
    auto adjRibEntry3 = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix3);
    auto adjRibEntry4 = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix4);

    /*
     * Verify various fields of rib entries which are accepted and policy
     * changed attributes. Verify modified attributes are present in postOut
     */
    EXPECT_NE(
        BgpAttrOrigin::BGP_ORIGIN_EGP, adjRibEntry1->getPreOut()->getOrigin());
    EXPECT_NE(adjRibEntry1->getPostAttr(), adjRibEntry1->getPreOut());
    EXPECT_EQ(
        BgpAttrOrigin::BGP_ORIGIN_EGP,
        adjRibEntry1->getPostAttr()->getOrigin());

    EXPECT_NE(
        BgpAttrOrigin::BGP_ORIGIN_EGP, adjRibEntry2->getPreOut()->getOrigin());
    EXPECT_NE(adjRibEntry2->getPostAttr(), adjRibEntry2->getPreOut());
    EXPECT_EQ(
        BgpAttrOrigin::BGP_ORIGIN_EGP,
        adjRibEntry2->getPostAttr()->getOrigin());

    // Verify preOut attributes are shared
    EXPECT_EQ(adjRibEntry1->getPreOut(), adjRibEntry2->getPreOut());
    // Verify postOut attributes are shared
    EXPECT_EQ(adjRibEntry1->getPostAttr(), adjRibEntry2->getPostAttr());

    /*
     * Verify as path in attributes (was {0, 0} after policy action)
     * has been replace to two localAsn of peer
     */
    auto asPath = adjRibEntry1->getPostAttr()->getAsPath();
    EXPECT_EQ(1, asPath->size());
    auto expectedAsns = std::vector<uint32_t>{
        adjRib_->peeringParams_.localAs, adjRib_->peeringParams_.localAs};
    EXPECT_EQ(expectedAsns, asPath->at(0).asSequence);
    EXPECT_EQ(asPath, adjRibEntry2->getPostAttr()->getAsPath());

    // Verify prefix discarded
    EXPECT_NE(nullptr, adjRibEntry3->getPreOut());
    EXPECT_EQ(nullptr, adjRibEntry3->getPostAttr());

    /*
     * Verify prefix permitted and policy did not modify anything
     * Same shared_ptr is used for postOut (IBGP)
     */
    EXPECT_EQ(adjRibEntry4->getPreOut(), adjRibEntry4->getPostAttr());
    EXPECT_NE(nullptr, adjRibEntry4->getPostAttr());
    EXPECT_EQ(
        BgpAttrOrigin::BGP_ORIGIN_IGP,
        adjRibEntry4->getPostAttr()->getOrigin());

    // Verify attributes are published
    EXPECT_TRUE(adjRibEntry1->getPostAttr()->isPublished());
    EXPECT_TRUE(adjRibEntry2->getPostAttr()->isPublished());
    EXPECT_TRUE(adjRibEntry4->getPostAttr()->isPublished());

    // Verify stats
    EXPECT_EQ(3, adjRib_->getStats().getPostOutPrefixCount());
    EXPECT_EQ(DeDuplicatedBgpPath::deduplicatorSize(), 2);

    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * Verify that a prefix which is denied due to policy, later changes it's
 * attributes, and is permitted by policy due to attribute changes is
 * processed properly and notified to Rib Verify that EORs are sent even if
 * policy denies all prefixes.
 */
TEST_F(AdjRibOutboundFixture, VerifyPermitAfterDeny) {
  /*
   * Create a policy with two terms
   * Term1 match origin IGP and deny
   * Term2 permit all
   */
  const std::string policyName = kEgressPolicyName;
  auto policyManager = setupDenyIgpOriginAcceptAllPolicy(policyName);
  // IBGP peer
  setupAdjRib(policyManager, policyName);

  fm_->addTask([&] {
    {
      // Announcement 1 which will be denied by policy
      auto ribMsg = createRibSingleAnnounce(
          kV4Prefix1,
          kV4Nexthop1,
          localPeerV4_,
          true, // EOR is true.
          BgpAttrOrigin::BGP_ORIGIN_IGP);
      pushRibOutMsgToAdjRib(ribMsg);
    }
    /*
     * Announcements after the initial dump are ignored until its EoR has been
     * sent, so let sendBgpUpdates send the EoRs first.
     */
    fiberSleepFor(40ms);
    {
      /*
       * Announcement 2 (modified origin) for same prefix will be accepted
       * by policy
       */
      auto ribMsg = createRibSingleAnnounce(
          kV4Prefix1,
          kV4Nexthop1,
          localPeerV4_,
          false,
          BgpAttrOrigin::BGP_ORIGIN_EGP);
      pushRibOutMsgToAdjRib(ribMsg);
    }
  });

  fm_->addTask([&] {
    /* Let sendBgpUpdates coro run if scheduled. */
    fiberSleepFor(10ms);
    /*
     * Announcement 1 will not lead to any bgp update but
     * we should see v4 and v6 EoRs
     */
    auto msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));

    // Verifying only after Announcement 2 is sent
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(
        std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
    auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
    EXPECT_EQ(
        toIPPrefix(kV4Prefix1),
        *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
    EXPECT_EQ(BgpAttrOrigin::BGP_ORIGIN_EGP, *bgpUpdate->attrs()->origin());

    EXPECT_TRUE(boundedAdjRibOutQ_->empty());
    // Verify adjrib entry is proper
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1);
    ASSERT_NE(nullptr, adjRibEntry->getPreOut());
    ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
    EXPECT_EQ(adjRibEntry->getPreOut(), adjRibEntry->getPostAttr());
    EXPECT_EQ(
        BgpAttrOrigin::BGP_ORIGIN_EGP, adjRibEntry->getPreOut()->getOrigin());
    // Verify stats
    EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCount());

    terminateAdjRib();
  });
  evb_.loop();
}

/*
 *  1. Create egress policy
 *  2. Setup AdjRib with egress policy and add path enabled
 *  3. Send Rib announcement of a prefix that is denied by policy
 *  4. Verify adjrib entry and adjrib tree
 *  5. Re-send same prefix with different attributes that is now allowed by
 *     policy
 *  6. Verify again adjrib entry and adjrib tree
 */
TEST_F(AdjRibOutboundFixture, VerifyPermitAfterDenyAddPath) {
  /*
   * Create a policy with two terms
   * Term1 match origin IGP and deny
   * Term2 permit all
   */
  const std::string policyName = kEgressPolicyName;
  auto policyManager = setupDenyIgpOriginAcceptAllPolicy(policyName);
  // IBGP peer
  setupAdjRib(policyManager, policyName, true, true);

  fm_->addTask([&] {
    {
      // Announcement 1 which will be denied by policy
      auto ribMsg = createRibSingleAnnounce(
          kV4Prefix1,
          kV4Nexthop1,
          localPeerV4_,
          true, // EOR is true.
          BgpAttrOrigin::BGP_ORIGIN_IGP,
          {},
          std::nullopt,
          std::nullopt,
          nullptr,
          true,
          kPlaceholderPathID);
      pushRibOutMsgToAdjRib(ribMsg);
    }
    /*
     * Announcements after the initial dump are ignored until its EoR has been
     * sent, so let sendBgpUpdates send the EoRs first.
     */
    fiberSleepFor(40ms);
    {
      /*
       * Announcement 2 (modified origin) for same prefix will be accepted
       * by policy
       */
      auto ribMsg = createRibSingleAnnounce(
          kV4Prefix1,
          kV4Nexthop1,
          localPeerV4_,
          false,
          BgpAttrOrigin::BGP_ORIGIN_EGP,
          {},
          std::nullopt,
          std::nullopt,
          nullptr,
          true,
          kPlaceholderPathID);
      pushRibOutMsgToAdjRib(ribMsg);
    }
  });

  fm_->addTask([&] {
    /* Let sendBgpUpdates coro run if scheduled. */
    fiberSleepFor(10ms);
    /*
     * Announcement 1 will not lead to any bgp update but
     * we should see v4 and v6 EoRs
     */
    auto msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));

    // Verifying only after Announcement 2 is sent
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(
        std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
    auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
    EXPECT_EQ(
        toIPPrefix(kV4Prefix1),
        *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
    EXPECT_EQ(BgpAttrOrigin::BGP_ORIGIN_EGP, *bgpUpdate->attrs()->origin());

    EXPECT_TRUE(boundedAdjRibOutQ_->empty());
    // Verify adjrib entry is proper
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1);
    ASSERT_NE(nullptr, adjRibEntry->getPreOut());
    ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
    EXPECT_EQ(adjRibEntry->getPreOut(), adjRibEntry->getPostAttr());
    EXPECT_EQ(
        BgpAttrOrigin::BGP_ORIGIN_EGP, adjRibEntry->getPreOut()->getOrigin());

    // Verify AdjRibTree size is non-zero
    EXPECT_EQ(
        1,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/true));
    // Verify stats
    EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCount());

    terminateAdjRib();
  });
  evb_.loop();
}

/**
 * Create four peers with different options for advertiseLinkBandwidth
 * config: DISABLE, BEST_PATH, SET_LINK_BPS, and AGGREGATE_RECEIVED. Create
 * four AdjRibOuts corresponding to the peers.
 *
 * Simulate two RibOutAnnouncement's;
 * - one with LBW community of 10G and aggregateReceivedUcmpWeight of zero
 * - the other with LBW community of 10G and aggregateReceivedUcmpWeight of
 * 20G.
 *
 * Expectations for the different options of advertiseLinkBandwidth:
 * - DISABLE: Prune LBW community as per config
 * - BEST_PATH: Prune LBW from the first announcement because aggregate is
 *   zero; Send LBW of 10G for the second announcement, which is the value
 * in best path
 * - SET_LINK_BPS: set to value specified in link_bandwidth_bps config (5G)
 * - AGGREGATE_RECEIVED: Prune LBW from the first announcement because
 * aggregate is zero; Send LBW of 20G for the second announcement, which is
 * the value of the aggregate
 */
TEST_F(AdjRibOutboundFixture, AdvertiseLinkBandwidth) {
  std::shared_ptr<PolicyManager> policyManager;

  TinyPeerInfo inPeer{
      folly::IPAddress("20.1.1.1"), 1, 1, BgpSessionType::EBGP, false};
  TinyPeerInfo outPeer1{
      folly::IPAddress("30.1.1.1"), 1, 2, BgpSessionType::EBGP, false};
  BgpPeerId outPeerId1(outPeer1.addr, outPeer1.routerId);
  TinyPeerInfo outPeer2{
      folly::IPAddress("40.1.1.1"), 1, 3, BgpSessionType::EBGP, false};
  BgpPeerId outPeerId2(outPeer2.addr, outPeer2.routerId);
  TinyPeerInfo outPeer3{
      folly::IPAddress("50.1.1.1"), 1, 4, BgpSessionType::EBGP, false};
  BgpPeerId outPeerId3(outPeer3.addr, outPeer3.routerId);
  TinyPeerInfo outPeer4{
      folly::IPAddress("60.1.1.1"), 1, 5, BgpSessionType::EBGP, false};
  BgpPeerId outPeerId4(outPeer4.addr, outPeer4.routerId);
  TinyPeerInfo outPeer5{
      folly::IPAddress("70.1.1.1"), 1, 6, BgpSessionType::EBGP, false};
  BgpPeerId outPeerId5(outPeer5.addr, outPeer5.routerId);
  TinyPeerInfo outPeer6{
      folly::IPAddress("80.1.1.1"), 1, 7, BgpSessionType::EBGP, false};
  BgpPeerId outPeerId6(outPeer6.addr, outPeer6.routerId);

  // Create Peer-1 with advertiseLinkBandwidth set to DISABLE
  PeeringParams peeringParamsOutPeer1{};
  peeringParamsOutPeer1.advertiseLinkBandwidth =
      AdvertiseLinkBandwidth::DISABLE;
  peeringParamsOutPeer1.peerAddr = outPeer1.addr;
  peeringParamsOutPeer1.localAs = kLocalAs1;
  auto adjRibOutGroup1 = std::make_shared<AdjRibOutGroup>(evb_, "Group1");
  AdjRib adjRibOutPeer1{
      outPeerId1,
      peeringParamsOutPeer1,
      evb_,
      ribInQ_,
      observerQ_,
      std::make_shared<folly::coro::Baton>(),
      policyManager,
      std::make_shared<std::atomic<bool>>(false) /* isSafeModeOn */,
      std::nullopt,
      std::nullopt,
      adjRibOutGroup1};
  adjRibOutPeer1.isAfiIpv4Negotiated_ = true;
  adjRibOutPeer1.pathIdGenerator_ = std::make_unique<PathIdGenerator>(false);

  // Create Peer-2 with advertiseLinkBandwidth set to BEST_PATH
  PeeringParams peeringParamsOutPeer2{};
  peeringParamsOutPeer2.advertiseLinkBandwidth =
      AdvertiseLinkBandwidth::BEST_PATH;
  peeringParamsOutPeer2.peerAddr = outPeer2.addr;
  peeringParamsOutPeer2.localAs = kLocalAs1;
  auto adjRibOutGroup2 = std::make_shared<AdjRibOutGroup>(evb_, "Group2");
  AdjRib adjRibOutPeer2{
      outPeerId2,
      peeringParamsOutPeer2,
      evb_,
      ribInQ_,
      observerQ_,
      std::make_shared<folly::coro::Baton>(),
      policyManager,
      std::make_shared<std::atomic<bool>>(false) /* isSafeModeOn */,
      std::nullopt,
      std::nullopt,
      adjRibOutGroup2};
  adjRibOutPeer2.isAfiIpv4Negotiated_ = true;
  adjRibOutPeer2.pathIdGenerator_ = std::make_unique<PathIdGenerator>(false);

  // Create Peer-3 with fwdLBW with SET_LINK_BPS of 5Gbps
  PeeringParams peeringParamsOutPeer3{};
  peeringParamsOutPeer3.advertiseLinkBandwidth =
      AdvertiseLinkBandwidth::SET_LINK_BPS;
  peeringParamsOutPeer3.linkBandwidthBps = kLbw5G;
  peeringParamsOutPeer3.peerAddr = outPeer3.addr;
  peeringParamsOutPeer3.localAs = kLocalAs1;
  auto adjRibOutGroup3 = std::make_shared<AdjRibOutGroup>(evb_, "Group3");
  AdjRib adjRibOutPeer3{
      outPeerId3,
      peeringParamsOutPeer3,
      evb_,
      ribInQ_,
      observerQ_,
      std::make_shared<folly::coro::Baton>(),
      policyManager,
      std::make_shared<std::atomic<bool>>(false) /* isSafeModeOn */,
      std::nullopt,
      std::nullopt,
      adjRibOutGroup3};
  adjRibOutPeer3.isAfiIpv4Negotiated_ = true;
  adjRibOutPeer3.pathIdGenerator_ = std::make_unique<PathIdGenerator>(false);

  // Create Peer-4 with advertiseLinkBandwidth set to AGGREGATE_RECEIVED
  PeeringParams peeringParamsOutPeer4{};
  peeringParamsOutPeer4.advertiseLinkBandwidth =
      AdvertiseLinkBandwidth::AGGREGATE_RECEIVED;
  peeringParamsOutPeer4.peerAddr = outPeer4.addr;
  peeringParamsOutPeer4.localAs = kLocalAs1;
  auto adjRibOutGroup4 = std::make_shared<AdjRibOutGroup>(evb_, "Group4");
  AdjRib adjRibOutPeer4{
      outPeerId4,
      peeringParamsOutPeer4,
      evb_,
      ribInQ_,
      observerQ_,
      std::make_shared<folly::coro::Baton>(),
      policyManager,
      std::make_shared<std::atomic<bool>>(false) /* isSafeModeOn */,
      std::nullopt,
      std::nullopt,
      adjRibOutGroup4};
  adjRibOutPeer4.isAfiIpv4Negotiated_ = true;
  adjRibOutPeer4.pathIdGenerator_ = std::make_unique<PathIdGenerator>(false);

  // Create Peer-5 with advertiseLinkBandwidth set to AGGREGATE_LOCAL
  PeeringParams peeringParamsOutPeer5{};
  peeringParamsOutPeer5.advertiseLinkBandwidth =
      AdvertiseLinkBandwidth::AGGREGATE_LOCAL;
  peeringParamsOutPeer5.peerAddr = outPeer5.addr;
  peeringParamsOutPeer5.localAs = kLocalAs1;
  auto adjRibOutGroup5 = std::make_shared<AdjRibOutGroup>(evb_, "Group5");
  AdjRib adjRibOutPeer5{
      outPeerId5,
      peeringParamsOutPeer5,
      evb_,
      ribInQ_,
      observerQ_,
      std::make_shared<folly::coro::Baton>(),
      policyManager,
      std::make_shared<std::atomic<bool>>(false) /* isSafeModeOn */,
      std::nullopt,
      std::nullopt,
      adjRibOutGroup5};
  adjRibOutPeer5.isAfiIpv4Negotiated_ = true;
  adjRibOutPeer5.pathIdGenerator_ = std::make_unique<PathIdGenerator>(false);

  // Create Peer-6 with advertiseLinkBandwidth set to RIB_POLICY_LBW
  PeeringParams peeringParamsOutPeer6{};
  peeringParamsOutPeer6.advertiseLinkBandwidth =
      AdvertiseLinkBandwidth::RIB_POLICY_LBW;
  peeringParamsOutPeer6.peerAddr = outPeer6.addr;
  peeringParamsOutPeer6.localAs = kLocalAs1;
  auto adjRibOutGroup6 = std::make_shared<AdjRibOutGroup>(evb_, "Group6");
  AdjRib adjRibOutPeer6{
      outPeerId6,
      peeringParamsOutPeer6,
      evb_,
      ribInQ_,
      observerQ_,
      std::make_shared<folly::coro::Baton>(),
      policyManager,
      std::make_shared<std::atomic<bool>>(false) /* isSafeModeOn */,
      std::nullopt,
      std::nullopt,
      adjRibOutGroup6};
  adjRibOutPeer6.isAfiIpv4Negotiated_ = true;
  adjRibOutPeer6.pathIdGenerator_ = std::make_unique<PathIdGenerator>(false);

  folly::CIDRNetwork prefix = folly::IPAddress::createNetwork("10.10.10.0/24");

  BgpUpdate2 inputUpdate = buildBgpUpdateAttributes(kV4Nexthop1);
  auto inAttrs = std::make_shared<facebook::bgp::BgpPath>(
      BgpPathFields(*BgpUpdate2toBgpPathC(inputUpdate)));
  // Add a LBW community with b/w = 10Gbps
  {
    BgpAttrExtCommunitiesC extCommunities;
    BgpExtCommunityLinkBandWidthTypeC correctLBW{uint16_t(kRemoteAs2), kLbw10G};
    // get the raw values from correct lbw
    auto [rawHighVal, rawLowVal] = correctLBW.getRawValueInWords();
    /*
     * flip the type from 0x40 to 0x00 so that it is "transitive" now
     * and we treat it as invalid LBW, which will be pruned before
     * advertising
     */
    uint32_t newHighVal = (0x00 << 24) + (rawHighVal - (0x40 << 24));
    BgpAttrExtCommunityC transitiveLBW{newHighVal, rawLowVal};
    extCommunities.emplace_back(correctLBW);
    extCommunities.emplace_back(transitiveLBW);
    inAttrs->setExtCommunities(extCommunities);
  }
  inAttrs->publish();
  {
    /*
     * Test with a rib announcement where aggregated UCMP weights are not
     * set
     */
    RibOutAnnouncementEntry update{prefix, kDefaultPathID, inPeer, inAttrs};
    {
      // Peer1, DISABLE - LBW community should be pruned always
      adjRibOutPeer1.processRibAnnouncedEntry(update);
      auto adjRibEntry = adjRibOutPeer1.getRibEntry(/*ingress=*/false, prefix);
      auto outAttrs = adjRibEntry->getPostAttr();
      auto extCommunities = outAttrs->getExtCommunities().get();
      EXPECT_EQ(extCommunities.size(), 0);
      EXPECT_FALSE(outAttrs->hasNonTransitiveLbwExtCommunity());
    }
    {
      /*
       * Peer2, BEST_PATH - LBW community should be pruned because
       * aggregated received weight is missing
       */
      adjRibOutPeer2.processRibAnnouncedEntry(update);
      auto adjRibEntry = adjRibOutPeer2.getRibEntry(/*ingress=*/false, prefix);
      auto outAttrs = adjRibEntry->getPostAttr();
      auto extCommunities = outAttrs->getExtCommunities().get();
      EXPECT_EQ(extCommunities.size(), 0);
      EXPECT_FALSE(outAttrs->hasNonTransitiveLbwExtCommunity());
    }
    {
      /*
       * Peer3, SET_LINK_BPS - Should have community with desired value of
       * 5G
       */
      adjRibOutPeer3.processRibAnnouncedEntry(update);
      auto adjRibEntry = adjRibOutPeer3.getRibEntry(/*ingress=*/false, prefix);
      auto outAttrs = adjRibEntry->getPostAttr();
      auto extCommunities = outAttrs->getExtCommunities().get();
      EXPECT_TRUE(outAttrs->getNonTransitiveLbwValue().has_value());
      EXPECT_TRUE(outAttrs->getNonTransitiveLbwAsn().has_value());
      EXPECT_EQ(outAttrs->getNonTransitiveLbwValue().value(), kLbw5G);
      EXPECT_EQ(AsNum(outAttrs->getNonTransitiveLbwAsn().value()), kLocalAs1);
    }
    {
      /*
       * Peer4, AGGREGATE_RECEIVED - LBW community should be pruned because
       * aggregated received weight is missing
       */
      adjRibOutPeer4.processRibAnnouncedEntry(update);
      auto adjRibEntry = adjRibOutPeer4.getRibEntry(/*ingress=*/false, prefix);
      auto outAttrs = adjRibEntry->getPostAttr();
      auto extCommunities = outAttrs->getExtCommunities().get();
      EXPECT_EQ(extCommunities.size(), 0);
      EXPECT_FALSE(outAttrs->hasNonTransitiveLbwExtCommunity());
    }
    {
      /*
       * Peer5, AGGREGATE_LOCAL - LBW community should be pruned because
       * aggregated local weight is missing
       */
      adjRibOutPeer5.processRibAnnouncedEntry(update);
      auto adjRibEntry = adjRibOutPeer5.getRibEntry(/*ingress=*/false, prefix);
      auto outAttrs = adjRibEntry->getPostAttr();
      auto extCommunities = outAttrs->getExtCommunities().get();
      EXPECT_EQ(extCommunities.size(), 0);
      EXPECT_FALSE(outAttrs->hasNonTransitiveLbwExtCommunity());
    }
    {
      /*
       * Peer6, RIB_POLICY_LBW - LBW community should be pruned because
       * rib policy weight is missing
       */
      adjRibOutPeer6.processRibAnnouncedEntry(update);
      auto adjRibEntry = adjRibOutPeer6.getRibEntry(/*ingress=*/false, prefix);
      auto outAttrs = adjRibEntry->getPostAttr();
      auto extCommunities = outAttrs->getExtCommunities().get();
      EXPECT_EQ(extCommunities.size(), 0);
      EXPECT_FALSE(outAttrs->hasNonTransitiveLbwExtCommunity());
    }
  }
  {
    /*
     * Test with a rib announcement where aggregated weights as 20G
     * (received) 30G (local), 10G (best-path), 5G (rib-policy)
     */
    RibOutAnnouncementEntry update{
        prefix,
        kDefaultPathID,
        inPeer,
        inAttrs,
        std::nullopt,
        std::nullopt,
        kLbw20G,
        kLbw10G * 3,
        kLbw5G};
    {
      // Peer1, DISABLE - LBW community should be pruned always
      adjRibOutPeer1.processRibAnnouncedEntry(update);
      auto adjRibEntry = adjRibOutPeer1.getRibEntry(/*ingress=*/false, prefix);
      auto outAttrs = adjRibEntry->getPostAttr();
      auto extCommunities = outAttrs->getExtCommunities().get();
      EXPECT_EQ(extCommunities.size(), 0);
      EXPECT_FALSE(outAttrs->hasNonTransitiveLbwExtCommunity());
    }
    {
      /*
       * Peer2, BEST_PATH - LBW community should be retained because
       * aggregated received weight is present
       */
      adjRibOutPeer2.processRibAnnouncedEntry(update);
      auto adjRibEntry = adjRibOutPeer2.getRibEntry(/*ingress=*/false, prefix);
      auto outAttrs = adjRibEntry->getPostAttr();
      auto extCommunities = outAttrs->getExtCommunities().get();
      EXPECT_EQ(extCommunities.size(), 1);
      EXPECT_TRUE(outAttrs->hasNonTransitiveLbwExtCommunity());
      EXPECT_EQ(outAttrs->getNonTransitiveLbwValue().value(), kLbw10G);
      EXPECT_EQ(AsNum(outAttrs->getNonTransitiveLbwAsn().value()), kRemoteAs2);
    }
    {
      /*
       * Peer3, SET_LINK_BPS - Should have community with desired value of
       * 5G
       */
      adjRibOutPeer3.processRibAnnouncedEntry(update);
      auto adjRibEntry = adjRibOutPeer3.getRibEntry(/*ingress=*/false, prefix);
      auto outAttrs = adjRibEntry->getPostAttr();
      auto extCommunities = outAttrs->getExtCommunities().get();
      EXPECT_TRUE(outAttrs->getNonTransitiveLbwValue().has_value());
      EXPECT_TRUE(outAttrs->getNonTransitiveLbwAsn().has_value());
      EXPECT_EQ(outAttrs->getNonTransitiveLbwValue().value(), kLbw5G);
      EXPECT_EQ(AsNum(outAttrs->getNonTransitiveLbwAsn().value()), kLocalAs1);
    }
    {
      /*
       * Peer4, AGGREGATE_RECEIVED - LBW community should be set to
       * aggregated received weight of 20G
       */
      adjRibOutPeer4.processRibAnnouncedEntry(update);
      auto adjRibEntry = adjRibOutPeer4.getRibEntry(/*ingress=*/false, prefix);
      auto outAttrs = adjRibEntry->getPostAttr();
      auto extCommunities = outAttrs->getExtCommunities().get();
      EXPECT_EQ(extCommunities.size(), 1);
      EXPECT_TRUE(outAttrs->hasNonTransitiveLbwExtCommunity());
      EXPECT_EQ(outAttrs->getNonTransitiveLbwValue().value(), kLbw20G);
      EXPECT_EQ(AsNum(outAttrs->getNonTransitiveLbwAsn().value()), kLocalAs1);
    }
    {
      /*
       * Peer5, AGGREGATE_LOCAL - LBW community should be set to aggregated
       * local weight of 30G
       */
      adjRibOutPeer5.processRibAnnouncedEntry(update);
      auto adjRibEntry = adjRibOutPeer5.getRibEntry(/*ingress=*/false, prefix);
      auto outAttrs = adjRibEntry->getPostAttr();
      auto extCommunities = outAttrs->getExtCommunities().get();
      EXPECT_EQ(extCommunities.size(), 1);
      EXPECT_TRUE(outAttrs->hasNonTransitiveLbwExtCommunity());
      EXPECT_EQ(outAttrs->getNonTransitiveLbwValue().value(), kLbw10G * 3);
      EXPECT_EQ(AsNum(outAttrs->getNonTransitiveLbwAsn().value()), kLocalAs1);
    }
    {
      /*
       * Peer6, RIB_POLICY_LBW - LBW community should be set to rib
       * policy weight of 5G
       */
      adjRibOutPeer6.processRibAnnouncedEntry(update);
      auto adjRibEntry = adjRibOutPeer6.getRibEntry(/*ingress=*/false, prefix);
      auto outAttrs = adjRibEntry->getPostAttr();
      auto extCommunities = outAttrs->getExtCommunities().get();
      EXPECT_EQ(extCommunities.size(), 1);
      EXPECT_TRUE(outAttrs->hasNonTransitiveLbwExtCommunity());
      EXPECT_EQ(outAttrs->getNonTransitiveLbwValue().value(), kLbw5G);
      EXPECT_EQ(AsNum(outAttrs->getNonTransitiveLbwAsn().value()), kLocalAs1);
    }
    {
      adjRibOutPeer6.peeringParams_.advertiseLinkBandwidth =
          static_cast<AdvertiseLinkBandwidth>(10);

      // Invalid advertise link bandwidth parameter
      EXPECT_DEATH(adjRibOutPeer6.processRibAnnouncedEntry(update), "");

      adjRibOutPeer6.peeringParams_.advertiseLinkBandwidth =
          AdvertiseLinkBandwidth::SET_LINK_BPS;
      adjRibOutPeer6.peeringParams_.linkBandwidthBps = std::nullopt;

      // linkBandwidthBps is null
      EXPECT_DEATH(
          adjRibOutPeer6.processRibAnnouncedEntry(update),
          "linkBandwidthBps not set for UCMP SET_LINK_BPS");

      // Cover the case that postPolicyAttrs is nullptr
      adjRibOutPeer6.updateAdvertiseLbwExtCommunity(update, nullptr);
    }
  }
}

/**
 * Test Egress UCMP Policy (Per Route UCMP)
 * both per-peer and per-route config has 5 possible values
 * (DISABLE, SET_LINK_BPS, BEST_PATH, AGGREGATE_RECEIVED, AGGREGATE_LOCAL)
 * we test all 5 * 5 = 25 combinations
 */
struct TestParam {
  AdvertiseLinkBandwidth advertiseLinkBandwidth;
  bgp_policy::LbwExtCommunityActionType lbwPolicyActionType;
  TestParam(
      AdvertiseLinkBandwidth advertiseLinkBandwidth,
      bgp_policy::LbwExtCommunityActionType lbwPolicyActionType)
      : advertiseLinkBandwidth(advertiseLinkBandwidth),
        lbwPolicyActionType(lbwPolicyActionType) {}
};

class EgressUcmpPolicyFixture
    : public AdjRibOutboundFixture,
      public ::testing::WithParamInterface<TestParam> {};

INSTANTIATE_TEST_CASE_P(
    EgressUcmpPolicyInstance,
    EgressUcmpPolicyFixture,
    ::testing::Values(
        TestParam(
            AdvertiseLinkBandwidth::DISABLE,
            bgp_policy::LbwExtCommunityActionType::DISABLE),
        TestParam(
            AdvertiseLinkBandwidth::DISABLE,
            bgp_policy::LbwExtCommunityActionType::SET_LINK_BPS),
        TestParam(
            AdvertiseLinkBandwidth::DISABLE,
            bgp_policy::LbwExtCommunityActionType::BEST_PATH),
        TestParam(
            AdvertiseLinkBandwidth::DISABLE,
            bgp_policy::LbwExtCommunityActionType::AGGREGATE_RECEIVED),
        TestParam(
            AdvertiseLinkBandwidth::DISABLE,
            bgp_policy::LbwExtCommunityActionType::AGGREGATE_LOCAL),
        TestParam(
            AdvertiseLinkBandwidth::DISABLE,
            bgp_policy::LbwExtCommunityActionType::
                ENCODE_AGGREGATE_RECEIVED_OVERWRITE),
        TestParam(
            AdvertiseLinkBandwidth::DISABLE,
            bgp_policy::LbwExtCommunityActionType::ENCODE_MULTIPATH),
        TestParam(
            AdvertiseLinkBandwidth::DISABLE,
            bgp_policy::LbwExtCommunityActionType::ENCODE_SWITCH_ID),
        TestParam(
            AdvertiseLinkBandwidth::DISABLE,
            bgp_policy::LbwExtCommunityActionType::
                DECODE_AGGREGATE_CAPACITY_OVERWRITE),
        TestParam(
            AdvertiseLinkBandwidth::SET_LINK_BPS,
            bgp_policy::LbwExtCommunityActionType::DISABLE),
        TestParam(
            AdvertiseLinkBandwidth::SET_LINK_BPS,
            bgp_policy::LbwExtCommunityActionType::SET_LINK_BPS),
        TestParam(
            AdvertiseLinkBandwidth::SET_LINK_BPS,
            bgp_policy::LbwExtCommunityActionType::BEST_PATH),
        TestParam(
            AdvertiseLinkBandwidth::SET_LINK_BPS,
            bgp_policy::LbwExtCommunityActionType::AGGREGATE_RECEIVED),
        TestParam(
            AdvertiseLinkBandwidth::SET_LINK_BPS,
            bgp_policy::LbwExtCommunityActionType::AGGREGATE_LOCAL),
        TestParam(
            AdvertiseLinkBandwidth::BEST_PATH,
            bgp_policy::LbwExtCommunityActionType::DISABLE),
        TestParam(
            AdvertiseLinkBandwidth::BEST_PATH,
            bgp_policy::LbwExtCommunityActionType::SET_LINK_BPS),
        TestParam(
            AdvertiseLinkBandwidth::BEST_PATH,
            bgp_policy::LbwExtCommunityActionType::BEST_PATH),
        TestParam(
            AdvertiseLinkBandwidth::BEST_PATH,
            bgp_policy::LbwExtCommunityActionType::AGGREGATE_RECEIVED),
        TestParam(
            AdvertiseLinkBandwidth::BEST_PATH,
            bgp_policy::LbwExtCommunityActionType::AGGREGATE_LOCAL),
        TestParam(
            AdvertiseLinkBandwidth::AGGREGATE_RECEIVED,
            bgp_policy::LbwExtCommunityActionType::DISABLE),
        TestParam(
            AdvertiseLinkBandwidth::AGGREGATE_RECEIVED,
            bgp_policy::LbwExtCommunityActionType::SET_LINK_BPS),
        TestParam(
            AdvertiseLinkBandwidth::AGGREGATE_RECEIVED,
            bgp_policy::LbwExtCommunityActionType::BEST_PATH),
        TestParam(
            AdvertiseLinkBandwidth::AGGREGATE_RECEIVED,
            bgp_policy::LbwExtCommunityActionType::AGGREGATE_RECEIVED),
        TestParam(
            AdvertiseLinkBandwidth::AGGREGATE_RECEIVED,
            bgp_policy::LbwExtCommunityActionType::AGGREGATE_LOCAL),
        TestParam(
            AdvertiseLinkBandwidth::AGGREGATE_LOCAL,
            bgp_policy::LbwExtCommunityActionType::DISABLE),
        TestParam(
            AdvertiseLinkBandwidth::AGGREGATE_LOCAL,
            bgp_policy::LbwExtCommunityActionType::SET_LINK_BPS),
        TestParam(
            AdvertiseLinkBandwidth::AGGREGATE_LOCAL,
            bgp_policy::LbwExtCommunityActionType::BEST_PATH),
        TestParam(
            AdvertiseLinkBandwidth::AGGREGATE_LOCAL,
            bgp_policy::LbwExtCommunityActionType::AGGREGATE_RECEIVED),
        TestParam(
            AdvertiseLinkBandwidth::AGGREGATE_LOCAL,
            bgp_policy::LbwExtCommunityActionType::AGGREGATE_LOCAL),
        TestParam(
            AdvertiseLinkBandwidth::RIB_POLICY_LBW,
            bgp_policy::LbwExtCommunityActionType::DISABLE),
        TestParam(
            AdvertiseLinkBandwidth::RIB_POLICY_LBW,
            bgp_policy::LbwExtCommunityActionType::SET_LINK_BPS),
        TestParam(
            AdvertiseLinkBandwidth::RIB_POLICY_LBW,
            bgp_policy::LbwExtCommunityActionType::BEST_PATH),
        TestParam(
            AdvertiseLinkBandwidth::RIB_POLICY_LBW,
            bgp_policy::LbwExtCommunityActionType::AGGREGATE_RECEIVED),
        TestParam(
            AdvertiseLinkBandwidth::RIB_POLICY_LBW,
            bgp_policy::LbwExtCommunityActionType::AGGREGATE_LOCAL)));

/**
 * Test EgressUcmpPolicy with following configs:
 * - prePolicyAttr asn-lbw:
 *     (kRemoteAs2, kLbw10G),
 *     200.f(agg-recv), 400.f(agg-local), 500.f(rib-policy)
 * - peer-config asn-lbw:
 *     (kLocalAs1, kLbw5G)
 * two routes passed in: p1, p2
 * p1 (no ucmp policy)
 * p2 (has ucmp policy)
 * verify expected behaviors (policy takes precedence over peer-config)
 */
TEST_P(EgressUcmpPolicyFixture, EgressUcmpPolicy) {
  // create a single prefix match
  auto createPrefixMatch =
      [&](folly::CIDRNetwork prefix) -> bgp_policy::BgpPolicyAtomicMatch {
    routing_policy::CompareNumericValue compareStructEQ;
    compareStructEQ.compare_operator() = routing_policy::ComparisonOperator::EQ;
    compareStructEQ.value() = prefix.second;
    const auto& prefixListEntry = createPrefixListEntry(
        IPAddress::networkToString(prefix), {compareStructEQ});
    return createPrefixListMatch({prefixListEntry});
  };

  // verify link bandwidth per peer config
  auto verifyLbwPerPeerConfig = [&](const std::shared_ptr<const BgpPath> attrs,
                                    AdvertiseLinkBandwidth
                                        advertiseLinkBandwidth) {
    switch (advertiseLinkBandwidth) {
      case AdvertiseLinkBandwidth::DISABLE:
        EXPECT_TRUE(attrs->getExtCommunities().nullOrEmpty());
        EXPECT_FALSE(attrs->hasNonTransitiveLbwExtCommunity());
        EXPECT_FALSE(attrs->getNonTransitiveLbwAsn().has_value());
        EXPECT_FALSE(attrs->getNonTransitiveLbwValue().has_value());
        break;
      case AdvertiseLinkBandwidth::SET_LINK_BPS:
        EXPECT_EQ(1, attrs->getExtCommunities()->size());
        EXPECT_TRUE(attrs->hasNonTransitiveLbwExtCommunity());
        EXPECT_TRUE(attrs->getNonTransitiveLbwAsn().has_value());
        EXPECT_TRUE(attrs->getNonTransitiveLbwValue().has_value());
        EXPECT_EQ(AsNum(attrs->getNonTransitiveLbwAsn().value()), kLocalAs1);
        EXPECT_EQ(attrs->getNonTransitiveLbwValue().value(), kLbw5G);
        break;
      case AdvertiseLinkBandwidth::BEST_PATH:
        EXPECT_EQ(1, attrs->getExtCommunities()->size());
        EXPECT_TRUE(attrs->hasNonTransitiveLbwExtCommunity());
        EXPECT_TRUE(attrs->getNonTransitiveLbwAsn().has_value());
        EXPECT_TRUE(attrs->getNonTransitiveLbwValue().has_value());
        EXPECT_EQ(AsNum(attrs->getNonTransitiveLbwAsn().value()), kRemoteAs2);
        EXPECT_EQ(attrs->getNonTransitiveLbwValue().value(), kLbw10G);
        break;
      case AdvertiseLinkBandwidth::AGGREGATE_RECEIVED:
        EXPECT_EQ(1, attrs->getExtCommunities()->size());
        EXPECT_TRUE(attrs->hasNonTransitiveLbwExtCommunity());
        EXPECT_TRUE(attrs->getNonTransitiveLbwAsn().has_value());
        EXPECT_TRUE(attrs->getNonTransitiveLbwValue().has_value());
        EXPECT_EQ(AsNum(attrs->getNonTransitiveLbwAsn().value()), kLocalAs1);
        EXPECT_EQ(attrs->getNonTransitiveLbwValue().value(), 200.0f);
        break;
      case AdvertiseLinkBandwidth::AGGREGATE_LOCAL:
        EXPECT_EQ(1, attrs->getExtCommunities()->size());
        EXPECT_TRUE(attrs->hasNonTransitiveLbwExtCommunity());
        EXPECT_TRUE(attrs->getNonTransitiveLbwAsn().has_value());
        EXPECT_TRUE(attrs->getNonTransitiveLbwValue().has_value());
        EXPECT_EQ(AsNum(attrs->getNonTransitiveLbwAsn().value()), kLocalAs1);
        EXPECT_EQ(attrs->getNonTransitiveLbwValue().value(), 400.0f);
        break;
      case AdvertiseLinkBandwidth::RIB_POLICY_LBW:
        EXPECT_EQ(1, attrs->getExtCommunities()->size());
        EXPECT_TRUE(attrs->hasNonTransitiveLbwExtCommunity());
        EXPECT_TRUE(attrs->getNonTransitiveLbwAsn().has_value());
        EXPECT_TRUE(attrs->getNonTransitiveLbwValue().has_value());
        EXPECT_EQ(AsNum(attrs->getNonTransitiveLbwAsn().value()), kLocalAs1);
        EXPECT_EQ(attrs->getNonTransitiveLbwValue().value(), 500.0f);
        break;
      default:
        EXPECT_FALSE(true);
    }
  };

  // verify link bandwidth per route config
  auto verifyLbwPerRouteConfig = [&](const std::shared_ptr<const BgpPath> attrs,
                                     bgp_policy::LbwExtCommunityActionType
                                         lbwPolicyActionType) {
    switch (lbwPolicyActionType) {
      case bgp_policy::LbwExtCommunityActionType::DISABLE:
        EXPECT_TRUE(attrs->getExtCommunities().nullOrEmpty());
        EXPECT_FALSE(attrs->hasNonTransitiveLbwExtCommunity());
        EXPECT_FALSE(attrs->getNonTransitiveLbwAsn().has_value());
        EXPECT_FALSE(attrs->getNonTransitiveLbwValue().has_value());
        break;
      case bgp_policy::LbwExtCommunityActionType::SET_LINK_BPS:
        EXPECT_EQ(1, attrs->getExtCommunities()->size());
        EXPECT_TRUE(attrs->hasNonTransitiveLbwExtCommunity());
        EXPECT_TRUE(attrs->getNonTransitiveLbwAsn().has_value());
        EXPECT_TRUE(attrs->getNonTransitiveLbwValue().has_value());
        EXPECT_EQ(AsNum(attrs->getNonTransitiveLbwAsn().value()), kLocalAs1);
        EXPECT_EQ(attrs->getNonTransitiveLbwValue().value(), kLbw5G);
        break;
      case bgp_policy::LbwExtCommunityActionType::BEST_PATH:
        EXPECT_EQ(1, attrs->getExtCommunities()->size());
        EXPECT_TRUE(attrs->hasNonTransitiveLbwExtCommunity());
        EXPECT_TRUE(attrs->getNonTransitiveLbwAsn().has_value());
        EXPECT_TRUE(attrs->getNonTransitiveLbwValue().has_value());
        EXPECT_EQ(AsNum(attrs->getNonTransitiveLbwAsn().value()), kRemoteAs2);
        EXPECT_EQ(attrs->getNonTransitiveLbwValue().value(), kLbw10G);
        break;
      case bgp_policy::LbwExtCommunityActionType::AGGREGATE_RECEIVED:
        EXPECT_EQ(1, attrs->getExtCommunities()->size());
        EXPECT_TRUE(attrs->hasNonTransitiveLbwExtCommunity());
        EXPECT_TRUE(attrs->getNonTransitiveLbwAsn().has_value());
        EXPECT_TRUE(attrs->getNonTransitiveLbwValue().has_value());
        EXPECT_EQ(AsNum(attrs->getNonTransitiveLbwAsn().value()), kLocalAs1);
        EXPECT_EQ(attrs->getNonTransitiveLbwValue().value(), 200.0f);
        break;
      case bgp_policy::LbwExtCommunityActionType::AGGREGATE_LOCAL:
        EXPECT_EQ(1, attrs->getExtCommunities()->size());
        EXPECT_TRUE(attrs->hasNonTransitiveLbwExtCommunity());
        EXPECT_TRUE(attrs->getNonTransitiveLbwAsn().has_value());
        EXPECT_TRUE(attrs->getNonTransitiveLbwValue().has_value());
        EXPECT_EQ(AsNum(attrs->getNonTransitiveLbwAsn().value()), kLocalAs1);
        EXPECT_EQ(attrs->getNonTransitiveLbwValue().value(), 400.0f);
        break;
      case bgp_policy::LbwExtCommunityActionType::
          ENCODE_AGGREGATE_RECEIVED_OVERWRITE:
        EXPECT_EQ(1, attrs->getExtCommunities()->size());
        EXPECT_TRUE(attrs->hasNonTransitiveLbwExtCommunity());
        EXPECT_TRUE(attrs->getNonTransitiveLbwAsn().has_value());
        EXPECT_TRUE(attrs->getNonTransitiveRawLbwValue().has_value());
        EXPECT_EQ(AsNum(attrs->getNonTransitiveLbwAsn().value()), kLocalAs1);
        EXPECT_EQ(attrs->getNonTransitiveRawLbwValue().value(), 200 << 8);
        break;
      case bgp_policy::LbwExtCommunityActionType::ENCODE_MULTIPATH:
      case bgp_policy::LbwExtCommunityActionType::ENCODE_SWITCH_ID:
        EXPECT_EQ(1, attrs->getExtCommunities()->size());
        EXPECT_TRUE(attrs->hasNonTransitiveLbwExtCommunity());
        EXPECT_TRUE(attrs->getNonTransitiveLbwAsn().has_value());
        EXPECT_TRUE(attrs->getNonTransitiveRawLbwValue().has_value());
        EXPECT_EQ(AsNum(attrs->getNonTransitiveLbwAsn().value()), kLocalAs1);
        /*
         * both encoding actions will update instead of overwrite lbw value
         * kLbw10G in float is 01001110100101010000001011111001b as uint32
         * which is 1318388473; if we replace the 8th - 15th bits with 8
         * then it becomes 1318390009
         */
        EXPECT_EQ(attrs->getNonTransitiveRawLbwValue().value(), 1318390009);
        break;
      case bgp_policy::LbwExtCommunityActionType::
          DECODE_AGGREGATE_CAPACITY_OVERWRITE:
        /*
         * kLbw10G in float is (01001110)(10010101)(00000010)(1111)(1001)b
         * the 8th - 15th bits are 2
         * the ucmp weight is 2
         */
        EXPECT_EQ(1, attrs->getExtCommunities()->size());
        EXPECT_TRUE(attrs->hasNonTransitiveLbwExtCommunity());
        EXPECT_TRUE(attrs->getNonTransitiveLbwAsn().has_value());
        EXPECT_TRUE(attrs->getNonTransitiveLbwValue().has_value());
        EXPECT_EQ(AsNum(attrs->getNonTransitiveLbwAsn().value()), kLocalAs1);
        EXPECT_EQ(attrs->getNonTransitiveLbwValue().value(), 2.0f);
        break;
      default:
        EXPECT_FALSE(true);
    }
  };

  nsf_policy::NsfTeWeightEncoding encoding;
  encoding.l2_encoding() = nsf_policy::NsfL2TeWeightEncoding();
  encoding.l2_encoding()->rack_id() = 4;
  encoding.l2_encoding()->plane_id() = 4;
  encoding.l2_encoding()->remote_rack_capacity() = 8;
  encoding.l2_encoding()->spine_capacity() = 8;
  encoding.l2_encoding()->local_rack_capacity() = 8;

  /*
   * helper function to verify each test case, each test case takes two
   * prefixes: p1, p2. ONLY p2 will match UCMP policy. Veriy that p1 takes
   * "per peer config" while p2 takes "per route config"
   * - advertiseLinkBandwidth: per peer config
   * - lbwPolicyActionType: per route config (policy)
   */
  auto verify = [&](AdvertiseLinkBandwidth advertiseLinkBandwidth,
                    bgp_policy::LbwExtCommunityActionType lbwPolicyActionType) {
    // create term1 without UCMP policy
    auto const match1 = createPrefixMatch(kV4Prefix1);
    auto actionEgp = createBgpPolicyAction(
        BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::EGP);
    auto term1 =
        createBgpPolicyTerm("non-UCMP-term", "", {match1}, {actionEgp});

    // create term2 with UCMP policy
    auto const match2 = createPrefixMatch(kV4Prefix2);
    auto ucmpAction = createBgpPolicyLbwExtCommunityAction(
        lbwPolicyActionType, encoding, 2 /* encodingId */);
    auto term2 = createBgpPolicyTerm("UCMP-term", "", {match2}, {ucmpAction});

    // create policy manager
    const std::string policyName = kEgressPolicyName;
    const auto& policyConfig = createBgpPolicies(policyName, {term1, term2});
    auto policyManager = std::make_shared<PolicyManager>(
        policyConfig, createTestBgpGlobalConfig());

    // create in peer config
    TinyPeerInfo inPeer{
        folly::IPAddress("20.1.1.1"), 1, 1, BgpSessionType::EBGP, false};

    /*
     * create out peer config with ASN,LBW as
     * (kLocalAs1, kLbw5G)
     */
    TinyPeerInfo outPeer{
        folly::IPAddress("30.1.1.1"), 1, 2, BgpSessionType::EBGP, false};
    BgpPeerId outPeerId(outPeer.addr, outPeer.routerId);
    PeeringParams peeringParamsOutPeer{};
    peeringParamsOutPeer.advertiseLinkBandwidth = advertiseLinkBandwidth;
    peeringParamsOutPeer.linkBandwidthBps = kLbw5G;
    peeringParamsOutPeer.peerAddr = outPeer.addr;
    peeringParamsOutPeer.localAs = kLocalAs1;

    auto adjRibOutGroup = std::make_shared<AdjRibOutGroup>(evb_, "Group");
    AdjRib adjRibOutPeer{
        outPeerId,
        peeringParamsOutPeer,
        evb_,
        ribInQ_,
        observerQ_,
        std::make_shared<folly::coro::Baton>(),
        policyManager,
        std::make_shared<std::atomic<bool>>(false) /* isSafeModeOn */,
        std::nullopt, /* ingress policy name */
        policyName /* egress policy name */,
        adjRibOutGroup};
    adjRibOutPeer.isAfiIpv4Negotiated_ = true;
    adjRibOutPeer.pathIdGenerator_ = std::make_unique<PathIdGenerator>(false);

    /*
     * build a BgpAttrs with ASN,LBW as
     * (kRemoteAs2, kLbw10G)
     */
    BgpUpdate2 inputUpdate = buildBgpUpdateAttributes(kV4Nexthop1);
    auto inAttrs = std::make_shared<facebook::bgp::BgpPath>(
        BgpPathFields(*BgpUpdate2toBgpPathC(inputUpdate)));
    BgpAttrExtCommunitiesC extCommunities;
    BgpExtCommunityLinkBandWidthTypeC correctLBW{uint16_t(kRemoteAs2), kLbw10G};
    // get the raw values from correct lbw
    auto [rawHighVal, rawLowVal] = correctLBW.getRawValueInWords();
    /*
     * flip the type from 0x40 to 0x00 so that it is "transitive" now
     * and we treat it as invalid LBW, which will be pruned before
     * advertising
     */
    uint32_t newHighVal =
        (BgpExtCommunityAsSpecificExtTypeC::kBgpExtCommASTransitiveType << 24) +
        (rawHighVal - (correctLBW.getType() << 24));
    BgpAttrExtCommunityC transitiveLBW{newHighVal, rawLowVal};
    extCommunities.emplace_back(correctLBW);
    extCommunities.emplace_back(transitiveLBW);
    inAttrs->setExtCommunities(extCommunities);
    inAttrs->publish();

    {
      // prefix1: no UCMP policy -> apply per peer config
      RibOutAnnouncementEntry update{
          kV4Prefix1,
          kDefaultPathID,
          inPeer,
          inAttrs,
          std::nullopt, // switchId
          std::nullopt, // multiPathSize
          200.f /* agg-received */,
          400.f /* agg-local*/,
          500.f /* rib-policy-lbw*/};
      adjRibOutPeer.processRibAnnouncedEntry(update);
      auto adjRibEntry =
          adjRibOutPeer.getRibEntry(/*ingress=*/false, kV4Prefix1);
      auto outAttrs = adjRibEntry->getPostAttr();
      verifyLbwPerPeerConfig(outAttrs, advertiseLinkBandwidth);
      XLOG(DBG1, "verified per-peer-config prefix");
    }
    {
      // prefix2: has UCMP policy -> apply per route config
      RibOutAnnouncementEntry update{
          kV4Prefix2,
          kDefaultPathID,
          inPeer,
          inAttrs,
          8, // switchId
          8, // multiPathSize
          200.f /* agg-received */,
          400.f /* agg-local*/,
          500.f /* rib-policy-lbw*/};
      adjRibOutPeer.processRibAnnouncedEntry(update);
      auto adjRibEntry =
          adjRibOutPeer.getRibEntry(/*ingress=*/false, kV4Prefix2);
      auto outAttrs = adjRibEntry->getPostAttr();
      verifyLbwPerRouteConfig(outAttrs, lbwPolicyActionType);
      XLOG(DBG1, "verified per-route-config prefix");
    }
  };

  const auto& param = GetParam();
  const auto& advertiseLinkBandwidth = param.advertiseLinkBandwidth;
  const auto& lbwPolicyActionType = param.lbwPolicyActionType;

  verify(advertiseLinkBandwidth, lbwPolicyActionType);
}

/*
 * Verify that a prefix, which is permitted due to policy, and later changes
 * it's attributes and is denied by policy due to attribute changes, is
 * processed properly and notified to peer. i.e. 2nd Rib Announcement leads
 * to AdjRib sending BgpUpdate2 withdrawal
 */
TEST_F(AdjRibOutboundFixture, VerifyDenyAfterPermit) {
  /*
   * Create a policy with two terms
   * Term1 match origin IGP and deny
   * Term2 permit all
   */
  const std::string policyName = kEgressPolicyName;
  auto policyManager = setupDenyIgpOriginAcceptAllPolicy(policyName);
  // IBGP peer
  setupAdjRib(policyManager, policyName);

  fm_->addTask([&] {
    {
      // Announcement 1 which will be permitted by policy
      auto ribMsg = createRibSingleAnnounce(
          kV4Prefix1,
          kV4Nexthop1,
          localPeerV4_,
          true, // send EOR
          BgpAttrOrigin::BGP_ORIGIN_EGP);
      pushRibOutMsgToAdjRib(ribMsg);
    }
    fiberSleepFor(40ms);
    {
      /*
       * Announcement 2 (modified origin) for same prefix will be denied by
       * policy
       */
      auto ribMsg = createRibSingleAnnounce(
          kV4Prefix1,
          kV4Nexthop1,
          localPeerV4_,
          false,
          BgpAttrOrigin::BGP_ORIGIN_IGP);
      pushRibOutMsgToAdjRib(ribMsg);
    }
  });

  fm_->addTask([&] {
    /* Let sendBgpUpdates coro run if scheduled. */
    fiberSleepFor(10ms);
    {
      // Verifying Announcement
      auto msg = folly::coro::blockingWait(popFromEgressQueue());
      ASSERT_TRUE(
          std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
      auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
      ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
      EXPECT_EQ(
          toIPPrefix(kV4Prefix1),
          *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
      EXPECT_EQ(BgpAttrOrigin::BGP_ORIGIN_EGP, *bgpUpdate->attrs()->origin());

      // v4 and v6 EoRs
      msg = folly::coro::blockingWait(popFromEgressQueue());
      ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
      msg = folly::coro::blockingWait(popFromEgressQueue());
      ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));

      // Verify adjrib entry is proper
      auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1);
      ASSERT_NE(nullptr, adjRibEntry->getPreOut());
      ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
      EXPECT_EQ(adjRibEntry->getPreOut(), adjRibEntry->getPostAttr());
      EXPECT_EQ(
          BgpAttrOrigin::BGP_ORIGIN_EGP, adjRibEntry->getPreOut()->getOrigin());

      EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCount());
    }
    {
      /*
       * Verifying withdrawal due to policy denying kV4Prefix1
       * after attribute change Rib announcement
       */
      auto msg = folly::coro::blockingWait(popFromEgressQueue());
      ASSERT_TRUE(
          std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
      auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
      ASSERT_EQ(1, bgpUpdate->v4Withdrawn2()->size());
      EXPECT_EQ(toIPPrefix(kV4Prefix1), *bgpUpdate->v4Withdrawn2()[0].prefix());

      // Verify adjrib entry is proper
      auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1);
      ASSERT_NE(nullptr, adjRibEntry->getPreOut());
      ASSERT_EQ(nullptr, adjRibEntry->getPostAttr());
      EXPECT_EQ(
          BgpAttrOrigin::BGP_ORIGIN_IGP, adjRibEntry->getPreOut()->getOrigin());

      EXPECT_EQ(0, adjRib_->getStats().getPostOutPrefixCount());
    }

    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * Verify that BgpUpdate2 of a prefix is sent only if postOutAttrs
 * contents have changed and not because of shared_ptr changed.
 * This can happen for cases like
 * - Rib announcement received with same values as policy modified
 * attributes from prior announcement
 * - Policy created attributes which are exactly same as received attributes
 */
TEST_F(AdjRibOutboundFixture, DeepComparePostOutAttributesBeforeNotifying) {
  /*
   * Create a policy with one term
   * Term1 match all, set action origin IGP
   */
  const std::string policyName = kEgressPolicyName;
  auto policyManager = setupMatchAllSetOriginIgpPolicy(policyName);
  // IBGP peer
  setupAdjRib(policyManager, policyName);

  fm_->addTask([&] {
    {
      // Announcement 1 which will be permitted by policy
      auto ribMsg = createRibSingleAnnounce(
          kV4Prefix1,
          kV4Nexthop1,
          localPeerV4_,
          true, // send EOR
          BgpAttrOrigin::BGP_ORIGIN_EGP);
      pushRibOutMsgToAdjRib(ribMsg);
    }
    fiberSleepFor(20ms);
    {
      // Announcement 2 for same prefix with IGP origin
      auto ribMsg = createRibSingleAnnounce(
          kV4Prefix1,
          kV4Nexthop1,
          localPeerV4_,
          false,
          BgpAttrOrigin::BGP_ORIGIN_IGP);
      pushRibOutMsgToAdjRib(ribMsg);
    }
  });

  fm_->addTask([&] {
    /* Let sendBgpUpdates coro run if scheduled. */
    fiberSleepFor(10ms);
    // Verifying first kV4Prefix1 update is announced
    auto msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(
        std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
    auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
    EXPECT_EQ(
        toIPPrefix(kV4Prefix1),
        *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
    EXPECT_EQ(BgpAttrOrigin::BGP_ORIGIN_IGP, *bgpUpdate->attrs()->origin());

    // Verify adjrib entry is proper
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1);
    ASSERT_NE(nullptr, adjRibEntry->getPreOut());
    ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
    EXPECT_NE(adjRibEntry->getPreOut(), adjRibEntry->getPostAttr());
    EXPECT_EQ(
        BgpAttrOrigin::BGP_ORIGIN_EGP, adjRibEntry->getPreOut()->getOrigin());
    EXPECT_EQ(
        BgpAttrOrigin::BGP_ORIGIN_IGP, adjRibEntry->getPostAttr()->getOrigin());

    // v4 and v6 EoRs
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));

    /*
     * No new BgpUpdate2 for the announcement 2
     * even though preOut updated (origin changed from EGP to IGP)
     */
    fiberSleepFor(40ms);
    EXPECT_TRUE(boundedAdjRibOutQ_->empty());

    EXPECT_EQ(
        BgpAttrOrigin::BGP_ORIGIN_IGP, adjRibEntry->getPreOut()->getOrigin());
    EXPECT_EQ(
        BgpAttrOrigin::BGP_ORIGIN_IGP, adjRibEntry->getPostAttr()->getOrigin());

    // Verify stats
    EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCount());

    terminateAdjRib();
  });
  evb_.loop();
}

/* Verify we set and clear the egress EoR pending flags on the adjRib. */
TEST_F(AdjRibOutboundFixture, VerifyEgressEoRFlagsResetSessionEstablished) {
  setupAdjRib(
      kLocalAs1,
      kLocalAs1,
      kRemoteAs1,
      false, // isRrClient
      false, // isConfedPeer
      NextHopSelfConfigured(false),
      kV4Nexthop1,
      kV6Nexthop1,
      false, // sessionEstablish
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      V4OverV6Nexthop(true),
      EnhancedRouteRefreshNegotiated(false),
      RouteRefreshNegotiated(false));

  adjRib_->setEgressEoRsPending(true, true);
  adjRib_->egressEoRsSent_ = true;

  fm_->addTask([&] {
    EXPECT_TRUE(adjRib_->egressEoRsPending());
    EXPECT_TRUE(adjRib_->egressEoRsSent_);

    /* sessionEstablished should reset all previous eor state. */
    adjRib_->sessionEstablished(
        std::nullopt /* remoteGrRestartTime */,
        adjRibInQ_,
        boundedAdjRibOutQ_,
        true /* isAfiIpv4Negotiated */,
        true /* isAfiIpv6Negotiated */,
        true /* isV4OverV6Nexthop */,
        false /* isEnhancedRouteRefreshNegotiated */,
        false /* isRouteRefreshNegotiated */,
        std::nullopt /* addPathCapa */);

    EXPECT_FALSE(adjRib_->egressEoRsPending());
    EXPECT_FALSE(adjRib_->egressEoRsSent_);
    terminateAdjRib();
  });

  evb_.loop();
}

TEST_F(AdjRibOutboundFixture, VerifyEgressEoRsPendingSetDuringRibInitialDump) {
  setupAdjRib(
      kLocalAs1,
      kLocalAs1,
      kRemoteAs1,
      false, // isRrClient
      false, // isConfedPeer
      NextHopSelfConfigured(false),
      kV4Nexthop1,
      kV6Nexthop1,
      true, // sessionEstablish
      AfiIpv4Negotiated(false),
      AfiIpv6Negotiated(true),
      V4OverV6Nexthop(false),
      EnhancedRouteRefreshNegotiated(false),
      RouteRefreshNegotiated(false));

  fm_->addTask([&] {
    auto ribMsg1 = createRibSingleAnnounce(
        kV6Prefix1, kV6Nexthop1, eBgpPeer_, false /* sendWithEoR */);
    /* Message needs to be considered part of initial dump to not be ignored. */
    std::get<RibOutAnnouncement>(ribMsg1).initialDump = true;
    auto ribMsg2 = createRibSingleAnnounce(
        kV6Prefix2, kV6Nexthop2, eBgpPeer_, true /* sendWithEoR */);
    pushRibOutMsgToAdjRib(ribMsg1);
    pushRibOutMsgToAdjRib(ribMsg2);
    /*
     * scheduled sendBgpUpdates is not called inline when backpressure
     * is enabled , so we will see egressEoRsPending = true
     * because EoR is not immediately sent.
     */
    EXPECT_TRUE(adjRib_->egressEoRsPending());
  });

  fm_->addTask([&] {
    /* Let sendBgpUpdates coro run if scheduled. */
    fiberSleepFor(10ms);
    /*
     * Two v6 update with one EoR. Order isn't guaranteed so we check
     * number of prefixes and then prefixes and nexthops seen.
     */
    auto msg = folly::coro::blockingWait(popFromEgressQueue());
    auto msg2 = folly::coro::blockingWait(popFromEgressQueue());
    auto msg3 = folly::coro::blockingWait(popFromEgressQueue());

    std::set<network::thrift::IPPrefix> seenPrefixes;
    std::unordered_set<std::string> seenNexthops;

    auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
    seenPrefixes.insert(*bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
    seenNexthops.insert(*bgpUpdate->attrs()->nexthop());

    auto bgpUpdate2 = std::get<std::shared_ptr<const BgpUpdate2>>(*msg2);
    ASSERT_EQ(1, bgpUpdate2->mpAnnounced()->prefixes()->size());
    seenPrefixes.insert(*bgpUpdate2->mpAnnounced()->prefixes()[0].prefix());
    seenNexthops.insert(*bgpUpdate2->attrs()->nexthop());

    EXPECT_TRUE(seenPrefixes.contains(toIPPrefix(kV6Prefix1)));
    EXPECT_TRUE(seenPrefixes.contains(toIPPrefix(kV6Prefix2)));
    EXPECT_TRUE(seenNexthops.contains(kV6Nexthop1.str()));
    EXPECT_TRUE(seenNexthops.contains(kV6Nexthop2.str()));

    EXPECT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg3));
    EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, std::get<BgpEndOfRib>(*msg3).afi());

    // no more msg in the queue
    EXPECT_EQ(0, boundedAdjRibOutQ_->size());
    EXPECT_FALSE(adjRib_->egressEoRsPending());
    EXPECT_TRUE(adjRib_->egressEoRsSent_);

    terminateAdjRib();
  });
  evb_.loop();
}

// Verify we announce prefix properly according to negotiated afi value
TEST_F(AdjRibOutboundFixture, VerifyV4OverV6) {
  /*
   * 1. afi v4 = true, v6 = true => v4 & v6 both enabled, v4OverV6 true
   * => v4 update go through, v6 update go through
   */
  {
    setupAdjRib(
        kLocalAs1,
        kLocalAs1,
        kRemoteAs1,
        false, // isRrClient
        false, // isConfedPeer
        NextHopSelfConfigured(false),
        kV4Nexthop1,
        kV6Nexthop1,
        true, // sessionEstablish
        AfiIpv4Negotiated(true),
        AfiIpv6Negotiated(true),
        V4OverV6Nexthop(true),
        EnhancedRouteRefreshNegotiated(false),
        RouteRefreshNegotiated(false));

    fm_->addTask([&] {
      adjRib_->egressEoRsSent_ = true;
      // v4 update
      auto ribMsg1 =
          createRibSingleAnnounce(kV4Prefix1, kV6Nexthop1, eBgpPeer_, false);
      pushRibOutMsgToAdjRib(ribMsg1);
      // v6 update
      auto ribMsg2 =
          createRibSingleAnnounce(kV6Prefix1, kV6Nexthop1, eBgpPeer_, false);
      pushRibOutMsgToAdjRib(ribMsg2);
    });

    fm_->addTask([&] {
      /* Let sendBgpUpdates coro run if scheduled. */
      fiberSleepFor(10ms);
      /*
       * v4 update and v6 should come through but the ordering isn't guaranteed
       * if AttrToPrefixMap isn't drained on each message.
       */

      std::set<network::thrift::IPPrefix> seenPrefixes;
      std::unordered_set<std::string> seenNexthops;

      auto msg = folly::coro::blockingWait(popFromEgressQueue());
      ASSERT_TRUE(
          std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
      auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
      ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
      seenPrefixes.insert(*bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
      seenNexthops.insert(*bgpUpdate->attrs()->nexthop());

      msg = folly::coro::blockingWait(popFromEgressQueue());
      ASSERT_TRUE(
          std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
      bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
      // Verify that announcement
      ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
      seenPrefixes.insert(*bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
      seenNexthops.insert(*bgpUpdate->attrs()->nexthop());

      EXPECT_TRUE(seenPrefixes.contains(toIPPrefix(kV4Prefix1)));
      EXPECT_TRUE(seenPrefixes.contains(toIPPrefix(kV6Prefix1)));
      EXPECT_TRUE(seenNexthops.contains(kV6Nexthop1.str()));
      EXPECT_EQ(1, seenNexthops.size());

      // no more msg in the queue
      EXPECT_EQ(0, boundedAdjRibOutQ_->size());

      terminateAdjRib();
    });
    evb_.loop();
  }

  /*
   * 2. v4 = true, v6 = false, v4OverV6 false
   * => v4 update with v6 nexthop not go through,
   * => v4 update with v4 nexthop go through
   */
  {
    setupAdjRib(
        kLocalAs1,
        kLocalAs1,
        kRemoteAs1,
        false, // isRrClient
        false, // isConfedPeer
        NextHopSelfConfigured(false),
        kV4Nexthop1,
        kV6Nexthop1,
        true, // sessionEstablish
        AfiIpv4Negotiated(true),
        AfiIpv6Negotiated(false),
        V4OverV6Nexthop(false),
        EnhancedRouteRefreshNegotiated(false),
        RouteRefreshNegotiated(false));
    fm_->addTask([&] {
      adjRib_->egressEoRsSent_ = true;
      // v4 update
      auto ribMsg1 =
          createRibSingleAnnounce(kV4Prefix1, kV6Nexthop1, eBgpPeer_, false);
      pushRibOutMsgToAdjRib(ribMsg1);
      // v6 update
      auto ribMsg2 =
          createRibSingleAnnounce(kV4Prefix2, kV4Nexthop1, eBgpPeer_, false);
      pushRibOutMsgToAdjRib(ribMsg2);
    });

    fm_->addTask([&] {
      /* Let sendBgpUpdates coro run if scheduled. */
      fiberSleepFor(1ms);
      // v4 with v6 nexthop didn't come through

      // v4 update with v4 nexthop come through
      auto msg = folly::coro::blockingWait(popFromEgressQueue());
      ASSERT_TRUE(
          std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
      auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
      // Verify that announcement
      ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
      EXPECT_EQ(
          toIPPrefix(kV4Prefix2),
          *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
      EXPECT_EQ(kV4Nexthop1.str(), *bgpUpdate->attrs()->nexthop());

      // no more msg in the queue
      EXPECT_EQ(0, boundedAdjRibOutQ_->size());

      terminateAdjRib();
    });
    evb_.loop();
  }

  /*
   * 3. v4 = true, v6 = false, v4OverV6 true
   * => v4 update with v4 nexthop should got updated to v6 nexthop
   */
  {
    setupAdjRib(
        kLocalAs1,
        kLocalAs1,
        kRemoteAs1,
        false, // isRrClient
        false, // isConfedPeer
        NextHopSelfConfigured(true),
        kV4Nexthop1,
        kV6Nexthop1,
        true, // sessionEstablish
        AfiIpv4Negotiated(true),
        AfiIpv6Negotiated(false),
        V4OverV6Nexthop(true),
        EnhancedRouteRefreshNegotiated(false),
        RouteRefreshNegotiated(false));
    fm_->addTask([&] {
      adjRib_->egressEoRsSent_ = true;
      // v4 update
      auto ribMsg1 =
          createRibSingleAnnounce(kV4Prefix1, kV4Nexthop1, eBgpPeer_, false);
      pushRibOutMsgToAdjRib(ribMsg1);
    });

    fm_->addTask([&] {
      /* Let sendBgpUpdates coro run if scheduled. */
      fiberSleepFor(10ms);
      // v4 update come through
      auto msg = folly::coro::blockingWait(popFromEgressQueue());
      ASSERT_TRUE(
          std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
      auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
      // Verify that announcement
      ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
      EXPECT_EQ(
          toIPPrefix(kV4Prefix1),
          *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
      EXPECT_EQ(kV6Nexthop1.str(), *bgpUpdate->attrs()->nexthop());

      // no more msg in the queue
      EXPECT_EQ(0, boundedAdjRibOutQ_->size());

      terminateAdjRib();
    });
    evb_.loop();
  }

  /*
   * 4. v4 = true, v6 = false, v4OverV6 false
   * => v4 update with v6 nexthop should got updated to v4 nexthop
   */
  {
    setupAdjRib(
        kLocalAs1,
        kLocalAs1,
        kRemoteAs1,
        false, // isRrClient
        false, // isConfedPeer
        NextHopSelfConfigured(true),
        kV4Nexthop1,
        kV6Nexthop1,
        true, // sessionEstablish
        AfiIpv4Negotiated(true),
        AfiIpv6Negotiated(false),
        V4OverV6Nexthop(false),
        EnhancedRouteRefreshNegotiated(false),
        RouteRefreshNegotiated(false));
    fm_->addTask([&] {
      adjRib_->egressEoRsSent_ = true;
      // v4 update
      auto ribMsg1 =
          createRibSingleAnnounce(kV4Prefix1, kV6Nexthop1, eBgpPeer_, false);
      pushRibOutMsgToAdjRib(ribMsg1);
    });

    fm_->addTask([&] {
      /* Let sendBgpUpdates coro run if scheduled. */
      fiberSleepFor(10ms);
      // v4 update come through
      auto msg = folly::coro::blockingWait(popFromEgressQueue());
      ASSERT_TRUE(
          std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
      auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
      // Verify that announcement
      ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
      EXPECT_EQ(
          toIPPrefix(kV4Prefix1),
          *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
      EXPECT_EQ(kV4Nexthop1.str(), *bgpUpdate->attrs()->nexthop());

      // no more msg in the queue
      EXPECT_EQ(0, boundedAdjRibOutQ_->size());

      terminateAdjRib();
    });
    evb_.loop();
  }
}

// Verify we announce prefix properly according to negotiated afi value
TEST_F(AdjRibOutboundFixture, VerifyAfiNegotiation) {
  /*
   * 1. v4 = false, v6 = false
   * => v4 update not go through, v6 update not go through
   */
  {
    setupAdjRib(
        kLocalAs1,
        kLocalAs1,
        kRemoteAs1,
        false, // isRrClient
        false, // isConfedPeer
        NextHopSelfConfigured(false),
        kV4Nexthop1,
        kV6Nexthop1,
        true, // sessionEstablish
        AfiIpv4Negotiated(false),
        AfiIpv6Negotiated(false));

    fm_->addTask([&] {
      // v4 update
      auto ribMsg1 =
          createRibSingleAnnounce(kV4Prefix1, kV4Nexthop1, eBgpPeer_, true);
      pushRibOutMsgToAdjRib(ribMsg1);
      fiberSleepFor(50ms);
      // v6 update
      auto ribMsg2 =
          createRibSingleAnnounce(kV6Prefix1, kV6Nexthop1, eBgpPeer_, true);
      pushRibOutMsgToAdjRib(ribMsg2);
    });

    fm_->addTask([&] {
      // nothing in the queue
      EXPECT_EQ(0, boundedAdjRibOutQ_->size());
      fiberSleepFor(50ms);
      terminateAdjRib();
    });
    evb_.loop();
  }

  /*
   * 2. v4 = false, v6 = true => v6 enabled
   * => v4 update not go through, v6 update go through
   */
  {
    setupAdjRib(
        kLocalAs1,
        kLocalAs1,
        kRemoteAs1,
        false, // isRrClient
        false, // isConfedPeer
        NextHopSelfConfigured(false),
        kV4Nexthop1,
        kV6Nexthop1,
        true, // sessionEstablish
        AfiIpv4Negotiated(false),
        AfiIpv6Negotiated(true));
    fm_->addTask([&] {
      adjRib_->egressEoRsSent_ = true;
      // v4 update
      auto ribMsg1 =
          createRibSingleAnnounce(kV4Prefix1, kV4Nexthop1, eBgpPeer_, false);
      pushRibOutMsgToAdjRib(ribMsg1);
      fiberSleepFor(50ms);
      // v6 update
      auto ribMsg2 =
          createRibSingleAnnounce(kV6Prefix1, kV6Nexthop1, eBgpPeer_, false);
      pushRibOutMsgToAdjRib(ribMsg2);
    });

    fm_->addTask([&] {
      /* Let sendBgpUpdates coro run if scheduled. */
      fiberSleepFor(10ms);
      // v4 didn't come through

      // v6 update come through
      auto msg = folly::coro::blockingWait(popFromEgressQueue());
      ASSERT_TRUE(
          std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
      auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
      // Verify that announcement
      ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
      EXPECT_EQ(
          toIPPrefix(kV6Prefix1),
          *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
      EXPECT_EQ(kV6Nexthop1.str(), *bgpUpdate->attrs()->nexthop());

      // no more msg in the queue
      EXPECT_EQ(0, boundedAdjRibOutQ_->size());

      terminateAdjRib();
    });
    evb_.loop();
  }

  /*
   * 3. v4 = true, v6 = false => v4 enabled
   * => v4 update go through, v6 update not go through
   */
  {
    setupAdjRib(
        kLocalAs1,
        kLocalAs1,
        kRemoteAs1,
        false, // isRrClient
        false, // isConfedPeer
        NextHopSelfConfigured(false),
        kV4Nexthop1,
        kV6Nexthop1,
        true, // sessionEstablish
        AfiIpv4Negotiated(true),
        AfiIpv6Negotiated(false));

    fm_->addTask([&] {
      adjRib_->egressEoRsSent_ = true;
      // v4 update
      auto ribMsg1 =
          createRibSingleAnnounce(kV4Prefix1, kV4Nexthop1, eBgpPeer_, false);
      pushRibOutMsgToAdjRib(ribMsg1);
      fiberSleepFor(50ms);
      // v6 update
      auto ribMsg2 =
          createRibSingleAnnounce(kV6Prefix1, kV6Nexthop1, eBgpPeer_, false);
      pushRibOutMsgToAdjRib(ribMsg2);
    });

    fm_->addTask([&] {
      /* Let sendBgpUpdates coro run if scheduled. */
      fiberSleepFor(10ms);
      // v4 update come through
      auto msg = folly::coro::blockingWait(popFromEgressQueue());
      ASSERT_TRUE(
          std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
      auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
      // Verify that announcement
      ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
      EXPECT_EQ(
          toIPPrefix(kV4Prefix1),
          *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
      EXPECT_EQ(kV4Nexthop1.str(), *bgpUpdate->attrs()->nexthop());

      /*
       * v6 update didn't come through
       * no more msg in the queue
       */
      EXPECT_EQ(0, boundedAdjRibOutQ_->size());
      fiberSleepFor(50ms);
      terminateAdjRib();
    });
    evb_.loop();
  }

  /*
   * 4. v4 = true, v6 = true => v4 & v6 both enabled
   * => v4 update go through, v6 update go through
   */
  {
    setupAdjRib(
        kLocalAs1,
        kLocalAs1,
        kRemoteAs1,
        false, // isRrClient
        false, // isConfedPeer
        NextHopSelfConfigured(false),
        kV4Nexthop1,
        kV6Nexthop1,
        true, // sessionEstablish
        AfiIpv4Negotiated(true),
        AfiIpv6Negotiated(true));

    fm_->addTask([&] {
      adjRib_->egressEoRsSent_ = true;
      // v4 update
      auto ribMsg1 =
          createRibSingleAnnounce(kV4Prefix1, kV4Nexthop1, eBgpPeer_, false);
      pushRibOutMsgToAdjRib(ribMsg1);
      fiberSleepFor(50ms);
      // v6 update
      auto ribMsg2 =
          createRibSingleAnnounce(kV6Prefix1, kV6Nexthop1, eBgpPeer_, false);
      pushRibOutMsgToAdjRib(ribMsg2);
    });

    fm_->addTask([&] {
      /* Let sendBgpUpdates coro run if scheduled. */
      fiberSleepFor(10ms);
      // v4 update come through
      auto msg = folly::coro::blockingWait(popFromEgressQueue());
      ASSERT_TRUE(
          std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
      auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
      // Verify that announcement
      ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
      EXPECT_EQ(
          toIPPrefix(kV4Prefix1),
          *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
      EXPECT_EQ(kV4Nexthop1.str(), *bgpUpdate->attrs()->nexthop());

      // v6 update come through
      msg = folly::coro::blockingWait(popFromEgressQueue());
      ASSERT_TRUE(
          std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
      bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
      // Verify that announcement
      ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
      EXPECT_EQ(
          toIPPrefix(kV6Prefix1),
          *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
      EXPECT_EQ(kV6Nexthop1.str(), *bgpUpdate->attrs()->nexthop());

      // no more msg in the queue
      EXPECT_EQ(0, boundedAdjRibOutQ_->size());

      terminateAdjRib();
    });
    evb_.loop();
  }
}

class MockScubaData : public rfe::ScubaData {
 public:
  explicit MockScubaData() : ScubaData("") {}

  MOCK_METHOD(
      size_t,
      addSample,
      (const rfe::ScubaDataSample& sample,
       int bucket,
       rfe::ScubaWriteMode writeType,
       scribe::api::thrift::MessageMetadata&& meta),
      (override));
};

/*
 * Verify that a prefix which passes egress policy gets denied by route
 * filter policy
 */
TEST_F(AdjRibOutboundFixture, VerifyRouteFilterPolicyDeny) {
  auto mockScuba = std::make_shared<MockScubaData>();

  EXPECT_CALL(*mockScuba, addSample(_, _, _, _))
      .Times(1)
      .WillOnce(Invoke(
          [&](const auto& sample,
              auto /* unused */,
              auto /* unused */,
              auto /* unused */) -> size_t {
            EXPECT_EQ("rsw001", sample.getNormalValue("device"));
            EXPECT_EQ("rsw.*", sample.getNormalValue("statement"));
            EXPECT_EQ("fsw001", sample.getNormalValue("peer"));
            EXPECT_EQ(
                folly::IPAddress::networkToString(kV4Prefix1),
                sample.getNormalValue("prefix"));
            EXPECT_EQ(0, sample.getIntValue("allow"));
            EXPECT_EQ(0, sample.getIntValue("permissive"));
            EXPECT_EQ(1, sample.getNormVectorValue("communities").size());
            EXPECT_EQ(
                "65530:15800", sample.getNormVectorValue("communities")[0]);
            return 1;
          }));
  auto logger = std::make_unique<ScubaRouteFilterLogger>(
      "rsw001", "rsw.*", "fsw001", mockScuba);

  // create a policy that permits all
  const std::string policyName = kEgressPolicyName;
  auto policyManager = setupAcceptAllPolicy(policyName);
  setupAdjRib(policyManager, policyName);
  fm_->addTask([&] {
    // load adjrib with empty prefix list, blocking every prefix
    auto tStmt = createTRouteFilterStatement({});
    auto [ingressChanged, egressChanged] = adjRib_->setRouteFilterStatement(
        std::make_shared<RouteFilterStatement>(tStmt), std::move(logger));
    EXPECT_FALSE(ingressChanged);
    EXPECT_TRUE(egressChanged);

    auto ribMsg = createRibSingleAnnounce(
        kV4Prefix1,
        kV4Nexthop1,
        localPeerV4_,
        true,
        BgpAttrOrigin::BGP_ORIGIN_EGP);
    pushRibOutMsgToAdjRib(ribMsg);
  });

  fm_->addTask([&] {
    /* Let sendBgpUpdates coro run if scheduled. */
    fiberSleepFor(10ms);
    /*
     * Announcement will not lead to any bgp update but
     * we should see v4 and v6 EoRs
     */
    auto msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    EXPECT_TRUE(boundedAdjRibOutQ_->empty());
    // Verify adjrib entry is proper
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry->getPreOut());
    EXPECT_EQ(nullptr, adjRibEntry->getPostAttr());
    EXPECT_TRUE(adjRibEntry->getPostOutPolicy());
    EXPECT_EQ("Denied by CRF", *adjRibEntry->getPostOutPolicy());
    // Verify stats
    EXPECT_EQ(0, adjRib_->getStats().getPostOutPrefixCount());

    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * Verify that a prefix which passes egress policy also passes route filter
 * policy in permissive mode
 */
TEST_F(AdjRibOutboundFixture, VerifyRouteFilterPolicyPermissiveAllow) {
  auto mockScuba = std::make_shared<MockScubaData>();

  EXPECT_CALL(*mockScuba, addSample(_, _, _, _))
      .Times(1)
      .WillOnce(Invoke(
          [&](const auto& sample,
              auto /* unused */,
              auto /* unused */,
              auto /* unused */) -> size_t {
            EXPECT_EQ("rsw001", sample.getNormalValue("device"));
            EXPECT_EQ("rsw.*", sample.getNormalValue("statement"));
            EXPECT_EQ("fsw001", sample.getNormalValue("peer"));
            EXPECT_EQ(
                folly::IPAddress::networkToString(kV4Prefix1),
                sample.getNormalValue("prefix"));
            EXPECT_EQ(1, sample.getIntValue("allow"));
            EXPECT_EQ(1, sample.getIntValue("permissive"));
            EXPECT_EQ(1, sample.getNormVectorValue("communities").size());
            EXPECT_EQ(
                "65530:15800", sample.getNormVectorValue("communities")[0]);
            return 1;
          }));
  auto logger = std::make_unique<ScubaRouteFilterLogger>(
      "rsw001", "rsw.*", "fsw001", mockScuba);

  // create a policy that permits all
  const std::string policyName = kEgressPolicyName;
  auto policyManager = setupAcceptAllPolicy(policyName);
  setupAdjRib(policyManager, policyName);
  fm_->addTask([&] {
    /*
     * load adjrib with empty prefix list, blocking every prefix, but in
     * permissive mode
     */
    auto tStmt = createTRouteFilterStatement({}, true /* permissive */);
    auto [ingressChanged, egressChanged] = adjRib_->setRouteFilterStatement(
        std::make_shared<RouteFilterStatement>(tStmt), std::move(logger));
    EXPECT_FALSE(ingressChanged);
    EXPECT_TRUE(egressChanged);

    auto ribMsg = createRibSingleAnnounce(
        kV4Prefix1,
        kV4Nexthop1,
        localPeerV4_,
        true,
        BgpAttrOrigin::BGP_ORIGIN_EGP);
    pushRibOutMsgToAdjRib(ribMsg);
  });

  fm_->addTask([&] {
    /* Let sendBgpUpdates coro run if scheduled. */
    fiberSleepFor(10ms);
    // Verifying Announcement
    auto msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(
        std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
    auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
    EXPECT_EQ(
        toIPPrefix(kV4Prefix1),
        *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
    EXPECT_EQ(BgpAttrOrigin::BGP_ORIGIN_EGP, *bgpUpdate->attrs()->origin());

    // v4 and v6 EoRs
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));
    msg = folly::coro::blockingWait(popFromEgressQueue());
    ASSERT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg));

    // Verify adjrib entry is proper
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1);
    ASSERT_NE(nullptr, adjRibEntry->getPreOut());
    ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
    EXPECT_EQ(adjRibEntry->getPreOut(), adjRibEntry->getPostAttr());
    EXPECT_EQ(
        BgpAttrOrigin::BGP_ORIGIN_EGP, adjRibEntry->getPreOut()->getOrigin());

    EXPECT_EQ(1, adjRib_->getStats().getPostOutPrefixCount());

    terminateAdjRib();
  });
  evb_.loop();
}

TEST_F(AdjRibOutboundFixture, SetRouteFilterStatementTest) {
  // create a policy that permits all
  const std::string policyName = kEgressPolicyName;
  auto policyManager = setupAcceptAllPolicy(policyName);
  setupAdjRib(policyManager, policyName, false /* establish session */);

  // simulate rf policy in Rib
  rib_policy::TRouteFilterPolicy tPolicy;
  tPolicy.statements()->emplace("stmt1", createTRouteFilterStatement({}));
  auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);

  // nullptr -> nullptr (should return (false, false), no change)
  auto [ingressChanged1, egressChanged1] =
      adjRib_->setRouteFilterStatement(nullptr);
  EXPECT_FALSE(ingressChanged1);
  EXPECT_FALSE(egressChanged1);

  // nullptr -> stmt1 (should return (false, true), statement set egress filter)
  auto [ingressChanged2, egressChanged2] =
      adjRib_->setRouteFilterStatement(policy->getStatements().at("stmt1"));
  EXPECT_FALSE(ingressChanged2);
  EXPECT_TRUE(egressChanged2);

  // stmt1 -> stmt1 (should return (false, false), same statement)
  auto [ingressChanged3, egressChanged3] =
      adjRib_->setRouteFilterStatement(policy->getStatements().at("stmt1"));
  EXPECT_FALSE(ingressChanged3);
  EXPECT_FALSE(egressChanged3);

  tPolicy.statements()->at("stmt1") =
      createTRouteFilterStatement({}, true /* permissive */);
  policy = std::make_unique<RouteFilterPolicy>(tPolicy);
  // stmt1 -> stmt1' (should return (false, true), different egress statement)
  auto [ingressChanged4, egressChanged4] =
      adjRib_->setRouteFilterStatement(policy->getStatements().at("stmt1"));
  EXPECT_FALSE(ingressChanged4);
  EXPECT_TRUE(egressChanged4);

  // simulate rf policy being purged in Rib
  policy.reset();

  // verify statement is still valid in adjrib
  EXPECT_NE(nullptr, adjRib_->routeFilterStmt_);
  auto tStmt = adjRib_->routeFilterStmt_->toThrift();

  // stmt1' -> nullptr (should return (false, true), removing egress statement)
  auto [ingressChanged5, egressChanged5] =
      adjRib_->setRouteFilterStatement(nullptr);
  EXPECT_FALSE(ingressChanged5);
  EXPECT_TRUE(egressChanged5);
}

/*
 * Unit test the function processRibMessage
 * 1. process RibOutAnnouncement
 * 2. process RibOutWithdrawal
 */
TEST_F(AdjRibOutboundFixture, ProcessRibMessageTest) {
  // Subscribe to the root
  auto& messages = subscribeToLogMessages("");

  setupAdjRibForOutUnitTest();

  // EOR not sent, rib messages will be ignored
  adjRib_->egressEoRsSent_ = false;

  BgpUpdate2 update = buildBgpUpdateAttributes(kV4Nexthop1);
  auto attrs = std::make_shared<facebook::bgp::BgpPath>(
      BgpPathFields(*BgpUpdate2toBgpPathC(update)));

  PrefixSet announcePrefixes = {std::make_pair(kV4Prefix1, 0)};
  PrefixSet withdrawPrefixes = {std::make_pair(kV4Prefix2, 0)};

  auto announceKey = BgpPathWithAfi{attrs, BgpUpdateAfi::AFI_IPv4};
  auto withdrawKey = BgpPathWithAfi{nullptr, BgpUpdateAfi::AFI_IPv4};
  adjRib_->attrToPrefixMap_.emplace(announceKey, announcePrefixes);
  adjRib_->attrToPrefixMap_.emplace(withdrawKey, withdrawPrefixes);

  // process RibOutAnnouncement
  {
    messages.clear();

    adjRib_->processRibMessage(RibOutAnnouncement{});

    EXPECT_EQ(1, messages.size());
    EXPECT_TRUE(
        messages[0].first.getMessage().starts_with(
            "Ignoring Rib announcement"));
  }

  // process RibOutWithdrawal
  {
    messages.clear();

    adjRib_->processRibMessage(RibOutWithdrawal{});

    EXPECT_EQ(1, messages.size());
    EXPECT_TRUE(
        messages[0].first.getMessage().starts_with("Ignoring Rib withdrawal"));
  }
}

/*
 * Unit test the function processRibWithdraw
 * 1. AFI not negotiated: ignored
 * 2. something in deferred updates, but not in adjRibOutLiteTree_:
 *    the entry in deferredUpdates_ is removed
 * 3. the added entry does not have a pre out: nothing to withdraw
 * 4. the added entry has a pre out: Test tryInsertWithdrawal with/without
 *    post out
 */
TEST_F(AdjRibOutboundFixture, ProcessRibWithdrawTest) {
  auto& messages = subscribeToLogMessages("", folly::LogLevel::DBG4);

  setupAdjRibForOutUnitTest();

  // AFI not negotiated
  adjRib_->isAfiIpv4Negotiated_ = false;
  {
    messages.clear();

    adjRib_->processRibWithdraw(kV4Prefix1, 0);

    EXPECT_EQ(1, messages.size());
    EXPECT_TRUE(
        messages[0].first.getMessage().starts_with(
            "Ignore RibWithdrawal of prefix"));
  }

  // something in deferred updates, but not in adjRibOutLiteTree_
  adjRib_->isAfiIpv4Negotiated_ = true;

  // create an entry in deferredUpdates_
  auto ribMsg = std::get<RibOutAnnouncement>(
      createRibSingleAnnounce(kV4Prefix1, kV4Nexthop1, localPeerV4_, true));
  auto& update = ribMsg.entries[0];
  adjRib_->deferredUpdates_.emplace(kV4Prefix1, update);
  {
    messages.clear();

    adjRib_->processRibWithdraw(kV4Prefix1, 0);

    EXPECT_EQ(2, messages.size());
    EXPECT_TRUE(
        messages[0].first.getMessage().starts_with(
            "Removed deferred entry of prefix"));
    EXPECT_TRUE(
        messages[1].first.getMessage().starts_with(
            "Received withdraw of prefix"));

    // the entry in deferredUpdates_ is removed
    EXPECT_TRUE(adjRib_->deferredUpdates_.empty());
  }

  // add ribMsg to adjRibOutLiteTree_ with path id 0
  adjRib_->pathIdGenerator_ = std::make_unique<PathIdGenerator>(
      false); // TODO: deprecate this once ADD-PATH changes are finished
  adjRib_->processRibAnnouncedEntry(update);

  auto adjRibEntry = adjRib_->adjRibOutGroup_->getFromLiteTree(
      adjRib_->adjRibOutGroup_->LiteTree_,
      update.prefix,
      adjRib_->getPeerOwnerKey());
  adjRibEntry->setPreOut(nullptr);

  // the added entry does not have a pre out
  {
    messages.clear();

    adjRib_->processRibWithdraw(kV4Prefix1, 0);

    EXPECT_EQ(1, messages.size());
    EXPECT_TRUE(
        messages[0].first.getMessage().starts_with(
            "Received withdraw of prefix"));
  }

  adjRibEntry->setPreOut(update.attrs);
  adjRibEntry->setPostAttr(nullptr);

  // no post out
  {
    messages.clear();

    adjRib_->processRibWithdraw(kV4Prefix1, 0);

    EXPECT_EQ(1, messages.size());
    EXPECT_TRUE(
        messages[0].first.getMessage().starts_with(
            "Ignoring Rib withdraw of prefix"));
  }

  // reset the path id
  adjRib_->pathIdGenerator_ = std::make_unique<PathIdGenerator>(false);
  // reinstate the prefix again
  adjRib_->processRibAnnouncedEntry(update);

  // has post out, withdraw the prefix
  {
    messages.clear();

    auto sentPrefixCount = adjRib_->stats_.getPostOutPrefixCount();
    EXPECT_GT(sentPrefixCount, 1);

    adjRib_->processRibWithdraw(kV4Prefix1, 0);

    EXPECT_EQ(1, messages.size());
    EXPECT_TRUE(
        messages[0].first.getMessage().starts_with("Withdrawing prefix"));

    EXPECT_EQ(1, adjRib_->attrToPrefixMap_.size());

    auto attrWithAfi = adjRib_->attrToPrefixMap_.begin()->first;
    EXPECT_FALSE(attrWithAfi.attrs);
    EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, attrWithAfi.afi);
    EXPECT_EQ(1, adjRib_->attrToPrefixMap_.begin()->second.size());

    EXPECT_EQ(adjRib_->stats_.getPostOutPrefixCount(), sentPrefixCount - 1);
  }

  // reset the path id
  adjRib_->pathIdGenerator_ = std::make_unique<PathIdGenerator>(false);
  // reinstate the prefix again
  adjRib_->processRibAnnouncedEntry(update);
  // clean up prefix count
  while (adjRib_->stats_.getPostOutPrefixCount() > 0) {
    adjRib_->stats_.decrementPostOutPrefixCount(kV4Prefix1.first.isV4());
  }

  /*
   * withdraw the prefix but the prefix count is 0
   * this would result in additional error message
   */
  {
    messages.clear();
    EXPECT_EQ(adjRib_->stats_.getPostOutPrefixCount(), 0);

    adjRib_->processRibWithdraw(kV4Prefix1, 0);

    /*
     * decrementPostOutPrefixCount returns early on the postOutPrefixCount == 0
     * underflow path, so it does not reach the totalSentPrefixCount decrement
     * (keeping per-container and global counters from diverging). Hence only
     * the postOutPrefixCount underflow is logged, not a totalSentPrefixCount
     * underflow.
     */
    EXPECT_EQ(3, messages.size());
    EXPECT_TRUE(
        messages[0].first.getMessage().starts_with(
            "Invalid sent prefix count"));
    EXPECT_TRUE(
        messages[1].first.getMessage().starts_with(
            "postOutPrefixCount underflow"));
    EXPECT_TRUE(
        messages[2].first.getMessage().starts_with("Withdrawing prefix"));
  }
}

/*
 * Unit test the function tryInsertWithdrawal
 * 1. Post out is nullptr: not inserted
 * 2. Post out is not nullptr: inserted
 */
TEST_F(AdjRibOutboundFixture, TryInsertWithdrawalTest) {
  auto& messages = subscribeToLogMessages("", folly::LogLevel::DBG3);

  setupAdjRibForOutUnitTest();

  AdjRibEntry adjRibEntry(100);

  // not inserted
  adjRibEntry.setPostAttr(nullptr);

  {
    messages.clear();

    adjRib_->tryInsertWithdrawal(
        kV4Prefix1, &adjRibEntry, "Dummy inserted", "Dummy not inserted");

    EXPECT_EQ(1, messages.size());
    EXPECT_EQ(messages[0].first.getMessage(), "Dummy not inserted");
  }

  // inserted
  BgpUpdate2 update = buildBgpUpdateAttributes(kV4Nexthop1);
  auto attrs = std::make_shared<facebook::bgp::BgpPath>(
      BgpPathFields(*BgpUpdate2toBgpPathC(update)));
  EXPECT_EQ(0, adjRib_->stats_.getPostOutPrefixCount());
  adjRibEntry.setPostAttr(attrs);

  {
    messages.clear();

    adjRib_->tryInsertWithdrawal(
        kV4Prefix1, &adjRibEntry, "Dummy inserted", "Dummy not inserted");

    /*
     * stats_.getPostOutPrefixCount == 0 is invalid. decrementPostOutPrefixCount
     * returns early on this underflow path, so it does not reach the
     * totalSentPrefixCount decrement -- only the postOutPrefixCount underflow
     * is logged.
     */
    EXPECT_EQ(3, messages.size());
    EXPECT_TRUE(
        messages[0].first.getMessage().starts_with(
            "Invalid sent prefix count"));
    EXPECT_TRUE(
        messages[1].first.getMessage().starts_with(
            "postOutPrefixCount underflow"));
    EXPECT_EQ(messages[2].first.getMessage(), "Dummy inserted");
  }
}

/*
 * Unit test the function tryInsertRibOutEntry, 3 scenarios:
 * 1. inserting a new entry that does not exist yet: insert a new entry
 * 2. trying to insert the same prefix with different nexthop: insert a new
 *    entry
 * 3. trying to insert the existing entry: return the existing entry
 */
TEST_F(AdjRibOutboundFixture, TryInsertRibOutEntryTest) {
  auto& messages = subscribeToLogMessages("", folly::LogLevel::DBG4);

  setupAdjRibForOutUnitTest();
  adjRib_->sendAddPath_ = true;
  // set the sendAddPath to true to generate different path ID
  adjRib_->pathIdGenerator_ = std::make_unique<PathIdGenerator>(true);

  const RibOutAnnouncementEntry updateNexthop1(
      kV4Prefix1,
      kDefaultPathID,
      eBgpPeer_,
      std::make_shared<facebook::bgp::BgpPath>(BgpPathFields(
          *BgpUpdate2toBgpPathC(buildBgpUpdateAttributes(kNextHopV4_1)))));
  const RibOutAnnouncementEntry updateNexthop2(
      kV4Prefix1,
      kDefaultPathID,
      eBgpPeer_,
      std::make_shared<facebook::bgp::BgpPath>(BgpPathFields(
          *BgpUpdate2toBgpPathC(buildBgpUpdateAttributes(kNextHopV4_2)))));

  // try inserting a new entry that does not exist yet
  messages.clear();

  auto adjRibEntry1 = adjRib_->tryInsertRibOutEntry(updateNexthop1);

  EXPECT_EQ(1, messages.size());
  EXPECT_TRUE(
      messages[0].first.getMessage().starts_with("Learning new prefix"));

  EXPECT_NE(adjRibEntry1, nullptr);

  /*
   * try inserting the same prefix with different nexthop
   * the two entries do not share the same path id
   */
  messages.clear();

  auto adjRibEntry2 = adjRib_->tryInsertRibOutEntry(updateNexthop2);

  EXPECT_EQ(1, messages.size());
  EXPECT_TRUE(
      messages[0].first.getMessage().starts_with("Learning new prefix"));

  EXPECT_NE(adjRibEntry1, adjRibEntry2);
  EXPECT_NE(adjRibEntry1->getPathId(), adjRibEntry2->getPathId());

  // try inserting the existing entry
  messages.clear();

  auto adjRibEntry3 = adjRib_->tryInsertRibOutEntry(updateNexthop1);

  EXPECT_EQ(1, messages.size());
  EXPECT_TRUE(
      messages[0].first.getMessage().starts_with("Updating preOut attributes"));

  // return the same entry
  EXPECT_EQ(adjRibEntry1, adjRibEntry3);
}

/*
 * This test checks the policy evaluation process along with cache hit case.
 * To test the cache key storage, this test deliberately builds
 *  - a policy term to match and accept everything
 *  - a policy term to modify the communities
 *
 * The test makes sure that:
 *  - cache is NOT hit for the first time when evaluated against policy
 *  - cache is hit in the second time when the same attr + prefix was evaluated
 */
TEST_F(AdjRibOutboundFixture, PolicyCacheEvaluationTest) {
  setupAdjRibForOutUnitTest();

  std::string updatePeerIdStr;

  // create some entries
  auto ribMsg = std::get<RibOutAnnouncement>(
      createRibSingleAnnounce(kV4Prefix1, kV4Nexthop1, localPeerV4_, true));
  auto& update = ribMsg.entries[0];
  AdjRibEntry adjRibEntry(100);
  auto prePolicyAttrs = std::make_shared<facebook::bgp::BgpPath>(BgpPathFields(
      *BgpUpdate2toBgpPathC(buildBgpUpdateAttributes(kV4Nexthop1))));

  /*
   * NOTE: must clone this. Otherwise, prePolicyAttrs can be changed with
   * shared_ptr access.
   */
  auto prePolicyAttrsClone = prePolicyAttrs->clone();

  const std::string policyName = kEgressPolicyName;
  const std::string postOutPolicy =
      fmt::format("Accepted/Modified by {} term term1", policyName);
  auto policyManager = setupMatchAllSetCommunityPolicy(policyName);
  setupAdjRib(policyManager, policyName, false);

  EXPECT_NE(adjRib_->policyCache_, nullptr);

  {
    // First time evaluate policy, no cache hit. Store key in cache.
    auto postPolicyAttrs = adjRib_->getPostOutPolicyAttributes(
        update, &adjRibEntry, prePolicyAttrs, updatePeerIdStr);

    EXPECT_NE(postPolicyAttrs, nullptr);
    EXPECT_TRUE(adjRibEntry.getPostOutPolicy());
    EXPECT_EQ(*adjRibEntry.getPostOutPolicy(), postOutPolicy);
    EXPECT_EQ(0, adjRib_->policyCache_->getTotalCacheHit());
    EXPECT_EQ(1, adjRib_->policyCache_->getTotalCacheMiss());
    EXPECT_GE(adjRib_->policyCache_->size(), 0);
    EXPECT_GE(adjRib_->policyCache_->getCacheMemUsage(), 0);
  }

  {
    // Second time evaluate policy. Expect cache hit.
    auto postPolicyAttrs = adjRib_->getPostOutPolicyAttributes(
        update, &adjRibEntry, prePolicyAttrsClone, updatePeerIdStr);

    EXPECT_NE(postPolicyAttrs, nullptr);
    EXPECT_TRUE(adjRibEntry.getPostOutPolicy());
    EXPECT_EQ(*adjRibEntry.getPostOutPolicy(), postOutPolicy);
    EXPECT_EQ(1, adjRib_->policyCache_->getTotalCacheHit());
    EXPECT_EQ(1, adjRib_->policyCache_->getTotalCacheMiss());
    EXPECT_GE(adjRib_->policyCache_->size(), 0);
    EXPECT_GE(adjRib_->policyCache_->getCacheMemUsage(), 0);
  }
}

/*
 * Unit test the functiona getPostOutPolicyAttributes and
 * getPostOutPolicyAttributesAndInfo there are many scenarios we should cover:
 * Case 1: adjRibEntry must not be nullptr
 * Case 2: Adj rib without egress policy
 * Case 3: Adj rib with egress policy, but it has no policyCache_
 *   Case 3.1: prefix is accepted, postOutPolicy is set to "Accepted/Modified"
 *   Case 3.2: prefix is denied, postOutPolicy is set to "Denied"
 *   Case 3.3: prefix is not found, postOutPolicy is default denied
 * Case 4: Adj rib has policy cache, and we hit the cache
 * Case 5: Blocked by Centralized Route Filtering (CRF)
 */
TEST_F(AdjRibOutboundFixture, GetPostOutPolicyAttributesTest) {
  auto& messages = subscribeToLogMessages("", folly::LogLevel::DBG5);

  setupAdjRibForOutUnitTest();

  // create some entries
  auto ribMsg = std::get<RibOutAnnouncement>(
      createRibSingleAnnounce(kV4Prefix1, kV4Nexthop1, localPeerV4_, true));
  auto& update = ribMsg.entries[0];
  AdjRibEntry adjRibEntry(100);
  auto prePolicyAttrs = std::make_shared<facebook::bgp::BgpPath>(BgpPathFields(
      *BgpUpdate2toBgpPathC(buildBgpUpdateAttributes(kV4Nexthop1))));
  std::string updatePeerIdStr = "[UPDATE] ";

  /*
   * Case 1:
   *   adjRibEntry must not be nullptr
   */
  {
    std::unique_ptr<AdjRibEntry> uniqueNullAdjRibEntry{nullptr};
    auto nullAdjRibEntry = uniqueNullAdjRibEntry.get();
    EXPECT_DEATH(
        adjRib_->getPostOutPolicyAttributes(
            update, nullAdjRibEntry, prePolicyAttrs, updatePeerIdStr),
        "");
  }

  // Case 2: dummy adj rib without egress policy
  {
    EXPECT_EQ(adjRib_->egressPolicyName_, std::nullopt);
    // no route filter statement set
    EXPECT_EQ(adjRib_->routeFilterStmt_, nullptr);

    auto postPolicyAttrs = adjRib_->getPostOutPolicyAttributes(
        update, &adjRibEntry, prePolicyAttrs, updatePeerIdStr);

    // post policy attributes is cloned from pre policy attributes
    EXPECT_EQ(*prePolicyAttrs, *postPolicyAttrs);
  }

  const std::string policyName = kEgressPolicyName;
  /*
   * Create a policy with three terms
   *
   * Term1 match kV4Prefix1, kV4Prefix2 and apply origin action (EGP) & as
   * path overwrite action as_path_overwrite_list set to {0, 0}, AdjRib will
   * override 0 asns based on ingress or egress routes;
   *
   * Term2 match kV4Prefix3 and discard
   *
   * Term3 match kV4Prefix4 and PERMIT (do not modify any attributes)
   */
  auto policyManager = setup3TermPolicy(policyName);

  // for unit test, we don't need the session being established
  setupAdjRib(policyManager, policyName, false);

  // Case 3: adjRib_ has egress policy, but it has no policyCache_
  std::shared_ptr<AdjRibPolicyCache> policyCachePtr{nullptr};
  std::swap(adjRib_->policyCache_, policyCachePtr);
  /*
   * Case 3.1: prefix is accepted, postOutPolicy is set to
   * "Accepted/Modified"
   */
  {
    messages.clear();

    EXPECT_EQ(adjRib_->policyCache_, nullptr);

    auto postPolicyAttrs = adjRib_->getPostOutPolicyAttributes(
        update, &adjRibEntry, prePolicyAttrs, updatePeerIdStr);

    EXPECT_NE(postPolicyAttrs, nullptr);

    EXPECT_GT(messages.size(), 1);
    EXPECT_TRUE(
        messages[0].first.getMessage().starts_with("Policy Cache Miss"));

    // PostOutPolicy is set to "Accepted/Modified"
    EXPECT_TRUE(adjRibEntry.getPostOutPolicy());
    EXPECT_TRUE(
        adjRibEntry.getPostOutPolicy()->starts_with("Accepted/Modified by"));
  }
  // Case 3.2: prefix is denied, postOutPolicy is set to "Denied"
  {
    AdjRibEntry adjRibEntryDenied(20);
    auto ribMsgDenied = std::get<RibOutAnnouncement>(
        createRibSingleAnnounce(kV4Prefix3, kV4Nexthop1, localPeerV4_, true));
    auto& updateDenied = ribMsgDenied.entries[0];

    auto postPolicyAttrs = adjRib_->getPostOutPolicyAttributes(
        updateDenied, &adjRibEntryDenied, prePolicyAttrs, updatePeerIdStr);

    // post policy is nullptr as it is denied
    EXPECT_EQ(postPolicyAttrs, nullptr);

    // PostOutPolicy is set to "Denied"
    EXPECT_TRUE(adjRibEntryDenied.getPostOutPolicy());
    EXPECT_TRUE(adjRibEntryDenied.getPostOutPolicy()->starts_with("Denied by"));
  }
  // Case 3.3: prefix is not found, postOutPolicy is default denied
  {
    AdjRibEntry adjRibEntryNotFound(0);
    auto ribMsgNotFound = std::get<RibOutAnnouncement>(
        createRibSingleAnnounce(kV4Prefix5, kV4Nexthop1, localPeerV4_, true));
    auto& updateNotFound = ribMsgNotFound.entries[0];

    auto postPolicyAttrs = adjRib_->getPostOutPolicyAttributes(
        updateNotFound, &adjRibEntryNotFound, prePolicyAttrs, updatePeerIdStr);

    // post policy is nullptr as it is denied
    EXPECT_EQ(postPolicyAttrs, nullptr);

    // PostOutPolicy is set to "Default denied"
    EXPECT_TRUE(adjRibEntryNotFound.getPostOutPolicy());
    EXPECT_TRUE(
        adjRibEntryNotFound.getPostOutPolicy()->ends_with("Default denied"));
  }

  // Case 4: Adj rib has policy cache, and we hit the cache
  std::swap(adjRib_->policyCache_, policyCachePtr);

  {
    // Check the adjRibEntry twice
    auto postPolicyAttrs1 = adjRib_->getPostOutPolicyAttributes(
        update, &adjRibEntry, prePolicyAttrs, updatePeerIdStr);

    // First time, no cache hit. Store result into cache.
    EXPECT_EQ(0, adjRib_->policyCache_->getTotalCacheHit());
    EXPECT_EQ(1, adjRib_->policyCache_->getTotalCacheMiss());

    messages.clear();

    auto postPolicyAttrs2 = adjRib_->getPostOutPolicyAttributes(
        update, &adjRibEntry, prePolicyAttrs, updatePeerIdStr);

    // Second time, cache hit.
    EXPECT_EQ(1, adjRib_->policyCache_->getTotalCacheHit());
    EXPECT_EQ(1, adjRib_->policyCache_->getTotalCacheMiss());

    EXPECT_EQ(2, messages.size());
    EXPECT_TRUE(messages[0].first.getMessage().starts_with("Searching"));
    EXPECT_TRUE(messages[1].first.getMessage().starts_with("Policy Cache Hit"));
  }

  /*
   * Case 5: Blocked by Centralized Route Filtering (CRF)
   * the statement that blocks kV4Prefix1
   */
  rib_policy::TRouteFilterStatement tStmt = createTRouteFilterStatement({});
  adjRib_->routeFilterStmt_ =
      std::make_shared<const RouteFilterStatement>(tStmt);
  {
    auto postPolicyAttrs = adjRib_->getPostOutPolicyAttributes(
        update, &adjRibEntry, prePolicyAttrs, updatePeerIdStr);

    // Third time, cache hit.
    EXPECT_EQ(2, adjRib_->policyCache_->getTotalCacheHit());
    EXPECT_EQ(1, adjRib_->policyCache_->getTotalCacheMiss());

    EXPECT_EQ(postPolicyAttrs, nullptr);
    EXPECT_TRUE(adjRibEntry.getPostOutPolicy());
    EXPECT_EQ(*adjRibEntry.getPostOutPolicy(), "Denied by CRF");
  }
}

TEST_F(AdjRibOutboundFixture, GetPostOutPolicyAttributesAndInfoTest) {
  setupAdjRibForOutUnitTest();

  std::string updatePeerIdStr = "[UPDATE] ";

  // Create a policy with one term which matches all and sets Med
  const std::string policyName = kEgressPolicyName;
  auto policyManager = setupMatchAllSetMedPolicy(policyName);

  // Set up AdjRib with the policy
  setupAdjRib(policyManager, policyName, false);

  // create some entries
  auto ribMsg = std::get<RibOutAnnouncement>(
      createRibSingleAnnounce(kV4Prefix1, kV4Nexthop1, localPeerV4_, true));
  auto& update = ribMsg.entries[0];
  AdjRibEntry adjRibEntry(100);

  // Create a BGP path with attributes
  auto prePolicyAttrs = std::make_shared<facebook::bgp::BgpPath>(BgpPathFields(
      *BgpUpdate2toBgpPathC(buildBgpUpdateAttributes(kV4Nexthop1))));

  // Call the function under test
  auto [postPolicyAttrs, postPolicyInfo] =
      adjRib_->getPostOutPolicyAttributesAndInfo(
          update, &adjRibEntry, prePolicyAttrs, updatePeerIdStr);

  // Verify the result
  EXPECT_NE(nullptr, postPolicyAttrs);
  EXPECT_TRUE(postPolicyInfo.isMedSetByPolicy);

  // Verify attributes were modified
  EXPECT_EQ(postPolicyAttrs->getMed(), kMed);
}

/*
 * Unit test the function tryDeleteRibOutEntry
 * If any pre/post of of the adjRibEntry is set, don't remove the entry from
 * adjRibOutLiteTree_, otherwise, remove the entry from adjRibOutLiteTree_
 */
TEST_F(AdjRibOutboundFixture, TryDeleteRibOutEntryTest) {
  setupAdjRibForOutUnitTest();

  auto adjRibEntry = std::make_unique<AdjRibEntry>(100);
  auto attrs = std::make_shared<facebook::bgp::BgpPath>(BgpPathFields(
      *BgpUpdate2toBgpPathC(buildBgpUpdateAttributes(kV4Nexthop1))));

  // insert an entry
  adjRib_->pathIdGenerator_ = std::make_unique<PathIdGenerator>(false);
  adjRib_->tryInsertRibOutEntry(
      RibOutAnnouncementEntry(kV4Prefix1, kDefaultPathID, eBgpPeer_, attrs));
  EXPECT_EQ(
      1,
      adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/false));

  // pre/post out is set for adjRibEntry, no deletion
  {
    adjRibEntry->setPreOut(attrs);
    adjRibEntry->setPostAttr(attrs);

    adjRib_->tryDeleteRibOutEntry(kV4Prefix1, adjRibEntry.get(), 99);

    // nothing removed
    EXPECT_EQ(
        1,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/false));
  }
  {
    adjRibEntry->setPreOut(attrs);
    adjRibEntry->setPostAttr(nullptr);

    adjRib_->tryDeleteRibOutEntry(kV4Prefix1, adjRibEntry.get(), 99);

    // nothing removed
    EXPECT_EQ(
        1,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/false));
  }
  {
    adjRibEntry->setPreOut(nullptr);
    adjRibEntry->setPostAttr(attrs);

    adjRib_->tryDeleteRibOutEntry(kV4Prefix1, adjRibEntry.get(), 99);

    // nothing removed
    EXPECT_EQ(
        1,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/false));
  }

  /*
   * remove the entry if both pre/post out are not set
   * i.e., the rib entry is no longer in use for output
   */
  {
    adjRibEntry->setPreOut(nullptr);
    adjRibEntry->setPostAttr(nullptr);

    adjRib_->tryDeleteRibOutEntry(kV4Prefix1, adjRibEntry.get(), 99);

    // entry removed because path id value does not matter
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/false));
  }
}

/*
 *  Repeat same test as above but this time with add path enabled
 *  TryDeleteRibOutEntry with various conditions
 *  Should not delete when
 *  - post attr is not null
 *  - pre attr is not null
 *  - pathid supplied is of different value
 *
 */
TEST_F(AdjRibOutboundFixture, TryDeleteRibOutEntryTestAddPath) {
  setupAdjRibForOutUnitTest();

  auto adjRibEntry = std::make_unique<AdjRibEntry>(100);
  auto attrs = std::make_shared<facebook::bgp::BgpPath>(BgpPathFields(
      *BgpUpdate2toBgpPathC(buildBgpUpdateAttributes(kV4Nexthop1))));

  // insert an entry
  adjRib_->sendAddPath_ = true;
  adjRib_->pathIdGenerator_ = std::make_unique<PathIdGenerator>(true);
  adjRib_->tryInsertRibOutEntry(
      RibOutAnnouncementEntry(kV4Prefix1, kDefaultPathID, eBgpPeer_, attrs));
  EXPECT_EQ(
      1, adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/true));

  // pre/post out is set for adjRibEntry, no deletion
  {
    adjRibEntry->setPreOut(attrs);
    adjRibEntry->setPostAttr(attrs);

    adjRib_->tryDeleteRibOutEntry(kV4Prefix1, adjRibEntry.get(), 99);

    // nothing removed
    EXPECT_EQ(
        1,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/true));
  }
  {
    adjRibEntry->setPreOut(attrs);
    adjRibEntry->setPostAttr(nullptr);

    adjRib_->tryDeleteRibOutEntry(kV4Prefix1, adjRibEntry.get(), 99);

    // nothing removed
    EXPECT_EQ(
        1,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/true));
  }
  {
    adjRibEntry->setPreOut(nullptr);
    adjRibEntry->setPostAttr(attrs);

    adjRib_->tryDeleteRibOutEntry(kV4Prefix1, adjRibEntry.get(), 99);

    // nothing removed
    EXPECT_EQ(
        1,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/true));
  }

  /*
   * remove the entry if both pre/post out are not set
   * i.e., the rib entry is no longer in use for output
   */
  {
    adjRibEntry->setPreOut(nullptr);
    adjRibEntry->setPostAttr(nullptr);

    adjRib_->tryDeleteRibOutEntry(kV4Prefix1, adjRibEntry.get(), 99);

    // entry not removed, because path-id is not correct
    EXPECT_EQ(
        1,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/true));

    adjRib_->tryDeleteRibOutEntry(kV4Prefix1, adjRibEntry.get(), 0);

    // entry removed now because path id passed was correct
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/false, /*isAddPathEnabled=*/true));
  }
}

/*
 * Unit test the function getPostPolicyOutAttrsAndPolicyFromMessage
 * 1. Prefix is not found in policyOut, the check would fail
 * 2. Prefix is found in policyOut, get the pointer to the
      attribute and policy
 */
TEST_F(AdjRibOutboundFixture, GetPostPolicyOutAttrsAndPolicyFromMessageTest) {
  setupAdjRibForOutUnitTest();

  PolicyOutMessage dummyPolicyOut;

  // prefix is not in policyOut
  {
    EXPECT_DEATH(
        adjRib_->getPostPolicyOutAttrsAndPolicyFromMessage(
            kV4Prefix1, dummyPolicyOut),
        "");
  }

  // prefix is accepted by the policy
  BgpUpdate2 update = buildBgpUpdateAttributes(kV4Nexthop1);
  // Creating two shared_ptr<BgpPath> with same content
  auto attrs =
      std::make_shared<BgpPath>(BgpPathFields(*BgpUpdate2toBgpPathC(update)));
  routing::AttributesAndPolicy<BgpPath> attrsAndPolicy(attrs, "DUMMY POLICY");
  dummyPolicyOut.result.emplace(
      kV4Prefix1,
      std::make_shared<routing::AttributesAndPolicy<BgpPath>>(attrsAndPolicy));

  {
    auto result = adjRib_->getPostPolicyOutAttrsAndPolicyFromMessage(
        kV4Prefix1, dummyPolicyOut);

    EXPECT_EQ(result->attrs, attrs);
    EXPECT_EQ(result->policyName, "DUMMY POLICY");
  }
}

// Tests negative case inputs for overridePrePolicyAttributes.
TEST_F(AdjRibOutboundFixture, OverridePrePolicyAttributesNegativeTest) {
  setupAdjRibForOutUnitTest();
  auto fields = buildBgpPathFields(
      1 /* as_count */,
      1 /* community_count */,
      1 /* ext_community_count */,
      1 /* cluster_list_count */);
  auto attrs = std::make_shared<BgpPath>(*fields);
  PolicyAttributesMask mask;

  EXPECT_DEATH(
      overridePrePolicyAttributesCommon(nullptr /* mask */, attrs, attrs), "");

  EXPECT_DEATH(
      overridePrePolicyAttributesCommon(
          &mask, nullptr /* policyResultAttrs */, attrs),
      "");

  EXPECT_DEATH(
      overridePrePolicyAttributesCommon(
          &mask, attrs, nullptr /* attrToOverride */),
      "");
}

/*
 * Tests that overridePrePolicyAttributes modifies the preAttrs
 * with the attribute values in postAttrs if the given policy mask
 * has indicated those attributes.
 */
TEST_F(AdjRibOutboundFixture, OverridePrePolicyAttributesPositiveTest) {
  setupAdjRibForOutUnitTest();
  auto fields1 = buildBgpPathFields(
      1 /* as_count */,
      1 /* community_count */,
      1 /* ext_community_count */,
      1 /* cluster_list_count */);
  auto fields2 = buildBgpPathFields(2, 2, 2, 2);
  auto preAttrs = std::make_shared<BgpPath>(*fields1);
  auto policyResultAttrs = std::make_shared<BgpPath>(*fields2);

  /*
   * Make Path1 and Path2 have different values for all attributes.
   * With the construction above,
   *   AsPath, Communities, ExtCommunities, ClusterList
   * are already guaranteed to be different.
   * Remaining fields are
   *   Origin, Nexthop, Med, LocalPref,
   *   AtomicAggregate, Aggregator, OriginatorId.
   */
  preAttrs->setOrigin(BgpAttrOrigin::BGP_ORIGIN_EGP);
  policyResultAttrs->setOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);

  preAttrs->setNexthop(kV4Nexthop1);
  policyResultAttrs->setNexthop(kV4Nexthop2);

  preAttrs->setMed(0);
  policyResultAttrs->setMed(1);

  preAttrs->setLocalPref(1);
  policyResultAttrs->setLocalPref(100);

  preAttrs->setAtomicAggregate(true);
  policyResultAttrs->setAtomicAggregate(false);

  preAttrs->setAggregator(BgpAttrAggregatorC{.asn = 0, .ip = kAggregatorAddr});
  policyResultAttrs->setAggregator(
      BgpAttrAggregatorC{.asn = 1, .ip = kEmptyV4PeerAddr});

  preAttrs->setOriginatorId(0);
  policyResultAttrs->setOriginatorId(1);

  // Case 1: Empty attributes override mask
  {
    auto originalPreAttrs = preAttrs->clone();
    auto modifiedPreAttrs = originalPreAttrs->clone();

    PolicyAttributesMask mask;

    overridePrePolicyAttributesCommon(
        &mask /* policyMask */, policyResultAttrs, modifiedPreAttrs);

    EXPECT_TRUE(*originalPreAttrs == *modifiedPreAttrs);
  }
  // Case 2: All attributes override mask
  {
    auto originalPreAttrs = preAttrs->clone();
    auto modifiedPreAttrs = originalPreAttrs->clone();

    PolicyAttributesMask fullMask{
        .origin = true,
        .asPath = true,
        .nexthop = true,
        .med = true,
        .localPref = true,
        .atomicAggregate = true,
        .aggregator = true,
        .communities = true,
        .originatorId = true,
        .clusterList = true,
        .extCommunities = true};

    overridePrePolicyAttributesCommon(
        &fullMask /* policyMask */, policyResultAttrs, modifiedPreAttrs);

    EXPECT_TRUE(*policyResultAttrs == *modifiedPreAttrs);
  }
  // Case 3: Partial attributes mask (first half)
  {
    auto expectedPreAttrs = preAttrs->clone();
    auto modifiedPreAttrs = expectedPreAttrs->clone();

    PolicyAttributesMask mask{
        .origin = false,
        .asPath = false,
        .nexthop = false,
        .med = false,
        .localPref = false,
        .atomicAggregate = false,
        .aggregator = true,
        .communities = true,
        .originatorId = true,
        .clusterList = true,
        .extCommunities = true};
    expectedPreAttrs->setAggregator(policyResultAttrs->getAggregator());
    expectedPreAttrs->setCommunities(policyResultAttrs->getCommunities().get());
    expectedPreAttrs->setOriginatorId(policyResultAttrs->getOriginatorId());
    expectedPreAttrs->setClusterList(policyResultAttrs->getClusterList().get());
    expectedPreAttrs->setExtCommunities(
        policyResultAttrs->getExtCommunities().get());

    overridePrePolicyAttributesCommon(
        &mask /* policyMask */, policyResultAttrs, modifiedPreAttrs);

    EXPECT_TRUE(*expectedPreAttrs == *modifiedPreAttrs);
  }
  // Case 4: Partial attributes mask (second half)
  {
    auto expectedPreAttrs = preAttrs->clone();
    auto modifiedPreAttrs = expectedPreAttrs->clone();

    PolicyAttributesMask mask{
        .origin = true,
        .asPath = true,
        .nexthop = true,
        .med = true,
        .localPref = true,
        .atomicAggregate = true,
        .aggregator = false,
        .communities = false,
        .originatorId = false,
        .clusterList = false,
        .extCommunities = false};
    expectedPreAttrs->setOrigin(policyResultAttrs->getOrigin());
    expectedPreAttrs->setAsPath(policyResultAttrs->getAsPath().get());
    expectedPreAttrs->setNexthop(policyResultAttrs->getNexthop());
    expectedPreAttrs->setMed(policyResultAttrs->getMed());
    expectedPreAttrs->setLocalPref(policyResultAttrs->getLocalPref());
    expectedPreAttrs->setAtomicAggregate(
        policyResultAttrs->getAtomicAggregate());

    overridePrePolicyAttributesCommon(
        &mask /* policyMask */, policyResultAttrs, modifiedPreAttrs);

    EXPECT_TRUE(*expectedPreAttrs == *modifiedPreAttrs);
  }
}

/*
 * Unit test the function suppressLoopedAdvertisements
 * 1. Case 1: IBGP peer
 * 2. Case 2: EBGP confed peer
 * 3. Case 3: EBGP peer
 */
TEST_F(AdjRibOutboundFixture, SuppressLoopedAdvertisementsTest) {
  setupAdjRibForOutUnitTest();

  adjRib_->peeringParams_.localAs = 12345;
  adjRib_->peeringParams_.remoteAs = 12345;
  adjRib_->remoteAs_ = 12345;

  auto attrs = std::make_shared<BgpPath>();

  // Case 1: IBGP peer
  EXPECT_FALSE(adjRib_->suppressLoopedAdvertisements(attrs));

  // Case 2: EBGP confed peer
  AsNum duplicatedRemoteAs = 54321;
  adjRib_->peeringParams_.isConfedPeer = true;
  adjRib_->peeringParams_.remoteAs = duplicatedRemoteAs;
  adjRib_->remoteAs_ = duplicatedRemoteAs;
  {
    // has AS set loop
    BgpAttrAsPathSegmentC segmentSet;
    segmentSet.asConfedSet.insert(duplicatedRemoteAs);
    attrs->setAsPath(BgpAttrAsPathC{{segmentSet}});

    EXPECT_TRUE(adjRib_->suppressLoopedAdvertisements(attrs));

    // has AS seq loop
    BgpAttrAsPathSegmentC segmentPath;
    segmentPath.asConfedSequence = {123, 321, duplicatedRemoteAs, 456};
    attrs->setAsPath(BgpAttrAsPathC{{segmentPath}});

    EXPECT_TRUE(adjRib_->suppressLoopedAdvertisements(attrs));

    // no loop
    BgpAttrAsPathSegmentC segmentNoLoopSet;
    segmentNoLoopSet.asConfedSet.insert(123);

    BgpAttrAsPathSegmentC segmentNoLoopPath;
    segmentPath.asConfedSequence = {321, 456};

    attrs->setAsPath(BgpAttrAsPathC{{segmentNoLoopSet, segmentNoLoopPath}});

    EXPECT_FALSE(adjRib_->suppressLoopedAdvertisements(attrs));
  }

  // Case 3: EBGP peer
  adjRib_->peeringParams_.isConfedPeer = false;
  {
    // has AS set loop
    BgpAttrAsPathSegmentC segmentSet;
    segmentSet.asSet.insert(duplicatedRemoteAs);
    attrs->setAsPath(BgpAttrAsPathC{{segmentSet}});

    EXPECT_TRUE(adjRib_->suppressLoopedAdvertisements(attrs));

    // has AS seq loop
    BgpAttrAsPathSegmentC segmentPath;
    segmentPath.asSequence = {123, 321, duplicatedRemoteAs, 456};
    attrs->setAsPath(BgpAttrAsPathC{{segmentPath}});

    EXPECT_TRUE(adjRib_->suppressLoopedAdvertisements(attrs));

    // no loop
    BgpAttrAsPathSegmentC segmentNoLoopSet;
    segmentNoLoopSet.asSet.insert(123);

    BgpAttrAsPathSegmentC segmentNoLoopPath;
    segmentPath.asSequence = {321, 456};

    attrs->setAsPath(BgpAttrAsPathC{{segmentNoLoopSet, segmentNoLoopPath}});

    EXPECT_FALSE(adjRib_->suppressLoopedAdvertisements(attrs));
  }
}

TEST_F(
    AdjRibOutboundFixture,
    SuppressLoopedAdvertisementsUsesAcceptedRemoteAs) {
  setupAdjRibForOutUnitTest();

  const AsNum configuredRemoteAs = 54320;
  const AsNum acceptedRemoteAs = 54321;
  adjRib_->peeringParams_.localAs = 12345;
  adjRib_->peeringParams_.remoteAs = configuredRemoteAs;
  adjRib_->remoteAs_ = acceptedRemoteAs;
  adjRib_->peeringParams_.isConfedPeer = false;

  auto attrs = std::make_shared<BgpPath>();
  BgpAttrAsPathSegmentC segment;
  segment.asSequence = {acceptedRemoteAs};
  attrs->setAsPath(BgpAttrAsPathC{{segment}});
  EXPECT_TRUE(adjRib_->suppressLoopedAdvertisements(attrs));

  segment.asSequence = {configuredRemoteAs};
  attrs->setAsPath(BgpAttrAsPathC{{segment}});
  EXPECT_FALSE(adjRib_->suppressLoopedAdvertisements(attrs));
}

TEST_F(AdjRibOutboundFixture, GetPostPolicyAttributesPolicyTermAndInfoTest) {
  setupAdjRibForOutUnitTest();

  // Create a policy with one term which matches all and sets Med
  const std::string policyName = kEgressPolicyName;
  auto policyManager = setupMatchAllSetMedPolicy(policyName);

  // Set up AdjRib with the policy
  setupAdjRib(policyManager, policyName, false);

  // Create a BGP path with attributes
  auto prePolicyAttrs = std::make_shared<facebook::bgp::BgpPath>(BgpPathFields(
      *BgpUpdate2toBgpPathC(buildBgpUpdateAttributes(kV4Nexthop1))));

  // Create policy action data
  auto policyActionData = adjRib_->createPolicyActionData(prePolicyAttrs);

  // Call the function under test
  auto [postPolicyAttrs, policyTermName, postPolicyInfo] =
      adjRib_->getPostPolicyAttributesPolicyTermAndInfo(
          policyName, kV4Prefix1, prePolicyAttrs, policyActionData);

  // Verify the result
  EXPECT_NE(nullptr, postPolicyAttrs);
  EXPECT_EQ("Accepted/Modified by Egress term term1", policyTermName);
  EXPECT_TRUE(postPolicyInfo.isMedSetByPolicy);

  // Verify attributes were modified
  EXPECT_EQ(postPolicyAttrs->getMed(), kMed);
}

/**
 * Verify that when a route is rejected due to invalid GAR weights
 * (isLbwRejected), the deny reason returned by
 * getPostPolicyAttributesPolicyTermAndInfo includes "invalid GAR weights".
 */
TEST_F(
    AdjRibOutboundFixture,
    GetPostPolicyAttributesPolicyTermAndInfoTest_RejectedByInvalidGarWeights) {
  setupAdjRibForOutUnitTest();

  /*
   * Create a ENCODE_AGGREGATE_RECEIVED_OVERWRITE UCMP policy.
   * When aggregateReceivedUcmpWeight is missing (nullopt), isLbwRejected
   * will be set to true.
   */
  nsf_policy::NsfTeWeightEncoding encoding;
  encoding.l2_encoding() = nsf_policy::NsfL2TeWeightEncoding();
  encoding.l2_encoding()->rack_id() = 4;
  encoding.l2_encoding()->plane_id() = 4;
  encoding.l2_encoding()->remote_rack_capacity() = 8;
  encoding.l2_encoding()->spine_capacity() = 8;
  encoding.l2_encoding()->local_rack_capacity() = 8;

  auto ucmpAction = createBgpPolicyLbwExtCommunityAction(
      bgp_policy::LbwExtCommunityActionType::
          ENCODE_AGGREGATE_RECEIVED_OVERWRITE,
      encoding,
      2 /* encodingId */);
  auto term = createBgpPolicyTerm("UCMP-term", "", {}, {ucmpAction});

  const std::string policyName = kEgressPolicyName;
  const auto& policyConfig = createBgpPolicies(policyName, {term});
  auto policyManager = std::make_shared<PolicyManager>(
      policyConfig, createTestBgpGlobalConfig());

  // Set up AdjRib with the policy
  setupAdjRib(policyManager, policyName, false);

  // Create a BGP path with attributes
  auto prePolicyAttrs = std::make_shared<facebook::bgp::BgpPath>(BgpPathFields(
      *BgpUpdate2toBgpPathC(buildBgpUpdateAttributes(kV4Nexthop1))));

  /*
   * Create policy action data WITHOUT aggregateReceivedUcmpWeight.
   * This triggers isLbwRejected = true in ENCODE_AGGREGATE_RECEIVED_OVERWRITE.
   */
  auto policyActionData = adjRib_->createPolicyActionData(
      prePolicyAttrs,
      std::nullopt, // switchId
      std::nullopt, // multiPathSize
      std::nullopt, // aggregateReceivedUcmpWeight (missing -> reject)
      std::nullopt, // aggregateLocalUcmpWeight
      std::nullopt); // ribPolicyUcmpWeight

  // Call the function under test
  auto [postPolicyAttrs, policyTermName, postPolicyInfo] =
      adjRib_->getPostPolicyAttributesPolicyTermAndInfo(
          policyName, kV4Prefix1, prePolicyAttrs, policyActionData);

  // Verify the route was denied
  EXPECT_EQ(nullptr, postPolicyAttrs);
  // Verify the deny reason includes the GAR weights suffix
  EXPECT_THAT(policyTermName, HasSubstr("Denied by"));
  EXPECT_THAT(
      policyTermName, HasSubstr(std::string(kInvalidGarWeightsDenyReason)));
}

/*
 * Tests for pruneLbwExtCommunitiesCommon function.
 *
 * This function should keep only the lowest transitive LBW and the lowest
 * non-transitive LBW extended communities, removing all other LBW communities
 * while preserving non-LBW extended communities.
 */

// Test case 1: Empty BgpAttrExtCommunitiesC - should remain empty
TEST(PruneLbwExtCommunitiesCommonTest, EmptyCommunities) {
  BgpAttrExtCommunitiesC communities;

  pruneLbwExtCommunitiesCommon(communities);

  EXPECT_TRUE(communities.empty());
}

// Test case 2: One transitive LBW extended community - should remain unchanged
TEST(PruneLbwExtCommunitiesCommonTest, OneTransitiveLBW) {
  BgpAttrExtCommunitiesC communities;

  /*
   * Create a transitive LBW community: type = 0x00, subtype = 0x04
   * rawValHigh = (type << 24) | (subtype << 16) | asn
   * Using type 0x00 (transitive), subtype 0x04 (LBW), ASN 12345
   */
  uint32_t rawValHigh = (0x00 << 24) | (0x04 << 16) | 12345;
  uint32_t rawValLow = 1000; // LBW value
  communities.emplace_back(rawValHigh, rawValLow);

  ASSERT_EQ(1, communities.size());
  EXPECT_TRUE(communities[0].isTransitive());

  pruneLbwExtCommunitiesCommon(communities);

  EXPECT_EQ(1, communities.size());
  EXPECT_EQ(rawValLow, communities[0].getRawValueInWords().second);
}

/*
 * Test case 3: One non-transitive LBW extended community - should remain
 * unchanged
 */
TEST(PruneLbwExtCommunitiesCommonTest, OneNonTransitiveLBW) {
  BgpAttrExtCommunitiesC communities;

  // Create a non-transitive LBW community using the helper
  BgpExtCommunityLinkBandWidthTypeC lbwComm(12345 /* asn */, 30000000.0f);
  communities.emplace_back(lbwComm);

  ASSERT_EQ(1, communities.size());
  EXPECT_TRUE(communities[0].isNonTransitiveLinkBandwidthCommunity());
  EXPECT_FALSE(communities[0].isTransitive());

  pruneLbwExtCommunitiesCommon(communities);

  EXPECT_EQ(1, communities.size());
  EXPECT_TRUE(communities[0].isNonTransitiveLinkBandwidthCommunity());
}

/*
 * Test case 4: One of each transitivity type + 5 non-LBW extended communities
 * Verify the lowest value is returned for each transitivity type and non-LBW
 * communities are preserved.
 */
TEST(PruneLbwExtCommunitiesCommonTest, NonLBWOnly) {
  BgpAttrExtCommunitiesC communities;

  /*
   * Add 5 non-LBW extended communities (route targets)
   * Route target: type = 0x00, subtype = 0x02
   */
  for (int i = 0; i < 5; ++i) {
    uint32_t rawValHigh = (0x00 << 24) | (0x02 << 16) | (100 + i);
    uint32_t rawValLow = 1000 + i;
    communities.emplace_back(rawValHigh, rawValLow);
  }

  auto originalCommunities = communities;

  pruneLbwExtCommunitiesCommon(communities);

  EXPECT_EQ(5, communities.size());
  EXPECT_EQ(originalCommunities, communities);
}

/*
 * Test case 4: One of each transitivity type + 5 non-LBW extended communities
 * Verify only the absolute lowest LBW is kept (regardless of transitivity)
 * and non-LBW communities are preserved.
 */
TEST(PruneLbwExtCommunitiesCommonTest, MixedTransitivityWithNonLBW) {
  BgpAttrExtCommunitiesC communities;

  /*
   * Add 5 non-LBW extended communities (route targets)
   * Route target: type = 0x00, subtype = 0x02
   */
  for (int i = 0; i < 5; ++i) {
    uint32_t rawValHigh = (0x00 << 24) | (0x02 << 16) | (100 + i);
    uint32_t rawValLow = 1000 + i;
    communities.emplace_back(rawValHigh, rawValLow);
  }

  // Add multiple non-transitive LBW communities with different values
  BgpExtCommunityLinkBandWidthTypeC lbwNonTrans1(12345, 300.0f);
  BgpExtCommunityLinkBandWidthTypeC lbwNonTrans2(12345, 100.0f);
  BgpExtCommunityLinkBandWidthTypeC lbwNonTrans3(12345, 500.0f);
  communities.emplace_back(lbwNonTrans1);
  communities.emplace_back(lbwNonTrans2);
  communities.emplace_back(lbwNonTrans3);

  /*
   * Add multiple transitive LBW communities with different values
   * Transitive LBW: type = 0x00, subtype = 0x04
   */
  uint32_t transLbwHigh = (0x00 << 24) | (0x04 << 16) | 12345;
  union {
    float floatVal;
    uint32_t intVal;
  } tmp{};

  // 50.0f is the absolute lowest across both types
  tmp.floatVal = 50.0f;
  uint32_t lowestLbwT = tmp.intVal;
  communities.emplace_back(transLbwHigh, lowestLbwT);

  tmp.floatVal = 150.0f;
  communities.emplace_back(transLbwHigh, tmp.intVal);

  tmp.floatVal = 250.0f;
  communities.emplace_back(transLbwHigh, tmp.intVal);

  // 5 non-LBW + 3 non-trans LBW + 3 trans LBW
  ASSERT_EQ(11, communities.size());

  pruneLbwExtCommunitiesCommon(communities);

  /*
   * After pruning:
   * - 5 non-LBW communities should remain
   * - Only 1 LBW with the absolute lowest value (50.0f transitive) should
   * remain
   */
  EXPECT_EQ(6, communities.size());

  // Count each type to verify
  int nonLbwCount = 0;
  int lbwCount = 0;

  for (const auto& comm : communities) {
    if (comm.isLinkBandwidthCommunity()) {
      lbwCount++;
      EXPECT_EQ(lowestLbwT, comm.getRawValueInWords().second);
      EXPECT_TRUE(comm.isTransitive());
    } else {
      nonLbwCount++;
    }
  }

  EXPECT_EQ(5, nonLbwCount);
  EXPECT_EQ(1, lbwCount);
}

// Test case 5: Single negative transitive LBW - should be filtered out
TEST(PruneLbwExtCommunitiesCommonTest, SingleNegativeTransitiveLBW) {
  BgpAttrExtCommunitiesC communities;

  /*
   * Create a transitive LBW community with negative value
   * Transitive LBW: type = 0x00, subtype = 0x04
   */
  uint32_t transLbwHigh = (0x00 << 24) | (0x04 << 16) | 12345;

  // Convert negative float to uint32_t
  union {
    float floatVal;
    uint32_t intVal;
  } tmp{};
  tmp.floatVal = -100.0f;
  communities.emplace_back(transLbwHigh, tmp.intVal);

  ASSERT_EQ(1, communities.size());
  EXPECT_TRUE(
      communities[0].isLinkBandwidthCommunity() &&
      communities[0].isTransitive());

  pruneLbwExtCommunitiesCommon(communities);

  // Negative LBW should be filtered out, leaving empty communities
  EXPECT_EQ(0, communities.size());
}

// Test case 6: Single negative non-transitive LBW - should be filtered out
TEST(PruneLbwExtCommunitiesCommonTest, SingleNegativeNonTransitiveLBW) {
  BgpAttrExtCommunitiesC communities;

  // Create a non-transitive LBW community with negative value
  BgpExtCommunityLinkBandWidthTypeC lbwComm(12345 /* asn */, -50.0f);
  communities.emplace_back(lbwComm);

  ASSERT_EQ(1, communities.size());
  EXPECT_TRUE(communities[0].isNonTransitiveLinkBandwidthCommunity());

  pruneLbwExtCommunitiesCommon(communities);

  // Negative LBW should be filtered out, leaving empty communities
  EXPECT_EQ(0, communities.size());
}

/*
 * Test case 7: Mixed positive and negative transitive LBWs - negative should be
 * filtered out
 */
TEST(PruneLbwExtCommunitiesCommonTest, MixedPositiveNegativeTransitiveLBW) {
  BgpAttrExtCommunitiesC communities;

  // Transitive LBW: type = 0x00, subtype = 0x04
  uint32_t transLbwHigh = (0x00 << 24) | (0x04 << 16) | 12345;

  union {
    float floatVal;
    uint32_t intVal;
  } tmp{};

  // Add positive transitive LBW (100.0f)
  tmp.floatVal = 100.0f;
  uint32_t positiveLbw = tmp.intVal;
  communities.emplace_back(transLbwHigh, positiveLbw);

  // Add negative transitive LBW (-50.0f) - should be filtered out
  tmp.floatVal = -50.0f;
  communities.emplace_back(transLbwHigh, tmp.intVal);

  /*
   * Add another positive transitive LBW (200.0f) - should be filtered (not
   * lowest)
   */
  tmp.floatVal = 200.0f;
  communities.emplace_back(transLbwHigh, tmp.intVal);

  ASSERT_EQ(3, communities.size());

  pruneLbwExtCommunitiesCommon(communities);

  // Should keep only the lowest positive (100.0f), negative is filtered
  EXPECT_EQ(1, communities.size());
  EXPECT_TRUE(
      communities[0].isLinkBandwidthCommunity() &&
      communities[0].isTransitive());
  EXPECT_EQ(positiveLbw, communities[0].getRawValueInWords().second);
}

/*
 * Test case 8: Mixed positive and negative non-transitive LBWs - negative
 * should be filtered out
 */
TEST(PruneLbwExtCommunitiesCommonTest, MixedPositiveNegativeNonTransitiveLBW) {
  BgpAttrExtCommunitiesC communities;

  // Add positive non-transitive LBW (100.0f)
  BgpExtCommunityLinkBandWidthTypeC lbwPos1(12345, 100.0f);
  communities.emplace_back(lbwPos1);
  uint32_t lowestLbw = lbwPos1.rawValLow;

  // Add negative non-transitive LBW (-50.0f) - should be filtered out
  BgpExtCommunityLinkBandWidthTypeC lbwNeg(12345, -50.0f);
  communities.emplace_back(lbwNeg);

  /*
   * Add another positive non-transitive LBW (200.0f) - should be filtered (not
   * lowest)
   */
  BgpExtCommunityLinkBandWidthTypeC lbwPos2(12345, 200.0f);
  communities.emplace_back(lbwPos2);

  ASSERT_EQ(3, communities.size());

  pruneLbwExtCommunitiesCommon(communities);

  // Should keep only the lowest positive (100.0f), negative is filtered
  EXPECT_EQ(1, communities.size());
  EXPECT_TRUE(communities[0].isNonTransitiveLinkBandwidthCommunity());
  EXPECT_EQ(lowestLbw, communities[0].getRawValueInWords().second);
}

/*
 * Test case 9: Negative LBWs mixed with positive ones and non-LBW communities -
 * negative LBWs should be filtered, only absolute lowest positive kept, non-LBW
 * preserved
 */
TEST(PruneLbwExtCommunitiesCommonTest, NegativeLBWsWithPositiveAndNonLBW) {
  BgpAttrExtCommunitiesC communities;

  // Add 3 non-LBW extended communities (route targets)
  for (int i = 0; i < 3; ++i) {
    uint32_t rawValHigh = (0x00 << 24) | (0x02 << 16) | (100 + i);
    uint32_t rawValLow = 1000 + i;
    communities.emplace_back(rawValHigh, rawValLow);
  }

  // Add negative non-transitive LBWs (should be filtered)
  BgpExtCommunityLinkBandWidthTypeC lbwNeg1(12345, -100.0f);
  BgpExtCommunityLinkBandWidthTypeC lbwNeg2(12345, -200.0f);
  communities.emplace_back(lbwNeg1);
  communities.emplace_back(lbwNeg2);

  /*
   * Add positive non-transitive LBWs (50.0f is lowest among non-trans, but not
   * absolute lowest)
   */
  BgpExtCommunityLinkBandWidthTypeC lbwPos1(12345, 50.0f);
  BgpExtCommunityLinkBandWidthTypeC lbwPos2(12345, 100.0f);
  communities.emplace_back(lbwPos1);
  communities.emplace_back(lbwPos2);

  // Add negative transitive LBWs (should be filtered)
  uint32_t transLbwHigh = (0x00 << 24) | (0x04 << 16) | 12345;
  union {
    float floatVal;
    uint32_t intVal;
  } tmp{};
  tmp.floatVal = -300.0f;
  communities.emplace_back(transLbwHigh, tmp.intVal);
  tmp.floatVal = -400.0f;
  communities.emplace_back(transLbwHigh, tmp.intVal);

  // Add positive transitive LBWs (25.0f is the absolute lowest across all LBWs)
  tmp.floatVal = 25.0f;
  uint32_t lowestLbw = tmp.intVal;
  communities.emplace_back(transLbwHigh, lowestLbw);
  tmp.floatVal = 75.0f;
  communities.emplace_back(transLbwHigh, tmp.intVal);

  // 3 non-LBW + 4 non-trans LBW + 4 trans LBW
  ASSERT_EQ(11, communities.size());

  pruneLbwExtCommunitiesCommon(communities);

  /*
   * After pruning:
   * - 3 non-LBW communities should remain
   * - Only 1 LBW with the absolute lowest value (25.0f transitive) should
   * remain
   * - All negative LBWs and non-lowest positive LBWs should be filtered out
   */
  EXPECT_EQ(4, communities.size());

  // Count each type to verify
  int nonLbwCount = 0;
  int lbwCount = 0;

  for (const auto& comm : communities) {
    if (comm.isLinkBandwidthCommunity()) {
      lbwCount++;
      EXPECT_EQ(lowestLbw, comm.getRawValueInWords().second);
      EXPECT_TRUE(comm.isTransitive());
    } else {
      nonLbwCount++;
    }
  }

  EXPECT_EQ(3, nonLbwCount);
  EXPECT_EQ(1, lbwCount);
}

/*
 * Test case 10: Mixed positive, negative, and zero LBWs - negative filtered,
 * zero and positive kept
 */
TEST(PruneLbwExtCommunitiesCommonTest, MixedPositiveNegativeZeroLBW) {
  BgpAttrExtCommunitiesC communities;

  // Add positive non-transitive LBW (100.0f)
  BgpExtCommunityLinkBandWidthTypeC lbwPos(12345, 100.0f);
  communities.emplace_back(lbwPos);

  // Add zero non-transitive LBW (0.0f) - should be kept as lowest
  BgpExtCommunityLinkBandWidthTypeC lbwZero(12345, 0.0f);
  communities.emplace_back(lbwZero);
  uint32_t zeroLbw = lbwZero.rawValLow;

  // Add negative non-transitive LBW (-50.0f) - should be filtered out
  BgpExtCommunityLinkBandWidthTypeC lbwNeg(12345, -50.0f);
  communities.emplace_back(lbwNeg);

  ASSERT_EQ(3, communities.size());

  pruneLbwExtCommunitiesCommon(communities);

  // Should keep only the zero (lowest non-negative), negative is filtered
  EXPECT_EQ(1, communities.size());
  EXPECT_TRUE(communities[0].isNonTransitiveLinkBandwidthCommunity());
  EXPECT_EQ(zeroLbw, communities[0].getRawValueInWords().second);
}

/*
 * Test case 11: All negative LBWs with non-LBW communities - all negative LBWs
 * should be filtered, non-LBW preserved
 */
TEST(PruneLbwExtCommunitiesCommonTest, AllNegativeLBWsWithNonLBW) {
  BgpAttrExtCommunitiesC communities;

  // Add 3 non-LBW extended communities (route targets)
  for (int i = 0; i < 3; ++i) {
    uint32_t rawValHigh = (0x00 << 24) | (0x02 << 16) | (100 + i);
    uint32_t rawValLow = 1000 + i;
    communities.emplace_back(rawValHigh, rawValLow);
  }

  // Add negative non-transitive LBWs
  BgpExtCommunityLinkBandWidthTypeC lbwNeg1(12345, -100.0f);
  BgpExtCommunityLinkBandWidthTypeC lbwNeg2(12345, -200.0f);
  communities.emplace_back(lbwNeg1);
  communities.emplace_back(lbwNeg2);

  // Add negative transitive LBWs
  uint32_t transLbwHigh = (0x00 << 24) | (0x04 << 16) | 12345;
  union {
    float floatVal;
    uint32_t intVal;
  } tmp{};
  tmp.floatVal = -300.0f;
  communities.emplace_back(transLbwHigh, tmp.intVal);
  tmp.floatVal = -400.0f;
  communities.emplace_back(transLbwHigh, tmp.intVal);

  ASSERT_EQ(7, communities.size());

  pruneLbwExtCommunitiesCommon(communities);

  // All negative LBWs should be filtered, only non-LBW communities remain
  EXPECT_EQ(3, communities.size());

  // Verify only non-LBW communities remain
  for (const auto& comm : communities) {
    EXPECT_FALSE(comm.isLinkBandwidthCommunity());
  }
}

/*
 * Test case 12: Transitive and non-transitive LBWs with the SAME value -
 * both should be kept since they have the same lowest bandwidth
 */
TEST(PruneLbwExtCommunitiesCommonTest, SameValueTransitiveAndNonTransitiveLBW) {
  BgpAttrExtCommunitiesC communities;

  // Add a non-transitive LBW with value 100.0f
  BgpExtCommunityLinkBandWidthTypeC lbwNonTrans(12345, 100.0f);
  communities.emplace_back(lbwNonTrans);
  uint32_t sameLbwValue = lbwNonTrans.rawValLow;

  /*
   * Add a transitive LBW with the SAME value (100.0f)
   * Transitive LBW: type = 0x00, subtype = 0x04
   */
  uint32_t transLbwHigh = (0x00 << 24) | (0x04 << 16) | 12345;
  communities.emplace_back(transLbwHigh, sameLbwValue);

  ASSERT_EQ(2, communities.size());

  pruneLbwExtCommunitiesCommon(communities);

  // Both LBWs should be kept since they have the same value
  EXPECT_EQ(2, communities.size());

  // Verify we have one transitive and one non-transitive
  int transCount = 0;
  int nonTransCount = 0;
  for (const auto& comm : communities) {
    EXPECT_TRUE(comm.isLinkBandwidthCommunity());
    EXPECT_EQ(sameLbwValue, comm.getRawValueInWords().second);
    if (comm.isTransitive()) {
      transCount++;
    } else {
      nonTransCount++;
    }
  }
  EXPECT_EQ(1, transCount);
  EXPECT_EQ(1, nonTransCount);
}

/*
 * Test case 13: Multiple LBWs with same value and some with different values -
 * only the lowest value LBWs should be kept (both transitive and non-transitive
 * if they share the same lowest value)
 */
TEST(
    PruneLbwExtCommunitiesCommonTest,
    SameLowestValueWithOtherHigherValuesLBW) {
  BgpAttrExtCommunitiesC communities;

  // Add non-transitive LBWs: 50.0f (lowest), 100.0f, 200.0f
  BgpExtCommunityLinkBandWidthTypeC lbwNonTrans1(12345, 50.0f);
  BgpExtCommunityLinkBandWidthTypeC lbwNonTrans2(12345, 100.0f);
  BgpExtCommunityLinkBandWidthTypeC lbwNonTrans3(12345, 200.0f);
  communities.emplace_back(lbwNonTrans1);
  communities.emplace_back(lbwNonTrans2);
  communities.emplace_back(lbwNonTrans3);
  uint32_t lowestLbwValue = lbwNonTrans1.rawValLow;

  /*
   * Add transitive LBWs: 50.0f (same as lowest non-trans), 75.0f, 150.0f
   * Transitive LBW: type = 0x00, subtype = 0x04
   */
  uint32_t transLbwHigh = (0x00 << 24) | (0x04 << 16) | 12345;
  union {
    float floatVal;
    uint32_t intVal;
  } tmp{};

  tmp.floatVal = 50.0f;
  communities.emplace_back(transLbwHigh, tmp.intVal);

  tmp.floatVal = 75.0f;
  communities.emplace_back(transLbwHigh, tmp.intVal);

  tmp.floatVal = 150.0f;
  communities.emplace_back(transLbwHigh, tmp.intVal);

  // Add some non-LBW communities
  for (int i = 0; i < 2; ++i) {
    uint32_t rawValHigh = (0x00 << 24) | (0x02 << 16) | (100 + i);
    uint32_t rawValLow = 1000 + i;
    communities.emplace_back(rawValHigh, rawValLow);
  }

  // 3 non-trans LBW + 3 trans LBW + 2 non-LBW
  ASSERT_EQ(8, communities.size());

  pruneLbwExtCommunitiesCommon(communities);

  /*
   * After pruning:
   * - 2 non-LBW communities should remain
   * - 2 LBWs with the same lowest value (50.0f) should remain:
   *   one transitive and one non-transitive
   */
  EXPECT_EQ(4, communities.size());

  // Count each type to verify
  int nonLbwCount = 0;
  int transLbwCount = 0;
  int nonTransLbwCount = 0;

  for (const auto& comm : communities) {
    if (comm.isLinkBandwidthCommunity()) {
      EXPECT_EQ(lowestLbwValue, comm.getRawValueInWords().second);
      if (comm.isTransitive()) {
        transLbwCount++;
      } else {
        nonTransLbwCount++;
      }
    } else {
      nonLbwCount++;
    }
  }

  EXPECT_EQ(2, nonLbwCount);
  EXPECT_EQ(1, transLbwCount);
  EXPECT_EQ(1, nonTransLbwCount);
}

/**
 * Test fixture for updateExtCommunitiesCommon function.
 *
 * This test verifies the behavior of extended community pruning based on:
 * - UCMP/GAR settings (customizedLbwEnabled, receiveLinkBandwidth,
 *   advertiseLinkBandwidth)
 * - Session type (EBGP removes non-transitive communities per RFC 4360)
 * - LBW pruning (keeps only the absolute lowest LBW, regardless of
 * transitivity)
 */
class UpdateExtCommunitiesCommonTest : public ::testing::Test {
 protected:
  /*
   * Helper to build extended communities for testing:
   * - 2 transitive LBW (bandwidths 200 and 100, lowest=100 is absolute lowest)
   * - 2 non-transitive LBW (bandwidths 300 and 150)
   * - 1 non-LBW extended community (route target)
   * After pruning, only the absolute lowest LBW (100.0f transitive) remains
   */
  static BgpAttrExtCommunitiesC buildTestExtCommunities() {
    BgpAttrExtCommunitiesC communities;

    /*
     * Add 1 transitive non-LBW extended community
     * (route target: type=0x00, subtype=0x02)
     */
    uint32_t rtHigh = (0x00 << 24) | (0x02 << 16) | 65000;
    uint32_t rtLow = 12345;
    communities.emplace_back(rtHigh, rtLow);

    /*
     * Add 2 non-transitive LBW communities using the helper
     * Non-transitive LBW: type=0x40, subtype=0x04
     */
    BgpExtCommunityLinkBandWidthTypeC lbwNonTrans1(12345, 300.0f);
    BgpExtCommunityLinkBandWidthTypeC lbwNonTrans2(12345, 150.0f); // lowest
    communities.emplace_back(lbwNonTrans1);
    communities.emplace_back(lbwNonTrans2);

    /*
     * Add 2 transitive LBW communities
     * Transitive LBW: type=0x00, subtype=0x04
     */
    uint32_t transLbwHigh = (0x00 << 24) | (0x04 << 16) | 12345;
    union {
      float floatVal;
      uint32_t intVal;
    } tmp{};
    tmp.floatVal = 200.0f;
    communities.emplace_back(transLbwHigh, tmp.intVal);

    tmp.floatVal = 100.0f; // lowest
    communities.emplace_back(transLbwHigh, tmp.intVal);

    return communities;
  }

  // Create EBGP PeeringParams: localAs != remoteAs && !isConfedPeer
  static PeeringParams buildEBgpPeeringParams() {
    PeeringParams params;
    params.localAs = 65001;
    params.remoteAs = 65002;
    params.isConfedPeer = ConfedPeerConfigured{false};
    return params;
  }

  // Create IBGP PeeringParams: localAs == remoteAs
  static PeeringParams buildIBgpPeeringParams() {
    PeeringParams params;
    params.localAs = 65001;
    params.remoteAs = 65001;
    params.isConfedPeer = ConfedPeerConfigured{false};
    return params;
  }

  // Create ConfedEBGP PeeringParams: isConfedPeer && localAs != remoteAs
  static PeeringParams buildConfedEBgpPeeringParams() {
    PeeringParams params;
    params.localAs = 65001;
    params.remoteAs = 65002;
    params.isConfedPeer = ConfedPeerConfigured{true};
    return params;
  }

  // Helper to count community types
  struct CommunityCounts {
    int nonLbw = 0;
    int transitiveLbw = 0;
    int nonTransitiveLbw = 0;
  };

  static CommunityCounts countCommunityTypes(
      const BgpAttrExtCommunitiesC& communities) {
    CommunityCounts counts;
    for (const auto& comm : communities) {
      if (comm.isNonTransitiveLinkBandwidthCommunity()) {
        counts.nonTransitiveLbw++;
      } else if (comm.isLinkBandwidthCommunity() && comm.isTransitive()) {
        counts.transitiveLbw++;
      } else {
        counts.nonLbw++;
      }
    }
    return counts;
  }
};

/*
 * Test case 1: customizedLbwEnabled = true
 * Non-transitive LBW communities are kept during EBGP filtering but then
 * pruneLbwExtCommunitiesCommon prunes to the absolute lowest LBW (100.0f
 * transitive). Result: 1 non-LBW + 1 transitive LBW = 2 communities.
 */
TEST_F(UpdateExtCommunitiesCommonTest, CustomizedLbwEnabledNoModification) {
  auto communities = buildTestExtCommunities();

  // Create BgpPath with test communities
  auto attrs = std::make_shared<BgpPath>();
  attrs->setExtCommunities(std::move(communities));

  // Set up mask with customizedLbwEnabled = true
  PolicyAttributesMask mask;
  mask.customizedLbwEnabled = true;

  // Use EBGP peer params (which would normally prune non-transitive)
  auto peeringParams = buildEBgpPeeringParams();

  updateExtCommunitiesCommon(peeringParams, BgpSessionType::EBGP, &mask, attrs);

  /*
   * Non-transitive LBW kept during EBGP filtering, but then pruned to lowest
   * LBW (100.0f transitive) by pruneLbwExtCommunitiesCommon
   */
  EXPECT_EQ(2, attrs->getExtCommunities()->size());
  auto counts = countCommunityTypes(attrs->getExtCommunities().get());
  EXPECT_EQ(1, counts.nonLbw);
  EXPECT_EQ(1, counts.transitiveLbw);
  EXPECT_EQ(0, counts.nonTransitiveLbw);
}

/*
 * Test case 2: receive/advertise link bandwidth configured
 * Non-transitive LBW communities are kept during EBGP filtering but then
 * pruneLbwExtCommunitiesCommon prunes to the absolute lowest LBW (100.0f
 * transitive). Result: 1 non-LBW + 1 transitive LBW = 2 communities.
 */
TEST_F(UpdateExtCommunitiesCommonTest, ReceiveAdvertiseLbwNoModification) {
  auto communities = buildTestExtCommunities();

  // Create BgpPath with test communities
  auto attrs = std::make_shared<BgpPath>();
  attrs->setExtCommunities(std::move(communities));

  // No mask (or mask with customizedLbwEnabled = false)
  PolicyAttributesMask mask;
  mask.customizedLbwEnabled = false;

  // Set up EBGP peer params with receiveLinkBandwidth configured
  auto peeringParams = buildEBgpPeeringParams();
  peeringParams.receiveLinkBandwidth = ReceiveLinkBandwidth::ACCEPT;

  updateExtCommunitiesCommon(peeringParams, BgpSessionType::EBGP, &mask, attrs);

  /*
   * Non-transitive LBW kept during EBGP filtering, but then pruned to lowest
   * LBW (100.0f transitive) by pruneLbwExtCommunitiesCommon
   */
  EXPECT_EQ(2, attrs->getExtCommunities()->size());
  auto counts = countCommunityTypes(attrs->getExtCommunities().get());
  EXPECT_EQ(1, counts.nonLbw);
  EXPECT_EQ(1, counts.transitiveLbw);
  EXPECT_EQ(0, counts.nonTransitiveLbw);

  // Also test with advertiseLinkBandwidth configured
  communities = buildTestExtCommunities();
  attrs = std::make_shared<BgpPath>();
  attrs->setExtCommunities(std::move(communities));

  peeringParams = buildEBgpPeeringParams();
  peeringParams.receiveLinkBandwidth = std::nullopt;
  peeringParams.advertiseLinkBandwidth = AdvertiseLinkBandwidth::BEST_PATH;

  updateExtCommunitiesCommon(peeringParams, BgpSessionType::EBGP, &mask, attrs);

  // Same behavior - pruned to lowest LBW
  EXPECT_EQ(2, attrs->getExtCommunities()->size());
  counts = countCommunityTypes(attrs->getExtCommunities().get());
  EXPECT_EQ(1, counts.nonLbw);
  EXPECT_EQ(1, counts.transitiveLbw);
  EXPECT_EQ(0, counts.nonTransitiveLbw);
}

/*
 * Test case 3: EBGP + (customizedLbwEnabled = false or mask = nullptr)
 * Only 1 transitive LBW + 1 non-LBW should remain
 * (non-transitive LBW removed per RFC 4360 for EBGP peers)
 */
TEST_F(UpdateExtCommunitiesCommonTest, EBgpPrunesNonTransitiveLbw) {
  // Test with mask = nullptr
  {
    auto communities = buildTestExtCommunities();
    auto attrs = std::make_shared<BgpPath>();
    attrs->setExtCommunities(std::move(communities));

    auto peeringParams = buildEBgpPeeringParams();

    updateExtCommunitiesCommon(
        peeringParams, BgpSessionType::EBGP, nullptr, attrs);

    /*
     * EBGP removes non-transitive communities (RFC 4360)
     * After pruning: 1 transitive non-LBW + 1 lowest transitive LBW
     */
    EXPECT_EQ(2, attrs->getExtCommunities()->size());
    auto counts = countCommunityTypes(attrs->getExtCommunities().get());
    EXPECT_EQ(1, counts.nonLbw);
    EXPECT_EQ(1, counts.transitiveLbw);
    EXPECT_EQ(0, counts.nonTransitiveLbw);
  }

  // Test with customizedLbwEnabled = false
  {
    auto communities = buildTestExtCommunities();
    auto attrs = std::make_shared<BgpPath>();
    attrs->setExtCommunities(std::move(communities));

    PolicyAttributesMask mask;
    mask.customizedLbwEnabled = false;

    auto peeringParams = buildEBgpPeeringParams();

    updateExtCommunitiesCommon(
        peeringParams, BgpSessionType::EBGP, &mask, attrs);

    /*
     * EBGP removes non-transitive communities (RFC 4360)
     * After pruning: 1 transitive non-LBW + 1 lowest transitive LBW
     */
    EXPECT_EQ(2, attrs->getExtCommunities()->size());
    auto counts = countCommunityTypes(attrs->getExtCommunities().get());
    EXPECT_EQ(1, counts.nonLbw);
    EXPECT_EQ(1, counts.transitiveLbw);
    EXPECT_EQ(0, counts.nonTransitiveLbw);
  }
}

/*
 * Test case 3b: EBGP + advertiseLinkBandwidth = AGGREGATE_LOCAL
 * Non-transitive LBW should be kept because advertiseLinkBandwidth is set
 * (keepNonTransitiveLbw = true when advertiseLinkBandwidth.has_value())
 * Use custom test data where non-transitive LBW is the lowest to demonstrate
 * that it's kept and selected as the lowest LBW
 */
TEST_F(
    UpdateExtCommunitiesCommonTest,
    EBgpKeepsNonTransitiveLbwWithAdvertiseLinkBandwidth) {
  // Create custom communities where non-transitive LBW is the lowest
  BgpAttrExtCommunitiesC communities;

  // Add 1 transitive non-LBW (route target)
  uint32_t rtHigh = (0x00 << 24) | (0x02 << 16) | 12345; // transitive
  uint32_t rtLow = 67890;
  communities.emplace_back(rtHigh, rtLow);

  // Add 1 non-transitive LBW with LOW value (50.0f - will be the lowest)
  BgpExtCommunityLinkBandWidthTypeC lbwNonTrans(12345, 50.0f);
  communities.emplace_back(lbwNonTrans);

  // Add 1 transitive LBW with HIGHER value (100.0f)
  uint32_t transLbwHigh = (0x00 << 24) | (0x04 << 16) | 12345;
  union {
    float floatVal;
    uint32_t intVal;
  } tmp{};
  tmp.floatVal = 100.0f;
  communities.emplace_back(transLbwHigh, tmp.intVal);

  auto attrs = std::make_shared<BgpPath>();
  attrs->setExtCommunities(std::move(communities));

  auto peeringParams = buildEBgpPeeringParams();
  peeringParams.advertiseLinkBandwidth =
      AdvertiseLinkBandwidth::AGGREGATE_LOCAL;

  updateExtCommunitiesCommon(
      peeringParams, BgpSessionType::EBGP, nullptr, attrs);

  /*
   * With advertiseLinkBandwidth set, non-transitive LBW is kept during EBGP
   * filtering and participates in LBW selection. Since 50.0f (non-transitive)
   * is lower than 100.0f (transitive), the non-transitive LBW is selected.
   * Result: 1 non-LBW + 1 non-transitive LBW = 2 communities
   */
  EXPECT_EQ(2, attrs->getExtCommunities()->size());
  auto counts = countCommunityTypes(attrs->getExtCommunities().get());
  EXPECT_EQ(1, counts.nonLbw);
  EXPECT_EQ(0, counts.transitiveLbw);
  EXPECT_EQ(1, counts.nonTransitiveLbw);
}

/*
 * Test case 4: IBGP or EBGP-Confed + (customizedLbwEnabled = false or mask =
 * nullptr) should prune to absolute lowest LBW (100.0f transitive).
 * Non-transitive LBWs (150.0f, 300.0f) are higher than 100.0f, so they are
 * removed. Result: 1 non-LBW + 1 transitive LBW = 2 communities.
 */
TEST_F(UpdateExtCommunitiesCommonTest, IBgpConfedKeepsAllLbwTypes) {
  // Test with IBGP peer
  {
    auto communities = buildTestExtCommunities();
    auto attrs = std::make_shared<BgpPath>();
    attrs->setExtCommunities(std::move(communities));

    auto peeringParams = buildIBgpPeeringParams();

    updateExtCommunitiesCommon(
        peeringParams, BgpSessionType::IBGP, nullptr, attrs);

    /*
     * IBGP keeps non-transitive communities but prunes LBW to absolute lowest
     * After pruning: 1 non-LBW + 1 lowest LBW (100.0f transitive)
     */
    EXPECT_EQ(2, attrs->getExtCommunities()->size());
    auto counts = countCommunityTypes(attrs->getExtCommunities().get());
    EXPECT_EQ(1, counts.nonLbw);
    EXPECT_EQ(1, counts.transitiveLbw);
    EXPECT_EQ(0, counts.nonTransitiveLbw);
  }

  // Test with ConfedEBGP peer
  {
    auto communities = buildTestExtCommunities();
    auto attrs = std::make_shared<BgpPath>();
    attrs->setExtCommunities(std::move(communities));

    auto peeringParams = buildConfedEBgpPeeringParams();

    updateExtCommunitiesCommon(
        peeringParams, BgpSessionType::ConfedEBGP, nullptr, attrs);

    /*
     * ConfedEBGP keeps non-transitive communities but prunes LBW to absolute
     * lowest. After pruning: 1 non-LBW + 1 lowest LBW (100.0f transitive)
     */
    EXPECT_EQ(2, attrs->getExtCommunities()->size());
    auto counts = countCommunityTypes(attrs->getExtCommunities().get());
    EXPECT_EQ(1, counts.nonLbw);
    EXPECT_EQ(1, counts.transitiveLbw);
    EXPECT_EQ(0, counts.nonTransitiveLbw);
  }

  // Test with customizedLbwEnabled = false (explicit mask)
  {
    auto communities = buildTestExtCommunities();
    auto attrs = std::make_shared<BgpPath>();
    attrs->setExtCommunities(std::move(communities));

    PolicyAttributesMask mask;
    mask.customizedLbwEnabled = false;

    auto peeringParams = buildIBgpPeeringParams();

    updateExtCommunitiesCommon(
        peeringParams, BgpSessionType::IBGP, &mask, attrs);

    // IBGP with explicit mask prunes LBW to absolute lowest
    EXPECT_EQ(2, attrs->getExtCommunities()->size());
    auto counts = countCommunityTypes(attrs->getExtCommunities().get());
    EXPECT_EQ(1, counts.nonLbw);
    EXPECT_EQ(1, counts.transitiveLbw);
    EXPECT_EQ(0, counts.nonTransitiveLbw);
  }
}

/*
 * Test that egress policy can add both transitive and non-transitive
 * link bandwidth extended communities, but only the transitive one
 * should pass through to EBGP peers (non-transitive is filtered).
 */
TEST_F(AdjRibOutboundFixture, EgressPolicyLinkBandwidthPropagationEBGPTest) {
  // Create extended communities: 100G transitive and 10G non-transitive
  auto extComm100GTransitive = createExtCommunity(
      0x00, // transitive
      0x04, // link bandwidth subtype
      "100G",
      "100G-transitive",
      "100G transitive link bandwidth");

  auto extComm10GNonTransitive = createExtCommunity(
      0x40, // non-transitive
      0x04, // link bandwidth subtype
      "10G",
      "10G-non-transitive",
      "10G non-transitive link bandwidth");

  // Create policy action to set these extended communities
  auto extCommAction = createBgpPolicyExtCommunityAction(
      bgp_policy::BgpAttrChangeActionType::EXT_COMMUNITY_LIST_SET,
      {extComm100GTransitive, extComm10GNonTransitive});

  // Create policy term with no match (matches all routes)
  auto term =
      createBgpPolicyTerm("add-lbw-communities", "", {}, {extCommAction});

  // Create policy manager
  const std::string policyName = kEgressPolicyName;
  const auto& policyConfig = createBgpPolicies(policyName, {term});
  auto policyManager = std::make_shared<PolicyManager>(
      policyConfig, createTestBgpGlobalConfig());

  // Setup EBGP peer
  TinyPeerInfo outPeer{
      folly::IPAddress("30.1.1.1"), 1, 2, BgpSessionType::EBGP, false};
  BgpPeerId outPeerId(outPeer.addr, outPeer.routerId);

  PeeringParams peeringParamsOutPeer{};
  peeringParamsOutPeer.peerAddr = outPeer.addr;
  peeringParamsOutPeer.localAs = kLocalAs1;
  peeringParamsOutPeer.remoteAs = kRemoteAs2; // Different AS for EBGP

  auto adjRibOutGroup = std::make_shared<AdjRibOutGroup>(evb_, "Group");
  AdjRib adjRibOutPeer{
      outPeerId,
      peeringParamsOutPeer,
      evb_,
      ribInQ_,
      observerQ_,
      std::make_shared<folly::coro::Baton>(),
      policyManager,
      std::make_shared<std::atomic<bool>>(false) /* isSafeModeOn */,
      std::nullopt, /* ingress policy name */
      policyName /* egress policy name */,
      adjRibOutGroup};
  adjRibOutPeer.remoteAs_ = kRemoteAs2;
  adjRibOutPeer.isAfiIpv4Negotiated_ = true;
  adjRibOutPeer.pathIdGenerator_ = std::make_unique<PathIdGenerator>(false);

  // Create input peer and attributes
  TinyPeerInfo inPeer{
      folly::IPAddress("20.1.1.1"), 1, 1, BgpSessionType::EBGP, false};

  /*
   * Build input attributes and add both transitive and non-transitive
   * communities
   */
  BgpUpdate2 inputUpdate = buildBgpUpdateAttributes(kV4Nexthop1);
  auto inAttrs = std::make_shared<facebook::bgp::BgpPath>(
      BgpPathFields(*BgpUpdate2toBgpPathC(inputUpdate)));

  // Add both communities to the input attributes using getBgpAttrExtCommunityC
  auto config = createConfigWithAsn(kLocalAs1);
  BgpAttrExtCommunitiesC extCommunities;
  extCommunities.emplace_back(
      getBgpAttrExtCommunityC(extComm100GTransitive, &config));
  extCommunities.emplace_back(
      getBgpAttrExtCommunityC(extComm10GNonTransitive, &config));
  inAttrs->setExtCommunities(extCommunities);
  inAttrs->publish();

  fm_->addTask([&] {
    // Create rib announcement
    RibOutAnnouncementEntry update{kV4Prefix1, kDefaultPathID, inPeer, inAttrs};

    // Process the announcement through the egress policy and EBGP filtering
    adjRibOutPeer.processRibAnnouncedEntry(update);

    // Verify the post-policy attributes
    auto adjRibEntry = adjRibOutPeer.getRibEntry(/*ingress=*/false, kV4Prefix1);
    ASSERT_NE(nullptr, adjRibEntry);
    auto outAttrs = adjRibEntry->getPostAttr();
    ASSERT_NE(nullptr, outAttrs);

    /*
     * For EBGP peers, only transitive extended communities should remain
     * So we should have 1 extended community (the 100G transitive one)
     * The 10G non-transitive should be filtered out by
     * updateExtCommunitiesCommon
     */
    auto outExtCommunities = outAttrs->getExtCommunities();
    EXPECT_FALSE(outExtCommunities.nullOrEmpty());
    EXPECT_EQ(1, outExtCommunities->size());

    // Verify that the remaining community is transitive
    for (const auto& extComm : outExtCommunities.get()) {
      EXPECT_TRUE(extComm.isTransitive());
    }
  });

  evb_.loop();
}

/*
 * Test that egress policy can add both transitive and non-transitive
 * link bandwidth extended communities, and for IBGP peers, non-transitive
 * communities are NOT filtered (unlike EBGP). However, LBW deduplication
 * still occurs, keeping only the lowest LBW value (10G non-transitive in
 * this case since 10G < 100G).
 */
TEST_F(AdjRibOutboundFixture, EgressPolicyLinkBandwidthPropagationIBGPTest) {
  // Create extended communities: 100G transitive and 10G non-transitive
  auto extComm100GTransitive = createExtCommunity(
      0x00, // transitive
      0x04, // link bandwidth subtype
      "100G",
      "100G-transitive",
      "100G transitive link bandwidth");

  auto extComm10GNonTransitive = createExtCommunity(
      0x40, // non-transitive
      0x04, // link bandwidth subtype
      "10G",
      "10G-non-transitive",
      "10G non-transitive link bandwidth");

  // Create policy action to set these extended communities
  auto extCommAction = createBgpPolicyExtCommunityAction(
      bgp_policy::BgpAttrChangeActionType::EXT_COMMUNITY_LIST_SET,
      {extComm100GTransitive, extComm10GNonTransitive});

  // Create policy term with no match (matches all routes)
  auto term =
      createBgpPolicyTerm("add-lbw-communities", "", {}, {extCommAction});

  // Create policy manager
  const std::string policyName = kEgressPolicyName;
  const auto& policyConfig = createBgpPolicies(policyName, {term});
  auto policyManager = std::make_shared<PolicyManager>(
      policyConfig, createTestBgpGlobalConfig());

  // Setup IBGP peer (same AS as local)
  TinyPeerInfo outPeer{
      folly::IPAddress("30.1.1.1"), 1, 2, BgpSessionType::IBGP, false};
  BgpPeerId outPeerId(outPeer.addr, outPeer.routerId);

  PeeringParams peeringParamsOutPeer{};
  peeringParamsOutPeer.peerAddr = outPeer.addr;
  peeringParamsOutPeer.localAs = kLocalAs1;
  peeringParamsOutPeer.remoteAs = kLocalAs1; // Same AS for IBGP

  auto adjRibOutGroup = std::make_shared<AdjRibOutGroup>(evb_, "Group");
  AdjRib adjRibOutPeer{
      outPeerId,
      peeringParamsOutPeer,
      evb_,
      ribInQ_,
      observerQ_,
      std::make_shared<folly::coro::Baton>(),
      policyManager,
      std::make_shared<std::atomic<bool>>(false) /* isSafeModeOn */,
      std::nullopt, /* ingress policy name */
      policyName /* egress policy name */,
      adjRibOutGroup};
  adjRibOutPeer.remoteAs_ = kLocalAs1;
  adjRibOutPeer.isAfiIpv4Negotiated_ = true;
  adjRibOutPeer.pathIdGenerator_ = std::make_unique<PathIdGenerator>(false);

  // Create input peer and attributes
  TinyPeerInfo inPeer{
      folly::IPAddress("20.1.1.1"), 1, 1, BgpSessionType::IBGP, false};

  /*
   * Build input attributes and add both transitive and non-transitive
   * communities
   */
  BgpUpdate2 inputUpdate = buildBgpUpdateAttributes(kV4Nexthop1);
  auto inAttrs = std::make_shared<facebook::bgp::BgpPath>(
      BgpPathFields(*BgpUpdate2toBgpPathC(inputUpdate)));

  // Add both communities to the input attributes using getBgpAttrExtCommunityC
  auto config = createConfigWithAsn(kLocalAs1);
  BgpAttrExtCommunitiesC extCommunities;
  extCommunities.emplace_back(
      getBgpAttrExtCommunityC(extComm100GTransitive, &config));
  extCommunities.emplace_back(
      getBgpAttrExtCommunityC(extComm10GNonTransitive, &config));
  inAttrs->setExtCommunities(extCommunities);
  inAttrs->publish();

  fm_->addTask([&] {
    // Create rib announcement
    RibOutAnnouncementEntry update{kV4Prefix1, kDefaultPathID, inPeer, inAttrs};

    // Process the announcement through the egress policy and IBGP filtering
    adjRibOutPeer.processRibAnnouncedEntry(update);

    // Verify the post-policy attributes
    auto adjRibEntry = adjRibOutPeer.getRibEntry(/*ingress=*/false, kV4Prefix1);
    ASSERT_NE(nullptr, adjRibEntry);
    auto outAttrs = adjRibEntry->getPostAttr();
    ASSERT_NE(nullptr, outAttrs);

    /*
     * For IBGP peers, non-transitive extended communities are NOT filtered out
     * (unlike EBGP where they would be removed per RFC 4360). However, LBW
     * deduplication still occurs, keeping only the lowest LBW value.
     * Since 10G < 100G, the 10G non-transitive LBW is kept.
     * Result: 1 extended community (the 10G non-transitive one)
     */
    auto outExtCommunities = outAttrs->getExtCommunities();
    EXPECT_FALSE(outExtCommunities.nullOrEmpty());
    EXPECT_EQ(1, outExtCommunities->size());

    // Verify that the remaining community is non-transitive LBW
    for (const auto& extComm : outExtCommunities.get()) {
      EXPECT_TRUE(extComm.isNonTransitiveLinkBandwidthCommunity());
    }
  });

  evb_.loop();
}

} // namespace facebook::bgp
