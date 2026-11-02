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

#include "PolicyMatch.h"
#include "PolicyUtils.h"

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <folly/logging/xlog.h>
#include <stdexcept>
#include "neteng/fboss/bgp/cpp/common/Utils.h"
#include "neteng/fboss/bgp/cpp/policy/base/PolicyUtils.h"

namespace facebook::bgp {

using namespace neteng::fboss::bgp_attr;
using namespace rib_policy;

using routing_policy::BooleanOperator;

CommunityMatch::CommunityMatch(
    const bgp_policy::BgpPolicyAtomicMatch& policyMatch)
    : AttributesMatch(*policyMatch.communities_filter()->name()),
      communityList_(*(policyMatch.communities_filter())) {
  if (communityList_.communities() && !communityList_.communities()->empty()) {
    PopulateCommunities(communityList_);
  } else {
    throw BgpError("The attribute \"communities\" is empty.");
  }
}

CommunityMatch::CommunityMatch(const TCommunityListMatch& listMatch)
    : AttributesMatch(""),
      communityList_(getEmptyCommunityList(*listMatch.boolean_operator())) {
  PopulateCommunities(*listMatch.communities());
  // sort the community string vector for later ease of comparison
  if (areOverlapped(communityStrings_, communityStringsDoNotMatch_)) {
    // We have conflict communities
    throw BgpError(
        fmt::format(
            "Conflict community matches are specified:\n - match: [{}]\n - do not match: [{}]",
            fmt::join(communityStrings_, ", "),
            fmt::join(communityStringsDoNotMatch_, ", ")));
  }
}

void CommunityMatch::PopulateReferences(
    const folly::F14NodeMap<std::string, bgp_policy::CommunityList>&
        communityListMap) {
  if (!communityList_.community_list_names() ||
      communityList_.community_list_names()->empty()) {
    return;
  }
  for (const auto& commListName : *(communityList_.community_list_names())) {
    auto communityListIter = communityListMap.find(commListName);
    if (communityListIter == communityListMap.end()) {
      // TODO: optimization: have a global validation
      throw BgpError("Could not find CommunityList reference: ", commListName);
    }
    const auto& communityListFromRef = communityListIter->second;
    if (*communityList_.boolean_operator() !=
        *communityListFromRef.boolean_operator()) {
      throw BgpError(
          "Conflicting boolean_operator from the reference: ", commListName);
    }
    if (communityListFromRef.communities() &&
        !communityListFromRef.communities()->empty()) {
      PopulateCommunities(communityListFromRef);
    }
  }
}

bgp_policy::CommunityList CommunityMatch::getEmptyCommunityList(
    routing_policy::BooleanOperator booleanOperator) {
  bgp_policy::CommunityList tCommunityList;
  tCommunityList.boolean_operator() = booleanOperator;
  tCommunityList.exact_match() = false;
  return tCommunityList;
}

// Check if the two SORTED sets are overlapped
bool CommunityMatch::areOverlapped(
    const std::set<std::string>& vec1,
    const std::set<std::string>& vec2) {
  auto it1 = vec1.begin();
  auto it2 = vec2.begin();
  while ((it1 != vec1.end()) && (it2 != vec2.end())) {
    if (*it1 == *it2) {
      return true;
    }
    if (*it1 < *it2) {
      ++it1;
    } else {
      ++it2;
    }
  }
  return false;
}

void CommunityMatch::PopulateCommunities(
    const bgp_policy::CommunityList& communityList) {
  for (const auto& comm : *(communityList.communities())) {
    if (parseCommunityStr(comm).hasValue()) {
      communityStrings_.emplace(comm);
      continue;
    }
    // try parse regex expression
    try {
      const auto& commRegex = boost::regex(comm);
      communityRegexs_.emplace_back(commRegex);
    } catch (boost::regex_error&) {
      throw BgpError("Malformed community config: ", comm);
    }
  }
}

void CommunityMatch::PopulateCommunities(
    const std::vector<TBgpCommunityMatch>& communityMatches) {
  // Avoid adding duplicated communities
  for (const auto& commMatch : communityMatches) {
    const TBgpCommunity& comm = *commMatch.community();
    if (commMatch.match_type() ==
        routing_policy::MatchValueLogicOperator::EQUAL) {
      communityStrings_.emplace(
          fmt::format("{}:{}", *comm.asn(), *comm.value()));
    } else {
      communityStringsDoNotMatch_.emplace(
          fmt::format("{}:{}", *comm.asn(), *comm.value()));
    }
  }
}

bool CommunityMatch::Match(
    const std::shared_ptr<const BgpPath>& attrs,
    const std::optional<BgpPolicyMatchData>&) const noexcept {
  // if the announcement does not have any community attribute, return
  if (attrs->getCommunities().nullOrEmpty()) {
    // if communityMatch is also empty, return true
    if (communityRegexs_.empty() && communityStrings_.empty()) {
      return true;
    }
    return false;
  }
  // call MatchAny or MatchAll depending on the boolean_operator
  return (
      *communityList_.boolean_operator() == BooleanOperator::OR
          ? MatchAny(attrs)
          : MatchAll(attrs));
}

bool CommunityMatch::MatchAny(
    const std::shared_ptr<const BgpPath>& attrs) const noexcept {
  // match any of the community strings
  auto communitiesStringsFromAttr =
      ConvertAttrCommunitiesToStringSet(attrs->getCommunities());
  bool exactMatch = communityList_.exact_match()
      ? communityList_.exact_match().value()
      : false;
  bool isMatch = false;
  // check if any community string is matched
  for (const auto& comm : communityStrings_) {
    if (communitiesStringsFromAttr.find(comm) !=
        communitiesStringsFromAttr.end()) {
      isMatch = true;
      break;
    }
  }
  if (!isMatch) {
    // check if any community-string-do-not-match is NOT matched
    for (const auto& comm : communityStringsDoNotMatch_) {
      if (communitiesStringsFromAttr.find(comm) ==
          communitiesStringsFromAttr.end()) {
        isMatch = true;
        break;
      }
    }
  }
  if (!isMatch) {
    // match any of the regexs
    for (const auto& comm : communitiesStringsFromAttr) {
      for (const auto& commRegex : communityRegexs_) {
        if (regex_match(comm, commRegex)) {
          isMatch = true;
          break;
        }
      }
    }
  }
  if (isMatch && exactMatch) {
    return ExactMatch(communitiesStringsFromAttr);
  }
  return isMatch;
}

bool CommunityMatch::MatchAll(
    const std::shared_ptr<const BgpPath>& attrs) const noexcept {
  // match all of the community strings, first sorted
  auto communitiesStringsFromAttr =
      ConvertAttrCommunitiesToStringSet(attrs->getCommunities());

  bool exactMatch = communityList_.exact_match()
      ? communityList_.exact_match().value()
      : false;
  bool isMatch = true;
  /*
   * check if the sorted community strings are all included in
   * the vector of communities from the announcement
   */
  if (!std::includes(
          communitiesStringsFromAttr.begin(),
          communitiesStringsFromAttr.end(),
          communityStrings_.begin(),
          communityStrings_.end())) {
    isMatch = false;
  }
  /*
   * check if the sorted community strings contain any string in
   * communityStringsDoNotMatch_ (also sorted)
   */
  isMatch &=
      (!areOverlapped(communitiesStringsFromAttr, communityStringsDoNotMatch_));
  if (isMatch) {
    for (const auto& commRegex : communityRegexs_) {
      bool foundMatch = false;
      for (const auto& comm : communitiesStringsFromAttr) {
        if (regex_match(comm, commRegex)) {
          foundMatch = true;
          break;
        }
      }
      if (!foundMatch) {
        isMatch = false;
      }
    }
    if (exactMatch) {
      return ExactMatch(communitiesStringsFromAttr);
    }
  }
  return isMatch;
}

bool CommunityMatch::ExactMatch(
    const std::set<std::string>& communitiesStringsFromAttr) const noexcept {
  // Check that every community in the announcement matches the policy.
  for (const auto& comm : communitiesStringsFromAttr) {
    if (communityStrings_.find(comm) != communityStrings_.end()) {
      // The community matched an exact string
      continue;
    }
    bool foundRegexMatch = false;
    for (const auto& commRegex : communityRegexs_) {
      if (regex_match(comm, commRegex)) {
        // The community matched a regular expression
        foundRegexMatch = true;
        break;
      }
    }
    if (foundRegexMatch) {
      continue;
    }
    // The community in the announcement did not match anything in the policy
    return false;
  }
  return true;
}

bool CommunityCountMatch::Match(
    const std::shared_ptr<const BgpPath>& attrs,
    const std::optional<BgpPolicyMatchData>&) const noexcept {
  const auto communitiesLen = attrs->getCommunities().nullOrEmpty()
      ? 0
      : attrs->getCommunities()->size();
  auto op = toPolicyComparisonOperator(
      *communityCount_.compare_numeric_value()->compare_operator());
  auto valueToCompare = *communityCount_.compare_numeric_value()->value();
  return routing::CompareNumValue(op, valueToCompare, communitiesLen);
}

bool AsPathLenMatch::Match(
    const std::shared_ptr<const BgpPath>& attrs,
    const std::optional<BgpPolicyMatchData>&) const noexcept {
  const auto asPathLen = attrs->getBgpAsPathLen();
  for (const auto& matchEntry : asPathLengths_) {
    auto op = toPolicyComparisonOperator(*matchEntry.compare_operator());
    auto valueToCompare = *matchEntry.value();
    if (!routing::CompareNumValue(op, valueToCompare, asPathLen)) {
      return false;
    }
  }
  return true;
}

std::vector<routing_policy::CompareNumericValue>
AsPathLenMatch::getAsPathLengths(const TBgpPathMatcher& matcher) {
  std::vector<routing_policy::CompareNumericValue> asPathLengths;
  routing_policy::CompareNumericValue asPathLength;
  if (matcher.as_path_length()) {
    asPathLength.compare_operator() = routing_policy::ComparisonOperator::EQ;
    asPathLength.value() = *matcher.as_path_length();
  } else {
    throw BgpError("The attribute \"as_path_length\" is empty.");
  }
  asPathLengths.push_back(std::move(asPathLength));
  return asPathLengths;
}

bool MinLbwBpsMatch::Match(
    const std::shared_ptr<const BgpPath>& attrs,
    const std::optional<BgpPolicyMatchData>&) const noexcept {
  const auto lbwBytesPerSecond = attrs->getNonTransitiveLbw();
  return lbwBytesPerSecond && ((lbwBytesPerSecond->second * 8) >= minLbwBps_);
}

bool AsPathLenWithConfedMatch::Match(
    const std::shared_ptr<const BgpPath>& attrs,
    const std::optional<BgpPolicyMatchData>&) const noexcept {
  const auto asPathLenWithConfed = attrs->getBgpAsPathLenWithConfed();
  for (const auto& matchEntry : asPathLengthsWithConfed_) {
    auto op = toPolicyComparisonOperator(*matchEntry.compare_operator());
    auto valueToCompare = *matchEntry.value();
    if (!routing::CompareNumValue(op, valueToCompare, asPathLenWithConfed)) {
      return false;
    }
  }
  return true;
}

/**
 * @brief  Match function for ALWAYS match type
 *
 * @param  attrs - BgpPath of a route for which match is being evaluated
 * @param  BgpPolicyMatchData - carries additional match data for match type
 *         both parameters in this case are unused. They are present to simply
 *         match the required function signature
 *
 * @return TRUE always
 */
bool AlwaysMatch::Match(
    const std::shared_ptr<const BgpPath>& /* attrs */, /* unused */
    const std::optional<BgpPolicyMatchData>&) const noexcept {
  return true;
}

bool OriginMatch::Match(
    const std::shared_ptr<const BgpPath>& attrs,
    const std::optional<BgpPolicyMatchData>&) const noexcept {
  const auto origin = attrs->getOrigin();
  switch (origin_) {
    case bgp_policy::Origin::EGP:
      return origin == nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_EGP;
    case bgp_policy::Origin::IGP:
      return origin == nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_IGP;
    case bgp_policy::Origin::INCOMPLETE:
      return origin == nettools::bgplib::BgpAttrOrigin::BGP_ORIGIN_INCOMPLETE;
    default:
      return false;
  }
}

bool WeightMatch::Match(
    const std::shared_ptr<const BgpPath>& attrs,
    const std::optional<BgpPolicyMatchData>&) const noexcept {
  const auto weight = attrs->getWeight();
  auto valueToCompare = *weightComparitor_.value();
  if (!routing::CompareNumValue(weightOperator_, valueToCompare, weight)) {
    return false;
  }
  return true;
}

void PrefixTreeMatch::PopulateReferences(
    const folly::F14NodeMap<std::string, routing_policy::PrefixList>&
        prefixListMap) {
  if (prefixListNames_.empty()) {
    return;
  }
  for (const auto& prefListName : prefixListNames_) {
    auto prefixListIter = prefixListMap.find(prefListName);
    if (prefixListIter == prefixListMap.end()) {
      throw BgpError("Could not find PrefixList reference: ", prefListName);
    }
    auto tree = validateAndCreatePrefixTree(prefixListIter->second);

    for (auto it = tree.begin(); it != tree.end(); it++) {
      radixTree_.insert(it->ipAddress(), it->masklen(), it->value());
    }
    // TODO: have a global validation for reference
  }
}

// Will throw exception upon invalid config
facebook::network::
    RadixTree<folly::IPAddress, std::vector<routing::PrefixLenOrRegexComp>>
    PrefixTreeMatch::validateAndCreatePrefixTree(
        const routing_policy::PrefixList& tPrefixList) {
  if (tPrefixList.compare_operator()) {
    throw BgpError("Unsupported PrefixList configuration: compare_operator");
  }
  if (*tPrefixList.boolean_operator() != routing_policy::BooleanOperator::OR) {
    throw BgpError("PrefixList BooleanOperator can only be OR");
  }
  // merge same prefix together first
  folly::
      F14NodeMap<folly::CIDRNetwork, std::vector<routing::PrefixLenOrRegexComp>>
          mergedPrefixList;
  for (const auto& tPrefixListEntry : *tPrefixList.prefixes()) {
    folly::CIDRNetwork prefix;
    try {
      prefix = folly::IPAddress::createNetwork(*tPrefixListEntry.base_prefix());
    } catch (folly::IPAddressFormatException&) {
      XLOGF(
          ERR,
          "Malformed base prefix in config: {}",
          *tPrefixListEntry.base_prefix());
      throw BgpError(
          "Malformed base prefix in config: ", *tPrefixListEntry.base_prefix());
    }

    if (tPrefixListEntry.seq_num()) {
      throw BgpError("Unsupported Prefix configuration: seq_num");
    }
    if (*tPrefixListEntry.match_logic() !=
        routing_policy::MatchValueLogicOperator::EQUAL) {
      throw BgpError("Unsupported Prefix configuration: match_logic");
    }

    if (tPrefixListEntry.prefix_len_ranges()->empty() &&
        tPrefixListEntry.regex() && !tPrefixListEntry.regex()->empty()) {
      // if prefix len ranges is empty and regex is not empty
      mergedPrefixList[prefix].emplace_back(
          std::make_shared<re2::RE2>(*tPrefixListEntry.regex()));
    } else {
      routing::PrefixLenRanges compVals;
      for (const auto& v : *tPrefixListEntry.prefix_len_ranges()) {
        compVals.emplace_back(
            toPolicyComparisonOperator(*v.compare_operator()), *v.value());
      }
      mergedPrefixList[prefix].emplace_back(compVals);
    }
  }
  facebook::network::
      RadixTree<folly::IPAddress, std::vector<routing::PrefixLenOrRegexComp>>
          radixTree;
  for (const auto& [prefix, compvals] : mergedPrefixList) {
    radixTree.insert(prefix.first, prefix.second, compvals);
  }
  // TODO: support for 6to4 prefixes
  return radixTree;
}

void AsPathMatch::PopulateReferences(
    const folly::F14NodeMap<std::string, bgp_policy::AsPathList>&
        asPathListMap) {
  if (!asPathList_.as_path_list_names() ||
      asPathList_.as_path_list_names()->empty()) {
    return;
  }
  for (const auto& aspListName : *(asPathList_.as_path_list_names())) {
    auto asPathListIter = asPathListMap.find(aspListName);
    if (asPathListIter == asPathListMap.end()) {
      throw BgpError("Could not find AsPathList reference: ", aspListName);
    }
    const auto& asPathListFromRef = asPathListIter->second;
    if (*asPathList_.boolean_operator() !=
        *asPathListFromRef.boolean_operator()) {
      throw BgpError(
          "Conflicting boolean_operator from the reference: ", aspListName);
    }
    if (asPathListFromRef.as_paths() &&
        !asPathListFromRef.as_paths()->empty()) {
      PopulateAsPathRegexs(*(asPathListFromRef.as_paths()));
    }
  }
}

bgp_policy::AsPathList AsPathMatch::getEmptyAsPathList() {
  bgp_policy::AsPathList tAsPathList;
  tAsPathList.boolean_operator() = BooleanOperator::AND;
  return tAsPathList;
}

void AsPathMatch::PopulateAsPathRegexs(
    const std::vector<std::string>& asPaths) {
  for (const auto& asPathInConfig : asPaths) {
    try {
      const auto& asPathReg = boost::regex(asPathInConfig);
      asPathRegexs_.emplace_back(asPathReg);
    } catch (boost::regex_error&) {
      throw BgpError("Malformed regex in aspath config: ", asPathInConfig);
    }
  }
}

bool AsPathMatch::Match(
    const std::shared_ptr<const BgpPath>& attrs,
    const std::optional<BgpPolicyMatchData>&) const noexcept {
  // if asPathRegexs_ is empty, it returns false
  if (asPathRegexs_.empty()) {
    return false;
  }
  /*
   * empty asPath will go be examined as empty string against asPathRegexes
   * call MatchAny or MatchAll depending on the boolean_operator
   */
  return (
      *asPathList_.boolean_operator() == BooleanOperator::OR ? MatchAny(attrs)
                                                             : MatchAll(attrs));
}

/*
 * return true if asPath matches any regex
 * if asPath is empty, it will compare empty string against asPathRegexs_
 */
bool AsPathMatch::MatchAny(const std::shared_ptr<const BgpPath>& attrs) const {
  const auto& asPathsVec = attrs->getFullBgpAsPathAsString();
  for (const auto& asPathRegex : asPathRegexs_) {
    for (const auto& asPathStr : asPathsVec) {
      try {
        if (regex_search(asPathStr, asPathRegex)) {
          return true;
        }
      } catch (const std::exception& e) {
        XLOGF(
            ERR,
            "regex_search threw in MatchAny: {}, asPathStr: {}, asPathRegex: {}",
            e.what(),
            asPathStr,
            asPathRegex.str());
        return false;
      }
    }
  }

  return false;
}

/*
 * return true if asPath matches all the regexes
 * if asPath is empty, it will compare empty string against asPathRegexs_
 */
bool AsPathMatch::MatchAll(const std::shared_ptr<const BgpPath>& attrs) const {
  const auto& asPathsVec = attrs->getFullBgpAsPathAsString();
  for (const auto& asPathRegex : asPathRegexs_) {
    for (const auto& asPathStr : asPathsVec) {
      try {
        if (!regex_search(asPathStr, asPathRegex)) {
          return false;
        }
      } catch (const std::exception& e) {
        XLOGF(
            ERR,
            "regex_search threw in MatchAll: {}, asPathStr: {}, asPathRegex: {}",
            e.what(),
            asPathStr,
            asPathRegex.str());
        return false;
      }
    }
  }
  return true;
}
} // namespace facebook::bgp
