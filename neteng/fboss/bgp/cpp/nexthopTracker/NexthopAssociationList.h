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

#include <cstddef>

#include <folly/container/IntrusiveList.h>
#include "neteng/fboss/bgp/cpp/common/RouteInfo.h"

namespace facebook::bgp {

/**
 * @class NexthopAssociationList
 *        This class does not hold thread-safe structures, in BGP++ it is
 *        accessed(read and write) only by the RIB thread
 * @brief A list of RouteInfo objects associated with a nexthop.
 */
class NexthopAssociationList {
 public:
  NexthopAssociationList() = default;

  /**
   * Link a RouteInfo to the association list.
   *
   * @param routeInfo The RouteInfo to link to the list
   * @return true if the RouteInfo was successfully linked to the list,
   *         false if the RouteInfo was already in the list and not linked
   */
  bool link(RouteInfo& routeInfo) {
    if (routeInfo.nextHopListHook_.is_linked()) {
      return false;
    }
    routeInfoList_.push_back(routeInfo);
    return true;
  }

  /**
   * Unlink a RouteInfo from the association list.
   *
   * @param routeInfo The RouteInfo to unlink from the list
   * @return true if the RouteInfo was successfully unlinked from the list,
   *         false if the RouteInfo was not in the list and couldn't be unlinked
   */
  bool unlink(RouteInfo& routeInfo) {
    if (!routeInfo.nextHopListHook_.is_linked()) {
      return false;
    }
    routeInfoList_.erase(routeInfoList_.iterator_to(routeInfo));
    return true;
  }

  bool empty() const {
    return routeInfoList_.empty();
  }

  std::size_t size() const {
    return routeInfoList_.size();
  }

  /**
   * @brief Get an iterator to the beginning of the RouteInfo list
   * @return Iterator to the beginning of the RouteInfo list
   */
  auto begin() const {
    return routeInfoList_.begin();
  }

  auto begin() {
    return routeInfoList_.begin();
  }

  /**
   * @brief Get an iterator to the end of the RouteInfo list
   * @return Iterator to the end of the RouteInfo list
   */
  auto end() const {
    return routeInfoList_.end();
  }

  auto end() {
    return routeInfoList_.end();
  }

 private:
  // List of routes associated with the nexthop
  folly::CountedIntrusiveList<RouteInfo, &RouteInfo::nextHopListHook_>
      routeInfoList_;
};

} // namespace facebook::bgp
