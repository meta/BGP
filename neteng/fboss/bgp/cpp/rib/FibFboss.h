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

#include "common/network/AddressUtil.h"
#include "neteng/fboss/bgp/cpp/rib/Fib.h"

#include <fboss/agent/if/gen-cpp2/ctrl_types.h>
#include <folly/coro/AsyncScope.h>
#include <folly/coro/Mutex.h>
#include <folly/coro/Task.h>
#include "neteng/fboss/bgp/cpp/rib/FibProgrammingHolddown.h"

namespace facebook::fboss {
class FbossCtrl;
} // namespace facebook::fboss

namespace facebook::bgp {

class FibFboss : public Fib {
 public:
  virtual ~FibFboss();

  /** Stage one FBOSS FIB request and return its normalized platform form. */
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

  /** Stage one FBOSS request with backup-nexthop support. */
  std::optional<FibOutRoute> updateUnicastRouteWithBackup(
      const folly::CIDRNetwork& prefix,
      std::shared_ptr<const BgpPath> attrsToBeAdvertised,
      std::shared_ptr<const WeightedNexthopMap> weightedNexthops,
      const bool isLocalRouteBest,
      const bool installToFib,
      const folly::F14NodeMap<folly::IPAddress, facebook::bgp::NexthopInfo>&,
      const std::optional<uint32_t>& classId,
      std::shared_ptr<const NexthopTopoInfoMap> nexthopTopoInfoMap,
      const BgpRouteType routeType,
      const std::optional<folly::IPAddress>& backupAddr,
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

  static std::unique_ptr<FibFboss> createFibFboss(
      folly::EventBase* evb,
      folly::coro::CancellableAsyncScope& asyncScope,
      Fib::FibMessageQueue& toRibQ);

  void stop() override;

  /*
   * Util method for string transformation
   */
  inline static std::string toString(const fboss::IpPrefix& prefix) {
    if (prefix.ip()->addr()->empty()) {
      // handle empty addr case
      return "";
    }
    return folly::IPAddress::networkToString(
        folly::CIDRNetwork(
            network::toIPAddress(*prefix.ip()), *prefix.prefixLength()));
  }

  static fboss::NetworkTopologyInformation createNetworkTopoInfo(
      const std::unordered_map<std::string, int64_t>& topoInfoMap);

 protected:
  /*
   * NOTE: Make the FibFboss constructor a protected method to allow ONLY
   * the public method createFibFboss() to create the FIB instance.
   */
  FibFboss(
      folly::EventBase* evb,
      folly::coro::CancellableAsyncScope& asyncScope,
      Fib::FibMessageQueue& toRibQ);

  // This ONLY happens in keepAliveRoutine
  virtual void connectAgent();

  /*
   * This happens in two places:
   *  1) syncFib/addUnicastRoutes fail
   *  2) aliveSince/getStatus fail
   */
  void disconnectAgent();

  struct Batch {
    std::vector<fboss::UnicastRoute> toAdd;
    std::vector<fboss::IpPrefix> toDelete;
    FibProgrammedPfxs waitForAck;
  };
  std::unique_ptr<Batch> batch_;
  std::unique_ptr<apache::thrift::Client<fboss::FbossCtrl>> client_;
  folly::EventBase* const evb_;

 private:
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

  /*
   * mutex protects client_
   * both keepAlive() and program() coroutine can reset client_ upon failure,
   * we do not want the coroutines that changes agent interrupt each other.
   */
  mutable folly::coro::Mutex agentMutex_;

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

  friend class FibFbossMock;
#ifdef FibFboss_TEST_FRIENDS
  FibFboss_TEST_FRIENDS
#endif
};

} // namespace facebook::bgp
