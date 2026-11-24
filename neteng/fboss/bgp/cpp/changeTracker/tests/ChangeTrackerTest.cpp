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

#include <chrono>

#include <fmt/format.h>
#include <folly/io/async/EventBase.h>

#include "neteng/fboss/bgp/cpp/changeTracker/ChangeTracker.h"
#include "neteng/fboss/bgp/cpp/changeTracker/ChangeTrackerDebug.h"
#include "neteng/fboss/bgp/cpp/changeTracker/Consumer.h"
#include "neteng/fboss/bgp/cpp/changeTracker/TrackableObject.h"

/**
 * Run the event loop until a condition is satisfied or a timeout occurs.
 *
 * @param evb The event base to run
 * @param condition The condition to check
 * @param timeoutMs The timeout in milliseconds
 * @param timeoutMsg The message to display if a timeout occurs
 * @return True if the condition was satisfied, false if a timeout occurred
 */
template <typename Condition>
bool runUntil(
    folly::EventBase& evb,
    Condition&& condition,
    std::chrono::milliseconds timeoutMs = std::chrono::seconds(5),
    const char* timeoutMsg = "Timeout waiting for condition") {
  auto startTime = std::chrono::steady_clock::now();
  while (!condition()) {
    evb.loopOnce(EVLOOP_NONBLOCK);
    if (std::chrono::steady_clock::now() - startTime > timeoutMs) {
      ADD_FAILURE() << timeoutMsg;
      return false;
    }
  }
  return true;
}

namespace {

// Simple test class to use with TrackableObject
class TestObject {
 public:
  explicit TestObject(int value) : value_(value) {}

  int getValue() const {
    return value_;
  }

  void setValue(int value) {
    value_ = value;
  }

 private:
  int value_;
};

// Custom Consumer class for testing
class TestConsumer : public Consumer<TestObject> {
 public:
  explicit TestConsumer(ChangeTracker<TestObject>& tracker, std::string name)
      : Consumer<TestObject>(tracker, static_cast<size_t>(-1)),
        name_(std::move(name)),
        processedItems_() {
    // Set up the callback for processing change items
    setProcessChangeItemCallback([this](ChangeItem<TestObject>* item) {
      // Get the typed object directly
      TestObject& testObject = item->getTypedObject();
      int value = testObject.getValue();

      /*
       * Pend on specific values if requested
       * NOTE: when we pend the contract is before processing the item
       */
      if (pendOnValues_.find(value) != pendOnValues_.end()) {
        pendedOnItem_ = item;
        return ProcessResult::YIELD;
      }

      // Record that we processed this item
      processedItems_.push_back(value);

      return ProcessResult::CONTINUE;
    });
  }

  // Set values to pend on
  void setPendOnValues(std::unordered_set<int> values) {
    pendOnValues_ = std::move(values);
  }

  // Get the processed items
  const std::vector<int>& getProcessedItems() const {
    return processedItems_;
  }

  // Get the item we're pended on
  ChangeItem<TestObject>* getPendedOnItem() const {
    return pendedOnItem_;
  }

  void backdateMarkerAdvanceTime(std::chrono::milliseconds offset) {
    lastMarkerAdvanceTime_ -= offset;
  }

 private:
  std::string name_;
  std::vector<int> processedItems_;
  std::unordered_set<int> pendOnValues_;
  ChangeItem<TestObject>* pendedOnItem_ = nullptr;
};

// Test producer class for creating and publishing objects
class TestProducer {
 public:
  explicit TestProducer(ChangeTracker<TestObject>& tracker)
      : tracker_(tracker) {}

  // Create a new trackable object
  TrackableObject<TestObject>* createObject(int value) {
    auto trackableObject =
        std::make_unique<TrackableObject<TestObject>>(TestObject(value));
    auto* ptr = trackableObject.get();
    objects_.push_back(std::move(trackableObject));
    return ptr;
  }

  // Publish a change for an object
  void publishChange(TrackableObject<TestObject>* object) {
    tracker_.publishChange(object);
  }

  // Update an object's value and publish the change
  void updateAndPublish(TrackableObject<TestObject>* object, int newValue) {
    object->get().setValue(newValue);
    tracker_.publishChange(object);
  }

  // Update an object's value and publish the change with event base push
  void updateAndPublishPush(
      folly::EventBase& evb,
      TrackableObject<TestObject>* object,
      int newValue) {
    object->get().setValue(newValue);
    tracker_.publishChange(object);
    evb.loopOnce(EVLOOP_NONBLOCK);
  }

