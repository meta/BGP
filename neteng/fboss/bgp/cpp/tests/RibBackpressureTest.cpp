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

#define RibBase_TEST_FRIENDS                                             \
  friend class QueueFairnessMockRib;                                     \
  FRIEND_TEST(RibFixture, MultipleRibPauseResumeFromDifferentTasksTest); \
  FRIEND_TEST(                                                           \
      RibFixture,                                                        \
      InlineBackpressurePauseAndResumeBestPathAndFibProgrammingTest);    \
  FRIEND_TEST(                                                           \
      RibFixture, InlineBackpressureTestWithAnnouncementsAndWithdraws);  \
  FRIEND_TEST(RibBackpressureTest, RibInQueueBackpressureTest);          \
  FRIEND_TEST(RibBackpressureTest, RibInQueueConsumerScopeTest);         \
  FRIEND_TEST(RibBackpressureTest, RibInQueueConsumerScopeExceptionPathTest);

#include <fmt/format.h>
#include <folly/CancellationToken.h>
#include <folly/IPAddress.h>
#include <folly/coro/AsyncScope.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Task.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/fibers/FiberManager.h>
#include <folly/io/async/EventBase.h>
#include <folly/logging/xlog.h>

#include "neteng/fboss/bgp/cpp/lib/fibers/Utils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibUtils.h"

using namespace facebook::nettools::bgplib;
using namespace std::chrono;

namespace facebook::bgp {

constexpr size_t kBusyQueueMessages{2};

class QueueFairnessMockRib final : public MockRib {
 public:
  enum class BusyQueue {
    RIB_IN,
    RIB_POLICY,
  };

  QueueFairnessMockRib(
      const BgpGlobalConfig& bgpGlobalConfig,
      nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ,
      MonitoredMPMCQueue<RibOutMessage>& ribOutQ,
      BusyQueue busyQueue)
      : MockRib(
            {},
            bgpGlobalConfig,
            std::nullopt,
            ribInQ,
            ribOutQ,
            "dev",
            nullptr),
        ribInQForTest_(ribInQ),
        busyQueue_(busyQueue) {}

  void fillBusyQueue(const TinyPeerInfo& peer) {
    if (busyQueue_ == BusyQueue::RIB_IN) {
      for (size_t i = 0; i < kBusyQueueMessages; ++i) {
        folly::coro::blockingWait(
            ribInQForTest_.push(RibInAddPathGrUpdate{peer, {}}));
      }
    } else {
      enqueueRibPolicyMsg(RouteAttributePolicyClearMsg{});
      enqueueRibPolicyMsg(PathSelectionPolicyClearMsg{});
      enqueueRibPolicyMsg(RouteFilterPolicyClearMsg{});
      enqueueRibPolicyMsg(RouteAttributePolicyTimerMsg{});
    }
  }

  folly::coro::Task<void> processBusyQueue() noexcept {
    switch (busyQueue_) {
      case BusyQueue::RIB_IN:
        co_await processRibInMsgLoop();
        break;
      case BusyQueue::RIB_POLICY:
        co_await RibDC::processRibPolicyMsgLoop();
        break;
    }
    co_return;
  }

  size_t busyQueueSize() const noexcept {
    if (busyQueue_ == BusyQueue::RIB_IN) {
      return ribInQForTest_.size();
    }
    return ribPolicyMsgQ_.size();
  }

 protected:
  bool replaceRouteAttributePolicy(
      std::unique_ptr<RouteAttributePolicy> /* newPolicy */) override {
    return false;
  }

  bool replacePathSelectionPolicy(
      std::unique_ptr<PathSelectionPolicy> /* newPolicy */,
      bool /* isBootstrap */,
      bool /* forceUpdate */) override {
    return false;
  }

  bool replaceRouteFilterPolicy(
      std::unique_ptr<RouteFilterPolicy> /* newPolicy */,
      bool /* isBootstrap */,
      bool /* forceUpdate */) override {
    return false;
  }

