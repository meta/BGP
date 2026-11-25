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
#include <folly/coro/Task.h>
#include <folly/futures/Future.h>

#include "neteng/fboss/bgp/cpp/adjrib/AdjRib.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibGroup.h"
#include "neteng/fboss/bgp/cpp/adjrib/UpdateGroupManager.h"

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/adjrib/AdjRibStructs.h"
#include "neteng/fboss/bgp/cpp/common/Types.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"
#include "neteng/fboss/bgp/cpp/tests/PolicyUtils.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

#include <fb303/ThreadCachedServiceData.h>

#include <folly/Singleton.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <chrono>
#include <utility>

using facebook::neteng::fboss::bgp_attr::AdvertiseLinkBandwidth;
using facebook::neteng::fboss::bgp_attr::ReceiveLinkBandwidth;
using ::testing::IsNull;
using ::testing::NotNull;

namespace facebook::bgp {

/*
 * DO NOT add a singleton declaration here.
 *
 * This test now links against AdjRib.cpp which already contains the production
 * singleton declaration for AdjRibPolicyCache. Adding it here causes a double
 * registration error.
 *
 * The singleton is declared in fbcode/neteng/fboss/bgp/cpp/adjrib/AdjRib.cpp
 * as: folly::Singleton<AdjRibPolicyCache> adjRibPolicyCacheSingleton;
 *
 * If you see a merge conflict here, do not re-add the singleton declaration.
 */

namespace {
int64_t getNumUpdateGroupsCounter() {
  fb303::ThreadCachedServiceData::get()->publishStats();
  return fb303::ThreadCachedServiceData::getShared()->getCounter(
      BgpStats::kNumUpdateGroups);
}
} // namespace

class UpdateGroupManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    folly::SingletonVault::singleton()->registrationComplete();
    BgpStats::initCounters();
  }
};

UpdateGroupKey createTestKey(
    std::string policyName = "policy1",
    std::string routeFilterStmtName = "peer_group",
    std::chrono::seconds outDelay = std::chrono::seconds(30),
    BgpSessionType sessionType = BgpSessionType::IBGP,
    bool afiIpv4Negotiated = true,
    bool afiIpv6Negotiated = false,
    bool isConfedPeer = false,
    bool isRrClient = false,
    AdvertiseLinkBandwidth advertiseLinkBandwidth =
        AdvertiseLinkBandwidth::DISABLE,
    ReceiveLinkBandwidth receiveLinkBandwidth = ReceiveLinkBandwidth::DISABLE,
    uint64_t linkBandwidthBps = 0,
    bool removePrivateAsn = false,
    bool sendAddPath = false,
    bool as4ByteCapable = true,
    bool extNhEncodingCapable = false,
    bool legacyV4NlriEncoding = false,
    std::string peerGroupName = "",
    bool peerOverride = false,
    uint32_t localAs = 0,
    std::optional<uint32_t> asConfedId = std::nullopt) {
  return UpdateGroupKey::buildUpdateGroupKey(
      fboss::kDefaultVrf,
      std::move(policyName),
      std::move(routeFilterStmtName),
      outDelay,
      sessionType,
      afiIpv4Negotiated,
      afiIpv6Negotiated,
      isConfedPeer,
      isRrClient,
      advertiseLinkBandwidth,
      receiveLinkBandwidth,
      linkBandwidthBps,
      removePrivateAsn,
      sendAddPath,
      as4ByteCapable,
      extNhEncodingCapable,
      legacyV4NlriEncoding,
      std::move(peerGroupName),
      peerOverride,
      localAs,
      asConfedId);
}

TEST_F(UpdateGroupManagerTest, InitialStateEmpty) {
  folly::EventBase evb;
  UpdateGroupManager manager(evb, UpdateGroupConfig{});
  UpdateGroupKey key = createTestKey();

  EXPECT_EQ(manager.getGroupCount(fboss::kDefaultVrf), 0);
  EXPECT_FALSE(manager.hasGroup(key));
  EXPECT_EQ(manager.getGroup(key), nullptr);
}

