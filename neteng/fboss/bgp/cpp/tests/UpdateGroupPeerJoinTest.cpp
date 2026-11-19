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
  friend class UpdateGroupPeerJoinTest;                                        \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapseLiteMatchingEntry);             \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapseLiteMismatchLogsError);         \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapseLiteNoPeerEntry);               \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapseLiteNoGroupEntry);              \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapseLiteMultiplePrefixes);          \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapsePathMatchingEntry);             \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapsePathMismatchLogsError);         \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapsePathMissingGroupPathId);        \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapsePathNoGroupEntry);              \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapsePathNoPeerEntry);               \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapsePathPathIdCountMismatch);       \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapseLiteMixedSharedAndCloned);      \
  FRIEND_TEST(                                                                 \
      UpdateGroupPeerJoinTest, CollapseLiteFailsWhenPostDetachEntryNotCloned); \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapsePathMixedSharedAndCloned);      \
  FRIEND_TEST(                                                                 \
      UpdateGroupPeerJoinTest, CollapsePathFailsWhenPostDetachEntryNotCloned); \
  FRIEND_TEST(UpdateGroupPeerJoinTest, DiscrepancyUpdatesDetachedRibVersion);  \
  FRIEND_TEST(UpdateGroupPeerJoinTest, DiscrepancyClearsDetachedOnRegistration);

#define AdjRibOutGroup_TEST_FRIENDS                                            \
  friend class UpdateGroupPeerJoinTest;                                        \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapseLiteMatchingEntry);             \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapseLiteMismatchLogsError);         \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapseLiteNoPeerEntry);               \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapseLiteNoGroupEntry);              \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapseLiteMultiplePrefixes);          \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapsePathMatchingEntry);             \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapsePathMismatchLogsError);         \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapsePathMissingGroupPathId);        \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapsePathNoGroupEntry);              \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapsePathNoPeerEntry);               \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapsePathPathIdCountMismatch);       \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapseLiteMixedSharedAndCloned);      \
  FRIEND_TEST(                                                                 \
      UpdateGroupPeerJoinTest, CollapseLiteFailsWhenPostDetachEntryNotCloned); \
  FRIEND_TEST(UpdateGroupPeerJoinTest, CollapsePathMixedSharedAndCloned);      \
  FRIEND_TEST(                                                                 \
      UpdateGroupPeerJoinTest, CollapsePathFailsWhenPostDetachEntryNotCloned); \
  FRIEND_TEST(UpdateGroupPeerJoinTest, DiscrepancyUpdatesDetachedRibVersion);  \
  FRIEND_TEST(UpdateGroupPeerJoinTest, DiscrepancyClearsDetachedOnRegistration);

#include <folly/io/async/EventBase.h>

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibGroup.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibStructs.h"
#include "neteng/fboss/bgp/cpp/common/BgpPath.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"

namespace facebook::bgp {
using ::testing::HasSubstr;

class UpdateGroupPeerJoinTest : public ::testing::Test {
 protected:
  void SetUp() override {
    folly::SingletonVault::singleton()->registrationComplete();
    evb_ = std::make_unique<folly::EventBase>();
    group_ = std::make_shared<AdjRibOutGroup>(
        *evb_, "test_group", 1, true /* enableUpdateGroup */, UpdateGroupKey{});
  }

  void TearDown() override {
    for (auto& peer : peers_) {
      group_->unregisterPeer(peer);
    }
    group_.reset();
    peers_.clear();
    evb_.reset();
  }

  std::shared_ptr<AdjRib> createAndRegisterPeer(uint64_t bit) {
    auto peerId = nettools::bgplib::BgpPeerId(
        folly::IPAddress("10.0.0.1"),
        folly::IPAddressV4("255.0.0.1").toLongHBO());
    auto adjRib = std::make_shared<AdjRib>(
        peerId,
        PeeringParams(fboss::kDefaultVrf),
        *evb_,
        ribInQ_,
        observerQ_,
        std::make_shared<folly::coro::Baton>(),
        nullptr /* policyManager */,
        std::make_shared<std::atomic<bool>>(false));
    group_->registerPeer(adjRib);
    /*
     * Bits are handed out lowest-free-first, so peers registered in order
     * land on the bit the caller expects.
     */
    EXPECT_EQ(bit, adjRib->getGroupBitPosition());
    peers_.push_back(adjRib);
    return adjRib;
  }

  std::shared_ptr<const BgpPath> makeAttrs(
      uint32_t asCount,
      uint32_t communityCount) {
    return std::make_shared<BgpPath>(
        *buildBgpPathFields(asCount, communityCount, 0, 0));
  }

