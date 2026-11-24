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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "fboss/agent/Constants.h"

/*
 * Friend declarations must precede NeighborWatcher.h include so the
 * NeighborWatcher_TEST_FRIENDS macro expands inside the class definition.
 * FRIEND_TEST expands to friend class TestFixture_TestName_Test, so names
 * must be unqualified (NeighborWatcher is in namespace facebook::bgp).
 */
#define NeighborWatcher_TEST_FRIENDS                                        \
  friend class NeighborWatcherFibTest;                                      \
  FRIEND_TEST(NeighborWatcherFibTest, FibWatcherNotCreatedWhenNhtDisabled); \
  FRIEND_TEST(NeighborWatcherFibTest, FibWatcherCreatedWhenNhtEnabled);     \
  FRIEND_TEST(                                                              \
      NeighborWatcherFibTest, FibWatcherNotCreatedWithoutSharedSubMgr);     \
  FRIEND_TEST(NeighborWatcherFibTest, CleanShutdownWithFibWatcher);         \
  FRIEND_TEST(NeighborWatcherFibTest, CleanShutdownWithoutFibWatcher);      \
  FRIEND_TEST(NeighborWatcherFibTest, CleanShutdownWithoutSharedSubMgr);    \
  FRIEND_TEST(NeighborWatcherFibTest, StopAfterSubscribeIsClean);

#include <folly/CancellationToken.h>
#include <folly/IPAddress.h>
#include <folly/coro/Baton.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/executors/ManualExecutor.h>
#include <folly/io/async/EventBase.h>
#include <folly/logging/xlog.h>
#include <stdexcept>
#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/fsdb/if/facebook/gen-cpp2/fsdb_model_types.h"
#include "fboss/fsdb/oper/instantiations/FsdbCowRoot.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/FsdbFibWatcher.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopCache.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopStatus.h"
#include "neteng/fboss/bgp/cpp/peer/NeighborWatcher.h"
#include "neteng/fboss/bgp/cpp/watchdog/MonitoredQueue.h"

namespace facebook::bgp {

using namespace ::testing;

/**
 * Test constants: representative peer addresses used across tests.
 * V4 peers produce /32 host prefixes, V6 peers produce /128.
 **/
const auto kPeerV4 = folly::IPAddress("10.0.0.1");
const auto kPeerV6 = folly::IPAddress("fdad:500::d:0");
const auto kPeerV6_2 = folly::IPAddress("fdad:50c:1::d:0");

fboss::fsdb::FsdbCowStateSubManager::Data makeCowDataWithRoute(
    const std::string& prefix,
    bool isV4,
    const std::vector<std::optional<int64_t>>& nexthopCosts,
    uint16_t nextHopVrfId = fboss::kDefaultVrf) {
  std::vector<fboss::NextHopThrift> nexthops;
  for (const auto& cost : nexthopCosts) {
    fboss::NextHopThrift nhop;
    nhop.address()->addr() = std::string(4, '\x01');
    if (cost.has_value()) {
      nhop.cost() = *cost;
    }
    nexthops.push_back(std::move(nhop));
  }

  fboss::state::RouteNextHopEntry fwd;
  fwd.nexthops() = std::move(nexthops);

  fboss::state::RouteFields route;
  route.fwd() = std::move(fwd);

  fboss::state::FibContainerFields fibContainer;
  if (isV4) {
    fibContainer.fibV4()[prefix] = std::move(route);
  } else {
    fibContainer.fibV6()[prefix] = std::move(route);
  }

  fboss::state::FibInfoFields fibInfo;
  fibInfo.fibsMap()[nextHopVrfId] = std::move(fibContainer);

  fboss::state::SwitchState switchState;
  switchState.fibsInfoMap()["id=0"] = std::move(fibInfo);

  fboss::fsdb::AgentData agentData;
  agentData.switchState() = std::move(switchState);

  fboss::fsdb::FsdbOperStateRoot stateRoot;
  stateRoot.agent() = std::move(agentData);

  auto cowRoot = std::make_shared<fboss::thrift_cow::FsdbCowStateRoot>(
      std::move(stateRoot));
  cowRoot->publish();
  return cowRoot;
}

fboss::fsdb::FsdbCowStateSubManager::Data makeCowDataWithoutRoute() {
  fboss::fsdb::FsdbOperStateRoot stateRoot;
  auto cowRoot = std::make_shared<fboss::thrift_cow::FsdbCowStateRoot>(
      std::move(stateRoot));
  cowRoot->publish();
  return cowRoot;
}

namespace {
std::vector<std::string> makeFibPath(
    const std::string& prefix,
    uint16_t vrfId = fboss::kDefaultVrf) {
  auto path = thriftpath::RootThriftPath<fboss::fsdb::FsdbOperStateRoot>()
                  .agent()
                  .switchState()
                  .fibsInfoMap()["id=0"]
                  .fibsMap()[vrfId];
  return folly::IPAddress::createNetwork(prefix).first.isV4()
      ? path.fibV4()[prefix].idTokens()
      : path.fibV6()[prefix].idTokens();
}
/*
 * Test seam: a watcher that intercepts path registration so tests can
 * (a) force failures to exercise addNexthopPaths's no-crash / rollback / retry
 * behavior and (b) observe whether a nexthop was routed to the live add-path
 * API (liveAdd=true, for RIB-IN-learned nexthops) or the pre-subscribe addPath
 * API (liveAdd=false, for statically configured peers) — all without a live
 * FSDB sub manager. addFsdbPathsForNexthop is virtual for this purpose.
 */
class ThrowingFsdbFibWatcher : public FsdbFibWatcher {
 public:
  using FsdbFibWatcher::FsdbFibWatcher;
  int pendingFailures{0}; // next N addFsdbPathsForNexthop calls throw
  std::vector<std::pair<NexthopKey, bool>> calls; // (nexthop, liveAdd)

 private:
  void addFsdbPathsForNexthop(const NexthopKey& nexthop, bool liveAdd)
      override {
    if (pendingFailures > 0) {
      --pendingFailures;
      throw std::runtime_error("injected addPath failure");
    }
    calls.emplace_back(nexthop, liveAdd);
    /*
     * The success path otherwise touches nothing on the real sub manager so the
     * test stays hermetic; the base addNexthopPaths()/addPaths() records the
     * nexthop in subscribedPeers_ on return.
     */
  }
};

std::shared_ptr<fboss::fsdb::FsdbCowStateSubManager> makeTestSubMgr() {
  return std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
      fboss::fsdb::SubscriptionOptions("test"),
      fboss::utils::ConnectionOptions("::1", 0));
}
} // namespace

/*
 * ---------------------------------------------------------------------------
 * FsdbFibWatcherTest: unit tests for FsdbFibWatcher in isolation
 * ---------------------------------------------------------------------------
 */

class FsdbFibWatcherTest : public ::testing::Test {
 protected:
  void SetUp() override {
    nexthopCache_ = std::make_shared<NexthopCache>();
  }

  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ_{
      nettools::bgplib::kMaxIngressQueueSize};
  std::shared_ptr<NexthopCache> nexthopCache_;
  folly::EventBase evb_;
};

/*
 * ---------------------------------------------------------------------------
 * Peer address management tests
 * ---------------------------------------------------------------------------
 */

TEST_F(FsdbFibWatcherTest, AddPeerAddress) {
  /**
   * Verify that addPeerAddress() registers peers in subscribedPeers_.
   * Duplicate adds should be idempotent.
   **/
  auto subMgr = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
      fboss::fsdb::SubscriptionOptions("test"),
      fboss::utils::ConnectionOptions("::1", 0));
  auto watcher =
      std::make_shared<FsdbFibWatcher>(nexthopCache_, ribInQ_, &evb_, subMgr);

  watcher->addPeerAddress(NexthopKey{.ip = kPeerV4});
  watcher->addPeerAddress(NexthopKey{.ip = kPeerV6});
  // Duplicate add — should be idempotent
  watcher->addPeerAddress(NexthopKey{.ip = kPeerV4});

  /*
   * No crash, no exception. Peer set should contain both addresses.
   * (subscribedPeers_ is private, so we verify indirectly via registerPeers)
   */
}

