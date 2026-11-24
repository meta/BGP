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

#pragma once

#include <fmt/format.h>
#include <folly/container/IntrusiveList.h>
#include <folly/coro/Task.h>
#include <chrono>
#include <coroutine>

#include "neteng/fboss/bgp/cpp/changeTracker/ChangeTrackerDebug.h"
#include "neteng/fboss/bgp/cpp/changeTracker/ConsumerBitmap.h"

#include <folly/Function.h>

// Forward declarations
template <typename T>
class ChangeTracker;
template <typename T>
class ChangeItem;

/**
 * Consumer represents an entity that consumes changes from the ChangeList.
 * @tparam T The type of the object being tracked
 */
template <typename T>
class Consumer : public std::enable_shared_from_this<Consumer<T>> {
 public:
  // Enum to represent the result of processing a change item
  enum class ProcessResult {
    // Continue processing the next item
    CONTINUE,
    // Yield processing and add to pending consumers
    YIELD
  };

  // Callback type for change notifications - type-erased callback
  using ProcessChangeItemCallback =
      folly::Function<ProcessResult(ChangeItem<T>*)>;

  // Enum to represent the consumption mode
  enum class ConsumptionMode {
    /*
     * Event-driven mode: suspend when no items available, resume on
     * notification
     */
    TRIGGERED,
    // Timer-driven mode: process available items periodically, no suspension
    POLLED
  };

  explicit Consumer(ChangeTracker<T>& tracker, size_t bitPosition);
  virtual ~Consumer();

  /**
   * Copy constructor (deleted).
   */
  Consumer(const Consumer&) = delete;

  /**
   * Copy assignment operator (deleted).
   */
  Consumer& operator=(const Consumer&) = delete;

  /**
   * Move constructor (deleted).
   */
  Consumer(Consumer&&) = delete;

  /**
   * Move assignment operator (deleted).
   */
  Consumer& operator=(Consumer&&) = delete;

  // Register this consumer with the change tracker
  void registerWithTracker();

  // Deregister this consumer from the change tracker
  void deregisterFromTracker();

  // Start consuming changes
  folly::coro::Task<void> consumeChanges();

  // Resume consuming changes from the current marker
  virtual void resume();

  // Set the termination flag to cleanly exit the coroutine
  void terminate() {
    shouldTerminate_ = true;
    // Resume the coroutine if it's suspended
    resume();
  }

  // Check if the consumer is marked for termination
  bool shouldTerminate() const {
    return shouldTerminate_;
  }

  // Get the bit position of this consumer
  size_t getBitPosition() const {
    return bitPosition_;
  }

  // Get/set the current marker (the change item this consumer is currently at)
  ChangeItem<T>* getMarker() const {
    return marker_;
  }
  void setMarker(ChangeItem<T>* newMarker) {
    marker_ = newMarker;
  }

  // Check if this consumer is in READY state (has reached the end of the list)
  bool isReady() const {
    return marker_ == nullptr;
  }

  // Check if this consumer is in the ready list
  bool isInReadyList() const {
    return isInReadyList_;
  }

  void recordMarkerAdvance() {
    lastMarkerAdvanceTime_ = std::chrono::steady_clock::now();
    stalenessLogged_ = false;
  }

  bool isStale(std::chrono::milliseconds threshold) const {
    if (marker_ == nullptr) {
      return false;
    }
    return (std::chrono::steady_clock::now() - lastMarkerAdvanceTime_) >
        threshold;
  }