  const std::vector<folly::CIDRNetwork> kPrefixes_{
      kV4Prefix1,
      kV4Prefix2,
      kV4Prefix3,
      kV4Prefix4,
      kV4Prefix5,
      kV4Prefix6,
      kV4Prefix7,
      kV6Prefix1,
      kV6Prefix2,
      kV6Prefix3,
  };

  size_t countPlPrefixes(const std::shared_ptr<AdjRib>& peer) const {
    size_t count = 0;
    for (const auto& [key, prefixes] : peer->attrToPrefixMap_) {
      count += prefixes.size();
    }
    return count;
  }

  bool plContains(
      const std::shared_ptr<AdjRib>& peer,
      const folly::CIDRNetwork& prefix,
      uint32_t pathId,
      const std::shared_ptr<const BgpPath>& expectedAttrs) const {
    for (const auto& [key, prefixes] : peer->attrToPrefixMap_) {
      if (BgpPathCompareWithNull{}(key.attrs, expectedAttrs) &&
          prefixes.contains({prefix, pathId})) {
        return true;
      }
    }
    return false;
  }

  std::unique_ptr<folly::EventBase> evb_;
  std::shared_ptr<AdjRibOutGroup> group_;
  std::vector<std::shared_ptr<AdjRib>> peers_;
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ_{
      nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<AdjRib::ObservableMessageT> observerQ_;
};

/*
 * NonAddPath: peer entry matches group entry — should be collapsed.
 */
TEST_F(UpdateGroupPeerJoinTest, CollapseLiteMatchingEntry) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();
  auto attrs = makeAttrs(1, 1);

  for (const auto& prefix : kPrefixes_) {
    group_
        ->addToLiteTree(
            group_->LiteTree_, prefix, groupOwnerKey, kPlaceholderPathID)
        ->setPostAttr(attrs);
    group_
        ->addToLiteTree(
            group_->LiteTree_, prefix, peerOwnerKey, kPlaceholderPathID)
        ->setPostAttr(attrs);
  }

  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib);

  group_->collapseLiteEntries(groupOwnerKey, {adjRib});
  EXPECT_FALSE(adjRib->isAdjRibFlagSet(AdjRib::RIB_OUT_DISCREPANCY));
  EXPECT_TRUE(adjRib->attrToPrefixMap_.empty());

  for (const auto& prefix : kPrefixes_) {
    EXPECT_EQ(
        group_->getFromLiteTree(group_->LiteTree_, prefix, peerOwnerKey),
        nullptr);
    EXPECT_NE(
        group_->getFromLiteTree(group_->LiteTree_, prefix, groupOwnerKey),
        nullptr);
  }
}

/*
 * NonAddPath: peer entry has different attrs from group — discrepancy,
 * peer entry erased, group's version inserted into peer's PL.
 */
TEST_F(UpdateGroupPeerJoinTest, CollapseLiteMismatchLogsError) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();
  auto groupAttrs = makeAttrs(1, 1);
  auto peerAttrs = makeAttrs(2, 2);

  for (const auto& prefix : kPrefixes_) {
    group_
        ->addToLiteTree(
            group_->LiteTree_, prefix, groupOwnerKey, kPlaceholderPathID)
        ->setPostAttr(groupAttrs);
    group_
        ->addToLiteTree(
            group_->LiteTree_, prefix, peerOwnerKey, kPlaceholderPathID)
        ->setPostAttr(peerAttrs);
  }

  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib);

  auto& messages = subscribeToLogMessages("", folly::LogLevel::ERR);
  messages.clear();

  group_->collapseLiteEntries(groupOwnerKey, {adjRib});
  EXPECT_TRUE(adjRib->isAdjRibFlagSet(AdjRib::RIB_OUT_DISCREPANCY));
  adjRib->clearAdjRibFlag(AdjRib::RIB_OUT_DISCREPANCY);

  // ERR logged for re-advertise (rate-limited)
  EXPECT_GE(messages.size(), 1);
  EXPECT_THAT(
      messages[0].first.getMessage(),
      HasSubstr("peer/group attrs mismatch, queuing re-advertisement"));

  // Peer entries erased, group entries remain
  for (const auto& prefix : kPrefixes_) {
    EXPECT_EQ(
        group_->getFromLiteTree(group_->LiteTree_, prefix, peerOwnerKey),
        nullptr);
    EXPECT_NE(
        group_->getFromLiteTree(group_->LiteTree_, prefix, groupOwnerKey),
        nullptr);
  }

  // PL has announcements (re-advertise) with group's attrs for all prefixes
  EXPECT_EQ(countPlPrefixes(adjRib), kPrefixes_.size());
  for (const auto& prefix : kPrefixes_) {
    EXPECT_TRUE(plContains(adjRib, prefix, kPlaceholderPathID, groupAttrs));
  }
}

