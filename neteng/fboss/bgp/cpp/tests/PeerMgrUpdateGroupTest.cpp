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

#define PeerManager_TEST_FRIENDS                                               \
  FRIEND_TEST(PeerManagerUpdateGroupTestFixture, UpdateGroupConstructionTest); \
  FRIEND_TEST(                                                                 \
      PeerManagerUpdateGroupTestFixture,                                       \
      NonDefaultVrfGroupInitialDumpOnAnnouncementDone);                        \
  FRIEND_TEST(                                                                 \
      PeerManagerUpdateGroupTestFixture,                                       \
      GetUpdateGroupInfoSurfacesGroupStats);                                   \
  FRIEND_TEST(                                                                 \
      PeerManagerUpdateGroupTestFixture, GetAdjRibStatsReturnsTypedSnapshots);

#define AdjRibOutGroup_TEST_FRIENDS          \
  FRIEND_TEST(                               \
      PeerManagerUpdateGroupTestFixture,     \
      GetUpdateGroupInfoSurfacesGroupStats); \
  FRIEND_TEST(                               \
      PeerManagerUpdateGroupTestFixture, GetAdjRibStatsReturnsTypedSnapshots);

#include <algorithm>
#include <vector>

#include <folly/coro/BlockingWait.h>
#include <folly/fibers/FiberManagerMap.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "neteng/fboss/bgp/cpp/tests/PeerManagerTestUtils.h"
#include "neteng/fboss/bgp/cpp/tests/Utils.h"

using facebook::neteng::fboss::bgp::thrift::TDirectionFilter;
using ::testing::_;
using ::testing::UnorderedElementsAre;
using namespace facebook::nettools::bgplib;

namespace facebook::bgp {

class PeerManagerUpdateGroupTestFixture : public PeerManagerTestFixture {
 public:
  void SetUp() override {
    PeerManagerTestFixture::SetUp();
    auto config = getConfig(
        true, /* includeStaticPeer */
        false, /* includeDynamicShivPeer */
        false, /* includeDynamicMonitorPeer */
        false, /* includeDynamicVipInjectorPeer */
        false, /* enableStatefulHa */
        false, /* enableVipServer */
        kDefaultEorTimeS, /* eorTimeS */
        false, /* enableSubscriberLimit */
        false, /* enableSwitchLimit */
        false, /* applyGoldenPrefixPolicy */
        {}, /* bgpFeatures */
        false, /* enableDynamicPolicyEvaluation */
        true /* enableUpdateGroup */);

    auto configManager = std::make_shared<ConfigManager>(config);
    peerMgr_ = std::make_shared<PeerManagerBase>(
        configManager, nullptr, ribInQ_, ribOutQ_, nbrRouteChangeQ_);

    auto versionNumber = std::make_shared<VersionNumber>(version_);
    auto mockInfo = mockInfo1_;
    // Set AFI to v4 so that AdjRibOut doesn't skip building announcements.
    mockInfo.negotiatedCapabilities.mpExtV4Unicast() = true;

    sessionInfo_ = FiberBgpPeer::getObservableSessionInfo(
        mockInfo,
        nullptr /* unbounded input queue, unused */,
        boundedAdjRibOutQ_,
        adjRibInQ_,
        versionNumber);
  }

  void cleanUp() {
    adjRibInQ_->fiberPush(
        FiberBgpPeer::BgpSessionStop{GracefulRestartFlag{false}});
    folly::coro::blockingWait(asyncScope_.cancelAndJoinAsync());
    peerMgr_->stop();
  }

  std::shared_ptr<PeerManagerBase> peerMgr_;
  folly::coro::CancellableAsyncScope asyncScope_;

  uint64_t version_ = 0x100;
  std::shared_ptr<FiberBgpPeer::ObservableSessionInfo> sessionInfo_;

