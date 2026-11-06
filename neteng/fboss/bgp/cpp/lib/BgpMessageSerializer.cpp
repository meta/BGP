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

#include <folly/io/Cursor.h>

#include "common/network/AddressUtil.h"
#include "neteng/fboss/bgp/cpp/lib/BgpException.h"
#include "neteng/fboss/bgp/cpp/lib/BgpMessageSerializer.h"
#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"
#include "thrift/lib/cpp/util/EnumUtils.h"

using folly::CIDRNetwork;
using folly::IPAddress;
using folly::io::Cursor;
using folly::io::RWPrivateCursor;

namespace {
/*
 * 25 = AttrType: 2 + AttrLen(extended): 2 + Attr: 21
 * Attr 21 = afi: 2 + safi: 1 + nextHop len: 1 + v6 next hop: 16 + reserved: 1
 */
const size_t kMpAnnouncedAttrLen = 25;
/**
 * Bgp update message struct:
 *  create an IOBuf for BGP Update message with max capacity, prepare cursors
 *  for easy access to certain fields
 */
struct BgpUpdateMsg {
  BgpUpdateMsg()
      : bufPtr{[]() {
          auto buf = folly::IOBuf::createCombined(
              facebook::nettools::bgplib::kMaxBgpMsgLen);
          buf->append(facebook::nettools::bgplib::kMaxBgpMsgLen);
          return buf;
        }()},
        hcursor{bufPtr.get()},
        wlcursor{bufPtr.get()},
        plcursor{bufPtr.get()} {
    wlcursor.skip(facebook::nettools::bgplib::kBgpMsgHeaderLen);
    plcursor.skip(facebook::nettools::bgplib::kBgpMsgHeaderLen + 2);
  }

  /**
   * @params:
   *  bufPtr   - unique pointer of the IOBuf
   *  hcursor  - Head cursor, pointing to the beginning of IOBuf
   *  wlcursor - Withdrawn length cursor, pointing to beginning of
   *             withdrawn length field
   *             -> wlcursor = hcursor + kBgpMsgHeaderLen
   *  plcursor - Path attributes length cursor, pointing to beginning of
   *             path sttributes length field
   *             -> plcursor = wlcursor + 2 (two bytes for withdrawn length)
   */
  std::unique_ptr<folly::IOBuf> bufPtr;
  folly::io::RWPrivateCursor hcursor;
  folly::io::RWPrivateCursor wlcursor;
  folly::io::RWPrivateCursor plcursor;
};
} // namespace