/*
 * NonAddPath: no peer entry at prefix but group has one, and
 * ribVersion > detachedRibVersion — announcement inserted into PL.
 */
TEST_F(UpdateGroupPeerJoinTest, CollapseLiteNoPeerEntry) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto attrs = makeAttrs(1, 1);
  adjRib->setDetachedRibVersion(5);

  for (const auto& prefix : kPrefixes_) {
    auto* entry = group_->addToLiteTree(
        group_->LiteTree_, prefix, groupOwnerKey, kPlaceholderPathID);
    entry->setPostAttr(attrs);
    entry->setRibVersion(10);
  }

  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib);

  auto& messages = subscribeToLogMessages("", folly::LogLevel::ERR);
  messages.clear();

  group_->collapseLiteEntries(groupOwnerKey, {adjRib});
  EXPECT_TRUE(adjRib->isAdjRibFlagSet(AdjRib::RIB_OUT_DISCREPANCY));
  adjRib->clearAdjRibFlag(AdjRib::RIB_OUT_DISCREPANCY);

  // ERR logged for announcement (rate-limited)
  EXPECT_GE(messages.size(), 1);
  EXPECT_THAT(messages[0].first.getMessage(), HasSubstr("group-only entry"));

  // PL has announcements with group's attrs for all prefixes
  EXPECT_EQ(countPlPrefixes(adjRib), kPrefixes_.size());
  for (const auto& prefix : kPrefixes_) {
    EXPECT_TRUE(plContains(adjRib, prefix, kPlaceholderPathID, attrs));
  }
}

/*
 * NonAddPath: multiple prefixes, one has mismatch.
 * All peer entries erased. Peer has discrepancy so RIB_OUT_DISCREPANCY is
 * set.
 */
TEST_F(UpdateGroupPeerJoinTest, CollapseLiteMultiplePrefixes) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();
  auto attrs = makeAttrs(1, 1);

  // All prefixes: matching entries
  for (const auto& prefix : kPrefixes_) {
    group_
        ->addToLiteTree(
            group_->LiteTree_, prefix, groupOwnerKey, kPlaceholderPathID)
        ->setPostAttr(attrs);
    group_
        ->addToLiteTree(
            group_->LiteTree_, prefix, peerOwnerKey, kPlaceholderPathID)
        ->setPostAttr(attrs);
  }

  // Override last prefix with mismatching attrs
  auto* lastPeerEntry = group_->getFromLiteTree(
      group_->LiteTree_, kPrefixes_.back(), peerOwnerKey);
  lastPeerEntry->setPostAttr(makeAttrs(2, 2));

  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib);

  auto& messages = subscribeToLogMessages("", folly::LogLevel::ERR);
  messages.clear();

  group_->collapseLiteEntries(groupOwnerKey, {adjRib});
  EXPECT_TRUE(adjRib->isAdjRibFlagSet(AdjRib::RIB_OUT_DISCREPANCY));
  adjRib->clearAdjRibFlag(AdjRib::RIB_OUT_DISCREPANCY);

  // ERR logged for the one re-advertise + summary
  EXPECT_EQ(messages.size(), 2);
  EXPECT_THAT(
      messages[0].first.getMessage(),
      HasSubstr("peer/group attrs mismatch, queuing re-advertisement"));
  EXPECT_THAT(
      messages[1].first.getMessage(), HasSubstr("collapse completed for peer"));

  // All peer entries erased (matching collapsed, mismatched corrected)
  for (const auto& prefix : kPrefixes_) {
    EXPECT_EQ(
        group_->getFromLiteTree(group_->LiteTree_, prefix, peerOwnerKey),
        nullptr);
  }

  // PL has 1 announcement with group's attrs for the mismatched prefix
  EXPECT_EQ(countPlPrefixes(adjRib), 1);
  EXPECT_TRUE(plContains(adjRib, kPrefixes_.back(), kPlaceholderPathID, attrs));
}

/*
 * AddPath: peer entry matches group entry at same pathId — should collapse.
 */
