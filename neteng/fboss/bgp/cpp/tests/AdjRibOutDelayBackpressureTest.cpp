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

#include <folly/coro/Collect.h>
#include <folly/coro/GtestHelpers.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#define AdjRib_TEST_FRIENDS                                                 \
  friend class AdjRibOutDelayFixture;                                       \
  FRIEND_TEST(AdjRibOutDelayFixture, SimpleOutDelayTest);                   \
  FRIEND_TEST(AdjRibOutDelayFixture, WithdrawBeforeOutDelayTimerFiresTest); \
  FRIEND_TEST(AdjRibOutDelayFixture, RibInitialDumpHasNoOutDelayTest);      \
  FRIEND_TEST(AdjRibOutDelayFixture, OutDelayPrefixesInBackpressureTest);   \
  FRIEND_TEST(AdjRibOutDelayFixture, NumBackpressureEventsStatsTest);       \
  FRIEND_TEST(AdjRibOutDelayFixture, OutDelayTimerTest);

#define AdjRibStats_TEST_FRIENDS      \
  friend class AdjRibOutDelayFixture; \
  FRIEND_TEST(AdjRibOutDelayFixture, NumBackpressureEventsStatsTest);

#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/AdjRibOutUtils.h"
#include "neteng/fboss/bgp/cpp/tests/BoundedWaitUtils.h"

/**
 * Test suite for out delay in AdjRibOut with egress backpressure enabled.
 */
using namespace facebook::nettools::bgplib;

namespace facebook::bgp {

using namespace ::testing;

class AdjRibOutDelayFixture : public AdjRibOutboundFixture {
 public:
  void SetUpOutDelayAdjRib(
      const nettools::bgplib::BgpPeerId& peerId,
      std::chrono::seconds outDelay = 0s) {
    testOutDelayMs_ =
        std::chrono::duration_cast<std::chrono::milliseconds>(outDelay).count();

    setupAdjRib(peerId);
    /* Set IPv6 negotiated to false to ensure only one EoR. */
    adjRib_->isAfiIpv6Negotiated_ = false;
    /* Set out delay and other configurables. */
    adjRib_->outDelay_ = std::chrono::seconds(outDelay);
    /* Setting only ipv4 so that we only get one EoR. */
    adjRib_->isAfiIpv4Negotiated_ = true;
    adjRib_->pathIdGenerator_ = std::make_unique<PathIdGenerator>(false);

    /* Attach queues. */
    adjRib_->adjRibOutQueue_ = std::make_shared<AdjRib::AdjRibOutQueueT>(
        5 /* capacity */, 3 /* highWm */, 0 /* lowWm */);
  }

  /**
   * Build a RibOutAnnouncement that contains all of the following
   * announcement entries in @entries. Each path, prefix is one entry.
   */
  RibOutAnnouncement buildAnnouncementWithInstallTimestamp(
      const folly::F14NodeMap<
          std::shared_ptr<const BgpPath>,
          folly::F14NodeSet<folly::CIDRNetwork>>& ribEntries,
      bool initialDump = false,
      bool sendWithEoR = false,
      std::optional<std::chrono::time_point<std::chrono::system_clock>>
          timestamp = std::nullopt) {
    RibOutAnnouncement ribMsg;
    auto installTimeStamp =
        timestamp ? *timestamp : std::chrono::system_clock::now();
    for (auto& [attr, pfxs] : ribEntries) {
      for (auto& pfx : pfxs) {
        ribMsg.entries.emplace_back(pfx, kDefaultPathID, localPeerV4_, attr);
        ribMsg.entries.back().installTimeStamp = installTimeStamp;
      }
    }
    ribMsg.initialDump = initialDump;
    ribMsg.sendWithEoR = sendWithEoR;
    return ribMsg;
  }