 private:
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQForTest_;
  const BusyQueue busyQueue_;
};

namespace {

template <typename RibT>
void expectOneExecutorEpochToLeaveMessages(
    RibT& rib,
    size_t expectedRemaining) {
  folly::ManualExecutor executor;
  folly::CancellationSource cancellationSource;
  bool consumerCompleted{false};
  bool consumerCompletedCleanly{false};
  auto consumer =
      folly::coro::co_withExecutor(&executor, rib.processBusyQueue());
  std::move(consumer).start(
      [&](auto&& result) {
        consumerCompleted = true;
        consumerCompletedCleanly = result.hasValue() ||
            result.template hasException<folly::OperationCancelled>();
      },
      cancellationSource.getToken());

  executor.run();
  EXPECT_EQ(expectedRemaining, rib.busyQueueSize());
  EXPECT_FALSE(consumerCompleted);

  cancellationSource.requestCancellation();
  executor.drain();
  EXPECT_TRUE(consumerCompleted);
  EXPECT_TRUE(consumerCompletedCleanly);
}

void expectBusyInputQueueToYield(QueueFairnessMockRib::BusyQueue busyQueue) {
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ{
      kBusyQueueMessages};
  MonitoredMPMCQueue<RibOutMessage> ribOutQ;
  BgpGlobalConfig bgpGlobalConfig(
      kAsn1,
      kLocalAddr1,
      kPeerAddr3,
      kHoldTime,
      std::nullopt,
      kGrRestartTime,
      {},
      {});
  auto rib = std::make_unique<QueueFairnessMockRib>(
      bgpGlobalConfig, ribInQ, ribOutQ, busyQueue);
  rib->createFib();
  const TinyPeerInfo peer{
      folly::IPAddress("10.0.0.2"),
      kAsn1,
      kPeerAddr1.asV4().toLongHBO(),
      BgpSessionType::IBGP,
      false};
  rib->fillBusyQueue(peer);
  const auto initialQueueSize = rib->busyQueueSize();
  expectOneExecutorEpochToLeaveMessages(*rib, initialQueueSize - 1);
}

} // namespace

/**
 * This test ensures RIB is resumed after receiving
 * ResumeBestPathAndFibProgramming from all tasks that sent
 * PauseBestPathAndFibProgramming
 */
TEST_F(RibFixture, MultipleRibPauseResumeFromDifferentTasksTest) {
  // Step 1: Send EOR to simulate RIB in steady state
  auto fibFuture = fib_->getFibProgramFuture();
  rib_->setRibPauseTime(seconds(30));
  sendInitialPathComputation();
  fibFuture.wait();
  /*
   * Step 2: Send PauseBestPathAndFibProgramming message to rib from SAFE_MODE
   * and verify best path and Fib programming is paused
   */
  sendPauseBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE);

  /*
   * Step 3: Send PauseBestPathAndFibProgramming message to rib from
   * BACKPRESSURE and verify best path and Fib programming is paused
   */
  sendPauseBestPathAndFibProgramming(RibPauseResumeCause::BACKPRESSURE);

  /*
   * Step 4: Send PauseBestPathAndFibProgramming message to rib from
   * ROUTE_FILTER_POLICY_UPDATE and verify best path and Fib programming is
   * paused
   */
  sendPauseBestPathAndFibProgramming(
      RibPauseResumeCause::ROUTE_FILTER_POLICY_UPDATE);

  /*
   * Step 5: Send ResumeBestPathAndFibProgramming message to rib from
   * ROUTE_FILTER_POLICY_UPDATE
   */
  sendResumeBestPathAndFibProgramming(
      RibPauseResumeCause::ROUTE_FILTER_POLICY_UPDATE);