TEST_F(UpdateGroupPeerJoinTest, CollapsePathMatchingEntry) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();
  auto attrs = makeAttrs(1, 1);
  uint32_t pathId = 42;

  for (const auto& prefix : kPrefixes_) {
    group_->addToPathTree(group_->PathTree_, prefix, groupOwnerKey, pathId)
        ->setPostAttr(attrs);
    group_->addToPathTree(group_->PathTree_, prefix, peerOwnerKey, pathId)
        ->setPostAttr(attrs);
  }

  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib);

  group_->collapsePathEntries(groupOwnerKey, {adjRib});
  EXPECT_FALSE(adjRib->isAdjRibFlagSet(AdjRib::RIB_OUT_DISCREPANCY));
  EXPECT_TRUE(adjRib->attrToPrefixMap_.empty());

  for (const auto& prefix : kPrefixes_) {
    EXPECT_EQ(
        group_->getFromPathTree(
            group_->PathTree_, prefix, peerOwnerKey, pathId),
        nullptr);
    EXPECT_NE(
        group_->getFromPathTree(
            group_->PathTree_, prefix, groupOwnerKey, pathId),
        nullptr);
  }
}

/*
 * AddPath: peer entry has different attrs — discrepancy,
 * peer entry erased, group's version inserted into peer's PL.
 */
TEST_F(UpdateGroupPeerJoinTest, CollapsePathMismatchLogsError) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();
  auto groupAttrs = makeAttrs(1, 1);
  auto peerAttrs = makeAttrs(2, 2);
  uint32_t pathId = 42;

  for (const auto& prefix : kPrefixes_) {
    group_->addToPathTree(group_->PathTree_, prefix, groupOwnerKey, pathId)
        ->setPostAttr(groupAttrs);
    group_->addToPathTree(group_->PathTree_, prefix, peerOwnerKey, pathId)
        ->setPostAttr(peerAttrs);
  }

  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib);

  auto& messages = subscribeToLogMessages("", folly::LogLevel::ERR);
  messages.clear();

  group_->collapsePathEntries(groupOwnerKey, {adjRib});
  EXPECT_TRUE(adjRib->isAdjRibFlagSet(AdjRib::RIB_OUT_DISCREPANCY));
  adjRib->clearAdjRibFlag(AdjRib::RIB_OUT_DISCREPANCY);

  // ERR logged for re-advertise (rate-limited)
  EXPECT_GE(messages.size(), 1);
  EXPECT_THAT(
      messages[0].first.getMessage(), HasSubstr("collapse path re-advertise"));

  for (const auto& prefix : kPrefixes_) {
    EXPECT_EQ(
        group_->getFromPathTree(
            group_->PathTree_, prefix, peerOwnerKey, pathId),
        nullptr);
  }

  // PL has announcements (re-advertise) with group's attrs for all prefixes
  EXPECT_EQ(countPlPrefixes(adjRib), kPrefixes_.size());
  for (const auto& prefix : kPrefixes_) {
    EXPECT_TRUE(plContains(adjRib, prefix, pathId, groupAttrs));
  }
}

/*
 * AddPath: peer has pathId not present in group — withdrawal for peer's
 * pathId, announcement for group's pathId. Discrepancy.
 */
TEST_F(UpdateGroupPeerJoinTest, CollapsePathMissingGroupPathId) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();
  auto attrs = makeAttrs(1, 1);

  for (const auto& prefix : kPrefixes_) {
    group_->addToPathTree(group_->PathTree_, prefix, groupOwnerKey, 42)
        ->setPostAttr(attrs);
    group_->addToPathTree(group_->PathTree_, prefix, peerOwnerKey, 99)
        ->setPostAttr(attrs);
  }

  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib);

  auto& messages = subscribeToLogMessages("", folly::LogLevel::ERR);
  messages.clear();

  group_->collapsePathEntries(groupOwnerKey, {adjRib});
  EXPECT_TRUE(adjRib->isAdjRibFlagSet(AdjRib::RIB_OUT_DISCREPANCY));
  adjRib->clearAdjRibFlag(AdjRib::RIB_OUT_DISCREPANCY);

  // ERR logged for withdrawal and announcement (rate-limited)
  EXPECT_GE(messages.size(), 1);

  for (const auto& prefix : kPrefixes_) {
    EXPECT_EQ(
        group_->getFromPathTree(group_->PathTree_, prefix, peerOwnerKey, 99),
        nullptr);
  }

  // PL has withdrawals for pathId 99 and announcements for pathId 42
  EXPECT_EQ(countPlPrefixes(adjRib), kPrefixes_.size() * 2);
  for (const auto& prefix : kPrefixes_) {
    EXPECT_TRUE(plContains(adjRib, prefix, 99, nullptr));
    EXPECT_TRUE(plContains(adjRib, prefix, 42, attrs));
  }
}

/*
 * NonAddPath: peer has entry but group doesn't — withdrawal inserted into PL.
 */
