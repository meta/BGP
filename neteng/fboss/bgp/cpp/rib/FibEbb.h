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
#include <folly/coro/Mutex.h>
#include <folly/coro/Task.h>
#include <gflags/gflags.h>

#include "neteng/fboss/bgp/cpp/rib/Fib.h"
#include "neteng/fboss/bgp/cpp/rib/FibProgrammingHolddown.h"

// Include the FibService header
#include "openr/common/NetworkUtil.h"
#include "openr/if/gen-cpp2/FibService.h"
#include "openr/if/gen-cpp2/FibServiceAsyncClient.h"
#include "openr/if/gen-cpp2/Network_types.h"

// Defined by FibEbb.cpp and consumed by the BB entry points.
DECLARE_int32(agent_thrift_port);
DECLARE_int32(agent_thrift_recv_timeout_ms);

namespace facebook::bgp {

/*
 * Note: This class is called from only Rib Thread. Any co-routines
 * scheduled from this class should check the value of the shared
 * variables once they get back the control, if they are being updated
 * in another co-routine.
 */
class FibEbb : public Fib {
 public:
  virtual ~FibEbb() override;

  // Delete copy constructor and copy assignment operator
  FibEbb(const FibEbb&) = delete;
  FibEbb& operator=(const FibEbb&) = delete;

  // Delete move constructor and move assignment operator
  FibEbb(FibEbb&&) = delete;
  FibEbb& operator=(FibEbb&&) = delete;

  /** Stage one EBB FIB request and return its normalized platform form. */
  std::optional<FibOutRoute> updateUnicastRoute(
      const folly::CIDRNetwork& prefix,
      std::shared_ptr<const BgpPath> attrsToBeAdvertised,
      std::shared_ptr<const WeightedNexthopMap> weightedNexthops,
      const bool isLocalRouteBest,
      const bool installToFib,
      const folly::F14NodeMap<folly::IPAddress, facebook::bgp::NexthopInfo>&,
      const std::optional<uint32_t>& classId = std::nullopt,
      std::shared_ptr<const NexthopTopoInfoMap> nexthopTopoInfoMap = nullptr,
      const BgpRouteType routeType = BgpRouteType::UNKNOWN,
      bool enableFibOutTracking = false) override;

  folly::coro::Task<void> program(
      bool isSync = false,
      VrfId vrfId = fboss::kDefaultVrf) override;

  bool isFullSynced() const override {
    return fullSynced_;
  }

  bool isConnected() const override {
    return client_ != nullptr;
  }

  static std::unique_ptr<FibEbb> createFibEbb(
      folly::EventBase* evb,
      folly::coro::CancellableAsyncScope& asyncScope,
      Fib::FibMessageQueue& toRibQ,
      uint16_t agentPort,
      uint32_t agentRecvTimeout);

  void stop() override;

 private:
  /*
   * NOTE: Make the FibEbb constructor a private method to allow ONLY
   * the public method createFibEbb() to create the FIB instance.
   */
  FibEbb(
      folly::EventBase* evb,
      folly::coro::CancellableAsyncScope& asyncScope,
      Fib::FibMessageQueue& toRibQ,
      uint16_t agentPort,
      uint32_t agentRecvTimeout);

  // This ONLY happens in keepAliveRoutine
  void connectAgent();

  /*
   * This happens in two places:
   *  1) syncFib/addUnicastRoutes fail
   *  2) aliveSince/getStatus fail
   */
  void disconnectAgent();

  struct Batch {
    std::vector<openr::thrift::UnicastRoute> toAdd;
    std::vector<openr::thrift::IpPrefix> toDelete;
    FibProgrammedPfxs waitForAck;
  };
  std::unique_ptr<Batch> batch_;
  std::unique_ptr<apache::thrift::Client<openr::thrift::FibService>> client_;
  uint16_t agentPort_;
  uint32_t agentRecvTimeout_;
  folly::EventBase* const evb_;

  folly::coro::CancellableAsyncScope& asyncScope_;
  FibMessageQueue& toRibQ_;

  // One time flag to mark initial full-sync to FIB finished
  bool initialFibSynced_{false};

  // coroutine for periodic keep-alive
  folly::coro::Task<void> keepAliveRoutine();
  folly::coro::Task<void> keepAlive();

  // agent start timestamp in seconds returned by client_.aliveSince()
  int64_t agentAliveSince_{0};

  /*
   * Flag to indicate if BGP-Agent is in-sync in real time. This is to make
   * sure we do not process calls before syncFib when agent reconnects(or first
   * time connects).
   *
   * ATTN: this flag is different from `initialFibSynced_` defined in `Fib.h`.
   *
   *  - `fullSynced_` flag will be modified if BGP is in disconnected state
   *    with agent;
   *  - `initialFibSynced` flag will NOT be modified once populated;
   */
  bool fullSynced_{false};

  /**
   * Keeps track of the programming history, including successful
   * and failed events.
   */
  std::unique_ptr<ProgrammingHistory> programmingHistory_;

  /**
   * Manages the hold-down state, which determines when to back off
   * from programming the FIB.
   */
  std::unique_ptr<HoldDownState> holdDownState_;

  friend class FibEbbFixture;
};

} // namespace facebook::bgp
