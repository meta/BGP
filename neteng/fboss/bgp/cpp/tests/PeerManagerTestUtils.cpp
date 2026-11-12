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

#define PeerManager_TEST_FRIENDS       \
  friend class PeerManagerTestFixture; \
  friend class StreamSubscriberFixture;

#define SessionManager_TEST_FRIENDS friend class PeerManagerTestFixture;

#define AdjRib_TEST_FRIENDS friend class PeerManagerTestFixture;

#include "neteng/fboss/bgp/cpp/tests/PeerManagerTestUtils.h"

#include <vector>

#include <fmt/core.h>

#include "neteng/fboss/bgp/cpp/config/facebook/ConfigDC.h"
#include "neteng/fboss/bgp/cpp/tests/BoundedWaitUtils.h"

/*
 * The fiber default is 16KB. We are increasing the default due to nested
 * recursion with large stack due to installed exception handlers.
 */
DEFINE_int32(fiber_stack_size, 64, "Default fiber stack size in KB");

using namespace facebook::nettools::bgplib;

using facebook::network::toBinaryAddress;
using facebook::network::toIPPrefix;
using folly::IPAddress;

namespace facebook::bgp {

folly::coro::Task<void> waitForConsumerTimerExpiry() noexcept {
  co_await folly::coro::sleep(std::chrono::milliseconds(kDefaultMraiInterval));
  co_return;
}

std::unique_ptr<BgpUpdate2> createBgpUpdate2(
    uint32_t num,
    folly::IPAddress nexthop) {
  CHECK_LE(num, 255);
  auto update = std::make_unique<BgpUpdate2>();
  for (int i = 0; i < num; i++) {
    auto prefixStr = fmt::format("1.1.{}.0/24", i);
    RiggedIPPrefix rigPrefix;
    rigPrefix.prefix() = toIPPrefix(folly::IPAddress::createNetwork(prefixStr));
    update->mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv4;
    update->mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
    update->mpAnnounced()->prefixes()->push_back(rigPrefix);
    update->mpAnnounced()->nexthop() =
        toBinaryAddress(folly::IPAddress("1.1.1.1"));
  }
  update->attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment;
  segment.asSequence()->push_back(100);
  update->attrs()->asPath()->push_back(segment);
  update->attrs()->nexthop() = "1.1.1.1";
  update->attrs()->med() = 32;
  update->attrs()->isMedSet() = true;
  update->attrs()->localPref() = kLocalPref;
  BgpAttrCommunity community;
  community.asn() = 65530;
  community.value() = 15800;
  update->attrs()->communities()->push_back(community);
  update->v4Nexthop() = toBinaryAddress(nexthop);
  return update;
}

// RIB update with prefilled attributes and single v4 or v6 announced prefix
RibOutMessage createRibSingleAnnounce(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& nexthop,
    AsNum ribEntryAs,
    bool sendWithEoR,
    bool addPath,
    uint32_t pathIdToSend) {
  RibOutAnnouncement ribMsg;
  BgpUpdate2 update = buildBgpUpdateAttributes(nexthop);
  auto attrs = std::make_shared<facebook::bgp::BgpPath>(
      BgpPathFields(*BgpUpdate2toBgpPathC(update)));

  auto peer = prefix.first.isV4()
      ? TinyPeerInfo(
            kLocalV4RoutePeerAddr, ribEntryAs, 0, BgpSessionType::IBGP, false)
      : TinyPeerInfo(
            kLocalV6RoutePeerAddr, ribEntryAs, 0, BgpSessionType::IBGP, false);

  if (addPath) {
    ribMsg.addPathEntries.emplace_back(prefix, pathIdToSend, peer, attrs);
  } else {
    ribMsg.entries.emplace_back(prefix, kDefaultPathID, peer, attrs);
  }
  ribMsg.initialDump = sendWithEoR;
  ribMsg.sendWithEoR = sendWithEoR;

  return ribMsg;
}

// RIB update with prefilled attributes and single v4 or v6 announced prefix
RibOutMessage createRibInitialSingleAnnounce(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& nexthop,
    AsNum ribEntryAs,
    bool sendWithEoR,
    bool addPath,
    uint32_t pathIdToSend) {
  RibOutAnnouncement ribMsg;
  BgpUpdate2 update = buildBgpUpdateAttributes(nexthop);
  auto attrs = std::make_shared<facebook::bgp::BgpPath>(
      BgpPathFields(*BgpUpdate2toBgpPathC(update)));

  auto peer = prefix.first.isV4()
      ? TinyPeerInfo(
            kLocalV4RoutePeerAddr, ribEntryAs, 0, BgpSessionType::IBGP, false)
      : TinyPeerInfo(
            kLocalV6RoutePeerAddr, ribEntryAs, 0, BgpSessionType::IBGP, false);

  if (addPath) {
    ribMsg.addPathEntries.emplace_back(prefix, pathIdToSend, peer, attrs);
  } else {
    ribMsg.entries.emplace_back(prefix, kDefaultPathID, peer, attrs);
  }
  ribMsg.initialDump = true;
  ribMsg.sendWithEoR = sendWithEoR;

  return ribMsg;
}

/*
 * Create GR state from previous incarnation. Save all the peerAddresses as
 * if they support GR and were established in previous incarnation
 */
void createGrState(std::vector<BgpPeerId> peerIds, bool staleTime) {
  std::ofstream grFile;

  grFile.open(FLAGS_gr_state_file, std::ios::out | std::ios::trunc);
  if (!grFile.is_open()) {
    XLOG(ERR, "Could not open GR state saving file for writing");
    return;
  }

  auto nowInSec = std::chrono::duration_cast<std::chrono::seconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
  if (staleTime) {
    // Stale info by 10 minutes
    nowInSec -= 10 * 60;
  }

  // Start with epoch time
  grFile << nowInSec << "\n";

  // All the established GR peers
  for (const auto& peerId : peerIds) {
    grFile << peerId.peerAddr.str() << " " << peerId.remoteBgpId << "\n";
  }

  // Sign termination (signature) to indicate properly terminated.
  grFile << kGrStateFileTermination;
  grFile.close();
  XLOGF(INFO, "Saved GR state to {}", FLAGS_gr_state_file);
}

/*
 * Verify if the GR state exists or not.
 * Returns TRUE if previous state exists else FALSE
 */
bool isGrStateExists() {
  std::ifstream grFile;

  grFile.open(FLAGS_gr_state_file);

  return grFile.is_open();
}

folly::Future<folly::Unit> TestSessionManager::getSessionsComeUpFuture(
    const std::unordered_set<BgpPeerId>& peerSet,
    const std::chrono::seconds& timeout) {
  /*
   * can only be called before stop, or the evb will not run and
   * execute the scheduled fiber
   */
  folly::Future<folly::Unit> future;
  evb_.runInEventBaseThreadAndWait(
      [this, &future, peerSet = peerSet, timeout = timeout]() {
        future = fm_.addTaskFuture(
            [this, peerSet = peerSet, timeout = timeout]() mutable {
              auto start = std::chrono::steady_clock::now();
              // busy waits till sessions come up
              while (true) {
                if (std::chrono::steady_clock::now() - start > timeout) {
                  XLOG(ERR, "Sessions Come Up timeout");
                  break;
                }
                // TODO: remove busy waiting by forking notify queue
                facebook::nettools::bgplib::fiberSleepFor(
                    std::chrono::milliseconds(100));
                // confirm that sessions come up
                bool allUp = true;
                for (const auto& peerId : peerSet) {
                  if (!isPeerUp(peerId)) {
                    allUp = false;
                    break;
                  }
                }
                if (allUp) {
                  // all sessions are up
                  break;
                }
              }
            });
      });
  return future;
}

folly::Future<folly::Unit> TestSessionManager::getSessionsGoDownFuture(
    const std::unordered_set<BgpPeerId>& peerSet,
    const std::chrono::seconds& timeout) {
  /*
   * can only be called before stop, or the evb will not run and
   * execute the schedule
   */
  folly::Future<folly::Unit> future;
  evb_.runInEventBaseThreadAndWait(
      [this, &future, peerSet = peerSet, timeout = timeout]() {
        future = fm_.addTaskFuture(
            [this, peerSet = peerSet, timeout = timeout]() mutable {
              auto start = std::chrono::steady_clock::now();
              // busy waits till sessions go down
              while (true) {
                if (std::chrono::steady_clock::now() - start > timeout) {
                  XLOG(ERR, "Sessions Go Down timeout");
                  break;
                }
                // TODO: remove busy waiting by forking notify queue
                facebook::nettools::bgplib::fiberSleepFor(
                    std::chrono::milliseconds(100));
                // confirm that sessions go down
                bool allDown = true;
                for (const auto& peerId : peerSet) {
                  if (isPeerUp(peerId)) {
                    allDown = false;
                    break;
                  }
                }
                if (allDown) {
                  break;
                }
              }
            });
      });
  return future;
}

void PeerManagerTestFixture::SetUp() {
  dynamicShivPeer1_ = createBgpPeer(
      kAsn2,
      kLocalAddr1,
      kPeerPrefix1,
      kNextHopV4_1,
      kNextHopV6_1,
      true,
      kPeerTypeShiv);
  dynamicShivPeer2_ = createBgpPeer(
      kAsn1,
      kLocalAddr2,
      kPeerPrefix2,
      kNextHopV4_2,
      kNextHopV6_2,
      true,
      kPeerTypeShiv);
  dynamicMonitorPeer1_ = createBgpPeer(
      kAsn1,
      kLocalAddr2,
      kPeerPrefix3,
      kNextHopV4_2,
      kNextHopV6_2,
      true,
      kPeerTypeBgpMonitor);
  // NOTE: explicitly set rfc5549(v4-over-v6) support
  dynamicMonitorPeer1_.v4_over_v6_nexthop() = true;
  dynamicVipInjectorPeer1_ = createBgpPeer(
      kAsn4,
      kLocalAddr2,
      kPeerPrefix4,
      kNextHopV4_2,
      kNextHopV6_2,
      true,
      kPeerTypeShiv);

  thrift::PeerGroup peerGroup;
  peerGroup.name() = kPeerGroupName1;
  staticPeerGroups.push_back(peerGroup);
  peerGroup.name() = kPeerGroupName2;
  staticPeerGroups.push_back(peerGroup);

  staticPeer1_ = createBgpPeer(
      kPeerAsn3,
      kLocalAddr1,
      kPeerAddr3,
      kNextHopV4_3,
      kNextHopV6_3,
      true,
      kPeerTypeCsw);
  staticPeer1_.description() = kDescription1;
  staticPeer1_.peer_group_name() = kPeerGroupName1;

  staticPeer2_ = createBgpPeer(
      kPeerAsn4,
      kLocalAddr1,
      kPeerAddr4,
      kNextHopV4_4,
      kNextHopV6_4,
      true,
      kPeerTypeCsw);
  /*
   * assign same peer group name as associated with staticPeer1_
   */
  staticPeer2_.peer_group_name() = kPeerGroupName1;

  options_ = nettools::bgplib::getFiberManagerOptions(FLAGS_fiber_stack_size);

  /*
   * Override default GR state file with file based on thread id.
   * This ensures stress run will use different file for each run.
   */
  FLAGS_gr_state_file = fmt::format(
      "/dev/shm/bgp_gr_state.txt.{}",
      std::hash<std::thread::id>{}(std::this_thread::get_id()));

  // spin up a config store
  bgpGlobalConfig1_ = std::make_shared<facebook::bgp::BgpGlobalConfig>(
      kPeerAsn3, // localAsn
      kPeerAddr3, // routerId
      kPeerAddr3, // clusterId
      kHoldTime, // holdTime
      std::nullopt, // listenAddr
      kGrRestartTime, // grRestartTime
      std::unordered_map<
          folly::CIDRNetwork,
          thrift::BgpNetwork>(), // networksV4
      std::unordered_map<
          folly::CIDRNetwork,
          thrift::BgpNetwork>() // networksV6
  );

  bgpGlobalConfig2_ = std::make_shared<facebook::bgp::BgpGlobalConfig>(
      kPeerAsn4, // localAsn
      kPeerAddr4, // routerId
      kPeerAddr4, // clusterId
      kHoldTime, // holdTime
      std::nullopt, // listenAddr
      kGrRestartTime, // grRestartTime
      std::unordered_map<
          folly::CIDRNetwork,
          thrift::BgpNetwork>(), // networksV4
      std::unordered_map<
          folly::CIDRNetwork,
          thrift::BgpNetwork>() // networksV6
  );

  bgpGlobalConfig3_ = std::make_shared<facebook::bgp::BgpGlobalConfig>(
      kPeerAsn2, // localAsn
      kPeerAddr2, // routerId
      kPeerAddr2, // clusterId
      kHoldTime, // holdTime
      std::nullopt, // listenAddr
      kGrRestartTime, // grRestartTime
      std::unordered_map<
          folly::CIDRNetwork,
          thrift::BgpNetwork>(), // networksV4
      std::unordered_map<
          folly::CIDRNetwork,
          thrift::BgpNetwork>() // networksV6
  );

  sessionTerminateBaton_ = std::make_shared<folly::coro::Baton>();
}

void PeerManagerTestFixture::TearDown() {
  remove(FLAGS_gr_state_file.c_str());
}

std::shared_ptr<Config> PeerManagerTestFixture::getConfig(
    bool includeStaticPeer,
    bool includeDynamicShivPeer,
    bool includeDynamicMonitorPeer,
    bool includeDynamicVipInjectorPeer,
    bool enableStatefulHa,
    bool enableVipService,
    int32_t eorTimeS,
    bool enableSubscriberLimit,
    bool enableSwitchLimit,
    bool applyGoldenPrefixPolicy,
    const std::set<std::string>& bgpFeatures,
    bool enableDynamicPolicyEvaluation,
    bool enableUpdateGroup,
    int32_t streamSubscriberLimit) {
  thrift::BgpConfig thriftConfig;
  thriftConfig.router_id() = kLocalAddr1.str();
  thriftConfig.local_as() = kAsn1;
  thriftConfig.hold_time() = kHoldTime.count();
  thriftConfig.graceful_restart_convergence_seconds() = kGrRestartTime.count();
  thriftConfig.listen_addr() = kLocalAddr1.str();
  thriftConfig.eor_time_s() = eorTimeS;
  /*
   * Stress-test will run multiple instance of tests simultaneously.
   * Binding to a static port is bound to fail in that situation.
   * Picking a pseudo random port > 1024 based on getpid().
   * There is a remote possibility this might also result in a collision but
   * that probability is very very low.
   */
  std::srand((uint16_t)getpid());
  thriftConfig.listen_port() = 1179 + (folly::Random::rand32() % 60000);

  if (includeDynamicShivPeer) {
    thriftConfig.peers()->push_back(dynamicShivPeer1_);
    thriftConfig.peers()->push_back(dynamicShivPeer2_);
  }
  if (includeDynamicMonitorPeer) {
    thriftConfig.peers()->push_back(dynamicMonitorPeer1_);
  }
  if (includeDynamicVipInjectorPeer) {
    thriftConfig.peers()->push_back(dynamicVipInjectorPeer1_);
  }
  if (includeStaticPeer) {
    if (enableStatefulHa) {
      staticPeer1_.enable_stateful_ha() = true;
      staticPeer2_.enable_stateful_ha() = true;
    }
    thriftConfig.peers()->push_back(staticPeer1_);
    thriftConfig.peers()->push_back(staticPeer2_);
    thriftConfig.peer_groups() = staticPeerGroups;
  }

  // VipService related fields
  if (enableVipService) {
    vipconfig::config::VipServiceConfig vipSvcConfig;
    vipSvcConfig.port() = 0;
    vipSvcConfig.min_ttl_s() = 60; // for tests, make the vips' ttl larger
    thriftConfig.vip_service_config() = std::move(vipSvcConfig);
    thriftConfig.enable_vip_service() = true;
  }

  if (enableSwitchLimit) {
    thrift::BgpSwitchLimitConfig switchLimitConfig;
    switchLimitConfig.overload_protection_mode() = applyGoldenPrefixPolicy
        ? thrift::OverloadProtectionMode::APPLY_GOLDEN_PREFIX_POLICY
        : thrift::OverloadProtectionMode::DROP_EXCESS_PREFIXES;
    switchLimitConfig.prefix_limit() = 1;
    switchLimitConfig.total_path_limit() = 1;
    thriftConfig.switch_limit_config() = switchLimitConfig;
  }

  // setup bgpSettingConfig with features and dynamic policy evaluation flag
  thrift::BgpSettingConfig tBgpSettingConfig;
  tBgpSettingConfig.features() = bgpFeatures;
  if (enableSubscriberLimit) {
    tBgpSettingConfig.features()->insert("stream_subscriber_limit");
    tBgpSettingConfig.stream_subscriber_limit() = streamSubscriberLimit;
  }
  tBgpSettingConfig.enable_dynamic_policy_evaluation() =
      enableDynamicPolicyEvaluation;
  tBgpSettingConfig.enable_update_group() = enableUpdateGroup;

  // Move the settings into the thrift config
  thriftConfig.bgp_setting_config() = std::move(tBgpSettingConfig);

  FeatureFlags::LoadFromThriftConfig(thriftConfig);

  if (enableVipService) {
    return std::make_shared<ConfigDC>(thriftConfig);
  }
  return std::make_shared<Config>(thriftConfig);
}

std::shared_ptr<Config> PeerManagerTestFixture::enableRibAllocatedPathId(
    const std::shared_ptr<Config>& config) {
  thrift::BgpConfig thriftConfig = config->getConfig();
  thriftConfig.bgp_setting_config()->enable_rib_allocated_path_id() = true;
  if (*thriftConfig.enable_vip_service()) {
    return std::make_shared<ConfigDC>(thriftConfig);
  }
  return std::make_shared<Config>(thriftConfig);
}

std::shared_ptr<Config> PeerManagerTestFixture::addPeerToConfig(
    const std::shared_ptr<Config>& config,
    thrift::BgpPeer& peer,
    const std::optional<const std::string>& peerGroupName) {
  thrift::BgpConfig thriftConfig = config->getConfig();
  if (peerGroupName) {
    peer.peer_group_name() = *peerGroupName;

    thrift::PeerGroup peerGroup;
    peerGroup.name() = *peerGroupName;
    thriftConfig.peer_groups()->push_back(peerGroup);
  }
  thriftConfig.peers()->push_back(peer);
  if (*thriftConfig.enable_vip_service()) {
    return std::make_shared<ConfigDC>(thriftConfig);
  }
  return std::make_shared<Config>(thriftConfig);
}

void PeerManagerTestFixture::initTwoSessionMgrs(
    folly::fibers::FiberManager* fm) {
  sessionMgr1_ = std::make_shared<TestSessionManager>(
      *bgpGlobalConfig1_, &callback1_, *fm, false);

  sessionMgr2_ = std::make_shared<TestSessionManager>(
      *bgpGlobalConfig2_, &callback2_, *fm, false);
}

void PeerManagerTestFixture::initThreeSessionMgrs(
    folly::fibers::FiberManager* fm) {
  sessionMgr1_ = std::make_shared<TestSessionManager>(
      *bgpGlobalConfig1_, &callback1_, *fm, false);

  sessionMgr2_ = std::make_shared<TestSessionManager>(
      *bgpGlobalConfig2_, &callback2_, *fm, false);

  sessionMgr3_ = std::make_shared<TestSessionManager>(
      *bgpGlobalConfig3_, &callback3_, *fm, false);
}

std::shared_ptr<AdjRib> PeerManagerTestFixture::setupAdjRib(
    folly::EventBase& evb,
    std::shared_ptr<ChangeTracker<ShadowRibEntry>> changeListTracker,
    const BgpPeerId& peerId,
    const AsNum& remoteAs,
    std::shared_ptr<folly::coro::Baton>& sessionTerminateBaton,
    std::shared_ptr<const Config> config,
    bool isRrClient,
    const folly::IPAddress& v4Nexthop,
    const folly::IPAddress& v6Nexthop,
    bool enableStatefulHa,
    bool v4OverV6Nexthop,
    std::shared_ptr<PolicyManager> policyManager) {
  auto adjRibOutGroup =
      std::make_shared<AdjRibOutGroup>(evb, "PeerManagerTest");
  auto adjRib = std::make_shared<AdjRib>(
      peerId,
      PeeringParams(
          peerId.peerAddr,
          std::nullopt, // peerPrefix
          kAsn1,
          kAsn1,
          remoteAs,
          kLocalAddr1.asV4(),
          kLocalAddr1.asV4(),
          std::chrono::seconds(kDefaultHoldTime),
          std::chrono::seconds(kGrRestartTime),
          nettools::bgplib::constants::kBgpPort,
          folly::AsyncSocket::anyAddress(),
          TBgpSessionConnectMode::PASSIVE_ACTIVE,
          v4Nexthop.asV4(),
          v6Nexthop.asV6(),
          RrClientConfigured(isRrClient),
          NextHopSelfConfigured{false},
          AfiIpv4Configured{true},
          AfiIpv6Configured{true},
          ConfedPeerConfigured{false},
          RemovePrivateAsConfigured{false},
          std::nullopt, // localConfedAs
          std::nullopt, // asConfedId
          AdvertiseLinkBandwidth::DISABLE,
          ReceiveLinkBandwidth::ACCEPT,
          std::nullopt, // linkBandwidthBps
          ValidateRemoteAs{true},
          std::nullopt, // preRouteLimit
          std::nullopt, // postRouteLimit
          false, // allowLoopbackReflection
          enableStatefulHa ? EnableStatefulHa{true} : EnableStatefulHa{false},
          std::nullopt, // addPath
          V4OverV6Nexthop{v4OverV6Nexthop}),
      evb,
      ribInQ_, /* write to the queue */
      observerQ_,
      sessionTerminateBaton,
      policyManager,
      isSafeModeOn_,
      std::nullopt,
      std::nullopt,
      adjRibOutGroup,
      std::nullopt /* outDelay */,
      std::make_shared<ConfigManager>(config));
  static ConsumerBitmap dummyAddPathBitmap;
  static ConsumerBitmap dummyNonAddPathBitmap;
  auto changeListConsumer = std::make_shared<AdjRibOutConsumer>(
      changeListTracker,
      adjRib,
      "ChangeList Consumer",
      evb,
      dummyAddPathBitmap,
      dummyNonAddPathBitmap);
  adjRib->setChangeListConsumer(changeListConsumer);
  return adjRib;
}

// Overload with bitmap parameters for selective multipath notification
std::shared_ptr<AdjRib> PeerManagerTestFixture::setupAdjRib(
    folly::EventBase& evb,
    std::shared_ptr<ChangeTracker<ShadowRibEntry>> changeListTracker,
    const BgpPeerId& peerId,
    const AsNum& remoteAs,
    std::shared_ptr<folly::coro::Baton>& sessionTerminateBaton,
    std::shared_ptr<const Config> config,
    ConsumerBitmap& addPathBitmap,
    ConsumerBitmap& nonAddPathBitmap,
    bool isRrClient,
    const folly::IPAddress& v4Nexthop,
    const folly::IPAddress& v6Nexthop,
    bool enableStatefulHa,
    bool v4OverV6Nexthop) {
  auto adjRibOutGroup =
      std::make_shared<AdjRibOutGroup>(evb, "PeerManagerTest");
  auto adjRib = std::make_shared<AdjRib>(
      peerId,
      PeeringParams(
          peerId.peerAddr,
          std::nullopt, // peerPrefix
          kAsn1,
          kAsn1,
          remoteAs,
          kLocalAddr1.asV4(),
          kLocalAddr1.asV4(),
          std::chrono::seconds(kDefaultHoldTime),
          std::chrono::seconds(kGrRestartTime),
          nettools::bgplib::constants::kBgpPort,
          folly::AsyncSocket::anyAddress(),
          TBgpSessionConnectMode::PASSIVE_ACTIVE,
          v4Nexthop.asV4(),
          v6Nexthop.asV6(),
          RrClientConfigured(isRrClient),
          NextHopSelfConfigured{false},
          AfiIpv4Configured{true},
          AfiIpv6Configured{true},
          ConfedPeerConfigured{false},
          RemovePrivateAsConfigured{false},
          std::nullopt, // localConfedAs
          std::nullopt, // asConfedId
          AdvertiseLinkBandwidth::DISABLE,
          ReceiveLinkBandwidth::ACCEPT,
          std::nullopt, // linkBandwidthBps
          ValidateRemoteAs{true},
          std::nullopt, // preRouteLimit
          std::nullopt, // postRouteLimit
          false, // allowLoopbackReflection
          enableStatefulHa ? EnableStatefulHa{true} : EnableStatefulHa{false},
          std::nullopt, // addPath
          V4OverV6Nexthop{v4OverV6Nexthop}),
      evb,
      ribInQ_, /* write to the queue */
      observerQ_,
      sessionTerminateBaton,
      nullptr /* PolicyManager */,
      isSafeModeOn_,
      std::nullopt,
      std::nullopt,
      adjRibOutGroup,
      std::nullopt /* outDelay */,
      std::make_shared<ConfigManager>(config));
  auto changeListConsumer = std::make_shared<AdjRibOutConsumer>(
      changeListTracker,
      adjRib,
      "ChangeList Consumer",
      evb,
      addPathBitmap,
      nonAddPathBitmap);
  adjRib->setChangeListConsumer(changeListConsumer);
  return adjRib;
}

std::shared_ptr<MockAdjRib> PeerManagerTestFixture::setupMockAdjRib(
    folly::EventBase& evb,
    const BgpPeerId& peerId,
    const AsNum& remoteAs,
    std::shared_ptr<folly::coro::Baton>& sessionTerminateBaton,
    bool isRrClient,
    const folly::IPAddress& v4Nexthop,
    const folly::IPAddress& v6Nexthop,
    bool enableStatefulHa,
    bool v4OverV6Nexthop) {
  auto adjRib = std::make_shared<MockAdjRib>(
      peerId,
      PeeringParams(
          peerId.peerAddr,
          std::nullopt, // peerPrefix
          kAsn1,
          kAsn1,
          remoteAs,
          kLocalAddr1.asV4(),
          kLocalAddr1.asV4(),
          std::chrono::seconds(kDefaultHoldTime),
          std::chrono::seconds(kGrRestartTime),
          nettools::bgplib::constants::kBgpPort,
          folly::AsyncSocket::anyAddress(),
          TBgpSessionConnectMode::PASSIVE_ACTIVE,
          v4Nexthop.asV4(),
          v6Nexthop.asV6(),
          RrClientConfigured(isRrClient),
          NextHopSelfConfigured{false},
          AfiIpv4Configured{true},
          AfiIpv6Configured{true},
          ConfedPeerConfigured{false},
          RemovePrivateAsConfigured{false},
          std::nullopt, // localConfedAs
          std::nullopt, // asConfedId
          AdvertiseLinkBandwidth::DISABLE,
          ReceiveLinkBandwidth::ACCEPT,
          std::nullopt, // linkBandwidthBps
          ValidateRemoteAs{true},
          std::nullopt, // preRouteLimit
          std::nullopt, // postRouteLimit
          false, // allowLoopbackReflection
          enableStatefulHa ? EnableStatefulHa{true} : EnableStatefulHa{false},
          std::nullopt, // addPath
          V4OverV6Nexthop{v4OverV6Nexthop}),
      evb,
      ribInQ_, /* write to the queue */
      observerQ_,
      sessionTerminateBaton,
      nullptr /* PolicyManager */,
      isSafeModeOn_);
  return adjRib;
}

/*
 * Creates a mock peer manager with appropriate config and
 * mock session manager
 */
std::shared_ptr<MockPeerManager> PeerManagerTestFixture::setupMockPeerManager(
    bool includeStaticPeer,
    bool includeDynamicShivPeer,
    bool includeDynamicMonitorPeer,
    bool includeDynamicVipInjectorPeer) {
  // create session manager
  config_ = getConfig(
      includeStaticPeer,
      includeDynamicShivPeer,
      includeDynamicMonitorPeer,
      includeDynamicVipInjectorPeer);

  /*
   * Setup values returned to sessionEstablishement
   * getEstablishedPeerDisplayInfo
   */
  auto peeringParams = bgp::PeeringParams();
  peeringParams.peerAddr = kPeerAddr1;
  peeringParams.peerPrefix = kV4Prefix3;
  peeringParams.localBgpId = kLocalAddr1.asV4();
  peeringParams.description = kDescription1;
  peeringParams.peerId = fmt::format("{}:v4:1", peeringParams.description);
  peeringParams.isEnhancedRouteRefreshConfigured = true;
  mockInfo1_.peeringParams = peeringParams;

  // create MockPeerManager with the config_
  auto configManager = std::make_shared<ConfigManager>(config_);
  auto mockPeerMgr = std::make_shared<MockPeerManager>(
      configManager,
      ribInQ_, /* write to the queue */
      ribOutQ_, /* read from the queue */
      nbrRouteChangeQ_);
  setupMockSessionManager(mockPeerMgr);

  mockPeerMgr->isSafeModeOn_ = isSafeModeOn_;
  return mockPeerMgr;
}

/*
 * create a mock peer manager in a separate thread such that funcitons running
 * in eventbase loop can also be scheduled
 */
std::shared_ptr<MockPeerManager>
PeerManagerTestFixture::setupMockPeerManagerWithSeparateThread(
    bool includeStaticPeer,
    bool includeDynamicShivPeer) {
  // create session manager
  config_ = getConfig(includeStaticPeer, includeDynamicShivPeer);
  auto globalConfig = config_->getBgpGlobalConfig();
  // initialize peer manager thread fiber manager

  // create MockPeerManager with the config_
  auto configManager = std::make_shared<ConfigManager>(config_);
  auto mockPeerMgr = std::make_shared<MockPeerManager>(
      configManager,
      ribInQ_, /* write to the queue */
      ribOutQ_, /* read from the queue */
      nbrRouteChangeQ_);

  return mockPeerMgr;
}

/*
 * create a mock session manager which shares the same fiber manager with
 * the mock peer manager. The mockPeerMgr should be a non-empty pointer, e.g.,
 * generated by setupMockPeerManager or setupMockPeerManagerWithSeparateThread
 */
std::shared_ptr<MockSessionManager>
PeerManagerTestFixture::setupMockSessionManager(
    std::shared_ptr<MockPeerManager>& mockPeerMgr) {
  // mockPeerMgr should not be empty
  EXPECT_TRUE(mockPeerMgr != nullptr);
  EXPECT_TRUE(mockPeerMgr->changeListTracker_ != nullptr);

  auto globalConfig = config_->getBgpGlobalConfig();

  auto oQueue = std::make_shared<AdjRib::AdjRibInQueueT>();
  auto biQueue = std::make_shared<AdjRib::AdjRibOutQueueT>(
      kMaxEgressQueueSize, kEgressQueueHighWatermark, kEgressQueueLowWatermark);

  auto mockSessionMgr = std::make_shared<MockSessionManager>(
      *globalConfig,
      false, /* enableMessagesOverNotifyQueue */
      biQueue,
      oQueue,
      true /* enableCoroNotifyQueue - required for PeerManagerBase's
              processPeerEventLoop */
  );
  mockPeerMgr->setSessionManager(mockSessionMgr);

  // Set default return values for mocked methods
  ON_CALL(*mockSessionMgr, getEstablishedPeerDisplayInfo(testing::_))
      .WillByDefault(testing::Return(mockInfo1_));

  return mockSessionMgr;
}

void PeerManagerTestFixture::runEoRTest(
    bool isSess1Restarting,
    bool isSess2Restarting) {
  static int numRuns;
  numRuns++;

  auto config = getConfig(
      true, /* includeStaticPeer */
      false, /* includeDynamicShivPeer */
      false, /* includeDynamicMonitorPeer */
      false, /* includeDynamicVipInjectorPeer */
      false, /* enableStatefulHa */
      false /* enableVipService */
  );

  std::chrono::seconds const eor_time{config->getConfig().eor_time_s().value()};

  auto configManager = std::make_shared<ConfigManager>(config);
  auto peerMgr = std::make_shared<PeerManagerBase>(
      configManager,
      nullptr,
      ribInQ_, /* write to the queue */
      ribOutQ_, /* read from the queue */
      nbrRouteChangeQ_);
  auto localSessionMgr = std::make_shared<SessionManager>(
      *config->getBgpGlobalConfig(),
      false, /* enableMessagesOverNotifyQueue */
      true); /* enableCoroNotifyQueue - required for PeerManagerBase's
                processPeerEventLoop */
  peerMgr->setSessionManager(localSessionMgr);

  folly::EventBase evb;

  auto& fm = folly::fibers::getFiberManager(evb, options_);
  initTwoSessionMgrs(&fm);

  auto start = std::chrono::steady_clock::now();

  uint32_t numAnnouncementsSent{0};
  /*
   * read ribInQ_, expect to get two announcements (one from sessionMgr1_,
   * one from sessionMgr2_), then one EoR at the end
   */
  uint32_t peer1AnnouncementCnt{0}, peer2AnnouncementCnt{0};
  uint32_t numAnnouncementsRcvd{0}, numWithdrawsRcvd{0};
  bool eorReceived{false};

  // Synchronization primitive
  folly::fibers::Baton stopPeerBaton, peerStoppedBaton;
  std::vector<folly::Future<folly::Unit>> taskFutures;

  XLOG(INFO, "Add peers to session mgr called.");
  const int peerMgrPort = *config->getConfig().listen_port();
  sessionMgr1_->setRestartingState(isSess1Restarting);
  sessionMgr2_->setRestartingState(isSess2Restarting);
  sessionMgr1_->addPeer(
      kLocalAddr1, kAsn1, kPeerAsn3, {kPeerAddr3, 0}, peerMgrPort);
  sessionMgr2_->addPeer(
      kLocalAddr1, kAsn1, kPeerAsn4, {kPeerAddr4, 0}, peerMgrPort);

  XLOG(INFO, "Fiber task (1/4) finished to add peer to session managers");

  // task to wait session up and send EoRs
  {
    auto task = fm.addTaskFuture([&] {
      folly::collectAll(
          sessionMgr1_->getSessionsComeUpFuture({kLocalPeerId1}),
          sessionMgr2_->getSessionsComeUpFuture({kLocalPeerId1}))
          .get();

      /*
       * TODO: remove this sleep after fixing the race between
       * getSessionsComeUpFuture and getEstablishedCallback().
       */
      nettools::bgplib::fiberSleepFor(100ms);
      // confirm that session comes up
      EXPECT_EQ(numRuns, callback1_.getEstablishedCallbackCount(kLocalPeerId1));
      EXPECT_EQ(
          (numRuns - 1), callback1_.getTerminatedCallbackCount(kLocalPeerId1));
      EXPECT_TRUE(callback1_.isSessionUp(kLocalPeerId1));

      EXPECT_EQ(numRuns, callback2_.getEstablishedCallbackCount(kLocalPeerId1));
      EXPECT_EQ(
          (numRuns - 1), callback2_.getTerminatedCallbackCount(kLocalPeerId1));
      EXPECT_TRUE(callback2_.isSessionUp(kLocalPeerId1));

      // create 10 update messages
      std::vector<std::unique_ptr<nettools::bgplib::BgpUpdate2>> updates1,
          updates2;
      updates1.emplace_back(createBgpUpdate2(5, kPeerAddr3));
      updates2.emplace_back(createBgpUpdate2(5, kPeerAddr4));

      // send updates and EoR to peerMgr
      if (!isSess1Restarting) {
        ASSERT_FALSE(
            sessionMgr1_->sendUpdates(kLocalPeerId1, std::move(updates1))
                .hasError());
        ASSERT_FALSE(sessionMgr1_->sendEndOfRib(kLocalPeerId1).hasError());
        numAnnouncementsSent++;
      }
      if (!isSess2Restarting) {
        ASSERT_FALSE(
            sessionMgr2_->sendUpdates(kLocalPeerId1, std::move(updates2))
                .hasError());
        ASSERT_FALSE(sessionMgr2_->sendEndOfRib(kLocalPeerId1).hasError());
        numAnnouncementsSent++;
      }
      // wait for EoR time to finish sending out updates and EoRs
      nettools::bgplib::fiberSleepFor(eor_time + 100ms);
      /*
       * Simulate session was up and running and then post baton to trigger
       * session tear-down.
       */
      stopPeerBaton.post();

      XLOG(INFO, "Fiber task (2/4) finished to send out updates and EoRs");
    });
    taskFutures.emplace_back(std::move(task));
  }

  // task to read ribInQ for verification
  {
    /*
     * The boundedBlockingPop below throws BoundedWaitTimeout on a hung
     * queue. Because this task runs inside FiberManager::addTaskFuture, a
     * throw here crashes the process rather than yielding a clean gtest
     * assertion — still a land-blocking CRITICAL outcome (not a suppressed
     * tpx TIMEOUT), and the BoundedWaitTimeout `what()` message is in the
     * crash log, so the root cause stays visible.
     */
    auto task = fm
                    .addTaskFuture(
                        [&] {
                          while (true) {
                            auto msg = facebook::bgp::test::boundedBlockingPop(
                                ribInQ_, "ribInQ_");
                            folly::variant_match(
            msg,
            [&](RibInAnnouncement announcement) {
              if (announcement.peer.addr == kPeerAddr3) {
                peer1AnnouncementCnt++;
              }
              if (announcement.peer.addr == kPeerAddr4) {
                peer2AnnouncementCnt++;
              }
              numAnnouncementsRcvd++;
            },
            [&](RibInWithdrawal /* unused */) { numWithdrawsRcvd++; },
            [&](RibInInitialPathComputation /* unused */) {
              /*
               * This is the crux of the EoR test.  In order to prevent
               * EoR deadlock, we assert that if other side is in
               * restarting state, peerMgr does not wait to receive
               * announcements / EoR from other side before notifying
               * RibInInitialPathComputation
               */
              auto end = std::chrono::steady_clock::now();
              auto elapsed =
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      end - start);
              XLOGF(
                  INFO,
                  "Received EoR. Elapsed time: {}ms. EoR timeout: {}s",
                  elapsed.count(),
                  eor_time.count());
              EXPECT_LT(elapsed, eor_time);
              if (isSess1Restarting) {
                EXPECT_EQ(0, peer1AnnouncementCnt);
              } else {
                EXPECT_EQ(1, peer1AnnouncementCnt);
              }
              if (isSess2Restarting) {
                EXPECT_EQ(0, peer2AnnouncementCnt);
              } else {
                EXPECT_EQ(1, peer2AnnouncementCnt);
              }
              eorReceived = true;
            },
            [&](RibDumpReq /* unused */) {},
            [&](PauseBestPathAndFibProgramming /* unused */) {},
            [&](ResumeBestPathAndFibProgramming /* unused */) {},
            [&](const RibInNexthopUpdate& /* unused */) {},
            [&](const NexthopResolutionUpdate& /* unused */) {},
            [&](const RibInAddPathGrUpdate& /* unused */) {});
                            /*
                             * Exit when EoR is received and all expected
                             * announcements have arrived. Note: stop() calls
                             * markDaemonShutdown() which skips sending
                             * withdrawals during session termination (fast
                             * cleanup path). Therefore, we don't wait for
                             * withdrawals here
                             */
                            if (eorReceived &&
                                numAnnouncementsRcvd == numAnnouncementsSent) {
                              break;
                            }
                          }

                          XLOG(
                              INFO,
                              "Fiber task (3/4) finished to verify EoR stats.");
                        });
    taskFutures.emplace_back(std::move(task));
  }