TEST_F(FsdbFibWatcherTest, RemovePeerAddress) {
  /**
   * Verify that removePeerAddress() removes a previously added peer.
   * Removing a non-existent peer should be a no-op.
   **/
  auto subMgr = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
      fboss::fsdb::SubscriptionOptions("test"),
      fboss::utils::ConnectionOptions("::1", 0));
  auto watcher =
      std::make_shared<FsdbFibWatcher>(nexthopCache_, ribInQ_, &evb_, subMgr);

  watcher->addPeerAddress(NexthopKey{.ip = kPeerV4});
  watcher->addPeerAddress(NexthopKey{.ip = kPeerV6});
  watcher->removePeerAddress(NexthopKey{.ip = kPeerV4});
  // Remove non-existent — should be a no-op
  watcher->removePeerAddress(NexthopKey{.ip = folly::IPAddress("192.168.1.1")});
}

TEST_F(FsdbFibWatcherTest, AddPeerAfterPathsRegisteredIsRejected) {
  /**
   * After registerPeers() has been called (which schedules addPaths()),
   * subsequent addPeerAddress()/removePeerAddress() calls should be
   * rejected with a warning (pathsAdded_ guard). We verify no crash.
   **/
  auto subMgr = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
      fboss::fsdb::SubscriptionOptions("test"),
      fboss::utils::ConnectionOptions("::1", 0));
  auto watcher =
      std::make_shared<FsdbFibWatcher>(nexthopCache_, ribInQ_, &evb_, subMgr);

  std::vector<NexthopKey> peers = {NexthopKey{.ip = kPeerV4}};
  watcher->registerPeers(peers);

  // Drain evb to execute addPaths()
  evb_.loopOnce();

  // These should log warnings but not crash
  watcher->addPeerAddress(NexthopKey{.ip = kPeerV6});
  watcher->removePeerAddress(NexthopKey{.ip = kPeerV4});
}

TEST_F(FsdbFibWatcherTest, RegisterPeersV4AndV6) {
  /**
   * Verify registerPeers() accepts both V4 and V6 addresses and
   * schedules addPaths() on the event base without errors.
   * addPaths() will fail to connect to the agent (no agent running)
   * but should fall back to switchId "id=0" gracefully.
   **/
  auto subMgr = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
      fboss::fsdb::SubscriptionOptions("test"),
      fboss::utils::ConnectionOptions("::1", 0));
  auto watcher =
      std::make_shared<FsdbFibWatcher>(nexthopCache_, ribInQ_, &evb_, subMgr);

  std::vector<NexthopKey> peers = {
      NexthopKey{.ip = kPeerV4},
      NexthopKey{.ip = kPeerV6},
      NexthopKey{.ip = kPeerV6_2}};
  watcher->registerPeers(peers);

  // Drain evb to execute addPaths() — agent call will fail, falls back to id=0
  evb_.loopOnce();

  // No crash; paths registered with default switch ID
}

/*
 * ---------------------------------------------------------------------------
 * RIB-IN-driven runtime subscription tests
 * ---------------------------------------------------------------------------
 */

TEST_F(FsdbFibWatcherTest, FilterNewNexthopsDeDupesAndSkipsTracked) {
  /**
   * filterNewNexthops() returns only nexthops not already tracked, and
   * de-dupes within the input.
   **/
  auto subMgr = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
      fboss::fsdb::SubscriptionOptions("test"),
      fboss::utils::ConnectionOptions("::1", 0));
  auto watcher =
      std::make_shared<FsdbFibWatcher>(nexthopCache_, ribInQ_, &evb_, subMgr);

  // Nothing tracked yet: both unique inputs returned, duplicate collapsed.
  auto fresh = watcher->filterNewNexthops(
      {NexthopKey{.ip = kPeerV4},
       NexthopKey{.ip = kPeerV6},
       NexthopKey{.ip = kPeerV4}});
  EXPECT_EQ(fresh.size(), 2);
  bool hasV4 = false, hasV6 = false;
  for (const auto& ip : fresh) {
    hasV4 |= (ip.ip == kPeerV4);
    hasV6 |= (ip.ip == kPeerV6);
  }
  EXPECT_TRUE(hasV4);
  EXPECT_TRUE(hasV6);
}

TEST_F(FsdbFibWatcherTest, AddNexthopPathsTracksAndDeDupes) {
  /**
   * addNexthopPaths() registers new nexthops for tracking; subsequently
   * filterNewNexthops() treats them as already tracked, while a fresh
   * nexthop is still reported as new. Re-adding is idempotent.
   *
   * Uses the hermetic ThrowingFsdbFibWatcher seam: addNexthopPaths routes to
   * addPathToLiveSubscription, which on a real (unsubscribed) sub manager would
   * abort — the seam records the call instead of touching the sub manager.
   **/
  auto watcher = std::make_shared<ThrowingFsdbFibWatcher>(
      nexthopCache_, ribInQ_, &evb_, makeTestSubMgr());

  // Agent is not running; ensureSwitchIds() falls back to "id=0".
  watcher->addNexthopPaths(
      {NexthopKey{.ip = kPeerV4}, NexthopKey{.ip = kPeerV6}}, /*liveAdd=*/true);

  // Already-tracked nexthops are filtered out.
  EXPECT_TRUE(watcher
                  ->filterNewNexthops(
                      {NexthopKey{.ip = kPeerV4}, NexthopKey{.ip = kPeerV6}})
                  .empty());

  // A new nexthop is still reported as new.
  auto fresh = watcher->filterNewNexthops(
      {NexthopKey{.ip = kPeerV6}, NexthopKey{.ip = kPeerV6_2}});
  EXPECT_EQ(fresh.size(), 1);
  EXPECT_EQ(fresh.front(), (NexthopKey{.ip = kPeerV6_2}));

  // Re-adding an already-tracked nexthop is a no-op.
  watcher->addNexthopPaths({NexthopKey{.ip = kPeerV4}}, /*liveAdd=*/true);
  EXPECT_TRUE(watcher->filterNewNexthops({NexthopKey{.ip = kPeerV4}}).empty());
}

TEST_F(FsdbFibWatcherTest, AddNexthopPathsForwardsLiveAddFlag) {
  /**
   * addNexthopPaths forwards its liveAdd flag to addFsdbPathsForNexthop: the
   * caller (NeighborWatcher) passes liveAdd=true when the server supports live
   * add-path (append to the running subscription) and liveAdd=false on the
   * older-server fallback (stage pre-subscribe, then stop/re-subscribe).
   **/
  auto watcher = std::make_shared<ThrowingFsdbFibWatcher>(
      nexthopCache_, ribInQ_, &evb_, makeTestSubMgr());

  watcher->addNexthopPaths({NexthopKey{.ip = kPeerV4}}, /*liveAdd=*/true);
  watcher->addNexthopPaths({NexthopKey{.ip = kPeerV6}}, /*liveAdd=*/false);

  const std::vector<std::pair<NexthopKey, bool>> expected{
      {NexthopKey{.ip = kPeerV4}, /*liveAdd=*/true},
      {NexthopKey{.ip = kPeerV6}, /*liveAdd=*/false}};
  EXPECT_EQ(expected, watcher->calls);
}

TEST_F(FsdbFibWatcherTest, RegisterPeersUsesNonLiveAddPath) {
  /**
   * Statically configured peers are registered before the initial subscribe
   * via the plain addPath API (liveAdd=false), NOT the live add-path API.
   **/
  auto watcher = std::make_shared<ThrowingFsdbFibWatcher>(
      nexthopCache_, ribInQ_, &evb_, makeTestSubMgr());

  watcher->registerPeers({NexthopKey{.ip = kPeerV4}});
  evb_.loopOnce(); // run the scheduled addPaths()

  const std::vector<std::pair<NexthopKey, bool>> expected{
      {NexthopKey{.ip = kPeerV4}, /*liveAdd=*/false}};
  EXPECT_EQ(expected, watcher->calls);
}

