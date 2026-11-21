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

#include <algorithm>
#include <memory>
#include <string>
#include <variant>

#include <folly/ScopeGuard.h>
#include <folly/Synchronized.h>
#include <folly/coro/BlockingWait.h>
#include <gflags/gflags.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "fboss/fsdb/client/FsdbPubSubManager.h"
#define RibBase_TEST_FRIENDS friend class RibFsdbFixture;

#include "neteng/fboss/bgp/cpp/fsdb/CanonicalRibUpdateQueue.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibFsdbPolicyTestFixture.h"
#include "neteng/fboss/bgp/cpp/tests/RibPolicyUtils.h"

using namespace facebook::bgp;
using namespace facebook::bgp::rib_policy;
using namespace facebook::neteng::fboss::bgp::thrift;
using namespace std::chrono;
using ::testing::_;

namespace facebook {
namespace bgp {

class CanonicalRibProducerFixture : public RibFixture {
 public:
  void SetUp() override {
    RibFixture::SetUp();
    updateQueue_ = std::make_shared<CanonicalRibUpdateQueue>();
    rib_->getEventBase().runInEventBaseThreadAndWait(
        [this]() { rib_->configureCanonicalRibProducerForTest(updateQueue_); });
  }

 protected:
  std::shared_ptr<CanonicalRibUpdateQueue> updateQueue_;
};

TEST_F(CanonicalRibProducerFixture, PreCycleChangesPublishOnlyTheFinalState) {
  const PrefixPathId prefixPathId{kV4Prefix1, kDefaultPathID};
  rib_->getEventBase().runInEventBaseThreadAndWait([this, &prefixPathId]() {
    rib_->processSingleRibInUpdateForTest(eBgpPeer1_, attr_, prefixPathId);
    rib_->processSingleRibInUpdateForTest(eBgpPeer1_, nullptr, prefixPathId);
  });
  EXPECT_TRUE(updateQueue_->empty());

  rib_->getEventBase().runInEventBaseThreadAndWait([this, &prefixPathId]() {
    rib_->processSingleRibInUpdateForTest(eBgpPeer1_, attr_, prefixPathId);
    rib_->prepareFibProgramming(fboss::kDefaultVrf);
  });

  ASSERT_EQ(updateQueue_->size(), 1);
  auto queued = folly::coro::blockingWait(updateQueue_->pop());
  ASSERT_TRUE(std::holds_alternative<CanonicalRibPrefixUpdate>(queued));
  const auto& update = std::get<CanonicalRibPrefixUpdate>(queued);
  EXPECT_EQ(update.prefix, kV4Prefix1);
  ASSERT_TRUE(update.entry.has_value());
  EXPECT_EQ(update.entry->paths.size(), 1);
}

TEST_F(
    CanonicalRibProducerFixture,
    PreCycleAnnouncementAndWithdrawalPublishNothing) {
  const PrefixPathId prefixPathId{kV4Prefix1, kDefaultPathID};
  rib_->getEventBase().runInEventBaseThreadAndWait([this, &prefixPathId]() {
    rib_->processSingleRibInUpdateForTest(eBgpPeer1_, attr_, prefixPathId);
    rib_->processSingleRibInUpdateForTest(eBgpPeer1_, nullptr, prefixPathId);
  });
  EXPECT_TRUE(updateQueue_->empty());

  rib_->getEventBase().runInEventBaseThreadAndWait(
      [this]() { rib_->prepareFibProgramming(fboss::kDefaultVrf); });

  EXPECT_TRUE(updateQueue_->empty());
}

TEST_F(CanonicalRibProducerFixture, ComputedWithdrawalPublishesAtNextCycle) {
  const PrefixPathId prefixPathId{kV4Prefix1, kDefaultPathID};
  auto fibFuture = fib_->getFibProgramFuture();
  rib_->getEventBase().runInEventBaseThreadAndWait([this, &prefixPathId]() {
    rib_->processSingleRibInUpdateForTest(eBgpPeer1_, attr_, prefixPathId);
    EXPECT_TRUE(updateQueue_->empty());
    rib_->prepareFibProgramming(fboss::kDefaultVrf, /* fullSync */ true);
  });
  fibFuture.wait();

  ASSERT_EQ(updateQueue_->size(), 1);
  auto announcementMessage = folly::coro::blockingWait(updateQueue_->pop());
  ASSERT_TRUE(
      std::holds_alternative<CanonicalRibPrefixUpdate>(announcementMessage));
  const auto& announcement =
      std::get<CanonicalRibPrefixUpdate>(announcementMessage);
  EXPECT_EQ(announcement.prefix, kV4Prefix1);
  EXPECT_TRUE(announcement.entry.has_value());

  fibFuture = fib_->getFibProgramFuture();
  rib_->getEventBase().runInEventBaseThreadAndWait([this, &prefixPathId]() {
    rib_->processSingleRibInUpdateForTest(eBgpPeer1_, nullptr, prefixPathId);
    EXPECT_TRUE(updateQueue_->empty());
    rib_->prepareFibProgramming(fboss::kDefaultVrf);
  });
  fibFuture.wait();

  ASSERT_EQ(updateQueue_->size(), 1);
  auto withdrawalMessage = folly::coro::blockingWait(updateQueue_->pop());
  ASSERT_TRUE(
      std::holds_alternative<CanonicalRibPrefixUpdate>(withdrawalMessage));
  const auto& withdrawal =
      std::get<CanonicalRibPrefixUpdate>(withdrawalMessage);
  EXPECT_EQ(withdrawal.prefix, kV4Prefix1);
  EXPECT_FALSE(withdrawal.entry.has_value());
}

TEST_F(CanonicalRibProducerFixture, FullSnapshotIsOneOwningQueueMessage) {
  const PrefixPathId first{kV4Prefix1, kDefaultPathID};
  const PrefixPathId second{kV4Prefix2, kDefaultPathID};
  rib_->getEventBase().runInEventBaseThreadAndWait([this, &first, &second]() {
    rib_->processSingleRibInUpdateForTest(eBgpPeer1_, attr_, first);
    rib_->processSingleRibInUpdateForTest(eBgpPeer1_, attr_, second);
    rib_->enqueueCanonicalRibFullSnapshotForTest();
  });

  ASSERT_EQ(updateQueue_->size(), 1);
  auto queued = folly::coro::blockingWait(updateQueue_->pop());
  const auto& snapshot = std::get<CanonicalRibFullSnapshotInput>(queued);
  ASSERT_EQ(snapshot.entries.size(), 2);
  const auto containsPrefix = [&snapshot](const folly::CIDRNetwork& prefix) {
    return std::any_of(
        snapshot.entries.begin(),
        snapshot.entries.end(),
        [&prefix](const auto& entry) {
          return entry.prefix == prefix && entry.entry.has_value();
        });
  };
  EXPECT_TRUE(containsPrefix(kV4Prefix1));
  EXPECT_TRUE(containsPrefix(kV4Prefix2));
}

class CanonicalRibFsdbFixture : public RibFixture {
 public:
  void SetUp() override {
    FLAGS_publish_rib_to_fsdb = true;
    createFsdbTestResources();
    RibFixture::SetUp();
    EXPECT_FALSE(isFsdbSyncerStarted());
  }

