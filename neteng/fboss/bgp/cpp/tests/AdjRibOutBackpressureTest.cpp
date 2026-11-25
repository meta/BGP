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

#include <folly/coro/Baton.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Collect.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/logging/xlog.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"

#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

#define AdjRib_TEST_FRIENDS                                                    \
  friend class SendBgpMessagesFixture;                                         \
  FRIEND_TEST(SendBgpMessagesFixture, SessionEstablishedCleanUp);              \
  FRIEND_TEST(SendBgpMessagesFixture, SessionTerminatedCleanUp);               \
  FRIEND_TEST(SendBgpMessagesFixture, PackPrefixesTest);                       \
  FRIEND_TEST(SendBgpMessagesFixture, BuildAndQueueWithdrawalsTest);           \
  FRIEND_TEST(SendBgpMessagesFixture, BuildAndQueueAnnouncementsTest);         \
  FRIEND_TEST(SendBgpMessagesFixture, PackPrefixesWithLimitTest);              \
  FRIEND_TEST(SendBgpMessagesFixture, BuildUpdateWithSizeEstimationTest);      \
  FRIEND_TEST(SendBgpMessagesFixture, CancelPackingTimersTest);                \
  FRIEND_TEST(SendBgpMessagesFixture, ReschedulePackingTimersTest);            \
  FRIEND_TEST(SendBgpMessagesFixture, ScheduleSendBgpMessagesTest);            \
  FRIEND_TEST(SendBgpMessagesFixture, WaitForQueueSpaceTest);                  \
  FRIEND_TEST(SendBgpMessagesFixture, SendPendingEoRsTest);                    \
  FRIEND_TEST(SendBgpMessagesFixture, RouteRefreshUsesUnboundedEgressQueue);   \
  FRIEND_TEST(SendBgpMessagesFixture, SimpleSendBgpUpdateMessagesTest);        \
  FRIEND_TEST(SendBgpMessagesFixture, BulkSendBgpUpdateMessagesTest);          \
  FRIEND_TEST(SendBgpMessagesFixture, SendBgpUpdateMessagesTest_AfterEoR);     \
  FRIEND_TEST(SendBgpMessagesFixture, SendBgpUpdateMessagesTest_BeforeEoR);    \
  FRIEND_TEST(SendBgpMessagesFixture, SendBgpUpdateMessagesTest_PendingEoR);   \
  FRIEND_TEST(SendBgpMessagesFixture, ActivateChangeListConsumerBeforeEoR);    \
  FRIEND_TEST(SendBgpMessagesFixture, ActivateChangeListConsumerAfterEoR);     \
  FRIEND_TEST(SendBgpMessagesFixture, NotifyConsumeChangeListUponEoRSent);     \
  FRIEND_TEST(SendBgpMessagesFixture, NotifyConsumeChangeListAfterEoR);        \
  FRIEND_TEST(SendBgpMessagesFixture, NumTransientUpdatesSuppressedStatsTest); \
  FRIEND_TEST(                                                                 \
      SendBgpMessagesFixture, ProcessRibAnnouncedEntry_PropagatesRibVersion);  \
  friend class SendBgpMessagesFixtureWithBackpressure;                         \
  FRIEND_TEST(SendBgpMessagesFixtureWithBackpressure, WaitForQueueSpaceTest);  \
  FRIEND_TEST(SendBgpMessagesFixtureWithBackpressure, SendPendingEoRsTest);    \
  FRIEND_TEST(                                                                 \
      SendBgpMessagesFixtureWithBackpressure,                                  \
      RouteRefreshUsesBoundedEgressQueue);                                     \
  FRIEND_TEST(                                                                 \
      SendBgpMessagesFixtureWithBackpressure,                                  \
      RouteRefreshFailsWhenBoundedEgressQueueIsBlocked);                       \
  FRIEND_TEST(                                                                 \
      SendBgpMessagesFixtureWithBackpressure,                                  \
      RouteRefreshFailsForUnnegotiatedAfi);                                    \
  FRIEND_TEST(                                                                 \
      SendBgpMessagesFixtureWithBackpressure,                                  \
      CommittedUpdateCountDoesNotCrossClear);                                  \
  FRIEND_TEST(                                                                 \
      SendBgpMessagesFixtureWithBackpressure,                                  \
      SendBgpUpdateMessagesTest_AfterEoR);                                     \
  FRIEND_TEST(                                                                 \
      SendBgpMessagesFixtureWithBackpressure,                                  \
      SendBgpUpdateMessagesTest_BeforeEoR);                                    \
  FRIEND_TEST(                                                                 \
      SendBgpMessagesFixtureWithBackpressure,                                  \
      SendBgpUpdateMessagesTest_PendingEoR);                                   \
  FRIEND_TEST(SendBgpMessagesFixtureWithBackpressure, QueueCloseTest);         \
  FRIEND_TEST(                                                                 \
      SendBgpMessagesFixtureWithBackpressure,                                  \
      SessionTerminatedQueueCloseWhileProducerWaiting);                        \
  FRIEND_TEST(                                                                 \
      SendBgpMessagesFixtureWithBackpressure,                                  \
      SessionTerminatedWhileQueueBlockedTerminatesSender);                     \
  FRIEND_TEST(                                                                 \
      SendBgpMessagesFixtureWithBackpressure,                                  \
      SendBgpUpdatesNoChangelistDrainsFull);                                   \
  FRIEND_TEST(                                                                 \
      SendBgpMessagesFixtureWithBackpressure,                                  \
      ActiveSenderRetriesFullDrainViaPackingTimer);                            \
  FRIEND_TEST(                                                                 \
      SendBgpMessagesFixtureWithBackpressure,                                  \
      ActivateDetachedModeProcessingDrainsPL);

#define AdjRibStats_TEST_FRIENDS                                               \
  friend class SendBgpMessagesFixture;                                         \
  FRIEND_TEST(SendBgpMessagesFixture, SessionTerminatedCleanUp);               \
  FRIEND_TEST(SendBgpMessagesFixture, NumTransientUpdatesSuppressedStatsTest); \
  FRIEND_TEST(                                                                 \
      SendBgpMessagesFixtureWithBackpressure,                                  \
      SessionTerminatedWhileQueueBlockedTerminatesSender);

#include "neteng/fboss/bgp/cpp/tests/AdjRibOutUtils.h"
#include "neteng/fboss/bgp/cpp/tests/BoundedWaitUtils.h"

namespace facebook::bgp {

using namespace facebook::nettools::bgplib;
using namespace ::testing;

/**
 * Test class for SendBgpMessages.
 *
 * This suite contains under non-backpressure scenarios to ensure that
 * packing is producing the correct messages to the adjRibOutQueue_
 * egress queue..
 */
class SendBgpMessagesFixture : public AdjRibOutboundFixture {
 public:
  void SetUp() override {
    // Register Singleton
    folly::SingletonVault::singleton()->registrationComplete();
    AdjRibPolicyCache::get()->clearCache();
    AdjRibPolicyCache::get()->resetTotalCacheHit();
    AdjRibPolicyCache::get()->resetTotalCacheMiss();
    DeDuplicatedBgpPath::clearDeduplicator();
    postPolicyResultCache_.clear();
    fm_ = std::make_unique<folly::fibers::FiberManager>(
        std::make_unique<folly::fibers::EventBaseLoopController>(),
        facebook::nettools::bgplib::getFiberManagerOptions());

    static_cast<folly::fibers::EventBaseLoopController&>(fm_->loopController())
        .attachEventBase(evb_);
  }

  void TearDown() override {
    /*
     * Since the changeTracker isn't on the base class, we need to
     * clean up the changeListConsumer here before AdjRibOutboundFixture
     * gets called.
     */
    adjRib_->resetChangeListConsumer();
  }

  // Sets up the adjRib_ without running message processing loop.
  void SetUpAdjRibStateForUnit(
      const bool eorPending,
      const bool eorSent,
      const bool enableIPv4 = true,
      const bool enableIPv6 = false,
      const bool enableEgressQueueBackpressure = true) {
    setupAdjRibForOutUnitTest();

    // Set up EoR sent adjRib state.
    adjRib_->egressEoRsSent_ = eorSent;

    adjRib_->isAfiIpv4Negotiated_ = enableIPv4;
    adjRib_->isAfiIpv6Negotiated_ = enableIPv6;

    adjRib_->setEgressEoRsPending(
        adjRib_->isAfiIpv4Negotiated_ && eorPending,
        adjRib_->isAfiIpv6Negotiated_ && eorPending);

    // Attach adjRibOutQueue_.
    adjRib_->adjRibOutQueue_ =
        std::make_shared<AdjRib::AdjRibOutQueueT>(capacity_, highWm_, lowWm_);
    adjRib_->pathIdGenerator_ = std::make_unique<PathIdGenerator>(false);

    // Clear any pending state. Should be empty regardless, but just in case.
    adjRib_->attrToPrefixMap_.clear();
  }

  void MockOutDelayPrefixes(std::vector<folly::CIDRNetwork> pfxs) {
    adjRib_->newDeferredPrefixes_ = std::move(pfxs);
  }

  void MockChangeListConsumer() {
    /*
     * Passing in nullptr for adjRib since we don't need to do anything
     * with the adjRib_ itself for this mock. Also because
     * the signature needs shared_ptr but the test instance is a unique_ptr.
     */
    static ConsumerBitmap dummyAddPathBitmap;
    static ConsumerBitmap dummyNonAddPathBitmap;
    auto changeListConsumer = std::make_shared<AdjRibOutConsumer>(
        changeListTracker_,
        nullptr /* adjRib */,
        "Test ChangeList Consumer",
        adjRib_->evb_,
        dummyAddPathBitmap,
        dummyNonAddPathBitmap);
    adjRib_->setChangeListConsumer(changeListConsumer);
  }

  void UpdateAttrToPrefixMap(
      std::shared_ptr<const BgpPath> attr,
      std::vector<folly::CIDRNetwork> pfxs,
      int repeat = 1) {
    PrefixSet pfxSetV4;
    PrefixSet pfxSetV6;
    for (auto& pfx : pfxs) {
      for (int i = 0; i < repeat; ++i) {
        if (pfx.first.isV4()) {
          pfxSetV4.emplace(pfx, kDefaultPathID + i);
        } else {
          pfxSetV6.emplace(pfx, kDefaultPathID + i);
        }
      }
    }
    if (!pfxSetV4.empty()) {
      auto keyV4 = BgpPathWithAfi{attr, BgpUpdateAfi::AFI_IPv4};
      adjRib_->attrToPrefixMap_.emplace(keyV4, pfxSetV4);
    }
    if (!pfxSetV6.empty()) {
      auto keyV6 = BgpPathWithAfi{attr, BgpUpdateAfi::AFI_IPv6};
      adjRib_->attrToPrefixMap_.emplace(keyV6, pfxSetV6);
    }
  }

