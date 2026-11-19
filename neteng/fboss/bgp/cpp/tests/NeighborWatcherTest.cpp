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

// #include <fboss/fsdb/client/FsdbPubSubManager.h>
#include <fb303/ThreadCachedServiceData.h>
#include <gtest/gtest.h>

#include <centralium/agent/tests/Commons.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/logging/xlog.h>
#include <folly/system/ThreadName.h>
#include <thrift/lib/cpp2/util/ScopedServerInterfaceThread.h>
#include <set>
#include <utility>

// Friend declaration for templated test fixture
#define NeighborWatcher_TEST_FRIENDS                                           \
  template <typename T>                                                        \
  friend class NeighborWatcherTestFixture;                                     \
  template <typename T>                                                        \
  FRIEND_TEST(NeighborWatcherTestFixture, fsdbCfgWatcherTrigger);              \
  template <typename T>                                                        \
  FRIEND_TEST(NeighborWatcherTestFixture, fsdbCfgWatcherNoTrigger);            \
  template <typename T>                                                        \
  FRIEND_TEST(NeighborWatcherTestFixture, fsdbCfgWatcherSub);                  \
  template <typename T>                                                        \
  FRIEND_TEST(NeighborWatcherTestFixture, SwitchImplicitlyNotReachableTest);   \
  template <typename T>                                                        \
  FRIEND_TEST(NeighborWatcherTestFixture, SwitchExplicitlyNotReachableTest);   \
  template <typename T>                                                        \
  FRIEND_TEST(NeighborWatcherTestFixture, SwitchReachableTest);                \
  template <typename T>                                                        \
  FRIEND_TEST(NeighborWatcherTestFixture, EmptyDsfSwitchReachabilityTable);    \
  template <typename T>                                                        \
  FRIEND_TEST(NeighborWatcherTestFixture, DsfFastTearDownFlagDisabledTest);    \
  template <typename T>                                                        \
  FRIEND_TEST(NeighborWatcherTestFixture, GetNbrEntryChanges_EntryDeleted);    \
  template <typename T>                                                        \
  FRIEND_TEST(                                                                 \
      NeighborWatcherTestFixture, GetNbrEntryChanges_EntryBecomesUnresolved);  \
  template <typename T>                                                        \
  FRIEND_TEST(NeighborWatcherTestFixture, GetNbrEntryChanges_EntryAdded);      \
  template <typename T>                                                        \
  FRIEND_TEST(                                                                 \
      NeighborWatcherTestFixture, GetNbrEntryChanges_EntryBecomesResolved);    \
  template <typename T>                                                        \
  FRIEND_TEST(                                                                 \
      NeighborWatcherTestFixture, GetNbrEntryChanges_LinkLocalIgnored);        \
  template <typename T>                                                        \
  FRIEND_TEST(                                                                 \
      NeighborWatcherTestFixture, GetNbrEntryChanges_UnresolvedEntryRemoved);  \
  template <typename T>                                                        \
  FRIEND_TEST(NeighborWatcherTestFixture, GetNbrEntryChanges_IPv6Addresses);   \
  template <typename T>                                                        \
  FRIEND_TEST(NeighborWatcherTestFixture, GetNbrEntryChanges_MultipleEntries); \
  template <typename T>                                                        \
  FRIEND_TEST(                                                                 \
      NeighborWatcherTestFixture, GetNbrEntryChanges_UnresolvedInBoth);        \
  template <typename T>                                                        \
  FRIEND_TEST(NeighborWatcherTestFixture, GetNbrEntryChanges_ResolvedInBoth);  \
  template <typename T>                                                        \
  FRIEND_TEST(                                                                 \
      NeighborWatcherTestFixture,                                              \
      GetNbrEntryChanges_NewUnresolvedEntryNotAdded);                          \
  template <typename T>                                                        \
  FRIEND_TEST(                                                                 \
      NeighborWatcherTestFixture,                                              \
      GetNbrEntryChanges_PortIdSetButStatePending);                            \
  template <typename T>                                                        \
  FRIEND_TEST(                                                                 \
      NeighborWatcherTestFixture, GetNbrEntryChanges_StateBecomesPending);     \
  template <typename T>                                                        \
  FRIEND_TEST(                                                                 \
      NeighborWatcherTestFixture, GetNbrEntryChanges_StateBecomesReachable);   \
  template <typename T>                                                        \
  FRIEND_TEST(                                                                 \
      NeighborWatcherTestFixture,                                              \
      CollectResolvedIpsFromTable_FiltersPendingEntries);                      \
  template <typename T>                                                        \
  FRIEND_TEST(                                                                 \
      NeighborWatcherTestFixture,                                              \
      ProcessInterfaceMapChanges_InterfaceIdRemovedFromNewMap);                \
  template <typename T>                                                        \
  FRIEND_TEST(                                                                 \
      NeighborWatcherTestFixture,                                              \
      ProcessInterfaceMapChanges_InterfaceIdAddedInNewMap);                    \
  template <typename T>                                                        \
  FRIEND_TEST(                                                                 \
      NeighborWatcherTestFixture,                                              \
      ProcessInterfaceMapChanges_FirstInterfaceMapUpdate);                     \
  template <typename T>                                                        \
  FRIEND_TEST(                                                                 \
      NeighborWatcherTestFixture,                                              \
      ProcessInterfaceMapChanges_NexthopResolutionUpdatePushedToRibInQ);       \
  template <typename T>                                                        \
  FRIEND_TEST(                                                                 \
      NeighborWatcherTestFixture,                                              \
      ProcessInterfaceMapChanges_NoUpdateSkipsRibInQPush);                     \
  template <typename T>                                                        \
  FRIEND_TEST(                                                                 \
      NeighborWatcherTestFixture,                                              \
      ProcessInterfaceMapChanges_CacheUpdatedEvenWithNoChanges);

#include "common/services/cpp/TLSConfig.h"
#include "fboss/agent/Constants.h"
#include "fboss/lib/thrift_service_client/ConnectionOptions.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/lib/coro/BackPressuredQueue.h"
#include "neteng/fboss/bgp/cpp/peer/NeighborWatcher.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/BoundedWaitUtils.h"

namespace facebook::bgp {

using namespace fboss::fsdb;
using namespace ::testing;
using namespace std::chrono_literals;

const thriftpath::RootThriftPath<FsdbOperStateRoot> kRootPath;
const auto kCidrV4 = folly::IPAddress::createNetwork("10.141.156.73/31");
const auto kCidrV6 =
    folly::IPAddress::createNetwork("2401:db00:e01e:2004::49/127");
const auto kSpeedMbps = fboss::cfg::PortSpeed::HUNDREDG;
const auto kPortNum = 110;
const auto kPortName = "eth1/29/1";
const auto kVlanId = 4001;
const auto kInterfaceId1 = 5001;
const auto kInterfaceId2 = 5002;
const auto kCidrNbrV6 = folly::IPAddressV6("2401:db00:e01e:2004::48");

template <bool EnableFsdbPatchAPI>
struct TestParams {
  static constexpr auto enableFsdbPatchApi = EnableFsdbPatchAPI;
};

using NeighborWatcherTestTypes =
    ::testing::Types<TestParams<false>, TestParams<true>>;

template <typename TestParam>
class NeighborWatcherTestFixture : public ::testing::Test {
 public:
  void SetUp() override {
    FLAGS_enable_fsdb_patch_subscriber = TestParam::enableFsdbPatchApi;
    fsdbHandler_ = std::make_shared<centralium::agent::MockFsdbHandler>();
    fsdbThread_ = std::make_unique<apache::thrift::ScopedServerInterfaceThread>(
        fsdbHandler_,
        "::1",
        0,
        services::TLSConfig::applyDefaultsToThriftServer);

    initializeNbrWatcher();

    // create fsdb manager mainly for publish purpose
    fsdbPubSubManager_ = std::make_unique<FsdbPubSubManager>(
        "NeighborWatcherTest",
        // reconnectEvb
        fsdbClientEvbThread_.getEventBase(),
        // subscriberEvb
        fsdbClientEvbThread_.getEventBase(),
        // statsPublisherEvb
        fsdbClientEvbThread_.getEventBase(),
        // statePublisherEvb
        fsdbClientEvbThread_.getEventBase());
  }

  void TearDown() override {
    XLOG(INFO, "Tearing down fsdb handler");
    fsdbHandler_.reset();
    fsdbThread_.reset();
  }

  void initializeNbrWatcher() {
    nbrWatcher_ = std::make_shared<NeighborWatcher>(
        neighborEventQ_,
        ribInQ_,
        true /* enableDsfFastTearDown */,
        nullptr /* sharedFsdbSubMgr */,
        fsdbThread_->getPort());
    fsdbNbrWatcher_ = nbrWatcher_->fsdbNbrWatcher_;
    fsdbCfgWatcher_ = nbrWatcher_->fsdbConfigWatcher_;
    fsdbReachabilityWatcher_ = nbrWatcher_->fsdbReachabilityWatcher_;
  }

