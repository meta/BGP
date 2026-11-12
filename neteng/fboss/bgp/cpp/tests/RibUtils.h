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

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <gmock/gmock.h>

#include <folly/IPAddress.h>
#include <folly/io/async/ScopedEventBaseThread.h>
#include <folly/testing/TestUtil.h>

#include "configerator/structs/neteng/config/gen-cpp2/routing_policy_types.h"
#include "configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h"
#include "configerator/structs/neteng/fboss/bgp/if/gen-cpp2/bgp_attr_types.h"
#include "neteng/fboss/bgp/cpp/BgpServiceDC.h"
#include "neteng/fboss/bgp/cpp/common/BgpPath.h"
#include "neteng/fboss/bgp/cpp/common/Structs.h"
#include "neteng/fboss/bgp/cpp/config/ConfigManager.h"
#include "neteng/fboss/bgp/cpp/fsdb/FsdbSyncer.h"
#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"
#include "neteng/fboss/bgp/cpp/rib/Fib.h"
#include "neteng/fboss/bgp/cpp/rib/RibBase.h"
#include "neteng/fboss/bgp/cpp/rib/RibDC.h"
#include "neteng/fboss/bgp/cpp/rib/RibPolicy.h"
#include "neteng/fboss/bgp/cpp/tests/MockPeerManager.h"
#include "neteng/fboss/bgp/cpp/tests/MockSessionManager.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"
#include "neteng/fboss/bgp/cpp/watchdog/Watchdog.h"
#include "neteng/fboss/bgp/if/gen-cpp2/BgpStructs_types.h"

#include "fboss/fsdb/client/FsdbPubSubManager.h"
#include "fboss/fsdb/client/FsdbSubscriber.h"
#include "fboss/fsdb/tests/utils/FsdbTestServer.h"
#include "fboss/fsdb/tests/utils/FsdbTestSubscriber.h"

namespace facebook {
namespace bgp {

std::vector<facebook::neteng::fboss::bgp_attr::TBgpCommunity>
createTBgpCommunities(const std::vector<std::string>& communitiesStr);

std::vector<facebook::neteng::fboss::bgp_attr::TAsPathSeg> createTAsPath(
    const std::vector<std::pair<int, std::vector<int64_t>>>& segments);

std::vector<facebook::nettools::bgplib::BgpAttrAsPathSegmentC>
createBgpAttrAsPathSegmentCV(
    const std::vector<std::pair<int, std::vector<uint32_t>>>& segments);

std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>
getLocalRoutesWithOldAsField();

std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>
getDefaultLocalRoutes();

// This macro helps to identify failure location easily using SCOPED_TRACE.
#define verifyAttributes(             \
    attrs,                            \
    expectedNexthop,                  \
    expectedOrigin,                   \
    expectedLocalPref,                \
    expectedCommunitySet,             \
    expectedAsPath)                   \
  {                                   \
    SCOPED_TRACE("verifyAttributes"); \
    verifyAttributesHelper(           \
        attrs,                        \
        expectedNexthop,              \
        expectedOrigin,               \
        expectedLocalPref,            \
        expectedCommunitySet,         \
        expectedAsPath);              \
  }

void verifyAttributesHelper(
    std::shared_ptr<const BgpPath> attrs,
    const folly::IPAddress& expectedNexthop,
    const facebook::nettools::bgplib::BgpAttrOrigin& expectedOrigin,
    const uint32_t expectedLocalPref,
    const std::vector<nettools::bgplib::BgpAttrCommunityC>&
        expectedCommunitySet,
    const std::vector<nettools::bgplib::BgpAttrAsPathSegmentC>& expectedAsPath);

class MockFib : public Fib {
 public:
  using NexthopInfoMap =
      folly::F14NodeMap<folly::IPAddress, facebook::bgp::NexthopInfo>;

  MockFib(
      folly::EventBase* evb,
      folly::coro::CancellableAsyncScope& asyncScope,
      Fib::FibMessageQueue& toRibQ)
      : evb_(evb), asyncScope_(asyncScope), toRibQ_(toRibQ) {}