  std::chrono::milliseconds stalenessDuration() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - lastMarkerAdvanceTime_);
  }

  bool isStalenessLogged() const {
    return stalenessLogged_;
  }

  void markStalenessLogged() {
    stalenessLogged_ = true;
  }

  // Set the ready list flag (internal use by ChangeTracker)
  void setInReadyList(bool inReadyList) {
    isInReadyList_ = inReadyList;
  }

  // Set the process item callback
  void setProcessChangeItemCallback(ProcessChangeItemCallback callback) {
    processChangeItemCallback_ = std::move(callback);
  }

  // Set the consumption mode
  void setConsumptionMode(ConsumptionMode mode) {
    consumptionMode_ = mode;
  }

  // Get the consumption mode
  ConsumptionMode getConsumptionMode() const {
    return consumptionMode_;
  }

  // Set polled mode consumption
  void setPolledMode() {
    consumptionMode_ = ConsumptionMode::POLLED;
  }

  // Set triggered mode consumption
  void setTriggeredMode() {
    consumptionMode_ = ConsumptionMode::TRIGGERED;
  }

  /**
   * Iterator-based interface methods. These provide a polling-based
   * alternative to the coroutine-based consumeChanges()
   */

  /**
   * Begin consuming items from the current position.
   * Convenience method that simply calls next().
   *
   * Typical usage:
   *   auto* item = consumer->begin();
   *   while (item) {
   *     // Process item
   *     consumer->markProcessed(item);
   *     item = consumer->next();
   *   }
   *   consumer->end();
   */
  ChangeItem<T>* begin();

  /**
   * Get the current item for this consumer to process.
   * - Returns the item at the current marker position
   * - Returns nullptr if marker is null (consumer is ready, no items to
   * process)
   *
   * Note: Marker is set by the tracker when items are published to ready
   * consumers. After markProcessed(), marker is automatically advanced to the
   * next item.
   */
  ChangeItem<T>* next();

  /**
   * Mark an item as processed by this consumer.
   * CRITICAL SAFETY: This method FIRST advances the marker to the next item,
   * THEN clears the consumer bit. This ensures the item can be safely destroyed
   * immediately after the bit is cleared without invalidating the consumer's
   * position.
   *
   * @param item The item that has been processed
   */
  void markProcessed(ChangeItem<T>* item);

  /**
   * End processing session - registers consumer in pending list if yielding.
   * Call this when:
   * - Yielding (exiting loop without processing all items)
   * - Reached end of list naturally
   *
   * This ensures the consumer is properly tracked if an item is updated while
   * the consumer is yielded on it.
   */
  void end();

  /**
   * Consume changes up to but NOT including the specified boundary marker.
   *
   * @param untilMarker If non-null, stop when the current item equals
   *        untilMarker (exclusive — the boundary item is NOT processed).
   *        The consumer's marker will be left pointing at untilMarker.
   *        If nullptr, consume all available items (equivalent to
   *        iterateChangesToEnd()).
   *
   * PRECONDITION: processChangeItemCallback_ MUST be set via
   * setProcessChangeItemCallback() before calling this method.
   * This is a mandatory requirement - the callback will be invoked for each
   * item without checking if it's set.
   *
   * If you need manual control over processing without a callback, use the
   * iterator methods (begin(), next(), markProcessed(), end()) directly.
   *
   * The method will:
   * 1. Call begin() to get first item
   * 2. Loop through items, calling processChangeItemCallback_ for each
   * 3. Stop BEFORE processing the untilMarker item (if non-null)
   * 4. Handle YIELD behavior (add to pending list and return)
   * 5. Call markProcessed() to advance to next item
   * 6. Call end() when done
   *
   * This should be used when enable_iterable_change_list_tracker flag is
   * enabled.
   */
  void iterateChangesUntilExcluding(ChangeItem<T>* untilMarker);

  /**
   * Consume every available change, leaving the marker at the end of the
   * change list. Convenience wrapper that calls
   * iterateChangesUntilExcluding(nullptr).
   *
   * Named for the guarantee callers depend on: with no boundary marker the
   * only thing that can stop the walk short is a consumer whose
   * processChangeItem() returns ProcessResult::YIELD. A consumer that never
   * yields is therefore guaranteed to be at the tail (isReady()) when this
   * returns, and callers need not re-check.
   *
   * This should be used when enable_iterable_change_list_tracker flag is
   * enabled.
   */
  void iterateChangesToEnd() {
    iterateChangesUntilExcluding(nullptr);
  }

  /**
   * Set the consumer display callback for debug tracing.
   * @param callback Function that returns string representation of this
   * consumer
   */
  void setConsumerDisplayCallback(
      facebook::neteng::fboss::bgp::changetracker::ChangeTrackerDebug::
          ConsumerDisplayCallback callback) {
    consumerDisplayCallback_ = std::move(callback);
  }

  /**
   * Get display string for this consumer (for debug tracing).
   * @return String representation of this consumer
   */
  std::string getDisplayString() const {
    if (consumerDisplayCallback_) {
      // Need to cast away const since folly::Function is not const-callable
      auto& callback =
          const_cast<facebook::neteng::fboss::bgp::changetracker::
                         ChangeTrackerDebug::ConsumerDisplayCallback&>(
              consumerDisplayCallback_);
      return callback();
    }
    return fmt::format("Consumer@{}", static_cast<const void*>(this));
  }

 protected:
  ChangeTracker<T>& tracker;

 public:
  // Hook for folly::intrusive::list
  folly::SafeIntrusiveListHook pendingConsumerHook;

 private:
  size_t bitPosition_;
  ChangeItem<T>* marker_ = nullptr;

  std::coroutine_handle<> pendingCoroutine_ =
      nullptr; // Handle to the suspended coroutine
  bool shouldTerminate_ =
      false; // Flag to indicate if the coroutine should terminate
  ConsumptionMode consumptionMode_ = ConsumptionMode::TRIGGERED; // Default mode
  ProcessChangeItemCallback
      processChangeItemCallback_; // Function callback for consumer to process
                                  // each change item
  bool isInReadyList_ = false; // Flag to track if consumer is in ready list

 protected:
  std::chrono::steady_clock::time_point lastMarkerAdvanceTime_{};
  bool stalenessLogged_{false};

 private:
  // Optional callback to display consumer info in debug traces
  facebook::neteng::fboss::bgp::changetracker::ChangeTrackerDebug::
      ConsumerDisplayCallback consumerDisplayCallback_;

  // Helper method to wait for new items
  folly::coro::Task<void> waitForNewItems();

  // Custom awaitable for suspending the coroutine
  struct SuspendAwaitable {
    Consumer<T>* consumer;

    bool await_ready() noexcept {
      return false;
    }

    void await_suspend(std::coroutine_handle<> h) noexcept {
      consumer->pendingCoroutine_ = h;
    }

    void await_resume() noexcept {}
  };

  // Friend class declarations
  friend class ChangeItem<T>;
};