  fboss::cfg::SwitchConfig buildSwitchConfig() {
    // Set up Interface Cfg info
    fboss::cfg::Interface interface;
    interface.intfID() = kInterfaceId1;
    interface.vlanID() = kVlanId;
    interface.ipAddresses()->emplace_back(
        folly::IPAddress::networkToString(kCidrV4));
    interface.ipAddresses()->emplace_back(
        folly::IPAddress::networkToString(kCidrV6));

    // Set up Port Cfg info
    fboss::cfg::Port port;
    port.logicalID() = kPortNum;
    port.name() = kPortName;
    port.ingressVlan() = kVlanId;
    port.speed() = kSpeedMbps;

    // Attach Interface and Port Cfg to SwitchConfig
    fboss::cfg::SwitchConfig switchConfig;
    switchConfig.interfaces()->emplace_back(interface);
    switchConfig.ports()->emplace_back(port);
    return switchConfig;
  }

  void publishSwitchConfigPath() {
    auto switchConfig = buildSwitchConfig();
    fboss::fsdb::OperState state;
    state.contents() =
        fboss::thrift_cow::serialize<apache::thrift::type_class::structure>(
            OperProtocol::BINARY, switchConfig);
    state.protocol() = OperProtocol::BINARY;
    fsdbPubSubManager_->publishState(std::move(state));
  }

  void publishDelta(
      const std::vector<std::string>& pathTokens,
      const fboss::fsdb::fbbinary& data) {
    OperDeltaUnit unit;
    unit.path()->raw() = pathTokens;
    unit.newState() = data;

    OperDelta delta;
    delta.changes() = {unit};
    delta.protocol() = OperProtocol::BINARY;

    fsdbPubSubManager_->publishState(std::move(delta));
  }

  void publishInterfaceMapDelta(
      std::map<int32_t, fboss::state::InterfaceFields>& interfaceFields) {
    std::map<
        fboss::state::SwitchIdList,
        std::map<int32_t, fboss::state::InterfaceFields>>
        interfaceMaps = {{"0", interfaceFields}};

    publishDelta(
        kRootPath.agent()
            .switchState()
            .interfaceMaps()
            .tokens() /* pathTokens */,
        fboss::thrift_cow::serialize<apache::thrift::type_class::structure>(
            OperProtocol::BINARY, interfaceMaps) /* data */);
  }

  // Create interfaceMap based on the input given
  std::map<int32_t, fboss::state::InterfaceFields> createInterfaceMap(
      std::string ipaddr,
      int port,
      int interfaceId,
      bool isV6Addr) {
    fboss::state::NeighborEntryFields nef;
    nef.ipaddress() = ipaddr;
    nef.portId()->portId() = port;
    std::map<std::string, fboss::state::NeighborEntryFields> arpTable;
    std::map<std::string, fboss::state::NeighborEntryFields> ndpTable;
    if (isV6Addr) {
      ndpTable[ipaddr] = nef;
    } else {
      arpTable[ipaddr] = nef;
    }
    fboss::state::InterfaceFields interfaceFields;
    interfaceFields.interfaceId() = interfaceId;
    interfaceFields.arpTable() = arpTable;
    interfaceFields.ndpTable() = ndpTable;
    std::map<int32_t, fboss::state::InterfaceFields> interfaceMap;
    interfaceMap[interfaceId] = interfaceFields;
    return interfaceMap;
  }

  /*
   * Construct a dsfSwitchReachability table given the
   * switchId and a switchIdToFabricPortGroup (map>i64, i32>)
   * map, then serialize the table to binary.
   *
   * An example of dsfSwitchReachability table:
   * First we are given a switchIdToFabricPortGroupMap =
   * {
   *   {1, 1},
   *   {2, 1},
   *   {400, 1},
   * }
   * Then we construct a SwitchReachability struct:
   *   switchReachability = {
   *      "switchIdToFabricPortGroupMap" = {
   *         {1, 1},
   *         {2, 1},
   *         {400, 1},
   *      }
   *   }
   * and then we create
   *   dsfSwitchReachability = {
   *       {400, switchReachability},
   *   },
   * and serialize dsfSwitchReachability to binary.
   */
  const fboss::fsdb::fbbinary createSwitchReachability(
      const int64_t switchId,
      const std::map<int64_t, int32_t>& switchIdToFabricPortGroup) {
    fboss::switch_reachability::SwitchReachability reachability;
    reachability.switchIdToFabricPortGroupMap() = switchIdToFabricPortGroup;
    std::map<int64_t, fboss::switch_reachability::SwitchReachability>
        switchReachabilityMap;
    switchReachabilityMap[switchId] = reachability;

    return fboss::thrift_cow::serialize<apache::thrift::type_class::structure>(
        OperProtocol::BINARY, switchReachabilityMap);
  }

  void publishSwitchReachability(const fboss::fsdb::fbbinary& data) {
    publishDelta(
        kRootPath.agent().dsfSwitchReachability().tokens() /* pathTokens */,
        data);
  }

  /*
   * Call fsdbInterfaceStateCb and loop the EventBase once so the
   * async coroutine (processInterfaceMapChanges) scheduled via
   * asyncScope_.add() actually executes before we check queue state.
   */
  void callFsdbInterfaceStateCbAndDrain(
      const std::map<int32_t, fboss::state::InterfaceFields>& interfaceMap) {
    /*
     * In production, fsdbInterfaceStateCb schedules processInterfaceMapChanges
     * asynchronously via asyncScope_.add(). In tests, we call
     * processInterfaceMapChanges directly via blockingWait so the coroutine
     * completes synchronously before we check queue state.
     */
    XLOG_IF(
        INFO,
        fsdbNbrWatcher_->interfaceMap_ == nullptr,
        "FSDB: first time receiving interfaceMap state update");
    XLOGF(
        INFO,
        "FSDB: current interfaceMap size: {}, new interfaceMap size: {}",
        fsdbNbrWatcher_->interfaceMap_ ? fsdbNbrWatcher_->interfaceMap_->size()
                                       : 0,
        interfaceMap.size());
    folly::coro::blockingWait(
        fsdbNbrWatcher_->processInterfaceMapChanges(interfaceMap));
  }

  /*
   * To trigger FSDB nbr_down, send two states.  The first with the address
   * resolved to a port, the second with portId set to 0
   */
  void triggerFsdbNbrDown(std::string ipaddr, int port, int interfaceId) {
    auto isV6Addr = folly::IPAddress(ipaddr).version() == 6;

    // Send the Initial State with port up
    auto interfaceMap = createInterfaceMap(ipaddr, port, interfaceId, isV6Addr);
    callFsdbInterfaceStateCbAndDrain(interfaceMap);

    // Now set port to 0 and send new state
    if (isV6Addr) {
      interfaceMap.at(interfaceId).ndpTable()[ipaddr].portId()->portId() = 0;
    } else {
      interfaceMap.at(interfaceId).arpTable()[ipaddr].portId()->portId() = 0;
    }
    callFsdbInterfaceStateCbAndDrain(interfaceMap);
  }

  /**
   * @brief Create a FSDB delta subscriber to root /
   * Used to test Mocked FSDB only.
   */
  folly::coro::Task<void> createFsdbDeltaSubscriber() {
    folly::coro::Baton subReady;
    auto stateCb = [&](auto /* oldstate */,
                       auto newstate,
                       std::optional<bool> /*initialSyncHasData*/) {
      if (newstate == fboss::fsdb::SubscriptionState::CONNECTED) {
        XLOG(INFO, "[NeighborWatcherTest]: FsdbDeltaSubscriber ready");
        subReady.post();
      } else {
        XLOG(INFO, "[NeighborWatcherTest]: FsdbDeltaSubscriber not ready");
      }
    };

    // delta callback
    auto deltaCb = [&](fboss::fsdb::OperDelta&& operDelta) {
      for (auto& change : *operDelta.changes()) {
        XLOGF(
            INFO,
            "[NeighborWatcherTest]: receive OperDelta at /{}",
            folly::join("/", *change.path()->raw()));
      }
    };

    fsdbPubSubManager_->addStateDeltaSubscription(
        kRootPath.tokens(),
        std::move(stateCb),
        std::move(deltaCb),
        fboss::utils::ConnectionOptions("::1", fsdbThread_->getPort()));

    co_await subReady;
  }

  /**
   * @brief Create a FSDB path subscriber to /agent/config/sw
   * Used to test Mocked FSDB only.
   */
  folly::coro::Task<void> createFsdbPathSubscriber() {
    folly::coro::Baton subReady;
    auto stateCb = [&](auto /* oldstate */,
                       auto newstate,
                       std::optional<bool> /*initialSyncHasData*/) {
      if (newstate == fboss::fsdb::SubscriptionState::CONNECTED) {
        XLOG(INFO, "[NeighborWatcherTest]: FsdbPathSubsriber ready");
        subReady.post();
      } else {
        XLOG(INFO, "[NeighborWatcherTest]: FsdbPathSubscriber not ready");
      }
    };
    auto pathCb = [&](fboss::fsdb::OperState&& operState) {
      XLOGF(
          INFO,
          "[NeighborWatcherTest]: receive OperState at /{}",
          folly::join("/", kRootPath.agent().config().sw().tokens()));
      auto switchConfig = apache::thrift::BinarySerializer::deserialize<
          typename decltype(kRootPath.agent().config().sw())::DataT>(
          *operState.contents());
      XLOGF(INFO, "{}", switchConfig.interfaces()->at(0).intfID().value());
    };
    fsdbPubSubManager_->addStatePathSubscription(
        kRootPath.agent().config().sw().tokens(),
        stateCb,
        pathCb,
        fboss::utils::ConnectionOptions("::1", fsdbThread_->getPort()));

    co_await subReady;
  }

