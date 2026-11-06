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
#include <optional>
#include <variant>

#include "common/network/AddressUtil.h"
#include "neteng/fboss/bgp/cpp/lib/BgpException.h"
#include "neteng/fboss/bgp/cpp/lib/BgpMessageParser.h"
#include "neteng/fboss/bgp/cpp/lib/detail/BgpMessageParserUtils.h"
#include "neteng/fboss/bgp/cpp/stats/StatsBase.h"

using folly::IOBuf;
using folly::io::Cursor;

namespace facebook {
namespace nettools {
namespace bgplib {

using namespace facebook::nettools::bgplib::detail;

using facebook::network::toBinaryAddress;
namespace thrift = facebook::network::thrift;

using folly::IPAddress;

/**
 * Peek for the BGP message header.  I.e., basic parse, with basic,
 * context-free length validation.
 *
 * The cursor must have at least kBgpMsgHeaderLen (19) bytes available to
 * read.
 *
 * Does not modify the caller's cursor.
 */
BgpMessageHeader BgpMessageParser2::parseBgpMsgHdr(
    Cursor cursor,
    std::optional<BgpMessageType> msgType) {
  size_t msglen = cursor.length();
  // Size of BgpMessage must be at least 19
  if (msglen < kBgpMsgHeaderLen) {
    auto errData = htons(msglen);
    throw BgpHeaderException(
        BgpNotifMsgHdrErrSubCode::BN_MH_BAD_MSG_LEN,
        std::string(reinterpret_cast<const char*>(&errData), sizeof(errData)),
        fmt::format("Invalid BGP message size: {}", msglen));
  }

  std::array<uint8_t, 16> marker{{0x0}};
  cursor.pull(marker.data(), 16);

  if (kBgpMarker != marker) {
    throw BgpHeaderException(
        BgpNotifMsgHdrErrSubCode::BN_MH_CONNECTION_NOT_SYNCHRONIZED,
        std::string(),
        "Synchronization error");
  }

  // Parse message header
  BgpMessageHeader hdr;
  hdr.length = cursor.readBE<uint16_t>();
  hdr.type = cursor.read<uint8_t>();

  // Basic header length check.
  if (hdr.length < kBgpMsgHeaderLen || hdr.length > kMaxBgpMsgLen) {
    auto errData = htons(hdr.length);
    throw BgpHeaderException(
        BgpNotifMsgHdrErrSubCode::BN_MH_BAD_MSG_LEN,
        std::string(reinterpret_cast<const char*>(&errData), sizeof(errData)),
        fmt::format("Invalid BGP header length: {}", hdr.length));
  }

  // Move ahead only if msg types are matching, when given
  if (msgType && hdr.type != *msgType) {
    throw BgpHeaderException(
        BgpNotifMsgHdrErrSubCode::BN_MH_BAD_MSG_TYPE,
        std::string(reinterpret_cast<const char*>(&hdr.type), sizeof(hdr.type)),
        fmt::format(
            "Unexpected message type: {}, expected: {}",
            (int)hdr.type,
            (int)*msgType));
  }
  return hdr;
}

/**
 * Parse and consume the header from the cursor.
 *
 * The header is consumed from the cursor.
 * The length in the header is validated against the bytes available from
 * the cursor and may be trusted after this function.
 */
BgpMessageHeader BgpMessageParser2::consumeBgpMsgHdr(
    Cursor& cursor,
    std::optional<BgpMessageType> msgType) {
  size_t readable = cursor.length();

  BgpMessageHeader hdr = BgpMessageParser2::parseBgpMsgHdr(cursor, msgType);
  cursor.skip(kBgpMsgHeaderLen);

  /*
   * Additionally, validate we have sufficient data buffered to parse
   * the length indicated in the BGP header.
   */
  if (hdr.length > readable) {
    auto errData = htons(hdr.length);
    /* RFC4271 6.1:
     * "All errors detected while processing the Message Header MUST be
     * indicated by sending the NOTIFICATION message with the Error Code
     * Message Header Error."
     */
    throw BgpHeaderException(
        BgpNotifMsgHdrErrSubCode::BN_MH_BAD_MSG_LEN,
        std::string(reinterpret_cast<const char*>(&errData), sizeof(errData)),
        fmt::format(
            "Invalid BGP header length: {} (have: {})", hdr.length, readable));
  }
  return hdr;
}

BgpOpenMsg BgpMessageParser2::parseBgpOpenMsgRaw(folly::IOBuf buf) {
  Cursor cursor(&buf);

  // Struct to hold bgp open message
  BgpOpenMsg msg;

  // Parse the header
  auto hdr = BgpMessageParser2::consumeBgpMsgHdr(cursor, BGP_MSG_TYPE_OPEN);

  XLOGF(
      DBG4,
      "parseBgpOpenMsgRaw: parsing full open message:\n{}",
      folly::hexDump(buf.data(), hdr.length));

  // Length of open message must be atleast 10 (apart from common header)
  if (hdr.length < kBgpMsgHeaderLen + 10) {
    // need to convert back to NBO
    auto errData = htons(hdr.length);
    throw BgpHeaderException(
        BgpNotifMsgHdrErrSubCode::BN_MH_BAD_MSG_LEN,
        std::string(reinterpret_cast<const char*>(&errData), sizeof(errData)),
        fmt::format(
            "Incomplete BgpOpenMessage. Minimum length of "
            "BgpOpenMessage must be"
            " 29. Got {} ({}).",
            hdr.length,
            buf.length()));
  }

  /*
   * Parse the OPEN message contents
   * Read BGP Version Number
   */
  msg.version() = cursor.read<uint8_t>();

  if (*msg.version() != kBgpVersion) {
    throw BgpOpenMsgException(
        BgpNotifOpenMsgErrSubCode::BN_OM_UNSUPPORTED_VERSION_NUMBER,
        std::string(
            reinterpret_cast<const char*>(&kBgpVersion), sizeof(kBgpVersion)),
        fmt::format(
            "Only BGP version-4 is supported. Got version-{}.",
            *msg.version()));
  }

  // Read 2-byte AS Number
  msg.asn() = cursor.readBE<uint16_t>();
  // Read hold time
  msg.holdTime() = cursor.readBE<uint16_t>();

  if (*msg.holdTime() != 0 && *msg.holdTime() < 3) {
    throw BgpOpenMsgException(
        BgpNotifOpenMsgErrSubCode::BN_OM_UNACCEPTABLE_HOLD_TIME,
        std::string(),
        fmt::format(
            "Hold time must be 0 or at least 3 seconds. Received {}s",
            *msg.holdTime()));
  }

  // Read BGP Identifier
  msg.bgpID() = cursor.readBE<uint32_t>();

  // Param Length
  uint8_t paramsLen = cursor.read<uint8_t>();

  // Length of open message must be atleast 10 (apart from common header)
  if (paramsLen > cursor.length()) {
    throw BgpOpenMsgException(
        BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC,
        std::string(),
        fmt::format(
            "Incomplete BgpOpenMessage. "
            "OptParam length {} but cursor has only {} left",
            paramsLen,
            cursor.length()));
  }

  // Parse OPEN message parameters
  auto paramBuf = IOBuf::wrapBuffer(cursor.data(), paramsLen);
  Cursor paramCursor(paramBuf.get());

  // Parse Open Message Params
  while (paramCursor.length()) {
    // Length of an optional capability parameter must be atleast 2 bytes
    if (paramCursor.length() < 2) {
      throw BgpOpenMsgException(
          BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC,
          std::string(),
          fmt::format(
              "Optional Capability Parameter, expected >= 2"
              ", cursor has only {} left",
              cursor.length()));
    }

    uint8_t pType = paramCursor.read<uint8_t>();
    uint8_t pLen = paramCursor.read<uint8_t>();
    if (pLen > paramCursor.length()) {
      throw BgpOpenMsgException(
          BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC,
          std::string(),
          fmt::format(
              "The length of parameter exceeds "
              "the remaining message length. "
              "Remaining message length: {}, "
              "parameter length: {}",
              paramCursor.length(),
              pLen));
    }

    // Parse each parameter
    auto subCursorBuf = IOBuf::wrapBuffer(paramCursor.data(), pLen);
    Cursor subCursor(subCursorBuf.get());
    try {
      paramCursor.skip(pLen);

      switch (pType) {
        case (uint8_t)BgpOpenMsgParam::BO_PARAM_CAPABILITIES:
          parseBgpCapabilities(subCursor, *msg.capabilities());
          break;
        default:
          throw BgpOpenMsgException(
              BgpNotifOpenMsgErrSubCode::BN_OM_UNSUPPORTED_OPTIONAL_PARAM,
              std::string(reinterpret_cast<const char*>(&pType), sizeof(pType)),
              fmt::format("Unknown open msg param type-{}", (uint32_t)pType));
      } // switch
    } catch (std::out_of_range const&) {
      throw BgpUpdateMsgException(
          BgpNotifUpdateMsgErrSubCode::BN_UM_MALFORMED_ATTRIBUTE_LIST,
          std::string(),
          fmt::format(
              "The length of parameter exceeds "
              "the remaining message length. "
              "Remaining message length: {}, "
              "parameter length: {}",
              subCursor.length(),
              pLen));
    }
  } // while cursor

  return msg;
}

BgpNotification BgpMessageParser2::parseBgpNotificationRaw(folly::IOBuf buf) {
  Cursor cursor(&buf);

  /*
   * Parse and validate common bgp message header
   * Cursor is advanced and stops at the start of the
   * actual BGP message
   */
  BgpMessageHeader hdr;

  // Parse the header
  hdr = BgpMessageParser2::consumeBgpMsgHdr(cursor, BGP_MSG_TYPE_NOTIFICATION);

  // Length of open message must be atleast 10 (apart from common header)
  if (hdr.length < kBgpMsgHeaderLen + 2) {
    auto errData = htons(hdr.length);
    throw BgpHeaderException(
        BgpNotifMsgHdrErrSubCode::BN_MH_BAD_MSG_LEN,
        std::string(reinterpret_cast<const char*>(&errData), sizeof(errData)),
        fmt::format(
            "Incomplete BgpNotification mesg. Minimum length of "
            "BgpNotification must be 21. Got {} ({}).",
            hdr.length,
            buf.length()));
  }

  return consumeBgpNotificationBody(cursor);
}

BgpNotification BgpMessageParser2::consumeBgpNotificationBody(Cursor& cursor) {
  BgpNotification notif;
  notif.errSubCode() = 0;
  notif.errSubCodeStr() = "NA";
  notif.data() = "";

  // Parse ErrorCode and ErrorSubCode
  uint8_t code = cursor.read<uint8_t>();
  uint8_t subCode = cursor.read<uint8_t>();

  switch (code) {
    case (int)BgpNotifErrCode::BN_MSG_HDR_ERR:
      if (subCode > 3) {
        throw BgpException(
            fmt::format(
                "Unknown BgpNotification message header subcode. "
                "code: {}, subcode: {}.",
                (uint32_t)code,
                (uint32_t)subCode));
      }
      if (subCode > 0) {
        notif.errSubCodeStr() = apache::thrift::util::enumNameSafe(
            static_cast<BgpNotifMsgHdrErrSubCode>(subCode));
      }
      break;
    case (int)BgpNotifErrCode::BN_OPEN_MSG_ERR:
      if (subCode > 7) {
        throw BgpException(
            fmt::format(
                "Unknown BgpNotification open msg error subcode. "
                "code: {}, subcode: {}.",
                (uint32_t)code,
                (uint32_t)subCode));
      }
      if (subCode > 0) {
        notif.errSubCodeStr() = apache::thrift::util::enumNameSafe(
            static_cast<BgpNotifOpenMsgErrSubCode>(subCode));
      }
      break;
    case (int)BgpNotifErrCode::BN_UPDATE_MSG_ERR:
      if (subCode > 11) {
        throw BgpException(
            fmt::format(
                "Unknown BgpNotification update msg error subcode. "
                "code: {}, subcode: {}.",
                (uint32_t)code,
                (uint32_t)subCode));
      }
      if (subCode > 0) {
        notif.errSubCodeStr() = apache::thrift::util::enumNameSafe(
            static_cast<BgpNotifUpdateMsgErrSubCode>(subCode));
      }
      break;
    case (int)BgpNotifErrCode::BN_CEASE:
      if (subCode >
          static_cast<uint8_t>(BgpNotifCeaseErrSubCode::BN_CEASE_HARD_RESET)) {
        throw BgpException(
            fmt::format(
                "Unknown BgpNotification cease msg error subcode. "
                "code: {}, subcode: {}.",
                (uint32_t)code,
                (uint32_t)subCode));
      }
      if (subCode > 0) {
        notif.errSubCodeStr() = apache::thrift::util::enumNameSafe(
            static_cast<BgpNotifCeaseErrSubCode>(subCode));
      }
      break;
    case (int)BgpNotifErrCode::BN_HOLD_TIMER_EXPIRED:
    case (int)BgpNotifErrCode::BN_FSM_ERROR:
      break;
    case (int)BgpNotifErrCode::BN_ROUTE_REFRESH_MSG_ERR:
      /*
       * Error handling for Route Refresh message per RFC 7313(Section 5)
       * Subcode 0 is the only valid subcode for Error handling
       */
      if (subCode != 0) {
        throw BgpException(
            fmt::format(
                "Unknown BgpNotification update msg error subcode. "
                "code: {}, subcode: {}.",
                (uint32_t)code,
                (uint32_t)subCode));
      }
      notif.errSubCodeStr() = apache::thrift::util::enumNameSafe(
          static_cast<BgpNotificationRouteRefreshErrSubCode>(subCode));
      break;
    default:
      throw BgpException(
          fmt::format(
              "Unknown BgpNotification error code. "
              "code: {}",
              (uint32_t)code));
  }

  notif.errCode() = static_cast<BgpNotifErrCode>(code);
  notif.errSubCode() = subCode;

  // Read binary notification-data into string if any
  if (cursor.length()) {
    notif.data() = cursor.readFixedString(cursor.length());
  }

  // at this point the cursor should have been exhausted
  DCHECK_EQ(0, cursor.length());

  return notif;
}

BgpKeepAlive BgpMessageParser2::parseBgpKeepAliveRaw(folly::IOBuf buf) {
  Cursor cursor(&buf);

  // Parse the header
  auto hdr =
      BgpMessageParser2::consumeBgpMsgHdr(cursor, BGP_MSG_TYPE_KEEPALIVE);

  if (hdr.length != kBgpMsgHeaderLen) {
    auto errData = htons(hdr.length);
    throw BgpHeaderException(
        BgpNotifMsgHdrErrSubCode::BN_MH_BAD_MSG_LEN,
        std::string(reinterpret_cast<const char*>(&errData), sizeof(errData)),
        fmt::format(
            "Unexpected length of KeepAlive msg. Got {}, expected 19",
            hdr.length));
  }

  return BgpKeepAlive{};
}

BgpRouteRefresh BgpMessageParser2::parseBgpRouteRefreshRaw(
    const folly::IOBuf& buf) {
  Cursor cursor(&buf);

  // Parse the header
  auto hdr =
      BgpMessageParser2::consumeBgpMsgHdr(cursor, BGP_MSG_TYPE_ROUTE_REFRESH);
  // Initialize the message structure
  BgpRouteRefresh msg;
  // Parse the AFI, SAFI and message subType
  auto afi = cursor.readBE<uint16_t>();
  auto msgSubType = cursor.read<uint8_t>();
  auto safi = cursor.read<uint8_t>();

  /*
   * Validate the message length (RFC 7313)
   * Length of Route Refresh message must be 4 (apart from common header) if
   * message subType is 1 or 2
   */
  if ((hdr.length != kBgpMsgHeaderLen + 4) &&
      (msgSubType == 1 || msgSubType == 2)) {
    auto errData = htons(hdr.length);
    throw BgpRouteRefreshMsgException(
        BgpNotificationRouteRefreshErrSubCode::BN_INVALID_MSG_LEN,
        std::string(reinterpret_cast<const char*>(&errData), sizeof(errData)),
        fmt::format(
            "Unexpected length of Route Refresh msg. Got {}, expected 4",
            hdr.length));
  }
  // Populate the message structure
  msg.afi() = static_cast<BgpUpdateAfi>(afi);
  msg.msgSubType() = static_cast<BgpRouteRefreshMessageSubtype>(msgSubType);
  msg.safi() = static_cast<BgpUpdateSafi>(safi);

  return msg;
}

/*
 * BgpMessageParser2 methods
 */

// static, public
std::variant<std::shared_ptr<const BgpUpdate2>, BgpEndOfRib>
BgpMessageParser2::parseBgpUpdateRaw(
    folly::IOBuf buf,
    const BgpCapabilities& capabilities) {
  Cursor updateMsgCursor(&buf);

  /*
   * Parse and validate common bgp message header
   * Cursor is advanced and stops at the start of the
   * actual BGP message
   */
  BgpMessageHeader hdr;

  // Parse the header
  hdr =
      BgpMessageParser2::consumeBgpMsgHdr(updateMsgCursor, BGP_MSG_TYPE_UPDATE);
  XLOGF(
      DBG4,
      "parseBgpUpdateRaw: parsing full update message:\n{}",
      folly::hexDump(buf.data(), hdr.length));
  /*
   * The last v4 NLRI field has an implied length. Fix the cursor to the end
   * of this message (there may be more messages in the buffer) to avoid
   * NLRI parser overrun into next message.
   */
  updateMsgCursor = Cursor(updateMsgCursor, hdr.length - kBgpMsgHeaderLen);

  // Size of Bgp Update must be at least 4
  if (hdr.length < kBgpMsgHeaderLen + 4) {
    auto errData = htons(hdr.length);
    throw BgpHeaderException(
        BgpNotifMsgHdrErrSubCode::BN_MH_BAD_MSG_LEN,
        std::string(reinterpret_cast<const char*>(&errData), sizeof(errData)),
        fmt::format(
            "Incomplete BgpUpdateMessage. Minimum length of BgpUpdate "
            "msg must be 23. Got {} ({})",
            hdr.length,
            buf.length()));
  }

  /*
   * State will be populated accordingly as we parse raw BgpMessage and at the
   * end it will be used to generate BgpUpdate messages
   */
  UpdateMsgParsingState state;

  state.parseV4Withdrawn(updateMsgCursor, capabilities);
  state.parsePathAttributes(updateMsgCursor, capabilities);

  // End Of Rib for IPv4-unicast (rfc4724)
  if (state.v4WithdrawnLen == 0 && state.paLen == 0) {
    // NLRI present in EoR
    if (updateMsgCursor.length()) {
      throw BgpUpdateMsgException(
          BgpNotifUpdateMsgErrSubCode::BN_UM_MALFORMED_ATTRIBUTE_LIST,
          std::string(),
          "End-of-RIB message with non-empty NLRI field.");
    }
    BgpEndOfRib eor;
    eor.isMpEor() = false;
    eor.afi() = BgpUpdateAfi::AFI_IPv4;
    eor.safi() = BgpUpdateSafi::SAFI_UNICAST;
    return eor;
  }

  state.parseV4Announced(updateMsgCursor, capabilities);
  state.doSanityChecks();

  // MP EoR detected (empty MP_UNREACH_NLRI)
  if (state.eor) {
    return *state.eor;
  }

  auto update = std::make_shared<BgpUpdate2>();
  update->attrs() = state.attrs;

  bool addPathCap = false;
  for (const auto& addPathAbi : *capabilities.addPathCapabilities()) {
    if (*addPathAbi.afi() == BgpUpdateAfi::AFI_IPv4 &&
        (*addPathAbi.sor() == BgpAddPathSendRec::RECEIVE ||
         *addPathAbi.sor() == BgpAddPathSendRec::BOTH)) {
      // add-path capability enabled
      addPathCap = true;
      // overwrite v4Announced2 and v4Withdrawn2 with path id.
      update->v4Announced2()->clear();
      update->v4Withdrawn2()->clear();
      for (const auto& bgpPrefix : state.v4Announced) {
        thrift::IPPrefix p;
        p.prefixAddress() = toBinaryAddress(bgpPrefix.prefix.first);
        p.prefixLength() = bgpPrefix.prefix.second;
        RiggedIPPrefix rigP;
        rigP.prefix() = p;
        if (bgpPrefix.pathId.has_value()) {
          rigP.pathId() = *bgpPrefix.pathId;
        }
        update->v4Announced2()->emplace_back(std::move(rigP));
      }
      for (const auto& bgpPrefix : state.v4Withdrawn) {
        thrift::IPPrefix p;
        p.prefixAddress() = toBinaryAddress(bgpPrefix.prefix.first);
        p.prefixLength() = bgpPrefix.prefix.second;
        RiggedIPPrefix rigP;
        rigP.prefix() = p;
        if (bgpPrefix.pathId.has_value()) {
          rigP.pathId() = *bgpPrefix.pathId;
        }
        update->v4Withdrawn2()->emplace_back(std::move(rigP));
      }
    }
  }

  // If no add-path capability then parse prefixes in classical way
  if (!addPathCap) {
    // Classical v4 withdrawn prefixes
    for (const auto& bgpPrefix : state.v4Withdrawn) {
      // XXX: We really need cpp2 types to use fragile c-tor
      thrift::IPPrefix p;
      p.prefixAddress() = toBinaryAddress(bgpPrefix.prefix.first);
      p.prefixLength() = bgpPrefix.prefix.second;
      RiggedIPPrefix rigP;
      rigP.prefix() = p;
      update->v4Withdrawn()->emplace_back(std::move(p));
      update->v4Withdrawn2()->emplace_back(std::move(rigP));
    }

    // Classical v4 announced prefixes
    for (const auto& bgpPrefix : state.v4Announced) {
      // XXX: We really need cpp2 types to use fragile c-tor
      thrift::IPPrefix p;
      p.prefixAddress() = toBinaryAddress(bgpPrefix.prefix.first);
      p.prefixLength() = bgpPrefix.prefix.second;
      RiggedIPPrefix rigP;
      rigP.prefix() = p;
      update->v4Announced()->emplace_back(std::move(p));
      update->v4Announced2()->emplace_back(std::move(rigP));
    }
  }

  update->v4Nexthop() = toBinaryAddress(state.v4Nexthop);

  // MP-withdrawn prefixes
  update->mpWithdrawn()->afi() = state.mpWithdrawnAfi;
  update->mpWithdrawn()->safi() = state.mpWithdrawnSafi;

  for (const auto& bgpPrefix : state.mpWithdrawn) {
    // XXX: We really need cpp2 types to use fragile c-tor
    RiggedIPPrefix rp;

    rp.prefix()->prefixAddress() = toBinaryAddress(bgpPrefix.prefix.first);
    rp.prefix()->prefixLength() = bgpPrefix.prefix.second;
    *rp.labels() = std::move(bgpPrefix.labels);
    if (bgpPrefix.pathId.has_value()) {
      rp.pathId() = *bgpPrefix.pathId;
    }

    update->mpWithdrawn()->prefixes()->emplace_back(std::move(rp));
  }

  // MP-announced prefixes
  update->mpAnnounced()->afi() = state.mpAnnouncedAfi;
  update->mpAnnounced()->safi() = state.mpAnnouncedSafi;
  update->mpAnnounced()->nexthop() = toBinaryAddress(state.mpNexthop);

  for (const auto& bgpPrefix : state.mpAnnounced) {
    // XXX: We really need cpp2 types to use fragile c-tor
    RiggedIPPrefix rp;

    rp.prefix()->prefixAddress() = toBinaryAddress(bgpPrefix.prefix.first);
    rp.prefix()->prefixLength() = bgpPrefix.prefix.second;
    *rp.labels() = std::move(bgpPrefix.labels);
    if (bgpPrefix.pathId.has_value()) {
      rp.pathId() = *bgpPrefix.pathId;
    }

    update->mpAnnounced()->prefixes()->emplace_back(std::move(rp));
  }

  return update;
}

void BgpMessageParser2::parseBgpMessage(
    BgpMessageParserCallbacks* bgpParseCb,
    folly::IOBuf buf,
    std::optional<const BgpCapabilities> capabilities) {
  Cursor cursor(&buf);

  try {
    /*
     * Parse and validate common bgp message header
     * Cursor is advanced and stops at the start of the
     * actual BGP message
     */
    BgpMessageHeader hdr;

    // Parse the header
    hdr = BgpMessageParser2::parseBgpMsgHdr(cursor);

    switch (hdr.type) {
      case BGP_MSG_TYPE_OPEN: {
        auto msg = BgpMessageParser2::parseBgpOpenMsgRaw(buf);
        bgpParseCb->rcvdBgpOpenMsg(std::move(msg));
      } break;
      case BGP_MSG_TYPE_UPDATE: {
        if (!capabilities) {
          throw BgpFsmException(
              fmt::format(
                  "No capabilities given to parse UPDATE message with."));
        }
        bgp::PeerStats::addUpdateBytesRecvToAvg(buf.length());
        auto msg = BgpMessageParser2::parseBgpUpdateRaw(buf, *capabilities);
        if (std::holds_alternative<std::shared_ptr<const BgpUpdate2>>(msg)) {
          auto msg2 =
              std::move(std::get<std::shared_ptr<const BgpUpdate2>>(msg));
          bgpParseCb->rcvdBgpUpdate(*msg2);
        } else {
          auto msg2 = std::get<BgpEndOfRib>(msg);
          bgpParseCb->rcvdBgpEndOfRib(msg2);
        }
      } break;
      case BGP_MSG_TYPE_NOTIFICATION: {
        auto msg = BgpMessageParser2::parseBgpNotificationRaw(buf);
        bgpParseCb->rcvdBgpNotification(std::move(msg));
      } break;
      case BGP_MSG_TYPE_KEEPALIVE: {
        auto msg = BgpMessageParser2::parseBgpKeepAliveRaw(buf);
        bgpParseCb->rcvdBgpKeepAlive();
      } break;
      case BGP_MSG_TYPE_ROUTE_REFRESH: {
        auto msg = BgpMessageParser2::parseBgpRouteRefreshRaw(buf);
        bgpParseCb->rcvdBgpRouteRefresh(msg);
      } break;
      default:
        throw BgpException(
            fmt::format(
                "Unsupported BGP Message type: {}\n{}\n\n",
                hdr.type,
                folly::hexDump(buf.data(), buf.length())));
    }
  } catch (const BgpOpenMsgException& ex) {
    bgpParseCb->handleBgpOpenMsgException(ex);
  } catch (const BgpUpdateMsgException& ex) {
    bgpParseCb->handleBgpUpdateMsgException(ex);
  } catch (const BgpFsmException& ex) {
    bgpParseCb->handleBgpFsmException(ex);
  } catch (const BgpHeaderException& ex) {
    bgpParseCb->handleBgpHeaderException(ex);
  } catch (const BgpException& ex) {
    bgpParseCb->handleBgpException(ex);
  } catch (const BgpRouteRefreshMsgException& ex) {
    bgpParseCb->handleBgpRouteRefreshMsgException(ex);
  }
}

void BgpMessageParser2::parseBgpMessage(
    BgpMessageParserCallbacks* bgpParseCb,
    const uint8_t* data,
    int len,
    std::optional<const BgpCapabilities> capabilities) {
  parseBgpMessage(
      bgpParseCb, folly::IOBuf::wrapBufferAsValue(data, len), capabilities);
}

} // namespace bgplib
} // namespace nettools
} // namespace facebook
