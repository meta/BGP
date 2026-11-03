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

#define AdjRib_TEST_FRIENDS                                                    \
  friend class UpdateGroupDetachedPeerTest;                                    \
  FRIEND_TEST(UpdateGroupDetachedPeerTest, ClonePackingListCreatesDeepCopy);   \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, ClonedPackingListContainsAllEntries);       \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, IsDFPReturnsTrueWhenAllConditionsMet);      \
  FRIEND_TEST(UpdateGroupDetachedPeerTest, IsDFPReturnsFalseWhenPLNotEmpty);   \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, IsDFPReturnsFalseWhenVersionsMismatch);     \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      CanWaitForGroupToRejoinReturnsFalseWhenVersionMismatchEvenIfPLEmpty);    \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      CanWaitForGroupToRejoinReturnsFalseWhenPLNotEmpty);                      \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      CanWaitForGroupToRejoinReturnsFalseWhenNoConsumer);                      \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      CanWaitForGroupToRejoinRequiresBothConsumersReady);                      \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      IsReadyToRejoinGroupReturnsFalseWhenGroupPLNotEmpty);                    \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, MaybeAcceptDSPPeerRechecksReadiness);       \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, CanWaitForGroupToRejoinReturnsTrue);        \
  FRIEND_TEST(UpdateGroupDetachedPeerTest, IsDFPReturnsFalseWhenGroupPLEmpty); \
  FRIEND_TEST(UpdateGroupDetachedPeerTest, IsDFPReturnsFalseWhenNoGroup);      \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, DetachSlowPeerSetsEgressEoRWhenFlagIsTrue); \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      DetachDoesNotSetEoRsPendingWhenGroupHasNone);                            \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, RibWalkDetachCopiesEgressEoRsPending);      \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      CanAnnounceDelegatesToGroupWhenUpdateGroupEnabled);                      \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, CanAnnounceUsesGroupRrClientSetting);       \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      CollapseLiteSkipsRibVersionCheckForInitDumpPeer);                        \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      CollapsePathSkipsRibVersionCheckForInitDumpPeer);                        \
  friend class UpdateGroupDetachLifecycleTest;                                 \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest, AcceptPeerSucceedsWithMatchingEntries);  \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest, AcceptPeerFailsWithMismatchingEntries);  \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      AcceptDSPWithMixedSharedAndClonedEntries);                               \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      AcceptDSPFailsWhenPostDetachEntryNotCloned);                             \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest, DSPPeerRejoinsWhenGroupConsumerReady);   \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest, DSPPeerRejoinsWhenPeerConsumerReady);    \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      CanAnnounceDelegatesToGroupWhenUpdateGroupEnabled);                      \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, CanAnnounceUsesGroupRrClientSetting);       \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      CanAnnounceEntrySkipsSuppressAsLoopWithUpdateGroup);                     \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      RouteRefreshDoesNotAdvanceJoinedPeerConsumer);                           \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      DetachCopiesEgressStatsAndCollapseReconciles);                           \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      CollapseDecrementsStatsOnWithdrawalDiscrepancy);                         \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      CollapseIncrementsStatsOnAnnouncementDiscrepancy);                       \
  FRIEND_TEST(UpdateGroupDetachLifecycleTest, DetachPeerSetsAllExpectedState); \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      UnregisterDetachedPeerCleansUpPerPeerLiteTreeEntries);                   \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      UnregisterDetachedPeerCleansUpPerPeerPathTreeEntries);                   \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      PeerAheadOfGroupTransitionsToReadyToJoin);                               \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      DownPeerDoesNotTransitionOnPeerUpdateState);                             \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      JoinedPeerDoesNotEnterDetachedRejoinPath);                               \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest, PendingRibDumpDefersDetachedRejoin);     \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      PeerAheadOfGroupDoesNotProceedOnChangelist);                             \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      DSPRejoinDeferredUntilGroupPackingListDrains);                           \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      AheadDepASelfPromotesWhenGroupHasNoSharingPeers);                        \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      SyncPeerDownSubtractsGroupShareFromGlobal);                              \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      DetachedPeerDownSubtractsOwnShareFromGlobal);                            \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      UnregisterDetachedAddPathPeerDecrementsMixedOwnershipPathCounts);

#define AdjRibOutGroup_TEST_FRIENDS                                            \
  friend class UpdateGroupDetachedPeerTest;                                    \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      HandleNoSyncPeersWithSharingDspKeepsConsumerRunning);                    \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, ShouldCloneFalseWhenPeerHasOwnEntry);       \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      ShouldCloneFalseWhenEntryAnnouncedAfterDetach);                          \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, ShouldCloneTrueWhenPeerWasSharingEntry);    \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, ShouldClonePathFalseWhenPeerHasOwnEntry);   \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      ShouldClonePathFalseWhenEntryAnnouncedAfterDetach);                      \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      ShouldClonePathTrueWhenPeerWasSharingEntry);                             \
  FRIEND_TEST(UpdateGroupDetachedPeerTest, CopyEntryForPeerCopiesAllFields);   \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, CopyEntryForPeerUsesGroupKeyWhenOnlyPeer);  \
  FRIEND_TEST(UpdateGroupDetachedPeerTest, LazyCloneNoDetachedPeersNoClone);   \
  FRIEND_TEST(UpdateGroupDetachedPeerTest, LazyCloneClonesForSharingPeerOnly); \
  FRIEND_TEST(UpdateGroupDetachedPeerTest, WithdrawClonesEntryToDetachedPeer); \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, WithdrawNoClonesWhenNoPeersDetached);       \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, WithdrawSkipsCloneWhenPeerHasOwnEntry);     \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      WithdrawSkipsCloneWhenEntryAnnouncedAfterDetach);                        \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, WithdrawMultiplePeersMixedCloneBehavior);   \
  FRIEND_TEST(UpdateGroupDetachedPeerTest, WithdrawClonesEntryAddPathMode);    \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, AnnouncementClonesEntryToDetachedPeer);     \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, AnnouncementNoClonesWhenNoPeersDetached);   \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, AnnouncementSkipsCloneWhenPeerHasOwnEntry); \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      AnnouncementSkipsCloneWhenEntryAnnouncedAfterDetach);                    \
  FRIEND_TEST(UpdateGroupDetachedPeerTest, AnnouncementNoClonesForNewPrefix);  \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, AnnouncementClonesEntryAddPathMode);        \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, DetachSlowPeerSetsEgressEoRWhenFlagIsTrue); \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      DetachDoesNotSetEoRsPendingWhenGroupHasNone);                            \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, RibWalkDetachCopiesEgressEoRsPending);      \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, DetachDoesNotResendCommittedEgressEoR);     \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      CollapseLiteSkipsRibVersionCheckForInitDumpPeer);                        \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest,                                             \
      CollapsePathSkipsRibVersionCheckForInitDumpPeer);                        \
  friend class UpdateGroupDetachLifecycleTest;                                 \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest, AcceptPeerSucceedsWithMatchingEntries);  \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest, AcceptPeerFailsWithMismatchingEntries);  \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      CheckAndAcceptOnlyProcessesReadyToJoinPeers);                            \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      CheckAndAcceptMultiplePeersMixedResults);                                \
  FRIEND_TEST(UpdateGroupDetachLifecycleTest, AcceptDFPWithSharedEntriesOnly); \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      AcceptDSPWithMixedSharedAndClonedEntries);                               \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      AcceptDSPFailsWhenPostDetachEntryNotCloned);                             \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      DetachCopiesEgressStatsAndCollapseReconciles);                           \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      CollapseDecrementsStatsOnWithdrawalDiscrepancy);                         \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      CollapseIncrementsStatsOnAnnouncementDiscrepancy);                       \
  FRIEND_TEST(UpdateGroupDetachLifecycleTest, DetachPeerSetsAllExpectedState); \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      PeerAheadOfGroupTransitionsToReadyToJoin);                               \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      DownPeerDoesNotTransitionOnPeerUpdateState);                             \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      PeerAheadOfGroupDoesNotProceedOnChangelist);                             \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      DSPRejoinDeferredUntilGroupPackingListDrains);                           \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, PromoteLiteMovesPeerEntryToGroupKey);       \
  FRIEND_TEST(UpdateGroupDetachedPeerTest, PromoteLiteDeletesGroupOnlyEntry);  \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, PromoteLitePeerOnlyEntryMovedToGroupKey);   \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachedPeerTest, PromotePathMovesPeerPathsToGroupKey);       \
  FRIEND_TEST(UpdateGroupDetachedPeerTest, PromotePathDeletesGroupOnlyEntry);  \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      SyncPeerDownSubtractsGroupShareFromGlobal);                              \
  FRIEND_TEST(                                                                 \
      UpdateGroupDetachLifecycleTest,                                          \
      DetachedPeerDownSubtractsOwnShareFromGlobal);

#include <fmt/core.h>
#include <folly/coro/BlockingWait.h>
#include <folly/io/async/EventBase.h>
#include <folly/logging/LoggerDB.h>
#include <folly/logging/test/TestLogHandler.h>

#include <fb303/ThreadCachedServiceData.h>
#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibGroup.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibStructs.h"
#include "neteng/fboss/bgp/cpp/adjrib/ShadowRibTypes.h"
#include "neteng/fboss/bgp/cpp/changeTracker/ChangeTracker.h"
#include "neteng/fboss/bgp/cpp/changeTracker/TrackableObject.h"
#include "neteng/fboss/bgp/cpp/common/BgpPath.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
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

bool logMessagesContain(
    const std::vector<std::pair<folly::LogMessage, const folly::LogCategory*>>&
        messages,
    const std::string& searchStr) {
  for (const auto& [msg, cat] : messages) {
    if (msg.getMessage().find(searchStr) != std::string::npos) {
      return true;
    }
  }
  return false;
}
} // namespace

using ::testing::HasSubstr;

constexpr auto kPeerIpFmt = "10.0.0.{}";

class UpdateGroupDetachedPeerTest : public ::testing::Test {
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
  }

  void TearDown() override {
    for (auto& peer : peers_) {
      group_->unregisterPeer(peer);
    }

    /* Drain asyncScope_ cooperatively before destroying the group */
    folly::coro::blockingWait(group_->drainAsyncScope());
    group_.reset();
    peers_.clear();
    evb_.reset();
  }

  /*
   * Create a minimal AdjRib and register it at the given bit position.
   */
  std::shared_ptr<AdjRib> createAndRegisterPeer(uint64_t bit) {
    auto peerId = nettools::bgplib::BgpPeerId(
        folly::IPAddress(fmt::format(kPeerIpFmt, bit + 1)),
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
    adjRib->enableUpdateGroup_ = true;
    /*
     * Map the peer onto its bit without going through registerPeer(): these
     * fixtures need a peer that is registered but neither in sync nor
     * detached, and no public entry point produces that state. Reaching in
     * directly is possible because both fixtures are friends of
     * AdjRibOutGroup.
     */
    adjRib->setGroupBitPosition(bit);
    group_->bitToAdjRibs_[bit] = adjRib;
    peers_.push_back(adjRib);
    return adjRib;
  }

  /*
   * Register a real in-sync peer at the given bit so the group's packing list
   * accepts entries (the group only queues when it has an in-sync peer).
   */
  std::shared_ptr<AdjRib> addInSyncPeer(uint64_t bit) {
    auto adjRib = createAndRegisterPeer(bit);
    group_->markPeerInSync(adjRib);
    return adjRib;
  }

  std::unique_ptr<folly::EventBase> evb_;
  std::shared_ptr<AdjRibOutGroup> group_;
  std::vector<std::shared_ptr<AdjRib>> peers_;
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ_{
      nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<AdjRib::ObservableMessageT> observerQ_;
};

TEST_F(UpdateGroupDetachedPeerTest, InitiallyNoPeersDetached) {
  EXPECT_TRUE(group_->getDetachedPeers().empty());
}

TEST_F(UpdateGroupDetachedPeerTest, MarkPeerDetached) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  group_->markPeerDetached(adjRib0);

  const auto& peers = group_->getDetachedPeers();
  EXPECT_TRUE(peers.contains(adjRib0));
  EXPECT_FALSE(peers.contains(adjRib1));
}

TEST_F(UpdateGroupDetachedPeerTest, MarkPeerConverged) {
  auto adjRib0 = createAndRegisterPeer(0);
  group_->markPeerDetached(adjRib0);
  EXPECT_TRUE(group_->getDetachedPeers().contains(adjRib0));

  group_->markPeerInSync(adjRib0);
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib0));
}

TEST_F(UpdateGroupDetachedPeerTest, MultiplePeersDetached) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  auto adjRib2 = createAndRegisterPeer(2);
  auto adjRib3 = createAndRegisterPeer(3);
  group_->markPeerDetached(adjRib0);
  group_->markPeerDetached(adjRib2);
  group_->markPeerDetached(adjRib3);

  const auto& peers = group_->getDetachedPeers();
  EXPECT_TRUE(peers.contains(adjRib0));
  EXPECT_FALSE(peers.contains(adjRib1));
  EXPECT_TRUE(peers.contains(adjRib2));
  EXPECT_TRUE(peers.contains(adjRib3));
}

TEST_F(UpdateGroupDetachedPeerTest, ConvergeOnePeerLeavesOthers) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  group_->markPeerDetached(adjRib0);
  group_->markPeerDetached(adjRib1);

  group_->markPeerInSync(adjRib0);

  const auto& peers = group_->getDetachedPeers();
  EXPECT_FALSE(peers.contains(adjRib0));
  EXPECT_TRUE(peers.contains(adjRib1));
}

TEST_F(UpdateGroupDetachedPeerTest, MarkDetachedIdempotent) {
  auto adjRib0 = createAndRegisterPeer(0);
  group_->markPeerDetached(adjRib0);
  group_->markPeerDetached(adjRib0);

  EXPECT_TRUE(group_->getDetachedPeers().contains(adjRib0));

  group_->markPeerInSync(adjRib0);
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib0));
}

TEST_F(UpdateGroupDetachedPeerTest, MarkConvergedIdempotent) {
  auto adjRib0 = createAndRegisterPeer(0);
  group_->markPeerInSync(adjRib0);
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib0));
}

TEST_F(UpdateGroupDetachedPeerTest, GetDetachedPeersReflectsState) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  auto adjRib2 = createAndRegisterPeer(2);
  auto adjRib3 = createAndRegisterPeer(3);
  group_->markPeerDetached(adjRib1);
  group_->markPeerDetached(adjRib3);

  const auto& peers = group_->getDetachedPeers();
  EXPECT_TRUE(peers.contains(adjRib1));
  EXPECT_TRUE(peers.contains(adjRib3));
  EXPECT_FALSE(peers.contains(adjRib0));
  EXPECT_FALSE(peers.contains(adjRib2));
}

TEST_F(UpdateGroupDetachedPeerTest, GetDetachedPeersAllCleared) {
  auto adjRib0 = createAndRegisterPeer(0);
  group_->markPeerDetached(adjRib0);
  group_->markPeerInSync(adjRib0);

  EXPECT_TRUE(group_->getDetachedPeers().empty());
}

TEST_F(UpdateGroupDetachedPeerTest, MarkPeerDetachedClearsSyncBit) {
  auto adjRib0 = createAndRegisterPeer(0);
  group_->markPeerInSync(adjRib0);
  EXPECT_TRUE(group_->isPeerInSync(0));

  group_->markPeerDetached(adjRib0);
  EXPECT_FALSE(group_->isPeerInSync(0));
}

TEST_F(UpdateGroupDetachedPeerTest, MarkPeerInSyncSetsSyncBit) {
  auto adjRib0 = createAndRegisterPeer(0);
  group_->markPeerDetached(adjRib0);
  EXPECT_FALSE(group_->isPeerInSync(0));

  group_->markPeerInSync(adjRib0);
  EXPECT_TRUE(group_->isPeerInSync(0));
}

TEST_F(UpdateGroupDetachedPeerTest, ClonePackingListCreatesDeepCopy) {
  auto adjRib = createAndRegisterPeer(0);

  // The group only queues to its packing list when it has an in-sync peer.
  addInSyncPeer(1);

  // Populate group's packing list with one entry
  auto attrs = std::make_shared<BgpPath>(BgpPathFields());
  attrs->setLocalPref(100);
  std::shared_ptr<const BgpPath> constAttrs = attrs;
  const folly::CIDRNetwork prefix1{folly::IPAddress("10.0.0.0"), 24};
  auto pp1 = std::make_pair(prefix1, kPlaceholderPathID);

  group_->tryUpdateAttrToPrefixMapForGroup(pp1, nullptr, constAttrs);
  ASSERT_EQ(group_->getAttrToPrefixMap().size(), 1);

  // Clone via the actual API
  group_->clonePackingListForPeer(adjRib);
  auto& detachedPl = adjRib->attrToPrefixMap_;
  ASSERT_EQ(detachedPl.size(), 1);

  // Verify the cloned packing list contains prefix1
  const auto& clonedPrefixSet = detachedPl.begin()->second;
  EXPECT_EQ(clonedPrefixSet.count(pp1), 1);

  const folly::CIDRNetwork prefix2{folly::IPAddress("20.0.0.0"), 24};
  auto pp2 = std::make_pair(prefix2, kPlaceholderPathID);
  const folly::CIDRNetwork prefix3{folly::IPAddress("30.0.0.0"), 24};
  auto pp3 = std::make_pair(prefix3, kPlaceholderPathID);

  // Modify group's packing list and verify detached is unaffected
  {
    group_->tryUpdateAttrToPrefixMapForGroup(pp2, nullptr, constAttrs);

    const auto& groupPrefixSet = group_->getAttrToPrefixMap().begin()->second;
    EXPECT_EQ(groupPrefixSet.count(pp1), 1);
    EXPECT_EQ(groupPrefixSet.count(pp2), 1);
    EXPECT_EQ(groupPrefixSet.count(pp3), 0);
    EXPECT_EQ(clonedPrefixSet.count(pp1), 1);
    EXPECT_EQ(clonedPrefixSet.count(pp2), 0);
    EXPECT_EQ(clonedPrefixSet.count(pp3), 0);
  }

  // Modify detached packing list and verify group is unaffected
  {
    BgpPathWithAfi key = detachedPl.begin()->first;
    detachedPl[key].insert(pp3);

    EXPECT_EQ(detachedPl.begin()->second.count(pp1), 1);
    EXPECT_EQ(detachedPl.begin()->second.count(pp2), 0);
    EXPECT_EQ(detachedPl.begin()->second.count(pp3), 1);
    const auto& groupPrefixSet = group_->getAttrToPrefixMap().begin()->second;
    EXPECT_EQ(groupPrefixSet.count(pp1), 1);
    EXPECT_EQ(groupPrefixSet.count(pp2), 1);
    EXPECT_EQ(groupPrefixSet.count(pp3), 0);
  }
}

TEST_F(UpdateGroupDetachedPeerTest, ClonedPackingListContainsAllEntries) {
  auto adjRib = createAndRegisterPeer(0);

  // The group only queues to its packing list when it has an in-sync peer.
  addInSyncPeer(1);

  // Populate group's packing list with multiple attr groups
  auto attrs1 = std::make_shared<BgpPath>(BgpPathFields());
  attrs1->setLocalPref(100);
  std::shared_ptr<const BgpPath> constAttrs1 = attrs1;

  auto attrs2 = std::make_shared<BgpPath>(BgpPathFields());
  attrs2->setLocalPref(200);
  std::shared_ptr<const BgpPath> constAttrs2 = attrs2;

  const folly::CIDRNetwork prefix1{folly::IPAddress("10.0.0.0"), 24};
  const folly::CIDRNetwork prefix2{folly::IPAddress("20.0.0.0"), 24};
  const folly::CIDRNetwork prefix3{folly::IPAddress("30.0.0.0"), 24};

  // Two prefixes under attrs1, one under attrs2
  group_->tryUpdateAttrToPrefixMapForGroup(
      std::make_pair(prefix1, kPlaceholderPathID), nullptr, constAttrs1);
  group_->tryUpdateAttrToPrefixMapForGroup(
      std::make_pair(prefix2, kPlaceholderPathID), nullptr, constAttrs1);
  group_->tryUpdateAttrToPrefixMapForGroup(
      std::make_pair(prefix3, kPlaceholderPathID), nullptr, constAttrs2);

  ASSERT_EQ(group_->getAttrToPrefixMap().size(), 2); // 2 attr groups

  // Clone via the actual API
  group_->clonePackingListForPeer(adjRib);
  const auto& detachedPl = adjRib->attrToPrefixMap_;

  // Verify same number of attr groups
  EXPECT_EQ(detachedPl.size(), 2);

  // Verify total prefix count and shared_ptr identity
  size_t groupPrefixCount = 0;
  for (const auto& [groupKey, groupPrefixSet] : group_->getAttrToPrefixMap()) {
    groupPrefixCount += groupPrefixSet.size();
    auto it = detachedPl.find(groupKey);
    ASSERT_NE(it, detachedPl.end());
    EXPECT_EQ(groupPrefixSet.size(), it->second.size());
    // The shared_ptr<const BgpPath> in the key should point to the same object
    EXPECT_EQ(groupKey.attrs.get(), it->first.attrs.get());
  }
  EXPECT_EQ(groupPrefixCount, 3);
}

TEST_F(UpdateGroupDetachedPeerTest, DetachSinglePeerGroupSkipsDetachment) {
  auto adjRib = createAndRegisterPeer(0);
  adjRib->setPeerState(PeerUpdateState::JOINED_BLOCKED);
  group_->markPeerInSync(adjRib);
  group_->markPeerBlocked(adjRib);

  auto slowPeersBefore = getNumSlowPeersCounter();

  auto& messages = subscribeToLogMessages("", folly::LogLevel::INFO);
  messages.clear();

  group_->detachSlowPeer(adjRib);

  // Peer should NOT be detached — still in sync, not diverged
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib));
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::JOINED_BLOCKED);

  // Verify log message
  bool foundSkipLog = false;
  for (const auto& [msg, cat] : messages) {
    if (msg.getMessage().find(
            "is the only synced member, skipping detachment") !=
        std::string::npos) {
      foundSkipLog = true;
      break;
    }
  }
  EXPECT_TRUE(foundSkipLog);

  /*
   * Counter should increment — slow peer was detected even though
   * detachment was skipped (single peer)
   */
  EXPECT_EQ(slowPeersBefore + 1, getNumSlowPeersCounter());
}

TEST_F(UpdateGroupDetachedPeerTest, DetachPeerInMultiPeerGroup) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);

  // Set up change tracker and consumer bitmaps
  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;

  // Wire the tracker and let the group create + register its own consumer
  group_->setChangeListTracker(changeTracker, addPathBitmap, nonAddPathBitmap);
  group_->registerGroupConsumer();
  auto groupConsumer = group_->getChangeListConsumer();

  /*
   * Publish items to the change list so the group consumer has a non-null
   * marker
   */
  ShadowRibEntry entry1;
  entry1.prefix = folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24};
  auto trackable1 =
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(entry1));
  ShadowRibEntry entry2;
  entry2.prefix = folly::CIDRNetwork{folly::IPAddress("20.0.0.0"), 24};
  auto trackable2 =
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(entry2));
  changeTracker->publishChange(trackable1.get());
  changeTracker->publishChange(trackable2.get());

  // Group consumer should be pended at a non-null marker
  ASSERT_NE(groupConsumer->getMarker(), nullptr);

  // Set peer 0 to JOINED_BLOCKED, peer 1 to JOINED_RUNNING
  adjRib0->setPeerState(PeerUpdateState::JOINED_BLOCKED);
  adjRib1->setPeerState(PeerUpdateState::JOINED_RUNNING);

  // Mark both peers in sync
  group_->markPeerInSync(adjRib0);
  group_->markPeerInSync(adjRib1);

  // Mark peer 0 as blocked
  group_->markPeerBlocked(adjRib0);
  EXPECT_TRUE(BitmapUtils::isBitSet(group_->getBlockedBitmap(), 0));

  // Verify no detached consumer before detachment
  EXPECT_EQ(adjRib0->getDetachedConsumer(), nullptr);

  auto& messages = subscribeToLogMessages("", folly::LogLevel::INFO);
  messages.clear();

  group_->detachSlowPeer(adjRib0);

  // Peer 0 should be detached and unblocked after detachment
  EXPECT_FALSE(group_->isPeerInSync(0));
  EXPECT_TRUE(group_->getDetachedPeers().contains(adjRib0));
  EXPECT_FALSE(BitmapUtils::isBitSet(group_->getBlockedBitmap(), 0));
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::DETACHED_BLOCKED);

  // Verify detached consumer was created at the same position as group consumer
  auto detachedConsumer = adjRib0->getDetachedConsumer();
  ASSERT_NE(detachedConsumer, nullptr);
  EXPECT_EQ(detachedConsumer->getMarker(), groupConsumer->getMarker());

  // Peer 1 should be unaffected
  EXPECT_TRUE(group_->isPeerInSync(1));
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib1));
  EXPECT_EQ(adjRib1->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_EQ(adjRib1->getDetachedConsumer(), nullptr);

  // Verify detachment success log
  bool foundDetachLog = false;
  for (const auto& [msg, cat] : messages) {
    if (msg.getMessage().find("detached successfully") != std::string::npos) {
      foundDetachLog = true;
      break;
    }
  }
  EXPECT_TRUE(foundDetachLog);

  // Clean up consumers before group destruction
  adjRib0->deactivateDetachedModeProcessing();
  groupConsumer->resetBitmap();
  groupConsumer->terminate();
  groupConsumer->deregisterFromTracker();
  group_->resetChangeListConsumer();
  groupConsumer.reset();
}

/*
 * numPeersDetachedAfterJoin_ counter tests
 */

TEST_F(UpdateGroupDetachedPeerTest, DetachedAfterJoinCountInitiallyZero) {
  EXPECT_EQ(group_->getNumPeersDetachedAfterJoin(), 0);
}

TEST_F(UpdateGroupDetachedPeerTest, DetachSlowPeerIncrementsDetachedAfterJoin) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  // Two in-sync members so detachSlowPeer does not skip (last-synced guard).
  group_->markPeerInSync(adjRib0);
  group_->markPeerInSync(adjRib1);
  group_->setLastSeenRibVersion(42);

  EXPECT_EQ(group_->getNumPeersDetachedAfterJoin(), 0);
  group_->detachSlowPeer(adjRib0);
  EXPECT_EQ(group_->getNumPeersDetachedAfterJoin(), 1);
  EXPECT_NE(adjRib0->getDetachedRibVersion(), 0);
}

TEST_F(UpdateGroupDetachedPeerTest, DeactivateDecrementsDetachedAfterJoin) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  group_->markPeerInSync(adjRib0);
  group_->markPeerInSync(adjRib1);
  group_->setLastSeenRibVersion(42);
  group_->detachSlowPeer(adjRib0);
  ASSERT_EQ(group_->getNumPeersDetachedAfterJoin(), 1);

  // Tearing down detached-mode processing (peer-down / rejoin) decrements.
  adjRib0->deactivateDetachedModeProcessing();
  EXPECT_EQ(group_->getNumPeersDetachedAfterJoin(), 0);
}

TEST_F(UpdateGroupDetachedPeerTest, InitDumpPeerDeactivateDoesNotDecrement) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  group_->markPeerInSync(adjRib0);
  group_->markPeerInSync(adjRib1);
  group_->setLastSeenRibVersion(42);
  group_->detachSlowPeer(adjRib0);
  ASSERT_EQ(group_->getNumPeersDetachedAfterJoin(), 1);

  /*
   * An init-dump peer has detachedRibVersion 0 and was never counted, so
   * deactivating it must leave the counter untouched.
   */
  auto initDumpPeer = createAndRegisterPeer(2);
  ASSERT_EQ(initDumpPeer->getDetachedRibVersion(), 0);
  initDumpPeer->deactivateDetachedModeProcessing();
  EXPECT_EQ(group_->getNumPeersDetachedAfterJoin(), 1);
}

TEST_F(UpdateGroupDetachedPeerTest, RejoinDecrementsDetachedAfterJoin) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  group_->markPeerInSync(adjRib0);
  group_->markPeerInSync(adjRib1);
  group_->setLastSeenRibVersion(42);

  // Detach a slow peer -> counted.
  group_->detachSlowPeer(adjRib0);
  ASSERT_EQ(group_->getNumPeersDetachedAfterJoin(), 1);

  /*
   * Drive a clean rejoin: with no diverged entries, collapse finds no
   * discrepancy, so the peer is accepted back into the group. Acceptance
   * deactivates detached processing, which decrements the counter.
   */
  adjRib0->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  auto accepted = group_->tryAcceptPeersToGroup({adjRib0});

  EXPECT_EQ(accepted.size(), 1);
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_EQ(group_->getNumPeersDetachedAfterJoin(), 0);
}

/*
 * Full lifecycle: a peer that detaches as a slow peer (counted), rejoins
 * (decremented), then becomes slow again and is moved to another group must
 * also be decremented on the move. Regression: movePeer cleared
 * detachedRibVersion directly without decrementing, leaking the count and
 * leaving handleNoSyncPeers frozen waiting for a peer that had moved out.
 */