  std::shared_ptr<const BgpPath> GetBgpPath(const folly::IPAddress& nh) {
    auto update = buildBgpUpdateAttributes(nh);
    return std::make_shared<facebook::bgp::BgpPath>(
        BgpPathFields(*BgpUpdate2toBgpPathC(update)));
  }

  int capacity_{10};
  int highWm_{8};
  int lowWm_{2};

 private:
  std::shared_ptr<ChangeTracker<ShadowRibEntry>> changeListTracker_ =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("Test ChangeTracker");
};

/**
 * Test class for SendBgpMessages that tests backpressure scenarios.
 * Tests the following behaviors:
 *
 *   1. Notification could not be queued from external caller and is pending.
 *   2. Route refresh could not be queued from external caller and is pending
 *   (v4 and v6).
 *   3. EoR could not be queued and is pending (v4 and v6).
 *   4. Queue blocks while draining the packing list during RIB dump.
 *   5. Queue blocks while draining the packing list after RIB dump/EoR.
 */
class SendBgpMessagesFixtureWithBackpressure : public SendBgpMessagesFixture {
 public:
  /* Simulate backpressure by loading up the queue. */
  void FillQueueToSize(int sz) {
    for (int i = 0; i < sz; ++i) {
      /* Push empty values that aren't the termination signal. */
      adjRib_->adjRibOutQueue_->push(nullptr /* empty BgpUpdate2 */);
    }
  }

