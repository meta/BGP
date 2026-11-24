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

#include <folly/coro/BlockingWait.h>
#include <folly/coro/Sleep.h>
#include <folly/coro/Task.h>
#include <folly/logging/xlog.h>

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/BgpProfiler.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/ThriftClientUtils.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopHandler.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/Utils.h"
#include "openr/if/gen-cpp2/Platform_types.h"

namespace facebook::bgp {

void NexthopHandler::run() noexcept {
  XLOG(INFO, "Starting NexthopHandler");

  // Start nexthop status stream handler routine
  asyncScope_.add(co_withExecutor(&evb_, handleNextHopStatusStreamRoutine()));

  // Run event base loop
  evb_.loopForever();

  XLOG(INFO, "[Exit] Successfully terminated NexthopHandler event-base");
}

void NexthopHandler::stop() noexcept {
  XLOG(INFO, "[Exit] Cancel and stop all coroutines in NexthopHandler");
  folly::coro::blockingWait(asyncScope_.cancelAndJoinAsync());

  // Reset client
  disconnectOpenrFibAgent();

  // Terminate event base to shutdown
  evb_.terminateLoopSoon();

  XLOG(INFO, "[Exit] Successfully stopped NexthopHandler");
}

void NexthopHandler::connectOpenrFibAgent() {
  // Check if existing client has a good connection
  if (isThriftClientHealthy(client_)) {
    return;
  }
  client_.reset();

  // Create new client using the template utility
  client_ =
      createThriftClient<apache::thrift::Client<openr::thrift::FibService>>(
          evb_,
          kLoopBackAddressV6,
          openrFibAgentPort_,
          kFibOpenrConnTimeout,
          kFibOpenrSendTimeout,
          kFibOpenrRecvTimeout);
}

void NexthopHandler::disconnectOpenrFibAgent() {
  XLOG(INFO, "Disconnecting OpenR FIB agent");

  // Reset client
  client_.reset();
}

folly::coro::Task<void> NexthopHandler::handleNextHopStatusStreamRoutine() {
  XLOGF(
      INFO,
      "Starting nexthop status stream routine (vrf={})",
      fboss::kDefaultVrf);

  while (true) {
    // When cancelAndJoinAsync is called, guaranteed to exit
    co_await folly::coro::co_safe_point;

    try {
      if (!client_) {
        XLOGF(
            INFO,
            "Connecting to OpenR FIB agent before starting nexthop status stream (vrf={})",
            fboss::kDefaultVrf);
        connectOpenrFibAgent();
      }

      // Create the request
      openr::thrift::StreamNextHopStatusRequest request;
      request.stream_all_loopbacks() = true;

      // Start the stream
      XLOGF(
          INFO,
          "Starting nexthop status stream from OpenR FIB agent (vrf={})",
          fboss::kDefaultVrf);
      auto stream = co_await client_->co_streamNextHopStatus(request);

      // Convert the stream to an AsyncGenerator
      auto asyncGen = std::move(stream).toAsyncGenerator();

      // Process the stream responses
      co_await handleStream(std::move(asyncGen));
    } catch (const openr::thrift::PlatformError& ex) {
      XLOGF(
          ERR,
          "OpenR FIB agent platform error (vrf={}): {}",
          fboss::kDefaultVrf,
          *ex.message());
      // Disconnect agent and reconnect on platform error
      disconnectOpenrFibAgent();
      // TODO: Add stats here to track platform errors
    } catch (const std::exception& ex) {
      XLOGF(
          ERR,
          "Stream connection error (vrf={}): {}",
          fboss::kDefaultVrf,
          ex.what());
      // Disconnect agent and reconnect on stream error
      disconnectOpenrFibAgent();
      // TODO: Add stats here to track stream errors
    }

    // Sleep before retrying
    co_await folly::coro::sleepReturnEarlyOnCancel(kOpenrFibAgentTimeout);
  }

  XLOGF(
      INFO,
      "Exiting nexthop status stream routine (vrf={})",
      fboss::kDefaultVrf);
  co_return;
}

folly::coro::Task<void> NexthopHandler::handleStream(
    folly::coro::AsyncGenerator<openr::thrift::StreamNextHopStatusResponse&&>
        asyncGen) {
  while (auto update = co_await asyncGen.next()) {
    // When cancelAndJoinAsync is called, guaranteed to exit
    co_await folly::coro::co_safe_point;

    if (!update.has_value()) {
      XLOGF(
          INFO,
          "Nexthop stream handling ended: received empty update (vrf={})",
          fboss::kDefaultVrf);
      break;
    }

    // TODO: Add stats here to track number of updates received

    // Process the updates
    XLOGF(
        INFO,
        "Received nexthop status update with {} entries (vrf={})",
        update.value().nexthopStatuses()->size(),
        fboss::kDefaultVrf);
    co_await co_processUpdates(update.value());
  }
}

folly::coro::Task<void> NexthopHandler::co_processUpdates(
    const openr::thrift::StreamNextHopStatusResponse& response) {
  ScopedProfile profile("NexthopHandler::processUpdates");
  // Process the response
  const auto& nexthopStatuses = *response.nexthopStatuses();

  /*
   * Convert the map to a vector of NexthopStatus objects using the utility
   * function and move it directly to update the cache
   */
  co_await co_updateCacheAndNotifyRib(
      convertFibAgentStatusToNexthopStatus(nexthopStatuses));
}

folly::coro::Task<void> NexthopHandler::co_updateCacheAndNotifyRib(
    const std::vector<NexthopStatus>& updates) {
  auto statuses = nexthopCache_->addOrUpdateNextHopStatus(updates);
  if (!statuses.empty()) {
    co_await ribInQ_.push(RibInNexthopUpdate(std::move(statuses)));
  }
}

} // namespace facebook::bgp