  /**
   * Check @prefixes are contained in the update's mpAnnounced prefixes.
   */
  void verifyPrefixesInUpdates(
      const std::vector<std::optional<FiberBgpPeer::InputMessageT>>& updates,
      const folly::F14NodeSet<folly::CIDRNetwork>& expectedPrefixes) {
    folly::F14NodeSet<folly::CIDRNetwork> seenPrefixes;
    for (auto& varupdate : updates) {
      auto& update = std::get<std::shared_ptr<const BgpUpdate2>>(*varupdate);
      for (auto& pfx : *update->mpAnnounced()->prefixes()) {
        seenPrefixes.insert(network::toCIDRNetwork(*pfx.prefix()));
      }
    }
    EXPECT_EQ(expectedPrefixes, seenPrefixes);
  }

  std::chrono::steady_clock::time_point systemToSteady(
      std::chrono::system_clock::time_point tp) const {
    auto now_sys = std::chrono::system_clock::now();
    auto now_steady = std::chrono::steady_clock::now();
    return now_steady + (tp - now_sys);
  }

  void scheduleVerifyUnprocessedRelativeToOutDelay(
      const std::chrono::time_point<std::chrono::system_clock> installTimeStamp,
      const int deltaMs,
      const std::vector<folly::CIDRNetwork>& prefixes,
      AdjRib* adjRib) {
    auto runAtTime =
        installTimeStamp + std::chrono::milliseconds(testOutDelayMs_ + deltaMs);
    evb_.scheduleAt(
        [prefixes, adjRib] {
          for (auto& pfx : prefixes) {
            EXPECT_FALSE(adjRib->getRibEntry(/*ingress=*/false, pfx));
          }
        },
        systemToSteady(runAtTime));
  }

  void scheduleVerifyProcessedRelativeToOutDelay(
      const std::chrono::time_point<std::chrono::system_clock> installTimeStamp,
      const int deltaMs,
      const std::vector<folly::CIDRNetwork>& prefixes,
      AdjRib* adjRib) {
    auto runAtTime =
        installTimeStamp + std::chrono::milliseconds(testOutDelayMs_ + deltaMs);
    evb_.scheduleAt(
        [this, prefixes, adjRib] {
          for (auto& pfx : prefixes) {
            EXPECT_TRUE(adjRib->getRibEntry(/*ingress=*/false, pfx));
          }
          // All verification callbacks complete, now we can terminate
          evb_.terminateLoopSoon();
        },
        systemToSteady(runAtTime));
  }

  std::shared_ptr<AdjRib> GetAdjRibSubscribedToChangeTracker(
      const nettools::bgplib::BgpPeerId& peerId) {
    SetUpOutDelayAdjRib(peerId, 0s);
    auto adjRib = SetUpChangeListConsumer();
    adjRib->egressEoRsSent_ = true;
    adjRib->activateChangeListConsumer();
    return adjRib;
  }

  std::shared_ptr<AdjRib> SetUpChangeListConsumer() {
    static ConsumerBitmap dummyAddPathBitmap;
    static ConsumerBitmap dummyNonAddPathBitmap;
    auto changeListConsumer = std::make_shared<AdjRibOutConsumer>(
        changeListTracker_,
        adjRib_,
        "Test ChangeList Consumer",
        adjRib_->evb_,
        dummyAddPathBitmap,
        dummyNonAddPathBitmap);
    adjRib_->setChangeListConsumer(changeListConsumer);
    return adjRib_;
  }

  void PublishChangesToChangeList(RibOutAnnouncement& announcement) {
    for (const auto& entry : announcement.entries) {
      auto srRouteInfo = std::make_shared<ShadowRibRouteInfo>(
          entry.peer, entry.attrs, kDefaultPathID);
      srRouteInfo->flags |= SHADOWRIBROUTE_IN_UPDATE;
      entries.push_back(
          std::make_unique<TrackableObject<ShadowRibEntry>>(ShadowRibEntry(
              entry.prefix,
              std::move(srRouteInfo), /* bestpath */
              {} /* multipath */,
              entry.switchId,
              entry.multiPathSize,
              entry.aggregateReceivedUcmpWeight,
              entry.aggregateLocalUcmpWeight,
              entry.ribPolicyUcmpWeight,
              entry.newlyInstalledInLocalRib,
              entry.installTimeStamp)));
      changeListTracker_->publishChange(entries.back().get());
    }
  }

  long testOutDelayMs_{0};

