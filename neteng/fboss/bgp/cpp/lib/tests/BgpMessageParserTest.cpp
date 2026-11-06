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

#include <folly/logging/xlog.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/ExceptionString.h>
#include <folly/IPAddress.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>

#include <folly/init/Init.h>
#include "common/network/AddressUtil.h"
#include "neteng/fboss/bgp/cpp/lib/BgpMessageParser.h"
#include "neteng/fboss/bgp/cpp/lib/BgpUtil.h"
#include "neteng/fboss/bgp/cpp/lib/detail/BgpMessageParserUtils.h"
#include "neteng/fboss/bgp/if/gen-cpp2/BgpStructs_types.h"

#include "BgpMessageParserTestData.h"

namespace facebook {
namespace nettools {
namespace bgplib {

using namespace facebook::nettools::bgplib::detail;

using folly::IOBuf;
using folly::io::Cursor;

BgpCapabilities capabilities;

void init() {
  *capabilities.mpExtV4Unicast() = true;
  *capabilities.mpExtV6Unicast() = true;
  *capabilities.mpExtV4LU() = true;
  *capabilities.mpExtV6LU() = true;
  *capabilities.as4byte() = true;
}

TEST(BgpMessageParser, parseNlriPrefixNormal) {
  /*
   * 6.4.3.2/32, first byte is prefix length;
   * we added extra byte at the end, this should be ignored
   */
  std::vector<uint8_t> v4Prefix = {0x20, 0x06, 0x05, 0x04, 0x03, 0x02};

  folly::CIDRNetwork prefix;

  auto nlriBuf = IOBuf::wrapBuffer(&v4Prefix[1], v4Prefix.size() - 1);
  Cursor nlriCursor(nlriBuf.get());

  prefix = parseNlriPrefix(nlriCursor, v4Prefix[0], BgpUpdateAfi::AFI_IPv4);

  EXPECT_EQ(folly::CIDRNetwork({folly::IPAddress("6.5.4.3"), 32}), prefix);

  // repeat for v6 prefix - once again, extra byte at the end

  std::vector<uint8_t> v6Prefix = {0x10, 0xfd, 0x00, 0x01};

  nlriBuf = IOBuf::wrapBuffer(&v6Prefix[1], v6Prefix.size() - 1);
  nlriCursor = Cursor(nlriBuf.get());

  prefix = parseNlriPrefix(nlriCursor, v6Prefix[0], BgpUpdateAfi::AFI_IPv6);

  EXPECT_EQ(folly::CIDRNetwork({folly::IPAddress("fd00::"), 16}), prefix);
}

TEST(BgpMessageParser, parseNlriPrefixBroken) {
  /*
   * 6.4.3.2/33, first byte is prefix length;
   * we added extra byte at the end, this should be ignored
   */
  std::vector<uint8_t> v4Prefix = {0x21, 0x06, 0x05, 0x04, 0x03, 0x02};

  folly::CIDRNetwork prefix;

  auto nlriBuf = IOBuf::wrapBuffer(&v4Prefix[1], v4Prefix.size() - 1);
  Cursor nlriCursor(nlriBuf.get());

  EXPECT_THROW(
      parseNlriPrefix(nlriCursor, v4Prefix[0], BgpUpdateAfi::AFI_IPv4),
      BgpUpdateMsgException);

  // repeat for v6 prefix - once again, extra bytes at the end

  std::vector<uint8_t> v6Prefix = {0xcc, 0xfd, 0x00, 0x01};

  nlriBuf = IOBuf::wrapBuffer(&v6Prefix[1], v6Prefix.size() - 1);
  nlriCursor = Cursor(nlriBuf.get());

  EXPECT_THROW(
      parseNlriPrefix(nlriCursor, v6Prefix[0], BgpUpdateAfi::AFI_IPv6),
      BgpUpdateMsgException);
}

/*
 * The following test validates that we actually apply the prefixLength
 * to mask the excessive bits that could be present in the network bytes
 */
TEST(BgpMessageParser, parseNlri) {
  /*
   * 3 prefixes, 10.20.{3,2,1}.0 of different lengths: 24, 23, 22
   * 10.20.1.0/22 is actually 10.20.0.0/22
   */
  std::vector<uint8_t> v4Prefixes = {
      0x18, 0x0a, 0x14, 0x03, 0x17, 0x0a, 0x14, 0x02, 0x16, 0x0a, 0x14, 0x01};

  std::vector<BgpPrefix> bgpPrefixes;

  auto nlriBuf = IOBuf::wrapBuffer(v4Prefixes.data(), v4Prefixes.size());
  Cursor nlriCursor(nlriBuf.get());

  bgpPrefixes = parseNlri(
      nlriCursor,
      BgpUpdateAfi::AFI_IPv4,
      BgpUpdateSafi::SAFI_UNICAST,
      false,
      capabilities);

  EXPECT_EQ(3, bgpPrefixes.size());
  for (auto i : std::vector<int>{3, 2, 1}) {
    auto addr = folly::IPAddress(fmt::format("10.20.{}.0", i)).mask(21 + i);
    EXPECT_EQ(folly::CIDRNetwork({addr, 21 + i}), bgpPrefixes[3 - i].prefix);
  }
}

TEST(BgpMessageParser, parseMpLabeledUnicast) {
  std::vector<uint8_t> labeledUnicast = {
      0x48,
      0x00,
      0x00,
      0x80,
      0x00,
      0x00,
      0x91,
      0xab,
      0xcd,
      0xef,
  };

  std::vector<BgpPrefix> bgpPrefixes;

  auto nlriBuf =
      IOBuf::wrapBuffer(labeledUnicast.data(), labeledUnicast.size());
  Cursor nlriCursor(nlriBuf.get());

  bgpPrefixes = parseMpNlri(
      nlriCursor,
      BgpUpdateAfi::AFI_IPv6,
      BgpUpdateSafi::SAFI_LABELED_UNICAST,
      capabilities,
      "",
      BgpAttrCode::BGP_ATTR_MP_REACH_NLRI);

  EXPECT_EQ(1, bgpPrefixes.size());
  auto addr = folly::IPAddress("abcd:ef00::");
  EXPECT_EQ(folly::CIDRNetwork({addr, 24}), bgpPrefixes[0].prefix);
  EXPECT_EQ((std::vector<int32_t>{8, 9}), bgpPrefixes[0].labels);
}

TEST(BgpMessageParser, parseMpLabeledUnicastWithdraw) {
  std::vector<uint8_t> labeledUnicast = {
      0x38, // len = 24 + prefix len 32
      0x00, // compatibility 0x000000
      0x00,
      0x00,
      0xc0, // prefix 192.0.2.1
      0x00,
      0x02,
      0x01,
  };

  std::vector<BgpPrefix> bgpPrefixes;

  auto nlriBuf =
      IOBuf::wrapBuffer(labeledUnicast.data(), labeledUnicast.size());
  Cursor nlriCursor(nlriBuf.get());

  bgpPrefixes = parseMpNlri(
      nlriCursor,
      BgpUpdateAfi::AFI_IPv4,
      BgpUpdateSafi::SAFI_LABELED_UNICAST,
      capabilities,
      "",
      BgpAttrCode::BGP_ATTR_MP_UNREACH_NLRI);

  EXPECT_EQ(1, bgpPrefixes.size());
  auto addr = folly::IPAddress("192.0.2.1");
  EXPECT_EQ(folly::CIDRNetwork({addr, 32}), bgpPrefixes[0].prefix);
  EXPECT_THAT(bgpPrefixes[0].labels, testing::IsEmpty());
}

TEST(BgpMessageParser, parseMpLabeledUnicastWithdrawV6) {
  std::vector<uint8_t> labeledUnicast = {
      0x98, // len = 24 + prefix len 128
      0x80, // compatibility 0x800000
      0x00, 0x00,
      0xab, // prefix abcd:ef00::
      0xcd, 0xef, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  std::vector<BgpPrefix> bgpPrefixes;

  auto nlriBuf =
      IOBuf::wrapBuffer(labeledUnicast.data(), labeledUnicast.size());
  Cursor nlriCursor(nlriBuf.get());

  bgpPrefixes = parseMpNlri(
      nlriCursor,
      BgpUpdateAfi::AFI_IPv6,
      BgpUpdateSafi::SAFI_LABELED_UNICAST,
      capabilities,
      "",
      BgpAttrCode::BGP_ATTR_MP_UNREACH_NLRI);

  EXPECT_EQ(1, bgpPrefixes.size());
  auto addr = folly::IPAddress("abcd:ef00::");
  EXPECT_EQ(folly::CIDRNetwork({addr, 128}), bgpPrefixes[0].prefix);
  EXPECT_THAT(bgpPrefixes[0].labels, testing::IsEmpty());
}

TEST(BgpMessageParser, parseMpLabeledUnicastWithWrongPrefixLength) {
  std::vector<uint8_t> labeledUnicast = {
      0x99, // len = 24 + prefix len 128 + error digit 1
      0x80, // compatibility 0x800000
      0x00, 0x00,
      0xab, // prefix abcd:ef00::
      0xcd, 0xef, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  auto nlriBuf =
      IOBuf::wrapBuffer(labeledUnicast.data(), labeledUnicast.size());
  Cursor nlriCursor(nlriBuf.get());

  EXPECT_THROW(
      parseMpNlri(
          nlriCursor,
          BgpUpdateAfi::AFI_IPv6,
          BgpUpdateSafi::SAFI_LABELED_UNICAST,
          capabilities,
          "",
          BgpAttrCode::BGP_ATTR_MP_UNREACH_NLRI),
      BgpUpdateMsgException);
}

/*
 * Bgp message header tests
 */

class BgpHeaderFixture : public ::testing::Test {
 public:
  std::vector<uint8_t> msg = {
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x13, // Length of BGP PDU
      0x01, // Bgp message type (OPEN)
  };
};

TEST_F(BgpHeaderFixture, BgpHeaderNormal) {
  IOBuf iob(IOBuf::WRAP_BUFFER, msg.data(), msg.size());
  Cursor cursor(&iob);

  auto hdr = BgpMessageParser2::parseBgpMsgHdr(
      cursor, BgpMessageType::BGP_MSG_TYPE_OPEN);
  EXPECT_EQ(0x13, hdr.length);
  EXPECT_EQ(BgpMessageType::BGP_MSG_TYPE_OPEN, hdr.type);
}

TEST_F(BgpHeaderFixture, BgpHeaderWrongType) {
  IOBuf iob(IOBuf::WRAP_BUFFER, msg.data(), msg.size());
  Cursor cursor(&iob);

  // the message has OPEN, but we expect NOTIFCATION instead
  EXPECT_NO_THROW({
    try {
      BgpMessageParser2::parseBgpMsgHdr(
          cursor, BgpMessageType::BGP_MSG_TYPE_NOTIFICATION);
      ADD_FAILURE();
    } catch (BgpHeaderException const& err) {
      EXPECT_EQ(BgpNotifMsgHdrErrSubCode::BN_MH_BAD_MSG_TYPE, err.getSubCode());
      EXPECT_EQ(
          std::string(reinterpret_cast<const char*>(&msg[18]), 1),
          err.getData());
    }
  });
}

TEST_F(BgpHeaderFixture, BgpHeaderWrongLength) {
  msg[17] = 0x10;
  IOBuf iob(IOBuf::WRAP_BUFFER, msg.data(), msg.size());
  Cursor cursor(&iob);

  EXPECT_NO_THROW({
    try {
      BgpMessageParser2::parseBgpMsgHdr(
          cursor, BgpMessageType::BGP_MSG_TYPE_OPEN);
      ADD_FAILURE();
    } catch (BgpHeaderException const& err) {
      EXPECT_EQ(BgpNotifMsgHdrErrSubCode::BN_MH_BAD_MSG_LEN, err.getSubCode());
      EXPECT_EQ(
          std::string(reinterpret_cast<const char*>(&msg[16]), 2),
          err.getData());
    }
  });
}

/*
 * KEEPALIVE message test
 */

class BgpKeepAliveFixture : public ::testing::Test {
 public:
  std::vector<uint8_t> msg = {
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x13, // Length of BGP PDU
      0x04, // Bgp message type (KEEPALIVE)
  };
};

TEST_F(BgpKeepAliveFixture, BgpKeepAlive) {
  BgpMessageParser2::parseBgpKeepAliveRaw(
      folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()));
}

TEST_F(BgpKeepAliveFixture, BgpKeepAliveWrongLength) {
  msg[17] = 0x10;

  EXPECT_NO_THROW({
    try {
      BgpMessageParser2::parseBgpKeepAliveRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()));
      ADD_FAILURE();
    } catch (BgpHeaderException const& err) {
      EXPECT_EQ(
          std::string(reinterpret_cast<const char*>(&msg[16]), 2),
          err.getData());
    }
  });
}

/*
 * OPEN message tests
 */

class BgpOpenMessageFixture : public ::testing::Test {
 public:
  std::vector<uint8_t> msg{
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0x00,
      0x3d, // Length of BGP PDU
      0x01, // Bgp message type (OPEN)
      0x04, // BGP Version
      0x80,
      0xa6, // ASN
      0x00,
      0x18, // Hold Time (24 seconds)
      0x01,
      0x02,
      0x03,
      0x04, // BGP ID
      0x20, // Optional Param Length
      // Params
      0x02, // Capabilities
      0x1e, // Length
      // Bgp Capabilities
      0x01,
      0x04, // MP Ext, Length-4
      0x00,
      0x01,
      0x00,
      0x01, // V4 + Unicast
      0x01,
      0x04, // MP Ext, Length-4
      0x00,
      0x02,
      0x00,
      0x04, // V6 + Labelled Unicast
      0x41,
      0x04, // 4 byte ASN
      0x01,
      0x02,
      0x03,
      0x04, // ASN value
      0x40,
      0x0a, // Graceful Restart, Length-10
      0x81,
      0x01, // state = true, time = 257
      0x00,
      0x01,
      0x01,
      0x80, // v4 + Unicast
      0x00,
      0x02,
      0x04,
      0x00, // v6 + Labeled Unicast
  };

