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

#include <folly/coro/BlockingWait.h>
#include <folly/io/async/EventBase.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <thread>

#include <fb303/ThreadCachedServiceData.h>
#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibGroup.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibStructs.h"
#include "neteng/fboss/bgp/cpp/changeTracker/ChangeTracker.h"
#include "neteng/fboss/bgp/cpp/changeTracker/ConsumerBitmap.h"
#include "neteng/fboss/bgp/cpp/changeTracker/TrackableObject.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"

namespace facebook::bgp {

namespace {
int64_t getNumSlowPeersCounter() {
  fb303::ThreadCachedServiceData::get()->publishStats();
  return fb303::ThreadCachedServiceData::get()->getCounter(
      BgpStats::kSlowPeerDetectionCount + ".sum");
}
} // namespace

class UpdateGroupSlowPeerDetectionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    folly::SingletonVault::singleton()->registrationComplete();
    BgpStats::initCounters();
    evb_ = std::make_unique<folly::EventBase>();
    UpdateGroupConfig ugConfig;
    ugConfig.enableSlowPeerBlockCountDetection = true;
    ugConfig.allowSlowPeerDetach = true;
    group_ = std::make_shared<AdjRibOutGroup>(
        *evb_,
        "test_group",
        1,
        true /* enableUpdateGroup */,
        UpdateGroupKey{},
        ShadowRibView::empty() /* shadowRib */,
        nullptr /* policyManager */,
        ugConfig);

    // Set up change tracker infrastructure for detachment support
    changeTracker_ =
        std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");

    group_->setChangeListTracker(
        changeTracker_, addPathBitmap_, nonAddPathBitmap_);
    group_->registerGroupConsumer();
    groupConsumer_ = group_->getChangeListConsumer();

    // Publish a CL item so group consumer has a non-null marker
    auto entry = std::make_unique<ShadowRibEntry>();
    entry->prefix = folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24};
    trackableObject_ =
        std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(*entry));
    changeTracker_->publishChange(trackableObject_.get());
  }

  void TearDown() override {
    for (auto& peer : peers_) {
      peer->deactivateDetachedModeProcessing();
      peer->resetSlowPeerDurationTimer();
      peer->setUpdateGroup(nullptr);
    }
    groupConsumer_->resetBitmap();
    groupConsumer_->terminate();
    groupConsumer_->deregisterFromTracker();
    group_->resetChangeListConsumer();
    groupConsumer_.reset();
    group_.reset();
    peers_.clear();
    evb_.reset();
  }

  std::shared_ptr<AdjRib> createAndRegisterPeer(
      uint64_t bit,
      PeerUpdateState state = PeerUpdateState::JOINED_RUNNING) {
    auto peerId = nettools::bgplib::BgpPeerId(
        folly::IPAddress("10.0.0.1"),
        folly::IPAddressV4("255.0.0.1").toLongHBO());
    auto adjRib = std::make_shared<AdjRib>(
        peerId,
        PeeringParams(),
        *evb_,
        ribInQ_,
        observerQ_,
        std::make_shared<folly::coro::Baton>(),
        nullptr /* policyManager */,
        std::make_shared<std::atomic<bool>>(false));
    adjRib->setUpdateGroup(group_);
    group_->registerPeer(adjRib);
    /*
     * Bits are handed out lowest-free-first, so peers registered in order
     * land on the bit the caller expects.
     */
    EXPECT_EQ(bit, adjRib->getGroupBitPosition());

    adjRib->setPeerState(state);
    switch (state) {
      case PeerUpdateState::JOINED_RUNNING:
      case PeerUpdateState::JOINED_BLOCKED:
        group_->markPeerInSync(adjRib);
        break;
      case PeerUpdateState::DOWN:
      case PeerUpdateState::INIT:
      case PeerUpdateState::DETACHED_INIT_DUMP:
      case PeerUpdateState::DETACHED_READY_TO_JOIN:
      case PeerUpdateState::DETACHED_BLOCKED:
      case PeerUpdateState::DETACHED_RUNNING:
      default:
        /*
         * registerPeer() marks the peer in sync while the group is still
         * UNINITIALIZED; these states model a peer that is not sharing the
         * group's stream, so undo it.
         */
        group_->markPeerDetached(adjRib);
        break;
    }

    peers_.push_back(adjRib);
    return adjRib;
  }

  std::unique_ptr<folly::EventBase> evb_;
  std::shared_ptr<AdjRibOutGroup> group_;
  std::shared_ptr<ChangeTracker<ShadowRibEntry>> changeTracker_;
  std::shared_ptr<AdjRibOutGroupConsumer> groupConsumer_;
  ConsumerBitmap addPathBitmap_;
  ConsumerBitmap nonAddPathBitmap_;
  std::unique_ptr<TrackableObject<ShadowRibEntry>> trackableObject_;
  std::vector<std::shared_ptr<AdjRib>> peers_;
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ_{
      nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<AdjRib::ObservableMessageT> observerQ_;
};

