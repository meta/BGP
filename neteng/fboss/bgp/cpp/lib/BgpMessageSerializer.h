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

#include "neteng/fboss/bgp/cpp/lib/BgpStructs.h"
#include "neteng/fboss/bgp/if/gen-cpp2/BgpStructs_types.h"

namespace facebook {
namespace nettools {
namespace bgplib {

/**
 * This class provides APIs to serialize BGP Message Thrift objects into
 * raw bytes!
 * All method can return error if bgp message size exceeds kMaxBgpMsgLen
 */
class BgpMessageSerializer {
 public:
  /**
   * Serialize BgpOpenMsg into a folly::IOBuf buffer.
   * @returns: Serialized buffer with size of bgp msg
   * @throws: BgpSerializerException on failure
   */
  static std::unique_ptr<folly::IOBuf> serializeBgpOpenMsg(
      const BgpOpenMsg& update);

  /**
   * Serialize BgpNotification into a folly::IOBuf buffer.
   * @returns: Serialized buffer with size of bgp msg
   * @throws: BgpSerializerException on failure
   */
  static std::unique_ptr<folly::IOBuf> serializeBgpNotification(
      const BgpNotification& notif);

  /**
   * Serialize a NOTIFICATION body (error code, subcode and data) at the
   * cursor, advancing it past the written bytes.
   */
  static void serializeBgpNotificationBody(
      folly::io::RWPrivateCursor& cursor,
      const BgpNotification& notif);

  /**
   * Generates new KeepAlive message into a folly::IOBuf buffer.
   * @returns: Serialized buffer with size of bgp msg
   * @throws: BgpSerializerException on failure
   */
  static std::unique_ptr<folly::IOBuf> serializeBgpKeepAlive();

  /**
   * API method to generate raw BGP update message depicting EndOfRib for
   * given afi/safi pair.
   * @returns: Serialized buffer with size of bgp msg
   * @throws: BgpSerializerException on failure
   */
  static std::unique_ptr<folly::IOBuf> serializeBgpEndOfRib(
      BgpUpdateAfi afi,
      BgpUpdateSafi safi);

  /**
   * @brief: Serialize BgpRouteRefresh into a folly::IOBuf buffer
   */
  static std::unique_ptr<folly::IOBuf> serializeBgpRouteRefresh(
      const BgpRouteRefresh& routeRefresh);

  /**
   * Serialize BgpUpdate2 (thrift struct) into raw bgp update message
   * @params: outNexthopOffsets - Optional output parameter for vector of
   *          nexthop locations. Each tuple contains:
   *          <bufferIndex, offset, isV4> where isV4=true for v4 nexthop
   * @returns: Serialized buffer with size of bgp msg
   * @throws: BgpSerializerException on failure
   */
  static std::unique_ptr<folly::IOBuf> serializeBgpUpdate2(
      const BgpUpdate2& update,
      bool as4byte = false, // Use 4 byte ASN
      bool extNhEncoding = false, // support v4 nlri over v6 nexthop <rfc5549>
      std::vector<std::tuple<size_t, size_t, bool>>* outNexthopOffsets =
          nullptr); // Optional: vector of nexthop locations

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
  static int serializeNlri(
      folly::io::RWPrivateCursor cursor,
      BgpUpdateAfi afi,
      BgpUpdateSafi safi,
      const std::vector<RiggedIPPrefix>& riggedIPPrefixes,
      size_t totalPrefixCnt,
      size_t bufSize,
      size_t maxPrefixLen,
      size_t& curPrefixCnt);

  /**
   * Utility method that calculates max prefix length based on if it contains
   * pathId and is v4.
   */
  static size_t getMaxPrefixLen(const bool hasPathId, const bool v4);

 private:
  /**
   * Serialize BGPHeader into buffer pointed by headCursor. It uses tailCursor
   * to find the length of BgpMessage. The assumption here is that first
   * kBgpMsgHeaderLen bytes pointed by headCursor are empty where header will
   * be serialized.
   * It also does sanity checks for BGP message length
   * @returns: length of total buffer consumed (size of bgp msg)
   * @throws: BgpSerializerException on failure
   */
  static int serializeBgpHeader(
      folly::io::RWPrivateCursor headCursor,
      folly::io::RWPrivateCursor tailCursor,
      BgpMessageType msgType);

  /**
   * Utility method to serialize path attributes (except MP_REACH_NLRI
   * and MP_UNREACH_NLRI)into buffer pointed by cursor.
   * @params: v4, true if v4Annouced, false if mpAnnounced
   *          outNexthopOffsetInAttrs - Optional output parameter for offset of
   *                                    nexthop within the attributes buffer
   * @returns: length of buffer consumed (size of prefix)
   * @throws: BgpSerializerException on failure
   */
  static int serializePathAttrs(
      folly::io::RWPrivateCursor pcursor,
      const BgpUpdate2& update,
      bool as4byte,
      bool v4,
      size_t* outNexthopOffsetInAttrs = nullptr);
};
} // namespace bgplib
} // namespace nettools
} // namespace facebook
