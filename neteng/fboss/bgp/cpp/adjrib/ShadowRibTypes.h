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

#include <cstdint>

#include <folly/IPAddress.h>
#include <folly/container/F14Map.h>

#include "neteng/fboss/bgp/cpp/changeTracker/TrackableObject.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"

namespace facebook::bgp {

/*
 * Type alias for the shadow RIB entries map.
 * This is the canonical definition used across PeerManagerBase,
 * UpdateGroupManager, and AdjRibGroup to ensure type consistency.
 *
 * The map stores shadow RIB entries wrapped in TrackableObject for change
 * tracking via the changeListTracker publish/subscribe pattern.
 */
using ShadowRibEntriesMap = folly::F14NodeMap<
    folly::CIDRNetwork,
    std::unique_ptr<TrackableObject<ShadowRibEntry>>>;

/*
 * Non-owning view of a PeerManagerVrf's shadow RIB handed to an update group:
 * the entries map plus a live reference to the VRF's max seen RIB version
 * (maxRibVersion_). Both bind to PeerManagerVrf members, which outlive every
 * group its PeerManagerBase creates.
 */
struct ShadowRibView {
  const ShadowRibEntriesMap& entries;
  const uint64_t& maxRibVersion;

  /*
   * Sentinels for groups constructed without a shadow RIB (direct test
   * construction). Function-local statics rather than namespace-scope globals:
   * a group binds references to them at construction, so they must be
   * initialized before first use regardless of translation unit
   * initialization order. A group bound to empty() walks no entries and never
   * advances lastSeenRibVersion_ off a dump.
   */
  static const ShadowRibEntriesMap& emptyEntries() {
    static const ShadowRibEntriesMap kEmptyEntries;
    return kEmptyEntries;
  }

  static const uint64_t& zeroRibVersion() {
    static constexpr uint64_t kZeroRibVersion = 0;
    return kZeroRibVersion;
  }

  static const ShadowRibView& empty() {
    static const ShadowRibView kEmptyView{emptyEntries(), zeroRibVersion()};
    return kEmptyView;
  }
};

} // namespace facebook::bgp