TEST_F(UpdateGroupSlowPeerDetectionTest, MarkPeerBlockedSetsBitmapBit) {
  auto adjRib = createAndRegisterPeer(0);

  group_->markPeerBlocked(adjRib);

  EXPECT_TRUE(BitmapUtils::isBitSet(group_->getBlockedBitmap(), 0));
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::JOINED_BLOCKED);
}

TEST_F(UpdateGroupSlowPeerDetectionTest, MarkPeerUnblockedClearsBitmapBit) {
  auto adjRib = createAndRegisterPeer(0);

  group_->markPeerBlocked(adjRib);
  EXPECT_TRUE(BitmapUtils::isBitSet(group_->getBlockedBitmap(), 0));
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::JOINED_BLOCKED);

  group_->markPeerUnblocked(adjRib);
  EXPECT_FALSE(BitmapUtils::isBitSet(group_->getBlockedBitmap(), 0));
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::JOINED_RUNNING);
}

TEST_F(
    UpdateGroupSlowPeerDetectionTest,
    HasBlockedPeersReturnsTrueWhenBlocked) {
  auto adjRib = createAndRegisterPeer(0);

  EXPECT_FALSE(group_->hasBlockedPeers());

  group_->markPeerBlocked(adjRib);
  EXPECT_TRUE(group_->hasBlockedPeers());
}

TEST_F(
    UpdateGroupSlowPeerDetectionTest,
    HasBlockedPeersReturnsFalseWhenNoneBlocked) {
  auto adjRib = createAndRegisterPeer(0);

  group_->markPeerBlocked(adjRib);
  group_->markPeerUnblocked(adjRib);

  EXPECT_FALSE(group_->hasBlockedPeers());
}

TEST_F(
    UpdateGroupSlowPeerDetectionTest,
    DurationDetectionTriggersSlowPeerProcedure) {
  auto adjRib = createAndRegisterPeer(0);
  auto slowPeersBefore = getNumSlowPeersCounter();

  // Set a short duration threshold so the timer fires quickly
  {
    UpdateGroupConfig cfg;
    cfg.enableSlowPeerBlockCountDetection = true;
    cfg.allowSlowPeerDetach = true;
    cfg.slowPeerTimeThreshold = std::chrono::milliseconds(10);
    cfg.slowPeerBlockCountThreshold = 100; // high so frequency doesn't trigger
    cfg.slowPeerBlockCountWindow = std::chrono::milliseconds(300000);
    group_->setUpdateGroupConfigForTesting(cfg);
  }

  group_->markPeerBlocked(adjRib);

  // Run the event base to let the duration timer fire
  evb_->scheduleAt(
      [this]() { evb_->terminateLoopSoon(); },
      evb_->now() + std::chrono::milliseconds(50));
  evb_->loopForever();

  /*
   * The timer should have fired and called detachSlowPeer.
   * Since this is a single-peer group, detachment is skipped.
   * Verify that the peer is still marked as blocked.
   */
  EXPECT_TRUE(BitmapUtils::isBitSet(group_->getBlockedBitmap(), 0));

  /*
   * Counter should increment — slow peer was detected (even though
   * detachment was skipped because it's the only synced peer)
   */
  EXPECT_EQ(slowPeersBefore + 1, getNumSlowPeersCounter());
}

