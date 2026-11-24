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

#include <boost/filesystem.hpp>

#include <fb303/FollyLoggingHandler.h>
#include <fb303/ServiceData.h>
#include <fmt/format.h>
#include <folly/FileUtil.h>
#include <folly/ScopeGuard.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Collect.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/logging/LogMessage.h>
#include <folly/logging/LoggerDB.h>
#include <folly/logging/test/TestLogHandler.h>
#include <folly/test/JsonTestUtil.h>
#include <folly/testing/TestUtil.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <thrift/lib/cpp2/util/ScopedServerInterfaceThread.h>

#include "neteng/fboss/bgp/cpp/BgpServiceDC.h"
#include "neteng/fboss/bgp/cpp/BgpServiceUtil.h"
#include "neteng/fboss/bgp/cpp/config/ConfigManager.h"
#include "neteng/fboss/bgp/cpp/stats/StatsDC.h"
#include "neteng/fboss/bgp/cpp/tests/MockPeerManager.h"
#include "neteng/fboss/bgp/cpp/tests/MockSessionManager.h"
#include "neteng/fboss/bgp/cpp/tests/RibPolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibUtils.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

using namespace ::testing;
using namespace facebook::neteng::fboss::bgp_attr;
using namespace facebook::neteng::fboss::bgp::thrift;

namespace facebook::bgp {
static const std::string kExitNullPtrLogPrefix = "ExitOrNullPtr";

class BgpServiceTestFixture : public ::testing::Test {
 public:
  void SetUp() override {
    /*
     * rib and peerManager are not tested in the tests
     * so it is fine to keep invalid references in services_ for now
     */

    /*
     * The rib policy files default to fixed paths shared by every bgp test
     * process on the host. MockRib reads the state file and installs it as the
     * bootstrap rib policy, so give each fixture its own pair.
     */
    FLAGS_rp_state_file = (ribPolicyDir_.path() / "rp_state.txt").string();
    FLAGS_rp_change_history_file =
        (ribPolicyDir_.path() / "rp_change_history.txt").string();

    // config
    config_ = createConfig();
    auto configManager = std::make_shared<ConfigManager>(config_);

    // rib
    rib_ = std::make_unique<MockRib>(
        std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>{},
        globalConfig_,
        std::nullopt /* policyConfig */,
        ribInQ_,
        ribOutQ_,
        "dev" /* platform */,
        nullptr /* fsdbSyncer*/);

    // peerManager
    peerManager_ = std::make_unique<MockPeerManager>(
        configManager, ribInQ_, ribOutQ_, neighborEventQ_);

    sessionMgr_ = std::make_shared<MockSessionManager>(globalConfig_, false);

    peerManager_->setSessionManager(sessionMgr_);

    // watchdog
    watchdog_ = std::make_unique<Watchdog>(config_);

    // bgpService
    service_ = std::make_unique<BgpServiceDC>(
        *peerManager_,
        configManager,
        *rib_,
        nullptr /* neighbor watcher */,
        *watchdog_,
        false /* thrift protection */);
  }

  void TearDown() override {
    service_.reset();
  }

 protected:
  std::unique_ptr<MockRib> rib_;
  std::unique_ptr<MockPeerManager> peerManager_;
  std::unique_ptr<BgpServiceDC> service_;
  std::shared_ptr<Config> config_;
  std::unique_ptr<Watchdog> watchdog_;
  std::shared_ptr<MockSessionManager> sessionMgr_;
  /* Owns this fixture's rib policy files; removed when the fixture dies. */
  folly::test::TemporaryDirectory ribPolicyDir_{"bgp_svc_rp"};

 private:
  /*
   * [Config]
   *
   * Define a virtual method to be inherited and overridden by child test
   * fixtures for customize config setup.
   */
  virtual std::shared_ptr<Config> createConfig() {
    thrift::BgpConfig thriftConfig;
    thriftConfig.router_id() = kLocalAddr1.str();
    thriftConfig.local_as() = kAsn1;
    thriftConfig.hold_time() = kHoldTime.count();
    thriftConfig.graceful_restart_convergence_seconds() =
        kGrRestartTime.count();
    thriftConfig.listen_addr() = kLocalAddr1.str();
    thriftConfig.eor_time_s() = 45;

    return std::make_shared<Config>(std::move(thriftConfig));
  }

  BgpGlobalConfig globalConfig_{
      kAsn1, // localAsn
      kLocalAddr1, // routerId
      kPeerAddr3, // clusterId
      kHoldTime, // holdTime
      std::nullopt, // listenAddr
      kGrRestartTime, // grRestartTime
      {}, // networksV4
      {} // networksV6
  };

  // Rib
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ_{
      nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<RibOutMessage> ribOutQ_;

  // Peer Manager
  std::optional<MonitoredMPMCQueue<NeighborWatcherMessage>> neighborEventQ_;
};

class BgpServiceNullPtrTestFixture : public BgpServiceTestFixture {
 public:
  void SetUp() override {
    BgpServiceTestFixture::SetUp();
    logHandler_ = std::make_shared<folly::TestLogHandler>();
    folly::LoggerDB::get().getCategory("")->addHandler(logHandler_);
    folly::LoggerDB::get().setLevel("", folly::LogLevel::INFO);
  }

  std::shared_ptr<folly::TestLogHandler> logHandler_;
  // Placeholder variables for reference arguments
  TResult res_;
  std::vector<rib_policy::TPathSelector> resPthSelector_;
  std::vector<TRibEntry> resRibEntry_;
  TBgpAfi afi_{0};
};

class BgpServiceUpdateGroupRouteFilterTestFixture
    : public BgpServiceTestFixture {
 private:
  std::shared_ptr<Config> createConfig() override {
    thrift::BgpConfig thriftConfig;
    thriftConfig.router_id() = kLocalAddr1.str();
    thriftConfig.local_as() = kAsn1;
    thriftConfig.hold_time() = kHoldTime.count();
    thriftConfig.graceful_restart_convergence_seconds() =
        kGrRestartTime.count();
    thriftConfig.listen_addr() = kLocalAddr1.str();
    thriftConfig.eor_time_s() = 45;
    thrift::BgpSettingConfig settings;
    settings.enable_update_group() = true;
    thriftConfig.bgp_setting_config() = std::move(settings);
    return std::make_shared<Config>(std::move(thriftConfig));
  }
};

class BgpServiceBaseNullPtrTestFixture : public BgpServiceTestFixture {
 public:
  void SetUp() override {
    BgpServiceTestFixture::SetUp();
    logHandler_ = std::make_shared<folly::TestLogHandler>();
    folly::LoggerDB::get().getCategory("")->addHandler(logHandler_);
    folly::LoggerDB::get().setLevel("", folly::LogLevel::INFO);
  }

  std::shared_ptr<folly::TestLogHandler> logHandler_;
  // Placeholder variables for reference arguments
  std::vector<TBgpSession> resBgpSession_;
  std::map<TIpPrefix, TBgpPath> resBgpPath_;
  std::map<TIpPrefix, std::vector<TBgpPath>> resBgpPaths_;
  std::map<TIpPrefix, TBgpNetwork> resBgpNetworkMap_;
  std::vector<TBgpNetwork> resBgpNetwork_;
};

class BgpServiceWithNodeDrainedConfigTestFixture
    : public BgpServiceTestFixture {
 public:
  std::shared_ptr<Config> createConfig() override {
    thrift::BgpConfig bgpConfig;
    bgpConfig.router_id() = kLocalAddr1.str();
    bgpConfig.local_as() = kAsn1;
    bgpConfig.hold_time() = kHoldTime.count();
    bgpConfig.graceful_restart_convergence_seconds() = kGrRestartTime.count();
    bgpConfig.listen_addr() = kLocalAddr1.str();
    bgpConfig.eor_time_s() = 45;
    bgpConfig.drain_state() = bgp_policy::DrainState::DRAINED;

    return std::make_shared<Config>(std::move(bgpConfig));
  }
};

class BgpServiceWithInterfaceDrainedConfigTestFixture
    : public BgpServiceTestFixture {
 public:
  std::shared_ptr<Config> createConfig() override {
    thrift::BgpConfig bgpConfig;
    bgpConfig.router_id() = kLocalAddr1.str();
    bgpConfig.local_as() = kAsn1;
    bgpConfig.hold_time() = kHoldTime.count();
    bgpConfig.graceful_restart_convergence_seconds() = kGrRestartTime.count();
    bgpConfig.listen_addr() = kLocalAddr1.str();
    bgpConfig.eor_time_s() = 45;
    bgpConfig.drain_state() = bgp_policy::DrainState::UNDRAINED;
    bgpConfig.drained_interfaces() = {"ethernet1/1"};

    return std::make_shared<Config>(std::move(bgpConfig));
  }
};

/*
 * Test setLogLevel API, inspired by https://fburl.com/code/t7sy7hd8
 */
TEST_F(BgpServiceTestFixture, SetLogLevelTest) {
  // include the handler
  facebook::fb303::registerFollyLoggingOptionHandlers();

  service_->setLogLevel(std::make_unique<std::string>(".=DBG1"));

  FOLLY_EXPECT_JSON_EQ(
      R"JSON({
  "categories" : {
    "" : {
      "handlers" : [
        "default"
      ],
      "inherit" : false,
      "level" : "DBG1",
      "propagate": "NONE"
    }
  },
  "handlers" : {
    "default" : {
      "options" : {
        "async" : "true",
        "stream" : "stderr"
      },
      "type" : "stream"
    }
  }
})JSON",
      fb303::ThreadCachedServiceData::get()->getOption("logging"));

  service_->setLogLevel(
      std::make_unique<std::string>(".=INFO;default:async=true"));

  FOLLY_EXPECT_JSON_EQ(
      R"JSON({
  "categories" : {
    "" : {
      "handlers" : [
        "default"
      ],
      "inherit" : false,
      "level" : "INFO",
      "propagate": "NONE"
    }
  },
  "handlers" : {
    "default" : {
      "options" : {
        "async" : "true",
        "stream" : "stderr"
      },
      "type" : "stream"
    }
  }
})JSON",
      fb303::ThreadCachedServiceData::get()->getOption("logging"));
}