  MOCK_METHOD(
      void,
      updateUnicastRoute_,
      (const folly::CIDRNetwork&,
       std::shared_ptr<const BgpPath>,
       std::shared_ptr<const WeightedNexthopMap>,
       const bool,
       const bool,
       const NexthopInfoMap&));

  MOCK_METHOD(
      void,
      updateUnicastRoute_,
      (const folly::CIDRNetwork&,
       std::shared_ptr<const BgpPath>,
       std::shared_ptr<const WeightedNexthopMap>,
       const bool,
       const bool,
       const NexthopInfoMap&,
       const std::optional<uint32_t>&));

  MOCK_METHOD(
      void,
      updateUnicastRoute_,
      (const folly::CIDRNetwork&,
       std::shared_ptr<const BgpPath>,
       std::shared_ptr<const WeightedNexthopMap>,
       const bool,
       const bool,
       const NexthopInfoMap&,
       const std::optional<uint32_t>&,
       std::shared_ptr<const NexthopTopoInfoMap>));

  MOCK_METHOD(void, program_, (bool));

  MOCK_METHOD(void, stop, ());

  bool isConnected() const override;

  bool isFullSynced() const override;

  /** Stage one mock FIB request and return its normalized form. */
  std::optional<FibOutRoute> updateUnicastRoute(
      const folly::CIDRNetwork& prefix,
      std::shared_ptr<const BgpPath> attrsToBeAdvertised,
      std::shared_ptr<const WeightedNexthopMap> weightedNexthops,
      const bool isLocalRouteBest,
      const bool installToFib,
      const folly::F14NodeMap<folly::IPAddress, facebook::bgp::NexthopInfo>&
          nexthopInfoMap,
      const std::optional<uint32_t>& classId,
      std::shared_ptr<const NexthopTopoInfoMap> nexthopTopoInfoMap,
      const BgpRouteType routeType,
      bool enableFibOutTracking = false) override;

  folly::coro::Task<void> program(bool isSync = false) override;

  /*
   * Return a Future which will be satisfied when Fib finishes program() call.
   * This is only required for unit test purpose.
   */
  folly::Future<folly::Unit> getFibProgramFuture();

  void disconnect();

  void connect();

 private:
  void fulfillFibProgramPromise();

  std::unique_ptr<folly::Promise<folly::Unit>> fibProgramPromise_;
  // Updating the promise is not an atomic operation.
  std::mutex fibProgramPromiseMutex_;

  bool fullSynced_{false};
  bool isConnected_{true};
  FibProgrammedPfxs waitForAck_{};

  folly::EventBase* const evb_;
  folly::coro::CancellableAsyncScope& asyncScope_;
  FibMessageQueue& toRibQ_;

  // One time flag to mark initial full-sync to FIB finished
  bool initialFibSynced_{false};
};

class MockRib : public RibDC {
 public:
  using RibDC::CacheMigrationResult;
  using RibDC::migrateRouteAttributePolicyCache;
  using RibDC::recordPartialDrainTransition;
  using RibDC::RibDC;
  using RibDC::ribPolicyLogger_;

  MOCK_METHOD(void, prepareFibProgramming_, ());

  MOCK_METHOD(
      std::chrono::seconds,
      getFibBackoffTimeout,
      (),
      (const, noexcept, override));

  void prepareFibProgramming(bool fullSync = false) noexcept override;

  void createFib() override;

  void clearRibPolicy() override;

  neteng::fboss::bgp::thrift::TResult setRouteAttributePolicy(
      std::unique_ptr<rib_policy::TRouteAttributePolicy> policy) override;

  void clearRouteAttributePolicy() override;

  neteng::fboss::bgp::thrift::TResult setPathSelectionPolicy(
      std::unique_ptr<rib_policy::TPathSelectionPolicy> policy,
      bool forceUpdate = false) override;

  void clearPathSelectionPolicy() override;