 private:
  ChangeTracker<TestObject>& tracker_;
  std::vector<std::unique_ptr<TrackableObject<TestObject>>> objects_;
};

// Helper functions for setting up consumers in different modes
enum class ConsumerMode { TRIGGERED, POLLED };

void setupConsumerMode(
    std::shared_ptr<TestConsumer> consumer,
    ConsumerMode mode) {
  if (mode == ConsumerMode::POLLED) {
    consumer->setPolledMode();
  } else {
    consumer->setTriggeredMode();
  }
}

// Helper to trigger consumption for polled mode
void triggerPolledConsumption(
    folly::EventBase& evb,
    std::shared_ptr<TestConsumer> consumer) {
  auto timerCallback = [&consumer, &evb]() noexcept {
    co_withExecutor(
        &evb, folly::coro::co_invoke([consumer]() -> folly::coro::Task<void> {
          co_await consumer->consumeChanges();
        }))
        .start();
  };

  auto timer = folly::AsyncTimeout::schedule(
      std::chrono::milliseconds(50), evb, timerCallback);
}

// Common test function for basic consumer processing
void testBasicConsumerProcessing(ConsumerMode mode) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("TestTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create some objects
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  // Create a consumer
  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer1");

  // Register the consumer and set mode
  consumer->registerWithTracker();
  setupConsumerMode(consumer, mode);

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start the consumer coroutine
  auto future =
      co_withExecutor(
          &evb, folly::coro::co_invoke([consumer]() -> folly::coro::Task<void> {
            co_await consumer->consumeChanges();
          }))
          .start();

  // Publish changes
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  // Trigger polled consumption if needed
  if (mode == ConsumerMode::POLLED) {
    triggerPolledConsumption(evb, consumer);
  }

  // Run the event loop until the consumer processes all items
  bool success = runUntil(
      evb,
      [&]() { return consumer->getProcessedItems().size() >= 3; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer to process all items");
  ASSERT_TRUE(success);

  // Verify the consumer processed all items in the correct order
  const auto& processedItems = consumer->getProcessedItems();
  ASSERT_EQ(processedItems.size(), 3);
  EXPECT_EQ(processedItems[0], 1);
  EXPECT_EQ(processedItems[1], 2);
  EXPECT_EQ(processedItems[2], 3);

  // Signal the consumer to terminate
  consumer->terminate();

  // Deregister the consumer
  consumer->deregisterFromTracker();

  // Cancel the future
  future.cancel();

  evb.loop();
}

// Common test function for publish with no consumers
void testPublishChangeWithNoConsumers(ConsumerMode) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("TestTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create an object
  auto* object = producer.createObject(42);

  // Publish a change without any registered consumers
  producer.publishChange(object);

  // Verify that the change list is empty
  EXPECT_TRUE(tracker.getChangeList().empty());
}

// Common test function for change processed callbacks
void testChangeProcessedCallbacks(ConsumerMode mode) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("TestTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create an object
  auto* object = producer.createObject(42);

  // Create a consumer
  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer1");

  // Register the consumer and set mode
  consumer->registerWithTracker();
  setupConsumerMode(consumer, mode);

  // Flag to track if the callback was called
  bool callbackCalled = false;

  // Set a global callback for the tracker
  tracker.setGlobalOnChangeProcessedCallback(
      [&callbackCalled](TrackableObject<TestObject>*) {
        callbackCalled = true;
      });

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start the consumer coroutine
  auto future =
      co_withExecutor(
          &evb, folly::coro::co_invoke([consumer]() -> folly::coro::Task<void> {
            co_await consumer->consumeChanges();
          }))
          .start();

  // Publish a change
  producer.publishChange(object);

  // Trigger polled consumption if needed
  if (mode == ConsumerMode::POLLED) {
    triggerPolledConsumption(evb, consumer);
  }

  // Run the event loop until the consumer processes the item
  bool success = runUntil(
      evb,
      [&]() { return consumer->getProcessedItems().size() >= 1; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer to process the item");
  ASSERT_TRUE(success);

  // Run the event loop until the callback is called
  success = runUntil(
      evb,
      [&]() { return callbackCalled; },
      std::chrono::seconds(5),
      "Timeout waiting for callback to be called");
  ASSERT_TRUE(success);

  // Verify the callback was called
  EXPECT_TRUE(callbackCalled);

  // Verify the consumer processed the item
  const auto& processedItems = consumer->getProcessedItems();
  ASSERT_EQ(processedItems.size(), 1);
  EXPECT_EQ(processedItems[0], 42);

  // Signal the consumer to terminate
  consumer->terminate();

  // Deregister the consumer
  consumer->deregisterFromTracker();

  // Cancel the future
  future.cancel();

  // Run the event loop to process any pending events
  evb.loop();
}

// Common test function for global callbacks with multiple objects
void testGlobalCallbacksMultiObjects(ConsumerMode mode) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("TestTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create some objects
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  // Create a consumer
  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer1");

  // Register the consumer and set mode
  consumer->registerWithTracker();
  setupConsumerMode(consumer, mode);

  // Track which objects were processed by the callback
  std::vector<int> callbackObjects;

  // Set a global callback for the tracker that records the object values
  tracker.setGlobalOnChangeProcessedCallback(
      [&callbackObjects](TrackableObject<TestObject>*) {
        // Add values to match expected values
        callbackObjects.push_back(
            static_cast<int>(callbackObjects.size()) +
            1); // Add 1, 2, 3 to match expected values
      });

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start the consumer coroutine
  auto future =
      co_withExecutor(
          &evb, folly::coro::co_invoke([consumer]() -> folly::coro::Task<void> {
            co_await consumer->consumeChanges();
          }))
          .start();

  // Publish changes
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  // Trigger polled consumption if needed
  if (mode == ConsumerMode::POLLED) {
    triggerPolledConsumption(evb, consumer);
  }

  // Run the event loop until the consumer processes all items
  bool success = runUntil(
      evb,
      [&]() { return consumer->getProcessedItems().size() >= 3; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer to process all items");
  ASSERT_TRUE(success);

  // Run the event loop until all callbacks are called
  success = runUntil(
      evb,
      [&]() { return callbackObjects.size() >= 3; },
      std::chrono::seconds(5),
      "Timeout waiting for all callbacks to be called");
  ASSERT_TRUE(success);

  // Verify the callback was called for each object
  ASSERT_EQ(callbackObjects.size(), 3);

  // The callback should be called in the order the objects are processed
  EXPECT_EQ(callbackObjects[0], 1);
  EXPECT_EQ(callbackObjects[1], 2);
  EXPECT_EQ(callbackObjects[2], 3);

  // Signal the consumer to terminate
  consumer->terminate();

  // Deregister the consumer
  consumer->deregisterFromTracker();

  // Cancel the future
  future.cancel();

  // Run the event loop to process any pending events
  evb.loop();
}

} // namespace

/*
 * Test that a consumer processes objects published by a producer - Triggered
 * Mode
 */
TEST(ChangeTrackerTest, BasicConsumerProcessing_Triggered) {
  testBasicConsumerProcessing(ConsumerMode::TRIGGERED);
}

// Test that a consumer processes objects published by a producer - Polled Mode
TEST(ChangeTrackerTest, BasicConsumerProcessing_Polled) {
  testBasicConsumerProcessing(ConsumerMode::POLLED);
}

/*
 * Test that publishChange with no consumers doesn't add to the change list -
 * Triggered Mode
 */
TEST(ChangeTrackerTest, PublishChangeWithNoConsumers_Triggered) {
  testPublishChangeWithNoConsumers(ConsumerMode::TRIGGERED);
}

/*
 * Test that publishChange with no consumers doesn't add to the change list -
 * Polled Mode
 */
TEST(ChangeTrackerTest, PublishChangeWithNoConsumers_Polled) {
  testPublishChangeWithNoConsumers(ConsumerMode::POLLED);
}

// Test that a consumer can pend on an object and then resume processing
TEST(ChangeTrackerTest, PendAndResumeConsumer) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("TestTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create some objects
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  // Create a consumer that will pend on object with value 2
  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer1");
  consumer->setPendOnValues({2});

  // Register the consumer
  consumer->registerWithTracker();

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start the consumer coroutine
  auto future =
      co_withExecutor(
          &evb, folly::coro::co_invoke([consumer]() -> folly::coro::Task<void> {
            co_await consumer->consumeChanges();
          }))
          .start();

  // Publish changes
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  // Run the event loop until the consumer pends on object2
  bool success = runUntil(
      evb,
      [&]() { return consumer->getPendedOnItem() != nullptr; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer to pend on object2");
  ASSERT_TRUE(success);

  // Verify the consumer processed items 1 and 2, but pended on 2
  const auto& processedItems = consumer->getProcessedItems();
  ASSERT_EQ(processedItems.size(), 1);
  EXPECT_EQ(processedItems[0], 1);

  // Verify the consumer is pended on object2
  auto* pendedItem = consumer->getPendedOnItem();
  ASSERT_NE(pendedItem, nullptr);
  EXPECT_EQ(pendedItem->getTypedObject().getValue(), 2);

  // reset the pend value
  consumer->setPendOnValues({});

  // Resume the consumer
  consumer->resume();

  // Run the event loop until the consumer processes item 3
  success = runUntil(
      evb,
      [&]() { return consumer->getProcessedItems().size() >= 3; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer to process item 3");
  ASSERT_TRUE(success);

  // Verify the consumer processed item 2 and 3
  ASSERT_EQ(consumer->getProcessedItems().size(), 3);
  EXPECT_EQ(consumer->getProcessedItems()[1], 2);
  EXPECT_EQ(consumer->getProcessedItems()[2], 3);

  // Signal the consumer to terminate
  consumer->terminate();

  // Deregister the consumer
  consumer->deregisterFromTracker();

  // Cancel the future
  future.cancel();

  // Run the event loop to process any pending events
  evb.loop();
}

// Test that a consumer processes an updated object after resuming from pend
TEST(ChangeTrackerTest, UpdatedObjectAfterPend) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("TestTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create some objects
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  // Create a consumer that will pend on object with value 2
  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer1");
  consumer->setPendOnValues({2});

  // Register the consumer
  consumer->registerWithTracker();

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start the consumer coroutine
  auto future =
      co_withExecutor(
          &evb, folly::coro::co_invoke([consumer]() -> folly::coro::Task<void> {
            co_await consumer->consumeChanges();
          }))
          .start();

  // Publish changes
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  // Run the event loop until the consumer pends on object2
  bool success = runUntil(
      evb,
      [&]() { return consumer->getPendedOnItem() != nullptr; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer to pend on object2");
  ASSERT_TRUE(success);

  // Verify the consumer processed items 1  but pended on 2
  const auto& processedItems = consumer->getProcessedItems();
  ASSERT_EQ(processedItems.size(), 1);
  EXPECT_EQ(processedItems[0], 1);

  // Update object2 while the consumer is pended on it
  producer.updateAndPublish(object2, 22);

  // reset the pend value
  consumer->setPendOnValues({});
  // Resume the consumer
  consumer->resume();

  /*
   * Run the event loop until the consumer processes the updated object2 and
   * object3
   */
  success = runUntil(
      evb,
      [&]() { return consumer->getProcessedItems().size() >= 3; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer to process updated object2 and object3");
  ASSERT_TRUE(success);

  /*
   * Verify the consumer processed the updated object2 (value 22) and object3
   * Notice how the object updated went to the end.
   */
  ASSERT_GE(consumer->getProcessedItems().size(), 3);
  EXPECT_EQ(consumer->getProcessedItems()[1], 3);
  EXPECT_EQ(consumer->getProcessedItems()[2], 22);

  // Signal the consumer to terminate
  consumer->terminate();

  // Deregister the consumer
  consumer->deregisterFromTracker();

  // Cancel the future
  future.cancel();

  // Run the event loop to process any pending events
  evb.loop();
}

/*
 * Test that multiple consumers with different processing speeds can process the
 * same objects
 */
TEST(ChangeTrackerTest, ConsumersWithDifferentSpeeds) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("TestTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create some objects
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  // Create consumers with different pend behaviors
  auto consumer1 = std::make_shared<TestConsumer>(tracker, "FastConsumer");
  auto consumer2 = std::make_shared<TestConsumer>(tracker, "SlowConsumer");
  consumer2->setPendOnValues({2}); // Consumer2 will pend on object with value 2

  // Register the consumers
  consumer1->registerWithTracker();
  consumer2->registerWithTracker();

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start the consumer coroutines
  auto future1 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer1]() -> folly::coro::Task<void> {
            co_await consumer1->consumeChanges();
          }))
          .start();

  auto future2 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer2]() -> folly::coro::Task<void> {
            co_await consumer2->consumeChanges();
          }))
          .start();

  // Publish changes
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  // Run the event loop until consumer1 processes all items
  bool success = runUntil(
      evb,
      [&]() { return consumer1->getProcessedItems().size() >= 3; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer1 to process all items");
  ASSERT_TRUE(success);

  // Run the event loop until consumer2 pends on object2
  success = runUntil(
      evb,
      [&]() { return consumer2->getPendedOnItem() != nullptr; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer2 to pend on object2");
  ASSERT_TRUE(success);

  // Verify consumer1 processed all items
  const auto& processedItems1 = consumer1->getProcessedItems();
  ASSERT_EQ(processedItems1.size(), 3);
  EXPECT_EQ(processedItems1[0], 1);
  EXPECT_EQ(processedItems1[1], 2);
  EXPECT_EQ(processedItems1[2], 3);

  // Verify consumer2 processed items 1 and 2, but pended on 2
  auto& processedItems2 = consumer2->getProcessedItems();
  ASSERT_EQ(processedItems2.size(), 1);
  EXPECT_EQ(processedItems2[0], 1);

  consumer2->setPendOnValues({}); // reset the pend value
  // Resume consumer2
  consumer2->resume();

  // Run the event loop until consumer2 processes item 3
  success = runUntil(
      evb,
      [&]() { return consumer2->getProcessedItems().size() >= 3; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer2 to process item 3");
  ASSERT_TRUE(success);

  // Verify consumer2 processed item 2 and 3
  ASSERT_EQ(consumer2->getProcessedItems().size(), 3);
  EXPECT_EQ(consumer2->getProcessedItems()[0], 1);
  EXPECT_EQ(consumer2->getProcessedItems()[1], 2);
  EXPECT_EQ(consumer2->getProcessedItems()[2], 3);

  // Signal the consumers to terminate
  consumer1->terminate();
  consumer2->terminate();

  // Deregister the consumers
  consumer1->deregisterFromTracker();
  consumer2->deregisterFromTracker();

  // Cancel the futures
  future1.cancel();
  future2.cancel();

  // Run the event loop to process any pending events
  evb.loop();
}

/*
 * Test that callbacks are invoked when changes are fully processed - Triggered
 * Mode
 */
TEST(ChangeTrackerTest, ChangeProcessedCallbacks_Triggered) {
  testChangeProcessedCallbacks(ConsumerMode::TRIGGERED);
}

/*
 * Test that callbacks are invoked when changes are fully processed - Polled
 * Mode
 */
TEST(ChangeTrackerTest, ChangeProcessedCallbacks_Polled) {
  testChangeProcessedCallbacks(ConsumerMode::POLLED);
}

// Test that global callbacks are called for multiple objects - Triggered Mode
TEST(ChangeTrackerTest, GlobalCallbacksMultiObjects_Triggered) {
  testGlobalCallbacksMultiObjects(ConsumerMode::TRIGGERED);
}

// Test that global callbacks are called for multiple objects - Polled Mode
TEST(ChangeTrackerTest, GlobalCallbacksMultiObjects_Polled) {
  testGlobalCallbacksMultiObjects(ConsumerMode::POLLED);
}

// Test that objects can be updated while consumers are pended on them
TEST(ChangeTrackerTest, UpdateObjectsWhilePended) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("TestTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create some objects
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  // Create consumers with different pend behaviors
  auto consumer1 = std::make_shared<TestConsumer>(tracker, "Consumer1");
  consumer1->setPendOnValues({1}); // Consumer1 will pend on object with value 1

  auto consumer2 = std::make_shared<TestConsumer>(tracker, "Consumer2");
  consumer2->setPendOnValues({2}); // Consumer2 will pend on object with value 2

  auto consumer3 = std::make_shared<TestConsumer>(tracker, "Consumer3");
  consumer3->setPendOnValues({3}); // Consumer3 will pend on object with value 3

  auto consumer4 = std::make_shared<TestConsumer>(tracker, "Consumer4");
  // Consumer4 doesn't pend on any values

  // Register the consumers
  consumer1->registerWithTracker();
  consumer2->registerWithTracker();
  consumer3->registerWithTracker();
  consumer4->registerWithTracker();

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start the consumer coroutines
  auto future1 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer1]() -> folly::coro::Task<void> {
            co_await consumer1->consumeChanges();
          }))
          .start();

  auto future2 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer2]() -> folly::coro::Task<void> {
            co_await consumer2->consumeChanges();
          }))
          .start();

  auto future3 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer3]() -> folly::coro::Task<void> {
            co_await consumer3->consumeChanges();
          }))
          .start();

  auto future4 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer4]() -> folly::coro::Task<void> {
            co_await consumer4->consumeChanges();
          }))
          .start();

  // Publish changes
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  // Run the event loop until consumer1 pends on object1
  bool success = runUntil(
      evb,
      [&]() { return consumer1->getPendedOnItem() != nullptr; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer1 to pend on object1");
  ASSERT_TRUE(success);

  // Run the event loop until consumer2 pends on object2
  success = runUntil(
      evb,
      [&]() { return consumer2->getPendedOnItem() != nullptr; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer2 to pend on object2");
  ASSERT_TRUE(success);

  // Run the event loop until consumer3 pends on object3
  success = runUntil(
      evb,
      [&]() { return consumer3->getPendedOnItem() != nullptr; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer3 to pend on object3");
  ASSERT_TRUE(success);

  // Run the event loop until consumer4 processes all items
  success = runUntil(
      evb,
      [&]() { return consumer4->getProcessedItems().size() >= 3; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer4 to process all items");
  ASSERT_TRUE(success);

  /*
   * Verify the initial state
   * Consumer1 should have pended on object1 without processing it
   */
  ASSERT_EQ(consumer1->getProcessedItems().size(), 0);
  EXPECT_EQ(consumer1->getPendedOnItem()->getTypedObject().getValue(), 1);

  // Consumer2 should have processed object1 and pended on object2
  ASSERT_EQ(consumer2->getProcessedItems().size(), 1);
  EXPECT_EQ(consumer2->getProcessedItems()[0], 1);
  EXPECT_EQ(consumer2->getPendedOnItem()->getTypedObject().getValue(), 2);

  // Consumer3 should have processed objects 1 and 2, and pended on object3ƒ
  ASSERT_EQ(consumer3->getProcessedItems().size(), 2);
  EXPECT_EQ(consumer3->getProcessedItems()[0], 1);
  EXPECT_EQ(consumer3->getProcessedItems()[1], 2);
  EXPECT_EQ(consumer3->getPendedOnItem()->getTypedObject().getValue(), 3);

  // Consumer4 should have processed all objects
  ASSERT_EQ(consumer4->getProcessedItems().size(), 3);
  EXPECT_EQ(consumer4->getProcessedItems()[0], 1);
  EXPECT_EQ(consumer4->getProcessedItems()[1], 2);
  EXPECT_EQ(consumer4->getProcessedItems()[2], 3);

  /*
   * Now update each object - this will move them to the end of the list
   * The order is important: we update object1, then object2, then object3
   */
  producer.updateAndPublish(object1, 11);
  producer.updateAndPublish(object2, 22);
  producer.updateAndPublish(object3, 33);

  // Reset pend values for all consumers
  consumer1->setPendOnValues({});
  consumer2->setPendOnValues({});
  consumer3->setPendOnValues({});

  /*
   * Resume all consumers - this is needed because they're already pended
   * and publishing new objects won't automatically resume them
   */
  consumer1->resume();
  consumer2->resume();
  consumer3->resume();

  // Run the event loop until all consumers process the updated objects
  success = runUntil(
      evb,
      [&]() {
        return consumer1->getProcessedItems().size() >= 3 &&
            consumer2->getProcessedItems().size() >= 4 &&
            consumer3->getProcessedItems().size() >= 5 &&
            consumer4->getProcessedItems().size() >= 6;
      },
      std::chrono::seconds(5),
      "Timeout waiting for consumers to process updated objects");
  ASSERT_TRUE(success);

  // Verify that consumer1 processed the updated objects
  ASSERT_GE(consumer1->getProcessedItems().size(), 3);
  EXPECT_EQ(consumer1->getProcessedItems()[0], 11);
  EXPECT_EQ(consumer1->getProcessedItems()[1], 22);
  EXPECT_EQ(consumer1->getProcessedItems()[2], 33);

  // Verify that consumer2 processed the updated objects
  ASSERT_GE(consumer2->getProcessedItems().size(), 4);
  EXPECT_EQ(consumer2->getProcessedItems()[1], 11);
  EXPECT_EQ(consumer2->getProcessedItems()[2], 22);
  EXPECT_EQ(consumer2->getProcessedItems()[3], 33);

  // Verify that consumer3 processed the updated objects
  ASSERT_GE(consumer3->getProcessedItems().size(), 5);
  EXPECT_EQ(consumer3->getProcessedItems()[2], 11);
  EXPECT_EQ(consumer3->getProcessedItems()[3], 22);
  EXPECT_EQ(consumer3->getProcessedItems()[4], 33);

  // Verify that consumer4 processed the updated objects
  ASSERT_GE(consumer4->getProcessedItems().size(), 6);
  EXPECT_EQ(consumer4->getProcessedItems()[3], 11);
  EXPECT_EQ(consumer4->getProcessedItems()[4], 22);
  EXPECT_EQ(consumer4->getProcessedItems()[5], 33);

  // Signal the consumers to terminate
  consumer1->terminate();
  consumer2->terminate();
  consumer3->terminate();
  consumer4->terminate();

  // Deregister the consumers
  consumer1->deregisterFromTracker();
  consumer2->deregisterFromTracker();
  consumer3->deregisterFromTracker();
  consumer4->deregisterFromTracker();

  // Cancel the futures
  future1.cancel();
  future2.cancel();
  future3.cancel();
  future4.cancel();

  // Run the event loop to process any pending events
  evb.loop();
}

// Test that objects can be updated multiple times while consumers are pended
TEST(ChangeTrackerTest, MultipleUpdatesWhilePended) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("TestTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create some objects
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  // Create consumers with different pend behaviors
  auto consumer1 = std::make_shared<TestConsumer>(tracker, "Consumer1");
  consumer1->setPendOnValues({1}); // Consumer1 will pend on object with value 1

  auto consumer2 = std::make_shared<TestConsumer>(tracker, "Consumer2");
  consumer2->setPendOnValues({2}); // Consumer2 will pend on object with value 2

  auto consumer3 = std::make_shared<TestConsumer>(tracker, "Consumer3");
  consumer3->setPendOnValues({3}); // Consumer3 will pend on object with value 3

  auto consumer4 = std::make_shared<TestConsumer>(tracker, "Consumer4");
  // Consumer4 doesn't pend on any values

  // Register the consumers
  consumer1->registerWithTracker();
  consumer2->registerWithTracker();
  consumer3->registerWithTracker();
  consumer4->registerWithTracker();

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start the consumer coroutines
  auto future1 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer1]() -> folly::coro::Task<void> {
            co_await consumer1->consumeChanges();
          }))
          .start();

  auto future2 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer2]() -> folly::coro::Task<void> {
            co_await consumer2->consumeChanges();
          }))
          .start();

  auto future3 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer3]() -> folly::coro::Task<void> {
            co_await consumer3->consumeChanges();
          }))
          .start();

  auto future4 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer4]() -> folly::coro::Task<void> {
            co_await consumer4->consumeChanges();
          }))
          .start();

  // Publish changes
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  // Run the event loop until consumer1 pends on object1
  bool success = runUntil(
      evb,
      [&]() { return consumer1->getPendedOnItem() != nullptr; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer1 to pend on object1");
  ASSERT_TRUE(success);

  // Run the event loop until consumer2 pends on object2
  success = runUntil(
      evb,
      [&]() { return consumer2->getPendedOnItem() != nullptr; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer2 to pend on object2");
  ASSERT_TRUE(success);

  // Run the event loop until consumer3 pends on object3
  success = runUntil(
      evb,
      [&]() { return consumer3->getPendedOnItem() != nullptr; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer3 to pend on object3");
  ASSERT_TRUE(success);

  // Run the event loop until consumer4 processes all items
  success = runUntil(
      evb,
      [&]() { return consumer4->getProcessedItems().size() >= 3; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer4 to process all items");
  ASSERT_TRUE(success);

  /*
   * Verify the initial state
   * Consumer1 should have pended on object1 without processing it
   */
  ASSERT_EQ(consumer1->getProcessedItems().size(), 0);
  EXPECT_EQ(consumer1->getPendedOnItem()->getTypedObject().getValue(), 1);

  // Consumer2 should have processed object1 and pended on object2
  ASSERT_EQ(consumer2->getProcessedItems().size(), 1);
  EXPECT_EQ(consumer2->getProcessedItems()[0], 1);
  EXPECT_EQ(consumer2->getPendedOnItem()->getTypedObject().getValue(), 2);

  // Consumer3 should have processed objects 1 and 2, and pended on object3
  ASSERT_EQ(consumer3->getProcessedItems().size(), 2);
  EXPECT_EQ(consumer3->getProcessedItems()[0], 1);
  EXPECT_EQ(consumer3->getProcessedItems()[1], 2);
  EXPECT_EQ(consumer3->getPendedOnItem()->getTypedObject().getValue(), 3);

  // Consumer4 should have processed all objects
  ASSERT_EQ(consumer4->getProcessedItems().size(), 3);
  EXPECT_EQ(consumer4->getProcessedItems()[0], 1);
  EXPECT_EQ(consumer4->getProcessedItems()[1], 2);
  EXPECT_EQ(consumer4->getProcessedItems()[2], 3);

  /*
   * Now make multiple updates to each object while consumers are pended
   * object1 goes through 2 changes
   */
  producer.updateAndPublishPush(evb, object1, 10);
  producer.updateAndPublishPush(evb, object1, 11);

  // object2 goes through 3 changes
  producer.updateAndPublishPush(evb, object2, 20);
  producer.updateAndPublishPush(evb, object2, 21);
  producer.updateAndPublishPush(evb, object2, 22);

  // object3 goes through 4 changes
  producer.updateAndPublishPush(evb, object3, 30);
  producer.updateAndPublishPush(evb, object3, 31);
  producer.updateAndPublishPush(evb, object3, 32);
  producer.updateAndPublishPush(evb, object3, 33);

  // Reset pend values for all consumers
  consumer1->setPendOnValues({});
  consumer2->setPendOnValues({});
  consumer3->setPendOnValues({});

  /*
   * Resume all consumers - this is needed because they're already pended
   * and publishing new objects won't automatically resume them
   */
  consumer1->resume();
  consumer2->resume();
  consumer3->resume();

  // Run the event loop until all consumers process the updated objects
  success = runUntil(
      evb,
      [&]() {
        return consumer1->getProcessedItems().size() >= 3 &&
            consumer2->getProcessedItems().size() >= 4 &&
            consumer3->getProcessedItems().size() >= 5 &&
            consumer4->getProcessedItems().size() >= 6;
      },
      std::chrono::seconds(5),
      "Timeout waiting for consumers to process updated objects");
  ASSERT_TRUE(success);

  // Verify that consumer1 processed only the final state of each object
  ASSERT_GE(consumer1->getProcessedItems().size(), 3);
  EXPECT_EQ(consumer1->getProcessedItems()[0], 11); // Final state of object1
  EXPECT_EQ(consumer1->getProcessedItems()[1], 22); // Final state of object2
  EXPECT_EQ(consumer1->getProcessedItems()[2], 33); // Final state of object3

  // Verify that consumer2 processed only the final state of each object
  ASSERT_GE(consumer2->getProcessedItems().size(), 4);
  EXPECT_EQ(consumer2->getProcessedItems()[1], 11); // Final state of object1
  EXPECT_EQ(consumer2->getProcessedItems()[2], 22); // Final state of object2
  EXPECT_EQ(consumer2->getProcessedItems()[3], 33); // Final state of object3

  // Verify that consumer3 processed only the final state of each object
  ASSERT_GE(consumer3->getProcessedItems().size(), 5);
  EXPECT_EQ(consumer3->getProcessedItems()[2], 11); // Final state of object1
  EXPECT_EQ(consumer3->getProcessedItems()[3], 22); // Final state of object2
  EXPECT_EQ(consumer3->getProcessedItems()[4], 33); // Final state of object3

  // Verify that consumer4 processed all states of each object
  ASSERT_GE(consumer4->getProcessedItems().size(), 6);
  // Initial states
  EXPECT_EQ(consumer4->getProcessedItems()[0], 1);
  EXPECT_EQ(consumer4->getProcessedItems()[1], 2);
  EXPECT_EQ(consumer4->getProcessedItems()[2], 3);
  // Updates to object1
  EXPECT_EQ(consumer4->getProcessedItems()[3], 10);
  EXPECT_EQ(consumer4->getProcessedItems()[4], 11);
  // Updates to object2
  EXPECT_EQ(consumer4->getProcessedItems()[5], 20);
  EXPECT_EQ(consumer4->getProcessedItems()[6], 21);
  EXPECT_EQ(consumer4->getProcessedItems()[7], 22);
  // Updates to object3
  EXPECT_EQ(consumer4->getProcessedItems()[8], 30);
  EXPECT_EQ(consumer4->getProcessedItems()[9], 31);
  EXPECT_EQ(consumer4->getProcessedItems()[10], 32);
  EXPECT_EQ(consumer4->getProcessedItems()[11], 33);

  // Signal the consumers to terminate
  consumer1->terminate();
  consumer2->terminate();
  consumer3->terminate();
  consumer4->terminate();

  // Deregister the consumers
  consumer1->deregisterFromTracker();
  consumer2->deregisterFromTracker();
  consumer3->deregisterFromTracker();
  consumer4->deregisterFromTracker();

  // Cancel the futures
  future1.cancel();
  future2.cancel();
  future3.cancel();
  future4.cancel();

  // Run the event loop to process any pending events
  evb.loop();
}

// Test that a new consumer can be registered while other consumers are pended
TEST(ChangeTrackerTest, NewConsumerWithPendedOthers) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("TestTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create some objects
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  // Create consumers with different pend behaviors
  auto consumer1 = std::make_shared<TestConsumer>(tracker, "Consumer1");
  consumer1->setPendOnValues({1}); // Consumer1 will pend on object with value 1

  auto consumer2 = std::make_shared<TestConsumer>(tracker, "Consumer2");
  consumer2->setPendOnValues({2}); // Consumer2 will pend on object with value 2

  auto consumer3 = std::make_shared<TestConsumer>(tracker, "Consumer3");
  // Consumer3 doesn't pend on any values (just ready)

  // Register the consumers
  consumer1->registerWithTracker();
  consumer2->registerWithTracker();
  consumer3->registerWithTracker();

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start the consumer coroutines
  auto future1 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer1]() -> folly::coro::Task<void> {
            co_await consumer1->consumeChanges();
          }))
          .start();

  auto future2 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer2]() -> folly::coro::Task<void> {
            co_await consumer2->consumeChanges();
          }))
          .start();

  auto future3 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer3]() -> folly::coro::Task<void> {
            co_await consumer3->consumeChanges();
          }))
          .start();

  // Publish changes
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  // Run the event loop until consumer1 pends on object1
  bool success = runUntil(
      evb,
      [&]() { return consumer1->getPendedOnItem() != nullptr; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer1 to pend on object1");
  ASSERT_TRUE(success);

  // Run the event loop until consumer2 pends on object2
  success = runUntil(
      evb,
      [&]() { return consumer2->getPendedOnItem() != nullptr; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer2 to pend on object2");
  ASSERT_TRUE(success);

  // Run the event loop until consumer3 processes all items (not pended)
  success = runUntil(
      evb,
      [&]() { return consumer3->getProcessedItems().size() >= 3; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer3 to process all items");
  ASSERT_TRUE(success);

  /*
   * Verify the initial state
   * Consumer1 should have pended on object1 without processing it
   */
  ASSERT_EQ(consumer1->getProcessedItems().size(), 0);
  EXPECT_EQ(consumer1->getPendedOnItem()->getTypedObject().getValue(), 1);

  // Consumer2 should have processed object1 and pended on object2
  ASSERT_EQ(consumer2->getProcessedItems().size(), 1);
  EXPECT_EQ(consumer2->getProcessedItems()[0], 1);
  EXPECT_EQ(consumer2->getPendedOnItem()->getTypedObject().getValue(), 2);

  // Consumer3 should have processed all objects
  ASSERT_EQ(consumer3->getProcessedItems().size(), 3);
  EXPECT_EQ(consumer3->getProcessedItems()[0], 1);
  EXPECT_EQ(consumer3->getProcessedItems()[1], 2);
  EXPECT_EQ(consumer3->getProcessedItems()[2], 3);

  // Now create and register a brand new consumer
  auto newConsumer = std::make_shared<TestConsumer>(tracker, "NewConsumer");
  newConsumer->registerWithTracker();

  // Start the new consumer coroutine
  auto newFuture =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([newConsumer]() -> folly::coro::Task<void> {
            co_await newConsumer->consumeChanges();
          }))
          .start();

  /*
   * The new consumer should not have processed any items yet
   * since it was registered after the initial objects were published
   */
  ASSERT_EQ(newConsumer->getProcessedItems().size(), 0);

  // Create a new object and update existing ones
  auto* object4 = producer.createObject(4);
  producer.updateAndPublish(object1, 11);
  producer.updateAndPublish(object2, 22);

  // Publish the new object
  producer.publishChange(object4);

  /*
   * Run the event loop until the new consumer processes the new object and
   * updates
   */
  success = runUntil(
      evb,
      [&]() { return newConsumer->getProcessedItems().size() >= 3; },
      std::chrono::seconds(5),
      "Timeout waiting for new consumer to process new object and updates");
  ASSERT_TRUE(success);

  /*
   * Verify the new consumer processed ONLY the new object and updates
   * that happened after it was registered
   */
  ASSERT_EQ(newConsumer->getProcessedItems().size(), 3);
  EXPECT_EQ(newConsumer->getProcessedItems()[0], 11); // Updated object1
  EXPECT_EQ(newConsumer->getProcessedItems()[1], 22); // Updated object2
  EXPECT_EQ(newConsumer->getProcessedItems()[2], 4); // New object4

  // Reset pend values for all consumers
  consumer1->setPendOnValues({});
  consumer2->setPendOnValues({});

  // Resume pended consumers
  consumer1->resume();
  consumer2->resume();

  // Run the event loop until all consumers process all objects
  success = runUntil(
      evb,
      [&]() {
        return consumer1->getProcessedItems().size() >= 4 &&
            consumer2->getProcessedItems().size() >= 4 &&
            consumer3->getProcessedItems().size() >= 6;
      },
      std::chrono::seconds(5),
      "Timeout waiting for all consumers to process all objects");
  ASSERT_TRUE(success);

  /*
   * Verify that all consumers processed all objects
   * Consumer1 (was pended on object1)
   */
  ASSERT_GE(consumer1->getProcessedItems().size(), 4);
  EXPECT_EQ(consumer1->getProcessedItems()[1], 11); // Updated object1
  EXPECT_EQ(consumer1->getProcessedItems()[2], 22); // Original object2
  EXPECT_EQ(consumer1->getProcessedItems()[3], 4); // New object4

  // Consumer2 (was pended on object2)
  ASSERT_GE(consumer2->getProcessedItems().size(), 5);
  EXPECT_EQ(consumer2->getProcessedItems()[2], 11); // Updated object1
  EXPECT_EQ(consumer2->getProcessedItems()[3], 22); // Updated object2
  EXPECT_EQ(consumer2->getProcessedItems()[4], 4); // New object4

  // Consumer3 (was not pended)
  ASSERT_GE(consumer3->getProcessedItems().size(), 6);
  EXPECT_EQ(consumer3->getProcessedItems()[3], 11); // Updated object1
  EXPECT_EQ(consumer3->getProcessedItems()[4], 22); // Updated object2
  EXPECT_EQ(consumer3->getProcessedItems()[5], 4); //  New object4

  // Signal all consumers to terminate
  consumer1->terminate();
  consumer2->terminate();
  consumer3->terminate();
  newConsumer->terminate();

  // Deregister all consumers
  consumer1->deregisterFromTracker();
  consumer2->deregisterFromTracker();
  consumer3->deregisterFromTracker();
  newConsumer->deregisterFromTracker();

  // Cancel all futures
  future1.cancel();
  future2.cancel();
  future3.cancel();
  newFuture.cancel();

  // Run the event loop to process any pending events
  evb.loop();
}

// Test that a deregistered consumer does not receive new objects
TEST(ChangeTrackerTest, DeregisteredConsumerNoNewObjects) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("TestTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create some objects
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  // Create multiple consumers
  auto consumer1 = std::make_shared<TestConsumer>(tracker, "Consumer1");
  auto consumer2 = std::make_shared<TestConsumer>(tracker, "Consumer2");

  // Register the consumers
  consumer1->registerWithTracker();
  consumer2->registerWithTracker();

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start the consumer coroutines
  auto future1 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer1]() -> folly::coro::Task<void> {
            co_await consumer1->consumeChanges();
          }))
          .start();

  auto future2 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer2]() -> folly::coro::Task<void> {
            co_await consumer2->consumeChanges();
          }))
          .start();

  // Publish initial objects
  producer.publishChange(object1);
  producer.publishChange(object2);

  // Run the event loop until both consumers process the initial objects
  bool success = runUntil(
      evb,
      [&]() {
        return consumer1->getProcessedItems().size() >= 2 &&
            consumer2->getProcessedItems().size() >= 2;
      },
      std::chrono::seconds(5),
      "Timeout waiting for consumers to process initial objects");
  ASSERT_TRUE(success);

  // Verify both consumers processed the initial objects
  ASSERT_EQ(consumer1->getProcessedItems().size(), 2);
  EXPECT_EQ(consumer1->getProcessedItems()[0], 1);
  EXPECT_EQ(consumer1->getProcessedItems()[1], 2);

  ASSERT_EQ(consumer2->getProcessedItems().size(), 2);
  EXPECT_EQ(consumer2->getProcessedItems()[0], 1);
  EXPECT_EQ(consumer2->getProcessedItems()[1], 2);

  // Deregister consumer1
  consumer1->deregisterFromTracker();

  // Publish another object
  producer.publishChange(object3);

  // Run the event loop until consumer2 processes the new object
  success = runUntil(
      evb,
      [&]() { return consumer2->getProcessedItems().size() >= 3; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer2 to process the new object");
  ASSERT_TRUE(success);

  // Verify consumer1 did not receive the new object (still has only 2 items)
  ASSERT_EQ(consumer1->getProcessedItems().size(), 2);

  // Verify consumer2 received the new object
  ASSERT_EQ(consumer2->getProcessedItems().size(), 3);
  EXPECT_EQ(consumer2->getProcessedItems()[2], 3);

  // Create a new object and publish it
  auto* object4 = producer.createObject(4);
  producer.publishChange(object4);

  // Run the event loop until consumer2 processes the newest object
  success = runUntil(
      evb,
      [&]() { return consumer2->getProcessedItems().size() >= 4; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer2 to process the newest object");
  ASSERT_TRUE(success);

  // Verify consumer1 still has only 2 items
  ASSERT_EQ(consumer1->getProcessedItems().size(), 2);

  // Verify consumer2 received the newest object
  ASSERT_EQ(consumer2->getProcessedItems().size(), 4);
  EXPECT_EQ(consumer2->getProcessedItems()[3], 4);

  // Verify the change list is empty (all items have been processed)
  EXPECT_TRUE(tracker.getChangeList().empty());

  // Signal the consumers to terminate
  consumer1->terminate();
  consumer2->terminate();

  // Deregister consumer2
  consumer2->deregisterFromTracker();

  // Cancel the futures
  future1.cancel();
  future2.cancel();

  // Run the event loop to process any pending events
  evb.loop();
}

// Test that multiple consumers can pend on the same objects
TEST(ChangeTrackerTest, MultipleConsumersPendOnSameObjects) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("TestTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create 5 objects
  constexpr int numObjects = 5;
  std::vector<TrackableObject<TestObject>*> objects;
  objects.reserve(numObjects);

  for (int i = 0; i < numObjects; i++) {
    objects.push_back(producer.createObject(i));
  }

  // Create 10 consumers (2 consumers will pend on each object)
  constexpr int numConsumers = 10;
  std::vector<std::shared_ptr<TestConsumer>> consumers;
  consumers.reserve(numConsumers);

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  /*
   * Create and register all consumers
   * Consumers 0,1 pend on object 0
   * Consumers 2,3 pend on object 1
   * Consumers 4,5 pend on object 2
   * Consumers 6,7 pend on object 3
   * Consumers 8,9 pend on object 4
   */
  for (int i = 0; i < numConsumers; i++) {
    auto consumer =
        std::make_shared<TestConsumer>(tracker, "Consumer" + std::to_string(i));

    /*
     * Set the value to pend on - each pair of consumers pends on the same
     * object
     */
    int objectToPendOn = i / 2;
    std::unordered_set<int> pendValues = {objectToPendOn};
    consumer->setPendOnValues(pendValues);

    consumer->registerWithTracker();
    consumers.push_back(consumer);
  }

  // Start all consumer coroutines
  std::vector<folly::SemiFuture<folly::Unit>> futures;
  futures.reserve(numConsumers);

  for (auto& consumer : consumers) {
    futures.push_back(
        co_withExecutor(
            &evb,
            folly::coro::co_invoke([consumer]() -> folly::coro::Task<void> {
              co_await consumer->consumeChanges();
            }))
            .start());
  }

  // Publish all objects
  for (auto* object : objects) {
    producer.publishChange(object);
  }

  // Run the event loop until all consumers pend on their respective objects
  bool success = runUntil(
      evb,
      [&]() {
        for (int i = 0; i < numConsumers; i++) {
          if (consumers[i]->getPendedOnItem() == nullptr) {
            return false;
          }
        }
        return true;
      },
      std::chrono::seconds(5),
      "Timeout waiting for consumers to pend on their objects");

  ASSERT_TRUE(success);

  // Verify that each consumer has pended on the correct object
  for (int i = 0; i < numConsumers; i++) {
    int expectedObjectValue = i / 2;
    ASSERT_NE(consumers[i]->getPendedOnItem(), nullptr);
    EXPECT_EQ(
        consumers[i]->getPendedOnItem()->getTypedObject().getValue(),
        expectedObjectValue);

    /*
     * Each consumer should have processed objects with values less than their
     * pend value
     */
    const auto& processedItems = consumers[i]->getProcessedItems();
    for (size_t j = 0; j < processedItems.size(); j++) {
      EXPECT_LT(processedItems[j], expectedObjectValue);
    }
  }

  // Reset pend values for all consumers and resume them
  for (auto& consumer : consumers) {
    consumer->setPendOnValues({});
    consumer->resume();
  }

  // Run the event loop until all consumers process all objects
  success = runUntil(
      evb,
      [&]() {
        for (int i = 0; i < numConsumers; i++) {
          if (consumers[i]->getProcessedItems().size() < numObjects) {
            return false;
          }
        }
        return true;
      },
      std::chrono::seconds(5),
      "Timeout waiting for consumers to process all objects after resuming");

  ASSERT_TRUE(success);

  // Verify that all consumers processed all objects
  for (int i = 0; i < numConsumers; i++) {
    ASSERT_EQ(consumers[i]->getProcessedItems().size(), numObjects);

    // Check that all objects were processed
    std::unordered_set<int> processedValues;
    for (int val : consumers[i]->getProcessedItems()) {
      processedValues.insert(val);
    }

    for (int j = 0; j < numObjects; j++) {
      EXPECT_TRUE(processedValues.find(j) != processedValues.end());
    }
  }

  // Signal all consumers to terminate and deregister them
  for (auto& consumer : consumers) {
    consumer->terminate();
    consumer->deregisterFromTracker();
  }

  // Cancel all futures
  for (auto& future : futures) {
    future.cancel();
  }

  // Run the event loop to process any pending events
  evb.loop();
}

/*
 * Test with maximum consumers (4096) and about 100,000 objects
 * Each consumer pends on one object, then resumes and receives all objects
 * Disabled due to stack-use-after-scope issues in the debugger
 */
TEST(ChangeTrackerTest, DISABLED_MaxConsumersAndObjects) {
  // Create a change tracker

  ChangeTracker<TestObject> tracker("MaxTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create 100,000 objects
  constexpr int numObjects = 100000;

  // Create objects vector on the heap using unique_ptr
  auto objects = std::make_unique<std::vector<TrackableObject<TestObject>*>>();
  objects->reserve(numObjects);

  for (int i = 0; i < numObjects; i++) {
    objects->push_back(producer.createObject(i));
  }

  // Create 4096 consumers (maximum)
  constexpr int maxConsumers = 4096;

  // Create consumers vector on the heap using unique_ptr
  auto consumers =
      std::make_unique<std::vector<std::shared_ptr<TestConsumer>>>();
  consumers->reserve(maxConsumers);

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  /*
   * Create and register all consumers
   * Each consumer will pend on a specific object (consumer i pends on object i
   * % numObjects)
   */
  for (int i = 0; i < maxConsumers; i++) {
    auto consumer =
        std::make_shared<TestConsumer>(tracker, "Consumer" + std::to_string(i));

    // Set the value to pend on - each consumer pends on a different object
    std::unordered_set<int> pendValues = {i % numObjects};
    consumer->setPendOnValues(pendValues);

    consumer->registerWithTracker();
    consumers->push_back(consumer);
  }

  // Start all consumer coroutines
  auto futures =
      std::make_unique<std::vector<folly::SemiFuture<folly::Unit>>>();
  futures->reserve(maxConsumers);

  // Start all consumer coroutines first
  for (auto& consumer : *consumers) {
    futures->push_back(
        co_withExecutor(
            &evb,
            folly::coro::co_invoke([consumer]() -> folly::coro::Task<void> {
              co_await consumer->consumeChanges();
            }))
            .start());
  }

  // Then publish all objects
  for (auto* object : *objects) {
    producer.publishChange(object);
  }

  /*
   * Sample a few consumers to check progress (checking all would be too
   * expensive)
   */
  const int sampleIndices[] = {
      1, 2, maxConsumers / 2, maxConsumers - 2, maxConsumers - 1};
  const int numSamples = sizeof(sampleIndices) / sizeof(sampleIndices[0]);

  // Run the event loop until sampled consumers pend on their respective objects
  bool success = runUntil(
      evb,
      [consumers = consumers.get(), sampleIndices]() {
        for (int i = 0; i < numSamples; i++) {
          int idx = sampleIndices[i];
          if ((*consumers)[idx]->getPendedOnItem() == nullptr) {
            return false;
          }
        }
        return true;
      },
      std::chrono::seconds(30), // Longer timeout due to large number of objects
      "Timeout waiting for consumers to pend on their objects");

  ASSERT_TRUE(success);

  // Verify that sampled consumers have pended on their respective objects
  for (int i = 0; i < numSamples; i++) {
    int idx = sampleIndices[i];
    ASSERT_NE((*consumers)[idx]->getPendedOnItem(), nullptr);
    EXPECT_EQ(
        (*consumers)[idx]->getPendedOnItem()->getTypedObject().getValue(),
        idx % numObjects);

    /*
     * Each consumer should have processed some objects before pending
     * The exact number depends on when they encountered their pend object
     */
    EXPECT_GT((*consumers)[idx]->getProcessedItems().size(), 0);
  }

  // Reset pend values for all consumers and resume them
  for (auto& consumer : *consumers) {
    consumer->setPendOnValues({});
    consumer->resume();
  }

  // Run the event loop until sampled consumers process all objects
  success = runUntil(
      evb,
      [consumers = consumers.get(), sampleIndices]() {
        for (int i = 0; i < numSamples; i++) {
          int idx = sampleIndices[i];
          if ((*consumers)[idx]->getProcessedItems().size() < numObjects) {
            return false;
          }
        }
        return true;
      },
      std::chrono::seconds(
          60), // Longer timeout due to very large number of objects
      "Timeout waiting for consumers to process all objects after resuming");

  ASSERT_TRUE(success);

  // Verify that sampled consumers processed all objects
  for (int i = 0; i < numSamples; i++) {
    int idx = sampleIndices[i];
    ASSERT_EQ((*consumers)[idx]->getProcessedItems().size(), numObjects);

    /*
     * Check a few sample values to ensure they were processed correctly
     * Note: The order might be different due to pending, but all objects should
     * be there
     */
    std::unordered_set<int> processedValues;
    for (int val : (*consumers)[idx]->getProcessedItems()) {
      processedValues.insert(val);
    }

    // Check that key values are present
    EXPECT_TRUE(processedValues.find(0) != processedValues.end());
    EXPECT_TRUE(processedValues.find(numObjects / 2) != processedValues.end());
    EXPECT_TRUE(processedValues.find(numObjects - 1) != processedValues.end());
    EXPECT_TRUE(
        processedValues.find(idx % numObjects) != processedValues.end());
  }

  // Signal all consumers to terminate and deregister them
  for (auto& consumer : *consumers) {
    consumer->terminate();
    consumer->deregisterFromTracker();
  }

  // Cancel all futures
  for (auto& future : *futures) {
    future.cancel();
  }

  // Run the event loop to process any pending events
  evb.loop();
}

/*
 * Test timer-driven polled mode with yield behavior (similar to out-delay timer
 * pattern)
 */
TEST(ChangeTrackerTest, TimerDrivenPolledMode) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("TimerDrivenTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create some objects
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);
  auto* object4 = producer.createObject(4);
  auto* object5 = producer.createObject(5);

  // Create an event base for scheduling coroutines and timers
  folly::EventBase evb;

  // Create a test consumer in polled mode that will yield on object 3
  auto consumer = std::make_shared<TestConsumer>(tracker, "TimerConsumer");
  consumer->setPendOnValues({3}); // Will yield on object with value 3
  consumer->registerWithTracker();
  consumer->setPolledMode();

  // Verify the consumer is in polled mode
  EXPECT_EQ(
      consumer->getConsumptionMode(),
      Consumer<TestObject>::ConsumptionMode::POLLED);

  // Publish changes to the tracker
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);
  producer.publishChange(object4);
  producer.publishChange(object5);

  /*
   * Simple timer callback that drives polled consumption (like
   * programOutDelayTimer)
   */
  auto timerCallback = [&consumer, &evb]() noexcept {
    // Start a polled cycle to consume available changes
    co_withExecutor(
        &evb, folly::coro::co_invoke([consumer]() -> folly::coro::Task<void> {
          co_await consumer->consumeChanges();
        }))
        .start();
  };

  /*
   * Schedule first timer cycle using folly::AsyncTimeout::schedule (like
   * out-delay timer)
   */
  auto timer1 = folly::AsyncTimeout::schedule(
      std::chrono::milliseconds(100), // 100ms delay
      evb,
      timerCallback);

  // Run the event loop until consumer yields on object 3
  bool success = runUntil(
      evb,
      [&]() { return consumer->getPendedOnItem() != nullptr; },
      std::chrono::seconds(5),
      "Timeout waiting for timer-driven consumer to yield on object 3");
  ASSERT_TRUE(success);

  // Verify the consumer processed objects 1 and 2, then yielded on object 3
  const auto& processedItems = consumer->getProcessedItems();
  ASSERT_EQ(processedItems.size(), 2);
  EXPECT_EQ(processedItems[0], 1);
  EXPECT_EQ(processedItems[1], 2);

  /*
   * Verify the consumer is added to pending list for object 3 (polled mode
   * behavior)
   */
  ASSERT_NE(consumer->getPendedOnItem(), nullptr);
  EXPECT_EQ(consumer->getPendedOnItem()->getTypedObject().getValue(), 3);

  // Clear the yield condition for next timer cycle
  consumer->setPendOnValues({});

  // Schedule second timer cycle - should process remaining objects
  auto timer2 = folly::AsyncTimeout::schedule(
      std::chrono::milliseconds(100), evb, timerCallback);

  // Run the event loop until consumer processes remaining items
  success = runUntil(
      evb,
      [&]() { return consumer->getProcessedItems().size() >= 5; },
      std::chrono::seconds(5),
      "Timeout waiting for timer-driven consumer to process remaining items");
  ASSERT_TRUE(success);

  // Verify the consumer processed all items
  ASSERT_EQ(consumer->getProcessedItems().size(), 5);
  EXPECT_EQ(consumer->getProcessedItems()[0], 1);
  EXPECT_EQ(consumer->getProcessedItems()[1], 2);
  EXPECT_EQ(consumer->getProcessedItems()[2], 3);
  EXPECT_EQ(consumer->getProcessedItems()[3], 4);
  EXPECT_EQ(consumer->getProcessedItems()[4], 5);

  /*
   * Test demonstrates timer-driven polled consumption pattern with yield:
   * 1. Timer drives periodic consumption cycles
   * 2. Consumer processes available items until yield condition
   * 3. On yield: adds to pending list and returns (no suspension)
   * 4. Next timer cycle continues from pending item
   * 5. Consumer processes remaining items and exits when none available
   */

  // Stop polled mode
  consumer->setTriggeredMode();

  // Signal the consumer to terminate
  consumer->terminate();

  // Deregister the consumer
  consumer->deregisterFromTracker();

  // Run the event loop to process any pending events
  evb.loop();
}

// Test that re-registering the same consumer causes heap use after free
TEST(ChangeTrackerTest, ReregisterConsumer) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("TestTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create objects
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);

  // Create a consumer
  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer1");

  // Register the consumer
  consumer->registerWithTracker();

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start the consumer coroutine
  auto future =
      co_withExecutor(
          &evb, folly::coro::co_invoke([consumer]() -> folly::coro::Task<void> {
            co_await consumer->consumeChanges();
          }))
          .start();

  // Publish changes
  producer.publishChange(object1);

  // Run the event loop until the consumer processes the item
  bool success = runUntil(
      evb,
      [&]() { return consumer->getProcessedItems().size() >= 1; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer to process item");
  ASSERT_TRUE(success);

  // Verify the consumer processed the item
  const auto& processedItems = consumer->getProcessedItems();
  ASSERT_EQ(processedItems.size(), 1);
  EXPECT_EQ(processedItems[0], 1);

  // Signal the consumer to terminate
  consumer->terminate();

  // Deregister the consumer
  consumer->deregisterFromTracker();

  // Cancel the future
  future.cancel();

  evb.loop();

  // Re-register the same consumer - test the deregister/register cycle
  consumer->registerWithTracker();

  // Start the consumer coroutine again
  auto future2 =
      co_withExecutor(
          &evb, folly::coro::co_invoke([consumer]() -> folly::coro::Task<void> {
            co_await consumer->consumeChanges();
          }))
          .start();

  // Publish another change
  producer.publishChange(object2);

  // Run the event loop until the consumer processes the new item
  success = runUntil(
      evb,
      [&]() { return consumer->getProcessedItems().size() >= 2; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer to process second item");
  ASSERT_TRUE(success);

  // Verify the consumer processed the second item
  ASSERT_EQ(consumer->getProcessedItems().size(), 2);
  EXPECT_EQ(consumer->getProcessedItems()[1], 2);

  // Signal the consumer to terminate
  consumer->terminate();

  // Deregister the consumer
  consumer->deregisterFromTracker();

  // Cancel the future
  future2.cancel();

  evb.loop();
}

// Test stress registration/deregistration with continuous object publishing
TEST(ChangeTrackerTest, StressRegistrationWithContinuousPublishing) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("StressTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Create 3 consumers that will be reused throughout all cycles
  constexpr int numConsumers = 1;
  constexpr int numCycles = 2;

  /*
   * Create persistent consumer objects that will be reused (IMPORTANT: keep
   * same objects)
   */
  std::vector<std::shared_ptr<TestConsumer>> consumers;
  consumers.reserve(numConsumers);
  for (int i = 0; i < numConsumers; i++) {
    consumers.push_back(
        std::make_shared<TestConsumer>(
            tracker, "PersistentConsumer" + std::to_string(i)));
  }

  int objectCounter = 0;

  // Publish objects AFTER consumer registration
  std::vector<int> postRegistrationValues;

  // Cycle through registration/deregistration 12 times
  for (int cycle = 0; cycle < numCycles; cycle++) {
    // Publish some objects before consumer registration
    std::vector<int> preRegistrationValues;
    for (int preObj = 0; preObj < 2; preObj++) {
      auto* preObject = producer.createObject(objectCounter++);
      producer.publishChange(preObject);
      preRegistrationValues.push_back(preObject->get().getValue());
    }

    /*
     * Register the SAME consumer objects for this cycle (no new objects
     * created)
     */
    std::vector<folly::SemiFuture<folly::Unit>> futures;
    for (int i = 0; i < numConsumers; i++) {
      // Register the existing consumer (same object throughout all cycles)
      consumers[i]->registerWithTracker();

      // Start the consumer coroutine
      futures.push_back(
          co_withExecutor(
              &evb,
              folly::coro::co_invoke(
                  [consumer = consumers[i]]() -> folly::coro::Task<void> {
                    co_await consumer->consumeChanges();
                  }))
              .start());
    }

    for (int postObj = 0; postObj < 2; postObj++) {
      auto* postObject = producer.createObject(objectCounter++);
      producer.publishChange(postObject);
      postRegistrationValues.push_back(postObject->get().getValue());
    }

    /*
     * Run the event loop until all consumers process the post-registration
     * objects
     */
    bool success = runUntil(
        evb,
        [&]() {
          for (const auto& consumer : consumers) {
            if (consumer->getProcessedItems().size() <
                postRegistrationValues.size()) {
              return false;
            }
          }
          return true;
        },
        std::chrono::seconds(180),
        ("Timeout in cycle " + std::to_string(cycle)).c_str());

    ASSERT_TRUE(success);

    // Verify each consumer only processed objects published AFTER registration
    for (int i = 0; i < numConsumers; i++) {
      const auto& processedItems = consumers[i]->getProcessedItems();

      // Convert to set for easier lookup
      std::unordered_set<int> processedSet(
          processedItems.begin(), processedItems.end());

      // Verify consumer processed post-registration objects
      for (int expectedValue : postRegistrationValues) {
        EXPECT_TRUE(processedSet.find(expectedValue) != processedSet.end())
            << "Consumer " << i << " in cycle " << cycle
            << " did not process post-registration object " << expectedValue;
      }

      /*
       * Verify consumer did NOT process pre-registration objects from this
       * cycle
       */
      for (int preRegValue : preRegistrationValues) {
        EXPECT_TRUE(processedSet.find(preRegValue) == processedSet.end())
            << "Consumer " << i << " in cycle " << cycle
            << " incorrectly processed pre-registration object " << preRegValue;
      }
    }

    /*
     * Terminate and deregister all consumers (IMPORTANT: keep same objects
     * around)
     */
    for (int i = 0; i < numConsumers; i++) {
      consumers[i]->terminate();
      consumers[i]->deregisterFromTracker();
      futures[i].cancel();
    }

    // Run the event loop to process any pending events
    evb.loop();
  }

  // Verify the change list is empty after all cycles
  EXPECT_TRUE(tracker.getChangeList().empty())
      << "Change list should be empty after all cycles";
}

/*
 * Test that the callback receives the correct TrackableObject and can access
 * its data
 */
TEST(ChangeTrackerTest, CallbackReceivesTrackableObject) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("CallbackTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create some objects with different values
  auto* object1 = producer.createObject(100);
  auto* object2 = producer.createObject(200);
  auto* object3 = producer.createObject(300);

  // Create a consumer
  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer1");

  // Register the consumer
  consumer->registerWithTracker();

  // Track which objects were processed by the callback and their values
  std::vector<std::pair<TrackableObject<TestObject>*, int>> callbackData;

  // Set a global callback that captures the TrackableObject and its value
  tracker.setGlobalOnChangeProcessedCallback(
      [&callbackData](TrackableObject<TestObject>* trackableObj) {
        // Verify we can access the trackable object and its data
        ASSERT_NE(trackableObj, nullptr);
        int value = trackableObj->get().getValue();
        callbackData.emplace_back(trackableObj, value);
      });

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start the consumer coroutine
  auto future =
      co_withExecutor(
          &evb, folly::coro::co_invoke([consumer]() -> folly::coro::Task<void> {
            co_await consumer->consumeChanges();
          }))
          .start();

  // Publish changes in a specific order
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  // Run the event loop until the consumer processes all items
  bool success = runUntil(
      evb,
      [&]() { return consumer->getProcessedItems().size() >= 3; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer to process all items");
  ASSERT_TRUE(success);

  // Run the event loop until all callbacks are called
  success = runUntil(
      evb,
      [&]() { return callbackData.size() >= 3; },
      std::chrono::seconds(5),
      "Timeout waiting for all callbacks to be called");
  ASSERT_TRUE(success);

  // Verify the callback was called for each object with correct data
  ASSERT_EQ(callbackData.size(), 3);

  /*
   * Verify the callback received the correct TrackableObject pointers and
   * values
   */
  EXPECT_EQ(callbackData[0].first, object1);
  EXPECT_EQ(callbackData[0].second, 100);

  EXPECT_EQ(callbackData[1].first, object2);
  EXPECT_EQ(callbackData[1].second, 200);

  EXPECT_EQ(callbackData[2].first, object3);
  EXPECT_EQ(callbackData[2].second, 300);

  // Verify the consumer processed the items in the correct order
  const auto& processedItems = consumer->getProcessedItems();
  ASSERT_EQ(processedItems.size(), 3);
  EXPECT_EQ(processedItems[0], 100);
  EXPECT_EQ(processedItems[1], 200);
  EXPECT_EQ(processedItems[2], 300);

  // Test updating an object and verify callback receives updated data
  producer.updateAndPublish(object1, 150);

  // Run the event loop until the consumer processes the updated item
  success = runUntil(
      evb,
      [&]() { return consumer->getProcessedItems().size() >= 4; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer to process updated item");
  ASSERT_TRUE(success);

  // Run the event loop until the callback is called for the updated object
  success = runUntil(
      evb,
      [&]() { return callbackData.size() >= 4; },
      std::chrono::seconds(5),
      "Timeout waiting for callback for updated object");
  ASSERT_TRUE(success);

  // Verify the callback received the updated object with new value
  ASSERT_EQ(callbackData.size(), 4);
  EXPECT_EQ(callbackData[3].first, object1);
  EXPECT_EQ(callbackData[3].second, 150);

  // Signal the consumer to terminate
  consumer->terminate();

  // Deregister the consumer
  consumer->deregisterFromTracker();

  // Cancel the future
  future.cancel();

  // Run the event loop to process any pending events
  evb.loop();
}

/*
 * Test polled mode where same object is published multiple times and consumer
 * gets latest change
 */
TEST(ChangeTrackerTest, PolledModeSameObjectMultiplePublishes) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("PolledTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create a single object that will be updated multiple times
  auto* object = producer.createObject(1);

  // Create an event base for scheduling coroutines and timers
  folly::EventBase evb;

  // Create a test consumer in polled mode
  auto consumer = std::make_shared<TestConsumer>(tracker, "PolledConsumer");
  consumer->registerWithTracker();
  consumer->setPolledMode();

  // Verify the consumer is in polled mode
  EXPECT_EQ(
      consumer->getConsumptionMode(),
      Consumer<TestObject>::ConsumptionMode::POLLED);

  /*
   * Publish the same object multiple times with different values
   * This simulates rapid updates to the same object before polling occurs
   */
  producer.updateAndPublish(object, 10); // First update
  producer.updateAndPublish(object, 20); // Second update
  producer.updateAndPublish(object, 30); // Third update
  producer.updateAndPublish(object, 40); // Fourth update
  producer.updateAndPublish(
      object, 50); // Final update (this should be the only one consumed)

  // Timer callback that drives polled consumption (simulates poll timer firing)
  auto timerCallback = [&consumer, &evb]() noexcept {
    // Start a polled cycle to consume available changes
    co_withExecutor(
        &evb, folly::coro::co_invoke([consumer]() -> folly::coro::Task<void> {
          co_await consumer->consumeChanges();
        }))
        .start();
  };

  /*
   * Schedule timer to fire after all updates are published (simulates poll
   * timer)
   */
  auto timer = folly::AsyncTimeout::schedule(
      std::chrono::milliseconds(
          100), // 100ms delay to ensure all updates are queued
      evb,
      timerCallback);

  // Run the event loop until consumer processes the change
  bool success = runUntil(
      evb,
      [&]() { return consumer->getProcessedItems().size() >= 1; },
      std::chrono::seconds(5),
      "Timeout waiting for polled consumer to process the latest change");
  ASSERT_TRUE(success);

  /*
   * Verify the consumer processed only ONE item (the latest change)
   * This is the key behavior: multiple publishes of same object should coalesce
   */
  const auto& processedItems = consumer->getProcessedItems();
  ASSERT_EQ(processedItems.size(), 1);

  // Verify the consumer got the LATEST value (50), not any intermediate values
  EXPECT_EQ(processedItems[0], 50);

  /*
   * Verify that intermediate values (10, 20, 30, 40) were NOT processed
   * This demonstrates change coalescing in polled mode
   */
  for (int intermediateValue : {10, 20, 30, 40}) {
    bool foundIntermediate = false;
    for (int processedValue : processedItems) {
      if (processedValue == intermediateValue) {
        foundIntermediate = true;
        break;
      }
    }
    EXPECT_FALSE(foundIntermediate)
        << "Consumer should not have processed intermediate value "
        << intermediateValue;
  }

  // Stop polled mode
  consumer->setTriggeredMode();

  // Signal the consumer to terminate
  consumer->terminate();

  // Deregister the consumer
  consumer->deregisterFromTracker();

  // Run the event loop to process any pending events
  evb.loop();
}

/*
 * Test polled mode where changes are published BEFORE consumer registration
 * Consumer should only receive changes published AFTER registration
 */
TEST(ChangeTrackerTest, PolledModeConsumerRegistersAfterPublish) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("PolledAfterPublishTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  /*
   * FIRST register a consumer in polled mode but DON'T start its timer yet
   * This ensures there are consumers registered so changes will be added to
   * change list
   */
  auto firstConsumer =
      std::make_shared<TestConsumer>(tracker, "FirstPolledConsumer");
  firstConsumer->registerWithTracker();
  firstConsumer->setPolledMode();

  /*
   * Create some objects and publish them AFTER first consumer registration
   * but BEFORE second consumer registration
   */
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  /*
   * Publish changes AFTER first consumer registration but BEFORE second
   * consumer registration
   */
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  // Verify that changes are in the tracker since there's a registered consumer
  EXPECT_FALSE(tracker.getChangeList().empty());

  /*
   * NOW register the SECOND consumer in polled mode AFTER the initial changes
   * were published
   */
  auto secondConsumer =
      std::make_shared<TestConsumer>(tracker, "SecondPolledConsumer");
  secondConsumer->registerWithTracker();
  secondConsumer->setPolledMode();

  // Verify both consumers are in polled mode
  EXPECT_EQ(
      firstConsumer->getConsumptionMode(),
      Consumer<TestObject>::ConsumptionMode::POLLED);
  EXPECT_EQ(
      secondConsumer->getConsumptionMode(),
      Consumer<TestObject>::ConsumptionMode::POLLED);

  // Both consumers should not have processed any items yet
  EXPECT_EQ(firstConsumer->getProcessedItems().size(), 0);
  EXPECT_EQ(secondConsumer->getProcessedItems().size(), 0);

  /*
   * CRITICAL PART: Call consumeChanges on the second consumer
   * It will go through the list but won't process anything because objects were
   * published before registration Due to a bug, when it reaches the end without
   * consuming, it's not added to the ready list
   */
  auto secondConsumerEmptyRunCallback = [&secondConsumer, &evb]() noexcept {
    co_withExecutor(
        &evb,
        folly::coro::co_invoke([secondConsumer]() -> folly::coro::Task<void> {
          co_await secondConsumer->consumeChanges();
        }))
        .start();
  };

  // Trigger the empty consumption run for SECOND consumer
  auto emptyRunTimer = folly::AsyncTimeout::schedule(
      std::chrono::milliseconds(100), evb, secondConsumerEmptyRunCallback);

  // Create a future that completes after a delay instead of using sleep
  bool waitCompleted = false;
  auto waitCallback = [&waitCompleted]() noexcept { waitCompleted = true; };

  auto waitTimer = folly::AsyncTimeout::schedule(
      std::chrono::milliseconds(150), evb, waitCallback);

  // Run the event loop to let the second consumer go through the list
  evb.loopOnce();

  // Wait for the timer to complete (replaces the sleep)
  bool timerCompleted = runUntil(
      evb,
      [&waitCompleted]() { return waitCompleted; },
      std::chrono::seconds(1),
      "Timeout waiting for async wait timer to complete");
  ASSERT_TRUE(timerCompleted);

  evb.loopOnce();

  // Verify the second consumer hasn't processed anything (as expected)
  EXPECT_EQ(secondConsumer->getProcessedItems().size(), 0);

  // NOW publish NEW objects - these should be consumable by the second consumer
  auto* object4 = producer.createObject(4);
  auto* object5 = producer.createObject(5);
  auto* object6 = producer.createObject(6);

  producer.publishChange(object4);
  producer.publishChange(object5);
  producer.publishChange(object6);

  // Try to consume the new objects with the second consumer
  auto timerCallback = [&secondConsumer, &evb]() noexcept {
    co_withExecutor(
        &evb,
        folly::coro::co_invoke([secondConsumer]() -> folly::coro::Task<void> {
          co_await secondConsumer->consumeChanges();
        }))
        .start();
  };

  auto timer = folly::AsyncTimeout::schedule(
      std::chrono::milliseconds(100), evb, timerCallback);

  // Try to consume the new objects with the second consumer
  bool success = runUntil(
      evb,
      [&]() { return secondConsumer->getProcessedItems().size() >= 3; },
      std::chrono::seconds(5),
      "Timeout waiting for second consumer to process new objects");

  ASSERT_TRUE(success);

  // Verify the second consumer processed the new objects
  const auto& processedItems = secondConsumer->getProcessedItems();
  ASSERT_EQ(processedItems.size(), 3);
  EXPECT_EQ(processedItems[0], 4);
  EXPECT_EQ(processedItems[1], 5);
  EXPECT_EQ(processedItems[2], 6);

  // Clean up
  firstConsumer->setTriggeredMode();
  secondConsumer->setTriggeredMode();
  firstConsumer->terminate();
  secondConsumer->terminate();
  firstConsumer->deregisterFromTracker();
  secondConsumer->deregisterFromTracker();
  evb.loop();
}

/*
 * Simple test with single polled consumer
 * Consumer registers first, consumes initial objects correctly
 */
TEST(ChangeTrackerTest, SimplePolledConsumerReadyList) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("SimplePolledTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Create and register a single consumer in polled mode FIRST
  auto consumer = std::make_shared<TestConsumer>(tracker, "PolledConsumer");
  consumer->registerWithTracker();
  consumer->setPolledMode();

  // Verify the consumer is in polled mode
  EXPECT_EQ(
      consumer->getConsumptionMode(),
      Consumer<TestObject>::ConsumptionMode::POLLED);

  // Consumer should not have processed any items yet
  EXPECT_EQ(consumer->getProcessedItems().size(), 0);

  // Publish first batch of objects AFTER consumer registration
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  // Timer callback that drives polled consumption
  auto timerCallback = [&consumer, &evb]() noexcept {
    co_withExecutor(
        &evb, folly::coro::co_invoke([consumer]() -> folly::coro::Task<void> {
          co_await consumer->consumeChanges();
        }))
        .start();
  };

  // Trigger first consumption cycle
  auto timer1 = folly::AsyncTimeout::schedule(
      std::chrono::milliseconds(100), evb, timerCallback);

  // Run the event loop until consumer processes first batch
  bool success = runUntil(
      evb,
      [&]() { return consumer->getProcessedItems().size() >= 3; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer to process first batch");
  ASSERT_TRUE(success);

  // Verify the consumer processed the first batch correctly
  const auto& processedItems = consumer->getProcessedItems();
  ASSERT_EQ(processedItems.size(), 3);
  EXPECT_EQ(processedItems[0], 1);
  EXPECT_EQ(processedItems[1], 2);
  EXPECT_EQ(processedItems[2], 3);

  // NOW publish second batch of objects
  auto* object4 = producer.createObject(4);
  auto* object5 = producer.createObject(5);
  auto* object6 = producer.createObject(6);

  producer.publishChange(object4);
  producer.publishChange(object5);
  producer.publishChange(object6);

  // Trigger second consumption cycle
  auto timer2 = folly::AsyncTimeout::schedule(
      std::chrono::milliseconds(100), evb, timerCallback);

  // Try to consume the second batch
  success = runUntil(
      evb,
      [&]() { return consumer->getProcessedItems().size() >= 6; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer to process second batch");

  ASSERT_TRUE(success);

  // Verify the consumer processed the second batch
  ASSERT_EQ(consumer->getProcessedItems().size(), 6);
  EXPECT_EQ(consumer->getProcessedItems()[3], 4);
  EXPECT_EQ(consumer->getProcessedItems()[4], 5);
  EXPECT_EQ(consumer->getProcessedItems()[5], 6);

  // Clean up
  consumer->setTriggeredMode();
  consumer->terminate();
  consumer->deregisterFromTracker();
  evb.loop();
}

/*
 * Test that publishing an object without any consumers does not call the
 * callback
 */
TEST(ChangeTrackerTest, PublishWithoutConsumersNoCallback) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("TestTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create an object
  auto* object = producer.createObject(42);

  // Flag to track if the callback was called
  bool callbackCalled = false;

  // Set a global callback for the tracker
  tracker.setGlobalOnChangeProcessedCallback(
      [&callbackCalled](TrackableObject<TestObject>*) {
        callbackCalled = true;
      });

  // Publish a change without any registered consumers
  producer.publishChange(object);

  // Verify that the change list is empty (no consumers means no processing)
  EXPECT_TRUE(tracker.getChangeList().empty());

  // Verify that the callback was called
  EXPECT_TRUE(callbackCalled);

  // The object should not be on the change list
  EXPECT_FALSE(object->isOnChangeList());
}

// Test Case 1a: Join consumer when existing consumer has no marker (nullopt)
TEST(ChangeTrackerTest, JoinReadyConsumer) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("JoinTestTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create and register first consumer
  auto consumer1 = std::make_shared<TestConsumer>(tracker, "Consumer1");
  consumer1->registerWithTracker();

  // Consumer1 should be ready (null marker, in ready list)
  EXPECT_EQ(consumer1->getMarker(), nullptr);

  // Create second consumer but don't register it yet
  auto consumer2 = std::make_shared<TestConsumer>(tracker, "Consumer2");

  // Register consumer2 first, then join it at the same position as consumer1
  consumer2->registerWithTracker();
  tracker.joinConsumer(consumer1, consumer2);

  // Consumer2 should also be ready (null marker, in ready list)
  EXPECT_EQ(consumer2->getMarker(), nullptr);

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start both consumer coroutines
  auto future1 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer1]() -> folly::coro::Task<void> {
            co_await consumer1->consumeChanges();
          }))
          .start();

  auto future2 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer2]() -> folly::coro::Task<void> {
            co_await consumer2->consumeChanges();
          }))
          .start();

  // Publish some objects
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  // Run the event loop until both consumers process all items
  bool success = runUntil(
      evb,
      [&]() {
        return consumer1->getProcessedItems().size() >= 3 &&
            consumer2->getProcessedItems().size() >= 3;
      },
      std::chrono::seconds(5),
      "Timeout waiting for consumers to process all items");
  ASSERT_TRUE(success);

  // Both consumers should have processed the same items
  const auto& items1 = consumer1->getProcessedItems();
  const auto& items2 = consumer2->getProcessedItems();

  ASSERT_EQ(items1.size(), 3);
  ASSERT_EQ(items2.size(), 3);

  EXPECT_EQ(items1[0], 1);
  EXPECT_EQ(items1[1], 2);
  EXPECT_EQ(items1[2], 3);

  EXPECT_EQ(items2[0], 1);
  EXPECT_EQ(items2[1], 2);
  EXPECT_EQ(items2[2], 3);

  // Clean up
  consumer1->terminate();
  consumer2->terminate();
  consumer1->deregisterFromTracker();
  consumer2->deregisterFromTracker();
  evb.loop();
}

/*
 * Test Case 1b: Join consumer when existing consumer has consumed all items
 * and is ready (marker is nullptr). The new consumer should also become ready.
 */
TEST(ChangeTrackerTest, JoinReadyConsumerWithNullptrMarker) {
  ChangeTracker<TestObject> tracker("JoinReadyNullptrTracker");
  TestProducer producer(tracker);

  auto consumer1 = std::make_shared<TestConsumer>(tracker, "Consumer1");
  consumer1->registerWithTracker();

  // Consumer1 starts with null marker
  EXPECT_EQ(consumer1->getMarker(), nullptr);

  folly::EventBase evb;

  // Start consumer1 coroutine
  auto future1 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer1]() -> folly::coro::Task<void> {
            co_await consumer1->consumeChanges();
          }))
          .start();

  /*
   * Publish and consume an item so consumer1 reaches ready state with
   * marker == nullptr (as opposed to nullopt)
   */
  auto* object1 = producer.createObject(1);
  producer.publishChange(object1);

  bool success = runUntil(
      evb,
      [&]() { return consumer1->getProcessedItems().size() >= 1; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer1 to process item");
  ASSERT_TRUE(success);

  // Consumer1 should now be ready with marker == nullptr
  ASSERT_TRUE(consumer1->isReady());

  // Create consumer2 and join at consumer1's position
  auto consumer2 = std::make_shared<TestConsumer>(tracker, "Consumer2");
  consumer2->registerWithTracker();
  tracker.joinConsumer(consumer1, consumer2);

  // Consumer2 should also be ready (marker is nullptr)
  EXPECT_TRUE(consumer2->isReady());

  // Publish more items — both consumers should process them
  auto* object2 = producer.createObject(2);
  producer.publishChange(object2);

  auto future2 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer2]() -> folly::coro::Task<void> {
            co_await consumer2->consumeChanges();
          }))
          .start();

  success = runUntil(
      evb,
      [&]() {
        return consumer1->getProcessedItems().size() >= 2 &&
            consumer2->getProcessedItems().size() >= 1;
      },
      std::chrono::seconds(5),
      "Timeout waiting for consumers to process items");
  ASSERT_TRUE(success);

  EXPECT_EQ(consumer1->getProcessedItems().size(), 2);
  EXPECT_EQ(consumer2->getProcessedItems().size(), 1);
  EXPECT_EQ(consumer2->getProcessedItems()[0], 2);

  consumer1->terminate();
  consumer2->terminate();
  consumer1->deregisterFromTracker();
  consumer2->deregisterFromTracker();
  evb.loop();
}

// Test Case 2: Join consumer when existing consumer is pended
TEST(ChangeTrackerTest, JoinPendedConsumer) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("JoinPendedTestTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create some objects first
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);
  auto* object4 = producer.createObject(4);
  auto* object5 = producer.createObject(5);

  // Create and register first consumer that will pend on value 3
  auto consumer1 = std::make_shared<TestConsumer>(tracker, "Consumer1");
  consumer1->setPendOnValues({3});
  consumer1->registerWithTracker();

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start consumer1 coroutine
  auto future1 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer1]() -> folly::coro::Task<void> {
            co_await consumer1->consumeChanges();
          }))
          .start();

  // Publish objects 1, 2, 3, 4, 5
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);
  producer.publishChange(object4);
  producer.publishChange(object5);

  // Run the event loop until consumer1 pends on object3
  bool success = runUntil(
      evb,
      [&]() { return consumer1->getPendedOnItem() != nullptr; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer1 to pend on object3");
  ASSERT_TRUE(success);

  // Verify consumer1 processed items 1 and 2, then pended on 3
  const auto& items1 = consumer1->getProcessedItems();
  ASSERT_EQ(items1.size(), 2);
  EXPECT_EQ(items1[0], 1);
  EXPECT_EQ(items1[1], 2);

  // Verify consumer1 is pended on object3
  ASSERT_NE(consumer1->getPendedOnItem(), nullptr);
  EXPECT_EQ(consumer1->getPendedOnItem()->getTypedObject().getValue(), 3);

  // Now create consumer2 and join it at the same position as consumer1
  auto consumer2 = std::make_shared<TestConsumer>(tracker, "Consumer2");

  // Register consumer2 first, then join it at the same position as consumer1
  consumer2->registerWithTracker();
  tracker.joinConsumer(consumer1, consumer2);

  // Consumer2 should be pended at the same item as consumer1
  ASSERT_NE(consumer2->getMarker(), nullptr);
  EXPECT_EQ(consumer2->getMarker()->getTypedObject().getValue(), 3);

  // Start consumer2 coroutine
  auto future2 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer2]() -> folly::coro::Task<void> {
            co_await consumer2->consumeChanges();
          }))
          .start();

  // Clear pend values for both consumers so they can continue
  consumer1->setPendOnValues({});
  consumer2->setPendOnValues({});

  // Resume both consumers
  consumer1->resume();
  consumer2->resume();

  // Run the event loop until both consumers process all remaining items
  success = runUntil(
      evb,
      [&]() {
        return consumer1->getProcessedItems().size() >= 5 &&
            consumer2->getProcessedItems().size() >= 3;
      },
      std::chrono::seconds(5),
      "Timeout waiting for consumers to process remaining items");
  ASSERT_TRUE(success);

  // Verify consumer1 processed all items (1, 2, 3, 4, 5)
  const auto& finalItems1 = consumer1->getProcessedItems();
  ASSERT_EQ(finalItems1.size(), 5);
  EXPECT_EQ(finalItems1[0], 1);
  EXPECT_EQ(finalItems1[1], 2);
  EXPECT_EQ(finalItems1[2], 3);
  EXPECT_EQ(finalItems1[3], 4);
  EXPECT_EQ(finalItems1[4], 5);

  // Verify consumer2 processed items from where it joined (3, 4, 5)
  const auto& finalItems2 = consumer2->getProcessedItems();
  ASSERT_EQ(finalItems2.size(), 3);
  EXPECT_EQ(finalItems2[0], 3);
  EXPECT_EQ(finalItems2[1], 4);
  EXPECT_EQ(finalItems2[2], 5);

  // Clean up
  consumer1->terminate();
  consumer2->terminate();
  consumer1->deregisterFromTracker();
  consumer2->deregisterFromTracker();
  future1.cancel();
  future2.cancel();
  evb.loop();
}

