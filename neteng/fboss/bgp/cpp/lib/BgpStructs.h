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

#pragma once

#include <folly/IPAddress.h>
#include <folly/String.h>
#include <folly/io/IOBuf.h>

#include "neteng/fboss/bgp/cpp/lib/DeDuplicator.h"
#include "neteng/fboss/bgp/if/gen-cpp2/BgpStructs_types.h"

namespace facebook {
namespace nettools {
namespace bgplib {

namespace constants {
// BGP well-known port
constexpr uint16_t kBgpPort = 179;
} // namespace constants

/*
 * Length of BgpMessage header
 * 19 = Marker: 16 + Length: 2 + Type: 1
 */
const size_t kBgpMsgHeaderLen = 19;

// Maximum allowed length of BGP Message
const size_t kMaxBgpMsgLen = 4096;

// Maximum allowed number of messages before task yiedling
const size_t kMsgBatchSizeToYield{10};

/*
 * Minimum length of BGP UPDATE Message
 * Inlcudes 16B (marker), 2B (length), 1B (type),
 * 2B (withdrawn routes length), 2B (total path attribute length).
 */
const size_t kMinBgpUpdateMsgLen = 23;

// Minimum length of a NOTIFICATION body: 1B (error code), 1B (error subcode)
inline constexpr size_t kMinBgpNotificationBodyLen{2};

/*
 * Maximum length of withdrawn routes + path attrs + nlri info in BGP Update
 * message
 */
const size_t kMaxBgpUpdateVarLen = kMaxBgpMsgLen - kMinBgpUpdateMsgLen;

// 17 = v6 prefix len: 1 + prefix: 16
const size_t kMaxV6PrefixLen = 17;

// 5 = v4 prefix len: 1 + prefix 4
const size_t kMaxV4prefixLen = 5;

// 4 byte path id
const size_t kMaxPathIdLen = 4;

// maximum as segment size specified in RFC
const size_t kMaxAsPathSegmentSize = 255;

// 16 byte BGP marker with all bits set to 1
// clang-format off
 const std::array<uint8_t, 16> kBgpMarker{{
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
 }};
// clang-format on

const uint8_t kBgpVersion{4};

constexpr uint16_t kAsTrans{23456};
constexpr uint16_t k2BytesAsnLimit = (1 << 16) - 1; // 65535

// Graceful Restart capability Restart Flags and Restart Time, RFC 4724/8538
inline constexpr uint16_t kGrRestartStateBit{0x8000};
inline constexpr uint16_t kGrNotificationBit{0x4000};
inline constexpr uint16_t kGrRestartTimeMask{0x0fff};

/*
 * Used to limit the number of items that can be written to
 * an egress message queue.
 *
 * TODO: These numbers may be changed once we have more
 * data from scaled EBB setup.
 */
const size_t kMaxEgressQueueSize{10};
const size_t kEgressQueueHighWatermark{8};
const size_t kEgressQueueLowWatermark{2};

const size_t kMaxIngressQueueSize{10};

enum BgpMessageType {
  BGP_MSG_TYPE_OPEN = 1,
  BGP_MSG_TYPE_UPDATE = 2,
  BGP_MSG_TYPE_NOTIFICATION = 3,
  BGP_MSG_TYPE_KEEPALIVE = 4,
  BGP_MSG_TYPE_ROUTE_REFRESH = 5, // RFC 7313
};

const std::map<BgpMessageType, std::string> kBgpMessageType_VALUES_TO_NAMES = {
    {BGP_MSG_TYPE_OPEN, "OPEN"},
    {BGP_MSG_TYPE_UPDATE, "UPDATE"},
    {BGP_MSG_TYPE_NOTIFICATION, "NOTIFICATION"},
    {BGP_MSG_TYPE_KEEPALIVE, "KEEPALIVE"}};

enum BgpAttrFlags {
  BGP_ATTR_FLAG_OPTIONAL_TRANSITIVE = 0xc0,
  BGP_ATTR_FLAG_OPTIONAL = 0x80,
  BGP_ATTR_FLAG_TRANSITIVE = 0x40,
  BGP_ATTR_FLAG_PARTIAL = 0x20,
  BGP_ATTR_FLAG_EXTENDED = 0x10,
};

enum class BgpAttrCode : uint8_t {
  BGP_ATTR_ORIGIN = 1,
  BGP_ATTR_AS_PATH = 2,
  BGP_ATTR_NEXT_HOP = 3,
  BGP_ATTR_MED = 4,
  BGP_ATTR_LOCAL_PREF = 5,
  BGP_ATTR_ATOMIC_AGGREGATE = 6,
  BGP_ATTR_AGGREGATOR = 7,
  BGP_ATTR_COMMUNITIES = 8,
  BGP_ATTR_ORIGINATOR_ID = 9,
  BGP_ATTR_CLUSTER_LIST = 10,
  BGP_ATTR_MP_REACH_NLRI = 14,
  BGP_ATTR_MP_UNREACH_NLRI = 15,
  BGP_ATTR_EXTENDED_COMMUNITIES = 16,
  BGP_ATTR_LINK_STATE = 29,
  BGP_ATTR_LARGE_COMMUNITIES = 32,
  // DEPRECATED
  // BGP_ATTR_AGGREGATE_BITMAP = 36,
};

struct BgpMessageHeader {
  uint16_t length;
  uint8_t type;
};

enum class BgpPeerState {
  IDLE = 1,
  CONNECT = 2,
  ACTIVE = 3,
  OPENSENT = 4,
  OPENCONFIRM = 5,
  ESTABLISHED = 6,
};

const std::map<BgpPeerState, std::string> kBgpPeerState_VALUES_TO_NAMES = {
    {BgpPeerState::IDLE, "IDLE"},
    {BgpPeerState::CONNECT, "CONNECT"},
    {BgpPeerState::ACTIVE, "ACTIVE"},
    {BgpPeerState::OPENSENT, "OPENSENT"},
    {BgpPeerState::OPENCONFIRM, "OPENCONFIRM"},
    {BgpPeerState::ESTABLISHED, "ESTABLISHED"},
};

enum class BgpPeerEvent {
  UNSPECIFIED_EVENT = 0,
  MANUAL_STOP = 2,
  CONNECT_RETRY_T_EXPIRE = 9,
  HOLD_T_EXPIRE = 10,
  KEEP_ALIVE_T_EXPIRE = 11,
  BGP_OPEN = 19,
  BGP_HEADER_ERR = 21,
  BGP_OPEN_MSG_ERR = 22,
  NOTIF_MSG_VER_ERR = 24,
  NOTIF_MSG = 25,
  KEEP_ALIVE_MSG = 26,
  UPDATE_MSG = 27,
  UPDATE_MSG_ERR = 28,
  TRANSPORT_ERR = 51,

