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
#include <gtest/gtest.h>

#include "common/network/AddressUtil.h"
#include "neteng/fboss/bgp/cpp/lib/BgpException.h"
#include "neteng/fboss/bgp/cpp/lib/BgpMessageParser.h"
#include "neteng/fboss/bgp/cpp/lib/BgpMessageSerializer.h"
#include "neteng/fboss/bgp/if/gen-cpp2/BgpStructs_types.h"

namespace facebook {
namespace nettools {
namespace bgplib {

using folly::IOBuf;
using folly::io::RWPrivateCursor;

const std::string PEER_IP = "11.22.33.44";

RiggedIPPrefix createRiggedIPPrefix(
    const std::string& prefixString,
    const int32_t pathId) {
  RiggedIPPrefix ipPrefix;
  ipPrefix.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork(prefixString));
  ipPrefix.labels() = {};
  ipPrefix.pathId() = pathId;
  return ipPrefix;
}

// Prepopulates BgpUpdate2 with dummy data for v4 testing that totals 1020 bytes
void populatePathAttributesV4(BgpUpdate2& update) {
  *update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment;
  for (int i = 0; i < 60; i++) {
    segment.asSequence()->push_back(i);
  }
  update.attrs()->asPath()->push_back(segment);
  *update.v4Nexthop() = network::toBinaryAddress(folly::IPAddress("1.2.3.4"));
  update.attrs()->med() = 32;
  update.attrs()->isMedSet() = true;
  update.attrs()->localPref() = 100;
  *update.attrs()->atomicAggregate() = true;
  *update.attrs()->aggregator()->asn() = 4660;
  *update.attrs()->aggregator()->ip() = "3.4.5.6";
  BgpAttrCommunity community;
  *community.asn() = 65530;
  *community.value() = 15800;
  for (int i = 0; i < 60; i++) {
    update.attrs()->communities()->push_back(community);
  }
  *update.attrs()->originatorId() = 0x86070000; // ip: "0.0.7.134""
  for (int i = 0; i < 60; i++) {
    update.attrs()->clusterList()->push_back(0x10010000); // ip:
                                                          // "0.0.1.16"
  }
  BgpAttrExtCommunity extCommunity;
  *extCommunity.firstWord() = 0x2272a;
  *extCommunity.secondWord() = 0x232f;
  for (int i = 0; i < 30; i++) {
    update.attrs()->extCommunities()->push_back(extCommunity);
  }
}

// Prepopulates BgpUpdate2 with dummy data for v6 testing that totals 879 bytes
void populatePathAttributesV6(BgpUpdate2& update) {
  *update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_EGP;
  BgpAttrAsPathSegment segment;
  for (int i = 0; i < 60; i++) {
    segment.asSequence()->push_back(i);
  }
  update.attrs()->asPath()->push_back(segment);
  update.attrs()->med() = 32;
  update.attrs()->isMedSet() = true;
  update.attrs()->localPref() = 100;
  BgpAttrCommunity community;
  *community.asn() = 65530;
  *community.value() = 15800;
  for (int i = 0; i < 60; i++) {
    update.attrs()->communities()->push_back(community);
  }
  *update.attrs()->originatorId() = 0x86070000; // ip: "0.0.7.134"
  for (int i = 0; i < 30; i++) {
    update.attrs()->clusterList()->push_back(0x10010000); // ip:
                                                          // "0.0.1.16"
  }
  BgpAttrExtCommunity extCommunity;
  *extCommunity.firstWord() = 0x2272a;
  *extCommunity.secondWord() = 0x232f;
  for (int i = 0; i < 30; i++) {
    update.attrs()->extCommunities()->push_back(extCommunity);
  }
}

TEST(BgpAttrCommunityCTest, CreateBgpAttrCommunityFromString) {
  {
    auto attr = *BgpAttrCommunityC::createBgpAttrCommunity("internet");
    EXPECT_EQ(attr.asn, 0);
    EXPECT_EQ(attr.value, 0);
  }
  {
    auto attr = *BgpAttrCommunityC::createBgpAttrCommunity("no-export");
    EXPECT_EQ(attr.asn, 0xFFFF);
    EXPECT_EQ(attr.value, 0xFF01);
  }
  {
    auto attr = *BgpAttrCommunityC::createBgpAttrCommunity("no-advertise");
    EXPECT_EQ(attr.asn, 0xFFFF);
    EXPECT_EQ(attr.value, 0xFF02);
  }
  {
    auto attr =
        *BgpAttrCommunityC::createBgpAttrCommunity("no-export-subconfed");
    EXPECT_EQ(attr.asn, 0xFFFF);
    EXPECT_EQ(attr.value, 0xFF03);
  }
  {
    auto attr = *BgpAttrCommunityC::createBgpAttrCommunity("local-as");
    EXPECT_EQ(attr.asn, 0xFFFF);
    EXPECT_EQ(attr.value, 0xFF03);
  }
  {
    auto attr = *BgpAttrCommunityC::createBgpAttrCommunity("12345:6789");
    EXPECT_EQ(attr.asn, 12345);
    EXPECT_EQ(attr.value, 6789);
  }
  {
    auto attr = *BgpAttrCommunityC::createBgpAttrCommunity("305397765");
    EXPECT_EQ(attr.asn, 0x1234);
    EXPECT_EQ(attr.value, 0x0005);
  }
  // Negative cases
  {
    auto attr =
        BgpAttrCommunityC::createBgpAttrCommunity("non-a-well-known-community");
    EXPECT_EQ(attr, std::nullopt);
  }
  {
    auto attr = BgpAttrCommunityC::createBgpAttrCommunity("11111111111111111");
    EXPECT_EQ(attr, std::nullopt);
  }
  {
    auto attr = BgpAttrCommunityC::createBgpAttrCommunity("12:34:45");
    EXPECT_EQ(attr, std::nullopt);
  }
  {
    auto attr = BgpAttrCommunityC::createBgpAttrCommunity("$34:45");
    EXPECT_EQ(attr, std::nullopt);
  }
  {
    auto attr = BgpAttrCommunityC::createBgpAttrCommunity(":34:45");
    EXPECT_EQ(attr, std::nullopt);
  }
  {
    auto attr = BgpAttrCommunityC::createBgpAttrCommunity("34%45");
    EXPECT_EQ(attr, std::nullopt);
  }
  {
    auto attr = BgpAttrCommunityC::createBgpAttrCommunity("99999:99999");
    EXPECT_EQ(attr, std::nullopt);
  }
}

TEST(BgpAttrCommunityCTest, ToThrift) {
  auto commNative = BgpAttrCommunityC{123, 456};
  auto commThrift = commNative.toThrift();
  EXPECT_EQ(commThrift.asn(), 123);
  EXPECT_EQ(commThrift.value(), 456);
}

TEST(BgpExtendedCommunity, BgpExtCommunityTest) {
  {
    // non-transitive AsSpecificExt type with high octet 0x40
    BgpAttrExtCommunityC asComm(0x40011234, 0x22334455);
    BgpExtCommunityAsSpecificExtTypeC* comm =
        dynamic_cast<BgpExtCommunityAsSpecificExtTypeC*>(asComm.attr.get());
    EXPECT_NE(comm, nullptr);
    EXPECT_EQ(comm->getType(), 0x40);
    EXPECT_EQ(comm->getSubType(), 0x01);
    EXPECT_EQ(comm->getAsn(), 0x1234);
    EXPECT_EQ(comm->getValue(), 0x22334455);
    EXPECT_EQ(
        asComm.getRawValueInWords(),
        (std::make_pair<uint32_t, uint32_t>(0x40011234, 0x22334455)));
    EXPECT_FALSE(asComm.isTransitive());
    EXPECT_FALSE(asComm.isRouteOrigin());
    EXPECT_FALSE(asComm.isRouteTarget());
    EXPECT_FALSE(asComm.isNonTransitiveLinkBandwidthCommunity());
    EXPECT_EQ(asComm.str(), "[AsSpecificExtType] 64:1:4660:573785173");
  }
  {
    // test transitive route origin IPV4 Specific bgp extended community type
    BgpAttrExtCommunityC asComm(0x01030A0A, 0x01011234);
    BgpExtCommunityIPv4SpecificExtTypeC* comm =
        dynamic_cast<BgpExtCommunityIPv4SpecificExtTypeC*>(asComm.attr.get());
    EXPECT_NE(comm, nullptr);
    EXPECT_EQ(comm->getType(), 0x01);
    EXPECT_EQ(comm->getSubType(), 0x03);
    EXPECT_EQ(comm->getIPv4().str(), "10.10.1.1");
    EXPECT_EQ(comm->getValue(), 0x1234);
    EXPECT_TRUE(asComm.isTransitive());
    EXPECT_TRUE(asComm.isRouteOrigin());
    EXPECT_FALSE(asComm.isRouteTarget());
    EXPECT_FALSE(asComm.isNonTransitiveLinkBandwidthCommunity());
    EXPECT_EQ(asComm.str(), "[IPv4SpecificExtType] 1:3:10.10.1.1:4660");
  }
  {
    // test transitive route target IPV4 Specific bgp extended community type
    BgpAttrExtCommunityC asComm(0x01020A0A, 0x01011234);
    BgpExtCommunityIPv4SpecificExtTypeC* comm =
        dynamic_cast<BgpExtCommunityIPv4SpecificExtTypeC*>(asComm.attr.get());
    EXPECT_NE(comm, nullptr);
    EXPECT_EQ(comm->getType(), 0x01);
    EXPECT_EQ(comm->getSubType(), 0x02);
    EXPECT_EQ(comm->getIPv4().str(), "10.10.1.1");
    EXPECT_EQ(comm->getValue(), 0x1234);
    EXPECT_TRUE(asComm.isTransitive());
    EXPECT_FALSE(asComm.isRouteOrigin());
    EXPECT_TRUE(asComm.isRouteTarget());
    EXPECT_FALSE(asComm.isNonTransitiveLinkBandwidthCommunity());
    EXPECT_EQ(asComm.str(), "[IPv4SpecificExtType] 1:2:10.10.1.1:4660");
  }
  {
    /*
     * test transitive route target bgp extended community type
     * with high octet in 0x00, 0x01, 0x02 group
     */
    for (const auto& highOctet : {0x00, 0x01, 0x02}) {
      auto highValue = (highOctet << 24) + 0x020A0A;
      BgpAttrExtCommunityC asComm(highValue, 0x01011234);
      auto comm = asComm.attr.get();
      EXPECT_NE(comm, nullptr);
      EXPECT_EQ(comm->getType(), highOctet);
      EXPECT_TRUE(asComm.isTransitive());
      EXPECT_FALSE(asComm.isRouteOrigin());
      EXPECT_TRUE(asComm.isRouteTarget());
    }
  }
  {
    /*
     * test transitive route orign bgp extended community type
     * with high octet in 0x00, 0x01, 0x02 group
     */
    for (const auto& highOctet : {0x00, 0x01, 0x02}) {
      auto highValue = (highOctet << 24) + 0x030A0A;
      BgpAttrExtCommunityC asComm(highValue, 0x01011234);
      auto comm = asComm.attr.get();
      EXPECT_NE(comm, nullptr);
      EXPECT_EQ(comm->getType(), highOctet);
      EXPECT_TRUE(asComm.isTransitive());
      EXPECT_TRUE(asComm.isRouteOrigin());
      EXPECT_FALSE(asComm.isRouteTarget());
    }
  }
  {
    /*
     * Add a community with link-bw value as 30000000 Bytes/sec
     * which is 0x4be4e1c0 (IEEE binary32 representation).
     */
    BgpAttrExtCommunityC lbwComm(0x40041434, 0x4be4e1c0);
    BgpExtCommunityLinkBandWidthTypeC* comm =
        dynamic_cast<BgpExtCommunityLinkBandWidthTypeC*>(lbwComm.attr.get());
    EXPECT_NE(comm, nullptr);
    EXPECT_FALSE(lbwComm.isTransitive());
    EXPECT_FALSE(lbwComm.isRouteOrigin());
    EXPECT_FALSE(lbwComm.isRouteTarget());
    EXPECT_TRUE(lbwComm.isNonTransitiveLinkBandwidthCommunity());
    EXPECT_EQ(comm->getLBW(), 30000000);
  }
}

TEST(BgpMessageSerializer, BgpOpenMessageBackwardCompatibilityTest) {
  // this test did not contain any add path related information
  uint8_t msg[] = {
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x3d,   // Length of BGP PDU
    0x01,         // Bgp message type (Notification)
      0x04,       // BGP Version
      0x80, 0xa6, // ASN
      0x00, 0x18, // Hold Time (24 seconds)
      0x01, 0x02, 0x03, 0x04, // BGP ID
      0x20, // Optional Param Length
      // Params
      0x02, // Capabilities
      0x1e, // Length
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
      // clang-format on
  };
  BgpOpenMsg bgpOpenMsg;
  *bgpOpenMsg.version() = 4;
  *bgpOpenMsg.asn() = 0x80a6;
  *bgpOpenMsg.holdTime() = 0x18;
  *bgpOpenMsg.bgpID() = 0x01020304;

  auto& capa = *bgpOpenMsg.capabilities();
  *capa.mpExtV4Unicast() = true;
  *capa.mpExtV6Unicast() = false;
  *capa.mpExtV4LU() = false;
  *capa.mpExtV6LU() = true;
  *capa.as4byte() = true;
  *capa.asn() = 0x1020304;
  *capa.gracefulRestart() = true;
  *capa.isRestarting() = true;
  *capa.restartTime() = 257;

  auto& grCapa = *capa.grCapabilities();
  grCapa.resize(2);
  *grCapa[0].afi() = BgpUpdateAfi::AFI_IPv4;
  *grCapa[0].safi() = BgpUpdateSafi::SAFI_UNICAST;
  *grCapa[0].forwardingState() = true;
  *grCapa[1].afi() = BgpUpdateAfi::AFI_IPv6;
  *grCapa[1].safi() = BgpUpdateSafi::SAFI_LABELED_UNICAST;
  *grCapa[1].forwardingState() = false;

  auto iobuf = BgpMessageSerializer::serializeBgpOpenMsg(bgpOpenMsg);
  EXPECT_EQ(iobuf->length(), sizeof(msg));
  EXPECT_EQ(0, memcmp(iobuf->data(), msg, sizeof(msg)));
}

// RFC 8538 GR Notification (N) bit
TEST(BgpMessageSerializer, BgpOpenMessageGrNotificationBitTest) {
  uint8_t msg[] = {
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
  BgpOpenMsg bgpOpenMsg;
  *bgpOpenMsg.version() = 4;
  *bgpOpenMsg.asn() = 0x80a6;
  *bgpOpenMsg.holdTime() = 0x18;
  *bgpOpenMsg.bgpID() = 0x01020304;

  auto& capa = *bgpOpenMsg.capabilities();
  *capa.gracefulRestart() = true;
  *capa.isRestarting() = false;
  *capa.grNotification() = true;
  *capa.restartTime() = 257;

  auto iobuf = BgpMessageSerializer::serializeBgpOpenMsg(bgpOpenMsg);
  EXPECT_EQ(iobuf->length(), sizeof(msg));
  EXPECT_EQ(0, memcmp(iobuf->data(), msg, sizeof(msg)));
}

// RFC 5549 Sec. 4
TEST(BgpMessageSerializer, BgpOpenMessageWithExtNHEncodingCapaTest) {
  std::vector<uint8_t> msg = {
      // clang-format off
    // Marker
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    // Length
    0x00, 0x2d,
    // Type
    0x01,
    // Version
    0x04,
    // ASN
    0x80, 0xa6,
    // Hold Time
    0x00, 0x18,
    // BGP Identifier
    0x01, 0x02, 0x03, 0x04,
    // Opt Parm Len
    0x10,
    // Parm. Type
    0x02,
    // Parm. Length
    0x0e,
    // Capability Code
    0x05,
    // Capability Length
    0x0c,
    /*
     * Parameter Value
     * First tuple <1,1,2>
     */
    0x00, 0x01, 0x00, 0x01, 0x00, 0x02,
    // Second tuple <1,4,2>
    0x00, 0x01, 0x00, 0x04, 0x00, 0x02,
      // clang-format on
  };

  BgpOpenMsg bgpOpenMsg;
  *bgpOpenMsg.version() = 4;
  *bgpOpenMsg.asn() = 0x80a6;
  *bgpOpenMsg.holdTime() = 0x18;
  *bgpOpenMsg.bgpID() = 0x01020304;

  auto& capa = *bgpOpenMsg.capabilities();
  auto& extNHEncodingCapa = *capa.extNHEncodingCapabilities();
  extNHEncodingCapa.resize(2);

  // tuple <1,1,2>
  *extNHEncodingCapa[0].nlriAfi() = BgpUpdateAfi::AFI_IPv4;
  *extNHEncodingCapa[0].nlriSafi() = BgpUpdateSafi::SAFI_UNICAST;
  *extNHEncodingCapa[0].nhAfi() = BgpUpdateAfi::AFI_IPv6;

  // tuple <1,4,2>
  *extNHEncodingCapa[1].nlriAfi() = BgpUpdateAfi::AFI_IPv4;
  *extNHEncodingCapa[1].nlriSafi() = BgpUpdateSafi::SAFI_LABELED_UNICAST;
  *extNHEncodingCapa[1].nhAfi() = BgpUpdateAfi::AFI_IPv6;

  auto iobuf = BgpMessageSerializer::serializeBgpOpenMsg(bgpOpenMsg);
  EXPECT_EQ(iobuf->length(), msg.size());
  EXPECT_EQ(0, memcmp(iobuf->data(), &msg[0], msg.size()));
}

// RFC 2918 Route Refresh Capability
TEST(BgpMessageSerializer, BgpOpenMessageWithRouteRefreshCapaTest) {
  std::vector<uint8_t> msg = {
      // clang-format off
    // Marker
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    // Length
    0x00, 0x21,
    // Type
    0x01,
    // Version
    0x04,
    // ASN
    0x80, 0xa6,
    // Hold Time
    0x00, 0x18,
    // BGP Identifier
    0x01, 0x02, 0x03, 0x04,
    // Opt Parm Len
    0x04,
    // Parm. Type
    0x02,
    // Parm. Length
    0x02,
    // Capability Code
    0x02,
    // Capability Length
    0x00,
      // clang-format on
  };

  BgpOpenMsg bgpOpenMsg;
  bgpOpenMsg.version() = 4;
  bgpOpenMsg.asn() = 0x80a6;
  bgpOpenMsg.holdTime() = 0x18;
  bgpOpenMsg.bgpID() = 0x01020304;

  auto& capa = *bgpOpenMsg.capabilities();
  capa.routeRefresh() = true;
  auto iobuf = BgpMessageSerializer::serializeBgpOpenMsg(bgpOpenMsg);
  EXPECT_EQ(iobuf->length(), msg.size());
  EXPECT_EQ(0, memcmp(iobuf->data(), &msg[0], msg.size()));
}

// RFC 7313 Enhanced Route Refresh Capability
TEST(BgpMessageSerializer, BgpOpenMessageWithEnhancedRouteRefreshCapaTest) {
  std::vector<uint8_t> msg = {
      // clang-format off
    // Marker
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    // Length
    0x00, 0x21,
    // Type
    0x01,
    // Version
    0x04,
    // ASN
    0x80, 0xa6,
    // Hold Time
    0x00, 0x18,
    // BGP Identifier
    0x01, 0x02, 0x03, 0x04,
    // Opt Parm Len
    0x04,
    // Parm. Type
    0x02,
    // Parm. Length
    0x02,
    // Capability Code
    0x46,
    // Capability Length
    0x00,
      // clang-format on
  };

  BgpOpenMsg bgpOpenMsg;
  bgpOpenMsg.version() = 4;
  bgpOpenMsg.asn() = 0x80a6;
  bgpOpenMsg.holdTime() = 0x18;
  bgpOpenMsg.bgpID() = 0x01020304;

  auto& capa = *bgpOpenMsg.capabilities();
  capa.enhancedRouteRefresh() = true;
  auto iobuf = BgpMessageSerializer::serializeBgpOpenMsg(bgpOpenMsg);
  EXPECT_EQ(iobuf->length(), msg.size());
  EXPECT_EQ(0, memcmp(iobuf->data(), &msg[0], msg.size()));
}

TEST(BgpMessageSerializer, BgpOpenMessageTestNew) {
  std::vector<uint8_t> msg = {
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    // 0x00, 0x29,   // Length of BGP PDU
    0x00, 0x4b,   // Length of BGP PDU
    0x01,         // Bgp message type (Notification)
      0x04,       // BGP Version
      0x80, 0xa6, // ASN
      0x00, 0x18, // Hold Time (24 seconds)
      0x01, 0x02, 0x03, 0x04, // BGP ID
      0x2e, // Optional Param Length
      // Params
      0x02, // Capabilities
      0x2c, // Length
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
        0x46, 0x00, // Enhanced Route Refresh, Length-0
      // clang-format on
  };
  BgpOpenMsg bgpOpenMsg;
  *bgpOpenMsg.version() = 4;
  *bgpOpenMsg.asn() = 0x80a6;
  *bgpOpenMsg.holdTime() = 0x18;
  *bgpOpenMsg.bgpID() = 0x01020304;

  auto& capa = *bgpOpenMsg.capabilities();
  *capa.mpExtV4Unicast() = true;
  *capa.mpExtV6Unicast() = false;
  *capa.mpExtV4LU() = false;
  *capa.mpExtV6LU() = true;
  *capa.as4byte() = true;
  *capa.asn() = 0x1020304;
  *capa.gracefulRestart() = true;
  *capa.isRestarting() = true;
  *capa.restartTime() = 257;

  auto& grCapa = *capa.grCapabilities();
  grCapa.resize(2);
  *grCapa[0].afi() = BgpUpdateAfi::AFI_IPv4;
  *grCapa[0].safi() = BgpUpdateSafi::SAFI_UNICAST;
  *grCapa[0].forwardingState() = true;
  *grCapa[1].afi() = BgpUpdateAfi::AFI_IPv6;
  *grCapa[1].safi() = BgpUpdateSafi::SAFI_LABELED_UNICAST;
  *grCapa[1].forwardingState() = false;

  auto& addPath = *capa.addPathCapabilities();
  addPath.resize(2);
  *addPath[0].afi() = BgpUpdateAfi::AFI_IPv4;
  *addPath[0].safi() = BgpUpdateSafi::SAFI_UNICAST;
  *addPath[0].sor() = BgpAddPathSendRec::RECEIVE;
  *addPath[1].afi() = BgpUpdateAfi::AFI_IPv6;
  *addPath[1].safi() = BgpUpdateSafi::SAFI_LABELED_UNICAST;
  *addPath[1].sor() = BgpAddPathSendRec::SEND;

  capa.routeRefresh() = true;
  capa.enhancedRouteRefresh() = true;

  auto iobuf = BgpMessageSerializer::serializeBgpOpenMsg(bgpOpenMsg);
  EXPECT_EQ(iobuf->length(), msg.size());
  EXPECT_EQ(0, memcmp(iobuf->data(), &msg[0], msg.size()));
}

TEST(BgpMessageSerializer, BgpKeepAliveTest) {
  uint8_t msg[] = {
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x13, // Length of BGP PDU
    0x04, // Bgp message type (KeepAlive)
      // clang-format on
  };
  auto iobuf = BgpMessageSerializer::serializeBgpKeepAlive();
  EXPECT_EQ(iobuf->length(), sizeof(msg));
  EXPECT_EQ(0, memcmp(iobuf->data(), msg, sizeof(msg)));
}

TEST(BgpMessageSerializer, BgpNotificationTest) {
  uint8_t msg[] = {
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x17,   // Length of BGP PDU
    0x03,         // Bgp message type (Notification)
      0x06,       // Cease Error
      0x05,       // Cease Connection rejected
      0x01, 0x02, // Data
      // clang-format on
  };
  BgpNotification notif;
  *notif.errCode() = BgpNotifErrCode::BN_CEASE;
  *notif.errSubCode() =
      (uint8_t)BgpNotifCeaseErrSubCode::BN_CEASE_CONN_REJECTED;
  *notif.data() = std::string({0x01, 0x02});
  auto iobuf = BgpMessageSerializer::serializeBgpNotification(notif);
  EXPECT_EQ(iobuf->length(), sizeof(msg));
  EXPECT_EQ(0, memcmp(iobuf->data(), msg, sizeof(msg)));
}

/**
 * Create Route Refresh message with all message sub types
 * and verify the serialized message.
 */
TEST(BgpMessageSerializer, BgpRouteRefreshTest) {
  uint8_t msg[] = {
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
  // Check Route Refresh request message subtype
  BgpRouteRefresh rr;
  rr.afi() = BgpUpdateAfi::AFI_IPv4;
  rr.msgSubType() = BgpRouteRefreshMessageSubtype::ROUTE_REFRESH_REQUEST;
  rr.safi() = BgpUpdateSafi::SAFI_UNICAST;
  auto iobuf = BgpMessageSerializer::serializeBgpRouteRefresh(rr);
  EXPECT_EQ(iobuf->length(), sizeof(msg));
  EXPECT_EQ(0, memcmp(iobuf->data(), msg, sizeof(msg)));
  // Check BoRR message subtype
  msg[21] = 0x01;
  rr.msgSubType() = BgpRouteRefreshMessageSubtype::BEGINNING_OF_ROUTE_REFRESH;
  iobuf = BgpMessageSerializer::serializeBgpRouteRefresh(rr);
  EXPECT_EQ(iobuf->length(), sizeof(msg));
  EXPECT_EQ(0, memcmp(iobuf->data(), msg, sizeof(msg)));
  // Check EoRR message subtype
  msg[21] = 0x02;
  rr.msgSubType() = BgpRouteRefreshMessageSubtype::END_OF_ROUTE_REFRESH;
  iobuf = BgpMessageSerializer::serializeBgpRouteRefresh(rr);
  EXPECT_EQ(iobuf->length(), sizeof(msg));
  EXPECT_EQ(0, memcmp(iobuf->data(), msg, sizeof(msg)));
}

TEST(BgpMessageSerializer, BgpUpdateV4EORTest) {
  uint8_t msg[] = {
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x17, // length of bgp pdu
    0x02, // bgp message type (update)
    0x00, 0x00, // withdrawn routes length
    0x00, 0x00, // path attributes length
    // no path attributes
    // no network layer reachability information
      // clang-format on
  };
  auto iobuf = BgpMessageSerializer::serializeBgpEndOfRib(
      BgpUpdateAfi::AFI_IPv4, BgpUpdateSafi::SAFI_UNICAST);
  EXPECT_EQ(iobuf->length(), sizeof(msg));
  EXPECT_EQ(0, memcmp(iobuf->data(), msg, sizeof(msg)));
}

TEST(BgpMessageSerializer, BgpUpdateV6EORTest) {
  uint8_t msg[] = {
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x1e, // Length of BGP PDU
    0x02, // Bgp message type (Update)
    0x00, 0x00, // Withdrawn routes length
    0x00, 0x07, // Path attributes length
    // Path Attributes
      0x90, 0x0f, 0x00, 0x03, /**** MP_UNREACH_NLRI (3 bytes) ****/
        0x00, 0x02, // Address family identifier
        0x01,       // Subsequent address family identifier
        // No MP-v6 withdrawn routes
    // No Network Layer Reachability information
      // clang-format on
  };
  auto iobuf = BgpMessageSerializer::serializeBgpEndOfRib(
      BgpUpdateAfi::AFI_IPv6, BgpUpdateSafi::SAFI_UNICAST);
  EXPECT_EQ(iobuf->length(), sizeof(msg));
  EXPECT_EQ(0, memcmp(iobuf->data(), msg, sizeof(msg)));
}

TEST(BgpMessageSerializer, BgpUpdate2V4UnicastTest) {
  std::array<uint8_t, 116> msg{{
      // clang-format off
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0x00, 0x74, // Length of BGP PDU
      0x02, // Bgp message type (Update)
      0x00, 0x00, // Withdrawn routes length
      // Path Attributes
      0x00, 0x58, // Path attributes length (38 bytes)
        0x40, 0x01, 0x01, // ORIGIN
          0x00, // IGP
        0x50, 0x02, 0x00, 0x06, // AS_PATH
          0x02, // Segment type: AS_SEQUENCE
          0x01, // Segment count: 1
          0x00, 0x00, 0x80, 0xa6, // asn: 32934
        0x40, 0x03, 0x04, // NEXT_HOP
          0x01, 0x02, 0x03, 0x04, // "1.2.3.4"
        0x80, 0x04, 0x04, // MULTI_EXIT_DESC
          0x00, 0x00, 0x00, 0x20, // 32
        0x40, 0x05, 0x04, // LOCAL_PREF
          0x00, 0x00, 0x00, 0x64, // 100
        0x40, 0x06, 0x00, // ATOMIC_AGGREGATOR
        0xc0, 0x07, 0x08, // AGGREGATOR
          0x00, 0x00, 0x12, 0x34, // asn: 4660
          0x03, 0x04, 0x05, 0x06, // ip: "3.4.5.6"
        0xd0, 0x08, 0x00, 0x04, // COMMUNITIES
          0xff, 0xfa, // asn: 65530
          0x3d, 0xb8, // value: 15800
        0x80, 0x09, 0x04, // ORIGINATOR_ID
          0x00, 0x00, 0x07, 0x86, // ip: "0.0.7.134"
        0x90, 0x0a, 0x00, 0x08, // CLUSTER_LIST
          0x00, 0x00, 0x01, 0x10, // ip: "0.0.1.16"
          0x00, 0x00, 0x07, 0x86, // ip: "0.0.7.134"
        0xd0, 0x10, 0x00, 0x08, // EXTENDED_COMMUNITIES
          0x00, 0x02, 0x27, 0x2a, // Community-1 first 4 bytes
          0x00, 0x00, 0x23, 0x2f, // Community-2 next 4 bytes
      // Network Layer Reachability information
      0x20, 0x06, 0x05, 0x04, 0x03, // Update prefix, "6.5.4.3/32"
      // clang-format on
  }};
  BgpUpdate2 update;
  update.v4Announced()->push_back(
      network::toIPPrefix(folly::IPAddress::createNetwork("6.5.4.3/32")));
  *update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment;
  segment.asSequence()->push_back(32934);
  update.attrs()->asPath()->push_back(segment);
  *update.v4Nexthop() = network::toBinaryAddress(folly::IPAddress("1.2.3.4"));
  update.attrs()->med() = 32;
  update.attrs()->isMedSet() = true;
  update.attrs()->localPref() = 100;
  *update.attrs()->atomicAggregate() = true;
  *update.attrs()->aggregator()->asn() = 4660;
  *update.attrs()->aggregator()->ip() = "3.4.5.6";
  BgpAttrCommunity community;
  *community.asn() = 65530;
  *community.value() = 15800;
  update.attrs()->communities()->push_back(community);
  *update.attrs()->originatorId() = 0x86070000; // ip: "0.0.7.134""
  update.attrs()->clusterList()->push_back(0x10010000); // ip:
                                                        // "0.0.1.16"
  update.attrs()->clusterList()->push_back(0x86070000); // ip:
                                                        // "0.0.7.134"
  BgpAttrExtCommunity extCommunity;
  *extCommunity.firstWord() = 0x2272a;
  *extCommunity.secondWord() = 0x232f;
  update.attrs()->extCommunities()->push_back(extCommunity);

  auto iobuf = BgpMessageSerializer::serializeBgpUpdate2(update, true);
  EXPECT_EQ(1, iobuf->countChainElements());
  EXPECT_EQ(msg.size(), iobuf->computeChainDataLength());
  EXPECT_TRUE(std::equal(msg.begin(), msg.end(), iobuf->data()));
}

TEST(BgpMessageSerializer, BgpUpdate2V4AddPathUnicastTest) {
  std::vector<uint8_t> msg{
      // clang-format off
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0x00, 0x78, // Length of BGP PDU
      0x02, // Bgp message type (Update)
      0x00, 0x00, // Withdrawn routes length
      // Path Attributes
      0x00, 0x58, // Path attributes length (38 bytes)
        0x40, 0x01, 0x01, // ORIGIN
          0x00, // IGP
        0x50, 0x02, 0x00, 0x06, // AS_PATH
          0x02, // Segment type: AS_SEQUENCE
          0x01, // Segment count: 1
          0x00, 0x00, 0x80, 0xa6, // asn: 32934
        0x40, 0x03, 0x04, // NEXT_HOP
          0x01, 0x02, 0x03, 0x04, // "1.2.3.4"
        0x80, 0x04, 0x04, // MULTI_EXIT_DESC
          0x00, 0x00, 0x00, 0x20, // 32
        0x40, 0x05, 0x04, // LOCAL_PREF
          0x00, 0x00, 0x00, 0x64, // 100
        0x40, 0x06, 0x00, // ATOMIC_AGGREGATOR
        0xc0, 0x07, 0x08, // AGGREGATOR
          0x00, 0x00, 0x12, 0x34, // asn: 4660
          0x03, 0x04, 0x05, 0x06, // ip: "3.4.5.6"
        0xd0, 0x08, 0x00, 0x04, // COMMUNITIES
          0xff, 0xfa, // asn: 65530
          0x3d, 0xb8, // value: 15800
        0x80, 0x09, 0x04, // ORIGINATOR_ID
          0x00, 0x00, 0x07, 0x86, // ip: "0.0.7.134"
        0x90, 0x0a, 0x00, 0x08, // CLUSTER_LIST
          0x00, 0x00, 0x01, 0x10, // ip: "0.0.1.16"
          0x00, 0x00, 0x07, 0x86, // ip: "0.0.7.134"
        0xd0, 0x10, 0x00, 0x08, // EXTENDED_COMMUNITIES
          0x00, 0x02, 0x27, 0x2a, // Community-1 first 4 bytes
          0x00, 0x00, 0x23, 0x2f, // Community-2 next 4 bytes
      // Network Layer Reachability information
      0x00, 0x00, 0x00, 0x01, //path_id 1
      0x20, 0x06, 0x05, 0x04, 0x03, // Update prefix, "6.5.4.3/32"
      // clang-format on
  };
  BgpUpdate2 update;
  RiggedIPPrefix ipPrefix;
  ipPrefix.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("6.5.4.3/32"));
  ipPrefix.labels() = {};
  ipPrefix.pathId() = 1;
  update.v4Announced2()->push_back(std::move(ipPrefix));
  *update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment;
  segment.asSequence()->push_back(32934);
  update.attrs()->asPath()->push_back(segment);
  *update.v4Nexthop() = network::toBinaryAddress(folly::IPAddress("1.2.3.4"));
  update.attrs()->med() = 32;
  update.attrs()->isMedSet() = true;
  update.attrs()->localPref() = 100;
  *update.attrs()->atomicAggregate() = true;
  *update.attrs()->aggregator()->asn() = 4660;
  *update.attrs()->aggregator()->ip() = "3.4.5.6";
  BgpAttrCommunity community;
  *community.asn() = 65530;
  *community.value() = 15800;
  update.attrs()->communities()->push_back(community);
  *update.attrs()->originatorId() = 0x86070000; // ip: "0.0.7.134""
  update.attrs()->clusterList()->push_back(0x10010000); // ip:
                                                        // "0.0.1.16"
  update.attrs()->clusterList()->push_back(0x86070000); // ip:
                                                        // "0.0.7.134"
  BgpAttrExtCommunity extCommunity;
  *extCommunity.firstWord() = 0x2272a;
  *extCommunity.secondWord() = 0x232f;
  update.attrs()->extCommunities()->push_back(extCommunity);

  auto iobuf = BgpMessageSerializer::serializeBgpUpdate2(update, true);

  EXPECT_EQ(1, iobuf->countChainElements());
  EXPECT_EQ(iobuf->computeChainDataLength(), msg.size());
  EXPECT_EQ(0, memcmp(iobuf->data(), &msg[0], msg.size()));
}

TEST(BgpMessageSerializer, BgpUpdate2WithdrawV4LUTest) {
  std::array<uint8_t, 40> msg{{
      // clang-format off
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0x00, 0x28, // Length of BGP PDU (40 bytes)
      0x02, // Bgp message type (Update)
      0x00, 0x00, // Withdrawn routes length
        // Path Attributes
      0x00, 0x11, // Path attributes length (17 bytes)
        0x90, 0x0f, 0x00, 0x0d, /**** MP_UNREACH_NLRI (13 bytes) ****/
          0x00, 0x01, // Address family identifier
          0x04, // Subsequent address family identifier
          0x48, // length (48 + 24  bits)
            0x00, 0x00, 0x80, // Label: 8, Bottom of Stack: false
            0x00, 0x00, 0x91, // Label: 9, Bottom of Stack: true
            0x04, 0x05, 0x06, // Prefix "4.5.6.0/24"
      // clang-format on
  }};
  BgpUpdate2 update;
  *update.mpWithdrawn()->afi() = BgpUpdateAfi::AFI_IPv4;
  *update.mpWithdrawn()->safi() = BgpUpdateSafi::SAFI_LABELED_UNICAST;
  RiggedIPPrefix prefix;
  *prefix.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("4.5.6.0/24"));
  *prefix.labels() = {8, 9};
  update.mpWithdrawn()->prefixes()->push_back(prefix);

  auto iobuf = BgpMessageSerializer::serializeBgpUpdate2(update, false);
  EXPECT_EQ(1, iobuf->countChainElements());
  EXPECT_EQ(msg.size(), iobuf->computeChainDataLength());
  EXPECT_TRUE(std::equal(msg.begin(), msg.end(), iobuf->data()));
}

TEST(BgpMessageSerializer, BgpUpdate2WithdrawV4LUAddPathTest) {
  std::vector<uint8_t> msg{
      // clang-format off
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
      0x00, 0x2c, // Length of BGP PDU (40 bytes)
      0x02, // Bgp message type (Update)
      0x00, 0x00, // Withdrawn routes length
        // Path Attributes
      0x00, 0x15, // Path attributes length (17 bytes)
        0x90, 0x0f, 0x00, 0x11, /**** MP_UNREACH_NLRI (13 bytes) ****/
          0x00, 0x01, // Address family identifier
          0x04, // Subsequent address family identifier
          0x00,0x00,0x00,0x01, //path id 1
          0x48, // length (48 + 24  bits)
            0x00, 0x00, 0x80, // Label: 8, Bottom of Stack: false
            0x00, 0x00, 0x91, // Label: 9, Bottom of Stack: true
            0x04, 0x05, 0x06, // Prefix "4.5.6.0/24"
      // clang-format on
  };
  BgpUpdate2 update;
  *update.mpWithdrawn()->afi() = BgpUpdateAfi::AFI_IPv4;
  *update.mpWithdrawn()->safi() = BgpUpdateSafi::SAFI_LABELED_UNICAST;
  RiggedIPPrefix prefix;
  *prefix.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("4.5.6.0/24"));
  *prefix.labels() = {8, 9};
  prefix.pathId() = 1;
  update.mpWithdrawn()->prefixes()->push_back(prefix);