  // task to verify that stop will save GR state, it has expected peers.
  {
    auto task = fm.addTaskFuture([&] {
      /*
       * Wait for sessions to be established and updates sent before
       * terminating the sessions.
       */
      facebook::bgp::test::boundedBatonWait(
          peerStoppedBaton,
          "peerStoppedBaton",
          facebook::bgp::test::kDefaultPopTimeout);

      auto sessionDownFuture1 =
          sessionMgr1_->getSessionsGoDownFuture({kLocalPeerId1});
      auto sessionDownFuture2 =
          sessionMgr2_->getSessionsGoDownFuture({kLocalPeerId1});

      sessionMgr1_->shutdownWithGR(false);
      sessionMgr2_->shutdownWithGR(false);

      // Order of addresses in file is immaterial.
      auto grLoadResult = peerMgr->readGrState();
      ASSERT_NE(nullptr, grLoadResult.peers);
      std::unordered_set<BgpPeerId> expectedPeers = {kPeerId3, kPeerId4};
      EXPECT_EQ(expectedPeers, *(grLoadResult.peers));

      folly::collectAll(sessionDownFuture1, sessionDownFuture2).get();

      sessionMgr1_->stop();
      sessionMgr2_->stop();

      XLOG(INFO, "Fiber task (4/4) finished to verify GR state.");
    });
    taskFutures.emplace_back(std::move(task));
  }