TEST_F(UpdateGroupPeerJoinTest, CollapseLiteNoGroupEntry) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();
  auto attrs = makeAttrs(1, 1);

  for (const auto& prefix : kPrefixes_) {
    group_
        ->addToLiteTree(
            group_->LiteTree_, prefix, peerOwnerKey, kPlaceholderPathID)
        ->setPostAttr(attrs);
  }

  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib);

  auto& messages = subscribeToLogMessages("", folly::LogLevel::ERR);
  messages.clear();

  group_->collapseLiteEntries(groupOwnerKey, {adjRib});
  EXPECT_TRUE(adjRib->isAdjRibFlagSet(AdjRib::RIB_OUT_DISCREPANCY));
  adjRib->clearAdjRibFlag(AdjRib::RIB_OUT_DISCREPANCY);

  // ERR logged for withdrawal (rate-limited)
  EXPECT_GE(messages.size(), 1);
  EXPECT_THAT(
      messages[0].first.getMessage(),
      HasSubstr("peer-only entry (no group entry), queuing withdrawal"));

  // Peer entries erased
  for (const auto& prefix : kPrefixes_) {
    EXPECT_EQ(
        group_->getFromLiteTree(group_->LiteTree_, prefix, peerOwnerKey),
        nullptr);
  }

  // PL has withdrawals for all prefixes
  EXPECT_EQ(countPlPrefixes(adjRib), kPrefixes_.size());
  for (const auto& prefix : kPrefixes_) {
    EXPECT_TRUE(plContains(adjRib, prefix, kPlaceholderPathID, nullptr));
  }
}

/*
 * AddPath: peer has prefix entry but group doesn't — withdrawal inserted.
 */
TEST_F(UpdateGroupPeerJoinTest, CollapsePathNoGroupEntry) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();
  auto attrs = makeAttrs(1, 1);

  for (const auto& prefix : kPrefixes_) {
    group_->addToPathTree(group_->PathTree_, prefix, peerOwnerKey, 42)
        ->setPostAttr(attrs);
  }

  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib);

  auto& messages = subscribeToLogMessages("", folly::LogLevel::ERR);
  messages.clear();

  group_->collapsePathEntries(groupOwnerKey, {adjRib});
  EXPECT_TRUE(adjRib->isAdjRibFlagSet(AdjRib::RIB_OUT_DISCREPANCY));
  adjRib->clearAdjRibFlag(AdjRib::RIB_OUT_DISCREPANCY);

  // ERR logged for withdrawal (rate-limited)
  EXPECT_GE(messages.size(), 1);
  EXPECT_THAT(
      messages[0].first.getMessage(), HasSubstr("collapse path withdrawal"));

  for (const auto& prefix : kPrefixes_) {
    EXPECT_EQ(
        group_->getFromPathTree(group_->PathTree_, prefix, peerOwnerKey, 42),
        nullptr);
  }

  // PL has withdrawals for all prefixes at pathId 42
  EXPECT_EQ(countPlPrefixes(adjRib), kPrefixes_.size());
  for (const auto& prefix : kPrefixes_) {
    EXPECT_TRUE(plContains(adjRib, prefix, 42, nullptr));
  }
}

/*
 * AddPath: group has prefix entry but peer doesn't, and
 * ribVersion > detachedRibVersion — announcement inserted into PL.
 */
TEST_F(UpdateGroupPeerJoinTest, CollapsePathNoPeerEntry) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto attrs = makeAttrs(1, 1);
  adjRib->setDetachedRibVersion(5);

  for (const auto& prefix : kPrefixes_) {
    auto* entry =
        group_->addToPathTree(group_->PathTree_, prefix, groupOwnerKey, 42);
    entry->setPostAttr(attrs);
    entry->setRibVersion(10);
  }

  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib);

  auto& messages = subscribeToLogMessages("", folly::LogLevel::ERR);
  messages.clear();

  group_->collapsePathEntries(groupOwnerKey, {adjRib});
  EXPECT_TRUE(adjRib->isAdjRibFlagSet(AdjRib::RIB_OUT_DISCREPANCY));
  adjRib->clearAdjRibFlag(AdjRib::RIB_OUT_DISCREPANCY);

  // ERR logged for announcement (rate-limited)
  EXPECT_GE(messages.size(), 1);
  EXPECT_THAT(
      messages[0].first.getMessage(), HasSubstr("collapse path announcement"));

  // PL has announcements with group's attrs for all prefixes at pathId 42
  EXPECT_EQ(countPlPrefixes(adjRib), kPrefixes_.size());
  for (const auto& prefix : kPrefixes_) {
    EXPECT_TRUE(plContains(adjRib, prefix, 42, attrs));
  }
}

/*
 * AddPath: peer and group have different number of pathIds.
 * Peer's extra pathId gets withdrawal, all entries erased. Discrepancy.
 */
