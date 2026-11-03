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
 * Common definitions for Update Group Slow Peer E2E tests.
 * Provides SlowPeerTestBase fixture with helper methods for:
 *   - Accessing update group and peer state
 *   - Configuring slow peer detection thresholds
 *   - Triggering and verifying detachment
 *   - Verifying invariants (syncBitmap, detachedPeers, etc.)
 *
 * Lazy clone case definitions (shouldCloneForPeer):
 *   Case 1: per-peer entry already exists — no clone needed
 *   Case 2: ribVersion==0, peer never saw this prefix — no clone
 *   Case 3: ribVersion > divergenceRibVersion — already cloned, no re-clone
 *   Case 4: peer was sharing group entry — clone before mutation
 *
 * Test plan:
 * https://docs.google.com/document/d/11lBp_Q_i6UYocI3meYbI3sUShZzZsu6Qlq8iSCdVXRc
 */

#include <folly/futures/Future.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>
#include <chrono>

#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibGroup.h"
#include "neteng/fboss/bgp/cpp/common/Types.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/UpdateGroupDistributionTestCommon.h"

namespace facebook {
namespace bgp {

/*
 * Base test fixture for slow peer E2E tests.
 * Extends UpdateGroupDistributionTestBase with slow peer specific helpers.
 *
 * Key design: All slow peer tests use tiny queue sizes to trigger
 * backpressure quickly. Different communities on each route force
 * separate BGP UPDATE messages (same attrs get packed together).
 */
class SlowPeerTestBase : public UpdateGroupDistributionTestBase {
 protected:
  /*
   * Standard setup with tiny queues for slow peer testing.
   * Queue (3, 2, 0): capacity=3, highWm=2, lowWm=0
   * This allows initial dump + EoRs (dual-stack sends 2 EoRs),
   * then blocks on the next 2 route UPDATEs.
   */
  void setupSlowPeerComponents(
      int queueCapacity = 3,
      int queueHighWm = 2,
      int queueLowWm = 0) {
    setDefaultQueueSizes(queueCapacity, queueHighWm, queueLowWm);
    setupComponents();
  }

  /*
   * Get AdjRib for a peer using E2ETestFixture::getAdjRibByAddr.
   */
  std::shared_ptr<AdjRib> getAdjRib(const folly::IPAddress& peerAddr) {
    return getAdjRibByAddr(peerAddr);
  }

  /*
   * Get the update group for a peer.
   *
   * Reads adjRibOutGroup_ on the PeerManagerBase event base (inline when
   * already on it) so the read is serialized with evb writers such as the
   * update-group split in processUpdateGroupsEgressPolicyReevaluation. An
   * off-evb read of the shared_ptr races that write under TSan.
   */
  std::shared_ptr<AdjRibOutGroup> getUpdateGroupForPeer(
      const folly::IPAddress& peerAddr) {
    auto read = [this, &peerAddr]() -> std::shared_ptr<AdjRibOutGroup> {
      auto adjRib = getAdjRib(peerAddr);
      return adjRib ? adjRib->getUpdateGroup() : nullptr;
    };
    if (!peerManager_) {
      return read();
    }
    std::shared_ptr<AdjRibOutGroup> group;
    peerManager_->getEventBase().runImmediatelyOrRunInEventBaseThreadAndWait(
        [&]() { group = read(); });
    return group;
  }

  /*
   * Get peer update state. Returns DOWN if peer not found.
   *
   * Hops to the PeerManagerBase event base before reading adjRib state so
   * the read is serialized with evb writers (e.g. markPeerUnblocked in the
   * deferredPushToPeer SCOPE_EXIT). Without this hop, a test-thread read
   * races those writes under TSan.
   */
  PeerUpdateState getPeerState(const folly::IPAddress& peerAddr) {
    if (!peerManager_) {
      return PeerUpdateState::DOWN;
    }
    auto& evb = peerManager_->getEventBase();
    return folly::via(
               &evb,
               [this, peerAddr]() -> PeerUpdateState {
                 auto adjRib = getAdjRib(peerAddr);
                 if (!adjRib) {
                   return PeerUpdateState::DOWN;
                 }
                 return adjRib->getPeerState();
               })
        .get();
  }