/*
 * Test Case 3: Join consumer when existing consumer is pended, then publish
 * more items
 */
TEST(ChangeTrackerTest, JoinConsumerPendedWithNewItems) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("JoinPendedNewItemsTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create some objects first
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  // Create and register first consumer that will pend on value 2
  auto consumer1 = std::make_shared<TestConsumer>(tracker, "Consumer1");
  consumer1->setPendOnValues({2});
  consumer1->registerWithTracker();

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start consumer1 coroutine
  auto future1 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer1]() -> folly::coro::Task<void> {
            co_await consumer1->consumeChanges();
          }))
          .start();

  // Publish objects 1, 2, 3
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  // Run the event loop until consumer1 pends on object2
  bool success = runUntil(
      evb,
      [&]() { return consumer1->getPendedOnItem() != nullptr; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer1 to pend on object2");
  ASSERT_TRUE(success);

  // Verify consumer1 processed item 1, then pended on 2
  const auto& items1 = consumer1->getProcessedItems();
  ASSERT_EQ(items1.size(), 1);
  EXPECT_EQ(items1[0], 1);

  // Now create consumer2 and join it at the same position as consumer1
  auto consumer2 = std::make_shared<TestConsumer>(tracker, "Consumer2");
  // Register consumer2 first, then join it at the same position as consumer1
  consumer2->registerWithTracker();
  tracker.joinConsumer(consumer1, consumer2);

  // Consumer2 should be pended at the same item as consumer1 (object2)
  ASSERT_NE(consumer2->getMarker(), nullptr);
  EXPECT_EQ(consumer2->getMarker()->getTypedObject().getValue(), 2);

  // Now publish MORE items AFTER consumer2 has joined
  auto* object4 = producer.createObject(4);
  auto* object5 = producer.createObject(5);
  auto* object6 = producer.createObject(6);

  producer.publishChange(object4);
  producer.publishChange(object5);
  producer.publishChange(object6);

  // Start consumer2 coroutine
  auto future2 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer2]() -> folly::coro::Task<void> {
            co_await consumer2->consumeChanges();
          }))
          .start();

  // Clear pend values for both consumers so they can continue
  consumer1->setPendOnValues({});
  consumer2->setPendOnValues({});

  // Resume both consumers
  consumer1->resume();
  consumer2->resume();

  // Run the event loop until both consumers process all items
  success = runUntil(
      evb,
      [&]() {
        return consumer1->getProcessedItems().size() >= 6 &&
            consumer2->getProcessedItems().size() >= 5;
      },
      std::chrono::seconds(5),
      "Timeout waiting for consumers to process all items");
  ASSERT_TRUE(success);

  // Verify consumer1 processed all items (1, 2, 3, 4, 5, 6)
  const auto& finalItems1 = consumer1->getProcessedItems();
  ASSERT_EQ(finalItems1.size(), 6);
  EXPECT_EQ(finalItems1[0], 1);
  EXPECT_EQ(finalItems1[1], 2);
  EXPECT_EQ(finalItems1[2], 3);
  EXPECT_EQ(finalItems1[3], 4);
  EXPECT_EQ(finalItems1[4], 5);
  EXPECT_EQ(finalItems1[5], 6);

  /*
   * Verify consumer2 processed items from where it joined (2, 3, 4, 5, 6)
   * This is the critical test - consumer2 should process items 4, 5, 6
   * because its bit was set in those items when it joined
   */
  const auto& finalItems2 = consumer2->getProcessedItems();
  ASSERT_EQ(finalItems2.size(), 5);
  EXPECT_EQ(finalItems2[0], 2);
  EXPECT_EQ(finalItems2[1], 3);
  EXPECT_EQ(finalItems2[2], 4);
  EXPECT_EQ(finalItems2[3], 5);
  EXPECT_EQ(finalItems2[4], 6);

  // Clean up
  consumer1->terminate();
  consumer2->terminate();
  consumer1->deregisterFromTracker();
  consumer2->deregisterFromTracker();
  future1.cancel();
  future2.cancel();
  evb.loop();
}