  /* Drain the queue to low watermark. */
  folly::coro::Task<bool> DrainQueueToLowWm() {
    while (adjRib_->adjRibOutQueue_->isBlocked()) {
      co_await facebook::bgp::test::boundedPop(
          *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");
    }
    co_return true;
  }

  folly::coro::Task<bool> WaitForPackingListToDrain() {
    for (size_t attempts = 0; attempts < 10000; ++attempts) {
      if (adjRib_->attrToPrefixMap_.empty()) {
        co_return true;
      }
      co_await folly::coro::co_reschedule_on_current_executor;
    }
    co_return false;
  }
};

TEST_F(SendBgpMessagesFixture, SessionEstablishedCleanUp) {
  setupAdjRibForOutUnitTest();
  adjRib_->setEgressEoRsPending(true, true);
  adjRib_->egressEoRsSent_ = true;
  adjRib_->sendCoroScheduled_ = true;

  adjRib_->sessionEstablished(
      std::nullopt /* remoteGrRestartTime */,
      std::make_shared<AdjRib::AdjRibInQueueT>(),
      std::make_shared<AdjRib::AdjRibOutQueueT>(capacity_, highWm_, lowWm_));

  EXPECT_FALSE(adjRib_->egressEoRsPending());
  EXPECT_FALSE(adjRib_->egressEoRsSent_);
  EXPECT_FALSE(adjRib_->sendCoroScheduled_);
}

TEST_F(SendBgpMessagesFixture, SessionTerminatedCleanUp) {
  setupAdjRibForOutUnitTest();
  /* Set pending state in attrToPrefixMap. */
  UpdateAttrToPrefixMap(nullptr, {kV4Prefix1});

  /* Update all stats. */
  adjRib_->stats_.incrementPreInPrefixCount(
      kV4Prefix1, true /* isVipPrefix */, true /* isGoldenVip */);
  adjRib_->stats_.incrementPostInPrefixCount(kV4Prefix1.first.isV4());
  adjRib_->stats_.incrementRecvUpdateMsgs();
  adjRib_->stats_.incrementRecvAnnouncementsIpv4();
  adjRib_->stats_.incrementRecvAnnouncementsIpv6();
  adjRib_->stats_.incrementRecvWithdrawals();
  adjRib_->stats_.incrementEnforceFirstAsRejects();
  auto path = GetBgpPath(kV4Nexthop1);
  adjRib_->stats_.updateAttributeSizes(path);

  adjRib_->stats_.incrementEgressQueueBackpressuredEvents();
  adjRib_->stats_.incrementTransientRouteUpdatesSuppressed();
  adjRib_->stats_.addEgressQueueBlockDuration(1);
  adjRib_->stats_.setLastEgressQueueBlockTime(1);
  adjRib_->stats_.incrementSentUpdateMsgs(1);
  adjRib_->stats_.incrementSentAnnouncementsIpv4();
  adjRib_->stats_.incrementSentAnnouncementsIpv6();
  adjRib_->stats_.incrementSentWithdrawals();
  adjRib_->stats_.incrementPreOutPrefixCount(kV4Prefix1.first.isV4());
  adjRib_->stats_.incrementPostOutPrefixCount(kV4Prefix1.first.isV4());

  EXPECT_EQ(1, adjRib_->stats_.vipPrefixes.size());
  EXPECT_EQ(1, adjRib_->stats_.preInPrefixCount);
  EXPECT_EQ(1, adjRib_->stats_.postInPrefixCount);
  EXPECT_EQ(1, adjRib_->stats_.preOutPrefixCount);
  EXPECT_EQ(1, adjRib_->stats_.postOutPrefixCount);
  EXPECT_EQ(1, adjRib_->stats_.preInPrefixCountIpv4);
  EXPECT_EQ(0, adjRib_->stats_.preInPrefixCountIpv6);
  EXPECT_EQ(1, adjRib_->stats_.postInPrefixCountIpv4);
  EXPECT_EQ(0, adjRib_->stats_.postInPrefixCountIpv6);
  EXPECT_EQ(1, adjRib_->stats_.preOutPrefixCountIpv4);
  EXPECT_EQ(0, adjRib_->stats_.preOutPrefixCountIpv6);
  EXPECT_EQ(1, adjRib_->stats_.postOutPrefixCountIpv4);
  EXPECT_EQ(0, adjRib_->stats_.postOutPrefixCountIpv6);
  EXPECT_EQ(1, adjRib_->stats_.recvUpdateMsgs);
  EXPECT_EQ(1, adjRib_->stats_.sentAnnouncementsIpv4);
  EXPECT_EQ(1, adjRib_->stats_.sentAnnouncementsIpv6);
  EXPECT_EQ(1, adjRib_->stats_.recvAnnouncementsIpv4);
  EXPECT_EQ(1, adjRib_->stats_.recvAnnouncementsIpv6);
  EXPECT_EQ(1, adjRib_->stats_.sentWithdrawals);
  EXPECT_EQ(1, adjRib_->stats_.recvWithdrawals);
  EXPECT_EQ(1, adjRib_->stats_.totalAttributeUpdates);
  EXPECT_EQ(1, adjRib_->stats_.totalEnforceFirstAsRejects);
  EXPECT_EQ(1, adjRib_->stats_.egressQueueBlocks);
  EXPECT_EQ(1, adjRib_->stats_.egressQueueTotalBlockDurationMs);
  EXPECT_EQ(1, adjRib_->stats_.lastEgressQueueBlockTimeMs);
  EXPECT_EQ(1, adjRib_->stats_.transientRouteUpdatesSuppressed);

  folly::coro::blockingWait(
      adjRib_->sessionTerminated(FiberBgpPeer::BgpSessionStop{}));

  EXPECT_EQ(1, adjRib_->stats_.vipPrefixes.size());
  EXPECT_EQ(1, adjRib_->stats_.preInPrefixCount);
  EXPECT_EQ(1, adjRib_->stats_.postInPrefixCount);
  EXPECT_EQ(1, adjRib_->stats_.preOutPrefixCount);
  EXPECT_EQ(1, adjRib_->stats_.postOutPrefixCount);
  EXPECT_EQ(1, adjRib_->stats_.preInPrefixCountIpv4);
  EXPECT_EQ(0, adjRib_->stats_.preInPrefixCountIpv6);
  EXPECT_EQ(1, adjRib_->stats_.postInPrefixCountIpv4);
  EXPECT_EQ(0, adjRib_->stats_.postInPrefixCountIpv6);
  EXPECT_EQ(1, adjRib_->stats_.preOutPrefixCountIpv4);
  EXPECT_EQ(0, adjRib_->stats_.preOutPrefixCountIpv6);
  EXPECT_EQ(1, adjRib_->stats_.postOutPrefixCountIpv4);
  EXPECT_EQ(0, adjRib_->stats_.postOutPrefixCountIpv6);
  EXPECT_EQ(0, adjRib_->stats_.sentUpdateMsgs);
  EXPECT_EQ(0, adjRib_->stats_.recvUpdateMsgs);
  EXPECT_EQ(0, adjRib_->stats_.sentAnnouncementsIpv4);
  EXPECT_EQ(0, adjRib_->stats_.sentAnnouncementsIpv6);
  EXPECT_EQ(0, adjRib_->stats_.recvAnnouncementsIpv4);
  EXPECT_EQ(0, adjRib_->stats_.recvAnnouncementsIpv6);
  EXPECT_EQ(0, adjRib_->stats_.sentWithdrawals);
  EXPECT_EQ(0, adjRib_->stats_.recvWithdrawals);
  EXPECT_EQ(1, adjRib_->stats_.totalAttributeUpdates);
  EXPECT_EQ(1, adjRib_->stats_.totalEnforceFirstAsRejects);
  EXPECT_EQ(0, adjRib_->stats_.egressQueueBlocks);
  EXPECT_EQ(0, adjRib_->stats_.egressQueueTotalBlockDurationMs);
  EXPECT_EQ(0, adjRib_->stats_.lastEgressQueueBlockTimeMs);
  EXPECT_EQ(0, adjRib_->stats_.transientRouteUpdatesSuppressed);

  EXPECT_TRUE(adjRib_->attrToPrefixMap_.empty());
}

TEST_F(SendBgpMessagesFixture, ScheduleSendBgpMessagesTest) {
  SetUpAdjRibStateForUnit(false /* eorPending */, false /* eorSent */);

  EXPECT_FALSE(adjRib_->sendCoroScheduled_);
  EXPECT_EQ(0, adjRib_->asyncScope_->remaining());

  // Case 1: Coro hasn't been scheduled yet.
  adjRib_->scheduleSendBgpUpdates(true /* tryPullNewChangeItems */);
  EXPECT_EQ(1, adjRib_->asyncScope_->remaining());
  EXPECT_TRUE(adjRib_->sendCoroScheduled_);

  // Case 2: Coro has already been scheduled and isn't scheduled again.
  adjRib_->scheduleSendBgpUpdates(true /* tryPullNewChangeItems */);
  EXPECT_EQ(1, adjRib_->asyncScope_->remaining());
  EXPECT_TRUE(adjRib_->sendCoroScheduled_);

  // Let the scheduled coro run for test cleanup.
  adjRib_->evb_.loopOnce();
  EXPECT_FALSE(adjRib_->sendCoroScheduled_);
}

CO_TEST_F(SendBgpMessagesFixture, WaitForQueueSpaceTest) {
  SetUpAdjRibStateForUnit(true /* eorPending */, false /* eorSent */);
  /* Case 1: Queue is not blocked. */
  auto backpressured = co_await adjRib_->waitForQueueSpace();
  EXPECT_FALSE(backpressured);
}

CO_TEST_F(SendBgpMessagesFixtureWithBackpressure, WaitForQueueSpaceTest) {
  SetUpAdjRibStateForUnit(true /* eorPending */, false /* eorSent */);
  FillQueueToSize(highWm_);
  // Start event base loop
  std::thread evbThread([this]() { evb_.loopForever(); });

  /* Wait for queue space to free up while queue is draining. */
  auto [backpressured, drained] = co_await folly::coro::collectAll(
      co_withExecutor(&evb_, adjRib_->waitForQueueSpace()),
      co_withExecutor(&evb_, DrainQueueToLowWm()));

  evb_.terminateLoopSoon();
  evbThread.join();

  EXPECT_TRUE(backpressured);
  EXPECT_TRUE(drained);
}

CO_TEST_F(SendBgpMessagesFixture, SendPendingEoRsTest) {
  SetUpAdjRibStateForUnit(
      true /* eorPending */,
      false /* eorSent */,
      true /* v4Afi */,
      true /* v6Afi */);
  /* Case 1: Queue is not blocked. */
  auto [backpressured, eorCnt] = co_await adjRib_->sendPendingEoRs();
  EXPECT_FALSE(backpressured);
  EXPECT_EQ(2, eorCnt);
  EXPECT_EQ(eorCnt, adjRib_->adjRibOutQueue_->size());
  EXPECT_EQ(0, adjRib_->getStats().getSentUpdateMsgs());
  EXPECT_EQ(2, adjRib_->getStats().getSentEndOfRibMsgs());
}

CO_TEST_F(SendBgpMessagesFixtureWithBackpressure, SendPendingEoRsTest) {
  SetUpAdjRibStateForUnit(
      true /* eorPending */,
      false /* eorSent */,
      true /* v4Afi */,
      true /* v6Afi */);
  FillQueueToSize(highWm_);
  EXPECT_TRUE(adjRib_->egressEoRsPending());

  // Start event base loop
  std::thread evbThread([this]() { evb_.loopForever(); });

  /* Wait for EORs to send while queue is draining. */
  auto [eorPendingStatus, drained] = co_await folly::coro::collectAll(
      co_withExecutor(&evb_, adjRib_->sendPendingEoRs()),
      co_withExecutor(&evb_, DrainQueueToLowWm()));

  evb_.terminateLoopSoon();
  evbThread.join();

  EXPECT_TRUE(drained);
  EXPECT_TRUE(eorPendingStatus.first);
  EXPECT_EQ(2, eorPendingStatus.second);
  EXPECT_EQ(0, adjRib_->getStats().getSentUpdateMsgs());
  EXPECT_EQ(2, adjRib_->getStats().getSentEndOfRibMsgs());

  /* The EoRs should be included in the remaining items in the queue. */
  bool v4Pending = true, v6Pending = true;

  while (v4Pending || v6Pending) {
    auto msg = co_await facebook::bgp::test::boundedPop(
        *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");

    /* Expect to eventually see the EORs. */
    if (msg && std::holds_alternative<BgpEndOfRib>(*msg)) {
      auto eor = std::get<BgpEndOfRib>(*msg);
      if (BgpUpdateAfi::AFI_IPv4 == eor.afi()) {
        v4Pending = false;
      } else if (BgpUpdateAfi::AFI_IPv6 == eor.afi()) {
        v6Pending = false;
      }
    }
  }
  EXPECT_FALSE(adjRib_->egressEoRsPending());
}

TEST_F(
    SendBgpMessagesFixtureWithBackpressure,
    RouteRefreshUsesBoundedEgressQueue) {
  SetUpAdjRibStateForUnit(
      false /* eorPending */,
      false /* eorSent */,
      true /* v4Afi */,
      true /* v6Afi */);

  for (const auto afi : {BgpUpdateAfi::AFI_IPv4, BgpUpdateAfi::AFI_IPv6}) {
    EXPECT_TRUE(adjRib_->buildAndSendRouteRefresh(
        BgpRouteRefreshMessageSubtype::ROUTE_REFRESH_REQUEST, afi));
  }

  ASSERT_EQ(adjRib_->adjRibOutQueue_->size(), 2);
  for (const auto expectedAfi :
       {BgpUpdateAfi::AFI_IPv4, BgpUpdateAfi::AFI_IPv6}) {
    const auto message = facebook::bgp::test::boundedBlockingPop(
        *adjRib_->adjRibOutQueue_, "adjRibOutQueue_");
    ASSERT_TRUE(std::holds_alternative<BgpRouteRefresh>(*message));
    EXPECT_EQ(expectedAfi, std::get<BgpRouteRefresh>(*message).afi().value());
  }
}

TEST_F(
    SendBgpMessagesFixtureWithBackpressure,
    RouteRefreshFailsWhenBoundedEgressQueueIsBlocked) {
  SetUpAdjRibStateForUnit(
      false /* eorPending */,
      false /* eorSent */,
      true /* v4Afi */,
      true /* v6Afi */);
  FillQueueToSize(highWm_);

  EXPECT_FALSE(adjRib_->buildAndSendRouteRefresh(
      BgpRouteRefreshMessageSubtype::ROUTE_REFRESH_REQUEST,
      BgpUpdateAfi::AFI_IPv4));

  EXPECT_EQ(adjRib_->adjRibOutQueue_->size(), highWm_);
}

TEST_F(
    SendBgpMessagesFixtureWithBackpressure,
    RouteRefreshFailsForUnnegotiatedAfi) {
  SetUpAdjRibStateForUnit(
      false /* eorPending */,
      false /* eorSent */,
      false /* v4Afi */,
      true /* v6Afi */);

  EXPECT_FALSE(adjRib_->buildAndSendRouteRefresh(
      BgpRouteRefreshMessageSubtype::ROUTE_REFRESH_REQUEST,
      BgpUpdateAfi::AFI_IPv4));

  EXPECT_TRUE(adjRib_->adjRibOutQueue_->empty());
}

TEST_F(
    SendBgpMessagesFixtureWithBackpressure,
    RouteRefreshOnlyMarksRequestedAfiEoRPending) {
  SetUpAdjRibStateForUnit(
      false /* eorPending */,
      false /* eorSent */,
      true /* v4Afi */,
      true /* v6Afi */);

  RibOutAnnouncement announcement;
  announcement.initialDump = true;
  announcement.routeRefreshAfi = BgpUpdateAfi::AFI_IPv4;
  announcement.sendWithEoR = true;
  adjRib_->processRibMessage(announcement);

  EXPECT_TRUE(adjRib_->egressEoRPendingV4());
  EXPECT_FALSE(adjRib_->egressEoRPendingV6());
}

TEST_F(
    SendBgpMessagesFixtureWithBackpressure,
    RouteRefreshDoesNotMarkUnnegotiatedAfiEoRPending) {
  SetUpAdjRibStateForUnit(
      false /* eorPending */,
      false /* eorSent */,
      false /* v4Afi */,
      true /* v6Afi */);

  RibOutAnnouncement announcement;
  announcement.initialDump = true;
  announcement.routeRefreshAfi = BgpUpdateAfi::AFI_IPv4;
  announcement.sendWithEoR = true;
  adjRib_->processRibMessage(announcement);

  EXPECT_FALSE(adjRib_->egressEoRPendingV4());
  EXPECT_FALSE(adjRib_->egressEoRPendingV6());
}

CO_TEST_F(
    SendBgpMessagesFixtureWithBackpressure,
    CommittedUpdateCountDoesNotCrossClear) {
  SetUpAdjRibStateForUnit(false /* eorPending */, false /* eorSent */);
  adjRib_->adjRibOutQueue_ = std::make_shared<AdjRib::AdjRibOutQueueT>(
      3 /* capacity */, 1 /* highWm */, 0 /* lowWm */);

  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop2), {kV4Prefix1});
  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop3), {kV4Prefix2});

  std::thread evbThread([this]() { evb_.loopForever(); });

  auto clearAndUnblock = [&]() -> folly::coro::Task<void> {
    while (!adjRib_->adjRibOutQueue_->isBlocked()) {
      co_await folly::coro::co_reschedule_on_current_executor;
    }

    /*
     * The first UPDATE owns a queue slot before the sender suspends on the
     * second UPDATE, so its count must already be visible to counter clear.
     */
    EXPECT_EQ(1, adjRib_->getStats().getSentUpdateMsgs());
    adjRib_->clearEgressMessageCounts();
    EXPECT_EQ(0, adjRib_->getStats().getSentUpdateMsgs());

    co_await facebook::bgp::test::boundedPop(
        *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");
  };

  co_await folly::coro::collectAll(
      co_withExecutor(
          &evb_, adjRib_->sendBgpUpdates(false /* tryPullNewChangeItems */)),
      co_withExecutor(&evb_, clearAndUnblock()));

  evb_.terminateLoopSoon();
  evbThread.join();

  /* Only the UPDATE queued after clear belongs to the new counter epoch. */
  EXPECT_EQ(1, adjRib_->getStats().getSentUpdateMsgs());
  EXPECT_EQ(1, adjRib_->adjRibOutQueue_->size());
}