  /*
   * Configure slow peer detection thresholds for the group.
   */
  void setSlowPeerThresholds(
      const folly::IPAddress& peerAddr,
      std::chrono::milliseconds timeThreshold,
      uint32_t countThreshold,
      std::chrono::milliseconds countWindow) {
    auto group = getUpdateGroupForPeer(peerAddr);
    ASSERT_NE(group, nullptr) << "Peer has no update group";
    UpdateGroupConfig cfg;
    cfg.enableSlowPeerBlockCountDetection = true;
    cfg.allowSlowPeerDetach = true;
    cfg.slowPeerTimeThreshold = timeThreshold;
    cfg.slowPeerBlockCountThreshold = countThreshold;
    cfg.slowPeerBlockCountWindow = countWindow;
    group->setUpdateGroupConfigForTesting(cfg);
  }

  /*
   * Wait for a peer to reach a specific PeerUpdateState.
   * Pumps event loops between retries.
   */
  bool waitForPeerState(
      const folly::IPAddress& peerAddr,
      PeerUpdateState targetState,
      int maxRetries = 50) {
    if (!peerManager_) {
      return false;
    }
    auto& evb = peerManager_->getEventBase();

    PeerUpdateState lastState = PeerUpdateState::DOWN;
    WITH_RETRIES_N(maxRetries, {
      lastState = folly::via(&evb, [this, peerAddr]() -> PeerUpdateState {
                    auto adjRib = getAdjRibByAddr(peerAddr);
                    if (!adjRib) {
                      return PeerUpdateState::DOWN;
                    }
                    return adjRib->getPeerState();
                  }).get();

      EXPECT_EVENTUALLY_EQ(lastState, targetState);
    });
    return lastState == targetState;
  }

  /*
   * Wait for a peer to reach any of the specified PeerUpdateStates.
   * Pumps event loops between retries.
   */
  bool waitForPeerStateAny(
      const folly::IPAddress& peerAddr,
      std::initializer_list<PeerUpdateState> validStates,
      int maxRetries = 50) {
    if (!peerManager_) {
      return false;
    }
    auto& evb = peerManager_->getEventBase();

    WITH_RETRIES_N(maxRetries, {
      auto state = folly::via(&evb, [this, peerAddr]() -> PeerUpdateState {
                     auto adjRib = getAdjRibByAddr(peerAddr);
                     if (!adjRib) {
                       return PeerUpdateState::DOWN;
                     }
                     return adjRib->getPeerState();
                   }).get();

      bool matched = false;
      for (auto s : validStates) {
        if (state == s) {
          matched = true;
          break;
        }
      }
      EXPECT_EVENTUALLY_TRUE(matched)
          << "Expected one of valid states, got " << static_cast<int>(state);
    });
    return true;
  }

