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
#include <folly/FixedString.h>

#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

#include <fb303/ThreadCachedServiceData.h>
#include <fb303/detail/QuantileStatWrappers.h>

namespace facebook::bgp {
using folly::string_literals::operator""_fs;

namespace BgpStatsBB {

// addPeers thrift API counters
constexpr auto kAddPeersSuccess = "bgpd.addPeers.success"_fs;
constexpr auto kAddPeersRejected = "bgpd.addPeers.rejected"_fs;

// delPeers thrift API counters
constexpr auto kDelPeersSuccess = "bgpd.delPeers.success"_fs;
constexpr auto kDelPeersRejected = "bgpd.delPeers.rejected"_fs;

/*
 * The effective link-up hold setting. 0 when enable_netlink_dampening was not
 * requested, or when it was requested and forced off because
 * bgp_resolve_nexthops_from_interface_state is set.
 */
constexpr auto kNetlinkDampeningEnabled =
    "bgpd.config.netlink_dampening_enabled"_fs;

void initCounters();

// Increment peer-management API counters
void incrAddPeersSuccess();
void incrAddPeersRejected();
void incrDelPeersSuccess();
void incrDelPeersRejected();

} // namespace BgpStatsBB

namespace RibStatsBB {

/*
 * unexpected CTE/CPS policy message received on a platform that does not
 * support it (e.g., BB receiving a RouteAttributePolicySetMsg)
 */
inline constexpr auto kUnsupportedPolicyMsg =
    "bgpd.ribPolicy.numUnsupportedPolicyMsg";
DECLARE_timeseries(unsupportedPolicyMsg);

/*
 * Link-up hold (link-flap dampening) counters.
 *
 * The active count is a gauge, because an operator must know how many
 * interfaces have a hold right now. The started and released counts are
 * timeseries, because they show the flap rate over time.
 *
 * These counters give the device total. A timeseries key is a fixed string, so
 * they cannot name an interface. To find the interface, read the [LinkHold] log
 * lines.
 */
inline const auto kNhtLinkHoldActive =
    fmt::format("{}.nht.link_hold.active", kBgpcppTag);
void setNhtLinkHoldActive(int64_t count);

inline const auto kNhtLinkHoldStarted =
    fmt::format("{}.nht.link_hold.started", kBgpcppTag);
DECLARE_timeseries(nhtLinkHoldStarted);
void incrNhtLinkHoldStarted();

inline const auto kNhtLinkHoldReleased =
    fmt::format("{}.nht.link_hold.released", kBgpcppTag);
DECLARE_timeseries(nhtLinkHoldReleased);
void incrNhtLinkHoldReleased();

void initCounters();

} // namespace RibStatsBB

void initStatsBB();

} // namespace facebook::bgp