CO_TEST_F(SendBgpMessagesFixture, SimpleSendBgpUpdateMessagesTest) {
  /* attrToPrefixMap_ should make 4 announcements + 1 EoR. */
  SetUpAdjRibStateForUnit(true /* eorPending */, false /* eorSent */);

  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop2), {kV4Prefix1});
  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop3), {kV4Prefix2});
  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop4), {kV4Prefix3});
  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop5), {kV4Prefix4});

  co_await adjRib_->sendBgpUpdates(false /* tryPullNewChangeItems */);

  EXPECT_EQ(5, adjRib_->adjRibOutQueue_->size());

  /* Check to see all the expected nexthops and prefixes are there. */
  std::set<network::thrift::BinaryAddress> seenNexthops;
  std::set<network::thrift::IPPrefix> seenPrefixes;
  while (adjRib_->adjRibOutQueue_->size() > 1) {
    auto msg = co_await facebook::bgp::test::boundedPop(
        *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");
    auto update =
        std::get<std::shared_ptr<const facebook::nettools::bgplib::BgpUpdate2>>(
            *msg);
    EXPECT_EQ(1, update->mpAnnounced()->prefixes()->size());
    seenNexthops.insert(*update->mpAnnounced()->nexthop());
    seenPrefixes.insert(*update->mpAnnounced()->prefixes()->front().prefix());
  }
  auto eor = co_await facebook::bgp::test::boundedPop(
      *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");
  EXPECT_TRUE(std::holds_alternative<BgpEndOfRib>(*eor));

  EXPECT_EQ(1, seenNexthops.size());
  EXPECT_EQ(4, seenPrefixes.size());

  /* Local bgp id due to ebgp session. */
  EXPECT_TRUE(seenNexthops.contains(network::toBinaryAddress(kV4Nexthop1)));

  EXPECT_TRUE(seenPrefixes.contains(network::toIPPrefix(kV4Prefix1)));
  EXPECT_TRUE(seenPrefixes.contains(network::toIPPrefix(kV4Prefix2)));
  EXPECT_TRUE(seenPrefixes.contains(network::toIPPrefix(kV4Prefix3)));
  EXPECT_TRUE(seenPrefixes.contains(network::toIPPrefix(kV4Prefix4)));

  EXPECT_FALSE(adjRib_->egressEoRsPending());
  EXPECT_TRUE(adjRib_->egressEoRsSent_);
  EXPECT_TRUE(adjRib_->attrToPrefixMap_.empty());
  EXPECT_EQ(4, adjRib_->getStats().getSentUpdateMsgs());
  EXPECT_EQ(1, adjRib_->getStats().getSentEndOfRibMsgs());
}

CO_TEST_F(SendBgpMessagesFixture, BulkSendBgpUpdateMessagesTest) {
  /* attrToPrefixMap_ should make 4 announcements + 1 EoR. */
  SetUpAdjRibStateForUnit(true /* eorPending */, false /* eorSent */);
  adjRib_->sendAddPath_ = true;
  adjRib_->adjRibOutQueue_ =
      std::make_shared<AdjRib::AdjRibOutQueueT>(20, 10, 2);

  int maxPathId = 600;
  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop2), {kV4Prefix1}, maxPathId);
  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop3), {kV4Prefix2}, maxPathId);
  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop4), {kV4Prefix3}, maxPathId);
  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop5), {kV4Prefix4}, maxPathId);

  co_await adjRib_->sendBgpUpdates(false /* tryPullNewChangeItems */);

  EXPECT_EQ(5, adjRib_->adjRibOutQueue_->size());

  /*
   * Each path should advertise prefix up to maxPathId times.
   * Check to see all the expected nexthops and prefixes are there.
   */
  std::set<network::thrift::BinaryAddress> seenNexthops;
  std::set<std::pair<network::thrift::IPPrefix, uint32_t>> seenPrefixes;
  while (adjRib_->adjRibOutQueue_->size() > 1) {
    auto msg = co_await facebook::bgp::test::boundedPop(
        *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");
    auto update =
        std::get<std::shared_ptr<const facebook::nettools::bgplib::BgpUpdate2>>(
            *msg);
    seenNexthops.insert(*update->mpAnnounced()->nexthop());
    for (auto& prefix : *update->mpAnnounced()->prefixes()) {
      seenPrefixes.insert(std::make_pair(*prefix.prefix(), *prefix.pathId()));
    }
  }
  auto eor = co_await facebook::bgp::test::boundedPop(
      *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");
  EXPECT_TRUE(std::holds_alternative<BgpEndOfRib>(*eor));

  EXPECT_EQ(1, seenNexthops.size());
  EXPECT_EQ(4 * maxPathId, seenPrefixes.size());
  for (int i = 0; i < maxPathId; ++i) {
    EXPECT_TRUE(seenPrefixes.contains(
        std::make_pair(network::toIPPrefix(kV4Prefix1), i)));
    EXPECT_TRUE(seenPrefixes.contains(
        std::make_pair(network::toIPPrefix(kV4Prefix2), i)));
    EXPECT_TRUE(seenPrefixes.contains(
        std::make_pair(network::toIPPrefix(kV4Prefix3), i)));
    EXPECT_TRUE(seenPrefixes.contains(
        std::make_pair(network::toIPPrefix(kV4Prefix4), i)));
  }

  /* Local bgp id due to ebgp session. */
  EXPECT_TRUE(seenNexthops.contains(network::toBinaryAddress(kV4Nexthop1)));

  EXPECT_FALSE(adjRib_->egressEoRsPending());
  EXPECT_TRUE(adjRib_->egressEoRsSent_);
  EXPECT_TRUE(adjRib_->attrToPrefixMap_.empty());
}

CO_TEST_F(SendBgpMessagesFixture, SendBgpUpdateMessagesTest_AfterEoR) {
  /* Case 1: Empty attrToPrefixMap_. */
  {
    SetUpAdjRibStateForUnit(false /* eorPending */, true /* eorSent */);

    co_await adjRib_->sendBgpUpdates(true /* tryPullNewChangeItems */);

    EXPECT_TRUE(adjRib_->adjRibOutQueue_->empty());

    EXPECT_FALSE(adjRib_->egressEoRsPending());
    EXPECT_TRUE(adjRib_->egressEoRsSent_);
  }

  /* Case 2: attrToPrefixMap_ should make 1 withdrawal + 1 announcement. */
  {
    SetUpAdjRibStateForUnit(false /* eorPending */, true /* eorSent */);

    UpdateAttrToPrefixMap(nullptr, {kV4Prefix1});
    UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop1), {kV4Prefix2});

    co_await adjRib_->sendBgpUpdates(true /* tryPullNewChangeItems */);

    EXPECT_EQ(2, adjRib_->adjRibOutQueue_->size());

    EXPECT_FALSE(adjRib_->egressEoRsPending());
    EXPECT_TRUE(adjRib_->egressEoRsSent_);
  }
}

CO_TEST_F(
    SendBgpMessagesFixtureWithBackpressure,
    SendBgpUpdateMessagesTest_AfterEoR) {
  SetUpAdjRibStateForUnit(false /* eorPending */, true /* eorSent */);
  FillQueueToSize(highWm_ - 1);

  UpdateAttrToPrefixMap(nullptr, {kV4Prefix1});
  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop1), {kV4Prefix2});

  // Start event base loop
  std::thread evbThread([this]() { evb_.loopForever(); });

  auto& messages = subscribeToLogMessages("");
  /* Wait for queue space to free up while queue is draining. */
  auto [_, drained] = co_await folly::coro::collectAll(
      co_withExecutor(
          &evb_, adjRib_->sendBgpUpdates(true /* tryPullNewChangeItems */)),
      co_withExecutor(&evb_, DrainQueueToLowWm()));

  evb_.terminateLoopSoon();
  evbThread.join();

  EXPECT_TRUE(drained);
  EXPECT_TRUE(messages[0].first.getMessage().ends_with("Backpressured = true"));

  /* Only one message should have made it into the queue before we were
   * blocked. Because force=false, we break early on backpressure.
   */

  /* Last message that made it into the queue should be announcement. */
  while (adjRib_->adjRibOutQueue_->size() > 1) {
    /* Everything else in the queue was nullptr padding. */
    auto msg = co_await facebook::bgp::test::boundedPop(
        *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");
    EXPECT_EQ(nullptr, std::get<std::shared_ptr<const BgpUpdate2>>(*msg));
  }
  EXPECT_EQ(1, adjRib_->adjRibOutQueue_->size());
  auto msg1 = co_await facebook::bgp::test::boundedPop(
      *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");

  auto announcement = std::get<std::shared_ptr<const BgpUpdate2>>(*msg1);
  EXPECT_EQ(
      network::toIPPrefix(kV4Prefix2),
      announcement->mpAnnounced()->prefixes()->front().prefix());
}

CO_TEST_F(SendBgpMessagesFixture, SendBgpUpdateMessagesTest_BeforeEoR) {
  /* Case 1: Empty attrToPrefixMap_. */
  {
    SetUpAdjRibStateForUnit(false /* eorPending */, false /* eorSent */);

    co_await adjRib_->sendBgpUpdates(false /* tryPullNewChangeItems */);

    EXPECT_TRUE(adjRib_->adjRibOutQueue_->empty());
  }

  /* Case 2: attrToPrefixMap_ should make 1 withdrawal + 1 announcement. */
  {
    SetUpAdjRibStateForUnit(false /* eorPending */, false /* eorSent */);

    UpdateAttrToPrefixMap(nullptr, {kV4Prefix1});
    UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop1), {kV4Prefix2});

    co_await adjRib_->sendBgpUpdates(false /* tryPullNewChangeItems */);

    EXPECT_EQ(2, adjRib_->adjRibOutQueue_->size());

    EXPECT_FALSE(adjRib_->egressEoRsPending());
    EXPECT_FALSE(adjRib_->egressEoRsSent_);
  }
}

CO_TEST_F(
    SendBgpMessagesFixtureWithBackpressure,
    SendBgpUpdateMessagesTest_BeforeEoR) {
  SetUpAdjRibStateForUnit(false /* eorPending */, false /* eorSent */);
  UpdateAttrToPrefixMap(nullptr, {kV4Prefix1});
  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop1), {kV4Prefix2});
  FillQueueToSize(highWm_);

  std::thread evbThread([this]() { evb_.loopForever(); });
  EXPECT_EQ(2, adjRib_->attrToPrefixMap_.size());

  auto DrainPadding = [&]() -> folly::coro::Task<bool> {
    bool allMessagesArePadding = true;
    while (!adjRib_->adjRibOutQueue_->empty()) {
      auto msg = co_await facebook::bgp::test::boundedPop(
          *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");
      auto update = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
      if (update) {
        allMessagesArePadding = false;
        break;
      }
    }
    co_return allMessagesArePadding;
  };

  auto& messages = subscribeToLogMessages("");
  auto [_, allMsgsNull] = co_await folly::coro::collectAll(
      co_withExecutor(
          &evb_, adjRib_->sendBgpUpdates(true /* tryPullNewChangeItems */)),
      co_withExecutor(&evb_, DrainPadding()));

  evb_.terminateLoopSoon();
  evbThread.join();

  EXPECT_TRUE(allMsgsNull);
  EXPECT_FALSE(adjRib_->egressEoRsSent_);
  EXPECT_TRUE(messages.empty());
  EXPECT_TRUE(adjRib_->adjRibOutQueue_->empty());
  EXPECT_EQ(2, adjRib_->attrToPrefixMap_.size());
}

