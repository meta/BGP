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

#include <folly/IPAddress.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include "common/network/AddressUtil.h"
#include "neteng/fboss/bgp/cpp/lib/BgpException.h"
#include "neteng/fboss/bgp/cpp/lib/detail/BgpMessageParserUtils.h"

#include <sstream>

// Helpers for BgpMessageParser
namespace facebook {
namespace nettools {
namespace bgplib {
namespace detail {

using namespace facebook::nettools::bgplib;

using folly::IOBuf;
using folly::io::Cursor;

/*
 * compatibility field in NLRI for withdrawal - rfc3107
 */
const int32_t kWithdrawCompatibility = 0x800000;
const int32_t kWithdrawCompatibility2 = 0x000000;

/*
 * The map betweent the attribute type and the flag
 */
const std::map<BgpAttrCode, uint8_t> kBgpAttr_Codes_TO_FLAGS = {
    {BgpAttrCode::BGP_ATTR_ORIGIN, BGP_ATTR_FLAG_TRANSITIVE},
    {BgpAttrCode::BGP_ATTR_AS_PATH, BGP_ATTR_FLAG_TRANSITIVE},
    {BgpAttrCode::BGP_ATTR_NEXT_HOP, BGP_ATTR_FLAG_TRANSITIVE},
    {BgpAttrCode::BGP_ATTR_MED, BGP_ATTR_FLAG_OPTIONAL},
    {BgpAttrCode::BGP_ATTR_LOCAL_PREF, BGP_ATTR_FLAG_TRANSITIVE},
    {BgpAttrCode::BGP_ATTR_ATOMIC_AGGREGATE, BGP_ATTR_FLAG_TRANSITIVE},
    {BgpAttrCode::BGP_ATTR_AGGREGATOR, BGP_ATTR_FLAG_OPTIONAL_TRANSITIVE},
    {BgpAttrCode::BGP_ATTR_COMMUNITIES, BGP_ATTR_FLAG_OPTIONAL_TRANSITIVE},
    {BgpAttrCode::BGP_ATTR_ORIGINATOR_ID, BGP_ATTR_FLAG_OPTIONAL},
    {BgpAttrCode::BGP_ATTR_CLUSTER_LIST, BGP_ATTR_FLAG_OPTIONAL},
    {BgpAttrCode::BGP_ATTR_MP_REACH_NLRI, BGP_ATTR_FLAG_OPTIONAL},
    {BgpAttrCode::BGP_ATTR_MP_UNREACH_NLRI, BGP_ATTR_FLAG_OPTIONAL},
    {BgpAttrCode::BGP_ATTR_EXTENDED_COMMUNITIES,
     BGP_ATTR_FLAG_OPTIONAL_TRANSITIVE},
    {BgpAttrCode::BGP_ATTR_LINK_STATE, BGP_ATTR_FLAG_OPTIONAL},
    {BgpAttrCode::BGP_ATTR_LARGE_COMMUNITIES,
     BGP_ATTR_FLAG_OPTIONAL_TRANSITIVE},
};

folly::CIDRNetwork
parseNlriPrefix(Cursor cursor, uint8_t prefixLen, BgpUpdateAfi afi) {
  // Length of prefix in bytes (bytes to read from buf)
  uint8_t bytes = ((prefixLen + 7) / 8);

  // init network with undefined address first
  folly::CIDRNetwork network(folly::IPAddress(), prefixLen);

  // Read prefix from buf into CIDR network
  if (afi == BgpUpdateAfi::AFI_IPv4) {
    // Number of bytes to read must be less than 4
    if (bytes > 4) {
      throw BgpUpdateMsgException(
          BgpNotifUpdateMsgErrSubCode::BN_UM_INVALID_NETWORK_FIELD,
          std::string(),
          fmt::format(
              "Unexpected length of v4-prefix. "
              "expected: <= 4, received: {}",
              (uint32_t)bytes));
    }
    // needed for zero-padding
    std::array<uint8_t, 4> data = {{0}};
    cursor.pull(data.data(), bytes);
    auto addr = folly::IPAddress::fromBinary(folly::ByteRange(data.data(), 4));
    network.first = addr.mask(prefixLen);
    DCHECK(network.first.isV4());
  } else if (afi == BgpUpdateAfi::AFI_IPv6) {
    // Number of bytes to read must be less than 16
    if (bytes > 16) {
      throw BgpUpdateMsgException(
          BgpNotifUpdateMsgErrSubCode::BN_UM_INVALID_NETWORK_FIELD,
          std::string(),
          fmt::format(
              "Unexpected length of v6-prefix. "
              "expected: <= 16, received: {}",
              (uint32_t)bytes));
    }
    // needed for zero-padding
    std::array<uint8_t, 16> data = {{0}};
    cursor.pull(data.data(), bytes);
    auto addr = folly::IPAddress::fromBinary(folly::ByteRange(data.data(), 16));
    network.first = addr.mask(prefixLen);
    DCHECK(network.first.isV6());
  } else {
    // TODO: we need to handle errors more gracefully :)
    DCHECK(false) << "Unknown afi type: {}" << (uint32_t)afi;
  }

  return network;
}

// NLRI has implied length. Cursor length must be bounded correctly.
std::vector<BgpPrefix> parseNlri(
    Cursor cursor,
    BgpUpdateAfi afi,
    BgpUpdateSafi safi /*safi*/,
    bool /*multiProtocol*/,
    const BgpCapabilities& capabilities) {
  // to return
  std::vector<BgpPrefix> bgpPrefixes;
  bool parsePathId = false;
  for (const auto& cap : *capabilities.addPathCapabilities()) {
    if (*cap.afi() == afi && *cap.safi() == safi) {
      parsePathId = *cap.sor() == BgpAddPathSendRec::RECEIVE ||
          *cap.sor() == BgpAddPathSendRec::BOTH;
    }
  }

  while (cursor.length()) {
    std::optional<int32_t> pathId = std::nullopt;
    if (parsePathId) {
      if (cursor.length() < 4) {
        throw BgpUpdateMsgException(
            BgpNotifUpdateMsgErrSubCode::BN_UM_INVALID_NETWORK_FIELD,
            std::string(),
            fmt::format(
                "Withdrawn NLRI: Attempting to 4-bytes of pathId,"
                " but only {} bytes remaining.",
                cursor.length()));
      }
      pathId = cursor.readBE<uint32_t>();
    }

    // Implies we read a pathId
    if (!cursor.length()) {
      throw BgpUpdateMsgException(
          BgpNotifUpdateMsgErrSubCode::BN_UM_INVALID_NETWORK_FIELD,
          std::string(),
          "Withdrawn NLRI: No bytes left, before reading length");
    }
    folly::CIDRNetwork prefix;
    auto prefixLen = cursor.read<uint8_t>();
    uint8_t bytes = (prefixLen + 7) / 8;

    if (bytes > cursor.length()) {
      throw BgpUpdateMsgException(
          BgpNotifUpdateMsgErrSubCode::BN_UM_INVALID_NETWORK_FIELD,
          std::string(),
          fmt::format(
              "Withdrawn NLRI: {} bytes prefixlen, but only {} bytes left.",
              bytes,
              cursor.length()));
    }

    auto prefixBuf = IOBuf::wrapBuffer(cursor.data(), bytes);
    Cursor prefixCursor(prefixBuf.get());

    cursor.skip(bytes);
    prefix = parseNlriPrefix(prefixCursor, prefixLen, afi);

    bgpPrefixes.emplace_back(prefix, std::vector<int32_t>{}, pathId);

  } // while

  return bgpPrefixes;
}

std::vector<BgpPrefix> parseMpNlri(
    Cursor cursor,
    BgpUpdateAfi afi,
    BgpUpdateSafi safi,
    const BgpCapabilities& capa,
    std::string attrBeingParsed,
    BgpAttrCode attrType) {
  bool parsePathId = false;
  for (const auto& cap : *capa.addPathCapabilities()) {
    if (*cap.afi() == afi && *cap.safi() == safi) {
      parsePathId = *cap.sor() == BgpAddPathSendRec::RECEIVE ||
          *cap.sor() == BgpAddPathSendRec::BOTH;
    }
  }

  if (safi == BgpUpdateSafi::SAFI_UNICAST) {
    if ((afi == BgpUpdateAfi::AFI_IPv6 && *capa.mpExtV6Unicast()) ||
        (afi == BgpUpdateAfi::AFI_IPv4 && *capa.mpExtV4Unicast())) {
      // copy and pass the cursor down
      return parseNlri(
          cursor,
          afi,
          static_cast<BgpUpdateSafi>(safi),
          true /* multi-proto */,
          capa);
    }

    // Skip unsupported AFI
    XLOGF(
        DBG2,
        "Skipping unsupported AFI {} for SAFI_UNICAST",
        static_cast<int>(afi));

    return std::vector<BgpPrefix>{};
  } // SAFI_UNICAST

  if (safi == BgpUpdateSafi::SAFI_LABELED_UNICAST) {
    if ((afi != BgpUpdateAfi::AFI_IPv6 || !(*capa.mpExtV6LU())) &&
        (afi != BgpUpdateAfi::AFI_IPv4 || !(*capa.mpExtV4LU()))) {
      // Skip unsupported capability
      XLOG(DBG2, "Skipping labeled unicast update as it's not enabled in caps");
      return std::vector<BgpPrefix>{};
    }

    // the value to return
    std::vector<BgpPrefix> bgpPrefixes;

    /*
     * NLRI with label stack (rfc3107)
     * Parse tuples of <length, label_stack, prefix>
     */
    while (cursor.length()) {
      folly::CIDRNetwork prefix;
      std::vector<int32_t> labels;

      // Parse length
      auto len = cursor.read<uint8_t>();

      // Parse label stack (rfc3107 and rfc3032)
      while (len > 24) {
        int32_t label = (cursor.read<uint8_t>() << 16) +
            (cursor.read<uint8_t>() << 8) + (cursor.read<uint8_t>());

        len -= 24; // Subtract 3 bytes (24 bits) from len

        // rfc8277 explicit withdraw
        if (attrType == BgpAttrCode::BGP_ATTR_MP_UNREACH_NLRI &&
            (label == kWithdrawCompatibility ||
             label == kWithdrawCompatibility2)) {
          break;
        }

        labels.push_back(label >> 4);
        if (label & 1) { // Bottom of Stack
          break;
        }
      }

      // Parse IP prefix
      std::optional<int32_t> pathId = std::nullopt;
      if (parsePathId) {
        pathId = cursor.readBE<uint32_t>();
      }

      auto bytes = (len + 7) / 8;
      if (bytes > cursor.length()) {
        throw BgpUpdateMsgException(
            BgpNotifUpdateMsgErrSubCode::BN_UM_UNSPECIFIC,
            attrBeingParsed,
            fmt::format(
                "bgp prefix length exceeds cursor length"
                "cursor length : {}, prefix length: {}",
                cursor.length(),
                bytes));
      }
      auto prefixBuf = IOBuf::wrapBuffer(cursor.data(), bytes);
      Cursor prefixCursor(prefixBuf.get());
      cursor.skip(bytes);

      prefix = parseNlriPrefix(prefixCursor, len, afi);
      bgpPrefixes.emplace_back(prefix, std::move(labels), pathId);
    }

    DCHECK_EQ(0, cursor.length());

    return bgpPrefixes;

  } // SAFI_LABELED_UNICAST

  // Unsupported SAFI
  throw BgpUpdateMsgException(
      BgpNotifUpdateMsgErrSubCode::BN_UM_OPTIONAL_ATTR_ERROR,
      attrBeingParsed,
      fmt::format("Unsupported safi: {}", static_cast<int>(safi)));
}

std::vector<BgpAttrAsPathSegment> parseAsPathAttr(
    Cursor cursor,
    const BgpCapabilities& capa,
    std::string attrBeingParsed) {
  std::vector<BgpAttrAsPathSegment> asPath;

  while (cursor.length()) {
    uint8_t segmentType{0};
    uint8_t asCnt{0};
    uint32_t asn{0};

    BgpAttrAsPathSegment seg;

    if (cursor.length() < 2) {
      throw BgpUpdateMsgException(
          BgpNotifUpdateMsgErrSubCode::BN_UM_ATTR_LEN_ERR,
          attrBeingParsed,
          "Invalid path attribute length");
    }

    segmentType = cursor.read<uint8_t>();
    asCnt = cursor.read<uint8_t>();

    auto expectedLength = *capa.as4byte() ? 4 * asCnt : 2 * asCnt;
    if (cursor.length() < expectedLength) {
      XLOGF(
          ERR,
          "AS_Path parsing error! cursor.length() {}< expectedLength {}",
          cursor.length(),
          expectedLength);

      throw BgpUpdateMsgException(
          BgpNotifUpdateMsgErrSubCode::BN_UM_ATTR_LEN_ERR,
          attrBeingParsed,
          fmt::format(
              "The length of attributes exceeds "
              "the remaining message length. "
              "Remaining message length: {}, "
              "expected AS PATH attributes length: {}, "
              "as4cap: {}",
              cursor.length(),
              (uint32_t)expectedLength,
              *capa.as4byte()));
    }

    while (asCnt--) {
      if (*capa.as4byte()) {
        asn = cursor.readBE<uint32_t>();
      } else {
        asn = cursor.readBE<uint16_t>();
      }

      switch (segmentType) {
        // AS_ET
        case 1:
          seg.asSet()->insert(asn);
          break;
        // AS_SEQUENCE
        case 2:
          seg.asSequence()->push_back(asn);
          break;
        // AS_CONFED_SEQ
        case 3:
          seg.asConfedSequence()->push_back(asn);
          break;
        // AS_CONFED_SET
        case 4:
          seg.asConfedSet()->insert(asn);
          break;
        // BOLLOCKS!
        default:
          throw BgpUpdateMsgException(
              BgpNotifUpdateMsgErrSubCode::BN_UM_MALFORMED_AS_PATH,
              std::string(),
              fmt::format(
                  "Unknown as segment type: {}", (uint32_t)segmentType));
      }
    } // while asCnt
    asPath.push_back(seg);
  }
  return asPath;
}

/*
 * UpdateMsgParsingState methods
 */

void UpdateMsgParsingState::parseV4Withdrawn(
    Cursor& updateMsgCursor,
    const BgpCapabilities& capabilities) {
  // buffer/cursor to hold withdrawn routes
  IOBuf withdrawnBuf(IOBuf::CREATE, 0);
  Cursor withdrawnCursor(&withdrawnBuf);

  // Get withdrawn routes
  if (sizeof(v4WithdrawnLen) > updateMsgCursor.length()) {
    throw BgpUpdateMsgException(
        BgpNotifUpdateMsgErrSubCode::BN_UM_MALFORMED_ATTRIBUTE_LIST,
        std::string(),
        fmt::format(
            "Failed to read the byte size of withdraw update "
            "Remaining message length {}, "
            "Expected field size {}",
            updateMsgCursor.length(),
            sizeof(v4WithdrawnLen)));
  }
  v4WithdrawnLen = updateMsgCursor.readBE<uint16_t>();
  if (!v4WithdrawnLen) {
    return;
  }
  if (v4WithdrawnLen > updateMsgCursor.length()) {
    throw BgpUpdateMsgException(
        BgpNotifUpdateMsgErrSubCode::BN_UM_MALFORMED_ATTRIBUTE_LIST,
        std::string(),
        fmt::format(
            "The length of withdrawn routes exceeds "
            "the remaining message length. "
            "Remaining message length {}, "
            "withdrawn length: {} ",
            updateMsgCursor.length(),
            v4WithdrawnLen));
  }
  withdrawnBuf =
      IOBuf::wrapBufferAsValue(updateMsgCursor.data(), v4WithdrawnLen);
  withdrawnCursor.reset(&withdrawnBuf);
  updateMsgCursor.skip(v4WithdrawnLen);

  // Read (v4) withdrawn routes (first time, fill the vector directly)
  try {
    v4Withdrawn = parseNlri(
        withdrawnCursor,
        BgpUpdateAfi::AFI_IPv4,
        BgpUpdateSafi::SAFI_UNICAST,
        false,
        capabilities);
  } catch (std::out_of_range const&) {
    throw BgpUpdateMsgException(
        BgpNotifUpdateMsgErrSubCode::BN_UM_INVALID_NETWORK_FIELD,
        std::string(),
        fmt::format(
            "Withdrawn NLRI: Unhandled parsing error, "
            "withdrawn length: {} ",
            withdrawnCursor.length()));
  }
}

void UpdateMsgParsingState::checkPathAttributeFlag(
    uint8_t attrFlags,
    BgpAttrCode attrType,
    std::string attrBeingParsed) {
  const auto& kv = kBgpAttr_Codes_TO_FLAGS.find(attrType);
  /*
   * Ignore the fourth high-order bit and the 3-rd high-order bit.
   * An attribute can have either extended length or not.
   * If an attribute can be extended, then the fourth high-order bit
   * can be either 0 or 1. Otherwise, it can only be 0.
   * For the 3-rd high-order bit, optional transitive attributes can set it
   * to be 0 or 1. For other attributes, it can only be 0.
   * Furthermore, the lower-order four bits of the Attribute Flags octet are
   * unused.  They MUST be zero when sent and MUST be ignored when received.
   */
  attrFlags = attrFlags & 0xc0;
  if (kv != kBgpAttr_Codes_TO_FLAGS.cend() && kv->second != attrFlags) {
    // Attribute flag is wrong
    throw BgpUpdateMsgException(
        BgpNotifUpdateMsgErrSubCode::BN_UM_ATTR_FLAGS_ERR,
        attrBeingParsed,
        fmt::format(
            "Wrong attribute flags for attribute"
            " 0x{0:02x} type, expected: 0x{1:02x}, "
            "received: 0x{2:02x}",
            static_cast<int>(attrType),
            kv->second,
            attrFlags));
  }
}

void UpdateMsgParsingState::parsePathAttributes(
    Cursor& updateMsgCursor,
    const BgpCapabilities& capa) {
  // buffer/cursor to hold path attributes
  IOBuf paBuf(IOBuf::CREATE, 0);
  Cursor paCursor(&paBuf);

  // Initialize MED to not set (for the case where MED is not found)
  attrs.isMedSet() = false;

  // Read path attrs length
  if (sizeof(paLen) > updateMsgCursor.length()) {
    throw BgpUpdateMsgException(
        BgpNotifUpdateMsgErrSubCode::BN_UM_MALFORMED_ATTRIBUTE_LIST,
        std::string(),
        fmt::format(
            "Failed to read the byte size of path attrs "
            "Remaining message length {}, "
            "Expected field size {}",
            updateMsgCursor.length(),
            sizeof(paLen)));
  }

  paLen = updateMsgCursor.readBE<uint16_t>();

  if (!paLen) {
    return;
  }
  if (paLen > updateMsgCursor.length()) {
    throw BgpUpdateMsgException(
        BgpNotifUpdateMsgErrSubCode::BN_UM_MALFORMED_ATTRIBUTE_LIST,
        std::string(),
        fmt::format(
            "The length of attributes exceeds "
            "the remaining message length. "
            "Remaining message length: {}, "
            "path attributes length: {}",
            updateMsgCursor.length(),
            paLen));
  }

  paBuf = IOBuf::wrapBufferAsValue(updateMsgCursor.data(), paLen);
  paCursor.reset(&paBuf);
  updateMsgCursor.skip(paLen);

  XLOGF(
      DBG4,
      "parsePathAttributes: dumping full attributes buffer (len {}):\n{}",
      paCursor.length(),
      folly::hexDump(paCursor.data(), paCursor.length()));

  bool mpReachNlriFound{false};
  bool mpUnreachNlriFound{false};

  while (paCursor.length()) {
    BgpAttrCode attrType;
    uint8_t attrFlags{0};
    uint16_t attrBodyLen{0};

    const uint8_t* rawAttrStart{nullptr};
    int16_t rawAttrLen{0};
    int16_t lengthSize{0};

    // Read attributes flags, type, length and then increment pos
    try {
      attrFlags = paCursor.read<uint8_t>();
      rawAttrStart = paCursor.peek().data();
      auto printable = paCursor.read<uint8_t>();
      attrType = static_cast<BgpAttrCode>(printable);

      if (attrFlags & BGP_ATTR_FLAG_EXTENDED) {
        attrBodyLen = paCursor.readBE<uint16_t>();
        lengthSize = sizeof(uint16_t);
      } else {
        attrBodyLen = paCursor.read<uint8_t>();
        lengthSize = sizeof(uint8_t);
      }
      rawAttrLen = attrBodyLen + lengthSize + sizeof(uint8_t);

      // handle situation where we have an invalid attribute length (very large)
      rawAttrLen = attrBodyLen < paCursor.length()
          ? rawAttrLen
          /*
           * rawAttrStart will be pointing to attrType
           * +1 attrType, is added later
           * + lengthSize, add back what we read
           */
          : (paCursor.length() + lengthSize + 1);

      /*
       * need to turn attrFlags to hex string in case we need to send a
       * notification
       */
      std::stringstream attrFlagsInHex;
      attrFlagsInHex << std::hex << attrFlags;
      // This field contains attr flag, type code,length and value
      auto attrBeingParsed = attrFlagsInHex.str() +
          std::string(reinterpret_cast<const char*>(rawAttrStart), rawAttrLen);
      checkPathAttributeFlag(attrFlags, attrType, attrBeingParsed);

      if (attrBodyLen > paCursor.length()) {
        throw BgpUpdateMsgException(
            BgpNotifUpdateMsgErrSubCode::BN_UM_MALFORMED_ATTRIBUTE_LIST,
            std::string(),
            fmt::format(
                "The length of the attribute exceeds "
                "the remaining message length. "
                "Remaining message length: {}, "
                "attribute {} has length: {}",
                paCursor.length(),
                uint32_t(attrType),
                attrBodyLen));
      }

      // Some sanity checks according to BGP-4
      if ((attrFlags & BGP_ATTR_FLAG_OPTIONAL) == 0 && // well known attr
          (attrFlags & BGP_ATTR_FLAG_TRANSITIVE) == 0) { // non-transitive
        throw BgpUpdateMsgException(
            BgpNotifUpdateMsgErrSubCode::BN_UM_ATTR_FLAGS_ERR,
            attrBeingParsed,
            "For well-known attributes, the Transitive bit MUST be set to "
            "1.");
      }
      if ((attrFlags & BGP_ATTR_FLAG_OPTIONAL) == 0 || // well known attr
          (attrFlags & BGP_ATTR_FLAG_TRANSITIVE) == 0) { // non-transitive
        if (attrFlags & BGP_ATTR_FLAG_PARTIAL) { // partial
          throw BgpUpdateMsgException(
              BgpNotifUpdateMsgErrSubCode::BN_UM_ATTR_FLAGS_ERR,
              attrBeingParsed,
              "For well-known attributes and for optional non-transitive "
              "attributes, the partial bit MUST be set to 0.");
        }
      }
      // the cursor for the attribute body
      auto attrBodyBuf = IOBuf::wrapBuffer(paCursor.data(), attrBodyLen);
      Cursor attrBodyCursor(attrBodyBuf.get());
      paCursor.skip(attrBodyLen);
      XLOGF(
          DBG5,
          "attr cursor dump (flags {}, type {}, attr len {}, cursor len {}):\n{}",
          static_cast<int>(attrFlags),
          static_cast<int>(attrType),
          attrBodyLen,
          attrBodyCursor.length(),
          folly::hexDump(attrBodyCursor.data(), attrBodyCursor.length()));
      switch (attrType) {
        case BgpAttrCode::BGP_ATTR_ORIGIN: {
          uint8_t originCode = attrBodyCursor.read<uint8_t>();
          if (originCode > 2) {
            throw BgpUpdateMsgException(
                BgpNotifUpdateMsgErrSubCode::BN_UM_INVALID_ORIGIN_ATTR,
                attrBeingParsed,
                fmt::format("Unknown origin code: {}", (uint32_t)originCode));
          }
          attrs.origin() = static_cast<BgpAttrOrigin>(originCode);
          hasOriginAttr = true;
        } // ORIGIN
        break;
        case BgpAttrCode::BGP_ATTR_AS_PATH: {
          *attrs.asPath() =
              parseAsPathAttr(attrBodyCursor, capa, attrBeingParsed);
          hasAsPathAttr = true;
        } // AS_PATH
        break;
        case BgpAttrCode::BGP_ATTR_NEXT_HOP: {
          if (attrBodyCursor.length() != 4) {
            throw BgpUpdateMsgException(
                BgpNotifUpdateMsgErrSubCode::BN_UM_INVALID_NEXT_HOP_ATTR,
                attrBeingParsed,
                fmt::format(
                    "Unexpected length of v4-nexthop. "
                    "expected: 4, received: {}",
                    attrBodyCursor.length()));
          }
          // extract the v4 next-hop
          v4Nexthop =
              folly::IPAddress::fromLongHBO(attrBodyCursor.readBE<uint32_t>());
          *attrs.nexthop() = v4Nexthop.str();
        } // NEXT_HOP
        break;
        case BgpAttrCode::BGP_ATTR_MED: {
          attrs.med() = attrBodyCursor.readBE<uint32_t>();
          attrs.isMedSet() = true;
        } // MED
        break;
        case BgpAttrCode::BGP_ATTR_LOCAL_PREF: {
          attrs.localPref() = attrBodyCursor.readBE<uint32_t>();
        } // LOCAL_PREF
        break;
        case BgpAttrCode::BGP_ATTR_ATOMIC_AGGREGATE: {
          attrs.atomicAggregate() = true;
        } // ATOMIC_AGGREGATE
        break;
        case BgpAttrCode::BGP_ATTR_AGGREGATOR: {
          if (*capa.as4byte()) {
            attrs.aggregator()->asn() = attrBodyCursor.readBE<uint32_t>();
          } else {
            attrs.aggregator()->asn() = attrBodyCursor.readBE<uint16_t>();
          }

          // Read aggregator's IPv4 address
          *attrs.aggregator()->ip() =
              folly::IPAddress::fromLongHBO(attrBodyCursor.readBE<uint32_t>())
                  .str();

          // Validate length of AGGREGATOR attribute
          if (attrBodyCursor.length()) {
            throw BgpUpdateMsgException(
                BgpNotifUpdateMsgErrSubCode::BN_UM_OPTIONAL_ATTR_ERROR,
                attrBeingParsed,
                fmt::format(
                    "Unexpected length of bgp attr AGGREGATOR. "
                    "expected: {}, received: {}",
                    4,
                    rawAttrLen));
          }
        } // AGGREGATOR
        break;
        case BgpAttrCode::BGP_ATTR_COMMUNITIES: {
          while (attrBodyCursor.length()) {
            BgpAttrCommunity community;

            community.asn() = attrBodyCursor.readBE<uint16_t>();
            community.value() = attrBodyCursor.readBE<uint16_t>();

            attrs.communities()->push_back(community);
          }
        } // COMMUNITIES
        break;
        case BgpAttrCode::BGP_ATTR_ORIGINATOR_ID: {
          // NOTE: we store originator in network byte order
          attrs.originatorId() = attrBodyCursor.read<uint32_t>();
          if (attrBodyCursor.length()) {
            throw BgpUpdateMsgException(
                BgpNotifUpdateMsgErrSubCode::BN_UM_ATTR_LEN_ERR,
                attrBeingParsed,
                fmt::format(
                    "Unexpected length of bgp attr ORIGINATOR_ID. "
                    "expected: {}, received: {}",
                    4,
                    rawAttrLen));
          }
        } // ORIGINATOR_ID
        break;
        case BgpAttrCode::BGP_ATTR_CLUSTER_LIST: {
          while (attrBodyCursor.length()) {
            // NOTE: we store cluster members in network byte order
            attrs.clusterList()->push_back(attrBodyCursor.read<uint32_t>());
          }
        } // CLUSTER_LIST
        break;
        case BgpAttrCode::BGP_ATTR_MP_REACH_NLRI: {
          XLOGF(
              DBG4,
              "MP_REACH_NLRI raw:\n{}",
              folly::hexDump(attrBodyCursor.data(), attrBodyCursor.length()));

          DCHECK(!mpReachNlriFound);
          mpReachNlriFound = true;

          // Parse afi,safi and move pos ahead
          {
            auto afi = attrBodyCursor.readBE<uint16_t>();
            auto safi = attrBodyCursor.read<uint8_t>();

            mpAnnouncedAfi = static_cast<BgpUpdateAfi>(afi);
            mpAnnouncedSafi = static_cast<BgpUpdateSafi>(safi);
          }

          // Parse Nexthop and MP_NLRI information
          switch (mpAnnouncedAfi) {
            case BgpUpdateAfi::AFI_IPv4: {
              // nexthop
              auto nextHopLen = attrBodyCursor.read<uint8_t>();
              // check for extended nexthop encoding capability
              const bool extNhEncoding =
                  capa.extNHEncodingCapabilities()->size() > 0;
              /*
               * If extended nexthop encoding not enabled, Size of nexthop must
               * be 4
               */
              if (!extNhEncoding && nextHopLen != 4) {
                throw BgpUpdateMsgException(
                    BgpNotifUpdateMsgErrSubCode::BN_UM_OPTIONAL_ATTR_ERROR,
                    attrBeingParsed,
                    fmt::format(
                        "Unexpected length of mpNexthop. expected: 4 "
                        "received: {}, extended nexthop encoding disabled",
                        (uint32_t)nextHopLen));
              }
              if (extNhEncoding && nextHopLen != 4 && nextHopLen != 16 &&
                  nextHopLen != 32) {
                throw BgpUpdateMsgException(
                    BgpNotifUpdateMsgErrSubCode::BN_UM_OPTIONAL_ATTR_ERROR,
                    attrBeingParsed,
                    fmt::format(
                        "Unexpected length of mpNexthop. expected: 4, 16 "
                        "or 32, received: {}",
                        (uint32_t)nextHopLen));
              }
              // read nexthop
              if (nextHopLen == 4) {
                mpNexthop = folly::IPAddress::fromLongHBO(
                    attrBodyCursor.readBE<uint32_t>());
              } else {
                // grab the first next-hop
                std::array<uint8_t, 16> buf = {{0}};
                attrBodyCursor.pull(buf.data(), 16);
                mpNexthop = folly::IPAddress::fromBinary(
                    folly::ByteRange(buf.data(), 16));

                // the second next-hop is link-local, if present, skip
                if (nextHopLen == 32) {
                  attrBodyCursor.skip(16);
                }
              }

              // reserved byte, yo
              attrBodyCursor.skip(1);

              // Parse MP_NLRI
              auto nlriBuf = IOBuf::wrapBuffer(
                  attrBodyCursor.data(), attrBodyCursor.length());
              Cursor nlriCursor(nlriBuf.get());
              attrBodyCursor.skip(attrBodyCursor.length());

              mpAnnounced = parseMpNlri(
                  nlriCursor,
                  BgpUpdateAfi::AFI_IPv4,
                  mpAnnouncedSafi,
                  capa,
                  attrBeingParsed,
                  attrType);

              break;
            }
            case BgpUpdateAfi::AFI_IPv6: {
              // Read length of nexthop
              auto nextHopLen = attrBodyCursor.read<uint8_t>();
              /*
               * Read nexthop (Size of nexthop can be 16 or 32). We just need
               * to read first 16 bytes as it always contains globally routable
               * nexthop
               */
              if (nextHopLen != 16 && nextHopLen != 32) {
                throw BgpUpdateMsgException(
                    BgpNotifUpdateMsgErrSubCode::BN_UM_OPTIONAL_ATTR_ERROR,
                    attrBeingParsed,
                    fmt::format(
                        "Unexpected length of mpNexthop. expected: 16 "
                        "or 32, received: {}",
                        (uint32_t)nextHopLen));
              }
              // grab the first next-hop
              std::array<uint8_t, 16> buf = {{0}};
              attrBodyCursor.pull(buf.data(), 16);
              mpNexthop = folly::IPAddress::fromBinary(
                  folly::ByteRange(buf.data(), 16));

              // the second next-hop is link-local, if present, skip
              if (nextHopLen == 32) {
                attrBodyCursor.skip(16);
              }

              // reserved byte, yo
              attrBodyCursor.skip(1);

              auto nlriBuf = IOBuf::wrapBuffer(
                  attrBodyCursor.data(), attrBodyCursor.length());
              Cursor nlriCursor(nlriBuf.get());
              attrBodyCursor.skip(attrBodyCursor.length());

              mpAnnounced = parseMpNlri(
                  nlriCursor,
                  BgpUpdateAfi::AFI_IPv6,
                  mpAnnouncedSafi,
                  capa,
                  attrBeingParsed,
                  attrType);

              break;
            }
            default:
              XLOGF(
                  ERR,
                  "Skipping unsupported afi {}",
                  static_cast<int>(mpAnnouncedAfi));
          }
        } break;
        case BgpAttrCode::BGP_ATTR_MP_UNREACH_NLRI: {
          XLOGF(
              DBG4,
              "MP_UNREACH_NLRI raw:\n{}",
              folly::hexDump(attrBodyCursor.data(), attrBodyCursor.length()));

          DCHECK(!mpUnreachNlriFound);
          mpUnreachNlriFound = true;

          // Parse afi and move pos ahead
          {
            auto afi = attrBodyCursor.readBE<uint16_t>();
            auto safi = attrBodyCursor.read<uint8_t>();

            // save the AFI/SAFI
            mpWithdrawnAfi = static_cast<BgpUpdateAfi>(afi);
            mpWithdrawnSafi = static_cast<BgpUpdateSafi>(safi);
          }

          // Parse MP_NLRI information
          switch (mpWithdrawnAfi) {
            case BgpUpdateAfi::AFI_IPv4: {
              if (!attrBodyCursor.length()) {
                XLOG(DBG4, "IPv4 MP End-of-Rib");

                eor = BgpEndOfRib();

                eor->isMpEor() = true;
                eor->afi() = BgpUpdateAfi::AFI_IPv4;
                eor->safi() = mpWithdrawnSafi;

                // No NLRI information to process
                break;
              }

              // Parse MP_NLRI
              auto nlriBuf = IOBuf::wrapBuffer(
                  attrBodyCursor.data(), attrBodyCursor.length());
              Cursor nlriCursor(nlriBuf.get());
              attrBodyCursor.skip(attrBodyCursor.length());

              mpWithdrawn = parseMpNlri(
                  nlriCursor,
                  BgpUpdateAfi::AFI_IPv4,
                  mpWithdrawnSafi,
                  capa,
                  attrBeingParsed,
                  attrType);
            } break;
            case BgpUpdateAfi::AFI_IPv6: {
              if (!attrBodyCursor.length()) {
                XLOG(DBG4, "IPv6 End-of-Rib");

                eor = BgpEndOfRib();

                eor->isMpEor() = true;
                eor->afi() = BgpUpdateAfi::AFI_IPv6;
                eor->safi() = mpWithdrawnSafi;

                // No NLRI information to process
                break;
              }

              auto nlriBuf = IOBuf::wrapBuffer(
                  attrBodyCursor.data(), attrBodyCursor.length());
              Cursor nlriCursor(nlriBuf.get());
              attrBodyCursor.skip(attrBodyCursor.length());

              mpWithdrawn = parseMpNlri(
                  nlriCursor,
                  BgpUpdateAfi::AFI_IPv6,
                  mpWithdrawnSafi,
                  capa,
                  attrBeingParsed,
                  attrType);
            } break;
            default:
              throw BgpUpdateMsgException(
                  BgpNotifUpdateMsgErrSubCode::BN_UM_OPTIONAL_ATTR_ERROR,
                  attrBeingParsed,
                  fmt::format("unsupported afi: {}", (uint32_t)mpAnnouncedAfi));
          }
        } // MP_UNREACH_NLRI
        break;
        case BgpAttrCode::BGP_ATTR_EXTENDED_COMMUNITIES: {
          while (attrBodyCursor.length()) {
            /*
             * Read 8 bytes into two word segments. The structure of Extended
             * communities is complicated and we only want to have blacklisting
             * matching functionality on EXT_COMMUNITIES attribute.
             */
            BgpAttrExtCommunity extCom;

            extCom.firstWord() = attrBodyCursor.readBE<uint32_t>();
            extCom.secondWord() = attrBodyCursor.readBE<uint32_t>();
            attrs.extCommunities()->emplace_back(std::move(extCom));
          }
        } // EXT_COMMUNITIES
        break;
        case BgpAttrCode::BGP_ATTR_LARGE_COMMUNITIES: {
          while (attrBodyCursor.length()) {
            BgpAttrLargeCommunity largeCom;

            largeCom.asn() = attrBodyCursor.readBE<uint32_t>();
            largeCom.localData1() = attrBodyCursor.readBE<uint32_t>();
            largeCom.localData2() = attrBodyCursor.readBE<uint32_t>();
            attrs.largeCommunities()->emplace_back(std::move(largeCom));
          }
        } // LARGE_COMMUNITIES
        break;
        default: {
          // log unsupported path attributes received
          XLOGF_EVERY_MS(
              WARN,
              60000,
              "Unknown Bgp Attribute. Code: {}",
              static_cast<uint32_t>(attrType));
        } // Unknown attribute
      } // switch(attrType)

    } catch (std::out_of_range const& err) {
      throw BgpUpdateMsgException(
          BgpNotifUpdateMsgErrSubCode::BN_UM_MALFORMED_ATTRIBUTE_LIST,
          std::string(),
          fmt::format(
              "out_of_range exception happened when parsing bgp attributes: {}",
              err.what()));
    }
  }
}

void UpdateMsgParsingState::parseV4Announced(
    Cursor& updateMsgCursor,
    const BgpCapabilities& capabilities) {
  if (!updateMsgCursor.length()) {
    return;
  }

  v4Announced = parseNlri(
      updateMsgCursor,
      BgpUpdateAfi::AFI_IPv4,
      BgpUpdateSafi::SAFI_UNICAST,
      false,
      capabilities);
  updateMsgCursor.skip(updateMsgCursor.length());
}

void UpdateMsgParsingState::doSanityChecks() {
  if (!v4Announced.empty() || !mpAnnounced.empty()) {
    if (!hasOriginAttr || !hasAsPathAttr) {
      auto attrType = BgpAttrCode::BGP_ATTR_ORIGIN;
      if (hasOriginAttr) {
        attrType = BgpAttrCode::BGP_ATTR_AS_PATH;
      }
      throw BgpUpdateMsgException(
          BgpNotifUpdateMsgErrSubCode::BN_UM_MISSING_WELL_KNOWN_ATTR,
          std::string(
              reinterpret_cast<const char*>(&attrType), sizeof(attrType)),
          "Missing mandatory bgp-attributes in update message.");
    }
  }

  if (!v4Announced.empty() && v4Nexthop.empty()) {
    auto attrType = BgpAttrCode::BGP_ATTR_NEXT_HOP;
    throw BgpUpdateMsgException(
        BgpNotifUpdateMsgErrSubCode::BN_UM_MISSING_WELL_KNOWN_ATTR,
        std::string(reinterpret_cast<const char*>(&attrType), sizeof(attrType)),
        "Missing mandatory bgp-attributes in update message.");
  }
}

void parseBgpCapabilities(Cursor cursor, BgpCapabilities& capa) {
  uint8_t cCode, cLen{0}, safi{0}, sor;
  uint16_t afi{0};

  XLOGF(
      DBG4,
      "parseBgpCapabilities: dumping full capabilities buffer:\n{}",
      folly::hexDump(cursor.data(), cursor.length()));

  while (cursor.length()) {
    try {
      cCode = cursor.read<uint8_t>();
      cLen = cursor.read<uint8_t>();
      XLOGF(
          DBG4,
          "parseBgpCapabilities: cursor len: {} sub cursor request len: {}",
          cursor.length(),
          int(cLen));

      if (cLen > cursor.length()) {
        throw BgpOpenMsgException(
            BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC,
            std::string(),
            fmt::format(
                "The length of capability exceeds "
                "the remaining message length. "
                "Remaining message length: {}, "
                "capability length: {}",
                cursor.length(),
                cLen));
      }

      // parse a particular capability
      auto subCursorBuf = IOBuf::wrapBuffer(cursor.data(), cLen);
      Cursor subCursor(subCursorBuf.get());
      cursor.skip(cLen);

      XLOGF(
          DBG4,
          "parseBgpCapabilities: dumping buffer for capa: {}\n{}",
          (int)cCode,
          folly::hexDump(subCursor.data(), subCursor.length()));

      switch (cCode) {
        // See rfc4760
        case (uint8_t)BgpCapability::CAPA_MP_EXT: {
          if (cLen != 4) {
            throw BgpOpenMsgException(
                BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC,
                std::string(),
                fmt::format(
                    "Unexpected length of MP-Extension capability. "
                    "expected 4, received: {}",
                    cLen));
          }

          afi = subCursor.readBE<uint16_t>();
          // 1 byte reserved
          subCursor.skip(1);
          safi = subCursor.read<uint8_t>();

          switch (afi) {
            case (int)BgpUpdateAfi::AFI_IPv4:
              if (safi == (int)BgpUpdateSafi::SAFI_UNICAST) {
                capa.mpExtV4Unicast() = true;
              } else if (safi == (int)BgpUpdateSafi::SAFI_LABELED_UNICAST) {
                capa.mpExtV4LU() = true;
              } else {
                // capability recognized, but malformed
                throw BgpOpenMsgException(
                    BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC,
                    std::string(),
                    fmt::format("Unknown safi value: {}.", (uint32_t)safi));
              }
              break;
            case (int)BgpUpdateAfi::AFI_IPv6:
              if (safi == (int)BgpUpdateSafi::SAFI_UNICAST) {
                capa.mpExtV6Unicast() = true;
              } else if (safi == (int)BgpUpdateSafi::SAFI_LABELED_UNICAST) {
                capa.mpExtV6LU() = true;
              } else {
                throw BgpOpenMsgException(
                    BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC,
                    std::string(),
                    fmt::format("Unknown safi value: {}.", (uint32_t)safi));
              }
              break;
            default:
              throw BgpOpenMsgException(
                  BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC,
                  std::string(),
                  fmt::format("Unknown afi value: {}.", (uint32_t)afi));
          }
          // set mp capability exist to true after we successfully parse fields
          capa.mpExtExist() = true;
        } // case CAPA_MP_EXT
        break;
        // rfc6793
        case (uint8_t)BgpCapability::CAPA_AS4_BYTE: {
          if (cLen != 4) {
            throw BgpOpenMsgException(
                BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC,
                std::string(),
                fmt::format(
                    "Unexpected length of 4-octet ASN capability. "
                    "expected 4, received: {}",
                    cLen));
          }
          capa.as4byte() = true;
          capa.asn() = subCursor.readBE<uint32_t>();
        } // case CAPAC_AS4_BYTE
        break;
        // rfc4724
        case (uint8_t)BgpCapability::CAPA_GR: {
          if (cLen % 4 < 2) {
            throw BgpOpenMsgException(
                BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC,
                std::string(),
                "Unexpected length of BGP graceful restart capability.");
          }

          capa.gracefulRestart() = true;

          auto value = subCursor.readBE<uint16_t>();
          // First bit is "restarting" flag
          capa.isRestarting() = (value & kGrRestartStateBit) != 0;
          // Second bit is "notification" flag, RFC 8538
          capa.grNotification() = (value & kGrNotificationBit) != 0;
          capa.restartTime() = value & kGrRestartTimeMask;

          std::vector<BgpGrCapability> grCapabilities;
          while (subCursor.length()) {
            BgpGrCapability grCapa;

            afi = subCursor.readBE<uint16_t>();
            safi = subCursor.read<uint8_t>();

            if (apache::thrift::util::enumName(
                    static_cast<BgpUpdateAfi>(afi)) == nullptr ||
                apache::thrift::util::enumName(
                    static_cast<BgpUpdateSafi>(safi)) == nullptr) {
              throw BgpOpenMsgException(
                  BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC,
                  std::string(),
                  fmt::format("Unknown afi-{}, safi-{}.", afi, safi));
            }

            grCapa.afi() = (static_cast<BgpUpdateAfi>(afi));
            grCapa.safi() = (static_cast<BgpUpdateSafi>(safi));
            grCapa.forwardingState() = (subCursor.read<uint8_t>() >> 7);

            grCapabilities.push_back(grCapa);
          }
          capa.grCapabilities() = grCapabilities;
        } // case CAPA_GR
        break;
        case (uint8_t)BgpCapability::CAPA_ADD_PATH: {
          if (cLen % 4) {
            throw BgpOpenMsgException(
                BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC,
                std::string(),
                "Unexpected length of BGP add path capability.");
          }

          std::vector<BgpAddPathCapability> addPathCapabilities;

          while (subCursor.length()) {
            BgpAddPathCapability addPath;

            afi = subCursor.readBE<uint16_t>();
            safi = subCursor.read<uint8_t>();
            sor = subCursor.read<uint8_t>();
            /*
             * if we receive anything not falls in enum range, set it to
             * UNKOWN(0)
             */
            if (apache::thrift::util::enumName(
                    static_cast<BgpAddPathSendRec>(sor)) == nullptr) {
              XLOGF(
                  ERR,
                  "Peer send invalid add path capability {}, ignore this capability",
                  sor);
              continue;
            }

            if (apache::thrift::util::enumName(
                    static_cast<BgpUpdateAfi>(afi)) == nullptr ||
                apache::thrift::util::enumName(
                    static_cast<BgpUpdateSafi>(safi)) == nullptr) {
              throw BgpOpenMsgException(
                  BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC,
                  std::string(),
                  fmt::format("Unknown afi-{}, safi-{}.", afi, safi));
            }

            addPath.afi() = (static_cast<BgpUpdateAfi>(afi));
            addPath.safi() = (static_cast<BgpUpdateSafi>(safi));
            addPath.sor() = (static_cast<BgpAddPathSendRec>(sor));
            addPathCapabilities.push_back(addPath);
          }
          capa.addPathCapabilities() = addPathCapabilities;
        } // case CAPA_ADD_PATH
        break;
        // RFC 5549 Sec. 4
        case (uint8_t)BgpCapability::CAPA_EXT_NH_ENCODING: {
          if (cLen % 6) {
            /*
             * Capability value field will be a list of 6 octets <afi, safi,
             * nhAfi> Note here the safi is of 2 octets that is different from
             * other cases
             */
            throw BgpOpenMsgException(
                BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC,
                std::string(),
                "Unexpected length of BGP Extended Next Hop Encoding Capability.");
          }

          std::vector<BgpExtNHEncodingCapability> extNHEncodingCapabilities;

          while (subCursor.length()) {
            uint16_t nlriAfi = subCursor.readBE<uint16_t>();
            uint16_t nlriSafi = subCursor.readBE<uint16_t>();
            uint16_t nhAfi = subCursor.readBE<uint16_t>();

            // For the AFI/SAFI not supported, we simply ignore
            if (static_cast<BgpUpdateAfi>(nlriAfi) != BgpUpdateAfi::AFI_IPv4) {
              XLOGF(
                  ERR,
                  "NLRI AFI should be IPv4 in Ext NH Encoding Capa. Ignoring this Ext NH Encoding Capa <{},{},{}>",
                  nlriAfi,
                  nlriSafi,
                  nhAfi);
              continue;
            }

            if (apache::thrift::util::enumName(
                    static_cast<BgpUpdateSafi>(nlriSafi)) == nullptr) {
              XLOGF(
                  ERR,
                  "NLRI SAFI {} is not supported. Ignoring this Ext NH Encoding Capa <{},{},{}>",
                  nlriSafi,
                  nlriAfi,
                  nlriSafi,
                  nhAfi);
              continue;
            }

            if (static_cast<BgpUpdateAfi>(nhAfi) != BgpUpdateAfi::AFI_IPv6) {
              XLOGF(
                  ERR,
                  "Next Hop AFI should be IPv6 in Ext NH Encoding Capa. Ignoring this Ext NH Encoding Capa <{},{},{}>",
                  nlriAfi,
                  nlriSafi,
                  nhAfi);
              continue;
            }

            BgpExtNHEncodingCapability c;
            c.nlriAfi() = static_cast<BgpUpdateAfi>(nlriAfi);
            c.nlriSafi() = static_cast<BgpUpdateSafi>(nlriSafi);
            c.nhAfi() = static_cast<BgpUpdateAfi>(nhAfi);

            extNHEncodingCapabilities.emplace_back(std::move(c));
          }

          capa.extNHEncodingCapabilities() = extNHEncodingCapabilities;
        } // case CAPA_EXT_NH_ENCODING
        break;
        // RFC 2918 requires Route Refresh capability length to be 0.
        case (uint8_t)BgpCapability::CAPA_ROUTE_REFRESH: {
          if (cLen != 0) {
            throw BgpOpenMsgException(
                BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC,
                std::string(),
                fmt::format(
                    "RFC 2918 Route Refresh capability must have length 0, got {}",
                    cLen));
          }
          capa.routeRefresh() = true;
        } // case CAPA_ROUTE_REFRESH
        break;
        // RFC 7313 requires Enhanced Route Refresh capability length to be 0.
        case (uint8_t)BgpCapability::CAPA_ENHANCED_ROUTE_REFRESH: {
          if (cLen != 0) {
            throw BgpOpenMsgException(
                BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC,
                std::string(),
                fmt::format(
                    "RFC 7313 Enhanced Route Refresh capability must have length 0, got {}",
                    cLen));
          }
          capa.enhancedRouteRefresh() = true;
        } // case CAPA_ENHANCED_ROUTE_REFRESH
        break;
        default: {
          XLOGF_EVERY_MS(
              WARN, 100, "Unknown BGP Capability code: {}", (uint32_t)cCode);
        } break;
      } // switch
    } catch (std::out_of_range const& err) {
      throw BgpOpenMsgException(
          BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC,
          std::string(),
          fmt::format(
              "out_of_range exception happened when parsing bgp capabilities: {}",
              err.what()));
    }
  } // while
}

} // namespace detail
} // namespace bgplib
} // namespace nettools
} // namespace facebook
