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

/*
 * E2E tests for RIB allocated path IDs (enable_rib_allocated_path_id).
 *
 * An add-path peer advertises many multipaths per prefix. Another add-path
 * peer must receive every multipath under its own RIB allocated path ID, and
 * churn must only send the path IDs that changed. Partial drain and aggregate
 * link bandwidth changes, which apply to every path of a prefix, must re-send
 * every path. An egress policy change must re-send every path under the new
 * policy, including the paths still pending in the peer's change list.
 *
 * Mocked: FIB (TestFib), SessionManager (MockSessionManager)
 * Real: RIB, PeerManagerBase, AdjRib
 */

#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include <fmt/format.h>
#include <folly/IPAddress.h>
#include <folly/coro/BlockingWait.h>
#include <folly/logging/xlog.h>

#include "neteng/fboss/bgp/cpp/BgpServiceBB.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibPolicyCache.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/rib/RibDC.h"
#include "neteng/fboss/bgp/cpp/tests/AdjRibInUtils.h"
#include "neteng/fboss/bgp/cpp/tests/PolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RibPolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/E2ETestFixture.h"

using namespace facebook::bgp;

namespace facebook::bgp {

namespace {

constexpr uint32_t kNumPrefixes = 100;
constexpr uint32_t kNumPaths = 128;
/*
 * Churn 20% of the multipaths of every prefix: withdraw 10% and send new
 * attributes for another 10% (each rounded up).
 */
constexpr uint32_t kNumWithdrawPaths = (kNumPaths + 9) / 10;
constexpr uint32_t kNumUpdatePaths = (kNumPaths + 9) / 10;
constexpr uint32_t kPathCommunityAsn = 65000;
constexpr uint32_t kUpdatedCommunityOffset = 1000;
/*
 * Link bandwidth of every source path, in bytes per second. A power of two, so
 * the float aggregates are exact.
 */
constexpr float kPathLbwBytesPerSec = 1 << 20;

/* Egress policies that tag every path with community 64999:1 or 64999:2. */
constexpr uint32_t kTagCommunityAsn = 64999;
constexpr auto kOldTagPolicyName = "TAG_OLD";
constexpr auto kOldTagCommunity = "64999:1";
constexpr auto kNewTagPolicyName = "TAG_NEW";
constexpr auto kNewTagCommunity = "64999:2";

folly::CIDRNetwork prefixCidr(uint32_t prefixIdx) {
  return folly::IPAddress::createNetwork(
      fmt::format("10.{}.0.0/16", prefixIdx));
}

std::string nexthopStr(uint32_t pathIdx) {
  return fmt::format("11.0.0.{}", pathIdx + 1);
}

/* Source path i of every prefix is tagged with community 65000:(i + 1). */
std::string communityStr(uint32_t pathIdx) {
  return fmt::format("{}:{}", kPathCommunityAsn, pathIdx + 1);
}

/* An updated path i gets a new nexthop and community 65000:(1000 + i + 1). */
std::string updatedNexthopStr(uint32_t pathIdx) {
  return fmt::format("11.0.1.{}", pathIdx + 1);
}

std::string updatedCommunityStr(uint32_t pathIdx) {
  return fmt::format(
      "{}:{}", kPathCommunityAsn, kUpdatedCommunityOffset + pathIdx + 1);
}

folly::CIDRNetwork toCidr(const facebook::network::thrift::IPPrefix& prefix) {
  const auto& addr = *prefix.prefixAddress()->addr();
  return {
      folly::IPAddress::fromBinary(
          folly::ByteRange(
              reinterpret_cast<const unsigned char*>(addr.data()),
              addr.size())),
      static_cast<uint8_t>(*prefix.prefixLength())};
}

using PathKey = std::pair<folly::CIDRNetwork, uint32_t>;

} // namespace

/*
 * Peer3 (add-path) is the route source. Peer5 (add-path) receives the
 * multipaths. Parameterized on update groups, which also rely on RIB
 * allocated path IDs.
 */
class E2ERibAllocatedPathIdTest : public E2ETestFixture,
                                  public ::testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    setUpComponents(std::nullopt /* receiverEgressPolicy */);
  }

  void setUpComponents(const std::optional<std::string>& receiverEgressPolicy) {
    /*
     * The source accepts link bandwidth, and the receiver advertises the
     * aggregate received link bandwidth on every path.
     */
    auto sourceSpec = kDefaultPeerSpec3_AddPath;
    sourceSpec.receiveLinkBandwidth = ReceiveLinkBandwidth::ACCEPT;
    auto receiverSpec = kDefaultPeerSpec5_AddPath;
    receiverSpec.advertiseLinkBandwidth =
        AdvertiseLinkBandwidth::AGGREGATE_RECEIVED;
    receiverSpec.egressPolicyName = receiverEgressPolicy;
    addPeer(sourceSpec);
    addPeer(receiverSpec);
    enableRibAllocatedPathId();
    /* Large egress queues, so the receiver is drained only between phases. */
    setDefaultQueueSizes(100000, 90000, 1000);
    createRib();
    createPeerManager(/*enableUpdateGroup=*/GetParam());
  }

  /* Partial drain is DC-only, so this suite runs against a RibDC. */
  RibDC* dcRib() {
    return static_cast<RibDC*>(rib_.get());
  }

  void bringUpPeersWithEor() {
    bringUpPeer(kPeerAddr3);
    bringUpPeer(kPeerAddr5);
    sendEoRToPeer(sourcePeerId_);
    sendEoRToPeer(receiverPeerId_);
    EXPECT_TRUE(waitForEoR(sourcePeerId_));
    EXPECT_TRUE(waitForEoR(receiverPeerId_));
  }

  /*
   * The receiver's add-path routes, rebuilt from the drained UPDATEs:
   * (prefix, path ID) -> source path community value.
   */
  std::map<PathKey, uint32_t> receivedPaths_;
  /* Received paths carrying kDrainCommunity. */
  std::set<PathKey> drainedPaths_;
  /* Link bandwidth on each received path. */
  std::map<PathKey, std::optional<float>> receivedLbw_;
  /* Egress policy tag communities (64999:x values) on each received path. */
  std::map<PathKey, std::set<uint32_t>> receivedTags_;
  /* What the drains since the last resetDrainCounts() carried. */
  std::map<PathKey, uint32_t> announced_;
  std::set<PathKey> withdrawn_;
  size_t announceCount_{0};
  size_t withdrawCount_{0};

  void resetDrainCounts() {
    announced_.clear();
    withdrawn_.clear();
    announceCount_ = 0;
    withdrawCount_ = 0;
  }

  void foldUpdate(const nettools::bgplib::BgpUpdate2& update) {
    uint32_t community = 0;
    bool drained = false;
    std::set<uint32_t> tags;
    for (const auto& comm : *update.attrs()->communities()) {
      if (static_cast<uint32_t>(*comm.asn()) == kPathCommunityAsn) {
        community = static_cast<uint32_t>(*comm.value());
      }
      if (static_cast<uint32_t>(*comm.asn()) == kTagCommunityAsn) {
        tags.insert(static_cast<uint32_t>(*comm.value()));
      }
      drained |=
          (static_cast<uint32_t>(*comm.asn()) ==
               static_cast<uint32_t>(kDrainCommunity.asn) &&
           static_cast<uint32_t>(*comm.value()) ==
               static_cast<uint32_t>(kDrainCommunity.value));
    }
    std::optional<float> lbw;
    if (!update.v4Announced2()->empty() ||
        !update.mpAnnounced()->prefixes()->empty()) {
      lbw = facebook::bgp::BgpPath(
                facebook::bgp::BgpPathFields(*BgpUpdate2toBgpPathC(update)))
                .getNonTransitiveLbwValue();
    }
    auto announce = [&](const auto& prefix) {
      const PathKey key{
          toCidr(*prefix.prefix()),
          static_cast<uint32_t>(prefix.pathId().value_or(0))};
      receivedPaths_[key] = community;
      announced_[key] = community;
      ++announceCount_;
      if (drained) {
        drainedPaths_.insert(key);
      } else {
        drainedPaths_.erase(key);
      }
      receivedLbw_[key] = lbw;
      receivedTags_[key] = tags;
    };
    auto withdraw = [&](const auto& prefix) {
      const PathKey key{
          toCidr(*prefix.prefix()),
          static_cast<uint32_t>(prefix.pathId().value_or(0))};
      receivedPaths_.erase(key);
      drainedPaths_.erase(key);
      receivedLbw_.erase(key);
      receivedTags_.erase(key);
      withdrawn_.insert(key);
      ++withdrawCount_;
    };
    for (const auto& prefix : *update.v4Announced2()) {
      announce(prefix);
    }
    for (const auto& prefix : *update.mpAnnounced()->prefixes()) {
      announce(prefix);
    }
    for (const auto& prefix : *update.v4Withdrawn2()) {
      withdraw(prefix);
    }
    for (const auto& prefix : *update.mpWithdrawn()->prefixes()) {
      withdraw(prefix);
    }
  }

  /*
   * Drain the receiver until done() holds, then drain once more so any extra
   * (unexpected) UPDATEs are also counted.
   */
  bool drainReceiverUntil(const std::function<bool()>& done) {
    for (int i = 0; i < 100 && !done(); i++) {
      for (const auto& msg : drainAllOutboundMessagesToOrderedVec(
               receiverPeerId_, /*idleRetries=*/10, /*maxMessages=*/0)) {
        if (msg.update) {
          foldUpdate(*msg.update);
        }
      }
    }
    for (const auto& msg : drainAllOutboundMessagesToOrderedVec(
             receiverPeerId_, /*idleRetries=*/20, /*maxMessages=*/0)) {
      if (msg.update) {
        foldUpdate(*msg.update);
      }
    }
    return done();
  }

  /*
   * Send source path pathIdx of every prefix in one UPDATE, under add-path ID
   * pathIdx + 1. The path has the same attributes on every prefix.
   */
  void announcePathForAllPrefixes(
      uint32_t pathIdx,
      const std::string& nexthop,
      const std::string& community,
      std::optional<float> lbwBytesPerSec = std::nullopt) {
    std::shared_ptr<nettools::bgplib::BgpUpdate2> update;
    for (uint32_t prefixIdx = 0; prefixIdx < kNumPrefixes; prefixIdx++) {
      auto single = createBgpUpdateAnnouncement(
          true /* isV4 */,
          prefixCidr(prefixIdx),
          nexthop,
          {65001} /* asPathSeq */,
          {community},
          pathIdx + 1 /* addPathId */,
          kLocalPref,
          0 /* med */,
          lbwBytesPerSec);
      if (!update) {
        update = std::move(single);
        continue;
      }
      for (const auto& nlri : *single->v4Announced2()) {
        update->v4Announced2()->push_back(nlri);
      }
      for (const auto& nlri : *single->mpAnnounced()->prefixes()) {
        update->mpAnnounced()->prefixes()->push_back(nlri);
      }
    }
    pushToSource(std::move(update));
  }

  /* Withdraw source paths [0, numPaths) of every prefix in one UPDATE. */
  void withdrawPathsForAllPrefixes(uint32_t numPaths) {
    auto update = std::make_shared<nettools::bgplib::BgpUpdate2>();
    for (uint32_t prefixIdx = 0; prefixIdx < kNumPrefixes; prefixIdx++) {
      for (uint32_t pathIdx = 0; pathIdx < numPaths; pathIdx++) {
        auto single = createBgpUpdateWithdrawal(
            true /* isV4 */,
            prefixCidr(prefixIdx),
            pathIdx + 1 /* addPathId */);
        for (const auto& nlri : *single->v4Withdrawn2()) {
          update->v4Withdrawn2()->push_back(nlri);
        }
      }
    }
    pushToSource(std::move(update));
  }

  void pushToSource(std::shared_ptr<nettools::bgplib::BgpUpdate2> update) {
    auto queues = getPeerQueues(sourcePeerId_);
    ASSERT_TRUE(queues.has_value());
    folly::coro::blockingWait(queues->adjRibInQ->push(std::move(update)));
  }

  /*
   * Install a path selection policy for every prefix, and wait for the RIB to
   * apply it.
   */
  void setPathSelectionPolicyForAllPrefixes(
      const rib_policy::TPathSelector& tPathSelector) {
    std::vector<folly::CIDRNetwork> prefixes;
    for (uint32_t prefixIdx = 0; prefixIdx < kNumPrefixes; prefixIdx++) {
      prefixes.push_back(prefixCidr(prefixIdx));
    }
    const auto tPolicy =
        createTPathSelectionPolicyWithPathSelector(prefixes, tPathSelector);
    const auto expected = PathSelectionPolicy{tPolicy}.toThrift();
    dcRib()->setPathSelectionPolicy(
        std::make_unique<rib_policy::TPathSelectionPolicy>(tPolicy));
    WITH_RETRIES_N_TIMED(100, std::chrono::milliseconds(20), {
      EXPECT_EVENTUALLY_TRUE(dcRib()->getPathSelectionPolicy() == expected)
          << "RIB never installed the path selection policy";
    });
  }

  /* Source path communities the receiver holds, per prefix. */
  std::map<folly::CIDRNetwork, std::set<uint32_t>> communitiesByPrefix() const {
    std::map<folly::CIDRNetwork, std::set<uint32_t>> communities;
    for (const auto& [key, community] : receivedPaths_) {
      communities[key.first].insert(community);
    }
    return communities;
  }

  const BgpPeerId sourcePeerId_{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  const BgpPeerId receiverPeerId_{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};
};