  std::shared_ptr<ChangeTracker<ShadowRibEntry>> changeListTracker_ =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("Test ChangeTracker");
  std::vector<std::unique_ptr<TrackableObject<ShadowRibEntry>>> entries;
};

/**
 * Verify that when a prefix is announced from RIB after the initial dump,
 * that prefix's time delta to being processed in AdjRibOut
 * is greater than the configured out-delay.
 *
 * To verify the out delay, we check the Adj-RIB-OUT entries before
 * and after the scheduled out delay expiration time.
 *
 */
CO_TEST_F(AdjRibOutDelayFixture, SimpleOutDelayTest) {
  SetUpOutDelayAdjRib(kPeerId1, 3s);
  adjRib_->egressEoRsSent_ = true;
  RibStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();

  /* Check clean baseline of no new prefixes, no adj rib entries. */
  EXPECT_FALSE(adjRib_->outDelayTimer_);
  EXPECT_FALSE(adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1));
  EXPECT_FALSE(adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix2));

  auto attrs1 = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 1, 1));
  auto ribMsg = buildAnnouncementWithInstallTimestamp(
      {{attrs1, {kV4Prefix1, kV4Prefix2}}});
  auto installTimeStamp = ribMsg.entries.back().installTimeStamp;

  /* Mock announcement from RIB after initial dump. */
  pushRibOutMsgToAdjRib(ribMsg);

  /* No prefixes should be processed before out delay timer fires. */
  scheduleVerifyUnprocessedRelativeToOutDelay(
      installTimeStamp,
      -10 /* deltaMs */,
      {kV4Prefix1, kV4Prefix2},
      adjRib_.get());

  /* Both prefixes should be processed after out delay timer fires. */
  scheduleVerifyProcessedRelativeToOutDelay(
      installTimeStamp,
      5 /* deltaMs */,
      {kV4Prefix1, kV4Prefix2},
      adjRib_.get());

  /* We should see that the outDelayTimer is scheduled. */
  EXPECT_TRUE(adjRib_->outDelayTimer_->isScheduled());
  // ODS counter should reflect 2 deferred entries
  tcData->publishStats();
  EXPECT_EQ(2, tcData->getCounter(RibStats::kDeferredUpdatesCount));

  std::thread evbThread([this]() { evb_.loopForever(); });

  /* Wait for prefixes to come through to the queue. */
  auto msg = co_await facebook::bgp::test::boundedPop(
      *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");

  /* Event loop will naturally drain all pending callbacks and terminate */
  evbThread.join();

  EXPECT_TRUE(adjRib_->adjRibOutQueue_->empty());

  verifyPrefixesInUpdates({msg}, {kV4Prefix1, kV4Prefix2});

  // ODS counter should be 0 after timer processed all deferred entries
  tcData->publishStats();
  EXPECT_EQ(0, tcData->getCounter(RibStats::kDeferredUpdatesCount));
}

/*
 * Verify implicit withdrawal removes advertised prefix
 * before outdelay timer fires.
 */