  void installNoBestPathPolicy(const folly::CIDRNetwork& prefix) {
    TPathSelector selector;
    selector.bgp_native_path_selection_min_nexthop() = 2;
    selector.drain_on_min_nexthop_violation() = false;
    sendPathSelectionPolicySet(
        createTPathSelectionPolicyWithPathSelector({prefix}, selector));
    rib_->waitForPathSelectionPolicyUpdate();
  }

 protected:
  gflags::FlagSaver flagSaver_;
};

class CanonicalRibPathModeFixture : public CanonicalRibFsdbFixture,
                                    public ::testing::WithParamInterface<bool> {
 public:
  void SetUp() override {
    FLAGS_publish_multipaths_to_fsdb = GetParam();
    CanonicalRibFsdbFixture::SetUp();
  }
};

TEST_P(CanonicalRibPathModeFixture, PublishesConfiguredPathShape) {
  auto subscribedRib = fsdbSubscriber_->subscribe(
      fsdbSubscriber_->getRootStatePath().bgp().canonicalRib());
  const PrefixPathIds prefixBatch{{kV4Prefix1, kDefaultPathID}};
  auto backupAttr = attr_->clone();
  backupAttr->setLocalPref(90);
  backupAttr->publish();

  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  sendAnnouncement(prefixBatch, eBgpPeer2_, attr_);
  sendAnnouncement(prefixBatch, eBgpPeer3_, std::move(backupAttr));
  sendInitialPathComputation();
  fibFuture.wait();
  waitForFsdbPublisherConnected();
  WITH_RETRIES_N(100, {
    EXPECT_EVENTUALLY_TRUE(fsdbSyncer_->isIncrementalPublicationReady());
  });

  const auto prefix = folly::IPAddress::networkToString(kV4Prefix1);
  WITH_RETRIES_N(100, {
    const auto rib = subscribedRib.rlock();
    ASSERT_EVENTUALLY_TRUE(rib->has_value());
    ASSERT_EVENTUALLY_TRUE((*rib)->rib_entries()->contains(prefix));
    const auto& entry = (*rib)->rib_entries()->at(prefix);
    ASSERT_EVENTUALLY_TRUE(entry.best_path().has_value());
    if (!GetParam()) {
      EXPECT_EVENTUALLY_TRUE(entry.paths()->empty());
    } else {
      EXPECT_EVENTUALLY_EQ(1, entry.paths()->size());
      const auto bestGroup = entry.paths()->find(std::string{kBestPathGroup});
      ASSERT_EVENTUALLY_NE(bestGroup, entry.paths()->end());
      EXPECT_EVENTUALLY_EQ(2, bestGroup->second.size());
      for (const auto& path : bestGroup->second) {
        EXPECT_EVENTUALLY_TRUE(
            (*rib)->deduped_paths()->contains(*path.path_idx()));
        ASSERT_EVENTUALLY_TRUE(path.peer_idx().has_value());
        EXPECT_EVENTUALLY_TRUE((*rib)->peers()->contains(*path.peer_idx()));
      }
    }
  });
}

INSTANTIATE_TEST_SUITE_P(
    BestPathOnlyAndMultipath,
    CanonicalRibPathModeFixture,
    ::testing::Values(false, true),
    [](const ::testing::TestParamInfo<bool>& info) {
      return info.param ? "Multipath" : "BestPathOnly";
    });

TEST_F(CanonicalRibFsdbFixture, ReconnectPublishesFreshCanonicalSnapshot) {
  auto subscribedRib = fsdbSubscriber_->subscribe(
      fsdbSubscriber_->getRootStatePath().bgp().canonicalRib());

  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  waitForFsdbPublisherConnected();
  rib_->setFibBatchTime(milliseconds(2));

  const auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  fibFuture.wait();
  const auto prefix = folly::IPAddress::networkToString(kV4Prefix1);
  WITH_RETRIES_N(10, {
    const auto rib = subscribedRib.rlock();
    ASSERT_EVENTUALLY_TRUE(rib->has_value());
    EXPECT_EVENTUALLY_TRUE((*rib)->rib_entries()->contains(prefix));
  });

  const auto fsdbPort = fsdbServer_->getFsdbPort();

  fsdbServer_.reset();
  WITH_RETRIES_N(10, {
    EXPECT_EVENTUALLY_FALSE(fsdbSyncer_->isIncrementalPublicationReady());
  });

  const auto replacementPrefixBatch =
      PrefixPathIds{{kV4Prefix2, kDefaultPathID}};
  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture(
      /*numRibEntriesToProgram=*/2);
  sendWithdrawal(prefixBatch, eBgpPeer1_);
  sendAnnouncement(replacementPrefixBatch, eBgpPeer1_, attr_);
  ribFuture.wait();
  WITH_RETRIES_N(
      100, { EXPECT_EVENTUALLY_TRUE(isCanonicalRibUpdateQueueEmpty()); });

  fsdbServer_ = std::make_unique<fboss::fsdb::test::FsdbTestServer>(fsdbPort);

  WITH_RETRIES_N(10, {
    EXPECT_EVENTUALLY_TRUE(fsdbSyncer_->isIncrementalPublicationReady());
    const auto rib = subscribedRib.rlock();
    ASSERT_EVENTUALLY_TRUE(rib->has_value());
    EXPECT_EVENTUALLY_FALSE((*rib)->rib_entries()->contains(prefix));
    EXPECT_EVENTUALLY_TRUE((*rib)->rib_entries()->contains(
        folly::IPAddress::networkToString(kV4Prefix2)));
  });
}

TEST_F(CanonicalRibFsdbFixture, WithdrawsPublishedEntryWithoutBestPath) {
  auto subscribedRib = fsdbSubscriber_->subscribe(
      fsdbSubscriber_->getRootStatePath().bgp().canonicalRib());
  rib_->setFibBatchTime(milliseconds(2));

  installNoBestPathPolicy(kV4Prefix1);

  const auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  sendInitialPathComputation();
  fibFuture.wait();
  waitForFsdbPublisherConnected();

  const auto prefix = folly::IPAddress::networkToString(kV4Prefix1);
  WITH_RETRIES_N(10, {
    const auto rib = subscribedRib.rlock();
    ASSERT_EVENTUALLY_TRUE(rib->has_value());
    ASSERT_EVENTUALLY_TRUE((*rib)->rib_entries()->contains(prefix));
    const auto& entry = (*rib)->rib_entries()->at(prefix);
    EXPECT_EVENTUALLY_FALSE(entry.best_path().has_value());
    EXPECT_EVENTUALLY_FALSE(entry.paths()->empty());
  });

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendWithdrawal(prefixBatch, eBgpPeer1_);
  ribFuture.wait();

  WITH_RETRIES_N(10, {
    const auto rib = subscribedRib.rlock();
    ASSERT_EVENTUALLY_TRUE(rib->has_value());
    EXPECT_EVENTUALLY_FALSE((*rib)->rib_entries()->contains(prefix));
  });
  WITH_RETRIES({
    EXPECT_EVENTUALLY_TRUE(
        rib_->getRibEntryForPrefix(std::make_unique<std::string>(prefix))
            .empty());
  });
}

TEST_F(CanonicalRibFsdbFixture, WithdrawsPublishedBestPathEntry) {
  auto subscribedRib = fsdbSubscriber_->subscribe(
      fsdbSubscriber_->getRootStatePath().bgp().canonicalRib());
  rib_->setFibBatchTime(milliseconds(2));

  const auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  auto fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  sendInitialPathComputation();
  fibFuture.wait();
  waitForFsdbPublisherConnected();

  const auto prefix = folly::IPAddress::networkToString(kV4Prefix1);
  WITH_RETRIES_N(10, {
    const auto rib = subscribedRib.rlock();
    ASSERT_EVENTUALLY_TRUE(rib->has_value());
    ASSERT_EVENTUALLY_TRUE((*rib)->rib_entries()->contains(prefix));
    EXPECT_EVENTUALLY_TRUE(
        (*rib)->rib_entries()->at(prefix).best_path().has_value());
  });

  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(prefixBatch, eBgpPeer1_);
  fibFuture.wait();

  WITH_RETRIES_N(10, {
    const auto rib = subscribedRib.rlock();
    ASSERT_EVENTUALLY_TRUE(rib->has_value());
    EXPECT_EVENTUALLY_FALSE((*rib)->rib_entries()->contains(prefix));
  });
}

/*
 * End-to-end publish path with publish_partial_drain_state_to_fsdb enabled.
 * Drives the Rib through a partial-drain transition (entry then exit) and
 * verifies both edges publish a populated TPartialDrainState (never nullopt):
 * is_partially_drained=true on entry, =false on exit. Mirrors
 * PartialDrainStatusReflectsRibState (RibTest.cpp) but under RibFsdbFixture so
 * a real FSDB syncer is wired in.
 */
TEST_F(RibFsdbFixture, PartialDrainStatePublishedToFsdbOnTransition) {
  FLAGS_publish_partial_drain_state_to_fsdb = true;
  SCOPE_EXIT {
    FLAGS_publish_partial_drain_state_to_fsdb = false;
  };

  auto subscribedState = fsdbSubscriber_->subscribe(
      fsdbSubscriber_->getRootStatePath().bgp().partialDrainState());

  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  rib_->setFibBatchTime(milliseconds(2));

  /*
   * Drain the initial-dump messages (RibInitialAnnouncementStart +
   * RibOutAnnouncement with initialDump=true).
   */
  WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(ribOutQ_.size(), 2); });
  REPEAT_N(2, folly::coro::blockingWait(ribOutQ_.pop()));

