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

#include <folly/fibers/EventBaseLoopController.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibCommon.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/config/ConfigManager.h"
#include "neteng/fboss/bgp/cpp/lib/coro/BackPressuredQueue.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyManager.h"
#include "neteng/fboss/bgp/cpp/tests/BoundedWaitUtils.h"
#include "neteng/fboss/bgp/cpp/tests/PolicyUtils.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

using facebook::bgp::bgp_policy::BgpPolicyActionType;
using facebook::bgp::bgp_policy::BgpPolicyAtomicMatchType;
using facebook::bgp::routing_policy::BooleanOperator;
using facebook::neteng::fboss::bgp_attr::AdvertiseLinkBandwidth;
using facebook::neteng::fboss::bgp_attr::ReceiveLinkBandwidth;
using folly::IPAddress;

namespace facebook::bgp {

using nettools::bgplib::DeDuplicatedBgpPath;
using ::testing::ElementsAre;

/*
 * The fixture provides fiber manager and evb for outbound related tests
 */
class AdjRibOutboundFixture : public ::testing::Test {
 public:
  void SetUp() override {
    // Register Singleton
    folly::SingletonVault::singleton()->registrationComplete();
    AdjRibPolicyCache::get()->clearCache();
    AdjRibPolicyCache::get()->resetTotalCacheHit();
    AdjRibPolicyCache::get()->resetTotalCacheMiss();
    DeDuplicatedBgpPath::clearDeduplicator();
    postPolicyResultCache_.clear();
    fm_ = std::make_unique<folly::fibers::FiberManager>(
        std::make_unique<folly::fibers::EventBaseLoopController>(),
        facebook::nettools::bgplib::getFiberManagerOptions());

    static_cast<folly::fibers::EventBaseLoopController&>(fm_->loopController())
        .attachEventBase(evb_);
  }

  // util function to terminate the running adjRib
  void terminateAdjRib(bool gracefulRestartFlag = false) {
    adjRibInQ_->fiberPush(
        nettools::bgplib::FiberBgpPeer::BgpSessionStop{gracefulRestartFlag});
  }

  /*
   * @brief: Call processRibMessage directly on the adjRib. If backpressure
   * is enabled, also schedule sendBgpUpdates.
   *
   * @details: Templated function to accept any instance of RibOutPayload
   * variant. Mimics the flow when simulating an announcement or withdrawal
   * from RIB.
   *
   * When backpressure is enabled, in production:
   *   (1) during initial announcement or Rib dump, processRibMessage is called
   *     directly on the RibOutMsg, no queueing. After all entries are
   *     done, then scheduleSendBgpUpdates will be called.
   *   (2) during regular route update, the AdjRib's changelist consumer will
   *     consume updates made to shadow RIB. After all changes have been
   *     processed by calling handleRibAnnouncedEntry, scheduleSendBgpUpdates
   *     will be called.
   *
   * Since most of the tests in the AdjRibOutboundFixture test suite don't
   * have changelist rigged but are also NOT testing changelist integration,
   * emulating (1) without changelist integration suffices.
   */
  template <typename T>
  void pushRibOutMsgToAdjRib(T&& msg) {
    RibOutPayload ribOutMsg(std::forward<T>(msg));
    adjRib_->processRibMessage(ribOutMsg);
    adjRib_->scheduleSendBgpUpdates(true /* tryPullNewChangeItems */);
  }

  /**
   * @brief Helper to call updateAttributesOutWithoutNexthopCommon using
   * adjRib_'s peer configuration.
   *
   * @details This helper obtains a PeerConfig from adjRib_ and delegates to
   * the common function. This keeps tests simple while using the consolidated
   * implementation in AdjRibCommon.
   */
  void updateAttributesOutWithoutNexthop(
      const RibOutAnnouncementEntry& update,
      const std::shared_ptr<const BgpPath>& policyCachedAttrs,
      std::shared_ptr<BgpPath>& attrsToUpdate,
      const PostPolicyInfo& postPolicyInfo) {
    updateAttributesOutWithoutNexthopCommon(
        adjRib_->getPeerConfig(),
        update,
        policyCachedAttrs,
        attrsToUpdate,
        postPolicyInfo);
  }

  folly::coro::Task<
      std::optional<nettools::bgplib::FiberBgpPeer::InputMessageT>>
  popFromEgressQueue() {
    /*
     * Bounded — a never-arriving message fails the test with a clear
     * BoundedWaitTimeout instead of hanging.
     */
    auto msg = co_await facebook::bgp::test::boundedPop(
        *boundedAdjRibOutQ_, "boundedAdjRibOutQ_");
    co_return msg;
  }