CO_TEST_F(AdjRibOutDelayFixture, WithdrawBeforeOutDelayTimerFiresTest) {
  SetUpOutDelayAdjRib(kPeerId1, 3s);
  /* Make sure non initial announcement message doesn't get ignored. */
  adjRib_->egressEoRsSent_ = true;
  RibStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();

  /* Check clean baseline of no new prefixes, no adj rib entries. */
  EXPECT_FALSE(adjRib_->outDelayTimer_);
  EXPECT_FALSE(adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1));
  EXPECT_FALSE(adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix2));

  auto attrs1 = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 1, 1));
  auto ribMsg = buildAnnouncementWithInstallTimestamp(
      {{attrs1, {kV4Prefix1, kV4Prefix2}}});

  auto installTimeStamp = ribMsg.entries.back().installTimeStamp;

  /* Mock announcement from RIB after initial dump. */
  pushRibOutMsgToAdjRib(ribMsg);

  /* No prefixes should be processed before out delay timer fired. */
  scheduleVerifyUnprocessedRelativeToOutDelay(
      installTimeStamp,
      -10 /* deltaMs */,
      {kV4Prefix1, kV4Prefix2},
      adjRib_.get());
  /* Only kV4Prefix2 should be processed after out delay timer fired. */
  scheduleVerifyUnprocessedRelativeToOutDelay(
      installTimeStamp, 1 /* deltaMs */, {kV4Prefix1}, adjRib_.get());
  scheduleVerifyProcessedRelativeToOutDelay(
      installTimeStamp, 2 /* deltaMs */, {kV4Prefix2}, adjRib_.get());

  /* We should see that the outDelayTimer is scheduled. */
  EXPECT_TRUE(adjRib_->outDelayTimer_->isScheduled());
  EXPECT_EQ(2, adjRib_->deferredUpdates_.size());
  // ODS counter should reflect 2 deferred entries
  tcData->publishStats();
  EXPECT_EQ(2, tcData->getCounter(RibStats::kDeferredUpdatesCount));

  /* Send a rib withdraw for one of the two announced prefixes. */
  pushRibOutMsgToAdjRib(createRibSingleWithdrawal(kV4Prefix1));
  EXPECT_EQ(1, adjRib_->deferredUpdates_.size());
  EXPECT_TRUE(adjRib_->deferredUpdates_.contains(kV4Prefix2));
  // ODS counter decremented after withdrawal removed deferred entry
  tcData->publishStats();
  EXPECT_EQ(1, tcData->getCounter(RibStats::kDeferredUpdatesCount));

  std::thread evbThread([this]() { evb_.loopForever(); });

  /* Wait for prefixes to come through to the queue. */
  auto msg = co_await facebook::bgp::test::boundedPop(
      *adjRib_->adjRibOutQueue_, "adjRib_->adjRibOutQueue_");

  evbThread.join();

  EXPECT_TRUE(adjRib_->adjRibOutQueue_->empty());

  EXPECT_FALSE(adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1));

  verifyPrefixesInUpdates({msg}, {kV4Prefix2});

  // ODS counter should be 0 after timer processed the remaining deferred entry
  tcData->publishStats();
  EXPECT_EQ(0, tcData->getCounter(RibStats::kDeferredUpdatesCount));
}

/*
 * Verify initial announcement is not subject to out-delay.
 */
CO_TEST_F(AdjRibOutDelayFixture, RibInitialDumpHasNoOutDelayTest) {
  SetUpOutDelayAdjRib(kPeerId1, 3s);

  /* Check clean baseline of no new prefixes, no adj rib entries. */
  EXPECT_FALSE(adjRib_->outDelayTimer_);
  EXPECT_FALSE(adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix1));
  EXPECT_FALSE(adjRib_->getRibEntry(/*ingress=*/false, kV4Prefix2));

  auto attrs1 = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 1, 1));
  auto initialDump = buildAnnouncementWithInstallTimestamp(
      {{attrs1, {kV4Prefix1, kV4Prefix2}}},
      true /* initialDump */,
      true /* sendWithEoR */);

  auto installTimeStamp = initialDump.entries.back().installTimeStamp;

  pushRibOutMsgToAdjRib(initialDump);
  /*
   * All prefixes should be processed before out delay expiration because
   * these prefixes are not subject to out delay.
   */
  scheduleVerifyProcessedRelativeToOutDelay(
      installTimeStamp,
      -100 /* deltaMs */,
      {kV4Prefix1, kV4Prefix2},
      adjRib_.get());

  std::thread evbThread([this]() { evb_.loopForever(); });

  auto msg1 = co_await folly::coro::co_withExecutor(
      &evb_, adjRib_->adjRibOutQueue_->pop());
  auto eor = co_await folly::coro::co_withExecutor(
      &evb_, adjRib_->adjRibOutQueue_->pop());

  evbThread.join();

  EXPECT_TRUE(std::holds_alternative<BgpEndOfRib>(*eor));

  verifyPrefixesInUpdates({msg1}, {kV4Prefix1, kV4Prefix2});
}

/*
 * Verify that even with backpressure the out delay scheduled
 * prefixes will not appear earlier than the scheduled out delay,
 * AND no updates are dropped while backpressured.
 */