TEST_F(UpdateGroupDetachedPeerTest, MovePeerDecrementsDetachedAfterJoin) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  // Two in-sync members so detachSlowPeer does not skip (last-synced guard).
  group_->markPeerInSync(adjRib0);
  group_->markPeerInSync(adjRib1);
  group_->setLastSeenRibVersion(42);
  ASSERT_EQ(group_->getNumPeersDetachedAfterJoin(), 0);

  // Detach as a slow peer -> counted.
  group_->detachSlowPeer(adjRib0);
  EXPECT_EQ(group_->getNumPeersDetachedAfterJoin(), 1);

  /*
   * Rejoin cleanly -> acceptance deactivates detached processing ->
   * decremented.
   */
  adjRib0->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  ASSERT_EQ(group_->tryAcceptPeersToGroup({adjRib0}).size(), 1);
  ASSERT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_EQ(group_->getNumPeersDetachedAfterJoin(), 0);

  // Detach as a slow peer again -> counted again.
  group_->detachSlowPeer(adjRib0);
  EXPECT_EQ(group_->getNumPeersDetachedAfterJoin(), 1);

  /*
   * Move the detached peer to another group -> must decrement the source
   * group's count (adjRib1 stays in sync, so the group is not frozen).
   */
  auto targetGroup = std::make_shared<AdjRibOutGroup>(*evb_, "target_group");
  group_->movePeers({adjRib0}, targetGroup);
  EXPECT_EQ(group_->getNumPeersDetachedAfterJoin(), 0);
  EXPECT_EQ(adjRib0->getUpdateGroup(), targetGroup);

  /*
   * The moved peer now lives in targetGroup; drop it from this fixture's
   * tracking and break the peer<->targetGroup ownership cycle so TearDown
   * (which unregisters tracked peers from group_) does not touch it.
   */
  targetGroup->unregisterPeer(adjRib0);
  adjRib0->setUpdateGroup(nullptr);
  peers_.erase(peers_.begin());
}

/*
 * Detaching a joined peer directly via detachPeer (e.g. policy re-evaluation)
 * counts it as detached-after-join just like the slow-peer path -- detachPeer
 * is the single increment point. Moving it out then cleanly decrements, so the
 * count returns to 0 (no other counted peer's slot is consumed).
 */
TEST_F(
    UpdateGroupDetachedPeerTest,
    DetachPeerIncrementsThenMoveDecrementsDetachedAfterJoin) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  group_->markPeerInSync(adjRib0);
  group_->markPeerInSync(adjRib1);
  group_->setLastSeenRibVersion(42);
  ASSERT_EQ(group_->getNumPeersDetachedAfterJoin(), 0);

  // Directly detach the joined peer -> counted (detachPeer is the increment).
  group_->detachPeer(adjRib0, AdjRibOutGroup::DetachReason::Policy);
  EXPECT_EQ(group_->getNumPeersDetachedAfterJoin(), 1);
  EXPECT_NE(adjRib0->getDetachedRibVersion(), 0);

  /*
   * Moving it out decrements back to 0; adjRib1 stays in sync so the group is
   * not frozen.
   */
  auto targetGroup = std::make_shared<AdjRibOutGroup>(*evb_, "target_group");
  group_->movePeers({adjRib0}, targetGroup);
  EXPECT_EQ(group_->getNumPeersDetachedAfterJoin(), 0);
  EXPECT_EQ(adjRib0->getUpdateGroup(), targetGroup);

  // Cleanup: see MovePeerDecrementsDetachedAfterJoin.
  targetGroup->unregisterPeer(adjRib0);
  adjRib0->setUpdateGroup(nullptr);
  peers_.erase(peers_.begin());
}

/*
 * Cumulative per-reason detachment counters (AdjRibStats)
 */

TEST_F(UpdateGroupDetachedPeerTest, DetachReasonCountsInitiallyZero) {
  auto adjRib0 = createAndRegisterPeer(0);
  EXPECT_EQ(adjRib0->getStats().getNumTimesDetachedByBlocking(), 0);
  EXPECT_EQ(adjRib0->getStats().getNumTimesDetachedByPolicy(), 0);
  EXPECT_EQ(adjRib0->getStats().getNumTimesDetachedByRouteRefresh(), 0);
}

TEST_F(UpdateGroupDetachedPeerTest, DetachSlowPeerIncrementsBlockingCount) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  // Two in-sync members so detachSlowPeer does not skip (last-synced guard).
  group_->markPeerInSync(adjRib0);
  group_->markPeerInSync(adjRib1);
  group_->setLastSeenRibVersion(42);

  group_->detachSlowPeer(adjRib0);

  // Blocking detach bumps only the detached peer's blocking counter.
  EXPECT_EQ(adjRib0->getStats().getNumTimesDetachedByBlocking(), 1);
  EXPECT_EQ(adjRib0->getStats().getNumTimesDetachedByPolicy(), 0);
}

TEST_F(UpdateGroupDetachedPeerTest, DetachPeerPolicyIncrementsPolicyCount) {
  auto adjRib0 = createAndRegisterPeer(0);
  group_->markPeerInSync(adjRib0);
  group_->setLastSeenRibVersion(42);

  group_->detachPeer(adjRib0, AdjRibOutGroup::DetachReason::Policy);

  // Policy detach bumps only the detached peer's policy counter.
  EXPECT_EQ(adjRib0->getStats().getNumTimesDetachedByPolicy(), 1);
  EXPECT_EQ(adjRib0->getStats().getNumTimesDetachedByBlocking(), 0);
}

/*
 * The persisted last detach reason/time are stamped on detach, kept across
 * detached sub-state changes and rejoin, and overwritten with Policy when the
 * peer is moved to another group.
 */
TEST_F(UpdateGroupDetachedPeerTest, LastDetachReasonPersistsAcrossRejoin) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  // Two in-sync members so detachSlowPeer does not skip (last-synced guard).
  group_->markPeerInSync(adjRib0);
  group_->markPeerInSync(adjRib1);
  group_->setLastSeenRibVersion(42);
  EXPECT_FALSE(adjRib0->getPeerUpdateStateInfo().lastDetachReason.has_value());
  EXPECT_EQ(adjRib0->getPeerUpdateStateInfo().lastDetachTimeMs, 0);

  group_->detachSlowPeer(adjRib0);
  EXPECT_EQ(
      adjRib0->getPeerUpdateStateInfo().lastDetachReason,
      AdjRibOutGroup::DetachReason::Blocking);
  const auto firstDetachTimeMs =
      adjRib0->getPeerUpdateStateInfo().lastDetachTimeMs;
  EXPECT_GT(firstDetachTimeMs, 0);

  adjRib0->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  EXPECT_EQ(
      adjRib0->getPeerUpdateStateInfo().lastDetachReason,
      AdjRibOutGroup::DetachReason::Blocking);

  ASSERT_EQ(group_->tryAcceptPeersToGroup({adjRib0}).size(), 1);
  ASSERT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_EQ(
      adjRib0->getPeerUpdateStateInfo().lastDetachReason,
      AdjRibOutGroup::DetachReason::Blocking);
  EXPECT_EQ(
      adjRib0->getPeerUpdateStateInfo().lastDetachTimeMs, firstDetachTimeMs);

  group_->detachSlowPeer(adjRib0);
  auto targetGroup = std::make_shared<AdjRibOutGroup>(*evb_, "target_group");
  group_->movePeers({adjRib0}, targetGroup);
  EXPECT_EQ(
      adjRib0->getPeerUpdateStateInfo().lastDetachReason,
      AdjRibOutGroup::DetachReason::Policy);
  EXPECT_GE(
      adjRib0->getPeerUpdateStateInfo().lastDetachTimeMs, firstDetachTimeMs);

  // Cleanup: see MovePeerDecrementsDetachedAfterJoin.
  targetGroup->unregisterPeer(adjRib0);
  adjRib0->setUpdateGroup(nullptr);
  peers_.erase(peers_.begin());
}

TEST_F(
    UpdateGroupDetachedPeerTest,
    DetachPeerRouteRefreshMovesOnlyRequesterToPrivateLane) {
  auto requester = createAndRegisterPeer(0);
  auto sibling = createAndRegisterPeer(1);
  requester->setPeerState(PeerUpdateState::JOINED_RUNNING);
  sibling->setPeerState(PeerUpdateState::JOINED_RUNNING);
  group_->markPeerInSync(requester);
  group_->markPeerInSync(sibling);
  group_->setLastSeenRibVersion(42);

  group_->detachPeer(requester, AdjRibOutGroup::DetachReason::RouteRefresh);

  EXPECT_EQ(requester->getPeerState(), PeerUpdateState::DETACHED_RUNNING);
  EXPECT_TRUE(group_->getDetachedPeers().contains(requester));
  EXPECT_FALSE(group_->isPeerInSync(0));
  EXPECT_EQ(requester->getLastSeenRibVersion(), 42);
  EXPECT_EQ(requester->getDetachedRibVersion(), 42);
  EXPECT_EQ(requester->getStats().getNumTimesDetachedByRouteRefresh(), 1);
  EXPECT_EQ(requester->getStats().getNumTimesDetachedByBlocking(), 0);
  EXPECT_EQ(requester->getStats().getNumTimesDetachedByPolicy(), 0);

  EXPECT_EQ(sibling->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_TRUE(group_->isPeerInSync(1));
  EXPECT_FALSE(group_->getDetachedPeers().contains(sibling));
}

TEST_F(
    UpdateGroupDetachedPeerTest,
    RouteRefreshDoesNotAdvanceJoinedPeerConsumer) {
  auto adjRib = createAndRegisterPeer(0);
  adjRib->setPeerState(PeerUpdateState::JOINED_RUNNING);

  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;
  ASSERT_TRUE(adjRib->registerDetachedConsumer(
      changeTracker, addPathBitmap, nonAddPathBitmap));
  auto consumer = adjRib->getChangeListConsumer();
  ASSERT_NE(consumer, nullptr);

  ShadowRibEntry entry;
  entry.prefix = kV4Prefix1;
  auto trackable =
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(entry));
  changeTracker->publishChange(trackable.get());
  ASSERT_FALSE(consumer->isReady());

  RibOutAnnouncement announcement;
  announcement.routeRefreshAfi = nettools::bgplib::BgpUpdateAfi::AFI_IPv4;
  announcement.sendWithEoR = true;
  adjRib->maybeBeginRrDump(announcement);
  adjRib->maybeEndRrDump(announcement);

  EXPECT_FALSE(consumer->isReady());
  adjRib->resetChangeListConsumer();
}

TEST_F(
    UpdateGroupDetachedPeerTest,
    DetachPeerRouteRefreshPreservesBlockedState) {
  auto requester = createAndRegisterPeer(0);
  auto sibling = createAndRegisterPeer(1);
  requester->setPeerState(PeerUpdateState::JOINED_RUNNING);
  sibling->setPeerState(PeerUpdateState::JOINED_RUNNING);
  group_->markPeerInSync(requester);
  group_->markPeerInSync(sibling);
  group_->setLastSeenRibVersion(42);
  group_->markPeerBlocked(requester);
  ASSERT_EQ(requester->getPeerState(), PeerUpdateState::JOINED_BLOCKED);

  group_->detachPeer(requester, AdjRibOutGroup::DetachReason::RouteRefresh);

  EXPECT_EQ(requester->getPeerState(), PeerUpdateState::DETACHED_BLOCKED);
  EXPECT_TRUE(group_->getDetachedPeers().contains(requester));
  EXPECT_FALSE(group_->isPeerInSync(0));
  EXPECT_EQ(requester->getLastSeenRibVersion(), 42);
  EXPECT_EQ(requester->getDetachedRibVersion(), 42);
  EXPECT_EQ(sibling->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_TRUE(group_->isPeerInSync(1));
}

TEST_F(UpdateGroupDetachedPeerTest, RegisterDetachedConsumerSkipsIfAlreadySet) {
  auto adjRib = createAndRegisterPeer(0);

  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;

  auto groupConsumer = std::make_shared<AdjRibOutGroupConsumer>(
      changeTracker,
      group_,
      "test_group_consumer",
      *evb_,
      addPathBitmap,
      nonAddPathBitmap);
  groupConsumer->registerWithTracker();
  groupConsumer->setBitmap();

  // Register once
  adjRib->registerDetachedConsumerAtGroupPosition(
      changeTracker, groupConsumer, addPathBitmap, nonAddPathBitmap);
  auto firstConsumer = adjRib->getDetachedConsumer();
  ASSERT_NE(firstConsumer, nullptr);

  auto& messages = subscribeToLogMessages("", folly::LogLevel::WARN);
  messages.clear();

  /*
   * Register again — should be a no-op. registerDetachedConsumer reports
   * failure (consumer already exists, WARN) and the wrapper logs an error and
   * skips the join, leaving the existing consumer untouched.
   */
  adjRib->registerDetachedConsumerAtGroupPosition(
      changeTracker, groupConsumer, addPathBitmap, nonAddPathBitmap);
  EXPECT_EQ(adjRib->getDetachedConsumer(), firstConsumer);

  ASSERT_EQ(messages.size(), 2);
  bool foundAlreadyRegistered = false;
  bool foundJoinSkipped = false;
  for (const auto& [msg, cat] : messages) {
    if (msg.getMessage().find("already has a detached consumer") !=
        std::string::npos) {
      foundAlreadyRegistered = true;
    }
    if (msg.getMessage().find("failed to register detached consumer") !=
        std::string::npos) {
      foundJoinSkipped = true;
    }
  }
  EXPECT_TRUE(foundAlreadyRegistered);
  EXPECT_TRUE(foundJoinSkipped);

  // Clean up
  adjRib->deactivateDetachedModeProcessing();
  groupConsumer->resetBitmap();
  groupConsumer->terminate();
  groupConsumer->deregisterFromTracker();
}

TEST_F(UpdateGroupDetachedPeerTest, RegisterDetachedConsumerSkipsNullTracker) {
  auto adjRib = createAndRegisterPeer(0);

  std::shared_ptr<ChangeTracker<ShadowRibEntry>> nullTracker;
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;

  auto& messages = subscribeToLogMessages("", folly::LogLevel::ERR);
  messages.clear();

  adjRib->registerDetachedConsumer(
      nullTracker, addPathBitmap, nonAddPathBitmap);
  EXPECT_EQ(adjRib->getDetachedConsumer(), nullptr);

  ASSERT_EQ(messages.size(), 1);
  EXPECT_THAT(
      messages[0].first.getMessage(), HasSubstr("null changeListTracker"));
}

TEST_F(
    UpdateGroupDetachedPeerTest,
    DeactivateDetachedModeProcessingNoOpWhenNull) {
  auto adjRib = createAndRegisterPeer(0);

  EXPECT_EQ(adjRib->getDetachedConsumer(), nullptr);
  adjRib->deactivateDetachedModeProcessing();
  EXPECT_EQ(adjRib->getDetachedConsumer(), nullptr);
  EXPECT_EQ(0, adjRib->getDetachedRibVersion());
}

/*
 * Regression test: a peer entering an already-running update group takes the
 * DETACHED_INIT_DUMP branch of registerPeer(). Its per-peer CL consumer, set
 * during session establishment but never registered with the ChangeTracker,
 * must be reset here. Otherwise registerDetachedConsumer() (called later from
 * PeerManagerBase::processRibDumpReq) short-circuits on its
 * `if (changeListConsumer_)` guard, changeListConsumeTimer_ is never created,
 * and detached CL consumption never starts. Mirrors the INIT-branch reset
 * (inspired by D100483403).
 */
TEST_F(
    UpdateGroupDetachedPeerTest,
    RegisterPeerInRunningGroupResetsStaleConsumer) {
  /*
   * Build a peer directly so the registration happens inline in the test
   * body, where the stale-consumer precondition can be set up first.
   */
  auto peerId = nettools::bgplib::BgpPeerId(
      folly::IPAddress(fmt::format(kPeerIpFmt, 1)),
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
  peers_.push_back(adjRib);

  /*
   * Simulate session establishment: a per-peer CL consumer that was never
   * registered with the ChangeTracker.
   */
  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;
  auto staleConsumer = std::make_shared<AdjRibOutConsumer>(
      changeTracker,
      adjRib,
      "ChangeList Consumer",
      *evb_,
      addPathBitmap,
      nonAddPathBitmap);
  adjRib->setChangeListConsumer(staleConsumer);
  ASSERT_NE(adjRib->getChangeListConsumer(), nullptr);

  /*
   * Group is already running -> registerPeer takes the DETACHED_INIT_DUMP
   * branch
   */
  group_->setState(UpdateGroupState::READY);
  group_->registerPeer(adjRib);

  // Peer entered detached-init-dump state...
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::DETACHED_INIT_DUMP);
  EXPECT_TRUE(adjRib->isAdjRibFlagSet(AdjRib::DETACHED_ON_REGISTRATION));
  EXPECT_TRUE(group_->getDetachedPeers().contains(adjRib));

  /*
   * ...and the stale consumer was reset, so registerDetachedConsumer() can
   * later create the bounded detached consumer and its consume timer instead
   * of short-circuiting.
   */
  EXPECT_EQ(adjRib->getChangeListConsumer(), nullptr);

  /*
   * Deactivating detached mode processing (e.g. on promotion to sync) clears
   * the DETACHED_ON_REGISTRATION flag so the peer is no longer treated as
   * detached-on-registration.
   */
  adjRib->deactivateDetachedModeProcessing();
  EXPECT_FALSE(adjRib->isAdjRibFlagSet(AdjRib::DETACHED_ON_REGISTRATION));
}

/*
 * isDFP() tests
 */

TEST_F(UpdateGroupDetachedPeerTest, IsDFPReturnsTrueWhenAllConditionsMet) {
  auto adjRib = createAndRegisterPeer(0);
  adjRib->setUpdateGroup(group_);

  // Condition 1: detachedPackingList_ empty (default)
  ASSERT_TRUE(adjRib->attrToPrefixMap_.empty());

  // Condition 2: group and peer have same lastSeenRibVersion
  group_->setLastSeenRibVersion(42);
  adjRib->setLastSeenRibVersion(42);
  // Peer hasn't advanced past its detach snapshot (detachedRibVersion_)
  adjRib->setDetachedRibVersion(42);
  /*
   * Peer is detached (not in sync), so getLastSeenRibVersion returns
   * peer's own version
   */
  ASSERT_FALSE(group_->isPeerInSync(0));

  // Verify per-peer table version ODS counter matches
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();
  tcData->publishStats();
  EXPECT_EQ(
      42,
      tcData->getCounter(
          fmt::format(
              PeerStats::kPeerTableVersion, kEbbPlatform, kBgpcppTag, "")));

  /*
   * Condition 3: group's packing list is non-empty. The group only queues to
   * its packing list when it has an in-sync peer, so add one (the peer under
   * test at bit 0 stays detached).
   */
  addInSyncPeer(1);
  auto attrs = std::make_shared<BgpPath>(BgpPathFields());
  attrs->setLocalPref(100);
  std::shared_ptr<const BgpPath> constAttrs = attrs;
  const folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  group_->tryUpdateAttrToPrefixMapForGroup(
      std::make_pair(prefix, kPlaceholderPathID), nullptr, constAttrs);
  ASSERT_FALSE(group_->getAttrToPrefixMap().empty());

  EXPECT_TRUE(adjRib->isDFP());
}

TEST_F(UpdateGroupDetachedPeerTest, IsDFPReturnsFalseWhenPLNotEmpty) {
  auto adjRib = createAndRegisterPeer(0);
  adjRib->setUpdateGroup(group_);

  // Set matching versions
  group_->setLastSeenRibVersion(42);
  adjRib->setLastSeenRibVersion(42);

  // Non-empty group PL (requires an in-sync peer to queue to the packing list).
  addInSyncPeer(1);
  auto attrs = std::make_shared<BgpPath>(BgpPathFields());
  std::shared_ptr<const BgpPath> constAttrs = attrs;
  const folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  group_->tryUpdateAttrToPrefixMapForGroup(
      std::make_pair(prefix, kPlaceholderPathID), nullptr, constAttrs);

  // But detached PL is non-empty
  group_->clonePackingListForPeer(adjRib);
  ASSERT_FALSE(adjRib->attrToPrefixMap_.empty());

  EXPECT_FALSE(adjRib->isDFP());
}

TEST_F(UpdateGroupDetachedPeerTest, IsDFPReturnsFalseWhenVersionsMismatch) {
  auto adjRib = createAndRegisterPeer(0);
  adjRib->setUpdateGroup(group_);

  // Empty detached PL (default)
  ASSERT_TRUE(adjRib->attrToPrefixMap_.empty());

  // Versions differ — group has moved on
  group_->setLastSeenRibVersion(50);
  adjRib->setLastSeenRibVersion(42);

  // Verify per-peer table version ODS counter reflects peer's version
  auto tcData = facebook::fb303::ThreadCachedServiceData::get();
  tcData->publishStats();
  EXPECT_EQ(
      42,
      tcData->getCounter(
          fmt::format(
              PeerStats::kPeerTableVersion, kEbbPlatform, kBgpcppTag, "")));

  // Non-empty group PL
  auto attrs = std::make_shared<BgpPath>(BgpPathFields());
  std::shared_ptr<const BgpPath> constAttrs = attrs;
  const folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  group_->tryUpdateAttrToPrefixMapForGroup(
      std::make_pair(prefix, kPlaceholderPathID), nullptr, constAttrs);

  EXPECT_FALSE(adjRib->isDFP());
}

TEST_F(UpdateGroupDetachedPeerTest, IsDFPReturnsFalseWhenGroupPLEmpty) {
  auto adjRib = createAndRegisterPeer(0);
  adjRib->setUpdateGroup(group_);

  // Empty detached PL and matching versions
  group_->setLastSeenRibVersion(42);
  adjRib->setLastSeenRibVersion(42);

  // But group PL is empty
  ASSERT_TRUE(group_->getAttrToPrefixMap().empty());

  EXPECT_FALSE(adjRib->isDFP());
}

TEST_F(UpdateGroupDetachedPeerTest, IsDFPReturnsFalseWhenNoGroup) {
  auto adjRib = createAndRegisterPeer(0);
  // Don't set update group — adjRibOutGroup_ remains nullptr

  EXPECT_FALSE(adjRib->isDFP());
}

/*
 * canWaitForGroupToRejoin() tests
 */

TEST_F(
    UpdateGroupDetachedPeerTest,
    CanWaitForGroupToRejoinReturnsFalseWhenVersionMismatchEvenIfPLEmpty) {
  auto adjRib = createAndRegisterPeer(0);

  // Set up change tracker and consumer
  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;

  group_->setChangeListTracker(changeTracker, addPathBitmap, nonAddPathBitmap);
  group_->registerGroupConsumer();
  auto groupConsumer = group_->getChangeListConsumer();
  ASSERT_NE(groupConsumer, nullptr);
  ASSERT_TRUE(groupConsumer->isReady());

  // Register detached consumer so the null-consumer guard passes.
  adjRib->registerDetachedConsumerAtGroupPosition(
      changeTracker, groupConsumer, addPathBitmap, nonAddPathBitmap);
  ASSERT_NE(adjRib->getDetachedConsumer(), nullptr);

  // Group has materialized a newer RIB version than the peer.
  group_->setLastSeenRibVersion(5);
  ASSERT_NE(group_->getLastSeenRibVersion(), adjRib->getLastSeenRibVersion());

  // PL is empty but the versions differ — not ready to rejoin.
  ASSERT_TRUE(adjRib->attrToPrefixMap_.empty());
  EXPECT_FALSE(adjRib->canWaitForGroupToRejoin());

  // Clean up
  adjRib->deactivateDetachedModeProcessing();
  groupConsumer->resetBitmap();
  groupConsumer->terminate();
  groupConsumer->deregisterFromTracker();
  group_->resetChangeListConsumer();
}

TEST_F(
    UpdateGroupDetachedPeerTest,
    CanWaitForGroupToRejoinReturnsFalseWhenPLNotEmpty) {
  auto adjRib = createAndRegisterPeer(0);

  // Set up detached consumer
  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;

  auto groupConsumer = std::make_shared<AdjRibOutGroupConsumer>(
      changeTracker,
      group_,
      "test_group_consumer",
      *evb_,
      addPathBitmap,
      nonAddPathBitmap);
  groupConsumer->registerWithTracker();
  groupConsumer->setBitmap();

  adjRib->registerDetachedConsumerAtGroupPosition(
      changeTracker, groupConsumer, addPathBitmap, nonAddPathBitmap);

  // Non-empty detached PL (requires an in-sync peer to queue to the group PL).
  addInSyncPeer(1);
  auto attrs = std::make_shared<BgpPath>(BgpPathFields());
  std::shared_ptr<const BgpPath> constAttrs = attrs;
  const folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  group_->tryUpdateAttrToPrefixMapForGroup(
      std::make_pair(prefix, kPlaceholderPathID), nullptr, constAttrs);
  group_->clonePackingListForPeer(adjRib);
  ASSERT_FALSE(adjRib->attrToPrefixMap_.empty());

  EXPECT_FALSE(adjRib->canWaitForGroupToRejoin());

  // Clean up
  adjRib->deactivateDetachedModeProcessing();
  groupConsumer->resetBitmap();
  groupConsumer->terminate();
  groupConsumer->deregisterFromTracker();
}

TEST_F(
    UpdateGroupDetachedPeerTest,
    CanWaitForGroupToRejoinReturnsFalseWhenNoConsumer) {
  auto adjRib = createAndRegisterPeer(0);

  // No detached consumer registered
  ASSERT_EQ(adjRib->getDetachedConsumer(), nullptr);
  ASSERT_TRUE(adjRib->attrToPrefixMap_.empty());

  EXPECT_FALSE(adjRib->canWaitForGroupToRejoin());
}

TEST_F(
    UpdateGroupDetachedPeerTest,
    CanWaitForGroupToRejoinRequiresBothConsumersReady) {
  auto adjRib = createAndRegisterPeer(0);
  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;

  group_->setChangeListTracker(changeTracker, addPathBitmap, nonAddPathBitmap);
  group_->registerGroupConsumer();
  auto groupConsumer = group_->getChangeListConsumer();
  adjRib->registerDetachedConsumerAtGroupPosition(
      changeTracker, groupConsumer, addPathBitmap, nonAddPathBitmap);
  auto peerConsumer = adjRib->getDetachedConsumer();

  group_->setLastSeenRibVersion(42);
  adjRib->setLastSeenRibVersion(42);
  ASSERT_TRUE(group_->getAttrToPrefixMap().empty());
  ASSERT_TRUE(adjRib->attrToPrefixMap_.empty());
  ASSERT_TRUE(groupConsumer->isReady());
  ASSERT_TRUE(peerConsumer->isReady());
  EXPECT_TRUE(adjRib->canWaitForGroupToRejoin());

  ShadowRibEntry firstEntry;
  firstEntry.prefix = folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24};
  auto firstTrackable =
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(firstEntry));
  changeTracker->publishChange(firstTrackable.get());

  ASSERT_FALSE(groupConsumer->isReady());
  ASSERT_FALSE(peerConsumer->isReady());
  ASSERT_EQ(groupConsumer->getMarker(), peerConsumer->getMarker());
  EXPECT_FALSE(adjRib->canWaitForGroupToRejoin());

  peerConsumer->iterateChangesToEnd();
  ASSERT_TRUE(peerConsumer->isReady());
  ASSERT_FALSE(groupConsumer->isReady());
  EXPECT_FALSE(adjRib->canWaitForGroupToRejoin());

  groupConsumer->iterateChangesToEnd();
  ASSERT_TRUE(peerConsumer->isReady());
  ASSERT_TRUE(groupConsumer->isReady());
  EXPECT_TRUE(adjRib->canWaitForGroupToRejoin());

  ShadowRibEntry secondEntry;
  secondEntry.prefix = folly::CIDRNetwork{folly::IPAddress("20.0.0.0"), 24};
  auto secondTrackable =
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(secondEntry));
  changeTracker->publishChange(secondTrackable.get());
  groupConsumer->iterateChangesToEnd();

  ASSERT_FALSE(peerConsumer->isReady());
  ASSERT_TRUE(groupConsumer->isReady());
  EXPECT_FALSE(adjRib->canWaitForGroupToRejoin());

  adjRib->deactivateDetachedModeProcessing();
  groupConsumer->resetBitmap();
  groupConsumer->terminate();
  groupConsumer->deregisterFromTracker();
  group_->resetChangeListConsumer();
}

