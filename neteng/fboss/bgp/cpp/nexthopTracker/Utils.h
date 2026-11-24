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
#include <unordered_map>
#include <vector>

#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopStatus.h"
#include "openr/common/NetworkUtil.h"
#include "openr/if/gen-cpp2/Platform_types.h"

namespace facebook::bgp {

namespace {

NexthopStatus makeNexthopStatus(
    const NexthopKey& nexthop,
    bool isReachable,
    std::optional<uint32_t> igpCost) {
  return NexthopStatus(nexthop, isReachable, igpCost, /*isConnected=*/false);
}
} // namespace

/**
 * @brief Convert a map of <binary string IPs, thrift NextHopStatus> to a vector
 * of NexthopStatus objects
 *
 * @param fibAgentStatusMap Map of binary string NextHop IPs to their status
 * provided by OpenR FibAgent
 * @return std::vector<NexthopStatus> Vector of NexthopStatus objects
 */
inline std::vector<NexthopStatus> convertFibAgentStatusToNexthopStatus(
    const std::map<std::string, openr::thrift::NextHopStatus>&
        fibAgentStatusMap) {
  std::vector<NexthopStatus> vec;
  vec.reserve(fibAgentStatusMap.size());
  for (const auto& [binaryIp, thriftStatus] : fibAgentStatusMap) {
    // Convert binary IP to folly::IPAddress
    folly::IPAddress nexthopIp = openr::toIPAddress(binaryIp);

    // Create NexthopStatus with the nexthop IP and thrift status
    vec.emplace_back(makeNexthopStatus(
        NexthopKey{.ip = nexthopIp, .nextHopVrfId = fboss::kDefaultVrf},
        *thriftStatus.isReachable(),
        thriftStatus.igpCost().to_optional()));
  }
  return vec;
}

} // namespace facebook::bgp
