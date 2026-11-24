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

#include <chrono>
#include <utility>

#include <folly/logging/xlog.h>

#include "neteng/fboss/bgp/cpp/common/RouteInfo.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopAssociationList.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopInfoBase.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopStatus.h"

namespace facebook::bgp {

class NexthopInfo : public NexthopInfoBase {
 public:
  explicit NexthopInfo(const NexthopStatus& status) : status_(status) {
    /*
     * Stamp the initial reachability and cost states so an unset timestamp is
     * distinguishable from a value received at creation.
     */
    const auto now = std::chrono::steady_clock::now();
    if (status_.isReachable()) {
      lastReachabilityChangeTs_ = now;
    }
    if (status_.getIgpCost().has_value()) {
      lastIgpCostChangeTs_ = now;
    }
  }

  /*
   * Delete copy constructor and assignment operator since
   * NexthopAssociationList is not copyable
   */
  NexthopInfo(const NexthopInfo&) = delete;
  NexthopInfo& operator=(const NexthopInfo&) = delete;

  /**
   * Move a NexthopInfo while keeping linked RouteInfo objects associated with
   * the new object.
   *
   * This constructor can run when a NexthopInfo is inserted into
   * nexthopInfoMap_ or is otherwise move-constructed. The association list is
   * normally empty during map insertion. If routes are already linked, update
   * their NexthopInfo pointers because moving the list does not update them.
   */
  NexthopInfo(NexthopInfo&& other) noexcept
      : status_(std::move(other.status_)),
        nexthopAssociationList_(std::move(other.nexthopAssociationList_)),
        lastReachabilityChangeTs_(std::move(other.lastReachabilityChangeTs_)),
        lastIgpCostChangeTs_(std::move(other.lastIgpCostChangeTs_)) {
    for (auto& routeInfo : nexthopAssociationList_) {
      routeInfo.setNexthopInfo(this);
    }
  }
  NexthopInfo& operator=(NexthopInfo&&) = delete;

  ~NexthopInfo() override {
    while (!nexthopAssociationList_.empty()) {
      unlinkRouteInfo(*nexthopAssociationList_.begin());
    }
  }

  /*
   * [Accessor Methods]
   */
  const folly::IPAddress& getNextHop() const {
    return status_.getNexthop();
  }

  bool isReachable() const override {
    return status_.isReachable();
  }

  std::optional<uint32_t> getIgpCost() const override {
    return status_.getIgpCost();
  }

  std::optional<bool> isConnected() const override {
    return status_.isConnected();
  }

  /**
   * @brief Get the interface that the nexthop is reachable through.
   * @return Interface name in kernel format, or std::nullopt if the source of
   * the nexthop does not know the interface. See NexthopStatus::getIfName().
   */
  const std::optional<std::string>& getIfName() const {
    return status_.getIfName();
  }

  bool isResolvedForSelection() const override {
    return status_.isResolvedForSelection();
  }

  /**
   * @brief Get the epoch of the NexthopCache status that the RIB last read.
   * See NexthopStatus::getEpoch().
   */
  uint64_t getEpoch() const {
    return status_.getEpoch();
  }

  /**
   * @brief Update the status of this nexthop
   * @param status The new status for this nexthop
   */
  void updateStatus(const NexthopStatus& status) {
    const auto now = std::chrono::steady_clock::now();
    if (status_.isReachable() != status.isReachable()) {
      lastReachabilityChangeTs_ = now;
    }
    if (status_.getIgpCost() != status.getIgpCost()) {
      lastIgpCostChangeTs_ = now;
    }
    status_ = status;
  }

  /**
   * @brief Time of the last reachability transition for this nexthop.
   * @return steady_clock time_point of the last change; nullopt if the source
   *         has never reported the nexthop as reachable.
   */
  std::optional<std::chrono::steady_clock::time_point>
  getLastReachabilityChangeTs() const {
    return lastReachabilityChangeTs_;
  }

  /**
   * @brief Time of the last IGP-cost change for this nexthop.
   * @return steady_clock time_point of the last change; nullopt if a cost has
   *         never been received.
   */
  std::optional<std::chrono::steady_clock::time_point> getLastIgpCostChangeTs()
      const {
    return lastIgpCostChangeTs_;
  }

  void linkRouteInfo(RouteInfo& routeInfo) {
    if (nexthopAssociationList_.link(routeInfo)) {
      routeInfo.setNexthopInfo(this);
    } else {
      const auto& nexthop = status_.getNexthopKey();
      XLOGF(
          DBG2,
          "Not linking RouteInfo to nexthop {} (vrf={}): RouteInfo already associated with NexthopInfo",
          nexthop.ip.str(),
          nexthop.nextHopVrfId);
    }
  }

  bool unlinkRouteInfo(RouteInfo& routeInfo) noexcept override {
    if (!nexthopAssociationList_.unlink(routeInfo)) {
      const auto& nexthop = status_.getNexthopKey();
      XLOGF(
          DBG2,
          "Not unlinking RouteInfo from nexthop {} (vrf={}): RouteInfo not associated with NexthopInfo",
          nexthop.ip.str(),
          nexthop.nextHopVrfId);
      return false;
    }
    routeInfo.setNexthopInfo(nullptr);
    return true;
  }

  bool isRouteInfoListEmpty() const {
    return nexthopAssociationList_.empty();
  }

  std::size_t getRouteInfoListSize() const {
    return nexthopAssociationList_.size();
  }

  /**
   * @brief Get an iterator to the beginning of the RouteInfo list
   * @return Iterator to the beginning of the RouteInfo list
   */
  auto begin() const {
    return nexthopAssociationList_.begin();
  }

  /**
   * @brief Get an iterator to the end of the RouteInfo list
   * @return Iterator to the end of the RouteInfo list
   */
  auto end() const {
    return nexthopAssociationList_.end();
  }

 private:
  // Nexthop status containing IP address and IGP cost
  NexthopStatus status_;
  // List of routes associated with the nexthop
  NexthopAssociationList nexthopAssociationList_;
  /*
   * Timestamps of the last reachability / IGP-cost change. nullopt means the
   * corresponding value has never been reported since this entry was created.
   */
  std::optional<std::chrono::steady_clock::time_point>
      lastReachabilityChangeTs_;
  std::optional<std::chrono::steady_clock::time_point> lastIgpCostChangeTs_;
};

} // namespace facebook::bgp