  // Install one path so any mnh > 1 policy violates → partial drain.
  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  fibFuture.wait();

  WITH_RETRIES({ ASSERT_EVENTUALLY_GE(ribOutQ_.size(), 1); });
  folly::coro::blockingWait(ribOutQ_.pop());

  /*
   * Inject CPS policy: mnh=3 + drain_on_min_nexthop_violation. The single
   * installed path violates mnh=3, so the prefix enters partial drain and
   * drainedPrefixCount_ flips 0 → 1 → setPartialDrainState(state) fires.
   */
  TPathSelector tPathSelector;
  tPathSelector.bgp_native_path_selection_min_nexthop() = 3;
  tPathSelector.drain_on_min_nexthop_violation() = true;

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  WITH_RETRIES_N(5, {
    auto stateLk = subscribedState.rlock();
    ASSERT_EVENTUALLY_TRUE(stateLk->has_value());
    EXPECT_EVENTUALLY_TRUE(
        *(*stateLk)->partial_drain_state()->is_partially_drained());
    EXPECT_EVENTUALLY_EQ(
        *(*stateLk)->partial_drain_state()->num_affected_prefixes(), 1);
    EXPECT_EVENTUALLY_EQ(
        *(*stateLk)->partial_drain_state()->partial_drain_transition_count(),
        1);
    /*
     * On a drain-status transition, RibDC::enqueuePartialDrainState builds
     * the drained-prefix snapshot on demand (getPartialDrainState scans
     * ribEntries_ for getIsPartialDrain() entries) and publishes it
     * alongside the device summary. One path was installed for kV4Prefix1
     * before the policy injection.
     *
     * min_capacity (TCapacity) carries the trigger criterion: this drain
     * was triggered by an MNH violation, so the union must hold
     * next_hop_count == 3 (the configured
     * bgp_native_path_selection_min_nexthop) and the agg_lbw_bps arm must NOT
     * be set. Pinned here so a future refactor of buildPartialDrainPrefixEntry
     * that mis-selects the union arm or forgets to plumb mnhThreshold_
     * end-to-end (RibPolicy → RibEntry → FSDB) fails this test.
     */
    ASSERT_EVENTUALLY_EQ((*stateLk)->drained_prefixes()->size(), 1);
    EXPECT_EVENTUALLY_EQ(
        *(*stateLk)->drained_prefixes()->at(0).min_capacity()->next_hop_count(),
        3);
    EXPECT_EVENTUALLY_FALSE((*stateLk)
                                ->drained_prefixes()
                                ->at(0)
                                .min_capacity()
                                ->agg_lbw_bps()
                                .has_value());
    /*
     * current_capacity mirrors the trigger criterion: MNH drain → the
     * next_hop_count arm holds the current nexthop count (1), and the
     * agg_lbw_bps arm is unset. Pins buildPartialDrainPrefixEntry's arm
     * selection for the current-value union end-to-end.
     */
    EXPECT_EVENTUALLY_EQ(
        *(*stateLk)
             ->drained_prefixes()
             ->at(0)
             .current_capacity()
             ->next_hop_count(),
        1);
    EXPECT_EVENTUALLY_FALSE((*stateLk)
                                ->drained_prefixes()
                                ->at(0)
                                .current_capacity()
                                ->agg_lbw_bps()
                                .has_value());
  })