// Implementation of Consumer methods
template <typename T>
Consumer<T>::Consumer(ChangeTracker<T>& tracker, size_t bitPosition)
    : tracker(tracker), bitPosition_(bitPosition) {}

template <typename T>
Consumer<T>::~Consumer() {
  // Deregister from the tracker if still registered
  if (bitPosition_ != static_cast<size_t>(-1)) {
    deregisterFromTracker();
  }
}

template <typename T>
void Consumer<T>::registerWithTracker() {
  // Register with the tracker
  bitPosition_ = tracker.registerConsumer(this->shared_from_this());

  /*
   * This needs to be done here to allow for deregistration to cancel coro in
   * triggered mode
   */
  shouldTerminate_ = false;
  // Clean up any pending coroutine state first
  if (pendingCoroutine_) {
    pendingCoroutine_ = nullptr;
  }
}

template <typename T>
void Consumer<T>::deregisterFromTracker() {
  // Deregister from the tracker
  tracker.unregisterConsumer(bitPosition_);
  bitPosition_ = static_cast<size_t>(-1);
  marker_ = nullptr;
}

template <typename T>
folly::coro::Task<void> Consumer<T>::consumeChanges() {
  CT_DEBUG_LOG(
      DBG3, "Starting consumeChanges() for consumer: {}", getDisplayString());

  // Common initialization
  if (marker_ != nullptr) {
    auto item = marker_;
    CT_DEBUG_LOG(
        DBG3,
        "Consumer {}: Removing from pending list of item: {}",
        getDisplayString(),
        tracker.getItemDisplayString(item));
    item->removePendingConsumer(this->shared_from_this());
  }

  /*
   * Initialize result to CONTINUE. In case if processChangeItemCallback is
   * not initialized, we just loop through all change items and be done with
   * the consumer
   */
  auto result = ProcessResult::CONTINUE;
  while (!shouldTerminate_) {
    CT_DEBUG_LOG(
        DBG3,
        "Consumer {}: Starting new iteration, current marker: {}",
        getDisplayString(),
        tracker.getItemDisplayString(marker_));

    // Handle empty list based on mode
    if (marker_ == nullptr) {
      CT_DEBUG_LOG(
          DBG3,
          "Consumer {}: Reached end of list, mode: {}",
          getDisplayString(),
          consumptionMode_ == ConsumptionMode::POLLED ? "POLLED" : "TRIGGERED");

      /*
       * we will either bail out, or suspend the coro
       * in either case go to ready state
       */
      tracker.addToReadyConsumers(this->shared_from_this());
      if (consumptionMode_ == ConsumptionMode::POLLED) {
        CT_DEBUG_LOG(
            DBG3,
            "Consumer {}: Exiting consumeChanges() in polled mode",
            getDisplayString());
        co_return; // Exit in polled mode
      } else {
        CT_DEBUG_LOG(
            DBG3,
            "Consumer {}: Waiting for new items in triggered mode",
            getDisplayString());
        co_await waitForNewItems(); // Wait in triggered mode
        CT_DEBUG_LOG(
            DBG3,
            "Consumer {}: Resumed from waiting, continuing iteration",
            getDisplayString());
        continue;
      }
    }

    // Common processing logic
    ChangeItem<T>* item = marker_;
    CT_DEBUG_LOG(
        DBG3,
        "Consumer {}: Processing item: {}",
        getDisplayString(),
        tracker.getItemDisplayString(item));

    // Skip items not meant for this consumer
    if (!BitmapUtils::isBitSet(item->consumerBitmap, bitPosition_)) {
      CT_DEBUG_LOG(
          DBG3,
          "Consumer {}: Skipping item (bit {} not set): {}",
          getDisplayString(),
          bitPosition_,
          tracker.getItemDisplayString(item));
      marker_ = get_next(item, tracker.getChangeList());
      continue;
    }

    CT_DEBUG_LOG(
        DBG3,
        "Consumer {}: Calling processChangeItem() for: {}",
        getDisplayString(),
        tracker.getItemDisplayString(item));

    // Process the current item
    if (processChangeItemCallback_) {
      result = processChangeItemCallback_(item);
    }

    CT_DEBUG_LOG(
        DBG3,
        "Consumer {}: processChangeItem() returned {} for item: {}",
        getDisplayString(),
        result == ProcessResult::CONTINUE ? "CONTINUE" : "YIELD",
        tracker.getItemDisplayString(item));

    // Handle YIELD based on mode
    if (result == ProcessResult::YIELD) {
      CT_DEBUG_LOG(
          DBG3,
          "Consumer {}: Yielded on item processing, mode: {}",
          getDisplayString(),
          consumptionMode_ == ConsumptionMode::POLLED ? "POLLED" : "TRIGGERED");
      if (consumptionMode_ == ConsumptionMode::POLLED) {
        // In polled mode, add to pending consumers but return (no suspension)
        CT_DEBUG_LOG(
            DBG3,
            "Consumer {}: Adding to pending list and exiting (polled mode)",
            getDisplayString());
        if (!pendingConsumerHook.is_linked()) {
          item->pendingConsumers.push_back(*this);
        }
        co_return;
      } else {
        // In triggered mode, suspend
        CT_DEBUG_LOG(
            DBG3,
            "Consumer {}: Adding to pending list and suspending (triggered mode)",
            getDisplayString());
        if (!pendingConsumerHook.is_linked()) {
          item->pendingConsumers.push_back(*this);
        }
        co_await SuspendAwaitable{this};
        CT_DEBUG_LOG(
            DBG3,
            "Consumer {}: Resumed from suspension, continuing iteration",
            getDisplayString());
        continue;
      }
    }

    // Common completion logic
    CT_DEBUG_LOG(
        DBG3,
        "Consumer {}: Marking item as processed and moving to next: {}",
        getDisplayString(),
        tracker.getItemDisplayString(item));
    item->markConsumerProcessed(bitPosition_);
    marker_ = get_next(item, tracker.getChangeList());
    tracker.checkAndNotifyProducer(item);
  }

  CT_DEBUG_LOG(
      DBG3,
      "Exiting consumeChanges() for consumer: {} (shouldTerminate_ = true)",
      getDisplayString());
}