  std::vector<uint8_t> msgWithAddPath{
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    // 0x00, 0x29,   // Length of BGP PDU
    0x00, 0x47,   // Length of BGP PDU
    0x01,         // Bgp message type (OPEN)
      0x04,       // BGP Version
      0x80, 0xa6, // ASN
      0x00, 0x18, // Hold Time (24 seconds)
      0x01, 0x02, 0x03, 0x04, // BGP ID
      0x2a, // Optional Param Length
      // Params
      0x02, // Capabilities
      0x28, // Length
        // Bgp Capabilities
        0x01, 0x04, // MP Ext, Length-4
          0x00, 0x01, 0x00, 0x01, // V4 + Unicast
        0x01, 0x04, // MP Ext, Length-4
          0x00, 0x02, 0x00, 0x04, // V6 + Labelled Unicast
        0x41, 0x04, // 4 byte ASN
          0x01, 0x02, 0x03, 0x04, // ASN value
        0x40, 0x0a, // Graceful Restart, Length-10
          0x81, 0x01, // state = true, time = 257
          0x00, 0x01, 0x01, 0x80, // v4 + Unicast
          0x00, 0x02, 0x04, 0x00,  // v6 + Unicast
        0x45, 0x08, // Add Path, Length-8
          0x00, 0x01, 0x01, 0x01, // v4 + Unicast + RECEIVE
          0x00, 0x02, 0x04, 0x02,  // v6 + LABELED_UNICAST + SEND
      // clang-format on
  };

  std::vector<uint8_t> msgWithExtNHEncoding{
      // clang-format off
    // Marker
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0x00, 0x3f, // Length
  0x01, // BGP message type (OPEN)
  0x04, // BGP Version
  0x80, 0xa6, // ASN
  0x00, 0x18, // Hold Time (24 seconds)
  0x01, 0x02, 0x03, 0x04, // BGP ID
  0x22, // Opt. Parm. Len
  // Parms
  0x02, // Parm. Type of capabilities
  0x20, // Parm. Length
  // Parm value
    0x05, // Capability Code of Extended Next Hop Encoding Capability
    0x1e, // Capa. Length
    // Capa. Value
    0x00, 0x01, 0x00, 0x01, 0x00, 0x02, // <1,1,2> valid
    0x00, 0x01, 0x00, 0x04, 0x00, 0x02, // <1,4,2> valid
    0x00, 0x01, 0x00, 0x05, 0x00, 0x02, // <1,5,2> invalid for nlri safi
    0x00, 0x02, 0x00, 0x01, 0x00, 0x02, // <2,1,2> invalid for nlri afi
    0x00, 0x01, 0x00, 0x01, 0x00, 0x01, // <1,1,1> invalid for nh afi
      // clang-format on
  };

  std::vector<uint8_t> msgWithExtNHEncodingMalformed{
      // clang-format off
    // Marker
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0x00, 0x2e, // Length
  0x01, // BGP message type (OPEN)
  0x04, // BGP Version
  0x80, 0xa6, // ASN
  0x00, 0x18, // Hold Time (24 seconds)
  0x01, 0x02, 0x03, 0x04, // BGP ID
  0x11, // Opt. Parm. Len
  // Parms
  0x02, // Parm. Type of capabilities
  0x0f, // Parm. Length
  // Parm value
    0x05, // Capability Code of Extended Next Hop Encoding Capability
    0x0d, // Capa. Length
    // Capa. Value
    0x00, 0x01, 0x00, 0x01, 0x00, 0x02, // <1,1,2> valid
    0x00, 0x01, 0x00, 0x04, 0x00, 0x02, // <1,4,2> valid
    0x00, // extra byte that is malformed as per RFC 5549
      // clang-format on
  };

  std::vector<uint8_t> msgWithEnhancedRouteRefresh{
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x49,   // Length of BGP PDU
    0x01,         // Bgp message type (OPEN)
      0x04,       // BGP Version
      0x80, 0xa6, // ASN
      0x00, 0x18, // Hold Time (24 seconds)
      0x01, 0x02, 0x03, 0x04, // BGP ID
      0x2c, // Optional Param Length
      // Params
      0x02, // Capabilities
      0x2a, // Length
        // Bgp Capabilities
        0x01, 0x04, // MP Ext, Length-4
          0x00, 0x01, 0x00, 0x01, // V4 + Unicast
        0x01, 0x04, // MP Ext, Length-4
          0x00, 0x02, 0x00, 0x04, // V6 + Labelled Unicast
        0x41, 0x04, // 4 byte ASN
          0x01, 0x02, 0x03, 0x04, // ASN value
        0x40, 0x0a, // Graceful Restart, Length-10
          0x81, 0x01, // state = true, time = 257
          0x00, 0x01, 0x01, 0x80, // v4 + Unicast
          0x00, 0x02, 0x04, 0x00,  // v6 + Unicast
        0x45, 0x08, // Add Path, Length-8
          0x00, 0x01, 0x01, 0x01, // v4 + Unicast + RECEIVE
          0x00, 0x02, 0x04, 0x02,  // v6 + LABELED_UNICAST + SEND
        0x46, 0x00, // Enhanced Route Refresh, Length-0
      // clang-format on
  };

  std::vector<uint8_t> msgWithRouteRefresh{
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x49,   // Length of BGP PDU
    0x01,         // Bgp message type (OPEN)
      0x04,       // BGP Version
      0x80, 0xa6, // ASN
      0x00, 0x18, // Hold Time (24 seconds)
      0x01, 0x02, 0x03, 0x04, // BGP ID
      0x2c, // Optional Param Length
      // Params
      0x02, // Capabilities
      0x2a, // Length
        // Bgp Capabilities
        0x01, 0x04, // MP Ext, Length-4
          0x00, 0x01, 0x00, 0x01, // V4 + Unicast
        0x01, 0x04, // MP Ext, Length-4
          0x00, 0x02, 0x00, 0x04, // V6 + Labelled Unicast
        0x41, 0x04, // 4 byte ASN
          0x01, 0x02, 0x03, 0x04, // ASN value
        0x40, 0x0a, // Graceful Restart, Length-10
          0x81, 0x01, // state = true, time = 257
          0x00, 0x01, 0x01, 0x80, // v4 + Unicast
          0x00, 0x02, 0x04, 0x00,  // v6 + Unicast
        0x45, 0x08, // Add Path, Length-8
          0x00, 0x01, 0x01, 0x01, // v4 + Unicast + RECEIVE
          0x00, 0x02, 0x04, 0x02,  // v6 + LABELED_UNICAST + SEND
        0x02, 0x00, // Route Refresh, Length-0
      // clang-format on
  };
};

/*
 * Correct OPEN message
 */
TEST_F(BgpOpenMessageFixture, BgpOpenMessageBackwardCompatible) {
  auto openMsg = BgpMessageParser2::parseBgpOpenMsgRaw(
      folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()));