TEST_F(BgpServiceTestFixture, InitializationApiTest) {
  const auto initializingTime{1};
  const auto initializedTime{100};

  /*
   * Step1: zero out counters
   */
  facebook::fb303::ThreadCachedServiceData::getShared()->clearCounter(
      fmt::format(
          kInitEventCounterFormat,
          apache::thrift::util::enumNameSafe(
              BgpInitializationEvent::INITIALIZING)));

  facebook::fb303::ThreadCachedServiceData::getShared()->clearCounter(
      fmt::format(
          kInitEventCounterFormat,
          apache::thrift::util::enumNameSafe(
              BgpInitializationEvent::INITIALIZED)));

  // verify NO converge signal
  EXPECT_FALSE(service_->initializationConverged());

  /*
   * Step2: test initializationConverged() API
   */
  facebook::fb303::ThreadCachedServiceData::getShared()->setCounter(
      fmt::format(
          kInitEventCounterFormat,
          apache::thrift::util::enumNameSafe(
              BgpInitializationEvent::INITIALIZED)),
      initializedTime /* time elapsed */);

  // verify initializing state
  EXPECT_TRUE(service_->initializationConverged());

  /*
   * Step3: test getInitializationEvents() API
   */
  BgpInitializationMap res{};
  service_->getInitializationEvents(res);
  EXPECT_EQ(1, res.size());
  EXPECT_EQ(initializedTime, res.at(BgpInitializationEvent::INITIALIZED));

  // set initialized time
  facebook::fb303::ThreadCachedServiceData::getShared()->setCounter(
      fmt::format(
          kInitEventCounterFormat,
          apache::thrift::util::enumNameSafe(
              BgpInitializationEvent::INITIALIZING)),
      initializingTime /* time elapsed */);

  service_->getInitializationEvents(res);
  EXPECT_EQ(2, res.size());
  EXPECT_EQ(initializingTime, res.at(BgpInitializationEvent::INITIALIZING));
}

// Test getNodeDrainState with UNDRAIN state
TEST_F(BgpServiceTestFixture, GetUndrainedNodeTest) {
  neteng::fboss::bgp::thrift::TBgpDrainState state;
  service_->getDrainState(state);
  EXPECT_EQ(*state.drain_state(), bgp_policy::DrainState::UNDRAINED);
  ASSERT_TRUE(state.drained_interfaces()->empty());
}

// Test getNodeDrainState with DRAIN state
TEST_F(BgpServiceWithNodeDrainedConfigTestFixture, GetDrainedNodeTest) {
  neteng::fboss::bgp::thrift::TBgpDrainState state;
  service_->getDrainState(state);
  EXPECT_EQ(*state.drain_state(), bgp_policy::DrainState::DRAINED);
  ASSERT_TRUE(state.drained_interfaces()->empty());
}

// Test getNodeDrainState with interface drained
TEST_F(
    BgpServiceWithInterfaceDrainedConfigTestFixture,
    GetDrainedInterfaceTest) {
  neteng::fboss::bgp::thrift::TBgpDrainState state;
  service_->getDrainState(state);
  EXPECT_EQ(*state.drain_state(), bgp_policy::DrainState::UNDRAINED);
  EXPECT_EQ(state.drained_interfaces()->size(), 1);
}

