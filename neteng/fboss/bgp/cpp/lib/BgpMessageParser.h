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

#include <folly/io/Cursor.h>
#include <folly/io/IOBufQueue.h>
#include <optional>

#include "neteng/fboss/bgp/cpp/lib/BgpException.h"
#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"
#include "neteng/fboss/bgp/if/gen-cpp2/BgpStructs_types.h"

namespace facebook {
namespace nettools {
namespace bgplib {

using BgpUpdates = std::vector<std::shared_ptr<BgpUpdate>>;

/**
 * This code implements parsing of raw BGP-4 messages. It has support for all
 * BGP messages
 * 1. OPEN
 * 2. UPDATE        - Look below for features(rfcs) that are supported!
 * 3. KEEPALIVE
 * 4. NOTIFICATION
 *
 * BGP Protocol
 * 1. BGP-4: https://tools.ietf.org/html/rfc4271
 *
 * COMMUNITIES
 * 2. BGP Communities: https://tools.ietf.org/html/rfc1997
 *
 * ORIGINATOR_ID and CLUSTER_LIST
 * 3. BGP Route Reflection: https://tools.ietf.org/html/rfc4456
 *
 * MP_REACH_NLRI and MP_UNREACH_NLRI
 * 4. Multiprotocol Extensions BGP-4: https://tools.ietf.org/html/rfc4760
 *
 * EXTENDED_COMMUNITIES
 * 5. BGP Extended Communities Attribute: https://tools.ietf.org/html/rfc4360
 * 6. Four-octet AS Specific BGP Extended Community:
 *      https://tools.ietf.org/html/draft-rekhter-as4octet-ext-community-03
 *
 * Labelled Unicast (Each label is 3 octet)
 * 7. Carrying Label Information in BGP-4: http://tools.ietf.org/html/rfc3107
 * 8. MPLS Label Stack Encoding: http://tools.ietf.org/html/rfc3032
 *
 * Note:
 * This library supports only few bgp capabilities as listed below
 * 1. MP Extensions for afi: v4/v6 and safi: Unicast/LU
 * 2. 4byte ASN encoding
 */
class BgpMessageParser2 {
 public:
  /**
   * Callbacks to
   */
  class BgpMessageParserCallbacks {
   public:
    virtual ~BgpMessageParserCallbacks() = default;
    BgpMessageParserCallbacks() = default;
    BgpMessageParserCallbacks(const BgpMessageParserCallbacks&) = delete;
    BgpMessageParserCallbacks& operator=(const BgpMessageParserCallbacks&) =
        delete;
    BgpMessageParserCallbacks(BgpMessageParserCallbacks&&) = delete;
    BgpMessageParserCallbacks& operator=(BgpMessageParserCallbacks&&) = delete;

    virtual void rcvdBgpOpenMsg(BgpOpenMsg openMsg) = 0;
    virtual void rcvdBgpNotification(BgpNotification notif) = 0;
    virtual void rcvdBgpKeepAlive() = 0;
    virtual void rcvdBgpUpdate(BgpUpdate2) = 0;
    virtual void rcvdBgpEndOfRib(BgpEndOfRib) = 0;
    virtual void rcvdBgpRouteRefresh(BgpRouteRefresh) = 0;
    /**
     * Invoked whenever parser encounters an exception with appropriate
     * BgpException which can then be used to understand the issue.  (e.g.
     * BgpCapability not understood) and take appropriate action on it.
     */
    virtual void handleBgpException(const BgpException& e) = 0;
    virtual void handleBgpFsmException(const BgpFsmException& e) = 0;
    virtual void handleBgpHeaderException(const BgpHeaderException& e) = 0;
    virtual void handleBgpOpenMsgException(const BgpOpenMsgException& e) = 0;
    virtual void handleBgpUpdateMsgException(const BgpUpdateMsgException&) = 0;
    virtual void handleBgpRouteRefreshMsgException(
        const BgpRouteRefreshMsgException&) = 0;
  };