  EXPECT_EQ(4, *openMsg.version());
  EXPECT_EQ(0x80a6, *openMsg.asn());
  EXPECT_EQ(24, *openMsg.holdTime());
  EXPECT_EQ(0x01020304, *openMsg.bgpID());

  auto& capa = *openMsg.capabilities();

  EXPECT_TRUE(*capa.mpExtV4Unicast());
  EXPECT_FALSE(*capa.mpExtV6Unicast());
  EXPECT_FALSE(*capa.mpExtV4LU());
  EXPECT_TRUE(*capa.mpExtV6LU());
  EXPECT_TRUE(*capa.as4byte());
  EXPECT_EQ(0x01020304, *capa.asn());

  EXPECT_TRUE(*capa.gracefulRestart());
  EXPECT_TRUE(*capa.isRestarting());
  EXPECT_FALSE(*capa.grNotification());
  EXPECT_EQ(257, *capa.restartTime());

  auto& grCapa = *capa.grCapabilities();

  ASSERT_EQ(2, grCapa.size());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, *grCapa[0].afi());
  EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *grCapa[0].safi());
  EXPECT_TRUE(*grCapa[0].forwardingState());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *grCapa[1].afi());
  EXPECT_EQ(BgpUpdateSafi::SAFI_LABELED_UNICAST, *grCapa[1].safi());
  EXPECT_FALSE(*grCapa[1].forwardingState());
}

// RFC 8538 GR Notification (N) bit
TEST(BgpMessageParser, BgpOpenMessageGrNotificationBit) {
  std::vector<uint8_t> msg = {
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x23,   // Length of BGP PDU
    0x01,         // Bgp message type (OPEN)
      0x04,       // BGP Version
      0x80, 0xa6, // ASN
      0x00, 0x18, // Hold Time (24 seconds)
      0x01, 0x02, 0x03, 0x04, // BGP ID
      0x06, // Optional Param Length
      // Params
      0x02, // Capabilities
      0x04, // Length
        // Bgp Capabilities
        0x40, 0x02, // Graceful Restart, Length-2
          0x41, 0x01, // R = 0, N = 1, time = 257
      // clang-format on
  };
  auto openMsg = BgpMessageParser2::parseBgpOpenMsgRaw(
      folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()));

  auto& capa = *openMsg.capabilities();
  EXPECT_TRUE(*capa.gracefulRestart());
  EXPECT_FALSE(*capa.isRestarting());
  EXPECT_TRUE(*capa.grNotification());
  EXPECT_EQ(257, *capa.restartTime());
  EXPECT_EQ(0, capa.grCapabilities()->size());
}

TEST_F(BgpOpenMessageFixture, BgpOpenMessageWithAddPath) {
  auto openMsg = BgpMessageParser2::parseBgpOpenMsgRaw(
      folly::IOBuf::wrapBufferAsValue(
          msgWithAddPath.data(), msgWithAddPath.size()));

  EXPECT_EQ(4, *openMsg.version());
  EXPECT_EQ(0x80a6, *openMsg.asn());
  EXPECT_EQ(24, *openMsg.holdTime());
  EXPECT_EQ(0x01020304, *openMsg.bgpID());

  auto& capa = *openMsg.capabilities();

  EXPECT_TRUE(*capa.mpExtV4Unicast());
  EXPECT_FALSE(*capa.mpExtV6Unicast());
  EXPECT_FALSE(*capa.mpExtV4LU());
  EXPECT_TRUE(*capa.mpExtV6LU());
  EXPECT_TRUE(*capa.as4byte());
  EXPECT_EQ(0x01020304, *capa.asn());

  EXPECT_TRUE(*capa.gracefulRestart());
  EXPECT_TRUE(*capa.isRestarting());
  EXPECT_EQ(257, *capa.restartTime());

  auto& grCapa = *capa.grCapabilities();

  ASSERT_EQ(2, grCapa.size());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, *grCapa[0].afi());
  EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *grCapa[0].safi());
  EXPECT_TRUE(*grCapa[0].forwardingState());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *grCapa[1].afi());
  EXPECT_EQ(BgpUpdateSafi::SAFI_LABELED_UNICAST, *grCapa[1].safi());
  EXPECT_FALSE(*grCapa[1].forwardingState());

  auto& addPathCapa = *capa.addPathCapabilities();
  ASSERT_EQ(2, addPathCapa.size());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, *addPathCapa[0].afi());
  EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *addPathCapa[0].safi());
  EXPECT_EQ(*addPathCapa[0].sor(), BgpAddPathSendRec::RECEIVE);
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *addPathCapa[1].afi());
  EXPECT_EQ(BgpUpdateSafi::SAFI_LABELED_UNICAST, *addPathCapa[1].safi());
  EXPECT_EQ(*addPathCapa[1].sor(), BgpAddPathSendRec::SEND);

  // change the add path capability length to be invalid.
  try {
    // change the add path length to 7
    auto msgWithAddPathWithWrongLength = msgWithAddPath;
    msgWithAddPathWithWrongLength[msgWithAddPathWithWrongLength.size() - 9] =
        0x07;
    auto openMsgWrong = BgpMessageParser2::parseBgpOpenMsgRaw(
        folly::IOBuf::wrapBufferAsValue(
            msgWithAddPathWithWrongLength.data(),
            msgWithAddPathWithWrongLength.size()));
  } catch (BgpOpenMsgException const& err) {
    EXPECT_EQ(err.getSubCode(), BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC);
  }

  /*
   * test if the msg has non-valid Add path capability. In such case,
   * we will ignore this capability.
   */
  auto msgWithAddPathWithInvalidCapability = msgWithAddPath;
  msgWithAddPathWithInvalidCapability
      [msgWithAddPathWithInvalidCapability.size() - 1] = 0x05;
  auto openMsgInvalidMsg = BgpMessageParser2::parseBgpOpenMsgRaw(
      folly::IOBuf::wrapBufferAsValue(
          msgWithAddPathWithInvalidCapability.data(),
          msgWithAddPathWithInvalidCapability.size()));
  addPathCapa = *openMsgInvalidMsg.capabilities()->addPathCapabilities();
  ASSERT_EQ(1, addPathCapa.size());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, *addPathCapa[0].afi());
  EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *addPathCapa[0].safi());
  EXPECT_EQ(*addPathCapa[0].sor(), BgpAddPathSendRec::RECEIVE);

  /*
   * change the code to be wrong. in such case, no add path capability should
   * be observed.
   */
  auto msgWithAddPathWithWrongCode = msgWithAddPath;
  msgWithAddPathWithWrongCode[msgWithAddPathWithWrongCode.size() - 10] = 0xff;
  auto openMsgInvalidCode = BgpMessageParser2::parseBgpOpenMsgRaw(
      folly::IOBuf::wrapBufferAsValue(
          msgWithAddPathWithWrongCode.data(),
          msgWithAddPathWithWrongCode.size()));
  addPathCapa = *openMsgInvalidCode.capabilities()->addPathCapabilities();
  ASSERT_EQ(0, addPathCapa.size());
}

TEST_F(BgpOpenMessageFixture, BgpOpenMessageWithExtNHEncoding) {
  auto openMsg = BgpMessageParser2::parseBgpOpenMsgRaw(
      folly::IOBuf::wrapBufferAsValue(
          msgWithExtNHEncoding.data(), msgWithExtNHEncoding.size()));

  // OPEN MSG common header
  EXPECT_EQ(4, *openMsg.version());
  EXPECT_EQ(0x80a6, *openMsg.asn());
  EXPECT_EQ(24, *openMsg.holdTime());
  EXPECT_EQ(0x01020304, *openMsg.bgpID());

  // OPEN MSG Opt. Parm. of capabilities
  auto& capa = *openMsg.capabilities();

  EXPECT_FALSE(*capa.mpExtV4Unicast());
  EXPECT_FALSE(*capa.mpExtV6Unicast());
  EXPECT_FALSE(*capa.mpExtV4LU());
  EXPECT_FALSE(*capa.mpExtV6LU());
  EXPECT_FALSE(*capa.as4byte());
  EXPECT_FALSE(*capa.gracefulRestart());
  EXPECT_FALSE(*capa.isRestarting());
  EXPECT_EQ(0, capa.grCapabilities()->size());

  // Extended Next Hop Encoding capability
  auto& extNHEncodingCapa = *capa.extNHEncodingCapabilities();
  EXPECT_EQ(2, extNHEncodingCapa.size());
  auto& tup1 = extNHEncodingCapa[0];
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, *tup1.nlriAfi());
  EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *tup1.nlriSafi());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *tup1.nhAfi());
  auto& tup2 = extNHEncodingCapa[1];
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, *tup2.nlriAfi());
  EXPECT_EQ(BgpUpdateSafi::SAFI_LABELED_UNICAST, *tup2.nlriSafi());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *tup2.nhAfi());
}

TEST_F(BgpOpenMessageFixture, BgpOpenMessageWithExtNHEncodingMalformed) {
  /*
   * In the Malformed msg, we have the length of Extended Next Hop Encoding
   * capability of 13, that is not a multiple of 6 (2 octets for each nlriAfi,
   * nlriSafi, nhAfi). We would throw exception upon this case.
   */
  EXPECT_THROW(
      BgpMessageParser2::parseBgpOpenMsgRaw(
          folly::IOBuf::wrapBufferAsValue(
              msgWithExtNHEncodingMalformed.data(),
              msgWithExtNHEncodingMalformed.size())),
      BgpOpenMsgException);
}

/**
 * Parse a BGP OPEN message with Enhanced Route Refresh capability.
 * Ensure all the capabilities are parsed correctly.
 */
