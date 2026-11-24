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
#include <stdexcept>
#include <utility>

#include <fmt/format.h>
#include <folly/Range.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Task.h>
#include <folly/coro/Timeout.h>
#include <folly/futures/Future.h>
#include <folly/synchronization/Baton.h>

namespace facebook::bgp::test {

/*
 * Default per-pop timeout. Sized so the slowest legitimate BGP test pop
 * (TSAN-mode e2e session setup, ~30-60s end-to-end) completes well within
 * budget while a real hang still fails fast. tpx kills at 600s, so 90s
 * leaves ~510s headroom for any per-test setup before the runner steps
 * in. Failures surface as test FAILUREs (assertion thrown via
 * BoundedWaitTimeout), not tpx TIMEOUTs (which fbcode CI suppresses as
 * warnings).
 */
inline constexpr std::chrono::seconds kDefaultPopTimeout{90};

/*
 * Default Baton wait timeout. Intra-test choreography is fast; 10s catches
 * a dead producer without false-tripping on slow CI hosts.
 */
inline constexpr std::chrono::seconds kDefaultBatonTimeout{10};

/*
 * Thrown by boundedPop / boundedBlockingPop / boundedBatonWait when the
 * configured timeout expires. Callers that need to distinguish "wait
 * timed out" from "queue closed / other exception" should catch this
 * type specifically and let it propagate so the test fails with a clear
 * message instead of silently bailing.
 */
class BoundedWaitTimeout : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

namespace detail {
/* Extract T from folly::coro::Task<T>; used to deduce pop() return type. */
template <typename T>
struct CoroTaskValue;

template <typename T>
struct CoroTaskValue<folly::coro::Task<T>> {
  using type = T;
};

template <typename Queue>
using PopResultT =
    typename CoroTaskValue<decltype(std::declval<Queue&>().pop())>::type;
} // namespace detail

/*
 * Bounded coroutine pop. Use anywhere a test currently does
 * `co_await queue.pop()` with no timeout.
 *
 * Returns the popped value on success. On timeout, throws via
 * `co_yield co_error(BoundedWaitTimeout(...))` with a message naming the
 * queue (queueName) and the budget — so the test fails fast with a
 * self-locating error instead of hanging until tpx kills it. Callers
 * that need to distinguish timeout from other failures should catch
 * `BoundedWaitTimeout` specifically.
 *
 * Any other exception (e.g. queue closed) propagates unchanged.
 */
template <typename Queue>
folly::coro::Task<detail::PopResultT<Queue>> boundedPop(
    Queue& q,
    folly::StringPiece queueName,
    std::chrono::seconds timeout = kDefaultPopTimeout) {
  auto result =
      co_await folly::coro::co_awaitTry(folly::coro::timeout(q.pop(), timeout));
  if (result.hasException()) {
    if (result.exception()
            .template is_compatible_with<folly::FutureTimeout>()) {
      co_yield folly::coro::co_error(BoundedWaitTimeout(
          fmt::format(
              "boundedPop timed out after {}s on '{}' — expected value never "
              "arrived; likely the producer is stuck or the test setup is wrong",
              timeout.count(),
              queueName)));
    }
    co_yield folly::coro::co_error(std::move(result).exception());
  }
  co_return std::move(result).value();
}

/*
 * Bounded blocking pop. Use anywhere a test currently does
 * `folly::coro::blockingWait(queue.pop())` from synchronous (non-coro)
 * test code.
 *
 * Returns the popped value on success. On timeout, throws
 * `BoundedWaitTimeout` (subclass of std::runtime_error) with the same
 * message format as boundedPop. The exception propagates out of
 * blockingWait and fails the test.
 */
template <typename Queue>
detail::PopResultT<Queue> boundedBlockingPop(
    Queue& q,
    folly::StringPiece queueName,
    std::chrono::seconds timeout = kDefaultPopTimeout) {
  return folly::coro::blockingWait(boundedPop(q, queueName, timeout));
}

/*
 * Bounded Baton wait. Use anywhere a test currently does
 * `baton.wait()` (the unbounded form). Templated to accept any baton-like
 * type with a `try_wait_for(duration) -> bool` method — covers both
 * `folly::Baton<>` and `folly::fibers::Baton`.
 *
 * On timeout, throws BoundedWaitTimeout naming the wait context — so a
 * dead producer surfaces as a clear test failure instead of a hang.
 */
template <typename Baton>
void boundedBatonWait(
    Baton& baton,
    folly::StringPiece context,
    std::chrono::seconds timeout = kDefaultBatonTimeout) {
  if (!baton.try_wait_for(timeout)) {
    throw BoundedWaitTimeout(
        fmt::format(
            "boundedBatonWait timed out after {}s on '{}' — baton was never "
            "posted; likely the producer thread died or never reached the post",
            timeout.count(),
            context));
  }
}

} // namespace facebook::bgp::test