  auto iobuf = BgpMessageSerializer::serializeBgpUpdate2(update, false);

  EXPECT_EQ(1, iobuf->countChainElements());
  EXPECT_EQ(iobuf->computeChainDataLength(), msg.size());
  EXPECT_EQ(0, memcmp(iobuf->data(), &msg[0], msg.size()));
}

TEST(BgpMessageSerializer, BgpUpdate2V6UnicastTest) {
  std::array<uint8_t, 135> msg{{
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x87, // Length of BGP PDU
    0x02, // Bgp message type (Update)
    0x00, 0x00, // Withdrawn routes length
    // Path Attributes
    0x00, 0x70, // Path attributes length (112 bytes)
      0x40, 0x01, 0x01, // ORIGIN
        0x01, // EGP
      0x50, 0x02, 0x00, 0x18, // AS_PATH
        0x01, // Segment type: AS_SET
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa6, // asn: 32934
        0x02, // Segment type: AS_SEQUENCE
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa7, // asn: 32935
        0x03, // Segment type: AS_CONFED_SEQUENCE
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa8, // asn: 32936
        0x04, // Segment type: AS_CONFED_SET
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa9, // asn: 32937
      0x80, 0x04, 0x04,  // MULTI_EXIT_DESC
        0x00, 0x00, 0x00, 0x20, // 32
      0x40, 0x05, 0x04, // LOCAL_PREF
        0x00, 0x00, 0x00, 0x64, // 100
      0xd0, 0x08, 0x00, 0x04, // COMMUNITIES
        0xff, 0xfa, // asn: 65530
        0x3d, 0xb8, // value: 15800
      0x80, 0x09, 0x04, // ORIGINATOR_ID
        0x00, 0x00, 0x07, 0x86, // ip: "0.0.7.134"
      0x90, 0x0e, 0x00, 0x2f, /**** MP_REACH_NLRI (38 bytes) ****/
        0x00, 0x02, // Address family identifier
        0x01,       // Subsequent address family identifier
        0x10,       // Length of nexthop
          0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // Nexthop, 16 bytes
        0x00, // Reserved
        0x7a, // Prefix: length (124 bit)
          0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00,
        0x40, // Prefix: length (64 bit)
          0xde, 0xad, 0xbe, 0xef, 0xfa, 0xce, 0xb0, 0x0c,
      // clang-format on
  }};
  BgpUpdate2 update;
  *update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_EGP;
  BgpAttrAsPathSegment segment, segment2, segment3, segment4;
  segment.asSet()->insert(32934);
  update.attrs()->asPath()->push_back(segment);
  segment2.asSequence()->push_back(32935);
  update.attrs()->asPath()->push_back(segment2);
  segment3.asConfedSequence()->push_back(32936);
  update.attrs()->asPath()->push_back(segment3);
  segment4.asConfedSet()->insert(32937);
  update.attrs()->asPath()->push_back(segment4);
  update.attrs()->med() = 32;
  update.attrs()->isMedSet() = true;
  update.attrs()->localPref() = 100;
  BgpAttrCommunity community;
  *community.asn() = 65530;
  *community.value() = 15800;
  update.attrs()->communities()->push_back(community);
  *update.attrs()->originatorId() = 0x86070000; // ip: "0.0.7.134"
  *update.mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update.mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  *update.mpAnnounced()->nexthop() =
      network::toBinaryAddress(folly::IPAddress("fd00::1"));
  RiggedIPPrefix prefix1, prefix2;
  *prefix1.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("fd00::3000/122"));
  update.mpAnnounced()->prefixes()->push_back(prefix1);
  *prefix2.prefix() = network::toIPPrefix(
      folly::IPAddress::createNetwork("dead:beef:face:b00c::/64"));
  update.mpAnnounced()->prefixes()->push_back(prefix2);