// Test Case 4: Multiple consumers join at the same pended position
TEST(ChangeTrackerTest, MultipleConsumersJoinSamePendedPosition) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("MultiJoinTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create some objects
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);
  auto* object4 = producer.createObject(4);

  // Create and register first consumer that will pend on value 2
  auto consumer1 = std::make_shared<TestConsumer>(tracker, "Consumer1");
  consumer1->setPendOnValues({2});
  consumer1->registerWithTracker();

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start consumer1 coroutine
  auto future1 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer1]() -> folly::coro::Task<void> {
            co_await consumer1->consumeChanges();
          }))
          .start();

  // Publish objects
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);
  producer.publishChange(object4);

  // Run the event loop until consumer1 pends on object2
  bool success = runUntil(
      evb,
      [&]() { return consumer1->getPendedOnItem() != nullptr; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer1 to pend on object2");
  ASSERT_TRUE(success);

  // Now create multiple consumers and join them at the same position
  auto consumer2 = std::make_shared<TestConsumer>(tracker, "Consumer2");
  auto consumer3 = std::make_shared<TestConsumer>(tracker, "Consumer3");

  consumer2->registerWithTracker();
  consumer3->registerWithTracker();
  tracker.joinConsumer(consumer1, consumer2);
  tracker.joinConsumer(consumer1, consumer3);

  // All consumers should be pended at the same item
  ASSERT_NE(consumer2->getMarker(), nullptr);
  ASSERT_NE(consumer3->getMarker(), nullptr);
  EXPECT_EQ(consumer2->getMarker()->getTypedObject().getValue(), 2);
  EXPECT_EQ(consumer3->getMarker()->getTypedObject().getValue(), 2);

  // Start consumer2 and consumer3 coroutines
  auto future2 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer2]() -> folly::coro::Task<void> {
            co_await consumer2->consumeChanges();
          }))
          .start();

  auto future3 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer3]() -> folly::coro::Task<void> {
            co_await consumer3->consumeChanges();
          }))
          .start();

  // Clear pend values and resume all consumers
  consumer1->setPendOnValues({});
  consumer2->setPendOnValues({});
  consumer3->setPendOnValues({});

  consumer1->resume();
  consumer2->resume();
  consumer3->resume();

  // Run the event loop until all consumers process all items
  success = runUntil(
      evb,
      [&]() {
        return consumer1->getProcessedItems().size() >= 4 &&
            consumer2->getProcessedItems().size() >= 3 &&
            consumer3->getProcessedItems().size() >= 3;
      },
      std::chrono::seconds(5),
      "Timeout waiting for all consumers to process items");
  ASSERT_TRUE(success);

  // Verify all consumers processed the expected items
  const auto& items1 = consumer1->getProcessedItems();
  const auto& items2 = consumer2->getProcessedItems();
  const auto& items3 = consumer3->getProcessedItems();

  // Consumer1 should have processed all items (1, 2, 3, 4)
  ASSERT_EQ(items1.size(), 4);
  EXPECT_EQ(items1[0], 1);
  EXPECT_EQ(items1[1], 2);
  EXPECT_EQ(items1[2], 3);
  EXPECT_EQ(items1[3], 4);

  /*
   * Consumer2 and Consumer3 should have processed items from where they joined
   * (2, 3, 4)
   */
  ASSERT_EQ(items2.size(), 3);
  EXPECT_EQ(items2[0], 2);
  EXPECT_EQ(items2[1], 3);
  EXPECT_EQ(items2[2], 4);

  ASSERT_EQ(items3.size(), 3);
  EXPECT_EQ(items3[0], 2);
  EXPECT_EQ(items3[1], 3);
  EXPECT_EQ(items3[2], 4);

  // Clean up
  consumer1->terminate();
  consumer2->terminate();
  consumer3->terminate();
  consumer1->deregisterFromTracker();
  consumer2->deregisterFromTracker();
  consumer3->deregisterFromTracker();
  evb.loop();
}