TEST_F(
    UpdateGroupDetachedPeerTest,
    IsReadyToRejoinGroupReturnsFalseWhenGroupPLNotEmpty) {
  auto adjRib = createAndRegisterPeer(0);
  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;

  group_->setChangeListTracker(changeTracker, addPathBitmap, nonAddPathBitmap);
  group_->registerGroupConsumer();
  auto groupConsumer = group_->getChangeListConsumer();
  adjRib->registerDetachedConsumerAtGroupPosition(
      changeTracker, groupConsumer, addPathBitmap, nonAddPathBitmap);

  group_->setLastSeenRibVersion(42);
  adjRib->setLastSeenRibVersion(42);
  addInSyncPeer(1);
  auto attrs = std::make_shared<BgpPath>(BgpPathFields());
  std::shared_ptr<const BgpPath> constAttrs = attrs;
  group_->tryUpdateAttrToPrefixMapForGroup(
      std::make_pair(
          folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24},
          kPlaceholderPathID),
      nullptr,
      constAttrs);

  ASSERT_TRUE(adjRib->attrToPrefixMap_.empty());
  ASSERT_FALSE(group_->getAttrToPrefixMap().empty());
  ASSERT_TRUE(adjRib->getDetachedConsumer()->isReady());
  ASSERT_TRUE(groupConsumer->isReady());
  /*
   * The group packing list is the only condition separating the two: the peer
   * still qualifies to transition to DETACHED_READY_TO_JOIN, but cannot be
   * accepted into the group until the group drains.
   */
  EXPECT_TRUE(adjRib->canWaitForGroupToRejoin());
  EXPECT_FALSE(adjRib->isReadyToRejoinGroup());

  adjRib->deactivateDetachedModeProcessing();
  groupConsumer->resetBitmap();
  groupConsumer->terminate();
  groupConsumer->deregisterFromTracker();
  group_->resetChangeListConsumer();
}

TEST_F(UpdateGroupDetachedPeerTest, MaybeAcceptDSPPeerRechecksReadiness) {
  auto adjRib = createAndRegisterPeer(0);
  group_->markPeerDetached(adjRib);
  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;

  group_->setChangeListTracker(changeTracker, addPathBitmap, nonAddPathBitmap);
  group_->registerGroupConsumer();
  auto groupConsumer = group_->getChangeListConsumer();

  ShadowRibEntry entry;
  entry.prefix = folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24};
  auto trackable =
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(entry));
  changeTracker->publishChange(trackable.get());
  adjRib->registerDetachedConsumerAtGroupPosition(
      changeTracker, groupConsumer, addPathBitmap, nonAddPathBitmap);
  group_->setLastSeenRibVersion(42);
  adjRib->setLastSeenRibVersion(42);

  ASSERT_FALSE(groupConsumer->isReady());
  ASSERT_FALSE(adjRib->getDetachedConsumer()->isReady());
  ASSERT_EQ(
      groupConsumer->getMarker(), adjRib->getDetachedConsumer()->getMarker());
  ASSERT_TRUE(group_->getAttrToPrefixMap().empty());
  ASSERT_TRUE(adjRib->attrToPrefixMap_.empty());

  group_->maybeAcceptDSPPeer(adjRib);

  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::DETACHED_READY_TO_JOIN);
  EXPECT_FALSE(group_->isPeerInSync(0));

  adjRib->deactivateDetachedModeProcessing();
  groupConsumer->resetBitmap();
  groupConsumer->terminate();
  groupConsumer->deregisterFromTracker();
  group_->resetChangeListConsumer();
}

TEST_F(UpdateGroupDetachedPeerTest, CanWaitForGroupToRejoinReturnsTrue) {
  auto adjRib = createAndRegisterPeer(0);

  // Set up change tracker and consumer
  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;

  group_->setChangeListTracker(changeTracker, addPathBitmap, nonAddPathBitmap);
  group_->registerGroupConsumer();
  auto groupConsumer = group_->getChangeListConsumer();

  // Publish and consume an item so group consumer reaches ready state
  ShadowRibEntry entry;
  entry.prefix = folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24};
  auto trackable =
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(entry));
  changeTracker->publishChange(trackable.get());
  groupConsumer->iterateChangesToEnd();
  ASSERT_TRUE(groupConsumer->isReady());

  // Register detached consumer — joins at group's ready position
  adjRib->registerDetachedConsumerAtGroupPosition(
      changeTracker, groupConsumer, addPathBitmap, nonAddPathBitmap);
  auto detachedConsumer = adjRib->getDetachedConsumer();
  ASSERT_NE(detachedConsumer, nullptr);

  ASSERT_TRUE(detachedConsumer->isReady());

  // PL is empty and consumer is ready — should return true
  ASSERT_TRUE(adjRib->attrToPrefixMap_.empty());
  EXPECT_TRUE(adjRib->canWaitForGroupToRejoin());

  // Clean up
  adjRib->deactivateDetachedModeProcessing();
  groupConsumer->resetBitmap();
  groupConsumer->terminate();
  groupConsumer->deregisterFromTracker();
  group_->resetChangeListConsumer();
}

/*
 * activateDetachedModeProcessing() tests
 */

TEST_F(UpdateGroupDetachedPeerTest, ActivateDSPTransitionsToReadyToJoin) {
  auto adjRib = createAndRegisterPeer(0);
  adjRib->setUpdateGroup(group_);
  adjRib->setPeerState(PeerUpdateState::DETACHED_RUNNING);

  // Set up change tracker and consumer so canWaitForGroupToRejoin() can pass
  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;

  group_->setChangeListTracker(changeTracker, addPathBitmap, nonAddPathBitmap);
  group_->registerGroupConsumer();
  auto groupConsumer = group_->getChangeListConsumer();

  // Publish a CL item and consume it so group consumer is ready
  ShadowRibEntry entry;
  entry.prefix = folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24};
  auto trackable =
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(entry));
  changeTracker->publishChange(trackable.get());
  groupConsumer->iterateChangesToEnd();
  EXPECT_TRUE(groupConsumer->isReady());

  // Register detached consumer — joins at group's ready position
  adjRib->registerDetachedConsumerAtGroupPosition(
      changeTracker, groupConsumer, addPathBitmap, nonAddPathBitmap);
  EXPECT_TRUE(adjRib->getDetachedConsumer()->isReady());

  // Activate detached mode processing — schedules sendBgpUpdates on evb
  adjRib->activateDetachedModeProcessing();

  // Pump the evb so sendBgpUpdates runs and DFP check triggers
  evb_->loopOnce();

  /*
   * DSP: peer and group both ready -> maybeAcceptDSPPeer accepts
   * immediately
   */
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_TRUE(group_->isPeerInSync(0));

  // Clean up
  groupConsumer->resetBitmap();
  groupConsumer->terminate();
  groupConsumer->deregisterFromTracker();
  group_->resetChangeListConsumer();
}

TEST_F(UpdateGroupDetachedPeerTest, ActivateDSPStartsTimerLoop) {
  auto adjRib = createAndRegisterPeer(0);
  adjRib->setUpdateGroup(group_);
  adjRib->setPeerState(PeerUpdateState::DETACHED_RUNNING);

  // Set up change tracker and consumer for CL consumption
  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;

  group_->setChangeListTracker(changeTracker, addPathBitmap, nonAddPathBitmap);
  group_->registerGroupConsumer();
  auto groupConsumer = group_->getChangeListConsumer();

  /*
   * Publish an item but DON'T consume it — group consumer stays pended.
   * Detached consumer joins pended too (not ready = DSP).
   */
  ShadowRibEntry entry;
  entry.prefix = folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24};
  auto trackable =
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(entry));
  changeTracker->publishChange(trackable.get());

  // Register detached consumer at group's position (pended, not ready)
  adjRib->registerDetachedConsumerAtGroupPosition(
      changeTracker, groupConsumer, addPathBitmap, nonAddPathBitmap);

  // Simulate prior block history
  adjRib->getPeerBlockInfo().blockCount = 3;

  // Activate detached mode processing — schedules sendBgpUpdates
  adjRib->activateDetachedModeProcessing();

  /*
   * Block info is NOT reset on activation — it's reset on rejoin instead,
   * so frequency-based slow peer detection is preserved while detached.
   */
  EXPECT_EQ(adjRib->getPeerBlockInfo().blockCount, 3);

  // Pump the evb so sendBgpUpdates runs and schedules the CL timer
  evb_->loopOnce();

  // Peer should NOT transition to DETACHED_READY_TO_JOIN yet
  EXPECT_NE(adjRib->getPeerState(), PeerUpdateState::DETACHED_READY_TO_JOIN);

  // Clean up
  adjRib->deactivateDetachedModeProcessing();
  groupConsumer->resetBitmap();
  groupConsumer->terminate();
  groupConsumer->deregisterFromTracker();
  group_->resetChangeListConsumer();
}

TEST_F(UpdateGroupDetachedPeerTest, ActivateDFPTransitionsViaIsDFPPath) {
  auto adjRib = createAndRegisterPeer(0);
  adjRib->setUpdateGroup(group_);
  adjRib->setPeerState(PeerUpdateState::DETACHED_RUNNING);

  // Set up change tracker and consumer
  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;

  group_->setChangeListTracker(changeTracker, addPathBitmap, nonAddPathBitmap);
  group_->registerGroupConsumer();
  auto groupConsumer = group_->getChangeListConsumer();

  /*
   * Publish an item but DON'T consume it — group consumer stays pended, so the
   * peer's detached consumer joins pended (not ready), asserted below.
   */
  ShadowRibEntry entry;
  entry.prefix = folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24};
  auto trackable =
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(entry));
  changeTracker->publishChange(trackable.get());

  // Register detached consumer at group's position (pended, not ready)
  adjRib->registerDetachedConsumerAtGroupPosition(
      changeTracker, groupConsumer, addPathBitmap, nonAddPathBitmap);
  EXPECT_FALSE(adjRib->getDetachedConsumer()->isReady());

  /*
   * Set up DFP conditions:
   *   1. PL empty (default)
   *   2. Matching versions between group and peer
   *   3. Peer hasn't advanced past its detach snapshot (detachedRibVersion_)
   *   4. Group PL non-empty
   */
  group_->setLastSeenRibVersion(42);
  adjRib->setLastSeenRibVersion(42);
  adjRib->setDetachedRibVersion(42);
  /*
   * The group only queues to its packing list when it has an in-sync peer (the
   * peer under test at bit 0 stays detached).
   */
  addInSyncPeer(1);
  auto attrs = std::make_shared<BgpPath>(BgpPathFields());
  attrs->setLocalPref(100);
  std::shared_ptr<const BgpPath> constAttrs = attrs;
  group_->tryUpdateAttrToPrefixMapForGroup(
      std::make_pair(
          folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24},
          kPlaceholderPathID),
      nullptr,
      constAttrs);

  EXPECT_TRUE(adjRib->isDFP());

  // Activate detached mode processing — schedules sendBgpUpdates on evb
  adjRib->activateDetachedModeProcessing();

  // Pump the evb so sendBgpUpdates runs and isDFP() triggers transition
  evb_->loopOnce();

  // DFP path: isDFP() returns true -> DETACHED_READY_TO_JOIN with flag
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::DETACHED_READY_TO_JOIN);
  EXPECT_TRUE(adjRib->isAdjRibFlagSet(AdjRib::IS_DETACHED_FAST_PEER));

  // Clean up
  adjRib->deactivateDetachedModeProcessing();
  groupConsumer->resetBitmap();
  groupConsumer->terminate();
  groupConsumer->deregisterFromTracker();
  group_->resetChangeListConsumer();
}

/*
 * deactivateDetachedModeProcessing() tests
 */

TEST_F(UpdateGroupDetachedPeerTest, DeactivateNoOpWhenNoTimer) {
  auto adjRib = createAndRegisterPeer(0);

  // No timer set — should be a no-op, no crash
  adjRib->deactivateDetachedModeProcessing();
}

/*
 * markPeerUnblocked() with detached peer tests
 */

TEST_F(
    UpdateGroupDetachedPeerTest,
    MarkPeerUnblockedTriggersDetachedProcessing) {
  auto adjRib = createAndRegisterPeer(0);
  adjRib->setUpdateGroup(group_);

  // Set up change tracker and consumer so canWaitForGroupToRejoin() can pass
  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;

  group_->setChangeListTracker(changeTracker, addPathBitmap, nonAddPathBitmap);
  group_->registerGroupConsumer();
  auto groupConsumer = group_->getChangeListConsumer();

  // Publish a CL item and consume it so group consumer is ready
  ShadowRibEntry entry;
  entry.prefix = folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24};
  auto trackable =
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(entry));
  changeTracker->publishChange(trackable.get());
  groupConsumer->iterateChangesToEnd();

  // Register detached consumer — joins at group's ready position
  adjRib->registerDetachedConsumerAtGroupPosition(
      changeTracker, groupConsumer, addPathBitmap, nonAddPathBitmap);

  // Set peer to DETACHED_BLOCKED state
  adjRib->setPeerState(PeerUpdateState::DETACHED_BLOCKED);

  // Unblock via markPeerUnblocked — triggers activateDetachedModeProcessing
  group_->markPeerUnblocked(adjRib);

  // Pump the evb so sendBgpUpdates runs and DFP check triggers
  evb_->loopOnce();

  /*
   * DSP: peer and group both ready -> maybeAcceptDSPPeer accepts
   * immediately
   */
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_TRUE(group_->isPeerInSync(0));

  // Clean up
  groupConsumer->resetBitmap();
  groupConsumer->terminate();
  groupConsumer->deregisterFromTracker();
  group_->resetChangeListConsumer();
}

/*
 * RIB-OUT Lazy Clone via Group Mutation
 *
 * shouldCloneLiteForPeer() tests — 3-case clone decision algorithm.
 */

TEST_F(UpdateGroupDetachedPeerTest, ShouldCloneFalseWhenPeerHasOwnEntry) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();

  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));

  auto groupEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  groupEntry->setPostAttr(attrs);
  groupEntry->setRibVersion(10);

  group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, peerOwnerKey, kPlaceholderPathID);

  adjRib->setDetachedRibVersion(50);

  auto radixNodeItr =
      group_->getRadixNodeItrFromLiteTree(group_->LiteTree_, kV4Prefix1);
  EXPECT_FALSE(group_->shouldCloneLiteForPeer(
      radixNodeItr, adjRib, groupEntry->getRibVersion()));
}

TEST_F(
    UpdateGroupDetachedPeerTest,
    ShouldCloneFalseWhenEntryAnnouncedAfterDetach) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();

  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));

  auto groupEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  groupEntry->setPostAttr(attrs);
  groupEntry->setRibVersion(100);

  adjRib->setDetachedRibVersion(50);

  auto radixNodeItr =
      group_->getRadixNodeItrFromLiteTree(group_->LiteTree_, kV4Prefix1);
  EXPECT_FALSE(group_->shouldCloneLiteForPeer(
      radixNodeItr, adjRib, groupEntry->getRibVersion()));
}

TEST_F(UpdateGroupDetachedPeerTest, ShouldCloneTrueWhenPeerWasSharingEntry) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();

  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));

  auto groupEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  groupEntry->setPostAttr(attrs);
  groupEntry->setRibVersion(10);

  adjRib->setDetachedRibVersion(50);

  auto radixNodeItr =
      group_->getRadixNodeItrFromLiteTree(group_->LiteTree_, kV4Prefix1);
  EXPECT_TRUE(group_->shouldCloneLiteForPeer(
      radixNodeItr, adjRib, groupEntry->getRibVersion()));
}

/*
 * copyEntryForOwner() tests
 */

TEST_F(UpdateGroupDetachedPeerTest, CopyEntryForPeerCopiesAllFields) {
  auto adjRib = createAndRegisterPeer(0);
  /*
   * Register a second peer so the group has >1 member and copyEntryForOwner
   * stores the cloned entry under the peer owner key (not the group key).
   */
  auto adjRib1 = createAndRegisterPeer(1);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();

  // Set up a group entry with all fields populated
  auto preOutAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  auto postAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(2, 0, 0, 0));
  std::shared_ptr<const BgpPath> constPreOut = preOutAttrs;
  std::shared_ptr<const BgpPath> constPost = postAttrs;

  auto groupEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  groupEntry->setPreOut(constPreOut);
  groupEntry->setPostAttr(constPost);
  groupEntry->setPostOutPolicy("test_policy");
  groupEntry->setRibVersion(42);
  groupEntry->setStale(true);

  // Clone the entry to the peer
  group_->copyEntryForOwner(
      group_->getOrCreateLiteOwnerMap(kV4Prefix1), peerOwnerKey, groupEntry);

  // Verify peer entry was created with all fields copied
  auto peerEntry =
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey);
  ASSERT_NE(peerEntry, nullptr);
  EXPECT_NE(peerEntry, groupEntry);
  EXPECT_EQ(peerEntry->getPreOut(), groupEntry->getPreOut());
  EXPECT_EQ(peerEntry->getPostAttr(), groupEntry->getPostAttr());
  EXPECT_EQ(peerEntry->getPostOutPolicy(), groupEntry->getPostOutPolicy());
  EXPECT_EQ(peerEntry->getRibVersion(), 42);
  EXPECT_EQ(peerEntry->flags_, groupEntry->flags_);
  EXPECT_TRUE(peerEntry->isStale());
}

TEST_F(UpdateGroupDetachedPeerTest, CopyEntryForPeerUsesGroupKeyWhenOnlyPeer) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();

  AdjRibEntry srcEntry(0);
  srcEntry.setRibVersion(42);

  // Caller passes the group owner key when the peer is the only member.
  group_->copyEntryForOwner(
      group_->getOrCreateLiteOwnerMap(kV4Prefix1), groupOwnerKey, &srcEntry);

  auto entry =
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, groupOwnerKey);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->getRibVersion(), 42);

  // Should NOT exist under peer owner key
  auto peerEntry =
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey);
  EXPECT_EQ(peerEntry, nullptr);
}

/*
 * promoteDetachedPeerLiteEntries() / promoteDetachedPeerPathEntries() tests
 */

TEST_F(UpdateGroupDetachedPeerTest, PromoteLiteMovesPeerEntryToGroupKey) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();

  // Group has a stale entry; the detached peer has a diverged entry.
  auto groupEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  groupEntry->setRibVersion(10);
  auto peerEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, peerOwnerKey, kPlaceholderPathID);
  peerEntry->setRibVersion(99);

  auto& messages = subscribeToLogMessages("", folly::LogLevel::INFO);
  messages.clear();

  group_->promoteDetachedPeerLiteEntries(adjRib);

  /*
   * The peer's entry (same object) is promoted under the group key; the stale
   * group entry is deleted and the peer key no longer exists.
   */
  auto promoted =
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, groupOwnerKey);
  ASSERT_NE(promoted, nullptr);
  EXPECT_EQ(promoted, peerEntry);
  EXPECT_EQ(promoted->getRibVersion(), 99);
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey),
      nullptr);

  // One entry was promoted: the logged count reflects it.
  EXPECT_TRUE(logMessagesContain(messages, "Promoted 1 LiteTree entries"));
}

TEST_F(UpdateGroupDetachedPeerTest, PromoteLiteDeletesGroupOnlyEntry) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();

  // Only a group entry exists (peer was sharing it, has no own entry).
  group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);

  auto& messages = subscribeToLogMessages("", folly::LogLevel::INFO);
  messages.clear();

  group_->promoteDetachedPeerLiteEntries(adjRib);

  // Group-only entry is deleted and the emptied node is cleaned up.
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, groupOwnerKey),
      nullptr);
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey),
      nullptr);

  // Nothing was promoted (only a group entry deleted): count is zero.
  EXPECT_TRUE(logMessagesContain(messages, "Promoted 0 LiteTree entries"));
}

TEST_F(UpdateGroupDetachedPeerTest, PromoteLitePeerOnlyEntryMovedToGroupKey) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();

  // Only a peer entry exists (no group entry at this prefix).
  auto peerEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, peerOwnerKey, kPlaceholderPathID);
  peerEntry->setRibVersion(99);

  auto& messages = subscribeToLogMessages("", folly::LogLevel::INFO);
  messages.clear();

  group_->promoteDetachedPeerLiteEntries(adjRib);

  auto promoted =
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, groupOwnerKey);
  ASSERT_NE(promoted, nullptr);
  EXPECT_EQ(promoted, peerEntry);
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey),
      nullptr);

  // One entry was promoted: the logged count reflects it.
  EXPECT_TRUE(logMessagesContain(messages, "Promoted 1 LiteTree entries"));
}

TEST_F(UpdateGroupDetachedPeerTest, PromotePathMovesPeerPathsToGroupKey) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();

  // Group has one path; the detached peer has two diverged paths.
  group_->addToPathTree(group_->PathTree_, kV4Prefix1, groupOwnerKey, 1);
  auto peerPath1 =
      group_->addToPathTree(group_->PathTree_, kV4Prefix1, peerOwnerKey, 1);
  peerPath1->setRibVersion(99);
  auto peerPath2 =
      group_->addToPathTree(group_->PathTree_, kV4Prefix1, peerOwnerKey, 2);
  peerPath2->setRibVersion(99);

  auto& messages = subscribeToLogMessages("", folly::LogLevel::INFO);
  messages.clear();

  group_->promoteDetachedPeerPathEntries(adjRib);

  /*
   * Both peer paths (same objects) are promoted under the group key, replacing
   * the group's path map; the peer key no longer exists.
   */
  EXPECT_EQ(
      group_->getFromPathTree(group_->PathTree_, kV4Prefix1, groupOwnerKey, 1),
      peerPath1);
  EXPECT_EQ(
      group_->getFromPathTree(group_->PathTree_, kV4Prefix1, groupOwnerKey, 2),
      peerPath2);
  EXPECT_EQ(
      group_->getFromPathTree(group_->PathTree_, kV4Prefix1, peerOwnerKey, 1),
      nullptr);
  EXPECT_EQ(
      group_->getFromPathTree(group_->PathTree_, kV4Prefix1, peerOwnerKey, 2),
      nullptr);

  // Count is by pathId: two paths were promoted (not one node).
  EXPECT_TRUE(logMessagesContain(messages, "Promoted 2 PathTree entries"));
}

TEST_F(UpdateGroupDetachedPeerTest, PromotePathDeletesGroupOnlyEntry) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();

  // Only a group path exists (peer was sharing it).
  group_->addToPathTree(group_->PathTree_, kV4Prefix1, groupOwnerKey, 1);

  auto& messages = subscribeToLogMessages("", folly::LogLevel::INFO);
  messages.clear();

  group_->promoteDetachedPeerPathEntries(adjRib);

  EXPECT_EQ(
      group_->getFromPathTree(group_->PathTree_, kV4Prefix1, groupOwnerKey, 1),
      nullptr);
  EXPECT_EQ(
      group_->getFromPathTree(group_->PathTree_, kV4Prefix1, peerOwnerKey, 1),
      nullptr);

  // Nothing was promoted (only a group path deleted): count is zero.
  EXPECT_TRUE(logMessagesContain(messages, "Promoted 0 PathTree entries"));
}

/*
 * promoteDetachedPeerToSync() tests
 */

TEST_F(UpdateGroupDetachedPeerTest, PromoteDetachedPeerToSyncTransitionsPeer) {
  auto adjRib = createAndRegisterPeer(0);

  /*
   * Wire the group's tracker so registerGroupConsumer() can create its
   * consumer.
   */
  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;
  group_->setChangeListTracker(changeTracker, addPathBitmap, nonAddPathBitmap);

  /*
   * Give the detached peer its own CL consumer; the group joins at its
   * position.
   */
  adjRib->registerDetachedConsumer(
      changeTracker, addPathBitmap, nonAddPathBitmap);

  // Detached peer ahead of the group (DEP-A: detachedRibVersion stays 0).
  group_->markPeerDetached(adjRib);
  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  adjRib->setLastSeenRibVersion(99);

  group_->promoteDetachedPeerToSync(adjRib);

  // Peer is in sync and out of the detached set.
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_TRUE(group_->isPeerInSync(0));
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib));

  // The promotion counts as a rejoin.
  EXPECT_EQ(adjRib->getStats().getNumTimesRejoined(), 1);

  // Group adopted the peer's RIB version; peer's detached version was reset.
  EXPECT_EQ(group_->getLastSeenRibVersion(), 99);
  EXPECT_EQ(adjRib->getDetachedRibVersion(), 0);

  // Cleanup before the local tracker is destroyed.
  group_->resetChangeListConsumer();
}

/*
 * Test (Rule 6): promoting a detached peer to sync makes the group adopt the
 * peer's egress counts, clears the peer's snapshot (via markPeerInSync), and
 * leaves the global totalSentPrefixCount untouched -- promotion advertises
 * nothing, it only re-parents the sole surviving view of the RIB-OUT.
 */
TEST_F(UpdateGroupDetachedPeerTest, PromoteDetachedPeerAdoptsPeerEgressCounts) {
  auto adjRib = createAndRegisterPeer(0);

  /*
   * Wire the group's tracker so registerGroupConsumer() can create its
   * consumer, and give the detached peer its own CL consumer to be promoted at.
   */
  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;
  group_->setChangeListTracker(changeTracker, addPathBitmap, nonAddPathBitmap);
  adjRib->registerDetachedConsumer(
      changeTracker, addPathBitmap, nonAddPathBitmap);

  group_->markPeerDetached(adjRib);
  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  adjRib->setLastSeenRibVersion(99);

  /*
   * The detached peer has advertised 2 prefixes independently; the group's
   * counts are stale (0 here, since it has no in-sync peers left).
   */
  auto totalSentBefore = totalSentPrefixCount;
  adjRib->incrementPostOutPrefixCount(true /* isIpv4 */);
  adjRib->incrementPostOutPrefixCount(true /* isIpv4 */);
  adjRib->incrementPreOutPrefixCount(true /* isIpv4 */);
  adjRib->incrementPreOutPrefixCount(true /* isIpv4 */);
  EXPECT_EQ(adjRib->getStats().getPostOutPrefixCount(), 2);
  EXPECT_EQ(group_->getStats().getPostOutPrefixCount(), 0);
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 2);

  group_->promoteDetachedPeerToSync(adjRib);

  /*
   * Group adopts the peer's counts, the peer's snapshot is cleared, and the
   * global total is unchanged by the promotion.
   */
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_EQ(group_->getStats().getPostOutPrefixCount(), 2);
  EXPECT_EQ(group_->getStats().getPreOutPrefixCount(), 2);
  EXPECT_EQ(adjRib->getStats().getPostOutPrefixCount(), 0);
  EXPECT_EQ(adjRib->getStats().getPreOutPrefixCount(), 0);
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 2);

  // Cleanup before the local tracker is destroyed.
  group_->resetChangeListConsumer();
}

TEST_F(
    UpdateGroupDetachedPeerTest,
    PromoteDetachedPeerToSyncMovesEntriesAndRebuildsConsumer) {
  auto adjRib = createAndRegisterPeer(0);

  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;
  group_->setChangeListTracker(changeTracker, addPathBitmap, nonAddPathBitmap);
  adjRib->registerDetachedConsumer(
      changeTracker, addPathBitmap, nonAddPathBitmap);
  group_->markPeerDetached(adjRib);

  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();
  /*
   * kV4Prefix1: peer's diverged entry + a stale group entry.
   * kV4Prefix2: group-only stale entry the peer never advertised.
   */
  auto peerEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, peerOwnerKey, kPlaceholderPathID);
  group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix2, groupOwnerKey, kPlaceholderPathID);

  group_->promoteDetachedPeerToSync(adjRib);

  // Peer entry promoted to the group owner key; peer owner key gone.
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, groupOwnerKey),
      peerEntry);
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey),
      nullptr);
  // Stale group-only entry deleted.
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix2, groupOwnerKey),
      nullptr);

  // Group's change list consumer was (re)created.
  EXPECT_NE(group_->getChangeListConsumer(), nullptr);

  group_->resetChangeListConsumer();
}

TEST_F(
    UpdateGroupDetachedPeerTest,
    PromoteDetachedPeerToSyncAbortsWhenPeerHasNoConsumer) {
  auto adjRib = createAndRegisterPeer(0);
  /*
   * No detached CL consumer registered for the peer -> getChangeListConsumer()
   * is null, so promotion must abort before mutating any state.
   */
  ASSERT_EQ(adjRib->getChangeListConsumer(), nullptr);

  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();
  auto peerEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, peerOwnerKey, kPlaceholderPathID);
  auto groupEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix2, groupOwnerKey, kPlaceholderPathID);
  group_->markPeerDetached(adjRib);
  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);

  group_->promoteDetachedPeerToSync(adjRib);

  /*
   * Aborted before moving entries: the peer's entry is untouched (not re-keyed
   * under the group owner key) and the group-only entry is intact.
   */
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey),
      peerEntry);
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, groupOwnerKey),
      nullptr);
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix2, groupOwnerKey),
      groupEntry);
  // Peer stays detached, not promoted.
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::DETACHED_READY_TO_JOIN);
  EXPECT_FALSE(group_->isPeerInSync(0));
  EXPECT_TRUE(group_->getDetachedPeers().contains(adjRib));
}

TEST_F(
    UpdateGroupDetachedPeerTest,
    PromoteDetachedPeerToSyncAbortsWhenTrackerNotSet) {
  auto adjRib = createAndRegisterPeer(0);

  /*
   * Give the peer a CL consumer, but deliberately do NOT wire the group's
   * tracker/bitmaps via setChangeListTracker(). Promotion must abort when it
   * cannot build a new group consumer.
   */
  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;
  adjRib->registerDetachedConsumer(
      changeTracker, addPathBitmap, nonAddPathBitmap);
  ASSERT_NE(adjRib->getChangeListConsumer(), nullptr);

  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();
  auto peerEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, peerOwnerKey, kPlaceholderPathID);
  auto groupEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix2, groupOwnerKey, kPlaceholderPathID);
  group_->markPeerDetached(adjRib);
  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);

  group_->promoteDetachedPeerToSync(adjRib);

  // Aborted before moving entries: entries untouched, no group consumer made.
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey),
      peerEntry);
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, groupOwnerKey),
      nullptr);
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix2, groupOwnerKey),
      groupEntry);
  EXPECT_EQ(group_->getChangeListConsumer(), nullptr);
  // Peer stays detached, not promoted.
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::DETACHED_READY_TO_JOIN);
  EXPECT_FALSE(group_->isPeerInSync(0));
  EXPECT_TRUE(group_->getDetachedPeers().contains(adjRib));

  // Cleanup the peer's consumer before the local tracker is destroyed.
  adjRib->resetChangeListConsumer();
}