  /*
   * Step 6: Send ResumeBestPathAndFibProgramming message to rib from
   * ROUTE_FILTER_POLICY_UPDATE again and ensure RIB is not resumed
   */
  sendResumeBestPathAndFibProgramming(
      RibPauseResumeCause::ROUTE_FILTER_POLICY_UPDATE);
  WITH_RETRIES_N_TIMED(600, milliseconds(50), {
    EXPECT_EVENTUALLY_TRUE(isBestPathAndFibProgrammingPaused());
    EXPECT_EVENTUALLY_EQ(
        2, rib_->bestPathAndFibProgrammingPausedBy_.rlock()->size());
    EXPECT_EVENTUALLY_TRUE(
        rib_->bestPathAndFibProgrammingPausedBy_.rlock()->contains(
            RibPauseResumeCause::SAFE_MODE));
    EXPECT_EVENTUALLY_TRUE(
        rib_->bestPathAndFibProgrammingPausedBy_.rlock()->contains(
            RibPauseResumeCause::BACKPRESSURE));
  });

  /*
   * Step 7: Resume BACKPRESSURE and verify SAFE_MODE still keeps best path
   * and Fib programming paused.
   */
  sendResumeBestPathAndFibProgramming(RibPauseResumeCause::BACKPRESSURE);
  WITH_RETRIES_N_TIMED(600, milliseconds(50), {
    EXPECT_EVENTUALLY_TRUE(isBestPathAndFibProgrammingPaused());
    EXPECT_EVENTUALLY_EQ(
        1, rib_->bestPathAndFibProgrammingPausedBy_.rlock()->size());
    EXPECT_EVENTUALLY_TRUE(
        rib_->bestPathAndFibProgrammingPausedBy_.rlock()->contains(
            RibPauseResumeCause::SAFE_MODE));
  });

  /*
   * Step 8: Send ResumeBestPathAndFibProgramming message to rib from
   * SAFE_MODE and verify best path and Fib programming is now resumed.
   */
  sendResumeBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE);
  WITH_RETRIES(
      { ASSERT_EVENTUALLY_FALSE(isBestPathAndFibProgrammingPaused()); });
}

/**
 * This test verifies RibAnnouncements and RibWithdrawals when local RIB
 * thread operations are paused and then resumed with BACKPRESSURE
 */