// Test debug facility with object and consumer display callbacks
TEST(ChangeTrackerTest, DebugFacilityCallbacks) {
  // Create a change tracker with object display callback
  ChangeTracker<TestObject> tracker("DebugTracker");

  // Set object display callback - producer should set this
  tracker.setObjectDisplayCallback([](const TestObject& obj) -> std::string {
    return fmt::format("TestObject(value={})", obj.getValue());
  });

  // Create a producer
  TestProducer producer(tracker);

  // Create some objects
  auto* object1 = producer.createObject(42);
  auto* object2 = producer.createObject(100);

  // Create a consumer with display callback
  auto consumer = std::make_shared<TestConsumer>(tracker, "DebugConsumer");
  consumer->setConsumerDisplayCallback(
      []() -> std::string { return "DebugTestConsumer(name=TestConsumer)"; });

  // Register the consumer
  consumer->registerWithTracker();

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start the consumer coroutine
  auto future =
      co_withExecutor(
          &evb, folly::coro::co_invoke([consumer]() -> folly::coro::Task<void> {
            co_await consumer->consumeChanges();
          }))
          .start();

  // Test object display string
  std::string objDisplayStr = tracker.getObjectDisplayString(object1->get());
  EXPECT_EQ(objDisplayStr, "TestObject(value=42)");

  // Test consumer display string
  std::string consumerDisplayStr = consumer->getDisplayString();
  EXPECT_EQ(consumerDisplayStr, "DebugTestConsumer(name=TestConsumer)");

  // Test item display string (includes object info)
  producer.publishChange(object1);

  // Get the change item to test display
  auto* head = tracker.getHead();
  ASSERT_NE(head, nullptr);

  std::string itemDisplayStr = tracker.getItemDisplayString(head);
  // Should contain both item pointer and object info
  EXPECT_TRUE(itemDisplayStr.find("item@") != std::string::npos);
  EXPECT_TRUE(itemDisplayStr.find("TestObject(value=42)") != std::string::npos);

  // Publish another change and verify processing
  producer.publishChange(object2);

  // Run the event loop until the consumer processes all items
  bool success = runUntil(
      evb,
      [&]() { return consumer->getProcessedItems().size() >= 2; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer to process all items");
  ASSERT_TRUE(success);

  // Verify the consumer processed all items
  const auto& processedItems = consumer->getProcessedItems();
  ASSERT_EQ(processedItems.size(), 2);
  EXPECT_EQ(processedItems[0], 42);
  EXPECT_EQ(processedItems[1], 100);

  // Test that display callbacks work with null/empty cases
  std::string nullItemDisplay = tracker.getItemDisplayString(nullptr);
  EXPECT_EQ(nullItemDisplay, "item@null");

  // Test consumer without display callback
  auto plainConsumer = std::make_shared<TestConsumer>(tracker, "PlainConsumer");
  std::string plainDisplayStr = plainConsumer->getDisplayString();
  EXPECT_TRUE(plainDisplayStr.find("Consumer@") != std::string::npos);

  /*
   * Test debug flag functionality
   * Note: We can't easily test the actual debug output without enabling the
   * flag but we can test that the debug functions exist and work
   */
  bool debugEnabled = facebook::neteng::fboss::bgp::changetracker::
      ChangeTrackerDebug::isDebugEnabled();
  // Debug should be disabled by default
  EXPECT_FALSE(debugEnabled);

  // Signal the consumer to terminate
  consumer->terminate();

  // Deregister the consumer
  consumer->deregisterFromTracker();

  // Cancel the future
  future.cancel();

  // Run the event loop to process any pending events
  evb.loop();
}

// Test isConsumerSetOnTrackableObject API
TEST(ChangeTrackerTest, IsConsumerSetOnTrackableObject) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("ConsumerSetTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create consumers that will pend on value 99
  auto consumer1 = std::make_shared<TestConsumer>(tracker, "Consumer1");
  auto consumer2 = std::make_shared<TestConsumer>(tracker, "Consumer2");

  consumer1->setPendOnValues({99});
  consumer2->setPendOnValues({99});

  // Register both consumers
  consumer1->registerWithTracker();
  consumer2->registerWithTracker();

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start consumer coroutines
  auto future1 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer1]() -> folly::coro::Task<void> {
            co_await consumer1->consumeChanges();
          }))
          .start();

  auto future2 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer2]() -> folly::coro::Task<void> {
            co_await consumer2->consumeChanges();
          }))
          .start();

  // FIRST: Publish a dummy object (99) that consumers will pend on
  auto* dummyObject = producer.createObject(99);
  producer.publishChange(dummyObject);

  // Wait for all consumers to pend on the dummy object
  bool success = runUntil(
      evb,
      [&]() {
        return consumer1->getPendedOnItem() != nullptr &&
            consumer2->getPendedOnItem() != nullptr;
      },
      std::chrono::seconds(5),
      "Timeout waiting for consumers to pend on dummy object");
  ASSERT_TRUE(success);

  /*
   * NOW consumers are blocked on the dummy object, we can test with stable
   * state Create test objects
   */
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  // Test 1: Object not published - should return false for all consumers
  EXPECT_FALSE(tracker.isConsumerSetOnTrackableObject(object1, consumer1));
  EXPECT_FALSE(tracker.isConsumerSetOnTrackableObject(object1, consumer2));

  // Test 2: Publish object1 - both consumers should now be set on it
  producer.publishChange(object1);

  // Verify object1 is on change list
  EXPECT_TRUE(object1->isOnChangeList());

  // Both consumers should be set on object1 since it's published
  EXPECT_TRUE(tracker.isConsumerSetOnTrackableObject(object1, consumer1));
  EXPECT_TRUE(tracker.isConsumerSetOnTrackableObject(object1, consumer2));

  // Object2 and object3 are not published yet, so should return false
  EXPECT_FALSE(tracker.isConsumerSetOnTrackableObject(object2, consumer1));
  EXPECT_FALSE(tracker.isConsumerSetOnTrackableObject(object3, consumer1));

  // Test 3: Publish object2
  producer.publishChange(object2);

  EXPECT_TRUE(object2->isOnChangeList());
  EXPECT_TRUE(tracker.isConsumerSetOnTrackableObject(object2, consumer1));
  EXPECT_TRUE(tracker.isConsumerSetOnTrackableObject(object2, consumer2));

  // Test 4: Test with null pointer - should return false
  EXPECT_FALSE(tracker.isConsumerSetOnTrackableObject(nullptr, consumer1));
  EXPECT_FALSE(tracker.isConsumerSetOnTrackableObject(object1, nullptr));

  // Test 5: Deregister consumer1 and verify its bit is cleared
  consumer1->terminate();
  consumer1->deregisterFromTracker();

  // Consumer1's bit should be cleared
  EXPECT_FALSE(tracker.isConsumerSetOnTrackableObject(object1, consumer1));
  EXPECT_FALSE(tracker.isConsumerSetOnTrackableObject(object2, consumer1));

  // Consumer2 should still have its bit set
  EXPECT_TRUE(tracker.isConsumerSetOnTrackableObject(object1, consumer2));
  EXPECT_TRUE(tracker.isConsumerSetOnTrackableObject(object2, consumer2));

  // Clean up
  consumer2->terminate();
  consumer2->deregisterFromTracker();
  future1.cancel();
  future2.cancel();
  evb.loop();
}

/**
 * Test selective consumer notification with optional consumer bitmap.
 * Verifies that when a bitmap is provided to publishChange, only the
 * consumers specified in the bitmap are notified.
 */
TEST(ChangeTrackerTest, SelectiveConsumerNotificationTest) {
  ChangeTracker<TestObject> tracker("SelectiveTest");
  TestProducer producer(tracker);

  // Create three consumers and register them
  auto consumer1 = std::make_shared<TestConsumer>(tracker, "Consumer1");
  auto consumer2 = std::make_shared<TestConsumer>(tracker, "Consumer2");
  auto consumer3 = std::make_shared<TestConsumer>(tracker, "Consumer3");

  consumer1->registerWithTracker();
  consumer2->registerWithTracker();
  consumer3->registerWithTracker();

  size_t bit1 = consumer1->getBitPosition();
  size_t bit2 = consumer2->getBitPosition();
  size_t bit3 = consumer3->getBitPosition();

  folly::EventBase evb;

  // Start consumer coroutines
  auto future1 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer1]() -> folly::coro::Task<void> {
            co_await consumer1->consumeChanges();
          }))
          .start();
  auto future2 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer2]() -> folly::coro::Task<void> {
            co_await consumer2->consumeChanges();
          }))
          .start();
  auto future3 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer3]() -> folly::coro::Task<void> {
            co_await consumer3->consumeChanges();
          }))
          .start();

  // Test 1: Publish without bitmap - all consumers should receive
  auto* object1 = producer.createObject(100);
  tracker.publishChange(object1);

  bool success = runUntil(
      evb,
      [&]() {
        return consumer1->getProcessedItems().size() == 1 &&
            consumer2->getProcessedItems().size() == 1 &&
            consumer3->getProcessedItems().size() == 1;
      },
      std::chrono::seconds(5),
      "Timeout waiting for all consumers to process object1");
  ASSERT_TRUE(success);

  EXPECT_EQ(consumer1->getProcessedItems()[0], 100);
  EXPECT_EQ(consumer2->getProcessedItems()[0], 100);
  EXPECT_EQ(consumer3->getProcessedItems()[0], 100);

  // Test 2: Publish with selective bitmap (only consumer1 and consumer2)
  ConsumerBitmap selectiveBitmap;
  BitmapUtils::setBit(selectiveBitmap, bit1);
  BitmapUtils::setBit(selectiveBitmap, bit2);

  auto* object2 = producer.createObject(200);
  tracker.publishChange(object2, selectiveBitmap);

  success = runUntil(
      evb,
      [&]() {
        return consumer1->getProcessedItems().size() == 2 &&
            consumer2->getProcessedItems().size() == 2;
      },
      std::chrono::seconds(5),
      "Timeout waiting for selective consumers to process object2");
  ASSERT_TRUE(success);

  EXPECT_EQ(consumer1->getProcessedItems().size(), 2);
  EXPECT_EQ(consumer2->getProcessedItems().size(), 2);
  EXPECT_EQ(consumer3->getProcessedItems().size(), 1); // Should NOT receive

  EXPECT_EQ(consumer1->getProcessedItems()[1], 200);
  EXPECT_EQ(consumer2->getProcessedItems()[1], 200);

  // Test 3: Publish with different bitmap (only consumer3)
  ConsumerBitmap selectiveBitmap3;
  BitmapUtils::setBit(selectiveBitmap3, bit3);

  auto* object3 = producer.createObject(300);
  tracker.publishChange(object3, selectiveBitmap3);

  success = runUntil(
      evb,
      [&]() { return consumer3->getProcessedItems().size() == 2; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer3 to process object3");
  ASSERT_TRUE(success);

  EXPECT_EQ(consumer1->getProcessedItems().size(), 2); // Should NOT receive
  EXPECT_EQ(consumer2->getProcessedItems().size(), 2); // Should NOT receive
  EXPECT_EQ(consumer3->getProcessedItems().size(), 2);

  EXPECT_EQ(consumer3->getProcessedItems()[1], 300);

  // Clean up
  consumer1->terminate();
  consumer2->terminate();
  consumer3->terminate();
  consumer1->deregisterFromTracker();
  consumer2->deregisterFromTracker();
  consumer3->deregisterFromTracker();
  evb.loop();
}

/**
 * Test that updates to existing objects respect the selective bitmap.
 */
TEST(ChangeTrackerTest, SelectiveConsumerNotificationUpdateTest) {
  ChangeTracker<TestObject> tracker("SelectiveUpdateTest");
  TestProducer producer(tracker);

  auto consumer1 = std::make_shared<TestConsumer>(tracker, "Consumer1");
  auto consumer2 = std::make_shared<TestConsumer>(tracker, "Consumer2");

  consumer1->registerWithTracker();
  consumer2->registerWithTracker();

  size_t bit1 = consumer1->getBitPosition();

  folly::EventBase evb;

  // Start consumer coroutines
  auto future1 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer1]() -> folly::coro::Task<void> {
            co_await consumer1->consumeChanges();
          }))
          .start();
  auto future2 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer2]() -> folly::coro::Task<void> {
            co_await consumer2->consumeChanges();
          }))
          .start();

  // First publish to all consumers
  auto* object1 = producer.createObject(100);
  tracker.publishChange(object1);

  bool success = runUntil(
      evb,
      [&]() {
        return consumer1->getProcessedItems().size() == 1 &&
            consumer2->getProcessedItems().size() == 1;
      },
      std::chrono::seconds(5),
      "Timeout waiting for initial publish");
  ASSERT_TRUE(success);

  // Update the object with selective bitmap (only consumer1)
  ConsumerBitmap selectiveBitmap;
  BitmapUtils::setBit(selectiveBitmap, bit1);

  object1->get().setValue(101);
  tracker.publishChange(object1, selectiveBitmap);

  success = runUntil(
      evb,
      [&]() { return consumer1->getProcessedItems().size() == 2; },
      std::chrono::seconds(5),
      "Timeout waiting for selective update");
  ASSERT_TRUE(success);

  // Consumer1 should have processed both
  EXPECT_EQ(consumer1->getProcessedItems().size(), 2);
  EXPECT_EQ(consumer1->getProcessedItems()[0], 100);
  EXPECT_EQ(consumer1->getProcessedItems()[1], 101);

  // Consumer2 should only have processed the first one
  EXPECT_EQ(consumer2->getProcessedItems().size(), 1);
  EXPECT_EQ(consumer2->getProcessedItems()[0], 100);

  // Clean up
  consumer1->terminate();
  consumer2->terminate();
  consumer1->deregisterFromTracker();
  consumer2->deregisterFromTracker();
  evb.loop();
}

/**
 * Test empty bitmap behavior - no consumers should be notified.
 */
TEST(ChangeTrackerTest, EmptyBitmapTest) {
  ChangeTracker<TestObject> tracker("EmptyBitmapTest");
  TestProducer producer(tracker);

  auto consumer1 = std::make_shared<TestConsumer>(tracker, "Consumer1");
  auto consumer2 = std::make_shared<TestConsumer>(tracker, "Consumer2");

  consumer1->registerWithTracker();
  consumer2->registerWithTracker();

  folly::EventBase evb;

  // Start consumer coroutines
  auto future1 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer1]() -> folly::coro::Task<void> {
            co_await consumer1->consumeChanges();
          }))
          .start();
  auto future2 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer2]() -> folly::coro::Task<void> {
            co_await consumer2->consumeChanges();
          }))
          .start();

  // Publish with empty bitmap - no consumers should be notified
  ConsumerBitmap emptyBitmap;
  auto* object1 = producer.createObject(100);
  tracker.publishChange(object1, emptyBitmap);

  // Run event loop a bit
  for (int i = 0; i < 10; i++) {
    evb.loopOnce(EVLOOP_NONBLOCK);
  }

  // No consumers should have processed anything
  EXPECT_EQ(consumer1->getProcessedItems().size(), 0);
  EXPECT_EQ(consumer2->getProcessedItems().size(), 0);

  // Now publish to all
  tracker.publishChange(object1);

  bool success = runUntil(
      evb,
      [&]() {
        return consumer1->getProcessedItems().size() == 1 &&
            consumer2->getProcessedItems().size() == 1;
      },
      std::chrono::seconds(5),
      "Timeout waiting for full publish");
  ASSERT_TRUE(success);

  EXPECT_EQ(consumer1->getProcessedItems().size(), 1);
  EXPECT_EQ(consumer2->getProcessedItems().size(), 1);

  // Clean up
  consumer1->terminate();
  consumer2->terminate();
  consumer1->deregisterFromTracker();
  consumer2->deregisterFromTracker();
  evb.loop();
}

/*
 * Test iterator-based consumption with YIELD behavior
 * This test verifies that when using iterateChangesToEnd():
 * 1. Consumer processes items until YIELD
 * 2. end() is called to add consumer to pending list
 * 3. Marker stays on the item that caused YIELD (not advanced)
 * 4. Consumer can be resumed and continues from same item
 */