// test validateConfig
TEST_F(BgpServiceNullPtrTestFixture, ValidateConfigNullPtrTest) {
  service_->validateConfig(res_, nullptr);
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test validateConfigAndPolicy
TEST_F(BgpServiceNullPtrTestFixture, ValidateConfigAndPolicyNullPtrTest) {
  service_->validateConfigAndPolicy(res_, nullptr, nullptr);
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_setRouteAttributePolicy
CO_TEST_F(BgpServiceNullPtrTestFixture, SetRouteAttributePolicyNullPtrTest) {
  co_await service_->co_setRouteAttributePolicy(nullptr);
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_setPathSelectionPolicy
CO_TEST_F(BgpServiceNullPtrTestFixture, SetPathSelectionPolicyNullPtrTest) {
  co_await service_->co_setPathSelectionPolicy(nullptr);
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getActivePathSelectionCriteria
TEST_F(
    BgpServiceNullPtrTestFixture,
    GetActivePathSelectionCriteriaNullPtrTest) {
  folly::coro::blockingWait(
      service_->co_getActivePathSelectionCriteria(nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_setRouteFilterPolicy
CO_TEST_F(BgpServiceNullPtrTestFixture, SetRouteFilterPolicyNullPtrTest) {
  co_await service_->co_setRouteFilterPolicy(nullptr);
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

CO_TEST_F(
    BgpServiceUpdateGroupRouteFilterTestFixture,
    SetRouteFilterPolicyRejectsEgressFilter) {
  rib_policy::TRouteFilterPolicy policy;
  policy.statements()->emplace(
      ".*", createTRouteFilterStatement({kV4Prefix1}, false, true));

  auto result = co_await service_->co_setRouteFilterPolicy(
      std::make_unique<rib_policy::TRouteFilterPolicy>(std::move(policy)));

  EXPECT_FALSE(*result->success());
  EXPECT_THAT(
      *result->err(),
      HasSubstr(
          "egress route filters are not supported when update groups are "
          "enabled"));
}

CO_TEST_F(
    BgpServiceUpdateGroupRouteFilterTestFixture,
    SetRouteFilterPolicyAcceptsIngressOnlyFilter) {
  rib_policy::TRouteFilterPolicy policy;
  policy.statements()->emplace(
      ".*", createTRouteFilterStatement({kV4Prefix1}, false, false));

  auto result = co_await service_->co_setRouteFilterPolicy(
      std::make_unique<rib_policy::TRouteFilterPolicy>(std::move(policy)));

  EXPECT_TRUE(*result->success());
  peerManager_->getEventBase().loopOnce();
}

// Helper function to create a peer group with AFI settings
thrift::PeerGroup createTestPeerGroup(
    bool disableIpv4 = false,
    bool disableIpv6 = false) {
  thrift::PeerGroup peerGroup;
  if (disableIpv4) {
    peerGroup.disable_ipv4_afi() = true;
  }
  if (disableIpv6) {
    peerGroup.disable_ipv6_afi() = true;
  }
  return peerGroup;
}

class BgpServicePeerGroupValidationTestFixture : public BgpServiceTestFixture {
 public:
  void SetUp() override {
    BgpServiceTestFixture::SetUp();
    logHandler_ = std::make_shared<folly::TestLogHandler>();
    folly::LoggerDB::get().getCategory("")->addHandler(logHandler_);
    folly::LoggerDB::get().setLevel("", folly::LogLevel::INFO);
  }

  std::shared_ptr<Config> createConfig() override {
    thrift::BgpConfig thriftConfig;
    thriftConfig.router_id() = kLocalAddr1.str();
    thriftConfig.local_as() = kAsn1;
    thriftConfig.hold_time() = kHoldTime.count();
    thriftConfig.graceful_restart_convergence_seconds() =
        kGrRestartTime.count();
    thriftConfig.listen_addr() = kLocalAddr1.str();
    thriftConfig.eor_time_s() = 45;

    // Add test peer groups
    std::vector<thrift::PeerGroup> testPeerGroups;

    thrift::PeerGroup peerGroup1 = createTestPeerGroup(false, true);
    peerGroup1.name() = "test-peer-group-ipv4-enabled";
    testPeerGroups.push_back(peerGroup1);

    thrift::PeerGroup peerGroup2 = createTestPeerGroup(true, false);
    peerGroup2.name() = "test-peer-group-ipv6-enabled";
    testPeerGroups.push_back(peerGroup2);

    thrift::PeerGroup peerGroup3 = createTestPeerGroup(false, false);
    peerGroup3.name() = "test-peer-group-both-enabled";
    testPeerGroups.push_back(peerGroup3);

    thrift::PeerGroup peerGroup4 = createTestPeerGroup(true, true);
    peerGroup4.name() = "test-peer-group-both-disabled";
    testPeerGroups.push_back(peerGroup4);

    thriftConfig.peer_groups() = testPeerGroups;

    return std::make_shared<Config>(std::move(thriftConfig));
  }

  std::shared_ptr<folly::TestLogHandler> logHandler_;
  TResult res_;
};

// Test setRouteFilterPolicy with peer group that doesn't exist
TEST_F(
    BgpServicePeerGroupValidationTestFixture,
    SetRouteFilterPolicyPeerGroupNotFoundTest) {
  rib_policy::TRouteFilterPolicy tPolicy;
  tPolicy.key_type() = rib_policy::KeyType::PEER_GROUP_NAME;
  tPolicy.statements()->emplace(
      "nonexistent-peer-group", createTRouteFilterStatement({}, false, true));

  auto res = folly::coro::blockingWait(service_->co_setRouteFilterPolicy(
      std::make_unique<rib_policy::TRouteFilterPolicy>(std::move(tPolicy))));

  EXPECT_FALSE(*res->success());
  EXPECT_THAT(*res->err(), HasSubstr("PEER_GROUP_NOT_FOUND"));
  EXPECT_THAT(
      logHandler_->getMessageValues(),
      Contains(ContainsRegex("BgpServicePeerGroupValidation")));
}

// Test setRouteFilterPolicy with IPv4 policy but peer group has IPv4 disabled
TEST_F(
    BgpServicePeerGroupValidationTestFixture,
    SetRouteFilterPolicyIpv4MismatchTest) {
  rib_policy::TRouteFilterPolicy tPolicy;
  tPolicy.key_type() = rib_policy::KeyType::PEER_GROUP_NAME;
  tPolicy.statements()->emplace(
      "test-peer-group-ipv6-enabled",
      createTRouteFilterStatementWithIngressAndEgressFilters(
          {kV4Prefix1},
          {kV4Prefix1},
          false,
          false,
          facebook::bgp::routing_policy::IPVersion::V4,
          std::nullopt));

  auto res = folly::coro::blockingWait(service_->co_setRouteFilterPolicy(
      std::make_unique<rib_policy::TRouteFilterPolicy>(std::move(tPolicy))));

  EXPECT_FALSE(*res->success());
  EXPECT_THAT(*res->err(), HasSubstr("IPV4_AFI_MISMATCH"));
  EXPECT_THAT(
      logHandler_->getMessageValues(),
      Contains(ContainsRegex("BgpServicePeerGroupValidation")));
}

// Test setRouteFilterPolicy with IPv6 policy but peer group has IPv6 disabled
TEST_F(
    BgpServicePeerGroupValidationTestFixture,
    SetRouteFilterPolicyIpv6MismatchTest) {
  rib_policy::TRouteFilterPolicy tPolicy;
  tPolicy.key_type() = rib_policy::KeyType::PEER_GROUP_NAME;
  tPolicy.statements()->emplace(
      "test-peer-group-ipv4-enabled",
      createTRouteFilterStatementWithIngressAndEgressFilters(
          {kV4Prefix1},
          {kV4Prefix1},
          false,
          false,
          std::nullopt,
          facebook::bgp::routing_policy::IPVersion::V6));

  auto res = folly::coro::blockingWait(service_->co_setRouteFilterPolicy(
      std::make_unique<rib_policy::TRouteFilterPolicy>(std::move(tPolicy))));

  EXPECT_FALSE(*res->success());
  EXPECT_THAT(*res->err(), HasSubstr("IPV6_AFI_MISMATCH"));
  EXPECT_THAT(
      logHandler_->getMessageValues(),
      Contains(ContainsRegex("BgpServicePeerGroupValidation")));
}

// test co_getRibPrefix
TEST_F(BgpServiceNullPtrTestFixture, GetRibPrefixNullPtrTest) {
  folly::coro::blockingWait(service_->co_getRibPrefix(nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getRibSubprefixes
TEST_F(BgpServiceNullPtrTestFixture, GetRibSubprefixesNullPtrTest) {
  folly::coro::blockingWait(service_->co_getRibSubprefixes(nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_addNetwork
TEST_F(BgpServiceNullPtrTestFixture, AddNetworkNullPtrTest) {
  service_->addNetwork(nullptr, nullptr);
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test addNetworks
TEST_F(BgpServiceNullPtrTestFixture, AddNetworksNullPtrTest) {
  service_->addNetworks(nullptr);
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test delNetwork
TEST_F(BgpServiceNullPtrTestFixture, DelNetworkNullPtrTest) {
  service_->delNetwork(nullptr);
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test delNetworks
TEST_F(BgpServiceNullPtrTestFixture, DelNetworksNullPtrTest) {
  service_->delNetworks(nullptr);
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getRibEntriesForCommunity
TEST_F(BgpServiceNullPtrTestFixture, GetRibEntriesForCommunityNullPtrTest) {
  folly::coro::blockingWait(
      service_->co_getRibEntriesForCommunity(afi_, nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getRibEntriesForCommunities
TEST_F(BgpServiceNullPtrTestFixture, GetRibEntriesForCommunitiesNullPtrTest) {
  folly::coro::blockingWait(
      service_->co_getRibEntriesForCommunities(afi_, nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getNexthopInfoForNexthop
TEST_F(BgpServiceNullPtrTestFixture, GetNexthopInfoForNexthopNullPtrTest) {
  folly::coro::blockingWait(service_->co_getNexthopInfoForNexthop(nullptr));
  EXPECT_THAT(
      logHandler_->getMessageValues(),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getNexthopInfos with nullptr
TEST_F(BgpServiceNullPtrTestFixture, GetNexthopInfosNullPtrTest) {
  auto result =
      folly::coro::blockingWait(service_->co_getNexthopInfos(nullptr));
  EXPECT_TRUE(result->empty());
  EXPECT_THAT(
      logHandler_->getMessageValues(),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getBgpNeighbors with nullptr
TEST_F(BgpServiceBaseNullPtrTestFixture, GetBgpNeighborsNullPtrTest) {
  auto result =
      folly::coro::blockingWait(service_->co_getBgpNeighbors(nullptr));
  EXPECT_TRUE(result->empty());
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getBgpNeighborsFromSession with nullptr
TEST_F(
    BgpServiceBaseNullPtrTestFixture,
    GetBgpNeighborsFromSessionNullPtrTest) {
  auto result = folly::coro::blockingWait(
      service_->co_getBgpNeighborsFromSession(nullptr, nullptr));
  EXPECT_TRUE(result->empty());
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getPrefilterReceivedNetworks
TEST_F(
    BgpServiceBaseNullPtrTestFixture,
    GetPrefilterReceivedNetworksNullPtrTest) {
  folly::coro::blockingWait(service_->co_getPrefilterReceivedNetworks(nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getPrefilterReceivedNetworks2
TEST_F(
    BgpServiceBaseNullPtrTestFixture,
    GetPrefilterReceivedNetworks2NullPtrTest) {
  folly::coro::blockingWait(
      service_->co_getPrefilterReceivedNetworks2(nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getPrefilterReceivedNetworksFromSession
TEST_F(
    BgpServiceBaseNullPtrTestFixture,
    GetPrefilterReceivedNetworksFromSessionNullPtrTest) {
  folly::coro::blockingWait(
      service_->co_getPrefilterReceivedNetworksFromSession(nullptr, nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getPrefilterReceivedNetworksFromSession2
TEST_F(
    BgpServiceBaseNullPtrTestFixture,
    GetPrefilterReceivedNetworksFromSession2NullPtrTest) {
  folly::coro::blockingWait(
      service_->co_getPrefilterReceivedNetworksFromSession2(nullptr, nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getPostfilterReceivedNetworks
TEST_F(
    BgpServiceBaseNullPtrTestFixture,
    GetPostfilterReceivedNetworksNullPtrTest) {
  folly::coro::blockingWait(
      service_->co_getPostfilterReceivedNetworks(nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getPostfilterReceivedNetworks2
TEST_F(
    BgpServiceBaseNullPtrTestFixture,
    GetPostfilterReceivedNetworks2NullPtrTest) {
  folly::coro::blockingWait(
      service_->co_getPostfilterReceivedNetworks2(nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getPostfilterReceivedNetworksFromSession
TEST_F(
    BgpServiceBaseNullPtrTestFixture,
    GetPostfilterReceivedNetworksFromSessionNullPtrTest) {
  folly::coro::blockingWait(
      service_->co_getPostfilterReceivedNetworksFromSession(nullptr, nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getPostfilterReceivedNetworksFromSession2
TEST_F(
    BgpServiceBaseNullPtrTestFixture,
    GetPostfilterReceivedNetworksFromSession2NullPtrTest) {
  folly::coro::blockingWait(
      service_->co_getPostfilterReceivedNetworksFromSession2(nullptr, nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getPrefilterAdvertisedNetworks
TEST_F(
    BgpServiceBaseNullPtrTestFixture,
    GetPrefilterAdvertisedNetworksNullPtrTest) {
  folly::coro::blockingWait(
      service_->co_getPrefilterAdvertisedNetworks(nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getPrefilterAdvertisedNetworks2
TEST_F(
    BgpServiceBaseNullPtrTestFixture,
    GetPrefilterAdvertisedNetworks2NullPtrTest) {
  folly::coro::blockingWait(
      service_->co_getPrefilterAdvertisedNetworks2(nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getPostfilterAdvertisedNetworks
TEST_F(
    BgpServiceBaseNullPtrTestFixture,
    GetPostfilterAdvertisedNetworksNullPtrTest) {
  folly::coro::blockingWait(
      service_->co_getPostfilterAdvertisedNetworks(nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getPostfilterAdvertisedNetworks2
TEST_F(
    BgpServiceBaseNullPtrTestFixture,
    GetPostfilterAdvertisedNetworks2NullPtrTest) {
  folly::coro::blockingWait(
      service_->co_getPostfilterAdvertisedNetworks2(nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getRejectedNetworksSummary
TEST_F(
    BgpServiceBaseNullPtrTestFixture,
    GetRejectedNetworksSummaryNullPtrTest) {
  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  folly::coro::blockingWait(service_->co_getRejectedNetworksSummary(nullptr));
  EXPECT_THAT(
      logHandler_->getMessageValues(),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getRejectedNetworksDetails
TEST_F(
    BgpServiceBaseNullPtrTestFixture,
    GetRejectedNetworksDetailsNullPtrTest) {
  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  folly::coro::blockingWait(service_->co_getRejectedNetworksDetails(nullptr));
  EXPECT_THAT(
      logHandler_->getMessageValues(),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

/*
 * TDirectionFilter::BOTH is a value this endpoint cannot serve, since rejection
 * is resolved against one Adj-RIB per walk. It must be refused rather than
 * silently answered for one of the two directions.
 */
TEST_F(BgpServiceBaseNullPtrTestFixture, GetRejectedNetworksBothDirectionTest) {
  auto request = std::make_unique<TRejectedNetworksRequest>();
  request->direction() = TDirectionFilter::BOTH;

  auto response = folly::coro::blockingWait(
      // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
      service_->co_getRejectedNetworksDetails(std::move(request)));

  EXPECT_TRUE(response->groups()->empty());
  EXPECT_TRUE(response->by_policy()->empty());
  EXPECT_THAT(
      logHandler_->getMessageValues(),
      Contains(ContainsRegex("unsupported direction")));
}

/*
 * A regex the caller mistyped must fail the request loudly. Falling back to
 * "match everything" would silently widen a fleet-wide query.
 */
TEST_F(
    BgpServiceBaseNullPtrTestFixture,
    GetRejectedNetworksInvalidNeighborRegexTest) {
  auto request = std::make_unique<TRejectedNetworksRequest>();
  request->neighbor_name_regex() = "*invalid(";

  auto response = folly::coro::blockingWait(
      // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
      service_->co_getRejectedNetworksDetails(std::move(request)));

  EXPECT_TRUE(response->groups()->empty());
  EXPECT_EQ(0, folly::copy(response->neighbors_matched().value()));
  EXPECT_THAT(
      logHandler_->getMessageValues(),
      Contains(ContainsRegex("invalid neighbor name regex")));
}

TEST_F(
    BgpServiceBaseNullPtrTestFixture,
    GetRejectedNetworksInvalidPolicyRegexTest) {
  auto request = std::make_unique<TRejectedNetworksRequest>();
  request->policy_name_regex() = "*invalid(";

  auto response = folly::coro::blockingWait(
      // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
      service_->co_getRejectedNetworksDetails(std::move(request)));

  EXPECT_TRUE(response->groups()->empty());
  EXPECT_THAT(
      logHandler_->getMessageValues(),
      Contains(ContainsRegex("invalid policy name regex")));
}

/*
 * The well-formed-request path is not exercised here: this fixture never runs
 * the peer manager event base, so any request that survives validation blocks
 * for the full evb timeout. The direction mapping, both event base hops and all
 * of the response aggregation are covered against a real running PeerManager in
 * e2e:e2e_rejected_networks_handler_test.
 */

// test co_getSubscriberNetworkInfo
TEST_F(BgpServiceBaseNullPtrTestFixture, GetSubscriberNetworkInfoNullPtrTest) {
  folly::coro::blockingWait(service_->co_getSubscriberNetworkInfo(1, nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_shutdownSession
CO_TEST_F(BgpServiceBaseNullPtrTestFixture, ShutdownSessionNullPtrTest) {
  co_await service_->co_shutdownSession(nullptr);
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_restartSession
CO_TEST_F(BgpServiceBaseNullPtrTestFixture, RestartSessionNullPtrTest) {
  co_await service_->co_restartSession(nullptr);
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_startSession
CO_TEST_F(BgpServiceBaseNullPtrTestFixture, StartSessionNullPtrTest) {
  co_await service_->co_startSession(nullptr);
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getAdvertisedNetworksFiltered
TEST_F(
    BgpServiceBaseNullPtrTestFixture,
    GetAdvertisedNetworksFilteredNullPtrTest) {
  folly::coro::blockingWait(
      service_->co_getAdvertisedNetworksFiltered(nullptr, nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getReceivedNetworks
TEST_F(BgpServiceBaseNullPtrTestFixture, GetReceivedNetworksNullPtrTest) {
  folly::coro::blockingWait(service_->co_getReceivedNetworks(nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

// test co_getAdvertisedNetworks
TEST_F(BgpServiceBaseNullPtrTestFixture, GetAdvertisedNetworksNullPtrTest) {
  folly::coro::blockingWait(service_->co_getAdvertisedNetworks(nullptr));
  EXPECT_THAT(
      std::move(logHandler_->getMessageValues()),
      Contains(ContainsRegex(kExitNullPtrLogPrefix)));
}

TEST_F(BgpServiceTestFixture, GetMonitoredQueueSizesTest) {
  QueueSizeMapT ret;
  std::vector<std::string> paths = {};

  // Nothing is monitored yet
  {
    service_->getMonitoredQueueSizes(
        ret, std::make_unique<std::vector<std::string>>(paths));

    EXPECT_EQ(ret.size(), 0);
  }

  // Add a monitored queue
  {
    MonitoredModule module;
    MonitoredQueue<std::deque<int>> queue;
    module.monitorQueue("queue", queue, MonitorableQueueTrace::Direction::IN);
    watchdog_->monitorModule("module", module);

    service_->getMonitoredQueueSizes(
        ret, std::make_unique<std::vector<std::string>>(paths));

    EXPECT_EQ(ret.size(), 1);
  }
}

/**
 * Test thrift API call is allowed within allowed execution window of
 * active requests where requests are called one after the other
 */
TEST_F(BgpServiceTestFixture, ThriftSequentialAllowedTest) {
  BgpStats::initCounters();
  service_->setThriftProtection(true);

  /*
   * Thrift call 1
   */
  auto tmpThread1 = std::thread([&]() {
    EXPECT_TRUE(service_->continueExecution(true));
    service_->decrRequestsInExecution();
  });

  /*
   * Thrift call 2
   */
  EXPECT_TRUE(service_->continueExecution(true));
  service_->decrRequestsInExecution();

  /*
   * Thrift call 3
   */
  auto tmpThread2 = std::thread([&]() {
    EXPECT_TRUE(service_->continueExecution(true));
    service_->decrRequestsInExecution();
  });

  tmpThread1.join();
  tmpThread2.join();

  fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_EQ(
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kThriftReject),
      0);
  EXPECT_EQ(
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kThriftSuspend),
      0);
}

/**
 * Test thrift API call is allowed within allowed execution window of
 * active requests where requests are overlapped
 */
TEST_F(BgpServiceTestFixture, ThriftParallelAllowedTest) {
  BgpStats::initCounters();
  folly::EventBase testEvb;
  service_->setThriftProtection(true);

  /*
   * Thrift call 1
   */
  service_->continueExecution(true);

  /*
   * Thrift call 2 without completing 1st call
   */
  auto tmpThread = std::thread([&]() {
    EXPECT_TRUE(service_->continueExecution(true));
    service_->decrRequestsInExecution();
  });

  /*
   * Sleep few ms to allow Thrift call 2 to kick in
   */
  testEvb.scheduleAt(
      [&]() noexcept { service_->decrRequestsInExecution(); },
      std::chrono::steady_clock::now() + std::chrono::milliseconds(2));

  testEvb.loop();
  tmpThread.join();

  /*
   * Thrift call 3 after completing call 1 and 2
   */
  EXPECT_TRUE(service_->continueExecution(true));

  fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_EQ(
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kThriftReject),
      0);
  EXPECT_EQ(
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kThriftSuspend),
      0);
}

/**
 * Test thrift API call is allowed when called to not enforce dampening
 */
TEST_F(BgpServiceTestFixture, ThriftParallelNoDampeningTest) {
  BgpStats::initCounters();
  folly::EventBase testEvb;
  service_->setThriftProtection(true);

  /*
   * Thrift call 1
   */
  service_->continueExecution(true);

  /*
   * Thrift call 2 without completing 1st call
   */
  std::thread tmpThread;
  testEvb.scheduleAt(
      [&]() noexcept {
        tmpThread = std::thread([&]() {
          EXPECT_TRUE(service_->continueExecution(false));
          service_->decrRequestsInExecution();
        });
      },
      std::chrono::steady_clock::now() +
          std::chrono::milliseconds(service_->getAllowedBufferWindow()));

  /*
   * Sleep few ms to allow Thrift call 2 to kick in
   */
  testEvb.scheduleAt(
      [&]() noexcept { service_->decrRequestsInExecution(); },
      std::chrono::steady_clock::now() +
          std::chrono::milliseconds(service_->getAllowedBufferWindow()));

  testEvb.loop();
  tmpThread.join();

  /*
   * Thrift call 3 after completing call 1 and 2
   */
  testEvb.scheduleAt(
      [&]() { EXPECT_TRUE(service_->continueExecution(true)); },
      std::chrono::steady_clock::now() +
          std::chrono::milliseconds(service_->getIdleWindow_() + 1));
  testEvb.loop();

  fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_EQ(
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kThriftReject),
      0);
  EXPECT_EQ(
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kThriftSuspend),
      0);
}

/**
 * Test thrift API call is suspended beyond allowed execution window of
 * active requests
 */
TEST_F(BgpServiceTestFixture, ThriftParallelDampeningFalseTest) {
  BgpStats::initCounters();
  folly::EventBase testEvb;

  service_->continueExecution(true);

  std::thread tmpThread;
  testEvb.scheduleAt(
      [&]() {
        tmpThread = std::thread(
            [&]() { EXPECT_TRUE(service_->continueExecution(true)); });
      },
      std::chrono::steady_clock::now() +
          std::chrono::milliseconds(service_->getAllowedBufferWindow()));

  testEvb.scheduleAt(
      [&]() { service_->decrRequestsInExecution(); },
      std::chrono::steady_clock::now() +
          std::chrono::milliseconds(service_->getAllowedBufferWindow()));
  testEvb.loop();
  tmpThread.join();

  fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_EQ(
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kThriftReject),
      0);
  EXPECT_EQ(
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kThriftSuspend),
      0);
}

/**
 * Test thrift API call is suspended beyond allowed execution window of
 * active requests
 */
TEST_F(BgpServiceTestFixture, ThriftParallelDampeningTest) {
  BgpStats::initCounters();
  service_->setThriftProtection(true);

  folly::EventBase testEvb;
  service_->continueExecution(true);
  std::thread tmpThread;
  testEvb.scheduleAt(
      [&]() {
        tmpThread = std::thread(
            [&]() { EXPECT_TRUE(service_->continueExecution(true)); });
      },
      std::chrono::steady_clock::now() +
          std::chrono::milliseconds(service_->getAllowedBufferWindow()));

  testEvb.scheduleAt(
      [&]() { service_->decrRequestsInExecution(); },
      std::chrono::steady_clock::now() +
          std::chrono::milliseconds(service_->getAllowedBufferWindow()));

  testEvb.loop();
  tmpThread.join();

  fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_EQ(
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kThriftReject),
      0);
  EXPECT_GT(
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kThriftSuspend),
      0);
}

/**
 * Test thrift API call is suspended beyond allowed execution window of
 * active requests, and eventually rejected as a result of suspended for
 * too long
 */
TEST_F(BgpServiceTestFixture, ThriftParallelRejectionTest) {
  BgpStats::initCounters();
  service_->setThriftProtection(true);
  service_->setRejectRequestWindow(1000);
  folly::EventBase testEvb;

  service_->continueExecution(true);

  std::thread tmpThread;
  testEvb.scheduleAt(
      [&]() {
        tmpThread = std::thread(
            [&]() { EXPECT_FALSE(service_->continueExecution(true)); });
      },
      std::chrono::steady_clock::now() +
          std::chrono::milliseconds(service_->getAllowedBufferWindow()));

  testEvb.scheduleAt(
      [&]() { service_->decrRequestsInExecution(); },
      std::chrono::steady_clock::now() +
          std::chrono::milliseconds(
              service_->getAllowedBufferWindow() +
              service_->getRejectRequestWindow()));

  testEvb.loop();
  tmpThread.join();

  fb303::ThreadCachedServiceData::get()->publishStats();
  EXPECT_GT(
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kThriftReject),
      0);
  EXPECT_GT(
      fb303::ThreadCachedServiceData::get()->getCounter(
          BgpStats::kThriftSuspend),
      0);
}

// Test co_getEntryStats endpoint
TEST_F(BgpServiceTestFixture, GetEntryStatsTest) {
  auto ribThread = rib_->runInThread();

  auto stats = folly::coro::blockingWait(service_->co_getEntryStats());
  EXPECT_EQ(*stats->total_ucast_routes(), 0);
  EXPECT_EQ(*stats->total_rib_paths(), 0);
  EXPECT_EQ(*stats->total_adj_ribs(), 0);
  EXPECT_EQ(*stats->total_originated_routes(), 0);
  EXPECT_EQ(*stats->total_shadow_rib_entries(), 0);
  EXPECT_EQ(*stats->total_netlink_wrapper_interfaces(), 0);
  rib_->stop();
  ribThread.join();
}

/*
 * Test co_getPolicyStats dispatches to PeerManagerBase evb and returns empty
 * stats
 */
TEST_F(BgpServiceTestFixture, GetPolicyStatsTest) {
  auto peerMgrThread = peerManager_->runInThread();

  auto stats = folly::coro::blockingWait(service_->co_getPolicyStats());
  EXPECT_TRUE(stats->policy_statement_stats()->empty());

  peerManager_->stop();
  peerMgrThread.join();
}

// Test getNexthopInfoForNexthop with invalid nexthop
TEST_F(BgpServiceTestFixture, GetNexthopInfoForNexthopInvalidTest) {
  auto ribThread = rib_->runInThread();

  auto nexthopInfo =
      folly::coro::blockingWait(service_->co_getNexthopInfoForNexthop(
          std::make_unique<std::string>("10.0.0.1")));

  EXPECT_FALSE(
      apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          nexthopInfo->next_hop()));

  rib_->stop();
  ribThread.join();
}

// Test getNexthopInfos returns empty when the RIB nexthop cache is empty
TEST_F(BgpServiceTestFixture, GetNexthopInfosEmptyTest) {
  auto ribThread = rib_->runInThread();

  // Empty filter over an empty cache => empty result.
  auto all = folly::coro::blockingWait(service_->co_getNexthopInfos(
      std::make_unique<std::vector<std::string>>()));
  EXPECT_TRUE(all->empty());

  // Specific (unknown) nexthop => empty result.
  auto filtered = folly::coro::blockingWait(service_->co_getNexthopInfos(
      std::make_unique<std::vector<std::string>>(
          std::vector<std::string>{"10.0.0.1"})));
  EXPECT_TRUE(filtered->empty());

  rib_->stop();
  ribThread.join();
}

/*
 * A non-empty request made up solely of invalid nexthop strings must return
 * empty rather than falling through to the list-all path (an empty filter =
 * every cache entry). Exercises the invalid-input guard + drop path.
 */
TEST_F(BgpServiceTestFixture, GetNexthopInfosAllInvalidTest) {
  auto ribThread = rib_->runInThread();

  auto result = folly::coro::blockingWait(service_->co_getNexthopInfos(
      std::make_unique<std::vector<std::string>>(
          std::vector<std::string>{"not-an-ip", ""})));
  EXPECT_TRUE(result->empty());

  rib_->stop();
  ribThread.join();
}

/*
 * Device-wide clearCounters completes cleanly over an empty peer set and
 * exercises the SessionManager (socket tx/rx) + PeerManager (AdjRib/fb303)
 * clear paths through the handler, in both directions.
 */
TEST_F(BgpServiceTestFixture, ClearCountersTest) {
  auto peerMgrThread = peerManager_->runInThread();
  auto sessionMgrThread = sessionMgr_->runInThread();

  folly::coro::blockingWait(
      service_->co_clearCounters(std::make_unique<std::vector<std::string>>()));

  peerManager_->stop();
  sessionMgr_->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
}

/*
 * Peer-scoped clearCounters filters to the given IP(s); an address with no
 * matching peer is a no-op and still completes cleanly through both the socket
 * and AdjRib clear paths.
 */
TEST_F(BgpServiceTestFixture, ClearCountersForPeerTest) {
  auto peerMgrThread = peerManager_->runInThread();
  auto sessionMgrThread = sessionMgr_->runInThread();

  folly::coro::blockingWait(service_->co_clearCounters(
      std::make_unique<std::vector<std::string>>(
          std::vector<std::string>{"2401:db00::1"})));

  peerManager_->stop();
  sessionMgr_->stop();
  peerMgrThread.join();
  sessionMgrThread.join();
}

/**
 * [CRF File Mode Tests]
 */
class BgpServiceCrfFileModeTestFixture
    : public BgpServicePeerGroupValidationTestFixture {
 public:
  void SetUp() override {
    BgpServicePeerGroupValidationTestFixture::SetUp();
    crfTmpFile_ =
        fmt::format("/tmp/bgp_service_crf_test_artifact_{}.json", getpid());
    FLAGS_crf_policy_file = crfTmpFile_;
  }

  void TearDown() override {
    boost::filesystem::remove(crfTmpFile_);
    BgpServicePeerGroupValidationTestFixture::TearDown();
  }

  void writeCrfArtifact(bool dryrun, int64_t version = 1) {
    rib_policy::CrfPolicyArtifact artifact;
    artifact.dryrun() = dryrun;
    rib_policy::TRouteFilterPolicy policy;
    policy.version() = version;
    artifact.policy() = policy;

    folly::writeFileAtomic(
        crfTmpFile_,
        apache::thrift::SimpleJSONSerializer::serialize<std::string>(artifact));
  }

  std::string crfTmpFile_;
};

CO_TEST_F(
    BgpServiceCrfFileModeTestFixture,
    SetCrfPolicyFromFileHappyPathDryrunFalse) {
  writeCrfArtifact(/*dryrun=*/false);

  /*
   * Remove non-thread-safe TestLogHandler before starting worker threads
   * to avoid TSAN data races on the handler's message vector.
   */
  folly::LoggerDB::get().getCategory("")->clearHandlers();

  auto peerMgrThread = peerManager_->runInThread();
  auto sessionMgrThread = sessionMgr_->runInThread();
  auto ribThread = rib_->runInThread();
  SCOPE_EXIT {
    rib_->stop();
    peerManager_->stop();
    sessionMgr_->stop();
    ribThread.join();
    peerMgrThread.join();
    sessionMgrThread.join();
  };

  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto ret = co_await service_->co_setCrfPolicyFromFile();

  EXPECT_TRUE(*ret->success());
  EXPECT_THAT(*ret->err(), HasSubstr("FILE_MODE"));
  EXPECT_TRUE(rib_->isCrfFileModeEnabled());
}

/*
 * Behavioral check: a successful file-mode refresh not only returns success and
 * flips the mode flag, but the artifact's policy actually reaches the RIB. We
 * wait for the enqueued policy to drain on the rib evb and assert the installed
 * route-filter policy version matches the artifact.
 */
CO_TEST_F(
    BgpServiceCrfFileModeTestFixture,
    SetCrfPolicyFromFileAppliesPolicyToRib) {
  writeCrfArtifact(/*dryrun=*/false, /*version=*/100);

  folly::LoggerDB::get().getCategory("")->clearHandlers();
  auto peerMgrThread = peerManager_->runInThread();
  auto sessionMgrThread = sessionMgr_->runInThread();
  auto ribThread = rib_->runInThread();
  SCOPE_EXIT {
    rib_->stop();
    peerManager_->stop();
    sessionMgr_->stop();
    ribThread.join();
    peerMgrThread.join();
    sessionMgrThread.join();
  };

  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto ret = co_await service_->co_setCrfPolicyFromFile();
  CO_ASSERT_TRUE(*ret->success());
  EXPECT_TRUE(rib_->isCrfFileModeEnabled());

  /*
   * Block until the enqueued policy is consumed and applied on the rib evb,
   * then assert the installed version is exactly what the artifact carried.
   */
  rib_->waitForRouteFilterPolicyUpdate();
  EXPECT_EQ(100, rib_->getRouteFilterPolicyVersion());
}

/*
 * A later refresh carrying a newer artifact version re-applies through the file
 * path and the RIB ends up with the newer version. version is part of policy
 * equality, so forceUpdate installs it. This exercises the artifact reaching
 * the RIB across successive refreshes (the read now happens under the lock, so
 * a concurrent COOP rewrite cannot let an older read win).
 */
CO_TEST_F(
    BgpServiceCrfFileModeTestFixture,
    SetCrfPolicyFromFileNewerVersionReapplied) {
  writeCrfArtifact(/*dryrun=*/false, /*version=*/100);

  folly::LoggerDB::get().getCategory("")->clearHandlers();
  auto peerMgrThread = peerManager_->runInThread();
  auto sessionMgrThread = sessionMgr_->runInThread();
  auto ribThread = rib_->runInThread();
  SCOPE_EXIT {
    rib_->stop();
    peerManager_->stop();
    sessionMgr_->stop();
    ribThread.join();
    peerMgrThread.join();
    sessionMgrThread.join();
  };

  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto ret1 = co_await service_->co_setCrfPolicyFromFile();
  CO_ASSERT_TRUE(*ret1->success());
  rib_->waitForRouteFilterPolicyUpdate();
  EXPECT_EQ(100, rib_->getRouteFilterPolicyVersion());

  writeCrfArtifact(/*dryrun=*/false, /*version=*/200);
  auto replaceFuture = rib_->getRibPolicyReplaceFuture();
  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto ret2 = co_await service_->co_setCrfPolicyFromFile();
  CO_ASSERT_TRUE(*ret2->success());
  replaceFuture.wait();
  EXPECT_EQ(200, rib_->getRouteFilterPolicyVersion());
}

CO_TEST_F(BgpServiceCrfFileModeTestFixture, SetCrfPolicyFromFileDryrunTrue) {
  writeCrfArtifact(/*dryrun=*/true);

  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto ret = co_await service_->co_setCrfPolicyFromFile();

  EXPECT_TRUE(*ret->success());
  EXPECT_THAT(*ret->err(), HasSubstr("THRIFT_MODE"));
  EXPECT_FALSE(rib_->isCrfFileModeEnabled());
}

CO_TEST_F(BgpServiceCrfFileModeTestFixture, SetCrfPolicyFromFileNoArtifact) {
  FLAGS_crf_policy_file = "/tmp/nonexistent_bgp_service_crf_test_file";
  boost::filesystem::remove(FLAGS_crf_policy_file);

  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto ret = co_await service_->co_setCrfPolicyFromFile();

  EXPECT_FALSE(*ret->success());
}

CO_TEST_F(
    BgpServiceCrfFileModeTestFixture,
    SetRouteFilterPolicyRejectedInFileMode) {
  rib_->setCrfFileModeEnabled(true);

  rib_policy::TRouteFilterPolicy tPolicy;
  tPolicy.version() = 1;

  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto res = co_await service_->co_setRouteFilterPolicy(
      std::make_unique<rib_policy::TRouteFilterPolicy>(tPolicy));

  EXPECT_FALSE(*res->success());
  EXPECT_THAT(*res->err(), HasSubstr("FILE_MODE"));
}

CO_TEST_F(
    BgpServiceCrfFileModeTestFixture,
    ClearRouteFilterPolicySkippedInFileMode) {
  // First apply a CRF policy via FILE_MODE
  writeCrfArtifact(/*dryrun=*/false, /*version=*/42);

  folly::LoggerDB::get().getCategory("")->clearHandlers();
  auto peerMgrThread = peerManager_->runInThread();
  auto sessionMgrThread = sessionMgr_->runInThread();
  auto ribThread = rib_->runInThread();
  SCOPE_EXIT {
    rib_->stop();
    peerManager_->stop();
    sessionMgr_->stop();
    ribThread.join();
    peerMgrThread.join();
    sessionMgrThread.join();
  };

  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto ret = co_await service_->co_setCrfPolicyFromFile();
  CO_ASSERT_TRUE(*ret->success());
  EXPECT_TRUE(rib_->isCrfFileModeEnabled());

  // Try to clear — should be silently skipped in FILE_MODE
  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  co_await service_->co_clearRouteFilterPolicy();

  // Verify FILE_MODE is still active and policy was NOT cleared
  EXPECT_TRUE(rib_->isCrfFileModeEnabled());
}

// Validation failure: artifact policy references a non-existent peer group
CO_TEST_F(
    BgpServiceCrfFileModeTestFixture,
    SetCrfPolicyFromFileValidationFailure) {
  // Write an artifact with key_type=PEER_GROUP_NAME and a non-existent group
  rib_policy::CrfPolicyArtifact artifact;
  artifact.dryrun() = false;
  rib_policy::TRouteFilterPolicy policy;
  policy.version() = 1;
  policy.key_type() = rib_policy::KeyType::PEER_GROUP_NAME;
  policy.statements()->emplace(
      "nonexistent-peer-group", createTRouteFilterStatement({}, false, true));
  artifact.policy() = policy;

  folly::writeFileAtomic(
      crfTmpFile_,
      apache::thrift::SimpleJSONSerializer::serialize<std::string>(artifact));

  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto ret = co_await service_->co_setCrfPolicyFromFile();

  EXPECT_FALSE(*ret->success());
  EXPECT_THAT(*ret->err(), HasSubstr("PEER_GROUP_NOT_FOUND"));
  // FILE_MODE should NOT be set on validation failure
  EXPECT_FALSE(rib_->isCrfFileModeEnabled());
}

// Full lifecycle: enable FILE_MODE → reject Thrift → disable → accept Thrift
CO_TEST_F(
    BgpServiceCrfFileModeTestFixture,
    CrfFileModeCycleEnableRejectDisableAccept) {
  // Step 1: Enable FILE_MODE via setCrfPolicyFromFile(dryrun=false)
  writeCrfArtifact(/*dryrun=*/false);

  folly::LoggerDB::get().getCategory("")->clearHandlers();
  auto peerMgrThread = peerManager_->runInThread();
  auto sessionMgrThread = sessionMgr_->runInThread();
  auto ribThread = rib_->runInThread();
  SCOPE_EXIT {
    rib_->stop();
    peerManager_->stop();
    sessionMgr_->stop();
    ribThread.join();
    peerMgrThread.join();
    sessionMgrThread.join();
  };

  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto ret = co_await service_->co_setCrfPolicyFromFile();
  CO_ASSERT_TRUE(*ret->success());
  EXPECT_TRUE(rib_->isCrfFileModeEnabled());

  // Step 2: Thrift setRouteFilterPolicy should be rejected
  rib_policy::TRouteFilterPolicy tPolicy;
  tPolicy.version() = 99;
  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto setRet = co_await service_->co_setRouteFilterPolicy(
      std::make_unique<rib_policy::TRouteFilterPolicy>(tPolicy));
  EXPECT_FALSE(*setRet->success());
  EXPECT_THAT(*setRet->err(), HasSubstr("FILE_MODE"));

  // Step 3: Disable FILE_MODE via setCrfPolicyFromFile(dryrun=true)
  writeCrfArtifact(/*dryrun=*/true);
  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto disableRet = co_await service_->co_setCrfPolicyFromFile();
  EXPECT_TRUE(*disableRet->success());
  EXPECT_FALSE(rib_->isCrfFileModeEnabled());

  // Step 4: Thrift setRouteFilterPolicy should now succeed
  rib_policy::TRouteFilterPolicy tPolicy2;
  tPolicy2.version() = 50;
  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto acceptRet = co_await service_->co_setRouteFilterPolicy(
      std::make_unique<rib_policy::TRouteFilterPolicy>(tPolicy2));
  EXPECT_TRUE(*acceptRet->success());
}

/*
 * Concurrency: file-mode refreshes racing Thrift CRF updates must be serialized
 * by crfPolicyMutex_ (the file API takes the exclusive lock; the Thrift API
 * takes a shared lock and is gated on FILE_MODE). The fleet of operations runs
 * in parallel on a multi-threaded executor; the test asserts there is no crash,
 * deadlock, or data race (under TSAN) and that the service is left in a
 * consistent state.
 */
CO_TEST_F(
    BgpServiceCrfFileModeTestFixture,
    CrfFileModeConcurrentThriftAndFileMode) {
  // dryrun=false → every file-mode refresh enables and applies FILE_MODE.
  writeCrfArtifact(/*dryrun=*/false);

  /*
   * Remove non-thread-safe TestLogHandler before starting worker threads to
   * avoid TSAN data races on the handler's message vector.
   */
  folly::LoggerDB::get().getCategory("")->clearHandlers();

  auto peerMgrThread = peerManager_->runInThread();
  auto sessionMgrThread = sessionMgr_->runInThread();
  auto ribThread = rib_->runInThread();
  SCOPE_EXIT {
    rib_->stop();
    peerManager_->stop();
    sessionMgr_->stop();
    ribThread.join();
    peerMgrThread.join();
    sessionMgrThread.join();
  };

  constexpr int kIters = 25;
  folly::CPUThreadPoolExecutor exec(4);

  /*
   * Interleave file-mode refreshes and Thrift CRF updates so they contend for
   * crfPolicyMutex_. Both RPCs return std::unique_ptr<TResult>.
   */
  std::vector<folly::coro::Task<std::unique_ptr<TResult>>> tasks;
  tasks.reserve(kIters * 2);
  for (int i = 0; i < kIters; ++i) {
    // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
    tasks.push_back(service_->co_setCrfPolicyFromFile());

    rib_policy::TRouteFilterPolicy tPolicy;
    tPolicy.version() = 1000 + i;
    // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
    tasks.push_back(service_->co_setRouteFilterPolicy(
        std::make_unique<rib_policy::TRouteFilterPolicy>(tPolicy)));
  }

  auto results = co_await folly::coro::co_withExecutor(
      &exec, folly::coro::collectAllRange(std::move(tasks)));

  /*
   * Every operation completed with a well-formed result. A Thrift update either
   * succeeded (it ran before FILE_MODE took effect) or was gracefully rejected
   * with a FILE_MODE error — never UB or a partial/garbage result.
   */
  for (const auto& res : results) {
    CO_ASSERT_NE(res, nullptr);
    if (!*res->success()) {
      EXPECT_THAT(*res->err(), HasSubstr("FILE_MODE"));
    }
  }

  /*
   * Thrift updates never clear FILE_MODE, so once any refresh has run the
   * terminal state is deterministically FILE_MODE enabled.
   */
  EXPECT_TRUE(rib_->isCrfFileModeEnabled());
}

/*
 * Soak: file-mode refreshes racing a concurrent artifact rewrite. COOP can
 * rewrite the artifact file at any time; the read now happens under
 * crfPolicyMutex_, so read + RIB enqueue are atomic. A background writer
 * rewrites the artifact to monotonically increasing versions while a fleet of
 * co_setCrfPolicyFromFile() refreshes read it in parallel. The test asserts
 * there is no crash, deadlock, or data race (under TSAN), and that after the
 * churn a final serial refresh of a known-highest version converges the RIB to
 * exactly that version.
 *
 * NOTE: this is a nondeterministic soak. It does NOT force the specific
 * stale-read interleaving that crfPolicyMutex_ guards against (an older read
 * enqueued after a newer one) — deterministically reproducing that would need a
 * test seam between the artifact read and the RIB enqueue, which does not
 * exist. This exercises the concurrent read-under-lock path and terminal
 * convergence only; it is not a regression guard for the reordering bug itself.
 */
CO_TEST_F(
    BgpServiceCrfFileModeTestFixture,
    CrfFileModeConcurrentRefreshWithArtifactRewrite) {
  writeCrfArtifact(/*dryrun=*/false, /*version=*/1);

  /*
   * Remove non-thread-safe TestLogHandler before starting worker threads to
   * avoid TSAN data races on the handler's message vector.
   */
  folly::LoggerDB::get().getCategory("")->clearHandlers();

  auto peerMgrThread = peerManager_->runInThread();
  auto sessionMgrThread = sessionMgr_->runInThread();
  auto ribThread = rib_->runInThread();
  SCOPE_EXIT {
    rib_->stop();
    peerManager_->stop();
    sessionMgr_->stop();
    ribThread.join();
    peerMgrThread.join();
    sessionMgrThread.join();
  };

  constexpr int kIters = 25;
  folly::CPUThreadPoolExecutor exec(4);

  /*
   * Background writer atomically rewrites the artifact to increasing versions
   * (mirroring COOP's writeFileAtomic) for the duration of the refresh fleet,
   * so refreshes read the file while it is changing underneath them. Versions
   * stay below kFinalVersion so the terminal convergence check is unambiguous.
   */
  std::atomic<bool> writerStop{false};
  std::thread writer([&] {
    int64_t version = 1;
    while (!writerStop.load(std::memory_order_relaxed)) {
      writeCrfArtifact(/*dryrun=*/false, /*version=*/++version);
    }
  });

  std::vector<folly::coro::Task<std::unique_ptr<TResult>>> tasks;
  tasks.reserve(kIters);
  for (int i = 0; i < kIters; ++i) {
    // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
    tasks.push_back(service_->co_setCrfPolicyFromFile());
  }

  auto results = co_await folly::coro::co_withExecutor(
      &exec, folly::coro::collectAllRange(std::move(tasks)));

  writerStop.store(true, std::memory_order_relaxed);
  writer.join();

  /*
   * Every refresh completed with a well-formed result. dryrun=false always
   * enables and applies FILE_MODE, so each refresh succeeds — never UB or a
   * partial/garbage result.
   */
  for (const auto& res : results) {
    CO_ASSERT_NE(res, nullptr);
    EXPECT_TRUE(*res->success());
  }

  /*
   * After the churn, a final serial refresh of a known-highest version must
   * converge the RIB to exactly that version: with no concurrent writer or
   * refresh, the read-under-lock installs the freshest on-disk artifact.
   */
  constexpr int64_t kFinalVersion = 1'000'000;
  writeCrfArtifact(/*dryrun=*/false, /*version=*/kFinalVersion);
  auto replaceFuture = rib_->getRibPolicyReplaceFuture();
  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto finalRet = co_await service_->co_setCrfPolicyFromFile();
  CO_ASSERT_TRUE(*finalRet->success());
  replaceFuture.wait();
  EXPECT_EQ(kFinalVersion, rib_->getRouteFilterPolicyVersion());

  EXPECT_TRUE(rib_->isCrfFileModeEnabled());
}

/*
 * Assert the CRF file-mode ODS SUM counters move on a real apply. A successful
 * file-mode apply bumps artifact-read + policy-applied success (the latter now
 * counted at the RIB apply, not at enqueue, so the coalescing MergeQueue does
 * not inflate it), and a Thrift setRouteFilterPolicy rejected while in
 * FILE_MODE bumps thrift_rpc_rejected. SUM stats accumulate process-wide across
 * tests, so we assert deltas.
 */
CO_TEST_F(BgpServiceCrfFileModeTestFixture, CrfFileModeOdsCountersIncrement) {
  BgpStatsDC::initCounters();
  auto counters = fb303::ThreadCachedServiceData::getShared();
  // SUM-type timeseries stats are read back with the ".sum" suffix.
  const std::string kReadSuccessSum =
      std::string(BgpStatsDC::kCrfArtifactReadSuccess) + ".sum";
  const std::string kAppliedSuccessSum =
      std::string(BgpStatsDC::kCrfPolicyAppliedSuccess) + ".sum";
  const std::string kThriftRejectedSum =
      std::string(BgpStatsDC::kCrfThriftRpcRejected) + ".sum";

  /*
   * A ".sum" key does not exist until its stat is first added, and SUM stats
   * accumulate across earlier tests in this process, so treat missing as 0 and
   * compare deltas rather than absolute values.
   */
  auto sumOr0 = [&](const std::string& key) {
    return counters->hasCounter(key) ? counters->getCounter(key) : 0;
  };

  counters->publishStats();
  const auto readSuccessBefore = sumOr0(kReadSuccessSum);
  const auto appliedSuccessBefore = sumOr0(kAppliedSuccessSum);
  const auto thriftRejectedBefore = sumOr0(kThriftRejectedSum);

  writeCrfArtifact(/*dryrun=*/false, /*version=*/7);

  folly::LoggerDB::get().getCategory("")->clearHandlers();
  auto peerMgrThread = peerManager_->runInThread();
  auto sessionMgrThread = sessionMgr_->runInThread();
  auto ribThread = rib_->runInThread();
  SCOPE_EXIT {
    rib_->stop();
    peerManager_->stop();
    sessionMgr_->stop();
    ribThread.join();
    peerMgrThread.join();
    sessionMgrThread.join();
  };

  // Successful file-mode apply -> artifact-read + policy-applied success.
  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto ret = co_await service_->co_setCrfPolicyFromFile();
  CO_ASSERT_TRUE(*ret->success());
  rib_->waitForRouteFilterPolicyUpdate();

  // A Thrift set rejected in FILE_MODE -> thrift_rpc_rejected.
  rib_policy::TRouteFilterPolicy tPolicy;
  tPolicy.version() = 8;
  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto rejectRet = co_await service_->co_setRouteFilterPolicy(
      std::make_unique<rib_policy::TRouteFilterPolicy>(tPolicy));
  EXPECT_FALSE(*rejectRet->success());

  counters->publishStats();
  EXPECT_EQ(readSuccessBefore + 1, sumOr0(kReadSuccessSum));
  EXPECT_EQ(appliedSuccessBefore + 1, sumOr0(kAppliedSuccessSum));
  EXPECT_EQ(thriftRejectedBefore + 1, sumOr0(kThriftRejectedSum));
}

class BgpServiceCpsFileModeTestFixture
    : public BgpServicePeerGroupValidationTestFixture {
 public:
  void SetUp() override {
    BgpServicePeerGroupValidationTestFixture::SetUp();
    cpsTmpFile_ =
        fmt::format("/tmp/bgp_service_cps_test_artifact_{}.json", getpid());
    FLAGS_cps_policy_file = cpsTmpFile_;
  }

  void TearDown() override {
    boost::filesystem::remove(cpsTmpFile_);
    BgpServicePeerGroupValidationTestFixture::TearDown();
  }

  void writeCpsArtifact(bool dryrun, int64_t version = 1) {
    rib_policy::CpsPolicyArtifact artifact;
    artifact.dryrun() = dryrun;
    rib_policy::TPathSelectionPolicy policy;
    policy.version() = version;
    artifact.policy() = policy;

    folly::writeFileAtomic(
        cpsTmpFile_,
        apache::thrift::SimpleJSONSerializer::serialize<std::string>(artifact));
  }

  std::string cpsTmpFile_;
};

CO_TEST_F(
    BgpServiceCpsFileModeTestFixture,
    SetCpsPolicyFromFileHappyPathDryrunFalse) {
  writeCpsArtifact(/*dryrun=*/false);

  /*
   * Remove non-thread-safe TestLogHandler before starting worker threads
   * to avoid TSAN data races on the handler's message vector.
   */
  folly::LoggerDB::get().getCategory("")->clearHandlers();

  auto peerMgrThread = peerManager_->runInThread();
  auto sessionMgrThread = sessionMgr_->runInThread();
  auto ribThread = rib_->runInThread();
  SCOPE_EXIT {
    rib_->stop();
    peerManager_->stop();
    sessionMgr_->stop();
    ribThread.join();
    peerMgrThread.join();
    sessionMgrThread.join();
  };

  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto ret = co_await service_->co_setCpsPolicyFromFile();

  EXPECT_TRUE(*ret->success());
  EXPECT_THAT(*ret->err(), HasSubstr("FILE_MODE"));
  EXPECT_TRUE(rib_->isCpsFileModeEnabled());
}

/*
 * Behavioral check: a successful file-mode refresh not only returns success and
 * flips the mode flag, but the artifact's policy actually reaches the RIB. We
 * wait for the enqueued policy to drain on the rib evb and assert the installed
 * path-selection policy version matches the artifact.
 */
CO_TEST_F(
    BgpServiceCpsFileModeTestFixture,
    SetCpsPolicyFromFileAppliesPolicyToRib) {
  writeCpsArtifact(/*dryrun=*/false, /*version=*/100);

  folly::LoggerDB::get().getCategory("")->clearHandlers();
  auto peerMgrThread = peerManager_->runInThread();
  auto sessionMgrThread = sessionMgr_->runInThread();
  auto ribThread = rib_->runInThread();
  SCOPE_EXIT {
    rib_->stop();
    peerManager_->stop();
    sessionMgr_->stop();
    ribThread.join();
    peerMgrThread.join();
    sessionMgrThread.join();
  };

  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto ret = co_await service_->co_setCpsPolicyFromFile();
  CO_ASSERT_TRUE(*ret->success());
  EXPECT_TRUE(rib_->isCpsFileModeEnabled());

  /*
   * Block until the enqueued policy is consumed and applied on the rib evb,
   * then assert the installed version is exactly what the artifact carried.
   */
  rib_->waitForPathSelectionPolicyUpdate();
  EXPECT_EQ(100, rib_->getPathSelectionPolicyVersion());
}

/*
 * A later refresh carrying a newer artifact version re-applies through the file
 * path and the RIB ends up with the newer version. version is part of policy
 * equality, so forceUpdate installs it. This exercises the artifact reaching
 * the RIB across successive refreshes (the read now happens under the lock, so
 * a concurrent COOP rewrite cannot let an older read win).
 */
CO_TEST_F(
    BgpServiceCpsFileModeTestFixture,
    SetCpsPolicyFromFileNewerVersionReapplied) {
  writeCpsArtifact(/*dryrun=*/false, /*version=*/100);

  folly::LoggerDB::get().getCategory("")->clearHandlers();
  auto peerMgrThread = peerManager_->runInThread();
  auto sessionMgrThread = sessionMgr_->runInThread();
  auto ribThread = rib_->runInThread();
  SCOPE_EXIT {
    rib_->stop();
    peerManager_->stop();
    sessionMgr_->stop();
    ribThread.join();
    peerMgrThread.join();
    sessionMgrThread.join();
  };

  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto ret1 = co_await service_->co_setCpsPolicyFromFile();
  CO_ASSERT_TRUE(*ret1->success());
  rib_->waitForPathSelectionPolicyUpdate();
  EXPECT_EQ(100, rib_->getPathSelectionPolicyVersion());

  writeCpsArtifact(/*dryrun=*/false, /*version=*/200);
  auto replaceFuture = rib_->getRibPolicyReplaceFuture();
  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto ret2 = co_await service_->co_setCpsPolicyFromFile();
  CO_ASSERT_TRUE(*ret2->success());
  replaceFuture.wait();
  EXPECT_EQ(200, rib_->getPathSelectionPolicyVersion());
}

CO_TEST_F(BgpServiceCpsFileModeTestFixture, SetCpsPolicyFromFileDryrunTrue) {
  writeCpsArtifact(/*dryrun=*/true);

  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto ret = co_await service_->co_setCpsPolicyFromFile();

  EXPECT_TRUE(*ret->success());
  EXPECT_THAT(*ret->err(), HasSubstr("THRIFT_MODE"));
  EXPECT_FALSE(rib_->isCpsFileModeEnabled());
}

CO_TEST_F(BgpServiceCpsFileModeTestFixture, SetCpsPolicyFromFileNoArtifact) {
  FLAGS_cps_policy_file = "/tmp/nonexistent_bgp_service_cps_test_file";
  boost::filesystem::remove(FLAGS_cps_policy_file);

  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto ret = co_await service_->co_setCpsPolicyFromFile();

  EXPECT_FALSE(*ret->success());
}

CO_TEST_F(
    BgpServiceCpsFileModeTestFixture,
    SetPathSelectionPolicyRejectedInFileMode) {
  rib_->setCpsFileModeEnabled(true);

  rib_policy::TPathSelectionPolicy tPolicy;
  tPolicy.version() = 1;

  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto res = co_await service_->co_setPathSelectionPolicy(
      std::make_unique<rib_policy::TPathSelectionPolicy>(tPolicy));

  EXPECT_FALSE(*res->success());
  EXPECT_THAT(*res->err(), HasSubstr("FILE_MODE"));
}

CO_TEST_F(
    BgpServiceCpsFileModeTestFixture,
    ClearPathSelectionPolicySkippedInFileMode) {
  // First apply a CPS policy via FILE_MODE
  writeCpsArtifact(/*dryrun=*/false, /*version=*/42);

  folly::LoggerDB::get().getCategory("")->clearHandlers();
  auto peerMgrThread = peerManager_->runInThread();
  auto sessionMgrThread = sessionMgr_->runInThread();
  auto ribThread = rib_->runInThread();
  SCOPE_EXIT {
    rib_->stop();
    peerManager_->stop();
    sessionMgr_->stop();
    ribThread.join();
    peerMgrThread.join();
    sessionMgrThread.join();
  };

  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto ret = co_await service_->co_setCpsPolicyFromFile();
  CO_ASSERT_TRUE(*ret->success());
  EXPECT_TRUE(rib_->isCpsFileModeEnabled());

  /*
   * Wait for the file-mode policy to reach the RIB so the post-clear assertion
   * below is comparing against a policy that was actually installed.
   */
  rib_->waitForPathSelectionPolicyUpdate();
  EXPECT_EQ(42, rib_->getPathSelectionPolicyVersion());

  // Try to clear — should be silently skipped in FILE_MODE
  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  co_await service_->co_clearPathSelectionPolicy();

  /*
   * Verify FILE_MODE is still active and the installed policy was NOT cleared
   * (the version still matches the artifact, not the -1 of a cleared policy).
   */
  EXPECT_TRUE(rib_->isCpsFileModeEnabled());
  EXPECT_EQ(42, rib_->getPathSelectionPolicyVersion());
}

// Full lifecycle: enable FILE_MODE → reject Thrift → disable → accept Thrift
CO_TEST_F(
    BgpServiceCpsFileModeTestFixture,
    CpsFileModeCycleEnableRejectDisableAccept) {
  // Step 1: Enable FILE_MODE via setCpsPolicyFromFile(dryrun=false)
  writeCpsArtifact(/*dryrun=*/false);

  folly::LoggerDB::get().getCategory("")->clearHandlers();
  auto peerMgrThread = peerManager_->runInThread();
  auto sessionMgrThread = sessionMgr_->runInThread();
  auto ribThread = rib_->runInThread();
  SCOPE_EXIT {
    rib_->stop();
    peerManager_->stop();
    sessionMgr_->stop();
    ribThread.join();
    peerMgrThread.join();
    sessionMgrThread.join();
  };

  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto ret = co_await service_->co_setCpsPolicyFromFile();
  CO_ASSERT_TRUE(*ret->success());
  EXPECT_TRUE(rib_->isCpsFileModeEnabled());

  // Step 2: Thrift setPathSelectionPolicy should be rejected
  rib_policy::TPathSelectionPolicy tPolicy;
  tPolicy.version() = 99;
  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto setRet = co_await service_->co_setPathSelectionPolicy(
      std::make_unique<rib_policy::TPathSelectionPolicy>(tPolicy));
  EXPECT_FALSE(*setRet->success());
  EXPECT_THAT(*setRet->err(), HasSubstr("FILE_MODE"));

  // Step 3: Disable FILE_MODE via setCpsPolicyFromFile(dryrun=true)
  writeCpsArtifact(/*dryrun=*/true);
  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto disableRet = co_await service_->co_setCpsPolicyFromFile();
  EXPECT_TRUE(*disableRet->success());
  EXPECT_FALSE(rib_->isCpsFileModeEnabled());

  /*
   * Step 4: Thrift setPathSelectionPolicy should now succeed and the new policy
   * must actually reach the RIB (final source = Thrift, final version = 50).
   */
  rib_policy::TPathSelectionPolicy tPolicy2;
  tPolicy2.version() = 50;
  auto replaceFuture = rib_->getRibPolicyReplaceFuture();
  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto acceptRet = co_await service_->co_setPathSelectionPolicy(
      std::make_unique<rib_policy::TPathSelectionPolicy>(tPolicy2));
  EXPECT_TRUE(*acceptRet->success());
  replaceFuture.wait();
  EXPECT_EQ(50, rib_->getPathSelectionPolicyVersion());
}

/*
 * Concurrency: file-mode refreshes racing Thrift CPS updates must be serialized
 * by cpsPolicyMutex_ (the file API takes the exclusive lock; the Thrift API
 * takes a shared lock and is gated on FILE_MODE). The fleet of operations runs
 * in parallel on a multi-threaded executor; the test asserts there is no crash,
 * deadlock, or data race (under TSAN) and that the service is left in a
 * consistent state.
 */
CO_TEST_F(
    BgpServiceCpsFileModeTestFixture,
    CpsFileModeConcurrentThriftAndFileMode) {
  // dryrun=false → every file-mode refresh enables and applies FILE_MODE.
  writeCpsArtifact(/*dryrun=*/false);

  /*
   * Remove non-thread-safe TestLogHandler before starting worker threads to
   * avoid TSAN data races on the handler's message vector.
   */
  folly::LoggerDB::get().getCategory("")->clearHandlers();

  auto peerMgrThread = peerManager_->runInThread();
  auto sessionMgrThread = sessionMgr_->runInThread();
  auto ribThread = rib_->runInThread();
  SCOPE_EXIT {
    rib_->stop();
    peerManager_->stop();
    sessionMgr_->stop();
    ribThread.join();
    peerMgrThread.join();
    sessionMgrThread.join();
  };

  constexpr int kIters = 25;
  folly::CPUThreadPoolExecutor exec(4);

  /*
   * Interleave file-mode refreshes and Thrift CPS updates so they contend for
   * cpsPolicyMutex_. Both RPCs return std::unique_ptr<TResult>.
   */
  std::vector<folly::coro::Task<std::unique_ptr<TResult>>> tasks;
  tasks.reserve(kIters * 2);
  for (int i = 0; i < kIters; ++i) {
    // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
    tasks.push_back(service_->co_setCpsPolicyFromFile());

    rib_policy::TPathSelectionPolicy tPolicy;
    tPolicy.version() = 1000 + i;
    // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
    tasks.push_back(service_->co_setPathSelectionPolicy(
        std::make_unique<rib_policy::TPathSelectionPolicy>(tPolicy)));
  }

  auto results = co_await folly::coro::co_withExecutor(
      &exec, folly::coro::collectAllRange(std::move(tasks)));

  /*
   * Every operation completed with a well-formed result. A Thrift update either
   * succeeded (it ran before FILE_MODE took effect) or was gracefully rejected
   * with a FILE_MODE error — never UB or a partial/garbage result.
   */
  for (const auto& res : results) {
    CO_ASSERT_NE(res, nullptr);
    if (!*res->success()) {
      EXPECT_THAT(*res->err(), HasSubstr("FILE_MODE"));
    }
  }

  /*
   * Thrift updates never clear FILE_MODE, so once any refresh has run the
   * terminal state is deterministically FILE_MODE enabled.
   */
  EXPECT_TRUE(rib_->isCpsFileModeEnabled());
}

/*
 * Assert the CPS file-mode ODS SUM counters actually move: a successful
 * file-mode apply bumps artifact-read + policy-applied success, and a Thrift
 * setPathSelectionPolicy rejected while in FILE_MODE bumps thrift_rpc_rejected.
 * SUM stats accumulate process-wide across tests, so we assert deltas.
 */
CO_TEST_F(BgpServiceCpsFileModeTestFixture, CpsFileModeOdsCountersIncrement) {
  BgpStatsDC::initCounters();
  auto counters = fb303::ThreadCachedServiceData::getShared();
  // SUM-type timeseries stats are read back with the ".sum" suffix.
  const std::string kReadSuccessSum =
      std::string(BgpStatsDC::kCpsArtifactReadSuccess) + ".sum";
  const std::string kAppliedSuccessSum =
      std::string(BgpStatsDC::kCpsPolicyAppliedSuccess) + ".sum";
  const std::string kThriftRejectedSum =
      std::string(BgpStatsDC::kCpsThriftRpcRejected) + ".sum";

  /*
   * A ".sum" key does not exist until its stat is first added, and SUM stats
   * accumulate across earlier tests in this process, so treat missing as 0 and
   * compare deltas rather than absolute values.
   */
  auto sumOr0 = [&](const std::string& key) {
    return counters->hasCounter(key) ? counters->getCounter(key) : 0;
  };

  counters->publishStats();
  const auto readSuccessBefore = sumOr0(kReadSuccessSum);
  const auto appliedSuccessBefore = sumOr0(kAppliedSuccessSum);
  const auto thriftRejectedBefore = sumOr0(kThriftRejectedSum);

  writeCpsArtifact(/*dryrun=*/false, /*version=*/7);

  folly::LoggerDB::get().getCategory("")->clearHandlers();
  auto peerMgrThread = peerManager_->runInThread();
  auto sessionMgrThread = sessionMgr_->runInThread();
  auto ribThread = rib_->runInThread();
  SCOPE_EXIT {
    rib_->stop();
    peerManager_->stop();
    sessionMgr_->stop();
    ribThread.join();
    peerMgrThread.join();
    sessionMgrThread.join();
  };

  // Successful file-mode apply → artifact-read + policy-applied success.
  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto ret = co_await service_->co_setCpsPolicyFromFile();
  CO_ASSERT_TRUE(*ret->success());
  rib_->waitForPathSelectionPolicyUpdate();

  /*
   * A non-null Thrift set is rejected in FILE_MODE → thrift_rpc_rejected.
   * (A null policy would short-circuit to "Empty policy" before the gate.)
   */
  rib_policy::TPathSelectionPolicy tPolicy;
  tPolicy.version() = 8;
  // @lint-ignore CLANGTIDY facebook-thrift-handler-direct-call
  auto rejectRet = co_await service_->co_setPathSelectionPolicy(
      std::make_unique<rib_policy::TPathSelectionPolicy>(tPolicy));
  EXPECT_FALSE(*rejectRet->success());

  counters->publishStats();
  EXPECT_EQ(readSuccessBefore + 1, sumOr0(kReadSuccessSum));
  EXPECT_EQ(appliedSuccessBefore + 1, sumOr0(kAppliedSuccessSum));
  EXPECT_EQ(thriftRejectedBefore + 1, sumOr0(kThriftRejectedSum));
}

/*
 * Test co_getPartialDrainStatus — dispatches onto Rib evb, returns empty
 * status when no partial-drain entries exist.
 */
TEST_F(BgpServiceTestFixture, CoGetPartialDrainStatusTest) {
  auto ribThread = rib_->runInThread();

  auto handler = std::shared_ptr<BgpServiceBase>(service_.get(), [](auto*) {});
  apache::thrift::ScopedServerInterfaceThread server(handler);
  auto client = server.newClient<apache::thrift::Client<TBgpService>>();

  auto status = folly::coro::blockingWait(client->co_getPartialDrainStatus());

  EXPECT_FALSE(*status.is_partially_drained());
  EXPECT_EQ(0, *status.num_affected_prefixes());
  EXPECT_EQ(0, *status.partial_drain_transition_count());
  EXPECT_EQ(0, service_->numRequestsInExecution());

  rib_->stop();
  ribThread.join();
}

/*
 * Test co_getPartialDrainState — dispatches onto Rib evb, returns empty
 * state when no partial-drain entries exist.
 */
TEST_F(BgpServiceTestFixture, CoGetPartialDrainStateTest) {
  auto ribThread = rib_->runInThread();

  auto handler = std::shared_ptr<BgpServiceBase>(service_.get(), [](auto*) {});
  apache::thrift::ScopedServerInterfaceThread server(handler);
  auto client = server.newClient<apache::thrift::Client<TBgpService>>();

  auto state = folly::coro::blockingWait(client->co_getPartialDrainState());

  EXPECT_FALSE(*state.partial_drain_state()->is_partially_drained());
  EXPECT_EQ(0, *state.partial_drain_state()->num_affected_prefixes());
  EXPECT_TRUE(state.drained_prefixes()->empty());
  EXPECT_EQ(0, service_->numRequestsInExecution());

  rib_->stop();
  ribThread.join();
}

/*
 * Test co_getPartiallyDrainedPrefixes — dispatches onto Rib evb, returns
 * empty list when no partial-drain entries exist.
 */
TEST_F(BgpServiceTestFixture, CoGetPartiallyDrainedPrefixesTest) {
  auto ribThread = rib_->runInThread();

  auto handler = std::shared_ptr<BgpServiceBase>(service_.get(), [](auto*) {});
  apache::thrift::ScopedServerInterfaceThread server(handler);
  auto client = server.newClient<apache::thrift::Client<TBgpService>>();

  auto prefixes =
      folly::coro::blockingWait(client->co_getPartiallyDrainedPrefixes());

  EXPECT_TRUE(prefixes.empty());
  EXPECT_EQ(0, service_->numRequestsInExecution());

  rib_->stop();
  ribThread.join();
}

} // namespace facebook::bgp