  auto serMsg = BgpMessageSerializer::serializeBgpUpdate2(update, true);
  EXPECT_EQ(1, serMsg->countChainElements());
  EXPECT_EQ(msg.size(), serMsg->computeChainDataLength());
  EXPECT_TRUE(std::equal(msg.begin(), msg.end(), serMsg->data()));
}

TEST(BgpMessageSerializer, BgpUpdate2V4UnicastInMpFormatTest) {
  std::array<uint8_t, 84> msg{{
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x54, // Length of BGP PDU (82 bytes)
    0x02, // Bgp message type (Update)
    0x00, 0x00, // Withdrawn routes length
    // Path Attributes
    0x00, 0x3d, // Path attributes length (61 bytes)
      0x40, 0x01, 0x01, // ORIGIN
        0x01, // EGP
      0x50, 0x02, 0x00, 0x06, // AS_PATH
        0x01, // Segment type: AS_SET
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa6, // asn: 32934
      0x80, 0x04, 0x04,  // MULTI_EXIT_DESC
        0x00, 0x00, 0x00, 0x20, // 32
      0x40, 0x05, 0x04, // LOCAL_PREF
        0x00, 0x00, 0x00, 0x64, // 100
      0xd0, 0x08, 0x00, 0x04, // COMMUNITIES
        0xff, 0xfa, // asn: 65530
        0x3d, 0xb8, // value: 15800
      0x80, 0x09, 0x04, // ORIGINATOR_ID
        0x00, 0x00, 0x07, 0x86, // ip: "0.0.7.134"
      0x90, 0x0e, 0x00, 0x0e, /**** MP_REACH_NLRI (14 bytes) ****/
        0x00, 0x01, // Address family identifier
        0x01,       // Subsequent address family identifier
        0x04,       // Length of nexthop
          0x03, 0x04, 0x05, 0x06, // nexthop: "3.4.5.6"
        0x00, // Reserved
        0x20, 0x06, 0x05, 0x04, 0x03, // Update prefix, "6.5.4.3/32"
      // clang-format on
  }};
  BgpUpdate2 update;
  *update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_EGP;
  BgpAttrAsPathSegment segment;
  segment.asSet()->insert(32934);
  update.attrs()->asPath()->push_back(segment);
  update.attrs()->med() = 32;
  update.attrs()->isMedSet() = true;
  update.attrs()->localPref() = 100;
  BgpAttrCommunity community;
  *community.asn() = 65530;
  *community.value() = 15800;
  update.attrs()->communities()->push_back(community);
  *update.attrs()->originatorId() = 0x86070000; // ip: "0.0.7.134"
  *update.mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv4;
  *update.mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  *update.mpAnnounced()->nexthop() =
      network::toBinaryAddress(folly::IPAddress("3.4.5.6"));
  RiggedIPPrefix prefix1;
  *prefix1.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("6.5.4.3/32"));
  update.mpAnnounced()->prefixes()->push_back(prefix1);

  auto serMsg = BgpMessageSerializer::serializeBgpUpdate2(update, true);
  EXPECT_EQ(1, serMsg->countChainElements());
  EXPECT_EQ(msg.size(), serMsg->computeChainDataLength());
  EXPECT_TRUE(std::equal(msg.begin(), msg.end(), serMsg->data()));
}

TEST(BgpMessageSerializer, BgpUpdate2ExtNhEncodingTest) {
  std::array<uint8_t, 114> msg{{
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x72, // Length of BGP PDU
    0x02, // Bgp message type (Update)
    0x00, 0x00, // Withdrawn routes length
    // Path Attributes
    0x00, 0x5b, // Path attributes length (112 bytes)
      0x40, 0x01, 0x01, // ORIGIN
        0x01, // EGP
      0x50, 0x02, 0x00, 0x18, // AS_PATH
        0x01, // Segment type: AS_SET
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa6, // asn: 32934
        0x02, // Segment type: AS_SEQUENCE
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa7, // asn: 32935
        0x03, // Segment type: AS_CONFED_SEQUENCE
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa8, // asn: 32936
        0x04, // Segment type: AS_CONFED_SET
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa9, // asn: 32937
      0x80, 0x04, 0x04,  // MULTI_EXIT_DESC
        0x00, 0x00, 0x00, 0x20, // 32
      0x40, 0x05, 0x04, // LOCAL_PREF
        0x00, 0x00, 0x00, 0x64, // 100
      0xd0, 0x08, 0x00, 0x04, // COMMUNITIES
        0xff, 0xfa, // asn: 65530
        0x3d, 0xb8, // value: 15800
      0x80, 0x09, 0x04, // ORIGINATOR_ID
        0x00, 0x00, 0x07, 0x86, // ip: "0.0.7.134"
      0x90, 0x0e, 0x00, 0x1a, /**** MP_REACH_NLRI (17 bytes) ****/
        0x00, 0x01, // Address family identifier
        0x01,       // Subsequent address family identifier
        0x10,       // Length of nexthop
          0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // Nexthop, 16 bytes
        0x00, // Reserved
        0x20, 0x06, 0x05, 0x04, 0x03, // Update prefix, "6.5.4.3/32"
      // clang-format on
  }};
  BgpUpdate2 update;
  *update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_EGP;
  BgpAttrAsPathSegment segment, segment2, segment3, segment4;
  segment.asSet()->insert(32934);
  update.attrs()->asPath()->push_back(segment);
  segment2.asSequence()->push_back(32935);
  update.attrs()->asPath()->push_back(segment2);
  segment3.asConfedSequence()->push_back(32936);
  update.attrs()->asPath()->push_back(segment3);
  segment4.asConfedSet()->insert(32937);
  update.attrs()->asPath()->push_back(segment4);
  update.attrs()->med() = 32;
  update.attrs()->isMedSet() = true;
  update.attrs()->localPref() = 100;
  BgpAttrCommunity community;
  *community.asn() = 65530;
  *community.value() = 15800;
  update.attrs()->communities()->push_back(community);
  *update.attrs()->originatorId() = 0x86070000; // ip: "0.0.7.134"
  *update.mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv4;
  *update.mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  *update.mpAnnounced()->nexthop() =
      network::toBinaryAddress(folly::IPAddress("fd00::1"));
  RiggedIPPrefix prefix1, prefix2;
  *prefix1.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("6.5.4.3/32"));
  update.mpAnnounced()->prefixes()->push_back(prefix1);

  auto serMsg = BgpMessageSerializer::serializeBgpUpdate2(
      update, true /*as4byte*/, true /*extNhEncoding*/);
  EXPECT_EQ(1, serMsg->countChainElements());
  EXPECT_EQ(msg.size(), serMsg->computeChainDataLength());
  EXPECT_TRUE(std::equal(msg.begin(), msg.end(), serMsg->data()));

  /*
   * if negotiated capability does not support extended nesthop encoding,
   * throw
   */
  EXPECT_THROW(
      {
        try {
          BgpMessageSerializer::serializeBgpUpdate2(
              update, true /*as4byte*/, false /*extNhEncoding*/);
        } catch (BgpSerializerException& e) {
          EXPECT_EQ(
              BgpSerializerExceptionCode::EXT_NH_ENCODING_NOT_SUPPORTTED,
              e.getCode());
          throw;
        }
      },
      BgpSerializerException);
}

