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

#include "neteng/fboss/bgp/cpp/changeTracker/ChangeItem.h"
#include "neteng/fboss/bgp/cpp/changeTracker/ChangeTrackerDebug.h"
#include "neteng/fboss/bgp/cpp/changeTracker/Consumer.h"
#include "neteng/fboss/bgp/cpp/changeTracker/ConsumerBitManager.h"
#include "neteng/fboss/bgp/cpp/changeTracker/TrackableObject.h"

#include <fmt/format.h>
#include <folly/Function.h>
#include <folly/container/F14Set.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

/**
 * ChangeTracker manages a list of objects that have changed and need to be
 * processed. It supports a pull model where consumers pull changes from the
 * change list.
 *
 * @tparam T The type of the object being tracked
 */
template <typename T>
class ChangeTracker {
 public:
  // Callback type for change notifications - type-erased callback
  using ChangeNotifyCallback = folly::Function<void()>;

  /*
   * Callback type for when a change item is fully processed - type-erased
   * callback
   */
  using OnChangeProcessedCallback = std::function<void(TrackableObject<T>*)>;

  // Callback type for object display - type-safe, specific to T
  using ObjectDisplayCallback = folly::Function<std::string(const T&)>;

  /**
   * Constructor.
   * @param name Name of the change tracker for debugging purposes.
   */
  explicit ChangeTracker(std::string name);

  /**
   * Set the object display callback for debug tracing.
   * The producer should call this to provide a callback that can display
   * objects in debug traces since the producer is responsible for publishing
   * items.
   * @param callback Function that takes a const T& and returns string
   * representation
   */
  void setObjectDisplayCallback(ObjectDisplayCallback callback) {
    objectDisplayCallback_ = std::move(callback);
  }

  /**
   * Get display string for an object (for debug tracing).
   * @param obj The object to display
   * @return String representation of the object
   */
  std::string getObjectDisplayString(const T& obj) const {
    if (objectDisplayCallback_) {
      // Need to cast away const since folly::Function is not const-callable
      auto& callback =
          const_cast<ObjectDisplayCallback&>(objectDisplayCallback_);
      return callback(obj);
    }
    return fmt::format("obj@{}", static_cast<const void*>(&obj));
  }

  /**
   * Get display string for a change item including its associated object.
   * @param item The change item to display
   * @return String representation of the item and its object
   */
  std::string getItemDisplayString(const ChangeItem<T>* item) const {
    if (!item) {
      return "item@null";
    }
    if (item->trackableObject) {
      return fmt::format(
          "item@{} [{}]",
          static_cast<const void*>(item),
          getObjectDisplayString(item->trackableObject->get()));
    }
    return fmt::format("item@{} [no_object]", static_cast<const void*>(item));
  }

  /**
   * Destructor.
   */
  virtual ~ChangeTracker();

  /**
   * Copy constructor (deleted).
   */
  ChangeTracker(const ChangeTracker&) = delete;

  /**
   * Copy assignment operator (deleted).
   */
  ChangeTracker& operator=(const ChangeTracker&) = delete;

  /**
   * Move constructor (deleted).
   */
  ChangeTracker(ChangeTracker&&) = delete;

  /**
   * Move assignment operator (deleted).
   */
  ChangeTracker& operator=(ChangeTracker&&) = delete;

  void updateItemPendingConsumers(ChangeItem<T>* itemPtr) {
    if (!itemPtr) {
      XLOG(DBG3, "updateItemPendingConsumers called with null item");
      return;
    }

    CT_DEBUG_LOG(
        DBG3,
        "Updating {} pending consumers for item: {}",
        itemPtr->pendingConsumers.size(),
        getItemDisplayString(itemPtr));

    // Get the next item in the list before removing this one
    ChangeItem<T>* nextItem = get_next(itemPtr, changeList_);

    if (nextItem) {
      CT_DEBUG_LOG(
          DBG3, "Next item in list: {}", getItemDisplayString(nextItem));
    } else {
      CT_DEBUG_LOG(
          DBG3, "No next item - consumers will be moved to ready list");
    }

    /*
     * Update markers for any consumers pending on this item
     * We need to iterate carefully since we're modifying the list
     */
    while (!itemPtr->pendingConsumers.empty()) {
      /*
       * Get reference to front consumer and unlink it from current list
       * SAFETY: Consumer object is owned by shared_ptr in consumers_ vector,
       * so it remains valid after unlinking from this intrusive list
       */
      auto& consumer = itemPtr->pendingConsumers.front();
      itemPtr->pendingConsumers.pop_front();

      // Update the marker to point to the next item
      consumer.setMarker(nextItem);

      // If there is a next item, add the consumer to its pending list
      if (nextItem) {
        CT_DEBUG_LOG(
            DBG3,
            "Moving consumer {} to pending list of next item {}",
            consumer.getDisplayString(),
            getItemDisplayString(nextItem));
        // Consumer was just popped, so it should not be linked
        if (!consumer.pendingConsumerHook.is_linked()) {
          nextItem->pendingConsumers.push_back(consumer);
        }
      } else {
        // If there's no next item, add to ready consumers
        CT_DEBUG_LOG(
            DBG3,
            "Moving consumer {} to ready consumers list (marker set to null)",
            consumer.getDisplayString());
        addToReadyConsumers(consumer.shared_from_this());
      }
    }

    CT_DEBUG_LOG(DBG3, "Cleared pending consumers list for processed item");
  }

