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

#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#define NexthopHandler_TEST_FRIENDS                                       \
  friend class NexthopHandlerTestFixture;                                 \
  FRIEND_TEST(NexthopHandlerTestFixture, TestStreamNextHopStatus);        \
  FRIEND_TEST(NexthopHandlerTestFixture, TestStreamNextHopStatusUpdate);  \
  FRIEND_TEST(                                                            \
      NexthopHandlerTestFixture, TestStreamErrorOrEmptyResponseRecovery); \
  FRIEND_TEST(NexthopHandlerTestFixture, TestStopCancelsCoroutines);      \
  FRIEND_TEST(                                                            \
      NexthopHandlerTestFixture, TestUpdateCacheAndNotifyRibPushesToRibInQ);

#include <folly/IPAddress.h>
#include <folly/Synchronized.h>
#include <folly/coro/Task.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/ScopedEventBaseThread.h>

#include <thrift/lib/cpp2/async/RocketClientChannel.h>
#include <thrift/lib/cpp2/util/ScopedServerInterfaceThread.h>

#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopCache.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopHandler.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "openr/if/gen-cpp2/FibService.h"
#include "openr/if/gen-cpp2/Platform_types.h"

using namespace facebook::bgp;
using namespace ::testing;
using namespace folly;

namespace facebook::bgp {

class MockFibAgentServiceHandler
    : public apache::thrift::ServiceHandler<openr::thrift::FibService> {
 public:
  explicit MockFibAgentServiceHandler()
      : eventBaseThread_("MockFibAgentThread") {}

  MOCK_METHOD(
      folly::coro::Task<apache::thrift::ServerStream<
          openr::thrift::StreamNextHopStatusResponse>>,
      co_streamNextHopStatus,
      (std::unique_ptr<openr::thrift::StreamNextHopStatusRequest>),
      (override));

  // Mock the sendNextHopStatus method
  MOCK_METHOD(
      void,
      sendNextHopStatus,
      (const folly::IPAddress&, const bool, const int32_t),
      ());

  // Method to create a stream response with the given nexthop status
  openr::thrift::StreamNextHopStatusResponse createNextHopStatusResponse(
      const folly::IPAddress& nexthop,
      bool isReachable,
      uint32_t igpCost) {
    openr::thrift::StreamNextHopStatusResponse response;

    openr::thrift::NextHopStatus status;
    status.isReachable() = isReachable;
    if (isReachable) {
      status.igpCost() = igpCost;
    }
    // Convert the nexthop to a binary string
    std::string binary;
    binary.assign(
        reinterpret_cast<const char*>(nexthop.bytes()), nexthop.byteCount());

    // Insert the entry directly into the response's nexthopStatuses map
    response.nexthopStatuses()->insert({binary, status});

    return response;
  }

  // Implementation of the sendNextHopStatus method
  void sendNextHopStatusImpl(
      const folly::IPAddress& nexthopIp,
      const bool isReachable,
      const int32_t igpCost) {
    XLOGF(
        INFO,
        "Sending nexthop status: ip={}, reachable={}, cost={}",
        nexthopIp.str(),
        isReachable,
        igpCost);

    // Create a response and store it in the responses vector
    openr::thrift::StreamNextHopStatusResponse response =
        createNextHopStatusResponse(nexthopIp, isReachable, igpCost);

    // Use a single lock for the entire operation to ensure thread safety
    std::lock_guard<std::mutex> lock(responsesMutex_);

    // Add the response to the vector
    responses_.push_back(response);

    // If we have a publisher, send the response
    if (publisher_) {
      for (const auto& storedResponse : responses_) {
        publisher_->next(storedResponse);
      }
      responses_.clear();
    }
  }

  // Method to start sending responses after receiving the request
  void startSendingResponses() {
    // Send any pending responses
    std::lock_guard<std::mutex> lock(responsesMutex_);
    if (publisher_) {
      for (const auto& storedResponse : responses_) {
        publisher_->next(storedResponse);
      }
      responses_.clear();
    }
  }

  folly::ScopedEventBaseThread& getEventBaseThread() {
    return eventBaseThread_;
  }

  // Set the publisher for the stream
  void setPublisher(
      apache::thrift::ServerStreamPublisher<
          openr::thrift::StreamNextHopStatusResponse> publisher) {
    std::lock_guard<std::mutex> lock(responsesMutex_);
    publisher_ = std::make_unique<apache::thrift::ServerStreamPublisher<
        openr::thrift::StreamNextHopStatusResponse>>(std::move(publisher));
  }

  // Reset the publisher (used to simulate stream errors)
  void resetPublisher() {
    std::lock_guard<std::mutex> lock(responsesMutex_);
    if (publisher_) {
      /*
       * Complete the stream normally and then reset the publisher
       * This will cause the stream to end and trigger reconnection
       */
      std::move(*publisher_).complete();
      publisher_.reset();
    }
  }

 private:
  folly::ScopedEventBaseThread eventBaseThread_;
  // Vector to store responses that will be sent once the stream is ready
  std::vector<openr::thrift::StreamNextHopStatusResponse> responses_;
  // Publisher for the stream
  std::unique_ptr<apache::thrift::ServerStreamPublisher<
      openr::thrift::StreamNextHopStatusResponse>>
      publisher_;
  mutable std::mutex responsesMutex_;
};

class NexthopHandlerTestFixture : public ::testing::Test {
 public:
  void SetUp() override {
    // Create a real NexthopCache
    nexthopCache_ = std::make_shared<NexthopCache>();

    // Create the mocked FIB agent service handler that runs in its own thread
    mockFibAgentHandler_ = std::make_shared<MockFibAgentServiceHandler>();

    // Create a server with our mocked FIB agent handler
    server_ = std::make_unique<apache::thrift::ScopedServerInterfaceThread>(
        mockFibAgentHandler_);

    // Initialize nexthopHandler_ with nexthopCache_ after setting the port
    nexthopHandler_ = std::make_unique<NexthopHandler>(
        nexthopCache_, ribInQ_, server_->getAddress().getPort());

    // Start the nexthopHandler in a thread AFTER setting the port
    nexthopHandlerThread_ = nexthopHandler_->runInThread();
  }