  // create peer manager thread
  auto peerMgrThread = peerMgr->runInThread();
  auto localSessionMgrThread = localSessionMgr->runInThread();
  auto sessionMgr1Thread = sessionMgr1_->runInThread();
  auto sessionMgr2Thread = sessionMgr2_->runInThread();

  // create evbThread to pump all of the fiber tasks
  auto evbThread = std::thread([&]() { evb.loop(); });
  evb.waitUntilRunning();

  /*
   * Step1: wait for session establishment with advertisement + EoR sending
   */
  facebook::bgp::test::boundedBatonWait(
      stopPeerBaton, "stopPeerBaton", facebook::bgp::test::kDefaultPopTimeout);

  /*
   * Step2: stop sessions by shutting down PeerManagerBase to save GR state.
   * Mirrors Main.cpp shutdown: markDaemonShutdown → saveGrState → stop.
   */
  peerMgr->markDaemonShutdown();
  peerMgr->saveGrState();
  localSessionMgr->stop();
  peerMgr->stop();

  /*
   * Step3: signal waiting tasks that PeerManagerBase teardown is complete.
   */
  peerStoppedBaton.post();

  /*
   * Step4: wait for all fiber task futures to be completed, including the
   * EoR verification task.
   */
  folly::collectAll(taskFutures.begin(), taskFutures.end()).get();

