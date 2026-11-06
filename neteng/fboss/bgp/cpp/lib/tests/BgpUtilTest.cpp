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

#include <folly/ExceptionString.h>
#include <folly/IPAddress.h>
#include <folly/io/IOBuf.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/init/Init.h>
#include "common/network/AddressUtil.h"
#include "neteng/fboss/bgp/cpp/lib/BgpMessageParser.h"
#include "neteng/fboss/bgp/cpp/lib/BgpMessageSerializer.h"
#include "neteng/fboss/bgp/cpp/lib/BgpUtil.h"
#include "neteng/fboss/bgp/if/gen-cpp2/BgpStructs_types.h"

namespace facebook {
namespace nettools {

namespace bgplib {

// capabilities used to parse raw BgpUpdate2 mesaages
BgpCapabilities capabilities;

// helper method to set BgpCapabilities
BgpCapabilities setCapa(bool allTrue) {
  BgpCapabilities capa;
  *capa.mpExtV4Unicast() = allTrue;
  *capa.mpExtV6Unicast() = allTrue;
  *capa.mpExtV4LU() = allTrue;
  *capa.mpExtV6LU() = allTrue;
  *capa.mpExtLs() = allTrue;
  *capa.as4byte() = allTrue;
  *capa.gracefulRestart() = allTrue;
  *capa.mpExtExist() = allTrue;
  return capa;
}

void init() {
  capabilities = setCapa(true);
}

TEST(BgpUtil, bgpUpdate2ToBgpUpdateTest) {
  BgpUpdate2 update2;
  *update2.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_EGP;
  BgpAttrAsPathSegment segment;
  segment.asSequence()->push_back(32934);
  update2.attrs()->asPath()->push_back(segment);
  *update2.v4Nexthop() = network::toBinaryAddress(folly::IPAddress("1.2.3.4"));
  update2.attrs()->med() = 32;
  update2.attrs()->isMedSet() = true;
  // v4Announced
  update2.v4Announced()->push_back(
      network::toIPPrefix(folly::IPAddress::createNetwork("6.5.4.3/32")));
  // v4Withdrawn
  update2.v4Withdrawn()->push_back(
      network::toIPPrefix(folly::IPAddress::createNetwork("3.4.5.6/32")));
  // mpAnnounced
  *update2.mpAnnounced()->afi() = BgpUpdateAfi::AFI_IPv6;
  *update2.mpAnnounced()->safi() = BgpUpdateSafi::SAFI_UNICAST;
  *update2.mpAnnounced()->nexthop() =
      network::toBinaryAddress(folly::IPAddress("fd00::1"));
  RiggedIPPrefix prefix1;
  *prefix1.prefix() = network::toIPPrefix(
      folly::IPAddress::createNetwork("dead:beef:face:b00c::/64"));
  update2.mpAnnounced()->prefixes()->push_back(prefix1);
  // mpWithdrawn
  *update2.mpWithdrawn()->afi() = BgpUpdateAfi::AFI_IPv4;
  *update2.mpWithdrawn()->safi() = BgpUpdateSafi::SAFI_LABELED_UNICAST;
  RiggedIPPrefix prefix2;
  *prefix2.prefix() =
      network::toIPPrefix(folly::IPAddress::createNetwork("4.5.6.0/24"));
  *prefix2.labels() = {8, 9};
  update2.mpWithdrawn()->prefixes()->push_back(prefix2);

  // different cases: set/unset optional fields

  // default case
  {
    /*
     * Serialization sequence is v4Withdrawn -> mpWithdrawn -> mpAnnounced ->
     * v4Announced
     */
    auto msgs = BgpMessageSerializer::serializeBgpUpdate2(update2, true);
    EXPECT_EQ(4, msgs->countChainElements());

    auto msg = msgs.get();
    std::vector<facebook::nettools::bgplib::BgpUpdate> expected;
    for (int i = 0; i < 4; i++) {
      auto ioBuf = folly::IOBuf::wrapBuffer(msg->data(), msg->length());
      auto updates = toBgpUpdate(
          BgpMessageParser2::parseBgpUpdateRaw(*ioBuf, capabilities));
      EXPECT_EQ(1, updates.size());
      expected.emplace_back(updates[0]);
      msg = msg->next();
    }

    // convert sequence is in same order
    auto actual = toBgpUpdate(update2);
    EXPECT_EQ(4, actual.size());

    EXPECT_EQ(expected, actual);
  }
  // set localPref in given update
  {
    auto update2Copy = update2;
    update2Copy.attrs()->localPref() = 100;

    /*
     * Serialization sequence is v4Withdrawn -> mpWithdrawn -> mpAnnounced ->
     * v4Announced
     */
    auto msgs = BgpMessageSerializer::serializeBgpUpdate2(update2Copy, true);
    EXPECT_EQ(4, msgs->countChainElements());

    auto msg = msgs.get();
    std::vector<facebook::nettools::bgplib::BgpUpdate> expected;
    for (int i = 0; i < 4; i++) {
      auto ioBuf = folly::IOBuf::wrapBuffer(msg->data(), msg->length());
      auto updates = toBgpUpdate(
          BgpMessageParser2::parseBgpUpdateRaw(*ioBuf, capabilities));
      EXPECT_EQ(1, updates.size());
      expected.emplace_back(updates[0]);
      msg = msg->next();
    }

    // convert sequence is in same order
    auto actual = toBgpUpdate(update2Copy);
    EXPECT_EQ(4, actual.size());

    EXPECT_EQ(expected, actual);
  }
}

TEST(BgpUtil, bgpEndOfRibToBgpUpdateTest) {
  BgpEndOfRib eor4;
  *eor4.isMpEor() = false;
  *eor4.afi() = BgpUpdateAfi::AFI_IPv4;
  *eor4.safi() = BgpUpdateSafi::SAFI_UNICAST;
  auto updatev4 = toBgpUpdate(eor4);
  EXPECT_EQ(BgpUpdateType::BU_ENDOFRIB, *updatev4.type());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv4, *updatev4.afi());
  EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *updatev4.safi());

