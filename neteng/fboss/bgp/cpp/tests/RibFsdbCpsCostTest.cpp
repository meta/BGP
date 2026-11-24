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

#define FsdbFibWatcher_TEST_FRIENDS friend class RibFsdbCpsCostTest;

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "fboss/agent/Constants.h"

#include <folly/coro/BlockingWait.h>
#include <folly/io/async/EventBase.h>
#include <gtest/gtest.h>

#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/fsdb/if/facebook/gen-cpp2/fsdb_model_types.h"
#include "fboss/fsdb/oper/instantiations/FsdbCowRoot.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/FsdbFibWatcher.h"
#undef FsdbFibWatcher_TEST_FRIENDS
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibPolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibUtils.h"

namespace facebook::bgp {
namespace {

fboss::fsdb::FsdbCowStateSubManager::Data makeFibState(
    int64_t firstCost,
    int64_t secondCost) {
  fboss::state::FibContainerFields fib;
  const auto addRoute = [&](const folly::IPAddress& address, int64_t cost) {
    fboss::NextHopThrift forwarded;
    forwarded.address()->addr() = std::string(4, '\x01');
    forwarded.cost() = address == kV4Nexthop1 ? 1 : 100;

    fboss::NextHopThrift openr;
    openr.address()->addr() = std::string(4, '\x02');
    openr.cost() = cost;

    fboss::state::RouteFields route;
    route.fwd()->nexthops() = {std::move(forwarded)};
    (*route.nexthopsmulti()->client2NextHopEntry())[fboss::ClientID::OPENR]
        .nexthops() = {std::move(openr)};
    fib.fibV4()[address.str() + "/32"] = std::move(route);
  };
  addRoute(kV4Nexthop1, firstCost);
  addRoute(kV4Nexthop2, secondCost);

  fboss::state::FibInfoFields fibInfo;
  fibInfo.fibsMap()[0] = std::move(fib);
  fboss::state::SwitchState switchState;
  switchState.fibsInfoMap()["id=0"] = std::move(fibInfo);
  fboss::fsdb::AgentData agent;
  agent.switchState() = std::move(switchState);
  fboss::fsdb::FsdbOperStateRoot root;
  root.agent() = std::move(agent);

  auto cowRoot =
      std::make_shared<fboss::thrift_cow::FsdbCowStateRoot>(std::move(root));
  cowRoot->publish();
  return cowRoot;
}
} // namespace

class RibFsdbCpsCostTest : public RibFixture {
 protected:
  void SetUp() override {
    cache_ = std::make_shared<NexthopCache>();
    ribFixtureDefaultSetup(
        ComputeUcmpFromLbwComm{true},
        CountConfedsInAsPathLen{false},
        EnableNexthopTracking{true},
        cache_);
    rib_->setFibBatchTime(std::chrono::milliseconds(2));
    subMgr_ = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
        fboss::fsdb::SubscriptionOptions("test"),
        fboss::utils::ConnectionOptions("::1", 0));
    watcher_ = std::make_shared<FsdbFibWatcher>(
        cache_, ribInQ_, &evb_, subMgr_, fboss::ClientID::OPENR);
    watcher_->switchIds_ = {"id=0"};
    watcher_->registerPeers(
        {NexthopKey{.ip = kV4Nexthop1}, NexthopKey{.ip = kV4Nexthop2}});
    evb_.loopOnce();
  }

  void injectFibCosts(int64_t firstCost, int64_t secondCost) {
    auto fibV4Path = watcher_->fsdbStateRootPath_.agent()
                         .switchState()
                         .fibsInfoMap()["id=0"]
                         .fibsMap()[fboss::kDefaultVrf]
                         .fibV4();
    folly::coro::blockingWait(watcher_->co_processFibUpdate(
        fboss::fsdb::FsdbCowStateSubManager::SubUpdate{
            makeFibState(firstCost, secondCost),
            {},
            {fibV4Path[kV4Nexthop1.str() + "/32"].idTokens(),
             fibV4Path[kV4Nexthop2.str() + "/32"].idTokens()},
            std::nullopt,
            std::nullopt,
            /*streamRevision=*/std::nullopt}));
  }

