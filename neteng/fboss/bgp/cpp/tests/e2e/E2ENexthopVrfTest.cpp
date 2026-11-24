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

#include <gtest/gtest.h>
#include "fboss/agent/Constants.h"

#include <memory>
#include <optional>
#include <utility>

#include <folly/container/F14Set.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/synchronization/Baton.h>

#include "fboss/fsdb/client/FsdbPatchPublisher.h"
#include "fboss/fsdb/client/instantiations/FsdbCowStateSubManager.h"
#include "fboss/fsdb/server/FsdbConfig.h"
#include "fboss/fsdb/tests/utils/FsdbTestServer.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopCache.h"
#include "neteng/fboss/bgp/cpp/peer/NeighborWatcher.h"
#include "neteng/fboss/bgp/cpp/tests/BoundedWaitUtils.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/E2ETestFixture.h"

namespace facebook::bgp {

/*
 * Real BGP, RIB, NeighborWatcher, and FSDB stream; TestFib records forwarding.
 * Nondefault resolution-VRF cases stop at the handler queue. Parameters
 * cover address families and serialized update groups.
 */
class E2ENexthopVrfTest
    : public E2ETestFixture,
      public testing::WithParamInterface<std::pair<bool, bool>> {
 protected:
  void SetUp() override {
    FLAGS_enable_fsdb_patch_subscriber = true;
    addPeer(kDefaultPeerSpec3);
    addPeer(kDefaultPeerSpec4);
    addPeer(kDefaultPeerSpec5);
    createRib(/*enableNexthopTracking=*/true);
    createPeerManager(
        /*enableUpdateGroup=*/GetParam().second,
        /*enableSerializeGroupPdu=*/GetParam().second);
    cache_ = getNexthopCache();
    /* Initial computation waits for ingress EoR from every configured peer. */
    for (const auto& peer : {kPeerAddr3, kPeerAddr4, kPeerAddr5}) {
      bringUpPeer(peer);
    }
    for (const auto& peer : {kPeerAddr3, kPeerAddr4, kPeerAddr5}) {
      sendEoRToPeer({peer, peer.asV4().toLongHBO()});
    }
    for (const auto& peer : {kPeerAddr3, kPeerAddr4, kPeerAddr5}) {
      ASSERT_TRUE(waitForEoR({peer, peer.asV4().toLongHBO()}));
    }

    server_ = std::make_unique<fboss::fsdb::test::FsdbTestServer>(
        std::make_shared<fboss::fsdb::FsdbConfig>(),
        /*port=*/0,
        /*stateSubscriptionServe_ms=*/50,
        /*statsSubscriptionServe_ms=*/1000,
        /*subscriptionServeQueueSize=*/1000,
        /*statsSubscriptionServeQueueSize=*/8,
        /*numIOWorkerThreads=*/1,
        /*numCPUWorkerThreads=*/1);
    publisher_ = std::make_unique<fboss::fsdb::FsdbPatchPublisher>(
        "agent",
        std::vector<std::string>{"agent"},
        publisherThread_.getEventBase(),
        reconnectThread_.getEventBase(),
        /*isStats=*/false,
        [this](auto, auto state) {
          if (state == fboss::fsdb::FsdbPatchPublisher::State::CONNECTED) {
            publisherConnected_.post();
          }
        });
    publisher_->setConnectionOptions(
        fboss::utils::ConnectionOptions("::1", server_->getFsdbPort()));
    test::boundedBatonWait(
        publisherConnected_,
        "agent publisher connected",
        test::kDefaultPopTimeout);
  }

  void TearDown() override {
    if (rib_) {
      rib_->getEventBase().runInEventBaseThreadAndWait(
          [this] { rib_->setNexthopSubscribeRequester({}); });
    }
    if (watcher_) {
      watcher_->stop();
      watcherThread_.join();
      watcher_.reset();
    }
    subMgr_.reset();
    publisher_.reset();
    server_.reset();
    E2ETestFixture::TearDown();
  }

  /*
   * The marker's delivered update establishes initial sync before runtime
   * requests. Routing cases use its BGP advertisement as the receipt barrier;
   * handler cases consume its queue update. No timing-based waits are needed.
   */
  void startWatcher(bool handlerOnly = false) {
    auto& queue = handlerOnly ? handlerQ_ : ribInQ_;
    subMgr_ = std::make_shared<fboss::fsdb::FsdbCowStateSubManager>(
        fboss::fsdb::SubscriptionOptions("nexthop-vrf-e2e"),
        fboss::utils::ConnectionOptions("::1", server_->getFsdbPort()));
    watcher_ = std::make_unique<NeighborWatcher>(
        neighborQ_,
        queue,
        /*enableSwitchReachabilityWatcher=*/false,
        subMgr_,
        server_->getFsdbPort());
    cache_->registerAndGetNexthopStatus(NexthopKey{.ip = marker_});
    watcher_->startFibWatcher(cache_, queue, {NexthopKey{.ip = marker_}});
    watcherThread_ = watcher_->runInThread();
    watcher_->getEventBase().waitUntilRunning();
    watcher_->subscribe();
    setHostRoute(marker_, fboss::kDefaultVrf, 100);
    publish();
    if (handlerOnly) {
      const auto message = test::boundedBlockingPop(handlerQ_, "initial sync");
      const auto* update = std::get_if<RibInNexthopUpdate>(&message);
      ASSERT_NE(nullptr, update);
      ASSERT_EQ(1, update->nexthopStatuses.size());
      EXPECT_EQ(
          (NexthopKey{.ip = marker_}),
          update->nexthopStatuses.front().getNexthopKey());
    } else {
      addRoute("v4", "198.19.0.0", 24, kPeerAddr3, marker_.str(), "65001");
      ASSERT_TRUE(waitForRouteInShadowRib(
          folly::IPAddress::createNetwork("198.19.0.0/24")));
      ASSERT_TRUE(verifyRouteAdd(
          "v4", "198.19.0.0", 24, kPeerAddr5, kNextHopV4_5.str()));
      rib_->getEventBase().runInEventBaseThreadAndWait([this] {
        rib_->setNexthopSubscribeRequester(
            [this](const std::vector<folly::IPAddress>& ips) {
              std::vector<NexthopKey> keys;
              keys.reserve(ips.size());
              for (const auto& ip : ips) {
                keys.push_back(NexthopKey{.ip = ip});
              }
              watcher_->requestNexthopSubscribe(std::move(keys));
            });
      });
    }
    ASSERT_TRUE(subMgr_->supportsLiveAddPath());
  }

  folly::IPAddress nexthop() const {
    return folly::IPAddress(GetParam().first ? "2001:db8::1" : "11.0.0.1");
  }

  folly::IPAddress otherNexthop() const {
    return folly::IPAddress(GetParam().first ? "2001:db8::2" : "11.0.0.2");
  }

  folly::CIDRNetwork prefix(bool second = false) const {
    return folly::IPAddress::createNetwork(
        GetParam().first ? (second ? "2001:db8:20::/48" : "2001:db8:10::/48")
                         : (second ? "20.0.0.0/8" : "10.0.0.0/8"));
  }

  std::string protocol() const {
    return GetParam().first ? "v6" : "v4";
  }

  void announce(
      const folly::CIDRNetwork& network,
      const folly::IPAddress& peer,
      const folly::IPAddress& nh) {
    addRoute(
        protocol(),
        network.first.str(),
        network.second,
        peer,
        nh.str(),
        "65001");
    ASSERT_TRUE(
        waitForPathCountInRib(folly::IPAddress::networkToString(network), 1));
  }

  /* Publish the wedge-agent's host tracking result; BGP performs no LPM. */
  void setHostRoute(
      const folly::IPAddress& ip,
      uint16_t nextHopVrfId,
      std::optional<int64_t> cost) {
    fboss::state::RouteFields route;
    fboss::NextHopThrift nh;
    if (cost) {
      nh.cost() = *cost;
    }
    route.fwd()->nexthops()->push_back(std::move(nh));
    auto& fib =
        (*state_.fibsInfoMap())["id=0"].fibsMap()->operator[](nextHopVrfId);
    auto& routes = ip.isV4() ? *fib.fibV4() : *fib.fibV6();
    routes[folly::IPAddress::networkToString(
        {ip, ip.isV4() ? kV4NexthopPrefixLen : kV6NexthopPrefixLen})] =
        std::move(route);
  }

  void removeHostRoute(
      const folly::IPAddress& ip,
      uint16_t nextHopVrfId = fboss::kDefaultVrf) {
    auto& fib =
        (*state_.fibsInfoMap())["id=0"].fibsMap()->operator[](nextHopVrfId);
    auto& routes = ip.isV4() ? *fib.fibV4() : *fib.fibV6();
    routes.erase(
        folly::IPAddress::networkToString(
            {ip, ip.isV4() ? kV4NexthopPrefixLen : kV6NexthopPrefixLen}));
  }

  /* One complete snapshot also exercises withdrawals from empty FIB maps. */
  void publish() {
    const auto path = rootPath_.agent().switchState();
    fboss::fsdb::Patch patch;
    patch.basePath() = path.idTokens();
    patch.protocol() = fboss::fsdb::OperProtocol::BINARY;
    fboss::thrift_cow::PatchNode node;
    node.set_val(
        fboss::thrift_cow::serializeBuf<typename decltype(path)::TC>(
            fboss::fsdb::OperProtocol::BINARY, state_));
    patch.patch() = std::move(node);
    ASSERT_TRUE(publisher_->write(std::move(patch)));
  }

  void expectAdvertised(
      const folly::CIDRNetwork& network,
      const folly::IPAddress& nh) {
    ASSERT_TRUE(verifyRouteAdd(
        protocol(),
        network.first.str(),
        network.second,
        kPeerAddr5,
        GetParam().first ? kNextHopV6_5.str() : kNextHopV4_5.str()));
    ASSERT_TRUE(waitForRouteInShadowRib(network));
    const auto fib =
        getFibWeightedNexthops(folly::IPAddress::networkToString(network));
    ASSERT_NE(nullptr, fib);
    EXPECT_TRUE(fib->contains(nh));
  }

  void expectWithdrawn(const folly::CIDRNetwork& network) {
    ASSERT_TRUE(verifyRouteWithdraw(
        protocol(), network.first.str(), network.second, kPeerAddr5));
    EXPECT_EQ(nullptr, getBestPath(network));
    const auto fib =
        getFibWeightedNexthops(folly::IPAddress::networkToString(network));
    EXPECT_TRUE(!fib || fib->empty());
  }

  /* A snapshot can deliver several keys together; duplicates must fail. */
  void expectUpdates(
      folly::F14FastSet<NexthopKey, NexthopKey::Hash> keys,
      bool reachable,
      std::optional<uint32_t> cost) {
    while (!keys.empty()) {
      const auto message =
          test::boundedBlockingPop(handlerQ_, "VRF nexthop update");
      const auto* update = std::get_if<RibInNexthopUpdate>(&message);
      ASSERT_NE(nullptr, update);
      ASSERT_FALSE(update->nexthopStatuses.empty());
      for (const auto& status : update->nexthopStatuses) {
        ASSERT_EQ(1, keys.erase(status.getNexthopKey()));
        EXPECT_EQ(reachable, status.isReachable());
        EXPECT_EQ(cost, status.getIgpCost());
      }
    }
  }

  /* A later publication fences earlier deliveries before checking silence. */
  void expectNoMoreUpdates() {
    setHostRoute(marker_, fboss::kDefaultVrf, 101);
    publish();
    const auto message =
        test::boundedBlockingPop(handlerQ_, "publication fence");
    const auto* update = std::get_if<RibInNexthopUpdate>(&message);
    ASSERT_NE(nullptr, update);
    ASSERT_EQ(1, update->nexthopStatuses.size());
    EXPECT_EQ(
        (NexthopKey{.ip = marker_}),
        update->nexthopStatuses.front().getNexthopKey());
    EXPECT_EQ(101, update->nexthopStatuses.front().getIgpCost());
    watcher_->getEventBase().runInEventBaseThreadAndWait([] {});
    EXPECT_TRUE(handlerQ_.empty());
  }

  std::shared_ptr<NexthopCache> cache_;
  std::unique_ptr<fboss::fsdb::test::FsdbTestServer> server_;
  folly::ScopedEventBaseThread publisherThread_;
  folly::ScopedEventBaseThread reconnectThread_;
  folly::Baton<> publisherConnected_;
  std::unique_ptr<fboss::fsdb::FsdbPatchPublisher> publisher_;
  std::shared_ptr<fboss::fsdb::FsdbCowStateSubManager> subMgr_;
  std::unique_ptr<NeighborWatcher> watcher_;
  std::thread watcherThread_;
  MonitoredMPMCQueue<NeighborWatcherMessage> neighborQ_;
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> handlerQ_{
      nettools::bgplib::kMaxIngressQueueSize};
  thriftpath::RootThriftPath<fboss::fsdb::FsdbOperStateRoot> rootPath_;
  fboss::state::SwitchState state_;
  const folly::IPAddress marker_{"198.18.0.1"};
};

/* Untracked resolution keys must not consume a pending default-key change. */
TEST_P(E2ENexthopVrfTest, UntrackedResolutionVrfUpdatePreservesDefaultRoutes) {
  const auto nh = nexthop();
  const auto network = prefix();
  const auto markerNh = otherNexthop();
  const auto markerNetwork = prefix(/*second=*/true);
  injectNexthopStatuses(
      {NexthopStatus(NexthopKey{.ip = nh}, /*isReachable=*/true, 10),
       NexthopStatus(NexthopKey{.ip = markerNh}, /*isReachable=*/true, 10)});
  ASSERT_NO_FATAL_FAILURE(announce(network, kPeerAddr3, nh));
  ASSERT_NO_FATAL_FAILURE(expectAdvertised(network, nh));
  ASSERT_NO_FATAL_FAILURE(announce(markerNetwork, kPeerAddr3, markerNh));
  ASSERT_NO_FATAL_FAILURE(expectAdvertised(markerNetwork, markerNh));

  /* Withhold the default notification to expose a foreign cache reread. */
  const auto pending = cache_->addOrUpdateNextHopStatus(
      {NexthopStatus(NexthopKey{.ip = nh}, /*isReachable=*/false)});
  ASSERT_EQ(1, pending.size());
  ribInQ_.fiberPush(RibInNexthopUpdate({NexthopStatus(
      NexthopKey{.ip = nh, .nextHopVrfId = 7}, /*isReachable=*/false)}));
  /* A separate route's withdrawal fences the preceding queue messages. */
  injectNexthopStatuses(
      {NexthopStatus(NexthopKey{.ip = markerNh}, /*isReachable=*/false)});
  ASSERT_NO_FATAL_FAILURE(expectWithdrawn(markerNetwork));
  EXPECT_NE(nullptr, getBestPath(network));
  const auto fib =
      getFibWeightedNexthops(folly::IPAddress::networkToString(network));
  ASSERT_NE(nullptr, fib);
  EXPECT_TRUE(fib->contains(nh));

  ribInQ_.fiberPush(RibInNexthopUpdate(pending));
  ASSERT_NO_FATAL_FAILURE(expectWithdrawn(network));
}

TEST_P(E2ENexthopVrfTest, LateResolutionWithdrawalAndRecovery) {
  ASSERT_NO_FATAL_FAILURE(startWatcher());
  announce(prefix(), kPeerAddr3, nexthop());
  EXPECT_TRUE(cache_->isRegistered(NexthopKey{.ip = nexthop()}));
  EXPECT_TRUE(verifyRouteNotInShadowRib(prefix()));
  setHostRoute(nexthop(), fboss::kDefaultVrf, 10);
  publish();
  ASSERT_NO_FATAL_FAILURE(expectAdvertised(prefix(), nexthop()));
  removeHostRoute(nexthop());
  publish();
  expectWithdrawn(prefix());
  setHostRoute(nexthop(), fboss::kDefaultVrf, 20);
  publish();
  ASSERT_NO_FATAL_FAILURE(expectAdvertised(prefix(), nexthop()));
  EXPECT_EQ(
      20, cache_->getNexthopStatus(NexthopKey{.ip = nexthop()})->getIgpCost());
}

TEST_P(E2ENexthopVrfTest, SharedRoutesRetainRegistrationUntilLastWithdrawal) {
  ASSERT_NO_FATAL_FAILURE(startWatcher());
  announce(prefix(), kPeerAddr3, nexthop());
  announce(prefix(/*second=*/true), kPeerAddr4, nexthop());
  setHostRoute(nexthop(), fboss::kDefaultVrf, 10);
  publish();
  ASSERT_TRUE(waitForRouteInShadowRib(prefix()));
  ASSERT_TRUE(waitForRouteInShadowRib(prefix(/*second=*/true)));
  const auto expected =
      GetParam().first ? kNextHopV6_5.str() : kNextHopV4_5.str();
  EXPECT_TRUE(verifyRoutes(
      protocol(),
      kPeerAddr5,
      {{prefix().first.str(), prefix().second, expected, "", "", 0},
       {prefix(/*second=*/true).first.str(),
        prefix(/*second=*/true).second,
        expected,
        "",
        "",
        0}}));
  for (const auto& network : {prefix(), prefix(/*second=*/true)}) {
    const auto fib =
        getFibWeightedNexthops(folly::IPAddress::networkToString(network));
    ASSERT_NE(nullptr, fib);
    EXPECT_TRUE(fib->contains(nexthop()));
  }
  deleteRoute(protocol(), prefix().first.str(), prefix().second, kPeerAddr3);
  expectWithdrawn(prefix());
  EXPECT_TRUE(verifyNexthopRouteCount(nexthop(), 1));
  EXPECT_TRUE(cache_->isRegistered(NexthopKey{.ip = nexthop()}));
  removeHostRoute(nexthop());
  publish();
  expectWithdrawn(prefix(/*second=*/true));
  bringDownPeer(kPeerAddr4);
  ASSERT_TRUE(waitForSessionTerminationBaton(kPeerAddr4));
  ASSERT_TRUE(waitForRouteWithdrawnFromRib(
      folly::IPAddress::networkToString(prefix(/*second=*/true))));
  EXPECT_FALSE(cache_->isRegistered(NexthopKey{.ip = nexthop()}));
  EXPECT_EQ(
      std::nullopt, cache_->getNexthopStatus(NexthopKey{.ip = nexthop()}));
  announce(prefix(), kPeerAddr3, nexthop());
  setHostRoute(nexthop(), fboss::kDefaultVrf, 30);
  publish();
  ASSERT_NO_FATAL_FAILURE(expectAdvertised(prefix(), nexthop()));
}

/* Existing publication and retained cache entries both resolve new routes. */
TEST_P(E2ENexthopVrfTest, LiveInitialSyncAndCachedStatusResolveAnnouncements) {
  setHostRoute(nexthop(), fboss::kDefaultVrf, 10);
  ASSERT_NO_FATAL_FAILURE(startWatcher());
  announce(prefix(), kPeerAddr3, nexthop());
  ASSERT_NO_FATAL_FAILURE(expectAdvertised(prefix(), nexthop()));
  const auto epoch =
      cache_->getNexthopStatus(NexthopKey{.ip = nexthop()})->getEpoch();
  deleteRoute(protocol(), prefix().first.str(), prefix().second, kPeerAddr3);
  expectWithdrawn(prefix());
  EXPECT_FALSE(cache_->isRegistered(NexthopKey{.ip = nexthop()}));
  ASSERT_TRUE(
      cache_->getNexthopStatus(NexthopKey{.ip = nexthop()}).has_value());
  EXPECT_TRUE(
      cache_->getNexthopStatus(NexthopKey{.ip = nexthop()})->isReachable());
  announce(prefix(), kPeerAddr3, nexthop());
  ASSERT_NO_FATAL_FAILURE(expectAdvertised(prefix(), nexthop()));
  EXPECT_EQ(
      epoch, cache_->getNexthopStatus(NexthopKey{.ip = nexthop()})->getEpoch());
}

TEST_P(E2ENexthopVrfTest, CostChangesSelectAlternatePath) {
  ASSERT_NO_FATAL_FAILURE(startWatcher());
  announce(prefix(), kPeerAddr3, nexthop());
  announce(prefix(), kPeerAddr4, otherNexthop());
  setHostRoute(nexthop(), fboss::kDefaultVrf, 10);
  setHostRoute(otherNexthop(), fboss::kDefaultVrf, 20);
  publish();
  ASSERT_TRUE(waitForRouteInShadowRib(prefix(), kPeerAddr3));
  setHostRoute(nexthop(), fboss::kDefaultVrf, 30);
  publish();
  ASSERT_TRUE(waitForRouteInShadowRib(prefix(), kPeerAddr4));
  auto fib =
      getFibWeightedNexthops(folly::IPAddress::networkToString(prefix()));
  ASSERT_NE(nullptr, fib);
  EXPECT_TRUE(fib->contains(otherNexthop()));
  EXPECT_FALSE(fib->contains(nexthop()));
  removeHostRoute(otherNexthop());
  publish();
  ASSERT_TRUE(waitForRouteInShadowRib(prefix(), kPeerAddr3));
  fib = getFibWeightedNexthops(folly::IPAddress::networkToString(prefix()));
  ASSERT_NE(nullptr, fib);
  EXPECT_TRUE(fib->contains(nexthop()));
  EXPECT_FALSE(fib->contains(otherNexthop()));
}

TEST_P(E2ENexthopVrfTest, ReachableWithoutCostRemainsEligible) {
  ASSERT_NO_FATAL_FAILURE(startWatcher());
  announce(prefix(), kPeerAddr3, nexthop());
  setHostRoute(nexthop(), fboss::kDefaultVrf, std::nullopt);
  publish();
  ASSERT_NO_FATAL_FAILURE(expectAdvertised(prefix(), nexthop()));
  EXPECT_TRUE(cache_->getNexthopStatus(NexthopKey{.ip = nexthop()})
                  ->isResolvedForSelection());
  EXPECT_EQ(
      std::nullopt,
      cache_->getNexthopStatus(NexthopKey{.ip = nexthop()})->getIgpCost());
  removeHostRoute(nexthop());
  publish();
  expectWithdrawn(prefix());
}

TEST_P(E2ENexthopVrfTest, UnregisteredKeyKeepsStatusWithoutNotifications) {
  ASSERT_NO_FATAL_FAILURE(startWatcher(/*handlerOnly=*/true));
  const NexthopKey key{.ip = nexthop()};
  cache_->registerAndGetNexthopStatus(key);
  watcher_->requestNexthopSubscribe({key, key});
  setHostRoute(nexthop(), fboss::kDefaultVrf, 10);
  publish();
  expectUpdates({NexthopKey{.ip = nexthop()}}, /*reachable=*/true, 10);
  const auto epoch = cache_->getNexthopStatus(key)->getEpoch();
  cache_->unregisterAndRemoveNexthopStatus(key);
  setHostRoute(nexthop(), fboss::kDefaultVrf, 20);
  publish();
  expectNoMoreUpdates();
  EXPECT_FALSE(cache_->isRegistered(key));
  EXPECT_GT(cache_->getNexthopStatus(key)->getEpoch(), epoch);
  EXPECT_EQ(20, cache_->registerAndGetNexthopStatus(key).getIgpCost());
  removeHostRoute(nexthop());
  publish();
  expectUpdates(
      {NexthopKey{.ip = nexthop()}}, /*reachable=*/false, std::nullopt);
}

TEST_P(E2ENexthopVrfTest, SameAddressHasIndependentNextHopVrfs) {
  ASSERT_NO_FATAL_FAILURE(startWatcher(/*handlerOnly=*/true));
  cache_->registerAndGetNexthopStatus(NexthopKey{.ip = nexthop()});
  cache_->registerAndGetNexthopStatus(
      NexthopKey{.ip = nexthop(), .nextHopVrfId = 7});
  cache_->registerAndGetNexthopStatus(
      NexthopKey{.ip = nexthop(), .nextHopVrfId = 7});
  watcher_->requestNexthopSubscribe(
      {NexthopKey{.ip = nexthop()},
       NexthopKey{.ip = nexthop(), .nextHopVrfId = 7}});
  setHostRoute(nexthop(), fboss::kDefaultVrf, 10);
  setHostRoute(nexthop(), 7, 10);
  publish();
  expectUpdates(
      {NexthopKey{.ip = nexthop()},
       NexthopKey{.ip = nexthop(), .nextHopVrfId = 7}},
      /*reachable=*/true,
      10);
  const auto defaultEpoch =
      cache_->getNexthopStatus(NexthopKey{.ip = nexthop()})->getEpoch();
  setHostRoute(nexthop(), 7, 70);
  publish();
  expectUpdates(
      {NexthopKey{.ip = nexthop(), .nextHopVrfId = 7}}, /*reachable=*/true, 70);
  EXPECT_EQ(
      defaultEpoch,
      cache_->getNexthopStatus(NexthopKey{.ip = nexthop()})->getEpoch());
  const auto otherEpoch =
      cache_->getNexthopStatus(NexthopKey{.ip = nexthop(), .nextHopVrfId = 7})
          ->getEpoch();
  removeHostRoute(nexthop());
  publish();
  expectUpdates(
      {NexthopKey{.ip = nexthop()}}, /*reachable=*/false, std::nullopt);
  EXPECT_EQ(
      otherEpoch,
      cache_->getNexthopStatus(NexthopKey{.ip = nexthop(), .nextHopVrfId = 7})
          ->getEpoch());
  cache_->unregisterAndRemoveNexthopStatus(NexthopKey{.ip = nexthop()});
  EXPECT_TRUE(
      cache_->isRegistered(NexthopKey{.ip = nexthop(), .nextHopVrfId = 7}));
  removeHostRoute(nexthop(), 7);
  publish();
  expectUpdates(
      {NexthopKey{.ip = nexthop(), .nextHopVrfId = 7}},
      /*reachable=*/false,
      std::nullopt);
  expectNoMoreUpdates();
}

/*
 * A cost change while disconnected must reach only its registered context on
 * initial sync. The unchanged VRF must retain its epoch and emit no update.
 */
TEST_P(E2ENexthopVrfTest, ReconnectReconcilesCostWithoutVrfChurn) {
  ASSERT_NO_FATAL_FAILURE(startWatcher(/*handlerOnly=*/true));
  cache_->registerAndGetNexthopStatus(NexthopKey{.ip = nexthop()});
  cache_->registerAndGetNexthopStatus(
      NexthopKey{.ip = nexthop(), .nextHopVrfId = 7});
  watcher_->requestNexthopSubscribe(
      {NexthopKey{.ip = nexthop()},
       NexthopKey{.ip = nexthop(), .nextHopVrfId = 7}});
  setHostRoute(nexthop(), fboss::kDefaultVrf, 10);
  setHostRoute(nexthop(), 7, 10);
  publish();
  expectUpdates(
      {NexthopKey{.ip = nexthop()},
       NexthopKey{.ip = nexthop(), .nextHopVrfId = 7}},
      /*reachable=*/true,
      10);
  const auto epoch =
      cache_->getNexthopStatus(NexthopKey{.ip = nexthop(), .nextHopVrfId = 7})
          ->getEpoch();
  watcher_->getEventBase().runInEventBaseThreadAndWait(
      [this] { subMgr_->stop(); });
  setHostRoute(nexthop(), fboss::kDefaultVrf, 20);
  setHostRoute(marker_, fboss::kDefaultVrf, 101);
  publish();
  watcher_->subscribe();

  /* The changed marker fences receipt of the post-disconnect snapshot. */
  folly::F14FastSet<NexthopKey, NexthopKey::Hash> remaining{
      NexthopKey{.ip = marker_}, NexthopKey{.ip = nexthop()}};
  while (!remaining.empty()) {
    const auto message =
        test::boundedBlockingPop(handlerQ_, "reconnect snapshot");
    const auto* update = std::get_if<RibInNexthopUpdate>(&message);
    ASSERT_NE(nullptr, update);
    ASSERT_FALSE(update->nexthopStatuses.empty());
    for (const auto& status : update->nexthopStatuses) {
      ASSERT_EQ(1, remaining.erase(status.getNexthopKey()));
      if (status.getNexthop() == marker_) {
        EXPECT_EQ(101, status.getIgpCost());
      } else {
        EXPECT_EQ(nexthop(), status.getNexthop());
        EXPECT_TRUE(status.isReachable());
        EXPECT_EQ(20, status.getIgpCost());
      }
    }
  }
  EXPECT_TRUE(handlerQ_.empty());
  EXPECT_EQ(
      epoch,
      cache_->getNexthopStatus(NexthopKey{.ip = nexthop(), .nextHopVrfId = 7})
          ->getEpoch());
  EXPECT_TRUE(
      cache_->getNexthopStatus(NexthopKey{.ip = nexthop(), .nextHopVrfId = 7})
          ->isReachable());
  removeHostRoute(nexthop());
  publish();
  expectUpdates(
      {NexthopKey{.ip = nexthop()}}, /*reachable=*/false, std::nullopt);
}

INSTANTIATE_TEST_SUITE_P(
    AddressFamiliesAndUpdateGroups,
    E2ENexthopVrfTest,
    testing::Values(
        std::pair{false, false},
        std::pair{true, false},
        std::pair{false, true},
        std::pair{true, true}),
    [](const testing::TestParamInfo<E2ENexthopVrfTest::ParamType>& info) {
      return std::string(info.param.first ? "V6" : "V4") +
          (info.param.second ? "Groups" : "Peers");
    });

} // namespace facebook::bgp