TEST_F(UpdateGroupPeerJoinTest, CollapsePathPathIdCountMismatch) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();
  auto attrs = makeAttrs(1, 1);

  for (const auto& prefix : kPrefixes_) {
    // Group has 1 pathId, peer has 2
    group_->addToPathTree(group_->PathTree_, prefix, groupOwnerKey, 42)
        ->setPostAttr(attrs);
    group_->addToPathTree(group_->PathTree_, prefix, peerOwnerKey, 42)
        ->setPostAttr(attrs);
    group_->addToPathTree(group_->PathTree_, prefix, peerOwnerKey, 99)
        ->setPostAttr(attrs);
  }

  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib);

  auto& messages = subscribeToLogMessages("", folly::LogLevel::ERR);
  messages.clear();

  group_->collapsePathEntries(groupOwnerKey, {adjRib});
  EXPECT_TRUE(adjRib->isAdjRibFlagSet(AdjRib::RIB_OUT_DISCREPANCY));
  adjRib->clearAdjRibFlag(AdjRib::RIB_OUT_DISCREPANCY);

  // ERR logged for withdrawal (rate-limited, pathId 99 not in group)
  EXPECT_GE(messages.size(), 1);
  EXPECT_THAT(
      messages[0].first.getMessage(), HasSubstr("collapse path withdrawal"));

  for (const auto& prefix : kPrefixes_) {
    EXPECT_EQ(
        group_->getFromPathTree(group_->PathTree_, prefix, peerOwnerKey, 42),
        nullptr);
    EXPECT_EQ(
        group_->getFromPathTree(group_->PathTree_, prefix, peerOwnerKey, 99),
        nullptr);
  }

  // PL has withdrawals for pathId 99
  EXPECT_EQ(countPlPrefixes(adjRib), kPrefixes_.size());
  for (const auto& prefix : kPrefixes_) {
    EXPECT_TRUE(plContains(adjRib, prefix, 99, nullptr));
  }
}

/*
 * NonAddPath: mix of shared (pre-detachment, group-only) and cloned entries.
 * Shared entries have ribVersion <= detachedRibVersion — OK, skip.
 * Cloned entries have matching attrs — collapsed.
 */
TEST_F(UpdateGroupPeerJoinTest, CollapseLiteMixedSharedAndCloned) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();
  auto attrs = makeAttrs(1, 1);
  adjRib->setDetachedRibVersion(10);

  // Prefix 1 & 2: shared (group-only, ribVersion <= detachedRibVersion)
  auto* entry1 = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  entry1->setPostAttr(attrs);
  entry1->setRibVersion(5);

  auto* entry2 = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix2, groupOwnerKey, kPlaceholderPathID);
  entry2->setPostAttr(attrs);
  entry2->setRibVersion(10);

  // Prefix 3: cloned (both peer and group)
  group_
      ->addToLiteTree(
          group_->LiteTree_, kV4Prefix3, groupOwnerKey, kPlaceholderPathID)
      ->setPostAttr(attrs);
  group_
      ->addToLiteTree(
          group_->LiteTree_, kV4Prefix3, peerOwnerKey, kPlaceholderPathID)
      ->setPostAttr(attrs);

  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib);

  group_->collapseLiteEntries(groupOwnerKey, {adjRib});
  EXPECT_FALSE(adjRib->isAdjRibFlagSet(AdjRib::RIB_OUT_DISCREPANCY));
  EXPECT_TRUE(adjRib->attrToPrefixMap_.empty());

  // Shared entries — group entry remains, no peer entry
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

  // Cloned entry — peer entry collapsed, group remains
  EXPECT_NE(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix3, groupOwnerKey),
      nullptr);
  EXPECT_EQ(
      group_->getFromLiteTree(group_->LiteTree_, kV4Prefix3, peerOwnerKey),
      nullptr);
}

/*
 * NonAddPath: group-only entry with ribVersion > detachedRibVersion and
 * no peer entry — post-detach entry not cloned. Announcement inserted.
 */