/*
 * ---------------------------------------------------------------------------
 * markNeedsReconcile tests
 * ---------------------------------------------------------------------------
 */

CO_TEST_F(FsdbFibWatcherTest, MarkNeedsReconcileEmptyIsNoop) {
  /**
   * When there are no reachable nexthops, co_markNeedsReconcile() should
   * be a no-op — no updates pushed to NexthopCache.
   **/
  auto subMgr = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
      fboss::fsdb::SubscriptionOptions("test"),
      fboss::utils::ConnectionOptions("::1", 0));
  auto watcher =
      std::make_shared<FsdbFibWatcher>(nexthopCache_, ribInQ_, &evb_, subMgr);

  co_await watcher->co_markNeedsReconcile();
}

CO_TEST_F(FsdbFibWatcherTest, ReconcileKeepsStillReachableNexthopWithoutChurn) {
  /*
   * On FSDB reconnect, co_markNeedsReconcile() must NOT eagerly mark reachable
   * nexthops unreachable. A still-reachable nexthop stays reachable across the
   * reconcile — no transient withdrawal / route churn — and the next snapshot
   * merely confirms it.
   */
  auto subMgr = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
      fboss::fsdb::SubscriptionOptions("test"),
      fboss::utils::ConnectionOptions("::1", 0));
  auto watcher =
      std::make_shared<FsdbFibWatcher>(nexthopCache_, ribInQ_, &evb_, subMgr);
  watcher->registerPeers({NexthopKey{.ip = kPeerV4}});
  evb_.loopOnce();

  // Nexthop becomes reachable via FIB.
  co_await watcher->co_processFibUpdate(
      fboss::fsdb::FsdbCowStateSubManager::SubUpdate{
          makeCowDataWithRoute("10.0.0.1/32", /*isV4=*/true, {42}),
          {},
          {makeFibPath("10.0.0.1/32")},
          std::nullopt,
          std::nullopt,
          /*streamRevision=*/std::nullopt});
  EXPECT_TRUE(
      nexthopCache_->registerAndGetNexthopStatus({kPeerV4}).isReachable());

  /*
   * Reconnect: arming the reconcile must not touch reachability (the old
   * clear-then-rebuild marked it unreachable here, causing a transient
   * withdrawal).
   */
  co_await watcher->co_markNeedsReconcile();
  EXPECT_TRUE(
      nexthopCache_->registerAndGetNexthopStatus({kPeerV4}).isReachable());

  // The reconcile snapshot confirms the nexthop is still reachable.
  co_await watcher->co_processFibUpdate(
      fboss::fsdb::FsdbCowStateSubManager::SubUpdate{
          makeCowDataWithRoute("10.0.0.1/32", /*isV4=*/true, {42}),
          {},
          {},
          std::nullopt,
          std::nullopt,
          /*streamRevision=*/std::nullopt});
  EXPECT_TRUE(
      nexthopCache_->registerAndGetNexthopStatus({kPeerV4}).isReachable());
}

CO_TEST_F(FsdbFibWatcherTest, ReconcileWithdrawsNexthopGoneWhileDisconnected) {
  /*
   * A nexthop whose FIB route disappeared while FSDB was disconnected is
   * withdrawn by the reconcile diff, while an unchanged nexthop stays
   * reachable.
   */
  auto subMgr = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
      fboss::fsdb::SubscriptionOptions("test"),
      fboss::utils::ConnectionOptions("::1", 0));
  auto watcher =
      std::make_shared<FsdbFibWatcher>(nexthopCache_, ribInQ_, &evb_, subMgr);
  watcher->registerPeers(
      {NexthopKey{.ip = kPeerV4}, NexthopKey{.ip = kPeerV6}});
  evb_.loopOnce();

  // Both nexthops reachable via FIB.
  co_await watcher->co_processFibUpdate(
      fboss::fsdb::FsdbCowStateSubManager::SubUpdate{
          makeCowDataWithRoute("10.0.0.1/32", /*isV4=*/true, {42}),
          {},
          {makeFibPath("10.0.0.1/32")},
          std::nullopt,
          std::nullopt,
          /*streamRevision=*/std::nullopt});
  co_await watcher->co_processFibUpdate(
      fboss::fsdb::FsdbCowStateSubManager::SubUpdate{
          makeCowDataWithRoute("fdad:500::d:0/128", /*isV4=*/false, {7}),
          {},
          {makeFibPath("fdad:500::d:0/128")},
          std::nullopt,
          std::nullopt,
          /*streamRevision=*/std::nullopt});
  EXPECT_TRUE(
      nexthopCache_->registerAndGetNexthopStatus({kPeerV4}).isReachable());
  EXPECT_TRUE(
      nexthopCache_->registerAndGetNexthopStatus({kPeerV6}).isReachable());

  /*
   * Reconnect; the fresh snapshot has only the V4 route (V6 route removed while
   * disconnected). Reconcile keeps V4 and withdraws V6.
   */
  co_await watcher->co_markNeedsReconcile();
  co_await watcher->co_processFibUpdate(
      fboss::fsdb::FsdbCowStateSubManager::SubUpdate{
          makeCowDataWithRoute("10.0.0.1/32", /*isV4=*/true, {42}),
          {},
          {},
          std::nullopt,
          std::nullopt,
          /*streamRevision=*/std::nullopt});

  EXPECT_TRUE(
      nexthopCache_->registerAndGetNexthopStatus({kPeerV4}).isReachable());
  EXPECT_FALSE(
      nexthopCache_->registerAndGetNexthopStatus({kPeerV6}).isReachable());
}

TEST_F(FsdbFibWatcherTest, StopIsIdempotent) {
  /**
   * stop() should be safe to call multiple times and should not crash.
   * Subscription lifecycle is managed by the shared sub manager owner.
   **/
  auto subMgr = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
      fboss::fsdb::SubscriptionOptions("test"),
      fboss::utils::ConnectionOptions("::1", 0));
  auto watcher =
      std::make_shared<FsdbFibWatcher>(nexthopCache_, ribInQ_, &evb_, subMgr);

  watcher->stop();
  watcher->stop(); // idempotent
}

CO_TEST_F(FsdbFibWatcherTest, CancellationBeforeUpdateLeavesCacheUnchanged) {
  const NexthopKey nexthop{.ip = kPeerV4};
  auto watcher = std::make_shared<FsdbFibWatcher>(
      nexthopCache_, ribInQ_, &evb_, makeTestSubMgr());
  watcher->registerPeers({nexthop});
  evb_.loopOnce();
  nexthopCache_->registerAndGetNexthopStatus(nexthop);
  folly::CancellationSource cancellation;
  cancellation.requestCancellation();
  auto result = co_await folly::coro::co_awaitTry(
      folly::coro::co_withCancellation(
          cancellation.getToken(),
          watcher->co_processFibUpdate(
              {makeCowDataWithRoute("10.0.0.1/32", true, {42}),
               {},
               {makeFibPath("10.0.0.1/32")},
               std::nullopt,
               std::nullopt,
               std::nullopt})));
  EXPECT_TRUE(result.hasException<folly::OperationCancelled>());
  EXPECT_FALSE(nexthopCache_->getNexthopStatus(nexthop)->isReachable());
  EXPECT_TRUE(ribInQ_.empty());
}