  void setRouteFilterPolicy(
      std::unique_ptr<rib_policy::TRouteFilterPolicy> policy,
      bool forceUpdate = false) override;

  void clearRouteFilterPolicy() override;

  MockFib* getMockFib();

  std::shared_ptr<RouteInfo> getBestPath(const folly::CIDRNetwork& prefix);

  void configureCanonicalRibProducerForTest(
      std::shared_ptr<CanonicalRibUpdateQueue> updateQueue);
  void processSingleRibInUpdateForTest(
      const TinyPeerInfo& peer,
      std::shared_ptr<const BgpPath> attrs,
      const PrefixPathId& prefixPathId);
  void enqueueCanonicalRibFullSnapshotForTest();

  folly::F14NodeMap<uint32_t, std::shared_ptr<RouteInfo>> getMultipath(
      const folly::CIDRNetwork& prefix);

  // get fib batch list snapshot
  folly::F14NodeMap<folly::CIDRNetwork, RibEntry> fibItems;

  /*
   * Return a Future which will be satisfied when Rib finishes programFib()
   * call. This is only required for unit test purpose.
   * numRibEntriesToProgram could be specified which requires the nubmer of
   * rib entires programmed before fulfilling the promise
   * By default, we set it to 0, meaning that we will definitely set the
   * promise next time if ribPrepareFibProgramming is called.
   */
  folly::Future<folly::Unit> getRibPrepareFibProgrammingFuture(
      int numRibEntriesToProgram = 0);

  // Set Rib pause timeout value
  void setRibPauseTime(std::chrono::milliseconds ribPauseTime);

  /*
   * Set Route churn detection thresholds - high and low watermarks and
   * route churn check interval
   */
  void setRouteChurnDetectionThresholds(
      uint64_t highWatermarkForRouteChurn,
      uint64_t lowWatermarkForRouteChurn,
      std::chrono::seconds routeChurncheckInterval);

  /*
   * These functions block until either the respective policy update is
   * complete, or they time out.
   */
  rib_policy::TPathSelectionPolicy waitForPathSelectionPolicyUpdate();
  rib_policy::TRouteAttributePolicy waitForRouteAttributePolicyUpdate();
  rib_policy::TRouteFilterPolicy waitForRouteFilterPolicyUpdate();
  rib_policy::TPathSelectionPolicy waitForPathSelectionPolicyClear();
  rib_policy::TRouteAttributePolicy waitForRouteAttributePolicyClear();
  rib_policy::TRouteFilterPolicy waitForRouteFilterPolicyClear();
  void waitForRibPolicyClear();

  /*
   * Return a Future which will be satisfied when any replaceRibPolicy
   * variant, i.e. replaceRibPolicy, replaceRouteAttributePolicy,
   * replacePathSelectionPolicy replaceRouteFilterPolicy, is called.
   */
  folly::Future<folly::Unit> getRibPolicyReplaceFuture();

 private:
  /*
   * numRibEntriesProgrammed is the number of rib entries that are
   * programmed in this round.
   */
  void fulfillRibPrepareFibProgrammingPromise(int numRibEntriesProgrammed);

  void replaceRibPolicy(
      std::unique_ptr<RibPolicy> newRibPolicy,
      bool isBootstrap = false) override;
  bool replaceRouteAttributePolicy(
      std::unique_ptr<RouteAttributePolicy> newPolicy) override;
  bool replacePathSelectionPolicy(
      std::unique_ptr<PathSelectionPolicy> newPolicy,
      bool isBootstrap = false,
      bool forceUpdate = false) override;
  bool replaceRouteFilterPolicy(
      std::unique_ptr<RouteFilterPolicy> newPolicy,
      bool isBootstrap = false,
      bool forceUpdate = false) override;

  void fulfillRibPolicyReplacePromise();