  peerMgrThread.join();
  localSessionMgrThread.join();
  sessionMgr1Thread.join();
  sessionMgr2Thread.join();
  evbThread.join();

  // release the session managers before evb is destroyed
  sessionMgr1_.reset();
  sessionMgr2_.reset();
  peerMgr.reset();
}

// Helper function to create a mock peer info for a static peer
std::shared_ptr<nettools::bgplib::BgpPeerDisplayInfo>
PeerManagerTestFixture::getMockPeerInfo(
    const folly::IPAddress& peerAddr /* static peer */,
    const uint64_t& routerId,
    const uint64_t& numReset /* number of session went down */,
    const uint64_t& lastWentDown /* hours ago */,
    const nettools::bgplib::ResetReason&
        lastResetReason /* iff numReset > 0 */) {
  auto mockConfig = getConfig(
      true /* includeStaticPeer */, false /* includeDynamicShivPeer */);
  auto staticPeerToConfig = mockConfig->getPeerToConfig();
  auto peeringParams =
      mockConfig->getPeeringParamsForPeer(*staticPeerToConfig.at(peerAddr));
  auto peerInfo = std::make_shared<nettools::bgplib::BgpPeerDisplayInfo>();
  peerInfo->peeringParams = peeringParams;
  peerInfo->remoteBgpId = routerId;
  peerInfo->remoteGrRestartTime = std::nullopt;
  peerInfo->state = nettools::bgplib::BgpSessionState::ESTABLISHED;
  peerInfo->localAddr = folly::SocketAddress("127.0.0.1", 1234);
  peerInfo->startTime = std::chrono::steady_clock::now();
  peerInfo->establishedTime = std::chrono::steady_clock::now();
  peerInfo->negotiatedCapabilities = nettools::bgplib::BgpCapabilities();
  peerInfo->remoteAs = peeringParams.remoteAs;
  peerInfo->negotiatedHoldTime = std::nullopt;
  peerInfo->numOfConnectionAttempts = 0;
  peerInfo->lastResetHoldTimer = 0;
  peerInfo->lastResetKeepAliveTimer = 0;
  peerInfo->lastReceivedKeepAlive = 0;
  peerInfo->lastSentKeepAlive = 0;
  peerInfo->numResets = numReset;
  if (numReset > 0) {
    peerInfo->lastResetTime =
        peerInfo->startTime - std::chrono::hours(lastWentDown);
    peerInfo->lastResetReason = lastResetReason;
  } else {
    peerInfo->lastResetReason = std::nullopt;
    peerInfo->lastResetTime = std::chrono::steady_clock::now();
  }

  return peerInfo;
};

