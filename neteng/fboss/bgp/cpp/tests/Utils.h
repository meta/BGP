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
#include <boost/filesystem.hpp>
#include <gtest/gtest.h>
#include <re2/re2.h>
#include <algorithm>

#include <folly/Overload.h>
#include <folly/logging/test/TestLogHandler.h>
#include <folly/logging/xlog.h>

#include <fb303/ThreadCachedServiceData.h>
#include "common/network/AddressUtil.h"
#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/rib_policy_types.h"
#include "configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h"
#include "neteng/fboss/bgp/cpp/common/BgpPath.h"
#include "neteng/fboss/bgp/cpp/common/Consts.h"
#include "neteng/fboss/bgp/cpp/common/RibMessage.h"
#include "neteng/fboss/bgp/cpp/common/RouteInfo.h"
#include "neteng/fboss/bgp/cpp/common/Structs.h"
#include "neteng/fboss/bgp/cpp/common/Types.h"
#include "neteng/fboss/bgp/cpp/config/ConfigUtils.h"
#include "neteng/fboss/bgp/cpp/lib/BgpUtil.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyAction.h"
#include "neteng/fboss/bgp/cpp/policy/PolicyUtils.h"
#include "neteng/fboss/bgp/cpp/policy/base/PolicyActionBase.h"
#include "neteng/fboss/bgp/cpp/rib/RibEntry.h"
#include "neteng/fboss/bgp/cpp/rib/RouteFilterConfig.h"
#include "neteng/fboss/bgp/cpp/rib/RouteInfoSelector.h"
#include "neteng/fboss/bgp/if/gen-cpp2/BgpStructs_types.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

using facebook::neteng::fboss::bgp_attr::AdvertiseLinkBandwidth;
using facebook::neteng::fboss::bgp_attr::ReceiveLinkBandwidth;

