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

#include <limits>

#define PeerManager_TEST_FRIENDS                                                \
  FRIEND_TEST(PeerManagerTestFixture, UpdateShadowRibEntryUtil);                \
  FRIEND_TEST(PeerManagerTestFixture, ShadowRibEntryBestpathTest);              \
  FRIEND_TEST(PeerManagerTestFixture, GetShadowRibEntriesCanonical);            \
  FRIEND_TEST(PeerManagerTestFixture, ShadowRibEntryEmptyAttrTest);             \
  FRIEND_TEST(PeerManagerTestFixture, ShadowRibEntryMixpathTest);               \
  FRIEND_TEST(PeerManagerTestFixture, ShadowRibEntryMultipathTest);             \
  FRIEND_TEST(PeerManagerTestFixture, ShadowRibEntryMultiUpdateTest);           \
  FRIEND_TEST(PeerManagerTestFixture, RibDumpReqNegativeTest);                  \
  FRIEND_TEST(PeerManagerTestFixture, RibDumpReqPositiveTest);                  \
  FRIEND_TEST(PeerManagerTestFixture, ReplicateRibMessageInitialadjRibsTest);   \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture, RibInitialAnnouncementStartTest);      \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture, HandleBufferedRibDumpReqsTest);        \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      SessionTerminatedWithPendingRibDumpReqTest);                              \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      SessionTerminatedWithoutPendingRibDumpReqTest);                           \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      ScheduleRibDumpForAdjRib_CoalescesConcurrentRequests);                    \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      ScheduleRibDumpForAdjRib_PreservesRouteRefreshMetadata);                  \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      ScheduleRibDumpForAdjRib_DeferredRequestPreservesMetadata);               \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      ScheduleRibDumpForAdjRib_OrdinaryDumpRemainsAllAfi);                      \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      ScheduleRibDumpForDetachedBlocked_RegistersMissingConsumer);              \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      UpdateGroupRouteRefreshCoalescesSameAfiAndRetainsCrossAfi);               \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      UpdateGroupRouteRefreshResumesFromReadyToJoin);                           \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      QueuedRouteRefreshPreservesReadyToJoinState);                             \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      DetachedInitDumpCompletionRetriesPendingRouteRefreshWithoutEoR);          \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      PendingRouteRefreshRetainedWithoutUpdateGroup);                           \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      CancelRibDumpForAdjRib_DiscardsDeferredRouteRefresh);                     \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      ScheduleRibDumpForAdjRib_RescheduleAfterCancelDumpsOnce);                 \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      ScheduleRibDumpForAdjRib_BufferedThenScheduledDumpsOnce);                 \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      CancelRibDumpForAdjRib_RemovesBufferedRequest);                           \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      MaybeBufferRibDumpReqTest_SessionUpBeforeRibInitialAnnouncementStart);    \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      MaybeBufferRibDumpReqTest_SessionUpAfterRibInitialAnnouncementDone);      \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      MaybeBufferRibDumpReqTest_SessionUpDuringRibInitialAnnouncement);         \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      MaybeBufferRibDumpForDetachedPeerTest_SessionUpAfterRibAnnouncementDone); \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      MaybeBufferRibDumpForDetachedPeerTest_SessionUpDuringRibAnnouncement);    \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      HandleBufferedRibDumpsForDetachedPeersTest);                              \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      PeerSessionEstablishedBeforeRibInitialAnnouncementTest);                  \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      PeerSessionEstablishedDuringRibInitialAnnouncementTest);                  \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      PeerSessionFlapsDuringRibInitialAnnouncementTest);                        \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      PeerSessionEstablishedAfterRibInitialAnnouncementTest);                   \
  FRIEND_TEST(                                                                  \
      PeerManagerTestFixture, SelectiveMultipathNotificationBestpathTest);      \
  FRIEND_TEST(                                                                  \
      PeerManagerTestFixture, SelectiveMultipathNotificationAddPathTest);       \
  FRIEND_TEST(PeerManagerTestFixture, SelectiveMultipathNotificationMixTest);   \
  FRIEND_TEST(PeerManagerTestFixture, MaxRibVersionCountsPrefixChunks);         \
  FRIEND_TEST(PeerManagerTestFixture, MaxRibVersionNeverMovesBackwards);        \
  FRIEND_TEST(PeerManagerTestFixture, ShadowRibEntryMultipathKeyedByPathId);    \
  FRIEND_TEST(PeerManagerTestFixture, ShadowRibEntryWithdrawalByPathId);

#define AdjRib_TEST_FRIENDS                                                     \
  FRIEND_TEST(PeerManagerTestFixture, RibDumpReqNegativeTest);                  \
  FRIEND_TEST(PeerManagerTestFixture, RibDumpReqPositiveTest);                  \
  FRIEND_TEST(PeerManagerTestFixture, ShadowRibEntryMultiUpdateTest);           \
  FRIEND_TEST(PeerManagerTestFixture, ReplicateRibMessageTest);                 \
  FRIEND_TEST(                                                                  \
      PeerManagerTestFixture, SelectiveMultipathNotificationBestpathTest);      \
  FRIEND_TEST(                                                                  \
      PeerManagerTestFixture, SelectiveMultipathNotificationAddPathTest);       \
  FRIEND_TEST(PeerManagerTestFixture, SelectiveMultipathNotificationMixTest);   \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      MaybeBufferRibDumpReqTest_SessionUpBeforeRibInitialAnnouncementStart);    \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      MaybeBufferRibDumpReqTest_SessionUpAfterRibInitialAnnouncementDone);      \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      MaybeBufferRibDumpReqTest_SessionUpDuringRibInitialAnnouncement);         \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      MaybeBufferRibDumpForDetachedPeerTest_SessionUpAfterRibAnnouncementDone); \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      MaybeBufferRibDumpForDetachedPeerTest_SessionUpDuringRibAnnouncement);    \
  FRIEND_TEST(                                                                  \
      RibInitialAnnouncementTestFixture,                                        \
      DetachedInitDumpCompletionRetriesPendingRouteRefreshWithoutEoR);

#include <folly/CancellationToken.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Sleep.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/network/AddressUtil.h"
#include "neteng/fboss/bgp/cpp/config/ConfigManager.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/BoundedWaitUtils.h"
#include "neteng/fboss/bgp/cpp/tests/PeerManagerTestUtils.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"

using ::testing::_;
using namespace facebook::nettools::bgplib;

namespace facebook::bgp {

namespace {
/*
 * Establishes a session on the AdjRib so a rib dump reaches processRibMessage
 * instead of bailing out on a missing bounded out queue.
 */
void establishSessionForRibDump(const std::shared_ptr<AdjRib>& adjRib) {
  adjRib->sessionEstablished(
      std::nullopt,
      std::make_shared<AdjRib::AdjRibInQueueT>(),
      std::make_shared<AdjRib::AdjRibOutQueueT>(
          kMaxEgressQueueSize,
          kEgressQueueHighWatermark,
          kEgressQueueLowWatermark));
  adjRib->markStateEstablished();
}
} // namespace

class RibInitialAnnouncementTestFixture : public PeerManagerTestFixture {
 public:
  void SetUp() override {
    PeerManagerTestFixture::SetUp();
    auto config = getConfig(
        true /* includeStaticPeer */,
        true /* includeDynamicShivPeer */,
        false /* includeDynamicMonitorPeer */,
        false /* includeDynamicVipInjectorPeer */,
        false /* enableStatefulHa */,
        true /* enableVipServer */,
        kDefaultEorTimeS,
        false /* enableSubscriberLimit */,
        false /* enableSwitchLimit */,
        false /* applyGoldenPrefixPolicy */,
        {} /* bgpFeatures */);
    auto configManager = std::make_shared<ConfigManager>(config);
    peerMgr_ = std::make_shared<PeerManagerBase>(
        configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

    auto versionNumber = std::make_shared<VersionNumber>(version_);
    auto mockInfo = mockInfo1_;
    // Set AFI to v4 so that AdjRibOut doesn't skip building announcements.
    mockInfo.negotiatedCapabilities.mpExtV4Unicast() = true;

    sessionInfo_ = FiberBgpPeer::getObservableSessionInfo(
        mockInfo,
        nullptr /* unbounded input queue, unused */,
        boundedAdjRibOutQ_,
        adjRibInQ_,
        versionNumber);
  }

  void refreshSessionInfo() {
    boundedAdjRibOutQ_ = std::make_shared<AdjRib::AdjRibOutQueueT>(
        kMaxEgressQueueSize,
        kEgressQueueHighWatermark,
        kEgressQueueLowWatermark);

    // Recreate sessionInfo_ with the new queue and updated version
    auto versionNumber = std::make_shared<VersionNumber>(version_);
    auto mockInfo = mockInfo1_;
    mockInfo.negotiatedCapabilities.mpExtV4Unicast() = true;

    sessionInfo_ = FiberBgpPeer::getObservableSessionInfo(
        mockInfo,
        nullptr /* unbounded input queue, unused */,
        boundedAdjRibOutQ_,
        adjRibInQ_,
        versionNumber);
  }

  void cleanUp() {
    adjRibInQ_->forcePush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    peerMgr_->stop();
  }

  /**
   * Helper utility for PeerSessionEstablished.*RibInitialAnnouncementTests
   * to check the invariant that the boundedAdjRibOutQ_ should finally contain
   * two announcements and one EoR at the time that this method is called.
   */
  folly::coro::Task<void> verifyTwoAnnouncementsWithEoR() {
    std::optional<FiberBgpPeer::InputMessageT> msg;
    int numAnnouncements = 0;
    /* Count number of announcements seen until EoR. */
    do {
      msg = co_await facebook::bgp::test::boundedPop(
          *boundedAdjRibOutQ_, "boundedAdjRibOutQ_");
      if (std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg)) {
        ++numAnnouncements;
      }
    } while (msg.has_value() && !std::holds_alternative<BgpEndOfRib>(*msg));

    EXPECT_EQ(2, numAnnouncements);
  }

  std::shared_ptr<MockAdjRib> setupUpdateGroupPeer(
      const BgpPeerId& peerId,
      const std::shared_ptr<AdjRibOutGroup>& group) {
    auto adjRib = setupMockAdjRib(
        peerMgr_->getEventBase(), peerId, AsNum(kAsn1), sessionTerminateBaton_);
    establishSessionForRibDump(adjRib);
    adjRib->setUpdateGroup(group);
    adjRib->setEnableUpdateGroup(true);
    group->registerPeer(adjRib);
    return adjRib;
  }

  std::shared_ptr<PeerManagerBase> peerMgr_;

  uint64_t version_ = 0x100;
  std::shared_ptr<FiberBgpPeer::ObservableSessionInfo> sessionInfo_;

  std::shared_ptr<AdjRib::AdjRibInQueueT> adjRibInQ_ =
      std::make_shared<AdjRib::AdjRibInQueueT>();
  std::shared_ptr<AdjRib::AdjRibOutQueueT> boundedAdjRibOutQ_ =
      std::make_shared<AdjRib::AdjRibOutQueueT>(
          kMaxEgressQueueSize,
          kEgressQueueHighWatermark,
          kEgressQueueLowWatermark);
};

/*
 * This test verifies the updateShadowRibEntryUtil() call to make sure
 * ShadowRibEntry is updated properly for the attributes except:
 *  - prefix
 *  - bestpath
 *  - multipaths
 */
TEST_F(PeerManagerTestFixture, UpdateShadowRibEntryUtil) {
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  ShadowRibEntry srEntry;
  TinyPeerInfo peer{
      folly::IPAddress("20.1.1.1"), 1, 1, BgpSessionType::EBGP, false};
  RibOutAnnouncementEntry entry{
      kV4Prefix1,
      kDefaultPathID,
      peer, /* TinyPeerInfo */
      nullptr, /* BgpPath */
      8, /* switchId */
      8, /* multiPathSize */
      200.f, /* agg-received */
      400.f, /* agg-local*/
      500.f, /* rib-policy-lbw*/
      true, /* newlyInstalledInLocalRib */
      std::chrono::system_clock::now() /* installTimeStamp */};

  // Call the function to test
  peerMgr->updateShadowRibEntryUtil(srEntry, entry);

  // Verify that srEntry has been updated correctly
  EXPECT_NE(srEntry.prefix, entry.prefix);
  EXPECT_EQ(srEntry.switchId, entry.switchId);
  EXPECT_EQ(srEntry.multiPathSize, entry.multiPathSize);
  EXPECT_EQ(
      srEntry.aggregateReceivedUcmpWeight, entry.aggregateReceivedUcmpWeight);
  EXPECT_EQ(srEntry.aggregateLocalUcmpWeight, entry.aggregateLocalUcmpWeight);
  EXPECT_EQ(srEntry.ribPolicyUcmpWeight, entry.ribPolicyUcmpWeight);
  EXPECT_EQ(srEntry.newlyInstalledInLocalRib, entry.newlyInstalledInLocalRib);
  EXPECT_EQ(srEntry.installTimeStamp, entry.installTimeStamp);
}

/*
 * This test creates RibAnnouncement with the bestpath update and make sure:
 *  1. bestpath is overridden when received update.
 *  2. multipaths is not populated.
 */
TEST_F(PeerManagerTestFixture, ShadowRibEntryBestpathTest) {
  // cleanup counter for testing purpose
  facebook::fb303::ThreadCachedServiceData::getShared()->zeroStats();

  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      std::make_shared<ConfigManager>(config),
      nullptr,
      ribInQ_,
      ribOutQ_,
      nbrRouteChangeQ_);

  // check counter not existing
  EXPECT_FALSE(
      facebook::fb303::ThreadCachedServiceData::getShared()->hasCounter(
          RibStats::kTotalShadowRibEntries));

  {
    /*
     * Step 0: verify the empty ShadowRib collection
     */
    EXPECT_EQ(0, peerMgr->shadowRibEntries_.size());
  }
  {
    /*
     * Step 1: push a Rib announcement
     */
    auto msg1 = createRibSingleAnnounce(
        kV4Prefix1, /* prefix */
        kV4Nexthop1, /* nexthop */
        kLocalRouteAs, /* peer */
        false, /* sendWithEoR */
        false /* addpath */
    );
    peerMgr->handleShadowRibEntryAnnouncement(
        std::get<RibOutAnnouncement>(msg1));
    EXPECT_EQ(1, peerMgr->shadowRibEntries_.size());
    const auto& srEntry1 = peerMgr->shadowRibEntries_.at(kV4Prefix1)->get();
    EXPECT_NE(nullptr, srEntry1.bestpath);
    EXPECT_EQ(0, srEntry1.multipaths.size());
    const auto bestpath1 = srEntry1.bestpath;

    /*
     * Step 2: push a different Rib announcement with different path.
     * Make sure path count is still 1 since only bestpath is updated.
     */
    auto msg2 = createRibSingleAnnounce(
        kV4Prefix1, /* prefix */
        kV4Nexthop2, /* nexthop */
        kLocalRouteAs, /* peer */
        false, /* sendWithEoR */
        false /* addpath */
    );
    peerMgr->handleShadowRibEntryAnnouncement(
        std::get<RibOutAnnouncement>(msg2));
    EXPECT_EQ(1, peerMgr->shadowRibEntries_.size());
    const auto& srEntry2 = peerMgr->shadowRibEntries_.at(kV4Prefix1)->get();
    EXPECT_NE(nullptr, srEntry2.bestpath);
    EXPECT_EQ(0, srEntry2.multipaths.size());
    const auto bestpath2 = srEntry2.bestpath;
    EXPECT_NE(bestpath1, bestpath2);

    EXPECT_EQ(
        1,
        facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
            RibStats::kTotalShadowRibEntries));
  }
  {
    /*
     * Step 3: push a RibWithdrawal for non-existing prefix.
     * Expect this is a no-op.
     */
    RibOutWithdrawal withdrawal{};
    withdrawal.entries.emplace_back(kV4Prefix2, kDefaultPathID);

    peerMgr->handleShadowRibEntryWithdrawal(withdrawal);
    EXPECT_EQ(1, peerMgr->shadowRibEntries_.size());
    EXPECT_TRUE(peerMgr->shadowRibEntries_.contains(kV4Prefix1));
  }
  {
    /*
     * Step 4: push a RibWithdrawal for the existing prefix
     */
    RibOutWithdrawal withdrawal{};
    withdrawal.entries.emplace_back(kV4Prefix1, kDefaultPathID);

    peerMgr->handleShadowRibEntryWithdrawal(withdrawal);
    EXPECT_EQ(0, peerMgr->shadowRibEntries_.size());
    EXPECT_EQ(
        0,
        facebook::fb303::ThreadCachedServiceData::getShared()->getCounter(
            RibStats::kTotalShadowRibEntries));
  }
}

/*
 * This test creates RibAnnouncement with the add-path update and make sure:
 *  1. multipath(one of) is overridden when the SAME nexthop is populated.
 *  2. multipath is appended when a DIFF nexthop is populated.
 */