  /*
   * Waits for a predicate to become true, or until a timeout is reached.
   * Policy updates are done asynchronously, so we need coro tasks to wait
   * for updates to complete.
   */
  folly::coro::Task<bool> co_waitForPredicate(
      const std::function<bool(void)>& pred);
  // Blocking wrapper for co_waitForPredicate.
  bool waitForPredicate(const std::function<bool(void)>& pred);

  /*
   * Use folly::Synchronized to avoid race coditions between the test thread
   * and the rib thread
   */
  folly::Synchronized<std::unique_ptr<folly::Promise<folly::Unit>>>
      ribPrepareFibProgrammingPromise_;
  /*
   * How many rib entires should be programmed before setting value to the
   * promise
   */
  int ribEntriesToProgram_{0};

  /*
   * The promise is fulfilled when any variants of replaceRibPolicy is
   * called
   */
  folly::Synchronized<std::unique_ptr<folly::Promise<folly::Unit>>>
      ribPolicyReplacePromise_;

  friend class RibFixture;
  friend class RibFsdbFixture;

/*
 * per class placeholder for test code injection
 * only need to be setup once here
 */
#ifdef MockRib_TEST_FRIENDS
  MockRib_TEST_FRIENDS
#endif
};

class RibFixture : public testing::Test {
 public:
  /*
   * Methods
   */
  RibFixture() = default;
  ~RibFixture() override = default;

  /** Build the global configuration used by the fixture's RIB. */
  void createGlobalConfig(
      ComputeUcmpFromLbwComm computeUcmpFromLbwComm =
          ComputeUcmpFromLbwComm{true},
      CountConfedsInAsPathLen countConfedsInAsPathLen =
          CountConfedsInAsPathLen{false},
      EnableNexthopTracking enableNexthopTracking =
          EnableNexthopTracking{false},
      bool enableFibOutTracking = false,
      bool enableRibAllocatedPathId = false);

  /** Create the standard peer, RIB, FIB, and service test dependencies. */
  void ribFixtureDefaultSetup(
      ComputeUcmpFromLbwComm computeUcmpFromLbwComm =
          ComputeUcmpFromLbwComm{true},
      CountConfedsInAsPathLen countConfedsInAsPathLen =
          CountConfedsInAsPathLen{false},
      EnableNexthopTracking enableNexthopTracking =
          EnableNexthopTracking{false},
      std::shared_ptr<NexthopCache> nexthopCache = nullptr,
      bool enableFibOutTracking = false,
      bool enableRibAllocatedPathId = false);

  void SetUp() override;
  void TearDown() override;

  std::unique_ptr<MockRib> createMockRib(
      const std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>&
          localRoutes = {},
      const std::optional<bgp_policy::BgpPolicies>& policyConfig = std::nullopt,
      std::shared_ptr<NexthopCache> nexthopCache = nullptr);

  void setUpRibAndFib(
      const std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>&
          localRoutes = {},
      const std::shared_ptr<NexthopCache>& nexthopCache = nullptr);

  void setUpService();

  /**
   * @brief Delayed start of FIB programming timer.
   */
  folly::coro::Task<void> delayedFibProgramSchedule();

  neteng::fboss::bgp::thrift::TResult sendRouteAttributePolicySet(
      rib_policy::TRouteAttributePolicy policy);
  neteng::fboss::bgp::thrift::TResult sendPathSelectionPolicySet(
      rib_policy::TPathSelectionPolicy policy);