TEST(BgpMessageSerializer, BgpUpdate2V6UnicastAddPathTest) {
  std::vector<uint8_t> msg = {
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x8f, // Length of BGP PDU
    0x02, // Bgp message type (Update)
    0x00, 0x00, // Withdrawn routes length
    // Path Attributes
    0x00, 0x78, // Path attributes length (114 bytes)
      0x40, 0x01, 0x01, // ORIGIN
        0x01, // EGP
      0x50, 0x02, 0x00, 0x18, // AS_PATH
        0x01, // Segment type: AS_SET
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa6, // asn: 32934
        0x02, // Segment type: AS_SEQUENCE
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa7, // asn: 32935
        0x03, // Segment type: AS_CONFED_SEQUENCE
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa8, // asn: 32936
        0x04, // Segment type: AS_CONFED_SET
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa9, // asn: 32937
      0x80, 0x04, 0x04,  // MULTI_EXIT_DESC
        0x00, 0x00, 0x00, 0x20, // 32
      0x40, 0x05, 0x04, // LOCAL_PREF
        0x00, 0x00, 0x00, 0x64, // 100
      0xd0, 0x08, 0x00, 0x04, // COMMUNITIES
        0xff, 0xfa, // asn: 65530
        0x3d, 0xb8, // value: 15800
      0x80, 0x09, 0x04, // ORIGINATOR_ID
        0x00, 0x00, 0x07, 0x86, // ip: "0.0.7.134"
      0x90, 0x0e, 0x00, 0x37, /**** MP_REACH_NLRI (40 bytes) ****/
        0x00, 0x02, // Address family identifier
        0x01,       // Subsequent address family identifier
        0x10,       // Length of nexthop
          0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // Nexthop, 16 bytes
        0x00, // Reserved
        0x00, 0x00, 0x00, 0x01, // path id 1
        0x7a, // Prefix: length (124 bit)
          0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00,
        0x00, 0x00, 0x00, 0x02, // path id 2
        0x40, // Prefix: length (64 bit)
          0xde, 0xad, 0xbe, 0xef, 0xfa, 0xce, 0xb0, 0x0c,
      // clang-format on
  };
  BgpUpdate2 update;
  *update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_EGP;
  BgpAttrAsPathSegment segment, segment2, segment3, segment4;
  segment.asSet()->insert(32934);
  update.attrs()->asPath()->push_back(segment);
  segment2.asSequence()->push_back(32935);
  update.attrs()->asPath()->push_back(segment2);
  segment3.asConfedSequence()->push_back(32936);
  update.attrs()->asPath()->push_back(segment3);
  segment4.asConfedSet()->insert(32937);
  update.attrs()->asPath()->push_back(segment4);
  update.attrs()->med() = 32;
  update.attrs()->isMedSet() = true;
  update.attrs()->localPref() = 100;
  BgpAttrCommunity community;
  *community.asn() = 65530;
  *community.value() = 15800;
  update.attrs()->communities()->push_back(community);
  *update.attrs()->originatorId() = 0x86070000; // ip: "0.0.7.134"
  *update.mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update.mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  *update.mpAnnounced()->nexthop() =
      network::toBinaryAddress(folly::IPAddress("fd00::1"));
  RiggedIPPrefix prefix1, prefix2;
  *prefix1.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("fd00::3000/122"));
  prefix1.pathId() = 1;
  update.mpAnnounced()->prefixes()->push_back(prefix1);
  prefix2.pathId() = 2;
  *prefix2.prefix() = network::toIPPrefix(
      folly::IPAddress::createNetwork("dead:beef:face:b00c::/64"));
  update.mpAnnounced()->prefixes()->push_back(prefix2);

  auto serMsg = BgpMessageSerializer::serializeBgpUpdate2(update, true);
  EXPECT_EQ(1, serMsg->countChainElements());
  EXPECT_EQ(serMsg->computeChainDataLength(), msg.size());
  EXPECT_EQ(0, memcmp(serMsg->data(), &msg[0], msg.size()));
}

TEST(BgpMessageSerializer, BgpUpdate2V6LUTest) {
  std::array<uint8_t, 79> msg{{
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x4f, // Length of BGP PDU
    0x02, // Bgp message type (Update)
    0x00, 0x00, // Withdrawn routes length
    // Path Attributes
    0x00, 0x38, // Path attributes length (73 bytes)
      0x40, 0x01, 0x01, // ORIGIN
        0x01, // EGP
      0x50, 0x02, 0x00, 0x06, // AS_PATH
        0x01, // Segment type: AS_SET
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa6, // asn: 32934
      0x40, 0x05, 0x04, // LOCAL_PREF
        0x00, 0x00, 0x00, 0x64, // 100
      0x90, 0x0e, 0x00, 0x1f, /**** MP_REACH_NLRI (33 bytes) ****/
        0x00, 0x02, // Address family identifier
        0x04,       // Subsequent address family identifier
        0x10,       // Length of nexthop
          0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // Nexthop, 16 bytes
        0x00, // Reserved
        0x47, // length (48 + 23  bits)
          0x00, 0x00, 0x50, // Label: 5, Bottom of Stack: false
          0x00, 0x00, 0x41, // Label: 4, Bottom of Stack: true
          0xde, 0xad, 0x00, // Prefix "dead::/23"
    // No Network Layer Reachability information
      // clang-format on
  }};
  BgpUpdate2 update;
  *update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_EGP;
  BgpAttrAsPathSegment segment;
  segment.asSet()->insert(32934);
  update.attrs()->asPath()->push_back(segment);
  update.attrs()->localPref() = 100;
  *update.mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update.mpAnnounced()->safi() = BgpUpdateSafi::SAFI_LABELED_UNICAST;
  *update.mpAnnounced()->nexthop() =
      network::toBinaryAddress(folly::IPAddress("fd00::1"));
  RiggedIPPrefix prefix;
  *prefix.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("dead::/23"));
  *prefix.labels() = {5, 4};
  update.mpAnnounced()->prefixes()->push_back(prefix);

  auto serMsg = BgpMessageSerializer::serializeBgpUpdate2(update, true);
  EXPECT_EQ(1, serMsg->countChainElements());
  EXPECT_EQ(msg.size(), serMsg->computeChainDataLength());
  EXPECT_TRUE(std::equal(msg.begin(), msg.end(), serMsg->data()));
}

TEST(BgpMessageSerializer, BgpUpdate2AllTypesTest) {
  std::array<uint8_t, 28> v4WithdrawnMsg{{
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x1c, // Length of BGP PDU
    0x02, // Bgp message type (Update)
    0x00, 0x05, // Withdrawn routes length (5 bytes)
      0x20, 0x03, 0x04, 0x05, 0x06, // Withdrawn prefix, "3.4.5.6/32"
    0x00, 0x00, // Path attributes length
      // No Path attributes
    // No Network Layer Reachability information
      // clang-format on
  }};
  std::array<uint8_t, 63> v4AnnouncedMsg{{
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x3f, // Length of BGP PDU
    0x02, // Bgp message type (Update)
    0x00, 0x00, // Withdrawn routes length
    // Path Attributes
    0x00, 0x23, // Path attributes length (35 bytes)
      0x40, 0x01, 0x01, // ORIGIN
        0x01, // EGP
      0x50, 0x02, 0x00, 0x06, // AS_PATH
        0x02, // Segment type: AS_SEQUENCE
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa6, // asn: 32934
      0x40, 0x03, 0x04, // NEXT_HOP
        0x01, 0x02, 0x03, 0x04, // "1.2.3.4"
      0x80, 0x04, 0x04, // MULTI_EXIT_DESC
        0x00, 0x00, 0x00, 0x20, // 32
      0x40, 0x05, 0x04, // LOCAL_PREF
        0x00, 0x00, 0x00, 0x64, // 100
      // Network Layer Reachability information
      0x20, 0x06, 0x05, 0x04, 0x03, // Update prefix, "6.5.4.3/32"
      // clang-format on
  }};
  std::array<uint8_t, 102> mpAnnouncedMsg{{
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x66, // Length of BGP PDU
    0x02, // Bgp message type (Update)
    0x00, 0x00, // Withdrawn routes length
    // Path Attributes
    0x00, 0x4f, // Path attributes length (79 bytes)
      0x40, 0x01, 0x01, // ORIGIN
        0x01, // EGP
      0x50, 0x02, 0x00, 0x06, // AS_PATH
        0x02, // Segment type: AS_SEQUENCE
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa6, // asn: 32934
      0x80, 0x04, 0x04,  // MULTI_EXIT_DESC
        0x00, 0x00, 0x00, 0x20, // 32
      0x40, 0x05, 0x04, // LOCAL_PREF
        0x00, 0x00, 0x00, 0x64, // 100
      0x90, 0x0e, 0x00, 0x2f, /**** MP_REACH_NLRI (47 bytes) ****/
        0x00, 0x02, // Address family identifier
        0x01,       // Subsequent address family identifier
        0x10,       // Length of nexthop
          0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // Nexthop, 16 bytes
        0x00, // Reserved
        0x7a, // Prefix: length (124 bit)
          0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00,
        0x40, // Prefix: length (64 bit)
          0xde, 0xad, 0xbe, 0xef, 0xfa, 0xce, 0xb0, 0x0c,
    // No Network Layer Reachability information
      // clang-format on
  }};
  std::array<uint8_t, 40> mpWithdrawnMsg{{
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x28, // Length of BGP PDU (40 bytes)
    0x02, // Bgp message type (Update)
    0x00, 0x00, // Withdrawn routes length
    // Path Attributes
    0x00, 0x11, // Path attributes length (17 bytes)
      0x90, 0x0f, 0x00, 0x0d, /**** MP_UNREACH_NLRI (13 bytes) ****/
        0x00, 0x01, // Address family identifier
        0x04, // Subsequent address family identifier
        0x48, // length (48 + 24 bits)
          0x00, 0x00, 0x80, // Label: 8, Bottom of Stack: false
          0x00, 0x00, 0x91, // Label: 9, Bottom of Stack: true
          0x04, 0x05, 0x06, // Prefix "4.5.6.0/24"
    // No Network Layer Reachability information
      // clang-format on
  }};

  BgpUpdate2 update;
  *update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_EGP;
  BgpAttrAsPathSegment segment;
  segment.asSequence()->push_back(32934);
  update.attrs()->asPath()->push_back(segment);
  *update.v4Nexthop() = network::toBinaryAddress(folly::IPAddress("1.2.3.4"));
  update.attrs()->med() = 32;
  update.attrs()->isMedSet() = true;
  update.attrs()->localPref() = 100;
  // v4Announced
  RiggedIPPrefix p1, p2;
  *p1.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("6.5.4.3/32"));
  update.v4Announced2()->push_back(p1);
  // v4Withdrawn
  *p2.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("3.4.5.6/32"));
  update.v4Withdrawn2()->push_back(p2);

  // mpAnnounced
  *update.mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update.mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  *update.mpAnnounced()->nexthop() =
      network::toBinaryAddress(folly::IPAddress("fd00::1"));
  RiggedIPPrefix prefix1, prefix2;
  *prefix1.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("fd00::3000/122"));
  update.mpAnnounced()->prefixes()->push_back(prefix1);
  *prefix2.prefix() = network::toIPPrefix(
      folly::IPAddress::createNetwork("dead:beef:face:b00c::/64"));
  update.mpAnnounced()->prefixes()->push_back(prefix2);
  // mpWithdrawn
  *update.mpWithdrawn()->afi() = BgpUpdateAfi::AFI_IPv4;
  *update.mpWithdrawn()->safi() = BgpUpdateSafi::SAFI_LABELED_UNICAST;
  RiggedIPPrefix prefix;
  *prefix.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("4.5.6.0/24"));
  *prefix.labels() = {8, 9};
  update.mpWithdrawn()->prefixes()->push_back(prefix);

  /*
   * Serialization sequence is v4Withdrawn -> mpWithdrawn -> mpAnnounced ->
   * v4Announced
   */
  auto iobufPtr = BgpMessageSerializer::serializeBgpUpdate2(update, true);
  EXPECT_EQ(4, iobufPtr->countChainElements());
  auto iobuf = iobufPtr.get();
  EXPECT_EQ(v4WithdrawnMsg.size(), iobuf->length());
  EXPECT_TRUE(
      std::equal(v4WithdrawnMsg.begin(), v4WithdrawnMsg.end(), iobuf->data()));
  iobuf = iobuf->next();
  EXPECT_EQ(mpWithdrawnMsg.size(), iobuf->length());
  EXPECT_TRUE(
      std::equal(mpWithdrawnMsg.begin(), mpWithdrawnMsg.end(), iobuf->data()));
  iobuf = iobuf->next();
  EXPECT_EQ(mpAnnouncedMsg.size(), iobuf->length());
  EXPECT_TRUE(
      std::equal(mpAnnouncedMsg.begin(), mpAnnouncedMsg.end(), iobuf->data()));
  iobuf = iobuf->next();
  EXPECT_EQ(v4AnnouncedMsg.size(), iobuf->length());
  EXPECT_TRUE(
      std::equal(v4AnnouncedMsg.begin(), v4AnnouncedMsg.end(), iobuf->data()));
}