namespace facebook {
namespace nettools {
namespace bgplib {

std::unique_ptr<folly::IOBuf> BgpMessageSerializer::serializeBgpOpenMsg(
    const BgpOpenMsg& msg) {
  // prepare message buffer
  auto buf = folly::IOBuf::createCombined(kMaxBgpMsgLen);
  buf->append(kMaxBgpMsgLen);
  RWPrivateCursor cursor(buf.get());

  // Leave room for BGP Header
  auto wcursor = cursor + kBgpMsgHeaderLen;

  // Write version, asn, hold time and bgp ID
  wcursor.write<uint8_t>((uint8_t)*msg.version());
  wcursor.writeBE<uint16_t>((uint16_t)*msg.asn());
  wcursor.writeBE<uint16_t>((uint16_t)*msg.holdTime());
  wcursor.writeBE<uint32_t>((uint32_t)*msg.bgpID());

  // BGP Capability param
  auto ccursor = wcursor + 1; // Leaving 1 byte for Open Msg params length
  ccursor.write<uint8_t>((uint8_t)2); // Param Type: BGP_CAPABILITIES
  ccursor += 1; // Leaving 1 byte for capabilities param length

  const auto& capa = *msg.capabilities();
  std::vector<std::pair<uint16_t, uint8_t>> mpCaps;
  if (*capa.mpExtV4Unicast()) {
    mpCaps.emplace_back(
        (uint16_t)BgpUpdateAfi::AFI_IPv4, (uint8_t)BgpUpdateSafi::SAFI_UNICAST);
  }
  if (*capa.mpExtV6Unicast()) {
    mpCaps.emplace_back(
        (uint16_t)BgpUpdateAfi::AFI_IPv6, (uint8_t)BgpUpdateSafi::SAFI_UNICAST);
  }
  if (*capa.mpExtV4LU()) {
    mpCaps.emplace_back(
        (uint16_t)BgpUpdateAfi::AFI_IPv4,
        (uint8_t)BgpUpdateSafi::SAFI_LABELED_UNICAST);
  }
  if (*capa.mpExtV6LU()) {
    mpCaps.emplace_back(
        (uint16_t)BgpUpdateAfi::AFI_IPv6,
        (uint8_t)BgpUpdateSafi::SAFI_LABELED_UNICAST);
  }
  if (*capa.mpExtLs()) {
    mpCaps.emplace_back(
        (uint16_t)BgpUpdateAfi::AFI_LS, (uint8_t)BgpUpdateSafi::SAFI_LS);
  }
  for (const auto& mpCapa : mpCaps) {
    ccursor.write<uint8_t>((uint8_t)BgpCapability::CAPA_MP_EXT);
    ccursor.write<uint8_t>((uint8_t)4); // Capability Length
    ccursor.writeBE<unsigned short>(mpCapa.first); // 2 byte AFI
    ccursor.write<uint8_t>((uint8_t)0); // Reserved byte (set it to 0)
    ccursor.write<unsigned char>(mpCapa.second); // 1 byte SAFI
  }

  if (*capa.as4byte()) {
    ccursor.write<uint8_t>((uint8_t)BgpCapability::CAPA_AS4_BYTE);
    ccursor.write<uint8_t>((uint8_t)4); // Capability Length
    ccursor.writeBE<uint32_t>((uint32_t)*capa.asn()); // capability value
  }

  // Bgp Graceful Restart capability
  if (*capa.gracefulRestart()) {
    // Write capability code and length
    ccursor.write<uint8_t>((uint8_t)BgpCapability::CAPA_GR);
    ccursor.write<uint8_t>((uint8_t)(2 + 4 * capa.grCapabilities()->size()));
    // Write restart state and time
    // Consider only 12 LS bits
    uint16_t grValue = *capa.restartTime() & kGrRestartTimeMask;
    if (*capa.isRestarting()) {
      grValue |= kGrRestartStateBit; // Set the first bit of grValue
    }
    if (*capa.grNotification()) {
      grValue |= kGrNotificationBit; // Set the second bit of grValue
    }
    ccursor.writeBE<uint16_t>((uint16_t)grValue);
    // Write afi, safi and their forwarding state
    for (auto& grCapa : *capa.grCapabilities()) {
      ccursor.writeBE<uint16_t>((uint16_t)*grCapa.afi());
      ccursor.write<uint8_t>((uint8_t)*grCapa.safi());
      ccursor.write<uint8_t>((uint8_t)(*grCapa.forwardingState() << 7));
    }
  }

  if (capa.addPathCapabilities()->size() > 0) {
    // Write capability code and length
    ccursor.write<uint8_t>((uint8_t)BgpCapability::CAPA_ADD_PATH);
    ccursor.write<uint8_t>((uint8_t)(4 * capa.addPathCapabilities()->size()));
    // Write afi, safi and their forwarding state
    for (auto& addPathCapa : *capa.addPathCapabilities()) {
      ccursor.writeBE<uint16_t>((uint16_t)*addPathCapa.afi());
      ccursor.write<uint8_t>((uint8_t)*addPathCapa.safi());
      ccursor.write<uint8_t>((uint8_t)(*addPathCapa.sor()));
    }
  }

  // RFC 5549 Extended Next Hop Encoding Capability
  if (capa.extNHEncodingCapabilities()->size() > 0) {
    ccursor.write<uint8_t>((uint8_t)BgpCapability::CAPA_EXT_NH_ENCODING);
    ccursor.write<uint8_t>(
        (uint8_t)(6 * capa.extNHEncodingCapabilities()->size()));
    for (auto& extNHEncodingCapability : *capa.extNHEncodingCapabilities()) {
      ccursor.writeBE<uint16_t>((uint16_t)*extNHEncodingCapability.nlriAfi());
      ccursor.writeBE<uint16_t>((uint16_t)*extNHEncodingCapability.nlriSafi());
      ccursor.writeBE<uint16_t>((uint16_t)*extNHEncodingCapability.nhAfi());
    }
  }

  // RFC 2918 Route Refresh Capability
  if (*capa.routeRefresh()) {
    ccursor.write<uint8_t>((uint8_t)BgpCapability::CAPA_ROUTE_REFRESH);
    ccursor.write<uint8_t>((uint8_t)0); // Capability Length
  }

  // RFC 7313 Enhanced Route Refresh Capability
  if (*capa.enhancedRouteRefresh()) {
    ccursor.write<uint8_t>((uint8_t)BgpCapability::CAPA_ENHANCED_ROUTE_REFRESH);
    ccursor.write<uint8_t>((uint8_t)0); // Capability Length
  }

  wcursor.write<uint8_t>((uint8_t)(ccursor - wcursor - 1)); // Open msg params
                                                            // length
  wcursor += 1; // Skip BGP Capability Param Type
  wcursor.write<uint8_t>((uint8_t)(ccursor - wcursor - 1)); // Capabilities
                                                            // length
  wcursor = ccursor; // Set wcursor to the end of data (at ccursor)

  auto len = serializeBgpHeader(cursor, wcursor, BGP_MSG_TYPE_OPEN);
  buf->trimEnd(kMaxBgpMsgLen - len);
  return buf;
}

std::unique_ptr<folly::IOBuf> BgpMessageSerializer::serializeBgpNotification(
    const BgpNotification& notif) {
  // prepare message buffer
  auto buf = folly::IOBuf::createCombined(kMaxBgpMsgLen);
  buf->append(kMaxBgpMsgLen);
  RWPrivateCursor cursor(buf.get());

  // Leave room for BGP Header
  auto wcursor = cursor + kBgpMsgHeaderLen;

  serializeBgpNotificationBody(wcursor, notif);

  auto len = serializeBgpHeader(cursor, wcursor, BGP_MSG_TYPE_NOTIFICATION);
  buf->trimEnd(kMaxBgpMsgLen - len);
  return buf;
}

void BgpMessageSerializer::serializeBgpNotificationBody(
    RWPrivateCursor& cursor,
    const BgpNotification& notif) {
  // Write errCode, errSubCode and data
  cursor.write<uint8_t>((uint8_t)*notif.errCode());
  cursor.write<uint8_t>((uint8_t)*notif.errSubCode());
  cursor.push((const uint8_t*)notif.data()->c_str(), notif.data()->size());
}

std::unique_ptr<folly::IOBuf> BgpMessageSerializer::serializeBgpKeepAlive() {
  // prepare message buffer
  auto buf = folly::IOBuf::createCombined(kMaxBgpMsgLen);
  buf->append(kMaxBgpMsgLen);
  RWPrivateCursor cursor(buf.get());

  // Leave room for BGP Header
  auto wcursor = cursor + kBgpMsgHeaderLen;
  auto len = serializeBgpHeader(cursor, wcursor, BGP_MSG_TYPE_KEEPALIVE);
  buf->trimEnd(kMaxBgpMsgLen - len);
  return buf;
}

std::unique_ptr<folly::IOBuf> BgpMessageSerializer::serializeBgpEndOfRib(
    BgpUpdateAfi afi,
    BgpUpdateSafi safi) {
  // prepare message buffer
  auto buf = folly::IOBuf::createCombined(kMaxBgpMsgLen);
  buf->append(kMaxBgpMsgLen);
  RWPrivateCursor cursor(buf.get());

  // Leave room for BGP Header
  auto wcursor = cursor + kBgpMsgHeaderLen;

  // No withdrawn routes!
  wcursor.writeBE<uint16_t>((uint16_t)0); // Withdrawn routes length

  if (afi == BgpUpdateAfi::AFI_IPv4 && safi == BgpUpdateSafi::SAFI_UNICAST) {
    // No path attribute for IPv4-Unicast EndOfRib
    wcursor.writeBE<uint16_t>((uint16_t)0); // Path attribute length
  } else {
    wcursor.writeBE<uint16_t>((uint16_t)7); // Path attribute length
    // Serialize empty MP_UNREACH_NLRI attribute with <afi,safi> pair
    wcursor.write<uint8_t>(
        (uint8_t)(BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_EXTENDED));
    wcursor.write<uint8_t>(
        static_cast<uint8_t>(BgpAttrCode::BGP_ATTR_MP_UNREACH_NLRI));
    wcursor.writeBE<uint16_t>((uint16_t)3); // Length of attribute
    wcursor.writeBE<uint16_t>((uint16_t)afi);
    wcursor.write<uint8_t>((uint8_t)safi);
  }

  auto len = serializeBgpHeader(cursor, wcursor, BGP_MSG_TYPE_UPDATE);
  buf->trimEnd(kMaxBgpMsgLen - len);
  return buf;
}

std::unique_ptr<folly::IOBuf> BgpMessageSerializer::serializeBgpRouteRefresh(
    const BgpRouteRefresh& routeRefresh) {
  // prepare message buffer
  auto buf = folly::IOBuf::createCombined(kMaxBgpMsgLen);
  buf->append(kMaxBgpMsgLen);
  RWPrivateCursor cursor(buf.get());

  // Leave room for BGP Header
  auto wcursor = cursor + kBgpMsgHeaderLen;

  // Write AFI, message subtype and SAFI
  wcursor.writeBE<uint16_t>((uint16_t)*routeRefresh.afi());
  wcursor.write<uint8_t>((uint8_t)*routeRefresh.msgSubType());
  wcursor.write<uint8_t>((uint8_t)*routeRefresh.safi());

  auto len = serializeBgpHeader(cursor, wcursor, BGP_MSG_TYPE_ROUTE_REFRESH);
  buf->trimEnd(kMaxBgpMsgLen - len);
  return buf;
}

std::unique_ptr<folly::IOBuf> BgpMessageSerializer::serializeBgpUpdate2(
    const BgpUpdate2& update,
    bool as4byte,
    bool extNhEncoding,
    std::vector<std::tuple<size_t, size_t, bool>>* outNexthopOffsets) {
  if ((update.v4Announced2()->size() > 0 && update.v4Announced()->size() > 0) ||
      (update.v4Withdrawn2()->size() > 0 && update.v4Withdrawn()->size() > 0)) {
    throw BgpSerializerException(
        BgpSerializerExceptionCode::VERSION_ERROR,
        "Client use both version for v4 announcement/withdrawn");
  }

  auto v4Announced = *update.v4Announced2();
  auto v4Withdrawn = *update.v4Withdrawn2();

  /*
   * Client is still using deprecated v4Announced/v4Withdrawn, populate routes
   * in v4Announced2/v4Withdrawn2 format
   */
  if (v4Announced.empty()) {
    for (const auto& prf : *update.v4Announced()) {
      RiggedIPPrefix rigPrf;
      *rigPrf.prefix() = prf;
      v4Announced.push_back(std::move(rigPrf));
    }
  }
  if (v4Withdrawn.size() == 0) {
    for (const auto& prf : *update.v4Withdrawn()) {
      RiggedIPPrefix rigPrf;
      *rigPrf.prefix() = prf;
      v4Withdrawn.push_back(std::move(rigPrf));
    }
  }

  const auto& mpAnnounced = *update.mpAnnounced();
  const auto& mpWithdrawn = *update.mpWithdrawn();

  if (v4Announced.empty() && v4Withdrawn.empty() &&
      mpAnnounced.prefixes()->empty() && mpWithdrawn.prefixes()->empty()) {
    throw BgpSerializerException(
        BgpSerializerExceptionCode::MISSING_NLRI_INFO,
        "Missing NLRI information.");
  }

  // error checking for extended nexthop setting
  if (!mpAnnounced.prefixes()->empty()) {
    const auto& nh = network::toIPAddress(*mpAnnounced.nexthop());
    if (!extNhEncoding &&
        (*mpAnnounced.afi() == BgpUpdateAfi::AFI_IPv4 && nh.isV6())) {
      throw BgpSerializerException(
          BgpSerializerExceptionCode::EXT_NH_ENCODING_NOT_SUPPORTTED,
          "V4 announcement has v6 nexthop, but extended nexthop encoding is "
          "not negotiated.");
    }
  }

  // Prepare IOBuf queue
  folly::IOBufQueue queue{};
  int ret = 0;

  // Track nexthop locations across all messages (only when requested)
  const bool trackNexthopOffsets = (outNexthopOffsets != nullptr);
  /*
   * Tracks which buffer in the queue will contain nexthops.
   * IMPORTANT: queueIndex is set BEFORE appending the buffer to the queue,
   * ensuring it accurately represents the buffer's position at tracking time.
   * We track ALL nexthops across ALL messages (not just the first one).
   */
  size_t queueIndex = 0;

  // --- Serialize v4-Unicast withdrawn NLRI information if any ---
  if (!v4Withdrawn.empty()) {
    const size_t maxV4PrefixLen =
        getMaxPrefixLen(v4Withdrawn.front().pathId().has_value(), true);

    /*
     * Keep feeding new IOBuf element until we finish the size of all
     * withdrawn prefixes
     */
    size_t curPrefixCnt = 0;
    while (curPrefixCnt < v4Withdrawn.size()) {
      // Create new message buffer
      BgpUpdateMsg curMsg{};
      // Current write cursor, leave 2 bytes for Withdrawn Prefix Length
      auto wcursor = curMsg.wlcursor + 2;

      /*
       * Remaining buffer size is max-size minus position to current cursor
       * position from header start
       */
      auto bufSize = kMaxBgpMsgLen - (wcursor - curMsg.hcursor);
      /*
       * NOTE: Adjust buffer size for anything used up in buffer besides
       * NLRI
       */
      // Path Attr Length
      bufSize -= sizeof(uint16_t);
      ret = serializeNlri(
          wcursor,
          BgpUpdateAfi::AFI_IPv4,
          BgpUpdateSafi::SAFI_UNICAST,
          v4Withdrawn,
          v4Withdrawn.size(),
          bufSize,
          maxV4PrefixLen,
          curPrefixCnt);
      wcursor.skip(ret);
      // Path Attr Length
      wcursor.writeBE<uint16_t>(static_cast<uint16_t>(0));
      // Withdrawn Prefix Length
      curMsg.wlcursor.writeBE<uint16_t>(static_cast<uint16_t>(ret));
      // Serilize header
      ret = serializeBgpHeader(curMsg.hcursor, wcursor, BGP_MSG_TYPE_UPDATE);
      // Trim tailing length
      curMsg.bufPtr->trimEnd(kMaxBgpMsgLen - ret);
      queue.append(std::move(curMsg.bufPtr));
      queueIndex++; // Track queue position for nexthop offset
    }
  }
  // --- End of withdrawn NLRI ---

  // --- Write MP_UNREACH_NLRI attribute if any ---
  if (!mpWithdrawn.prefixes()->empty()) {
    const size_t maxV6PrefixLen = getMaxPrefixLen(
        mpWithdrawn.prefixes()->front().pathId().has_value(), false);

    /*
     * Keep feeding new IOBuf element until we finish the size of all
     * withdrawn prefixes
     */
    size_t curPrefixCnt = 0;
    while (curPrefixCnt < mpWithdrawn.prefixes()->size()) {
      BgpUpdateMsg curMsg{};
      // Current write cursor, leave 2 bytes for Path Attributes Length
      auto wcursor = curMsg.plcursor + 2;

      // Serialize attr type
      wcursor.write<uint8_t>(static_cast<uint8_t>(
          BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_EXTENDED));
      wcursor.write<uint8_t>(
          static_cast<uint8_t>(BgpAttrCode::BGP_ATTR_MP_UNREACH_NLRI));
      // MpWithdrawn Attribute Length cursor
      auto mplcursor = wcursor;
      wcursor.skip(2); // Leave 2 bytes for mpWithdrawn attr length

      // Serialize afi,safi values
      wcursor.writeBE<uint16_t>(static_cast<uint16_t>(*mpWithdrawn.afi()));
      wcursor.write<uint8_t>(static_cast<uint8_t>(*mpWithdrawn.safi()));

      /*
       * Remaining buffer size is max-size minus position to current cursor
       * position from header start
       */
      auto bufSize = kMaxBgpMsgLen - (wcursor - curMsg.hcursor);
      /*
       * NOTE: Adjust buffer size for anything used up in buffer besides
       * NLRI
       */
      ret = serializeNlri(
          wcursor,
          *mpWithdrawn.afi(),
          *mpWithdrawn.safi(),
          *mpWithdrawn.prefixes(),
          mpWithdrawn.prefixes()->size(),
          bufSize,
          maxV6PrefixLen,
          curPrefixCnt);
      wcursor.skip(ret);

      // MpWithdrawn Attr Length
      mplcursor.writeBE<uint16_t>(
          static_cast<uint16_t>(wcursor - mplcursor - 2));
      // Path Attr Length
      curMsg.plcursor.writeBE<uint16_t>(
          static_cast<uint16_t>(wcursor - curMsg.plcursor - 2));
      // Withdrawn Prefix Length
      curMsg.wlcursor.writeBE<uint16_t>(static_cast<uint16_t>(0));
      // Serilize header
      ret = serializeBgpHeader(curMsg.hcursor, wcursor, BGP_MSG_TYPE_UPDATE);
      // Trim tailing length
      curMsg.bufPtr->trimEnd(kMaxBgpMsgLen - ret);
      queue.append(std::move(curMsg.bufPtr));
      queueIndex++; // Track queue position for nexthop offset
    }
  }

  // TODO: serialize withdrawnBgpNlris

  // --- Serialize MP_REACH_NLRI attribute if any ---
  if (!mpAnnounced.prefixes()->empty()) {
    // --- Write common Attrbutes and sanity check path attr length ---
    auto attrBufPtr = folly::IOBuf::createCombined(kMaxBgpUpdateVarLen);
    attrBufPtr->append(kMaxBgpUpdateVarLen);
    RWPrivateCursor pcursor(attrBufPtr.get());
    Cursor cpcursor(pcursor);

    // remove v4Nexthop information for mpAnnounced
    const bool v4 = false;
    const size_t attrLen = serializePathAttrs(pcursor, update, as4byte, v4);
    const size_t maxV6PrefixLen = getMaxPrefixLen(
        mpAnnounced.prefixes()->front().pathId().has_value(), v4);

    // throw if path attributed can not fit in one message buf
    if (mpAnnounced.prefixes()->size() > 0 &&
        attrLen + kMpAnnouncedAttrLen + maxV6PrefixLen > kMaxBgpUpdateVarLen) {
      throw BgpSerializerException(
          BgpSerializerExceptionCode::EXCEEDED_MAX_SIZE,
          "too large Path Attr length");
    }

    /*
     * Keep feeding new IOBuf element until we finish the size of all
     * announced prefixes
     */
    size_t curPrefixCnt = 0;
    while (curPrefixCnt < mpAnnounced.prefixes()->size()) {
      BgpUpdateMsg curMsg{};
      // Current write cursor, leave 2 bytes for Path Attributes Length
      auto wcursor = curMsg.plcursor + 2;
      // Write attributs
      wcursor.push(cpcursor, attrLen);

      // mpAnnounced Attr
      wcursor.write<uint8_t>(static_cast<uint8_t>(
          BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_EXTENDED));
      wcursor.write<uint8_t>(
          static_cast<uint8_t>(BgpAttrCode::BGP_ATTR_MP_REACH_NLRI));
      // mpAnnounced Attr Length cursor
      auto mplcursor = wcursor;
      wcursor.skip(2); // Leave 2 bytes for mpAnnounced attr length

      // Serialize afi,safi values
      wcursor.writeBE<uint16_t>(static_cast<uint16_t>(*mpAnnounced.afi()));
      wcursor.write<uint8_t>(static_cast<uint8_t>(*mpAnnounced.safi()));
      // Serialize Nexthop attribute
      const auto& nh = network::toIPAddress(*mpAnnounced.nexthop());
      if (nh.isV4()) {
        wcursor.write<uint8_t>(static_cast<uint8_t>(4)); // Nexthop Length
        // Track nexthop offset for zero-copy optimization
        if (trackNexthopOffsets) {
          outNexthopOffsets->emplace_back(
              queueIndex, wcursor - curMsg.hcursor, true);
        }
        wcursor.write<uint32_t>(*(reinterpret_cast<uint32_t*>(
            const_cast<unsigned char*>(nh.bytes()))));
      } else {
        wcursor.write<uint8_t>(static_cast<uint8_t>(16)); // Nexthop Length
        // Track nexthop offset for zero-copy optimization
        if (trackNexthopOffsets) {
          outNexthopOffsets->emplace_back(
              queueIndex, wcursor - curMsg.hcursor, false);
        }
        const in6_addr in6 = nh.asV6().toAddr();
        wcursor.push(reinterpret_cast<const uint8_t*>(&in6), 16);
      }
      // Reserved byte
      wcursor.write<uint8_t>(static_cast<uint8_t>(0));
      // Write mpAnnounced NLRI
      /*
       * Remaining buffer size is max-size minus position to current cursor
       * position from header start
       */
      auto bufSize = kMaxBgpMsgLen - (wcursor - curMsg.hcursor);
      /*
       * NOTE: Adjust buffer size for anything used up in buffer besides
       * NLRI
       */
      ret = serializeNlri(
          wcursor,
          *mpAnnounced.afi(),
          *mpAnnounced.safi(),
          *mpAnnounced.prefixes(),
          mpAnnounced.prefixes()->size(),
          bufSize,
          maxV6PrefixLen,
          curPrefixCnt);
      wcursor.skip(ret);

      // MpAnnounced Attr Length
      mplcursor.writeBE<uint16_t>(
          static_cast<uint16_t>(wcursor - mplcursor - 2));
      // Path Attr Length
      curMsg.plcursor.writeBE<uint16_t>(
          static_cast<uint16_t>(wcursor - curMsg.plcursor - 2));
      // Withdrawn Prefix Length
      curMsg.wlcursor.writeBE<uint16_t>(static_cast<uint16_t>(0));
      // Serilize header
      ret = serializeBgpHeader(curMsg.hcursor, wcursor, BGP_MSG_TYPE_UPDATE);
      // Trim tailing length
      curMsg.bufPtr->trimEnd(kMaxBgpMsgLen - ret);
      queue.append(std::move(curMsg.bufPtr));
      queueIndex++; // Track queue position for nexthop offset
    }
  } // --- End of Path Attribute ---

  // --- Serialize v4-Unicast announced NLRI information if any ---
  if (!v4Announced.empty()) {
    // --- Write common Attrbutes and sanity check path attr length ---
    auto attrBufPtr = folly::IOBuf::createCombined(kMaxBgpUpdateVarLen);
    attrBufPtr->append(kMaxBgpUpdateVarLen);
    RWPrivateCursor pcursor(attrBufPtr.get());
    Cursor cpcursor(pcursor);

    const bool v4 = true;
    size_t nexthopOffsetInAttrs = 0;
    const size_t attrLen = serializePathAttrs(
        pcursor,
        update,
        as4byte,
        v4,
        trackNexthopOffsets ? &nexthopOffsetInAttrs : nullptr);
    const size_t maxV4PrefixLen =
        getMaxPrefixLen(v4Announced.front().pathId().has_value(), v4);

    // throw if path attributed can not fit in one message buf
    if (v4Announced.size() > 0 &&
        attrLen + maxV4PrefixLen > kMaxBgpUpdateVarLen) {
      throw BgpSerializerException(
          BgpSerializerExceptionCode::EXCEEDED_MAX_SIZE,
          "too large Path Attr length");
    }

    /*
     * Remaining buffer size is max-size minus position to current cursor
     * position from header start
     */
    size_t curPrefixCnt = 0;
    while (curPrefixCnt < v4Announced.size()) {
      BgpUpdateMsg curMsg{};
      // Current write cursor, leave 2 bytes for Path Attributes Length
      auto wcursor = curMsg.plcursor + 2;

      // Track nexthop offset for zero-copy optimization (all messages)
      if (trackNexthopOffsets) {
        /*
         * Nexthop offset = header + withdrawn length + path attr length +
         * offset in attrs
         */
        outNexthopOffsets->emplace_back(
            queueIndex,
            (curMsg.plcursor + 2 - curMsg.hcursor) + nexthopOffsetInAttrs,
            true);
      }

      // Write attributs
      wcursor.push(cpcursor, attrLen);
      // Write V4 Announced NLRI
      /*
       * Remaining buffer size is max-size minus position to current cursor
       * position from header start
       */
      auto bufSize = kMaxBgpMsgLen - (wcursor - curMsg.hcursor);
      /*
       * NOTE: Adjust buffer size for anything used up in buffer besides
       * NLRI
       */
      ret = serializeNlri(
          wcursor,
          BgpUpdateAfi::AFI_IPv4,
          BgpUpdateSafi::SAFI_UNICAST,
          v4Announced,
          v4Announced.size(),
          bufSize,
          maxV4PrefixLen,
          curPrefixCnt);
      wcursor.skip(ret);
      // Path Attr Length
      curMsg.plcursor.writeBE<uint16_t>(static_cast<uint16_t>(attrLen));
      // Withdrawn Prefix Length
      curMsg.wlcursor.writeBE<uint16_t>(static_cast<uint16_t>(0));
      // Serilize header
      ret = serializeBgpHeader(curMsg.hcursor, wcursor, BGP_MSG_TYPE_UPDATE);
      // Trim tailing length
      curMsg.bufPtr->trimEnd(kMaxBgpMsgLen - ret);
      queue.append(std::move(curMsg.bufPtr));
      queueIndex++; // Track queue position for nexthop offset
    }
  }

  // TODO: serialize announcedBgpNlris

  return std::make_unique<folly::IOBuf>(*queue.front());
}

int BgpMessageSerializer::serializeBgpHeader(
    RWPrivateCursor headCursor,
    RWPrivateCursor tailCursor,
    BgpMessageType msgType) {
  /*
   * Sanity check for bgp message length (msgLen is guaranteed to be less than
   * kMaxBgpMsgLen)
   */
  uint16_t msgLen = static_cast<uint16_t>(tailCursor - headCursor);
  if (msgLen > kMaxBgpMsgLen) {
    throw BgpSerializerException(
        BgpSerializerExceptionCode::EXCEEDED_MAX_SIZE,
        fmt::format(
            "Message size exceeded the max alowed BGP message length. "
            "allowed: {}, msgLen: {}",
            kMaxBgpMsgLen,
            msgLen));
  }

  // Write BGP Header at the front of msgHeader
  headCursor.push(kBgpMarker.data(), kBgpMarker.size());
  headCursor.writeBE<uint16_t>(msgLen);
  headCursor.write<uint8_t>(static_cast<uint8_t>(msgType));

  return msgLen;
}

/**
 * Utility method to serialize as many NLRIs that can be packed in the
 * supplied bufsize pointed by cursor of the buffer.
 * It understands afi/safi
 * it packs NLRI upto the bufsize
 *   - To save pre-calculation of the size, for every next NLRI, it checks
 *     if buffer has at least maximum possible NLRI size left. This means
 *     if last NLRI to be packed is less than max possible NLRI size then
 *     packing will done there leaving possible some space towards the end
 *
 * it packs NLRI until curPrefixCnt reaches totalPrefixCnt
 *
 * @params: cursor - current pointer in the buffer from where to start
 *                 serialization
 *          afi - Address Family of an IP Address
 *          safi - subsequent type of the Address Family
 *          riggedIPPrefixes - Vector of RiggedIPPrefix
 *          totalPrefixCnt - Total number of prefix count to reach max
 *                 from curPrefixCnt
 *          bufSize - Buffer size available to fit from remaining prefixes
 *                 starting from offset
 *          maxPrefixLen - Maximum possible length of a prefix (includes
 *                 size of the path if path-enabled)
 *          curPrefixCnt - gets updated as NLRI gets serialized
 *
 * @returns: length of buffer consumed (size of nlri)
 * @throws: BgpSerializerException on failure
 */
int BgpMessageSerializer::serializeNlri(
    RWPrivateCursor cursor,
    BgpUpdateAfi afi,
    BgpUpdateSafi safi,
    const std::vector<RiggedIPPrefix>& riggedIPPrefixes,
    size_t totalPrefixCnt,
    size_t bufSize,
    size_t maxPrefixLen,
    size_t& curPrefixCnt) {
  auto startCursor = cursor;
  uint32_t label = 0;
  uint8_t prefixLenBytes = 0;

  auto maxPerPrefixSpace = maxPrefixLen + sizeof(uint8_t);
  // Serialize NLRI information
  /*
   * Run loop until either buffer is full or packed all the
   * remaining prefixes
   *
   * Check if there is space for atleast max length possible for
   * a prefix, if not consider buffer is full
   */
  while ((((cursor - startCursor) + maxPerPrefixSpace) < bufSize) &&
         (curPrefixCnt < totalPrefixCnt)) {
    auto oneRiggedIPPrefix = riggedIPPrefixes[(curPrefixCnt)];
    auto pfix = network::toCIDRNetwork(*oneRiggedIPPrefix.prefix());
    const auto& labels = *oneRiggedIPPrefix.labels();
    if ((pfix.first.isV4() && afi != BgpUpdateAfi::AFI_IPv4) ||
        (pfix.first.isV6() && afi != BgpUpdateAfi::AFI_IPv6)) {
      throw BgpSerializerException(
          BgpSerializerExceptionCode::AFI_MISMATCH,
          fmt::format(
              "AFI value mismatch. AFI {}, Prefix {}",
              apache::thrift::util::enumNameSafe(afi),
              IPAddress::networkToString(pfix)));
    }

    // Verify the presence of label stack depending on SAFI
    if ((safi == BgpUpdateSafi::SAFI_LABELED_UNICAST && labels.empty()) ||
        (safi == BgpUpdateSafi::SAFI_UNICAST && !labels.empty())) {
      throw BgpSerializerException(
          BgpSerializerExceptionCode::INVALID_NLRI_LABEL_INFO,
          fmt::format(
              "Invalid label info: SAFI {} label stack size {}",
              apache::thrift::util::enumNameSafe(safi),
              labels.size()));
    }

    if (oneRiggedIPPrefix.pathId().has_value()) {
      cursor.writeBE<uint32_t>(uint32_t(oneRiggedIPPrefix.pathId().value()));
    }

    // Write prefix length in bits to buffer
    uint8_t prefixLenBits = pfix.second;
    if (safi == BgpUpdateSafi::SAFI_LABELED_UNICAST) {
      prefixLenBits += labels.size() * 24; // Each label is 3 bytes
    }
    cursor.write<uint8_t>(prefixLenBits);

    // Serialize label stack information
    if (safi == BgpUpdateSafi::SAFI_LABELED_UNICAST) {
      // Serialize label stack information
      for (int j = 0; j < labels.size(); j++) {
        label = (labels[j] << 4);
        if (j == labels.size() - 1) {
          label |= 1; // Add bottom of stack bit
        }
        cursor.write<uint8_t>(static_cast<uint8_t>((label >> 16) & 0xff));
        cursor.write<uint8_t>(static_cast<uint8_t>((label >> 8) & 0xff));
        cursor.write<uint8_t>(static_cast<uint8_t>(label & 0xff));
      }
    }

    // Calculate prefix length in bytes and serialize prefix
    prefixLenBytes = (pfix.second + 7) / 8;
    cursor.push(pfix.first.bytes(), prefixLenBytes);
    curPrefixCnt++;
  }

  return (cursor - startCursor);
}

int BgpMessageSerializer::serializePathAttrs(
    RWPrivateCursor pcursor,
    const BgpUpdate2& update,
    bool as4byte,
    bool v4,
    size_t* outNexthopOffsetInAttrs) {
  const auto& attrs = *update.attrs();
  auto startCursor = pcursor;
  const uint8_t asnLen = (as4byte ? 4 : 2);

  // Write Origin Attribute
  pcursor.write<uint8_t>(static_cast<uint8_t>(BGP_ATTR_FLAG_TRANSITIVE));
  pcursor.write<uint8_t>(static_cast<uint8_t>(BgpAttrCode::BGP_ATTR_ORIGIN));
  pcursor.write<uint8_t>(static_cast<uint8_t>(1));
  pcursor.write<uint8_t>(static_cast<uint8_t>(*attrs.origin()));

  // Write AS_PATH attribute
  pcursor.write<uint8_t>(
      static_cast<uint8_t>(BGP_ATTR_FLAG_TRANSITIVE | BGP_ATTR_FLAG_EXTENDED));
  pcursor.write<uint8_t>(static_cast<uint8_t>(BgpAttrCode::BGP_ATTR_AS_PATH));
  auto asPathCursor = pcursor + 2; // Leave 2 byte for as path length
  if (!attrs.asPath()->empty()) {
    for (const auto& segment : *attrs.asPath()) {
      // Verify that only one segment type is used.
      auto numUsedSegmentTypes = 0;
      if (!segment.asSequence()->empty()) {
        numUsedSegmentTypes++;
      }
      if (!segment.asSet()->empty()) {
        numUsedSegmentTypes++;
      }
      if (!segment.asConfedSequence()->empty()) {
        numUsedSegmentTypes++;
      }
      if (!segment.asConfedSet()->empty()) {
        numUsedSegmentTypes++;
      }
      if (numUsedSegmentTypes != 1) {
        throw BgpSerializerException(
            BgpSerializerExceptionCode::INVALID_ASPATH_INFO,
            fmt::format(
                "Invalid AsPathSegment asSet {} asSeq {} asConfedSeq {} "
                "asConfedSet {}",
                segment.asSet()->size(),
                segment.asSequence()->size(),
                segment.asConfedSequence()->size(),
                segment.asConfedSet()->size()));
      }

      if (!segment.asSet()->empty()) {
        asPathCursor.write<uint8_t>(static_cast<uint8_t>(1)); // AS_SET
        asPathCursor.write<uint8_t>(
            static_cast<uint8_t>(segment.asSet()->size()));
        for (const auto& asn : *segment.asSet()) {
          if (as4byte) {
            asPathCursor.writeBE<uint32_t>(static_cast<uint32_t>(asn));
          } else {
            asPathCursor.writeBE<uint16_t>(static_cast<uint16_t>(asn));
          }
        }
        continue;
      }

      if (!segment.asSequence()->empty()) {
        asPathCursor.write<uint8_t>(static_cast<uint8_t>(2)); // AS_SEQUENCE
        asPathCursor.write<uint8_t>(
            static_cast<uint8_t>(segment.asSequence()->size()));
        for (const auto& asn : *segment.asSequence()) {
          if (as4byte) {
            asPathCursor.writeBE<uint32_t>(static_cast<uint32_t>(asn));
          } else {
            asPathCursor.writeBE<uint16_t>(static_cast<uint16_t>(asn));
          }
        }
        continue;
      }

      if (!segment.asConfedSequence()->empty()) {
        asPathCursor.write<uint8_t>(
            static_cast<uint8_t>(3)); // AS_CONFED_SEQUENCE
        asPathCursor.write<uint8_t>(
            static_cast<uint8_t>(segment.asConfedSequence()->size()));
        for (const auto& asn : *segment.asConfedSequence()) {
          if (as4byte) {
            asPathCursor.writeBE<uint32_t>(static_cast<uint32_t>(asn));
          } else {
            asPathCursor.writeBE<uint16_t>(static_cast<uint16_t>(asn));
          }
        }
        continue;
      }

      asPathCursor.write<uint8_t>(static_cast<uint8_t>(4)); // AS_CONFED_SET
      asPathCursor.write<uint8_t>(
          static_cast<uint8_t>(segment.asConfedSet()->size()));
      for (const auto& asn : *segment.asConfedSet()) {
        if (as4byte) {
          asPathCursor.writeBE<uint32_t>(static_cast<uint32_t>(asn));
        } else {
          asPathCursor.writeBE<uint16_t>(static_cast<uint16_t>(asn));
        }
      }
    }
  }
  auto asPathLen = asPathCursor - pcursor - 2;
  if (asPathLen > 0xFFF) {
    throw BgpSerializerException(
        BgpSerializerExceptionCode::EXCEEDED_MAX_SIZE,
        "too large as path length");
  }
  pcursor.writeBE<uint16_t>(static_cast<uint16_t>(asPathLen)); // Len of path
                                                               // attrs
  pcursor = asPathCursor; // Advance to pcursor to end of AS_PATH attribute

  // Write NEXT_HOP attribute if information is not multi protocol
  if (v4) {
    pcursor.write<uint8_t>(static_cast<uint8_t>(BGP_ATTR_FLAG_TRANSITIVE));
    pcursor.write<uint8_t>(
        static_cast<uint8_t>(BgpAttrCode::BGP_ATTR_NEXT_HOP));
    pcursor.write<uint8_t>(static_cast<uint8_t>(4)); // Attribute Length
    // Track nexthop offset for zero-copy optimization (before writing)
    if (outNexthopOffsetInAttrs != nullptr) {
      *outNexthopOffsetInAttrs = pcursor - startCursor;
    }
    pcursor.write<uint32_t>(
        *(reinterpret_cast<uint32_t*>(const_cast<unsigned char*>(
            network::toIPAddress(*update.v4Nexthop()).bytes()))));
  }

  // Write MED attribute
  if (*attrs.isMedSet()) {
    pcursor.write<uint8_t>(static_cast<uint8_t>(BGP_ATTR_FLAG_OPTIONAL));
    pcursor.write<uint8_t>(static_cast<uint8_t>(BgpAttrCode::BGP_ATTR_MED));
    pcursor.write<uint8_t>(static_cast<uint8_t>(4)); // Length of MED attribute
    pcursor.writeBE<uint32_t>(static_cast<uint32_t>(*attrs.med()));
  }

  // Write LOCAL_PREF attribute
  if (attrs.localPref().has_value()) {
    pcursor.write<uint8_t>(static_cast<uint8_t>(BGP_ATTR_FLAG_TRANSITIVE));
    pcursor.write<uint8_t>(
        static_cast<uint8_t>(BgpAttrCode::BGP_ATTR_LOCAL_PREF));
    pcursor.write<uint8_t>(static_cast<uint8_t>(4)); // Length of LOCAL_PREF
                                                     // attribute
    pcursor.writeBE<uint32_t>(static_cast<uint32_t>(*attrs.localPref()));
  }

  // Write ATOMIC AGGREGATE attribute
  if (*attrs.atomicAggregate()) {
    pcursor.write<uint8_t>(static_cast<uint8_t>(BGP_ATTR_FLAG_TRANSITIVE));
    pcursor.write<uint8_t>(
        static_cast<uint8_t>(BgpAttrCode::BGP_ATTR_ATOMIC_AGGREGATE));
    pcursor.write<uint8_t>(static_cast<uint8_t>(0));
  }

  // Write AGGREGATOR attribute
  if (*attrs.aggregator()->asn() && !attrs.aggregator()->ip()->empty()) {
    pcursor.write<uint8_t>(static_cast<uint8_t>(
        BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANSITIVE));
    pcursor.write<uint8_t>(
        static_cast<uint8_t>(BgpAttrCode::BGP_ATTR_AGGREGATOR));
    pcursor.write<uint8_t>(static_cast<uint8_t>(4 + asnLen));
    if (as4byte) {
      pcursor.writeBE<uint32_t>(
          static_cast<uint32_t>(*attrs.aggregator()->asn()));
    } else {
      pcursor.writeBE<uint16_t>(
          static_cast<uint16_t>(*attrs.aggregator()->asn()));
    }
    // TODO: Replace string in BgpAggregator with folly::IPAddress
    auto aggIp = folly::IPAddress(*attrs.aggregator()->ip());
    pcursor.writeBE<uint32_t>(aggIp.asV4().toLongHBO());
  }

  // Write Communities attribute
  if (!attrs.communities()->empty()) {
    pcursor.write<uint8_t>(static_cast<uint8_t>(
        BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANSITIVE |
        BGP_ATTR_FLAG_EXTENDED));
    pcursor.write<uint8_t>(
        static_cast<uint8_t>(BgpAttrCode::BGP_ATTR_COMMUNITIES));
    pcursor.writeBE<uint16_t>(
        static_cast<uint16_t>(4 * attrs.communities()->size()));
    for (auto& community : *attrs.communities()) {
      pcursor.writeBE<uint16_t>(static_cast<uint16_t>(*community.asn()));
      pcursor.writeBE<uint16_t>(static_cast<uint16_t>(*community.value()));
    }
  }

  // Write ORIGINATOR_ID attribute
  if (*attrs.originatorId()) {
    pcursor.write<uint8_t>(static_cast<uint8_t>(BGP_ATTR_FLAG_OPTIONAL));
    pcursor.write<uint8_t>(
        static_cast<uint8_t>(BgpAttrCode::BGP_ATTR_ORIGINATOR_ID));
    pcursor.write<uint8_t>(static_cast<uint8_t>(4));
    pcursor.write<uint32_t>(static_cast<uint32_t>(*attrs.originatorId()));
  }

  // Write CLUSTER_LIST attribute
  if (!attrs.clusterList()->empty()) {
    pcursor.write<uint8_t>(
        static_cast<uint8_t>(BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_EXTENDED));
    pcursor.write<uint8_t>(
        static_cast<uint8_t>(BgpAttrCode::BGP_ATTR_CLUSTER_LIST));
    pcursor.writeBE<uint16_t>(
        static_cast<uint16_t>(4 * attrs.clusterList()->size()));
    for (auto& clusterId : *attrs.clusterList()) {
      pcursor.write<uint32_t>(static_cast<uint32_t>(clusterId));
    }
  }

  // Write Extended Communities if any
  if (!attrs.extCommunities()->empty()) {
    pcursor.write<uint8_t>(static_cast<uint8_t>(
        BGP_ATTR_FLAG_OPTIONAL | BGP_ATTR_FLAG_TRANSITIVE |
        BGP_ATTR_FLAG_EXTENDED));
    pcursor.write<uint8_t>(
        static_cast<uint8_t>(BgpAttrCode::BGP_ATTR_EXTENDED_COMMUNITIES));
    pcursor.writeBE<uint16_t>(
        static_cast<uint16_t>(8 * attrs.extCommunities()->size()));
    for (const auto& community : *attrs.extCommunities()) {
      pcursor.writeBE<uint32_t>(static_cast<uint32_t>(*community.firstWord()));
      pcursor.writeBE<uint32_t>(static_cast<uint32_t>(*community.secondWord()));
    }
  }

  return (pcursor - startCursor);
}

size_t BgpMessageSerializer::getMaxPrefixLen(
    const bool hasPathId,
    const bool v4) {
  static constexpr size_t kMaxV4PrefixLenWithPathId =
      kMaxV4prefixLen + kMaxPathIdLen;
  static constexpr size_t kMaxV6PrefixLenWithPathId =
      kMaxV6PrefixLen + kMaxPathIdLen;
  if (v4) {
    return hasPathId ? kMaxV4PrefixLenWithPathId : kMaxV4prefixLen;
  }
  return hasPathId ? kMaxV6PrefixLenWithPathId : kMaxV6PrefixLen;
}

} // namespace bgplib
} // namespace nettools
} // namespace facebook