TEST_F(PeerManagerTestFixture, ShadowRibEntryMultipathTest) {
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      std::make_shared<ConfigManager>(config),
      nullptr,
      ribInQ_,
      ribOutQ_,
      nbrRouteChangeQ_);

  PathId pathId = kV4Nexthop1;

  {
    /*
     * Step 0: verify the empty ShadowRib collection
     */
    EXPECT_EQ(0, peerMgr->shadowRibEntries_.size());
  }
  {
    /*
     * Step 1: push a Rib announcement and expect multipath is updated
     */
    auto msg1 = createRibSingleAnnounce(
        kV4Prefix1, /* prefix */
        kV4Nexthop1, /* nexthop */
        kLocalRouteAs, /* peer */
        false, /* sendWithEoR */
        true, /* addPath */
        0); /* pathIdToSend */

    peerMgr->handleShadowRibEntryAnnouncement(
        std::get<RibOutAnnouncement>(msg1));
    EXPECT_EQ(1, peerMgr->shadowRibEntries_.size());
    const auto& srEntry1 = peerMgr->shadowRibEntries_.at(kV4Prefix1)->get();
    EXPECT_EQ(nullptr, srEntry1.bestpath);
    EXPECT_EQ(1, srEntry1.multipaths.size());
    const auto peerBefore = srEntry1.multipaths.at(pathId)->peer;
    const auto attrBefore = srEntry1.multipaths.at(pathId)->attrs;

    /*
     * Step 2: push the same announcement with the same path.
     * Make sure path count is NOT incremented as path is updated.
     */
    auto msg2 = createRibSingleAnnounce(
        kV4Prefix1, /* prefix */
        kV4Nexthop1, /* nexthop */
        kLocalAs1, /* peer */
        false, /* sendWithEoR */
        true, /* addPath */
        0); /* pathIdToSend */

    peerMgr->handleShadowRibEntryAnnouncement(
        std::get<RibOutAnnouncement>(msg2));
    EXPECT_EQ(1, peerMgr->shadowRibEntries_.size());
    const auto& srEntry2 = peerMgr->shadowRibEntries_.at(kV4Prefix1)->get();
    EXPECT_EQ(nullptr, srEntry2.bestpath);
    EXPECT_EQ(1, srEntry2.multipaths.size());
    const auto peerAfter = srEntry2.multipaths.at(pathId)->peer;
    const auto attrAfter = srEntry2.multipaths.at(pathId)->attrs;
    EXPECT_NE(peerBefore, peerAfter);
    EXPECT_NE(attrBefore, attrAfter);
  }
  {
    /*
     * Step 3: push a different Rib announcement with different path.
     * Make sure path count is incremented since multipath is extended.
     */
    auto msg3 = createRibSingleAnnounce(
        kV4Prefix1, /* prefix */
        kV4Nexthop2, /* nexthop */
        kLocalRouteAs, /* peer */
        false, /* sendWithEoR */
        true, /* addPath */
        1); /* pathIdToSend */

    peerMgr->handleShadowRibEntryAnnouncement(
        std::get<RibOutAnnouncement>(msg3));
    EXPECT_EQ(1, peerMgr->shadowRibEntries_.size());
    const auto& srEntry = peerMgr->shadowRibEntries_.at(kV4Prefix1)->get();
    EXPECT_EQ(nullptr, srEntry.bestpath);

    // multipath is extended with a different nexthop
    EXPECT_EQ(2, srEntry.multipaths.size());
  }
  {
    /*
     * Step 4: push a witdrawal to remove a non-existing path.
     * Expect this is a no-op.
     */
    RibOutWithdrawal withdrawal{};
    withdrawal.addPathEntries.emplace_back(kV4Prefix2, 15);

    peerMgr->handleShadowRibEntryWithdrawal(withdrawal);
    EXPECT_EQ(1, peerMgr->shadowRibEntries_.size());
    const auto& srEntry = peerMgr->shadowRibEntries_.at(kV4Prefix1)->get();
    EXPECT_EQ(2, srEntry.multipaths.size());
  }
  {
    /*
     * Step 5: deliberately push a witdrawal with different combinations.
     */
    RibOutWithdrawal withdrawal{};

    //  1. a non-existing path. Expect this is a no-op.
    withdrawal.addPathEntries.emplace_back(kV4Prefix1, 15, kV4Nexthop3);

    //  2. an existing path with expected ID. Expect path to be erased.
    withdrawal.addPathEntries.emplace_back(kV4Prefix1, 1, kV4Nexthop2);

    peerMgr->handleShadowRibEntryWithdrawal(withdrawal);
    EXPECT_EQ(1, peerMgr->shadowRibEntries_.size());
    const auto& srEntry = peerMgr->shadowRibEntries_.at(kV4Prefix1)->get();
    EXPECT_EQ(1, srEntry.multipaths.size());
    EXPECT_TRUE(srEntry.multipaths.contains(pathId));
  }
}

/*
 * With RIB allocated path IDs, shadow RIB multipaths are keyed by path ID, so
 * paths that share a nexthop keep separate entries.
 */
TEST_F(PeerManagerTestFixture, ShadowRibEntryMultipathKeyedByPathId) {
  auto config = enableRibAllocatedPathId(getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */));
  auto peerMgr = std::make_shared<PeerManagerBase>(
      std::make_shared<ConfigManager>(config),
      nullptr,
      ribInQ_,
      ribOutQ_,
      nbrRouteChangeQ_);

  for (const uint32_t pathIdToSend : {0u, 1u}) {
    auto msg = createRibSingleAnnounce(
        kV4Prefix1, /* prefix */
        kV4Nexthop1, /* nexthop */
        kLocalRouteAs, /* peer */
        false, /* sendWithEoR */
        true, /* addPath */
        pathIdToSend);
    peerMgr->handleShadowRibEntryAnnouncement(
        std::get<RibOutAnnouncement>(msg));
  }

  const auto& srEntry = peerMgr->shadowRibEntries_.at(kV4Prefix1)->get();
  EXPECT_EQ(2, srEntry.multipaths.size());
  EXPECT_TRUE(srEntry.multipaths.contains(PathId{uint32_t{0}}));
  EXPECT_TRUE(srEntry.multipaths.contains(PathId{uint32_t{1}}));
}

/*
 * With RIB allocated path IDs, an add-path withdrawal removes only the path
 * with its path ID, even when another path shares the nexthop.
 */
TEST_F(PeerManagerTestFixture, ShadowRibEntryWithdrawalByPathId) {
  auto config = enableRibAllocatedPathId(getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */));
  auto peerMgr = std::make_shared<PeerManagerBase>(
      std::make_shared<ConfigManager>(config),
      nullptr,
      ribInQ_,
      ribOutQ_,
      nbrRouteChangeQ_);

  for (const uint32_t pathIdToSend : {0u, 1u}) {
    auto msg = createRibSingleAnnounce(
        kV4Prefix1, /* prefix */
        kV4Nexthop1, /* nexthop */
        kLocalRouteAs, /* peer */
        false, /* sendWithEoR */
        true, /* addPath */
        pathIdToSend);
    peerMgr->handleShadowRibEntryAnnouncement(
        std::get<RibOutAnnouncement>(msg));
  }

  RibOutWithdrawal withdrawal{};
  withdrawal.addPathEntries.emplace_back(kV4Prefix1, 1, kV4Nexthop1);
  peerMgr->handleShadowRibEntryWithdrawal(withdrawal);

  const auto& srEntry = peerMgr->shadowRibEntries_.at(kV4Prefix1)->get();
  EXPECT_EQ(1, srEntry.multipaths.size());
  EXPECT_TRUE(srEntry.multipaths.contains(PathId{uint32_t{0}}));
}

TEST_F(PeerManagerTestFixture, ShadowRibEntryEmptyAttrTest) {
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      std::make_shared<ConfigManager>(config),
      nullptr,
      ribInQ_,
      ribOutQ_,
      nbrRouteChangeQ_);

  {
    /*
     * Step 0: verify the empty ShadowRib collection
     */
    EXPECT_EQ(0, peerMgr->shadowRibEntries_.size());
  }
  {
    /*
     * Step 1: set attrs to be nullptr to create a broken update
     */
    auto msg = createRibSingleAnnounce(
        kV4Prefix1, /* prefix */
        kV4Nexthop1, /* nexthop */
        kLocalRouteAs, /* peer */
        false, /* sendWithEoR */
        true, /* addPath */
        0); /* pathIdToSend */

    auto announcement = std::get<RibOutAnnouncement>(msg);
    EXPECT_EQ(1, announcement.addPathEntries.size());

    // deliberately reset attrs
    announcement.addPathEntries.front().attrs = nullptr;

    peerMgr->handleShadowRibEntryAnnouncement(announcement);

    // shadowRib entry will not be updated
    EXPECT_EQ(0, peerMgr->shadowRibEntries_.size());
  }
}

/*
 * This test creates a mixed update of "bestpath" and "multipath" from RIB
 * and make sure different processing order does not make a difference for the
 * processing result.
 */
TEST_F(PeerManagerTestFixture, ShadowRibEntryMixpathTest) {
  PathId pathId = kV4Nexthop1;

  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      std::make_shared<ConfigManager>(config),
      nullptr,
      ribInQ_,
      ribOutQ_,
      nbrRouteChangeQ_);

  auto msg1 = createRibSingleAnnounce(
      kV4Prefix1, /* prefix */
      kV4Nexthop1, /* nexthop */
      kLocalRouteAs, /* peer */
      false, /* sendWithEoR */
      false /* addPath */
  );
  auto msg2 = createRibSingleAnnounce(
      kV4Prefix1, /* prefix */
      kV4Nexthop1, /* nexthop */
      kLocalRouteAs, /* peer */
      false, /* sendWithEoR */
      true, /* addPath */
      0); /* pathIdToSend */

  {
    /*
     * Step 1: push a bestpath update followed by a multipath update to make
     * sure state is consistent.
     */
    peerMgr->handleShadowRibEntryAnnouncement(
        std::get<RibOutAnnouncement>(msg1));
    peerMgr->handleShadowRibEntryAnnouncement(
        std::get<RibOutAnnouncement>(msg2));
    EXPECT_EQ(1, peerMgr->shadowRibEntries_.size());
    const auto* srEntry = &peerMgr->shadowRibEntries_.at(kV4Prefix1)->get();
    EXPECT_NE(nullptr, srEntry->bestpath);
    EXPECT_EQ(1, srEntry->multipaths.size());
    const auto peerBefore = srEntry->multipaths.at(pathId)->peer;
    const auto attrBefore = srEntry->multipaths.at(pathId)->attrs;

    /*
     * Step 2: push a withdrawal update and expect shadow ribEntry to be
     * removed
     */
    RibOutWithdrawal withdrawal{};
    withdrawal.entries.emplace_back(kV4Prefix1, 0);
    peerMgr->handleShadowRibEntryWithdrawal(withdrawal);
    /*
     * Only bestpath is explicitly withdrwan. Not multipath, hence
     * shadowRibEntry would still stick around
     */
    EXPECT_EQ(1, peerMgr->shadowRibEntries_.size());

    srEntry = &peerMgr->shadowRibEntries_.at(kV4Prefix1)->get();
    EXPECT_EQ(nullptr, srEntry->bestpath);
    EXPECT_EQ(1, srEntry->multipaths.size());

    /*
     * Step 3: push a multipath update followed by a bestpath update to make
     * sure state is consistent.
     */
    peerMgr->handleShadowRibEntryAnnouncement(
        std::get<RibOutAnnouncement>(msg2));
    peerMgr->handleShadowRibEntryAnnouncement(
        std::get<RibOutAnnouncement>(msg1));
    EXPECT_EQ(1, peerMgr->shadowRibEntries_.size());
    const auto& srEntry1 = peerMgr->shadowRibEntries_.at(kV4Prefix1)->get();
    EXPECT_NE(nullptr, srEntry1.bestpath);
    EXPECT_EQ(1, srEntry1.multipaths.size());
    const auto peerAfter = srEntry1.multipaths.at(pathId)->peer;
    const auto attrAfter = srEntry1.multipaths.at(pathId)->attrs;

    EXPECT_EQ(peerBefore, peerAfter);
    EXPECT_EQ(attrBefore, attrAfter);
  }
}

TEST_F(PeerManagerTestFixture, RibDumpReqNegativeTest) {
  /*
   * Step 0: test setup with PeerManagerBase and 1 adjRib
   */
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      std::make_shared<ConfigManager>(config),
      nullptr,
      ribInQ_,
      ribOutQ_,
      nbrRouteChangeQ_);

  auto& evb = peerMgr->getEventBase();
  auto adjRib = setupAdjRib(
      evb,
      peerMgr->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);

  // start to subscrribe to log messages
  auto& messages = subscribeToLogMessages("");

  {
    /*
     * Test 1: request RibDumpReq and expect skip due to no adjrib found.
     */
    peerMgr->ribInitialAnnouncementStarted_ = true;

    messages.clear();

    folly::coro::blockingWait(peerMgr->processRibDumpReqCoro(
        RibDumpReq(kPeerId1, false /* sendAddPath */)));
    EXPECT_EQ(
        messages.back().first.getMessage(),
        fmt::format(
            "Skip sending announcement since adjRib {} does not exist.",
            kPeerId1.str()));
  }

  /*
   * Register adjRib so it passes the "does not exist" check,
   * but hits the "session queues not initialized" check.
   */
  peerMgr->adjRibs_[kPeerId1] = adjRib;

  messages.clear();
  folly::coro::blockingWait(peerMgr->processRibDumpReqCoro(
      RibDumpReq(kPeerId1, false /* sendAddPath */)));
  EXPECT_EQ(
      messages.back().first.getMessage(),
      fmt::format(
          "Skip RibDumpReq since session queues not initialized for {}.",
          adjRib->getRemotePeerId().str()));
  /*
   * Cleanup: reset change list consumer to break circular shared_ptr reference
   * (adjRib → consumer → adjRib) which keeps AdjRibPolicyCache singleton alive
   * past destroyInstances, causing SIGABRT.
   */
  adjRib->resetChangeListConsumer();
}

/**
 * Test to update shadow Rib Entry immediately followed by withdrawal
 * The test specifically designed to verify shadowRib State with
 * changeList enabled, and also verify consumption of RIB entry by
 * adjRib as expected
 */
CO_TEST_F(PeerManagerTestFixture, ShadowRibEntryMultiUpdateTest) {
  /*
   * Step 0.0: test setup with PeerManagerBase and adjRib:
   */
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      std::make_shared<ConfigManager>(config),
      nullptr,
      ribInQ_,
      ribOutQ_,
      nbrRouteChangeQ_);

  auto& evb = peerMgr->getEventBase();
  auto adjRibInQ1 = std::make_shared<AdjRib::AdjRibInQueueT>();

  auto boundedOutQ1 = std::make_shared<AdjRib::AdjRibOutQueueT>(
      kMaxEgressQueueSize, kEgressQueueHighWatermark, kEgressQueueLowWatermark);
  auto boundedOutQ2 = std::make_shared<AdjRib::AdjRibOutQueueT>(
      kMaxEgressQueueSize, kEgressQueueHighWatermark, kEgressQueueLowWatermark);

  auto adjRib1 = setupAdjRib(
      evb,
      peerMgr->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config,
      peerMgr->addPathConsumerBitmap_,
      peerMgr->nonAddPathConsumerBitmap_);

  peerMgr->ribInitialAnnouncementStarted_ = true;
  peerMgr->ribInitialAnnouncementDone_ = true;

  // create mapping of adjRibs
  peerMgr->adjRibs_[kPeerId1] = adjRib1;

  // mark session in established state for queue validation
  adjRib1->sessionEstablished(std::nullopt, adjRibInQ1, boundedOutQ1);
  adjRib1->sendAddPath_ = false; /* add-path incapable peer */
  adjRib1->markStateEstablished();

  /*
   * Step 0.1: pre-populate shadow rib entries with bestpath and multipath
   */
  auto msg1 = createRibSingleAnnounce(
      kV4Prefix1, /* prefix */
      kV4Nexthop1, /* nexthop */
      kLocalRouteAs, /* peer */
      false, /* sendWithEoR */
      false /* addPath */
  );
  peerMgr->handleShadowRibEntryAnnouncement(std::get<RibOutAnnouncement>(msg1));

  /*
   * No EoR sent and hence no changeListConsumer registered and hence no
   * items in changeList
   */
  auto changeItem = peerMgr->getChangeListTracker()->getHead();
  EXPECT_EQ(nullptr, changeItem);

  /*
   * Step 0.2: send RibDumpReq for adjrib1 with sendAddPath = false.
   */
  folly::coro::blockingWait(peerMgr->processRibDumpReqCoro(
      RibDumpReq(kPeerId1, false /* sendAddPath */)));

  /*
   * This would also trigger EoR. Thus now adjRib1 should have all
   * the routes from shadowRib
   */

  EXPECT_EQ(1, adjRib1->stats_.getPreOutPrefixCount());
  evb.loopOnce();
  co_await facebook::bgp::test::boundedPop(
      *adjRib1->adjRibOutQueue_, "adjRib1->adjRibOutQueue_");

  /*
   * Step 0.3: send Single RIB update immediately followed by withdrawal
   */
  auto msg2 = createRibSingleAnnounce(
      kV4Prefix1, /* prefix */
      kV4Nexthop2, /* nexthop */
      kLocalRouteAs, /* peer */
      false, /* sendWithEoR */
      false /* addPath */
  );
  peerMgr->handleShadowRibEntryAnnouncement(std::get<RibOutAnnouncement>(msg2));

  RibOutWithdrawal ribWithdrawal;
  ribWithdrawal.entries.emplace_back(kV4Prefix1, kDefaultPathID);
  peerMgr->handleShadowRibEntryWithdrawal(ribWithdrawal);

  co_await adjRib1->getChangeListConsumer()->consumeChanges();
  evb.loopOnce();
  co_await facebook::bgp::test::boundedPop(
      *adjRib1->adjRibOutQueue_, "adjRib1->adjRibOutQueue_");

  /* adjrib should have correctly received final withdrawal */
  EXPECT_EQ(0, adjRib1->stats_.getPreOutPrefixCount());
  /*
   * shadowRib should have correctly removed withdrawn entry
   * and thus now shadowRib should be empty
   */
  EXPECT_EQ(0, peerMgr->shadowRibEntries_.size());
}