TEST(ChangeTrackerTest, IteratorBasedConsumptionWithYield) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("IteratorYieldTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create some objects
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);
  auto* object4 = producer.createObject(4);
  auto* object5 = producer.createObject(5);

  // Create a consumer that will YIELD on value 3
  auto consumer = std::make_shared<TestConsumer>(tracker, "IteratorConsumer");
  consumer->setPendOnValues({3}); // Will YIELD on object with value 3
  consumer->registerWithTracker();

  // Publish all objects
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);
  producer.publishChange(object4);
  producer.publishChange(object5);

  // Call iterateChangesToEnd() directly (no coroutine)
  consumer->iterateChangesToEnd();

  // Verify consumer processed items 1 and 2, then YIELDED on 3
  const auto& processedItems = consumer->getProcessedItems();
  ASSERT_EQ(processedItems.size(), 2);
  EXPECT_EQ(processedItems[0], 1);
  EXPECT_EQ(processedItems[1], 2);

  /*
   * CRITICAL: Verify consumer is on pending list of object3
   * This proves end() was called
   */
  ASSERT_NE(consumer->getPendedOnItem(), nullptr);
  EXPECT_EQ(consumer->getPendedOnItem()->getTypedObject().getValue(), 3);

  // CRITICAL: Verify marker is still on object3 (NOT advanced)
  ASSERT_NE(consumer->getMarker(), nullptr);
  EXPECT_EQ(consumer->getMarker()->getTypedObject().getValue(), 3);

  /*
   * MOST CRITICAL: Verify consumer is ACTUALLY in the item's pendingConsumers
   * list This is the REAL proof that end() was called correctly
   */
  auto* item3 = consumer->getMarker();
  bool foundInPendingList = false;
  for (const auto& pendingConsumer : item3->pendingConsumers) {
    if (&pendingConsumer == consumer.get()) {
      foundInPendingList = true;
      break;
    }
  }
  ASSERT_TRUE(foundInPendingList)
      << "Consumer MUST be in item's pendingConsumers list after YIELD";

  // Clear the YIELD condition
  consumer->setPendOnValues({});

  // Resume consumption by calling iterateChangesToEnd() again
  consumer->iterateChangesToEnd();

  // Verify consumer processed remaining items (3, 4, 5)
  ASSERT_EQ(consumer->getProcessedItems().size(), 5);
  EXPECT_EQ(consumer->getProcessedItems()[0], 1);
  EXPECT_EQ(consumer->getProcessedItems()[1], 2);
  EXPECT_EQ(consumer->getProcessedItems()[2], 3); // Processed after resume
  EXPECT_EQ(consumer->getProcessedItems()[3], 4);
  EXPECT_EQ(consumer->getProcessedItems()[4], 5);

  // Verify marker is now null (reached end of list)
  EXPECT_EQ(consumer->getMarker(), nullptr);

  // Verify consumer is in ready list
  EXPECT_TRUE(consumer->isReady());

  // Deregister the consumer
  consumer->deregisterFromTracker();
}

// Testable subclass to expose protected changeListHasOnlyOneItem method
class TestableChangeTracker : public ChangeTracker<TestObject> {
 public:
  explicit TestableChangeTracker(std::string name)
      : ChangeTracker<TestObject>(std::move(name)) {}

  // Expose protected method for testing
  bool testChangeListHasOnlyOneItem() const {
    return changeListHasOnlyOneItem();
  }
};

// Test changeListHasOnlyOneItem returns false for empty list
TEST(ChangeTrackerTest, ChangeListHasOnlyOneItem_EmptyList) {
  TestableChangeTracker tracker("TestTracker");

  // Empty list should return false
  EXPECT_FALSE(tracker.testChangeListHasOnlyOneItem());
}

// Test changeListHasOnlyOneItem returns true for list with exactly one item
TEST(ChangeTrackerTest, ChangeListHasOnlyOneItem_OneItem) {
  TestableChangeTracker tracker("TestTracker");

  // Create a consumer so publishChange will add to the list
  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer1");
  consumer->registerWithTracker();

  // Create a trackable object and publish it
  auto trackableObject =
      std::make_unique<TrackableObject<TestObject>>(TestObject(1));
  tracker.publishChange(trackableObject.get());

  // List with one item should return true
  EXPECT_TRUE(tracker.testChangeListHasOnlyOneItem());
  EXPECT_EQ(tracker.getChangeList().size(), 1);

  consumer->deregisterFromTracker();
}

// Test changeListHasOnlyOneItem returns false for list with multiple items
TEST(ChangeTrackerTest, ChangeListHasOnlyOneItem_MultipleItems) {
  TestableChangeTracker tracker("TestTracker");

  // Create a consumer so publishChange will add to the list
  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer1");
  consumer->registerWithTracker();

  // Create multiple trackable objects and publish them
  auto trackableObject1 =
      std::make_unique<TrackableObject<TestObject>>(TestObject(1));
  auto trackableObject2 =
      std::make_unique<TrackableObject<TestObject>>(TestObject(2));
  auto trackableObject3 =
      std::make_unique<TrackableObject<TestObject>>(TestObject(3));

  tracker.publishChange(trackableObject1.get());
  tracker.publishChange(trackableObject2.get());
  tracker.publishChange(trackableObject3.get());

  // List with multiple items should return false
  EXPECT_FALSE(tracker.testChangeListHasOnlyOneItem());
  EXPECT_EQ(tracker.getChangeList().size(), 3);

  consumer->deregisterFromTracker();
}

/*
 * Test changeListHasOnlyOneItem transitions correctly as items are
 * added/removed
 */
TEST(ChangeTrackerTest, ChangeListHasOnlyOneItem_Transitions) {
  TestableChangeTracker tracker("TestTracker");

  // Create a consumer so publishChange will add to the list
  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer1");
  consumer->registerWithTracker();

  // Initially empty
  EXPECT_FALSE(tracker.testChangeListHasOnlyOneItem());

  // Add first item
  auto trackableObject1 =
      std::make_unique<TrackableObject<TestObject>>(TestObject(1));
  tracker.publishChange(trackableObject1.get());
  EXPECT_TRUE(tracker.testChangeListHasOnlyOneItem());

  // Add second item - should now be false
  auto trackableObject2 =
      std::make_unique<TrackableObject<TestObject>>(TestObject(2));
  tracker.publishChange(trackableObject2.get());
  EXPECT_FALSE(tracker.testChangeListHasOnlyOneItem());

  consumer->deregisterFromTracker();
}

/*
 * Test that items published with consumer bitmap 0 (no consumers set) are
 * properly cleaned up from the change list and don't remain forever.
 * This verifies that when an item has an empty consumer bitmap (meaning no
 * consumers need to process it), the item is immediately removed from the
 * change list rather than staying there indefinitely.
 */
TEST(ChangeTrackerTest, ItemsWithEmptyBitmapAreCleanedUp) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("TestTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create multiple consumers and register them
  auto consumer1 = std::make_shared<TestConsumer>(tracker, "Consumer1");
  auto consumer2 = std::make_shared<TestConsumer>(tracker, "Consumer2");
  auto consumer3 = std::make_shared<TestConsumer>(tracker, "Consumer3");

  consumer1->registerWithTracker();
  consumer2->registerWithTracker();
  consumer3->registerWithTracker();

  // Verify consumers are registered
  ASSERT_FALSE(BitmapUtils::isAllBitsCleared(tracker.getFullConsumerBitmap()));

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start the consumer coroutines
  auto future1 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer1]() -> folly::coro::Task<void> {
            co_await consumer1->consumeChanges();
          }))
          .start();

  auto future2 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer2]() -> folly::coro::Task<void> {
            co_await consumer2->consumeChanges();
          }))
          .start();

  auto future3 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer3]() -> folly::coro::Task<void> {
            co_await consumer3->consumeChanges();
          }))
          .start();

  // Create multiple trackable objects
  auto* object1 = producer.createObject(100);
  auto* object2 = producer.createObject(200);
  auto* object3 = producer.createObject(300);

  // Create an empty consumer bitmap (bitmap 0 - no consumers set)
  ConsumerBitmap emptyBitmap;

  /*
   * Publish changes with empty bitmap - these items should be cleaned up
   * immediately since no consumers need to process them
   */
  tracker.publishChange(object1, emptyBitmap);
  tracker.publishChange(object2, emptyBitmap);
  tracker.publishChange(object3, emptyBitmap);

  // Run the event loop briefly to allow any cleanup to occur
  for (int i = 0; i < 10; ++i) {
    evb.loopOnce(EVLOOP_NONBLOCK);
  }

  /*
   * Verify that all items with empty bitmap are cleaned up from the change list
   * This test will FAIL if items remain on the change list forever
   */
  EXPECT_TRUE(tracker.getChangeList().empty())
      << "Items published with empty consumer bitmap (bitmap 0) should be "
         "immediately cleaned up from the change list, but "
      << tracker.getChangeList().size() << " items remain.";

  // Verify that the trackable objects are no longer on the change list
  EXPECT_FALSE(object1->isOnChangeList())
      << "Object 1 should not be on change list after publishing with empty "
         "bitmap";
  EXPECT_FALSE(object2->isOnChangeList())
      << "Object 2 should not be on change list after publishing with empty "
         "bitmap";
  EXPECT_FALSE(object3->isOnChangeList())
      << "Object 3 should not be on change list after publishing with empty "
         "bitmap";

  // Now verify that normal publishing (with all consumers) still works
  producer.publishChange(object1); // Uses full consumer bitmap

  // Run the event loop until consumers process the item
  bool success = runUntil(
      evb,
      [&]() { return consumer1->getProcessedItems().size() >= 1; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer to process item");
  ASSERT_TRUE(success);

  // Verify consumers processed the item
  EXPECT_EQ(consumer1->getProcessedItems().size(), 1);
  EXPECT_EQ(consumer1->getProcessedItems()[0], 100);

  // Signal the consumers to terminate
  consumer1->terminate();
  consumer2->terminate();
  consumer3->terminate();

  // Deregister the consumers
  consumer1->deregisterFromTracker();
  consumer2->deregisterFromTracker();
  consumer3->deregisterFromTracker();

  // Cancel the futures
  future1.cancel();
  future2.cancel();
  future3.cancel();

  // Run the event loop to process any pending events
  evb.loop();
}

/*
 * Test that multiple items published with empty bitmap while consumers are
 * actively processing other items are still cleaned up properly
 */
TEST(ChangeTrackerTest, EmptyBitmapItemsCleanedUpDuringActiveProcessing) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("TestTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Track cleanup callbacks
  std::vector<int> cleanedUpObjects;

  // Set a global callback to track when items are cleaned up
  tracker.setGlobalOnChangeProcessedCallback(
      [&cleanedUpObjects](TrackableObject<TestObject>* obj) {
        if (obj) {
          cleanedUpObjects.push_back(obj->get().getValue());
        }
      });

  // Create consumers - one that pends on value 50 to slow down processing
  auto consumer1 = std::make_shared<TestConsumer>(tracker, "Consumer1");
  auto consumer2 = std::make_shared<TestConsumer>(tracker, "Consumer2");
  consumer1->setPendOnValues({50}); // Will pend on object with value 50

  consumer1->registerWithTracker();
  consumer2->registerWithTracker();

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start the consumer coroutines
  auto future1 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer1]() -> folly::coro::Task<void> {
            co_await consumer1->consumeChanges();
          }))
          .start();

  auto future2 =
      co_withExecutor(
          &evb,
          folly::coro::co_invoke([consumer2]() -> folly::coro::Task<void> {
            co_await consumer2->consumeChanges();
          }))
          .start();

  // First, publish an item that consumer1 will pend on
  auto* pendObject = producer.createObject(50);
  producer.publishChange(pendObject);

  // Run until consumer1 pends on the object
  bool success = runUntil(
      evb,
      [&]() { return consumer1->getPendedOnItem() != nullptr; },
      std::chrono::seconds(5),
      "Timeout waiting for consumer1 to pend on object");
  ASSERT_TRUE(success);

  // Now create objects that will be published with empty bitmap
  auto* emptyBitmapObj1 = producer.createObject(1000);
  auto* emptyBitmapObj2 = producer.createObject(2000);
  auto* emptyBitmapObj3 = producer.createObject(3000);

  ConsumerBitmap emptyBitmap;

  // Record the change list size before publishing empty bitmap items
  size_t listSizeBeforeEmptyBitmapPublish = tracker.getChangeList().size();

  // Publish items with empty bitmap while consumer1 is pended
  tracker.publishChange(emptyBitmapObj1, emptyBitmap);
  tracker.publishChange(emptyBitmapObj2, emptyBitmap);
  tracker.publishChange(emptyBitmapObj3, emptyBitmap);

  // Run the event loop briefly
  for (int i = 0; i < 10; ++i) {
    evb.loopOnce(EVLOOP_NONBLOCK);
  }

  /*
   * Check that items with empty bitmap were cleaned up (or at least didn't
   * increase the list size significantly)
   * The change list should not grow indefinitely due to empty bitmap items
   */
  size_t listSizeAfter = tracker.getChangeList().size();

  /*
   * The empty bitmap items should have been cleaned up, so list size should
   * not have grown by 3 (the number of empty bitmap items we published)
   */
  EXPECT_LE(listSizeAfter, listSizeBeforeEmptyBitmapPublish)
      << "Items published with empty consumer bitmap should be cleaned up "
         "immediately and not accumulate in the change list. "
         "List size before: "
      << listSizeBeforeEmptyBitmapPublish << ", after: " << listSizeAfter;

  // Verify that objects published with empty bitmap are not on the change list
  EXPECT_FALSE(emptyBitmapObj1->isOnChangeList())
      << "Empty bitmap object 1 should not be on change list";
  EXPECT_FALSE(emptyBitmapObj2->isOnChangeList())
      << "Empty bitmap object 2 should not be on change list";
  EXPECT_FALSE(emptyBitmapObj3->isOnChangeList())
      << "Empty bitmap object 3 should not be on change list";

  // Resume consumer1 to allow normal processing to complete
  consumer1->setPendOnValues({});
  consumer1->resume();

  // Run until all consumers finish processing
  success = runUntil(
      evb,
      [&]() {
        return consumer1->getProcessedItems().size() >= 1 &&
            consumer2->getProcessedItems().size() >= 1;
      },
      std::chrono::seconds(5),
      "Timeout waiting for consumers to finish processing");
  ASSERT_TRUE(success);

  // Signal the consumers to terminate
  consumer1->terminate();
  consumer2->terminate();

  // Deregister the consumers
  consumer1->deregisterFromTracker();
  consumer2->deregisterFromTracker();

  // Cancel the futures
  future1.cancel();
  future2.cancel();

  // Run the event loop to process any pending events
  evb.loop();
}

/*
 * Test that demonstrates O(1) removal of consumers from pending list
 * (T250391172) This test verifies that removePendingConsumer uses iterator_to()
 * for O(1) removal instead of std::find which was O(n)
 */
TEST(ChangeTrackerTest, PendingConsumerRemovalIsO1) {
  // Create a change tracker
  ChangeTracker<TestObject> tracker("TestTracker");

  // Create a producer
  TestProducer producer(tracker);

  // Create an object
  auto* object1 = producer.createObject(1);

  /*
   * Create multiple consumers - having many consumers helps demonstrate the
   * issue With O(n) removal and many consumers, removal would be slow With O(1)
   * removal via intrusive list, it's always fast
   */
  std::vector<std::shared_ptr<TestConsumer>> consumers;
  const int numConsumers = 100;

  for (int i = 0; i < numConsumers; i++) {
    auto consumer =
        std::make_shared<TestConsumer>(tracker, "Consumer" + std::to_string(i));
    consumer->setPendOnValues({1}); // All consumers will pend on value 1
    consumer->registerWithTracker();
    consumer->setPolledMode();
    consumers.push_back(consumer);
  }

  // Create an event base for scheduling coroutines
  folly::EventBase evb;

  // Start all consumer coroutines
  std::vector<folly::SemiFuture<folly::Unit>> futures;
  futures.reserve(consumers.size());
  for (auto& consumer : consumers) {
    futures.push_back(
        co_withExecutor(
            &evb,
            folly::coro::co_invoke([&consumer]() -> folly::coro::Task<void> {
              co_await consumer->consumeChanges();
            }))
            .start());
  }

  // Publish the change
  producer.publishChange(object1);

  // Run the event loop until all consumers are pended
  bool success = runUntil(
      evb,
      [&]() {
        for (auto& consumer : consumers) {
          if (consumer->getPendedOnItem() == nullptr) {
            return false;
          }
        }
        return true;
      },
      std::chrono::seconds(5),
      "Timeout waiting for all consumers to pend");
  ASSERT_TRUE(success);

  // Verify all consumers are in the pending list using intrusive hook
  auto* item = consumers[0]->getPendedOnItem();
  ASSERT_NE(item, nullptr);

  // All consumers should have their hooks linked (in the pending list)
  for (auto& consumer : consumers) {
    EXPECT_TRUE(consumer->pendingConsumerHook.is_linked())
        << "Consumer should be in pending list via intrusive hook";
  }

  // Verify the pending list size matches
  EXPECT_EQ(item->pendingConsumers.size(), static_cast<size_t>(numConsumers));

  /*
   * Now remove a consumer from the middle - this should be O(1) with intrusive
   * list Previously with std::find + erase on std::list, this was O(n)
   */
  auto& middleConsumer = consumers[numConsumers / 2];
  EXPECT_TRUE(middleConsumer->pendingConsumerHook.is_linked());

  // Remove the consumer - this uses iterator_to() for O(1) removal
  item->removePendingConsumer(middleConsumer);

  // Verify the hook is now unlinked
  EXPECT_FALSE(middleConsumer->pendingConsumerHook.is_linked())
      << "Consumer hook should be unlinked after removal";

  // Verify the list size decreased
  EXPECT_EQ(
      item->pendingConsumers.size(), static_cast<size_t>(numConsumers - 1));

  // Verify other consumers are still linked
  for (int i = 0; i < numConsumers; i++) {
    if (i == numConsumers / 2) {
      EXPECT_FALSE(consumers[i]->pendingConsumerHook.is_linked());
    } else {
      EXPECT_TRUE(consumers[i]->pendingConsumerHook.is_linked());
    }
  }

  // Clean up - deregister all consumers
  for (auto& consumer : consumers) {
    consumer->deregisterFromTracker();
  }
}

/*
 * Tests for consumerResetChangeList API
 *
 * This API is extracted from unregisterConsumer and can be called
 * independently. It walks the change list from the consumer's marker
 * position, removing the consumer from pending lists and marking it
 * as processed on all remaining items.
 */

/* Consumer pended at marker with subsequent items gets all items processed */
TEST(ChangeTrackerTest, ConsumerResetChangeList_PendedConsumer) {
  ChangeTracker<TestObject> tracker("ResetTracker");
  TestProducer producer(tracker);

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  /* Consumer yields on value 2 so it pends there */
  auto consumer = std::make_shared<TestConsumer>(tracker, "ResetConsumer");
  consumer->setPendOnValues({2});
  consumer->registerWithTracker();

  /* Publish all objects */
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  /* Consume with iterator - will process 1, then yield on 2 */
  consumer->iterateChangesToEnd();

  /* Verify consumer processed item 1 and is pended on item 2 */
  ASSERT_EQ(consumer->getProcessedItems().size(), 1);
  EXPECT_EQ(consumer->getProcessedItems()[0], 1);
  ASSERT_NE(consumer->getMarker(), nullptr);
  EXPECT_EQ(consumer->getMarker()->getTypedObject().getValue(), 2);

  /* Track producer notifications */
  std::vector<int> producerNotifications;
  tracker.setGlobalOnChangeProcessedCallback(
      [&producerNotifications](TrackableObject<TestObject>* obj) {
        producerNotifications.push_back(obj->get().getValue());
      });

  /* Call consumerResetChangeList directly */
  tracker.consumerResetChangeList(consumer);

  /* Verify that items 2 and 3 were marked as processed */
  EXPECT_TRUE(tracker.getChangeList().empty())
      << "All items should be removed after single consumer reset";

  /* Verify producer was notified for the remaining items */
  ASSERT_GE(producerNotifications.size(), 2);

  consumer->deregisterFromTracker();
}

/* Consumer with no marker (nullopt) - should be a no-op */
TEST(ChangeTrackerTest, ConsumerResetChangeList_NoMarker) {
  ChangeTracker<TestObject> tracker("ResetTracker");

  auto consumer = std::make_shared<TestConsumer>(tracker, "NoMarkerConsumer");
  consumer->registerWithTracker();

  /* Consumer is freshly registered, marker is nullptr by default */
  ASSERT_EQ(consumer->getMarker(), nullptr);

  /* Should not crash or do anything */
  tracker.consumerResetChangeList(consumer);

  /* Still no marker */
  ASSERT_EQ(consumer->getMarker(), nullptr);

  consumer->deregisterFromTracker();
}

/* Consumer with null marker (ready state) - should be a no-op */
TEST(ChangeTrackerTest, ConsumerResetChangeList_NullMarker) {
  ChangeTracker<TestObject> tracker("ResetTracker");
  TestProducer producer(tracker);

  auto* object1 = producer.createObject(1);

  auto consumer = std::make_shared<TestConsumer>(tracker, "ReadyConsumer");
  consumer->registerWithTracker();

  /* Publish and consume everything so consumer reaches ready state */
  producer.publishChange(object1);
  consumer->iterateChangesToEnd();

  /* Verify consumer is in ready state (marker == nullptr) */
  ASSERT_EQ(consumer->getMarker(), nullptr);
  ASSERT_TRUE(consumer->isReady());

  /* Should not crash or do anything */
  tracker.consumerResetChangeList(consumer);

  /* Consumer should still be in ready state */
  ASSERT_TRUE(consumer->isReady());

  consumer->deregisterFromTracker();
}

/* Null consumer pointer - should be a no-op */
TEST(ChangeTrackerTest, ConsumerResetChangeList_NullConsumer) {
  ChangeTracker<TestObject> tracker("ResetTracker");

  /* Should not crash with null consumer */
  std::shared_ptr<Consumer<TestObject>> nullConsumer = nullptr;
  tracker.consumerResetChangeList(nullConsumer);
}