TEST(BgpMessageSerializer, BgpUpdate2AllTypesAddPathTest) {
  std::vector<uint8_t> v4WithdrawnMsg{
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x20, // Length of BGP PDU +4 ****
    0x02, // Bgp message type (Update)
    0x00, 0x09, // Withdrawn routes length (5 + 4 bytes) ***
      0x00,0x00,0x00,0x01, //path id 1
      0x20, 0x03, 0x04, 0x05, 0x06, // Withdrawn prefix, "3.4.5.6/32"
    0x00, 0x00, // Path attributes length
      // No Path attributes
    // No Network Layer Reachability information
      // clang-format on
  };
  std::vector<uint8_t> v4AnnouncedMsg{
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x43, // Length of BGP PDU + 4
    0x02, // Bgp message type (Update)
    0x00, 0x00, // Withdrawn routes length
    // Path Attributes
    0x00, 0x23, // Path attributes length (35 bytes)
      0x40, 0x01, 0x01, // ORIGIN
        0x01, // EGP
      0x50, 0x02, 0x00, 0x06, // AS_PATH
        0x02, // Segment type: AS_SEQUENCE
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa6, // asn: 32934
      0x40, 0x03, 0x04, // NEXT_HOP
        0x01, 0x02, 0x03, 0x04, // "1.2.3.4"
      0x80, 0x04, 0x04, // MULTI_EXIT_DESC
        0x00, 0x00, 0x00, 0x20, // 32
      0x40, 0x05, 0x04, // LOCAL_PREF
        0x00, 0x00, 0x00, 0x64, // 100
      // Network Layer Reachability information
      0x00,0x00,0x00, 0x02, //path id 1
      0x20, 0x06, 0x05, 0x04, 0x03, // Update prefix, "6.5.4.3/32"
      // clang-format on
  };
  std::vector<uint8_t> mpAnnouncedMsg{
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x6e, // Length of BGP PDU + 8
    0x02, // Bgp message type (Update)
    0x00, 0x00, // Withdrawn routes length
    // Path Attributes
    0x00, 0x57, // Path attributes length (79 + 8 bytes)
      0x40, 0x01, 0x01, // ORIGIN
        0x01, // EGP
      0x50, 0x02, 0x00, 0x06, // AS_PATH
        0x02, // Segment type: AS_SEQUENCE
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa6, // asn: 32934
      0x80, 0x04, 0x04,  // MULTI_EXIT_DESC
        0x00, 0x00, 0x00, 0x20, // 32
      0x40, 0x05, 0x04, // LOCAL_PREF
        0x00, 0x00, 0x00, 0x64, // 100
      0x90, 0x0e, 0x00, 0x37, /**** MP_REACH_NLRI (47 + 8 bytes) ****/
        0x00, 0x02, // Address family identifier
        0x01,       // Subsequent address family identifier
        0x10,       // Length of nexthop
          0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // Nexthop, 16 bytes
        0x00, // Reserved
        0x00,0x00,0x00,0x03,
        0x7a, // Prefix: length (124 bit)
          0xfd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00,
        0x00,0x00,0x00,0x04,
        0x40, // Prefix: length (64 bit)
          0xde, 0xad, 0xbe, 0xef, 0xfa, 0xce, 0xb0, 0x0c,
    // No Network Layer Reachability information
      // clang-format on
  };
  std::vector<uint8_t> mpWithdrawnMsg{
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x2c, // Length of BGP PDU (40 + 4 bytes)
    0x02, // Bgp message type (Update)
    0x00, 0x00, // Withdrawn routes length
    // Path Attributes
    0x00, 0x15, // Path attributes length (17 + 4 bytes)
      0x90, 0x0f, 0x00, 0x11, /**** MP_UNREACH_NLRI (13 + 4 bytes) ****/
        0x00, 0x01, // Address family identifier
        0x04, // Subsequent address family identifier
        0x00,0x00,0x00,0x05, //path id 5
        0x48, // length (48 + 24 bits)
          0x00, 0x00, 0x80, // Label: 8, Bottom of Stack: false
          0x00, 0x00, 0x91, // Label: 9, Bottom of Stack: true
          0x04, 0x05, 0x06, // Prefix "4.5.6.0/24"
    // No Network Layer Reachability information
      // clang-format on
  };

  BgpUpdate2 update;
  *update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_EGP;
  BgpAttrAsPathSegment segment;
  segment.asSequence()->push_back(32934);
  update.attrs()->asPath()->push_back(segment);
  *update.v4Nexthop() = network::toBinaryAddress(folly::IPAddress("1.2.3.4"));
  update.attrs()->med() = 32;
  update.attrs()->isMedSet() = true;
  update.attrs()->localPref() = 100;

  RiggedIPPrefix p1, p2;
  *p1.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("6.5.4.3/32"));
  p1.pathId() = 2;
  // v4Announced
  update.v4Announced2()->push_back(p1);
  // v4Withdrawn

  *p2.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("3.4.5.6/32"));
  p2.pathId() = 1;
  update.v4Withdrawn2()->push_back(p2);

  // mpAnnounced
  *update.mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update.mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  *update.mpAnnounced()->nexthop() =
      network::toBinaryAddress(folly::IPAddress("fd00::1"));
  RiggedIPPrefix prefix1, prefix2;
  prefix1.pathId() = 3;
  prefix2.pathId() = 4;
  *prefix1.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("fd00::3000/122"));
  update.mpAnnounced()->prefixes()->push_back(prefix1);
  *prefix2.prefix() = network::toIPPrefix(
      folly::IPAddress::createNetwork("dead:beef:face:b00c::/64"));
  update.mpAnnounced()->prefixes()->push_back(prefix2);

  // mpWithdrawn
  *update.mpWithdrawn()->afi() = BgpUpdateAfi::AFI_IPv4;
  *update.mpWithdrawn()->safi() = BgpUpdateSafi::SAFI_LABELED_UNICAST;
  RiggedIPPrefix prefix;
  prefix.pathId() = 5;
  *prefix.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("4.5.6.0/24"));
  *prefix.labels() = {8, 9};
  update.mpWithdrawn()->prefixes()->push_back(prefix);

  /*
   * Serialization sequence is v4Withdrawn -> mpWithdrawn -> mpAnnounced ->
   * v4Announced
   */
  auto iobufPtr = BgpMessageSerializer::serializeBgpUpdate2(update, true);
  EXPECT_EQ(4, iobufPtr->countChainElements());
  auto iobuf = iobufPtr.get();
  EXPECT_EQ(v4WithdrawnMsg.size(), iobuf->length());
  EXPECT_EQ(
      0, memcmp(iobuf->data(), &v4WithdrawnMsg[0], v4WithdrawnMsg.size()));

  iobuf = iobuf->next();
  EXPECT_EQ(mpWithdrawnMsg.size(), iobuf->length());
  EXPECT_EQ(
      0, memcmp(iobuf->data(), &mpWithdrawnMsg[0], mpWithdrawnMsg.size()));

  iobuf = iobuf->next();
  EXPECT_EQ(mpAnnouncedMsg.size(), iobuf->length());
  EXPECT_EQ(
      0, memcmp(iobuf->data(), &mpAnnouncedMsg[0], mpAnnouncedMsg.size()));

  iobuf = iobuf->next();
  EXPECT_EQ(v4AnnouncedMsg.size(), iobuf->length());
  EXPECT_EQ(
      0, memcmp(iobuf->data(), &v4AnnouncedMsg[0], v4AnnouncedMsg.size()));
}

TEST(BgpMessageSerializer, BgpUpdate2LargeV4Test) {
  BgpUpdate2 update;
  // 220 * 4 * 5 = 4400 bytes v4 withdrawn
  for (int i = 1; i <= 220; i++) {
    update.v4Withdrawn()->push_back(
        network::toIPPrefix(
            folly::IPAddress::createNetwork(fmt::format("6.5.4.{}/32", i))));
    update.v4Withdrawn()->push_back(
        network::toIPPrefix(
            folly::IPAddress::createNetwork(fmt::format("6.5.{}.3/32", i))));
    update.v4Withdrawn()->push_back(
        network::toIPPrefix(
            folly::IPAddress::createNetwork(fmt::format("6.{}.4.3/32", i))));
    update.v4Withdrawn()->push_back(
        network::toIPPrefix(
            folly::IPAddress::createNetwork(fmt::format("{}.5.4.3/32", i))));
  }
  // 220 * 4 * 5 = 4400 bytes v4 announced
  for (int i = 1; i <= 220; i++) {
    update.v4Announced()->push_back(
        network::toIPPrefix(
            folly::IPAddress::createNetwork(fmt::format("6.5.5.{}/32", i))));
    update.v4Announced()->push_back(
        network::toIPPrefix(
            folly::IPAddress::createNetwork(fmt::format("6.5.{}.3/32", i))));
    update.v4Announced()->push_back(
        network::toIPPrefix(
            folly::IPAddress::createNetwork(fmt::format("6.{}.4.3/32", i))));
    update.v4Announced()->push_back(
        network::toIPPrefix(
            folly::IPAddress::createNetwork(fmt::format("{}.5.4.3/32", i))));
  }
  // 1020 bytes Path Attrs length
  populatePathAttributesV4(update);

  // serializeBgpUpdate2
  auto serMsg = BgpMessageSerializer::serializeBgpUpdate2(update, true);
  EXPECT_EQ(4, serMsg->countChainElements());

  /*
   * expected length:
   * 19 + 2 + 4070 + 2 = 4093
   * 19 + 2 + 330 + 2 = 353
   * 19 + 2 + 0 + 2 + 1024 + 3045 = 4092
   * 19 + 2 + 0 + 2 + 1024 + 1355 = 2402
   */
  std::vector<int> expectLen{4093, 353, 4092, 2402};

  // capabilities used to parse raw bgp update mesaages
  BgpCapabilities capabilities;
  *capabilities.mpExtV4Unicast() = true;
  *capabilities.mpExtV6Unicast() = true;
  *capabilities.as4byte() = true;
  std::vector<BgpUpdate2> parsedMsgs;

  // check serilized message length and parse them back to BgpUpdate2
  auto oneMsg = serMsg.get();
  for (int i = 0; i < serMsg->countChainElements(); i++) {
    EXPECT_EQ(expectLen[i], oneMsg->length());
    auto msgBuf = folly::IOBuf::wrapBuffer(oneMsg->data(), oneMsg->length());
    auto msg = std::get<std::shared_ptr<const BgpUpdate2>>(
        BgpMessageParser2::parseBgpUpdateRaw(*msgBuf, capabilities));
    parsedMsgs.push_back(*msg);
    oneMsg = oneMsg->next();
  }

  /*
   * Combine messages back to one:
   * parsedMsgs[0], parsedMsgs[1] contain v4Withdrawn
   * parsedMsgs[2], parsedMsgs[3] contain v4Announced
   * Use parsedMsgs[2] as base since it has all path attrs
   */
  auto parsedUpdate = parsedMsgs[2];
  // fill in v4Withdrawn from parsedMsgs[0] and parsedMsgs[1]
  *parsedUpdate.v4Withdrawn() = *parsedMsgs[0].v4Withdrawn();
  parsedUpdate.v4Withdrawn()->insert(
      parsedUpdate.v4Withdrawn()->end(),
      parsedMsgs[1].v4Withdrawn()->begin(),
      parsedMsgs[1].v4Withdrawn()->end());

  *parsedUpdate.v4Withdrawn2() = *parsedMsgs[0].v4Withdrawn2();
  parsedUpdate.v4Withdrawn2()->insert(
      parsedUpdate.v4Withdrawn2()->end(),
      parsedMsgs[1].v4Withdrawn2()->begin(),
      parsedMsgs[1].v4Withdrawn2()->end());

  // append v4Withdrawn with parsedMsgs[3].v4Announced
  parsedUpdate.v4Announced()->insert(
      parsedUpdate.v4Announced()->end(),
      parsedMsgs[3].v4Announced()->begin(),
      parsedMsgs[3].v4Announced()->end());

  parsedUpdate.v4Announced2()->insert(
      parsedUpdate.v4Announced2()->end(),
      parsedMsgs[3].v4Announced2()->begin(),
      parsedMsgs[3].v4Announced2()->end());

  // parser also fills in attrs.nexthop
  *update.attrs()->nexthop() = "1.2.3.4";
  EXPECT_NE(update, parsedUpdate);

  // populate rigged prefix to make sure it is right.
  for (int i = 1; i <= 220; i++) {
    auto prefix = network::toIPPrefix(
        folly::IPAddress::createNetwork(fmt::format("6.5.4.{}/32", i)));
    RiggedIPPrefix rigPrefix;
    *rigPrefix.prefix() = prefix;
    update.v4Withdrawn2()->push_back(rigPrefix);

    prefix = network::toIPPrefix(
        folly::IPAddress::createNetwork(fmt::format("6.5.{}.3/32", i)));
    *rigPrefix.prefix() = prefix;
    update.v4Withdrawn2()->push_back(rigPrefix);

    prefix = network::toIPPrefix(
        folly::IPAddress::createNetwork(fmt::format("6.{}.4.3/32", i)));
    *rigPrefix.prefix() = prefix;
    update.v4Withdrawn2()->push_back(rigPrefix);

    prefix = network::toIPPrefix(
        folly::IPAddress::createNetwork(fmt::format("{}.5.4.3/32", i)));
    *rigPrefix.prefix() = prefix;
    update.v4Withdrawn2()->push_back(rigPrefix);
  }
  // 220 * 4 * 5 = 4400 bytes v4 announced
  for (int i = 1; i <= 220; i++) {
    auto prefix = network::toIPPrefix(
        folly::IPAddress::createNetwork(fmt::format("6.5.5.{}/32", i)));
    RiggedIPPrefix rigPrefix;
    *rigPrefix.prefix() = prefix;
    update.v4Announced2()->push_back(rigPrefix);

    prefix = network::toIPPrefix(
        folly::IPAddress::createNetwork(fmt::format("6.5.{}.3/32", i)));
    *rigPrefix.prefix() = prefix;
    update.v4Announced2()->push_back(rigPrefix);

    prefix = network::toIPPrefix(
        folly::IPAddress::createNetwork(fmt::format("6.{}.4.3/32", i)));
    *rigPrefix.prefix() = prefix;
    update.v4Announced2()->push_back(rigPrefix);

    prefix = network::toIPPrefix(
        folly::IPAddress::createNetwork(fmt::format("{}.5.4.3/32", i)));
    *rigPrefix.prefix() = prefix;
    update.v4Announced2()->push_back(rigPrefix);
  }
  EXPECT_EQ(update, parsedUpdate);
}

TEST(BgpMessageSerializer, BgpUpdate2LargeV6Test) {
  BgpUpdate2 update;
  // 879 bytes Path Attr(except mpWithDrawn and mpAnnounced) length
  populatePathAttributesV6(update);
  // 2 + 1 + 1 + 16 + 1 = 21
  *update.mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update.mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  *update.mpAnnounced()->nexthop() =
      network::toBinaryAddress(folly::IPAddress("fd00::1"));
  // 17 * 200 + 9 * 200 = 5200
  for (int i = 1; i <= 200; i++) {
    RiggedIPPrefix prefix1, prefix2;
    *prefix1.prefix() = network::toIPPrefix(
        folly::IPAddress::createNetwork(fmt::format("fd00::{}/122", i)));
    update.mpAnnounced()->prefixes()->push_back(prefix1);
    *prefix2.prefix() = network::toIPPrefix(
        folly::IPAddress::createNetwork(
            fmt::format("dead:beef:face:{}::/64", i)));
    update.mpAnnounced()->prefixes()->push_back(prefix2);
  }

  // 2 + 1 = attr length
  *update.mpWithdrawn()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update.mpWithdrawn()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  // 17 * 200 + 9 * 200 = 5200 mpWithDrawn NLRI length
  for (int i = 1; i <= 200; i++) {
    RiggedIPPrefix prefix1, prefix2;
    *prefix1.prefix() = network::toIPPrefix(
        folly::IPAddress::createNetwork(fmt::format("fd00::{}/122", i)));
    update.mpWithdrawn()->prefixes()->push_back(prefix1);
    *prefix2.prefix() = network::toIPPrefix(
        folly::IPAddress::createNetwork(
            fmt::format("dead:beef:face:{}::/64", i)));
    update.mpWithdrawn()->prefixes()->push_back(prefix2);
  }

  // serializeBgpUpdate2
  auto serMsg = BgpMessageSerializer::serializeBgpUpdate2(update, true);
  EXPECT_EQ(4, serMsg->countChainElements());

  // expected length:
  std::vector<int> expectLen{4086, 1174, 4094, 2968};

  // capabilities used to parse raw bgp update mesaages
  BgpCapabilities capabilities;
  *capabilities.mpExtV4Unicast() = true;
  *capabilities.mpExtV6Unicast() = true;
  *capabilities.as4byte() = true;
  std::vector<BgpUpdate2> parsedMsgs;

  auto oneMsg = serMsg.get();
  for (int i = 0; i < serMsg->countChainElements(); i++) {
    EXPECT_EQ(expectLen[i], oneMsg->length());
    auto msgBuf = folly::IOBuf::wrapBuffer(oneMsg->data(), oneMsg->length());
    auto msg = std::get<std::shared_ptr<const BgpUpdate2>>(
        BgpMessageParser2::parseBgpUpdateRaw(*msgBuf, capabilities));
    parsedMsgs.push_back(*msg);
    oneMsg = oneMsg->next();
  }

  /*
   * Combine messages back to one:
   *    parsedMsgs[0], parsedMsgs[1] contain mpWithdrawn
   *    parsedMsgs[2], parsedMsgs[3] contain mpAnnounced
   * Use parsedMsgs[2] as base since it has all path attrs
   */
  auto parsedUpdate = parsedMsgs[2];
  // fill in mpWithdrawn from parsedMsgs[0] and parsedMsgs[1]
  *parsedUpdate.mpWithdrawn() = *parsedMsgs[0].mpWithdrawn();
  parsedUpdate.mpWithdrawn()->prefixes()->insert(
      parsedUpdate.mpWithdrawn()->prefixes()->end(),
      parsedMsgs[1].mpWithdrawn()->prefixes()->begin(),
      parsedMsgs[1].mpWithdrawn()->prefixes()->end());
  // append mpAnnounced with parsedMsgs[3]
  parsedUpdate.mpAnnounced()->prefixes()->insert(
      parsedUpdate.mpAnnounced()->prefixes()->end(),
      parsedMsgs[3].mpAnnounced()->prefixes()->begin(),
      parsedMsgs[3].mpAnnounced()->prefixes()->end());

  EXPECT_EQ(update, parsedUpdate);
}