template <typename T>
void Consumer<T>::resume() {
  CT_DEBUG_LOG(
      DBG3,
      "Consumer {}: resume() called, has coroutine handle: {}",
      getDisplayString(),
      pendingCoroutine_ ? "true" : "false");

  // Only resume if we're pending and have a coroutine handle
  if (pendingCoroutine_) {
    // Resume the coroutine directly
    auto handle = pendingCoroutine_;
    pendingCoroutine_ = nullptr;
    CT_DEBUG_LOG(
        DBG3, "Consumer {}: Resuming suspended coroutine", getDisplayString());
    // Remove from pending consumers
    handle.resume();
  }
}

template <typename T>
folly::coro::Task<void> Consumer<T>::waitForNewItems() {
  CT_DEBUG_LOG(
      DBG3,
      "Consumer {}: waitForNewItems() called, shouldTerminate: {}",
      getDisplayString(),
      shouldTerminate_ ? "true" : "false");

  // If we're terminating, don't wait for new items
  if (shouldTerminate_) {
    CT_DEBUG_LOG(
        DBG3,
        "Consumer {}: Terminating, exiting waitForNewItems()",
        getDisplayString());
    co_return;
  }

  CT_DEBUG_LOG(
      DBG3,
      "Consumer {}: Suspending coroutine to wait for new items",
      getDisplayString());

  /*
   * Use our custom awaitable to suspend the coroutine
   * This will store the coroutine handle in pendingCoroutine_
   */
  co_await SuspendAwaitable{this};

  CT_DEBUG_LOG(
      DBG3,
      "Consumer {}: Resumed from waitForNewItems(), continuing",
      getDisplayString());

  // After being resumed, we should have a new marker
}