TEST_F(UpdateGroupPeerJoinTest, CollapseLiteFailsWhenPostDetachEntryNotCloned) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto attrs = makeAttrs(1, 1);
  adjRib->setDetachedRibVersion(10);

  // Prefix 1: shared — OK
  auto* entry1 = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  entry1->setPostAttr(attrs);
  entry1->setRibVersion(5);

  // Prefix 2: post-detach, NOT cloned — discrepancy
  auto* entry2 = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix2, groupOwnerKey, kPlaceholderPathID);
  entry2->setPostAttr(attrs);
  entry2->setRibVersion(15);

  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib);

  auto& messages = subscribeToLogMessages("", folly::LogLevel::ERR);
  messages.clear();

  group_->collapseLiteEntries(groupOwnerKey, {adjRib});
  EXPECT_TRUE(adjRib->isAdjRibFlagSet(AdjRib::RIB_OUT_DISCREPANCY));
  adjRib->clearAdjRibFlag(AdjRib::RIB_OUT_DISCREPANCY);

  // ERR logged for the one announcement + summary
  EXPECT_EQ(messages.size(), 2);
  EXPECT_THAT(messages[0].first.getMessage(), HasSubstr("group-only entry"));
  EXPECT_THAT(
      messages[1].first.getMessage(), HasSubstr("collapse completed for peer"));

  // PL has 1 announcement for the post-detach prefix
  EXPECT_EQ(countPlPrefixes(adjRib), 1);
  EXPECT_TRUE(plContains(adjRib, kV4Prefix2, kPlaceholderPathID, attrs));
}

/*
 * AddPath: mix of shared (pre-detachment, group-only) and cloned entries.
 * Shared entries have ribVersion <= detachedRibVersion — OK, skip.
 * Cloned entries have matching attrs — collapsed.
 */
TEST_F(UpdateGroupPeerJoinTest, CollapsePathMixedSharedAndCloned) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto peerOwnerKey = adjRib->getPeerOwnerKey();
  auto attrs = makeAttrs(1, 1);
  uint32_t pathId = 42;
  adjRib->setDetachedRibVersion(10);

  // Prefix 1 & 2: shared (group-only, ribVersion <= detachedRibVersion)
  auto* entry1 = group_->addToPathTree(
      group_->PathTree_, kV4Prefix1, groupOwnerKey, pathId);
  entry1->setPostAttr(attrs);
  entry1->setRibVersion(5);

  auto* entry2 = group_->addToPathTree(
      group_->PathTree_, kV4Prefix2, groupOwnerKey, pathId);
  entry2->setPostAttr(attrs);
  entry2->setRibVersion(10);

  // Prefix 3: cloned (both peer and group)
  group_->addToPathTree(group_->PathTree_, kV4Prefix3, groupOwnerKey, pathId)
      ->setPostAttr(attrs);
  group_->addToPathTree(group_->PathTree_, kV4Prefix3, peerOwnerKey, pathId)
      ->setPostAttr(attrs);

  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib);

  group_->collapsePathEntries(groupOwnerKey, {adjRib});
  EXPECT_FALSE(adjRib->isAdjRibFlagSet(AdjRib::RIB_OUT_DISCREPANCY));
  EXPECT_TRUE(adjRib->attrToPrefixMap_.empty());

  // Shared entries — group entry remains, no peer entry
  EXPECT_NE(
      group_->getFromPathTree(
          group_->PathTree_, kV4Prefix1, groupOwnerKey, pathId),
      nullptr);
  EXPECT_EQ(
      group_->getFromPathTree(
          group_->PathTree_, kV4Prefix1, peerOwnerKey, pathId),
      nullptr);
  EXPECT_NE(
      group_->getFromPathTree(
          group_->PathTree_, kV4Prefix2, groupOwnerKey, pathId),
      nullptr);
  EXPECT_EQ(
      group_->getFromPathTree(
          group_->PathTree_, kV4Prefix2, peerOwnerKey, pathId),
      nullptr);

  // Cloned entry — peer entry collapsed, group remains
  EXPECT_NE(
      group_->getFromPathTree(
          group_->PathTree_, kV4Prefix3, groupOwnerKey, pathId),
      nullptr);
  EXPECT_EQ(
      group_->getFromPathTree(
          group_->PathTree_, kV4Prefix3, peerOwnerKey, pathId),
      nullptr);
}

/*
 * AddPath: group-only entry with ribVersion > detachedRibVersion and
 * no peer entry — post-detach entry not cloned. Announcement inserted.
 */