  // Non-RFC events
  FSM_ERR = 101,
};

const std::map<BgpPeerEvent, std::string> kBgpPeerEvent_VALUES_TO_NAMES = {
    {BgpPeerEvent::UNSPECIFIED_EVENT, "UNSPECIFIED_EVENT"},
    {BgpPeerEvent::MANUAL_STOP, "MANUAL_STOP"},
    {BgpPeerEvent::CONNECT_RETRY_T_EXPIRE, "CONNECT_RETRY_T_EXPIRE"},
    {BgpPeerEvent::HOLD_T_EXPIRE, "HOLD_T_EXPIRE"},
    {BgpPeerEvent::KEEP_ALIVE_T_EXPIRE, "KEEP_ALIVE_T_EXPIRE"},
    {BgpPeerEvent::BGP_OPEN, "BGP_OPEN"},
    {BgpPeerEvent::BGP_HEADER_ERR, "BGP_HEADER_ERR"},
    {BgpPeerEvent::BGP_OPEN_MSG_ERR, "BGP_OPEN_MSG_ERR"},
    {BgpPeerEvent::NOTIF_MSG_VER_ERR, "NOTIF_MSG_VER_ERR"},
    {BgpPeerEvent::NOTIF_MSG, "NOTIF_MSG"},
    {BgpPeerEvent::KEEP_ALIVE_MSG, "KEEP_ALIVE_MSG"},
    {BgpPeerEvent::UPDATE_MSG, "UPDATE_MSG"},
    {BgpPeerEvent::UPDATE_MSG_ERR, "UPDATE_MSG_ERR"},
    {BgpPeerEvent::TRANSPORT_ERR, "TRANSPORT_ERR"},
    {BgpPeerEvent::FSM_ERR, "FSM_ERR"},
};

const std::map<BgpAttrOrigin, std::string> kBgpAttrOrigin_VALUES_TO_NAMES = {
    {BgpAttrOrigin::BGP_ORIGIN_IGP, "IGP"},
    {BgpAttrOrigin::BGP_ORIGIN_EGP, "EGP"},
    {BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE, "INCOMPLETE"},
};

/*
 * Ideally we would have liked all thrift structures like BgpAttributes
 * named BgpAttributesT, so that C++ structs could be without any suffix.
 * Thrift structures are used in lot of places, so naming cpp structs with
 * C suffix
 */
struct BgpAttrAsPathSegmentC {
  /*
   * As per RFC 6996, for 2-byte ASNs, the private ASN range is 64512 to
   * 65534 inclusive.  We don't have to worry about 65535 because we will
   * never get that in our AS path (it is reserved for well-known
   * communities, see RFC 7300 for more info).  For two-byte ASNs, we treat
   * any number below 64512 as a public ASN.
   */
  static const uint32_t BGP_PRIVATE_AS_START = 64512;

