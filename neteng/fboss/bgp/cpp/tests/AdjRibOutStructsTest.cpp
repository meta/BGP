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

#include "neteng/fboss/bgp/cpp/adjrib/AdjRibStructs.h"
#include "neteng/fboss/bgp/cpp/common/Types.h"

#include <gtest/gtest.h>
#include <chrono>

using namespace facebook::neteng::fboss;
using namespace facebook::nettools::bgplib;

namespace facebook::bgp {

TEST(UpdateGroupKeyTest, EqualityOperatorDefault) {
  UpdateGroupKey key1;
  UpdateGroupKey key2;

  EXPECT_TRUE(key1 == key2);
}

TEST(UpdateGroupKeyTest, InequalityDifferentPolicy) {
  UpdateGroupKey key1;
  key1.egressPolicyName = "policy1";

  UpdateGroupKey key2 = key1;
  key2.egressPolicyName = "policy2";

  EXPECT_FALSE(key1 == key2);
}

TEST(UpdateGroupKeyTest, InequalityDifferentOutDelay) {
  UpdateGroupKey key1;
  key1.outDelay = std::chrono::seconds(30);

  UpdateGroupKey key2 = key1;
  key2.outDelay = std::chrono::seconds(60);

  EXPECT_FALSE(key1 == key2);
}

TEST(UpdateGroupKeyTest, InequalityDifferentSessionType) {
  UpdateGroupKey key1;
  key1.sessionType = BgpSessionType::IBGP;

  UpdateGroupKey key2 = key1;
  key2.sessionType = BgpSessionType::EBGP;

  EXPECT_FALSE(key1 == key2);
}

TEST(UpdateGroupKeyTest, InequalityDifferentAfiIpv4) {
  UpdateGroupKey key1;
  key1.afiIpv4Negotiated = true;

  UpdateGroupKey key2 = key1;
  key2.afiIpv4Negotiated = false;

  EXPECT_FALSE(key1 == key2);
}

TEST(UpdateGroupKeyTest, InequalityDifferentAfiIpv6) {
  UpdateGroupKey key1;
  key1.afiIpv6Negotiated = false;

  UpdateGroupKey key2 = key1;
  key2.afiIpv6Negotiated = true;

  EXPECT_FALSE(key1 == key2);
}

TEST(UpdateGroupKeyTest, InequalityDifferentConfedPeer) {
  UpdateGroupKey key1;
  key1.isConfedPeer = false;

  UpdateGroupKey key2 = key1;
  key2.isConfedPeer = true;

  EXPECT_FALSE(key1 == key2);
}

TEST(UpdateGroupKeyTest, InequalityDifferentRrClient) {
  UpdateGroupKey key1;
  key1.isRrClient = false;

  UpdateGroupKey key2 = key1;
  key2.isRrClient = true;

  EXPECT_FALSE(key1 == key2);
}

TEST(UpdateGroupKeyTest, InequalityDifferentLinkBandwidth) {
  UpdateGroupKey key1;
  key1.advertiseLinkBandwidth = bgp_attr::AdvertiseLinkBandwidth::DISABLE;
  key1.receiveLinkBandwidth = bgp_attr::ReceiveLinkBandwidth::DISABLE;
  key1.linkBandwidthBps = 1000000;

  UpdateGroupKey key2 = key1;
  key2.advertiseLinkBandwidth = bgp_attr::AdvertiseLinkBandwidth::SET_LINK_BPS;

  UpdateGroupKey key3 = key1;
  key3.receiveLinkBandwidth = bgp_attr::ReceiveLinkBandwidth::ACCEPT;

  UpdateGroupKey key4 = key1;
  key4.linkBandwidthBps = 2000000;

  EXPECT_FALSE(key1 == key2);
  EXPECT_FALSE(key1 == key3);
  EXPECT_FALSE(key1 == key4);
}

TEST(UpdateGroupKeyTest, InequalityDifferentRemovePrivateAsn) {
  UpdateGroupKey key1;
  key1.removePrivateAsn = false;

  UpdateGroupKey key2 = key1;
  key2.removePrivateAsn = true;

  EXPECT_FALSE(key1 == key2);
}

TEST(UpdateGroupKeyTest, InequalityDifferentRouteFilter) {
  UpdateGroupKey key1;
  key1.routeFilterStmtName = "^10\\.";

  UpdateGroupKey key2 = key1;
  key2.routeFilterStmtName = "^192\\.168\\.";

  EXPECT_FALSE(key1 == key2);
}

TEST(UpdateGroupKeyTest, InequalityDifferentAs4ByteCapable) {
  UpdateGroupKey key1;
  key1.as4ByteCapable = true;

  UpdateGroupKey key2 = key1;
  key2.as4ByteCapable = false;

  EXPECT_FALSE(key1 == key2);
}

TEST(UpdateGroupKeyTest, InequalityDifferentExtNhEncodingCapable) {
  UpdateGroupKey key1;
  key1.extNhEncodingCapable = false;

  UpdateGroupKey key2 = key1;
  key2.extNhEncodingCapable = true;

  EXPECT_FALSE(key1 == key2);
}

TEST(UpdateGroupKeyTest, InequalityDifferentLegacyV4NlriEncoding) {
  UpdateGroupKey key1;
  key1.legacyV4NlriEncoding = false;

  UpdateGroupKey key2 = key1;
  key2.legacyV4NlriEncoding = true;

  EXPECT_FALSE(key1 == key2);
}

/*
 * localAs is per-peer overridable (RFC-7705) and the group producer prepends
 * the first registered member's localAs to AS_PATH for every member, so peers
 * with different local AS must key into separate groups.
 */
TEST(UpdateGroupKeyTest, InequalityDifferentLocalAs) {
  UpdateGroupKey key1;
  key1.localAs = 65001;

  UpdateGroupKey key2 = key1;
  key2.localAs = 65002;

  EXPECT_FALSE(key1 == key2);
  EXPECT_NE(key1.hash(), key2.hash());
}

TEST(UpdateGroupKeyTest, InequalityDifferentAsConfedId) {
  UpdateGroupKey key1;
  key1.asConfedId = 65001;

  UpdateGroupKey key2 = key1;
  key2.asConfedId = 65002;

  EXPECT_FALSE(key1 == key2);
  EXPECT_NE(key1.hash(), key2.hash());
}

TEST(UpdateGroupKeyTest, HashConsistency) {
  UpdateGroupKey key1;
  key1.egressPolicyName = "policy1";
  key1.outDelay = std::chrono::seconds(30);
  key1.sessionType = BgpSessionType::IBGP;
  key1.afiIpv4Negotiated = true;
  key1.afiIpv6Negotiated = false;

  UpdateGroupKey key2 = key1;

  EXPECT_EQ(key1.hash(), key2.hash());
}

TEST(UpdateGroupKeyTest, HashDifference) {
  UpdateGroupKey key1;
  key1.egressPolicyName = "policy1";
  key1.outDelay = std::chrono::seconds(30);

  UpdateGroupKey key2 = key1;
  key2.egressPolicyName = "policy2";

  // Different keys should (very likely) have different hashes
  EXPECT_NE(key1.hash(), key2.hash());
}

TEST(UpdateGroupKeyTest, HashFunctorWorks) {
  UpdateGroupKey key1;
  key1.egressPolicyName = "policy1";
  key1.outDelay = std::chrono::seconds(30);

  UpdateGroupKeyHash hasher;
  size_t hash = hasher(key1);

  EXPECT_EQ(hash, key1.hash());
}

TEST(UpdateGroupKeyTest, F14MapCompatibility) {
  folly::F14FastMap<UpdateGroupKey, int, UpdateGroupKeyHash> map;

  UpdateGroupKey key1;
  key1.egressPolicyName = "policy1";
  key1.outDelay = std::chrono::seconds(30);
  key1.sessionType = BgpSessionType::IBGP;

  UpdateGroupKey key2;
  key2.egressPolicyName = "policy2";
  key2.outDelay = std::chrono::seconds(60);
  key2.sessionType = BgpSessionType::EBGP;

  map[key1] = 1;
  map[key2] = 2;

  EXPECT_EQ(map.size(), 2);
  EXPECT_EQ(map[key1], 1);
  EXPECT_EQ(map[key2], 2);

  // Same key should return same value
  UpdateGroupKey key1Copy = key1;
  EXPECT_EQ(map[key1Copy], 1);
}

TEST(UpdateGroupKeyTest, CompleteFieldsTest) {
  // Test with all fields set to non-default values
  auto key1 = UpdateGroupKey::buildUpdateGroupKey(
      fboss::kDefaultVrf,
      "PROPAGATE_FSW_SSW_OUT", /* egress policy */
      "^10\\.0\\..*", /* routeFilterStmtName */
      std::chrono::seconds(45), /* oudelay */
      BgpSessionType::EBGP, /* sessionType */
      true, /* afiIpv4Negotiated */
      true, /* afiIpv6Negotiated */
      true, /* isConfedPeer */
      true, /* isRrClient */
      bgp_attr::AdvertiseLinkBandwidth::BEST_PATH, /* advertiseLinkBandwidth */
      bgp_attr::ReceiveLinkBandwidth::ACCEPT, /* receiveLinkBandwidth */
      10000000, /* linkBandwidthBps */
      true, /* removePrivateAsn */
      true, /* sendAddPath */
      true, /* as4ByteCapable */
      false, /* extNhEncodingCapable */
      false, /* legacyV4NlriEncoding */
      "", /* peerGroupName */
      false, /* peerOverride */
      65001 /* localAs */);
  auto key2 = UpdateGroupKey::buildUpdateGroupKey(
      fboss::kDefaultVrf,
      "PROPAGATE_FSW_SSW_OUT", /* egress policy */
      "^10\\.0\\..*", /* routeFilterStmtName */
      std::chrono::seconds(45), /* oudelay */
      BgpSessionType::EBGP, /* sessionType */
      true, /* afiIpv4Negotiated */
      true, /* afiIpv6Negotiated */
      true, /* isConfedPeer */
      true, /* isRrClient */
      bgp_attr::AdvertiseLinkBandwidth::BEST_PATH, /* advertiseLinkBandwidth */
      bgp_attr::ReceiveLinkBandwidth::ACCEPT, /* receiveLinkBandwidth */
      10000000, /* linkBandwidthBps */
      true, /* removePrivateAsn */
      true, /* sendAddPath */
      true, /* as4ByteCapable */
      false, /* extNhEncodingCapable */
      false, /* legacyV4NlriEncoding */
      "", /* peerGroupName */
      false, /* peerOverride */
      65001 /* localAs */);

  EXPECT_TRUE(key1 == key2);
  EXPECT_EQ(key1.hash(), key2.hash());
}

/**
 * Test basic equality comparison for AdjRibOutOwnerKey
 */
TEST(AdjRibOutKeyTest, EqualityTest) {
  auto peerId1 = std::make_shared<nettools::bgplib::BgpPeerId>();
  auto peerId2 = std::make_shared<nettools::bgplib::BgpPeerId>();

  AdjRibOutOwnerKey peer1 = AdjRibOutOwnerKey::forPeer(peerId1);
  AdjRibOutOwnerKey peer2 = AdjRibOutOwnerKey::forPeer(peerId1);
  AdjRibOutOwnerKey peer3 = AdjRibOutOwnerKey::forPeer(peerId2);
  AdjRibOutOwnerKey group1 = AdjRibOutOwnerKey::forGroup(123);

  EXPECT_EQ(peer1, peer2);
  EXPECT_NE(peer1, peer3);
  EXPECT_NE(peer1, group1);
}

/**
 * Test inequality comparison for AdjRibOutOwnerKey
 */
TEST(AdjRibOutKeyTest, InequalityTest) {
  auto peerId1 = std::make_shared<nettools::bgplib::BgpPeerId>();
  auto peerId2 = std::make_shared<nettools::bgplib::BgpPeerId>();

  AdjRibOutOwnerKey peer1 = AdjRibOutOwnerKey::forPeer(peerId1);
  AdjRibOutOwnerKey peer2 = AdjRibOutOwnerKey::forPeer(peerId1);
  AdjRibOutOwnerKey peer3 = AdjRibOutOwnerKey::forPeer(peerId2);

  EXPECT_FALSE(peer1 != peer2);
  EXPECT_TRUE(peer1 != peer3);
}

/**
 * Test hash function produces consistent results
 */
TEST(AdjRibOutKeyTest, HashConsistencyTest) {
  auto peerId = std::make_shared<nettools::bgplib::BgpPeerId>();

  AdjRibOutOwnerKey peer1 = AdjRibOutOwnerKey::forPeer(peerId);
  AdjRibOutOwnerKey peer2 = AdjRibOutOwnerKey::forPeer(peerId);

  EXPECT_EQ(peer1.hash(), peer2.hash());
  EXPECT_EQ(peer1.hash(), peer1.hash());
}

/**
 * Test hash function produces different results for different keys
 */
TEST(AdjRibOutKeyTest, HashUniquenessTest) {
  auto peerId = std::make_shared<nettools::bgplib::BgpPeerId>();

  AdjRibOutOwnerKey peer = AdjRibOutOwnerKey::forPeer(peerId);
  AdjRibOutOwnerKey group1 = AdjRibOutOwnerKey::forGroup(123);
  AdjRibOutOwnerKey group2 = AdjRibOutOwnerKey::forGroup(456);

  // Peer and group should have different hashes
  EXPECT_NE(peer.hash(), group1.hash());
  // Two groups with different IDs should have different hashes
  EXPECT_NE(group1.hash(), group2.hash());
}

/**
 * Test AdjRibOutOwnerKeyHash functor works correctly
 */
TEST(AdjRibOutKeyTest, HashFunctorTest) {
  auto peerId = std::make_shared<nettools::bgplib::BgpPeerId>();
  AdjRibOutOwnerKey peer = AdjRibOutOwnerKey::forPeer(peerId);
  AdjRibOutOwnerKeyHash hasher;

  EXPECT_EQ(hasher(peer), peer.hash());
}

/**
 * Test that keys can be used in F14 maps
 */
TEST(AdjRibOutKeyTest, F14MapUsageTest) {
  folly::F14NodeMap<AdjRibOutOwnerKey, std::string, AdjRibOutOwnerKeyHash> map;

  auto peerId100 = std::make_shared<nettools::bgplib::BgpPeerId>();
  auto peerId200 = std::make_shared<nettools::bgplib::BgpPeerId>();

  AdjRibOutOwnerKey peer1 = AdjRibOutOwnerKey::forPeer(peerId100);
  AdjRibOutOwnerKey peer2 = AdjRibOutOwnerKey::forPeer(peerId200);
  AdjRibOutOwnerKey group1 = AdjRibOutOwnerKey::forGroup(1);

  map[peer1] = "Peer 100";
  map[peer2] = "Peer 200";
  map[group1] = "Group 1";

  EXPECT_EQ(map.at(peer1), "Peer 100");
  EXPECT_EQ(map.at(peer2), "Peer 200");
  EXPECT_EQ(map.at(group1), "Group 1");
  EXPECT_EQ(map.size(), 3);

  AdjRibOutOwnerKey peer1_copy = AdjRibOutOwnerKey::forPeer(peerId100);
  EXPECT_EQ(map.at(peer1_copy), "Peer 100");
}

/**
 * Test creating keys for different scenarios
 */
TEST(AdjRibOutKeyTest, KeyCreationTest) {
  auto peerId = std::make_shared<nettools::bgplib::BgpPeerId>();

  AdjRibOutOwnerKey peerKey = AdjRibOutOwnerKey::forPeer(peerId);
  EXPECT_TRUE(peerKey.isPeer());
  EXPECT_FALSE(peerKey.isGroup());
  EXPECT_EQ(peerKey.getPeerId(), peerId);

  AdjRibOutOwnerKey groupKey = AdjRibOutOwnerKey::forGroup(7);
  EXPECT_FALSE(groupKey.isPeer());
  EXPECT_TRUE(groupKey.isGroup());
  EXPECT_EQ(groupKey.getGroupId(), 7);
}

} // namespace facebook::bgp