TEST_F(UpdateGroupPeerJoinTest, CollapsePathFailsWhenPostDetachEntryNotCloned) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto attrs = makeAttrs(1, 1);
  uint32_t pathId = 42;
  adjRib->setDetachedRibVersion(10);

  // Prefix 1: shared — OK
  auto* entry1 = group_->addToPathTree(
      group_->PathTree_, kV4Prefix1, groupOwnerKey, pathId);
  entry1->setPostAttr(attrs);
  entry1->setRibVersion(5);

  // Prefix 2: post-detach, NOT cloned — discrepancy
  auto* entry2 = group_->addToPathTree(
      group_->PathTree_, kV4Prefix2, groupOwnerKey, pathId);
  entry2->setPostAttr(attrs);
  entry2->setRibVersion(15);

  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib);

  auto& messages = subscribeToLogMessages("", folly::LogLevel::ERR);
  messages.clear();

  group_->collapsePathEntries(groupOwnerKey, {adjRib});
  EXPECT_TRUE(adjRib->isAdjRibFlagSet(AdjRib::RIB_OUT_DISCREPANCY));
  adjRib->clearAdjRibFlag(AdjRib::RIB_OUT_DISCREPANCY);

  // ERR logged for the one announcement + summary
  EXPECT_EQ(messages.size(), 2);
  EXPECT_THAT(
      messages[0].first.getMessage(), HasSubstr("collapse path announcement"));
  EXPECT_THAT(
      messages[1].first.getMessage(), HasSubstr("collapse completed for peer"));

  // PL has 1 announcement for the post-detach prefix
  EXPECT_EQ(countPlPrefixes(adjRib), 1);
  EXPECT_TRUE(plContains(adjRib, kV4Prefix2, pathId, attrs));
}

/*
 * When tryAcceptPeersToGroup finds discrepancies, the peer is sent back to
 * DETACHED_RUNNING. Its detachedRibVersion must be updated to the group's
 * lastSeenRibVersion so the next collapse only flags entries newer than
 * this point — preventing an infinite discrepancy-resolution loop.
 */
TEST_F(UpdateGroupPeerJoinTest, DiscrepancyUpdatesDetachedRibVersion) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto attrs = makeAttrs(1, 1);

  group_->setLastSeenRibVersion(50);

  auto* entry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  entry->setPostAttr(attrs);
  entry->setRibVersion(30);

  adjRib->setDetachedRibVersion(10);
  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib);

  ASSERT_EQ(adjRib->getDetachedRibVersion(), 10);

  auto accepted = group_->tryAcceptPeersToGroup({adjRib});

  EXPECT_TRUE(accepted.empty());
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::DETACHED_RUNNING);
  EXPECT_EQ(adjRib->getDetachedRibVersion(), 50);
}

/*
 * collapseLiteEntry seeds `hasDiscrepancy` from DETACHED_ON_REGISTRATION, so
 * for a peer that joined already detached every group-only entry counts as a
 * discrepancy regardless of rib version. Once the discrepancy path has
 * collapsed that peer into the group it is sharing the group's entries and is
 * no longer detached-on-registration, so the flag must be cleared.
 *
 * Without clearing it, the retry re-counts the very entries the first round
 * just shared, and the count grows once per attempt instead of settling.
 */
TEST_F(UpdateGroupPeerJoinTest, DiscrepancyClearsDetachedOnRegistration) {
  auto adjRib = createAndRegisterPeer(0);
  auto groupOwnerKey = group_->getGroupOwnerKey();
  auto attrs = makeAttrs(1, 1);

  group_->setLastSeenRibVersion(50);

  /*
   * Inject the discrepancy: a single group-only entry the peer never
   * materialized. Its ribVersion is below the peer's detachedRibVersion, so
   * it is counted only because the peer is detached-on-registration.
   */
  auto* entry = group_->addToLiteTree(
      group_->LiteTree_, kV4Prefix1, groupOwnerKey, kPlaceholderPathID);
  entry->setPostAttr(attrs);
  entry->setRibVersion(30);

  adjRib->setDetachedRibVersion(10);
  adjRib->setAdjRibFlag(AdjRib::DETACHED_ON_REGISTRATION);
  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->markPeerDetached(adjRib);

  const auto discrepanciesBefore = group_->getTotalDiscrepancies();

  EXPECT_TRUE(group_->tryAcceptPeersToGroup({adjRib}).empty());
  EXPECT_EQ(adjRib->getPeerState(), PeerUpdateState::DETACHED_RUNNING);
  EXPECT_EQ(discrepanciesBefore + 1, group_->getTotalDiscrepancies());
  EXPECT_FALSE(adjRib->isAdjRibFlagSet(AdjRib::DETACHED_ON_REGISTRATION))
      << "peer is sharing the group's entries but is still marked "
         "detached-on-registration";

  /*
   * Retry against the same unchanged entry. The peer is no longer
   * detached-on-registration and the entry is not newer than its refreshed
   * detachedRibVersion, so the one discrepancy above must remain the only one.
   */
  adjRib->setPeerState(PeerUpdateState::DETACHED_READY_TO_JOIN);
  group_->tryAcceptPeersToGroup({adjRib});
  EXPECT_EQ(discrepanciesBefore + 1, group_->getTotalDiscrepancies())
      << "retry re-counted entries the first collapse already shared";
}

} // namespace facebook::bgp
