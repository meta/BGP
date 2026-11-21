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

#include <folly/logging/xlog.h>

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/rib/RibBase.h"

namespace facebook::bgp {

RibVrf::RibVrf(VrfId vrfId, RibBase& owner)
    : vrfId_(vrfId),
      owner_(owner),
      /*
       * Only the default VRF publishes its counts to fb303.
       * TODO_CTR_VRF: Publish the counts of every VRF.
       */
      ribCounters_(vrfId == fboss::kDefaultVrf) {
  /*
   * initialize fibBatchTimer_ for batch processing of fib programming
   * requests
   */
  fibBatchTimer_ = folly::AsyncTimeout::make(
      owner_.evb_, [this]() noexcept { owner_.prepareFibProgramming(vrfId_); });
}

void RibVrf::addToFibBatch(RibEntry& entry) noexcept {
  if (!entry.isOnFibBatchList()) {
    fibBatchList_.push_back(entry);
  }
}

void RibVrf::replaceFibOut(RibEntry& entry, FibOutState state) {
  DCHECK(owner_.isFibOutTrackingEnabled());
  CHECK(state.nexthops);
  entry.fibOutState_ = std::make_shared<const FibOutState>(std::move(state));
}

void RibVrf::eraseFibOut(RibEntry& entry) {
  entry.fibOutState_.reset();
  entry.markFibOutStateAbsent();
}

const FibOutState* RibVrf::findFibOut(const RibEntry& entry) const {
  return entry.fibOutState_.get();
}

void RibVrf::clear() {
  XLOGF(
      INFO,
      "[Exit] Clearing ribEntries_ for VRF {} ({} entries)...",
      vrfId_,
      ribEntries_.size());
  ribEntries_.clear();
  fibNexthopSets_.clear();
  ribCounters_.reset();
}

void RibVrf::remove(RibEntry& entry) {
  eraseFibOut(entry);
  // The callback runs only if the entry is erased, before it is destroyed.
  ribEntries_.eraseInto(
      entry.getPrefix(),
      [this](const folly::CIDRNetwork& prefix, const RibEntry& erased) {
        ribCounters_.onPrefixRemoved(
            prefix.first.isV4(), prefix.second, erased.getInactivePathCnt());
      });
}

void RibVrf::schedulePrepareFibProgrammingTimer() noexcept {
  if (owner_.pauseBestPathAndFibProgramming_ || fibBatchTimer_->isScheduled()) {
    /*
     * Best path selection and Fib programming is paused or timer has been
     * scheduled already
     */
    return;
  }

  // Schedule fib programming in a batch fashion
  fibBatchTimer_->scheduleTimeout(owner_.getFibBatchTime());
}

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
