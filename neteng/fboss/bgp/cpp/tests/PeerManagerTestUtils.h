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
#include "neteng/fboss/bgp/cpp/BgpServiceUtil.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/FeatureFlags.h"
#include "neteng/fboss/bgp/cpp/config/Config.h"
#include "neteng/fboss/bgp/cpp/config/ConfigManager.h"
#include "neteng/fboss/bgp/cpp/config/ConfigUtils.h"
#include "neteng/fboss/bgp/cpp/lib/BgpUtil.h"
#include "neteng/fboss/bgp/cpp/lib/coro/BackPressuredQueue.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberBgpPeer.h"
#include "neteng/fboss/bgp/cpp/lib/tests/FiberBgpPeerManagerTestUtils.h"
#include "neteng/fboss/bgp/cpp/tests/MockAdjRib.h"
#include "neteng/fboss/bgp/cpp/tests/MockPeerManager.h"
#include "neteng/fboss/bgp/cpp/tests/MockSessionManager.h"
#include "neteng/fboss/bgp/cpp/tests/PolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"

DECLARE_int32(fiber_stack_size);

namespace facebook {
namespace bgp {

std::unique_ptr<nettools::bgplib::BgpUpdate2> createBgpUpdate2(
    uint32_t num,
    folly::IPAddress nexthop);

RibOutMessage createRibSingleAnnounce(
    const folly::CIDRNetwork& prefix = kV4Prefix1,
    const folly::IPAddress& nexthop = kV4Nexthop1,
    AsNum ribEntryAs = kLocalRouteAs,
    bool sendWithEoR = false,
    bool addPath = false,
    uint32_t pathIdToSend = kDefaultPathID);

RibOutMessage createRibInitialSingleAnnounce(
    const folly::CIDRNetwork& prefix = kV4Prefix1,
    const folly::IPAddress& nexthop = kV4Nexthop1,
    AsNum ribEntryAs = kLocalRouteAs,
    bool sendWithEoR = false,
    bool addPath = false,
    uint32_t pathIdToSend = kDefaultPathID);

void createGrState(std::vector<BgpPeerId> peerIds, bool staleTime = false);

bool isGrStateExists();

folly::coro::Task<void> waitForConsumerTimerExpiry() noexcept;

class TestSessionManager : public facebook::bgp::SessionManager {
 public:
  TestSessionManager(
      const facebook::bgp::BgpGlobalConfig& config,
      TestFiberBgpPeerCallback* callback,
      folly::fibers::FiberManager& fm,
      bool enableMessagesOverNotifyQueue = true,
      bool enableCoroNotifyQueue = false)
      : facebook::bgp::SessionManager(
            config,
            enableMessagesOverNotifyQueue,
            enableCoroNotifyQueue) {
    if (enableCoroNotifyQueue) {
      // skip processing non-coro callback for now
      return;
    }

    // fiber which converts notifications to callback
    fm.addTask([this, callback]() mutable {
      facebook::nettools::bgplib::BgpPeerManagerEventObserver visitor{callback};
      auto reader = getNotifyQueue();
      while (true) {
        auto msg = reader.get();
        if (!msg) {
          break;
        }
        std::visit(visitor, *msg);
      }
    });
  }