TEST_F(BgpOpenMessageFixture, BgpOpenMessageWithEnhancedRouteRefresh) {
  auto openMsg = BgpMessageParser2::parseBgpOpenMsgRaw(
      folly::IOBuf::wrapBufferAsValue(
          msgWithEnhancedRouteRefresh.data(),
          msgWithEnhancedRouteRefresh.size()));

  EXPECT_EQ(4, *openMsg.version());
  EXPECT_EQ(0x80a6, *openMsg.asn());
  EXPECT_EQ(24, *openMsg.holdTime());
  EXPECT_EQ(0x01020304, *openMsg.bgpID());

  auto& capability = *openMsg.capabilities();

  EXPECT_TRUE(*capability.mpExtV4Unicast());
  EXPECT_FALSE(*capability.mpExtV6Unicast());
  EXPECT_FALSE(*capability.mpExtV4LU());
  EXPECT_TRUE(*capability.mpExtV6LU());
  EXPECT_TRUE(*capability.as4byte());
  EXPECT_EQ(0x01020304, *capability.asn());

  EXPECT_TRUE(*capability.gracefulRestart());
  EXPECT_TRUE(*capability.isRestarting());
  EXPECT_EQ(257, *capability.restartTime());

  auto& grCapability = *capability.grCapabilities();

  ASSERT_EQ(2, grCapability.size());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, *grCapability[0].afi());
  EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *grCapability[0].safi());
  EXPECT_TRUE(*grCapability[0].forwardingState());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *grCapability[1].afi());
  EXPECT_EQ(BgpUpdateSafi::SAFI_LABELED_UNICAST, *grCapability[1].safi());
  EXPECT_FALSE(*grCapability[1].forwardingState());

  auto& addPathCapa = *capability.addPathCapabilities();
  ASSERT_EQ(2, addPathCapa.size());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, *addPathCapa[0].afi());
  EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *addPathCapa[0].safi());
  EXPECT_EQ(*addPathCapa[0].sor(), BgpAddPathSendRec::RECEIVE);
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *addPathCapa[1].afi());
  EXPECT_EQ(BgpUpdateSafi::SAFI_LABELED_UNICAST, *addPathCapa[1].safi());
  EXPECT_EQ(*addPathCapa[1].sor(), BgpAddPathSendRec::SEND);

  EXPECT_TRUE(*capability.enhancedRouteRefresh());
}

/**
 * Parse a BGP OPEN message with an invalid Enhanced Route Refresh capability
 * code. Ensure the Enhanced Route Refresh capability is not observed.
 */
TEST_F(BgpOpenMessageFixture, BgpOpenMessageEnhancedRouteRefreshInvalidCode) {
  /*
   * Create a wrong code for Enhanced route refresh capability. Here, this
   * capability should not be observed.
   */
  auto msgWithEnhancedRouteRefreshWrongCode = msgWithEnhancedRouteRefresh;
  msgWithEnhancedRouteRefreshWrongCode
      [msgWithEnhancedRouteRefreshWrongCode.size() - 2] = 0xff;
  auto openMsgInvalidCode = BgpMessageParser2::parseBgpOpenMsgRaw(
      folly::IOBuf::wrapBufferAsValue(
          msgWithEnhancedRouteRefreshWrongCode.data(),
          msgWithEnhancedRouteRefreshWrongCode.size()));
  EXPECT_FALSE(*openMsgInvalidCode.capabilities()->enhancedRouteRefresh());
}

/**
 * Parse a BGP OPEN message with an invalid Enhanced Route Refresh capability
 * length. Ensure BgpOpenMsgException is thrown.
 */
TEST_F(
    BgpOpenMessageFixture,
    BgpOpenMessageWithEnhancedRouteRefreshInvalidLength) {
  // change the enhanced route refresh capability length to 1 instead of 0
  auto msgWithEnhancedRouteRefreshWrongLength = msgWithEnhancedRouteRefresh;
  msgWithEnhancedRouteRefreshWrongLength
      [msgWithEnhancedRouteRefreshWrongLength.size() - 1] = 0x01;
  try {
    BgpMessageParser2::parseBgpOpenMsgRaw(
        folly::IOBuf::wrapBufferAsValue(
            msgWithEnhancedRouteRefreshWrongLength.data(),
            msgWithEnhancedRouteRefreshWrongLength.size()));
    ADD_FAILURE() << "Expected BgpOpenMsgException";
  } catch (BgpOpenMsgException const& err) {
    EXPECT_EQ(err.getSubCode(), BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC);
  }
}

/**
 * Parse a BGP OPEN message with Route Refresh capability (RFC 2918).
 * Ensure the route refresh capability is parsed correctly.
 */
TEST_F(BgpOpenMessageFixture, BgpOpenMessageWithRouteRefresh) {
  auto openMsg = BgpMessageParser2::parseBgpOpenMsgRaw(
      folly::IOBuf::wrapBufferAsValue(
          msgWithRouteRefresh.data(), msgWithRouteRefresh.size()));

  auto& capability = *openMsg.capabilities();
  EXPECT_TRUE(*capability.routeRefresh());
  EXPECT_FALSE(*capability.enhancedRouteRefresh());
}

/**
 * Parse a BGP OPEN message with an invalid Route Refresh capability code.
 * Ensure the Route Refresh capability is not observed.
 */
TEST_F(BgpOpenMessageFixture, BgpOpenMessageRouteRefreshInvalidCode) {
  /*
   * Replace the Route Refresh capability code (0x02) with an invalid code.
   * The capability should not be observed.
   */
  auto msgWithRouteRefreshWrongCode = msgWithRouteRefresh;
  msgWithRouteRefreshWrongCode[msgWithRouteRefreshWrongCode.size() - 2] = 0xff;
  auto openMsgInvalidCode = BgpMessageParser2::parseBgpOpenMsgRaw(
      folly::IOBuf::wrapBufferAsValue(
          msgWithRouteRefreshWrongCode.data(),
          msgWithRouteRefreshWrongCode.size()));
  EXPECT_FALSE(*openMsgInvalidCode.capabilities()->routeRefresh());
}

/**
 * Parse a BGP OPEN message with an invalid Route Refresh capability length.
 * Ensure BgpOpenMsgException is thrown.
 */
TEST_F(BgpOpenMessageFixture, BgpOpenMessageWithRouteRefreshInvalidLength) {
  // Change the Route Refresh capability length to 1 instead of 0.
  auto msgWithRouteRefreshWrongLength = msgWithRouteRefresh;
  msgWithRouteRefreshWrongLength[msgWithRouteRefreshWrongLength.size() - 1] =
      0x01;
  try {
    BgpMessageParser2::parseBgpOpenMsgRaw(
        folly::IOBuf::wrapBufferAsValue(
            msgWithRouteRefreshWrongLength.data(),
            msgWithRouteRefreshWrongLength.size()));
    ADD_FAILURE() << "Expected BgpOpenMsgException";
  } catch (BgpOpenMsgException const& err) {
    EXPECT_EQ(err.getSubCode(), BgpNotifOpenMsgErrSubCode::BN_OM_UNSPECIFIC);
  }
}

TEST_F(BgpOpenMessageFixture, BgpOpenMessageWithWrongCapabilitySize) {
  /*
   * In the Malformed msg, we have capability length larger than remaining msg
   * length. We should throw exception upon this case.
   */
  msg[32] = 0xff;
  EXPECT_THROW(
      BgpMessageParser2::parseBgpOpenMsgRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size())),
      BgpOpenMsgException);
}

TEST_F(BgpOpenMessageFixture, BgpOpenMessageWithWrongParameterSize) {
  /*
   * In the Malformed msg, we have parameter length larger than remaining msg
   * length. We should throw exception upon this case.
   */
  msg[30] = 0xff;
  EXPECT_THROW(
      BgpMessageParser2::parseBgpOpenMsgRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size())),
      BgpOpenMsgException);
}

TEST_F(BgpOpenMessageFixture, WrongLength) {
  // patch the second by of the length field
  msg[17] = 0x19;

  EXPECT_NO_THROW({
    try {
      BgpMessageParser2::parseBgpOpenMsgRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()));

      ADD_FAILURE();
    } catch (BgpHeaderException const& err) {
      EXPECT_EQ(BgpNotifMsgHdrErrSubCode::BN_MH_BAD_MSG_LEN, err.getSubCode());
    }
  });
}

TEST_F(BgpOpenMessageFixture, WrongVersion) {
  // patch the version field
  msg[19] = 5;

  auto value = std::array<char, 1>{{4}};
  EXPECT_NO_THROW({
    try {
      BgpMessageParser2::parseBgpOpenMsgRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()));

      ADD_FAILURE();
    } catch (BgpOpenMsgException const& err) {
      EXPECT_EQ(
          BgpNotifOpenMsgErrSubCode::BN_OM_UNSUPPORTED_VERSION_NUMBER,
          err.getSubCode());
      EXPECT_EQ(std::string(value.data(), 1), err.getData());
    }
  });
}

TEST_F(BgpOpenMessageFixture, WrongHoldTime) {
  // patch the hold-time field
  msg[23] = 2;

  EXPECT_NO_THROW({
    try {
      BgpMessageParser2::parseBgpOpenMsgRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()));

      ADD_FAILURE();
    } catch (BgpOpenMsgException const& err) {
      EXPECT_EQ(
          BgpNotifOpenMsgErrSubCode::BN_OM_UNACCEPTABLE_HOLD_TIME,
          err.getSubCode());
    }
  });
}

TEST_F(BgpOpenMessageFixture, WrongOptionalParam) {
  // patch the param type number
  msg[29] = static_cast<uint8_t>(
                apache::thrift::TEnumTraits<BgpOpenMsgParam>::max()) +
      1;

  EXPECT_NO_THROW({
    try {
      BgpMessageParser2::parseBgpOpenMsgRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()));

      ADD_FAILURE();
    } catch (BgpOpenMsgException const& err) {
      EXPECT_EQ(
          BgpNotifOpenMsgErrSubCode::BN_OM_UNSUPPORTED_OPTIONAL_PARAM,
          err.getSubCode());
      EXPECT_EQ(
          std::string(reinterpret_cast<const char*>(&msg[29]), 1),
          err.getData());
    }
  });
}

