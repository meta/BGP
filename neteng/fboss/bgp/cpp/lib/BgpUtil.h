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

#include <neteng/fboss/bgp/cpp/lib/BgpStructs.h>
#include <neteng/fboss/bgp/if/gen-cpp2/BgpStructs_types.h>

namespace std {

/**
 * Make BgpAttributes hashable
 */

template <>
struct hash<facebook::nettools::bgplib::BgpAttributes> {
  size_t operator()(facebook::nettools::bgplib::BgpAttributes const&) const;
};

} // namespace std

namespace facebook {
namespace nettools {
namespace bgplib {
extern const std::string kNullMessage;

namespace {
/*
 * This function computes a computation by recursively looking for available
 * samples ahead of an offset.
 */
template <class T>
void computeCombinations(
    int offset,
    int k,
    std::vector<T>& currentCombination,
    std::vector<std::vector<T>>& combinations,
    const std::vector<T>& container) {
  if (k == 0) {
    combinations.push_back(currentCombination);
    return;
  }

  for (int i = offset; i <= container.size() - k; ++i) {
    currentCombination.push_back(container.at(i));
    computeCombinations(
        i + 1, k - 1, currentCombination, combinations, container);
    currentCombination.pop_back();
  }
}
} // namespace

/*
 * This function gets all the combinations of a given container
 * in k number of samples. The return value is a list with all the
 * combinations.
 *
 * Example: container = [1, 2, 3], k = 2
 * The formula to compute combinations C(n, k):
 *    C(n, k) = n! / (k! * (n - k)!) = 3! / (2! * (3 - 2)!) = 3 combinations
 *
 * Result = {[1, 2], [1, 3], [2, 3]}
 */
template <class T>
std::vector<std::vector<T>> getCombinations(
    const std::vector<T>& container,
    int k) {
  const int size = container.size();
  if (k > size || k < 0) {
    throw std::out_of_range("Number of samples (k) out of combination range.");
  }
  if (k == size) {
    return {container};
  }
  if (k == 0) {
    return {};
  }

  std::vector<T> currentCombination;
  std::vector<std::vector<T>> combinations;
  computeCombinations(0, k, currentCombination, combinations, container);
  return combinations;
}

/*
 * This function computes the difference from two vectors by
 * finding the remaning values between the two of them.
 *
 * Reference: http://www.cplusplus.com/reference/algorithm/set_difference/
 */
const std::vector<std::string> getCommunityDifference(
    std::vector<std::string> list,
    std::vector<std::string> combination);

/*
 * This function performs a DFS over the communities of a neighbor route
 * and finds the name for that given community under a communitySet.
 */
const std::map<std::vector<std::string>, std::string> findCommunities(
    const std::vector<std::string>& community_list,
    std::map<
        std::set<std::string>, // Community Set
        std::string // Community Alias
        >& community_set);

/**
 * Utility method to convert BgpUpate to BgpUpdate2
 * when we serialize bgp update 2, we do not expect both
 * v4Announced/v4Announced2 to be there at the same time. Also
 * v4Withdrawn/v4Withdrawn2
 */
BgpUpdate2 toBgpUpdate2(const BgpUpdate& update, bool toSerialize = false);

/**
 * Utility method to convert BgpUpate2 to BgpUpdate
 */
std::vector<BgpUpdate> toBgpUpdate(const BgpUpdate2& update2);

/**
 * Utility method to convert BgpEndOfRib to BgpUpdate
 */
BgpUpdate toBgpUpdate(const BgpEndOfRib& eor);

/**
 * Utility method to convert variant<BgpUpdate2, BgpEndOfRib> to BgpUpdate
 */
std::vector<BgpUpdate> toBgpUpdate(
    const std::variant<BgpUpdate2, BgpEndOfRib>& update);

/**
 * Utility method to convert variant<shared_ptr<const BgpUpdate2>, BgpEndOfRib>
 * (as returned by BgpMessageParser2::parseBgpUpdateRaw) to vector<BgpUpdate>
 */
std::vector<BgpUpdate> toBgpUpdate(
    const std::variant<std::shared_ptr<const BgpUpdate2>, BgpEndOfRib>& update);

/*
 * convert thrift BgpAttributes to CPP BgpAttributes
 */
std::shared_ptr<BgpAttributesC> bgpAttributesToBgpAttributesC(
    const BgpAttributes& attr);

/*
 * convert thrift BgpAttributes to CPP BgpPath
 */
std::shared_ptr<BgpPathC> bgpAttributesToBgpPathC(const BgpAttributes& attrs);

/**
 * convert CPP BgpAttributes to thrift BgpAttributes
 */
BgpAttributes bgpAttributesCtoBgpAttributes(
    std::shared_ptr<const BgpAttributesC> attrs);

/**
 * convert CPP BgpPath to thrift BgpAttributes
 */
BgpAttributes bgpPathCtoBgpAttributes(std::shared_ptr<const BgpPathC> path);

/**
 * Utility method to convert BgpUpate2 to shared_ptr<BgpAttributesC>
 */
std::shared_ptr<BgpAttributesC> BgpUpdate2toBgpAttributesC(
    const BgpUpdate2& update);

/**
 * Utility method to convert BgpUpate2 to shared_ptr<BgpPathC>
 */
std::shared_ptr<BgpPathC> BgpUpdate2toBgpPathC(const BgpUpdate2& update);

/**
 * Utility method to convert shared_ptr<BgpAttributesC> to BgpUpdate2
 */
BgpUpdate2 BgpAttributesCtoBgpUpdate2(
    std::shared_ptr<const BgpAttributesC> attrs);

/**
 * Utility method to convert shared_ptr<BgpPathC> to BgpUpdate2
 */
BgpUpdate2 BgpPathCtoBgpUpdate2(std::shared_ptr<const BgpPathC> attrs);

/**
 * Negotiate capabilities between both sides
 */
BgpCapabilities negotiateCapabilities(
    const BgpCapabilities& myCapa,
    const BgpCapabilities& peerCapa);

/**
 * Negotiate V4 NLRI Over V6 NextHop, RFC 5549
 */
std::vector<BgpExtNHEncodingCapability> negotiateExtNHEncodingCapabilities(
    const std::vector<BgpExtNHEncodingCapability>& myCapa,
    const std::vector<BgpExtNHEncodingCapability>& peerCapa);

/*
 * Negotiate add path capabilities between both sides
 */
void negotiateBgpAddPathCapabilities(
    BgpCapabilities& ngtCapa,
    const std::vector<BgpAddPathCapability>& myCapas,
    const std::vector<BgpAddPathCapability>& peerCapas);

/*
 * helper method to convert string to prefixes/ip addresses;
 * throws InvalidAddress if there's an parsing error
 */
folly::CIDRNetwork strToNetwork(const std::string& netStr);

/*
 * Helper method to check if a device has an arista role
 * based on its hostname
 */
bool isAristaDevice();

/*
 * Utility method to build BgpNotification thrift object, since it's done in
 * several places
 */
BgpNotification buildBgpNotification(
    const ::facebook::nettools::bgplib::BgpNotifErrCode& errCode,
    const ::std::int16_t& errSubCode,
    const ::std::string& errSubCodeStr,
    const ::std::string& data);

/*
 * RFC 8538 Sec. 3: build a Cease/Hard Reset NOTIFICATION whose data carries
 * the nested NOTIFICATION error code, subcode and data
 */
BgpNotification buildBgpHardResetNotification(
    const BgpNotification& nestedNotification);

bool isBgpHardResetNotification(const BgpNotification& notif);

/*
 * Decodes the nested NOTIFICATION from the data of a Cease/Hard Reset.
 * Throws BgpException if the data is too short or carries an unknown error
 * code or subcode.
 */
BgpNotification decodeBgpHardResetNotification(const std::string& data);

/*
 * RFC 8538 Sec. 2: true if both peers advertised the Graceful Restart
 * capability with the Notification (N) bit set
 */
bool isGrNotificationNegotiated(const BgpCapabilities& negotiatedCapa);

/**
 * Construct a BgpUpdate announcement message
 */
std::unique_ptr<BgpUpdate> constructAnnounceInBgpUpdateFormat(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& nexthop,
    const std::vector<uint32_t>& asPath,
    const std::vector<BgpAttrCommunity>& communities);

/**
 * Construct a BgpUpdate withdrawal message
 */
std::unique_ptr<BgpUpdate> constructWithdrawInBgpUpdateFormat(
    const folly::CIDRNetwork& prefix);

/**
 * Construct a BgpUpdate2 announcement message
 */
std::unique_ptr<BgpUpdate2> constructAnnounceInBgpUpdate2Format(
    const folly::CIDRNetwork& prefix,
    const folly::IPAddress& nexthop,
    const std::vector<uint32_t>& asPath,
    const std::vector<BgpAttrCommunity>& communities);

/**
 * Construct a BgpUpdate2 withdrawal message
 */
std::unique_ptr<BgpUpdate2> constructWithdrawInBgpUpdate2Format(
    const folly::CIDRNetwork& prefix,
    bool toV2 = false);

/**
 * Construct a BgpEndOfRib message
 */
std::unique_ptr<BgpEndOfRib> constructEndOfRib(const BgpUpdateAfi& afi);

} // namespace bgplib
} // namespace nettools
} // namespace facebook