  folly::Future<folly::Unit> getSessionsComeUpFuture(
      const std::unordered_set<BgpPeerId>& peerSet,
      const std::chrono::seconds& timeout = std::chrono::seconds(5));
  folly::Future<folly::Unit> getSessionsGoDownFuture(
      const std::unordered_set<BgpPeerId>& peerSet,
      const std::chrono::seconds& timeout = std::chrono::seconds(5));
};

class PeerManagerTestFixture : public ::testing::Test {
 public:
  folly::fibers::FiberManager::Options options_;

  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ_{
      nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<RibOutMessage> ribOutQ_;
  std::optional<MonitoredMPMCQueue<NeighborWatcherMessage>> nbrRouteChangeQ_ =
      std::make_optional<MonitoredMPMCQueue<NeighborWatcherMessage>>();
  MonitoredMPMCQueue<AdjRib::ObservableMessageT> observerQ_;

  // two peers peering with peerMgr_
  std::shared_ptr<TestSessionManager> sessionMgr1_;
  std::shared_ptr<TestSessionManager> sessionMgr2_;
  std::shared_ptr<TestSessionManager> sessionMgr3_;
  TestFiberBgpPeerCallback callback1_;
  TestFiberBgpPeerCallback callback2_;
  TestFiberBgpPeerCallback callback3_;

  thrift::BgpPeer dynamicShivPeer1_;
  thrift::BgpPeer dynamicShivPeer2_;
  thrift::BgpPeer dynamicMonitorPeer1_;
  thrift::BgpPeer dynamicVipInjectorPeer1_;
  thrift::BgpPeer staticPeer1_;
  thrift::BgpPeer staticPeer2_;
  thrift::BgpPeer sprPeer_;
  std::vector<thrift::PeerGroup> staticPeerGroups;

  std::shared_ptr<facebook::bgp::BgpGlobalConfig> bgpGlobalConfig1_;
  std::shared_ptr<facebook::bgp::BgpGlobalConfig> bgpGlobalConfig2_;
  std::shared_ptr<facebook::bgp::BgpGlobalConfig> bgpGlobalConfig3_;

  std::shared_ptr<folly::coro::Baton> sessionTerminateBaton_;

  static constexpr int32_t kDefaultEorTimeS = 45;

  // Config created by setupMockPeerManager
  std::shared_ptr<Config> config_;

  // Mock display info
  nettools::bgplib::BgpPeerDisplayInfo mockInfo1_;

  folly::not_null_shared_ptr<std::atomic<bool>> isSafeModeOn_ =
      std::make_shared<std::atomic<bool>>(false);

  virtual void SetUp() override;

  virtual void TearDown() override;

  std::shared_ptr<Config> getConfig(
      bool includeStaticPeer,
      bool includeDynamicShivPeer,
      bool includeDynamicMonitorPeer = false,
      bool includeDynamicVipInjectorPeer = false,
      bool enableStatefulHa = false,
      bool enableVipServer = true,
      int32_t eorTimeS = kDefaultEorTimeS,
      bool enableSubscriberLimit = false,
      bool enableSwitchLimit = false,
      bool applyGoldenPrefixPolicy = false,
      const std::set<std::string>& bgpFeatures = {},
      bool enableDynamicPolicyEvaluation = false,
      bool enableUpdateGroup = false,
      int32_t streamSubscriberLimit = 1);

  // Returns a copy of config with enable_rib_allocated_path_id set.
  std::shared_ptr<Config> enableRibAllocatedPathId(
      const std::shared_ptr<Config>& config);

  std::shared_ptr<Config> addPeerToConfig(
      const std::shared_ptr<Config>& config,
      thrift::BgpPeer& peer,
      const std::optional<const std::string>& peerGroupName = std::nullopt);

  void initTwoSessionMgrs(folly::fibers::FiberManager* fm);
  void initThreeSessionMgrs(folly::fibers::FiberManager* fm);

  std::shared_ptr<AdjRib> setupAdjRib(
      folly::EventBase& evb,
      std::shared_ptr<ChangeTracker<ShadowRibEntry>> changeListTracker,
      const BgpPeerId& peerId,
      const AsNum& remoteAs,
      std::shared_ptr<folly::coro::Baton>& sessionTerminateBaton,
      std::shared_ptr<const Config> config,
      bool isRrClient = false,
      const folly::IPAddress& v4Nexthop = kV4Nexthop1,
      const folly::IPAddress& v6Nexthop = kV6Nexthop1,
      bool enableStatefulHa = false,
      bool v4OverV6Nexthop = false,
      std::shared_ptr<PolicyManager> policyManager = nullptr);

  // Overload with bitmap parameters for selective multipath notification
  std::shared_ptr<AdjRib> setupAdjRib(
      folly::EventBase& evb,
      std::shared_ptr<ChangeTracker<ShadowRibEntry>> changeListTracker,
      const BgpPeerId& peerId,
      const AsNum& remoteAs,
      std::shared_ptr<folly::coro::Baton>& sessionTerminateBaton,
      std::shared_ptr<const Config> config,
      ConsumerBitmap& addPathBitmap,
      ConsumerBitmap& nonAddPathBitmap,
      bool isRrClient = false,
      const folly::IPAddress& v4Nexthop = kV4Nexthop1,
      const folly::IPAddress& v6Nexthop = kV6Nexthop1,
      bool enableStatefulHa = false,
      bool v4OverV6Nexthop = false);

  std::shared_ptr<MockAdjRib> setupMockAdjRib(
      folly::EventBase& evb,
      const BgpPeerId& peerId,
      const AsNum& remoteAs,
      std::shared_ptr<folly::coro::Baton>& sessionTerminateBaton,
      bool isRrClient = false,
      const folly::IPAddress& v4Nexthop = kV4Nexthop1,
      const folly::IPAddress& v6Nexthop = kV6Nexthop1,
      bool enableStatefulHa = false,
      bool v4OverV6Nexthop = false);

  /*
   * Creates a mock peer manager with appropriate config and
   * mock session manager
   */
  std::shared_ptr<MockPeerManager> setupMockPeerManager(
      bool includeStaticPeer,
      bool includeDynamicShivPeer,
      bool includeDynamicMonitorPeer = false,
      bool includeDynamicVipInjectorPeer = false);

  /*
   * create a mock peer manager in a separate thread such that funcitons
   * running in eventbase loop can also be scheduled
   */
  std::shared_ptr<MockPeerManager> setupMockPeerManagerWithSeparateThread(
      bool includeStaticPeer,
      bool includeDynamicShivPeer);

  /*
   * create a mock session manager which shares the same fiber manager
   * with the mock peer manager. The mockPeerMgr should be a non-empty
   * pointer, e.g., generated by setupMockPeerManager or
   * setupMockPeerManagerWithSeparateThread
   */
  std::shared_ptr<MockSessionManager> setupMockSessionManager(
      std::shared_ptr<MockPeerManager>& mockPeerMgr);

  void runEoRTest(bool isSess1Restarting, bool isSess2Restarting);

  // Helper function to create a mock peer info for a static peer
  std::shared_ptr<nettools::bgplib::BgpPeerDisplayInfo> getMockPeerInfo(
      const folly::IPAddress& peerAddr,
      const uint64_t& routerId,
      const uint64_t& numReset = 0,
      const uint64_t& lastWentDown = 0 /* hours ago */,
      const nettools::bgplib::ResetReason& lastResetReason =
          nettools::bgplib::ResetReason::NOTIFICATION_RCVD);
  // Helper function to create a mock peer info for a dynamic peer
  std::shared_ptr<nettools::bgplib::BgpPeerDisplayInfo> getMockPeerInfo(
      const folly::CIDRNetwork& prefix,
      const uint64_t& routerId,
      bool vipInjectorPeer,
      bool otherDynamicPeer);

  /*
   * Used to trigger scheduleSendBgpUpdates to execute when egress backpressure
   * is enabled. Convenient for tests that want to wait before checking
   * for some adjRib state.
   */
  folly::coro::Task<void> waitForAdjRibsToProcessUpdates(
      folly::EventBase& evb,
      std::vector<std::shared_ptr<AdjRib::AdjRibOutQueueT>> queues);
};

/**
 * @class StreamSubscriberFixture
 * @brief Fixture for stream subscriber tests
 */
class StreamSubscriberFixture : public PeerManagerTestFixture {
 public:
  /**
   * @brief Override default setup function
   */
  void SetUp() override {}