  // Only one of them will have elements in one path segment
  std::set<uint32_t> asSet;
  std::vector<uint32_t> asSequence;
  std::vector<uint32_t> asConfedSequence;
  std::set<uint32_t> asConfedSet;

  inline bool operator==(const BgpAttrAsPathSegmentC& other) const {
    return (this->asSet == other.asSet) &&
        (this->asSequence == other.asSequence) &&
        (this->asConfedSet == other.asConfedSet) &&
        (this->asConfedSequence == other.asConfedSequence);
  }

  inline bool operator!=(const BgpAttrAsPathSegment& other) const {
    std::set<long> asSetConvert;
    std::vector<long> asSequenceConvert;
    std::vector<long> asConfedSequenceConvert;
    std::set<long> asConfedSetConvert;
    for (const auto k : this->asSet) {
      asSetConvert.insert(static_cast<long>(k));
    }
    asSequenceConvert.reserve(this->asSequence.size());
    for (const auto k : this->asSequence) {
      asSequenceConvert.push_back(static_cast<long>(k));
    }
    asConfedSequenceConvert.reserve(this->asConfedSequence.size());
    for (const auto k : this->asConfedSequence) {
      asConfedSequenceConvert.push_back(static_cast<long>(k));
    }
    for (const auto k : this->asConfedSet) {
      asConfedSetConvert.insert(static_cast<long>(k));
    }
    return (asSetConvert != *other.asSet()) ||
        (asSequenceConvert != *other.asSequence()) ||
        (asConfedSetConvert != *other.asConfedSet()) ||
        (asConfedSequenceConvert != *other.asConfedSequence());
  }

  static inline BgpAttrAsPathSegmentC fromAsSet(
      const std::set<uint32_t>& asSet) {
    BgpAttrAsPathSegmentC seg;
    seg.asSet = asSet;
    return seg;
  }

  static inline BgpAttrAsPathSegmentC fromAsSeq(
      const std::vector<uint32_t>& asSeq) {
    BgpAttrAsPathSegmentC seg;
    seg.asSequence = asSeq;
    return seg;
  }

  static inline BgpAttrAsPathSegmentC fromConfedSeq(
      const std::vector<uint32_t>& confedSeq) {
    BgpAttrAsPathSegmentC seg;
    seg.asConfedSequence = confedSeq;
    return seg;
  }

  static inline BgpAttrAsPathSegmentC fromConfedSet(
      const std::set<uint32_t>& confedSet) {
    BgpAttrAsPathSegmentC seg;
    seg.asConfedSet = confedSet;
    return seg;
  }

  // convert to string
  std::string str() const;
  bool hasAsn(const uint32_t asn) const;
  bool hasPublicAsn() const;
  bool isConfedSegment() const;
  std::size_t hash() const;

 private:
  template <typename Func>
  bool checkSegmentAnyAsn(Func func) const;
};

class BgpAttrAsPathC : public std::vector<BgpAttrAsPathSegmentC> {
 public:
  std::size_t hash() const;
};

using DeDuplicatedAsPath = DeDuplicatedAttribute<BgpAttrAsPathC>;

struct BgpAttrAggregatorC {
  uint32_t asn{0};
  folly::IPAddress ip;

