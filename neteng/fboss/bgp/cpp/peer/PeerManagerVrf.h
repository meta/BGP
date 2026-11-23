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

#include <folly/coro/AsyncScope.h>

#include "neteng/fboss/bgp/cpp/common/Types.h"

namespace facebook::bgp {

class PeerManagerBase;

/*
 * Mutable PeerManager state for one routing table. Runs on the owner's
 * EventBase.
 */
class PeerManagerVrf final {
 public:
  PeerManagerVrf(
      VrfId vrfId,
      PeerManagerBase& owner,
      folly::coro::CancellableAsyncScope& asyncScope);
  ~PeerManagerVrf() = default;

  PeerManagerVrf(const PeerManagerVrf&) = delete;
  PeerManagerVrf& operator=(const PeerManagerVrf&) = delete;
  PeerManagerVrf(PeerManagerVrf&&) = delete;
  PeerManagerVrf& operator=(PeerManagerVrf&&) = delete;

  VrfId vrfId() const {
    return vrfId_;
  }

 private:
  // Identifies the routing table owned by this context.
  const VrfId vrfId_;
  [[maybe_unused]] PeerManagerBase& owner_;

  // The owner's scope, for coroutines scheduled on behalf of this table.
  [[maybe_unused]] folly::coro::CancellableAsyncScope& asyncScope_;
};

} // namespace facebook::bgp