  /*
   * Withdraw the only path → drainedPrefixCount_ flips 1 → 0. The exit edge
   * publishes a populated is_partially_drained=false snapshot (not nullopt), so
   * the node stays present with an empty drained set and transition_count=2.
   */
  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(prefixBatch, eBgpPeer1_);
  fibFuture.wait();

  WITH_RETRIES_N(5, {
    auto stateLk = subscribedState.rlock();
    ASSERT_EVENTUALLY_TRUE(stateLk->has_value());
    EXPECT_EVENTUALLY_FALSE(
        *(*stateLk)->partial_drain_state()->is_partially_drained());
    EXPECT_EVENTUALLY_EQ(
        *(*stateLk)->partial_drain_state()->num_affected_prefixes(), 0);
    EXPECT_EVENTUALLY_EQ(
        *(*stateLk)->partial_drain_state()->partial_drain_transition_count(),
        2);
    EXPECT_EVENTUALLY_TRUE((*stateLk)->drained_prefixes()->empty());
  })
}

/*
 * Initial-publish path with no drain transition. The initial state is computed
 * from that FIB pass rather than hard-coded false; this test exercises the case
 * where the pass finds no partially drained prefixes. The first /bgp snapshot
 * must therefore contain is_partially_drained=false, transition_count=0, and an
 * empty drained set.
 */