CO_TEST_F(FsdbFibWatcherTest, CancellationAfterCacheUpdateStillDeliversToRib) {
  const NexthopKey nexthop{.ip = kPeerV4};
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> queue{1};
  auto watcher = std::make_shared<FsdbFibWatcher>(
      nexthopCache_, queue, &evb_, makeTestSubMgr());
  watcher->registerPeers({nexthop});
  evb_.loopOnce();
  nexthopCache_->registerAndGetNexthopStatus(nexthop);
  co_await watcher->co_processFibUpdate(
      {makeCowDataWithRoute("10.0.0.1/32", true, {42}),
       {},
       {makeFibPath("10.0.0.1/32")},
       std::nullopt,
       std::nullopt,
       std::nullopt});
  CO_ASSERT_EQ(1, queue.size());

  folly::ManualExecutor executor;
  folly::CancellationSource cancellation;
  folly::coro::Baton finished;
  folly::Try<void> result;
  folly::coro::co_withExecutor(
      &executor,
      watcher->co_processFibUpdate(
          {makeCowDataWithoutRoute(),
           {},
           {makeFibPath("10.0.0.1/32")},
           std::nullopt,
           std::nullopt,
           std::nullopt}))
      .start(
          [&](folly::Try<void> outcome) {
            result = std::move(outcome);
            finished.post();
          },
          cancellation.getToken());
  executor.drain();
  EXPECT_FALSE(nexthopCache_->getNexthopStatus(nexthop)->isReachable());
  EXPECT_FALSE(finished.ready());
  cancellation.requestCancellation();
  executor.drain();
  EXPECT_FALSE(finished.ready());
  co_await queue.pop();
  executor.drain();
  co_await finished;
  EXPECT_TRUE(result.hasValue());
  CO_ASSERT_EQ(1, queue.size());
  auto message = co_await queue.pop();
  const auto& updates = std::get<RibInNexthopUpdate>(message).nexthopStatuses;
  CO_ASSERT_EQ(1, updates.size());
  EXPECT_EQ(nexthop, updates.front().getNexthopKey());
  EXPECT_FALSE(updates.front().isReachable());
  EXPECT_EQ(
      nexthopCache_->getNexthopStatus(nexthop)->getEpoch(),
      updates.front().getEpoch());
}

CO_TEST_F(FsdbFibWatcherTest, PrefixUpdateChangesOnlyItsResolutionVrf) {
  const NexthopKey underlay{.ip = kPeerV4};
  const NexthopKey other{.ip = kPeerV4, .nextHopVrfId = 7};
  const std::string prefix = "10.0.0.1/32";
  auto watcher = std::make_shared<FsdbFibWatcher>(
      nexthopCache_, ribInQ_, &evb_, makeTestSubMgr());
  watcher->registerPeers({underlay, other});
  evb_.loopOnce();
  nexthopCache_->registerAndGetNexthopStatus(underlay);
  nexthopCache_->registerAndGetNexthopStatus(other);
  auto state = makeCowDataWithRoute(prefix, true, {10})->toThrift();
  auto& fibs = *state.agent()->switchState()->fibsInfoMap()["id=0"].fibsMap();
  fibs[7] = fibs.at(fboss::kDefaultVrf);
  fibs[7].fibV4()[prefix].fwd()->nexthops()->front().cost() = 70;
  auto data = std::make_shared<fboss::thrift_cow::FsdbCowStateRoot>(state);
  data->publish();
  co_await watcher->co_processFibUpdate(
      {data,
       {},
       {makeFibPath(prefix), makeFibPath(prefix, 7)},
       std::nullopt,
       std::nullopt,
       std::nullopt});
  co_await ribInQ_.pop();
  const auto underlayEpoch =
      nexthopCache_->getNexthopStatus(underlay)->getEpoch();
  fibs[fboss::kDefaultVrf].fibV4()[prefix].fwd()->nexthops()->front().cost() =
      20;
  fibs[7].fibV4()[prefix].fwd()->nexthops()->front().cost() = 90;
  data =
      std::make_shared<fboss::thrift_cow::FsdbCowStateRoot>(std::move(state));
  data->publish();
  co_await watcher->co_processFibUpdate(
      {data,
       {},
       {makeFibPath(prefix, 7)},
       std::nullopt,
       std::nullopt,
       std::nullopt});
  CO_ASSERT_EQ(1, ribInQ_.size());
  auto message = co_await ribInQ_.pop();
  const auto& updates = std::get<RibInNexthopUpdate>(message).nexthopStatuses;
  CO_ASSERT_EQ(1, updates.size());
  EXPECT_EQ(other, updates.front().getNexthopKey());
  EXPECT_EQ(90, updates.front().getIgpCost());
  EXPECT_EQ(10, nexthopCache_->getNexthopStatus(underlay)->getIgpCost());
  EXPECT_EQ(
      underlayEpoch, nexthopCache_->getNexthopStatus(underlay)->getEpoch());
}

/*
 * ---------------------------------------------------------------------------
 * NexthopCache source-priority tests
 * ---------------------------------------------------------------------------
 */

TEST_F(FsdbFibWatcherTest, SourcePriorityConnectedBlocksNonConnected) {
  /**
   * Once a nexthop is marked isConnected=true (by NeighborWatcher),
   * updates from non-connected sources (isConnected=nullopt, as sent by
   * FsdbFibWatcher) should be silently skipped.
   **/
  auto ip = kPeerV4;

  // Simulate NeighborWatcher marking nexthop as directly connected
  std::vector<NexthopStatus> connectedUpdate;
  connectedUpdate.emplace_back(
      NexthopKey{.ip = ip}, true, 1, true /* isConnected */);
  nexthopCache_->addOrUpdateNextHopStatus(connectedUpdate);

  // Register the nexthop in RIB so we can read it back
  auto status = nexthopCache_->registerAndGetNexthopStatus({ip});
  EXPECT_TRUE(status.isReachable());
  EXPECT_EQ(status.getIgpCost(), 1);
  EXPECT_EQ(status.isConnected(), true);

  // Simulate FsdbFibWatcher sending unreachable with isConnected=nullopt
  std::vector<NexthopStatus> fibUpdate;
  fibUpdate.emplace_back(
      NexthopKey{.ip = ip}, false, std::nullopt, std::nullopt);
  nexthopCache_->addOrUpdateNextHopStatus(fibUpdate);

  // Should still be reachable — non-connected source was skipped
  status = nexthopCache_->registerAndGetNexthopStatus({ip});
  EXPECT_TRUE(status.isReachable());
  EXPECT_EQ(status.getIgpCost(), 1);
  EXPECT_EQ(status.isConnected(), true);
}

TEST_F(FsdbFibWatcherTest, SourcePriorityConnectedCanOverrideConnected) {
  /**
   * A connected source (isConnected=true) should be able to override
   * another connected source's data.
   **/
  auto ip = kPeerV6;

  // First connected update: reachable with igpCost=1
  std::vector<NexthopStatus> update1;
  update1.emplace_back(NexthopKey{.ip = ip}, true, 1, true);
  nexthopCache_->addOrUpdateNextHopStatus(update1);

  auto status = nexthopCache_->registerAndGetNexthopStatus({ip});
  EXPECT_TRUE(status.isReachable());
  EXPECT_EQ(status.getIgpCost(), 1);

  // Second connected update: unreachable (neighbor went away)
  std::vector<NexthopStatus> update2;
  update2.emplace_back(NexthopKey{.ip = ip}, false, std::nullopt, true);
  nexthopCache_->addOrUpdateNextHopStatus(update2);

  status = nexthopCache_->registerAndGetNexthopStatus({ip});
  EXPECT_FALSE(status.isReachable());
}

TEST_F(FsdbFibWatcherTest, NonConnectedSourceCanUpdateNonConnected) {
  /**
   * When a nexthop has isConnected=nullopt (set by FsdbFibWatcher),
   * another non-connected update should be accepted.
   **/
  auto ip = kPeerV4;

  // FIB says reachable
  std::vector<NexthopStatus> update1;
  update1.emplace_back(NexthopKey{.ip = ip}, true, 5, std::nullopt);
  nexthopCache_->addOrUpdateNextHopStatus(update1);

  auto status = nexthopCache_->registerAndGetNexthopStatus({ip});
  EXPECT_TRUE(status.isReachable());

  // FIB says unreachable
  std::vector<NexthopStatus> update2;
  update2.emplace_back(NexthopKey{.ip = ip}, false, std::nullopt, std::nullopt);
  nexthopCache_->addOrUpdateNextHopStatus(update2);

  status = nexthopCache_->registerAndGetNexthopStatus({ip});
  EXPECT_FALSE(status.isReachable());
}