  /**
   * @brief Create a FSDB delta publisher to root /
   * Used to publish any delta changes to Mocked FSDB.
   */
  folly::coro::Task<void> createFsdbDeltaPublisher() {
    folly::coro::Baton publisherReady;
    auto stateCb = [&](FsdbStreamClient::State oldState,
                       FsdbStreamClient::State newState) {
      // only post the publisher readiness when state turns into connected
      if (newState == FsdbStreamClient::State::CONNECTED &&
          oldState != newState) {
        XLOG(INFO, "[NeighborWatcherTest]: FsdbDeltaPublisher ready");
        publisherReady.post();
      } else {
        XLOG(INFO, "[NeighborWatcherTest]: FsdbDeltaPublisher not ready");
      }
    };

    fsdbPubSubManager_->createStateDeltaPublisher(
        {}, stateCb, fsdbThread_->getPort());
    co_await publisherReady;
  }

  /**
   * @brief Create a FSDB state publisher to /agent/config/sw
   * Used to publish switchConfig to Mocked FSDB.
   */
  folly::coro::Task<void> createFsdbStatePublisher() {
    folly::coro::Baton publisherReady;
    fsdbPubSubManager_->createStatePathPublisher(
        kRootPath.agent().config().sw().tokens(),
        //{},
        [&](auto /* oldState */, auto newState) {
          if (newState == fboss::fsdb::FsdbStreamClient::State::CONNECTED) {
            XLOG(INFO, "[NeighborWatcherTest]: FsdbStatePublisher ready");
            publisherReady.post();
          } else {
            XLOG(INFO, "[NeighborWatcherTest]: FsdbStatePublisher not ready");
          }
        },
        fsdbThread_->getPort());
    co_await publisherReady;
  }

  void cleanUpFsdbPubSub() {
    fsdbPubSubManager_->removeStateDeltaSubscription(kRootPath.tokens());
    fsdbPubSubManager_->removeStatePathSubscription(
        kRootPath.agent().config().sw().tokens());
    fsdbPubSubManager_->removeStatePathPublisher();
    fsdbPubSubManager_->removeStateDeltaPublisher();
  }

  MonitoredMPMCQueue<NeighborWatcherMessage> neighborEventQ_{};
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ_{
      nettools::bgplib::kMaxIngressQueueSize};
  std::shared_ptr<FsdbNeighborWatcher> fsdbNbrWatcher_;
  std::shared_ptr<NeighborWatcher> nbrWatcher_;
  std::shared_ptr<FsdbConfigWatcher> fsdbCfgWatcher_;
  std::shared_ptr<FsdbSwitchReachabilityWatcher> fsdbReachabilityWatcher_;

  // fsdb client to pub/sub test state/stats to MockFsdbHandler
  folly::ScopedEventBaseThread fsdbClientEvbThread_;
  std::unique_ptr<FsdbPubSubManager> fsdbPubSubManager_{nullptr};