/*
 * Iterator-based interface implementation (for
 * enable_iterable_change_list_tracker) Get the next item in the change list
 * (forward declaration - defined in ChangeItem.h)
 */
template <typename T>
inline ChangeItem<T>* get_next(
    const ChangeItem<T>* item,
    const ChangeItemList<T>& list);

template <typename T>
ChangeItem<T>* Consumer<T>::begin() {
  return next();
}

template <typename T>
ChangeItem<T>* Consumer<T>::next() {
  CT_DEBUG_LOG(
      DBG3,
      "Consumer {}: next() called, marker: {}",
      getDisplayString(),
      tracker.getItemDisplayString(marker_));

  // marker should have already been advanced by markProcessed()
  if (marker_ == nullptr) {
    CT_DEBUG_LOG(
        DBG3,
        "Consumer {}: next() - at end of list, returning nullptr",
        getDisplayString());
    return nullptr;
  }

  auto* item = marker_;
  CT_DEBUG_LOG(
      DBG3,
      "Consumer {}: next() returning item: {}",
      getDisplayString(),
      tracker.getItemDisplayString(item));
  return item;
}

template <typename T>
void Consumer<T>::markProcessed(ChangeItem<T>* item) {
  if (item == nullptr) {
    CT_DEBUG_LOG(
        DBG3,
        "Consumer {}: markProcessed() called with null item",
        getDisplayString());
    return;
  }

  CT_DEBUG_LOG(
      DBG3,
      "Consumer {}: markProcessed() for item: {}",
      getDisplayString(),
      tracker.getItemDisplayString(item));

  // Remove from pending list since we're processing (not yielding)
  item->removePendingConsumer(this->shared_from_this());

  /*
   * CRITICAL SAFETY: Find and save next item BEFORE clearing the bit
   * This ensures that if the item is destroyed after clearing the bit,
   * our marker will still be valid
   */
  auto* nextItem = get_next(item, tracker.getChangeList());

  // Skip items that don't have our bit set
  while (nextItem != nullptr &&
         !BitmapUtils::isBitSet(nextItem->consumerBitmap, bitPosition_)) {
    nextItem = get_next(nextItem, tracker.getChangeList());
  }

  CT_DEBUG_LOG(
      DBG3,
      "Consumer {}: Found next item: {}",
      getDisplayString(),
      tracker.getItemDisplayString(nextItem));

  // NOW clear the bit (item might be destroyed immediately after this!)
  item->markConsumerProcessed(bitPosition_);

  CT_DEBUG_LOG(
      DBG3,
      "Consumer {}: Cleared bit {} for item: {}",
      getDisplayString(),
      bitPosition_,
      tracker.getItemDisplayString(item));

  // Update marker to the saved next item
  marker_ = nextItem;

  // If we've reached the end, add to ready consumers
  if (nextItem == nullptr) {
    tracker.addToReadyConsumers(this->shared_from_this());
    CT_DEBUG_LOG(
        DBG3,
        "Consumer {}: Reached end of list, added to ready consumers",
        getDisplayString());
  }

  // Check if all consumers have processed this item, and notify producer if so
  tracker.checkAndNotifyProducer(item);
}