  RibOutPayload buildAnnouncementFromMap(
      const folly::F14NodeMap<
          std::shared_ptr<BgpPath>,
          std::vector<folly::CIDRNetwork>>& entries,
      bool ebgpPeer = true,
      bool initialDump = true,
      bool sendWithEoR = false);

  void TestBody() override {}

  /**
   * @brief: This setupAdjRib(...) function is taking the BgpPeerId as a
   * parameter as opposed to the other overload function which is taking the
   * peerAddr as a parameter.
   */
  void setupAdjRib(
      const AsNum& globalAs,
      const AsNum& localAs,
      const AsNum& remoteAs,
      const bool isRrClient,
      const bool isConfedPeer,
      const NextHopSelfConfigured& isNextHopSelfConfigured,
      const folly::IPAddress& v4Nexthop,
      const folly::IPAddress& v6Nexthop,
      const bool sessionEstablish,
      const AfiIpv4Negotiated& isAfiIpv4Negotiated,
      const AfiIpv6Negotiated& isAfiIpv6Negotiated,
      const V4OverV6Nexthop& isV4OverV6Nexthop,
      const EnhancedRouteRefreshNegotiated& isEnhancedRouteRefreshNegotiated,
      const RouteRefreshNegotiated& isRouteRefreshNegotiated,
      const RemovePrivateAsConfigured& removePrivateAs,
      const std::shared_ptr<PolicyManager>& policy,
      const std::optional<std::string>& egressPolicyName,
      const nettools::bgplib::BgpPeerId& peerId,
      const bool allowLoopbackReflection,
      const std::optional<nettools::bgplib::BgpAddPathSendRec>& addPathCapa,
      const std::optional<std::string>& ingressPolicyName = std::nullopt);

  /**
   * @brief: This setupAdjRib(...) function is taking the peerAddr as a
   * parameter as opposed to the other overload function which is taking the
   * BgpPeerId as a parameter.
   */
  void setupAdjRib(
      const AsNum& globalAs = kLocalAs1,
      const AsNum& localAs = kLocalAs1,
      const AsNum& remoteAs = kRemoteAs1,
      const bool isRrClient = false,
      const bool isConfedPeer = false,
      const NextHopSelfConfigured& isNextHopSelfConfigured =
          NextHopSelfConfigured(false),
      const folly::IPAddress& v4Nexthop = kV4Nexthop1,
      const folly::IPAddress& v6Nexthop = kV6Nexthop1,
      const bool sessionEstablish = true,
      const AfiIpv4Negotiated& isAfiIpv4Negotiated = AfiIpv4Negotiated(true),
      const AfiIpv6Negotiated& isAfiIpv6Negotiated = AfiIpv6Negotiated(true),
      const V4OverV6Nexthop& isV4OverV6Nexthop = V4OverV6Nexthop(false),
      const EnhancedRouteRefreshNegotiated& isEnhancedRouteRefreshNegotiated =
          EnhancedRouteRefreshNegotiated(false),
      const RouteRefreshNegotiated& isRouteRefreshNegotiated =
          RouteRefreshNegotiated(false),
      const RemovePrivateAsConfigured& removePrivateAs =
          (RemovePrivateAsConfigured(false)),
      const std::shared_ptr<PolicyManager>& policy = nullptr,
      const std::optional<std::string>& egressPolicyName = std::nullopt,
      const folly::IPAddress& peerAddr = kPeerAddr1,
      const bool allowLoopbackReflection = false,
      const std::optional<nettools::bgplib::BgpAddPathSendRec>& addPathCapa =
          std::nullopt,
      const std::optional<std::string>& ingressPolicyName = std::nullopt);

  void setupAdjRib(
      const nettools::bgplib::BgpPeerId& peerId,
      const uint32_t remoteAs = kRemoteAs1);

  void setupAdjRib(const bool sendAddPath);

  // IBGP setup for policy testing
  void setupAdjRib(
      const std::shared_ptr<PolicyManager>& policy,
      const std::optional<std::string>& egressPolicyName,
      const bool sessionEstablish = true,
      const bool sendAddPath = false);

  void setupAdjRibForOutUnitTest();

  std::unique_ptr<folly::fibers::FiberManager> fm_;
  folly::EventBase evb_;
  std::shared_ptr<AdjRib> adjRib_;

  /*
   * Cluster ID passed to PeeringParams by setupAdjRib. Defaults to the BGP
   * identifier because that is what Config derives today; set it before
   * setupAdjRib to exercise reflection with the two configured independently.
   */
  folly::IPAddressV4 localClusterId_{kLocalAddr1.asV4()};