  /*
   * Drive a detached peer through recovery to DETACHED_READY_TO_JOIN while
   * DRJ acceptance is deferred via testOnlyDeferDrjAcceptance.
   *
   * Precondition: peer is in DETACHED_BLOCKED state. Caller is responsible
   * for having already configured DRJ acceptance deferral (the helper
   * sets/clears it internally — caller should NOT call
   * testOnlyDeferDrjAcceptance before invoking this).
   *
   * Pattern encapsulated:
   *   1) defer DRJ acceptance
   *   2) unblock peer
   *   3) drain peer's queue repeatedly while yielding to evb (prevents
   *      CL-consumer backpressure wedging the peer in DB)
   *   4) assert DRJ reached
   *   5) release DRJ acceptance deferral
   *
   * This consolidates the ~12-line drain-loop pattern that previously
   * existed in 32 call sites across 14 test files (see D101122490).
   * Using folly::futures::sleep yields the test thread to the folly
   * timekeeper rather than holding a kernel sleep — partially addressing
   * the "avoid main-thread sleep" lint/review feedback (a true
   * condition-variable-based wait would require production-side signaling
   * infrastructure that does not yet exist).
   */
  void waitForDrjWithDrain(
      const folly::IPAddress& peerAddr,
      const BgpPeerId& peerId) {
    testOnlyDeferDrjAcceptance(peerAddr, true);
    unblockPeer(peerAddr);
    for (int i = 0; i < 20; ++i) {
      if (getPeerState(peerAddr) == PeerUpdateState::DETACHED_READY_TO_JOIN) {
        break;
      }
      drainPeerQueueCompletely(peerId, 1, 100);
      peerManager_->getEventBase().runInEventBaseThreadAndWait([]() {});
      folly::futures::sleep(std::chrono::milliseconds(100)).get();
    }
    drainPeerQueueCompletely(peerId, 1, 100);
    ASSERT_TRUE(
        waitForPeerState(peerAddr, PeerUpdateState::DETACHED_READY_TO_JOIN));
    testOnlyDeferDrjAcceptance(peerAddr, false);
  }

  /*
   * Check if a peer is detached from its update group.
   *
   * Hops to the PeerManagerBase event base before walking detachedPeers — the
   * collection is mutated on the evb by detach/accept paths, so an
   * off-evb walk races those mutations.
   */
  bool isPeerDetached(const folly::IPAddress& peerAddr) {
    if (!peerManager_) {
      return false;
    }
    auto& evb = peerManager_->getEventBase();
    return folly::via(
               &evb,
               [this, peerAddr]() -> bool {
                 auto adjRib = getAdjRib(peerAddr);
                 if (!adjRib) {
                   return false;
                 }
                 auto group = adjRib->getUpdateGroup();
                 if (!group) {
                   return false;
                 }
                 const auto& detachedPeers = group->getDetachedPeers();
                 for (const auto& detachedRib : detachedPeers) {
                   if (detachedRib.get() == adjRib.get()) {
                     return true;
                   }
                 }
                 return false;
               })
        .get();
  }

  /*
   * Check if a peer is in sync with its update group.
   *
   * Hops to the PeerManagerBase event base before reading the sync bitmap —
   * the bitmap is mutated on the evb by detach/accept/group-state paths.
   */
  bool isPeerInSync(const folly::IPAddress& peerAddr) {
    if (!peerManager_) {
      return false;
    }
    auto& evb = peerManager_->getEventBase();
    return folly::via(
               &evb,
               [this, peerAddr]() -> bool {
                 auto adjRib = getAdjRib(peerAddr);
                 if (!adjRib) {
                   return false;
                 }
                 auto group = adjRib->getUpdateGroup();
                 if (!group) {
                   return false;
                 }
                 return group->isPeerInSync(adjRib->getGroupBitPosition());
               })
        .get();
  }

  /*
   * Get member count for the update group of a peer.
   */
  size_t getGroupMemberCount(const folly::IPAddress& peerAddr) {
    if (!peerManager_) {
      return 0;
    }
    auto& evb = peerManager_->getEventBase();
    return folly::via(
               &evb,
               [this, peerAddr]() {
                 auto group = getUpdateGroupForPeer(peerAddr);
                 return group ? group->getMemberCount() : 0;
               })
        .get();
  }