TEST_F(RibFsdbFixture, PartialDrainStateInitialFalsePublishedWhenNeverDrained) {
  FLAGS_publish_partial_drain_state_to_fsdb = true;
  SCOPE_EXIT {
    FLAGS_publish_partial_drain_state_to_fsdb = false;
  };

  folly::Synchronized<std::vector<fboss::fsdb::BgpData>> bgpUpdates;
  fboss::fsdb::FsdbPubSubManager rootSubscriber("bgp-root-subscriber");
  rootSubscriber.addStatePathSubscription(
      std::vector<std::string>{"bgp"},
      [](fboss::fsdb::SubscriptionState /*oldState*/,
         fboss::fsdb::SubscriptionState /*newState*/,
         std::optional<bool> /*initialSyncHasData*/) {},
      [&bgpUpdates](fboss::fsdb::OperState state) {
        if (state.contents()) {
          bgpUpdates.wlock()->push_back(
              apache::thrift::BinarySerializer::deserialize<
                  fboss::fsdb::BgpData>(*state.contents()));
        }
      });
  auto subscribedState = fsdbSubscriber_->subscribe(
      fsdbSubscriber_->getRootStatePath().bgp().partialDrainState());

  EXPECT_FALSE(isFsdbSyncerStarted());
  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  EXPECT_TRUE(isFsdbSyncerStarted());
  rib_->setFibBatchTime(milliseconds(2));

  WITH_RETRIES_N(5, {
    auto updates = bgpUpdates.rlock();
    ASSERT_EVENTUALLY_FALSE(updates->empty());
    const auto& initialSnapshot = updates->front();
    ASSERT_EVENTUALLY_TRUE(initialSnapshot.partialDrainState().has_value());
    EXPECT_EVENTUALLY_FALSE(*initialSnapshot.partialDrainState()
                                 ->partial_drain_state()
                                 ->is_partially_drained());
    EXPECT_EVENTUALLY_EQ(
        0,
        *initialSnapshot.partialDrainState()
             ->partial_drain_state()
             ->num_affected_prefixes());
    EXPECT_EVENTUALLY_TRUE(
        initialSnapshot.partialDrainState()->drained_prefixes()->empty());
  })

  WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(ribOutQ_.size(), 2); });
  REPEAT_N(2, folly::coro::blockingWait(ribOutQ_.pop()));

  /*
   * Install a path with no drain-triggering policy, so the device never enters
   * partial drain.
   */
  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  fibFuture.wait();

  /*
   * The first completed FIB pass publishes a positive
   * is_partially_drained=false snapshot without any transition: node present,
   * no affected prefixes, empty drained set, and transition_count still 0 (the
   * initial publish does not bump the enter/exit counter).
   */
  WITH_RETRIES_N(5, {
    auto stateLk = subscribedState.rlock();
    ASSERT_EVENTUALLY_TRUE(stateLk->has_value());
    EXPECT_EVENTUALLY_FALSE(
        *(*stateLk)->partial_drain_state()->is_partially_drained());
    EXPECT_EVENTUALLY_EQ(
        *(*stateLk)->partial_drain_state()->num_affected_prefixes(), 0);
    EXPECT_EVENTUALLY_EQ(
        *(*stateLk)->partial_drain_state()->partial_drain_transition_count(),
        0);
    EXPECT_EVENTUALLY_TRUE((*stateLk)->drained_prefixes()->empty());
  })
}