TEST_F(UpdateGroupManagerTest, CreateAndFindGroup) {
  folly::EventBase evb;
  UpdateGroupManager manager(evb, UpdateGroupConfig{});
  auto key = createTestKey();

  /*
   * Initially no groups exist
   */
  EXPECT_EQ(0, manager.getGroupCount(fboss::kDefaultVrf));
  EXPECT_FALSE(manager.hasGroup(key));
  EXPECT_THAT(manager.getGroup(key), IsNull());
  EXPECT_EQ(0, getNumUpdateGroupsCounter());

  /*
   * findOrCreateGroup creates a new group
   */
  auto group1 = manager.findOrCreateGroup(key, ShadowRibView::empty());
  EXPECT_THAT(group1, NotNull());
  EXPECT_EQ(1, manager.getGroupCount(fboss::kDefaultVrf));
  EXPECT_TRUE(manager.hasGroup(key));
  EXPECT_EQ(1, getNumUpdateGroupsCounter());

  /*
   * findOrCreateGroup returns existing group for same key
   * Counter should not change
   */
  auto group2 = manager.findOrCreateGroup(key, ShadowRibView::empty());
  EXPECT_THAT(group2, NotNull());
  EXPECT_EQ(group1.get(), group2.get());
  EXPECT_EQ(1, manager.getGroupCount(fboss::kDefaultVrf));
  EXPECT_EQ(1, getNumUpdateGroupsCounter());
}

TEST_F(UpdateGroupManagerTest, MultipleGroups) {
  folly::EventBase evb;
  UpdateGroupManager manager(evb, UpdateGroupConfig{});

  auto key1 = createTestKey("policy1");
  auto key2 = createTestKey("policy2");

  /*
   * Create first group
   */
  auto group1 = manager.findOrCreateGroup(key1, ShadowRibView::empty());
  EXPECT_THAT(group1, NotNull());
  EXPECT_EQ(1, manager.getGroupCount(fboss::kDefaultVrf));
  EXPECT_TRUE(manager.hasGroup(key1));
  EXPECT_FALSE(manager.hasGroup(key2));

  /*
   * Create second group with different key
   */
  auto group2 = manager.findOrCreateGroup(key2, ShadowRibView::empty());
  EXPECT_THAT(group2, NotNull());
  EXPECT_EQ(2, manager.getGroupCount(fboss::kDefaultVrf));
  EXPECT_TRUE(manager.hasGroup(key1));
  EXPECT_TRUE(manager.hasGroup(key2));

  /*
   * Groups should be different
   */
  EXPECT_NE(group1.get(), group2.get());
}

/*
 * Peers identical except for their local AS must land in separate update
 * groups: the group producer runs the egress attribute transform once using
 * the first registered member's peering params, so a shared group would
 * prepend one member's local AS to the other member's AS_PATH.
 */
TEST_F(UpdateGroupManagerTest, DifferentLocalAsCreatesSeparateGroups) {
  folly::EventBase evb;
  UpdateGroupManager manager(evb, UpdateGroupConfig{});

  auto key1 = createTestKey();
  key1.localAs = 65001;

  auto key2 = key1;
  key2.localAs = 65002;

  auto group1 = manager.findOrCreateGroup(key1, ShadowRibView::empty());
  auto group2 = manager.findOrCreateGroup(key2, ShadowRibView::empty());

  EXPECT_THAT(group1, NotNull());
  EXPECT_THAT(group2, NotNull());
  EXPECT_NE(group1.get(), group2.get());
  EXPECT_EQ(2, manager.getGroupCount(fboss::kDefaultVrf));
}

TEST_F(UpdateGroupManagerTest, DifferentAsConfedIdCreatesSeparateGroups) {
  folly::EventBase evb;
  UpdateGroupManager manager(evb, UpdateGroupConfig{});

  auto key1 = createTestKey();
  key1.asConfedId = 65001;

  auto key2 = key1;
  key2.asConfedId = 65002;

  auto group1 = manager.findOrCreateGroup(key1, ShadowRibView::empty());
  auto group2 = manager.findOrCreateGroup(key2, ShadowRibView::empty());

  EXPECT_THAT(group1, NotNull());
  EXPECT_THAT(group2, NotNull());
  EXPECT_NE(group1.get(), group2.get());
  EXPECT_EQ(2, manager.getGroupCount(fboss::kDefaultVrf));
}

TEST_F(UpdateGroupManagerTest, GroupNameGeneration) {
  folly::EventBase evb;
  UpdateGroupManager manager(evb, UpdateGroupConfig{});

  auto key1 = createTestKey("policy1");
  auto key2 = createTestKey("policy2");

  /*
   * Create groups and verify they have unique names
   */
  auto group1 = manager.findOrCreateGroup(key1, ShadowRibView::empty());
  auto group2 = manager.findOrCreateGroup(key2, ShadowRibView::empty());

  EXPECT_THAT(group1, NotNull());
  EXPECT_THAT(group2, NotNull());

  auto name1 = group1->getAdjRibGroupName();
  auto name2 = group2->getAdjRibGroupName();

  /*
   * Names should be non-empty and different
   */
  EXPECT_FALSE(name1.empty());
  EXPECT_FALSE(name2.empty());
  EXPECT_NE(name1, name2);
}