CO_TEST_F(SendBgpMessagesFixture, SendBgpUpdateMessagesTest_PendingEoR) {
  SetUpAdjRibStateForUnit(true /* eorPending */, false /* eorSent */);

  UpdateAttrToPrefixMap(nullptr, {kV4Prefix1});
  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop1), {kV4Prefix2});

  co_await adjRib_->sendBgpUpdates(false /* tryPullNewChangeItems */);

  // Check that sendBgpUpdates updated egressEorSent_ to true.
  EXPECT_TRUE(adjRib_->egressEoRsSent_);

  // Verify withdrawal, announcement, and EoR (in order).
  EXPECT_EQ(3, adjRib_->adjRibOutQueue_->size());
  auto msg1 = co_await facebook::bgp::test::boundedPop(
      *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");
  auto msg2 = co_await facebook::bgp::test::boundedPop(
      *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");
  auto msg3 = co_await facebook::bgp::test::boundedPop(
      *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");

  auto announcement = std::get<std::shared_ptr<const BgpUpdate2>>(*msg1);
  EXPECT_EQ(
      network::toIPPrefix(kV4Prefix2),
      announcement->mpAnnounced()->prefixes()->front().prefix());

  auto withdrawal = std::get<std::shared_ptr<const BgpUpdate2>>(*msg2);
  EXPECT_EQ(
      network::toIPPrefix(kV4Prefix1),
      withdrawal->v4Withdrawn2()->front().prefix());

  EXPECT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg3));
}

CO_TEST_F(
    SendBgpMessagesFixtureWithBackpressure,
    SendBgpUpdateMessagesTest_PendingEoR) {
  SetUpAdjRibStateForUnit(true /* eorPending */, false /* eorSent */);
  UpdateAttrToPrefixMap(nullptr, {kV4Prefix1});
  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop1), {kV4Prefix2});
  FillQueueToSize(highWm_);

  // Start event base loop
  std::thread evbThread([this]() { evb_.loopForever(); });

  auto& messages = subscribeToLogMessages("");
  /* Wait for queue space to free up while queue is draining. */
  auto [_, drained] = co_await folly::coro::collectAll(
      co_withExecutor(
          &evb_, adjRib_->sendBgpUpdates(false /* tryPullNewChangeItems */)),
      co_withExecutor(&evb_, DrainQueueToLowWm()));

  evb_.terminateLoopSoon();
  evbThread.join();

  EXPECT_TRUE(drained);
  EXPECT_TRUE(adjRib_->egressEoRsSent_);
  auto eorIt =
      std::find_if(messages.begin(), messages.end(), [](const auto& entry) {
        return entry.first.getMessage().starts_with("Sending EoR to peer");
      });
  EXPECT_NE(eorIt, messages.end());
  auto bpIt =
      std::find_if(messages.begin(), messages.end(), [](const auto& entry) {
        return entry.first.getMessage().ends_with("Backpressured = true");
      });
  EXPECT_NE(bpIt, messages.end());

  /* Last three messages should be withdrawal, announcement, and EoR */
  while (adjRib_->adjRibOutQueue_->size() > 3) {
    co_await facebook::bgp::test::boundedPop(
        *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");
  }
  EXPECT_EQ(3, adjRib_->adjRibOutQueue_->size());
  auto msg1 = co_await facebook::bgp::test::boundedPop(
      *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");
  auto msg2 = co_await facebook::bgp::test::boundedPop(
      *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");
  auto msg3 = co_await facebook::bgp::test::boundedPop(
      *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");

  auto announcement = std::get<std::shared_ptr<const BgpUpdate2>>(*msg1);
  EXPECT_EQ(
      network::toIPPrefix(kV4Prefix2),
      announcement->mpAnnounced()->prefixes()->front().prefix());

  auto withdrawal = std::get<std::shared_ptr<const BgpUpdate2>>(*msg2);
  EXPECT_EQ(
      network::toIPPrefix(kV4Prefix1),
      withdrawal->v4Withdrawn2()->front().prefix());

  EXPECT_TRUE(std::holds_alternative<BgpEndOfRib>(*msg3));
}

TEST_F(SendBgpMessagesFixture, CancelPackingTimersTest) {
  SetUpAdjRibStateForUnit(true /* eorPending */, true /* eorSent */);
  adjRib_->sendCoroScheduled_ = true;

  auto& messages = subscribeToLogMessages("", folly::LogLevel::DBG2);
  // Case 1: Timers not initialized -> no-op
  {
    adjRib_->cancelPackingTimers();

    EXPECT_TRUE(messages.empty());
    EXPECT_FALSE(adjRib_->outDelayTimer_);
    EXPECT_FALSE(adjRib_->changeListConsumeTimer_);
  }

  // Case 2: Already scheduled
  {
    MockChangeListConsumer();
    adjRib_->activateChangeListConsumer();
    MockOutDelayPrefixes({kV4Prefix1, kV6Prefix1});
    adjRib_->scheduleOutDelayTimer();

    // Check that the two timers have been scheduled.
    EXPECT_TRUE(adjRib_->outDelayTimer_->isScheduled());
    EXPECT_TRUE(adjRib_->changeListConsumeTimer_->isScheduled());

    messages.clear();
    adjRib_->cancelPackingTimers();

    EXPECT_FALSE(adjRib_->outDelayTimer_->isScheduled());
    EXPECT_FALSE(adjRib_->changeListConsumeTimer_->isScheduled());

    EXPECT_EQ(2, messages.size());
    EXPECT_TRUE(messages.front().first.getMessage().starts_with(
        "Canceled changeListConsumeTimer_ timeout for "));
    EXPECT_TRUE(messages.back().first.getMessage().starts_with(
        "Canceled outDelayTimer_ timeout for "));
  }

  // Case 3: Timers initialized but already canceled.
  {
    EXPECT_FALSE(adjRib_->outDelayTimer_->isScheduled());
    EXPECT_FALSE(adjRib_->changeListConsumeTimer_->isScheduled());

    messages.clear();
    adjRib_->cancelPackingTimers();

    EXPECT_TRUE(messages.empty());
    EXPECT_FALSE(adjRib_->outDelayTimer_->isScheduled());
    EXPECT_FALSE(adjRib_->changeListConsumeTimer_->isScheduled());
  }
}

TEST_F(SendBgpMessagesFixture, ReschedulePackingTimersTest) {
  SetUpAdjRibStateForUnit(true /* eorPending */, true /* eorSent */);
  adjRib_->sendCoroScheduled_ = true;

  auto& messages = subscribeToLogMessages("", folly::LogLevel::DBG2);
  // Case 1: Timers not initialized -> no-op
  {
    adjRib_->reschedulePackingTimers();

    EXPECT_TRUE(messages.empty());
    EXPECT_FALSE(adjRib_->outDelayTimer_);
    EXPECT_FALSE(adjRib_->changeListConsumeTimer_);
  }

  // Case 2: Already scheduled
  {
    MockChangeListConsumer();
    adjRib_->activateChangeListConsumer();

    MockOutDelayPrefixes({kV4Prefix1, kV6Prefix1});
    adjRib_->scheduleOutDelayTimer();

    // Check that the two timers have been scheduled.
    EXPECT_TRUE(adjRib_->outDelayTimer_->isScheduled());
    EXPECT_TRUE(adjRib_->changeListConsumeTimer_->isScheduled());

    messages.clear();
    adjRib_->reschedulePackingTimers();

    EXPECT_TRUE(adjRib_->outDelayTimer_->isScheduled());
    EXPECT_TRUE(adjRib_->changeListConsumeTimer_->isScheduled());

    EXPECT_EQ(0, messages.size());
  }

  adjRib_->cancelPackingTimers();

  // Case 3: Timers initialized and ready for rescheduling.
  {
    EXPECT_FALSE(adjRib_->outDelayTimer_->isScheduled());
    EXPECT_FALSE(adjRib_->changeListConsumeTimer_->isScheduled());

    messages.clear();
    adjRib_->reschedulePackingTimers();

    EXPECT_TRUE(adjRib_->outDelayTimer_->isScheduled());
    EXPECT_TRUE(adjRib_->changeListConsumeTimer_->isScheduled());

    EXPECT_EQ(2, messages.size());
    EXPECT_TRUE(messages.front().first.getMessage().starts_with(
        "Rescheduled changeListConsumeTimer_"));
    EXPECT_TRUE(messages.back().first.getMessage().starts_with(
        "Rescheduled outDelayTimer_"));
  }

  adjRib_->cancelPackingTimers();

  // Case 4: Out-delay PQ is empty; don't need to reschedule out-delay.
  {
    EXPECT_FALSE(adjRib_->outDelayTimer_->isScheduled());
    EXPECT_FALSE(adjRib_->changeListConsumeTimer_->isScheduled());

    adjRib_->outDelayPQ_ = {};

    messages.clear();
    adjRib_->reschedulePackingTimers();

    EXPECT_FALSE(adjRib_->outDelayTimer_->isScheduled());
    EXPECT_TRUE(adjRib_->changeListConsumeTimer_->isScheduled());

    EXPECT_EQ(1, messages.size());
    EXPECT_TRUE(messages.front().first.getMessage().starts_with(
        "Rescheduled changeListConsumeTimer_"));
  }
}

TEST_F(SendBgpMessagesFixture, ActivateChangeListConsumerBeforeEoR) {
  SetUpAdjRibStateForUnit(true /* eorPending */, false /* eorSent */);
  MockChangeListConsumer();
  adjRib_->activateChangeListConsumer();
  EXPECT_FALSE(adjRib_->changeListConsumeTimer_->isScheduled());
  evb_.loopOnce();
}

TEST_F(SendBgpMessagesFixture, ActivateChangeListConsumerAfterEoR) {
  SetUpAdjRibStateForUnit(true /* eorPending */, true /* eorSent */);
  MockChangeListConsumer();
  adjRib_->activateChangeListConsumer();
  EXPECT_TRUE(adjRib_->changeListConsumeTimer_->isScheduled());
  evb_.loopOnce();
}

TEST_F(SendBgpMessagesFixture, NotifyConsumeChangeListUponEoRSent) {
  SetUpAdjRibStateForUnit(true /* eorPending */, false /* eorSent */);
  MockChangeListConsumer();
  adjRib_->activateChangeListConsumer();

  EXPECT_FALSE(adjRib_->changeListConsumeTimer_->isScheduled());

  /*
   * Let the scheduled sendBgpUpdates coro run; it should reschedule
   * the timer.
   */
  evb_.loopOnce();

  EXPECT_TRUE(adjRib_->changeListConsumeTimer_->isScheduled());
}

TEST_F(SendBgpMessagesFixture, NotifyConsumeChangeListAfterEoR) {
  SetUpAdjRibStateForUnit(true /* eorPending */, true /* eorSent */);
  MockChangeListConsumer();
  adjRib_->activateChangeListConsumer();
  EXPECT_TRUE(adjRib_->changeListConsumeTimer_->isScheduled());

  UpdateAttrToPrefixMap(nullptr, {kV4Prefix1});
  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop1), {kV4Prefix2});

  /*
   * Let the scheduled sendBgpUpdates coro run; it should reschedule
   * the timer.
   */
  evb_.loopOnce();

  EXPECT_TRUE(adjRib_->changeListConsumeTimer_->isScheduled());
}

TEST_F(SendBgpMessagesFixture, PackPrefixesWithLimitTest) {
  SetUpAdjRibStateForUnit(true /* eorPending */, true /* eorSent */);
  PrefixSet pfxSetV4;
  PrefixSet pfxSetV6;
  int kPrefixes = 10000;

  for (int i = 0; i < kPrefixes; ++i) {
    pfxSetV4.emplace(kV4Prefix1Slash25, i);
    pfxSetV6.emplace(kV6Prefix1, i);
  }

  std::vector<RiggedIPPrefix> container;
  // v4 overflow (announcements)
  container.clear();
  {
    adjRib_->sendAddPath_ = false;
    int packed = adjRib_->packPrefixesWithLimit(
        kApproxSerializedAttrLen, pfxSetV4, container);
    /*
     * With new formula: (4077 - 300) * 10 / 5 = 7554
     * Since we have 10000 prefixes, only 7554 should be packed (overflow)
     */
    EXPECT_EQ(7554, packed);

    EXPECT_EQ(kPrefixes - packed, pfxSetV4.size());
    EXPECT_EQ(packed, container.size());

    for (int i = 0; i < packed; ++i) {
      EXPECT_EQ(network::toIPPrefix(kV4Prefix1Slash25), container[i].prefix());
      EXPECT_FALSE(container[i].pathId().has_value());
    }
  }
  // v4 overflow (withdrawals)
  container.clear();
  {
    adjRib_->sendAddPath_ = false;
    int packed = adjRib_->packPrefixesWithLimit(0, pfxSetV4, container);
    /*
     * With new formula: (4077 - 0) * 10 / 5 = 8154
     * Since we have (10000 - 7554 = 2446) prefixes remaining, all should be
     * packed (no overflow) Let's reset to 10000 for this test
     */
    pfxSetV4.clear();
    for (int i = 0; i < kPrefixes; ++i) {
      pfxSetV4.emplace(kV4Prefix1Slash25, i);
    }
    container.clear();

    packed = adjRib_->packPrefixesWithLimit(0, pfxSetV4, container);
    /*
     * With new formula: (4077 - 0) * 10 / 5 = 8154
     * Since we have 10000 prefixes, only 8154 should be packed (overflow)
     */
    EXPECT_EQ(8154, packed);

    EXPECT_EQ(kPrefixes - packed, pfxSetV4.size());
    EXPECT_EQ(packed, container.size());

    for (int i = 0; i < packed; ++i) {
      EXPECT_EQ(network::toIPPrefix(kV4Prefix1Slash25), container[i].prefix());
      EXPECT_FALSE(container[i].pathId().has_value());
    }
  }
  // v6 overflow (withdrawals)
  container.clear();
  {
    adjRib_->sendAddPath_ = true;
    int packed = adjRib_->packPrefixesWithLimit(0, pfxSetV6, container);
    /*
     * With new formula: (4077 - 0) * 10 / 21 = 1941
     * Since we have 10000 prefixes, only 1941 should be packed (overflow)
     */
    EXPECT_EQ(1941, packed);

    EXPECT_EQ(kPrefixes - packed, pfxSetV6.size());
    EXPECT_EQ(packed, container.size());

    for (int i = 0; i < packed; ++i) {
      EXPECT_EQ(network::toIPPrefix(kV6Prefix1), container[i].prefix());
      EXPECT_TRUE(container[i].pathId().has_value());
    }
  }
  // No overflow case
  container.clear();
  pfxSetV6.clear();
  {
    pfxSetV6.emplace(kV6Prefix1, 1);
    pfxSetV6.emplace(kV6Prefix1, 2);
    pfxSetV6.emplace(kV6Prefix1, 3);

    adjRib_->sendAddPath_ = false;
    int packed = adjRib_->packPrefixesWithLimit(
        kApproxSerializedAttrLen, pfxSetV6, container);
    EXPECT_EQ(3, packed);

    EXPECT_TRUE(pfxSetV6.empty());
    EXPECT_EQ(packed, container.size());

    EXPECT_EQ(network::toIPPrefix(kV6Prefix1), *container[0].prefix());
    EXPECT_EQ(network::toIPPrefix(kV6Prefix1), *container[1].prefix());
    EXPECT_EQ(network::toIPPrefix(kV6Prefix1), *container[2].prefix());
  }
}

TEST_F(SendBgpMessagesFixture, BuildUpdateWithSizeEstimationTest) {
  SetUpAdjRibStateForUnit(true /* eorPending */, true /* eorSent */);
  auto attr = GetBgpPath(kV4Nexthop1);
  UpdateAttrToPrefixMap(attr, {kV4Prefix1, kV6Prefix1});
  UpdateAttrToPrefixMap(nullptr, {kV4Prefix2, kV6Prefix2});
  EXPECT_EQ(4, adjRib_->attrToPrefixMap_.size());

  // v4 update
  {
    auto v4AnnounceKey = BgpPathWithAfi{attr, BgpUpdateAfi::AFI_IPv4};
    auto it = adjRib_->attrToPrefixMap_.find(v4AnnounceKey);
    auto v4Announce =
        adjRib_->buildUpdateWithSizeEstimation(it->first, it->second);

    EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, v4Announce->mpAnnounced()->afi());
    EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, v4Announce->mpAnnounced()->safi());

    EXPECT_EQ(1, v4Announce->mpAnnounced()->prefixes()->size());
    EXPECT_EQ(
        network::toIPPrefix(kV4Prefix1),
        v4Announce->mpAnnounced()->prefixes()->front().prefix());

    EXPECT_TRUE(it->second.empty());
  }
  // v4 withdraw
  {
    auto v4WithdrawKey = BgpPathWithAfi{nullptr, BgpUpdateAfi::AFI_IPv4};
    auto it = adjRib_->attrToPrefixMap_.find(v4WithdrawKey);
    auto v4Withdraw =
        adjRib_->buildUpdateWithSizeEstimation(it->first, it->second);

    EXPECT_EQ(1, v4Withdraw->v4Withdrawn2()->size());
    EXPECT_EQ(
        network::toIPPrefix(kV4Prefix2),
        v4Withdraw->v4Withdrawn2()->front().prefix());

    EXPECT_TRUE(it->second.empty());
  }
  // v6 update
  {
    auto v6AnnounceKey = BgpPathWithAfi{attr, BgpUpdateAfi::AFI_IPv6};
    auto it = adjRib_->attrToPrefixMap_.find(v6AnnounceKey);
    auto v6Announce =
        adjRib_->buildUpdateWithSizeEstimation(it->first, it->second);

    EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, v6Announce->mpAnnounced()->afi());
    EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, v6Announce->mpAnnounced()->safi());

    EXPECT_EQ(1, v6Announce->mpAnnounced()->prefixes()->size());
    EXPECT_EQ(
        network::toIPPrefix(kV6Prefix1),
        v6Announce->mpAnnounced()->prefixes()->front().prefix());

    EXPECT_TRUE(it->second.empty());
  }
  // v6 withdraw
  {
    auto v6WithdrawKey = BgpPathWithAfi{nullptr, BgpUpdateAfi::AFI_IPv6};
    auto it = adjRib_->attrToPrefixMap_.find(v6WithdrawKey);
    auto v6Withdraw =
        adjRib_->buildUpdateWithSizeEstimation(it->first, it->second);

    EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, v6Withdraw->mpWithdrawn()->afi());
    EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, v6Withdraw->mpWithdrawn()->safi());

    EXPECT_EQ(1, v6Withdraw->mpWithdrawn()->prefixes()->size());
    EXPECT_EQ(
        network::toIPPrefix(kV6Prefix2),
        v6Withdraw->mpWithdrawn()->prefixes()->front().prefix());

    EXPECT_TRUE(it->second.empty());
  }
}