TEST_F(
    RibFixture,
    InlineBackpressurePauseAndResumeBestPathAndFibProgrammingTest) {
  /*
   * Case 1: Send 1 RibAnnouncement when local Rib thread operations are
   * paused and 1 RibAnnouncement when it is resumed
   */
  auto prefixBatch1 = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  {
    auto fibFuture = fib_->getFibProgramFuture();
    // Step 1: Send EOR to simulate RIB in steady state
    sendInitialPathComputation();

    // Step 2: Send PauseBestPathAndFibProgramming message to rib
    sendPauseBestPathAndFibProgramming(RibPauseResumeCause::BACKPRESSURE);

    // Step 3: Send 1 announcement when local RIB thread is paused
    sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);

    // Step 4: Send withdrawal of the same prefix
    sendWithdrawal(prefixBatch1, iBgpPeer_);

    // Step 5: Resume best path computation and fib programming
    sendResumeBestPathAndFibProgramming(RibPauseResumeCause::BACKPRESSURE);

    fibFuture.wait();
    EXPECT_TRUE(rib_->ribEntries_.find(kV4Prefix1) == rib_->ribEntries_.end());

    fibFuture = fib_->getFibProgramFuture();
    // Step 6: Send PauseBestPathAndFibProgramming message to rib
    sendPauseBestPathAndFibProgramming(RibPauseResumeCause::BACKPRESSURE);

    // Step 7: Announce same prefix batch again
    sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);

    // Step 8: Resume best path computation and fib programming
    sendResumeBestPathAndFibProgramming(RibPauseResumeCause::BACKPRESSURE);

    // Step 9: Send 1 another announcement
    auto prefixBatch2 = PrefixPathIds{{kV6Prefix2, kDefaultPathID}};
    sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);

    fibFuture.wait();

    /*
     * Test expects to see RibInitialAnnouncementStart and both the
     * announcements
     */
    WITH_RETRIES({ ASSERT_EVENTUALLY_TRUE(ribOutQ_.size() == 4); });
    // RibInitialAnnouncementStart
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibInitialAnnouncementStart>(msg));

    std::vector<RibOutAnnouncement> rcvdAnnouncements;

    // Announcement 1
    msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    rcvdAnnouncements.emplace_back(std::get<RibOutAnnouncement>(msg));

    // Announcement 2
    msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutAnnouncement>(msg));
    rcvdAnnouncements.emplace_back(std::get<RibOutAnnouncement>(msg));

    EXPECT_EQ(2, rcvdAnnouncements.size());
    // Check first announcement
    EXPECT_TRUE(rcvdAnnouncements[0].sendWithEoR);
    EXPECT_TRUE(rcvdAnnouncements[0].initialDump);
    // Check second announcement
    EXPECT_FALSE(rcvdAnnouncements[1].sendWithEoR);
    EXPECT_FALSE(rcvdAnnouncements[1].initialDump);

    // pop last message out
    msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
  }
  /*
   * Case 2: Send 1 RibAnnouncement when local Rib thread operations are
   * paused and 1 RibWithdrawal when it is resumed
   */
  {
    auto fibFuture = fib_->getFibProgramFuture();

    // Step 1: Send PauseBestPathAndFibProgramming message to rib
    sendPauseBestPathAndFibProgramming(RibPauseResumeCause::BACKPRESSURE);

    // Step 2: Send 1 announcement when local RIB thread is paused
    sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);

    // Step 3: Send ResumeBestPathAndFibProgramming message to rib
    sendResumeBestPathAndFibProgramming(RibPauseResumeCause::BACKPRESSURE);

    // Test expects to see 1 announcement
    WITH_RETRIES({ ASSERT_EVENTUALLY_TRUE(ribOutQ_.size() == 0); });

    // Step 5: Send withdrawal of the announced prefix
    sendWithdrawal(prefixBatch1, iBgpPeer_);

    fibFuture.wait();

    // Test expects to see 1 withdrawal
    WITH_RETRIES({ ASSERT_EVENTUALLY_TRUE(ribOutQ_.size() == 1); });
    auto msg = folly::coro::blockingWait(ribOutQ_.pop()).payload;
    ASSERT_TRUE(std::holds_alternative<RibOutWithdrawal>(msg));
  }
}

TEST_F(RibFixture, InlineBackpressureTestWithAnnouncementsAndWithdraws) {
  RibStats::initCounters();
  // set inline backpressure threshold
  rib_->ribOutQHighWatermark_ = 2;

  PrefixPathIds prefixBatch1;
  for (uint16_t i = 1; i <= 10; ++i) {
    auto prefix =
        folly::IPAddress::createNetwork(fmt::format("1:{}::/64", i), 64);
    prefixBatch1.emplace_back(prefix, kDefaultPathID);
  }
  auto prefixBatch2 = PrefixPathIds{
      {folly::IPAddress::createNetwork("2::/64"), kDefaultPathID}};

  // Step 1: Send RibInEOR to simulate RIB in steady state
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();

  // Step 2: Now, in a loop, announce, withdraw and re-advertise the routes
  for (int i = 0; i < 5; i++) {
    sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
    sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);
    sendWithdrawal(prefixBatch1, iBgpPeer_);
    sendWithdrawal(prefixBatch2, iBgpPeer_);
    sendAnnouncement(prefixBatch1, iBgpPeer_, attr_);
    sendAnnouncement(prefixBatch2, iBgpPeer_, attr_);
  }

  // Step 3: Expect Rib Pause due to inline backpressure kicking in
  WITH_RETRIES(
      { ASSERT_EVENTUALLY_TRUE(isBestPathAndFibProgrammingPaused()); });

  // Step 4: send signal to resume from inline backpressure
  sendResumeBestPathAndFibProgramming(RibPauseResumeCause::BACKPRESSURE);

  WITH_RETRIES(
      { ASSERT_EVENTUALLY_FALSE(isBestPathAndFibProgrammingPaused()); });
}