// Helper function to create a mock peer info for a dynamic peer
std::shared_ptr<nettools::bgplib::BgpPeerDisplayInfo>
PeerManagerTestFixture::getMockPeerInfo(
    const folly::CIDRNetwork& prefix /* dynamic peer */,
    const uint64_t& routerId,
    bool vipInjectorPeer,
    bool otherDynamicPeer) {
  auto mockConfig = getConfig(false, otherDynamicPeer, false, vipInjectorPeer);
  auto dynamicPeerToConfig = mockConfig->getDynamicPeerToConfig();
  auto param1 = mockConfig->getPeeringParamsForDynamicPeer(
      *dynamicPeerToConfig.at(prefix));
  auto peerInfo = std::make_shared<nettools::bgplib::BgpPeerDisplayInfo>();
  peerInfo->peeringParams = param1;
  peerInfo->remoteBgpId = routerId;
  peerInfo->remoteGrRestartTime = std::nullopt;
  peerInfo->state = nettools::bgplib::BgpSessionState::ESTABLISHED;
  peerInfo->localAddr = folly::SocketAddress("127.0.0.1", 1234);
  peerInfo->startTime = std::chrono::steady_clock::now();
  peerInfo->establishedTime = std::chrono::steady_clock::now();
  peerInfo->negotiatedCapabilities = nettools::bgplib::BgpCapabilities();
  peerInfo->remoteAs = param1.remoteAs;
  peerInfo->negotiatedHoldTime = std::nullopt;
  peerInfo->numOfConnectionAttempts = 0;
  peerInfo->lastResetHoldTimer = 0;
  peerInfo->lastResetKeepAliveTimer = 0;
  peerInfo->lastReceivedKeepAlive = 0;
  peerInfo->lastSentKeepAlive = 0;
  peerInfo->lastResetReason = std::nullopt;
  return peerInfo;
}