  inline bool operator==(const BgpAttrAggregatorC& other) const {
    return (this->asn == other.asn) && (this->ip == other.ip);
  }

  std::size_t hash() const;
};

struct BgpAttrCommunityC {
  uint16_t asn{0};
  uint16_t value{0};

  constexpr inline BgpAttrCommunityC(
      uint16_t asn = 0,
      uint16_t value = 0) noexcept
      : asn(asn), value(value) {}

  inline bool operator==(const BgpAttrCommunityC& other) const {
    return (this->asn == other.asn) && (this->value == other.value);
  }

  inline bool operator!=(const BgpAttrCommunity& other) const {
    return (this->asn != *other.asn()) || (this->value != *other.value());
  }

  inline bool operator<(const BgpAttrCommunityC& other) const {
    if (asn != other.asn) {
      return asn < other.asn;
    } else {
      return value < other.value;
    }
  }

  inline std::string to_string() const {
    return fmt::format("{:d}:{:d}", asn, value);
  }

  /*
   * Create a BgpAttrCommunity from passed in commStr param.
   * Valid arguments are either a well-known community name or
   * a 32bit integer string or a ASN:NN formatted string where both
   * ASN and NN are 16bit integers
   */
  static std::optional<BgpAttrCommunityC> createBgpAttrCommunity(
      const std::string& comm);

  // Convert the community to BgpAttrCommunity (generated Thrift class).
  inline BgpAttrCommunity toThrift() const {
    BgpAttrCommunity comm;
    comm.asn() = asn;
    comm.value() = value;
    return comm;
  }

  std::size_t hash() const;
};

class BgpAttrCommunitiesC : public std::vector<BgpAttrCommunityC> {
 public:
  std::size_t hash() const;
};

using DeDuplicatedCommunities =
    DeDuplicatedAttribute<BgpAttrCommunitiesC, true>;

class BgpAttrClusterListC : public std::vector<uint32_t> {
 public:
  std::size_t hash() const;
};

using DeDuplicatedClusterList = DeDuplicatedAttribute<BgpAttrClusterListC>;

/**
 * Base class to encode an externded community (RFC:4360).
 */
struct BgpExtCommunityBaseTypeC {
  // Well-known type codes
  static const uint8_t kBgpExtCommASNonTransitiveType = 0x40;
  static const uint8_t kBgpExtCommASTransitiveType = 0x00;
  static const uint8_t kBgpExtCommASTransitiveType2 = 0x02;
  static const uint8_t kBgpExtCommIPv4TransitiveType = 0x01;
  static const uint8_t kBgpExtCommIPv4NonTransitiveType = 0x41;
  static const uint8_t kBgpExtCommOpaqueTransitiveType = 0x03;
  static const uint8_t kBgpExtCommOpaqueNonTransitiveType = 0x43;

  BgpExtCommunityBaseTypeC(uint32_t rawValHigh, uint32_t rawValLow)
      : rawValHigh(rawValHigh), rawValLow(rawValLow) {}

  inline virtual std::pair<uint32_t, uint32_t> getRawValueInWords()
      const final {
    return std::make_pair(rawValHigh, rawValLow);
  }

  virtual uint8_t getType() const {
    return rawValHigh >> 24;
  }

  // This is overwritten by extended-commuity types which has sub-types.
  virtual std::optional<uint8_t> getSubType() const {
    return std::nullopt;
  }

  virtual std::string str() const {
    return fmt::format("{}:{}", rawValHigh, rawValLow);
  }

  virtual ~BgpExtCommunityBaseTypeC() {}

  uint32_t rawValHigh{0};
  uint32_t rawValLow{0};
};

/**
 * Encoding of AS-specific extended type extended comunity attribute.
 * (Reference: RFC:4360)
 */
struct BgpExtCommunityAsSpecificExtTypeC : BgpExtCommunityBaseTypeC {
  BgpExtCommunityAsSpecificExtTypeC(uint32_t rawValHigh, uint32_t rawValLow);

  std::string str() const override {
    return fmt::format(
        "[AsSpecificExtType] {}:{}:{}:{}",
        getType(),
        *getSubType(),
        getAsn(),
        getValue());
  }