TEST(RibInputQueueFairnessTest, RibInQueueYieldsAfterEveryMessage) {
  expectBusyInputQueueToYield(QueueFairnessMockRib::BusyQueue::RIB_IN);
}

TEST(RibInputQueueFairnessTest, RibDcPolicyQueueYieldsAfterEveryMessage) {
  expectBusyInputQueueToYield(QueueFairnessMockRib::BusyQueue::RIB_POLICY);
}

/**
 * Test that ribInQ handles backpressure correctly when the queue is full.
 * This test validates the queue-level backpressure mechanism by:
 * 1. Creating a standalone mock Rib without starting message processing
 * 2. Filling ribInQ to capacity (maxIngressQueueSize) with announcements
 * 3. Verifying the queue is exactly at capacity
 * 4. Spawning a producer fiber that attempts to push additional messages
 * 5. Confirming the producer fiber blocks and queue remains at capacity
 * 6. Starting Rib's processRibInMsgLoop coroutine to drain the queue
 * 7. Verifying the queue drains, unblocking the producer fiber
 * 8. Confirming all messages are processed and producer completes successfully
 *
 * NOTE: This test does NOT use RibFixture to avoid automatic RIB startup.
 * It manually creates ribInQ, ribOutQ, MockRib, and manages the full lifecycle
 * to have precise control over when message processing begins.
 */