folly::coro::Task<void> PeerManagerTestFixture::waitForAdjRibsToProcessUpdates(
    folly::EventBase& evb,
    std::vector<std::shared_ptr<AdjRib::AdjRibOutQueueT>> queues) {
  evb.loopOnce();
  for (auto& q : queues) {
    while (!q->empty()) {
      co_await facebook::bgp::test::boundedPop(*q, "q");
    }
  }
}

/**
 * @brief Parameterized SetUp() function for StreamSubscriberFixture tests
 * Note that this SetUp() function should be explicitly called within the test
 *
 * @param configureMonitorPeer: If true, configure a BgpMonitor peer
 * @param initialAnnouncementDone: ribInitialAnnouncementDone_ flag in the
 * peerManager
 * @param enableSubscriberLimit: Enable stream subscriber limit.
 * @param streamSubscriberLimit: Value of the limit when it is enabled.
 */
void StreamSubscriberFixture::SetUp(
    bool configureMonitorPeer,
    bool initialAnnouncementDone,
    bool enableSubscriberLimit,
    int32_t streamSubscriberLimit) {
  PeerManagerTestFixture::SetUp();

  // Get the BgpMonitor peer config
  config_ = getConfig(
      false /* includeStaticPeer */,
      false /* includeDynamicShivPeer */,
      configureMonitorPeer,
      false /* includeDynamicVipInjectorPeer */,
      false /* enableStatefulHa */,
      true /* enableVipServer */,
      kDefaultEorTimeS /* eorTimeS = */,
      enableSubscriberLimit,
      false /* enableSwitchLimit */,
      false /* applyGoldenPrefixPolicy */,
      {} /* bgpFeatures */,
      false /* enableDynamicPolicyEvaluation */,
      false /* enableUpdateGroup */,
      streamSubscriberLimit);

  // Instantiate peerManager object
  auto configManager = std::make_shared<ConfigManager>(config_);
  peerMgr = std::make_shared<PeerManagerBase>(
      configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  sessionMgr =
      std::make_shared<SessionManager>(*config_->getBgpGlobalConfig(), false);
  peerMgr->setSessionManager(sessionMgr);

  // Simulate the peerManager flags.
  peerMgr->eorTimerExpired_ = false;
  peerMgr->initialized_ = true;
  /*
   * If both flags below are true, this indicates that the EOR is sent out
   * after initial Fib sync.
   */
  peerMgr->ribInitialAnnouncementStarted_ = true;
  peerMgr->ribInitialAnnouncementDone_ = initialAnnouncementDone;

  // create peer manager thread
  peerMgrThread = std::make_shared<std::thread>(peerMgr->runInThread());

  sessionMgrThread = std::make_shared<std::thread>(sessionMgr->runInThread());
}

/**
 * @brief Override default tear down function
 */
void StreamSubscriberFixture::TearDown() {
  PeerManagerTestFixture::TearDown();
  peerMgr->markDaemonShutdown();
  sessionMgr->stop();
  peerMgr->stop();
  peerMgrThread->join();
  sessionMgrThread->join();
  /*
   * LoadFromThriftConfig only inserts into this process-global set; nothing
   * clears it. Tests on this fixture enable stream_subscriber_limit, so
   * without this reset the feature would stay on for every Config built later
   * in the binary and unrelated tests would depend on run order.
   */
  FeatureFlags::features.clear();
  SUCCEED();
}

/**
 * @brief Implementation of
 * PeerManagerDynamicPolicyEvaluationFixture::SetUp()
 *
 * This initializes the configuration with dynamic policy evaluation
 * enabled/disabled based on the parameter.
 */
void PeerManagerDynamicPolicyEvaluationFixture::SetUp(
    bool enableDynamicPolicyEvaluation) {
  // Call parent's SetUp first
  PeerManagerTestFixture::SetUp();

  /*
   * Create the config with appropriate parameters for dynamic policy evaluation
   * testing
   */
  config_ = getConfig(
      true, // includeStaticPeer
      true, // includeDynamicShivPeer
      false, // includeDynamicMonitorPeer
      false, // includeDynamicVipInjectorPeer
      false, // enableStatefulHa
      true, // enableVipService
      kDefaultEorTimeS, // eorTimeS
      false, // enableSubscriberLimit
      false, // enableSwitchLimit
      false, // applyGoldenPrefixPolicy
      {}, // bgpFeatures
      enableDynamicPolicyEvaluation);
}

void PeerManagerDynamicPolicyEvaluationFixture::verifyStateWithRetries(
    folly::EventBase& evb,
    const std::vector<AdjRibPolicyUpdateState>& adjRibStates) {
  // Wait for async operations to complete and verify the final steady state
  WITH_RETRIES_N(5, {
    evb.runInEventBaseThreadAndWait([&]() {
      for (const auto& state : adjRibStates) {
        EXPECT_EVENTUALLY_EQ(
            state.expectedIngressPendingPolicyUpdate,
            state.adjRib->isPendingIngressPolicyUpdate());
        EXPECT_EVENTUALLY_EQ(
            state.expectedEgressPendingPolicyUpdate,
            state.adjRib->isEgressPolicyUpdateRequired());
      }
    });
  });
}

void PeerManagerDynamicPolicyEvaluationFixture::verifyState(
    folly::EventBase& evb,
    const std::vector<AdjRibPolicyUpdateState>& adjRibStates) {
  evb.runInEventBaseThreadAndWait([&]() {
    for (const auto& state : adjRibStates) {
      EXPECT_EQ(
          state.expectedIngressPendingPolicyUpdate,
          state.adjRib->isPendingIngressPolicyUpdate());
      EXPECT_EQ(
          state.expectedEgressPendingPolicyUpdate,
          state.adjRib->isEgressPolicyUpdateRequired());
    }
  });
}

void PeerManagerDynamicPolicyEvaluationFixture::verifyRouteFilterStatement(
    folly::EventBase& evb,
    const std::vector<
        std::pair<std::shared_ptr<AdjRib>, rib_policy::TRouteFilterStatement>>&
        adjRibStatements) {
  WITH_RETRIES_N(5, {
    evb.runInEventBaseThreadAndWait([&]() {
      for (const auto& [adjRib, expectedStatement] : adjRibStatements) {
        auto currentStatement = adjRib->getRouteFilterStatement();
        EXPECT_EVENTUALLY_NE(nullptr, currentStatement);
        if (currentStatement) {
          EXPECT_EVENTUALLY_EQ(expectedStatement, currentStatement->toThrift());
        }
      }
    });
  });
}

std::unique_ptr<PeerToPolicyMap> createPolicyMap(
    const std::vector<std::tuple<std::string, std::string, std::string>>&
        policyMapEntries) {
  auto policyMap = std::make_unique<PeerToPolicyMap>();

  for (const auto& [key, ingressPolicy, egressPolicy] : policyMapEntries) {
    /*
     * Empty string means "clear/unset policy" (std::nullopt)
     * Non-empty string means "set to this policy"
     */
    (*policyMap)[key][facebook::bgp::bgp_policy::DIRECTION::IN] =
        ingressPolicy.empty() ? std::nullopt
                              : std::make_optional(ingressPolicy);
    (*policyMap)[key][facebook::bgp::bgp_policy::DIRECTION::OUT] =
        egressPolicy.empty() ? std::nullopt : std::make_optional(egressPolicy);
  }

  return policyMap;
}

} // namespace facebook::bgp