  void TearDown() override {
    // Make sure to stop the handler to avoid any hanging coroutines
    nexthopHandler_->stop();
    nexthopHandlerThread_.join();
    if (server_) {
      server_.reset();
    }
    mockFibAgentHandler_.reset();
  }

 protected:
  std::shared_ptr<NexthopCache> nexthopCache_;
  std::unique_ptr<NexthopHandler> nexthopHandler_;
  std::shared_ptr<MockFibAgentServiceHandler> mockFibAgentHandler_;
  std::unique_ptr<apache::thrift::ScopedServerInterfaceThread> server_;
  std::thread nexthopHandlerThread_;
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ_{
      nettools::bgplib::kMaxIngressQueueSize};
};

TEST_F(NexthopHandlerTestFixture, TestStreamNextHopStatus) {
  // Set up test nexthops
  std::vector<folly::IPAddress> nexthops = {
      folly::IPAddress("2620:0:1cff:dead:bef1:ffff:ffff:1"),
      folly::IPAddress("2620:0:1cff:dead:bef1:ffff:ffff:2"),
      folly::IPAddress("2620:0:1cff:dead:bef1:ffff:ffff:3")};
  bool isReachable = true;
  int32_t igpCost = 10;

  // Set up the mock to return a stream with a single response
  EXPECT_CALL(*mockFibAgentHandler_, co_streamNextHopStatus)
      .WillOnce(
          [this](std::unique_ptr<openr::thrift::StreamNextHopStatusRequest> req)
              -> folly::coro::Task<apache::thrift::ServerStream<
                  openr::thrift::StreamNextHopStatusResponse>> {
            // Create a stream that will yield responses
            auto [stream, publisher] = apache::thrift::ServerStream<
                openr::thrift::StreamNextHopStatusResponse>::createPublisher();

            // Store the publisher in the handler
            mockFibAgentHandler_->getEventBaseThread()
                .getEventBase()
                ->runInEventBaseThread([this,
                                        p = std::move(publisher)]() mutable {
                  // Store the publisher in the mock handler
                  mockFibAgentHandler_->setPublisher(std::move(p));

                  // Set up EXPECT_CALL for sendNextHopStatus
                  EXPECT_CALL(*mockFibAgentHandler_, sendNextHopStatus(_, _, _))
                      .WillRepeatedly(Invoke(
                          mockFibAgentHandler_.get(),
                          &MockFibAgentServiceHandler::sendNextHopStatusImpl));

                  // Start sending responses immediately
                  mockFibAgentHandler_->startSendingResponses();
                });

            co_return std::move(stream);
          });

  // Send nexthop status updates for each nexthop
  for (const auto& nexthop : nexthops) {
    mockFibAgentHandler_->sendNextHopStatusImpl(nexthop, isReachable, igpCost);
  }

  // Verify that the nexthop cache was updated with the status from the stream
  WITH_RETRIES_N(5, {
    for (const auto& nexthop : nexthops) {
      auto status = nexthopCache_->registerAndGetNexthopStatus({nexthop});

      EXPECT_EVENTUALLY_EQ(status.isReachable(), isReachable);

      if (!status.getIgpCost().has_value()) {
        EXPECT_EVENTUALLY_TRUE(false);
        continue;
      }

      EXPECT_EVENTUALLY_EQ(status.getIgpCost().value(), igpCost);
    }
  });
}

TEST_F(NexthopHandlerTestFixture, TestStreamNextHopStatusUpdate) {
  // Set up test nexthops with different statuses
  std::vector<folly::IPAddress> nexthops = {
      folly::IPAddress("2620:0:1cff:dead:bef1:ffff:ffff:1"),
      folly::IPAddress("2620:0:1cff:dead:bef1:ffff:ffff:2"),
      folly::IPAddress("2620:0:1cff:dead:bef1:ffff:ffff:3")};

  // Initial status: all nexthops are reachable with different costs
  std::vector<bool> initialReachability = {true, true, true};
  std::vector<int32_t> initialCosts = {10, 20, 30};

  // Updated status: some nexthops become unreachable
  std::vector<bool> updatedReachability = {false, true, false};

  /*
   * Note: costs are 0 for testing purposes, createNextHopStatusResponse() in
   * the teseFixture will not set igpCost if reachable is false, this is same
   * as FibAgentServiceHandler behavior
   */
  std::vector<int32_t> updatedCosts = {0, 15, 0};

  /*
   * Set up the mock to return a stream with multiple responses showing status
   * changes
   */
  EXPECT_CALL(*mockFibAgentHandler_, co_streamNextHopStatus)
      .WillOnce(
          [this](std::unique_ptr<openr::thrift::StreamNextHopStatusRequest> req)
              -> folly::coro::Task<apache::thrift::ServerStream<
                  openr::thrift::StreamNextHopStatusResponse>> {
            // Create a stream that will yield responses
            auto [stream, publisher] = apache::thrift::ServerStream<
                openr::thrift::StreamNextHopStatusResponse>::createPublisher();

            // Store the publisher in the handler
            mockFibAgentHandler_->getEventBaseThread()
                .getEventBase()
                ->runInEventBaseThread([this,
                                        p = std::move(publisher)]() mutable {
                  // Store the publisher in the mock handler
                  mockFibAgentHandler_->setPublisher(std::move(p));

                  // Set up EXPECT_CALL for sendNextHopStatus
                  EXPECT_CALL(*mockFibAgentHandler_, sendNextHopStatus(_, _, _))
                      .WillRepeatedly(Invoke(
                          mockFibAgentHandler_.get(),
                          &MockFibAgentServiceHandler::sendNextHopStatusImpl));

                  // Start sending responses only after receiving the request
                  mockFibAgentHandler_->startSendingResponses();
                });

            co_return std::move(stream);
          });

  // Send initial nexthop status updates
  for (size_t i = 0; i < nexthops.size(); i++) {
    mockFibAgentHandler_->sendNextHopStatusImpl(
        nexthops[i], initialReachability[i], initialCosts[i]);
  }

  // Wait for the nexthop cache to be updated with the initial status
  WITH_RETRIES_N(5, {
    for (size_t i = 0; i < nexthops.size(); i++) {
      auto status = nexthopCache_->registerAndGetNexthopStatus({nexthops[i]});

      EXPECT_EVENTUALLY_EQ(status.isReachable(), initialReachability[i]);

      if (!status.getIgpCost().has_value()) {
        EXPECT_EVENTUALLY_TRUE(false);
        continue;
      }

      EXPECT_EVENTUALLY_EQ(status.getIgpCost().value(), initialCosts[i]);
    }
  });

  // Send updated nexthop status
  for (size_t i = 0; i < nexthops.size(); i++) {
    mockFibAgentHandler_->sendNextHopStatusImpl(
        nexthops[i], updatedReachability[i], updatedCosts[i]);
  }

  // Wait for the nexthop cache to be updated with the final status
  WITH_RETRIES_N(5, {
    for (size_t i = 0; i < nexthops.size(); i++) {
      auto status = nexthopCache_->registerAndGetNexthopStatus({nexthops[i]});

      EXPECT_EVENTUALLY_EQ(status.isReachable(), updatedReachability[i]);

      if (updatedReachability[i]) {
        if (!status.getIgpCost().has_value()) {
          EXPECT_EVENTUALLY_TRUE(false);
          continue;
        }

        EXPECT_EVENTUALLY_EQ(status.getIgpCost().value(), updatedCosts[i]);
      } else {
        EXPECT_EVENTUALLY_FALSE(status.getIgpCost().has_value());
      }
    }
  });
}

TEST_F(NexthopHandlerTestFixture, TestDuplicateResponses) {
  // Set up test nexthop
  folly::IPAddress nexthop("2620:0:1cff:dead:bef1:ffff:ffff:1");
  bool isReachable = true;
  int32_t igpCost = 10;

  // Set up the mock to return a stream with duplicate responses
  EXPECT_CALL(*mockFibAgentHandler_, co_streamNextHopStatus)
      .WillOnce(
          [this](std::unique_ptr<openr::thrift::StreamNextHopStatusRequest> req)
              -> folly::coro::Task<apache::thrift::ServerStream<
                  openr::thrift::StreamNextHopStatusResponse>> {
            // Create a stream that will yield responses
            auto [stream, publisher] = apache::thrift::ServerStream<
                openr::thrift::StreamNextHopStatusResponse>::createPublisher();

            // Store the publisher in the handler
            mockFibAgentHandler_->getEventBaseThread()
                .getEventBase()
                ->runInEventBaseThread([this,
                                        p = std::move(publisher)]() mutable {
                  // Store the publisher in the mock handler
                  mockFibAgentHandler_->setPublisher(std::move(p));

                  // Set up EXPECT_CALL for sendNextHopStatus
                  EXPECT_CALL(*mockFibAgentHandler_, sendNextHopStatus(_, _, _))
                      .WillRepeatedly(Invoke(
                          mockFibAgentHandler_.get(),
                          &MockFibAgentServiceHandler::sendNextHopStatusImpl));

                  // Start sending responses immediately
                  mockFibAgentHandler_->startSendingResponses();
                });

            co_return std::move(stream);
          });

  // Send the same nexthop status update multiple times
  for (int i = 0; i < 3; i++) {
    mockFibAgentHandler_->sendNextHopStatusImpl(nexthop, isReachable, igpCost);
  }

  // Verify that the nexthop cache only contains one entry for the nexthop
  WITH_RETRIES_N(5, {
    // Check that the nexthop status is in the cache
    auto status = nexthopCache_->registerAndGetNexthopStatus({nexthop});
    EXPECT_EVENTUALLY_EQ(status.isReachable(), isReachable);

    if (!status.getIgpCost().has_value()) {
      EXPECT_EVENTUALLY_TRUE(false);
      continue;
    }
    EXPECT_EVENTUALLY_EQ(status.getIgpCost().value(), igpCost);
  });

  /*
   * Verify that sending duplicate entries doesn't create multiple entries
   * by checking that the nexthop has the expected values
   */

  // Try to add a different value for the same nexthop
  int32_t newIgpCost = 15;
  mockFibAgentHandler_->sendNextHopStatusImpl(nexthop, isReachable, newIgpCost);

  WITH_RETRIES_N(5, {
    // Verify that the entry was updated, not duplicated
    auto updatedStatus = nexthopCache_->registerAndGetNexthopStatus({nexthop});
    EXPECT_EVENTUALLY_EQ(updatedStatus.isReachable(), isReachable);

    if (!updatedStatus.getIgpCost().has_value()) {
      EXPECT_EVENTUALLY_TRUE(false);
      continue;
    }
    EXPECT_EVENTUALLY_EQ(updatedStatus.getIgpCost().value(), newIgpCost);
  });
  /*
   * Check that a different nexthop is created with default values when
   * registered
   */
  folly::IPAddress nonExistentNexhop("2620:0:1cff:dead:bef1:ffff:ffff:9999");
  WITH_RETRIES_N(5, {
    auto nonExistentStatus =
        nexthopCache_->registerAndGetNexthopStatus({nonExistentNexhop});
    /*
     * Since registerAndGetNexthopStatus now creates a new entry with default
     * values, we expect it to be unreachable with no IGP cost
     */
    EXPECT_EVENTUALLY_FALSE(nonExistentStatus.isReachable());
    EXPECT_EVENTUALLY_FALSE(nonExistentStatus.getIgpCost().has_value());
  });
}

TEST_F(NexthopHandlerTestFixture, TestStreamErrorOrEmptyResponseRecovery) {
  // Set up test nexthops
  std::vector<folly::IPAddress> nexthops = {
      folly::IPAddress("2620:0:1cff:dead:bef1:ffff:ffff:1"),
      folly::IPAddress("2620:0:1cff:dead:bef1:ffff:ffff:2")};
  bool isReachable = true;
  int32_t igpCost = 10;

  /*
   * Counter to track number of stream calls (must be static to ensure it
   * persists across lambda calls)
   */
  static std::atomic<int> streamCallCount{0};
  streamCallCount.store(0); // Reset for this test

  /*
   * Flag to track if we've simulated an error (must be static to ensure it
   * persists across lambda calls)
   */
  static std::atomic<bool> errorSimulated{false};
  errorSimulated.store(false); // Reset for this test

  // Set up the mock to throw an exception after sending initial data
  EXPECT_CALL(*mockFibAgentHandler_, co_streamNextHopStatus)
      .WillRepeatedly(
          [this](std::unique_ptr<openr::thrift::StreamNextHopStatusRequest> req)
              -> folly::coro::Task<apache::thrift::ServerStream<
                  openr::thrift::StreamNextHopStatusResponse>> {
            int currentCall = ++streamCallCount;
            XLOGF(INFO, "Stream call count: {}", currentCall);

            // Create a stream that will yield responses
            auto [stream, publisher] = apache::thrift::ServerStream<
                openr::thrift::StreamNextHopStatusResponse>::createPublisher();

            // Store the publisher in the handler
            mockFibAgentHandler_->getEventBaseThread()
                .getEventBase()
                ->runInEventBaseThread([this,
                                        p = std::move(publisher),
                                        currentCall]() mutable {
                  // Store the publisher in the mock handler
                  mockFibAgentHandler_->setPublisher(std::move(p));

                  // Set up EXPECT_CALL for sendNextHopStatus
                  EXPECT_CALL(*mockFibAgentHandler_, sendNextHopStatus(_, _, _))
                      .WillRepeatedly(Invoke(
                          mockFibAgentHandler_.get(),
                          &MockFibAgentServiceHandler::sendNextHopStatusImpl));

                  // Start sending responses immediately
                  mockFibAgentHandler_->startSendingResponses();

                  /*
                   * If this is the first call, throw an exception after a delay
                   * to simulate a stream error
                   */
                  if (currentCall == 1) {
                    mockFibAgentHandler_->getEventBaseThread()
                        .getEventBase()
                        ->runAfterDelay(
                            [this]() {
                              /*
                               * Simulate stream error by closing the publisher
                               * This will cause the stream to end and trigger
                               * the error handling in NexthopHandler
                               */
                              XLOG(INFO, "Simulating stream error");
                              mockFibAgentHandler_->resetPublisher();
                              errorSimulated.store(true);
                            },
                            50); // 50ms delay
                  }
                });

            co_return std::move(stream);
          });

  // Send initial nexthop status updates
  for (const auto& nexthop : nexthops) {
    mockFibAgentHandler_->sendNextHopStatusImpl(nexthop, isReachable, igpCost);
  }

  /*
   * Wait for the initial updates to be processed and verify they're in the
   * cache
   */
  WITH_RETRIES_N(10, {
    for (const auto& nexthop : nexthops) {
      auto status = nexthopCache_->registerAndGetNexthopStatus({nexthop});
      EXPECT_EVENTUALLY_EQ(status.isReachable(), isReachable);
    }
  });

  /*
   * Wait for the stream error to occur and the client to reconnect
   * The NexthopHandler should reconnect after kOpenrFibSubscribeTimeout (100ms)
   */

  // First wait for the error to be simulated
  WITH_RETRIES_N(20, { EXPECT_EVENTUALLY_TRUE(errorSimulated.load()); });

  // Then wait for reconnection to happen
  WITH_RETRIES_N(20, { EXPECT_EVENTUALLY_GT(streamCallCount.load(), 1); });

  // Send new nexthop status updates with different cost
  int32_t newIgpCost = 20;
  for (const auto& nexthop : nexthops) {
    mockFibAgentHandler_->sendNextHopStatusImpl(
        nexthop, isReachable, newIgpCost);
  }

  // Verify that the cache gets updated with the new data after reconnection
  WITH_RETRIES_N(10, {
    for (const auto& nexthop : nexthops) {
      auto status = nexthopCache_->registerAndGetNexthopStatus({nexthop});

      EXPECT_EVENTUALLY_EQ(status.isReachable(), isReachable);

      if (!status.getIgpCost().has_value()) {
        EXPECT_EVENTUALLY_TRUE(false);
        continue;
      }

      EXPECT_EVENTUALLY_EQ(status.getIgpCost().value(), newIgpCost);
    }
  });
}

CO_TEST_F(
    NexthopHandlerTestFixture,
    TestUpdateCacheAndNotifyRibPushesToRibInQ) {
  // Set up mock so the background stream routine doesn't fail unexpectedly
  EXPECT_CALL(*mockFibAgentHandler_, co_streamNextHopStatus)
      .WillRepeatedly(
          [this](auto) -> folly::coro::Task<apache::thrift::ServerStream<
                           openr::thrift::StreamNextHopStatusResponse>> {
            auto [stream, publisher] = apache::thrift::ServerStream<
                openr::thrift::StreamNextHopStatusResponse>::createPublisher();
            mockFibAgentHandler_->setPublisher(std::move(publisher));
            co_return std::move(stream);
          });

  const folly::IPAddress first("2620:0:1cff:dead:bef1:ffff:ffff:1");
  const folly::IPAddress second("2620:0:1cff:dead:bef1:ffff:ffff:2");
  const folly::IPAddress unregistered("2620:0:1cff:dead:bef1:ffff:ffff:3");
  nexthopCache_->registerAndGetNexthopStatus({first});
  nexthopCache_->registerAndGetNexthopStatus({second});
  const std::vector<NexthopStatus> updates{
      NexthopStatus({.ip = first}, /*isReachable=*/true, 10u),
      NexthopStatus({.ip = second}, /*isReachable=*/true, 20u),
      NexthopStatus({.ip = unregistered}, /*isReachable=*/true, 30u)};
  CO_ASSERT_EQ(0, ribInQ_.size());
  co_await nexthopHandler_->co_updateCacheAndNotifyRib(updates);

  CO_ASSERT_EQ(1, ribInQ_.size());
  const auto message = co_await ribInQ_.pop();
  const auto* update = std::get_if<RibInNexthopUpdate>(&message);
  CO_ASSERT_NE(nullptr, update);
  CO_ASSERT_EQ(2, update->nexthopStatuses.size());
  std::vector<folly::IPAddress> addresses;
  for (const auto& status : update->nexthopStatuses) {
    addresses.push_back(status.getNexthop());
    EXPECT_EQ(fboss::kDefaultVrf, status.getNexthopKey().nextHopVrfId);
    EXPECT_TRUE(status.isReachable());
    EXPECT_GT(status.getEpoch(), 0);
  }
  EXPECT_THAT(addresses, UnorderedElementsAre(first, second));
  const auto cached = nexthopCache_->getNexthopStatus({unregistered});
  CO_ASSERT_TRUE(cached.has_value());
  EXPECT_TRUE(cached->isReachable());
  EXPECT_EQ(30u, cached->getIgpCost());
  EXPECT_FALSE(nexthopCache_->isRegistered({unregistered}));

  co_await nexthopHandler_->co_updateCacheAndNotifyRib(updates);
  EXPECT_TRUE(ribInQ_.empty());
  nexthopCache_->unregisterAndRemoveNexthopStatus({first});
  co_await nexthopHandler_->co_updateCacheAndNotifyRib(
      {NexthopStatus({.ip = first}, /*isReachable=*/false),
       NexthopStatus({.ip = second}, /*isReachable=*/false)});
  CO_ASSERT_EQ(1, ribInQ_.size());
  const auto withdrawalMessage = co_await ribInQ_.pop();
  const auto* withdrawal = std::get_if<RibInNexthopUpdate>(&withdrawalMessage);
  CO_ASSERT_NE(nullptr, withdrawal);
  CO_ASSERT_EQ(1, withdrawal->nexthopStatuses.size());
  EXPECT_EQ(second, withdrawal->nexthopStatuses.front().getNexthop());
  EXPECT_EQ(
      (NexthopKey{.ip = second}),
      withdrawal->nexthopStatuses.front().getNexthopKey());
  EXPECT_FALSE(withdrawal->nexthopStatuses.front().isReachable());
  EXPECT_FALSE(nexthopCache_->getNexthopStatus({first})->isReachable());
}

} // namespace facebook::bgp
