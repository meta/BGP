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

#include <folly/IPAddress.h>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>

#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopKey.h"
#include "openr/if/gen-cpp2/Network_types.h"
#include "openr/if/gen-cpp2/Platform_types.h"

namespace facebook::bgp {

/**
 * @class NexthopStatus
 * @brief Class to store a nexthop resolution key and its reachability metadata.
 */
class NexthopStatus {
 public:
  /**
   * @brief Constructor for NexthopStatus
   * @param nexthop The address and resolution VRF of the nexthop
   * @param isReachable Whether the nexthop is reachable
   * @param igpCost Optional IGP cost to reach the nexthop. If IgpCost is unset,
   * the value is considered as INT_MAX by BGP++
   * @param isConnected whether the nexthop is directly connected or not. If
   * isConnected is unset, then it is considered as unknown.
   * @param excludeNexthopWithoutCost If true, a reachable nexthop that has no
   * IGP cost is treated as unresolved and excluded from best-path selection
   * (EBB semantics). If false, such a nexthop stays eligible for best-path
   * selection (FBOSS semantics). Defaults to true. See
   * isResolvedForSelection().
   * @param ifName Name of the interface that the nexthop is reachable through.
   * The name uses the kernel format, for example "po1331". Only the
   * interface-state resolution path sets it. All other sources leave it unset.
   * See getIfName().
   */
  explicit NexthopStatus(
      const NexthopKey& nexthop,
      bool isReachable,
      std::optional<uint32_t> igpCost = std::nullopt,
      std::optional<bool> isConnected = std::nullopt,
      bool excludeNexthopWithoutCost = true,
      std::optional<std::string> ifName = std::nullopt)
      : nexthop_(nexthop),
        isReachable_(isReachable),
        igpCost_(isReachable ? igpCost : std::nullopt),
        isConnected_(isConnected),
        excludeNexthopWithoutCost_(excludeNexthopWithoutCost),
        ifName_(std::move(ifName)) {}

  /**
   * @brief Constructor for NexthopStatus from openr::thrift::NextHopStatus
   * @param nexthop The address and resolution VRF of the nexthop
   * @param thriftStatus The thrift NextHopStatus to convert from
   * @param isConnected whether the nexthop is directly connected or not. If
   * isConnected is unset, then it is considered as unknown.
   * @param excludeNexthopWithoutCost If true, a reachable nexthop that has no
   * IGP cost is treated as unresolved and excluded from best-path selection
   * (EBB semantics). If false, such a nexthop stays eligible for best-path
   * selection (FBOSS semantics). Defaults to true. See
   * isResolvedForSelection().
   */
  explicit NexthopStatus(
      const NexthopKey& nexthop,
      const openr::thrift::NextHopStatus& thriftStatus,
      std::optional<bool> isConnected = std::nullopt,
      bool excludeNexthopWithoutCost = true)
      : nexthop_(nexthop),
        isConnected_(isConnected),
        excludeNexthopWithoutCost_(excludeNexthopWithoutCost) {
    bool isReachable = thriftStatus.isReachable().value();
    std::optional<uint32_t> igpCost = thriftStatus.igpCost().to_optional();
    isReachable_ = isReachable;
    igpCost_ = isReachable ? igpCost : std::nullopt;
  }

  const NexthopKey& getNexthopKey() const {
    return nexthop_;
  }

  /**
   * @brief Get the nexthop IP address
   * @return The nexthop IP address
   */
  const folly::IPAddress& getNexthop() const {
    return nexthop_.ip;
  }

  /**
   * @brief Check whether the source reports the nexthop as reachable.
   */
  bool isReachable() const {
    return isReachable_;
  }

  /**
   * @brief Get the IGP cost to reach the nexthop
   * @return Optional IGP cost, or nullopt if the source supplied no cost.
   */
  std::optional<uint32_t> getIgpCost() const {
    return igpCost_;
  }

  /**
   * @brief Get the whether the nexthop is directly connected or not
   * @return bool, true if the nexthop is directly connected,
   * false if not directly connected, nullopt if unknown
   */
  std::optional<bool> isConnected() const {
    return isConnected_;
  }

  /**
   * @brief Get the interface that the nexthop is reachable through.
   *
   * The fib agent can put a route into an EOS nexthop-group. Each entry in a
   * nexthop-group must carry an egress interface. Only the interface-state
   * resolution path knows the interface. Thus the name is unset for every
   * other nexthop source. An unset name makes the fib agent send no interface
   * name.
   *
   * @return Interface name in kernel format, for example "po1331". The value
   * is std::nullopt if the source of the nexthop does not know the interface.
   */
  const std::optional<std::string>& getIfName() const {
    return ifName_;
  }

  /**
   * @brief Whether this nexthop is resolved for the purpose of best-path
   * selection.
   *
   * A nexthop is resolved when its source reports it as reachable and either
   * supplies an IGP cost or allows selection without a cost.
   *
   * @return true if the nexthop is eligible for best-path selection
   */
  bool isResolvedForSelection() const {
    return isReachable_ &&
        (igpCost_.has_value() || !excludeNexthopWithoutCost_);
  }

  /**
   * @brief Get the epoch of the NexthopCache write that stored this status.
   * @return A number that NexthopCache increments on every write. A larger
   * epoch means a later write. 0 means NexthopCache did not store this status.
   */
  uint64_t getEpoch() const {
    return epoch_;
  }

 private:
  friend class NexthopCache;

  NexthopKey nexthop_;

  /*
   * Whether the nexthop source reports the nexthop as reachable.
   */
  bool isReachable_{false};

  /*
   * If metric/cost to reach a reachable nexthop, nullopt if unreachable
   * If IgpCost is unset, the value is considered as INT_MAX by BGP++
   */
  std::optional<uint32_t> igpCost_;

  /*
   * True if the nexthop is directly connected, false if not directly connected,
   * nullopt if unknown
   */
  std::optional<bool> isConnected_;

  /*
   * Policy for a reachable nexthop that has no IGP cost: if true, treat it as
   * unresolved and exclude it from best-path selection (EBB); if false, keep it
   * eligible for selection (FBOSS). Hardcoded false by the FBOSS nexthop source
   * (FsdbFibWatcher); defaults to true for all other sources.
   *
   * TODO: This flag exists only to keep EBB on its current (exclude) behavior
   * while FBOSS opts in. Once EBB is also switched to false and verified in
   * production, this flag can be removed entirely and isResolvedForSelection()
   * can rely solely on the isReachable_ flag.
   */
  bool excludeNexthopWithoutCost_{true};

  /*
   * Name of the interface that the nexthop is reachable through, in kernel
   * format. Only NetlinkWrapper sets it, and only when
   * bgp_resolve_nexthops_from_interface_state is true. The value stays unset
   * for every other nexthop source.
   */
  std::optional<std::string> ifName_;

  /*
   * Set only by NexthopCache, on every write. See getEpoch().
   */
  uint64_t epoch_{0};
};

/* Registration is independent for each nexthop resolution key. */
using NexthopStatusWithRegistration = std::tuple<NexthopStatus, bool>;

} // namespace facebook::bgp
