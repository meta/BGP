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

#include "neteng/fboss/bgp/cpp/rib/Fib.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

namespace facebook::bgp {

class FibDev : public Fib {
 public:
  ~FibDev() override;

  /** Stage one development FIB request and return its normalized form. */
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
    // Lying that we have connected to agent
    return true;
  }

  static std::unique_ptr<FibDev> createFibDev(Fib::FibMessageQueue& toRibQ);

  void stop() override;

 protected:
  /*
   * NOTE: Make the FibDev constructor a protected method to allow ONLY
   * the public method createFibDev() to create the FIB instance.
   */
  FibDev(Fib::FibMessageQueue& toRibQ);

  std::unique_ptr<FibProgrammedPfxs> waitForAck_;

 private:
  FibMessageQueue& toRibQ_;

  // One time flag to mark initial full-sync to FIB finished
  bool initialFibSynced_{false};

  /*
   * this is to make sure we do not process calls before syncFib
   * when agent reconnects (or first time connect)
   */
  bool fullSynced_{false};
};

} // namespace facebook::bgp