/*
 * handleNoSyncPeers() tests
 */

TEST_F(
    UpdateGroupDetachedPeerTest,
    HandleNoSyncPeersPromotesDfpAndRestoresSync) {
  auto adjRib = createAndRegisterPeer(0);
  // A DFP: detached, drained its packing list, RIB-OUT identical to the group.
  group_->markPeerDetached(adjRib);
  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  adjRib->setAdjRibFlag(AdjRib::IS_DETACHED_FAST_PEER);
  ASSERT_EQ(group_->getNumInSyncPeers(), 0);

  group_->handleNoSyncPeers();

  // checkAndAcceptReadyToJoinPeers promotes the DFP, restoring sync.
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_TRUE(group_->isPeerInSync(0));
  EXPECT_EQ(group_->getNumInSyncPeers(), 1);
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib));
}

TEST_F(
    UpdateGroupDetachedPeerTest,
    HandleNoSyncPeersWithSharingDspKeepsConsumerRunning) {
  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;
  group_->setChangeListTracker(changeTracker, addPathBitmap, nonAddPathBitmap);
  group_->registerGroupConsumer();

  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  group_->markPeerInSync(adjRib0);
  group_->markPeerInSync(adjRib1);
  group_->setLastSeenRibVersion(42);

  /*
   * adjRib0 detaches after join -> DSP that still shares the group's entries
   * (detachedRibVersion > 0, counted in numPeersDetachedAfterJoin_).
   */
  group_->detachSlowPeer(adjRib0);
  ASSERT_GT(adjRib0->getDetachedRibVersion(), 0);
  ASSERT_EQ(group_->getNumPeersDetachedAfterJoin(), 1);
  // Drop the last in-sync peer too, leaving the group with no SYNC peers.
  group_->markPeerDetached(adjRib1);
  ASSERT_EQ(group_->getNumInSyncPeers(), 0);

  group_->handleNoSyncPeers();

  /*
   * Nothing is promoted until both consumers reach the end, but the group
   * consumer remains active so the sharing DSP cannot wedge behind it.
   */
  EXPECT_EQ(group_->getNumInSyncPeers(), 0);
  EXPECT_NE(adjRib0->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_TRUE(group_->getDetachedPeers().contains(adjRib0));
  ASSERT_NE(group_->changeListConsumeTimer_, nullptr);
  EXPECT_TRUE(group_->changeListConsumeTimer_->isScheduled());

  adjRib0->resetChangeListConsumer();
  group_->resetChangeListConsumer();
}

TEST_F(UpdateGroupDetachedPeerTest, HandleNoSyncPeersPromotesReadyDepA) {
  auto adjRib = createAndRegisterPeer(0);

  /*
   * Wire the group's tracker so promoteDetachedPeerToSync can rebuild the
   * group consumer; give the peer its own CL consumer to join at.
   */
  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;
  group_->setChangeListTracker(changeTracker, addPathBitmap, nonAddPathBitmap);
  adjRib->registerDetachedConsumer(
      changeTracker, addPathBitmap, nonAddPathBitmap);

  /*
   * DEP-A: detached ahead of the group on the CL (detachedRibVersion 0), with
   * its packing list drained (DETACHED_READY_TO_JOIN).
   */
  group_->markPeerDetached(adjRib);
  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  adjRib->setLastSeenRibVersion(99);
  ASSERT_EQ(group_->getNumInSyncPeers(), 0);
  ASSERT_EQ(group_->getNumPeersDetachedAfterJoin(), 0);

  group_->handleNoSyncPeers();

  /*
   * No SYNC peer and no sharing DSP -> the ready DEP-A is promoted to SYNC via
   * promoteDetachedPeerToSync.
   */
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_TRUE(group_->isPeerInSync(0));
  EXPECT_EQ(group_->getNumInSyncPeers(), 1);
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib));
  EXPECT_EQ(adjRib->getStats().getNumTimesRejoined(), 1);

  group_->resetChangeListConsumer();
}

TEST_F(UpdateGroupDetachedPeerTest, HandleNoSyncPeersNoReadyPeerStaysFrozen) {
  auto adjRib = createAndRegisterPeer(0);
  /*
   * DEP-A ahead of the group but still draining (not DETACHED_READY_TO_JOIN),
   * detachedRibVersion 0 so there is no sharing DSP either.
   */
  group_->markPeerDetached(adjRib);
  adjRib->setPeerState(PeerUpdateState::DETACHED_RUNNING);
  ASSERT_EQ(group_->getNumInSyncPeers(), 0);
  ASSERT_EQ(group_->getNumPeersDetachedAfterJoin(), 0);

  group_->handleNoSyncPeers();

  /*
   * Nothing is ready to promote -> the group stays frozen and the peer is
   * untouched.
   */
  EXPECT_EQ(group_->getNumInSyncPeers(), 0);
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::DETACHED_RUNNING);
  EXPECT_TRUE(group_->getDetachedPeers().contains(adjRib));
}

/*
 * lazyCloneLiteForDetachedPeers() tests
 */

TEST_F(UpdateGroupDetachedPeerTest, LazyCloneNoDetachedPeersNoClone) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();

  // No detached peers
  ASSERT_TRUE(group_->getDetachedPeers().empty());

  // Set up a group entry
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  auto groupEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  groupEntry->setPostAttr(std::shared_ptr<const BgpPath>(attrs));
  groupEntry->setRibVersion(10);

  // Call lazyCloneLiteForDetachedPeers — should be a no-op
  auto radixNodeItr =
      group_->getRadixNodeItrFromLiteTree(group_->LiteTree_, kV4Prefix1);
  group_->lazyCloneLiteForDetachedPeers(kV4Prefix1, radixNodeItr, groupEntry);

  // No peer entry should have been created
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey),
      nullptr);
}

TEST_F(UpdateGroupDetachedPeerTest, LazyCloneClonesForSharingPeerOnly) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey0 = adjRib0->getPeerOwnerKey();
  auto peerOwnerKey1 = adjRib1->getPeerOwnerKey();

  // Both peers are detached
  group_->markPeerDetached(adjRib0);
  group_->markPeerDetached(adjRib1);

  // Set up a group entry that predates both peers' detach versions
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  auto groupEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  groupEntry->setPostAttr(std::shared_ptr<const BgpPath>(attrs));
  groupEntry->setRibVersion(10);

  adjRib0->setDetachedRibVersion(50);
  adjRib1->setDetachedRibVersion(50);

  // Peer 1 already has its own entry — should NOT get a clone
  group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, peerOwnerKey1, kPlaceholderPathID);

  // Call lazyCloneLiteForDetachedPeers
  auto radixNodeItr =
      group_->getRadixNodeItrFromLiteTree(group_->LiteTree_, kV4Prefix1);
  group_->lazyCloneLiteForDetachedPeers(kV4Prefix1, radixNodeItr, groupEntry);

  // Peer 0 was sharing the entry → should get a clone
  auto peerEntry0 =
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey0);
  ASSERT_NE(peerEntry0, nullptr);
  EXPECT_EQ(peerEntry0->getPostAttr(), groupEntry->getPostAttr());
  EXPECT_EQ(peerEntry0->getRibVersion(), groupEntry->getRibVersion());

  /*
   * Peer 1 already had its own entry — should NOT get overwritten
   * (shouldCloneLiteForPeer returns false for Case 1)
   */
  auto peerEntry1 =
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey1);
  ASSERT_NE(peerEntry1, nullptr);
  // Peer 1's entry should have no postAttr (it was empty when we added it)
  EXPECT_EQ(peerEntry1->getPostAttr(), nullptr);
}

/**
 * Group withdrawal with a detached peer that was sharing the entry.
 * The entry should be cloned to the peer before being removed from the group.
 */
TEST_F(UpdateGroupDetachedPeerTest, WithdrawClonesEntryToDetachedPeer) {
  group_->groupKey_.afiIpv4Negotiated = true;
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib0->getPeerOwnerKey();

  // Set up group entry with ribVersion before peer detached
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  auto groupEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  groupEntry->setPreOut(attrs);
  groupEntry->setPostAttr(attrs);
  groupEntry->setRibVersion(10);

  // Detach peer 0 at ribVersion 50 (> entry's 10, so peer was sharing it)
  group_->markPeerDetached(adjRib0);
  adjRib0->setDetachedRibVersion(50);

  // Peer should not have its own entry yet
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey),
      nullptr);

  // Withdraw the prefix from the group
  group_->processGroupRibWithdraw(kV4Prefix1, kPlaceholderPathID);

  // Peer should now have a cloned entry
  auto peerEntry =
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey);
  ASSERT_NE(peerEntry, nullptr);
  EXPECT_EQ(peerEntry->getPreOut(), attrs);
  EXPECT_EQ(peerEntry->getPostAttr(), attrs);
  EXPECT_EQ(peerEntry->getRibVersion(), 10);
}

/*
 * Group withdrawal with no detached peers. No clone should happen.
 */
TEST_F(UpdateGroupDetachedPeerTest, WithdrawNoClonesWhenNoPeersDetached) {
  group_->groupKey_.afiIpv4Negotiated = true;
  auto adjRib0 = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();

  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  auto groupEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  groupEntry->setPreOut(attrs);
  groupEntry->setPostAttr(attrs);
  groupEntry->setRibVersion(10);

  // No peers detached
  EXPECT_TRUE(group_->getDetachedPeers().empty());

  group_->processGroupRibWithdraw(kV4Prefix1, kPlaceholderPathID);

  // No peer entry should exist
  auto peerOwnerKey = adjRib0->getPeerOwnerKey();
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey),
      nullptr);
}

/*
 * Group withdrawal skips clone when detached peer already has its own entry.
 */
TEST_F(UpdateGroupDetachedPeerTest, WithdrawSkipsCloneWhenPeerHasOwnEntry) {
  group_->groupKey_.afiIpv4Negotiated = true;
  auto adjRib0 = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib0->getPeerOwnerKey();

  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  auto peerAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(2, 2, 0, 0));

  auto groupEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  groupEntry->setPreOut(attrs);
  groupEntry->setPostAttr(attrs);
  groupEntry->setRibVersion(10);

  // Peer already has its own entry (diverged earlier)
  auto existingPeerEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, peerOwnerKey, kPlaceholderPathID);
  existingPeerEntry->setPreOut(peerAttrs);
  existingPeerEntry->setPostAttr(peerAttrs);

  group_->markPeerDetached(adjRib0);
  adjRib0->setDetachedRibVersion(50);

  group_->processGroupRibWithdraw(kV4Prefix1, kPlaceholderPathID);

  // Peer entry should still have its original attrs (not overwritten)
  auto peerEntry =
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey);
  ASSERT_NE(peerEntry, nullptr);
  EXPECT_EQ(peerEntry->getPostAttr(), peerAttrs);
}

/*
 * Group withdrawal skips clone when entry was announced after peer detached.
 */
TEST_F(
    UpdateGroupDetachedPeerTest,
    WithdrawSkipsCloneWhenEntryAnnouncedAfterDetach) {
  group_->groupKey_.afiIpv4Negotiated = true;
  auto adjRib0 = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib0->getPeerOwnerKey();

  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  auto groupEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  groupEntry->setPreOut(attrs);
  groupEntry->setPostAttr(attrs);
  groupEntry->setRibVersion(100); // Announced after peer detached

  group_->markPeerDetached(adjRib0);
  adjRib0->setDetachedRibVersion(50); // Detached at version 50

  group_->processGroupRibWithdraw(kV4Prefix1, kPlaceholderPathID);

  // No clone — peer never saw this entry
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey),
      nullptr);
}

/*
 * Multiple peers in different states during group withdrawal:
 * - Peer 1 (bit 0): detached, no own entry → gets clone
 * - Peer 2 (bit 1): detached, already has own entry → no clone (keeps original)
 * - Peer 3 (bit 2): not detached (joined) → no clone
 */
TEST_F(UpdateGroupDetachedPeerTest, WithdrawMultiplePeersMixedCloneBehavior) {
  group_->groupKey_.afiIpv4Negotiated = true;
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  auto adjRib2 = createAndRegisterPeer(2);

  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey0 = adjRib0->getPeerOwnerKey();
  auto peerOwnerKey1 = adjRib1->getPeerOwnerKey();
  auto peerOwnerKey2 = adjRib2->getPeerOwnerKey();

  auto groupAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  auto peer1Attrs = std::make_shared<BgpPath>(*buildBgpPathFields(2, 2, 0, 0));

  // Set up group entry
  auto groupEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  groupEntry->setPreOut(groupAttrs);
  groupEntry->setPostAttr(groupAttrs);
  groupEntry->setRibVersion(10);

  // Peer 0: detached, no own entry → should get clone
  group_->markPeerDetached(adjRib0);
  adjRib0->setDetachedRibVersion(50);

  // Peer 1: detached, already has own entry → should NOT get clone
  group_->markPeerDetached(adjRib1);
  adjRib1->setDetachedRibVersion(50);
  auto existingPeer1Entry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, peerOwnerKey1, kPlaceholderPathID);
  existingPeer1Entry->setPreOut(peer1Attrs);
  existingPeer1Entry->setPostAttr(peer1Attrs);

  // Peer 2: not detached → should NOT get clone

  group_->processGroupRibWithdraw(kV4Prefix1, kPlaceholderPathID);

  // Peer 0: should have a cloned entry with group attrs
  auto peer0Entry =
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey0);
  ASSERT_NE(peer0Entry, nullptr);
  EXPECT_EQ(peer0Entry->getPreOut(), groupAttrs);
  EXPECT_EQ(peer0Entry->getPostAttr(), groupAttrs);
  EXPECT_EQ(peer0Entry->getRibVersion(), 10);

  // Peer 1: should still have its original entry (not overwritten)
  auto peer1Entry =
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey1);
  ASSERT_NE(peer1Entry, nullptr);
  EXPECT_EQ(peer1Entry->getPostAttr(), peer1Attrs);

  // Peer 2: should NOT have any entry
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey2),
      nullptr);
}

/**
 * Group withdrawal with sendAddPath=true exercises the PathTree code path
 * in copyEntryForOwner and lazyClonePathForDetachedPeers.
 */
TEST_F(UpdateGroupDetachedPeerTest, WithdrawClonesEntryAddPathMode) {
  group_->groupKey_.afiIpv4Negotiated = true;
  group_->groupKey_.sendAddPath = true;
  auto adjRib0 = createAndRegisterPeer(0);
  // Register a second peer so copyEntryForOwner uses peer owner key.
  auto adjRib1 = createAndRegisterPeer(1);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib0->getPeerOwnerKey();

  constexpr uint32_t kPathId = 42;

  // Set up group entry in PathTree
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  auto groupEntry = group_->addToPathTree(
      group_->PathTree_, kV4Prefix1, groupOwnerKey, kPathId);
  groupEntry->setPreOut(attrs);
  groupEntry->setPostAttr(attrs);
  groupEntry->setRibVersion(10);

  // Detach peer 0 at ribVersion 50 (> entry's 10, so peer was sharing it)
  group_->markPeerDetached(adjRib0);
  adjRib0->setDetachedRibVersion(50);

  // Peer should not have its own entry yet
  EXPECT_EQ(
      group_->getFromPathTree(
          group_->PathTree_, kV4Prefix1, peerOwnerKey, kPathId),
      nullptr);

  // Withdraw the prefix from the group
  group_->processGroupRibWithdraw(kV4Prefix1, kPathId);

  // Peer should now have a cloned entry in PathTree
  auto peerEntry = group_->getFromPathTree(
      group_->PathTree_, kV4Prefix1, peerOwnerKey, kPathId);
  ASSERT_NE(peerEntry, nullptr);
  EXPECT_EQ(peerEntry->getPreOut(), attrs);
  EXPECT_EQ(peerEntry->getPostAttr(), attrs);
  EXPECT_EQ(peerEntry->getRibVersion(), 10);
}

/*
 * shouldClonePathForPeer() tests — PathTree (add-path) variant.
 */

TEST_F(UpdateGroupDetachedPeerTest, ShouldClonePathFalseWhenPeerHasOwnEntry) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();

  constexpr uint32_t kPathId = 42;
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));

  auto groupEntry = group_->addToPathTree(
      group_->PathTree_, kV4Prefix1, groupOwnerKey, kPathId);
  groupEntry->setPostAttr(attrs);
  groupEntry->setRibVersion(10);

  group_->addToPathTree(group_->PathTree_, kV4Prefix1, peerOwnerKey, kPathId);

  adjRib->setDetachedRibVersion(50);

  auto radixNodeItr =
      group_->getRadixNodeItrFromPathTree(group_->PathTree_, kV4Prefix1);
  EXPECT_FALSE(group_->shouldClonePathForPeer(
      radixNodeItr, kPathId, adjRib, groupEntry->getRibVersion()));
}

TEST_F(
    UpdateGroupDetachedPeerTest,
    ShouldClonePathFalseWhenEntryAnnouncedAfterDetach) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();

  constexpr uint32_t kPathId = 42;
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));

  auto groupEntry = group_->addToPathTree(
      group_->PathTree_, kV4Prefix1, groupOwnerKey, kPathId);
  groupEntry->setPostAttr(attrs);
  groupEntry->setRibVersion(100);

  adjRib->setDetachedRibVersion(50);

  auto radixNodeItr =
      group_->getRadixNodeItrFromPathTree(group_->PathTree_, kV4Prefix1);
  EXPECT_FALSE(group_->shouldClonePathForPeer(
      radixNodeItr, kPathId, adjRib, groupEntry->getRibVersion()));
}

TEST_F(
    UpdateGroupDetachedPeerTest,
    ShouldClonePathTrueWhenPeerWasSharingEntry) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();

  constexpr uint32_t kPathId = 42;
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));

  auto groupEntry = group_->addToPathTree(
      group_->PathTree_, kV4Prefix1, groupOwnerKey, kPathId);
  groupEntry->setPostAttr(attrs);
  groupEntry->setRibVersion(10);

  adjRib->setDetachedRibVersion(50);

  auto radixNodeItr =
      group_->getRadixNodeItrFromPathTree(group_->PathTree_, kV4Prefix1);
  EXPECT_TRUE(group_->shouldClonePathForPeer(
      radixNodeItr, kPathId, adjRib, groupEntry->getRibVersion()));
}

/*
 * Announcement path lazy clone tests.
 * These verify that processRibAnnouncedEntryForGroup clones the existing
 * group entry to detached peers before mutating it with new attributes.
 */

/**
 * Re-announcement of a prefix clones old state to a detached peer that was
 * sharing the group entry, then updates the group entry with new attrs.
 */
TEST_F(UpdateGroupDetachedPeerTest, AnnouncementClonesEntryToDetachedPeer) {
  group_->groupKey_.afiIpv4Negotiated = true;
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib0->getPeerOwnerKey();

  // First announcement — establishes group entry at ribVersion 10
  auto oldAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  oldAttrs->publish();
  RibOutAnnouncementEntry entry1(
      kV4Prefix1,
      kPlaceholderPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      oldAttrs);
  entry1.ribVersion = 10;
  group_->processRibAnnouncedEntryForGroup(entry1);

  // Detach peer 0 at ribVersion 50 (> entry's 10, so peer was sharing it)
  group_->markPeerDetached(adjRib0);
  adjRib0->setDetachedRibVersion(50);

  // Peer should not have its own entry yet
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey),
      nullptr);

  // Re-announce same prefix with different attrs at ribVersion 60
  auto newAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(2, 2, 0, 0));
  newAttrs->publish();
  RibOutAnnouncementEntry entry2(
      kV4Prefix1,
      kPlaceholderPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      newAttrs);
  entry2.ribVersion = 60;
  group_->processRibAnnouncedEntryForGroup(entry2);

  // Peer should now have a cloned entry preserving the OLD state
  auto peerEntry =
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey);
  ASSERT_NE(peerEntry, nullptr);
  EXPECT_EQ(peerEntry->getPreOut(), oldAttrs);
  EXPECT_EQ(peerEntry->getRibVersion(), 10);

  // Group entry should have the NEW state
  auto groupEntry =
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, groupOwnerKey);
  ASSERT_NE(groupEntry, nullptr);
  EXPECT_EQ(groupEntry->getPreOut(), newAttrs);
  EXPECT_EQ(groupEntry->getRibVersion(), 60);
}

/*
 * Re-announcement with no detached peers. No clone should happen.
 */
TEST_F(UpdateGroupDetachedPeerTest, AnnouncementNoClonesWhenNoPeersDetached) {
  group_->groupKey_.afiIpv4Negotiated = true;
  auto adjRib0 = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();

  // First announcement
  auto oldAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  oldAttrs->publish();
  RibOutAnnouncementEntry entry1(
      kV4Prefix1,
      kPlaceholderPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      oldAttrs);
  entry1.ribVersion = 10;
  group_->processRibAnnouncedEntryForGroup(entry1);

  // No peers detached
  EXPECT_TRUE(group_->getDetachedPeers().empty());

  // Re-announce with different attrs
  auto newAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(2, 2, 0, 0));
  newAttrs->publish();
  RibOutAnnouncementEntry entry2(
      kV4Prefix1,
      kPlaceholderPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      newAttrs);
  entry2.ribVersion = 20;
  group_->processRibAnnouncedEntryForGroup(entry2);

  // No peer entry should exist
  auto peerOwnerKey = adjRib0->getPeerOwnerKey();
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey),
      nullptr);
}

/*
 * Re-announcement skips clone when detached peer already has its own entry.
 */
TEST_F(UpdateGroupDetachedPeerTest, AnnouncementSkipsCloneWhenPeerHasOwnEntry) {
  group_->groupKey_.afiIpv4Negotiated = true;
  auto adjRib0 = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib0->getPeerOwnerKey();

  // First announcement
  auto oldAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  oldAttrs->publish();
  RibOutAnnouncementEntry entry1(
      kV4Prefix1,
      kPlaceholderPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      oldAttrs);
  entry1.ribVersion = 10;
  group_->processRibAnnouncedEntryForGroup(entry1);

  // Peer already has its own entry (diverged earlier)
  auto peerAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(3, 3, 0, 0));
  auto existingPeerEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, peerOwnerKey, kPlaceholderPathID);
  existingPeerEntry->setPreOut(peerAttrs);
  existingPeerEntry->setPostAttr(peerAttrs);

  group_->markPeerDetached(adjRib0);
  adjRib0->setDetachedRibVersion(50);

  // Re-announce with different attrs
  auto newAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(2, 2, 0, 0));
  newAttrs->publish();
  RibOutAnnouncementEntry entry2(
      kV4Prefix1,
      kPlaceholderPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      newAttrs);
  entry2.ribVersion = 60;
  group_->processRibAnnouncedEntryForGroup(entry2);

  // Peer entry should still have its original attrs (not overwritten by clone)
  auto peerEntry =
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey);
  ASSERT_NE(peerEntry, nullptr);
  EXPECT_EQ(peerEntry->getPostAttr(), peerAttrs);
}

/*
 * Re-announcement skips clone when the first announcement happened after
 * the peer detached (peer never saw this prefix).
 */
TEST_F(
    UpdateGroupDetachedPeerTest,
    AnnouncementSkipsCloneWhenEntryAnnouncedAfterDetach) {
  group_->groupKey_.afiIpv4Negotiated = true;
  auto adjRib0 = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib0->getPeerOwnerKey();

  // Detach peer 0 at ribVersion 50
  group_->markPeerDetached(adjRib0);
  adjRib0->setDetachedRibVersion(50);

  // First announcement at ribVersion 100 (after detach)
  auto oldAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  oldAttrs->publish();
  RibOutAnnouncementEntry entry1(
      kV4Prefix1,
      kPlaceholderPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      oldAttrs);
  entry1.ribVersion = 100;
  group_->processRibAnnouncedEntryForGroup(entry1);

  // Re-announce with different attrs at ribVersion 110
  auto newAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(2, 2, 0, 0));
  newAttrs->publish();
  RibOutAnnouncementEntry entry2(
      kV4Prefix1,
      kPlaceholderPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      newAttrs);
  entry2.ribVersion = 110;
  group_->processRibAnnouncedEntryForGroup(entry2);

  // No clone — peer never saw this entry (both versions > detachedRibVersion)
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey),
      nullptr);
}

/*
 * First announcement of a brand new prefix with detached peers.
 * No lazy clone needed since there's no existing entry to preserve.
 */
TEST_F(UpdateGroupDetachedPeerTest, AnnouncementNoClonesForNewPrefix) {
  group_->groupKey_.afiIpv4Negotiated = true;
  auto adjRib0 = createAndRegisterPeer(0);
  auto peerOwnerKey = adjRib0->getPeerOwnerKey();

  // Detach peer 0 but don't create any group entries
  group_->markPeerDetached(adjRib0);
  adjRib0->setDetachedRibVersion(50);

  // First announcement — creates a fresh entry, no clone needed
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  attrs->publish();
  RibOutAnnouncementEntry entry(
      kV4Prefix1,
      kPlaceholderPathID,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      attrs);
  entry.ribVersion = 100;
  group_->processRibAnnouncedEntryForGroup(entry);

  // No peer entry should exist (nothing to clone from)
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey),
      nullptr);
}

/**
 * Re-announcement with sendAddPath=true exercises the PathTree code path.
 */
TEST_F(UpdateGroupDetachedPeerTest, AnnouncementClonesEntryAddPathMode) {
  group_->groupKey_.afiIpv4Negotiated = true;
  group_->groupKey_.sendAddPath = true;
  auto adjRib0 = createAndRegisterPeer(0);
  // Register a second peer so copyEntryForOwner uses peer owner key.
  auto adjRib1 = createAndRegisterPeer(1);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib0->getPeerOwnerKey();

  constexpr uint32_t kPathId = 42;

  // First announcement at ribVersion 10
  auto oldAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  oldAttrs->publish();
  RibOutAnnouncementEntry entry1(
      kV4Prefix1,
      kPathId,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      oldAttrs);
  entry1.ribVersion = 10;
  group_->processRibAnnouncedEntryForGroup(entry1);

  // Detach peer 0 at ribVersion 50 (> entry's 10, so peer was sharing it)
  group_->markPeerDetached(adjRib0);
  adjRib0->setDetachedRibVersion(50);

  // Peer should not have its own entry yet
  EXPECT_EQ(
      group_->getFromPathTree(
          group_->PathTree_, kV4Prefix1, peerOwnerKey, kPathId),
      nullptr);

  // Re-announce with different attrs at ribVersion 60
  auto newAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(2, 2, 0, 0));
  newAttrs->publish();
  RibOutAnnouncementEntry entry2(
      kV4Prefix1,
      kPathId,
      TinyPeerInfo(
          folly::IPAddress("1.1.1.1"), 65000, 1, BgpSessionType::EBGP, false),
      newAttrs);
  entry2.ribVersion = 60;
  group_->processRibAnnouncedEntryForGroup(entry2);

  // Peer should now have a cloned entry preserving the OLD state
  auto peerEntry = group_->getFromPathTree(
      group_->PathTree_, kV4Prefix1, peerOwnerKey, kPathId);
  ASSERT_NE(peerEntry, nullptr);
  EXPECT_EQ(peerEntry->getPreOut(), oldAttrs);
  EXPECT_EQ(peerEntry->getRibVersion(), 10);

  // Group entry should have the NEW state
  auto groupEntry = group_->getFromPathTree(
      group_->PathTree_, kV4Prefix1, groupOwnerKey, kPathId);
  ASSERT_NE(groupEntry, nullptr);
  EXPECT_EQ(groupEntry->getPreOut(), newAttrs);
  EXPECT_EQ(groupEntry->getRibVersion(), 60);
}

TEST_F(
    UpdateGroupDetachedPeerTest,
    UnregisterDetachedPeerCleansUpPerPeerLiteTreeEntries) {
  auto adjRib = createAndRegisterPeer(0);
  auto peerOwnerKey = adjRib->getPeerOwnerKey();
  auto groupOwnerKey = group_->getGroupOwnerKey();

  adjRib->setPeerState(PeerUpdateState::DETACHED_RUNNING);
  adjRib->setDetachedRibVersion(50);
  group_->markPeerDetached(adjRib);

  // Add per-peer entry (diverged from group)
  group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, peerOwnerKey, kPlaceholderPathID);

  /*
   * Add group entry that the peer was sharing (ribVersion <=
   * detachedRibVersion)
   */
  auto groupEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix2, groupOwnerKey, kPlaceholderPathID);
  groupEntry->setRibVersion(10);

  ASSERT_NE(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey),
      nullptr);
  ASSERT_NE(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix2, groupOwnerKey),
      nullptr);

  group_->unregisterPeer(adjRib);

  // Per-peer entry should be deleted
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey),
      nullptr);

  // Group entry should still exist (shared group-owned entry, not deleted)
  EXPECT_NE(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix2, groupOwnerKey),
      nullptr);

  // Peer removed from detached set
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib));

  // Remove from peers_ so TearDown doesn't double-unregister
  peers_.erase(std::remove(peers_.begin(), peers_.end(), adjRib), peers_.end());
}