TEST_F(UpdateGroupManagerTest, MaybeDestroyUpdateGroups) {
  folly::EventBase evb;
  UpdateGroupManager manager(evb, UpdateGroupConfig{});
  auto key = createTestKey();

  /*
   * An empty group (no members) is destroyed and erased from the manager.
   */
  auto group = manager.findOrCreateGroup(key, ShadowRibView::empty());
  EXPECT_EQ(1, manager.getGroupCount(fboss::kDefaultVrf));
  folly::coro::blockingWait(manager.maybeDestroyUpdateGroups({group}));
  EXPECT_EQ(0, manager.getGroupCount(fboss::kDefaultVrf));

  /*
   * Passing an already-destroyed group (and an empty list) is a safe no-op.
   */
  folly::coro::blockingWait(manager.maybeDestroyUpdateGroups({group}));
  folly::coro::blockingWait(manager.maybeDestroyUpdateGroups({}));
  EXPECT_EQ(0, manager.getGroupCount(fboss::kDefaultVrf));
}

TEST_F(UpdateGroupManagerTest, MaybeDestroyUpdateGroupsDestroysAllEmptyGroups) {
  folly::EventBase evb;
  UpdateGroupManager manager(evb, UpdateGroupConfig{});

  auto group1 = manager.findOrCreateGroup(
      createTestKey("policy1"), ShadowRibView::empty());
  auto group2 = manager.findOrCreateGroup(
      createTestKey("policy2"), ShadowRibView::empty());
  auto group3 = manager.findOrCreateGroup(
      createTestKey("policy3"), ShadowRibView::empty());
  EXPECT_EQ(3, manager.getGroupCount(fboss::kDefaultVrf));

  // All three groups are empty, so passing them together destroys every one.
  folly::coro::blockingWait(
      manager.maybeDestroyUpdateGroups({group1, group2, group3}));
  EXPECT_EQ(0, manager.getGroupCount(fboss::kDefaultVrf));
}

/*
 * Keys that differ only in their VRF create two groups, each stored and found
 * under its own VRF.
 */
TEST_F(UpdateGroupManagerTest, SameKeyInTwoVrfsCreatesSeparateGroups) {
  folly::EventBase evb;
  UpdateGroupManager manager(evb, UpdateGroupConfig{});

  auto key = createTestKey("policy1");
  auto otherVrfKey = key;
  otherVrfKey.vrfId = fboss::kQuarantineVrfId;
  EXPECT_FALSE(key == otherVrfKey);
  EXPECT_TRUE(UpdateGroupKey::toString(otherVrfKey).ends_with("-1"));

  auto defaultVrfGroup = manager.findOrCreateGroup(key, ShadowRibView::empty());
  auto otherVrfGroup =
      manager.findOrCreateGroup(otherVrfKey, ShadowRibView::empty());

  EXPECT_NE(defaultVrfGroup.get(), otherVrfGroup.get());
  EXPECT_EQ(1, manager.getGroupCount(fboss::kDefaultVrf));
  EXPECT_EQ(1, manager.getGroupCount(fboss::kQuarantineVrfId));
  EXPECT_EQ(defaultVrfGroup, manager.getGroup(key));
  EXPECT_EQ(otherVrfGroup, manager.getGroup(otherVrfKey));
  EXPECT_EQ(fboss::kDefaultVrf, defaultVrfGroup->getVrfId());
  EXPECT_EQ(fboss::kQuarantineVrfId, otherVrfGroup->getVrfId());
  EXPECT_THAT(
      otherVrfGroup->getGroupDescriptor(), testing::HasSubstr(",vrf=1)"));
  EXPECT_EQ(2, getNumUpdateGroupsCounter());
}

// One maybeDestroyUpdateGroups call destroys empty groups from several VRFs.
TEST_F(UpdateGroupManagerTest, MaybeDestroyUpdateGroupsAcrossVrfs) {
  folly::EventBase evb;
  UpdateGroupManager manager(evb, UpdateGroupConfig{});

  auto key = createTestKey("policy1");
  auto otherVrfKey = key;
  otherVrfKey.vrfId = fboss::kQuarantineVrfId;
  auto defaultVrfGroup = manager.findOrCreateGroup(key, ShadowRibView::empty());
  auto otherVrfGroup =
      manager.findOrCreateGroup(otherVrfKey, ShadowRibView::empty());

  folly::coro::blockingWait(
      manager.maybeDestroyUpdateGroups({defaultVrfGroup, otherVrfGroup}));
  EXPECT_EQ(0, manager.getGroupCount(fboss::kDefaultVrf));
  EXPECT_EQ(0, manager.getGroupCount(fboss::kQuarantineVrfId));
  EXPECT_EQ(0, getNumUpdateGroupsCounter());
}