CO_TEST_F(PeerManagerTestFixture, RibDumpReqPositiveTest) {
  /*
   * Step 0.0: test setup with PeerManagerBase and 2 adjRibs:
   *  - adjrib1: add-path disabled peer(by default)
   *  - adirib2: add-path enabled peer
   */
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      std::make_shared<ConfigManager>(config),
      nullptr,
      ribInQ_,
      ribOutQ_,
      nbrRouteChangeQ_);

  auto& evb = peerMgr->getEventBase();
  /*
   * Create adjRibInQueues with unlimited capacity since tests don't
   * start message processing loops to drain these queues. With the new limited
   * kMaxIngressQueueSize, using default capacity (1) would cause tests to
   * block if more than 1 message needs to be queued.
   */
  auto adjRibInQ1 = std::make_shared<AdjRib::AdjRibInQueueT>(
      std::numeric_limits<size_t>::max());
  auto adjRibInQ2 = std::make_shared<AdjRib::AdjRibInQueueT>(
      std::numeric_limits<size_t>::max());
  auto boundedOutQ1 = std::make_shared<AdjRib::AdjRibOutQueueT>(
      kMaxEgressQueueSize, kEgressQueueHighWatermark, kEgressQueueLowWatermark);
  auto boundedOutQ2 = std::make_shared<AdjRib::AdjRibOutQueueT>(
      kMaxEgressQueueSize, kEgressQueueHighWatermark, kEgressQueueLowWatermark);

  auto adjRib1 = setupAdjRib(
      evb,
      peerMgr->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config,
      peerMgr->addPathConsumerBitmap_,
      peerMgr->nonAddPathConsumerBitmap_);

  auto adjRib2 = setupAdjRib(
      evb,
      peerMgr->getChangeListTracker(),
      kPeerId2,
      AsNum(kAsn2),
      sessionTerminateBaton_,
      config,
      peerMgr->addPathConsumerBitmap_,
      peerMgr->nonAddPathConsumerBitmap_);

  /*
   * mark EoR ready to simulate initial Fib sync done and initial
   * Rib announcements are queued
   */
  peerMgr->ribInitialAnnouncementStarted_ = true;
  peerMgr->ribInitialAnnouncementDone_ = true;

  // create mapping of adjRibs
  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  peerMgr->adjRibs_[kPeerId2] = adjRib2;

  // mark session in established state for queue validation
  adjRib1->sessionEstablished(std::nullopt, adjRibInQ1, boundedOutQ1);
  adjRib1->sendAddPath_ = false; /* add-path incapable peer */
  adjRib1->markStateEstablished();

  adjRib2->sessionEstablished(std::nullopt, adjRibInQ2, boundedOutQ2);
  adjRib2->sendAddPath_ = true; /* add-path capable peer */
  adjRib2->markStateEstablished();

  /*
   * Step 0.1: pre-populate shadow rib entries with bestpath and multipath
   */
  auto msg1 = createRibSingleAnnounce(
      kV4Prefix1, /* prefix */
      kV4Nexthop1, /* nexthop */
      kLocalRouteAs, /* peer */
      false, /* sendWithEoR */
      false /* addPath */
  );
  auto msg2 = createRibSingleAnnounce(
      kV4Prefix2, /* prefix */
      kV4Nexthop1, /* nexthop */
      kLocalRouteAs, /* peer */
      false, /* sendWithEoR */
      true, /* addPath */
      0 /* pathIdToSend */
  );
  auto msg3 = createRibSingleAnnounce(
      kV4Prefix2, /* prefix */
      kV4Nexthop2, /* nexthop */
      kLocalRouteAs, /* peer */
      false, /* sendWithEoR */
      true, /* addPath */
      1 + 1 /* pathIdToSend */
  );

  // populate mutlipath first without bestpath
  peerMgr->handleShadowRibEntryAnnouncement(std::get<RibOutAnnouncement>(msg2));

  /*
   * No EoR sent and hence no changeListConsumer registered and hence no
   * items in changeList
   */
  auto changeItem = peerMgr->getChangeListTracker()->getHead();
  EXPECT_EQ(nullptr, changeItem);

  {
    /*
     * Step 0.2: send RibDumpReq for adjrib1 with sendAddPath = false.
     *         Since there has been no bestpath yet in shadowRib collection,
     *         the handling of RibDumpReq should not crash and send nothing.
     */
    co_await peerMgr->processRibDumpReqCoro(
        RibDumpReq(kPeerId1, false /* sendAddPath */));
  }
  {
    /*
     * Step 1: send RibDumpReq for adjrib1 with sendAddPath = false
     *
     * populate bestpath
     */
    peerMgr->handleShadowRibEntryAnnouncement(
        std::get<RibOutAnnouncement>(msg1));
    peerMgr->handleShadowRibEntryAnnouncement(
        std::get<RibOutAnnouncement>(msg3));

    /*
     * Since Ribdump was sent, updates now published to the changeList
     */
    auto changeItemAfterDump = peerMgr->getChangeListTracker()->getHead();
    EXPECT_NE(nullptr, changeItemAfterDump);
    auto& srEntry = changeItemAfterDump->getTypedObject();
    EXPECT_EQ(1, srEntry.publishCount);
    EXPECT_EQ(true, isShadowRibRouteInUpdate(srEntry.bestpath->flags));
    EXPECT_EQ(false, isShadowRibRouteInWithdraw(srEntry.bestpath->flags));

    auto lastAdjRib1SendUpdateMsgs = adjRib1->stats_.getSentUpdateMsgs();
    auto lastAdjRib1SendEorMsgs = adjRib1->stats_.getSentEndOfRibMsgs();
    co_await peerMgr->processRibDumpReqCoro(
        RibDumpReq(kPeerId1, false /* sendAddPath */));

    /*
     * Because both the prefixes are on change-list, they won't be sent
     * in response to RibDumpRequest. Only EoR for each AFI sent
     */
    /* Let the sendBgpMessages coro run once */
    evb.loopOnce();
    auto eor1 = co_await facebook::bgp::test::boundedPop(
        *adjRib1->adjRibOutQueue_, "adjRib1->adjRibOutQueue_");
    auto eor2 = co_await facebook::bgp::test::boundedPop(
        *adjRib1->adjRibOutQueue_, "adjRib1->adjRibOutQueue_");
    EXPECT_TRUE(std::holds_alternative<BgpEndOfRib>(*eor1));
    EXPECT_TRUE(std::holds_alternative<BgpEndOfRib>(*eor2));
    EXPECT_EQ(lastAdjRib1SendUpdateMsgs, adjRib1->stats_.getSentUpdateMsgs());
    EXPECT_EQ(
        lastAdjRib1SendEorMsgs + 2, adjRib1->stats_.getSentEndOfRibMsgs());
  }
  {
    co_await adjRib1->getChangeListConsumer()->consumeChanges();
    /*
     * Step 2: send RibDumpReq for adjrib2 with sendAddPath = true
     */
    auto lastAdjRib1SendUpdateMsgs = adjRib1->stats_.getSentUpdateMsgs();
    auto lastAdjRib1SendEorMsgs = adjRib1->stats_.getSentEndOfRibMsgs();
    co_await peerMgr->processRibDumpReqCoro(
        RibDumpReq(kPeerId1, true /* sendAddPath */));

    /*
     * Because change-list had been consumed, one update message with
     * all the prefixes sent, and EoR for each AFI
     */
    /* Let the sendBgpMessages coro run once */
    evb.loopOnce();
    auto update = co_await facebook::bgp::test::boundedPop(
        *adjRib1->adjRibOutQueue_, "adjRib1->adjRibOutQueue_");
    auto eor1 = co_await facebook::bgp::test::boundedPop(
        *adjRib1->adjRibOutQueue_, "adjRib1->adjRibOutQueue_");
    auto eor2 = co_await facebook::bgp::test::boundedPop(
        *adjRib1->adjRibOutQueue_, "adjRib1->adjRibOutQueue_");
    EXPECT_TRUE(
        std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*update));
    EXPECT_TRUE(std::holds_alternative<BgpEndOfRib>(*eor1));
    EXPECT_TRUE(std::holds_alternative<BgpEndOfRib>(*eor2));
    EXPECT_EQ(
        lastAdjRib1SendUpdateMsgs + 1, adjRib1->stats_.getSentUpdateMsgs());
    EXPECT_EQ(
        lastAdjRib1SendEorMsgs + 2, adjRib1->stats_.getSentEndOfRibMsgs());
  }
}

/*
 * Verifies that ribInitialAnnouncementDone_ flag is set upon receipt of
 * RibInitialAnnouncementStart msg.
 */
CO_TEST_F(RibInitialAnnouncementTestFixture, RibInitialAnnouncementStartTest) {
  EXPECT_FALSE(peerMgr_->ribInitialAnnouncementStarted_);
  EXPECT_FALSE(peerMgr_->ribInitialAnnouncementDone_);

  auto& messages = subscribeToLogMessages("");

  auto& evb = peerMgr_->getEventBase();
  auto keepAlive = folly::Executor::getKeepAliveToken(evb);
  std::thread evbThread([&]() { evb.loop(); });

  // Start processRibOutMsgLoop and push RibInitialAnnouncementStart.
  peerMgr_->asyncScope_.add(
      co_withExecutor(&evb, peerMgr_->processRibOutMsgLoop()));
  ribOutQ_.push(RibInitialAnnouncementStart{});

  /*
   * Wait for ribOutQ_ to drain, then synchronize with the EVB thread to
   * ensure processRibOutMsgLoop has finished processing the message.
   * runInEventBaseThreadAndWait provides a happens-before guarantee,
   * preventing data races when reading ribInitialAnnouncementStarted_ from
   * this thread.
   */
  while (!ribOutQ_.empty()) {
    co_await folly::coro::sleep(std::chrono::milliseconds(1));
  }
  evb.runInEventBaseThreadAndWait([]() {});

  EXPECT_TRUE(peerMgr_->ribInitialAnnouncementStarted_);
  EXPECT_FALSE(peerMgr_->ribInitialAnnouncementDone_);

  cleanUp();

  /*
   * Release the keep-alive token. The EVB will exit loop() once all
   * remaining work (AdjRib teardown, etc.) completes.
   */
  keepAlive.reset();
  evbThread.join();

  /*
   * Check log messages after the EVB thread has joined to avoid racing
   * with the EVB thread appending to the messages vector.
   */
  auto hasMessage = [&](const std::string& prefix) {
    return std::any_of(messages.begin(), messages.end(), [&](const auto& msg) {
      return msg.first.getMessage().starts_with(prefix);
    });
  };
  EXPECT_TRUE(hasMessage("Starting ribOutMsg processing coro task..."));
  EXPECT_TRUE(hasMessage(
      "[Initialization] Received RibInitialAnnouncementStart message."));
}

TEST_F(
    RibInitialAnnouncementTestFixture,
    MaybeBufferRibDumpReqTest_SessionUpBeforeRibInitialAnnouncementStart) {
  EXPECT_TRUE(ribOutQ_.empty());
  EXPECT_FALSE(peerMgr_->ribInitialAnnouncementStarted_);
  EXPECT_FALSE(peerMgr_->ribInitialAnnouncementDone_);

  auto& evb = peerMgr_->getEventBase();
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  peerMgr_->adjRibs_[kPeerId1] = setupAdjRib(
      evb,
      peerMgr_->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);

  auto& adjRib = peerMgr_->adjRibs_[kPeerId1];
  auto& messages = subscribeToLogMessages("");
  {
    messages.clear();

    // Case 1: sendAddPath = true
    adjRib->sendAddPath_ = true;
    peerMgr_->maybeBufferRibDumpReq(adjRib);

    EXPECT_EQ(1, messages.size());
    EXPECT_EQ(
        messages[0].first.getMessage(),
        fmt::format(
            "Not sending RibDumpReq for {}; Rib has not started initial announcement.",
            kPeerId1.str()));

    EXPECT_TRUE(peerMgr_->pendingRibDumpReqs_.empty());
    EXPECT_EQ(0, peerMgr_->asyncScope_.remaining());
  }
  {
    messages.clear();

    // Case 2: sendAddPath = false
    adjRib->sendAddPath_ = false;
    peerMgr_->maybeBufferRibDumpReq(adjRib);

    EXPECT_EQ(1, messages.size());
    EXPECT_EQ(
        messages[0].first.getMessage(),
        fmt::format(
            "Not sending RibDumpReq for {}; Rib has not started initial announcement.",
            kPeerId1.str()));

    EXPECT_TRUE(peerMgr_->pendingRibDumpReqs_.empty());
    EXPECT_EQ(0, peerMgr_->asyncScope_.remaining());
  }
}

TEST_F(
    RibInitialAnnouncementTestFixture,
    MaybeBufferRibDumpReqTest_SessionUpAfterRibInitialAnnouncementDone) {
  auto& evb = peerMgr_->getEventBase();
  peerMgr_->ribInitialAnnouncementStarted_ = true;
  peerMgr_->ribInitialAnnouncementDone_ = true;

  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  peerMgr_->adjRibs_[kPeerId1] = setupAdjRib(
      evb,
      peerMgr_->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);

  auto& adjRib = peerMgr_->adjRibs_[kPeerId1];
  auto& messages = subscribeToLogMessages("");
  {
    messages.clear();

    // Case 1: sendAddPath = true
    adjRib->sendAddPath_ = true;
    peerMgr_->maybeBufferRibDumpReq(adjRib);

    EXPECT_EQ(1, messages.size());
    EXPECT_EQ(
        messages[0].first.getMessage(),
        fmt::format(
            "Insert RibDumpReq in pending list for {}", kPeerId1.str()));

    EXPECT_EQ(1, peerMgr_->pendingRibDumpReqs_.size());
    EXPECT_TRUE(peerMgr_->pendingRibDumpReqs_[kPeerId1]);
    EXPECT_EQ(1, peerMgr_->asyncScope_.remaining());

    /*
     * Only the drain task is scheduled; the per-peer dump (and its cancellation
     * source) is not created until handleBufferedRibDumpReqs runs.
     */
    EXPECT_FALSE(adjRib->isRibDumpScheduled());
  }
  {
    messages.clear();

    // Case 2: sendAddPath = false
    adjRib->sendAddPath_ = false;
    peerMgr_->maybeBufferRibDumpReq(adjRib);

    EXPECT_EQ(1, messages.size());
    EXPECT_EQ(
        messages[0].first.getMessage(),
        fmt::format(
            "Insert RibDumpReq in pending list for {}", kPeerId1.str()));

    EXPECT_EQ(1, peerMgr_->pendingRibDumpReqs_.size());
    EXPECT_FALSE(peerMgr_->pendingRibDumpReqs_[kPeerId1]);
    EXPECT_EQ(1, peerMgr_->asyncScope_.remaining());

    /*
     * Only the drain task is scheduled; the per-peer dump (and its cancellation
     * source) is not created until handleBufferedRibDumpReqs runs.
     */
    EXPECT_FALSE(adjRib->isRibDumpScheduled());
  }

  // Test cleanup.
  evb.loop();
  folly::coro::blockingWait(peerMgr_->asyncScope_.joinAsync());
}

TEST_F(
    RibInitialAnnouncementTestFixture,
    MaybeBufferRibDumpReqTest_SessionUpDuringRibInitialAnnouncement) {
  auto& evb = peerMgr_->getEventBase();
  peerMgr_->ribInitialAnnouncementStarted_ = true;
  peerMgr_->ribInitialAnnouncementDone_ = false;

  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  peerMgr_->adjRibs_[kPeerId1] = setupAdjRib(
      evb,
      peerMgr_->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);

  auto& adjRib = peerMgr_->adjRibs_[kPeerId1];
  auto& messages = subscribeToLogMessages("");
  {
    messages.clear();

    // Case 1: sendAddPath = true
    adjRib->sendAddPath_ = true;
    peerMgr_->maybeBufferRibDumpReq(adjRib);

    EXPECT_EQ(1, messages.size());
    EXPECT_EQ(
        messages[0].first.getMessage(),
        fmt::format(
            "Insert RibDumpReq in pending list for {}", kPeerId1.str()));

    EXPECT_EQ(1, peerMgr_->pendingRibDumpReqs_.size());
    EXPECT_TRUE(peerMgr_->pendingRibDumpReqs_[kPeerId1]);
    EXPECT_EQ(0, peerMgr_->asyncScope_.remaining());

    EXPECT_FALSE(adjRib->isRibDumpScheduled());
  }
  {
    messages.clear();

    // Case 2: sendAddPath = false
    adjRib->sendAddPath_ = false;
    peerMgr_->maybeBufferRibDumpReq(adjRib);

    EXPECT_EQ(1, messages.size());
    EXPECT_EQ(
        messages[0].first.getMessage(),
        fmt::format(
            "Insert RibDumpReq in pending list for {}", kPeerId1.str()));

    EXPECT_EQ(1, peerMgr_->pendingRibDumpReqs_.size());
    EXPECT_FALSE(peerMgr_->pendingRibDumpReqs_[kPeerId1]);
    EXPECT_EQ(0, peerMgr_->asyncScope_.remaining());

    EXPECT_FALSE(adjRib->isRibDumpScheduled());
  }
}

/**
 * Simple unit test to verify that processRibDumpReq is scheduled
 * on peerMgr's asyncScope after handleBufferedRibDumpReqs is called.
 */
TEST_F(RibInitialAnnouncementTestFixture, HandleBufferedRibDumpReqsTest) {
  auto& evb = peerMgr_->getEventBase();
  peerMgr_->ribInitialAnnouncementStarted_ = true;
  peerMgr_->ribInitialAnnouncementDone_ = true;

  int numPeers = 10;

  // Setup: Populate pendingRibDumpReqs_ with some peerIds.
  BgpPeerId peerId;
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  for (int i = 0; i < numPeers; ++i) {
    peerId = BgpPeerId(folly::IPAddressV4::fromLongHBO(i), i);
    peerMgr_->adjRibs_[peerId] = setupAdjRib(
        evb,
        peerMgr_->getChangeListTracker(),
        peerId,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config);
    peerMgr_->pendingRibDumpReqs_.insert({peerId, i % 2});
  }

  EXPECT_EQ(10, peerMgr_->pendingRibDumpReqs_.size());

  folly::coro::blockingWait(peerMgr_->handleBufferedRibDumpReqs());
  evb.loop();
  folly::coro::blockingWait(peerMgr_->asyncScope_.joinAsync());

  for (const auto& [id, adjRib] : peerMgr_->adjRibs_) {
    EXPECT_FALSE(adjRib->isRibDumpScheduled());
  }

  /*
   * Test cleanup: reset change list consumers to break circular shared_ptr
   * references and stop consumer coroutines, then drain the evb.
   */
  for (auto& [_, adjRib] : peerMgr_->adjRibs_) {
    if (adjRib) {
      adjRib->resetChangeListConsumer();
    }
  }
  evb.loop();
  // The non-update-group buffered drain consumed all pending requests.
  EXPECT_TRUE(peerMgr_->pendingRibDumpReqs_.empty());
  SUCCEED();
}

/*
 * The RibDumpCancellationSource_* tests below unit-test the per-AdjRib rib dump
 * cancellation source APIs that back PeerManagerBase's coalescing and
 * cancellation of scheduled rib dumps, one API per test:
 *   - isRibDumpScheduled
 *   - getCancellationTokenForNewRibDump
 *   - cancelRibDump
 *   - resetRibDumpCancellationSource
 * plus one test for the "superseded dump" interaction between them.
 */

/*
 * isRibDumpScheduled: false with no source, true once a source exists, false
 * again after the source is cleared.
 */
TEST_F(
    RibInitialAnnouncementTestFixture,
    RibDumpCancellationSource_IsRibDumpScheduled) {
  auto& evb = peerMgr_->getEventBase();
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto adjRib = setupAdjRib(
      evb,
      peerMgr_->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);

  EXPECT_FALSE(adjRib->isRibDumpScheduled());

  adjRib->getCancellationTokenForNewRibDump();
  EXPECT_TRUE(adjRib->isRibDumpScheduled());

  adjRib->cancelRibDump();
  EXPECT_FALSE(adjRib->isRibDumpScheduled());

  adjRib->resetChangeListConsumer();
  evb.loop();
}

/*
 * getCancellationTokenForNewRibDump: creates a source (so a dump is now
 * scheduled) and returns a live, un-cancelled token.
 */
TEST_F(
    RibInitialAnnouncementTestFixture,
    RibDumpCancellationSource_GetRibDumpCancellationToken) {
  auto& evb = peerMgr_->getEventBase();
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto adjRib = setupAdjRib(
      evb,
      peerMgr_->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);

  EXPECT_FALSE(adjRib->isRibDumpScheduled());

  auto token = adjRib->getCancellationTokenForNewRibDump();
  EXPECT_TRUE(adjRib->isRibDumpScheduled());
  EXPECT_FALSE(token.isCancellationRequested());

  adjRib->resetChangeListConsumer();
  evb.loop();
}

/*
 * cancelRibDump: cancels the in-flight token AND clears the source. The
 * previously handed-out token still observes the cancellation (the underlying
 * state outlives the source). A second cancel is a no-op.
 */
TEST_F(
    RibInitialAnnouncementTestFixture,
    RibDumpCancellationSource_CancelRibDump) {
  auto& evb = peerMgr_->getEventBase();
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto adjRib = setupAdjRib(
      evb,
      peerMgr_->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);

  auto token = adjRib->getCancellationTokenForNewRibDump();
  EXPECT_TRUE(adjRib->isRibDumpScheduled());

  adjRib->cancelRibDump();
  EXPECT_FALSE(adjRib->isRibDumpScheduled());
  EXPECT_TRUE(token.isCancellationRequested());

  // cancelRibDump is idempotent when no source exists.
  adjRib->cancelRibDump();
  EXPECT_FALSE(adjRib->isRibDumpScheduled());

  adjRib->resetChangeListConsumer();
  evb.loop();
}