/* Multiple consumers, reset one while others still need to process */
TEST(ChangeTrackerTest, ConsumerResetChangeList_MultipleConsumers) {
  ChangeTracker<TestObject> tracker("ResetTracker");
  TestProducer producer(tracker);

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  /* Create two consumers, both yield on value 2 */
  auto consumer1 = std::make_shared<TestConsumer>(tracker, "Consumer1");
  auto consumer2 = std::make_shared<TestConsumer>(tracker, "Consumer2");
  consumer1->setPendOnValues({2});
  consumer2->setPendOnValues({2});
  consumer1->registerWithTracker();
  consumer2->registerWithTracker();

  /* Publish all objects */
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  /* Both consumers consume - both will process 1, yield on 2 */
  consumer1->iterateChangesToEnd();
  consumer2->iterateChangesToEnd();

  ASSERT_EQ(consumer1->getProcessedItems().size(), 1);
  ASSERT_EQ(consumer2->getProcessedItems().size(), 1);

  /* Reset consumer1 only */
  tracker.consumerResetChangeList(consumer1);

  /* Items should still be on the change list because consumer2 hasn't
   * processed them */
  EXPECT_FALSE(tracker.getChangeList().empty())
      << "Items should remain because consumer2 still needs to process them";

  /* Now let consumer2 finish processing */
  consumer2->setPendOnValues({});
  consumer2->iterateChangesToEnd();

  /* Now the change list should be empty */
  EXPECT_TRUE(tracker.getChangeList().empty())
      << "All items should be removed after both consumers finish";

  consumer1->deregisterFromTracker();
  consumer2->deregisterFromTracker();
}

/* Consumer pended on the last item in the list */
TEST(ChangeTrackerTest, ConsumerResetChangeList_PendedOnLastItem) {
  ChangeTracker<TestObject> tracker("ResetTracker");
  TestProducer producer(tracker);

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  /* Consumer yields on value 3 (last item) */
  auto consumer = std::make_shared<TestConsumer>(tracker, "LastItemConsumer");
  consumer->setPendOnValues({3});
  consumer->registerWithTracker();

  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  consumer->iterateChangesToEnd();

  /* Verify consumer processed 1, 2 and is pended on 3 */
  ASSERT_EQ(consumer->getProcessedItems().size(), 2);
  ASSERT_NE(consumer->getMarker(), nullptr);
  EXPECT_EQ(consumer->getMarker()->getTypedObject().getValue(), 3);

  std::vector<int> producerNotifications;
  tracker.setGlobalOnChangeProcessedCallback(
      [&producerNotifications](TrackableObject<TestObject>* obj) {
        producerNotifications.push_back(obj->get().getValue());
      });

  tracker.consumerResetChangeList(consumer);

  /* Only item 3 should have been processed by reset */
  EXPECT_TRUE(tracker.getChangeList().empty());
  ASSERT_EQ(producerNotifications.size(), 1);
  EXPECT_EQ(producerNotifications[0], 3);

  consumer->deregisterFromTracker();
}

/* Consumer pended on the first item (all items still pending) */
TEST(ChangeTrackerTest, ConsumerResetChangeList_PendedOnFirstItem) {
  ChangeTracker<TestObject> tracker("ResetTracker");
  TestProducer producer(tracker);

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  /* Consumer yields on value 1 (first item) */
  auto consumer = std::make_shared<TestConsumer>(tracker, "FirstItemConsumer");
  consumer->setPendOnValues({1});
  consumer->registerWithTracker();

  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  consumer->iterateChangesToEnd();

  /* Verify consumer processed nothing and is pended on item 1 */
  ASSERT_EQ(consumer->getProcessedItems().size(), 0);
  ASSERT_NE(consumer->getMarker(), nullptr);
  EXPECT_EQ(consumer->getMarker()->getTypedObject().getValue(), 1);

  std::vector<int> producerNotifications;
  tracker.setGlobalOnChangeProcessedCallback(
      [&producerNotifications](TrackableObject<TestObject>* obj) {
        producerNotifications.push_back(obj->get().getValue());
      });

  tracker.consumerResetChangeList(consumer);

  /* All 3 items should be processed */
  EXPECT_TRUE(tracker.getChangeList().empty());
  ASSERT_EQ(producerNotifications.size(), 3);

  consumer->deregisterFromTracker();
}

/* Calling consumerResetChangeList twice is safe (idempotent on second call) */
TEST(ChangeTrackerTest, ConsumerResetChangeList_CalledTwice) {
  ChangeTracker<TestObject> tracker("ResetTracker");
  TestProducer producer(tracker);

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);

  auto consumer =
      std::make_shared<TestConsumer>(tracker, "DoubleResetConsumer");
  consumer->setPendOnValues({1});
  consumer->registerWithTracker();

  producer.publishChange(object1);
  producer.publishChange(object2);

  consumer->iterateChangesToEnd();

  /* First reset */
  tracker.consumerResetChangeList(consumer);
  EXPECT_TRUE(tracker.getChangeList().empty());

  /* Second reset should be safe - marker is nullptr so it's a no-op */
  EXPECT_EQ(consumer->getMarker(), nullptr);
  tracker.consumerResetChangeList(consumer);

  consumer->deregisterFromTracker();
}

/* Verify unregisterConsumer still works correctly after the refactor */
TEST(ChangeTrackerTest, ConsumerResetChangeList_UnregisterStillWorks) {
  ChangeTracker<TestObject> tracker("ResetTracker");
  TestProducer producer(tracker);

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  auto consumer = std::make_shared<TestConsumer>(tracker, "UnregisterConsumer");
  consumer->setPendOnValues({2});
  consumer->registerWithTracker();

  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  consumer->iterateChangesToEnd();

  /* Verify consumer is pended on item 2 */
  ASSERT_EQ(consumer->getProcessedItems().size(), 1);
  ASSERT_NE(consumer->getMarker(), nullptr);

  /* Deregistering should clean up via consumerResetChangeList */
  consumer->deregisterFromTracker();

  /* All items should be processed since no consumers remain */
  EXPECT_TRUE(tracker.getChangeList().empty());
}

/* Single item in the change list, consumer pended on it */
TEST(ChangeTrackerTest, ConsumerResetChangeList_SingleItem) {
  ChangeTracker<TestObject> tracker("ResetTracker");
  TestProducer producer(tracker);

  auto* object1 = producer.createObject(1);

  auto consumer = std::make_shared<TestConsumer>(tracker, "SingleItemConsumer");
  consumer->setPendOnValues({1});
  consumer->registerWithTracker();

  producer.publishChange(object1);

  consumer->iterateChangesToEnd();

  ASSERT_EQ(consumer->getProcessedItems().size(), 0);
  ASSERT_NE(consumer->getMarker(), nullptr);

  tracker.consumerResetChangeList(consumer);

  EXPECT_TRUE(tracker.getChangeList().empty());

  consumer->deregisterFromTracker();
}

TEST(ChangeTrackerTest, JoinConsumerWithExistingMarkerAborts) {
  ChangeTracker<TestObject> tracker("JoinAbortTracker");
  TestProducer producer(tracker);

  auto consumer1 = std::make_shared<TestConsumer>(tracker, "Consumer1");
  consumer1->registerWithTracker();

  auto consumer2 = std::make_shared<TestConsumer>(tracker, "Consumer2");
  consumer2->registerWithTracker();
  consumer2->setPendOnValues({1});

  auto* object1 = producer.createObject(1);
  producer.publishChange(object1);

  // Synchronously consume — consumer2 pends on value 1
  consumer2->iterateChangesToEnd();

  auto* markerBefore = consumer2->getMarker();
  ASSERT_NE(markerBefore, nullptr);

  // joinConsumer should abort — marker must remain unchanged
  tracker.joinConsumer(consumer1, consumer2);

  auto* markerAfter = consumer2->getMarker();
  EXPECT_EQ(markerBefore, markerAfter);

  consumer1->terminate();
  consumer2->terminate();
  consumer1->deregisterFromTracker();
  consumer2->deregisterFromTracker();
}

/*
 * Test that double-registering the same consumer without deregistering first
 * is a no-op. The guard in registerConsumer() must detect that the consumer
 * already holds a valid bitPosition and return the existing position instead
 * of allocating a new one (which would corrupt bitmaps).
 */
TEST(ChangeTrackerTest, DoubleRegistrationPrevented) {
  ChangeTracker<TestObject> tracker("DoubleRegTracker");
  TestProducer producer(tracker);

  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer1");

  /* First registration — should succeed normally */
  consumer->registerWithTracker();
  size_t firstBit = consumer->getBitPosition();
  EXPECT_NE(firstBit, static_cast<size_t>(-1))
      << "First registration must assign a valid bit position";

  /* Second registration without deregistering — must be a no-op */
  consumer->registerWithTracker();
  size_t secondBit = consumer->getBitPosition();
  EXPECT_EQ(firstBit, secondBit)
      << "Double registration must return the same bit position, not allocate a new one";

  /* Publish a change and consume via iterator */
  auto* object1 = producer.createObject(42);
  producer.publishChange(object1);

  consumer->iterateChangesToEnd();

  /* Must have processed exactly 1 item, not 2 (which would happen with
   * two bit positions both needing to clear their bits) */
  EXPECT_EQ(consumer->getProcessedItems().size(), 1);
  EXPECT_EQ(consumer->getProcessedItems()[0], 42);

  consumer->deregisterFromTracker();
}

/*
 * ============================================================
 * Consumer Marker Staleness Detection Tests
 * ============================================================
 */

TEST(ChangeTrackerTest, StalenessNotStaleWhenMarkerIsNull) {
  ChangeTracker<TestObject> tracker("StalenessNullMarkerTracker");
  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer");
  consumer->registerWithTracker();

  EXPECT_TRUE(consumer->isReady());
  EXPECT_FALSE(consumer->isStale(std::chrono::milliseconds(0)));

  consumer->deregisterFromTracker();
}

TEST(ChangeTrackerTest, StalenessNotStaleImmediatelyAfterPublish) {
  ChangeTracker<TestObject> tracker("StalenessImmediateTracker");
  TestProducer producer(tracker);
  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer");
  consumer->setPendOnValues({1});
  consumer->registerWithTracker();

  auto* object = producer.createObject(1);
  producer.publishChange(object);

  consumer->iterateChangesToEnd();

  ASSERT_NE(consumer->getMarker(), nullptr);
  EXPECT_FALSE(consumer->isStale(std::chrono::seconds(60)));

  consumer->deregisterFromTracker();
}

TEST(ChangeTrackerTest, StalenessDetectedAfterThresholdElapsed) {
  ChangeTracker<TestObject> tracker("StalenessThresholdTracker");
  TestProducer producer(tracker);
  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer");
  consumer->setPendOnValues({1});
  consumer->registerWithTracker();

  auto* object = producer.createObject(1);
  producer.publishChange(object);

  consumer->iterateChangesToEnd();

  ASSERT_NE(consumer->getMarker(), nullptr);
  /* Backdate timestamp by 11 minutes to simulate elapsed time */
  consumer->backdateMarkerAdvanceTime(std::chrono::minutes(11));
  EXPECT_TRUE(consumer->isStale(std::chrono::minutes(10)));

  consumer->deregisterFromTracker();
}

TEST(ChangeTrackerTest, StalenessDurationReturnsReasonableValue) {
  ChangeTracker<TestObject> tracker("StalenessDurationTracker");
  TestProducer producer(tracker);
  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer");
  consumer->setPendOnValues({1});
  consumer->registerWithTracker();

  auto* object = producer.createObject(1);
  producer.publishChange(object);

  consumer->iterateChangesToEnd();

  ASSERT_NE(consumer->getMarker(), nullptr);
  /* Backdate timestamp by 5 minutes to simulate elapsed time */
  consumer->backdateMarkerAdvanceTime(std::chrono::minutes(5));
  auto duration = consumer->stalenessDuration();
  EXPECT_GE(
      duration.count(),
      std::chrono::milliseconds(std::chrono::minutes(5)).count());

  consumer->deregisterFromTracker();
}

TEST(ChangeTrackerTest, StalenessLoggedFlagBehavior) {
  ChangeTracker<TestObject> tracker("StalenessLoggedFlagTracker");
  TestProducer producer(tracker);
  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer");
  consumer->setPendOnValues({1});
  consumer->registerWithTracker();

  auto* object = producer.createObject(1);
  producer.publishChange(object);

  consumer->iterateChangesToEnd();

  EXPECT_FALSE(consumer->isStalenessLogged());
  consumer->markStalenessLogged();
  EXPECT_TRUE(consumer->isStalenessLogged());

  consumer->deregisterFromTracker();
}

TEST(ChangeTrackerTest, StalenessResetOnMarkerAdvance) {
  ChangeTracker<TestObject> tracker("StalenessResetTracker");
  TestProducer producer(tracker);
  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer");
  consumer->setPendOnValues({2});
  consumer->registerWithTracker();

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  producer.publishChange(object1);
  producer.publishChange(object2);

  consumer->iterateChangesToEnd();

  /* Consumer processed item 1, then yielded on item 2. */
  ASSERT_EQ(consumer->getProcessedItems().size(), 1);
  ASSERT_NE(consumer->getMarker(), nullptr);

  /* Mark staleness as logged to verify it gets reset */
  consumer->markStalenessLogged();
  EXPECT_TRUE(consumer->isStalenessLogged());

  /* Resume consumption — marker will advance */
  consumer->setPendOnValues({});
  consumer->iterateChangesToEnd();

  /* After processing item 2, marker becomes null (ready state) */
  EXPECT_TRUE(consumer->isReady());
  EXPECT_FALSE(consumer->isStale(std::chrono::milliseconds(0)));
  /* recordMarkerAdvance() should have cleared the logged flag */
  EXPECT_FALSE(consumer->isStalenessLogged());

  consumer->deregisterFromTracker();
}

TEST(ChangeTrackerTest, StalenessResetOnNewItemsAfterReady) {
  ChangeTracker<TestObject> tracker("StalenessReadyResetTracker");
  TestProducer producer(tracker);
  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer");
  consumer->registerWithTracker();

  /* Publish and consume — consumer reaches ready state */
  auto* object1 = producer.createObject(1);
  producer.publishChange(object1);
  consumer->iterateChangesToEnd();
  EXPECT_TRUE(consumer->isReady());

  /* Now publish a new item — notifyReadyConsumers will call
   * recordMarkerAdvance() setting a fresh timestamp */
  consumer->setPendOnValues({2});
  auto* object2 = producer.createObject(2);
  producer.publishChange(object2);

  /* Consumer has a marker now (pended by notifyReadyConsumers) */
  EXPECT_FALSE(consumer->isReady());
  /* Should not be stale yet — threshold is large */
  EXPECT_FALSE(consumer->isStale(std::chrono::seconds(60)));

  consumer->deregisterFromTracker();
}

TEST(ChangeTrackerTest, StalenessNotResetWhenConsumerYieldsWithoutProgress) {
  ChangeTracker<TestObject> tracker("StalenessYieldNoProgressTracker");
  TestProducer producer(tracker);
  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer");
  consumer->setPendOnValues({1});
  consumer->registerWithTracker();

  auto* object = producer.createObject(1);
  producer.publishChange(object);

  /* First consumption: yields on item 1 without processing anything */
  consumer->iterateChangesToEnd();
  ASSERT_NE(consumer->getMarker(), nullptr);

  /* Backdate timestamp to simulate 11 minutes of being stuck */
  consumer->backdateMarkerAdvanceTime(std::chrono::minutes(11));
  EXPECT_TRUE(consumer->isStale(std::chrono::minutes(10)));

  /* Second consumption: still yields on the same item — no progress */
  consumer->iterateChangesToEnd();

  /* Staleness clock must NOT have been reset since marker didn't move */
  EXPECT_TRUE(consumer->isStale(std::chrono::minutes(10)));

  consumer->deregisterFromTracker();
}

TEST(ChangeTrackerTest, StalenessNotStaleWhileConsumerMakesProgress) {
  ChangeTracker<TestObject> tracker("StalenessProgressTracker");
  TestProducer producer(tracker);
  auto consumer = std::make_shared<TestConsumer>(tracker, "Consumer");
  consumer->registerWithTracker();

  /* Publish several items */
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  /* Consume all items — marker advances through each */
  consumer->iterateChangesToEnd();

  ASSERT_EQ(consumer->getProcessedItems().size(), 3);
  EXPECT_TRUE(consumer->isReady());
  /* Ready consumer is never stale, even with 0ms threshold */
  EXPECT_FALSE(consumer->isStale(std::chrono::milliseconds(0)));

  consumer->deregisterFromTracker();
}

/*
 * iterateChangesUntilExcluding stops before the boundary item
 * (exclusive). Publish 5 items, boundary at item 3. Items 1-2 should be
 * processed, marker left at item 3.
 */
TEST(ChangeTrackerTest, ConsumeWithIteratorUntil_StopsAtBoundary) {
  ChangeTracker<TestObject> tracker("UntilBoundaryTracker");
  TestProducer producer(tracker);

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);
  auto* object4 = producer.createObject(4);
  auto* object5 = producer.createObject(5);

  auto consumer =
      std::make_shared<TestConsumer>(tracker, "UntilBoundaryConsumer");
  consumer->registerWithTracker();

  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);
  producer.publishChange(object4);
  producer.publishChange(object5);

  // Walk to item 3 as boundary
  auto* item = consumer->getMarker();
  ASSERT_NE(item, nullptr);
  ASSERT_EQ(item->getTypedObject().getValue(), 1);
  item = get_next(item, tracker.getChangeList());
  ASSERT_EQ(item->getTypedObject().getValue(), 2);
  item = get_next(item, tracker.getChangeList());
  ASSERT_EQ(item->getTypedObject().getValue(), 3);
  auto* boundary = item;

  consumer->iterateChangesUntilExcluding(boundary);

  // Items 1, 2 processed; item 3 NOT processed (exclusive boundary)
  const std::vector<int> expectedProcessed{1, 2};
  EXPECT_EQ(consumer->getProcessedItems(), expectedProcessed);

  // Marker should be on item 3 (the boundary, not past it)
  ASSERT_NE(consumer->getMarker(), nullptr);
  EXPECT_EQ(consumer->getMarker()->getTypedObject().getValue(), 3);

  EXPECT_FALSE(consumer->isReady());

  consumer->deregisterFromTracker();
}

/*
 * iterateChangesUntilExcluding with nullptr consumes all items,
 * matching the behavior of iterateChanges.
 */
TEST(ChangeTrackerTest, ConsumeWithIteratorUntil_NullptrConsumesAll) {
  ChangeTracker<TestObject> tracker("UntilNullptrTracker");
  TestProducer producer(tracker);

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  auto consumer =
      std::make_shared<TestConsumer>(tracker, "UntilNullptrConsumer");
  consumer->registerWithTracker();

  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  consumer->iterateChangesUntilExcluding(nullptr);

  const auto& processed = consumer->getProcessedItems();
  ASSERT_EQ(processed.size(), 3);
  EXPECT_EQ(processed[0], 1);
  EXPECT_EQ(processed[1], 2);
  EXPECT_EQ(processed[2], 3);

  EXPECT_TRUE(consumer->isReady());

  consumer->deregisterFromTracker();
}

/*
 * iterateChangesUntilExcluding with boundary at the last item
 * processes items 1-2, leaves marker at item 3 (exclusive).
 */
TEST(ChangeTrackerTest, ConsumeWithIteratorUntil_BoundaryAtLastItem) {
  ChangeTracker<TestObject> tracker("UntilLastItemTracker");
  TestProducer producer(tracker);

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  auto consumer =
      std::make_shared<TestConsumer>(tracker, "UntilLastItemConsumer");
  consumer->registerWithTracker();

  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  // Find the last item (item 3)
  auto* item = consumer->getMarker();
  while (get_next(item, tracker.getChangeList()) != nullptr) {
    item = get_next(item, tracker.getChangeList());
  }
  ASSERT_EQ(item->getTypedObject().getValue(), 3);

  consumer->iterateChangesUntilExcluding(item);

  const auto& processed = consumer->getProcessedItems();
  ASSERT_EQ(processed.size(), 2);
  EXPECT_EQ(processed[0], 1);
  EXPECT_EQ(processed[1], 2);

  // Marker at item 3 (boundary, not processed)
  ASSERT_NE(consumer->getMarker(), nullptr);
  EXPECT_EQ(consumer->getMarker()->getTypedObject().getValue(), 3);
  EXPECT_FALSE(consumer->isReady());

  consumer->deregisterFromTracker();
}

/*
 * iterateChangesUntilExcluding with boundary at the first item
 * processes nothing and leaves marker at item 1.
 */
TEST(ChangeTrackerTest, ConsumeWithIteratorUntil_BoundaryAtFirstItem) {
  ChangeTracker<TestObject> tracker("UntilFirstItemTracker");
  TestProducer producer(tracker);

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);

  auto consumer =
      std::make_shared<TestConsumer>(tracker, "UntilFirstItemConsumer");
  consumer->registerWithTracker();

  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  auto* boundary = consumer->getMarker();
  ASSERT_EQ(boundary->getTypedObject().getValue(), 1);

  consumer->iterateChangesUntilExcluding(boundary);

  // Nothing processed — boundary is at the first item (exclusive)
  const auto& processed = consumer->getProcessedItems();
  ASSERT_EQ(processed.size(), 0);

  // Marker stays at item 1
  ASSERT_NE(consumer->getMarker(), nullptr);
  EXPECT_EQ(consumer->getMarker()->getTypedObject().getValue(), 1);

  consumer->deregisterFromTracker();
}

/*
 * iterateChangesUntilExcluding respects YIELD even before boundary.
 * Consumer yields on item 2 when boundary is item 4. Only item 1 should
 * be processed, marker stays on item 2.
 */
TEST(ChangeTrackerTest, ConsumeWithIteratorUntil_YieldBeforeBoundary) {
  ChangeTracker<TestObject> tracker("UntilYieldTracker");
  TestProducer producer(tracker);

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);
  auto* object4 = producer.createObject(4);

  auto consumer = std::make_shared<TestConsumer>(tracker, "UntilYieldConsumer");
  consumer->setPendOnValues({2});
  consumer->registerWithTracker();

  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);
  producer.publishChange(object4);

  // Find item 4 as boundary
  auto* item = consumer->getMarker();
  while (item->getTypedObject().getValue() != 4) {
    item = get_next(item, tracker.getChangeList());
  }

  consumer->iterateChangesUntilExcluding(item);

  // Only item 1 processed (yielded on 2, before reaching boundary 4)
  const auto& processed = consumer->getProcessedItems();
  ASSERT_EQ(processed.size(), 1);
  EXPECT_EQ(processed[0], 1);

  // Marker stays on item 2 (the yield point)
  ASSERT_NE(consumer->getMarker(), nullptr);
  EXPECT_EQ(consumer->getMarker()->getTypedObject().getValue(), 2);

  consumer->deregisterFromTracker();
}

