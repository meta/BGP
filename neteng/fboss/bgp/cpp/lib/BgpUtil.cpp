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

#include <boost/functional/hash.hpp>
#include <common/network/AddressUtil.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include "neteng/fboss/bgp/cpp/lib/BgpUtil.h"

using folly::IPAddress;

namespace std {

/**
 * Make BgpAttributes hashable
 */

size_t hash<facebook::nettools::bgplib::BgpAttributes>::operator()(
    facebook::nettools::bgplib::BgpAttributes const& attrs) const {
  size_t seed = 0;
  boost::hash_combine(seed, *attrs.origin());
  boost::hash_range(seed, attrs.asPath()->begin(), attrs.asPath()->end());
  boost::hash_combine(seed, *attrs.nexthop());
  boost::hash_combine(seed, *attrs.med());
  boost::hash_combine(seed, *attrs.isMedSet());
  if (attrs.localPref()) {
    boost::hash_combine(seed, *attrs.localPref());
  }
  boost::hash_combine(seed, *attrs.atomicAggregate());
  boost::hash_combine(seed, *attrs.aggregator());
  boost::hash_range(
      seed, attrs.communities()->begin(), attrs.communities()->end());
  boost::hash_combine(seed, *attrs.originatorId());
  boost::hash_range(
      seed, attrs.clusterList()->begin(), attrs.clusterList()->end());
  boost::hash_range(
      seed, attrs.extCommunities()->begin(), attrs.extCommunities()->end());
  boost::hash_combine(seed, *attrs.weight());
  return seed;
}

} // namespace std

