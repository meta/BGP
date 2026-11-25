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

#define AdjRib_TEST_FRIENDS friend class AdjRibInboundFixture;
#include "neteng/fboss/bgp/cpp/tests/AdjRibInUtils.h"
#undef AdjRib_TEST_FRIENDS

#include <folly/container/small_vector.h>

#include "fboss/agent/Constants.h"
#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"

namespace facebook::bgp {

// BGP update with prefilled attributes and single v4 announced prefix
std::shared_ptr<BgpUpdate2> createV4BgpUpdateSingleAnnounce(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& nexthop,
    const uint32_t med,
    const uint32_t originatorId,
    const BgpAttrOrigin& origin,
    const uint32_t pathId,
    const bool confed) {
  auto update = std::make_shared<BgpUpdate2>();

  RiggedIPPrefix rigPrefix;
  rigPrefix.prefix() = toIPPrefix(prefix);
  rigPrefix.pathId() = pathId;
  update->v4Announced2()->push_back(rigPrefix);
  update->v4Nexthop() = toBinaryAddress(nexthop);

  update->attrs()->origin() = origin;
  BgpAttrAsPathSegment segment1;
  if (confed) {
    segment1.asConfedSequence()->push_back(kAsSeqAsNum);
  } else {
    segment1.asSequence()->push_back(kAsSeqAsNum);
  }
  update->attrs()->asPath()->push_back(segment1);
  update->attrs()->nexthop() = nexthop.str();
  update->attrs()->med() = med;
  update->attrs()->isMedSet() = true;
  update->attrs()->localPref() = kLocalPref;
  update->attrs()->atomicAggregate() = true;
  update->attrs()->aggregator()->asn() = kAggregatorAsNum;
  update->attrs()->aggregator()->ip() = kAggregatorAddr.str();
  // originatorId and clusterList should be in network byte order
  update->attrs()->originatorId() = htonl(originatorId);
  update->attrs()->clusterList()->push_back(htonl(originatorId));

  BgpAttrCommunity community;
  community.asn() = kCommAsNum;
  community.value() = kCommAsVal;
  update->attrs()->communities()->push_back(community);

  BgpAttrExtCommunity extCommunity;
  // AS-specific extended-community
  extCommunity.firstWord() = kExtCommASTypeFirstWord;
  extCommunity.secondWord() = kExtCommASTypeSecondWord;
  update->attrs()->extCommunities()->push_back(extCommunity);
  // Link-BW extended-community (10G)
  extCommunity.firstWord() = kExtCommLbwTypeFirstWord;
  extCommunity.secondWord() = kExtCommLbwTypeSecondWord10G;
  update->attrs()->extCommunities()->push_back(extCommunity);
  // Regular extended-community asn
  extCommunity.firstWord() = kExtCommRegularTypeFirstWord;
  extCommunity.secondWord() = kExtCommRegularTypeSecondWord;
  update->attrs()->extCommunities()->push_back(extCommunity);

  return update;
}

// BGP update with single withdraw
std::shared_ptr<BgpUpdate2> createV4BgpUpdateSingleWithdraw(
    const folly::CIDRNetwork& prefix,
    const uint32_t pathId) {
  auto update = std::make_shared<BgpUpdate2>();
  RiggedIPPrefix rigPrefix;
  *rigPrefix.prefix() = toIPPrefix(prefix);
  rigPrefix.pathId() = pathId;
  update->v4Withdrawn2()->push_back(rigPrefix);
  return update;
}

// BGP update with prefilled attributes and multiple v4 announced prefix
std::shared_ptr<BgpUpdate2> createV4BgpUpdateMultipleAnnounce(
    const std::vector<folly::CIDRNetwork>& vec,
    const BgpAttrOrigin& origin,
    const int64_t lbwRawByte,
    const uint32_t asNum) {
  auto update = std::make_shared<BgpUpdate2>();

  for (const auto& prefix : vec) {
    RiggedIPPrefix rigPrefix;
    *rigPrefix.prefix() = toIPPrefix(prefix);
    update->v4Announced2()->push_back(rigPrefix);
  }
  *update->v4Nexthop() = toBinaryAddress(kV4Nexthop1);

  *update->attrs()->origin() = origin;
  BgpAttrAsPathSegment segment1;
  segment1.asSequence()->push_back(asNum);
  update->attrs()->asPath()->push_back(segment1);
  *update->attrs()->nexthop() = kV4Nexthop1.str();
  update->attrs()->med() = kMed;
  update->attrs()->isMedSet() = true;
  update->attrs()->localPref() = kLocalPref;
  *update->attrs()->atomicAggregate() = false;
  // originatorId should be in network byte order
  *update->attrs()->originatorId() = htonl(kOriginatorId);

  // Link bandwidth Ext Community
  BgpAttrExtCommunity extCommunity;
  // Link-BW extended-community (10G)
  extCommunity.firstWord() = kExtCommLbwTypeFirstWord;
  extCommunity.secondWord() = lbwRawByte;
  update->attrs()->extCommunities()->push_back(extCommunity);
  return update;
}

// BGP update with prefilled attributes and single v6 announced prefix
std::shared_ptr<BgpUpdate2> createV6BgpUpdateSingleAnnounce(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& nexthop) {
  RiggedIPPrefix rPrefix;

  auto update = std::make_shared<BgpUpdate2>();

  *update->mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update->mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  *update->mpAnnounced()->nexthop() = toBinaryAddress(nexthop);

  *rPrefix.prefix() = toIPPrefix(prefix);
  update->mpAnnounced()->prefixes()->push_back(rPrefix);

  *update->attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment1;
  segment1.asSequence()->push_back(kAsSeqAsNum);
  update->attrs()->asPath()->push_back(segment1);

  update->attrs()->med() = kMed;
  update->attrs()->isMedSet() = true;
  update->attrs()->localPref() = kLocalPref;
  *update->attrs()->atomicAggregate() = false;
  return update;
}

// BGP update with prefilled attributes and multiple v6 announced prefix
std::shared_ptr<BgpUpdate2> createV6BgpUpdateMultipleAnnounce(
    const std::vector<folly::CIDRNetwork>& vec) {
  auto update = std::make_shared<BgpUpdate2>();

  *update->mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update->mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  *update->mpAnnounced()->nexthop() = toBinaryAddress(kV6Nexthop1);

  for (auto& prefix : vec) {
    RiggedIPPrefix rPrefix;
    *rPrefix.prefix() = toIPPrefix(prefix);
    update->mpAnnounced()->prefixes()->push_back(rPrefix);
  }
  *update->attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment1;
  segment1.asSequence()->push_back(kAsSeqAsNum);
  update->attrs()->asPath()->push_back(segment1);

  update->attrs()->med() = kMed;
  update->attrs()->isMedSet() = true;
  update->attrs()->localPref() = kLocalPref;
  *update->attrs()->atomicAggregate() = false;
  return update;
}

// BGP update with single withdraw
std::shared_ptr<BgpUpdate2> createV6BgpUpdateSingleWithdraw(
    const folly::CIDRNetwork& prefix) {
  auto update = std::make_shared<BgpUpdate2>();
  *update->mpWithdrawn()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update->mpWithdrawn()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  RiggedIPPrefix rigPrefix;
  *rigPrefix.prefix() = toIPPrefix(prefix);
  update->mpWithdrawn()->prefixes()->emplace_back(rigPrefix);
  return update;
}

std::shared_ptr<BgpUpdate2> createV4AndV6BgpUpdateSingleAnnounce(
    const folly::CIDRNetwork& prefix,
    const std::vector<BgpAttrCommunityC>& communities) {
  auto update = std::make_shared<BgpUpdate2>();

  if (prefix.first.isV6()) {
    RiggedIPPrefix rPrefix;
    *rPrefix.prefix() = toIPPrefix(prefix);
    update->mpAnnounced()->prefixes()->push_back(rPrefix);
    *update->mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
    *update->mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
    *update->mpAnnounced()->nexthop() = toBinaryAddress(kV6Nexthop1);
  } else {
    EXPECT_TRUE(prefix.first.isV4());
    RiggedIPPrefix rigPrefix;
    *rigPrefix.prefix() = toIPPrefix(prefix);
    update->v4Announced2()->push_back(rigPrefix);
    *update->v4Nexthop() = toBinaryAddress(kV4Nexthop1);
  }

  std::vector<BgpAttrCommunity> comms;
  for (const auto comm : communities) {
    comms.push_back(comm.toThrift());
  }

  *update->attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment1;
  segment1.asSequence()->push_back(kAsSeqAsNum);
  update->attrs()->asPath()->push_back(segment1);
  *update->attrs()->nexthop() = kV4Nexthop1.str();
  update->attrs()->med() = kMed;
  update->attrs()->isMedSet() = true;
  update->attrs()->localPref() = kLocalPref;
  *update->attrs()->atomicAggregate() = false;
  // originatorId should be in network byte order
  *update->attrs()->originatorId() = htonl(kOriginatorId);
  *update->attrs()->communities() = comms;
  return update;
}

// Creates BGPupdate2 with a mix of v4 and v6 networks
std::shared_ptr<BgpUpdate2> createV4AndV6BgpUpdateMultipleAnnounce(
    const std::vector<folly::CIDRNetwork>& vec) {
  auto update = std::make_shared<BgpUpdate2>();
  auto isV6PrefixPresent = false;
  auto isV4PrefixPresent = false;

  for (const auto& prefix : vec) {
    if (prefix.first.isV6()) {
      RiggedIPPrefix rPrefix;
      *rPrefix.prefix() = toIPPrefix(prefix);
      update->mpAnnounced()->prefixes()->push_back(rPrefix);
      isV6PrefixPresent = true;
    } else {
      EXPECT_TRUE(prefix.first.isV4());
      RiggedIPPrefix rigPrefix;
      *rigPrefix.prefix() = toIPPrefix(prefix);
      update->v4Announced2()->push_back(rigPrefix);
      isV4PrefixPresent = true;
    }
  }

  if (isV6PrefixPresent) {
    *update->mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
    *update->mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
    *update->mpAnnounced()->nexthop() = toBinaryAddress(kV6Nexthop1);
  }

  if (isV4PrefixPresent) {
    *update->v4Nexthop() = toBinaryAddress(kV4Nexthop1);
  }

  *update->attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment1;
  segment1.asSequence()->push_back(kAsSeqAsNum);
  update->attrs()->asPath()->push_back(segment1);
  *update->attrs()->nexthop() = kV4Nexthop1.str();
  update->attrs()->med() = kMed;
  update->attrs()->isMedSet() = true;
  update->attrs()->localPref() = kLocalPref;
  *update->attrs()->atomicAggregate() = false;
  // originatorId should be in network byte order
  *update->attrs()->originatorId() = htonl(kOriginatorId);
  return update;
}

std::shared_ptr<BgpUpdate2> createV4BgpUpdateWithAsLoop(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& nexthop,
    const uint32_t asn,
    const bool confed) {
  auto update = createV4BgpUpdateSingleAnnounce(prefix, nexthop);
  createBgpUpdateWithAsLoop(update, asn, confed);
  return update;
}

std::shared_ptr<BgpUpdate2> createV6BgpUpdateWithAsLoop(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& nexthop,
    const uint32_t asn,
    const bool confed) {
  auto update = createV6BgpUpdateSingleAnnounce(prefix, nexthop);
  createBgpUpdateWithAsLoop(update, asn, confed);
  return update;
}

/**
 * @brief Create a BGP update with AS loop by adding adjRib's own AS
 * to the AS path sequence.
 *
 * @param update BgpUpdate2 to add AS loop to.
 * @param asn AS number to add to the AS path sequence.
 * @param confed Whether the AS should be added to AS_PATH or AS_CONFED_PATH.
 */
std::shared_ptr<BgpUpdate2> createBgpUpdateWithAsLoop(
    std::shared_ptr<BgpUpdate2>& update,
    const uint32_t asn,
    const bool confed) {
  if (confed) {
    BgpAttrAsPathSegment segment;
    segment.asConfedSequence()->push_back(asn);
    update->attrs()->asPath()->push_back(segment);
    return update;
  }
  BgpAttrAsPathSegment segment;
  segment.asSequence()->push_back(asn);
  update->attrs()->asPath()->push_back(segment);
  return update;
}

/**
 * @brief Create a BGP update with AS loop by adding adjRib's own AS
 * to the AS path set.
 *
 * @param update BgpUpdate2 to add AS loop to.
 * @param asn AS number to add to the AS path set.
 * @param confed Whether the AS should be added to AS_PATH or AS_CONFED_PATH.
 */
std::shared_ptr<BgpUpdate2> createV4BgpUpdateWithAsSetLoop(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& nexthop,
    const uint32_t asn,
    const bool confed) {
  auto update = createV4BgpUpdateSingleAnnounce(prefix, nexthop);
  if (confed) {
    BgpAttrAsPathSegment segment;
    segment.asConfedSet()->insert(asn);
    update->attrs()->asPath()->push_back(segment);
    return update;
  }
  BgpAttrAsPathSegment segment;
  segment.asSet()->insert(asn);
  update->attrs()->asPath()->push_back(segment);

  return update;
}

// Helper functions for creating BGP updates
namespace {

void setUpdatePrefix(
    BgpUpdate2& update,
    bool isV4,
    const folly::CIDRNetwork& cidr,
    uint32_t addPathId) {
  RiggedIPPrefix rigPrefix;
  rigPrefix.prefix() = toIPPrefix(cidr);
  if (addPathId != 0) {
    rigPrefix.pathId() = addPathId;
  }

  if (isV4) {
    update.v4Announced2()->push_back(rigPrefix);
  } else {
    update.mpAnnounced()->prefixes()->push_back(rigPrefix);
    *update.mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
    *update.mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  }
}

void setUpdateNexthop(
    BgpUpdate2& update,
    bool isV4,
    const std::string& nexthop) {
  const auto nexthopAddr = folly::IPAddress(nexthop);

  if (isV4) {
    update.v4Nexthop() = toBinaryAddress(nexthopAddr);
  } else {
    update.mpAnnounced()->nexthop() = toBinaryAddress(nexthopAddr);
  }

  update.attrs()->nexthop() = nexthop;
}

void setUpdateAsPath(
    BgpUpdate2& update,
    const std::vector<uint32_t>& asPathSeq) {
  if (asPathSeq.empty()) {
    return;
  }

  BgpAttrAsPathSegment segment;
  for (uint32_t asn : asPathSeq) {
    segment.asSequence()->push_back(asn);
  }
  update.attrs()->asPath()->push_back(segment);
}

void setUpdateCommunities(
    BgpUpdate2& update,
    const std::vector<std::string>& communities) {
  /*
   * Delegate to BgpAttrCommunityC::createBgpAttrCommunity so RFC 1997
   * well-known names ("no-advertise", "no-export", "no-export-subconfed",
   * "local-as", "internet") AND numeric forms ("asn:value", flat uint32)
   * are all supported. Silently skipping unparseable communities keeps
   * the prior behavior for legacy callers.
   */
  for (const auto& comm : communities) {
    const auto parsedOpt =
        nettools::bgplib::BgpAttrCommunityC::createBgpAttrCommunity(comm);
    if (!parsedOpt.has_value()) {
      continue;
    }
    BgpAttrCommunity bgpCommunity;
    bgpCommunity.asn() = parsedOpt->asn;
    bgpCommunity.value() = parsedOpt->value;
    update.attrs()->communities()->push_back(bgpCommunity);
  }
}

void setUpdateLinkBandwidth(BgpUpdate2& update, float linkBandwidthBps) {
  /*
   * Create LBW extended community using the same encoding as
   * BgpExtCommunityLinkBandWidthTypeC. Per draft-ietf-idr-link-bandwidth:
   * - High-order octet of extended Type Field is 0x40 (non-transitive)
   * - Low-order octet of extended Type Field is 0x04 (link bandwidth subtype)
   * - Global Administrator subfield is the AS number
   */
  BgpAttrExtCommunity extCommunity;
  constexpr uint8_t lbwType = 0x40;
  constexpr uint8_t lbwSubType = 0x04;
  uint16_t asn = kLocalAs1;
  extCommunity.firstWord() = (lbwType << 24) | (lbwSubType << 16) | asn;

  /*
   * The link bandwidth value is encoded as IEEE 754 single precision float.
   * We reinterpret the float bits as uint32_t for the secondWord.
   */
  union {
    uint32_t intVal;
    float floatVal;
  } tmp;
  tmp.floatVal = linkBandwidthBps;
  extCommunity.secondWord() = tmp.intVal;

  update.attrs()->extCommunities()->push_back(extCommunity);
}

} // namespace

std::shared_ptr<BgpUpdate2> createBgpUpdateAnnouncement(
    bool isV4,
    const folly::CIDRNetwork& cidr,
    const std::string& nexthop,
    const std::vector<uint32_t>& asPathSeq,
    const std::vector<std::string>& communities,
    uint32_t addPathId,
    uint32_t localPref,
    uint32_t med,
    std::optional<float> linkBandwidthBps) {
  auto update = std::make_shared<BgpUpdate2>();

  setUpdatePrefix(*update, isV4, cidr, addPathId);
  setUpdateNexthop(*update, isV4, nexthop);

  update->attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_EGP;
  update->attrs()->localPref() = localPref;

  if (med > 0) {
    update->attrs()->med() = med;
    update->attrs()->isMedSet() = true;
  }

  setUpdateAsPath(*update, asPathSeq);
  setUpdateCommunities(*update, communities);

  if (linkBandwidthBps.has_value()) {
    setUpdateLinkBandwidth(*update, linkBandwidthBps.value());
  }

  return update;
}

std::shared_ptr<BgpUpdate2> createBgpUpdateWithdrawal(
    bool isV4,
    const folly::CIDRNetwork& cidr,
    uint32_t addPathId) {
  auto update = std::make_shared<BgpUpdate2>();

  RiggedIPPrefix rigPrefix;
  *rigPrefix.prefix() = toIPPrefix(cidr);
  if (addPathId != 0) {
    rigPrefix.pathId() = addPathId;
  }

  if (isV4) {
    update->v4Withdrawn2()->push_back(rigPrefix);
  } else {
    update->mpWithdrawn()->prefixes()->push_back(rigPrefix);
    *update->mpWithdrawn()->afi() = BgpUpdateAfi::AFI_IPv6;
    *update->mpWithdrawn()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  }

  return update;
}

void AdjRibInboundFixture::setupAdjRib(
    const std::chrono::seconds& localGrRestartTime,
    const std::optional<std::chrono::seconds>& remoteGrRestartTime,
    const bool callSessionEstablished,
    const uint32_t globalAs,
    const uint32_t localAs,
    const uint32_t configuredRemoteAs,
    const AfiIpv4Negotiated& isAfiIpv4Negotiated,
    const AfiIpv6Negotiated& isAfiIpv6Negotiated,
    const std::shared_ptr<PolicyManager>& policy,
    const std::optional<std::string>& ingressPolicyName,
    const bool isConfedPeer,
    const std::optional<uint32_t>& localConfedAs,
    const std::optional<uint32_t>& asConfedId,
    const AdvertiseLinkBandwidth& advertiseLinkBandwidth,
    const ReceiveLinkBandwidth& receiveLinkBandwidth,
    const std::optional<float>& linkBandwidthBps,
    ValidateRemoteAs validateRemoteAs,
    const uint32_t maxRoutes,
    const bool warningOnly,
    const uint8_t warningLimit,
    const uint32_t maxAcceptedRoutes,
    const bool acptWarningOnly,
    const uint8_t acptWarningLimit,
    const std::optional<nettools::bgplib::BgpPeerId>& peerId,
    const IsRedistributePeer isRedistributePeer,
    std::shared_ptr<std::atomic<bool>> isSafeModeOn,
    const bool enforce_first_as,
    const std::optional<uint32_t>& remoteAs) {
  thrift::RouteLimit preFilterLimit;
  preFilterLimit.max_routes() = maxRoutes;
  preFilterLimit.warning_only() = warningOnly;
  preFilterLimit.warning_limit() = warningLimit;

  thrift::RouteLimit postFilterLimit;
  postFilterLimit.max_routes() = maxAcceptedRoutes;
  postFilterLimit.warning_only() = acptWarningOnly;
  postFilterLimit.warning_limit() = acptWarningLimit;

  auto adjRibOutGroup =
      std::make_shared<AdjRibOutGroup>(evb_, fboss::kDefaultVrf, "Group1");
  adjRibInQ_->open();
  ribInQ_.open();
  adjRib_ = std::make_shared<AdjRib>(
      *peerId,
      PeeringParams(
          fboss::kDefaultVrf,
          kPeerAddr1,
          std::nullopt, // peerPrefix
          globalAs,
          localAs,
          configuredRemoteAs,
          kLocalAddr1.asV4(), // localBgpId
          localClusterId_, // localClusterId
          std::chrono::seconds(facebook::bgp::kDefaultHoldTime), // holdTime
          localGrRestartTime,
          kBgpPort,
          folly::AsyncSocket::anyAddress(), // bindAddr
          TBgpSessionConnectMode::PASSIVE_ACTIVE, // connectMode
          kV4Nexthop1.asV4(), // nexthopV4
          kV6Nexthop1.asV6(), // nexthopV6
          RrClientConfigured(kIsRrClientFalse), // isRrClient
          NextHopSelfConfigured(false), // nextHopSelf
          AfiIpv4Configured(true),
          AfiIpv6Configured(true),
          ConfedPeerConfigured(isConfedPeer),
          RemovePrivateAsConfigured(false),
          localConfedAs,
          asConfedId,
          advertiseLinkBandwidth, // advertiseLinkBandwidth
          receiveLinkBandwidth, // receiveLinkBandwidth
          linkBandwidthBps,
          validateRemoteAs, // validateRemoteAs
          preFilterLimit,
          postFilterLimit,
          false, // allowLoopbackReflection
          EnableStatefulHa{false}, // enableStatefulHa
          std::nullopt, // addPath,
          V4OverV6Nexthop{false}, // v4OverV6Nexthop,
          isRedistributePeer,
          false, // isEnhancedRouteRefreshConfigured
          enforce_first_as),
      evb_,
      ribInQ_,
      fromAdjRibQ_,
      std::make_shared<folly::coro::Baton>(),
      policy,
      isSafeModeOn,
      ingressPolicyName,
      std::nullopt, /* egressPolicyName */
      adjRibOutGroup,
      std::nullopt, /* outDelay */
      config_ ? std::make_shared<ConfigManager>(config_) : nullptr);
  adjRib_->remoteAs_ = remoteAs.value_or(configuredRemoteAs);

  if (callSessionEstablished) {
    establishSession(
        remoteGrRestartTime,
        isAfiIpv4Negotiated,
        isAfiIpv6Negotiated,
        remoteAs);
  }
}

std::shared_ptr<AdjRib> AdjRibInboundFixture::setupAdjRib(
    const nettools::bgplib::BgpPeerId& peerId,
    const PeeringParams& params) {
  auto adjRibOutGroup =
      std::make_shared<AdjRibOutGroup>(evb_, fboss::kDefaultVrf, "Group2");
  auto adjRib = std::make_shared<AdjRib>(
      peerId,
      params,
      evb_,
      ribInQ_,
      fromAdjRibQ_,
      std::make_shared<folly::coro::Baton>(),
      nullptr /* policyManager */,
      std::make_shared<std::atomic<bool>>(false) /* isSafeModeOn */,
      std::nullopt,
      std::nullopt,
      adjRibOutGroup,
      std::nullopt /* outDelay */,
      config_ ? std::make_shared<ConfigManager>(config_) : nullptr);

  return adjRib;
}

void AdjRibInboundFixture::setupAdjRib(
    const std::shared_ptr<PolicyManager>& policy,
    const std::optional<std::string>& ingressPolicyName) {
  setupAdjRib(
      kShortGrRestartTime,
      std::nullopt, // remoteGrRestartTime
      true, // callSessionEstablished
      kLocalAs1,
      kLocalAs1,
      kRemoteAs1,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      policy,
      ingressPolicyName);
}

void AdjRibInboundFixture::setupAdjRib(
    const nettools::bgplib::BgpPeerId& peerId) {
  setupAdjRib(
      kShortGrRestartTime, // localGrRestartTime
      std::nullopt, // remoteGrRestartTime
      true, // callSessionEstablished
      kLocalAs1, // globalAs
      kLocalAs1, // localAs
      kRemoteAs1, // remoteAs
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      nullptr, // PolicyManager
      std::nullopt, // ingressPolicyName
      kIsConfedPeerFalse, // isConfedPeer
      std::nullopt, // localConfedAs
      std::nullopt, // asConfedId
      AdvertiseLinkBandwidth::DISABLE, // advertiseLinkBandwidth
      ReceiveLinkBandwidth::DISABLE, // receiveLinkBandwidth
      std::nullopt, // linkBandwidthBps
      ValidateRemoteAs{true},
      kDefaultPreMaxRoutes, // maxRoutes
      false, // warningOnly
      kDefaultPreWarningThreshold, // warningLimit
      kDefaultPostMaxRoutes, // maxAcceptedRoutes
      false, // acptWarningOnly
      kDefaultPostWarningThreshold, // acptWarningLimit
      peerId);
}

void AdjRibInboundFixture::setupAdjRib(
    const std::optional<float>& linkBandwidthBps,
    const ReceiveLinkBandwidth& receiveLBW,
    const std::shared_ptr<PolicyManager>& policy,
    const std::optional<std::string>& ingressPolicyName) {
  setupAdjRib(
      kShortGrRestartTime, // localGrRestartTime
      std::nullopt, // remoteGrRestartTime
      true, // callSessionEstablished
      kLocalAs1, // globalAs
      kLocalAs1, // localAs
      kRemoteAs1, // remoteAs
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      policy, // PolicyManager
      ingressPolicyName, // ingressPolicyName
      kIsConfedPeerFalse, // isConfedPeer
      std::nullopt, // localConfedAs
      std::nullopt, // asConfedId
      AdvertiseLinkBandwidth::DISABLE, // advertiseLinkBandwidth
      receiveLBW, // receiveLinkBandwidth
      linkBandwidthBps); // linkBandwidthBps
}

void AdjRibInboundFixture::setupAdjRibForRedistributePeer() {
  setupAdjRib(
      kShortGrRestartTime, // localGrRestartTime
      std::nullopt, // remoteGrRestartTime
      true, // callSessionEstablished
      kLocalAs1, // globalAs
      kLocalAs1, // localAs
      kRemoteAs1, // remoteAs
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      nullptr, // PolicyManager
      std::nullopt, // ingressPolicyName
      kIsConfedPeerFalse, // isConfedPeer
      std::nullopt, // localConfedAs
      std::nullopt, // asConfedId
      AdvertiseLinkBandwidth::DISABLE, // advertiseLinkBandwidth
      ReceiveLinkBandwidth::DISABLE, // receiveLinkBandwidth
      std::nullopt, // linkBandwidthBps
      ValidateRemoteAs{true},
      kDefaultPreMaxRoutes, // maxRoutes
      false, // warningOnly
      kDefaultPreWarningThreshold, // warningLimit
      kDefaultPostMaxRoutes, // maxAcceptedRoutes
      false, // acptWarningOnly
      kDefaultPostWarningThreshold, // acptWarningLimit
      kPeerId1,
      IsRedistributePeer(true));
}

void AdjRibInboundFixture::establishSession(
    const std::optional<std::chrono::seconds>& remoteGrRestartTime,
    const AfiIpv4Negotiated& isAfiIpv4Negotiated,
    const AfiIpv6Negotiated& isAfiIpv6Negotiated,
    const std::optional<uint32_t>& remoteAs,
    const EnhancedRouteRefreshNegotiated& isEnhancedRouteRefreshNegotiated,
    const RouteRefreshNegotiated& isRouteRefreshNegotiated) {
  fm_->addTask([&,
                remoteGrRestartTime,
                isAfiIpv4Negotiated,
                isAfiIpv6Negotiated,
                remoteAs,
                isEnhancedRouteRefreshNegotiated,
                isRouteRefreshNegotiated] {
    adjRib_->sessionEstablished(
        remoteAs.value_or(adjRib_->getPeeringParams().remoteAs),
        (remoteGrRestartTime
             ? std::optional<uint16_t>(remoteGrRestartTime->count())
             : std::nullopt),
        adjRibInQ_,
        boundedAdjRibOutQ_,
        isAfiIpv4Negotiated,
        isAfiIpv6Negotiated,
        V4OverV6Nexthop{false},
        isEnhancedRouteRefreshNegotiated,
        isRouteRefreshNegotiated,
        std::nullopt,
        /*as4ByteCapable=*/true,
        /*extNhEncodingCapable=*/false,
        /*remoteMpExtExist=*/true);
    adjRib_->startMessageProcessingLoop();
  });
}

void AdjRibInboundFixture::reEstablishSession(
    const std::optional<std::chrono::seconds>& remoteGrRestartTime,
    const AfiIpv4Negotiated& isAfiIpv4Negotiated,
    const AfiIpv6Negotiated& isAfiIpv6Negotiated,
    const std::optional<uint32_t>& remoteAs) {
  /*
   * Mimic PeerManagerBase::sessionEstablished by ensuring the async scope is
   * re-initialized before re-establishing the session. This joins the
   * cancelled scope from the previous session and creates a fresh one.
   * Must be called from fiber context (within fm_->addTask).
   */
  folly::coro::blockingWait(adjRib_->ensureAsyncScopeInitialized());

  adjRib_->sessionEstablished(
      remoteAs.value_or(adjRib_->getPeeringParams().remoteAs),
      (remoteGrRestartTime
           ? std::optional<uint16_t>(remoteGrRestartTime->count())
           : std::nullopt),
      adjRibInQ_,
      boundedAdjRibOutQ_,
      isAfiIpv4Negotiated,
      isAfiIpv6Negotiated,
      V4OverV6Nexthop{false},
      EnhancedRouteRefreshNegotiated{false},
      RouteRefreshNegotiated{false},
      std::nullopt,
      /*as4ByteCapable=*/true,
      /*extNhEncodingCapable=*/false,
      /*remoteMpExtExist=*/true);
  adjRib_->startMessageProcessingLoop();
}
} // namespace facebook::bgp