CO_TEST_F(AdjRibOutDelayFixture, OutDelayPrefixesInBackpressureTest) {
  SetUpOutDelayAdjRib(kPeerId1, 4s);
  auto adjRib = SetUpChangeListConsumer();

  /*
   * Override the ingress queue to unlimited since this test is focused on
   * egress backpressure, not ingress. The changelist-based flow doesn't
   * properly drain adjRibInQueue_, so with the new limited
   * kMaxIngressQueueSize, this test would timeout if the queue fills up and
   * blocks.
   */
  adjRib->adjRibInQueue_ = std::make_shared<AdjRib::AdjRibInQueueT>(
      std::numeric_limits<size_t>::max());

  adjRib->egressEoRsSent_ = true;
  adjRib->activateChangeListConsumer();

  /*
   * Announce 6 prefixes that are all subject to
   * out delay because of their install timestamp being the current time.
   */
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(5, 5, 5, 5));
  auto announcements = buildAnnouncementWithInstallTimestamp(
      {{attrs,
        {kV4Prefix1,
         kV4Prefix2,
         kV4Prefix3,
         kV4Prefix3,
         kV4Prefix4,
         kV4Prefix5,
         kV4Prefix6}}});
  auto installTimeStamp = announcements.entries.back().installTimeStamp;

  evb_.runInEventBaseThread([&] { PublishChangesToChangeList(announcements); });

  std::thread evbThread([this]() { evb_.loopForever(); });

  auto msg1 = co_await folly::coro::co_withExecutor(
      &evb_, adjRib->adjRibOutQueue_->pop());

  verifyPrefixesInUpdates(
      {msg1},
      {kV4Prefix1, kV4Prefix2, kV4Prefix3, kV4Prefix4, kV4Prefix5, kV4Prefix6});

  /*
   * These prefixes are no longer subject to out-delay when we use
   * the same installTimeStamp to announce them again with entirely
   * new attributes; use these changes to trigger backpressure.
   * This should generate 4 update messages to be queued but the highWm of
   * the queue is 3.
   */
  auto attrs1 = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 1, 1));
  auto attrs2 = std::make_shared<BgpPath>(*buildBgpPathFields(2, 2, 2, 2));
  auto attrs3 = std::make_shared<BgpPath>(*buildBgpPathFields(3, 3, 3, 3));
  auto attrs4 = std::make_shared<BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  auto announcementsWithoutOutDelay = buildAnnouncementWithInstallTimestamp(
      {{attrs1, {kV4Prefix1, kV4Prefix2}},
       {attrs2, {kV4Prefix3, kV4Prefix4}},
       {attrs3, {kV4Prefix5}},
       {attrs4, {kV4Prefix6}}},
      false,
      false,
      installTimeStamp);

  /*
   * Let's also publish a new never before seen prefix which
   * is now subject to out-delay.
   */
  auto newAnnouncement =
      buildAnnouncementWithInstallTimestamp({{attrs1, {kV4Prefix7}}});

  /*
   * Publish both batches in one evb_ callback. The change list consumer
   * also runs on evb_. With two callbacks, it can consume the first batch
   * and block the queue before kV4Prefix7 is published.
   */
  evb_.runInEventBaseThread([&] {
    PublishChangesToChangeList(announcementsWithoutOutDelay);
    PublishChangesToChangeList(newAnnouncement);
  });

  /*
   * All items have to be popped from the queue before the
   * packing timers can be rescheduled because the lowWm is 0.
   * When we resume here, we know that the queue is currently
   * blocked and all of the packing timers were canceled. The
   * out delayed prefix kV4Prefix7 is still stuck in deferredUpdates_.
   */
  auto msg2 = co_await folly::coro::co_withExecutor(
      &evb_, adjRib->adjRibOutQueue_->pop());

  EXPECT_TRUE(adjRib->adjRibOutQueue_->isBlocked());
  EXPECT_FALSE(adjRib->changeListConsumeTimer_->isScheduled());
  EXPECT_FALSE(adjRib->outDelayTimer_->isScheduled());
  EXPECT_EQ(1, adjRib->deferredUpdates_.size());
  EXPECT_TRUE(adjRib->deferredUpdates_.contains(kV4Prefix7));

  auto msg3 = co_await folly::coro::co_withExecutor(
      &evb_, adjRib->adjRibOutQueue_->pop());
  auto msg4 = co_await folly::coro::co_withExecutor(
      &evb_, adjRib->adjRibOutQueue_->pop());

  /*
   * From msg2 to msg4, we managed to unblock the queue by popping
   * all 3 messages.
   * After msg5 we know the queue freed up space.
   */
  auto msg5 = co_await folly::coro::co_withExecutor(
      &evb_, adjRib->adjRibOutQueue_->pop());
  EXPECT_FALSE(adjRib->adjRibOutQueue_->isBlocked());

  /* Wait for the out-delay prefix to get processed. */
  auto msg6 = co_await folly::coro::co_withExecutor(
      &evb_, adjRib->adjRibOutQueue_->pop());

  evb_.terminateLoopSoon();
  evbThread.join();

  EXPECT_TRUE(adjRib->adjRibOutQueue_->empty());

  /*
   * Verify that we received updates for all of the prefixes from
   * kV4Prefix1 to kV4Prefix6 in the four messages we drained.
   */
  verifyPrefixesInUpdates(
      {msg2, msg3, msg4, msg5},
      {kV4Prefix1, kV4Prefix2, kV4Prefix3, kV4Prefix4, kV4Prefix5, kV4Prefix6});

  /*
   * kV4Prefix7 should come in its own update despite having
   * the same egress attrs as kV4Prefix1 and kV4Prefix2, because of
   * out delay.
   */
  verifyPrefixesInUpdates({msg6}, {kV4Prefix7});

  adjRib->resetChangeListConsumer();
}