  /**
   * Publishes a change to all registered consumers.
   * This is a convenience overload that notifies all registered consumers.
   * @param trackableObject Pointer to the trackable object that has changed.
   */
  void publishChange(TrackableObject<T>* trackableObject) {
    publishChange(trackableObject, getFullConsumerBitmap());
  }

  /**
   * Publishes a change to specific consumers.
   * This adds the object to the change list if it's not already there,
   * or moves it to the end of the list if it is.
   * @param trackableObject Pointer to the trackable object that has changed.
   * @param consumerBitmap Bitmap of consumers to notify.
   */
  void publishChange(
      TrackableObject<T>* trackableObject,
      const ConsumerBitmap& consumerBitmap) {
    if (!trackableObject) {
      CT_DEBUG_LOG(
          DBG1,
          "publishChange called with null trackableObject - returning early");
      return;
    }

    CT_DEBUG_LOG(
        DBG3,
        "Publishing change for object: {}",
        getObjectDisplayString(trackableObject->get()));

    ChangeItem<T>* itemPtr = nullptr;

    if (shouldSkipChangePublication(trackableObject, consumerBitmap)) {
      /*
       * No consumers registered, or no consumers need to process this change
       * (object not already on list and empty bitmap), so no need to track
       * Call the global callback if set
       */
      if (onChangeProcessedCallback_) {
        onChangeProcessedCallback_(trackableObject);
      }
      CT_DEBUG_LOG(
          DBG3, "No consumers to process change - skipping change publication");
      return;
    }

    if (trackableObject->isOnChangeList()) {
      CT_DEBUG_LOG(
          DBG3,
          "Object already on change list, moving to end: {}",
          getObjectDisplayString(trackableObject->get()));
      // Get the existing change item
      itemPtr = trackableObject->getChangeItem();

      // Remove from the list if it's linked
      if (itemPtr->changeListHook.is_linked()) {
        // Check if this is the only item in the list
        bool isOnlyItem = changeListHasOnlyOneItem();
        CT_DEBUG_LOG(
            DBG3,
            "Removing item from position in change list (isOnlyItem: {})",
            isOnlyItem ? "true" : "false");

        if (!isOnlyItem) {
          updateItemPendingConsumers(itemPtr);
        }
        /*
         * If it's the only item, we don't need to update markers
         * as they will still be pointing to the same item after it's moved
         */

        // Now remove the item from the list
        changeList_.erase(changeList_.iterator_to(*itemPtr));
      }

      // Update the bitmap - OR with incoming bitmap to accumulate consumers
      auto newBitmap =
          BitmapUtils::orBitmaps(itemPtr->consumerBitmap, consumerBitmap);
      itemPtr->setConsumerBitMap(newBitmap);
      CT_DEBUG_LOG(DBG3, "Updated consumer bitmap for existing item");
    } else {
      CT_DEBUG_LOG(
          DBG3,
          "Creating new change item for object: {}",
          getObjectDisplayString(trackableObject->get()));
      // Create a new change item with the appropriate consumer bitmap
      auto changeItem = createChangeItem(trackableObject, consumerBitmap);
      itemPtr = changeItem.get();

      // Store the change item in the trackable object
      trackableObject->setChangeItem(std::move(changeItem));
    }

    /*
     * Common operations: add to the end of the list and notify ready
     * consumers, blocked consumers will be notified when the event they want
     * occurs (e.g BGP peer unblocked)
     */

    changeList_.push_back(*itemPtr);
    CT_DEBUG_LOG(
        DBG3,
        "Added item to end of change list. List size: {}",
        changeList_.size());
    notifyReadyConsumers(itemPtr);
  }

  bool isConsumerSetOnTrackableObject(
      TrackableObject<T>* trackableObject,
      const std::shared_ptr<Consumer<T>>& consumer) {
    if (!trackableObject || !consumer) {
      return false;
    }

    if (!trackableObject->isOnChangeList()) {
      return false;
    }

    // Get the existing change item
    auto itemPtr = trackableObject->getChangeItem();
    return (BitmapUtils::isBitSet(
        itemPtr->consumerBitmap, consumer->getBitPosition()));
  }

  /**
   * Register a consumer.
   * @param consumer Shared pointer to the consumer to register.
   * @return Consumer ID that can be used to identify the consumer.
   */
  size_t registerConsumer(std::shared_ptr<Consumer<T>> consumer);

  /**
   * Unregister a consumer.
   * @param consumerId ID of the consumer to unregister.
   */
  void unregisterConsumer(size_t consumerId);