template <typename T>
void Consumer<T>::end() {
  CT_DEBUG_LOG(
      DBG3,
      "Consumer {}: end() called, marker: {}",
      getDisplayString(),
      tracker.getItemDisplayString(marker_));

  /*
   * If we have a marker pointing to an item, add ourselves to its pending list
   * This means consumer is YIELDING on this item
   * If marker is null, consumer will already be in ready list by
   * markProcessed()
   */
  if (marker_ != nullptr) {
    if (!pendingConsumerHook.is_linked()) {
      marker_->pendingConsumers.push_back(*this);
    }
    CT_DEBUG_LOG(
        DBG3,
        "Consumer {}: Yielding - added to pending list of item: {}",
        getDisplayString(),
        tracker.getItemDisplayString(marker_));
  }
}

template <typename T>
void Consumer<T>::iterateChangesUntilExcluding(ChangeItem<T>* untilMarker) {
  CT_DEBUG_LOG(
      DBG3,
      "Consumer {}: iterateChangesUntilExcluding() called, boundary: {}, "
      "consumer bit set on boundary: {}",
      getDisplayString(),
      tracker.getItemDisplayString(untilMarker),
      untilMarker &&
          BitmapUtils::isBitSet(untilMarker->consumerBitmap, bitPosition_));

  /*
   * The boundary item must carry this consumer's bit. markProcessed()/next()
   * skip items that don't have our bit set, so a boundary that lacks our bit
   * would be jumped over rather than stopped at — the loop would never satisfy
   * item == untilMarker and would consume PAST the boundary to the end of the
   * change list. Rather than silently overrun the boundary, log an error and
   * return without consuming; leaving the consumer untouched makes this far
   * easier to debug than an unintended skip.
   */
  if (untilMarker &&
      !BitmapUtils::isBitSet(untilMarker->consumerBitmap, bitPosition_)) {
    XLOGF(
        ERR,
        "Consumer {}: iterateChangesUntilExcluding boundary {} does not have this consumer's bit set; returning without consuming to avoid skipping past the boundary",
        getDisplayString(),
        tracker.getItemDisplayString(untilMarker));
    return;
  }

  // Remove from pending list if we're starting fresh
  if (marker_ != nullptr) {
    auto item = marker_;
    CT_DEBUG_LOG(
        DBG3,
        "Consumer {}: Removing from pending list of item: {}",
        getDisplayString(),
        tracker.getItemDisplayString(item));
    item->removePendingConsumer(this->shared_from_this());
  }

  // Capture initial marker to detect progress at end of cycle
  auto* initialMarker = marker_;

  /*
   * Tracks the last callback result. Defaults to CONTINUE so the boundary-first
   * case (loop breaks before any callback runs) is treated as "did not yield".
   */
  ProcessResult result = ProcessResult::CONTINUE;

  // Iterator-based polling cycle to consume available changes
  auto* item = begin();
  while (item) {
    // Stop before processing the boundary item (exclusive)
    if (item == untilMarker) {
      break;
    }

    CT_DEBUG_LOG(
        DBG3,
        "Consumer {}: Processing item: {}",
        getDisplayString(),
        tracker.getItemDisplayString(item));

    // Process the change item using the callback (MANDATORY)
    result = processChangeItemCallback_(item);

    if (result == ProcessResult::YIELD) {
      CT_DEBUG_LOG(
          DBG3, "Consumer {}: Yielded during processing", getDisplayString());
      /*
       * Break out of loop - end() will handle adding to pending list
       * Do NOT call markProcessed() - marker stays on current item for retry
       */
      break;
    }

    markProcessed(item);
    item = next();
  }

  if (marker_ != initialMarker) {
    recordMarkerAdvance();
  }

  /*
   * Always call end() to properly register consumer state
   * - If marker points to item (YIELD case): adds to pending list
   * - If marker is null (finished): adds to ready list if needed
   */
  end();

  CT_DEBUG_LOG(
      DBG3,
      "Consumer {}: iterateChangesUntilExcluding() completed",
      getDisplayString());

  /*
   * Reaching the end of the change list before the boundary is unexpected when
   * a boundary was requested. result == CONTINUE excludes the YIELD exit (which
   * is expected and leaves marker_ on the yielded item); marker_ != untilMarker
   * excludes the normal stop where we landed exactly on the boundary. When no
   * boundary was requested (untilMarker == nullptr), marker_ is also null at
   * the end, so marker_ != untilMarker is false and we do not warn.
   */
  XLOGF_IF(
      WARN,
      result == ProcessResult::CONTINUE && marker_ != untilMarker,
      "Consumer {}: iterateChangesUntilExcluding reached end of change list before boundary {}",
      getDisplayString(),
      tracker.getItemDisplayString(untilMarker));
}