/*
 * ============================================================
 * attachToMarkerAhead Tests
 * ============================================================
 */

namespace {

/*
 * Advance `consumer` forward `steps` items using the iterator API, acking each
 * via markProcessed() WITHOUT invoking the process callback, then pend it at
 * the resulting marker. Used to deterministically set up "ahead"/"behind"
 * consumer positions for attachToMarkerAhead tests without polluting
 * getProcessedItems().
 */
void advanceConsumerBySteps(
    const std::shared_ptr<TestConsumer>& consumer,
    int steps) {
  auto* item = consumer->begin();
  for (int i = 0; i < steps && item != nullptr; ++i) {
    consumer->markProcessed(item);
    item = consumer->next();
  }
  consumer->end();
}

} // namespace

/*
 * Target ahead: currentConsumer fast-forwards to target's marker, acking (not
 * processing) every item in between. With both consumers having acked the
 * skipped prefix, those items become fully processed and are freed (producer
 * notified, in order). currentConsumer ends aligned with target and never runs
 * its process callback. This also exercises the get-next-before-ack ordering:
 * items are freed mid-walk, so the wrong order would use-after-free.
 */
TEST(ChangeTrackerTest, AttachToMarkerAhead_TargetAhead_Aligns) {
  std::vector<int> freed;
  ChangeTracker<TestObject> tracker("SkipTargetAheadTracker");
  TestProducer producer(tracker);

  auto current = std::make_shared<TestConsumer>(tracker, "Current");
  auto target = std::make_shared<TestConsumer>(tracker, "Target");
  current->registerWithTracker();
  target->registerWithTracker();

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);
  auto* object4 = producer.createObject(4);
  auto* object5 = producer.createObject(5);
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);
  producer.publishChange(object4);
  producer.publishChange(object5);

  // Move target ahead to item 3; current stays behind at item 1.
  advanceConsumerBySteps(target, 2);
  ASSERT_NE(target->getMarker(), nullptr);
  ASSERT_EQ(target->getMarker()->getTypedObject().getValue(), 3);

  tracker.setGlobalOnChangeProcessedCallback(
      [&freed](TrackableObject<TestObject>* obj) {
        if (obj) {
          freed.push_back(obj->get().getValue());
        }
      });

  tracker.attachToMarkerAhead(current, target);

  // current is now aligned at target's marker (item 3).
  ASSERT_NE(current->getMarker(), nullptr);
  EXPECT_EQ(current->getMarker(), target->getMarker());
  EXPECT_EQ(current->getMarker()->getTypedObject().getValue(), 3);

  // skip acks, never processes: current's callback was not invoked.
  EXPECT_TRUE(current->getProcessedItems().empty());

  // items 1 and 2 were acked by both consumers -> fully processed -> freed.
  const std::vector<int> expectedFreed{1, 2};
  EXPECT_EQ(freed, expectedFreed);
  EXPECT_EQ(tracker.getChangeList().size(), 3u);

  // current still owns the remaining items; the freed item is gone.
  EXPECT_TRUE(tracker.isConsumerSetOnTrackableObject(object3, current));
  EXPECT_FALSE(tracker.isConsumerSetOnTrackableObject(object1, current));

  current->deregisterFromTracker();
  target->deregisterFromTracker();
}

/*
 * Target at end of list (null marker): currentConsumer acks every remaining
 * item through to the end and becomes a ready consumer (null marker, in ready
 * list). With both consumers acking, all items are freed.
 */
TEST(ChangeTrackerTest, AttachToMarkerAhead_TargetReady_CurrentBecomesReady) {
  std::vector<int> freed;
  ChangeTracker<TestObject> tracker("SkipTargetReadyTracker");
  TestProducer producer(tracker);

  auto current = std::make_shared<TestConsumer>(tracker, "Current");
  auto target = std::make_shared<TestConsumer>(tracker, "Target");
  current->registerWithTracker();
  target->registerWithTracker();

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  // Advance target off the end: it becomes ready with a null marker.
  advanceConsumerBySteps(target, 3);
  ASSERT_TRUE(target->isReady());

  tracker.setGlobalOnChangeProcessedCallback(
      [&freed](TrackableObject<TestObject>* obj) {
        if (obj) {
          freed.push_back(obj->get().getValue());
        }
      });

  tracker.attachToMarkerAhead(current, target);

  // current walked to the end and is now ready.
  EXPECT_TRUE(current->isReady());
  EXPECT_EQ(current->getMarker(), nullptr);
  EXPECT_TRUE(current->isInReadyList());
  EXPECT_TRUE(current->getProcessedItems().empty());

  // all three items were acked by both consumers -> freed.
  const std::vector<int> expectedFreed{1, 2, 3};
  EXPECT_EQ(freed, expectedFreed);
  EXPECT_TRUE(tracker.getChangeList().empty());

  current->deregisterFromTracker();
  target->deregisterFromTracker();
}

/*
 * current and target already at the same marker: attachToMarkerAhead is a
 * no-op. Nothing is acked or freed and current's position is unchanged.
 */
TEST(ChangeTrackerTest, AttachToMarkerAhead_AlreadyAligned_NoOp) {
  std::vector<int> freed;
  ChangeTracker<TestObject> tracker("SkipAlignedTracker");
  TestProducer producer(tracker);

  auto current = std::make_shared<TestConsumer>(tracker, "Current");
  auto target = std::make_shared<TestConsumer>(tracker, "Target");
  current->registerWithTracker();
  target->registerWithTracker();

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  // Both consumers are pended at item 1 (same marker).
  ASSERT_EQ(current->getMarker(), target->getMarker());

  tracker.setGlobalOnChangeProcessedCallback(
      [&freed](TrackableObject<TestObject>* obj) {
        if (obj) {
          freed.push_back(obj->get().getValue());
        }
      });

  tracker.attachToMarkerAhead(current, target);

  // No-op: current still at item 1, nothing freed, callback never ran.
  ASSERT_NE(current->getMarker(), nullptr);
  EXPECT_EQ(current->getMarker()->getTypedObject().getValue(), 1);
  EXPECT_TRUE(freed.empty());
  EXPECT_EQ(tracker.getChangeList().size(), 3u);
  EXPECT_TRUE(current->getProcessedItems().empty());

  current->deregisterFromTracker();
  target->deregisterFromTracker();
}

/*
 * Target BEHIND current (target's marker is not reachable by walking forward):
 * current acks through to the end and becomes ready. Nothing is freed here
 * because target still owns every item (its bit stays set).
 */
TEST(ChangeTrackerTest, AttachToMarkerAhead_TargetBehind_CurrentBecomesReady) {
  ChangeTracker<TestObject> tracker("SkipTargetBehindTracker");
  TestProducer producer(tracker);

  auto current = std::make_shared<TestConsumer>(tracker, "Current");
  auto target = std::make_shared<TestConsumer>(tracker, "Target");
  current->registerWithTracker();
  target->registerWithTracker();

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);
  auto* object4 = producer.createObject(4);
  auto* object5 = producer.createObject(5);
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);
  producer.publishChange(object4);
  producer.publishChange(object5);

  // Move current ahead to item 3; target stays behind at item 1.
  advanceConsumerBySteps(current, 2);
  ASSERT_EQ(current->getMarker()->getTypedObject().getValue(), 3);
  ASSERT_EQ(target->getMarker()->getTypedObject().getValue(), 1);

  tracker.attachToMarkerAhead(current, target);

  // current walked off the end (never found the behind target) -> ready.
  EXPECT_TRUE(current->isReady());
  EXPECT_EQ(current->getMarker(), nullptr);
  EXPECT_TRUE(current->isInReadyList());
  EXPECT_TRUE(current->getProcessedItems().empty());

  // Nothing freed: target still owns every item.
  EXPECT_EQ(tracker.getChangeList().size(), 5u);

  current->deregisterFromTracker();
  target->deregisterFromTracker();
}

/*
 * attachToMarkerAhead is a safe no-op on invalid arguments: null current, null
 * target, or an unregistered current consumer.
 */
TEST(ChangeTrackerTest, AttachToMarkerAhead_InvalidArgs_NoOp) {
  ChangeTracker<TestObject> tracker("SkipInvalidArgsTracker");
  TestProducer producer(tracker);

  auto target = std::make_shared<TestConsumer>(tracker, "Target");
  target->registerWithTracker();
  auto* object1 = producer.createObject(1);
  producer.publishChange(object1);
  auto* markerBefore = target->getMarker();
  ASSERT_NE(markerBefore, nullptr);

  // Unregistered consumer: no registerWithTracker, so bitPosition == -1.
  auto unregistered = std::make_shared<TestConsumer>(tracker, "Unregistered");

  tracker.attachToMarkerAhead(nullptr, target);
  tracker.attachToMarkerAhead(target, nullptr);
  tracker.attachToMarkerAhead(unregistered, target);

  // target and the unregistered consumer are both untouched.
  EXPECT_EQ(target->getMarker(), markerBefore);
  EXPECT_EQ(unregistered->getMarker(), nullptr);

  target->deregisterFromTracker();
}

/*
 * current already at the end of the list (ready, null marker) when called:
 * attachToMarkerAhead is a no-op and current remains a ready consumer.
 */
TEST(ChangeTrackerTest, AttachToMarkerAhead_CurrentAlreadyReady_NoOp) {
  ChangeTracker<TestObject> tracker("SkipCurrentReadyTracker");
  TestProducer producer(tracker);

  auto current = std::make_shared<TestConsumer>(tracker, "Current");
  auto target = std::make_shared<TestConsumer>(tracker, "Target");
  current->registerWithTracker();
  target->registerWithTracker();

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  producer.publishChange(object1);
  producer.publishChange(object2);

  /*
   * Drive current to the end so it is ready with a null marker; target stays
   * behind at item 1.
   */
  advanceConsumerBySteps(current, 2);
  ASSERT_TRUE(current->isReady());
  ASSERT_EQ(current->getMarker(), nullptr);
  ASSERT_NE(target->getMarker(), nullptr);

  tracker.attachToMarkerAhead(current, target);

  // current is unchanged: still ready with a null marker.
  EXPECT_TRUE(current->isReady());
  EXPECT_EQ(current->getMarker(), nullptr);
  EXPECT_TRUE(current->isInReadyList());

  current->deregisterFromTracker();
  target->deregisterFromTracker();
}

/*
 * Target marker present but NOT owned by currentConsumer: object2 is published
 * selectively to target only, so target pends ahead at an item current's bit is
 * not on. The skip walks the raw change list and stops at target's marker by
 * pointer identity regardless of ownership, so current acks the prefix it does
 * own and parks at target's marker, aligned with target -- it does NOT overrun
 * to the ready list.
 */
TEST(
    ChangeTrackerTest,
    AttachToMarkerAhead_TargetMarkerNotOwnedByCurrent_ParksAtTarget) {
  ChangeTracker<TestObject> tracker("SkipTargetNotOwnedTracker");
  TestProducer producer(tracker);

  auto current = std::make_shared<TestConsumer>(tracker, "Current");
  auto target = std::make_shared<TestConsumer>(tracker, "Target");
  current->registerWithTracker();
  target->registerWithTracker();

  /*
   * object1 goes to both consumers; object2 is published to target only, so it
   * carries target's bit but not current's.
   */
  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  producer.publishChange(object1);

  ConsumerBitmap targetOnly;
  BitmapUtils::setBit(targetOnly, target->getBitPosition());
  tracker.publishChange(object2, targetOnly);

  /*
   * Move target ahead to object2 (the item current's bit is not on); current
   * stays pended at object1.
   */
  advanceConsumerBySteps(target, 1);
  ASSERT_NE(target->getMarker(), nullptr);
  ASSERT_EQ(target->getMarker()->getTypedObject().getValue(), 2);
  ASSERT_NE(current->getMarker(), nullptr);
  ASSERT_EQ(current->getMarker()->getTypedObject().getValue(), 1);

  tracker.attachToMarkerAhead(current, target);

  /*
   * The raw walk stops at target's marker by pointer identity even though
   * current does not own object2, so current acks object1 and parks at object2,
   * aligned with target -- without running its process callback.
   */
  EXPECT_FALSE(current->isReady());
  ASSERT_NE(current->getMarker(), nullptr);
  EXPECT_EQ(current->getMarker()->getTypedObject().getValue(), 2);
  EXPECT_EQ(current->getMarker(), target->getMarker());
  EXPECT_FALSE(current->isInReadyList());
  EXPECT_TRUE(current->getProcessedItems().empty());

  /*
   * object1 was acked by both consumers -> fully processed -> freed; object2
   * remains, owned by target and now also current's parked marker.
   */
  EXPECT_EQ(tracker.getChangeList().size(), 1u);
  ASSERT_NE(target->getMarker(), nullptr);
  EXPECT_EQ(target->getMarker()->getTypedObject().getValue(), 2);

  current->deregisterFromTracker();
  target->deregisterFromTracker();
}

/*
 * ============================================================
 * attachToMarkerBehind Tests
 * ============================================================
 */

namespace {

/*
 * Count how many times `consumer` appears in `item`'s pending list. Used to
 * verify pending-list membership and that no duplicate entry is created.
 */
size_t pendingListCount(
    ChangeItem<TestObject>* item,
    const std::shared_ptr<TestConsumer>& consumer) {
  size_t count = 0;
  for (auto& pending : item->pendingConsumers) {
    if (&pending == consumer.get()) {
      ++count;
    }
  }
  return count;
}

bool isInPendingList(
    ChangeItem<TestObject>* item,
    const std::shared_ptr<TestConsumer>& consumer) {
  return pendingListCount(item, consumer) > 0;
}

} // namespace

/*
 * attachToMarkerBehind core: source has advanced ahead (acking a prefix) while
 * target sits behind. source is rewound onto target's marker, re-owning every
 * item from target's position through source's old marker, and target is reset.
 * Nothing is freed because source now owns the whole tail target used to own.
 */
TEST(ChangeTrackerTest, AttachToMarkerBehind_RewindsSourceAndRetiresTarget) {
  std::vector<int> freed;
  ChangeTracker<TestObject> tracker("AttachBehindRewindTracker");
  TestProducer producer(tracker);

  auto source = std::make_shared<TestConsumer>(tracker, "Source");
  auto target = std::make_shared<TestConsumer>(tracker, "Target");
  source->registerWithTracker();
  target->registerWithTracker();

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);
  auto* object4 = producer.createObject(4);
  auto* object5 = producer.createObject(5);
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);
  producer.publishChange(object4);
  producer.publishChange(object5);

  /*
   * Move source ahead to item 3 (acking items 1-2 without processing them);
   * target stays behind at item 1. Items survive because target still owns
   * them.
   */
  advanceConsumerBySteps(source, 2);
  ASSERT_NE(source->getMarker(), nullptr);
  ASSERT_EQ(source->getMarker()->getTypedObject().getValue(), 3);
  ASSERT_EQ(target->getMarker()->getTypedObject().getValue(), 1);
  ASSERT_FALSE(tracker.isConsumerSetOnTrackableObject(object1, source));

  tracker.setGlobalOnChangeProcessedCallback(
      [&freed](TrackableObject<TestObject>* obj) {
        if (obj) {
          freed.push_back(obj->get().getValue());
        }
      });

  auto* item1 = target->getMarker();
  tracker.attachToMarkerBehind(source, target);

  // source is rewound onto target's old marker (item 1) and pended there.
  ASSERT_NE(source->getMarker(), nullptr);
  EXPECT_EQ(source->getMarker(), item1);
  EXPECT_EQ(source->getMarker()->getTypedObject().getValue(), 1);
  EXPECT_TRUE(isInPendingList(item1, source));
  EXPECT_FALSE(source->isInReadyList());

  /*
   * source re-owns every item from item 1 to the end -- including the prefix it
   * had already acked -- so it will reprocess them.
   */
  EXPECT_TRUE(tracker.isConsumerSetOnTrackableObject(object1, source));
  EXPECT_TRUE(tracker.isConsumerSetOnTrackableObject(object2, source));
  EXPECT_TRUE(tracker.isConsumerSetOnTrackableObject(object3, source));
  EXPECT_TRUE(tracker.isConsumerSetOnTrackableObject(object4, source));
  EXPECT_TRUE(tracker.isConsumerSetOnTrackableObject(object5, source));

  /*
   * target is reset: marker null, unpended, owns no item all the way to the
   * end.
   */
  EXPECT_EQ(target->getMarker(), nullptr);
  EXPECT_FALSE(isInPendingList(item1, target));
  EXPECT_FALSE(tracker.isConsumerSetOnTrackableObject(object1, target));
  EXPECT_FALSE(tracker.isConsumerSetOnTrackableObject(object3, target));
  EXPECT_FALSE(tracker.isConsumerSetOnTrackableObject(object5, target));

  // Nothing freed: source now owns the whole list target used to own.
  EXPECT_TRUE(freed.empty());
  EXPECT_EQ(tracker.getChangeList().size(), 5u);

  source->deregisterFromTracker();
  target->deregisterFromTracker();
}

/*
 * attachToMarkerBehind with a source that has run to the end (ready, null
 * marker). source owns nothing; the rewind sets its bit from target's marker
 * through the end of the list and pends it at target's marker.
 */
TEST(ChangeTrackerTest, AttachToMarkerBehind_ReadySourceRewindsToTarget) {
  ChangeTracker<TestObject> tracker("AttachBehindReadySourceTracker");
  TestProducer producer(tracker);

  auto source = std::make_shared<TestConsumer>(tracker, "Source");
  auto target = std::make_shared<TestConsumer>(tracker, "Target");
  source->registerWithTracker();
  target->registerWithTracker();

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  /*
   * Drive source off the end -> ready with a null marker; target stays at
   * item1.
   */
  advanceConsumerBySteps(source, 3);
  ASSERT_TRUE(source->isReady());
  ASSERT_TRUE(source->isInReadyList());
  auto* item1 = target->getMarker();
  ASSERT_NE(item1, nullptr);

  tracker.attachToMarkerBehind(source, target);

  /*
   * source leaves the ready list, pends at target's marker, and owns the whole
   * list again.
   */
  EXPECT_EQ(source->getMarker(), item1);
  EXPECT_FALSE(source->isReady());
  EXPECT_FALSE(source->isInReadyList());
  EXPECT_TRUE(isInPendingList(item1, source));
  EXPECT_TRUE(tracker.isConsumerSetOnTrackableObject(object1, source));
  EXPECT_TRUE(tracker.isConsumerSetOnTrackableObject(object2, source));
  EXPECT_TRUE(tracker.isConsumerSetOnTrackableObject(object3, source));

  // target reset.
  EXPECT_EQ(target->getMarker(), nullptr);
  EXPECT_FALSE(tracker.isConsumerSetOnTrackableObject(object1, target));

  EXPECT_EQ(tracker.getChangeList().size(), 3u);

  source->deregisterFromTracker();
  target->deregisterFromTracker();
}

/*
 * attachToMarkerBehind where source and target start at the SAME marker. source
 * keeps its position and re-owns the tail (its bit is already set there);
 * target is retired. Nothing is freed.
 */
TEST(ChangeTrackerTest, AttachToMarkerBehind_SameMarker_AbsorbsAndRetires) {
  ChangeTracker<TestObject> tracker("AttachBehindSameMarkerTracker");
  TestProducer producer(tracker);

  auto source = std::make_shared<TestConsumer>(tracker, "Source");
  auto target = std::make_shared<TestConsumer>(tracker, "Target");
  source->registerWithTracker();
  target->registerWithTracker();

  auto* object1 = producer.createObject(1);
  auto* object2 = producer.createObject(2);
  auto* object3 = producer.createObject(3);
  producer.publishChange(object1);
  producer.publishChange(object2);
  producer.publishChange(object3);

  auto* item1 = source->getMarker();
  ASSERT_EQ(source->getMarker(), target->getMarker());

  tracker.attachToMarkerBehind(source, target);

  // source unchanged at item1 and owns the whole list.
  EXPECT_EQ(source->getMarker(), item1);
  EXPECT_TRUE(isInPendingList(item1, source));
  EXPECT_TRUE(tracker.isConsumerSetOnTrackableObject(object1, source));
  EXPECT_TRUE(tracker.isConsumerSetOnTrackableObject(object3, source));

  // target retired: marker null, unpended, owns nothing.
  EXPECT_EQ(target->getMarker(), nullptr);
  EXPECT_FALSE(isInPendingList(item1, target));
  EXPECT_FALSE(tracker.isConsumerSetOnTrackableObject(object1, target));

  // source still owns everything -> nothing freed.
  EXPECT_EQ(tracker.getChangeList().size(), 3u);

  source->deregisterFromTracker();
  target->deregisterFromTracker();
}

/*
 * attachToMarkerBehind is a safe no-op on invalid arguments: null source, null
 * target, or an unregistered source. The valid consumer is left untouched (in
 * particular, target is NOT reset).
 */
TEST(ChangeTrackerTest, AttachToMarkerBehind_InvalidArgs_NoOp) {
  ChangeTracker<TestObject> tracker("AttachBehindInvalidArgsTracker");
  TestProducer producer(tracker);

  auto source = std::make_shared<TestConsumer>(tracker, "Source");
  auto target = std::make_shared<TestConsumer>(tracker, "Target");
  source->registerWithTracker();
  target->registerWithTracker();

  auto* object1 = producer.createObject(1);
  producer.publishChange(object1);
  auto* item1 = target->getMarker();
  ASSERT_NE(item1, nullptr);

  // Unregistered consumer: never registered, so bitPosition == -1.
  auto unregistered = std::make_shared<TestConsumer>(tracker, "Unregistered");

  std::shared_ptr<TestConsumer> nullConsumer;
  tracker.attachToMarkerBehind(nullConsumer, target);
  tracker.attachToMarkerBehind(source, nullConsumer);
  tracker.attachToMarkerBehind(unregistered, target);

  // target untouched (NOT reset), source untouched, unregistered untouched.
  EXPECT_EQ(target->getMarker(), item1);
  EXPECT_TRUE(isInPendingList(item1, target));
  EXPECT_TRUE(tracker.isConsumerSetOnTrackableObject(object1, target));
  EXPECT_EQ(source->getMarker(), item1);
  EXPECT_EQ(unregistered->getMarker(), nullptr);

  source->deregisterFromTracker();
  target->deregisterFromTracker();
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