  /**
   * Reset a consumer's position in the change list.
   * If the consumer has a marker, removes it from the pending consumers list
   * of that item, then walks all subsequent items marking the consumer as
   * processed, notifying the producer when all consumers have processed an
   * item. After processing, the consumer's marker is reset to nullopt.
   *
   * This is safe to call even if the consumer has no marker or a null marker.
   *
   * @param consumer Shared pointer to the consumer.
   */
  void consumerResetChangeList(const std::shared_ptr<Consumer<T>>& consumer);

  /**
   * Join a new consumer at the same position as an existing consumer.
   * The new consumer must already be registered with the tracker.
   * - If existing consumer is ready (no marker), new consumer joins as ready
   * - If existing consumer is pended at a marker, new consumer joins at same
   * item and sets its bit for all items from that point to the end of change
   * list
   * @param existingConsumer The consumer whose position to join at
   * @param newConsumer The new consumer to join (must be already registered)
   */
  void joinConsumer(
      const std::shared_ptr<Consumer<T>>& existingConsumer,
      const std::shared_ptr<Consumer<T>>& newConsumer);

  /**
   * Fast-forward sourceConsumer up to targetConsumer's position by acking
   * (marking processed) every item in between, WITHOUT invoking
   * sourceConsumer's process callback.
   *
   * Normally targetConsumer is at or ahead of sourceConsumer on the change
   * list. When it is ahead, the boundary is EXCLUSIVE - the item at
   * targetConsumer's marker is not acked; sourceConsumer ends pended there,
   * aligned with targetConsumer.
   *
   * If targetConsumer's marker is null, or is behind sourceConsumer (so
   * walking forward never reaches it), sourceConsumer simply acks through to
   * the end of the change list and becomes a ready consumer (null marker).
   *
   * Unlike joinConsumer(), this operates on an already-active consumer that is
   * BEHIND: it only clears sourceConsumer's bit on the skipped prefix and
   * leaves the already-set bits on [targetMarker, end] untouched.
   *
   * @param sourceConsumer The behind consumer to advance.
   * @param targetConsumer The consumer whose marker is the exclusive boundary.
   */
  void attachToMarkerAhead(
      const std::shared_ptr<Consumer<T>>& sourceConsumer,
      const std::shared_ptr<Consumer<T>>& targetConsumer);

  /**
   * Rewind sourceConsumer back to targetConsumer's (behind) marker and retire
   * targetConsumer.
   *
   * targetConsumer must be at or behind sourceConsumer on the change list.
   * sourceConsumer re-owns every item from target's marker up to and INCLUDING
   * its own current marker (setting its bit), so after the move source owns
   * [targetMarker, end] and will reprocess the items it had already advanced
   * past. source's marker (and pending-list membership) is then moved onto
   * target's marker.
   *
   * Finally targetConsumer is reset via consumerResetChangeList(): its bit is
   * cleared on every item from its marker to the end, it is unpended, and its
   * marker is nulled. The CALLER is responsible for deregistering
   * targetConsumer from the tracker afterwards.
   *
   * The bit-set above happens BEFORE the reset so clearing target's bit can
   * never fully-process (and free) an item the freshly-moved markers point at.
   *
   * This is the mirror of attachToMarkerAhead(), which instead fast-forwards
   * source to an AHEAD target by clearing bits.
   *
   * @param sourceConsumer The consumer to rewind onto target's marker.
   * @param targetConsumer The behind consumer whose position source adopts; it
   *        is then reset for the caller to deregister.
   */
  void attachToMarkerBehind(
      const std::shared_ptr<Consumer<T>>& sourceConsumer,
      const std::shared_ptr<Consumer<T>>& targetConsumer);

  /**
   * Get the full consumer bitmap (all bits set for registered consumers).
   * @return Bitmap with bits set for all registered consumers.
   */
  ConsumerBitmap getFullConsumerBitmap() const;

  /**
   * Notify the producer that a change item has been fully processed.
   * This is called when all consumers have processed the item.
   * @param item The change item that has been fully processed.
   */
  void notifyProducer(ChangeItem<T>* item);

  /**
   * Check if all consumers have processed an item and notify the producer if
   * so. This is called when a consumer marks itself as having processed an
   * item.
   * @param item The change item to check.
   */
  void checkAndNotifyProducer(ChangeItem<T>* item);

  /**
   * Get the head of the change list.
   * @return Pointer to the first item in the change list, or nullptr if the
   * list is empty.
   */
  ChangeItem<T>* getHead() const {
    return changeList_.empty()
        ? nullptr
        : const_cast<ChangeItem<T>*>(&changeList_.front());
  }

  /**
   * Get the change list.
   * @return Reference to the change list.
   */
  const ChangeItemList<T>& getChangeList() const {
    return changeList_;
  }

