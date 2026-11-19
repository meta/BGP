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
#include <folly/coro/Baton.h>
#include <gmock/gmock.h>

#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"
#include "neteng/fboss/bgp/cpp/config/ConfigManager.h"

namespace facebook::bgp {
/**
 * This MockAdjRib can be used to spy on method calls, or to just
 * mock adjRibs when you don't really care about what's
 * inside.
 */
class MockAdjRib : public AdjRib {
 public:
  MockAdjRib(
      const nettools::bgplib::BgpPeerId& peerId,
      const PeeringParams& peeringParams,
      folly::EventBase& evb,
      nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage>& ribInQ,
      MonitoredMPMCQueue<ObservableMessageT>& fromAdjRibQ,
      const std::shared_ptr<folly::coro::Baton>& sessionTerminateBaton,
      const std::shared_ptr<PolicyManager>& policy = nullptr,
      folly::not_null_shared_ptr<std::atomic<bool>> isSafeModeOn =
          std::make_shared<std::atomic<bool>>(false),
      const std::optional<std::string>& ingressPolicyName = std::nullopt,
      const std::optional<std::string>& egressPolicyName = std::nullopt,
      const std::shared_ptr<AdjRibOutGroup> adjRibOutGroup = nullptr,
      const std::optional<std::chrono::seconds>& outDelay = std::nullopt,
      const std::shared_ptr<const Config> config = nullptr)
      : AdjRib(
            peerId,
            peeringParams,
            evb,
            ribInQ,
            fromAdjRibQ,
            sessionTerminateBaton,
            policy,
            isSafeModeOn,
            ingressPolicyName,
            egressPolicyName,
            adjRibOutGroup,
            outDelay,
            config ? std::make_shared<ConfigManager>(config) : nullptr) {
    ON_CALL(*this, isPeerGracefulRestarting()).WillByDefault([this]() {
      return AdjRib::isPeerGracefulRestarting();
    });
    /*
     * By default delegate to the real implementation so existing users of
     * MockAdjRib that don't care about rib-dump delivery are unaffected.
     */
    ON_CALL(*this, processRibMessage(testing::_))
        .WillByDefault([this](const RibOutPayload& update) {
          AdjRib::processRibMessage(update);
        });
  }

  MOCK_METHOD(folly::coro::Task<void>, stop, (), (noexcept, override));
  MOCK_METHOD(bool, isPeerGracefulRestarting, (), (const, noexcept, override));
  MOCK_METHOD(
      folly::coro::Task<void>,
      cleanupGrState,
      (bool isDaemonShutdown),
      (noexcept, override));
  MOCK_METHOD(
      void,
      processRibMessage,
      (const RibOutPayload& update),
      (noexcept, override));
};

} // namespace facebook::bgp