/*
 * With enableSlowPeerBlockCountDetection left at its default (false), blocks
 * past the count threshold neither detect nor detach the peer, and its block
 * info is not populated.
 */
TEST_F(UpdateGroupSlowPeerDetectionTest, BlockCountDetectionDisabledByDefault) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  auto slowPeersBefore = getNumSlowPeersCounter();

  {
    UpdateGroupConfig cfg;
    cfg.allowSlowPeerDetach = true;
    cfg.slowPeerTimeThreshold = std::chrono::milliseconds(300000);
    cfg.slowPeerBlockCountThreshold = 1;
    cfg.slowPeerBlockCountWindow = std::chrono::milliseconds(300000);
    group_->setUpdateGroupConfigForTesting(cfg);
  }

  group_->markPeerBlocked(adjRib0);
  group_->markPeerUnblocked(adjRib0);
  group_->markPeerBlocked(adjRib0);

  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_BLOCKED);
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib0));
  EXPECT_EQ(0, adjRib0->getPeerBlockInfo().blockCount);
  EXPECT_EQ(slowPeersBefore, getNumSlowPeersCounter());
}

TEST_F(
    UpdateGroupSlowPeerDetectionTest,
    FrequencyDetectionTriggersSlowPeerProcedure) {
  auto adjRib = createAndRegisterPeer(0);
  auto slowPeersBefore = getNumSlowPeersCounter();

  // Set frequency threshold to 3 blocks within the window
  {
    UpdateGroupConfig cfg;
    cfg.enableSlowPeerBlockCountDetection = true;
    cfg.allowSlowPeerDetach = true;
    cfg.slowPeerTimeThreshold =
        std::chrono::milliseconds(300000); // high so duration doesn't trigger
    cfg.slowPeerBlockCountThreshold = 3;
    cfg.slowPeerBlockCountWindow = std::chrono::milliseconds(300000);
    group_->setUpdateGroupConfigForTesting(cfg);
  }

  // Block/unblock 3 times to hit the frequency threshold
  group_->markPeerBlocked(adjRib);
  group_->markPeerUnblocked(adjRib);
  group_->markPeerBlocked(adjRib);
  group_->markPeerUnblocked(adjRib);
  group_->markPeerBlocked(adjRib);

  /*
   * The 3rd block should have triggered detachSlowPeer.
   * Verify peer is still tracked as blocked.
   */
  EXPECT_TRUE(BitmapUtils::isBitSet(group_->getBlockedBitmap(), 0));

  /*
   * Counter should increment — slow peer was detected (even though
   * detachment was skipped because it's the only synced peer)
   */
  EXPECT_EQ(slowPeersBefore + 1, getNumSlowPeersCounter());
}

TEST_F(
    UpdateGroupSlowPeerDetectionTest,
    UnblockBeforeThresholdPreventsDetection) {
  auto adjRib = createAndRegisterPeer(0);

  // Set a longer duration threshold
  {
    UpdateGroupConfig cfg;
    cfg.enableSlowPeerBlockCountDetection = true;
    cfg.allowSlowPeerDetach = true;
    cfg.slowPeerTimeThreshold = std::chrono::milliseconds(500);
    cfg.slowPeerBlockCountThreshold = 100; // high so frequency doesn't trigger
    cfg.slowPeerBlockCountWindow = std::chrono::milliseconds(300000);
    group_->setUpdateGroupConfigForTesting(cfg);
  }

  // Block then unblock before the timer fires
  group_->markPeerBlocked(adjRib);
  group_->markPeerUnblocked(adjRib);

  // Run the event base briefly — timer should have been cancelled
  evb_->scheduleAt(
      [this]() { evb_->terminateLoopSoon(); },
      evb_->now() + std::chrono::milliseconds(50));
  evb_->loopForever();

  EXPECT_FALSE(BitmapUtils::isBitSet(group_->getBlockedBitmap(), 0));
  EXPECT_FALSE(group_->hasBlockedPeers());
}