  void sendRouteFilterPolicySet(rib_policy::TRouteFilterPolicy policy);
  void sendInitialPathComputation(
      std::chrono::milliseconds nexthopResolutionTimeout =
          std::chrono::milliseconds(0));
  void sendAnnouncement(
      const PrefixPathIds& pfxPathIds,
      const TinyPeerInfo& peer,
      std::shared_ptr<facebook::bgp::BgpPath> attr);
  void sendWithdrawal(
      const PrefixPathIds& pfxPathIds,
      const TinyPeerInfo& peer);
  // Send announcement to Rib with count number of prefixes
  void sendBulkAnnouncement(
      uint32_t count,
      const TinyPeerInfo& peer,
      std::shared_ptr<facebook::bgp::BgpPath> attr,
      int startAddress = 0x01000000 /* 1.0.0.0 */);
  // Send withdrawals to Rib with count number of prefixes
  void sendBulkWithdrawal(
      uint32_t count,
      const TinyPeerInfo& peer,
      int startAddress = 0x01000000 /* 1.0.0.0 */);
  // Send message to pause best path and FIB programming
  void sendPauseBestPathAndFibProgramming(
      RibPauseResumeCause ribPauseResumeCause);
  // Send message to resume best path and FIB programming
  void sendResumeBestPathAndFibProgramming(
      RibPauseResumeCause ribPauseResumeCause);
  // Send NexthopResolutionUpdate for conditional local route origination
  void sendNexthopResolutionUpdate(NexthopResolutionUpdate nexthopResUpdate);
  void runLbwCommunityBestPathTest(bool computeUcmpFromLbwComm);
  Config config() const;
  void createPeerManager();
  /**
   * Update nexthop cache and push changed statuses to ribInQ.
   */
  void updateCacheAndNotifyRib(const std::vector<NexthopStatus>& updates);

  void createFsdbTestResources();
  void setUpFsdb();
  void completeFibProgrammingPass(bool fullSync);
  bool isFsdbSyncerStarted() const;
  bool isCanonicalRibUpdateQueueEmpty() const;
  // True once Rib has run its initial path computation (initial full-sync).
  bool isRibEoRReceived() const;
  /*
   * True while Rib has deferred its initial path computation waiting for all
   * registered nexthops to resolve.
   */
  bool isInitialPathComputationPending() const;
  void waitForFsdbPublisherConnected();
  // Check if best path computation and FIB programming is paused
  bool isBestPathAndFibProgrammingPaused() const;
  folly::F14FastMap<uint32_t, std::shared_ptr<const BgpPath>>
  getPathIdAttrsMapFromAnnouncement(const RibOutAnnouncement& ann);
  folly::F14FastSet<uint32_t> getPathIdSetFromWithdrawal(
      const RibOutWithdrawal& with);

