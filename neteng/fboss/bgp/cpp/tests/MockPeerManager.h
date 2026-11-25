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
#include <gmock/gmock.h>

#include "neteng/fboss/bgp/cpp/peer/facebook/PeerManagerDC.h"
#include "neteng/fboss/bgp/cpp/watchdog/MonitoredQueue.h"

namespace facebook::bgp {

class MockPeerManager : public PeerManagerDC {
 public:
  // needed for unit tests
  MockPeerManager(
      std::shared_ptr<ConfigManager> configManager,
      nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ,
      MonitoredMPMCQueue<RibOutMessage>& ribOutQ,
      std::optional<MonitoredMPMCQueue<NeighborWatcherMessage>>&
          nbrRouteChangeQ)
      : PeerManagerDC(
            configManager,
            nullptr,
            ribInQ,
            ribOutQ,
            nbrRouteChangeQ) {}

  MOCK_METHOD(
      void,
      getNetworks,
      ((std::map<
           facebook::neteng::fboss::bgp_attr::TIpPrefix,
           facebook::neteng::fboss::bgp::thrift::TBgpPath>&),
       const std::unique_ptr<std::string>&,
       const RouteFilterType&),
      (noexcept, override));
  MOCK_METHOD(
      void,
      getNetworks,
      ((std::map<
           facebook::neteng::fboss::bgp_attr::TIpPrefix,
           facebook::neteng::fboss::bgp::thrift::TBgpPath>&),
       const std::unique_ptr<std::string>&,
       const std::unique_ptr<std::string>&,
       const RouteFilterType&),
      (noexcept, override));

  MOCK_METHOD(
      void,
      getNetworks2,
      ((std::map<
           facebook::neteng::fboss::bgp_attr::TIpPrefix,
           std::vector<facebook::neteng::fboss::bgp::thrift::TBgpPath>>&),
       const std::unique_ptr<std::string>&,
       const RouteFilterType&),
      (noexcept, override));

  MOCK_METHOD(
      void,
      getNetworks2,
      ((std::map<
           facebook::neteng::fboss::bgp_attr::TIpPrefix,
           std::vector<facebook::neteng::fboss::bgp::thrift::TBgpPath>>&),
       const std::unique_ptr<std::string>&,
       const std::unique_ptr<std::string>&,
       const RouteFilterType&),
      (noexcept, override));

  void setRouteFilterPolicy(
      std::unique_ptr<RouteFilterPolicy> policy,
      bool forceUpdate = false) noexcept;

  void run() noexcept override {
    // start bgp manager coroutine
    if (runVipPeerManager_) {
      // start vip peer manager coroutine
      asyncScope_.add(co_withExecutor(&evb_, startVipPeerManagerRoutine()));
    }
    evb_.loop();
  }

  void enableVipPeerManager() {
    runVipPeerManager_ = true;
  }

 private:
  /*
   * if true, run VipPeerManager in the run() call
   * enableVipPeerManager() and run() are most likely going to be called
   * sequentially but just in case we make the variable atomic
   */
  std::atomic<bool> runVipPeerManager_{false};
};

} // namespace facebook::bgp