/*
 * Lifecycle scenario testing slow peer detection through multiple phases:
 *   Phase 1: Duration near-miss — block and unblock before the duration timer
 *            fires, verifying the timer is cancelled and detection is avoided.
 *   Phase 2: Frequency window expiry — accumulate blocks (carrying over from
 *            Phase 1), then advance past the window boundary so the counter
 *            resets. A subsequent block should start fresh at count=1.
 *   Phase 3: Cycle within threshold — block/unblock without exceeding the
 *            frequency threshold, verifying no detection.
 *   Phase 4: Eventual trigger — one final block pushes the count to the
 *            threshold, triggering startSlowPeerProcedure. Verified via log
 *            messages.
 */
TEST_F(
    UpdateGroupSlowPeerDetectionTest,
    SlowPeerLifecycleRecoveryAndEventualDetection) {
  auto adjRib = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  group_->markPeerInSync(adjRib);
  group_->markPeerInSync(adjRib1);

  auto slowPeersBefore = getNumSlowPeersCounter();

  auto& messages = subscribeToLogMessages("", folly::LogLevel::INFO);

  /*
   * Testable thresholds:
   * - Duration: 100ms continuous block to trigger
   * - Frequency: 4 blocks within a 200ms window
   */
  {
    UpdateGroupConfig cfg;
    cfg.enableSlowPeerBlockCountDetection = true;
    cfg.allowSlowPeerDetach = true;
    cfg.slowPeerTimeThreshold = std::chrono::milliseconds(100);
    cfg.slowPeerBlockCountThreshold = 4;
    cfg.slowPeerBlockCountWindow = std::chrono::milliseconds(200);
    group_->setUpdateGroupConfigForTesting(cfg);
  }

  /*
   * --- Phase 1: Duration near-miss ---
   * Block and immediately unblock before the 100ms duration timer fires.
   */
  auto messagesBeforePhase1 = messages.size();
  group_->markPeerBlocked(adjRib);
  EXPECT_TRUE(BitmapUtils::isBitSet(group_->getBlockedBitmap(), 0));
  group_->markPeerUnblocked(adjRib);

  /*
   * Run the event base past the duration threshold to confirm the cancelled
   * timer does not fire.
   */
  evb_->scheduleAt(
      [this]() { evb_->terminateLoopSoon(); },
      evb_->now() + std::chrono::milliseconds(150));
  evb_->loopForever();

  EXPECT_FALSE(BitmapUtils::isBitSet(group_->getBlockedBitmap(), 0));
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  // Verify slow peer detection was not triggered.
  EXPECT_EQ(messages.size(), messagesBeforePhase1);

  /*
   * --- Phase 2: Frequency window expiry resets counter ---
   * Phase 1's block count carries over (count is now 1).
   * Block/unblock twice more to reach count=3, just under threshold of 4.
   */
  auto messagesBeforePhase2 = messages.size();
  group_->markPeerBlocked(adjRib);
  group_->markPeerUnblocked(adjRib);
  group_->markPeerBlocked(adjRib);
  group_->markPeerUnblocked(adjRib);

  // Advance past the window so the counter resets.
  std::this_thread::sleep_for(std::chrono::milliseconds(250));

  // Block again — counter should have reset to 1, not accumulated to 4.
  group_->markPeerBlocked(adjRib);
  EXPECT_TRUE(BitmapUtils::isBitSet(group_->getBlockedBitmap(), 0));
  group_->markPeerUnblocked(adjRib);
  EXPECT_FALSE(group_->hasBlockedPeers());
  // Verify slow peer detection was not triggered.
  EXPECT_EQ(messages.size(), messagesBeforePhase2);

  /*
   * --- Phase 3: Cycle within threshold ---
   * Two more block/unblock cycles in the same window (counts 2 and 3),
   * staying under the threshold of 4.
   */
  auto messagesBeforePhase3 = messages.size();
  group_->markPeerBlocked(adjRib);
  group_->markPeerUnblocked(adjRib);
  group_->markPeerBlocked(adjRib);
  group_->markPeerUnblocked(adjRib);
  EXPECT_FALSE(group_->hasBlockedPeers());
  // Verify slow peer detection was not triggered.
  EXPECT_EQ(messages.size(), messagesBeforePhase3);

  /*
   * --- Phase 4: Eventual frequency trigger ---
   * One more block pushes the count to 4, hitting the threshold.
   * detachSlowPeer is called, which clears the blocked bit.
   */
  auto messagesBeforePhase4 = messages.size();
  group_->markPeerBlocked(adjRib);
  EXPECT_FALSE(BitmapUtils::isBitSet(group_->getBlockedBitmap(), 0));
  EXPECT_GE(messages.size(), messagesBeforePhase4 + 2);
  EXPECT_THAT(
      messages[messagesBeforePhase4].first.getMessage(),
      testing::HasSubstr("exceeded block frequency threshold"));
  EXPECT_THAT(
      messages[messagesBeforePhase4 + 1].first.getMessage(),
      testing::HasSubstr("Detaching slow peer"));

  // Verify ODS counter incremented — detachment succeeded (multi-peer group)
  EXPECT_EQ(slowPeersBefore + 1, getNumSlowPeersCounter());
}