INSTANTIATE_TEST_SUITE_P(
    UpdateGroupModes,
    E2ERibAllocatedPathIdTest,
    ::testing::Values(false, true),
    [](const testing::TestParamInfo<bool>& info) {
      return std::string(info.param ? "Enabled" : "Disabled");
    });

/*
 * 100 prefixes with 128 multipaths each are all advertised to an add-path
 * peer, each multipath under its own path ID. Then 20% of the multipaths of
 * every prefix churn: 10% are withdrawn and 10% get new attributes. The
 * receiver gets exactly the withdrawn path IDs, and the updated paths under
 * the path IDs they already had. Unchanged multipaths are never re-sent.
 */
TEST_P(E2ERibAllocatedPathIdTest, ChurnSendsOnlyChangedPathIds) {
  bringUpPeersWithEor();
  std::set<uint32_t> withdrawCommunities;
  std::set<uint32_t> updateCommunities;
  std::map<folly::CIDRNetwork, std::set<uint32_t>> expectedCommunities;
  for (uint32_t prefixIdx = 0; prefixIdx < kNumPrefixes; prefixIdx++) {
    for (uint32_t pathIdx = 0; pathIdx < kNumPaths; pathIdx++) {
      expectedCommunities[prefixCidr(prefixIdx)].insert(pathIdx + 1);
    }
  }
  for (uint32_t pathIdx = 0; pathIdx < kNumWithdrawPaths + kNumUpdatePaths;
       pathIdx++) {
    (pathIdx < kNumWithdrawPaths ? withdrawCommunities : updateCommunities)
        .insert(pathIdx + 1);
  }

  /* Announce every multipath of every prefix, one UPDATE per source path. */
  for (uint32_t pathIdx = 0; pathIdx < kNumPaths; pathIdx++) {
    announcePathForAllPrefixes(
        pathIdx, nexthopStr(pathIdx), communityStr(pathIdx));
  }
  ASSERT_TRUE(drainReceiverUntil([&]() {
    return receivedPaths_.size() == kNumPrefixes * kNumPaths;
  })) << "received "
      << receivedPaths_.size() << " add-path routes";
  EXPECT_EQ(0, withdrawCount_);
  EXPECT_EQ(expectedCommunities, communitiesByPrefix());

  /*
   * Churn every prefix at once: withdraw 10% of the multipaths and send new
   * attributes for another 10%.
   */
  std::set<PathKey> expectedWithdrawn;
  std::map<PathKey, uint32_t> expectedAnnounced;
  auto expectedFinal = receivedPaths_;
  for (const auto& [key, community] : receivedPaths_) {
    if (withdrawCommunities.contains(community)) {
      expectedWithdrawn.insert(key);
      expectedFinal.erase(key);
    } else if (updateCommunities.contains(community)) {
      // An updated path keeps its path ID.
      expectedAnnounced[key] = kUpdatedCommunityOffset + community;
      expectedFinal[key] = kUpdatedCommunityOffset + community;
    }
  }
  ASSERT_EQ(kNumPrefixes * kNumWithdrawPaths, expectedWithdrawn.size());
  ASSERT_EQ(kNumPrefixes * kNumUpdatePaths, expectedAnnounced.size());
  resetDrainCounts();
  withdrawPathsForAllPrefixes(kNumWithdrawPaths);
  for (uint32_t pathIdx = kNumWithdrawPaths;
       pathIdx < kNumWithdrawPaths + kNumUpdatePaths;
       pathIdx++) {
    announcePathForAllPrefixes(
        pathIdx, updatedNexthopStr(pathIdx), updatedCommunityStr(pathIdx));
  }
  ASSERT_TRUE(drainReceiverUntil([&]() {
    return withdrawn_.size() == expectedWithdrawn.size() &&
        announced_.size() == expectedAnnounced.size();
  })) << "withdrawn "
      << withdrawn_.size() << ", announced " << announced_.size();
  EXPECT_EQ(expectedWithdrawn, withdrawn_);
  EXPECT_EQ(expectedAnnounced, announced_);
  // Each changed path is sent once. Unchanged multipaths are not re-sent.
  EXPECT_EQ(kNumPrefixes * kNumWithdrawPaths, withdrawCount_);
  EXPECT_EQ(kNumPrefixes * kNumUpdatePaths, announceCount_);
  EXPECT_EQ(expectedFinal, receivedPaths_);
}