  /**
   * Publish a change to an object with a callback for when the change is
   * fully processed. This adds the object to the change list if it's not
   * already there, or moves it to the end of the list if it is.
   * @param trackableObject Pointer to the trackable object that has changed.
   * @param callback The callback function to call when the change item is
   * fully processed.
   * @deprecated Use setGlobalOnChangeProcessedCallback() instead for better
   * memory efficiency.
   */
  void publishChangeWithCallback(
      TrackableObject<T>* trackableObject,
      OnChangeProcessedCallback callback) {
    // Set the global callback
    setGlobalOnChangeProcessedCallback(std::move(callback));

    // Publish the change
    publishChange(trackableObject);
  }

  /**
   * Set a global callback to be called when any change item is fully
   * processed. This callback will be called when all consumers have processed
   * a change item. This is more memory efficient than setting individual
   * callbacks for each trackable object.
   * @param callback The callback function to call when a change item is fully
   * processed.
   */
  void setGlobalOnChangeProcessedCallback(OnChangeProcessedCallback callback) {
    onChangeProcessedCallback_ = std::move(callback);
  }

 protected:
  /**
   * Check if the change list has exactly one element.
   * Uses iterator-based approach which is O(1) for intrusive lists.
   * @return true if the list has exactly one element, false otherwise.
   */
  bool changeListHasOnlyOneItem() const {
    auto it = changeList_.begin();
    return ((it != changeList_.end()) && (std::next(it) == changeList_.end()));
  }

  /**
   * Check if the change publication should be skipped.
   * Returns true if:
   * - No consumers are registered, OR
   * - The object is not already on the change list AND the consumer bitmap is
   *   empty (no consumers need to process this change)
   * @param trackableObject The object being published
   * @param consumerBitmap The bitmap of consumers to notify
   * @return true if change publication should be skipped, false otherwise
   */
  inline bool shouldSkipChangePublication(
      const TrackableObject<T>* trackableObject,
      const ConsumerBitmap& consumerBitmap) const {
    return consumerBitManager_.getNumBitsInUse() == 0 ||
        (!trackableObject->isOnChangeList() &&
         BitmapUtils::isAllBitsCleared(consumerBitmap));
  }

  // Virtual method to create a change item
  std::unique_ptr<ChangeItem<T>> createChangeItem(
      TrackableObject<T>* trackableObject,
      const std::optional<ConsumerBitmap>& consumerBitmap = std::nullopt) {
    CT_DEBUG_LOG(
        DBG3,
        "Creating change item for trackable object: {}",
        getObjectDisplayString(trackableObject->get()));
    auto bitmap = consumerBitmap.has_value() ? consumerBitmap.value()
                                             : getFullConsumerBitmap();
    CT_DEBUG_LOG(
        DBG3,
        "Using consumer bitmap with {} bits in use",
        consumerBitManager_.getNumBitsInUse());
    return std::make_unique<ChangeItem<T>>(trackableObject, bitmap);
  }

  /**
   * Helper to notify ready consumers about a new change item.
   * Ready consumers are those that have reached the end of the list
   * and have their markers set to NULL.
   */
  void notifyReadyConsumers(ChangeItem<T>* item);

  // Name of the change tracker for debugging
  std::string name_;

  // Intrusive list of change items
  ChangeItemList<T> changeList_;

  // Vector of consumers
  std::vector<std::shared_ptr<Consumer<T>>> consumers_;

  // Set of consumers that have reached the end of the list
  folly::F14NodeSet<std::shared_ptr<Consumer<T>>> readyConsumers_;

  // Manager for consumer bit positions
  ConsumerBitManager consumerBitManager_;

  // Cached bitmap of all registered consumers
  ConsumerBitmap cachedConsumerBitmap_;

  // Global callback for when any change item is fully processed
  OnChangeProcessedCallback onChangeProcessedCallback_;

  // Optional callback to display objects in debug traces
  ObjectDisplayCallback objectDisplayCallback_;

 public:
  /**
   * Add a consumer to the ready consumers list.
   * This is called when a consumer reaches the end of the change list.
   */
  void addToReadyConsumers(const std::shared_ptr<Consumer<T>>& consumer) {
    // Defensive check if consumer is already in the ready list using flag
    if (consumer->isInReadyList()) {
      return;
    }

    readyConsumers_.insert(consumer);
    consumer->setInReadyList(true);
  }

  /**
   * Remove a consumer from the ready consumers list.
   * This is called when a consumer is moved out of the ready state.
   */
  void removeFromReadyConsumers(const std::shared_ptr<Consumer<T>>& consumer) {
    if (!consumer->isInReadyList()) {
      return;
    }

    readyConsumers_.erase(consumer);
    consumer->setInReadyList(false);
  }
};

// Implementation of ChangeTracker methods
template <typename T>
ChangeTracker<T>::ChangeTracker(std::string name)
    : name_(std::move(name)),
      cachedConsumerBitmap_(),
      onChangeProcessedCallback_(nullptr) {
  CT_DEBUG_LOG(DBG3, "ChangeTracker '{}' created", name_);
}