/*
 * resetRibDumpCancellationSource: models normal completion. Clears the source
 * (so a future dump can be scheduled) WITHOUT cancelling, so a token handed out
 * before the reset stays un-cancelled.
 */
TEST_F(
    RibInitialAnnouncementTestFixture,
    RibDumpCancellationSource_ResetRibDumpCancellationSource) {
  auto& evb = peerMgr_->getEventBase();
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto adjRib = setupAdjRib(
      evb,
      peerMgr_->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);

  auto token = adjRib->getCancellationTokenForNewRibDump();
  EXPECT_TRUE(adjRib->isRibDumpScheduled());

  adjRib->resetRibDumpCancellationSource();
  EXPECT_FALSE(adjRib->isRibDumpScheduled());
  EXPECT_FALSE(token.isCancellationRequested());

  adjRib->resetChangeListConsumer();
  evb.loop();
}

/*
 * "Don't reset the wrong source": when a dump is superseded, the old token is
 * cancelled and a fresh source is created for the replacement. The old task's
 * completion guard (token.isCancellationRequested()) is what prevents it from
 * overwriting the new source. Replicate that guard here and verify the new
 * source survives.
 */
TEST_F(
    RibInitialAnnouncementTestFixture,
    RibDumpCancellationSource_SupersededKeepsNewerSource) {
  auto& evb = peerMgr_->getEventBase();
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto adjRib = setupAdjRib(
      evb,
      peerMgr_->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);

  auto oldToken = adjRib->getCancellationTokenForNewRibDump();
  adjRib->cancelRibDump(); // supersede: cancels oldToken, clears source
  auto newToken =
      adjRib->getCancellationTokenForNewRibDump(); // create replacement
  EXPECT_TRUE(oldToken.isCancellationRequested());
  EXPECT_FALSE(newToken.isCancellationRequested());
  EXPECT_TRUE(adjRib->isRibDumpScheduled());

  if (!oldToken.isCancellationRequested()) {
    // Superseded task would reach here and reset; the guard above keeps it out.
    adjRib->resetRibDumpCancellationSource();
  }
  EXPECT_TRUE(adjRib->isRibDumpScheduled());
  EXPECT_FALSE(newToken.isCancellationRequested());

  adjRib->resetChangeListConsumer();
  evb.loop();
}

/*
 * scheduleRibDumpForAdjRib coalesces concurrent requests: scheduling a dump for
 * a peer that already has one scheduled (its cancellation source exists and is
 * not cancelled) is a no-op. Two back-to-back schedules create exactly one dump
 * coroutine, so the AdjRib receives the dump (processRibMessage) only once.
 */
TEST_F(
    RibInitialAnnouncementTestFixture,
    ScheduleRibDumpForAdjRib_CoalescesConcurrentRequests) {
  auto& evb = peerMgr_->getEventBase();
  auto mockAdjRib =
      setupMockAdjRib(evb, kPeerId1, AsNum(kAsn1), sessionTerminateBaton_);
  establishSessionForRibDump(mockAdjRib);
  // processRibDumpReqCoro looks the peer up in adjRibs_.
  peerMgr_->adjRibs_[kPeerId1] = mockAdjRib;

  // The coalesced dump must be delivered to the AdjRib exactly once.
  EXPECT_CALL(*mockAdjRib, processRibMessage(testing::_))
      .Times(1)
      .WillOnce(testing::Return());

  EXPECT_FALSE(mockAdjRib->isRibDumpScheduled());
  EXPECT_FALSE(peerMgr_->isRibDumpScheduledForAdjRib(mockAdjRib));

  // First request schedules exactly one dump coroutine.
  peerMgr_->scheduleRibDumpForAdjRib(mockAdjRib);
  EXPECT_TRUE(mockAdjRib->isRibDumpScheduled());
  EXPECT_TRUE(peerMgr_->isRibDumpScheduledForAdjRib(mockAdjRib));
  EXPECT_EQ(1, peerMgr_->asyncScope_.remaining());

  // Second request for the same peer coalesces -> still exactly one coroutine.
  peerMgr_->scheduleRibDumpForAdjRib(mockAdjRib);
  EXPECT_TRUE(mockAdjRib->isRibDumpScheduled());
  EXPECT_EQ(1, peerMgr_->asyncScope_.remaining());

  // Drain the single dump; it completes and clears the source.
  evb.loop();
  folly::coro::blockingWait(peerMgr_->asyncScope_.joinAsync());
  EXPECT_FALSE(mockAdjRib->isRibDumpScheduled());

  // Cleanup.
  mockAdjRib->resetChangeListConsumer();
  evb.loop();
  testing::Mock::VerifyAndClearExpectations(mockAdjRib.get());
}

TEST_F(
    RibInitialAnnouncementTestFixture,
    ScheduleRibDumpForAdjRib_PreservesRouteRefreshMetadata) {
  auto& evb = peerMgr_->getEventBase();
  auto mockAdjRib =
      setupMockAdjRib(evb, kPeerId1, AsNum(kAsn1), sessionTerminateBaton_);
  establishSessionForRibDump(mockAdjRib);
  peerMgr_->adjRibs_[kPeerId1] = mockAdjRib;

  auto v4Announcement = createRibSingleAnnounce(
      kV4Prefix1, kV4Nexthop1, kLocalRouteAs, false, false);
  auto v6Announcement = createRibSingleAnnounce(
      kV6Prefix1, kV6Nexthop1, kLocalRouteAs, false, false);
  peerMgr_->handleShadowRibEntryAnnouncement(
      std::get<RibOutAnnouncement>(v4Announcement));
  peerMgr_->handleShadowRibEntryAnnouncement(
      std::get<RibOutAnnouncement>(v6Announcement));

  EXPECT_CALL(*mockAdjRib, processRibMessage(testing::_))
      .WillOnce([](const RibOutMessage& message) {
        ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(message));
        const auto& announcement = std::get<RibOutAnnouncement>(message);
        EXPECT_TRUE(announcement.routeRefreshAfi.has_value());
        EXPECT_EQ(announcement.routeRefreshAfi, BgpUpdateAfi::AFI_IPv4);
        ASSERT_EQ(announcement.entries.size(), 1);
        EXPECT_EQ(announcement.entries.front().prefix, kV4Prefix1);
      });

  peerMgr_->scheduleRibDumpForAdjRib(
      mockAdjRib,
      RibDumpReq(kPeerId1, false /* sendAddPath */, BgpUpdateAfi::AFI_IPv4));

  evb.loop();
  folly::coro::blockingWait(peerMgr_->asyncScope_.joinAsync());
  EXPECT_FALSE(mockAdjRib->isRibDumpScheduled());

  mockAdjRib->resetChangeListConsumer();
  evb.loop();
  testing::Mock::VerifyAndClearExpectations(mockAdjRib.get());
}

TEST_F(
    RibInitialAnnouncementTestFixture,
    ScheduleRibDumpForAdjRib_DeferredRequestPreservesMetadata) {
  auto& evb = peerMgr_->getEventBase();
  auto mockAdjRib =
      setupMockAdjRib(evb, kPeerId1, AsNum(kAsn1), sessionTerminateBaton_);
  establishSessionForRibDump(mockAdjRib);
  peerMgr_->adjRibs_[kPeerId1] = mockAdjRib;

  auto v4Announcement = createRibSingleAnnounce(
      kV4Prefix1, kV4Nexthop1, kLocalRouteAs, false, false);
  auto v6Announcement = createRibSingleAnnounce(
      kV6Prefix1, kV6Nexthop1, kLocalRouteAs, false, false);
  peerMgr_->handleShadowRibEntryAnnouncement(
      std::get<RibOutAnnouncement>(v4Announcement));
  peerMgr_->handleShadowRibEntryAnnouncement(
      std::get<RibOutAnnouncement>(v6Announcement));

  bool delivered = false;
  EXPECT_CALL(*mockAdjRib, processRibMessage(testing::_))
      .WillOnce([&](const RibOutMessage& message) {
        delivered = true;
        ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(message));
        const auto& announcement = std::get<RibOutAnnouncement>(message);
        EXPECT_TRUE(announcement.routeRefreshAfi.has_value());
        EXPECT_EQ(announcement.routeRefreshAfi, BgpUpdateAfi::AFI_IPv6);
        ASSERT_EQ(announcement.entries.size(), 1);
        EXPECT_EQ(announcement.entries.front().prefix, kV6Prefix1);
      });

  mockAdjRib->testOnlyDeferInitDump = true;
  peerMgr_->scheduleRibDumpForAdjRib(
      mockAdjRib,
      RibDumpReq(kPeerId1, false /* sendAddPath */, BgpUpdateAfi::AFI_IPv6));

  evb.loopOnce();
  EXPECT_FALSE(delivered);
  EXPECT_TRUE(mockAdjRib->isRibDumpScheduled());

  mockAdjRib->testOnlyDeferInitDump = false;
  evb.loop();
  folly::coro::blockingWait(peerMgr_->asyncScope_.joinAsync());
  EXPECT_TRUE(delivered);
  EXPECT_FALSE(mockAdjRib->isRibDumpScheduled());

  mockAdjRib->resetChangeListConsumer();
  evb.loop();
  testing::Mock::VerifyAndClearExpectations(mockAdjRib.get());
}

TEST_F(
    RibInitialAnnouncementTestFixture,
    ScheduleRibDumpForAdjRib_OrdinaryDumpRemainsAllAfi) {
  auto& evb = peerMgr_->getEventBase();
  auto mockAdjRib =
      setupMockAdjRib(evb, kPeerId1, AsNum(kAsn1), sessionTerminateBaton_);
  establishSessionForRibDump(mockAdjRib);
  peerMgr_->adjRibs_[kPeerId1] = mockAdjRib;

  auto v4Announcement = createRibSingleAnnounce(
      kV4Prefix1, kV4Nexthop1, kLocalRouteAs, false, false);
  auto v6Announcement = createRibSingleAnnounce(
      kV6Prefix1, kV6Nexthop1, kLocalRouteAs, false, false);
  peerMgr_->handleShadowRibEntryAnnouncement(
      std::get<RibOutAnnouncement>(v4Announcement));
  peerMgr_->handleShadowRibEntryAnnouncement(
      std::get<RibOutAnnouncement>(v6Announcement));

  EXPECT_CALL(*mockAdjRib, processRibMessage(testing::_))
      .WillOnce([](const RibOutMessage& message) {
        ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(message));
        const auto& announcement = std::get<RibOutAnnouncement>(message);
        EXPECT_FALSE(announcement.routeRefreshAfi.has_value());
        ASSERT_EQ(announcement.entries.size(), 2);
        bool sawV4 = false;
        bool sawV6 = false;
        for (const auto& entry : announcement.entries) {
          sawV4 |= entry.prefix == kV4Prefix1;
          sawV6 |= entry.prefix == kV6Prefix1;
        }
        EXPECT_TRUE(sawV4);
        EXPECT_TRUE(sawV6);
      });

  peerMgr_->scheduleRibDumpForAdjRib(mockAdjRib);

  evb.loop();
  folly::coro::blockingWait(peerMgr_->asyncScope_.joinAsync());
  EXPECT_FALSE(mockAdjRib->isRibDumpScheduled());

  mockAdjRib->resetChangeListConsumer();
  evb.loop();
  testing::Mock::VerifyAndClearExpectations(mockAdjRib.get());
}

TEST_F(
    RibInitialAnnouncementTestFixture,
    ScheduleRibDumpForDetachedBlocked_RegistersMissingConsumer) {
  auto& evb = peerMgr_->getEventBase();
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto adjRib = setupAdjRib(
      evb,
      peerMgr_->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  establishSessionForRibDump(adjRib);
  adjRib->setEnableUpdateGroup(true);
  adjRib->setPeerState(PeerUpdateState::DETACHED_BLOCKED);
  adjRib->resetChangeListConsumer();
  ASSERT_EQ(adjRib->getChangeListConsumer(), nullptr);

  peerMgr_->scheduleRibDumpForAdjRib(adjRib);
  evb.loop();
  folly::coro::blockingWait(peerMgr_->asyncScope_.joinAsync());

  EXPECT_NE(adjRib->getChangeListConsumer(), nullptr);
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::DETACHED_BLOCKED);

  adjRib->resetChangeListConsumer();
  evb.loop();
}

TEST_F(
    RibInitialAnnouncementTestFixture,
    UpdateGroupRouteRefreshCoalescesSameAfiAndRetainsCrossAfi) {
  auto& evb = peerMgr_->getEventBase();
  auto group = std::make_shared<AdjRibOutGroup>(
      evb, "route-refresh-test", 1, true, UpdateGroupKey{});
  auto mockAdjRib = setupUpdateGroupPeer(kPeerId1, group);
  peerMgr_->adjRibs_[kPeerId1] = mockAdjRib;
  mockAdjRib->setPeerState(PeerUpdateState::INIT);

  auto v4Announcement = createRibSingleAnnounce(
      kV4Prefix1, kV4Nexthop1, kLocalRouteAs, false, false);
  auto v6Announcement = createRibSingleAnnounce(
      kV6Prefix1, kV6Nexthop1, kLocalRouteAs, false, false);
  peerMgr_->handleShadowRibEntryAnnouncement(
      std::get<RibOutAnnouncement>(v4Announcement));
  peerMgr_->handleShadowRibEntryAnnouncement(
      std::get<RibOutAnnouncement>(v6Announcement));

  std::vector<folly::CIDRNetwork> replayedPrefixes;
  EXPECT_CALL(*mockAdjRib, processRibMessage(testing::_))
      .Times(2)
      .WillRepeatedly([&](const RibOutMessage& message) {
        ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(message));
        const auto& announcement = std::get<RibOutAnnouncement>(message);
        EXPECT_TRUE(announcement.routeRefreshAfi.has_value());
        ASSERT_EQ(announcement.entries.size(), 1);
        replayedPrefixes.push_back(announcement.entries.front().prefix);
      });

  const auto v4Afi = BgpUpdateAfi::AFI_IPv4;
  peerMgr_->scheduleRouteRefreshForUpdateGroupPeer(mockAdjRib, v4Afi);
  peerMgr_->scheduleRouteRefreshForUpdateGroupPeer(mockAdjRib, v4Afi);
  peerMgr_->scheduleRouteRefreshForUpdateGroupPeer(
      mockAdjRib, BgpUpdateAfi::AFI_IPv6);

  ASSERT_EQ(peerMgr_->pendingRouteRefreshPeers_.size(), 1);
  EXPECT_EQ(peerMgr_->pendingRouteRefreshPeers_.at(mockAdjRib).size(), 2);
  EXPECT_FALSE(mockAdjRib->isRibDumpScheduled());

  mockAdjRib->setPeerState(PeerUpdateState::JOINED_RUNNING);
  group->markPeerInSync(mockAdjRib);
  /* This in-sync peer transitions from JOINED_RUNNING to JOINED_BLOCKED. */
  group->markPeerBlocked(mockAdjRib);
  EXPECT_EQ(mockAdjRib->getPeerState(), PeerUpdateState::JOINED_BLOCKED);
  peerMgr_->scheduleRouteRefreshForUpdateGroupPeer(mockAdjRib, v4Afi);
  EXPECT_FALSE(mockAdjRib->isRibDumpScheduled());
  peerMgr_->maybeSchedulePendingRouteRefresh(mockAdjRib);
  ASSERT_EQ(mockAdjRib->getPeerState(), PeerUpdateState::DETACHED_BLOCKED);
  ASSERT_TRUE(mockAdjRib->isRibDumpScheduled());

  evb.loop();
  folly::coro::blockingWait(peerMgr_->asyncScope_.joinAsync());

  EXPECT_THAT(
      replayedPrefixes, testing::UnorderedElementsAre(kV4Prefix1, kV6Prefix1));
  EXPECT_FALSE(mockAdjRib->isRibDumpScheduled());
  EXPECT_FALSE(peerMgr_->pendingRouteRefreshPeers_.contains(mockAdjRib));

  mockAdjRib->resetChangeListConsumer();
  group->unregisterPeer(mockAdjRib);
  evb.loop();
  testing::Mock::VerifyAndClearExpectations(mockAdjRib.get());
}

TEST_F(
    RibInitialAnnouncementTestFixture,
    UpdateGroupRouteRefreshResumesFromReadyToJoin) {
  auto& evb = peerMgr_->getEventBase();
  auto group = std::make_shared<AdjRibOutGroup>(
      evb, "route-refresh-test", 1, true, UpdateGroupKey{});
  auto mockAdjRib = setupUpdateGroupPeer(kPeerId1, group);
  peerMgr_->adjRibs_[kPeerId1] = mockAdjRib;
  group->markPeerDetached(mockAdjRib);

  const auto v4Afi = BgpUpdateAfi::AFI_IPv4;

  /* The peer is still in INIT, so queue the refresh for later. */
  peerMgr_->scheduleRouteRefreshForUpdateGroupPeer(mockAdjRib, v4Afi);

  mockAdjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  mockAdjRib->setAdjRibFlag(AdjRib::IS_DETACHED_FAST_PEER);

  /* A duplicate must not disturb the ready-to-join recovery state. */
  peerMgr_->scheduleRouteRefreshForUpdateGroupPeer(mockAdjRib, v4Afi);

  EXPECT_EQ(
      mockAdjRib->getPeerState(), PeerUpdateState::DETACHED_READY_TO_JOIN);
  EXPECT_TRUE(mockAdjRib->isAdjRibFlagSet(AdjRib::IS_DETACHED_FAST_PEER));
  EXPECT_FALSE(mockAdjRib->isRibDumpScheduled());

  /* Resume the pending refresh as a detached private replay. */
  peerMgr_->maybeSchedulePendingRouteRefresh(mockAdjRib);

  EXPECT_EQ(mockAdjRib->getPeerState(), PeerUpdateState::DETACHED_RUNNING);
  EXPECT_FALSE(mockAdjRib->isAdjRibFlagSet(AdjRib::IS_DETACHED_FAST_PEER));
  EXPECT_TRUE(mockAdjRib->isRibDumpScheduled());

  peerMgr_->cancelRibDumpForAdjRib(
      mockAdjRib, true /* discardRouteRefreshRequests */);
  group->unregisterPeer(mockAdjRib);
  evb.loop();
  folly::coro::blockingWait(peerMgr_->asyncScope_.joinAsync());
}

