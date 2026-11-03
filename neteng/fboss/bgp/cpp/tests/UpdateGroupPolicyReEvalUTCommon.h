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

/*
 * Unit-test fixture for single peer policy re-evaluation tests.
 *
 * PeerManager_TEST_FRIENDS and AdjRib_TEST_FRIENDS MUST be defined
 * before including this header to grant friend access.
 *
 * Provides:
 *   - PeerManager with update groups enabled
 *   - Two AdjRibs in the same group (same peerGroupName)
 *   - 100 routes pre-loaded in shadowRibEntries_ with 10 attribute sets
 *   - Real routing policies (propagate-all + re-eval-tag)
 *   - Helpers to manipulate peer state and trigger policy re-eval
 */

#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Task.h>
#include <folly/coro/Timeout.h>
#include <folly/hash/Hash.h>
#include <gtest/gtest.h>

#include "neteng/fboss/bgp/cpp/BgpServiceBase.h"
#include "neteng/fboss/bgp/cpp/BgpServiceUtil.h"
#include "neteng/fboss/bgp/cpp/changeTracker/ChangeTracker.h"
#include "neteng/fboss/bgp/cpp/changeTracker/TrackableObject.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/config/ConfigManager.h"
#include "neteng/fboss/bgp/cpp/lib/fibers/FiberBgpPeerManager.h"
#include "neteng/fboss/bgp/cpp/peer/SessionManager.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyManager.h"
#include "neteng/fboss/bgp/cpp/tests/PeerManagerTestUtils.h"
#include "neteng/fboss/bgp/cpp/tests/PolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"

namespace facebook::bgp {

/* ========= TEST FIXTURE CONSTANTS ========= */

/* ---- Policy community constants ---- */
inline constexpr auto kPCommNoAdvt = "65535:65282";
inline constexpr auto kPCommModify = "65500:100";
inline constexpr auto kPCommAppend = "65500:200";

/* ---- Policy name constants ---- */
inline constexpr auto kPNameMatchNoAdvtDeny =
    "match-no-advt-community-deny-continue";
inline constexpr auto kPNameMatchModifyAppend =
    "match-modify-community-append-tag-continue";
inline constexpr auto kPNamePermitAll = "permit-all-continue";

/* ---- Route parameterization constants ---- */
inline constexpr int kRouteCount = 100;
inline constexpr int kAttrSetCount = 10;

/* ---- Partial route-publish batch sizes (subset of kRouteCount) ---- */
// First batch, large enough to fill a blocking peer's queue and detach it.
inline constexpr int kDetachTriggerBatch = 20;
// Follow-up batch (DSP) that advances the group past the detached peer.
inline constexpr int kGroupAdvanceBatch = 12;
// Rounds of drain-then-publish used to advance the group's changelist marker.
inline constexpr int kGroupAdvanceRounds = 5;

/* ---- Bounded out-queue sizing ---- */
// Large default out-queue: peers never block under normal test load.
inline constexpr size_t kDefaultOutQueueCapacity = 1000;
inline constexpr size_t kDefaultOutQueueHighWm = 800;
inline constexpr size_t kDefaultOutQueueLowWm = 50;
// Small out-queue capacity used to make a peer block after a few routes.
inline constexpr size_t kBlockingOutQueueCapacity = 10;

/* ---- Blocking-queue watermarks (capacity kBlockingOutQueueCapacity) ----
 * A "target" peer gets a lower high-watermark than its "blocker" so it blocks
 * first and is the one detached.
 */
// blockGroupViaPeerOnEvb: single blocked peer.
inline constexpr size_t kBlockGroupHighWm = 8;
inline constexpr size_t kBlockGroupLowWm = 2;
// triggerDetachedBlockedFromDetachedOnEvb.
inline constexpr size_t kDetachFromDetachedTargetHighWm = 7;
inline constexpr size_t kDetachFromDetachedBlockerHighWm = 8;
inline constexpr size_t kDetachFromDetachedLowWm = 2;
// triggerPeerDetachedReadyToJoinOnEvb.
inline constexpr size_t kReadyToJoinTargetHighWm = 6;
inline constexpr size_t kReadyToJoinBlockerHighWm = 7;
inline constexpr size_t kReadyToJoinLowWm = 0;
// DSP blocker: larger queue so it doesn't block on the first batch.
inline constexpr size_t kDspBlockerCapacity = 20;
inline constexpr size_t kDspBlockerHighWm = 12;
// triggerDetachedBlockedFromJoinedOnEvb: tiny queue pre-filled to force block.
inline constexpr size_t kFillQueueCapacity = 3;
inline constexpr size_t kFillQueueHighWm = 2;
inline constexpr size_t kFillQueueLowWm = 1;
// triggerDetachedInitDump: out-queue for the re-established peer.
inline constexpr size_t kInitDumpQueueCapacity = 100;
inline constexpr size_t kInitDumpQueueHighWm = 80;
inline constexpr size_t kInitDumpQueueLowWm = 10;

/* ---- Slow-peer detection thresholds ---- */
/*
 * Sentinels large enough that the corresponding trigger effectively never
 * fires.
 */
inline constexpr auto kSlowPeerTimeNever = std::chrono::milliseconds(50000000);
inline constexpr int kSlowPeerBlockCountNever = 100000;
inline constexpr auto kSlowPeerBlockWindowNever =
    std::chrono::milliseconds(50000000);
// Standard finite block-count window.
inline constexpr auto kSlowPeerBlockWindow = std::chrono::milliseconds(60000);
// Lenient (finite) time threshold used when isolating frequency-based detach.
inline constexpr auto kSlowPeerTimeLenient = std::chrono::milliseconds(600000);
// Detach on the first block (frequency) / almost immediately (duration).
inline constexpr int kSlowPeerBlockCountImmediate = 1;
inline constexpr auto kSlowPeerTimeImmediate = std::chrono::milliseconds(1);

class UpdateGroupPolicyReEvalUTBase : public PeerManagerTestFixture {
 protected:
  using BgpPeerId = nettools::bgplib::BgpPeerId;

  struct TestContext {
    std::shared_ptr<PeerManagerBase> peerMgr;
    std::shared_ptr<SessionManager> sessionMgr;
    std::shared_ptr<ConfigManager> configMgr;
    folly::F14FastMap<BgpPeerId, std::shared_ptr<AdjRib>> adjRibs;
    std::thread peerMgrThread;
    std::thread sessionMgrThread;

    // Queues created by triggerPeerUpOnEvb that need cleanup in tearDown
    std::vector<std::shared_ptr<AdjRib::AdjRibInQueueT>> sessionInQueues;
    std::vector<std::shared_ptr<AdjRib::AdjRibOutQueueT>>
        sessionBoundedOutQueues;

    int ribBaseVersion{0};
    bool manuallyApplyPolicyNames{false};
  };

  /*
   * Build a splitToNewGroup target the way
   * UpdateGroupManager::findOrCreateGroup builds a group. splitToNewGroup
   * copies only operational state, so whatever the constructor wires has to be
   * passed here or the clone is not faithful: the shadow RIB view (read by the
   * change list consume timer the split registers on the target) and the policy
   * manager.
   */
  std::shared_ptr<AdjRibOutGroup> makeSplitTargetGroup(
      TestContext& ctx,
      folly::EventBase& evb,
      const std::shared_ptr<AdjRibOutGroup>& sourceGroup) {
    return std::make_shared<AdjRibOutGroup>(
        evb,
        "split_target",
        sourceGroup->getGroupId() + 1,
        /*enableUpdateGroup=*/true,
        sourceGroup->getGroupKey(),
        ShadowRibView{
            ctx.peerMgr->shadowRibEntries_, ctx.peerMgr->maxRibVersion_},
        ctx.peerMgr->getPolicyManager());
  }

  std::shared_ptr<AdjRib> setupAdjRibWithPeerGroup(
      folly::EventBase& evb,
      const nettools::bgplib::BgpPeerId& peerId,
      const AsNum& remoteAs,
      std::shared_ptr<folly::coro::Baton>& sessionTerminateBaton,
      const std::shared_ptr<ConfigManager>& configManager,
      const std::shared_ptr<PolicyManager>& policyManager,
      const std::string& description,
      const std::string& peerGroupName) {
    auto config = configManager->getConfig();
    std::optional<std::string> egressPolicyName;
    const auto& peerGroups = config->getPeerGroups();
    auto pgIt = peerGroups.find(peerGroupName);
    if (pgIt != peerGroups.end() &&
        pgIt->second.egress_policy_name().has_value()) {
      egressPolicyName = *pgIt->second.egress_policy_name();
    }
    // A per-peer egress override takes precedence over the peer group's policy.
    auto peerConfig = config->getConfigOfAPeer(peerId.peerAddr);
    if (peerConfig && peerConfig->egressPolicyName.has_value()) {
      egressPolicyName = peerConfig->egressPolicyName;
    }

    auto adjRibOutGroup = std::make_shared<AdjRibOutGroup>(evb, peerGroupName);
    auto adjRib = std::make_shared<AdjRib>(
        peerId,
        PeeringParams(
            peerId.peerAddr,
            std::nullopt,
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
            kV4Nexthop1.asV4(),
            kV6Nexthop1.asV6(),
            RrClientConfigured(false),
            NextHopSelfConfigured{false},
            AfiIpv4Configured{true},
            AfiIpv6Configured{true},
            ConfedPeerConfigured{false},
            RemovePrivateAsConfigured{false},
            std::nullopt,
            std::nullopt,
            AdvertiseLinkBandwidth::DISABLE,
            ReceiveLinkBandwidth::ACCEPT,
            std::nullopt,
            ValidateRemoteAs{true},
            std::nullopt,
            std::nullopt,
            false,
            EnableStatefulHa{false},
            std::nullopt,
            V4OverV6Nexthop{false}),
        evb,
        ribInQ_,
        observerQ_,
        sessionTerminateBaton,
        policyManager,
        isSafeModeOn_,
        /*ingressPolicyName=*/std::nullopt,
        /*egressPolicyName=*/egressPolicyName,
        adjRibOutGroup,
        std::nullopt,
        configManager);
    adjRib->peeringParams_.description = description;
    adjRib->peeringParams_.peerGroupName = peerGroupName;
    adjRib->adjRibOutQueue_ = std::make_shared<AdjRib::AdjRibOutQueueT>(
        kDefaultOutQueueCapacity,
        kDefaultOutQueueHighWm,
        kDefaultOutQueueLowWm);
    adjRib->pathIdGenerator_ = std::make_unique<PathIdGenerator>(false);
    adjRib->isAfiIpv4Negotiated_ = true;
    adjRib->remoteAs_ = remoteAs;
    adjRib->markStateEstablished();
    return adjRib;
  }

