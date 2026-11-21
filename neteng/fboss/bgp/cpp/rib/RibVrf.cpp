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

#include "neteng/fboss/bgp/cpp/rib/RibVrf.h"

namespace facebook::bgp {

RibVrf::RibVrf(VrfId vrfId) : vrfId_(vrfId) {}

void RibVrf::VrfProgrammingRequests::push(RibVrf& vrf) noexcept {
  if (vrf.programmingRequestHook_.is_linked()) {
    return;
  }
  requests.push_back(vrf);
  if (waiter_) {
    auto waiter = std::move(*waiter_);
    waiter_.reset();
    waiter.setValue();
  }
}

folly::coro::Task<RibVrf&> RibVrf::VrfProgrammingRequests::pop() {
  /*
   * Recheck after every wakeup: a VRF destroyed while pending unlinks itself
   * and can leave the list empty again.
   */
  while (requests.empty()) {
    auto [waiter, woken] = folly::coro::makePromiseContract<void>();
    waiter_ = std::move(waiter);
    // Awaiting the future completes it with OperationCancelled on cancellation.
    auto result = co_await folly::coro::co_awaitTry(std::move(woken));
    if (result.hasException()) {
      waiter_.reset();
      co_yield folly::coro::co_error(std::move(result).exception());
    }
  }
  auto& vrf = requests.front();
  requests.pop_front();
  co_return vrf;
}

} // namespace facebook::bgp