  /*
   * Get update group state for a peer's group.
   *
   * Hops to the PeerManagerBase event base before reading state_ — the group
   * state machine is mutated on the evb (e.g. the change-list consume timer's
   * transition to WAITING/IDLE and
   * tryRejoinDetachedPeersOnAllChangesProcessed), so an off-evb read races
   * those writes under TSan.
   */
  UpdateGroupState getGroupState(const folly::IPAddress& peerAddr) {
    if (!peerManager_) {
      return UpdateGroupState::UNINITIALIZED;
    }
    auto& evb = peerManager_->getEventBase();
    return folly::via(
               &evb,
               [this, peerAddr]() -> UpdateGroupState {
                 auto group = getUpdateGroupForPeer(peerAddr);
                 return group ? group->getState()
                              : UpdateGroupState::UNINITIALIZED;
               })
        .get();
  }

  /*
   * Get count of detached peers in the group.
   */
  size_t getDetachedPeerCount(const folly::IPAddress& peerAddr) {
    auto group = getUpdateGroupForPeer(peerAddr);
    return group ? group->getDetachedPeers().size() : 0;
  }

  /*
   * Verify invariant: detached peers should NOT be in syncBitmap.
   */
  void verifySlowPeerInvariants(const folly::IPAddress& peerAddr) {
    auto group = getUpdateGroupForPeer(peerAddr);
    ASSERT_NE(group, nullptr) << "No update group for peer";

    const auto& detachedPeers = group->getDetachedPeers();
    for (const auto& detachedRib : detachedPeers) {
      uint64_t bit = detachedRib->getGroupBitPosition();
      EXPECT_FALSE(group->isPeerInSync(bit))
          << "Detached peer at bit " << bit << " should NOT be in sync bitmap";
    }
  }

  /*
   * Inject N routes with different communities to force N separate UPDATEs.
   */
  void injectDistinctRoutes(
      const std::vector<std::string>& prefixes,
      int communityBase,
      int localPref = 150) {
    int idx = 1;
    for (const auto& prefixStr : prefixes) {
      std::string community =
          std::to_string(communityBase) + ":" + std::to_string(idx++);
      injectLocalRoutesAtRuntime({prefixStr}, {community}, localPref);
      ASSERT_TRUE(
          waitForRouteInShadowRib(folly::IPAddress::createNetwork(prefixStr)))
          << "Route " << prefixStr << " did not reach shadowRIB";
    }
  }

  /*
   * Wait for a peer's cached RIB version to reach a target value.
   * Useful for reconnect tests where version propagation is async.
   */
  bool waitForPeerCachedVersionToReach(
      const folly::IPAddress& peerAddr,
      uint64_t targetVersion,
      int maxRetries = 50) {
    WITH_RETRIES_N(maxRetries, {
      auto version = getPeerCachedRibVersion(peerAddr);
      EXPECT_EVENTUALLY_GE(version, targetVersion);
    });
    return true;
  }

  /*
   * Standard 3-peer setup with EoR consumed, all JOINED_RUNNING.
   * Returns peer IDs for convenience.
   */
  struct ThreePeerSetup {
    BgpPeerId peerId3;
    BgpPeerId peerId4;
    BgpPeerId peerId5;
  };

  ThreePeerSetup setupThreePeersJoined() {
    addPeer(kDefaultPeerSpec3);
    addPeer(kDefaultPeerSpec4);
    addPeer(kDefaultPeerSpec5);

    setupSlowPeerComponents(3, 2, 0);

    BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
    BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
    BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

    bringUpPeer(kPeerAddr3);
    bringUpPeer(kPeerAddr4);
    bringUpPeer(kPeerAddr5);
    sendEoRToPeer(peerId3);
    sendEoRToPeer(peerId4);
    sendEoRToPeer(peerId5);

    /* Consume v4+v6 EoRs from all 3 peers */
    EXPECT_TRUE(waitForEoR(peerId3));
    EXPECT_TRUE(waitForEoR(peerId3));
    EXPECT_TRUE(waitForEoR(peerId4));
    EXPECT_TRUE(waitForEoR(peerId4));
    EXPECT_TRUE(waitForEoR(peerId5));
    EXPECT_TRUE(waitForEoR(peerId5));

    return {peerId3, peerId4, peerId5};
  }