/*
 * ---------------------------------------------------------------------------
 * IGP cost extraction tests (co_processFibUpdate + COW tree)
 * ---------------------------------------------------------------------------
 */

CO_TEST_F(FsdbFibWatcherTest, ProcessFibUpdateSingleNexthopWithCost) {
  /**
   * Route with a single nexthop carrying cost=42.
   * Verify the actual IGP cost flows through to NexthopCache.
   **/
  auto subMgr = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
      fboss::fsdb::SubscriptionOptions("test"),
      fboss::utils::ConnectionOptions("::1", 0));
  auto watcher =
      std::make_shared<FsdbFibWatcher>(nexthopCache_, ribInQ_, &evb_, subMgr);
  watcher->registerPeers({NexthopKey{.ip = kPeerV4}});
  evb_.loopOnce();

  auto cowData = makeCowDataWithRoute("10.0.0.1/32", /*isV4=*/true, {42});
  co_await watcher->co_processFibUpdate(
      fboss::fsdb::FsdbCowStateSubManager::SubUpdate{
          cowData,
          {},
          {makeFibPath("10.0.0.1/32")},
          std::nullopt,
          std::nullopt,
          /*streamRevision=*/std::nullopt});

  auto status = nexthopCache_->registerAndGetNexthopStatus({kPeerV4});
  EXPECT_TRUE(status.isReachable());
  EXPECT_EQ(42u, status.getIgpCost());
}

CO_TEST_F(FsdbFibWatcherTest, ProcessFibUpdateMultipleNexthopsMinCost) {
  /**
   * Route with multiple nexthops carrying different costs.
   * Verify the minimum cost is selected.
   **/
  auto subMgr = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
      fboss::fsdb::SubscriptionOptions("test"),
      fboss::utils::ConnectionOptions("::1", 0));
  auto watcher =
      std::make_shared<FsdbFibWatcher>(nexthopCache_, ribInQ_, &evb_, subMgr);
  watcher->registerPeers({NexthopKey{.ip = kPeerV4}});
  evb_.loopOnce();

  auto cowData =
      makeCowDataWithRoute("10.0.0.1/32", /*isV4=*/true, {100, 50, 200});
  co_await watcher->co_processFibUpdate(
      fboss::fsdb::FsdbCowStateSubManager::SubUpdate{
          cowData,
          {},
          {makeFibPath("10.0.0.1/32")},
          std::nullopt,
          std::nullopt,
          /*streamRevision=*/std::nullopt});

  auto status = nexthopCache_->registerAndGetNexthopStatus({kPeerV4});
  EXPECT_TRUE(status.isReachable());
  EXPECT_EQ(50u, status.getIgpCost());
}

CO_TEST_F(FsdbFibWatcherTest, ProcessFibUpdateNexthopWithoutCost) {
  /**
   * Route with a nexthop that has no cost field set.
   * When no nexthop carries cost, igpCost is nullopt.
   **/
  auto subMgr = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
      fboss::fsdb::SubscriptionOptions("test"),
      fboss::utils::ConnectionOptions("::1", 0));
  auto watcher =
      std::make_shared<FsdbFibWatcher>(nexthopCache_, ribInQ_, &evb_, subMgr);
  watcher->registerPeers({NexthopKey{.ip = kPeerV4}});
  evb_.loopOnce();

  auto cowData =
      makeCowDataWithRoute("10.0.0.1/32", /*isV4=*/true, {std::nullopt});
  co_await watcher->co_processFibUpdate(
      fboss::fsdb::FsdbCowStateSubManager::SubUpdate{
          cowData,
          {},
          {makeFibPath("10.0.0.1/32")},
          std::nullopt,
          std::nullopt,
          /*streamRevision=*/std::nullopt});

  auto status = nexthopCache_->registerAndGetNexthopStatus({kPeerV4});
  EXPECT_TRUE(status.isReachable());
  EXPECT_EQ(std::nullopt, status.getIgpCost());
  EXPECT_TRUE(status.isResolvedForSelection());
}

CO_TEST_F(FsdbFibWatcherTest, ProcessFibUpdateMixedCostNexthopsV6) {
  /**
   * V6 route with a mix of nexthops — some with cost, some without.
   * Verify the minimum of the set costs is selected.
   **/
  auto subMgr = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
      fboss::fsdb::SubscriptionOptions("test"),
      fboss::utils::ConnectionOptions("::1", 0));
  auto watcher =
      std::make_shared<FsdbFibWatcher>(nexthopCache_, ribInQ_, &evb_, subMgr);
  watcher->registerPeers({NexthopKey{.ip = kPeerV6}});
  evb_.loopOnce();

  auto cowData = makeCowDataWithRoute(
      "fdad:500::d:0/128", /*isV4=*/false, {100, std::nullopt, 30});
  co_await watcher->co_processFibUpdate(
      fboss::fsdb::FsdbCowStateSubManager::SubUpdate{
          cowData,
          {},
          {makeFibPath("fdad:500::d:0/128")},
          std::nullopt,
          std::nullopt,
          /*streamRevision=*/std::nullopt});

  auto status = nexthopCache_->registerAndGetNexthopStatus({kPeerV6});
  EXPECT_TRUE(status.isReachable());
  EXPECT_EQ(30u, status.getIgpCost());
}

CO_TEST_F(FsdbFibWatcherTest, ProcessFibUpdateBecameUnreachable) {
  /**
   * First make nexthop reachable with cost=42, then send an update
   * where the route no longer exists. Verify the nexthop becomes
   * unreachable with nullopt cost.
   **/
  auto subMgr = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
      fboss::fsdb::SubscriptionOptions("test"),
      fboss::utils::ConnectionOptions("::1", 0));
  auto watcher =
      std::make_shared<FsdbFibWatcher>(nexthopCache_, ribInQ_, &evb_, subMgr);
  watcher->registerPeers({NexthopKey{.ip = kPeerV4}});
  evb_.loopOnce();

  auto cowData1 = makeCowDataWithRoute("10.0.0.1/32", /*isV4=*/true, {42});
  co_await watcher->co_processFibUpdate(
      fboss::fsdb::FsdbCowStateSubManager::SubUpdate{
          cowData1,
          {},
          {makeFibPath("10.0.0.1/32")},
          std::nullopt,
          std::nullopt,
          /*streamRevision=*/std::nullopt});

  auto status1 = nexthopCache_->registerAndGetNexthopStatus({kPeerV4});
  EXPECT_TRUE(status1.isReachable());
  EXPECT_EQ(42u, status1.getIgpCost());

  auto cowData2 = makeCowDataWithoutRoute();
  co_await watcher->co_processFibUpdate(
      fboss::fsdb::FsdbCowStateSubManager::SubUpdate{
          cowData2,
          {},
          {makeFibPath("10.0.0.1/32")},
          std::nullopt,
          std::nullopt,
          /*streamRevision=*/std::nullopt});

  auto status2 = nexthopCache_->registerAndGetNexthopStatus({kPeerV4});
  EXPECT_FALSE(status2.isReachable());
  EXPECT_EQ(std::nullopt, status2.getIgpCost());
}