/*
 * Partially draining every prefix at once re-sends every multipath under the
 * path ID it already had, with the drain community. Clearing the policy
 * re-sends every multipath without it. Nothing is withdrawn.
 */
TEST_P(E2ERibAllocatedPathIdTest, PartialDrainResendsAllPathIds) {
  bringUpPeersWithEor();
  for (uint32_t pathIdx = 0; pathIdx < kNumPaths; pathIdx++) {
    announcePathForAllPrefixes(
        pathIdx, nexthopStr(pathIdx), communityStr(pathIdx));
  }
  ASSERT_TRUE(drainReceiverUntil([&]() {
    return receivedPaths_.size() == kNumPrefixes * kNumPaths;
  })) << "received "
      << receivedPaths_.size() << " add-path routes";
  const auto advertised = receivedPaths_;
  std::set<PathKey> allPaths;
  for (const auto& [key, _] : advertised) {
    allPaths.insert(key);
  }
  EXPECT_TRUE(drainedPaths_.empty());

  /* Drain every prefix: min nexthop above the multipath count, with drain. */
  rib_policy::TPathSelector drainSelector;
  drainSelector.bgp_native_path_selection_min_nexthop() = kNumPaths + 1;
  drainSelector.drain_on_min_nexthop_violation() = true;
  resetDrainCounts();
  setPathSelectionPolicyForAllPrefixes(drainSelector);
  ASSERT_TRUE(drainReceiverUntil([&]() {
    return drainedPaths_.size() == allPaths.size();
  })) << "drained "
      << drainedPaths_.size() << " add-path routes";
  EXPECT_EQ(advertised, announced_);
  EXPECT_EQ(advertised.size(), announceCount_);
  EXPECT_EQ(0, withdrawCount_);
  EXPECT_EQ(allPaths, drainedPaths_);
  EXPECT_EQ(advertised, receivedPaths_);

  /* Clearing the policy undrains every prefix. */
  resetDrainCounts();
  setPathSelectionPolicyForAllPrefixes(rib_policy::TPathSelector{});
  ASSERT_TRUE(drainReceiverUntil([&]() {
    return drainedPaths_.empty() && announced_.size() == advertised.size();
  })) << "drained "
      << drainedPaths_.size() << ", announced " << announced_.size();
  EXPECT_EQ(advertised, announced_);
  EXPECT_EQ(advertised.size(), announceCount_);
  EXPECT_EQ(0, withdrawCount_);
  EXPECT_EQ(advertised, receivedPaths_);
}