  std::optional<uint8_t> getSubType() const override {
    return rawValHigh >> 16;
  }

  uint16_t getAsn() const {
    return (rawValHigh & 0xFFFF);
  }

  uint32_t getValue() const {
    return rawValLow;
  }

  void setValue(uint32_t value) {
    rawValLow = value;
  }
};

// Link Bandwidth (UCMP) Community
struct BgpExtCommunityLinkBandWidthTypeC : BgpExtCommunityAsSpecificExtTypeC {
 public:
  BgpExtCommunityLinkBandWidthTypeC(uint32_t rawValHigh, uint32_t rawValLow);
  BgpExtCommunityLinkBandWidthTypeC(uint16_t asn, float val);

  static uint32_t rawValueHigh(uint16_t asn);

  std::string str() const override {
    return fmt::format(
        "{} LBW:{}", BgpExtCommunityAsSpecificExtTypeC::str(), getLBW());
  }

  float getLBW() const;

  void setLBW(float lbwInFloat);
};

/**
 * Encoding of IPv4-specific extended type extended comunity attribute.
 * (Reference: RFC:4360)
 */
struct BgpExtCommunityIPv4SpecificExtTypeC : BgpExtCommunityBaseTypeC {
 public:
  BgpExtCommunityIPv4SpecificExtTypeC(uint32_t rawValHigh, uint32_t rawValLow);

  std::string str() const override {
    return fmt::format(
        "[IPv4SpecificExtType] {}:{}:{}:{}",
        getType(),
        *getSubType(),
        getIPv4().str(),
        getValue());
  }

  std::optional<uint8_t> getSubType() const override {
    return rawValHigh >> 16;
  }

  folly::IPAddress getIPv4() const {
    return folly::IPAddress::fromLongHBO(
        ((rawValHigh & 0x0000FFFF) << 16) | ((rawValLow & 0xFFFF0000) >> 16));
  }

  uint16_t getValue() const {
    return (rawValLow & 0xFFFF);
  }
};

/**
 * Encoding of Opaque extended type extended comunity attribute.
 * (Reference: RFC:4360)
 */
struct BgpExtCommunityOpaqueExtTypeC : BgpExtCommunityBaseTypeC {
 public:
  BgpExtCommunityOpaqueExtTypeC(uint32_t rawValHigh, uint32_t rawValLow);

  std::string str() const override {
    return fmt::format(
        "[OpaqueExtType] {}:{}:{}",
        getType(),
        *getSubType(),
        folly::hexDump(getValue().data(), 6));
  }

  std::optional<uint8_t> getSubType() const override {
    return rawValHigh >> 16;
  }

  std::array<uint8_t, 6> getValue() const;
};

/**
 * Encoding of regular type of extended community is:
 *  <Type: 1-octets> <Value: 7 Octects>
 */
struct BgpAttrExtCommunityRegularTypeC : BgpExtCommunityBaseTypeC {
 public:
  using BgpExtCommunityBaseTypeC::BgpExtCommunityBaseTypeC;

  std::string str() const override {
    return fmt::format(
        "[RegularType] {}:{}", getType(), folly::hexDump(getValue().data(), 7));
  }

  std::array<uint8_t, 7> getValue() const;
};

/**
 * Captures a single extended community attribute.
 */
struct BgpAttrExtCommunityC {
  /** RFC defined flags for extended community
   * https://datatracker.ietf.org/doc/html/rfc4360
   * The high-order octet of the Type Field is as shown below:
   *    0 1 2 3 4 5 6 7
   *   +-+-+-+-+-+-+-+-+
   *   |I|T|           |
   *   +-+-+-+-+-+-+-+-+
   *   I - IANA authority bit
   *     Value 0: IANA-assignable type using the "First Come First
   *     Serve" policy
   *     Value 1: Part of this Type Field space is for IANA
   *     assignable types using either the Standard Action or the
   *     Early IANA Allocation policy.  The rest of this Type
   *     Field space is for Experimental use.
   *   T - Transitive bit
   *     Value 0: The community is transitive across ASes
   *     Value 1: The community is non-transitive across ASes
   *   HighType: 0x00 -> transitive; 0x40 -> non-transitive
   */
  enum class BGP_EXT_COMMUNITY_FLAGS {
    IANA_AUTHORITY = 0x1 << 7,
    TRANSITIVE = 0x1 << 6,
  };