  // thread and Handle for MockFsdb
  std::shared_ptr<centralium::agent::MockFsdbHandler> fsdbHandler_{nullptr};
  std::unique_ptr<apache::thrift::ScopedServerInterfaceThread> fsdbThread_{
      nullptr};
};

TYPED_TEST_SUITE(NeighborWatcherTestFixture, NeighborWatcherTestTypes);

/*
 * Verify that we get expected results of "peer subnet to lbw" mapping via
 * results from FSDB switch config subscription
 */
TYPED_TEST(NeighborWatcherTestFixture, fsdbCfgWatcherTrigger) {
  FLAGS_fsdb_config_timeout_s = 1;

  std::optional<folly::F14NodeMap<folly::CIDRNetwork, int64_t>>
      peerSubnetLbwMap = std::nullopt;

  // Start nbrWatcher event base loop in nbrWatcherThread
  std::thread nbrWatcherThread([&]() {
    this->nbrWatcher_->evb_.loopForever();
    XLOG(INFO, "nbrWatcherThread got stopped");
  });

  /*
   * Create an EventBase for test job, and in it, wait till we get
   * peerSubnetLbwMap
   */
  folly::EventBase testEvb;
  testEvb.runInEventBaseThread([&] {
    while (!peerSubnetLbwMap) {
      peerSubnetLbwMap = this->nbrWatcher_->getFsdbPeerSubnetLbwMap();
    }
  });

  /*
   * Trigger FSDB Cfg publish after a short sleep in order to ensure that
   * getFsdbPeerSubnetLbwMap times out at least once
   */
  folly::futures::sleep(std::chrono::seconds(FLAGS_fsdb_config_timeout_s * 2))
      .get();
  this->nbrWatcher_->evb_.runInEventBaseThread([&] {
    this->fsdbCfgWatcher_->fsdbSwitchCfgCb(this->buildSwitchConfig());
  });

  // We will exit this loop once we get peerSubnetLbwMap
  testEvb.loop();

  // Verify results
  XLOG(INFO, "Verify results");
  ASSERT_TRUE(peerSubnetLbwMap);
  EXPECT_EQ(2, peerSubnetLbwMap->size());
  EXPECT_EQ(static_cast<int>(kSpeedMbps), peerSubnetLbwMap->at(kCidrV4));
  EXPECT_EQ(static_cast<int>(kSpeedMbps), peerSubnetLbwMap->at(kCidrV6));

  // Clean up
  this->nbrWatcher_->evb_.terminateLoopSoon();
  nbrWatcherThread.join();
}

/*
 * Negative test of fsdbCfgWatcherTrigger test -- verify that we return from
 * getPeerSubnetLbwMap after timeout even if there was not FSDB config trigger
 */
TYPED_TEST(NeighborWatcherTestFixture, fsdbCfgWatcherNoTrigger) {
  FLAGS_fsdb_config_timeout_s = 1;

  std::thread evbLoopThread([&]() {
    this->nbrWatcher_->evb_.loopForever();
    XLOG(INFO, "evbLoopThread got stopped");
  });

  // Wait for Peer LBW Map.  This will block until baton timeout
  auto peerSubnetLbwMap = this->nbrWatcher_->getFsdbPeerSubnetLbwMap();

  // Wait for results
  this->nbrWatcher_->evb_.terminateLoopSoon();

  // Verify results
  XLOG(INFO, "Verify results");
  ASSERT_FALSE(peerSubnetLbwMap);
  evbLoopThread.join();
}

// Verify that we use nbr_down signal from FSDB
TYPED_TEST(NeighborWatcherTestFixture, nbrWatcherUseFsdb) {
  std::string ipaddr = "2401:db00:e01e:2004::48";

  // Now trigger nbr_down using FSDB signal
  this->triggerFsdbNbrDown(ipaddr, kPortNum, kInterfaceId1);

  // Verify that we enqueue the nbr_down message for PeerManger
  ASSERT_EQ(this->neighborEventQ_.size(), 1);
  // Verify that we enqueue both the nbr_up and nbr_down message for Rib
  ASSERT_EQ(this->ribInQ_.size(), 2);

  // Verify contents of the message
  {
    auto msg = facebook::bgp::test::boundedBlockingPop(
        this->neighborEventQ_, "neighborEventQ_");
    EXPECT_FALSE(std::get<NeighborEventMsg>(msg).isUp);
    EXPECT_EQ(std::get<NeighborEventMsg>(msg).nbrAddr.str(), ipaddr);
    EXPECT_EQ(std::get<NeighborEventMsg>(msg).vrfId, fboss::kDefaultVrf);
  }

  // Verify the contents of the two messages in ribInQ_
  {
    auto msg =
        facebook::bgp::test::boundedBlockingPop(this->ribInQ_, "ribInQ_");
    EXPECT_EQ(std::get<NexthopResolutionUpdate>(msg).resolved.size(), 1);
    EXPECT_EQ(std::get<NexthopResolutionUpdate>(msg).resolved[0].str(), ipaddr);
  }
  {
    auto msg =
        facebook::bgp::test::boundedBlockingPop(this->ribInQ_, "ribInQ_");
    EXPECT_EQ(std::get<NexthopResolutionUpdate>(msg).unresolved.size(), 1);
    EXPECT_EQ(
        std::get<NexthopResolutionUpdate>(msg).unresolved[0].str(), ipaddr);
  }
}

/*
 * Verify fsdb dsfSwitchReachability subscription doesn't exist
 * if NeighborWatcher does not have enableDsfFastTearDown as true, and
 * neighborWatcher should still run correctly.
 */
CO_TYPED_TEST(NeighborWatcherTestFixture, DsfFastTearDownFlagDisabledTest) {
  auto flagDisabledWatcher = std::make_shared<NeighborWatcher>(
      this->neighborEventQ_,
      this->ribInQ_,
      false /* enableDsfFastTearDown */,
      nullptr /* sharedFsdbSubMgr */,
      this->fsdbThread_->getPort());
  EXPECT_TRUE(flagDisabledWatcher->fsdbNbrWatcher_);
  EXPECT_TRUE(flagDisabledWatcher->fsdbConfigWatcher_);
  EXPECT_FALSE(flagDisabledWatcher->fsdbReachabilityWatcher_);

  auto neighborWatcherThread = std::thread([&]() {
    const std::string threadName = "neighbor_watcher";
    XLOGF(INFO, "Starting {} thread...", threadName);
    folly::setThreadName(threadName);
    google::SetLogThreadName(threadName); // glog name
    flagDisabledWatcher->run();
    XLOGF(INFO, "[Exit] {} got stopped", threadName);
  });

  co_await folly::coro::sleepReturnEarlyOnCancel(3s);
  flagDisabledWatcher->stop();
  neighborWatcherThread.join();
}

/*
 * Verify NeighborReachabilityMsg sent when self switchId is not
 * populated in switchIdToFabricPortGroupMap.
 */
CO_TYPED_TEST(NeighborWatcherTestFixture, SwitchImplicitlyNotReachableTest) {
  EXPECT_TRUE(this->nbrWatcher_->fsdbReachabilityWatcher_);

  XLOG(INFO, "Create FSDB delta publisher");
  co_await this->createFsdbDeltaPublisher();

  int switchId = 100;
  int kPortGroup = 1;
  // switchIdToFabricPortGroupMap with filler entries.
  std::map<int64_t, int32_t> switchIdToFabricPortGroupMap = {
      {switchId + 1, kPortGroup},
      {switchId + 2, kPortGroup},
      {switchId + 3, kPortGroup},
  };
  this->publishSwitchReachability(
      this->createSwitchReachability(switchId, switchIdToFabricPortGroupMap));

  //  Start nbrWatcher_ in a separate thread
  auto neighborWatcherThread = std::thread([&]() {
    const std::string threadName = "neighbor_watcher";
    XLOGF(INFO, "Starting {} thread...", threadName);
    folly::setThreadName(threadName);
    google::SetLogThreadName(threadName); // glog name
    this->nbrWatcher_->run();
    XLOGF(INFO, "[Exit] {} got stopped", threadName);
  });

  co_await folly::coro::sleepReturnEarlyOnCancel(3s);

  /*
   * Verify contents of the message. There is nothing
   * inside, so we just get the type of the variant.
   */
  EXPECT_EQ(this->neighborEventQ_.size(), 1);
  auto msg = facebook::bgp::test::boundedBlockingPop(
      this->neighborEventQ_, "neighborEventQ_");
  EXPECT_TRUE(std::holds_alternative<NeighborReachabilityMsg>(msg));

  this->cleanUpFsdbPubSub();
  this->nbrWatcher_->stop();
  neighborWatcherThread.join();
}

/*
 * Verify NeighborReachabilityMsg sent when self switchId has
 * invalid port group in switchIdToPortGroupMap.
 */
CO_TYPED_TEST(NeighborWatcherTestFixture, SwitchExplicitlyNotReachableTest) {
  EXPECT_TRUE(this->nbrWatcher_->fsdbReachabilityWatcher_);

  XLOG(INFO, "Create FSDB delta publisher");
  co_await this->createFsdbDeltaPublisher();

  int switchId = 100;
  int kPortGroup = 1;
  // switchIdToFabricPortGroupMap with filler entries.
  std::map<int64_t, int32_t> switchIdToFabricPortGroupMap = {
      {switchId, kNoPortGroup},
      {switchId + 1, kPortGroup},
      {switchId + 2, kPortGroup},
      {switchId + 3, kPortGroup},
  };
  this->publishSwitchReachability(
      this->createSwitchReachability(switchId, switchIdToFabricPortGroupMap));

  //  Start nbrWatcher_ in a separate thread
  auto neighborWatcherThread = std::thread([&]() {
    const std::string threadName = "neighbor_watcher";
    XLOGF(INFO, "Starting {} thread...", threadName);
    folly::setThreadName(threadName);
    google::SetLogThreadName(threadName); // glog name
    this->nbrWatcher_->run();
    XLOGF(INFO, "[Exit] {} got stopped", threadName);
  });

  co_await folly::coro::sleepReturnEarlyOnCancel(3s);

  /*
   * Verify contents of the message. There is nothing
   * inside, so we just get the type of the variant.
   */
  EXPECT_EQ(this->neighborEventQ_.size(), 1);
  auto msg = facebook::bgp::test::boundedBlockingPop(
      this->neighborEventQ_, "neighborEventQ_");
  EXPECT_TRUE(std::holds_alternative<NeighborReachabilityMsg>(msg));

  this->cleanUpFsdbPubSub();
  this->nbrWatcher_->stop();
  neighborWatcherThread.join();
}

/*
 * Verify NeighborReachabilityMsg not sent when self switchId is
 * reachable.
 */
CO_TYPED_TEST(NeighborWatcherTestFixture, SwitchReachableTest) {
  EXPECT_TRUE(this->nbrWatcher_->fsdbReachabilityWatcher_);

  XLOG(INFO, "Create FSDB delta publisher");
  co_await this->createFsdbDeltaPublisher();

  int switchId = 100;
  int kPortGroup = 1;
  // switchIdToFabricPortGroupMap with filler entries.
  std::map<int64_t, int32_t> switchIdToFabricPortGroupMap = {
      {switchId, kPortGroup},
      {switchId + 1, kPortGroup},
      {switchId + 2, kPortGroup},
      {switchId + 3, kPortGroup},
  };
  this->publishSwitchReachability(
      this->createSwitchReachability(switchId, switchIdToFabricPortGroupMap));

  //  Start nbrWatcher_ in a separate thread
  auto neighborWatcherThread = std::thread([&]() {
    const std::string threadName = "neighbor_watcher";
    XLOGF(INFO, "Starting {} thread...", threadName);
    folly::setThreadName(threadName);
    google::SetLogThreadName(threadName); // glog name
    this->nbrWatcher_->run();
    XLOGF(INFO, "[Exit] {} got stopped", threadName);
  });

  co_await folly::coro::sleepReturnEarlyOnCancel(3s);

  EXPECT_TRUE(this->neighborEventQ_.empty());

  this->cleanUpFsdbPubSub();
  this->nbrWatcher_->stop();
  neighborWatcherThread.join();
}

/*
 * Check that queue is not populated with NeighborReachability msg
 * when there is no dsfSwitchReachability table, or the table is empty.
 */
CO_TYPED_TEST(NeighborWatcherTestFixture, EmptyDsfSwitchReachabilityTable) {
  EXPECT_TRUE(this->nbrWatcher_->fsdbReachabilityWatcher_);

  XLOG(INFO, "Create FSDB delta publisher");
  co_await this->createFsdbDeltaPublisher();

  //  Start nbrWatcher_ in a separate thread
  auto neighborWatcherThread = std::thread([&]() {
    const std::string threadName = "neighbor_watcher";
    XLOGF(INFO, "Starting {} thread...", threadName);
    folly::setThreadName(threadName);
    google::SetLogThreadName(threadName); // glog name
    this->nbrWatcher_->run();
    XLOGF(INFO, "[Exit] {} got stopped", threadName);
  });

  // Run with no switchReachability updates.
  this->publishSwitchReachability({} /* data */);
  co_await folly::coro::sleepReturnEarlyOnCancel(3s);
  EXPECT_TRUE(this->neighborEventQ_.empty());

  // Publish empty table.
  std::map<int64_t, fboss::switch_reachability::SwitchReachability> emptyData;
  this->publishSwitchReachability(
      fboss::thrift_cow::serialize<apache::thrift::type_class::structure>(
          OperProtocol::BINARY, emptyData));
  co_await folly::coro::sleepReturnEarlyOnCancel(3s);
  EXPECT_TRUE(this->neighborEventQ_.empty());

  this->cleanUpFsdbPubSub();
  this->nbrWatcher_->stop();
  neighborWatcherThread.join();
}

CO_TYPED_TEST(NeighborWatcherTestFixture, fsdbCfgWatcherSub) {
  XLOG(INFO, "Create FSDB publisher and publish switchConfig");
  co_await this->createFsdbStatePublisher();
  this->publishSwitchConfigPath();

  //  Start nbrWatcher_ in a separate thread
  auto neighborWatcherThread = std::thread([&]() {
    const std::string threadName = "neighbor_watcher";
    XLOGF(INFO, "Starting {} thread...", threadName);
    folly::setThreadName(threadName);
    google::SetLogThreadName(threadName); // glog name
    this->nbrWatcher_->run();
    XLOGF(INFO, "[Exit] {} got stopped", threadName);
  });

  co_await folly::coro::sleepReturnEarlyOnCancel(3s);

  /*
   * Verify results
   * We should see 2 entries (kCidrV4/6) in peerSubnetLbwMap
   */
  auto peerSubnetLbwMap = this->nbrWatcher_->getFsdbPeerSubnetLbwMap();
  XLOG(INFO, "Verify fsdbCfgWatcher results");
  CO_ASSERT_TRUE(peerSubnetLbwMap);
  EXPECT_EQ(2, peerSubnetLbwMap->size());
  EXPECT_EQ(static_cast<int>(kSpeedMbps), peerSubnetLbwMap->at(kCidrV4));
  EXPECT_EQ(static_cast<int>(kSpeedMbps), peerSubnetLbwMap->at(kCidrV6));
  XLOG(INFO, "Verify fsdbCfgWatcher done");

  /*
   * Cleanup existing FSDB State Publisher and create a Delta Publisher
   * FsdbPubSubManager only allow on Publisher exist.
   */
  this->cleanUpFsdbPubSub();
  co_await this->createFsdbDeltaPublisher();

  XLOG(INFO, "Publish init InterfaceMap");
  auto interfaceMap =
      this->createInterfaceMap(kCidrNbrV6.str(), kPortNum, kInterfaceId1, true);
  this->publishInterfaceMapDelta(interfaceMap);
  co_await folly::coro::sleepReturnEarlyOnCancel(3s);
  XLOG(INFO, "Publish updated InterfaceMap Delta");
  interfaceMap.at(kInterfaceId1)
      .ndpTable()[kCidrNbrV6.str()]
      .portId()
      ->portId() = 0;
  this->publishInterfaceMapDelta(interfaceMap);

  co_await folly::coro::sleepReturnEarlyOnCancel(3s);
  // Verify that we enqueue the nbr_down message for PeerManger
  CO_ASSERT_EQ(this->neighborEventQ_.size(), 1);

  // Verify contents of the message
  auto msg = facebook::bgp::test::boundedBlockingPop(
      this->neighborEventQ_, "neighborEventQ_");
  EXPECT_FALSE(std::get<NeighborEventMsg>(msg).isUp);
  EXPECT_EQ(std::get<NeighborEventMsg>(msg).nbrAddr.str(), kCidrNbrV6.str());

  // Cleanup
  this->cleanUpFsdbPubSub();
  this->nbrWatcher_->stop();
  EXPECT_FALSE(this->fsdbCfgWatcher_->fsdbSubscribed_);
  neighborWatcherThread.join();
}

/*
 * Unit tests for getNbrEntryChanges() function
 * Test: Entry deleted (present in old with non-zero port, missing in new)
 */
TYPED_TEST(NeighborWatcherTestFixture, GetNbrEntryChanges_EntryDeleted) {
  std::map<std::string, fboss::state::NeighborEntryFields> oldNbrEntry;
  std::map<std::string, fboss::state::NeighborEntryFields> newNbrEntry;
  std::vector<folly::IPAddress> deletedAddrs;
  std::vector<folly::IPAddress> addedAddrs;

  // Add an entry to old state with non-zero port
  fboss::state::NeighborEntryFields oldEntry;
  oldEntry.ipaddress() = "10.0.0.1";
  oldEntry.portId()->portId() = 100;
  oldNbrEntry["10.0.0.1"] = oldEntry;

  // New state is empty (entry deleted)
  this->fsdbNbrWatcher_->getNbrEntryChanges(
      oldNbrEntry, newNbrEntry, deletedAddrs, addedAddrs);

  EXPECT_EQ(deletedAddrs.size(), 1);
  EXPECT_EQ(deletedAddrs[0].str(), "10.0.0.1");
  EXPECT_TRUE(addedAddrs.empty());
}

// Test: Entry becomes unresolved (port changes from non-zero to 0)
TYPED_TEST(
    NeighborWatcherTestFixture,
    GetNbrEntryChanges_EntryBecomesUnresolved) {
  std::map<std::string, fboss::state::NeighborEntryFields> oldNbrEntry;
  std::map<std::string, fboss::state::NeighborEntryFields> newNbrEntry;
  std::vector<folly::IPAddress> deletedAddrs;
  std::vector<folly::IPAddress> addedAddrs;

  // Add an entry to old state with non-zero port (resolved)
  fboss::state::NeighborEntryFields oldEntry;
  oldEntry.ipaddress() = "10.0.0.1";
  oldEntry.portId()->portId() = 100;
  oldNbrEntry["10.0.0.1"] = oldEntry;

  // Same entry in new state but with port 0 (unresolved)
  fboss::state::NeighborEntryFields newEntry;
  newEntry.ipaddress() = "10.0.0.1";
  newEntry.portId()->portId() = 0;
  newNbrEntry["10.0.0.1"] = newEntry;

  this->fsdbNbrWatcher_->getNbrEntryChanges(
      oldNbrEntry, newNbrEntry, deletedAddrs, addedAddrs);

  EXPECT_EQ(deletedAddrs.size(), 1);
  EXPECT_EQ(deletedAddrs[0].str(), "10.0.0.1");
  EXPECT_TRUE(addedAddrs.empty());
}

// Test: Entry added (missing in old, present in new with non-zero port)
TYPED_TEST(NeighborWatcherTestFixture, GetNbrEntryChanges_EntryAdded) {
  std::map<std::string, fboss::state::NeighborEntryFields> oldNbrEntry;
  std::map<std::string, fboss::state::NeighborEntryFields> newNbrEntry;
  std::vector<folly::IPAddress> deletedAddrs;
  std::vector<folly::IPAddress> addedAddrs;

  /*
   * Old state is empty
   * Add an entry to new state with non-zero port
   */
  fboss::state::NeighborEntryFields newEntry;
  newEntry.ipaddress() = "10.0.0.1";
  newEntry.portId()->portId() = 100;
  newNbrEntry["10.0.0.1"] = newEntry;

  this->fsdbNbrWatcher_->getNbrEntryChanges(
      oldNbrEntry, newNbrEntry, deletedAddrs, addedAddrs);

  EXPECT_TRUE(deletedAddrs.empty());
  EXPECT_EQ(addedAddrs.size(), 1);
  EXPECT_EQ(addedAddrs[0].str(), "10.0.0.1");
}

// Test: Entry becomes resolved (port changes from 0 to non-zero)
TYPED_TEST(
    NeighborWatcherTestFixture,
    GetNbrEntryChanges_EntryBecomesResolved) {
  std::map<std::string, fboss::state::NeighborEntryFields> oldNbrEntry;
  std::map<std::string, fboss::state::NeighborEntryFields> newNbrEntry;
  std::vector<folly::IPAddress> deletedAddrs;
  std::vector<folly::IPAddress> addedAddrs;

  // Add an entry to old state with port 0 (unresolved)
  fboss::state::NeighborEntryFields oldEntry;
  oldEntry.ipaddress() = "10.0.0.1";
  oldEntry.portId()->portId() = 0;
  oldNbrEntry["10.0.0.1"] = oldEntry;

  // Same entry in new state but with non-zero port (resolved)
  fboss::state::NeighborEntryFields newEntry;
  newEntry.ipaddress() = "10.0.0.1";
  newEntry.portId()->portId() = 100;
  newNbrEntry["10.0.0.1"] = newEntry;

  this->fsdbNbrWatcher_->getNbrEntryChanges(
      oldNbrEntry, newNbrEntry, deletedAddrs, addedAddrs);

  EXPECT_TRUE(deletedAddrs.empty());
  EXPECT_EQ(addedAddrs.size(), 1);
  EXPECT_EQ(addedAddrs[0].str(), "10.0.0.1");
}

// Test: Link-local addresses are ignored
TYPED_TEST(NeighborWatcherTestFixture, GetNbrEntryChanges_LinkLocalIgnored) {
  std::map<std::string, fboss::state::NeighborEntryFields> oldNbrEntry;
  std::map<std::string, fboss::state::NeighborEntryFields> newNbrEntry;
  std::vector<folly::IPAddress> deletedAddrs;
  std::vector<folly::IPAddress> addedAddrs;

  // Add link-local IPv6 to old state with non-zero port
  fboss::state::NeighborEntryFields oldEntry;
  oldEntry.ipaddress() = "fe80::1";
  oldEntry.portId()->portId() = 100;
  oldNbrEntry["fe80::1"] = oldEntry;

  // Add link-local IPv6 to new state with non-zero port
  fboss::state::NeighborEntryFields newEntry;
  newEntry.ipaddress() = "fe80::2";
  newEntry.portId()->portId() = 100;
  newNbrEntry["fe80::2"] = newEntry;

  this->fsdbNbrWatcher_->getNbrEntryChanges(
      oldNbrEntry, newNbrEntry, deletedAddrs, addedAddrs);

  // Both should be ignored since they are link-local
  EXPECT_TRUE(deletedAddrs.empty());
  EXPECT_TRUE(addedAddrs.empty());
}

/*
 * Test: Entry with port=0 in old that gets removed should NOT be in
 * deletedAddrs
 */
TYPED_TEST(
    NeighborWatcherTestFixture,
    GetNbrEntryChanges_UnresolvedEntryRemoved) {
  std::map<std::string, fboss::state::NeighborEntryFields> oldNbrEntry;
  std::map<std::string, fboss::state::NeighborEntryFields> newNbrEntry;
  std::vector<folly::IPAddress> deletedAddrs;
  std::vector<folly::IPAddress> addedAddrs;

  // Add an entry to old state with port 0 (unresolved)
  fboss::state::NeighborEntryFields oldEntry;
  oldEntry.ipaddress() = "10.0.0.1";
  oldEntry.portId()->portId() = 0;
  oldNbrEntry["10.0.0.1"] = oldEntry;

  // New state is empty (entry removed, but was unresolved anyway)
  this->fsdbNbrWatcher_->getNbrEntryChanges(
      oldNbrEntry, newNbrEntry, deletedAddrs, addedAddrs);

  // Entry should NOT be in deletedAddrs since it was unresolved
  EXPECT_TRUE(deletedAddrs.empty());
  EXPECT_TRUE(addedAddrs.empty());
}

// Test: IPv6 addresses work correctly
TYPED_TEST(NeighborWatcherTestFixture, GetNbrEntryChanges_IPv6Addresses) {
  std::map<std::string, fboss::state::NeighborEntryFields> oldNbrEntry;
  std::map<std::string, fboss::state::NeighborEntryFields> newNbrEntry;
  std::vector<folly::IPAddress> deletedAddrs;
  std::vector<folly::IPAddress> addedAddrs;

  // Add an IPv6 entry to old state with non-zero port
  fboss::state::NeighborEntryFields oldEntry;
  oldEntry.ipaddress() = "2401:db00:e01e:2004::1";
  oldEntry.portId()->portId() = 100;
  oldNbrEntry["2401:db00:e01e:2004::1"] = oldEntry;

  // New state is empty (entry deleted)
  this->fsdbNbrWatcher_->getNbrEntryChanges(
      oldNbrEntry, newNbrEntry, deletedAddrs, addedAddrs);

  EXPECT_EQ(deletedAddrs.size(), 1);
  EXPECT_EQ(deletedAddrs[0].str(), "2401:db00:e01e:2004::1");
  EXPECT_TRUE(addedAddrs.empty());
}

// Test: Multiple entries with mixed changes
TYPED_TEST(NeighborWatcherTestFixture, GetNbrEntryChanges_MultipleEntries) {
  std::map<std::string, fboss::state::NeighborEntryFields> oldNbrEntry;
  std::map<std::string, fboss::state::NeighborEntryFields> newNbrEntry;
  std::vector<folly::IPAddress> deletedAddrs;
  std::vector<folly::IPAddress> addedAddrs;

  // Entry 1: Will be deleted (present in old, missing in new)
  fboss::state::NeighborEntryFields entry1;
  entry1.ipaddress() = "10.0.0.1";
  entry1.portId()->portId() = 100;
  oldNbrEntry["10.0.0.1"] = entry1;

  // Entry 2: Will become unresolved (port changes from non-zero to 0)
  fboss::state::NeighborEntryFields entry2Old;
  entry2Old.ipaddress() = "10.0.0.2";
  entry2Old.portId()->portId() = 100;
  oldNbrEntry["10.0.0.2"] = entry2Old;

  fboss::state::NeighborEntryFields entry2New;
  entry2New.ipaddress() = "10.0.0.2";
  entry2New.portId()->portId() = 0;
  newNbrEntry["10.0.0.2"] = entry2New;

  // Entry 3: Will be added (missing in old, present in new)
  fboss::state::NeighborEntryFields entry3;
  entry3.ipaddress() = "10.0.0.3";
  entry3.portId()->portId() = 100;
  newNbrEntry["10.0.0.3"] = entry3;

  // Entry 4: Will become resolved (port changes from 0 to non-zero)
  fboss::state::NeighborEntryFields entry4Old;
  entry4Old.ipaddress() = "10.0.0.4";
  entry4Old.portId()->portId() = 0;
  oldNbrEntry["10.0.0.4"] = entry4Old;

  fboss::state::NeighborEntryFields entry4New;
  entry4New.ipaddress() = "10.0.0.4";
  entry4New.portId()->portId() = 100;
  newNbrEntry["10.0.0.4"] = entry4New;

  // Entry 5: Unchanged (same non-zero port in both)
  fboss::state::NeighborEntryFields entry5;
  entry5.ipaddress() = "10.0.0.5";
  entry5.portId()->portId() = 100;
  oldNbrEntry["10.0.0.5"] = entry5;
  newNbrEntry["10.0.0.5"] = entry5;

  this->fsdbNbrWatcher_->getNbrEntryChanges(
      oldNbrEntry, newNbrEntry, deletedAddrs, addedAddrs);

  // Entries 1 and 2 should be in deletedAddrs
  EXPECT_EQ(deletedAddrs.size(), 2);
  std::set<std::string> deletedSet;
  for (const auto& addr : deletedAddrs) {
    deletedSet.insert(addr.str());
  }
  EXPECT_TRUE(deletedSet.count("10.0.0.1"));
  EXPECT_TRUE(deletedSet.count("10.0.0.2"));

  // Entries 3 and 4 should be in addedAddrs
  EXPECT_EQ(addedAddrs.size(), 2);
  std::set<std::string> addedSet;
  for (const auto& addr : addedAddrs) {
    addedSet.insert(addr.str());
  }
  EXPECT_TRUE(addedSet.count("10.0.0.3"));
  EXPECT_TRUE(addedSet.count("10.0.0.4"));
}

// Test: Entry with port=0 in both states (no change)
TYPED_TEST(NeighborWatcherTestFixture, GetNbrEntryChanges_UnresolvedInBoth) {
  std::map<std::string, fboss::state::NeighborEntryFields> oldNbrEntry;
  std::map<std::string, fboss::state::NeighborEntryFields> newNbrEntry;
  std::vector<folly::IPAddress> deletedAddrs;
  std::vector<folly::IPAddress> addedAddrs;

  // Entry with port 0 in both states
  fboss::state::NeighborEntryFields entry;
  entry.ipaddress() = "10.0.0.1";
  entry.portId()->portId() = 0;
  oldNbrEntry["10.0.0.1"] = entry;
  newNbrEntry["10.0.0.1"] = entry;

  this->fsdbNbrWatcher_->getNbrEntryChanges(
      oldNbrEntry, newNbrEntry, deletedAddrs, addedAddrs);

  // No changes should be detected
  EXPECT_TRUE(deletedAddrs.empty());
  EXPECT_TRUE(addedAddrs.empty());
}

// Test: Entry with non-zero port in both states (no change)
TYPED_TEST(NeighborWatcherTestFixture, GetNbrEntryChanges_ResolvedInBoth) {
  std::map<std::string, fboss::state::NeighborEntryFields> oldNbrEntry;
  std::map<std::string, fboss::state::NeighborEntryFields> newNbrEntry;
  std::vector<folly::IPAddress> deletedAddrs;
  std::vector<folly::IPAddress> addedAddrs;

  // Entry with non-zero port in both states
  fboss::state::NeighborEntryFields entry;
  entry.ipaddress() = "10.0.0.1";
  entry.portId()->portId() = 100;
  oldNbrEntry["10.0.0.1"] = entry;
  newNbrEntry["10.0.0.1"] = entry;

  this->fsdbNbrWatcher_->getNbrEntryChanges(
      oldNbrEntry, newNbrEntry, deletedAddrs, addedAddrs);

  // No changes should be detected
  EXPECT_TRUE(deletedAddrs.empty());
  EXPECT_TRUE(addedAddrs.empty());
}

// Test: New entry with port=0 should not be added
TYPED_TEST(
    NeighborWatcherTestFixture,
    GetNbrEntryChanges_NewUnresolvedEntryNotAdded) {
  std::map<std::string, fboss::state::NeighborEntryFields> oldNbrEntry;
  std::map<std::string, fboss::state::NeighborEntryFields> newNbrEntry;
  std::vector<folly::IPAddress> deletedAddrs;
  std::vector<folly::IPAddress> addedAddrs;

  // New entry with port 0 (unresolved)
  fboss::state::NeighborEntryFields newEntry;
  newEntry.ipaddress() = "10.0.0.1";
  newEntry.portId()->portId() = 0;
  newNbrEntry["10.0.0.1"] = newEntry;

  this->fsdbNbrWatcher_->getNbrEntryChanges(
      oldNbrEntry, newNbrEntry, deletedAddrs, addedAddrs);

  // Entry should NOT be in addedAddrs since it's unresolved
  EXPECT_TRUE(deletedAddrs.empty());
  EXPECT_TRUE(addedAddrs.empty());
}

// S661800 repro: portId != 0 with state == Pending must classify as unresolved.
TYPED_TEST(
    NeighborWatcherTestFixture,
    GetNbrEntryChanges_PortIdSetButStatePending) {
  std::map<std::string, fboss::state::NeighborEntryFields> oldNbrEntry;
  std::map<std::string, fboss::state::NeighborEntryFields> newNbrEntry;
  std::vector<folly::IPAddress> deletedAddrs;
  std::vector<folly::IPAddress> addedAddrs;

  fboss::state::NeighborEntryFields newEntry;
  newEntry.ipaddress() = "10.0.0.1";
  newEntry.portId()->portId() = 100;
  newEntry.state() = fboss::state::NeighborState::Pending;
  newNbrEntry["10.0.0.1"] = newEntry;

  this->fsdbNbrWatcher_->getNbrEntryChanges(
      oldNbrEntry, newNbrEntry, deletedAddrs, addedAddrs);

  EXPECT_TRUE(deletedAddrs.empty());
  EXPECT_TRUE(addedAddrs.empty());
}

// Test: state transitions Reachable -> Pending must be reported as deleted
TYPED_TEST(NeighborWatcherTestFixture, GetNbrEntryChanges_StateBecomesPending) {
  std::map<std::string, fboss::state::NeighborEntryFields> oldNbrEntry;
  std::map<std::string, fboss::state::NeighborEntryFields> newNbrEntry;
  std::vector<folly::IPAddress> deletedAddrs;
  std::vector<folly::IPAddress> addedAddrs;

  fboss::state::NeighborEntryFields oldEntry;
  oldEntry.ipaddress() = "10.0.0.1";
  oldEntry.portId()->portId() = 100;
  oldEntry.state() = fboss::state::NeighborState::Reachable;
  oldNbrEntry["10.0.0.1"] = oldEntry;

  fboss::state::NeighborEntryFields newEntry;
  newEntry.ipaddress() = "10.0.0.1";
  newEntry.portId()->portId() = 100;
  newEntry.state() = fboss::state::NeighborState::Pending;
  newNbrEntry["10.0.0.1"] = newEntry;

  this->fsdbNbrWatcher_->getNbrEntryChanges(
      oldNbrEntry, newNbrEntry, deletedAddrs, addedAddrs);

  const std::vector<folly::IPAddress> expectedDeleted{
      folly::IPAddress("10.0.0.1")};
  EXPECT_EQ(expectedDeleted, deletedAddrs);
  EXPECT_TRUE(addedAddrs.empty());
}

// Test: state transitions Pending -> Reachable must be reported as added
TYPED_TEST(
    NeighborWatcherTestFixture,
    GetNbrEntryChanges_StateBecomesReachable) {
  std::map<std::string, fboss::state::NeighborEntryFields> oldNbrEntry;
  std::map<std::string, fboss::state::NeighborEntryFields> newNbrEntry;
  std::vector<folly::IPAddress> deletedAddrs;
  std::vector<folly::IPAddress> addedAddrs;

  fboss::state::NeighborEntryFields oldEntry;
  oldEntry.ipaddress() = "10.0.0.1";
  oldEntry.portId()->portId() = 100;
  oldEntry.state() = fboss::state::NeighborState::Pending;
  oldNbrEntry["10.0.0.1"] = oldEntry;

  fboss::state::NeighborEntryFields newEntry;
  newEntry.ipaddress() = "10.0.0.1";
  newEntry.portId()->portId() = 100;
  newEntry.state() = fboss::state::NeighborState::Reachable;
  newNbrEntry["10.0.0.1"] = newEntry;

  this->fsdbNbrWatcher_->getNbrEntryChanges(
      oldNbrEntry, newNbrEntry, deletedAddrs, addedAddrs);

  const std::vector<folly::IPAddress> expectedAdded{
      folly::IPAddress("10.0.0.1")};
  EXPECT_TRUE(deletedAddrs.empty());
  EXPECT_EQ(expectedAdded, addedAddrs);
}

/*
 * Only entries with both portId != 0 AND state == Reachable count as resolved;
 * mismatched entries bump the bgpd.neighbor.portid_state_mismatch counter.
 */
TYPED_TEST(
    NeighborWatcherTestFixture,
    CollectResolvedIpsFromTable_FiltersPendingEntries) {
  facebook::bgp::BgpStats::initCounters();
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();
  tcData->publishStats();
  const auto initialCount =
      tcData->getCounter(facebook::bgp::BgpStats::kNeighborPortIdStateMismatch);

  std::map<std::string, fboss::state::NeighborEntryFields> nbrTable;

  fboss::state::NeighborEntryFields portZeroReachable;
  portZeroReachable.ipaddress() = "10.0.0.1";
  portZeroReachable.portId()->portId() = 0;
  portZeroReachable.state() = fboss::state::NeighborState::Reachable;
  nbrTable["10.0.0.1"] = portZeroReachable;

  fboss::state::NeighborEntryFields portSetPending;
  portSetPending.ipaddress() = "10.0.0.2";
  portSetPending.portId()->portId() = 100;
  portSetPending.state() = fboss::state::NeighborState::Pending;
  nbrTable["10.0.0.2"] = portSetPending;

  fboss::state::NeighborEntryFields portSetReachable;
  portSetReachable.ipaddress() = "10.0.0.3";
  portSetReachable.portId()->portId() = 100;
  portSetReachable.state() = fboss::state::NeighborState::Reachable;
  nbrTable["10.0.0.3"] = portSetReachable;

  folly::F14FastSet<folly::IPAddress> resolvedIps;
  FsdbNeighborWatcher::collectResolvedIpsFromTable(nbrTable, resolvedIps);

  const folly::F14FastSet<folly::IPAddress> expected{
      folly::IPAddress("10.0.0.3")};
  EXPECT_EQ(expected, resolvedIps);

  /*
   * Two of the three entries above have portId/state mismatch (10.0.0.1 and
   * 10.0.0.2), so the counter should bump by exactly 2.
   */
  tcData->publishStats();
  EXPECT_EQ(
      initialCount + 2,
      tcData->getCounter(
          facebook::bgp::BgpStats::kNeighborPortIdStateMismatch));
}

/*
 * Unit tests for processInterfaceMapChanges() function
 * Test: InterfaceId present in old map but missing in new map - entries should
 * be deleted
 */
TYPED_TEST(
    NeighborWatcherTestFixture,
    ProcessInterfaceMapChanges_InterfaceIdRemovedFromNewMap) {
  std::string ipaddr = "10.0.0.1";

  // Set up old interfaceMap with two interfaces
  auto oldInterfaceMap =
      this->createInterfaceMap(ipaddr, kPortNum, kInterfaceId1, false);
  // Add a second interface to old map
  fboss::state::NeighborEntryFields nef2;
  nef2.ipaddress() = "10.0.0.2";
  nef2.portId()->portId() = kPortNum;
  std::map<std::string, fboss::state::NeighborEntryFields> arpTable2;
  arpTable2["10.0.0.2"] = nef2;
  fboss::state::InterfaceFields interfaceFields2;
  interfaceFields2.interfaceId() = kInterfaceId2;
  interfaceFields2.arpTable() = arpTable2;
  interfaceFields2.ndpTable() = {};
  oldInterfaceMap[kInterfaceId2] = interfaceFields2;

  // First, set up the old interfaceMap by calling fsdbInterfaceStateCb
  this->callFsdbInterfaceStateCbAndDrain(oldInterfaceMap);

  // Drain any messages from first update
  while (!this->ribInQ_.empty()) {
    facebook::bgp::test::boundedBlockingPop(this->ribInQ_, "ribInQ_");
  }

  /*
   * Now create new interfaceMap with only kInterfaceId1 (kInterfaceId2 is
   * removed)
   */
  auto newInterfaceMap =
      this->createInterfaceMap(ipaddr, kPortNum, kInterfaceId1, false);

  // Process the change - kInterfaceId2's entry should be deleted
  this->callFsdbInterfaceStateCbAndDrain(newInterfaceMap);

  // Verify that the entry from interfaceId2 is marked as deleted
  ASSERT_EQ(this->neighborEventQ_.size(), 1);
  auto msg = facebook::bgp::test::boundedBlockingPop(
      this->neighborEventQ_, "neighborEventQ_");
  EXPECT_FALSE(std::get<NeighborEventMsg>(msg).isUp);
  EXPECT_EQ(std::get<NeighborEventMsg>(msg).nbrAddr.str(), "10.0.0.2");

  // Verify that the entry from interfaceId2 is marked as deleted in ribInQ_
  ASSERT_EQ(this->ribInQ_.size(), 1);
  auto ribMsg =
      facebook::bgp::test::boundedBlockingPop(this->ribInQ_, "ribInQ_");
  EXPECT_EQ(std::get<NexthopResolutionUpdate>(ribMsg).unresolved.size(), 1);
  EXPECT_EQ(
      std::get<NexthopResolutionUpdate>(ribMsg).unresolved[0].str(),
      "10.0.0.2");
}

/*
 * Test: InterfaceId present in new map but missing in old map - entries should
 * be added
 */
TYPED_TEST(
    NeighborWatcherTestFixture,
    ProcessInterfaceMapChanges_InterfaceIdAddedInNewMap) {
  std::string ipaddr = "10.0.0.1";

  // Set up old interfaceMap with only kInterfaceId1
  auto oldInterfaceMap =
      this->createInterfaceMap(ipaddr, kPortNum, kInterfaceId1, false);

  // First, set up the old interfaceMap by calling fsdbInterfaceStateCb
  this->callFsdbInterfaceStateCbAndDrain(oldInterfaceMap);

  // Drain any messages from first update
  while (!this->ribInQ_.empty()) {
    facebook::bgp::test::boundedBlockingPop(this->ribInQ_, "ribInQ_");
  }

  // Now create new interfaceMap with both kInterfaceId1 and kInterfaceId2
  auto newInterfaceMap =
      this->createInterfaceMap(ipaddr, kPortNum, kInterfaceId1, false);
  // Add a new kInterfaceId2 to new map
  fboss::state::NeighborEntryFields nef2;
  nef2.ipaddress() = "10.0.0.2";
  nef2.portId()->portId() = kPortNum;
  std::map<std::string, fboss::state::NeighborEntryFields> arpTable2;
  arpTable2["10.0.0.2"] = nef2;
  fboss::state::InterfaceFields interfaceFields2;
  interfaceFields2.interfaceId() = kInterfaceId2;
  interfaceFields2.arpTable() = arpTable2;
  interfaceFields2.ndpTable() = {};
  newInterfaceMap[kInterfaceId2] = interfaceFields2;

  // Process the change - kInterfaceId2's entry should be added
  this->callFsdbInterfaceStateCbAndDrain(newInterfaceMap);

  ASSERT_TRUE(this->neighborEventQ_.empty());

  // Verify that the entry from kInterfaceId2 is marked as added in ribInQ_
  ASSERT_EQ(this->ribInQ_.size(), 1);
  auto ribMsg =
      facebook::bgp::test::boundedBlockingPop(this->ribInQ_, "ribInQ_");
  EXPECT_EQ(std::get<NexthopResolutionUpdate>(ribMsg).resolved.size(), 1);
  EXPECT_EQ(
      std::get<NexthopResolutionUpdate>(ribMsg).resolved[0].str(), "10.0.0.2");
}

/*
 * Test: First interfaceMap update (interfaceMap_ is null) - all resolved
 * entries should be added
 */
TYPED_TEST(
    NeighborWatcherTestFixture,
    ProcessInterfaceMapChanges_FirstInterfaceMapUpdate) {
  std::string ipaddr = "10.0.0.1";

  /*
   * Ensure interfaceMap_ is null (fresh FsdbNeighborWatcher)
   * The fsdbNbrWatcher_ is already initialized with interfaceMap_ = nullptr
   */

  // Create interfaceMap with a resolved entry
  auto interfaceMap =
      this->createInterfaceMap(ipaddr, kPortNum, kInterfaceId1, false);

  // Process the first update - entry should be added
  this->callFsdbInterfaceStateCbAndDrain(interfaceMap);

  ASSERT_TRUE(this->neighborEventQ_.empty());

  // Verify that the entry is marked as added
  ASSERT_EQ(this->ribInQ_.size(), 1);
  auto ribMsg =
      facebook::bgp::test::boundedBlockingPop(this->ribInQ_, "ribInQ_");
  EXPECT_EQ(std::get<NexthopResolutionUpdate>(ribMsg).resolved.size(), 1);
  EXPECT_EQ(
      std::get<NexthopResolutionUpdate>(ribMsg).resolved[0].str(), "10.0.0.1");
}

/*
 * Test: Verify NexthopResolutionUpdate is pushed to ribInQ when interface map
 * changes
 */
TYPED_TEST(
    NeighborWatcherTestFixture,
    ProcessInterfaceMapChanges_NexthopResolutionUpdatePushedToRibInQ) {
  std::string ipaddr1 = "10.0.0.1";
  std::string ipaddr2 = "10.0.0.2";

  // Set up old interfaceMap with kInterfaceId1 (ipaddr1 is resolved)
  auto oldInterfaceMap =
      this->createInterfaceMap(ipaddr1, kPortNum, kInterfaceId1, false);

  // First, set up the old interfaceMap by calling fsdbInterfaceStateCb
  this->callFsdbInterfaceStateCbAndDrain(oldInterfaceMap);

  // Drain any messages from first update
  while (!this->neighborEventQ_.empty()) {
    facebook::bgp::test::boundedBlockingPop(
        this->neighborEventQ_, "neighborEventQ_");
  }
  while (!this->ribInQ_.empty()) {
    facebook::bgp::test::boundedBlockingPop(this->ribInQ_, "ribInQ_");
  }

  /*
   * Now create new interfaceMap:
   * - kInterfaceId1 removed (ipaddr1 becomes unresolved - goes to deletedAddrs)
   * - kInterfaceId2 added with ipaddr2 resolved (goes to addedAddrs)
   */
  auto newInterfaceMap =
      this->createInterfaceMap(ipaddr2, kPortNum, kInterfaceId2, false);

  // Process the change
  this->callFsdbInterfaceStateCbAndDrain(newInterfaceMap);

  /*
   * Verify NexthopResolutionUpdate is pushed to ribInQ_
   * The queue should have one message: NexthopResolutionUpdate
   */
  ASSERT_FALSE(this->ribInQ_.empty());
  auto ribMsg =
      facebook::bgp::test::boundedBlockingPop(this->ribInQ_, "ribInQ_");

  // Verify the message is NexthopResolutionUpdate with correct values
  auto* nexthopUpdate = std::get_if<NexthopResolutionUpdate>(&ribMsg);
  ASSERT_NE(nexthopUpdate, nullptr);

  // ipaddr2 should be in resolved (added)
  ASSERT_EQ(nexthopUpdate->resolved.size(), 1);
  EXPECT_EQ(nexthopUpdate->resolved[0].str(), ipaddr2);

  // ipaddr1 should be in unresolved (deleted due to interface removal)
  ASSERT_EQ(nexthopUpdate->unresolved.size(), 1);
  EXPECT_EQ(nexthopUpdate->unresolved[0].str(), ipaddr1);
}

/*
 * Test: Verify NexthopResolutionUpdate is NOT pushed to ribInQ when there are
 * no changes (both addedAddrs and deletedAddrs are empty)
 */
TYPED_TEST(
    NeighborWatcherTestFixture,
    ProcessInterfaceMapChanges_NoUpdateSkipsRibInQPush) {
  std::string ipaddr = "10.0.0.1";

  // Create interfaceMap with a resolved entry
  auto interfaceMap =
      this->createInterfaceMap(ipaddr, kPortNum, kInterfaceId1, false);

  // First, set up the initial interfaceMap by calling fsdbInterfaceStateCb
  this->callFsdbInterfaceStateCbAndDrain(interfaceMap);

  // Drain any messages from first update
  while (!this->neighborEventQ_.empty()) {
    facebook::bgp::test::boundedBlockingPop(
        this->neighborEventQ_, "neighborEventQ_");
  }
  while (!this->ribInQ_.empty()) {
    facebook::bgp::test::boundedBlockingPop(this->ribInQ_, "ribInQ_");
  }

  // Now call fsdbInterfaceStateCb with the same interfaceMap (no changes)
  this->callFsdbInterfaceStateCbAndDrain(interfaceMap);

  // Verify that no messages were pushed to neighborEventQ_
  EXPECT_TRUE(this->neighborEventQ_.empty());

  /*
   * Verify that no NexthopResolutionUpdate is pushed to ribInQ_
   * since there are no changes (addedAddrs and deletedAddrs are both empty)
   */
  EXPECT_TRUE(this->ribInQ_.empty());
}

/*
 * Verify that interfaceMap_ cache is always updated even when there are
 * no nexthop resolution changes (no push to ribInQ_). Previously, the
 * coroutine returned early when no changes were detected, skipping the
 * cache update. This caused subsequent calls to diff against stale state.
 */
TYPED_TEST(
    NeighborWatcherTestFixture,
    ProcessInterfaceMapChanges_CacheUpdatedEvenWithNoChanges) {
  std::string ipaddr = "10.0.0.1";

  // Create interfaceMap with a resolved entry
  auto interfaceMap =
      this->createInterfaceMap(ipaddr, kPortNum, kInterfaceId1, false);

  // Set up initial state
  this->callFsdbInterfaceStateCbAndDrain(interfaceMap);

  // Drain messages from first update
  while (!this->ribInQ_.empty()) {
    facebook::bgp::test::boundedBlockingPop(this->ribInQ_, "ribInQ_");
  }

  // Call again with the same interfaceMap (no changes)
  this->callFsdbInterfaceStateCbAndDrain(interfaceMap);

  // Verify no push to ribInQ_ (no changes detected)
  EXPECT_TRUE(this->ribInQ_.empty());

  // Verify interfaceMap_ cache was still updated (not stale)
  ASSERT_NE(this->fsdbNbrWatcher_->interfaceMap_, nullptr);
  EXPECT_EQ(this->fsdbNbrWatcher_->interfaceMap_->size(), 1);
  EXPECT_TRUE(this->fsdbNbrWatcher_->interfaceMap_->contains(kInterfaceId1));
}

} // namespace facebook::bgp