  /*
   * Detach peer3 from a 3-peer group using frequency detection.
   * Assumes all 3 peers are JOINED_RUNNING.
   * Injects 3 routes with distinct communities, blocks peer3,
   * drains peer4/peer5, waits for peer3 DETACHED_BLOCKED.
   */
  void detachPeer3ViaFrequency(
      const BgpPeerId& peerId3,
      const BgpPeerId& /* peerId4 */,
      const BgpPeerId& /* peerId5 */,
      int routeBase = 100) {
    /* Set frequency threshold=1 for immediate detach on first block */
    setSlowPeerThresholds(
        kPeerAddr3,
        std::chrono::milliseconds(600000),
        1,
        std::chrono::milliseconds(60000));

    blockPeer(kPeerAddr3);

    /* Inject 3 routes (>= queue capacity) for reliable blocking */
    std::string p1 = std::to_string(routeBase) + ".0.0.0/8";
    std::string p2 = std::to_string(routeBase + 1) + ".0.0.0/8";
    std::string p3 = std::to_string(routeBase + 2) + ".0.0.0/8";
    std::string c1 = std::to_string(routeBase * 10) + ":1";
    std::string c2 = std::to_string((routeBase + 1) * 10) + ":1";
    std::string c3 = std::to_string((routeBase + 2) * 10) + ":1";

    injectLocalRoutesAtRuntime({p1}, {c1}, 150);
    ASSERT_TRUE(waitForRouteInShadowRib(folly::IPAddress::createNetwork(p1)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        std::to_string(routeBase) + ".0.0.0",
        8,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        c1));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        std::to_string(routeBase) + ".0.0.0",
        8,
        kPeerAddr5,
        getExpectedNexthop(kPeerAddr5),
        "4200000001",
        c1));

