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

#include "neteng/fboss/bgp/cpp/tests/e2e/E2ETestFixture.h"

#include <fmt/core.h>
#include <folly/container/small_vector.h>
#include <folly/coro/Baton.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/TimedWait.h>

#include "neteng/fboss/bgp/cpp/changeTracker/ChangeItem.h"
#include "neteng/fboss/bgp/cpp/common/FeatureFlags.h"
#include "neteng/fboss/bgp/cpp/config/ConfigManager.h"
#include "neteng/fboss/bgp/cpp/lib/BgpMessageParser.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/BgpSerializer.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberBgpPeer.h"
#include "neteng/fboss/bgp/cpp/nexthopTracker/NexthopCache.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyManager.h"
#include "neteng/fboss/bgp/cpp/tests/AdjRibInUtils.h"
#include "neteng/fboss/bgp/cpp/tests/AdjRibOutUtils.h"
#include "neteng/fboss/bgp/cpp/tests/BoundedWaitUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/E2ETestRibFactory.h"

#include "neteng/fboss/bgp/cpp/tests/PeerManagerTestUtils.h"

using facebook::bgp::test::boundedBlockingPop;
using facebook::bgp::test::BoundedWaitTimeout;

namespace facebook {
namespace bgp {

std::shared_ptr<Config> E2ETestFixture::getConfig(
    bool enableUpdateGroup,
    bool enableSerializeGroupPdu) {
  /* Build thrift config */
  thrift::BgpConfig thriftConfig;
  thriftConfig.router_id() = kLocalAddr1.str();
  thriftConfig.local_as() = kAsn1;
  thriftConfig.hold_time() = kHoldTime.count();
  thriftConfig.graceful_restart_convergence_seconds() =
      grConvergenceSecondsOverride_.value_or(kGrRestartTime.count());
  thriftConfig.listen_addr() = kLocalAddr1.str();
  thriftConfig.eor_time_s() = eorTimeSecondsOverride_.value_or(45);
  /*
   * Use port 0 for OS-assigned dynamic port allocation. This avoids port
   * collision when running multiple tests in parallel (e.g., with stress-runs).
   * The previous approach of random ports (1179 + rand % 60000) led to
   * collisions during parallel stress testing.
   */
  thriftConfig.listen_port() = 0;

  /* Enable UCMP calculation from LBW extended community if requested */
  if (enableComputeUcmpFromLbw_) {
    thriftConfig.compute_ucmp_from_link_bandwidth_community() = true;
  }

  /* Add all dynamically configured peers */
  for (const auto& peer : peers_) {
    thriftConfig.peers()->push_back(peer);
  }

  /*
   * Register peer groups referenced by any peer's peerGroupName so
   * peer-group-level policy APIs can resolve them.
   */
  if (!pendingPeerGroupNames_.empty()) {
    std::vector<thrift::PeerGroup> peerGroups;
    for (const auto& name : pendingPeerGroupNames_) {
      thrift::PeerGroup peerGroup;
      peerGroup.name() = name;
      peerGroups.push_back(std::move(peerGroup));
    }
    thriftConfig.peer_groups() = std::move(peerGroups);
  }

  /*
   * Plumb the parent confederation AS into the global config if any peer
   * added via addPeer() asked for it. Required by Config.cpp validation
   * when peer.is_confed_peer=true (see Config.cpp ~line 1015).
   */
  if (pendingLocalConfedAsn_.has_value()) {
    thriftConfig.local_confed_as_4_byte() = *pendingLocalConfedAsn_;
  }

  /* Setup bgp_setting_config */
  thrift::BgpSettingConfig tBgpSettingConfig;
  tBgpSettingConfig.enable_update_group() = enableUpdateGroup;
  tBgpSettingConfig.enable_next_hop_tracking() = enableNexthopTracking_;
  {
    thrift::UpdateGroupConfig ugConfig;
    if (updateGroupConfigOverride_.has_value()) {
      ugConfig = *updateGroupConfigOverride_;
      ugConfig.enableSerializeGroupPdu() = enableSerializeGroupPdu;
    } else {
      ugConfig.allowSlowPeerDetach() = true;
      ugConfig.enableSerializeGroupPdu() = enableSerializeGroupPdu;
    }
    tBgpSettingConfig.update_group_config() = std::move(ugConfig);
  }
  if (enableEiBgpMultipath_) {
    tBgpSettingConfig.enable_eibgp_multipath() = true;
  }
  if (enableRibAllocatedPathId_) {
    tBgpSettingConfig.enable_rib_allocated_path_id() = true;
  }
  if (enableLegacyV4NlriEncoding_) {
    tBgpSettingConfig.enable_legacy_v4_nlri_encoding() = true;
  }
  if (enableDynamicPolicyEvaluation_) {
    tBgpSettingConfig.enable_dynamic_policy_evaluation() = true;
  }

  thriftConfig.bgp_setting_config() = std::move(tBgpSettingConfig);

  FeatureFlags::LoadFromThriftConfig(thriftConfig);

  return std::make_shared<Config>(thriftConfig);
}

// ==================== PEER MANAGEMENT ====================

void E2ETestFixture::addPeer(const BgpPeerSpec& spec) {
  auto peer = createBgpPeer(
      spec.asn,
      spec.localAddr,
      spec.peerAddr,
      spec.v4Nexthop,
      spec.v6Nexthop,
      true /* allowAsIn */,
      spec.peerType);

  if (!spec.description.empty()) {
    peer.description() = spec.description;
  }

  if (spec.disableIpv4Afi) {
    peer.disable_ipv4_afi() = true;
  }
  if (spec.disableIpv6Afi) {
    peer.disable_ipv6_afi() = true;
  }

  /* Set out-delay timer if specified */
  if (spec.outDelaySeconds.has_value()) {
    thrift::BgpPeerTimers timers;
    timers.hold_time_seconds() = 90;
    timers.keep_alive_seconds() = 30;
    timers.out_delay_seconds() = spec.outDelaySeconds.value();
    peer.bgp_peer_timers() = std::move(timers);
  }

  /* Set LBW (Link Bandwidth) configuration if specified */
  if (spec.advertiseLinkBandwidth.has_value()) {
    peer.advertise_link_bandwidth() = spec.advertiseLinkBandwidth.value();
  }
  if (spec.receiveLinkBandwidth.has_value()) {
    peer.receive_link_bandwidth() = spec.receiveLinkBandwidth.value();
  }

  /* Set egress policy name if specified */
  if (spec.egressPolicyName.has_value()) {
    peer.egress_policy_name() = spec.egressPolicyName.value();
  }

  /* Confederation peer: mark on the thrift::BgpPeer. */
  if (spec.isConfedPeer) {
    peer.is_confed_peer() = true;
  }

  /* Route-Reflector client: mark on the thrift::BgpPeer. */
  if (spec.isRrClient) {
    peer.is_rr_client() = true;
  }

  /*
   * Peer-group membership: set on the peer and track the group name so
   * getConfig() registers it in thriftConfig.peer_groups().
   */
  if (spec.peerGroupName.has_value()) {
    peer.peer_group_name() = spec.peerGroupName.value();
    pendingPeerGroupNames_.insert(spec.peerGroupName.value());
  }

  if (spec.additionalRemoteAs.has_value()) {
    peer.additional_remote_as_4_byte() = *spec.additionalRemoteAs;
  }

  if (spec.enforceFirstAs.has_value()) {
    peer.enforce_first_as() = *spec.enforceFirstAs;
  }

  /*
   * Parent confederation AS is a global config field, not per-peer. If
   * multiple specs supply localConfedAsn they must agree (otherwise the
   * test setup is internally inconsistent).
   */
  if (spec.localConfedAsn.has_value()) {
    if (pendingLocalConfedAsn_.has_value() &&
        *pendingLocalConfedAsn_ != *spec.localConfedAsn) {
      throw std::logic_error(
          "BgpPeerSpec::localConfedAsn conflicts across peers in this fixture");
    }
    pendingLocalConfedAsn_ = spec.localConfedAsn;
  }

  /* Store ADD-PATH capability for this peer (used in establishSession) */
  peerAddPathCapabilities_[spec.peerAddr] = spec.addPathCapability;

  /* Store the peer's MP-EXT capability flag (used to model the remote MP-EXT
   * bit at session establishment; false = capability-less peer). */
  peerMpExtCapable_[spec.peerAddr] = spec.mpExtCapable;

  if (spec.enableRouteRefresh || enableRouteRefreshForAllPeers_) {
    peerRouteRefreshEnabled_.insert(spec.peerAddr);
  }

  peers_.push_back(std::move(peer));
}

void E2ETestFixture::deletePeer(const folly::IPAddress& peerAddr) {
  peers_.erase(
      std::remove_if(
          peers_.begin(),
          peers_.end(),
          [&](const auto& p) { return *p.peer_addr() == peerAddr.str(); }),
      peers_.end());
}

folly::Expected<folly::Unit, nettools::bgplib::FiberBgpPeerManager::ErrorCode>
E2ETestFixture::delPeerAtRuntime(const folly::IPAddress& peerAddr) {
  XLOGF(INFO, "=== delPeerAtRuntime: peer {} ===", peerAddr.str());
  return folly::coro::blockingWait(co_withExecutor(
      &peerManager_->getEventBase(), peerManager_->delPeers({peerAddr})));
}

void E2ETestFixture::bringUpPeer(
    const folly::IPAddress& peerAddr,
    uint64_t versionNumber) {
  BgpPeerId peerId{peerAddr, peerAddr.asV4().toLongHBO()};
  establishSession(peerId, versionNumber);
}

void E2ETestFixture::bringUpPeerBlocked(
    const folly::IPAddress& peerAddr,
    uint64_t versionNumber) {
  blockPeer(peerAddr);
  bringUpPeer(peerAddr, versionNumber);
}

void E2ETestFixture::testOnlyDeferInitDump(
    const folly::IPAddress& peerAddr,
    bool defer) {
  peerManager_->testOnlySetDeferInitDump(peerAddr, defer);
}

void E2ETestFixture::testOnlyDeferDrjAcceptance(
    const folly::IPAddress& peerAddr,
    bool defer) {
  peerManager_->testOnlySetDeferDrjAcceptance(peerAddr, defer);
}

void E2ETestFixture::bringDownPeer(
    const folly::IPAddress& peerAddr,
    bool peerDelete) {
  BgpPeerId peerId{peerAddr, peerAddr.asV4().toLongHBO()};
  if (auto queues = getPeerQueues(peerId)) {
    /*
     * Close bounded queue BEFORE sending BgpSessionStop to wake up any
     * deferredPushToPeer coroutines blocked on waitToPush().
     *
     * In production, the I/O thread (FiberBgpPeer) owns this queue and closes
     * it during session teardown. In E2E tests, we mock the I/O thread, so the
     * test owns the queue and must close it here.
     *
     * Without this, the coroutine remains blocked and can cause use-after-free
     * when the group is destroyed while the coroutine still holds references.
     */
    if (queues->boundedAdjRibOutQ) {
      queues->boundedAdjRibOutQ->close();
      XLOGF(
          INFO,
          "bringDownPeer: Closed bounded queue for peer {}",
          peerAddr.str());
    }
    queues->adjRibInQ->fiberPush(FiberBgpPeer::BgpSessionStop{});
  }

  /*
   * Wait for session termination to complete before returning.
   *
   * This is critical for test correctness: without waiting, the next
   * bringUpPeer() call would block in waitForSessionTerminateBaton()
   * on PeerManagerBase's EVB, but the work to signal the baton (processing
   * BgpSessionStop in processPeerMessageLoop) also runs on the same EVB,
   * causing a deadlock.
   *
   * By waiting here from the test thread (not the EVB), we allow the
   * EVB to process the BgpSessionStop and signal the semaphore before we
   * attempt to bring the peer back up.
   */
  waitForSessionTerminationBaton(peerAddr);

  /*
   * Now notify PeerManagerBase about the session termination.
   *
   * With MockSessionManager, pushing BgpSessionStop to adjRibInQ only triggers
   * AdjRib::sessionTerminated(). We also need to trigger
   * PeerManagerBase::sessionTerminated() to update PeerManagerBase state (e.g.,
   * markStateTerminated on AdjRib, cleanup update groups, etc.).
   *
   * This must be done AFTER waiting for the semaphore, because
   * PeerManagerBase::sessionTerminated() accesses AdjRib state that may be
   * modified during AdjRib::sessionTerminated().
   */
  FiberBgpPeer::ObservableStateT terminateEvent{
      .peerId = peerId,
      .state = BgpSessionState::IDLE,
      .versionNumber = 0,
      .sessionInfo = nullptr,
      .peerDelete = peerDelete};

  /*
   * Drive sessionTerminated on the PeerManagerBase EVB while blocking the
   * *test* thread (not the EVB thread) for completion, mirroring
   * delPeerAtRuntime().
   *
   * The earlier folly::via(&evb, [...]{ blockingWait(...); }).wait() ran
   * blockingWait *on* the EVB thread, occupying the EVB loop for the whole
   * duration of sessionTerminated. When the terminate removes the last member
   * of an update group, sessionTerminated co_awaits
   * UpdateGroupManager::maybeDestroyUpdateGroup() ->
   * AdjRibOutGroup::drainAsyncScope(), which requests cancellation of the
   * group's async-scope coroutines and then co_awaits asyncScope_.joinAsync().
   * Those coroutines are bound to the EVB; with the loop blocked inside
   * blockingWait they can never run to observe the cancellation, so
   * joinAsync() never completes and the EVB deadlocks. Scheduling the task on
   * the EVB and waiting from the test thread keeps the loop free to drain the
   * scope, exactly as sessionTerminated runs in production.
   */
  folly::coro::blockingWait(
      folly::coro::co_withExecutor(
          &peerManager_->getEventBase(),
          peerManager_->sessionTerminated(terminateEvent)));

  XLOGF(INFO, "Session termination complete for peer: {}", peerAddr.str());
}

bool E2ETestFixture::waitForSessionTerminationBaton(
    const folly::IPAddress& peerAddr,
    std::chrono::milliseconds timeout) {
  BgpPeerId peerId{peerAddr, peerAddr.asV4().toLongHBO()};
  std::shared_ptr<folly::coro::Baton> terminationBaton;
  peerManager_->getEventBase().runInEventBaseThreadAndWait([&]() {
    auto batonIt = peerManager_->sessionTerminateBatons_.find(peerId);
    if (batonIt != peerManager_->sessionTerminateBatons_.end()) {
      terminationBaton = batonIt->second;
    }
  });
  if (!terminationBaton) {
    return true;
  }

  XLOGF(
      INFO,
      "Waiting for session termination baton for peer: {}",
      peerAddr.str());
  const auto waitResult = folly::coro::blockingWait(
      folly::coro::timed_wait(
          [&]() -> folly::coro::Task<void> { co_await *terminationBaton; }(),
          timeout));
  if (!waitResult.has_value()) {
    XLOGF(
        WARN,
        "Timed out after {}ms waiting for session termination baton for "
        "peer: {}",
        timeout.count(),
        peerAddr.str());
    return false;
  }
  XLOGF(
      INFO, "Session termination baton received for peer: {}", peerAddr.str());
  return true;
}

void E2ETestFixture::beginPeerSessionTermination(
    const folly::IPAddress& peerAddr) {
  BgpPeerId peerId{peerAddr, peerAddr.asV4().toLongHBO()};
  auto queues = getPeerQueues(peerId);
  ASSERT_TRUE(queues.has_value());
  queues->adjRibInQ->fiberPush(FiberBgpPeer::BgpSessionStop{});

  auto batonIt = peerManager_->sessionTerminateBatons_.find(peerId);
  ASSERT_NE(batonIt, peerManager_->sessionTerminateBatons_.end());
  folly::coro::blockingWait(
      [&]() -> folly::coro::Task<void> { co_await *batonIt->second; }());
}

void E2ETestFixture::completePeerSessionTermination(
    const folly::IPAddress& peerAddr,
    bool peerDelete) {
  BgpPeerId peerId{peerAddr, peerAddr.asV4().toLongHBO()};
  FiberBgpPeer::ObservableStateT terminateEvent{
      .peerId = peerId,
      .state = BgpSessionState::IDLE,
      .versionNumber = 0,
      .sessionInfo = nullptr,
      .peerDelete = peerDelete};
  folly::coro::blockingWait(
      folly::coro::co_withExecutor(
          &peerManager_->getEventBase(),
          peerManager_->sessionTerminated(terminateEvent)));
}

void E2ETestFixture::bringDownPeerWithGr(const folly::IPAddress& peerAddr) {
  BgpPeerId peerId{peerAddr, peerAddr.asV4().toLongHBO()};
  auto it = peerQueues_.find(peerId);
  ASSERT_NE(it, peerQueues_.end());

  if (it->second.boundedAdjRibOutQ) {
    it->second.boundedAdjRibOutQ->close();
  }
  it->second.adjRibInQ->fiberPush(
      FiberBgpPeer::BgpSessionStop{.gracefulRestart = true});

  auto batonIt = peerManager_->sessionTerminateBatons_.find(peerId);
  ASSERT_NE(batonIt, peerManager_->sessionTerminateBatons_.end());
  folly::coro::blockingWait(
      [&]() -> folly::coro::Task<void> { co_await *batonIt->second; }());

  FiberBgpPeer::ObservableStateT terminateEvent{
      .peerId = peerId,
      .state = BgpSessionState::IDLE,
      .versionNumber = 0,
      .sessionInfo = nullptr};
  auto& evb = peerManager_->getEventBase();
  folly::via(&evb, [this, terminateEvent]() {
    folly::coro::blockingWait(peerManager_->sessionTerminated(terminateEvent));
  }).wait();
}

void E2ETestFixture::runAdjRibStop(const std::shared_ptr<AdjRib>& adjRib) {
  auto& evb = peerManager_->getEventBase();
  folly::via(&evb, [adjRib]() {
    folly::coro::blockingWait(adjRib->stop());
  }).wait();
}

PeerUpdateState E2ETestFixture::getPeerState(
    const nettools::bgplib::BgpPeerId& peerId) {
  PeerUpdateState state = PeerUpdateState::DOWN;
  auto& evb = peerManager_->getEventBase();
  folly::via(&evb, [&]() {
    auto adjRib = peerManager_->findAdjRib(peerId);
    if (adjRib) {
      state = adjRib->getPeerState();
    }
  }).wait();
  return state;
}

// ==================== LOCAL ROUTE MANAGEMENT ====================

void E2ETestFixture::addLocalRoute(
    const std::string& prefix,
    const std::vector<std::string>& communities,
    uint32_t localPref,
    const std::string& nexthop,
    uint32_t minSupportingRoutes) {
  thrift::BgpNetwork network;
  network.prefix() = prefix;

  if (!communities.empty()) {
    network.communities() = communities;
  }

  if (localPref != kLocalPref) {
    network.local_pref() = localPref;
  }

  if (!nexthop.empty()) {
    network.nexthop() = nexthop;
  }

  if (minSupportingRoutes > 0) {
    network.minimum_supporting_routes() = minSupportingRoutes;
  }

  auto cidr = folly::IPAddress::createNetwork(prefix);
  localRoutes_[cidr] = std::move(network);
}

void E2ETestFixture::injectLocalRoutesAtRuntime(
    const std::vector<std::string>& prefixes,
    const std::vector<std::string>& communities,
    uint32_t localPref) {
  std::map<
      facebook::neteng::fboss::bgp_attr::TIpPrefix,
      facebook::neteng::fboss::bgp::thrift::TBgpAttributes>
      routesToInject;

  for (const auto& prefixStr : prefixes) {
    auto cidr = folly::IPAddress::createNetwork(prefixStr);

    /* Build TIpPrefix */
    facebook::neteng::fboss::bgp_attr::TIpPrefix tPrefix;
    auto binAddr = facebook::network::toBinaryAddress(cidr.first);
    tPrefix.afi() = cidr.first.isV4()
        ? facebook::neteng::fboss::bgp_attr::TBgpAfi::AFI_IPV4
        : facebook::neteng::fboss::bgp_attr::TBgpAfi::AFI_IPV6;
    tPrefix.num_bits() = cidr.second;
    tPrefix.prefix_bin() = binAddr.addr()->toStdString();

    /* Build TBgpAttributes */
    facebook::neteng::fboss::bgp::thrift::TBgpAttributes attributes;

    if (!communities.empty()) {
      std::vector<facebook::neteng::fboss::bgp_attr::TBgpCommunity>
          tCommunities;
      for (const auto& commStr : communities) {
        folly::small_vector<std::string_view, 2> parts;
        folly::split(':', commStr, parts);
        if (parts.size() == 2) {
          facebook::neteng::fboss::bgp_attr::TBgpCommunity comm;
          comm.asn() = folly::to<uint16_t>(parts[0]);
          comm.value() = folly::to<uint16_t>(parts[1]);
          tCommunities.push_back(comm);
        }
      }
      attributes.communities() = std::move(tCommunities);
    }

    if (localPref != kLocalPref) {
      attributes.local_pref() = localPref;
    }

    routesToInject[tPrefix] = std::move(attributes);
  }

  /* Inject routes into RIB */
  rib_->injectLocalRoutes(routesToInject);
}

void E2ETestFixture::withdrawLocalRoutesAtRuntime(
    const std::vector<std::string>& prefixes) {
  std::set<facebook::neteng::fboss::bgp_attr::TIpPrefix> routesToWithdraw;

  for (const auto& prefixStr : prefixes) {
    auto cidr = folly::IPAddress::createNetwork(prefixStr);

    /* Build TIpPrefix */
    facebook::neteng::fboss::bgp_attr::TIpPrefix tPrefix;
    auto binAddr = facebook::network::toBinaryAddress(cidr.first);
    tPrefix.afi() = cidr.first.isV4()
        ? facebook::neteng::fboss::bgp_attr::TBgpAfi::AFI_IPV4
        : facebook::neteng::fboss::bgp_attr::TBgpAfi::AFI_IPV6;
    tPrefix.num_bits() = cidr.second;
    tPrefix.prefix_bin() = binAddr.addr()->toStdString();

    routesToWithdraw.insert(tPrefix);
  }

  rib_->removeLocalRoutes(routesToWithdraw);
}

// ==================== POLICY CONFIGURATION ====================

void E2ETestFixture::setPolicyConfig(const bgp_policy::BgpPolicies& policies) {
  policyConfig_ = policies;
}

void E2ETestFixture::addPrefixDenyPolicy(
    const std::vector<std::string>& prefixes) {
  /*
   * Create a policy that denies routes matching the specified prefixes.
   * Structure:
   *   Term 1: match prefix-list -> DENY
   *   Term 2: match any -> PERMIT (implicit accept-all fallback)
   */
  std::vector<routing_policy::PrefixListEntry> prefixEntries;
  for (const auto& prefixStr : prefixes) {
    std::vector<routing_policy::CompareNumericValue> emptyLenRange;
    auto entry = createPrefixListEntry(prefixStr, emptyLenRange);
    prefixEntries.push_back(entry);
  }

  auto prefixMatch = createPrefixListMatch(prefixEntries);
  auto denyAction =
      createBgpPolicyAction(bgp_policy::BgpPolicyActionType::DENY);

  auto denyTerm = createBgpPolicyTerm(
      "deny-prefix",
      "Deny matching prefixes",
      {std::move(prefixMatch)},
      {std::move(denyAction)},
      bgp_policy::FlowControlAction::NEXT_TERM);

  /* Accept-all fallback term */
  auto acceptAction =
      createBgpPolicyAction(bgp_policy::BgpPolicyActionType::PERMIT);
  auto acceptTerm = createBgpPolicyTerm(
      "accept-all",
      "Accept all other routes",
      {},
      {std::move(acceptAction)},
      bgp_policy::FlowControlAction::NEXT_TERM);

  policyConfig_ = createBgpPolicies(
      "prefix-deny-policy", {std::move(denyTerm), std::move(acceptTerm)});
  ingressPolicyName_ = "prefix-deny-policy";
}

void E2ETestFixture::addCommunityDenyPolicy(const std::string& community) {
  /*
   * Create a policy that denies routes matching the specified community.
   */
  auto communityMatch = createBgpPolicyAtomicMatch(
      bgp_policy::BgpPolicyAtomicMatchType::COMMUNITY_LIST, {community});
  auto denyAction =
      createBgpPolicyAction(bgp_policy::BgpPolicyActionType::DENY);

  auto denyTerm = createBgpPolicyTerm(
      "deny-community",
      "Deny routes with matching community",
      {std::move(communityMatch)},
      {std::move(denyAction)},
      bgp_policy::FlowControlAction::NEXT_TERM);

  /* Accept-all fallback term */
  auto acceptAction =
      createBgpPolicyAction(bgp_policy::BgpPolicyActionType::PERMIT);
  auto acceptTerm = createBgpPolicyTerm(
      "accept-all",
      "Accept all other routes",
      {},
      {std::move(acceptAction)},
      bgp_policy::FlowControlAction::NEXT_TERM);

  policyConfig_ = createBgpPolicies(
      "community-deny-policy", {std::move(denyTerm), std::move(acceptTerm)});
  ingressPolicyName_ = "community-deny-policy";
}

void E2ETestFixture::addCommunitySetLocalPrefPolicy(
    const std::string& matchCommunity,
    uint32_t localPref) {
  /*
   * Create a policy that sets local preference for routes matching a community.
   */
  auto communityMatch = createBgpPolicyAtomicMatch(
      bgp_policy::BgpPolicyAtomicMatchType::COMMUNITY_LIST, {matchCommunity});
  auto setLpAction = createActionSetLocalPreference(localPref);
  auto acceptAction =
      createBgpPolicyAction(bgp_policy::BgpPolicyActionType::PERMIT);

  auto matchTerm = createBgpPolicyTerm(
      "set-localpref",
      "Set local preference for matching community",
      {std::move(communityMatch)},
      {std::move(setLpAction), std::move(acceptAction)},
      bgp_policy::FlowControlAction::NEXT_TERM);

  /* Accept-all fallback term */
  auto acceptAllAction =
      createBgpPolicyAction(bgp_policy::BgpPolicyActionType::PERMIT);
  auto acceptTerm = createBgpPolicyTerm(
      "accept-all",
      "Accept all other routes",
      {},
      {std::move(acceptAllAction)},
      bgp_policy::FlowControlAction::NEXT_TERM);

  policyConfig_ = createBgpPolicies(
      "community-setlp-policy", {std::move(matchTerm), std::move(acceptTerm)});
  ingressPolicyName_ = "community-setlp-policy";
}

void E2ETestFixture::addAcceptAllEgressPolicy(const std::string& name) {
  /*
   * Permit-all policy with no community match or community action, so
   * PolicyAttributesMask::communities stays false and the egress policy
   * cache key ignores communities. Egress-only: the caller sets
   * egressPolicyName on the receiver peer spec; we do not touch
   * ingressPolicyName_.
   */
  auto acceptAction =
      createBgpPolicyAction(bgp_policy::BgpPolicyActionType::PERMIT);
  auto acceptTerm = createBgpPolicyTerm(
      "accept-all",
      "Accept all routes",
      {},
      {std::move(acceptAction)},
      bgp_policy::FlowControlAction::NEXT_TERM);

  policyConfig_ = createBgpPolicies(name, {std::move(acceptTerm)});
}

bool E2ETestFixture::waitForRouteInShadowRib(
    const folly::CIDRNetwork& prefix,
    std::optional<folly::IPAddress> fromPeer,
    int maxRetries) {
  if (!peerManager_) {
    XLOG(
        ERR,
        "PeerManagerBase must be created before calling waitForRouteInShadowRib");
    return false;
  }

  auto& evb = peerManager_->getEventBase();
  bool found = false;

  /*
   * Use deterministic retry loop instead of sleep
   * This matches the pattern used in readOutboundUpdateToPeer()
   */
  WITH_RETRIES_N(maxRetries, {
    /*
     * Run the check on PeerManagerBase's event base thread
     * This is thread-safe because shadowRibEntries_ is only accessed
     * from PeerManagerBase's event base
     */
    found = folly::via(
                &evb,
                [this, prefix, fromPeer]() -> bool {
                  auto it = peerManager_->shadowRibEntries_.find(prefix);
                  if (it == peerManager_->shadowRibEntries_.end()) {
                    return false;
                  }
                  /*
                   * If fromPeer is specified, check that the bestpath
                   * is from that specific peer
                   */
                  if (fromPeer.has_value()) {
                    const auto& entry = it->second->get();
                    if (!entry.bestpath) {
                      return false;
                    }
                    return entry.bestpath->peer.addr == fromPeer.value();
                  }
                  return true;
                })
                .wait()
                .value();

    EXPECT_EVENTUALLY_TRUE(found);
  });

  if (found) {
    if (fromPeer.has_value()) {
      XLOGF(
          DBG2,
          "Route {} from peer {} found in shadowRIB",
          folly::IPAddress::networkToString(prefix),
          fromPeer.value().str());
    } else {
      XLOGF(
          DBG2,
          "Route {} found in shadowRIB",
          folly::IPAddress::networkToString(prefix));
    }
  } else {
    if (fromPeer.has_value()) {
      XLOGF(
          WARN,
          "Route {} from peer {} NOT found in shadowRIB after {} retries",
          folly::IPAddress::networkToString(prefix),
          fromPeer.value().str(),
          maxRetries);
    } else {
      XLOGF(
          WARN,
          "Route {} NOT found in shadowRIB after {} retries",
          folly::IPAddress::networkToString(prefix),
          maxRetries);
    }
  }

  return found;
}

bool E2ETestFixture::verifyRouteNotInShadowRib(
    const folly::CIDRNetwork& prefix,
    int waitRetries) {
  if (!peerManager_) {
    XLOG(
        ERR,
        "PeerManagerBase must be created before calling verifyRouteNotInShadowRib");
    return false;
  }

  auto& evb = peerManager_->getEventBase();

  /*
   * Wait for the route to NOT be present in shadowRib. With change list
   * tracker and backpressure enabled, there can be a delay between the
   * RIB processing a withdrawal and PeerManagerBase updating shadowRib.
   * Retry until the route disappears or retries are exhausted.
   */
  for (int i = 0; i < waitRetries; ++i) {
    bool found = folly::via(
                     &evb,
                     [this, prefix]() -> bool {
                       return peerManager_->shadowRibEntries_.find(prefix) !=
                           peerManager_->shadowRibEntries_.end();
                     })
                     .wait()
                     .value();

    if (!found) {
      XLOGF(
          DBG2,
          "Route {} correctly NOT found in shadowRIB on retry {}",
          folly::IPAddress::networkToString(prefix),
          i);
      return true;
    }

    // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  XLOGF(
      WARN,
      "Route {} still found in shadowRIB after {} retries",
      folly::IPAddress::networkToString(prefix),
      waitRetries);
  return false;
}

bool E2ETestFixture::waitForPathCountInRib(
    const std::string& prefixStr,
    size_t minPathCount,
    int maxRetries) {
  if (!rib_) {
    XLOG(ERR, "RIB must be created before calling waitForPathCountInRib");
    return false;
  }

  bool found = false;

  WITH_RETRIES_N(maxRetries, {
    auto ribEntries =
        rib_->getRibEntryForPrefix(std::make_unique<std::string>(prefixStr));
    if (!ribEntries.empty()) {
      size_t totalPaths = 0;
      for (const auto& [key, paths] : *ribEntries[0].paths()) {
        totalPaths += paths.size();
      }
      found = (totalPaths >= minPathCount);
    }
    EXPECT_EVENTUALLY_TRUE(found);
  });

  if (found) {
    XLOGF(
        DBG2, "Route {} has at least {} paths in RIB", prefixStr, minPathCount);
  } else {
    XLOGF(
        WARN,
        "Route {} does NOT have {} paths in RIB after {} retries",
        prefixStr,
        minPathCount,
        maxRetries);
  }

  return found;
}

bool E2ETestFixture::waitForRouteWithdrawnFromRib(
    const std::string& prefixStr,
    int maxRetries) {
  if (!rib_) {
    XLOG(
        ERR, "RIB must be created before calling waitForRouteWithdrawnFromRib");
    return false;
  }

  bool withdrawn = false;

  WITH_RETRIES_N(maxRetries, {
    auto ribEntries =
        rib_->getRibEntryForPrefix(std::make_unique<std::string>(prefixStr));
    if (ribEntries.empty()) {
      withdrawn = true;
    } else {
      size_t totalPaths = 0;
      for (const auto& [key, paths] : *ribEntries[0].paths()) {
        totalPaths += paths.size();
      }
      withdrawn = (totalPaths == 0);
    }
    EXPECT_EVENTUALLY_TRUE(withdrawn);
  });

  if (withdrawn) {
    XLOGF(DBG2, "Route {} withdrawn from RIB", prefixStr);
  } else {
    XLOGF(
        WARN,
        "Route {} still has paths in RIB after {} retries",
        prefixStr,
        maxRetries);
  }

  return withdrawn;
}

size_t E2ETestFixture::getMultipathNexthopCount(const std::string& prefixStr) {
  if (!rib_) {
    XLOG(ERR, "RIB must be created before calling getMultipathNexthopCount");
    return 0;
  }

  size_t nexthopCount = 0;
  auto prefix = folly::IPAddress::createNetwork(prefixStr);

  rib_->getEventBase().runInEventBaseThreadAndWait([&]() {
    auto ribEntry = rib_->ribEntries_.find(prefix);
    if (ribEntry != rib_->ribEntries_.end()) {
      auto weightedNexthops = ribEntry->second.getMultipathWeightedNexthops();
      if (weightedNexthops) {
        nexthopCount = weightedNexthops->size();
      }
    }
  });

  return nexthopCount;
}

bool E2ETestFixture::waitForMultipathNexthopCount(
    const std::string& prefixStr,
    size_t minNexthops,
    int maxRetries) {
  if (!rib_) {
    XLOG(
        ERR, "RIB must be created before calling waitForMultipathNexthopCount");
    return false;
  }

  bool found = false;
  auto prefix = folly::IPAddress::createNetwork(prefixStr);

  WITH_RETRIES_N(maxRetries, {
    size_t nexthopCount = 0;
    rib_->getEventBase().runInEventBaseThreadAndWait([&]() {
      auto ribEntry = rib_->ribEntries_.find(prefix);
      if (ribEntry != rib_->ribEntries_.end()) {
        auto weightedNexthops = ribEntry->second.getMultipathWeightedNexthops();
        if (weightedNexthops) {
          nexthopCount = weightedNexthops->size();
        }
      }
    });
    found = (nexthopCount >= minNexthops);
    EXPECT_EVENTUALLY_TRUE(found);
  });

  if (found) {
    XLOGF(
        DBG2,
        "Route {} has at least {} multipath nexthops",
        prefixStr,
        minNexthops);
  } else {
    XLOGF(
        WARN,
        "Route {} does NOT have {} multipath nexthops after {} retries",
        prefixStr,
        minNexthops,
        maxRetries);
  }

  return found;
}

std::map<folly::IPAddress, uint32_t> E2ETestFixture::getWeightedNexthops(
    const std::string& prefixStr) {
  std::map<folly::IPAddress, uint32_t> result;

  if (!rib_) {
    XLOG(ERR, "RIB must be created before calling getWeightedNexthops");
    return result;
  }

  auto prefix = folly::IPAddress::createNetwork(prefixStr);

  rib_->getEventBase().runInEventBaseThreadAndWait([&]() {
    auto ribEntry = rib_->ribEntries_.find(prefix);
    if (ribEntry != rib_->ribEntries_.end()) {
      auto weightedNexthops = ribEntry->second.getMultipathWeightedNexthops();
      if (weightedNexthops) {
        for (const auto& [nexthop, weight] : *weightedNexthops) {
          result[nexthop] = weight;
        }
      }
    }
  });

  return result;
}

// ==================== COMPONENT LIFECYCLE ====================

void E2ETestFixture::TearDown() {
  XLOG(INFO, "=== TearDown starting ===");

  /*
   * Flush the PeerManagerBase event base before inspecting any peer/queue
   * state from the test thread. Without this, in-flight evb writes
   * (e.g. markPeerUnblocked in the deferredPushToPeer SCOPE_EXIT) can race
   * the test-thread reads of queue size / isBlocked a few lines below,
   * causing TSan reports that mark the test as flaky.
   */
  if (peerManager_) {
    peerManager_->getEventBase().runInEventBaseThreadAndWait([] {});
  }

  auto allQueues = getAllPeerQueues();

  /* Log the state of all peers before termination */
  for (const auto& [peerId, queues] : allQueues) {
    size_t queueSize = queues.boundedAdjRibOutQ->size();
    bool isBlocked = queues.boundedAdjRibOutQ->isBlocked();
    XLOGF(
        INFO,
        "TearDown: peer {} queueSize={} isBlocked={}",
        peerId.peerAddr.str(),
        queueSize,
        isBlocked);
  }

  /*
   * CRITICAL FIX: Close all bounded queues BEFORE sending BgpSessionStop.
   *
   * This breaks the circular reference deadlock:
   * - deferredPushToPeer() coroutine holds shared_ptr<AdjRib>
   * - AdjRib holds shared_ptr<AdjRibOutGroup>
   * - AdjRibOutGroup owns the coroutine on asyncScope_
   * - Coroutine is suspended at waitToPush()
   *
   * By closing the queue first, we wake up the suspended coroutine.
   * The coroutine exits, releases the shared_ptr<AdjRib>, breaking the cycle.
   */
  XLOG(INFO, "TearDown: Closing all bounded queues to wake up waiters");
  for (const auto& [peerId, queues] : allQueues) {
    if (queues.boundedAdjRibOutQ) {
      queues.boundedAdjRibOutQ->close();
      XLOGF(
          INFO,
          "TearDown: Closed bounded queue for peer {}",
          peerId.peerAddr.str());
    }
  }

  /* Terminate peer sessions */
  XLOG(INFO, "TearDown: Sending BgpSessionStop to all peers");
  for (const auto& [peerId, queues] : allQueues) {
    XLOGF(
        INFO,
        "TearDown: Sending BgpSessionStop to peer {}",
        peerId.peerAddr.str());
    queues.adjRibInQ->fiberPush(FiberBgpPeer::BgpSessionStop{});
  }

  /* Stop PeerManagerBase */
  XLOG(INFO, "TearDown: Stopping PeerManagerBase");
  if (peerManager_) {
    peerManager_->stop();
    XLOG(INFO, "TearDown: PeerManagerBase stop() returned, joining thread");
    if (peerMgrThread_.joinable()) {
      peerMgrThread_.join();
    }
    XLOG(INFO, "TearDown: PeerManagerBase thread joined");
  }

  /* Stop SessionManager */
  XLOG(INFO, "TearDown: Stopping SessionManager");
  if (sessionManager_) {
    sessionManager_->stop();
    if (sessionMgrThread_ && sessionMgrThread_->joinable()) {
      sessionMgrThread_->join();
    }
    XLOG(INFO, "TearDown: SessionManager thread joined");
  }

  /* Stop RIB */
  XLOG(INFO, "TearDown: Stopping RIB");
  if (rib_) {
    rib_->stop();
    if (ribThread_.joinable()) {
      ribThread_.join();
    }
    XLOG(INFO, "TearDown: RIB thread joined");
  }

  peerManager_.reset();
  sessionManager_.reset();
  rib_.reset();

  XLOG(INFO, "=== TearDown complete ===");
}

void E2ETestFixture::createRib(
    bool enableNexthopTracking,
    const std::unordered_map<folly::CIDRNetwork, thrift::BgpNetwork>&
        localRoutes,
    FsdbSyncer* fsdbSyncer) {
  XLOG(INFO, "=== Creating RIB... ===");
  XLOGF(
      INFO,
      "Nexthop tracking: {}",
      enableNexthopTracking ? "enabled" : "disabled");

  /*
   * The rib policy files default to fixed paths shared by every bgp test
   * process on the host. The RIB reads the state file and installs it as the
   * bootstrap path selection policy, so give each fixture its own pair.
   */
  FLAGS_rp_state_file = (ribPolicyDir_.path() / "rp_state.txt").string();
  FLAGS_rp_change_history_file =
      (ribPolicyDir_.path() / "rp_change_history.txt").string();

  // Store the nexthop tracking setting so it can be reused by createPeerManager
  enableNexthopTracking_ = enableNexthopTracking;

  /*
   * If a policy is configured, set the ingress policy name on all peers.
   * This must be done before getConfig() creates the Config object.
   */
  if (ingressPolicyName_.has_value()) {
    for (auto& peer : peers_) {
      peer.ingress_policy_name() = *ingressPolicyName_;
    }
    XLOGF(
        INFO,
        "Set ingress policy '{}' on {} peers",
        *ingressPolicyName_,
        peers_.size());
  }

  /* Get config - uses enableNexthopTracking_ and enableComputeUcmpFromLbw_ */
  auto bgpConfig = getConfig();
  auto globalConfig = bgpConfig->getBgpGlobalConfig();
  ASSERT_NE(globalConfig, nullptr);

  /*
   * Use provided localRoutes if non-empty, otherwise use member variable
   * This allows both explicit passing and using addLocalRoute()
   */
  const auto& routesToUse = localRoutes.empty() ? localRoutes_ : localRoutes;

  // Create NexthopCache if nexthop tracking is enabled
  std::shared_ptr<NexthopCache> nexthopCache = nullptr;
  if (enableNexthopTracking) {
    nexthopCache = std::make_shared<NexthopCache>();
    XLOG(INFO, "Created NexthopCache for nexthop tracking");
  }

  // Create a new BgpGlobalConfig with nexthop tracking if needed
  std::shared_ptr<BgpGlobalConfig> modifiedGlobalConfig;
  if (enableNexthopTracking && !globalConfig->enableNextHopTracking) {
    // Need to create new config with nexthop tracking enabled
    modifiedGlobalConfig = std::make_shared<BgpGlobalConfig>(
        globalConfig->localAsn,
        globalConfig->routerId,
        globalConfig->clusterId,
        globalConfig->holdTime,
        globalConfig->listenAddr,
        globalConfig->grRestartTime,
        globalConfig->networksV4,
        globalConfig->networksV6,
        globalConfig->localConfedAsn,
        globalConfig->computeUcmpFromLbwComm,
        globalConfig->ucmpWidth,
        globalConfig->ucmpQuantizer,
        globalConfig->validateRemoteAs,
        globalConfig->supportStatefulGr,
        globalConfig->enableServerSocket,
        globalConfig->allowLoopbackReflection,
        globalConfig->countConfedsInAsPathLen,
        globalConfig->deviceName,
        globalConfig->switchLimitConfig,
        globalConfig->dynamicPeerLimit,
        globalConfig->streamSubscriberLimit,
        EnableNexthopTracking{true}); // Enable nexthop tracking
  } else {
    modifiedGlobalConfig =
        std::const_pointer_cast<BgpGlobalConfig>(globalConfig);
  }

  /* Build the platform-specific TestRib. makeTestRib() is defined by the
   * linked fixture library (E2ETestRibBB.cpp for RibBB, E2ETestRibDC.cpp for
   * RibDC), which selects the platform — see E2ETestRibFactory.h. */
  rib_ = makeTestRib(
      routesToUse,
      *modifiedGlobalConfig,
      policyConfig_,
      ribInQ_,
      ribOutQ_,
      nexthopCache,
      fsdbSyncer);

  rib_->createFib();

  /* Start RIB thread using runInThread() */
  ribThread_ = rib_->runInThread();

  rib_->getEventBase().waitUntilRunning();
  XLOG(INFO, "RIB running");
}

TestFib* E2ETestFixture::getTestFib() {
  auto* testRib = dynamic_cast<TestRibIf*>(rib_.get());
  return testRib ? testRib->getTestFib() : nullptr;
}

void E2ETestFixture::sendUnsupportedDcPolicyMsgs() {
  /*
   * Push DC-only policy messages directly onto the RIB policy queue (friend
   * access to RibBase internals). On a BB-linked binary,
   * RibBB::processRibPolicyMsgLoop rejects each one with an error log and a
   * numUnsupportedPolicyMsg increment, without mutating RIB state.
   */
  rib_->getEventBase().runInEventBaseThreadAndWait([this]() {
    rib_->enqueueRibPolicyMsg(RibBase::PathSelectionPolicyClearMsg{});
    rib_->enqueueRibPolicyMsg(RibBase::RouteAttributePolicyClearMsg{});
    rib_->enqueueRibPolicyMsg(RibBase::RouteAttributePolicyTimerMsg{});
  });
}

namespace {

BgpPeerDisplayInfo createDisplayInfo(
    const BgpPeerId& peerId,
    const BgpCommonPeerGroupConfig& cfg,
    const std::shared_ptr<const BgpGlobalConfig>& globalConfig,
    const std::optional<nettools::bgplib::BgpAddPathSendRec>& addPathCapa =
        std::nullopt,
    std::optional<uint16_t> grRestartTimeSeconds = std::nullopt,
    bool mpExtCapable = true,
    bool enableRouteRefresh = false) {
  BgpPeerDisplayInfo displayInfo;
  displayInfo.peeringParams.peerAddr = peerId.peerAddr;
  displayInfo.peeringParams.remoteAs = cfg.peerAsn;
  displayInfo.peeringParams.localAs =
      cfg.localAsn.value_or(globalConfig->localAsn);
  displayInfo.peeringParams.globalAs = globalConfig->localAsn;
  /*
   * Confederation plumbing — mirror production Config::getPeeringParamsHelper
   * (Config.cpp ~lines 1011-1021). Without this, getBgpSessionType() classifies
   * confed peers as plain EBGP and tests for ConfedEBGP semantics
   * (e.g., NO_EXPORT allowed to ConfedEBGP per RFC 1997) fail.
   */
  displayInfo.peeringParams.isConfedPeer =
      ConfedPeerConfigured{cfg.isConfedPeer.value_or(false)};
  if (displayInfo.peeringParams.isConfedPeer &&
      globalConfig->localConfedAsn.has_value()) {
    displayInfo.peeringParams.localConfedAs = *globalConfig->localConfedAsn;
    displayInfo.peeringParams.asConfedId =
        cfg.localAsn.value_or(globalConfig->localAsn);
    displayInfo.peeringParams.localAs = *globalConfig->localConfedAsn;
  }
  /*
   * Route-Reflector client plumbing — production parses peer.is_rr_client
   * (Config.cpp:518) into the per-peer BgpCommonPeerGroupConfig, which we
   * propagate here so tests can exercise RR-aware code paths.
   */
  displayInfo.peeringParams.isRrClient =
      RrClientConfigured{cfg.isRrClient.value_or(false)};
  displayInfo.peeringParams.localBgpId = globalConfig->routerId.asV4();
  displayInfo.peeringParams.localClusterId = globalConfig->clusterId.asV4();
  displayInfo.peeringParams.holdTime =
      cfg.holdTime.value_or(std::chrono::seconds(90));
  displayInfo.peeringParams.description = cfg.description.value_or("");
  /*
   * Propagate the peer group name (mirrors production
   * Config::getPeeringParamsHelper, Config.cpp:1153). peerGroupName is part of
   * the update-group key, so without this every peer keys to "" and distinct
   * peer groups would collapse into a single update group.
   */
  displayInfo.peeringParams.peerGroupName = cfg.peerGroupName;
  displayInfo.peeringParams.isAfiIpv4Configured =
      !cfg.disableIpv4Afi.value_or(false);
  displayInfo.peeringParams.isAfiIpv6Configured =
      !cfg.disableIpv6Afi.value_or(false);
  displayInfo.peeringParams.nexthopV4 = cfg.nexthopV4;
  displayInfo.peeringParams.nexthopV6 = cfg.nexthopV6;
  displayInfo.peeringParams.advertiseLinkBandwidth = cfg.advertiseLinkBandwidth;
  displayInfo.peeringParams.receiveLinkBandwidth = cfg.receiveLinkBandwidth;
  displayInfo.peeringParams.linkBandwidthBps = cfg.linkBandwidthBps;
  displayInfo.remoteBgpId = peerId.remoteBgpId;
  displayInfo.remoteAs = cfg.peerAsn;
  /*
   * Model the peer's remote (pre-negotiation) MP-EXT-exists bit. A
   * capability-less peer advertised no MP-EXT (mpExtCapable = false), while
   * negotiated v4 stays true (below), mirroring negotiateCapabilities().
   */
  displayInfo.remoteCapabilities.mpExtExist() = mpExtCapable;
  displayInfo.negotiatedCapabilities.mpExtV4Unicast() =
      !cfg.disableIpv4Afi.value_or(false);
  displayInfo.negotiatedCapabilities.mpExtV6Unicast() =
      !cfg.disableIpv6Afi.value_or(false);
  /* Enable 4-byte ASN capability (most common at Meta, required for ASNs >
   * 65535) */
  displayInfo.negotiatedCapabilities.as4byte() = true;
  /*
   * Route Refresh capabilities are opt-in per peer via
   * BgpPeerSpec::enableRouteRefresh. Tests that exercise inbound RR via
   * sendRouteRefreshToPeer or outbound RR via co_clearBgpNeighbor must
   * enable this on the relevant peer specs.
   */
  if (enableRouteRefresh) {
    displayInfo.negotiatedCapabilities.routeRefresh() = true;
    displayInfo.negotiatedCapabilities.enhancedRouteRefresh() = false;
  }

  /*
   * Enable GR capability for tests that opt in via setPeerGrRestartTime().
   * Without this, AdjRib::sessionTerminated() with gracefulRestart=true
   * still falls into the immediate-purge branch (remoteGrRestartTime_ == 0s).
   */
  if (grRestartTimeSeconds.has_value()) {
    displayInfo.negotiatedCapabilities.gracefulRestart() = true;
    displayInfo.negotiatedCapabilities.restartTime() =
        grRestartTimeSeconds.value();
    displayInfo.remoteGrRestartTime = grRestartTimeSeconds.value();
  }

  /* Set ADD-PATH capabilities if enabled for this peer */
  if (addPathCapa.has_value()) {
    nettools::bgplib::BgpAddPathCapability v4AddPath;
    v4AddPath.afi() = nettools::bgplib::BgpUpdateAfi::AFI_IPv4;
    v4AddPath.safi() = nettools::bgplib::BgpUpdateSafi::SAFI_UNICAST;
    v4AddPath.sor() = addPathCapa.value();

    nettools::bgplib::BgpAddPathCapability v6AddPath;
    v6AddPath.afi() = nettools::bgplib::BgpUpdateAfi::AFI_IPv6;
    v6AddPath.safi() = nettools::bgplib::BgpUpdateSafi::SAFI_UNICAST;
    v6AddPath.sor() = addPathCapa.value();

    displayInfo.negotiatedCapabilities.addPathCapabilities()->push_back(
        v4AddPath);
    displayInfo.negotiatedCapabilities.addPathCapabilities()->push_back(
        v6AddPath);
  }

  return displayInfo;
}

} // namespace

void E2ETestFixture::createPeerManager(
    bool enableUpdateGroup,
    bool enableSerializeGroupPdu) {
  XLOG(INFO, "=== Creating PeerManagerBase... ===");

  /* Get config with dynamic peers */
  config_ = getConfig(enableUpdateGroup, enableSerializeGroupPdu);
  auto globalConfig = config_->getBgpGlobalConfig();
  ASSERT_NE(globalConfig, nullptr);

  /* Create MockSessionManager */
  sessionManager_ = std::make_shared<MockSessionManager>(*globalConfig, true);
  sessionMgrThread_ =
      std::make_shared<std::thread>(sessionManager_->runInThread());

  /* Create PeerManagerBase */
  configManager_ = std::make_shared<ConfigManager>(config_);

  /*
   * Create PolicyManager if policy config is set.
   * This enables policy-based E2E tests (deny, attribute modification, etc.)
   */
  std::shared_ptr<PolicyManager> policyManager = nullptr;
  if (policyConfig_.has_value()) {
    policyManager =
        std::make_shared<PolicyManager>(*policyConfig_, globalConfig.get());
    XLOG(INFO, "Created PolicyManager for E2E policy tests");
  }

  peerManager_ = std::make_unique<PeerManagerBase>(
      configManager_, policyManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

  peerManager_->setSessionManager(sessionManager_);

  /* Start PeerManagerBase thread */
  peerMgrThread_ = std::thread([this]() {
    XLOG(INFO, "PeerManagerBase thread started");
    peerManager_->run();
    XLOG(INFO, "PeerManagerBase thread exiting");
  });

  peerManager_->getEventBase().waitUntilRunning();
  XLOG(INFO, "PeerManagerBase running");
}

void E2ETestFixture::establishSession(
    const BgpPeerId& peerId,
    uint64_t versionNumber) {
  XLOGF(
      INFO, "=== Establishing session for peer: {} ===", peerId.peerAddr.str());

  auto qs = getQueueSizesForPeer(peerId.peerAddr);
  auto adjRibInQ = std::make_shared<AdjRib::AdjRibInQueueT>();
  auto boundedAdjRibOutQ = std::make_shared<AdjRib::AdjRibOutQueueT>(
      qs.capacity, qs.highWm, qs.lowWm);

  XLOGF(
      INFO,
      "Test created bounded queue {} for peer {} (capacity={}, highWm={}, lowWm={})",
      (void*)boundedAdjRibOutQ.get(),
      peerId.peerAddr.str(),
      qs.capacity,
      qs.highWm,
      qs.lowWm);

  peerQueues_[peerId] = {adjRibInQ, boundedAdjRibOutQ};
  auto currentVersion = std::make_shared<VersionNumber>(versionNumber);

  // Get peer configuration to populate peeringParams
  auto peerConfig = config_->getConfigOfAPeer(peerId.peerAddr);
  ASSERT_TRUE(peerConfig.has_value())
      << "Peer config not found for " << peerId.peerAddr.str();

  auto& cfg = peerConfig.value();
  auto globalConfig = config_->getBgpGlobalConfig();

  /* Look up ADD-PATH capability for this peer */
  std::optional<nettools::bgplib::BgpAddPathSendRec> addPathCapa = std::nullopt;
  auto addPathIt = peerAddPathCapabilities_.find(peerId.peerAddr);
  if (addPathIt != peerAddPathCapabilities_.end()) {
    addPathCapa = addPathIt->second;
  }

  bool mpExtCapable = true;
  auto mpExtIt = peerMpExtCapable_.find(peerId.peerAddr);
  if (mpExtIt != peerMpExtCapable_.end()) {
    mpExtCapable = mpExtIt->second;
  }

  const bool enableRR = peerRouteRefreshEnabled_.contains(peerId.peerAddr);
  auto displayInfo = createDisplayInfo(
      peerId,
      cfg,
      globalConfig,
      addPathCapa,
      peerGrRestartTimeSeconds_,
      mpExtCapable,
      enableRR);
  if (localClusterIdOverride_.has_value()) {
    displayInfo.peeringParams.localClusterId = *localClusterIdOverride_;
  }

  auto sessionInfo = FiberBgpPeer::getObservableSessionInfo(
      displayInfo,
      nullptr /* unbounded input queue, unused */,
      std::move(boundedAdjRibOutQ),
      std::move(adjRibInQ),
      currentVersion);

  FiberBgpPeer::ObservableStateT stateEvent{
      .peerId = peerId,
      .state = BgpSessionState::ESTABLISHED,
      .versionNumber = versionNumber,
      .remoteAs = cfg.peerAsn,
      .sessionInfo = std::move(sessionInfo)};

  auto& evb = peerManager_->getEventBase();
  folly::via(&evb, [this, stateEvent = std::move(stateEvent)]() mutable {
    folly::coro::blockingWait(
        peerManager_->sessionEstablished(std::move(stateEvent)));
  }).wait();

  XLOGF(INFO, "Session established for peer: {}", peerId.peerAddr.str());
}

void E2ETestFixture::dispatchStaleSessionEstablished(
    const folly::IPAddress& peerAddr) {
  BgpPeerId peerId{peerAddr, peerAddr.asV4().toLongHBO()};
  XLOGF(
      INFO,
      "=== Dispatching stale sessionEstablished for peer: {} ===",
      peerAddr.str());

  auto staleQs = getQueueSizesForPeer(peerAddr);
  auto adjRibInQ = std::make_shared<AdjRib::AdjRibInQueueT>();
  auto boundedAdjRibOutQ = std::make_shared<AdjRib::AdjRibOutQueueT>(
      staleQs.capacity, staleQs.highWm, staleQs.lowWm);

  // Create VersionNumber with an initial value
  uint64_t staleVersionNumber = 42;
  auto currentVersion = std::make_shared<VersionNumber>(staleVersionNumber);

  auto peerConfig = config_->getConfigOfAPeer(peerAddr);
  ASSERT_TRUE(peerConfig.has_value())
      << "Peer config not found for " << peerAddr.str();
  auto& cfg = peerConfig.value();
  auto globalConfig = config_->getBgpGlobalConfig();
  auto displayInfo = createDisplayInfo(peerId, cfg, globalConfig, std::nullopt);
  if (localClusterIdOverride_.has_value()) {
    displayInfo.peeringParams.localClusterId = *localClusterIdOverride_;
  }

  auto sessionInfo = FiberBgpPeer::getObservableSessionInfo(
      displayInfo,
      nullptr /* unbounded input queue, unused */,
      std::move(boundedAdjRibOutQ),
      std::move(adjRibInQ),
      currentVersion);

  FiberBgpPeer::ObservableStateT staleEvent{
      .peerId = peerId,
      .state = BgpSessionState::ESTABLISHED,
      .versionNumber = staleVersionNumber, // captured before bump
      .remoteAs = cfg.peerAsn,
      .sessionInfo = std::move(sessionInfo)};

  /*
   * Simulate FiberBgpPeer bumping the version for a new session incarnation.
   * Now currentVersion->getWithoutLock() != staleVersionNumber, causing
   * the version check inside sessionEstablished() to fail -> early return.
   * Critically, the baton is NOT reset on early return (latch stays posted).
   */
  currentVersion->bumpUp();

  auto& evb = peerManager_->getEventBase();
  folly::via(&evb, [this, staleEvent = std::move(staleEvent)]() mutable {
    folly::coro::blockingWait(
        peerManager_->sessionEstablished(std::move(staleEvent)));
  }).wait();

  XLOGF(
      INFO,
      "Stale sessionEstablished dispatched (early return expected) for peer: {}",
      peerAddr.str());
}

void E2ETestFixture::sendUpdateToPeer(
    const BgpPeerId& peerId,
    const folly::CIDRNetwork& prefix) {
  XLOGF(
      INFO,
      "=== Sending UPDATE to peer {} for prefix {} ===",
      peerId.peerAddr.str(),
      prefix.first.str());

  auto queues = getPeerQueues(peerId);
  ASSERT_TRUE(queues.has_value())
      << "No queues for peer " << peerId.peerAddr.str();

  auto update = createV4BgpUpdateSingleAnnounce(
      prefix, kV4Nexthop1, kMed, kOriginatorId, BgpAttrOrigin::BGP_ORIGIN_EGP);

  queues->adjRibInQ->fiberPush(std::move(update));
}

void E2ETestFixture::sendEoRToPeer(const BgpPeerId& peerId) {
  XLOGF(INFO, "Sending EoR to peer: {}", peerId.peerAddr.str());
  auto queues = getPeerQueues(peerId);
  ASSERT_TRUE(queues.has_value())
      << "No queues for peer " << peerId.peerAddr.str();
  queues->adjRibInQ->fiberPush(BgpEndOfRib{});
}

void E2ETestFixture::sendRouteRefreshToPeer(
    const BgpPeerId& peerId,
    nettools::bgplib::BgpUpdateAfi afi,
    nettools::bgplib::BgpUpdateSafi safi) {
  XLOGF(INFO, "Sending Route Refresh to peer: {}", peerId.peerAddr.str());
  auto it = peerQueues_.find(peerId);
  ASSERT_NE(it, peerQueues_.end());
  nettools::bgplib::BgpRouteRefresh rr;
  rr.afi() = afi;
  rr.msgSubType() =
      nettools::bgplib::BgpRouteRefreshMessageSubtype::ROUTE_REFRESH_REQUEST;
  rr.safi() = safi;
  it->second.adjRibInQ->fiberPush(std::move(rr));
}

void E2ETestFixture::processRouteRefreshEventForTesting(
    const BgpPeerId& peerId,
    nettools::bgplib::BgpUpdateAfi afi) {
  AdjRib::ObservableMessageT event{peerId, AdjRib::RouteRefreshReceived{afi}};
  folly::coro::blockingWait(
      folly::coro::co_withExecutor(
          &peerManager_->getEventBase(),
          peerManager_->processAdjRibEvent(std::move(event))));
}

/*
 * ==================== HELPER FUNCTIONS FOR QUEUE/MESSAGE HANDLING
 * ====================
 */

namespace {

bool isIpv4Protocol(const std::string& protocol) {
  return protocol == "v4" || protocol == "V4" || protocol == "ipv4";
}

bool isValidProtocol(const std::string& protocol) {
  return isIpv4Protocol(protocol) || protocol == "v6" || protocol == "V6" ||
      protocol == "ipv6";
}

using QueueMessage = std::variant<
    std::shared_ptr<const BgpUpdate2>,
    nettools::bgplib::UpdateDescriptor,
    BgpEndOfRib,
    nettools::bgplib::BgpRouteRefresh,
    nettools::bgplib::BgpNotification>;

std::optional<QueueMessage> popFromQueue(
    const E2ETestFixture::PeerQueues& queues) {
  try {
    XLOGF(
        INFO,
        "Test READING from QUEUE POINTER {}",
        (void*)queues.boundedAdjRibOutQ.get());
    return boundedBlockingPop(*queues.boundedAdjRibOutQ, "boundedAdjRibOutQ");
  } catch (const BoundedWaitTimeout&) {
    /*
     * Wait timed out — propagate so the test fails fast with the
     * descriptive message from boundedBlockingPop instead of silently
     * returning nullopt (which would let the test hang elsewhere).
     */
    throw;
  } catch (const std::exception&) {
    /* Queue closed / cancelled — callers handle nullopt gracefully. */
    return std::nullopt;
  }
}

bool isUpdateMessage(const QueueMessage& msg) {
  return std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(msg) ||
      std::holds_alternative<nettools::bgplib::UpdateDescriptor>(msg);
}

bool isEoRMessage(const QueueMessage& msg) {
  if (std::holds_alternative<BgpEndOfRib>(msg)) {
    return true;
  }
  if (auto* updatePtr = std::get_if<std::shared_ptr<const BgpUpdate2>>(&msg)) {
    return isEoRUpdate(**updatePtr);
  }
  return false;
}

/*
 * Helper to convert IPPrefix thrift struct to folly::CIDRNetwork for display
 */
folly::CIDRNetwork ipPrefixToCIDR(
    const facebook::network::thrift::IPPrefix& prefix) {
  return facebook::network::toCIDRNetwork(prefix);
}

/*
 * Log BgpUpdate2 contents for debugging
 */
void logBgpUpdate(const BgpUpdate2& update, const std::string& context = "") {
  std::string logMsg = context.empty() ? "BgpUpdate2:" : context + ":";

  /* Log V4 announced prefixes */
  if (!update.v4Announced2()->empty()) {
    logMsg += fmt::format(
        "\n  V4 Announced: {} prefixes", update.v4Announced2()->size());
    for (const auto& prefix : *update.v4Announced2()) {
      auto cidr = ipPrefixToCIDR(*prefix.prefix());
      logMsg +=
          fmt::format("\n    {}", folly::IPAddress::networkToString(cidr));
      if (prefix.pathId()) {
        logMsg += fmt::format(" (pathId={})", *prefix.pathId());
      }
    }
  }

  /* Log MP announced prefixes (v6 or v4) */
  if (!update.mpAnnounced()->prefixes()->empty()) {
    logMsg += fmt::format(
        "\n  MP Announced: {} prefixes (AFI={}, SAFI={})",
        update.mpAnnounced()->prefixes()->size(),
        static_cast<int>(*update.mpAnnounced()->afi()),
        static_cast<int>(*update.mpAnnounced()->safi()));
    for (const auto& prefix : *update.mpAnnounced()->prefixes()) {
      auto cidr = ipPrefixToCIDR(*prefix.prefix());
      logMsg +=
          fmt::format("\n    {}", folly::IPAddress::networkToString(cidr));
      if (prefix.pathId()) {
        logMsg += fmt::format(" (pathId={})", *prefix.pathId());
      }
    }
    if (!update.mpAnnounced()->nexthop()->addr()->empty()) {
      try {
        auto nexthopBytes = update.mpAnnounced()->nexthop()->addr().value();
        auto nexthopIp = folly::IPAddress::fromBinary(
            folly::ByteRange(
                reinterpret_cast<const unsigned char*>(nexthopBytes.data()),
                nexthopBytes.size()));
        logMsg += fmt::format("\n    MP Nexthop: {}", nexthopIp.str());
      } catch (const std::exception& e) {
        logMsg += fmt::format("\n    MP Nexthop: <parse error: {}>", e.what());
      }
    }
  }

  /* Log V4 withdrawn prefixes */
  if (!update.v4Withdrawn2()->empty()) {
    logMsg += fmt::format(
        "\n  V4 Withdrawn: {} prefixes", update.v4Withdrawn2()->size());
    for (const auto& prefix : *update.v4Withdrawn2()) {
      auto cidr = ipPrefixToCIDR(*prefix.prefix());
      logMsg +=
          fmt::format("\n    {}", folly::IPAddress::networkToString(cidr));
      if (prefix.pathId()) {
        logMsg += fmt::format(" (pathId={})", *prefix.pathId());
      }
    }
  }

  /* Log MP withdrawn prefixes (v6) */
  if (!update.mpWithdrawn()->prefixes()->empty()) {
    logMsg += fmt::format(
        "\n  MP Withdrawn: {} prefixes",
        update.mpWithdrawn()->prefixes()->size());
    for (const auto& prefix : *update.mpWithdrawn()->prefixes()) {
      auto cidr = ipPrefixToCIDR(*prefix.prefix());
      logMsg +=
          fmt::format("\n    {}", folly::IPAddress::networkToString(cidr));
      if (prefix.pathId()) {
        logMsg += fmt::format(" (pathId={})", *prefix.pathId());
      }
    }
  }

  /* Log path attributes */
  logMsg += "\n  Attributes:";

  /* Nexthop */
  if (!update.attrs()->nexthop()->empty()) {
    logMsg +=
        fmt::format("\n    Nexthop: {}", update.attrs()->nexthop().value());
  }

  /* AS Path */
  if (!update.attrs()->asPath()->empty()) {
    logMsg += "\n    AS Path:";
    for (const auto& segment : update.attrs()->asPath().value()) {
      if (!segment.asSequence()->empty()) {
        logMsg += " [";
        for (size_t i = 0; i < segment.asSequence()->size(); ++i) {
          if (i > 0) {
            logMsg += " ";
          }
          logMsg += fmt::format("{}", segment.asSequence().value()[i]);
        }
        logMsg += "]";
      }
    }
  } else {
    logMsg += "\n    AS Path: <empty>";
  }

  /* Local Pref */
  if (update.attrs()->localPref()) {
    logMsg += fmt::format(
        "\n    Local Pref: {}", update.attrs()->localPref().value());
  }

  /* MED */
  if (apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          update.attrs()->med())) {
    logMsg += fmt::format("\n    MED: {}", update.attrs()->med().value());
  }

  /* Communities */
  if (!update.attrs()->communities()->empty()) {
    logMsg += "\n    Communities:";
    for (const auto& comm : update.attrs()->communities().value()) {
      logMsg += fmt::format(" {}:{}", comm.asn().value(), comm.value().value());
    }
  }

  /* Origin */
  if (apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          update.attrs()->origin())) {
    logMsg += fmt::format(
        "\n    Origin: {}", static_cast<int>(update.attrs()->origin().value()));
  }

  XLOGF(INFO, "{}", logMsg);
}

} // namespace

/*
 * Helper class to capture BgpUpdate2 from BgpMessageParser2 callbacks
 */
class CaptureUpdateCallback
    : public nettools::bgplib::BgpMessageParser2::BgpMessageParserCallbacks {
 private:
  std::shared_ptr<const BgpUpdate2> capturedUpdate_;

 public:
  void rcvdBgpUpdate(BgpUpdate2 update) override {
    XLOG(INFO, "CaptureUpdateCallback: rcvdBgpUpdate called");
    capturedUpdate_ = std::make_shared<const BgpUpdate2>(std::move(update));
  }

  std::shared_ptr<const BgpUpdate2> getCapturedUpdate() const {
    return capturedUpdate_;
  }

  /* Log when other callbacks are called */
  void rcvdBgpOpenMsg(nettools::bgplib::BgpOpenMsg) override {
    XLOG(WARN, "CaptureUpdateCallback: rcvdBgpOpenMsg called (unexpected)");
  }
  void rcvdBgpNotification(nettools::bgplib::BgpNotification) override {
    XLOG(
        WARN, "CaptureUpdateCallback: rcvdBgpNotification called (unexpected)");
  }
  void rcvdBgpKeepAlive() override {
    XLOG(WARN, "CaptureUpdateCallback: rcvdBgpKeepAlive called (unexpected)");
  }
  void rcvdBgpEndOfRib(BgpEndOfRib) override {
    XLOG(WARN, "CaptureUpdateCallback: rcvdBgpEndOfRib called (unexpected)");
  }
  void rcvdBgpRouteRefresh(nettools::bgplib::BgpRouteRefresh) override {
    XLOG(
        WARN, "CaptureUpdateCallback: rcvdBgpRouteRefresh called (unexpected)");
  }
  void handleBgpException(const nettools::bgplib::BgpException& e) override {
    XLOGF(ERR, "CaptureUpdateCallback: handleBgpException: {}", e.what());
  }
  void handleBgpFsmException(
      const nettools::bgplib::BgpFsmException& e) override {
    XLOGF(ERR, "CaptureUpdateCallback: handleBgpFsmException: {}", e.what());
  }
  void handleBgpHeaderException(
      const nettools::bgplib::BgpHeaderException& e) override {
    XLOGF(ERR, "CaptureUpdateCallback: handleBgpHeaderException: {}", e.what());
  }
  void handleBgpOpenMsgException(
      const nettools::bgplib::BgpOpenMsgException& e) override {
    XLOGF(
        ERR, "CaptureUpdateCallback: handleBgpOpenMsgException: {}", e.what());
  }
  void handleBgpUpdateMsgException(
      const nettools::bgplib::BgpUpdateMsgException& e) override {
    XLOGF(
        ERR,
        "CaptureUpdateCallback: handleBgpUpdateMsgException: {}",
        e.what());
  }
  void handleBgpRouteRefreshMsgException(
      const nettools::bgplib::BgpRouteRefreshMsgException& e) override {
    XLOGF(
        ERR,
        "CaptureUpdateCallback: handleBgpRouteRefreshMsgException: {}",
        e.what());
  }
};

/*
 * Parse IOBuf containing serialized BGP UPDATE message into BgpUpdate2
 * This simulates what the I/O thread does on the ingress path
 */
std::optional<std::shared_ptr<const BgpUpdate2>> parseBgpUpdateFromIOBuf(
    std::unique_ptr<folly::IOBuf> iobuf) {
  if (!iobuf || iobuf->empty()) {
    XLOG(ERR, "parseBgpUpdateFromIOBuf: null or empty IOBuf");
    return std::nullopt;
  }

  XLOGF(
      DBG2,
      "parseBgpUpdateFromIOBuf: Parsing IOBuf of length {}",
      iobuf->computeChainDataLength());

  try {
    CaptureUpdateCallback callback;

    /* Create capabilities for parsing - must match what peers negotiated.
     * IMPORTANT: Must match capabilities used during serialization!
     * All test peers negotiate these capabilities (see createDisplayInfo). */
    nettools::bgplib::BgpCapabilities caps;
    caps.as4byte() = true; /* Peers negotiate 4-byte ASN capability */
    caps.mpExtV4Unicast() =
        true; /* Peers negotiate multiprotocol IPv4 unicast */
    caps.mpExtV6Unicast() =
        true; /* Peers negotiate multiprotocol IPv6 unicast */

    /* Use the IOBuf overload of parseBgpMessage with capabilities */
    nettools::bgplib::BgpMessageParser2::parseBgpMessage(
        &callback, *iobuf, std::move(caps));

    auto result = callback.getCapturedUpdate();
    if (!result) {
      XLOG(
          ERR,
          "parseBgpUpdateFromIOBuf: Parser did not capture BgpUpdate2 (might be a different message type or parse error)");
      return std::nullopt;
    }

    XLOG(DBG2, "parseBgpUpdateFromIOBuf: Successfully parsed BgpUpdate2");
    return result;
  } catch (const std::exception& e) {
    XLOGF(
        ERR,
        "parseBgpUpdateFromIOBuf: exception parsing BGP message: {}",
        e.what());
    return std::nullopt;
  }
}

/*
 * Deserialize UpdateDescriptor into BgpUpdate2
 * This simulates what happens in the I/O thread:
 * 1. Clone IOBuf from serializedGroupPDU
 * 2. Mutate nexthop at specified offsets
 * 3. Parse the final IOBuf into BgpUpdate2
 */
std::optional<std::shared_ptr<const BgpUpdate2>> deserializeUpdateDescriptor(
    const nettools::bgplib::UpdateDescriptor& desc) {
  XLOGF(
      DBG2,
      "deserializeUpdateDescriptor: v4Nexthop={}, v6Nexthop={}, nexthopOffsets.size={}",
      desc.v4Nexthop.str(),
      desc.v6Nexthop.str(),
      desc.nexthopOffsets.size());

  if (!desc.serializedGroupPDU) {
    XLOG(ERR, "deserializeUpdateDescriptor: serializedGroupPDU is null");
    return std::nullopt;
  }

  XLOGF(
      DBG2,
      "deserializeUpdateDescriptor: serializedGroupPDU length={}",
      desc.serializedGroupPDU->computeChainDataLength());

  /* Dump hex before nexthop mutation */
  XLOGF(
      INFO,
      "deserializeUpdateDescriptor: serializedGroupPDU hex BEFORE mutation:\n{}",
      folly::hexDump(
          desc.serializedGroupPDU->data(),
          desc.serializedGroupPDU->computeChainDataLength()));

  /* Use BgpSerializer to clone IOBuf and mutate nexthop (simulates I/O thread).
   * IMPORTANT: Capabilities must match what peers negotiated during session
   * establishment! All test peers negotiate these capabilities (see
   * createDisplayInfo). */
  nettools::bgplib::BgpCapabilities defaultCaps;
  defaultCaps.as4byte() = true; /* Peers negotiate 4-byte ASN capability */
  defaultCaps.mpExtV4Unicast() =
      true; /* Peers negotiate multiprotocol IPv4 unicast */
  defaultCaps.mpExtV6Unicast() =
      true; /* Peers negotiate multiprotocol IPv6 unicast */
  nettools::bgplib::BgpSerializer serializer(defaultCaps);

  /* Call operator() to get the final IOBuf with mutated nexthop */
  auto finalIOBuf = serializer(desc);

  if (!finalIOBuf || finalIOBuf->empty()) {
    XLOG(ERR, "deserializeUpdateDescriptor: serializer returned empty IOBuf");
    return std::nullopt;
  }

  XLOGF(
      DBG2,
      "deserializeUpdateDescriptor: finalIOBuf length={} (after nexthop mutation)",
      finalIOBuf->computeChainDataLength());

  /* Dump hex after nexthop mutation */
  XLOGF(
      INFO,
      "deserializeUpdateDescriptor: finalIOBuf hex AFTER mutation:\n{}",
      folly::hexDump(finalIOBuf->data(), finalIOBuf->computeChainDataLength()));

  /* Parse the serialized BGP UPDATE back to BgpUpdate2 */
  return parseBgpUpdateFromIOBuf(std::move(finalIOBuf));
}

/* Helper function to attempt reading update from queue - can set breakpoint
 * here.
 *
 * maxDrainAttempts caps how many pops we issue while skipping non-UPDATE
 * messages (e.g. multiple EoRs in a row). Default 0 means "no early-exit
 * check, just keep popping" — safe now that popFromQueue is bounded via
 * boundedBlockingPop (kDefaultPopTimeout), so a permanently empty queue
 * surfaces as a BoundedWaitTimeout instead of a real hang. Bounded
 * callers (e.g. waitForOutboundUpdate) pass a small positive limit to
 * bail early when the queue runs dry between checks.
 */
std::optional<std::shared_ptr<const BgpUpdate2>> tryReadUpdateFromQueue(
    const E2ETestFixture::PeerQueues& queues,
    int maxDrainAttempts = 0) {
  std::optional<std::shared_ptr<const BgpUpdate2>> result;
  int attempts = 0;

  /* Keep reading messages until we find a non-EoR UPDATE */
  while (!result.has_value()) {
    if (maxDrainAttempts > 0) {
      bool queueEmpty = queues.boundedAdjRibOutQ->empty();
      if (queueEmpty) {
        /* Queue drained — bail rather than block on an empty pop */
        break;
      }
      if (++attempts > maxDrainAttempts) {
        break;
      }
    }
    auto msg = popFromQueue(queues);
    if (!msg.has_value()) {
      break;
    }

    if (isEoRMessage(*msg)) {
      continue;
    }

    if (isUpdateMessage(*msg)) {
      /* Handle both BgpUpdate2 and UpdateDescriptor */
      if (auto* updatePtr =
              std::get_if<std::shared_ptr<const BgpUpdate2>>(&(*msg))) {
        /* Non-serialized path: BgpUpdate2 directly */
        XLOG(
            INFO,
            "tryReadUpdateFromQueue: Got BgpUpdate2 directly (non-serialized)");
        result = *updatePtr;
        break;
      } else if (
          auto* descPtr =
              std::get_if<nettools::bgplib::UpdateDescriptor>(&(*msg))) {
        /* Serialized path: UpdateDescriptor → deserialize → BgpUpdate2 */
        XLOG(
            INFO,
            "tryReadUpdateFromQueue: Got UpdateDescriptor (serialized), deserializing...");
        result = deserializeUpdateDescriptor(*descPtr);
        if (!result.has_value()) {
          XLOG(ERR, "tryReadUpdateFromQueue: Deserialization FAILED!");
        } else {
          XLOG(INFO, "tryReadUpdateFromQueue: Deserialization succeeded");
        }
        break;
      }
    }
  }

  return result;
}

/*
 * Helper to wait for and consume EoR from peer's outbound queue
 * Returns true if EoR was found, false otherwise
 */
bool tryConsumeEoRFromQueue(const E2ETestFixture::PeerQueues& queues) {
  while (true) {
    auto msg = popFromQueue(queues);
    if (!msg.has_value()) {
      return false;
    }

    if (isEoRMessage(*msg)) {
      return true;
    }

    /*
     * Skip non-EoR messages
     * This shouldn't happen in normal flow but handle it gracefully
     */
  }
}

std::optional<std::shared_ptr<const BgpUpdate2>>
E2ETestFixture::readOutboundUpdateToPeer(const BgpPeerId& peerId) {
  /* Check if peer is blocked - do not read if blocked */
  if (isPeerBlocked(peerId.peerAddr)) {
    XLOGF(
        DBG2,
        "readOutboundUpdateToPeer: peer {} is blocked - skipping read",
        peerId.peerAddr.str());
    return std::nullopt;
  }

  auto queues = getPeerQueues(peerId);
  if (!queues) {
    return std::nullopt;
  }

  XLOGF(
      INFO,
      "Test reading from peer {} bounded queue {}",
      peerId.peerAddr.str(),
      (void*)queues->boundedAdjRibOutQ.get());

  /*
   * popFromQueue() is now bounded (boundedBlockingPop, kDefaultPopTimeout).
   * If no UPDATE arrives within that budget, the test fails with a
   * descriptive BoundedWaitTimeout instead of hanging until tpx kills it.
   */
  return tryReadUpdateFromQueue(*queues);
}

std::optional<nettools::bgplib::BgpRouteRefresh>
E2ETestFixture::readOutboundRouteRefreshFromPeer(const BgpPeerId& peerId) {
  const auto it = peerQueues_.find(peerId);
  if (it == peerQueues_.end()) {
    return std::nullopt;
  }

  const auto msg = popFromQueue(it->second);
  if (!msg.has_value()) {
    return std::nullopt;
  }

  if (const auto* rrPtr =
          std::get_if<nettools::bgplib::BgpRouteRefresh>(&(*msg))) {
    return *rrPtr;
  }
  return std::nullopt;
}

std::optional<std::shared_ptr<const BgpUpdate2>>
E2ETestFixture::waitForOutboundUpdate(const BgpPeerId& peerId, int maxRetries) {
  /*
   * Wait for an UPDATE to appear in peer's outbound queue.
   * Uses check-sleep-retry pattern to allow async processing to complete.
   */
  if (isPeerBlocked(peerId.peerAddr)) {
    XLOGF(
        DBG2,
        "waitForOutboundUpdate: peer {} is blocked - skipping",
        peerId.peerAddr.str());
    return std::nullopt;
  }

  auto queues = getPeerQueues(peerId);
  if (!queues) {
    XLOGF(
        WARN,
        "waitForOutboundUpdate: peer {} not found",
        peerId.peerAddr.str());
    return std::nullopt;
  }

  XLOGF(
      DBG2,
      "waitForOutboundUpdate: waiting for update from peer {} (maxRetries={})",
      peerId.peerAddr.str(),
      maxRetries);

  /*
   * Check-sleep-retry pattern:
   * 1. Check if queue has messages
   * 2. If not, sleep briefly to allow async processing
   * 3. Repeat up to maxRetries
   */
  for (int retry = 0; retry < maxRetries; ++retry) {
    bool queueEmpty = queues->boundedAdjRibOutQ->empty();

    if (!queueEmpty) {
      XLOGF(
          DBG2,
          "waitForOutboundUpdate: queue not empty after {} retries, reading",
          retry);
      /*
       * Cap inner drain attempts at maxRetries so a queue that drains
       * to empty between empty()-check and pop() doesn't trap us in
       * the inner blockingWait of tryReadUpdateFromQueue.
       */
      return tryReadUpdateFromQueue(*queues, /*maxDrainAttempts=*/maxRetries);
    }

    /* Sleep briefly to allow async processing to complete */
    // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  XLOGF(
      WARN,
      "waitForOutboundUpdate: queue still empty after {} retries",
      maxRetries);
  return std::nullopt;
}

bool E2ETestFixture::waitForEoR(const BgpPeerId& peerId) {
  auto queues = getPeerQueues(peerId);
  if (!queues) {
    return false;
  }

  bool result = false;

  WITH_RETRIES_N(10, {
    result = tryConsumeEoRFromQueue(*queues);
    EXPECT_EVENTUALLY_TRUE(result);
  });

  return result;
}

/*
 * ==================== QUEUE BLOCKING HELPER IMPLEMENTATIONS
 * ====================
 */

bool E2ETestFixture::isPeerQueueBlocked(const BgpPeerId& peerId) {
  auto queues = getPeerQueues(peerId);
  if (!queues) {
    return false;
  }

  return queues->boundedAdjRibOutQ->isBlocked();
}

bool E2ETestFixture::waitForPeerQueueBlocked(
    const BgpPeerId& peerId,
    int maxRetries) {
  bool blocked = false;
  WITH_RETRIES_N(maxRetries, {
    blocked = isPeerQueueBlocked(peerId);
    EXPECT_EVENTUALLY_TRUE(blocked);
  });
  return blocked;
}

size_t E2ETestFixture::getPeerQueueSize(const BgpPeerId& peerId) {
  auto queues = getPeerQueues(peerId);
  if (!queues) {
    return 0;
  }

  return queues->boundedAdjRibOutQ->size();
}

size_t E2ETestFixture::drainPeerQueueCompletely(
    const BgpPeerId& peerId,
    int maxRetries,
    int maxMessages) {
  /*
   * Pump-only drain (no sleep between empty retries) that discards the drained
   * messages and returns the count. Shares the drain loop with
   * drainAllOutboundMessagesToOrderedVec; callers that need the ordered
   * messages should use that directly.
   */
  return drainAllOutboundMessagesToOrderedVec(
             peerId, maxRetries, maxMessages, /*sleepMsBetweenRetries=*/0)
      .size();
}

void E2ETestFixture::recordDrainedRoutes(
    const BgpPeerId& peerId,
    int idleRetries,
    int maxMessages) {
  auto messages =
      drainAllOutboundMessagesToOrderedVec(peerId, idleRetries, maxMessages);
  auto& routes = drainedRoutesByPeer_[peerId];
  for (const auto& msg : messages) {
    if (msg.isEoR || !msg.update) {
      continue;
    }
    // All announced prefixes in one UPDATE share its path attributes.
    const auto& attrs = *msg.update->attrs();
    for (const auto& prefix : getAnnouncedPrefixes(*msg.update)) {
      routes[prefix] = attrs;
    }
    for (const auto& prefix : getWithdrawnPrefixes(*msg.update)) {
      routes.erase(prefix);
    }
  }
}

const E2ETestFixture::ReceivedRoutes& E2ETestFixture::receivedRoutes(
    const BgpPeerId& peerId) const {
  static const ReceivedRoutes kEmpty;
  auto it = drainedRoutesByPeer_.find(peerId);
  return it == drainedRoutesByPeer_.end() ? kEmpty : it->second;
}

void E2ETestFixture::drainAndClassifyMessages(
    const BgpPeerId& peerId,
    size_t& updateCount,
    size_t& eorCount,
    int maxRetries,
    int maxMessages) {
  updateCount = 0;
  eorCount = 0;

  /*
   * Drain in order via drainAllOutboundMessagesToOrderedVec (the single source
   * of truth for the empty/retry/pump drain loop), then tally updates vs EoRs.
   * Pump-only (no sleep between retries) to match the original behavior;
   * maxRetries maps to the idle-retry budget and maxMessages caps draining.
   */
  const auto messages = drainAllOutboundMessagesToOrderedVec(
      peerId, maxRetries, maxMessages, /*sleepMsBetweenRetries=*/0);
  for (const auto& msg : messages) {
    if (msg.isEoR) {
      eorCount++;
    } else if (msg.update) {
      updateCount++;
    }
  }

  XLOGF(
      INFO,
      "drainAndClassifyMessages: peer {} — {} updates, {} EoRs, {} total",
      peerId.peerAddr.str(),
      updateCount,
      eorCount,
      updateCount + eorCount);
}

E2ETestFixture::PrefixOccurrenceCounts
E2ETestFixture::countPrefixOccurrencesAndDrain(
    const BgpPeerId& peerId,
    const folly::CIDRNetwork& prefix,
    bool isV4,
    int maxRetries,
    int maxMessages) {
  PrefixOccurrenceCounts counts;
  auto queues = getPeerQueues(peerId);
  if (!queues) {
    XLOGF(
        WARN,
        "countPrefixOccurrencesAndDrain: peer {} not found",
        peerId.peerAddr.str());
    return counts;
  }

  XLOGF(
      INFO,
      "countPrefixOccurrencesAndDrain: draining peer {} for prefix {} "
      "(maxRetries={}, maxMessages={})",
      peerId.peerAddr.str(),
      folly::IPAddress::networkToString(prefix),
      maxRetries,
      maxMessages);

  int emptyRetries = 0;
  while (true) {
    if (maxMessages > 0 &&
        static_cast<int>(counts.totalMessages) >= maxMessages) {
      XLOGF(
          INFO,
          "countPrefixOccurrencesAndDrain: hit max messages limit ({}), "
          "stopping",
          maxMessages);
      break;
    }

    bool queueEmpty = queues->boundedAdjRibOutQ->empty();
    if (queueEmpty) {
      if (emptyRetries >= maxRetries) {
        break;
      }
      emptyRetries++;
      /* Pump event loops so any in-flight async work can land. */
      if (rib_) {
        rib_->getEventBase().runInEventBaseThreadAndWait([]() {});
      }
      if (peerManager_) {
        peerManager_->getEventBase().runInEventBaseThreadAndWait([]() {});
      }
      continue;
    }
    emptyRetries = 0;

    auto msg = popFromQueue(*queues);
    if (!msg.has_value()) {
      break;
    }
    counts.totalMessages++;

    /* Classify the message. Only UPDATE messages can carry the prefix;
     * EoR / RouteRefresh / Notification are counted in totalMessages
     * but cannot match. UpdateDescriptor is deserialized first. */
    std::shared_ptr<const nettools::bgplib::BgpUpdate2> updatePtr;
    if (auto* p =
            std::get_if<std::shared_ptr<const nettools::bgplib::BgpUpdate2>>(
                &(*msg))) {
      updatePtr = *p;
    } else if (
        auto* descPtr =
            std::get_if<nettools::bgplib::UpdateDescriptor>(&(*msg))) {
      auto deserialized = deserializeUpdateDescriptor(*descPtr);
      if (deserialized.has_value()) {
        updatePtr = *deserialized;
      }
    }
    if (!updatePtr) {
      continue;
    }
    /*
     * Messages are classified in queue order, so the last assignment to
     * lastEvent is the wire-final event for this prefix. A single UPDATE
     * carrying both an announce and a withdraw of the SAME prefix would be
     * malformed; if it ever occurred the withdraw would win here.
     */
    if (findPrefixInAnnouncements(*updatePtr, isV4, prefix, /*addPathId=*/0)) {
      counts.announceCount++;
      counts.lastEvent = PrefixEvent::Announce;
    }
    if (findPrefixInWithdrawals(*updatePtr, isV4, prefix, /*addPathId=*/0)) {
      counts.withdrawCount++;
      counts.lastEvent = PrefixEvent::Withdraw;
    }
  }

  XLOGF(
      INFO,
      "countPrefixOccurrencesAndDrain: peer {} prefix {} -> "
      "announceCount={} withdrawCount={} totalMessages={}",
      peerId.peerAddr.str(),
      folly::IPAddress::networkToString(prefix),
      counts.announceCount,
      counts.withdrawCount,
      counts.totalMessages);
  return counts;
}

std::vector<std::shared_ptr<const nettools::bgplib::BgpUpdate2>>
E2ETestFixture::drainPeerQueueAndCollectUpdates(
    const BgpPeerId& peerId,
    int maxRetries,
    int maxMessages) {
  std::vector<std::shared_ptr<const nettools::bgplib::BgpUpdate2>> updates;
  auto queues = getPeerQueues(peerId);
  if (!queues) {
    XLOGF(
        WARN,
        "drainPeerQueueAndCollectUpdates: peer {} not found",
        peerId.peerAddr.str());
    return updates;
  }

  XLOGF(
      INFO,
      "drainPeerQueueAndCollectUpdates: draining peer {} "
      "(maxRetries={}, maxMessages={})",
      peerId.peerAddr.str(),
      maxRetries,
      maxMessages);

  size_t totalDrained = 0;
  int emptyRetries = 0;
  while (true) {
    if (maxMessages > 0 && static_cast<int>(totalDrained) >= maxMessages) {
      break;
    }
    bool queueEmpty = queues->boundedAdjRibOutQ->empty();
    if (queueEmpty) {
      if (emptyRetries >= maxRetries) {
        break;
      }
      emptyRetries++;
      if (rib_) {
        rib_->getEventBase().runInEventBaseThreadAndWait([]() {});
      }
      if (peerManager_) {
        peerManager_->getEventBase().runInEventBaseThreadAndWait([]() {});
      }
      continue;
    }
    emptyRetries = 0;

    auto msg = popFromQueue(*queues);
    if (!msg.has_value()) {
      break;
    }
    totalDrained++;

    /* Surface only UPDATE messages; EoR / RouteRefresh / Notification are
     * popped to advance the queue but not returned. UpdateDescriptor is
     * deserialized to BgpUpdate2 first. */
    if (auto* p =
            std::get_if<std::shared_ptr<const nettools::bgplib::BgpUpdate2>>(
                &(*msg))) {
      updates.push_back(*p);
    } else if (
        auto* descPtr =
            std::get_if<nettools::bgplib::UpdateDescriptor>(&(*msg))) {
      auto deserialized = deserializeUpdateDescriptor(*descPtr);
      if (deserialized.has_value()) {
        updates.push_back(*deserialized);
      }
    }
  }

  XLOGF(
      INFO,
      "drainPeerQueueAndCollectUpdates: peer {} -> collected {} UPDATE "
      "messages from {} total drained",
      peerId.peerAddr.str(),
      updates.size(),
      totalDrained);
  return updates;
}

/*
 * blockPeer - Block a peer from having its queue read
 *
 * Sets a flag that prevents readOutboundUpdateToPeer() from reading
 * messages from this peer's queue. The queue will fill up naturally
 * and cause backpressure.
 */
void E2ETestFixture::blockPeer(const folly::IPAddress& peerAddr) {
  blockedPeers_.insert(peerAddr);

  XLOGF(
      INFO,
      "blockPeer: blocked peer {} - queue reads will be prevented",
      peerAddr.str());
}

/*
 * unblockPeer - Unblock a peer and drain its queue
 *
 * Clears the block flag and optionally drains all pending messages
 * from the peer's bounded queue.
 */
bool E2ETestFixture::unblockPeer(
    const folly::IPAddress& peerAddr,
    int maxRetries,
    int maxMessages) {
  auto it = blockedPeers_.find(peerAddr);
  bool wasBlocked = (it != blockedPeers_.end());

  if (wasBlocked) {
    blockedPeers_.erase(it);
    XLOGF(INFO, "unblockPeer: unblocked peer {}", peerAddr.str());
  } else {
    XLOGF(WARN, "unblockPeer: peer {} was not blocked", peerAddr.str());
  }

  if (maxRetries == 0) {
    // no drain
    return wasBlocked;
  }
  /* Drain the queue to resume normal operation */
  BgpPeerId peerId{peerAddr, peerAddr.asV4().toLongHBO()};
  size_t drained = drainPeerQueueCompletely(peerId, maxRetries, maxMessages);

  XLOGF(
      INFO,
      "unblockPeer: drained {} messages from peer {}",
      drained,
      peerAddr.str());

  return wasBlocked;
}

/*
 * isPeerBlocked - Check if a peer is currently blocked
 */
bool E2ETestFixture::isPeerBlocked(const folly::IPAddress& peerAddr) const {
  return blockedPeers_.find(peerAddr) != blockedPeers_.end();
}

/*
 * isPeerEgressQueueEmpty - Check if a peer's egress queue is empty
 * Used for out-delay testing to verify routes are deferred.
 */
bool E2ETestFixture::isPeerEgressQueueEmpty(const folly::IPAddress& peerAddr) {
  BgpPeerId peerId{peerAddr, peerAddr.asV4().toLongHBO()};
  return getPeerQueueSize(peerId) == 0;
}

/*
 * waitForPeerEgressQueueNonEmpty - Wait for a peer's egress queue to have at
 * least one message. Returns true if message arrives within retries, false
 * otherwise.
 */
bool E2ETestFixture::waitForPeerEgressQueueNonEmpty(
    const folly::IPAddress& peerAddr,
    int maxRetries) {
  BgpPeerId peerId{peerAddr, peerAddr.asV4().toLongHBO()};

  for (int i = 0; i < maxRetries; ++i) {
    if (getPeerQueueSize(peerId) > 0) {
      return true;
    }
    /* Allow event loops to process */
    // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return getPeerQueueSize(peerId) > 0;
}

// ==================== NEW HELPER API IMPLEMENTATIONS ====================

void E2ETestFixture::addRoute(
    const std::string& protocol,
    const std::string& prefix,
    uint8_t prefixLen,
    const folly::IPAddress& peer,
    const std::string& nexthop,
    const std::string& asPath,
    const std::string& community,
    uint32_t addPathId,
    uint32_t localPref,
    uint32_t med) {
  ASSERT_TRUE(isValidProtocol(protocol)) << "Invalid protocol: " << protocol;

  auto peerIdOpt = findPeerIdByAddress(peer);
  ASSERT_TRUE(peerIdOpt.has_value()) << "Peer " << peer.str() << " not found";

  const folly::CIDRNetwork cidr{folly::IPAddress(prefix), prefixLen};
  const auto asPathSeq = parseAsPath(asPath);
  const auto communities = parseCommunities(community);

  auto update = createBgpUpdateAnnouncement(
      isIpv4Protocol(protocol),
      cidr,
      nexthop,
      asPathSeq,
      communities,
      addPathId,
      localPref,
      med);

  auto queues = getPeerQueues(*peerIdOpt);
  ASSERT_TRUE(queues.has_value()) << "No queues for peer " << peer.str();
  folly::coro::blockingWait(queues->adjRibInQ->push(std::move(update)));
}

void E2ETestFixture::addRouteWithLbw(
    const std::string& protocol,
    const std::string& prefix,
    uint8_t prefixLen,
    const folly::IPAddress& peer,
    const std::string& nexthop,
    const std::string& asPath,
    float linkBandwidthGbps,
    uint32_t localPref) {
  ASSERT_TRUE(isValidProtocol(protocol)) << "Invalid protocol: " << protocol;

  auto peerIdOpt = findPeerIdByAddress(peer);
  ASSERT_TRUE(peerIdOpt.has_value()) << "Peer " << peer.str() << " not found";

  const folly::CIDRNetwork cidr{folly::IPAddress(prefix), prefixLen};
  const auto asPathSeq = parseAsPath(asPath);

  /*
   * Convert Gbps to Bytes/sec for the LBW extended community.
   * LBW value is stored as float Bytes/sec: Gbps * 10^9 / 8
   */
  float lbwBytesPerSec = linkBandwidthGbps * BpsPerGBps / 8.0f;

  auto update = createBgpUpdateAnnouncement(
      isIpv4Protocol(protocol),
      cidr,
      nexthop,
      asPathSeq,
      {},
      0,
      localPref,
      0,
      lbwBytesPerSec);

  auto queues = getPeerQueues(*peerIdOpt);
  ASSERT_TRUE(queues.has_value()) << "No queues for peer " << peer.str();
  folly::coro::blockingWait(queues->adjRibInQ->push(std::move(update)));
}

void E2ETestFixture::addRouteWithExtCommunities(
    const std::string& protocol,
    const std::string& prefix,
    uint8_t prefixLen,
    const folly::IPAddress& peer,
    const std::string& nexthop,
    const std::string& asPath,
    const std::vector<nettools::bgplib::BgpAttrExtCommunityC>& extCommunities,
    uint32_t localPref) {
  ASSERT_TRUE(isValidProtocol(protocol)) << "Invalid protocol: " << protocol;

  auto peerIdOpt = findPeerIdByAddress(peer);
  ASSERT_TRUE(peerIdOpt.has_value()) << "Peer " << peer.str() << " not found";

  const folly::CIDRNetwork cidr{folly::IPAddress(prefix), prefixLen};
  const auto asPathSeq = parseAsPath(asPath);

  auto update = createBgpUpdateAnnouncement(
      isIpv4Protocol(protocol), cidr, nexthop, asPathSeq, {}, 0, localPref, 0);

  for (const auto& ec : extCommunities) {
    nettools::bgplib::BgpAttrExtCommunity extComm;
    auto [firstWord, secondWord] = ec.getRawValueInWords();
    extComm.firstWord() = firstWord;
    extComm.secondWord() = secondWord;
    update->attrs()->extCommunities()->push_back(extComm);
  }

  auto queues = getPeerQueues(*peerIdOpt);
  ASSERT_TRUE(queues.has_value()) << "No queues for peer " << peer.str();
  folly::coro::blockingWait(queues->adjRibInQ->push(std::move(update)));
}

AdjRibEntry* E2ETestFixture::getAdjRibEntry(
    const BgpPeerId& peerId,
    bool ingress,
    const folly::CIDRNetwork& prefix) {
  auto it = peerManager_->adjRibs_.find(peerId);
  if (it == peerManager_->adjRibs_.end()) {
    return nullptr;
  }
  return it->second->getRibEntry(ingress, prefix);
}

std::shared_ptr<AdjRib> E2ETestFixture::getAdjRibByAddr(
    const folly::IPAddress& peerAddr) {
  if (!peerManager_) {
    return nullptr;
  }
  for (const auto& [peerId, adjRib] : peerManager_->adjRibs_) {
    if (peerId.peerAddr == peerAddr) {
      return adjRib;
    }
  }
  return nullptr;
}

// ========== STREAM SUBSCRIBER (MP-BGP MONITOR) HELPER IMPLEMENTATIONS =====

std::optional<size_t> E2ETestFixture::getSubscriberQueueSize(
    const std::string& subscriberName) {
  std::optional<size_t> size;
  if (!peerManager_) {
    return size;
  }
  peerManager_->getEventBase().runInEventBaseThreadAndWait([&]() {
    auto* subscriber = peerManager_->getStreamSubscriber(subscriberName);
    if (subscriber && subscriber->peerInputQ) {
      size = subscriber->peerInputQ->size();
    }
  });
  return size;
}

std::optional<bool> E2ETestFixture::isSubscriberQueueBlocked(
    const std::string& subscriberName) {
  std::optional<bool> blocked;
  if (!peerManager_) {
    return blocked;
  }
  peerManager_->getEventBase().runInEventBaseThreadAndWait([&]() {
    auto* subscriber = peerManager_->getStreamSubscriber(subscriberName);
    if (subscriber && subscriber->peerInputQ) {
      blocked = subscriber->peerInputQ->isBlocked();
    }
  });
  return blocked;
}

bool E2ETestFixture::waitForSubscriberQueueBlocked(
    const std::string& subscriberName,
    int maxRetries) {
  bool blocked = false;
  WITH_RETRIES_N(maxRetries, {
    auto state = isSubscriberQueueBlocked(subscriberName);
    blocked = state.has_value() && *state;
    EXPECT_EVENTUALLY_TRUE(blocked);
  });
  return blocked;
}

bool E2ETestFixture::waitForSubscriberQueueUnblocked(
    const std::string& subscriberName,
    int maxRetries) {
  bool unblocked = false;
  WITH_RETRIES_N(maxRetries, {
    /*
     * The has_value() test is necessary. A subscriber that bgpd tore down has
     * no queue. If this code read an absent queue as unblocked, each recovery
     * assertion in this suite would pass on the failure that it must catch.
     */
    auto state = isSubscriberQueueBlocked(subscriberName);
    unblocked = state.has_value() && !*state;
    EXPECT_EVENTUALLY_TRUE(unblocked);
  });
  return unblocked;
}

bool E2ETestFixture::waitForSubscriberQueueSizeAtMost(
    const std::string& subscriberName,
    size_t maxSize,
    int maxRetries) {
  bool drained = false;
  WITH_RETRIES_N(maxRetries, {
    auto size = getSubscriberQueueSize(subscriberName);
    drained = size.has_value() && *size <= maxSize;
    EXPECT_EVENTUALLY_TRUE(drained);
  });
  return drained;
}

std::optional<bool> E2ETestFixture::isSubscriberChangeListTimerScheduled(
    const std::string& subscriberName) {
  std::optional<bool> scheduled;
  if (!peerManager_) {
    return scheduled;
  }
  peerManager_->getEventBase().runInEventBaseThreadAndWait([&]() {
    auto* subscriber = peerManager_->getStreamSubscriber(subscriberName);
    if (!subscriber) {
      return;
    }
    auto adjRib = peerManager_->findAdjRib(subscriber->peerId);
    if (!adjRib) {
      return;
    }
    scheduled = adjRib->changeListConsumeTimer_ &&
        adjRib->changeListConsumeTimer_->isScheduled();
  });
  return scheduled;
}

bool E2ETestFixture::waitForSubscriberChangeListTimer(
    const std::string& subscriberName,
    bool scheduled,
    int maxRetries) {
  bool matched = false;
  WITH_RETRIES_N(maxRetries, {
    auto state = isSubscriberChangeListTimerScheduled(subscriberName);
    matched = state.has_value() && *state == scheduled;
    EXPECT_EVENTUALLY_TRUE(matched);
  });
  return matched;
}

bool E2ETestFixture::isSubscriberEstablished(
    const std::string& subscriberName) {
  if (!peerManager_) {
    return false;
  }
  bool established = false;
  peerManager_->getEventBase().runInEventBaseThreadAndWait([&]() {
    auto* subscriber = peerManager_->getStreamSubscriber(subscriberName);
    established = subscriber != nullptr &&
        subscriber->state ==
            neteng::fboss::bgp::thrift::TBgpPeerState::ESTABLISHED;
  });
  return established;
}

bool E2ETestFixture::waitForSubscriberEstablished(
    const std::string& subscriberName,
    bool established,
    int maxRetries) {
  bool matched = false;
  WITH_RETRIES_N(maxRetries, {
    matched = isSubscriberEstablished(subscriberName) == established;
    EXPECT_EVENTUALLY_TRUE(matched);
  });
  return matched;
}

bool E2ETestFixture::waitForChangeListConsumerReady(
    const folly::IPAddress& peerAddr,
    int maxRetries) {
  if (!peerManager_) {
    XLOG(
        ERR,
        "PeerManagerBase must be created before calling waitForChangeListConsumerReady");
    return false;
  }

  bool ready = false;
  WITH_RETRIES_N(maxRetries, {
    /*
     * Run on PeerManagerBase's event base: adjRibs_ and the change-list
     * consumer state are only accessed from that thread.
     */
    peerManager_->getEventBase().runInEventBaseThreadAndWait([&]() {
      auto adjRib = getAdjRibByAddr(peerAddr);
      if (!adjRib) {
        ready = false;
        return;
      }
      const auto& consumer = adjRib->getChangeListConsumer();
      ready = consumer && consumer->isReady();
    });
    EXPECT_EVENTUALLY_TRUE(ready);
  });
  return ready;
}

bool E2ETestFixture::waitForChangeListConsumerPended(
    const folly::IPAddress& peerAddr,
    const folly::CIDRNetwork& prefix,
    int maxRetries) {
  if (!peerManager_) {
    XLOG(
        ERR,
        "PeerManagerBase must be created before calling waitForChangeListConsumerPended");
    return false;
  }

  bool pended = false;
  WITH_RETRIES_N(maxRetries, {
    /*
     * Run on PeerManagerBase's event base: adjRibs_ and the change-list
     * consumer state are only accessed from that thread.
     */
    peerManager_->getEventBase().runInEventBaseThreadAndWait([&]() {
      auto adjRib = getAdjRibByAddr(peerAddr);
      if (!adjRib) {
        pended = false;
        return;
      }
      const auto& consumer = adjRib->getChangeListConsumer();
      auto* marker = consumer ? consumer->getMarker() : nullptr;
      pended = marker != nullptr && marker->getTypedObject().prefix == prefix;
    });
    EXPECT_EVENTUALLY_TRUE(pended);
  });
  return pended;
}

std::optional<bool> E2ETestFixture::checkRibOutNexthopSetByPolicy(
    const folly::IPAddress& peerAddr,
    const folly::CIDRNetwork& prefix) {
  std::optional<bool> result;
  if (!peerManager_) {
    return result;
  }
  peerManager_->getEventBase().runInEventBaseThreadAndWait([&]() {
    auto adjRib = getAdjRibByAddr(peerAddr);
    if (!adjRib) {
      return;
    }
    auto* entry = adjRib->getRibEntry(/*ingress=*/false, prefix);
    if (entry) {
      result = entry->isNexthopSetByPolicy();
    }
  });
  return result;
}

std::vector<E2ETestFixture::OutboundMessage>
E2ETestFixture::drainAllOutboundMessagesToOrderedVec(
    const BgpPeerId& peerId,
    int idleRetries,
    int maxMessages,
    int sleepMsBetweenRetries) {
  std::vector<OutboundMessage> result;
  auto queues = getPeerQueues(peerId);
  if (!queues) {
    XLOGF(
        WARN,
        "drainAllOutboundMessagesToOrderedVec: peer {} not found",
        peerId.peerAddr.str());
    return result;
  }

  XLOGF(
      INFO,
      "drainAllOutboundMessagesToOrderedVec: draining queue for peer {} (idleRetries={}, maxMessages={}, sleepMsBetweenRetries={})",
      peerId.peerAddr.str(),
      idleRetries,
      maxMessages,
      sleepMsBetweenRetries);

  while (true) {
    /* Stop once we've drained the message cap (0 == unlimited). */
    if (maxMessages > 0 && static_cast<int>(result.size()) >= maxMessages) {
      XLOGF(
          INFO,
          "drainAllOutboundMessagesToOrderedVec: hit max messages limit ({}), stopping",
          maxMessages);
      break;
    }
    /*
     * Wait for a message to become available. Popping unblocks the queue so
     * sendBgpUpdates / the change-list timer can emit the next message; allow
     * idleRetries quiet checks before concluding the queue is fully drained.
     * Between checks we pump the RIB and PeerManagerBase event bases. When
     * sleepMsBetweenRetries > 0 we also sleep that long to wait out
     * timer-driven emits (e.g. the MRAI timer); sleepMsBetweenRetries == 0
     * means pump-only (used by drainPeerQueueCompletely).
     */
    bool available = false;
    for (int i = 0; i < idleRetries; ++i) {
      bool empty = queues->boundedAdjRibOutQ->empty();
      if (!empty) {
        available = true;
        break;
      }
      if (rib_) {
        rib_->getEventBase().runInEventBaseThreadAndWait([]() {});
      }
      if (peerManager_) {
        peerManager_->getEventBase().runInEventBaseThreadAndWait([]() {});
      }
      if (sleepMsBetweenRetries > 0) {
        // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
        std::this_thread::sleep_for(
            std::chrono::milliseconds(sleepMsBetweenRetries));
      }
    }
    if (!available) {
      XLOGF(
          INFO,
          "drainAllOutboundMessagesToOrderedVec: queue empty after {} retries, stopping",
          idleRetries);
      break;
    }

    auto msg = popFromQueue(*queues);
    if (!msg.has_value()) {
      continue;
    }
    OutboundMessage out;
    if (isEoRMessage(*msg)) {
      out.isEoR = true;
      XLOGF(
          INFO,
          "drainAllOutboundMessagesToOrderedVec: peer {} got EoR",
          peerId.peerAddr.str());
    } else if (isUpdateMessage(*msg)) {
      if (auto* updatePtr =
              std::get_if<std::shared_ptr<const BgpUpdate2>>(&(*msg))) {
        out.update = *updatePtr;
      } else if (
          auto* descPtr =
              std::get_if<nettools::bgplib::UpdateDescriptor>(&(*msg))) {
        auto deser = deserializeUpdateDescriptor(*descPtr);
        if (deser.has_value()) {
          out.update = *deser;
        }
      }
    }
    result.push_back(std::move(out));
  }

  XLOGF(
      INFO,
      "drainAllOutboundMessagesToOrderedVec: drained {} messages from peer {}",
      result.size(),
      peerId.peerAddr.str());

  return result;
}

void E2ETestFixture::deleteRoute(
    const std::string& protocol,
    const std::string& prefix,
    uint8_t prefixLen,
    const folly::IPAddress& peer,
    uint32_t addPathId) {
  ASSERT_TRUE(isValidProtocol(protocol)) << "Invalid protocol: " << protocol;

  auto peerIdOpt = findPeerIdByAddress(peer);
  ASSERT_TRUE(peerIdOpt.has_value()) << "Peer " << peer.str() << " not found";

  const folly::CIDRNetwork cidr{folly::IPAddress(prefix), prefixLen};
  auto update =
      createBgpUpdateWithdrawal(isIpv4Protocol(protocol), cidr, addPathId);

  auto queues = getPeerQueues(*peerIdOpt);
  ASSERT_TRUE(queues.has_value()) << "No queues for peer " << peer.str();
  folly::coro::blockingWait(queues->adjRibInQ->push(std::move(update)));
}

bool E2ETestFixture::verifyRouteAdd(
    const std::string& protocol,
    const std::string& prefix,
    uint8_t prefixLen,
    const folly::IPAddress& peer,
    const std::string& expectedNexthop,
    const std::string& expectedAsPath,
    const std::string& expectedCommunity,
    uint32_t addPathId,
    int maxWaitRetries) {
  if (!isValidProtocol(protocol)) {
    return false;
  }

  auto peerIdOpt = findPeerIdByAddress(peer);
  if (!peerIdOpt.has_value()) {
    return false;
  }

  /*
   * If maxWaitRetries > 0, use event loop pumping to wait for the UPDATE.
   * This is needed when testing bestpath changes where the UPDATE might
   * not be generated immediately after addRoute().
   */
  std::optional<std::shared_ptr<const BgpUpdate2>> updateOpt;
  if (maxWaitRetries > 0) {
    updateOpt = waitForOutboundUpdate(*peerIdOpt, maxWaitRetries);
  } else {
    updateOpt = readOutboundUpdateToPeer(*peerIdOpt);
  }

  if (!updateOpt.has_value()) {
    XLOGF(
        ERR,
        "verifyRouteAdd: No update received from queue for peer {}",
        peer.str());
    return false;
  }

  logBgpUpdate(
      **updateOpt,
      fmt::format("verifyRouteAdd received update from peer {}", peer.str()));

  const folly::CIDRNetwork expectedCidr{folly::IPAddress(prefix), prefixLen};
  if (!findPrefixInAnnouncements(
          **updateOpt, isIpv4Protocol(protocol), expectedCidr, addPathId)) {
    return false;
  }

  return verifyRouteAttributes(
      **updateOpt, expectedNexthop, expectedAsPath, expectedCommunity);
}

bool E2ETestFixture::drainAndFindRouteAdvertised(
    const std::string& protocol,
    const std::string& prefix,
    uint8_t prefixLen,
    const folly::IPAddress& peer,
    const std::string& expectedNexthop,
    const std::string& expectedAsPath,
    const std::string& expectedCommunity,
    uint32_t addPathId,
    int maxFlushRetries) {
  if (!isValidProtocol(protocol)) {
    return false;
  }
  auto peerIdOpt = findPeerIdByAddress(peer);
  if (!peerIdOpt.has_value()) {
    return false;
  }
  if (isPeerBlocked(peerIdOpt->peerAddr)) {
    return false;
  }
  auto queues = getPeerQueues(*peerIdOpt);
  if (!queues) {
    return false;
  }

  const folly::CIDRNetwork expectedCidr{folly::IPAddress(prefix), prefixLen};

  XLOGF(
      INFO,
      "drainAndFindRouteAdvertised: searching peer {} for {}/{} nexthop={}",
      peer.str(),
      prefix,
      prefixLen,
      expectedNexthop);

  int emptyFlushes = 0;
  while (true) {
    bool queueEmpty = queues->boundedAdjRibOutQ->empty();
    if (queueEmpty) {
      if (emptyFlushes >= maxFlushRetries) {
        XLOGF(
            WARN,
            "drainAndFindRouteAdvertised: queue empty after {} evb flushes; target {}/{} not found",
            maxFlushRetries,
            prefix,
            prefixLen);
        return false;
      }
      emptyFlushes++;
      /*
       * Deterministic sync: flush rib_ and peerManager_ event bases so any
       * in-flight push completes before we re-check.
       */
      if (rib_) {
        rib_->getEventBase().runInEventBaseThreadAndWait([]() {});
      }
      if (peerManager_) {
        peerManager_->getEventBase().runInEventBaseThreadAndWait([]() {});
      }
      continue;
    }
    emptyFlushes = 0;

    /*
     * Reuse tryReadUpdateFromQueue (which already skips EoR messages and
     * handles both BgpUpdate2-direct and UpdateDescriptor-serialized
     * paths). It pops one matching UPDATE per call.
     *
     * Pass maxDrainAttempts so the inner skip-EoR loop checks queueEmpty
     * before each pop. Otherwise, if the queue's only message is an EoR,
     * the inner loop pops it, hits `continue`, then waits in popFromQueue
     * until its bounded pop times out — and our outer empty-check never
     * re-runs. Bound by maxFlushRetries so caller can tune.
     */
    auto updateOpt =
        tryReadUpdateFromQueue(*queues, /*maxDrainAttempts=*/maxFlushRetries);
    if (!updateOpt.has_value()) {
      /* Queue ran dry while we were popping — loop will re-check */
      continue;
    }

    logBgpUpdate(
        **updateOpt,
        fmt::format(
            "drainAndFindRouteAdvertised popped from peer {}", peer.str()));

    if (!findPrefixInAnnouncements(
            **updateOpt, isIpv4Protocol(protocol), expectedCidr, addPathId)) {
      /* Not our prefix — discard and keep draining */
      continue;
    }

    if (!verifyRouteAttributes(
            **updateOpt, expectedNexthop, expectedAsPath, expectedCommunity)) {
      /* Right prefix wrong attrs — discard and keep draining */
      continue;
    }

    XLOGF(
        INFO,
        "drainAndFindRouteAdvertised: found target {}/{} for peer {}",
        prefix,
        prefixLen,
        peer.str());
    return true;
  }
}

bool E2ETestFixture::verifyRouteWithdraw(
    const std::string& protocol,
    const std::string& prefix,
    uint8_t prefixLen,
    const folly::IPAddress& peer,
    uint32_t addPathId) {
  if (!isValidProtocol(protocol)) {
    return false;
  }

  auto peerIdOpt = findPeerIdByAddress(peer);
  if (!peerIdOpt.has_value()) {
    return false;
  }

  auto updateOpt = readOutboundUpdateToPeer(*peerIdOpt);
  if (!updateOpt.has_value()) {
    return false;
  }

  const folly::CIDRNetwork expectedCidr{folly::IPAddress(prefix), prefixLen};
  return findPrefixInWithdrawals(
      **updateOpt, isIpv4Protocol(protocol), expectedCidr, addPathId);
}

void E2ETestFixture::addRoutes(
    const std::string& protocol,
    const folly::IPAddress& peer,
    const std::vector<RouteSpec>& routes) {
  for (const auto& route : routes) {
    addRoute(
        protocol,
        route.prefix,
        route.prefixLen,
        peer,
        route.nexthop,
        route.asPath,
        route.community,
        route.addPathId,
        route.localPref,
        route.med);
  }
}

void E2ETestFixture::deleteRoutes(
    const std::string& protocol,
    const folly::IPAddress& peer,
    const std::vector<RouteSpec>& routes) {
  for (const auto& route : routes) {
    deleteRoute(protocol, route.prefix, route.prefixLen, peer, route.addPathId);
  }
}

namespace {

size_t checkRoutesInUpdate(
    const BgpUpdate2& update,
    bool isV4,
    const std::vector<E2ETestFixture::VerifySpec>& routes,
    std::vector<bool>& verified) {
  size_t newlyVerified = 0;

  for (size_t i = 0; i < routes.size(); ++i) {
    if (verified[i]) {
      continue;
    }

    const auto& route = routes[i];
    const folly::CIDRNetwork expectedCidr{
        folly::IPAddress(route.prefix), route.prefixLen};

    if (!findPrefixInAnnouncements(
            update, isV4, expectedCidr, route.addPathId)) {
      continue;
    }

    if (!verifyRouteAttributes(
            update,
            route.expectedNexthop,
            route.expectedAsPath,
            route.expectedCommunity)) {
      continue;
    }

    verified[i] = true;
    newlyVerified++;
  }

  return newlyVerified;
}

size_t checkWithdrawalsInUpdate(
    const BgpUpdate2& update,
    bool isV4,
    const std::vector<E2ETestFixture::WithdrawSpec>& routes,
    std::vector<bool>& verified) {
  size_t newlyVerified = 0;

  for (size_t i = 0; i < routes.size(); ++i) {
    if (verified[i]) {
      continue;
    }

    const auto& route = routes[i];
    const folly::CIDRNetwork expectedCidr{
        folly::IPAddress(route.prefix), route.prefixLen};

    if (findPrefixInWithdrawals(update, isV4, expectedCidr, route.addPathId)) {
      verified[i] = true;
      newlyVerified++;
    }
  }

  return newlyVerified;
}

} // namespace

bool E2ETestFixture::verifyRoutes(
    const std::string& protocol,
    const folly::IPAddress& peer,
    const std::vector<VerifySpec>& routes) {
  if (routes.empty()) {
    return true;
  }

  if (!isValidProtocol(protocol)) {
    return false;
  }

  auto peerIdOpt = findPeerIdByAddress(peer);
  if (!peerIdOpt.has_value()) {
    return false;
  }

  std::vector<bool> verified(routes.size(), false);
  size_t verifiedCount = 0;

  XLOGF(INFO, "Verifying {} routes from peer {}", routes.size(), peer.str());

  /*
   * Keep reading UPDATEs until all routes are verified
   * readOutboundUpdateToPeer() handles retries internally
   */
  int updateCount = 0;
  while (verifiedCount < routes.size()) {
    auto updateOpt = readOutboundUpdateToPeer(*peerIdOpt);
    if (!updateOpt.has_value()) {
      XLOGF(
          WARN,
          "No more updates available, verified {}/{} routes",
          verifiedCount,
          routes.size());
      break;
    }

    updateCount++;
    logBgpUpdate(
        **updateOpt,
        fmt::format("Update #{} from peer {}", updateCount, peer.str()));

    size_t newlyVerified = checkRoutesInUpdate(
        **updateOpt, isIpv4Protocol(protocol), routes, verified);

    verifiedCount += newlyVerified;
    XLOGF(
        INFO,
        "Update #{}: verified {} routes (total {}/{})",
        updateCount,
        newlyVerified,
        verifiedCount,
        routes.size());
  }

  if (verifiedCount != routes.size()) {
    XLOGF(
        ERR,
        "Route verification failed: expected {} routes, got {}",
        routes.size(),
        verifiedCount);
  }

  return verifiedCount == routes.size();
}

bool E2ETestFixture::verifyRouteWithdraws(
    const std::string& protocol,
    const folly::IPAddress& peer,
    const std::vector<WithdrawSpec>& routes) {
  if (routes.empty()) {
    return true;
  }

  if (!isValidProtocol(protocol)) {
    return false;
  }

  auto peerIdOpt = findPeerIdByAddress(peer);
  if (!peerIdOpt.has_value()) {
    return false;
  }

  std::vector<bool> verified(routes.size(), false);
  size_t verifiedCount = 0;

  /*
   * Keep reading UPDATEs until all withdrawals are verified
   * readOutboundUpdateToPeer() handles retries internally
   */
  while (verifiedCount < routes.size()) {
    auto updateOpt = readOutboundUpdateToPeer(*peerIdOpt);
    if (!updateOpt.has_value()) {
      break;
    }

    verifiedCount += checkWithdrawalsInUpdate(
        **updateOpt, isIpv4Protocol(protocol), routes, verified);
  }

  return verifiedCount == routes.size();
}

void E2ETestFixture::enableComputeUcmpFromLbw(bool enable) {
  enableComputeUcmpFromLbw_ = enable;
}

void E2ETestFixture::enableEiBgpMultipath(bool enable) {
  enableEiBgpMultipath_ = enable;
}

void E2ETestFixture::enableRibAllocatedPathId(bool enable) {
  enableRibAllocatedPathId_ = enable;
}

void E2ETestFixture::enableLegacyV4NlriEncoding(bool enable) {
  enableLegacyV4NlriEncoding_ = enable;
}

void E2ETestFixture::enableDynamicPolicyEvaluation(bool enable) {
  enableDynamicPolicyEvaluation_ = enable;
}

// ==================== NEXTHOP TRACKING IMPLEMENTATIONS ====================

void E2ETestFixture::injectNexthopStatuses(
    const std::vector<NexthopStatus>& nexthopStatuses) {
  ASSERT_NE(rib_, nullptr) << "RIB must be created first";
  ASSERT_NE(rib_->nexthopCache_, nullptr) << "NexthopCache must be initialized";

  XLOGF(
      DBG2,
      "Injecting {} nexthop statuses into RIB's NexthopCache",
      nexthopStatuses.size());

  // Inject nexthop statuses into the cache and push changed statuses to ribInQ_
  auto changedStatuses =
      rib_->nexthopCache_->addOrUpdateNextHopStatus(nexthopStatuses);
  if (!changedStatuses.empty()) {
    ribInQ_.fiberPush(RibInNexthopUpdate(std::move(changedStatuses)));
  }

  for (const auto& status : nexthopStatuses) {
    XLOGF(
        INFO,
        "Injected nexthop {} (reachable={}, igpCost={})",
        status.getNexthop().str(),
        status.isReachable(),
        status.getIgpCost().has_value()
            ? std::to_string(status.getIgpCost().value())
            : "none");
  }
}

bool E2ETestFixture::verifyNexthopRouteCount(
    const folly::IPAddress& nexthopIp,
    std::optional<size_t> expectedRouteCount) {
  if (rib_ == nullptr) {
    XLOG(ERR, "verifyNexthopRouteCount FAILED: RIB must be created first");
    return false;
  }

  // Run verification on RIB's event base thread for thread safety
  bool found = false;
  size_t actualRouteCount = 0;

  rib_->getEventBase().runInEventBaseThreadAndWait([&] {
    auto it = rib_->nexthopInfoMap_.find(nexthopIp);
    if (it != rib_->nexthopInfoMap_.end()) {
      found = true;
      actualRouteCount = it->second.getRouteInfoListSize();
      XLOGF(
          INFO,
          "Nexthop {} found in nexthopInfoMap_ with {} routes",
          nexthopIp.str(),
          actualRouteCount);
    } else {
      XLOGF(INFO, "Nexthop {} NOT found in nexthopInfoMap_", nexthopIp.str());
    }
  });

  // If expectedRouteCount is nullopt, verify nexthop does NOT exist
  if (!expectedRouteCount.has_value()) {
    if (found) {
      XLOGF(
          ERR,
          "verifyNexthopRouteCount FAILED: Nexthop {} found in nexthopInfoMap_ "
          "(expected NOT to be found)",
          nexthopIp.str());
      return false;
    }
    XLOGF(
        INFO,
        "verifyNexthopRouteCount SUCCESS: Nexthop {} not in nexthopInfoMap_",
        nexthopIp.str());
    return true;
  }

  // If expectedRouteCount has a value, verify nexthop exists with that count
  if (!found) {
    XLOGF(
        ERR,
        "verifyNexthopRouteCount FAILED: Nexthop {} not found in nexthopInfoMap_",
        nexthopIp.str());
    return false;
  }

  if (actualRouteCount != expectedRouteCount.value()) {
    XLOGF(
        ERR,
        "verifyNexthopRouteCount FAILED: Nexthop {} has {} routes, expected {}",
        nexthopIp.str(),
        actualRouteCount,
        expectedRouteCount.value());
    return false;
  }

  XLOGF(
      INFO,
      "verifyNexthopRouteCount SUCCESS: Nexthop {} has {} routes",
      nexthopIp.str(),
      actualRouteCount);
  return true;
}

std::shared_ptr<RouteInfo> E2ETestFixture::getBestPath(
    const folly::CIDRNetwork& prefix) {
  if (!rib_) {
    XLOG(ERR, "RIB is not initialized");
    return nullptr;
  }

  std::shared_ptr<RouteInfo> bestPath = nullptr;

  rib_->getEventBase().runInEventBaseThreadAndWait([&]() {
    auto ribEntry = rib_->ribEntries_.find(prefix);
    if (ribEntry != rib_->ribEntries_.end()) {
      bestPath = ribEntry->second.getBestPath();
    }
  });

  return bestPath;
}

std::shared_ptr<const WeightedNexthopMap>
E2ETestFixture::getFibWeightedNexthops(const std::string& prefixStr) {
  if (!rib_) {
    XLOG(ERR, "getFibWeightedNexthops: RIB is not initialized");
    return nullptr;
  }

  auto testFib = getTestFib();
  if (!testFib) {
    XLOG(ERR, "getFibWeightedNexthops: TestFib is not initialized");
    return nullptr;
  }

  const auto prefix = folly::IPAddress::createNetwork(prefixStr);
  std::shared_ptr<const WeightedNexthopMap> weightedNexthops;
  rib_->getEventBase().runInEventBaseThreadAndWait([&]() {
    const auto& programmedRoutes = testFib->getProgrammedRoutes();
    if (const auto it = programmedRoutes.find(prefix);
        it != programmedRoutes.end()) {
      weightedNexthops = it->second;
    }
  });

  if (weightedNexthops) {
    XLOGF(
        DBG2,
        "getFibWeightedNexthops: Found {} nexthops for prefix {}",
        weightedNexthops->size(),
        prefixStr);
    return weightedNexthops;
  }

  XLOGF(WARN, "getFibWeightedNexthops: Prefix {} not found in FIB", prefixStr);
  return nullptr;
}

uint64_t E2ETestFixture::getPeerCachedRibVersion(
    const folly::IPAddress& peerAddr) {
  if (!peerManager_) {
    XLOG(ERR, "PeerManagerBase is not initialized");
    return 0;
  }

  BgpPeerId peerId{peerAddr, peerAddr.asV4().toLongHBO()};
  uint64_t version = 0;

  peerManager_->getEventBase().runInEventBaseThreadAndWait([&]() {
    auto adjRib = peerManager_->findAdjRib(peerId);
    if (adjRib) {
      version = adjRib->getLastSeenRibVersion();
    }
  });

  return version;
}

size_t E2ETestFixture::getShadowRibEntriesCount() {
  if (!peerManager_) {
    XLOG(ERR, "PeerManagerBase is not initialized");
    return 0;
  }

  size_t count = 0;
  peerManager_->getEventBase().runInEventBaseThreadAndWait(
      [&]() { count = peerManager_->shadowRibEntries_.size(); });

  return count;
}

void E2ETestFixture::triggerRibDumpForPeer(
    const folly::IPAddress& peerAddr,
    bool sendAddPath,
    bool sendWithEoR) {
  if (!peerManager_) {
    XLOG(ERR, "PeerManagerBase is not initialized");
    return;
  }

  BgpPeerId peerId{peerAddr, peerAddr.asV4().toLongHBO()};
  peerManager_->getEventBase().runInEventBaseThreadAndWait([&]() {
    auto adjRib = peerManager_->findAdjRib(peerId);
    if (adjRib) {
      peerManager_->processRibDumpReq(
          adjRib, RibDumpReq(peerId, sendAddPath), sendWithEoR);
    }
  });
}

std::optional<E2ETestFixture::PeerQueues> E2ETestFixture::getPeerQueues(
    const BgpPeerId& peerId) const {
  auto it = peerQueues_.find(peerId);
  if (it == peerQueues_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::unordered_map<BgpPeerId, E2ETestFixture::PeerQueues>
E2ETestFixture::getAllPeerQueues() const {
  return peerQueues_;
}

std::optional<BgpPeerId> E2ETestFixture::findPeerIdByAddress(
    const folly::IPAddress& peerAddr) const {
  for (const auto& [id, queues] : peerQueues_) {
    if (id.peerAddr == peerAddr) {
      return id;
    }
  }
  return std::nullopt;
}

} // namespace bgp
} // namespace facebook