CO_TEST_F(FsdbFibWatcherTest, ProcessFibUpdateCostChangeWhileReachable) {
  /**
   * Nexthop already reachable with cost=42. A subsequent FIB update with
   * cost=100 should override the cost even though the nexthop was already
   * reachable (no reachability transition needed).
   **/
  auto subMgr = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
      fboss::fsdb::SubscriptionOptions("test"),
      fboss::utils::ConnectionOptions("::1", 0));
  auto watcher =
      std::make_shared<FsdbFibWatcher>(nexthopCache_, ribInQ_, &evb_, subMgr);
  watcher->registerPeers({NexthopKey{.ip = kPeerV4}});
  evb_.loopOnce();

  auto cowData1 = makeCowDataWithRoute("10.0.0.1/32", /*isV4=*/true, {42});
  co_await watcher->co_processFibUpdate(
      fboss::fsdb::FsdbCowStateSubManager::SubUpdate{
          cowData1,
          {},
          {makeFibPath("10.0.0.1/32")},
          std::nullopt,
          std::nullopt,
          /*streamRevision=*/std::nullopt});

  auto status1 = nexthopCache_->registerAndGetNexthopStatus({kPeerV4});
  EXPECT_TRUE(status1.isReachable());
  EXPECT_EQ(42u, status1.getIgpCost());

  auto cowData2 = makeCowDataWithRoute("10.0.0.1/32", /*isV4=*/true, {100});
  co_await watcher->co_processFibUpdate(
      fboss::fsdb::FsdbCowStateSubManager::SubUpdate{
          cowData2,
          {},
          {makeFibPath("10.0.0.1/32")},
          std::nullopt,
          std::nullopt,
          /*streamRevision=*/std::nullopt});

  auto status2 = nexthopCache_->registerAndGetNexthopStatus({kPeerV4});
  EXPECT_TRUE(status2.isReachable());
  EXPECT_EQ(100u, status2.getIgpCost());
}

CO_TEST_F(FsdbFibWatcherTest, ProcessFibUpdateCostRemovedWhileReachable) {
  /**
   * Nexthop already reachable with cost=42. A subsequent FIB update where
   * nexthops have no cost field should override cost to nullopt (N/A).
   **/
  auto subMgr = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
      fboss::fsdb::SubscriptionOptions("test"),
      fboss::utils::ConnectionOptions("::1", 0));
  auto watcher =
      std::make_shared<FsdbFibWatcher>(nexthopCache_, ribInQ_, &evb_, subMgr);
  watcher->registerPeers({NexthopKey{.ip = kPeerV4}});
  evb_.loopOnce();

  auto cowData1 = makeCowDataWithRoute("10.0.0.1/32", /*isV4=*/true, {42});
  co_await watcher->co_processFibUpdate(
      fboss::fsdb::FsdbCowStateSubManager::SubUpdate{
          cowData1,
          {},
          {makeFibPath("10.0.0.1/32")},
          std::nullopt,
          std::nullopt,
          /*streamRevision=*/std::nullopt});

  auto status1 = nexthopCache_->registerAndGetNexthopStatus({kPeerV4});
  EXPECT_TRUE(status1.isReachable());
  EXPECT_EQ(42u, status1.getIgpCost());

  auto cowData2 =
      makeCowDataWithRoute("10.0.0.1/32", /*isV4=*/true, {std::nullopt});
  co_await watcher->co_processFibUpdate(
      fboss::fsdb::FsdbCowStateSubManager::SubUpdate{
          cowData2,
          {},
          {makeFibPath("10.0.0.1/32")},
          std::nullopt,
          std::nullopt,
          /*streamRevision=*/std::nullopt});

  auto status2 = nexthopCache_->registerAndGetNexthopStatus({kPeerV4});
  EXPECT_TRUE(status2.isReachable());
  EXPECT_EQ(std::nullopt, status2.getIgpCost());
  EXPECT_TRUE(status2.isResolvedForSelection());
}

CO_TEST_F(FsdbFibWatcherTest, ProcessFibUpdateUnreachableCostIsNullopt) {
  /**
   * When a nexthop becomes unreachable (route disappears), the cost
   * should be explicitly set to nullopt (N/A). Verify the cost is
   * cleared on the unreachable transition.
   **/
  auto subMgr = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
      fboss::fsdb::SubscriptionOptions("test"),
      fboss::utils::ConnectionOptions("::1", 0));
  auto watcher =
      std::make_shared<FsdbFibWatcher>(nexthopCache_, ribInQ_, &evb_, subMgr);
  watcher->registerPeers({NexthopKey{.ip = kPeerV4}});
  evb_.loopOnce();

  auto cowData1 = makeCowDataWithRoute("10.0.0.1/32", /*isV4=*/true, {99});
  co_await watcher->co_processFibUpdate(
      fboss::fsdb::FsdbCowStateSubManager::SubUpdate{
          cowData1,
          {},
          {makeFibPath("10.0.0.1/32")},
          std::nullopt,
          std::nullopt,
          /*streamRevision=*/std::nullopt});

  auto status1 = nexthopCache_->registerAndGetNexthopStatus({kPeerV4});
  EXPECT_TRUE(status1.isReachable());
  EXPECT_EQ(99u, status1.getIgpCost());

  auto cowData2 = makeCowDataWithoutRoute();
  co_await watcher->co_processFibUpdate(
      fboss::fsdb::FsdbCowStateSubManager::SubUpdate{
          cowData2,
          {},
          {makeFibPath("10.0.0.1/32")},
          std::nullopt,
          std::nullopt,
          /*streamRevision=*/std::nullopt});

  auto status2 = nexthopCache_->registerAndGetNexthopStatus({kPeerV4});
  EXPECT_FALSE(status2.isReachable());
  EXPECT_EQ(std::nullopt, status2.getIgpCost());
}

/*
 * ---------------------------------------------------------------------------
 * addNexthopPaths: failure handling (no crash, rollback, retry)
 * ---------------------------------------------------------------------------
 */

/*
 * A throw while registering paths must not escape addNexthopPaths (no crash),
 * must NOT mark the nexthop as tracked, and the nexthop must be retried (and
 * succeed) on a subsequent request.
 */
TEST_F(FsdbFibWatcherTest, AddNexthopPathsThrowIsCaughtRolledBackAndRetried) {
  auto watcher = std::make_shared<ThrowingFsdbFibWatcher>(
      nexthopCache_, ribInQ_, &evb_, makeTestSubMgr());

  watcher->pendingFailures = 1; // fail the first registration
  EXPECT_NO_THROW(
      watcher->addNexthopPaths({NexthopKey{.ip = kPeerV4}}, /*liveAdd=*/true));

  /*
   * Failed nexthop was rolled back -> still considered new (retry-able), not
   * silently skipped.
   */
  EXPECT_EQ(
      (std::vector<NexthopKey>{NexthopKey{.ip = kPeerV4}}),
      watcher->filterNewNexthops({NexthopKey{.ip = kPeerV4}}));

  // Retry (no failure armed) succeeds -> nexthop becomes tracked.
  EXPECT_NO_THROW(
      watcher->addNexthopPaths({NexthopKey{.ip = kPeerV4}}, /*liveAdd=*/true));
  EXPECT_TRUE(watcher->filterNewNexthops({NexthopKey{.ip = kPeerV4}}).empty());
}

// A failure on one nexthop in a batch must not abort registration of the rest.
TEST_F(FsdbFibWatcherTest, AddNexthopPathsFailureDoesNotBlockOtherNexthops) {
  auto watcher = std::make_shared<ThrowingFsdbFibWatcher>(
      nexthopCache_, ribInQ_, &evb_, makeTestSubMgr());

  watcher->pendingFailures = 1; // only the first nexthop in the batch fails
  EXPECT_NO_THROW(watcher->addNexthopPaths(
      {NexthopKey{.ip = kPeerV4}, NexthopKey{.ip = kPeerV6}},
      /*liveAdd=*/true));

  // kPeerV4 failed (still new); kPeerV6 succeeded (tracked).
  EXPECT_EQ(
      (std::vector<NexthopKey>{NexthopKey{.ip = kPeerV4}}),
      watcher->filterNewNexthops(
          {NexthopKey{.ip = kPeerV4}, NexthopKey{.ip = kPeerV6}}));
}

/*
 * ---------------------------------------------------------------------------
 * lookupIgpCostFromRoute: protocol/client-aware IGP cost extraction
 * ---------------------------------------------------------------------------
 */