  std::shared_ptr<AdjRib::AdjRibInQueueT> adjRibInQ_ =
      std::make_shared<AdjRib::AdjRibInQueueT>();
  std::shared_ptr<AdjRib::AdjRibOutQueueT> boundedAdjRibOutQ_ =
      std::make_shared<AdjRib::AdjRibOutQueueT>(
          kMaxEgressQueueSize,
          kEgressQueueHighWatermark,
          kEgressQueueLowWatermark);
};

/*
 * This test verifies the updateGroup creation when directly invoking
 * sessionEstablished() call from PeerManagerBase.
 */
TEST_F(PeerManagerUpdateGroupTestFixture, UpdateGroupConstructionTest) {
  auto& evb = peerMgr_->getEventBase();
  auto& fm = folly::fibers::getFiberManager(peerMgr_->getEventBase(), options_);

  FiberBgpPeer::ObservableStateT stateEvent{
      .peerId = kPeerId3,
      .versionNumber = version_,
      .remoteAs = mockInfo1_.peeringParams.remoteAs,
      .sessionInfo = sessionInfo_};

  peerMgr_->getDefaultVrf().setRibInitialAnnouncementStarted(true);
  peerMgr_->getDefaultVrf().setRibInitialAnnouncementDone(true);

  fm.addTask([&] {
    EXPECT_EQ(0, peerMgr_->getDefaultVrf().getPendingRibDumpReqs().size());
    folly::coro::blockingWait(peerMgr_->sessionEstablished(stateEvent));

    // Validation of update-group creation
    auto adjRib = peerMgr_->adjRibs_.at(kPeerId3);
    auto updateGroupKey = adjRib->getUpdateGroupKey();
    EXPECT_EQ(
        1, peerMgr_->updateGroupManager_->getGroupCount(fboss::kDefaultVrf));
    EXPECT_TRUE(peerMgr_->updateGroupManager_->hasGroup(updateGroupKey));
    auto adjRibOutGroup = peerMgr_->adjRibOutGroups_.begin()->second;
    auto updateGroup = peerMgr_->updateGroupManager_->findOrCreateGroup(
        updateGroupKey, peerMgr_->getDefaultVrf().getShadowRibView());
    EXPECT_NE(adjRibOutGroup, updateGroup);

    // trigger stop of task
    cleanUp();
  });
  evb.loop();
  SUCCEED();
}

/*
 * A peer in a VRF other than the default joins its update group before that
 * VRF's initial RIB announcement is done, so the group's initial dump is
 * deferred. markRibInitialAnnouncementDone for that VRF must schedule it.
 */
TEST_F(
    PeerManagerUpdateGroupTestFixture,
    NonDefaultVrfGroupInitialDumpOnAnnouncementDone) {
  auto& evb = peerMgr_->getEventBase();
  auto& fm = folly::fibers::getFiberManager(peerMgr_->getEventBase(), options_);

  const VrfId vrfId{5};
  auto& vrf = peerMgr_->createVrf(vrfId, /*publishStats=*/false);
  auto mockInfo = mockInfo1_;
  mockInfo.negotiatedCapabilities.mpExtV4Unicast() = true;
  mockInfo.peeringParams.vrfId = vrfId;
  auto versionNumber = std::make_shared<VersionNumber>(version_);
  auto sessionInfo = FiberBgpPeer::getObservableSessionInfo(
      mockInfo,
      nullptr /* unbounded input queue, unused */,
      boundedAdjRibOutQ_,
      adjRibInQ_,
      versionNumber);
  FiberBgpPeer::ObservableStateT stateEvent{
      .vrfId = vrfId,
      .peerId = kPeerId3,
      .versionNumber = version_,
      .remoteAs = mockInfo.peeringParams.remoteAs,
      .sessionInfo = sessionInfo};

  vrf.setRibInitialAnnouncementStarted(true);

  fm.addTask([&] {
    folly::coro::blockingWait(peerMgr_->sessionEstablished(stateEvent));

    auto adjRib = peerMgr_->adjRibs_.at(kPeerId3);
    EXPECT_EQ(1, peerMgr_->updateGroupManager_->getGroupCount(vrfId));
    auto updateGroup = peerMgr_->updateGroupManager_->findOrCreateGroup(
        adjRib->getUpdateGroupKey(), vrf.getShadowRibView());
    EXPECT_EQ(UpdateGroupState::UNINITIALIZED, updateGroup->getState());
    EXPECT_FALSE(updateGroup->isRibDumpScheduled());

    peerMgr_->markRibInitialAnnouncementDone(vrf);
    EXPECT_TRUE(vrf.isRibInitialAnnouncementDone());
    EXPECT_TRUE(updateGroup->isRibDumpScheduled());

    // trigger stop of task
    cleanUp();
  });
  evb.loop();
  SUCCEED();
}

/*
 * getUpdateGroupInfo() surfaces the group's OWN AdjRibStats (announcement /
 * withdrawal / UPDATE / EoR PDU counts) into TUpdateGroupStats. For in-sync
 * members the UPDATE is built once at the group, so the members' per-peer
 * counters stay 0 and the group's counters are the source of truth. This guards
 * the group-stats -> thrift wiring (previously the announce/withdraw fields
 * were summed from the zeroed per-peer counters and read 0).
 */
TEST_F(
    PeerManagerUpdateGroupTestFixture,
    GetUpdateGroupInfoSurfacesGroupStats) {
  auto& evb = peerMgr_->getEventBase();
  auto& fm = folly::fibers::getFiberManager(peerMgr_->getEventBase(), options_);

  FiberBgpPeer::ObservableStateT stateEvent{
      .peerId = kPeerId3,
      .versionNumber = version_,
      .remoteAs = mockInfo1_.peeringParams.remoteAs,
      .sessionInfo = sessionInfo_};

  peerMgr_->getDefaultVrf().setRibInitialAnnouncementStarted(true);
  peerMgr_->getDefaultVrf().setRibInitialAnnouncementDone(true);

  fm.addTask([&] {
    folly::coro::blockingWait(peerMgr_->sessionEstablished(stateEvent));

    auto adjRib = peerMgr_->adjRibs_.at(kPeerId3);
    auto updateGroupKey = adjRib->getUpdateGroupKey();
    // Read the group from the same manager getUpdateGroupInfo() iterates.
    auto group = peerMgr_->updateGroupManager_->findOrCreateGroup(
        updateGroupKey, peerMgr_->getDefaultVrf().getShadowRibView());
    ASSERT_NE(group, nullptr);

    /*
     * Inject known group-level counts directly (the send path would set these;
     * here we set them to keep the test focused on getUpdateGroupInfo wiring).
     */
    constexpr uint64_t kAnnV4 = 11;
    constexpr uint64_t kAnnV6 = 12;
    constexpr uint64_t kWithdrawals = 13;
    for (uint64_t i = 0; i < kAnnV4; ++i) {
      group->stats_.incrementSentAnnouncementsIpv4();
    }
    for (uint64_t i = 0; i < kAnnV6; ++i) {
      group->stats_.incrementSentAnnouncementsIpv6();
    }
    for (uint64_t i = 0; i < kWithdrawals; ++i) {
      group->stats_.incrementSentWithdrawals();
    }

    const auto groupId = static_cast<int64_t>(group->getGroupId());
    auto infos = peerMgr_->getUpdateGroupInfo(groupId);
    ASSERT_EQ(1, infos.size());
    const auto& stats = infos[0].stats().value();
    EXPECT_EQ(kAnnV4, stats.total_sent_announcement_msgs_ipv4().value());
    EXPECT_EQ(kAnnV6, stats.total_sent_announcement_msgs_ipv6().value());
    EXPECT_EQ(kWithdrawals, stats.total_sent_withdrawal_msgs().value());

    auto summaries = peerMgr_->getUpdateGroupSummaries();
    auto summary = std::find_if(
        summaries.begin(), summaries.end(), [&](const auto& candidate) {
          return candidate.group_id().value() == groupId;
        });
    ASSERT_NE(summary, summaries.end());
    EXPECT_EQ(
        updateGroupKey.egressPolicyName.value_or(""),
        summary->egress_policy_name().value());
    EXPECT_EQ(group->getMemberCount(), summary->member_count().value());
    EXPECT_EQ(
        group->getNumInSyncPeers(), summary->in_sync_peer_count().value());
    EXPECT_EQ(
        group->getDetachedPeers().size(),
        summary->detached_peer_count().value());
    EXPECT_EQ(
        group->getLastSeenRibVersion(),
        summary->last_seen_rib_version().value());
    EXPECT_EQ(
        group->getStats().getPostOutPrefixCount(),
        summary->post_out_prefix_count().value());

    cleanUp();
  });
  evb.loop();
  SUCCEED();
}

TEST_F(PeerManagerUpdateGroupTestFixture, GetAdjRibStatsReturnsTypedSnapshots) {
  constexpr size_t kExpectedPackingListSize = 1;
  constexpr uint64_t kGroupRibVersionAtDetach = 0x2000;
  constexpr uint64_t kAdvancedGroupRibVersion = 0x3000;
  constexpr uint32_t kSecondPathLocalPreference = 200;
  auto& evb = peerMgr_->getEventBase();
  auto& fm = folly::fibers::getFiberManager(peerMgr_->getEventBase(), options_);

  FiberBgpPeer::ObservableStateT stateEvent{
      .peerId = kPeerId3,
      .versionNumber = version_,
      .remoteAs = mockInfo1_.peeringParams.remoteAs,
      .sessionInfo = sessionInfo_};
  FiberBgpPeer::ObservableStateT secondStateEvent{
      .peerId = kPeerId4,
      .versionNumber = version_,
      .remoteAs = mockInfo1_.peeringParams.remoteAs,
      .sessionInfo = sessionInfo_};

  peerMgr_->getDefaultVrf().setRibInitialAnnouncementStarted(true);
  peerMgr_->getDefaultVrf().setRibInitialAnnouncementDone(true);

  fm.addTask([&] {
    folly::coro::blockingWait(peerMgr_->sessionEstablished(stateEvent));
    secondStateEvent.versionNumber = ++version_;
    sessionInfo_->currentVersion = std::make_shared<VersionNumber>(version_);
    folly::coro::blockingWait(peerMgr_->sessionEstablished(secondStateEvent));

    const auto adjRib = peerMgr_->adjRibs_.at(kPeerId3);
    const auto group = peerMgr_->updateGroupManager_->findOrCreateGroup(
        adjRib->getUpdateGroupKey(),
        peerMgr_->getDefaultVrf().getShadowRibView());
    ASSERT_NE(nullptr, group);

    auto attrs = std::make_shared<BgpPath>(BgpPathFields());
    auto* groupEntry = group->addToLiteTree(
        group->LiteTree_,
        kV4Prefix1,
        group->getGroupOwnerKey(),
        kDefaultPathID);
    groupEntry->setPreOut(attrs);
    groupEntry->setRibVersion(kGroupRibVersionAtDetach);
    group->stats_.incrementPreOutPrefixCount(/*isIpv4=*/true);
    group->tryUpdateAttrToPrefixMapForGroup(
        std::make_pair(kV4Prefix1, kDefaultPathID), nullptr, attrs);
    ASSERT_EQ(kExpectedPackingListSize, group->getAttrToPrefixMap().size());
    group->setLastSeenRibVersion(kGroupRibVersionAtDetach);
    group->transitionInitPeersToJoinedRunning();

    const auto response = peerMgr_->getAdjRibStats(TDirectionFilter::BOTH);
    constexpr size_t kExpectedPeerCount = 2;
    ASSERT_EQ(kExpectedPeerCount, response.rib_in()->peers()->size());
    ASSERT_EQ(kExpectedPeerCount, response.rib_out()->peers()->size());
    std::vector<std::string> peerAddresses;
    peerAddresses.reserve(response.rib_in()->peers()->size());
    for (const auto& peer : response.rib_in()->peers().value()) {
      peerAddresses.emplace_back(peer.peer_key()->peer_address().value());
    }
    EXPECT_THAT(
        peerAddresses,
        UnorderedElementsAre(kPeerId3.peerAddr.str(), kPeerId4.peerAddr.str()));

    const auto ribInPeerStats = std::find_if(
        response.rib_in()->peers()->begin(),
        response.rib_in()->peers()->end(),
        [](const auto& peer) {
          return peer.peer_key()->peer_address().value() ==
              kPeerId3.peerAddr.str();
        });
    ASSERT_NE(response.rib_in()->peers()->end(), ribInPeerStats);
    EXPECT_EQ(
        mockInfo1_.peeringParams.description,
        ribInPeerStats->peer_name().value());

    const auto peerStats = std::find_if(
        response.rib_out()->peers()->begin(),
        response.rib_out()->peers()->end(),
        [](const auto& peer) {
          return peer.peer_key()->peer_address().value() ==
              kPeerId3.peerAddr.str();
        });
    ASSERT_NE(response.rib_out()->peers()->end(), peerStats);
    const auto& peer = *peerStats;
    EXPECT_EQ(kPeerId3.peerAddr.str(), peer.peer_key()->peer_address().value());
    EXPECT_EQ(
        kPeerId3.remoteBgpId,
        static_cast<uint32_t>(peer.peer_key()->remote_bgp_id().value()));
    const auto& peerGroupKey = peer.group_key().value();
    EXPECT_EQ(
        group->getGroupKey().egressPolicyName.value_or(""),
        peerGroupKey.egress_policy_name().value());
    EXPECT_EQ(
        static_cast<int64_t>(group->getGroupId()),
        peerGroupKey.group_id().value());
    EXPECT_EQ(mockInfo1_.peeringParams.description, peer.peer_name().value());
    EXPECT_EQ(0, peer.active_prefixes().value());
    EXPECT_EQ(1, peer.active_paths().value());
    EXPECT_EQ(kExpectedPackingListSize, peer.packing_list_size().value());
    EXPECT_EQ(kGroupRibVersionAtDetach, adjRib->getLastSeenRibVersion());

    const auto bitPosition = adjRib->getGroupBitPosition();
    ASSERT_TRUE(group->isPeerInSync(bitPosition));
    ASSERT_EQ(PeerUpdateState::JOINED_RUNNING, adjRib->getPeerState());
    group->detachPeer(adjRib, AdjRibOutGroup::DetachReason::Policy);
    EXPECT_FALSE(group->isPeerInSync(bitPosition));
    EXPECT_TRUE(adjRib->isDetachedPeer());

    auto secondAttrs = std::make_shared<BgpPath>(BgpPathFields());
    secondAttrs->setLocalPref(kSecondPathLocalPreference);
    auto* secondGroupEntry = group->addToLiteTree(
        group->LiteTree_,
        kV4Prefix2,
        group->getGroupOwnerKey(),
        kDefaultPathID);
    secondGroupEntry->setPreOut(secondAttrs);
    secondGroupEntry->setRibVersion(kAdvancedGroupRibVersion);
    group->stats_.incrementPreOutPrefixCount(/*isIpv4=*/true);
    group->tryUpdateAttrToPrefixMapForGroup(
        std::make_pair(kV4Prefix2, kDefaultPathID), nullptr, secondAttrs);
    group->setLastSeenRibVersion(kAdvancedGroupRibVersion);
    EXPECT_EQ(2, group->getAttrToPrefixMap().size());

    const auto peerBackedSnapshot = adjRib->getRibOutStatsSnapshot();
    EXPECT_EQ(1, peerBackedSnapshot.active_paths().value());
    EXPECT_EQ(
        kExpectedPackingListSize,
        peerBackedSnapshot.packing_list_size().value());
    EXPECT_EQ(kGroupRibVersionAtDetach, adjRib->getLastSeenRibVersion());

    const auto ingressResponse =
        peerMgr_->getAdjRibStats(TDirectionFilter::INGRESS);
    EXPECT_EQ(kExpectedPeerCount, ingressResponse.rib_in()->peers()->size());
    EXPECT_TRUE(ingressResponse.rib_out()->peers()->empty());

    const auto egressResponse =
        peerMgr_->getAdjRibStats(TDirectionFilter::EGRESS);
    EXPECT_TRUE(egressResponse.rib_in()->peers()->empty());
    EXPECT_EQ(kExpectedPeerCount, egressResponse.rib_out()->peers()->size());

    cleanUp();
  });
  evb.loop();
  SUCCEED();
}

} // namespace facebook::bgp