    injectLocalRoutesAtRuntime({p2}, {c2}, 150);
    ASSERT_TRUE(waitForRouteInShadowRib(folly::IPAddress::createNetwork(p2)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        std::to_string(routeBase + 1) + ".0.0.0",
        8,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        c2));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        std::to_string(routeBase + 1) + ".0.0.0",
        8,
        kPeerAddr5,
        getExpectedNexthop(kPeerAddr5),
        "4200000001",
        c2));

    injectLocalRoutesAtRuntime({p3}, {c3}, 150);
    ASSERT_TRUE(waitForRouteInShadowRib(folly::IPAddress::createNetwork(p3)));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        std::to_string(routeBase + 2) + ".0.0.0",
        8,
        kPeerAddr4,
        getExpectedNexthop(kPeerAddr4),
        "4200000001",
        c3));
    EXPECT_TRUE(verifyRouteAdd(
        "v4",
        std::to_string(routeBase + 2) + ".0.0.0",
        8,
        kPeerAddr5,
        getExpectedNexthop(kPeerAddr5),
        "4200000001",
        c3));

    /* Wait for detachment */
    EXPECT_TRUE(waitForPeerQueueBlocked(peerId3));
    EXPECT_TRUE(
        waitForPeerState(kPeerAddr3, PeerUpdateState::DETACHED_BLOCKED));
    EXPECT_TRUE(isPeerDetached(kPeerAddr3));
    EXPECT_FALSE(isPeerInSync(kPeerAddr3));
  }

  /*
   * Common setup helpers to reduce boilerplate.
   * Each brings up peers, sends EoR, waits for dual-stack EoRs,
   * and asserts all peers reach JOINED_RUNNING.
   */
  struct PeerIds {
    BgpPeerId peerId3;
    BgpPeerId peerId4;
    BgpPeerId peerId5;
  };

  PeerIds setupTwoPeersJoined(int queueSize = 3, int hwm = 2, int preload = 0) {
    addPeer(kDefaultPeerSpec3);
    addPeer(kDefaultPeerSpec4);
    setupSlowPeerComponents(queueSize, hwm, preload);

    BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
    BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};

    bringUpPeer(kPeerAddr3);
    bringUpPeer(kPeerAddr4);
    sendEoRToPeer(peerId3);
    sendEoRToPeer(peerId4);

    /* Consume v4 + v6 EoRs (dual-stack sends 2 EoRs per peer) */
    EXPECT_TRUE(waitForEoR(peerId3));
    EXPECT_TRUE(waitForEoR(peerId3));
    EXPECT_TRUE(waitForEoR(peerId4));
    EXPECT_TRUE(waitForEoR(peerId4));

    EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
    EXPECT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));

    return {peerId3, peerId4, {}};
  }

  PeerIds setupThreePeersJoined(int queueSize, int hwm, int preload) {
    addPeer(kDefaultPeerSpec3);
    addPeer(kDefaultPeerSpec4);
    addPeer(kDefaultPeerSpec5);
    setupSlowPeerComponents(queueSize, hwm, preload);

    BgpPeerId peerId3{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
    BgpPeerId peerId4{kPeerAddr4, kPeerAddr4.asV4().toLongHBO()};
    BgpPeerId peerId5{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};

    bringUpPeer(kPeerAddr3);
    bringUpPeer(kPeerAddr4);
    bringUpPeer(kPeerAddr5);
    sendEoRToPeer(peerId3);
    sendEoRToPeer(peerId4);
    sendEoRToPeer(peerId5);

    /* Consume v4 + v6 EoRs (dual-stack sends 2 EoRs per peer) */
    EXPECT_TRUE(waitForEoR(peerId3));
    EXPECT_TRUE(waitForEoR(peerId3));
    EXPECT_TRUE(waitForEoR(peerId4));
    EXPECT_TRUE(waitForEoR(peerId4));
    EXPECT_TRUE(waitForEoR(peerId5));
    EXPECT_TRUE(waitForEoR(peerId5));

    EXPECT_TRUE(waitForPeerState(kPeerAddr3, PeerUpdateState::JOINED_RUNNING));
    EXPECT_TRUE(waitForPeerState(kPeerAddr4, PeerUpdateState::JOINED_RUNNING));
    EXPECT_TRUE(waitForPeerState(kPeerAddr5, PeerUpdateState::JOINED_RUNNING));

    return {peerId3, peerId4, peerId5};
  }
};

/*
 * Fixture aliases for each test phase
 */
class UpdateGroupSlowPeerDetectionTest : public SlowPeerTestBase {};
class UpdateGroupDetachmentTest : public SlowPeerTestBase {};
class UpdateGroupDFPRecoveryTest : public SlowPeerTestBase {};
class UpdateGroupDSPRecoveryTest : public SlowPeerTestBase {};
class UpdateGroupLazyCloneTest : public SlowPeerTestBase {};
class UpdateGroupAcceptanceTest : public SlowPeerTestBase {};
class UpdateGroupPolicyRevalTest : public SlowPeerTestBase {};
class UpdateGroupPeerDownTest : public SlowPeerTestBase {};
class UpdateGroupMultiPeerTest : public SlowPeerTestBase {};
class UpdateGroupEventSequenceTest : public SlowPeerTestBase {};
class UpdateGroupLifecycleTest : public SlowPeerTestBase {};
class UpdateGroupShutdownTest : public SlowPeerTestBase {};

/* UG2 E2E test fixtures — gap coverage for production bugs */
class UpdateGroupCrossBoundaryStateTest : public SlowPeerTestBase {};
class UpdateGroupCoroutineRaceTest : public SlowPeerTestBase {};
class UpdateGroupReconnectVersionTest : public SlowPeerTestBase {};
class UpdateGroupEoRLifecycleTest : public SlowPeerTestBase {};
class UpdateGroupEmptyCLDetachTest : public SlowPeerTestBase {};
class UpdateGroupSplitHorizonTest : public SlowPeerTestBase {};

} // namespace bgp
} // namespace facebook