TEST_F(
    UpdateGroupDetachedPeerTest,
    UnregisterDetachedPeerCleansUpPerPeerPathTreeEntries) {
  auto adjRib = createAndRegisterPeer(0);
  adjRib->sendAddPath_ = true;
  auto peerOwnerKey = adjRib->getPeerOwnerKey();
  auto groupOwnerKey = group_->getGroupOwnerKey();

  adjRib->setPeerState(PeerUpdateState::DETACHED_RUNNING);
  adjRib->setDetachedRibVersion(50);
  group_->markPeerDetached(adjRib);

  // Add per-peer path entries (diverged)
  group_->addToPathTree(
      group_->PathTree_, kV4Prefix1, peerOwnerKey, /*pathId=*/1);
  group_->addToPathTree(
      group_->PathTree_, kV4Prefix1, peerOwnerKey, /*pathId=*/2);

  // Add group entry the peer was sharing
  auto groupEntry = group_->addToPathTree(
      group_->PathTree_, kV4Prefix2, groupOwnerKey, /*pathId=*/1);
  groupEntry->setRibVersion(10);

  ASSERT_NE(
      group_->getFromPathTree(group_->PathTree_, kV4Prefix1, peerOwnerKey, 1),
      nullptr);
  ASSERT_NE(
      group_->getFromPathTree(group_->PathTree_, kV4Prefix1, peerOwnerKey, 2),
      nullptr);

  group_->unregisterPeer(adjRib);

  // Per-peer entries should be deleted
  EXPECT_EQ(
      group_->getFromPathTree(group_->PathTree_, kV4Prefix1, peerOwnerKey, 1),
      nullptr);
  EXPECT_EQ(
      group_->getFromPathTree(group_->PathTree_, kV4Prefix1, peerOwnerKey, 2),
      nullptr);

  // Group entry should still exist
  EXPECT_NE(
      group_->getFromPathTree(group_->PathTree_, kV4Prefix2, groupOwnerKey, 1),
      nullptr);

  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib));

  peers_.erase(std::remove(peers_.begin(), peers_.end(), adjRib), peers_.end());
}

/*
 * Lifecycle tests for slow peer detachment and rejoin flow.
 *
 * These tests exercise the full lifecycle:
 *   peer joins → blocks → detaches → unblocks → DFP/DSP → ready to rejoin
 *
 * Uses a richer fixture with change tracker infrastructure pre-wired.
 */
class UpdateGroupDetachLifecycleTest : public ::testing::Test {
 protected:
  void SetUp() override {
    folly::SingletonVault::singleton()->registrationComplete();
    evb_ = std::make_unique<folly::EventBase>();
    UpdateGroupConfig ugConfig;
    ugConfig.enableSlowPeerBlockCountDetection = true;
    ugConfig.allowSlowPeerDetach = true;
    UpdateGroupKey ugKey;
    ugKey.afiIpv4Negotiated = true;
    ugKey.afiIpv6Negotiated = true;
    ugKey.sessionType = BgpSessionType::EBGP;
    group_ = std::make_shared<AdjRibOutGroup>(
        *evb_,
        "test_group",
        1,
        true /* enableUpdateGroup */,
        ugKey,
        ShadowRibView::empty() /* shadowRib */,
        nullptr /* policyManager */,
        ugConfig);

    // Set up change tracker infrastructure
    changeTracker_ =
        std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");

    group_->setChangeListTracker(
        changeTracker_, addPathBitmap_, nonAddPathBitmap_);
    group_->registerGroupConsumer();
    groupConsumer_ = group_->getChangeListConsumer();
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

    /* Drain asyncScope_ cooperatively before destroying the group */
    folly::coro::blockingWait(group_->drainAsyncScope());
    group_.reset();

    /*
     * activateChangeListConsumer() schedules sendBgpUpdates on evb_, which
     * these tests never loop. ~AdjRib() would then block forever joining a
     * task that cannot start, so cancel and join each peer's scope while
     * driving evb_, as AdjRib::stop() does in production.
     */
    for (auto& peer : peers_) {
      if (peer->asyncScope_) {
        folly::coro::blockingWait(
            peer->asyncScope_->cancelAndJoinAsync(), evb_.get());
        peer->asyncScope_.reset();
      }
    }
    peers_.clear();
    evb_.reset();
  }

  std::shared_ptr<AdjRib> createAndRegisterPeer(uint64_t bit) {
    /*
     * Distinct per-bit IP so each peer gets a distinct peer owner key (keyed on
     * the peer address); required for tests with multiple peers that each carry
     * their own per-peer RIB-OUT entries.
     */
    auto peerId = nettools::bgplib::BgpPeerId(
        folly::IPAddress(fmt::format("10.0.0.{}", bit + 1)),
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
    adjRib->enableUpdateGroup_ = true;
    /*
     * Map the peer onto its bit without going through registerPeer(): these
     * fixtures need a peer that is registered but neither in sync nor
     * detached, and no public entry point produces that state. Reaching in
     * directly is possible because both fixtures are friends of
     * AdjRibOutGroup.
     */
    adjRib->setGroupBitPosition(bit);
    group_->bitToAdjRibs_[bit] = adjRib;
    peers_.push_back(adjRib);
    return adjRib;
  }

  /*
   * Set up a peer as JOINED_RUNNING with sync bit set.
   */
  void setUpJoinedRunningPeer(
      const std::shared_ptr<AdjRib>& adjRib,
      uint64_t bit) {
    /* markPeerInSync() resolves the bit off the peer; check they agree. */
    ASSERT_EQ(bit, adjRib->getGroupBitPosition());
    adjRib->setPeerState(PeerUpdateState::JOINED_RUNNING);
    group_->markPeerInSync(adjRib);
  }

  /*
   * Publish a change item to the change tracker so the group consumer
   * has a non-null marker.
   */
  TrackableObject<ShadowRibEntry>* publishChangeItem(
      const folly::CIDRNetwork& prefix) {
    auto entry = std::make_unique<ShadowRibEntry>();
    entry->prefix = prefix;
    auto trackable =
        std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(*entry));
    auto* ptr = trackable.get();
    trackableObjects_.push_back(std::move(trackable));
    changeTracker_->publishChange(ptr);
    return ptr;
  }

  /*
   * Set the group consumer to ready state so groupAtEndOfCL is true.
   * Required for DSP peers to be eligible in checkAndAcceptReadyToJoinPeers.
   */
  void setGroupConsumerReady() {
    groupConsumer_->setMarker(nullptr);
  }

  bool isGroupConsumeTimerScheduled() const {
    return group_->changeListConsumeTimer_ &&
        group_->changeListConsumeTimer_->isScheduled();
  }

  /*
   * Set up a peer's CL consumer in ready state so canWaitForGroupToRejoin()
   * returns true. Required for DSP peers going through
   * checkAndAcceptReadyToJoinPeers. canWaitForGroupToRejoin() gates on the
   * peer's lastSeenRibVersion matching the group's, so bring the peer up to the
   * group's version to reflect a caught-up peer.
   */
  void setUpReadyPeerConsumer(const std::shared_ptr<AdjRib>& adjRib) {
    auto peerConsumer = std::make_shared<AdjRibOutConsumer>(
        changeTracker_,
        adjRib,
        "peer_consumer",
        *evb_,
        addPathBitmap_,
        nonAddPathBitmap_);
    adjRib->setChangeListConsumer(peerConsumer);
    peerConsumer->setMarker(nullptr);
    adjRib->setLastSeenRibVersion(group_->getLastSeenRibVersion());
  }

  /*
   * Add a prefix to the group packing list.
   */
  void addPrefixToGroupPL(
      const folly::CIDRNetwork& prefix,
      uint32_t localPref = 100) {
    auto attrs = std::make_shared<BgpPath>(BgpPathFields());
    attrs->setLocalPref(localPref);
    std::shared_ptr<const BgpPath> constAttrs = attrs;
    group_->tryUpdateAttrToPrefixMapForGroup(
        std::make_pair(prefix, kPlaceholderPathID), nullptr, constAttrs);
  }

  std::unique_ptr<folly::EventBase> evb_;
  std::shared_ptr<AdjRibOutGroup> group_;
  std::shared_ptr<ChangeTracker<ShadowRibEntry>> changeTracker_;
  std::shared_ptr<AdjRibOutGroupConsumer> groupConsumer_;
  ConsumerBitmap addPathBitmap_;
  ConsumerBitmap nonAddPathBitmap_;
  std::vector<std::shared_ptr<AdjRib>> peers_;
  std::vector<std::unique_ptr<TrackableObject<ShadowRibEntry>>>
      trackableObjects_;
  bool verifyInAdjRibPackingList(
      const std::shared_ptr<AdjRib>& adjRib,
      const std::shared_ptr<const BgpPath>& attrs,
      const folly::CIDRNetwork& prefix,
      uint32_t pathId = kPlaceholderPathID) {
    for (const auto& [key, prefixes] : adjRib->attrToPrefixMap_) {
      if (BgpPathCompareWithNull{}(key.attrs, attrs) &&
          prefixes.contains({prefix, pathId})) {
        return true;
      }
    }
    return false;
  }

  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ_{
      nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<AdjRib::ObservableMessageT> observerQ_;
};

/*
 * unregisterPeer -> removePeer no-sync-peers recovery tests
 */

/*
 * Unregistering the last SYNC peer triggers the removePeer recovery sweep,
 * which immediately promotes every ready detached peer: 3 DFPs (RIB-OUT
 * identical to the group, accepted directly), 1 DSP needing a full entry
 * collapse (per-peer copies of all prefixes), and 2 DSPs needing a partial
 * collapse (one per-peer copy plus already-shared group entries). All 6 end up
 * SYNC with their RIB-OUT shared with the group.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    UnregisterLastSyncPeerPromotesDfpsAndCollapsesDsps) {
  const std::array<folly::CIDRNetwork, 3> prefixes = {
      kV4Prefix1, kV4Prefix2, kV4Prefix3};
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));

  /*
   * Group RIB-OUT: 3 prefixes (ribVersion 10). The cached version is bumped
   * past them so a sync peer shares all of them.
   */
  for (const auto& prefix : prefixes) {
    auto* g = group_->addToLiteTree(
        group_->LiteTree_, prefix, groupOwnerKey, kPlaceholderPathID);
    g->setPostAttr(attrs);
    g->setRibVersion(10);
  }
  group_->setLastSeenRibVersion(100);

  // The lone SYNC peer; unregistering it drives the no-sync-peers sweep.
  auto syncPeer = createAndRegisterPeer(0);
  setUpJoinedRunningPeer(syncPeer, 0);

  std::vector<std::shared_ptr<AdjRib>> promoted;

  // 3 DFPs: RIB-OUT identical to the group (no per-peer entries).
  for (uint64_t bit : {1, 2, 3}) {
    auto dfp = createAndRegisterPeer(bit);
    dfp->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
    dfp->setAdjRibFlag(AdjRib::IS_DETACHED_FAST_PEER);
    group_->markPeerDetached(dfp);
    promoted.push_back(dfp);
  }

  /*
   * 1 DSP needing a FULL collapse: per-peer copies of all 3 prefixes (matching
   * the group), all erased on collapse.
   */
  auto dspFull = createAndRegisterPeer(4);
  /*
   * Establish a detached-after-join peer via the production path: detachPeer
   * marks it detached AND counts it in numPeersDetachedAfterJoin_ (balanced
   * against the decrement its rejoin performs), setting detachedRibVersion to
   * the group's version. Then present it as a ready-to-rejoin DSP.
   */
  group_->markPeerInSync(dspFull);
  group_->detachPeer(dspFull, AdjRibOutGroup::DetachReason::Blocking);
  dspFull->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  setUpReadyPeerConsumer(dspFull);
  for (const auto& prefix : prefixes) {
    group_
        ->addToLiteTree(
            group_->LiteTree_,
            prefix,
            dspFull->getPeerOwnerKey(),
            kPlaceholderPathID)
        ->setPostAttr(attrs);
  }
  promoted.push_back(dspFull);

  /*
   * 2 DSPs needing a PARTIAL collapse: one per-peer copy (kV4Prefix1) plus two
   * already-shared group entries (ribVersion 10 <= the peer's
   * detachedRibVersion, which detachPeer set to the group's version).
   */
  for (uint64_t bit : {5, 6}) {
    auto dsp = createAndRegisterPeer(bit);
    /*
     * Detached-after-join via the production path (detachPeer counts it), then
     * presented as a ready-to-rejoin DSP.
     */
    group_->markPeerInSync(dsp);
    group_->detachPeer(dsp, AdjRibOutGroup::DetachReason::Blocking);
    dsp->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
    setUpReadyPeerConsumer(dsp);
    group_
        ->addToLiteTree(
            group_->LiteTree_,
            kV4Prefix1,
            dsp->getPeerOwnerKey(),
            kPlaceholderPathID)
        ->setPostAttr(attrs);
    promoted.push_back(dsp);
  }

  setGroupConsumerReady();

  // Remove the last SYNC peer -> immediate promotion sweep runs.
  group_->unregisterPeer(syncPeer);

  // All 6 detached peers became SYNC.
  EXPECT_EQ(group_->getNumInSyncPeers(), 6);
  for (const auto& peer : promoted) {
    EXPECT_EQ(peer->getPeerState(), PeerUpdateState::JOINED_RUNNING);
    EXPECT_TRUE(group_->isPeerInSync(peer->getGroupBitPosition()));
    EXPECT_FALSE(group_->getDetachedPeers().contains(peer));
  }

  // Every promoted peer now resolves each prefix to the shared group entry.
  for (const auto& peer : promoted) {
    for (const auto& prefix : prefixes) {
      auto [entry, isPerPeerEntry] = group_->getRibEntrySharedOrPeer(
          prefix,
          peer->getPeerOwnerKey(),
          kPlaceholderPathID,
          group_->getLastSeenRibVersion());
      EXPECT_NE(entry, nullptr);
      EXPECT_FALSE(isPerPeerEntry); // shared with the group
    }
  }
}

/*
 * Unregistering the last SYNC peer when only a detached peer ahead of the group
 * (DEP-A) remains promotes it via promoteDetachedPeerToSync: the peer's RIB-OUT
 * becomes the new group truth (group-only entries it never had are dropped).
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    UnregisterLastSyncPeerPromotesAheadDepAAndAdoptsRibOut) {
  auto groupOwnerKey = group_->getGroupOwnerKey();

  /*
   * "Random" existing group RIB-OUT: kV4Prefix1 (also owned by the peer) and
   * kV4Prefix4 (group-only, the peer never had it).
   */
  auto groupAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  for (const auto& prefix : {kV4Prefix1, kV4Prefix4}) {
    group_
        ->addToLiteTree(
            group_->LiteTree_, prefix, groupOwnerKey, kPlaceholderPathID)
        ->setPostAttr(groupAttrs);
  }
  group_->setLastSeenRibVersion(50);

  auto syncPeer = createAndRegisterPeer(0);
  setUpJoinedRunningPeer(syncPeer, 0);

  /*
   * DEP-A: detached ahead of the group (detachedRibVersion 0), with its own
   * diverged RIB-OUT for 3 prefixes.
   */
  auto depA = createAndRegisterPeer(1);
  depA->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  depA->setLastSeenRibVersion(100); // ahead of the group (50)
  group_->markPeerDetached(depA);
  setUpReadyPeerConsumer(depA);
  /*
   * Group consumer not at end of CL -> peer is "ahead", not
   * collapse-rejoinable, forcing the promoteDetachedPeerToSync path.
   */
  publishChangeItem(folly::CIDRNetwork{folly::IPAddress("10.1.0.0"), 24});

  auto peerOwnerKey = depA->getPeerOwnerKey();
  auto peerAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(2, 0, 0, 0));
  std::vector<std::pair<folly::CIDRNetwork, AdjRibEntry*>> peerEntries;
  for (const auto& prefix : {kV4Prefix1, kV4Prefix2, kV4Prefix3}) {
    auto* e = group_->addToLiteTree(
        group_->LiteTree_, prefix, peerOwnerKey, kPlaceholderPathID);
    e->setPostAttr(peerAttrs);
    peerEntries.emplace_back(prefix, e);
  }

  group_->unregisterPeer(syncPeer);

  // DEP-A promoted to SYNC; group adopted its RIB version.
  EXPECT_EQ(depA->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_TRUE(group_->isPeerInSync(1));
  EXPECT_FALSE(group_->getDetachedPeers().contains(depA));
  EXPECT_EQ(group_->getNumInSyncPeers(), 1);
  EXPECT_EQ(group_->getLastSeenRibVersion(), 100);

  /*
   * The group RIB-OUT is now exactly the peer's old RIB-OUT: each peer entry
   * was re-keyed under the group owner key (same object) and the peer key is
   * gone.
   */
  for (const auto& [prefix, entry] : peerEntries) {
    EXPECT_EQ(
        group_->getFromLiteTree(group_->LiteTree_, prefix, groupOwnerKey),
        entry);
    EXPECT_EQ(
        group_->getFromLiteTree(group_->LiteTree_, prefix, peerOwnerKey),
        nullptr);
  }
  // The group-only entry the peer never had was deleted.
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix4, groupOwnerKey),
      nullptr);

  /*
   * promoteDetachedPeerToSync rebuilt the group consumer; point the fixture at
   * the live one so TearDown tears down the right consumer.
   */
  groupConsumer_ = group_->getChangeListConsumer();
}

/*
 * Unregistering the last SYNC peer with no promotable detached peer leaves the
 * group frozen: nothing is promoted and it stays sync-less.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    UnregisterLastSyncPeerWithNoPromotablePeerStaysFrozen) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);

  setUpJoinedRunningPeer(adjRib0, 0);
  ASSERT_EQ(group_->getNumInSyncPeers(), 1);

  /*
   * adjRib1 is detached but still draining (not DETACHED_READY_TO_JOIN), so
   * nothing is promotable.
   */
  group_->markPeerDetached(adjRib1);
  adjRib1->setPeerState(PeerUpdateState::DETACHED_RUNNING);

  group_->unregisterPeer(adjRib0);

  /*
   * No promotable peer -> group stays frozen with no SYNC peers, adjRib1
   * untouched.
   */
  EXPECT_EQ(group_->getNumInSyncPeers(), 0);
  EXPECT_EQ(adjRib1->getPeerState(), PeerUpdateState::DETACHED_RUNNING);
  EXPECT_TRUE(group_->getDetachedPeers().contains(adjRib1));
}

/*
 * movePeer <-> handleNoSyncPeers integration: moving the last SYNC peer out of
 * the group (movePeer -> removePeer) triggers the same no-sync-peers recovery
 * sweep as unregisterPeer. These mirror the unregisterPeer recovery tests but
 * drive the sweep through movePeer, additionally checking the moved peer lands
 * in the target group as a detached peer.
 */

/*
 * Moving out the last SYNC peer runs the recovery sweep, immediately promoting
 * every ready detached peer: 3 DFPs (RIB-OUT identical to the group), 1 DSP
 * needing a full entry collapse, and 2 DSPs needing a partial collapse. All 6
 * end up SYNC with their RIB-OUT shared with the group.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    MoveLastSyncPeerPromotesDfpsAndCollapsesDsps) {
  auto targetGroup = std::make_shared<AdjRibOutGroup>(*evb_, "target_group");
  const std::array<folly::CIDRNetwork, 3> prefixes = {
      kV4Prefix1, kV4Prefix2, kV4Prefix3};
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));

  /*
   * Group RIB-OUT: 3 prefixes (ribVersion 10). The cached version is bumped
   * past them so a sync peer shares all of them.
   */
  for (const auto& prefix : prefixes) {
    auto* g = group_->addToLiteTree(
        group_->LiteTree_, prefix, groupOwnerKey, kPlaceholderPathID);
    g->setPostAttr(attrs);
    g->setRibVersion(10);
  }
  group_->setLastSeenRibVersion(100);

  // The lone SYNC peer; moving it out drives the no-sync-peers sweep.
  auto syncPeer = createAndRegisterPeer(0);
  setUpJoinedRunningPeer(syncPeer, 0);

  std::vector<std::shared_ptr<AdjRib>> promoted;

  // 3 DFPs: RIB-OUT identical to the group (no per-peer entries).
  for (uint64_t bit : {1, 2, 3}) {
    auto dfp = createAndRegisterPeer(bit);
    dfp->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
    dfp->setAdjRibFlag(AdjRib::IS_DETACHED_FAST_PEER);
    group_->markPeerDetached(dfp);
    promoted.push_back(dfp);
  }

  /*
   * 1 DSP needing a FULL collapse: per-peer copies of all 3 prefixes (matching
   * the group), all erased on collapse.
   */
  auto dspFull = createAndRegisterPeer(4);
  /*
   * Establish a detached-after-join peer via the production path: detachPeer
   * marks it detached AND counts it in numPeersDetachedAfterJoin_ (balanced
   * against the decrement its rejoin performs), setting detachedRibVersion to
   * the group's version. Then present it as a ready-to-rejoin DSP.
   */
  group_->markPeerInSync(dspFull);
  group_->detachPeer(dspFull, AdjRibOutGroup::DetachReason::Blocking);
  dspFull->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  setUpReadyPeerConsumer(dspFull);
  for (const auto& prefix : prefixes) {
    group_
        ->addToLiteTree(
            group_->LiteTree_,
            prefix,
            dspFull->getPeerOwnerKey(),
            kPlaceholderPathID)
        ->setPostAttr(attrs);
  }
  promoted.push_back(dspFull);

  /*
   * 2 DSPs needing a PARTIAL collapse: one per-peer copy (kV4Prefix1) plus two
   * already-shared group entries (ribVersion 10 <= the peer's
   * detachedRibVersion, which detachPeer set to the group's version).
   */
  for (uint64_t bit : {5, 6}) {
    auto dsp = createAndRegisterPeer(bit);
    /*
     * Detached-after-join via the production path (detachPeer counts it), then
     * presented as a ready-to-rejoin DSP.
     */
    group_->markPeerInSync(dsp);
    group_->detachPeer(dsp, AdjRibOutGroup::DetachReason::Blocking);
    dsp->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
    setUpReadyPeerConsumer(dsp);
    group_
        ->addToLiteTree(
            group_->LiteTree_,
            kV4Prefix1,
            dsp->getPeerOwnerKey(),
            kPlaceholderPathID)
        ->setPostAttr(attrs);
    promoted.push_back(dsp);
  }

  setGroupConsumerReady();

  /*
   * Move out the last SYNC peer, then run the caller-side recovery. movePeers
   * is a pure mover; the orchestrator (PeerManagerBase egress re-eval) runs
   * recoverIfNoSyncPeers once all moves are complete, which we mirror here.
   */
  group_->movePeers({syncPeer}, targetGroup);
  group_->recoverIfNoSyncPeers();

  // All 6 detached peers became SYNC.
  EXPECT_EQ(group_->getNumInSyncPeers(), 6);
  for (const auto& peer : promoted) {
    EXPECT_EQ(peer->getPeerState(), PeerUpdateState::JOINED_RUNNING);
    EXPECT_TRUE(group_->isPeerInSync(peer->getGroupBitPosition()));
    EXPECT_FALSE(group_->getDetachedPeers().contains(peer));
  }

  // Every promoted peer now resolves each prefix to the shared group entry.
  for (const auto& peer : promoted) {
    for (const auto& prefix : prefixes) {
      auto [entry, isPerPeerEntry] = group_->getRibEntrySharedOrPeer(
          prefix,
          peer->getPeerOwnerKey(),
          kPlaceholderPathID,
          group_->getLastSeenRibVersion());
      EXPECT_NE(entry, nullptr);
      EXPECT_FALSE(isPerPeerEntry); // shared with the group
    }
  }

  // The moved peer landed in the target group as a detached peer.
  EXPECT_EQ(syncPeer->getUpdateGroup(), targetGroup);
  EXPECT_EQ(syncPeer->getPeerState(), PeerUpdateState::DETACHED_INIT_DUMP);
  EXPECT_TRUE(targetGroup->getDetachedPeers().contains(syncPeer));
}

/*
 * Moving out the last SYNC peer with no promotable detached peer leaves the
 * group frozen: nothing is promoted and it stays sync-less.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    MoveLastSyncPeerWithNoPromotablePeerStaysFrozen) {
  auto targetGroup = std::make_shared<AdjRibOutGroup>(*evb_, "target_group");
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);

  setUpJoinedRunningPeer(adjRib0, 0);
  ASSERT_EQ(group_->getNumInSyncPeers(), 1);

  /*
   * adjRib1 is detached but still draining (not DETACHED_READY_TO_JOIN), so
   * nothing is promotable.
   */
  group_->markPeerDetached(adjRib1);
  adjRib1->setPeerState(PeerUpdateState::DETACHED_RUNNING);

  group_->movePeers({adjRib0}, targetGroup);
  group_->recoverIfNoSyncPeers();

  /*
   * No promotable peer -> group stays frozen with no SYNC peers, adjRib1
   * untouched.
   */
  EXPECT_EQ(group_->getNumInSyncPeers(), 0);
  EXPECT_EQ(adjRib1->getPeerState(), PeerUpdateState::DETACHED_RUNNING);
  EXPECT_TRUE(group_->getDetachedPeers().contains(adjRib1));

  // The moved peer landed in the target group as a detached peer.
  EXPECT_EQ(adjRib0->getUpdateGroup(), targetGroup);
  EXPECT_TRUE(targetGroup->getDetachedPeers().contains(adjRib0));
}

/*
 * Moving out the last SYNC peer when only a detached peer ahead of the group
 * (DEP-A) remains promotes it via promoteDetachedPeerToSync and adopts the
 * group RIB-OUT, while the moved peer lands in the target group as a detached
 * peer.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    MoveLastSyncPeerPromotesAheadDepAAndAdoptsRibOut) {
  auto targetGroup = std::make_shared<AdjRibOutGroup>(*evb_, "target_group");
  auto groupOwnerKey = group_->getGroupOwnerKey();

  /*
   * "Random" existing group RIB-OUT: kV4Prefix1 (also owned by the peer) and
   * kV4Prefix4 (group-only, the peer never had it).
   */
  auto groupAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  for (const auto& prefix : {kV4Prefix1, kV4Prefix4}) {
    group_
        ->addToLiteTree(
            group_->LiteTree_, prefix, groupOwnerKey, kPlaceholderPathID)
        ->setPostAttr(groupAttrs);
  }
  group_->setLastSeenRibVersion(50);

  auto syncPeer = createAndRegisterPeer(0);
  setUpJoinedRunningPeer(syncPeer, 0);

  /*
   * DEP-A: detached ahead of the group (detachedRibVersion 0), with its own
   * diverged RIB-OUT for 3 prefixes.
   */
  auto depA = createAndRegisterPeer(1);
  depA->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  depA->setLastSeenRibVersion(100); // ahead of the group (50)
  group_->markPeerDetached(depA);
  setUpReadyPeerConsumer(depA);
  /*
   * Group consumer not at end of CL -> peer is "ahead", not
   * collapse-rejoinable, forcing the promoteDetachedPeerToSync path.
   */
  publishChangeItem(folly::CIDRNetwork{folly::IPAddress("10.1.0.0"), 24});

  auto peerOwnerKey = depA->getPeerOwnerKey();
  auto peerAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(2, 0, 0, 0));
  std::vector<std::pair<folly::CIDRNetwork, AdjRibEntry*>> peerEntries;
  for (const auto& prefix : {kV4Prefix1, kV4Prefix2, kV4Prefix3}) {
    auto* e = group_->addToLiteTree(
        group_->LiteTree_, prefix, peerOwnerKey, kPlaceholderPathID);
    e->setPostAttr(peerAttrs);
    peerEntries.emplace_back(prefix, e);
  }

  /*
   * Move the last SYNC peer out, then run the caller-side recovery. movePeers
   * is a pure mover; the orchestrator runs recoverIfNoSyncPeers once all moves
   * are complete, which we mirror here to drive the no-sync-peers sweep.
   */
  group_->movePeers({syncPeer}, targetGroup);
  group_->recoverIfNoSyncPeers();

  // DEP-A promoted to SYNC; group adopted its RIB version.
  EXPECT_EQ(depA->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_TRUE(group_->isPeerInSync(1));
  EXPECT_FALSE(group_->getDetachedPeers().contains(depA));
  EXPECT_EQ(group_->getNumInSyncPeers(), 1);
  EXPECT_EQ(group_->getLastSeenRibVersion(), 100);

  /*
   * The group RIB-OUT is now exactly the peer's old RIB-OUT: each peer entry
   * was re-keyed under the group owner key (same object) and the peer key is
   * gone.
   */
  for (const auto& [prefix, entry] : peerEntries) {
    EXPECT_EQ(
        group_->getFromLiteTree(group_->LiteTree_, prefix, groupOwnerKey),
        entry);
    EXPECT_EQ(
        group_->getFromLiteTree(group_->LiteTree_, prefix, peerOwnerKey),
        nullptr);
  }
  // The group-only entry the peer never had was deleted.
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix4, groupOwnerKey),
      nullptr);

  // The moved peer landed in the target group as a detached peer.
  EXPECT_EQ(syncPeer->getUpdateGroup(), targetGroup);
  EXPECT_EQ(syncPeer->getPeerState(), PeerUpdateState::DETACHED_INIT_DUMP);
  EXPECT_TRUE(targetGroup->getDetachedPeers().contains(syncPeer));

  /*
   * promoteDetachedPeerToSync rebuilt the group consumer; point the fixture at
   * the live one so TearDown tears down the right consumer.
   */
  groupConsumer_ = group_->getChangeListConsumer();
}