TEST(RibBackpressureTest, RibInQueueBackpressureTest) {
  size_t maxIngressQueueSize = 10;

  // Create the ribInQ and ribOutQ
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ{
      maxIngressQueueSize};
  MonitoredMPMCQueue<RibOutMessage> ribOutQ;

  // Create BGP global config
  BgpGlobalConfig bgpGlobalConfig(
      kAsn1, // localAsn
      kLocalAddr1, // routerId
      kPeerAddr3, // clusterId
      kHoldTime, // holdTime
      std::nullopt, // listenAddr
      kGrRestartTime, // grRestartTime
      {}, // networksV4
      {} // networksV6
  );

  // Create mock Rib (note: we DON'T start the rib thread)
  auto mockRib = std::make_unique<MockRib>(
      std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>(),
      bgpGlobalConfig,
      std::nullopt, // policy config
      ribInQ,
      ribOutQ,
      "dev", // platform
      nullptr // FsdbSyncer
  );

  // Create peer info and attributes for announcement
  TinyPeerInfo iBgpPeer{
      folly::IPAddress("10.0.0.2"),
      kAsn1,
      kPeerAddr1.asV4().toLongHBO(),
      BgpSessionType::IBGP,
      false};

  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attr->publish();

  // Create fiber manager for testing
  folly::EventBase evb;
  auto& fm = folly::fibers::getFiberManager(evb, getFiberManagerOptions());

  fm.addTask([&] {
    // Create RibInMessage to fill the queue
    auto announcement = RibInAnnouncement(
        iBgpPeer,
        {{folly::IPAddress::createNetwork("10.0.1.0/24"), kDefaultPathID}},
        attr);

    /*
     * Fill the queue to exactly capacity
     * Note: Rib processing is stopped, so messages accumulate in ribInQ
     */
    fm.addTask([&] {
      for (size_t i = 0; i < maxIngressQueueSize + 2; ++i) {
        ribInQ.fiberPush(announcement);
      }
    });

    /*
     * Wait for queue to fill to capacity by polling the queue size
     * Use fiberSleepFor(0ms) to allow event loop to run
     */
    while (ribInQ.size() < maxIngressQueueSize) {
      folly::fibers::yield();
    }

    // Verify ribInQ is exactly at capacity
    auto queueSize = ribInQ.size();
    XLOGF(INFO, "ribInQ size after filling: {}", queueSize);
    EXPECT_EQ(maxIngressQueueSize, queueSize);

    /*
     * Create a fiber task to try pushing additional messages
     * This should block because the queue is full
     */
    std::atomic<bool> pushBlocked{true};
    fm.addTask([&] {
      const size_t additionalMessages = 3;
      for (size_t i = 0; i < additionalMessages; ++i) {
        // This should block on the first push
        ribInQ.fiberPush(announcement);
      }
      pushBlocked = false;
    });

    /*
     * Yield multiple times and verify queue size stays at capacity
     * The push fiber should be blocked and not complete
     */
    for (int i = 0; i < 10; ++i) {
      fiberSleepFor(0ms);
      queueSize = ribInQ.size();
      EXPECT_EQ(maxIngressQueueSize, queueSize);
      // Verify the push fiber hasn't completed yet
      EXPECT_TRUE(pushBlocked.load());
    }

    XLOGF(INFO, "ribInQ size after attempting more pushes: {}", queueSize);
    EXPECT_EQ(maxIngressQueueSize, queueSize);

    /*
     * Start Rib processing by adding processRibInMsgLoop to async scope
     * The coroutine is scheduled on the local event base via co_withExecutor
     * The event loop (evb.loop()) will process both fibers and the coroutine
     */
    XLOG(INFO, "Starting Rib processing to drain queue");

    /*
     * Create async scope and add processRibInMsgLoop coroutine
     * The async scope manages the coroutine lifecycle for later cancellation
     */
    folly::coro::CancellableAsyncScope asyncScope;
    asyncScope.add(
        folly::coro::co_withExecutor(&evb, mockRib->processRibInMsgLoop()));

    // Wait for queue to start draining (unblocks the producer)
    while (ribInQ.size() >= maxIngressQueueSize) {
      // Use fiberSleepFor(0ms) to allow event loop and coroutines to run
      fiberSleepFor(0ms);
    }

    XLOG(INFO, "Queue started draining, producer should now be unblocked");

    // Wait for the blocked push to complete
    while (pushBlocked.load()) {
      fiberSleepFor(0ms);
    }

    // Wait for all messages to be processed
    while (ribInQ.size() > 0) {
      fiberSleepFor(0ms);
    }

    XLOG(INFO, "Queue fully drained");
    EXPECT_EQ(0, ribInQ.size());

    // Verify the push fiber completed after queue was drained
    EXPECT_FALSE(pushBlocked.load());

    // Cancel the async scope to stop the processRibInMsgLoop coroutine
    XLOG(INFO, "Cancelling async scope to stop processRibInMsgLoop");
    folly::coro::blockingWait(asyncScope.cancelAndJoinAsync());
  });

  evb.loop();
}

/**
 * Test that ConsumerScope correctly manages queue state:
 * 1. By default, ribInQ is open
 * 2. Close the queue manually and verify pushes are dropped
 * 3. Start processRibInMsgLoop - it creates ConsumerScope which opens the queue
 * 4. Verify pushes work when the loop is running (queue is open)
 * 5. Cancel the async scope to end the loop
 * 6. Verify the queue is closed after the loop ends
 *
 * NOTE: This test does NOT use RibFixture to avoid automatic RIB startup.
 * It manually creates ribInQ, ribOutQ, MockRib, and manages the full lifecycle
 * to have precise control over when message processing begins.
 */