  // Well-known community subtypes.
  enum class BGP_EXT_COMMUNITY_SUBTYPES {
    ROUTE_TARGET_COMMUNITY_SUBTYPE = 0x2,
    ROUTE_ORIGIN_COMMUNITY_SUBTYPE = 0x3,
    LINK_BW_COMMUNITY_SUBTYPE = 0x4,
  };

  /*
   * Ctor:: construct the right type of extended-community from the
   * raw values passed in.
   */
  explicit BgpAttrExtCommunityC(uint32_t rawValHigh, uint32_t rawValLow);
  explicit BgpAttrExtCommunityC(
      const BgpExtCommunityLinkBandWidthTypeC& lbwComm)
      : BgpAttrExtCommunityC(lbwComm.rawValHigh, lbwComm.rawValLow) {}

  // A transitive extended community might be passed on to the peers.
  bool isTransitive() const;

  bool isRouteTarget() const;

  bool isRouteOrigin() const;

  bool isNonTransitiveLinkBandwidthCommunity() const;

  bool isLinkBandwidthCommunity() const;

  std::string str() const;

  std::pair<uint32_t, uint32_t> getRawValueInWords() const;

  bool operator==(const BgpAttrExtCommunityC& other) const;

  inline bool operator<(const BgpAttrExtCommunityC& other) const {
    return getRawValueInWords() < other.getRawValueInWords();
  }

  std::size_t hash() const;

  std::shared_ptr<BgpExtCommunityBaseTypeC> attr;
};

class BgpAttrExtCommunitiesC : public std::vector<BgpAttrExtCommunityC> {
 public:
  std::size_t hash() const;
};

using DeDuplicatedExtCommunities =
    DeDuplicatedAttribute<BgpAttrExtCommunitiesC, true>;

struct BgpAttributesC {
  // All fields are populated in host byte order

  // ORIGIN
  facebook::nettools::bgplib::BgpAttrOrigin origin{
      nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE};
  // AS_PATH
  DeDuplicatedAsPath asPath;
  // MULTI_EXIT_DESCRIMINATOR
  uint32_t med{0};
  // Is the MED Attribute set flag
  bool isMedSet{false};
  // LOCAL_PREF
  std::optional<uint32_t> localPref;
  // ATOMIC_AGGREGATE
  bool atomicAggregate{false};
  // AGGREGATOR
  BgpAttrAggregatorC aggregator;
  // COMMUNITIES
  DeDuplicatedCommunities communities;
  // ORIGINATOR_ID, converted to host byte order like other fields
  uint32_t originatorId{0};
  // CLUSTER_LIST, converted to host byte order like other fields
  DeDuplicatedClusterList clusterList;
  // EXTENDED_COMMUNITIES
  DeDuplicatedExtCommunities extCommunities;
  // LOCAL_WEIGHT
  uint16_t weight{0};

  inline bool operator==(const BgpAttributesC& other) const {
    /*
     * Ordered to compare easy elements and bailout quick
     * Is there any easier way to compare? Should we store/compare hash.
     */
    return (this->origin == other.origin) && (this->med == other.med) &&
        (this->isMedSet == other.isMedSet) &&
        (this->localPref == other.localPref) &&
        (this->atomicAggregate == other.atomicAggregate) &&
        (this->originatorId == other.originatorId) &&
        (this->asPath == other.asPath) &&
        (this->aggregator == other.aggregator) &&
        (this->communities == other.communities) &&
        (this->clusterList == other.clusterList) &&
        (this->extCommunities == other.extCommunities) &&
        (this->weight == other.weight);
  }

  inline bool operator!=(const BgpAttributesC& other) const {
    return !(*this == other);
  }

  // convert to string
  std::string str() const;

  std::size_t hash() const;
};

using DeDuplicatedBgpAttributesC = DeDuplicatedAttribute<BgpAttributesC>;

struct BgpPathC {
  // This attribute will be deduplicated
  DeDuplicatedBgpAttributesC attrs;

  // NEXT_HOP
  folly::IPAddress nexthop;

  std::optional<std::unordered_map<std::string, int64_t>> topologyInfo;