  /*
   * Denies routes with kPCommNoAdvt community, accepts everything else.
   */
  static bgp_policy::BgpPolicyStatement buildMatchNoAdvtDenyPolicy() {
    auto match = createBgpPolicyAtomicMatch(
        bgp_policy::BgpPolicyAtomicMatchType::COMMUNITY_LIST, {kPCommNoAdvt});
    auto deny = createBgpPolicyAction(bgp_policy::BgpPolicyActionType::DENY);
    auto denyTerm = createBgpPolicyTerm(
        "deny-no-advt",
        "",
        {std::move(match)},
        {std::move(deny)},
        bgp_policy::FlowControlAction::NEXT_TERM);

    auto accept =
        createBgpPolicyAction(bgp_policy::BgpPolicyActionType::PERMIT);
    auto acceptTerm = createBgpPolicyTerm(
        "accept-all",
        "",
        {},
        {std::move(accept)},
        bgp_policy::FlowControlAction::NEXT_TERM);

    return createBgpPolicyStatement(
        kPNameMatchNoAdvtDeny, {std::move(denyTerm), std::move(acceptTerm)});
  }

  /*
   * Appends kPCommAppend to routes with kPCommModify, accepts all.
   */
  static bgp_policy::BgpPolicyStatement buildMatchModifyAppendPolicy() {
    auto match = createBgpPolicyAtomicMatch(
        bgp_policy::BgpPolicyAtomicMatchType::COMMUNITY_LIST, {kPCommModify});
    auto append = createBgpPolicyCommunityAction(
        bgp_policy::CommunityActionType::ADD, {kPCommAppend});
    auto permit =
        createBgpPolicyAction(bgp_policy::BgpPolicyActionType::PERMIT);
    auto tagTerm = createBgpPolicyTerm(
        "append-tag",
        "",
        {std::move(match)},
        {std::move(append), std::move(permit)},
        bgp_policy::FlowControlAction::NEXT_TERM);

    auto accept =
        createBgpPolicyAction(bgp_policy::BgpPolicyActionType::PERMIT);
    auto acceptTerm = createBgpPolicyTerm(
        "accept-all",
        "",
        {},
        {std::move(accept)},
        bgp_policy::FlowControlAction::NEXT_TERM);

    return createBgpPolicyStatement(
        kPNameMatchModifyAppend, {std::move(tagTerm), std::move(acceptTerm)});
  }

  /*
   * Permit-all policy. Used as a distinct third egress policy name in
   * multi-group re-evaluation tests; its filtering behavior is irrelevant --
   * only the policy name (as part of the UpdateGroupKey) matters there.
   */
  static bgp_policy::BgpPolicyStatement buildPermitAllPolicy() {
    auto permit =
        createBgpPolicyAction(bgp_policy::BgpPolicyActionType::PERMIT);
    auto acceptTerm = createBgpPolicyTerm(
        "accept-all",
        "",
        {},
        {std::move(permit)},
        bgp_policy::FlowControlAction::NEXT_TERM);
    return createBgpPolicyStatement(kPNamePermitAll, {std::move(acceptTerm)});
  }

  static bgp_policy::BgpPolicies buildPolicies() {
    bgp_policy::BgpPolicies policies;
    policies.bgp_policy_statements()->emplace_back(
        buildMatchNoAdvtDenyPolicy());
    policies.bgp_policy_statements()->emplace_back(
        buildMatchModifyAppendPolicy());
    policies.bgp_policy_statements()->emplace_back(buildPermitAllPolicy());
    return policies;
  }

  /*
   * Populate 100 routes in shadowRibEntries_.
   * Routes: 100.0.0.0/24 through 100.0.99.0/24.
   * Attribute sets cycle every kAttrSetCount routes via AS_PATH length.
   * Odd-numbered routes are tagged with kPCommNoAdvt.
   * Routes at multiples of 3 are tagged with kPCommModify.
   * A route can carry both communities (e.g. route 3, 9, 15, ...).
   *
   * ribBaseVersion: determines the rib version range and localPref.
   *   ribVersion = ribBaseVersion * 100 + i + 1 (range [base*100+1,
   * base*100+100]). localPref = ribBaseVersion. Use ribBaseVersion=0 for the
   * first batch, ribBaseVersion=1 for the next, etc. to produce non-overlapping
   * version ranges and distinguish batches by localPref.
   *
   * isInitialDump: sets announcement.initialDump.
   *   true = routes are part of the initial RIB dump (default).
   *   false = routes are incremental updates after initialization.
   *
   * routeIndexPredicate: only routes where the predicate returns true
   *   for the route index (0..99) are included in the announcement.
   *   Default: all routes included.
   */
  void publishRouteUpdates(
      TestContext& ctx,
      bool isInitialDump = true,
      std::function<bool(int)> routeIndexPredicate = [](int) { return true; }) {
    int ribBaseVersion = ctx.ribBaseVersion++;
    XLOG(INFO) << "publishRouteUpdates ribBaseVersion " << ribBaseVersion;
    auto& evb = ctx.peerMgr->getEventBase();

    evb.runInEventBaseThreadAndWait([&]() {
      TinyPeerInfo peer(
          kPeerAddr2, kAsn1, kPeerRouterId2, BgpSessionType::EBGP, false);

      RibOutAnnouncement announcement;
      announcement.initialDump = isInitialDump;

      for (int i = 0; i < kRouteCount; ++i) {
        if (!routeIndexPredicate(i)) {
          continue;
        }
        int asPathLen = (i % kAttrSetCount) + 1;
        auto fields = buildBgpPathFields(asPathLen, 0, 0, 0);
        {
          auto mutableAttrs = fields->attrs.get();
          mutableAttrs.localPref = ribBaseVersion;
          fields->attrs = std::move(mutableAttrs);
        }

        nettools::bgplib::BgpAttrCommunitiesC communities;
        if (i % 2 == 1) {
          communities.push_back(
              *nettools::bgplib::BgpAttrCommunityC::createBgpAttrCommunity(
                  kPCommNoAdvt));
        }
        if (i % 3 == 0) {
          communities.push_back(
              *nettools::bgplib::BgpAttrCommunityC::createBgpAttrCommunity(
                  kPCommModify));
        }
        if (!communities.empty()) {
          auto mutableAttrs = fields->attrs.get();
          mutableAttrs.communities = std::move(communities);
          fields->attrs = std::move(mutableAttrs);
        }

        auto attrs = std::make_shared<BgpPath>(*fields);
        attrs->publish();

        announcement.entries.emplace_back(
            folly::CIDRNetwork{
                folly::IPAddress(fmt::format("100.0.{}.0", i)), 24},
            kDefaultPathID,
            peer,
            attrs);
        announcement.entries.back().ribVersion = ribBaseVersion * 100 + i + 1;
      }

      ctx.peerMgr->handleShadowRibEntryAnnouncement(announcement);
    });
  }