TEST(RibBackpressureTest, RibInQueueConsumerScopeTest) {
  // Create the ribInQ and ribOutQ
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ{1};
  MonitoredMPMCQueue<RibOutMessage> ribOutQ;

  // Create BGP global config
  BgpGlobalConfig bgpGlobalConfig(
      kAsn1, // localAsn
      kLocalAddr1, // routerId
      kPeerAddr3, // clusterId
      kHoldTime, // holdTime
      std::nullopt, // listenAddr
      kGrRestartTime, // grRestartTime
      {}, // networksV4
      {} // networksV6
  );

  // Create mock Rib (note: we DON'T start the rib thread)
  auto mockRib = std::make_unique<MockRib>(
      std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>(),
      bgpGlobalConfig,
      std::nullopt, // policy config
      ribInQ,
      ribOutQ,
      "dev", // platform
      nullptr // FsdbSyncer
  );

  // Create peer info and attributes for announcement
  TinyPeerInfo eBgpPeer{
      folly::IPAddress("10.0.0.2"),
      kAsn1,
      kPeerAddr1.asV4().toLongHBO(),
      BgpSessionType::EBGP,
      false};

  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attr->publish();

  // Step 1: Verify ribInQ is open by default (pushes work)
  auto prefixBatch1 = PrefixPathIds{
      {folly::IPAddress::createNetwork("1::/64"), kDefaultPathID}};

  folly::coro::blockingWait(
      ribInQ.push(RibInAnnouncement(eBgpPeer, prefixBatch1, attr)));
  EXPECT_EQ(1, ribInQ.size());

  // Step 2: Close the queue manually and verify pushes are dropped
  ribInQ.close();

  auto prefixBatch2 = PrefixPathIds{
      {folly::IPAddress::createNetwork("2::/64"), kDefaultPathID}};
  folly::coro::blockingWait(
      ribInQ.push(RibInAnnouncement(eBgpPeer, prefixBatch2, attr)));
  EXPECT_EQ(1, ribInQ.size()); // Push was dropped

  /*
   * Step 3: Start processRibInMsgLoop
   * This creates a ConsumerScope which should open the queue
   */
  folly::coro::CancellableAsyncScope asyncScope;
  folly::EventBase evb;
  asyncScope.add(
      folly::coro::co_withExecutor(&evb, mockRib->processRibInMsgLoop()));

  // Run the event loop in a separate thread to process the coroutine
  std::thread evbThread([&evb]() { evb.loop(); });

  // Step 4: Verify pushes work now that queue is open (ConsumerScope opened it)
  auto prefixBatch3 = PrefixPathIds{
      {folly::IPAddress::createNetwork("3::/64"), kDefaultPathID}};
  folly::coro::blockingWait(
      ribInQ.push(RibInAnnouncement(eBgpPeer, prefixBatch3, attr)));

  // Push more messages to verify the loop is still running
  for (int i = 0; i < 10; ++i) {
    auto prefix = fmt::format("{}::/64", i);
    auto prefixBatch = PrefixPathIds{
        {folly::IPAddress::createNetwork(prefix), kDefaultPathID}};
    folly::coro::blockingWait(
        ribInQ.push(RibInAnnouncement(eBgpPeer, prefixBatch, attr)));
  }

  // Step 5: Cancel the async scope to stop processRibInMsgLoop
  XLOG(INFO, "Cancelling async scope to stop processRibInMsgLoop");
  folly::coro::blockingWait(asyncScope.cancelAndJoinAsync());
  evbThread.join();

  /*
   * Step 6: Verify queue is closed after ConsumerScope is destroyed
   * Queue is now closed, push should be dropped
   */
  size_t sizeBeforePush = ribInQ.size();
  auto prefixBatch4 = PrefixPathIds{
      {folly::IPAddress::createNetwork("11::/64"), kDefaultPathID}};
  folly::coro::blockingWait(
      ribInQ.push(RibInAnnouncement(eBgpPeer, prefixBatch4, attr)));
  EXPECT_EQ(sizeBeforePush, ribInQ.size()); // Push was dropped

  XLOG(INFO, "ProcessRibInMsgLoop ConsumerScope test completed");
}

/**
 * Test that ConsumerScope properly closes the queue even when the
 * processing loop exits via cancellation (not normal termination).
 *
 * This tests a different exit path than normal termination:
 * - Normal test: uses cancelAndJoinAsync after processing messages
 * - This test: destroys MockRib while loop is running → triggers cleanup →
 * ConsumerScope closes queue
 *
 * This verifies the RAII guarantee works for the object destruction code path.
 */