  inline bool operator==(const BgpPathC& other) const {
    /*
     * Ordered to compare easy elements and bailout quick
     * Is there any easier way to compare? Should we store/compare hash.
     */
    return (this->attrs == other.attrs) && (this->nexthop == other.nexthop) &&
        (this->topologyInfo == other.topologyInfo);
  }

  // convert to string
  std::string str() const;

  // convert topology info map to string
  static std::string topoInfoToStr(
      const std::unordered_map<std::string, int64_t>& topoInfo);
};

struct BgpPeerId {
  folly::IPAddress peerAddr;
  uint32_t remoteBgpId{};
  std::string peerDescription;

  BgpPeerId() = default;

  BgpPeerId(const BgpPeerId& other)
      : peerAddr(other.peerAddr),
        remoteBgpId(other.remoteBgpId),
        peerDescription(other.peerDescription) {}

  BgpPeerId(
      const folly::IPAddress& peerAddr,
      const uint32_t remoteBgpId,
      const std::string& peerDescription = "")
      : peerAddr(peerAddr),
        remoteBgpId(remoteBgpId),
        peerDescription(peerDescription) {}

  std::string str() const {
    return fmt::format(
        "peerAddr {}, remoteBgpId {}",
        peerAddr.str(),
        folly::IPAddress::fromLongHBO(remoteBgpId).str());
  }

  std::string toOdsKey() const {
    return fmt::format(
        "{}_{}",
        peerAddr.str(),
        folly::IPAddress::fromLongHBO(remoteBgpId).str());
  }

  bool operator==(const BgpPeerId& rhs) const {
    return peerAddr == rhs.peerAddr && remoteBgpId == rhs.remoteBgpId;
  }

  BgpPeerId& operator=(BgpPeerId other) {
    // check for self-assignment
    if (&other == this) {
      return *this;
    }
    peerAddr = other.peerAddr;
    remoteBgpId = other.remoteBgpId;
    peerDescription = other.peerDescription;
    return *this;
  }
};

/**
 * UpdateDescriptor: Zero-copy BGP UPDATE distribution within update groups
 *
 * When update groups are enabled with enableSerializeGroupPdu, the group
 * serializes BGP UPDATE once and distributes this descriptor to all peers in
 * the group. Each peer's I/O thread clones the IOBuf (cheap, shares underlying
 * memory) and mutates nexthop fields across multiple messages.
 *
 * A single BgpUpdate2 can create multiple BGP UPDATE messages (fragmented due
 * to size limits). Some messages may have no nexthop (withdraws only), some
 * may have v4 nexthop, others may have v6 nexthop. This descriptor tracks all
 * nexthop locations across all messages.
 */
struct UpdateDescriptor {
  /** Shared serialized BGP UPDATE PDU (zero-copy across peers) */
  std::shared_ptr<const folly::IOBuf> serializedGroupPDU;

  /**
   * IPv4 nexthop value to write (4 bytes).
   * Set to AF_UNSPEC if no v4 nexthop in this update.
   */
  folly::IPAddress v4Nexthop;

  /**
   * IPv6 nexthop value to write (16 bytes).
   * Set to AF_UNSPEC if no v6 nexthop in this update.
   */
  folly::IPAddress v6Nexthop;

  /**
   * Vector of nexthop locations across all messages.
   * Each tuple contains:
   *   - bufferIndex: Index of buffer in IOBuf chain (0=first, 1=second, etc.)
   *   - offset: Byte offset of nexthop within that buffer
   *   - isV4: true = use v4Nexthop, false = use v6Nexthop
   */
  std::vector<std::tuple<size_t, size_t, bool>> nexthopOffsets;
};

} // namespace bgplib
} // namespace nettools
} // namespace facebook

namespace std {
template <>
struct hash<facebook::nettools::bgplib::BgpPeerId> {
  std::size_t operator()(
      const facebook::nettools::bgplib::BgpPeerId& peerId) const {
    return std::hash<std::string>{}(
        fmt::format("{}_{}", peerId.peerAddr.str(), peerId.remoteBgpId));
  }
};

template <>
struct hash<facebook::nettools::bgplib::BgpAttrCommunityC> {
  std::size_t operator()(
      const facebook::nettools::bgplib::BgpAttrCommunityC& bgpComm) const {
    return std::hash<std::string>{}(bgpComm.to_string());
  }
};

} // namespace std
