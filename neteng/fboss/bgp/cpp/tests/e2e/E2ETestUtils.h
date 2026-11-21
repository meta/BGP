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

/*
 * E2E test utilities: TestFib, TestRib, and helper functions
 * Minimal mocking (FIB only), real RIB/PeerManagerBase/AdjRib components
 */

#pragma once

#include <folly/coro/BlockingWait.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <chrono>

#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/config/Config.h"
#include "neteng/fboss/bgp/cpp/rib/Fib.h"
#include "neteng/fboss/bgp/cpp/rib/RibBase.h"
#include "neteng/fboss/bgp/cpp/rib/RibPolicy.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"
#include "neteng/fboss/bgp/cpp/watchdog/MonitoredQueue.h"

/*
 * NOTE: This header is platform-NEUTRAL and MUST NOT include RibBB.h or
 * RibDC.h. Those headers each pull in a per-platform PlatformConstant.h that
 * defines kBgpPlatformType (and the FIB-agent port/timeout) as conflicting
 * `inline constexpr` values, so they cannot coexist in one translation unit
 * or one linked binary (ODR). The concrete RIB base is selected at link time
 * via the makeTestRib() factory — see E2ETestRibFactory.h.
 */

namespace facebook {
namespace bgp {

// Mock FIB that stages route updates and reports completion to the RIB.
class TestFib : public Fib {
 public:
  using FibMessageQueue = Fib::FibMessageQueue;
  using FibProgrammedPfxs = Fib::FibProgrammedPfxs;

  explicit TestFib(FibMessageQueue& toRibQ);

  /** Stage one test FIB request and return its normalized form. */
  std::optional<FibOutRoute> updateUnicastRoute(
      const folly::CIDRNetwork& prefix,
      std::shared_ptr<const BgpPath> attrsToBeAdvertised,
      std::shared_ptr<const WeightedNexthopMap> weightedNexthops,
      const bool isLocalRouteBest,
      const bool installToFib,
      const folly::F14NodeMap<folly::IPAddress, NexthopInfo>& nextHopInfoMap,
      const std::optional<uint32_t>& classId = std::nullopt,
      std::shared_ptr<const NexthopTopoInfoMap> nhtTopo = nullptr,
      const BgpRouteType routeType = BgpRouteType::UNKNOWN,
      bool enableFibOutTracking = false) override;

  bool isConnected() const override;

  bool isFullSynced() const override;

  /* Control FIB connection state for testing disconnect/reconnect scenarios */
  void setConnected(bool connected) {
    connected_ = connected;
  }

  /* Add artificial delay before program() sends FibProgrammedMessage */
  void setProgramDelay(std::chrono::milliseconds delay) {
    programDelay_ = delay;
  }

  /*
   * Make the next program() call fail without sending a completion to the RIB.
   * One-shot: resets to false after one failure.
   */
  void setFailNextProgram(bool fail) {
    failNextProgram_ = fail;
  }

  bool getFailNextProgram() const {
    return failNextProgram_;
  }

  size_t getFailedProgramCount() const {
    return failedProgramCount_;
  }

  /*
   * Simulate FIB disconnect followed by reconnect.
   * On reconnect, pushes FibSyncReq to RIB to trigger full route resync.
   */
  void simulateDisconnect() {
    connected_ = false;
    XLOG(INFO, "[TestFib] Simulated disconnect");
  }

  void simulateReconnect() {
    connected_ = true;
    fullSynced_ = false;
    toRibQ_.push(Fib::FibSyncReq{});
    XLOG(INFO, "[TestFib] Simulated reconnect, sent FibSyncReq");
  }

  folly::coro::Task<void> program(
      bool isSync = false,
      VrfId vrfId = fboss::kDefaultVrf) override;

  void stop() override;

  // Test helper methods
  size_t getProgramCallCount() const {
    return programCallCount_;
  }

  const FibProgrammedPfxs& getStagedRoutes() const {
    return staged_;
  }

  /*
   * Get all programmed routes (persists across program() calls).
   * Unlike staged_ which is cleared after each program() call,
   * this map retains all routes that have been programmed to FIB.
   */
  const folly::
      F14NodeMap<folly::CIDRNetwork, std::shared_ptr<const WeightedNexthopMap>>&
      getProgrammedRoutes() const {
    return programmedRoutes_;
  }

 private:
  FibProgrammedPfxs staged_;
  /*
   * Persistent map of all programmed routes (prefix -> weighted nexthops).
   * Updated on each updateUnicastRoute() call, not cleared on program().
   */
  folly::
      F14NodeMap<folly::CIDRNetwork, std::shared_ptr<const WeightedNexthopMap>>
          programmedRoutes_;
  bool connected_{true};
  bool fullSynced_{false};
  bool failNextProgram_{false};
  size_t failedProgramCount_{0};
  std::chrono::milliseconds programDelay_{0};
  FibMessageQueue& toRibQ_;
  size_t programCallCount_{0};
};

/*
 * Base-agnostic accessor for the injected TestFib. Lets E2ETestFixture reach
 * the TestFib through a RibBase* without knowing which RIB base (RibBB or
 * RibDC) the test is running against.
 */
class TestRibIf {
 public:
  virtual ~TestRibIf() = default;
  virtual TestFib* getTestFib() = 0;
};

/*
 * RIB subclass that injects TestFib, parameterized over the RIB base so the
 * E2E suite can run against BOTH platforms:
 *   - TestRibBB (RibBB) — the EBB/border platform. CRF-only policy handling;
 *     RibBB drops PathSelectionPolicySetMsg and other DC-only policy messages
 *     as error-logging lambdas in processRibPolicyMsgLoop.
 *   - TestRibDC (RibDC) — the data-center platform. Exercises partial-drain,
 *     MNH-drain, CPS and CTE code paths that only exist on RibDC.
 *
 * See E2ETestFixture.h ("Where does my test go?") for how to pick a base.
 */
template <class Base>
class TestRibT : public Base, public TestRibIf {
 public:
  using Base::Base;

  void createFib() override {
    if (!this->fib_) {
      auto testFib = std::make_unique<TestFib>(this->fromFibMessageQ_);
      testFib_ = testFib.get();
      this->fib_ = std::move(testFib);
    }
  }

  TestFib* getTestFib() override {
    return testFib_;
  }

 private:
  TestFib* testFib_{nullptr};
};

// Build a BgpPath for testing with standard attributes
std::shared_ptr<BgpPath> buildBgpPath(const folly::IPAddress& nexthop);

} // namespace bgp
} // namespace facebook