TEST_F(
    RibInitialAnnouncementTestFixture,
    QueuedRouteRefreshPreservesReadyToJoinState) {
  auto& evb = peerMgr_->getEventBase();
  auto group = std::make_shared<AdjRibOutGroup>(
      evb, "route-refresh-test", 1, true, UpdateGroupKey{});
  auto mockAdjRib = setupUpdateGroupPeer(kPeerId1, group);
  peerMgr_->adjRibs_[kPeerId1] = mockAdjRib;
  group->markPeerDetached(mockAdjRib);
  mockAdjRib->setPeerState(PeerUpdateState::DETACHED_RUNNING);

  peerMgr_->scheduleRouteRefreshForUpdateGroupPeer(
      mockAdjRib, BgpUpdateAfi::AFI_IPv4);
  ASSERT_TRUE(mockAdjRib->isRibDumpScheduled());

  mockAdjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  mockAdjRib->setAdjRibFlag(AdjRib::IS_DETACHED_FAST_PEER);
  peerMgr_->scheduleRouteRefreshForUpdateGroupPeer(
      mockAdjRib, BgpUpdateAfi::AFI_IPv6);

  EXPECT_EQ(
      mockAdjRib->getPeerState(), PeerUpdateState::DETACHED_READY_TO_JOIN);
  EXPECT_TRUE(mockAdjRib->isAdjRibFlagSet(AdjRib::IS_DETACHED_FAST_PEER));
  EXPECT_THAT(
      peerMgr_->pendingRouteRefreshPeers_.at(mockAdjRib),
      testing::UnorderedElementsAre(
          BgpUpdateAfi::AFI_IPv4, BgpUpdateAfi::AFI_IPv6));

  peerMgr_->cancelRibDumpForAdjRib(
      mockAdjRib, true /* discardRouteRefreshRequests */);
  group->unregisterPeer(mockAdjRib);
  evb.loop();
  folly::coro::blockingWait(peerMgr_->asyncScope_.joinAsync());
}

TEST_F(
    RibInitialAnnouncementTestFixture,
    DetachedInitDumpCompletionRetriesPendingRouteRefreshWithoutEoR) {
  auto& evb = peerMgr_->getEventBase();
  auto group = std::make_shared<AdjRibOutGroup>(
      evb, "route-refresh-test", 2, true, UpdateGroupKey{});
  auto mockAdjRib = setupUpdateGroupPeer(kPeerId1, group);
  auto sibling = setupUpdateGroupPeer(kPeerId2, group);
  peerMgr_->adjRibs_[kPeerId1] = mockAdjRib;
  group->markPeerDetached(mockAdjRib);
  mockAdjRib->setPeerState(PeerUpdateState::DETACHED_INIT_DUMP);

  /* A refresh received during the detached initial dump stays pending. */
  peerMgr_->scheduleRouteRefreshForUpdateGroupPeer(
      mockAdjRib, BgpUpdateAfi::AFI_IPv4);
  ASSERT_TRUE(peerMgr_->pendingRouteRefreshPeers_.contains(mockAdjRib));
  EXPECT_FALSE(mockAdjRib->isRibDumpScheduled());

  /*
   * Draining the detached initial dump emits DetachedInitDumpCompleted after
   * leaving DETACHED_INIT_DUMP; this path does not emit another EoR.
   */
  mockAdjRib->setLastSeenRibVersion(1);
  ASSERT_TRUE(group->hasEntrySharingPeers());
  ASSERT_FALSE(mockAdjRib->isRibDumpScheduled());
  mockAdjRib->transitionPeerUpdateState();
  ASSERT_EQ(
      mockAdjRib->getPeerState(), PeerUpdateState::DETACHED_READY_TO_JOIN);
  ASSERT_EQ(observerQ_.size(), 1);
  auto completion = folly::coro::blockingWait(observerQ_.pop());
  ASSERT_TRUE(
      std::holds_alternative<AdjRib::DetachedInitDumpCompleted>(
          completion.message));

  /* PeerManager uses the completion event to retry the pending refresh. */
  folly::coro::blockingWait(
      peerMgr_->processAdjRibEvent(std::move(completion)));

  ASSERT_TRUE(mockAdjRib->isRibDumpScheduled());
  EXPECT_EQ(mockAdjRib->getPeerState(), PeerUpdateState::DETACHED_RUNNING);
  EXPECT_FALSE(mockAdjRib->isAdjRibFlagSet(AdjRib::IS_DETACHED_FAST_PEER));
  EXPECT_THAT(
      peerMgr_->pendingRouteRefreshPeers_.at(mockAdjRib),
      testing::ElementsAre(BgpUpdateAfi::AFI_IPv4));

  peerMgr_->cancelRibDumpForAdjRib(
      mockAdjRib, true /* discardRouteRefreshRequests */);
  group->unregisterPeer(mockAdjRib);
  group->unregisterPeer(sibling);
  evb.loop();
  folly::coro::blockingWait(peerMgr_->asyncScope_.joinAsync());
}

TEST_F(
    RibInitialAnnouncementTestFixture,
    PendingRouteRefreshRetainedWithoutUpdateGroup) {
  auto& evb = peerMgr_->getEventBase();
  auto mockAdjRib =
      setupMockAdjRib(evb, kPeerId1, AsNum(kAsn1), sessionTerminateBaton_);
  establishSessionForRibDump(mockAdjRib);

  mockAdjRib->setEnableUpdateGroup(true);
  mockAdjRib->setPeerState(PeerUpdateState::INIT);
  mockAdjRib->setUpdateGroup(
      std::make_shared<AdjRibOutGroup>(
          evb, "route-refresh-test", 1, true, UpdateGroupKey{}));
  peerMgr_->scheduleRouteRefreshForUpdateGroupPeer(
      mockAdjRib, BgpUpdateAfi::AFI_IPv4);

  mockAdjRib->setPeerState(PeerUpdateState::DETACHED_RUNNING);
  mockAdjRib->setUpdateGroup(nullptr);
  peerMgr_->maybeSchedulePendingRouteRefresh(mockAdjRib);

  ASSERT_TRUE(peerMgr_->pendingRouteRefreshPeers_.contains(mockAdjRib));
  EXPECT_THAT(
      peerMgr_->pendingRouteRefreshPeers_.at(mockAdjRib),
      testing::ElementsAre(BgpUpdateAfi::AFI_IPv4));
  EXPECT_FALSE(mockAdjRib->isRibDumpScheduled());

  peerMgr_->cancelRibDumpForAdjRib(
      mockAdjRib, true /* discardRouteRefreshRequests */);
}

TEST_F(
    RibInitialAnnouncementTestFixture,
    CancelRibDumpForAdjRib_DiscardsDeferredRouteRefresh) {
  auto& evb = peerMgr_->getEventBase();
  auto group = std::make_shared<AdjRibOutGroup>(
      evb, "route-refresh-test", 1, true, UpdateGroupKey{});
  auto mockAdjRib = setupUpdateGroupPeer(kPeerId1, group);
  peerMgr_->adjRibs_[kPeerId1] = mockAdjRib;
  group->markPeerDetached(mockAdjRib);
  mockAdjRib->setPeerState(PeerUpdateState::DETACHED_BLOCKED);
  EXPECT_CALL(*mockAdjRib, processRibMessage(testing::_)).Times(0);

  peerMgr_->scheduleRouteRefreshForUpdateGroupPeer(
      mockAdjRib, BgpUpdateAfi::AFI_IPv4);
  peerMgr_->scheduleRouteRefreshForUpdateGroupPeer(
      mockAdjRib, BgpUpdateAfi::AFI_IPv6);
  ASSERT_TRUE(peerMgr_->pendingRouteRefreshPeers_.contains(mockAdjRib));
  EXPECT_TRUE(mockAdjRib->isRibDumpScheduled());
  EXPECT_EQ(peerMgr_->pendingRouteRefreshPeers_.at(mockAdjRib).size(), 2);

  peerMgr_->cancelRibDumpForAdjRib(
      mockAdjRib, true /* discardRouteRefreshRequests */);

  EXPECT_FALSE(peerMgr_->pendingRouteRefreshPeers_.contains(mockAdjRib));
  EXPECT_FALSE(mockAdjRib->isRibDumpScheduled());

  peerMgr_->pendingRouteRefreshPeers_[mockAdjRib];
  peerMgr_->cancelRibDumpForAdjRib(mockAdjRib);
  EXPECT_FALSE(peerMgr_->pendingRouteRefreshPeers_.contains(mockAdjRib));

  group->unregisterPeer(mockAdjRib);
  evb.loop();
  folly::coro::blockingWait(peerMgr_->asyncScope_.joinAsync());
  testing::Mock::VerifyAndClearExpectations(mockAdjRib.get());
}

/*
 * schedule -> cancel -> reschedule leaves two coroutines on asyncScope_ (the
 * cancelled one is still queued, plus the rescheduled one), but the AdjRib
 * receives the dump only once: when the cancelled coroutine runs it observes
 * its cancelled token and skips delivery; only the rescheduled coroutine
 * delivers.
 */
TEST_F(
    RibInitialAnnouncementTestFixture,
    ScheduleRibDumpForAdjRib_RescheduleAfterCancelDumpsOnce) {
  auto& evb = peerMgr_->getEventBase();
  auto mockAdjRib =
      setupMockAdjRib(evb, kPeerId1, AsNum(kAsn1), sessionTerminateBaton_);
  establishSessionForRibDump(mockAdjRib);
  peerMgr_->adjRibs_[kPeerId1] = mockAdjRib;

  /*
   * Despite two scheduled coroutines, only the rescheduled (non-cancelled) one
   * delivers the dump.
   */
  EXPECT_CALL(*mockAdjRib, processRibMessage(testing::_))
      .Times(1)
      .WillOnce([](const RibOutMessage& message) {
        ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(message));
        EXPECT_FALSE(
            std::get<RibOutAnnouncement>(message).routeRefreshAfi.has_value());
      });

  // Schedule an RR dump -> one coroutine.
  peerMgr_->scheduleRibDumpForAdjRib(
      mockAdjRib,
      RibDumpReq(kPeerId1, false /* sendAddPath */, BgpUpdateAfi::AFI_IPv4));
  EXPECT_TRUE(mockAdjRib->isRibDumpScheduled());
  EXPECT_EQ(1, peerMgr_->asyncScope_.remaining());

  /*
   * Cancel: the source is cancelled and cleared, but the already-queued
   * coroutine stays on asyncScope_.
   */
  mockAdjRib->cancelRibDump();
  EXPECT_FALSE(mockAdjRib->isRibDumpScheduled());
  EXPECT_EQ(1, peerMgr_->asyncScope_.remaining());

  // Reschedule an ordinary dump -> two tasks are now pending.
  peerMgr_->scheduleRibDumpForAdjRib(mockAdjRib);
  EXPECT_TRUE(mockAdjRib->isRibDumpScheduled());
  EXPECT_EQ(2, peerMgr_->asyncScope_.remaining());

  /*
   * Drain both: the cancelled coroutine skips delivery, the rescheduled one
   * delivers -> processRibMessage is called exactly once (EXPECT_CALL above).
   */
  evb.loop();
  folly::coro::blockingWait(peerMgr_->asyncScope_.joinAsync());
  EXPECT_FALSE(mockAdjRib->isRibDumpScheduled());

  // Cleanup.
  mockAdjRib->resetChangeListConsumer();
  evb.loop();
  testing::Mock::VerifyAndClearExpectations(mockAdjRib.get());
}

/*
 * Coalescing spans both states: two consecutive maybeBufferRibDumpReq calls
 * buffer a single request, then two consecutive scheduleRibDumpForAdjRib calls
 * coalesce against that buffered request (isRibDumpScheduledForAdjRib already
 * true) and add nothing to asyncScope_. Draining the buffer then delivers the
 * dump to the AdjRib exactly once.
 */
TEST_F(
    RibInitialAnnouncementTestFixture,
    ScheduleRibDumpForAdjRib_BufferedThenScheduledDumpsOnce) {
  auto& evb = peerMgr_->getEventBase();
  /*
   * Done: maybeBufferRibDumpForDetachedPeer buffers the request and schedules a
   * single handleBufferedRibDumpsForDetachedPeers drain on asyncScope_.
   */
  peerMgr_->ribInitialAnnouncementStarted_ = true;
  peerMgr_->ribInitialAnnouncementDone_ = true;

  auto mockAdjRib =
      setupMockAdjRib(evb, kPeerId1, AsNum(kAsn1), sessionTerminateBaton_);
  establishSessionForRibDump(mockAdjRib);
  peerMgr_->adjRibs_[kPeerId1] = mockAdjRib;

  // The dump must be delivered to the AdjRib exactly once across both states.
  EXPECT_CALL(*mockAdjRib, processRibMessage(testing::_))
      .Times(1)
      .WillOnce(testing::Return());

  /*
   * Two consecutive buffers -> the F14 set dedups to a single buffered request,
   * and a single drain task is scheduled on asyncScope_.
   */
  peerMgr_->maybeBufferRibDumpForDetachedPeer(mockAdjRib);
  peerMgr_->maybeBufferRibDumpForDetachedPeer(mockAdjRib);
  EXPECT_EQ(1, peerMgr_->pendingRibDumpAdjRibs_.size());
  EXPECT_EQ(1, peerMgr_->asyncScope_.remaining());
  // Buffering does not arm the AdjRib's own cancellation source.
  EXPECT_FALSE(mockAdjRib->isRibDumpScheduled());
  // A buffered request already counts as scheduled for the peer.
  EXPECT_TRUE(peerMgr_->isRibDumpScheduledForAdjRib(mockAdjRib));

  /*
   * Two consecutive schedules -> both coalesce against the buffered request
   * (isRibDumpScheduledForAdjRib is already true), so no additional asyncScope_
   * task is created and the peer stays buffered.
   */
  peerMgr_->scheduleRibDumpForAdjRib(mockAdjRib);
  peerMgr_->scheduleRibDumpForAdjRib(mockAdjRib);
  EXPECT_EQ(1, peerMgr_->pendingRibDumpAdjRibs_.size());
  EXPECT_EQ(1, peerMgr_->asyncScope_.remaining());
  // The coalesced schedules short-circuited, so the source is still unarmed.
  EXPECT_FALSE(mockAdjRib->isRibDumpScheduled());
  EXPECT_TRUE(peerMgr_->isRibDumpScheduledForAdjRib(mockAdjRib));

  /*
   * Run the scheduled drain -> it delivers to the AdjRib exactly once
   * (EXPECT_CALL above), and pendingRibDumpAdjRibs_ is emptied.
   */
  evb.loop();
  folly::coro::blockingWait(peerMgr_->asyncScope_.joinAsync());
  EXPECT_TRUE(peerMgr_->pendingRibDumpAdjRibs_.empty());
  EXPECT_FALSE(peerMgr_->isRibDumpScheduledForAdjRib(mockAdjRib));

  // Cleanup.
  mockAdjRib->resetChangeListConsumer();
  evb.loop();
  testing::Mock::VerifyAndClearExpectations(mockAdjRib.get());
}

/*
 * cancelRibDumpForAdjRib removes a buffered request: after buffering via
 * maybeBufferRibDumpForDetachedPeer, cancelling drops the request from
 * pendingRibDumpAdjRibs_, so a subsequent drain delivers nothing.
 */
TEST_F(
    RibInitialAnnouncementTestFixture,
    CancelRibDumpForAdjRib_RemovesBufferedRequest) {
  auto& evb = peerMgr_->getEventBase();
  /*
   * Done: maybeBufferRibDumpForDetachedPeer buffers the request and schedules a
   * handleBufferedRibDumpsForDetachedPeers drain on asyncScope_.
   */
  peerMgr_->ribInitialAnnouncementStarted_ = true;
  peerMgr_->ribInitialAnnouncementDone_ = true;

  auto mockAdjRib =
      setupMockAdjRib(evb, kPeerId1, AsNum(kAsn1), sessionTerminateBaton_);
  establishSessionForRibDump(mockAdjRib);
  peerMgr_->adjRibs_[kPeerId1] = mockAdjRib;

  // A cancelled buffered request must never be delivered.
  EXPECT_CALL(*mockAdjRib, processRibMessage(testing::_)).Times(0);

  // Buffer the request (also schedules the drain on asyncScope_).
  peerMgr_->maybeBufferRibDumpForDetachedPeer(mockAdjRib);
  EXPECT_EQ(1, peerMgr_->pendingRibDumpAdjRibs_.size());
  EXPECT_TRUE(peerMgr_->isRibDumpScheduledForAdjRib(mockAdjRib));

  // Cancel -> the buffered request is removed.
  peerMgr_->cancelRibDumpForAdjRib(mockAdjRib);
  EXPECT_TRUE(peerMgr_->pendingRibDumpAdjRibs_.empty());
  EXPECT_FALSE(peerMgr_->isRibDumpScheduledForAdjRib(mockAdjRib));

  /*
   * The scheduled drain now finds an empty set and delivers nothing
   * (EXPECT_CALL Times(0) above).
   */
  evb.loop();
  folly::coro::blockingWait(peerMgr_->asyncScope_.joinAsync());
  EXPECT_TRUE(peerMgr_->pendingRibDumpAdjRibs_.empty());

  // Cleanup.
  mockAdjRib->resetChangeListConsumer();
  evb.loop();
  testing::Mock::VerifyAndClearExpectations(mockAdjRib.get());
}

/**
 * Update-group equivalent of
 * MaybeBufferRibDumpReqTest_SessionUpAfterRibInitialAnnouncementDone: a
 * detached peer coming up after initial announcement is done is buffered in
 * pendingRibDumpAdjRibs_ and a drain task is scheduled.
 */
TEST_F(
    RibInitialAnnouncementTestFixture,
    MaybeBufferRibDumpForDetachedPeerTest_SessionUpAfterRibAnnouncementDone) {
  auto& evb = peerMgr_->getEventBase();
  peerMgr_->ribInitialAnnouncementStarted_ = true;
  peerMgr_->ribInitialAnnouncementDone_ = true;

  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  peerMgr_->adjRibs_[kPeerId1] = setupAdjRib(
      evb,
      peerMgr_->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);

  auto& adjRib = peerMgr_->adjRibs_[kPeerId1];
  auto& messages = subscribeToLogMessages("");
  messages.clear();

  peerMgr_->maybeBufferRibDumpForDetachedPeer(adjRib);

  EXPECT_EQ(1, messages.size());
  EXPECT_EQ(
      messages[0].first.getMessage(),
      fmt::format("Insert RibDumpReq in pending list for {}", kPeerId1.str()));

  /*
   * Initial announcement is done, so the AdjRib is buffered in
   * pendingRibDumpAdjRibs_ and a single handleBufferedRibDumpsForDetachedPeers
   * task is scheduled on asyncScope_.
   */
  EXPECT_EQ(1, peerMgr_->pendingRibDumpAdjRibs_.size());
  EXPECT_TRUE(peerMgr_->pendingRibDumpAdjRibs_.count(adjRib));
  EXPECT_EQ(1, peerMgr_->asyncScope_.remaining());
  EXPECT_TRUE(peerMgr_->isRibDumpScheduledForAdjRib(adjRib));

  // Test cleanup.
  evb.loop();
  folly::coro::blockingWait(peerMgr_->asyncScope_.joinAsync());
}

/**
 * A detached peer coming up while initial announcement is still in progress
 * (before DONE) is NOT buffered: it waits for its update group's shadow-RIB
 * walk, which markRibInitialAnnouncementDone() triggers. Buffering it early
 * would let the entry be drained later by a post-DONE peer, giving this peer a
 * spurious independent dump on top of the group's initial walk.
 */
