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
 * peer must receive every multipath under its own RIB allocated path ID. An
 * egress policy change must re-send every path under the new policy, including
 * the paths still pending in the peer's change list.
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
#include "neteng/fboss/bgp/cpp/tests/AdjRibInUtils.h"
#include "neteng/fboss/bgp/cpp/tests/PolicyUtils.h"
#include "neteng/fboss/bgp/cpp/tests/RetryUtils.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"
#include "neteng/fboss/bgp/cpp/tests/e2e/E2ETestFixture.h"

using namespace facebook::bgp;

namespace facebook::bgp {

namespace {

constexpr uint32_t kNumPrefixes = 100;
constexpr uint32_t kNumPaths = 128;
constexpr uint32_t kPathCommunityAsn = 65000;
constexpr uint32_t kUpdatedCommunityOffset = 1000;

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

/* An updated path i gets community 65000:(1000 + i + 1). */
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
    auto receiverSpec = kDefaultPeerSpec5_AddPath;
    receiverSpec.egressPolicyName = receiverEgressPolicy;
    addPeer(kDefaultPeerSpec3_AddPath);
    addPeer(receiverSpec);
    enableRibAllocatedPathId();
    /* Large egress queues, so the receiver is drained only between phases. */
    setDefaultQueueSizes(100000, 90000, 1000);
    createRib();
    createPeerManager(/*enableUpdateGroup=*/GetParam());
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
  /* Egress policy tag communities (64999:x values) on each received path. */
  std::map<PathKey, std::set<uint32_t>> receivedTags_;
  /* What the drains since the last resetDrainCounts() withdrew. */
  std::set<PathKey> withdrawn_;

  void resetDrainCounts() {
    withdrawn_.clear();
  }

  void foldUpdate(const nettools::bgplib::BgpUpdate2& update) {
    uint32_t community = 0;
    std::set<uint32_t> tags;
    for (const auto& comm : *update.attrs()->communities()) {
      if (static_cast<uint32_t>(*comm.asn()) == kPathCommunityAsn) {
        community = static_cast<uint32_t>(*comm.value());
      }
      if (static_cast<uint32_t>(*comm.asn()) == kTagCommunityAsn) {
        tags.insert(static_cast<uint32_t>(*comm.value()));
      }
    }
    auto announce = [&](const auto& prefix) {
      const PathKey key{
          toCidr(*prefix.prefix()),
          static_cast<uint32_t>(prefix.pathId().value_or(0))};
      receivedPaths_[key] = community;
      receivedTags_[key] = tags;
    };
    auto withdraw = [&](const auto& prefix) {
      const PathKey key{
          toCidr(*prefix.prefix()),
          static_cast<uint32_t>(prefix.pathId().value_or(0))};
      receivedPaths_.erase(key);
      receivedTags_.erase(key);
      withdrawn_.insert(key);
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
      const std::string& community) {
    std::shared_ptr<nettools::bgplib::BgpUpdate2> update;
    for (uint32_t prefixIdx = 0; prefixIdx < kNumPrefixes; prefixIdx++) {
      auto single = createBgpUpdateAnnouncement(
          true /* isV4 */,
          prefixCidr(prefixIdx),
          nexthop,
          {65001} /* asPathSeq */,
          {community},
          pathIdx + 1 /* addPathId */,
          kLocalPref);
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

  void pushToSource(std::shared_ptr<nettools::bgplib::BgpUpdate2> update) {
    auto queues = getPeerQueues(sourcePeerId_);
    ASSERT_TRUE(queues.has_value());
    folly::coro::blockingWait(queues->adjRibInQ->push(std::move(update)));
  }

  const BgpPeerId sourcePeerId_{kPeerAddr3, kPeerAddr3.asV4().toLongHBO()};
  const BgpPeerId receiverPeerId_{kPeerAddr5, kPeerAddr5.asV4().toLongHBO()};
};

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

} // namespace facebook::bgp