  /*
   * Variables
   */
  std::shared_ptr<facebook::bgp::BgpGlobalConfig> bgpGlobalConfig1_;
  std::unique_ptr<MockRib> rib_;
  MockFib* fib_;
  std::thread ribThread_;

  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ_{
      nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<RibOutMessage> ribOutQ_;

  // BgpService dependencies
  Config config_{config()};

  // Peer Manager
  std::optional<MonitoredMPMCQueue<NeighborWatcherMessage>> neighborEventQ_;
  std::shared_ptr<MockPeerManager> peerManager_;
  std::shared_ptr<MockSessionManager> sessionMgr_;

  // Watchdog
  Watchdog watchdog_{std::make_shared<const Config>(config_)};

  std::unique_ptr<BgpServiceDC> service_{nullptr};
  std::unique_ptr<facebook::fboss::fsdb::test::FsdbTestServer> fsdbServer_;
  std::unique_ptr<facebook::fboss::fsdb::test::FsdbTestSubscriber>
      fsdbSubscriber_;
  std::unique_ptr<folly::ScopedEventBaseThread> fsdbSyncerEventBaseThread_;
  std::unique_ptr<FsdbSyncer> fsdbSyncer_{nullptr};

  TinyPeerInfo eBgpPeer1_{
      kPeerAddr1,
      kPeerAsn1,
      kPeerRouterId1,
      BgpSessionType::EBGP,
      false /*isRrClient*/,
      false /*isRedistributePeer*/,
      10.f};

  TinyPeerInfo eBgpPeer2_{
      kPeerAddr2,
      kPeerAsn2,
      kPeerRouterId2,
      BgpSessionType::EBGP,
      false /*isRrClient*/,
      false /*isRedistributePeer*/,
      20.f};

  // create redistribute peer (ebgp peer)
  TinyPeerInfo redistributePeer_{
      kPeerAddr2,
      kPeerAsn2,
      kPeerRouterId2,
      BgpSessionType::EBGP,
      false /*isRrClient*/,
      true /*isRedistributePeer*/};

  TinyPeerInfo eBgpPeer3_{
      kPeerAddr3,
      kPeerAsn3,
      kPeerRouterId3,
      BgpSessionType::EBGP,
      false /*isRrClient*/,
      false /*isRedistributePeer*/,
      30.f};

  TinyPeerInfo eBgpPeer4_{
      kPeerAddr4,
      kPeerAsn4,
      kPeerRouterId4,
      BgpSessionType::EBGP,
      false /*isRrClient*/,
      false /*isRedistributePeer*/};

  TinyPeerInfo eBgpPeer5_{
      kPeerAddr5,
      kPeerAsn5,
      kPeerRouterId5,
      BgpSessionType::EBGP,
      false /*isRrClient*/,
      false /*isRedistributePeer*/};

  TinyPeerInfo eBgpPeer6_{
      kPeerAddr6,
      kPeerAsn1,
      kPeerRouterId1,
      BgpSessionType::EBGP,
      false /*isSrClient*/,
      false /*isRedistributePeer*/};

  TinyPeerInfo confedEBgpPeer_{
      kPeerAddr1,
      kPeerAsn1,
      kPeerRouterId1,
      BgpSessionType::ConfedEBGP,
      false /*isRrClient*/,
      false /*isRedistributePeer*/};

  TinyPeerInfo iBgpPeer_{
      kPeerAddr2,
      kLocalAs1,
      kPeerRouterId2,
      BgpSessionType::IBGP,
      false // isRrClient
  };

  TinyPeerInfo localPeer_{
      kLocalRoutePeerAddr,
      kLocalRoutePeerAsn,
      kLocalRoutePeerRouterId,
      BgpSessionType::IBGP,
      false // isRrClient
  };

  TinyPeerInfo injector1_{
      kPeerAddr2,
      kLocalAs1,
      kPeerRouterId1,
      BgpSessionType::IBGP,
      false // isRrClient
  };

  TinyPeerInfo injector2_{
      kPeerAddr2,
      kLocalAs1,
      kPeerRouterId2,
      BgpSessionType::IBGP,
      false // isRrClient
  };

  nettools::bgplib::BgpPeerId localPeerId_{
      kLocalRoutePeerAddr,
      kLocalRoutePeerRouterId};

  std::shared_ptr<facebook::bgp::BgpPath> attr_;
  std::shared_ptr<facebook::bgp::BgpPath> attrHighLocalPref_;
  // Set expectations with 10 times the prod timeout.
  std::chrono::seconds kFibBackOffTimeoutTest = std::chrono::seconds(10);

  MockFib::NexthopInfoMap& getNexthopInfoMap() {
    return nexthopInfoMap_;
  }

 private:
  gflags::FlagSaver flagSaver_;
  MockFib::NexthopInfoMap nexthopInfoMap_;
  /* Owns this fixture's rib policy files; removed when the fixture dies. */
  folly::test::TemporaryDirectory ribPolicyDir_{"bgp_rib_rp"};
};

class RibWithLocalRouteFixture : public RibFixture {
 public:
  void SetUp() override;
};

class LocalRouteWithPolicyFixture : public RibFixture {
 public:
  void SetUp() override;
  void TearDown() override;
};

class RibNoUcmpComputeFixture : public RibFixture {
 public:
  void SetUp() override;
};

class RibFixtureCountConfedsInAsPathLen : public RibFixture {
 public:
  void SetUp() override;
};

class RibFixtureAddPathTestSuite : public RibFixture,
                                   public testing::WithParamInterface<bool> {};

class RibNexthopTrackingFixture : public RibFixture {
 public:
  void SetUp() override;
};

// RibFixture with BgpGlobalConfig::enableRibAllocatedPathId set.
class RibAllocatedPathIdFixture : public RibFixture {
 public:
  void SetUp() override;
};

} // namespace bgp
} // namespace facebook