namespace facebook {
namespace nettools {
namespace bgplib {
const std::string kNullMessage = "(NA)";
// make BgpAttrAsPathSegment hashable
std::size_t hash_value(BgpAttrAsPathSegment const& seg) {
  // return boost::hash<string>(attrs.nexthop);
  size_t seed = 0;
  boost::hash_range(seed, seg.asSet()->begin(), seg.asSet()->end());
  boost::hash_range(seed, seg.asSequence()->begin(), seg.asSequence()->end());
  boost::hash_range(
      seed, seg.asConfedSequence()->begin(), seg.asConfedSequence()->end());
  boost::hash_range(seed, seg.asConfedSet()->begin(), seg.asConfedSet()->end());
  return seed;
}

// make BgpAttrAggregator hashable
std::size_t hash_value(BgpAttrAggregator const& agg) {
  size_t seed = 0;
  boost::hash_combine(seed, *agg.asn());
  boost::hash_combine(seed, *agg.ip());
  return seed;
}

// make BgpAttrCommunity hashable
std::size_t hash_value(BgpAttrCommunity const& comm) {
  size_t seed = 0;
  boost::hash_combine(seed, *comm.asn());
  boost::hash_combine(seed, *comm.value());
  return seed;
}

// make BgpAttrExtCommunity hashable
std::size_t hash_value(BgpAttrExtCommunity const& comm) {
  size_t seed = 0;
  boost::hash_combine(seed, *comm.firstWord());
  boost::hash_combine(seed, *comm.secondWord());
  return seed;
}

BgpUpdate2 toBgpUpdate2(const BgpUpdate& update, bool toSerialize) {
  BgpUpdate2 update2;
  *update2.attrs() = *update.attrs();
  // get prefix object from string
  auto pfix = network::toIPPrefix(IPAddress::createNetwork(*update.prefix()));
  RiggedIPPrefix rigPrf;
  *rigPrf.prefix() = pfix;
  if ((*update.afi() == BgpUpdateAfi::AFI_IPv6) ||
      (*update.safi() != BgpUpdateSafi::SAFI_UNICAST)) {
    // multi protocol
    BgpNlri nlri;
    *nlri.afi() = *update.afi();
    *nlri.safi() = *update.safi();
    RiggedIPPrefix rigPfix;
    *rigPfix.prefix() = pfix;
    *rigPfix.labels() = *update.labels();
    nlri.prefixes()->push_back(rigPfix);
    if (*update.type() == BgpUpdateType::BU_WITHDRAW) {
      *update2.mpWithdrawn() = nlri;
    } else {
      *update2.attrs()->nexthop() = "";
      *nlri.nexthop() = network::toBinaryAddress(
          folly::IPAddress(*update.attrs()->nexthop()));
      *update2.mpAnnounced() = nlri;
    }
  } else {
    /*
     * v4
     * TODO: retire v4Withdrawn and v4Announced once client gets migrated.
     */
    if (*update.type() == BgpUpdateType::BU_WITHDRAW) {
      update2.v4Withdrawn()->push_back(pfix);
      if (!toSerialize) {
        update2.v4Withdrawn2()->push_back(rigPrf);
      }
    } else {
      update2.v4Announced()->push_back(pfix);
      if (!toSerialize) {
        update2.v4Announced2()->push_back(rigPrf);
      }
      *update2.v4Nexthop() = network::toBinaryAddress(
          folly::IPAddress(*update.attrs()->nexthop()));
    }
  }
  return update2;
}

std::vector<BgpUpdate> toBgpUpdate(const BgpUpdate2& update2) {
  std::vector<BgpUpdate> updates{};
  /*
   * Since prefix can be populated in V4Announced2 or V4Withdraw2
   * along with V4Announced or V4Withdraw. We need to check both for backward
   * compatibility. Ex: https://fburl.com/code/u83fwceg
   */
  bool useV4Withdraw2 = true;
  bool useV4Announced2 = true;
  for (const auto& prefix : *update2.v4Withdrawn()) {
    BgpUpdate update;
    update.type() = BgpUpdateType::BU_WITHDRAW;
    update.afi() = BgpUpdateAfi::AFI_IPv4;
    update.safi() = BgpUpdateSafi::SAFI_UNICAST;
    update.prefix() =
        IPAddress::networkToString(network::toCIDRNetwork(prefix));
    update.peerIp() = "";
    update.attrs() = BgpAttributes();
    update.labels() = {};
    updates.push_back(std::move(update));
    useV4Withdraw2 = false;
  }

  for (const auto& riggedIPPrefix : *update2.mpWithdrawn()->prefixes()) {
    BgpUpdate update;
    update.type() = BgpUpdateType::BU_WITHDRAW;
    update.afi() = *update2.mpWithdrawn()->afi();
    update.safi() = *update2.mpWithdrawn()->safi();
    update.prefix() = IPAddress::networkToString(
        network::toCIDRNetwork(*riggedIPPrefix.prefix()));
    update.peerIp() = "";
    update.attrs() = BgpAttributes();
    update.labels() = *riggedIPPrefix.labels();
    updates.push_back(std::move(update));
  }

  for (const auto& riggedIPPrefix : *update2.mpAnnounced()->prefixes()) {
    auto attrs = *update2.attrs();
    attrs.nexthop() =
        network::toIPAddress(*update2.mpAnnounced()->nexthop()).str();
    BgpUpdate update;
    update.type() = BgpUpdateType::BU_UPDATE;
    update.afi() = *update2.mpAnnounced()->afi();
    update.safi() = *update2.mpAnnounced()->safi();
    update.prefix() = IPAddress::networkToString(
        network::toCIDRNetwork(*riggedIPPrefix.prefix()));
    update.peerIp() = "";
    update.attrs() = attrs;
    update.labels() = *riggedIPPrefix.labels();
    updates.push_back(std::move(update));
  }

  for (const auto& prefix : *update2.v4Announced()) {
    auto attrs = *update2.attrs();
    *attrs.nexthop() = network::toIPAddress(*update2.v4Nexthop()).str();
    BgpUpdate update;
    update.type() = BgpUpdateType::BU_UPDATE;
    update.afi() = BgpUpdateAfi::AFI_IPv4;
    update.safi() = BgpUpdateSafi::SAFI_UNICAST;
    update.prefix() =
        IPAddress::networkToString(network::toCIDRNetwork(prefix));
    update.peerIp() = "";
    update.attrs() = attrs;
    update.labels() = {};
    updates.push_back(std::move(update));
    useV4Announced2 = false;
  }
  if (useV4Announced2) {
    for (const auto& riggedIPPrefix : *update2.v4Announced2()) {
      BgpUpdate update;
      update.type() = BgpUpdateType::BU_UPDATE;
      update.afi() = BgpUpdateAfi::AFI_IPv4;
      update.safi() = BgpUpdateSafi::SAFI_UNICAST;
      update.prefix() = IPAddress::networkToString(
          network::toCIDRNetwork(*riggedIPPrefix.prefix()));
      update.peerIp() = "";
      update.attrs() = *update2.attrs();
      update.attrs()->nexthop() =
          network::toIPAddress(*update2.v4Nexthop()).str();
      update.labels() = *riggedIPPrefix.labels();
      updates.push_back(std::move(update));
    }
  }
  if (useV4Withdraw2) {
    for (const auto& riggedIPPrefix : *update2.v4Withdrawn2()) {
      BgpUpdate update;
      update.type() = BgpUpdateType::BU_WITHDRAW;
      update.afi() = BgpUpdateAfi::AFI_IPv4;
      update.safi() = BgpUpdateSafi::SAFI_UNICAST;
      update.prefix() = IPAddress::networkToString(
          network::toCIDRNetwork(*riggedIPPrefix.prefix()));
      update.peerIp() = "";
      update.attrs() = BgpAttributes();
      update.labels() = *riggedIPPrefix.labels();
      updates.push_back(std::move(update));
    }
  }
  return updates;
}

BgpUpdate toBgpUpdate(const BgpEndOfRib& eor) {
  BgpUpdate update;
  *update.type() = BgpUpdateType::BU_ENDOFRIB;
  *update.afi() = *eor.isMpEor() ? *eor.afi() : BgpUpdateAfi::AFI_IPv4;
  *update.safi() = *eor.isMpEor() ? *eor.safi() : BgpUpdateSafi::SAFI_UNICAST;
  return update;
}

std::vector<BgpUpdate> toBgpUpdate(
    const std::variant<BgpUpdate2, BgpEndOfRib>& update) {
  if (std::holds_alternative<BgpUpdate2>(update)) {
    // convert BgpUpdate2
    return toBgpUpdate(std::get<BgpUpdate2>(update));
  } else {
    // convert BgpEndOfRib
    return std::vector<BgpUpdate>{toBgpUpdate(std::get<BgpEndOfRib>(update))};
  }
}

std::vector<BgpUpdate> toBgpUpdate(
    const std::variant<std::shared_ptr<const BgpUpdate2>, BgpEndOfRib>&
        update) {
  if (std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(update)) {
    // convert shared_ptr<const BgpUpdate2> - dereference the pointer
    return toBgpUpdate(*std::get<std::shared_ptr<const BgpUpdate2>>(update));
  } else {
    // convert BgpEndOfRib
    return std::vector<BgpUpdate>{toBgpUpdate(std::get<BgpEndOfRib>(update))};
  }
}

/*
 * Create a shared pointer using update.attrs
 * Converts from thrift struture to CPP structure for optimal storage and
 * performance
 * NOTE: As BgpUpdate2 can have both V4/V6 nexthop, currently we are returning
 *       only V4 nexthop filled. We need to see if we want to return a vector
 *       or pair if both V4 and V6 nexthops are present or let user request
 *       which nexthop must be filled.
 */
std::shared_ptr<BgpAttributesC> BgpUpdate2toBgpAttributesC(
    const BgpUpdate2& update) {
  return bgpAttributesToBgpAttributesC(*update.attrs());
}

std::shared_ptr<BgpPathC> BgpUpdate2toBgpPathC(const BgpUpdate2& update) {
  return bgpAttributesToBgpPathC(*update.attrs());
}

// convert thrift BgpAttributes to CPP BgpAttributes
std::shared_ptr<BgpAttributesC> bgpAttributesToBgpAttributesC(
    const BgpAttributes& attrs) {
  auto attrsC = std::make_shared<BgpAttributesC>();

  attrsC->origin = *attrs.origin();
  BgpAttrAsPathC asPathC;
  asPathC.reserve(attrs.asPath()->size());
  for (const auto& seg : *attrs.asPath()) {
    BgpAttrAsPathSegmentC segC;

    std::set<uint32_t> asSet(seg.asSet()->begin(), seg.asSet()->end());
    std::vector<uint32_t> asSequence(
        seg.asSequence()->begin(), seg.asSequence()->end());
    std::vector<uint32_t> asConfedSequence(
        seg.asConfedSequence()->begin(), seg.asConfedSequence()->end());
    std::set<uint32_t> asConfedSet(
        seg.asConfedSet()->begin(), seg.asConfedSet()->end());

    segC.asSet = std::move(asSet);
    segC.asSequence = std::move(asSequence);
    segC.asConfedSequence = std::move(asConfedSequence);
    segC.asConfedSet = std::move(asConfedSet);

    asPathC.emplace_back(std::move(segC));
  }
  attrsC->asPath = std::move(asPathC);

  attrsC->med = (uint32_t)*attrs.med();
  attrsC->isMedSet = *attrs.isMedSet();
  attrsC->localPref = attrs.localPref().has_value()
      ? std::optional<uint32_t>(*attrs.localPref())
      : std::nullopt;
  attrsC->atomicAggregate = *attrs.atomicAggregate();
  attrsC->aggregator.asn = (uint32_t)*attrs.aggregator()->asn();
  if (!attrs.aggregator()->ip()->empty()) {
    attrsC->aggregator.ip = folly::IPAddress(*attrs.aggregator()->ip());
  }

  BgpAttrCommunitiesC communitiesC;
  communitiesC.reserve(attrs.communities()->size());
  for (const auto& comm : *attrs.communities()) {
    BgpAttrCommunityC commC;
    commC.asn = (uint16_t)*comm.asn();
    commC.value = (uint16_t)*comm.value();
    communitiesC.emplace_back(commC);
  }
  attrsC->communities = std::move(communitiesC);

  /*
   * bgplib::BgpAttributes stores originatorId and clusterList in network byte
   * order, but bgplib::BgpAttributesC stores them in host byte order like other
   * fields.
   */
  attrsC->originatorId = ntohl(*attrs.originatorId());

  BgpAttrClusterListC clusterListC;
  clusterListC.reserve(attrs.clusterList()->size());
  for (const auto& cluster : *attrs.clusterList()) {
    clusterListC.emplace_back(ntohl(cluster));
  }
  attrsC->clusterList = std::move(clusterListC);

  BgpAttrExtCommunitiesC extCommunitiesC;
  for (const auto& extComm : *attrs.extCommunities()) {
    extCommunitiesC.emplace_back(
        (uint32_t)*extComm.firstWord(), (uint32_t)*extComm.secondWord());
  }
  attrsC->extCommunities = std::move(extCommunitiesC);
  attrsC->weight = (uint16_t)*attrs.weight();
  return attrsC;
}

std::shared_ptr<BgpPathC> bgpAttributesToBgpPathC(const BgpAttributes& attrs) {
  auto pathC = std::make_shared<BgpPathC>();

  BgpAttributesC attrsC;

  attrsC.origin = *attrs.origin();
  BgpAttrAsPathC asPathC;
  asPathC.reserve(attrs.asPath()->size());
  for (const auto& seg : *attrs.asPath()) {
    BgpAttrAsPathSegmentC segC;

    std::set<uint32_t> asSet(seg.asSet()->begin(), seg.asSet()->end());
    std::vector<uint32_t> asSequence(
        seg.asSequence()->begin(), seg.asSequence()->end());
    std::vector<uint32_t> asConfedSequence(
        seg.asConfedSequence()->begin(), seg.asConfedSequence()->end());
    std::set<uint32_t> asConfedSet(
        seg.asConfedSet()->begin(), seg.asConfedSet()->end());

    segC.asSet = std::move(asSet);
    segC.asSequence = std::move(asSequence);
    segC.asConfedSequence = std::move(asConfedSequence);
    segC.asConfedSet = std::move(asConfedSet);

    asPathC.emplace_back(std::move(segC));
  }
  attrsC.asPath = std::move(asPathC);

  if (!attrs.nexthop()->empty()) {
    pathC->nexthop = folly::IPAddress(*attrs.nexthop());
  }
  attrsC.med = (uint32_t)*attrs.med();
  attrsC.isMedSet = *attrs.isMedSet();
  attrsC.localPref = attrs.localPref().has_value()
      ? std::optional<uint32_t>(*attrs.localPref())
      : std::nullopt;
  attrsC.atomicAggregate = *attrs.atomicAggregate();
  attrsC.aggregator.asn = (uint32_t)*attrs.aggregator()->asn();
  if (!attrs.aggregator()->ip()->empty()) {
    attrsC.aggregator.ip = folly::IPAddress(*attrs.aggregator()->ip());
  }

  BgpAttrCommunitiesC communitiesC;
  communitiesC.reserve(attrs.communities()->size());
  for (const auto& comm : *attrs.communities()) {
    BgpAttrCommunityC commC;
    commC.asn = (uint16_t)*comm.asn();
    commC.value = (uint16_t)*comm.value();
    communitiesC.emplace_back(commC);
  }
  attrsC.communities = std::move(communitiesC);

  /*
   * bgplib::BgpAttributes stores originatorId and clusterList in network byte
   * order, but bgplib::BgpAttributesC stores them in host byte order like other
   * fields.
   */
  attrsC.originatorId = ntohl(*attrs.originatorId());

  BgpAttrClusterListC clusterListC;
  clusterListC.reserve(attrs.clusterList()->size());
  for (const auto& cluster : *attrs.clusterList()) {
    clusterListC.emplace_back(ntohl(cluster));
  }
  attrsC.clusterList = std::move(clusterListC);

  BgpAttrExtCommunitiesC extCommunitiesC;
  for (const auto& extComm : *attrs.extCommunities()) {
    extCommunitiesC.emplace_back(
        (uint32_t)*extComm.firstWord(), (uint32_t)*extComm.secondWord());
  }
  attrsC.extCommunities = std::move(extCommunitiesC);
  attrsC.weight = (uint32_t)*attrs.weight();
  pathC->attrs = std::move(attrsC);

  return pathC;
}

/*
 * Populate BgpUpdate2.attrs from BgpAttributesC
 * Note: User has to fill all other fields in BgpUpdate2
 */
BgpUpdate2 BgpAttributesCtoBgpUpdate2(
    std::shared_ptr<const BgpAttributesC> attrs) {
  CHECK(attrs != nullptr) << "Passed attrs null pointer";

  BgpUpdate2 update;
  *update.attrs() = bgpAttributesCtoBgpAttributes(attrs);
  return update;
}

BgpUpdate2 BgpPathCtoBgpUpdate2(std::shared_ptr<const BgpPathC> attrs) {
  CHECK(attrs != nullptr) << "Passed attrs null pointer";

  BgpUpdate2 update;
  *update.attrs() = bgpPathCtoBgpAttributes(attrs);
  return update;
}

// convert CPP BgpAttributes to thrift BgpAttributes
BgpAttributes bgpAttributesCtoBgpAttributes(
    std::shared_ptr<const BgpAttributesC> attrs) {
  CHECK(attrs != nullptr) << "Passed attrs null pointer";

  BgpAttributes attrsT; // thrift BgpAttributes

  *attrsT.origin() = attrs->origin;

  for (const auto& seg : attrs->asPath.get()) {
    BgpAttrAsPathSegment segT;

    std::set<int64_t> asSet(seg.asSet.begin(), seg.asSet.end());
    std::vector<int64_t> asSequence(
        seg.asSequence.begin(), seg.asSequence.end());
    std::vector<int64_t> asConfedSequence(
        seg.asConfedSequence.begin(), seg.asConfedSequence.end());
    std::set<int64_t> asConfedSet(
        seg.asConfedSet.begin(), seg.asConfedSet.end());

    *segT.asSet() = asSet;
    *segT.asSequence() = asSequence;
    *segT.asConfedSequence() = asConfedSequence;
    *segT.asConfedSet() = asConfedSet;

    attrsT.asPath()->emplace_back(segT);
  }

  attrsT.med() = attrs->med;
  attrsT.isMedSet() = attrs->isMedSet;
  if (attrs->localPref) {
    attrsT.localPref() = (*attrs->localPref);
  }
  *attrsT.atomicAggregate() = attrs->atomicAggregate;
  *attrsT.aggregator()->asn() = attrs->aggregator.asn;
  if (!attrs->aggregator.ip.empty()) {
    *attrsT.aggregator()->ip() = attrs->aggregator.ip.str();
  } else {
    *attrsT.aggregator()->ip() = "";
  }

  for (const auto& comm : attrs->communities.get()) {
    BgpAttrCommunity commT;
    *commT.asn() = comm.asn;
    *commT.value() = comm.value;
    attrsT.communities()->emplace_back(commT);
  }

  *attrsT.originatorId() = htonl(attrs->originatorId);
  for (const auto& cluster : attrs->clusterList.get()) {
    attrsT.clusterList()->emplace_back(htonl(cluster));
  }

  for (const auto& extComm : attrs->extCommunities.get()) {
    BgpAttrExtCommunity extCommT;
    std::tie(*extCommT.firstWord(), *extCommT.secondWord()) =
        extComm.getRawValueInWords();
    attrsT.extCommunities()->emplace_back(extCommT);
  }
  attrsT.weight() = attrs->weight;
  return attrsT;
}

// convert CPP BgpAttributes to thrift BgpAttributes
BgpAttributes bgpPathCtoBgpAttributes(std::shared_ptr<const BgpPathC> path) {
  CHECK(path != nullptr) << "Passed path null pointer";

  BgpAttributes attrsT; // thrift BgpAttributes

  auto attrsC = path->attrs.get();

  *attrsT.origin() = attrsC.origin;

  for (const auto& seg : attrsC.asPath.get()) {
    BgpAttrAsPathSegment segT;

    std::set<int64_t> asSet(seg.asSet.begin(), seg.asSet.end());
    std::vector<int64_t> asSequence(
        seg.asSequence.begin(), seg.asSequence.end());
    std::vector<int64_t> asConfedSequence(
        seg.asConfedSequence.begin(), seg.asConfedSequence.end());
    std::set<int64_t> asConfedSet(
        seg.asConfedSet.begin(), seg.asConfedSet.end());

    *segT.asSet() = asSet;
    *segT.asSequence() = asSequence;
    *segT.asConfedSequence() = asConfedSequence;
    *segT.asConfedSet() = asConfedSet;

    attrsT.asPath()->emplace_back(segT);
  }

  if (!path->nexthop.empty()) {
    *attrsT.nexthop() = path->nexthop.str();
  } else {
    *attrsT.nexthop() = "";
  }
  attrsT.med() = attrsC.med;
  attrsT.isMedSet() = attrsC.isMedSet;
  if (attrsC.localPref) {
    attrsT.localPref() = (*attrsC.localPref);
  }
  *attrsT.atomicAggregate() = attrsC.atomicAggregate;
  *attrsT.aggregator()->asn() = attrsC.aggregator.asn;
  if (!attrsC.aggregator.ip.empty()) {
    *attrsT.aggregator()->ip() = attrsC.aggregator.ip.str();
  } else {
    *attrsT.aggregator()->ip() = "";
  }

  for (const auto& comm : attrsC.communities.get()) {
    BgpAttrCommunity commT;
    *commT.asn() = comm.asn;
    *commT.value() = comm.value;
    attrsT.communities()->emplace_back(commT);
  }

  *attrsT.originatorId() = htonl(attrsC.originatorId);
  for (const auto& cluster : attrsC.clusterList.get()) {
    attrsT.clusterList()->emplace_back(htonl(cluster));
  }

  for (const auto& extComm : attrsC.extCommunities.get()) {
    BgpAttrExtCommunity extCommT;
    std::tie(*extCommT.firstWord(), *extCommT.secondWord()) =
        extComm.getRawValueInWords();
    attrsT.extCommunities()->emplace_back(extCommT);
  }
  attrsT.weight() = attrsC.weight;
  return attrsT;
}

void negotiateBgpAddPathCapabilities(
    BgpCapabilities& ngtCapa,
    const std::vector<BgpAddPathCapability>& myCapas,
    const std::vector<BgpAddPathCapability>& peerCapas) {
  if (myCapas.empty()) {
    return;
  }

  for (const auto& myCapa : myCapas) {
    for (const auto& peerCapa : peerCapas) {
      if (*myCapa.afi() == *peerCapa.afi() &&
          *myCapa.safi() == *peerCapa.safi()) {
        auto negotiatedAddPathCapability = BgpAddPathCapability();
        *negotiatedAddPathCapability.afi() = *myCapa.afi();
        *negotiatedAddPathCapability.safi() = *myCapa.safi();

        switch (*peerCapa.sor()) {
          // if peer can do both. Then it is upto our capability.
          case BgpAddPathSendRec::BOTH:
            *negotiatedAddPathCapability.sor() = *myCapa.sor();
            ngtCapa.addPathCapabilities()->emplace_back(
                negotiatedAddPathCapability);
            break;
          // if peer can send. Then we have to be able to do BOTH or RECEIVE
          case BgpAddPathSendRec::SEND:
            if (*myCapa.sor() == BgpAddPathSendRec::RECEIVE ||
                *myCapa.sor() == BgpAddPathSendRec::BOTH) {
              *negotiatedAddPathCapability.sor() = BgpAddPathSendRec::RECEIVE;
              ngtCapa.addPathCapabilities()->emplace_back(
                  negotiatedAddPathCapability);
            }
            break;
          // if peer can RECEIVE. Then we have to be able to do BOTH or SEND
          case BgpAddPathSendRec::RECEIVE:
            if (*myCapa.sor() == BgpAddPathSendRec::SEND ||
                *myCapa.sor() == BgpAddPathSendRec::BOTH) {
              *negotiatedAddPathCapability.sor() = BgpAddPathSendRec::SEND;
              ngtCapa.addPathCapabilities()->emplace_back(
                  negotiatedAddPathCapability);
            }
            break;
        }
        XLOGF(
            INFO,
            "Negotiate complete afi: {} safi: {}, my add_path capability: {}, "
            "peer add_path capability : {}. negotiated add path capability: "
            "{} ",
            apache::thrift::util::enumNameSafe(*myCapa.afi()),
            apache::thrift::util::enumNameSafe(*myCapa.safi()),
            apache::thrift::util::enumNameSafe(*myCapa.sor()),
            apache::thrift::util::enumNameSafe(*peerCapa.sor()),
            apache::thrift::util::enumNameSafe(
                *negotiatedAddPathCapability.sor()));
        break;
      }
    }
  }
}

BgpCapabilities negotiateCapabilities(
    const BgpCapabilities& myCapa,
    const BgpCapabilities& peerCapa) {
  auto result = myCapa; // start with my capabilities
  // if no multi-protocol capability is sent in open message, we assume peer
  // support v4
  result.mpExtV4Unicast().value() &=
      (*peerCapa.mpExtV4Unicast() || !(*peerCapa.mpExtExist()));
  result.mpExtV6Unicast().value() &= *peerCapa.mpExtV6Unicast();
  result.mpExtV4LU().value() &= *peerCapa.mpExtV4LU();
  result.mpExtV6LU().value() &= *peerCapa.mpExtV6LU();
  result.mpExtLs().value() &= *peerCapa.mpExtLs();
  result.as4byte().value() &= *peerCapa.as4byte();
  result.gracefulRestart().value() &= *peerCapa.gracefulRestart();
  result.isRestarting() = *peerCapa.isRestarting();
  // RFC 8538 procedures apply only if both peers advertise the N bit
  result.grNotification().value() &= *peerCapa.grNotification();
  // Extended Next Hop Encoding capabilities, RFC 5549
  result.extNHEncodingCapabilities() = negotiateExtNHEncodingCapabilities(
      *myCapa.extNHEncodingCapabilities(),
      *peerCapa.extNHEncodingCapabilities());
  // RFC 2918 Route Refresh Capability
  result.routeRefresh().value() &= *peerCapa.routeRefresh();
  // RFC 7313 Enhanced Route Refresh Capability
  result.enhancedRouteRefresh().value() &= *peerCapa.enhancedRouteRefresh();

  result.addPathCapabilities()->clear();
  // we will repopulate the negotiated add path capability
  negotiateBgpAddPathCapabilities(
      result, *myCapa.addPathCapabilities(), *peerCapa.addPathCapabilities());
  return result;
}

std::vector<BgpExtNHEncodingCapability> negotiateExtNHEncodingCapabilities(
    const std::vector<BgpExtNHEncodingCapability>& myC,
    const std::vector<BgpExtNHEncodingCapability>& peerC) {
  auto myCapa = folly::copy(myC);
  auto peerCapa = folly::copy(peerC);
  std::vector<BgpExtNHEncodingCapability> ret{myCapa.size() + peerCapa.size()};

  std::sort(myCapa.begin(), myCapa.end());
  std::sort(peerCapa.begin(), peerCapa.end());
  auto it = std::set_intersection(
      myCapa.begin(),
      myCapa.end(),
      peerCapa.begin(),
      peerCapa.end(),
      ret.begin());
  ret.resize(it - ret.begin());
  return ret;
}

folly::CIDRNetwork strToNetwork(const std::string& netStr) {
  auto maybeNet = folly::IPAddress::tryCreateNetwork(netStr);
  if (maybeNet.hasError()) {
    InvalidAddress ex;
    *ex.prefix() = netStr;
    throw ex;
  }
  return *maybeNet;
}

bool isAristaDevice() {
  const std::vector<std::string> aristeDeviceRoles{
      "eb", // Express backbone router
      "leb", // Lab Express backbone router
  };

  const size_t kHostNameMaxLen = 256;
  char hostnameBuffer[kHostNameMaxLen];
  if (gethostname(hostnameBuffer, kHostNameMaxLen) == 0) {
    std::string hostname = hostnameBuffer;
    for (const auto& role : aristeDeviceRoles) {
      if (hostname.rfind(role, 0) != std::string::npos) {
        return true;
      }
    }
  }
  return false;
}

const std::vector<std::string> getCommunityDifference(
    std::vector<std::string> list,
    std::vector<std::string> combination) {
  std::sort(list.begin(), list.end());
  std::sort(combination.begin(), combination.end());

  std::vector<std::string> difference(list.size() + combination.size());

  const auto& it = std::set_difference(
      list.begin(),
      list.end(),
      combination.begin(),
      combination.end(),
      difference.begin());
  difference.resize(it - difference.begin());

  return difference;
}

const std::map<std::vector<std::string>, std::string> findCommunities(
    const std::vector<std::string>& communityList,
    std::map<std::set<std::string>, std::string>& communitySet) {
  if (communityList.size() == 1) {
    const auto& community = communitySet.find({communityList[0]});
    if (community != communitySet.end()) {
      return {{communityList, community->second}};
    } else {
      return {{communityList, kNullMessage}};
    }
  }

  /*
   * Each community alias can have a maximum of two communities
   * mapped in it's community set. In this case, we want to
   * compute the minimum number of combinations possible by taking the length
   * of our list to the fixed maximum lengh of the community set: 2.
   */
  for (int i = std::min((int)communityList.size(), 2); i > 0; --i) {
    const auto communityCombinations = getCombinations(communityList, i);
    for (const auto& combination : communityCombinations) {
      const std::set<std::string> combinationSet(
          combination.begin(), combination.end());
      if (combination.size() == 1) {
        std::string name = kNullMessage;
        if (const auto& it = communitySet.find(combinationSet);
            it != communitySet.end()) {
          name = it->second;
        }
        std::map<std::vector<std::string>, std::string> communityNames = {
            {combination, name}};

        const auto& newCommunityNames = findCommunities(
            getCommunityDifference(communityList, combination), communitySet);
        communityNames.insert(
            newCommunityNames.begin(), newCommunityNames.end());

        return communityNames;
      }

      if (combinationSet.size() == combination.size() &&
          (communitySet.find(combinationSet) != communitySet.end())) {
        std::map<std::vector<std::string>, std::string> communityNames = {
            {combination, communitySet.at(combinationSet)}};

        const auto& newCommunities =
            getCommunityDifference(communityList, combination);

        if (newCommunities.size() != 0) {
          const auto& newCommunityNames =
              findCommunities(newCommunities, communitySet);
          communityNames.insert(
              newCommunityNames.begin(), newCommunityNames.end());
        }
        return communityNames;
      }
    }
  }
  return {};
}

BgpNotification buildBgpNotification(
    const ::facebook::nettools::bgplib::BgpNotifErrCode& errCode,
    const ::std::int16_t& errSubCode,
    const ::std::string& errSubCodeStr,
    const ::std::string& data) {
  BgpNotification notification;
  notification.errCode() = errCode;
  notification.errSubCode() = errSubCode;
  notification.errSubCodeStr() = errSubCodeStr;
  notification.data() = data;
  return notification;
}

std::unique_ptr<BgpUpdate> constructAnnounceInBgpUpdateFormat(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& nexthop,
    const std::vector<uint32_t>& asPath,
    const std::vector<BgpAttrCommunity>& communities) {
  std::vector<int64_t> asPath2;
  asPath2.insert(asPath2.end(), asPath.begin(), asPath.end());

  auto update = std::make_unique<BgpUpdate>();
  update->type() = BgpUpdateType::BU_UPDATE;
  update->afi() =
      nexthop.isV4() ? BgpUpdateAfi::AFI_IPv4 : BgpUpdateAfi::AFI_IPv6;
  update->safi() = BgpUpdateSafi::SAFI_UNICAST;
  update->prefix() = folly::IPAddress::networkToString(prefix);

  BgpAttributes attrs;
  attrs.origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  if (!asPath2.empty()) {
    BgpAttrAsPathSegment segment;
    segment.asSequence() = std::move(asPath2);
    attrs.asPath() = {std::move(segment)};
  }
  attrs.nexthop() = nexthop.str();

  attrs.communities() = communities;
  update->attrs() = std::move(attrs);
  update->labels() = {};
  return update;
}

std::unique_ptr<BgpUpdate> constructWithdrawInBgpUpdateFormat(
    const folly::CIDRNetwork& prefix) {
  auto update = std::make_unique<BgpUpdate>();
  update->type() = BgpUpdateType::BU_WITHDRAW;
  update->afi() =
      prefix.first.isV4() ? BgpUpdateAfi::AFI_IPv4 : BgpUpdateAfi::AFI_IPv6;
  update->safi() = BgpUpdateSafi::SAFI_UNICAST;
  update->prefix() = folly::IPAddress::networkToString(prefix);
  return update;
}

std::unique_ptr<BgpUpdate2> constructAnnounceInBgpUpdate2Format(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& nexthop,
    const std::vector<uint32_t>& asPath,
    const std::vector<BgpAttrCommunity>& communities) {
  // create attrs
  BgpAttributes attrs;
  attrs.origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  std::vector<int64_t> asPath2;
  asPath2.insert(asPath2.end(), asPath.begin(), asPath.end());
  if (!asPath2.empty()) {
    BgpAttrAsPathSegment segment;
    segment.asSequence() = std::move(asPath2);
    attrs.asPath() = {std::move(segment)};
  }
  attrs.communities() = communities;
  attrs.nexthop() = "";

  // create update
  auto update = std::make_unique<BgpUpdate2>();
  auto pfix = network::toIPPrefix(prefix);
  update->attrs() = std::move(attrs);
  if (nexthop.isV4()) {
    // v4
    update->v4Announced()->push_back(pfix);
    update->v4Nexthop() = network::toBinaryAddress(nexthop);
  } else {
    // v6
    BgpNlri nlri;
    nlri.afi() = BgpUpdateAfi::AFI_IPv6;
    nlri.safi() = BgpUpdateSafi::SAFI_UNICAST;
    RiggedIPPrefix rigPfix;
    rigPfix.prefix() = std::move(pfix);
    nlri.prefixes()->push_back(std::move(rigPfix));
    nlri.nexthop() = network::toBinaryAddress(nexthop);
    update->mpAnnounced() = std::move(nlri);
  }
  return update;
}

std::unique_ptr<BgpUpdate2> constructWithdrawInBgpUpdate2Format(
    const folly::CIDRNetwork& prefix,
    bool toV2) {
  auto update = std::make_unique<BgpUpdate2>();
  auto pfix = network::toIPPrefix(prefix);
  if (prefix.first.isV4()) {
    if (toV2) {
      RiggedIPPrefix rigPfx;
      rigPfx.prefix() = pfix;
      update->v4Withdrawn2()->push_back(std::move(rigPfx));
    }
    update->v4Withdrawn()->push_back(std::move(pfix));
  } else {
    // multi protocol
    BgpNlri nlri;
    nlri.afi() = BgpUpdateAfi::AFI_IPv6;
    nlri.safi() = BgpUpdateSafi::SAFI_UNICAST;
    RiggedIPPrefix rigPfix;
    rigPfix.prefix() = std::move(pfix);
    nlri.prefixes()->push_back(std::move(rigPfix));
    update->mpWithdrawn() = std::move(nlri);
  }
  return update;
}

std::unique_ptr<BgpEndOfRib> constructEndOfRib(const BgpUpdateAfi& afi) {
  auto eor = std::make_unique<BgpEndOfRib>();
  eor->isMpEor() = (afi == BgpUpdateAfi::AFI_IPv4) ? false : true;
  eor->afi() = afi;
  eor->safi() = BgpUpdateSafi::SAFI_UNICAST;
  return eor;
}

} // namespace bgplib
} // namespace nettools
} // namespace facebook