/**
 * This test verifies that the kEgressQueueBackpressuredEvents counter is
 * incremented properly, when backpressure is experienced.
 *
 * The regular StatsTest.cpp does not mock AdjRibs, and the
 * AdjRibOutDelayFixture already has a very nice setup rigged with
 * a changelist that can be published to, so we add the E2E stats test here.
 */
CO_TEST_F(AdjRibOutDelayFixture, NumBackpressureEventsStatsTest) {
  BgpStats::initCounters();
  auto counters = facebook::fb303::ThreadCachedServiceData::getShared();

  auto adjRib1 = GetAdjRibSubscribedToChangeTracker(kPeerId1);
  auto adjRib2 = GetAdjRibSubscribedToChangeTracker(kPeerId2);

  auto totalSentBefore = totalSentPrefixCount;

  auto attrs1 = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 1, 1));
  auto attrs2 = std::make_shared<BgpPath>(*buildBgpPathFields(2, 2, 2, 2));
  auto attrs3 = std::make_shared<BgpPath>(*buildBgpPathFields(3, 3, 3, 3));
  auto attrs4 = std::make_shared<BgpPath>(*buildBgpPathFields(4, 4, 4, 4));

  /* These announcements should generate 4 update messages to each adjRib. */
  auto announcements = buildAnnouncementWithInstallTimestamp(
      {{attrs1, {kV4Prefix1}},
       {attrs2, {kV4Prefix2}},
       {attrs3, {kV4Prefix3}},
       {attrs4, {kV4Prefix4}}});

  facebook::fb303::ThreadCachedServiceData::get()->publishStats();

  EXPECT_EQ(
      0,
      counters->getCounter(
          BgpStats::kEgressQueueBackpressuredEvents + ".count"));
  EXPECT_EQ(
      0,
      counters->getCounter(
          BgpStats::kEgressQueueBackpressuredEvents + ".count.60"));

  PublishChangesToChangeList(announcements);

  std::thread evbThread([this]() { evb_.loopForever(); });

  /*
   * Because the highWm is 3 and lowWm is 0, both queues should be blocked
   * until they are both drained to 0.
   */
  auto msgFromQ1 = co_await folly::coro::co_withExecutor(
      &evb_, adjRib1->adjRibOutQueue_->pop());
  auto msgFromQ2 = co_await folly::coro::co_withExecutor(
      &evb_, adjRib2->adjRibOutQueue_->pop());
  EXPECT_TRUE(adjRib1->adjRibOutQueue_->isBlocked());
  EXPECT_TRUE(adjRib2->adjRibOutQueue_->isBlocked());
  /*
   * Drain queues to let sendBgpMessages continue queueing updates.
   */
  while (!adjRib1->adjRibOutQueue_->empty()) {
    co_await facebook::bgp::test::boundedPop(
        *adjRib1->adjRibOutQueue_, "adjRib1->adjRibOutQueue_");
  }
  co_await facebook::bgp::test::boundedPop(
      *adjRib1->adjRibOutQueue_, "adjRib1->adjRibOutQueue_");
  while (!adjRib2->adjRibOutQueue_->empty()) {
    co_await facebook::bgp::test::boundedPop(
        *adjRib2->adjRibOutQueue_, "adjRib2->adjRibOutQueue_");
  }
  co_await facebook::bgp::test::boundedPop(
      *adjRib2->adjRibOutQueue_, "adjRib2->adjRibOutQueue_");

  facebook::fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_EQ(
      2,
      counters->getCounter(
          BgpStats::kEgressQueueBackpressuredEvents + ".count"));
  EXPECT_EQ(
      2,
      counters->getCounter(
          BgpStats::kEgressQueueBackpressuredEvents + ".count.60"));
  EXPECT_EQ(1, adjRib1->stats_.getEgressQueueBackpressuredEvents());
  EXPECT_EQ(1, adjRib2->stats_.getEgressQueueBackpressuredEvents());

  /* Each adjRib processed 4 prefixes independently from the CL.
   * totalSentPrefixCount should increase by 4 per peer = 8 total. */
  EXPECT_EQ(adjRib1->stats_.getPostOutPrefixCount(), 4);
  EXPECT_EQ(adjRib1->stats_.getPreOutPrefixCount(), 4);
  EXPECT_EQ(adjRib2->stats_.getPostOutPrefixCount(), 4);
  EXPECT_EQ(adjRib2->stats_.getPreOutPrefixCount(), 4);
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 8);

  // Stop the event loop and join the thread before cleanup
  evb_.terminateLoopSoon();
  evbThread.join();

  adjRib1->resetChangeListConsumer();
  adjRib2->resetChangeListConsumer();
}