namespace facebook::bgp {

#define REPEAT_N_TIMED(maxTries, sleepTime, ...) \
  {                                              \
    int tries = 0;                               \
    while (tries++ < maxTries) {                 \
      /* do not sleep on first try */            \
      if (tries != 1) {                          \
        std::this_thread::sleep_for(sleepTime);  \
      }                                          \
      __VA_ARGS__;                               \
    }                                            \
  }

#define REPEAT_N(maxRetries, ...) \
  REPEAT_N_TIMED(maxRetries, std::chrono::milliseconds(1000), __VA_ARGS__);

#define REPEAT(...) REPEAT_N(30, __VA_ARGS__);

// ----------------- Default values -----------------
inline const auto kDefaultV4 = folly::IPAddress::createNetwork("0.0.0.0/0");
inline const auto kDefaultV6 = folly::IPAddress::createNetwork("::/0");
inline const auto kPrefix1 = folly::IPAddress::createNetwork("101.0.0.0/24");
inline const auto kEmptyV4PeerAddr = folly::IPAddress("0.0.0.0");
inline const auto kEmptyV4Nexthop = folly::IPAddress("0.0.0.0");
inline const auto kEmptyV6PeerAddr = folly::IPAddress("::");
inline const auto kEmptyV6Nexthop = folly::IPAddress("::");

inline const auto kDefaultAsn = 100;
inline const auto kMed = 32;
inline const auto kMed2 = 50;
inline const auto kWeight = 100;
inline const auto kWeight2 = 200;
inline const auto kLocalPref = 100;
inline const auto kLocalPref2 = 200;
inline const auto kOriginatorId = 0x86070000; // ip: "134.7.0.0""
inline const auto kAsSeqAsNum = 32934;
inline const auto kCommAsNum = 65530;
inline const auto kCommAsVal = 15800;
inline const auto kExtCommASTypeFirstWord = 0x00011234;
inline const auto kExtCommASTypeSecondWord = 0x600DCAFE;
inline const auto kExtCommLbwTypeFirstWord = 0x40041111;
inline const auto kExtCommLbwTypeSecondWord10G = 0x501502F9; // 10G
inline const auto kExtCommLbwTypeSecondWord20G = 0x509502f9; // 20G
inline const auto kExtCommRegularTypeFirstWord = 0x43011234;
inline const auto kExtCommRegularTypeSecondWord = 0x56789ABC;
inline const auto kClusterIp = 0x0a0a0a0a; // ip: "10.10.10.10"
inline const auto kV4NexthopPrefixLen = 32;
inline const auto kV6NexthopPrefixLen = 128;
inline const auto kAggregatorAsNum = 4660;
inline const auto kAggregatorAddr = folly::IPAddress("3.4.5.6");
inline const auto kIsRrClientFalse = false;
inline const auto kIsRrClientTrue = true;
inline const auto kIsConfedPeerFalse = false;
inline const auto kIsConfedPeerTrue = true;
inline const auto kRemovePrivateAsFalse = false;
inline const auto kRemovePrivateAsTrue = true;

inline const auto kLocalAs1 = facebook::bgp::AsNum(1);
inline const auto kLocalAs2 = facebook::bgp::AsNum(11);
inline const auto kLocalAs3 = facebook::bgp::AsNum(12);
inline const auto kRemoteAs1 = facebook::bgp::AsNum(1);
inline const auto kRemoteAs2 = facebook::bgp::AsNum(2);
inline const auto kLocalRouteAs = facebook::bgp::AsNum(0);
inline const auto kLocalPrivateAs1 = facebook::bgp::AsNum(65001);
inline const auto kRemotePrivateAs2 = facebook::bgp::AsNum(65002);
inline const auto kIPAddr1 = folly::IPAddress("1.1.1.1");

inline const std::chrono::milliseconds kLoopDetectionInterval{5};
inline const int64_t kLoopDetectionRetryLimit{1000};

// ------------------- Peers ------------------------
inline const auto kPeerAddr1 = folly::IPAddress("1.1.1.1");
inline const auto kPeerAsn1 = facebook::bgp::AsNum(2);
inline const auto kPeerRouterId1 = kPeerAddr1.asV4().toLongHBO();
inline const auto kPeerId1 =
    facebook::nettools::bgplib::BgpPeerId(kPeerAddr1, kPeerRouterId1);

inline const auto kPeerAddr2 = folly::IPAddress("2.2.2.2");
inline const auto kPeerAsn2 = facebook::bgp::AsNum(3);
inline const auto kPeerRouterId2 = kPeerAddr2.asV4().toLongHBO();
inline const auto kPeerId2 =
    facebook::nettools::bgplib::BgpPeerId(kPeerAddr2, kPeerRouterId2);

inline const auto kLocalV4RoutePeerAddr = folly::IPAddress("0.0.0.0");
inline const auto kLocalV6RoutePeerAddr = folly::IPAddress("::");

// SSW policy testing peers
inline const auto kSswGlobalAs = facebook::bgp::AsNum(64903);
inline const auto kSswLocalAs = facebook::bgp::AsNum(64903);
inline const auto kFaDuLocalAs = facebook::bgp::AsNum(65325);
inline const auto kFaDuLocalAsForFauu = facebook::bgp::AsNum(7001);
inline const auto kFaDuNextHopV4 = folly::IPAddress("10.216.130.65");
inline const auto kFaDuNextHopV6 = folly::IPAddress("::");
inline const auto kSswNextHopV4 = folly::IPAddress("10.246.0.4");
inline const auto kSswNextHopV6 = folly::IPAddress("::");
inline const auto kSswAddr = folly::IPAddress("10.246.0.5");
inline const auto kFaDuAddr = folly::IPAddress("10.216.130.64");
inline const auto kSswLocalAsForFsw = facebook::bgp::AsNum(65301);
inline const auto kFswLocalAs = facebook::bgp::AsNum(65401);
inline const auto kSswIpForFsw = folly::IPAddress("10.120.2.0");
inline const auto kFswIp = folly::IPAddress("10.120.2.1");

// fauu as number shown up in bgpconfig on du device
inline const auto kFauuAsOnDu = facebook::bgp::AsNum(8001);
inline const auto kFauuIp = folly::IPAddress("10.246.63.0");
inline const auto kFauuNextHopV6 = folly::IPAddress("::");
inline const auto kFauuNextHopV4 = folly::IPAddress("10.246.63.1");

inline const auto kNextHopV4ForDuOnUu = folly::IPAddress("10.216.63.46");

// eb configs on FAUU
inline const auto kEbAs = facebook::bgp::AsNum(64562);
inline const auto kUuAsForEb = facebook::bgp::AsNum(65333);
inline const auto kEbIp = folly::IPAddress("10.216.63.192");
inline const auto kFauuNextHopV6ForEb = folly::IPAddress("::");
inline const auto kFauuNextHopV4ForEb = folly::IPAddress("10.216.63.193");
inline const auto kUuIpForEb = folly::IPAddress("10.216.63.193");

// dr configs on FAUU
inline const auto kDrAs = facebook::bgp::AsNum(32934);
inline const auto kUuAsForDr = facebook::bgp::AsNum(65333);
inline const auto kDrIp = folly::IPAddress("10.216.63.254");
inline const auto kFauuNextHopV6ForDr = folly::IPAddress("::");
inline const auto kFauuNextHopV4ForDr = folly::IPAddress("10.216.63.255");
inline const auto kUuIpForDr = folly::IPAddress("10.216.63.255");

// rsw configs on FSW for policy unittest
inline const auto kFswAsForRsw = facebook::bgp::AsNum(6001);
inline const auto kRswAsForFsw = facebook::bgp::AsNum(2001);
inline const auto kFswIpForRsw = folly::IPAddress("10.120.78.0");
inline const auto kRswForFswIp = folly::IPAddress("10.120.78.1");

// ------------------ v4 prefixes ---------------------
inline const auto kV4Prefix1 =
    folly::CIDRNetwork(folly::IPAddress("8.0.0.0"), 24);
inline const auto kV4Prefix1Base =
    folly::CIDRNetwork(folly::IPAddress("8.0.0.0"), 16);
inline const auto kV4Prefix1Slash23 =
    folly::CIDRNetwork(folly::IPAddress("8.0.0.0"), 23);
inline const auto kV4Prefix1Slash25 =
    folly::CIDRNetwork(folly::IPAddress("8.0.0.0"), 25);
inline const auto kV4Prefix1Slash26 =
    folly::CIDRNetwork(folly::IPAddress("8.0.0.0"), 26);
inline const auto kV4Prefix1Slash27 =
    folly::CIDRNetwork(folly::IPAddress("8.0.0.0"), 27);
inline const auto kV4Prefix1Slash28 =
    folly::CIDRNetwork(folly::IPAddress("8.0.0.0"), 28);
inline const auto kV4Prefix2 =
    folly::CIDRNetwork(folly::IPAddress("9.0.0.0"), 24);
inline const auto kV4Prefix2Slash31 =
    folly::CIDRNetwork(folly::IPAddress("9.0.0.0"), 31);
inline const auto kV4Prefix3 =
    folly::CIDRNetwork(folly::IPAddress("8.0.1.0"), 24);
inline const auto kV4Prefix4 =
    folly::CIDRNetwork(folly::IPAddress("10.0.1.0"), 24);
inline const auto kV4Prefix5 =
    folly::CIDRNetwork(folly::IPAddress("11.0.1.0"), 24);
inline const auto kV4Prefix6 =
    folly::CIDRNetwork(folly::IPAddress("11.0.2.0"), 24);
inline const auto kV4Prefix7 =
    folly::CIDRNetwork(folly::IPAddress("12.0.2.0"), 24);
inline const auto kV4PrefixZero =
    folly::CIDRNetwork(folly::IPAddress("0.0.0.0"), 0);
inline const auto kV4PrefixNoMaskStr = "8.1.1.1";
inline const auto kV4PrefixNoMask =
    folly::CIDRNetwork(folly::IPAddress("8.1.1.1"), 32);
inline const auto kV4Nexthop1 = folly::IPAddress("11.0.0.1");
inline const auto kV4Nexthop2 = folly::IPAddress("11.0.0.2");
inline const auto kV4Nexthop3 = folly::IPAddress("11.0.0.3");
inline const auto kV4Nexthop4 = folly::IPAddress("11.0.0.4");
inline const auto kV4Nexthop5 = folly::IPAddress("11.0.0.5");

inline const auto kV4Prefix8_0Slash24 =
    folly::IPAddress::createNetwork("9.0.0.0/24");
inline const auto kV4Prefix8_0Slash26 =
    folly::IPAddress::createNetwork("9.0.0.0/26");
inline const auto kV4Prefix8_128Slash26 =
    folly::IPAddress::createNetwork("9.0.0.128/26");
inline const auto kV4Prefix8_0Slash28 =
    folly::IPAddress::createNetwork("9.0.0.0/28");
inline const auto kV4Prefix8_16Slash28 =
    folly::IPAddress::createNetwork("9.0.0.16/28");
inline const auto kV4Prefix8_0Slash29 =
    folly::IPAddress::createNetwork("9.0.0.0/29");
inline const auto kV4Prefix8_8Slash29 =
    folly::IPAddress::createNetwork("9.0.0.8/29");
inline const auto kV4Prefix8_0Slash30 =
    folly::IPAddress::createNetwork("9.0.0.0/30");
inline const auto kV4Prefix8_8Slash30 =
    folly::IPAddress::createNetwork("9.0.0.8/30");
inline const auto kV4Prefix8_2Slash31 =
    folly::IPAddress::createNetwork("9.0.0.2/31");
inline const auto kV4Prefix8_4Slash31 =
    folly::IPAddress::createNetwork("9.0.0.4/31");
inline const auto kV4Prefix8_1Slash32 =
    folly::IPAddress::createNetwork("9.0.0.1/32");
inline const auto kV4Prefix8_2Slash32 =
    folly::IPAddress::createNetwork("9.0.0.2/32");
inline const auto kV4Prefix8_3Slash32 =
    folly::IPAddress::createNetwork("9.0.0.3/32");
inline const auto kV4Prefix8_4Slash32 =
    folly::IPAddress::createNetwork("9.0.0.4/32");

// ----------------- v6 prefixes ----------------------
inline const auto kV6Prefix1 =
    folly::CIDRNetwork(folly::IPAddress("2001::"), 64);
inline const auto kV6Prefix1Base =
    folly::CIDRNetwork(folly::IPAddress("2001::"), 30);
inline const auto kV6Prefix1Slash63 =
    folly::CIDRNetwork(folly::IPAddress("2001::"), 63);
inline const auto kV6Prefix1Slash65 =
    folly::CIDRNetwork(folly::IPAddress("2001::"), 65);
inline const auto kV6Prefix1Slash66 =
    folly::CIDRNetwork(folly::IPAddress("2001::"), 66);
inline const auto kV6Prefix1Slash67 =
    folly::CIDRNetwork(folly::IPAddress("2001::"), 67);
inline const auto kV6PrefixZero =
    folly::CIDRNetwork(folly::IPAddress("0::"), 0);
inline const auto kV6Prefix2 =
    folly::CIDRNetwork(folly::IPAddress("2002::"), 64);
inline const auto kV6Prefix2Slash127 =
    folly::CIDRNetwork(folly::IPAddress("2002::"), 127);
inline const auto kV6Prefix3 =
    folly::CIDRNetwork(folly::IPAddress("2003::"), 64);
inline const auto kV6Nexthop1 = folly::IPAddress("3001::1");
inline const auto kV6Nexthop2 = folly::IPAddress("3001::2");
inline const auto kV6Nexthop3 = folly::IPAddress("3001::3");
inline const auto kV6Prefix4_0Slash59 =
    folly::IPAddress::createNetwork("2401:db00:21:7000::/59");
inline const auto kV6Prefix4_3Slash64 =
    folly::IPAddress::createNetwork("2401:db00:21:7003::/64");
inline const auto kV6Prefix4_4Slash64 =
    folly::IPAddress::createNetwork("2401:db00:21:7004::/64");
inline const auto kV6Prefix4_4Slash62 =
    folly::IPAddress::createNetwork("2401:db00:21:7004::/62");
inline const auto kV6Prefix4_7Slash64 =
    folly::IPAddress::createNetwork("2401:db00:21:7007::/64");

inline const auto kV6Prefix2Base = folly::CIDRNetwork("2401:db00::", 32);
inline const auto kV6Prefix2Slash64 =
    folly::CIDRNetwork("2401:db00:1ff:7000::", 64);
inline const auto kV6Prefix2Slash128 =
    folly::CIDRNetwork("2401:db00:1ff:7000::", 128);

inline const uint32_t kAsn1 = 4200000001;
inline const uint32_t kAsn2 = 64550;
inline const uint32_t kAsn3 = 64552;
inline const uint32_t kAsn4 = 65000;
inline const uint32_t kAsn5 = 65001;
inline const uint32_t kAsn6 = 65002;
inline const uint32_t kRepeatedTimes1 = 5;
inline const uint32_t kRepeatedTimes2 = 253;
inline constexpr auto kHoldTime = std::chrono::seconds(30);
inline constexpr auto kKeepAliveTime1 = std::chrono::seconds(10);
inline constexpr auto kKeepAliveTime2 = std::chrono::seconds(15);
inline constexpr auto kGrRestartTime = std::chrono::seconds(60); // 60 sec
inline const std::chrono::seconds kLongGrRestartTime =
    std::chrono::seconds(60); // 60 sec
inline const std::chrono::seconds k1SecGrRestartTime =
    std::chrono::seconds(1); // 1 sec
inline const std::chrono::seconds k10SecGrRestartTime =
    std::chrono::seconds(10); // 10 sec
inline const std::chrono::seconds kShortGrRestartTime =
    std::chrono::seconds(0); // 0 sec
inline const auto kLocalAddr1 = folly::IPAddress("127.1.0.1");
inline const auto kLocalRouterId1 = kLocalAddr1.asV4().toLongHBO();
/*
 * Route-reflector cluster ID, deliberately unequal to kLocalAddr1 so that
 * reflection tests cannot pass while the cluster ID and the BGP identifier
 * are conflated. RFC 4456 requires the two to be settable independently, and
 * a cluster ID need not be a routable address. Matches the constant the
 * route-reflection tests use as their configured cluster ID.
 */
inline const auto kLocalClusterAddr1 = folly::IPAddress("1.0.0.20");
inline const auto kLocalClusterId1 = kLocalClusterAddr1.asV4().toLongHBO();
inline const auto kLocalPeerId1 =
    facebook::nettools::bgplib::BgpPeerId(kLocalAddr1, kLocalRouterId1);
inline const auto kPeerPrefix1 =
    folly::IPAddress::createNetwork("127.1.0.0/30");
inline const auto kDynamicPeerAddr1 = folly::IPAddress("127.1.0.1");
inline const auto kDynamicRouterId1 = kDynamicPeerAddr1.asV4().toLongHBO();
inline const auto kDynamicPeerId1 =
    facebook::nettools::bgplib::BgpPeerId(kDynamicPeerAddr1, kDynamicRouterId1);
inline const auto kNextHopV4_1 = folly::IPAddress("0.0.0.0");
inline const auto kNextHopV6_1 = folly::IPAddress("::");
inline const auto kDescription1 = "rsw001.p001.f01.bgp1";
inline const auto kPeerGroupName1 = "PEERGROUP_RSW_CSW";
inline const auto kPeerGroupName2 = "PEERGROUP_CSW_RSW";
inline const auto kPeerGroupNameDsf = "PEERGROUP_RTSW_EDSW";
const uint32_t kPreMaxRoutes = 12345;
inline const uint32_t kPreWarningThreshold = 50;
inline const uint32_t kPostMaxRoutes = 54321;
inline const uint32_t kPostWarningThreshold = 80;

inline const auto kLocalAddr2 = kLocalAddr1;
inline const auto kPeerPrefix2 =
    folly::IPAddress::createNetwork("127.2.0.0/30");
inline const auto kDynamicPeerAddr2 = folly::IPAddress("127.2.0.1");
inline const auto kDynamicRouterId2 = kDynamicPeerAddr2.asV4().toLongHBO();
inline const auto kDynamicPeerId2 =
    facebook::nettools::bgplib::BgpPeerId(kDynamicPeerAddr2, kDynamicRouterId2);
inline const auto kNextHopV4_2 = kLocalAddr1;
inline const auto kNextHopV6_2 = folly::IPAddress("2401:db00:111:400b::a");
inline const auto kDescription2 = "description2";

inline const auto kPeerPrefix3 =
    folly::IPAddress::createNetwork("10.127.240.0/23");
inline const auto kDynamicPeerAddr3 = folly::IPAddress("10.127.240.0");
// Stream subscriber peer address (localhost, used by getStreamPeeringParams())
inline const auto kStreamPeerAddr = folly::IPAddress("::1");
inline const auto kDynamicRouterId3 = kDynamicPeerAddr3.asV4().toLongHBO();
inline const auto kDynamicPeerId3 =
    facebook::nettools::bgplib::BgpPeerId(kDynamicPeerAddr3, kDynamicRouterId3);

inline const auto kPeerPrefix21 =
    folly::IPAddress::createNetwork("127.21.0.0/30");
inline const auto kPeerPrefix22 =
    folly::IPAddress::createNetwork("127.22.0.0/30");

// VIP injector based dynamic peer (router id falls in 255.x.x.x)
inline const auto kPeerPrefix4 =
    folly::IPAddress::createNetwork("169.254.0.0/16");
inline const auto kDynamicPeerAddr4 = folly::IPAddress("169.254.0.81");
inline const auto kDynamicRouterId4 =
    folly::IPAddressV4("255.0.0.1").toLongHBO();
inline const auto kDynamicPeerId4 =
    facebook::nettools::bgplib::BgpPeerId(kDynamicPeerAddr4, kDynamicRouterId4);

// Exabgp based dynamic peer (router id doesn't fall in 255.x.x.x)
inline const auto kPeerPrefix5 =
    folly::IPAddress::createNetwork("169.254.0.0/16");
inline const auto kDynamicPeerAddr5 = folly::IPAddress("169.254.0.81");
inline const auto kDynamicRouterId5 =
    folly::IPAddressV4("169.0.0.1").toLongHBO();
inline const auto kDynamicPeerId5 =
    facebook::nettools::bgplib::BgpPeerId(kDynamicPeerAddr5, kDynamicRouterId5);

inline const auto kLocalAddr3 = folly::IPAddress("127.1.0.2");
inline const auto kPeerAddr3 = folly::IPAddress("127.3.0.1");
inline const auto kPeerRouterId3 = kPeerAddr3.asV4().toLongHBO();
inline const auto kPeerId3 =
    facebook::nettools::bgplib::BgpPeerId(kPeerAddr3, kPeerRouterId3);
inline const uint32_t kPeerAsn3 = 4200000010;
inline const auto kNextHopV4_3 = folly::IPAddress("127.5.0.1");
inline const auto kNextHopV6_3 =
    folly::IPAddress("2401:db00:e011:411:1000::29");

inline const auto kLocalAddr4 = folly::IPAddress("127.1.0.3");
inline const auto kPeerAddr4 = folly::IPAddress("127.4.0.1");
inline const auto kPeerRouterId4 = kPeerAddr4.asV4().toLongHBO();
inline const auto kPeerId4 =
    facebook::nettools::bgplib::BgpPeerId(kPeerAddr4, kPeerRouterId4);
inline const uint32_t kPeerAsn4 = 64541;
inline const auto kNextHopV4_4 = folly::IPAddress("127.5.0.3");
inline const auto kNextHopV6_4 =
    folly::IPAddress("2401:db00:e011:411:1000::2b");

inline const auto kLocalAddr5 = folly::IPAddress("127.1.0.4");
inline const auto kPeerAddr5 = folly::IPAddress("127.5.0.1");
inline const auto kPeerRouterId5 = 6;
inline const uint32_t kPeerAsn5 = 64542;
inline const auto kNextHopV4_5 = folly::IPAddress("127.5.0.4");
inline const auto kNextHopV6_5 =
    folly::IPAddress("2401:db00:e011:411:1000::2d");

inline const auto kLocalAddr6 = folly::IPAddress("127.1.0.5");
inline const auto kPeerAddr6 = folly::IPAddress("127.6.0.1");
inline const auto kPeerRouterId6 = 7;
inline const uint32_t kPeerAsn6 = 64543;
inline const auto kNextHopV4_6 = folly::IPAddress("127.5.0.5");
inline const auto kNextHopV6_6 =
    folly::IPAddress("2401:db00:e011:411:1000::30");

inline const auto kLocalAddr7 = folly::IPAddress("127.1.0.6");
inline const auto kPeerAddr7 = folly::IPAddress("127.7.0.1");
inline const auto kPeerRouterId7 = 8;
inline const uint32_t kPeerAsn7 = 64544;
inline const auto kNextHopV4_7 = folly::IPAddress("127.5.0.6");
inline const auto kNextHopV6_7 =
    folly::IPAddress("2401:db00:e011:411:1000::30");

inline const auto kLocalAddr8 = folly::IPAddress("127.1.0.7");
inline const auto kPeerAddr8 = folly::IPAddress("127.8.0.1");
inline const auto kPeerRouterId8 = 9;
inline const uint32_t kPeerAsn8 = 64545;
inline const auto kNextHopV4_8 = folly::IPAddress("127.5.0.7");
inline const auto kNextHopV6_8 =
    folly::IPAddress("2401:db00:e011:411:1000::30");

inline const auto kLocalAddr9 = folly::IPAddress("127.1.0.8");
inline const auto kPeerAddr9 = folly::IPAddress("127.9.0.1");
inline const auto kPeerRouterId9 = 10;
inline const uint32_t kPeerAsn9 = 64546;
inline const auto kNextHopV4_9 = folly::IPAddress("127.5.0.8");
inline const auto kNextHopV6_9 =
    folly::IPAddress("2401:db00:e011:411:1000::30");

inline const auto kCommunity1 = "65500:1";
inline const auto kCommunity2 = "65500:2";
inline const auto kCommunity3 = "65500:3";
inline const auto kCommunity4 = "65500:4";
inline const auto kCommunityRegex1 = "65500:1.*";
inline const auto kCommunityRegex2 = "65500:2.*";
inline const auto kCommunityMatchingRegex1 = "65500:11";
inline const auto kCommunityMatchingRegex2 = "65500:21";
inline const auto kCommunityNotMatchingRegex1 = "65500:31";
inline const auto kASPath1 = "65500";
inline const auto kASPath2 = "65501";
inline const auto kASPathRegex1 = "^65000.*";
inline const auto kASPathSetRegex1 = "^{65000.*";
inline const auto kASPathRegex2 = ".*65001$";
inline const auto kASPathSetRegex2 = ".*65001}";
inline const auto kASPathRegex3 = ".*";
inline const auto kASPathRegex4 = "^65002.*";
inline const auto kASPathRegex5 = "65000";
inline const auto kASPathRegex6 = "65000_65000";
inline const auto kASPathRegexDot = "6.000";
inline const auto kASPathRegexNum = "\\d{5}";
inline const auto kASPathRegexMultiSeq =
    "^\\(2[0-9][0-9][0-9]\\)_65000_65000_65000$";

inline const auto kPeerTypeCsw = "CSW";
inline const auto kPeerTypeFa = "FA_UU";
inline const auto kPeerTypeShiv = "SHIV";
inline const auto kPeerTypeBgpMonitor = "BGP_MONITOR";
inline const auto kPeerTypeEdsw = "EDSW";
inline const auto kPeerTypeRdsw = "RDSW";

inline const auto kIngressPolicyName = "Ingress";
inline const auto kEgressPolicyName = "Egress";

// Link Bandwidth in Bytes (not bits) per second;
inline const auto kLbw150G = float(150) * facebook::bgp::BpsPerGBps / 8;
inline const auto kLbw100G = float(100) * facebook::bgp::BpsPerGBps / 8;
inline const auto kLbw20G = float(20) * facebook::bgp::BpsPerGBps / 8;
inline const auto kLbw10G = float(10) * facebook::bgp::BpsPerGBps / 8;
inline const auto kLbw5G = float(5) * facebook::bgp::BpsPerGBps / 8;
inline const auto kLbw2G = float(2) * facebook::bgp::BpsPerGBps / 8;
inline const auto kLbw1G = float(1) * facebook::bgp::BpsPerGBps / 8;

// Encoded LBW
inline const auto kEncodedLbw =
    103031942; // 0xb(110)(00100100)(00100100)(1000)(0110): 6,36,36,8,6

// multipath selectors
inline const auto multipathSelector =
    std::make_unique<facebook::bgp::RouteInfoSelector>(
        facebook::bgp::getBaseRouteFilterConfigsMultiPath(
            facebook::bgp::CountConfedsInAsPathLen{false}));
inline const auto multipathSelectorCountConfeds =
    std::make_unique<facebook::bgp::RouteInfoSelector>(
        facebook::bgp::getBaseRouteFilterConfigsMultiPath(
            facebook::bgp::CountConfedsInAsPathLen{true}));

// bestpath selectors
inline const auto bestpathSelector =
    std::make_unique<facebook::bgp::RouteInfoSelector>(
        facebook::bgp::getRouteFilterConfigsBestPath());

inline bool checkTRibEntryIsForPrefix(
    const facebook::neteng::fboss::bgp::thrift::TRibEntry& tRibEntry,
    const folly::CIDRNetwork& prefix) {
  return (
      (tRibEntry.prefix().value().prefix_bin().value() ==
       facebook::network::toBinaryAddress(prefix.first)
           .addr()
           ->toStdString()) &&
      (folly::copy(tRibEntry.prefix().value().num_bits().value()) ==
       prefix.second));
}

inline std::shared_ptr<facebook::bgp::RouteInfo> createRouteInfo(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& peerAddr,
    const folly::IPAddress& nexthop,
    uint32_t localPref = facebook::bgp::kDefaultLocalPref,
    const std::vector<std::string>& communities = {},
    uint32_t peerAsn = 0,
    uint32_t peerRouterId = 0,
    std::shared_ptr<facebook::bgp::BgpPath> attrs = nullptr,
    std::optional<nettools::bgplib::BgpAttrAsPathC> asPath = std::nullopt,
    uint32_t receivedPathId = kDefaultPathID,
    std::optional<uint32_t> pathIdToSend = std::nullopt) {
  if (!attrs) {
    attrs = std::make_shared<facebook::bgp::BgpPath>();
    attrs->setLocalPref(localPref);
    attrs->setCommunities(
        facebook::bgp::createBgpAttrCommunitiesC(communities));
    attrs->setOrigin(nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP);
    if (asPath.has_value()) {
      attrs->setAsPath(asPath.value());
    }
    attrs->setNexthop(nexthop);
  }
  attrs->publish();

  // Create RibEntry using the input prefix for testing
  facebook::bgp::RibEntry testRibEntry(prefix);

  auto routeInfo = std::make_shared<facebook::bgp::RouteInfo>(
      prefix,
      facebook::bgp::TinyPeerInfo(
          peerAddr,
          peerAsn,
          peerRouterId,
          facebook::bgp::BgpSessionType::EBGP,
          false // isRrClient
          ),
      attrs,
      receivedPathId,
      testRibEntry,
      pathIdToSend);

  return routeInfo;
}

/*
 * Builds dynamic attribute with configurable elements
 * All C++ structs instead of thrift struct
 */
inline std::shared_ptr<facebook::bgp::BgpPathFields> buildBgpPathFields(
    uint32_t as_count,
    uint32_t community_count,
    uint32_t ext_community_count,
    uint32_t cluster_list_count,
    uint32_t confed_as_count = 0,
    folly::IPAddress nh = kV4Nexthop1) {
  auto attrs = std::make_shared<facebook::bgp::BgpPathFields>();

  facebook::nettools::bgplib::BgpAttributesC mutableAttrs;

  mutableAttrs.origin =
      facebook::nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP;

  facebook::nettools::bgplib::BgpAttrAsPathC asPath;

  if (as_count > 0) {
    facebook::nettools::bgplib::BgpAttrAsPathSegmentC segment;
    for (int i = 0; i < as_count; i++) {
      segment.asSequence.push_back(i);
    }
    asPath.push_back(segment);
  }

  if (confed_as_count > 0) {
    facebook::nettools::bgplib::BgpAttrAsPathSegmentC segment;
    for (int i = 0; i < confed_as_count; i++) {
      segment.asConfedSequence.push_back(i);
    }
    asPath.push_back(segment);
  }
  mutableAttrs.asPath = std::move(asPath);

  mutableAttrs.med = kMed;
  mutableAttrs.isMedSet = true;

  mutableAttrs.localPref = kLocalPref;
  attrs->nexthop = nh;

  if (community_count) {
    facebook::nettools::bgplib::BgpAttrCommunityC community;
    facebook::nettools::bgplib::BgpAttrCommunitiesC communities;
    communities.reserve(community_count);

    for (int i = 0; i < community_count; i++) {
      community.asn = kCommAsNum;
      community.value = kCommAsVal + i;
      communities.push_back(community);
    }
    mutableAttrs.communities = std::move(communities);
  }

  if (ext_community_count) {
    facebook::nettools::bgplib::BgpAttrExtCommunityC extCommunity(
        kExtCommASTypeFirstWord, kExtCommASTypeSecondWord);
    facebook::nettools::bgplib::BgpAttrExtCommunitiesC extCommunities;
    extCommunities.reserve(ext_community_count);

    for (int i = 0; i < ext_community_count; i++) {
      extCommunities.push_back(extCommunity);
    }
    mutableAttrs.extCommunities = std::move(extCommunities);
  }

  mutableAttrs.originatorId = kOriginatorId;

  if (cluster_list_count) {
    facebook::nettools::bgplib::BgpAttrClusterListC clusterList;
    clusterList.reserve(cluster_list_count);
    for (int i = 0; i < cluster_list_count; i++) {
      clusterList.emplace_back(kClusterIp);
    }
    mutableAttrs.clusterList = std::move(clusterList);
  }
  mutableAttrs.weight = kWeight;
  attrs->attrs = std::move(mutableAttrs);

  return attrs;
}

inline facebook::bgp::BgpPolicyActionData createLbwActionData(
    const std::optional<std::pair<uint16_t, float>>& originalAsnLbw,
    const uint32_t asn,
    const std::optional<size_t> switchId = std::nullopt,
    const std::optional<size_t> multiPathSize = std::nullopt,
    const std::optional<float> linkBandwidthBps = std::nullopt,
    const std::optional<float> aggregateReceivedUcmpWeight = std::nullopt,
    const std::optional<float> aggregateLocalUcmpWeight = std::nullopt) {
  facebook::bgp::LbwActionData lbwActionData{
      originalAsnLbw,
      asn,
      linkBandwidthBps,
      aggregateReceivedUcmpWeight,
      aggregateLocalUcmpWeight};

  facebook::bgp::BgpPolicyActionData policyActionData{
      switchId, multiPathSize, std::move(lbwActionData)};
  return policyActionData;
}

inline facebook::bgp::thrift::BgpPeer createBgpPeerHelper(
    const uint32_t remoteAsn,
    const folly::IPAddress& localAddr,
    const folly::IPAddress& nextHopV4,
    const folly::IPAddress& nextHopV6,
    const std::optional<bool>& isPassive,
    const std::optional<std::string>& type,
    const std::optional<bool>& enableStatefulHa = std::nullopt) {
  facebook::bgp::thrift::BgpPeer bgpPeer;
  bgpPeer.remote_as_4_byte() = remoteAsn;
  bgpPeer.local_addr() = localAddr.str();
  bgpPeer.next_hop4() = nextHopV4.str();
  bgpPeer.next_hop6() = nextHopV6.str();
  bgpPeer.is_passive().from_optional(isPassive);
  bgpPeer.type().from_optional(type);
  bgpPeer.enable_stateful_ha().from_optional(enableStatefulHa);
  return bgpPeer;
}

inline facebook::bgp::thrift::BgpPeer createDefaultBgpPeer() {
  auto bgpPeer = createBgpPeerHelper(
      kAsn1, // remoteAsn
      kLocalAddr1,
      kNextHopV4_1,
      kNextHopV6_1,
      false, // isPassive
      std::nullopt // type
  );
  bgpPeer.peer_addr() = kPeerAddr1.str();
  return bgpPeer;
}

inline facebook::bgp::thrift::BgpPeer createBgpPeer(
    const uint32_t remoteAsn,
    const folly::IPAddress& localAddr,
    const folly::IPAddress& peerAddr,
    const folly::IPAddress& nextHopV4,
    const folly::IPAddress& nextHopV6,
    bool isPassive,
    const std::string& type) {
  auto bgpPeer = createBgpPeerHelper(
      remoteAsn, localAddr, nextHopV4, nextHopV6, isPassive, type);
  bgpPeer.peer_addr() = peerAddr.str();
  return bgpPeer;
}

inline facebook::bgp::thrift::BgpPeer createBgpPeer(
    const uint32_t remoteAsn,
    const folly::IPAddress& localAddr,
    const folly::CIDRNetwork& peerPrefix,
    const folly::IPAddress& nextHopV4,
    const folly::IPAddress& nextHopV6,
    bool isPassive,
    const std::string& type) {
  auto bgpPeer = createBgpPeerHelper(
      remoteAsn, localAddr, nextHopV4, nextHopV6, isPassive, type);
  bgpPeer.peer_addr() = folly::IPAddress::networkToString(peerPrefix);
  return bgpPeer;
}

inline facebook::bgp::thrift::BgpPeer createBgpPeer(
    const uint32_t remoteAsn,
    const folly::IPAddress& localAddr,
    const folly::IPAddress& peerAddr,
    const folly::IPAddress& nextHopV4,
    const folly::IPAddress& nextHopV6,
    const bool isPassive,
    const std::string& type,
    const std::optional<std::string>& ingressPolicyName,
    const std::optional<std::string>& egressPolicyName,
    const std::optional<AdvertiseLinkBandwidth>& advertiseLinkBandwidth =
        std::nullopt,
    const std::optional<bool>& removePrivateAs = std::nullopt,
    const std::optional<bool>& enableStatefulHa = std::nullopt) {
  auto bgpPeer = createBgpPeerHelper(
      remoteAsn, localAddr, nextHopV4, nextHopV6, isPassive, type);
  bgpPeer.peer_addr() = peerAddr.str();
  bgpPeer.ingress_policy_name().from_optional(ingressPolicyName);
  bgpPeer.egress_policy_name().from_optional(egressPolicyName);
  bgpPeer.advertise_link_bandwidth().from_optional(advertiseLinkBandwidth);
  bgpPeer.remove_private_as().from_optional(removePrivateAs);
  bgpPeer.enable_stateful_ha().from_optional(enableStatefulHa);
  return bgpPeer;
}

// Get absolute fbcode path from the relative file path.
inline std::string getAbsoluteFilePath(const std::string& relativePath) {
  std::string dir = boost::filesystem::current_path().c_str();
  return dir.substr(0, dir.find("buck-out")) + "/" + relativePath;
}

// Get running sessions counter
inline std::optional<int64_t> getRunningSessions() {
  std::map<std::string, int64_t> counters;
  facebook::fb303::ThreadCachedServiceData::get()->getCounters(counters);
  auto it = counters.find("bgpd.runningSessions");
  if (it == counters.end()) {
    return std::nullopt;
  }
  return it->second;
}

// Get convergence time ms
inline std::optional<int64_t> getConvergenceTimeMs() {
  std::map<std::string, int64_t> counters;
  facebook::fb303::ThreadCachedServiceData::get()->getCounters(counters);
  auto it = counters.find("bgpd.convergenceTimeMs");
  if (it == counters.end()) {
    return std::nullopt;
  }
  return it->second;
}

inline facebook::nettools::bgplib::BgpUpdate2 buildBgpUpdateAttributesAsPath(
    const folly::IPAddress& nexthop,
    const std::vector<facebook::nettools::bgplib::BgpAttrAsPathSegment>&
        asPath) {
  facebook::nettools::bgplib::BgpUpdate2 update;

  update.attrs()->origin() =
      facebook::nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_IGP;
  update.attrs()->asPath() = asPath;
  update.attrs()->nexthop() = nexthop.str();
  update.attrs()->med() = kMed;
  update.attrs()->isMedSet() = true;
  update.attrs()->localPref() = kLocalPref;
  update.attrs()->atomicAggregate() = true;
  update.attrs()->aggregator()->asn() = kAggregatorAsNum;
  update.attrs()->aggregator()->ip() = kAggregatorAddr.str();
  // BgpUpdate2 stores originatorId and clusterList in network byte order
  update.attrs()->originatorId() = htonl(kOriginatorId);
  update.attrs()->clusterList()->push_back(htonl(kOriginatorId));

  facebook::nettools::bgplib::BgpAttrCommunity community;
  community.asn() = kCommAsNum;
  community.value() = kCommAsVal;
  update.attrs()->communities()->push_back(community);

  return update;
}

inline facebook::nettools::bgplib::BgpUpdate2 buildBgpUpdateAttributes(
    const folly::IPAddress& nexthop) {
  std::vector<facebook::nettools::bgplib::BgpAttrAsPathSegment> asPath;
  facebook::nettools::bgplib::BgpAttrAsPathSegment segment1;
  segment1.asSequence()->push_back(kAsSeqAsNum);
  asPath.push_back(segment1);
  return buildBgpUpdateAttributesAsPath(nexthop, asPath);
}

// RIB update with prefilled attributes and single v4 or v6 announced prefix
inline facebook::bgp::RibOutPayload createRibSingleAnnounce(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& nexthop,
    const facebook::bgp::TinyPeerInfo& peer,
    bool sendWithEoR,
    const facebook::nettools::bgplib::BgpAttrOrigin& origin =
        facebook::nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_IGP,
    const std::vector<std::string>& communities = {},
    const std::optional<
        std::vector<facebook::nettools::bgplib::BgpAttrAsPathSegmentC>>&
        aspaths = std::nullopt,
    const std::optional<uint32_t> locPref = std::nullopt,
    const std::shared_ptr<facebook::bgp::BgpPathFields>& /* attrFields */ =
        nullptr,
    const bool addPath = false,
    const uint32_t pathIdToSend = kPlaceholderPathID) {
  facebook::bgp::RibOutAnnouncement ribMsg;
  facebook::nettools::bgplib::BgpUpdate2 update =
      buildBgpUpdateAttributes(nexthop);
  auto attrs = std::make_shared<facebook::bgp::BgpPath>(
      facebook::bgp::BgpPathFields(*BgpUpdate2toBgpPathC(update)));
  attrs->setOrigin(origin);
  if (locPref.has_value()) {
    attrs->setLocalPref(locPref.value());
  }
  if (!communities.empty()) {
    attrs->setCommunities(
        facebook::bgp::createBgpAttrCommunitiesC(communities));
  }
  if (aspaths.has_value()) {
    attrs->setAsPath(
        static_cast<facebook::nettools::bgplib::BgpAttrAsPathC>(
            aspaths.value()));
  }
  attrs->publish();
  if (addPath) {
    ribMsg.addPathEntries.emplace_back(prefix, pathIdToSend, peer, attrs);
  } else {
    ribMsg.entries.emplace_back(prefix, kDefaultPathID, peer, attrs);
  }
  if (sendWithEoR) {
    ribMsg.initialDump = true;
  }
  ribMsg.sendWithEoR = sendWithEoR;
  return ribMsg;
}

// RIB update with prefilled attributes and single v4 or v6 announced prefix
inline facebook::bgp::RibOutPayload createRibSingleAnnounce(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& nexthop,
    const facebook::bgp::TinyPeerInfo& peer,
    bool sendWithEoR,
    const bool addPath,
    uint32_t pathIdToSend = kPlaceholderPathID) {
  return createRibSingleAnnounce(
      prefix,
      nexthop,
      peer,
      sendWithEoR,
      facebook::nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_IGP,
      {},
      std::nullopt,
      std::nullopt,
      nullptr,
      addPath,
      pathIdToSend);
}

inline facebook::bgp::RibOutAnnouncementEntry createRibOutAnnounceEntry(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& nexthop,
    const facebook::bgp::TinyPeerInfo& peer,
    const facebook::nettools::bgplib::BgpAttrOrigin& origin =
        facebook::nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_IGP,
    const std::vector<std::string>& communities = {},
    const std::optional<
        std::vector<facebook::nettools::bgplib::BgpAttrAsPathSegmentC>>&
        aspaths = std::nullopt,
    const std::optional<uint32_t> locPref = std::nullopt,
    const uint32_t pathIdToSend = kDefaultPathID) {
  facebook::nettools::bgplib::BgpUpdate2 update =
      buildBgpUpdateAttributes(nexthop);
  auto attrs = std::make_shared<facebook::bgp::BgpPath>(
      facebook::bgp::BgpPathFields(*BgpUpdate2toBgpPathC(update)));
  attrs->setOrigin(origin);
  if (locPref.has_value()) {
    attrs->setLocalPref(locPref.value());
  }
  attrs->setCommunities(facebook::bgp::createBgpAttrCommunitiesC(communities));
  if (aspaths.has_value()) {
    attrs->setAsPath(
        static_cast<facebook::nettools::bgplib::BgpAttrAsPathC>(
            aspaths.value()));
  }
  attrs->publish();
  return facebook::bgp::RibOutAnnouncementEntry(
      prefix, pathIdToSend, peer, attrs);
}

// Creates desired number of networks
inline std::vector<folly::CIDRNetwork> getv4Prefixes(u_int16_t count) {
  std::vector<folly::CIDRNetwork> networks;
  for (auto i = 0; i < count; i++) {
    auto prefixStr = fmt::format("8.{}.{}.0", int(i / 255), int(i % 255));
    networks.emplace_back(folly::IPAddress(prefixStr), 24);
  }
  return networks;
}

inline const re2::RE2 communityParseRegex("(\\d+):(\\d+)");

inline facebook::neteng::fboss::bgp_attr::TBgpCommunity getTBgpCommunity(
    const char* community) {
  facebook::neteng::fboss::bgp_attr::TBgpCommunity tCommunity;
  int32_t asn, value;
  if (re2::RE2::FullMatch(community, communityParseRegex, &asn, &value)) {
    tCommunity.asn() = asn;
    tCommunity.value() = value;
  }
  return tCommunity;
}

inline facebook::bgp::rib_policy::TBgpCommunityMatch getTBgpCommunityMatch(
    const char* community,
    const facebook::bgp::routing_policy::MatchValueLogicOperator match_type =
        facebook::bgp::routing_policy::MatchValueLogicOperator::EQUAL) {
  facebook::bgp::rib_policy::TBgpCommunityMatch tCommunityMatch;
  tCommunityMatch.match_type() = match_type;
  int32_t asn, value;
  if (re2::RE2::FullMatch(community, communityParseRegex, &asn, &value)) {
    tCommunityMatch.community()->asn() = asn;
    tCommunityMatch.community()->value() = value;
  }
  return tCommunityMatch;
}

inline std::vector<std::pair<folly::LogMessage, const folly::LogCategory*>>&
subscribeToLogMessages(
    const std::string& category,
    folly::LogLevel logLevel = folly::LogLevel::DBG1) {
  auto handler = std::make_shared<folly::TestLogHandler>();
  folly::LoggerDB::get().getCategory(category)->addHandler(handler);
  folly::LoggerDB::get().setLevel(category, logLevel);
  return handler->getMessages();
}

inline folly::F14FastMap<std::shared_ptr<RouteInfo>, uint32_t>
getAndCheckAllocatedPathIds(
    const folly::F14FastMap<std::shared_ptr<RouteInfo>, uint32_t>&
        previousAllocatedPathIds,
    RibEntry& ribEntry) {
  auto multipaths = ribEntry.getMultipaths();
  folly::F14FastMap<std::shared_ptr<RouteInfo>, uint32_t> allocatedPathIds;
  folly::F14FastSet<uint32_t> encounteredPathIds;
  for (auto& [id, path] : multipaths) {
    EXPECT_TRUE(path->pathIdToSend.has_value());
    uint32_t pathId = path->pathIdToSend.value();
    EXPECT_EQ(pathId, id); // key should match actual path's ID
    // honor previous ID if there is one
    EXPECT_TRUE(
        !previousAllocatedPathIds.contains(path) ||
        (previousAllocatedPathIds.at(path) == pathId));
    // expect uniqueness
    EXPECT_TRUE(!encounteredPathIds.contains(pathId));
    allocatedPathIds.insert_or_assign(path, pathId);
    encounteredPathIds.insert(pathId);
  }
  EXPECT_EQ(allocatedPathIds.size(), multipaths.size());
  return allocatedPathIds;
}

using PrefixToPathIdsMap =
    folly::F14FastMap<folly::CIDRNetwork, folly::F14FastSet<uint32_t>>;
inline void checkRibOutEntriesAddPathIds(
    std::variant<RibOutAnnouncement, RibOutWithdrawal> ribOutMessage,
    std::optional<PrefixToPathIdsMap> expectedPathIds = std::nullopt) {
  /*
   * determine the set of sent path IDs, whether the message is announcement or
   * withdrawal. Organize them by prefix
   */
  PrefixToPathIdsMap msgPathIds;
  folly::variant_match(
      ribOutMessage,
      [&](const RibOutAnnouncement& announcement) {
        for (auto& entry : announcement.addPathEntries) {
          if (!msgPathIds.contains(entry.prefix)) {
            msgPathIds.insert({entry.prefix, folly::F14FastSet<uint32_t>{}});
          }
          auto& pfxPathIds = msgPathIds.at(entry.prefix);
          EXPECT_FALSE(pfxPathIds.contains(entry.pathIdToSend));
          pfxPathIds.insert(entry.pathIdToSend);
        }
      },
      [&](const RibOutWithdrawal& withdrawal) {
        for (auto& entry : withdrawal.addPathEntries) {
          if (!msgPathIds.contains(entry.prefix)) {
            msgPathIds.insert({entry.prefix, folly::F14FastSet<uint32_t>{}});
          }
          auto& pfxPathIds = msgPathIds.at(entry.prefix);
          EXPECT_FALSE(pfxPathIds.contains(entry.pathIdToSend));
          pfxPathIds.insert(entry.pathIdToSend);
        }
      });

  /*
   * by default just expect some ascending set starting at kMinPathIDToSend, for
   * each prefix
   */
  if (expectedPathIds == std::nullopt) {
    expectedPathIds = PrefixToPathIdsMap{};
    for (const auto& pfxAndPathIdSet : msgPathIds) {
      folly::F14FastSet<uint32_t> pathIds;
      auto pathId = kMinPathIDToSend;
      for (auto _ : pfxAndPathIdSet.second) {
        pathIds.insert(pathId++);
      }
      expectedPathIds->insert({pfxAndPathIdSet.first, pathIds});
    }
  }

  // sent path IDs should consist of same elements as the expected IDs
  EXPECT_EQ(msgPathIds, expectedPathIds.value());
}

} // namespace facebook::bgp