template <typename T>
ChangeTracker<T>::~ChangeTracker() {
  CT_DEBUG_LOG(
      DBG3,
      "ChangeTracker '{}' destructor: clearing {} consumers, {} ready consumers, {} change items",
      name_,
      consumers_.size(),
      readyConsumers_.size(),
      changeList_.size());

  // Clear consumers and their ready list flags
  for (const auto& consumer : readyConsumers_) {
    consumer->setInReadyList(false);
  }
  consumers_.clear();
  readyConsumers_.clear();

  // Clear the change list to ensure proper cleanup
  changeList_.clear();

  CT_DEBUG_LOG(DBG3, "ChangeTracker '{}' destroyed", name_);
}

template <typename T>
size_t ChangeTracker<T>::registerConsumer(
    std::shared_ptr<Consumer<T>> consumer) {
  if (!consumer) {
    CT_DEBUG_LOG(
        DBG1, "registerConsumer called with null consumer - returning early");
    return SIZE_MAX; // Invalid position
  }

  size_t pos = consumer->getBitPosition();
  if (pos != static_cast<size_t>(-1)) {
    CT_DEBUG_LOG(
        ERR,
        "registerConsumer called for consumer {} already registered at bit position {} - returning existing position",
        consumer->getDisplayString(),
        pos);
    return pos;
  }

  // Get a bit position from the bit manager
  size_t position = consumerBitManager_.getConsumerBit();
  CT_DEBUG_LOG(DBG3, "Registered consumer at bit position: {}", position);

  // Add to consumers vector, expanding if necessary
  if (position >= consumers_.size()) {
    consumers_.resize(position + 1, nullptr);
  }
  consumers_[position] = consumer;

  // Update the cached bitmap
  BitmapUtils::setBit(cachedConsumerBitmap_, position);

  // Add to ready consumers
  addToReadyConsumers(consumer);
  CT_DEBUG_LOG(
      DBG3, "Added consumer at position {} to ready consumers list", position);

  return position;
}

template <typename T>
void ChangeTracker<T>::unregisterConsumer(size_t consumerId) {
  // Ensure the position is valid
  if (consumerId >= consumers_.size()) {
    CT_DEBUG_LOG(
        DBG1,
        "unregisterConsumer called with invalid consumerId {} (>= consumers_.size() {}) - returning early",
        consumerId,
        consumers_.size());
    return;
  }

  if (!consumers_[consumerId]) {
    CT_DEBUG_LOG(
        DBG1,
        "unregisterConsumer called with consumerId {} that has no registered consumer - returning early",
        consumerId);
    return;
  }

  // Get the consumer
  auto consumer = consumers_[consumerId];
  CT_DEBUG_LOG(DBG3, "Unregistering consumer at position {}", consumerId);
  consumers_[consumerId] = nullptr;

  // Free the bit position
  consumerBitManager_.freeConsumerBit(consumerId);

  // Update the cached bitmap
  BitmapUtils::clearBit(cachedConsumerBitmap_, consumerId);

  // Remove from ready consumers if present
  removeFromReadyConsumers(consumer);

  // Reset the consumer's position in the change list
  consumerResetChangeList(consumer);
}

template <typename T>
void ChangeTracker<T>::consumerResetChangeList(
    const std::shared_ptr<Consumer<T>>& consumer) {
  if (!consumer) {
    CT_DEBUG_LOG(
        DBG1,
        "consumerResetChangeList called with null consumer - returning early");
    return;
  }

  auto* marker = consumer->getMarker();
  if (marker == nullptr) {
    CT_DEBUG_LOG(
        DBG3,
        "consumerResetChangeList: consumer has null marker - nothing to reset");
    return;
  }

  size_t consumerId = consumer->getBitPosition();
  ChangeItem<T>* item = marker;

  // Remove from pending consumers
  item->removePendingConsumer(consumer);

  /*
   * For all subsequent items, mark the consumer as processed and make sure
   * producer gets notified if all consumers have processed the item
   */
  while (item != nullptr) {
    ChangeItem<T>* next_item = get_next(item, changeList_);

    item->markConsumerProcessed(consumerId);
    if (item->allConsumersProcessed()) {
      notifyProducer(item);
    }
    item = next_item;
  }

  // Reset the consumer's marker so it doesn't point to freed items
  consumer->setMarker(nullptr);
}

template <typename T>
ConsumerBitmap ChangeTracker<T>::getFullConsumerBitmap() const {
  CT_DEBUG_LOG(
      DBG3,
      "Returning cached consumer bitmap with %zu bits in use",
      consumerBitManager_.getNumBitsInUse());
  // Return the cached bitmap
  return cachedConsumerBitmap_;
}