TEST_F(
    RibInitialAnnouncementTestFixture,
    MaybeBufferRibDumpForDetachedPeerTest_SessionUpDuringRibAnnouncement) {
  auto& evb = peerMgr_->getEventBase();
  peerMgr_->ribInitialAnnouncementStarted_ = true;
  peerMgr_->ribInitialAnnouncementDone_ = false;

  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  peerMgr_->adjRibs_[kPeerId1] = setupAdjRib(
      evb,
      peerMgr_->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);

  auto& adjRib = peerMgr_->adjRibs_[kPeerId1];
  auto& messages = subscribeToLogMessages("");
  messages.clear();

  peerMgr_->maybeBufferRibDumpForDetachedPeer(adjRib);

  EXPECT_EQ(1, messages.size());
  EXPECT_EQ(
      messages[0].first.getMessage(),
      fmt::format(
          "Waiting for group RIB walk for {}; Rib has not finished initial announcement.",
          kPeerId1.str()));

  /*
   * Initial announcement is not done yet, so nothing is buffered, no drain task
   * is scheduled, the peer waits for its update group's initial shadow-RIB
   * walk.
   */
  EXPECT_EQ(0, peerMgr_->pendingRibDumpAdjRibs_.size());
  EXPECT_EQ(0, peerMgr_->asyncScope_.remaining());
}

/**
 * Update-group equivalent of HandleBufferedRibDumpReqsTest: the buffered
 * detached-peer RibDumpReqs in pendingRibDumpAdjRibs_ are drained and the set
 * is emptied.
 */
TEST_F(
    RibInitialAnnouncementTestFixture,
    HandleBufferedRibDumpsForDetachedPeersTest) {
  auto& evb = peerMgr_->getEventBase();
  peerMgr_->ribInitialAnnouncementStarted_ = true;
  peerMgr_->ribInitialAnnouncementDone_ = true;

  int numPeers = 10;

  // Setup: Populate pendingRibDumpAdjRibs_ with some detached peers' AdjRibs.
  BgpPeerId peerId;
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  for (int i = 0; i < numPeers; ++i) {
    peerId = BgpPeerId(folly::IPAddressV4::fromLongHBO(i), i);
    peerMgr_->adjRibs_[peerId] = setupAdjRib(
        evb,
        peerMgr_->getChangeListTracker(),
        peerId,
        AsNum(kAsn1),
        sessionTerminateBaton_,
        config);
    peerMgr_->pendingRibDumpAdjRibs_.insert(peerMgr_->adjRibs_[peerId]);
  }

  EXPECT_EQ(numPeers, peerMgr_->pendingRibDumpAdjRibs_.size());
  folly::coro::blockingWait(peerMgr_->handleBufferedRibDumpsForDetachedPeers());

  // All buffered detached dumps are drained.
  EXPECT_TRUE(peerMgr_->pendingRibDumpAdjRibs_.empty());

  /*
   * Test cleanup: reset change list consumers to break circular shared_ptr
   * references and stop consumer coroutines, then drain the evb.
   */
  for (auto& [_, adjRib] : peerMgr_->adjRibs_) {
    if (adjRib) {
      adjRib->resetChangeListConsumer();
    }
  }
  evb.loop();
  SUCCEED();
}

TEST_F(
    RibInitialAnnouncementTestFixture,
    SessionTerminatedWithPendingRibDumpReqTest) {
  auto& evb = peerMgr_->getEventBase();
  auto& fm = folly::fibers::getFiberManager(peerMgr_->getEventBase(), options_);

  FiberBgpPeer::ObservableStateT stateEvent{
      .peerId = kPeerId3,
      .versionNumber = version_,
      .remoteAs = mockInfo1_.peeringParams.remoteAs,
      .sessionInfo = sessionInfo_};

  // Indicate that RIB initial announcement is in progress.
  peerMgr_->ribInitialAnnouncementStarted_ = true;
  peerMgr_->ribInitialAnnouncementDone_ = false;

  fm.addTask([&] {
    EXPECT_EQ(0, peerMgr_->pendingRibDumpReqs_.size());
    folly::coro::blockingWait(peerMgr_->sessionEstablished(stateEvent));
    /**
     * Rib has not started initial announcement.
     * and adjRib is marked as having established session before
     * Rib initial announcement.
     */
    EXPECT_FALSE(peerMgr_->adjRibs_[kPeerId3]->inInitialAnnouncement());

    EXPECT_EQ(1, peerMgr_->pendingRibDumpReqs_.size());
    /*
     * No dump is scheduled because ribInitialAnnouncementDone_ is false, so
     * handleBufferedRibDumpReqs is not yet scheduled.
     */
    EXPECT_FALSE(peerMgr_->adjRibs_[kPeerId3]->isRibDumpScheduled());

    // Verify ODS counter matches pendingRibDumpReqs_ size after increment
    auto tcData = fb303::ThreadCachedServiceData::get();
    tcData->publishStats();
    EXPECT_EQ(1, tcData->getCounter(BgpStats::kPendingRibDumpReqsCount));

    folly::coro::blockingWait(peerMgr_->sessionTerminated(stateEvent));

    EXPECT_EQ(0, peerMgr_->pendingRibDumpReqs_.size());

    // Verify ODS counter decremented after sessionTerminated
    tcData->publishStats();
    EXPECT_EQ(0, tcData->getCounter(BgpStats::kPendingRibDumpReqsCount));

    cleanUp();
  });
  evb.loop();
  SUCCEED();
}

TEST_F(
    RibInitialAnnouncementTestFixture,
    SessionTerminatedWithoutPendingRibDumpReqTest) {
  auto& evb = peerMgr_->getEventBase();
  auto& fm = folly::fibers::getFiberManager(peerMgr_->getEventBase(), options_);

  FiberBgpPeer::ObservableStateT stateEvent{
      .peerId = kPeerId3,
      .versionNumber = version_,
      .remoteAs = mockInfo1_.peeringParams.remoteAs,
      .sessionInfo = sessionInfo_};

  peerMgr_->ribInitialAnnouncementStarted_ = true;
  peerMgr_->ribInitialAnnouncementDone_ = true;

  // Add some fake entries into pendingRibDumpReqs_.
  int numPeers = 5;
  BgpPeerId peerId;
  for (int i = 0; i < numPeers; ++i) {
    peerId = BgpPeerId(folly::IPAddressV4::fromLongHBO(i), i);
    peerMgr_->pendingRibDumpReqs_.insert({peerId, i % 2});
  }

  fm.addTask([&] {
    EXPECT_EQ(numPeers, peerMgr_->pendingRibDumpReqs_.size());

    // One more pending ribDumpReq added
    folly::coro::blockingWait(peerMgr_->sessionEstablished(stateEvent));
    numPeers++;
    EXPECT_EQ(numPeers, peerMgr_->pendingRibDumpReqs_.size());

    // One pending ribDumpReq should be removed
    folly::coro::blockingWait(peerMgr_->sessionTerminated(stateEvent));
    numPeers--;
    EXPECT_EQ(numPeers, peerMgr_->pendingRibDumpReqs_.size());

    cleanUp();
  });
  evb.loop();
  SUCCEED();
}

/**
 * Invariant: peer should have two announcements and
 * one EOR queued in the boundedAdjRibOutQ_ even if the peer
 * comes up before RIB starts announcing initial dump.
 */
CO_TEST_F(
    RibInitialAnnouncementTestFixture,
    PeerSessionEstablishedBeforeRibInitialAnnouncementTest) {
  /**
   * Initial checks that Rib initial announcement hasn't started.
   */
  EXPECT_FALSE(peerMgr_->ribInitialAnnouncementStarted_);
  EXPECT_FALSE(peerMgr_->ribInitialAnnouncementDone_);

  auto& evb = peerMgr_->getEventBase();
  auto keepAlive = folly::Executor::getKeepAliveToken(evb);
  std::thread evbThread([&]() { evb.loop(); });

  FiberBgpPeer::ObservableStateT stateEvent{
      .peerId = kPeerId3,
      .versionNumber = version_,
      .remoteAs = mockInfo1_.peeringParams.remoteAs,
      .sessionInfo = sessionInfo_};

  co_await peerMgr_->sessionEstablished(stateEvent);

  /**
   * Rib has not started initial announcement.
   * and adjRib is marked as having established session before
   * Rib initial announcement.
   */
  EXPECT_TRUE(peerMgr_->adjRibs_[kPeerId3]->inInitialAnnouncement());

  peerMgr_->asyncScope_.add(
      co_withExecutor(&evb, peerMgr_->processRibOutMsgLoop()));

  // Simulate RIB starting initial dump with two RibOutAnnouncements.
  ribOutQ_.push(RibInitialAnnouncementStart{});

  ribOutQ_.push(createRibInitialSingleAnnounce(
      kV4Prefix1,
      kV4Nexthop1,
      kLocalRouteAs,
      false /* sendWithEoR */,
      false /* sendAddPath */));

  ribOutQ_.push(createRibInitialSingleAnnounce(
      kV4Prefix2,
      kV4Nexthop2,
      kLocalRouteAs,
      true /* sendWithEoR */,
      false /* sendAddPath */));

  // Expect 2 announcements and 1 EOR.
  co_await verifyTwoAnnouncementsWithEoR();

  cleanUp();

  /*
   * Release the keep-alive token. The EVB will exit loop() once all
   * remaining work (AdjRib teardown, etc.) completes.
   */
  keepAlive.reset();
  evbThread.join();
}

/**
 * Invariant: peer should have two announcements and
 * one EOR queued in the boundedAdjRibOutQ_ even if the session
 * comes up for the first time during RIB initial dump.
 */
CO_TEST_F(
    RibInitialAnnouncementTestFixture,
    PeerSessionEstablishedDuringRibInitialAnnouncementTest) {
  /**
   * Initial checks that Rib initial announcement hasn't started.
   */
  EXPECT_FALSE(peerMgr_->ribInitialAnnouncementStarted_);
  EXPECT_FALSE(peerMgr_->ribInitialAnnouncementDone_);

  auto& evb = peerMgr_->getEventBase();
  auto keepAlive = folly::Executor::getKeepAliveToken(evb);

  std::thread evbThread([&]() { evb.loop(); });

  peerMgr_->asyncScope_.add(
      co_withExecutor(&evb, peerMgr_->processRibOutMsgLoop()));

  FiberBgpPeer::ObservableStateT stateEvent{
      .peerId = kPeerId3,
      .versionNumber = version_,
      .remoteAs = mockInfo1_.peeringParams.remoteAs,
      .sessionInfo = sessionInfo_};

  // Simulate RIB starting initial dump with two RibOutAnnouncements.
  ribOutQ_.push(RibInitialAnnouncementStart{});

  ribOutQ_.push(createRibInitialSingleAnnounce(
      kV4Prefix1,
      kV4Nexthop1,
      kLocalRouteAs,
      false /* sendWithEoR */,
      false /* sendAddPath */));

  /*
   * Wait for ribOutQ_ to drain, then synchronize with the EVB thread to
   * ensure processRibOutMsgLoop has finished processing and is suspended.
   * runInEventBaseThreadAndWait provides a happens-before guarantee,
   * preventing data races when sessionEstablished accesses PeerManagerBase
   * state (adjRibs_, pendingRibDumpReqs_, etc.) from this thread.
   */
  while (!ribOutQ_.empty()) {
    co_await folly::coro::sleep(std::chrono::milliseconds(1));
  }
  evb.runInEventBaseThreadAndWait([]() {});

  co_await peerMgr_->sessionEstablished(stateEvent);

  /**
   * Rib has started initial announcement.
   * and adjRib is marked as having established session during
   * Rib initial announcement.
   */
  EXPECT_FALSE(peerMgr_->adjRibs_[kPeerId3]->inInitialAnnouncement());

  ribOutQ_.push(createRibInitialSingleAnnounce(
      kV4Prefix2,
      kV4Nexthop2,
      kLocalRouteAs,
      true /* sendWithEoR */,
      false /* sendAddPath */));

  // Expect 2 announcements and 1 EOR.
  co_await verifyTwoAnnouncementsWithEoR();

  // Expect that adjRib->inInitialAnnouncement was set to false.
  auto adjRib = peerMgr_->findAdjRib(kPeerId3);
  EXPECT_FALSE(adjRib->inInitialAnnouncement());

  cleanUp();

  /*
   * Release the keep-alive token. The EVB will exit loop() once all
   * remaining work (AdjRib teardown, etc.) completes.
   */
  keepAlive.reset();
  evbThread.join();
}

/**
 * Invariant: peer should have two announcements and
 * one EOR queued in the boundedAdjRibOutQ_ even if
 * the session flaps and comes back up, with both
 * sessionEstablished calls DURING RIB initial announcement.
 */
CO_TEST_F(
    RibInitialAnnouncementTestFixture,
    PeerSessionFlapsDuringRibInitialAnnouncementTest) {
  /**
   * Initial checks that Rib initial announcement hasn't started.
   */
  EXPECT_FALSE(peerMgr_->ribInitialAnnouncementStarted_);
  EXPECT_FALSE(peerMgr_->ribInitialAnnouncementDone_);

  auto& evb = peerMgr_->getEventBase();
  auto keepAlive = folly::Executor::getKeepAliveToken(evb);

  std::thread evbThread([&]() { evb.loop(); });

  peerMgr_->asyncScope_.add(
      co_withExecutor(&evb, peerMgr_->processRibOutMsgLoop()));

  FiberBgpPeer::ObservableStateT stateEvent{
      .peerId = kPeerId3,
      .versionNumber = version_,
      .remoteAs = mockInfo1_.peeringParams.remoteAs,
      .sessionInfo = sessionInfo_};

  // Simulate RIB starting initial dump.
  ribOutQ_.push(RibInitialAnnouncementStart{});

  // Wait for ribOutQ_ to drain, then synchronize with the EVB thread.
  while (!ribOutQ_.empty()) {
    co_await folly::coro::sleep(std::chrono::milliseconds(1));
  }
  evb.runInEventBaseThreadAndWait([]() {});

  co_await co_withExecutor(&evb, peerMgr_->sessionEstablished(stateEvent));
  /**
   * Rib has started initial announcement.
   * and adjRib is marked as having established session during
   * Rib initial announcement.
   */
  auto adjRib = peerMgr_->findAdjRib(kPeerId3);
  EXPECT_FALSE(adjRib->inInitialAnnouncement());
  // There should be ONE pending rib dump req for this peer.
  EXPECT_EQ(1, peerMgr_->pendingRibDumpReqs_.size());

  ribOutQ_.push(createRibInitialSingleAnnounce(
      kV4Prefix1,
      kV4Nexthop1,
      kLocalRouteAs,
      false /* sendWithEoR */,
      false /* sendAddPath */));

  /*
   * Drain ribOutQ_ and synchronize with the EVB thread so this announcement is
   * distributed to the adjRib before the session flaps, and so the EVB thread
   * is quiesced before the EXPECT_* reads below observe shared state.
   */
  while (!ribOutQ_.empty()) {
    co_await folly::coro::sleep(std::chrono::milliseconds(1));
  }
  evb.runInEventBaseThreadAndWait([]() {});

  // Bring the session down and then back up.
  co_await adjRibInQ_->push(
      FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});

  // Simulate FiberBgpPeerManager increasing version
  stateEvent.versionNumber = ++version_;

  sessionInfo_->currentVersion = std::make_shared<VersionNumber>(version_);
  co_await co_withExecutor(&evb, peerMgr_->sessionTerminated(stateEvent));
  EXPECT_FALSE(adjRib->inInitialAnnouncement());
  EXPECT_FALSE(adjRib->isStateEstablished());
  // There should be 0 pending rib dump reqs for this peer.
  EXPECT_EQ(0, peerMgr_->pendingRibDumpReqs_.size());
  /*
   * Restore closed adjRibOutQueue_ queue which was closed
   * during sessionTerminated.
   */
  refreshSessionInfo();
  // Update stateEvent to use the new sessionInfo with the new queue
  stateEvent.sessionInfo = sessionInfo_;

  // Bring peer back up.
  co_await co_withExecutor(&evb, peerMgr_->sessionEstablished(stateEvent));
  EXPECT_TRUE(adjRib->isStateEstablished());

  /*
   * Rib is still in initial announcement, so inInitialAnnouncement=false
   * indicates the session came up during initial announcement.
   */
  EXPECT_FALSE(adjRib->inInitialAnnouncement());
  // There should be 1 pending rib dump req for this peer.
  EXPECT_EQ(1, peerMgr_->pendingRibDumpReqs_.size());

  ribOutQ_.push(createRibInitialSingleAnnounce(
      kV4Prefix2,
      kV4Nexthop2,
      kLocalRouteAs,
      true /* sendWithEoR */,
      false /* sendAddPath */));

  // Expect 2 announcements and 1 EOR.
  co_await verifyTwoAnnouncementsWithEoR();

  // Expect that adjRib->inInitialAnnouncement was set to false.
  EXPECT_FALSE(adjRib->inInitialAnnouncement());

  cleanUp();

  /*
   * Release the keep-alive token. The EVB will exit loop() once all
   * remaining work (AdjRib teardown, etc.) completes.
   */
  keepAlive.reset();
  evbThread.join();
}

/**
 * Invariant: peer should have two announcements and
 * one EOR queued in the boundedAdjRibOutQ_ even if the
 * peer session is established after RIB's
 * initial dump.
 */
CO_TEST_F(
    RibInitialAnnouncementTestFixture,
    PeerSessionEstablishedAfterRibInitialAnnouncementTest) {
  /**
   * Initial checks that Rib initial announcement hasn't started.
   */
  EXPECT_FALSE(peerMgr_->ribInitialAnnouncementStarted_);
  EXPECT_FALSE(peerMgr_->ribInitialAnnouncementDone_);

  auto& evb = peerMgr_->getEventBase();
  auto keepAlive = folly::Executor::getKeepAliveToken(evb);

  std::thread evbThread([&]() { evb.loop(); });

  peerMgr_->asyncScope_.add(
      co_withExecutor(&evb, peerMgr_->processRibOutMsgLoop()));

  FiberBgpPeer::ObservableStateT stateEvent{
      .peerId = kPeerId3,
      .versionNumber = version_,
      .remoteAs = mockInfo1_.peeringParams.remoteAs,
      .sessionInfo = sessionInfo_};

  // Simulate RIB starting initial dump with two RibOutAnnouncements.
  ribOutQ_.push(RibInitialAnnouncementStart{});

  ribOutQ_.push(createRibInitialSingleAnnounce(
      kV4Prefix1,
      kV4Nexthop1,
      kLocalRouteAs,
      false /* sendWithEoR */,
      false /* sendAddPath */));

  ribOutQ_.push(createRibInitialSingleAnnounce(
      kV4Prefix2,
      kV4Nexthop2,
      kLocalRouteAs,
      true /* sendWithEoR */,
      false /* sendAddPath */));

  // Verify that all rib out messages were processed.
  while (!ribOutQ_.empty()) {
    co_await folly::coro::sleep(std::chrono::milliseconds(1));
  }
  evb.runInEventBaseThreadAndWait([]() {});

  co_await co_withExecutor(&evb, peerMgr_->sessionEstablished(stateEvent));

  /**
   * Rib finished initial announcement.
   * and adjRib is marked as having established session after
   * Rib initial announcement.
   */
  auto adjRib = peerMgr_->findAdjRib(kPeerId3);
  EXPECT_TRUE(adjRib);
  EXPECT_FALSE(adjRib->inInitialAnnouncement());

  // Expect 2 announcements and 1 EOR.
  co_await verifyTwoAnnouncementsWithEoR();

  // Expect that adjRib->inInitialAnnouncement was set to false.
  EXPECT_FALSE(adjRib->inInitialAnnouncement());

  cleanUp();

  /*
   * Release the keep-alive token. The EVB will exit loop() once all
   * remaining work (AdjRib teardown, etc.) completes.
   */
  keepAlive.reset();
  evbThread.join();
}