/**
 * This test verifies the kEgressTransientUpdatesSuppressed counter
 * is updated when transient route updates are suppressed.
 *
 * This is possible because sendBgpMessages is invoked at a different
 * cadence from processRibMessage.
 */
TEST_F(SendBgpMessagesFixture, NumTransientUpdatesSuppressedStatsTest) {
  SetUpAdjRibStateForUnit(
      false /* eorSeen */,
      true /* eorSent */,
      true /* v4Afi */,
      true /* v6Afi */);

  BgpStats::initCounters();
  auto counters = facebook::fb303::ThreadCachedServiceData::getShared();

  auto attrs1 = std::make_shared<BgpPath>(*buildBgpPathFields(0, 1, 1, 1));
  auto attrs2 = std::make_shared<BgpPath>(*buildBgpPathFields(0, 2, 2, 2));

  auto initialRoutes = buildAnnouncementFromMap(
      {{attrs1, {kV4Prefix1, kV4Prefix3}}, {attrs2, {kV4Prefix2, kV4Prefix4}}});

  adjRib_->processRibMessage(initialRoutes);

  /*
   * Every prefix was announced for the first time, so no transient
   * updates have been made yet.
   */
  EXPECT_EQ(
      0,
      counters->getCounter(
          BgpStats::kEgressTransientUpdatesSuppressed + ".count"));
  EXPECT_EQ(
      0,
      counters->getCounter(
          BgpStats::kEgressTransientUpdatesSuppressed + ".count.60"));
  EXPECT_EQ(0, adjRib_->stats_.getTransientRouteUpdatesSuppressed());

  /*
   * Now we start flapping the routes by changing the advertisement of
   * all prefixes. This should cause the counter to increment by 4
   * due to each prefix changing.
   */
  auto routeFlap0 = buildAnnouncementFromMap(
      {{attrs2, {kV4Prefix1, kV4Prefix3}}, {attrs1, {kV4Prefix2, kV4Prefix4}}});
  adjRib_->processRibMessage(routeFlap0);
  facebook::fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_EQ(
      4,
      counters->getCounter(
          BgpStats::kEgressTransientUpdatesSuppressed + ".count"));
  EXPECT_EQ(
      4,
      counters->getCounter(
          BgpStats::kEgressTransientUpdatesSuppressed + ".count.60"));
  EXPECT_EQ(4, adjRib_->stats_.getTransientRouteUpdatesSuppressed());

  /*
   * Now set 2 of the prefixes to be announced with their original
   * routes again. This causes the counter to increment by another 2.
   */
  auto routeFlap1 = buildAnnouncementFromMap(
      {{attrs2, {kV4Prefix2}}, {attrs1, {kV4Prefix1}}});
  adjRib_->processRibMessage(routeFlap1);
  facebook::fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_EQ(
      6,
      counters->getCounter(
          BgpStats::kEgressTransientUpdatesSuppressed + ".count"));
  EXPECT_EQ(
      6,
      counters->getCounter(
          BgpStats::kEgressTransientUpdatesSuppressed + ".count.60"));
  EXPECT_EQ(6, adjRib_->stats_.getTransientRouteUpdatesSuppressed());

  /* Finally withdraw kV4Prefix3 entirely. */
  adjRib_->processRibMessage(createRibSingleWithdrawal(kV4Prefix3));
  facebook::fb303::ThreadCachedServiceData::get()->publishStats();
  /* This should only register as one suppression event. */
  EXPECT_EQ(
      7,
      counters->getCounter(
          BgpStats::kEgressTransientUpdatesSuppressed + ".count"));
  EXPECT_EQ(
      7,
      counters->getCounter(
          BgpStats::kEgressTransientUpdatesSuppressed + ".count.60"));

  EXPECT_EQ(7, adjRib_->stats_.getTransientRouteUpdatesSuppressed());
}