TEST(BgpMessageParser, OpenWithMultiOptionalParam) {
  std::vector<uint8_t> msg{
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0xff,
      0x00,
      0x4f, // Length of BGP PDU
      0x01, // Bgp message type (OPEN)
      0x04, // BGP Version
      0x80,
      0xa6, // ASN
      0x00,
      0x18, // Hold Time (24 seconds)
      0x01,
      0x02,
      0x03,
      0x04, // BGP ID
      0x32, // Optional Param Length
      // Params
      0x02, // Capabilities
      0x06, // Length
      // Bgp Capabilities
      0x01,
      0x04, // MP Ext, Length-4
      0x00,
      0x01,
      0x00,
      0x01, // V4 + Unicast
      // Params
      0x02, // Capabilities
      0x06, // Length
      // Bgp Capabilities
      0x01,
      0x04, // MP Ext, Length-4
      0x00,
      0x02,
      0x00,
      0x01, // V6 + Unicast
      // Params
      0x02, // Capabilities
      0x02, // Length
      // Bgp Capabilities
      0x80,
      0x00, // Private Use, Ignored
      // Params
      0x02, // Capabilities
      0x02, // Length
      // Bgp Capabilities
      0x02,
      0x00, // Route Refresh, Ignored
      // Params
      0x02, // Capabilities
      0x0c, // Length
      // Bgp Capabilities
      0x40,
      0x0a, // Graceful Restart, Length-10
      0x81,
      0x01, // state = true, time = 257
      0x00,
      0x01,
      0x01,
      0x80, // v4 + Unicast
      0x00,
      0x02,
      0x01,
      0x80, // v6 + Unicast
      // Params
      0x02, // Capabilities
      0x06, // Length
      // Bgp Capabilities
      0x41,
      0x04, // 4 byte ASN, Length-4
      0x01,
      0x02,
      0x03,
      0x04, // ASN value
      // Params
      0x02, // Capabilities
      0x02, // Length
      // Bgp Capabilities
      0x47,
      0x00, // Long-Lived Graceful Restart, Ignored
  };

  auto openMsg = BgpMessageParser2::parseBgpOpenMsgRaw(
      folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()));

  auto& capa = *openMsg.capabilities();

  EXPECT_TRUE(*capa.mpExtV4Unicast());
  EXPECT_TRUE(*capa.mpExtV6Unicast());
  EXPECT_FALSE(*capa.mpExtV4LU());
  EXPECT_FALSE(*capa.mpExtV6LU());
  EXPECT_TRUE(*capa.as4byte());
  EXPECT_EQ(0x01020304, *capa.asn());
  EXPECT_TRUE(*capa.gracefulRestart());
  EXPECT_TRUE(*capa.isRestarting());
  EXPECT_EQ(257, *capa.restartTime());

  auto& grCapa = *capa.grCapabilities();

  ASSERT_EQ(2, grCapa.size());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, *grCapa[0].afi());
  EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *grCapa[0].safi());
  EXPECT_TRUE(*grCapa[0].forwardingState());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *grCapa[1].afi());
  EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *grCapa[1].safi());
  EXPECT_TRUE(*grCapa[1].forwardingState());
}

/*
 * NOTIFICATION message tests
 */

class NotificationMessageFixture : public ::testing::Test {
 public:
  std::vector<uint8_t> msg = {
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x17,   // Length of BGP PDU
    0x03,         // Bgp message type (Notification)
      0x03,       // Update Message Error
      0x05,       // Attribute length error
      0x01, 0x02, // Data
      // clang-format on
  };
};

TEST_F(NotificationMessageFixture, BgpNotificationMessageNormal) {
  auto notif = BgpMessageParser2::parseBgpNotificationRaw(
      folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()));

  EXPECT_EQ(BgpNotifErrCode::BN_UPDATE_MSG_ERR, *notif.errCode());
  EXPECT_EQ(0x05, *notif.errSubCode());
  EXPECT_EQ("BN_UM_ATTR_LEN_ERR", *notif.errSubCodeStr());
  EXPECT_EQ(std::string({0x01, 0x02}), *notif.data());
}

// RFC 8538 Cease/Hard Reset
TEST_F(NotificationMessageFixture, BgpNotificationMessageHardReset) {
  msg[19] = 0x06; // Cease
  msg[20] = 0x09; // Hard Reset
  msg[21] = 0x06; // Data: nested Cease
  msg[22] = 0x02; // Data: nested Administrative Shutdown

  auto notif = BgpMessageParser2::parseBgpNotificationRaw(
      folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()));

  EXPECT_EQ(BgpNotifErrCode::BN_CEASE, *notif.errCode());
  EXPECT_EQ(0x09, *notif.errSubCode());
  EXPECT_EQ("BN_CEASE_HARD_RESET", *notif.errSubCodeStr());
  EXPECT_EQ(std::string({0x06, 0x02}), *notif.data());
}

/*
 * An unnamed subcode keeps the parser's "NA" seed rather than coming through
 * empty. Pins the contract `show bgpcpp neighbors` relies on: its renderer
 * treats "NA" as unnamed and prints the bare number, so a change to this
 * sentinel would silently turn the output into "subcode NA (0)".
 */
TEST_F(NotificationMessageFixture, CeaseZeroSubCodeStaysUnnamed) {
  msg[19] = 0x06; // Cease
  msg[20] = 0x00;
  auto notif = BgpMessageParser2::parseBgpNotificationRaw(
      folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()));

  EXPECT_EQ(BgpNotifErrCode::BN_CEASE, *notif.errCode());
  EXPECT_EQ(0x00, *notif.errSubCode());
  EXPECT_EQ("NA", *notif.errSubCodeStr());
}

TEST_F(NotificationMessageFixture, BgpNotificationMessageWrongLength) {
  // set incorrect length
  msg[17] = 0x14;

  EXPECT_THROW(
      BgpMessageParser2::parseBgpNotificationRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size())),
      BgpHeaderException);
}

TEST_F(NotificationMessageFixture, BgpNotificationMessageWrongCode) {
  // invalid subcode
  msg[19] = 0;

  EXPECT_THROW(
      BgpMessageParser2::parseBgpNotificationRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size())),
      BgpException);

  // change the error code to another invalid value
  msg[19] = static_cast<uint8_t>(
                apache::thrift::TEnumTraits<BgpNotifErrCode>::max()) +
      1;

  EXPECT_THROW(
      BgpMessageParser2::parseBgpNotificationRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size())),
      BgpException);
}

/*
 * Group of tests to validate that parsing notification message
 * throws on exceeding subCodes. Since every code has its own
 * range of sub-codes we pair each code value with subCode type
 */

// To use a value as a type
template <BgpNotifErrCode N>
class TypeValue {
 public:
  static constexpr BgpNotifErrCode value = N;
};

template <typename T>
class BgpNotificationMessageTest : public ::testing::Test {};

// Pair error codes with subCode types
using BgpNotificationTypes = ::testing::Types<
    std::tuple<
        BgpNotifMsgHdrErrSubCode,
        TypeValue<BgpNotifErrCode::BN_MSG_HDR_ERR>>,
    std::tuple<
        BgpNotifOpenMsgErrSubCode,
        TypeValue<BgpNotifErrCode::BN_OPEN_MSG_ERR>>,
    std::tuple<
        BgpNotifUpdateMsgErrSubCode,
        TypeValue<BgpNotifErrCode::BN_UPDATE_MSG_ERR>>,
    std::tuple<BgpNotifCeaseErrSubCode, TypeValue<BgpNotifErrCode::BN_CEASE>>>;

TYPED_TEST_CASE(BgpNotificationMessageTest, BgpNotificationTypes);

TYPED_TEST(BgpNotificationMessageTest, WrongSubcode) {
  std::vector<uint8_t> msg = {
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x17,   // Length of BGP PDU
    0x03,         // Bgp message type (Notification)
      0x03,       // Update Message Error
      0x05,       // Attribute length error
      0x01, 0x02, // Data
    // clang-format off
  };

  // extract the type and error code from template type parameter
  using SubCodeType =
      typename std::tuple_element<0, TypeParam>::type;

  const BgpNotifErrCode kErrorCode =
      std::tuple_element<1, TypeParam>::type::value;

  // set valid error code
  msg[19] = static_cast<uint8_t>(kErrorCode);

  // set error sub-code out of range, yo!
  msg[20] =
      static_cast<uint8_t>(apache::thrift::TEnumTraits<SubCodeType>::max()) + 1;

  EXPECT_THROW(
      BgpMessageParser2::parseBgpNotificationRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size())),
      BgpException);

  //Set Route Refresh error code
  msg[19] = 0x07;
  // set error sub-code out of range
  msg[20] =
      static_cast<uint8_t>(apache::thrift::TEnumTraits<SubCodeType>::max()) + 1;
  EXPECT_THROW(
      BgpMessageParser2::parseBgpNotificationRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size())),
      BgpException);
}

/*
 * ROUTE REFRESH message tests
 */

class RouteRefreshMessageTestFixture : public ::testing::Test {
 public:
  std::vector<uint8_t> msg = {
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x17,   // Length of BGP PDU
    0x05,         // Bgp message type (Route Refresh)
    0x00, 0x01,   // V4 AFI
    0x00,         // Message sub type
    0x01,   // UNICAST SAFI
      // clang-format on
  };
};

/**
 * Parse a valid BGP Route Refresh message
 * Ensure all fields are parsed correctly
 */
TEST_F(RouteRefreshMessageTestFixture, BgpRouteRefreshMessageValid) {
  auto routeRefresh = BgpMessageParser2::parseBgpRouteRefreshRaw(
      folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()));

  EXPECT_EQ(*routeRefresh.afi(), BgpUpdateAfi::AFI_IPv4);
  EXPECT_EQ(
      *routeRefresh.msgSubType(),
      BgpRouteRefreshMessageSubtype::ROUTE_REFRESH_REQUEST);
  EXPECT_EQ(*routeRefresh.safi(), BgpUpdateSafi::SAFI_UNICAST);
}

/**
 * Parse a Route Refresh message with wrong length
 * Ensure BgpRouteRefreshMsgException is thrown with subcode BN_INVALID_MSG_LEN
 */
TEST_F(RouteRefreshMessageTestFixture, BgpRouteRefreshMessageWrongLength) {
  // Set Message subtype to 1(BoRR)
  msg[21] = 0x01;

  // Add 1 byte to the length
  msg.emplace_back(0x01);

  try {
    BgpMessageParser2::parseBgpRouteRefreshRaw(
        folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()));
  } catch (BgpRouteRefreshMsgException const& err) {
    EXPECT_EQ(
        BgpNotificationRouteRefreshErrSubCode::BN_INVALID_MSG_LEN,
        err.getSubCode());
    EXPECT_EQ(
        std::string(err.what()),
        "Unexpected length of Route Refresh msg. Got 23, expected 4");
  }
}

TEST(BgpMessageParser, BgpKeepAliveMessage) {
  std::vector<uint8_t> msg = {
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x13, // Length of BGP PDU
    0x04, // Bgp message type (KeepAlive)
      // clang-format on
  };

  auto keepAlive = BgpMessageParser2::parseBgpKeepAliveRaw(
      folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()));

  SUCCEED();
}

/*
 * UPDATE message tests
 */