  /**
   * @brief Parameterized SetUp() function for StreamSubscriberFixture
   * tests Note that this SetUp() function should be explicitly called
   * within the test
   *
   * @param configureMonitorPeer: If true, configure a BgpMonitor peer
   * @param initialAnnouncementDone: ribInitialAnnouncementDone_ flag in the
   * peerManager
   * @param enableSubscriberLimit: Enable stream subscriber limit.
   * @param streamSubscriberLimit: Value of the limit when it is enabled.
   */
  void SetUp(
      bool configureMonitorPeer,
      bool initialAnnouncementDone,
      bool enableSubscriberLimit = false,
      int32_t streamSubscriberLimit = 1);

  /**
   * @brief Override default tear down function
   */
  void TearDown() override;

  std::shared_ptr<PeerManagerBase> peerMgr;
  std::shared_ptr<SessionManager> sessionMgr;
  std::shared_ptr<std::thread> peerMgrThread;
  std::shared_ptr<std::thread> sessionMgrThread;
};

class PeerManagerFixtureCanaryKnobTestSuite
    : public PeerManagerTestFixture,
      public testing::WithParamInterface<bool> {};

/**
 * @class PeerManagerDynamicPolicyEvaluationFixture
 * @brief Fixture for testing dynamic policy evaluation
 */
class PeerManagerDynamicPolicyEvaluationFixture
    : public PeerManagerTestFixture {
 public:
  /**
   * @brief Override default setup function
   */
  void SetUp() override {}

  void SetUp(bool enableDynamicPolicyEvaluation = true);

 protected:
  // Structure to hold adjRib and expected state for utility functions
  struct AdjRibPolicyUpdateState {
    std::shared_ptr<AdjRib> adjRib;
    bool expectedIngressPendingPolicyUpdate;
    bool expectedEgressPendingPolicyUpdate;

    AdjRibPolicyUpdateState(
        std::shared_ptr<AdjRib> adjRib,
        bool ingress,
        bool egress)
        : adjRib(std::move(adjRib)),
          expectedIngressPendingPolicyUpdate(ingress),
          expectedEgressPendingPolicyUpdate(egress) {}
  };

  // Generic utility to verify pending flags state with retries
  void verifyStateWithRetries(
      folly::EventBase& evb,
      const std::vector<AdjRibPolicyUpdateState>& adjRibStates);

  // Generic utility to verify pending flags state
  void verifyState(
      folly::EventBase& evb,
      const std::vector<AdjRibPolicyUpdateState>& adjRibStates);

  // Utility to verify route filter statements for multiple adjRibs
  void verifyRouteFilterStatement(
      folly::EventBase& evb,
      const std::vector<std::pair<
          std::shared_ptr<AdjRib>,
          rib_policy::TRouteFilterStatement>>& adjRibStatements);
};

/**
 * Utility function to create policy maps for testing
 * @param policyMapEntries Vector of tuples containing (key, ingress_policy,
 * egress_policy). Empty string means "clear/unset policy" (std::nullopt).
 * @return Unique pointer to policy map with std::optional values
 */
std::unique_ptr<PeerToPolicyMap> createPolicyMap(
    const std::vector<std::tuple<std::string, std::string, std::string>>&
        policyMapEntries);

} // namespace bgp
} // namespace facebook
