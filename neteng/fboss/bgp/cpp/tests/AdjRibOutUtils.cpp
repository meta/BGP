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

#define AdjRib_TEST_FRIENDS friend class AdjRibOutboundFixture;
#include "AdjRibOutUtils.h"
#include "fboss/agent/Constants.h"
#undef AdjRib_TEST_FRIENDS

#include <folly/container/small_vector.h>

namespace facebook::bgp {

/**
 * @brief: This setupAdjRib(...) function is taking the peerAddr as a parameter
 * as opposed to the other overload function which is taking the BgpPeerId as a
 * parameter.
 */
void AdjRibOutboundFixture::setupAdjRib(
    const AsNum& globalAs,
    const AsNum& localAs,
    const AsNum& remoteAs,
    const bool isRrClient,
    const bool isConfedPeer,
    const NextHopSelfConfigured& isNextHopSelfConfigured,
    const folly::IPAddress& v4Nexthop,
    const folly::IPAddress& v6Nexthop,
    const bool sessionEstablish,
    const AfiIpv4Negotiated& isAfiIpv4Negotiated,
    const AfiIpv6Negotiated& isAfiIpv6Negotiated,
    const V4OverV6Nexthop& isV4OverV6Nexthop,
    const EnhancedRouteRefreshNegotiated& isEnhancedRouteRefreshNegotiated,
    const RouteRefreshNegotiated& isRouteRefreshNegotiated,
    const RemovePrivateAsConfigured& removePrivateAs,
    const std::shared_ptr<PolicyManager>& policy,
    const std::optional<std::string>& egressPolicyName,
    const folly::IPAddress& peerAddr, // folly::IPAddress
    const bool allowLoopbackReflection,
    const std::optional<nettools::bgplib::BgpAddPathSendRec>& addPathCapa,
    const std::optional<std::string>& ingressPolicyName) {
  auto peerId = nettools::bgplib::BgpPeerId(
      peerAddr, folly::IPAddressV4("255.0.0.1").toLongHBO());
  /*
   * Recursively calling the overload setupAdjRib(...) by passing the peerId
   * instead of the peerAddr
   */
  setupAdjRib(
      globalAs,
      localAs,
      remoteAs,
      isRrClient,
      isConfedPeer,
      isNextHopSelfConfigured,
      v4Nexthop,
      v6Nexthop,
      sessionEstablish,
      isAfiIpv4Negotiated,
      isAfiIpv6Negotiated,
      isV4OverV6Nexthop,
      isEnhancedRouteRefreshNegotiated,
      isRouteRefreshNegotiated,
      removePrivateAs,
      policy,
      egressPolicyName,
      peerId,
      allowLoopbackReflection,
      addPathCapa,
      ingressPolicyName);
}

/**
 * @brief: This setupAdjRib(...) function is taking the BgpPeerId as a parameter
 * as opposed to the other overload function which is taking the peerAddr as a
 * parameter.
 */
void AdjRibOutboundFixture::setupAdjRib(
    const AsNum& globalAs,
    const AsNum& localAs,
    const AsNum& remoteAs,
    const bool isRrClient,
    const bool isConfedPeer,
    const NextHopSelfConfigured& isNextHopSelfConfigured,
    const folly::IPAddress& v4Nexthop,
    const folly::IPAddress& v6Nexthop,
    const bool sessionEstablish,
    const AfiIpv4Negotiated& isAfiIpv4Negotiated,
    const AfiIpv6Negotiated& isAfiIpv6Negotiated,
    const V4OverV6Nexthop& isV4OverV6Nexthop,
    const EnhancedRouteRefreshNegotiated& isEnhancedRouteRefreshNegotiated,
    const RouteRefreshNegotiated& isRouteRefreshNegotiated,
    const RemovePrivateAsConfigured& removePrivateAs,
    const std::shared_ptr<PolicyManager>& policy,
    const std::optional<std::string>& egressPolicyName,
    const nettools::bgplib::BgpPeerId& peerId,
    const bool allowLoopbackReflection,
    const std::optional<nettools::bgplib::BgpAddPathSendRec>& addPathCapa,
    const std::optional<std::string>& ingressPolicyName) {
  auto adjRibOutGroup =
      std::make_shared<AdjRibOutGroup>(evb_, fboss::kDefaultVrf, "GroupOut");
  adjRibInQ_->open();
  ribInQ_.open();
  adjRib_ = std::make_shared<AdjRib>(
      peerId,
      PeeringParams(
          fboss::kDefaultVrf,
          peerId.peerAddr,
          std::nullopt, // peerPrefix
          globalAs,
          localAs,
          remoteAs,
          kLocalAddr1.asV4(), // localBgpId
          localClusterId_, // localClusterId
          std::chrono::seconds(facebook::bgp::kDefaultHoldTime),
          kShortGrRestartTime,
          facebook::nettools::bgplib::constants::kBgpPort,
          folly::AsyncSocket::anyAddress(),
          TBgpSessionConnectMode::PASSIVE_ACTIVE,
          v4Nexthop.asV4(),
          v6Nexthop.asV6(),
          RrClientConfigured(isRrClient),
          isNextHopSelfConfigured,
          AfiIpv4Configured(true),
          AfiIpv6Configured(true),
          ConfedPeerConfigured(isConfedPeer),
          RemovePrivateAsConfigured(removePrivateAs),
          std::nullopt, // localConfedAs
          std::nullopt, // asConfedId
          AdvertiseLinkBandwidth::DISABLE, // advertiseLinkBandwidth
          ReceiveLinkBandwidth::ACCEPT, // receiveLinkBandwidth
          std::nullopt, // linkBandwidthBps
          ValidateRemoteAs{true},
          std::nullopt, // preRouteLimit
          std::nullopt, // postRouteLimit
          allowLoopbackReflection,
          EnableStatefulHa{false},
          std::nullopt, // addPath
          V4OverV6Nexthop{true}),
      evb_,
      ribInQ_,
      observerQ_,
      std::make_shared<folly::coro::Baton>(),
      policy,
      std::make_shared<std::atomic<bool>>(false) /* isSafeModeOn */,
      ingressPolicyName,
      egressPolicyName,
      adjRibOutGroup,
      std::nullopt /* outDelay */,
      config_ ? std::make_shared<ConfigManager>(config_) : nullptr);
  adjRib_->remoteAs_ = remoteAs;

  if (sessionEstablish) {
    fm_->addTask([&,
                  isAfiIpv4Negotiated,
                  isAfiIpv6Negotiated,
                  isV4OverV6Nexthop,
                  isEnhancedRouteRefreshNegotiated,
                  isRouteRefreshNegotiated,
                  addPathCapa] {
      /*
       * boundedAdjRibOutQ_ can be closed at the end of session terminated
       * and will no longer accept messages.
       *
       * Whenever we set up the adjRib, we should always provide a fresh queue.
       */
      boundedAdjRibOutQ_ = std::make_shared<AdjRib::AdjRibOutQueueT>(
          nettools::bgplib::kMaxEgressQueueSize,
          nettools::bgplib::kEgressQueueHighWatermark,
          nettools::bgplib::kEgressQueueLowWatermark);

      adjRib_->sessionEstablished(
          std::nullopt, // remoteGrRestartTime
          adjRibInQ_,
          boundedAdjRibOutQ_,
          isAfiIpv4Negotiated,
          isAfiIpv6Negotiated,
          isV4OverV6Nexthop,
          isEnhancedRouteRefreshNegotiated,
          isRouteRefreshNegotiated,
          addPathCapa);
      adjRib_->startMessageProcessingLoop();
    });
  }
}
/**
 * @parameter: sendAddPath: AdjRib's sendAddPath_ capability
 */
void AdjRibOutboundFixture::setupAdjRib(const bool sendAddPath) {
  std::optional<nettools::bgplib::BgpAddPathSendRec> addPath{std::nullopt};
  if (sendAddPath) {
    addPath = facebook::nettools::bgplib::BgpAddPathSendRec::SEND;
  }
  /*
   * Calling the overload setupAdjRib(...) by passing the peerAddr and addPath
   * capability
   */
  setupAdjRib(
      kLocalAs1,
      kLocalAs1,
      kRemoteAs1,
      false,
      false,
      NextHopSelfConfigured(false),
      kV4Nexthop1,
      kV6Nexthop1,
      true,
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      V4OverV6Nexthop(false),
      EnhancedRouteRefreshNegotiated(false),
      RouteRefreshNegotiated(false),
      RemovePrivateAsConfigured(false),
      nullptr,
      std::nullopt,
      kPeerAddr1,
      false,
      addPath);
}

void AdjRibOutboundFixture::setupAdjRib(
    const nettools::bgplib::BgpPeerId& peerId,
    const uint32_t remoteAs) {
  // Calling the overload setupAdjRib(...) by passing the peerId and remoteAs
  setupAdjRib(
      kLocalAs1,
      kLocalAs1,
      remoteAs,
      false,
      false,
      NextHopSelfConfigured(false),
      kV4Nexthop1,
      kV6Nexthop1,
      false, /*session established*/
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      V4OverV6Nexthop(false),
      EnhancedRouteRefreshNegotiated(false),
      RouteRefreshNegotiated(false),
      RemovePrivateAsConfigured(false),
      nullptr,
      std::nullopt,
      peerId,
      false,
      std::nullopt /* addpath */);
}

void AdjRibOutboundFixture::setupAdjRib(
    const std::shared_ptr<PolicyManager>& policy,
    const std::optional<std::string>& egressPolicyName,
    const bool sessionEstablish,
    const bool sendAddPath) {
  std::optional<nettools::bgplib::BgpAddPathSendRec> addPath{std::nullopt};
  if (sendAddPath) {
    addPath = facebook::nettools::bgplib::BgpAddPathSendRec::SEND;
  }
  setupAdjRib(
      kLocalAs1,
      kLocalAs1,
      kRemoteAs1,
      false, // isRrClient
      false, // isConfedPeer
      NextHopSelfConfigured(false),
      kV4Nexthop1,
      kV6Nexthop1,
      sessionEstablish, // sessionEstablish
      AfiIpv4Negotiated(true),
      AfiIpv6Negotiated(true),
      V4OverV6Nexthop(false),
      EnhancedRouteRefreshNegotiated(false),
      RouteRefreshNegotiated(false),
      RemovePrivateAsConfigured(false),
      policy,
      egressPolicyName,
      kPeerAddr1, // peerAddr
      false /* allowLoopbackReflection */,
      addPath);
}

void AdjRibOutboundFixture::setupAdjRibForOutUnitTest() {
  setupAdjRib(
      kLocalAs2,
      kLocalAs2,
      kRemoteAs1,
      kIsRrClientFalse,
      kIsConfedPeerFalse,
      NextHopSelfConfigured(false),
      kV4Nexthop1,
      kV6Nexthop1,
      false // sessionEstablish
  );
}

RibOutPayload AdjRibOutboundFixture::buildAnnouncementFromMap(
    const folly::F14NodeMap<
        std::shared_ptr<BgpPath>,
        std::vector<folly::CIDRNetwork>>& entries,
    bool ebgpPeer,
    bool initialDump,
    bool sendWithEoR) {
  RibOutAnnouncement ribMsg;
  ribMsg.initialDump = initialDump;
  ribMsg.sendWithEoR = sendWithEoR;

  auto& peerInfo = ebgpPeer ? eBgpPeer_ : iBgpPeer_;
  for (auto& [attrs, pfxs] : entries) {
    for (auto& pfx : pfxs) {
      ribMsg.entries.emplace_back(pfx, kDefaultPathID, peerInfo, attrs);
    }
  }
  return RibOutPayload(ribMsg);
}

// RIB update with prefilled attributes and multiple v4 or v6 announced prefix
RibOutPayload createRibMultipleAnnounce(
    const std::vector<folly::CIDRNetwork>& prefixes,
    const folly::IPAddress& nexthop,
    const TinyPeerInfo& peer,
    bool sendWithEoR) {
  RibOutAnnouncement ribMsg;
  facebook::nettools::bgplib::BgpUpdate2 update =
      buildBgpUpdateAttributes(nexthop);
  auto attrs = std::make_shared<facebook::bgp::BgpPath>(
      BgpPathFields(*BgpUpdate2toBgpPathC(update)));
  attrs->publish();

  for (const auto& prefix : prefixes) {
    ribMsg.entries.emplace_back(prefix, kDefaultPathID, peer, attrs);
  }
  if (sendWithEoR) {
    ribMsg.initialDump = true;
  }
  ribMsg.sendWithEoR = sendWithEoR;
  return ribMsg;
}

// Verification helper functions
namespace {

bool checkPrefixMatch(
    const nettools::bgplib::RiggedIPPrefix& riggedPrefix,
    const folly::CIDRNetwork& expectedCidr,
    uint32_t addPathId) {
  const auto actualCidr =
      facebook::network::toCIDRNetwork(*riggedPrefix.prefix());
  if (actualCidr != expectedCidr) {
    return false;
  }

  if (addPathId != 0) {
    return riggedPrefix.pathId().has_value() &&
        *riggedPrefix.pathId() == addPathId;
  }
  return true;
}

bool searchPrefixesInList(
    const std::vector<nettools::bgplib::RiggedIPPrefix>& prefixes,
    const folly::CIDRNetwork& expectedCidr,
    uint32_t addPathId) {
  for (const auto& riggedPrefix : prefixes) {
    if (checkPrefixMatch(riggedPrefix, expectedCidr, addPathId)) {
      return true;
    }
  }
  return false;
}

bool verifyNexthop(
    const nettools::bgplib::BgpUpdate2& update,
    const std::string& expectedNexthop) {
  const auto& attrs = update.attrs();

  /* Check old-style NEXT_HOP attribute (for non-multiprotocol BGP) */
  if (!attrs->nexthop()->empty()) {
    XLOGF(
        DBG2,
        "verifyNexthop: old-style nexthop = {}",
        attrs->nexthop().value());
    if (attrs->nexthop().value() == expectedNexthop) {
      XLOG(DBG2, "verifyNexthop: MATCHED via old-style NEXT_HOP");
      return true;
    }
  }

  /* Check MP_REACH_NLRI nexthop (for multiprotocol BGP) */
  if (!update.mpAnnounced()->nexthop()->addr()->empty()) {
    try {
      auto nexthopBytes = update.mpAnnounced()->nexthop()->addr().value();
      auto nexthopIp = folly::IPAddress::fromBinary(
          folly::ByteRange(
              reinterpret_cast<const unsigned char*>(nexthopBytes.data()),
              nexthopBytes.size()));
      XLOGF(
          DBG2,
          "verifyNexthop: MP nexthop = {}, expected = {}",
          nexthopIp.str(),
          expectedNexthop);
      if (nexthopIp.str() == expectedNexthop) {
        XLOG(DBG2, "verifyNexthop: MATCHED via MP_REACH_NLRI");
        return true;
      }
    } catch (const std::exception&) {
      XLOG(WARN, "verifyNexthop: Failed to parse MP nexthop");
    }
  }

  XLOGF(WARN, "verifyNexthop: NO MATCH for expected={}", expectedNexthop);
  return false;
}

std::vector<uint32_t> extractAsPathFromUpdate(
    const nettools::bgplib::BgpUpdate2& update) {
  std::vector<uint32_t> actualAsPathSeq;
  for (const auto& segment : update.attrs()->asPath().value()) {
    /*
     * Check if asSequence has data. For parsed BGP updates from wire format,
     * the thrift internal tracking may not be set correctly, so we check
     * if the vector actually has elements rather than relying on
     * is_non_optional_field_set_manually_or_by_serializer.
     */
    if (!segment.asSequence()->empty()) {
      for (const auto& asn : *segment.asSequence()) {
        actualAsPathSeq.push_back(static_cast<uint32_t>(asn));
      }
    }
  }
  return actualAsPathSeq;
}

bool verifyAsPath(
    const nettools::bgplib::BgpUpdate2& update,
    const std::string& expectedAsPath) {
  if (expectedAsPath.empty()) {
    return true;
  }

  if (update.attrs()->asPath()->empty()) {
    return false;
  }

  const auto expectedAsPathSeq = parseAsPath(expectedAsPath);
  const auto actualAsPathSeq = extractAsPathFromUpdate(update);

  return actualAsPathSeq == expectedAsPathSeq;
}

std::vector<std::string> extractCommunitiesFromUpdate(
    const nettools::bgplib::BgpUpdate2& update) {
  std::vector<std::string> actualCommunities;
  for (const auto& comm : update.attrs()->communities().value()) {
    actualCommunities.push_back(
        folly::to<std::string>(comm.asn().value()) + ":" +
        folly::to<std::string>(comm.value().value()));
  }
  std::sort(actualCommunities.begin(), actualCommunities.end());
  return actualCommunities;
}

bool verifyCommunities(
    const nettools::bgplib::BgpUpdate2& update,
    const std::string& expectedCommunity) {
  if (expectedCommunity.empty()) {
    return true;
  }

  if (update.attrs()->communities()->empty()) {
    return false;
  }

  auto expectedCommunities = parseCommunities(expectedCommunity);
  std::sort(expectedCommunities.begin(), expectedCommunities.end());

  const auto actualCommunities = extractCommunitiesFromUpdate(update);

  return actualCommunities == expectedCommunities;
}

} // namespace

std::vector<uint32_t> parseAsPath(const std::string& asPath) {
  if (asPath.empty()) {
    return {};
  }

  folly::small_vector<std::string_view, 8> tokens;
  folly::split(' ', asPath, tokens, true);

  std::vector<uint32_t> asPathSeq;
  asPathSeq.reserve(tokens.size());
  for (const auto& token : tokens) {
    asPathSeq.push_back(folly::to<uint32_t>(token));
  }
  return asPathSeq;
}

std::vector<std::string> parseCommunities(const std::string& community) {
  if (community.empty()) {
    return {};
  }

  folly::small_vector<std::string_view, 8> tokens;
  folly::split(' ', community, tokens, true);

  std::vector<std::string> communities;
  communities.reserve(tokens.size());
  for (const auto& token : tokens) {
    communities.emplace_back(token);
  }
  return communities;
}

bool isEoRUpdate(const nettools::bgplib::BgpUpdate2& update) {
  return update.v4Announced2()->empty() && update.v4Withdrawn2()->empty() &&
      update.mpAnnounced()->prefixes()->empty() &&
      update.mpWithdrawn()->prefixes()->empty();
}

bool findPrefixInAnnouncements(
    const nettools::bgplib::BgpUpdate2& update,
    bool isV4,
    const folly::CIDRNetwork& expectedCidr,
    uint32_t addPathId) {
  if (isV4 &&
      searchPrefixesInList(*update.v4Announced2(), expectedCidr, addPathId)) {
    return true;
  }
  return searchPrefixesInList(
      *update.mpAnnounced()->prefixes(), expectedCidr, addPathId);
}

bool findPrefixInWithdrawals(
    const nettools::bgplib::BgpUpdate2& update,
    bool isV4,
    const folly::CIDRNetwork& expectedCidr,
    uint32_t addPathId) {
  if (isV4 &&
      searchPrefixesInList(*update.v4Withdrawn2(), expectedCidr, addPathId)) {
    return true;
  }
  return searchPrefixesInList(
      *update.mpWithdrawn()->prefixes(), expectedCidr, addPathId);
}

std::vector<folly::CIDRNetwork> getAnnouncedPrefixes(
    const nettools::bgplib::BgpUpdate2& update) {
  std::vector<folly::CIDRNetwork> prefixes;
  for (const auto& riggedPrefix : *update.v4Announced2()) {
    prefixes.push_back(
        facebook::network::toCIDRNetwork(*riggedPrefix.prefix()));
  }
  for (const auto& riggedPrefix : *update.mpAnnounced()->prefixes()) {
    prefixes.push_back(
        facebook::network::toCIDRNetwork(*riggedPrefix.prefix()));
  }
  return prefixes;
}

std::vector<folly::CIDRNetwork> getWithdrawnPrefixes(
    const nettools::bgplib::BgpUpdate2& update) {
  std::vector<folly::CIDRNetwork> prefixes;
  for (const auto& riggedPrefix : *update.v4Withdrawn2()) {
    prefixes.push_back(
        facebook::network::toCIDRNetwork(*riggedPrefix.prefix()));
  }
  for (const auto& riggedPrefix : *update.mpWithdrawn()->prefixes()) {
    prefixes.push_back(
        facebook::network::toCIDRNetwork(*riggedPrefix.prefix()));
  }
  return prefixes;
}

bool verifyRouteAttributes(
    const nettools::bgplib::BgpUpdate2& update,
    const std::string& expectedNexthop,
    const std::string& expectedAsPath,
    const std::string& expectedCommunity) {
  bool nhMatch = verifyNexthop(update, expectedNexthop);
  bool asMatch = verifyAsPath(update, expectedAsPath);
  bool commMatch = verifyCommunities(update, expectedCommunity);
  return nhMatch && asMatch && commMatch;
}

// RIB update for single v4 or v6 withdrawal prefix
RibOutPayload createRibSingleWithdrawal(const folly::CIDRNetwork& prefix) {
  RibOutWithdrawal ribMsg;
  ribMsg.entries.emplace_back(prefix, kDefaultPathID);
  return ribMsg;
}

// RIB update for single v4 or v6 withdrawal prefix
RibOutPayload createRibSingleWithdrawalForAddPath(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& nh,
    const uint32_t pathIdToSend) {
  RibOutWithdrawal ribMsg;
  ribMsg.addPathEntries.emplace_back(prefix, pathIdToSend, nh);
  return ribMsg;
}

} // namespace facebook::bgp