TEST_F(UpdateGroupManagerTest, MaybeDestroyUpdateGroupsSkipsNonEmptyGroups) {
  folly::EventBase evb;
  UpdateGroupManager manager(evb, UpdateGroupConfig{});

  auto emptyKey1 = createTestKey("policy1");
  auto nonEmptyKey = createTestKey("policy2");
  auto emptyKey2 = createTestKey("policy3");
  auto emptyGroup1 =
      manager.findOrCreateGroup(emptyKey1, ShadowRibView::empty());
  auto nonEmptyGroup =
      manager.findOrCreateGroup(nonEmptyKey, ShadowRibView::empty());
  auto emptyGroup2 =
      manager.findOrCreateGroup(emptyKey2, ShadowRibView::empty());
  EXPECT_EQ(3, manager.getGroupCount(fboss::kDefaultVrf));

  /*
   * Register a peer into the middle group so it has a member; only the two
   * empty groups should be destroyed when all three are passed together.
   */
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ{
      nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<AdjRib::ObservableMessageT> observerQ;
  auto peerId = nettools::bgplib::BgpPeerId(
      folly::IPAddress("10.0.0.1"),
      folly::IPAddressV4("255.0.0.1").toLongHBO());
  auto adjRib = std::make_shared<AdjRib>(
      peerId,
      PeeringParams(fboss::kDefaultVrf),
      evb,
      ribInQ,
      observerQ,
      std::make_shared<folly::coro::Baton>(),
      nullptr,
      std::make_shared<std::atomic<bool>>(false));
  nonEmptyGroup->registerPeer(adjRib);

  folly::coro::blockingWait(manager.maybeDestroyUpdateGroups(
      {emptyGroup1, nonEmptyGroup, emptyGroup2}));

  // Only the non-empty group survives.
  EXPECT_EQ(1, manager.getGroupCount(fboss::kDefaultVrf));
  EXPECT_TRUE(manager.hasGroup(nonEmptyKey));
  EXPECT_FALSE(manager.hasGroup(emptyKey1));
  EXPECT_FALSE(manager.hasGroup(emptyKey2));
}

TEST_F(UpdateGroupManagerTest, MaybeDestroyUpdateGroupsFreesPinnedRibOut) {
  folly::EventBase evb;
  UpdateGroupManager manager(evb, UpdateGroupConfig{});

  auto key = createTestKey();
  auto group = manager.findOrCreateGroup(key, ShadowRibView::empty());

  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ{
      nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<AdjRib::ObservableMessageT> observerQ;
  auto peerId = nettools::bgplib::BgpPeerId(
      folly::IPAddress("10.0.0.1"),
      folly::IPAddressV4("255.0.0.1").toLongHBO());
  auto adjRib = std::make_shared<AdjRib>(
      peerId,
      PeeringParams(fboss::kDefaultVrf),
      evb,
      ribInQ,
      observerQ,
      std::make_shared<folly::coro::Baton>(),
      nullptr,
      std::make_shared<std::atomic<bool>>(false));

  adjRib->setUpdateGroup(group);
  group->registerPeer(adjRib);

  // Materialize a group-owned RIB-OUT entry.
  const auto kV4Prefix = folly::IPAddress::createNetwork("10.1.0.0/24");
  group->addToLiteTree(
      group->LiteTree_, kV4Prefix, group->getGroupOwnerKey(), kDefaultPathID);
  ASSERT_EQ(1, group->LiteTree_.size());

  /*
   * Session goes down without the peer being deleted, so the AdjRib survives.
   * Unregistering must drop its reference to the group, or the departed last
   * member would pin the emptied group -- and its whole RIB-OUT -- until it
   * re-establishes.
   */
  std::weak_ptr<AdjRibOutGroup> weakGroup = group;
  group->unregisterPeer(adjRib);
  folly::coro::blockingWait(manager.maybeDestroyUpdateGroups({group}));

  EXPECT_EQ(0, manager.getGroupCount(fboss::kDefaultVrf));
  EXPECT_EQ(nullptr, adjRib->getUpdateGroup());

  /*
   * The down peer still answers the CLI/thrift reads that walk every AdjRib in
   * adjRibs_ regardless of session state, and reports an empty RIB-OUT.
   */
  EXPECT_TRUE(adjRib->getAllPrefixes().empty());
  EXPECT_EQ(nullptr, adjRib->getRibEntry(/*ingress=*/false, kV4Prefix));
  EXPECT_EQ("", adjRib->getAdjRibOutGroupName());

  /*
   * Nothing keeps the emptied group alive once this scope's own reference
   * goes: the retained AdjRib is not an owner, so the RIB-OUT is reclaimed.
   */
  group.reset();
  EXPECT_TRUE(weakGroup.expired());
}

/*
 * A group going down must not leave a rib dump behind. maybeDestroyUpdateGroups
 * cancels the queued dump while it erases the group, before it suspends to
 * drain the group's async scope -- an uncancelled dump walks the ShadowRib and
 * registers a change list consumer on a group the manager has already erased,
 * and that consumer has no consume timer, pins withdrawn ShadowRib entries, and
 * forms a group-consumer ownership cycle that aborts teardown.
 */
TEST_F(
    UpdateGroupManagerTest,
    MaybeDestroyUpdateGroupsCancelsScheduledRibDump) {
  folly::EventBase evb;
  UpdateGroupManager manager(evb, UpdateGroupConfig{});

  auto key = createTestKey();
  auto group = manager.findOrCreateGroup(key, ShadowRibView::empty());

  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ{
      nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<AdjRib::ObservableMessageT> observerQ;
  auto peerId = nettools::bgplib::BgpPeerId(
      folly::IPAddress("10.0.0.1"),
      folly::IPAddressV4("255.0.0.1").toLongHBO());
  auto adjRib = std::make_shared<AdjRib>(
      peerId,
      PeeringParams(fboss::kDefaultVrf),
      evb,
      ribInQ,
      observerQ,
      std::make_shared<folly::coro::Baton>(),
      nullptr,
      std::make_shared<std::atomic<bool>>(false));
  group->registerPeer(adjRib);

  /*
   * Reproduce the production ordering: the dump is scheduled, the last peer
   * leaves, and the group is destroyed all within one event-loop turn, so the
   * queued dump task has not had its turn when the group is erased.
   */
  auto emptyAndDestroy = [&]() -> folly::coro::Task<void> {
    group->scheduleInitialDump();
    EXPECT_TRUE(group->isRibDumpScheduled());
    group->unregisterPeer(adjRib);
    EXPECT_EQ(0, group->getMemberCount());
    co_await manager.maybeDestroyUpdateGroups({group});
  };
  auto destroyed =
      folly::coro::co_withExecutor(&evb, emptyAndDestroy()).start();
  evb.loop();
  ASSERT_TRUE(destroyed.isReady());
  std::move(destroyed).get();

  EXPECT_EQ(0, manager.getGroupCount(fboss::kDefaultVrf));
  EXPECT_FALSE(group->isRibDumpScheduled());

  // The cancelled dump never walked: no state advance, no resurrected consumer.
  EXPECT_EQ(UpdateGroupState::UNINITIALIZED, group->getState());
  EXPECT_THAT(group->getChangeListConsumer(), IsNull());
}

/*
 * Only the erased groups lose their dump. A group passed in that still has
 * members is skipped, and its scheduled dump must survive: a cancelled dump
 * leaves state_ at UNINITIALIZED, so cancelling it here would strand the group
 * with no dump at all.
 */
TEST_F(
    UpdateGroupManagerTest,
    MaybeDestroyUpdateGroupsKeepsSurvivingGroupRibDump) {
  folly::EventBase evb;
  UpdateGroupManager manager(evb, UpdateGroupConfig{});

  auto emptyKey = createTestKey("policy1");
  auto nonEmptyKey = createTestKey("policy2");
  auto emptyGroup = manager.findOrCreateGroup(emptyKey, ShadowRibView::empty());
  auto nonEmptyGroup =
      manager.findOrCreateGroup(nonEmptyKey, ShadowRibView::empty());

  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ{
      nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<AdjRib::ObservableMessageT> observerQ;
  auto peerId = nettools::bgplib::BgpPeerId(
      folly::IPAddress("10.0.0.1"),
      folly::IPAddressV4("255.0.0.1").toLongHBO());
  auto adjRib = std::make_shared<AdjRib>(
      peerId,
      PeeringParams(fboss::kDefaultVrf),
      evb,
      ribInQ,
      observerQ,
      std::make_shared<folly::coro::Baton>(),
      nullptr,
      std::make_shared<std::atomic<bool>>(false));
  nonEmptyGroup->registerPeer(adjRib);
  nonEmptyGroup->scheduleInitialDump();
  ASSERT_TRUE(nonEmptyGroup->isRibDumpScheduled());

  /*
   * The empty group has nothing queued on its async scope, so its drain
   * completes inline and the survivor's dump is still queued -- not run, not
   * cancelled -- when this returns.
   */
  folly::coro::blockingWait(
      manager.maybeDestroyUpdateGroups({emptyGroup, nonEmptyGroup}));

  EXPECT_EQ(1, manager.getGroupCount(fboss::kDefaultVrf));
  EXPECT_TRUE(manager.hasGroup(nonEmptyKey));
  EXPECT_TRUE(nonEmptyGroup->isRibDumpScheduled());

  // Run the surviving dump so the group's async scope is idle at destruction.
  evb.loop();
  EXPECT_FALSE(nonEmptyGroup->isRibDumpScheduled());
  EXPECT_NE(UpdateGroupState::UNINITIALIZED, nonEmptyGroup->getState());

  /*
   * The survivor was never handed to maybeDestroyUpdateGroups' drain, so join
   * its async scope here -- AsyncScope requires it before destruction once a
   * task has started on it.
   */
  folly::coro::blockingWait(nonEmptyGroup->drainAsyncScope());
}

TEST_F(UpdateGroupManagerTest, SetUpdateGroupState) {
  folly::EventBase evb;
  UpdateGroupManager manager(evb, UpdateGroupConfig{});
  auto key = createTestKey();

  /*
   * Calling setUpdateGroupState on non-existent group should not crash
   */
  manager.setUpdateGroupState(key, UpdateGroupState::UNINITIALIZED);
  EXPECT_EQ(0, manager.getGroupCount(fboss::kDefaultVrf));
}

TEST_F(UpdateGroupManagerTest, PolicyManagerPassedToConstructor) {
  folly::EventBase evb;

  /*
   * Test that UpdateGroupManager can be created with nullptr policyManager
   * This is the common case when policyManager is not needed
   */
  UpdateGroupManager manager1(evb, UpdateGroupConfig{}, nullptr);
  EXPECT_EQ(0, manager1.getGroupCount(fboss::kDefaultVrf));

  /*
   * Test that UpdateGroupManager can be created with explicit nullptr
   * for all optional parameters
   */
  UpdateGroupManager manager2(
      evb,
      UpdateGroupConfig{},
      nullptr); // policyManager
  EXPECT_EQ(0, manager2.getGroupCount(fboss::kDefaultVrf));
}

TEST_F(
    UpdateGroupManagerTest,
    GetPostPolicyAttributesPolicyTermAndInfoWithPolicyManager) {
  folly::EventBase evb;

  /*
   * Create a policy manager with a simple "match all, set MED" policy
   */
  const std::string policyName = "test_egress_policy";
  auto policyManager = setupMatchAllSetMedPolicy(policyName);
  ASSERT_NE(nullptr, policyManager);

  /*
   * Create UpdateGroupManager with the policyManager
   */
  UpdateGroupManager manager(evb, UpdateGroupConfig{}, policyManager);

  /*
   * Create an update group with egress policy configured
   * This verifies that UpdateGroupManager successfully passes the
   * policyManager to the AdjRibOutGroup during construction
   */
  auto key = createTestKey(
      policyName, // egressPolicyName
      "peer_group", // routeFilterStmtName
      std::chrono::seconds(30), // outDelay
      BgpSessionType::IBGP,
      true, // afiIpv4Negotiated
      false, // afiIpv6Negotiated
      false, // isConfedPeer
      false, // isRrClient
      AdvertiseLinkBandwidth::DISABLE,
      ReceiveLinkBandwidth::DISABLE,
      0, // linkBandwidthBps
      false, // removePrivateAsn
      false, // sendAddPath
      true, // as4ByteCapable
      false); // extNhEncodingCapable

  auto group = manager.findOrCreateGroup(key, ShadowRibView::empty());
  ASSERT_NE(nullptr, group);
  EXPECT_EQ(1, manager.getGroupCount(fboss::kDefaultVrf));

  /*
   * Verify the group was created successfully and has a valid name
   * This confirms that UpdateGroupManager correctly initializes groups
   * with the policyManager, allowing the group to function properly
   */
  EXPECT_FALSE(group->getAdjRibGroupName().empty());
  EXPECT_EQ(0, group->getGroupId());
}

TEST_F(UpdateGroupManagerTest, ToThriftConvertsAllFields) {
  auto key = createTestKey(
      "egress_policy",
      "route_filter",
      std::chrono::seconds(10),
      BgpSessionType::EBGP,
      /*afiIpv4Negotiated=*/true,
      /*afiIpv6Negotiated=*/true,
      /*isConfedPeer=*/false,
      /*isRrClient=*/true,
      AdvertiseLinkBandwidth::BEST_PATH,
      ReceiveLinkBandwidth::DISABLE,
      /*linkBandwidthBps=*/100000,
      /*removePrivateAsn=*/true,
      /*sendAddPath=*/false,
      /*as4ByteCapable=*/true,
      /*extNhEncodingCapable=*/true,
      /*legacyV4NlriEncoding=*/true,
      "spine_peers",
      /*peerOverride=*/true,
      /*localAs=*/65001,
      /*asConfedId=*/65000);

  auto thriftKey = key.toThrift();

  EXPECT_EQ("egress_policy", thriftKey.egress_policy_name().value());
  EXPECT_EQ("route_filter", thriftKey.route_filter_stmt_name().value());
  EXPECT_EQ(10, thriftKey.out_delay_seconds().value());
  EXPECT_EQ("EBGP", thriftKey.session_type().value());
  EXPECT_TRUE(thriftKey.afi_ipv4_negotiated().value());
  EXPECT_TRUE(thriftKey.afi_ipv6_negotiated().value());
  EXPECT_FALSE(thriftKey.is_confed_peer().value());
  EXPECT_TRUE(thriftKey.is_rr_client().value());
  EXPECT_EQ(
      AdvertiseLinkBandwidth::BEST_PATH,
      thriftKey.advertise_link_bandwidth().value());
  EXPECT_EQ(
      ReceiveLinkBandwidth::DISABLE,
      thriftKey.receive_link_bandwidth().value());
  EXPECT_EQ(100000, thriftKey.link_bandwidth_bps().value());
  EXPECT_TRUE(thriftKey.remove_private_asn().value());
  EXPECT_FALSE(thriftKey.send_add_path().value());
  EXPECT_TRUE(thriftKey.as4_byte_capable().value());
  EXPECT_TRUE(thriftKey.ext_nh_encoding_capable().value());
  EXPECT_TRUE(thriftKey.legacy_v4_nlri_encoding().value());
  EXPECT_EQ("spine_peers", thriftKey.peer_group_name().value());
  EXPECT_EQ(65001, thriftKey.local_as().value());
  EXPECT_EQ(65000, thriftKey.as_confed_id().value());
  EXPECT_TRUE(thriftKey.peer_override().value());
}

TEST_F(UpdateGroupManagerTest, ToThriftOmitsUnsetOptionals) {
  auto key = createTestKey();
  key.advertiseLinkBandwidth = std::nullopt;
  key.receiveLinkBandwidth = std::nullopt;

  auto thriftKey = key.toThrift();

  EXPECT_FALSE(thriftKey.advertise_link_bandwidth().has_value());
  EXPECT_FALSE(thriftKey.receive_link_bandwidth().has_value());
  EXPECT_FALSE(thriftKey.link_bandwidth_bps().has_value());
  EXPECT_FALSE(thriftKey.as_confed_id().has_value());
}

TEST_F(UpdateGroupManagerTest, InitialDumpTimestampAndDiscrepancies) {
  folly::EventBase evb;
  UpdateGroupManager manager(evb, UpdateGroupConfig{});
  auto key = createTestKey();
  auto group = manager.findOrCreateGroup(key, ShadowRibView::empty());

  EXPECT_FALSE(group->getInitialDumpCompletionTimeMs().has_value());
  EXPECT_EQ(0, group->getTotalDiscrepancies());

  group->incrTotalDiscrepancies();
  group->incrTotalDiscrepancies();
  EXPECT_EQ(2, group->getTotalDiscrepancies());
}

TEST_F(UpdateGroupManagerTest, RekeyGroupUpdatesMapKey) {
  folly::EventBase evb;
  UpdateGroupConfig config;
  UpdateGroupManager manager(evb, config);

  auto oldKey = createTestKey("OLD_POLICY");
  auto group = manager.findOrCreateGroup(oldKey, ShadowRibView::empty());
  ASSERT_NE(group, nullptr);
  EXPECT_TRUE(manager.hasGroup(oldKey));

  // Register a peer and change its egress policy name
  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ{
      nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<AdjRib::ObservableMessageT> observerQ;
  auto peerId = nettools::bgplib::BgpPeerId(
      folly::IPAddress("10.0.0.1"),
      folly::IPAddressV4("255.0.0.1").toLongHBO());
  auto adjRib = std::make_shared<AdjRib>(
      peerId,
      PeeringParams(fboss::kDefaultVrf),
      evb,
      ribInQ,
      observerQ,
      std::make_shared<folly::coro::Baton>(),
      nullptr,
      std::make_shared<std::atomic<bool>>(false));
  group->registerPeer(adjRib);

  // Change egress policy on the peer
  folly::F14FastMap<bgp_policy::DIRECTION, std::optional<std::string>>
      newPolicy;
  newPolicy[bgp_policy::DIRECTION::OUT] = "NEW_POLICY";
  adjRib->updateIngressEgressPolicyNames(newPolicy);

  // Rekey — caller rebuilds the member key, then passes the new key
  adjRib->buildAndSetUpdateGroupKey();
  manager.rekeyGroup(group, adjRib->getUpdateGroupKey());

  // Old key should be gone, new key should exist
  EXPECT_FALSE(manager.hasGroup(oldKey));
  auto newKey = group->getGroupKey();
  EXPECT_EQ(newKey.egressPolicyName, "NEW_POLICY");
  EXPECT_TRUE(manager.hasGroup(newKey));
  EXPECT_EQ(manager.getGroup(newKey), group);
  EXPECT_EQ(manager.getGroupCount(fboss::kDefaultVrf), 1);

  // Verify adjRib's key was updated by rekeyGroup
  EXPECT_EQ(adjRib->getUpdateGroupKey().egressPolicyName, "NEW_POLICY");
}

TEST_F(UpdateGroupManagerTest, RekeyGroupNoOpWhenKeyUnchanged) {
  folly::EventBase evb;
  UpdateGroupConfig config;
  UpdateGroupManager manager(evb, config);

  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ{
      nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<AdjRib::ObservableMessageT> observerQ;
  auto peerId = nettools::bgplib::BgpPeerId(
      folly::IPAddress("10.0.0.1"),
      folly::IPAddressV4("255.0.0.1").toLongHBO());
  auto adjRib = std::make_shared<AdjRib>(
      peerId,
      PeeringParams(fboss::kDefaultVrf),
      evb,
      ribInQ,
      observerQ,
      std::make_shared<folly::coro::Baton>(),
      nullptr,
      std::make_shared<std::atomic<bool>>(false));

  // Build the key from the peer first, then create the group with that key
  adjRib->buildAndSetUpdateGroupKey();
  auto key = adjRib->getUpdateGroupKey();
  auto group = manager.findOrCreateGroup(key, ShadowRibView::empty());
  group->registerPeer(adjRib);

  // Rekey without changing the policy — should be a no-op
  manager.rekeyGroup(group, key);

  EXPECT_TRUE(manager.hasGroup(key));
  EXPECT_EQ(manager.getGroupCount(fboss::kDefaultVrf), 1);
}

TEST_F(UpdateGroupManagerTest, RekeyGroupUpdatesAllPeerKeys) {
  folly::EventBase evb;
  UpdateGroupConfig config;
  UpdateGroupManager manager(evb, config);

  nettools::bgplib::MonitoredBackPressuredQueue<RibInMessage> ribInQ{
      nettools::bgplib::kMaxIngressQueueSize};
  MonitoredMPMCQueue<AdjRib::ObservableMessageT> observerQ;

  auto peerId1 = nettools::bgplib::BgpPeerId(
      folly::IPAddress("10.0.0.1"),
      folly::IPAddressV4("255.0.0.1").toLongHBO());
  auto adjRib1 = std::make_shared<AdjRib>(
      peerId1,
      PeeringParams(fboss::kDefaultVrf),
      evb,
      ribInQ,
      observerQ,
      std::make_shared<folly::coro::Baton>(),
      nullptr,
      std::make_shared<std::atomic<bool>>(false));

  auto peerId2 = nettools::bgplib::BgpPeerId(
      folly::IPAddress("10.0.0.2"),
      folly::IPAddressV4("255.0.0.2").toLongHBO());
  auto adjRib2 = std::make_shared<AdjRib>(
      peerId2,
      PeeringParams(fboss::kDefaultVrf),
      evb,
      ribInQ,
      observerQ,
      std::make_shared<folly::coro::Baton>(),
      nullptr,
      std::make_shared<std::atomic<bool>>(false));

  auto group =
      manager.findOrCreateGroup(UpdateGroupKey{}, ShadowRibView::empty());
  group->registerPeer(adjRib1);
  group->registerPeer(adjRib2);

  // Change egress policy on both peers
  folly::F14FastMap<bgp_policy::DIRECTION, std::optional<std::string>>
      newPolicy;
  newPolicy[bgp_policy::DIRECTION::OUT] = "MULTI_PEER_POLICY";
  adjRib1->updateIngressEgressPolicyNames(newPolicy);
  adjRib2->updateIngressEgressPolicyNames(newPolicy);

  // Caller rebuilds both members' keys, then passes the new key.
  adjRib1->buildAndSetUpdateGroupKey();
  adjRib2->buildAndSetUpdateGroupKey();
  manager.rekeyGroup(group, adjRib1->getUpdateGroupKey());

  // Both peers should have their keys rebuilt
  EXPECT_EQ(adjRib1->getUpdateGroupKey().egressPolicyName, "MULTI_PEER_POLICY");
  EXPECT_EQ(adjRib2->getUpdateGroupKey().egressPolicyName, "MULTI_PEER_POLICY");

  // Group key should match
  EXPECT_EQ(group->getGroupKey().egressPolicyName, "MULTI_PEER_POLICY");
}

} // namespace facebook::bgp