TEST(BgpMessageParser, BgpUpdateTestAsConfedSegments) {
  std::vector<uint8_t> msg = {
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x75, // Length of BGP PDU
    0x02, // Bgp message type (Update)
    0x00, 0x05, // Withdrawn routes length
    0x20, 0x09, 0x08, 0x07, 0x06, // Withdraw prefix, "9.8.7.6/32"
    // Path Attributes
    0x00, 0x54, // Path attributes length (34 bytes)
      0x40, 0x01, 0x01, // ORIGIN
        0x00, // IGP
      0x40, 0x02, 0x06, // AS_PATH
        0x03, // Segment type: AS_CONFED_SEQUENCE
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa6, // asn: 32934
      0x40, 0x03, 0x04, // NEXT_HOP
        0x01, 0x02, 0x03, 0x04, // "1.2.3.4"
      0x80, 0x04, 0x04,  // MULTI_EXIT_DESC
        0x00, 0x00, 0x00, 0x20, // 32
      0x40, 0x05, 0x04, // LOCAL_PREF
        0x00, 0x00, 0x00, 0x64, // 100
      0x40, 0x06, 0x00, // ATOMIC_AGGREGATOR
      0xc0, 0x07, 0x08, // AGGREGATOR
        0x00, 0x00, 0x12, 0x34, // asn: 4660
        0x03, 0x04, 0x05, 0x06, // ip: "3.4.5.6"
      0xc0, 0x08, 0x04, // COMMUNITIES
        0xff, 0xfa, // asn: 65530
        0x3d, 0xb8, // value: 15800
      0x80, 0x09, 0x04, // ORIGINATOR_ID
        0x00, 0x00, 0x03, 0x12, // id: 786
      0x80, 0x0a, 0x08, // CLUSTER_LIST
        0x00, 0x00, 0x00, 0x6E, // id: 110
        0x00, 0x00, 0x03, 0x12, // id: 786
      0xc0, 0x10, 0x08, // EXTENDED_COMMUNITIES
        0x00, 0x02, 0x27, 0x2a, // Community-1 first 4 bytes
        0x00, 0x00, 0x23, 0x2f, // Community-2 next 4 bytes
    // Network Layer Reachability information
    0x20, 0x06, 0x05, 0x04, 0x03, // Update prefix, "6.5.4.3/32"
      // clang-format on
  };

  auto updates = toBgpUpdate(
      BgpMessageParser2::parseBgpUpdateRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()),
          capabilities));
  ASSERT_EQ(2, updates.size());
  auto update = updates[1];

  EXPECT_EQ(1, update.attrs()->asPath()->size());
  auto path = update.attrs()->asPath()[0];
  EXPECT_EQ(0, path.asSet()->size());
  EXPECT_EQ(0, path.asSequence()->size());
  EXPECT_EQ(0, path.asConfedSet()->size());
  ASSERT_EQ(1, path.asConfedSequence()->size());
  EXPECT_EQ(32934, path.asConfedSequence()[0]);

  msg[35] = 0x04; // SWITCH TO AS CONFED SET
  updates = toBgpUpdate(
      BgpMessageParser2::parseBgpUpdateRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()),
          capabilities));
  ASSERT_EQ(2, updates.size());
  update = updates[1];

  EXPECT_EQ(1, update.attrs()->asPath()->size());
  path = update.attrs()->asPath()[0];
  EXPECT_EQ(0, path.asSet()->size());
  EXPECT_EQ(0, path.asSequence()->size());
  EXPECT_EQ(0, path.asConfedSequence()->size());
  ASSERT_EQ(1, path.asConfedSet()->size());
  EXPECT_EQ(1, path.asConfedSet()->count(32934));
}

TEST(BgpMessageParser, BgpUpdateWithdrawV4Attr) {
  auto msg = kBgpUpdateWithdrawV4AttrMsg;
  auto updates = toBgpUpdate(
      BgpMessageParser2::parseBgpUpdateRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()),
          capabilities));

  ASSERT_EQ(2, updates.size());

  auto withdraw = updates[0];
  EXPECT_EQ(BgpUpdateType::BU_WITHDRAW, *withdraw.type());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, *withdraw.afi());
  EXPECT_EQ("9.8.7.6/32", *withdraw.prefix());

  auto update = updates[1];
  EXPECT_EQ(BgpUpdateType::BU_UPDATE, update.type());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, update.afi());
  EXPECT_EQ("6.5.4.3/32", update.prefix());
  EXPECT_EQ(BgpAttrOrigin::BGP_ORIGIN_IGP, *update.attrs()->origin());
  EXPECT_EQ(1, update.attrs()->asPath()->size());

  if (update.attrs()->asPath()->size() == 1) {
    auto& path = update.attrs()->asPath()[0];
    EXPECT_EQ(0, path.asSet()->size());
    EXPECT_EQ(1, path.asSequence()->size());
    if (path.asSequence()->size()) {
      EXPECT_EQ(32934, path.asSequence()[0]);
    }
  }

  EXPECT_EQ("1.2.3.4", *update.attrs()->nexthop());
  EXPECT_EQ(32, *update.attrs()->med());

  ASSERT_TRUE(update.attrs()->localPref().has_value());
  EXPECT_EQ(100, *update.attrs()->localPref());
  EXPECT_TRUE(*update.attrs()->atomicAggregate());
  EXPECT_EQ(4660, *update.attrs()->aggregator()->asn());
  EXPECT_EQ("3.4.5.6", *update.attrs()->aggregator()->ip());
  EXPECT_EQ(1, update.attrs()->communities()->size());

  if (update.attrs()->communities()->size()) {
    EXPECT_EQ(65530, *update.attrs()->communities()[0].asn());
    EXPECT_EQ(15800, *update.attrs()->communities()[0].value());
  }

  EXPECT_EQ(0x12030000, *update.attrs()->originatorId());
  EXPECT_EQ(2, update.attrs()->clusterList()->size());

  if (update.attrs()->clusterList()->size() == 2) {
    EXPECT_EQ(0x10010000, update.attrs()->clusterList()[0]);
    EXPECT_EQ(0x86070000, update.attrs()->clusterList()[1]);
  }

  EXPECT_EQ(1, update.attrs()->extCommunities()->size());

  if (update.attrs()->extCommunities()->size()) {
    auto extCommunity = update.attrs()->extCommunities()[0];
    EXPECT_EQ(0x2272a, *extCommunity.firstWord());
    EXPECT_EQ(0x232f, *extCommunity.secondWord());
  }

  EXPECT_EQ(2, update.attrs()->largeCommunities()->size());

  if (update.attrs()->largeCommunities()->size()) {
    for (const auto& largeCommunity : *update.attrs()->largeCommunities()) {
      EXPECT_EQ(65530, *largeCommunity.asn());
      EXPECT_EQ(0x2272a, *largeCommunity.localData1());
      EXPECT_EQ(0x232a, *largeCommunity.localData2());
    }
  }
}

TEST(BgpMessageParser, BgpUpdateWithdrawV6Attr) {
  auto msg = kBgpUpdateWithdrawV6AttrMsg;
  auto updates = toBgpUpdate(
      BgpMessageParser2::parseBgpUpdateRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()),
          capabilities));

  ASSERT_EQ(2, updates.size());
  auto withdraw = updates[0];
  EXPECT_EQ(BgpUpdateType::BU_WITHDRAW, *withdraw.type());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *withdraw.afi());
  EXPECT_EQ("face:b00c::3000/122", *withdraw.prefix());

  auto update = updates[1];
  EXPECT_EQ(BgpUpdateType::BU_UPDATE, update.type());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, update.afi());
  EXPECT_EQ("fd00::3000/122", update.prefix());
  EXPECT_EQ(BgpAttrOrigin::BGP_ORIGIN_EGP, *update.attrs()->origin());
  EXPECT_EQ(1, update.attrs()->asPath()->size());
  if (update.attrs()->asPath()->size() == 1) {
    auto& path = update.attrs()->asPath()[0];
    EXPECT_EQ(1, path.asSet()->size());
    EXPECT_EQ(0, path.asSequence()->size());
    EXPECT_NE(path.asSet()->end(), path.asSet()->find(32934));
  }
  EXPECT_EQ("fd00::1", *update.attrs()->nexthop());
  EXPECT_EQ(32, *update.attrs()->med());

  ASSERT_TRUE(update.attrs()->localPref().has_value());
  EXPECT_EQ(100, *update.attrs()->localPref());
  EXPECT_FALSE(*update.attrs()->atomicAggregate());
  EXPECT_EQ(update.attrs()->communities()->size(), 1);
  if (update.attrs()->communities()->size()) {
    EXPECT_EQ(65530, *update.attrs()->communities()[0].asn());
    EXPECT_EQ(15800, *update.attrs()->communities()[0].value());
  }
  EXPECT_EQ(0x86070000, *update.attrs()->originatorId());

  EXPECT_EQ(2, update.attrs()->largeCommunities()->size());
  if (update.attrs()->largeCommunities()->size()) {
    for (const auto& largeCommunity : *update.attrs()->largeCommunities()) {
      EXPECT_EQ(65530, *largeCommunity.asn());
      EXPECT_EQ(0x2272a, *largeCommunity.localData1());
      EXPECT_EQ(0x232a, *largeCommunity.localData2());
    }
  }
}

TEST(BgpMessageParser, BgpUpdateWithdrawV6Only) {
  auto msg = kBgpUpdateWithdrawV6OnlyMsg;

  auto updates = toBgpUpdate(
      BgpMessageParser2::parseBgpUpdateRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()),
          capabilities));

  ASSERT_EQ(1, updates.size());

  auto v6Withdraw = updates[0];
  EXPECT_EQ(BgpUpdateType::BU_WITHDRAW, *v6Withdraw.type());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *v6Withdraw.afi());
  EXPECT_EQ("face:b00c::3000/122", *v6Withdraw.prefix());
}