  /*
   * Publish a single add-path-only change. It is published with the add-path
   * consumer bitmap, so a non-add-path consumer's bit is never set on the
   * change item: the change advances PeerManager's max RIB version without
   * ever being offered to those consumers.
   *
   * Returns the resulting max RIB version.
   */
  uint64_t publishAddPathOnlyUpdate(TestContext& ctx) {
    auto& evb = ctx.peerMgr->getEventBase();
    uint64_t maxRibVersion = 0;
    evb.runInEventBaseThreadAndWait([&]() {
      TinyPeerInfo peer(
          kPeerAddr2, kAsn1, kPeerRouterId2, BgpSessionType::EBGP, false);
      auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 0, 0, 0));
      attrs->publish();

      RibOutAnnouncement announcement;
      announcement.addPathEntries.emplace_back(
          folly::CIDRNetwork{folly::IPAddress("200.0.0.0"), 24},
          1 /* pathIdToSend */,
          peer,
          attrs);
      announcement.addPathEntries.back().ribVersion =
          ctx.peerMgr->getMaxRibVersion() + 1;
      ctx.peerMgr->handleShadowRibEntryAnnouncement(announcement);
      maxRibVersion = ctx.peerMgr->getMaxRibVersion();
    });
    return maxRibVersion;
  }

  void expectEventualStateOnEvb(
      TestContext& ctx,
      const BgpPeerId& peerId,
      PeerUpdateState expected) {
    auto& evb = ctx.peerMgr->getEventBase();
    WITH_RETRIES({
      auto state = folly::via(&evb, [&]() {
                     return ctx.adjRibs.at(peerId)->getPeerState();
                   }).get();
      EXPECT_EVENTUALLY_EQ(state, expected);
    });
  }

  void drainOne(TestContext& ctx, const BgpPeerId& peerId) {
    auto queue = ctx.adjRibs.at(peerId)->adjRibOutQueue_;
    XLOGF(
        DBG5,
        "drainOne for peer {}: queue size={}",
        peerId.peerAddr.str(),
        queue->size());
    /*
     * Pop one item, but don't block forever if the peer never pushes another
     * (e.g. it already caught up to the group). Time out after a short budget
     * so a logic problem surfaces as a quick failure instead of a hang.
     */
    auto result = folly::coro::blockingWait(
        folly::coro::co_awaitTry(
            folly::coro::timeout(queue->pop(), std::chrono::seconds(2))));
    if (result.hasException()) {
      XLOGF(
          DBG5,
          "drainOne for peer {}: timed out, nothing to pop",
          peerId.peerAddr.str());
    }
  }

  void drainQueue(TestContext& ctx, const BgpPeerId& peerId) {
    auto queue = ctx.adjRibs.at(peerId)->adjRibOutQueue_;
    auto initialSize = queue->size();
    size_t popped = 0;
    while (!queue->empty()) {
      folly::coro::blockingWait(queue->pop());
      ++popped;
    }
    XLOGF(
        DBG5,
        "drainQueue for peer {}: initial size={}, popped={}",
        peerId.peerAddr.str(),
        initialSize,
        popped);
  }

  void resizePeerQueue(
      const std::shared_ptr<AdjRib>& adjRib,
      size_t capacity,
      size_t hiWm,
      size_t loWm) {
    adjRib->adjRibOutQueue_ =
        std::make_shared<AdjRib::AdjRibOutQueueT>(capacity, hiWm, loWm);
  }

  /*
   * Block the group by shrinking a joined peer's queue. The caller
   * should publish route updates afterwards to trigger distribution
   * and fill the small queue, causing the peer to become JOINED_BLOCKED
   * and the group to enter WAITING.
   * Slow peer thresholds are set very lenient to prevent detachment.
   */
  void blockGroupViaPeerOnEvb(
      TestContext& ctx,
      const BgpPeerId& blockedPeerId) {
    auto& evb = ctx.peerMgr->getEventBase();

    evb.runInEventBaseThreadAndWait([&]() {
      auto& adjRib = ctx.adjRibs.at(blockedPeerId);
      resizePeerQueue(
          adjRib,
          kBlockingOutQueueCapacity,
          kBlockGroupHighWm,
          kBlockGroupLowWm);

      auto group = adjRib->getUpdateGroup();
      UpdateGroupConfig cfg;
      cfg.enableSlowPeerBlockCountDetection = true;
      cfg.allowSlowPeerDetach = true;
      cfg.slowPeerTimeThreshold = kSlowPeerTimeNever;
      cfg.slowPeerBlockCountThreshold = kSlowPeerBlockCountNever;
      cfg.slowPeerBlockCountWindow = kSlowPeerBlockWindowNever;
      group->setUpdateGroupConfigForTesting(cfg);
    });
  }

  /*
   * Transition a peer from JOINED_BLOCKED to DETACHED_BLOCKED.
   * Requires at least 2 peers in the group (detachSlowPeer skips if
   * numInSyncPeers <= 1).
   *
   * useBlockFrequency=true: frequency-based detection (block count
   *   threshold = 1, triggers on first markPeerBlocked).
   * useBlockFrequency=false: duration-based detection (very short
   *   slowPeerTimeThreshold, triggers after timer fires).
   *
   * Steps:
   *   1. Set all peers in the group to JOINED_RUNNING with sync bitmap
   *   2. Configure slow peer thresholds based on useBlockFrequency
   *   3. Shrink only the target's bounded queue so it backpressures first
   *   4. Publish a small route batch: the normal group send path
   *      (buildAndSendGroupBgpMessages -> tryPushToPeer) backpressures the
   *      target, which schedules its deferredPushToPeer and marks it blocked;
   *      the slow-peer criteria then detach it to DETACHED_BLOCKED -- leaving a
   *      live deferredPushToPeer so it can recover when its queue drains.
   */
  void triggerDetachedBlockedFromJoinedOnEvb(
      TestContext& ctx,
      const BgpPeerId& targetPeerId,
      bool useBlockFrequency = true) {
    auto& evb = ctx.peerMgr->getEventBase();

    evb.runInEventBaseThreadAndWait([&]() {
      auto& targetAdjRib = ctx.adjRibs.at(targetPeerId);
      auto group = targetAdjRib->getUpdateGroup();
      ASSERT_NE(group, nullptr);
      ASSERT_GE(group->getMemberCount(), 2)
          << "Need at least 2 peers for frequency detachment";

      for (auto& [peerId, adjRib] : ctx.adjRibs) {
        if (adjRib->getUpdateGroup().get() == group.get()) {
          adjRib->setPeerState(PeerUpdateState::JOINED_RUNNING);
          group->markPeerInSync(adjRib);
        }
      }

      UpdateGroupConfig cfg;
      cfg.enableSlowPeerBlockCountDetection = true;
      cfg.allowSlowPeerDetach = true;
      if (useBlockFrequency) {
        cfg.slowPeerTimeThreshold = kSlowPeerTimeLenient;
        cfg.slowPeerBlockCountThreshold = kSlowPeerBlockCountImmediate;
        cfg.slowPeerBlockCountWindow = kSlowPeerBlockWindow;
      } else {
        cfg.slowPeerTimeThreshold = kSlowPeerTimeImmediate;
        cfg.slowPeerBlockCountThreshold = kSlowPeerBlockCountNever;
        cfg.slowPeerBlockCountWindow = kSlowPeerBlockWindow;
      }
      group->setUpdateGroupConfigForTesting(cfg);

      /*
       * Shrink only the target's queue so it is the first (and only) peer to
       * backpressure on the next distribution; the other members keep their
       * larger queues, stay JOINED_RUNNING, and keep the group unfrozen.
       */
      resizePeerQueue(
          targetAdjRib,
          kBlockingOutQueueCapacity,
          kReadyToJoinTargetHighWm,
          kReadyToJoinLowWm);
    });

    /*
     * Drive a real distribution: buildAndSendGroupBgpMessages pushes to the
     * target, whose small queue backpressures. tryPushToPeer then marks it
     * blocked AND schedules a deferredPushToPeer, and the slow-peer criteria
     * detach it to DETACHED_BLOCKED -- leaving that deferredPushToPeer live so
     * the peer recovers once its queue drains (markPeerUnblocked). The block
     * emerges from the normal send path; no internal state is forced.
     */
    publishRouteUpdates(ctx, /*isInitialDump=*/false, [](int i) {
      return i < kDetachTriggerBatch;
    });
    expectEventualStateOnEvb(
        ctx, targetPeerId, PeerUpdateState::DETACHED_BLOCKED);
    XLOG(INFO) << "Peer " << targetPeerId.peerAddr.str()
               << " detached from JOINED via "
               << (useBlockFrequency ? "frequency" : "duration");
  }

  /*
   * Transition a peer to DETACHED_BLOCKED via the detached mode path:
   * JOINED_BLOCKED -> DETACHED_BLOCKED -> drain -> DETACHED_RUNNING
   * -> process changelist entries -> queue re-fills -> DETACHED_BLOCKED.
   *
   * Requires setUp + sendInitialRibDump already called.
   * Publishes routes to create changelist entries for the detached peer
   * to process after being unblocked.
   */
  void triggerDetachedBlockedFromDetachedOnEvb(
      TestContext& ctx,
      const BgpPeerId& targetPeerId) {
    auto& evb = ctx.peerMgr->getEventBase();

    // Find the blocker: any other peer in the same group.
    BgpPeerId blockerPeerId = [&]() {
      auto group = ctx.adjRibs.at(targetPeerId)->getUpdateGroup();
      for (auto& [bitPos, adjRib] : group->getBitToAdjRibs()) {
        if (adjRib->getRemotePeerId() != targetPeerId) {
          return adjRib->getRemotePeerId();
        }
      }
      return targetPeerId;
    }();

    /*
     * Step 1: Configure queues and thresholds. Use the same setup
     * as triggerPeerDetachedReadyToJoin to get a real detachment
     * with a running deferredPushToPeer coroutine.
     */
    evb.runInEventBaseThreadAndWait([&]() {
      auto group = ctx.adjRibs.at(targetPeerId)->getUpdateGroup();
      UpdateGroupConfig cfg;
      cfg.enableSlowPeerBlockCountDetection = true;
      cfg.allowSlowPeerDetach = true;
      cfg.slowPeerTimeThreshold = kSlowPeerTimeNever;
      cfg.slowPeerBlockCountThreshold = kSlowPeerBlockCountImmediate;
      cfg.slowPeerBlockCountWindow = kSlowPeerBlockWindow;
      group->setUpdateGroupConfigForTesting(cfg);

      // Target's high-watermark is below the blocker's so it blocks first.
      resizePeerQueue(
          ctx.adjRibs.at(targetPeerId),
          kBlockingOutQueueCapacity,
          kDetachFromDetachedTargetHighWm,
          kDetachFromDetachedLowWm);
      resizePeerQueue(
          ctx.adjRibs.at(blockerPeerId),
          kBlockingOutQueueCapacity,
          kDetachFromDetachedBlockerHighWm,
          kDetachFromDetachedLowWm);
    });

    // Step 2: Publish routes to trigger detachment.
    publishRouteUpdates(ctx, /*isInitialDump=*/false, [](int i) {
      return i < kDetachTriggerBatch;
    });
    expectEventualStateOnEvb(
        ctx, targetPeerId, PeerUpdateState::DETACHED_BLOCKED);

    /*
     * After the first detach the blocker is JOINED_BLOCKED and the group is
     * frozen, so the detached peer sits exactly at the group's changelist
     * marker -- a detached peer is "caught up" once its marker == the group's
     * (AdjRib::readyToRejoin, AdjRibOut.cpp), and it never advances past the
     * group -- so it has nothing to process and cannot re-block. To re-block it
     * from detached we must move the group PAST it, freeze the group there,
     * then let the detached peer catch up and refill its own queue.
     */

    /*
     * Step 3: Unblock the group and advance its changelist marker well past the
     * detached peer. Repeatedly drain the blocker (so the group keeps
     * consuming) while publishing fresh changes. The detached target stays
     * frozen at its marker because its queue is full, so the gap between its
     * marker and the group's keeps growing.
     */
    for (int round = 0; round < kGroupAdvanceRounds; ++round) {
      drainQueue(ctx, blockerPeerId);
      publishRouteUpdates(ctx, /*isInitialDump=*/false);
    }

    /*
     * Step 4: Re-block the group at the advanced position. Stop draining the
     * blocker and publish once more so its queue fills and the group freezes
     * ahead of the detached peer.
     */
    publishRouteUpdates(ctx, /*isInitialDump=*/false);
    expectEventualStateOnEvb(
        ctx, blockerPeerId, PeerUpdateState::JOINED_BLOCKED);

    /*
     * Step 5: Activate the detached target (drain its queue once to drop below
     * the low-water mark), then leave it alone. As it catches up to the group's
     * advanced marker it refills its small queue past the high-water mark and
     * re-blocks. Do NOT keep draining -- that is what previously kept the queue
     * empty so it never re-filled.
     */
    drainQueue(ctx, targetPeerId);
    expectEventualStateOnEvb(
        ctx, targetPeerId, PeerUpdateState::DETACHED_BLOCKED);
  }

  /*
   * Transition a peer to DETACHED_READY_TO_JOIN.
   *
   * Requires setUp + sendInitialRibDump already called.
   * targetIndex is the peer index to detach. The peer with the next
   * higher bit position in the group becomes the JOINED_BLOCKED
   * blocker that prevents the group from advancing.
   *
   * isDFP=true (Detached Fast Peer):
   *   Group and peer are at the same CL position after the peer
   *   finishes draining. The peer was never behind the group.
   *
   * isDFP=false (Detached Slow Peer):
   *   Additional route updates are published after detachment so the
   *   group moves ahead. The peer catches up by consuming the extra
   *   CL changes.
   */
  void triggerPeerDetachedReadyToJoin(
      TestContext& ctx,
      const BgpPeerId& targetPeerId,
      bool isDFP = true) {
    auto& evb = ctx.peerMgr->getEventBase();

    // Find the blocker: any other peer in the same group.
    BgpPeerId blockerPeerId = [&]() {
      auto group = ctx.adjRibs.at(targetPeerId)->getUpdateGroup();
      for (auto& [bitPos, adjRib] : group->getBitToAdjRibs()) {
        if (adjRib->getRemotePeerId() != targetPeerId) {
          return adjRib->getRemotePeerId();
        }
      }
      return targetPeerId;
    }();

    /*
     * Step 1: Configure thresholds.
     * Frequency threshold = 1: first peer to block gets detached.
     * The target peer has a smaller queue than the blocker, so it
     * blocks first during distribution.
     */
    evb.runInEventBaseThreadAndWait([&]() {
      auto group = ctx.adjRibs.at(targetPeerId)->getUpdateGroup();
      UpdateGroupConfig cfg;
      cfg.enableSlowPeerBlockCountDetection = true;
      cfg.allowSlowPeerDetach = true;
      cfg.slowPeerTimeThreshold = kSlowPeerTimeNever;
      cfg.slowPeerBlockCountThreshold = kSlowPeerBlockCountImmediate;
      cfg.slowPeerBlockCountWindow = kSlowPeerBlockWindow;
      group->setUpdateGroupConfigForTesting(cfg);

      // Target's high-watermark is below the blocker's so it blocks first.
      resizePeerQueue(
          ctx.adjRibs.at(targetPeerId),
          kBlockingOutQueueCapacity,
          kReadyToJoinTargetHighWm,
          kReadyToJoinLowWm);
      if (isDFP) {
        resizePeerQueue(
            ctx.adjRibs.at(blockerPeerId),
            kBlockingOutQueueCapacity,
            kReadyToJoinBlockerHighWm,
            kReadyToJoinLowWm);
      } else {
        /*
         * DSP: blocker needs a larger queue so it doesn't block on
         * the first batch, allowing the group to advance on the CL
         * before blocking on the second batch.
         */
        resizePeerQueue(
            ctx.adjRibs.at(blockerPeerId),
            kDspBlockerCapacity,
            kDspBlockerHighWm,
            kReadyToJoinLowWm);
      }
    });

    /*
     * Step 2: Publish routes to trigger distribution.
     * Target peer blocks → frequency detach → DETACHED_BLOCKED.
     */
    publishRouteUpdates(ctx, /*isInitialDump=*/false, [](int i) {
      return i < kDetachTriggerBatch;
    });

    expectEventualStateOnEvb(
        ctx, targetPeerId, PeerUpdateState::DETACHED_BLOCKED);

    /*
     * Step 3 (DSP only): Publish more routes so the group moves ahead.
     * Wait for the blocker peer to become JOINED_BLOCKED before
     * proceeding, so the group has fully processed the new routes.
     */
    if (!isDFP) {
      publishRouteUpdates(ctx, /*isInitialDump=*/false, [](int i) {
        return i < kGroupAdvanceBatch;
      });
      expectEventualStateOnEvb(
          ctx, blockerPeerId, PeerUpdateState::JOINED_BLOCKED);
    }

    /*
     * Step 4: Drain the target peer's queue so the detached peer can
     * finish its packing list.
     */
    drainQueue(ctx, targetPeerId);

    /*
     * Step 4b (DSP only): The detached peer receives new changelist
     * entries from step 3 and re-blocks. Drain again.
     */
    if (!isDFP) {
      expectEventualStateOnEvb(
          ctx, targetPeerId, PeerUpdateState::DETACHED_BLOCKED);
      drainQueue(ctx, targetPeerId);
    }

    // Step 5: Wait for the peer to reach DETACHED_READY_TO_JOIN.
    expectEventualStateOnEvb(
        ctx, targetPeerId, PeerUpdateState::DETACHED_READY_TO_JOIN);
  }

  /*
   * Bring up a new peer into DETACHED_INIT_DUMP state.
   *
   * Precondition: the group must be initialized (not UNINITIALIZED).
   * Call setUp(2) + sendInitialRibDump first.
   *
   * Steps:
   *   1. Set testOnlyDeferInitDump on the new peer's address so the
   *      RibDumpReq is re-buffered and the peer stays in
   *      DETACHED_INIT_DUMP.
   *   2. Bring up the peer via triggerPeerUpOnEvb with large queues
   *      (capacity=100, hiWm=80, loWm=10).
   *   3. Wait for the peer to reach DETACHED_INIT_DUMP.
   *
   * Returns the new peer's BgpPeerId.
   */
  BgpPeerId triggerDetachedInitDump(TestContext& ctx, int peerIndex) {
    auto peerId = makePeerId(peerIndex);
    auto& evb = ctx.peerMgr->getEventBase();

    triggerPeerDownOnEvb(ctx, peerId);
    expectEventualStateOnEvb(ctx, peerId, PeerUpdateState::DOWN);

    ctx.peerMgr->testOnlySetDeferInitDump(peerId.peerAddr, true);
    triggerPeerUpOnEvb(
        ctx,
        peerId,
        kInitDumpQueueCapacity,
        kInitDumpQueueHighWm,
        kInitDumpQueueLowWm);

    /*
     * sessionEstablished creates a new AdjRib. Refresh ctx.adjRibs
     * so subsequent checks use the new AdjRib, not the stale one.
     */
    evb.runInEventBaseThreadAndWait([&]() {
      auto it = ctx.peerMgr->adjRibs_.find(peerId);
      if (it != ctx.peerMgr->adjRibs_.end()) {
        ctx.adjRibs[peerId] = it->second;
      }
    });

    expectEventualStateOnEvb(ctx, peerId, PeerUpdateState::DETACHED_INIT_DUMP);

    return peerId;
  }

  /*
   * Simulate a peer session going down by pushing an IDLE event
   * directly to the notifyCoroQueue. PeerManager::processPeerEventLoop
   * processes it as sessionTerminated.
   */
  void triggerPeerDownOnEvb(TestContext& ctx, const BgpPeerId& peerId) {
    nettools::bgplib::FiberBgpPeer::ObservableStateT stateEvt{
        .peerId = peerId,
        .state = nettools::bgplib::BgpSessionState::IDLE,
        .versionNumber = 0,
        .sessionInfo = nullptr};

    nettools::bgplib::ObservableEventT obsEvent = std::move(stateEvt);
    ctx.sessionMgr->getNotifyCoroQueue().push(std::move(obsEvent));
  }

  /*
   * Simulate a peer session coming up by pushing an ESTABLISHED event
   * directly to the notifyCoroQueue. PeerManager::processPeerEventLoop
   * processes it as sessionEstablished.
   */
  void triggerPeerUpOnEvb(
      TestContext& ctx,
      const BgpPeerId& peerId,
      int queueCapacity = kDefaultOutQueueCapacity,
      int queueHighWm = kDefaultOutQueueHighWm,
      int queueLowWm = kDefaultOutQueueLowWm) {
    auto boundedAdjRibOutQ = std::make_shared<AdjRib::AdjRibOutQueueT>(
        queueCapacity, queueHighWm, queueLowWm);
    auto adjRibInQ = std::make_shared<AdjRib::AdjRibInQueueT>();
    auto versionNumber = std::make_shared<nettools::bgplib::VersionNumber>(1);

    nettools::bgplib::BgpPeerDisplayInfo displayInfo;
    displayInfo.peeringParams.peerAddr = peerId.peerAddr;
    displayInfo.peeringParams.remoteAs = kAsn1;
    displayInfo.peeringParams.localAs = kAsn1;
    displayInfo.peeringParams.globalAs = kAsn1;
    displayInfo.peeringParams.isAfiIpv4Configured = AfiIpv4Configured{true};
    displayInfo.peeringParams.isAfiIpv6Configured = AfiIpv6Configured{true};
    displayInfo.remoteBgpId = peerId.remoteBgpId;
    displayInfo.remoteAs = displayInfo.peeringParams.remoteAs;
    displayInfo.negotiatedCapabilities.mpExtV4Unicast() = true;
    displayInfo.negotiatedCapabilities.as4byte() = true;
    /*
     * MP-capable peer: advertised MP-EXT in its OPEN. Without this the thrift
     * default (false) makes buildUpdateGroupKey compute legacyV4NlriEncoding =
     * isAfiIpv4Negotiated && !remoteMpExtExist = true, splitting these peers
     * into a legacy update group and breaking group-formation expectations.
     */
    displayInfo.remoteCapabilities.mpExtExist() = true;

    auto sessionInfo = nettools::bgplib::FiberBgpPeer::getObservableSessionInfo(
        displayInfo,
        nullptr /* unbounded input queue, unused */,
        boundedAdjRibOutQ,
        adjRibInQ,
        versionNumber);

    nettools::bgplib::FiberBgpPeer::ObservableStateT stateEvt{
        .peerId = peerId,
        .state = nettools::bgplib::BgpSessionState::ESTABLISHED,
        .versionNumber = versionNumber->getWithoutLock(),
        .remoteAs = displayInfo.peeringParams.remoteAs,
        .sessionInfo = std::move(sessionInfo)};

    nettools::bgplib::ObservableEventT obsEvent = std::move(stateEvt);
    ctx.sessionMgr->getNotifyCoroQueue().push(std::move(obsEvent));

    ctx.sessionInQueues.push_back(adjRibInQ);
    ctx.sessionBoundedOutQueues.push_back(boundedAdjRibOutQ);
  }

  /*
   * Resolve the effective per-peer policies for the peers selected by `filter`
   * from `newConfig`. Normal tests apply them through PeerManager, mirroring
   * the BgpServiceBB policy RPC. White-box evaluator tests stage only the names
   * and pending flags so they can invoke reconciliation manually.
   */
  void applyResolvedPeerPolicies(
      TestContext& ctx,
      const std::shared_ptr<const Config>& newConfig,
      const std::function<bool(const folly::IPAddress&, const BgpPeerConfig&)>&
          filter) {
    auto peerToPolicyNames = resolveEffectivePeerPolicies(*newConfig, filter);
    if (!ctx.manuallyApplyPolicyNames) {
      ctx.peerMgr->updateIngressEgressPolicyNames(std::move(peerToPolicyNames));
      return;
    }

    ctx.peerMgr->getEventBase().runInEventBaseThreadAndWait(
        [&, stagedPolicyNames = std::move(peerToPolicyNames)]() {
          for (auto& [_, adjRib] : ctx.peerMgr->adjRibs_) {
            auto [ingressChanged, egressChanged] =
                ctx.peerMgr->updateIngressEgressPolicyNamesForAdjRib(
                    adjRib, *stagedPolicyNames);
            adjRib->setPendingIngressPolicyUpdate(ingressChanged);
            adjRib->setPendingEgressPolicyUpdate(egressChanged);
          }
          ctx.peerMgr->lastAppliedPolicyVersion_ =
              ctx.configMgr->getConfigVersion();
        });
  }

  /*
   * Flush the peer manager's EventBase: run a no-op to completion on the evb so
   * every task queued ahead of it has run. The setPolicy helpers below normally
   * apply names through an asyncScope coroutine, not synchronously. Without a
   * flush between calls, the first queued coroutine can stamp
   * lastAppliedPolicyVersion_ with the latest config version, causing the
   * remaining change sets to be skipped as stale.
   */
  void flushEventBase(TestContext& ctx) {
    ctx.peerMgr->getEventBase().runInEventBaseThreadAndWait([]() {});
  }

  /*
   * Install a per-peer egress policy override (peerOverride becomes true). This
   * updates the config and pushes the resolved egress name onto the AdjRib's
   * egressPolicyName_.
   */
  /*
   * Apply one egress policy to several peers in a single update, the way a
   * setPeersPolicy RPC carrying multiple peers does. Distinct from calling
   * updatePeerEgressPolicyOnEvb per peer, which is several updates and so
   * several re-evaluations -- peers then diverge one at a time instead of
   * together.
   */
  void updatePeersEgressPolicyOnEvb(
      TestContext& ctx,
      const std::vector<BgpPeerId>& peerIds,
      const std::string& newPolicyName) {
    std::map<std::string, std::map<bgp_policy::DIRECTION, std::string>>
        peersPolicy;
    for (const auto& peerId : peerIds) {
      peersPolicy[peerId.peerAddr.str()][bgp_policy::DIRECTION::OUT] =
          newPolicyName;
    }
    auto newConfig = ctx.configMgr->updatePeerPolicies(peersPolicy);
    applyResolvedPeerPolicies(
        ctx,
        newConfig,
        [&](const folly::IPAddress& peerAddr, const BgpPeerConfig&) {
          return peersPolicy.contains(peerAddr.str());
        });
    flushEventBase(ctx);
  }

  void updatePeerEgressPolicyOnEvb(
      TestContext& ctx,
      const BgpPeerId& peerId,
      const std::string& newPolicyName) {
    std::map<std::string, std::map<bgp_policy::DIRECTION, std::string>>
        peersPolicy;
    peersPolicy[peerId.peerAddr.str()][bgp_policy::DIRECTION::OUT] =
        newPolicyName;
    auto newConfig = ctx.configMgr->updatePeerPolicies(peersPolicy);
    applyResolvedPeerPolicies(
        ctx,
        newConfig,
        [&](const folly::IPAddress& peerAddr, const BgpPeerConfig&) {
          return peersPolicy.contains(peerAddr.str());
        });
    /*
     * co_setPeersPolicy applies egressPolicyName_ via an async lambda posted by
     * updateIngressEgressPolicyNames, which is version-gated. Flush the evb so
     * that apply runs (and bumps the applied version) before the next call --
     * otherwise back-to-back calls' applies coalesce under the gate and drop
     * change sets, leaving some peers' egress unapplied (a load-dependent
     * flake).
     */
    flushEventBase(ctx);
  }

  /*
   * Change a peer group's egress policy (a peer-group-level, non-override
   * change). This updates the config and pushes the resolved egress name onto
   * every member AdjRib's egressPolicyName_, leaving peerOverride false since
   * no per-peer override is installed.
   */
  void updatePeerGroupEgressPolicyOnEvb(
      TestContext& ctx,
      const std::string& peerGroupName,
      const std::string& newPolicyName) {
    std::map<std::string, std::map<bgp_policy::DIRECTION, std::string>>
        peerGroupsPolicy;
    peerGroupsPolicy[peerGroupName][bgp_policy::DIRECTION::OUT] = newPolicyName;
    auto newConfig = ctx.configMgr->updatePeerGroupPolicies(peerGroupsPolicy);
    applyResolvedPeerPolicies(
        ctx,
        newConfig,
        [&](const folly::IPAddress&, const BgpPeerConfig& peerConfig) {
          const auto& pgName = peerConfig.commonPeerGroupConfig.peerGroupName;
          return pgName.has_value() && peerGroupsPolicy.contains(*pgName);
        });
    /*
     * Flush this call's version-gated apply before the next call; see
     * updatePeerEgressPolicyOnEvb.
     */
    flushEventBase(ctx);
  }

  /*
   * Remove the per-peer egress policy override so the peer falls back to its
   * peer-group policy (peerOverride becomes false). A per-peer override is
   * installed even when the name matches the group's, so unsetting is the only
   * way to clear the override.
   */
  void unsetPeerEgressPolicyOnEvb(TestContext& ctx, const BgpPeerId& peerId) {
    std::map<std::string, std::set<bgp_policy::DIRECTION>> peersToUnset;
    peersToUnset[peerId.peerAddr.str()].insert(bgp_policy::DIRECTION::OUT);
    auto newConfig = ctx.configMgr->unsetPeerPolicies(peersToUnset);
    applyResolvedPeerPolicies(
        ctx,
        newConfig,
        [&](const folly::IPAddress& peerAddr, const BgpPeerConfig&) {
          return peersToUnset.count(peerAddr.str()) > 0;
        });
    /*
     * Flush this call's version-gated apply before the next call; see
     * updatePeerEgressPolicyOnEvb.
     */
    flushEventBase(ctx);
  }

  /*
   * Helpers for driving egress policy re-evaluation from tests
   * (processUpdateGroupsEgressPolicyReevaluation is private; this fixture is a
   * friend).
   */

  /*
   * These tests stage policy names directly and drive the evaluator by hand;
   * automatic evaluation would move peers before the scenario is complete.
   * Staging the names is what suppresses it: updateIngressEgressPolicyNames is
   * never called, so nothing schedules a re-evaluation.
   */
  void disableAsyncEgressReEvalOnEvb(TestContext& ctx) {
    ctx.manuallyApplyPolicyNames = true;
  }

  /*
   * co_setPeersPolicy / co_setPeerGroupsPolicy apply egressPolicyName_ on the
   * peer manager evb asynchronously (updateIngressEgressPolicyNames posts the
   * apply via runInEventBaseThread), so poll until it lands before rebuilding
   * the key -- otherwise the rebuild races the apply and captures a stale
   * egress for some peers.
   */
  void waitForEgressPolicyUpdatedAdjRibOnEvb(
      TestContext& ctx,
      const BgpPeerId& peerId,
      const std::string& policyName) {
    auto& evb = ctx.peerMgr->getEventBase();
    WITH_RETRIES({
      auto name = folly::via(&evb, [&]() {
                    return ctx.adjRibs.at(peerId)->egressPolicyName_;
                  }).get();
      EXPECT_EVENTUALLY_TRUE(name.has_value() && name.value() == policyName);
    });
  }

  /*
   * Apply a peer-group-level (non-override) egress change covering peerId, then
   * rebuild peerId's UpdateGroupKey. Because this changes the peer group's
   * egress (not a per-peer override), peerOverride stays false. Note this
   * changes the egress for the whole peer group; callers rebuild each member's
   * key as needed.
   */
  void setNonOverrideEgressAndRebuildKeyOnEvb(
      TestContext& ctx,
      const BgpPeerId& peerId,
      const std::string& policyName) {
    disableAsyncEgressReEvalOnEvb(ctx);
    auto& evb = ctx.peerMgr->getEventBase();
    std::string peerGroupName;
    evb.runInEventBaseThreadAndWait([&]() {
      peerGroupName =
          ctx.adjRibs.at(peerId)->peeringParams_.peerGroupName.value_or("");
    });
    updatePeerGroupEgressPolicyOnEvb(ctx, peerGroupName, policyName);
    waitForEgressPolicyUpdatedAdjRibOnEvb(ctx, peerId, policyName);
    evb.runInEventBaseThreadAndWait(
        [&]() { ctx.adjRibs.at(peerId)->buildAndSetUpdateGroupKey(); });
  }

  /*
   * Make a peer a per-peer egress override (peerOverride=true) for policyName,
   * then rebuild its UpdateGroupKey.
   */
  void setOverrideEgressAndRebuildKeyOnEvb(
      TestContext& ctx,
      const BgpPeerId& peerId,
      const std::string& policyName) {
    disableAsyncEgressReEvalOnEvb(ctx);
    updatePeerEgressPolicyOnEvb(ctx, peerId, policyName);
    waitForEgressPolicyUpdatedAdjRibOnEvb(ctx, peerId, policyName);
    auto& evb = ctx.peerMgr->getEventBase();
    evb.runInEventBaseThreadAndWait(
        [&]() { ctx.adjRibs.at(peerId)->buildAndSetUpdateGroupKey(); });
  }

  /*
   * Drive the full processUpdateGroupsEgressPolicyReevaluation coroutine on the
   * evb: rebuild affected peers' keys, rekey affected groups, move override
   * members out, and destroy any old group emptied by the moves.
   */
  void runProcessUpdateGroupsEgressPolicyReevaluationOnEvb(TestContext& ctx) {
    auto& evb = ctx.peerMgr->getEventBase();
    folly::coro::blockingWait(co_withExecutor(
        &evb, ctx.peerMgr->processUpdateGroupsEgressPolicyReevaluation()));
  }

  // Whether the update group manager still tracks a group for the given key.
  bool hasUpdateGroupOnEvb(TestContext& ctx, const UpdateGroupKey& key) {
    auto& evb = ctx.peerMgr->getEventBase();
    return folly::via(
               &evb,
               [&]() {
                 return ctx.peerMgr->updateGroupManager_->hasGroup(key);
               })
        .get();
  }

  /*
   * Mark the given peers as needing egress policy re-evaluation; the affected-
   * peer scan in processUpdateGroupsEgressPolicyReevaluation keys off this
   * flag. Set it explicitly because each policy RPC's apply ASSIGNS the flag
   * for every peer from that RPC's change set, so it does not accumulate across
   * the several RPCs a multi-group scenario issues -- the config/keys are
   * correct, only the dirty flags need to be (re)asserted before a manual
   * reeval.
   */
  void markEgressPolicyUpdateRequiredOnEvb(
      TestContext& ctx,
      const std::vector<BgpPeerId>& peers) {
    auto& evb = ctx.peerMgr->getEventBase();
    evb.runInEventBaseThreadAndWait([&]() {
      for (const auto& id : peers) {
        ctx.adjRibs.at(id)->setPendingEgressPolicyUpdate(true);
      }
    });
  }

  /*
   * Re-align every peer's stored UpdateGroupKey to its current group's key.
   * A test that diverges a peer's key from its group (e.g. installs an override
   * but does not move the peer) must call this before tearDown -- otherwise
   * shutdown's maybeDestroyUpdateGroup keys off the stale key, never finds the
   * group, and leaks it (and its AdjRibPolicyCache references).
   */
  void realignPeerKeysToGroupsOnEvb(TestContext& ctx) {
    auto& evb = ctx.peerMgr->getEventBase();
    evb.runInEventBaseThreadAndWait([&]() {
      for (auto& [peerId, adjRib] : ctx.adjRibs) {
        if (auto group = adjRib->getUpdateGroup()) {
          adjRib->updateGroupKey_ = group->getGroupKey();
        }
      }
    });
  }

  /*
   * Retarget a group and all its members to a new egress policy without going
   * through config (so the production async re-evaluation is NOT scheduled).
   * The group walk reads the policy from the group key, and each detached
   * peer's inline dump reads it from its own egressPolicyName_, so set both.
   * Marks each member pending egress update. This sets up the precondition that
   * rekeyAffectedUpdateGroups would establish before
   * processGroupEgressPolicyReEvaluation runs.
   */
  void changeGroupEgressPolicyOnEvb(
      TestContext& ctx,
      const std::shared_ptr<AdjRibOutGroup>& group,
      const std::string& policyName) {
    auto& evb = ctx.peerMgr->getEventBase();
    evb.runInEventBaseThreadAndWait([&]() {
      folly::F14FastMap<bgp_policy::DIRECTION, std::optional<std::string>> m;
      m[bgp_policy::DIRECTION::OUT] = policyName;
      std::optional<UpdateGroupKey> newKey;
      for (const auto& [_, adjRib] : group->getBitToAdjRibs()) {
        /*
         * updateIngressEgressPolicyNames only updates the names and reports
         * what changed; the caller owns the pending flag (as PeerManager does).
         */
        auto [_ingressChanged, egressChanged] =
            adjRib->updateIngressEgressPolicyNames(m);
        adjRib->setPendingEgressPolicyUpdate(egressChanged);
        adjRib->buildAndSetUpdateGroupKey();
        newKey = adjRib->getUpdateGroupKey();
      }
      if (newKey.has_value()) {
        /*
         * Rekey via the manager so its map entry tracks the new key too; a bare
         * setGroupKey would leave the map keyed under the old key and prevent
         * teardown from destroying the group (leaking its policy cache).
         */
        ctx.peerMgr->updateGroupManager_->rekeyGroup(group, *newKey);
      }
    });
  }

  /*
   * Run PeerManager::processGroupEgressPolicyReEvaluation (private) to
   * completion, blocking the test thread while it runs on the evb.
   */
  void processGroupEgressPolicyReEvaluationOnEvb(
      TestContext& ctx,
      const std::shared_ptr<AdjRibOutGroup>& group) {
    auto& evb = ctx.peerMgr->getEventBase();
    evb.runInEventBaseThreadAndWait(
        [&]() { ctx.peerMgr->processGroupEgressPolicyReEvaluation(group); });
  }

  /*
   * Run PeerManager::processDetachedPeerEgressPolicyReEvaluation (private) for
   * a single peer to completion on the evb.
   */
  void processDetachedPeerEgressPolicyReEvaluationOnEvb(
      TestContext& ctx,
      const std::shared_ptr<AdjRib>& adjRib) {
    auto& evb = ctx.peerMgr->getEventBase();
    evb.runInEventBaseThreadAndWait([&]() {
      ctx.peerMgr->processDetachedPeerEgressPolicyReEvaluation(adjRib);
    });
  }

  /*
   * Drain all peers and poll until every change-list consumer for the group has
   * reached the end of the change list (null marker): the group consumer (which
   * serves the in-sync peers) and each detached peer's own consumer. Draining
   * unblocks any peer whose full out-queue was stalling its consumer.
   */
  void expectPeersAtEndOfChangeList(
      TestContext& ctx,
      const std::shared_ptr<AdjRibOutGroup>& group) {
    auto& evb = ctx.peerMgr->getEventBase();
    WITH_RETRIES({
      for (auto& kv : ctx.adjRibs) {
        drainQueue(ctx, kv.first);
      }
      auto atEnd =
          folly::via(&evb, [&]() {
            auto groupConsumer = group->getChangeListConsumer();
            if (groupConsumer && groupConsumer->getMarker() != nullptr) {
              return false;
            }
            for (const auto& [_, adjRib] : group->getBitToAdjRibs()) {
              if (adjRib->isDetachedPeer()) {
                auto consumer = adjRib->getDetachedConsumer();
                if (consumer && consumer->getMarker() != nullptr) {
                  return false;
                }
              }
            }
            return true;
          }).get();
      EXPECT_EVENTUALLY_TRUE(atEnd);
    });
  }

  static std::function<bool(const AdjRibEntry&, const folly::CIDRNetwork&)>
  verifyAdvertised() {
    return [](const AdjRibEntry& entry, const folly::CIDRNetwork& prefix) {
      EXPECT_NE(entry.getPostAttr(), nullptr)
          << prefix.first.str() << "/" << prefix.second
          << " was not advertised";
      return entry.getPostAttr() != nullptr;
    };
  }

  static std::function<bool(const AdjRibEntry&, const folly::CIDRNetwork&)>
  verifyNotAdvertised() {
    return [](const AdjRibEntry& entry, const folly::CIDRNetwork& prefix) {
      EXPECT_EQ(entry.getPostAttr(), nullptr)
          << prefix.first.str() << "/" << prefix.second
          << " should be withdrawn or not advertised";
      return entry.getPostAttr() == nullptr;
    };
  }

  static std::function<bool(const AdjRibEntry&, const folly::CIDRNetwork&)>
  verifyCommOnAdvertisedRoute(const std::string& community) {
    auto expectedComm =
        *nettools::bgplib::BgpAttrCommunityC::createBgpAttrCommunity(community);
    return [expectedComm](
               const AdjRibEntry& entry,
               const folly::CIDRNetwork& prefix) -> bool {
      auto postPolicy = entry.getPostAttr();
      EXPECT_NE(postPolicy, nullptr) << prefix.first.str() << "/"
                                     << prefix.second << " was not advertised";
      if (!postPolicy) {
        return false;
      }
      const auto& communities = postPolicy->getCommunities();
      bool found = false;
      for (const auto& comm : communities.get()) {
        if (comm == expectedComm) {
          found = true;
          break;
        }
      }
      EXPECT_TRUE(found) << prefix.first.str() << "/" << prefix.second
                         << " missing expected community";
      return found;
    };
  }

  /*
   * Verify a peer's RIB-OUT entries in the group's LiteTree.
   * Returns the count of entries matching routeIndexPredicate.
   * Checks the peer's owner key first; if not found, falls back to the
   * group owner key for shared entries via isEntryNotShared.
   *
   * verifyOnRouteIndexFunc is called on each matched entry for custom
   * verification (e.g., verifyAdvertised, verifyCommOnAdvertisedRoute).
   *
   * Uses folly::via to run on the evb and return the count via Future.
   */
  size_t verifyRibOutEntries(
      TestContext& ctx,
      const BgpPeerId& peerId,
      std::function<bool(int)> routeIndexPredicate = [](int) { return true; },
      std::function<bool(const AdjRibEntry&, const folly::CIDRNetwork&)>
          verifyOnRouteIndexFunc =
              [](const AdjRibEntry&, const folly::CIDRNetwork&) {
                return true;
              }) {
    auto& evb = ctx.peerMgr->getEventBase();

    return folly::via(
               &evb,
               [&]() -> size_t {
                 auto& adjRib = ctx.adjRibs.at(peerId);
                 auto group = adjRib->getUpdateGroup();
                 auto peerOwnerKey = adjRib->getPeerOwnerKey();
                 auto groupOwnerKey = group->getGroupOwnerKey();
                 bool isDetached = adjRib->isDetachedPeer();
                 auto detachedRibVersion = adjRib->getDetachedRibVersion();

                 size_t count = 0;
                 for (auto it = group->LiteTree_.begin();
                      it != group->LiteTree_.end();
                      ++it) {
                   auto& ownerMap = it->value();
                   AdjRibEntry* entry = nullptr;

                   auto peerIt = ownerMap.find(peerOwnerKey);
                   if (peerIt != ownerMap.end()) {
                     entry = peerIt->second.get();
                   } else {
                     auto groupIt = ownerMap.find(groupOwnerKey);
                     if (groupIt != ownerMap.end() &&
                         (!isDetached ||
                          AdjRibOutGroup::isEntryShared(
                              /*peerHasOwnEntry=*/false,
                              detachedRibVersion,
                              groupIt->second->getRibVersion()))) {
                       entry = groupIt->second.get();
                     }
                   }

                   if (!entry) {
                     continue;
                   }

                   int routeIndexOctet = it.ipAddress().asV4().getNthMSByte(2);
                   if (!routeIndexPredicate(routeIndexOctet)) {
                     continue;
                   }
                   folly::CIDRNetwork prefix{it.ipAddress(), it.masklen()};
                   if (verifyOnRouteIndexFunc(*entry, prefix)) {
                     ++count;
                   }
                 }
                 return count;
               })
        .get();
  }

  /*
   * Verify a peer's RIB-OUT matches what its group's egress policy should
   * advertise over the 100 setup routes (100.0.i.0/24; odd i carry the
   * no-advertise community, i % 3 == 0 carry the modify community):
   *   - kPNameMatchNoAdvtDeny: the 50 even routes advertised, the 50 odd
   *     (no-advertise) routes denied;
   *   - kPNameMatchModifyAppend: all 100 advertised, the 34 modify routes carry
   *     the appended kPCommAppend;
   *   - kPNamePermitAll: all 100 advertised, unmodified.
   */
  void expectRibOutForPolicy(
      TestContext& ctx,
      const BgpPeerId& peerId,
      const std::string& policyName) {
    if (policyName == kPNameMatchNoAdvtDeny) {
      EXPECT_EQ(
          verifyRibOutEntries(
              ctx,
              peerId,
              [](int i) { return i % 2 == 0; },
              verifyAdvertised()),
          50u);
      EXPECT_EQ(
          verifyRibOutEntries(
              ctx,
              peerId,
              [](int i) { return i % 2 == 1; },
              verifyNotAdvertised()),
          50u);
    } else if (policyName == kPNameMatchModifyAppend) {
      EXPECT_EQ(
          verifyRibOutEntries(
              ctx, peerId, [](int) { return true; }, verifyAdvertised()),
          100u);
      EXPECT_EQ(
          verifyRibOutEntries(
              ctx,
              peerId,
              [](int i) { return i % 3 == 0; },
              verifyCommOnAdvertisedRoute(kPCommAppend)),
          34u);
    } else if (policyName == kPNamePermitAll) {
      EXPECT_EQ(
          verifyRibOutEntries(
              ctx, peerId, [](int) { return true; }, verifyAdvertised()),
          100u);
    } else {
      FAIL() << "expectRibOutForPolicy: unknown policy " << policyName;
    }
  }

  /*
   * Serialize a group's RIB-OUT for a single owner so two snapshots can be
   * compared for exact equality (e.g. before/after splitToNewGroup). Produces a
   * deterministic string of comma-separated tokens:
   *   "Path{G_pfx_h, p_keyhash_pfx_h, ...}, Lite{G_pfx_h, p_keyhash_pfx_h}"
   * Group-owned (shared) entries are tagged "G"; entries owned by `ownerKey`
   * are tagged "p_<ownerKeyHash>"; entries belonging to any other owner are
   * ignored. "h" is a single hash over the AdjRibEntry's content (pathId, rib
   * version, flags, pre/post attribute hashes, post-policy name).
   * lastUpdateRcvdUsec is intentionally excluded -- it is not carried by
   * copyEntryForOwner and is not part of the advertised RIB-OUT. Order is
   * deterministic: prefixes in radix order, group entries before owner entries,
   * path ids ascending.
   *
   * MUST be called on the group's EventBase thread (it reads the RIB-OUT
   * trees).
   */
  std::string serializeSharedRibOutTreesGroupKeyNormalized(
      const std::shared_ptr<AdjRibOutGroup>& group,
      const AdjRibOutOwnerKey& ownerKey) {
    const auto groupOwnerKey = group->getGroupOwnerKey();
    const std::string groupTag = "G";
    const std::string ownerTag = fmt::format("p_{}", ownerKey.hash());

    auto entryHash = [](uint32_t pathId, const AdjRibEntry& e) {
      return folly::hash::hash_combine(
          pathId,
          e.getRibVersion(),
          e.isStale(),
          e.isNexthopSetByPolicy(),
          e.getPreOut() ? e.getPreOut()->hash() : 0,
          e.getPostAttr() ? e.getPostAttr()->hash() : 0,
          e.getPostOutPolicy() ? *e.getPostOutPolicy() : std::string{});
    };

    auto join = [](const std::vector<std::string>& tokens) {
      std::string out;
      for (size_t i = 0; i < tokens.size(); ++i) {
        if (i) {
          out += ", ";
        }
        out += tokens[i];
      }
      return out;
    };

    // Lite tree: at most one entry per (prefix, owner).
    std::vector<std::string> liteTokens;
    for (auto it = group->LiteTree_.begin(); it != group->LiteTree_.end();
         ++it) {
      const auto& ownerMap = it->value();
      auto pfx = fmt::format("{}/{}", it.ipAddress().str(), it.masklen());
      if (auto g = ownerMap.find(groupOwnerKey); g != ownerMap.end()) {
        liteTokens.push_back(
            fmt::format(
                "{}_{}_{}",
                groupTag,
                pfx,
                entryHash(kDefaultPathID, *g->second)));
      }
      if (auto p = ownerMap.find(ownerKey); p != ownerMap.end()) {
        liteTokens.push_back(
            fmt::format(
                "{}_{}_{}",
                ownerTag,
                pfx,
                entryHash(kDefaultPathID, *p->second)));
      }
    }

    // Path tree: multiple paths per (prefix, owner), keyed by pathId.
    std::vector<std::string> pathTokens;
    for (auto it = group->PathTree_.begin(); it != group->PathTree_.end();
         ++it) {
      const auto& ownerMap = it->value();
      auto pfx = fmt::format("{}/{}", it.ipAddress().str(), it.masklen());
      auto emitOwner = [&](const std::string& tag,
                           const AdjRibOutOwnerKey& key) {
        auto oit = ownerMap.find(key);
        if (oit == ownerMap.end()) {
          return;
        }
        // Order path ids ascending for a stable, comparable string.
        std::map<uint32_t, const AdjRibEntry*> byPathId;
        for (const auto& [pathId, entry] : oit->second) {
          byPathId.emplace(pathId, entry.get());
        }
        for (const auto& [pathId, entry] : byPathId) {
          pathTokens.push_back(
              fmt::format("{}_{}_{}", tag, pfx, entryHash(pathId, *entry)));
        }
      };
      emitOwner(groupTag, groupOwnerKey);
      emitOwner(ownerTag, ownerKey);
    }

    return fmt::format(
        "Path{{{}}}, Lite{{{}}}", join(pathTokens), join(liteTokens));
  }

  /*
   * Generate a BgpPeerId from an index. Peer addresses start at 10.1.0.1.
   */
  static BgpPeerId makePeerId(int index) {
    auto addr = folly::IPAddress(
        fmt::format("10.1.{}.{}", index / 255, index % 255 + 1));
    return BgpPeerId(addr, addr.asV4().toLongHBO());
  }

  /*
   * Create the full test context:
   *   - PeerManager with update groups + policies
   *   - numPeers AdjRibs in the same group
   *   - 100 routes in shadowRibEntries_
   */
  /*
   * Multi-peer-group setup. Each entry in groupSpecs is a (peerGroupName,
   * peerCount); peers get sequential global indices across all groups (group 0
   * -> [0, c0), group 1 -> [c0, c0+c1), ...), matching makePeerId(index). Every
   * peer group starts with egressPolicy, so each becomes its own update group.
   */
  TestContext setUpGroups(
      const std::vector<std::pair<std::string, int>>& groupSpecs,
      bool initialDumpCompleted = true,
      const std::string& egressPolicy = kPNameMatchNoAdvtDeny,
      const std::map<int, std::string>& perPeerEgressOverride = {}) {
    auto policies = buildPolicies();

    auto config = getConfig(
        /*includeStaticPeer=*/true,
        /*includeDynamicShivPeer=*/true,
        /*includeDynamicMonitorPeer=*/false,
        /*includeDynamicVipInjectorPeer=*/false,
        /*enableStatefulHa=*/false,
        /*enableVipServer=*/true,
        /*eorTimeS=*/1,
        /*enableSubscriberLimit=*/false,
        /*enableSwitchLimit=*/false,
        /*applyGoldenPrefixPolicy=*/false,
        /*bgpFeatures=*/{},
        /*enableDynamicPolicyEvaluation=*/false,
        /*enableUpdateGroup=*/true);

    auto thriftConfig = config->getConfig();

    // Flat (globalPeerIndex, peerGroupName) list used to build AdjRibs below.
    std::vector<std::pair<int, std::string>> peerAssignments;
    int nextPeerIndex = 0;
    for (const auto& [peerGroupName, peerCount] : groupSpecs) {
      thrift::PeerGroup peerGroup;
      peerGroup.name() = peerGroupName;
      peerGroup.egress_policy_name() = egressPolicy;
      thriftConfig.peer_groups()->push_back(std::move(peerGroup));
      for (int i = 0; i < peerCount; ++i) {
        int idx = nextPeerIndex++;
        auto addr = makePeerId(idx).peerAddr;
        thrift::BgpPeer peer;
        peer.peer_addr() = addr.str();
        peer.local_addr() = kLocalAddr1.str();
        peer.next_hop4() = kV4Nexthop1.str();
        peer.next_hop6() = kV6Nexthop1.str();
        peer.remote_as_4_byte() = kAsn1;
        peer.peer_group_name() = peerGroupName;
        auto ovrIt = perPeerEgressOverride.find(idx);
        if (ovrIt != perPeerEgressOverride.end()) {
          peer.egress_policy_name() = ovrIt->second;
        }
        thriftConfig.peers()->push_back(std::move(peer));
        peerAssignments.emplace_back(idx, peerGroupName);
      }
    }

    /*
     * These tests drive peers directly and never accept inbound BGP
     * connections. Disable the passive server socket via tunables so the
     * SessionManager does not bind the configured listen port -- otherwise
     * FiberBgpPeerManager::run() aborts the SessionManager thread when that
     * port is unavailable. All other tunables keep their defaults.
     */
    BgpSettings tunables(
        ValidateRemoteAs{true},
        SupportStatefulGr{true},
        EnableServerSocket{false},
        AllowLoopbackReflection{false});
    config = std::make_shared<Config>(std::move(thriftConfig), tunables);

    auto configManager = std::make_shared<ConfigManager>(config);
    auto globalConfig = config->getBgpGlobalConfig();
    auto policyManager =
        std::make_shared<PolicyManager>(policies, globalConfig.get());
    auto peerMgr = std::make_shared<PeerManagerBase>(
        configManager, policyManager, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

    auto sessionMgr = std::make_shared<SessionManager>(*globalConfig, false);
    peerMgr->setSessionManager(sessionMgr);

    auto& evb = peerMgr->getEventBase();

    folly::F14FastMap<BgpPeerId, std::shared_ptr<AdjRib>> adjRibs;
    for (const auto& [idx, peerGroupName] : peerAssignments) {
      auto peerId = makePeerId(idx);
      auto baton = std::make_shared<folly::coro::Baton>();
      peerMgr->sessionTerminateBatons_.insert_or_assign(peerId, baton);
      auto adjRib = setupAdjRibWithPeerGroup(
          evb,
          peerId,
          AsNum(kAsn1),
          baton,
          configManager,
          policyManager,
          fmt::format("adjRib{}", idx),
          peerGroupName);
      /*
       * Post immediately: in production the baton is posted when the
       * AdjRib's processing loop finishes. Since we never start that
       * loop for manually created peers, we post it here so that
       * sessionEstablished's waitForSessionTerminateBaton doesn't hang.
       */
      baton->post();
      adjRib->setInInitialAnnouncement();
      adjRib->buildAndSetUpdateGroupKey();

      auto group = peerMgr->updateGroupManager_->findOrCreateGroup(
          adjRib->getUpdateGroupKey());
      adjRib->adjRibOutGroup_ = group;
      group->registerPeer(adjRib);
      peerMgr->adjRibs_[peerId] = adjRib;
      adjRibs[peerId] = std::move(adjRib);
    }

    /*
     * Wire each group's change list tracker/bitmaps. The group consumer is
     * registered later by the initial dump (buildAndScheduleSendInitialDump-
     * FromShadowRib), so we don't register it here to avoid
     * double-registration.
     */
    auto changeListTracker = peerMgr->getChangeListTracker();
    for (auto& [key, group] : peerMgr->updateGroupManager_->getAllGroups()) {
      group->setChangeListTracker(
          changeListTracker,
          peerMgr->addPathConsumerBitmap_,
          peerMgr->nonAddPathConsumerBitmap_);
    }

    peerMgr->ribInitialAnnouncementStarted_ = true;
    peerMgr->ribInitialAnnouncementDone_ = true;
    peerMgr->ribInitPathComputationNotified_ = true;
    peerMgr->initialized_ = true;

    auto peerMgrThread = peerMgr->runInThread();
    auto sessionMgrThread = sessionMgr->runInThread();

    TestContext ctx{
        .peerMgr = peerMgr,
        .sessionMgr = sessionMgr,
        .configMgr = configManager,
        .adjRibs = std::move(adjRibs),
        .peerMgrThread = std::move(peerMgrThread),
        .sessionMgrThread = std::move(sessionMgrThread)};

    if (initialDumpCompleted) {
      publishRouteUpdates(ctx);
    }

    return ctx;
  }

  TestContext setUp(int numPeers = 2, bool initialDumpCompleted = true) {
    return setUpGroups({{"PEERGROUP_A", numPeers}}, initialDumpCompleted);
  }

  void sendInitialRibDump(TestContext& ctx) {
    auto& evb = ctx.peerMgr->getEventBase();
    // Snapshot the group pointers on the evb thread for safe map access.
    std::vector<std::shared_ptr<AdjRibOutGroup>> groups;
    evb.runInEventBaseThreadAndWait([&]() {
      for (auto& [key, group] :
           ctx.peerMgr->updateGroupManager_->getAllGroups()) {
        groups.push_back(group);
      }
    });
    /*
     * Drive each dump Task on the evb but block the *test* thread (not the evb
     * loop) for completion, so the evb stays free to make progress while the
     * Task awaits scope/timer work bound to it.
     */
    for (const auto& group : groups) {
      folly::coro::blockingWait(co_withExecutor(
          &evb, group->buildAndScheduleSendInitialDumpFromShadowRib()));
    }
  }

  void tearDown(TestContext& ctx) {
    XLOG(INFO, "START tearDown");
    auto& evb = ctx.peerMgr->getEventBase();

    evb.runInEventBaseThreadAndWait([&]() {
      for (auto& [peerId, baton] : ctx.peerMgr->sessionTerminateBatons_) {
        if (baton && !baton->ready()) {
          baton->post();
        }
      }

      /*
       * Cancel async scopes on all AdjRibs so blocked coroutines
       * (e.g. deferredPushToPeer waiting on waitToPush) are cancelled.
       */
      for (auto& [peerId, adjRib] : ctx.adjRibs) {
        if (adjRib->asyncScope_) {
          adjRib->asyncScope_->requestCancellation();
        }
      }
    });

    // Drain and close all peer queues.
    for (auto& [peerId, adjRib] : ctx.adjRibs) {
      drainQueue(ctx, peerId);
      auto queue = adjRib->adjRibOutQueue_;
      if (queue) {
        queue->close();
      }
    }

    for (auto& inQ : ctx.sessionInQueues) {
      if (inQ) {
        inQ->fiberPush(nettools::bgplib::FiberBgpPeer::BgpSessionStop{});
      }
    }
    for (auto& outQ : ctx.sessionBoundedOutQueues) {
      if (outQ) {
        outQ->close();
      }
    }

    ctx.peerMgr->stop();
    ctx.sessionMgr->stop();
    ctx.peerMgrThread.join();
    ctx.sessionMgrThread.join();

    XLOG(INFO, "END tearDown");
  }
};

} // namespace facebook::bgp