/**
 * Test that verifies the close() API is called correctly during session
 * termination when the queue is in a blocked state.
 *
 * This test:
 * 1. Sets up adjRib with sessionEstablished to start message processing loops
 * 2. Forces the queue into a blocked state by filling it to high watermark
 * 3. Verifies the queue is blocked using isBlocked()
 * 4. Verifies asyncScope has exactly 1 task running (no sendBgpUpdates)
 * 5. Calls sessionTerminated() which should call close() on the queue
 * 6. Verifies asyncScope still has 1 task (not yet completed)
 * 7. Runs event loop to let cancelled tasks complete
 * 8. Verifies all tasks have exited (asyncScope has 0 remaining tasks)
 * 9. Verifies the queue is closed but not reset to nullptr
 */
CO_TEST_F(SendBgpMessagesFixtureWithBackpressure, QueueCloseTest) {
  // Set up adjRib with sessionEstablished to start message processing loops
  setupAdjRib(
      kLocalAs2,
      kLocalAs2,
      kRemoteAs1,
      kIsRrClientFalse,
      kIsConfedPeerFalse,
      NextHopSelfConfigured(false),
      kV4Nexthop1,
      kV6Nexthop1,
      true /* sessionEstablish */);

  // Run event loop to let the session establishment fiber task complete
  evb_.loopOnce();

  // Set up EoR sent adjRib state
  adjRib_->setEgressEoRsPending(false, false);
  adjRib_->egressEoRsSent_ = true;

  // Save a copy of the queue pointer
  auto queuePtr = adjRib_->adjRibOutQueue_;

  // Fill the queue to high watermark to force it into blocked state
  FillQueueToSize(highWm_);

  // Verify the queue is blocked
  EXPECT_TRUE(queuePtr->isBlocked());
  EXPECT_TRUE(adjRib_->adjRibOutQueue_->isBlocked());

  /*
   * Verify that we have exactly 1 task running on asyncScope:
   * processPeerMessageLoop.
   * The sendBgpUpdates task should not be running.
   */
  EXPECT_EQ(1, adjRib_->asyncScope_->remaining());

  // Call sessionTerminated which should close the queue and reset the pointer
  co_await adjRib_->sessionTerminated(FiberBgpPeer::BgpSessionStop{});

  /*
   * After sessionTerminated, asyncScope_->requestCancellation() has been
   * called. The 1 task should still be running until we run the event loop.
   */
  EXPECT_EQ(1, adjRib_->asyncScope_->remaining());

  // Run the event loop to let the cancelled tasks complete
  evb_.loopOnce();

  /*
   * The queue is closed but not reset to nullptr (a new queue will be provided
   * when a new session is established)
   */
  EXPECT_NE(nullptr, adjRib_->adjRibOutQueue_);
  EXPECT_TRUE(adjRib_->adjRibOutQueue_->isClosed());

  // The saved queue pointer should still be valid (shared_ptr) and closed
  EXPECT_TRUE(queuePtr->isBlocked());
  EXPECT_TRUE(queuePtr->isClosed());
}

/**
 * Test that verifies the queue behavior when session is terminated while
 * the queue is blocked waiting for space.
 *
 * This test used to crash before D91490974.
 *
 * 1. Sets up adjRib and fills the queue to the high watermark
 * 2. Sends one additional message to force it into waiting for queue space
 * 3. Releases all external references to the queue (fixture)
 * 4. Calls sessionTerminated() which closes the queue
 * 5. Verifies that the adjRibOutQueue_ on the AdjRib is closed
 */
