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

#define AdjRib_TEST_FRIENDS                                                   \
  friend class AdjRibInboundFixture;                                          \
  FRIEND_TEST(AdjRibInboundFixture, ReceivedPathIdReachesRib);                \
  FRIEND_TEST(AdjRibInboundFixture, AdjRibStatsBasicTest);                    \
  FRIEND_TEST(AdjRibInboundFixture, ConsecutiveV4Withdraw);                   \
  FRIEND_TEST(AdjRibInboundFixture, VerifyUpdateAttributesIn);                \
  FRIEND_TEST(                                                                \
      AdjRibInboundFixture, VerifyWithdrawalBeforeStalePathExpiryAddPath);    \
  FRIEND_TEST(AdjRibInboundFixture, VerifyGRRestartStalePathCleanup);         \
  FRIEND_TEST(                                                                \
      AdjRibInboundFixture, PromoteStaleRibInEntryIfExistsTest_NoMatch);      \
  FRIEND_TEST(                                                                \
      AdjRibInboundFixture,                                                   \
      PromoteStaleRibInEntryIfExistsTest_AddPathStaleRemoval);                \
  FRIEND_TEST(                                                                \
      AdjRibInboundFixture,                                                   \
      PromoteStaleRibInEntryIfExistsTest_AddPathUpdates);                     \
  FRIEND_TEST(                                                                \
      AdjRibInboundFixture, PromoteStaleRibInEntryIfExistsTest_NonAddPath);   \
  FRIEND_TEST(                                                                \
      AdjRibInboundFixture,                                                   \
      PromoteStaleRibInEntryIfExistsInPlaceTest_NoMatch);                     \
  FRIEND_TEST(                                                                \
      AdjRibInboundFixture,                                                   \
      PromoteStaleRibInEntryIfExistsInPlaceTest_AddPathClears);               \
  FRIEND_TEST(                                                                \
      AdjRibInboundFixture,                                                   \
      PromoteStaleRibInEntryIfExistsInPlaceTest_NonAddPath);                  \
  FRIEND_TEST(                                                                \
      AdjRibInboundFixture, CleanupStaleRoutesInPlaceTest_NoStaleEntries);    \
  FRIEND_TEST(AdjRibInboundFixture, CleanupStaleRoutesInPlaceTest_AddPath);   \
  FRIEND_TEST(                                                                \
      AdjRibInboundFixture, CleanupStaleRoutesInPlaceTest_NonAddPathCase);    \
  FRIEND_TEST(                                                                \
      AdjRibInboundFixture, CleanupStaleRoutes_NoPostAttrCounterUnderflow);   \
  FRIEND_TEST(AdjRibInboundFixture, VerifyGRRestartStalePathRestore);         \
  FRIEND_TEST(AdjRibInboundFixture, VerifyGRRestartStalePathRestoreAddPath);  \
  FRIEND_TEST(AdjRibInboundFixture, VerifyGRRestartStalePathShuffleAddPath);  \
  FRIEND_TEST(AdjRibInboundFixture, V4UpdateProcessingMultipleWithAddPath);   \
  FRIEND_TEST(AdjRibInboundFixture, V6UpdateProcessingMultipleWithAddPath);   \
  FRIEND_TEST(AdjRibInboundFixture, V4UpdatePolicyProcessing);                \
  FRIEND_TEST(AdjRibInboundFixture, VerifyStalePathCleanupWithEoR);           \
  FRIEND_TEST(AdjRibInboundFixture, VerifyDenyAfterPermitAddpath);            \
  FRIEND_TEST(AdjRibInboundFixture, SetRouteFilterStatementTest);             \
  FRIEND_TEST(AdjRibInboundFixture, SetRouteFilterStatementIngressTest);      \
  FRIEND_TEST(AdjRibInboundFixture, VerifyRouteFilterPolicyDeny);             \
  FRIEND_TEST(AdjRibInboundFixture, VerifyRouteFilterPolicyAllow);            \
  FRIEND_TEST(                                                                \
      AdjRibInboundFixture,                                                   \
      VerifyRouteFilterPolicyReEvaluationWithBatchProcessing);                \
  FRIEND_TEST(                                                                \
      AdjRibInboundFixture, VerifyRouteFilterPolicyReEvaluationWithGR);       \
  FRIEND_TEST(AdjRibInboundFixture, AdjRibInQueueBackpressureTest);           \
  FRIEND_TEST(AdjRibInboundFixture, AdjRibInQueueConsumerScopeTest);          \
  FRIEND_TEST(                                                                \
      AdjRibInboundFixture, AdjRibInQueueConsumerScopeExceptionPathTest);     \
  FRIEND_TEST(AdjRibInboundFixture, MarkLearntRoutesStaleInPlaceTest);        \
  FRIEND_TEST(                                                                \
      AdjRibInboundFixture, MarkLearntRoutesStaleInPlaceTest_NonAddPath);     \
  FRIEND_TEST(                                                                \
      AdjRibProcessPeerAnnouncedFixture,                                      \
      MaybeAnnouncePrefixTest_PolicyBlockedNewPrefix);                        \
  FRIEND_TEST(                                                                \
      AdjRibProcessPeerAnnouncedFixture,                                      \
      MaybeAnnouncePrefixTest_PolicyBlockedOldPrefix);                        \
  FRIEND_TEST(                                                                \
      AdjRibProcessPeerAnnouncedFixture,                                      \
      MaybeAnnouncePrefixTest_OldPrefixNoPathChange);                         \
  FRIEND_TEST(                                                                \
      AdjRibProcessPeerAnnouncedFixture,                                      \
      MaybeAnnouncePrefixTest_BackupAddressChange);                           \
  FRIEND_TEST(                                                                \
      AdjRibProcessPeerAnnouncedFixture,                                      \
      MaybeAnnouncePrefixTest_PolicyAcceptedPathChange);                      \
  FRIEND_TEST(                                                                \
      AdjRibProcessPeerAnnouncedFixture,                                      \
      MaybeAnnouncePrefixTest_PolicyAcceptedNewRoute);                        \
  FRIEND_TEST(                                                                \
      AdjRibProcessPeerAnnouncedFixture,                                      \
      MaybeAnnouncePrefixTest_CappedNewRoute);                                \
  FRIEND_TEST(                                                                \
      AdjRibProcessPeerAnnouncedFixture, CanAddRibEntryTest_WithinLimit);     \
  FRIEND_TEST(                                                                \
      AdjRibProcessPeerAnnouncedFixture, CanAddRibEntryTest_CappedNewRoute);  \
  FRIEND_TEST(                                                                \
      AdjRibProcessPeerAnnouncedFixture, CanAddRibEntryTest_PrefixOverload);  \
  FRIEND_TEST(                                                                \
      AdjRibProcessPeerAnnouncedFixture,                                      \
      GetPostInPolicyAttributesTest_UnexpectedNullAttrs);                     \
  FRIEND_TEST(                                                                \
      AdjRibProcessPeerAnnouncedFixture,                                      \
      GetPostInPolicyAttributesTest_NoPolicyConfigured);                      \
  FRIEND_TEST(                                                                \
      AdjRibProcessPeerAnnouncedFixture,                                      \
      GetPostInPolicyAttributesTest_RejectedByPolicy);                        \
  FRIEND_TEST(                                                                \
      AdjRibProcessPeerAnnouncedFixture,                                      \
      GetPostInPolicyAttributesTest_AcceptedByPolicy);                        \
  FRIEND_TEST(                                                                \
      AdjRibProcessPeerAnnouncedFixture,                                      \
      GetPostInPolicyAttributesTest_RejectedByInvalidGarWeights);             \
  FRIEND_TEST(                                                                \
      AdjRibInboundFixture, VerifyPolicyReEvaluationWithSessionTermination);  \
  FRIEND_TEST(AdjRibInboundFixture, VerifyPolicyReEvaluationSynchronization); \
  FRIEND_TEST(AdjRibInboundFixture, CollectStaleRoutes_EmptyReturnsNullopt);  \
  FRIEND_TEST(                                                                \
      AdjRibInboundFixture,                                                   \
      CollectStaleRoutes_ReturnsWithdrawalAndClearsState);                    \
  FRIEND_TEST(AdjRibInboundFixture, StopDrainsPendingRibInPushes);            \
  FRIEND_TEST(AdjRibInboundFixture, StopBlocksUntilPendingPushCompletes);     \
  FRIEND_TEST(AdjRibInboundFixture, SchedulePendingRibInPushTrimsReady);      \
  FRIEND_TEST(AdjRibInboundFixture, PushStaleWithdrawalPushesToRibInQ);       \
  FRIEND_TEST(                                                                \
      AdjRibProcessPeerAnnouncedFixture,                                      \
      PreFilterDroppedRouteCountIncrementsOnCap);                             \
  FRIEND_TEST(                                                                \
      AdjRibProcessPeerAnnouncedFixture,                                      \
      PreFilterDroppedRouteCountClearedOnSessionDown);

#define AdjRibStats_TEST_FRIENDS                                               \
  friend class AdjRibInboundFixture;                                           \
  FRIEND_TEST(AdjRibInboundFixture, V4UpdateProcessingSingle);                 \
  FRIEND_TEST(                                                                 \
      AdjRibInboundFixture,                                                    \
      CheckLimitAndAlarmWarningOnlyWithWarningLimitTest);                      \
  FRIEND_TEST(AdjRibInboundFixture, VipInjectorPrefixCount);                   \
  FRIEND_TEST(AdjRibInboundFixture, VerifyAnnouncementsWithRouteFilterPolicy); \
  FRIEND_TEST(AdjRibInboundFixture, VerifyRouteFilterPolicyDeny);              \
  FRIEND_TEST(AdjRibInboundFixture, VerifyRouteFilterPolicyAllow);             \
  FRIEND_TEST(                                                                 \
      AdjRibInboundFixture,                                                    \
      VerifyRouteFilterPolicyReEvaluationWithBatchProcessing);                 \
  FRIEND_TEST(                                                                 \
      AdjRibProcessPeerAnnouncedFixture,                                       \
      MaybeAnnouncePrefixTest_PolicyBlockedNewPrefix);                         \
  FRIEND_TEST(                                                                 \
      AdjRibProcessPeerAnnouncedFixture,                                       \
      MaybeAnnouncePrefixTest_PolicyAcceptedPathChange);                       \
  FRIEND_TEST(                                                                 \
      AdjRibProcessPeerAnnouncedFixture,                                       \
      MaybeAnnouncePrefixTest_PolicyAcceptedNewRoute);                         \
  FRIEND_TEST(                                                                 \
      AdjRibProcessPeerAnnouncedFixture,                                       \
      MaybeAnnouncePrefixTest_CappedNewRoute);                                 \
  FRIEND_TEST(                                                                 \
      AdjRibProcessPeerAnnouncedFixture, CanAddRibEntryTest_CappedNewRoute);

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/IPAddress.h>
#include <folly/Overload.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Invoke.h>
#include <folly/futures/Promise.h>
#include <folly/io/async/EventBase.h>
#include <folly/logging/xlog.h>

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"
#include "neteng/fboss/bgp/cpp/adjrib/facebook/RouteFilterLogger.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/common/Utils.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyManager.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/AdjRibInUtils.h"
#include "neteng/fboss/bgp/cpp/tests/BoundedWaitUtils.h"
#include "neteng/fboss/bgp/cpp/tests/MockScubaData.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibPolicyUtils.h"

using namespace facebook::bgp;
using namespace facebook::nettools::bgplib;
using namespace facebook::neteng::fboss::bgp::thrift;
using namespace facebook::neteng::fboss::bgp_attr;
using namespace folly::fibers;

using folly::IPAddress;

using bgp_policy::BgpPolicyActionType;

namespace facebook::bgp {

using namespace ::testing;
using ::testing::ElementsAre;

/******************************************************************************
 *      START   -   BGP Update message processing tests.                      *
 ******************************************************************************/

// Ensure that a single BGPupdate2 with v4 only fields is properly processed
TEST_F(AdjRibInboundFixture, V4UpdateProcessingSingle) {
  setupAdjRib(kLongGrRestartTime, kLongGrRestartTime);
  auto acceptedBefore = totalAcceptedPrefixCount;

  fm_->addTask([&] {
    auto update = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    // Verify rib In message
    auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));
    auto announcement = std::get<RibInAnnouncement>(msg);
    EXPECT_EQ(kPeerAddr1, announcement.peer.addr);
    PrefixPathIds prefixSet{{kV4Prefix1, kDefaultPathID}};
    EXPECT_EQ(prefixSet, announcement.pfxPathIds);
    EXPECT_EQ(kV4Nexthop1, announcement.attrs->getNexthop());

    // Verify RIB entry is created. Match various fields from input
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1));
    EXPECT_NE(nullptr, adjRibEntry);
    ASSERT_NE(nullptr, adjRibEntry->getPreIn());
    ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
    EXPECT_TRUE(adjRibEntry->getPreIn()->isPublished());
    EXPECT_TRUE(adjRibEntry->getPostAttr()->isPublished());
    EXPECT_EQ(kMed, adjRibEntry->getPreIn()->getMed());
    EXPECT_EQ(kV4Nexthop1, adjRibEntry->getPreIn()->getNexthop());

    // Verify AdjRibLiteTree size is non-zero
    EXPECT_EQ(
        1,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/false));
    // Verify AdjRibTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

    // Verify stats
    EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(1, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(2, adjRib_->getStats().getTotalAttributeUpdates());

    // Negative test, ensure that VIP counters are not increasing
    EXPECT_EQ(0, totalVipPrefixesCount);

    // Verify global accepted prefix counter incremented
    EXPECT_EQ(acceptedBefore + 1, totalAcceptedPrefixCount);

    terminateAdjRib();
  });

  evb_.loop();
}

// Ensure that a single BGPupdate2 with v4 only fields is properly processed
TEST_F(AdjRibInboundFixture, RedistributePeerUpdate) {
  setupAdjRibForRedistributePeer();

  fm_->addTask([&] {
    {
      auto update = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
      adjRibInQ_->fiberPush(std::move(update));

      // Verify rib In message
      auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));
      auto announcement = std::get<RibInAnnouncement>(msg);
      EXPECT_EQ(kPeerAddr1, announcement.peer.addr);
      EXPECT_TRUE(announcement.peer.isRedistributePeer);
    }

    {
      auto update = createV4BgpUpdateSingleWithdraw(kV4Prefix1);
      adjRibInQ_->fiberPush(std::move(update));

      // Verify rib In message
      auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(msg));
      auto withdrawal = std::get<RibInWithdrawal>(msg);
      EXPECT_EQ(kPeerAddr1, withdrawal.peer.addr);
      EXPECT_TRUE(withdrawal.peer.isRedistributePeer);
    }

    terminateAdjRib();
  });

  evb_.loop();
}

// Test last update received time
TEST_F(AdjRibInboundFixture, LastUpdateRcvdTime) {
  setupAdjRib(kLongGrRestartTime, kLongGrRestartTime);

  fm_->addTask([&] {
    // create update1 for prefix1
    auto update1 =
        createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1, kMed);
    // update2 is exactly same as update1
    auto update2 =
        createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1, kMed);
    // create update3 for prefix2
    auto update3 =
        createV4BgpUpdateSingleAnnounce(kV4Prefix2, kV4Nexthop1, kMed);
    // update4 modifies attributes of prefix2
    auto update4 =
        createV4BgpUpdateSingleAnnounce(kV4Prefix2, kV4Nexthop1, kMed2);

    /*
     * Put 4 updates into the adjRibInQ.
     *
     * For prefix1, since update1 and update2 are the same, we expect only the
     * first one to be sent to RIB.
     *
     * For prefix2, since update3 and update4 are different, we expect both to
     * be sent to RIB. Moreover, when we query lastUpdateRcvd timestamp, we
     * should see the time modification for prefix2, but not for prefix1.
     */
    adjRibInQ_->fiberPush(std::move(update1));
    adjRibInQ_->fiberPush(std::move(update2));
    adjRibInQ_->fiberPush(std::move(update3));
    adjRibInQ_->fiberPush(std::move(update4));
  });

  fm_->addTask([&] {
    uint64_t prefix1LastModified{0};
    uint64_t prefix2LastModified{0};

    {
      // Verify RibInUpdate for kV4Prefix1
      auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      auto announcement = std::get<RibInAnnouncement>(msg);
      EXPECT_EQ(1, announcement.pfxPathIds.size());
      auto prefix = std::get<0>(announcement.pfxPathIds[0]);
      EXPECT_EQ(prefix, kV4Prefix1);
      auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, prefix);
      prefix1LastModified = adjRibEntry->getLastUpdateRcvdTime();
    }
    {
      // Verify the first RibInUpdate for kV4Prefix2
      auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      auto announcement = std::get<RibInAnnouncement>(msg);
      EXPECT_EQ(1, announcement.pfxPathIds.size());
      auto prefix = std::get<0>(announcement.pfxPathIds[0]);
      EXPECT_EQ(prefix, kV4Prefix2);
    }
    {
      // Verify the second RibInUpdate for kV4Prefix2
      auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      auto announcement = std::get<RibInAnnouncement>(msg);
      EXPECT_EQ(1, announcement.pfxPathIds.size());
      auto prefix = std::get<0>(announcement.pfxPathIds[0]);
      EXPECT_EQ(prefix, kV4Prefix2);
      auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, prefix);
      prefix2LastModified = adjRibEntry->getLastUpdateRcvdTime();
    }

    /*
     * Validate difference of last modified time.
     *
     * NOTE: it will be hard to validate the different timestamp between 2
     * updates from prefix2 due to the fact that adjRibEntry timestamp is
     * updated BEFORE the time when ribInQ received the updates.
     */
    EXPECT_LT(prefix1LastModified, prefix2LastModified);

    terminateAdjRib();
  });

  evb_.loop();
}

// Ensure that a BGPupdate2 with multiple v4 only fields is properly processed
TEST_F(AdjRibInboundFixture, V4UpdateProcessingMultiple) {
  setupAdjRib();
  std::vector<folly::CIDRNetwork> prefixSet{kV4Prefix1, kV4Prefix2};
  PrefixPathIds pfxPathIds{
      {kV4Prefix1, kDefaultPathID}, {kV4Prefix2, kDefaultPathID}};

  fm_->addTask([&] {
    auto update = createV4BgpUpdateMultipleAnnounce(prefixSet);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    // Verify rib In message
    auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));
    auto announcement = std::get<RibInAnnouncement>(msg);
    EXPECT_EQ(kPeerAddr1, announcement.peer.addr);
    EXPECT_EQ(pfxPathIds, announcement.pfxPathIds);
    EXPECT_EQ(kV4Nexthop1, announcement.attrs->getNexthop());

    for (auto& prefix : prefixSet) {
      /*
       * Verify multiple RIB entries are created.
       * Match various fields from input
       */
      auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, prefix);
      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/false, prefix));
      EXPECT_NE(nullptr, adjRibEntry);
      ASSERT_NE(nullptr, adjRibEntry->getPreIn());
      ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
      // Token check few parameters
      EXPECT_TRUE(adjRibEntry->getPreIn()->isPublished());
      EXPECT_TRUE(adjRibEntry->getPostAttr()->isPublished());
      EXPECT_EQ(kMed, adjRibEntry->getPreIn()->getMed());
      EXPECT_EQ(kLocalPref, adjRibEntry->getPreIn()->getLocalPref());
      EXPECT_EQ(kV4Nexthop1, adjRibEntry->getPreIn()->getNexthop());
    }

    // Verify both routes share same attribute pointer
    auto adjRibEntry1 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    auto adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
    // Shallow compare
    EXPECT_EQ(adjRibEntry1->getPreIn(), adjRibEntry2->getPreIn());
    EXPECT_EQ(adjRibEntry1->getPostAttr(), adjRibEntry2->getPostAttr());

    // Verify AdjRibLiteTree size is non-zero
    EXPECT_EQ(
        2,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/false));
    // Verify AdjRibTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

    EXPECT_EQ(2, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(2, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(1, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(4, adjRib_->getStats().getTotalAttributeUpdates());

    terminateAdjRib();
  });

  evb_.loop();
}

/**
 * Send multiple prefixes in different messages, each with exact same
 * attributes. Verify PostIn attr pointer is same for all of them
 */
TEST_F(AdjRibInboundFixture, V4UpdateProcessingMultipleInMultipleMsgs) {
  setupAdjRib();
  std::vector<folly::CIDRNetwork> prefixSet1{kV4Prefix1, kV4Prefix2};
  PrefixPathIds pfxPathIds1{
      {kV4Prefix1, kDefaultPathID}, {kV4Prefix2, kDefaultPathID}};
  std::vector<folly::CIDRNetwork> prefixSet2{kV4Prefix3, kV4Prefix4};
  PrefixPathIds pfxPathIds2{
      {kV4Prefix3, kDefaultPathID}, {kV4Prefix4, kDefaultPathID}};

  fm_->addTask([&] {
    auto update = createV4BgpUpdateMultipleAnnounce(prefixSet1);
    adjRibInQ_->fiberPush(std::move(update));
    update = createV4BgpUpdateMultipleAnnounce(prefixSet2);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    // Verify rib In message
    auto msg1 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg1));
    auto announcement1 = std::get<RibInAnnouncement>(msg1);
    EXPECT_EQ(kPeerAddr1, announcement1.peer.addr);
    EXPECT_EQ(pfxPathIds1, announcement1.pfxPathIds);
    EXPECT_EQ(kV4Nexthop1, announcement1.attrs->getNexthop());

    auto msg2 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg2));
    auto announcement2 = std::get<RibInAnnouncement>(msg2);
    EXPECT_EQ(kPeerAddr1, announcement2.peer.addr);
    EXPECT_EQ(pfxPathIds2, announcement2.pfxPathIds);
    EXPECT_EQ(kV4Nexthop1, announcement2.attrs->getNexthop());

    for (auto& prefix : prefixSet1) {
      /*
       * Verify multiple RIB entries are created.
       * Match various fields from input
       */
      auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, prefix);
      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/false, prefix));
      EXPECT_NE(nullptr, adjRibEntry);
      ASSERT_NE(nullptr, adjRibEntry->getPreIn());
      ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
      // Token check few parameters
      EXPECT_TRUE(adjRibEntry->getPreIn()->isPublished());
      EXPECT_TRUE(adjRibEntry->getPostAttr()->isPublished());
      EXPECT_EQ(kMed, adjRibEntry->getPreIn()->getMed());
      EXPECT_EQ(kLocalPref, adjRibEntry->getPreIn()->getLocalPref());
      EXPECT_EQ(kV4Nexthop1, adjRibEntry->getPreIn()->getNexthop());
    }

    // Verify both routes share same attribute pointer
    auto adjRibEntry1 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    auto adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
    // Shallow compare
    EXPECT_EQ(adjRibEntry1->getPreIn(), adjRibEntry2->getPreIn());
    EXPECT_EQ(adjRibEntry1->getPostAttr(), adjRibEntry2->getPostAttr());

    for (auto& prefix : prefixSet2) {
      /*
       * Verify multiple RIB entries are created.
       * Match various fields from input
       */
      auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, prefix);
      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/false, prefix));
      EXPECT_NE(nullptr, adjRibEntry);
      ASSERT_NE(nullptr, adjRibEntry->getPreIn());
      ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
      // Token check few parameters
      EXPECT_TRUE(adjRibEntry->getPreIn()->isPublished());
      EXPECT_TRUE(adjRibEntry->getPostAttr()->isPublished());
      EXPECT_EQ(kMed, adjRibEntry->getPreIn()->getMed());
      EXPECT_EQ(kLocalPref, adjRibEntry->getPreIn()->getLocalPref());
      EXPECT_EQ(kV4Nexthop1, adjRibEntry->getPreIn()->getNexthop());
    }

    // Verify both routes share same attribute pointer
    auto adjRibEntry3 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix3);
    auto adjRibEntry4 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix4);

    // Shallow compare
    EXPECT_EQ(adjRibEntry3->getPreIn(), adjRibEntry4->getPreIn());
    EXPECT_EQ(adjRibEntry3->getPostAttr(), adjRibEntry4->getPostAttr());

    // Shallow compare
    EXPECT_EQ(adjRibEntry1->getPostAttr(), adjRibEntry4->getPostAttr());
    EXPECT_EQ(adjRibEntry2->getPostAttr(), adjRibEntry3->getPostAttr());

    // Verify AdjRibLiteTree size is non-zero
    EXPECT_EQ(
        4,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/false));
    // Verify AdjRibTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

    EXPECT_EQ(4, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(4, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(2, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(2, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(8, adjRib_->getStats().getTotalAttributeUpdates());

    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * Ensure that a BGPupdate2 with multiple v4 only fields is properly processed
 * with addpath feature.
 */
TEST_F(AdjRibInboundFixture, V4UpdateProcessingMultipleWithAddPath) {
  setupAdjRib();
  std::vector<folly::CIDRNetwork> prefixSet{kV4Prefix1, kV4Prefix2};
  auto prefixSetAdjOutPathIds1 =
      PrefixPathIds{{kV4Prefix1, 1}, {kV4Prefix2, 1}};
  auto prefixSetAdjOutPathIds2 =
      PrefixPathIds{{kV4Prefix1, 2}, {kV4Prefix2, 2}};
  auto prefixSetAdjOutPathIds3 =
      PrefixPathIds{{kV4Prefix1, 3}, {kV4Prefix2, 3}};
  folly::fibers::Baton bt1, bt2, bt3, bt4, bt5, bt6, bt7;

  fm_->addTask([&] {
    bt1.wait();
    adjRib_->recAddPath_ = true;
    auto update = createV4BgpUpdateMultipleAnnounce(prefixSet);
    for (auto& rigPrefix : *update->v4Announced2()) {
      rigPrefix.pathId() = 1;
    }
    adjRibInQ_->fiberPush(std::move(update));

    bt2.wait();
    update = createV4BgpUpdateMultipleAnnounce(prefixSet);
    for (auto& rigPrefix : *update->v4Announced2()) {
      rigPrefix.pathId() = 2;
    }
    *update->attrs()->nexthop() = kV4Nexthop2.str();
    adjRibInQ_->fiberPush(std::move(update));

    bt3.wait();
    update = createV4BgpUpdateMultipleAnnounce(prefixSet);
    for (auto& rigPrefix : *update->v4Announced2()) {
      rigPrefix.pathId() = 1;
    }
    *update->attrs()->nexthop() = kV4Nexthop3.str();
    adjRibInQ_->fiberPush(std::move(update));

    bt4.wait();
    update = createV4BgpUpdateMultipleAnnounce(prefixSet);
    for (auto& rigPrefix : *update->v4Announced2()) {
      rigPrefix.pathId() = 3;
    }
    *update->attrs()->nexthop() = kV4Nexthop1.str();
    adjRibInQ_->fiberPush(std::move(update));

    bt5.wait();
    update = createV4BgpUpdateSingleWithdraw(kV4Prefix1, 2);
    adjRibInQ_->fiberPush(std::move(update));

    bt6.wait();
    update = createV4BgpUpdateSingleWithdraw(kV4Prefix1, 1);
    adjRibInQ_->fiberPush(std::move(update));

    bt7.wait();
    update = createV4BgpUpdateSingleWithdraw(kV4Prefix1, 3);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    bt1.post();
    /*
     * peer announced kV4Prefix1, kV4Prefix2 with same attr and nexthop
     * kV4Nexthop1 and path id 1
     * here we verify adjrib send ribannouncement for these two prefix with
     * kV4Nexthop1
     */
    auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));
    auto announcement = std::get<RibInAnnouncement>(msg);
    EXPECT_EQ(kPeerAddr1, announcement.peer.addr);
    EXPECT_EQ(prefixSetAdjOutPathIds1, announcement.pfxPathIds);
    EXPECT_EQ(kV4Nexthop1, announcement.attrs->getNexthop());

    // Verify AdjRibLiteTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/false));
    // Verify AdjRibTree size is 2
    EXPECT_EQ(
        2,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

    EXPECT_EQ(2, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(2, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(1, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(4, adjRib_->getStats().getTotalAttributeUpdates());

    /*
     * peer announced kV4Prefix1, kV4Prefix2 with same attr and nexthop
     * kV4Nexthop2 and path id 2
     * here we verify adjrib send ribannouncement for these two prefix with
     * kV4Nexthop2
     */
    bt2.post();
    // Verify rib In message
    auto msg2 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg2));
    auto announcement2 = std::get<RibInAnnouncement>(msg2);
    EXPECT_EQ(kPeerAddr1, announcement2.peer.addr);
    EXPECT_EQ(prefixSetAdjOutPathIds2, announcement2.pfxPathIds);
    EXPECT_EQ(kV4Nexthop2, announcement2.attrs->getNexthop());

    // Verify AdjRibLiteTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/false));
    /*
     * Verify AdjRibTree size is 2
     * Radix tree keys prefx and hence tree size still would be 2
     * Multiple paths for a prefix does not add size to the tree
     */
    EXPECT_EQ(
        2,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

    EXPECT_EQ(4, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(4, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(2, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(2, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(8, adjRib_->getStats().getTotalAttributeUpdates());

    /*
     * peer replace path 1 for kV4Prefix1 and kV4Prefix2. The new nexthop is
     * kV4Nexthop3.
     * here we verify adjrib send ribannouncement for updating these two prefix
     * with kV4Nexthop3.
     */
    bt3.post();
    // Verify rib In message
    auto msg3 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg3));
    auto announcement3 = std::get<RibInAnnouncement>(msg3);
    EXPECT_EQ(kPeerAddr1, announcement3.peer.addr);
    EXPECT_EQ(prefixSetAdjOutPathIds1, announcement3.pfxPathIds);
    EXPECT_EQ(kV4Nexthop3, announcement3.attrs->getNexthop());

    // Verify AdjRibLiteTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/false));
    /*
     * Verify AdjRibTree size should still remain 2 because both
     * the prefixes are still present in the radix tree
     */
    EXPECT_EQ(
        2,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

    EXPECT_EQ(4, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(4, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(3, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(3, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(12, adjRib_->getStats().getTotalAttributeUpdates());

    bt4.post();
    /*
     * peer announced kV4Prefix1, kV4Prefix2 with same attr and nexthop
     * kV4Nexthop1 and path id 3
     * here we verify adjrib send ribannouncement for these two prefix with
     * kV4Nexthop1
     */
    auto msg4 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    auto announcement4 = std::get<RibInAnnouncement>(msg4);
    EXPECT_EQ(kPeerAddr1, announcement4.peer.addr);
    EXPECT_EQ(prefixSetAdjOutPathIds3, announcement4.pfxPathIds);
    EXPECT_EQ(kV4Nexthop1, announcement4.attrs->getNexthop());

    // Verify AdjRibLiteTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/false));
    /*
     * Verify AdjRibTree size is still 2
     * Because announcement is for same prefix with different path
     */
    EXPECT_EQ(
        2,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

    EXPECT_EQ(6, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(6, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(4, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(4, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(16, adjRib_->getStats().getTotalAttributeUpdates());

    /*
     * peer withdraws path 2 for kV4Prefix1.
     * adjrib will send Ribwithdraw msg for  kV4Prefix1 with kV4Nexthop2
     */
    bt5.post();
    auto msg5 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    auto withdrawal = std::get<RibInWithdrawal>(msg5);
    EXPECT_EQ(kPeerAddr1, withdrawal.peer.addr);
    auto prefixSetAdjOutWithdrawal = PrefixPathIds{{kV4Prefix1, 2}};
    EXPECT_EQ(prefixSetAdjOutWithdrawal, withdrawal.pfxPathIds);

    // Verify AdjRibLiteTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/false));
    /*
     * Verify AdjRibTree size still remains 2
     * One path for one kV4Prefix1 is withdrawn but other paths
     * for that prefix still exist
     */
    EXPECT_EQ(
        2,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

    EXPECT_EQ(5, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(5, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(5, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(4, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(1, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(16, adjRib_->getStats().getTotalAttributeUpdates());

    /*
     * peer withdraws path 1 for kV4Prefix1.
     * adjrib will send Ribwithdraw msg for  kV4Prefix1 with kV4Nexthop3
     */
    bt6.post();
    auto msg6 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    auto withdrawal2 = std::get<RibInWithdrawal>(msg6);
    EXPECT_EQ(kPeerAddr1, withdrawal2.peer.addr);
    auto prefixSetAdjOutWithdrawal2 = PrefixPathIds{{kV4Prefix1, 1}};
    EXPECT_EQ(prefixSetAdjOutWithdrawal2, withdrawal2.pfxPathIds);

    // Verify AdjRibLiteTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/false));
    /*
     * Verify AdjRibTree size still should be 2
     * kV4Prefix1 still has non-withdrawn path and hence kV4Prefix1
     * is still there in the radix tree
     */
    EXPECT_EQ(
        2,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

    EXPECT_EQ(4, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(4, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(6, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(4, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(2, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(16, adjRib_->getStats().getTotalAttributeUpdates());

    /*
     * peer withdraws path 3 for kV4Prefix1.
     * adjrib will send Ribwithdraw msg for  kV4Prefix1 with kV4Nexthop1
     */
    bt7.post();
    auto msg7 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    auto withdrawal3 = std::get<RibInWithdrawal>(msg7);
    EXPECT_EQ(kPeerAddr1, withdrawal3.peer.addr);
    auto prefixSetAdjOutWithdrawal3 = PrefixPathIds{{kV4Prefix1, 3}};
    EXPECT_EQ(prefixSetAdjOutWithdrawal3, withdrawal3.pfxPathIds);

    // Verify AdjRibLiteTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/false));
    /*
     * Verify AdjRibTree size now is 1
     * All paths for kV4Prefix1 have been withdrawn, and hence
     * that prefix should have been removed from the tree
     */
    EXPECT_EQ(
        1,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

    EXPECT_EQ(3, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(3, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(7, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(4, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(3, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(16, adjRib_->getStats().getTotalAttributeUpdates());

    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * Ensure that a BGPupdate2 with multiple v6 only fields is properly processed
 */
TEST_F(AdjRibInboundFixture, V6UpdateProcessingMultiple) {
  setupAdjRib();
  std::vector<folly::CIDRNetwork> prefixSet{kV6Prefix1, kV6Prefix2};
  PrefixPathIds pfxPathIds{
      {kV6Prefix1, kDefaultPathID}, {kV6Prefix2, kDefaultPathID}};

  fm_->addTask([&] {
    auto update = createV6BgpUpdateMultipleAnnounce(prefixSet);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));
    auto announcement = std::get<RibInAnnouncement>(msg);
    EXPECT_EQ(kPeerAddr1, announcement.peer.addr);
    EXPECT_EQ(pfxPathIds, announcement.pfxPathIds);
    EXPECT_EQ(kV6Nexthop1, announcement.attrs->getNexthop());

    for (auto& prefix : prefixSet) {
      /*
       * Verify multiple RIB entries are created.
       * Match various fields from input
       */
      auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, prefix);
      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/false, prefix));
      EXPECT_NE(nullptr, adjRibEntry);
      ASSERT_NE(nullptr, adjRibEntry->getPreIn());
      ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
      EXPECT_TRUE(adjRibEntry->getPreIn()->isPublished());
      EXPECT_TRUE(adjRibEntry->getPostAttr()->isPublished());
      EXPECT_EQ(kMed, adjRibEntry->getPreIn()->getMed());
      EXPECT_EQ(kV6Nexthop1, adjRibEntry->getPreIn()->getNexthop());
    }

    // Verify both routes share same attribute pointer
    auto adjRibEntry1 = adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix1);
    auto adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix2);
    // Shallow compare
    EXPECT_EQ(adjRibEntry1->getPreIn(), adjRibEntry2->getPreIn());

    // Verify AdjRibLiteTree size is non-zero
    EXPECT_EQ(
        2,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/false));
    // Verify AdjRibTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

    EXPECT_EQ(2, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(2, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(1, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(4, adjRib_->getStats().getTotalAttributeUpdates());

    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * Ensure that a BGPupdate2 with multiple v6 only fields is properly processed
 */
TEST_F(AdjRibInboundFixture, V6UpdateProcessingMultipleWithAddPath) {
  setupAdjRib();
  std::vector<folly::CIDRNetwork> prefixSet{kV6Prefix1, kV6Prefix2};
  auto prefixBatchPathIds1 = PrefixPathIds{{kV6Prefix1, 1}, {kV6Prefix2, 1}};
  auto prefixBatchPathIds2 = PrefixPathIds{{kV6Prefix1, 2}, {kV6Prefix2, 2}};
  auto prefixBatchPathIds3 = PrefixPathIds{{kV6Prefix1, 3}, {kV6Prefix2, 3}};
  folly::fibers::Baton bt1, bt2, bt3, bt4, bt5, bt6, bt7;

  fm_->addTask([&] {
    bt1.wait();
    adjRib_->recAddPath_ = true;
    auto update = createV6BgpUpdateMultipleAnnounce(prefixSet);
    for (auto& rigPrefix : *update->mpAnnounced()->prefixes()) {
      rigPrefix.pathId() = 1;
    }
    adjRibInQ_->fiberPush(std::move(update));

    bt2.wait();
    update = createV6BgpUpdateMultipleAnnounce(prefixSet);
    for (auto& rigPrefix : *update->mpAnnounced()->prefixes()) {
      rigPrefix.pathId() = 2;
    }
    *update->mpAnnounced()->nexthop() = toBinaryAddress(kV6Nexthop2);
    adjRibInQ_->fiberPush(std::move(update));

    bt3.wait();
    update = createV6BgpUpdateMultipleAnnounce(prefixSet);
    for (auto& rigPrefix : *update->mpAnnounced()->prefixes()) {
      rigPrefix.pathId() = 1;
    }
    *update->mpAnnounced()->nexthop() = toBinaryAddress(kV6Nexthop3);
    adjRibInQ_->fiberPush(std::move(update));

    bt4.wait();
    update = createV6BgpUpdateMultipleAnnounce(prefixSet);
    for (auto& rigPrefix : *update->mpAnnounced()->prefixes()) {
      rigPrefix.pathId() = 3;
    }
    *update->mpAnnounced()->nexthop() = toBinaryAddress(kV6Nexthop1);
    adjRibInQ_->fiberPush(std::move(update));

    bt5.wait();
    update = createV6BgpUpdateSingleWithdraw({kV6Prefix1});
    update->mpWithdrawn()->prefixes()[0].pathId() = 2;
    adjRibInQ_->fiberPush(std::move(update));

    bt6.wait();
    update = createV6BgpUpdateSingleWithdraw({kV6Prefix1});
    update->mpWithdrawn()->prefixes()[0].pathId() = 1;
    adjRibInQ_->fiberPush(std::move(update));

    bt7.wait();
    update = createV6BgpUpdateSingleWithdraw({kV6Prefix1});
    update->mpWithdrawn()->prefixes()[0].pathId() = 3;
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    bt1.post();
    /*
     * peer announced kV6Prefix1, kV6Prefix2 with same attr and nexthop
     * kV6Nexthop1 and path id 1
     * here we verify adjrib send ribannouncement for these two prefix with
     * kV6Nexthop1
     */
    auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    auto announcement = std::get<RibInAnnouncement>(msg);
    EXPECT_EQ(kPeerAddr1, announcement.peer.addr);
    EXPECT_EQ(prefixBatchPathIds1, announcement.pfxPathIds);
    EXPECT_EQ(kV6Nexthop1, announcement.attrs->getNexthop());

    // Verify AdjRibLiteTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/false));
    // Verify AdjRibTree size is 2
    EXPECT_EQ(
        2,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

    EXPECT_EQ(2, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(2, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(1, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(4, adjRib_->getStats().getTotalAttributeUpdates());

    /*
     * peer announced kV6Prefix1, kV6Prefix2 with same attr and nexthop
     * kV6Nexthop2 and path id 2
     * here we verify adjrib send ribannouncement for these two prefix with
     * kV6Nexthop2
     */
    bt2.post();
    auto msg2 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    auto announcement2 = std::get<RibInAnnouncement>(msg2);
    EXPECT_EQ(kPeerAddr1, announcement2.peer.addr);
    EXPECT_EQ(prefixBatchPathIds2, announcement2.pfxPathIds);
    EXPECT_EQ(kV6Nexthop2, announcement2.attrs->getNexthop());

    // Verify AdjRibLiteTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/false));
    /*
     * Verify AdjRibTree size is 2
     * Radix tree keys prefx and hence tree size still would be 2
     * Multiple paths for a prefix does not add size to the tree
     */
    EXPECT_EQ(
        2,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

    EXPECT_EQ(4, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(4, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(2, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(2, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(8, adjRib_->getStats().getTotalAttributeUpdates());

    /*
     * peer replace path 1 for kV6Prefix1 and kV6Prefix2. The new nexthop is
     * kV6Nexthop3.
     * here we verify adjrib send ribannouncement for updating these two prefix
     * with kV6Nexthop3.
     */
    bt3.post();
    auto msg3 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    auto announcement3 = std::get<RibInAnnouncement>(msg3);
    EXPECT_EQ(kPeerAddr1, announcement3.peer.addr);
    EXPECT_EQ(prefixBatchPathIds1, announcement3.pfxPathIds);
    EXPECT_EQ(kV6Nexthop3, announcement3.attrs->getNexthop());

    // Verify AdjRibLiteTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/false));
    /*
     * Verify AdjRibTree size should still remain 2 because both
     * the prefixes are still present in the radix tree
     */
    EXPECT_EQ(
        2,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

    EXPECT_EQ(4, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(4, adjRib_->getStats().getPostInPrefixCount());

    bt4.post();
    /*
     * peer announced kV6Prefix1, kV6Prefix2 with same attr and nexthop
     * kV6Nexthop1 and path id 3
     * here we verify adjrib send ribannouncement for these two prefix with
     * kV6Nexthop1
     */
    auto msg4 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    auto announcement4 = std::get<RibInAnnouncement>(msg4);
    EXPECT_EQ(kPeerAddr1, announcement4.peer.addr);
    EXPECT_EQ(prefixBatchPathIds3, announcement4.pfxPathIds);
    EXPECT_EQ(kV6Nexthop1, announcement4.attrs->getNexthop());

    // Verify AdjRibLiteTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/false));
    /*
     * Verify AdjRibTree size is still 2
     * Because announcement is for same prefix with different path
     */
    EXPECT_EQ(
        2,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

    EXPECT_EQ(6, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(6, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(4, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(4, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(16, adjRib_->getStats().getTotalAttributeUpdates());

    /*
     * peer withdraws path 2 for kV6Prefix1.
     * adjrib will send Ribwithdraw msg for  kV6Prefix with kV6Nexthop2
     */
    bt5.post();
    auto msg5 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    auto withdrawal = std::get<RibInWithdrawal>(msg5);
    EXPECT_EQ(kPeerAddr1, withdrawal.peer.addr);
    auto prefixSetAdjOutWithdrawal = PrefixPathIds{{kV6Prefix1, 2}};
    EXPECT_EQ(prefixSetAdjOutWithdrawal, withdrawal.pfxPathIds);
    EXPECT_EQ(5, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(5, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(5, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(4, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(1, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(16, adjRib_->getStats().getTotalAttributeUpdates());

    /*
     * peer withdraws path 1 for kV6Prefix1.
     * adjrib will send Ribwithdraw msg for  kV6Prefix with kV6Nexthop3
     */
    bt6.post();
    auto msg6 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    auto withdrawal2 = std::get<RibInWithdrawal>(msg6);
    EXPECT_EQ(kPeerAddr1, withdrawal2.peer.addr);
    auto prefixSetAdjOutWithdrawal2 = PrefixPathIds{{kV6Prefix1, 1}};

    // Verify AdjRibLiteTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/false));
    /*
     * Verify AdjRibTree size still remains 2
     * One path for one kV4Prefix1 is withdrawn but other paths
     * for that prefix still exist
     */
    EXPECT_EQ(
        2,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

    EXPECT_EQ(prefixSetAdjOutWithdrawal2, withdrawal2.pfxPathIds);
    EXPECT_EQ(4, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(4, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(6, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(4, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(2, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(16, adjRib_->getStats().getTotalAttributeUpdates());

    /*
     * peer withdraws path 1 for kV6Prefix1.
     * adjrib will send Ribwithdraw msg for  kV6Prefix with kV6Nexthop1
     */
    bt7.post();
    auto msg7 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    auto withdrawal3 = std::get<RibInWithdrawal>(msg7);
    EXPECT_EQ(kPeerAddr1, withdrawal3.peer.addr);
    auto prefixSetAdjOutWithdrawal3 = PrefixPathIds{{kV6Prefix1, 3}};

    // Verify AdjRibLiteTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/false));
    /*
     * Verify AdjRibTree size now is 1
     * All paths for kV4Prefix1 have been withdrawn, and hence
     * that prefix should have been removed from the tree
     */
    EXPECT_EQ(
        1,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

    EXPECT_EQ(prefixSetAdjOutWithdrawal3, withdrawal3.pfxPathIds);
    EXPECT_EQ(3, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(3, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(7, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(4, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(3, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(16, adjRib_->getStats().getTotalAttributeUpdates());

    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * Verify that withdraw is processed properly and withdraw message is
 * queued properly to rib
 */
TEST_F(AdjRibInboundFixture, V4AnnounceAndWithdraw) {
  setupAdjRib();
  auto acceptedBefore = totalAcceptedPrefixCount;
  std::vector<folly::CIDRNetwork> prefixSet{kV4Prefix1, kV4Prefix2};

  fm_->addTask([&] {
    // Announce 2 routes
    auto update1 = createV4BgpUpdateMultipleAnnounce(prefixSet);
    adjRibInQ_->fiberPush(std::move(update1));
    // Withdraw 1 route
    auto update2 = createV4BgpUpdateSingleWithdraw(kV4Prefix1);
    adjRibInQ_->fiberPush(std::move(update2));
  });

  fm_->addTask([&] {
    auto msg1 = facebook::bgp::test::boundedBlockingPop(
        ribInQ_, "ribInQ_"); // add route1 and route2
    auto msg2 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_"); // withdraw

    // Verify withdrawal message
    ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(msg2));
    auto withdrawal = std::get<RibInWithdrawal>(msg2);
    PrefixPathIds withdrawPrefixSet{{kV4Prefix1, kDefaultPathID}};
    EXPECT_EQ(withdrawPrefixSet, withdrawal.pfxPathIds);

    // Verify that rib entry is present for non-withdrawn route
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
    EXPECT_NE(nullptr, adjRibEntry);
    EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix2));

    // Verify that withdrawn route is deleted
    auto adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry2);
    EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1));

    // Verify AdjRibLiteTree size is non-zero
    EXPECT_EQ(
        1,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/false));
    // Verify AdjRibTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

    // Verify stats
    EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(2, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(1, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(1, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(4, adjRib_->getStats().getTotalAttributeUpdates());

    // Verify global accepted prefix counter: 2 announced - 1 withdrawn = +1
    EXPECT_EQ(acceptedBefore + 1, totalAcceptedPrefixCount);

    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * [S364501]
 *
 * When prefixes(v4 or v6) are marked as stale, duplicate withdraw message can
 * result segmentation fault when moving entry from adjRibInStale_ to
 * adjRibInPathTree_.
 *
 * This test mimick the case that:
 * 1) All prefixes are marked stale in adjRibInStale_;
 * 2) Process one withdraw message for prefix X;
 * 3) Process another withdraw message containing the same prefix X;
 *
 * Make sure BGP can handle this gracefully.
 */
TEST_F(AdjRibInboundFixture, ConsecutiveV4Withdraw) {
  setupAdjRib();
  RibStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();
  std::vector<folly::CIDRNetwork> prefixSet{kV4Prefix1, kV4Prefix2};

  fm_->addTask([&] {
    {
      // Announce 2 routes
      auto update1 = createV4BgpUpdateMultipleAnnounce(prefixSet);
      adjRibInQ_->fiberPush(std::move(update1));

      // Ensure we add announcement before marking stale
      facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");

      EXPECT_NE(
          0,
          adjRib_->getRibTreeSize(
              true, /* ingress */
              false /* isAddPathEnabled */));
      // Mark learnt route stale
      adjRib_->markLearntRoutesStale();
      EXPECT_NE(0, adjRib_->getRibInStaleTreeSize());
      // Stale ODS counter should reflect entries moved to stale tree
      tcData->publishStats();
      EXPECT_EQ(2, tcData->getCounter(RibStats::kAdjRibInStaleCount));
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(
              true, /* ingress */
              false /* isAddPathEnabled */));
    }
    {
      // Withdraw 1 prefix
      auto withdraw1 = createV4BgpUpdateSingleWithdraw(kV4Prefix1);
      adjRibInQ_->fiberPush(std::move(withdraw1));
      // Withdraw the duplicate prefix
      auto withdraw2 = createV4BgpUpdateSingleWithdraw(kV4Prefix1);
      adjRibInQ_->fiberPush(std::move(withdraw2));

      // Ensure we check stats AFTER finish processing
      auto msg1 = facebook::bgp::test::boundedBlockingPop(
          ribInQ_, "ribInQ_"); // withdraw

      /*
       * Verify AdjRibStaleTree size. One stale entry (for the prefix with two
       * duplicate withdraws) was moved while the other stale entry was not
       * moved. Thus the size should be 1
       */
      EXPECT_EQ(1, adjRib_->getRibInStaleTreeSize());
      // Stale ODS counter decremented (one stale entry promoted for withdrawal)
      tcData->publishStats();
      EXPECT_EQ(1, tcData->getCounter(RibStats::kAdjRibInStaleCount));
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(
              true, /* ingress */
              false /* isAddPathEnabled */));
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(
              true, /* ingress */
              true /* isAddPathEnabled */));
    }

    // Verify stats
    EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(3, adjRib_->getStats().getRecvUpdateMsgs()); // 1 add + 2 del
    EXPECT_EQ(1, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(2, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(4, adjRib_->getStats().getTotalAttributeUpdates());

    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * Verify that withdraw without announcement doesn't lead to any message
 * Or AdjRibEntry creation
 */
TEST_F(AdjRibInboundFixture, SpuriousWithdraw) {
  setupAdjRib();

  fm_->addTask([&] {
    // Withdraw 1 route (without any announcement)
    auto update = createV4BgpUpdateSingleWithdraw(kV4Prefix1);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    /*
     * As we are checking for queue to be empty, sleeping for a while
     * to ensure processing is completed
     */
    fiberSleepFor(50ms);
    EXPECT_TRUE(ribInQ_.empty());
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(1, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(0, adjRib_->getStats().getTotalAttributeUpdates());

    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);

    terminateAdjRib();
  });

  evb_.loop();
}

TEST_F(AdjRibInboundFixture, ReceivedPathIdReachesRib) {
  setupAdjRib();

  /*
   * withdraw/announce route, ensure pathID reaches RibIn message.
   * pathID would be some nonzero value in ADD-PATH case, and 0 otherwise
   */
  fm_->addTask([&] {
    uint32_t expectedPathId = 135;
    adjRib_->recAddPath_ = true;
    auto update1 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1,
        kV4Nexthop1,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP,
        expectedPathId);
    adjRibInQ_->fiberPush(std::move(update1));

    // Verify rib In messages
    PrefixPathIds announcePrefixSet{{kV4Prefix1, expectedPathId}};
    auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));
    auto announcement = std::get<RibInAnnouncement>(msg);
    EXPECT_EQ(kPeerAddr1, announcement.peer.addr);
    EXPECT_EQ(announcePrefixSet, announcement.pfxPathIds);
    EXPECT_EQ(kV4Nexthop1, announcement.attrs->getNexthop());

    auto update2 = createV4BgpUpdateSingleWithdraw(kV4Prefix1, expectedPathId);
    adjRibInQ_->fiberPush(std::move(update2));

    PrefixPathIds withdrawPrefixSet{{kV4Prefix1, expectedPathId}};
    auto msg2 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(msg2));
    auto withdrawal = std::get<RibInWithdrawal>(msg2);
    EXPECT_EQ(kPeerAddr1, withdrawal.peer.addr);
    EXPECT_EQ(withdrawPrefixSet, withdrawal.pfxPathIds);

    expectedPathId = 0;
    adjRib_->recAddPath_ = false;

    auto update3 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1,
        kV4Nexthop1,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP,
        expectedPathId);
    adjRibInQ_->fiberPush(std::move(update3));

    // Verify rib In messages
    PrefixPathIds announce2PrefixSet{{kV4Prefix1, expectedPathId}};
    auto msg3 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg3));
    auto announcement2 = std::get<RibInAnnouncement>(msg3);
    EXPECT_EQ(kPeerAddr1, announcement2.peer.addr);
    EXPECT_EQ(announce2PrefixSet, announcement2.pfxPathIds);
    EXPECT_EQ(kV4Nexthop1, announcement2.attrs->getNexthop());

    auto update4 = createV4BgpUpdateSingleWithdraw(kV4Prefix1, expectedPathId);
    adjRibInQ_->fiberPush(std::move(update4));

    PrefixPathIds withdraw2PrefixSet{{kV4Prefix1, expectedPathId}};
    auto msg4 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(msg4));
    auto withdrawal2 = std::get<RibInWithdrawal>(msg4);
    EXPECT_EQ(kPeerAddr1, withdrawal2.peer.addr);
    EXPECT_EQ(withdraw2PrefixSet, withdrawal2.pfxPathIds);

    terminateAdjRib();
  });

  evb_.loop();
}

TEST_F(
    AdjRibProcessPeerAnnouncedFixture,
    MaybeAnnouncePrefixTest_PolicyBlockedNewPrefix) {
  // Insert an adjRibEntry for kV4Prefix1 for testing purposes.
  auto adjRibEntry = adjRib_->addRibEntry(true /* ingress */, kV4Prefix1);
  auto& messages = subscribeToLogMessages("", folly::LogLevel::DBG3);
  /**
   * Policy blocked prefix and Rib does not know about this prefix.
   * This should be a noop.
   */
  messages.clear();
  adjRibEntry->setPostAttr(nullptr /* attrs */);

  adjRib_->maybeAnnouncePrefix(
      kV4Prefix1,
      kDefaultPathID,
      nullptr /* postInAttrs */,
      adjRibEntry,
      withdrawnPfxPathIds_,
      groupAnnouncedPrefixes_);

  EXPECT_EQ(1, messages.size());
  EXPECT_TRUE(
      messages[0].first.getMessage().ends_with(
          "Reason: Blocked prefix by policy. (Not previously announced to Rib)"));
  EXPECT_EQ(0, withdrawnPfxPathIds_.size());
  EXPECT_EQ(0, groupAnnouncedPrefixes_.size());
  EXPECT_EQ(0, adjRib_->stats_.getPostInPrefixCount());
}

TEST_F(
    AdjRibProcessPeerAnnouncedFixture,
    MaybeAnnouncePrefixTest_PolicyBlockedOldPrefix) {
  // Insert an adjRibEntry for kV4Prefix1 for testing purposes.
  auto adjRibEntry = adjRib_->addRibEntry(true /* ingress */, kV4Prefix1);
  auto& messages = subscribeToLogMessages("", folly::LogLevel::DBG3);

  /**
   * Policy blocked this prefix and now we need to withdraw from Rib.
   * withdrawnPfxPathIds_ should contain this prefix.
   */
  messages.clear();
  withdrawnPfxPathIds_.clear();

  auto path = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 1, 1));
  adjRibEntry->setPostAttr(path);

  adjRib_->maybeAnnouncePrefix(
      kV4Prefix1,
      kDefaultPathID,
      nullptr /* postInAttrs */,
      adjRibEntry,
      withdrawnPfxPathIds_,
      groupAnnouncedPrefixes_);

  EXPECT_EQ(1, messages.size());
  EXPECT_TRUE(
      messages[0].first.getMessage().ends_with(
          "Reason: Blocked prefix by policy. (Previously announced to Rib)"));
  EXPECT_EQ(1, withdrawnPfxPathIds_.size());
  EXPECT_EQ(0, groupAnnouncedPrefixes_.size());
}

TEST_F(
    AdjRibProcessPeerAnnouncedFixture,
    MaybeAnnouncePrefixTest_OldPrefixNoPathChange) {
  // Insert an adjRibEntry for kV4Prefix1 for testing purposes.
  auto adjRibEntry = adjRib_->addRibEntry(true /* ingress */, kV4Prefix1);
  auto& messages = subscribeToLogMessages("", folly::LogLevel::DBG3);

  /**
   * Policy accepted this prefix but there is no change to what was
   * previously announced to Rib. This should be a noop.
   */
  messages.clear();
  withdrawnPfxPathIds_.clear();

  auto path = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 1, 1));
  adjRibEntry->setPostAttr(path);

  adjRib_->maybeAnnouncePrefix(
      kV4Prefix1,
      kDefaultPathID,
      path->clone() /* postInAttrs */,
      adjRibEntry,
      withdrawnPfxPathIds_,
      groupAnnouncedPrefixes_);

  EXPECT_EQ(1, messages.size());
  EXPECT_TRUE(
      messages[0].first.getMessage().ends_with(
          "Reason: got same post policy in attributes."));
  EXPECT_EQ(0, withdrawnPfxPathIds_.size());
  EXPECT_EQ(0, groupAnnouncedPrefixes_.size());
}

TEST_F(
    AdjRibProcessPeerAnnouncedFixture,
    MaybeAnnouncePrefixTest_BackupAddressChange) {
  auto adjRibEntry = adjRib_->addRibEntry(true /* ingress */, kV4Prefix1);
  auto path = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 1, 1));
  const auto oldBackupAddr = folly::IPAddress("2001:db8::1");
  const auto newBackupAddr = folly::IPAddress("2001:db8::2");
  path->setBackupAddr(oldBackupAddr);
  adjRibEntry->setPostAttr(path);
  auto updatedPath = path->clone();
  updatedPath->setBackupAddr(newBackupAddr);

  adjRib_->maybeAnnouncePrefix(
      kV4Prefix1,
      kDefaultPathID,
      updatedPath,
      adjRibEntry,
      withdrawnPfxPathIds_,
      groupAnnouncedPrefixes_);

  EXPECT_TRUE(withdrawnPfxPathIds_.empty());
  ASSERT_EQ(1, groupAnnouncedPrefixes_.size());
  const auto& [announcementAttrs, prefixes] = *groupAnnouncedPrefixes_.begin();
  EXPECT_EQ(newBackupAddr, announcementAttrs->getBackupAddr());
  EXPECT_EQ(newBackupAddr, adjRibEntry->getPostAttr()->getBackupAddr());
  EXPECT_EQ(PrefixPathIds({{kV4Prefix1, kDefaultPathID}}), prefixes);
}

TEST_F(
    AdjRibProcessPeerAnnouncedFixture,
    MaybeAnnouncePrefixTest_PolicyAcceptedPathChange) {
  // Insert an adjRibEntry for kV4Prefix1 for testing purposes.
  auto adjRibEntry = adjRib_->addRibEntry(true /* ingress */, kV4Prefix1);
  auto& messages = subscribeToLogMessages("", folly::LogLevel::DBG3);
  /**
   * Policy accepted this prefix and we want to announce to Rib
   * because the path attributes changed for an existing route.
   */
  messages.clear();
  withdrawnPfxPathIds_.clear();
  groupAnnouncedPrefixes_.clear();

  auto postInPfxCount = adjRib_->stats_.getPostInPrefixCount();

  auto path1 = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 1, 1));
  adjRibEntry->setPostAttr(path1);

  auto path2 = std::make_shared<BgpPath>(*buildBgpPathFields(2, 2, 2, 2));

  adjRib_->maybeAnnouncePrefix(
      kV4Prefix1,
      kDefaultPathID,
      path2 /* postInAttrs */,
      adjRibEntry,
      withdrawnPfxPathIds_,
      groupAnnouncedPrefixes_);

  EXPECT_EQ(0, messages.size());
  EXPECT_EQ(0, withdrawnPfxPathIds_.size());
  EXPECT_EQ(1, groupAnnouncedPrefixes_.size());
  EXPECT_EQ(kV4Prefix1, get<0>(groupAnnouncedPrefixes_[path2].front()));
  EXPECT_EQ(kDefaultPathID, get<1>(groupAnnouncedPrefixes_[path2].front()));
  EXPECT_EQ(postInPfxCount, adjRib_->stats_.getPostInPrefixCount());
}

TEST_F(
    AdjRibProcessPeerAnnouncedFixture,
    MaybeAnnouncePrefixTest_PolicyAcceptedNewRoute) {
  // Insert an adjRibEntry for kV4Prefix1 for testing purposes.
  auto adjRibEntry = adjRib_->addRibEntry(true /* ingress */, kV4Prefix1);
  auto& messages = subscribeToLogMessages("", folly::LogLevel::DBG3);
  /**
   * Policy accepted this prefix and we want to announce to Rib
   * because the path attributes are for a newly learned route.
   */
  messages.clear();
  withdrawnPfxPathIds_.clear();
  groupAnnouncedPrefixes_.clear();

  auto postInPfxCount = adjRib_->stats_.getPostInPrefixCount();

  adjRibEntry->setPostAttr(nullptr /* path */);

  auto path = std::make_shared<BgpPath>(*buildBgpPathFields(2, 2, 2, 2));

  adjRib_->maybeAnnouncePrefix(
      kV4Prefix1,
      kDefaultPathID,
      path /* postInAttrs */,
      adjRibEntry,
      withdrawnPfxPathIds_,
      groupAnnouncedPrefixes_);

  EXPECT_EQ(0, messages.size());
  EXPECT_EQ(0, withdrawnPfxPathIds_.size());
  EXPECT_EQ(1, groupAnnouncedPrefixes_.size());
  EXPECT_EQ(kV4Prefix1, get<0>(groupAnnouncedPrefixes_[path].front()));
  EXPECT_EQ(kDefaultPathID, get<1>(groupAnnouncedPrefixes_[path].front()));
  EXPECT_EQ(postInPfxCount + 1, adjRib_->stats_.getPostInPrefixCount());
}

TEST_F(
    AdjRibProcessPeerAnnouncedFixture,
    MaybeAnnouncePrefixTest_CappedNewRoute) {
  // Insert an adjRibEntry for kV4Prefix1 for testing purposes.
  auto adjRibEntry = adjRib_->addRibEntry(true /* ingress */, kV4Prefix1);
  auto& messages = subscribeToLogMessages("", folly::LogLevel::DBG3);
  /**
   * Policy accepted this prefix but we need to cap route for this peer.
   */
  withdrawnPfxPathIds_.clear();
  groupAnnouncedPrefixes_.clear();

  adjRibEntry->setPostAttr(nullptr /* path */);
  /**
   * Trigger capped route condition.
   */
  for (int i = 0; i < adjRib_->peeringParams_.postRouteLimit->max_routes();
       ++i) {
    adjRib_->stats_.incrementPostInPrefixCount(kV4Prefix1.first.isV4());
  }
  auto postInPfxCount = adjRib_->stats_.getPostInPrefixCount();

  auto path = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 1, 1));

  messages.clear();
  adjRib_->maybeAnnouncePrefix(
      kV4Prefix1,
      kDefaultPathID,
      path /* postInAttrs */,
      adjRibEntry,
      withdrawnPfxPathIds_,
      groupAnnouncedPrefixes_);

  EXPECT_EQ(2, messages.size());
  EXPECT_TRUE(
      messages[0].first.getMessage().starts_with(
          "12001 prefixes received from peer"));
  EXPECT_TRUE(messages[1].first.getMessage().starts_with("Tearing down peer"));

  EXPECT_EQ(0, withdrawnPfxPathIds_.size());
  EXPECT_EQ(0, groupAnnouncedPrefixes_.size());
  EXPECT_EQ(postInPfxCount, adjRib_->stats_.getPostInPrefixCount());
}

TEST_F(AdjRibProcessPeerAnnouncedFixture, CanAddRibEntryTest_PrefixOverload) {
  auto& messages = subscribeToLogMessages("", folly::LogLevel::ERR);
  /**
   * We are deciding whether to add a new adjRibEntry, but
   * we have hit limit for prefix overload protection.
   * We set switchLimitConfig to 0 to trigger trivial overload condition.
   */
  adjRib_->switchLimitConfig_ =
      std::make_shared<thrift::BgpSwitchLimitConfig>();
  adjRib_->switchLimitConfig_->total_path_limit() = 0;
  auto path = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 1, 1));

  messages.clear();
  bool canAdd = adjRib_->canAddRibInEntry(kV4Prefix1, path);

  EXPECT_FALSE(canAdd);

  EXPECT_EQ(1, messages.size());
  EXPECT_TRUE(
      messages[0].first.getMessage().starts_with(
          "Total path received: 1 exceeds max limit 0"));
}

TEST_F(AdjRibProcessPeerAnnouncedFixture, CanAddRibEntryTest_CappedNewRoute) {
  auto& messages = subscribeToLogMessages("", folly::LogLevel::DBG3);
  /**
   * We are deciding whether to add a new adjRibEntry, but
   * we need to cap route for this peer.
   * Trigger capped route condition.
   */
  for (int i = 0; i < adjRib_->peeringParams_.preRouteLimit->max_routes();
       ++i) {
    adjRib_->stats_.incrementPreInPrefixCount(
        kV4Prefix1, false /* isVipPrefix */, false /* isGoldenVip */);
  }
  auto preInPfxCount = adjRib_->stats_.getPreInPrefixCount();

  auto path = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 1, 1));

  messages.clear();
  bool canAdd = adjRib_->canAddRibInEntry(kV4Prefix1, path);

  EXPECT_FALSE(canAdd);
  EXPECT_EQ(preInPfxCount, adjRib_->stats_.getPreInPrefixCount());

  EXPECT_EQ(2, messages.size());
  EXPECT_TRUE(
      messages[0].first.getMessage().starts_with(
          "12001 prefixes received from peer"));
  EXPECT_TRUE(messages[1].first.getMessage().starts_with("Tearing down peer"));
}

TEST_F(AdjRibProcessPeerAnnouncedFixture, CanAddRibEntryTest_WithinLimit) {
  auto path = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 1, 1));
  bool canAdd = adjRib_->canAddRibInEntry(kV4Prefix1, path);
  EXPECT_TRUE(canAdd);
}

TEST_F(
    AdjRibProcessPeerAnnouncedFixture,
    PreFilterDroppedRouteCountIncrementsOnCap) {
  /*
   * Hitting the pre-filter cap increments the per-peer pre-filter dropped
   * route counter (consumed by `show bgp summary` and the health validator).
   */
  for (int i = 0; i < adjRib_->peeringParams_.preRouteLimit->max_routes();
       ++i) {
    adjRib_->stats_.incrementPreInPrefixCount(
        kV4Prefix1, false /* isVipPrefix */, false /* isGoldenVip */);
  }
  EXPECT_EQ(0, adjRib_->stats_.getPreFilterDroppedRouteCount());

  auto path = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 1, 1));
  EXPECT_FALSE(adjRib_->canAddRibInEntry(kV4Prefix1, path));

  EXPECT_EQ(1, adjRib_->stats_.getPreFilterDroppedRouteCount());
}

TEST_F(
    AdjRibProcessPeerAnnouncedFixture,
    PreFilterDroppedRouteCountClearedOnSessionDown) {
  /*
   * clear() is invoked on session teardown (AdjRib::sessionTerminated), so the
   * per-peer dropped counter resets when the session goes down.
   */
  adjRib_->stats_.incrementPreFilterDroppedRouteCount();
  adjRib_->stats_.incrementPreFilterDroppedRouteCount();
  EXPECT_EQ(2, adjRib_->stats_.getPreFilterDroppedRouteCount());

  adjRib_->stats_.clear();

  EXPECT_EQ(0, adjRib_->stats_.getPreFilterDroppedRouteCount());
}

TEST_F(
    AdjRibProcessPeerAnnouncedFixture,
    GetPostInPolicyAttributesTest_UnexpectedNullAttrs) {
  // Insert an adjRibEntry for kV4Prefix1 for testing purposes.
  auto adjRibEntry = adjRib_->addRibEntry(true /* ingress */, kV4Prefix1);

  // Create null attrs.
  std::shared_ptr<BgpPath> prePolicyAttrs = nullptr;
  auto policyActionData = std::make_shared<BgpPolicyActionData>();

  EXPECT_DEATH(
      adjRib_->getPostInPolicyAttributes(
          kV4Prefix1, prePolicyAttrs, policyActionData, adjRibEntry),
      "");
}

TEST_F(
    AdjRibProcessPeerAnnouncedFixture,
    GetPostInPolicyAttributesTest_NoPolicyConfigured) {
  // Insert an adjRibEntry for kV4Prefix1 for testing purposes.
  auto adjRibEntry = adjRib_->addRibEntry(true /* ingress */, kV4Prefix1);

  // Create published attrs.
  auto prePolicyAttrs =
      std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 1, 1));
  prePolicyAttrs->publish();
  auto policyActionData = std::make_shared<BgpPolicyActionData>();

  // Policy is automatically not configured in test setup.
  auto postPolicyAttrs = adjRib_->getPostInPolicyAttributes(
      kV4Prefix1, prePolicyAttrs, policyActionData, adjRibEntry);

  // Input attrs should be output attrs without any cloning occurring.
  EXPECT_TRUE(postPolicyAttrs->isPublished());
  EXPECT_EQ(prePolicyAttrs, postPolicyAttrs);
  // No policy result was stored.
  EXPECT_TRUE(adjRibEntry->getPostInPolicy());
  EXPECT_EQ("", *adjRibEntry->getPostInPolicy());
}

TEST_F(
    AdjRibProcessPeerAnnouncedFixture,
    GetPostInPolicyAttributesTest_RejectedByPolicy) {
  auto policyManager = setupDenyIgpOriginAcceptAllPolicy(kIngressPolicyName);
  // Set up adjRib with policy manager.
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      false, // callSessionEstablished
      kLocalAs1,
      kLocalAs1,
      kRemoteAs1,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      policyManager,
      kIngressPolicyName);

  auto prePolicyAttrs =
      std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 1, 1));
  prePolicyAttrs->setOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
  prePolicyAttrs->publish();

  std::vector<folly::CIDRNetwork> prefixSetIn = {kV4Prefix1};
  PolicyInMessage policyIn(prefixSetIn, prePolicyAttrs);

  // Insert an adjRibEntry for kV4Prefix1 for testing purposes.
  auto adjRibEntry = adjRib_->addRibEntry(true /* ingress */, kV4Prefix1);
  auto policyActionData = std::make_shared<BgpPolicyActionData>();

  // kV4Prefix1 should have been denied because of IGP.
  auto postPolicyAttrs = adjRib_->getPostInPolicyAttributes(
      kV4Prefix1, prePolicyAttrs, policyActionData, adjRibEntry);

  EXPECT_FALSE(postPolicyAttrs);
  // Verify policy result was stored.
  EXPECT_TRUE(adjRibEntry->getPostInPolicy());
  EXPECT_EQ("Denied by Ingress term Term1", *adjRibEntry->getPostInPolicy());
}

TEST_F(
    AdjRibProcessPeerAnnouncedFixture,
    GetPostInPolicyAttributesTest_AcceptedByPolicy) {
  // Set up policy manager that modifies community list to {kCommunity1}.
  auto policyManager =
      setupMatchEgpOriginSetCommunityPolicy(kIngressPolicyName);
  // Set up adjRib with policy manager.
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      false, // callSessionEstablished
      kLocalAs1,
      kLocalAs1,
      kRemoteAs1,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      policyManager,
      kIngressPolicyName);

  auto prePolicyAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(
      2 /* as_count */,
      1 /* community_count */,
      0 /* ext_community_count */,
      0 /* cluster_list_count */));
  // Set origin to EGP to be accepted by policy.
  prePolicyAttrs->setOrigin(BgpAttrOrigin::BGP_ORIGIN_EGP);
  prePolicyAttrs->publish();

  std::vector<folly::CIDRNetwork> prefixSetIn = {kV4Prefix1};
  PolicyInMessage policyIn(prefixSetIn, prePolicyAttrs);

  // Insert an adjRibEntry for kV4Prefix1 for testing purposes.
  auto adjRibEntry = adjRib_->addRibEntry(true /* ingress */, kV4Prefix1);
  auto policyActionData = std::make_shared<BgpPolicyActionData>();

  auto postPolicyAttrs = adjRib_->getPostInPolicyAttributes(
      kV4Prefix1, prePolicyAttrs, policyActionData, adjRibEntry);

  // kV4Prefix1 was accepted by policy.
  EXPECT_TRUE(postPolicyAttrs);
  EXPECT_TRUE(postPolicyAttrs->isPublished());
  EXPECT_NE(prePolicyAttrs, postPolicyAttrs);

  // Verify postPolicyAttrs.
  EXPECT_EQ(BgpAttrOrigin::BGP_ORIGIN_EGP, postPolicyAttrs->getOrigin());
  EXPECT_EQ(1, postPolicyAttrs->getAsPath()->size());
  EXPECT_EQ("1_1", postPolicyAttrs->getFullBgpAsPathAsString().at(0));
  EXPECT_EQ(kV4Nexthop1, postPolicyAttrs->getNexthop());
  EXPECT_EQ(kMed, postPolicyAttrs->getMed());
  EXPECT_EQ(kLocalPref, postPolicyAttrs->getLocalPref());
  EXPECT_FALSE(postPolicyAttrs->getAtomicAggregate());
  EXPECT_EQ(kOriginatorId, postPolicyAttrs->getOriginatorId());
  EXPECT_FALSE(postPolicyAttrs->getExtCommunities());
  EXPECT_EQ(
      (BgpAttrAggregatorC{.asn = 0, .ip = folly::IPAddress()}),
      postPolicyAttrs->getAggregator());
  EXPECT_EQ(1, postPolicyAttrs->getCommunities()->size());
  // Community was modified by policy.
  EXPECT_EQ(kCommunity1, postPolicyAttrs->getCommunities()->at(0).to_string());

  // Verify policy result was stored.
  EXPECT_TRUE(adjRibEntry->getPostInPolicy());
  EXPECT_EQ(
      "Accepted/Modified by Ingress term term1",
      *adjRibEntry->getPostInPolicy());
}

/******************************************************************************
 *      END   -   BGP Update message processing tests.                        *
 ******************************************************************************/

/******************************************************************************
 *      START   -   AS path loop and setting related tests.                   *
 ******************************************************************************/

// Verify that a v4 route with our own AS in AS_PATH is not learnt.
TEST_F(AdjRibInboundFixture, RejectRouteWithAsLoop) {
  setupAdjRib();

  fm_->addTask([&] {
    // Announce a route with our own AS
    auto update = createV4BgpUpdateWithAsLoop(kV4Prefix1);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    fiberSleepFor(50ms);
    EXPECT_TRUE(ribInQ_.empty());

    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());

    terminateAdjRib();
  });

  evb_.loop();
}

// Verify that a v4 route with our own AS in AS_SET of AS_PATH is not learnt.
TEST_F(AdjRibInboundFixture, RejectRouteWithAsSetLoop) {
  setupAdjRib();

  fm_->addTask([&] {
    // Announce a route with our own AS
    auto update = createV4BgpUpdateWithAsSetLoop(kV4Prefix1);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    fiberSleepFor(50ms);
    EXPECT_TRUE(ribInQ_.empty());

    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());

    terminateAdjRib();
  });

  evb_.loop();
}

// Verify that a v6 route with our own AS in AS_PATH is not learnt.
TEST_F(AdjRibInboundFixture, RejectIPv6RouteWithAsLoop) {
  setupAdjRib();

  fm_->addTask([&] {
    // Announce a route with our own AS
    auto update = createV6BgpUpdateWithAsLoop(kV6Prefix1);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    fiberSleepFor(50ms);
    EXPECT_TRUE(ribInQ_.empty());

    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());

    terminateAdjRib();
  });

  evb_.loop();
}

// Verify that a v4 route with our own AS in AS_CONFED_SET is not learnt.
TEST_F(AdjRibInboundFixture, RejectRouteWithConfedAsSetLoop) {
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      true, // callSessionEstablished
      kLocalAs1, // global as
      kLocalAs1, // local as
      kRemoteAs1,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      nullptr, // policy manager, not used
      std::nullopt, // ingress policy name, not used
      kIsConfedPeerTrue,
      static_cast<uint32_t>(kLocalAs1), // local confed as
      static_cast<uint32_t>(kLocalAs2)); // as confed id

  fm_->addTask([&] {
    // Announce a route with kLocalAs1 in Confed AS Sequence
    auto update = createV4BgpUpdateWithAsSetLoop(
        kV4Prefix1, kV4Nexthop1, kLocalAs1, true // confed
    );
    adjRibInQ_->fiberPush(std::move(update));

    // Announce a route with kLocalAs2 in AS Sequence
    update = createV4BgpUpdateWithAsSetLoop(
        kV4Prefix1, kV4Nexthop1, kLocalAs2, false // confed
    );
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    fiberSleepFor(50ms);
    EXPECT_TRUE(ribInQ_.empty());

    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());

    terminateAdjRib();
  });

  evb_.loop();
}

// Verify that a route with our own confed AS in AS_CONFED_SEQ is not learnt
TEST_F(AdjRibInboundFixture, RejectRouteWithConfedAsLoop) {
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      true, // callSessionEstablished
      kLocalAs1, // global as
      kLocalAs1, // local as
      kRemoteAs1,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      nullptr, // policy manager, not used
      std::nullopt, // ingress policy name, not used
      kIsConfedPeerTrue,
      static_cast<uint32_t>(kLocalAs1), // local confed as
      static_cast<uint32_t>(kLocalAs2)); // as confed id

  fm_->addTask([&] {
    // Announce a route with kLocalAs1 in Confed AS Sequence
    auto update = createV4BgpUpdateWithAsLoop(
        kV4Prefix1, kV4Nexthop1, kLocalAs1, true // confed
    );
    adjRibInQ_->fiberPush(std::move(update));

    // Announce a route with kLocalAs2 in AS Sequence
    update = createV4BgpUpdateWithAsLoop(
        kV4Prefix1, kV4Nexthop1, kLocalAs2, false // confed
    );
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    fiberSleepFor(50ms);
    EXPECT_TRUE(ribInQ_.empty());

    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());

    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * With a local-as session test verifies an update with no
 * loop is accepted into AdjRibIn.
 */
TEST_F(AdjRibInboundFixture, AcceptPathLocalAsSession) {
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      true, // callSessionEstablished
      kLocalAs2 /* global as */,
      kLocalAs3 /* local as */,
      kRemoteAs1);

  fm_->addTask([&] {
    // Send update with AsLoop i.e. AS# kLocalAs1
    auto update = createV4BgpUpdateWithAsLoop(kV4Prefix1);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    fiberSleepFor(50ms);
    EXPECT_FALSE(ribInQ_.empty());

    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_EQ(kV4Nexthop1, adjRibEntry->getPreIn()->getNexthop());
    EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCount());

    terminateAdjRib();
  });

  evb_.loop();
}

TEST_F(AdjRibInboundFixture, RejectRouteAsLoopLocalAsSession) {
  // Loop check for local-as ASN in the as-path for local-as session
  {
    setupAdjRib(
        kShortGrRestartTime,
        std::nullopt, // remoteGrRestartTime
        true, // callSessionEstablished
        kLocalAs2 /* global as */,
        kLocalAs1 /* local as */,
        kRemoteAs1);

    fm_->addTask([&] {
      // Announce a route with our own AS (kLocalAs1)
      auto update = createV4BgpUpdateWithAsLoop(kV4Prefix1);
      adjRibInQ_->fiberPush(std::move(update));
    });

    fm_->addTask([&] {
      fiberSleepFor(50ms);
      EXPECT_TRUE(ribInQ_.empty());

      auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
      EXPECT_EQ(nullptr, adjRibEntry);
      EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());

      terminateAdjRib();
    });

    evb_.loop();
  }
  // Loop check for global-as ASN in the as-path for local-as session
  {
    adjRib_.reset();
    setupAdjRib(
        kShortGrRestartTime,
        std::nullopt, // remoteGrRestartTime
        true, // callSessionEstablished
        kLocalAs1 /* global as */,
        kLocalAs2 /* local as */,
        kRemoteAs1);

    fm_->addTask([&] {
      // Announce a route with our own AS (kLocalAs1)
      auto update = createV4BgpUpdateWithAsLoop(kV4Prefix1);
      adjRibInQ_->fiberPush(std::move(update));
    });

    fm_->addTask([&] {
      fiberSleepFor(50ms);
      EXPECT_TRUE(ribInQ_.empty());

      auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
      EXPECT_EQ(nullptr, adjRibEntry);
      EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());

      terminateAdjRib();
    });

    evb_.loop();
  }
}

/******************************************************************************
 *      END   -   AS path loop and setting related tests.                     *
 ******************************************************************************/

/******************************************************************************
 *      START   -   Invalid BGP attribute related route rejection tests.      *
 ******************************************************************************/

/*
 * Verify that a route from iBGP peer without local preference in
 * path attributes is not learnt
 */
TEST_F(AdjRibInboundFixture, RejectIbgpRouteWithoutLocalPref) {
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      true, // callSessionEstablished
      kLocalAs1,
      kLocalAs1,
      kRemoteAs1);

  fm_->addTask([&] {
    // Announce a route without local preference
    auto update = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
    update->attrs()->localPref().reset();
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    fiberSleepFor(50ms);
    EXPECT_TRUE(ribInQ_.empty());

    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());

    terminateAdjRib();
  });

  evb_.loop();
}

// Verify that a route with our own router-id as originatorId is not learnt
TEST_F(AdjRibInboundFixture, OriginatorIdFiltering) {
  setupAdjRib();

  fm_->addTask([&] {
    // Announce a route with our own router-id (kLocalAddr1) as originator id
    auto update = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1, kV4Nexthop1, kMed, kLocalAddr1.asV4().toLongHBO());
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    fiberSleepFor(50ms);
    EXPECT_TRUE(ribInQ_.empty());

    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());

    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * Verify that a route with different originatorId but same cluster id in
 * cluster list is not learnt
 */
TEST_F(AdjRibInboundFixture, ClusterListFiltering) {
  setupAdjRib();

  fm_->addTask([&] {
    auto update = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1, kV4Nexthop1, kMed, kPeerAddr3.asV4().toLongHBO());
    /*
     * push our clusterId (kLocalAddr1) into the clusterList
     * cluster list should be in network byte order
     */
    update->attrs()->clusterList()->push_back(kLocalAddr1.asV4().toLong());
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    fiberSleepFor(50ms);
    EXPECT_TRUE(ribInQ_.empty());

    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());

    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * The four tests below pin hasRRLoop's two arms to the identifier each one is
 * defined against, with the cluster ID configured away from the BGP
 * identifier. While the two are equal every arm looks correct no matter which
 * identifier it reads, so these are the only cases that can catch the two
 * being transposed.
 */

// A route reflected through our own cluster is a loop and must be dropped
TEST_F(AdjRibInboundFixture, ClusterListFilteringUsesConfiguredClusterId) {
  localClusterId_ = kLocalClusterAddr1.asV4();
  setupAdjRib();

  fm_->addTask([&] {
    auto update = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1, kV4Nexthop1, kMed, kPeerAddr3.asV4().toLongHBO());
    // cluster list is in network byte order
    update->attrs()->clusterList()->push_back(
        kLocalClusterAddr1.asV4().toLong());
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    fiberSleepFor(50ms);
    EXPECT_TRUE(ribInQ_.empty());

    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());

    terminateAdjRib();
  });

  evb_.loop();
}

// Our BGP identifier in a CLUSTER_LIST is some other cluster's ID, not a loop
TEST_F(AdjRibInboundFixture, ClusterListFilteringIgnoresRouterId) {
  localClusterId_ = kLocalClusterAddr1.asV4();
  setupAdjRib();

  fm_->addTask([&] {
    auto update = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1, kV4Nexthop1, kMed, kPeerAddr3.asV4().toLongHBO());
    update->attrs()->clusterList()->push_back(kLocalAddr1.asV4().toLong());
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));
    PrefixPathIds prefixSet{{kV4Prefix1, kDefaultPathID}};
    EXPECT_EQ(prefixSet, std::get<RibInAnnouncement>(msg).pfxPathIds);

    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    ASSERT_NE(nullptr, adjRibEntry);
    EXPECT_EQ(kV4Nexthop1, adjRibEntry->getPreIn()->getNexthop());
    EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCount());

    terminateAdjRib();
  });

  evb_.loop();
}

// ORIGINATOR_ID keeps keying off the BGP identifier, not the cluster ID
TEST_F(AdjRibInboundFixture, OriginatorIdFilteringUsesRouterId) {
  localClusterId_ = kLocalClusterAddr1.asV4();
  setupAdjRib();

  fm_->addTask([&] {
    auto update = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1, kV4Nexthop1, kMed, kLocalAddr1.asV4().toLongHBO());
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    fiberSleepFor(50ms);
    EXPECT_TRUE(ribInQ_.empty());

    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());

    terminateAdjRib();
  });

  evb_.loop();
}

// A route originated by our cluster ID was not originated by us
TEST_F(AdjRibInboundFixture, OriginatorIdFilteringIgnoresClusterId) {
  localClusterId_ = kLocalClusterAddr1.asV4();
  setupAdjRib();

  fm_->addTask([&] {
    auto update = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1, kV4Nexthop1, kMed, kLocalClusterAddr1.asV4().toLongHBO());
    /*
     * The builder seeds CLUSTER_LIST from the originator id, which would trip
     * the cluster-list arm and mask what this test is checking.
     */
    update->attrs()->clusterList()->clear();
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));
    PrefixPathIds prefixSet{{kV4Prefix1, kDefaultPathID}};
    EXPECT_EQ(prefixSet, std::get<RibInAnnouncement>(msg).pfxPathIds);

    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    ASSERT_NE(nullptr, adjRibEntry);
    EXPECT_EQ(kV4Nexthop1, adjRibEntry->getPreIn()->getNexthop());
    EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCount());

    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * Verify we stop the following invalid behaviors
 * 1. receiving updates with confed fileds set from non members in EBGP
 */
TEST_F(AdjRibInboundFixture, RejectConfedAsPathFromNonConfedPeerWithEBGP) {
  // make current adjRib not in same confed
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      true, // callSessionEstablished
      kLocalAs1, // global as
      kLocalAs1, // local as
      kRemoteAs2,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      nullptr, // policy manager, not used
      std::nullopt, // ingress policy name, not used
      kIsConfedPeerFalse,
      static_cast<uint32_t>(kLocalAs1), // local confed as
      static_cast<uint32_t>(kLocalAs2)); // as confed id

  fm_->addTask([&] {
    // create updates with confed fields set
    {
      auto update = createV4BgpUpdateSingleAnnounce();
      update->attrs()->asPath()[0].asConfedSequence()->push_back(kRemoteAs1);
      adjRibInQ_->fiberPush(std::move(update));
    }
    {
      auto update = createV4BgpUpdateSingleAnnounce();
      update->attrs()->asPath()[0].asConfedSet()->insert(kRemoteAs2);
      adjRibInQ_->fiberPush(std::move(update));
    }
  });

  fm_->addTask([&] {
    fiberSleepFor(50ms);
    // expect to see 2 notification
    EXPECT_EQ(boundedAdjRibOutQ_->size(), 2);
    auto result = facebook::bgp::test::boundedBlockingPop(
        *boundedAdjRibOutQ_, "boundedAdjRibOutQ_");
    auto ret = folly::variant_match(
        *result,
        [&](BgpNotification notification) {
          EXPECT_EQ(
              *notification.errCode(), BgpNotifErrCode::BN_UPDATE_MSG_ERR);
          EXPECT_EQ(
              *notification.errSubCode(),
              static_cast<uint16_t>(
                  BgpNotifUpdateMsgErrSubCode::BN_UM_MALFORMED_AS_PATH));
          return true;
        },
        [&](std::shared_ptr<const BgpUpdate2> const&) { return false; },
        [&](const UpdateDescriptor&) { return false; },
        [&](BgpRouteRefresh) { return false; },
        [&](BgpEndOfRib) { return false; });
    // make sure we actually getting a notification
    EXPECT_EQ(ret, true);

    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * Verify we stop the following invalid behaviors
 * 2. receiving updates without confed as path segment from confed ebgp peer
 */
TEST_F(AdjRibInboundFixture, CheckConfedAsPathFromConfedEbgpPeer) {
  // make current adjRib in same confed
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      true, // callSessionEstablished
      kLocalAs1, // global as is 1
      kLocalAs1, // local as asNum is 1
      kRemoteAs2, // asNum is 2
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      nullptr, // policy manager, not used
      std::nullopt, // ingress policy name, not used
      kIsConfedPeerTrue,
      static_cast<uint32_t>(kLocalAs1), // local confed as
      static_cast<uint32_t>(kLocalAs2)); // as confed id

  fm_->addTask([&] {
    {
      /*
       * create an update without confed fields set
       * invalid, expect to generate a notification
       */
      auto update = createV4BgpUpdateSingleAnnounce();
      adjRibInQ_->fiberPush(std::move(update));
    }
    {
      /*
       * create an update with empty asPathSeg
       * invalid, expect to generate a notification
       */
      auto update = createV4BgpUpdateSingleAnnounce();
      update->attrs()->asPath()->clear();
      adjRibInQ_->fiberPush(std::move(update));
    }
  });

  fm_->addTask([&] {
    fiberSleepFor(50ms);
    // expect to see 2 notifications
    EXPECT_EQ(boundedAdjRibOutQ_->size(), 2);
    auto isUpdateErrNotification = folly::overload(
        [](const BgpNotification& notification) {
          EXPECT_EQ(
              *notification.errCode(), BgpNotifErrCode::BN_UPDATE_MSG_ERR);
          EXPECT_EQ(
              *notification.errSubCode(),
              static_cast<uint16_t>(
                  BgpNotifUpdateMsgErrSubCode::BN_UM_MALFORMED_AS_PATH));
          return true;
        },
        [](const std::shared_ptr<const BgpUpdate2>&) { return false; },
        [](const UpdateDescriptor&) { return false; },
        [](const BgpRouteRefresh&) { return false; },
        [](const BgpEndOfRib&) { return false; });
    // make sure both notifications are update error notification
    {
      auto result = facebook::bgp::test::boundedBlockingPop(
          *boundedAdjRibOutQ_, "boundedAdjRibOutQ_");
      EXPECT_TRUE(std::visit(isUpdateErrNotification, *result));
    }
    {
      auto result = facebook::bgp::test::boundedBlockingPop(
          *boundedAdjRibOutQ_, "boundedAdjRibOutQ_");
      EXPECT_TRUE(std::visit(isUpdateErrNotification, *result));
    }
    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * Verify we stop the following invalid behaviors
 * 3. without confed fileds set from members in IBGP
 */
TEST_F(AdjRibInboundFixture, CheckConfedAsPathFromConfedPeerWithIBGP) {
  // make current adjRib in same confed
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      true, // callSessionEstablished
      kLocalAs1, // global as
      kLocalAs1, // local as
      kRemoteAs1,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      nullptr, // policy manager, not used
      std::nullopt, // ingress policy name, not used
      kIsConfedPeerTrue,
      static_cast<uint32_t>(kLocalAs1), // local confed as
      static_cast<uint32_t>(kLocalAs2)); // as confed id

  fm_->addTask([&] {
    {
      /*
       * create an update without confed fields set
       * valid, we won't get a notification
       */
      auto update = createV4BgpUpdateSingleAnnounce();
      adjRibInQ_->fiberPush(std::move(update));
    }
    {
      /*
       * create an update with empty asPathSeg
       * valid, we won't get a notification
       */
      auto update = createV4BgpUpdateSingleAnnounce();
      update->attrs()->asPath()->clear();
      adjRibInQ_->fiberPush(std::move(update));
    }
  });

  fm_->addTask([&] {
    fiberSleepFor(50ms);
    // expect not to see a notification
    EXPECT_EQ(boundedAdjRibOutQ_->size(), 0);

    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * Verify enforce-first-as behavior for non confed eBGP peer
 * Create 2 updates, 1 with the correct AS number at the left most position and
 * one without. The invalid one should be discarded, and ribInQ_ should have
 * only the prefix from the valid update
 */
TEST_F(AdjRibInboundFixture, EnforceFirstAsUsesRemoteAsTest) {
  // make current adjRib not in same confed
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      true, // callSessionEstablished
      kLocalAs2, // global as
      kLocalAs3, // local as
      kRemoteAs1,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      nullptr, // policy manager, not used
      std::nullopt, // ingress policy name, not used
      kIsConfedPeerFalse,
      static_cast<uint32_t>(kLocalAs1), // local confed as
      static_cast<uint32_t>(kLocalAs2), // as confed id
      AdvertiseLinkBandwidth::DISABLE, // advertised lbdw
      ReceiveLinkBandwidth::ACCEPT, // received lbdw
      std::nullopt, // lbdw bps
      ValidateRemoteAs{true}, // validate remote as
      kDefaultPreMaxRoutes, // max routes
      false, // warning only
      kDefaultPreWarningThreshold, // warning limit
      kDefaultPostMaxRoutes, // max accepted routes
      false, // accept warning only
      kDefaultPostWarningThreshold, // accept warning limit
      kPeerId1, // peerId
      IsRedistributePeer{false}, // isRedistributePeer
      std::make_shared<std::atomic<bool>>(false), // isSafeModeOn
      true, // enforce_first_as
      kAsSeqAsNum); // remoteAs

  fm_->addTask([&] {
    {
      /*
       * enforce-first-as validation should fail as left most AS of this update
       * does not match peer AS
       */
      auto update = createV4BgpUpdateSingleAnnounce();
      update->attrs()->asPath()[0].asSequence()[0] = kRemoteAs1;

      BgpAttrAsPathSegment segment2;
      segment2.asSequence()->push_back(kAsSeqAsNum);
      update->attrs()->asPath()->push_back(segment2);

      adjRibInQ_->fiberPush(std::move(update));
    }
    {
      auto update = createV4BgpUpdateSingleAnnounce(kV4Prefix2);
      update->attrs()->asPath()[0].asSequence()[0] = kAsSeqAsNum;

      BgpAttrAsPathSegment segment2;
      segment2.asSequence()->push_back(kAsSeqAsNum);
      update->attrs()->asPath()->push_back(segment2);

      adjRibInQ_->fiberPush(std::move(update));
    }
  });

  fm_->addTask([&] {
    fiberSleepFor(50ms);
    EXPECT_EQ(1, ribInQ_.size());

    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);

    auto adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
    EXPECT_NE(nullptr, adjRibEntry2);
    EXPECT_EQ(1, adjRib_->getStats().getEnforceFirstAsRejects());

    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * Verify enforce-first-as behavior for confed eBGP peer
 * Create 2 updates, 1 with the correct AS number at the left most position and
 * one without. The invalid one should be discarded, and ribInQ_ should have
 * only the prefix from the valid update
 */
TEST_F(AdjRibInboundFixture, EnforceFirstAsConfedTest) {
  // make current adjRib not in same confed
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      true, // callSessionEstablished
      kLocalAs2, // global as
      kLocalAs3, // local as
      kRemoteAs1,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      nullptr, // policy manager, not used
      std::nullopt, // ingress policy name, not used
      kIsConfedPeerTrue, // isConfedPeer
      static_cast<uint32_t>(kLocalAs2), // local confed as
      static_cast<uint32_t>(kLocalAs3), // as confed id
      AdvertiseLinkBandwidth::DISABLE, // advertised lbdw
      ReceiveLinkBandwidth::ACCEPT, // received lbdw
      std::nullopt, // lbdw bps
      ValidateRemoteAs{true}, // validate remote as
      kDefaultPreMaxRoutes, // max routes
      false, // warning only
      kDefaultPreWarningThreshold, // warning limit
      kDefaultPostMaxRoutes, // max accepted routes
      false, // accept warning only
      kDefaultPostWarningThreshold, // accept warning limit
      kPeerId1, // peerId
      IsRedistributePeer{false}, // isRedistributePeer
      std::make_shared<std::atomic<bool>>(false), // isSafeModeOn
      true); // enforce_first_as

  fm_->addTask([&] {
    {
      /*
       * enforce-first-as validation should fail as left most AS of this update
       * does not match peer AS
       */
      auto update = createV4BgpUpdateSingleAnnounce(
          kV4Prefix1,
          kV4Nexthop1,
          kMed,
          kOriginatorId,
          BgpAttrOrigin::BGP_ORIGIN_IGP,
          kDefaultPathID,
          kIsConfedPeerTrue);
      update->attrs()->asPath()[0].asConfedSequence()[0] = 2;

      BgpAttrAsPathSegment segment2;
      segment2.asConfedSequence()->push_back(kAsSeqAsNum);
      update->attrs()->asPath()->push_back(segment2);

      adjRibInQ_->fiberPush(std::move(update));
    }
    {
      auto update = createV4BgpUpdateSingleAnnounce(
          kV4Prefix2,
          kV4Nexthop2,
          kMed,
          kOriginatorId,
          BgpAttrOrigin::BGP_ORIGIN_IGP,
          kDefaultPathID,
          kIsConfedPeerTrue);
      update->attrs()->asPath()[0].asConfedSequence()[0] = kRemoteAs1;

      BgpAttrAsPathSegment segment2;
      segment2.asConfedSequence()->push_back(kAsSeqAsNum);
      update->attrs()->asPath()->push_back(segment2);

      adjRibInQ_->fiberPush(std::move(update));
    }
  });

  fm_->addTask([&] {
    fiberSleepFor(50ms);
    EXPECT_EQ(1, ribInQ_.size());
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);

    auto adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
    EXPECT_NE(nullptr, adjRibEntry2);
    EXPECT_EQ(1, adjRib_->getStats().getEnforceFirstAsRejects());

    terminateAdjRib();
  });

  evb_.loop();
}

/******************************************************************************
 *      END   -   Invalid BGP attribute related route rejection tests.        *
 ******************************************************************************/

/******************************************************************************
 *      START   -   Session teardown related tests.                           *
 ******************************************************************************/

// Verify that all routes are cleaned up when session goes down
TEST_F(AdjRibInboundFixture, SessionGoingDown) {
  setupAdjRib();
  std::vector<folly::CIDRNetwork> prefixSet{kV6Prefix1, kV6Prefix2};

  PrefixPathIds withDrawalPrefixSet{
      {kV6Prefix1, kDefaultPathID}, {kV6Prefix2, kDefaultPathID}};

  fm_->addTask([&] {
    auto update = createV6BgpUpdateMultipleAnnounce(prefixSet);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    // RibInAnnouncement route messages
    auto msg1 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");

    for (auto& prefix : prefixSet) {
      // Verify multiple RIB entries are created.
      auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, prefix);
      EXPECT_NE(nullptr, adjRibEntry);
    }
    // Terminate the session
    terminateAdjRib();
    fiberSleepFor(50ms);

    // Verify AdjRibStaleTree size is zero since we are not in GR
    EXPECT_EQ(0, adjRib_->getRibInStaleTreeSize());
    // Verify AdjRibLiteTree size zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/false));
    // Verify AdjRibTree size is zero
    EXPECT_EQ(
        0,
        adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

    // RibInWithdrawal message
    auto msg2 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    auto withdraw = std::get<RibInWithdrawal>(msg2);
    EXPECT_EQ(withDrawalPrefixSet, withdraw.pfxPathIds);
    for (auto& prefix : prefixSet) {
      // Verify that all learnt routes are deleted
      auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, prefix);
      EXPECT_EQ(nullptr, adjRibEntry);
    }
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());
  });

  evb_.loop();
}

/*
 * Verify that counters are proper if BGP session flaps
 * Send 2 v6 prefixes, verify count is fine, terminate session, send 3 v6
 * prefixes verify that count is now 3, terminate session, send 1 v4 prefix
 * verify count is 1. (Verify clean up after every termination)
 */
TEST_F(AdjRibInboundFixture, VerifyCounterWithSessionFlap) {
  setupAdjRib();

  std::vector<folly::CIDRNetwork> prefixSet1{kV6Prefix1, kV6Prefix2};
  std::vector<folly::CIDRNetwork> prefixSet2{
      kV6Prefix1, kV6Prefix2, kV6Prefix3};
  std::vector<folly::CIDRNetwork> prefixSet3{kV4Prefix1};

  fm_->addTask([&] {
    auto update = createV6BgpUpdateMultipleAnnounce(prefixSet1);
    adjRibInQ_->fiberPush(std::move(update));
    fiberSleepFor(50ms);
    update = createV6BgpUpdateMultipleAnnounce(prefixSet2);
    adjRibInQ_->fiberPush(std::move(update));
    fiberSleepFor(50ms);
    update = createV4BgpUpdateMultipleAnnounce(prefixSet3);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    auto lambdaVerifyCounts = [&](int size) {
      // wait till RibInAnnouncement route message is sent
      facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      // Verify AdjRibLiteTree size zero
      EXPECT_EQ(
          size,
          adjRib_->getRibTreeSize(
              /*ingress=*/true, /*isAddPathEnabled=*/false));
      // Verify AdjRibTree size is zero
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));
      EXPECT_EQ(size, adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(size, adjRib_->getStats().getPostInPrefixCount());

      // Terminate the session
      terminateAdjRib();

      // wait till RibInWithdrawal message is sent
      facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      // Verify AdjRibStaleTree size is zero since we are not in GR
      EXPECT_EQ(0, adjRib_->getRibInStaleTreeSize());
      // Verify AdjRibLiteTree size zero
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(
              /*ingress=*/true, /*isAddPathEnabled=*/false));
      // Verify AdjRibTree size is zero
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

      EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());
    };

    lambdaVerifyCounts(prefixSet1.size());
    reEstablishSession(kShortGrRestartTime);
    lambdaVerifyCounts(prefixSet2.size());

    reEstablishSession(kShortGrRestartTime);
    lambdaVerifyCounts(prefixSet3.size());
  });

  evb_.loop();
}

/******************************************************************************
 *      END   -   Session teardown related tests.                             *
 ******************************************************************************/

/******************************************************************************
 *      START   -   Graceful restart related tests.                           *
 ******************************************************************************/

/*
 * Verify that routes are kept till stale path timer expiry after session
 * flaps.
 *
 * 1) Peer with long restart time sends 2 v6 prefixes. Verify count is fine,
 * terminate session, and verify that routes are kept.
 *
 * 2) Peer comes up with long restart time and sends 3 v6 prefixes. Verify
 * count is now 3, terminate session, and verify that the 3 routes are kept.
 *
 * 3) Peer comes up with 1 sec restart time and sends 1 v4 prefix. Verify that
 * count is now 4, terminate session, and verify that only latest 1 route is
 * kept. Here we are doing successive flaps before stale path timer expiry
 * All routes marked stale will be deleted at terminate. Wait 1 sec see that
 * single prefix is also cleaned up after stale path timer expiry.
 *
 * 4) Peer comes up with 0 sec restart time and sends 1 v4 prefix. Verify that
 * count is 1 initially, terminate session, and verify that no routes are
 * kept.
 */
TEST_F(AdjRibInboundFixture, VerifyStalePathTimerWithSessionFlap) {
  setupAdjRib(
      kShortGrRestartTime, // local
      kLongGrRestartTime // remote
  );

  std::vector<folly::CIDRNetwork> prefixSet1{kV6Prefix1, kV6Prefix2};
  std::vector<folly::CIDRNetwork> prefixSet2{
      kV6Prefix1, kV6Prefix2, kV6Prefix3};
  std::vector<folly::CIDRNetwork> prefixSet3{kV4Prefix1};

  std::array<folly::fibers::Baton, 3> syncBaton;

  fm_->addTask([&] {
    auto update = createV6BgpUpdateMultipleAnnounce(prefixSet1);
    adjRibInQ_->fiberPush(std::move(update));
    facebook::bgp::test::boundedBatonWait(syncBaton[0], "syncBaton[0]");
    update = createV6BgpUpdateMultipleAnnounce(prefixSet2);
    adjRibInQ_->fiberPush(std::move(update));
    facebook::bgp::test::boundedBatonWait(syncBaton[1], "syncBaton[1]");
    update = createV4BgpUpdateMultipleAnnounce(prefixSet3);
    adjRibInQ_->fiberPush(std::move(update));
    facebook::bgp::test::boundedBatonWait(syncBaton[2], "syncBaton[2]");
    update = createV4BgpUpdateMultipleAnnounce(prefixSet3);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    {
      // wait till RibInAnnouncement route message is sent
      facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPostInPrefixCount());
      EXPECT_EQ(1, adjRib_->getStats().getRecvUpdateMsgs());
      EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv4());
      EXPECT_EQ(1, adjRib_->getStats().getRecvAnnouncementsIpv6());
      EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
      EXPECT_EQ(4, adjRib_->getStats().getTotalAttributeUpdates());

      // Terminate the session
      terminateAdjRib(true);

      // wait for sometime
      fiberSleepFor(10ms);

      /*
       * Verify AdjRibStaleTree size is non-zero since we are in GR
       * Note that size here is for num prefixes because only prefix
       * is used as key to radix tree.
       */
      EXPECT_EQ(prefixSet1.size(), adjRib_->getRibInStaleTreeSize());
      // Verify AdjRibLiteTree size has gone to zero
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(
              /*ingress=*/true, /*isAddPathEnabled=*/false));
      // Verify AdjRibTree size is zero
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPostInPrefixCount());
      EXPECT_EQ(0, adjRib_->getStats().getRecvUpdateMsgs());
      EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv4());
      EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
      EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
      EXPECT_EQ(4, adjRib_->getStats().getTotalAttributeUpdates());
    }
    syncBaton[0].post();
    {
      adjRibInQ_->open();
      reEstablishSession(kLongGrRestartTime);
      // wait till RibInAnnouncement route message is sent
      facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      EXPECT_EQ(prefixSet2.size(), adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(prefixSet2.size(), adjRib_->getStats().getPostInPrefixCount());
      EXPECT_EQ(1, adjRib_->getStats().getRecvUpdateMsgs());
      EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv4());
      EXPECT_EQ(1, adjRib_->getStats().getRecvAnnouncementsIpv6());
      EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
      EXPECT_EQ(6, adjRib_->getStats().getTotalAttributeUpdates());

      // Terminate the session
      terminateAdjRib(true);

      // wait for sometime
      fiberSleepFor(10ms);

      // Verify AdjRibStaleTree size is non-zero since we are in GR
      EXPECT_EQ(prefixSet2.size(), adjRib_->getRibInStaleTreeSize());
      // Verify AdjRibLiteTree size has gone to zero
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(
              /*ingress=*/true, /*isAddPathEnabled=*/false));
      // Verify AdjRibTree size is zero
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

      EXPECT_EQ(prefixSet2.size(), adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(prefixSet2.size(), adjRib_->getStats().getPostInPrefixCount());
      EXPECT_EQ(0, adjRib_->getStats().getRecvUpdateMsgs());
      EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv4());
      EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
      EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
      EXPECT_EQ(6, adjRib_->getStats().getTotalAttributeUpdates());
    }
    syncBaton[1].post();
    {
      adjRibInQ_->open();
      reEstablishSession(std::chrono::seconds(1));
      // wait till RibInAnnouncement route message is sent
      facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      EXPECT_EQ(
          prefixSet2.size() + prefixSet3.size(),
          adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(
          prefixSet2.size() + prefixSet3.size(),
          adjRib_->getStats().getPostInPrefixCount());

      // Terminate the session
      terminateAdjRib(true);

      // wait for sometime
      fiberSleepFor(10ms);
      EXPECT_EQ(prefixSet3.size(), adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(prefixSet3.size(), adjRib_->getStats().getPostInPrefixCount());
      EXPECT_EQ(0, adjRib_->getStats().getRecvUpdateMsgs());
      EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv4());
      EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
      EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
      EXPECT_EQ(8, adjRib_->getStats().getTotalAttributeUpdates());

      // wait till stale path timer expiry
      fiberSleepFor(1050ms);

      EXPECT_EQ(0, adjRib_->getRibInStaleTreeSize());
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(
              /*ingress=*/true, /*isAddPathEnabled=*/false));
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

      EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());
      EXPECT_EQ(0, adjRib_->getStats().getRecvUpdateMsgs());
      EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv4());
      EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
      EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
      EXPECT_EQ(8, adjRib_->getStats().getTotalAttributeUpdates());
    }
    syncBaton[2].post();
    {
      adjRibInQ_->open();
      reEstablishSession(kShortGrRestartTime);
      // wait for short time
      fiberSleepFor(10ms);
      EXPECT_EQ(prefixSet3.size(), adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(prefixSet3.size(), adjRib_->getStats().getPostInPrefixCount());
      EXPECT_EQ(1, adjRib_->getStats().getRecvUpdateMsgs());
      EXPECT_EQ(1, adjRib_->getStats().getRecvAnnouncementsIpv4());
      EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
      EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
      EXPECT_EQ(10, adjRib_->getStats().getTotalAttributeUpdates());

      // Terminate the session
      terminateAdjRib(true);

      // wait for sometime
      fiberSleepFor(10ms);
      EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());
      EXPECT_EQ(0, adjRib_->getStats().getRecvUpdateMsgs());
      EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv4());
      EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
      EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
      EXPECT_EQ(10, adjRib_->getStats().getTotalAttributeUpdates());
    }
  });

  evb_.loop();
}

/*
 * Test to verify we handle correctly if we receive a withdrawal for
 * a prefix which is learnt from other peers, before EOR after a session flap.
 * Steps are:
 * Establish a session,
 * Send update with two prefixes p1, p2
 * Terminate the session
 * Establish the session back
 * Send p1 from Rib (simulating learnt prefix from another peer)
 * Send prefix p1 withdrawal, p2 update
 * Validate that p1 adjRibEntry is gone in AdjRibIn because the prefix learned
 * from another peer is in AdjRibOut.
 * Verify that received count is reduced to 1 (even with 2 adjRibEntry)
 * Send EOR
 * Validate counters and routes. Received count is still 1.
 */
TEST_F(AdjRibInboundFixture, VerifyWithdrawalBeforeStalePathExpiry) {
  setupAdjRib(
      kShortGrRestartTime, // local
      1s // remote
  );

  std::vector<folly::CIDRNetwork> prefixSet1{kV4Prefix1, kV4Prefix2};
  folly::fibers::Baton sendFirstUpdate, sendWithdrawAndUpdate, sendEor;

  // Fiber to send updates and eors
  fm_->addTask([&] {
    sendFirstUpdate.wait();
    auto update = createV4BgpUpdateMultipleAnnounce(prefixSet1);
    adjRibInQ_->fiberPush(std::move(update));
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv4));
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv6));

    sendWithdrawAndUpdate.wait();
    update = createV4BgpUpdateMultipleAnnounce({kV4Prefix2});
    adjRibInQ_->fiberPush(std::move(update));
    auto withdraw = createV4BgpUpdateSingleWithdraw(kV4Prefix1);
    adjRibInQ_->fiberPush(std::move(withdraw));

    sendEor.wait();
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv4));
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv6));
  });

  fm_->addTask([&] {
    {
      /*
       * sessionEstablished already called inside setup method.
       * Trigger sending of Update message from fiber-bgp-peer.
       */
      sendFirstUpdate.post();
      facebook::bgp::test::boundedBlockingPop(fromAdjRibQ_, "fromAdjRibQ_");
      // wait till RibInAnnouncement route message is sent
      facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");

      // Verify AdjRibStaleTree size is zero since no GR yet
      EXPECT_EQ(0, adjRib_->getRibInStaleTreeSize());
      // Verify AdjRibLiteTree size is non-zero
      EXPECT_EQ(
          prefixSet1.size(),
          adjRib_->getRibTreeSize(
              /*ingress=*/true, /*isAddPathEnabled=*/false));
      // Verify AdjRibTree size is zero
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(/*ingress=*/true, /*isAddPathEnabled=*/true));

      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPostInPrefixCount());
      EXPECT_EQ(1, adjRib_->getStats().getRecvUpdateMsgs());
      EXPECT_EQ(1, adjRib_->getStats().getRecvAnnouncementsIpv4());
      EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
      EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
      EXPECT_EQ(4, adjRib_->getStats().getTotalAttributeUpdates());
      fiberSleepFor(10ms);

      // Terminate the session
      terminateAdjRib(true);
      fiberSleepFor(10ms);
      // Verify AdjRibStaleTree size is now non-zero
      EXPECT_EQ(prefixSet1.size(), adjRib_->getRibInStaleTreeSize());
      // Verify AdjRibLiteTree size has gone to zero
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(
              /*ingress=*/true, /*isAddPathEnabled=*/false));

      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1));
      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2));
      // Re-establish session
      reEstablishSession(std::chrono::seconds(1));
      fiberSleepFor(10ms);
      // Verify AdjRibStaleTree size is now non-zero
      EXPECT_EQ(prefixSet1.size(), adjRib_->getRibInStaleTreeSize());
      // Verify AdjRibLiteTree size has gone to zero
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(
              /*ingress=*/true, /*isAddPathEnabled=*/false));

      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1));
      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2));
      EXPECT_EQ(2, adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(2, adjRib_->getStats().getPostInPrefixCount());
      EXPECT_EQ(0, adjRib_->getStats().getRecvUpdateMsgs());
      EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv4());
      EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
      EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
      EXPECT_EQ(4, adjRib_->getStats().getTotalAttributeUpdates());

      // Simulating the same route learnt from another peer
      TinyPeerInfo localPeerV4_{
          kLocalV4RoutePeerAddr, kLocalRouteAs, 0, BgpSessionType::IBGP, false};
      auto ribMsg =
          createRibSingleAnnounce(kV4Prefix1, kV4Nexthop1, localPeerV4_, true);
      adjRib_->processRibMessage(ribMsg);
      fiberSleepFor(10ms);

      // Send update for only kV4Prefix2 and withdrawal for p1
      sendWithdrawAndUpdate.post();
      // Wait till withdrawal is processed and announced to RIB
      facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1));
      EXPECT_NE(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2));

      // Verify AdjRibStaleTree size is now zero, since stale entry was moved
      EXPECT_EQ(0, adjRib_->getRibInStaleTreeSize());
      // Verify AdjRibLiteTree size is 1
      EXPECT_EQ(
          1,
          adjRib_->getRibTreeSize(
              /*ingress=*/true, /*isAddPathEnabled=*/false));

      /*
       * NOTE: Even though two entries are present, as one entry preIn is
       * nullptr we will see that count is only 1 and not 2
       */
      EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCount());
      EXPECT_EQ(2, adjRib_->getStats().getRecvUpdateMsgs());
      EXPECT_EQ(1, adjRib_->getStats().getRecvAnnouncementsIpv4());
      EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
      EXPECT_EQ(1, adjRib_->getStats().getRecvWithdrawals());
      EXPECT_EQ(6, adjRib_->getStats().getTotalAttributeUpdates());

      // Send EOR (This will trigger clean up of stale routes)
      sendEor.post();
      // wait till EoR Ack goes to peer-mgr
      facebook::bgp::test::boundedBlockingPop(fromAdjRibQ_, "fromAdjRibQ_");

      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1));
      EXPECT_NE(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2));
      // Verify that counters are still proper
      EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCount());
      EXPECT_EQ(2, adjRib_->getStats().getRecvUpdateMsgs());
      EXPECT_EQ(1, adjRib_->getStats().getRecvAnnouncementsIpv4());
      EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
      EXPECT_EQ(1, adjRib_->getStats().getRecvWithdrawals());
      EXPECT_EQ(6, adjRib_->getStats().getTotalAttributeUpdates());

      // Now that test is done tear down the session in non-gr mode quickly
      terminateAdjRib();
      fiberSleepFor(10ms);

      // Verify AdjRibStaleTree size is zero
      EXPECT_EQ(0, adjRib_->getRibInStaleTreeSize());
      // Verify AdjRibLiteTree size is zero
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(
              /*ingress=*/true, /*isAddPathEnabled=*/false));
    }
  });
  evb_.loop();
}

/*
 * (1) have path X in stale adj rib tree
 * (2) receive withdrawal for X
 * (3) verify stale tree empty, adjRibIn empty, withdrawal goes to RibIn
 */
TEST_F(AdjRibInboundFixture, VerifyWithdrawalBeforeStalePathExpiryAddPath) {
  setupAdjRib(kShortGrRestartTime, kShortGrRestartTime);

  // (1) add path to adjRibInStale_
  auto dummyPathId = 5;
  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>>
      adjRibStaleTreeEntry;
  auto adjRibEntry = std::make_unique<AdjRibEntry>(dummyPathId);
  adjRibEntry->setPostAttr(
      std::make_shared<BgpPath>()); // withdrawal will not get processed
                                    // correctly if postAttr is not set
  adjRibStaleTreeEntry.emplace(dummyPathId, std::move(adjRibEntry));
  adjRib_->adjRibInStale_.insert(
      kV4Prefix1.first, kV4Prefix1.second, std::move(adjRibStaleTreeEntry));

  std::vector<folly::CIDRNetwork> prefixSet1{kV4Prefix1};

  // (2) simulate withdrawal for same path
  fm_->addTask([&] {
    adjRib_->recAddPath_ = true;
    auto withdraw = createV4BgpUpdateSingleWithdraw(kV4Prefix1, dummyPathId);
    adjRibInQ_->fiberPush(std::move(withdraw));
  });

  fm_->addTask([&] {
    /*
     * (3) wait until withdrawal is processed and announced to Rib. Verify
     * adjRibIn and adjRibInStale empty
     */
    facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    EXPECT_EQ(adjRib_->adjRibInStale_.size(), 0);
    EXPECT_EQ(adjRib_->adjRibInPathTree_.size(), 0);

    // now that test is done tear down the session
    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * Test to verify that stale routes are cleaned up as soon as EoR is
 * received.
 */
TEST_F(AdjRibInboundFixture, VerifyStalePathCleanupWithEoR) {
  setupAdjRib(
      kShortGrRestartTime, // local
      1s // remote
  );

  std::vector<folly::CIDRNetwork> prefixSet1{kV6Prefix1, kV6Prefix2};
  folly::fibers::Baton sendFirstUpdate, sendSecondUpdate;

  // Fiber to send updates and eors
  fm_->addTask([&] {
    sendFirstUpdate.wait();
    auto update = createV6BgpUpdateMultipleAnnounce(prefixSet1);
    adjRibInQ_->fiberPush(std::move(update));
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv4));
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv6));
    sendSecondUpdate.wait();
    update = createV6BgpUpdateMultipleAnnounce({kV6Prefix1});
    adjRibInQ_->fiberPush(std::move(update));
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv4));
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv6));
  });

  fm_->addTask([&] {
    {
      /*
       * sessionEstablished already called inside setup method.
       * Trigger sending of Update message from fiber-bgp-peer.
       */
      sendFirstUpdate.post();
      facebook::bgp::test::boundedBlockingPop(fromAdjRibQ_, "fromAdjRibQ_");
      // wait till RibInAnnouncement route message is sent
      facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPostInPrefixCount());
      /*
       * Both received EoR PDUs (v4 + v6) are counted at the control plane; this
       * converges with the socket-layer socket_rx_eor_msgs.
       */
      EXPECT_EQ(2, adjRib_->getStats().getRecvEndOfRibMsgs());
      // wait for sometime
      fiberSleepFor(10ms);
      // Terminate the session
      terminateAdjRib(true);
      // wait for sometime
      fiberSleepFor(10ms);
      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix1));
      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix2));
      EXPECT_NE(
          nullptr, adjRib_->getStaleRibInEntry(kV6Prefix1, kDefaultPathID));
      EXPECT_NE(
          nullptr, adjRib_->getStaleRibInEntry(kV6Prefix2, kDefaultPathID));
      // Re-establish session
      reEstablishSession(std::chrono::seconds(1));
      fiberSleepFor(10ms);
      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix1));
      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix2));
      EXPECT_NE(
          nullptr, adjRib_->getStaleRibInEntry(kV6Prefix1, kDefaultPathID));
      EXPECT_NE(
          nullptr, adjRib_->getStaleRibInEntry(kV6Prefix2, kDefaultPathID));
      EXPECT_EQ(2, adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(2, adjRib_->getStats().getPostInPrefixCount());
      // Send update for only kV6Prefix1 and then send the EoRs
      sendSecondUpdate.post();
      // wait till EoR Ack goes to peer-mgr
      facebook::bgp::test::boundedBlockingPop(fromAdjRibQ_, "fromAdjRibQ_");

      // Check that kV6Prefix2 (stale route) is removed
      EXPECT_NE(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix1));
      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix2));
      EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCount());

      // Now that test is done tear down the session in non-gr mode quickly
      terminateAdjRib();
    }
  });
  evb_.loop();
}

// Verify interaction of restart and stale-path timer.
TEST_F(AdjRibInboundFixture, VerifyGRRestartStalePathRestore) {
  setupAdjRib(
      kShortGrRestartTime, // local
      kLongGrRestartTime // remote
  );
  RibStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();

  std::vector<folly::CIDRNetwork> prefixSet1{kV6Prefix1, kV6Prefix2};
  std::array<folly::fibers::Baton, 2> syncBaton;

  fm_->addTask([&] {
    facebook::bgp::test::boundedBatonWait(syncBaton[0], "syncBaton[0]");
    auto update = createV6BgpUpdateMultipleAnnounce(prefixSet1);
    adjRibInQ_->fiberPush(std::move(update));
    facebook::bgp::test::boundedBatonWait(syncBaton[1], "syncBaton[1]");
    update = createV6BgpUpdateMultipleAnnounce(prefixSet1);
    // update nexthop and attributes
    *update->mpAnnounced()->nexthop() = toBinaryAddress(kV6Nexthop2);
    update->attrs()->localPref() = kLocalPref2;

    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    /*
     * Verify that priviously announced paths are cleared of stale,
     * after session restablishment and receipt of updates before stale path
     * timer expiry.
     */
    {
      syncBaton[0].post();
      // wait till RibInAnnouncement route message is sent
      facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPreInPrefixCount());
      // Terminate the session
      terminateAdjRib(true);
      // wait for sometime
      fiberSleepFor(10ms);
      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPreInPrefixCount());
      // Restablish session
      reEstablishSession(std::chrono::seconds(1));
      /*
       * Delay sending updates check that in between routes are marked as
       * stale.
       */
      fiberSleepFor(50ms);
      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix1));
      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix2));
      EXPECT_FALSE(adjRib_->adjRibInStale_
                       .exactMatch(kV6Prefix1.first, kV6Prefix1.second)
                       .atEnd());
      EXPECT_FALSE(adjRib_->adjRibInStale_
                       .exactMatch(kV6Prefix2.first, kV6Prefix2.second)
                       .atEnd());
      // Stale ODS counter should reflect entries moved to stale tree
      tcData->publishStats();
      EXPECT_EQ(2, tcData->getCounter(RibStats::kAdjRibInStaleCount));

      // Send updates and check that stale routes are cleared.
      syncBaton[1].post();
      fiberSleepFor(10ms);
      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPreInPrefixCount());
      EXPECT_NE(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix1));
      EXPECT_NE(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix2));
      EXPECT_TRUE(adjRib_->adjRibInStale_
                      .exactMatch(kV6Prefix1.first, kV6Prefix1.second)
                      .atEnd());
      EXPECT_TRUE(adjRib_->adjRibInStale_
                      .exactMatch(kV6Prefix2.first, kV6Prefix2.second)
                      .atEnd());
      // Stale ODS counter should be 0 after all entries promoted back
      tcData->publishStats();
      EXPECT_EQ(0, tcData->getCounter(RibStats::kAdjRibInStaleCount));
      terminateAdjRib();
    }
  });
  evb_.loop();
}

TEST_F(AdjRibInboundFixture, VerifyGRRestartStalePathRestoreAddPath) {
  setupAdjRib(
      kShortGrRestartTime, // local
      k10SecGrRestartTime // remote
  );

  std::vector<folly::CIDRNetwork> prefixSet1{kV6Prefix1, kV6Prefix2};
  std::array<folly::fibers::Baton, 4> syncBaton; // Added one more baton

  fm_->addTask([&] {
    facebook::bgp::test::boundedBatonWait(syncBaton[0], "syncBaton[0]");
    adjRib_->recAddPath_ = true;
    auto update = createV6BgpUpdateMultipleAnnounce(prefixSet1);
    for (auto& rigPrefix : *update->mpAnnounced()->prefixes()) {
      rigPrefix.pathId() = 1;
    }
    adjRibInQ_->fiberPush(std::move(update));

    update = createV6BgpUpdateMultipleAnnounce(prefixSet1);
    for (auto& rigPrefix : *update->mpAnnounced()->prefixes()) {
      rigPrefix.pathId() = 2;
    }
    adjRibInQ_->fiberPush(std::move(update));

    facebook::bgp::test::boundedBatonWait(syncBaton[1], "syncBaton[1]");
    update = createV6BgpUpdateMultipleAnnounce(prefixSet1);
    for (auto& rigPrefix : *update->mpAnnounced()->prefixes()) {
      rigPrefix.pathId() = 2;
    }
    adjRibInQ_->fiberPush(std::move(update));

    facebook::bgp::test::boundedBatonWait(syncBaton[2], "syncBaton[2]");
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv4));
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv6));
  });

  fm_->addTask([&] {
    /*
     * Verify that priviously announced paths are cleared of stale,
     * after session restablishment and receipt of updates before stale path
     * timer expiry.
     */
    {
      syncBaton[0].post();
      // wait till RibInAnnouncement route message is sent
      facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      EXPECT_EQ(4, adjRib_->getStats().getPreInPrefixCount());
      // Terminate the session
      terminateAdjRib(true);
      // wait for sometime
      fiberSleepFor(10ms);
      EXPECT_EQ(4, adjRib_->getStats().getPreInPrefixCount());
      // Restablish session
      reEstablishSession(std::chrono::seconds(1));
      adjRib_->recAddPath_ = true;
      /*
       * Delay sending updates check that in between routes are marked as
       * stale.
       */
      fiberSleepFor(50ms);
      EXPECT_EQ(4, adjRib_->getStats().getPreInPrefixCount());

      /*
       * Verify both the prefixes don't exist in adjRibTree
       */
      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix1));
      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix2));

      /*
       * Stale Tree size of prefixes should be 2
       */
      EXPECT_EQ(2, adjRib_->getRibInStaleTreeSize());
      /*
       * Stale Tree size including all paths should be 4
       */
      EXPECT_EQ(4, adjRib_->getRibInStaleTreePaths());
      /*
       * Verify both the prefixes are now in Stale_ tree with stale paths 1 & 2
       */
      auto pfx1Itr = adjRib_->adjRibInStale_.exactMatch(
          kV6Prefix1.first, kV6Prefix1.second);
      EXPECT_FALSE(pfx1Itr.atEnd());
      EXPECT_EQ(pfx1Itr.value().size(), 2);
      EXPECT_TRUE(pfx1Itr.value().contains(1));
      EXPECT_TRUE(pfx1Itr.value().contains(2));
      auto pfx2Itr = adjRib_->adjRibInStale_.exactMatch(
          kV6Prefix2.first, kV6Prefix2.second);
      EXPECT_FALSE(pfx2Itr.atEnd());
      EXPECT_EQ(pfx2Itr.value().size(), 2);
      EXPECT_TRUE(pfx2Itr.value().contains(1));
      EXPECT_TRUE(pfx2Itr.value().contains(2));

      // Send updates and check that stale routes are cleared.
      syncBaton[1].post();
      fiberSleepFor(10ms);
      EXPECT_EQ(4, adjRib_->getStats().getPreInPrefixCount());

      /*
       * Stale Tree size of prefixes should be 2
       */
      EXPECT_EQ(2, adjRib_->getRibInStaleTreeSize());
      /*
       * Stale Tree size including all paths should now be 2
       */
      EXPECT_EQ(2, adjRib_->getRibInStaleTreePaths());
      /*
       * Both the prefixes with pathId 2 should be in adjRibIn tree now
       */
      EXPECT_NE(nullptr, adjRib_->getRibEntry(true, kV6Prefix1, 2));
      EXPECT_NE(nullptr, adjRib_->getRibEntry(true, kV6Prefix2, 2));
      /*
       * Same prefixes with pathId 1 still would be in Stale_ tree
       */
      pfx1Itr = adjRib_->adjRibInStale_.exactMatch(
          kV6Prefix1.first, kV6Prefix1.second);
      EXPECT_FALSE(pfx1Itr.atEnd());
      EXPECT_EQ(pfx1Itr.value().size(), 1);
      EXPECT_TRUE(pfx1Itr.value().contains(1));
      pfx2Itr = adjRib_->adjRibInStale_.exactMatch(
          kV6Prefix2.first, kV6Prefix2.second);
      EXPECT_FALSE(pfx2Itr.atEnd());
      EXPECT_EQ(pfx2Itr.value().size(), 1);
      EXPECT_TRUE(pfx2Itr.value().contains(1));

      syncBaton[2].post();
      auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(msg));
      auto withdrawal = std::get<RibInWithdrawal>(msg);
      EXPECT_EQ(kPeerAddr1, withdrawal.peer.addr);
      auto prefixSetAdjOutWithdrawl =
          PrefixPathIds{{kV6Prefix1, 1}, {kV6Prefix2, 1}};
      EXPECT_EQ(prefixSetAdjOutWithdrawl, withdrawal.pfxPathIds);

      EXPECT_EQ(2, adjRib_->getStats().getPreInPrefixCount());

      /*
       * Both the prefixes with pathId 2 should still be in
       * adjRibIn tree since those are not the paths being removed
       */
      EXPECT_NE(nullptr, adjRib_->getRibEntry(true, kV6Prefix1, 2));
      EXPECT_NE(nullptr, adjRib_->getRibEntry(true, kV6Prefix2, 2));
      /*
       * Same prefixes with pathId 1 that were in Stale tree, should
       * have been removed
       */
      EXPECT_TRUE(adjRib_->adjRibInStale_
                      .exactMatch(kV6Prefix1.first, kV6Prefix1.second)
                      .atEnd());
      EXPECT_TRUE(adjRib_->adjRibInStale_
                      .exactMatch(kV6Prefix2.first, kV6Prefix2.second)
                      .atEnd());
      /*
       * Stale Tree size of prefixes should be 0
       */
      EXPECT_EQ(0, adjRib_->getRibInStaleTreeSize());

      terminateAdjRib();
    }
  });
  evb_.loop();
}

/*
 * When acting as a GR helper with receive ADD-PATH enabled,
 * from the helper's perspective, pathIDs are not guaranteed to be the same
 * after GR. Say we have adjRibInStale of {pathID x -> path A, pathID y -> path
 * B}. There are a few interesting announcement cases to consider:
 * 1. Pre-restart path with yet-to-be-seen pathID - {pathID z -> path A}
 * 2. Pre-restart path with seen but different pathID - {pathID x -> path B}
 * This test covers these cases, mostly verifying AdjRibIn and AdjRibInStale
 * state, but also checking RibIn messages sent along the way
 */
TEST_F(AdjRibInboundFixture, VerifyGRRestartStalePathShuffleAddPath) {
  setupAdjRib(
      kShortGrRestartTime, // local
      k10SecGrRestartTime // remote
  );

  std::vector<folly::CIDRNetwork> prefixSet1{kV4Prefix1};
  std::array<folly::fibers::Baton, 4> syncBaton;

  // vars for pathID and path values mentioned in description above
  uint32_t x = 1;
  uint32_t y = 2;
  uint32_t z = 3;
  uint32_t A = 1;
  uint32_t B = 2;
  fm_->addTask([&] {
    adjRib_->recAddPath_ = true;
    // Step 0: First we announce {pathID x -> path A, pathID y -> path B}
    auto update = createV6BgpUpdateMultipleAnnounce(prefixSet1);
    for (auto& rigPrefix : *update->mpAnnounced()->prefixes()) {
      rigPrefix.pathId() = x;
    }
    update->attrs()->localPref() = A;
    adjRibInQ_->fiberPush(std::move(update));

    update = createV6BgpUpdateMultipleAnnounce(prefixSet1);
    for (auto& rigPrefix : *update->mpAnnounced()->prefixes()) {
      rigPrefix.pathId() = y;
    }
    update->attrs()->localPref() = B;
    adjRibInQ_->fiberPush(std::move(update));
    // Wait for routes to be marked as stale
    facebook::bgp::test::boundedBatonWait(syncBaton[1], "syncBaton[1]");

    // Step 1: Now announce {pathID z -> path A}
    update = createV6BgpUpdateMultipleAnnounce(prefixSet1);
    for (auto& rigPrefix : *update->mpAnnounced()->prefixes()) {
      rigPrefix.pathId() = z;
    }
    update->attrs()->localPref() = A;
    adjRibInQ_->fiberPush(std::move(update));
    // Wait for verification
    facebook::bgp::test::boundedBatonWait(syncBaton[2], "syncBaton[2]");

    // Step 2: Now announce {pathID x -> path B}
    update = createV6BgpUpdateMultipleAnnounce(prefixSet1);
    for (auto& rigPrefix : *update->mpAnnounced()->prefixes()) {
      rigPrefix.pathId() = x;
    }
    update->attrs()->localPref() = B;
    adjRibInQ_->fiberPush(std::move(update));
    // Wait for verification
    facebook::bgp::test::boundedBatonWait(syncBaton[3], "syncBaton[3]");

    /*
     * Step 3: Announce EOR. Other task should verify stale routes are moved as
     * expected
     */
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv4));
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv6));
  });

  fm_->addTask([&] {
    {
      /*
       * Step 0: Wait for initial announcements {pathID x -> path A, pathID y ->
       * path B}. Check they reach Rib...
       */
      auto ribInMsg1 =
          facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribInMsg1));
      auto announcement1 = std::get<RibInAnnouncement>(ribInMsg1);
      ASSERT_EQ(announcement1.pfxPathIds.size(), 1);
      EXPECT_EQ(get<0>(announcement1.pfxPathIds[0]), kV4Prefix1);
      EXPECT_EQ(get<1>(announcement1.pfxPathIds[0]), x);
      EXPECT_EQ(announcement1.attrs->getLocalPref(), A);
      auto ribInMsg2 =
          facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribInMsg2));
      auto announcement2 = std::get<RibInAnnouncement>(ribInMsg2);
      ASSERT_EQ(announcement2.pfxPathIds.size(), 1);
      EXPECT_EQ(get<0>(announcement2.pfxPathIds[0]), kV4Prefix1);
      EXPECT_EQ(get<1>(announcement2.pfxPathIds[0]), y);
      EXPECT_EQ(announcement2.attrs->getLocalPref(), B);
      // ...and check that they reach adjRib...
      EXPECT_EQ(2, adjRib_->getRibTreePeerEntriesCount(true, true));
      auto adjRibEntry_x = adjRib_->getRibEntry(true, kV4Prefix1, x);
      ASSERT_NE(nullptr, adjRibEntry_x);
      EXPECT_EQ(adjRibEntry_x->getPreIn()->getLocalPref(), A);
      auto adjRibEntry_y = adjRib_->getRibEntry(true, kV4Prefix1, y);
      ASSERT_NE(nullptr, adjRibEntry_y);
      EXPECT_EQ(adjRibEntry_y->getPreIn()->getLocalPref(), B);
      // ...now terminate the session...
      terminateAdjRib(true);

      // yield fiber to give another fiber task a chance to run
      fiberSleepFor(10ms);

      // ...restablish session...
      reEstablishSession(k10SecGrRestartTime);

      adjRib_->recAddPath_ = true;
      fiberSleepFor(50ms);
      /*
       * ...verify nothing sent to Rib yet, and both paths (IDs x and y) are
       * moved from path tree to stale tree ...
       */
      EXPECT_TRUE(ribInQ_.empty());
      EXPECT_EQ(0, adjRib_->getRibTreePeerEntriesCount(true, true));
      EXPECT_EQ(2, adjRib_->getRibInStaleTreePaths());
      auto stalePfxMatch = adjRib_->adjRibInStale_.exactMatch(
          kV4Prefix1.first, kV4Prefix1.second);
      ASSERT_TRUE(adjRib_->stalePathExist(stalePfxMatch));
      auto stalePath_x = stalePfxMatch.value().find(x);
      ASSERT_TRUE(stalePath_x != stalePfxMatch.value().end());
      EXPECT_EQ(stalePath_x->second->getPreIn()->getLocalPref(), A);
      auto stalePath_y = stalePfxMatch.value().find(y);
      ASSERT_TRUE(stalePath_y != stalePfxMatch.value().end());
      EXPECT_EQ(stalePath_y->second->getPreIn()->getLocalPref(), B);
      // ...finally, signal that paths are marked as stale
      syncBaton[1].post();

      /*
       * Step 1: Wait for first announce, {pathID z -> path A}. Verify it
       * reaches Rib...
       */
      auto ribInMsg3 =
          facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribInMsg3));
      auto announcement3 = std::get<RibInAnnouncement>(ribInMsg3);
      ASSERT_EQ(announcement3.pfxPathIds.size(), 1);
      EXPECT_EQ(get<0>(announcement3.pfxPathIds[0]), kV4Prefix1);
      EXPECT_EQ(get<1>(announcement3.pfxPathIds[0]), z);
      EXPECT_EQ(announcement3.attrs->getLocalPref(), A);
      // ...verify stale tree is untouched, and a new AdjRibIn entry for z->A...
      EXPECT_EQ(2, adjRib_->getRibInStaleTreePaths());
      stalePfxMatch = adjRib_->adjRibInStale_.exactMatch(
          kV4Prefix1.first, kV4Prefix1.second);
      ASSERT_TRUE(adjRib_->stalePathExist(stalePfxMatch));
      stalePath_x = stalePfxMatch.value().find(x);
      ASSERT_TRUE(stalePath_x != stalePfxMatch.value().end());
      EXPECT_EQ(stalePath_x->second->getPreIn()->getLocalPref(), A);
      stalePath_y = stalePfxMatch.value().find(y);
      ASSERT_TRUE(stalePath_y != stalePfxMatch.value().end());
      EXPECT_EQ(stalePath_y->second->getPreIn()->getLocalPref(), B);
      EXPECT_EQ(1, adjRib_->getRibTreePeerEntriesCount(true, true));
      auto adjRibEntry_z = adjRib_->getRibEntry(true, kV4Prefix1, z);
      ASSERT_NE(nullptr, adjRibEntry_z);
      EXPECT_EQ(adjRibEntry_z->getPreIn()->getLocalPref(), A);
      // ...finally, signal first announcement is verified
      syncBaton[2].post();

      /*
       * Step 2: Wait for second announce, {pathID x -> path B}. Verify it
       * reaches Rib...
       */
      auto ribInMsg4 =
          facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribInMsg4));
      auto announcement4 = std::get<RibInAnnouncement>(ribInMsg4);
      ASSERT_EQ(announcement4.pfxPathIds.size(), 1);
      EXPECT_EQ(get<0>(announcement4.pfxPathIds[0]), kV4Prefix1);
      EXPECT_EQ(get<1>(announcement4.pfxPathIds[0]), x);
      EXPECT_EQ(announcement4.attrs->getLocalPref(), B);
      /*
       * ...verify x is removed from stale tree, but the stale entry for y is
       * untouched...
       */
      EXPECT_EQ(1, adjRib_->getRibInStaleTreePaths());
      stalePfxMatch = adjRib_->adjRibInStale_.exactMatch(
          kV4Prefix1.first, kV4Prefix1.second);
      ASSERT_TRUE(adjRib_->stalePathExist(stalePfxMatch));
      stalePath_x = stalePfxMatch.value().find(x);
      EXPECT_TRUE(stalePath_x == stalePfxMatch.value().end());
      stalePath_y = stalePfxMatch.value().find(y);
      ASSERT_TRUE(stalePath_y != stalePfxMatch.value().end());
      EXPECT_EQ(stalePath_y->second->getPreIn()->getLocalPref(), B);
      /*
       * ...verify there are two live entries, one for x->B and still one for
       * z->A...
       */
      EXPECT_EQ(2, adjRib_->getRibTreePeerEntriesCount(true, true));
      adjRibEntry_x = adjRib_->getRibEntry(true, kV4Prefix1, x);
      ASSERT_NE(nullptr, adjRibEntry_x);
      EXPECT_EQ(adjRibEntry_x->getPreIn()->getLocalPref(), B);
      adjRibEntry_z = adjRib_->getRibEntry(true, kV4Prefix1, z);
      ASSERT_NE(nullptr, adjRibEntry_z);
      EXPECT_EQ(adjRibEntry_z->getPreIn()->getLocalPref(), A);
      /*
       * ...finally, signal second announcement is verified. Cleanup will occur
       * next.
       */
      syncBaton[3].post();

      /*
       * At this point, AdjRibIn contains:
       * pathID x -> path B
       * pathID z -> path A
       * AdjRibInStale contains:
       * pathID y -> path B
       */

      /*
       * Step 3: Wait for ribInWithdrawal of stale route. Verify it reaches
       * Rib...
       */
      auto ribInMsg5 =
          facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(ribInMsg5));
      auto withdrawal1 = std::get<RibInWithdrawal>(ribInMsg5);
      ASSERT_EQ(withdrawal1.pfxPathIds.size(), 1);
      EXPECT_EQ(get<0>(withdrawal1.pfxPathIds[0]), kV4Prefix1);
      EXPECT_EQ(get<1>(withdrawal1.pfxPathIds[0]), y);
      /*
       * ...and verify adjRibInStale empty and adjRibIn untouched with x->B and
       * z->A
       */
      EXPECT_EQ(0, adjRib_->getRibInStaleTreePaths());
      EXPECT_EQ(2, adjRib_->getRibTreePeerEntriesCount(true, true));
      adjRibEntry_x = adjRib_->getRibEntry(true, kV4Prefix1, x);
      ASSERT_NE(nullptr, adjRibEntry_x);
      EXPECT_EQ(adjRibEntry_x->getPreIn()->getLocalPref(), B);
      adjRibEntry_z = adjRib_->getRibEntry(true, kV4Prefix1, z);
      ASSERT_NE(nullptr, adjRibEntry_z);
      EXPECT_EQ(adjRibEntry_z->getPreIn()->getLocalPref(), A);

      terminateAdjRib();
    }
  });
  evb_.loop();
}

// Verify interaction of restart and stale-path timer.
TEST_F(AdjRibInboundFixture, VerifyGRRestartStalePathCleanup) {
  setupAdjRib(
      kShortGrRestartTime, // local
      k1SecGrRestartTime // remote
  );
  RibStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();

  std::vector<folly::CIDRNetwork> prefixSet1{kV6Prefix1, kV6Prefix2};
  folly::fibers::Baton syncBaton;

  fm_->addTask([&] {
    facebook::bgp::test::boundedBatonWait(syncBaton, "syncBaton");
    auto update = createV6BgpUpdateMultipleAnnounce(prefixSet1);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    /*
     * Verify that priviously announced paths are cleaned up as stale
     * after session restablishment and expiry of stale timer without
     * receipt of updates.
     */
    {
      syncBaton.post();
      // wait till RibInAnnouncement route message is sent
      facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPostInPrefixCount());
      // Terminate the session
      terminateAdjRib(true);
      // wait for sometime
      fiberSleepFor(50ms);
      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPostInPrefixCount());
      adjRib_->stalePathTime_ = std::chrono::seconds(1);
      // Restablish session
      reEstablishSession(std::chrono::seconds(1));

      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(prefixSet1.size(), adjRib_->getStats().getPostInPrefixCount());
      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix1));
      EXPECT_EQ(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV6Prefix2));
      EXPECT_EQ(
          false,
          adjRib_->adjRibInStale_
              .exactMatch(kV6Prefix1.first, kV6Prefix1.second)
              .atEnd());
      EXPECT_EQ(
          false,
          adjRib_->adjRibInStale_
              .exactMatch(kV6Prefix2.first, kV6Prefix2.second)
              .atEnd());
      // Stale ODS counter should reflect entries moved to stale tree
      tcData->publishStats();
      EXPECT_EQ(2, tcData->getCounter(RibStats::kAdjRibInStaleCount));
      // Let stale path time expire without any update.
      fiberSleepFor(1050ms);
      // Verify that stale routes are purged.
      EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());
      EXPECT_EQ(0, adjRib_->adjRibInStale_.size());
      // Stale ODS counter should be 0 after cleanup
      tcData->publishStats();
      EXPECT_EQ(0, tcData->getCounter(RibStats::kAdjRibInStaleCount));
      terminateAdjRib();
    }
  });
  evb_.loop();
}

// Ensure that EndOfRib is processed correctly
TEST_F(AdjRibInboundFixture, EndOfRibTest) {
  // create adjRib_ without call sessionEstablished
  setupAdjRib(
      kShortGrRestartTime, std::nullopt /* remoteGrRestartTime */, false);

  // when both ipv4 and ipv6 are negotiated
  {
    establishSession();
    fm_->addTask([&] {
      auto bgpEndOfRibV4 = buildEndOfRib(BgpUpdateAfi::AFI_IPv4);
      auto bgpEndOfRibV6 = buildEndOfRib(BgpUpdateAfi::AFI_IPv6);
      adjRibInQ_->fiberPush(std::move(bgpEndOfRibV4));
      adjRibInQ_->fiberPush(std::move(bgpEndOfRibV6));
    });

    fm_->addTask([&] {
      // Verify fromAdjRibQ_ message
      auto msg =
          facebook::bgp::test::boundedBlockingPop(fromAdjRibQ_, "fromAdjRibQ_");
      EXPECT_EQ(kPeerAddr1, msg.peerId.peerAddr);

      terminateAdjRib();
    });
    evb_.loop();
  }
  // when only ipv4 is negotiated
  {
    adjRibInQ_->open();
    fm_->addTask([&] {
      reEstablishSession(
          kShortGrRestartTime,
          AfiIpv4Negotiated(true),
          AfiIpv6Negotiated(false));
    });
    fm_->addTask([&] {
      auto bgpEndOfRibV4 = buildEndOfRib(BgpUpdateAfi::AFI_IPv4);
      adjRibInQ_->fiberPush(std::move(bgpEndOfRibV4));
    });

    fm_->addTask([&] {
      // Verify fromAdjRibQ_ message
      auto msg =
          facebook::bgp::test::boundedBlockingPop(fromAdjRibQ_, "fromAdjRibQ_");
      EXPECT_EQ(kPeerAddr1, msg.peerId.peerAddr);

      terminateAdjRib();
    });
    evb_.loop();
  }
  // when only ipv6 is negotiated
  {
    adjRibInQ_->open();
    fm_->addTask([&] {
      reEstablishSession(
          kShortGrRestartTime,
          AfiIpv4Negotiated(false),
          AfiIpv6Negotiated(true));
    });
    fm_->addTask([&] {
      auto bgpEndOfRibV6 = buildEndOfRib(BgpUpdateAfi::AFI_IPv6);
      adjRibInQ_->fiberPush(std::move(bgpEndOfRibV6));
    });

    fm_->addTask([&] {
      // Verify fromAdjRibQ_ message
      auto msg =
          facebook::bgp::test::boundedBlockingPop(fromAdjRibQ_, "fromAdjRibQ_");
      EXPECT_EQ(kPeerAddr1, msg.peerId.peerAddr);

      terminateAdjRib();
    });
    evb_.loop();
  }
}

/*
 * checks that PromoteStaleRibEntryIfExists is a no-op if there is no matching
 * stale entry for the given prefix/pathID
 */
TEST_F(AdjRibInboundFixture, PromoteStaleRibInEntryIfExistsTest_NoMatch) {
  setupAdjRib(kShortGrRestartTime, kShortGrRestartTime, false);
  EXPECT_EQ(adjRib_->adjRibInLiteTree_.size(), 0);
  EXPECT_EQ(adjRib_->adjRibInPathTree_.size(), 0);
  EXPECT_EQ(adjRib_->adjRibInStale_.size(), 0);

  // no stale paths for prefix at all. AdjRibIn structures still empty
  adjRib_->promoteStaleRibInEntryIfExists(kV4Prefix1, kDefaultPathID);
  EXPECT_EQ(adjRib_->adjRibInLiteTree_.size(), 0);
  EXPECT_EQ(adjRib_->adjRibInPathTree_.size(), 0);
  EXPECT_EQ(adjRib_->adjRibInStale_.size(), 0);

  // no stale path for given path ID. AdjRibIn structures still empty
  auto dummyPathId = 5;
  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>>
      adjRibStaleTreeEntry;
  adjRibStaleTreeEntry.emplace(
      dummyPathId, std::make_unique<AdjRibEntry>(dummyPathId));
  adjRib_->adjRibInStale_.insert(
      kV4Prefix1.first, kV4Prefix1.second, std::move(adjRibStaleTreeEntry));
  adjRib_->promoteStaleRibInEntryIfExists(kV4Prefix1, kDefaultPathID);
  EXPECT_EQ(adjRib_->adjRibInLiteTree_.size(), 0);
  EXPECT_EQ(adjRib_->adjRibInPathTree_.size(), 0);
  // stale entry for other pathID is still there, untouched
  EXPECT_EQ(adjRib_->adjRibInStale_.size(), 1);
  auto pfx1AdjRibStaleTreeEntry =
      adjRib_->adjRibInStale_.exactMatch(kV4Prefix1.first, kV4Prefix1.second);
  ASSERT_FALSE(pfx1AdjRibStaleTreeEntry.atEnd());
  EXPECT_TRUE(pfx1AdjRibStaleTreeEntry.value().contains(dummyPathId));
}

/*
 * checks that stale entry promotion properly removes stale entries from the
 * stale tree, whether they are the only entry for the corresponding prefix or
 * not
 */
TEST_F(
    AdjRibInboundFixture,
    PromoteStaleRibInEntryIfExistsTest_AddPathStaleRemoval) {
  setupAdjRib(kShortGrRestartTime, kShortGrRestartTime, false);
  adjRib_->recAddPath_ = true;
  EXPECT_EQ(adjRib_->adjRibInLiteTree_.size(), 0);
  EXPECT_EQ(adjRib_->adjRibInPathTree_.size(), 0);
  EXPECT_EQ(adjRib_->adjRibInStale_.size(), 0);
  RibStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();

  /*
   * set up stale tree with entries for two prefixes: one with only a single
   * stale path, and one with multiple stale paths, to verify correct removal of
   * individual paths from the stale tree on match
   */
  auto dummyPathId1 = 0;
  auto dummyPathId2 = 1;

  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>>
      pfx1AdjRibStaleTreeEntry;
  pfx1AdjRibStaleTreeEntry.emplace(
      dummyPathId1, std::make_unique<AdjRibEntry>(dummyPathId1));

  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>>
      pfx2AdjRibStaleTreeEntry;
  pfx2AdjRibStaleTreeEntry.emplace(
      dummyPathId1, std::make_unique<AdjRibEntry>(dummyPathId1));
  pfx2AdjRibStaleTreeEntry.emplace(
      dummyPathId2, std::make_unique<AdjRibEntry>(dummyPathId2));

  adjRib_->adjRibInStale_.insert(
      kV4Prefix1.first, kV4Prefix1.second, std::move(pfx1AdjRibStaleTreeEntry));
  adjRib_->adjRibInStale_.insert(
      kV4Prefix2.first, kV4Prefix2.second, std::move(pfx2AdjRibStaleTreeEntry));
  /*
   * 3 stale entries manually inserted (pfx1/pathId1, pfx2/pathId1,
   * pfx2/pathId2)
   */
  adjRib_->adjRibInStaleSize_ = 3;
  RibStats::incrAdjRibInStaleCount();
  RibStats::incrAdjRibInStaleCount();
  RibStats::incrAdjRibInStaleCount();
  tcData->publishStats();
  EXPECT_EQ(3, tcData->getCounter(RibStats::kAdjRibInStaleCount));

  // call for the prefix with only one stale path
  adjRib_->promoteStaleRibInEntryIfExists(kV4Prefix1, dummyPathId1);
  // path tree should now have kV4Prefix1 -> {dummyPathId1}
  EXPECT_EQ(adjRib_->adjRibInPathTree_.size(), 1);
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kAdjRibInCount));
  EXPECT_EQ(2, tcData->getCounter(RibStats::kAdjRibInStaleCount));
  auto pfx1Entry = adjRib_->adjRibInPathTree_.exactMatch(
      kV4Prefix1.first, kV4Prefix1.second);
  ASSERT_FALSE(pfx1Entry.atEnd());
  auto& pfx1PathEntries = pfx1Entry.value();
  EXPECT_EQ(pfx1PathEntries.size(), 1);
  auto pfx1PathEntry = pfx1PathEntries.find(dummyPathId1);
  ASSERT_NE(pfx1PathEntry, pfx1PathEntries.end());
  EXPECT_EQ(pfx1PathEntry->first, dummyPathId1);
  ASSERT_NE(pfx1PathEntry->second, nullptr);
  EXPECT_EQ(pfx1PathEntry->second->getPathId(), dummyPathId1);
  // stale tree should be empty, no pathID1 entry
  EXPECT_TRUE(
      adjRib_->adjRibInStale_.exactMatch(kV4Prefix1.first, kV4Prefix1.second)
          .atEnd());

  // call for the prefix with multiple stale paths
  adjRib_->promoteStaleRibInEntryIfExists(kV4Prefix2, dummyPathId1);
  /*
   * path tree should now have kV4Prefix2 -> {dummyPathId1} (and still
   * kV4Prefix1 -> {dummyPathId1})
   */
  EXPECT_EQ(adjRib_->adjRibInPathTree_.size(), 2);
  tcData->publishStats();
  EXPECT_EQ(2, tcData->getCounter(RibStats::kAdjRibInCount));
  // Stale counter decremented again (pfx2/pathId1 promoted)
  EXPECT_EQ(1, tcData->getCounter(RibStats::kAdjRibInStaleCount));
  auto pfx2Entry = adjRib_->adjRibInPathTree_.exactMatch(
      kV4Prefix2.first, kV4Prefix2.second);
  ASSERT_FALSE(pfx2Entry.atEnd());
  auto& pfx2PathEntries = pfx2Entry.value();
  EXPECT_EQ(pfx2PathEntries.size(), 1);
  auto pfx2PathEntry = pfx2PathEntries.find(dummyPathId1);
  ASSERT_NE(pfx2PathEntry, pfx2PathEntries.end());
  EXPECT_EQ(pfx2PathEntry->first, dummyPathId1);
  ASSERT_NE(pfx2PathEntry->second, nullptr);
  EXPECT_EQ(pfx2PathEntry->second->getPathId(), dummyPathId1);
  // stale tree should still have pathId2 entry
  auto pfx2AdjRibStaleTreeEntryFinal =
      adjRib_->adjRibInStale_.exactMatch(kV4Prefix2.first, kV4Prefix2.second);
  EXPECT_FALSE(pfx2AdjRibStaleTreeEntryFinal.atEnd());
  auto& pfx2StalePathEntries = pfx2AdjRibStaleTreeEntryFinal.value();
  EXPECT_EQ(pfx2StalePathEntries.size(), 1);
  ASSERT_TRUE(pfx2StalePathEntries.contains(dummyPathId2));
  EXPECT_EQ(
      pfx2StalePathEntries.at(dummyPathId2).get()->getPathId(), dummyPathId2);
}

/*
 * checks whether stale entry promotion works for pfx X pathID A when there are
 * already non-stale entries for X (so we need to move the promoted path into
 * X's entry) and/or A (we need to overwrite the non-stale path for A with the
 * promoted path)
 */
TEST_F(
    AdjRibInboundFixture,
    PromoteStaleRibInEntryIfExistsTest_AddPathUpdates) {
  setupAdjRib(kShortGrRestartTime, kShortGrRestartTime, false);
  adjRib_->recAddPath_ = true;
  EXPECT_EQ(adjRib_->adjRibInLiteTree_.size(), 0);
  EXPECT_EQ(adjRib_->adjRibInPathTree_.size(), 0);
  EXPECT_EQ(adjRib_->adjRibInStale_.size(), 0);

  /*
   * put one stale entry for kV4Prefix1 -> {dummyPathId1} and one stale entry
   * for kV4Prefix2 -> {dummyPathId1}. These will be promoted for testing.
   * explicitly set nexthop on the prefix2 path so that we can tell exactly
   * which entry ends up in the path tree later
   */
  auto dummyPathId1 = 0;

  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>>
      pfx1AdjRibStaleTreeEntry;
  pfx1AdjRibStaleTreeEntry.emplace(
      dummyPathId1, std::make_unique<AdjRibEntry>(dummyPathId1));

  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>>
      pfx2AdjRibStaleTreeEntry;
  auto stalePfx2Path1Entry = std::make_unique<AdjRibEntry>(dummyPathId1);
  auto stalePfx2Path1 = std::make_shared<BgpPath>();
  stalePfx2Path1->setNexthop(kV4Nexthop2);
  stalePfx2Path1Entry->setPreIn(stalePfx2Path1);
  pfx2AdjRibStaleTreeEntry.emplace(
      dummyPathId1, std::move(stalePfx2Path1Entry));

  adjRib_->adjRibInStale_.insert(
      kV4Prefix1.first, kV4Prefix1.second, std::move(pfx1AdjRibStaleTreeEntry));
  adjRib_->adjRibInStale_.insert(
      kV4Prefix2.first, kV4Prefix2.second, std::move(pfx2AdjRibStaleTreeEntry));

  /*
   * put a non-stale entry for kV4Prefix1 -> {dummyPathId2} (same prefix,
   * different path) and one non-stale entry for kV4Prefix2 -> {dummyPathId1}
   * (same prefix, same path)
   * for the non-stale entry with exact path match in stale tree, set nexthop
   * explicitly so that we can tell which entry ends up in the path tree later
   */
  auto dummyPathId2 = 1;

  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>>
      pfx1AdjRibTreeEntry;
  pfx1AdjRibTreeEntry.emplace(
      dummyPathId2, std::make_unique<AdjRibEntry>(dummyPathId2));

  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>>
      pfx2AdjRibTreeEntry;
  auto pfx2Path1Entry = std::make_unique<AdjRibEntry>(dummyPathId1);
  auto pfx2Path1 = std::make_shared<BgpPath>();
  pfx2Path1->setNexthop(kV4Nexthop1);
  pfx2Path1Entry->setPreIn(pfx2Path1);
  pfx2AdjRibTreeEntry.emplace(dummyPathId1, std::move(pfx2Path1Entry));

  adjRib_->adjRibInPathTree_.insert(
      kV4Prefix1.first, kV4Prefix1.second, std::move(pfx1AdjRibTreeEntry));
  adjRib_->adjRibInPathTree_.insert(
      kV4Prefix2.first, kV4Prefix2.second, std::move(pfx2AdjRibTreeEntry));
  RibStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();
  // 2 stale entries manually inserted (pfx1/pathId1, pfx2/pathId1)
  adjRib_->adjRibInStaleSize_ = 2;
  RibStats::incrAdjRibInStaleCount();
  RibStats::incrAdjRibInStaleCount();
  tcData->publishStats();
  EXPECT_EQ(2, tcData->getCounter(RibStats::kAdjRibInStaleCount));

  // promote the stale path w/ existing non-stale entry for same prefix
  adjRib_->promoteStaleRibInEntryIfExists(kV4Prefix1, dummyPathId1);
  // stale entry should be gone from stale tree
  EXPECT_TRUE(
      adjRib_->adjRibInStale_.exactMatch(kV4Prefix1.first, kV4Prefix1.second)
          .atEnd());
  // ODS counter should increment (new pathId for existing prefix)
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kAdjRibInCount));
  // Stale counter decremented (pfx1/pathId1 promoted out of stale)
  EXPECT_EQ(1, tcData->getCounter(RibStats::kAdjRibInStaleCount));
  // path tree should now have both pathID 1 & pathID 2 for this prefix
  auto pfx1Entry = adjRib_->adjRibInPathTree_.exactMatch(
      kV4Prefix1.first, kV4Prefix1.second);
  ASSERT_FALSE(pfx1Entry.atEnd());
  auto& pfx1PathEntries = pfx1Entry.value();
  ASSERT_TRUE(pfx1PathEntries.contains(dummyPathId1));
  EXPECT_EQ(pfx1PathEntries.at(dummyPathId1).get()->getPathId(), dummyPathId1);
  ASSERT_TRUE(pfx1PathEntries.contains(dummyPathId2));
  EXPECT_EQ(pfx1PathEntries.at(dummyPathId2).get()->getPathId(), dummyPathId2);

  /*
   * promote the stale path w/ existing non-stale entry for same exact path,
   * which would be considered an update
   */
  adjRib_->promoteStaleRibInEntryIfExists(kV4Prefix2, dummyPathId1);
  // stale entry should be gone from stale tree
  EXPECT_TRUE(
      adjRib_->adjRibInStale_.exactMatch(kV4Prefix2.first, kV4Prefix2.second)
          .atEnd());
  // ODS counter should NOT increment (overwrite of existing pathId)
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kAdjRibInCount));
  // Stale counter decremented to 0 (pfx2/pathId1 promoted out of stale)
  EXPECT_EQ(0, tcData->getCounter(RibStats::kAdjRibInStaleCount));
  /*
   * path tree should now have just the updated entry (so, kV4Nexthop2, not
   * kV4Nexthop1) for pathID 1
   */
  auto pfx2Entry = adjRib_->adjRibInPathTree_.exactMatch(
      kV4Prefix2.first, kV4Prefix2.second);
  ASSERT_FALSE(pfx2Entry.atEnd());
  auto& pfx2PathEntries = pfx2Entry.value();
  ASSERT_TRUE(pfx2PathEntries.contains(dummyPathId1));
  auto pfx2Path1EntryFinal = pfx2PathEntries.at(dummyPathId1).get();
  EXPECT_EQ(pfx2Path1EntryFinal->getPathId(), dummyPathId1);
  EXPECT_EQ(pfx2Path1EntryFinal->getPreIn()->getNexthop(), kV4Nexthop2);
}

/*
 * checks whether both new and existing paths from stale entry promotion work in
 * the non-ADD-PATH case
 */
TEST_F(AdjRibInboundFixture, PromoteStaleRibInEntryIfExistsTest_NonAddPath) {
  setupAdjRib(kShortGrRestartTime, kShortGrRestartTime, false);
  EXPECT_EQ(adjRib_->adjRibInLiteTree_.size(), 0);
  EXPECT_EQ(adjRib_->adjRibInPathTree_.size(), 0);
  EXPECT_EQ(adjRib_->adjRibInStale_.size(), 0);

  /*
   * put one stale entry for kV4Prefix1 -> {dummyPathId1} and one stale entry
   * for kV4Prefix2 -> {dummyPathId1}. One will have no corresponding non-stale
   * entry (new path case), and one will (update case). Explicitly set the
   * nexthop of the stale entry for prefix2 to distinguish it from the non-stale
   * one later
   */
  auto dummyPathId1 = 0;

  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>>
      pfx1AdjRibStaleTreeEntry;
  pfx1AdjRibStaleTreeEntry.emplace(
      dummyPathId1, std::make_unique<AdjRibEntry>(dummyPathId1));

  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>>
      pfx2AdjRibStaleTreeEntry;
  auto pfx2StaleEntry = std::make_unique<AdjRibEntry>(dummyPathId1);
  auto pfx2StalePath = std::make_shared<BgpPath>();
  pfx2StalePath->setNexthop(kV4Nexthop2);
  pfx2StaleEntry->setPreIn(pfx2StalePath);
  pfx2AdjRibStaleTreeEntry.emplace(dummyPathId1, std::move(pfx2StaleEntry));

  adjRib_->adjRibInStale_.insert(
      kV4Prefix1.first, kV4Prefix1.second, std::move(pfx1AdjRibStaleTreeEntry));
  adjRib_->adjRibInStale_.insert(
      kV4Prefix2.first, kV4Prefix2.second, std::move(pfx2AdjRibStaleTreeEntry));

  /*
   * put the corresponding non-stale entry for kV4Prefix2. Mark the nexthop to
   * distinguish it with the corresponding stale one
   */
  auto pfx2Entry = std::make_unique<AdjRibEntry>(dummyPathId1);
  auto pfx2Path = std::make_shared<BgpPath>();
  pfx2Path->setNexthop(kV4Nexthop1);
  pfx2Entry->setPreIn(pfx2Path);
  adjRib_->adjRibInLiteTree_.insert(
      kV4Prefix2.first, kV4Prefix2.second, std::move(pfx2Entry));
  RibStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();
  // 2 stale entries manually inserted (pfx1/pathId1, pfx2/pathId1)
  adjRib_->adjRibInStaleSize_ = 2;
  RibStats::incrAdjRibInStaleCount();
  RibStats::incrAdjRibInStaleCount();
  tcData->publishStats();
  EXPECT_EQ(2, tcData->getCounter(RibStats::kAdjRibInStaleCount));

  // promote the path with no corresponding non-stale entry
  adjRib_->promoteStaleRibInEntryIfExists(kV4Prefix1, dummyPathId1);
  EXPECT_TRUE(
      adjRib_->adjRibInStale_.exactMatch(kV4Prefix1.first, kV4Prefix1.second)
          .atEnd());
  // ODS counter should increment (new entry)
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kAdjRibInCount));
  // Stale counter decremented (pfx1/pathId1 promoted)
  EXPECT_EQ(1, tcData->getCounter(RibStats::kAdjRibInStaleCount));
  // Lite tree entry exists with pathID 1

  auto liteEntry = adjRib_->adjRibInLiteTree_.exactMatch(
      kV4Prefix1.first, kV4Prefix1.second);
  ASSERT_FALSE(liteEntry.atEnd());
  EXPECT_EQ(liteEntry.value()->getPathId(), dummyPathId1);

  // promote the path with corresponding stale entry
  adjRib_->promoteStaleRibInEntryIfExists(kV4Prefix2, dummyPathId1);
  EXPECT_TRUE(
      adjRib_->adjRibInStale_.exactMatch(kV4Prefix2.first, kV4Prefix2.second)
          .atEnd());
  // ODS counter should NOT increment (existing entry, stale discarded)
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kAdjRibInCount));
  // Stale counter decremented to 0 (pfx2/pathId1 promoted, stale discarded)
  EXPECT_EQ(0, tcData->getCounter(RibStats::kAdjRibInStaleCount));
  /*
   * Lite tree entry exists with pathID 1, and we see
   * the stale path was discarded and the extant path in liteTree was preserved
   * (w/ nexthop1)
   */
  liteEntry = adjRib_->adjRibInLiteTree_.exactMatch(
      kV4Prefix2.first, kV4Prefix2.second);
  ASSERT_FALSE(liteEntry.atEnd());
  EXPECT_EQ(liteEntry.value()->getPathId(), dummyPathId1);
  EXPECT_EQ(liteEntry.value()->getPreIn()->getNexthop(), kV4Nexthop1);
}

/*
 * Tests for promoteStaleRibInEntryIfExistsInPlace (optimized GR)
 * Verifies that when there's no stale entry for the given prefix/pathID,
 * the method is a no-op
 */
TEST_F(
    AdjRibInboundFixture,
    PromoteStaleRibInEntryIfExistsInPlaceTest_NoMatch) {
  setupAdjRib(kShortGrRestartTime, kShortGrRestartTime, false);
  adjRib_->enableOptimizedGR_ = true;
  EXPECT_EQ(adjRib_->adjRibInLiteTree_.size(), 0);
  EXPECT_EQ(adjRib_->adjRibInPathTree_.size(), 0);
  EXPECT_EQ(adjRib_->staleEntryCount_, 0);

  // no entries at all - should be a no-op
  adjRib_->promoteStaleRibInEntryIfExistsInPlace(kV4Prefix1, kDefaultPathID);
  EXPECT_EQ(adjRib_->adjRibInLiteTree_.size(), 0);
  EXPECT_EQ(adjRib_->adjRibInPathTree_.size(), 0);
  EXPECT_EQ(adjRib_->staleEntryCount_, 0);

  // entry exists but not marked stale - should be a no-op
  auto entry = std::make_unique<AdjRibEntry>(kDefaultPathID);
  entry->setPreIn(std::make_shared<BgpPath>());
  EXPECT_FALSE(entry->isStale());
  adjRib_->adjRibInLiteTree_.insert(
      kV4Prefix1.first, kV4Prefix1.second, std::move(entry));

  adjRib_->promoteStaleRibInEntryIfExistsInPlace(kV4Prefix1, kDefaultPathID);
  EXPECT_EQ(adjRib_->adjRibInLiteTree_.size(), 1);
  EXPECT_EQ(adjRib_->staleEntryCount_, 0);

  // verify the entry is still not stale
  auto liteEntry = adjRib_->adjRibInLiteTree_.exactMatch(
      kV4Prefix1.first, kV4Prefix1.second);
  ASSERT_FALSE(liteEntry.atEnd());
  EXPECT_FALSE(liteEntry.value()->isStale());
}

/*
 * Verifies that promoteStaleRibInEntryIfExistsInPlace clears stale bit and
 * decrements counter in ADD-PATH case
 */
TEST_F(
    AdjRibInboundFixture,
    PromoteStaleRibInEntryIfExistsInPlaceTest_AddPathClears) {
  setupAdjRib(kShortGrRestartTime, kShortGrRestartTime, false);
  adjRib_->enableOptimizedGR_ = true;
  adjRib_->recAddPath_ = true;
  EXPECT_EQ(adjRib_->adjRibInPathTree_.size(), 0);
  EXPECT_EQ(adjRib_->staleEntryCount_, 0);

  // create entries with some marked stale
  auto pathId1 = 1;
  auto pathId2 = 2;

  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>> pfx1Entries;
  auto entry1 = std::make_unique<AdjRibEntry>(pathId1);
  entry1->setPreIn(std::make_shared<BgpPath>());
  entry1->setStale(true);
  auto entry2 = std::make_unique<AdjRibEntry>(pathId2);
  entry2->setPreIn(std::make_shared<BgpPath>());
  entry2->setStale(true);
  pfx1Entries.emplace(pathId1, std::move(entry1));
  pfx1Entries.emplace(pathId2, std::move(entry2));
  adjRib_->adjRibInPathTree_.insert(
      kV4Prefix1.first, kV4Prefix1.second, std::move(pfx1Entries));
  adjRib_->staleEntryCount_ = 2;

  // promote first stale entry
  adjRib_->promoteStaleRibInEntryIfExistsInPlace(kV4Prefix1, pathId1);
  EXPECT_EQ(adjRib_->staleEntryCount_, 1);

  auto pfx1Match = adjRib_->adjRibInPathTree_.exactMatch(
      kV4Prefix1.first, kV4Prefix1.second);
  ASSERT_FALSE(pfx1Match.atEnd());
  auto& pathEntries = pfx1Match.value();
  EXPECT_EQ(pathEntries.size(), 2);

  // pathId1 should no longer be stale
  auto path1Entry = pathEntries.find(pathId1);
  ASSERT_NE(path1Entry, pathEntries.end());
  EXPECT_FALSE(path1Entry->second->isStale());

  // pathId2 should still be stale
  auto path2Entry = pathEntries.find(pathId2);
  ASSERT_NE(path2Entry, pathEntries.end());
  EXPECT_TRUE(path2Entry->second->isStale());

  // promote second stale entry
  adjRib_->promoteStaleRibInEntryIfExistsInPlace(kV4Prefix1, pathId2);
  EXPECT_EQ(adjRib_->staleEntryCount_, 0);

  // both entries should now be non-stale
  pfx1Match = adjRib_->adjRibInPathTree_.exactMatch(
      kV4Prefix1.first, kV4Prefix1.second);
  ASSERT_FALSE(pfx1Match.atEnd());
  auto& pathEntriesFinal = pfx1Match.value();
  path1Entry = pathEntriesFinal.find(pathId1);
  ASSERT_NE(path1Entry, pathEntriesFinal.end());
  EXPECT_FALSE(path1Entry->second->isStale());
  path2Entry = pathEntriesFinal.find(pathId2);
  ASSERT_NE(path2Entry, pathEntriesFinal.end());
  EXPECT_FALSE(path2Entry->second->isStale());
}

/*
 * Verifies that promoteStaleRibInEntryIfExistsInPlace clears stale bit and
 * decrements counter in non-ADD-PATH case
 */
TEST_F(
    AdjRibInboundFixture,
    PromoteStaleRibInEntryIfExistsInPlaceTest_NonAddPath) {
  setupAdjRib(kShortGrRestartTime, kShortGrRestartTime, false);
  adjRib_->enableOptimizedGR_ = true;
  adjRib_->recAddPath_ = false;
  EXPECT_EQ(adjRib_->adjRibInLiteTree_.size(), 0);
  EXPECT_EQ(adjRib_->staleEntryCount_, 0);

  // create a stale entry in liteTree
  auto entry = std::make_unique<AdjRibEntry>(kDefaultPathID);
  entry->setPreIn(std::make_shared<BgpPath>());
  entry->setStale(true);
  adjRib_->adjRibInLiteTree_.insert(
      kV4Prefix1.first, kV4Prefix1.second, std::move(entry));
  adjRib_->staleEntryCount_ = 1;

  // promote the stale entry
  adjRib_->promoteStaleRibInEntryIfExistsInPlace(kV4Prefix1, kDefaultPathID);
  EXPECT_EQ(adjRib_->staleEntryCount_, 0);

  // verify entry still exists but is no longer stale
  auto liteEntry = adjRib_->adjRibInLiteTree_.exactMatch(
      kV4Prefix1.first, kV4Prefix1.second);
  ASSERT_FALSE(liteEntry.atEnd());
  EXPECT_FALSE(liteEntry.value()->isStale());
}

/*
 * Tests for markLearntRoutesStaleInPlace
 * Verifies marking routes stale in ADD-PATH case
 */
TEST_F(AdjRibInboundFixture, MarkLearntRoutesStaleInPlaceTest) {
  setupAdjRib(kShortGrRestartTime, kShortGrRestartTime, false);
  adjRib_->enableOptimizedGR_ = true;
  adjRib_->recAddPath_ = true;
  EXPECT_EQ(adjRib_->adjRibInPathTree_.size(), 0);
  EXPECT_EQ(adjRib_->staleEntryCount_, 0);

  // create multiple entries with preIn set
  auto pathId1 = 1;
  auto pathId2 = 2;

  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>> pfx1Entries;
  auto entry1 = std::make_unique<AdjRibEntry>(pathId1);
  entry1->setPreIn(std::make_shared<BgpPath>());
  auto entry2 = std::make_unique<AdjRibEntry>(pathId2);
  entry2->setPreIn(std::make_shared<BgpPath>());
  pfx1Entries.emplace(pathId1, std::move(entry1));
  pfx1Entries.emplace(pathId2, std::move(entry2));
  adjRib_->adjRibInPathTree_.insert(
      kV4Prefix1.first, kV4Prefix1.second, std::move(pfx1Entries));

  // create an entry for a second prefix
  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>> pfx2Entries;
  auto entry3 = std::make_unique<AdjRibEntry>(pathId1);
  entry3->setPreIn(std::make_shared<BgpPath>());
  pfx2Entries.emplace(pathId1, std::move(entry3));
  adjRib_->adjRibInPathTree_.insert(
      kV4Prefix2.first, kV4Prefix2.second, std::move(pfx2Entries));

  // create an entry without preIn - should not be marked stale
  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>> pfx3Entries;
  auto entry4 = std::make_unique<AdjRibEntry>(pathId1);
  // no setPreIn call
  pfx3Entries.emplace(pathId1, std::move(entry4));
  adjRib_->adjRibInPathTree_.insert(
      kV4Prefix3.first, kV4Prefix3.second, std::move(pfx3Entries));

  // mark all routes stale
  adjRib_->markLearntRoutesStaleInPlace();

  // verify staleEntryCount_ is 3 (entry1, entry2, entry3 have preIn)
  EXPECT_EQ(adjRib_->staleEntryCount_, 3);

  // verify entries with preIn are marked stale
  auto pfx1Match = adjRib_->adjRibInPathTree_.exactMatch(
      kV4Prefix1.first, kV4Prefix1.second);
  ASSERT_FALSE(pfx1Match.atEnd());
  for (const auto& [pathId, adjRibEntry] : pfx1Match.value()) {
    EXPECT_TRUE(adjRibEntry->isStale());
  }

  auto pfx2Match = adjRib_->adjRibInPathTree_.exactMatch(
      kV4Prefix2.first, kV4Prefix2.second);
  ASSERT_FALSE(pfx2Match.atEnd());
  EXPECT_TRUE(pfx2Match.value().at(pathId1)->isStale());

  // verify entry without preIn is not marked stale
  auto pfx3Match = adjRib_->adjRibInPathTree_.exactMatch(
      kV4Prefix3.first, kV4Prefix3.second);
  ASSERT_FALSE(pfx3Match.atEnd());
  EXPECT_FALSE(pfx3Match.value().at(pathId1)->isStale());
}

// Verifies marking routes stale in non-ADD-PATH case
TEST_F(AdjRibInboundFixture, MarkLearntRoutesStaleInPlaceTest_NonAddPath) {
  setupAdjRib(kShortGrRestartTime, kShortGrRestartTime, false);
  adjRib_->enableOptimizedGR_ = true;
  adjRib_->recAddPath_ = false;
  EXPECT_EQ(adjRib_->adjRibInLiteTree_.size(), 0);
  EXPECT_EQ(adjRib_->staleEntryCount_, 0);

  // create entries with preIn set
  auto entry1 = std::make_unique<AdjRibEntry>(kDefaultPathID);
  entry1->setPreIn(std::make_shared<BgpPath>());
  adjRib_->adjRibInLiteTree_.insert(
      kV4Prefix1.first, kV4Prefix1.second, std::move(entry1));

  auto entry2 = std::make_unique<AdjRibEntry>(kDefaultPathID);
  entry2->setPreIn(std::make_shared<BgpPath>());
  adjRib_->adjRibInLiteTree_.insert(
      kV4Prefix2.first, kV4Prefix2.second, std::move(entry2));

  // create an entry without preIn - should not be marked stale
  auto entry3 = std::make_unique<AdjRibEntry>(kDefaultPathID);
  // no setPreIn call
  adjRib_->adjRibInLiteTree_.insert(
      kV4Prefix3.first, kV4Prefix3.second, std::move(entry3));

  // mark all routes stale
  adjRib_->markLearntRoutesStaleInPlace();

  // verify staleEntryCount_ is 2 (entry1 and entry2 have preIn)
  EXPECT_EQ(adjRib_->staleEntryCount_, 2);

  // verify entries with preIn are marked stale
  auto liteEntry1 = adjRib_->adjRibInLiteTree_.exactMatch(
      kV4Prefix1.first, kV4Prefix1.second);
  ASSERT_FALSE(liteEntry1.atEnd());
  EXPECT_TRUE(liteEntry1.value()->isStale());

  auto liteEntry2 = adjRib_->adjRibInLiteTree_.exactMatch(
      kV4Prefix2.first, kV4Prefix2.second);
  ASSERT_FALSE(liteEntry2.atEnd());
  EXPECT_TRUE(liteEntry2.value()->isStale());

  // verify entry without preIn is not marked stale
  auto liteEntry3 = adjRib_->adjRibInLiteTree_.exactMatch(
      kV4Prefix3.first, kV4Prefix3.second);
  ASSERT_FALSE(liteEntry3.atEnd());
  EXPECT_FALSE(liteEntry3.value()->isStale());
}

/*
 * Tests for cleanupStaleRoutesInPlace (optimized GR)
 * Verifies early exit when no stale entries exist
 */
TEST_F(AdjRibInboundFixture, CleanupStaleRoutesInPlaceTest_NoStaleEntries) {
  setupAdjRib(kShortGrRestartTime, kShortGrRestartTime, false);
  adjRib_->enableOptimizedGR_ = true;
  adjRib_->recAddPath_ = false;
  adjRib_->staleEntryCount_ = 0;
  EXPECT_EQ(adjRib_->adjRibInLiteTree_.size(), 0);

  // Add a non-stale entry
  auto entry = std::make_unique<AdjRibEntry>(kDefaultPathID);
  entry->setPreIn(std::make_shared<BgpPath>());
  entry->setStale(false);
  adjRib_->adjRibInLiteTree_.insert(
      kV4Prefix1.first, kV4Prefix1.second, std::move(entry));

  // cleanup should be a no-op when staleEntryCount_ is 0
  folly::coro::blockingWait(adjRib_->cleanupStaleRoutesInPlace(false));

  // Entry should still exist
  EXPECT_EQ(adjRib_->adjRibInLiteTree_.size(), 1);
  auto liteEntry = adjRib_->adjRibInLiteTree_.exactMatch(
      kV4Prefix1.first, kV4Prefix1.second);
  ASSERT_FALSE(liteEntry.atEnd());
  EXPECT_FALSE(liteEntry.value()->isStale());
  EXPECT_EQ(adjRib_->staleEntryCount_, 0);
}

// Verifies cleanup of stale entries in ADD-PATH case
TEST_F(AdjRibInboundFixture, CleanupStaleRoutesInPlaceTest_AddPath) {
  setupAdjRib(kShortGrRestartTime, kShortGrRestartTime, false);
  adjRib_->remoteAs_ = kAsSeqAsNum;
  adjRib_->enableOptimizedGR_ = true;
  adjRib_->recAddPath_ = true;
  EXPECT_EQ(adjRib_->adjRibInPathTree_.size(), 0);
  EXPECT_EQ(adjRib_->staleEntryCount_, 0);

  auto pathId1 = 1;
  auto pathId2 = 2;

  // create entries - some stale, some not stale
  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>> pfx1Entries;
  auto entry1 = std::make_unique<AdjRibEntry>(pathId1);
  entry1->setPreIn(std::make_shared<BgpPath>());
  entry1->setStale(true); // stale
  auto entry2 = std::make_unique<AdjRibEntry>(pathId2);
  entry2->setPreIn(std::make_shared<BgpPath>());
  entry2->setStale(false); // not stale
  pfx1Entries.emplace(pathId1, std::move(entry1));
  pfx1Entries.emplace(pathId2, std::move(entry2));
  adjRib_->adjRibInPathTree_.insert(
      kV4Prefix1.first, kV4Prefix1.second, std::move(pfx1Entries));

  // create another stale entry for a different prefix
  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>> pfx2Entries;
  auto entry3 = std::make_unique<AdjRibEntry>(pathId1);
  entry3->setPreIn(std::make_shared<BgpPath>());
  entry3->setStale(true); // stale
  pfx2Entries.emplace(pathId1, std::move(entry3));
  adjRib_->adjRibInPathTree_.insert(
      kV4Prefix2.first, kV4Prefix2.second, std::move(pfx2Entries));

  adjRib_->staleEntryCount_ = 2; // 2 stale entries (entry1 and entry3)

  // cleanup stale entries
  folly::coro::blockingWait(adjRib_->cleanupStaleRoutesInPlace(false));

  auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
  ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(msg));
  EXPECT_EQ(kAsSeqAsNum, std::get<RibInWithdrawal>(msg).peer.asn);

  // verify staleEntryCount_ is reset to 0
  EXPECT_EQ(adjRib_->staleEntryCount_, 0);

  // verify stale entry was cleaned up from prefix1
  auto pfx1Match = adjRib_->adjRibInPathTree_.exactMatch(
      kV4Prefix1.first, kV4Prefix1.second);
  ASSERT_FALSE(pfx1Match.atEnd());
  // Only non-stale entry2 should remain
  EXPECT_EQ(pfx1Match.value().size(), 1);
  auto path2Entry = pfx1Match.value().find(pathId2);
  ASSERT_NE(path2Entry, pfx1Match.value().end());
  EXPECT_FALSE(path2Entry->second->isStale());

  // verify stale entry was cleaned up from prefix2
  auto pfx2Match = adjRib_->adjRibInPathTree_.exactMatch(
      kV4Prefix2.first, kV4Prefix2.second);
  // The entire prefix should be removed since its only entry was stale
  EXPECT_TRUE(pfx2Match.atEnd());
}

// Verifies cleanup of stale entries in non-ADD-PATH case
TEST_F(AdjRibInboundFixture, CleanupStaleRoutesInPlaceTest_NonAddPathCase) {
  setupAdjRib(kShortGrRestartTime, kShortGrRestartTime, false);
  adjRib_->enableOptimizedGR_ = true;
  adjRib_->recAddPath_ = false;
  EXPECT_EQ(adjRib_->adjRibInLiteTree_.size(), 0);
  EXPECT_EQ(adjRib_->staleEntryCount_, 0);

  // create stale entry
  auto entry1 = std::make_unique<AdjRibEntry>(kDefaultPathID);
  entry1->setPreIn(std::make_shared<BgpPath>());
  entry1->setStale(true);
  adjRib_->adjRibInLiteTree_.insert(
      kV4Prefix1.first, kV4Prefix1.second, std::move(entry1));

  // create non-stale entry
  auto entry2 = std::make_unique<AdjRibEntry>(kDefaultPathID);
  entry2->setPreIn(std::make_shared<BgpPath>());
  entry2->setStale(false);
  adjRib_->adjRibInLiteTree_.insert(
      kV4Prefix2.first, kV4Prefix2.second, std::move(entry2));

  // create another stale entry
  auto entry3 = std::make_unique<AdjRibEntry>(kDefaultPathID);
  entry3->setPreIn(std::make_shared<BgpPath>());
  entry3->setStale(true);
  adjRib_->adjRibInLiteTree_.insert(
      kV4Prefix3.first, kV4Prefix3.second, std::move(entry3));

  adjRib_->staleEntryCount_ = 2; // 2 stale entries (entry1 and entry3)

  // cleanup stale entries
  folly::coro::blockingWait(adjRib_->cleanupStaleRoutesInPlace(false));

  // verify staleEntryCount_ is reset to 0
  EXPECT_EQ(adjRib_->staleEntryCount_, 0);

  // verify stale entry1 was cleaned up
  auto liteEntry1 = adjRib_->adjRibInLiteTree_.exactMatch(
      kV4Prefix1.first, kV4Prefix1.second);
  EXPECT_TRUE(liteEntry1.atEnd());

  // verify non-stale entry2 still exists
  auto liteEntry2 = adjRib_->adjRibInLiteTree_.exactMatch(
      kV4Prefix2.first, kV4Prefix2.second);
  ASSERT_FALSE(liteEntry2.atEnd());
  EXPECT_FALSE(liteEntry2.value()->isStale());

  // verify stale entry3 was cleaned up
  auto liteEntry3 = adjRib_->adjRibInLiteTree_.exactMatch(
      kV4Prefix3.first, kV4Prefix3.second);
  EXPECT_TRUE(liteEntry3.atEnd());

  // Only 1 entry should remain (the non-stale one)
  EXPECT_EQ(adjRib_->adjRibInLiteTree_.size(), 1);
}

/*
 * Reproduce P1: cleanupStaleRoutes decrements postInPrefixCount without
 * checking if the entry has a postAttr. If a stale route was blocked by
 * inbound policy (has preIn but no postAttr), the counter underflows.
 *
 * The correct pattern (used in cleanupStaleRoutesInPlace) guards the
 * decrement with if (adjRibEntry->getPostAttr()).
 */
TEST_F(AdjRibInboundFixture, CleanupStaleRoutes_NoPostAttrCounterUnderflow) {
  setupAdjRib(kShortGrRestartTime, kShortGrRestartTime, false);
  adjRib_->enableOptimizedGR_ = false;
  RibStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();

  EXPECT_EQ(adjRib_->stats_.getPostInPrefixCount(), 0);

  /*
   * Create a stale entry with preIn but NO postAttr (policy-rejected route).
   * This goes into adjRibInStale_ which cleanupStaleRoutes iterates.
   */
  folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>> entries;
  auto entry = std::make_unique<AdjRibEntry>(kDefaultPathID);
  entry->setPreIn(std::make_shared<BgpPath>());
  entries.emplace(kDefaultPathID, std::move(entry));

  adjRib_->adjRibInStale_.insert(
      kV4Prefix1.first, kV4Prefix1.second, std::move(entries));
  // 1 stale entry manually inserted
  adjRib_->adjRibInStaleSize_ = 1;
  RibStats::incrAdjRibInStaleCount();
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kAdjRibInStaleCount));

  adjRib_->incrementPreInPrefixCount(kV4Prefix1, false, false);

  folly::coro::blockingWait(adjRib_->cleanupStaleRoutes(false));

  EXPECT_EQ(adjRib_->stats_.getPostInPrefixCount(), 0)
      << "postInPrefixCount should not change for entries without postAttr";
  // Stale ODS counter should be 0 after cleanup
  tcData->publishStats();
  EXPECT_EQ(0, tcData->getCounter(RibStats::kAdjRibInStaleCount));
}

/******************************************************************************
 *      END   -   Graceful restart related tests.                             *
 ******************************************************************************/

/******************************************************************************
 *      START   -   Update processing correctness tests.                      *
 ******************************************************************************/

/*
 * Verify that a policy with both V4 and V6 terms is processed properly when
 * a BGPUpdate2 with both V4 and V6 prefixes is received.
 * Send a BGPUpdate2 with 2 V4 and 2 V6 prefixes.
 * Verify a V6 prefix is accepted. (kV6Prefix1)
 * Verify a V4 prefix is accepted. (kV4Prefix1)
 * Verify a V6 prefix is denied. (kV6Prefix2)
 * Verify a V4 prefix is denied. (kV4Prefix2)
 */
TEST_F(AdjRibInboundFixture, V4AndV6Policy) {
  /*
   * Create a policy with two terms
   * Term1 match kV4Prefix1 and apply origin action (EGP)
   * Term2 match kV6Prefix1 and apply origin action (INCOMPLETE)
   * Prefixes not matched by any term will be denied by default
   */

  // Creating TERM1 (match kV4Prefix1 and apply origin action (EGP))
  routing_policy::CompareNumericValue compareStructEQ;
  *compareStructEQ.compare_operator() = routing_policy::ComparisonOperator::EQ;
  *compareStructEQ.value() = kV4Prefix1.second;
  const auto& prefixListEntry1 = createPrefixListEntry(
      IPAddress::networkToString(kV4Prefix1), {compareStructEQ});

  const auto& match1 = createPrefixListMatch({prefixListEntry1});
  auto actionEgp = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::EGP);
  auto term1 = createBgpPolicyTerm("Term1", "", {match1}, {actionEgp});

  // Creating TERM2 (match kV6Prefix1 and apply origin action (INCOMPLETE))
  *compareStructEQ.compare_operator() = routing_policy::ComparisonOperator::EQ;
  *compareStructEQ.value() = kV6Prefix1.second;
  const auto& prefixListEntry2 = createPrefixListEntry(
      IPAddress::networkToString(kV6Prefix1), {compareStructEQ});
  const auto& match2 = createPrefixListMatch({prefixListEntry2});
  auto actionIncomplete = createBgpPolicyAction(
      BgpPolicyActionType::ORIGIN, {}, "", bgp_policy::Origin::INCOMPLETE);
  auto term2 = createBgpPolicyTerm("Term2", "", {match2}, {actionIncomplete});

  // Create policy
  const std::string policyName = kIngressPolicyName;
  const auto& policyConfig = createBgpPolicies(policyName, {term1, term2});
  auto policyManager = std::make_shared<PolicyManager>(
      policyConfig, createTestBgpGlobalConfig());

  setupAdjRib(policyManager, policyName);

  const std::vector<folly::CIDRNetwork> inputPrefixSet{
      kV4Prefix1, kV6Prefix1, kV4Prefix2, kV6Prefix2};

  fm_->addTask([&] {
    auto update = createV4AndV6BgpUpdateMultipleAnnounce(inputPrefixSet);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    /*
     * Verify ribIn messages (2 messages)
     * V4 and V6 policies are applied separately.
     * NOTE: Here order of messages is hardcoded.
     *       If we change order of processing we need to modify testcase.
     */
    auto msg1 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg1));
    const auto announcement1 = std::get<RibInAnnouncement>(msg1);
    EXPECT_EQ(kPeerAddr1, announcement1.peer.addr);
    const PrefixPathIds expectedPrefixSet1{{kV4Prefix1, kDefaultPathID}};
    EXPECT_EQ(expectedPrefixSet1, announcement1.pfxPathIds);

    auto msg2 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg2));
    const auto announcement2 = std::get<RibInAnnouncement>(msg2);
    EXPECT_EQ(kPeerAddr1, announcement2.peer.addr);
    const PrefixPathIds expectedPrefixSet2{{kV6Prefix1, kDefaultPathID}};
    EXPECT_EQ(expectedPrefixSet2, announcement2.pfxPathIds);

    fiberSleepFor(20ms);
    EXPECT_TRUE(ribInQ_.empty());

    // Verify permitted prefixes
    const std::vector<folly::CIDRNetwork> permittedPrefixSet{
        kV4Prefix1, kV6Prefix1};
    for (const auto& prefix : permittedPrefixSet) {
      auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, prefix);
      ASSERT_NE(nullptr, adjRibEntry);
      ASSERT_NE(nullptr, adjRibEntry->getPreIn());
      ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
      EXPECT_EQ(
          BgpAttrOrigin::BGP_ORIGIN_IGP, adjRibEntry->getPreIn()->getOrigin());

      if (prefix == kV4Prefix1) {
        EXPECT_EQ(kV4Nexthop1, adjRibEntry->getPreIn()->getNexthop());
        EXPECT_EQ(kV4Nexthop1, adjRibEntry->getPostAttr()->getNexthop());
        EXPECT_EQ(
            BgpAttrOrigin::BGP_ORIGIN_EGP,
            adjRibEntry->getPostAttr()->getOrigin());
        EXPECT_EQ(announcement1.attrs, adjRibEntry->getPostAttr());
      } else { // kV6Prefix1
        EXPECT_EQ(kV6Nexthop1, adjRibEntry->getPreIn()->getNexthop());
        EXPECT_EQ(kV6Nexthop1, adjRibEntry->getPostAttr()->getNexthop());
        EXPECT_EQ(
            BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE,
            adjRibEntry->getPostAttr()->getOrigin());
        EXPECT_EQ(announcement2.attrs, adjRibEntry->getPostAttr());
      }
    }

    // Verify denied prefixes
    const std::vector<folly::CIDRNetwork> deniedPrefixSet{
        kV4Prefix2, kV6Prefix2};
    for (const auto& prefix : deniedPrefixSet) {
      auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, prefix);
      ASSERT_NE(nullptr, adjRibEntry);
      ASSERT_NE(nullptr, adjRibEntry->getPreIn());
      EXPECT_EQ(nullptr, adjRibEntry->getPostAttr());
      EXPECT_EQ(
          BgpAttrOrigin::BGP_ORIGIN_IGP, adjRibEntry->getPreIn()->getOrigin());
    }

    terminateAdjRib();
  });

  evb_.loop();
}

TEST_F(AdjRibInboundFixture, VerifyUpdateAttributesIn) {
  /*
   * IBGP peer
   * Verify preIn attributes are same as in the update
   * i.e. local preference, originator id, cluster list are unmodified
   */
  {
    setupAdjRib(
        kShortGrRestartTime,
        std::nullopt, // remoteGrRestartTime
        false, // callSessionEstablished
        kLocalAs1,
        kLocalAs1,
        kRemoteAs1);

    auto inputUpdate = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
    // Overwrite default values to trigger possible cases
    inputUpdate->attrs()->localPref() = kLocalPref2;
    // originator id and cluster list should be in network byte order
    *inputUpdate->attrs()->originatorId() = kPeerAddr1.asV4().toLong();
    inputUpdate->attrs()->clusterList()->clear();
    inputUpdate->attrs()->clusterList()->push_back(kPeerAddr1.asV4().toLong());

    auto inputAttrs = std::make_shared<facebook::bgp::BgpPath>(
        BgpPathFields(*BgpUpdate2toBgpPathC(*inputUpdate)));
    inputAttrs->publish();

    // Deep compare of BgpPath
    auto outputAttrs = adjRib_->updateAttributesIn(inputAttrs);
    EXPECT_EQ(*inputAttrs, *outputAttrs);
  }
  /*
   * Confed EBGP peer
   * Verify preIn is same as in the update when local preference is presented
   * i.e. local preference, originator id, cluster list are unmodified
   */
  {
    setupAdjRib(
        kShortGrRestartTime,
        std::nullopt, // remoteGrRestartTime
        false, // do not establish
        kLocalAs1, // global as
        kLocalAs1, // local as
        kRemoteAs2,
        AfiIpv4Negotiated(true),
        AfiIpv6Negotiated(true),
        nullptr, // policy manager, not used
        std::nullopt, // ingress policy name, not used
        kIsConfedPeerTrue,
        static_cast<uint32_t>(kLocalAs1), // local confed as
        static_cast<uint32_t>(kLocalAs2)); // as confed id

    /*
     * Case 1: Verify localPref is preserved (not set to default)
     * Case 2: Verify originator id and cluster list are retained
     */
    auto inputUpdate = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
    // Overwrite default value in update to non-default local preferencee
    inputUpdate->attrs()->localPref() = kLocalPref2;

    auto inputAttrs = std::make_shared<facebook::bgp::BgpPath>(
        BgpPathFields(*BgpUpdate2toBgpPathC(*inputUpdate)));
    inputAttrs->publish();

    ASSERT_EQ(kOriginatorId, inputAttrs->getOriginatorId());
    EXPECT_THAT(inputAttrs->getClusterList().get(), ElementsAre(kOriginatorId));

    auto outputAttrs = adjRib_->updateAttributesIn(inputAttrs);
    // Deep compare of BgpPath
    EXPECT_EQ(*inputAttrs, *outputAttrs);
  }
  /*
   * Confed EBGP peer
   * Verify preIn is updated to default when local preference is not presented
   */
  {
    setupAdjRib(
        kShortGrRestartTime,
        std::nullopt, // remoteGrRestartTime
        false, // do not establish
        kLocalAs1, // global as
        kLocalAs1, // local as
        kRemoteAs2,
        AfiIpv4Negotiated(true),
        AfiIpv6Negotiated(true),
        nullptr, // policy manager, not used
        std::nullopt, // ingress policy name, not used
        kIsConfedPeerTrue,
        static_cast<uint32_t>(kLocalAs1), // local confed as
        static_cast<uint32_t>(kLocalAs2)); // as confed id

    auto inputUpdate = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
    inputUpdate->attrs()->localPref().reset();

    auto inputAttrs = std::make_shared<facebook::bgp::BgpPath>(
        BgpPathFields(*BgpUpdate2toBgpPathC(*inputUpdate)));
    inputAttrs->publish();
    EXPECT_EQ(std::nullopt, inputAttrs->getLocalPref());

    auto outputAttrs = adjRib_->updateAttributesIn(inputAttrs);
    EXPECT_EQ(facebook::bgp::kDefaultLocalPref, outputAttrs->getLocalPref());
  }
  /*
   * Verify that for Ebgp peer, preIn attributes are updated
   * Local preference is set to default.
   * Orginator id and cluster list stripped.
   */
  {
    setupAdjRib(
        kShortGrRestartTime,
        std::nullopt, // remoteGrRestartTime
        false, // callSessionEstablished
        kLocalAs1,
        kLocalAs1,
        kRemoteAs2);

    auto inputUpdate = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
    // Case 1: Verify localPref is set to default (100)
    inputUpdate->attrs()->localPref().reset();
    /*
     * Case 2: Verify originatorId and clusterList are stripped
     * originator id and cluster list shoule be in network byte order in
     * BgpUpdate2
     */
    const auto originatorId = kPeerAddr1.asV4().toLong();
    *inputUpdate->attrs()->originatorId() = originatorId;
    inputUpdate->attrs()->clusterList()->clear();
    inputUpdate->attrs()->clusterList()->push_back(originatorId);

    auto inputAttrs = std::make_shared<facebook::bgp::BgpPath>(
        BgpPathFields(*BgpUpdate2toBgpPathC(*inputUpdate)));
    inputAttrs->publish();
    EXPECT_EQ(std::nullopt, inputAttrs->getLocalPref());

    // originatorId and clusterList are in host byte order in BgpPath
    EXPECT_EQ(originatorId, inputAttrs->getOriginatorId());
    EXPECT_THAT(inputAttrs->getClusterList().get(), ElementsAre(originatorId));

    auto outputAttrs = adjRib_->updateAttributesIn(inputAttrs);
    EXPECT_EQ(facebook::bgp::kDefaultLocalPref, outputAttrs->getLocalPref());
    EXPECT_EQ(0, outputAttrs->getOriginatorId());
    EXPECT_TRUE(outputAttrs->getClusterList().nullOrEmpty());
  }
}

TEST_F(AdjRibInboundFixture, VerifyTinyPeerInfoInRibInMessages) {
  {
    // non-confed IBGP to kRemoteAs1
    setupAdjRib();

    fm_->addTask([&] {
      auto update = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
      adjRibInQ_->fiberPush(std::move(update));

      auto withdraw = createV4BgpUpdateSingleWithdraw(kV4Prefix1);
      adjRibInQ_->fiberPush(std::move(withdraw));
    });

    fm_->addTask([&] {
      std::vector<folly::CIDRNetwork> prefixSet{kV4Prefix1};
      PrefixPathIds pfxPathIds{{kV4Prefix1, kDefaultPathID}};
      {
        // Verify RibInAnnouncement
        auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
        ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));
        auto announcement = std::get<RibInAnnouncement>(msg);
        EXPECT_EQ(kPeerAddr1, announcement.peer.addr);
        EXPECT_EQ(kRemoteAs1, announcement.peer.asn);
        EXPECT_EQ(BgpSessionType::IBGP, announcement.peer.sessionType);
        EXPECT_EQ(
            kPeerRouterId1, announcement.peer.routerId); // default, not used
        EXPECT_FALSE(announcement.peer.isRrClient); // default, not used
        EXPECT_EQ(pfxPathIds, announcement.pfxPathIds);
        EXPECT_EQ(kV4Nexthop1, announcement.attrs->getNexthop());
      }
      {
        // Verify RibInWithdrawal
        auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
        ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(msg));
        auto withdrawal = std::get<RibInWithdrawal>(msg);
        EXPECT_EQ(kPeerAddr1, withdrawal.peer.addr);
        EXPECT_EQ(kRemoteAs1, withdrawal.peer.asn);
        EXPECT_EQ(BgpSessionType::IBGP, withdrawal.peer.sessionType);
        EXPECT_EQ(
            kPeerRouterId1, withdrawal.peer.routerId); // default, not used
        EXPECT_FALSE(withdrawal.peer.isRrClient); // default, not used
        EXPECT_EQ(pfxPathIds, withdrawal.pfxPathIds);
      }
      terminateAdjRib();
    });

    evb_.loop();
  }
  {
    // non-confed EBGP to kRemoteAs2
    setupAdjRib(
        kShortGrRestartTime, // default
        std::nullopt, // remoteGrRestartTime
        false, // callSessionEstablished
        kLocalAs1, // default
        kLocalAs1, // default
        kRemoteAs2);
    establishSession(
        std::nullopt,
        AfiIpv4Negotiated(true),
        AfiIpv6Negotiated(true),
        kAsSeqAsNum);

    fm_->addTask([&] {
      auto update = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
      adjRibInQ_->fiberPush(std::move(update));

      auto withdraw = createV4BgpUpdateSingleWithdraw(kV4Prefix1);
      adjRibInQ_->fiberPush(std::move(withdraw));
    });

    fm_->addTask([&] {
      std::vector<folly::CIDRNetwork> prefixSet{kV4Prefix1};
      PrefixPathIds pfxPathIds{{kV4Prefix1, kDefaultPathID}};
      {
        // Verify RibInAnnouncement
        auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
        ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));
        auto announcement = std::get<RibInAnnouncement>(msg);
        EXPECT_EQ(kPeerAddr1, announcement.peer.addr);
        EXPECT_EQ(kAsSeqAsNum, announcement.peer.asn);
        EXPECT_EQ(BgpSessionType::EBGP, announcement.peer.sessionType);
        EXPECT_EQ(
            kPeerRouterId1, announcement.peer.routerId); // default, not used
        EXPECT_FALSE(announcement.peer.isRrClient); // default, not used
        EXPECT_EQ(pfxPathIds, announcement.pfxPathIds);
        EXPECT_EQ(kV4Nexthop1, announcement.attrs->getNexthop());
      }
      {
        // Verify RibInWithdrawal
        auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
        ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(msg));
        auto withdrawal = std::get<RibInWithdrawal>(msg);
        EXPECT_EQ(kPeerAddr1, withdrawal.peer.addr);
        EXPECT_EQ(kAsSeqAsNum, withdrawal.peer.asn);
        EXPECT_EQ(BgpSessionType::EBGP, withdrawal.peer.sessionType);
        EXPECT_EQ(
            kPeerRouterId1, withdrawal.peer.routerId); // default, not used
        EXPECT_FALSE(withdrawal.peer.isRrClient); // default, not used
        EXPECT_EQ(pfxPathIds, withdrawal.pfxPathIds);
      }
      terminateAdjRib();
    });

    evb_.loop();
  }
  {
    // confed EBGP to kRemoteAs2
    setupAdjRib(
        kShortGrRestartTime, // localGrRestartTime
        std::nullopt, // remoteGrRestartTime
        true, // callSessionEstablished
        kLocalAs1,
        kLocalAs1,
        kRemoteAs2,
        AfiIpv4Negotiated(true),
        AfiIpv6Negotiated(true),
        nullptr, // policy
        std::nullopt, // ingressPolicyName
        kIsConfedPeerTrue, // isConfedPeer
        static_cast<uint32_t>(kLocalAs1), // localConfedAs
        static_cast<uint32_t>(kLocalAs2)); // asConfedId

    fm_->addTask([&] {
      auto update = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
      // expect confed as path segment from confed peer
      BgpAttrAsPathSegment segment;
      segment.asConfedSequence()->push_back(kRemoteAs2);
      update->attrs()->asPath()->insert(
          update->attrs()->asPath()->begin(), segment);
      adjRibInQ_->fiberPush(std::move(update));

      auto withdraw = createV4BgpUpdateSingleWithdraw(kV4Prefix1);
      adjRibInQ_->fiberPush(std::move(withdraw));
    });

    fm_->addTask([&] {
      std::vector<folly::CIDRNetwork> prefixSet{kV4Prefix1};
      PrefixPathIds pfxPathIds{{kV4Prefix1, kDefaultPathID}};
      {
        // Verify RibInAnnouncement
        auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
        ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));
        auto announcement = std::get<RibInAnnouncement>(msg);
        EXPECT_EQ(kPeerAddr1, announcement.peer.addr);
        EXPECT_EQ(kRemoteAs2, announcement.peer.asn);
        EXPECT_EQ(BgpSessionType::ConfedEBGP, announcement.peer.sessionType);
        EXPECT_EQ(
            kPeerRouterId1, announcement.peer.routerId); // default, not used
        EXPECT_FALSE(announcement.peer.isRrClient); // default, not used
        EXPECT_EQ(pfxPathIds, announcement.pfxPathIds);
        EXPECT_EQ(kV4Nexthop1, announcement.attrs->getNexthop());
      }
      {
        // Verify RibInWithdrawal
        auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
        ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(msg));
        auto withdrawal = std::get<RibInWithdrawal>(msg);
        EXPECT_EQ(kPeerAddr1, withdrawal.peer.addr);
        EXPECT_EQ(kRemoteAs2, withdrawal.peer.asn);
        EXPECT_EQ(BgpSessionType::ConfedEBGP, withdrawal.peer.sessionType);
        EXPECT_EQ(
            kPeerRouterId1, withdrawal.peer.routerId); // default, not used
        EXPECT_FALSE(withdrawal.peer.isRrClient); // default, not used
        EXPECT_EQ(pfxPathIds, withdrawal.pfxPathIds);
      }
      terminateAdjRib();
    });

    evb_.loop();
  }
}

/******************************************************************************
 *      END   -   Update processing correctness tests                         *
 ******************************************************************************/

/******************************************************************************
 *      START   -   Route change tests caused by incremental BGP update       *
 ******************************************************************************/

/*
 * Verify that a learnt route is deleted if we get update
 * with our own AS in the path attributes
 */
TEST_F(AdjRibInboundFixture, AsLoopRouteProcessingAfterLearning) {
  setupAdjRib();

  fm_->addTask([&] {
    // Announce a route which is learnt
    auto update1 = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
    adjRibInQ_->fiberPush(std::move(update1));

    // Announce the same route with our own AS
    auto update2 = createV4BgpUpdateWithAsLoop(kV4Prefix1);
    adjRibInQ_->fiberPush(std::move(update2));
  });

  fm_->addTask([&] {
    auto msg1 = facebook::bgp::test::boundedBlockingPop(
        ribInQ_, "ribInQ_"); // add route
    auto msg2 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_"); // withdraw

    // Verify withdrawal message
    ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(msg2));
    auto withdraw = std::get<RibInWithdrawal>(msg2);
    PrefixPathIds prefixSet{{kV4Prefix1, kDefaultPathID}};
    EXPECT_EQ(prefixSet, withdraw.pfxPathIds);

    // Verify route is deleted
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);
    EXPECT_EQ(0, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());

    terminateAdjRib();
  });

  evb_.loop();
}

// Verify that nexthop change is processed and notified to rib
TEST_F(AdjRibInboundFixture, NexthopChangeHandling) {
  setupAdjRib();

  fm_->addTask([&] {
    auto update = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
    adjRibInQ_->fiberPush(std::move(update));

    // Updating nexthop
    auto update2 = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop2);
    adjRibInQ_->fiberPush(std::move(update2));
  });

  fm_->addTask([&] {
    auto msg1 = facebook::bgp::test::boundedBlockingPop(
        ribInQ_, "ribInQ_"); // add route
    auto msg2 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_"); // withdraw

    // Verify initial rib In message
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg1));
    auto ann = std::get<RibInAnnouncement>(msg1);
    PrefixPathIds prefixSet{{kV4Prefix1, kDefaultPathID}};
    EXPECT_EQ(prefixSet, ann.pfxPathIds);
    EXPECT_EQ(kV4Nexthop1, ann.attrs->getNexthop());

    // Verify updated rib In message
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg2));
    auto ann2 = std::get<RibInAnnouncement>(msg2);
    EXPECT_EQ(prefixSet, ann2.pfxPathIds);
    EXPECT_EQ(kV4Nexthop2, ann2.attrs->getNexthop());

    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry);
    EXPECT_EQ(kV4Nexthop2, adjRibEntry->getPreIn()->getNexthop());

    EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCount());

    terminateAdjRib();
  });

  evb_.loop();
}

/**
 * Verify that AdjRib processes the subsequent route announcement for the same
 * prefix that policy rejects. Also verify that stats are updated correctly.
 *
 * Steps
 *  1) Announce prefix1 with EGP type. Policy accepts it and it appears in Rib
 *  2) Update prefix1 with IGP type. Policy rejects it and is withdrawn from Rib
 */
TEST_F(AdjRibInboundFixture, V4PolicyAcceptReject) {
  /*
   * Create a policy with three terms
   * Term1 match kV4Prefix1, kV4Prefix2 and apply origin action (EGP) & as
   * path overwrite action as_path_overwrite_list set to {0, 0}, AdjRib will
   * override 0 asns based on ingress or egress routes; Term2 match kV4Prefix3
   * and discard Term3 match kV4Prefix4 and PERMIT (do not modify any
   * attributes)
   */
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setupDenyIgpOriginAcceptAllPolicy(policyName);

  setupAdjRib(policyManager, policyName);

  fm_->addTask([&] {
    // 1) Announce prefix1 with EGP type
    {
      auto update = createV4BgpUpdateSingleAnnounce(
          kV4Prefix1,
          kV4Nexthop1,
          kMed,
          kOriginatorId,
          BgpAttrOrigin::BGP_ORIGIN_EGP);
      adjRibInQ_->fiberPush(std::move(update));

      // Appears in RIB as update
      auto ribUpdate =
          facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribUpdate));
      EXPECT_EQ(1, std::get<RibInAnnouncement>(ribUpdate).pfxPathIds.size());

      // Verify stats
      auto stats = adjRib_->getStats();
      EXPECT_EQ(1, stats.getPreInPrefixCount());
      EXPECT_EQ(1, stats.getPostInPrefixCount());
    }

    // 2) Announce prefix1 with IGP type
    {
      auto update = createV4BgpUpdateSingleAnnounce(
          kV4Prefix1,
          kV4Nexthop1,
          kMed,
          kOriginatorId,
          BgpAttrOrigin::BGP_ORIGIN_IGP);
      adjRibInQ_->fiberPush(std::move(update));

      // Appears in RIB as withdraw
      auto ribUpdate =
          facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(ribUpdate));
      EXPECT_EQ(1, std::get<RibInWithdrawal>(ribUpdate).pfxPathIds.size());

      // Verify stats (Received but not accepted)
      auto stats = adjRib_->getStats();
      EXPECT_EQ(1, stats.getPreInPrefixCount());
      EXPECT_EQ(0, stats.getPostInPrefixCount());
    }

    // Trigger test termination condition
    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * Ensure that a BGPupdate2 with multiple v4 prefix when policy applied is
 * properly processed.
 * Verify the following:
 * Prefix discarded is processed properly
 * Accepted prefixes with policy modified attributes are processed properly
 * Accepted prefix without changes to attributes is processed properly
 * Multiple prefixes sharing same attributes are notified to Rib together
 */
TEST_F(AdjRibInboundFixture, V4UpdatePolicyProcessing) {
  /*
   * Create a policy with three terms
   * Term1 match kV4Prefix1, kV4Prefix2 and apply origin action (EGP) & as
   * path overwrite action as_path_overwrite_list set to {0, 0}, AdjRib will
   * override 0 asns based on ingress or egress routes; Term2 match kV4Prefix3
   * and discard Term3 match kV4Prefix4 and PERMIT (do not modify any
   * attributes)
   */
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setup3TermPolicy(policyName);

  setupAdjRib(policyManager, policyName);

  const std::vector<folly::CIDRNetwork> inputPrefixSet{
      kV4Prefix1, kV4Prefix2, kV4Prefix3, kV4Prefix4};

  fm_->addTask([&] {
    auto update = createV4BgpUpdateMultipleAnnounce(inputPrefixSet);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    /*
     * Verify ribIn messages (2 messages)
     * 1st message with 1 prefix
     * 2nd message with 2 prefixes sharing attributes
     * NOTE: Here order of messages is hardcoded.
     *       If we change order of processing we need to modify testcase.
     */
    auto msg1 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg1));
    const auto announcement1 = std::get<RibInAnnouncement>(msg1);
    EXPECT_EQ(kPeerAddr1, announcement1.peer.addr);
    const PrefixPathIds expectedPrefixSet1{{kV4Prefix4, kDefaultPathID}};
    EXPECT_EQ(expectedPrefixSet1, announcement1.pfxPathIds);

    auto msg2 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg2));
    const auto announcement2 = std::get<RibInAnnouncement>(msg2);
    EXPECT_EQ(kPeerAddr1, announcement2.peer.addr);
    const PrefixPathIds expectedPrefixSet2{
        {kV4Prefix1, kDefaultPathID}, {kV4Prefix2, kDefaultPathID}};
    EXPECT_EQ(expectedPrefixSet2, announcement2.pfxPathIds);

    fiberSleepFor(20ms);
    EXPECT_TRUE(ribInQ_.empty());

    const std::vector<folly::CIDRNetwork> permittedPrefixSet{
        kV4Prefix1, kV4Prefix2, kV4Prefix4};
    for (const auto& prefix : permittedPrefixSet) {
      // Verify adjrib entries are created for all permitted prefixes
      auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, prefix);
      ASSERT_NE(nullptr, adjRibEntry);
      ASSERT_NE(nullptr, adjRibEntry->getPreIn());
      ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
      // Token check few parameters
      EXPECT_TRUE(adjRibEntry->getPreIn()->isPublished());
      EXPECT_TRUE(adjRibEntry->getPostAttr()->isPublished());
      // unmodified fields by policy
      EXPECT_EQ(kMed, adjRibEntry->getPreIn()->getMed());
      EXPECT_EQ(kLocalPref, adjRibEntry->getPreIn()->getLocalPref());
      EXPECT_EQ(kV4Nexthop1, adjRibEntry->getPreIn()->getNexthop());
      EXPECT_EQ(kMed, adjRibEntry->getPostAttr()->getMed());
      EXPECT_EQ(kLocalPref, adjRibEntry->getPostAttr()->getLocalPref());
      EXPECT_EQ(kV4Nexthop1, adjRibEntry->getPostAttr()->getNexthop());
    }

    auto adjRibEntry1 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    auto adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
    auto adjRibEntry3 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix3);
    auto adjRibEntry4 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix4);

    /*
     * Verify policy permitted and modified attribute prefixes
     * (kV4Prefix1, kV4Prefix2) share postIn. Shallow compare
     */
    EXPECT_EQ(adjRibEntry1->getPostAttr(), adjRibEntry2->getPostAttr());
    EXPECT_NE(adjRibEntry1->getPostAttr(), adjRibEntry4->getPostAttr());
    EXPECT_NE(adjRibEntry1->getPreIn(), adjRibEntry1->getPostAttr());
    // Check modified fields by policy
    EXPECT_EQ(
        BgpAttrOrigin::BGP_ORIGIN_IGP, adjRibEntry1->getPreIn()->getOrigin());
    EXPECT_EQ(
        BgpAttrOrigin::BGP_ORIGIN_EGP,
        adjRibEntry1->getPostAttr()->getOrigin());
    // Check postIn is properly notified to rib
    EXPECT_EQ(announcement2.attrs, adjRibEntry1->getPostAttr());

    /*
     * Verify DENIED prefix (kV4Prefix3) adjrib entry
     * There will be an rib entry corresponding to kV4Prefix3.
     */
    EXPECT_EQ(nullptr, adjRibEntry3->getPostAttr());
    EXPECT_EQ(adjRibEntry3->getPreIn(), adjRibEntry1->getPreIn());

    /*
     * Verify policy unmodified PERMITED prefix (kV4Prefix4) fields.
     * PreIn == PostIn (comparing deep copy), PostIn and rib notified
     * attributes are same
     */
    EXPECT_EQ(*adjRibEntry4->getPreIn(), *adjRibEntry4->getPostAttr());
    EXPECT_EQ(adjRibEntry4->getPreIn(), adjRibEntry1->getPreIn());
    EXPECT_EQ(announcement1.attrs, adjRibEntry4->getPostAttr());

    /*
     * Verify as path in attributes (was {0, 0} after policy action)
     * has been replace to two remoteAs of peer
     */
    auto asPath = adjRibEntry1->getPostAttr()->getAsPath();
    EXPECT_EQ(1, asPath->size());
    auto expectedAsns = std::vector<uint32_t>{
        adjRib_->peeringParams_.remoteAs, adjRib_->peeringParams_.remoteAs};
    EXPECT_EQ(expectedAsns, asPath->at(0).asSequence);
    // Same policy should apply to other entries as well
    EXPECT_EQ(asPath, adjRibEntry2->getPostAttr()->getAsPath());

    EXPECT_EQ(4, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(3, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(1, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(7, adjRib_->getStats().getTotalAttributeUpdates());

    terminateAdjRib();
  });

  evb_.loop();
}

TEST_F(AdjRibInboundFixture, IngressPolicyAsPathUsesAcceptedRemoteAs) {
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setup3TermPolicy(policyName);

  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, /* remoteGrRestartTime */
      false, /* callSessionEstablished */
      kLocalAs3,
      kLocalAs3,
      kRemoteAs1,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      policyManager,
      policyName);
  establishSession(
      std::nullopt,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      kRemoteAs2);

  fm_->addTask([&] {
    auto update = createV4BgpUpdateMultipleAnnounce({kV4Prefix1});
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    auto message = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(message));

    const auto& attrs = std::get<RibInAnnouncement>(message).attrs;
    ASSERT_NE(nullptr, attrs);
    const auto& asPath = attrs->getAsPath();
    ASSERT_EQ(1, asPath->size());
    EXPECT_EQ(
        (std::vector<uint32_t>{kRemoteAs2, kRemoteAs2}),
        asPath->at(0).asSequence);

    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * Verify that a prefix which is denied due to policy and later changes it's
 * attributes and is permitted by policy due to attribute changes is processed
 * properly and notified to Rib
 */
TEST_F(AdjRibInboundFixture, VerifyPermitAfterDeny) {
  /*
   * Create a policy with two terms
   * Term1 match origin IGP and deny
   * Term2 permit all
   */
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setupDenyIgpOriginAcceptAllPolicy(policyName);

  setupAdjRib(policyManager, policyName);

  fm_->addTask([&] {
    {
      // Update 1 which will be denied due to policy
      auto update = createV4BgpUpdateSingleAnnounce(
          kV4Prefix1,
          kV4Nexthop1,
          kMed,
          kOriginatorId,
          BgpAttrOrigin::BGP_ORIGIN_IGP);
      adjRibInQ_->fiberPush(std::move(update));
    }
    {
      // Update 2 (modified origin) for same prefix will be accepted by policy
      auto update = createV4BgpUpdateSingleAnnounce(
          kV4Prefix1,
          kV4Nexthop1,
          kMed,
          kOriginatorId,
          BgpAttrOrigin::BGP_ORIGIN_EGP);
      adjRibInQ_->fiberPush(std::move(update));
    }
  });

  fm_->addTask([&] {
    /*
     * Update 1 will not lead to any Rib notification
     * Verifying only after Update 2 is sent
     */
    auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));
    const auto announcement = std::get<RibInAnnouncement>(msg);
    EXPECT_EQ(kPeerAddr1, announcement.peer.addr);
    const PrefixPathIds expectedPrefixSet{{kV4Prefix1, kDefaultPathID}};
    EXPECT_EQ(expectedPrefixSet, announcement.pfxPathIds);

    fiberSleepFor(20ms);
    EXPECT_TRUE(ribInQ_.empty());

    // Verify adjrib entry is proper
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    ASSERT_NE(nullptr, adjRibEntry);
    ASSERT_NE(nullptr, adjRibEntry->getPreIn());
    ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
    EXPECT_EQ(*adjRibEntry->getPreIn(), *adjRibEntry->getPostAttr());
    EXPECT_EQ(adjRibEntry->getPostAttr(), announcement.attrs);
    EXPECT_EQ(
        adjRibEntry->getPostAttr()->getCommunities(),
        announcement.attrs->getCommunities());

    EXPECT_EQ(
        adjRibEntry->getPostAttr()->getOrigin(),
        announcement.attrs->getOrigin());
    EXPECT_EQ(adjRibEntry->getPostAttr()->getMed(), kMed);
    EXPECT_EQ(adjRibEntry->getPostAttr()->getOriginatorId(), kOriginatorId);
    EXPECT_EQ(
        BgpAttrOrigin::BGP_ORIGIN_EGP, adjRibEntry->getPostAttr()->getOrigin());

    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * Verify that a prefix which is permitted due to policy and later changes
 * it's attributes and is denied by policy due to attribute changes is
 * processed properly and notified to Rib. i.e. 2nd BgpUpdate Announcement
 * leads to AdjRib sending RibInWithdrawal
 */
TEST_F(AdjRibInboundFixture, VerifyDenyAfterPermit) {
  /*
   * Create a policy with two terms
   * Term1 match origin IGP and deny
   * Term2 permit all
   */
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setupDenyIgpOriginAcceptAllPolicy(policyName);

  setupAdjRib(policyManager, policyName);

  fm_->addTask([&] {
    {
      // Update 1 which will be permitted by policy
      auto update = createV4BgpUpdateSingleAnnounce(
          kV4Prefix1,
          kV4Nexthop1,
          kMed,
          kOriginatorId,
          BgpAttrOrigin::BGP_ORIGIN_EGP);
      adjRibInQ_->fiberPush(std::move(update));
    }
    fiberSleepFor(40ms);
    {
      // Update 2 (modified origin) for same prefix will be denied by policy
      auto update = createV4BgpUpdateSingleAnnounce(
          kV4Prefix1,
          kV4Nexthop1,
          kMed,
          kOriginatorId,
          BgpAttrOrigin::BGP_ORIGIN_IGP);
      adjRibInQ_->fiberPush(std::move(update));
    }
  });

  fm_->addTask([&] {
    {
      // Verifying first kV4Prefix1 update is announced
      auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));
      const auto announcement = std::get<RibInAnnouncement>(msg);
      EXPECT_EQ(kPeerAddr1, announcement.peer.addr);
      const PrefixPathIds expectedPrefixSet{{kV4Prefix1, kDefaultPathID}};
      EXPECT_EQ(expectedPrefixSet, announcement.pfxPathIds);

      // Verify adjrib entry is proper
      auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
      ASSERT_NE(nullptr, adjRibEntry);
      ASSERT_NE(nullptr, adjRibEntry->getPreIn());
      ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
      EXPECT_EQ(*adjRibEntry->getPreIn(), *adjRibEntry->getPostAttr());
      EXPECT_EQ(adjRibEntry->getPostAttr(), announcement.attrs);
      EXPECT_EQ(
          BgpAttrOrigin::BGP_ORIGIN_EGP, adjRibEntry->getPreIn()->getOrigin());
    }
    {
      /*
       * Verifying withdrawal due to policy denying kV4Prefix1
       * after attribute change
       */
      auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(msg));
      const auto withdrawal = std::get<RibInWithdrawal>(msg);
      const PrefixPathIds withdrawPrefixSet{{kV4Prefix1, kDefaultPathID}};
      EXPECT_EQ(withdrawPrefixSet, withdrawal.pfxPathIds);

      // Verify adjrib entry is proper
      auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
      ASSERT_NE(nullptr, adjRibEntry);
      ASSERT_NE(nullptr, adjRibEntry->getPreIn());
      EXPECT_EQ(
          BgpAttrOrigin::BGP_ORIGIN_IGP, adjRibEntry->getPreIn()->getOrigin());
      EXPECT_EQ(nullptr, adjRibEntry->getPostAttr());
    }
    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * Verify that an add-path prefix, which is initially permitted due to policy
 * is denied by policy, when an update with changed attributes is received.
 * Check that attribute changes are processed properly and prefix notified as
 * withdrawn to Rib.
 */
TEST_F(AdjRibInboundFixture, VerifyDenyAfterPermitAddpath) {
  /*
   * Create a policy with two terms
   * Term1 match origin IGP and deny
   * Term2 permit all
   */
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setupDenyIgpOriginAcceptAllPolicy(policyName);

  setupAdjRib(policyManager, policyName);

  fm_->addTask([&] {
    /*
     * recAddPath_ must be overwritten in a task, to allow
     * AdjRibInboundFixture::establishSession() created task
     * to run adjRib->sessionEstablished() and initialize recAddPath_,
     * before this overwrite gets to run.
     */
    adjRib_->recAddPath_ = true;
    {
      // Update 1 which will be permitted by policy
      auto update = createV4BgpUpdateSingleAnnounce(
          kV4Prefix1,
          kV4Nexthop1,
          kMed,
          kOriginatorId,
          BgpAttrOrigin::BGP_ORIGIN_EGP);
      for (auto& rigPrefix : *update->v4Announced2()) {
        rigPrefix.pathId() = 1;
      }
      adjRibInQ_->fiberPush(std::move(update));
    }
    fiberSleepFor(40ms);
    {
      // Update 2 (modified origin) for same prefix will be denied by policy
      auto update = createV4BgpUpdateSingleAnnounce(
          kV4Prefix1,
          kV4Nexthop1,
          kMed,
          kOriginatorId,
          BgpAttrOrigin::BGP_ORIGIN_IGP);
      for (auto& rigPrefix : *update->v4Announced2()) {
        rigPrefix.pathId() = 1;
      }
      adjRibInQ_->fiberPush(std::move(update));
    }
  });

  fm_->addTask([&] {
    {
      // Verifying first kV4Prefix1 update is announced
      auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));
      const auto announcement = std::get<RibInAnnouncement>(msg);
      EXPECT_EQ(kPeerAddr1, announcement.peer.addr);
      const PrefixPathIds expectedPrefixSet{{kV4Prefix1, 1}};
      EXPECT_EQ(expectedPrefixSet, announcement.pfxPathIds);
      EXPECT_EQ(kV4Nexthop1, announcement.attrs->getNexthop());

      // Verify AdjRibLiteTree size is zero
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(
              /*ingress=*/true, /*isAddPathEnabled=*/false));
      // Verify AdjRibTree size is non-zero
      EXPECT_EQ(
          1,
          adjRib_->getRibTreeSize(
              /*ingress=*/true, /*isAddPathEnabled=*/true));

      // Verify adjrib entry is proper
      EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCount());
    }
    {
      /*
       * Verifying withdrawal due to policy denying kV4Prefix1
       * after attribute change
       */
      auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(msg));
      const auto withdrawal = std::get<RibInWithdrawal>(msg);
      const PrefixPathIds withdrawPrefixSet{{kV4Prefix1, 1}};
      EXPECT_EQ(withdrawPrefixSet, withdrawal.pfxPathIds);

      // Verify AdjRibLiteTree size is zero
      EXPECT_EQ(
          0,
          adjRib_->getRibTreeSize(
              /*ingress=*/true, /*isAddPathEnabled=*/false));
      // Verify AdjRibTree size is non-zero
      EXPECT_EQ(
          1,
          adjRib_->getRibTreeSize(
              /*ingress=*/true, /*isAddPathEnabled=*/true));

      // Verify adjrib entry is proper
      EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCount());
      EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());
    }
    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * Verify that we change local preference to default value
 * for ebgp-learned routes
 */
TEST_F(AdjRibInboundFixture, LocalPrefPrePolicyProcessing) {
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      true, // callSessionEstablished
      kLocalAs1,
      kLocalAs1,
      kRemoteAs2);

  fm_->addTask([&] {
    auto update = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
    update->attrs()->localPref().reset();
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    auto msg1 = facebook::bgp::test::boundedBlockingPop(
        ribInQ_, "ribInQ_"); // add route

    // Verify preInAttrs is updated
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry);
    EXPECT_EQ(kDefaultLocalPref, adjRibEntry->getPostAttr()->getLocalPref());

    // Verify initial rib In message
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg1));
    auto ann = std::get<RibInAnnouncement>(msg1);
    PrefixPathIds prefixSet{{kV4Prefix1, kDefaultPathID}};
    EXPECT_EQ(prefixSet, ann.pfxPathIds);
    EXPECT_EQ(kDefaultLocalPref, ann.attrs->getLocalPref());

    terminateAdjRib();
  });

  evb_.loop();
}

// Verify that Attribute change is processed and notified to rib
TEST_F(AdjRibInboundFixture, AttributeChangeHandling) {
  setupAdjRib();

  auto medValue1 = 32;
  auto medValue2 = 44;

  fm_->addTask([&] {
    auto update =
        createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1, medValue1);
    adjRibInQ_->fiberPush(std::move(update));

    // updating med
    auto update2 =
        createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1, medValue2);
    adjRibInQ_->fiberPush(std::move(update2));
  });

  fm_->addTask([&] {
    // Verify initial rib In message
    auto msg1 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    auto msg2 = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg2));
    auto ann2 = std::get<RibInAnnouncement>(msg2);
    EXPECT_EQ(medValue2, ann2.attrs->getMed());

    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry);
    EXPECT_EQ(medValue2, adjRibEntry->getPreIn()->getMed());

    EXPECT_EQ(1, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(2, adjRib_->getStats().getRecvUpdateMsgs());
    EXPECT_EQ(2, adjRib_->getStats().getRecvAnnouncementsIpv4());
    EXPECT_EQ(0, adjRib_->getStats().getRecvAnnouncementsIpv6());
    EXPECT_EQ(0, adjRib_->getStats().getRecvWithdrawals());
    EXPECT_EQ(4, adjRib_->getStats().getTotalAttributeUpdates());

    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * Verify that a withdraw message is notified to Rib only if it was previously
 * announced to Rib. (i.e. If a prefix is never announced to Rib due to policy
 * denying it, we will not send withdrawal as well)
 */
TEST_F(
    AdjRibInboundFixture,
    WithdrawNotNotifiedToRibIfPolicyBlockedAnnouncement) {
  /*
   * Create a policy with one term
   * Term1 deny all
   */

  // Creating TERM1 (match all and DENY)
  auto actionDeny = createBgpPolicyAction(BgpPolicyActionType::DENY);
  auto term1 = createBgpPolicyTerm("Term1", "", {}, {actionDeny});

  // Create policy
  const std::string policyName = kIngressPolicyName;
  const auto& policyConfig = createBgpPolicies(policyName, {term1});
  auto policyManager = std::make_shared<PolicyManager>(
      policyConfig, createTestBgpGlobalConfig());

  setupAdjRib(policyManager, policyName);

  fm_->addTask([&] {
    auto update = createV4BgpUpdateSingleAnnounce(kV4Prefix1);
    adjRibInQ_->fiberPush(std::move(update));
    fiberSleepFor(40ms);
    auto withdraw = createV4BgpUpdateSingleWithdraw(kV4Prefix1);
    adjRibInQ_->fiberPush(std::move(withdraw));
  });

  fm_->addTask([&] {
    // Verify first update
    fiberSleepFor(20ms);
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry);
    EXPECT_NE(nullptr, adjRibEntry->getPreIn());
    // Rejected route has an rib entry.
    EXPECT_EQ(nullptr, adjRibEntry->getPostAttr());

    // Verify no messages were sent to Rib
    fiberSleepFor(40ms);
    EXPECT_TRUE(ribInQ_.empty());

    // Verify withdrawal is processed but not notified to Rib
    adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry);

    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * Verify that RibInAnnouncement of a prefix is done only if postInAttrs
 * contents have changed and not because of shared_ptr changed.
 * This can happen for cases like
 * - Bgp update received with same values as policy modified attributes from
 * prior update
 * - Policy created attributes which are exactly same as received attributes
 */
TEST_F(AdjRibInboundFixture, DeepComparePostInAttributesBeforeNotifying) {
  /*
   * Create a policy with one term
   * Term1 match all, set action origin IGP
   */
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setupMatchAllSetOriginIgpPolicy(policyName);

  setupAdjRib(policyManager, policyName);

  fm_->addTask([&] {
    {
      /*
       * Update 1 which will be permitted by policy
       * Policy will modify origin attribute to IGP
       */
      auto update = createV4BgpUpdateSingleAnnounce(
          kV4Prefix1,
          kV4Nexthop1,
          kMed,
          kOriginatorId,
          BgpAttrOrigin::BGP_ORIGIN_EGP);
      adjRibInQ_->fiberPush(std::move(update));
    }
    fiberSleepFor(20ms);
    {
      /*
       * Update 2 for same prefix with IGP origin
       * This update will not lead to any RibInAnnouncement as postInAttrs
       * have not changed from previous update (even though preIn changed)
       */
      auto update = createV4BgpUpdateSingleAnnounce(
          kV4Prefix1,
          kV4Nexthop1,
          kMed,
          kOriginatorId,
          BgpAttrOrigin::BGP_ORIGIN_IGP);
      adjRibInQ_->fiberPush(std::move(update));
    }
  });

  fm_->addTask([&] {
    // Verifying first kV4Prefix1 update is announced
    auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));
    auto announcement = std::get<RibInAnnouncement>(msg);
    EXPECT_EQ(kPeerAddr1, announcement.peer.addr);
    PrefixPathIds expectedPrefixSet{{kV4Prefix1, kDefaultPathID}};
    EXPECT_EQ(expectedPrefixSet, announcement.pfxPathIds);

    // Verify adjrib entry is proper
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    ASSERT_NE(nullptr, adjRibEntry);
    ASSERT_NE(nullptr, adjRibEntry->getPreIn());
    ASSERT_NE(nullptr, adjRibEntry->getPostAttr());
    EXPECT_NE(adjRibEntry->getPreIn(), adjRibEntry->getPostAttr());
    EXPECT_EQ(adjRibEntry->getPostAttr(), announcement.attrs);
    EXPECT_EQ(
        BgpAttrOrigin::BGP_ORIGIN_EGP, adjRibEntry->getPreIn()->getOrigin());
    EXPECT_EQ(
        BgpAttrOrigin::BGP_ORIGIN_IGP, adjRibEntry->getPostAttr()->getOrigin());

    /*
     * No new RibInAnnouncement for the Update 2
     * even though preIn updated (origin changed from EGP to IGP)
     */
    fiberSleepFor(40ms);
    EXPECT_TRUE(ribInQ_.empty());

    EXPECT_EQ(
        BgpAttrOrigin::BGP_ORIGIN_IGP, adjRibEntry->getPreIn()->getOrigin());
    EXPECT_EQ(
        BgpAttrOrigin::BGP_ORIGIN_IGP, adjRibEntry->getPostAttr()->getOrigin());

    terminateAdjRib();
  });

  evb_.loop();
}

/******************************************************************************
 *      END   -   Route change tests caused by incremental BGP update         *
 ******************************************************************************/

/******************************************************************************
 *      START   -   VIP Injector related functionality test.                  *
 ******************************************************************************/
TEST_F(AdjRibInboundFixture, VipInjectorPrefixCount) {
  // Setting a peer of VIP-injector (non-exabgp)
  setupAdjRib(kDynamicPeerId4);

  std::vector<folly::CIDRNetwork> prefixSet{kV4Prefix1, kV4Prefix2};

  fm_->addTask([&] {
    auto update = createV4BgpUpdateMultipleAnnounce(
        prefixSet,
        BgpAttrOrigin::BGP_ORIGIN_IGP,
        kExtCommLbwTypeSecondWord10G,
        65000 /* VIP ASN */);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    // Verify rib In message
    auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));
    EXPECT_EQ(2, totalVipPrefixesCount);

    terminateAdjRib();
  });

  evb_.loop();
}

/******************************************************************************
 *      END   -   VIP Injector related functionality test. *
 ******************************************************************************/

/******************************************************************************
 *      START   -   Ingress UCMP Policy Test                                  *
 ******************************************************************************/

std::shared_ptr<PolicyManager> createPolicyManagerForIngressUcmpPolicy(
    bgp_policy::LbwExtCommunityActionType lbwExtCommActionType) {
  auto createPrefixMatch =
      [&](folly::CIDRNetwork prefix) -> bgp_policy::BgpPolicyAtomicMatch {
    routing_policy::CompareNumericValue compareStructEQ;
    compareStructEQ.compare_operator() = routing_policy::ComparisonOperator::EQ;
    compareStructEQ.value() = prefix.second;
    const auto& prefixListEntry = createPrefixListEntry(
        IPAddress::networkToString(prefix), {compareStructEQ});
    return createPrefixListMatch({prefixListEntry});
  };

  nsf_policy::NsfTeWeightEncoding encoding;
  encoding.l2_encoding() = nsf_policy::NsfL2TeWeightEncoding();
  encoding.l2_encoding()->rack_id() = 4;
  encoding.l2_encoding()->plane_id() = 4;
  encoding.l2_encoding()->remote_rack_capacity() = 8;
  encoding.l2_encoding()->spine_capacity() = 8;
  encoding.l2_encoding()->local_rack_capacity() = 8;

  // create term with UCMP policy
  const auto match = createPrefixMatch(kV4Prefix1);
  auto ucmpAction =
      createBgpPolicyLbwExtCommunityAction(lbwExtCommActionType, encoding);
  auto term = createBgpPolicyTerm("UCMP-term", "", {match}, {ucmpAction});

  // create policy manager
  const std::string policyName = kIngressPolicyName;
  const auto& policyConfig = createBgpPolicies(policyName, {term});
  auto policyManager = std::make_shared<PolicyManager>(
      policyConfig, createTestBgpGlobalConfig());

  return policyManager;
}

TEST_F(AdjRibInboundFixture, IngressUcmpPolicyTestDecodeAll) {
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      true, // callSessionEstablished
      kLocalAs1,
      kLocalAs1,
      kRemoteAs2,
      true,
      true,
      createPolicyManagerForIngressUcmpPolicy(
          bgp_policy::LbwExtCommunityActionType::DECODE_ALL),
      kIngressPolicyName);

  fm_->addTask([&] {
    auto update = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    auto msg1 = facebook::bgp::test::boundedBlockingPop(
        ribInQ_, "ribInQ_"); // add route

    // Verify preInAttrs is updated
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry);
    auto topoInfo = adjRibEntry->getPostAttr()->getTopologyInfo();
    EXPECT_TRUE(topoInfo.has_value());
    /*
     * kExtCommLbwTypeSecondWord10G is
     * (01010000)(00010101)(00000010)(1111)(1001)b
     */
    EXPECT_EQ(topoInfo->at("rack_id"), 9);
    EXPECT_EQ(topoInfo->at("plane_id"), 15);
    EXPECT_EQ(topoInfo->at("remote_rack_capacity"), 2);
    EXPECT_EQ(topoInfo->at("spine_capacity"), 21);
    EXPECT_EQ(topoInfo->at("local_rack_capacity"), 80);

    auto lbwExtComm = adjRibEntry->getPostAttr()->getNonTransitiveRawLbwValue();
    EXPECT_TRUE(lbwExtComm.has_value());
    EXPECT_EQ(*lbwExtComm, kExtCommLbwTypeSecondWord10G);

    // Verify initial rib In message
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg1));
    auto ann = std::get<RibInAnnouncement>(msg1);
    PrefixPathIds prefixSet{{kV4Prefix1, kDefaultPathID}};
    EXPECT_EQ(prefixSet, ann.pfxPathIds);
    EXPECT_EQ(kDefaultLocalPref, ann.attrs->getLocalPref());

    terminateAdjRib();
  });

  evb_.loop();
}

TEST_F(AdjRibInboundFixture, IngressUcmpPolicyTestAccept) {
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      true, // callSessionEstablished
      kLocalAs1,
      kLocalAs1,
      kRemoteAs2,
      true,
      true,
      createPolicyManagerForIngressUcmpPolicy(
          bgp_policy::LbwExtCommunityActionType::ACCEPT),
      kIngressPolicyName);

  fm_->addTask([&] {
    auto update = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    auto msg1 = facebook::bgp::test::boundedBlockingPop(
        ribInQ_, "ribInQ_"); // add route

    // Verify preInAttrs is updated
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry);
    auto topoInfo = adjRibEntry->getPostAttr()->getTopologyInfo();
    EXPECT_FALSE(topoInfo.has_value());

    auto lbwExtComm = adjRibEntry->getPostAttr()->getNonTransitiveRawLbwValue();
    EXPECT_TRUE(lbwExtComm.has_value());
    EXPECT_EQ(*lbwExtComm, kExtCommLbwTypeSecondWord10G);

    // Verify initial rib In message
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg1));
    auto ann = std::get<RibInAnnouncement>(msg1);
    PrefixPathIds prefixSet{{kV4Prefix1, kDefaultPathID}};
    EXPECT_EQ(prefixSet, ann.pfxPathIds);
    EXPECT_EQ(kDefaultLocalPref, ann.attrs->getLocalPref());

    terminateAdjRib();
  });

  evb_.loop();
}

/**
 * Verify that when a route is rejected due to invalid GAR weights
 * (isLbwRejected), the deny reason stored in postInPolicy includes
 * "invalid GAR weights" suffix.
 */
TEST_F(
    AdjRibProcessPeerAnnouncedFixture,
    GetPostInPolicyAttributesTest_RejectedByInvalidGarWeights) {
  /*
   * Create a DECODE_ALL UCMP policy that will trigger isLbwRejected
   * when the route has no LBW extended community.
   */
  auto policyManager = createPolicyManagerForIngressUcmpPolicy(
      bgp_policy::LbwExtCommunityActionType::DECODE_ALL);
  // Set up adjRib with policy manager.
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      false, // callSessionEstablished
      kLocalAs1,
      kLocalAs1,
      kRemoteAs1,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      policyManager,
      kIngressPolicyName);

  /*
   * Create attrs WITHOUT any LBW extended community.
   * DECODE_ALL will set isLbwRejected = true when LBW is missing.
   */
  auto prePolicyAttrs =
      std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  prePolicyAttrs->setOrigin(BgpAttrOrigin::BGP_ORIGIN_IGP);
  prePolicyAttrs->publish();

  // Insert an adjRibEntry for kV4Prefix1 for testing purposes.
  auto adjRibEntry = adjRib_->addRibEntry(true /* ingress */, kV4Prefix1);
  auto policyActionData = adjRib_->createPolicyActionData(prePolicyAttrs);

  // Route should be denied due to missing LBW.
  auto postPolicyAttrs = adjRib_->getPostInPolicyAttributes(
      kV4Prefix1, prePolicyAttrs, policyActionData, adjRibEntry);

  EXPECT_FALSE(postPolicyAttrs);
  // Verify policy result includes the GAR weights reason suffix.
  EXPECT_TRUE(adjRibEntry->getPostInPolicy());
  EXPECT_THAT(
      *adjRibEntry->getPostInPolicy(),
      ::testing::HasSubstr(std::string(kInvalidGarWeightsDenyReason)));
  EXPECT_THAT(
      *adjRibEntry->getPostInPolicy(), ::testing::HasSubstr("Denied by"));
}

/******************************************************************************
 *      END   -   Ingress UCMP Policy Test                                    *
 ******************************************************************************/

/******************************************************************************
 *      START -   Route Filter Policy Test                                    *
 ******************************************************************************/

/**
 * Test setting route filter statements on AdjRib with different configurations.
 * Verifies that statements can be set, updated, and cleared correctly.
 */
TEST_F(AdjRibInboundFixture, SetRouteFilterStatementIngressTest) {
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setupAcceptAllPolicy(policyName);
  setupAdjRib(policyManager, policyName);

  auto dummyPathId1 = 0;
  auto pfx2Entry = std::make_unique<AdjRibEntry>(dummyPathId1);
  auto pfx2Path = std::make_shared<BgpPath>();
  pfx2Path->setNexthop(kV4Nexthop1);
  pfx2Entry->setPreIn(pfx2Path);
  adjRib_->adjRibInLiteTree_.insert(
      kV4Prefix2.first, kV4Prefix2.second, std::move(pfx2Entry));

  adjRib_->adjRibInLiteTree_.insert(
      kV4Prefix2.first, kV4Prefix2.second, std::move(pfx2Entry));

  // Create route filter policy
  rib_policy::TRouteFilterPolicy tPolicy;
  tPolicy.statements()->emplace(
      "stmt1",
      createTRouteFilterStatement(
          {}, false /* permissive */, false /* egress */));
  auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);

  // nullptr -> nullptr (should return (false, false), no change)
  auto [ingressChanged1, egressChanged1] =
      adjRib_->setRouteFilterStatement(nullptr);
  EXPECT_FALSE(ingressChanged1);
  EXPECT_FALSE(egressChanged1);

  // nullptr -> stmt1 (should return (true, false), statement set)
  auto [ingressChanged2, egressChanged2] =
      adjRib_->setRouteFilterStatement(policy->getStatements().at("stmt1"));
  EXPECT_TRUE(ingressChanged2);
  EXPECT_FALSE(egressChanged2);

  // stmt1 -> stmt1 (should return (false, false), same statement)
  auto [ingressChanged3, egressChanged3] =
      adjRib_->setRouteFilterStatement(policy->getStatements().at("stmt1"));
  EXPECT_FALSE(ingressChanged3);
  EXPECT_FALSE(egressChanged3);

  // Create a different statement - permissive mode
  tPolicy.statements()->at("stmt1") = createTRouteFilterStatement(
      {}, true /* permissive */, false /* egress */);
  policy = std::make_unique<RouteFilterPolicy>(tPolicy);

  // stmt1 -> stmt1' (should return true, different statement)
  auto [ingressChanged4, egressChanged4] =
      adjRib_->setRouteFilterStatement(policy->getStatements().at("stmt1"));
  EXPECT_TRUE(ingressChanged4);
  EXPECT_FALSE(egressChanged4);

  // Simulate policy being cleared
  policy.reset();

  // stmt1' -> nullptr (should return true, statement cleared)
  auto [ingressChanged5, egressChanged5] =
      adjRib_->setRouteFilterStatement(nullptr);
  EXPECT_TRUE(ingressChanged5);
  EXPECT_FALSE(egressChanged5);

  fm_->addTask([&] {
    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

TEST_F(AdjRibInboundFixture, SetRouteFilterStatementEgressTest) {
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setupAcceptAllPolicy(policyName);
  setupAdjRib(policyManager, policyName);

  // Create route filter policy for egress (using default value for egress)
  rib_policy::TRouteFilterPolicy tPolicy;
  tPolicy.statements()->emplace(
      "stmt1",
      createTRouteFilterStatement(
          {}, false /* permissive */)); // default value for egress (true)
  auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);

  // nullptr -> nullptr (should return (false, false), no change)
  auto [ingressChanged1, egressChanged1] =
      adjRib_->setRouteFilterStatement(nullptr);
  EXPECT_FALSE(ingressChanged1);
  EXPECT_FALSE(egressChanged1);
  EXPECT_FALSE(adjRib_->isPendingIngressPolicyUpdate());
  EXPECT_FALSE(adjRib_->isEgressPolicyUpdateRequired());

  // nullptr -> stmt1 (should return (false, true), statement set)
  auto [ingressChanged2, egressChanged2] =
      adjRib_->setRouteFilterStatement(policy->getStatements().at("stmt1"));
  EXPECT_FALSE(ingressChanged2);
  EXPECT_TRUE(egressChanged2);

  // stmt1 -> stmt1 (should return (false, false), same statement)
  auto [ingressChanged3, egressChanged3] =
      adjRib_->setRouteFilterStatement(policy->getStatements().at("stmt1"));
  EXPECT_FALSE(ingressChanged3);
  EXPECT_FALSE(egressChanged3);

  // Create a different statement - permissive mode
  tPolicy.statements()->at("stmt1") = createTRouteFilterStatement(
      {}, true /* permissive */); // default value for egress (true)
  policy = std::make_unique<RouteFilterPolicy>(tPolicy);

  // stmt1 -> stmt1' (should return true, different statement)
  auto [ingressChanged4, egressChanged4] =
      adjRib_->setRouteFilterStatement(policy->getStatements().at("stmt1"));
  EXPECT_FALSE(ingressChanged4);
  EXPECT_TRUE(egressChanged4);

  // Simulate policy being cleared
  policy.reset();

  // stmt1' -> nullptr (should return true, statement cleared)
  auto [ingressChanged5, egressChanged5] =
      adjRib_->setRouteFilterStatement(nullptr);
  EXPECT_FALSE(ingressChanged5);
  EXPECT_TRUE(egressChanged5);

  fm_->addTask([&] {
    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

TEST_F(AdjRibInboundFixture, SetRouteFilterStatementNewlyInitializedPeerTest) {
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setupAcceptAllPolicy(policyName);
  setupAdjRib(policyManager, policyName);

  // Create route filter policy for ingress
  rib_policy::TRouteFilterPolicy tPolicy;
  tPolicy.statements()->emplace(
      "stmt1",
      createTRouteFilterStatement(
          {}, false /* permissive */, false /* egress */));
  auto policy = std::make_unique<RouteFilterPolicy>(tPolicy);

  /*
   * Test with newly initialized adjRib (no learned routes yet)
   * nullptr -> stmt1 (should return (true, false), statement set for ingress
   * only)
   */
  auto [ingressChanged1, egressChanged1] = adjRib_->setRouteFilterStatement(
      policy->getStatements().at("stmt1"), nullptr);
  EXPECT_TRUE(ingressChanged1);
  EXPECT_FALSE(egressChanged1);

  // Create a different statement - egress policy
  tPolicy.statements()->at("stmt1") = createTRouteFilterStatement(
      {}, false /* permissive */); // default value for egress (true)
  policy = std::make_unique<RouteFilterPolicy>(tPolicy);

  /*
   * Test egress policy change with newly initialized adjRib
   * stmt1 -> stmt1' (should return (true, true), statement changed removed
   * ingress and added egress)
   */
  auto [ingressChanged2, egressChanged2] = adjRib_->setRouteFilterStatement(
      policy->getStatements().at("stmt1"), nullptr);
  EXPECT_TRUE(ingressChanged2);
  EXPECT_TRUE(egressChanged2);

  // Create a statement affecting both ingress and egress
  tPolicy.statements()->at("stmt1") =
      createTRouteFilterStatementWithIngressAndEgressFilters(
          {}, {}, false /* ingressPermissive */, false /* egressPermissive */);
  policy = std::make_unique<RouteFilterPolicy>(tPolicy);

  /*
   * Test ingress and egress policy change with newly initialized adjRib
   * stmt1' -> stmt1'' (should return (false, true), statement changed for
   * egress only
   */
  auto [ingressChanged3, egressChanged3] = adjRib_->setRouteFilterStatement(
      policy->getStatements().at("stmt1"), nullptr);
  EXPECT_FALSE(ingressChanged3);
  EXPECT_TRUE(egressChanged3);

  fm_->addTask([&] {
    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

/**
 * Test that route filter policy blocks prefix and no RibInAnnouncements are
 * generated for the blocked prefix.
 * No RibInWithdrawals will be generated since this prefix was not previously
 * announced to Rib.
 */
TEST_F(AdjRibInboundFixture, VerifyRouteFilterPolicyDeny) {
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setupAcceptAllPolicy(policyName);

  setupAdjRib(policyManager, policyName);

  auto mockScuba = std::make_shared<MockScubaData>();

  EXPECT_CALL(*mockScuba, addSample(_, _, _, _))
      .Times(2)
      .WillOnce(Invoke(
          [&](const auto& sample,
              auto /* unused */,
              auto /* unused */,
              const auto& /* unused */) -> size_t {
            EXPECT_EQ("rsw001", sample.getNormalValue("device"));
            EXPECT_EQ("rsw.*", sample.getNormalValue("statement"));
            EXPECT_EQ("fsw001", sample.getNormalValue("peer"));
            EXPECT_EQ(
                folly::IPAddress::networkToString(kV4Prefix1),
                sample.getNormalValue("prefix"));
            EXPECT_EQ(0, sample.getIntValue("allow"));
            EXPECT_EQ(0, sample.getIntValue("permissive"));
            EXPECT_EQ(0, sample.getNormVectorValue("communities").size());
            return 1;
          }));
  auto logger = std::make_unique<ScubaRouteFilterLogger>(
      "rsw001", "rsw.*", "fsw001", mockScuba);

  fm_->addTask([&] {
    /*
     * Set route filter statement to block all prefixes (empty list in blocking
     * mode)
     */
    auto tStmt = createTRouteFilterStatement(
        {}, false /* permissive */, false /* egress */);

    auto [ingressChanged2, egressChanged2] = adjRib_->setRouteFilterStatement(
        std::make_shared<RouteFilterStatement>(tStmt), std::move(logger));
    EXPECT_TRUE(ingressChanged2);
    EXPECT_FALSE(egressChanged2);

    /*
     * Announce 2 prefixes and expect both to be blocked by Ingress route filter
     * policy
     */
    auto update1 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1,
        kV4Nexthop1,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);
    adjRibInQ_->fiberPush(std::move(update1));

    auto update2 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix2,
        kV4Nexthop1,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);
    adjRibInQ_->fiberPush(std::move(update2));
  });

  fm_->addTask([&] {
    fiberSleepFor(50ms);
    /*
     * Ensure no RibInAnnouncements or RibInWithdrawals are generated for the
     * blocked prefixes
     */
    WITH_RETRIES_N(5, { EXPECT_TRUE(ribInQ_.empty()); });

    // Verify adjrib entries are correct
    auto adjRibEntry1 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry1);
    EXPECT_NE(nullptr, adjRibEntry1->getPreIn());
    EXPECT_EQ(nullptr, adjRibEntry1->getPostAttr());
    EXPECT_TRUE(adjRibEntry1->getPostInPolicy());
    EXPECT_EQ(
        "Denied by Route Filter Policy", *adjRibEntry1->getPostInPolicy());

    auto adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
    EXPECT_NE(nullptr, adjRibEntry2);
    EXPECT_NE(nullptr, adjRibEntry2->getPreIn());
    EXPECT_EQ(nullptr, adjRibEntry2->getPostAttr());
    EXPECT_TRUE(adjRibEntry2->getPostInPolicy());
    EXPECT_EQ(
        "Denied by Route Filter Policy", *adjRibEntry2->getPostInPolicy());

    // Verify stats
    EXPECT_EQ(0, adjRib_->getStats().getPostInPrefixCount());
    EXPECT_EQ(2, adjRib_->getStats().getTotalIngressRouteFilterDenied());

    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

/**
 * Test to verify route filter and routing policy that allows prefixes generate
 * RibInAnnouncement messages.
 */
TEST_F(AdjRibInboundFixture, VerifyRouteFilterPolicyAllow) {
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setupAcceptAllPolicy(policyName);

  setupAdjRib(policyManager, policyName);

  auto mockScuba = std::make_shared<MockScubaData>();
  auto logger = std::make_unique<ScubaRouteFilterLogger>(
      "rsw001", "rsw.*", "fsw001", mockScuba);

  fm_->addTask([&] {
    // Set route filter statement to allow specific prefixes 1 and 2
    auto tStmt = createTRouteFilterStatement(
        {kV4Prefix1, kV4Prefix2}, false /* permissive */, false /* egress */);
    auto [ingressChanged3, egressChanged3] = adjRib_->setRouteFilterStatement(
        std::make_shared<RouteFilterStatement>(tStmt), std::move(logger));
    EXPECT_TRUE(ingressChanged3);
    EXPECT_FALSE(egressChanged3);

    // Announce a prefix that should be allowed (kV4Prefix1)
    auto update1 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1,
        kV4Nexthop1,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);
    adjRibInQ_->fiberPush(std::move(update1));

    // Should receive RibInAnnouncement for the allowed prefix
    auto ribUpdate1 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribUpdate1));
    auto& announcement1 = std::get<RibInAnnouncement>(ribUpdate1);
    EXPECT_EQ(1, announcement1.pfxPathIds.size());
    // Verify the announced prefix is correct
    EXPECT_EQ(kV4Prefix1, std::get<0>(announcement1.pfxPathIds[0]));

    // Announce another allowed prefix (kV4Prefix2)
    auto update2 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix2,
        kV4Nexthop2,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);
    adjRibInQ_->fiberPush(std::move(update2));

    // Should receive RibInAnnouncement for the second allowed prefix
    auto ribUpdate2 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribUpdate2));
    auto& announcement2 = std::get<RibInAnnouncement>(ribUpdate2);
    EXPECT_EQ(1, announcement2.pfxPathIds.size());
    EXPECT_EQ(kV4Prefix2, std::get<0>(announcement2.pfxPathIds[0]));

    // Verify CRF denied counter is not incremented for allowed prefixes
    EXPECT_EQ(0, adjRib_->getStats().getTotalIngressRouteFilterDenied());

    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

/*
 * VerifyRouteFilterPolicyAllowAndDenyWithAnnouncement
 *
 * This test verifies that route filter policy correctly handles transitions
 * from allowing to denying prefixes when re-announcements occur with changed
 * attributes, ensuring proper RibInQ message generation.
 *
 * Test flow:
 * 1. Create and set route filter statement to allow a specific prefix
 * 2. Announce the prefix and verify RibInAnnouncement is generated
 * 3. Verify the prefix is properly stored in adjRibIn with correct attributes
 * 4. Update route filter statement to deny the same prefix
 * 5. Re-announce the same prefix with changed attributes (different MED, etc.)
 * 6. Verify that RibInWithdrawal is generated for the now-denied prefix
 * despite the re-announcement
 * 7. Confirm the adjRibEntry reflects the policy change and shows denied status
 */
TEST_F(
    AdjRibInboundFixture,
    VerifyRouteFilterPolicyAllowAndDenyWithAnnouncement) {
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setupAcceptAllPolicy(policyName);

  setupAdjRib(policyManager, policyName);

  auto mockScuba = std::make_shared<MockScubaData>();
  auto logger = std::make_unique<ScubaRouteFilterLogger>(
      "rsw001", "rsw.*", "fsw001", mockScuba);

  fm_->addTask([&] {
    // Step 1: Set route filter statement to allow specific prefixes 1 and 2
    auto tStmt = createTRouteFilterStatement(
        {kV4Prefix1, kV4Prefix2}, false /* permissive */, false /* egress */);
    auto [ingressChanged1, egressChanged1] = adjRib_->setRouteFilterStatement(
        std::make_shared<RouteFilterStatement>(tStmt), std::move(logger));
    EXPECT_TRUE(ingressChanged1);
    EXPECT_FALSE(egressChanged1);

    // Step 2: Announce a prefix that should be allowed (kV4Prefix1)
    auto update1 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1,
        kV4Nexthop1,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);
    adjRibInQ_->fiberPush(std::move(update1));

    // Should receive RibInAnnouncement for the allowed prefix
    auto ribUpdate1 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribUpdate1));
    auto& announcement1 = std::get<RibInAnnouncement>(ribUpdate1);
    EXPECT_EQ(1, announcement1.pfxPathIds.size());
    // Verify the announced prefix is correct
    EXPECT_EQ(kV4Prefix1, std::get<0>(announcement1.pfxPathIds[0]));

    // Step 3: Announce another allowed prefix (kV4Prefix2)
    auto update2 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix2,
        kV4Nexthop2,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);
    adjRibInQ_->fiberPush(std::move(update2));

    // Should receive RibInAnnouncement for the second allowed prefix
    auto ribUpdate2 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribUpdate2));
    auto& announcement2 = std::get<RibInAnnouncement>(ribUpdate2);
    EXPECT_EQ(1, announcement2.pfxPathIds.size());
    EXPECT_EQ(kV4Prefix2, std::get<0>(announcement2.pfxPathIds[0]));

    // Verify adjrib entries are correct
    auto adjRibEntry1 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry1);
    EXPECT_NE(nullptr, adjRibEntry1->getPreIn());
    EXPECT_NE(nullptr, adjRibEntry1->getPostAttr());
    EXPECT_TRUE(adjRibEntry1->getPostInPolicy());
    EXPECT_NE(nullptr, adjRibEntry1->getPostInPolicy());

    auto adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
    EXPECT_NE(nullptr, adjRibEntry2);
    EXPECT_NE(nullptr, adjRibEntry2->getPreIn());
    EXPECT_NE(nullptr, adjRibEntry2->getPostAttr());
    EXPECT_TRUE(adjRibEntry2->getPostInPolicy());
    EXPECT_NE(nullptr, adjRibEntry2->getPostInPolicy());

    // Verify stats
    EXPECT_EQ(2, adjRib_->getStats().getPostInPrefixCount());

    // Step 4: Update the policy to block Prefix1
    tStmt = createTRouteFilterStatement(
        {kV4Prefix2}, false /* permissive */, false /* egress */);

    auto [ingressChanged2, egressChanged2] = adjRib_->setRouteFilterStatement(
        std::make_shared<RouteFilterStatement>(tStmt), std::move(logger));
    EXPECT_TRUE(ingressChanged2);
    EXPECT_FALSE(egressChanged2);

    // Step 5: Announce Prefix1 again with different nexthop
    auto update3 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1,
        kV4Nexthop3,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);
    adjRibInQ_->fiberPush(std::move(update3));

    // Should receive RibInWithdrawal for Prefix1
    auto ribUpdate3 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(ribUpdate3));
    auto& withdrawal1 = std::get<RibInWithdrawal>(ribUpdate3);
    EXPECT_EQ(1, withdrawal1.pfxPathIds.size());
    EXPECT_EQ(kV4Prefix1, std::get<0>(withdrawal1.pfxPathIds[0]));

    // Verify adjrib entries are correct
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry);
    EXPECT_NE(nullptr, adjRibEntry->getPreIn());
    EXPECT_EQ(nullptr, adjRibEntry->getPostAttr());
    EXPECT_TRUE(adjRibEntry->getPostInPolicy());
    EXPECT_EQ("Denied by Route Filter Policy", *adjRibEntry->getPostInPolicy());

    // Verify no more withdrawals are generated
    WITH_RETRIES_N(5, { EXPECT_TRUE(ribInQ_.empty()); });

    // Verify adjrib entries are unchanged for Prefix2
    adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
    EXPECT_NE(nullptr, adjRibEntry2);
    EXPECT_NE(nullptr, adjRibEntry2->getPreIn());
    EXPECT_NE(nullptr, adjRibEntry2->getPostAttr());
    EXPECT_TRUE(adjRibEntry2->getPostInPolicy());
    EXPECT_NE(
        "Denied by Route Filter Policy", *adjRibEntry2->getPostInPolicy());

    // Verify stats
    EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCount());

    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

/**
 * Test that verifies egress route filter policy has no impact on announcements
 */
TEST_F(AdjRibInboundFixture, VerifyAnnouncementsWithEgressRouteFilterPolicy) {
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setupAcceptAllPolicy(policyName);

  setupAdjRib(policyManager, policyName);

  auto mockScuba = std::make_shared<MockScubaData>();
  auto logger = std::make_unique<ScubaRouteFilterLogger>(
      "rsw001", "rsw.*", "fsw001", mockScuba);

  fm_->addTask([&] {
    // Set Egress route filter statement to block all prefixes
    auto tStmt = createTRouteFilterStatement(
        {}, false /* permissive */, true /* egress */);

    auto [ingressChanged1, egressChanged1] = adjRib_->setRouteFilterStatement(
        std::make_shared<RouteFilterStatement>(tStmt), std::move(logger));
    EXPECT_FALSE(ingressChanged1);
    EXPECT_TRUE(egressChanged1);

    // Announce a prefix that should be allowed (kV4Prefix1)
    auto update1 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1,
        kV4Nexthop1,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);
    adjRibInQ_->fiberPush(std::move(update1));

    // Should receive RibInAnnouncement for the allowed prefix
    auto ribUpdate1 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribUpdate1));
    auto& announcement1 = std::get<RibInAnnouncement>(ribUpdate1);
    EXPECT_EQ(1, announcement1.pfxPathIds.size());
    // Verify the announced prefix is correct
    EXPECT_EQ(kV4Prefix1, std::get<0>(announcement1.pfxPathIds[0]));

    // Announce another allowed prefix (kV4Prefix2)
    auto update2 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix2,
        kV4Nexthop2,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);
    adjRibInQ_->fiberPush(std::move(update2));

    // Should receive RibInAnnouncement for the second allowed prefix
    auto ribUpdate2 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribUpdate2));
    auto& announcement2 = std::get<RibInAnnouncement>(ribUpdate2);
    EXPECT_EQ(1, announcement2.pfxPathIds.size());
    EXPECT_EQ(kV4Prefix2, std::get<0>(announcement2.pfxPathIds[0]));

    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

/*
 * VerifyRouteFilterPolicyAllowAndDenyWithReEvaluation
 *
 * This test verifies that route filter policy re-evaluation correctly handles
 * transitions from allowing to denying prefixes, ensuring proper RibInQ
 * message generation during policy changes.
 *
 * Test flow:
 * 1. Create and set route filter statement to allow a specific prefix
 * 2. Announce the prefix and verify RibInAnnouncement is generated
 * 3. Verify the prefix is properly stored in adjRibIn with correct attributes
 * 4. Update route filter statement to deny the same prefix
 * 5. Issue policy re-evaluation
 * 6. Verify that RibInWithdrawal is generated for the now-denied prefix
 * 7. Confirm the adjRibEntry is updated with proper postInAttrs reflecting
 * the policy change
 */
TEST_F(
    AdjRibInboundFixture,
    VerifyRouteFilterPolicyAllowAndDenyWithReEvaluation) {
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setupAcceptAllPolicy(policyName);

  setupAdjRib(policyManager, policyName);
  adjRib_->markStateEstablished();
  EXPECT_TRUE(adjRib_->isStateEstablished());

  auto mockScuba = std::make_shared<MockScubaData>();
  auto logger = std::make_unique<ScubaRouteFilterLogger>(
      "rsw001", "rsw.*", "fsw001", mockScuba);

  fm_->addTask([&] {
    // Step 1: Set route filter statement to allow specific prefixes 1 and 2
    auto tStmt = createTRouteFilterStatement(
        {kV4Prefix1, kV4Prefix2}, false /* permissive */, false /* egress */);
    auto [ingressChanged1, egressChanged1] = adjRib_->setRouteFilterStatement(
        std::make_shared<RouteFilterStatement>(tStmt), std::move(logger));
    EXPECT_TRUE(ingressChanged1);
    EXPECT_FALSE(egressChanged1);

    // Step 2: Announce a prefix that should be allowed (kV4Prefix1)
    auto update1 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1,
        kV4Nexthop1,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);
    adjRibInQ_->fiberPush(std::move(update1));

    // Should receive RibInAnnouncement for the allowed prefix
    auto ribUpdate1 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribUpdate1));
    auto& announcement1 = std::get<RibInAnnouncement>(ribUpdate1);
    EXPECT_EQ(1, announcement1.pfxPathIds.size());
    // Verify the announced prefix is correct
    EXPECT_EQ(kV4Prefix1, std::get<0>(announcement1.pfxPathIds[0]));

    // Step 3: Announce another allowed prefix (kV4Prefix2)
    auto update2 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix2,
        kV4Nexthop2,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);
    adjRibInQ_->fiberPush(std::move(update2));

    // Should receive RibInAnnouncement for the second allowed prefix
    auto ribUpdate2 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribUpdate2));
    auto& announcement2 = std::get<RibInAnnouncement>(ribUpdate2);
    EXPECT_EQ(1, announcement2.pfxPathIds.size());
    EXPECT_EQ(kV4Prefix2, std::get<0>(announcement2.pfxPathIds[0]));

    // Verify adjrib entries are correct
    auto adjRibEntry1 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry1);
    EXPECT_NE(nullptr, adjRibEntry1->getPreIn());
    EXPECT_NE(nullptr, adjRibEntry1->getPostAttr());
    EXPECT_TRUE(adjRibEntry1->getPostInPolicy());
    EXPECT_NE(nullptr, adjRibEntry1->getPostInPolicy());

    auto adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
    EXPECT_NE(nullptr, adjRibEntry2);
    EXPECT_NE(nullptr, adjRibEntry2->getPreIn());
    EXPECT_NE(nullptr, adjRibEntry2->getPostAttr());
    EXPECT_TRUE(adjRibEntry2->getPostInPolicy());
    EXPECT_NE(nullptr, adjRibEntry2->getPostInPolicy());

    // Verify stats
    EXPECT_EQ(2, adjRib_->getStats().getPostInPrefixCount());

    // Step 4: Update the policy to block Prefix1
    tStmt = createTRouteFilterStatement(
        {kV4Prefix2}, false /* permissive */, false /* egress */);

    auto [ingressChanged2, egressChanged2] = adjRib_->setRouteFilterStatement(
        std::make_shared<RouteFilterStatement>(tStmt), std::move(logger));
    EXPECT_TRUE(ingressChanged2);
    EXPECT_FALSE(egressChanged2);

    // Step 5: Issue policy re-evaluation
    folly::coro::blockingWait(adjRib_->processAdjRibReEvaluation(
        RibPauseResumeCause::ROUTE_FILTER_POLICY_UPDATE));

    // Should receive RibInWithdrawal for Prefix1
    auto ribUpdate3 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(ribUpdate3));
    auto& withdrawal1 = std::get<RibInWithdrawal>(ribUpdate3);
    EXPECT_EQ(1, withdrawal1.pfxPathIds.size());
    EXPECT_EQ(kV4Prefix1, std::get<0>(withdrawal1.pfxPathIds[0]));

    // Verify adjrib entries are correct
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry);
    EXPECT_NE(nullptr, adjRibEntry->getPreIn());
    EXPECT_EQ(nullptr, adjRibEntry->getPostAttr());
    EXPECT_TRUE(adjRibEntry->getPostInPolicy());
    EXPECT_EQ("Denied by Route Filter Policy", *adjRibEntry->getPostInPolicy());

    // Verify no more withdrawals are generated
    WITH_RETRIES_N(5, { EXPECT_TRUE(ribInQ_.empty()); });

    // Verify adjrib entries are unchanged for Prefix2
    adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
    EXPECT_NE(nullptr, adjRibEntry2);
    EXPECT_NE(nullptr, adjRibEntry2->getPreIn());
    EXPECT_NE(nullptr, adjRibEntry2->getPostAttr());
    EXPECT_TRUE(adjRibEntry2->getPostInPolicy());
    EXPECT_NE(
        "Denied by Route Filter Policy", *adjRibEntry2->getPostInPolicy());

    // Verify stats
    EXPECT_EQ(1, adjRib_->getStats().getPostInPrefixCount());

    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

/*
 * VerifyRouteFilterPolicyReEvaluationWithBatchProcessing
 *
 * This test verifies that route filter policy re-evaluation correctly honors
 * the batch size configuration when sending announcements and withdrawals
 * to RibInQ during policy re-evaluation.
 *
 * Test flow:
 * 1. Create and set route filter statement to allow multiple prefixes
 * 2. Announce multiple prefixes and verify RibInQ announcements and
 * adjRibEntries
 * 3. Update route filter statement to deny some prefixes
 * 4. Issue policy re-evaluation
 * 5. Verify that announcements/withdrawals are sent to RibInQ in batches
 * according to the configured batch size, not all at once
 * 6. Confirm that all affected prefixes are properly re-evaluated and
 * their states updated correctly
 */
TEST_F(
    AdjRibInboundFixture,
    VerifyRouteFilterPolicyReEvaluationWithBatchProcessing) {
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setupAcceptAllPolicy(policyName);

  setupAdjRib(policyManager, policyName);
  adjRib_->markStateEstablished();
  EXPECT_TRUE(adjRib_->isStateEstablished());

  // set batch size to 2
  adjRib_->policyReEvaluationBatchSize_ = 2;

  auto mockScuba = std::make_shared<MockScubaData>();
  auto logger = std::make_unique<ScubaRouteFilterLogger>(
      "rsw001", "rsw.*", "fsw001", mockScuba);
  std::array<folly::fibers::Baton, 4> syncBaton; // Added one more baton
  std::vector<folly::CIDRNetwork> prefixSet1{
      kV4Prefix2, kV4Prefix3, kV4Prefix4};

  fm_->addTask([&] {
    facebook::bgp::test::boundedBatonWait(syncBaton[0], "syncBaton[0]");
    // Step 1: Set route filter statement to allow specific prefixes 1 and 2
    auto tStmt = createTRouteFilterStatement(
        {kV4Prefix1, kV4Prefix2}, false /* permissive */, false /* egress */);
    auto [ingressChanged1, egressChanged1] = adjRib_->setRouteFilterStatement(
        std::make_shared<RouteFilterStatement>(tStmt), std::move(logger));
    EXPECT_TRUE(ingressChanged1);
    EXPECT_FALSE(egressChanged1);

    // Step 2: Announce 1 prefix that should be allowed (kV4Prefix1)
    auto update1 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1,
        kV4Nexthop1,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);
    adjRibInQ_->fiberPush(std::move(update1));

    facebook::bgp::test::boundedBatonWait(syncBaton[1], "syncBaton[1]");

    /*
     * Step 3: Announce 3 prefixes, 2 of them will be rejected by route filter
     * policy (kV4Prefix3 and kV4Prefix4) and 1 allowed (kV4Prefix2)
     */
    auto update = createV4BgpUpdateMultipleAnnounce(prefixSet1);
    adjRibInQ_->fiberPush(std::move(update));

    facebook::bgp::test::boundedBatonWait(syncBaton[2], "syncBaton[2]");

    // Step 4: Update the policy to allow just Prefix3 and Prefix4
    tStmt = createTRouteFilterStatement(
        {kV4Prefix3, kV4Prefix4}, false /* permissive */, false /* egress */);

    auto [ingressChanged2, egressChanged2] = adjRib_->setRouteFilterStatement(
        std::make_shared<RouteFilterStatement>(tStmt), std::move(logger));
    EXPECT_TRUE(ingressChanged2);
    EXPECT_FALSE(egressChanged2);

    // Step 5: Issue policy re-evaluation
    folly::coro::blockingWait(adjRib_->processAdjRibReEvaluation(
        RibPauseResumeCause::ROUTE_FILTER_POLICY_UPDATE));
  });

  fm_->addTask([&] {
    syncBaton[0].post();

    /*
     * -------------Step 1-2 verifications------------------
     * Wait for and verify RibInAnnouncements for the initially allowed
     * prefixes
     */
    auto ribUpdate1 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribUpdate1));
    auto& announcement1 = std::get<RibInAnnouncement>(ribUpdate1);
    EXPECT_EQ(1, announcement1.pfxPathIds.size());
    EXPECT_EQ(kV4Prefix1, std::get<0>(announcement1.pfxPathIds[0]));

    // Verify no more messages are generated right now
    WITH_RETRIES_N(5, { EXPECT_TRUE(ribInQ_.empty()); });

    // Verify adjrib entries are correct for initially allowed prefixes
    auto adjRibEntry1 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry1);
    EXPECT_NE(nullptr, adjRibEntry1->getPreIn());
    EXPECT_NE(nullptr, adjRibEntry1->getPostAttr());
    EXPECT_TRUE(adjRibEntry1->getPostInPolicy());
    EXPECT_NE(nullptr, adjRibEntry1->getPostInPolicy());

    syncBaton[1].post();

    /*
     * -------------Step 3 verifications------------------
     * Wait for and verify RibInAnnouncements for the
     * allowed prefix (kV4Prefix2)
     */
    auto ribUpdate2 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribUpdate2));
    auto& announcement2 = std::get<RibInAnnouncement>(ribUpdate2);
    EXPECT_EQ(1, announcement2.pfxPathIds.size());
    EXPECT_EQ(kV4Prefix2, std::get<0>(announcement2.pfxPathIds[0]));

    auto adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
    EXPECT_NE(nullptr, adjRibEntry2);
    EXPECT_NE(nullptr, adjRibEntry2->getPreIn());
    EXPECT_NE(nullptr, adjRibEntry2->getPostAttr());
    EXPECT_TRUE(adjRibEntry2->getPostInPolicy());
    EXPECT_NE(nullptr, adjRibEntry2->getPostInPolicy());

    WITH_RETRIES_N(5, {
      // Wait until all 4 prefixes are processed (2 allowed + 2 blocked)
      EXPECT_EVENTUALLY_EQ(4, adjRib_->getStats().getPreInPrefixCount());
    });

    // Now verify the blocked prefixes exist with correct status
    auto adjRibEntry3 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix3);
    EXPECT_NE(nullptr, adjRibEntry3);
    EXPECT_NE(nullptr, adjRibEntry3->getPreIn());
    EXPECT_EQ(nullptr, adjRibEntry3->getPostAttr()); // Blocked
    EXPECT_TRUE(adjRibEntry3->getPostInPolicy());
    EXPECT_EQ(
        "Denied by Route Filter Policy", *adjRibEntry3->getPostInPolicy());

    auto adjRibEntry4 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix4);
    EXPECT_NE(nullptr, adjRibEntry4);
    EXPECT_NE(nullptr, adjRibEntry4->getPreIn());
    EXPECT_EQ(nullptr, adjRibEntry4->getPostAttr()); // Blocked
    EXPECT_TRUE(adjRibEntry4->getPostInPolicy());
    EXPECT_EQ(
        "Denied by Route Filter Policy", *adjRibEntry4->getPostInPolicy());

    /*
     * Verify stats - should be 2 postIn (only allowed prefixes) but 4 preIn
     * (all prefixes)
     */
    EXPECT_EQ(4, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(2, adjRib_->getStats().getPostInPrefixCount());

    syncBaton[2].post();

    /*
     *  -------------Step 4-5 verifications------------------
     * Verify RibInWithdrawal is received for Prefix1 and Prefix2 in
     * batch
     */
    auto ribUpdateBatch1 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(ribUpdateBatch1));
    auto& batch1 = std::get<RibInWithdrawal>(ribUpdateBatch1);
    EXPECT_EQ(2, batch1.pfxPathIds.size());

    // Check that both kV4Prefix1 and kV4Prefix2 are withdrawn
    std::set<folly::CIDRNetwork> withdrawnPrefixes;
    for (const auto& pfxPathId : batch1.pfxPathIds) {
      withdrawnPrefixes.insert(std::get<0>(pfxPathId));
    }
    EXPECT_EQ(1, withdrawnPrefixes.count(kV4Prefix1));
    EXPECT_EQ(1, withdrawnPrefixes.count(kV4Prefix2));

    /*
     * Verify adjrib entries are correctly updated for previously allowed
     * prefixes
     */
    adjRibEntry1 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry1);
    EXPECT_NE(nullptr, adjRibEntry1->getPreIn());
    EXPECT_EQ(nullptr, adjRibEntry1->getPostAttr()); // Now blocked
    EXPECT_TRUE(adjRibEntry1->getPostInPolicy());
    EXPECT_EQ(
        "Denied by Route Filter Policy", *adjRibEntry1->getPostInPolicy());

    adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
    EXPECT_NE(nullptr, adjRibEntry2);
    EXPECT_NE(nullptr, adjRibEntry2->getPreIn());
    EXPECT_EQ(nullptr, adjRibEntry2->getPostAttr()); // Now blocked
    EXPECT_TRUE(adjRibEntry2->getPostInPolicy());
    EXPECT_EQ(
        "Denied by Route Filter Policy", *adjRibEntry2->getPostInPolicy());

    /*
     * Verify RibInAnnouncement is received for Prefix3 and Prefix4 in
     * batch
     */
    auto ribUpdateBatch2 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribUpdateBatch2));
    auto& batch2 = std::get<RibInAnnouncement>(ribUpdateBatch2);
    EXPECT_EQ(2, batch2.pfxPathIds.size());

    // Check that both kV4Prefix3 and kV4Prefix4 are announced
    std::set<folly::CIDRNetwork> announcedPrefixes;
    for (const auto& pfxPathId : batch2.pfxPathIds) {
      announcedPrefixes.insert(std::get<0>(pfxPathId));
    }
    EXPECT_EQ(1, announcedPrefixes.count(kV4Prefix3));
    EXPECT_EQ(1, announcedPrefixes.count(kV4Prefix4));

    // Verify adjrib entries are correctly updated for newly allowed prefixes
    adjRibEntry3 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix3);
    EXPECT_NE(nullptr, adjRibEntry3);
    EXPECT_NE(nullptr, adjRibEntry3->getPreIn());
    EXPECT_NE(nullptr, adjRibEntry3->getPostAttr()); // Now allowed
    EXPECT_TRUE(adjRibEntry3->getPostInPolicy());
    EXPECT_NE(nullptr, adjRibEntry3->getPostInPolicy());

    adjRibEntry4 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix4);
    EXPECT_NE(nullptr, adjRibEntry4);
    EXPECT_NE(nullptr, adjRibEntry4->getPreIn());
    EXPECT_NE(nullptr, adjRibEntry4->getPostAttr()); // Now allowed
    EXPECT_TRUE(adjRibEntry4->getPostInPolicy());
    EXPECT_NE(nullptr, adjRibEntry4->getPostInPolicy());

    /*
     * Verify final stats - still 4 preIn prefixes, but now 2 different postIn
     * prefixes
     */
    EXPECT_EQ(4, adjRib_->getStats().getPreInPrefixCount());
    EXPECT_EQ(2, adjRib_->getStats().getPostInPrefixCount());

    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

/*
 * VerifyRouteFilterPolicyReEvaluationWithSessionDownNoGr
 *
 * This test verifies that route filter policy re-evaluation correctly handles
 * scenarios where the BGP session is down without graceful restart, ensuring
 * re-evaluation becomes a no-op since all routes are purged.
 *
 * Test flow:
 * 1. Create and set route filter statement to allow 2 prefixes (kV4Prefix1,
 * kV4Prefix2)
 * 2. Announce 2 prefixes and verify RibInQ announcements and adjRibEntries
 * 3. Session stop with graceful restart flag false (no GR)
 * 4. Update route filter statement to deny all prefixes (empty policy)
 * 5. Issue policy re-evaluation
 * 6. Verify that re-evaluation is a no-op since all adjRibEntries are already
 * purged when session goes down without GR
 */
TEST_F(
    AdjRibInboundFixture,
    VerifyRouteFilterPolicyReEvaluationWithSessionDownNoGr) {
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setupAcceptAllPolicy(policyName);

  setupAdjRib(policyManager, policyName);
  adjRib_->markStateEstablished();
  EXPECT_TRUE(adjRib_->isStateEstablished());

  auto mockScuba = std::make_shared<MockScubaData>();
  auto logger = std::make_unique<ScubaRouteFilterLogger>(
      "rsw001", "rsw.*", "fsw001", mockScuba);

  std::array<folly::fibers::Baton, 3> syncBaton;

  fm_->addTask([&] {
    facebook::bgp::test::boundedBatonWait(syncBaton[0], "syncBaton[0]");

    // Step 1: Set route filter statement to allow specific prefixes 1 and 2
    auto tStmt = createTRouteFilterStatement(
        {kV4Prefix1, kV4Prefix2}, false /* permissive */, false /* egress */);
    auto [ingressChanged1, egressChanged1] = adjRib_->setRouteFilterStatement(
        std::make_shared<RouteFilterStatement>(tStmt), std::move(logger));
    EXPECT_TRUE(ingressChanged1);
    EXPECT_FALSE(egressChanged1);

    /*
     * Step 2: Announce 2 prefixes that should be allowed (kV4Prefix1 and
     * kV4Prefix2)
     */
    auto update1 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1,
        kV4Nexthop1,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);
    adjRibInQ_->fiberPush(std::move(update1));

    auto update2 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix2,
        kV4Nexthop2,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);
    adjRibInQ_->fiberPush(std::move(update2));

    facebook::bgp::test::boundedBatonWait(syncBaton[1], "syncBaton[1]");

    /*
     * Step 3: Update the policy to deny all prefixes (empty list in blocking
     * mode)
     */
    tStmt = createTRouteFilterStatement(
        {}, false /* permissive */, false /* egress */);

    auto [ingressChanged2, egressChanged2] = adjRib_->setRouteFilterStatement(
        std::make_shared<RouteFilterStatement>(tStmt), std::move(logger));
    EXPECT_TRUE(ingressChanged2);
    EXPECT_FALSE(egressChanged2);

    // Step 4: terminate the session with GR = false
    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});

    facebook::bgp::test::boundedBatonWait(syncBaton[2], "syncBaton[2]");

    /*
     * Step 5: Issue policy re-evaluation after session termination is complete
     * This should be a no-op since the session is terminated
     */
    folly::coro::blockingWait(adjRib_->processAdjRibReEvaluation(
        RibPauseResumeCause::ROUTE_FILTER_POLICY_UPDATE));
  });

  fm_->addTask([&] {
    syncBaton[0].post();

    /*
     * -------------Step 1-2 verifications------------------
     * Verify RibInAnnouncements are received for the allowed prefixes
     */
    auto ribUpdate1 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribUpdate1));
    auto& announcement1 = std::get<RibInAnnouncement>(ribUpdate1);
    EXPECT_EQ(1, announcement1.pfxPathIds.size());
    // Verify the announced prefix is correct
    EXPECT_EQ(kV4Prefix1, std::get<0>(announcement1.pfxPathIds[0]));

    auto ribUpdate2 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribUpdate2));
    auto& announcement2 = std::get<RibInAnnouncement>(ribUpdate2);
    EXPECT_EQ(1, announcement2.pfxPathIds.size());
    EXPECT_EQ(kV4Prefix2, std::get<0>(announcement2.pfxPathIds[0]));

    // Verify no more messages are generated
    WITH_RETRIES_N(5, { EXPECT_TRUE(ribInQ_.empty()); });

    // Verify adjrib entries are correct
    auto adjRibEntry1 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry1);
    EXPECT_NE(nullptr, adjRibEntry1->getPreIn());
    EXPECT_NE(nullptr, adjRibEntry1->getPostAttr());
    EXPECT_TRUE(adjRibEntry1->getPostInPolicy());
    EXPECT_NE(nullptr, adjRibEntry1->getPostInPolicy());

    auto adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
    EXPECT_NE(nullptr, adjRibEntry2);
    EXPECT_NE(nullptr, adjRibEntry2->getPreIn());
    EXPECT_NE(nullptr, adjRibEntry2->getPostAttr());
    EXPECT_TRUE(adjRibEntry2->getPostInPolicy());
    EXPECT_NE(nullptr, adjRibEntry2->getPostInPolicy());

    // Verify stats
    EXPECT_EQ(2, adjRib_->getStats().getPostInPrefixCount());

    syncBaton[1].post();

    // -------------Step 3-4 verifications------------------
    auto ribUpdateAfterGr =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(ribUpdateAfterGr));
    auto& batch = std::get<RibInWithdrawal>(ribUpdateAfterGr);
    EXPECT_EQ(2, batch.pfxPathIds.size());

    // Wait for session stop processing to complete
    WITH_RETRIES_N(5, {
      // Verify adjrib entries are purged after session down (no GR)
      adjRibEntry1 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
      EXPECT_EVENTUALLY_EQ(nullptr, adjRibEntry1);

      adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
      EXPECT_EVENTUALLY_EQ(nullptr, adjRibEntry2);
    });

    // Signal that session termination is complete
    syncBaton[2].post();

    /*
     * -------------Step 5 verifications------------------
     * Verify no more messages are generated from policy re-eval(should be a
     * no-op)
     */
    WITH_RETRIES_N(5, { EXPECT_TRUE(ribInQ_.empty()); });
  });

  evb_.loop();
}

/*
 * VerifyRouteFilterPolicyReEvaluationWithGR
 *
 * This test verifies that route filter policy re-evaluation works correctly
 * during graceful restart scenarios.
 *
 * Test flow:
 * 1. Create and set route filter statement to allow 3 prefixes (kV4Prefix1,
 * kV4Prefix2, kV4Prefix3)
 * 2. Announce 3 prefixes and verify RibInQ announcements and adjRibEntries
 * 3. Session stop with graceful restart flag true
 * 4. Update route filter statement to deny all prefixes (empty policy)
 * 5. Issue policy re-evaluation
 * 6. Verify adjRibEntries in stale are updated with proper postInAttrs, no
 * entry is moved to adjRibIn tree
 * 7. Establish session and re-announce 3 prefixes
 * 8. Verify adjRibInStale is cleared and correct "denied" adjRibInEntry is
 * updated in adjRibIn
 */
TEST_F(AdjRibInboundFixture, VerifyRouteFilterPolicyReEvaluationWithGR) {
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setupAcceptAllPolicy(policyName);

  setupAdjRib(
      kLongGrRestartTime,
      kLongGrRestartTime,
      true,
      kLocalAs1,
      kLocalAs1,
      kRemoteAs1,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      policyManager,
      kIngressPolicyName);

  adjRib_->markStateEstablished();
  EXPECT_TRUE(adjRib_->isStateEstablished());
  auto mockScuba = std::make_shared<MockScubaData>();
  auto logger = std::make_unique<ScubaRouteFilterLogger>(
      "rsw001", "rsw.*", "fsw001", mockScuba);

  fm_->addTask([&] {
    // Step 1: Create and set route filter statement with 3 prefixes
    auto tStmt = createTRouteFilterStatement(
        {kV4Prefix1, kV4Prefix2, kV4Prefix3},
        false /* permissive */,
        false /* egress */);

    auto [ingressChanged1, egressChanged1] = adjRib_->setRouteFilterStatement(
        std::make_shared<RouteFilterStatement>(tStmt), std::move(logger));
    EXPECT_TRUE(ingressChanged1);
    EXPECT_FALSE(egressChanged1);

    // Step 2: Announce 3 prefixes
    auto update1 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1,
        kV4Nexthop1,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);
    adjRibInQ_->fiberPush(std::move(update1));

    auto update2 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix2,
        kV4Nexthop2,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);
    adjRibInQ_->fiberPush(std::move(update2));

    auto update3 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix3,
        kV4Nexthop3,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);

    adjRibInQ_->fiberPush(std::move(update3));

    // Verify RibInQ announcements are received
    for (int i = 0; i < 3; i++) {
      auto ribInMsg =
          facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribInMsg));
      auto announcement = std::get<RibInAnnouncement>(ribInMsg);
      EXPECT_EQ(1, announcement.pfxPathIds.size());

      // Verify it's one of the expected prefixes
      auto prefix = std::get<0>(announcement.pfxPathIds[0]);
      EXPECT_TRUE(
          prefix == kV4Prefix1 || prefix == kV4Prefix2 || prefix == kV4Prefix3);
    }

    // Verify adjRibEntries are correct
    auto adjRibEntry1 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry1);
    EXPECT_NE(nullptr, adjRibEntry1->getPreIn());
    EXPECT_NE(nullptr, adjRibEntry1->getPostAttr());
    auto adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
    EXPECT_NE(nullptr, adjRibEntry2);
    EXPECT_NE(nullptr, adjRibEntry2->getPreIn());
    EXPECT_NE(nullptr, adjRibEntry2->getPostAttr());

    auto adjRibEntry3 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix3);
    EXPECT_NE(nullptr, adjRibEntry3);
    EXPECT_NE(nullptr, adjRibEntry3->getPreIn());
    EXPECT_NE(nullptr, adjRibEntry3->getPostAttr());

    // Step 3: Session stop with graceful restart flag true
    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{true}});
    fiberSleepFor(10ms);

    // Verify routes are moved to stale
    EXPECT_EQ(3, adjRib_->getRibInStaleTreePaths());

    // Step 4: Update route filter statement to deny all prefixes (empty policy)
    auto emptyStmt = createTRouteFilterStatement(
        {}, false /* permissive */, false /* egress */);

    logger = std::make_unique<ScubaRouteFilterLogger>(
        "rsw001", "rsw.*", "fsw001", mockScuba);

    auto [ingressChanged2, egressChanged2] = adjRib_->setRouteFilterStatement(
        std::make_shared<RouteFilterStatement>(emptyStmt), std::move(logger));
    EXPECT_TRUE(ingressChanged2);
    EXPECT_FALSE(egressChanged2);

    // Step 5: Issue policy re-evaluation
    folly::coro::blockingWait(adjRib_->processAdjRibReEvaluation(
        RibPauseResumeCause::ROUTE_FILTER_POLICY_UPDATE));

    // Step 6: Verify RibInQ withdrawals for the 3 prefixes are generated
    auto ribInMsg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(ribInMsg));
    auto withdrawal = std::get<RibInWithdrawal>(ribInMsg);
    EXPECT_EQ(3, withdrawal.pfxPathIds.size());

    // Verify no entry in adjRibIn tree after policy re-evaluation
    adjRibEntry1 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_EQ(nullptr, adjRibEntry1);
    adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
    EXPECT_EQ(nullptr, adjRibEntry2);

    adjRibEntry3 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix3);
    EXPECT_EQ(nullptr, adjRibEntry3);

    // Verify stale entries have correct postInAttrs (denied by policy)
    auto stalePfxMatch1 =
        adjRib_->adjRibInStale_.exactMatch(kV4Prefix1.first, kV4Prefix1.second);
    ASSERT_TRUE(adjRib_->stalePathExist(stalePfxMatch1));
    auto staleEntry1 = stalePfxMatch1.value().find(kDefaultPathID);
    ASSERT_TRUE(staleEntry1 != stalePfxMatch1.value().end());
    EXPECT_NE(nullptr, staleEntry1->second->getPreIn());
    EXPECT_EQ(nullptr, staleEntry1->second->getPostAttr());
    EXPECT_TRUE(staleEntry1->second->getPostInPolicy());
    EXPECT_EQ(
        "Denied by Route Filter Policy",
        *staleEntry1->second->getPostInPolicy());

    auto stalePfxMatch2 =
        adjRib_->adjRibInStale_.exactMatch(kV4Prefix2.first, kV4Prefix2.second);
    ASSERT_TRUE(adjRib_->stalePathExist(stalePfxMatch2));
    auto staleEntry2 = stalePfxMatch2.value().find(kDefaultPathID);
    ASSERT_TRUE(staleEntry2 != stalePfxMatch2.value().end());
    EXPECT_NE(nullptr, staleEntry2->second->getPreIn());
    EXPECT_EQ(nullptr, staleEntry2->second->getPostAttr());
    EXPECT_TRUE(staleEntry2->second->getPostInPolicy());
    EXPECT_EQ(
        "Denied by Route Filter Policy",
        *staleEntry2->second->getPostInPolicy());

    auto stalePfxMatch3 =
        adjRib_->adjRibInStale_.exactMatch(kV4Prefix3.first, kV4Prefix3.second);
    ASSERT_TRUE(adjRib_->stalePathExist(stalePfxMatch3));
    auto staleEntry3 = stalePfxMatch3.value().find(kDefaultPathID);
    ASSERT_TRUE(staleEntry3 != stalePfxMatch3.value().end());
    EXPECT_NE(nullptr, staleEntry3->second->getPreIn());
    EXPECT_EQ(nullptr, staleEntry3->second->getPostAttr());
    EXPECT_TRUE(staleEntry3->second->getPostInPolicy());
    EXPECT_EQ(
        "Denied by Route Filter Policy",
        *staleEntry3->second->getPostInPolicy());

    // Step 7: Establish session and re-announce 3 prefixes
    reEstablishSession(kLongGrRestartTime);
    fiberSleepFor(10ms);
    auto reUpdate1 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1,
        kV4Nexthop1,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);

    adjRibInQ_->fiberPush(std::move(reUpdate1));
    auto reUpdate2 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix2,
        kV4Nexthop2,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);

    adjRibInQ_->fiberPush(std::move(reUpdate2));
    auto reUpdate3 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix3,
        kV4Nexthop3,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);

    adjRibInQ_->fiberPush(std::move(reUpdate3));
    // Wait for updates to be processed
    fiberSleepFor(10ms);

    // Expect RibInQ to be empty
    WITH_RETRIES_N(5, { EXPECT_TRUE(ribInQ_.empty()); });

    /*
     * Step 8 verification: After session re-establishment and re-announcement
     * Verify adjRibInStale is cleared
     */
    EXPECT_EQ(0, adjRib_->getRibInStaleTreePaths());

    /*
     * Verify adjRibInEntry with denied by Ingress Route Filter policy is
     * updated in adjRibIn
     */
    adjRibEntry1 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry1);
    EXPECT_NE(nullptr, adjRibEntry1->getPreIn());
    EXPECT_EQ(nullptr, adjRibEntry1->getPostAttr());
    EXPECT_TRUE(adjRibEntry1->getPostInPolicy());
    EXPECT_EQ(
        "Denied by Route Filter Policy", *adjRibEntry1->getPostInPolicy());

    adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
    EXPECT_NE(nullptr, adjRibEntry2);
    EXPECT_NE(nullptr, adjRibEntry2->getPreIn());
    EXPECT_EQ(nullptr, adjRibEntry2->getPostAttr());
    EXPECT_TRUE(adjRibEntry2->getPostInPolicy());
    EXPECT_EQ(
        "Denied by Route Filter Policy", *adjRibEntry2->getPostInPolicy());

    adjRibEntry3 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix3);
    EXPECT_NE(nullptr, adjRibEntry3);
    EXPECT_NE(nullptr, adjRibEntry3->getPreIn());
    EXPECT_EQ(nullptr, adjRibEntry3->getPostAttr());
    EXPECT_TRUE(adjRibEntry3->getPostInPolicy());
    EXPECT_EQ(
        "Denied by Route Filter Policy", *adjRibEntry3->getPostInPolicy());
    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

/*
 * VerifyRouteFilterPolicyReEvaluationAfterPeerReconnectsWithGR
 *
 * This test verifies that route filter policy re-evaluation works correctly
 * after a peer reconnects with graceful restart, ensuring that both stale
 * routes and active routes are properly evaluated when policy changes occur.
 *
 * Test flow:
 * 1. Create and set route filter statement to allow 3 prefixes (kV4Prefix1,
 * kV4Prefix2, kV4Prefix3)
 * 2. Announce 3 prefixes and verify RibInQ announcements and adjRibEntries
 * 3. Session stop with graceful restart flag true
 * 4. Establish session and re-announce 1 prefix
 * 5. Update route filter statement to deny all prefixes (empty policy)
 * 6. Issue policy re-evaluation
 * 7. Verify adjRibEntries in stale and adjRibInTree are evaluated with
 * withdrawals sent to RibInQ
 */
TEST_F(
    AdjRibInboundFixture,
    VerifyRouteFilterPolicyReEvaluationAfterPeerReconnectsWithGR) {
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setupAcceptAllPolicy(policyName);

  setupAdjRib(
      kLongGrRestartTime,
      kLongGrRestartTime,
      true,
      kLocalAs1,
      kLocalAs1,
      kRemoteAs1,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      policyManager,
      kIngressPolicyName);

  adjRib_->markStateEstablished();
  EXPECT_TRUE(adjRib_->isStateEstablished());

  auto mockScuba = std::make_shared<MockScubaData>();
  auto logger = std::make_unique<ScubaRouteFilterLogger>(
      "rsw001", "rsw.*", "fsw001", mockScuba);

  fm_->addTask([&] {
    // Step 1: Create and set route filter statement with 3 prefixes
    auto tStmt = createTRouteFilterStatement(
        {kV4Prefix1, kV4Prefix2, kV4Prefix3},
        false /* permissive */,
        false /* egress */);
    auto [ingressChanged1, egressChanged1] = adjRib_->setRouteFilterStatement(
        std::make_shared<RouteFilterStatement>(tStmt), std::move(logger));
    EXPECT_TRUE(ingressChanged1);
    EXPECT_FALSE(egressChanged1);

    // Step 2: Announce 3 prefixes
    auto update1 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1,
        kV4Nexthop1,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);
    adjRibInQ_->fiberPush(std::move(update1));

    auto update2 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix2,
        kV4Nexthop2,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);
    adjRibInQ_->fiberPush(std::move(update2));

    auto update3 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix3,
        kV4Nexthop3,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);
    adjRibInQ_->fiberPush(std::move(update3));

    // Verify RibInQ announcements are received
    for (int i = 0; i < 3; i++) {
      auto ribInMsg =
          facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(ribInMsg));
      auto announcement = std::get<RibInAnnouncement>(ribInMsg);
      EXPECT_EQ(1, announcement.pfxPathIds.size());

      // Verify it's one of the expected prefixes
      auto prefix = std::get<0>(announcement.pfxPathIds[0]);
      EXPECT_TRUE(
          prefix == kV4Prefix1 || prefix == kV4Prefix2 || prefix == kV4Prefix3);
    }

    // Verify adjRibEntries are correct
    auto adjRibEntry1 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry1);
    EXPECT_NE(nullptr, adjRibEntry1->getPreIn());
    EXPECT_NE(nullptr, adjRibEntry1->getPostAttr());

    auto adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
    EXPECT_NE(nullptr, adjRibEntry2);
    EXPECT_NE(nullptr, adjRibEntry2->getPreIn());
    EXPECT_NE(nullptr, adjRibEntry2->getPostAttr());

    auto adjRibEntry3 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix3);
    EXPECT_NE(nullptr, adjRibEntry3);
    EXPECT_NE(nullptr, adjRibEntry3->getPreIn());
    EXPECT_NE(nullptr, adjRibEntry3->getPostAttr());

    // Step 3: Session stop with graceful restart flag true
    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{true}});
    fiberSleepFor(10ms);

    // Wait for session termination using ONLY state polling
    WITH_RETRIES_N(
        5, { EXPECT_EVENTUALLY_EQ(3, adjRib_->getRibInStaleTreePaths()); });

    // Step 4: Establish session and re-announce 1 prefix (kV4Prefix1)
    reEstablishSession(kLongGrRestartTime);
    fiberSleepFor(10ms);

    auto reUpdate1 = createV4BgpUpdateSingleAnnounce(
        kV4Prefix1,
        kV4Nexthop1,
        kMed,
        kOriginatorId,
        BgpAttrOrigin::BGP_ORIGIN_IGP);

    adjRibInQ_->fiberPush(std::move(reUpdate1));
    fiberSleepFor(10ms);

    // Verify kV4Prefix1 is back in adjRibIn, others remain in stale
    adjRibEntry1 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry1);
    EXPECT_NE(nullptr, adjRibEntry1->getPreIn());
    EXPECT_NE(nullptr, adjRibEntry1->getPostAttr());

    // Verify kV4Prefix2 and kV4Prefix3 are still in stale (not re-announced)
    EXPECT_EQ(2, adjRib_->getRibInStaleTreePaths());

    // Step 5: Update route filter statement to deny all prefixes (empty policy)
    auto emptyStmt = createTRouteFilterStatement(
        {}, false /* permissive */, false /* egress */);

    logger = std::make_unique<ScubaRouteFilterLogger>(
        "rsw001", "rsw.*", "fsw001", mockScuba);

    auto [ingressChanged2, egressChanged2] = adjRib_->setRouteFilterStatement(
        std::make_shared<RouteFilterStatement>(emptyStmt), std::move(logger));
    EXPECT_TRUE(ingressChanged2);
    EXPECT_FALSE(egressChanged2);

    // Step 6: Issue policy re-evaluation
    folly::coro::blockingWait(adjRib_->processAdjRibReEvaluation(
        RibPauseResumeCause::ROUTE_FILTER_POLICY_UPDATE));

    /*
     * Step 7: Verify withdrawals are sent for all prefixes
     * Expect withdrawal for kV4Prefix1 (from adjRibIn)
     */

    auto ribInMsg1 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(ribInMsg1));
    auto withdrawal1 = std::get<RibInWithdrawal>(ribInMsg1);
    EXPECT_EQ(1, withdrawal1.pfxPathIds.size());
    EXPECT_EQ(kV4Prefix1, std::get<0>(withdrawal1.pfxPathIds[0]));

    // Expect withdrawal for stale prefixes (kV4Prefix2, kV4Prefix3)
    auto ribInMsg2 =
        facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(ribInMsg2));
    auto withdrawal2 = std::get<RibInWithdrawal>(ribInMsg2);
    EXPECT_EQ(2, withdrawal2.pfxPathIds.size());

    // Verify announced prefix has no change
    adjRibEntry1 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry1);
    EXPECT_NE(nullptr, adjRibEntry1->getPreIn());
    EXPECT_EQ(nullptr, adjRibEntry1->getPostAttr());
    EXPECT_EQ(
        "Denied by Route Filter Policy", *adjRibEntry1->getPostInPolicy());

    /*
     * Verify no entry in adjRibIn tree after policy re-evaluation for stale
     * prefixes
     */
    adjRibEntry2 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2);
    EXPECT_EQ(nullptr, adjRibEntry2);
    adjRibEntry3 = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix3);
    EXPECT_EQ(nullptr, adjRibEntry3);

    // Verify stale entries count remains correct (entries are still in stale)
    EXPECT_EQ(2, adjRib_->getRibInStaleTreePaths());

    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

/******************************************************************************
 *      END   -   Route Filter Policy Test                                    *
 ******************************************************************************/

/******************************************************************************
 *      START   -   updateIngressEgressPolicyNames Test                       *
 ******************************************************************************/

/**
 * Test updateIngressEgressPolicyNames method with empty policy map
 */
TEST_F(AdjRibInboundFixture, UpdateIngressEgressPolicyNames_EmptyMap) {
  setupAdjRib(kLongGrRestartTime, kLongGrRestartTime);

  fm_->addTask([&] {
    // Test with empty policy map
    folly::F14FastMap<
        facebook::bgp::bgp_policy::DIRECTION,
        std::optional<std::string>>
        emptyMap;
    auto [ingressChanged, egressChanged] =
        adjRib_->updateIngressEgressPolicyNames(emptyMap);

    EXPECT_FALSE(ingressChanged);
    EXPECT_FALSE(egressChanged);

    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

/**
 * Test updateIngressEgressPolicyNames method with only ingress policy
 */
TEST_F(AdjRibInboundFixture, UpdateIngressEgressPolicyNames_IngressOnly) {
  setupAdjRib(kLongGrRestartTime, kLongGrRestartTime);

  fm_->addTask([&] {
    // Test with only ingress policy
    folly::F14FastMap<
        facebook::bgp::bgp_policy::DIRECTION,
        std::optional<std::string>>
        policyMap;
    policyMap[facebook::bgp::bgp_policy::DIRECTION::IN] = "ingress_policy_v1";

    auto [ingressChanged, egressChanged] =
        adjRib_->updateIngressEgressPolicyNames(policyMap);

    EXPECT_TRUE(ingressChanged);
    EXPECT_FALSE(egressChanged);

    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

/**
 * Test updateIngressEgressPolicyNames method with only egress policy
 */
TEST_F(AdjRibInboundFixture, UpdateIngressEgressPolicyNames_EgressOnly) {
  setupAdjRib(kLongGrRestartTime, kLongGrRestartTime);

  fm_->addTask([&] {
    // Test with only egress policy
    folly::F14FastMap<
        facebook::bgp::bgp_policy::DIRECTION,
        std::optional<std::string>>
        policyMap;
    policyMap[facebook::bgp::bgp_policy::DIRECTION::OUT] = "egress_policy_v1";

    auto [ingressChanged, egressChanged] =
        adjRib_->updateIngressEgressPolicyNames(policyMap);

    EXPECT_FALSE(ingressChanged);
    EXPECT_TRUE(egressChanged);

    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

/**
 * Test updateIngressEgressPolicyNames method with both ingress and egress
 * policies
 */
TEST_F(AdjRibInboundFixture, UpdateIngressEgressPolicyNames_BothPolicies) {
  setupAdjRib(kLongGrRestartTime, kLongGrRestartTime);

  fm_->addTask([&] {
    // Test with both ingress and egress policies
    folly::F14FastMap<
        facebook::bgp::bgp_policy::DIRECTION,
        std::optional<std::string>>
        policyMap;
    policyMap[facebook::bgp::bgp_policy::DIRECTION::IN] = "ingress_policy_v1";
    policyMap[facebook::bgp::bgp_policy::DIRECTION::OUT] = "egress_policy_v1";

    auto [ingressChanged, egressChanged] =
        adjRib_->updateIngressEgressPolicyNames(policyMap);

    EXPECT_TRUE(ingressChanged);
    EXPECT_TRUE(egressChanged);

    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

/**
 * Test updateIngressEgressPolicyNames method with policy updates (same policy
 * names)
 */
TEST_F(AdjRibInboundFixture, UpdateIngressEgressPolicyNames_NoChangesSameName) {
  setupAdjRib(kLongGrRestartTime, kLongGrRestartTime);

  fm_->addTask([&] {
    // Set initial policies
    folly::F14FastMap<
        facebook::bgp::bgp_policy::DIRECTION,
        std::optional<std::string>>
        policyMap1;
    policyMap1[facebook::bgp::bgp_policy::DIRECTION::IN] = "ingress_policy_v1";
    policyMap1[facebook::bgp::bgp_policy::DIRECTION::OUT] = "egress_policy_v1";

    auto [ingressChanged1, egressChanged1] =
        adjRib_->updateIngressEgressPolicyNames(policyMap1);

    EXPECT_TRUE(ingressChanged1);
    EXPECT_TRUE(egressChanged1);

    // Update with same policy names - should not detect changes
    folly::F14FastMap<
        facebook::bgp::bgp_policy::DIRECTION,
        std::optional<std::string>>
        policyMap2;
    policyMap2[facebook::bgp::bgp_policy::DIRECTION::IN] = "ingress_policy_v1";
    policyMap2[facebook::bgp::bgp_policy::DIRECTION::OUT] = "egress_policy_v1";

    auto [ingressChanged2, egressChanged2] =
        adjRib_->updateIngressEgressPolicyNames(policyMap2);

    EXPECT_FALSE(ingressChanged2);
    EXPECT_FALSE(egressChanged2);

    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

/**
 * Test updateIngressEgressPolicyNames method with policy updates (different
 * policy names)
 */
TEST_F(
    AdjRibInboundFixture,
    UpdateIngressEgressPolicyNames_ChangedPolicyNames) {
  setupAdjRib(kLongGrRestartTime, kLongGrRestartTime);

  fm_->addTask([&] {
    // Set initial policies
    folly::F14FastMap<
        facebook::bgp::bgp_policy::DIRECTION,
        std::optional<std::string>>
        policyMap1;
    policyMap1[facebook::bgp::bgp_policy::DIRECTION::IN] = "ingress_policy_v1";
    policyMap1[facebook::bgp::bgp_policy::DIRECTION::OUT] = "egress_policy_v1";

    auto [ingressChanged1, egressChanged1] =
        adjRib_->updateIngressEgressPolicyNames(policyMap1);

    EXPECT_TRUE(ingressChanged1);
    EXPECT_TRUE(egressChanged1);

    // Update with different policy names - should detect changes
    folly::F14FastMap<
        facebook::bgp::bgp_policy::DIRECTION,
        std::optional<std::string>>
        policyMap2;
    policyMap2[facebook::bgp::bgp_policy::DIRECTION::IN] = "ingress_policy_v2";
    policyMap2[facebook::bgp::bgp_policy::DIRECTION::OUT] = "egress_policy_v2";

    auto [ingressChanged2, egressChanged2] =
        adjRib_->updateIngressEgressPolicyNames(policyMap2);

    EXPECT_TRUE(ingressChanged2);
    EXPECT_TRUE(egressChanged2);

    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

/**
 * Test updateIngressEgressPolicyNames method with partial policy updates
 */
TEST_F(AdjRibInboundFixture, UpdateIngressEgressPolicyNames_PartialUpdate) {
  setupAdjRib(kLongGrRestartTime, kLongGrRestartTime);

  fm_->addTask([&] {
    // Set initial policies
    folly::F14FastMap<
        facebook::bgp::bgp_policy::DIRECTION,
        std::optional<std::string>>
        policyMap1;
    policyMap1[facebook::bgp::bgp_policy::DIRECTION::IN] = "ingress_policy_v1";
    policyMap1[facebook::bgp::bgp_policy::DIRECTION::OUT] = "egress_policy_v1";

    auto [ingressChanged1, egressChanged1] =
        adjRib_->updateIngressEgressPolicyNames(policyMap1);

    EXPECT_TRUE(ingressChanged1);
    EXPECT_TRUE(egressChanged1);

    // Update only ingress policy - should detect only ingress change
    folly::F14FastMap<
        facebook::bgp::bgp_policy::DIRECTION,
        std::optional<std::string>>
        policyMap2;
    policyMap2[facebook::bgp::bgp_policy::DIRECTION::IN] = "ingress_policy_v2";
    policyMap2[facebook::bgp::bgp_policy::DIRECTION::OUT] = "egress_policy_v1";

    auto [ingressChanged2, egressChanged2] =
        adjRib_->updateIngressEgressPolicyNames(policyMap2);

    EXPECT_TRUE(ingressChanged2);
    EXPECT_FALSE(egressChanged2);

    // Update only egress policy - should detect only egress change
    folly::F14FastMap<
        facebook::bgp::bgp_policy::DIRECTION,
        std::optional<std::string>>
        policyMap3;
    policyMap3[facebook::bgp::bgp_policy::DIRECTION::IN] = "ingress_policy_v2";
    policyMap3[facebook::bgp::bgp_policy::DIRECTION::OUT] = "egress_policy_v2";

    auto [ingressChanged3, egressChanged3] =
        adjRib_->updateIngressEgressPolicyNames(policyMap3);

    EXPECT_FALSE(ingressChanged3);
    EXPECT_TRUE(egressChanged3);

    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

/**
 * Test updateIngressEgressPolicyNames method with unknown direction (should be
 * ignored)
 */
TEST_F(AdjRibInboundFixture, UpdateIngressEgressPolicyNames_UnknownDirection) {
  setupAdjRib(kLongGrRestartTime, kLongGrRestartTime);

  fm_->addTask([&] {
    // Test with unknown direction (should be ignored by default case)
    folly::F14FastMap<
        facebook::bgp::bgp_policy::DIRECTION,
        std::optional<std::string>>
        policyMap;
    policyMap[facebook::bgp::bgp_policy::DIRECTION::IN] = "ingress_policy_v1";
    policyMap[facebook::bgp::bgp_policy::DIRECTION::OUT] = "egress_policy_v1";
    /*
     * Add a valid direction but then modify the map to simulate unknown
     * direction handling
     */

    auto [ingressChanged, egressChanged] =
        adjRib_->updateIngressEgressPolicyNames(policyMap);

    // Should still detect the valid directions
    EXPECT_TRUE(ingressChanged);
    EXPECT_TRUE(egressChanged);

    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

/**
 * Test updateIngressEgressPolicyNames method consistency between calls
 */
TEST_F(AdjRibInboundFixture, UpdateIngressEgressPolicyNames_CallConsistency) {
  setupAdjRib(kLongGrRestartTime, kLongGrRestartTime);

  fm_->addTask([&] {
    folly::F14FastMap<
        facebook::bgp::bgp_policy::DIRECTION,
        std::optional<std::string>>
        policyMap;
    policyMap[facebook::bgp::bgp_policy::DIRECTION::IN] = "test_policy";

    // First call should detect change
    auto [ingressChanged1, egressChanged1] =
        adjRib_->updateIngressEgressPolicyNames(policyMap);
    EXPECT_TRUE(ingressChanged1);
    EXPECT_FALSE(egressChanged1);

    // Second call with same policy should not detect change
    auto [ingressChanged2, egressChanged2] =
        adjRib_->updateIngressEgressPolicyNames(policyMap);
    EXPECT_FALSE(ingressChanged2);
    EXPECT_FALSE(egressChanged2);

    // Third call with same policy should still not detect change
    auto [ingressChanged3, egressChanged3] =
        adjRib_->updateIngressEgressPolicyNames(policyMap);
    EXPECT_FALSE(ingressChanged3);
    EXPECT_FALSE(egressChanged3);

    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
  });

  evb_.loop();
}

/******************************************************************************
 *      END   -   updateIngressEgressPolicyNames Test                         *
 ******************************************************************************/

/**
 * End-to-end test that adjRibInQueue_ handles back pressure correctly when the
 * queue is full. This test simulates a realistic scenario where:
 * 1. BGP updates are pushed to adjRibInQueue_ faster than AdjRib can process
 * 2. adjRibInQueue_ fills up to exactly maxIngressQueueSize capacity
 * 3. Producer blocks on fiberPush when queue is full (back pressure)
 * 4. Verify producer is actually blocked (additional messages not consumed)
 * 5. Once processing loop starts draining, processing continues normally
 */
TEST_F(AdjRibInboundFixture, AdjRibInQueueBackpressureTest) {
  size_t maxIngressQueueSize = 10;
  adjRibInQ_ = std::make_shared<AdjRib::AdjRibInQueueT>(maxIngressQueueSize);

  // Setup AdjRib but don't start the session/processing loop yet
  setupAdjRib(
      kLongGrRestartTime,
      kLongGrRestartTime,
      false /* callSessionEstablished */);

  fm_->addTask([&] {
    // Create updates to fill the queue
    auto update = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);

    /*
     * Send enough messages to fill the queue to capacity
     * Note: We haven't started the AdjRib processing loop yet, so messages
     * will accumulate in adjRibInQueue_
     */
    fm_->addTask([&] {
      for (size_t i = 0; i < maxIngressQueueSize + 2; ++i) {
        adjRibInQ_->fiberPush(update);
      }
    });

    /*
     * Wait for queue to fill to capacity by polling the queue size
     * Use fiberSleepFor(0ms) to allow event loop to run
     */
    while (adjRibInQ_->size() < maxIngressQueueSize) {
      folly::fibers::yield();
    }

    // Verify adjRibInQueue_ is exactly at capacity
    auto queueSize = adjRibInQ_->size();
    XLOGF(INFO, "adjRibInQueue_ size after filling: {}", queueSize);
    EXPECT_EQ(maxIngressQueueSize, queueSize);

    /*
     * Create a fiber task to try pushing additional messages
     * This should block because the queue is full
     */
    std::atomic<bool> pushBlocked{true};
    fm_->addTask([&] {
      /*
       * Now try to send additional messages and verify producer is blocked
       * (messages won't be consumed because queue is full)
       */
      const size_t additionalMessages = 3;
      for (size_t i = 0; i < additionalMessages; ++i) {
        // This should block on the first push
        adjRibInQ_->fiberPush(update);
      }
      pushBlocked = false;
    });

    /*
     * Yield multiple times and verify queue size stays at capacity
     * The push fiber should be blocked and not complete
     */
    for (int i = 0; i < 10; ++i) {
      folly::fibers::yield();
      queueSize = adjRibInQ_->size();
      EXPECT_EQ(maxIngressQueueSize, queueSize);
      // Verify the push fiber hasn't completed yet
      EXPECT_TRUE(pushBlocked.load());
    }

    XLOGF(
        INFO,
        "adjRibInQueue_ size after attempting more pushes: {}",
        queueSize);
    EXPECT_EQ(maxIngressQueueSize, queueSize);

    // Now establish the session which will start the processing loop
    XLOG(INFO, "Starting AdjRib processing to drain queue");
    establishSession(kLongGrRestartTime);

    // Wait for queue to start draining (unblocks the producer)
    while (adjRibInQ_->size() >= maxIngressQueueSize) {
      /*
       * Use fiberSleepFor(0ms) instead of folly::fibers::yield() to get
       * coroutines scheduled and run
       */
      fiberSleepFor(0ms);
    }

    XLOG(INFO, "Queue started draining, producer should now be unblocked");

    // Wait for the blocked push to complete
    while (pushBlocked.load()) {
      fiberSleepFor(0ms);
    }

    // Wait for all messages to be processed
    while (adjRibInQ_->size() > 0) {
      fiberSleepFor(0ms);
    }

    XLOG(INFO, "Queue fully drained");
    EXPECT_EQ(0, adjRibInQ_->size());

    // Verify the push fiber completed after queue was drained
    EXPECT_FALSE(pushBlocked.load());

    // Clean up
    terminateAdjRib();
  });

  evb_.loop();
}

/******************************************************************************
 *      START   -   Policy Re-evaluation Synchronization Test                *
 ******************************************************************************/

/**
 * Test to verify that once processAdjRibReEvaluationForPolicyChange starts,
 * it completes fully (even across co_await suspension points) before
 * processPeerMessageLoop can process any peer messages that modify trees.
 */
TEST_F(AdjRibInboundFixture, VerifyPolicyReEvaluationSynchronization) {
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setupAcceptAllPolicy(policyName);

  setupAdjRib(policyManager, policyName);
  adjRib_->markStateEstablished();
  adjRib_->enableDynamicPolicyEvaluation_ = true;
  EXPECT_TRUE(adjRib_->isStateEstablished());

  fm_->addTask([&] {
    /*
     * Step 1: Send initial prefix announcements and wait for them to be
     * processed
     */
    auto update1 = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
    auto update2 = createV4BgpUpdateSingleAnnounce(kV4Prefix2, kV4Nexthop1);
    auto update3 = createV4BgpUpdateSingleAnnounce(kV4Prefix3, kV4Nexthop1);

    adjRibInQ_->fiberPush(std::move(update1));
    adjRibInQ_->fiberPush(std::move(update2));
    adjRibInQ_->fiberPush(std::move(update3));

    // Drain initial announcements from ribInQ
    for (int i = 0; i < 3; i++) {
      auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));
    }

    // Verify all prefixes exist in AdjRibIn
    EXPECT_NE(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1));
    EXPECT_NE(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix2));
    EXPECT_NE(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix3));

    /*
     * Step 2: Trigger policy re-evaluation in background task
     * We need to test that while this is running, peer messages are blocked
     */
    auto reEvalTask = fm_->addTaskFuture([this] {
      return adjRib_->processAdjRibReEvaluation(
          RibPauseResumeCause::ROUTING_POLICY_UPDATE);
    });

    /*
     * Step 3: Give re-evaluation a moment to start and acquire the semaphore
     * Use fiberSleepFor to allow the re-evaluation task to start without
     * blocking fibers
     */
    fiberSleepFor(50ms);

    /*
     * Step 4: Queue a new peer update while re-evaluation is in progress
     * The processing of this update should be blocked by the semaphore
     */
    auto update4 = createV4BgpUpdateSingleAnnounce(kV4Prefix4, kV4Nexthop1);
    adjRibInQ_->fiberPush(std::move(update4));

    // Step 5: Wait for policy re-evaluation to complete
    auto reEvalResult = folly::coro::blockingWait(std::move(reEvalTask));
    (void)reEvalResult; // Explicitly ignore the result

    // Step 6: Now the peer update should be processed
    auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));

    // Verify the new prefix exists
    EXPECT_NE(nullptr, adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix4));

    XLOG(INFO, "Policy re-evaluation completed before peer update processing");

    terminateAdjRib();
  });

  evb_.loop();
}

/**
 * Test to verify that sessionTerminated also waits for policy re-evaluation
 * to complete before modifying trees.
 */
TEST_F(AdjRibInboundFixture, VerifyPolicyReEvaluationWithSessionTermination) {
  const std::string policyName = kIngressPolicyName;
  auto policyManager = setupAcceptAllPolicy(policyName);

  setupAdjRib(policyManager, policyName);
  adjRib_->markStateEstablished();
  adjRib_->enableDynamicPolicyEvaluation_ = true;
  EXPECT_TRUE(adjRib_->isStateEstablished());

  fm_->addTask([&] {
    // Step 1: Send initial prefix announcements and wait for them
    auto update1 = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
    auto update2 = createV4BgpUpdateSingleAnnounce(kV4Prefix2, kV4Nexthop1);

    adjRibInQ_->fiberPush(std::move(update1));
    adjRibInQ_->fiberPush(std::move(update2));

    // Drain initial announcements
    for (int i = 0; i < 2; i++) {
      auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
      ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));
    }

    // Step 2: Start policy re-evaluation in background
    auto reEvalTask = fm_->addTaskFuture([this] {
      return adjRib_->processAdjRibReEvaluation(
          RibPauseResumeCause::ROUTING_POLICY_UPDATE);
    });

    /*
     * Step 3: Give re-evaluation time to start and acquire semaphore
     * Use fiberSleepFor to allow the re-evaluation task to start without
     * blocking fibers
     */
    fiberSleepFor(50ms);

    /*
     * Step 4: Try to terminate session while policy re-evaluation is running
     * This should be blocked until policy re-evaluation completes
     */
    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});

    // Step 5: Wait for policy re-evaluation to complete first
    auto reEvalResult = folly::coro::blockingWait(std::move(reEvalTask));
    (void)reEvalResult; // Explicitly ignore the result

    /*
     * Step 6: Now session termination should process and send withdrawals
     * Wait for withdrawals from session termination
     */
    auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    // Could be announcement or withdrawal depending on what re-evaluation did

    XLOG(INFO, "Session termination correctly blocked by policy re-evaluation");
  });

  evb_.loop();
}

/******************************************************************************
 *      END   -   Policy Re-evaluation Synchronization Test                  *
 ******************************************************************************/

/******************************************************************************
 *      START   -   ConsumerScope Test for processPeerMessageLoop           *
 ******************************************************************************/

/**
 * Test that processPeerMessageLoop properly manages adjRibInQueue_'s
 * open/close state via ConsumerScope.
 *
 * This test verifies the ConsumerScope RAII pattern:
 * 1. By default, adjRibInQueue_ is open
 * 2. Close the queue manually and verify pushes are dropped
 * 3. Start processPeerMessageLoop directly - it creates
 *    ConsumerScope which opens the queue
 * 4. Verify pushes work when the loop is running (queue is open)
 * 5. Cancel the async scope to end the loop
 * 6. Verify the loop has ended
 */
TEST_F(AdjRibInboundFixture, AdjRibInQueueConsumerScopeTest) {
  adjRibInQ_ = std::make_shared<AdjRib::AdjRibInQueueT>(1);

  // Setup AdjRib but don't start the session/processing loop yet
  setupAdjRib(
      kLongGrRestartTime,
      kLongGrRestartTime,
      false /* callSessionEstablished */);

  // Create async scope and baton for explicit loop control
  folly::coro::CancellableAsyncScope loopAsyncScope;
  auto terminateBaton = std::make_shared<folly::coro::Baton>();

  fm_->addTask([&] {
    // Step 1: Verify adjRibInQueue_ is open by default (pushes work)
    auto update1 = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
    adjRibInQ_->fiberPush(update1);
    EXPECT_EQ(1, adjRibInQ_->size());

    // Step 2: Close the queue manually and verify pushes are dropped
    adjRibInQ_->close();

    auto update2 = createV4BgpUpdateSingleAnnounce(kV4Prefix2, kV4Nexthop1);
    adjRibInQ_->fiberPush(update2);
    EXPECT_EQ(1, adjRibInQ_->size()); // Push was dropped

    /*
     * Step 3: Setup session state and start processPeerMessageLoop directly
     * This creates a ConsumerScope which should open the queue
     */
    adjRib_->sessionEstablished(
        kLongGrRestartTime.count(),
        adjRibInQ_,
        boundedAdjRibOutQ_,
        AfiIpv4Negotiated(true),
        AfiIpv6Negotiated(true));
    loopAsyncScope.add(
        folly::coro::co_withExecutor(
            &evb_, adjRib_->processPeerMessageLoop(terminateBaton)));

    /*
     * Step 4: Verify pushes work now that queue is open (ConsumerScope opened
     * it)
     */
    for (int i = 0; i < 10; ++i) {
      auto update = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
      adjRibInQ_->fiberPush(update);
    }

    // Wait for all messages to be processed
    while (adjRibInQ_->size() > 0) {
      fiberSleepFor(0ms);
    }

    // Step 5: Cancel the async scope to end the loop
    folly::coro::blockingWait(loopAsyncScope.cancelAndJoinAsync());

    // Queue has been closed, push would be dropped
    size_t sizeBeforePush = adjRibInQ_->size();
    for (int i = 0; i < 10; ++i) {
      auto update = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
      adjRibInQ_->fiberPush(update);
    }
    EXPECT_EQ(sizeBeforePush, adjRibInQ_->size()); // Push was dropped
  });

  evb_.loop();
}

/**
 * Test that ConsumerScope properly closes the queue even when the
 * processing loop exits via cancellation (not normal termination).
 *
 * This tests a different exit path than normal termination (BgpSessionStop):
 * - Normal test: sends BgpSessionStop -> loop processes it -> exits ->
 * ConsumerScope closes queue
 * - This test: async scope cancelled -> loop exits via cancellation ->
 * ConsumerScope closes queue -> AdjRib destroyed
 *
 * This verifies the RAII guarantee works for the cancellation code path.
 */
TEST_F(AdjRibInboundFixture, AdjRibInQueueConsumerScopeExceptionPathTest) {
  adjRibInQ_ = std::make_shared<AdjRib::AdjRibInQueueT>(1);

  // Setup AdjRib but don't start the session/processing loop yet
  setupAdjRib(
      kLongGrRestartTime,
      kLongGrRestartTime,
      false /* callSessionEstablished */);

  // Create async scope and baton for explicit loop control
  folly::coro::CancellableAsyncScope loopAsyncScope;
  auto terminateBaton = std::make_shared<folly::coro::Baton>();

  fm_->addTask([&] {
    /*
     * Step 1: Setup session state and start processPeerMessageLoop directly
     * This creates a ConsumerScope which opens the queue
     */
    adjRib_->sessionEstablished(
        kLongGrRestartTime.count(),
        adjRibInQ_,
        boundedAdjRibOutQ_,
        AfiIpv4Negotiated(true),
        AfiIpv6Negotiated(true));
    loopAsyncScope.add(
        folly::coro::co_withExecutor(
            &evb_, adjRib_->processPeerMessageLoop(terminateBaton)));

    // Step 2: Verify queue is open (ConsumerScope opened it)
    auto update1 = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
    adjRibInQ_->fiberPush(update1);
    EXPECT_EQ(1, adjRibInQ_->size()); // Push succeeded

    // Wait for message to be processed
    while (adjRibInQ_->size() > 0) {
      fiberSleepFor(0ms);
    }

    /*
     * Step 3: Cancel the loop WITHOUT sending termination signal
     * (BgpSessionStop). The loop exits via cancellation check
     * (isCancellationRequested), NOT via processing BgpSessionStop message.
     * ConsumerScope destructor is called and closes the queue.
     *
     * Cancel before destroying AdjRib to match production lifetime:
     * AdjRib::~AdjRib() calls blockingWait(asyncScope_->cancelAndJoinAsync())
     * before member destruction.
     */
    folly::coro::blockingWait(loopAsyncScope.cancelAndJoinAsync());
    adjRib_.reset();

    /*
     * Step 4: Verify queue is closed by ConsumerScope destructor
     * (RAII guarantee - queue closed even on cancellation exit path)
     */
    size_t sizeBeforePush = adjRibInQ_->size();
    for (int i = 0; i < 10; ++i) {
      auto update = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
      adjRibInQ_->fiberPush(update);
    }
    EXPECT_EQ(sizeBeforePush, adjRibInQ_->size()); // Pushes were dropped
  });

  evb_.loop();
}

/******************************************************************************
 *      END   -   ConsumerScope Test for processPeerMessageLoop             *
 ******************************************************************************/
/*
 * Test that ingress policy can add both transitive and non-transitive
 * link bandwidth extended communities, and both should pass through
 * on the ingress side.
 */
TEST_F(AdjRibInboundFixture, IngressPolicyLinkBandwidthPropagationTest) {
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

  // Create policy action to add these extended communities
  auto extCommAction = createBgpPolicyExtCommunityAction(
      bgp_policy::BgpAttrChangeActionType::EXT_COMMUNITY_LIST_ADD,
      {extComm100GTransitive, extComm10GNonTransitive});

  // Create policy term with no match (matches all routes)
  auto term =
      createBgpPolicyTerm("add-lbw-communities", "", {}, {extCommAction});

  // Create policy manager
  const std::string policyName = kIngressPolicyName;
  const auto& policyConfig = createBgpPolicies(policyName, {term});
  auto policyManager = std::make_shared<PolicyManager>(
      policyConfig, createTestBgpGlobalConfig());

  // Setup AdjRib with the policy
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      true, // callSessionEstablished
      kLocalAs1,
      kLocalAs1,
      kRemoteAs2,
      true,
      true,
      policyManager,
      policyName);

  fm_->addTask([&] {
    auto update = createV4BgpUpdateSingleAnnounce(kV4Prefix1, kV4Nexthop1);
    adjRibInQ_->fiberPush(std::move(update));
  });

  fm_->addTask([&] {
    auto msg = facebook::bgp::test::boundedBlockingPop(
        ribInQ_, "ribInQ_"); // add route

    // Verify post-policy attributes have both extended communities
    auto adjRibEntry = adjRib_->getRibEntry(/*ingress=*/true, kV4Prefix1);
    EXPECT_NE(nullptr, adjRibEntry);
    auto postAttrs = adjRibEntry->getPostAttr();
    EXPECT_NE(nullptr, postAttrs);

    /*
     * Both extended communities should be present (ingress allows both)
     * The policy adds 2 communities to the existing 3 from the update = 5 total
     */
    auto extCommunities = postAttrs->getExtCommunities();
    EXPECT_FALSE(extCommunities.nullOrEmpty());
    EXPECT_EQ(5, extCommunities->size());

    // Verify the message was sent to RibIn
    ASSERT_TRUE(std::holds_alternative<RibInAnnouncement>(msg));
    auto ann = std::get<RibInAnnouncement>(msg);
    PrefixPathIds prefixSet{{kV4Prefix1, kDefaultPathID}};
    EXPECT_EQ(prefixSet, ann.pfxPathIds);

    terminateAdjRib();
  });

  evb_.loop();
}

// collectStaleRoutes returns nullopt when adjRibInStale_ is empty.
TEST_F(AdjRibInboundFixture, CollectStaleRoutes_EmptyReturnsNullopt) {
  setupAdjRib();
  fm_->addTask([&] {
    EXPECT_EQ(0, adjRib_->adjRibInStale_.size());
    auto withdrawal = adjRib_->collectStaleRoutes(/*isGrHelperMode=*/true);
    EXPECT_FALSE(withdrawal.has_value());
    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * collectStaleRoutes returns a withdrawal containing the stale prefixes and
 * clears adjRibInStale_ as a side effect.
 */
TEST_F(
    AdjRibInboundFixture,
    CollectStaleRoutes_ReturnsWithdrawalAndClearsState) {
  setupAdjRib();
  fm_->addTask([&] {
    adjRib_->remoteAs_ = kAsSeqAsNum;
    // Manually populate adjRibInStale_ with one entry.
    auto dummyPathId = 5;
    folly::F14ValueMap<uint32_t, std::unique_ptr<AdjRibEntry>> staleEntry;
    auto entry = std::make_unique<AdjRibEntry>(dummyPathId);
    entry->setPostAttr(std::make_shared<BgpPath>());
    staleEntry.emplace(dummyPathId, std::move(entry));
    adjRib_->adjRibInStale_.insert(
        kV4Prefix1.first, kV4Prefix1.second, std::move(staleEntry));
    adjRib_->adjRibInStaleSize_++;
    RibStats::incrAdjRibInStaleCount();

    auto withdrawal = adjRib_->collectStaleRoutes(/*isGrHelperMode=*/true);
    ASSERT_TRUE(withdrawal.has_value());
    EXPECT_EQ(kAsSeqAsNum, withdrawal->peer.asn);
    ASSERT_EQ(1, withdrawal->pfxPathIds.size());
    EXPECT_EQ(kV4Prefix1, std::get<0>(withdrawal->pfxPathIds.front()));
    EXPECT_EQ(dummyPathId, std::get<1>(withdrawal->pfxPathIds.front()));

    // Stale state cleared as a side effect.
    EXPECT_EQ(0, adjRib_->adjRibInStale_.size());
    EXPECT_EQ(0, adjRib_->adjRibInStaleSize_);

    // Calling again returns nullopt — nothing left to collect.
    EXPECT_FALSE(adjRib_->collectStaleRoutes(true).has_value());

    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * AdjRib::stop() drains pendingRibInPushes_ — verifies the load-bearing
 * invariant that prevents UAF on this->ribInQ_ when AdjRib is destroyed
 * while pushes from timer callbacks are still suspended on back-pressure.
 */
TEST_F(AdjRibInboundFixture, StopDrainsPendingRibInPushes) {
  setupAdjRib();
  fm_->addTask([&] {
    /*
     * Seed the vector with two ready SemiFutures — drain should consume
     * them and leave the vector empty.
     */
    adjRib_->pendingRibInPushes_.push_back(folly::makeSemiFuture(folly::unit));
    adjRib_->pendingRibInPushes_.push_back(folly::makeSemiFuture(folly::unit));
    EXPECT_EQ(2, adjRib_->pendingRibInPushes_.size());

    folly::coro::blockingWait(adjRib_->stop());

    EXPECT_EQ(0, adjRib_->pendingRibInPushes_.size());

    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * stop() must block on in-flight pushes — not just clear the vector. Seeds
 * pendingRibInPushes_ with an UNRESOLVED Promise/SemiFuture pair so
 * collectAllRange has something real to wait on. Without the drain in
 * stop(), the AdjRib could be destroyed while a push is still suspended on
 * ribInQ_'s nonCancellablePush(), causing UAF on resume.
 */
TEST_F(AdjRibInboundFixture, StopBlocksUntilPendingPushCompletes) {
  setupAdjRib();
  fm_->addTask([&] {
    folly::Promise<folly::Unit> promise;
    adjRib_->pendingRibInPushes_.push_back(promise.getSemiFuture());

    /*
     * Launch stop() without awaiting it — it should suspend on
     * collectAllRange waiting for our promise.
     */
    auto stopFuture =
        co_withExecutor(
            &evb_, folly::coro::co_invoke([this]() -> folly::coro::Task<void> {
              co_await adjRib_->stop();
            }))
            .start();

    /*
     * The baton is never posted, so try_wait_for always times out —
     * but while the fiber sleeps the evb runs the stop() task to its first
     * co_await. 100ms is many orders of magnitude more than the few
     * microseconds stop() needs to reach collectAllRange.
     */
    folly::fibers::Baton parkBaton;
    parkBaton.try_wait_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(stopFuture.isReady())
        << "stop() returned before pendingRibInPushes_ entry was resolved — "
           "drain is not actually waiting on the in-flight push";

    // Resolve the promise.
    promise.setValue();
    folly::coro::blockingWait(std::move(stopFuture));
    EXPECT_EQ(0, adjRib_->pendingRibInPushes_.size());

    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * schedulePendingRibInPush trims completed SemiFutures before adding a new
 * entry
 */
TEST_F(AdjRibInboundFixture, SchedulePendingRibInPushTrimsReady) {
  setupAdjRib();
  fm_->addTask([&] {
    /*
     * Seed three completed entries to simulate prior GR cycles whose
     * pushes have landed.
     */
    adjRib_->pendingRibInPushes_.push_back(folly::makeSemiFuture(folly::unit));
    adjRib_->pendingRibInPushes_.push_back(folly::makeSemiFuture(folly::unit));
    adjRib_->pendingRibInPushes_.push_back(folly::makeSemiFuture(folly::unit));
    EXPECT_EQ(3, adjRib_->pendingRibInPushes_.size());

    /*
     * Build a withdrawal and schedule it. Trim should drop the 3 ready
     * entries before the new one is added.
     */
    PrefixPathIds pfxPathIds{{kV4Prefix1, 1}};
    RibInWithdrawal w(
        fboss::kDefaultVrf,
        TinyPeerInfo(
            kPeerAddr1,
            kRemoteAs1,
            kPeerRouterId1,
            BgpSessionType::EBGP,
            /*isRrClient=*/false,
            /*isRedistributePeer=*/false,
            std::nullopt,
            ""),
        pfxPathIds);
    adjRib_->schedulePendingRibInPush(std::move(w));

    // After trim + add, only the new in-flight entry remains.
    EXPECT_EQ(1, adjRib_->pendingRibInPushes_.size());

    /*
     * Drain the new push so stop()'s drain doesn't hang on the real
     * ribInQ_ — pop the message that was just pushed.
     */
    facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    folly::coro::blockingWait(adjRib_->stop());
    EXPECT_EQ(0, adjRib_->pendingRibInPushes_.size());

    terminateAdjRib();
  });
  evb_.loop();
}

/*
 * the prepared withdrawal lands in ribInQ_ as a RibInWithdrawal message with
 * the expected payload.
 */
TEST_F(AdjRibInboundFixture, PushStaleWithdrawalPushesToRibInQ) {
  setupAdjRib();
  fm_->addTask([&] {
    PrefixPathIds pfxPathIds{{kV4Prefix1, 7}};
    RibInWithdrawal w(
        fboss::kDefaultVrf,
        TinyPeerInfo(
            kPeerAddr1,
            kRemoteAs1,
            kPeerRouterId1,
            BgpSessionType::EBGP,
            /*isRrClient=*/false,
            /*isRedistributePeer=*/false,
            std::nullopt,
            ""),
        pfxPathIds);

    folly::coro::blockingWait(adjRib_->pushStaleWithdrawal(std::move(w)));

    auto msg = facebook::bgp::test::boundedBlockingPop(ribInQ_, "ribInQ_");
    ASSERT_TRUE(std::holds_alternative<RibInWithdrawal>(msg));
    const auto& got = std::get<RibInWithdrawal>(msg);
    ASSERT_EQ(1, got.pfxPathIds.size());
    EXPECT_EQ(kV4Prefix1, std::get<0>(got.pfxPathIds.front()));
    EXPECT_EQ(7, std::get<1>(got.pfxPathIds.front()));

    terminateAdjRib();
  });
  evb_.loop();
}

/******************************************************************************
 *      START   -   Route Refresh (RFC 2918 / RFC 7313) tests.                *
 *                                                                            *
 * processPeerRouteRefresh() gates the incoming Route Refresh on:             *
 *   1. RR (RFC 2918, cap 2) negotiated; ERR (RFC 7313) alone is not enough.  *
 *   2. SAFI must be SAFI_UNICAST (only unicast supported in v1).             *
 *   3. AFI must match a negotiated AFI (v4 or v6).                           *
 *                                                                            *
 * When the gate passes, a RouteRefreshReceived ObservableMessage is pushed   *
 * to fromAdjRibQ_ for PeerManager to act on. When the gate fails, the        *
 * Route Refresh is silently dropped.                                         *
 *                                                                            *
 * Each negative test pushes a BgpRouteRefresh that should be dropped, then   *
 * an EoR that should be observed. If the popped message is the EoR (and not  *
 * RouteRefreshReceived), the gate dropped the RR as expected.                *
 ******************************************************************************/

namespace {
nettools::bgplib::BgpRouteRefresh makeRouteRefresh(
    nettools::bgplib::BgpUpdateAfi afi =
        nettools::bgplib::BgpUpdateAfi::AFI_IPv4,
    nettools::bgplib::BgpUpdateSafi safi =
        nettools::bgplib::BgpUpdateSafi::SAFI_UNICAST) {
  nettools::bgplib::BgpRouteRefresh rr;
  rr.afi() = afi;
  rr.safi() = safi;
  rr.msgSubType() =
      nettools::bgplib::BgpRouteRefreshMessageSubtype::ROUTE_REFRESH_REQUEST;
  return rr;
}
} // namespace

// Gate drops Route Refresh when neither RR nor ERR was negotiated.
TEST_F(AdjRibInboundFixture, ProcessPeerRouteRefresh_NoCapability_Ignored) {
  setupAdjRib(
      kLongGrRestartTime,
      kLongGrRestartTime,
      false /* callSessionEstablished — we'll call ours below */);
  // Both RR and ERR negotiated = false (defaulted)
  establishSession(
      std::nullopt,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      /*remoteAs=*/std::nullopt,
      EnhancedRouteRefreshNegotiated(false),
      RouteRefreshNegotiated(false));

  fm_->addTask([&] {
    adjRibInQ_->fiberPush(makeRouteRefresh());
    /*
     * Push v4+v6 EoRs that together trigger an EoR ObservableMessage on
     * fromAdjRibQ_ (proves the loop processed the RR before the EoR).
     */
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv4));
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv6));
  });

  fm_->addTask([&] {
    auto msg = folly::coro::blockingWait(fromAdjRibQ_.pop());
    EXPECT_FALSE(
        std::holds_alternative<AdjRib::RouteRefreshReceived>(msg.message))
        << "Route Refresh should have been dropped: gate not negotiated";
    EXPECT_TRUE(std::holds_alternative<AdjRib::EoR>(msg.message));
    EXPECT_EQ(fboss::kDefaultVrf, msg.vrfId);
    terminateAdjRib();
  });

  evb_.loop();
}

// Happy path: RR (cap 2) negotiated, RouteRefreshReceived flows through.
TEST_F(
    AdjRibInboundFixture,
    ProcessPeerRouteRefresh_RrNegotiated_PushesToQueue) {
  setupAdjRib(kLongGrRestartTime, kLongGrRestartTime, false);
  establishSession(
      std::nullopt,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      /*remoteAs=*/std::nullopt,
      EnhancedRouteRefreshNegotiated(false),
      RouteRefreshNegotiated(true));

  fm_->addTask([&] { adjRibInQ_->fiberPush(makeRouteRefresh()); });

  fm_->addTask([&] {
    auto msg = folly::coro::blockingWait(fromAdjRibQ_.pop());
    EXPECT_TRUE(
        std::holds_alternative<AdjRib::RouteRefreshReceived>(msg.message));
    terminateAdjRib();
  });

  evb_.loop();
}

// RFC 2918 requires receivers to ignore the reserved octet.
TEST_F(
    AdjRibInboundFixture,
    ProcessPeerRouteRefresh_NonzeroReservedOctet_PushesToQueue) {
  setupAdjRib(kLongGrRestartTime, kLongGrRestartTime, false);
  establishSession(
      std::nullopt,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      /*remoteAs=*/std::nullopt,
      EnhancedRouteRefreshNegotiated(false),
      RouteRefreshNegotiated(true));

  auto routeRefresh = makeRouteRefresh();
  routeRefresh.msgSubType() = static_cast<BgpRouteRefreshMessageSubtype>(0xff);
  fm_->addTask([&] { adjRibInQ_->fiberPush(std::move(routeRefresh)); });

  fm_->addTask([&] {
    auto msg = folly::coro::blockingWait(fromAdjRibQ_.pop());
    EXPECT_TRUE(
        std::holds_alternative<AdjRib::RouteRefreshReceived>(msg.message));
    terminateAdjRib();
  });

  evb_.loop();
}

TEST_F(AdjRibInboundFixture, ProcessPeerRouteRefresh_EnhancedMarkers_Ignored) {
  setupAdjRib(kLongGrRestartTime, kLongGrRestartTime, false);
  establishSession(
      std::nullopt,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      /*remoteAs=*/std::nullopt,
      EnhancedRouteRefreshNegotiated(true),
      RouteRefreshNegotiated(true));

  auto beginningOfRefresh = makeRouteRefresh();
  beginningOfRefresh.msgSubType() =
      BgpRouteRefreshMessageSubtype::BEGINNING_OF_ROUTE_REFRESH;
  auto endOfRefresh = makeRouteRefresh();
  endOfRefresh.msgSubType() =
      BgpRouteRefreshMessageSubtype::END_OF_ROUTE_REFRESH;
  fm_->addTask([&] {
    adjRibInQ_->fiberPush(std::move(beginningOfRefresh));
    adjRibInQ_->fiberPush(std::move(endOfRefresh));
    /*
     * The two per-AFI EoRs produce one aggregate AdjRib::EoR. Seeing it as
     * the first output proves neither preceding marker triggered a re-dump.
     */
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv4));
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv6));
  });

  fm_->addTask([&] {
    auto msg = folly::coro::blockingWait(fromAdjRibQ_.pop());
    EXPECT_FALSE(
        std::holds_alternative<AdjRib::RouteRefreshReceived>(msg.message));
    EXPECT_TRUE(std::holds_alternative<AdjRib::EoR>(msg.message));
    terminateAdjRib();
  });

  evb_.loop();
}

/*
 * ERR (cap 70) alone is NOT sufficient: we do not implement RFC 7313, so an
 * ERR-only peer would expect a BoRR/EoRR-delimited re-dump and get a bare
 * RFC 2918 one instead. Drop until Phase 2 adds the markers.
 */
TEST_F(
    AdjRibInboundFixture,
    ProcessPeerRouteRefresh_ErrOnlyNegotiated_Ignored) {
  setupAdjRib(kLongGrRestartTime, kLongGrRestartTime, false);
  establishSession(
      std::nullopt,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      /*remoteAs=*/std::nullopt,
      EnhancedRouteRefreshNegotiated(true),
      RouteRefreshNegotiated(false));

  fm_->addTask([&] {
    adjRibInQ_->fiberPush(makeRouteRefresh());
    /*
     * EoRs behind the RR give the pop below something to observe, proving the
     * loop ran past the RR rather than merely being slow to produce it.
     */
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv4));
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv6));
  });

  fm_->addTask([&] {
    auto msg = folly::coro::blockingWait(fromAdjRibQ_.pop());
    EXPECT_FALSE(
        std::holds_alternative<AdjRib::RouteRefreshReceived>(msg.message))
        << "ERR-only session must not trigger a re-dump: RR cap 2 absent";
    EXPECT_TRUE(std::holds_alternative<AdjRib::EoR>(msg.message));
    terminateAdjRib();
  });

  evb_.loop();
}

// AFI validation: RR negotiated but the requested AFI was not negotiated.
TEST_F(AdjRibInboundFixture, ProcessPeerRouteRefresh_UnnegotiatedAfi_Ignored) {
  setupAdjRib(kLongGrRestartTime, kLongGrRestartTime, false);
  // v4 AFI NOT negotiated; RR cap is.
  establishSession(
      std::nullopt,
      AfiIpv4Negotiated(false),
      AfiIpv6Negotiated(true),
      /*remoteAs=*/std::nullopt,
      EnhancedRouteRefreshNegotiated(false),
      RouteRefreshNegotiated(true));

  fm_->addTask([&] {
    // Request RR for v4, but v4 wasn't negotiated → drop.
    adjRibInQ_->fiberPush(
        makeRouteRefresh(nettools::bgplib::BgpUpdateAfi::AFI_IPv4));
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv4));
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv6));
  });

  fm_->addTask([&] {
    auto msg = folly::coro::blockingWait(fromAdjRibQ_.pop());
    EXPECT_FALSE(
        std::holds_alternative<AdjRib::RouteRefreshReceived>(msg.message))
        << "Route Refresh for unnegotiated AFI should have been dropped";
    EXPECT_TRUE(std::holds_alternative<AdjRib::EoR>(msg.message));
    terminateAdjRib();
  });

  evb_.loop();
}

// SAFI validation: only SAFI_UNICAST supported in v1.
TEST_F(AdjRibInboundFixture, ProcessPeerRouteRefresh_NonUnicastSafi_Ignored) {
  setupAdjRib(kLongGrRestartTime, kLongGrRestartTime, false);
  establishSession(
      std::nullopt,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      /*remoteAs=*/std::nullopt,
      EnhancedRouteRefreshNegotiated(false),
      RouteRefreshNegotiated(true));

  fm_->addTask([&] {
    // SAFI_LABELED_UNICAST is not supported in v1 → drop.
    adjRibInQ_->fiberPush(makeRouteRefresh(
        nettools::bgplib::BgpUpdateAfi::AFI_IPv4,
        nettools::bgplib::BgpUpdateSafi::SAFI_LABELED_UNICAST));
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv4));
    adjRibInQ_->fiberPush(buildEndOfRib(BgpUpdateAfi::AFI_IPv6));
  });

  fm_->addTask([&] {
    auto msg = folly::coro::blockingWait(fromAdjRibQ_.pop());
    EXPECT_FALSE(
        std::holds_alternative<AdjRib::RouteRefreshReceived>(msg.message))
        << "Route Refresh with non-UNICAST SAFI should have been dropped";
    EXPECT_TRUE(std::holds_alternative<AdjRib::EoR>(msg.message));
    terminateAdjRib();
  });

  evb_.loop();
}

} // namespace facebook::bgp