/*
 * tryAcceptPeerToGroup() tests
 */

/*
 * DSP peer in DETACHED_READY_TO_JOIN with matching RIB-OUT entries.
 * tryAcceptPeerToGroup succeeds: sync bitmap set, removed from detachedPeer
 * set, detached RIB version reset, state transitions to JOINED_RUNNING.
 */
TEST_F(UpdateGroupDetachLifecycleTest, AcceptPeerSucceedsWithMatchingEntries) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib1, 1);

  // Peer 0 is detached DSP (no sync bit set)
  adjRib0->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib0);
  adjRib0->setDetachedRibVersion(42);
  setUpReadyPeerConsumer(adjRib0);

  // Add matching entries in group's LiteTree for both peer and group
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib0->getPeerOwnerKey();
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));

  for (const auto& prefix : {kV4Prefix1, kV4Prefix2, kV4Prefix3}) {
    group_
        ->addToLiteTree(
            group_->LiteTree_, prefix, groupOwnerKey, kPlaceholderPathID)
        ->setPostAttr(attrs);
    group_
        ->addToLiteTree(
            group_->LiteTree_, prefix, peerOwnerKey, kPlaceholderPathID)
        ->setPostAttr(attrs);
  }

  setGroupConsumerReady();
  group_->checkAndAcceptReadyToJoinPeers();

  // Peer should be JOINED_RUNNING
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_RUNNING);

  // Sync bitmap set, removed from detachedPeer set
  EXPECT_TRUE(group_->isPeerInSync(0));
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib0));

  // Detached RIB version reset
  EXPECT_EQ(adjRib0->getDetachedRibVersion(), 0);

  // Peer entries should be collapsed (erased)
  for (const auto& prefix : {kV4Prefix1, kV4Prefix2, kV4Prefix3}) {
    EXPECT_EQ(
        group_->getFromLiteTree(group_->LiteTree_, prefix, peerOwnerKey),
        nullptr);
    EXPECT_NE(
        group_->getFromLiteTree(group_->LiteTree_, prefix, groupOwnerKey),
        nullptr);
  }

  // No PL corrections needed (all entries matched)
  EXPECT_TRUE(adjRib0->attrToPrefixMap_.empty());

  // Peer 1 unaffected
  EXPECT_EQ(adjRib1->getPeerState(), PeerUpdateState::JOINED_RUNNING);
}

/*
 * DSP peer in DETACHED_READY_TO_JOIN with mismatching RIB-OUT entries.
 * tryAcceptPeerToGroup fails: peer set back to DETACHED_RUNNING,
 * no state changes applied. Packing timers are rescheduled so the peer
 * can continue processing pending changes and attempt to rejoin later.
 */
TEST_F(UpdateGroupDetachLifecycleTest, AcceptPeerFailsWithMismatchingEntries) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib1, 1);

  // Peer 0 is detached (no sync bit set)
  adjRib0->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib0);
  adjRib0->setDetachedRibVersion(42);

  // Set up CL consumer and activate it so the peer has a timer to reschedule
  auto peerConsumer = std::make_shared<AdjRibOutConsumer>(
      changeTracker_,
      adjRib0,
      "peer0_consumer",
      *evb_,
      addPathBitmap_,
      nonAddPathBitmap_);
  adjRib0->setChangeListConsumer(peerConsumer);
  adjRib0->activateChangeListConsumer();
  // Cancel timer so we can verify it gets rescheduled after failed rejoin
  adjRib0->cancelPackingTimers();
  EXPECT_FALSE(adjRib0->changeListConsumeTimer_->isScheduled());

  // Add mismatching entries
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib0->getPeerOwnerKey();
  auto groupAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  auto peerAttrs = std::make_shared<BgpPath>(*buildBgpPathFields(2, 2, 0, 0));

  group_
      ->addToLiteTree(
          group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID)
      ->setPostAttr(groupAttrs);
  group_
      ->addToLiteTree(
          group_->LiteTree_, kV4Prefix1, peerOwnerKey, kPlaceholderPathID)
      ->setPostAttr(peerAttrs);

  auto acceptedPeers = group_->tryAcceptPeersToGroup({adjRib0});
  EXPECT_TRUE(acceptedPeers.empty());

  // Peer should be set back to DETACHED_RUNNING
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::DETACHED_RUNNING);

  // Still in detachedPeer set, not in sync
  EXPECT_TRUE(group_->getDetachedPeers().contains(adjRib0));
  EXPECT_FALSE(group_->isPeerInSync(0));

  // Detached RIB version updated to group's lastSeenRibVersion
  EXPECT_EQ(adjRib0->getDetachedRibVersion(), group_->getLastSeenRibVersion());

  // Peer entry collapsed (erased), discrepancy corrected via packing list
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey),
      nullptr);

  /*
   * Packing list contains re-advertisement with group's attrs for the
   * discrepant prefix
   */
  EXPECT_TRUE(verifyInAdjRibPackingList(adjRib0, groupAttrs, kV4Prefix1));

  // Packing timers rescheduled so peer can continue processing
  EXPECT_TRUE(adjRib0->changeListConsumeTimer_->isScheduled());
}

/*
 * checkAndAcceptReadyToJoinPeers only processes DETACHED_READY_TO_JOIN peers.
 * Peers in other states are not touched.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    CheckAndAcceptOnlyProcessesReadyToJoinPeers) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  auto adjRib2 = createAndRegisterPeer(2);
  setUpJoinedRunningPeer(adjRib2, 2);

  // Peer 0: DETACHED_READY_TO_JOIN with matching entries
  adjRib0->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib0);
  setUpReadyPeerConsumer(adjRib0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey0 = adjRib0->getPeerOwnerKey();
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  group_
      ->addToLiteTree(
          group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID)
      ->setPostAttr(attrs);
  group_
      ->addToLiteTree(
          group_->LiteTree_, kV4Prefix1, peerOwnerKey0, kPlaceholderPathID)
      ->setPostAttr(attrs);

  // Peer 1: DETACHED_RUNNING (should not be processed)
  adjRib1->setPeerState(PeerUpdateState::DETACHED_RUNNING);

  /*
   * Peer 2: JOINED_RUNNING (should not be processed)
   * Already set by setUpJoinedRunningPeer
   */

  setGroupConsumerReady();
  group_->checkAndAcceptReadyToJoinPeers();

  // Peer 0 should be accepted
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_TRUE(group_->isPeerInSync(0));

  // Peer 1 should remain DETACHED_RUNNING
  EXPECT_EQ(adjRib1->getPeerState(), PeerUpdateState::DETACHED_RUNNING);

  // Peer 2 should remain JOINED_RUNNING
  EXPECT_EQ(adjRib2->getPeerState(), PeerUpdateState::JOINED_RUNNING);
}

/*
 * Multiple DSP peers in DETACHED_READY_TO_JOIN: one succeeds, one fails.
 * Verify independent handling.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    CheckAndAcceptMultiplePeersMixedResults) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  auto adjRib2 = createAndRegisterPeer(2);
  setUpJoinedRunningPeer(adjRib2, 2);

  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  auto differentAttrs =
      std::make_shared<BgpPath>(*buildBgpPathFields(2, 2, 0, 0));

  // Peer 0: DETACHED_READY_TO_JOIN with matching entries (will succeed)
  adjRib0->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib0);
  setUpReadyPeerConsumer(adjRib0);
  auto peerOwnerKey0 = adjRib0->getPeerOwnerKey();
  group_
      ->addToLiteTree(
          group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID)
      ->setPostAttr(attrs);
  group_
      ->addToLiteTree(
          group_->LiteTree_, kV4Prefix1, peerOwnerKey0, kPlaceholderPathID)
      ->setPostAttr(attrs);

  // Peer 1: DETACHED_READY_TO_JOIN with mismatching entries (will fail)
  adjRib1->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib1);
  setUpReadyPeerConsumer(adjRib1);
  auto peerOwnerKey1 = adjRib1->getPeerOwnerKey();
  group_
      ->addToLiteTree(
          group_->LiteTree_, kV4Prefix1, peerOwnerKey1, kPlaceholderPathID)
      ->setPostAttr(differentAttrs);

  setGroupConsumerReady();
  group_->checkAndAcceptReadyToJoinPeers();

  // Peer 0 should be accepted
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_TRUE(group_->isPeerInSync(0));
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib0));

  // Peer 1 should be set back to DETACHED_RUNNING
  EXPECT_EQ(adjRib1->getPeerState(), PeerUpdateState::DETACHED_RUNNING);
  EXPECT_FALSE(group_->isPeerInSync(1));

  // Peer 2 unaffected
  EXPECT_EQ(adjRib2->getPeerState(), PeerUpdateState::JOINED_RUNNING);
}

/*
 * DFP with group-only entries in the RIB-OUT (no peer entries).
 * All entries have ribVersion <= detachedRibVersion since the peer was
 * sharing them before detachment. Collapse should succeed with 0 entries
 * collapsed (nothing to erase).
 */
TEST_F(UpdateGroupDetachLifecycleTest, AcceptDFPWithSharedEntriesOnly) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib1, 1);

  adjRib0->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  adjRib0->setAdjRibFlag(AdjRib::IS_DETACHED_FAST_PEER);
  group_->markPeerDetached(adjRib0);
  adjRib0->setDetachedRibVersion(10);

  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));

  // All entries are group-only, announced before detachment
  for (const auto& [prefix, version] :
       std::vector<std::pair<folly::CIDRNetwork, uint64_t>>{
           {kV4Prefix1, 3}, {kV4Prefix2, 7}, {kV4Prefix3, 10}}) {
    auto* entry = group_->addToLiteTree(
        group_->LiteTree_, prefix, groupOwnerKey, kPlaceholderPathID);
    entry->setPostAttr(attrs);
    entry->setRibVersion(version);
  }

  group_->checkAndAcceptReadyToJoinPeers();

  // Peer promoted to JOINED_RUNNING (DFP: no collapse needed)
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_FALSE(adjRib0->isAdjRibFlagSet(AdjRib::IS_DETACHED_FAST_PEER));
  EXPECT_TRUE(group_->isPeerInSync(0));
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib0));
  EXPECT_EQ(adjRib0->getDetachedRibVersion(), 0);

  // All group entries still present (DFP doesn't touch them)
  for (const auto& prefix : {kV4Prefix1, kV4Prefix2, kV4Prefix3}) {
    EXPECT_NE(
        group_->getFromLiteTree(group_->LiteTree_, prefix, groupOwnerKey),
        nullptr);
  }
}

/* A DFP rejoin (accepted directly, no collapse) bumps the peer's rejoin count.
 */
TEST_F(UpdateGroupDetachLifecycleTest, RejoinCountIncrementsForDFP) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib1, 1);

  adjRib0->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  adjRib0->setAdjRibFlag(AdjRib::IS_DETACHED_FAST_PEER);
  group_->markPeerDetached(adjRib0);
  adjRib0->setDetachedRibVersion(10);
  ASSERT_EQ(adjRib0->getStats().getNumTimesRejoined(), 0);

  group_->checkAndAcceptReadyToJoinPeers();

  ASSERT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_EQ(adjRib0->getStats().getNumTimesRejoined(), 1);
}

/* A DSP rejoin (accepted via collapse) bumps the peer's rejoin count. */
TEST_F(UpdateGroupDetachLifecycleTest, RejoinCountIncrementsForDSP) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib1, 1);

  adjRib0->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib0);
  adjRib0->setDetachedRibVersion(10);
  setUpReadyPeerConsumer(adjRib0);

  // Shared, pre-detachment group entry so collapse finds no discrepancy.
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  auto* entry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  entry->setPostAttr(attrs);
  entry->setRibVersion(5);
  ASSERT_EQ(adjRib0->getStats().getNumTimesRejoined(), 0);

  setGroupConsumerReady();
  group_->checkAndAcceptReadyToJoinPeers();

  ASSERT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_EQ(adjRib0->getStats().getNumTimesRejoined(), 1);
}

/*
 * DSP with a mix of shared (pre-detachment) and lazily-cloned entries.
 * Shared entries: group-only, ribVersion <= detachedRibVersion.
 * Cloned entries: both peer and group, matching attrs.
 * Collapse should succeed — shared entries pass version check, cloned entries
 * are collapsed.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    AcceptDSPWithMixedSharedAndClonedEntries) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib1, 1);

  adjRib0->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib0);
  adjRib0->setDetachedRibVersion(10);
  setUpReadyPeerConsumer(adjRib0);

  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib0->getPeerOwnerKey();
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));

  // Prefix 1 & 2: announced before detachment (shared, group-only)
  auto* entry1 = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  entry1->setPostAttr(attrs);
  entry1->setRibVersion(5);

  auto* entry2 = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix2, groupOwnerKey, kPlaceholderPathID);
  entry2->setPostAttr(attrs);
  entry2->setRibVersion(10);

  // Prefix 3: announced after detachment, lazily cloned to peer
  auto* groupEntry3 = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix3, groupOwnerKey, kPlaceholderPathID);
  groupEntry3->setPostAttr(attrs);
  groupEntry3->setRibVersion(15);

  auto* peerEntry3 = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix3, peerOwnerKey, kPlaceholderPathID);
  peerEntry3->setPostAttr(attrs);
  peerEntry3->setRibVersion(15);

  setGroupConsumerReady();
  group_->checkAndAcceptReadyToJoinPeers();

  // Peer promoted to JOINED_RUNNING
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_TRUE(group_->isPeerInSync(0));
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib0));
  EXPECT_EQ(adjRib0->getDetachedRibVersion(), 0);

  // Shared entries (prefix 1 & 2) — group entry remains, no peer entry
  EXPECT_NE(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, groupOwnerKey),
      nullptr);
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix1, peerOwnerKey),
      nullptr);
  EXPECT_NE(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix2, groupOwnerKey),
      nullptr);
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix2, peerOwnerKey),
      nullptr);

  // Cloned entry (prefix 3) — peer entry collapsed, group entry remains
  EXPECT_NE(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix3, groupOwnerKey),
      nullptr);
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix3, peerOwnerKey),
      nullptr);

  // No PL corrections needed (shared entries pass version check, cloned match)
  EXPECT_TRUE(adjRib0->attrToPrefixMap_.empty());
}

/*
 * DSP with a post-detachment group entry that was NOT lazily cloned.
 * Entry has ribVersion > detachedRibVersion and no peer entry — discrepancy.
 * Collapse should fail, peer set back to DETACHED_RUNNING.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    AcceptDSPFailsWhenPostDetachEntryNotCloned) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib1, 1);

  adjRib0->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib0);
  adjRib0->setDetachedRibVersion(10);

  // Set up CL consumer so packing timers can be rescheduled on failure
  auto peerConsumer = std::make_shared<AdjRibOutConsumer>(
      changeTracker_,
      adjRib0,
      "peer0_consumer",
      *evb_,
      addPathBitmap_,
      nonAddPathBitmap_);
  adjRib0->setChangeListConsumer(peerConsumer);
  adjRib0->activateChangeListConsumer();
  adjRib0->cancelPackingTimers();

  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib0->getPeerOwnerKey();
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));

  // Prefix 1: announced before detachment — shared, OK
  auto* entry1 = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  entry1->setPostAttr(attrs);
  entry1->setRibVersion(5);

  // Prefix 2: announced after detachment, NOT cloned — discrepancy
  auto* entry2 = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix2, groupOwnerKey, kPlaceholderPathID);
  entry2->setPostAttr(attrs);
  entry2->setRibVersion(15);

  auto acceptedPeers = group_->tryAcceptPeersToGroup({adjRib0});
  EXPECT_TRUE(acceptedPeers.empty());

  // Peer set back to DETACHED_RUNNING
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::DETACHED_RUNNING);
  EXPECT_TRUE(group_->getDetachedPeers().contains(adjRib0));
  EXPECT_FALSE(group_->isPeerInSync(0));

  // Detached RIB version updated to group's lastSeenRibVersion
  EXPECT_EQ(adjRib0->getDetachedRibVersion(), group_->getLastSeenRibVersion());

  // Packing list contains announcement correction for the missing prefix
  EXPECT_TRUE(verifyInAdjRibPackingList(adjRib0, attrs, kV4Prefix2));

  // Packing timers rescheduled
  EXPECT_TRUE(adjRib0->changeListConsumeTimer_->isScheduled());
}

/*
 * A never-in-sync DSP (DETACHED_ON_REGISTRATION, detachedRibVersion 0, so not
 * yet counted in numPeersDetachedAfterJoin_) whose rejoin collapse hits a
 * discrepancy is given a non-zero detachedRibVersion. It must be counted at
 * that moment: otherwise the decrement on its eventual rejoin/teardown (guarded
 * on detachedRibVersion > 0) would underflow the counter.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    RejoinDiscrepancyCountsPreviouslyUncountedPeer) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib1, 1);
  group_->setLastSeenRibVersion(100);

  /*
   * Never-in-sync peer: DETACHED_ON_REGISTRATION with detachedRibVersion 0,
   * caught up (ready to rejoin).
   */
  adjRib0->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  adjRib0->setAdjRibFlag(AdjRib::DETACHED_ON_REGISTRATION);
  group_->markPeerDetached(adjRib0);
  setUpReadyPeerConsumer(adjRib0);
  ASSERT_EQ(adjRib0->getDetachedRibVersion(), 0);

  // A group-only entry the peer never saw -> collapse flags a discrepancy.
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  auto* entry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  entry->setPostAttr(attrs);
  entry->setRibVersion(15);

  ASSERT_EQ(group_->getNumPeersDetachedAfterJoin(), 0);

  auto acceptedPeers = group_->tryAcceptPeersToGroup({adjRib0});

  /*
   * Discrepancy: not accepted, kept DETACHED_RUNNING, version bumped to
   * group's.
   */
  EXPECT_TRUE(acceptedPeers.empty());
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::DETACHED_RUNNING);
  EXPECT_EQ(adjRib0->getDetachedRibVersion(), group_->getLastSeenRibVersion());

  // The previously-uncounted peer is now counted (increment on discrepancy).
  EXPECT_EQ(group_->getNumPeersDetachedAfterJoin(), 1);
}

/*
 * Unregistering a detached peer settles its own prefix counts: cleanUp
 * decrements both for the per-peer diverged entries it erases AND for the
 * shared (group-owned) entries the peer advertised, so the peer's
 * pre/postOutPrefixCount return to 0.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    UnregisterDetachedPeerDecrementsOwnPrefixCounts) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  // Keep the group non-frozen after adjRib0 is removed.
  setUpJoinedRunningPeer(adjRib1, 1);
  group_->setLastSeenRibVersion(100);

  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib0->getPeerOwnerKey();
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));

  /*
   * Shared, group-owned entry (ribVersion 5 <= detachedRibVersion 100) that the
   * peer advertised while detached.
   */
  auto* shared = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  shared->setPostAttr(attrs);
  shared->setRibVersion(5);

  // Diverged, per-peer entry the peer advertised.
  auto* diverged = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix2, peerOwnerKey, kPlaceholderPathID);
  diverged->setPostAttr(attrs);

  // Detach the peer and give it the counts for both advertised prefixes.
  group_->markPeerDetached(adjRib0);
  adjRib0->setPeerState(PeerUpdateState::DETACHED_RUNNING);
  adjRib0->setDetachedRibVersion(100);
  // Balances the decrement deactivateDetachedModeProcessing does on unregister.
  group_->incrementPeersDetachedAfterJoin();
  // Only postOut feeds the global totalSentPrefixCount; preOut does not.
  auto totalSentBefore = totalSentPrefixCount;
  adjRib0->incrementPostOutPrefixCount(true /* isIpv4 */);
  adjRib0->incrementPostOutPrefixCount(true /* isIpv4 */);
  adjRib0->incrementPreOutPrefixCount(true /* isIpv4 */);
  adjRib0->incrementPreOutPrefixCount(true /* isIpv4 */);
  ASSERT_EQ(adjRib0->getStats().getPostOutPrefixCount(), 2);
  ASSERT_EQ(adjRib0->getStats().getPreOutPrefixCount(), 2);
  ASSERT_EQ(totalSentPrefixCount, totalSentBefore + 2);

  group_->unregisterPeer(adjRib0);

  /*
   * Both the per-peer (erased) and the shared (left in the group)
   * advertisements are decremented, so the peer's own counts return to 0 and
   * its whole postOut contribution is removed from the global.
   */
  EXPECT_EQ(adjRib0->getStats().getPostOutPrefixCount(), 0);
  EXPECT_EQ(adjRib0->getStats().getPreOutPrefixCount(), 0);
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore);
}

/*
 * Add-path variant of the above, with both ownerships on the SAME prefix.
 * Divergence is per (prefix, pathId), so a detached peer can own a lazy clone
 * of one pathId while still sharing the group's other pathId at that prefix.
 * Cleanup must settle both -- decrementing only the paths the peer owns leaks
 * the shared ones out of the peer's counts and out of the global total.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    UnregisterDetachedAddPathPeerDecrementsMixedOwnershipPathCounts) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  // Keep the group non-frozen after adjRib0 is removed.
  setUpJoinedRunningPeer(adjRib1, 1);
  group_->setLastSeenRibVersion(200);
  adjRib0->sendAddPath_ = true;

  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib0->getPeerOwnerKey();
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));

  /*
   * One prefix, two paths. The group re-announced pathId 1 after the peer
   * detached at v100, which lazily cloned the pre-mutation entry under the
   * peer's own key and left the group's copy at v150. pathId 2 was untouched
   * (v5 <= 100), so the peer still shares it.
   */
  auto* groupPath1 = group_->addToPathTree(
      group_->PathTree_, kV4Prefix1, groupOwnerKey, /*pathId=*/1);
  groupPath1->setPostAttr(attrs);
  groupPath1->setRibVersion(150);
  auto* groupPath2 = group_->addToPathTree(
      group_->PathTree_, kV4Prefix1, groupOwnerKey, /*pathId=*/2);
  groupPath2->setPostAttr(attrs);
  groupPath2->setRibVersion(5);
  auto* peerPath1 = group_->addToPathTree(
      group_->PathTree_, kV4Prefix1, peerOwnerKey, /*pathId=*/1);
  peerPath1->setPostAttr(attrs);
  peerPath1->setRibVersion(100);

  group_->markPeerDetached(adjRib0);
  adjRib0->setPeerState(PeerUpdateState::DETACHED_RUNNING);
  adjRib0->setDetachedRibVersion(100);
  // Balances the decrement deactivateDetachedModeProcessing does on unregister.
  group_->incrementPeersDetachedAfterJoin();

  // The peer advertised both paths: its own pathId 1 and the shared pathId 2.
  auto totalSentBefore = totalSentPrefixCount;
  adjRib0->incrementPostOutPrefixCount(true /* isIpv4 */);
  adjRib0->incrementPostOutPrefixCount(true /* isIpv4 */);
  adjRib0->incrementPreOutPrefixCount(true /* isIpv4 */);
  adjRib0->incrementPreOutPrefixCount(true /* isIpv4 */);
  ASSERT_EQ(adjRib0->getStats().getPostOutPrefixCount(), 2);
  ASSERT_EQ(adjRib0->getStats().getPreOutPrefixCount(), 2);
  ASSERT_EQ(totalSentPrefixCount, totalSentBefore + 2);

  group_->unregisterPeer(adjRib0);

  EXPECT_EQ(adjRib0->getStats().getPostOutPrefixCount(), 0)
      << "the shared pathId at the same prefix as the peer's own clone was "
         "not decremented";
  EXPECT_EQ(adjRib0->getStats().getPreOutPrefixCount(), 0);
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore)
      << "the peer's share of the shared pathId leaked into the global total";

  // The peer's clone is erased; the group keeps both of its own paths.
  EXPECT_EQ(
      group_->getFromPathTree(group_->PathTree_, kV4Prefix1, peerOwnerKey, 1),
      nullptr);
  EXPECT_NE(
      group_->getFromPathTree(group_->PathTree_, kV4Prefix1, groupOwnerKey, 1),
      nullptr);
  EXPECT_NE(
      group_->getFromPathTree(group_->PathTree_, kV4Prefix1, groupOwnerKey, 2),
      nullptr);
}

/*
 * Peer detaches as slow peer. Consumer has pending CL items to process
 * → peer is DSP → starts timer-driven CL consumption loop.
 */
TEST_F(UpdateGroupDetachLifecycleTest, DSPDetachAndStartConsumption) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib0, 0);
  setUpJoinedRunningPeer(adjRib1, 1);

  /*
   * Publish a CL item but DON'T consume it — group consumer stays pended.
   * When detached consumer joins, it also starts pended (not ready = DSP).
   */
  publishChangeItem(folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24});

  // Step 1: Peer 0 blocks and detaches
  group_->markPeerBlocked(adjRib0);
  EXPECT_EQ(adjRib0->getPeerBlockInfo().blockCount, 1);
  group_->detachSlowPeer(adjRib0);
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::DETACHED_BLOCKED);

  // Step 2: Peer 0 unblocks — DSP path (consumer not ready)
  group_->markPeerUnblocked(adjRib0);

  // Block info is NOT reset on activation — it's reset on rejoin instead.
  EXPECT_EQ(adjRib0->getPeerBlockInfo().blockCount, 1);

  // Pump the evb so sendBgpUpdates runs
  evb_->loopOnce();

  // Peer should NOT transition to DETACHED_READY_TO_JOIN yet
  EXPECT_NE(adjRib0->getPeerState(), PeerUpdateState::DETACHED_READY_TO_JOIN);

  // Clean up
  adjRib0->deactivateDetachedModeProcessing();
}

/**
 * Two peers join group. Peer 0 blocks, gets detected as slow, detaches.
 * Group hasn't moved on CL -> peer is DFP -> immediate
 * DETACHED_READY_TO_JOIN with IS_DETACHED_FAST_PEER flag.
 */
TEST_F(UpdateGroupDetachLifecycleTest, DFPDetachAndReadyToJoin) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib0, 0);
  setUpJoinedRunningPeer(adjRib1, 1);

  /*
   * Publish a CL item and have the group consumer consume it.
   * After consumption, group consumer is "ready" (at end of CL).
   * DFP scenario: group hasn't moved further on CL after this point.
   */
  publishChangeItem(folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24});
  groupConsumer_->iterateChangesToEnd();
  EXPECT_TRUE(groupConsumer_->isReady());

  // Add a prefix to the group packing list (group is mid-send)
  addPrefixToGroupPL(folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24});

  // Step 1: Peer 0 becomes blocked
  group_->markPeerBlocked(adjRib0);
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_BLOCKED);
  EXPECT_TRUE(BitmapUtils::isBitSet(group_->getBlockedBitmap(), 0));

  // Step 2: Slow peer detection fires, peer detaches
  group_->detachSlowPeer(adjRib0);
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::DETACHED_BLOCKED);
  EXPECT_TRUE(group_->getDetachedPeers().contains(adjRib0));
  EXPECT_FALSE(group_->isPeerInSync(0));
  EXPECT_FALSE(BitmapUtils::isBitSet(group_->getBlockedBitmap(), 0));

  // Verify detached consumer was created
  EXPECT_NE(adjRib0->getDetachedConsumer(), nullptr);

  // DFP: consumer is ready (joined at group's ready position), PL empty
  adjRib0->clearPackingList();

  // Step 3: Peer 0 unblocks
  group_->markPeerUnblocked(adjRib0);

  // Pump the evb so sendBgpUpdates runs and DFP check triggers
  evb_->loopOnce();

  /*
   * DFP: isDFP() returns true (group PL non-empty, versions match)
   * -> transition to DETACHED_READY_TO_JOIN with IS_DETACHED_FAST_PEER flag
   */
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::DETACHED_READY_TO_JOIN);
  EXPECT_TRUE(adjRib0->isAdjRibFlagSet(AdjRib::IS_DETACHED_FAST_PEER));

  // Peer 1 should be completely unaffected
  EXPECT_EQ(adjRib1->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_TRUE(group_->isPeerInSync(1));
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib1));

  // Clean up
  adjRib0->deactivateDetachedModeProcessing();
}

/*
 * Peer detaches, reaches DETACHED_READY_TO_JOIN, then rejoins via
 * checkAndAcceptReadyToJoinPeers. Block info should be reset on rejoin,
 * not on detach activation.
 */