TEST(BgpMessageSerializer, BgpUpdate2LargeExtendedAttrLenTest) {
  BgpUpdate2 update;
  // 1963 bytes Path Attr(except mpWithDrawn and mpAnnounced) length
  *update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_EGP;
  BgpAttrAsPathSegment segment;
  // 120 * 4 = 480 > 255, this will make path attr length go over 1 btye
  for (int i = 0; i < 120; i++) {
    segment.asSequence()->push_back(i);
  }
  update.attrs()->asPath()->push_back(segment);
  update.attrs()->med() = 32;
  update.attrs()->isMedSet() = true;
  update.attrs()->localPref() = 100;
  BgpAttrCommunity community;
  *community.asn() = 65530;
  *community.value() = 15800;
  // 120 * 4 = 480 > 255, this will make communities length go over 1 btye
  for (int i = 0; i < 120; i++) {
    update.attrs()->communities()->push_back(community);
  }
  *update.attrs()->originatorId() = 0x86070000; // ip: "0.0.7.134"
  // 120 * 4 = 480 > 255, this will make cluster list length go over 1 btye
  for (int i = 0; i < 120; i++) {
    update.attrs()->clusterList()->push_back(0x10010000); // ip:
                                                          // "0.0.1.16"
  }
  BgpAttrExtCommunity extCommunity;
  *extCommunity.firstWord() = 0x2272a;
  *extCommunity.secondWord() = 0x232f;
  /*
   * 60 * 8 = 480 > 255
   * this will make extended communities length go over 1 btye
   */
  for (int i = 0; i < 60; i++) {
    update.attrs()->extCommunities()->push_back(extCommunity);
  }
  // 2 + 1 + 1 + 16 + 1 = 21
  *update.mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update.mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  *update.mpAnnounced()->nexthop() =
      network::toBinaryAddress(folly::IPAddress("fd00::1"));
  RiggedIPPrefix prefix1, prefix2;
  *prefix1.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("fd00::1/122"));
  update.mpAnnounced()->prefixes()->push_back(prefix1);
  *prefix2.prefix() = network::toIPPrefix(
      folly::IPAddress::createNetwork("dead:beef:face:1::/64"));
  update.mpAnnounced()->prefixes()->push_back(prefix2);

  // serializeBgpUpdate2
  auto serMsg = BgpMessageSerializer::serializeBgpUpdate2(update, true);
  EXPECT_EQ(1, serMsg->countChainElements());
  // expected length: 19 + 2 + 0 + 2 + 1963 + (4 + 21 + 26) = 2037
  EXPECT_EQ(2037, serMsg->length());

  // capabilities used to parse raw bgp update mesaages
  BgpCapabilities capabilities;
  *capabilities.mpExtV4Unicast() = true;
  *capabilities.mpExtV6Unicast() = true;
  *capabilities.as4byte() = true;
  auto msgBuf = folly::IOBuf::wrapBuffer(serMsg->data(), serMsg->length());
  auto parsedUpdate = std::get<std::shared_ptr<const BgpUpdate2>>(
      BgpMessageParser2::parseBgpUpdateRaw(*msgBuf, capabilities));

  EXPECT_EQ(update, *parsedUpdate);
}

TEST(BgpMessageSerializer, BgpUpdate2WithoutLocalPref) {
  // The message below is valid without localPref attr
  std::vector<uint8_t> msg = {
      // clang-format off
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x2f, // Length of BGP PDU
    0x02, // Bgp message type (Update)
    0x00, 0x00, // Withdrawn routes length
    // Path Attributes
    0x00, 0x15, // Path attributes length (21 bytes)
      0x40, 0x01, 0x01, // ORIGIN
        0x01, // EGP
      0x50, 0x02, 0x00, 0x06, // AS_PATH
        0x01, // Segment type: AS_SET
        0x01, // Segment count: 1
        0x00, 0x00, 0x80, 0xa6, // asn: 32934
      0x40, 0x03, 0x04, // NEXT_HOP
        0x07, 0x06, 0x05, 0x04, // "7.6.5.4"
    // Network Layer Reachability information
    0x10, 0x04, 0x05, // "4.5.0.0/16"
      // clang-format on
  };
  BgpUpdate2 update;
  update.v4Announced()->push_back(
      network::toIPPrefix(folly::IPAddress::createNetwork("4.5.0.0/16")));
  *update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_EGP;
  BgpAttrAsPathSegment segment;
  segment.asSet()->insert(32934);
  update.attrs()->asPath()->push_back(segment);
  *update.v4Nexthop() = network::toBinaryAddress(folly::IPAddress("7.6.5.4"));

  auto iobuf = BgpMessageSerializer::serializeBgpUpdate2(update, true);
  EXPECT_EQ(1, iobuf->countChainElements());
  EXPECT_EQ(msg.size(), iobuf->computeChainDataLength());
  EXPECT_TRUE(std::equal(msg.begin(), msg.end(), iobuf->data()));
}

TEST(BgpMessageSerializer, BgpUpdate2ErrorTest) {
  /*
   * CASE 1
   * Empty NLRI test
   */
  BgpUpdate2 update;
  *update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_EGP;
  BgpAttrAsPathSegment segment;
  segment.asSet()->insert(32934);
  update.attrs()->asPath()->push_back(segment);
  update.attrs()->localPref() = 100;
  *update.v4Nexthop() = network::toBinaryAddress(folly::IPAddress("1.2.3.4"));
  EXPECT_THROW(
      {
        try {
          BgpMessageSerializer::serializeBgpUpdate2(update, true);
        } catch (BgpSerializerException& e) {
          EXPECT_EQ(BgpSerializerExceptionCode::MISSING_NLRI_INFO, e.getCode());
          throw;
        }
      },
      BgpSerializerException);

  /*
   * CASE 2
   * Invalid AS Path test
   */
  BgpUpdate2 update2;
  update2.v4Announced()->push_back(
      network::toIPPrefix(folly::IPAddress::createNetwork("6.5.4.3/32")));
  *update2.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment2;
  segment2.asSequence()->push_back(32934);
  segment2.asConfedSequence()->push_back(2000);
  update2.attrs()->asPath()->push_back(segment2);
  *update2.v4Nexthop() = network::toBinaryAddress(folly::IPAddress("1.2.3.4"));
  update2.attrs()->localPref() = 100;
  EXPECT_THROW(
      {
        try {
          BgpMessageSerializer::serializeBgpUpdate2(update2, true);
        } catch (BgpSerializerException& e) {
          EXPECT_EQ(
              BgpSerializerExceptionCode::INVALID_ASPATH_INFO, e.getCode());
          throw;
        }
      },
      BgpSerializerException);

  /*
   * CASE 3
   * Too large as path length when the buffer size is the max bgp message size.
   */
  BgpUpdate2 update3;
  update3.v4Announced()->push_back(
      network::toIPPrefix(folly::IPAddress::createNetwork("6.5.4.3/32")));
  *update3.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment3;
  // With 4-byte ASN, the as path length will exceed 4KB message size limit.
  for (int i = 0; i < 1100; i++) {
    segment3.asSequence()->push_back(i);
  }
  update3.attrs()->asPath()->push_back(segment3);
  *update3.v4Nexthop() = network::toBinaryAddress(folly::IPAddress("1.2.3.4"));
  update3.attrs()->localPref() = 100;
  EXPECT_THROW(
      BgpMessageSerializer::serializeBgpUpdate2(update3, true),
      std::out_of_range);

  /*
   * CASE 4
   * Too large as path length with presence of v4Withdrawn and mpWithdrawn.
   * nothing will be sent in this case.
   */
  BgpUpdate2 update4;
  update4.v4Announced()->push_back(
      network::toIPPrefix(folly::IPAddress::createNetwork("6.5.4.3/32")));
  *update4.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment4;
  // With 4-byte ASN, the as path length will exceed 4KB message size limit.
  for (int i = 0; i < 1100; i++) {
    segment4.asSequence()->push_back(i);
  }
  update4.attrs()->asPath()->push_back(segment4);
  *update4.v4Nexthop() = network::toBinaryAddress(folly::IPAddress("1.2.3.4"));
  update4.attrs()->localPref() = 100;
  update4.v4Withdrawn()->push_back(
      network::toIPPrefix(folly::IPAddress::createNetwork("3.4.5.6/32")));
  // mpWithdrawn
  *update4.mpWithdrawn()->afi() = BgpUpdateAfi::AFI_IPv4;
  *update4.mpWithdrawn()->safi() = BgpUpdateSafi::SAFI_LABELED_UNICAST;
  RiggedIPPrefix prefix;
  *prefix.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("4.5.6.0/24"));
  *prefix.labels() = {8, 9};
  update4.mpWithdrawn()->prefixes()->push_back(prefix);

  EXPECT_THROW(
      BgpMessageSerializer::serializeBgpUpdate2(update4, true),
      std::out_of_range);
}

TEST(BgpMessageSerializer, MultipleNlriTest) {
  std::array<uint8_t, 12> nlri{{
      0x0D,
      0x06,
      0x50, // Update prefix, "6.80.0.0/13"
      0x11,
      0x06,
      0x05,
      0x00, // Update prefix, "6.5.0.0/17"
      0x20,
      0x06,
      0x05,
      0x04,
      0x03, // Update prefix, "6.5.4.3/32"
  }};
  auto iobuf = IOBuf::create(kMaxBgpMsgLen);
  iobuf->append(kMaxBgpMsgLen);

  std::vector<RiggedIPPrefix> riggedPrefixes;
  RiggedIPPrefix rigPrefix1, rigPrefix2, rigPrefix3;
  rigPrefix1.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("6.80.0.0/13"));
  riggedPrefixes.push_back(rigPrefix1);
  rigPrefix2.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("6.5.0.0/17"));
  riggedPrefixes.push_back(rigPrefix2);
  rigPrefix3.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("6.5.4.3/32"));
  riggedPrefixes.push_back(rigPrefix3);

  size_t curPrefixCnt = 0;
  int ret = BgpMessageSerializer::serializeNlri(
      RWPrivateCursor(iobuf.get()),
      BgpUpdateAfi::AFI_IPv4,
      BgpUpdateSafi::SAFI_UNICAST,
      riggedPrefixes,
      riggedPrefixes.size(),
      kMaxBgpMsgLen,
      5, /* maxPrefixLen */
      curPrefixCnt);
  EXPECT_EQ(ret, nlri.size());
  auto buf = iobuf->data();
  EXPECT_TRUE(std::equal(nlri.begin(), nlri.end(), buf));
}

TEST(BgpMessageSerializer, MpLuNlriTest) {
  std::array<uint8_t, 33> nlri{{
      // clang-format off
    0x47, // length (48 + 23  bits)
      0x00, 0x00, 0x50, // Label: 5, Bottom of Stack: false
      0x00, 0x00, 0x41, // Label: 4, Bottom of Stack: true
      0xde, 0xad, 0x00, // Prefix "dead::/23"
    0xb0, // length (48 + 128  bits)
      0x00, 0x00, 0x80, // Label: 8, Bottom of Stack: false
      0x00, 0x00, 0x91, // Label: 9, Bottom of Stack: true
                        // Prefix "dead::beaf/128"
      0xde, 0xad, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xbe, 0xaf,
      // clang-format on
  }};
  auto iobuf = IOBuf::create(kMaxBgpMsgLen);
  iobuf->append(kMaxBgpMsgLen);

  BgpNlri mpAnnounced;
  *mpAnnounced.afi() = BgpUpdateAfi::AFI_IPv6;
  *mpAnnounced.safi() = BgpUpdateSafi::SAFI_LABELED_UNICAST;
  RiggedIPPrefix prefix1, prefix2;
  *prefix1.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("dead::/23"));
  *prefix1.labels() = {5, 4};
  mpAnnounced.prefixes()->push_back(prefix1);
  *prefix2.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("dead::beaf/128"));
  *prefix2.labels() = {8, 9};
  mpAnnounced.prefixes()->push_back(prefix2);

  size_t curPrefixCnt = 0;
  int ret = BgpMessageSerializer::serializeNlri(
      RWPrivateCursor(iobuf.get()),
      *mpAnnounced.afi(),
      *mpAnnounced.safi(),
      *mpAnnounced.prefixes(),
      mpAnnounced.prefixes()->size(),
      kMaxBgpMsgLen,
      25, /* maxPrefixLen for IPv6 LU */
      curPrefixCnt);
  EXPECT_EQ(ret, nlri.size());
  auto buf = iobuf->data();
  EXPECT_TRUE(std::equal(nlri.begin(), nlri.end(), buf));
}

TEST(BgpMessageSerializer, MpNlriErrorTest) {
  auto iobuf = IOBuf::create(kMaxBgpMsgLen);
  iobuf->append(kMaxBgpMsgLen);
  // CASE 1
  std::vector<RiggedIPPrefix> riggedPrefixes1;
  RiggedIPPrefix rigPrefix1;
  rigPrefix1.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("1.2.3.0/24"));
  riggedPrefixes1.push_back(rigPrefix1);
  EXPECT_THROW(
      {
        try {
          size_t curPrefixCnt = 0;
          BgpMessageSerializer::serializeNlri(
              RWPrivateCursor(iobuf.get()),
              BgpUpdateAfi::AFI_IPv6,
              BgpUpdateSafi::SAFI_UNICAST,
              riggedPrefixes1,
              riggedPrefixes1.size(),
              kMaxBgpMsgLen,
              17, /* maxPrefixLen for IPv6 */
              curPrefixCnt);
        } catch (BgpSerializerException& e) {
          EXPECT_EQ(BgpSerializerExceptionCode::AFI_MISMATCH, e.getCode());
          throw;
        }
      },
      BgpSerializerException);

  // CASE 2
  std::vector<RiggedIPPrefix> riggedPrefixes2;
  RiggedIPPrefix rigPrefix2;
  rigPrefix2.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("fd00::3000/122"));
  riggedPrefixes2.push_back(rigPrefix2);
  EXPECT_THROW(
      {
        try {
          size_t curPrefixCnt = 0;
          BgpMessageSerializer::serializeNlri(
              RWPrivateCursor(iobuf.get()),
              BgpUpdateAfi::AFI_IPv6,
              BgpUpdateSafi::SAFI_LABELED_UNICAST,
              riggedPrefixes2,
              riggedPrefixes2.size(),
              kMaxBgpMsgLen,
              25, /* maxPrefixLen for IPv6 LU */
              curPrefixCnt);
        } catch (BgpSerializerException& e) {
          EXPECT_EQ(
              BgpSerializerExceptionCode::INVALID_NLRI_LABEL_INFO, e.getCode());
          throw;
        }
      },
      BgpSerializerException);

  // CASE 3
  BgpNlri mpAnnounced;
  *mpAnnounced.afi() = BgpUpdateAfi::AFI_IPv6;
  *mpAnnounced.safi() = BgpUpdateSafi::SAFI_UNICAST;
  RiggedIPPrefix prefix1, prefix2;
  *prefix1.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("fd00::3000/122"));
  mpAnnounced.prefixes()->push_back(prefix1);
  *prefix2.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("1.2.3.0/24"));
  mpAnnounced.prefixes()->push_back(prefix2);
  EXPECT_THROW(
      {
        try {
          size_t curPrefixCnt = 0;
          BgpMessageSerializer::serializeNlri(
              RWPrivateCursor(iobuf.get()),
              *mpAnnounced.afi(),
              *mpAnnounced.safi(),
              *mpAnnounced.prefixes(),
              mpAnnounced.prefixes()->size(),
              kMaxBgpMsgLen,
              25, /* maxPrefixLen */
              curPrefixCnt);
        } catch (BgpSerializerException& e) {
          EXPECT_EQ(BgpSerializerExceptionCode::AFI_MISMATCH, e.getCode());
          throw;
        }
      },
      BgpSerializerException);

  // CASE 4
  BgpNlri mpAnnounced2;
  *mpAnnounced2.afi() = BgpUpdateAfi::AFI_IPv6;
  *mpAnnounced2.safi() = BgpUpdateSafi::SAFI_UNICAST;
  RiggedIPPrefix prefix3;
  *prefix3.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("fd00::3000/122"));
  *prefix3.labels() = {5, 4};
  mpAnnounced2.prefixes()->push_back(prefix3);
  EXPECT_THROW(
      {
        try {
          size_t curPrefixCnt = 0;
          BgpMessageSerializer::serializeNlri(
              RWPrivateCursor(iobuf.get()),
              *mpAnnounced2.afi(),
              *mpAnnounced2.safi(),
              *mpAnnounced2.prefixes(),
              mpAnnounced2.prefixes()->size(),
              kMaxBgpMsgLen,
              25, /* maxPrefixLen */
              curPrefixCnt);
        } catch (BgpSerializerException& e) {
          EXPECT_EQ(
              BgpSerializerExceptionCode::INVALID_NLRI_LABEL_INFO, e.getCode());
          throw;
        }
      },
      BgpSerializerException);

  // CASE 5
  BgpNlri mpAnnounced3;
  *mpAnnounced3.afi() = BgpUpdateAfi::AFI_IPv6;
  *mpAnnounced3.safi() = BgpUpdateSafi::SAFI_LABELED_UNICAST;
  RiggedIPPrefix prefix4;
  *prefix4.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("fd00::3000/122"));
  mpAnnounced3.prefixes()->push_back(prefix4);
  EXPECT_THROW(
      {
        try {
          size_t curPrefixCnt = 0;
          BgpMessageSerializer::serializeNlri(
              RWPrivateCursor(iobuf.get()),
              *mpAnnounced3.afi(),
              *mpAnnounced3.safi(),
              *mpAnnounced3.prefixes(),
              mpAnnounced3.prefixes()->size(),
              kMaxBgpMsgLen,
              25, /* maxPrefixLen */
              curPrefixCnt);
        } catch (BgpSerializerException& e) {
          EXPECT_EQ(
              BgpSerializerExceptionCode::INVALID_NLRI_LABEL_INFO, e.getCode());
          throw;
        }
      },
      BgpSerializerException);
}