/*
 * ReplicateRibMessageTest was removed because it tested the toAdjRibQ_ path
 * (enableChangeListTracker_ = false), which is now dead code. The changeList
 * path is unconditionally enabled and tested via other tests.
 */

TEST_F(PeerManagerTestFixture, ReplicateRibMessageInitialadjRibsTest) {
  /*
   * Step 0: test setup
   */

  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      std::make_shared<ConfigManager>(config),
      nullptr,
      ribInQ_,
      ribOutQ_,
      nbrRouteChangeQ_);

  auto& evb = peerMgr->getEventBase();
  auto& fm = folly::fibers::getFiberManager(peerMgr->getEventBase(), options_);

  auto adjRibInQ1 = std::make_shared<AdjRib::AdjRibInQueueT>();
  auto adjRibInQ2 = std::make_shared<AdjRib::AdjRibInQueueT>();
  auto boundedOutQ1 = std::make_shared<AdjRib::AdjRibOutQueueT>(
      kMaxEgressQueueSize, kEgressQueueHighWatermark, kEgressQueueLowWatermark);
  auto boundedOutQ2 = std::make_shared<AdjRib::AdjRibOutQueueT>(
      kMaxEgressQueueSize, kEgressQueueHighWatermark, kEgressQueueLowWatermark);

  // batons used to do synchronization to notify destruction sequence
  folly::fibers::Baton baton1, baton2;

  auto adjRib1 = setupAdjRib(
      evb,
      peerMgr->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);
  adjRib1->setInInitialAnnouncement();
  auto adjRib2 = setupAdjRib(
      evb,
      peerMgr->getChangeListTracker(),
      kPeerId2,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config);

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  peerMgr->adjRibs_[kPeerId2] = adjRib2;

  // Start fiber tasks from both Rib and FiberBgpPeerManager
  fm.addTask([&] {
    adjRib1->sessionEstablished(std::nullopt, adjRibInQ1, boundedOutQ1);
    adjRib1->startMessageProcessingLoop();
  });
  fm.addTask([&] {
    adjRib2->sessionEstablished(std::nullopt, adjRibInQ2, boundedOutQ2);
    adjRib2->startMessageProcessingLoop();
  });

  // start coroutine task
  folly::coro::CancellableAsyncScope asyncScope;
  asyncScope.add(co_withExecutor(&evb, peerMgr->processRibOutMsgLoop()));

  fm.addTask([&] {
    auto ribMsg =
        createRibSingleAnnounce(kV4Prefix1, kV4Nexthop1, kLocalRouteAs, true);
    ribOutQ_.push(std::move(ribMsg));
  });

  fm.addTask([&] {
    auto msg =
        facebook::bgp::test::boundedBlockingPop(*boundedOutQ1, "boundedOutQ1");
    ASSERT_TRUE(msg);
    // 2 EoRs as a result of initial announcement
    ASSERT_EQ(2, boundedOutQ1->size());
    ASSERT_TRUE(
        std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(*msg));
    // Verify that announcement message is notified to Fiber Bgp Peer properly
    auto bgpUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    ASSERT_EQ(1, bgpUpdate->mpAnnounced()->prefixes()->size());
    EXPECT_EQ(
        network::toIPPrefix(kV4Prefix1),
        *bgpUpdate->mpAnnounced()->prefixes()[0].prefix());
    EXPECT_EQ(kLocalPref, bgpUpdate->attrs()->localPref());
    EXPECT_EQ(htonl(kOriginatorId), *bgpUpdate->attrs()->originatorId());
    EXPECT_EQ(kV4Nexthop1.str(), *bgpUpdate->attrs()->nexthop());
    // notify verification done
    baton1.post();
  });

  fm.addTask([&] {
    REPEAT({ EXPECT_EQ(0, boundedOutQ2->size()); });
    // notify verification done
    baton2.post();
  });

  std::thread stopPeerThread([&]() {
    // Hold till adjRib1 and adjRib2 both received replicated RibMessage
    baton1.wait();
    baton2.wait();

    // Manually shutdown AdjRibs since no actual sessionMgr running
    adjRibInQ1->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    adjRibInQ2->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    peerMgr->stop();

    // cancel coroutines
    folly::coro::blockingWait(asyncScope.cancelAndJoinAsync());
  });

  evb.loop();
  stopPeerThread.join();
  SUCCEED();
}

/**
 * Test selective multipath notification: bestpath changes should be sent to
 * ALL peers (both add-path capable and non-add-path capable).
 */
CO_TEST_F(PeerManagerTestFixture, SelectiveMultipathNotificationBestpathTest) {
  // Step 0.0: test setup with 2 adjRibs
  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);

  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto& evb = peerMgr->getEventBase();
  auto adjRibInQ1 = std::make_shared<AdjRib::AdjRibInQueueT>();
  auto adjRibInQ2 = std::make_shared<AdjRib::AdjRibInQueueT>();
  auto boundedOutQ1 = std::make_shared<AdjRib::AdjRibOutQueueT>(
      kMaxEgressQueueSize, kEgressQueueHighWatermark, kEgressQueueLowWatermark);
  auto boundedOutQ2 = std::make_shared<AdjRib::AdjRibOutQueueT>(
      kMaxEgressQueueSize, kEgressQueueHighWatermark, kEgressQueueLowWatermark);

  auto adjRib1 = setupAdjRib(
      evb,
      peerMgr->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config,
      peerMgr->addPathConsumerBitmap_,
      peerMgr->nonAddPathConsumerBitmap_);

  auto adjRib2 = setupAdjRib(
      evb,
      peerMgr->getChangeListTracker(),
      kPeerId2,
      AsNum(kAsn2),
      sessionTerminateBaton_,
      config,
      peerMgr->addPathConsumerBitmap_,
      peerMgr->nonAddPathConsumerBitmap_);

  peerMgr->ribInitialAnnouncementStarted_ = true;
  peerMgr->ribInitialAnnouncementDone_ = true;

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  peerMgr->adjRibs_[kPeerId2] = adjRib2;

  // Mark sessions in established state
  adjRib1->sessionEstablished(std::nullopt, adjRibInQ1, boundedOutQ1);
  adjRib1->sendAddPath_ = false; // non-add-path peer
  adjRib1->markStateEstablished();

  adjRib2->sessionEstablished(std::nullopt, adjRibInQ2, boundedOutQ2);
  adjRib2->sendAddPath_ = true; // add-path capable peer
  adjRib2->markStateEstablished();

  /*
   * Step 0.1: Pre-populate shadowRib:
   * - kV4Prefix1: bestpath only (no multipaths)
   * - kV4Prefix2: bestpath AND two multipaths
   */
  auto msg1 = createRibSingleAnnounce(
      kV4Prefix1,
      kV4Nexthop1,
      kLocalRouteAs,
      false /* sendWithEoR */,
      false /* addPath */);
  auto msg2 = createRibSingleAnnounce(
      kV4Prefix2,
      kV4Nexthop1,
      kLocalRouteAs,
      false /* sendWithEoR */,
      true /* addPath */,
      kMinPathIDToSend);
  auto msg3 = createRibSingleAnnounce(
      kV4Prefix2,
      kV4Nexthop2,
      kLocalRouteAs,
      false /* sendWithEoR */,
      true /* addPath */,
      kMinPathIDToSend + 1);
  auto msg4 = createRibSingleAnnounce(
      kV4Prefix2,
      kV4Nexthop1,
      kLocalRouteAs,
      false /* sendWithEoR */,
      false /* addPath */);

  // Populate multipaths first
  peerMgr->handleShadowRibEntryAnnouncement(std::get<RibOutAnnouncement>(msg2));
  peerMgr->handleShadowRibEntryAnnouncement(std::get<RibOutAnnouncement>(msg3));
  // Then populate bestpaths
  peerMgr->handleShadowRibEntryAnnouncement(std::get<RibOutAnnouncement>(msg1));
  peerMgr->handleShadowRibEntryAnnouncement(std::get<RibOutAnnouncement>(msg4));

  // No consumer registered yet, so no items in changeList
  auto changeItem = peerMgr->getChangeListTracker()->getHead();
  EXPECT_EQ(nullptr, changeItem);

  // Step 0.2: Send RibDumpReq for both adjRibs
  folly::coro::blockingWait(peerMgr->processRibDumpReqCoro(
      RibDumpReq(kPeerId1, false /* sendAddPath */)));
  folly::coro::blockingWait(peerMgr->processRibDumpReqCoro(
      RibDumpReq(kPeerId2, true /* sendAddPath */)));

  /*
   * Verify:
   * - adjRib1 (non-addpath) gets bestpath for BOTH prefixes
   * - adjRib2 (addpath) gets ONLY kV4Prefix2 (which has multipaths)
   */
  co_await waitForAdjRibsToProcessUpdates(
      evb, {adjRib1->adjRibOutQueue_, adjRib2->adjRibOutQueue_});

  EXPECT_EQ(2, adjRib1->stats_.getPreOutPrefixCount()); // both prefixes
  EXPECT_EQ(1, adjRib2->stats_.getPreOutPrefixCount()); // only kV4Prefix2

  // Consume initial changes
  co_await adjRib1->getChangeListConsumer()->consumeChanges();
  co_await adjRib2->getChangeListConsumer()->consumeChanges();

  // Step 0.3: Test change list delivery AFTER RibDumpReq

  /*
   * Case 1: Bestpath change for NEW prefix kV4Prefix3
   * This should be delivered to BOTH adjRib1 and adjRib2
   * Add bestpath for kV4Prefix3 (this will also be in multipaths)
   */
  auto msg5a = createRibSingleAnnounce(
      kV4Prefix3,
      kV4Nexthop3,
      kLocalRouteAs,
      false /* sendWithEoR */,
      true /* addPath */,
      kMinPathIDToSend + 2);
  auto msg5b = createRibSingleAnnounce(
      kV4Prefix3,
      kV4Nexthop3,
      kLocalRouteAs,
      false /* sendWithEoR */,
      false /* addPath */);

  /*
   * Add multipath first, then bestpath (bestpath should be one of the
   * multipaths)
   */
  peerMgr->handleShadowRibEntryAnnouncement(
      std::get<RibOutAnnouncement>(msg5a));
  peerMgr->handleShadowRibEntryAnnouncement(
      std::get<RibOutAnnouncement>(msg5b));

  co_await adjRib1->getChangeListConsumer()->consumeChanges();
  co_await adjRib2->getChangeListConsumer()->consumeChanges();
  co_await waitForAdjRibsToProcessUpdates(
      evb, {adjRib1->adjRibOutQueue_, adjRib2->adjRibOutQueue_});

  // Both adjRibs should receive the bestpath change
  EXPECT_EQ(3, adjRib1->stats_.getPreOutPrefixCount());
  EXPECT_EQ(2, adjRib2->stats_.getPreOutPrefixCount());

  /*
   * Case 2: Add multipath (non-bestpath) for kV4Prefix3
   * This should ONLY be delivered to adjRib2 (addpath peer)
   * This verifies the bitmap filtering is working correctly
   */
  auto msg6 = createRibSingleAnnounce(
      kV4Prefix3,
      kV4Nexthop1, // different nexthop from bestpath
      kLocalRouteAs,
      false /* sendWithEoR */,
      true /* addPath */,
      kMinPathIDToSend + 3);
  peerMgr->handleShadowRibEntryAnnouncement(std::get<RibOutAnnouncement>(msg6));

  /*
   * Verify the bitmap: only addpath consumer (adjRib2) should have the bit
   * set
   */
  changeItem = peerMgr->getChangeListTracker()->getHead();
  EXPECT_NE(nullptr, changeItem);
  // adjRib2 (addpath capable) should have bit set
  EXPECT_TRUE(
      BitmapUtils::isBitSet(
          changeItem->consumerBitmap,
          adjRib2->getChangeListConsumer()->getBitPosition()));
  // adjRib1 (non-addpath) should NOT have bit set
  EXPECT_FALSE(
      BitmapUtils::isBitSet(
          changeItem->consumerBitmap,
          adjRib1->getChangeListConsumer()->getBitPosition()));

  co_await adjRib1->getChangeListConsumer()->consumeChanges();
  co_await adjRib2->getChangeListConsumer()->consumeChanges();
  co_await waitForAdjRibsToProcessUpdates(
      evb, {adjRib1->adjRibOutQueue_, adjRib2->adjRibOutQueue_});

  // Only adjRib2 should receive the multipath-only change
  EXPECT_EQ(3, adjRib1->stats_.getPreOutPrefixCount()); // unchanged
  EXPECT_EQ(
      2, adjRib2->stats_.getPreOutPrefixCount()); // unchanged (same prefix)

  /*
   * Case 3: Test variation - bestpath change while in changelist, then
   * multipath change Bestpath change should still be delivered to ALL ribs
   * (multipath change cannot override bestpath change)
   */

  // Add kV4Prefix4: First add as multipath, then as bestpath
  auto msg7a = createRibSingleAnnounce(
      kV4Prefix4,
      kV4Nexthop4,
      kLocalRouteAs,
      false /* sendWithEoR */,
      true /* addPath */,
      kMinPathIDToSend + 4);
  auto msg7b = createRibSingleAnnounce(
      kV4Prefix4,
      kV4Nexthop4,
      kLocalRouteAs,
      false /* sendWithEoR */,
      false /* addPath */);
  peerMgr->handleShadowRibEntryAnnouncement(
      std::get<RibOutAnnouncement>(msg7a));
  peerMgr->handleShadowRibEntryAnnouncement(
      std::get<RibOutAnnouncement>(msg7b));

  // Immediately add another multipath BEFORE consuming the bestpath change
  auto msg8 = createRibSingleAnnounce(
      kV4Prefix4,
      kV4Nexthop1, // different nexthop from bestpath
      kLocalRouteAs,
      false /* sendWithEoR */,
      true /* addPath */,
      kMinPathIDToSend + 5);
  peerMgr->handleShadowRibEntryAnnouncement(std::get<RibOutAnnouncement>(msg8));

  // Now consume - bestpath change should be delivered to BOTH adjribs
  co_await adjRib1->getChangeListConsumer()->consumeChanges();
  co_await adjRib2->getChangeListConsumer()->consumeChanges();
  co_await waitForAdjRibsToProcessUpdates(
      evb, {adjRib1->adjRibOutQueue_, adjRib2->adjRibOutQueue_});

  /*
   * Both adjRibs should receive the bestpath change (multipath cannot
   * override)
   */
  EXPECT_EQ(4, adjRib1->stats_.getPreOutPrefixCount());
  EXPECT_EQ(3, adjRib2->stats_.getPreOutPrefixCount());

  /*
   * Case 4: Test variation - multipath change in changelist, then bestpath
   * change
   * Bestpath change will OR nonAddPathConsumerBitmap with existing
   * addPathConsumerBitmap, resulting in both bits set
   */

  // First, add multipath for kV4Prefix5 (non-bestpath nexthop)
  auto msg9 = createRibSingleAnnounce(
      kV4Prefix5,
      kV4Nexthop1,
      kLocalRouteAs,
      false /* sendWithEoR */,
      true /* addPath */,
      kMinPathIDToSend + 6);
  peerMgr->handleShadowRibEntryAnnouncement(std::get<RibOutAnnouncement>(msg9));

  // Verify: initial multipath publish has only addpath bit set
  changeItem = peerMgr->getChangeListTracker()->getHead();
  EXPECT_NE(nullptr, changeItem);
  EXPECT_TRUE(
      BitmapUtils::isBitSet(
          changeItem->consumerBitmap,
          adjRib2->getChangeListConsumer()->getBitPosition()));
  EXPECT_FALSE(
      BitmapUtils::isBitSet(
          changeItem->consumerBitmap,
          adjRib1->getChangeListConsumer()->getBitPosition()));

  // Add bestpath nexthop as multipath first
  auto msg10a = createRibSingleAnnounce(
      kV4Prefix5,
      kV4Nexthop5,
      kLocalRouteAs,
      false /* sendWithEoR */,
      true /* addPath */,
      kMinPathIDToSend + 7);
  peerMgr->handleShadowRibEntryAnnouncement(
      std::get<RibOutAnnouncement>(msg10a));

  /*
   * Verify: after second multipath publish (item already on list with
   * multipath), bitmap should remain ONLY addpath
   */
  changeItem = peerMgr->getChangeListTracker()->getHead();
  EXPECT_NE(nullptr, changeItem);
  EXPECT_TRUE(
      BitmapUtils::isBitSet(
          changeItem->consumerBitmap,
          adjRib2->getChangeListConsumer()->getBitPosition()));
  EXPECT_FALSE(
      BitmapUtils::isBitSet(
          changeItem->consumerBitmap,
          adjRib1->getChangeListConsumer()->getBitPosition()));

  // Now add bestpath BEFORE consuming the multipath changes
  auto msg10b = createRibSingleAnnounce(
      kV4Prefix5,
      kV4Nexthop5,
      kLocalRouteAs,
      false /* sendWithEoR */,
      false /* addPath */);
  peerMgr->handleShadowRibEntryAnnouncement(
      std::get<RibOutAnnouncement>(msg10b));

  // Consume changes - both adjribs should receive the update
  co_await adjRib1->getChangeListConsumer()->consumeChanges();
  co_await adjRib2->getChangeListConsumer()->consumeChanges();
  co_await waitForAdjRibsToProcessUpdates(
      evb, {adjRib1->adjRibOutQueue_, adjRib2->adjRibOutQueue_});

  // Both adjRibs should have received the changes
  EXPECT_EQ(5, adjRib1->stats_.getPreOutPrefixCount());
  EXPECT_EQ(4, adjRib2->stats_.getPreOutPrefixCount());

  co_return;
}

/**
 * Test selective multipath notification: multipath (add-path) changes should
 * only be sent to add-path capable peers.
 */