/*
 * Disabled (default) path: with publish_partial_drain_state_to_fsdb off,
 * enqueuePartialDrainState is a no-op on both edges. Drives the same drain
 * entry/exit as PartialDrainStatePublishedToFsdbOnTransition and asserts the
 * FSDB node stays absent (!has_value()) throughout.
 */
TEST_F(RibFsdbFixture, PartialDrainStateNotPublishedWhenFlagDisabled) {
  ASSERT_FALSE(FLAGS_publish_partial_drain_state_to_fsdb);

  auto subscribedState = fsdbSubscriber_->subscribe(
      fsdbSubscriber_->getRootStatePath().bgp().partialDrainState());

  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  rib_->setFibBatchTime(milliseconds(2));

  WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(ribOutQ_.size(), 2); });
  REPEAT_N(2, folly::coro::blockingWait(ribOutQ_.pop()));

  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attr_);
  fibFuture.wait();

  WITH_RETRIES({ ASSERT_EVENTUALLY_GE(ribOutQ_.size(), 1); });
  folly::coro::blockingWait(ribOutQ_.pop());

  /*
   * mnh=3 with a single installed path → prefix enters partial drain
   * (drainedPrefixCount_ 0 → 1). Flag off → no publish.
   */
  TPathSelector tPathSelector;
  tPathSelector.bgp_native_path_selection_min_nexthop() = 3;
  tPathSelector.drain_on_min_nexthop_violation() = true;

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  WITH_RETRIES_N(5, {
    auto stateLk = subscribedState.rlock();
    EXPECT_EVENTUALLY_FALSE(stateLk->has_value());
  })

  /*
   * Withdraw the only path → drainedPrefixCount_ flips 1 → 0. Still off, so the
   * exit edge is also a no-op and the node remains absent.
   */
  fibFuture = fib_->getFibProgramFuture();
  sendWithdrawal(prefixBatch, eBgpPeer1_);
  fibFuture.wait();

  WITH_RETRIES_N(5, {
    auto stateLk = subscribedState.rlock();
    EXPECT_EVENTUALLY_FALSE(stateLk->has_value());
  })
}