/*
 * The receiver advertises the aggregate received link bandwidth on every path.
 * Withdrawing 10% of the multipaths of every prefix at once withdraws exactly
 * those path IDs, and re-sends every remaining path with the new aggregate.
 */
TEST_P(E2ERibAllocatedPathIdTest, AggregateLbwResendsAllPathIds) {
  bringUpPeersWithEor();
  for (uint32_t pathIdx = 0; pathIdx < kNumPaths; pathIdx++) {
    announcePathForAllPrefixes(
        pathIdx,
        nexthopStr(pathIdx),
        communityStr(pathIdx),
        kPathLbwBytesPerSec);
  }
  ASSERT_TRUE(drainReceiverUntil([&]() {
    return receivedPaths_.size() == kNumPrefixes * kNumPaths;
  })) << "received "
      << receivedPaths_.size() << " add-path routes";
  std::map<PathKey, std::optional<float>> expectedLbw;
  for (const auto& [key, _] : receivedPaths_) {
    expectedLbw[key] = kNumPaths * kPathLbwBytesPerSec;
  }
  EXPECT_EQ(expectedLbw, receivedLbw_);

  /* Withdraw 10% of the multipaths of every prefix at once. */
  std::set<PathKey> expectedWithdrawn;
  std::map<PathKey, uint32_t> expectedAnnounced;
  expectedLbw.clear();
  for (const auto& [key, community] : receivedPaths_) {
    if (community <= kNumWithdrawPaths) {
      expectedWithdrawn.insert(key);
    } else {
      // Every remaining path is re-sent with the new aggregate.
      expectedAnnounced[key] = community;
      expectedLbw[key] = (kNumPaths - kNumWithdrawPaths) * kPathLbwBytesPerSec;
    }
  }
  ASSERT_EQ(kNumPrefixes * kNumWithdrawPaths, expectedWithdrawn.size());
  resetDrainCounts();
  withdrawPathsForAllPrefixes(kNumWithdrawPaths);
  ASSERT_TRUE(drainReceiverUntil([&]() {
    return withdrawn_.size() == expectedWithdrawn.size() &&
        announced_.size() == expectedAnnounced.size();
  })) << "withdrawn "
      << withdrawn_.size() << ", announced " << announced_.size();
  EXPECT_EQ(expectedWithdrawn, withdrawn_);
  EXPECT_EQ(expectedAnnounced, announced_);
  EXPECT_EQ(expectedWithdrawn.size(), withdrawCount_);
  EXPECT_EQ(expectedAnnounced.size(), announceCount_);
  EXPECT_EQ(expectedAnnounced, receivedPaths_);
  EXPECT_EQ(expectedLbw, receivedLbw_);
}