TEST_F(UpdateGroupDetachLifecycleTest, BlockCountResetOnRejoin) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib0, 0);
  setUpJoinedRunningPeer(adjRib1, 1);

  // Publish a CL item and consume it so group consumer is ready
  publishChangeItem(folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24});
  groupConsumer_->iterateChangesToEnd();
  EXPECT_TRUE(groupConsumer_->isReady());

  // Add a prefix to the group packing list (group is mid-send)
  addPrefixToGroupPL(folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24});

  // Step 1: Peer 0 blocks and detaches
  group_->markPeerBlocked(adjRib0);
  EXPECT_EQ(adjRib0->getPeerBlockInfo().blockCount, 1);
  group_->detachSlowPeer(adjRib0);
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::DETACHED_BLOCKED);

  // DFP: consumer is ready, PL empty
  adjRib0->clearPackingList();

  // Step 2: Peer 0 unblocks — DFP path
  group_->markPeerUnblocked(adjRib0);

  // Block count should NOT be reset on activation
  EXPECT_EQ(adjRib0->getPeerBlockInfo().blockCount, 1);

  // Pump the evb so sendBgpUpdates runs and DFP check triggers
  evb_->loopOnce();

  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::DETACHED_READY_TO_JOIN);
  EXPECT_TRUE(adjRib0->isAdjRibFlagSet(AdjRib::IS_DETACHED_FAST_PEER));

  // Block count still preserved before rejoin
  EXPECT_EQ(adjRib0->getPeerBlockInfo().blockCount, 1);

  // Step 3: Rejoin — checkAndAcceptReadyToJoinPeers resets block info
  group_->checkAndAcceptReadyToJoinPeers();

  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_TRUE(group_->isPeerInSync(0));

  // Block count should be reset on rejoin
  EXPECT_EQ(adjRib0->getPeerBlockInfo().blockCount, 0);
}

/*
 * DSP peer in DETACHED_READY_TO_JOIN when group consumer is NOT at end of CL.
 * Peer cannot rejoin — should transition back to DETACHED_RUNNING and
 * reschedule packing timers.
 */
TEST_F(UpdateGroupDetachLifecycleTest, DSPPeerRejoinsWhenGroupConsumerReady) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib1, 1);

  adjRib0->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib0);

  // Set up CL consumer and activate so the peer has timers
  auto peerConsumer = std::make_shared<AdjRibOutConsumer>(
      changeTracker_,
      adjRib0,
      "peer0_consumer",
      *evb_,
      addPathBitmap_,
      nonAddPathBitmap_);
  adjRib0->setChangeListConsumer(peerConsumer);
  adjRib0->activateChangeListConsumer();
  adjRib0->cancelPackingTimers();
  EXPECT_FALSE(adjRib0->changeListConsumeTimer_->isScheduled());

  // Set peer consumer to ready
  setUpReadyPeerConsumer(adjRib0);

  /*
   * Do NOT call setGroupConsumerReady() — group consumer marker is nullptr
   * (freshly registered), so isReady() returns true and the peer can rejoin.
   */
  group_->checkAndAcceptReadyToJoinPeers();

  /*
   * Peer rejoins — group never published any changes, so both consumers
   * are at nullptr (ready)
   */
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib0));
  EXPECT_TRUE(group_->isPeerInSync(0));
}

/*
 * DSP peer in DETACHED_READY_TO_JOIN when group consumer IS at end of CL
 * and peer's consumer has a null marker (no changes published). Peer can
 * rejoin since both consumers are ready.
 */
TEST_F(UpdateGroupDetachLifecycleTest, DSPPeerRejoinsWhenPeerConsumerReady) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib1, 1);

  adjRib0->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib0);

  // Set up CL consumer and activate so the peer has timers
  auto peerConsumer = std::make_shared<AdjRibOutConsumer>(
      changeTracker_,
      adjRib0,
      "peer0_consumer",
      *evb_,
      addPathBitmap_,
      nonAddPathBitmap_);
  adjRib0->setChangeListConsumer(peerConsumer);
  adjRib0->activateChangeListConsumer();
  adjRib0->cancelPackingTimers();
  EXPECT_FALSE(adjRib0->changeListConsumeTimer_->isScheduled());

  /*
   * Peer consumer marker is nullptr (no changes published), so isReady()
   * returns true.
   */
  EXPECT_TRUE(adjRib0->canWaitForGroupToRejoin());

  // Group consumer IS ready
  setGroupConsumerReady();

  group_->checkAndAcceptReadyToJoinPeers();

  // Peer rejoins — both consumers are at nullptr (ready)
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib0));
  EXPECT_TRUE(group_->isPeerInSync(0));
}

/**
 * Test: getRibEntryWithUpdateGroup returns per-peer entry when it exists
 */
TEST_F(
    UpdateGroupDetachedPeerTest,
    GetRibEntryWithUpdateGroupReturnsPeerEntry) {
  auto adjRib = createAndRegisterPeer(0);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  auto peerOwnerKey = adjRib->getPeerOwnerKey();

  // Add a per-peer entry directly to the group's tree
  auto added =
      group_->addToLiteTree(group_->LiteTree_, prefix, peerOwnerKey, 0);
  ASSERT_NE(added, nullptr);

  auto* entry = adjRib->getRibEntryWithUpdateGroup(prefix, 0);
  EXPECT_EQ(entry, added);
}

/**
 * Test: getRibEntryWithUpdateGroup clones shared group entry to per-peer entry
 */
TEST_F(
    UpdateGroupDetachedPeerTest,
    GetRibEntryWithUpdateGroupCopiesSharedEntry) {
  auto adjRib = createAndRegisterPeer(0);
  // Register a second peer so copyEntryForOwner uses peer owner key.
  auto adjRib1 = createAndRegisterPeer(1);
  adjRib->setDetachedRibVersion(10);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  auto groupOwnerKey = AdjRibOutOwnerKey::forGroup(1);

  // Add a group entry with ribVersion=5 (< detachedRibVersion=10)
  auto groupEntry =
      group_->addToLiteTree(group_->LiteTree_, prefix, groupOwnerKey, 0);
  ASSERT_NE(groupEntry, nullptr);
  groupEntry->setRibVersion(5);

  auto entry = adjRib->getRibEntryWithUpdateGroup(prefix, 0);

  // Should return a cloned per-peer entry, not the group entry
  EXPECT_NE(entry, nullptr);
  EXPECT_NE(entry, groupEntry);
  // Cloned entry should have the same ribVersion
  EXPECT_EQ(entry->getRibVersion(), groupEntry->getRibVersion());

  // Verify group entry is still in the tree untouched
  auto groupEntryAfter =
      group_->getFromLiteTree(group_->LiteTree_, prefix, groupOwnerKey);
  EXPECT_EQ(groupEntryAfter, groupEntry);
}

/**
 * Test: getRibEntryWithUpdateGroup returns nullptr when no entry exists
 */
TEST_F(
    UpdateGroupDetachedPeerTest,
    GetRibEntryWithUpdateGroupReturnsNullWhenNoEntry) {
  auto adjRib = createAndRegisterPeer(0);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};

  auto* entry = adjRib->getRibEntryWithUpdateGroup(prefix, 0);
  EXPECT_EQ(entry, nullptr);
}

/**
 * Test: getRibEntryWithUpdateGroup skips group entry when peer never saw it
 * (detachedRibVersion < groupEntry ribVersion)
 */
TEST_F(
    UpdateGroupDetachedPeerTest,
    GetRibEntryWithUpdateGroupSkipsUnseenGroupEntry) {
  auto adjRib = createAndRegisterPeer(0);
  adjRib->setDetachedRibVersion(5);

  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};
  auto groupOwnerKey = AdjRibOutOwnerKey::forGroup(1);

  // Add a group entry with ribVersion=10 (> detachedRibVersion=5)
  auto groupEntry =
      group_->addToLiteTree(group_->LiteTree_, prefix, groupOwnerKey, 0);
  ASSERT_NE(groupEntry, nullptr);
  groupEntry->setRibVersion(10);

  auto* entry = adjRib->getRibEntryWithUpdateGroup(prefix, 0);
  EXPECT_EQ(entry, nullptr);

  // Verify group entry is still in the tree untouched
  auto groupEntryAfter =
      group_->getFromLiteTree(group_->LiteTree_, prefix, groupOwnerKey);
  EXPECT_EQ(groupEntryAfter, groupEntry);
}

TEST_F(UpdateGroupDetachedPeerTest, DetachSlowPeerRetainsPendingEgressEoR) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);

  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;
  group_->setChangeListTracker(changeTracker, addPathBitmap, nonAddPathBitmap);
  group_->registerGroupConsumer();
  auto groupConsumer = group_->getChangeListConsumer();

  ShadowRibEntry entry;
  entry.prefix = folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24};
  auto trackable =
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(entry));
  changeTracker->publishChange(trackable.get());
  ASSERT_NE(groupConsumer->getMarker(), nullptr);

  adjRib0->setPeerState(PeerUpdateState::JOINED_BLOCKED);
  adjRib1->setPeerState(PeerUpdateState::JOINED_RUNNING);
  group_->markPeerInSync(adjRib0);
  group_->markPeerInSync(adjRib1);
  group_->markPeerBlocked(adjRib0);

  EXPECT_FALSE(adjRib0->egressEoRsPending());

  /*
   * In-sync peers are marked as owing EoR when the EoR becomes owed
   * (walkAndProcessShadowRib); simulate that here. The peer has not committed
   * its EoR (it is blocked), so it must RETAIN the pending EoR through
   * slow-peer detach -- detach must neither clear nor re-arm it.
   */
  adjRib0->setEgressEoRsPending(true /* v4 */, false /* v6 */);
  group_->detachSlowPeer(adjRib0);

  /* Peer retains its (uncommitted) pending EoR across detach. */
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::DETACHED_BLOCKED);
  EXPECT_TRUE(adjRib0->egressEoRsPending());

  /* Clean up consumers before group destruction */
  adjRib0->deactivateDetachedModeProcessing();
  groupConsumer->resetBitmap();
  groupConsumer->terminate();
  groupConsumer->deregisterFromTracker();
  group_->resetChangeListConsumer();
  groupConsumer.reset();
}

/**
 * Test: detachSlowPeer does not set egressEoRsPending when the peer does not
 * have it pending.
 */
TEST_F(
    UpdateGroupDetachedPeerTest,
    DetachDoesNotSetEoRsPendingWhenPeerHasNone) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);

  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;

  group_->setChangeListTracker(changeTracker, addPathBitmap, nonAddPathBitmap);
  group_->registerGroupConsumer();
  auto groupConsumer = group_->getChangeListConsumer();

  ShadowRibEntry entry;
  entry.prefix = folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24};
  auto trackable =
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(entry));
  changeTracker->publishChange(trackable.get());

  adjRib0->setPeerState(PeerUpdateState::JOINED_BLOCKED);
  adjRib1->setPeerState(PeerUpdateState::JOINED_RUNNING);
  group_->markPeerInSync(adjRib0);
  group_->markPeerInSync(adjRib1);
  group_->markPeerBlocked(adjRib0);

  EXPECT_FALSE(adjRib0->egressEoRsPending());

  group_->detachSlowPeer(adjRib0);

  /*
   * The peer was never marked as owing an EoR, so it has nothing pending after
   * detach.
   */
  EXPECT_FALSE(adjRib0->egressEoRsPending());

  /* Clean up consumers before group destruction */
  adjRib0->deactivateDetachedModeProcessing();
  groupConsumer->resetBitmap();
  groupConsumer->terminate();
  groupConsumer->deregisterFromTracker();
  group_->resetChangeListConsumer();
  groupConsumer.reset();
}

/**
 * Test: Initial RIB dump with sendWithEoR=true. When a slow peer gets detached
 * during distribution, it retains its pending egress EoR.
 *
 * Flow:
 *   1. walkAndProcessShadowRib(true) populates the packing list and marks every
 * in-sync peer's per-AFI flags via setEgressEorsPendingSyncPeers
 *   2. buildAndSendGroupBgpMessages distributes messages
 *   3. distributeMessageToInSyncPeers finds peer0's queue blocked
 *   4. markPeerBlocked triggers detachSlowPeer(adjRib)
 */
TEST_F(UpdateGroupDetachedPeerTest, RibWalkDetachCopiesEgressEoRsPending) {
  /* Build shadow RIB with one entry */
  ShadowRibEntriesMap shadowRibEntries;
  folly::CIDRNetwork prefix{folly::IPAddress("10.0.0.0"), 24};

  ShadowRibEntry srEntry;
  srEntry.prefix = prefix;
  srEntry.ribVersion = 1;
  auto attrs = std::make_shared<BgpPath>(BgpPathFields());
  attrs->setLocalPref(100);
  TinyPeerInfo peerInfo{
      folly::IPAddress("1.1.1.1"), 65000, 0, BgpSessionType::EBGP, false};
  srEntry.bestpath =
      std::make_shared<ShadowRibRouteInfo>(peerInfo, attrs, kDefaultPathID);

  shadowRibEntries.emplace(
      prefix,
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(srEntry)));

  /* Create group with the shadow RIB (IPv4 negotiated for the entry to pass) */
  UpdateGroupKey groupKey;
  groupKey.afiIpv4Negotiated = true;
  UpdateGroupConfig ugConfig;
  ugConfig.enableSlowPeerBlockCountDetection = true;
  ugConfig.allowSlowPeerDetach = true;
  group_ = std::make_shared<AdjRibOutGroup>(
      *evb_,
      "test_group",
      1,
      true /* enableUpdateGroup */,
      groupKey,
      ShadowRibView{
          shadowRibEntries, ShadowRibView::zeroRibVersion()} /* shadowRib */,
      nullptr /* policyManager */,
      ugConfig);

  /* Set slow peer threshold to 1 so first block triggers detach */
  {
    UpdateGroupConfig cfg;
    cfg.enableSlowPeerBlockCountDetection = true;
    cfg.allowSlowPeerDetach = true;
    cfg.slowPeerTimeThreshold = std::chrono::milliseconds(50000);
    cfg.slowPeerBlockCountThreshold = 1;
    cfg.slowPeerBlockCountWindow = std::chrono::milliseconds(300000);
    group_->setUpdateGroupConfigForTesting(cfg);
  }

  /* Register two peers */
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);

  /* Give peer0 a tiny bounded queue that's already blocked */
  auto blockedQueue = std::make_shared<AdjRib::AdjRibOutQueueT>(3, 2, 1);
  /* Fill past high watermark to make it blocked */
  blockedQueue->push(std::nullopt);
  blockedQueue->push(std::nullopt);
  ASSERT_TRUE(blockedQueue->isBlocked());
  adjRib0->adjRibOutQueue_ = blockedQueue;

  /* Give peer1 a normal queue */
  adjRib1->adjRibOutQueue_ =
      std::make_shared<AdjRib::AdjRibOutQueueT>(1000, 500, 100);

  /* Set up change tracker and consumer for detachSlowPeer */
  auto changeTracker =
      std::make_shared<ChangeTracker<ShadowRibEntry>>("test_tracker");
  ConsumerBitmap addPathBitmap;
  ConsumerBitmap nonAddPathBitmap;

  group_->setChangeListTracker(changeTracker, addPathBitmap, nonAddPathBitmap);
  group_->registerGroupConsumer();
  auto groupConsumer = group_->getChangeListConsumer();

  /* Publish an item so the consumer has a non-null marker */
  ShadowRibEntry clEntry;
  clEntry.prefix = folly::CIDRNetwork{folly::IPAddress("20.0.0.0"), 24};
  auto trackable =
      std::make_unique<TrackableObject<ShadowRibEntry>>(std::move(clEntry));
  changeTracker->publishChange(trackable.get());

  /* Set peer states and mark both in sync */
  adjRib0->setPeerState(PeerUpdateState::JOINED_RUNNING);
  adjRib1->setPeerState(PeerUpdateState::JOINED_RUNNING);
  group_->markPeerInSync(adjRib0);
  group_->markPeerInSync(adjRib1);

  /* Peer should not have egressEoRsPending before rib walk */
  EXPECT_FALSE(adjRib0->egressEoRsPending());

  /* Run the initial dump with sendWithEoR=true (walks shadow RIB and schedules
   * the build+send) */
  group_->processRibDumpForGroup(true /* sendWithEoR */);

  /*
   * Pump the event loop to let buildAndSendGroupBgpMessages run.
   * This will:
   *   1. Try to distribute messages
   *   2. peer0's queue is blocked -> markPeerBlocked -> detachSlowPeer
   *   3. peer0 was marked as owing EoR at intake (walkAndProcessShadowRib)
   * and never committed it (blocked), so it retains its pending EoR on detach
   */
  evb_->loopOnce();

  /* Peer0 should have been detached and retained egressEoRsPending. */
  EXPECT_TRUE(group_->getDetachedPeers().contains(adjRib0));
  EXPECT_TRUE(adjRib0->egressEoRsPending());
  EXPECT_FALSE(adjRib1->egressEoRsPending());

  /* Close blocked queue and pump evb so deferredPushToPeer can exit */
  blockedQueue->close();
  evb_->loopOnce();

  /* Clean up */
  adjRib0->deactivateDetachedModeProcessing();
  groupConsumer->resetBitmap();
  groupConsumer->terminate();
  groupConsumer->deregisterFromTracker();
  group_->resetChangeListConsumer();
  groupConsumer.reset();
}

/*
 * Regression test for a duplicate-EoR race on detach.
 *
 * A peer's EGRESS_EOR_PENDING_<afi> flag is cleared when that peer's EoR push
 * resolves. If the peer detaches between AFIs, detachPeer must preserve that
 * state: v4 stays clear while the still-uncommitted v6 remains pending.
 */
TEST_F(UpdateGroupDetachedPeerTest, DetachDoesNotResendCommittedEgressEoR) {
  auto adjRib0 = createAndRegisterPeer(0);
  adjRib0->setPeerState(PeerUpdateState::JOINED_RUNNING);
  group_->markPeerInSync(adjRib0);

  /*
   * Peer0 was marked to send both AFIs (distributePendingEoRs step A), then its
   * v4 EoR push resolved inline -> markEgressEoRSent cleared peer0's v4 flag.
   * v6 is still pending for peer0 (its v6 push has not resolved).
   */
  adjRib0->setEgressEoRsPending(true /* v4 */, true /* v6 */);
  adjRib0->markEgressEoRSent(nettools::bgplib::BgpUpdateAfi::AFI_IPv4);
  ASSERT_FALSE(adjRib0->isAdjRibFlagSet(AdjRib::EGRESS_EOR_PENDING_V4));
  ASSERT_TRUE(adjRib0->isAdjRibFlagSet(AdjRib::EGRESS_EOR_PENDING_V6));

  /* Peer0 detaches between its v4 and v6 EoR pushes. */
  group_->detachPeer(adjRib0, AdjRibOutGroup::DetachReason::Blocking);

  /*
   * v4 was already handed off to peer0 -> must NOT be re-armed (no duplicate
   * EoR). v6 was never committed -> must remain owed (no missed EoR).
   */
  EXPECT_FALSE(adjRib0->isAdjRibFlagSet(AdjRib::EGRESS_EOR_PENDING_V4));
  EXPECT_TRUE(adjRib0->isAdjRibFlagSet(AdjRib::EGRESS_EOR_PENDING_V6));
}

/*
 * Test: collapseLiteEntry skips detachedRibVersion check for init dump peers
 * (DETACHED_ON_REGISTRATION flag) and queues announcements for all
 * group-only entries.
 */
TEST_F(
    UpdateGroupDetachedPeerTest,
    CollapseLiteSkipsRibVersionCheckForInitDumpPeer) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();

  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));

  /* Add group-only entry with ribVersion=5, lower than detachedRibVersion */
  auto* groupEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  groupEntry->setPostAttr(attrs);
  groupEntry->setRibVersion(5);

  /* Set detachedRibVersion=10 — normally ribVersion(5) <= 10 would skip */
  adjRib->setDetachedRibVersion(10);
  adjRib->setAdjRibFlag(AdjRib::DETACHED_ON_REGISTRATION);
  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib);

  group_->collapseLiteEntries(groupOwnerKey, {adjRib});

  /* Despite ribVersion < detachedRibVersion, init dump peer
   * should still get the announcement queued */
  EXPECT_TRUE(adjRib->isAdjRibFlagSet(AdjRib::RIB_OUT_DISCREPANCY));
  EXPECT_FALSE(adjRib->attrToPrefixMap_.empty());
}

/**
 * Test: collapsePathEntry skips detachedRibVersion check for init dump peers
 * (DETACHED_ON_REGISTRATION flag) and queues announcements for all
 * group-only entries.
 */
TEST_F(
    UpdateGroupDetachedPeerTest,
    CollapsePathSkipsRibVersionCheckForInitDumpPeer) {
  /* Create group with sendAddPath=true for path tree */
  UpdateGroupKey groupKey;
  groupKey.sendAddPath = true;
  group_ = std::make_shared<AdjRibOutGroup>(
      *evb_, "test_group", 1, true /* enableUpdateGroup */, groupKey);

  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();

  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));

  /* Add group-only entry with ribVersion=5, lower than detachedRibVersion */
  auto* groupEntry = group_->addToPathTree(
      group_->PathTree_, kV4Prefix1, groupOwnerKey, 42 /* pathId */);
  groupEntry->setPostAttr(attrs);
  groupEntry->setRibVersion(5);

  /* Set detachedRibVersion=10 — normally ribVersion(5) <= 10 would skip */
  adjRib->setDetachedRibVersion(10);
  adjRib->setAdjRibFlag(AdjRib::DETACHED_ON_REGISTRATION);
  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib);

  group_->collapsePathEntries(groupOwnerKey, {adjRib});

  /* Despite ribVersion < detachedRibVersion, init dump peer
   * should still get the announcement queued */
  EXPECT_TRUE(adjRib->isAdjRibFlagSet(AdjRib::RIB_OUT_DISCREPANCY));
  EXPECT_FALSE(adjRib->attrToPrefixMap_.empty());
}

/**
 * Test: When update group is enabled, peer's canAnnounce delegates to
 * canAnnounceForGroup. An IBGP non-RR peer that would normally block
 * IBGP-learned routes allows them when the group has isRrClient=true.
 */
TEST_F(
    UpdateGroupDetachedPeerTest,
    CanAnnounceDelegatesToGroupWhenUpdateGroupEnabled) {
  /* Create group with isRrClient=true */
  UpdateGroupKey groupKey;
  groupKey.sessionType = BgpSessionType::IBGP;
  groupKey.isRrClient = true;
  group_ = std::make_shared<AdjRibOutGroup>(
      *evb_, "test_group", 1, true /* enableUpdateGroup */, groupKey);

  auto adjRib = createAndRegisterPeer(0);

  /* Build an IBGP-learned route from a non-RR client peer */
  TinyPeerInfo peerInfo{
      folly::IPAddress("2.2.2.2"),
      65000,
      0,
      BgpSessionType::IBGP,
      false /* isRrClient */};
  RibOutAnnouncementEntry entry{
      folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24},
      kDefaultPathID,
      peerInfo,
      std::make_shared<BgpPath>(BgpPathFields())};

  /*
   * Peer's own peeringParams_.isRrClient is false (default PeeringParams),
   * so without update group, this would be blocked. With update group,
   * it delegates to canAnnounceForGroup which uses groupKey.isRrClient=true.
   */
  EXPECT_TRUE(adjRib->canAnnounceEntry(entry));
}

/**
 * Test: When update group is enabled with isRrClient=false (non-RR IBGP group),
 * canAnnounce blocks IBGP-learned routes from non-RR peers, even if the
 * peer itself has isRrClient=true.
 */
TEST_F(UpdateGroupDetachedPeerTest, CanAnnounceUsesGroupRrClientSetting) {
  /* Create group with isRrClient=false */
  UpdateGroupKey groupKey;
  groupKey.sessionType = BgpSessionType::IBGP;
  groupKey.isRrClient = false;
  group_ = std::make_shared<AdjRibOutGroup>(
      *evb_, "test_group", 1, true /* enableUpdateGroup */, groupKey);

  auto adjRib = createAndRegisterPeer(0);
  /* Override the peer's RR client setting — should be ignored */
  adjRib->peeringParams_.isRrClient = true;

  /* Build an IBGP-learned route from a non-RR client peer */
  TinyPeerInfo peerInfo{
      folly::IPAddress("2.2.2.2"),
      65000,
      0,
      BgpSessionType::IBGP,
      false /* isRrClient */};
  RibOutAnnouncementEntry entry{
      folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24},
      kDefaultPathID,
      peerInfo,
      std::make_shared<BgpPath>(BgpPathFields())};

  /*
   * Group says isRrClient=false and route is from IBGP non-RR peer,
   * so canAnnounceForGroup returns false.
   */
  EXPECT_FALSE(adjRib->canAnnounceEntry(entry));
}

/*
 * Test: When update group is enabled, canAnnounceEntry skips the
 * sender_suppress_as_loop check. The group-level path does not perform
 * per-peer AS loop suppression because it uses per-peer remoteAs which
 * is incompatible with the shared group processing model.
 *
 * Verifies:
 *  - With enableUpdateGroup_=true: canAnnounceEntry returns true even when
 *    the AS path contains the remote AS (suppress_as_loop is skipped).
 *  - With enableUpdateGroup_=false: canAnnounceEntry returns false for the
 *    same entry (suppress_as_loop kicks in).
 */
TEST_F(
    UpdateGroupDetachedPeerTest,
    CanAnnounceEntrySkipsSuppressAsLoopWithUpdateGroup) {
  /* Create an eBGP group so canAnnounceForGroup returns true */
  UpdateGroupKey groupKey;
  groupKey.sessionType = BgpSessionType::EBGP;
  groupKey.isRrClient = false;
  group_ = std::make_shared<AdjRibOutGroup>(
      *evb_, "test_group", 1, true /* enableUpdateGroup */, groupKey);

  auto adjRib = createAndRegisterPeer(0);

  /* Configure as eBGP peer with a known remote AS.
   * localAs must differ from remoteAs so isIBgpPeer() returns false. */
  constexpr uint32_t remoteAs = 65001;
  adjRib->peeringParams_.remoteAs = remoteAs;
  adjRib->remoteAs_ = remoteAs;
  adjRib->peeringParams_.localAs = 65000;
  adjRib->peeringParams_.isConfedPeer = ConfedPeerConfigured{false};
  adjRib->sender_suppress_as_loop_ = true;

  /* Build a BgpPath whose AS path contains the remote AS (would trigger
   * suppress_as_loop in per-peer mode) */
  auto attrs = std::make_shared<BgpPath>(BgpPathFields());
  nettools::bgplib::BgpAttrAsPathC asPath;
  nettools::bgplib::BgpAttrAsPathSegmentC seg;
  seg.asSequence.push_back(remoteAs);
  seg.asSequence.push_back(65000);
  asPath.push_back(seg);
  attrs->setAsPath(std::move(asPath));

  /* Build announcement entry from a different peer */
  TinyPeerInfo peerInfo{
      folly::IPAddress("3.3.3.3"),
      65002,
      0,
      BgpSessionType::EBGP,
      false /* isRrClient */};
  RibOutAnnouncementEntry entry{
      folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24},
      kDefaultPathID,
      peerInfo,
      attrs};

  /* With update group enabled: should return true (suppress_as_loop skipped) */
  EXPECT_TRUE(adjRib->canAnnounceEntry(entry));

  /* Disable update group: suppress_as_loop should now block the entry */
  adjRib->enableUpdateGroup_ = false;
  EXPECT_FALSE(adjRib->canAnnounceEntry(entry));
}