/**
 * Verify for v4 that when we have bgp updates with pathId that is too large to
 * fit into one message, we are chunking them correctly.
 */
TEST(BgpMessageSerializer, BgpUpdate2LargeV4WithPathIdTest) {
  BgpUpdate2 update;
  // 200 iters * 4  per iter * 9 prefix with pathId = 7200 bytes v4 prefixes
  for (int i = 1; i <= 200; i++) {
    update.v4Withdrawn2()->push_back(
        createRiggedIPPrefix(fmt::format("6.5.4.{}/32", i), i));
    update.v4Withdrawn2()->push_back(
        createRiggedIPPrefix(fmt::format("6.5.{}.3/32", i), i));
    update.v4Withdrawn2()->push_back(
        createRiggedIPPrefix(fmt::format("6.{}.4.3/32", i), i));
    update.v4Withdrawn2()->push_back(
        createRiggedIPPrefix(fmt::format("{}.5.4.3/32", i), i));
  }

  for (int i = 1; i <= 200; i++) {
    update.v4Announced2()->push_back(
        createRiggedIPPrefix(fmt::format("6.5.5.{}/32", i), i));
    update.v4Announced2()->push_back(
        createRiggedIPPrefix(fmt::format("6.5.{}.3/32", i), i));
    update.v4Announced2()->push_back(
        createRiggedIPPrefix(fmt::format("6.{}.5.3/32", i), i));
    update.v4Announced2()->push_back(
        createRiggedIPPrefix(fmt::format("{}.5.5.3/32", i), i));
  }
  // 1020 bytes Path Attrs length
  populatePathAttributesV4(update);

  auto serMsg = BgpMessageSerializer::serializeBgpUpdate2(update, true);
  EXPECT_EQ(5, serMsg->countChainElements());

  /*
   * expected msg lengths:
   * 19 + 2 + 4068 + 2 = 4091
   * 19 + 2 + 3132 + 2 = 3155
   * 19 + 2 + 0 + 2 + 1024 + 3042 = 4089
   * 19 + 2 + 0 + 2 + 1024 + 3042 = 4089
   * 19 + 2 + 0 + 2 + 1024 + 1116 = 2163
   */
  std::vector<int> expectLen{4091, 3155, 4089, 4089, 2163};

  // check serilized message length
  auto oneMsg = serMsg.get();
  for (int i = 0; i < serMsg->countChainElements(); i++) {
    EXPECT_EQ(expectLen[i], oneMsg->length());
    oneMsg = oneMsg->next();
  }
}

/**
 * Verify for v6 that when we have bgp updates with pathId that is too large to
 * fit into one message, we are chunking them correctly.
 */
TEST(BgpMessageSerializer, BgpUpdate2LargeV6WithPathIdTest) {
  BgpUpdate2 update;
  // 1020 bytes Path Attrs length
  populatePathAttributesV6(update);

  // 2 + 1 = 3
  *update.mpWithdrawn()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update.mpWithdrawn()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  // (17+4) * 200 = 4200
  for (int i = 1; i <= 200; i++) {
    update.mpWithdrawn()->prefixes()->push_back(
        createRiggedIPPrefix(fmt::format("fd00::{}/122", i), i));
  }

  // 2 + 1 + 1 + 16 + 1 = 21
  *update.mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update.mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  *update.mpAnnounced()->nexthop() =
      network::toBinaryAddress(folly::IPAddress("fd00::1"));
  // (17+4) * 200 = 4200
  for (int i = 1; i <= 200; i++) {
    update.mpAnnounced()->prefixes()->push_back(
        createRiggedIPPrefix(fmt::format("fd00::{}/122", i), i));
  }

  auto serMsg = BgpMessageSerializer::serializeBgpUpdate2(update, true);
  EXPECT_EQ(4, serMsg->countChainElements());

  /*
   * expected msg lengths:
   * 19 + 2 + 0 + 2 + (2 + 2 + 3 + (21 * 193)) = 4083
   * 19 + 2 + 0 + 2 + (2 + 2 + 3 + (21 * 7)) = 177
   * 19 + 2 + 0 + 2 + (883 + 4 + 21 + (21*150)) = 4081
   * 19 + 2 + 0 + 2 + (883 + 4 + 21 + (21*50)) = 1981
   */
  std::vector<int> expectLen{4083, 177, 4081, 1981};

  // check serilized message length
  auto oneMsg = serMsg.get();
  for (int i = 0; i < serMsg->countChainElements(); i++) {
    EXPECT_EQ(expectLen[i], oneMsg->length());
    oneMsg = oneMsg->next();
  }
}

TEST(Bgp2MessageSerializer, NexthopOffsetTrackingIPv4) {
  BgpUpdate2 update;
  populatePathAttributesV4(update);
  update.v4Announced2()->push_back(createRiggedIPPrefix("10.0.0.0/24", 0));

  std::vector<std::tuple<size_t, size_t, bool>> nexthopOffsets;
  auto iobuf = BgpMessageSerializer::serializeBgpUpdate2(
      update, true /*as4byte*/, false /*extNhEncoding*/, &nexthopOffsets);

  ASSERT_NE(iobuf, nullptr);
  ASSERT_EQ(nexthopOffsets.size(), 1);

  auto [bufferIndex, nexthopOffset, isV4] = nexthopOffsets[0];
  EXPECT_GT(nexthopOffset, 0);
  EXPECT_TRUE(isV4); // v4 nexthop

  auto buf = iobuf.get();
  for (size_t i = 0; i < bufferIndex && buf != nullptr; i++) {
    buf = buf->next();
  }
  ASSERT_NE(buf, nullptr);
  ASSERT_LE(nexthopOffset + 4, buf->length());

  const uint8_t* data = buf->data() + nexthopOffset;
  EXPECT_EQ(data[0], 1);
  EXPECT_EQ(data[1], 2);
  EXPECT_EQ(data[2], 3);
  EXPECT_EQ(data[3], 4);
}

TEST(Bgp2MessageSerializer, NexthopOffsetTrackingIPv6) {
  BgpUpdate2 update;
  update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment;
  segment.asSequence()->push_back(65001);
  update.attrs()->asPath()->push_back(segment);

  folly::IPAddress v6NextHop("2001:db8::1");
  *update.mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update.mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  update.mpAnnounced()->nexthop() = network::toBinaryAddress(v6NextHop);
  update.mpAnnounced()->prefixes()->push_back(
      createRiggedIPPrefix("2001:db8:1::/64", 0));

  std::vector<std::tuple<size_t, size_t, bool>> nexthopOffsets;
  auto iobuf = BgpMessageSerializer::serializeBgpUpdate2(
      update, true /*as4byte*/, false /*extNhEncoding*/, &nexthopOffsets);

  ASSERT_NE(iobuf, nullptr);
  ASSERT_EQ(nexthopOffsets.size(), 1);

  auto [bufferIndex, nexthopOffset, isV4] = nexthopOffsets[0];
  EXPECT_GT(nexthopOffset, 0);
  EXPECT_FALSE(isV4); // v6 nexthop

  auto buf = iobuf.get();
  for (size_t i = 0; i < bufferIndex && buf != nullptr; i++) {
    buf = buf->next();
  }
  ASSERT_NE(buf, nullptr);
  ASSERT_LE(nexthopOffset + 16, buf->length());

  const uint8_t* data = buf->data() + nexthopOffset;
  auto extractedAddr =
      folly::IPAddressV6::tryFromBinary(folly::ByteRange(data, 16));
  ASSERT_TRUE(extractedAddr.hasValue());
  EXPECT_EQ(extractedAddr.value(), v6NextHop);
}

TEST(Bgp2MessageSerializer, NexthopOffsetTrackingExtNhEncoding) {
  BgpUpdate2 update;
  update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment;
  segment.asSequence()->push_back(65001);
  update.attrs()->asPath()->push_back(segment);

  folly::IPAddress v6NextHop("fe80::1");
  *update.mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv4;
  *update.mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  update.mpAnnounced()->nexthop() = network::toBinaryAddress(v6NextHop);
  update.mpAnnounced()->prefixes()->push_back(
      createRiggedIPPrefix("192.168.1.0/24", 0));

  std::vector<std::tuple<size_t, size_t, bool>> nexthopOffsets;
  auto iobuf = BgpMessageSerializer::serializeBgpUpdate2(
      update, true /*as4byte*/, true /*extNhEncoding*/, &nexthopOffsets);

  ASSERT_NE(iobuf, nullptr);
  ASSERT_EQ(nexthopOffsets.size(), 1);

  auto [bufferIndex, nexthopOffset, isV4] = nexthopOffsets[0];
  EXPECT_GT(nexthopOffset, 0);
  EXPECT_FALSE(isV4); // v6 nexthop

  auto buf = iobuf.get();
  for (size_t i = 0; i < bufferIndex && buf != nullptr; i++) {
    buf = buf->next();
  }
  ASSERT_NE(buf, nullptr);
  ASSERT_LE(nexthopOffset + 16, buf->length());

  const uint8_t* data = buf->data() + nexthopOffset;
  auto extractedAddr =
      folly::IPAddressV6::tryFromBinary(folly::ByteRange(data, 16));
  ASSERT_TRUE(extractedAddr.hasValue());
  EXPECT_EQ(extractedAddr.value(), v6NextHop);
}

/*
 * Test multi-message nexthop tracking where a single BgpUpdate2 creates
 * multiple BGP UPDATE messages with different nexthop types (v4 and v6)
 */
TEST(BgpMessageSerializer, NexthopOffsetTrackingMultiMessage) {
  BgpUpdate2 update;
  update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_EGP;
  BgpAttrAsPathSegment segment;
  segment.asSequence()->push_back(32934);
  update.attrs()->asPath()->push_back(segment);
  *update.v4Nexthop() = network::toBinaryAddress(folly::IPAddress("1.2.3.4"));
  update.attrs()->med() = 32;
  update.attrs()->isMedSet() = true;
  update.attrs()->localPref() = 100;

  // v4Announced - will create a message with v4 nexthop
  RiggedIPPrefix p1;
  *p1.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("6.5.4.3/32"));
  update.v4Announced2()->push_back(p1);

  // mpAnnounced - will create a message with v6 nexthop
  folly::IPAddress v6NextHop("fd00::1");
  *update.mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update.mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  update.mpAnnounced()->nexthop() = network::toBinaryAddress(v6NextHop);
  RiggedIPPrefix prefix1;
  *prefix1.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("fd00::3000/122"));
  update.mpAnnounced()->prefixes()->push_back(prefix1);

  // mpWithdrawn - no nexthop (withdraw-only message)
  *update.mpWithdrawn()->afi() = BgpUpdateAfi::AFI_IPv4;
  *update.mpWithdrawn()->safi() = BgpUpdateSafi::SAFI_LABELED_UNICAST;
  RiggedIPPrefix prefix;
  *prefix.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("4.5.6.0/24"));
  *prefix.labels() = {8, 9};
  update.mpWithdrawn()->prefixes()->push_back(prefix);

  std::vector<std::tuple<size_t, size_t, bool>> nexthopOffsets;
  auto iobufPtr = BgpMessageSerializer::serializeBgpUpdate2(
      update, true /*as4byte*/, false /*extNhEncoding*/, &nexthopOffsets);

  ASSERT_NE(iobufPtr, nullptr);

  /*
   * We expect 2 nexthop offsets: one for mpAnnounced (v6) and one for
   * v4Announced (v4). mpWithdrawn has no nexthop since it's withdraw-only.
   */
  ASSERT_EQ(nexthopOffsets.size(), 2);

  /*
   * Serialization order is: mpWithdrawn (buffer 0, no nexthop) ->
   * mpAnnounced (buffer 1, v6 nexthop) -> v4Announced (buffer 2, v4 nexthop)
   */

  // First nexthop should be v6 in buffer 1 (mpAnnounced)
  auto [bufIdx1, offset1, isV4_1] = nexthopOffsets[0];
  EXPECT_EQ(bufIdx1, 1);
  EXPECT_FALSE(isV4_1); // v6 nexthop
  EXPECT_GT(offset1, 0);

  // Second nexthop should be v4 in buffer 2 (v4Announced)
  auto [bufIdx2, offset2, isV4_2] = nexthopOffsets[1];
  EXPECT_EQ(bufIdx2, 2);
  EXPECT_TRUE(isV4_2); // v4 nexthop
  EXPECT_GT(offset2, 0);

  // Verify v6 nexthop extraction
  auto buf = iobufPtr.get();
  for (size_t i = 0; i < bufIdx1 && buf != nullptr; i++) {
    buf = buf->next();
  }
  ASSERT_NE(buf, nullptr);
  ASSERT_LE(offset1 + 16, buf->length());
  const uint8_t* v6Data = buf->data() + offset1;
  auto extractedV6 =
      folly::IPAddressV6::tryFromBinary(folly::ByteRange(v6Data, 16));
  ASSERT_TRUE(extractedV6.hasValue());
  EXPECT_EQ(extractedV6.value(), v6NextHop);

  // Verify v4 nexthop extraction
  buf = iobufPtr.get();
  for (size_t i = 0; i < bufIdx2 && buf != nullptr; i++) {
    buf = buf->next();
  }
  ASSERT_NE(buf, nullptr);
  ASSERT_LE(offset2 + 4, buf->length());
  const uint8_t* v4Data = buf->data() + offset2;
  auto extractedV4 =
      folly::IPAddressV4::tryFromBinary(folly::ByteRange(v4Data, 4));
  ASSERT_TRUE(extractedV4.hasValue());
  EXPECT_EQ(extractedV4.value(), folly::IPAddressV4("1.2.3.4"));
}

// Test boundary conditions with IPv4 prefixes and path IDs
TEST(BgpMessageSerializer, BgpUpdate2IPv4WithPathIdsBoundaryTest) {
  BgpUpdate2 update;
  // Small path attributes for maximizing prefix space
  *update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment;
  segment.asSequence()->push_back(65001);
  update.attrs()->asPath()->push_back(segment);
  *update.v4Nexthop() = network::toBinaryAddress(folly::IPAddress("10.0.0.1"));

  /*
   * Path attrs: origin(4) + asPath(~7) + nexthop(7) = ~18 bytes
   * Available space: 4096 - 19(header) - 2(withdrawn) - 2(attr_len) = 4073
   * After attrs: 4073 - 18 = 4055 bytes
   * IPv4 prefix with path ID: 4(pathId) + 1(len) + 4(prefix) = 9 bytes max
   * Can fit approximately: 4055 / 9 = ~450 prefixes
   */

  // Add exactly 450 prefixes to test near-boundary packing
  for (int i = 0; i < 449; i++) {
    auto prefix = createRiggedIPPrefix(
        fmt::format("192.168.{}.{}/32", i / 256, i % 256), 1000 + i);
    update.v4Announced2()->push_back(prefix);
  }

  auto serMsg = BgpMessageSerializer::serializeBgpUpdate2(update, true);

  // Should fit in 1 message with dynamic packing
  EXPECT_EQ(1, serMsg->countChainElements());

  // Parse and verify
  BgpCapabilities capabilities;
  *capabilities.as4byte() = true;
  auto& addPath = *capabilities.addPathCapabilities();
  addPath.resize(1);
  *addPath[0].afi() = BgpUpdateAfi::AFI_IPv4;
  *addPath[0].safi() = BgpUpdateSafi::SAFI_UNICAST;
  *addPath[0].sor() = BgpAddPathSendRec::BOTH;

  auto msgBuf = folly::IOBuf::wrapBuffer(serMsg->data(), serMsg->length());
  auto parsedMsg = std::get<std::shared_ptr<const BgpUpdate2>>(
      BgpMessageParser2::parseBgpUpdateRaw(*msgBuf, capabilities));

  EXPECT_EQ(449, parsedMsg->v4Announced2()->size());
  EXPECT_EQ(update.v4Announced2()->size(), parsedMsg->v4Announced2()->size());
}