/*
 * The receiver has an egress policy, and it can be switched at runtime through
 * the BgpServiceBB::co_setPeersPolicy handler.
 */
class E2ERibAllocatedPathIdEgressPolicyTest : public E2ERibAllocatedPathIdTest {
 protected:
  void SetUp() override {
    bgp_policy::BgpPolicies policies;
    policies.bgp_policy_statements()->emplace_back(
        buildTagPolicy(kOldTagPolicyName, kOldTagCommunity));
    policies.bgp_policy_statements()->emplace_back(
        buildTagPolicy(kNewTagPolicyName, kNewTagCommunity));
    setPolicyConfig(policies);
    /*
     * The receiver keeps the default 8-entry egress queue, so it is blocked
     * by backpressure while the churn and the policy change are processed.
     */
    setQueueSizeForPeer(kPeerAddr5, 8, 6, 2);
    setUpComponents(kOldTagPolicyName);
    watchdog_ = std::make_unique<Watchdog>(config_);
    bgpService_ = std::make_shared<BgpServiceBB>(
        *peerManager_,
        configManager_,
        *rib_,
        *watchdog_,
        /*nlWrapper=*/nullptr,
        false);
  }

  /* Accept every path and add the given community. */
  static bgp_policy::BgpPolicyStatement buildTagPolicy(
      const std::string& name,
      const std::string& community) {
    return createBgpPolicyStatement(
        name,
        {createBgpPolicyTerm(
            "tag-all",
            "Accept and tag with community",
            {},
            {createBgpPolicyCommunityAction(
                 bgp_policy::CommunityActionType::ADD, {community}),
             createBgpPolicyAction(bgp_policy::BgpPolicyActionType::PERMIT)},
            bgp_policy::FlowControlAction::NEXT_TERM)});
  }