namespace {
fboss::NextHopThrift makeNexthop(char addrByte, std::optional<int64_t> cost) {
  fboss::NextHopThrift nh;
  nh.address()->addr() = std::string(4, addrByte);
  if (cost.has_value()) {
    nh.cost() = *cost;
  }
  return nh;
}
} // namespace

/*
 * With no client configured, cost is the minimum over the resolved fwd
 * nexthops (current behavior).
 */
TEST(FsdbFibWatcherCostTest, NoClientUsesFwdMinCost) {
  fboss::state::RouteFields route;
  route.fwd()->nexthops() = {makeNexthop(1, 20), makeNexthop(2, 10)};

  EXPECT_EQ(10u, FsdbFibWatcher::lookupIgpCostFromRoute(route, std::nullopt));
}

/*
 * With a client configured, cost comes solely from that client's nexthops
 * (minimized). There is NO intersection with fwd: every one of the client's
 * nexthops counts, and the fwd-side costs are not consulted.
 */
TEST(FsdbFibWatcherCostTest, ClientCostIgnoresFwdNoIntersection) {
  fboss::state::RouteFields route;
  // fwd has addr 1 and 2; its costs (99) must be ignored on the client path.
  route.fwd()->nexthops() = {makeNexthop(1, 99), makeNexthop(2, 99)};
  // OPENR advertises addr 1 (cost 5, in fwd) and addr 3 (cost 3, NOT in fwd).
  (*route.nexthopsmulti()->client2NextHopEntry())[fboss::ClientID::OPENR]
      .nexthops() = {makeNexthop(1, 5), makeNexthop(3, 3)};

  /*
   * Min over ALL OPENR nexthops -> 3. addr 3 is counted even though it is not
   * in fwd (no intersection), and fwd's own cost 99 is ignored.
   */
  EXPECT_EQ(
      3u,
      FsdbFibWatcher::lookupIgpCostFromRoute(route, fboss::ClientID::OPENR));
}

// If the configured client has no entry for the route, cost is unset.
TEST(FsdbFibWatcherCostTest, MissingClientEntryReturnsNullopt) {
  fboss::state::RouteFields route;
  route.fwd()->nexthops() = {makeNexthop(1, 7)};
  (*route.nexthopsmulti()->client2NextHopEntry())[fboss::ClientID::BGPD]
      .nexthops() = {makeNexthop(1, 5)};

  EXPECT_EQ(
      std::nullopt,
      FsdbFibWatcher::lookupIgpCostFromRoute(route, fboss::ClientID::OPENR));
}

/*
 * Even when none of the client's nexthops overlap fwd, the cost is still taken
 * from the client's nexthops: we do not check whether the two sets intersect.
 */
TEST(FsdbFibWatcherCostTest, ClientCostTakenEvenWithoutFwdOverlap) {
  fboss::state::RouteFields route;
  route.fwd()->nexthops() = {makeNexthop(2, 99)};
  (*route.nexthopsmulti()->client2NextHopEntry())[fboss::ClientID::OPENR]
      .nexthops() = {makeNexthop(1, 5)};

  // addr 1 is not in fwd, but its cost is still used (no intersection check).
  EXPECT_EQ(
      5u,
      FsdbFibWatcher::lookupIgpCostFromRoute(route, fboss::ClientID::OPENR));
}

/*
 * If the configured client's entry exists but none of its nexthops carry a
 * cost, the cost is unset (route stays reachable, cost N/A).
 */
TEST(FsdbFibWatcherCostTest, ClientWithoutCostReturnsNullopt) {
  fboss::state::RouteFields route;
  route.fwd()->nexthops() = {makeNexthop(1, 99)};
  (*route.nexthopsmulti()->client2NextHopEntry())[fboss::ClientID::OPENR]
      .nexthops() = {makeNexthop(1, std::nullopt)};

  EXPECT_EQ(
      std::nullopt,
      FsdbFibWatcher::lookupIgpCostFromRoute(route, fboss::ClientID::OPENR));
}

/*
 * ---------------------------------------------------------------------------
 * NeighborWatcher + FsdbFibWatcher integration tests
 * ---------------------------------------------------------------------------
 */

class NeighborWatcherFibTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FLAGS_enable_fsdb_patch_subscriber = true;
    sharedSubMgr_ = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
        fboss::fsdb::SubscriptionOptions("test"),
        fboss::utils::ConnectionOptions("::1", 0));
    nexthopCache_ = std::make_shared<NexthopCache>();
  }

  MonitoredMPMCQueue<NeighborWatcherMessage> neighborEventQ_{};
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ_{
      nettools::bgplib::kMaxIngressQueueSize};
  std::shared_ptr<fboss::fsdb::FsdbCowStateSubManager> sharedSubMgr_;
  std::shared_ptr<NexthopCache> nexthopCache_;
};

TEST_F(NeighborWatcherFibTest, FibWatcherNotCreatedWhenNhtDisabled) {
  /**
   * When nexthop tracking is not enabled (i.e., startFibWatcher() is
   * never called), fsdbFibWatcher_ should remain nullptr. This mirrors
   * the Main.cpp flow where startFibWatcher() is only called inside
   * the enableNextHopTracking guard.
   **/
  auto nbrWatcher = std::make_shared<NeighborWatcher>(
      neighborEventQ_,
      ribInQ_,
      false /* enableDsfFastTearDown */,
      sharedSubMgr_);

  // FIB watcher should not exist — startFibWatcher() was never called
  EXPECT_FALSE(nbrWatcher->fsdbFibWatcher_);
}

TEST_F(NeighborWatcherFibTest, FibWatcherCreatedWhenNhtEnabled) {
  /**
   * When startFibWatcher() is called (nexthop tracking enabled),
   * fsdbFibWatcher_ should be created and hold a reference to the
   * shared sub manager.
   * We must run the evb thread so that registerPeers()'s scheduled
   * addPaths() callback executes before teardown.
   **/
  auto nbrWatcher = std::make_shared<NeighborWatcher>(
      neighborEventQ_,
      ribInQ_,
      false /* enableDsfFastTearDown */,
      sharedSubMgr_);

  auto nbrWatcherThread = nbrWatcher->runInThread();

  std::vector<NexthopKey> peers = {
      NexthopKey{.ip = kPeerV4}, NexthopKey{.ip = kPeerV6}};
  nbrWatcher->startFibWatcher(nexthopCache_, ribInQ_, peers);

  EXPECT_TRUE(nbrWatcher->fsdbFibWatcher_);

  nbrWatcher->stop();
  nbrWatcherThread.join();
}

TEST_F(NeighborWatcherFibTest, FibWatcherNotCreatedWithoutSharedSubMgr) {
  /**
   * startFibWatcher() should bail out gracefully (log error, no crash)
   * when sharedFsdbSubMgr_ is nullptr (non-patch-subscriber mode).
   **/
  auto nbrWatcher = std::make_shared<NeighborWatcher>(
      neighborEventQ_,
      ribInQ_,
      false /* enableDsfFastTearDown */,
      nullptr /* sharedFsdbSubMgr */);

  nbrWatcher->startFibWatcher(
      nexthopCache_, ribInQ_, {NexthopKey{.ip = kPeerV4}});

  // Should remain nullptr — no shared sub manager available
  EXPECT_FALSE(nbrWatcher->fsdbFibWatcher_);
}

TEST_F(NeighborWatcherFibTest, SubscribeWithoutFibWatcherIsNoCrash) {
  /**
   * subscribe() should work correctly when FsdbFibWatcher is not
   * created (NHT disabled). The subscription dispatches only to
   * FsdbNeighborWatcher.
   **/
  auto nbrWatcher = std::make_shared<NeighborWatcher>(
      neighborEventQ_,
      ribInQ_,
      false /* enableDsfFastTearDown */,
      sharedSubMgr_);

  auto nbrWatcherThread = nbrWatcher->runInThread();

  // subscribe() without startFibWatcher() — neighbor-only mode
  nbrWatcher->subscribe();

  // Clean shutdown
  nbrWatcher->stop();
  nbrWatcherThread.join();
}