/*
 * Test: detachSlowPeer copies egress prefix counts from group to peer.
 * After detach, the peer processes new announcements independently (its
 * preOut/postOut counts increase). When the peer rejoins the group via
 * collapse, discrepancies are resolved and the peer's counts match the
 * group's counts.
 *
 * Flow:
 *   1. Two peers JOINED_RUNNING, group announces 3 prefixes
 *   2. Peer 0 detaches — gets group's prefix counts copied
 *   3. Peer 0 independently processes a new prefix (counts diverge)
 *   4. Group withdraws that new prefix while peer 0 is blocked
 *   5. Peer 0 rejoins — collapse resolves the discrepancy
 *   6. After rejoin, peer 0's counts match the group's counts
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    DetachCopiesEgressStatsAndCollapseReconciles) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib0, 0);
  setUpJoinedRunningPeer(adjRib1, 1);

  auto peerOwnerKey = adjRib0->getPeerOwnerKey();
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  TinyPeerInfo peerInfo{
      folly::IPAddress("1.1.1.1"), 65000, 0, BgpSessionType::EBGP, false};

  auto totalSentBefore = totalSentPrefixCount;

  /* Step 1: Group announces 3 v4 prefixes via processRibAnnouncedEntryForGroup
   */
  for (const auto& prefix : {kV4Prefix1, kV4Prefix2, kV4Prefix3}) {
    RibOutAnnouncementEntry entry{prefix, kDefaultPathID, peerInfo, attrs};
    group_->processRibAnnouncedEntryForGroup(entry);
  }

  EXPECT_EQ(group_->getStats().getPostOutPrefixCount(), 3);
  EXPECT_EQ(group_->getStats().getPreOutPrefixCount(), 3);
  EXPECT_EQ(adjRib0->getStats().getPostOutPrefixCount(), 0);
  EXPECT_EQ(adjRib0->getStats().getPreOutPrefixCount(), 0);
  /* 2 in-sync peers * 3 prefixes = 6 */
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 6);

  /* Step 2: Detach peer 0 — stats should be copied from group.
   * totalSentPrefixCount should not change. */
  auto totalSentAfterStep1 = totalSentPrefixCount;
  adjRib0->setPeerState(PeerUpdateState::JOINED_BLOCKED);
  group_->markPeerBlocked(adjRib0);
  group_->detachSlowPeer(adjRib0);

  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::DETACHED_BLOCKED);
  EXPECT_EQ(adjRib0->getStats().getPostOutPrefixCount(), 3);
  EXPECT_EQ(adjRib0->getStats().getPreOutPrefixCount(), 3);
  EXPECT_EQ(adjRib0->getStats().getPostOutPrefixCountIpv4(), 3);
  EXPECT_EQ(adjRib0->getStats().getPreOutPrefixCountIpv4(), 3);
  EXPECT_EQ(totalSentPrefixCount, totalSentAfterStep1);

  /* Step 3: Peer 0 independently processes a new prefix (kV4Prefix4).
   * This simulates the detached peer consuming from the change list
   * and announcing a prefix the group also has. */
  auto peerEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix4, peerOwnerKey, kPlaceholderPathID);
  peerEntry->setPostAttr(attrs);
  peerEntry->setRibVersion(100);
  adjRib0->incrementPostOutPrefixCount(true /* isIpv4 */);
  adjRib0->incrementPreOutPrefixCount(true /* isIpv4 */);
  adjRib0->setDetachedRibVersion(50);

  EXPECT_EQ(adjRib0->getStats().getPostOutPrefixCount(), 4);
  EXPECT_EQ(adjRib0->getStats().getPreOutPrefixCount(), 4);

  /* Step 4: Group also processes the same prefix. Group and peer now both
   * have an entry for kV4Prefix4 — no discrepancy for this prefix.
   * Copy the group's postAttr to the peer entry so hasMatchingPostPolicyAttrs
   * matches (it compares shared_ptr identity, not deep content). */
  RibOutAnnouncementEntry entry4(
      kV4Prefix4,
      kDefaultPathID,
      peerInfo,
      attrs,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      false,
      std::chrono::system_clock::now(),
      100 /* ribVersion */);
  group_->processRibAnnouncedEntryForGroup(entry4);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto* groupEntry4 =
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix4, groupOwnerKey);
  ASSERT_NE(groupEntry4, nullptr);
  peerEntry->setPostAttr(groupEntry4->getPostAttr());

  EXPECT_EQ(group_->getStats().getPostOutPrefixCount(), 4);
  EXPECT_EQ(group_->getStats().getPreOutPrefixCount(), 4);
  /* 6 from step 1 + 1 (peer 0 detached increment) + 1 (group with 1 sync peer)
   */
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 8);

  /* Step 5: Peer 0 rejoins. Clear PL to simulate that sendBgpUpdates was run
   * and packing list was drained. Collapse should find matching entries and
   * accept the peer. */
  adjRib0->clearPackingList();
  group_->clearPackingList();
  adjRib0->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  setUpReadyPeerConsumer(adjRib0);
  setGroupConsumerReady();
  group_->checkAndAcceptReadyToJoinPeers();

  /*
   * After rejoin the peer folds back into the group's shared accounting, so
   * markPeerInSync clears its snapshot egress counts. The group keeps the
   * counts, and the global totalSentPrefixCount is untouched by the rejoin --
   * nothing is advertised or withdrawn.
   */
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_EQ(adjRib0->getStats().getPostOutPrefixCount(), 0);
  EXPECT_EQ(adjRib0->getStats().getPreOutPrefixCount(), 0);
  EXPECT_EQ(group_->getStats().getPostOutPrefixCount(), 4);
  EXPECT_EQ(group_->getStats().getPreOutPrefixCount(), 4);
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 8);
}

/*
 * Test (Rule 4): when an in-sync peer goes down, the group's local counts are
 * left untouched, but the peer's share of the global totalSentPrefixCount --
 * equal to the group's postOutPrefixCount -- is removed.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    SyncPeerDownSubtractsGroupShareFromGlobal) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib0, 0);
  setUpJoinedRunningPeer(adjRib1, 1);

  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  TinyPeerInfo peerInfo{
      folly::IPAddress("1.1.1.1"), 65000, 0, BgpSessionType::EBGP, false};

  auto totalSentBefore = totalSentPrefixCount;

  /* Group announces 3 v4 prefixes to its 2 in-sync peers. */
  for (const auto& prefix : {kV4Prefix1, kV4Prefix2, kV4Prefix3}) {
    RibOutAnnouncementEntry entry{prefix, kDefaultPathID, peerInfo, attrs};
    group_->processRibAnnouncedEntryForGroup(entry);
  }
  EXPECT_EQ(group_->getStats().getPostOutPrefixCount(), 3);
  /* 2 in-sync peers * 3 prefixes. */
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 6);

  /* An in-sync peer goes down. */
  group_->unregisterPeer(adjRib0);

  /* Group local counts unchanged; global drops by the group's share (3). */
  EXPECT_EQ(group_->getStats().getPostOutPrefixCount(), 3);
  EXPECT_EQ(group_->getNumInSyncPeers(), 1);
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 3);
}

/*
 * Test (Rule 5): when a detached peer goes down, its OWN postOutPrefixCount
 * (which may differ from the group's after independent processing) is removed
 * from the global totalSentPrefixCount, and the group's local counts are left
 * untouched.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    DetachedPeerDownSubtractsOwnShareFromGlobal) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib0, 0);
  setUpJoinedRunningPeer(adjRib1, 1);
  group_->setLastSeenRibVersion(42);

  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  TinyPeerInfo peerInfo{
      folly::IPAddress("1.1.1.1"), 65000, 0, BgpSessionType::EBGP, false};

  auto totalSentBefore = totalSentPrefixCount;

  /* Group announces 3 v4 prefixes to its 2 in-sync peers. */
  for (const auto& prefix : {kV4Prefix1, kV4Prefix2, kV4Prefix3}) {
    RibOutAnnouncementEntry entry{prefix, kDefaultPathID, peerInfo, attrs};
    group_->processRibAnnouncedEntryForGroup(entry);
  }
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 6);

  /* Detach peer 0: it snapshots the group's egress counts (3); global steady.
   */
  adjRib0->setPeerState(PeerUpdateState::JOINED_BLOCKED);
  group_->markPeerBlocked(adjRib0);
  group_->detachSlowPeer(adjRib0);
  ASSERT_TRUE(adjRib0->isDetachedPeer());
  EXPECT_EQ(adjRib0->getStats().getPostOutPrefixCount(), 3);
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 6);

  /* Peer 0 independently advertises one more prefix (kV4Prefix4) as a per-peer
   * diverged entry: own count and global +1. */
  auto peerOwnerKey = adjRib0->getPeerOwnerKey();
  auto* peerEntry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix4, peerOwnerKey, kPlaceholderPathID);
  peerEntry->setPostAttr(attrs);
  peerEntry->setRibVersion(100);
  adjRib0->incrementPostOutPrefixCount(true /* isIpv4 */);
  adjRib0->incrementPreOutPrefixCount(true /* isIpv4 */);
  EXPECT_EQ(adjRib0->getStats().getPostOutPrefixCount(), 4);
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 7);

  /* Detached peer goes down: cleanUpPeerRibOut settles its own
   * contribution -- the per-peer kV4Prefix4 entry it erases plus the 3 shared
   * group entries it advertised -- so the global drops by all 4. */
  group_->unregisterPeer(adjRib0);
  EXPECT_EQ(group_->getStats().getPostOutPrefixCount(), 3);
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 3);
}

/*
 * Test: After detach, group withdraws a prefix that the peer still has.
 * On rejoin, collapse detects the discrepancy (peer-only entry) and
 * decrements the peer's prefix counts to match the group.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    CollapseDecrementsStatsOnWithdrawalDiscrepancy) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib0, 0);
  setUpJoinedRunningPeer(adjRib1, 1);

  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib0->getPeerOwnerKey();
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  TinyPeerInfo peerInfo{
      folly::IPAddress("1.1.1.1"), 65000, 0, BgpSessionType::EBGP, false};

  auto totalSentBefore = totalSentPrefixCount;

  /* Group announces 3 prefixes (2 in-sync peers) */
  for (const auto& prefix : {kV4Prefix1, kV4Prefix2, kV4Prefix3}) {
    RibOutAnnouncementEntry entry{prefix, kDefaultPathID, peerInfo, attrs};
    group_->processRibAnnouncedEntryForGroup(entry);
  }
  /* 2 peers * 3 prefixes = 6 */
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 6);

  /* Detach peer 0 — gets group's counts (3 each) */
  adjRib0->setPeerState(PeerUpdateState::JOINED_BLOCKED);
  group_->markPeerBlocked(adjRib0);
  group_->detachSlowPeer(adjRib0);
  adjRib0->setDetachedRibVersion(50);

  EXPECT_EQ(adjRib0->getStats().getPostOutPrefixCount(), 3);
  EXPECT_EQ(adjRib0->getStats().getPreOutPrefixCount(), 3);
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 6);

  /* Peer clones group entries for the 3 prefixes it was sharing */
  for (const auto& prefix : {kV4Prefix1, kV4Prefix2, kV4Prefix3}) {
    auto peerEntry = group_->addToLiteTree(
        group_->LiteTree_, prefix, peerOwnerKey, kPlaceholderPathID);
    peerEntry->setPostAttr(attrs);
    peerEntry->setRibVersion(10);
  }

  /* Group withdraws kV4Prefix3 while peer 0 is detached.
   * Remove the group's owner entry from the tree node, leaving peer-only. */
  auto itr3 =
      group_->getRadixNodeItrFromLiteTree(group_->LiteTree_, kV4Prefix3);
  ASSERT_NE(itr3, group_->LiteTree_.end());
  itr3->value().erase(groupOwnerKey);
  /* 1 in-sync peer remaining after detach */
  group_->stats_.decrementPostOutPrefixCount(true /* isIpv4 */, 1);
  group_->stats_.decrementPreOutPrefixCount(true /* isIpv4 */);
  /* 6 - 1 (group withdrawal with 1 sync peer) = 5 */
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 5);

  EXPECT_EQ(group_->getStats().getPostOutPrefixCount(), 2);
  EXPECT_EQ(group_->getStats().getPreOutPrefixCount(), 2);

  /* Peer 0 still thinks it has 3 prefixes */
  EXPECT_EQ(adjRib0->getStats().getPostOutPrefixCount(), 3);
  EXPECT_EQ(adjRib0->getStats().getPreOutPrefixCount(), 3);

  /* Peer 0 rejoins — collapse should detect peer-only entry for kV4Prefix3
   * and decrement peer's counts. Clear PL first so canWaitForGroupToRejoin()
   * passes. */
  adjRib0->clearPackingList();
  group_->clearPackingList();
  adjRib0->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  setUpReadyPeerConsumer(adjRib0);
  setGroupConsumerReady();
  group_->checkAndAcceptReadyToJoinPeers();

  /* Peer has discrepancy so it goes back to DETACHED_RUNNING, but
   * the stats should have been adjusted by the collapse logic */
  EXPECT_EQ(adjRib0->getStats().getPostOutPrefixCount(), 2);
  EXPECT_EQ(adjRib0->getStats().getPreOutPrefixCount(), 2);
  EXPECT_EQ(
      adjRib0->getStats().getPostOutPrefixCount(),
      group_->getStats().getPostOutPrefixCount());
  EXPECT_EQ(
      adjRib0->getStats().getPreOutPrefixCount(),
      group_->getStats().getPreOutPrefixCount());
  /* 5 - 1 (collapse withdrawal for 1 peer) = 4 */
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 4);
}

/*
 * Test: After detach, group announces a new prefix that the peer doesn't have.
 * On rejoin, collapse detects the discrepancy (group-only entry) and
 * increments the peer's prefix counts.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    CollapseIncrementsStatsOnAnnouncementDiscrepancy) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib0, 0);
  setUpJoinedRunningPeer(adjRib1, 1);

  auto peerOwnerKey = adjRib0->getPeerOwnerKey();
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  TinyPeerInfo peerInfo{
      folly::IPAddress("1.1.1.1"), 65000, 0, BgpSessionType::EBGP, false};

  auto totalSentBefore = totalSentPrefixCount;

  /* Group announces 2 prefixes (2 in-sync peers) */
  for (const auto& prefix : {kV4Prefix1, kV4Prefix2}) {
    RibOutAnnouncementEntry entry{prefix, kDefaultPathID, peerInfo, attrs};
    group_->processRibAnnouncedEntryForGroup(entry);
  }
  /* 2 peers * 2 prefixes = 4 */
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 4);

  /* Detach peer 0 — gets group's counts (2 each) */
  adjRib0->setPeerState(PeerUpdateState::JOINED_BLOCKED);
  group_->markPeerBlocked(adjRib0);
  group_->detachSlowPeer(adjRib0);
  adjRib0->setDetachedRibVersion(50);

  EXPECT_EQ(adjRib0->getStats().getPostOutPrefixCount(), 2);
  EXPECT_EQ(adjRib0->getStats().getPreOutPrefixCount(), 2);
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 4);

  /* Peer clones group entries for the 2 prefixes it was sharing */
  for (const auto& prefix : {kV4Prefix1, kV4Prefix2}) {
    auto peerEntry = group_->addToLiteTree(
        group_->LiteTree_, prefix, peerOwnerKey, kPlaceholderPathID);
    peerEntry->setPostAttr(attrs);
    peerEntry->setRibVersion(10);
  }

  /* Group announces a new prefix (kV4Prefix3) while peer 0 is detached */
  RibOutAnnouncementEntry entry3(
      kV4Prefix3,
      kDefaultPathID,
      peerInfo,
      attrs,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      false,
      std::chrono::system_clock::now(),
      100 /* ribVersion */);
  group_->processRibAnnouncedEntryForGroup(entry3);

  EXPECT_EQ(group_->getStats().getPostOutPrefixCount(), 3);
  EXPECT_EQ(group_->getStats().getPreOutPrefixCount(), 3);
  /* 4 + 1 (group announcement with 1 sync peer) = 5 */
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 5);

  /* Peer 0 still thinks it has 2 prefixes */
  EXPECT_EQ(adjRib0->getStats().getPostOutPrefixCount(), 2);
  EXPECT_EQ(adjRib0->getStats().getPreOutPrefixCount(), 2);

  /* Peer 0 rejoins — collapse should detect group-only entry for kV4Prefix3
   * and increment peer's counts. Clear PL to simulate that sendBgpUpdates was
   * run and packing list was drained. */
  adjRib0->clearPackingList();
  group_->clearPackingList();
  adjRib0->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  setUpReadyPeerConsumer(adjRib0);
  setGroupConsumerReady();
  group_->checkAndAcceptReadyToJoinPeers();

  /* Peer has discrepancy so it goes back to DETACHED_RUNNING, but
   * the stats should have been adjusted by the collapse logic */
  EXPECT_EQ(adjRib0->getStats().getPostOutPrefixCount(), 3);
  EXPECT_EQ(adjRib0->getStats().getPreOutPrefixCount(), 3);
  EXPECT_EQ(
      adjRib0->getStats().getPostOutPrefixCount(),
      group_->getStats().getPostOutPrefixCount());
  EXPECT_EQ(
      adjRib0->getStats().getPreOutPrefixCount(),
      group_->getStats().getPreOutPrefixCount());
  /* 5 + 1 (collapse announcement for 1 peer) = 6 */
  EXPECT_EQ(totalSentPrefixCount, totalSentBefore + 6);
}

/*
 * Verify detachPeer sets all expected state:
 * 1. Egress prefix counts copied from group
 * 2. Packing list cloned
 * 3. Peer marked as detached (in detachedPeers_, sync bit cleared)
 * 4. Version fields inherited from group
 * 5. Blocked bitmap cleared
 * 6. Slow peer duration timer cancelled
 * 7. Detached consumer registered
 * 8. EoR state retained if peer still owes EoR (not yet committed)
 */
TEST_F(UpdateGroupDetachLifecycleTest, DetachPeerSetsAllExpectedState) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib0, 0);
  setUpJoinedRunningPeer(adjRib1, 1);

  group_->setLastSeenRibVersion(42);

  // Set up some group prefix counts
  group_->stats_.incrementPostOutPrefixCount(true);
  group_->stats_.incrementPostOutPrefixCount(true);
  group_->stats_.incrementPreOutPrefixCount(true);
  group_->stats_.incrementPreOutPrefixCount(true);

  // Mark peer as blocked in the group bitmap
  BitmapUtils::setBit(group_->adjRibBlockedBitmap_, 0);

  /* Mark the in-sync peer as owing both EoRs, mirroring intake marking in
   * walkAndProcessShadowRib. Detach must preserve this per-peer state.
   */
  adjRib0->setEgressEoRsPending(true /* v4 */, true /* v6 */);

  // Add prefix to group PL so clonePackingList has something to clone
  auto attrs = std::make_shared<BgpPath>(*buildBgpPathFields(1, 1, 0, 0));
  addPrefixToGroupPL(folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24});

  // Preconditions
  ASSERT_TRUE(group_->isPeerInSync(0));
  ASSERT_FALSE(group_->getDetachedPeers().contains(adjRib0));
  ASSERT_TRUE(BitmapUtils::isBitSet(group_->adjRibBlockedBitmap_, 0));
  ASSERT_EQ(adjRib0->getStats().getPostOutPrefixCount(), 0);

  group_->detachPeer(adjRib0, AdjRibOutGroup::DetachReason::Blocking);

  // 1. Egress prefix counts copied from group
  EXPECT_EQ(
      adjRib0->getStats().getPostOutPrefixCount(),
      group_->getStats().getPostOutPrefixCount());
  EXPECT_EQ(
      adjRib0->getStats().getPreOutPrefixCount(),
      group_->getStats().getPreOutPrefixCount());

  // 2. Packing list cloned (peer has non-empty PL)
  EXPECT_FALSE(adjRib0->attrToPrefixMap_.empty());

  // 3. Peer marked as detached
  EXPECT_TRUE(group_->getDetachedPeers().contains(adjRib0));
  EXPECT_FALSE(group_->isPeerInSync(0));

  // 4. Version fields inherited
  EXPECT_EQ(adjRib0->getLastSeenRibVersion(), 42);
  EXPECT_EQ(adjRib0->getDetachedRibVersion(), 42);

  // 5. Blocked bitmap cleared
  EXPECT_FALSE(BitmapUtils::isBitSet(group_->adjRibBlockedBitmap_, 0));

  // 6. Slow peer duration timer cancelled
  EXPECT_FALSE(
      adjRib0->slowPeerDurationTimer_ &&
      adjRib0->slowPeerDurationTimer_->isScheduled());

  // 7. Detached consumer registered
  EXPECT_NE(adjRib0->getDetachedConsumer(), nullptr);

  // 8. EoR state retained (peer owed EoR and had not committed it)
  EXPECT_TRUE(adjRib0->egressEoRsPending());

  // Peer 1 unaffected
  EXPECT_TRUE(group_->isPeerInSync(1));
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib1));
}

/*
 * A detached peer with lastSeenRibVersion > group's transitions to
 * DETACHED_READY_TO_JOIN after PL drain via transitionPeerUpdateState.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    PeerAheadOfGroupTransitionsToReadyToJoin) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib0, 0);
  setUpJoinedRunningPeer(adjRib1, 1);

  adjRib0->setPeerState(PeerUpdateState::DETACHED_INIT_DUMP);
  adjRib0->setLastSeenRibVersion(100);
  group_->setLastSeenRibVersion(50);
  group_->markPeerDetached(adjRib0);

  EXPECT_TRUE(adjRib0->attrToPrefixMap_.empty());

  adjRib0->transitionPeerUpdateState();

  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::DETACHED_READY_TO_JOIN);
  EXPECT_FALSE(adjRib0->isAdjRibFlagSet(AdjRib::IS_DETACHED_FAST_PEER));
  EXPECT_EQ(adjRib1->getPeerState(), PeerUpdateState::JOINED_RUNNING);
}

TEST_F(
    UpdateGroupDetachLifecycleTest,
    JoinedPeerDoesNotEnterDetachedRejoinPath) {
  auto adjRib = createAndRegisterPeer(0);
  setUpJoinedRunningPeer(adjRib, 0);
  group_->setLastSeenRibVersion(42);
  adjRib->setLastSeenRibVersion(42);
  adjRib->setDetachedRibVersion(42);
  addPrefixToGroupPL(folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24});

  adjRib->transitionPeerUpdateState();

  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_TRUE(group_->isPeerInSync(0));
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib));
}

TEST_F(UpdateGroupDetachLifecycleTest, PendingRibDumpDefersDetachedRejoin) {
  auto adjRib = createAndRegisterPeer(0);
  auto sibling = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib, 0);
  setUpJoinedRunningPeer(sibling, 1);
  group_->setLastSeenRibVersion(42);
  adjRib->setLastSeenRibVersion(42);
  group_->detachPeer(adjRib, AdjRibOutGroup::DetachReason::RouteRefresh);
  addPrefixToGroupPL(folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24});
  ASSERT_EQ(adjRib->getPeerState(), PeerUpdateState::DETACHED_RUNNING);
  ASSERT_TRUE(adjRib->isDFP());
  adjRib->getCancellationTokenForNewRibDump();

  adjRib->transitionPeerUpdateState();

  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::DETACHED_RUNNING);
  EXPECT_FALSE(adjRib->isAdjRibFlagSet(AdjRib::IS_DETACHED_FAST_PEER));
  EXPECT_FALSE(group_->isPeerInSync(0));
  EXPECT_TRUE(group_->getDetachedPeers().contains(adjRib));
  adjRib->cancelRibDump();
}

/*
 * An unregistered peer must not transition at all. PeerManager::
 * sessionTerminated can run before AdjRib::sessionTerminated, so an
 * already-scheduled sendBgpUpdates() can still reach
 * transitionPeerUpdateState() after unregisterPeer has set the peer DOWN and
 * cleared its group. Acting on that peer would operate on a terminated
 * session.
 *
 * Staged exactly like PeerAheadOfGroupTransitionsToReadyToJoin -- the branch
 * this peer would otherwise take, sending it to DETACHED_READY_TO_JOIN -- with
 * unregisterPeer() standing in for the PeerManager half of the teardown.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    DownPeerDoesNotTransitionOnPeerUpdateState) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib0, 0);
  setUpJoinedRunningPeer(adjRib1, 1);

  adjRib0->setPeerState(PeerUpdateState::DETACHED_INIT_DUMP);
  adjRib0->setLastSeenRibVersion(100);
  group_->setLastSeenRibVersion(50);
  group_->markPeerDetached(adjRib0);

  /*
   * PeerManager::sessionTerminated -> AdjRibGroup::unregisterPeer: sets the
   * peer DOWN and clears its group.
   */
  group_->unregisterPeer(adjRib0);
  ASSERT_EQ(adjRib0->getPeerState(), PeerUpdateState::DOWN);
  ASSERT_EQ(adjRib0->getUpdateGroup(), nullptr);

  /* The late tail of an already-scheduled sendBgpUpdates(). */
  adjRib0->transitionPeerUpdateState();

  /* Nothing happened: no state transition, no DFP promotion, no rejoin. */
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::DOWN);
  EXPECT_EQ(adjRib0->getUpdateGroup(), nullptr);
  EXPECT_FALSE(adjRib0->isAdjRibFlagSet(AdjRib::IS_DETACHED_FAST_PEER));
  EXPECT_FALSE(group_->isPeerInSync(0));

  EXPECT_FALSE(
      adjRib0->changeListConsumeTimer_ &&
      adjRib0->changeListConsumeTimer_->isScheduled());

  /* Peer 1 unaffected. */
  EXPECT_EQ(adjRib1->getPeerState(), PeerUpdateState::JOINED_RUNNING);
}

/*
 * checkAndAcceptReadyToJoinPeers does not reschedule packing timers for a
 * DETACHED_READY_TO_JOIN peer whose lastSeenRibVersion > group's. The peer
 * stays in DETACHED_READY_TO_JOIN.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    PeerAheadOfGroupDoesNotProceedOnChangelist) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib1, 1);

  adjRib0->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  adjRib0->setLastSeenRibVersion(100);
  group_->setLastSeenRibVersion(50);
  group_->markPeerDetached(adjRib0);
  setUpReadyPeerConsumer(adjRib0);

  // Group consumer is NOT at end of CL
  publishChangeItem(folly::CIDRNetwork{folly::IPAddress("10.0.0.0"), 24});

  group_->checkAndAcceptReadyToJoinPeers();

  // Peer should stay in DETACHED_READY_TO_JOIN, not set to DETACHED_RUNNING
  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::DETACHED_READY_TO_JOIN);
}

/*
 * A DEP-A finishing its drain (transitionPeerUpdateState) while the group is
 * frozen with no SYNC peers and no peer sharing its entries self-promotes to
 * SYNC via promoteDetachedPeerToSync.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    AheadDepASelfPromotesWhenGroupHasNoSharingPeers) {
  auto depA = createAndRegisterPeer(0);

  /*
   * DEP-A ahead of the group (detachedRibVersion 0), still draining. The group
   * has no SYNC peers and nothing sharing its entries, so it is frozen waiting
   * for a detached peer to promote itself.
   */
  depA->setPeerState(PeerUpdateState::DETACHED_RUNNING);
  depA->setLastSeenRibVersion(100);
  group_->setLastSeenRibVersion(50);
  group_->markPeerDetached(depA);
  setUpReadyPeerConsumer(depA);
  /*
   * Group consumer not at end of CL -> canWaitForGroupToRejoin() is false, so
   * the peer takes the "ahead of group" branch.
   */
  publishChangeItem(folly::CIDRNetwork{folly::IPAddress("10.1.0.0"), 24});
  ASSERT_EQ(group_->getNumInSyncPeers(), 0);
  ASSERT_EQ(group_->getNumPeersDetachedAfterJoin(), 0);

  depA->transitionPeerUpdateState();

  // The DEP-A self-promoted to SYNC and the group adopted its RIB version.
  EXPECT_EQ(depA->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_TRUE(group_->isPeerInSync(0));
  EXPECT_FALSE(group_->getDetachedPeers().contains(depA));
  EXPECT_EQ(group_->getNumInSyncPeers(), 1);
  EXPECT_EQ(group_->getLastSeenRibVersion(), 100);

  // The group's consume timer was restarted now that it has a SYNC peer again.
  EXPECT_TRUE(isGroupConsumeTimerScheduled());

  /*
   * promoteDetachedPeerToSync rebuilt the group consumer; repoint the fixture's
   * handle so TearDown tears down the live one.
   */
  groupConsumer_ = group_->getChangeListConsumer();
}

/*
 * DSP defer path: when a peer reaches the DSP rejoin point via
 * transitionPeerUpdateState while the group's packing list still has
 * undistributed entries, maybeAcceptDSPPeer defers acceptance. The peer
 * stays DETACHED_READY_TO_JOIN (not in sync) until the group drains its packing
 * list, at which point checkAndAcceptReadyToJoinPeers accepts it.
 */
TEST_F(
    UpdateGroupDetachLifecycleTest,
    DSPRejoinDeferredUntilGroupPackingListDrains) {
  auto adjRib0 = createAndRegisterPeer(0);
  auto adjRib1 = createAndRegisterPeer(1);
  setUpJoinedRunningPeer(adjRib1, 1);

  /*
   * Peer 0 has caught up to the group's CL marker (ready to rejoin). Use
   * DETACHED_INIT_DUMP so transitionPeerUpdateState deterministically
   * takes the DSP rejoin path (the DFP fast-path is skipped for init-dump
   * peers) and calls maybeAcceptDSPPeer.
   */
  adjRib0->setPeerState(PeerUpdateState::DETACHED_INIT_DUMP);
  group_->markPeerDetached(adjRib0);
  setUpReadyPeerConsumer(adjRib0); // peer CL marker == group marker (nullptr)
  setGroupConsumerReady();
  ASSERT_TRUE(adjRib0->attrToPrefixMap_.empty());
  ASSERT_TRUE(adjRib0->canWaitForGroupToRejoin());

  // The group still owes an undistributed entry in its packing list.
  addPrefixToGroupPL(kV4Prefix1);
  ASSERT_FALSE(group_->getAttrToPrefixMap().empty());

  // DSP rejoin attempt: acceptance is deferred because the group PL is dirty.
  adjRib0->transitionPeerUpdateState();

  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::DETACHED_READY_TO_JOIN);
  EXPECT_FALSE(group_->isPeerInSync(0));
  EXPECT_TRUE(group_->getDetachedPeers().contains(adjRib0));

  /*
   * Once the group's packing list drains, checkAndAcceptReadyToJoinPeers picks
   * up the deferred peer and accepts it.
   */
  group_->attrToPrefixMap_.clear();
  ASSERT_TRUE(group_->getAttrToPrefixMap().empty());

  group_->checkAndAcceptReadyToJoinPeers();

  EXPECT_EQ(adjRib0->getPeerState(), PeerUpdateState::JOINED_RUNNING);
  EXPECT_TRUE(group_->isPeerInSync(0));
  EXPECT_FALSE(group_->getDetachedPeers().contains(adjRib0));
}

} // namespace facebook::bgp