  neteng::fboss::bgp::thrift::BgpPolicyChangeResult setReceiverEgressPolicy(
      const std::string& policyName) {
    std::map<std::string, std::map<bgp_policy::DIRECTION, std::string>>
        peersPolicy;
    peersPolicy[kPeerAddr5.str()][bgp_policy::DIRECTION::OUT] = policyName;
    return folly::coro::blockingWait(bgpService_->co_setPeersPolicy(
        std::make_unique<decltype(peersPolicy)>(std::move(peersPolicy))));
  }

  /*
   * Wait until the receiver's change-list consumer, the update group's when
   * update groups are enabled, has not reached the end of the change list.
   */
  bool waitForReceiverChangeListPended() {
    bool pended = false;
    WITH_RETRIES_N(50, {
      peerManager_->getEventBase().runInEventBaseThreadAndWait([&]() {
        pended = false;
        auto adjRib = getAdjRibByAddr(kPeerAddr5);
        if (!adjRib) {
          return;
        }
        if (GetParam()) {
          const auto& consumer =
              adjRib->getUpdateGroup()->getChangeListConsumer();
          pended = consumer && !consumer->isReady();
          return;
        }
        const auto& consumer = adjRib->getChangeListConsumer();
        pended = consumer && !consumer->isReady();
      });
      EXPECT_EVENTUALLY_TRUE(pended);
    });
    return pended;
  }

  /* Egress policy evaluations so far: policy cache hits plus misses. */
  uint64_t policyCacheLookups() {
    uint64_t lookups = 0;
    peerManager_->getEventBase().runInEventBaseThreadAndWait([&]() {
      const auto cache = AdjRibPolicyCache::get();
      lookups = cache->getTotalCacheHit() + cache->getTotalCacheMiss();
    });
    return lookups;
  }

  std::unique_ptr<Watchdog> watchdog_;
  std::shared_ptr<BgpServiceBB> bgpService_;
};

INSTANTIATE_TEST_SUITE_P(
    UpdateGroupModes,
    E2ERibAllocatedPathIdEgressPolicyTest,
    ::testing::Values(false, true),
    [](const testing::TestParamInfo<bool>& info) {
      return std::string(info.param ? "Enabled" : "Disabled");
    });

/*
 * Switching the receiver's egress policy at runtime, together with churn on
 * 50% of the multipaths of every prefix (25% withdrawn, 25% with new
 * attributes), leaves every remaining path with the new policy's community
 * instead of the old one. Updated paths keep their path IDs, and only the
 * withdrawn path IDs are withdrawn.
 */
TEST_P(E2ERibAllocatedPathIdEgressPolicyTest, PolicyChangeUpdatesAllPathIds) {
  constexpr uint32_t kNumPolicyWithdrawPaths = kNumPaths / 4;
  constexpr uint32_t kNumPolicyUpdatePaths = kNumPaths / 4;
  bringUpPeersWithEor();
  for (uint32_t pathIdx = 0; pathIdx < kNumPaths; pathIdx++) {
    announcePathForAllPrefixes(
        pathIdx, nexthopStr(pathIdx), communityStr(pathIdx));
  }
  ASSERT_TRUE(drainReceiverUntil([&]() {
    return receivedPaths_.size() == kNumPrefixes * kNumPaths;
  })) << "received "
      << receivedPaths_.size() << " add-path routes";
  std::map<PathKey, std::set<uint32_t>> expectedTags;
  for (const auto& [key, _] : receivedPaths_) {
    expectedTags[key] = {1};
  }
  EXPECT_EQ(expectedTags, receivedTags_);

  /*
   * Churn 50% of the multipaths of every prefix, then switch the receiver to
   * the new policy while it is blocked, without draining in between.
   */
  std::set<PathKey> expectedWithdrawn;
  auto expectedFinal = receivedPaths_;
  expectedTags.clear();
  for (const auto& [key, community] : receivedPaths_) {
    if (community <= kNumPolicyWithdrawPaths) {
      expectedWithdrawn.insert(key);
      expectedFinal.erase(key);
      continue;
    }
    if (community <= kNumPolicyWithdrawPaths + kNumPolicyUpdatePaths) {
      // An updated path keeps its path ID.
      expectedFinal[key] = kUpdatedCommunityOffset + community;
    }
    expectedTags[key] = {2};
  }
  resetDrainCounts();
  /*
   * The updated paths fill the receiver's egress queue. The withdrawals are
   * sent once it is blocked, so they stay in the change list when the policy
   * changes.
   */
  for (uint32_t pathIdx = kNumPolicyWithdrawPaths;
       pathIdx < kNumPolicyWithdrawPaths + kNumPolicyUpdatePaths;
       pathIdx++) {
    announcePathForAllPrefixes(
        pathIdx, updatedNexthopStr(pathIdx), updatedCommunityStr(pathIdx));
  }
  ASSERT_TRUE(waitForPeerQueueBlocked(receiverPeerId_));
  withdrawPathsForAllPrefixes(kNumPolicyWithdrawPaths);
  ASSERT_TRUE(waitForReceiverChangeListPended());
  const auto lookupsBeforePolicyChange = policyCacheLookups();
  ASSERT_EQ(
      neteng::fboss::bgp::thrift::BgpPolicyChangeResult::POLICIES_APPLIED,
      setReceiverEgressPolicy(kNewTagPolicyName));
  ASSERT_TRUE(drainReceiverUntil([&]() {
    return withdrawn_ == expectedWithdrawn && receivedPaths_ == expectedFinal &&
        receivedTags_ == expectedTags;
  })) << "withdrawn "
      << withdrawn_.size() << ", received " << receivedPaths_.size();
  EXPECT_EQ(expectedWithdrawn, withdrawn_);
  EXPECT_EQ(expectedFinal, receivedPaths_);
  EXPECT_EQ(expectedTags, receivedTags_);
  /* Every remaining path is evaluated under the new policy at least once. */
  EXPECT_GE(
      policyCacheLookups() - lookupsBeforePolicyChange,
      kNumPrefixes * (kNumPaths - kNumPolicyWithdrawPaths));
}