  /**
   * Parse BGP message and call the relevant callback functions.  Parsing
   * errors should result in the relevant exception callback being
   * called.
   *
   * This function may still throw an exception, due to bugs in the parser.
   * @throws std::exception
   */
  static void parseBgpMessage(
      BgpMessageParserCallbacks* bgpParseCb,
      const uint8_t* data,
      int len,
      std::optional<const BgpCapabilities> capabilities = std::nullopt);

  /**
   * Parse BGP message and call the relevant callback functions.
   * @throws BgpException, BgpHeaderException, BgpUpdateMsgException,
             or BgpOpenMsgException on failure.
   */
  static void parseBgpMessage(
      BgpMessageParserCallbacks*,
      folly::IOBuf,
      std::optional<const BgpCapabilities> capabilities = std::nullopt);

  /**
   * Parse raw BGP keepalive message
   * @returns: BgpKeepAlive
   * @throws: BgpHeaderException on failure
   */
  static BgpKeepAlive parseBgpKeepAliveRaw(folly::IOBuf buf);

  /**
   * Parse raw NOTIFICATION message. Caller will take care of buffer once this
   * function returns.
   * @returns: BgpNotification
   * @throws: BgpHeaderException or BgpException on failure
   */
  static BgpNotification parseBgpNotificationRaw(folly::IOBuf buf);

  /**
   * Parse and consume a NOTIFICATION body (error code, subcode and data)
   * from the cursor. Expects at least 2 bytes to be readable.
   * @returns: BgpNotification
   * @throws: BgpException on unknown error code or subcode
   */
  static BgpNotification consumeBgpNotificationBody(folly::io::Cursor& cursor);

  /**
   * Parse raw BGP OPEN message. It's up to the caller to construct
   * proper IOBuf and decide whether to copy or move it into our
   * value argument.
   *
   * @returns: BgpOpenMsg
   * @throws: BgpHeaderException or BgpOpenMsgException on failure
   */
  static BgpOpenMsg parseBgpOpenMsgRaw(folly::IOBuf buf);

  /**
   * Similar to BgpMessageParser::parseBgpUpdateRaw, but emits BgpUpdate2
   * objects, which group prefixes sharing same attributes in single thrift
   * object
   *
   * @returns: <std::shared_ptr<const BgpUpdate2>, BgpEndOfRib>
   * @throws: BgpHeaderException or BgpUpdateMsgException on failure
   */
  static std::variant<std::shared_ptr<const BgpUpdate2>, BgpEndOfRib>
  parseBgpUpdateRaw(folly::IOBuf buf, const BgpCapabilities& capabilities);

  /**
   * Parse and validate common BGP Header.
   * Does not modify the caller's cursor.
   * Expects at least kBgpMsgHeaderLen (19) bytes to be readable from the
   * cursor.
   * @returns: parse BGP message header
   * @throws: BgpHeaderException on failure
   */
  static BgpMessageHeader parseBgpMsgHdr(
      folly::io::Cursor cursor,
      std::optional<BgpMessageType> msgType = std::nullopt);

  /**
   * Parse and consume the common BGP Header from the buffer indicated by
   * the cursor.
   * Modifies the caller's cursor to reflect the parsed data.
   * Expects at least kBgpMsgHeaderLen (19) bytes to be readable from the
   * cursor.
   * @returns: parse BGP message header
   * @throws: BgpHeaderException on failure
   */
  static BgpMessageHeader consumeBgpMsgHdr(
      folly::io::Cursor& cursor,
      std::optional<BgpMessageType> msgType = std::nullopt);

  /**
   * Parse raw BGP ROUTE_REFRESH message
   * @returns: BgpRouteRefresh
   * @throws: BgpHeaderException or BgpException on failure
   */
  static BgpRouteRefresh parseBgpRouteRefreshRaw(const folly::IOBuf& buf);

  BgpMessageParser2() = delete;
};

} // namespace bgplib
} // namespace nettools
} // namespace facebook