TEST_F(UpdateGroupSlowPeerDetectionTest, MultiPeerDetectionIndependent) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);

  // Set frequency threshold to 3
  {
    UpdateGroupConfig cfg;
    cfg.enableSlowPeerBlockCountDetection = true;
    cfg.allowSlowPeerDetach = true;
    cfg.slowPeerTimeThreshold = std::chrono::milliseconds(300000);
    cfg.slowPeerBlockCountThreshold = 3;
    cfg.slowPeerBlockCountWindow = std::chrono::milliseconds(300000);
    group_->setUpdateGroupConfigForTesting(cfg);
  }

  // Block peer 0 twice (below threshold)
  group_->markPeerBlocked(adjRib0);
  group_->markPeerUnblocked(adjRib0);
  group_->markPeerBlocked(adjRib0);
  group_->markPeerUnblocked(adjRib0);

  // Peer 0 should not be detached (only 2 blocks, threshold is 3)
  EXPECT_TRUE(group_->isPeerInSync(0));

  // Block peer 1 three times — 3rd block hits threshold, triggers detachment
  group_->markPeerBlocked(adjRib1);
  group_->markPeerUnblocked(adjRib1);
  group_->markPeerBlocked(adjRib1);
  group_->markPeerUnblocked(adjRib1);
  group_->markPeerBlocked(adjRib1);

  // Peer 1 should be detached (3 blocks hit threshold)
  EXPECT_FALSE(group_->isPeerInSync(1));
  EXPECT_EQ(adjRib1->getPeerState(), PeerUpdateState::DETACHED_BLOCKED);

  // Peer 0 should still be in sync — counts are independent
  EXPECT_TRUE(group_->isPeerInSync(0));
}

TEST_F(UpdateGroupSlowPeerDetectionTest, UnregisterPeerSetsPeerStateDown) {
  auto adjRib = createAndRegisterPeer(0);

  adjRib->setPeerState(PeerUpdateState::JOINED_RUNNING);
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::JOINED_RUNNING);

  group_->unregisterPeer(adjRib);
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::DOWN);

  // Remove from peers_ so TearDown doesn't double-unregister
  peers_.clear();
}

TEST_F(
    UpdateGroupSlowPeerDetectionTest,
    UnregisterBlockedPeerSetsPeerStateDown) {
  auto adjRib = createAndRegisterPeer(0);

  group_->markPeerBlocked(adjRib);
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::JOINED_BLOCKED);

  group_->unregisterPeer(adjRib);
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::DOWN);

  peers_.clear();
}

/*
 * Verify that when allowSlowPeerDetach is disabled, slow peer detection
 * still increments the counter but does NOT detach the peer.
 */