  std::vector<folly::IPAddress> selectedNexthops() {
    std::vector<folly::IPAddress> nexthops;
    for (const auto& [_, path] : rib_->getMultipath(kV6Prefix1)) {
      nexthops.push_back(path->attrs->getNexthop());
    }
    std::sort(nexthops.begin(), nexthops.end());
    return nexthops;
  }

  void expectSelected(const std::vector<folly::IPAddress>& expected) {
    WITH_RETRIES_N_TIMED(50, std::chrono::milliseconds(100), {
      EXPECT_EVENTUALLY_EQ(expected, selectedNexthops());
    });
  }

  void expectOpenrCosts(uint32_t firstCost, uint32_t secondCost) {
    ASSERT_TRUE(cache_->isRegistered({kV4Nexthop1}));
    ASSERT_TRUE(cache_->isRegistered({kV4Nexthop2}));
    EXPECT_EQ(
        firstCost,
        cache_->registerAndGetNexthopStatus({kV4Nexthop1}).getIgpCost());
    EXPECT_EQ(
        secondCost,
        cache_->registerAndGetNexthopStatus({kV4Nexthop2}).getIgpCost());
  }

  folly::EventBase evb_;
  std::shared_ptr<NexthopCache> cache_;
  std::shared_ptr<fboss::fsdb::FsdbCowStateSubManager> subMgr_;
  std::shared_ptr<FsdbFibWatcher> watcher_;
};

TEST_F(RibFsdbCpsCostTest, OpenrFibCostSelectsAndReselectsCpsPaths) {
  injectFibCosts(30, 10);

  auto first = std::make_shared<BgpPath>(*buildBgpPathFields(2, 1, 0, 2));
  first->setNexthop(kV4Nexthop1);
  first->setOrigin(nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP);
  first->publish();
  auto second = first->clone();
  second->setNexthop(kV4Nexthop2);
  second->publish();

  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  WITH_RETRIES_N_TIMED(100, std::chrono::milliseconds(100), {
    ASSERT_EVENTUALLY_TRUE(fibFuture.isReady());
  });
  std::move(fibFuture).get();
  const PrefixPathIds prefix{{kV6Prefix1, kDefaultPathID}};
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefix, eBgpPeer1_, first);
  sendAnnouncement(prefix, eBgpPeer2_, second);
  WITH_RETRIES_N_TIMED(100, std::chrono::milliseconds(100), {
    ASSERT_EVENTUALLY_TRUE(fibFuture.isReady());
  });
  std::move(fibFuture).get();
  expectOpenrCosts(30, 10);

  rib_policy::TBgpPathMatcher matcher;
  matcher.origin() = bgp_policy::Origin::EGP;
  auto policy = createTPathSelectionPolicyWithPathSelector(
      {kV6Prefix1}, createTPathSlectorWithOneMatcher(matcher));
  sendPathSelectionPolicySet(policy);
  rib_->waitForPathSelectionPolicyUpdate();
  expectSelected({kV4Nexthop1, kV4Nexthop2});

  matcher.prefer_lowest_igp_cost() = true;
  policy = createTPathSelectionPolicyWithPathSelector(
      {kV6Prefix1}, createTPathSlectorWithOneMatcher(matcher));
  policy.version() = 2;
  sendPathSelectionPolicySet(policy);
  rib_->waitForPathSelectionPolicyUpdate();
  expectSelected({kV4Nexthop2});

  injectFibCosts(5, 10);
  expectOpenrCosts(5, 10);
  expectSelected({kV4Nexthop1});

  injectFibCosts(10, 10);
  expectOpenrCosts(10, 10);
  expectSelected({kV4Nexthop1, kV4Nexthop2});
}

} // namespace facebook::bgp
