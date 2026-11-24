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
#include <cstdint>

#include <folly/IPAddress.h>
#include <folly/hash/Hash.h>

#include "fboss/agent/Constants.h"

namespace facebook::bgp {

/*
 * A resolution result is identified by its address and nexthop VRF. For
 * example, NexthopKey{.ip = vtep, .nextHopVrfId = 7} selects VRF 7. Omitting
 * the VRF selects VRF 0.
 */
struct NexthopKey {
  folly::IPAddress ip;
  uint16_t nextHopVrfId{fboss::kDefaultVrf};

  bool operator==(const NexthopKey&) const = default;

  struct Hash {
    size_t operator()(const NexthopKey& key) const noexcept {
      return folly::hash::hash_combine(key.ip, key.nextHopVrfId);
    }
  };
};

} // namespace facebook::bgp