  BgpEndOfRib eor6;
  *eor6.isMpEor() = true;
  *eor6.afi() = BgpUpdateAfi::AFI_IPv6;
  *eor6.safi() = BgpUpdateSafi::SAFI_UNICAST;
  auto updatev6 = toBgpUpdate(eor6);
  EXPECT_EQ(BgpUpdateType::BU_ENDOFRIB, *updatev6.type());
  EXPECT_EQ(BgpUpdateAfi::AFI_IPv6, *updatev6.afi());
  EXPECT_EQ(BgpUpdateSafi::SAFI_UNICAST, *updatev6.safi());
}

TEST(BgpUtil, BgpUpdate2toBgpAttributesCTest) {
  // Sample update with all fields filled in
  BgpUpdate2 updateGiven;
  *updateGiven.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment1;
  segment1.asSequence()->push_back(32934);
  updateGiven.attrs()->asPath()->push_back(segment1);
  BgpAttrAsPathSegment segment2;
  segment2.asConfedSequence()->push_back(1234);
  updateGiven.attrs()->asPath()->push_back(segment2);
  BgpAttrAsPathSegment segment3;
  segment3.asSet()->emplace(456);
  updateGiven.attrs()->asPath()->push_back(segment3);
  BgpAttrAsPathSegment segment4;
  segment4.asConfedSet()->emplace(789);
  updateGiven.attrs()->asPath()->push_back(segment2);
  updateGiven.attrs()->med() = 32;
  updateGiven.attrs()->isMedSet() = true;
  *updateGiven.attrs()->atomicAggregate() = true;
  *updateGiven.attrs()->aggregator()->asn() = 4660;
  *updateGiven.attrs()->aggregator()->ip() = "3.4.5.6";
  BgpAttrCommunity community;
  *community.asn() = 65530;
  *community.value() = 15800;
  updateGiven.attrs()->communities()->push_back(community);
  *updateGiven.attrs()->originatorId() = 0x00000786; // ip: "134.7.0.0""
  updateGiven.attrs()->clusterList()->push_back(0x00000110); // ip: "16.1.0.0"
  updateGiven.attrs()->clusterList()->push_back(0x00000786); // ip: "134.7.0.0"
  BgpAttrExtCommunity extCommunity;
  *extCommunity.firstWord() = 0x2272a;
  *extCommunity.secondWord() = 0x232f;
  updateGiven.attrs()->extCommunities()->push_back(extCommunity);

  // different cases: set/unset optional fields

  // default case
  {
    // Converting updateGiven to shared_ptr<BgpAttributesC>
    auto attrsC = BgpUpdate2toBgpAttributesC(updateGiven);
    // Converting shared_ptr<BgpAttributesC> back to BgpUpdate2
    auto updateReturned = BgpAttributesCtoBgpUpdate2(attrsC);
    // Verifying all attributes match the input after converting and back
    EXPECT_EQ(*updateGiven.attrs(), *updateReturned.attrs());
  }
  // set localPref in given update
  {
    auto updateGivenCopy = updateGiven;
    updateGivenCopy.attrs()->localPref() = 100;

    // Converting updateGivenCopy to shared_ptr<BgpAttributesC>
    auto attrsC = BgpUpdate2toBgpAttributesC(updateGivenCopy);
    // Converting shared_ptr<BgpAttributesC> back to BgpUpdate2
    auto updateReturned = BgpAttributesCtoBgpUpdate2(attrsC);
    // Verifying all attributes match the input after converting and back
    EXPECT_EQ(*updateGivenCopy.attrs(), *updateReturned.attrs());
  }
}

TEST(BgpUtil, BgpUpdate2toBgpPathCTest) {
  // Sample update with all fields filled in
  BgpUpdate2 updateGiven;
  updateGiven.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment1;
  segment1.asSequence()->push_back(32934);
  updateGiven.attrs()->asPath()->push_back(segment1);
  BgpAttrAsPathSegment segment2;
  segment2.asConfedSequence()->push_back(1234);
  updateGiven.attrs()->asPath()->push_back(segment2);
  BgpAttrAsPathSegment segment3;
  segment3.asSet()->emplace(456);
  updateGiven.attrs()->asPath()->push_back(segment3);
  BgpAttrAsPathSegment segment4;
  segment4.asConfedSet()->emplace(789);
  updateGiven.attrs()->asPath()->push_back(segment2);
  updateGiven.attrs()->nexthop() = "1.2.3.4";
  updateGiven.attrs()->med() = 32;
  updateGiven.attrs()->isMedSet() = true;
  updateGiven.attrs()->atomicAggregate() = true;
  updateGiven.attrs()->aggregator()->asn() = 4660;
  updateGiven.attrs()->aggregator()->ip() = "3.4.5.6";
  BgpAttrCommunity community;
  community.asn() = 65530;
  community.value() = 15800;
  updateGiven.attrs()->communities()->push_back(community);
  updateGiven.attrs()->originatorId() = 0x00000786; // ip: "134.7.0.0""
  updateGiven.attrs()->clusterList()->push_back(0x00000110); // ip: "16.1.0.0"
  updateGiven.attrs()->clusterList()->push_back(0x00000786); // ip: "134.7.0.0"
  BgpAttrExtCommunity extCommunity;
  extCommunity.firstWord() = 0x2272a;
  extCommunity.secondWord() = 0x232f;
  updateGiven.attrs()->extCommunities()->push_back(extCommunity);

  // different cases: set/unset optional fields

  // default case
  {
    // Converting updateGiven to shared_ptr<BgpPathC>
    auto attrsC = BgpUpdate2toBgpPathC(updateGiven);
    // Converting shared_ptr<BgpPathC> back to BgpUpdate2
    auto updateReturned = BgpPathCtoBgpUpdate2(attrsC);
    // Verifying all attributes match the input after converting and back
    EXPECT_EQ(*updateGiven.attrs(), *updateReturned.attrs());
  }
  // set localPref in given update
  {
    auto updateGivenCopy = updateGiven;
    updateGivenCopy.attrs()->localPref() = 100;

    // Converting updateGivenCopy to shared_ptr<BgpPathC>
    auto attrsC = BgpUpdate2toBgpPathC(updateGivenCopy);
    // Converting shared_ptr<BgpPathC> back to BgpUpdate2
    auto updateReturned = BgpPathCtoBgpUpdate2(attrsC);
    // Verifying all attributes match the input after converting and back
    EXPECT_EQ(*updateGivenCopy.attrs(), *updateReturned.attrs());
  }
}

// Test operator==, !=
TEST(BgpUtil, BgpAttributesCOperatorTest) {
  // Sample update with all fields filled in
  BgpUpdate2 updateGiven;
  *updateGiven.attrs()->origin() = BgpAttrOrigin::BGP_ORIGIN_IGP;
  BgpAttrAsPathSegment segment1;
  segment1.asSequence()->push_back(32934);
  updateGiven.attrs()->asPath()->push_back(segment1);
  BgpAttrAsPathSegment segment2;
  segment2.asConfedSequence()->push_back(1234);
  updateGiven.attrs()->asPath()->push_back(segment2);
  BgpAttrAsPathSegment segment3;
  segment3.asSet()->emplace(456);
  updateGiven.attrs()->asPath()->push_back(segment3);
  BgpAttrAsPathSegment segment4;
  segment4.asConfedSet()->emplace(789);
  updateGiven.attrs()->asPath()->push_back(segment2);
  *updateGiven.attrs()->nexthop() = "1.2.3.4";
  updateGiven.attrs()->med() = 32;
  updateGiven.attrs()->isMedSet() = true;
  *updateGiven.attrs()->atomicAggregate() = true;
  *updateGiven.attrs()->aggregator()->asn() = 4660;
  *updateGiven.attrs()->aggregator()->ip() = "3.4.5.6";
  BgpAttrCommunity community;
  *community.asn() = 65530;
  *community.value() = 15800;
  updateGiven.attrs()->communities()->push_back(community);
  *updateGiven.attrs()->originatorId() = 0x86070000; // ip: "134.7.0.0""
  updateGiven.attrs()->clusterList()->push_back(0x10010000); // ip: "16.1.0.0"
  updateGiven.attrs()->clusterList()->push_back(0x86070000); // ip: "134.7.0.0"
  BgpAttrExtCommunity extCommunity;
  *extCommunity.firstWord() = 0x2272a;
  *extCommunity.secondWord() = 0x232f;
  updateGiven.attrs()->extCommunities()->push_back(extCommunity);

  // different cases: set/unset optional fields

  // default case
  {
    // Converting updateGiven to shared_ptr<BgpAttributesC>
    auto attrs1 = BgpUpdate2toBgpAttributesC(updateGiven);
    auto attrs2 = BgpUpdate2toBgpAttributesC(updateGiven);
    // Check that operator== correctly verifies all fields are same
    EXPECT_TRUE(*attrs1 == *attrs2);
    EXPECT_EQ(*attrs1, *attrs2); // uses operator !=

    // Modify few fields and see that operator== detects
    updateGiven.attrs()->med() = 67; // modify value
    updateGiven.attrs()->isMedSet() = true;
    attrs2 = BgpUpdate2toBgpAttributesC(updateGiven);
    EXPECT_FALSE(*attrs1 == *attrs2);
    EXPECT_TRUE(*attrs1 != *attrs2);
    updateGiven.attrs()->med() = 32; // restore value
    updateGiven.attrs()->isMedSet() = true;

    *updateGiven.attrs()->communities()[0].asn() = 11111;
    attrs2 = BgpUpdate2toBgpAttributesC(updateGiven);
    EXPECT_FALSE(*attrs1 == *attrs2);
    *updateGiven.attrs()->communities()[0].asn() = 65530;

    updateGiven.attrs()->asPath()[0].asSequence()[0] = 1111;
    attrs2 = BgpUpdate2toBgpAttributesC(updateGiven);
    EXPECT_FALSE(*attrs1 == *attrs2);
    updateGiven.attrs()->asPath()[0].asSequence()[0] = 32934;
  }
  // set localPref in given update
  {
    auto updateGivenCopy = updateGiven;
    updateGivenCopy.attrs()->localPref() = 100;

    // Converting updateGiven to shared_ptr<BgpAttributesC>
    auto attrs1 = BgpUpdate2toBgpAttributesC(updateGivenCopy);
    auto attrs2 = BgpUpdate2toBgpAttributesC(updateGivenCopy);
    // Check that operator== correctly verifies all fields are same
    EXPECT_TRUE(*attrs1 == *attrs2);
    EXPECT_EQ(*attrs1, *attrs2); // uses operator !=
  }
}

// This test case is for verifying negotiation result is as expected
TEST(BgpUtil, NegotiateCapabilitiesTest) {
  // set up capability
  BgpCapabilities allTrueCapa = setCapa(true);
  BgpCapabilities allFalseCapa = setCapa(false);

  /*
   * 1. myCapa all true, peerCapa all false, peer with mp capability
   * expect result to be all false
   */
  {
    // peer has mp capability
    *allFalseCapa.mpExtExist() = true;
    BgpCapabilities restult = negotiateCapabilities(allTrueCapa, allFalseCapa);
    EXPECT_FALSE(*restult.mpExtV4Unicast());
    EXPECT_FALSE(*restult.mpExtV6Unicast());
    EXPECT_FALSE(*restult.mpExtV4LU());
    EXPECT_FALSE(*restult.mpExtV6LU());
    EXPECT_FALSE(*restult.mpExtLs());
    EXPECT_FALSE(*restult.as4byte());
    EXPECT_FALSE(*restult.gracefulRestart());
  }
  /*
   * 2. myCapa all false, peerCapa all true
   * expect result to be all false
   */
  {
    BgpCapabilities restult = negotiateCapabilities(allFalseCapa, allTrueCapa);
    EXPECT_FALSE(*restult.mpExtV4Unicast());
    EXPECT_FALSE(*restult.mpExtV6Unicast());
    EXPECT_FALSE(*restult.mpExtV4LU());
    EXPECT_FALSE(*restult.mpExtV6LU());
    EXPECT_FALSE(*restult.mpExtLs());
    EXPECT_FALSE(*restult.as4byte());
    EXPECT_FALSE(*restult.gracefulRestart());
  }
  /*
   * 3. myCapa all true, peerCapa all true
   * expect result to be all True
   */
  {
    BgpCapabilities restult = negotiateCapabilities(allTrueCapa, allTrueCapa);
    EXPECT_TRUE(*restult.mpExtV4Unicast());
    EXPECT_TRUE(*restult.mpExtV6Unicast());
    EXPECT_TRUE(*restult.mpExtV4LU());
    EXPECT_TRUE(*restult.mpExtV6LU());
    EXPECT_TRUE(*restult.mpExtLs());
    EXPECT_TRUE(*restult.as4byte());
    EXPECT_TRUE(*restult.gracefulRestart());
  }
  /*
   * 4. myCapa all true, peerCapa all false but without mpExtExist
   * expect result to be v4 enabled, all other fields are false
   */
  {
    *allFalseCapa.mpExtExist() = false;
    BgpCapabilities restult = negotiateCapabilities(allTrueCapa, allFalseCapa);
    EXPECT_TRUE(*restult.mpExtV4Unicast());
    EXPECT_FALSE(*restult.mpExtV6Unicast());
    EXPECT_FALSE(*restult.mpExtV4LU());
    EXPECT_FALSE(*restult.mpExtV6LU());
    EXPECT_FALSE(*restult.mpExtLs());
    EXPECT_FALSE(*restult.as4byte());
    EXPECT_FALSE(*restult.gracefulRestart());
  }
}

// RFC 8538: N bit is negotiated only if both peers advertise it
TEST(BgpUtil, NegotiateGrNotificationTest) {
  BgpCapabilities withN = setCapa(true);
  *withN.grNotification() = true;
  BgpCapabilities withoutN = setCapa(true);

  EXPECT_TRUE(*negotiateCapabilities(withN, withN).grNotification());
  EXPECT_FALSE(*negotiateCapabilities(withN, withoutN).grNotification());
  EXPECT_FALSE(*negotiateCapabilities(withoutN, withN).grNotification());
  EXPECT_FALSE(*negotiateCapabilities(withoutN, withoutN).grNotification());
}

/*
 * This test case is for verifying negotiation result for Extended Next Hop
 * Encoding capabilities. RFC 5549
 */
TEST(BgpUtil, NegotiateExtNHEncodingCapabilities) {
  BgpExtNHEncodingCapability capability1;
  capability1.nlriAfi() = BgpUpdateAfi::AFI_IPv4;
  capability1.nlriSafi() = BgpUpdateSafi::SAFI_UNICAST;
  capability1.nhAfi() = BgpUpdateAfi::AFI_IPv6;
  BgpExtNHEncodingCapability capability2;
  capability2.nlriAfi() = BgpUpdateAfi::AFI_IPv4;
  capability2.nlriSafi() = BgpUpdateSafi::SAFI_LABELED_UNICAST;
  capability2.nhAfi() = BgpUpdateAfi::AFI_IPv6;
  std::vector<BgpExtNHEncodingCapability> myCapa;
  myCapa.push_back(std::move(capability1));
  myCapa.push_back(std::move(capability2));
  std::vector<BgpExtNHEncodingCapability> peer1Capa{
      myCapa.begin(), myCapa.end()};
  std::vector<BgpExtNHEncodingCapability> peer2Capa{myCapa.at(0)};

  auto ngtCapa1 = negotiateExtNHEncodingCapabilities(myCapa, peer1Capa);
  EXPECT_EQ(2, ngtCapa1.size());
  EXPECT_THAT(myCapa, ngtCapa1);

  auto ngtCapa2 = negotiateExtNHEncodingCapabilities(myCapa, peer2Capa);
  EXPECT_EQ(1, ngtCapa2.size());
  EXPECT_THAT(peer2Capa, ngtCapa2);
}

// basic test for ensuring builder function produces notification object
TEST(BgpUtil, BgpNotificationBuilderTest) {
  auto code = ::facebook::nettools::bgplib::BgpNotifErrCode::BN_MSG_HDR_ERR;
  auto subCode = BgpNotifMsgHdrErrSubCode::BN_MH_CONNECTION_NOT_SYNCHRONIZED;
  std::string subCodeStr = "a sub code string";
  std::string data = "some data";
  BgpNotification builtNotification = buildBgpNotification(
      code, static_cast<uint16_t>(subCode), subCodeStr, data);
  EXPECT_EQ(*builtNotification.errCode(), code);
  EXPECT_EQ(*builtNotification.errSubCode(), static_cast<uint16_t>(subCode));
  EXPECT_EQ(*builtNotification.errSubCodeStr(), subCodeStr);
  EXPECT_EQ(*builtNotification.data(), data);
}

// Tests for constructAnnounceInBgpUpdateFormat
TEST(BgpUtil, ConstructAnnounceInBgpUpdateFormatV4Test) {
  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/24");
  auto nexthop = folly::IPAddress("192.168.1.1");
  std::vector<uint32_t> asPath = {65001, 65002, 65003};
  BgpAttrCommunity community;
  community.asn() = 65530;
  community.value() = 100;
  std::vector<BgpAttrCommunity> communities = {community};

  auto update =
      constructAnnounceInBgpUpdateFormat(prefix, nexthop, asPath, communities);

  EXPECT_NE(update, nullptr);
  EXPECT_EQ(*update->type(), BgpUpdateType::BU_UPDATE);
  EXPECT_EQ(*update->afi(), BgpUpdateAfi::AFI_IPv4);
  EXPECT_EQ(*update->safi(), BgpUpdateSafi::SAFI_UNICAST);
  EXPECT_EQ(*update->prefix(), "10.0.0.0/24");
  EXPECT_EQ(*update->attrs()->origin(), BgpAttrOrigin::BGP_ORIGIN_IGP);
  EXPECT_EQ(*update->attrs()->nexthop(), "192.168.1.1");
  EXPECT_EQ(update->attrs()->asPath()->size(), 1);
  EXPECT_EQ(update->attrs()->asPath()->at(0).asSequence()->size(), 3);
  EXPECT_EQ(update->attrs()->communities()->size(), 1);
}

TEST(BgpUtil, ConstructAnnounceInBgpUpdateFormatV6Test) {
  auto prefix = folly::IPAddress::createNetwork("2001:db8::/32");
  auto nexthop = folly::IPAddress("2001:db8::1");
  std::vector<uint32_t> asPath = {65001};
  std::vector<BgpAttrCommunity> communities = {};

  auto update =
      constructAnnounceInBgpUpdateFormat(prefix, nexthop, asPath, communities);

  EXPECT_NE(update, nullptr);
  EXPECT_EQ(*update->type(), BgpUpdateType::BU_UPDATE);
  EXPECT_EQ(*update->afi(), BgpUpdateAfi::AFI_IPv6);
  EXPECT_EQ(*update->safi(), BgpUpdateSafi::SAFI_UNICAST);
  EXPECT_EQ(*update->prefix(), "2001:db8::/32");
  EXPECT_EQ(*update->attrs()->nexthop(), "2001:db8::1");
}

// Tests for constructWithdrawInBgpUpdateFormat
TEST(BgpUtil, ConstructWithdrawInBgpUpdateFormatV4Test) {
  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/24");

  auto update = constructWithdrawInBgpUpdateFormat(prefix);

  EXPECT_NE(update, nullptr);
  EXPECT_EQ(*update->type(), BgpUpdateType::BU_WITHDRAW);
  EXPECT_EQ(*update->afi(), BgpUpdateAfi::AFI_IPv4);
  EXPECT_EQ(*update->safi(), BgpUpdateSafi::SAFI_UNICAST);
  EXPECT_EQ(*update->prefix(), "10.0.0.0/24");
}

TEST(BgpUtil, ConstructWithdrawInBgpUpdateFormatV6Test) {
  auto prefix = folly::IPAddress::createNetwork("2001:db8::/32");

  auto update = constructWithdrawInBgpUpdateFormat(prefix);

  EXPECT_NE(update, nullptr);
  EXPECT_EQ(*update->type(), BgpUpdateType::BU_WITHDRAW);
  EXPECT_EQ(*update->afi(), BgpUpdateAfi::AFI_IPv6);
  EXPECT_EQ(*update->safi(), BgpUpdateSafi::SAFI_UNICAST);
  EXPECT_EQ(*update->prefix(), "2001:db8::/32");
}

// Tests for constructAnnounceInBgpUpdate2Format
TEST(BgpUtil, ConstructAnnounceInBgpUpdate2FormatV4Test) {
  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/24");
  auto nexthop = folly::IPAddress("192.168.1.1");
  std::vector<uint32_t> asPath = {65001, 65002};
  BgpAttrCommunity community;
  community.asn() = 65530;
  community.value() = 200;
  std::vector<BgpAttrCommunity> communities = {community};

  auto update =
      constructAnnounceInBgpUpdate2Format(prefix, nexthop, asPath, communities);

  EXPECT_NE(update, nullptr);
  EXPECT_EQ(update->v4Announced()->size(), 1);
  EXPECT_TRUE(update->mpAnnounced()->prefixes()->empty());
  EXPECT_EQ(*update->attrs()->origin(), BgpAttrOrigin::BGP_ORIGIN_IGP);
  EXPECT_EQ(update->attrs()->asPath()->size(), 1);
  EXPECT_EQ(update->attrs()->communities()->size(), 1);
  // Check nexthop is set via v4Nexthop
  EXPECT_EQ(
      network::toIPAddress(*update->v4Nexthop()),
      folly::IPAddress("192.168.1.1"));
}

TEST(BgpUtil, ConstructAnnounceInBgpUpdate2FormatV6Test) {
  auto prefix = folly::IPAddress::createNetwork("2001:db8::/32");
  auto nexthop = folly::IPAddress("2001:db8::1");
  std::vector<uint32_t> asPath = {65001};
  std::vector<BgpAttrCommunity> communities = {};

  auto update =
      constructAnnounceInBgpUpdate2Format(prefix, nexthop, asPath, communities);

  EXPECT_NE(update, nullptr);
  EXPECT_TRUE(update->v4Announced()->empty());
  EXPECT_EQ(update->mpAnnounced()->prefixes()->size(), 1);
  EXPECT_EQ(*update->mpAnnounced()->afi(), BgpUpdateAfi::AFI_IPv6);
  EXPECT_EQ(*update->mpAnnounced()->safi(), BgpUpdateSafi::SAFI_UNICAST);
  EXPECT_EQ(
      network::toIPAddress(*update->mpAnnounced()->nexthop()),
      folly::IPAddress("2001:db8::1"));
}

// Tests for constructWithdrawInBgpUpdate2Format
TEST(BgpUtil, ConstructWithdrawInBgpUpdate2FormatV4Test) {
  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/24");

  auto update = constructWithdrawInBgpUpdate2Format(prefix, false);

  EXPECT_NE(update, nullptr);
  EXPECT_EQ(update->v4Withdrawn()->size(), 1);
  EXPECT_TRUE(update->v4Withdrawn2()->empty());
  EXPECT_TRUE(update->mpWithdrawn()->prefixes()->empty());
}

TEST(BgpUtil, ConstructWithdrawInBgpUpdate2FormatV4WithToV2Test) {
  auto prefix = folly::IPAddress::createNetwork("10.0.0.0/24");

  auto update = constructWithdrawInBgpUpdate2Format(prefix, true);

  EXPECT_NE(update, nullptr);
  EXPECT_EQ(update->v4Withdrawn()->size(), 1);
  EXPECT_EQ(update->v4Withdrawn2()->size(), 1);
  EXPECT_TRUE(update->mpWithdrawn()->prefixes()->empty());
}

TEST(BgpUtil, ConstructWithdrawInBgpUpdate2FormatV6Test) {
  auto prefix = folly::IPAddress::createNetwork("2001:db8::/32");

  auto update = constructWithdrawInBgpUpdate2Format(prefix, false);

  EXPECT_NE(update, nullptr);
  EXPECT_TRUE(update->v4Withdrawn()->empty());
  EXPECT_TRUE(update->v4Withdrawn2()->empty());
  EXPECT_EQ(update->mpWithdrawn()->prefixes()->size(), 1);
  EXPECT_EQ(*update->mpWithdrawn()->afi(), BgpUpdateAfi::AFI_IPv6);
  EXPECT_EQ(*update->mpWithdrawn()->safi(), BgpUpdateSafi::SAFI_UNICAST);
}

// Tests for constructEndOfRib
TEST(BgpUtil, ConstructEndOfRibV4Test) {
  auto eor = constructEndOfRib(BgpUpdateAfi::AFI_IPv4);

  EXPECT_NE(eor, nullptr);
  EXPECT_FALSE(*eor->isMpEor());
  EXPECT_EQ(*eor->afi(), BgpUpdateAfi::AFI_IPv4);
  EXPECT_EQ(*eor->safi(), BgpUpdateSafi::SAFI_UNICAST);
}

TEST(BgpUtil, ConstructEndOfRibV6Test) {
  auto eor = constructEndOfRib(BgpUpdateAfi::AFI_IPv6);

  EXPECT_NE(eor, nullptr);
  EXPECT_TRUE(*eor->isMpEor());
  EXPECT_EQ(*eor->afi(), BgpUpdateAfi::AFI_IPv6);
  EXPECT_EQ(*eor->safi(), BgpUpdateSafi::SAFI_UNICAST);
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