TEST(BgpMessageParser, BgpUpdateWithdrawV4V6) {
  auto msg = kBgpUpdateWithdrawV4V6Msg;
  auto updates = toBgpUpdate(
      BgpMessageParser2::parseBgpUpdateRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()),
          capabilities));

  ASSERT_EQ(4, updates.size());

  auto v4Withdraw = updates[0];
  EXPECT_EQ(BgpUpdateType::BU_WITHDRAW, *v4Withdraw.type());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, *v4Withdraw.afi());
  EXPECT_EQ("1.2.0.0/16", *v4Withdraw.prefix());

  auto v6Withdraw = updates[1];
  EXPECT_EQ(BgpUpdateType::BU_WITHDRAW, *v6Withdraw.type());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *v6Withdraw.afi());
  EXPECT_EQ("face:b00c::3000/122", *v6Withdraw.prefix());

  auto v6Update = updates[2];
  EXPECT_EQ(BgpUpdateType::BU_UPDATE, *v6Update.type());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *v6Update.afi());
  EXPECT_EQ("fd00::3000/122", *v6Update.prefix());
  EXPECT_EQ(BgpAttrOrigin::BGP_ORIGIN_EGP, *v6Update.attrs()->origin());
  EXPECT_EQ(1, v6Update.attrs()->asPath()->size());

  if (v6Update.attrs()->asPath()->size() == 1) {
    auto& path = v6Update.attrs()->asPath()[0];
    EXPECT_EQ(1, path.asSet()->size());
    EXPECT_EQ(0, path.asSequence()->size());
    EXPECT_NE(path.asSet()->end(), path.asSet()->find(32934));
  }

  EXPECT_EQ(*v6Update.attrs()->nexthop(), "fd00::1");
  ASSERT_TRUE(v6Update.attrs()->localPref().has_value());
  EXPECT_EQ(100, *v6Update.attrs()->localPref());

  auto v4Update = updates[3];
  EXPECT_EQ(BgpUpdateType::BU_UPDATE, *v4Update.type());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, *v4Update.afi());
  EXPECT_EQ("4.5.0.0/16", *v4Update.prefix());
  EXPECT_EQ(BgpAttrOrigin::BGP_ORIGIN_EGP, *v4Update.attrs()->origin());
  EXPECT_EQ(1, v4Update.attrs()->asPath()->size());
  if (v4Update.attrs()->asPath()->size() == 1) {
    auto& path = v4Update.attrs()->asPath()[0];
    EXPECT_EQ(1, path.asSet()->size());
    EXPECT_EQ(0, path.asSequence()->size());
    EXPECT_NE(path.asSet()->end(), path.asSet()->find(32934));
  }
  EXPECT_EQ("7.6.5.4", *v4Update.attrs()->nexthop());
  ASSERT_TRUE(v4Update.attrs()->localPref().has_value());
  EXPECT_EQ(100, *v4Update.attrs()->localPref());
}

TEST(BgpMessageParser, BgpUpdateWithdrawV4WithLabels) {
  auto msg = kBgpUpdateWithdrawV4WithLabelsMsg;
  auto updates = toBgpUpdate(
      BgpMessageParser2::parseBgpUpdateRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()),
          capabilities));

  ASSERT_EQ(2, updates.size());

  auto v4Withdraw = updates[0];
  EXPECT_EQ(BgpUpdateType::BU_WITHDRAW, *v4Withdraw.type());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, *v4Withdraw.afi());
  EXPECT_EQ("4.5.6.0/24", *v4Withdraw.prefix());
  EXPECT_EQ(2, v4Withdraw.labels()->size());
  if (v4Withdraw.labels()->size() == 2) {
    EXPECT_EQ(8, v4Withdraw.labels()[0]);
    EXPECT_EQ(9, v4Withdraw.labels()[1]);
  }

  auto v4Update = updates[1];

  EXPECT_EQ(BgpUpdateType::BU_UPDATE, *v4Update.type());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, *v4Update.afi());
  EXPECT_EQ("4.5.6.0/23", *v4Update.prefix());
  EXPECT_EQ(BgpAttrOrigin::BGP_ORIGIN_EGP, *v4Update.attrs()->origin());
  EXPECT_EQ(1, v4Update.attrs()->asPath()->size());

  if (v4Update.attrs()->asPath()->size() == 1) {
    auto& path = v4Update.attrs()->asPath()[0];
    EXPECT_EQ(1, path.asSet()->size());
    EXPECT_EQ(0, path.asSequence()->size());
    EXPECT_TRUE(path.asSet()->find(32934) != path.asSet()->end());
  }
  EXPECT_EQ("1.2.3.4", *v4Update.attrs()->nexthop());

  ASSERT_TRUE(v4Update.attrs()->localPref().has_value());
  EXPECT_EQ(100, *v4Update.attrs()->localPref());
  EXPECT_EQ(2, v4Update.labels()->size());
  if (v4Update.labels()->size() == 2) {
    EXPECT_EQ(5, v4Update.labels()[0]);
    EXPECT_EQ(4, v4Update.labels()[1]);
  }
}

TEST(BgpMessageParser, BgpUpdateWithdrawV6WithLabels) {
  auto msg = kBgpUpdateWithdrawV6WithLabelsMsg;
  auto updates = toBgpUpdate(
      BgpMessageParser2::parseBgpUpdateRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()),
          capabilities));

  ASSERT_EQ(2, updates.size());

  auto v6Withdraw = updates[0];

  EXPECT_EQ(BgpUpdateType::BU_WITHDRAW, *v6Withdraw.type());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *v6Withdraw.afi());
  EXPECT_EQ("abcd:ef00::/24", *v6Withdraw.prefix());
  EXPECT_EQ(2, v6Withdraw.labels()->size());

  if (v6Withdraw.labels()->size() == 2) {
    EXPECT_EQ(8, v6Withdraw.labels()[0]);
    EXPECT_EQ(9, v6Withdraw.labels()[1]);
  }

  auto v6Update = updates[1];

  EXPECT_EQ(BgpUpdateType::BU_UPDATE, *v6Update.type());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *v6Update.afi());
  EXPECT_EQ("dead::/23", *v6Update.prefix());
  EXPECT_EQ(BgpAttrOrigin::BGP_ORIGIN_EGP, *v6Update.attrs()->origin());
  EXPECT_EQ(1, v6Update.attrs()->asPath()->size());

  if (v6Update.attrs()->asPath()->size() == 1) {
    auto& path = v6Update.attrs()->asPath()[0];
    EXPECT_EQ(1, path.asSet()->size());
    EXPECT_EQ(0, path.asSequence()->size());
    EXPECT_NE(path.asSet()->end(), path.asSet()->find(32934));
  }
  EXPECT_EQ("fd00::1", *v6Update.attrs()->nexthop());
  ASSERT_TRUE(v6Update.attrs()->localPref().has_value());
  EXPECT_EQ(100, *v6Update.attrs()->localPref());
  EXPECT_EQ(2, v6Update.labels()->size());
  if (v6Update.labels()->size() == 2) {
    EXPECT_EQ(5, v6Update.labels()[0]);
    EXPECT_EQ(4, v6Update.labels()[1]);
  }
}

TEST(BgpMessageParser, BgpUpdateV6Nexthop32) {
  auto msg = kBgpUpdateV6Nexthop32Msg;
  auto updates = toBgpUpdate(
      BgpMessageParser2::parseBgpUpdateRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()),
          capabilities));
  ASSERT_EQ(3, updates.size());

  auto update = updates[0];

  EXPECT_EQ(BgpUpdateType::BU_UPDATE, update.type());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, update.afi());
  EXPECT_EQ("2001:db8:1:2::/64", update.prefix());
  EXPECT_EQ(BgpAttrOrigin::BGP_ORIGIN_IGP, *update.attrs()->origin());
  EXPECT_EQ(1, update.attrs()->asPath()->size());

  if (update.attrs()->asPath()->size() == 1) {
    auto& path = update.attrs()->asPath()[0];
    EXPECT_EQ(0, path.asSet()->size());
    EXPECT_EQ(1, path.asSequence()->size());
    if (path.asSequence()->size() == 1) {
      EXPECT_EQ(65001, path.asSequence()[0]);
    }
  }

  EXPECT_EQ("2001:db8::1", *update.attrs()->nexthop());
  EXPECT_EQ(0, *update.attrs()->med());
  EXPECT_FALSE(update.attrs()->localPref().has_value());

  update = updates[1];

  EXPECT_EQ(BgpUpdateType::BU_UPDATE, update.type());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, update.afi());
  EXPECT_EQ("2001:db8:1:1::/64", update.prefix());
  EXPECT_EQ(BgpAttrOrigin::BGP_ORIGIN_IGP, *update.attrs()->origin());
  EXPECT_EQ(1, update.attrs()->asPath()->size());

  if (update.attrs()->asPath()->size() == 1) {
    auto& path = update.attrs()->asPath()[0];
    EXPECT_EQ(0, path.asSet()->size());
    EXPECT_EQ(1, path.asSequence()->size());
    if (path.asSequence()->size() == 1) {
      EXPECT_EQ(65001, path.asSequence()[0]);
    }
  }
  EXPECT_EQ("2001:db8::1", *update.attrs()->nexthop());
  EXPECT_EQ(0, *update.attrs()->med());

  EXPECT_FALSE(update.attrs()->localPref().has_value());

  update = updates[2];

  EXPECT_EQ(BgpUpdateType::BU_UPDATE, update.type());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, update.afi());
  EXPECT_EQ("2001:db8:1::/64", update.prefix());
  EXPECT_EQ(BgpAttrOrigin::BGP_ORIGIN_IGP, *update.attrs()->origin());
  EXPECT_EQ(1, update.attrs()->asPath()->size());

  if (update.attrs()->asPath()->size() == 1) {
    auto& path = update.attrs()->asPath()[0];
    EXPECT_EQ(0, path.asSet()->size());
    EXPECT_EQ(1, path.asSequence()->size());
    if (path.asSequence()->size() == 1) {
      EXPECT_EQ(65001, path.asSequence()[0]);
    }
  }
  EXPECT_EQ("2001:db8::1", *update.attrs()->nexthop());
  EXPECT_EQ(0, *update.attrs()->med());

  EXPECT_FALSE(update.attrs()->localPref().has_value());
}

TEST(BgpMessageParser, BgpUpdateMessageWithoutLocalPref) {
  auto msg = kBgpUpdateMessageWithoutLocalPref;
  auto updates = toBgpUpdate(
      BgpMessageParser2::parseBgpUpdateRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()),
          capabilities));

  ASSERT_EQ(1, updates.size());

  auto update = updates[0];

  EXPECT_FALSE(update.attrs()->localPref().has_value());
}

TEST(BgpMessageParser, BgpUpdateMessageWithoutMed) {
  auto msg = kBgpUpdateMessageWithoutMed;
  auto updates = toBgpUpdate(
      BgpMessageParser2::parseBgpUpdateRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()),
          capabilities));

  ASSERT_EQ(1, updates.size());

  auto update = updates[0];

  EXPECT_FALSE(update.attrs()->isMedSet().value());
}

TEST(BgpMessageParser, BgpUpdateV4EOR) {
  auto msg = kBgpUpdateV4EORMsg;
  auto updates = toBgpUpdate(
      BgpMessageParser2::parseBgpUpdateRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()),
          capabilities));

  ASSERT_EQ(1, updates.size());

  auto update = updates[0];

  EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, update.afi());
  EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, update.safi());
  EXPECT_EQ(BgpUpdateType::BU_ENDOFRIB, update.type());
}