/*
 * End-to-end gating test for the LBW-violation branch of the FSDB
 * partial-drain publish. Mirrors PartialDrainStatePublishedToFsdbOnTransition
 * but triggers the drain via bgp_min_aggregate_lbw_bps instead of
 * bgp_native_path_selection_min_nexthop. Verifies the published
 * TPartiallyDrainedPrefix carries min_capacity.agg_lbw_bps() set
 * to the configured threshold (and the next_hop_count union arm unset).
 *
 * Pinned end-to-end so a future regression in
 * RibBase::buildPartialDrainPrefixEntry that mis-selects the union arm — or
 * a missed aggLbwBpsThreshold_ plumbing step (RibPolicy → RibEntry → FSDB)
 * — fails here.
 */
TEST_F(RibFsdbFixture, PartialDrainStatePublishedWithLbwThresholdOnTransition) {
  FLAGS_publish_partial_drain_state_to_fsdb = true;
  SCOPE_EXIT {
    FLAGS_publish_partial_drain_state_to_fsdb = false;
  };

  auto subscribedState = fsdbSubscriber_->subscribe(
      fsdbSubscriber_->getRootStatePath().bgp().partialDrainState());

  auto fibFuture = fib_->getFibProgramFuture();
  sendInitialPathComputation();
  fibFuture.wait();
  rib_->setFibBatchTime(milliseconds(2));

  /*
   * Drain the initial-dump messages (RibInitialAnnouncementStart +
   * RibOutAnnouncement with initialDump=true).
   */
  WITH_RETRIES({ ASSERT_EVENTUALLY_EQ(ribOutQ_.size(), 2); });
  REPEAT_N(2, folly::coro::blockingWait(ribOutQ_.pop()));

  /*
   * Build an LBW-capable attribute path: 10 Gbps via non-transitive LBW
   * community on AS1. Any policy threshold above 10 Gbps will violate the
   * aggregate-LBW check after the single path is multipath-selected.
   * (RibPolicy.cpp converts community bytes/sec to bits/sec via `*8` and
   * sums across multipaths before comparing against bgpNativeMinAggLbwbps_.)
   */
  auto attrLbw =
      std::make_shared<facebook::bgp::BgpPath>(*buildBgpPathFields(4, 4, 4, 4));
  attrLbw->setNonTransitiveLbwExtCommunity(uint16_t(kLocalAs1), kLbw10G);
  attrLbw->publish();

  auto prefixBatch = PrefixPathIds{{kV4Prefix1, kDefaultPathID}};
  fibFuture = fib_->getFibProgramFuture();
  sendAnnouncement(prefixBatch, eBgpPeer1_, attrLbw);
  fibFuture.wait();

  WITH_RETRIES({ ASSERT_EVENTUALLY_GE(ribOutQ_.size(), 1); });
  folly::coro::blockingWait(ribOutQ_.pop());

  /*
   * Inject CPS policy with bgp_min_aggregate_lbw_bps = 100 Gbps and
   * drain_on_min_nexthop_violation = true. The single installed path
   * provides 10 Gbps, so the aggregate falls below 100 Gbps and the prefix
   * enters partial drain via the LBW branch.
   *
   * Critical: bgp_native_path_selection_min_nexthop must NOT be set. When
   * MNH is set, PathSelector::overrideMultipathSelection returns before
   * reaching the LBW check (see the MNH branch in RibPolicy.cpp), which
   * would route the drain through the MNH arm instead.
   */
  constexpr int64_t kAggLbwBpsThreshold = 100LL * 1'000'000'000LL; // 100 Gbps
  TPathSelector tPathSelector;
  tPathSelector.bgp_min_aggregate_lbw_bps() = kAggLbwBpsThreshold;
  tPathSelector.drain_on_min_nexthop_violation() = true;

  auto ribFuture = rib_->getRibPrepareFibProgrammingFuture();
  sendPathSelectionPolicySet(
      createTPathSelectionPolicyWithPathSelector({kV4Prefix1}, tPathSelector));
  rib_->waitForPathSelectionPolicyUpdate();
  ribFuture.wait();

  WITH_RETRIES_N(5, {
    auto stateLk = subscribedState.rlock();
    ASSERT_EVENTUALLY_TRUE(stateLk->has_value());
    EXPECT_EVENTUALLY_TRUE(
        *(*stateLk)->partial_drain_state()->is_partially_drained());
    EXPECT_EVENTUALLY_EQ(
        *(*stateLk)->partial_drain_state()->num_affected_prefixes(), 1);
    EXPECT_EVENTUALLY_EQ(
        *(*stateLk)->partial_drain_state()->partial_drain_transition_count(),
        1);
    ASSERT_EVENTUALLY_EQ((*stateLk)->drained_prefixes()->size(), 1);
    /*
     * The drain trigger was LBW, so min_capacity (TCapacity) must hold
     * agg_lbw_bps == the configured threshold (100 Gbps) and the
     * next_hop_count arm must NOT be set. Mirrors the inverse assertion in
     * the MNH test above.
     */
    EXPECT_EVENTUALLY_EQ(
        *(*stateLk)->drained_prefixes()->at(0).min_capacity()->agg_lbw_bps(),
        kAggLbwBpsThreshold);
    EXPECT_EVENTUALLY_FALSE((*stateLk)
                                ->drained_prefixes()
                                ->at(0)
                                .min_capacity()
                                ->next_hop_count()
                                .has_value());
    /*
     * current_capacity mirrors the LBW criterion: the agg_lbw_bps arm holds
     * the CURRENT aggregate link bandwidth (not the threshold) — one path of
     * kLbw10G bytes/sec * 8 = 10 Gbps, computed on demand by
     * RibDC::buildPartialDrainPrefixEntry — and the next_hop_count arm is
     * unset. This is the behavioral evidence that the producer recomputes and
     * surfaces the current aggregate LBW, the gap yikailin's comment flagged.
     */
    EXPECT_EVENTUALLY_EQ(
        *(*stateLk)
             ->drained_prefixes()
             ->at(0)
             .current_capacity()
             ->agg_lbw_bps(),
        static_cast<int64_t>(kLbw10G * 8));
    EXPECT_EVENTUALLY_FALSE((*stateLk)
                                ->drained_prefixes()
                                ->at(0)
                                .current_capacity()
                                ->next_hop_count()
                                .has_value());
  })
}

} // namespace bgp
} // namespace facebook