/*
 * Switching the receiver's egress policy at runtime, while new communities for
 * 25% of the multipaths of every prefix are still pending in its change list,
 * leaves every path with the new policy's community instead of the old one.
 * Nothing is withdrawn, and every path keeps its path ID.
 */
TEST_P(
    E2ERibAllocatedPathIdEgressPolicyTest,
    PolicyChangeWithPendingUpdatesUpdatesAllPathIds) {
  constexpr uint32_t kNumFillUpdatePaths = kNumPaths / 4;
  constexpr uint32_t kNumPendingUpdatePaths = kNumPaths / 4;
  bringUpPeersWithEor();
  for (uint32_t pathIdx = 0; pathIdx < kNumPaths; pathIdx++) {
    announcePathForAllPrefixes(
        pathIdx, nexthopStr(pathIdx), communityStr(pathIdx));
  }
  ASSERT_TRUE(drainReceiverUntil([&]() {
    return receivedPaths_.size() == kNumPrefixes * kNumPaths;
  })) << "received "
      << receivedPaths_.size() << " add-path routes";
  std::map<PathKey, std::set<uint32_t>> expectedTags;
  for (const auto& [key, _] : receivedPaths_) {
    expectedTags[key] = {1};
  }
  EXPECT_EQ(expectedTags, receivedTags_);

  /*
   * Send new communities for 50% of the multipaths of every prefix, then switch
   * the receiver to the new policy while it is blocked, without draining in
   * between.
   */
  auto expectedFinal = receivedPaths_;
  for (auto& [key, community] : expectedFinal) {
    if (community <= kNumFillUpdatePaths + kNumPendingUpdatePaths) {
      // An updated path keeps its path ID.
      community += kUpdatedCommunityOffset;
    }
    expectedTags[key] = {2};
  }
  resetDrainCounts();
  /*
   * Only the communities change, so the multipaths and their nexthops stay the
   * same and nothing is withdrawn. The first updates fill the receiver's
   * egress queue. The rest are sent once it is blocked, so they stay in the
   * change list when the policy changes.
   */
  for (uint32_t pathIdx = 0; pathIdx < kNumFillUpdatePaths; pathIdx++) {
    announcePathForAllPrefixes(
        pathIdx, nexthopStr(pathIdx), updatedCommunityStr(pathIdx));
  }
  ASSERT_TRUE(waitForPeerQueueBlocked(receiverPeerId_));
  for (uint32_t pathIdx = kNumFillUpdatePaths;
       pathIdx < kNumFillUpdatePaths + kNumPendingUpdatePaths;
       pathIdx++) {
    announcePathForAllPrefixes(
        pathIdx, nexthopStr(pathIdx), updatedCommunityStr(pathIdx));
  }
  ASSERT_TRUE(waitForReceiverChangeListPended());
  const auto lookupsBeforePolicyChange = policyCacheLookups();
  ASSERT_EQ(
      neteng::fboss::bgp::thrift::BgpPolicyChangeResult::POLICIES_APPLIED,
      setReceiverEgressPolicy(kNewTagPolicyName));
  ASSERT_TRUE(drainReceiverUntil([&]() {
    return receivedPaths_ == expectedFinal && receivedTags_ == expectedTags;
  })) << "received "
      << receivedPaths_.size() << ", withdrawn " << withdrawn_.size();
  EXPECT_TRUE(withdrawn_.empty());
  EXPECT_EQ(expectedFinal, receivedPaths_);
  EXPECT_EQ(expectedTags, receivedTags_);
  /* Every path is evaluated under the new policy at least once. */
  EXPECT_GE(
      policyCacheLookups() - lookupsBeforePolicyChange,
      kNumPrefixes * kNumPaths);
}

/*
 * The receiver has the old tag egress policy and the default large egress
 * queue, so each announced path is evaluated by the egress policy exactly once.
 */