TEST(RibBackpressureTest, RibInQueueConsumerScopeExceptionPathTest) {
  // Create the ribInQ and ribOutQ
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ{1};
  MonitoredMPMCQueue<RibOutMessage> ribOutQ;

  // Create BGP global config
  BgpGlobalConfig bgpGlobalConfig(
      kAsn1, // localAsn
      kLocalAddr1, // routerId
      kPeerAddr3, // clusterId
      kHoldTime, // holdTime
      std::nullopt, // listenAddr
      kGrRestartTime, // grRestartTime
      {}, // networksV4
      {} // networksV6
  );

  // Create mock Rib (note: we DON'T start the rib thread)
  auto mockRib = std::make_unique<MockRib>(
      std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>(),
      bgpGlobalConfig,
      std::nullopt, // policy config
      ribInQ,
      ribOutQ,
      "dev", // platform
      nullptr // FsdbSyncer
  );

  // Create peer info and attributes for announcement
  TinyPeerInfo eBgpPeer{
      folly::IPAddress("10.0.0.2"),
      kAsn1,
      kPeerAddr1.asV4().toLongHBO(),
      BgpSessionType::EBGP,
      false};

  auto attr =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attr->publish();

  /*
   * Step 1: Start processRibInMsgLoop
   * This creates a ConsumerScope which opens the queue
   */
  folly::coro::CancellableAsyncScope asyncScope;
  folly::EventBase evb;
  asyncScope.add(
      folly::coro::co_withExecutor(&evb, mockRib->processRibInMsgLoop()));

  // Run the event loop in a separate thread to process the coroutine
  std::thread evbThread([&evb]() { evb.loop(); });

  // Step 2: Verify pushes work (queue is open via ConsumerScope)
  auto prefix1 = folly::IPAddress::createNetwork("1::/64");
  auto prefixBatch1 = PrefixPathIds{{prefix1, kDefaultPathID}};
  folly::coro::blockingWait(
      ribInQ.push(RibInAnnouncement(eBgpPeer, prefixBatch1, attr)));

  /*
   * Verify the announcement was consumed (and hence has been pushed
   * successfully) by checking ribEntries_ on the evb thread
   */
  WITH_RETRIES({
    bool found = false;
    evb.runInEventBaseThreadAndWait([&]() {
      found = mockRib->ribEntries_.find(prefix1) != mockRib->ribEntries_.end();
    });
    ASSERT_EVENTUALLY_TRUE(found);
  });

  /*
   * Step 3: Destroy MockRib WITHOUT normal termination
   * This triggers cleanup. The async scope will be cancelled next,
   * causing the loop to exit via cancellation and ConsumerScope to close queue.
   */
  XLOG(INFO, "Destroying MockRib to trigger exception path");
  evb.runInEventBaseThreadAndWait([&]() { mockRib.reset(); });

  // Step 4: Cancel and wait for the loop to exit
  folly::coro::blockingWait(asyncScope.cancelAndJoinAsync());
  evbThread.join();

  /*
   * Step 5: Verify queue is closed by ConsumerScope destructor
   * (RAII guarantee - queue closed even on cancellation exit path)
   * Push should be dropped when queue is closed
   */
  size_t sizeBeforePush = ribInQ.size();
  auto prefixBatch2 = PrefixPathIds{
      {folly::IPAddress::createNetwork("2::/64"), kDefaultPathID}};
  folly::coro::blockingWait(
      ribInQ.push(RibInAnnouncement(eBgpPeer, prefixBatch2, attr)));
  EXPECT_EQ(sizeBeforePush, ribInQ.size()); // Push was dropped

  XLOG(INFO, "ProcessRibInMsgLoop ConsumerScope exception path test completed");
}

} // namespace facebook::bgp