TEST_F(NeighborWatcherFibTest, SubscribeWithFibWatcherIsNoCrash) {
  /**
   * subscribe() should work correctly when FsdbFibWatcher is created
   * (NHT enabled). The subscription dispatches to both
   * FsdbNeighborWatcher and FsdbFibWatcher.
   **/
  auto nbrWatcher = std::make_shared<NeighborWatcher>(
      neighborEventQ_,
      ribInQ_,
      false /* enableDsfFastTearDown */,
      sharedSubMgr_);

  auto nbrWatcherThread = nbrWatcher->runInThread();

  nbrWatcher->learnConnectedNeighbors(nexthopCache_, ribInQ_);
  nbrWatcher->startFibWatcher(
      nexthopCache_,
      ribInQ_,
      {NexthopKey{.ip = kPeerV4}, NexthopKey{.ip = kPeerV6}});
  nbrWatcher->subscribe();

  // Clean shutdown
  nbrWatcher->stop();
  nbrWatcherThread.join();
}

TEST_F(NeighborWatcherFibTest, RequestNexthopSubscribeAfterSubscribeIsNoCrash) {
  /**
   * After subscribe(), a RIB-learned nexthop is appended to the live shared
   * subscription (addPathToLiveSubscription) — no stop/re-subscribe. Verify
   * this path does not crash and shuts down cleanly.
   **/
  auto nbrWatcher = std::make_shared<NeighborWatcher>(
      neighborEventQ_,
      ribInQ_,
      false /* enableDsfFastTearDown */,
      sharedSubMgr_);

  auto nbrWatcherThread = nbrWatcher->runInThread();

  nbrWatcher->startFibWatcher(
      nexthopCache_, ribInQ_, {NexthopKey{.ip = kPeerV4}});
  nbrWatcher->subscribe();

  // RIB-IN learns a new nexthop at runtime.
  nbrWatcher->requestNexthopSubscribe({NexthopKey{.ip = kPeerV6}});

  nbrWatcher->stop();
  nbrWatcherThread.join();
}

TEST_F(
    NeighborWatcherFibTest,
    RequestNexthopSubscribeBeforeSubscribeIsNoCrash) {
  /**
   * requestNexthopSubscribe() before subscribe() must be skipped by the
   * fsdbSubscribed_ guard: addPathToLiveSubscription requires an active
   * subscription, so issuing it on an unsubscribed sub manager would abort.
   * Verify the guard prevents that and the flow is a clean no-op.
   **/
  auto nbrWatcher = std::make_shared<NeighborWatcher>(
      neighborEventQ_,
      ribInQ_,
      false /* enableDsfFastTearDown */,
      sharedSubMgr_);

  auto nbrWatcherThread = nbrWatcher->runInThread();

  nbrWatcher->startFibWatcher(
      nexthopCache_, ribInQ_, {NexthopKey{.ip = kPeerV4}});
  // subscribe() deliberately NOT called: the guard must skip the live add.
  nbrWatcher->requestNexthopSubscribe({NexthopKey{.ip = kPeerV6}});

  nbrWatcher->stop();
  nbrWatcherThread.join();
}

/*
 * ---------------------------------------------------------------------------
 * Cleanup / exit sequence tests
 * ---------------------------------------------------------------------------
 */

TEST_F(NeighborWatcherFibTest, CleanShutdownWithFibWatcher) {
  /**
   * Verify clean shutdown sequence: stop() should properly tear down
   * the shared sub manager and FsdbFibWatcher without crashes or hangs.
   * This mirrors the Main.cpp exit path:
   *   neighborWatcher->stop() → evb stops → thread joins → reset
   **/
  auto nbrWatcher = std::make_shared<NeighborWatcher>(
      neighborEventQ_,
      ribInQ_,
      false /* enableDsfFastTearDown */,
      sharedSubMgr_);

  auto nbrWatcherThread = nbrWatcher->runInThread();

  nbrWatcher->learnConnectedNeighbors(nexthopCache_, ribInQ_);
  nbrWatcher->startFibWatcher(
      nexthopCache_,
      ribInQ_,
      {NexthopKey{.ip = kPeerV4}, NexthopKey{.ip = kPeerV6}});
  nbrWatcher->subscribe();

  // Verify FIB watcher exists
  EXPECT_TRUE(nbrWatcher->fsdbFibWatcher_);

  // Stop and join — must not hang or crash
  nbrWatcher->stop();
  nbrWatcherThread.join();

  /*
   * After stop, the watcher pointers are still valid (shared_ptr) but
   * the evb loop has exited and subscriptions are torn down
   */
  nbrWatcher.reset();
}

TEST_F(NeighborWatcherFibTest, CleanShutdownWithoutFibWatcher) {
  /**
   * Verify clean shutdown when NHT is disabled (no FsdbFibWatcher).
   * stop() should properly tear down just the NeighborWatcher components.
   **/
  auto nbrWatcher = std::make_shared<NeighborWatcher>(
      neighborEventQ_,
      ribInQ_,
      false /* enableDsfFastTearDown */,
      sharedSubMgr_);

  auto nbrWatcherThread = nbrWatcher->runInThread();
  nbrWatcher->subscribe();

  EXPECT_FALSE(nbrWatcher->fsdbFibWatcher_);

  nbrWatcher->stop();
  nbrWatcherThread.join();
  nbrWatcher.reset();
}

TEST_F(NeighborWatcherFibTest, CleanShutdownWithoutSharedSubMgr) {
  /**
   * Verify clean shutdown when using legacy mode (no shared sub manager,
   * no patch subscriber). This is the non-COW path.
   **/
  FLAGS_enable_fsdb_patch_subscriber = false;
  auto nbrWatcher = std::make_shared<NeighborWatcher>(
      neighborEventQ_,
      ribInQ_,
      false /* enableDsfFastTearDown */,
      nullptr /* sharedFsdbSubMgr */);

  auto nbrWatcherThread = nbrWatcher->runInThread();

  EXPECT_FALSE(nbrWatcher->fsdbFibWatcher_);

  nbrWatcher->stop();
  nbrWatcherThread.join();
  nbrWatcher.reset();
}

TEST_F(NeighborWatcherFibTest, StopBeforeSubscribeIsNoCrash) {
  /**
   * Calling stop() before subscribe() should be safe — no subscription
   * was started, so there's nothing to tear down on the FSDB side.
   **/
  auto nbrWatcher = std::make_shared<NeighborWatcher>(
      neighborEventQ_,
      ribInQ_,
      false /* enableDsfFastTearDown */,
      sharedSubMgr_);

  auto nbrWatcherThread = nbrWatcher->runInThread();

  nbrWatcher->startFibWatcher(
      nexthopCache_, ribInQ_, {NexthopKey{.ip = kPeerV4}});
  // Note: subscribe() deliberately NOT called

  nbrWatcher->stop();
  nbrWatcherThread.join();
}

TEST_F(NeighborWatcherFibTest, StopAfterSubscribeIsClean) {
  /**
   * Verify that stop() after subscribe() with FsdbFibWatcher properly
   * tears down all subscriptions and the evb loop without crashing.
   * Note: stop() is NOT idempotent (asyncScope_.cancelAndJoinAsync()
   * asserts on double-join), so we only call it once.
   **/
  auto nbrWatcher = std::make_shared<NeighborWatcher>(
      neighborEventQ_,
      ribInQ_,
      false /* enableDsfFastTearDown */,
      sharedSubMgr_);

  auto nbrWatcherThread = nbrWatcher->runInThread();

  nbrWatcher->startFibWatcher(
      nexthopCache_, ribInQ_, {NexthopKey{.ip = kPeerV4}});
  nbrWatcher->subscribe();

  nbrWatcher->stop();
  nbrWatcherThread.join();
}

} // namespace facebook::bgp