// Test IPv4 prefixes forcing message split at boundary
TEST(BgpMessageSerializer, BgpUpdate2IPv4WithPathIdsSplitBoundaryTest) {
  BgpUpdate2 update;
  // Larger path attributes to force splits
  *update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment;
  for (int i = 0; i < 30; i++) {
    segment.asSequence()->push_back(65000 + i);
  }
  update.attrs()->asPath()->push_back(segment);
  *update.v4Nexthop() = network::toBinaryAddress(folly::IPAddress("10.0.0.1"));
  update.attrs()->med() = 100;
  update.attrs()->isMedSet() = true;
  update.attrs()->localPref() = 200;

  // Add 600 prefixes with path IDs - should require multiple messages
  for (int i = 0; i < 600; i++) {
    auto prefix = createRiggedIPPrefix(
        fmt::format("10.{}.{}.0/24", i / 256, i % 256), 2000 + i);
    update.v4Announced2()->push_back(prefix);
  }

  auto serMsg = BgpMessageSerializer::serializeBgpUpdate2(update, true);

  // Should split into multiple messages
  EXPECT_GT(serMsg->countChainElements(), 1);

  // Verify all messages are within size limit
  auto oneMsg = serMsg.get();
  int totalPrefixes = 0;
  for (int i = 0; i < serMsg->countChainElements(); i++) {
    EXPECT_LE(oneMsg->length(), kMaxBgpMsgLen);
    EXPECT_GE(oneMsg->length(), kMinBgpUpdateMsgLen);

    // Parse each message
    BgpCapabilities capabilities;
    *capabilities.as4byte() = true;
    auto& addPath = *capabilities.addPathCapabilities();
    addPath.resize(1);
    *addPath[0].afi() = BgpUpdateAfi::AFI_IPv4;
    *addPath[0].safi() = BgpUpdateSafi::SAFI_UNICAST;
    *addPath[0].sor() = BgpAddPathSendRec::BOTH;

    auto msgBuf = folly::IOBuf::wrapBuffer(oneMsg->data(), oneMsg->length());
    auto parsedMsg = std::get<std::shared_ptr<const BgpUpdate2>>(
        BgpMessageParser2::parseBgpUpdateRaw(*msgBuf, capabilities));
    totalPrefixes += parsedMsg->v4Announced2()->size();

    oneMsg = oneMsg->next();
  }

  EXPECT_EQ(600, totalPrefixes);
}

// Test IPv6 prefixes with path IDs at boundary
TEST(BgpMessageSerializer, BgpUpdate2IPv6WithPathIdsBoundaryTest) {
  BgpUpdate2 update;
  // Minimal path attributes
  *update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment;
  segment.asSequence()->push_back(65001);
  update.attrs()->asPath()->push_back(segment);

  *update.mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update.mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  *update.mpAnnounced()->nexthop() =
      network::toBinaryAddress(folly::IPAddress("2001:db8::1"));

  /*
   * IPv6 prefix with path ID: 4(pathId) + 1(len) + 16(prefix) = 21 bytes max
   * Available space after headers and attrs: ~4050 bytes
   * Can fit approximately: 4050 / 21 = ~192 prefixes
   */

  // Add exactly 192 prefixes to test boundary
  for (int i = 0; i < 192; i++) {
    RiggedIPPrefix prefix;
    *prefix.prefix() = network::toIPPrefix(
        folly::IPAddress::createNetwork(fmt::format("2001:db8:{}::/64", i)));
    prefix.pathId() = 3000 + i;
    update.mpAnnounced()->prefixes()->push_back(prefix);
  }

  auto serMsg = BgpMessageSerializer::serializeBgpUpdate2(update, true);

  // Should fit in 1 message
  EXPECT_EQ(1, serMsg->countChainElements());

  // Verify size is under limit
  EXPECT_LE(serMsg->length(), kMaxBgpMsgLen);

  // Parse and verify
  BgpCapabilities capabilities;
  *capabilities.mpExtV6Unicast() = true;
  *capabilities.as4byte() = true;
  auto& addPath = *capabilities.addPathCapabilities();
  addPath.resize(1);
  *addPath[0].afi() = BgpUpdateAfi::AFI_IPv6;
  *addPath[0].safi() = BgpUpdateSafi::SAFI_UNICAST;
  *addPath[0].sor() = BgpAddPathSendRec::BOTH;

  auto msgBuf = folly::IOBuf::wrapBuffer(serMsg->data(), serMsg->length());
  auto parsedMsg = std::get<std::shared_ptr<const BgpUpdate2>>(
      BgpMessageParser2::parseBgpUpdateRaw(*msgBuf, capabilities));

  EXPECT_EQ(192, parsedMsg->mpAnnounced()->prefixes()->size());
}

// Test IPv6 prefixes forcing split with path IDs
TEST(BgpMessageSerializer, BgpUpdate2IPv6WithPathIdsSplitTest) {
  BgpUpdate2 update;
  populatePathAttributesV6(update);

  *update.mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update.mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  *update.mpAnnounced()->nexthop() =
      network::toBinaryAddress(folly::IPAddress("2001:db8::1"));

  // Add 300 IPv6 prefixes with path IDs - should require multiple messages
  for (int i = 0; i < 300; i++) {
    RiggedIPPrefix prefix;
    *prefix.prefix() = network::toIPPrefix(
        folly::IPAddress::createNetwork(fmt::format("2001:db8:{:x}::/48", i)));
    prefix.pathId() = 4000 + i;
    update.mpAnnounced()->prefixes()->push_back(prefix);
  }

  auto serMsg = BgpMessageSerializer::serializeBgpUpdate2(update, true);

  // Should split into multiple messages due to large attrs
  EXPECT_GT(serMsg->countChainElements(), 1);

  // Verify all messages and count prefixes
  auto oneMsg = serMsg.get();
  int totalPrefixes = 0;
  for (int i = 0; i < serMsg->countChainElements(); i++) {
    EXPECT_LE(oneMsg->length(), kMaxBgpMsgLen);
    EXPECT_GE(oneMsg->length(), kMinBgpUpdateMsgLen);

    BgpCapabilities capabilities;
    *capabilities.mpExtV6Unicast() = true;
    *capabilities.as4byte() = true;
    auto& addPath = *capabilities.addPathCapabilities();
    addPath.resize(1);
    *addPath[0].afi() = BgpUpdateAfi::AFI_IPv6;
    *addPath[0].safi() = BgpUpdateSafi::SAFI_UNICAST;
    *addPath[0].sor() = BgpAddPathSendRec::BOTH;

    auto msgBuf = folly::IOBuf::wrapBuffer(oneMsg->data(), oneMsg->length());
    auto parsedMsg = std::get<std::shared_ptr<const BgpUpdate2>>(
        BgpMessageParser2::parseBgpUpdateRaw(*msgBuf, capabilities));
    totalPrefixes += parsedMsg->mpAnnounced()->prefixes()->size();

    oneMsg = oneMsg->next();
  }

  EXPECT_EQ(300, totalPrefixes);
}

// Test exact boundary - message that fits exactly at 4096 bytes
TEST(BgpMessageSerializer, BgpUpdate2ExactBoundaryTest) {
  BgpUpdate2 update;
  // Minimal attributes
  *update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment;
  segment.asSequence()->push_back(65001);
  update.attrs()->asPath()->push_back(segment);
  *update.v4Nexthop() = network::toBinaryAddress(folly::IPAddress("1.1.1.1"));

  /*
   * Calculate exact number of prefixes to reach close to 4096
   * Header: 19, withdrawn: 2, attr_len: 2, path_attrs: ~18
   * Available: 4096 - 19 - 2 - 2 - 18 = 4055
   * Each prefix with pathId: 9 bytes
   * Prefixes: 4055 / 9 = 450 (leaving 5 bytes buffer)
   */

  for (int i = 0; i < 449; i++) {
    auto prefix =
        createRiggedIPPrefix(fmt::format("172.16.{}.1/32", i % 256), i);
    update.v4Announced2()->push_back(prefix);
  }

  auto serMsg = BgpMessageSerializer::serializeBgpUpdate2(update, true);

  EXPECT_EQ(1, serMsg->countChainElements());
  // Should be very close to 4096
  EXPECT_GE(serMsg->length(), 4080);
  EXPECT_LE(serMsg->length(), kMaxBgpMsgLen);
}

// Test mixed IPv4 withdrawn and announced with path IDs
TEST(BgpMessageSerializer, BgpUpdate2IPv4MixedWithPathIdsTest) {
  BgpUpdate2 update;
  *update.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment;
  segment.asSequence()->push_back(65100);
  update.attrs()->asPath()->push_back(segment);
  *update.v4Nexthop() = network::toBinaryAddress(folly::IPAddress("10.1.1.1"));

  // Add 300 withdrawn prefixes with path IDs
  for (int i = 0; i < 300; i++) {
    auto prefix =
        createRiggedIPPrefix(fmt::format("192.0.{}.0/24", i % 256), 5000 + i);
    update.v4Withdrawn2()->push_back(prefix);
  }

  // Add 300 announced prefixes with path IDs
  for (int i = 0; i < 300; i++) {
    auto prefix =
        createRiggedIPPrefix(fmt::format("10.10.{}.0/24", i % 256), 6000 + i);
    update.v4Announced2()->push_back(prefix);
  }

  auto serMsg = BgpMessageSerializer::serializeBgpUpdate2(update, true);

  // Should split into multiple messages
  EXPECT_GT(serMsg->countChainElements(), 1);

  // Count withdrawn and announced
  auto oneMsg = serMsg.get();
  int totalWithdrawn = 0;
  int totalAnnounced = 0;

  for (int i = 0; i < serMsg->countChainElements(); i++) {
    EXPECT_LE(oneMsg->length(), kMaxBgpMsgLen);

    BgpCapabilities capabilities;
    *capabilities.as4byte() = true;
    auto& addPath = *capabilities.addPathCapabilities();
    addPath.resize(1);
    *addPath[0].afi() = BgpUpdateAfi::AFI_IPv4;
    *addPath[0].safi() = BgpUpdateSafi::SAFI_UNICAST;
    *addPath[0].sor() = BgpAddPathSendRec::BOTH;

    auto msgBuf = folly::IOBuf::wrapBuffer(oneMsg->data(), oneMsg->length());
    auto parsedMsg = std::get<std::shared_ptr<const BgpUpdate2>>(
        BgpMessageParser2::parseBgpUpdateRaw(*msgBuf, capabilities));

    totalWithdrawn += parsedMsg->v4Withdrawn2()->size();
    totalAnnounced += parsedMsg->v4Announced2()->size();

    oneMsg = oneMsg->next();
  }

  EXPECT_EQ(300, totalWithdrawn);
  EXPECT_EQ(300, totalAnnounced);
}

// Test mixed IPv6 withdrawn and announced with path IDs
TEST(BgpMessageSerializer, BgpUpdate2IPv6MixedWithPathIdsTest) {
  BgpUpdate2 update;
  populatePathAttributesV6(update);

  *update.mpWithdrawn()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update.mpWithdrawn()->safi() = BgpUpdateSafi::SAFI_UNICAST;

  *update.mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update.mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  *update.mpAnnounced()->nexthop() =
      network::toBinaryAddress(folly::IPAddress("2001:db8::1"));

  // Add 150 withdrawn IPv6 prefixes with path IDs
  for (int i = 0; i < 150; i++) {
    RiggedIPPrefix prefix;
    *prefix.prefix() = network::toIPPrefix(
        folly::IPAddress::createNetwork(
            fmt::format("2001:db8:1000:{:x}::/64", i)));
    prefix.pathId() = 7000 + i;
    update.mpWithdrawn()->prefixes()->push_back(prefix);
  }

  // Add 150 announced IPv6 prefixes with path IDs
  for (int i = 0; i < 150; i++) {
    RiggedIPPrefix prefix;
    *prefix.prefix() = network::toIPPrefix(
        folly::IPAddress::createNetwork(
            fmt::format("2001:db8:2000:{:x}::/64", i)));
    prefix.pathId() = 8000 + i;
    update.mpAnnounced()->prefixes()->push_back(prefix);
  }

  auto serMsg = BgpMessageSerializer::serializeBgpUpdate2(update, true);

  // Should split into multiple messages
  EXPECT_GT(serMsg->countChainElements(), 1);

  // Verify counts
  auto oneMsg = serMsg.get();
  int totalWithdrawn = 0;
  int totalAnnounced = 0;

  for (int i = 0; i < serMsg->countChainElements(); i++) {
    EXPECT_LE(oneMsg->length(), kMaxBgpMsgLen);

    BgpCapabilities capabilities;
    *capabilities.mpExtV6Unicast() = true;
    *capabilities.as4byte() = true;
    auto& addPath = *capabilities.addPathCapabilities();
    addPath.resize(1);
    *addPath[0].afi() = BgpUpdateAfi::AFI_IPv6;
    *addPath[0].safi() = BgpUpdateSafi::SAFI_UNICAST;
    *addPath[0].sor() = BgpAddPathSendRec::BOTH;

    auto msgBuf = folly::IOBuf::wrapBuffer(oneMsg->data(), oneMsg->length());
    auto parsedMsg = std::get<std::shared_ptr<const BgpUpdate2>>(
        BgpMessageParser2::parseBgpUpdateRaw(*msgBuf, capabilities));

    totalWithdrawn += parsedMsg->mpWithdrawn()->prefixes()->size();
    totalAnnounced += parsedMsg->mpAnnounced()->prefixes()->size();

    oneMsg = oneMsg->next();
  }

  EXPECT_EQ(150, totalWithdrawn);
  EXPECT_EQ(150, totalAnnounced);
}

// Test single prefix per message edge case
TEST(BgpMessageSerializer, BgpUpdate2SinglePrefixPerMessageTest) {
  BgpUpdate2 update;
  // Very large path attributes leaving minimal space
  populatePathAttributesV4(update);

  // Add just 5 prefixes with path IDs
  for (int i = 0; i < 5; i++) {
    auto prefix =
        createRiggedIPPrefix(fmt::format("192.168.1.{}/32", i), 9000 + i);
    update.v4Announced2()->push_back(prefix);
  }

  auto serMsg = BgpMessageSerializer::serializeBgpUpdate2(update, true);

  // With large attrs, dynamic packing should still work efficiently
  EXPECT_GE(serMsg->countChainElements(), 1);

  // Verify all prefixes are serialized
  auto oneMsg = serMsg.get();
  int totalPrefixes = 0;

  for (int i = 0; i < serMsg->countChainElements(); i++) {
    EXPECT_LE(oneMsg->length(), kMaxBgpMsgLen);

    BgpCapabilities capabilities;
    *capabilities.as4byte() = true;
    auto& addPath = *capabilities.addPathCapabilities();
    addPath.resize(1);
    *addPath[0].afi() = BgpUpdateAfi::AFI_IPv4;
    *addPath[0].safi() = BgpUpdateSafi::SAFI_UNICAST;
    *addPath[0].sor() = BgpAddPathSendRec::BOTH;

    auto msgBuf = folly::IOBuf::wrapBuffer(oneMsg->data(), oneMsg->length());
    auto parsedMsg = std::get<std::shared_ptr<const BgpUpdate2>>(
        BgpMessageParser2::parseBgpUpdateRaw(*msgBuf, capabilities));
    totalPrefixes += parsedMsg->v4Announced2()->size();

    oneMsg = oneMsg->next();
  }

  EXPECT_EQ(5, totalPrefixes);
}

} // namespace bgplib
} // namespace nettools
} // namespace facebook