TEST_F(AdjRibOutDelayFixture, OutDelayTimerTest) {
  // Peer-1 has out-delay 1sec
  SetUpOutDelayAdjRib(kPeerId1, 1s);
  adjRib_->egressEoRsSent_ = true;

  fm_->addTask([&]() {
    auto attrs1 = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 1, 1));
    auto ribMsg =
        buildAnnouncementWithInstallTimestamp({{attrs1, {kV4Prefix1}}});
    pushRibOutMsgToAdjRib(ribMsg);

    fiberSleepFor(1s);
    std::vector<folly::CIDRNetwork> newDeferredPrefixes = {kV4Prefix1};
    AdjRibOutDelayEntry entry(
        std::chrono::system_clock::now(), newDeferredPrefixes);

    // Add 15 items to the priority queue.
    for (int i = 0; i < 15; i++) {
      adjRib_->outDelayPQ_.push(entry);
    }

    /*
     * Each programOutDelayTimer() call handles up to
     * 8 deferred items in the queue
     * If there is any item left in the PQ,
     * it sets up a timer that calls
     * the same function after 25 ms
     */
    adjRib_->programOutDelayTimer();
    EXPECT_TRUE(adjRib_->outDelayPQ_.size() > 0);
    EXPECT_TRUE(adjRib_->outDelayTimer_->isScheduled());

    // The remaining items in the queue should be flushed out after 25 ms
    fiberSleepFor(550ms);
    EXPECT_EQ(adjRib_->outDelayPQ_.size(), 0);
    EXPECT_FALSE(adjRib_->outDelayTimer_->isScheduled());

    evb_.terminateLoopSoon();
  });

  evb_.loopForever();
}

} // namespace facebook::bgp