  // adjrib <-> FiberBgpPeer queue
  std::shared_ptr<AdjRib::AdjRibInQueueT> adjRibInQ_ =
      std::make_shared<AdjRib::AdjRibInQueueT>(
          nettools::bgplib::kMaxIngressQueueSize);

  std::shared_ptr<AdjRib::AdjRibOutQueueT> boundedAdjRibOutQ_ =
      std::make_shared<AdjRib::AdjRibOutQueueT>(
          nettools::bgplib::kMaxEgressQueueSize,
          nettools::bgplib::kEgressQueueHighWatermark,
          nettools::bgplib::kEgressQueueLowWatermark);

  // adjrib <-> rib queue
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ_{
      nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<AdjRib::ObservableMessageT> observerQ_;

  std::shared_ptr<const Config> config_{nullptr};

  TinyPeerInfo localPeerV4_{
      kLocalV4RoutePeerAddr,
      kLocalRouteAs,
      0,
      BgpSessionType::IBGP,
      false};
  TinyPeerInfo iBgpPeer_{
      kPeerAddr2,
      kLocalAs1,
      kPeerRouterId2,
      BgpSessionType::IBGP,
      false};
  TinyPeerInfo rrcPeer_{
      kPeerAddr3,
      kLocalAs1,
      kPeerRouterId3,
      BgpSessionType::IBGP,
      true};
  TinyPeerInfo eBgpPeer_{
      kPeerAddr4,
      kPeerAsn4,
      kPeerRouterId4,
      BgpSessionType::EBGP,
      false};

  TinyPeerInfo loopbackPeerV4_{
      kLoopBackAddressV4,
      kLocalAs1,
      kPeerRouterId2,
      BgpSessionType::IBGP,
      false};
  TinyPeerInfo loopbackPeerV6_{
      kLoopBackAddressV6,
      kLocalAs1,
      kPeerRouterId2,
      BgpSessionType::IBGP,
      false};
};

class AdjRibOutboundFixtureBooleanSuite
    : public AdjRibOutboundFixture,
      public testing::WithParamInterface<bool> {};

// RIB update with prefilled attributes and multiple v4 or v6 announced prefix
RibOutPayload createRibMultipleAnnounce(
    const std::vector<folly::CIDRNetwork>& prefixes,
    const folly::IPAddress& nexthop,
    const TinyPeerInfo& peer,
    bool sendWithEoR);

// RIB update for single v4 or v6 withdrawal prefix
RibOutPayload createRibSingleWithdrawal(
    const folly::CIDRNetwork& prefix = kV4Prefix1);

// RIB update for single v4 or v6 withdrawal prefix for addPath
RibOutPayload createRibSingleWithdrawalForAddPath(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& nh,
    const uint32_t pathIdToSend);

// Parse AS path string into vector of ASNs
std::vector<uint32_t> parseAsPath(const std::string& asPath);

// Parse community string into vector
std::vector<std::string> parseCommunities(const std::string& community);

// Check if UPDATE is an EoR (empty UPDATE with no routes)
bool isEoRUpdate(const nettools::bgplib::BgpUpdate2& update);

// Check if prefix exists in UPDATE announcements
bool findPrefixInAnnouncements(
    const nettools::bgplib::BgpUpdate2& update,
    bool isV4,
    const folly::CIDRNetwork& expectedCidr,
    uint32_t addPathId = 0);

// Check if prefix exists in UPDATE withdrawals
bool findPrefixInWithdrawals(
    const nettools::bgplib::BgpUpdate2& update,
    bool isV4,
    const folly::CIDRNetwork& expectedCidr,
    uint32_t addPathId = 0);

/*
 * Collect every announced (REACH) prefix in an UPDATE, across the v4 and MP
 * NLRI lists, as CIDRs.
 */
std::vector<folly::CIDRNetwork> getAnnouncedPrefixes(
    const nettools::bgplib::BgpUpdate2& update);

/*
 * Collect every withdrawn (UNREACH) prefix in an UPDATE, across the v4 and MP
 * NLRI lists, as CIDRs.
 */
std::vector<folly::CIDRNetwork> getWithdrawnPrefixes(
    const nettools::bgplib::BgpUpdate2& update);

// Verify route attributes match expectations
bool verifyRouteAttributes(
    const nettools::bgplib::BgpUpdate2& update,
    const std::string& expectedNexthop,
    const std::string& expectedAsPath = "",
    const std::string& expectedCommunity = "");

} // namespace facebook::bgp