class E2ERibAllocatedPathIdPolicyCacheTest
    : public E2ERibAllocatedPathIdEgressPolicyTest {
 protected:
  void SetUp() override {
    bgp_policy::BgpPolicies policies;
    policies.bgp_policy_statements()->emplace_back(
        buildTagPolicy(kOldTagPolicyName, kOldTagCommunity));
    setPolicyConfig(policies);
    setUpComponents(kOldTagPolicyName);
  }

  /*
   * Wait until every prefix in the RIB holds exactly the source paths with
   * these community values.
   */
  bool waitForRibCommunities(const std::set<uint32_t>& communities) {
    bool done = false;
    WITH_RETRIES_N(60, {
      done = true;
      for (uint32_t prefixIdx = 0; prefixIdx < kNumPrefixes && done;
           prefixIdx++) {
        const auto ribEntries = rib_->getRibEntryForPrefix(
            std::make_unique<std::string>(
                folly::IPAddress::networkToString(prefixCidr(prefixIdx))));
        std::set<uint32_t> found;
        if (!ribEntries.empty()) {
          for (const auto& [_, paths] : *ribEntries[0].paths()) {
            for (const auto& path : paths) {
              for (const auto& comm : path.communities().value_or({})) {
                if (static_cast<uint32_t>(*comm.asn()) == kPathCommunityAsn) {
                  found.insert(static_cast<uint32_t>(*comm.value()));
                }
              }
            }
          }
        }
        done = found == communities;
      }
      EXPECT_EVENTUALLY_TRUE(done);
    });
    return done;
  }
};

INSTANTIATE_TEST_SUITE_P(
    UpdateGroupModes,
    E2ERibAllocatedPathIdPolicyCacheTest,
    ::testing::Values(false, true),
    [](const testing::TestParamInfo<bool>& info) {
      return std::string(info.param ? "Enabled" : "Disabled");
    });

/*
 * Churn on 20% of the multipaths of every prefix (10% withdrawn, 10% with new
 * attributes) only evaluates the egress policy for the updated paths. The
 * unchanged multipaths are not processed again.
 */
TEST_P(E2ERibAllocatedPathIdPolicyCacheTest, ChurnEvaluatesOnlyChangedPathIds) {
  bringUpPeersWithEor();
  const auto lookupsBeforeAnnounce = policyCacheLookups();
  /*
   * Each burst of UPDATEs is processed in one best path pass, so every prefix
   * is published to the change list once. A prefix published again before the
   * source peer consumes it keeps its earlier paths flagged as updated, so the
   * receiver would evaluate them again.
   */
  folly::coro::blockingWait(ribInQ_.push(
      PauseBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE)));
  std::set<uint32_t> expectedCommunities;
  for (uint32_t pathIdx = 0; pathIdx < kNumPaths; pathIdx++) {
    announcePathForAllPrefixes(
        pathIdx, nexthopStr(pathIdx), communityStr(pathIdx));
    expectedCommunities.insert(pathIdx + 1);
  }
  ASSERT_TRUE(waitForRibCommunities(expectedCommunities));
  folly::coro::blockingWait(ribInQ_.push(
      ResumeBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE)));
  ASSERT_TRUE(drainReceiverUntil([&]() {
    return receivedPaths_.size() == kNumPrefixes * kNumPaths;
  })) << "received "
      << receivedPaths_.size() << " add-path routes";
  /* One evaluation per announced path. */
  const auto lookupsAfterAnnounce = policyCacheLookups();
  EXPECT_EQ(
      kNumPrefixes * kNumPaths, lookupsAfterAnnounce - lookupsBeforeAnnounce);

  resetDrainCounts();
  folly::coro::blockingWait(ribInQ_.push(
      PauseBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE)));
  withdrawPathsForAllPrefixes(kNumWithdrawPaths);
  for (uint32_t pathIdx = 0; pathIdx < kNumWithdrawPaths; pathIdx++) {
    expectedCommunities.erase(pathIdx + 1);
  }
  for (uint32_t pathIdx = kNumWithdrawPaths;
       pathIdx < kNumWithdrawPaths + kNumUpdatePaths;
       pathIdx++) {
    announcePathForAllPrefixes(
        pathIdx, updatedNexthopStr(pathIdx), updatedCommunityStr(pathIdx));
    expectedCommunities.erase(pathIdx + 1);
    expectedCommunities.insert(kUpdatedCommunityOffset + pathIdx + 1);
  }
  ASSERT_TRUE(waitForRibCommunities(expectedCommunities));
  folly::coro::blockingWait(ribInQ_.push(
      ResumeBestPathAndFibProgramming(RibPauseResumeCause::SAFE_MODE)));
  ASSERT_TRUE(drainReceiverUntil([&]() {
    return withdrawn_.size() == kNumPrefixes * kNumWithdrawPaths &&
        announced_.size() == kNumPrefixes * kNumUpdatePaths;
  })) << "withdrawn "
      << withdrawn_.size() << ", announced " << announced_.size();
  /* Only the updated paths are evaluated. Withdrawals are not evaluated. */
  EXPECT_EQ(
      kNumPrefixes * kNumUpdatePaths,
      policyCacheLookups() - lookupsAfterAnnounce);
}

} // namespace facebook::bgp
