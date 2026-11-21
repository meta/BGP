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

#include <chrono>
#include <memory>
#include <vector>

#include <folly/CancellationToken.h>
#include <folly/OperationCancelled.h>
#include <folly/coro/Collect.h>
#include <folly/coro/CurrentExecutor.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Timeout.h>
#include <folly/futures/Future.h>
#include <gtest/gtest.h>

#include "neteng/fboss/bgp/cpp/rib/RibVrf.h"

namespace facebook::bgp {

namespace {
constexpr std::chrono::milliseconds kPopTimeout{50};

folly::coro::Task<VrfId> popVrfId(RibVrf::VrfProgrammingRequests* requests) {
  co_return (co_await requests->pop()).vrfId();
}
} // namespace

CO_TEST(VrfProgrammingRequestsTest, PopsVrfsInPushOrder) {
  RibVrf::VrfProgrammingRequests requests;
  RibVrf vrf0{0};
  RibVrf vrf1{1};
  RibVrf vrf2{2};

  requests.push(vrf1);
  requests.push(vrf0);
  requests.push(vrf2);
  // Already linked, so the second push is ignored.
  requests.push(vrf1);

  EXPECT_EQ(1, (co_await requests.pop()).vrfId());
  EXPECT_EQ(0, (co_await requests.pop()).vrfId());
  EXPECT_EQ(2, (co_await requests.pop()).vrfId());
  EXPECT_TRUE(requests.requests.empty());
}

CO_TEST(VrfProgrammingRequestsTest, PushWakesWaitingPop) {
  RibVrf::VrfProgrammingRequests requests;
  RibVrf vrf1{1};

  auto [popped, _] = co_await folly::coro::collectAll(
      popVrfId(&requests),
      [](auto* requests, RibVrf* vrf) -> folly::coro::Task<void> {
        requests->push(*vrf);
        co_return;
      }(&requests, &vrf1));

  EXPECT_EQ(1, popped);
  EXPECT_TRUE(requests.requests.empty());
}

/*
 * A VRF destroyed while pending unlinks itself. pop() must skip it and keep
 * waiting instead of returning a destroyed VRF.
 */
CO_TEST(VrfProgrammingRequestsTest, DeletedPendingVrfIsSkipped) {
  RibVrf::VrfProgrammingRequests requests;
  std::vector<std::unique_ptr<RibVrf>> vrfs;
  for (VrfId vrfId = 0; vrfId < 5; ++vrfId) {
    vrfs.push_back(std::make_unique<RibVrf>(vrfId));
    requests.push(*vrfs.back());
  }

  vrfs[2].reset();

  for (VrfId expected : {0, 1, 3, 4}) {
    EXPECT_EQ(expected, (co_await requests.pop()).vrfId());
  }
  EXPECT_TRUE(requests.requests.empty());

  // The list is empty, so pop() waits until the timeout cancels it.
  auto extraPop = co_await folly::coro::co_awaitTry(
      folly::coro::timeout(requests.pop(), kPopTimeout));
  EXPECT_TRUE(extraPop.hasException<folly::FutureTimeout>());

  RibVrf vrf5{5};
  requests.push(vrf5);
  EXPECT_EQ(5, (co_await requests.pop()).vrfId());
  EXPECT_TRUE(requests.requests.empty());
}

/*
 * The push that wakes pop() can link a VRF that is destroyed before pop()
 * resumes. pop() must find the list empty and wait for the next push.
 */
CO_TEST(VrfProgrammingRequestsTest, WaitsAgainWhenWokenVrfIsDeleted) {
  RibVrf::VrfProgrammingRequests requests;
  auto vrf1 = std::make_unique<RibVrf>(1);
  RibVrf vrf2{2};

  auto [popped, _] = co_await folly::coro::collectAll(
      popVrfId(&requests),
      [](auto* requests,
         std::unique_ptr<RibVrf>* vrf1,
         RibVrf* vrf2) -> folly::coro::Task<void> {
        requests->push(**vrf1);
        vrf1->reset();
        // Let pop() resume, find the list empty and wait again.
        co_await folly::coro::co_reschedule_on_current_executor;
        requests->push(*vrf2);
      }(&requests, &vrf1, &vrf2));

  EXPECT_EQ(2, popped);
  EXPECT_TRUE(requests.requests.empty());
}

CO_TEST(VrfProgrammingRequestsTest, CancellationEndsWaitingPop) {
  RibVrf::VrfProgrammingRequests requests;
  folly::CancellationSource cancelSource;

  auto [popped, _] = co_await folly::coro::collectAllTry(
      folly::coro::co_withCancellation(
          cancelSource.getToken(), popVrfId(&requests)),
      [](folly::CancellationSource* cancelSource) -> folly::coro::Task<void> {
        cancelSource->requestCancellation();
        co_return;
      }(&cancelSource));

  EXPECT_TRUE(popped.hasException<folly::OperationCancelled>());

  // A later pop() is not affected by the cancelled one.
  RibVrf vrf1{1};
  requests.push(vrf1);
  EXPECT_EQ(1, (co_await requests.pop()).vrfId());
}

} // namespace facebook::bgp