TEST_F(
    UpdateGroupSlowPeerDetectionTest,
    SlowPeerDetachDisabledPreventsDetachment) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  group_->markPeerInSync(adjRib0);
  group_->markPeerInSync(adjRib1);

  auto slowPeersBefore = getNumSlowPeersCounter();

  // Disable slow peer detachment and set frequency threshold to 2
  {
    UpdateGroupConfig cfg;
    cfg.enableSlowPeerBlockCountDetection = true;
    cfg.allowSlowPeerDetach = false;
    cfg.slowPeerTimeThreshold = std::chrono::milliseconds(300000);
    cfg.slowPeerBlockCountThreshold = 2;
    cfg.slowPeerBlockCountWindow = std::chrono::milliseconds(300000);
    group_->setUpdateGroupConfigForTesting(cfg);
  }

  // Block peer 0 twice to hit frequency threshold
  group_->markPeerBlocked(adjRib0);
  group_->markPeerUnblocked(adjRib0);
  group_->markPeerBlocked(adjRib0);

  // Slow peer was detected (counter incremented) but NOT detached
  EXPECT_EQ(slowPeersBefore + 1, getNumSlowPeersCounter());
  EXPECT_TRUE(group_->isPeerInSync(0));
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_BLOCKED);
}

/*
 * Verify that when allowSlowPeerDetach is disabled, duration-based detection
 * also does not detach the peer.
 */
TEST_F(
    UpdateGroupSlowPeerDetectionTest,
    SlowPeerDetachDisabledPreventsDurationDetachment) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  group_->markPeerInSync(adjRib0);
  group_->markPeerInSync(adjRib1);

  auto slowPeersBefore = getNumSlowPeersCounter();

  // Disable slow peer detachment; set a short duration threshold
  {
    UpdateGroupConfig cfg;
    cfg.allowSlowPeerDetach = false;
    cfg.slowPeerTimeThreshold = std::chrono::milliseconds(10);
    cfg.slowPeerBlockCountThreshold = 100; // high so frequency doesn't trigger
    cfg.slowPeerBlockCountWindow = std::chrono::milliseconds(300000);
    group_->setUpdateGroupConfigForTesting(cfg);
  }

  group_->markPeerBlocked(adjRib0);

  // Run the event base to let the duration timer fire
  evb_->scheduleAt(
      [this]() { evb_->terminateLoopSoon(); },
      evb_->now() + std::chrono::milliseconds(50));
  evb_->loopForever();

  // Slow peer was detected (counter incremented) but NOT detached
  EXPECT_EQ(slowPeersBefore + 1, getNumSlowPeersCounter());
  EXPECT_TRUE(group_->isPeerInSync(0));
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_BLOCKED);
}

/*
 * Verify that re-enabling allowSlowPeerDetach after disabling it
 * restores normal detachment behavior.
 */
TEST_F(
    UpdateGroupSlowPeerDetectionTest,
    SlowPeerDetachReenabledAllowsDetachment) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  group_->markPeerInSync(adjRib0);
  group_->markPeerInSync(adjRib1);

  // Disable detach, set frequency threshold to 2 blocks
  {
    UpdateGroupConfig cfg;
    cfg.enableSlowPeerBlockCountDetection = true;
    cfg.allowSlowPeerDetach = false;
    cfg.slowPeerTimeThreshold = std::chrono::milliseconds(300000);
    cfg.slowPeerBlockCountThreshold = 2;
    cfg.slowPeerBlockCountWindow = std::chrono::milliseconds(300000);
    group_->setUpdateGroupConfigForTesting(cfg);
  }

  // Trigger detection (should not detach because allowSlowPeerDetach=false)
  group_->markPeerBlocked(adjRib0);
  group_->markPeerUnblocked(adjRib0);
  group_->markPeerBlocked(adjRib0);
  EXPECT_TRUE(group_->isPeerInSync(0));
  group_->markPeerUnblocked(adjRib0);

  /*
   * Re-enable detach and trigger detection again.
   * Block count carried over (2), so the next block (count=3 >= 2)
   * triggers detachSlowPeer which now succeeds.
   */
  {
    UpdateGroupConfig cfg;
    cfg.enableSlowPeerBlockCountDetection = true;
    cfg.allowSlowPeerDetach = true;
    cfg.slowPeerTimeThreshold = std::chrono::milliseconds(300000);
    cfg.slowPeerBlockCountThreshold = 2;
    cfg.slowPeerBlockCountWindow = std::chrono::milliseconds(300000);
    group_->setUpdateGroupConfigForTesting(cfg);
  }
  group_->markPeerBlocked(adjRib0);

  // Peer should now be detached
  EXPECT_FALSE(group_->isPeerInSync(0));
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::DETACHED_BLOCKED);
}