TEST(BgpMessageParser, BgpUpdateV6EOR) {
  auto msg = kBgpUpdateV6EORMsg;
  auto updates = toBgpUpdate(
      BgpMessageParser2::parseBgpUpdateRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()),
          capabilities));

  ASSERT_EQ(1, updates.size());

  auto update = updates[0];

  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, update.afi());
  EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, update.safi());
  EXPECT_EQ(BgpUpdateType::BU_ENDOFRIB, update.type());
}

/*
 * UPDATE message errors
 */

TEST_F(BgpUpdateMessageErrorFixture, WrongMsgLength) {
  // set length less than 19 + 4
  msg[17] = 0x16;

  EXPECT_NO_THROW({
    try {
      BgpMessageParser2::parseBgpUpdateRaw(
          folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()),
          capabilities);
      ADD_FAILURE();
    } catch (BgpHeaderException const& err) {
      EXPECT_EQ(
          std::string(reinterpret_cast<const char*>(&msg[16]), 2),
          err.getData());
    }
  });
}

TEST_F(BgpUpdateMessageErrorFixture, TruncatedMessage) {
  try {
    // truncate the size of the message
    BgpMessageParser2::parseBgpUpdateRaw(
        folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size() - 6),
        capabilities);
    ADD_FAILURE();
  } catch (BgpHeaderException const& err) {
    EXPECT_EQ(BgpNotifMsgHdrErrSubCode::BN_MH_BAD_MSG_LEN, err.getSubCode());
  } catch (std::exception const& err) {
    XLOGF(ERR, "Unexpected exception: {}", folly::exceptionStr(err));
    ADD_FAILURE();
  }
}

TEST(BgpMessageParser, BgpUpdateMessageMissingOriginAttr) {
  auto msg = kBgpUpdateMessageMissingOriginAttrMsg;
  try {
    BgpMessageParser2::parseBgpUpdateRaw(
        folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()), capabilities);
    ADD_FAILURE();
  } catch (BgpUpdateMsgException const& err) {
    EXPECT_EQ(
        BgpNotifUpdateMsgErrSubCode::BN_UM_MISSING_WELL_KNOWN_ATTR,
        err.getSubCode());
    EXPECT_EQ(
        static_cast<uint8_t>(BgpAttrCode::BGP_ATTR_ORIGIN), err.getData()[0]);
  } catch (std::exception const& err) {
    XLOGF(ERR, "Unexpected exception: {}", folly::exceptionStr(err));
    ADD_FAILURE();
  }
}

TEST(BgpMessageParser, BgpUpdateMessageMissingAsPathAttr) {
  auto msg = kBgpUpdateMessageMissingAsPathAttrMsg;

  try {
    BgpMessageParser2::parseBgpUpdateRaw(
        folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()), capabilities);
    ADD_FAILURE();
  } catch (BgpUpdateMsgException const& err) {
    EXPECT_EQ(
        BgpNotifUpdateMsgErrSubCode::BN_UM_MISSING_WELL_KNOWN_ATTR,
        err.getSubCode());
    EXPECT_EQ(
        static_cast<uint8_t>(BgpAttrCode::BGP_ATTR_AS_PATH), err.getData()[0]);
  } catch (std::exception const& err) {
    XLOGF(ERR, "Unexpected exception: {}", folly::exceptionStr(err));
    ADD_FAILURE();
  }
}

TEST(BgpMessageParser, BgpUpdateMessageMissingNextHopV4) {
  /*
   * the message below is correct but missing the AsPath
   * attribute
   */
  auto msg = kBgpUpdateMessageMissingNextHopV4Msg;
  try {
    BgpMessageParser2::parseBgpUpdateRaw(
        folly::IOBuf::wrapBufferAsValue(msg.data(), msg.size()), capabilities);
    ADD_FAILURE();
  } catch (BgpUpdateMsgException const& err) {
    EXPECT_EQ(
        BgpNotifUpdateMsgErrSubCode::BN_UM_MISSING_WELL_KNOWN_ATTR,
        err.getSubCode());
    EXPECT_EQ(
        static_cast<uint8_t>(BgpAttrCode::BGP_ATTR_NEXT_HOP), err.getData()[0]);
  } catch (std::exception const& err) {
    XLOGF(ERR, "Unexpected exception: {}", folly::exceptionStr(err));
    ADD_FAILURE();
  }
}

TEST(BgpMessageSerializer, multipleNlri) {
  const std::vector<uint8_t> raw = {
      // clang-format off
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0x01, 0x1e, 0x02, 0x00, 0x00, 0x01, 0x07, 0x40,
      0x01, 0x01, 0x00, 0x40, 0x02, 0x00, 0x40, 0x05, 0x04, 0x00, 0x00, 0x00,
      0x64, 0x80, 0x1d, 0x57, 0x04, 0x40, 0x00, 0x04, 0x00, 0x00, 0x80, 0x00,
      0x04, 0x41, 0x00, 0x04, 0x4e, 0x95, 0x02, 0xf9, 0x04, 0x42, 0x00, 0x04,
      0x4e, 0x5f, 0x84, 0x76, 0x04, 0x43, 0x00, 0x20, 0x4e, 0x5f, 0x84, 0x76,
      0x4e, 0x5f, 0x84, 0x76, 0x4e, 0x5f, 0x84, 0x76, 0x4e, 0x5f, 0x84, 0x76,
      0x4e, 0x5f, 0x84, 0x76, 0x4e, 0x5f, 0x84, 0x76, 0x4e, 0x5f, 0x84, 0x76,
      0x4e, 0x5f, 0x84, 0x76, 0x04, 0x44, 0x00, 0x04, 0x00, 0x03, 0x2d, 0x48,
      0x04, 0x47, 0x00, 0x03, 0x03, 0x2d, 0x48, 0x04, 0x48, 0x00, 0x08, 0x00,
      0x01, 0xd7, 0x4f, 0x00, 0x01, 0xd7, 0x4e,
      // Path Attribute - MP_REACH_NLRI
      0x90,  // Flags: 0x90, Optional, Extended-Length, Non-transitive, Complete
      0x0e,  // Type Code: MP_REACH_NLRI (14)
      0x00, 0x9b,  // Length: 155
      0x40, 0x04,  // Address family identifier (AFI): BGP-LS (16388)
      0x47,        // Subsequent address family identifier (SAFI): BGP-LS (71)
      0x04, 0x9d, 0xf0, 0x3d, 0x63,  // Next Hop: 157.240.61.99
      0x00,        // Number of Subnetwork points of attachment (SNPA): 0
      0x00, 0x02,  // NLRI Type: Link NLRI (2)
      0x00, 0x45,  // NLRI Length: 69
      0x02,        // Protocol ID: IS-IS Level 2 (2)
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00,                                // Identifier: L3 packet topology (0)
      0x01, 0x00,                          // Type: 256
      0x00, 0x12,                          // Length: 18
      0x02, 0x00,                          // Type: 512
      0x00, 0x04,                          // Length: 4
      0x00, 0x00, 0x80, 0xa6,              // AS ID: 32934 (0x000080a6)
      0x02, 0x03,                          // Type: 515
      0x00, 0x06,                          // Length: 6
      0x20, 0x40, 0x15, 0x02, 0x11, 0x07,  // IGP ID: 204015021107
      0x01, 0x01,                          // Type: 257
      0x00, 0x12,                          // Length: 18
      0x02, 0x00,                          // Type: 512
      0x00, 0x04,                          // Length: 4
      0x00, 0x00, 0x80, 0xa6,              // AS ID: 32934 (0x000080a6)
      0x02, 0x03,                          // Type: 515
      0x00, 0x06,                          // Length: 6
      0x15, 0x72, 0x40, 0x05, 0x80, 0x86,  // IGP ID: 157240058086
      0x01, 0x03,                          // Type: 259
      0x00, 0x04,                          // Length: 4
      0x1f, 0x0d, 0x1a, 0x87,  // IPv4 Interface Address: 31.13.26.135
      0x01, 0x04,              // Type: 260
      0x00, 0x04,              // Length: 4
      0x1f, 0x0d, 0x1a, 0x86,  // IPv4 Neighbor Address: 31.13.26.134
      0x00, 0x02,              // NLRI Type: Link NLRI (2)
      0x00, 0x45,              // NLRI Length: 69
      0x02,                    // Protocol ID: IS-IS Level 2 (2)
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00,  // Identifier: L3 packet topology (0)

      0x01, 0x00,                          // Type: 256
      0x00, 0x12,                          // Length: 18
      0x02, 0x00,                          // Type: 512
      0x00, 0x04,                          // Length: 4
      0x00, 0x00, 0x80, 0xa6,              // AS ID: 32934 (0x000080a6)
      0x02, 0x03,                          // Type: 515
      0x00, 0x06,                          // Length: 6
      0x20, 0x40, 0x15, 0x02, 0x11, 0x08,  // IGP ID: 204015021108
      0x01, 0x01,                          // Type: 257
      0x00, 0x12,                          // Length: 18
      0x02, 0x00,                          // Type: 512
      0x00, 0x04,                          // Length: 4
      0x00, 0x00, 0x80, 0xa6,              // AS ID: 32934 (0x000080a6)
      0x02, 0x03,                          // Type: 515
      0x00, 0x06,                          // Length: 6
      0x15, 0x72, 0x40, 0x05, 0x80, 0x86,  // IGP ID: 157240058086
      0x01, 0x03,                          // Type: 259
      0x00, 0x04,                          // Length: 4
      0x1f, 0x0d, 0x1e, 0xb7,  // IPv4 Interface Address: 31.13.30.183
      0x01, 0x04,              // Type: 260
      0x00, 0x04,              // Length: 4
      0x1f, 0x0d, 0x1e, 0xb6   // IPv4 Neighbor Address: 31.13.30.182
      // clang-format on
  };

  // capabilities used to parse raw bgp update mesaages
  BgpCapabilities testCapabilities;
  testCapabilities.mpExtLs() = true;

  auto buf = folly::IOBuf::wrapBuffer(raw.data(), raw.size());
  auto parsed = std::get<std::shared_ptr<const BgpUpdate2>>(
      BgpMessageParser2::parseBgpUpdateRaw(*buf, testCapabilities));

  CHECK_EQ(0, parsed->mpAnnounced()->prefixes()->size());
}
} // namespace bgplib
} // namespace nettools
} // namespace facebook

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  folly::Init init(&argc, &argv);
  google::InstallFailureSignalHandler();

  // Init
  facebook::nettools::bgplib::init();

  // run the unittests
  return RUN_ALL_TESTS();
}