template <typename T>
void ChangeTracker<T>::notifyProducer(ChangeItem<T>* item) {
  CT_DEBUG_LOG(
      DBG3,
      "Notifying producer: Item processing completed for {}",
      getItemDisplayString(item));
  updateItemPendingConsumers(item);

  // Remove the item from the list if it's linked
  if (item->changeListHook.is_linked()) {
    CT_DEBUG_LOG(
        DBG3,
        "Removing completed item from change list: {}",
        getItemDisplayString(item));
    changeList_.erase(changeList_.iterator_to(*item));
  }

  // Save the trackable object pointer before clearing the change item
  TrackableObject<T>* trackableObject = item->trackableObject;

  /*
   * Clear the reference in the trackable object
   * This will delete the ChangeItem since TrackableObject owns it via
   * unique_ptr
   */
  if (trackableObject) {
    CT_DEBUG_LOG(
        DBG3,
        "Clearing change item from trackable object: {}",
        getObjectDisplayString(trackableObject->get()));
    trackableObject->clearChangeItem();
  }

  // Call the global callback if set
  if (onChangeProcessedCallback_) {
    CT_DEBUG_LOG(
        DBG3, "Calling global onChangeProcessed callback for trackable object");
    onChangeProcessedCallback_(trackableObject);
  }
}

template <typename T>
void ChangeTracker<T>::checkAndNotifyProducer(ChangeItem<T>* item) {
  if (!item) {
    CT_DEBUG_LOG(DBG3, "checkAndNotifyProducer called with null item");
    return;
  }

  // Check if all consumers have processed this item
  if (item->allConsumersProcessed()) {
    CT_DEBUG_LOG(
        DBG3,
        "All consumers have processed item {} - notifying producer",
        getItemDisplayString(item));
    notifyProducer(item);
  } else {
    CT_DEBUG_LOG(
        DBG3,
        "Not all consumers have processed item {} yet",
        getItemDisplayString(item));
  }
}

template <typename T>
void ChangeTracker<T>::joinConsumer(
    const std::shared_ptr<Consumer<T>>& existingConsumer,
    const std::shared_ptr<Consumer<T>>& newConsumer) {
  CT_DEBUG_LOG(
      DBG3,
      "joinConsumer() called: existing={}, new={}",
      existingConsumer ? existingConsumer->getDisplayString() : "null",
      newConsumer ? newConsumer->getDisplayString() : "null");

  // The new consumer must already be registered - verify this
  size_t newConsumerBitPosition = newConsumer->getBitPosition();
  if (newConsumerBitPosition >= consumers_.size() ||
      consumers_[newConsumerBitPosition] != newConsumer) {
    CT_DEBUG_LOG(
        DBG1,
        "joinConsumer() failed: new consumer {} not properly registered (bit position {})",
        newConsumer->getDisplayString(),
        newConsumerBitPosition);
    throw std::invalid_argument(
        "New consumer must be registered before calling joinConsumer");
  }

  // New consumer must not have an existing marker when joining
  auto* newMarker = newConsumer->getMarker();
  if (newMarker != nullptr) {
    XLOGF(
        ERR,
        "joinConsumer: new consumer {} has an existing marker, aborting join",
        newConsumer->getDisplayString());
    return;
  }

  // Get the existing consumer's marker
  auto* existingMarker = existingConsumer->getMarker();

  CT_DEBUG_LOG(
      DBG3,
      "Existing consumer {} marker: {}",
      existingConsumer->getDisplayString(),
      getItemDisplayString(existingMarker));

  if (existingMarker == nullptr) {
    /*
     * Case 1: C1 is ready (nullptr)
     * C2's marker is already nullptr from registration, so no need to set.
     * C2 is already in the ready list from registration.
     */
    CT_DEBUG_LOG(
        DBG3,
        "Case 1: Existing consumer {} marker is null - new consumer {} already matches",
        existingConsumer->getDisplayString(),
        newConsumer->getDisplayString());
    return;
  }
  // Case 2: C1 is pended at a marker
  CT_DEBUG_LOG(
      DBG3,
      "Case 2: Existing consumer {} is pended at item {}",
      existingConsumer->getDisplayString(),
      getItemDisplayString(existingMarker));

  /*
   * Remove the new consumer from ready consumers since we'll position it at
   * the pended location
   */
  removeFromReadyConsumers(newConsumer);

  // C2 joins with its marker at same item as C1
  ChangeItem<T>* pendedItem = existingMarker;

  newConsumer->setMarker(pendedItem);
  CT_DEBUG_LOG(
      DBG3,
      "Set new consumer {} marker to {}",
      newConsumer->getDisplayString(),
      getItemDisplayString(pendedItem));

  /*
   * Add C2 to the pending consumers list of the same item
   * Only add if not already linked
   */
  if (!newConsumer->pendingConsumerHook.is_linked()) {
    pendedItem->pendingConsumers.push_back(*newConsumer);
  }
  CT_DEBUG_LOG(
      DBG3,
      "Added new consumer {} to pending consumers list of item {}",
      newConsumer->getDisplayString(),
      getItemDisplayString(pendedItem));

  /*
   * C2 must also traverse ALL items from the pended item to the end of change
   * list and set its bit so that the consumption logic continues to work
   */
  ChangeItem<T>* currentItem = pendedItem;
  size_t itemsUpdated = 0;
  CT_DEBUG_LOG(
      DBG3,
      "Traversing change list from item {} to end to set bit {} for new consumer {}",
      getItemDisplayString(pendedItem),
      newConsumerBitPosition,
      newConsumer->getDisplayString());

  while (currentItem != nullptr) {
    // Set the bit for the new consumer in this item's bitmap
    BitmapUtils::setBit(currentItem->consumerBitmap, newConsumerBitPosition);
    itemsUpdated++;
    CT_DEBUG_LOG(
        DBG3,
        "Set bit {} for consumer {} in item {}",
        newConsumerBitPosition,
        newConsumer->getDisplayString(),
        getItemDisplayString(currentItem));

    // Move to the next item
    currentItem = get_next(currentItem, changeList_);
  }
  CT_DEBUG_LOG(
      DBG3,
      "Updated {} items with bit for new consumer {} (traversed from {} to end)",
      itemsUpdated,
      newConsumer->getDisplayString(),
      getItemDisplayString(pendedItem));
}