CO_TEST_F(
    SendBgpMessagesFixtureWithBackpressure,
    SessionTerminatedQueueCloseWhileProducerWaiting) {
  // Set up adjRib with full session establishment
  setupAdjRib(
      kLocalAs2,
      kLocalAs2,
      kRemoteAs1,
      kIsRrClientFalse,
      kIsConfedPeerFalse,
      NextHopSelfConfigured(false),
      kV4Nexthop1,
      kV6Nexthop1,
      true /* sessionEstablish */);

  // Run event loop to let the session establishment fiber task complete
  evb_.loopOnce();

  // Set up EoR sent state (normal operating state)
  adjRib_->setEgressEoRsPending(false, false);
  adjRib_->egressEoRsSent_ = true;

  LOG(INFO) << "Initial queue ref count: "
            << adjRib_->adjRibOutQueue_.use_count();

  // Step 1: Fill the queue to the high watermark to force it into blocked state
  FillQueueToSize(highWm_);

  // Verify the queue is blocked
  EXPECT_TRUE(adjRib_->adjRibOutQueue_->isBlocked());

  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop1), {kV4Prefix1});
  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop2), {kV4Prefix2});

  // Start the event base loop in a separate thread
  std::thread evbThread([this]() { evb_.loopForever(); });

  /*
   * Step 2: Schedule sendBgpUpdates which will call waitForQueueSpace() and
   * block on waitToPush() because the queue is full (forcing it into waiting)
   */
  std::atomic<bool> sendBgpUpdatesStarted{false};
  auto sendTask = [&]() -> folly::coro::Task<void> {
    sendBgpUpdatesStarted = true;
    co_await adjRib_->sendBgpUpdates(true /* tryPullNewChangeItems */);
    co_return;
  };

  adjRib_->asyncScope_->add(co_withExecutor(&evb_, sendTask()));

  // Wait a bit for the coroutine to start and block on waitToPush
  co_await folly::coro::co_withExecutor(
      &evb_,
      facebook::bgp::test::boundedPop(
          *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_"));

  // Verify the task has started (should be blocked waiting for queue space)
  EXPECT_TRUE(sendBgpUpdatesStarted.load());

  LOG(INFO) << "Before releasing fixture queue ref count: "
            << adjRib_->adjRibOutQueue_.use_count();

  /*
   * Step 3: Release the fixture's ownership of the queue
   * After this, only the AdjRib holds a reference (ref count = 1)
   */
  boundedAdjRibOutQ_.reset();

  LOG(INFO) << "After releasing fixture queue ref count: "
            << adjRib_->adjRibOutQueue_.use_count();

  /*
   * Step 4: Call sessionTerminated() while the queue is still active
   * This should:
   * 1. Close the queue (wake up waiters)
   * 2. Request cancellation on asyncScope
   *
   * If there is some kind of timing issue between close() and queue
   * destruction, we would have seen the mutex assertion and crash in ASAN.
   */
  co_await folly::coro::co_withExecutor(
      &evb_, adjRib_->sessionTerminated(FiberBgpPeer::BgpSessionStop{}));

  LOG(INFO) << "After sessionTerminated - queue destroyed";

  /*
   * Step 5: Verify that the adjRibOutQueue_ on the AdjRib is closed
   * (not nullptr - a new queue will be provided when a new session is
   * established)
   */
  EXPECT_NE(nullptr, adjRib_->adjRibOutQueue_);
  EXPECT_TRUE(adjRib_->adjRibOutQueue_->isClosed());

  // Run the event loop briefly to let cancelled tasks exit
  /* sleep override */
  evb_.runInEventBaseThreadAndWait([]() {});

  // Stop the event loop
  evb_.terminateLoopSoon();
  evbThread.join();
}

/**
 * Test that verifies no EoRs are sent when session is terminated during
 * an initial dump.
 *
 * This test:
 * 1. Sets up adjRib in initial dump state (egressEoRsPending_ = true)
 * 2. Schedules sendBgpUpdates on asyncScope which will trigger sendPendingEoRs
 * 3. Waits for sessionTerminated to finish
 * 4. Verifies after popping all items from the queue that there were no EoRs
 */
CO_TEST_F(
    SendBgpMessagesFixtureWithBackpressure,
    SessionTerminatedWhileQueueBlockedTerminatesSender) {
  /* Set up adjRib state for unit testing with controlled queue capacity */
  SetUpAdjRibStateForUnit(
      true /* eorPending */,
      false /* eorSent */,
      true /* enableIPv4 */,
      true /* enableIPv6 */);

  /* Set up changeListConsumeTimer_ as non-nullptr */
  MockChangeListConsumer();
  adjRib_->activateChangeListConsumer();
  EXPECT_TRUE(adjRib_->changeListConsumeTimer_);

  /* Set up outDelayTimer_ and populate outDelayPQ_ */
  MockOutDelayPrefixes({kV4Prefix1, kV6Prefix1});
  adjRib_->scheduleOutDelayTimer();
  EXPECT_TRUE(adjRib_->outDelayTimer_);
  EXPECT_TRUE(adjRib_->outDelayTimer_->isScheduled());

  /* Fill the queue to high watermark so it is blocked */
  FillQueueToSize(highWm_);

  /* Verify the queue is blocked */
  EXPECT_TRUE(adjRib_->adjRibOutQueue_->isBlocked());

  /**
   * Add an update to the packing list so sendBgpUpdates will need to call
   * waitForQueueSpace for route updates
   */
  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop1), {kV4Prefix1});

  /* Subscribe to log messages at DBG2 to capture reschedule logs */
  auto& messages = subscribeToLogMessages("", folly::LogLevel::DBG2);

  /* Start the event base loop in a separate thread */
  std::thread evbThread([this]() { evb_.loopForever(); });

  /**
   * Schedule sendBgpUpdates which will trigger sendPendingEoRs since we're
   * in initial dump state (egressEoRsPending_ = true)
   */
  folly::coro::Baton taskStartedBaton;
  auto sendTask = [&]() -> folly::coro::Task<void> {
    taskStartedBaton.post();
    co_await adjRib_->sendBgpUpdates(true /* tryPullNewChangeItems */);
  };

  /* Record the initial value of egressQueueBlocks stat */
  auto initialEgressQueueBlocks = adjRib_->stats_.egressQueueBlocks;

  adjRib_->asyncScope_->add(co_withExecutor(&evb_, sendTask()));

  /* Wait for the task to start - it will block on waitForQueueSpace */
  co_await taskStartedBaton;

  /* Pop one item to allow sendBgpUpdates to proceed and block on waitToPush */
  co_await folly::coro::co_withExecutor(
      &evb_,
      facebook::bgp::test::boundedPop(
          *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_"));

  /**
   * Verify that egressQueueBlocks was incremented, confirming we hit
   * waitForQueueSpace
   */
  EXPECT_GT(adjRib_->stats_.egressQueueBlocks, initialEgressQueueBlocks)
      << "sendBgpUpdates should have entered waitForQueueSpace and blocked";

  /**
   * Verify that there are tasks waiting on asyncScope before sessionTerminated
   * We expect at least 1 task: the sendBgpUpdates task we just scheduled
   */
  EXPECT_GE(adjRib_->asyncScope_->remaining(), 1);

  /**
   * Call sessionTerminated which should:
   * 1. Close the queue (interrupting sendPendingEoRs)
   * 2. Request cancellation on asyncScope
   */
  co_await folly::coro::co_withExecutor(
      &evb_, adjRib_->sessionTerminated(FiberBgpPeer::BgpSessionStop{}));

  /* Run the event loop briefly to let cancelled tasks exit */
  evb_.runInEventBaseThreadAndWait([]() {});

  /* Verify that all tasks on asyncScope have completed */
  EXPECT_EQ(0, adjRib_->asyncScope_->remaining());

  /**
   * Verify no "Sending accumulated changes" log was emitted
   * This log would indicate that sendBgpUpdates completed and sent EoRs
   */
  for (const auto& [logMsg, _] : messages) {
    EXPECT_FALSE(
        logMsg.getMessage().starts_with("Sending accumulated changes"));
    EXPECT_FALSE(
        logMsg.getMessage().starts_with("Rescheduled changeListConsumeTimer_"));
    EXPECT_FALSE(logMsg.getMessage().starts_with("Rescheduled outDelayTimer_"));
  }

  /* Pop all items from the queue and verify there are no EoRs */
  while (!adjRib_->adjRibOutQueue_->empty()) {
    auto msg = co_await folly::coro::co_withExecutor(
        &evb_,
        facebook::bgp::test::boundedPop(
            *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_"));
    if (msg) {
      /**
       * Verify that no EoRs were sent because sessionTerminated interrupted
       * the sendPendingEoRs operation
       */
      EXPECT_FALSE(std::holds_alternative<BgpEndOfRib>(*msg));
    }
  }

  /* Verify the queue is closed */
  EXPECT_TRUE(adjRib_->adjRibOutQueue_->isClosed());

  /**
   * Verify changeListConsumeTimer_ was reset to nullptr during
   * sessionTerminated (deactivateChangeListConsumer resets the timer)
   */
  EXPECT_FALSE(adjRib_->changeListConsumeTimer_);

  /* Verify outDelayTimer_ was reset to nullptr during sessionTerminated */
  EXPECT_FALSE(adjRib_->outDelayTimer_);

  /* Stop the event loop */
  evb_.terminateLoopSoon();
  evbThread.join();
}

TEST_F(SendBgpMessagesFixture, ProcessRibAnnouncedEntry_PropagatesRibVersion) {
  SetUpAdjRibStateForUnit(
      /* eorPending */ false, /* eorSent */ true);

  auto attrs = GetBgpPath(kV4Nexthop1);
  RibOutAnnouncementEntry entry(kV4Prefix1, kDefaultPathID, eBgpPeer_, attrs);
  entry.ribVersion = 55;
  adjRib_->processRibAnnouncedEntry(entry);

  auto* adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1);
  ASSERT_NE(adjRibEntry, nullptr);
  EXPECT_EQ(adjRibEntry->getRibVersion(), 55);

  entry.ribVersion = 100;
  adjRib_->processRibAnnouncedEntry(entry);

  adjRibEntry = adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1);
  ASSERT_NE(adjRibEntry, nullptr);
  EXPECT_EQ(adjRibEntry->getRibVersion(), 100);
}
/**
 * Test that sendBgpUpdates with force=true drains the full
 * packing list even under backpressure after EoR.
 *
 * With force=false (the AfterEoR test above), backpressure
 * causes an early break after sending one message. With force=true, the loop
 * should continue draining after queue space frees up, sending ALL messages.
 */
CO_TEST_F(
    SendBgpMessagesFixtureWithBackpressure,
    SendBgpUpdatesNoChangelistDrainsFull) {
  SetUpAdjRibStateForUnit(false /* eorPending */, true /* eorSent */);
  FillQueueToSize(highWm_ - 1);

  UpdateAttrToPrefixMap(nullptr, {kV4Prefix1});
  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop1), {kV4Prefix2});

  // Start event base loop
  std::thread evbThread([this]() { evb_.loopForever(); });

  auto& messages = subscribeToLogMessages("");
  /* Wait for queue space to free up while queue is draining. */
  auto [_, drained] = co_await folly::coro::collectAll(
      co_withExecutor(
          &evb_, adjRib_->sendBgpUpdates(false /* tryPullNewChangeItems */)),
      co_withExecutor(&evb_, DrainQueueToLowWm()));

  evb_.terminateLoopSoon();
  evbThread.join();

  EXPECT_TRUE(drained);
  EXPECT_TRUE(messages[0].first.getMessage().ends_with("Backpressured = true"));

  /*
   * Unlike force=false (which breaks after 1 message),
   * force=true should drain the full packing list.
   * Both the withdrawal and announcement should have been sent.
   */
  EXPECT_TRUE(adjRib_->attrToPrefixMap_.empty());

  /* Drain padding and collect real messages. */
  std::vector<std::shared_ptr<const BgpUpdate2>> realMessages;
  while (!adjRib_->adjRibOutQueue_->empty()) {
    auto msg = co_await facebook::bgp::test::boundedPop(
        *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");
    auto update = std::get<std::shared_ptr<const BgpUpdate2>>(*msg);
    if (update) {
      realMessages.push_back(update);
    }
  }

  /* Both the withdrawal and announcement should have been sent. */
  EXPECT_EQ(2, realMessages.size());
}

CO_TEST_F(
    SendBgpMessagesFixtureWithBackpressure,
    ActiveSenderRetriesFullDrainViaPackingTimer) {
  SetUpAdjRibStateForUnit(false /* eorPending */, true /* eorSent */);
  UpdateAttrToPrefixMap(nullptr, {kV4Prefix1});
  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop1), {kV4Prefix2});

  /*
   * A normal sender exits after encountering backpressure. Its tail rearms the
   * packing timer, which retries the full drain that collided with the sender.
   */
  FillQueueToSize(highWm_);
  adjRib_->changeListConsumeTimer_ =
      folly::AsyncTimeout::make(evb_, [this]() noexcept {
        adjRib_->scheduleSendBgpUpdates(false /* tryPullNewChangeItems */);
      });
  adjRib_->mraiInterval = 0;
  adjRib_->scheduleSendBgpUpdates(true /* tryPullNewChangeItems */);
  evb_.loopOnce();
  EXPECT_TRUE(adjRib_->sendCoroScheduled_);

  adjRib_->scheduleSendBgpUpdates(false /* tryPullNewChangeItems */);
  std::thread evbThread([this]() { evb_.loopForever(); });

  auto [packingListDrained, queueDrained] = co_await folly::coro::collectAll(
      co_withExecutor(&evb_, WaitForPackingListToDrain()),
      co_withExecutor(&evb_, DrainQueueToLowWm()));

  evb_.terminateLoopSoon();
  evbThread.join();

  EXPECT_TRUE(packingListDrained);
  EXPECT_TRUE(queueDrained);
  EXPECT_TRUE(adjRib_->attrToPrefixMap_.empty());

  size_t updateCount = 0;
  while (!adjRib_->adjRibOutQueue_->empty()) {
    auto message = co_await facebook::bgp::test::boundedPop(
        *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");
    if (message &&
        std::get<std::shared_ptr<const BgpUpdate2>>(*message) != nullptr) {
      ++updateCount;
    }
  }
  EXPECT_EQ(updateCount, 2);
}

/**
 * Test that activateDetachedModeProcessing drains the packing list
 * via sendBgpUpdates(false) before checking DFP/DSP.
 *
 * Sets up a peer with a non-empty PL, calls activateDetachedModeProcessing,
 * and verifies that the PL is fully drained and messages are in the queue.
 * No changeListConsumer_ is set up, so canWaitForGroupToRejoin() returns false
 * and the peer stays DETACHED_RUNNING (DSP path).
 */
TEST_F(
    SendBgpMessagesFixtureWithBackpressure,
    ActivateDetachedModeProcessingDrainsPL) {
  SetUpAdjRibStateForUnit(false /* eorPending */, true /* eorSent */);

  // Set up an update group so DFP/DSP checks can run
  auto group = std::make_shared<AdjRibOutGroup>(
      evb_,
      fboss::kDefaultVrf,
      "test_group",
      1,
      true /* enableUpdateGroup */,
      UpdateGroupKey{});
  adjRib_->adjRibOutGroup_ = group;

  // Populate PL with entries to drain
  UpdateAttrToPrefixMap(nullptr, {kV4Prefix1});
  UpdateAttrToPrefixMap(GetBgpPath(kV4Nexthop1), {kV4Prefix2});
  EXPECT_EQ(2, adjRib_->attrToPrefixMap_.size());

  adjRib_->activateDetachedModeProcessing();

  // Pump the evb so sendBgpUpdates runs and drains the PL
  evb_.loopOnce();

  // PL should be fully drained
  EXPECT_TRUE(adjRib_->attrToPrefixMap_.empty());

  // Messages should be in the queue
  EXPECT_EQ(2, adjRib_->adjRibOutQueue_->size());

  // Clean up
  adjRib_->adjRibOutGroup_ = nullptr;
}

} // namespace facebook::bgp