CO_TEST_F(PeerManagerTestFixture, SelectiveMultipathNotificationAddPathTest) {
  auto config = getConfig(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);

  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto& evb = peerMgr->getEventBase();
  auto adjRibInQ1 = std::make_shared<AdjRib::AdjRibInQueueT>();
  auto adjRibInQ2 = std::make_shared<AdjRib::AdjRibInQueueT>();
  auto boundedOutQ1 = std::make_shared<AdjRib::AdjRibOutQueueT>(
      kMaxEgressQueueSize, kEgressQueueHighWatermark, kEgressQueueLowWatermark);
  auto boundedOutQ2 = std::make_shared<AdjRib::AdjRibOutQueueT>(
      kMaxEgressQueueSize, kEgressQueueHighWatermark, kEgressQueueLowWatermark);

  // Create adjRib1: add-path capable peer
  auto adjRib1 = setupAdjRib(
      evb,
      peerMgr->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config,
      peerMgr->addPathConsumerBitmap_,
      peerMgr->nonAddPathConsumerBitmap_);

  // Create adjRib2: non-add-path capable peer
  auto adjRib2 = setupAdjRib(
      evb,
      peerMgr->getChangeListTracker(),
      kPeerId2,
      AsNum(kAsn2),
      sessionTerminateBaton_,
      config,
      peerMgr->addPathConsumerBitmap_,
      peerMgr->nonAddPathConsumerBitmap_);

  peerMgr->ribInitialAnnouncementStarted_ = true;
  peerMgr->ribInitialAnnouncementDone_ = true;

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  peerMgr->adjRibs_[kPeerId2] = adjRib2;

  // Mark sessions as established
  adjRib1->sessionEstablished(std::nullopt, adjRibInQ1, boundedOutQ1);
  adjRib1->sendAddPath_ = true; // add-path capable
  adjRib1->markStateEstablished();
  /* Do not skip non-initial announcement by marking egressEoRsSent as true. */
  adjRib1->egressEoRsSent_ = true;

  adjRib2->sessionEstablished(std::nullopt, adjRibInQ2, boundedOutQ2);
  adjRib2->sendAddPath_ = false; // non-add-path capable
  adjRib2->markStateEstablished();
  /* Do not skip non-initial announcement by marking egressEoRsSent as true. */
  adjRib2->egressEoRsSent_ = true;

  // Send RibDumpReq to activate changeListConsumer
  folly::coro::blockingWait(peerMgr->processRibDumpReqCoro(
      RibDumpReq(kPeerId1, true /* sendAddPath */)));
  folly::coro::blockingWait(peerMgr->processRibDumpReqCoro(
      RibDumpReq(kPeerId2, false /* sendAddPath */)));

  /*
   * Publish an add-path (multipath) announcement (addPath = true)
   * This should ONLY be seen by adjRib1 (add-path capable)
   */
  auto msg1 = createRibSingleAnnounce(
      kV4Prefix1,
      kV4Nexthop1,
      kLocalRouteAs,
      false,
      true /* addPath */,
      kMinPathIDToSend);
  peerMgr->handleShadowRibEntryAnnouncement(std::get<RibOutAnnouncement>(msg1));

  // Consume changes for both adjRibs
  co_await adjRib1->getChangeListConsumer()->consumeChanges();
  co_await adjRib2->getChangeListConsumer()->consumeChanges();
  co_await waitForAdjRibsToProcessUpdates(
      evb, {adjRib1->adjRibOutQueue_, adjRib2->adjRibOutQueue_});

  // Only adjRib1 (add-path capable) should have received the add-path route
  EXPECT_EQ(1, adjRib1->stats_.getPreOutPrefixCount());
  // adjRib2 (non-add-path) should NOT have received the add-path route
  EXPECT_EQ(0, adjRib2->stats_.getPreOutPrefixCount());
  co_return;
}

/**
 * Test selective multipath notification with mixed bestpath and multipath
 * changes to verify the complete workflow.
 */
CO_TEST_F(PeerManagerTestFixture, SelectiveMultipathNotificationMixTest) {
  auto config = getConfig(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);

  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  auto& evb = peerMgr->getEventBase();
  auto adjRibInQ1 = std::make_shared<AdjRib::AdjRibInQueueT>();
  auto adjRibInQ2 = std::make_shared<AdjRib::AdjRibInQueueT>();
  auto boundedOutQ1 = std::make_shared<AdjRib::AdjRibOutQueueT>(
      kMaxEgressQueueSize, kEgressQueueHighWatermark, kEgressQueueLowWatermark);
  auto boundedOutQ2 = std::make_shared<AdjRib::AdjRibOutQueueT>(
      kMaxEgressQueueSize, kEgressQueueHighWatermark, kEgressQueueLowWatermark);

  // Create adjRib1: add-path capable peer
  auto adjRib1 = setupAdjRib(
      evb,
      peerMgr->getChangeListTracker(),
      kPeerId1,
      AsNum(kAsn1),
      sessionTerminateBaton_,
      config,
      peerMgr->addPathConsumerBitmap_,
      peerMgr->nonAddPathConsumerBitmap_);

  // Create adjRib2: non-add-path capable peer
  auto adjRib2 = setupAdjRib(
      evb,
      peerMgr->getChangeListTracker(),
      kPeerId2,
      AsNum(kAsn2),
      sessionTerminateBaton_,
      config,
      peerMgr->addPathConsumerBitmap_,
      peerMgr->nonAddPathConsumerBitmap_);

  peerMgr->ribInitialAnnouncementStarted_ = true;
  peerMgr->ribInitialAnnouncementDone_ = true;

  peerMgr->adjRibs_[kPeerId1] = adjRib1;
  peerMgr->adjRibs_[kPeerId2] = adjRib2;

  // Mark sessions as established
  adjRib1->sessionEstablished(std::nullopt, adjRibInQ1, boundedOutQ1);
  adjRib1->sendAddPath_ = true; // add-path capable
  adjRib1->markStateEstablished();
  /* Do not skip non-initial announcement by marking egressEoRsSent as true. */
  adjRib1->egressEoRsSent_ = true;

  adjRib2->sessionEstablished(std::nullopt, adjRibInQ2, boundedOutQ2);
  adjRib2->sendAddPath_ = false; // non-add-path capable
  adjRib2->markStateEstablished();
  /* Do not skip non-initial announcement by marking egressEoRsSent as true. */
  adjRib2->egressEoRsSent_ = true;

  // Send RibDumpReq to activate changeListConsumer
  folly::coro::blockingWait(peerMgr->processRibDumpReqCoro(
      RibDumpReq(kPeerId1, true /* sendAddPath */)));
  folly::coro::blockingWait(peerMgr->processRibDumpReqCoro(
      RibDumpReq(kPeerId2, false /* sendAddPath */)));

  co_await waitForAdjRibsToProcessUpdates(
      evb, {adjRib1->adjRibOutQueue_, adjRib2->adjRibOutQueue_});
  co_await adjRib1->getChangeListConsumer()->consumeChanges();
  co_await adjRib2->getChangeListConsumer()->consumeChanges();

  /*
   * Test Case 1: Publish a bestpath announcement
   * (should be seen by BOTH adjRibs)
   * First add as multipath, then as bestpath
   */
  auto msg1a = createRibSingleAnnounce(
      kV4Prefix1,
      kV4Nexthop1,
      kLocalRouteAs,
      false,
      true /* addPath */,
      kMinPathIDToSend);
  auto msg1b = createRibSingleAnnounce(
      kV4Prefix1, kV4Nexthop1, kLocalRouteAs, false, false /* addPath */);
  peerMgr->handleShadowRibEntryAnnouncement(
      std::get<RibOutAnnouncement>(msg1a));
  peerMgr->handleShadowRibEntryAnnouncement(
      std::get<RibOutAnnouncement>(msg1b));

  co_await adjRib1->getChangeListConsumer()->consumeChanges();
  co_await adjRib2->getChangeListConsumer()->consumeChanges();
  co_await waitForAdjRibsToProcessUpdates(
      evb, {adjRib1->adjRibOutQueue_, adjRib2->adjRibOutQueue_});

  EXPECT_EQ(1, adjRib1->stats_.getPreOutPrefixCount());
  EXPECT_EQ(1, adjRib2->stats_.getPreOutPrefixCount());

  /*
   * Test Case 2: Publish an add-path (multipath) announcement
   * (should be seen ONLY by adjRib1)
   */
  auto msg2 = createRibSingleAnnounce(
      kV4Prefix2,
      kV4Nexthop2,
      kLocalRouteAs,
      false,
      true /* addPath */,
      kMinPathIDToSend);
  peerMgr->handleShadowRibEntryAnnouncement(std::get<RibOutAnnouncement>(msg2));

  co_await adjRib1->getChangeListConsumer()->consumeChanges();
  co_await adjRib2->getChangeListConsumer()->consumeChanges();
  co_await waitForAdjRibsToProcessUpdates(
      evb, {adjRib1->adjRibOutQueue_, adjRib2->adjRibOutQueue_});

  // adjRib1 should have both prefixes
  EXPECT_EQ(2, adjRib1->stats_.getPreOutPrefixCount());
  // adjRib2 should only have the bestpath prefix
  EXPECT_EQ(1, adjRib2->stats_.getPreOutPrefixCount());

  /*
   * Test Case 3: Publish another bestpath to verify system still works
   * First add as multipath, then as bestpath
   */
  auto msg3a = createRibSingleAnnounce(
      kV4Prefix3,
      kV4Nexthop3,
      kLocalRouteAs,
      false,
      true /* addPath */,
      kMinPathIDToSend + 1);
  auto msg3b = createRibSingleAnnounce(
      kV4Prefix3, kV4Nexthop3, kLocalRouteAs, false, false /* addPath */);
  peerMgr->handleShadowRibEntryAnnouncement(
      std::get<RibOutAnnouncement>(msg3a));
  peerMgr->handleShadowRibEntryAnnouncement(
      std::get<RibOutAnnouncement>(msg3b));

  co_await adjRib1->getChangeListConsumer()->consumeChanges();
  co_await adjRib2->getChangeListConsumer()->consumeChanges();
  co_await waitForAdjRibsToProcessUpdates(
      evb, {adjRib1->adjRibOutQueue_, adjRib2->adjRibOutQueue_});

  // Both adjRibs should have received the third bestpath route
  EXPECT_EQ(3, adjRib1->stats_.getPreOutPrefixCount());
  EXPECT_EQ(2, adjRib2->stats_.getPreOutPrefixCount());
  /* Execute any pending work before test cleanup. */
  evb.loopOnce();
  co_return;
}

/*
 * The shadow RIB version counts the contiguous per-prefix chunks the shadow
 * RIB has applied: one increment per chunk, no matter how many paths the chunk
 * carries and no matter what RibVersion the RIB stamped on the entries.
 */
TEST_F(PeerManagerTestFixture, MaxRibVersionCountsPrefixChunks) {
  auto config = getConfig(
      false /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      std::make_shared<ConfigManager>(config),
      nullptr,
      ribInQ_,
      ribOutQ_,
      nbrRouteChangeQ_);

  auto makeAddPathEntry = [](const folly::CIDRNetwork& prefix,
                             const folly::IPAddress& nexthop,
                             uint64_t ribVersion) {
    auto msg = std::get<RibOutAnnouncement>(createRibSingleAnnounce(
        prefix,
        nexthop,
        kLocalRouteAs,
        false /* sendWithEoR */,
        true /* addPath */));
    auto entry = msg.addPathEntries.front();
    entry.ribVersion = ribVersion;
    return entry;
  };

  /*
   * Two paths of one prefix followed by a second prefix: two chunks. The RIB
   * versions are constant and then move backwards, so a version that tracked
   * them would land somewhere other than 2.
   */
  RibOutAnnouncement announcement{};
  announcement.addPathEntries.push_back(
      makeAddPathEntry(kV4Prefix1, kV4Nexthop1, 500 /* ribVersion */));
  announcement.addPathEntries.push_back(
      makeAddPathEntry(kV4Prefix1, kV4Nexthop2, 500 /* ribVersion */));
  announcement.addPathEntries.push_back(
      makeAddPathEntry(kV4Prefix2, kV4Nexthop1, 100 /* ribVersion */));

  peerMgr->handleShadowRibEntryAnnouncement(announcement);

  EXPECT_EQ(2, peerMgr->getMaxRibVersion());
  EXPECT_EQ(1, peerMgr->shadowRibEntries_.at(kV4Prefix1)->get().ribVersion);
  EXPECT_EQ(2, peerMgr->shadowRibEntries_.at(kV4Prefix2)->get().ribVersion);

  /* A withdrawal for a prefix the shadow RIB never had still opens a chunk. */
  RibOutWithdrawal withdrawal{};
  withdrawal.entries.emplace_back(kV4Prefix3, kDefaultPathID);
  peerMgr->handleShadowRibEntryWithdrawal(withdrawal);

  EXPECT_EQ(3, peerMgr->getMaxRibVersion());
}

/*
 * The shadow RIB version is monotonic by construction: it is a counter this
 * class owns, so no ordering of RIB-supplied versions can move it -- or any
 * shadow rib entry stamped from it -- backwards.
 */
TEST_F(PeerManagerTestFixture, MaxRibVersionNeverMovesBackwards) {
  auto config = getConfig(
      false /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      std::make_shared<ConfigManager>(config),
      nullptr,
      ribInQ_,
      ribOutQ_,
      nbrRouteChangeQ_);

  auto announceBestpath = [&](const folly::CIDRNetwork& prefix,
                              uint64_t ribVersion) {
    auto msg = std::get<RibOutAnnouncement>(createRibSingleAnnounce(
        prefix,
        kV4Nexthop1,
        kLocalRouteAs,
        false /* sendWithEoR */,
        false /* addPath */));
    msg.entries.back().ribVersion = ribVersion;
    peerMgr->handleShadowRibEntryAnnouncement(msg);
  };

  auto withdrawBestpath = [&](const folly::CIDRNetwork& prefix,
                              uint64_t ribVersion) {
    RibOutWithdrawal withdrawal{};
    withdrawal.entries.emplace_back(prefix, kDefaultPathID);
    withdrawal.entries.back().ribVersion = ribVersion;
    peerMgr->handleShadowRibEntryWithdrawal(withdrawal);
  };

  /*
   * RIB versions that jump forward, repeat, and then collapse to 1. Each step
   * is one prefix chunk, so the shadow RIB version must simply count 1..6.
   */
  const std::vector<uint64_t> kOutOfOrderRibVersions = {9000, 9000, 3, 1, 1, 2};
  std::vector<uint64_t> observed;
  uint64_t previous = peerMgr->getMaxRibVersion();
  ASSERT_EQ(0, previous);

  for (size_t i = 0; i < kOutOfOrderRibVersions.size(); ++i) {
    const auto& prefix = (i % 2 == 0) ? kV4Prefix1 : kV4Prefix2;
    const bool isAnnouncement = i < 4;
    if (isAnnouncement) {
      announceBestpath(prefix, kOutOfOrderRibVersions[i]);
    } else {
      withdrawBestpath(prefix, kOutOfOrderRibVersions[i]);
    }
    const auto current = peerMgr->getMaxRibVersion();
    EXPECT_GT(current, previous)
        << "step " << i << " with RIB version " << kOutOfOrderRibVersions[i]
        << " failed to advance the shadow RIB version";
    previous = current;
    observed.push_back(current);

    /*
     * An announced prefix carries the version of the chunk that just touched
     * it -- never a RIB-supplied number. A withdrawn one is gone: no consumer
     * is registered here, so the change completes inline and
     * processChangeItemCompleteCallback erases the entry.
     */
    if (isAnnouncement) {
      EXPECT_EQ(
          current, peerMgr->shadowRibEntries_.at(prefix)->get().ribVersion);
    } else {
      EXPECT_FALSE(peerMgr->shadowRibEntries_.contains(prefix));
    }
  }

  const std::vector<uint64_t> expected = {1, 2, 3, 4, 5, 6};
  EXPECT_EQ(expected, observed);
}

/*
 * getShadowRibEntriesCanonical converts the live shadow RIB to the canonical
 * deduplicated form: announce two IPv4 bestpath routes and verify both prefixes
 * appear with a path marked is_best_path, and that AFI filtering excludes IPv6
 * (none announced) and the unsupported AFI_ALL.
 */
TEST_F(PeerManagerTestFixture, GetShadowRibEntriesCanonical) {
  using neteng::fboss::bgp_attr::TBgpAfi;

  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      std::make_shared<ConfigManager>(config),
      nullptr,
      ribInQ_,
      ribOutQ_,
      nbrRouteChangeQ_);

  auto msg1 = createRibSingleAnnounce(
      kV4Prefix1,
      kV4Nexthop1,
      kLocalRouteAs,
      false /* EoR */,
      false /* addpath */);
  peerMgr->handleShadowRibEntryAnnouncement(std::get<RibOutAnnouncement>(msg1));
  auto msg2 = createRibSingleAnnounce(
      kV4Prefix2,
      kV4Nexthop2,
      kLocalRouteAs,
      false /* EoR */,
      false /* addpath */);
  peerMgr->handleShadowRibEntryAnnouncement(std::get<RibOutAnnouncement>(msg2));

  /*
   * Both announced prefixes convert. The getter does not populate best_path
   * (FSDB-only); each entry instead carries its best path via is_best_path.
   */
  auto state = peerMgr->getShadowRibEntriesCanonical(TBgpAfi::AFI_IPV4);
  ASSERT_EQ(2, state.rib_entries()->size());
  for (const auto& [prefixStr, entry] : *state.rib_entries()) {
    EXPECT_FALSE(entry.best_path().has_value()) << prefixStr;
    EXPECT_FALSE(entry.paths()->empty()) << prefixStr;
    bool hasBestPath = false;
    for (const auto& [group, paths] : *entry.paths()) {
      for (const auto& path : paths) {
        hasBestPath |= path.is_best_path().value_or(false);
      }
    }
    EXPECT_TRUE(hasBestPath) << prefixStr;
  }

  // No IPv6 routes announced, and AFI_ALL is unsupported by this getter.
  EXPECT_EQ(
      0,
      peerMgr->getShadowRibEntriesCanonical(TBgpAfi::AFI_IPV6)
          .rib_entries()
          ->size());
  EXPECT_EQ(
      0,
      peerMgr->getShadowRibEntriesCanonical(TBgpAfi::AFI_ALL)
          .rib_entries()
          ->size());
}

/*
 * getChangeListEntriesCanonical shares its ShadowRibEntry->canonical conversion
 * with getShadowRibEntriesCanonical (both call
 * buildCanonicalPathInputsFromShadowEntry, exercised above and in RibTest /
 * CanonicalRibBuilderTest). Here we verify the getter-specific guards: an
 * unsupported AFI and an empty (but live) change list both yield an empty
 * canonical state.
 */
TEST_F(PeerManagerTestFixture, GetChangeListEntriesCanonical) {
  using neteng::fboss::bgp_attr::TBgpAfi;

  auto config = getConfig(
      true /* includeStaticPeer */, true /* includeDynamicShivPeer */);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      std::make_shared<ConfigManager>(config),
      nullptr,
      ribInQ_,
      ribOutQ_,
      nbrRouteChangeQ_);

  // Unsupported AFI short-circuits before touching the change list.
  EXPECT_EQ(
      0,
      peerMgr->getChangeListEntriesCanonical(TBgpAfi::AFI_ALL)
          .rib_entries()
          ->size());
  // Empty (but live) change list iterates to an empty canonical state.
  EXPECT_EQ(
      0,
      peerMgr->getChangeListEntriesCanonical(TBgpAfi::AFI_IPV4)
          .rib_entries()
          ->size());
}

} // namespace facebook::bgp