template <typename T>
void ChangeTracker<T>::attachToMarkerAhead(
    const std::shared_ptr<Consumer<T>>& sourceConsumer,
    const std::shared_ptr<Consumer<T>>& targetConsumer) {
  if (!sourceConsumer || !targetConsumer) {
    CT_DEBUG_LOG(
        DBG4,
        "attachToMarkerAhead called with null consumer (source={}, target={}) - returning early",
        sourceConsumer ? sourceConsumer->getDisplayString() : "null",
        targetConsumer ? targetConsumer->getDisplayString() : "null");
    return;
  }

  if (sourceConsumer->getBitPosition() == static_cast<size_t>(-1)) {
    CT_DEBUG_LOG(
        DBG4,
        "attachToMarkerAhead: source consumer {} is not registered - returning early",
        sourceConsumer->getDisplayString());
    return;
  }

  ChangeItem<T>* targetMarker = targetConsumer->getMarker();
  ChangeItem<T>* startItem = sourceConsumer->getMarker();

  /*
   * sourceConsumer is already at the end of the change list (null marker):
   * there is nothing ahead to skip. Ensure it is a ready consumer and return.
   * This trivially covers the case where targetMarker is also null or behind.
   */
  if (startItem == nullptr) {
    addToReadyConsumers(sourceConsumer);
    CT_DEBUG_LOG(
        DBG3,
        "attachToMarkerAhead: consumer {} already at end of list, now a ready consumer",
        sourceConsumer->getDisplayString());
    return;
  }

  // Already pended at the same item as target: nothing to skip.
  if (startItem == targetMarker) {
    CT_DEBUG_LOG(
        DBG3,
        "attachToMarkerAhead: consumer {} already aligned with target {} at {}",
        sourceConsumer->getDisplayString(),
        targetConsumer->getDisplayString(),
        getItemDisplayString(targetMarker));
    return;
  }

  CT_DEBUG_LOG(
      DBG3,
      "attachToMarkerAhead: advancing consumer {} from {} up to target {} marker {}",
      sourceConsumer->getDisplayString(),
      getItemDisplayString(startItem),
      targetConsumer->getDisplayString(),
      getItemDisplayString(targetMarker));

  /*
   * Walk the raw change list forward from startItem, stopping at targetMarker
   * by pointer identity (exclusive). get_next() visits every item -- owned or
   * not -- so the walk stops at targetMarker even when sourceConsumer does not
   * own it (e.g. an item published selectively to targetConsumer only). That is
   * what lets sourceConsumer align with targetConsumer instead of skipping
   * past an unowned boundary the way the bit-following iterator would.
   *
   * Ack only the items sourceConsumer owns: clear its bit and notify the
   * producer if the item thereby becomes fully processed. Capture the next
   * pointer BEFORE acking, because notifying the producer may free a
   * fully-processed item (avoids use-after-free).
   *
   * sourceConsumer is pended at startItem; detach it before acking so the
   * pending-consumer migration that runs when an owned item is freed does not
   * drag its marker forward. It is re-pended at its final position below.
   *
   * If targetMarker is null (targetConsumer is ready) or behind sourceConsumer
   * the forward walk never meets it and runs off the end, leaving
   * sourceConsumer a ready consumer.
   */
  const size_t bitPosition = sourceConsumer->getBitPosition();
  startItem->removePendingConsumer(sourceConsumer);

  ChangeItem<T>* item = startItem;
  while (item != nullptr && item != targetMarker) {
    ChangeItem<T>* nextItem = get_next(item, changeList_);
    if (BitmapUtils::isBitSet(item->consumerBitmap, bitPosition)) {
      item->markConsumerProcessed(bitPosition);
      checkAndNotifyProducer(item);
    }
    item = nextItem;
  }

  sourceConsumer->setMarker(item);
  sourceConsumer->recordMarkerAdvance();
  if (item == nullptr) {
    addToReadyConsumers(sourceConsumer);
  } else {
    sourceConsumer->end();
  }

  CT_DEBUG_LOG(
      DBG3,
      "attachToMarkerAhead: consumer {} advanced; marker now {}",
      sourceConsumer->getDisplayString(),
      getItemDisplayString(sourceConsumer->getMarker()));
}