/*
 * Verify that configurable thresholds via constructor are applied correctly.
 */
TEST_F(
    UpdateGroupSlowPeerDetectionTest,
    ConstructorConfigurableThresholdsApplied) {
  // Create a new group with custom thresholds via constructor
  UpdateGroupConfig customConfig;
  customConfig.enableSlowPeerBlockCountDetection = true;
  customConfig.allowSlowPeerDetach = true;
  customConfig.slowPeerTimeThreshold = std::chrono::milliseconds{100};
  customConfig.slowPeerBlockCountThreshold = 3;
  customConfig.slowPeerBlockCountWindow = std::chrono::milliseconds{60000};
  auto customGroup = std::make_shared<AdjRibOutGroup>(
      *evb_,
      "custom_group",
      2,
      true /* enableUpdateGroup */,
      UpdateGroupKey{},
      ShadowRibView::empty() /* shadowRib */,
      nullptr /* policyManager */,
      customConfig);

  // Set up change tracker for the custom group
  auto customChangeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("custom_tracker");
  ConsumerBitmap customAddPath, customNonAddPath;
  customGroup->setChangeListTracker(
      customChangeTracker, customAddPath, customNonAddPath);
  customGroup->registerGroupConsumer();
  auto customConsumer = customGroup->getChangeListConsumer();

  auto entry = std::make_unique<ShadowRibEntry>();
  entry->prefix = folly::CIDRNetwork{folly::IPAddress("10.0.1.0"), 24};
  auto trackable =
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(*entry));
  customChangeTracker->publishChange(trackable.get());

  // Create two peers in the custom group
  auto peerId = nettools::bgplib::BgpPeerId(
      folly::IPAddress("10.0.0.1"),
      folly::IPAddressV4("255.0.0.1").toLongHBO());
  auto peer0 = std::make_shared<AdjRib>(
      peerId,
      PeeringParams(),
      *evb_,
      ribInQ_,
      observerQ_,
      std::make_shared<folly::coro::Baton>(),
      nullptr,
      std::make_shared<std::atomic<bool>>(false));
  peer0->setUpdateGroup(customGroup);
  customGroup->registerPeer(peer0);
  ASSERT_EQ(0, peer0->getGroupBitPosition());
  customGroup->markPeerInSync(peer0);
  peer0->setPeerState(PeerUpdateState::JOINED_RUNNING);

  auto peer1 = std::make_shared<AdjRib>(
      peerId,
      PeeringParams(),
      *evb_,
      ribInQ_,
      observerQ_,
      std::make_shared<folly::coro::Baton>(),
      nullptr,
      std::make_shared<std::atomic<bool>>(false));
  peer1->setUpdateGroup(customGroup);
  customGroup->registerPeer(peer1);
  ASSERT_EQ(1, peer1->getGroupBitPosition());
  customGroup->markPeerInSync(peer1);
  peer1->setPeerState(PeerUpdateState::JOINED_RUNNING);

  // Block peer0 3 times to hit the custom countThreshold of 3
  customGroup->markPeerBlocked(peer0);
  customGroup->markPeerUnblocked(peer0);
  customGroup->markPeerBlocked(peer0);
  customGroup->markPeerUnblocked(peer0);
  customGroup->markPeerBlocked(peer0);

  // Peer 0 should be detached (3 blocks hit custom threshold of 3)
  EXPECT_FALSE(customGroup->isPeerInSync(0));
  EXPECT_EQ(peer0->getPeerState(), PeerUpdateState::DETACHED_BLOCKED);

  // Cleanup
  peer0->deactivateDetachedModeProcessing();
  peer0->resetSlowPeerDurationTimer();
  peer0->setUpdateGroup(nullptr);
  peer1->deactivateDetachedModeProcessing();
  peer1->resetSlowPeerDurationTimer();
  peer1->setUpdateGroup(nullptr);
  customConsumer->resetBitmap();
  customConsumer->terminate();
  customConsumer->deregisterFromTracker();
  customGroup->resetChangeListConsumer();
}

} // namespace facebook::bgp