template <typename T>
void ChangeTracker<T>::attachToMarkerBehind(
    const std::shared_ptr<Consumer<T>>& sourceConsumer,
    const std::shared_ptr<Consumer<T>>& targetConsumer) {
  if (!sourceConsumer || !targetConsumer) {
    CT_DEBUG_LOG(
        DBG4,
        "attachToMarkerBehind called with null consumer (source={}, target={}) - returning early",
        sourceConsumer ? sourceConsumer->getDisplayString() : "null",
        targetConsumer ? targetConsumer->getDisplayString() : "null");
    return;
  }

  if (sourceConsumer->getBitPosition() == static_cast<size_t>(-1)) {
    CT_DEBUG_LOG(
        DBG4,
        "attachToMarkerBehind: source consumer {} is not registered - returning early",
        sourceConsumer->getDisplayString());
    return;
  }

  ChangeItem<T>* targetMarker = targetConsumer->getMarker();
  ChangeItem<T>* sourceMarker = sourceConsumer->getMarker();

  CT_DEBUG_LOG(
      DBG3,
      "attachToMarkerBehind: rewinding source {} from {} back to target {} marker {}",
      sourceConsumer->getDisplayString(),
      getItemDisplayString(sourceMarker),
      targetConsumer->getDisplayString(),
      getItemDisplayString(targetMarker));

  /*
   * Step 1: set source's bit on every item from target's marker up to and
   * INCLUDING source's own marker, so source re-owns [targetMarker,
   * sourceMarker] and reprocesses the range it had advanced past.
   *
   * This runs before resetting target (step 3): with source's bit set across
   * the range, clearing target's bit can never make an item fully processed, so
   * consumerResetChangeList() cannot free an item the markers below point at.
   */
  const size_t bitPosition = sourceConsumer->getBitPosition();
  for (ChangeItem<T>* item = targetMarker; item != nullptr;
       item = get_next(item, changeList_)) {
    BitmapUtils::setBit(item->consumerBitmap, bitPosition);
    if (item == sourceMarker) {
      break;
    }
  }

  /*
   * Step 2: point source's marker at target's marker, moving its pending-list
   * membership (or the ready list when target's marker is null).
   */
  if (sourceMarker != nullptr) {
    sourceMarker->removePendingConsumer(sourceConsumer);
  } else {
    removeFromReadyConsumers(sourceConsumer);
  }
  sourceConsumer->setMarker(targetMarker);
  if (targetMarker != nullptr) {
    sourceConsumer->end();
  } else {
    addToReadyConsumers(sourceConsumer);
  }

  /*
   * Step 3: reset target -- clears its bit from its marker to the end. The
   * caller deregisters target afterwards.
   */
  consumerResetChangeList(targetConsumer);

  CT_DEBUG_LOG(
      DBG3,
      "attachToMarkerBehind: source {} now at {}; target {} reset",
      sourceConsumer->getDisplayString(),
      getItemDisplayString(sourceConsumer->getMarker()),
      targetConsumer->getDisplayString());
}

template <typename T>
void ChangeTracker<T>::notifyReadyConsumers(ChangeItem<T>* item) {
  if (!readyConsumers_.empty()) {
    CT_DEBUG_LOG(
        DBG3,
        "Notifying {} ready consumers about new item: {}",
        readyConsumers_.size(),
        getItemDisplayString(item));
  }

  // Notify consumers that are in READY state about the new item
  for (const auto& consumer : readyConsumers_) {
    CT_DEBUG_LOG(
        DBG3,
        "Setting marker for ready consumer {} to item {} and adding to pending list",
        consumer ? consumer->getDisplayString() : "null",
        getItemDisplayString(item));
    consumer->setMarker(item);
    /*
     * Start the staleness clock when the consumer first gets work assigned
     * (null -> non-null marker). Without this, a newly notified consumer
     * would have an epoch timestamp and appear immediately stale.
     */
    consumer->recordMarkerAdvance();
    // Only add to pending list if not already linked
    if (!consumer->pendingConsumerHook.is_linked()) {
      item->pendingConsumers.push_back(*consumer);
    }
    // Clear the ready list flag since consumer is no longer in ready list
    consumer->setInReadyList(false);
    // Notify the consumer to start consuming changes
    CT_DEBUG_LOG(
        DBG3,
        "Notifying consumer {} to resume processing for item {}",
        consumer ? consumer->getDisplayString() : "null",
        getItemDisplayString(item));
    consumer->resume();
  }

  // Clear the ready consumers list
  readyConsumers_.clear();
  CT_DEBUG_LOG(DBG3, "Cleared ready consumers list after notifications");
}
